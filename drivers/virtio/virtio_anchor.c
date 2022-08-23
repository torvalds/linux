// SPDX-License-Identifier: GPL-2.0-only
#include <linux/virtio.h>
#include <linux/virtio_anchor.h>

bool virtio_require_restricted_mem_acc(struct virtio_device *dev)
{
	return true;
}
EXPORT_SYMBOL_GPL(virtio_require_restricted_mem_acc);

static bool virtio_no_restricted_mem_acc(struct virtio_device *dev)
{
	return false;
}

bool (*virtio_check_mem_acc_cb)(struct virtio_device *dev) =
	virtio_no_restricted_mem_acc;
EXPORT_SYMBOL_GPL(virtio_check_mem_acc_cb);
