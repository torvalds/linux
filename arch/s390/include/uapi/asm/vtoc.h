/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 * This file contains volume label definitions for DASD devices.
 *
 * Copyright IBM Corp. 2005
 *
 * Author(s): Volker Sameske <sameske@de.ibm.com>
 *
 */

#ifndef _ASM_S390_VTOC_H
#define _ASM_S390_VTOC_H

#include <linux/types.h>

struct vtoc_ttr
{
	__u16 tt;
	__u8 r;
} __attribute__ ((packed));

struct vtoc_cchhb
{
	__u16 cc;
	__u16 hh;
	__u8 b;
} __attribute__ ((packed));

struct vtoc_cchh
{
	__u16 cc;
	__u16 hh;
} __attribute__ ((packed));

struct vtoc_labeldate
{
	__u8 year;
	__u16 day;
} __attribute__ ((packed));

struct vtoc_volume_label_cdl
{
	char volkey[4];		/* volume key = volume label */
	char vollbl[4];		/* volume label */
	char volid[6];		/* volume identifier */
	__u8 security;		/* security byte */
	struct vtoc_cchhb vtoc;	/* VTOC address */
	char res1[5];		/* reserved */
	char cisize[4];		/* CI-size for FBA,... */
				/* ...blanks for CKD */
	char blkperci[4];	/* no of blocks per CI (FBA), blanks for CKD */
	char labperci[4];	/* no of labels per CI (FBA), blanks for CKD */
	char res2[4];		/* reserved */
	char lvtoc[14];		/* owner code for LVTOC */
	char res3[29];		/* reserved */
} __attribute__ ((packed));

struct vtoc_volume_label_ldl {
	char vollbl[4];		/* volume label */
	char volid[6];		/* volume identifier */
	char res3[69];		/* reserved */
	char ldl_version;	/* version number, valid for ldl format */
	__u64 formatted_blocks; /* valid when ldl_version >= f2  */
} __attribute__ ((packed));

struct vtoc_extent
{
	__u8 typeind;			/* extent type indicator */
	__u8 seqno;			/* extent sequence number */
	struct vtoc_cchh llimit;	/* starting point of this extent */
	struct vtoc_cchh ulimit;	/* ending point of this extent */
} __attribute__ ((packed));

struct vtoc_dev_const
{
	__u16 DS4DSCYL;	/* number of logical cyls */
	__u16 DS4DSTRK;	/* number of tracks in a logical cylinder */
	__u16 DS4DEVTK;	/* device track length */
	__u8 DS4DEVI;	/* non-last keyed record overhead */
	__u8 DS4DEVL;	/* last keyed record overhead */
	__u8 DS4DEVK;	/* non-keyed record overhead differential */
	__u8 DS4DEVFG;	/* flag byte */
	__u16 DS4DEVTL;	/* device tolerance */
	__u8 DS4DEVDT;	/* number of DSCB's per track */
	__u8 DS4DEVDB;	/* number of directory blocks per track */
} __attribute__ ((packed));

struct vtoc_format1_label
{
	char DS1DSNAM[44];	/* data set name */
	__u8 DS1FMTID;		/* format identifier */
	char DS1DSSN[6];	/* data set serial number */
	__u16 DS1VOLSQ;		/* volume sequence number */
	struct vtoc_labeldate DS1CREDT; /* creation date: ydd */
	struct vtoc_labeldate DS1EXPDT; /* expiration date */
	__u8 DS1NOEPV;		/* number of extents on volume */
	__u8 DS1NOBDB;		/* no. of bytes used in last direction blk */
	__u8 DS1FLAG1;		/* flag 1 */
	char DS1SYSCD[13];	/* system code */
	struct vtoc_labeldate DS1REFD; /* date last referenced	*/
	__u8 DS1SMSFG;		/* system managed storage indicators */
	__u8 DS1SCXTF;		/* sec. space extension flag byte */
	__u16 DS1SCXTV;		/* secondary space extension value */
	__u8 DS1DSRG1;		/* data set organisation byte 1 */
	__u8 DS1DSRG2;		/* data set organisation byte 2 */
	__u8 DS1RECFM;		/* record format */
	__u8 DS1OPTCD;		/* option code */
	__u16 DS1BLKL;		/* block length */
	__u16 DS1LRECL;		/* record length */
	__u8 DS1KEYL;		/* key length */
	__u16 DS1RKP;		/* relative key position */
	__u8 DS1DSIND;		/* data set indicators */
	__u8 DS1SCAL1;		/* secondary allocation flag byte */
	char DS1SCAL3[3];	/* secondary allocation quantity */
	struct vtoc_ttr DS1LSTAR; /* last used track and block on track */
	__u16 DS1TRBAL;		/* space remaining on last used track */
	__u16 res1;		/* reserved */
	struct vtoc_extent DS1EXT1; /* first extent description */
	struct vtoc_extent DS1EXT2; /* second extent description */
	struct vtoc_extent DS1EXT3; /* third extent description */
	struct vtoc_cchhb DS1PTRDS; /* possible pointer to f2 or f3 DSCB */
} __attribute__ ((packed));

struct vtoc_format4_label
{
	char DS4KEYCD[44];	/* key code for VTOC labels: 44 times 0x04 */
	__u8 DS4IDFMT;		/* format identifier */
	struct vtoc_cchhb DS4HPCHR; /* highest address of a format 1 DSCB */
	__u16 DS4DSREC;		/* number of available DSCB's */
	struct vtoc_cchh DS4HCCHH; /* CCHH of next available alternate track */
	__u16 DS4NOATK;		/* number of remaining alternate tracks */
	__u8 DS4VTOCI;		/* VTOC indicators */
	__u8 DS4NOEXT;		/* number of extents in VTOC */
	__u8 DS4SMSFG;		/* system managed storage indicators */
	__u8 DS4DEVAC;		/* number of alternate cylinders.
				 * Subtract from first two bytes of
				 * DS4DEVSZ to get number of usable
				 * cylinders. can be zero. valid
				 * only if DS4DEVAV on. */
	struct vtoc_dev_const DS4DEVCT;	/* device constants */
	char DS4AMTIM[8];	/* VSAM time stamp */
	char DS4AMCAT[3];	/* VSAM catalog indicator */
	char DS4R2TIM[8];	/* VSAM volume/catalog match time stamp */
	char res1[5];		/* reserved */
	char DS4F6PTR[5];	/* pointer to first format 6 DSCB */
	struct vtoc_extent DS4VTOCE; /* VTOC extent description */
	char res2[10];		/* reserved */
	__u8 DS4EFLVL;		/* extended free-space management level */
	struct vtoc_cchhb DS4EFPTR; /* pointer to extended free-space info */
	char res3;		/* reserved */
	__u32 DS4DCYL;		/* number of logical cyls */
	char res4[2];		/* reserved */
	__u8 DS4DEVF2;		/* device flags */
	char res5;		/* reserved */
} __attribute__ ((packed));

struct vtoc_ds5ext
{
	__u16 t;	/* RTA of the first track of free extent */
	__u16 fc;	/* number of whole cylinders in free ext. */
	__u8 ft;	/* number of remaining free tracks */
} __attribute__ ((packed));

struct vtoc_format5_label
{
	char DS5KEYID[4];	/* key identifier */
	struct vtoc_ds5ext DS5AVEXT; /* first available (free-space) extent. */
	struct vtoc_ds5ext DS5EXTAV[7]; /* seven available extents */
	__u8 DS5FMTID;		/* format identifier */
	struct vtoc_ds5ext DS5MAVET[18]; /* eighteen available extents */
	struct vtoc_cchhb DS5PTRDS; /* pointer to next format5 DSCB */
} __attribute__ ((packed));

struct vtoc_ds7ext
{
	__u32 a; /* starting RTA value */
	__u32 b; /* ending RTA value + 1 */
} __attribute__ ((packed));

struct vtoc_format7_label
{
	char DS7KEYID[4];	/* key identifier */
	struct vtoc_ds7ext DS7EXTNT[5]; /* space for 5 extent descriptions */
	__u8 DS7FMTID;		/* format identifier */
	struct vtoc_ds7ext DS7ADEXT[11]; /* space for 11 extent descriptions */
	char res1[2];		/* reserved */
	struct vtoc_cchhb DS7PTRDS; /* pointer to next FMT7 DSCB */
} __attribute__ ((packed));

struct vtoc_cms_label {
	__u8 label_id[4];		/* Label identifier */
	__u8 vol_id[6];		/* Volid */
	__u16 version_id;		/* Version identifier */
	__u32 block_size;		/* Disk block size */
	__u32 origin_ptr;		/* Disk origin pointer */
	__u32 usable_count;	/* Number of usable cylinders/blocks */
	__u32 formatted_count;	/* Maximum number of formatted cylinders/
				 * blocks */
	__u32 block_count;	/* Disk size in CMS blocks */
	__u32 used_count;		/* Number of CMS blocks in use */
	__u32 fst_size;		/* File Status Table (FST) size */
	__u32 fst_count;		/* Number of FSTs per CMS block */
	__u8 format_date[6];	/* Disk FORMAT date */
	__u8 reserved1[2];
	__u32 disk_offset;	/* Disk offset when reserved*/
	__u32 map_block;		/* Allocation Map Block with next hole */
	__u32 hblk_disp;		/* Displacement into HBLK data of next hole */
	__u32 user_disp;		/* Displacement into user part of Allocation
				 * map */
	__u8 reserved2[4];
	__u8 segment_name[8];	/* Name of shared segment */
} __attribute__ ((packed));

#endif /* _ASM_S390_VTOC_H */
