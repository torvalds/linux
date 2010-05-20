/*
 *  arch/s390/hypfs/hypfs.h
 *    Hypervisor filesystem for Linux on s390.
 *
 *    Copyright (C) IBM Corp. 2006
 *    Author(s): Michael Holzheu <holzheu@de.ibm.com>
 */

#ifndef _HYPFS_H_
#define _HYPFS_H_

#include <linux/fs.h>
#include <linux/types.h>
#include <linux/debugfs.h>

#define REG_FILE_MODE    0440
#define UPDATE_FILE_MODE 0220
#define DIR_MODE         0550

extern struct dentry *hypfs_mkdir(struct super_block *sb, struct dentry *parent,
				  const char *name);

extern struct dentry *hypfs_create_u64(struct super_block *sb,
				       struct dentry *dir, const char *name,
				       __u64 value);

extern struct dentry *hypfs_create_str(struct super_block *sb,
				       struct dentry *dir, const char *name,
				       char *string);

/* LPAR Hypervisor */
extern int hypfs_diag_init(void);
extern void hypfs_diag_exit(void);
extern int hypfs_diag_create_files(struct super_block *sb, struct dentry *root);

/* VM Hypervisor */
extern int hypfs_vm_init(void);
extern void hypfs_vm_exit(void);
extern int hypfs_vm_create_files(struct super_block *sb, struct dentry *root);

/* Directory for debugfs files */
extern struct dentry *hypfs_dbfs_dir;
#endif /* _HYPFS_H_ */
