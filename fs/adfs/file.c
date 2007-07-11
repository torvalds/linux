/*
 *  linux/fs/adfs/file.c
 *
 * Copyright (C) 1997-1999 Russell King
 * from:
 *
 *  linux/fs/ext2/file.c
 *
 * Copyright (C) 1992, 1993, 1994, 1995
 * Remy Card (card@masi.ibp.fr)
 * Laboratoire MASI - Institut Blaise Pascal
 * Universite Pierre et Marie Curie (Paris VI)
 *
 *  from
 *
 *  linux/fs/minix/file.c
 *
 *  Copyright (C) 1991, 1992  Linus Torvalds
 *
 *  adfs regular file handling primitives           
 */
#include <linux/fs.h>
#include <linux/buffer_head.h>			/* for file_fsync() */
#include <linux/adfs_fs.h>

#include "adfs.h"

const struct file_operations adfs_file_operations = {
	.llseek		= generic_file_llseek,
	.read		= do_sync_read,
	.aio_read	= generic_file_aio_read,
	.mmap		= generic_file_mmap,
	.fsync		= file_fsync,
	.write		= do_sync_write,
	.aio_write	= generic_file_aio_write,
	.splice_read	= generic_file_splice_read,
};

const struct inode_operations adfs_file_inode_operations = {
	.setattr	= adfs_notify_change,
};
