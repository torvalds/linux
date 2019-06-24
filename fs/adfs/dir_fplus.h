/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  linux/fs/adfs/dir_fplus.h
 *
 *  Copyright (C) 1999 Russell King
 *
 *  Structures of directories on the F+ format disk
 */

#define ADFS_FPLUS_NAME_LEN	255

#define BIGDIRSTARTNAME ('S' | 'B' << 8 | 'P' << 16 | 'r' << 24)
#define BIGDIRENDNAME	('o' | 'v' << 8 | 'e' << 16 | 'n' << 24)

struct adfs_bigdirheader {
	__u8	startmasseq;
	__u8	bigdirversion[3];
	__le32	bigdirstartname;
	__le32	bigdirnamelen;
	__le32	bigdirsize;
	__le32	bigdirentries;
	__le32	bigdirnamesize;
	__le32	bigdirparent;
	char	bigdirname[1];
};

struct adfs_bigdirentry {
	__le32	bigdirload;
	__le32	bigdirexec;
	__le32	bigdirlen;
	__le32	bigdirindaddr;
	__le32	bigdirattr;
	__le32	bigdirobnamelen;
	__le32	bigdirobnameptr;
};

struct adfs_bigdirtail {
	__le32	bigdirendname;
	__u8	bigdirendmasseq;
	__u8	reserved[2];
	__u8	bigdircheckbyte;
};
