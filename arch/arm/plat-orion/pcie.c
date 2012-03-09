/*
 * arch/arm/plat-orion/pcie.c
 *
 * Marvell Orion SoC PCIe handling.
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/mbus.h>
#include <asm/mach/pci.h>
#include <plat/pcie.h>
#include <plat/addr-map.h>
#include <linux/delay.h>

/*
 * PCIe unit register offsets.
 */
#define PCIE_DEV_ID_OFF		0x0000
#define PCIE_CMD_OFF		0x0004
#define PCIE_DEV_REV_OFF	0x0008
#define PCIE_BAR_LO_OFF(n)	(0x0010 + ((n) << 3))
#define PCIE_BAR_HI_OFF(n)	(0x0014 + ((n) << 3))
#define PCIE_HEADER_LOG_4_OFF	0x0128
#define PCIE_BAR_CTRL_OFF(n)	(0x1804 + ((n - 1) * 4))
#define PCIE_WIN04_CTRL_OFF(n)	(0x1820 + ((n) << 4))
#define PCIE_WIN04_BASE_OFF(n)	(0x1824 + ((n) << 4))
#define PCIE_WIN04_REMAP_OFF(n)	(0x182c + ((n) << 4))
#define PCIE_WIN5_CTRL_OFF	0x1880
#define PCIE_WIN5_BASE_OFF	0x1884
#define PCIE_WIN5_REMAP_OFF	0x188c
#define PCIE_CONF_ADDR_OFF	0x18f8
#define  PCIE_CONF_ADDR_EN		0x80000000
#define  PCIE_CONF_REG(r)		((((r) & 0xf00) << 16) | ((r) & 0xfc))
#define  PCIE_CONF_BUS(b)		(((b) & 0xff) << 16)
#define  PCIE_CONF_DEV(d)		(((d) & 0x1f) << 11)
#define  PCIE_CONF_FUNC(f)		(((f) & 0x7) << 8)
#define PCIE_CONF_DATA_OFF	0x18fc
#define PCIE_MASK_OFF		0x1910
#define PCIE_CTRL_OFF		0x1a00
#define  PCIE_CTRL_X1_MODE		0x0001
#define PCIE_STAT_OFF		0x1a04
#define  PCIE_STAT_DEV_OFFS		20
#define  PCIE_STAT_DEV_MASK		0x1f
#define  PCIE_STAT_BUS_OFFS		8
#define  PCIE_STAT_BUS_MASK		0xff
#define  PCIE_STAT_LINK_DOWN		1
#define PCIE_DEBUG_CTRL         0x1a60
#define  PCIE_DEBUG_SOFT_RESET		(1<<20)


u32 __init orion_pcie_dev_id(void __iomem *base)
{
	return readl(base + PCIE_DEV_ID_OFF) >> 16;
}

u32 __init orion_pcie_rev(void __iomem *base)
{
	return readl(base + PCIE_DEV_REV_OFF) & 0xff;
}

int orion_pcie_link_up(void __iomem *base)
{
	return !(readl(base + PCIE_STAT_OFF) & PCIE_STAT_LINK_DOWN);
}

int __init orion_pcie_x4_mode(void __iomem *base)
{
	return !(readl(base + PCIE_CTRL_OFF) & PCIE_CTRL_X1_MODE);
}

int orion_pcie_get_local_bus_nr(void __iomem *base)
{
	u32 stat = readl(base + PCIE_STAT_OFF);

	return (stat >> PCIE_STAT_BUS_OFFS) & PCIE_STAT_BUS_MASK;
}

void __init orion_pcie_set_local_bus_nr(void __iomem *base, int nr)
{
	u32 stat;

	stat = readl(base + PCIE_STAT_OFF);
	stat &= ~(PCIE_STAT_BUS_MASK << PCIE_STAT_BUS_OFFS);
	stat |= nr << PCIE_STAT_BUS_OFFS;
	writel(stat, base + PCIE_STAT_OFF);
}

void __init orion_pcie_reset(void __iomem *base)
{
	u32 reg;
	int i;

	/*
	 * MV-S104860-U0, Rev. C:
	 * PCI Express Unit Soft Reset
	 * When set, generates an internal reset in the PCI Express unit.
	 * This bit should be cleared after the link is re-established.
	 */
	reg = readl(base + PCIE_DEBUG_CTRL);
	reg |= PCIE_DEBUG_SOFT_RESET;
	writel(reg, base + PCIE_DEBUG_CTRL);

	for (i = 0; i < 20; i++) {
		mdelay(10);

		if (orion_pcie_link_up(base))
			break;
	}

	reg &= ~(PCIE_DEBUG_SOFT_RESET);
	writel(reg, base + PCIE_DEBUG_CTRL);
}

/*
 * Setup PCIE BARs and Address Decode Wins:
 * BAR[0,2] -> disabled, BAR[1] -> covers all DRAM banks
 * WIN[0-3] -> DRAM bank[0-3]
 */
static void __init orion_pcie_setup_wins(void __iomem *base,
					 struct mbus_dram_target_info *dram)
{
	u32 size;
	int i;

	/*
	 * First, disable and clear BARs and windows.
	 */
	for (i = 1; i <= 2; i++) {
		writel(0, base + PCIE_BAR_CTRL_OFF(i));
		writel(0, base + PCIE_BAR_LO_OFF(i));
		writel(0, base + PCIE_BAR_HI_OFF(i));
	}

	for (i = 0; i < 5; i++) {
		writel(0, base + PCIE_WIN04_CTRL_OFF(i));
		writel(0, base + PCIE_WIN04_BASE_OFF(i));
		writel(0, base + PCIE_WIN04_REMAP_OFF(i));
	}

	writel(0, base + PCIE_WIN5_CTRL_OFF);
	writel(0, base + PCIE_WIN5_BASE_OFF);
	writel(0, base + PCIE_WIN5_REMAP_OFF);

	/*
	 * Setup windows for DDR banks.  Count total DDR size on the fly.
	 */
	size = 0;
	for (i = 0; i < dram->num_cs; i++) {
		struct mbus_dram_window *cs = dram->cs + i;

		writel(cs->base & 0xffff0000, base + PCIE_WIN04_BASE_OFF(i));
		writel(0, base + PCIE_WIN04_REMAP_OFF(i));
		writel(((cs->size - 1) & 0xffff0000) |
			(cs->mbus_attr << 8) |
			(dram->mbus_dram_target_id << 4) | 1,
				base + PCIE_WIN04_CTRL_OFF(i));

		size += cs->size;
	}

	/*
	 * Round up 'size' to the nearest power of two.
	 */
	if ((size & (size - 1)) != 0)
		size = 1 << fls(size);

	/*
	 * Setup BAR[1] to all DRAM banks.
	 */
	writel(dram->cs[0].base, base + PCIE_BAR_LO_OFF(1));
	writel(0, base + PCIE_BAR_HI_OFF(1));
	writel(((size - 1) & 0xffff0000) | 1, base + PCIE_BAR_CTRL_OFF(1));
}

void __init orion_pcie_setup(void __iomem *base)
{
	u16 cmd;
	u32 mask;

	/*
	 * Point PCIe unit MBUS decode windows to DRAM space.
	 */
	orion_pcie_setup_wins(base, &orion_mbus_dram_info);

	/*
	 * Master + slave enable.
	 */
	cmd = readw(base + PCIE_CMD_OFF);
	cmd |= PCI_COMMAND_IO;
	cmd |= PCI_COMMAND_MEMORY;
	cmd |= PCI_COMMAND_MASTER;
	writew(cmd, base + PCIE_CMD_OFF);

	/*
	 * Enable interrupt lines A-D.
	 */
	mask = readl(base + PCIE_MASK_OFF);
	mask |= 0x0f000000;
	writel(mask, base + PCIE_MASK_OFF);
}

int orion_pcie_rd_conf(void __iomem *base, struct pci_bus *bus,
		       u32 devfn, int where, int size, u32 *val)
{
	writel(PCIE_CONF_BUS(bus->number) |
		PCIE_CONF_DEV(PCI_SLOT(devfn)) |
		PCIE_CONF_FUNC(PCI_FUNC(devfn)) |
		PCIE_CONF_REG(where) | PCIE_CONF_ADDR_EN,
			base + PCIE_CONF_ADDR_OFF);

	*val = readl(base + PCIE_CONF_DATA_OFF);

	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 3))) & 0xffff;

	return PCIBIOS_SUCCESSFUL;
}

int orion_pcie_rd_conf_tlp(void __iomem *base, struct pci_bus *bus,
			   u32 devfn, int where, int size, u32 *val)
{
	writel(PCIE_CONF_BUS(bus->number) |
		PCIE_CONF_DEV(PCI_SLOT(devfn)) |
		PCIE_CONF_FUNC(PCI_FUNC(devfn)) |
		PCIE_CONF_REG(where) | PCIE_CONF_ADDR_EN,
			base + PCIE_CONF_ADDR_OFF);

	*val = readl(base + PCIE_CONF_DATA_OFF);

	if (bus->number != orion_pcie_get_local_bus_nr(base) ||
	    PCI_FUNC(devfn) != 0)
		*val = readl(base + PCIE_HEADER_LOG_4_OFF);

	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 3))) & 0xffff;

	return PCIBIOS_SUCCESSFUL;
}

int orion_pcie_rd_conf_wa(void __iomem *wa_base, struct pci_bus *bus,
			  u32 devfn, int where, int size, u32 *val)
{
	*val = readl(wa_base + (PCIE_CONF_BUS(bus->number) |
				PCIE_CONF_DEV(PCI_SLOT(devfn)) |
				PCIE_CONF_FUNC(PCI_FUNC(devfn)) |
				PCIE_CONF_REG(where)));

	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 3))) & 0xffff;

	return PCIBIOS_SUCCESSFUL;
}

int orion_pcie_wr_conf(void __iomem *base, struct pci_bus *bus,
		       u32 devfn, int where, int size, u32 val)
{
	int ret = PCIBIOS_SUCCESSFUL;

	writel(PCIE_CONF_BUS(bus->number) |
		PCIE_CONF_DEV(PCI_SLOT(devfn)) |
		PCIE_CONF_FUNC(PCI_FUNC(devfn)) |
		PCIE_CONF_REG(where) | PCIE_CONF_ADDR_EN,
			base + PCIE_CONF_ADDR_OFF);

	if (size == 4) {
		writel(val, base + PCIE_CONF_DATA_OFF);
	} else if (size == 2) {
		writew(val, base + PCIE_CONF_DATA_OFF + (where & 3));
	} else if (size == 1) {
		writeb(val, base + PCIE_CONF_DATA_OFF + (where & 3));
	} else {
		ret = PCIBIOS_BAD_REGISTER_NUMBER;
	}

	return ret;
}
