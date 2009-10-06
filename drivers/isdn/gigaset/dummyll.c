/*
 * Dummy LL interface for the Gigaset driver
 *
 * Copyright (c) 2009 by Tilman Schmidt <tilman@imap.cc>.
 *
 * =====================================================================
 *	This program is free software; you can redistribute it and/or
 *	modify it under the terms of the GNU General Public License as
 *	published by the Free Software Foundation; either version 2 of
 *	the License, or (at your option) any later version.
 * =====================================================================
 */

#include "gigaset.h"

void gigaset_skb_sent(struct bc_state *bcs, struct sk_buff *skb)
{
}
EXPORT_SYMBOL_GPL(gigaset_skb_sent);

void gigaset_skb_rcvd(struct bc_state *bcs, struct sk_buff *skb)
{
}
EXPORT_SYMBOL_GPL(gigaset_skb_rcvd);

void gigaset_isdn_rcv_err(struct bc_state *bcs)
{
}
EXPORT_SYMBOL_GPL(gigaset_isdn_rcv_err);

int gigaset_isdn_icall(struct at_state_t *at_state)
{
	return ICALL_IGNORE;
}

void gigaset_isdn_connD(struct bc_state *bcs)
{
}

void gigaset_isdn_hupD(struct bc_state *bcs)
{
}

void gigaset_isdn_connB(struct bc_state *bcs)
{
}

void gigaset_isdn_hupB(struct bc_state *bcs)
{
}

void gigaset_isdn_start(struct cardstate *cs)
{
}

void gigaset_isdn_stop(struct cardstate *cs)
{
}

int gigaset_isdn_register(struct cardstate *cs, const char *isdnid)
{
	pr_info("no ISDN subsystem interface\n");
	return 1;
}

void gigaset_isdn_unregister(struct cardstate *cs)
{
}
