/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * This file describes the layout of the file handles as passed
 * over the wire.
 *
 * Copyright (C) 1995, 1996, 1997 Olaf Kirch <okir@monad.swb.de>
 */

#ifndef _LINUX_NFSD_FH_H
#define _LINUX_NFSD_FH_H

#include <linux/types.h>
#include <linux/nfs.h>
#include <linux/nfs2.h>
#include <linux/nfs3.h>
#include <linux/nfs4.h>

/*
 * This is the old "dentry style" Linux NFSv2 file handle.
 *
 * The xino and xdev fields are currently used to transport the
 * ino/dev of the exported inode.
 */
struct nfs_fhbase_old {
	__u32		fb_dcookie;	/* dentry cookie - always 0xfeebbaca */
	__u32		fb_ino;		/* our inode number */
	__u32		fb_dirino;	/* dir inode number, 0 for directories */
	__u32		fb_dev;		/* our device */
	__u32		fb_xdev;
	__u32		fb_xino;
	__u32		fb_generation;
};

/*
 * This is the new flexible, extensible style NFSv2/v3/v4 file handle.
 *
 * The file handle starts with a sequence of four-byte words.
 * The first word contains a version number (1) and three descriptor bytes
 * that tell how the remaining 3 variable length fields should be handled.
 * These three bytes are auth_type, fsid_type and fileid_type.
 *
 * All four-byte values are in host-byte-order.
 *
 * The auth_type field is deprecated and must be set to 0.
 *
 * The fsid_type identifies how the filesystem (or export point) is
 *    encoded.
 *  Current values:
 *     0  - 4 byte device id (ms-2-bytes major, ls-2-bytes minor), 4byte inode number
 *        NOTE: we cannot use the kdev_t device id value, because kdev_t.h
 *              says we mustn't.  We must break it up and reassemble.
 *     1  - 4 byte user specified identifier
 *     2  - 4 byte major, 4 byte minor, 4 byte inode number - DEPRECATED
 *     3  - 4 byte device id, encoded for user-space, 4 byte inode number
 *     4  - 4 byte inode number and 4 byte uuid
 *     5  - 8 byte uuid
 *     6  - 16 byte uuid
 *     7  - 8 byte inode number and 16 byte uuid
 *
 * The fileid_type identified how the file within the filesystem is encoded.
 *   The values for this field are filesystem specific, exccept that
 *   filesystems must not use the values '0' or '0xff'. 'See enum fid_type'
 *   in include/linux/exportfs.h for currently registered values.
 */
struct nfs_fhbase_new {
	union {
		struct {
			__u8		fb_version_aux;	/* == 1, even => nfs_fhbase_old */
			__u8		fb_auth_type_aux;
			__u8		fb_fsid_type_aux;
			__u8		fb_fileid_type_aux;
			__u32		fb_auth[1];
			/*	__u32		fb_fsid[0]; floating */
			/*	__u32		fb_fileid[0]; floating */
		};
		struct {
			__u8		fb_version;	/* == 1, even => nfs_fhbase_old */
			__u8		fb_auth_type;
			__u8		fb_fsid_type;
			__u8		fb_fileid_type;
			__u32		fb_auth_flex[]; /* flexible-array member */
		};
	};
};

struct knfsd_fh {
	unsigned int	fh_size;	/* significant for NFSv3.
					 * Points to the current size while building
					 * a new file handle
					 */
	union {
		struct nfs_fhbase_old	fh_old;
		__u32			fh_pad[NFS4_FHSIZE/4];
		struct nfs_fhbase_new	fh_new;
	} fh_base;
};

#define ofh_dcookie		fh_base.fh_old.fb_dcookie
#define ofh_ino			fh_base.fh_old.fb_ino
#define ofh_dirino		fh_base.fh_old.fb_dirino
#define ofh_dev			fh_base.fh_old.fb_dev
#define ofh_xdev		fh_base.fh_old.fb_xdev
#define ofh_xino		fh_base.fh_old.fb_xino
#define ofh_generation		fh_base.fh_old.fb_generation

#define	fh_version		fh_base.fh_new.fb_version
#define	fh_fsid_type		fh_base.fh_new.fb_fsid_type
#define	fh_auth_type		fh_base.fh_new.fb_auth_type
#define	fh_fileid_type		fh_base.fh_new.fb_fileid_type
#define	fh_fsid			fh_base.fh_new.fb_auth_flex

/* Do not use, provided for userspace compatiblity. */
#define	fh_auth			fh_base.fh_new.fb_auth

#endif /* _LINUX_NFSD_FH_H */
