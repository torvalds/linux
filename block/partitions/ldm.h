// SPDX-License-Identifier: GPL-2.0-or-later
/**
 * ldm - Part of the Linux-NTFS project.
 *
 * Copyright (C) 2001,2002 Richard Russon <ldm@flatcap.org>
 * Copyright (c) 2001-2007 Anton Altaparmakov
 * Copyright (C) 2001,2002 Jakob Kemi <jakob.kemi@telia.com>
 *
 * Documentation is available at http://www.linux-ntfs.org/doku.php?id=downloads 
 */

#ifndef _FS_PT_LDM_H_
#define _FS_PT_LDM_H_

#include <linux/types.h>
#include <linux/list.h>
#include <linux/fs.h>
#include <linux/unaligned.h>
#include <asm/byteorder.h>

struct parsed_partitions;

/* Magic numbers in CPU format. */
#define MAGIC_VMDB	0x564D4442		/* VMDB */
#define MAGIC_VBLK	0x56424C4B		/* VBLK */
#define MAGIC_PRIVHEAD	0x5052495648454144ULL	/* PRIVHEAD */
#define MAGIC_TOCBLOCK	0x544F43424C4F434BULL	/* TOCBLOCK */

/* The defined vblk types. */
#define VBLK_VOL5		0x51		/* Volume,     version 5 */
#define VBLK_CMP3		0x32		/* Component,  version 3 */
#define VBLK_PRT3		0x33		/* Partition,  version 3 */
#define VBLK_DSK3		0x34		/* Disk,       version 3 */
#define VBLK_DSK4		0x44		/* Disk,       version 4 */
#define VBLK_DGR3		0x35		/* Disk Group, version 3 */
#define VBLK_DGR4		0x45		/* Disk Group, version 4 */

/* vblk flags indicating extra information will be present */
#define	VBLK_FLAG_COMP_STRIPE	0x10
#define	VBLK_FLAG_PART_INDEX	0x08
#define	VBLK_FLAG_DGR3_IDS	0x08
#define	VBLK_FLAG_DGR4_IDS	0x08
#define	VBLK_FLAG_VOLU_ID1	0x08
#define	VBLK_FLAG_VOLU_ID2	0x20
#define	VBLK_FLAG_VOLU_SIZE	0x80
#define	VBLK_FLAG_VOLU_DRIVE	0x02

/* size of a vblk's static parts */
#define VBLK_SIZE_HEAD		16
#define VBLK_SIZE_CMP3		22		/* Name and version */
#define VBLK_SIZE_DGR3		12
#define VBLK_SIZE_DGR4		44
#define VBLK_SIZE_DSK3		12
#define VBLK_SIZE_DSK4		45
#define VBLK_SIZE_PRT3		28
#define VBLK_SIZE_VOL5		58

/* component types */
#define COMP_STRIPE		0x01		/* Stripe-set */
#define COMP_BASIC		0x02		/* Basic disk */
#define COMP_RAID		0x03		/* Raid-set */

/* Other constants. */
#define LDM_DB_SIZE		2048		/* Size in sectors (= 1MiB). */

#define OFF_PRIV1		6		/* Offset of the first privhead
						   relative to the start of the
						   device in sectors */

/* Offsets to structures within the LDM Database in sectors. */
#define OFF_PRIV2		1856		/* Backup private headers. */
#define OFF_PRIV3		2047

#define OFF_TOCB1		1		/* Tables of contents. */
#define OFF_TOCB2		2
#define OFF_TOCB3		2045
#define OFF_TOCB4		2046

#define OFF_VMDB		17		/* List of partitions. */

#define LDM_PARTITION		0x42		/* Formerly SFS (Landis). */

#define TOC_BITMAP1		"config"	/* Names of the two defined */
#define TOC_BITMAP2		"log"		/* bitmaps in the TOCBLOCK. */

struct frag {				/* VBLK Fragment handling */
	struct list_head list;
	u32		group;
	u8		num;		/* Total number of records */
	u8		rec;		/* This is record number n */
	u8		map;		/* Which portions are in use */
	u8		data[];
};

/* In memory LDM database structures. */

struct privhead {			/* Offsets and sizes are in sectors. */
	u16	ver_major;
	u16	ver_minor;
	u64	logical_disk_start;
	u64	logical_disk_size;
	u64	config_start;
	u64	config_size;
	uuid_t	disk_id;
};

struct tocblock {			/* We have exactly two bitmaps. */
	u8	bitmap1_name[16];
	u64	bitmap1_start;
	u64	bitmap1_size;
	u8	bitmap2_name[16];
	u64	bitmap2_start;
	u64	bitmap2_size;
};

struct vmdb {				/* VMDB: The database header */
	u16	ver_major;
	u16	ver_minor;
	u32	vblk_size;
	u32	vblk_offset;
	u32	last_vblk_seq;
};

struct vblk_comp {			/* VBLK Component */
	u8	state[16];
	u64	parent_id;
	u8	type;
	u8	children;
	u16	chunksize;
};

struct vblk_dgrp {			/* VBLK Disk Group */
	u8	disk_id[64];
};

struct vblk_disk {			/* VBLK Disk */
	uuid_t	disk_id;
	u8	alt_name[128];
};

struct vblk_part {			/* VBLK Partition */
	u64	start;
	u64	size;			/* start, size and vol_off in sectors */
	u64	volume_offset;
	u64	parent_id;
	u64	disk_id;
	u8	partnum;
};

struct vblk_volu {			/* VBLK Volume */
	u8	volume_type[16];
	u8	volume_state[16];
	u8	guid[16];
	u8	drive_hint[4];
	u64	size;
	u8	partition_type;
};

struct vblk_head {			/* VBLK standard header */
	u32 group;
	u16 rec;
	u16 nrec;
};

struct vblk {				/* Generalised VBLK */
	u8	name[64];
	u64	obj_id;
	u32	sequence;
	u8	flags;
	u8	type;
	union {
		struct vblk_comp comp;
		struct vblk_dgrp dgrp;
		struct vblk_disk disk;
		struct vblk_part part;
		struct vblk_volu volu;
	} vblk;
	struct list_head list;
};

struct ldmdb {				/* Cache of the database */
	struct privhead ph;
	struct tocblock toc;
	struct vmdb     vm;
	struct list_head v_dgrp;
	struct list_head v_disk;
	struct list_head v_volu;
	struct list_head v_comp;
	struct list_head v_part;
};

#endif /* _FS_PT_LDM_H_ */

