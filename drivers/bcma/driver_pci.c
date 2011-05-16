/*
 * Broadcom specific AMBA
 * PCI Core
 *
 * Copyright 2005, Broadcom Corporation
 * Copyright 2006, 2007, Michael Buesch <mb@bu3sch.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"
#include <linux/bcma/bcma.h>

/**************************************************
 * R/W ops.
 **************************************************/

static u32 bcma_pcie_read(struct bcma_drv_pci *pc, u32 address)
{
	pcicore_write32(pc, 0x130, address);
	pcicore_read32(pc, 0x130);
	return pcicore_read32(pc, 0x134);
}

#if 0
static void bcma_pcie_write(struct bcma_drv_pci *pc, u32 address, u32 data)
{
	pcicore_write32(pc, 0x130, address);
	pcicore_read32(pc, 0x130);
	pcicore_write32(pc, 0x134, data);
}
#endif

static void bcma_pcie_mdio_set_phy(struct bcma_drv_pci *pc, u8 phy)
{
	const u16 mdio_control = 0x128;
	const u16 mdio_data = 0x12C;
	u32 v;
	int i;

	v = (1 << 30); /* Start of Transaction */
	v |= (1 << 28); /* Write Transaction */
	v |= (1 << 17); /* Turnaround */
	v |= (0x1F << 18);
	v |= (phy << 4);
	pcicore_write32(pc, mdio_data, v);

	udelay(10);
	for (i = 0; i < 200; i++) {
		v = pcicore_read32(pc, mdio_control);
		if (v & 0x100 /* Trans complete */)
			break;
		msleep(1);
	}
}

static u16 bcma_pcie_mdio_read(struct bcma_drv_pci *pc, u8 device, u8 address)
{
	const u16 mdio_control = 0x128;
	const u16 mdio_data = 0x12C;
	int max_retries = 10;
	u16 ret = 0;
	u32 v;
	int i;

	v = 0x80; /* Enable Preamble Sequence */
	v |= 0x2; /* MDIO Clock Divisor */
	pcicore_write32(pc, mdio_control, v);

	if (pc->core->id.rev >= 10) {
		max_retries = 200;
		bcma_pcie_mdio_set_phy(pc, device);
	}

	v = (1 << 30); /* Start of Transaction */
	v |= (1 << 29); /* Read Transaction */
	v |= (1 << 17); /* Turnaround */
	if (pc->core->id.rev < 10)
		v |= (u32)device << 22;
	v |= (u32)address << 18;
	pcicore_write32(pc, mdio_data, v);
	/* Wait for the device to complete the transaction */
	udelay(10);
	for (i = 0; i < max_retries; i++) {
		v = pcicore_read32(pc, mdio_control);
		if (v & 0x100 /* Trans complete */) {
			udelay(10);
			ret = pcicore_read32(pc, mdio_data);
			break;
		}
		msleep(1);
	}
	pcicore_write32(pc, mdio_control, 0);
	return ret;
}

static void bcma_pcie_mdio_write(struct bcma_drv_pci *pc, u8 device,
				u8 address, u16 data)
{
	const u16 mdio_control = 0x128;
	const u16 mdio_data = 0x12C;
	int max_retries = 10;
	u32 v;
	int i;

	v = 0x80; /* Enable Preamble Sequence */
	v |= 0x2; /* MDIO Clock Divisor */
	pcicore_write32(pc, mdio_control, v);

	if (pc->core->id.rev >= 10) {
		max_retries = 200;
		bcma_pcie_mdio_set_phy(pc, device);
	}

	v = (1 << 30); /* Start of Transaction */
	v |= (1 << 28); /* Write Transaction */
	v |= (1 << 17); /* Turnaround */
	if (pc->core->id.rev < 10)
		v |= (u32)device << 22;
	v |= (u32)address << 18;
	v |= data;
	pcicore_write32(pc, mdio_data, v);
	/* Wait for the device to complete the transaction */
	udelay(10);
	for (i = 0; i < max_retries; i++) {
		v = pcicore_read32(pc, mdio_control);
		if (v & 0x100 /* Trans complete */)
			break;
		msleep(1);
	}
	pcicore_write32(pc, mdio_control, 0);
}

/**************************************************
 * Workarounds.
 **************************************************/

static u8 bcma_pcicore_polarity_workaround(struct bcma_drv_pci *pc)
{
	return (bcma_pcie_read(pc, 0x204) & 0x10) ? 0xC0 : 0x80;
}

static void bcma_pcicore_serdes_workaround(struct bcma_drv_pci *pc)
{
	const u8 serdes_pll_device = 0x1D;
	const u8 serdes_rx_device = 0x1F;
	u16 tmp;

	bcma_pcie_mdio_write(pc, serdes_rx_device, 1 /* Control */,
			      bcma_pcicore_polarity_workaround(pc));
	tmp = bcma_pcie_mdio_read(pc, serdes_pll_device, 1 /* Control */);
	if (tmp & 0x4000)
		bcma_pcie_mdio_write(pc, serdes_pll_device, 1, tmp & ~0x4000);
}

/**************************************************
 * Init.
 **************************************************/

void bcma_core_pci_init(struct bcma_drv_pci *pc)
{
	bcma_pcicore_serdes_workaround(pc);
}
