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

/* >>> #include <gru/grukservices.h> */
/* >>> uv_gpa() is defined in <gru/grukservices.h> */
#define uv_gpa(_a)		((unsigned long)_a)

/* >>> temporarily define next three items for xpc.h */
#define	SGI_XPC_ACTIVATE	23
#define	SGI_XPC_NOTIFY		24
#define sn_send_IPI_phys(_a, _b, _c, _d)

#include "xpc.h"

static void *xpc_activate_mq;

static enum xp_retval
xpc_rsvd_page_init_uv(struct xpc_rsvd_page *rp)
{
	/* >>> need to have established xpc_activate_mq earlier */
	rp->sn.activate_mq_gpa = uv_gpa(xpc_activate_mq);
	return xpSuccess;
}

/*
 * Setup the infrastructure necessary to support XPartition Communication
 * between the specified remote partition and the local one.
 */
static enum xp_retval
xpc_setup_infrastructure_uv(struct xpc_partition *part)
{
	/* >>> this function needs fleshing out */
	return xpUnsupported;
}

/*
 * Teardown the infrastructure necessary to support XPartition Communication
 * between the specified remote partition and the local one.
 */
static void
xpc_teardown_infrastructure_uv(struct xpc_partition *part)
{
	/* >>> this function needs fleshing out */
	return;
}

static enum xp_retval
xpc_make_first_contact_uv(struct xpc_partition *part)
{
	/* >>> this function needs fleshing out */
	return xpUnsupported;
}

static u64
xpc_get_IPI_flags_uv(struct xpc_partition *part)
{
	/* >>> this function needs fleshing out */
	return 0UL;
}

static struct xpc_msg *
xpc_get_deliverable_msg_uv(struct xpc_channel *ch)
{
	/* >>> this function needs fleshing out */
	return NULL;
}

void
xpc_init_uv(void)
{
	xpc_rsvd_page_init = xpc_rsvd_page_init_uv;
	xpc_setup_infrastructure = xpc_setup_infrastructure_uv;
	xpc_teardown_infrastructure = xpc_teardown_infrastructure_uv;
	xpc_make_first_contact = xpc_make_first_contact_uv;
	xpc_get_IPI_flags = xpc_get_IPI_flags_uv;
	xpc_get_deliverable_msg = xpc_get_deliverable_msg_uv;
}

void
xpc_exit_uv(void)
{
}
