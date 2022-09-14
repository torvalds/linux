// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * RDMA Network Block Driver
 *
 * Copyright (c) 2014 - 2018 ProfitBricks GmbH. All rights reserved.
 * Copyright (c) 2018 - 2019 1&1 IONOS Cloud GmbH. All rights reserved.
 * Copyright (c) 2019 - 2020 1&1 IONOS SE. All rights reserved.
 */
#undef pr_fmt
#define pr_fmt(fmt) KBUILD_MODNAME " L" __stringify(__LINE__) ": " fmt

#include "rnbd-srv-dev.h"
#include "rnbd-log.h"

struct rnbd_dev *rnbd_dev_open(const char *path, fmode_t flags)
{
	struct rnbd_dev *dev;
	int ret;

	dev = kzalloc(sizeof(*dev), GFP_KERNEL);
	if (!dev)
		return ERR_PTR(-ENOMEM);

	dev->blk_open_flags = flags;
	dev->bdev = blkdev_get_by_path(path, flags, THIS_MODULE);
	ret = PTR_ERR_OR_ZERO(dev->bdev);
	if (ret)
		goto err;

	dev->blk_open_flags = flags;

	return dev;

err:
	kfree(dev);
	return ERR_PTR(ret);
}

void rnbd_dev_close(struct rnbd_dev *dev)
{
	blkdev_put(dev->bdev, dev->blk_open_flags);
	kfree(dev);
}
