/*
 * arch/arm/mach-kirkwood/pcie.c
 *
 * PCIe functions for Marvell Kirkwood SoCs
 *
 * This file is licensed under the terms of the GNU General Public
 * License version 2.  This program is licensed "as is" without any
 * warranty of any kind, whether express or implied.
 */

#include <linux/kernel.h>
#include <linux/pci.h>
#include <linux/slab.h>
#include <linux/clk.h>
#include <video/vga.h>
#include <asm/irq.h>
#include <asm/mach/pci.h>
#include <plat/pcie.h>
#include <mach/bridge-regs.h>
#include "common.h"

static void kirkwood_enable_pcie_clk(const char *port)
{
	struct clk *clk;

	clk = clk_get_sys("pcie", port);
	if (IS_ERR(clk)) {
		pr_err("PCIE clock %s missing\n", port);
		return;
	}
	clk_prepare_enable(clk);
	clk_put(clk);
}

/* This function is called very early in the boot when probing the
   hardware to determine what we actually are, and what rate tclk is
   ticking at. Hence calling kirkwood_enable_pcie_clk() is not
   possible since the clk tree has not been created yet. */
void kirkwood_enable_pcie(void)
{
	u32 curr = readl(CLOCK_GATING_CTRL);
	if (!(curr & CGC_PEX0))
		writel(curr | CGC_PEX0, CLOCK_GATING_CTRL);
}

void kirkwood_pcie_id(u32 *dev, u32 *rev)
{
	kirkwood_enable_pcie();
	*dev = orion_pcie_dev_id(PCIE_VIRT_BASE);
	*rev = orion_pcie_rev(PCIE_VIRT_BASE);
}

struct pcie_port {
	u8			root_bus_nr;
	void __iomem		*base;
	spinlock_t		conf_lock;
	int			irq;
	struct resource		res;
};

static int pcie_port_map[2];
static int num_pcie_ports;

static int pcie_valid_config(struct pcie_port *pp, int bus, int dev)
{
	/*
	 * Don't go out when trying to access --
	 * 1. nonexisting device on local bus
	 * 2. where there's no device connected (no link)
	 */
	if (bus == pp->root_bus_nr && dev == 0)
		return 1;

	if (!orion_pcie_link_up(pp->base))
		return 0;

	if (bus == pp->root_bus_nr && dev != 1)
		return 0;

	return 1;
}


/*
 * PCIe config cycles are done by programming the PCIE_CONF_ADDR register
 * and then reading the PCIE_CONF_DATA register. Need to make sure these
 * transactions are atomic.
 */

static int pcie_rd_conf(struct pci_bus *bus, u32 devfn, int where,
			int size, u32 *val)
{
	struct pci_sys_data *sys = bus->sysdata;
	struct pcie_port *pp = sys->private_data;
	unsigned long flags;
	int ret;

	if (pcie_valid_config(pp, bus->number, PCI_SLOT(devfn)) == 0) {
		*val = 0xffffffff;
		return PCIBIOS_DEVICE_NOT_FOUND;
	}

	spin_lock_irqsave(&pp->conf_lock, flags);
	ret = orion_pcie_rd_conf(pp->base, bus, devfn, where, size, val);
	spin_unlock_irqrestore(&pp->conf_lock, flags);

	return ret;
}

static int pcie_wr_conf(struct pci_bus *bus, u32 devfn,
			int where, int size, u32 val)
{
	struct pci_sys_data *sys = bus->sysdata;
	struct pcie_port *pp = sys->private_data;
	unsigned long flags;
	int ret;

	if (pcie_valid_config(pp, bus->number, PCI_SLOT(devfn)) == 0)
		return PCIBIOS_DEVICE_NOT_FOUND;

	spin_lock_irqsave(&pp->conf_lock, flags);
	ret = orion_pcie_wr_conf(pp->base, bus, devfn, where, size, val);
	spin_unlock_irqrestore(&pp->conf_lock, flags);

	return ret;
}

static struct pci_ops pcie_ops = {
	.read = pcie_rd_conf,
	.write = pcie_wr_conf,
};

static void __init pcie0_ioresources_init(struct pcie_port *pp)
{
	pp->base = PCIE_VIRT_BASE;
	pp->irq	= IRQ_KIRKWOOD_PCIE;

	/*
	 * IORESOURCE_MEM
	 */
	pp->res.name = "PCIe 0 MEM";
	pp->res.start = KIRKWOOD_PCIE_MEM_PHYS_BASE;
	pp->res.end = pp->res.start + KIRKWOOD_PCIE_MEM_SIZE - 1;
	pp->res.flags = IORESOURCE_MEM;
}

static void __init pcie1_ioresources_init(struct pcie_port *pp)
{
	pp->base = PCIE1_VIRT_BASE;
	pp->irq	= IRQ_KIRKWOOD_PCIE1;

	/*
	 * IORESOURCE_MEM
	 */
	pp->res.name = "PCIe 1 MEM";
	pp->res.start = KIRKWOOD_PCIE1_MEM_PHYS_BASE;
	pp->res.end = pp->res.start + KIRKWOOD_PCIE1_MEM_SIZE - 1;
	pp->res.flags = IORESOURCE_MEM;
}

static int __init kirkwood_pcie_setup(int nr, struct pci_sys_data *sys)
{
	struct pcie_port *pp;
	int index;

	if (nr >= num_pcie_ports)
		return 0;

	index = pcie_port_map[nr];
	pr_info("PCI: bus%d uses PCIe port %d\n", sys->busnr, index);

	pp = kzalloc(sizeof(*pp), GFP_KERNEL);
	if (!pp)
		panic("PCIe: failed to allocate pcie_port data");
	sys->private_data = pp;
	pp->root_bus_nr = sys->busnr;
	spin_lock_init(&pp->conf_lock);

	switch (index) {
	case 0:
		kirkwood_enable_pcie_clk("0");
		pcie0_ioresources_init(pp);
		pci_ioremap_io(SZ_64K * sys->busnr, KIRKWOOD_PCIE_IO_PHYS_BASE);
		break;
	case 1:
		kirkwood_enable_pcie_clk("1");
		pcie1_ioresources_init(pp);
		pci_ioremap_io(SZ_64K * sys->busnr,
			       KIRKWOOD_PCIE1_IO_PHYS_BASE);
		break;
	default:
		panic("PCIe setup: invalid controller %d", index);
	}

	if (request_resource(&iomem_resource, &pp->res))
		panic("Request PCIe%d Memory resource failed\n", index);

	pci_add_resource_offset(&sys->resources, &pp->res, sys->mem_offset);

	/*
	 * Generic PCIe unit setup.
	 */
	orion_pcie_set_local_bus_nr(pp->base, sys->busnr);

	orion_pcie_setup(pp->base);

	return 1;
}

/*
 * The root complex has a hardwired class of PCI_CLASS_MEMORY_OTHER, when it
 * is operating as a root complex this needs to be switched to
 * PCI_CLASS_BRIDGE_HOST or Linux will errantly try to process the BAR's on
 * the device. Decoding setup is handled by the orion code.
 */
static void rc_pci_fixup(struct pci_dev *dev)
{
	if (dev->bus->parent == NULL && dev->devfn == 0) {
		int i;

		dev->class &= 0xff;
		dev->class |= PCI_CLASS_BRIDGE_HOST << 8;
		for (i = 0; i < DEVICE_COUNT_RESOURCE; i++) {
			dev->resource[i].start = 0;
			dev->resource[i].end   = 0;
			dev->resource[i].flags = 0;
		}
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_MARVELL, PCI_ANY_ID, rc_pci_fixup);

static int __init kirkwood_pcie_map_irq(const struct pci_dev *dev, u8 slot,
	u8 pin)
{
	struct pci_sys_data *sys = dev->sysdata;
	struct pcie_port *pp = sys->private_data;

	return pp->irq;
}

static struct hw_pci kirkwood_pci __initdata = {
	.setup		= kirkwood_pcie_setup,
	.map_irq	= kirkwood_pcie_map_irq,
	.ops            = &pcie_ops,
};

static void __init add_pcie_port(int index, void __iomem *base)
{
	pcie_port_map[num_pcie_ports++] = index;
	pr_info("Kirkwood PCIe port %d: link %s\n", index,
		orion_pcie_link_up(base) ? "up" : "down");
}

void __init kirkwood_pcie_init(unsigned int portmask)
{
	vga_base = KIRKWOOD_PCIE_MEM_PHYS_BASE;

	if (portmask & KW_PCIE0)
		add_pcie_port(0, PCIE_VIRT_BASE);

	if (portmask & KW_PCIE1)
		add_pcie_port(1, PCIE1_VIRT_BASE);

	kirkwood_pci.nr_controllers = num_pcie_ports;
	pci_common_init(&kirkwood_pci);
}
