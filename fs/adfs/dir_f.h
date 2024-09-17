/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/fs/adfs/dir_f.h
 *
 *  Copyright (C) 1999 Russell King
 *
 *  Structures of directories on the F format disk
 */
#ifndef ADFS_DIR_F_H
#define ADFS_DIR_F_H

/*
 * Directory header
 */
struct adfs_dirheader {
	__u8 startmasseq;
	__u8 startname[4];
} __attribute__((packed));

#define ADFS_NEWDIR_SIZE	2048
#define ADFS_NUM_DIR_ENTRIES	77

/*
 * Directory entries
 */
struct adfs_direntry {
#define ADFS_F_NAME_LEN 10
	char dirobname[ADFS_F_NAME_LEN];
	__u8 dirload[4];
	__u8 direxec[4];
	__u8 dirlen[4];
	__u8 dirinddiscadd[3];
	__u8 newdiratts;
} __attribute__((packed));

/*
 * Directory tail
 */
struct adfs_olddirtail {
	__u8 dirlastmask;
	char dirname[10];
	__u8 dirparent[3];
	char dirtitle[19];
	__u8 reserved[14];
	__u8 endmasseq;
	__u8 endname[4];
	__u8 dircheckbyte;
} __attribute__((packed));

struct adfs_newdirtail {
	__u8 dirlastmask;
	__u8 reserved[2];
	__u8 dirparent[3];
	char dirtitle[19];
	char dirname[10];
	__u8 endmasseq;
	__u8 endname[4];
	__u8 dircheckbyte;
} __attribute__((packed));

union adfs_dirtail {
	struct adfs_olddirtail old;
	struct adfs_newdirtail new;
};

#endif
