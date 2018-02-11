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
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 *
 * Copyright (c) 2013, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 *
 * Author: Nathan Rutman <nathan.rutman@sun.com>
 *
 * Kernel <-> userspace communication routines.
 * The definitions below are used in the kernel and userspace.
 */

#ifndef __UAPI_LUSTRE_KERNELCOMM_H__
#define __UAPI_LUSTRE_KERNELCOMM_H__

#include <linux/types.h>

/* KUC message header.
 * All current and future KUC messages should use this header.
 * To avoid having to include Lustre headers from libcfs, define this here.
 */
struct kuc_hdr {
	__u16 kuc_magic;
	/* Each new Lustre feature should use a different transport */
	__u8  kuc_transport;
	__u8  kuc_flags;
	/* Message type or opcode, transport-specific */
	__u16 kuc_msgtype;
	/* Including header */
	__u16 kuc_msglen;
} __aligned(sizeof(__u64));

#define KUC_CHANGELOG_MSG_MAXSIZE (sizeof(struct kuc_hdr) + CR_MAXSIZE)

#define KUC_MAGIC		0x191C /*Lustre9etLinC */

/* kuc_msgtype values are defined in each transport */
enum kuc_transport_type {
	KUC_TRANSPORT_GENERIC	= 1,
	KUC_TRANSPORT_HSM	= 2,
	KUC_TRANSPORT_CHANGELOG	= 3,
};

enum kuc_generic_message_type {
	KUC_MSG_SHUTDOWN	= 1,
};

/* KUC Broadcast Groups. This determines which userspace process hears which
 * messages.  Mutliple transports may be used within a group, or multiple
 * groups may use the same transport.  Broadcast
 * groups need not be used if e.g. a UID is specified instead;
 * use group 0 to signify unicast.
 */
#define KUC_GRP_HSM	0x02
#define KUC_GRP_MAX	KUC_GRP_HSM

#define LK_FLG_STOP 0x01
#define LK_NOFD -1U

/* kernelcomm control structure, passed from userspace to kernel */
struct lustre_kernelcomm {
	__u32 lk_wfd;
	__u32 lk_rfd;
	__u32 lk_uid;
	__u32 lk_group;
	__u32 lk_data;
	__u32 lk_flags;
} __packed;

#endif	/* __UAPI_LUSTRE_KERNELCOMM_H__ */
