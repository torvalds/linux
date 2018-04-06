/* AFS fileserver XDR types
 *
 * Copyright (C) 2018 Red Hat, Inc. All Rights Reserved.
 * Written by David Howells (dhowells@redhat.com)
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public Licence
 * as published by the Free Software Foundation; either version
 * 2 of the Licence, or (at your option) any later version.
 */

#ifndef XDR_FS_H
#define XDR_FS_H

struct afs_xdr_AFSFetchStatus {
	__be32	if_version;
#define AFS_FSTATUS_VERSION	1
	__be32	type;
	__be32	nlink;
	__be32	size_lo;
	__be32	data_version_lo;
	__be32	author;
	__be32	owner;
	__be32	caller_access;
	__be32	anon_access;
	__be32	mode;
	__be32	parent_vnode;
	__be32	parent_unique;
	__be32	seg_size;
	__be32	mtime_client;
	__be32	mtime_server;
	__be32	group;
	__be32	sync_counter;
	__be32	data_version_hi;
	__be32	lock_count;
	__be32	size_hi;
	__be32	abort_code;
} __packed;

#define AFS_DIR_HASHTBL_SIZE	128
#define AFS_DIR_DIRENT_SIZE	32
#define AFS_DIR_SLOTS_PER_BLOCK	64
#define AFS_DIR_BLOCK_SIZE	2048
#define AFS_DIR_BLOCKS_PER_PAGE	(PAGE_SIZE / AFS_DIR_BLOCK_SIZE)
#define AFS_DIR_MAX_SLOTS	65536
#define AFS_DIR_BLOCKS_WITH_CTR	128
#define AFS_DIR_MAX_BLOCKS	1023
#define AFS_DIR_RESV_BLOCKS	1
#define AFS_DIR_RESV_BLOCKS0	13

/*
 * Directory entry structure.
 */
union afs_dirent {
	struct {
		uint8_t		valid;
		uint8_t		unused[1];
		__be16		hash_next;
		__be32		vnode;
		__be32		unique;
		uint8_t		name[16];
		uint8_t		overflow[4];	/* if any char of the name (inc
						 * NUL) reaches here, consume
						 * the next dirent too */
	} u;
	uint8_t	extended_name[32];
};

/*
 * Directory page header (one at the beginning of every 2048-byte chunk).
 */
struct afs_dir_pagehdr {
	__be16		npages;
	__be16		magic;
#define AFS_DIR_MAGIC htons(1234)
	uint8_t		reserved;
	uint8_t		bitmap[8];
	uint8_t		pad[19];
};

/*
 * Directory block layout
 */
union afs_dir_block {
	struct afs_dir_pagehdr	pagehdr;

	struct {
		struct afs_dir_pagehdr	pagehdr;
		uint8_t			alloc_ctrs[AFS_DIR_MAX_BLOCKS];
		__be16			hashtable[AFS_DIR_HASHTBL_SIZE];
	} hdr;

	union afs_dirent	dirents[AFS_DIR_SLOTS_PER_BLOCK];
};

/*
 * Directory layout on a linux VM page.
 */
struct afs_dir_page {
	union afs_dir_block	blocks[AFS_DIR_BLOCKS_PER_PAGE];
};

#endif /* XDR_FS_H */
