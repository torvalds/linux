// SPDX-License-Identifier: GPL-2.0
#include <linux/seq_file.h>
#include <linux/debugfs.h>

#include "nitrox_csr.h"
#include "nitrox_debugfs.h"
#include "nitrox_dev.h"

static int firmware_show(struct seq_file *s, void *v)
{
	struct nitrox_device *ndev = s->private;

	seq_printf(s, "Version: %s\n", ndev->hw.fw_name[0]);
	seq_printf(s, "Version: %s\n", ndev->hw.fw_name[1]);
	return 0;
}

DEFINE_SHOW_ATTRIBUTE(firmware);

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

DEFINE_SHOW_ATTRIBUTE(device);

static int stats_show(struct seq_file *s, void *v)
{
	struct nitrox_device *ndev = s->private;

	seq_printf(s, "NITROX [%d] Request Statistics\n", ndev->idx);
	seq_printf(s, "  Posted: %llu\n",
		   (u64)atomic64_read(&ndev->stats.posted));
	seq_printf(s, "  Completed: %llu\n",
		   (u64)atomic64_read(&ndev->stats.completed));
	seq_printf(s, "  Dropped: %llu\n",
		   (u64)atomic64_read(&ndev->stats.dropped));

	return 0;
}

DEFINE_SHOW_ATTRIBUTE(stats);

void nitrox_debugfs_exit(struct nitrox_device *ndev)
{
	debugfs_remove_recursive(ndev->debugfs_dir);
	ndev->debugfs_dir = NULL;
}

void nitrox_debugfs_init(struct nitrox_device *ndev)
{
	struct dentry *dir;

	dir = debugfs_create_dir(KBUILD_MODNAME, NULL);

	ndev->debugfs_dir = dir;
	debugfs_create_file("firmware", 0400, dir, ndev, &firmware_fops);
	debugfs_create_file("device", 0400, dir, ndev, &device_fops);
	debugfs_create_file("stats", 0400, dir, ndev, &stats_fops);
}
