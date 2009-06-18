/*
 * Low-Level PCI Express Support for the SH7786
 *
 *  Copyright (C) 2009  Paul Mundt
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

static struct resource sh7786_pci_32bit_mem_resources[] = {
	{
		.name	= "pci0_mem",
		.start	= SH4A_PCIMEM_BASEA,
		.end	= SH4A_PCIMEM_BASEA + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "pci1_mem",
		.start	= SH4A_PCIMEM_BASEA1,
		.end	= SH4A_PCIMEM_BASEA1 + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	}, {
		.name	= "pci2_mem",
		.start	= SH4A_PCIMEM_BASEA2,
		.end	= SH4A_PCIMEM_BASEA2 + SZ_64M - 1,
		.flags	= IORESOURCE_MEM,
	},
};

static struct resource sh7786_pci_29bit_mem_resource = {
	.start	= SH4A_PCIMEM_BASE,
	.end	= SH4A_PCIMEM_BASE + SZ_64M - 1,
	.flags	= IORESOURCE_MEM,
};

static struct resource sh7786_pci_io_resources[] = {
	{
		.name	= "pci0_io",
		.start	= SH4A_PCIIO_BASE,
		.end	= SH4A_PCIIO_BASE + SZ_8M - 1,
		.flags	= IORESOURCE_IO,
	}, {
		.name	= "pci1_io",
		.start	= SH4A_PCIIO_BASE1,
		.end	= SH4A_PCIIO_BASE1 + SZ_8M - 1,
		.flags	= IORESOURCE_IO,
	}, {
		.name	= "pci2_io",
		.start	= SH4A_PCIIO_BASE2,
		.end	= SH4A_PCIIO_BASE2 + SZ_4M - 1,
		.flags	= IORESOURCE_IO,
	},
};

extern struct pci_ops sh7786_pci_ops;

#define DEFINE_CONTROLLER(start, idx)				\
{								\
	.pci_ops	= &sh7786_pci_ops,			\
	.reg_base	= start,				\
	/* mem_resource filled in at probe time */		\
	.mem_offset	= 0,					\
	.io_resource	= &sh7786_pci_io_resources[idx],	\
	.io_offset	= 0,					\
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
	int ret;

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

	/* Enable x4 link width and extended sync. */
	data = pci_read_reg(chan, SH4A_PCIEEXPCAP4);
	data &= ~(PCI_EXP_LNKSTA_NLW << 16);
	data |= (1 << 22) | PCI_EXP_LNKCTL_ES;
	pci_write_reg(chan, data, SH4A_PCIEEXPCAP4);

	/* Set the completion timer timeout to the maximum 32ms. */
	data = pci_read_reg(chan, SH4A_PCIETLCTLR);
	data &= ~0xffff;
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

	pci_write_reg(chan, 0x00100007, SH4A_PCIEPCICONF1);
	pci_write_reg(chan, 0x80888000, SH4A_PCIETXVC0DCTLR);
	pci_write_reg(chan, 0x00222000, SH4A_PCIERXVC0DCTLR);
	pci_write_reg(chan, 0x000050A0, SH4A_PCIEEXPCAP2);

	wmb();

	data = pci_read_reg(chan, SH4A_PCIEMACSR);
	printk(KERN_NOTICE "PCI: PCIe#%d link width %d\n",
	       port->index, (data >> 20) & 0x3f);

	pci_write_reg(chan, 0x007c0000, SH4A_PCIEPAMR0);
	pci_write_reg(chan, 0x00000000, SH4A_PCIEPARH0);
	pci_write_reg(chan, 0x00000000, SH4A_PCIEPARL0);
	pci_write_reg(chan, 0x80000100, SH4A_PCIEPTCTLR0);

	pci_write_reg(chan, 0x03fc0000, SH4A_PCIEPAMR2);
	pci_write_reg(chan, 0x00000000, SH4A_PCIEPARH2);
	pci_write_reg(chan, 0x00000000, SH4A_PCIEPARL2);
	pci_write_reg(chan, 0x80000000, SH4A_PCIEPTCTLR2);

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

	register_pci_controller(port->hose);

	return 0;
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
		port->hose->io_map_base	= port->hose->io_resource->start;

		/*
		 * Check if we are booting in 29 or 32-bit mode
		 *
		 * 32-bit mode provides each controller with its own
		 * memory window, while 29-bit mode uses a shared one.
		 */
		port->hose->mem_resource = test_mode_pin(MODE_PIN10) ?
			&sh7786_pci_32bit_mem_resources[i] :
			&sh7786_pci_29bit_mem_resource;

		ret |= sh7786_pcie_hwops->port_init_hw(port);
	}

	if (unlikely(ret))
		return ret;

	return 0;
}
arch_initcall(sh7786_pcie_init);
