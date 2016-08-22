/*
 * ircd-ratbox: an advanced Internet Relay Chat Daemon(ircd).
 * monitor.c - Code for server-side notify lists
 *
 * Copyright (C) 2005 Lee Hardy <lee -at- leeh.co.uk>
 * Copyright (C) 2005-2012 ircd-ratbox development team
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are
 * met:
 *
 * 1.Redistributions of source code must retain the above copyright notice,
 *   this list of conditions and the following disclaimer.
 * 2.Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in the
 *   documentation and/or other materials provided with the distribution.
 * 3.The name of the author may not be used to endorse or promote products
 *   derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING
 * IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 * $Id: monitor.c 28697 2015-10-02 20:57:42Z androsyn $
 */
#include <stdinc.h>
#include <struct.h>
#include <monitor.h>
#include <hash.h>
#include <numeric.h>
#include <ircd.h>
#include <match.h>
#include <send.h>

#define MONITOR_HASH_BITS 16
#define MONITOR_HASH_SIZE (1<<MONITOR_HASH_BITS)

static rb_dlink_list monitorTable[MONITOR_HASH_SIZE];

static inline unsigned int
hash_monitor_nick(const char *name)
{
	return fnv_hash_upper((const unsigned char *)name, MONITOR_HASH_BITS, 0);
}

struct monitor *
find_monitor(const char *name, int add)
{
	struct monitor *monptr;
	rb_dlink_node *ptr;

	uint32_t hashv = hash_monitor_nick(name);

	RB_DLINK_FOREACH(ptr, monitorTable[hashv].head)
	{
		monptr = ptr->data;
		if(!irccmp(monptr->name, name))
			return monptr;
	
	}

	if(add != 0)
	{
		monptr = rb_malloc(sizeof(struct monitor));
		monptr->name = rb_strdup(name);
		monptr->hashv = hashv;
		rb_dlinkAdd(monptr, &monptr->node, &monitorTable[hashv]);
		return monptr;
	}

	return NULL;
}

void
free_monitor(struct monitor *monptr)
{
	/* don't free if there are users attached */
	if(rb_dlink_list_length(&monptr->users) > 0)
		return;

	rb_dlinkDelete(&monptr->node, &monitorTable[monptr->hashv]);	
	rb_free(monptr->name);
	rb_free(monptr);
}


/* monitor_signon()
 *
 * inputs	- client who has just connected
 * outputs	-
 * side effects	- notifies any clients monitoring this nickname that it has
 *		  connected to the network
 */
void
monitor_signon(struct Client *client_p)
{
	char buf[USERHOST_REPLYLEN];
	struct monitor *monptr;
	
	monptr = find_monitor(client_p->name, 0);

	/* no watchers watching this nick */
	if(monptr == NULL)
		return;

	snprintf(buf, sizeof(buf), "%s!%s@%s", client_p->name, client_p->username, client_p->host);

	sendto_monitor(monptr, form_str(RPL_MONONLINE), me.name, "*", buf);
}

/* monitor_signoff()
 *
 * inputs	- client who is exiting
 * outputs	-
 * side effects	- notifies any clients monitoring this nickname that it has
 *		  left the network
 */
void
monitor_signoff(struct Client *client_p)
{
	struct monitor *monptr;

	monptr  = find_monitor(client_p->name, 0);

	/* noones watching this nick */
	if(monptr == NULL)
		return;

	sendto_monitor(monptr, form_str(RPL_MONOFFLINE), me.name, "*", client_p->name);
}


void
clear_monitor(struct Client *client_p)
{
	struct monitor *monptr;
	rb_dlink_node *ptr, *next_ptr;

	RB_DLINK_FOREACH_SAFE(ptr, next_ptr, client_p->localClient->monitor_list.head)
	{
		monptr = ptr->data;
		rb_dlinkFindDestroy(client_p, &monptr->users);

		/* free the rb_dlink_node allocated in add_monitor -
		 * we don't use rb_dlinkDestory here as we are clearing the list anyways.. */
		rb_free_rb_dlink_node(ptr); 

		free_monitor(monptr); /* this checks if monptr is still in use */
	}

	client_p->localClient->monitor_list.head = client_p->localClient->monitor_list.tail = NULL;
	client_p->localClient->monitor_list.length = 0;
}


