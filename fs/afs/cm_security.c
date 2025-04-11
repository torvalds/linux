// SPDX-License-Identifier: GPL-2.0-or-later
/* Cache manager security.
 *
 * Copyright (C) 2025 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 */

#include <linux/slab.h>
#include <crypto/krb5.h>
#include "internal.h"
#include "afs_fs.h"
#include "protocol_yfs.h"
#define RXRPC_TRACE_ONLY_DEFINE_ENUMS
#include <trace/events/rxrpc.h>

/*
 * Respond to an RxGK challenge, adding appdata.
 */
static int afs_respond_to_challenge(struct sk_buff *challenge)
{
#ifdef CONFIG_RXGK
	struct krb5_buffer appdata = {};
#endif
	struct rxrpc_peer *peer;
	unsigned long peer_data;
	u16 service_id;
	u8 security_index;

	rxrpc_kernel_query_challenge(challenge, &peer, &peer_data,
				     &service_id, &security_index);

	_enter("%u,%u", service_id, security_index);

	switch (service_id) {
		/* We don't send CM_SERVICE RPCs, so don't expect a challenge
		 * therefrom.
		 */
	case FS_SERVICE:
	case VL_SERVICE:
	case YFS_FS_SERVICE:
	case YFS_VL_SERVICE:
		break;
	default:
		pr_warn("Can't respond to unknown challenge %u:%u",
			service_id, security_index);
		return rxrpc_kernel_reject_challenge(challenge, RX_USER_ABORT, -EPROTO,
						     afs_abort_unsupported_sec_class);
	}

	switch (security_index) {
#ifdef CONFIG_RXKAD
	case RXRPC_SECURITY_RXKAD:
		return rxkad_kernel_respond_to_challenge(challenge);
#endif

#ifdef CONFIG_RXGK
	case RXRPC_SECURITY_RXGK:
	case RXRPC_SECURITY_YFS_RXGK:
		return rxgk_kernel_respond_to_challenge(challenge, &appdata);
#endif

	default:
		return rxrpc_kernel_reject_challenge(challenge, RX_USER_ABORT, -EPROTO,
						     afs_abort_unsupported_sec_class);
	}
}

/*
 * Process the OOB message queue, processing challenge packets.
 */
void afs_process_oob_queue(struct work_struct *work)
{
	struct afs_net *net = container_of(work, struct afs_net, rx_oob_work);
	struct sk_buff *oob;
	enum rxrpc_oob_type type;

	while ((oob = rxrpc_kernel_dequeue_oob(net->socket, &type))) {
		switch (type) {
		case RXRPC_OOB_CHALLENGE:
			afs_respond_to_challenge(oob);
			break;
		}
		rxrpc_kernel_free_oob(oob);
	}
}
