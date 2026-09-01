// SPDX-License-Identifier: GPL-2.0-only

#include "edac_module.h"

static struct dentry *edac_debugfs;

void __init edac_debugfs_init(void)
{
	edac_debugfs = debugfs_create_dir("edac", NULL);
}

void edac_debugfs_exit(void)
{
	debugfs_remove_recursive(edac_debugfs);
}

void edac_create_debugfs_nodes(struct mem_ctl_info *mci)
{
	mci->debugfs = debugfs_create_dir(mci->dev.kobj.name, edac_debugfs);
}

/* Create a toplevel dir under EDAC's debugfs hierarchy */
struct dentry *edac_debugfs_create_dir(const char *dirname)
{
	if (!edac_debugfs)
		return NULL;

	return debugfs_create_dir(dirname, edac_debugfs);
}
EXPORT_SYMBOL_GPL(edac_debugfs_create_dir);

/* Create a toplevel dir under EDAC's debugfs hierarchy with parent @parent */
struct dentry *
edac_debugfs_create_dir_at(const char *dirname, struct dentry *parent)
{
	return debugfs_create_dir(dirname, parent);
}
EXPORT_SYMBOL_GPL(edac_debugfs_create_dir_at);

/*
 * Create a file under EDAC's hierarchy or a sub-hierarchy:
 *
 * @name: file name
 * @mode: file permissions
 * @parent: parent dentry. If NULL, it becomes the toplevel EDAC dir
 * @data: private data of caller
 * @fops: file operations of this file
 */
struct dentry *
edac_debugfs_create_file(const char *name, umode_t mode, struct dentry *parent,
			 void *data, const struct file_operations *fops)
{
	if (!parent)
		parent = edac_debugfs;

	return debugfs_create_file(name, mode, parent, data, fops);
}
EXPORT_SYMBOL_GPL(edac_debugfs_create_file);

/* Wrapper for debugfs_create_x8() */
void edac_debugfs_create_x8(const char *name, umode_t mode,
			    struct dentry *parent, u8 *value)
{
	if (!parent)
		parent = edac_debugfs;

	debugfs_create_x8(name, mode, parent, value);
}
EXPORT_SYMBOL_GPL(edac_debugfs_create_x8);

/* Wrapper for debugfs_create_x16() */
void edac_debugfs_create_x16(const char *name, umode_t mode,
			     struct dentry *parent, u16 *value)
{
	if (!parent)
		parent = edac_debugfs;

	debugfs_create_x16(name, mode, parent, value);
}
EXPORT_SYMBOL_GPL(edac_debugfs_create_x16);

/* Wrapper for debugfs_create_x32() */
void edac_debugfs_create_x32(const char *name, umode_t mode,
			     struct dentry *parent, u32 *value)
{
	if (!parent)
		parent = edac_debugfs;

	debugfs_create_x32(name, mode, parent, value);
}
EXPORT_SYMBOL_GPL(edac_debugfs_create_x32);
