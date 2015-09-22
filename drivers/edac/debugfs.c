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

static const struct file_operations debug_fake_inject_fops = {
	.open = simple_open,
	.write = edac_fake_inject_write,
	.llseek = generic_file_llseek,
};

int __init edac_debugfs_init(void)
{
	edac_debugfs = debugfs_create_dir("edac", NULL);
	if (IS_ERR(edac_debugfs)) {
		edac_debugfs = NULL;
		return -ENOMEM;
	}
	return 0;
}

void edac_debugfs_exit(void)
{
	debugfs_remove(edac_debugfs);
}

int edac_create_debugfs_nodes(struct mem_ctl_info *mci)
{
	struct dentry *d, *parent;
	char name[80];
	int i;

	if (!edac_debugfs)
		return -ENODEV;

	d = debugfs_create_dir(mci->dev.kobj.name, edac_debugfs);
	if (!d)
		return -ENOMEM;
	parent = d;

	for (i = 0; i < mci->n_layers; i++) {
		sprintf(name, "fake_inject_%s",
			     edac_layer_name[mci->layers[i].type]);
		d = debugfs_create_u8(name, S_IRUGO | S_IWUSR, parent,
				      &mci->fake_inject_layer[i]);
		if (!d)
			goto nomem;
	}

	d = debugfs_create_bool("fake_inject_ue", S_IRUGO | S_IWUSR, parent,
				&mci->fake_inject_ue);
	if (!d)
		goto nomem;

	d = debugfs_create_u16("fake_inject_count", S_IRUGO | S_IWUSR, parent,
				&mci->fake_inject_count);
	if (!d)
		goto nomem;

	d = debugfs_create_file("fake_inject", S_IWUSR, parent,
				&mci->dev,
				&debug_fake_inject_fops);
	if (!d)
		goto nomem;

	mci->debugfs = parent;
	return 0;
nomem:
	debugfs_remove(mci->debugfs);
	return -ENOMEM;
}
