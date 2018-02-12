// SPDX-License-Identifier: GPL-2.0
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
 *
 * Copyright (c) 2011, 2012, Intel Corporation.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/include/lustre_disk.h
 *
 * Lustre disk format definitions.
 *
 * Author: Nathan Rutman <nathan@clusterfs.com>
 */

#ifndef _LUSTRE_DISK_H
#define _LUSTRE_DISK_H

/** \defgroup disk disk
 *
 * @{
 */

#include <asm/byteorder.h>
#include <linux/types.h>
#include <linux/backing-dev.h>
#include <linux/libcfs/libcfs.h>

/****************** persistent mount data *********************/

#define LDD_F_SV_TYPE_MDT   0x0001
#define LDD_F_SV_TYPE_OST   0x0002
#define LDD_F_SV_TYPE_MGS   0x0004
#define LDD_F_SV_TYPE_MASK (LDD_F_SV_TYPE_MDT  | \
			    LDD_F_SV_TYPE_OST  | \
			    LDD_F_SV_TYPE_MGS)
#define LDD_F_SV_ALL	0x0008

/****************** mount command *********************/

/* The lmd is only used internally by Lustre; mount simply passes
 * everything as string options
 */

#define LMD_MAGIC    0xbdacbd03
#define LMD_PARAMS_MAXLEN	4096

/* gleaned from the mount command - no persistent info here */
struct lustre_mount_data {
	__u32      lmd_magic;
	__u32      lmd_flags;	 /* lustre mount flags */
	int	lmd_mgs_failnodes; /* mgs failover node count */
	int	lmd_exclude_count;
	int	lmd_recovery_time_soft;
	int	lmd_recovery_time_hard;
	char      *lmd_dev;	   /* device name */
	char      *lmd_profile;    /* client only */
	char      *lmd_mgssec;	/* sptlrpc flavor to mgs */
	char      *lmd_opts;	/* lustre mount options (as opposed to
				 * _device_ mount options)
				 */
	char      *lmd_params;	/* lustre params */
	__u32     *lmd_exclude; /* array of OSTs to ignore */
	char	*lmd_mgs;	/* MGS nid */
	char	*lmd_osd_type;  /* OSD type */
};

#define LMD_FLG_SERVER		0x0001	/* Mounting a server */
#define LMD_FLG_CLIENT		0x0002	/* Mounting a client */
#define LMD_FLG_ABORT_RECOV	0x0008	/* Abort recovery */
#define LMD_FLG_NOSVC		0x0010	/* Only start MGS/MGC for servers,
					 * no other services
					 */
#define LMD_FLG_NOMGS		0x0020	/* Only start target for servers,
					 * reusing existing MGS services
					 */
#define LMD_FLG_WRITECONF	0x0040	/* Rewrite config log */
#define LMD_FLG_NOIR		0x0080	/* NO imperative recovery */
#define LMD_FLG_NOSCRUB		0x0100	/* Do not trigger scrub automatically */
#define LMD_FLG_MGS		0x0200	/* Also start MGS along with server */
#define LMD_FLG_IAM		0x0400	/* IAM dir */
#define LMD_FLG_NO_PRIMNODE	0x0800	/* all nodes are service nodes */
#define LMD_FLG_VIRGIN		0x1000	/* the service registers first time */
#define LMD_FLG_UPDATE		0x2000	/* update parameters */
#define LMD_FLG_HSM		0x4000	/* Start coordinator */

#define lmd_is_client(x) ((x)->lmd_flags & LMD_FLG_CLIENT)

/****************** superblock additional info *********************/

struct ll_sb_info;

struct lustre_sb_info {
	int		       lsi_flags;
	struct obd_device	*lsi_mgc;     /* mgc obd */
	struct lustre_mount_data *lsi_lmd;     /* mount command info */
	struct ll_sb_info	*lsi_llsbi;   /* add'l client sbi info */
	struct dt_device	 *lsi_dt_dev;  /* dt device to access disk fs*/
	atomic_t	      lsi_mounts;  /* references to the srv_mnt */
	char			  lsi_svname[MTI_NAME_MAXLEN];
	char			  lsi_osd_obdname[64];
	char			  lsi_osd_uuid[64];
	struct obd_export	 *lsi_osd_exp;
	char			  lsi_osd_type[16];
	char			  lsi_fstype[16];
};

#define LSI_UMOUNT_FAILOVER	      0x00200000

#define     s2lsi(sb)	((struct lustre_sb_info *)((sb)->s_fs_info))
#define     s2lsi_nocast(sb) ((sb)->s_fs_info)

#define     get_profile_name(sb)   (s2lsi(sb)->lsi_lmd->lmd_profile)

/****************** prototypes *********************/

/* obd_mount.c */

int lustre_start_mgc(struct super_block *sb);
void lustre_register_super_ops(struct module *mod,
			       int (*cfs)(struct super_block *sb),
			       void (*ksc)(struct super_block *sb));
int lustre_common_put_super(struct super_block *sb);

int mgc_fsname2resid(char *fsname, struct ldlm_res_id *res_id, int type);

/** @} disk */

#endif /* _LUSTRE_DISK_H */
