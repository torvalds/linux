// SPDX-License-Identifier: GPL-2.0-only
/*
 * VFIO platform driver specialized for AMD xgbe reset
 * reset code is inherited from AMD xgbe native driver
 *
 * Copyright (c) 2015 Linaro Ltd.
 *              www.linaro.org
 */

#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/io.h>
#include <uapi/linux/mdio.h>
#include <linux/delay.h>

#include "../vfio_platform_private.h"

#define DMA_MR			0x3000
#define MAC_VR			0x0110
#define DMA_ISR			0x3008
#define MAC_ISR			0x00b0
#define PCS_MMD_SELECT		0xff
#define MDIO_AN_INT		0x8002
#define MDIO_AN_INTMASK		0x8001

static unsigned int xmdio_read(void __iomem *ioaddr, unsigned int mmd,
			       unsigned int reg)
{
	unsigned int mmd_address, value;

	mmd_address = (mmd << 16) | ((reg) & 0xffff);
	iowrite32(mmd_address >> 8, ioaddr + (PCS_MMD_SELECT << 2));
	value = ioread32(ioaddr + ((mmd_address & 0xff) << 2));
	return value;
}

static void xmdio_write(void __iomem *ioaddr, unsigned int mmd,
			unsigned int reg, unsigned int value)
{
	unsigned int mmd_address;

	mmd_address = (mmd << 16) | ((reg) & 0xffff);
	iowrite32(mmd_address >> 8, ioaddr + (PCS_MMD_SELECT << 2));
	iowrite32(value, ioaddr + ((mmd_address & 0xff) << 2));
}

static int vfio_platform_amdxgbe_reset(struct vfio_platform_device *vdev)
{
	struct vfio_platform_region *xgmac_regs = &vdev->regions[0];
	struct vfio_platform_region *xpcs_regs = &vdev->regions[1];
	u32 dma_mr_value, pcs_value, value;
	unsigned int count;

	dev_err_once(vdev->device, "DEPRECATION: VFIO AMD XGBE platform reset is deprecated and will be removed in a future kernel release\n");

	if (!xgmac_regs->ioaddr) {
		xgmac_regs->ioaddr =
			ioremap(xgmac_regs->addr, xgmac_regs->size);
		if (!xgmac_regs->ioaddr)
			return -ENOMEM;
	}
	if (!xpcs_regs->ioaddr) {
		xpcs_regs->ioaddr =
			ioremap(xpcs_regs->addr, xpcs_regs->size);
		if (!xpcs_regs->ioaddr)
			return -ENOMEM;
	}

	/* reset the PHY through MDIO*/
	pcs_value = xmdio_read(xpcs_regs->ioaddr, MDIO_MMD_PCS, MDIO_CTRL1);
	pcs_value |= MDIO_CTRL1_RESET;
	xmdio_write(xpcs_regs->ioaddr, MDIO_MMD_PCS, MDIO_CTRL1, pcs_value);

	count = 50;
	do {
		msleep(20);
		pcs_value = xmdio_read(xpcs_regs->ioaddr, MDIO_MMD_PCS,
					MDIO_CTRL1);
	} while ((pcs_value & MDIO_CTRL1_RESET) && --count);

	if (pcs_value & MDIO_CTRL1_RESET)
		dev_warn(vdev->device, "%s: XGBE PHY reset timeout\n",
			 __func__);

	/* disable auto-negotiation */
	value = xmdio_read(xpcs_regs->ioaddr, MDIO_MMD_AN, MDIO_CTRL1);
	value &= ~MDIO_AN_CTRL1_ENABLE;
	xmdio_write(xpcs_regs->ioaddr, MDIO_MMD_AN, MDIO_CTRL1, value);

	/* disable AN IRQ */
	xmdio_write(xpcs_regs->ioaddr, MDIO_MMD_AN, MDIO_AN_INTMASK, 0);

	/* clear AN IRQ */
	xmdio_write(xpcs_regs->ioaddr, MDIO_MMD_AN, MDIO_AN_INT, 0);

	/* MAC software reset */
	dma_mr_value = ioread32(xgmac_regs->ioaddr + DMA_MR);
	dma_mr_value |= 0x1;
	iowrite32(dma_mr_value, xgmac_regs->ioaddr + DMA_MR);

	usleep_range(10, 15);

	count = 2000;
	while (--count && (ioread32(xgmac_regs->ioaddr + DMA_MR) & 1))
		usleep_range(500, 600);

	if (!count)
		dev_warn(vdev->device, "%s: MAC SW reset failed\n", __func__);

	return 0;
}

module_vfio_reset_handler("amd,xgbe-seattle-v1a", vfio_platform_amdxgbe_reset);

MODULE_VERSION("0.1");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Eric Auger <eric.auger@linaro.org>");
MODULE_DESCRIPTION("Reset support for AMD xgbe vfio platform device");
