// SPDX-License-Identifier: GPL-2.0
/*
 * Support for V3 Semiconductor PCI Local Bus to PCI Bridge
 * Copyright (C) 2017 Linus Walleij <linus.walleij@linaro.org>
 *
 * Based on the code from arch/arm/mach-integrator/pci_v3.c
 * Copyright (C) 1999 ARM Limited
 * Copyright (C) 2000-2001 Deep Blue Solutions Ltd
 *
 * Contributors to the old driver include:
 * Russell King <linux@armlinux.org.uk>
 * David A. Rusling <david.rusling@linaro.org> (uHAL, ARM Firmware suite)
 * Rob Herring <robh@kernel.org>
 * Liviu Dudau <Liviu.Dudau@arm.com>
 * Grant Likely <grant.likely@secretlab.ca>
 * Arnd Bergmann <arnd@arndb.de>
 * Bjorn Helgaas <bhelgaas@google.com>
 */
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/of_address.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/of_pci.h>
#include <linux/pci.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/bitops.h>
#include <linux/irq.h>
#include <linux/mfd/syscon.h>
#include <linux/regmap.h>
#include <linux/clk.h>

#include "../pci.h"

#define V3_PCI_VENDOR			0x00000000
#define V3_PCI_DEVICE			0x00000002
#define V3_PCI_CMD			0x00000004
#define V3_PCI_STAT			0x00000006
#define V3_PCI_CC_REV			0x00000008
#define V3_PCI_HDR_CFG			0x0000000C
#define V3_PCI_IO_BASE			0x00000010
#define V3_PCI_BASE0			0x00000014
#define V3_PCI_BASE1			0x00000018
#define V3_PCI_SUB_VENDOR		0x0000002C
#define V3_PCI_SUB_ID			0x0000002E
#define V3_PCI_ROM			0x00000030
#define V3_PCI_BPARAM			0x0000003C
#define V3_PCI_MAP0			0x00000040
#define V3_PCI_MAP1			0x00000044
#define V3_PCI_INT_STAT			0x00000048
#define V3_PCI_INT_CFG			0x0000004C
#define V3_LB_BASE0			0x00000054
#define V3_LB_BASE1			0x00000058
#define V3_LB_MAP0			0x0000005E
#define V3_LB_MAP1			0x00000062
#define V3_LB_BASE2			0x00000064
#define V3_LB_MAP2			0x00000066
#define V3_LB_SIZE			0x00000068
#define V3_LB_IO_BASE			0x0000006E
#define V3_FIFO_CFG			0x00000070
#define V3_FIFO_PRIORITY		0x00000072
#define V3_FIFO_STAT			0x00000074
#define V3_LB_ISTAT			0x00000076
#define V3_LB_IMASK			0x00000077
#define V3_SYSTEM			0x00000078
#define V3_LB_CFG			0x0000007A
#define V3_PCI_CFG			0x0000007C
#define V3_DMA_PCI_ADR0			0x00000080
#define V3_DMA_PCI_ADR1			0x00000090
#define V3_DMA_LOCAL_ADR0		0x00000084
#define V3_DMA_LOCAL_ADR1		0x00000094
#define V3_DMA_LENGTH0			0x00000088
#define V3_DMA_LENGTH1			0x00000098
#define V3_DMA_CSR0			0x0000008B
#define V3_DMA_CSR1			0x0000009B
#define V3_DMA_CTLB_ADR0		0x0000008C
#define V3_DMA_CTLB_ADR1		0x0000009C
#define V3_DMA_DELAY			0x000000E0
#define V3_MAIL_DATA			0x000000C0
#define V3_PCI_MAIL_IEWR		0x000000D0
#define V3_PCI_MAIL_IERD		0x000000D2
#define V3_LB_MAIL_IEWR			0x000000D4
#define V3_LB_MAIL_IERD			0x000000D6
#define V3_MAIL_WR_STAT			0x000000D8
#define V3_MAIL_RD_STAT			0x000000DA
#define V3_QBA_MAP			0x000000DC

/* PCI STATUS bits */
#define V3_PCI_STAT_PAR_ERR		BIT(15)
#define V3_PCI_STAT_SYS_ERR		BIT(14)
#define V3_PCI_STAT_M_ABORT_ERR		BIT(13)
#define V3_PCI_STAT_T_ABORT_ERR		BIT(12)

/* LB ISTAT bits */
#define V3_LB_ISTAT_MAILBOX		BIT(7)
#define V3_LB_ISTAT_PCI_RD		BIT(6)
#define V3_LB_ISTAT_PCI_WR		BIT(5)
#define V3_LB_ISTAT_PCI_INT		BIT(4)
#define V3_LB_ISTAT_PCI_PERR		BIT(3)
#define V3_LB_ISTAT_I2O_QWR		BIT(2)
#define V3_LB_ISTAT_DMA1		BIT(1)
#define V3_LB_ISTAT_DMA0		BIT(0)

/* PCI COMMAND bits */
#define V3_COMMAND_M_FBB_EN		BIT(9)
#define V3_COMMAND_M_SERR_EN		BIT(8)
#define V3_COMMAND_M_PAR_EN		BIT(6)
#define V3_COMMAND_M_MASTER_EN		BIT(2)
#define V3_COMMAND_M_MEM_EN		BIT(1)
#define V3_COMMAND_M_IO_EN		BIT(0)

/* SYSTEM bits */
#define V3_SYSTEM_M_RST_OUT		BIT(15)
#define V3_SYSTEM_M_LOCK		BIT(14)
#define V3_SYSTEM_UNLOCK		0xa05f

/* PCI CFG bits */
#define V3_PCI_CFG_M_I2O_EN		BIT(15)
#define V3_PCI_CFG_M_IO_REG_DIS		BIT(14)
#define V3_PCI_CFG_M_IO_DIS		BIT(13)
#define V3_PCI_CFG_M_EN3V		BIT(12)
#define V3_PCI_CFG_M_RETRY_EN		BIT(10)
#define V3_PCI_CFG_M_AD_LOW1		BIT(9)
#define V3_PCI_CFG_M_AD_LOW0		BIT(8)
/*
 * This is the value applied to C/BE[3:1], with bit 0 always held 0
 * during DMA access.
 */
#define V3_PCI_CFG_M_RTYPE_SHIFT	5
#define V3_PCI_CFG_M_WTYPE_SHIFT	1
#define V3_PCI_CFG_TYPE_DEFAULT		0x3

/* PCI BASE bits (PCI -> Local Bus) */
#define V3_PCI_BASE_M_ADR_BASE		0xFFF00000U
#define V3_PCI_BASE_M_ADR_BASEL		0x000FFF00U
#define V3_PCI_BASE_M_PREFETCH		BIT(3)
#define V3_PCI_BASE_M_TYPE		(3 << 1)
#define V3_PCI_BASE_M_IO		BIT(0)

/* PCI MAP bits (PCI -> Local bus) */
#define V3_PCI_MAP_M_MAP_ADR		0xFFF00000U
#define V3_PCI_MAP_M_RD_POST_INH	BIT(15)
#define V3_PCI_MAP_M_ROM_SIZE		(3 << 10)
#define V3_PCI_MAP_M_SWAP		(3 << 8)
#define V3_PCI_MAP_M_ADR_SIZE		0x000000F0U
#define V3_PCI_MAP_M_REG_EN		BIT(1)
#define V3_PCI_MAP_M_ENABLE		BIT(0)

/* LB_BASE0,1 bits (Local bus -> PCI) */
#define V3_LB_BASE_ADR_BASE		0xfff00000U
#define V3_LB_BASE_SWAP			(3 << 8)
#define V3_LB_BASE_ADR_SIZE		(15 << 4)
#define V3_LB_BASE_PREFETCH		BIT(3)
#define V3_LB_BASE_ENABLE		BIT(0)

#define V3_LB_BASE_ADR_SIZE_1MB		(0 << 4)
#define V3_LB_BASE_ADR_SIZE_2MB		(1 << 4)
#define V3_LB_BASE_ADR_SIZE_4MB		(2 << 4)
#define V3_LB_BASE_ADR_SIZE_8MB		(3 << 4)
#define V3_LB_BASE_ADR_SIZE_16MB	(4 << 4)
#define V3_LB_BASE_ADR_SIZE_32MB	(5 << 4)
#define V3_LB_BASE_ADR_SIZE_64MB	(6 << 4)
#define V3_LB_BASE_ADR_SIZE_128MB	(7 << 4)
#define V3_LB_BASE_ADR_SIZE_256MB	(8 << 4)
#define V3_LB_BASE_ADR_SIZE_512MB	(9 << 4)
#define V3_LB_BASE_ADR_SIZE_1GB		(10 << 4)
#define V3_LB_BASE_ADR_SIZE_2GB		(11 << 4)

#define v3_addr_to_lb_base(a)	((a) & V3_LB_BASE_ADR_BASE)

/* LB_MAP0,1 bits (Local bus -> PCI) */
#define V3_LB_MAP_MAP_ADR		0xfff0U
#define V3_LB_MAP_TYPE			(7 << 1)
#define V3_LB_MAP_AD_LOW_EN		BIT(0)

#define V3_LB_MAP_TYPE_IACK		(0 << 1)
#define V3_LB_MAP_TYPE_IO		(1 << 1)
#define V3_LB_MAP_TYPE_MEM		(3 << 1)
#define V3_LB_MAP_TYPE_CONFIG		(5 << 1)
#define V3_LB_MAP_TYPE_MEM_MULTIPLE	(6 << 1)

#define v3_addr_to_lb_map(a)	(((a) >> 16) & V3_LB_MAP_MAP_ADR)

/* LB_BASE2 bits (Local bus -> PCI IO) */
#define V3_LB_BASE2_ADR_BASE		0xff00U
#define V3_LB_BASE2_SWAP_AUTO		(3 << 6)
#define V3_LB_BASE2_ENABLE		BIT(0)

#define v3_addr_to_lb_base2(a)	(((a) >> 16) & V3_LB_BASE2_ADR_BASE)

/* LB_MAP2 bits (Local bus -> PCI IO) */
#define V3_LB_MAP2_MAP_ADR		0xff00U

#define v3_addr_to_lb_map2(a)	(((a) >> 16) & V3_LB_MAP2_MAP_ADR)

/* FIFO priority bits */
#define V3_FIFO_PRIO_LOCAL		BIT(12)
#define V3_FIFO_PRIO_LB_RD1_FLUSH_EOB	BIT(10)
#define V3_FIFO_PRIO_LB_RD1_FLUSH_AP1	BIT(11)
#define V3_FIFO_PRIO_LB_RD1_FLUSH_ANY	(BIT(10)|BIT(11))
#define V3_FIFO_PRIO_LB_RD0_FLUSH_EOB	BIT(8)
#define V3_FIFO_PRIO_LB_RD0_FLUSH_AP1	BIT(9)
#define V3_FIFO_PRIO_LB_RD0_FLUSH_ANY	(BIT(8)|BIT(9))
#define V3_FIFO_PRIO_PCI		BIT(4)
#define V3_FIFO_PRIO_PCI_RD1_FLUSH_EOB	BIT(2)
#define V3_FIFO_PRIO_PCI_RD1_FLUSH_AP1	BIT(3)
#define V3_FIFO_PRIO_PCI_RD1_FLUSH_ANY	(BIT(2)|BIT(3))
#define V3_FIFO_PRIO_PCI_RD0_FLUSH_EOB	BIT(0)
#define V3_FIFO_PRIO_PCI_RD0_FLUSH_AP1	BIT(1)
#define V3_FIFO_PRIO_PCI_RD0_FLUSH_ANY	(BIT(0)|BIT(1))

/* Local bus configuration bits */
#define V3_LB_CFG_LB_TO_64_CYCLES	0x0000
#define V3_LB_CFG_LB_TO_256_CYCLES	BIT(13)
#define V3_LB_CFG_LB_TO_512_CYCLES	BIT(14)
#define V3_LB_CFG_LB_TO_1024_CYCLES	(BIT(13)|BIT(14))
#define V3_LB_CFG_LB_RST		BIT(12)
#define V3_LB_CFG_LB_PPC_RDY		BIT(11)
#define V3_LB_CFG_LB_LB_INT		BIT(10)
#define V3_LB_CFG_LB_ERR_EN		BIT(9)
#define V3_LB_CFG_LB_RDY_EN		BIT(8)
#define V3_LB_CFG_LB_BE_IMODE		BIT(7)
#define V3_LB_CFG_LB_BE_OMODE		BIT(6)
#define V3_LB_CFG_LB_ENDIAN		BIT(5)
#define V3_LB_CFG_LB_PARK_EN		BIT(4)
#define V3_LB_CFG_LB_FBB_DIS		BIT(2)

/* ARM Integrator-specific extended control registers */
#define INTEGRATOR_SC_PCI_OFFSET	0x18
#define INTEGRATOR_SC_PCI_ENABLE	BIT(0)
#define INTEGRATOR_SC_PCI_INTCLR	BIT(1)
#define INTEGRATOR_SC_LBFADDR_OFFSET	0x20
#define INTEGRATOR_SC_LBFCODE_OFFSET	0x24

struct v3_pci {
	struct device *dev;
	void __iomem *base;
	void __iomem *config_base;
	struct pci_bus *bus;
	u32 config_mem;
	u32 non_pre_mem;
	u32 pre_mem;
	phys_addr_t non_pre_bus_addr;
	phys_addr_t pre_bus_addr;
	struct regmap *map;
};

/*
 * The V3 PCI interface chip in Integrator provides several windows from
 * local bus memory into the PCI memory areas. Unfortunately, there
 * are not really enough windows for our usage, therefore we reuse
 * one of the windows for access to PCI configuration space. On the
 * Integrator/AP, the memory map is as follows:
 *
 * Local Bus Memory         Usage
 *
 * 40000000 - 4FFFFFFF      PCI memory.  256M non-prefetchable
 * 50000000 - 5FFFFFFF      PCI memory.  256M prefetchable
 * 60000000 - 60FFFFFF      PCI IO.  16M
 * 61000000 - 61FFFFFF      PCI Configuration. 16M
 *
 * There are three V3 windows, each described by a pair of V3 registers.
 * These are LB_BASE0/LB_MAP0, LB_BASE1/LB_MAP1 and LB_BASE2/LB_MAP2.
 * Base0 and Base1 can be used for any type of PCI memory access.   Base2
 * can be used either for PCI I/O or for I20 accesses.  By default, uHAL
 * uses this only for PCI IO space.
 *
 * Normally these spaces are mapped using the following base registers:
 *
 * Usage Local Bus Memory         Base/Map registers used
 *
 * Mem   40000000 - 4FFFFFFF      LB_BASE0/LB_MAP0
 * Mem   50000000 - 5FFFFFFF      LB_BASE1/LB_MAP1
 * IO    60000000 - 60FFFFFF      LB_BASE2/LB_MAP2
 * Cfg   61000000 - 61FFFFFF
 *
 * This means that I20 and PCI configuration space accesses will fail.
 * When PCI configuration accesses are needed (via the uHAL PCI
 * configuration space primitives) we must remap the spaces as follows:
 *
 * Usage Local Bus Memory         Base/Map registers used
 *
 * Mem   40000000 - 4FFFFFFF      LB_BASE0/LB_MAP0
 * Mem   50000000 - 5FFFFFFF      LB_BASE0/LB_MAP0
 * IO    60000000 - 60FFFFFF      LB_BASE2/LB_MAP2
 * Cfg   61000000 - 61FFFFFF      LB_BASE1/LB_MAP1
 *
 * To make this work, the code depends on overlapping windows working.
 * The V3 chip translates an address by checking its range within
 * each of the BASE/MAP pairs in turn (in ascending register number
 * order).  It will use the first matching pair.   So, for example,
 * if the same address is mapped by both LB_BASE0/LB_MAP0 and
 * LB_BASE1/LB_MAP1, the V3 will use the translation from
 * LB_BASE0/LB_MAP0.
 *
 * To allow PCI Configuration space access, the code enlarges the
 * window mapped by LB_BASE0/LB_MAP0 from 256M to 512M.  This occludes
 * the windows currently mapped by LB_BASE1/LB_MAP1 so that it can
 * be remapped for use by configuration cycles.
 *
 * At the end of the PCI Configuration space accesses,
 * LB_BASE1/LB_MAP1 is reset to map PCI Memory.  Finally the window
 * mapped by LB_BASE0/LB_MAP0 is reduced in size from 512M to 256M to
 * reveal the now restored LB_BASE1/LB_MAP1 window.
 *
 * NOTE: We do not set up I2O mapping.  I suspect that this is only
 * for an intelligent (target) device.  Using I2O disables most of
 * the mappings into PCI memory.
 */
static void __iomem *v3_map_bus(struct pci_bus *bus,
				unsigned int devfn, int offset)
{
	struct v3_pci *v3 = bus->sysdata;
	unsigned int address, mapaddress, busnr;

	busnr = bus->number;
	if (busnr == 0) {
		int slot = PCI_SLOT(devfn);

		/*
		 * local bus segment so need a type 0 config cycle
		 *
		 * build the PCI configuration "address" with one-hot in
		 * A31-A11
		 *
		 * mapaddress:
		 *  3:1 = config cycle (101)
		 *  0   = PCI A1 & A0 are 0 (0)
		 */
		address = PCI_FUNC(devfn) << 8;
		mapaddress = V3_LB_MAP_TYPE_CONFIG;

		if (slot > 12)
			/*
			 * high order bits are handled by the MAP register
			 */
			mapaddress |= BIT(slot - 5);
		else
			/*
			 * low order bits handled directly in the address
			 */
			address |= BIT(slot + 11);
	} else {
		/*
		 * not the local bus segment so need a type 1 config cycle
		 *
		 * address:
		 *  23:16 = bus number
		 *  15:11 = slot number (7:3 of devfn)
		 *  10:8  = func number (2:0 of devfn)
		 *
		 * mapaddress:
		 *  3:1 = config cycle (101)
		 *  0   = PCI A1 & A0 from host bus (1)
		 */
		mapaddress = V3_LB_MAP_TYPE_CONFIG | V3_LB_MAP_AD_LOW_EN;
		address = (busnr << 16) | (devfn << 8);
	}

	/*
	 * Set up base0 to see all 512Mbytes of memory space (not
	 * prefetchable), this frees up base1 for re-use by
	 * configuration memory
	 */
	writel(v3_addr_to_lb_base(v3->non_pre_mem) |
	       V3_LB_BASE_ADR_SIZE_512MB | V3_LB_BASE_ENABLE,
	       v3->base + V3_LB_BASE0);

	/*
	 * Set up base1/map1 to point into configuration space.
	 * The config mem is always 16MB.
	 */
	writel(v3_addr_to_lb_base(v3->config_mem) |
	       V3_LB_BASE_ADR_SIZE_16MB | V3_LB_BASE_ENABLE,
	       v3->base + V3_LB_BASE1);
	writew(mapaddress, v3->base + V3_LB_MAP1);

	return v3->config_base + address + offset;
}

static void v3_unmap_bus(struct v3_pci *v3)
{
	/*
	 * Reassign base1 for use by prefetchable PCI memory
	 */
	writel(v3_addr_to_lb_base(v3->pre_mem) |
	       V3_LB_BASE_ADR_SIZE_256MB | V3_LB_BASE_PREFETCH |
	       V3_LB_BASE_ENABLE,
	       v3->base + V3_LB_BASE1);
	writew(v3_addr_to_lb_map(v3->pre_bus_addr) |
	       V3_LB_MAP_TYPE_MEM, /* was V3_LB_MAP_TYPE_MEM_MULTIPLE */
	       v3->base + V3_LB_MAP1);

	/*
	 * And shrink base0 back to a 256M window (NOTE: MAP0 already correct)
	 */
	writel(v3_addr_to_lb_base(v3->non_pre_mem) |
	       V3_LB_BASE_ADR_SIZE_256MB | V3_LB_BASE_ENABLE,
	       v3->base + V3_LB_BASE0);
}

static int v3_pci_read_config(struct pci_bus *bus, unsigned int fn,
			      int config, int size, u32 *value)
{
	struct v3_pci *v3 = bus->sysdata;
	int ret;

	dev_dbg(&bus->dev,
		"[read]  slt: %.2d, fnc: %d, cnf: 0x%.2X, val (%d bytes): 0x%.8X\n",
		PCI_SLOT(fn), PCI_FUNC(fn), config, size, *value);
	ret = pci_generic_config_read(bus, fn, config, size, value);
	v3_unmap_bus(v3);
	return ret;
}

static int v3_pci_write_config(struct pci_bus *bus, unsigned int fn,
				    int config, int size, u32 value)
{
	struct v3_pci *v3 = bus->sysdata;
	int ret;

	dev_dbg(&bus->dev,
		"[write] slt: %.2d, fnc: %d, cnf: 0x%.2X, val (%d bytes): 0x%.8X\n",
		PCI_SLOT(fn), PCI_FUNC(fn), config, size, value);
	ret = pci_generic_config_write(bus, fn, config, size, value);
	v3_unmap_bus(v3);
	return ret;
}

static struct pci_ops v3_pci_ops = {
	.map_bus = v3_map_bus,
	.read = v3_pci_read_config,
	.write = v3_pci_write_config,
};

static irqreturn_t v3_irq(int irq, void *data)
{
	struct v3_pci *v3 = data;
	struct device *dev = v3->dev;
	u32 status;

	status = readw(v3->base + V3_PCI_STAT);
	if (status & V3_PCI_STAT_PAR_ERR)
		dev_err(dev, "parity error interrupt\n");
	if (status & V3_PCI_STAT_SYS_ERR)
		dev_err(dev, "system error interrupt\n");
	if (status & V3_PCI_STAT_M_ABORT_ERR)
		dev_err(dev, "master abort error interrupt\n");
	if (status & V3_PCI_STAT_T_ABORT_ERR)
		dev_err(dev, "target abort error interrupt\n");
	writew(status, v3->base + V3_PCI_STAT);

	status = readb(v3->base + V3_LB_ISTAT);
	if (status & V3_LB_ISTAT_MAILBOX)
		dev_info(dev, "PCI mailbox interrupt\n");
	if (status & V3_LB_ISTAT_PCI_RD)
		dev_err(dev, "PCI target LB->PCI READ abort interrupt\n");
	if (status & V3_LB_ISTAT_PCI_WR)
		dev_err(dev, "PCI target LB->PCI WRITE abort interrupt\n");
	if (status &  V3_LB_ISTAT_PCI_INT)
		dev_info(dev, "PCI pin interrupt\n");
	if (status & V3_LB_ISTAT_PCI_PERR)
		dev_err(dev, "PCI parity error interrupt\n");
	if (status & V3_LB_ISTAT_I2O_QWR)
		dev_info(dev, "I2O inbound post queue interrupt\n");
	if (status & V3_LB_ISTAT_DMA1)
		dev_info(dev, "DMA channel 1 interrupt\n");
	if (status & V3_LB_ISTAT_DMA0)
		dev_info(dev, "DMA channel 0 interrupt\n");
	/* Clear all possible interrupts on the local bus */
	writeb(0, v3->base + V3_LB_ISTAT);
	if (v3->map)
		regmap_write(v3->map, INTEGRATOR_SC_PCI_OFFSET,
			     INTEGRATOR_SC_PCI_ENABLE |
			     INTEGRATOR_SC_PCI_INTCLR);

	return IRQ_HANDLED;
}

static int v3_integrator_init(struct v3_pci *v3)
{
	unsigned int val;

	v3->map =
		syscon_regmap_lookup_by_compatible("arm,integrator-ap-syscon");
	if (IS_ERR(v3->map)) {
		dev_err(v3->dev, "no syscon\n");
		return -ENODEV;
	}

	regmap_read(v3->map, INTEGRATOR_SC_PCI_OFFSET, &val);
	/* Take the PCI bridge out of reset, clear IRQs */
	regmap_write(v3->map, INTEGRATOR_SC_PCI_OFFSET,
		     INTEGRATOR_SC_PCI_ENABLE |
		     INTEGRATOR_SC_PCI_INTCLR);

	if (!(val & INTEGRATOR_SC_PCI_ENABLE)) {
		/* If we were in reset we need to sleep a bit */
		msleep(230);

		/* Set the physical base for the controller itself */
		writel(0x6200, v3->base + V3_LB_IO_BASE);

		/* Wait for the mailbox to settle after reset */
		do {
			writeb(0xaa, v3->base + V3_MAIL_DATA);
			writeb(0x55, v3->base + V3_MAIL_DATA + 4);
		} while (readb(v3->base + V3_MAIL_DATA) != 0xaa &&
			 readb(v3->base + V3_MAIL_DATA) != 0x55);
	}

	dev_info(v3->dev, "initialized PCI V3 Integrator/AP integration\n");

	return 0;
}

static int v3_pci_setup_resource(struct v3_pci *v3,
				 struct pci_host_bridge *host,
				 struct resource_entry *win)
{
	struct device *dev = v3->dev;
	struct resource *mem;
	struct resource *io;

	switch (resource_type(win->res)) {
	case IORESOURCE_IO:
		io = win->res;

		/* Setup window 2 - PCI I/O */
		writel(v3_addr_to_lb_base2(pci_pio_to_address(io->start)) |
		       V3_LB_BASE2_ENABLE,
		       v3->base + V3_LB_BASE2);
		writew(v3_addr_to_lb_map2(io->start - win->offset),
		       v3->base + V3_LB_MAP2);
		break;
	case IORESOURCE_MEM:
		mem = win->res;
		if (mem->flags & IORESOURCE_PREFETCH) {
			mem->name = "V3 PCI PRE-MEM";
			v3->pre_mem = mem->start;
			v3->pre_bus_addr = mem->start - win->offset;
			dev_dbg(dev, "PREFETCHABLE MEM window %pR, bus addr %pap\n",
				mem, &v3->pre_bus_addr);
			if (resource_size(mem) != SZ_256M) {
				dev_err(dev, "prefetchable memory range is not 256MB\n");
				return -EINVAL;
			}
			if (v3->non_pre_mem &&
			    (mem->start != v3->non_pre_mem + SZ_256M)) {
				dev_err(dev,
					"prefetchable memory is not adjacent to non-prefetchable memory\n");
				return -EINVAL;
			}
			/* Setup window 1 - PCI prefetchable memory */
			writel(v3_addr_to_lb_base(v3->pre_mem) |
			       V3_LB_BASE_ADR_SIZE_256MB |
			       V3_LB_BASE_PREFETCH |
			       V3_LB_BASE_ENABLE,
			       v3->base + V3_LB_BASE1);
			writew(v3_addr_to_lb_map(v3->pre_bus_addr) |
			       V3_LB_MAP_TYPE_MEM, /* Was V3_LB_MAP_TYPE_MEM_MULTIPLE */
			       v3->base + V3_LB_MAP1);
		} else {
			mem->name = "V3 PCI NON-PRE-MEM";
			v3->non_pre_mem = mem->start;
			v3->non_pre_bus_addr = mem->start - win->offset;
			dev_dbg(dev, "NON-PREFETCHABLE MEM window %pR, bus addr %pap\n",
				mem, &v3->non_pre_bus_addr);
			if (resource_size(mem) != SZ_256M) {
				dev_err(dev,
					"non-prefetchable memory range is not 256MB\n");
				return -EINVAL;
			}
			/* Setup window 0 - PCI non-prefetchable memory */
			writel(v3_addr_to_lb_base(v3->non_pre_mem) |
			       V3_LB_BASE_ADR_SIZE_256MB |
			       V3_LB_BASE_ENABLE,
			       v3->base + V3_LB_BASE0);
			writew(v3_addr_to_lb_map(v3->non_pre_bus_addr) |
			       V3_LB_MAP_TYPE_MEM,
			       v3->base + V3_LB_MAP0);
		}
		break;
	case IORESOURCE_BUS:
		dev_dbg(dev, "BUS %pR\n", win->res);
		host->busnr = win->res->start;
		break;
	default:
		dev_info(dev, "Unknown resource type %lu\n",
			 resource_type(win->res));
		break;
	}

	return 0;
}

static int v3_get_dma_range_config(struct v3_pci *v3,
				   struct resource_entry *entry,
				   u32 *pci_base, u32 *pci_map)
{
	struct device *dev = v3->dev;
	u64 cpu_addr = entry->res->start;
	u64 cpu_end = entry->res->end;
	u64 pci_end = cpu_end - entry->offset;
	u64 pci_addr = entry->res->start - entry->offset;
	u32 val;

	if (pci_addr & ~V3_PCI_BASE_M_ADR_BASE) {
		dev_err(dev, "illegal range, only PCI bits 31..20 allowed\n");
		return -EINVAL;
	}
	val = ((u32)pci_addr) & V3_PCI_BASE_M_ADR_BASE;
	*pci_base = val;

	if (cpu_addr & ~V3_PCI_MAP_M_MAP_ADR) {
		dev_err(dev, "illegal range, only CPU bits 31..20 allowed\n");
		return -EINVAL;
	}
	val = ((u32)cpu_addr) & V3_PCI_MAP_M_MAP_ADR;

	switch (resource_size(entry->res)) {
	case SZ_1M:
		val |= V3_LB_BASE_ADR_SIZE_1MB;
		break;
	case SZ_2M:
		val |= V3_LB_BASE_ADR_SIZE_2MB;
		break;
	case SZ_4M:
		val |= V3_LB_BASE_ADR_SIZE_4MB;
		break;
	case SZ_8M:
		val |= V3_LB_BASE_ADR_SIZE_8MB;
		break;
	case SZ_16M:
		val |= V3_LB_BASE_ADR_SIZE_16MB;
		break;
	case SZ_32M:
		val |= V3_LB_BASE_ADR_SIZE_32MB;
		break;
	case SZ_64M:
		val |= V3_LB_BASE_ADR_SIZE_64MB;
		break;
	case SZ_128M:
		val |= V3_LB_BASE_ADR_SIZE_128MB;
		break;
	case SZ_256M:
		val |= V3_LB_BASE_ADR_SIZE_256MB;
		break;
	case SZ_512M:
		val |= V3_LB_BASE_ADR_SIZE_512MB;
		break;
	case SZ_1G:
		val |= V3_LB_BASE_ADR_SIZE_1GB;
		break;
	case SZ_2G:
		val |= V3_LB_BASE_ADR_SIZE_2GB;
		break;
	default:
		dev_err(v3->dev, "illegal dma memory chunk size\n");
		return -EINVAL;
		break;
	}
	val |= V3_PCI_MAP_M_REG_EN | V3_PCI_MAP_M_ENABLE;
	*pci_map = val;

	dev_dbg(dev,
		"DMA MEM CPU: 0x%016llx -> 0x%016llx => "
		"PCI: 0x%016llx -> 0x%016llx base %08x map %08x\n",
		cpu_addr, cpu_end,
		pci_addr, pci_end,
		*pci_base, *pci_map);

	return 0;
}

static int v3_pci_parse_map_dma_ranges(struct v3_pci *v3,
				       struct device_node *np)
{
	struct pci_host_bridge *bridge = pci_host_bridge_from_priv(v3);
	struct device *dev = v3->dev;
	struct resource_entry *entry;
	int i = 0;

	resource_list_for_each_entry(entry, &bridge->dma_ranges) {
		int ret;
		u32 pci_base, pci_map;

		ret = v3_get_dma_range_config(v3, entry, &pci_base, &pci_map);
		if (ret)
			return ret;

		if (i == 0) {
			writel(pci_base, v3->base + V3_PCI_BASE0);
			writel(pci_map, v3->base + V3_PCI_MAP0);
		} else if (i == 1) {
			writel(pci_base, v3->base + V3_PCI_BASE1);
			writel(pci_map, v3->base + V3_PCI_MAP1);
		} else {
			dev_err(dev, "too many ranges, only two supported\n");
			dev_err(dev, "range %d ignored\n", i);
		}
		i++;
	}
	return 0;
}

static int v3_pci_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct device_node *np = dev->of_node;
	struct resource *regs;
	struct resource_entry *win;
	struct v3_pci *v3;
	struct pci_host_bridge *host;
	struct clk *clk;
	u16 val;
	int irq;
	int ret;

	host = devm_pci_alloc_host_bridge(dev, sizeof(*v3));
	if (!host)
		return -ENOMEM;

	host->dev.parent = dev;
	host->ops = &v3_pci_ops;
	host->busnr = 0;
	host->msi = NULL;
	host->map_irq = of_irq_parse_and_map_pci;
	host->swizzle_irq = pci_common_swizzle;
	v3 = pci_host_bridge_priv(host);
	host->sysdata = v3;
	v3->dev = dev;

	/* Get and enable host clock */
	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk)) {
		dev_err(dev, "clock not found\n");
		return PTR_ERR(clk);
	}
	ret = clk_prepare_enable(clk);
	if (ret) {
		dev_err(dev, "unable to enable clock\n");
		return ret;
	}

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	v3->base = devm_ioremap_resource(dev, regs);
	if (IS_ERR(v3->base))
		return PTR_ERR(v3->base);
	/*
	 * The hardware has a register with the physical base address
	 * of the V3 controller itself, verify that this is the same
	 * as the physical memory we've remapped it from.
	 */
	if (readl(v3->base + V3_LB_IO_BASE) != (regs->start >> 16))
		dev_err(dev, "V3_LB_IO_BASE = %08x but device is @%pR\n",
			readl(v3->base + V3_LB_IO_BASE), regs);

	/* Configuration space is 16MB directly mapped */
	regs = platform_get_resource(pdev, IORESOURCE_MEM, 1);
	if (resource_size(regs) != SZ_16M) {
		dev_err(dev, "config mem is not 16MB!\n");
		return -EINVAL;
	}
	v3->config_mem = regs->start;
	v3->config_base = devm_ioremap_resource(dev, regs);
	if (IS_ERR(v3->config_base))
		return PTR_ERR(v3->config_base);

	ret = pci_parse_request_of_pci_ranges(dev, &host->windows,
					      &host->dma_ranges, NULL);
	if (ret)
		return ret;

	/* Get and request error IRQ resource */
	irq = platform_get_irq(pdev, 0);
	if (irq <= 0) {
		dev_err(dev, "unable to obtain PCIv3 error IRQ\n");
		return -ENODEV;
	}
	ret = devm_request_irq(dev, irq, v3_irq, 0,
			"PCIv3 error", v3);
	if (ret < 0) {
		dev_err(dev,
			"unable to request PCIv3 error IRQ %d (%d)\n",
			irq, ret);
		return ret;
	}

	/*
	 * Unlock V3 registers, but only if they were previously locked.
	 */
	if (readw(v3->base + V3_SYSTEM) & V3_SYSTEM_M_LOCK)
		writew(V3_SYSTEM_UNLOCK, v3->base + V3_SYSTEM);

	/* Disable all slave access while we set up the windows */
	val = readw(v3->base + V3_PCI_CMD);
	val &= ~(PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER);
	writew(val, v3->base + V3_PCI_CMD);

	/* Put the PCI bus into reset */
	val = readw(v3->base + V3_SYSTEM);
	val &= ~V3_SYSTEM_M_RST_OUT;
	writew(val, v3->base + V3_SYSTEM);

	/* Retry until we're ready */
	val = readw(v3->base + V3_PCI_CFG);
	val |= V3_PCI_CFG_M_RETRY_EN;
	writew(val, v3->base + V3_PCI_CFG);

	/* Set up the local bus protocol */
	val = readw(v3->base + V3_LB_CFG);
	val |= V3_LB_CFG_LB_BE_IMODE; /* Byte enable input */
	val |= V3_LB_CFG_LB_BE_OMODE; /* Byte enable output */
	val &= ~V3_LB_CFG_LB_ENDIAN; /* Little endian */
	val &= ~V3_LB_CFG_LB_PPC_RDY; /* TODO: when using on PPC403Gx, set to 1 */
	writew(val, v3->base + V3_LB_CFG);

	/* Enable the PCI bus master */
	val = readw(v3->base + V3_PCI_CMD);
	val |= PCI_COMMAND_MASTER;
	writew(val, v3->base + V3_PCI_CMD);

	/* Get the I/O and memory ranges from DT */
	resource_list_for_each_entry(win, &host->windows) {
		ret = v3_pci_setup_resource(v3, host, win);
		if (ret) {
			dev_err(dev, "error setting up resources\n");
			return ret;
		}
	}
	ret = v3_pci_parse_map_dma_ranges(v3, np);
	if (ret)
		return ret;

	/*
	 * Disable PCI to host IO cycles, enable I/O buffers @3.3V,
	 * set AD_LOW0 to 1 if one of the LB_MAP registers choose
	 * to use this (should be unused).
	 */
	writel(0x00000000, v3->base + V3_PCI_IO_BASE);
	val = V3_PCI_CFG_M_IO_REG_DIS | V3_PCI_CFG_M_IO_DIS |
		V3_PCI_CFG_M_EN3V | V3_PCI_CFG_M_AD_LOW0;
	/*
	 * DMA read and write from PCI bus commands types
	 */
	val |=  V3_PCI_CFG_TYPE_DEFAULT << V3_PCI_CFG_M_RTYPE_SHIFT;
	val |=  V3_PCI_CFG_TYPE_DEFAULT << V3_PCI_CFG_M_WTYPE_SHIFT;
	writew(val, v3->base + V3_PCI_CFG);

	/*
	 * Set the V3 FIFO such that writes have higher priority than
	 * reads, and local bus write causes local bus read fifo flush
	 * on aperture 1. Same for PCI.
	 */
	writew(V3_FIFO_PRIO_LB_RD1_FLUSH_AP1 |
	       V3_FIFO_PRIO_LB_RD0_FLUSH_AP1 |
	       V3_FIFO_PRIO_PCI_RD1_FLUSH_AP1 |
	       V3_FIFO_PRIO_PCI_RD0_FLUSH_AP1,
	       v3->base + V3_FIFO_PRIORITY);


	/*
	 * Clear any error interrupts, and enable parity and write error
	 * interrupts
	 */
	writeb(0, v3->base + V3_LB_ISTAT);
	val = readw(v3->base + V3_LB_CFG);
	val |= V3_LB_CFG_LB_LB_INT;
	writew(val, v3->base + V3_LB_CFG);
	writeb(V3_LB_ISTAT_PCI_WR | V3_LB_ISTAT_PCI_PERR,
	       v3->base + V3_LB_IMASK);

	/* Special Integrator initialization */
	if (of_device_is_compatible(np, "arm,integrator-ap-pci")) {
		ret = v3_integrator_init(v3);
		if (ret)
			return ret;
	}

	/* Post-init: enable PCI memory and invalidate (master already on) */
	val = readw(v3->base + V3_PCI_CMD);
	val |= PCI_COMMAND_MEMORY | PCI_COMMAND_INVALIDATE;
	writew(val, v3->base + V3_PCI_CMD);

	/* Clear pending interrupts */
	writeb(0, v3->base + V3_LB_ISTAT);
	/* Read or write errors and parity errors cause interrupts */
	writeb(V3_LB_ISTAT_PCI_RD | V3_LB_ISTAT_PCI_WR | V3_LB_ISTAT_PCI_PERR,
	       v3->base + V3_LB_IMASK);

	/* Take the PCI bus out of reset so devices can initialize */
	val = readw(v3->base + V3_SYSTEM);
	val |= V3_SYSTEM_M_RST_OUT;
	writew(val, v3->base + V3_SYSTEM);

	/*
	 * Re-lock the system register.
	 */
	val = readw(v3->base + V3_SYSTEM);
	val |= V3_SYSTEM_M_LOCK;
	writew(val, v3->base + V3_SYSTEM);

	ret = pci_scan_root_bus_bridge(host);
	if (ret) {
		dev_err(dev, "failed to register host: %d\n", ret);
		return ret;
	}
	v3->bus = host->bus;

	pci_bus_assign_resources(v3->bus);
	pci_bus_add_devices(v3->bus);

	return 0;
}

static const struct of_device_id v3_pci_of_match[] = {
	{
		.compatible = "v3,v360epc-pci",
	},
	{},
};

static struct platform_driver v3_pci_driver = {
	.driver = {
		.name = "pci-v3-semi",
		.of_match_table = of_match_ptr(v3_pci_of_match),
		.suppress_bind_attrs = true,
	},
	.probe  = v3_pci_probe,
};
builtin_platform_driver(v3_pci_driver);
