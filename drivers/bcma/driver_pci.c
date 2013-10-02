/*
 * Broadcom specific AMBA
 * PCI Core
 *
 * Copyright 2005, 2011, Broadcom Corporation
 * Copyright 2006, 2007, Michael Buesch <m@bues.ch>
 * Copyright 2011, 2012, Hauke Mehrtens <hauke@hauke-m.de>
 *
 * Licensed under the GNU/GPL. See COPYING for details.
 */

#include "bcma_private.h"
#include <linux/export.h>
#include <linux/bcma/bcma.h>

/**************************************************
 * R/W ops.
 **************************************************/

u32 bcma_pcie_read(struct bcma_drv_pci *pc, u32 address)
{
	pcicore_write32(pc, BCMA_CORE_PCI_PCIEIND_ADDR, address);
	pcicore_read32(pc, BCMA_CORE_PCI_PCIEIND_ADDR);
	return pcicore_read32(pc, BCMA_CORE_PCI_PCIEIND_DATA);
}

static void bcma_pcie_write(struct bcma_drv_pci *pc, u32 address, u32 data)
{
	pcicore_write32(pc, BCMA_CORE_PCI_PCIEIND_ADDR, address);
	pcicore_read32(pc, BCMA_CORE_PCI_PCIEIND_ADDR);
	pcicore_write32(pc, BCMA_CORE_PCI_PCIEIND_DATA, data);
}

static void bcma_pcie_mdio_set_phy(struct bcma_drv_pci *pc, u16 phy)
{
	u32 v;
	int i;

	v = BCMA_CORE_PCI_MDIODATA_START;
	v |= BCMA_CORE_PCI_MDIODATA_WRITE;
	v |= (BCMA_CORE_PCI_MDIODATA_DEV_ADDR <<
	      BCMA_CORE_PCI_MDIODATA_DEVADDR_SHF);
	v |= (BCMA_CORE_PCI_MDIODATA_BLK_ADDR <<
	      BCMA_CORE_PCI_MDIODATA_REGADDR_SHF);
	v |= BCMA_CORE_PCI_MDIODATA_TA;
	v |= (phy << 4);
	pcicore_write32(pc, BCMA_CORE_PCI_MDIO_DATA, v);

	udelay(10);
	for (i = 0; i < 200; i++) {
		v = pcicore_read32(pc, BCMA_CORE_PCI_MDIO_CONTROL);
		if (v & BCMA_CORE_PCI_MDIOCTL_ACCESS_DONE)
			break;
		usleep_range(1000, 2000);
	}
}

static u16 bcma_pcie_mdio_read(struct bcma_drv_pci *pc, u16 device, u8 address)
{
	int max_retries = 10;
	u16 ret = 0;
	u32 v;
	int i;

	/* enable mdio access to SERDES */
	v = BCMA_CORE_PCI_MDIOCTL_PREAM_EN;
	v |= BCMA_CORE_PCI_MDIOCTL_DIVISOR_VAL;
	pcicore_write32(pc, BCMA_CORE_PCI_MDIO_CONTROL, v);

	if (pc->core->id.rev >= 10) {
		max_retries = 200;
		bcma_pcie_mdio_set_phy(pc, device);
		v = (BCMA_CORE_PCI_MDIODATA_DEV_ADDR <<
		     BCMA_CORE_PCI_MDIODATA_DEVADDR_SHF);
		v |= (address << BCMA_CORE_PCI_MDIODATA_REGADDR_SHF);
	} else {
		v = (device << BCMA_CORE_PCI_MDIODATA_DEVADDR_SHF_OLD);
		v |= (address << BCMA_CORE_PCI_MDIODATA_REGADDR_SHF_OLD);
	}

	v = BCMA_CORE_PCI_MDIODATA_START;
	v |= BCMA_CORE_PCI_MDIODATA_READ;
	v |= BCMA_CORE_PCI_MDIODATA_TA;

	pcicore_write32(pc, BCMA_CORE_PCI_MDIO_DATA, v);
	/* Wait for the device to complete the transaction */
	udelay(10);
	for (i = 0; i < max_retries; i++) {
		v = pcicore_read32(pc, BCMA_CORE_PCI_MDIO_CONTROL);
		if (v & BCMA_CORE_PCI_MDIOCTL_ACCESS_DONE) {
			udelay(10);
			ret = pcicore_read32(pc, BCMA_CORE_PCI_MDIO_DATA);
			break;
		}
		usleep_range(1000, 2000);
	}
	pcicore_write32(pc, BCMA_CORE_PCI_MDIO_CONTROL, 0);
	return ret;
}

static void bcma_pcie_mdio_write(struct bcma_drv_pci *pc, u16 device,
				u8 address, u16 data)
{
	int max_retries = 10;
	u32 v;
	int i;

	/* enable mdio access to SERDES */
	v = BCMA_CORE_PCI_MDIOCTL_PREAM_EN;
	v |= BCMA_CORE_PCI_MDIOCTL_DIVISOR_VAL;
	pcicore_write32(pc, BCMA_CORE_PCI_MDIO_CONTROL, v);

	if (pc->core->id.rev >= 10) {
		max_retries = 200;
		bcma_pcie_mdio_set_phy(pc, device);
		v = (BCMA_CORE_PCI_MDIODATA_DEV_ADDR <<
		     BCMA_CORE_PCI_MDIODATA_DEVADDR_SHF);
		v |= (address << BCMA_CORE_PCI_MDIODATA_REGADDR_SHF);
	} else {
		v = (device << BCMA_CORE_PCI_MDIODATA_DEVADDR_SHF_OLD);
		v |= (address << BCMA_CORE_PCI_MDIODATA_REGADDR_SHF_OLD);
	}

	v = BCMA_CORE_PCI_MDIODATA_START;
	v |= BCMA_CORE_PCI_MDIODATA_WRITE;
	v |= BCMA_CORE_PCI_MDIODATA_TA;
	v |= data;
	pcicore_write32(pc, BCMA_CORE_PCI_MDIO_DATA, v);
	/* Wait for the device to complete the transaction */
	udelay(10);
	for (i = 0; i < max_retries; i++) {
		v = pcicore_read32(pc, BCMA_CORE_PCI_MDIO_CONTROL);
		if (v & BCMA_CORE_PCI_MDIOCTL_ACCESS_DONE)
			break;
		usleep_range(1000, 2000);
	}
	pcicore_write32(pc, BCMA_CORE_PCI_MDIO_CONTROL, 0);
}

static u16 bcma_pcie_mdio_writeread(struct bcma_drv_pci *pc, u16 device,
				    u8 address, u16 data)
{
	bcma_pcie_mdio_write(pc, device, address, data);
	return bcma_pcie_mdio_read(pc, device, address);
}

/**************************************************
 * Workarounds.
 **************************************************/

static u8 bcma_pcicore_polarity_workaround(struct bcma_drv_pci *pc)
{
	u32 tmp;

	tmp = bcma_pcie_read(pc, BCMA_CORE_PCI_PLP_STATUSREG);
	if (tmp & BCMA_CORE_PCI_PLP_POLARITYINV_STAT)
		return BCMA_CORE_PCI_SERDES_RX_CTRL_FORCE |
		       BCMA_CORE_PCI_SERDES_RX_CTRL_POLARITY;
	else
		return BCMA_CORE_PCI_SERDES_RX_CTRL_FORCE;
}

static void bcma_pcicore_serdes_workaround(struct bcma_drv_pci *pc)
{
	u16 tmp;

	bcma_pcie_mdio_write(pc, BCMA_CORE_PCI_MDIODATA_DEV_RX,
	                     BCMA_CORE_PCI_SERDES_RX_CTRL,
			     bcma_pcicore_polarity_workaround(pc));
	tmp = bcma_pcie_mdio_read(pc, BCMA_CORE_PCI_MDIODATA_DEV_PLL,
	                          BCMA_CORE_PCI_SERDES_PLL_CTRL);
	if (tmp & BCMA_CORE_PCI_PLL_CTRL_FREQDET_EN)
		bcma_pcie_mdio_write(pc, BCMA_CORE_PCI_MDIODATA_DEV_PLL,
		                     BCMA_CORE_PCI_SERDES_PLL_CTRL,
		                     tmp & ~BCMA_CORE_PCI_PLL_CTRL_FREQDET_EN);
}

static void bcma_core_pci_fixcfg(struct bcma_drv_pci *pc)
{
	struct bcma_device *core = pc->core;
	u16 val16, core_index;
	uint regoff;

	regoff = BCMA_CORE_PCI_SPROM(BCMA_CORE_PCI_SPROM_PI_OFFSET);
	core_index = (u16)core->core_index;

	val16 = pcicore_read16(pc, regoff);
	if (((val16 & BCMA_CORE_PCI_SPROM_PI_MASK) >> BCMA_CORE_PCI_SPROM_PI_SHIFT)
	     != core_index) {
		val16 = (core_index << BCMA_CORE_PCI_SPROM_PI_SHIFT) |
			(val16 & ~BCMA_CORE_PCI_SPROM_PI_MASK);
		pcicore_write16(pc, regoff, val16);
	}
}

/* Fix MISC config to allow coming out of L2/L3-Ready state w/o PRST */
/* Needs to happen when coming out of 'standby'/'hibernate' */
static void bcma_core_pci_config_fixup(struct bcma_drv_pci *pc)
{
	u16 val16;
	uint regoff;

	regoff = BCMA_CORE_PCI_SPROM(BCMA_CORE_PCI_SPROM_MISC_CONFIG);

	val16 = pcicore_read16(pc, regoff);

	if (!(val16 & BCMA_CORE_PCI_SPROM_L23READY_EXIT_NOPERST)) {
		val16 |= BCMA_CORE_PCI_SPROM_L23READY_EXIT_NOPERST;
		pcicore_write16(pc, regoff, val16);
	}
}

static void bcma_core_pci_power_save(struct bcma_drv_pci *pc, bool up)
{
	u16 data;

	if (pc->core->id.rev >= 15 && pc->core->id.rev <= 20) {
		data = up ? 0x74 : 0x7C;
		bcma_pcie_mdio_writeread(pc, BCMA_CORE_PCI_MDIO_BLK1,
					 BCMA_CORE_PCI_MDIO_BLK1_MGMT1, 0x7F64);
		bcma_pcie_mdio_writeread(pc, BCMA_CORE_PCI_MDIO_BLK1,
					 BCMA_CORE_PCI_MDIO_BLK1_MGMT3, data);
	} else if (pc->core->id.rev >= 21 && pc->core->id.rev <= 22) {
		data = up ? 0x75 : 0x7D;
		bcma_pcie_mdio_writeread(pc, BCMA_CORE_PCI_MDIO_BLK1,
					 BCMA_CORE_PCI_MDIO_BLK1_MGMT1, 0x7E65);
		bcma_pcie_mdio_writeread(pc, BCMA_CORE_PCI_MDIO_BLK1,
					 BCMA_CORE_PCI_MDIO_BLK1_MGMT3, data);
	}
}

/**************************************************
 * Init.
 **************************************************/

static void bcma_core_pci_clientmode_init(struct bcma_drv_pci *pc)
{
	bcma_core_pci_fixcfg(pc);
	bcma_pcicore_serdes_workaround(pc);
	bcma_core_pci_config_fixup(pc);
}

void bcma_core_pci_init(struct bcma_drv_pci *pc)
{
	if (pc->setup_done)
		return;

#ifdef CONFIG_BCMA_DRIVER_PCI_HOSTMODE
	pc->hostmode = bcma_core_pci_is_in_hostmode(pc);
	if (pc->hostmode)
		bcma_core_pci_hostmode_init(pc);
#endif /* CONFIG_BCMA_DRIVER_PCI_HOSTMODE */

	if (!pc->hostmode)
		bcma_core_pci_clientmode_init(pc);
}

int bcma_core_pci_irq_ctl(struct bcma_drv_pci *pc, struct bcma_device *core,
			  bool enable)
{
	struct pci_dev *pdev;
	u32 coremask, tmp;
	int err = 0;

	if (!pc || core->bus->hosttype != BCMA_HOSTTYPE_PCI) {
		/* This bcma device is not on a PCI host-bus. So the IRQs are
		 * not routed through the PCI core.
		 * So we must not enable routing through the PCI core. */
		goto out;
	}

	pdev = pc->core->bus->host_pci;

	err = pci_read_config_dword(pdev, BCMA_PCI_IRQMASK, &tmp);
	if (err)
		goto out;

	coremask = BIT(core->core_index) << 8;
	if (enable)
		tmp |= coremask;
	else
		tmp &= ~coremask;

	err = pci_write_config_dword(pdev, BCMA_PCI_IRQMASK, tmp);

out:
	return err;
}
EXPORT_SYMBOL_GPL(bcma_core_pci_irq_ctl);

static void bcma_core_pci_extend_L1timer(struct bcma_drv_pci *pc, bool extend)
{
	u32 w;

	w = bcma_pcie_read(pc, BCMA_CORE_PCI_DLLP_PMTHRESHREG);
	if (extend)
		w |= BCMA_CORE_PCI_ASPMTIMER_EXTEND;
	else
		w &= ~BCMA_CORE_PCI_ASPMTIMER_EXTEND;
	bcma_pcie_write(pc, BCMA_CORE_PCI_DLLP_PMTHRESHREG, w);
	bcma_pcie_read(pc, BCMA_CORE_PCI_DLLP_PMTHRESHREG);
}

void bcma_core_pci_up(struct bcma_bus *bus)
{
	struct bcma_drv_pci *pc;

	if (bus->hosttype != BCMA_HOSTTYPE_PCI)
		return;

	pc = &bus->drv_pci[0];

	bcma_core_pci_power_save(pc, true);

	bcma_core_pci_extend_L1timer(pc, true);
}
EXPORT_SYMBOL_GPL(bcma_core_pci_up);

void bcma_core_pci_down(struct bcma_bus *bus)
{
	struct bcma_drv_pci *pc;

	if (bus->hosttype != BCMA_HOSTTYPE_PCI)
		return;

	pc = &bus->drv_pci[0];

	bcma_core_pci_extend_L1timer(pc, false);

	bcma_core_pci_power_save(pc, false);
}
EXPORT_SYMBOL_GPL(bcma_core_pci_down);
