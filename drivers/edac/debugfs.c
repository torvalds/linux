#include "edac_module.h"

static struct dentry *edac_defs;

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

	printk(KERN_DE
	       "Generating %d %s fake error%s to %d.%d.%d to test core handling. NOTE: this won't test the driver-specific decoding logic.\n",
		errcount,
		(type == HW_EVENT_ERR_UNCORRECTED) ? "UE" : "CE",
		errcount > 1 ? "s" : "",
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

static const struct file_operations de_fake_inject_fops = {
	.open = simple_open,
	.write = edac_fake_inject_write,
	.llseek = generic_file_llseek,
};

void __init edac_defs_init(void)
{
	edac_defs = defs_create_dir("edac", NULL);
}

void edac_defs_exit(void)
{
	defs_remove_recursive(edac_defs);
}

void edac_create_defs_nodes(struct mem_ctl_info *mci)
{
	struct dentry *parent;
	char name[80];
	int i;

	parent = defs_create_dir(mci->dev.kobj.name, edac_defs);

	for (i = 0; i < mci->n_layers; i++) {
		sprintf(name, "fake_inject_%s",
			     edac_layer_name[mci->layers[i].type]);
		defs_create_u8(name, S_IRUGO | S_IWUSR, parent,
				  &mci->fake_inject_layer[i]);
	}

	defs_create_bool("fake_inject_ue", S_IRUGO | S_IWUSR, parent,
			    &mci->fake_inject_ue);

	defs_create_u16("fake_inject_count", S_IRUGO | S_IWUSR, parent,
			   &mci->fake_inject_count);

	defs_create_file("fake_inject", S_IWUSR, parent, &mci->dev,
			    &de_fake_inject_fops);

	mci->defs = parent;
}

/* Create a toplevel dir under EDAC's defs hierarchy */
struct dentry *edac_defs_create_dir(const char *dirname)
{
	if (!edac_defs)
		return NULL;

	return defs_create_dir(dirname, edac_defs);
}
EXPORT_SYMBOL_GPL(edac_defs_create_dir);

/* Create a toplevel dir under EDAC's defs hierarchy with parent @parent */
struct dentry *
edac_defs_create_dir_at(const char *dirname, struct dentry *parent)
{
	return defs_create_dir(dirname, parent);
}
EXPORT_SYMBOL_GPL(edac_defs_create_dir_at);

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
edac_defs_create_file(const char *name, umode_t mode, struct dentry *parent,
			 void *data, const struct file_operations *fops)
{
	if (!parent)
		parent = edac_defs;

	return defs_create_file(name, mode, parent, data, fops);
}
EXPORT_SYMBOL_GPL(edac_defs_create_file);

/* Wrapper for defs_create_x8() */
struct dentry *edac_defs_create_x8(const char *name, umode_t mode,
				       struct dentry *parent, u8 *value)
{
	if (!parent)
		parent = edac_defs;

	return defs_create_x8(name, mode, parent, value);
}
EXPORT_SYMBOL_GPL(edac_defs_create_x8);

/* Wrapper for defs_create_x16() */
struct dentry *edac_defs_create_x16(const char *name, umode_t mode,
				       struct dentry *parent, u16 *value)
{
	if (!parent)
		parent = edac_defs;

	return defs_create_x16(name, mode, parent, value);
}
EXPORT_SYMBOL_GPL(edac_defs_create_x16);
