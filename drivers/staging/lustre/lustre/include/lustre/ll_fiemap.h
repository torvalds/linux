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
 * Copyright (c) 2008, 2010, Oracle and/or its affiliates. All rights reserved.
 * Use is subject to license terms.
 */
/*
 * This file is part of Lustre, http://www.lustre.org/
 * Lustre is a trademark of Sun Microsystems, Inc.
 *
 * lustre/include/lustre/ll_fiemap.h
 *
 * FIEMAP data structures and flags. This header file will be used until
 * fiemap.h is available in the upstream kernel.
 *
 * Author: Kalpak Shah <kalpak.shah@sun.com>
 * Author: Andreas Dilger <adilger@sun.com>
 */

#ifndef _LUSTRE_FIEMAP_H
#define _LUSTRE_FIEMAP_H



struct ll_fiemap_extent {
	__u64 fe_logical;  /* logical offset in bytes for the start of
			    * the extent from the beginning of the file */
	__u64 fe_physical; /* physical offset in bytes for the start
			    * of the extent from the beginning of the disk */
	__u64 fe_length;   /* length in bytes for this extent */
	__u64 fe_reserved64[2];
	__u32 fe_flags;    /* FIEMAP_EXTENT_* flags for this extent */
	__u32 fe_device;   /* device number for this extent */
	__u32 fe_reserved[2];
};

struct ll_user_fiemap {
	__u64 fm_start;  /* logical offset (inclusive) at
			  * which to start mapping (in) */
	__u64 fm_length; /* logical length of mapping which
			  * userspace wants (in) */
	__u32 fm_flags;  /* FIEMAP_FLAG_* flags for request (in/out) */
	__u32 fm_mapped_extents;/* number of extents that were mapped (out) */
	__u32 fm_extent_count;  /* size of fm_extents array (in) */
	__u32 fm_reserved;
	struct ll_fiemap_extent fm_extents[0]; /* array of mapped extents (out) */
};

#define FIEMAP_MAX_OFFSET      (~0ULL)

#define FIEMAP_FLAG_SYNC	 0x00000001 /* sync file data before map */
#define FIEMAP_FLAG_XATTR	0x00000002 /* map extended attribute tree */

#define FIEMAP_EXTENT_LAST	      0x00000001 /* Last extent in file. */
#define FIEMAP_EXTENT_UNKNOWN	   0x00000002 /* Data location unknown. */
#define FIEMAP_EXTENT_DELALLOC	  0x00000004 /* Location still pending.
						    * Sets EXTENT_UNKNOWN. */
#define FIEMAP_EXTENT_ENCODED	   0x00000008 /* Data can not be read
						    * while fs is unmounted */
#define FIEMAP_EXTENT_DATA_ENCRYPTED    0x00000080 /* Data is encrypted by fs.
						    * Sets EXTENT_NO_DIRECT. */
#define FIEMAP_EXTENT_NOT_ALIGNED       0x00000100 /* Extent offsets may not be
						    * block aligned. */
#define FIEMAP_EXTENT_DATA_INLINE       0x00000200 /* Data mixed with metadata.
						    * Sets EXTENT_NOT_ALIGNED.*/
#define FIEMAP_EXTENT_DATA_TAIL	 0x00000400 /* Multiple files in block.
						    * Sets EXTENT_NOT_ALIGNED.*/
#define FIEMAP_EXTENT_UNWRITTEN	 0x00000800 /* Space allocated, but
						    * no data (i.e. zero). */
#define FIEMAP_EXTENT_MERGED	    0x00001000 /* File does not natively
						    * support extents. Result
						    * merged for efficiency. */


static inline size_t fiemap_count_to_size(size_t extent_count)
{
	return (sizeof(struct ll_user_fiemap) + extent_count *
					       sizeof(struct ll_fiemap_extent));
}

static inline unsigned fiemap_size_to_count(size_t array_size)
{
	return ((array_size - sizeof(struct ll_user_fiemap)) /
					       sizeof(struct ll_fiemap_extent));
}

#define FIEMAP_FLAG_DEVICE_ORDER 0x40000000 /* return device ordered mapping */

#ifdef FIEMAP_FLAGS_COMPAT
#undef FIEMAP_FLAGS_COMPAT
#endif

/* Lustre specific flags - use a high bit, don't conflict with upstream flag */
#define FIEMAP_EXTENT_NO_DIRECT	 0x40000000 /* Data mapping undefined */
#define FIEMAP_EXTENT_NET	       0x80000000 /* Data stored remotely.
						    * Sets NO_DIRECT flag */

#endif /* _LUSTRE_FIEMAP_H */
