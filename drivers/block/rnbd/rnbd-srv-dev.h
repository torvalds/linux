/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * RDMA Network Block Driver
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */
#ifndef RNBD_SRV_DEV_H
#define RNBD_SRV_DEV_H

#include <linux/fs.h>
#include "rnbd-proto.h"

struct rnbd_dev {
	struct block_device	*bdev;
	fmode_t			blk_open_flags;
};

/**
 * rnbd_dev_open() - Open a device
 * @path:	path to open
 * @flags:	open flags
 */
struct rnbd_dev *rnbd_dev_open(const char *path, fmode_t flags);

/**
 * rnbd_dev_close() - Close a device
 */
void rnbd_dev_close(struct rnbd_dev *dev);

void rnbd_endio(void *priv, int error);

static inline int rnbd_dev_get_max_segs(const struct rnbd_dev *dev)
{
	return queue_max_segments(bdev_get_queue(dev->bdev));
}

static inline int rnbd_dev_get_max_hw_sects(const struct rnbd_dev *dev)
{
	return queue_max_hw_sectors(bdev_get_queue(dev->bdev));
}

static inline int rnbd_dev_get_secure_discard(const struct rnbd_dev *dev)
{
	return bdev_max_secure_erase_sectors(dev->bdev);
}

static inline int rnbd_dev_get_max_discard_sects(const struct rnbd_dev *dev)
{
	return bdev_max_discard_sectors(dev->bdev);
}

static inline int rnbd_dev_get_discard_granularity(const struct rnbd_dev *dev)
{
	return bdev_get_queue(dev->bdev)->limits.discard_granularity;
}

static inline int rnbd_dev_get_discard_alignment(const struct rnbd_dev *dev)
{
	return bdev_discard_alignment(dev->bdev);
}

#endif /* RNBD_SRV_DEV_H */
