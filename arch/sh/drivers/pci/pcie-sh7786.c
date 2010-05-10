/*
 * Low-Level PCI Express Support for the SH7786
 *
 *  Copyright (C) 2009 - 2010  Paul Mundt
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 */
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include "pcie-sh7786.h"
#include <asm/sizes.h>

struct sh7786_pcie_port {
	struct pci_channel	*hose;
	unsigned int		index;
	int			endpoint;
	int			link;
};

static struct sh7786_pcie_port *sh7786_pcie_ports;
static unsigned int nr_ports;

static struct sh7786_pcie_hwops {
	int (*core_init)(void);
	int (*port_init_hw)(struct sh7786_pcie_port *port);
} *sh7786_pcie_hwops;

static struct resource sh7786_pci0_resources[] = {
	{
		.name	= "PCIe0 IO",
		.start	= 0xfd000000,
		.end	= 0xfd000000 + SZ_8M - 1,
		.flags	= IORESOURCE_IO,
	}, {
		.name	= "PCIe0 MEM 0",
		.start	= 0xc0000000,
		.end	= 0xc0000000 + SZ_512M - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
	}, {
		.name	= "PCIe0 MEM 1",
		.start	= 0x10000000,
		.end	= 0x10000000 + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "PCIe0 MEM 2",
		.start	= 0xfe100000,
		.end	= 0xfe100000 + SZ_1M - 1,
	},
};

static struct resource sh7786_pci1_resources[] = {
	{
		.name	= "PCIe1 IO",
		.start	= 0xfd800000,
		.end	= 0xfd800000 + SZ_8M - 1,
		.flags	= IORESOURCE_IO,
	}, {
		.name	= "PCIe1 MEM 0",
		.start	= 0xa0000000,
		.end	= 0xa0000000 + SZ_512M - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
	}, {
		.name	= "PCIe1 MEM 1",
		.start	= 0x30000000,
		.end	= 0x30000000 + SZ_256M - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
	}, {
		.name	= "PCIe1 MEM 2",
		.start	= 0xfe300000,
		.end	= 0xfe300000 + SZ_1M - 1,
	},
};

static struct resource sh7786_pci2_resources[] = {
	{
		.name	= "PCIe2 IO",
		.start	= 0xfc800000,
		.end	= 0xfc800000 + SZ_4M - 1,
	}, {
		.name	= "PCIe2 MEM 0",
		.start	= 0x80000000,
		.end	= 0x80000000 + SZ_512M - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
	}, {
		.name	= "PCIe2 MEM 1",
		.start	= 0x20000000,
		.end	= 0x20000000 + SZ_256M - 1,
		.flags	= IORESOURCE_MEM | IORESOURCE_MEM_32BIT,
	}, {
		.name	= "PCIe2 MEM 2",
		.start	= 0xfcd00000,
		.end	= 0xfcd00000 + SZ_1M - 1,
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

static int phy_wait_for_ack(struct pci_channel *chan)
{
	unsigned int timeout = 100;

	while (timeout--) {
		if (pci_read_reg(chan, SH4A_PCIEPHYADRR) & (1 << BITS_ACK))
			return 0;

		udelay(100);
	}

	return -ETIMEDOUT;
}

static int pci_wait_for_irq(struct pci_channel *chan, unsigned int mask)
{
	unsigned int timeout = 100;

	while (timeout--) {
		if ((pci_read_reg(chan, SH4A_PCIEINTR) & mask) == mask)
			return 0;

		udelay(100);
	}

	return -ETIMEDOUT;
}

static void phy_write_reg(struct pci_channel *chan, unsigned int addr,
			  unsigned int lane, unsigned int data)
{
	unsigned long phyaddr, ctrl;

	phyaddr = (1 << BITS_CMD) + ((lane & 0xf) << BITS_LANE) +
			((addr & 0xff) << BITS_ADR);

	/* Enable clock */
	ctrl = pci_read_reg(chan, SH4A_PCIEPHYCTLR);
	ctrl |= (1 << BITS_CKE);
	pci_write_reg(chan, ctrl, SH4A_PCIEPHYCTLR);

	/* Set write data */
	pci_write_reg(chan, data, SH4A_PCIEPHYDOUTR);
	pci_write_reg(chan, phyaddr, SH4A_PCIEPHYADRR);

	phy_wait_for_ack(chan);

	/* Clear command */
	pci_write_reg(chan, 0, SH4A_PCIEPHYADRR);

	phy_wait_for_ack(chan);

	/* Disable clock */
	ctrl = pci_read_reg(chan, SH4A_PCIEPHYCTLR);
	ctrl &= ~(1 << BITS_CKE);
	pci_write_reg(chan, ctrl, SH4A_PCIEPHYCTLR);
}

static int phy_init(struct pci_channel *chan)
{
	unsigned int timeout = 100;

	/* Initialize the phy */
	phy_write_reg(chan, 0x60, 0xf, 0x004b008b);
	phy_write_reg(chan, 0x61, 0xf, 0x00007b41);
	phy_write_reg(chan, 0x64, 0xf, 0x00ff4f00);
	phy_write_reg(chan, 0x65, 0xf, 0x09070907);
	phy_write_reg(chan, 0x66, 0xf, 0x00000010);
	phy_write_reg(chan, 0x74, 0xf, 0x0007001c);
	phy_write_reg(chan, 0x79, 0xf, 0x01fc000d);

	/* Deassert Standby */
	phy_write_reg(chan, 0x67, 0xf, 0x00000400);

	while (timeout--) {
		if (pci_read_reg(chan, SH4A_PCIEPHYSR))
			return 0;

		udelay(100);
	}

	return -ETIMEDOUT;
}

static int pcie_init(struct sh7786_pcie_port *port)
{
	struct pci_channel *chan = port->hose;
	unsigned int data;
	phys_addr_t memphys;
	size_t memsize;
	int ret, i;

	/* Begin initialization */
	pci_write_reg(chan, 0, SH4A_PCIETCTLR);

	/* Initialize as type1. */
	data = pci_read_reg(chan, SH4A_PCIEPCICONF3);
	data &= ~(0x7f << 16);
	data |= PCI_HEADER_TYPE_BRIDGE << 16;
	pci_write_reg(chan, data, SH4A_PCIEPCICONF3);

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

	memphys = __pa(memory_start);
	memsize = roundup_pow_of_two(memory_end - memory_start);

	/*
	 * If there's more than 512MB of memory, we need to roll over to
	 * LAR1/LAMR1.
	 */
	if (memsize > SZ_512M) {
		__raw_writel(memphys + SZ_512M, chan->reg_base + SH4A_PCIELAR1);
		__raw_writel(((memsize - SZ_512M) - SZ_256) | 1,
			     chan->reg_base + SH4A_PCIELAMR1);
		memsize = SZ_512M;
	} else {
		/*
		 * Otherwise just zero it out and disable it.
		 */
		__raw_writel(0, chan->reg_base + SH4A_PCIELAR1);
		__raw_writel(0, chan->reg_base + SH4A_PCIELAMR1);
	}

	/*
	 * LAR0/LAMR0 covers up to the first 512MB, which is enough to
	 * cover all of lowmem on most platforms.
	 */
	__raw_writel(memphys, chan->reg_base + SH4A_PCIELAR0);
	__raw_writel((memsize - SZ_256) | 1, chan->reg_base + SH4A_PCIELAMR0);

	/* Finish initialization */
	data = pci_read_reg(chan, SH4A_PCIETCTLR);
	data |= 0x1;
	pci_write_reg(chan, data, SH4A_PCIETCTLR);

	/* Enable DL_Active Interrupt generation */
	data = pci_read_reg(chan, SH4A_PCIEDLINTENR);
	data |= PCIEDLINTENR_DLL_ACT_ENABLE;
	pci_write_reg(chan, data, SH4A_PCIEDLINTENR);

	/* Disable MAC data scrambling. */
	data = pci_read_reg(chan, SH4A_PCIEMACCTLR);
	data |= PCIEMACCTLR_SCR_DIS | (0xff << 16);
	pci_write_reg(chan, data, SH4A_PCIEMACCTLR);

	ret = pci_wait_for_irq(chan, MASK_INT_TX_CTRL);
	if (unlikely(ret != 0))
		return -ENODEV;

	data = pci_read_reg(chan, SH4A_PCIEPCICONF1);
	data &= ~(PCI_STATUS_DEVSEL_MASK << 16);
	data |= PCI_COMMAND_IO | PCI_COMMAND_MEMORY | PCI_COMMAND_MASTER |
		(PCI_STATUS_CAP_LIST | PCI_STATUS_DEVSEL_FAST) << 16;
	pci_write_reg(chan, data, SH4A_PCIEPCICONF1);

	pci_write_reg(chan, 0x80888000, SH4A_PCIETXVC0DCTLR);
	pci_write_reg(chan, 0x00222000, SH4A_PCIERXVC0DCTLR);

	wmb();

	data = pci_read_reg(chan, SH4A_PCIEMACSR);
	printk(KERN_NOTICE "PCI: PCIe#%d link width %d\n",
	       port->index, (data >> 20) & 0x3f);


	for (i = 0; i < chan->nr_resources; i++) {
		struct resource *res = chan->resources + i;
		resource_size_t size;
		u32 enable_mask;

		pci_write_reg(chan, 0x00000000, SH4A_PCIEPTCTLR(i));

		size = resource_size(res);

		/*
		 * The PAMR mask is calculated in units of 256kB, which
		 * keeps things pretty simple.
		 */
		__raw_writel(((roundup_pow_of_two(size) / SZ_256K) - 1) << 18,
			     chan->reg_base + SH4A_PCIEPAMR(i));

		pci_write_reg(chan, 0x00000000, SH4A_PCIEPARH(i));
		pci_write_reg(chan, 0x00000000, SH4A_PCIEPARL(i));

		enable_mask = MASK_PARE;
		if (res->flags & IORESOURCE_IO)
			enable_mask |= MASK_SPC;

		pci_write_reg(chan, enable_mask, SH4A_PCIEPTCTLR(i));
	}

	return 0;
}

int __init pcibios_map_platform_irq(struct pci_dev *pdev, u8 slot, u8 pin)
{
        return 71;
}

static int sh7786_pcie_core_init(void)
{
	/* Return the number of ports */
	return test_mode_pin(MODE_PIN12) ? 3 : 2;
}

static int __devinit sh7786_pcie_init_hw(struct sh7786_pcie_port *port)
{
	int ret;

	ret = phy_init(port->hose);
	if (unlikely(ret < 0))
		return ret;

	/*
	 * Check if we are configured in endpoint or root complex mode,
	 * this is a fixed pin setting that applies to all PCIe ports.
	 */
	port->endpoint = test_mode_pin(MODE_PIN11);

	ret = pcie_init(port);
	if (unlikely(ret < 0))
		return ret;

	return register_pci_controller(port->hose);
}

static struct sh7786_pcie_hwops sh7786_65nm_pcie_hwops __initdata = {
	.core_init	= sh7786_pcie_core_init,
	.port_init_hw	= sh7786_pcie_init_hw,
};

static int __init sh7786_pcie_init(void)
{
	int ret = 0, i;

	printk(KERN_NOTICE "PCI: Starting intialization.\n");

	sh7786_pcie_hwops = &sh7786_65nm_pcie_hwops;

	nr_ports = sh7786_pcie_hwops->core_init();
	BUG_ON(nr_ports > ARRAY_SIZE(sh7786_pci_channels));

	if (unlikely(nr_ports == 0))
		return -ENODEV;

	sh7786_pcie_ports = kzalloc(nr_ports * sizeof(struct sh7786_pcie_port),
				    GFP_KERNEL);
	if (unlikely(!sh7786_pcie_ports))
		return -ENOMEM;

	printk(KERN_NOTICE "PCI: probing %d ports.\n", nr_ports);

	for (i = 0; i < nr_ports; i++) {
		struct sh7786_pcie_port *port = sh7786_pcie_ports + i;

		port->index		= i;
		port->hose		= sh7786_pci_channels + i;
		port->hose->io_map_base	= port->hose->resources[0].start;

		ret |= sh7786_pcie_hwops->port_init_hw(port);
	}

	if (unlikely(ret))
		return ret;

	return 0;
}
arch_initcall(sh7786_pcie_init);
