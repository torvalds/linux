/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2008 Silicon Graphics, Inc.  All Rights Reserved.
 */

/*
 * Cross Partition Communication (XPC) uv-based functions.
 *
 *     Architecture specific implementation of common functions.
 *
 */

#include <linux/kernel.h>
#include <asm/uv/uv_hub.h>
#include "../sgi-gru/grukservices.h"
#include "xpc.h"

static DECLARE_BITMAP(xpc_heartbeating_to_mask_uv, XP_MAX_NPARTITIONS_UV);

static void *xpc_activate_mq;

static void
xpc_send_local_activate_IRQ_uv(struct xpc_partition *part)
{
	/*
	 * !!! Make our side think that the remote parition sent an activate
	 * !!! message our way. Also do what the activate IRQ handler would
	 * !!! do had one really been sent.
	 */
}

static enum xp_retval
xpc_rsvd_page_init_uv(struct xpc_rsvd_page *rp)
{
	/* !!! need to have established xpc_activate_mq earlier */
	rp->sn.activate_mq_gpa = uv_gpa(xpc_activate_mq);
	return xpSuccess;
}

static void
xpc_increment_heartbeat_uv(void)
{
	/* !!! send heartbeat msg to xpc_heartbeating_to_mask partids */
}

static void
xpc_heartbeat_init_uv(void)
{
	bitmap_zero(xpc_heartbeating_to_mask_uv, XP_MAX_NPARTITIONS_UV);
	xpc_heartbeating_to_mask = &xpc_heartbeating_to_mask_uv[0];
}

static void
xpc_heartbeat_exit_uv(void)
{
	/* !!! send heartbeat_offline msg to xpc_heartbeating_to_mask partids */
}

static void
xpc_request_partition_activation_uv(struct xpc_rsvd_page *remote_rp,
				    unsigned long remote_rp_pa, int nasid)
{
	short partid = remote_rp->SAL_partid;
	struct xpc_partition *part = &xpc_partitions[partid];

/*
 * !!! Setup part structure with the bits of info we can glean from the rp:
 * !!!	part->remote_rp_pa = remote_rp_pa;
 * !!!	part->sn.uv.activate_mq_gpa = remote_rp->sn.activate_mq_gpa;
 */

	xpc_send_local_activate_IRQ_uv(part);
}

static void
xpc_request_partition_reactivation_uv(struct xpc_partition *part)
{
	xpc_send_local_activate_IRQ_uv(part);
}

/*
 * Setup the infrastructure necessary to support XPartition Communication
 * between the specified remote partition and the local one.
 */
static enum xp_retval
xpc_setup_infrastructure_uv(struct xpc_partition *part)
{
	/* !!! this function needs fleshing out */
	return xpUnsupported;
}

/*
 * Teardown the infrastructure necessary to support XPartition Communication
 * between the specified remote partition and the local one.
 */
static void
xpc_teardown_infrastructure_uv(struct xpc_partition *part)
{
	/* !!! this function needs fleshing out */
	return;
}

static enum xp_retval
xpc_make_first_contact_uv(struct xpc_partition *part)
{
	/* !!! this function needs fleshing out */
	return xpUnsupported;
}

static u64
xpc_get_chctl_all_flags_uv(struct xpc_partition *part)
{
	/* !!! this function needs fleshing out */
	return 0UL;
}

static struct xpc_msg *
xpc_get_deliverable_msg_uv(struct xpc_channel *ch)
{
	/* !!! this function needs fleshing out */
	return NULL;
}

void
xpc_init_uv(void)
{
	xpc_rsvd_page_init = xpc_rsvd_page_init_uv;
	xpc_increment_heartbeat = xpc_increment_heartbeat_uv;
	xpc_heartbeat_init = xpc_heartbeat_init_uv;
	xpc_heartbeat_exit = xpc_heartbeat_exit_uv;
	xpc_request_partition_activation = xpc_request_partition_activation_uv;
	xpc_request_partition_reactivation =
	    xpc_request_partition_reactivation_uv;
	xpc_setup_infrastructure = xpc_setup_infrastructure_uv;
	xpc_teardown_infrastructure = xpc_teardown_infrastructure_uv;
	xpc_make_first_contact = xpc_make_first_contact_uv;
	xpc_get_chctl_all_flags = xpc_get_chctl_all_flags_uv;
	xpc_get_deliverable_msg = xpc_get_deliverable_msg_uv;
}

void
xpc_exit_uv(void)
{
}
