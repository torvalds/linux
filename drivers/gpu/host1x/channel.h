/*
 * Tegra host1x Channel
 *
 * Copyright (c) 2010-2013, NVIDIA Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#ifndef __HOST1X_CHANNEL_H
#define __HOST1X_CHANNEL_H

#include <linux/io.h>

#include "cdma.h"

struct host1x;

struct host1x_channel {
	struct list_head list;

	unsigned int refcount;
	unsigned int id;
	struct mutex reflock;
	struct mutex submitlock;
	void __iomem *regs;
	struct device *dev;
	struct host1x_cdma cdma;
};

/* channel list operations */
int host1x_channel_list_init(struct host1x *host);

#define host1x_for_each_channel(host, channel)				\
	list_for_each_entry(channel, &host->chlist.list, list)

#endif
