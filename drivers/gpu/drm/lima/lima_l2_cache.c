// SPDX-License-Identifier: GPL-2.0 OR MIT
/* Copyright 2017-2019 Qiang Yu <yuq825@gmail.com> */

#include <linux/iopoll.h>
#include <linux/device.h>

#include "lima_device.h"
#include "lima_l2_cache.h"
#include "lima_regs.h"

#define l2_cache_write(reg, data) writel(data, ip->iomem + reg)
#define l2_cache_read(reg) readl(ip->iomem + reg)

static int lima_l2_cache_wait_idle(struct lima_ip *ip)
{
	struct lima_device *dev = ip->dev;
	int err;
	u32 v;

	err = readl_poll_timeout(ip->iomem + LIMA_L2_CACHE_STATUS, v,
				 !(v & LIMA_L2_CACHE_STATUS_COMMAND_BUSY),
				 0, 1000);
	if (err) {
		dev_err(dev->dev, "l2 cache wait command timeout\n");
		return err;
	}
	return 0;
}

int lima_l2_cache_flush(struct lima_ip *ip)
{
	int ret;

	spin_lock(&ip->data.lock);
	l2_cache_write(LIMA_L2_CACHE_COMMAND, LIMA_L2_CACHE_COMMAND_CLEAR_ALL);
	ret = lima_l2_cache_wait_idle(ip);
	spin_unlock(&ip->data.lock);
	return ret;
}

static int lima_l2_cache_hw_init(struct lima_ip *ip)
{
	int err;

	err = lima_l2_cache_flush(ip);
	if (err)
		return err;

	l2_cache_write(LIMA_L2_CACHE_ENABLE,
		       LIMA_L2_CACHE_ENABLE_ACCESS |
		       LIMA_L2_CACHE_ENABLE_READ_ALLOCATE);
	l2_cache_write(LIMA_L2_CACHE_MAX_READS, 0x1c);

	return 0;
}

int lima_l2_cache_resume(struct lima_ip *ip)
{
	return lima_l2_cache_hw_init(ip);
}

void lima_l2_cache_suspend(struct lima_ip *ip)
{

}

int lima_l2_cache_init(struct lima_ip *ip)
{
	int i;
	u32 size;
	struct lima_device *dev = ip->dev;

	/* l2_cache2 only exists when one of PP4-7 present */
	if (ip->id == lima_ip_l2_cache2) {
		for (i = lima_ip_pp4; i <= lima_ip_pp7; i++) {
			if (dev->ip[i].present)
				break;
		}
		if (i > lima_ip_pp7)
			return -ENODEV;
	}

	spin_lock_init(&ip->data.lock);

	size = l2_cache_read(LIMA_L2_CACHE_SIZE);
	dev_info(dev->dev, "l2 cache %uK, %u-way, %ubyte cache line, %ubit external bus\n",
		 1 << (((size >> 16) & 0xff) - 10),
		 1 << ((size >> 8) & 0xff),
		 1 << (size & 0xff),
		 1 << ((size >> 24) & 0xff));

	return lima_l2_cache_hw_init(ip);
}

void lima_l2_cache_fini(struct lima_ip *ip)
{

}
