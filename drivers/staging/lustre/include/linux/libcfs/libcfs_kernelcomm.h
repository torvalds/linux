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
 * http://www.sun.com/software/products/lustre/docs/GPLv2.pdf
 *
 * Please contact Sun Microsystems, Inc., 4150 Network Circle, Santa Clara,
 * CA 95054 USA or visit www.sun.com if you need additional information or
 * have any questions.
 *
 * GPL HEADER END
 */
/*
 * Copyright (c) 2009, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * Author: Nathan Rutman <nathan.rutman@sun.com>
 *
 * libcfs/include/libcfs/libcfs_kernelcomm.h
 *
 * Kernel <-> userspace communication routines.
 * The definitions below are used in the kernel and userspace.
 *
 */

#ifndef __LIBCFS_KERNELCOMM_H__
#define __LIBCFS_KERNELCOMM_H__

#ifndef __LIBCFS_LIBCFS_H__
#error Do not #include this file directly. #include <linux/libcfs/libcfs.h> instead
#endif

/* KUC message header.
 * All current and future KUC messages should use this header.
 * To avoid having to include Lustre headers from libcfs, define this here.
 */
struct kuc_hdr {
	__u16 kuc_magic;
	__u8  kuc_transport;  /* Each new Lustre feature should use a different
				 transport */
	__u8  kuc_flags;
	__u16 kuc_msgtype;    /* Message type or opcode, transport-specific */
	__u16 kuc_msglen;     /* Including header */
} __attribute__((aligned(sizeof(__u64))));

#define KUC_CHANGELOG_MSG_MAXSIZE (sizeof(struct kuc_hdr)+CR_MAXSIZE)

#define KUC_MAGIC  0x191C /*Lustre9etLinC */
#define KUC_FL_BLOCK 0x01   /* Wait for send */

/* kuc_msgtype values are defined in each transport */
enum kuc_transport_type {
	KUC_TRANSPORT_GENERIC   = 1,
	KUC_TRANSPORT_HSM       = 2,
	KUC_TRANSPORT_CHANGELOG = 3,
};

enum kuc_generic_message_type {
	KUC_MSG_SHUTDOWN = 1,
};

/* prototype for callback function on kuc groups */
typedef int (*libcfs_kkuc_cb_t)(__u32 data, void *cb_arg);

/* KUC Broadcast Groups. This determines which userspace process hears which
 * messages.  Mutliple transports may be used within a group, or multiple
 * groups may use the same transport.  Broadcast
 * groups need not be used if e.g. a UID is specified instead;
 * use group 0 to signify unicast.
 */
#define KUC_GRP_HSM	   0x02
#define KUC_GRP_MAX	   KUC_GRP_HSM

/* Kernel methods */
int libcfs_kkuc_msg_put(struct file *fp, void *payload);
int libcfs_kkuc_group_put(int group, void *payload);
int libcfs_kkuc_group_add(struct file *fp, int uid, int group,
				 __u32 data);
int libcfs_kkuc_group_rem(int uid, int group);
int libcfs_kkuc_group_foreach(int group, libcfs_kkuc_cb_t cb_func,
				     void *cb_arg);

#define LK_FLG_STOP 0x01

/* kernelcomm control structure, passed from userspace to kernel */
typedef struct lustre_kernelcomm {
	__u32 lk_wfd;
	__u32 lk_rfd;
	__u32 lk_uid;
	__u32 lk_group;
	__u32 lk_data;
	__u32 lk_flags;
} __attribute__((packed)) lustre_kernelcomm;

/* Userspace methods */
int libcfs_ukuc_start(lustre_kernelcomm *l, int groups);
int libcfs_ukuc_stop(lustre_kernelcomm *l);
int libcfs_ukuc_msg_get(lustre_kernelcomm *l, char *buf, int maxsize,
			       int transport);

#endif /* __LIBCFS_KERNELCOMM_H__ */
