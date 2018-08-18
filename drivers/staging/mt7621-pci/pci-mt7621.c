// SPDX-License-Identifier: GPL-2.0+
/**************************************************************************
 *
 *  BRIEF MODULE DESCRIPTION
 *     PCI init for Ralink RT2880 solution
 *
 *  Copyright 2007 Ralink Inc. (bruce_chang@ralinktech.com.tw)
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 *  THIS  SOFTWARE  IS PROVIDED   ``AS  IS'' AND   ANY  EXPRESS OR IMPLIED
 *  WARRANTIES,   INCLUDING, BUT NOT  LIMITED  TO, THE IMPLIED WARRANTIES OF
 *  MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 *  NO  EVENT  SHALL   THE AUTHOR  BE    LIABLE FOR ANY   DIRECT, INDIRECT,
 *  INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 *  NOT LIMITED   TO, PROCUREMENT OF  SUBSTITUTE GOODS  OR SERVICES; LOSS OF
 *  USE, DATA,  OR PROFITS; OR  BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 *  ANY THEORY OF LIABILITY, WHETHER IN  CONTRACT, STRICT LIABILITY, OR TORT
 *  (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 *  THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *  You should have received a copy of the  GNU General Public License along
 *  with this program; if not, write  to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 *
 **************************************************************************
 * May 2007 Bruce Chang
 * Initial Release
 *
 * May 2009 Bruce Chang
 * support RT2880/RT3883 PCIe
 *
 * May 2011 Bruce Chang
 * support RT6855/MT7620 PCIe
 *
 **************************************************************************
 */

#include <linux/bitops.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/of_platform.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/reset.h>
#include <mt7621.h>
#include <ralink_regs.h>

#include "../../pci/pci.h"

/*
 * These functions and structures provide the BIOS scan and mapping of the PCI
 * devices.
 */

#define RALINK_PCIE0_CLK_EN		BIT(24)
#define RALINK_PCIE1_CLK_EN		BIT(25)
#define RALINK_PCIE2_CLK_EN		BIT(26)

#define RALINK_PCI_CONFIG_ADDR		0x20
#define RALINK_PCI_CONFIG_DATA		0x24
#define RALINK_PCI_MEMBASE		0x28
#define RALINK_PCI_IOBASE		0x2C
#define RALINK_PCIE0_RST		BIT(24)
#define RALINK_PCIE1_RST		BIT(25)
#define RALINK_PCIE2_RST		BIT(26)

#define RALINK_PCI_PCICFG_ADDR		0x0000
#define RALINK_PCI_PCIMSK_ADDR		0x000C

#define RT6855_PCIE0_OFFSET		0x2000
#define RT6855_PCIE1_OFFSET		0x3000
#define RT6855_PCIE2_OFFSET		0x4000

#define RALINK_PCI_BAR0SETUP_ADDR	0x0010
#define RALINK_PCI_IMBASEBAR0_ADDR	0x0018
#define RALINK_PCI_ID			0x0030
#define RALINK_PCI_CLASS		0x0034
#define RALINK_PCI_SUBID		0x0038
#define RALINK_PCI_STATUS		0x0050

#define RALINK_PCIEPHY_P0P1_CTL_OFFSET	0x9000
#define RALINK_PCIEPHY_P2_CTL_OFFSET	0xA000

#define RALINK_PCI_MM_MAP_BASE		0x60000000
#define RALINK_PCI_IO_MAP_BASE		0x1e160000

#define ASSERT_SYSRST_PCIE(val)		\
	do {								\
		if (rt_sysc_r32(SYSC_REG_CHIP_REV) == 0x00030101)	\
			rt_sysc_m32(0, val, RALINK_RSTCTRL);		\
		else							\
			rt_sysc_m32(val, 0, RALINK_RSTCTRL);		\
	} while (0)
#define DEASSERT_SYSRST_PCIE(val)	\
	do {								\
		if (rt_sysc_r32(SYSC_REG_CHIP_REV) == 0x00030101)	\
			rt_sysc_m32(val, 0, RALINK_RSTCTRL);		\
		else							\
			rt_sysc_m32(0, val, RALINK_RSTCTRL);		\
	} while (0)

#define RALINK_CLKCFG1			0x30
#define RALINK_RSTCTRL			0x34
#define RALINK_GPIOMODE			0x60
#define RALINK_PCIE_CLK_GEN		0x7c
#define RALINK_PCIE_CLK_GEN1		0x80
//RALINK_RSTCTRL bit
#define RALINK_PCIE_RST			BIT(23)
#define RALINK_PCI_RST			BIT(24)
//RALINK_CLKCFG1 bit
#define RALINK_PCI_CLK_EN		BIT(19)
#define RALINK_PCIE_CLK_EN		BIT(21)

#define MEMORY_BASE 0x0
static int pcie_link_status = 0;

/**
 * struct mt7621_pcie_port - PCIe port information
 * @base: IO mapped register base
 * @list: port list
 * @pcie: pointer to PCIe host info
 * @reset: pointer to port reset control
 */
struct mt7621_pcie_port {
	void __iomem *base;
	struct list_head list;
	struct mt7621_pcie *pcie;
	struct reset_control *reset;
};

/**
 * struct mt7621_pcie - PCIe host information
 * @base: IO Mapped Register Base
 * @io: IO resource
 * @mem: non-prefetchable memory resource
 * @busn: bus range
 * @offset: IO / Memory offset
 * @dev: Pointer to PCIe device
 * @ports: pointer to PCIe port information
 */
struct mt7621_pcie {
	void __iomem *base;
	struct device *dev;
	struct resource io;
	struct resource mem;
	struct resource busn;
	struct {
		resource_size_t mem;
		resource_size_t io;
	} offset;
	struct list_head ports;
};

static inline u32 pcie_read(struct mt7621_pcie *pcie, u32 reg)
{
	return readl(pcie->base + reg);
}

static inline void pcie_write(struct mt7621_pcie *pcie, u32 val, u32 reg)
{
	writel(val, pcie->base + reg);
}

static inline u32 mt7621_pci_get_cfgaddr(unsigned int bus, unsigned int slot,
					 unsigned int func, unsigned int where)
{
	return (((where & 0xF00) >> 8) << 24) | (bus << 16) | (slot << 11) |
		(func << 8) | (where & 0xfc) | 0x80000000;
}

static void __iomem *mt7621_pcie_map_bus(struct pci_bus *bus,
					 unsigned int devfn, int where)
{
	struct mt7621_pcie *pcie = bus->sysdata;
	u32 address = mt7621_pci_get_cfgaddr(bus->number, PCI_SLOT(devfn),
					     PCI_FUNC(devfn), where);

	writel(address, pcie->base + RALINK_PCI_CONFIG_ADDR);

	return pcie->base + RALINK_PCI_CONFIG_DATA + (where & 3);
}

struct pci_ops mt7621_pci_ops = {
	.map_bus	= mt7621_pcie_map_bus,
	.read		= pci_generic_config_read,
	.write		= pci_generic_config_write,
};

static u32
read_config(struct mt7621_pcie *pcie, unsigned int dev, u32 reg)
{
	u32 address = mt7621_pci_get_cfgaddr(0, dev, 0, reg);

	pcie_write(pcie, address, RALINK_PCI_CONFIG_ADDR);
	return pcie_read(pcie, RALINK_PCI_CONFIG_DATA);
}

static void
write_config(struct mt7621_pcie *pcie, unsigned int dev, u32 reg, u32 val)
{
	u32 address = mt7621_pci_get_cfgaddr(0, dev, 0, reg);

	pcie_write(pcie, address, RALINK_PCI_CONFIG_ADDR);
	pcie_write(pcie, val, RALINK_PCI_CONFIG_DATA);
}

void
set_pcie_phy(struct mt7621_pcie *pcie, u32 offset,
	     int start_b, int bits, int val)
{
	u32 reg = pcie_read(pcie, offset);

	reg &= ~(((1 << bits) - 1) << start_b);
	reg |= val << start_b;
	pcie_write(pcie, reg, offset);
}

void
bypass_pipe_rst(struct mt7621_pcie *pcie)
{
	/* PCIe Port 0 */
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x02c), 12, 1, 0x01);	// rg_pe1_pipe_rst_b
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x02c),  4, 1, 0x01);	// rg_pe1_pipe_cmd_frc[4]
	/* PCIe Port 1 */
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x12c), 12, 1, 0x01);	// rg_pe1_pipe_rst_b
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x12c),  4, 1, 0x01);	// rg_pe1_pipe_cmd_frc[4]
	/* PCIe Port 2 */
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x02c), 12, 1, 0x01);	// rg_pe1_pipe_rst_b
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x02c),  4, 1, 0x01);	// rg_pe1_pipe_cmd_frc[4]
}

void
set_phy_for_ssc(struct mt7621_pcie *pcie)
{
	unsigned long reg = rt_sysc_r32(SYSC_REG_SYSTEM_CONFIG0);

	reg = (reg >> 6) & 0x7;
	/* Set PCIe Port0 & Port1 PHY to disable SSC */
	/* Debug Xtal Type */
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x400),  8, 1, 0x01);	// rg_pe1_frc_h_xtal_type
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x400),  9, 2, 0x00);	// rg_pe1_h_xtal_type
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x000),  4, 1, 0x01);	// rg_pe1_frc_phy_en	//Force Port 0 enable control
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x100),  4, 1, 0x01);	// rg_pe1_frc_phy_en	//Force Port 1 enable control
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x000),  5, 1, 0x00);	// rg_pe1_phy_en	//Port 0 disable
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x100),  5, 1, 0x00);	// rg_pe1_phy_en	//Port 1 disable
	if (reg <= 5 && reg >= 3) {	// 40MHz Xtal
		set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x490),  6, 2, 0x01);	// RG_PE1_H_PLL_PREDIV	//Pre-divider ratio (for host mode)
		printk("***** Xtal 40MHz *****\n");
	} else {			// 25MHz | 20MHz Xtal
		set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x490),  6, 2, 0x00);	// RG_PE1_H_PLL_PREDIV	//Pre-divider ratio (for host mode)
		if (reg >= 6) {
			printk("***** Xtal 25MHz *****\n");
			set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x4bc),  4, 2, 0x01);	// RG_PE1_H_PLL_FBKSEL	//Feedback clock select
			set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x49c),  0, 31, 0x18000000);	// RG_PE1_H_LCDDS_PCW_NCPO	//DDS NCPO PCW (for host mode)
			set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x4a4),  0, 16, 0x18d);	// RG_PE1_H_LCDDS_SSC_PRD	//DDS SSC dither period control
			set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x4a8),  0, 12, 0x4a);	// RG_PE1_H_LCDDS_SSC_DELTA	//DDS SSC dither amplitude control
			set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x4a8), 16, 12, 0x4a);	// RG_PE1_H_LCDDS_SSC_DELTA1	//DDS SSC dither amplitude control for initial
		} else {
			printk("***** Xtal 20MHz *****\n");
		}
	}
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x4a0),  5, 1, 0x01);	// RG_PE1_LCDDS_CLK_PH_INV	//DDS clock inversion
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x490), 22, 2, 0x02);	// RG_PE1_H_PLL_BC
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x490), 18, 4, 0x06);	// RG_PE1_H_PLL_BP
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x490), 12, 4, 0x02);	// RG_PE1_H_PLL_IR
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x490),  8, 4, 0x01);	// RG_PE1_H_PLL_IC
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x4ac), 16, 3, 0x00);	// RG_PE1_H_PLL_BR
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x490),  1, 3, 0x02);	// RG_PE1_PLL_DIVEN
	if (reg <= 5 && reg >= 3) {	// 40MHz Xtal
		set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x414),  6, 2, 0x01);	// rg_pe1_mstckdiv		//value of da_pe1_mstckdiv when force mode enable
		set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x414),  5, 1, 0x01);	// rg_pe1_frc_mstckdiv	//force mode enable of da_pe1_mstckdiv
	}
	/* Enable PHY and disable force mode */
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x000),  5, 1, 0x01);	// rg_pe1_phy_en	//Port 0 enable
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x100),  5, 1, 0x01);	// rg_pe1_phy_en	//Port 1 enable
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x000),  4, 1, 0x00);	// rg_pe1_frc_phy_en	//Force Port 0 disable control
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P0P1_CTL_OFFSET + 0x100),  4, 1, 0x00);	// rg_pe1_frc_phy_en	//Force Port 1 disable control

	/* Set PCIe Port2 PHY to disable SSC */
	/* Debug Xtal Type */
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x400),  8, 1, 0x01);	// rg_pe1_frc_h_xtal_type
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x400),  9, 2, 0x00);	// rg_pe1_h_xtal_type
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x000),  4, 1, 0x01);	// rg_pe1_frc_phy_en	//Force Port 0 enable control
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x000),  5, 1, 0x00);	// rg_pe1_phy_en	//Port 0 disable
	if (reg <= 5 && reg >= 3) {	// 40MHz Xtal
		set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x490),  6, 2, 0x01);	// RG_PE1_H_PLL_PREDIV	//Pre-divider ratio (for host mode)
	} else {			// 25MHz | 20MHz Xtal
		set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x490),  6, 2, 0x00);	// RG_PE1_H_PLL_PREDIV	//Pre-divider ratio (for host mode)
		if (reg >= 6) {		// 25MHz Xtal
			set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x4bc),  4, 2, 0x01);	// RG_PE1_H_PLL_FBKSEL	//Feedback clock select
			set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x49c),  0, 31, 0x18000000);	// RG_PE1_H_LCDDS_PCW_NCPO	//DDS NCPO PCW (for host mode)
			set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x4a4),  0, 16, 0x18d);	// RG_PE1_H_LCDDS_SSC_PRD	//DDS SSC dither period control
			set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x4a8),  0, 12, 0x4a);	// RG_PE1_H_LCDDS_SSC_DELTA	//DDS SSC dither amplitude control
			set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x4a8), 16, 12, 0x4a);	// RG_PE1_H_LCDDS_SSC_DELTA1	//DDS SSC dither amplitude control for initial
		}
	}
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x4a0),  5, 1, 0x01);	// RG_PE1_LCDDS_CLK_PH_INV	//DDS clock inversion
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x490), 22, 2, 0x02);	// RG_PE1_H_PLL_BC
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x490), 18, 4, 0x06);	// RG_PE1_H_PLL_BP
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x490), 12, 4, 0x02);	// RG_PE1_H_PLL_IR
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x490),  8, 4, 0x01);	// RG_PE1_H_PLL_IC
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x4ac), 16, 3, 0x00);	// RG_PE1_H_PLL_BR
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x490),  1, 3, 0x02);	// RG_PE1_PLL_DIVEN
	if (reg <= 5 && reg >= 3) {	// 40MHz Xtal
		set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x414),  6, 2, 0x01);	// rg_pe1_mstckdiv		//value of da_pe1_mstckdiv when force mode enable
		set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x414),  5, 1, 0x01);	// rg_pe1_frc_mstckdiv	//force mode enable of da_pe1_mstckdiv
	}
	/* Enable PHY and disable force mode */
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x000),  5, 1, 0x01);	// rg_pe1_phy_en	//Port 0 enable
	set_pcie_phy(pcie, (RALINK_PCIEPHY_P2_CTL_OFFSET + 0x000),  4, 1, 0x00);	// rg_pe1_frc_phy_en	//Force Port 0 disable control
}

static void setup_cm_memory_region(struct resource *mem_resource)
{
	resource_size_t mask;

	if (mips_cps_numiocu(0)) {
		/* FIXME: hardware doesn't accept mask values with 1s after
		 * 0s (e.g. 0xffef), so it would be great to warn if that's
		 * about to happen */
		mask = ~(mem_resource->end - mem_resource->start);

		write_gcr_reg1_base(mem_resource->start);
		write_gcr_reg1_mask(mask | CM_GCR_REGn_MASK_CMTGT_IOCU0);
		printk("PCI coherence region base: 0x%08llx, mask/settings: 0x%08llx\n",
			(unsigned long long)read_gcr_reg1_base(),
			(unsigned long long)read_gcr_reg1_mask());
	}
}

static int mt7621_pci_parse_request_of_pci_ranges(struct mt7621_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *node = dev->of_node;
	struct of_pci_range_parser parser;
	struct of_pci_range range;
	int err;

	if (of_pci_range_parser_init(&parser, node)) {
		dev_err(dev, "missing \"ranges\" property\n");
		return -EINVAL;
	}

	for_each_of_pci_range(&parser, &range) {
		struct resource *res = NULL;

		switch (range.flags & IORESOURCE_TYPE_BITS) {
		case IORESOURCE_IO:
			ioremap(range.cpu_addr, range.size);
			res = &pcie->io;
			pcie->offset.io = 0x00000000UL;
			break;
		case IORESOURCE_MEM:
			res = &pcie->mem;
			pcie->offset.mem = 0x00000000UL;
			break;
		}

		if (res != NULL)
			of_pci_range_to_resource(&range, node, res);
	}

	err = of_pci_parse_bus_range(node, &pcie->busn);
	if (err < 0) {
		dev_err(dev, "failed to parse bus ranges property: %d\n", err);
		pcie->busn.name = node->name;
		pcie->busn.start = 0;
		pcie->busn.end = 0xff;
		pcie->busn.flags = IORESOURCE_BUS;
	}

	return 0;
}

static int mt7621_pcie_parse_dt(struct mt7621_pcie *pcie)
{
	struct device *dev = pcie->dev;
	struct device_node *node = dev->of_node;
	struct resource regs;
	const char *type;
	int err;

	type = of_get_property(node, "device_type", NULL);
	if (!type || strcmp(type, "pci") != 0) {
		dev_err(dev, "invalid \"device_type\" %s\n", type);
		return -EINVAL;
	}

	err = of_address_to_resource(node, 0, &regs);
	if (err) {
		dev_err(dev, "missing \"reg\" property\n");
		return err;
	}

	pcie->base = devm_ioremap_resource(dev, &regs);
	if (IS_ERR(pcie->base))
		return PTR_ERR(pcie->base);

	return 0;
}

static int mt7621_pcie_request_resources(struct mt7621_pcie *pcie,
					 struct list_head *res)
{
	struct device *dev = pcie->dev;
	int err;

	pci_add_resource_offset(res, &pcie->io, pcie->offset.io);
	pci_add_resource_offset(res, &pcie->mem, pcie->offset.mem);
	pci_add_resource(res, &pcie->busn);

	err = devm_request_pci_bus_resources(dev, res);
	if (err < 0)
		return err;

	return 0;
}

static int mt7621_pcie_register_host(struct pci_host_bridge *host,
				     struct list_head *res)
{
	struct mt7621_pcie *pcie = pci_host_bridge_priv(host);

	list_splice_init(res, &host->windows);
	host->busnr = pcie->busn.start;
	host->dev.parent = pcie->dev;
	host->ops = &mt7621_pci_ops;
	host->map_irq = of_irq_parse_and_map_pci;
	host->swizzle_irq = pci_common_swizzle;
	host->sysdata = pcie;

	return pci_host_probe(host);
}

static int mt7621_pci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct mt7621_pcie *pcie;
	struct pci_host_bridge *bridge;
	int err;
	u32 val = 0;
	LIST_HEAD(res);

	if (!dev->of_node)
		return -ENODEV;

	bridge = devm_pci_alloc_host_bridge(dev, sizeof(*pcie));
	if (!bridge)
		return -ENODEV;

	pcie = pci_host_bridge_priv(bridge);
	pcie->dev = dev;
	platform_set_drvdata(pdev, pcie);
	INIT_LIST_HEAD(&pcie->ports);

	err = mt7621_pcie_parse_dt(pcie);
	if (err) {
		dev_err(dev, "Parsing DT failed\n");
		return err;
	}

	/* set resources limits */
	iomem_resource.start = 0;
	iomem_resource.end = ~0UL; /* no limit */
	ioport_resource.start = 0;
	ioport_resource.end = ~0UL; /* no limit */

	val = RALINK_PCIE0_RST;
	val |= RALINK_PCIE1_RST;
	val |= RALINK_PCIE2_RST;

	ASSERT_SYSRST_PCIE(RALINK_PCIE0_RST | RALINK_PCIE1_RST | RALINK_PCIE2_RST);

	*(unsigned int *)(0xbe000060) &= ~(0x3<<10 | 0x3<<3);
	*(unsigned int *)(0xbe000060) |= 0x1<<10 | 0x1<<3;
	mdelay(100);
	*(unsigned int *)(0xbe000600) |= 0x1<<19 | 0x1<<8 | 0x1<<7; // use GPIO19/GPIO8/GPIO7 (PERST_N/UART_RXD3/UART_TXD3)
	mdelay(100);
	*(unsigned int *)(0xbe000620) &= ~(0x1<<19 | 0x1<<8 | 0x1<<7);		// clear DATA

	mdelay(100);

	val = RALINK_PCIE0_RST;
	val |= RALINK_PCIE1_RST;
	val |= RALINK_PCIE2_RST;

	DEASSERT_SYSRST_PCIE(val);

	if ((*(unsigned int *)(0xbe00000c)&0xFFFF) == 0x0101) // MT7621 E2
		bypass_pipe_rst(pcie);
	set_phy_for_ssc(pcie);

	val = read_config(pcie, 0, 0x70c);
	printk("Port 0 N_FTS = %x\n", (unsigned int)val);

	val = read_config(pcie, 1, 0x70c);
	printk("Port 1 N_FTS = %x\n", (unsigned int)val);

	val = read_config(pcie, 2, 0x70c);
	printk("Port 2 N_FTS = %x\n", (unsigned int)val);

	rt_sysc_m32(0, RALINK_PCIE_RST, RALINK_RSTCTRL);
	rt_sysc_m32(0x30, 2 << 4, SYSC_REG_SYSTEM_CONFIG1);

	rt_sysc_m32(0x80000000, 0, RALINK_PCIE_CLK_GEN);
	rt_sysc_m32(0x7f000000, 0xa << 24, RALINK_PCIE_CLK_GEN1);
	rt_sysc_m32(0, 0x80000000, RALINK_PCIE_CLK_GEN);

	mdelay(50);
	rt_sysc_m32(RALINK_PCIE_RST, 0, RALINK_RSTCTRL);

	/* Use GPIO control instead of PERST_N */
	*(unsigned int *)(0xbe000620) |= 0x1<<19 | 0x1<<8 | 0x1<<7;		// set DATA
	mdelay(1000);

	if ((pcie_read(pcie, RT6855_PCIE0_OFFSET + RALINK_PCI_STATUS) & 0x1) == 0) {
		printk("PCIE0 no card, disable it(RST&CLK)\n");
		ASSERT_SYSRST_PCIE(RALINK_PCIE0_RST);
		rt_sysc_m32(RALINK_PCIE0_CLK_EN, 0, RALINK_CLKCFG1);
		pcie_link_status &= ~(1<<0);
	} else {
		pcie_link_status |= 1<<0;
		val = pcie_read(pcie, RALINK_PCI_PCIMSK_ADDR);
		val |= (1<<20); // enable pcie1 interrupt
		pcie_write(pcie, val, RALINK_PCI_PCIMSK_ADDR);
	}

	if ((pcie_read(pcie, RT6855_PCIE1_OFFSET + RALINK_PCI_STATUS) & 0x1) == 0) {
		printk("PCIE1 no card, disable it(RST&CLK)\n");
		ASSERT_SYSRST_PCIE(RALINK_PCIE1_RST);
		rt_sysc_m32(RALINK_PCIE1_CLK_EN, 0, RALINK_CLKCFG1);
		pcie_link_status &= ~(1<<1);
	} else {
		pcie_link_status |= 1<<1;
		val = pcie_read(pcie, RALINK_PCI_PCIMSK_ADDR);
		val |= (1<<21); // enable pcie1 interrupt
		pcie_write(pcie, val, RALINK_PCI_PCIMSK_ADDR);
	}

	if ((pcie_read(pcie, RT6855_PCIE2_OFFSET + RALINK_PCI_STATUS) & 0x1) == 0) {
		printk("PCIE2 no card, disable it(RST&CLK)\n");
		ASSERT_SYSRST_PCIE(RALINK_PCIE2_RST);
		rt_sysc_m32(RALINK_PCIE2_CLK_EN, 0, RALINK_CLKCFG1);
		pcie_link_status &= ~(1<<2);
	} else {
		pcie_link_status |= 1<<2;
		val = pcie_read(pcie, RALINK_PCI_PCIMSK_ADDR);
		val |= (1<<22); // enable pcie2 interrupt
		pcie_write(pcie, val, RALINK_PCI_PCIMSK_ADDR);
	}

	if (pcie_link_status == 0)
		return 0;

/*
pcie(2/1/0) link status	pcie2_num	pcie1_num	pcie0_num
3'b000			x		x		x
3'b001			x		x		0
3'b010			x		0		x
3'b011			x		1		0
3'b100			0		x		x
3'b101			1		x		0
3'b110			1		0		x
3'b111			2		1		0
*/
	switch (pcie_link_status) {
	case 2:
		val = pcie_read(pcie, RALINK_PCI_PCICFG_ADDR);
		val &= ~0x00ff0000;
		val |= 0x1 << 16;	// port 0
		val |= 0x0 << 20;	// port 1
		pcie_write(pcie, val, RALINK_PCI_PCICFG_ADDR);
		break;
	case 4:
		val = pcie_read(pcie, RALINK_PCI_PCICFG_ADDR);
		val &= ~0x0fff0000;
		val |= 0x1 << 16;	//port0
		val |= 0x2 << 20;	//port1
		val |= 0x0 << 24;	//port2
		pcie_write(pcie, val, RALINK_PCI_PCICFG_ADDR);
		break;
	case 5:
		val = pcie_read(pcie, RALINK_PCI_PCICFG_ADDR);
		val &= ~0x0fff0000;
		val |= 0x0 << 16;	//port0
		val |= 0x2 << 20;	//port1
		val |= 0x1 << 24;	//port2
		pcie_write(pcie, val, RALINK_PCI_PCICFG_ADDR);
		break;
	case 6:
		val = pcie_read(pcie, RALINK_PCI_PCICFG_ADDR);
		val &= ~0x0fff0000;
		val |= 0x2 << 16;	//port0
		val |= 0x0 << 20;	//port1
		val |= 0x1 << 24;	//port2
		pcie_write(pcie, val, RALINK_PCI_PCICFG_ADDR);
		break;
	}

/*
	ioport_resource.start = mt7621_res_pci_io1.start;
	ioport_resource.end = mt7621_res_pci_io1.end;
*/

	pcie_write(pcie, 0xffffffff, RALINK_PCI_MEMBASE);
	pcie_write(pcie, RALINK_PCI_IO_MAP_BASE, RALINK_PCI_IOBASE);

	//PCIe0
	if ((pcie_link_status & 0x1) != 0) {
		/* open 7FFF:2G; ENABLE */
		pcie_write(pcie, 0x7FFF0001,
			   RT6855_PCIE0_OFFSET + RALINK_PCI_BAR0SETUP_ADDR);
		pcie_write(pcie, MEMORY_BASE,
			   RT6855_PCIE0_OFFSET + RALINK_PCI_IMBASEBAR0_ADDR);
		pcie_write(pcie, 0x06040001,
			   RT6855_PCIE0_OFFSET + RALINK_PCI_CLASS);
		printk("PCIE0 enabled\n");
	}

	//PCIe1
	if ((pcie_link_status & 0x2) != 0) {
		/* open 7FFF:2G; ENABLE */
		pcie_write(pcie, 0x7FFF0001,
			   RT6855_PCIE1_OFFSET + RALINK_PCI_BAR0SETUP_ADDR);
		pcie_write(pcie, MEMORY_BASE,
			   RT6855_PCIE1_OFFSET + RALINK_PCI_IMBASEBAR0_ADDR);
		pcie_write(pcie, 0x06040001,
			   RT6855_PCIE1_OFFSET + RALINK_PCI_CLASS);
		printk("PCIE1 enabled\n");
	}

	//PCIe2
	if ((pcie_link_status & 0x4) != 0) {
		/* open 7FFF:2G; ENABLE */
		pcie_write(pcie, 0x7FFF0001,
			   RT6855_PCIE2_OFFSET + RALINK_PCI_BAR0SETUP_ADDR);
		pcie_write(pcie, MEMORY_BASE,
			   RT6855_PCIE2_OFFSET + RALINK_PCI_IMBASEBAR0_ADDR);
		pcie_write(pcie, 0x06040001,
			   RT6855_PCIE2_OFFSET + RALINK_PCI_CLASS);
		printk("PCIE2 enabled\n");
	}

	switch (pcie_link_status) {
	case 7:
		val = read_config(pcie, 2, 0x4);
		write_config(pcie, 2, 0x4, val|0x4);
		val = read_config(pcie, 2, 0x70c);
		val &= ~(0xff)<<8;
		val |= 0x50<<8;
		write_config(pcie, 2, 0x70c, val);
	case 3:
	case 5:
	case 6:
		val = read_config(pcie, 1, 0x4);
		write_config(pcie, 1, 0x4, val|0x4);
		val = read_config(pcie, 1, 0x70c);
		val &= ~(0xff)<<8;
		val |= 0x50<<8;
		write_config(pcie, 1, 0x70c, val);
	default:
		val = read_config(pcie, 0, 0x4);
		write_config(pcie, 0, 0x4, val|0x4); //bus master enable
		val = read_config(pcie, 0, 0x70c);
		val &= ~(0xff)<<8;
		val |= 0x50<<8;
		write_config(pcie, 0, 0x70c, val);
	}

	err = mt7621_pci_parse_request_of_pci_ranges(pcie);
	if (err) {
		dev_err(dev, "Error requesting pci resources from ranges");
		return err;
	}

	setup_cm_memory_region(&pcie->mem);

	err = mt7621_pcie_request_resources(pcie, &res);
	if (err) {
		dev_err(dev, "Error requesting resources\n");
		return err;
	}

	err = mt7621_pcie_register_host(bridge, &res);
	if (err) {
		dev_err(dev, "Error registering host\n");
		return err;
	}

	return 0;
}

static const struct of_device_id mt7621_pci_ids[] = {
	{ .compatible = "mediatek,mt7621-pci" },
	{},
};
MODULE_DEVICE_TABLE(of, mt7621_pci_ids);

static struct platform_driver mt7621_pci_driver = {
	.probe = mt7621_pci_probe,
	.driver = {
		.name = "mt7621-pci",
		.of_match_table = of_match_ptr(mt7621_pci_ids),
	},
};

static int __init mt7621_pci_init(void)
{
	return platform_driver_register(&mt7621_pci_driver);
}

arch_initcall(mt7621_pci_init);
