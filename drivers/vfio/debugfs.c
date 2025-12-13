// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) 2023, HiSilicon Ltd.
 */

#include <linux/device.h>
#include <linux/debugfs.h>
#include <linux/seq_file.h>
#include <linux/vfio.h>
#include "vfio.h"

static struct dentry *vfio_debugfs_root;

static int vfio_device_state_read(struct seq_file *seq, void *data)
{
	struct device *vf_dev = seq->private;
	struct vfio_device *vdev = container_of(vf_dev,
						struct vfio_device, device);
	enum vfio_device_mig_state state;
	int ret;

	BUILD_BUG_ON(VFIO_DEVICE_STATE_NR !=
		     VFIO_DEVICE_STATE_PRE_COPY_P2P + 1);

	ret = vdev->mig_ops->migration_get_state(vdev, &state);
	if (ret)
		return -EINVAL;

	switch (state) {
	case VFIO_DEVICE_STATE_ERROR:
		seq_puts(seq, "ERROR\n");
		break;
	case VFIO_DEVICE_STATE_STOP:
		seq_puts(seq, "STOP\n");
		break;
	case VFIO_DEVICE_STATE_RUNNING:
		seq_puts(seq, "RUNNING\n");
		break;
	case VFIO_DEVICE_STATE_STOP_COPY:
		seq_puts(seq, "STOP_COPY\n");
		break;
	case VFIO_DEVICE_STATE_RESUMING:
		seq_puts(seq, "RESUMING\n");
		break;
	case VFIO_DEVICE_STATE_RUNNING_P2P:
		seq_puts(seq, "RUNNING_P2P\n");
		break;
	case VFIO_DEVICE_STATE_PRE_COPY:
		seq_puts(seq, "PRE_COPY\n");
		break;
	case VFIO_DEVICE_STATE_PRE_COPY_P2P:
		seq_puts(seq, "PRE_COPY_P2P\n");
		break;
	default:
		seq_puts(seq, "Invalid\n");
	}

	return 0;
}

static int vfio_device_features_read(struct seq_file *seq, void *data)
{
	struct device *vf_dev = seq->private;
	struct vfio_device *vdev = container_of(vf_dev, struct vfio_device, device);

	if (vdev->migration_flags & VFIO_MIGRATION_STOP_COPY)
		seq_puts(seq, "stop-copy\n");
	if (vdev->migration_flags & VFIO_MIGRATION_P2P)
		seq_puts(seq, "p2p\n");
	if (vdev->migration_flags & VFIO_MIGRATION_PRE_COPY)
		seq_puts(seq, "pre-copy\n");
	if (vdev->log_ops)
		seq_puts(seq, "dirty-tracking\n");

	return 0;
}

void vfio_device_debugfs_init(struct vfio_device *vdev)
{
	struct device *dev = &vdev->device;

	vdev->debug_root = debugfs_create_dir(dev_name(vdev->dev),
					      vfio_debugfs_root);

	if (vdev->mig_ops) {
		struct dentry *vfio_dev_migration = NULL;

		vfio_dev_migration = debugfs_create_dir("migration",
							vdev->debug_root);
		debugfs_create_devm_seqfile(dev, "state", vfio_dev_migration,
					    vfio_device_state_read);
		debugfs_create_devm_seqfile(dev, "features", vfio_dev_migration,
					    vfio_device_features_read);
	}
}

void vfio_device_debugfs_exit(struct vfio_device *vdev)
{
	debugfs_remove_recursive(vdev->debug_root);
}

void vfio_debugfs_create_root(void)
{
	vfio_debugfs_root = debugfs_create_dir("vfio", NULL);
}

void vfio_debugfs_remove_root(void)
{
	debugfs_remove_recursive(vfio_debugfs_root);
	vfio_debugfs_root = NULL;
}
