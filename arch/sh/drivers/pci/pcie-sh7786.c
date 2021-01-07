// SPDX-License-Identifier: GPL-2.0
/*
 * Low-Level PCI Express Support for the SH7786
 *
 *  Copyright (C) 2009 - 2011  Paul Mundt
 */
#define pr_fmt(fmt) "PCI: " fmt

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/async.h>
#include <linux/delay.h>
#include <linux/dma-map-ops.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <linux/sh_clk.h>
#include <linux/sh_intc.h>
#include <cpu/sh7786.h>
#include "pcie-sh7786.h"
#include <linux/sizes.h>

struct sh7786_pcie_port {
	struct pci_channel	*hose;
	struct clk		*fclk, phy_clk;
	unsigned int		index;
	int			endpoint;
	int			link;
};

static struct sh7786_pcie_port *sh7786_pcie_ports;
static unsigned int nr_ports;
static unsigned long dma_pfn_offset;
size_t memsize;
u64 memstart;

static struct sh7786_pcie_hwops {
	int (*core_init)(void);
	async_func_t port_init_hw;
} *sh7786_pcie_hwops;

static struct resource sh7786_pci0_resources[] = {
	{
		.name	= "PCIe0 MEM 0",
		.start	= 0xfd000000,
		.end	= 0xfd000000 + SZ_8M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "PCIe0 MEM 1",
		.start	= 0xc0000000,
		.end	= 0xc0000000 + SZ_512M - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
	}, {
		.name	= "PCIe0 MEM 2",
		.start	= 0x10000000,
		.end	= 0x10000000 + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "PCIe0 IO",
		.start	= 0xfe100000,
		.end	= 0xfe100000 + SZ_1M - 1,
		.flags	= IORESOURCE_IO,
	},
};

static struct resource sh7786_pci1_resources[] = {
	{
		.name	= "PCIe1 MEM 0",
		.start	= 0xfd800000,
		.end	= 0xfd800000 + SZ_8M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "PCIe1 MEM 1",
		.start	= 0xa0000000,
		.end	= 0xa0000000 + SZ_512M - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
	}, {
		.name	= "PCIe1 MEM 2",
		.start	= 0x30000000,
		.end	= 0x30000000 + SZ_256M - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
	}, {
		.name	= "PCIe1 IO",
		.start	= 0xfe300000,
		.end	= 0xfe300000 + SZ_1M - 1,
		.flags	= IORESOURCE_IO,
	},
};

static struct resource sh7786_pci2_resources[] = {
	{
		.name	= "PCIe2 MEM 0",
		.start	= 0xfc800000,
		.end	= 0xfc800000 + SZ_4M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "PCIe2 MEM 1",
		.start	= 0x80000000,
		.end	= 0x80000000 + SZ_512M - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
	}, {
		.name	= "PCIe2 MEM 2",
		.start	= 0x20000000,
		.end	= 0x20000000 + SZ_256M - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
	}, {
		.name	= "PCIe2 IO",
		.start	= 0xfcd00000,
		.end	= 0xfcd00000 + SZ_1M - 1,
		.flags	= IORESOURCE_IO,
	},
};

extern struct pci_ops sh7786_pci_ops;

#define DEFINE_CONTROLLER(start, idx)					\
{									\
	.pci_ops	= &sh7786_pci_ops,				\
	.resources	= sh7786_pci##idx##_resources,			\
	.nr_resources	= ARRAY_SIZE(sh7786_pci##idx##_resources),	\
	.reg_base	= start,					\
	.mem_offset	= 0,						\
	.io_offset	= 0,						\
}

static struct pci_channel sh7786_pci_channels[] = {
	DEFINE_CONTROLLER(0xfe000000, 0),
	DEFINE_CONTROLLER(0xfe200000, 1),
	DEFINE_CONTROLLER(0xfcc00000, 2),
};

static struct clk fixed_pciexclkp = {
	.rate = 100000000,	/* 100 MHz reference clock */
};

static void sh7786_pci_fixup(struct pci_dev *dev)
{
	/*
	 * Prevent enumeration of root complex resources.
	 */
	if (pci_is_root_bus(dev->bus) && dev->devfn == 0) {
		int i;

		for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
			dev->resource[i].start	= 0;
			dev->resource[i].end	= 0;
			dev->resource[i].flags	= 0;
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_RENESAS, PCI_DEVICE_ID_RENESAS_SH7786,
			 sh7786_pci_fixup);

static int __init phy_wait_for_ack(struct pci_channel *chan)
{
	unsigned int timeout = 100;

	while (timeout--) {
		if (pci_read_reg(chan, SH4A_PCIEPHYADRR) & (1 << BITS_ACK))
			return 0;

		udelay(100);
	}

	return -ETIMEDOUT;
}

static int __init pci_wait_for_irq(struct pci_channel *chan, unsigned int mask)
{
	unsigned int timeout = 100;

	while (timeout--) {
		if ((pci_read_reg(chan, SH4A_PCIEINTR) & mask) == mask)
			return 0;

		udelay(100);
	}

	return -ETIMEDOUT;
}

static void __init phy_write_reg(struct pci_channel *chan, unsigned int addr,
				 unsigned int lane, unsigned int data)
{
	unsigned long phyaddr;

	phyaddr = (1 << BITS_CMD) + ((lane & 0xf) << BITS_LANE) +
			((addr & 0xff) << BITS_ADR);

	/* Set write data */
	pci_write_reg(chan, data, SH4A_PCIEPHYDOUTR);
	pci_write_reg(chan, phyaddr, SH4A_PCIEPHYADRR);

	phy_wait_for_ack(chan);

	/* Clear command */
	pci_write_reg(chan, 0, SH4A_PCIEPHYDOUTR);
	pci_write_reg(chan, 0, SH4A_PCIEPHYADRR);

	phy_wait_for_ack(chan);
}

static int __init pcie_clk_init(struct sh7786_pcie_port *port)
{
	struct pci_channel *chan = port->hose;
	struct clk *clk;
	char fclk_name[16];
	int ret;

	/*
	 * First register the fixed clock
	 */
	ret = clk_register(&fixed_pciexclkp);
	if (unlikely(ret != 0))
		return ret;

	/*
	 * Grab the port's function clock, which the PHY clock depends
	 * on. clock lookups don't help us much at this point, since no
	 * dev_id is available this early. Lame.
	 */
	snprintf(fclk_name, sizeof(fclk_name), "pcie%d_fck", port->index);

	port->fclk = clk_get(NULL, fclk_name);
	if (IS_ERR(port->fclk)) {
		ret = PTR_ERR(port->fclk);
		goto err_fclk;
	}

	clk_enable(port->fclk);

	/*
	 * And now, set up the PHY clock
	 */
	clk = &port->phy_clk;

	memset(clk, 0, sizeof(struct clk));

	clk->parent = &fixed_pciexclkp;
	clk->enable_reg = (void __iomem *)(chan->reg_base + SH4A_PCIEPHYCTLR);
	clk->enable_bit = BITS_CKE;

	ret = sh_clk_mstp_register(clk, 1);
	if (unlikely(ret < 0))
		goto err_phy;

	return 0;

err_phy:
	clk_disable(port->fclk);
	clk_put(port->fclk);
err_fclk:
	clk_unregister(&fixed_pciexclkp);

	return ret;
}

static int __init phy_init(struct sh7786_pcie_port *port)
{
	struct pci_channel *chan = port->hose;
	unsigned int timeout = 100;

	clk_enable(&port->phy_clk);

	/* Initialize the phy */
	phy_write_reg(chan, 0x60, 0xf, 0x004b008b);
	phy_write_reg(chan, 0x61, 0xf, 0x00007b41);
	phy_write_reg(chan, 0x64, 0xf, 0x00ff4f00);
	phy_write_reg(chan, 0x65, 0xf, 0x09070907);
	phy_write_reg(chan, 0x66, 0xf, 0x00000010);
	phy_write_reg(chan, 0x74, 0xf, 0x0007001c);
	phy_write_reg(chan, 0x79, 0xf, 0x01fc000d);
	phy_write_reg(chan, 0xb0, 0xf, 0x00000610);

	/* Deassert Standby */
	phy_write_reg(chan, 0x67, 0x1, 0x00000400);

	/* Disable clock */
	clk_disable(&port->phy_clk);

	while (timeout--) {
		if (pci_read_reg(chan, SH4A_PCIEPHYSR))
			return 0;

		udelay(100);
	}

	return -ETIMEDOUT;
}

static void __init pcie_reset(struct sh7786_pcie_port *port)
{
	struct pci_channel *chan = port->hose;

	pci_write_reg(chan, 1, SH4A_PCIESRSTR);
	pci_write_reg(chan, 0, SH4A_PCIETCTLR);
	pci_write_reg(chan, 0, SH4A_PCIESRSTR);
	pci_write_reg(chan, 0, SH4A_PCIETXVC0SR);
}

static int __init pcie_init(struct sh7786_pcie_port *port)
{
	struct pci_channel *chan = port->hose;
	unsigned int data;
	phys_addr_t memstart, memend;
	int ret, i, win;

	/* Begin initialization */
	pcie_reset(port);

	/*
	 * Initial header for port config space is type 1, set the device
	 * class to match. Hardware takes care of propagating the IDSETR
	 * settings, so there is no need to bother with a quirk.
	 */
	pci_write_reg(chan, PCI_CLASS_BRIDGE_PCI << 16, SH4A_PCIEIDSETR1);

	/* Initialize default capabilities. */
	data = pci_read_reg(chan, SH4A_PCIEEXPCAP0);
	data &= ~(PCI_EXP_FLAGS_TYPE << 16);

	if (port->endpoint)
		data |= PCI_EXP_TYPE_ENDPOINT << 20;
	else
		data |= PCI_EXP_TYPE_ROOT_PORT << 20;

	data |= PCI_CAP_ID_EXP;
	pci_write_reg(chan, data, SH4A_PCIEEXPCAP0);

	/* Enable data link layer active state reporting */
	pci_write_reg(chan, PCI_EXP_LNKCAP_DLLLARC, SH4A_PCIEEXPCAP3);

	/* Enable extended sync and ASPM L0s support */
	data = pci_read_reg(chan, SH4A_PCIEEXPCAP4);
	data &= ~PCI_EXP_LNKCTL_ASPMC;
	data |= PCI_EXP_LNKCTL_ES | 1;
	pci_write_reg(chan, data, SH4A_PCIEEXPCAP4);

	/* Write out the physical slot number */
	data = pci_read_reg(chan, SH4A_PCIEEXPCAP5);
	data &= ~PCI_EXP_SLTCAP_PSN;
	data |= (port->index + 1) << 19;
	pci_write_reg(chan, data, SH4A_PCIEEXPCAP5);

	/* Set the completion timer timeout to the maximum 32ms. */
	data = pci_read_reg(chan, SH4A_PCIETLCTLR);
	data &= ~0x3f00;
	data |= 0x32 << 8;
	pci_write_reg(chan, data, SH4A_PCIETLCTLR);

	/*
	 * Set fast training sequences to the maximum 255,
	 * and enable MAC data scrambling.
	 */
	data = pci_read_reg(chan, SH4A_PCIEMACCTLR);
	data &= ~PCIEMACCTLR_SCR_DIS;
	data |= (0xff << 16);
	pci_write_reg(chan, data, SH4A_PCIEMACCTLR);

	memstart = __pa(memory_start);
	memend   = __pa(memory_end);
	memsize = roundup_pow_of_two(memend - memstart);

	/*
	 * The start address must be aligned on its size. So we round
	 * it down, and then recalculate the size so that it covers
	 * the entire memory.
	 */
	memstart = ALIGN_DOWN(memstart, memsize);
	memsize = roundup_pow_of_two(memend - memstart);

	/*
	 * If there's more than 512MB of memory, we need to roll over to
	 * LAR1/LAMR1.
	 */
	if (memsize > SZ_512M) {
		pci_write_reg(chan, memstart + SZ_512M, SH4A_PCIELAR1);
		pci_write_reg(chan, ((memsize - SZ_512M) - SZ_256) | 1,
			      SH4A_PCIELAMR1);
		memsize = SZ_512M;
	} else {
		/*
		 * Otherwise just zero it out and disable it.
		 */
		pci_write_reg(chan, 0, SH4A_PCIELAR1);
		pci_write_reg(chan, 0, SH4A_PCIELAMR1);
	}

	/*
	 * LAR0/LAMR0 covers up to the first 512MB, which is enough to
	 * cover all of lowmem on most platforms.
	 */
	pci_write_reg(chan, memstart, SH4A_PCIELAR0);
	pci_write_reg(chan, (memsize - SZ_256) | 1, SH4A_PCIELAMR0);

	/* Finish initialization */
	data = pci_read_reg(chan, SH4A_PCIETCTLR);
	data |= 0x1;
	pci_write_reg(chan, data, SH4A_PCIETCTLR);

	/* Let things settle down a bit.. */
	mdelay(100);

	/* Enable DL_Active Interrupt generation */
	data = pci_read_reg(chan, SH4A_PCIEDLINTENR);
	data |= PCIEDLINTENR_DLL_ACT_ENABLE;
	pci_write_reg(chan, data, SH4A_PCIEDLINTENR);

	/* Disable MAC data scrambling. */
	data = pci_read_reg(chan, SH4A_PCIEMACCTLR);
	data |= PCIEMACCTLR_SCR_DIS | (0xff << 16);
	pci_write_reg(chan, data, SH4A_PCIEMACCTLR);

	/*
	 * This will timeout if we don't have a link, but we permit the
	 * port to register anyways in order to support hotplug on future
	 * hardware.
	 */
	ret = pci_wait_for_irq(chan, MASK_INT_TX_CTRL);

	data = pci_read_reg(chan, SH4A_PCIEPCICONF1);
	data &= ~(PCI_STATUS_DEVSEL_MASK << 16);
	data |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
		(PCI_STATUS_CAP_LIST | PCI_STATUS_DEVSEL_FAST) << 16;
	pci_write_reg(chan, data, SH4A_PCIEPCICONF1);

	pci_write_reg(chan, 0x80888000, SH4A_PCIETXVC0DCTLR);
	pci_write_reg(chan, 0x00222000, SH4A_PCIERXVC0DCTLR);

	wmb();

	if (ret == 0) {
		data = pci_read_reg(chan, SH4A_PCIEMACSR);
		printk(KERN_NOTICE "PCI: PCIe#%d x%d link detected\n",
		       port->index, (data >> 20) & 0x3f);
	} else
		printk(KERN_NOTICE "PCI: PCIe#%d link down\n",
		       port->index);

	for (i = win = 0; i < chan->nr_resources; i++) {
		struct resource *res = chan->resources + i;
		resource_size_t size;
		u32 mask;

		/*
		 * We can't use the 32-bit mode windows in legacy 29-bit
		 * mode, so just skip them entirely.
		 */
		if ((res->flags & IORESOURCE_MEM_32BIT) && __in_29bit_mode())
			res->flags |= IORESOURCE_DISABLED;

		if (res->flags & IORESOURCE_DISABLED)
			continue;

		pci_write_reg(chan, 0x00000000, SH4A_PCIEPTCTLR(win));

		/*
		 * The PAMR mask is calculated in units of 256kB, which
		 * keeps things pretty simple.
		 */
		size = resource_size(res);
		mask = (roundup_pow_of_two(size) / SZ_256K) - 1;
		pci_write_reg(chan, mask << 18, SH4A_PCIEPAMR(win));

		pci_write_reg(chan, upper_32_bits(res->start),
			      SH4A_PCIEPARH(win));
		pci_write_reg(chan, lower_32_bits(res->start),
			      SH4A_PCIEPARL(win));

		mask = MASK_PARE;
		if (res->flags & IORESOURCE_IO)
			mask |= MASK_SPC;

		pci_write_reg(chan, mask, SH4A_PCIEPTCTLR(win));

		win++;
	}

	return 0;
}

int pcibios_map_platform_irq(const struct pci_dev *pdev, u8 slot, u8 pin)
{
        return evt2irq(0xae0);
}

void pcibios_bus_add_device(struct pci_dev *pdev)
{
	dma_direct_set_offset(&pdev->dev, __pa(memory_start),
			      __pa(memory_start) - memstart, memsize);
}

static int __init sh7786_pcie_core_init(void)
{
	/* Return the number of ports */
	return test_mode_pin(MODE_PIN12) ? 3 : 2;
}

static void __init sh7786_pcie_init_hw(void *data, async_cookie_t cookie)
{
	struct sh7786_pcie_port *port = data;
	int ret;

	/*
	 * Check if we are configured in endpoint or root complex mode,
	 * this is a fixed pin setting that applies to all PCIe ports.
	 */
	port->endpoint = test_mode_pin(MODE_PIN11);

	/*
	 * Setup clocks, needed both for PHY and PCIe registers.
	 */
	ret = pcie_clk_init(port);
	if (unlikely(ret < 0)) {
		pr_err("clock initialization failed for port#%d\n",
		       port->index);
		return;
	}

	ret = phy_init(port);
	if (unlikely(ret < 0)) {
		pr_err("phy initialization failed for port#%d\n",
		       port->index);
		return;
	}

	ret = pcie_init(port);
	if (unlikely(ret < 0)) {
		pr_err("core initialization failed for port#%d\n",
			       port->index);
		return;
	}

	/* In the interest of preserving device ordering, synchronize */
	async_synchronize_cookie(cookie);

	register_pci_controller(port->hose);
}

static struct sh7786_pcie_hwops sh7786_65nm_pcie_hwops __initdata = {
	.core_init	= sh7786_pcie_core_init,
	.port_init_hw	= sh7786_pcie_init_hw,
};

static int __init sh7786_pcie_init(void)
{
	struct clk *platclk;
	u32 mm_sel;
	int i;

	printk(KERN_NOTICE "PCI: Starting initialization.\n");

	sh7786_pcie_hwops = &sh7786_65nm_pcie_hwops;

	nr_ports = sh7786_pcie_hwops->core_init();
	BUG_ON(nr_ports > ARRAY_SIZE(sh7786_pci_channels));

	if (unlikely(nr_ports == 0))
		return -ENODEV;

	sh7786_pcie_ports = kcalloc(nr_ports, sizeof(struct sh7786_pcie_port),
				    GFP_KERNEL);
	if (unlikely(!sh7786_pcie_ports))
		return -ENOMEM;

	/*
	 * Fetch any optional platform clock associated with this block.
	 *
	 * This is a rather nasty hack for boards with spec-mocking FPGAs
	 * that have a secondary set of clocks outside of the on-chip
	 * ones that need to be accounted for before there is any chance
	 * of touching the existing MSTP bits or CPG clocks.
	 */
	platclk = clk_get(NULL, "pcie_plat_clk");
	if (IS_ERR(platclk)) {
		/* Sane hardware should probably get a WARN_ON.. */
		platclk = NULL;
	}

	clk_enable(platclk);

	mm_sel = sh7786_mm_sel();

	/*
	 * Depending on the MMSELR register value, the PCIe0 MEM 1
	 * area may not be available. See Table 13.11 of the SH7786
	 * datasheet.
	 */
	if (mm_sel != 1 && mm_sel != 2 && mm_sel != 5 && mm_sel != 6)
		sh7786_pci0_resources[2].flags |= IORESOURCE_DISABLED;

	printk(KERN_NOTICE "PCI: probing %d ports.\n", nr_ports);

	for (i = 0; i < nr_ports; i++) {
		struct sh7786_pcie_port *port = sh7786_pcie_ports + i;

		port->index		= i;
		port->hose		= sh7786_pci_channels + i;
		port->hose->io_map_base	= port->hose->resources[0].start;

		async_schedule(sh7786_pcie_hwops->port_init_hw, port);
	}

	async_synchronize_full();

	return 0;
}
arch_initcall(sh7786_pcie_init);
