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
	unsigned char startmasseq;
	unsigned char startname[4];
};

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
};

/*
 * Directory tail
 */
union adfs_dirtail {
	struct {
		unsigned char dirlastmask;
		char dirname[10];
		unsigned char dirparent[3];
		char dirtitle[19];
		unsigned char reserved[14];
		unsigned char endmasseq;
		unsigned char endname[4];
		unsigned char dircheckbyte;
	} old;
	struct {
		unsigned char dirlastmask;
		unsigned char reserved[2];
		unsigned char dirparent[3];
		char dirtitle[19];
		char dirname[10];
		unsigned char endmasseq;
		unsigned char endname[4];
		unsigned char dircheckbyte;
	} new;
};

#endif
