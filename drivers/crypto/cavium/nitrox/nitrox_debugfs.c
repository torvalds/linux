// SPDX-License-Identifier: GPL-2.0
#include <linux/seq_file.h>
#include <linux/debugfs.h>

#include "nitrox_csr.h"
#include "nitrox_dev.h"

static int firmware_show(struct seq_file *s, void *v)
{
	struct nitrox_device *ndev = s->private;

	seq_printf(s, "Version: %s\n", ndev->hw.fw_name);
	return 0;
}

static int firmware_open(struct inode *inode, struct file *file)
{
	return single_open(file, firmware_show, inode->i_private);
}

static const struct file_operations firmware_fops = {
	.owner = THIS_MODULE,
	.open = firmware_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int device_show(struct seq_file *s, void *v)
{
	struct nitrox_device *ndev = s->private;

	seq_printf(s, "NITROX [%d]\n", ndev->idx);
	seq_printf(s, "  Part Name: %s\n", ndev->hw.partname);
	seq_printf(s, "  Frequency: %d MHz\n", ndev->hw.freq);
	seq_printf(s, "  Device ID: 0x%0x\n", ndev->hw.device_id);
	seq_printf(s, "  Revision ID: 0x%0x\n", ndev->hw.revision_id);
	seq_printf(s, "  Cores: [AE=%u  SE=%u  ZIP=%u]\n",
		   ndev->hw.ae_cores, ndev->hw.se_cores, ndev->hw.zip_cores);

	return 0;
}

static int nitrox_open(struct inode *inode, struct file *file)
{
	return single_open(file, device_show, inode->i_private);
}

static const struct file_operations nitrox_fops = {
	.owner = THIS_MODULE,
	.open = nitrox_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

static int stats_show(struct seq_file *s, void *v)
{
	struct nitrox_device *ndev = s->private;

	seq_printf(s, "NITROX [%d] Request Statistics\n", ndev->idx);
	seq_printf(s, "  Posted: %lld\n", atomic64_read(&ndev->stats.posted));
	seq_printf(s, "  Completed: %lld\n",
		   atomic64_read(&ndev->stats.completed));
	seq_printf(s, "  Dropped: %lld\n", atomic64_read(&ndev->stats.dropped));

	return 0;
}

static int nitrox_stats_open(struct inode *inode, struct file *file)
{
	return single_open(file, stats_show, inode->i_private);
}

static const struct file_operations nitrox_stats_fops = {
	.owner = THIS_MODULE,
	.open = nitrox_stats_open,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
};

void nitrox_debugfs_exit(struct nitrox_device *ndev)
{
	debugfs_remove_recursive(ndev->debugfs_dir);
	ndev->debugfs_dir = NULL;
}

int nitrox_debugfs_init(struct nitrox_device *ndev)
{
	struct dentry *dir, *f;

	dir = debugfs_create_dir(KBUILD_MODNAME, NULL);
	if (!dir)
		return -ENOMEM;

	ndev->debugfs_dir = dir;
	f = debugfs_create_file("firmware", 0400, dir, ndev, &firmware_fops);
	if (!f)
		goto err;
	f = debugfs_create_file("device", 0400, dir, ndev, &nitrox_fops);
	if (!f)
		goto err;
	f = debugfs_create_file("stats", 0400, dir, ndev, &nitrox_stats_fops);
	if (!f)
		goto err;

	return 0;

err:
	nitrox_debugfs_exit(ndev);
	return -ENODEV;
}
