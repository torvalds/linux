// SPDX-License-Identifier: GPL-2.0-only

#include <linux/string_choices.h>

#include "edac_module.h"

static struct dentry *edac_debugfs;

static ssize_t edac_fake_inject_write(struct file *file,
				      const char __user *data,
				      size_t count, loff_t *ppos)
{
	struct device *dev = file->private_data;
	struct mem_ctl_info *mci = to_mci(dev);
	static enum hw_event_mc_err_type type;
	u16 errcount = mci->fake_inject_count;

	if (!errcount)
		errcount = 1;

	type = mci->fake_inject_ue ? HW_EVENT_ERR_UNCORRECTED
				   : HW_EVENT_ERR_CORRECTED;

	printk(KERN_DEBUG
	       "Generating %d %s fake error%s to %d.%d.%d to test core handling. NOTE: this won't test the driver-specific decoding logic.\n",
		errcount,
		(type == HW_EVENT_ERR_UNCORRECTED) ? "UE" : "CE",
		str_plural(errcount),
		mci->fake_inject_layer[0],
		mci->fake_inject_layer[1],
		mci->fake_inject_layer[2]
	       );
	edac_mc_handle_error(type, mci, errcount, 0, 0, 0,
			     mci->fake_inject_layer[0],
			     mci->fake_inject_layer[1],
			     mci->fake_inject_layer[2],
			     "FAKE ERROR", "for EDAC testing only");

	return count;
}

static const struct file_operations debug_fake_inject_fops = {
	.open = simple_open,
	.write = edac_fake_inject_write,
	.llseek = generic_file_llseek,
};

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
	struct dentry *parent;
	char name[80];
	int i;

	parent = debugfs_create_dir(mci->dev.kobj.name, edac_debugfs);

	for (i = 0; i < mci->n_layers; i++) {
		sprintf(name, "fake_inject_%s",
			     edac_layer_name[mci->layers[i].type]);
		debugfs_create_u8(name, S_IRUGO | S_IWUSR, parent,
				  &mci->fake_inject_layer[i]);
	}

	debugfs_create_bool("fake_inject_ue", S_IRUGO | S_IWUSR, parent,
			    &mci->fake_inject_ue);

	debugfs_create_u16("fake_inject_count", S_IRUGO | S_IWUSR, parent,
			   &mci->fake_inject_count);

	debugfs_create_file("fake_inject", S_IWUSR, parent, &mci->dev,
			    &debug_fake_inject_fops);

	mci->debugfs = parent;
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
