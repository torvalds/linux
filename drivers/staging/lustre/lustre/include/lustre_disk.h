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

#include "../../include/linux/libcfs/libcfs.h"
#include "../../include/linux/lnet/types.h"
#include <linux/backing-dev.h>

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
   everything as string options */

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
	char      *lmd_profile;       /* client only */
	char      *lmd_mgssec;	/* sptlrpc flavor to mgs */
	char      *lmd_opts;	  /* lustre mount options (as opposed to
					 _device_ mount options) */
	char      *lmd_params;	/* lustre params */
	__u32     *lmd_exclude;       /* array of OSTs to ignore */
	char	*lmd_mgs;	   /* MGS nid */
	char	*lmd_osd_type;      /* OSD type */
};

#define LMD_FLG_SERVER		0x0001	/* Mounting a server */
#define LMD_FLG_CLIENT		0x0002	/* Mounting a client */
#define LMD_FLG_ABORT_RECOV	0x0008	/* Abort recovery */
#define LMD_FLG_NOSVC		0x0010	/* Only start MGS/MGC for servers,
					   no other services */
#define LMD_FLG_NOMGS		0x0020	/* Only start target for servers, reusing
					   existing MGS services */
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

/****************** last_rcvd file *********************/

/** version recovery epoch */
#define LR_EPOCH_BITS   32
#define lr_epoch(a) ((a) >> LR_EPOCH_BITS)
#define LR_EXPIRE_INTERVALS 16 /**< number of intervals to track transno */
#define ENOENT_VERSION 1 /** 'virtual' version of non-existent object */

#define LR_SERVER_SIZE   512
#define LR_CLIENT_START 8192
#define LR_CLIENT_SIZE   128
#if LR_CLIENT_START < LR_SERVER_SIZE
#error "Can't have LR_CLIENT_START < LR_SERVER_SIZE"
#endif

/*
 * This limit is arbitrary (131072 clients on x86), but it is convenient to use
 * 2^n * PAGE_CACHE_SIZE * 8 for the number of bits that fit an order-n allocation.
 * If we need more than 131072 clients (order-2 allocation on x86) then this
 * should become an array of single-page pointers that are allocated on demand.
 */
#if (128 * 1024UL) > (PAGE_CACHE_SIZE * 8)
#define LR_MAX_CLIENTS (128 * 1024UL)
#else
#define LR_MAX_CLIENTS (PAGE_CACHE_SIZE * 8)
#endif

/** COMPAT_146: this is an OST (temporary) */
#define OBD_COMPAT_OST	  0x00000002
/** COMPAT_146: this is an MDT (temporary) */
#define OBD_COMPAT_MDT	  0x00000004
/** 2.0 server, interop flag to show server version is changed */
#define OBD_COMPAT_20	   0x00000008

/** MDS handles LOV_OBJID file */
#define OBD_ROCOMPAT_LOVOBJID   0x00000001

/** OST handles group subdirs */
#define OBD_INCOMPAT_GROUPS     0x00000001
/** this is an OST */
#define OBD_INCOMPAT_OST	0x00000002
/** this is an MDT */
#define OBD_INCOMPAT_MDT	0x00000004
/** common last_rvcd format */
#define OBD_INCOMPAT_COMMON_LR  0x00000008
/** FID is enabled */
#define OBD_INCOMPAT_FID	0x00000010
/** Size-on-MDS is enabled */
#define OBD_INCOMPAT_SOM	0x00000020
/** filesystem using iam format to store directory entries */
#define OBD_INCOMPAT_IAM_DIR    0x00000040
/** LMA attribute contains per-inode incompatible flags */
#define OBD_INCOMPAT_LMA	0x00000080
/** lmm_stripe_count has been shrunk from __u32 to __u16 and the remaining 16
 * bits are now used to store a generation. Once we start changing the layout
 * and bumping the generation, old versions expecting a 32-bit lmm_stripe_count
 * will be confused by interpreting stripe_count | gen << 16 as the actual
 * stripe count */
#define OBD_INCOMPAT_LMM_VER    0x00000100
/** multiple OI files for MDT */
#define OBD_INCOMPAT_MULTI_OI   0x00000200

/* Data stored per server at the head of the last_rcvd file.  In le32 order.
   This should be common to filter_internal.h, lustre_mds.h */
struct lr_server_data {
	__u8  lsd_uuid[40];	/* server UUID */
	__u64 lsd_last_transno;    /* last completed transaction ID */
	__u64 lsd_compat14;	/* reserved - compat with old last_rcvd */
	__u64 lsd_mount_count;     /* incarnation number */
	__u32 lsd_feature_compat;  /* compatible feature flags */
	__u32 lsd_feature_rocompat;/* read-only compatible feature flags */
	__u32 lsd_feature_incompat;/* incompatible feature flags */
	__u32 lsd_server_size;     /* size of server data area */
	__u32 lsd_client_start;    /* start of per-client data area */
	__u16 lsd_client_size;     /* size of per-client data area */
	__u16 lsd_subdir_count;    /* number of subdirectories for objects */
	__u64 lsd_catalog_oid;     /* recovery catalog object id */
	__u32 lsd_catalog_ogen;    /* recovery catalog inode generation */
	__u8  lsd_peeruuid[40];    /* UUID of MDS associated with this OST */
	__u32 lsd_osd_index;       /* index number of OST in LOV */
	__u32 lsd_padding1;	/* was lsd_mdt_index, unused in 2.4.0 */
	__u32 lsd_start_epoch;     /* VBR: start epoch from last boot */
	/** transaction values since lsd_trans_table_time */
	__u64 lsd_trans_table[LR_EXPIRE_INTERVALS];
	/** start point of transno table below */
	__u32 lsd_trans_table_time; /* time of first slot in table above */
	__u32 lsd_expire_intervals; /* LR_EXPIRE_INTERVALS */
	__u8  lsd_padding[LR_SERVER_SIZE - 288];
};

/* Data stored per client in the last_rcvd file.  In le32 order. */
struct lsd_client_data {
	__u8  lcd_uuid[40];      /* client UUID */
	__u64 lcd_last_transno; /* last completed transaction ID */
	__u64 lcd_last_xid;     /* xid for the last transaction */
	__u32 lcd_last_result;  /* result from last RPC */
	__u32 lcd_last_data;    /* per-op data (disposition for open &c.) */
	/* for MDS_CLOSE requests */
	__u64 lcd_last_close_transno; /* last completed transaction ID */
	__u64 lcd_last_close_xid;     /* xid for the last transaction */
	__u32 lcd_last_close_result;  /* result from last RPC */
	__u32 lcd_last_close_data;    /* per-op data */
	/* VBR: last versions */
	__u64 lcd_pre_versions[4];
	__u32 lcd_last_epoch;
	/** orphans handling for delayed export rely on that */
	__u32 lcd_first_epoch;
	__u8  lcd_padding[LR_CLIENT_SIZE - 128];
};

/* bug20354: the lcd_uuid for export of clients may be wrong */
static inline void check_lcd(char *obd_name, int index,
			     struct lsd_client_data *lcd)
{
	int length = sizeof(lcd->lcd_uuid);

	if (strnlen((char *)lcd->lcd_uuid, length) == length) {
		lcd->lcd_uuid[length - 1] = '\0';

		LCONSOLE_ERROR("the client UUID (%s) on %s for exports stored in last_rcvd(index = %d) is bad!\n",
			       lcd->lcd_uuid, obd_name, index);
	}
}

/* last_rcvd handling */
static inline void lsd_le_to_cpu(struct lr_server_data *buf,
				 struct lr_server_data *lsd)
{
	int i;

	memcpy(lsd->lsd_uuid, buf->lsd_uuid, sizeof(lsd->lsd_uuid));
	lsd->lsd_last_transno     = le64_to_cpu(buf->lsd_last_transno);
	lsd->lsd_compat14	 = le64_to_cpu(buf->lsd_compat14);
	lsd->lsd_mount_count      = le64_to_cpu(buf->lsd_mount_count);
	lsd->lsd_feature_compat   = le32_to_cpu(buf->lsd_feature_compat);
	lsd->lsd_feature_rocompat = le32_to_cpu(buf->lsd_feature_rocompat);
	lsd->lsd_feature_incompat = le32_to_cpu(buf->lsd_feature_incompat);
	lsd->lsd_server_size      = le32_to_cpu(buf->lsd_server_size);
	lsd->lsd_client_start     = le32_to_cpu(buf->lsd_client_start);
	lsd->lsd_client_size      = le16_to_cpu(buf->lsd_client_size);
	lsd->lsd_subdir_count     = le16_to_cpu(buf->lsd_subdir_count);
	lsd->lsd_catalog_oid      = le64_to_cpu(buf->lsd_catalog_oid);
	lsd->lsd_catalog_ogen     = le32_to_cpu(buf->lsd_catalog_ogen);
	memcpy(lsd->lsd_peeruuid, buf->lsd_peeruuid, sizeof(lsd->lsd_peeruuid));
	lsd->lsd_osd_index	= le32_to_cpu(buf->lsd_osd_index);
	lsd->lsd_padding1	= le32_to_cpu(buf->lsd_padding1);
	lsd->lsd_start_epoch      = le32_to_cpu(buf->lsd_start_epoch);
	for (i = 0; i < LR_EXPIRE_INTERVALS; i++)
		lsd->lsd_trans_table[i] = le64_to_cpu(buf->lsd_trans_table[i]);
	lsd->lsd_trans_table_time = le32_to_cpu(buf->lsd_trans_table_time);
	lsd->lsd_expire_intervals = le32_to_cpu(buf->lsd_expire_intervals);
}

static inline void lsd_cpu_to_le(struct lr_server_data *lsd,
				 struct lr_server_data *buf)
{
	int i;

	memcpy(buf->lsd_uuid, lsd->lsd_uuid, sizeof(buf->lsd_uuid));
	buf->lsd_last_transno     = cpu_to_le64(lsd->lsd_last_transno);
	buf->lsd_compat14	 = cpu_to_le64(lsd->lsd_compat14);
	buf->lsd_mount_count      = cpu_to_le64(lsd->lsd_mount_count);
	buf->lsd_feature_compat   = cpu_to_le32(lsd->lsd_feature_compat);
	buf->lsd_feature_rocompat = cpu_to_le32(lsd->lsd_feature_rocompat);
	buf->lsd_feature_incompat = cpu_to_le32(lsd->lsd_feature_incompat);
	buf->lsd_server_size      = cpu_to_le32(lsd->lsd_server_size);
	buf->lsd_client_start     = cpu_to_le32(lsd->lsd_client_start);
	buf->lsd_client_size      = cpu_to_le16(lsd->lsd_client_size);
	buf->lsd_subdir_count     = cpu_to_le16(lsd->lsd_subdir_count);
	buf->lsd_catalog_oid      = cpu_to_le64(lsd->lsd_catalog_oid);
	buf->lsd_catalog_ogen     = cpu_to_le32(lsd->lsd_catalog_ogen);
	memcpy(buf->lsd_peeruuid, lsd->lsd_peeruuid, sizeof(buf->lsd_peeruuid));
	buf->lsd_osd_index	  = cpu_to_le32(lsd->lsd_osd_index);
	buf->lsd_padding1	  = cpu_to_le32(lsd->lsd_padding1);
	buf->lsd_start_epoch      = cpu_to_le32(lsd->lsd_start_epoch);
	for (i = 0; i < LR_EXPIRE_INTERVALS; i++)
		buf->lsd_trans_table[i] = cpu_to_le64(lsd->lsd_trans_table[i]);
	buf->lsd_trans_table_time = cpu_to_le32(lsd->lsd_trans_table_time);
	buf->lsd_expire_intervals = cpu_to_le32(lsd->lsd_expire_intervals);
}

static inline void lcd_le_to_cpu(struct lsd_client_data *buf,
				 struct lsd_client_data *lcd)
{
	memcpy(lcd->lcd_uuid, buf->lcd_uuid, sizeof (lcd->lcd_uuid));
	lcd->lcd_last_transno       = le64_to_cpu(buf->lcd_last_transno);
	lcd->lcd_last_xid	   = le64_to_cpu(buf->lcd_last_xid);
	lcd->lcd_last_result	= le32_to_cpu(buf->lcd_last_result);
	lcd->lcd_last_data	  = le32_to_cpu(buf->lcd_last_data);
	lcd->lcd_last_close_transno = le64_to_cpu(buf->lcd_last_close_transno);
	lcd->lcd_last_close_xid     = le64_to_cpu(buf->lcd_last_close_xid);
	lcd->lcd_last_close_result  = le32_to_cpu(buf->lcd_last_close_result);
	lcd->lcd_last_close_data    = le32_to_cpu(buf->lcd_last_close_data);
	lcd->lcd_pre_versions[0]    = le64_to_cpu(buf->lcd_pre_versions[0]);
	lcd->lcd_pre_versions[1]    = le64_to_cpu(buf->lcd_pre_versions[1]);
	lcd->lcd_pre_versions[2]    = le64_to_cpu(buf->lcd_pre_versions[2]);
	lcd->lcd_pre_versions[3]    = le64_to_cpu(buf->lcd_pre_versions[3]);
	lcd->lcd_last_epoch	 = le32_to_cpu(buf->lcd_last_epoch);
	lcd->lcd_first_epoch	= le32_to_cpu(buf->lcd_first_epoch);
}

static inline void lcd_cpu_to_le(struct lsd_client_data *lcd,
				 struct lsd_client_data *buf)
{
	memcpy(buf->lcd_uuid, lcd->lcd_uuid, sizeof (lcd->lcd_uuid));
	buf->lcd_last_transno       = cpu_to_le64(lcd->lcd_last_transno);
	buf->lcd_last_xid	   = cpu_to_le64(lcd->lcd_last_xid);
	buf->lcd_last_result	= cpu_to_le32(lcd->lcd_last_result);
	buf->lcd_last_data	  = cpu_to_le32(lcd->lcd_last_data);
	buf->lcd_last_close_transno = cpu_to_le64(lcd->lcd_last_close_transno);
	buf->lcd_last_close_xid     = cpu_to_le64(lcd->lcd_last_close_xid);
	buf->lcd_last_close_result  = cpu_to_le32(lcd->lcd_last_close_result);
	buf->lcd_last_close_data    = cpu_to_le32(lcd->lcd_last_close_data);
	buf->lcd_pre_versions[0]    = cpu_to_le64(lcd->lcd_pre_versions[0]);
	buf->lcd_pre_versions[1]    = cpu_to_le64(lcd->lcd_pre_versions[1]);
	buf->lcd_pre_versions[2]    = cpu_to_le64(lcd->lcd_pre_versions[2]);
	buf->lcd_pre_versions[3]    = cpu_to_le64(lcd->lcd_pre_versions[3]);
	buf->lcd_last_epoch	 = cpu_to_le32(lcd->lcd_last_epoch);
	buf->lcd_first_epoch	= cpu_to_le32(lcd->lcd_first_epoch);
}

static inline __u64 lcd_last_transno(struct lsd_client_data *lcd)
{
	return (lcd->lcd_last_transno > lcd->lcd_last_close_transno ?
		lcd->lcd_last_transno : lcd->lcd_last_close_transno);
}

static inline __u64 lcd_last_xid(struct lsd_client_data *lcd)
{
	return (lcd->lcd_last_xid > lcd->lcd_last_close_xid ?
		lcd->lcd_last_xid : lcd->lcd_last_close_xid);
}

/****************** superblock additional info *********************/

struct ll_sb_info;

struct lustre_sb_info {
	int		       lsi_flags;
	struct obd_device	*lsi_mgc;     /* mgc obd */
	struct lustre_mount_data *lsi_lmd;     /* mount command info */
	struct ll_sb_info	*lsi_llsbi;   /* add'l client sbi info */
	struct dt_device	 *lsi_dt_dev;  /* dt device to access disk fs*/
	struct vfsmount	  *lsi_srv_mnt; /* the one server mount */
	atomic_t	      lsi_mounts;  /* references to the srv_mnt */
	char			  lsi_svname[MTI_NAME_MAXLEN];
	char			  lsi_osd_obdname[64];
	char			  lsi_osd_uuid[64];
	struct obd_export	 *lsi_osd_exp;
	char			  lsi_osd_type[16];
	char			  lsi_fstype[16];
	struct backing_dev_info   lsi_bdi;     /* each client mountpoint needs
						  own backing_dev_info */
};

#define LSI_UMOUNT_FAILOVER	      0x00200000
#define LSI_BDI_INITIALIZED	      0x00400000

#define     s2lsi(sb)	((struct lustre_sb_info *)((sb)->s_fs_info))
#define     s2lsi_nocast(sb) ((sb)->s_fs_info)

#define     get_profile_name(sb)   (s2lsi(sb)->lsi_lmd->lmd_profile)
#define	    get_mount_flags(sb)	   (s2lsi(sb)->lsi_lmd->lmd_flags)
#define	    get_mntdev_name(sb)	   (s2lsi(sb)->lsi_lmd->lmd_dev)

/****************** mount lookup info *********************/

struct lustre_mount_info {
	char		 *lmi_name;
	struct super_block   *lmi_sb;
	struct vfsmount      *lmi_mnt;
	struct list_head	    lmi_list_chain;
};

/****************** prototypes *********************/

/* obd_mount.c */

int lustre_start_mgc(struct super_block *sb);
void lustre_register_client_fill_super(int (*cfs)(struct super_block *sb,
						  struct vfsmount *mnt));
void lustre_register_kill_super_cb(void (*cfs)(struct super_block *sb));
int lustre_common_put_super(struct super_block *sb);

int mgc_fsname2resid(char *fsname, struct ldlm_res_id *res_id, int type);

/** @} disk */

#endif /* _LUSTRE_DISK_H */
