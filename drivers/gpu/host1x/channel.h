/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Tegra host1x Channel
 *
 * Copyright (c) 2010-2013, NVIDIA Corporation.
 */

#ifndef __HOST1X_CHANNEL_H
#define __HOST1X_CHANNEL_H

#include <linux/io.h>
#include <linux/kref.h>
#include <linux/mutex.h>

#include "cdma.h"

struct host1x;
struct host1x_channel;

struct host1x_channel_list {
	struct host1x_channel *channels;

	struct mutex lock;
	unsigned long *allocated_channels;
};

struct host1x_channel {
	struct kref refcount;
	unsigned int id;
	struct mutex submitlock;
	void __iomem *regs;
	struct host1x_client *client;
	struct device *dev;
	struct host1x_cdma cdma;
};

/* channel list operations */
int host1x_channel_list_init(struct host1x_channel_list *chlist,
			     unsigned int num_channels);
void host1x_channel_list_free(struct host1x_channel_list *chlist);
struct host1x_channel *host1x_channel_get_index(struct host1x *host,
						unsigned int index);
void host1x_channel_stop_all(struct host1x *host);

#endif
