/*
 * arch/arm/mach-tegra/pci.c
 *
 * PCIe host controller driver for TEGRA(2) SOCs
 *
 * Copyright (c) 2010, CompuLab, Ltd.
 * Author: Mike Rapoport <mike@compulab.co.il>
 *
 * Based on NVIDIA PCIe driver
 * Copyright (c) 2008-2009, NVIDIA Corporation.
 *
 * Bits taken from arch/arm/mach-dove/pcie.c
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/export.h>

#include <asm/sizes.h>
#include <asm/mach/pci.h>

#include <mach/iomap.h>
#include <mach/clk.h>
#include <mach/powergate.h>

#include "board.h"

/* register definitions */
#define AFI_OFFSET	0x3800
#define PADS_OFFSET	0x3000
#define RP0_OFFSET	0x0000
#define RP1_OFFSET	0x1000

#define AFI_AXI_BAR0_SZ	0x00
#define AFI_AXI_BAR1_SZ	0x04
#define AFI_AXI_BAR2_SZ	0x08
#define AFI_AXI_BAR3_SZ	0x0c
#define AFI_AXI_BAR4_SZ	0x10
#define AFI_AXI_BAR5_SZ	0x14

#define AFI_AXI_BAR0_START	0x18
#define AFI_AXI_BAR1_START	0x1c
#define AFI_AXI_BAR2_START	0x20
#define AFI_AXI_BAR3_START	0x24
#define AFI_AXI_BAR4_START	0x28
#define AFI_AXI_BAR5_START	0x2c

#define AFI_FPCI_BAR0	0x30
#define AFI_FPCI_BAR1	0x34
#define AFI_FPCI_BAR2	0x38
#define AFI_FPCI_BAR3	0x3c
#define AFI_FPCI_BAR4	0x40
#define AFI_FPCI_BAR5	0x44

#define AFI_CACHE_BAR0_SZ	0x48
#define AFI_CACHE_BAR0_ST	0x4c
#define AFI_CACHE_BAR1_SZ	0x50
#define AFI_CACHE_BAR1_ST	0x54

#define AFI_MSI_BAR_SZ		0x60
#define AFI_MSI_FPCI_BAR_ST	0x64
#define AFI_MSI_AXI_BAR_ST	0x68

#define AFI_CONFIGURATION		0xac
#define  AFI_CONFIGURATION_EN_FPCI	(1 << 0)

#define AFI_FPCI_ERROR_MASKS	0xb0

#define AFI_INTR_MASK		0xb4
#define  AFI_INTR_MASK_INT_MASK	(1 << 0)
#define  AFI_INTR_MASK_MSI_MASK	(1 << 8)

#define AFI_INTR_CODE		0xb8
#define  AFI_INTR_CODE_MASK	0xf
#define  AFI_INTR_MASTER_ABORT	4
#define  AFI_INTR_LEGACY	6

#define AFI_INTR_SIGNATURE	0xbc
#define AFI_SM_INTR_ENABLE	0xc4

#define AFI_AFI_INTR_ENABLE		0xc8
#define  AFI_INTR_EN_INI_SLVERR		(1 << 0)
#define  AFI_INTR_EN_INI_DECERR		(1 << 1)
#define  AFI_INTR_EN_TGT_SLVERR		(1 << 2)
#define  AFI_INTR_EN_TGT_DECERR		(1 << 3)
#define  AFI_INTR_EN_TGT_WRERR		(1 << 4)
#define  AFI_INTR_EN_DFPCI_DECERR	(1 << 5)
#define  AFI_INTR_EN_AXI_DECERR		(1 << 6)
#define  AFI_INTR_EN_FPCI_TIMEOUT	(1 << 7)

#define AFI_PCIE_CONFIG					0x0f8
#define  AFI_PCIE_CONFIG_PCIEC0_DISABLE_DEVICE		(1 << 1)
#define  AFI_PCIE_CONFIG_PCIEC1_DISABLE_DEVICE		(1 << 2)
#define  AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_MASK	(0xf << 20)
#define  AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_SINGLE	(0x0 << 20)
#define  AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_DUAL	(0x1 << 20)

#define AFI_FUSE			0x104
#define  AFI_FUSE_PCIE_T0_GEN2_DIS	(1 << 2)

#define AFI_PEX0_CTRL			0x110
#define AFI_PEX1_CTRL			0x118
#define  AFI_PEX_CTRL_RST		(1 << 0)
#define  AFI_PEX_CTRL_REFCLK_EN		(1 << 3)

#define RP_VEND_XP	0x00000F00
#define  RP_VEND_XP_DL_UP	(1 << 30)

#define RP_LINK_CONTROL_STATUS			0x00000090
#define  RP_LINK_CONTROL_STATUS_LINKSTAT_MASK	0x3fff0000

#define PADS_CTL_SEL		0x0000009C

#define PADS_CTL		0x000000A0
#define  PADS_CTL_IDDQ_1L	(1 << 0)
#define  PADS_CTL_TX_DATA_EN_1L	(1 << 6)
#define  PADS_CTL_RX_DATA_EN_1L	(1 << 10)

#define PADS_PLL_CTL				0x000000B8
#define  PADS_PLL_CTL_RST_B4SM			(1 << 1)
#define  PADS_PLL_CTL_LOCKDET			(1 << 8)
#define  PADS_PLL_CTL_REFCLK_MASK		(0x3 << 16)
#define  PADS_PLL_CTL_REFCLK_INTERNAL_CML	(0 << 16)
#define  PADS_PLL_CTL_REFCLK_INTERNAL_CMOS	(1 << 16)
#define  PADS_PLL_CTL_REFCLK_EXTERNAL		(2 << 16)
#define  PADS_PLL_CTL_TXCLKREF_MASK		(0x1 << 20)
#define  PADS_PLL_CTL_TXCLKREF_DIV10		(0 << 20)
#define  PADS_PLL_CTL_TXCLKREF_DIV5		(1 << 20)

/* PMC access is required for PCIE xclk (un)clamping */
#define PMC_SCRATCH42		0x144
#define PMC_SCRATCH42_PCX_CLAMP	(1 << 0)

static void __iomem *reg_pmc_base = IO_ADDRESS(TEGRA_PMC_BASE);

#define pmc_writel(value, reg) \
	__raw_writel(value, reg_pmc_base + (reg))
#define pmc_readl(reg) \
	__raw_readl(reg_pmc_base + (reg))

/*
 * Tegra2 defines 1GB in the AXI address map for PCIe.
 *
 * That address space is split into different regions, with sizes and
 * offsets as follows:
 *
 * 0x80000000 - 0x80003fff - PCI controller registers
 * 0x80004000 - 0x80103fff - PCI configuration space
 * 0x80104000 - 0x80203fff - PCI extended configuration space
 * 0x80203fff - 0x803fffff - unused
 * 0x80400000 - 0x8040ffff - downstream IO
 * 0x80410000 - 0x8fffffff - unused
 * 0x90000000 - 0x9fffffff - non-prefetchable memory
 * 0xa0000000 - 0xbfffffff - prefetchable memory
 */
#define TEGRA_PCIE_BASE		0x80000000

#define PCIE_REGS_SZ		SZ_16K
#define PCIE_CFG_OFF		PCIE_REGS_SZ
#define PCIE_CFG_SZ		SZ_1M
#define PCIE_EXT_CFG_OFF	(PCIE_CFG_SZ + PCIE_CFG_OFF)
#define PCIE_EXT_CFG_SZ		SZ_1M
#define PCIE_IOMAP_SZ		(PCIE_REGS_SZ + PCIE_CFG_SZ + PCIE_EXT_CFG_SZ)

#define MMIO_BASE		(TEGRA_PCIE_BASE + SZ_4M)
#define MMIO_SIZE		SZ_64K
#define MEM_BASE_0		(TEGRA_PCIE_BASE + SZ_256M)
#define MEM_SIZE_0		SZ_128M
#define MEM_BASE_1		(MEM_BASE_0 + MEM_SIZE_0)
#define MEM_SIZE_1		SZ_128M
#define PREFETCH_MEM_BASE_0	(MEM_BASE_1 + MEM_SIZE_1)
#define PREFETCH_MEM_SIZE_0	SZ_128M
#define PREFETCH_MEM_BASE_1	(PREFETCH_MEM_BASE_0 + PREFETCH_MEM_SIZE_0)
#define PREFETCH_MEM_SIZE_1	SZ_128M

#define  PCIE_CONF_BUS(b)	((b) << 16)
#define  PCIE_CONF_DEV(d)	((d) << 11)
#define  PCIE_CONF_FUNC(f)	((f) << 8)
#define  PCIE_CONF_REG(r)	\
	(((r) & ~0x3) | (((r) < 256) ? PCIE_CFG_OFF : PCIE_EXT_CFG_OFF))

struct tegra_pcie_port {
	int			index;
	u8			root_bus_nr;
	void __iomem		*base;

	bool			link_up;

	char			io_space_name[16];
	char			mem_space_name[16];
	char			prefetch_space_name[20];
	struct resource		res[3];
};

struct tegra_pcie_info {
	struct tegra_pcie_port	port[2];
	int			num_ports;

	void __iomem		*regs;
	struct resource		res_mmio;

	struct clk		*pex_clk;
	struct clk		*afi_clk;
	struct clk		*pcie_xclk;
	struct clk		*pll_e;
};

static struct tegra_pcie_info tegra_pcie = {
	.res_mmio = {
		.name = "PCI IO",
		.start = MMIO_BASE,
		.end = MMIO_BASE + MMIO_SIZE - 1,
		.flags = IORESOURCE_MEM,
	},
};

void __iomem *tegra_pcie_io_base;
EXPORT_SYMBOL(tegra_pcie_io_base);

static inline void afi_writel(u32 value, unsigned long offset)
{
	writel(value, offset + AFI_OFFSET + tegra_pcie.regs);
}

static inline u32 afi_readl(unsigned long offset)
{
	return readl(offset + AFI_OFFSET + tegra_pcie.regs);
}

static inline void pads_writel(u32 value, unsigned long offset)
{
	writel(value, offset + PADS_OFFSET + tegra_pcie.regs);
}

static inline u32 pads_readl(unsigned long offset)
{
	return readl(offset + PADS_OFFSET + tegra_pcie.regs);
}

static struct tegra_pcie_port *bus_to_port(int bus)
{
	int i;

	for (i = tegra_pcie.num_ports - 1; i >= 0; i--) {
		int rbus = tegra_pcie.port[i].root_bus_nr;
		if (rbus != -1 && rbus == bus)
			break;
	}

	return i >= 0 ? tegra_pcie.port + i : NULL;
}

static int tegra_pcie_read_conf(struct pci_bus *bus, unsigned int devfn,
				int where, int size, u32 *val)
{
	struct tegra_pcie_port *pp = bus_to_port(bus->number);
	void __iomem *addr;

	if (pp) {
		if (devfn != 0) {
			*val = 0xffffffff;
			return PCIBIOS_DEVICE_NOT_FOUND;
		}

		addr = pp->base + (where & ~0x3);
	} else {
		addr = tegra_pcie.regs + (PCIE_CONF_BUS(bus->number) +
					  PCIE_CONF_DEV(PCI_SLOT(devfn)) +
					  PCIE_CONF_FUNC(PCI_FUNC(devfn)) +
					  PCIE_CONF_REG(where));
	}

	*val = readl(addr);

	if (size == 1)
		*val = (*val >> (8 * (where & 3))) & 0xff;
	else if (size == 2)
		*val = (*val >> (8 * (where & 3))) & 0xffff;

	return PCIBIOS_SUCCESSFUL;
}

static int tegra_pcie_write_conf(struct pci_bus *bus, unsigned int devfn,
				 int where, int size, u32 val)
{
	struct tegra_pcie_port *pp = bus_to_port(bus->number);
	void __iomem *addr;

	u32 mask;
	u32 tmp;

	if (pp) {
		if (devfn != 0)
			return PCIBIOS_DEVICE_NOT_FOUND;

		addr = pp->base + (where & ~0x3);
	} else {
		addr = tegra_pcie.regs + (PCIE_CONF_BUS(bus->number) +
					  PCIE_CONF_DEV(PCI_SLOT(devfn)) +
					  PCIE_CONF_FUNC(PCI_FUNC(devfn)) +
					  PCIE_CONF_REG(where));
	}

	if (size == 4) {
		writel(val, addr);
		return PCIBIOS_SUCCESSFUL;
	}

	if (size == 2)
		mask = ~(0xffff << ((where & 0x3) * 8));
	else if (size == 1)
		mask = ~(0xff << ((where & 0x3) * 8));
	else
		return PCIBIOS_BAD_REGISTER_NUMBER;

	tmp = readl(addr) & mask;
	tmp |= val << ((where & 0x3) * 8);
	writel(tmp, addr);

	return PCIBIOS_SUCCESSFUL;
}

static struct pci_ops tegra_pcie_ops = {
	.read	= tegra_pcie_read_conf,
	.write	= tegra_pcie_write_conf,
};

static void __devinit tegra_pcie_fixup_bridge(struct pci_dev *dev)
{
	u16 reg;

	if ((dev->class >> 16) == PCI_BASE_CLASS_BRIDGE) {
		pci_read_config_word(dev, PCI_COMMAND, &reg);
		reg |= (PCI_COMMAND_IO | PCI_COMMAND_MEMORY |
			PCI_COMMAND_MASTER | PCI_COMMAND_SERR);
		pci_write_config_word(dev, PCI_COMMAND, reg);
	}
}
DECLARE_PCI_FIXUP_FINAL(PCI_ANY_ID, PCI_ANY_ID, tegra_pcie_fixup_bridge);

/* Tegra PCIE root complex wrongly reports device class */
static void __devinit tegra_pcie_fixup_class(struct pci_dev *dev)
{
	dev->class = PCI_CLASS_BRIDGE_PCI << 8;
}
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_NVIDIA, 0x0bf0, tegra_pcie_fixup_class);
DECLARE_PCI_FIXUP_EARLY(PCI_VENDOR_ID_NVIDIA, 0x0bf1, tegra_pcie_fixup_class);

/* Tegra PCIE requires relaxed ordering */
static void __devinit tegra_pcie_relax_enable(struct pci_dev *dev)
{
	pcie_capability_set_word(dev, PCI_EXP_DEVCTL, PCI_EXP_DEVCTL_RELAX_EN);
}
DECLARE_PCI_FIXUP_FINAL(PCI_ANY_ID, PCI_ANY_ID, tegra_pcie_relax_enable);

static int tegra_pcie_setup(int nr, struct pci_sys_data *sys)
{
	struct tegra_pcie_port *pp;

	if (nr >= tegra_pcie.num_ports)
		return 0;

	pp = tegra_pcie.port + nr;
	pp->root_bus_nr = sys->busnr;

	/*
	 * IORESOURCE_IO
	 */
	snprintf(pp->io_space_name, sizeof(pp->io_space_name),
		 "PCIe %d I/O", pp->index);
	pp->io_space_name[sizeof(pp->io_space_name) - 1] = 0;
	pp->res[0].name = pp->io_space_name;
	if (pp->index == 0) {
		pp->res[0].start = PCIBIOS_MIN_IO;
		pp->res[0].end = pp->res[0].start + SZ_32K - 1;
	} else {
		pp->res[0].start = PCIBIOS_MIN_IO + SZ_32K;
		pp->res[0].end = IO_SPACE_LIMIT;
	}
	pp->res[0].flags = IORESOURCE_IO;
	if (request_resource(&ioport_resource, &pp->res[0]))
		panic("Request PCIe IO resource failed\n");
	pci_add_resource_offset(&sys->resources, &pp->res[0], sys->io_offset);

	/*
	 * IORESOURCE_MEM
	 */
	snprintf(pp->mem_space_name, sizeof(pp->mem_space_name),
		 "PCIe %d MEM", pp->index);
	pp->mem_space_name[sizeof(pp->mem_space_name) - 1] = 0;
	pp->res[1].name = pp->mem_space_name;
	if (pp->index == 0) {
		pp->res[1].start = MEM_BASE_0;
		pp->res[1].end = pp->res[1].start + MEM_SIZE_0 - 1;
	} else {
		pp->res[1].start = MEM_BASE_1;
		pp->res[1].end = pp->res[1].start + MEM_SIZE_1 - 1;
	}
	pp->res[1].flags = IORESOURCE_MEM;
	if (request_resource(&iomem_resource, &pp->res[1]))
		panic("Request PCIe Memory resource failed\n");
	pci_add_resource_offset(&sys->resources, &pp->res[1], sys->mem_offset);

	/*
	 * IORESOURCE_MEM | IORESOURCE_PREFETCH
	 */
	snprintf(pp->prefetch_space_name, sizeof(pp->prefetch_space_name),
		 "PCIe %d PREFETCH MEM", pp->index);
	pp->prefetch_space_name[sizeof(pp->prefetch_space_name) - 1] = 0;
	pp->res[2].name = pp->prefetch_space_name;
	if (pp->index == 0) {
		pp->res[2].start = PREFETCH_MEM_BASE_0;
		pp->res[2].end = pp->res[2].start + PREFETCH_MEM_SIZE_0 - 1;
	} else {
		pp->res[2].start = PREFETCH_MEM_BASE_1;
		pp->res[2].end = pp->res[2].start + PREFETCH_MEM_SIZE_1 - 1;
	}
	pp->res[2].flags = IORESOURCE_MEM | IORESOURCE_PREFETCH;
	if (request_resource(&iomem_resource, &pp->res[2]))
		panic("Request PCIe Prefetch Memory resource failed\n");
	pci_add_resource_offset(&sys->resources, &pp->res[2], sys->mem_offset);

	return 1;
}

static int tegra_pcie_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	return INT_PCIE_INTR;
}

static struct pci_bus __init *tegra_pcie_scan_bus(int nr,
						  struct pci_sys_data *sys)
{
	struct tegra_pcie_port *pp;

	if (nr >= tegra_pcie.num_ports)
		return NULL;

	pp = tegra_pcie.port + nr;
	pp->root_bus_nr = sys->busnr;

	return pci_scan_root_bus(NULL, sys->busnr, &tegra_pcie_ops, sys,
				 &sys->resources);
}

static struct hw_pci tegra_pcie_hw __initdata = {
	.nr_controllers	= 2,
	.setup		= tegra_pcie_setup,
	.scan		= tegra_pcie_scan_bus,
	.map_irq	= tegra_pcie_map_irq,
};


static irqreturn_t tegra_pcie_isr(int irq, void *arg)
{
	const char *err_msg[] = {
		"Unknown",
		"AXI slave error",
		"AXI decode error",
		"Target abort",
		"Master abort",
		"Invalid write",
		"Response decoding error",
		"AXI response decoding error",
		"Transcation timeout",
	};

	u32 code, signature;

	code = afi_readl(AFI_INTR_CODE) & AFI_INTR_CODE_MASK;
	signature = afi_readl(AFI_INTR_SIGNATURE);
	afi_writel(0, AFI_INTR_CODE);

	if (code == AFI_INTR_LEGACY)
		return IRQ_NONE;

	if (code >= ARRAY_SIZE(err_msg))
		code = 0;

	/*
	 * do not pollute kernel log with master abort reports since they
	 * happen a lot during enumeration
	 */
	if (code == AFI_INTR_MASTER_ABORT)
		pr_debug("PCIE: %s, signature: %08x\n", err_msg[code], signature);
	else
		pr_err("PCIE: %s, signature: %08x\n", err_msg[code], signature);

	return IRQ_HANDLED;
}

static void tegra_pcie_setup_translations(void)
{
	u32 fpci_bar;
	u32 size;
	u32 axi_address;

	/* Bar 0: config Bar */
	fpci_bar = ((u32)0xfdff << 16);
	size = PCIE_CFG_SZ;
	axi_address = TEGRA_PCIE_BASE + PCIE_CFG_OFF;
	afi_writel(axi_address, AFI_AXI_BAR0_START);
	afi_writel(size >> 12, AFI_AXI_BAR0_SZ);
	afi_writel(fpci_bar, AFI_FPCI_BAR0);

	/* Bar 1: extended config Bar */
	fpci_bar = ((u32)0xfe1 << 20);
	size = PCIE_EXT_CFG_SZ;
	axi_address = TEGRA_PCIE_BASE + PCIE_EXT_CFG_OFF;
	afi_writel(axi_address, AFI_AXI_BAR1_START);
	afi_writel(size >> 12, AFI_AXI_BAR1_SZ);
	afi_writel(fpci_bar, AFI_FPCI_BAR1);

	/* Bar 2: downstream IO bar */
	fpci_bar = ((__u32)0xfdfc << 16);
	size = MMIO_SIZE;
	axi_address = MMIO_BASE;
	afi_writel(axi_address, AFI_AXI_BAR2_START);
	afi_writel(size >> 12, AFI_AXI_BAR2_SZ);
	afi_writel(fpci_bar, AFI_FPCI_BAR2);

	/* Bar 3: prefetchable memory BAR */
	fpci_bar = (((PREFETCH_MEM_BASE_0 >> 12) & 0x0fffffff) << 4) | 0x1;
	size =  PREFETCH_MEM_SIZE_0 +  PREFETCH_MEM_SIZE_1;
	axi_address = PREFETCH_MEM_BASE_0;
	afi_writel(axi_address, AFI_AXI_BAR3_START);
	afi_writel(size >> 12, AFI_AXI_BAR3_SZ);
	afi_writel(fpci_bar, AFI_FPCI_BAR3);

	/* Bar 4: non prefetchable memory BAR */
	fpci_bar = (((MEM_BASE_0 >> 12)	& 0x0FFFFFFF) << 4) | 0x1;
	size = MEM_SIZE_0 + MEM_SIZE_1;
	axi_address = MEM_BASE_0;
	afi_writel(axi_address, AFI_AXI_BAR4_START);
	afi_writel(size >> 12, AFI_AXI_BAR4_SZ);
	afi_writel(fpci_bar, AFI_FPCI_BAR4);

	/* Bar 5: NULL out the remaining BAR as it is not used */
	fpci_bar = 0;
	size = 0;
	axi_address = 0;
	afi_writel(axi_address, AFI_AXI_BAR5_START);
	afi_writel(size >> 12, AFI_AXI_BAR5_SZ);
	afi_writel(fpci_bar, AFI_FPCI_BAR5);

	/* map all upstream transactions as uncached */
	afi_writel(PHYS_OFFSET, AFI_CACHE_BAR0_ST);
	afi_writel(0, AFI_CACHE_BAR0_SZ);
	afi_writel(0, AFI_CACHE_BAR1_ST);
	afi_writel(0, AFI_CACHE_BAR1_SZ);

	/* No MSI */
	afi_writel(0, AFI_MSI_FPCI_BAR_ST);
	afi_writel(0, AFI_MSI_BAR_SZ);
	afi_writel(0, AFI_MSI_AXI_BAR_ST);
	afi_writel(0, AFI_MSI_BAR_SZ);
}

static int tegra_pcie_enable_controller(void)
{
	u32 val, reg;
	int i, timeout;

	/* Enable slot clock and pulse the reset signals */
	for (i = 0, reg = AFI_PEX0_CTRL; i < 2; i++, reg += 0x8) {
		val = afi_readl(reg) |  AFI_PEX_CTRL_REFCLK_EN;
		afi_writel(val, reg);
		val &= ~AFI_PEX_CTRL_RST;
		afi_writel(val, reg);

		val = afi_readl(reg) | AFI_PEX_CTRL_RST;
		afi_writel(val, reg);
	}

	/* Enable dual controller and both ports */
	val = afi_readl(AFI_PCIE_CONFIG);
	val &= ~(AFI_PCIE_CONFIG_PCIEC0_DISABLE_DEVICE |
		 AFI_PCIE_CONFIG_PCIEC1_DISABLE_DEVICE |
		 AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_MASK);
	val |= AFI_PCIE_CONFIG_SM2TMS0_XBAR_CONFIG_DUAL;
	afi_writel(val, AFI_PCIE_CONFIG);

	val = afi_readl(AFI_FUSE) & ~AFI_FUSE_PCIE_T0_GEN2_DIS;
	afi_writel(val, AFI_FUSE);

	/* Initialze internal PHY, enable up to 16 PCIE lanes */
	pads_writel(0x0, PADS_CTL_SEL);

	/* override IDDQ to 1 on all 4 lanes */
	val = pads_readl(PADS_CTL) | PADS_CTL_IDDQ_1L;
	pads_writel(val, PADS_CTL);

	/*
	 * set up PHY PLL inputs select PLLE output as refclock,
	 * set TX ref sel to div10 (not div5)
	 */
	val = pads_readl(PADS_PLL_CTL);
	val &= ~(PADS_PLL_CTL_REFCLK_MASK | PADS_PLL_CTL_TXCLKREF_MASK);
	val |= (PADS_PLL_CTL_REFCLK_INTERNAL_CML | PADS_PLL_CTL_TXCLKREF_DIV10);
	pads_writel(val, PADS_PLL_CTL);

	/* take PLL out of reset  */
	val = pads_readl(PADS_PLL_CTL) | PADS_PLL_CTL_RST_B4SM;
	pads_writel(val, PADS_PLL_CTL);

	/*
	 * Hack, set the clock voltage to the DEFAULT provided by hw folks.
	 * This doesn't exist in the documentation
	 */
	pads_writel(0xfa5cfa5c, 0xc8);

	/* Wait for the PLL to lock */
	timeout = 300;
	do {
		val = pads_readl(PADS_PLL_CTL);
		usleep_range(1000, 1000);
		if (--timeout == 0) {
			pr_err("Tegra PCIe error: timeout waiting for PLL\n");
			return -EBUSY;
		}
	} while (!(val & PADS_PLL_CTL_LOCKDET));

	/* turn off IDDQ override */
	val = pads_readl(PADS_CTL) & ~PADS_CTL_IDDQ_1L;
	pads_writel(val, PADS_CTL);

	/* enable TX/RX data */
	val = pads_readl(PADS_CTL);
	val |= (PADS_CTL_TX_DATA_EN_1L | PADS_CTL_RX_DATA_EN_1L);
	pads_writel(val, PADS_CTL);

	/* Take the PCIe interface module out of reset */
	tegra_periph_reset_deassert(tegra_pcie.pcie_xclk);

	/* Finally enable PCIe */
	val = afi_readl(AFI_CONFIGURATION) | AFI_CONFIGURATION_EN_FPCI;
	afi_writel(val, AFI_CONFIGURATION);

	val = (AFI_INTR_EN_INI_SLVERR | AFI_INTR_EN_INI_DECERR |
	       AFI_INTR_EN_TGT_SLVERR | AFI_INTR_EN_TGT_DECERR |
	       AFI_INTR_EN_TGT_WRERR | AFI_INTR_EN_DFPCI_DECERR);
	afi_writel(val, AFI_AFI_INTR_ENABLE);
	afi_writel(0xffffffff, AFI_SM_INTR_ENABLE);

	/* FIXME: No MSI for now, only INT */
	afi_writel(AFI_INTR_MASK_INT_MASK, AFI_INTR_MASK);

	/* Disable all execptions */
	afi_writel(0, AFI_FPCI_ERROR_MASKS);

	return 0;
}

static void tegra_pcie_xclk_clamp(bool clamp)
{
	u32 reg;

	reg = pmc_readl(PMC_SCRATCH42) & ~PMC_SCRATCH42_PCX_CLAMP;

	if (clamp)
		reg |= PMC_SCRATCH42_PCX_CLAMP;

	pmc_writel(reg, PMC_SCRATCH42);
}

static void tegra_pcie_power_off(void)
{
	tegra_periph_reset_assert(tegra_pcie.pcie_xclk);
	tegra_periph_reset_assert(tegra_pcie.afi_clk);
	tegra_periph_reset_assert(tegra_pcie.pex_clk);

	tegra_powergate_power_off(TEGRA_POWERGATE_PCIE);
	tegra_pcie_xclk_clamp(true);
}

static int tegra_pcie_power_regate(void)
{
	int err;

	tegra_pcie_power_off();

	tegra_pcie_xclk_clamp(true);

	tegra_periph_reset_assert(tegra_pcie.pcie_xclk);
	tegra_periph_reset_assert(tegra_pcie.afi_clk);

	err = tegra_powergate_sequence_power_up(TEGRA_POWERGATE_PCIE,
						tegra_pcie.pex_clk);
	if (err) {
		pr_err("PCIE: powerup sequence failed: %d\n", err);
		return err;
	}

	tegra_periph_reset_deassert(tegra_pcie.afi_clk);

	tegra_pcie_xclk_clamp(false);

	clk_prepare_enable(tegra_pcie.afi_clk);
	clk_prepare_enable(tegra_pcie.pex_clk);
	return clk_prepare_enable(tegra_pcie.pll_e);
}

static int tegra_pcie_clocks_get(void)
{
	int err;

	tegra_pcie.pex_clk = clk_get(NULL, "pex");
	if (IS_ERR(tegra_pcie.pex_clk))
		return PTR_ERR(tegra_pcie.pex_clk);

	tegra_pcie.afi_clk = clk_get(NULL, "afi");
	if (IS_ERR(tegra_pcie.afi_clk)) {
		err = PTR_ERR(tegra_pcie.afi_clk);
		goto err_afi_clk;
	}

	tegra_pcie.pcie_xclk = clk_get(NULL, "pcie_xclk");
	if (IS_ERR(tegra_pcie.pcie_xclk)) {
		err =  PTR_ERR(tegra_pcie.pcie_xclk);
		goto err_pcie_xclk;
	}

	tegra_pcie.pll_e = clk_get_sys(NULL, "pll_e");
	if (IS_ERR(tegra_pcie.pll_e)) {
		err = PTR_ERR(tegra_pcie.pll_e);
		goto err_pll_e;
	}

	return 0;

err_pll_e:
	clk_put(tegra_pcie.pcie_xclk);
err_pcie_xclk:
	clk_put(tegra_pcie.afi_clk);
err_afi_clk:
	clk_put(tegra_pcie.pex_clk);

	return err;
}

static void tegra_pcie_clocks_put(void)
{
	clk_put(tegra_pcie.pll_e);
	clk_put(tegra_pcie.pcie_xclk);
	clk_put(tegra_pcie.afi_clk);
	clk_put(tegra_pcie.pex_clk);
}

static int __init tegra_pcie_get_resources(void)
{
	struct resource *res_mmio = &tegra_pcie.res_mmio;
	int err;

	err = tegra_pcie_clocks_get();
	if (err) {
		pr_err("PCIE: failed to get clocks: %d\n", err);
		return err;
	}

	err = tegra_pcie_power_regate();
	if (err) {
		pr_err("PCIE: failed to power up: %d\n", err);
		goto err_pwr_on;
	}

	tegra_pcie.regs = ioremap_nocache(TEGRA_PCIE_BASE, PCIE_IOMAP_SZ);
	if (tegra_pcie.regs == NULL) {
		pr_err("PCIE: Failed to map PCI/AFI registers\n");
		err = -ENOMEM;
		goto err_map_reg;
	}

	err = request_resource(&iomem_resource, res_mmio);
	if (err) {
		pr_err("PCIE: Failed to request resources: %d\n", err);
		goto err_req_io;
	}

	tegra_pcie_io_base = ioremap_nocache(res_mmio->start,
					     resource_size(res_mmio));
	if (tegra_pcie_io_base == NULL) {
		pr_err("PCIE: Failed to map IO\n");
		err = -ENOMEM;
		goto err_map_io;
	}

	err = request_irq(INT_PCIE_INTR, tegra_pcie_isr,
			  IRQF_SHARED, "PCIE", &tegra_pcie);
	if (err) {
		pr_err("PCIE: Failed to register IRQ: %d\n", err);
		goto err_irq;
	}
	set_irq_flags(INT_PCIE_INTR, IRQF_VALID);

	return 0;

err_irq:
	iounmap(tegra_pcie_io_base);
err_map_io:
	release_resource(&tegra_pcie.res_mmio);
err_req_io:
	iounmap(tegra_pcie.regs);
err_map_reg:
	tegra_pcie_power_off();
err_pwr_on:
	tegra_pcie_clocks_put();

	return err;
}

/*
 * FIXME: If there are no PCIe cards attached, then calling this function
 * can result in the increase of the bootup time as there are big timeout
 * loops.
 */
#define TEGRA_PCIE_LINKUP_TIMEOUT	200	/* up to 1.2 seconds */
static bool tegra_pcie_check_link(struct tegra_pcie_port *pp, int idx,
				  u32 reset_reg)
{
	u32 reg;
	int retries = 3;
	int timeout;

	do {
		timeout = TEGRA_PCIE_LINKUP_TIMEOUT;
		while (timeout) {
			reg = readl(pp->base + RP_VEND_XP);

			if (reg & RP_VEND_XP_DL_UP)
				break;

			mdelay(1);
			timeout--;
		}

		if (!timeout)  {
			pr_err("PCIE: port %d: link down, retrying\n", idx);
			goto retry;
		}

		timeout = TEGRA_PCIE_LINKUP_TIMEOUT;
		while (timeout) {
			reg = readl(pp->base + RP_LINK_CONTROL_STATUS);

			if (reg & 0x20000000)
				return true;

			mdelay(1);
			timeout--;
		}

retry:
		/* Pulse the PEX reset */
		reg = afi_readl(reset_reg) | AFI_PEX_CTRL_RST;
		afi_writel(reg, reset_reg);
		mdelay(1);
		reg = afi_readl(reset_reg) & ~AFI_PEX_CTRL_RST;
		afi_writel(reg, reset_reg);

		retries--;
	} while (retries);

	return false;
}

static void __init tegra_pcie_add_port(int index, u32 offset, u32 reset_reg)
{
	struct tegra_pcie_port *pp;

	pp = tegra_pcie.port + tegra_pcie.num_ports;

	pp->index = -1;
	pp->base = tegra_pcie.regs + offset;
	pp->link_up = tegra_pcie_check_link(pp, index, reset_reg);

	if (!pp->link_up) {
		pp->base = NULL;
		printk(KERN_INFO "PCIE: port %d: link down, ignoring\n", index);
		return;
	}

	tegra_pcie.num_ports++;
	pp->index = index;
	pp->root_bus_nr = -1;
	memset(pp->res, 0, sizeof(pp->res));
}

int __init tegra_pcie_init(bool init_port0, bool init_port1)
{
	int err;

	if (!(init_port0 || init_port1))
		return -ENODEV;

	pcibios_min_mem = 0;

	err = tegra_pcie_get_resources();
	if (err)
		return err;

	err = tegra_pcie_enable_controller();
	if (err)
		return err;

	/* setup the AFI address translations */
	tegra_pcie_setup_translations();

	if (init_port0)
		tegra_pcie_add_port(0, RP0_OFFSET, AFI_PEX0_CTRL);

	if (init_port1)
		tegra_pcie_add_port(1, RP1_OFFSET, AFI_PEX1_CTRL);

	pci_common_init(&tegra_pcie_hw);

	return 0;
}
