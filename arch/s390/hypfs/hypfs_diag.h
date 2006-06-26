/*
 *  fs/hypfs/hypfs_diag.h
 *    Hypervisor filesystem for Linux on s390.
 *
 *    Copyright (C) IBM Corp. 2006
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#ifndef _HYPFS_DIAG_H_
#define _HYPFS_DIAG_H_

extern int hypfs_diag_init(void);
extern void hypfs_diag_exit(void);
extern int hypfs_diag_create_files(struct super_block *sb, struct dentry *root);

#endif /* _HYPFS_DIAG_H_ */
