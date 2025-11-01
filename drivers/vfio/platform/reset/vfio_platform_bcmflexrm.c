// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017 Broadcom
 */

/*
 * This driver provides reset support for Broadcom FlexRM ring manager
 * to VFIO platform.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/module.h>

#include "../vfio_platform_private.h"

/* FlexRM configuration */
#define RING_REGS_SIZE					0x10000
#define RING_VER_MAGIC					0x76303031

/* Per-Ring register offsets */
#define RING_VER					0x000
#define RING_CONTROL					0x034
#define RING_FLUSH_DONE					0x038

/* Register RING_CONTROL fields */
#define CONTROL_FLUSH_SHIFT				5

/* Register RING_FLUSH_DONE fields */
#define FLUSH_DONE_MASK					0x1

static int vfio_platform_bcmflexrm_shutdown(void __iomem *ring)
{
	unsigned int timeout;

	/* Disable/inactivate ring */
	writel_relaxed(0x0, ring + RING_CONTROL);

	/* Set ring flush state */
	timeout = 1000; /* timeout of 1s */
	writel_relaxed(BIT(CONTROL_FLUSH_SHIFT), ring + RING_CONTROL);
	do {
		if (readl_relaxed(ring + RING_FLUSH_DONE) &
		    FLUSH_DONE_MASK)
			break;
		mdelay(1);
	} while (--timeout);
	if (!timeout)
		return -ETIMEDOUT;

	/* Clear ring flush state */
	timeout = 1000; /* timeout of 1s */
	writel_relaxed(0x0, ring + RING_CONTROL);
	do {
		if (!(readl_relaxed(ring + RING_FLUSH_DONE) &
		      FLUSH_DONE_MASK))
			break;
		mdelay(1);
	} while (--timeout);
	if (!timeout)
		return -ETIMEDOUT;

	return 0;
}

static int vfio_platform_bcmflexrm_reset(struct vfio_platform_device *vdev)
{
	void __iomem *ring;
	int rc = 0, ret = 0, ring_num = 0;
	struct vfio_platform_region *reg = &vdev->regions[0];

	dev_err_once(vdev->device, "DEPRECATION: VFIO Broadcom FlexRM platform reset is deprecated and will be removed in a future kernel release\n");

	/* Map FlexRM ring registers if not mapped */
	if (!reg->ioaddr) {
		reg->ioaddr = ioremap(reg->addr, reg->size);
		if (!reg->ioaddr)
			return -ENOMEM;
	}

	/* Discover and shutdown each FlexRM ring */
	for (ring = reg->ioaddr;
	     ring < (reg->ioaddr + reg->size); ring += RING_REGS_SIZE) {
		if (readl_relaxed(ring + RING_VER) == RING_VER_MAGIC) {
			rc = vfio_platform_bcmflexrm_shutdown(ring);
			if (rc) {
				dev_warn(vdev->device,
					 "FlexRM ring%d shutdown error %d\n",
					 ring_num, rc);
				ret |= rc;
			}
			ring_num++;
		}
	}

	return ret;
}

module_vfio_reset_handler("brcm,iproc-flexrm-mbox",
			  vfio_platform_bcmflexrm_reset);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Anup Patel <anup.patel@broadcom.com>");
MODULE_DESCRIPTION("Reset support for Broadcom FlexRM VFIO platform device");
