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

#endif /* RNBD_SRV_DEV_H */
