/*
 *  Copyright IBM Corp. 2012
 *
 *  Author(s):
 *    Jan Glauber <jang@linux.vnet.ibm.com>
 */

#define COMPONENT "zPCI"
#define pr_fmt(fmt) COMPONENT ": " fmt

#include <linux/kernel.h>
#include <linux/seq_file.h>
#include <linux/debugfs.h>
#include <linux/pci.h>
#include <asm/debug.h>

#include <asm/pci_dma.h>

static struct dentry *debugfs_root;

static char *pci_perf_names[] = {
	/* hardware counters */
	"Load operations",
	"Store operations",
	"Store block operations",
	"Refresh operations",
	"DMA read bytes",
	"DMA write bytes",
	/* software counters */
	"Allocated pages",
	"Mapped pages",
	"Unmapped pages",
};

static int pci_perf_show(struct seq_file *m, void *v)
{
	struct zpci_dev *zdev = m->private;
	u64 *stat;
	int i;

	if (!zdev)
		return 0;
	if (!zdev->fmb)
		return seq_printf(m, "FMB statistics disabled\n");

	/* header */
	seq_printf(m, "FMB @ %p\n", zdev->fmb);
	seq_printf(m, "Update interval: %u ms\n", zdev->fmb_update);
	seq_printf(m, "Samples: %u\n", zdev->fmb->samples);
	seq_printf(m, "Last update TOD: %Lx\n", zdev->fmb->last_update);

	/* hardware counters */
	stat = (u64 *) &zdev->fmb->ld_ops;
	for (i = 0; i < 4; i++)
		seq_printf(m, "%26s:\t%llu\n",
			   pci_perf_names[i], *(stat + i));
	if (zdev->fmb->dma_valid)
		for (i = 4; i < 6; i++)
			seq_printf(m, "%26s:\t%llu\n",
				   pci_perf_names[i], *(stat + i));
	/* software counters */
	for (i = 6; i < ARRAY_SIZE(pci_perf_names); i++)
		seq_printf(m, "%26s:\t%llu\n",
			   pci_perf_names[i],
			   atomic64_read((atomic64_t *) (stat + i)));

	return 0;
}

static ssize_t pci_perf_seq_write(struct file *file, const char __user *ubuf,
				  size_t count, loff_t *off)
{
	struct zpci_dev *zdev = ((struct seq_file *) file->private_data)->private;
	unsigned long val;
	int rc;

	if (!zdev)
		return 0;

	rc = kstrtoul_from_user(ubuf, count, 10, &val);
	if (rc)
		return rc;

	switch (val) {
	case 0:
		rc = zpci_fmb_disable_device(zdev);
		if (rc)
			return rc;
		break;
	case 1:
		rc = zpci_fmb_enable_device(zdev);
		if (rc)
			return rc;
		break;
	}
	return count;
}

static int pci_perf_seq_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, pci_perf_show,
			   file_inode(filp)->i_private);
}

static const struct file_operations debugfs_pci_perf_fops = {
	.open	 = pci_perf_seq_open,
	.read	 = seq_read,
	.write	 = pci_perf_seq_write,
	.llseek  = seq_lseek,
	.release = single_release,
};

static int pci_debug_show(struct seq_file *m, void *v)
{
	struct zpci_dev *zdev = m->private;

	zpci_debug_info(zdev, m);
	return 0;
}

static int pci_debug_seq_open(struct inode *inode, struct file *filp)
{
	return single_open(filp, pci_debug_show,
			   file_inode(filp)->i_private);
}

static const struct file_operations debugfs_pci_debug_fops = {
	.open	 = pci_debug_seq_open,
	.read	 = seq_read,
	.llseek  = seq_lseek,
	.release = single_release,
};

void zpci_debug_init_device(struct zpci_dev *zdev)
{
	zdev->debugfs_dev = debugfs_create_dir(dev_name(&zdev->pdev->dev),
					       debugfs_root);
	if (IS_ERR(zdev->debugfs_dev))
		zdev->debugfs_dev = NULL;

	zdev->debugfs_perf = debugfs_create_file("statistics",
				S_IFREG | S_IRUGO | S_IWUSR,
				zdev->debugfs_dev, zdev,
				&debugfs_pci_perf_fops);
	if (IS_ERR(zdev->debugfs_perf))
		zdev->debugfs_perf = NULL;

	zdev->debugfs_debug = debugfs_create_file("debug",
				S_IFREG | S_IRUGO | S_IWUSR,
				zdev->debugfs_dev, zdev,
				&debugfs_pci_debug_fops);
	if (IS_ERR(zdev->debugfs_debug))
		zdev->debugfs_debug = NULL;
}

void zpci_debug_exit_device(struct zpci_dev *zdev)
{
	debugfs_remove(zdev->debugfs_perf);
	debugfs_remove(zdev->debugfs_debug);
	debugfs_remove(zdev->debugfs_dev);
}

int __init zpci_debug_init(void)
{
	/* event trace buffer */
	pci_debug_msg_id = debug_register("pci_msg", 16, 1, 16 * sizeof(long));
	if (!pci_debug_msg_id)
		return -EINVAL;
	debug_register_view(pci_debug_msg_id, &debug_sprintf_view);
	debug_set_level(pci_debug_msg_id, 3);
	zpci_dbg("Debug view initialized\n");

	/* error log */
	pci_debug_err_id = debug_register("pci_error", 2, 1, 16);
	if (!pci_debug_err_id)
		return -EINVAL;
	debug_register_view(pci_debug_err_id, &debug_hex_ascii_view);
	debug_set_level(pci_debug_err_id, 6);
	zpci_err("Debug view initialized\n");

	debugfs_root = debugfs_create_dir("pci", NULL);
	return 0;
}

void zpci_debug_exit(void)
{
	if (pci_debug_msg_id)
		debug_unregister(pci_debug_msg_id);
	if (pci_debug_err_id)
		debug_unregister(pci_debug_err_id);

	debugfs_remove(debugfs_root);
}
