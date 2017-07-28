/*
 * GPL HEADER START
 *
 * DO NOT ALTER OR REMOVE COPYRIGHT NOTICES OR THIS FILE HEADER.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 only,
 * as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 for more details (a copy is included
 * in the LICENSE file that accompanied this code).
 *
 * You should have received a copy of the GNU General Public License
 * version 2 along with this program; If not, see
 * http://www.gnu.org/licenses/gpl-2.0.html
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2007, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 */

#define DEBUG_SUBSYSTEM S_LNET
#include "../../include/linux/lnet/lib-lnet.h"

static int
lolnd_send(struct lnet_ni *ni, void *private, struct lnet_msg *lntmsg)
{
	LASSERT(!lntmsg->msg_routing);
	LASSERT(!lntmsg->msg_target_is_router);

	return lnet_parse(ni, &lntmsg->msg_hdr, ni->ni_nid, lntmsg, 0);
}

static int
lolnd_recv(struct lnet_ni *ni, void *private, struct lnet_msg *lntmsg,
	   int delayed, struct iov_iter *to, unsigned int rlen)
{
	struct lnet_msg *sendmsg = private;

	if (lntmsg) {		   /* not discarding */
		if (sendmsg->msg_iov)
			lnet_copy_iov2iter(to,
					   sendmsg->msg_niov,
					   sendmsg->msg_iov,
					   sendmsg->msg_offset,
					   iov_iter_count(to));
		else
			lnet_copy_kiov2iter(to,
					    sendmsg->msg_niov,
					    sendmsg->msg_kiov,
					    sendmsg->msg_offset,
					    iov_iter_count(to));

		lnet_finalize(ni, lntmsg, 0);
	}

	lnet_finalize(ni, sendmsg, 0);
	return 0;
}

static int lolnd_instanced;

static void
lolnd_shutdown(struct lnet_ni *ni)
{
	CDEBUG(D_NET, "shutdown\n");
	LASSERT(lolnd_instanced);

	lolnd_instanced = 0;
}

static int
lolnd_startup(struct lnet_ni *ni)
{
	LASSERT(ni->ni_lnd == &the_lolnd);
	LASSERT(!lolnd_instanced);
	lolnd_instanced = 1;

	return 0;
}

struct lnet_lnd the_lolnd = {
	/* .lnd_list       = */ {&the_lolnd.lnd_list, &the_lolnd.lnd_list},
	/* .lnd_refcount   = */ 0,
	/* .lnd_type       = */ LOLND,
	/* .lnd_startup    = */ lolnd_startup,
	/* .lnd_shutdown   = */ lolnd_shutdown,
	/* .lnt_ctl        = */ NULL,
	/* .lnd_send       = */ lolnd_send,
	/* .lnd_recv       = */ lolnd_recv,
	/* .lnd_eager_recv = */ NULL,
	/* .lnd_notify     = */ NULL,
	/* .lnd_accept     = */ NULL
};
