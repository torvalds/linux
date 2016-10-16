/*
 * VFIO platform driver specialized for Calxeda xgmac reset
 * reset code is inherited from calxeda xgmac native driver
 *
 * Copyright 2010-2011 Calxeda, Inc.
 * Copyright (c) 2015 Linaro Ltd.
 *              www.linaro.org
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
 * You should have received a copy of the GNU General Public License along with
 * this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>

#include "vfio_platform_private.h"

#define DRIVER_VERSION  "0.1"
#define DRIVER_AUTHOR   "Eric Auger <eric.auger@linaro.org>"
#define DRIVER_DESC     "Reset support for Calxeda xgmac vfio platform device"

/* XGMAC Register definitions */
#define XGMAC_CONTROL           0x00000000      /* MAC Configuration */

/* DMA Control and Status Registers */
#define XGMAC_DMA_CONTROL       0x00000f18      /* Ctrl (Operational Mode) */
#define XGMAC_DMA_INTR_ENA      0x00000f1c      /* Interrupt Enable */

/* DMA Control registe defines */
#define DMA_CONTROL_ST          0x00002000      /* Start/Stop Transmission */
#define DMA_CONTROL_SR          0x00000002      /* Start/Stop Receive */

/* Common MAC defines */
#define MAC_ENABLE_TX           0x00000008      /* Transmitter Enable */
#define MAC_ENABLE_RX           0x00000004      /* Receiver Enable */

static inline void xgmac_mac_disable(void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + XGMAC_DMA_CONTROL);

	value &= ~(DMA_CONTROL_ST | DMA_CONTROL_SR);
	writel(value, ioaddr + XGMAC_DMA_CONTROL);

	value = readl(ioaddr + XGMAC_CONTROL);
	value &= ~(MAC_ENABLE_TX | MAC_ENABLE_RX);
	writel(value, ioaddr + XGMAC_CONTROL);
}

static int vfio_platform_calxedaxgmac_reset(struct vfio_platform_device *vdev)
{
	struct vfio_platform_region *reg = &vdev->regions[0];

	if (!reg->ioaddr) {
		reg->ioaddr =
			ioremap_nocache(reg->addr, reg->size);
		if (!reg->ioaddr)
			return -ENOMEM;
	}

	/* disable IRQ */
	writel(0, reg->ioaddr + XGMAC_DMA_INTR_ENA);

	/* Disable the MAC core */
	xgmac_mac_disable(reg->ioaddr);

	return 0;
}

module_vfio_reset_handler("calxeda,hb-xgmac", vfio_platform_calxedaxgmac_reset);

MODULE_VERSION(DRIVER_VERSION);
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR(DRIVER_AUTHOR);
MODULE_DESCRIPTION(DRIVER_DESC);
