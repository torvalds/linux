/*
 * numa.c - Low-level PCI access for NUMA-Q machines
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/nodemask.h>
#include "pci.h"

#define BUS2QUAD(global) (mp_bus_id_to_node[global])
#define BUS2LOCAL(global) (mp_bus_id_to_local[global])
#define QUADLOCAL2BUS(quad,local) (quad_local_to_mp_bus_id[quad][local])

#define PCI_CONF1_MQ_ADDRESS(bus, devfn, reg) \
	(0x80000000 | (BUS2LOCAL(bus) << 16) | (devfn << 8) | (reg & ~3))

static int pci_conf1_mq_read(unsigned int seg, unsigned int bus,
			     unsigned int devfn, int reg, int len, u32 *value)
{
	unsigned long flags;

	if (!value || (bus >= MAX_MP_BUSSES) || (devfn > 255) || (reg > 255))
		return -EINVAL;

	spin_lock_irqsave(&pci_config_lock, flags);

	outl_quad(PCI_CONF1_MQ_ADDRESS(bus, devfn, reg), 0xCF8, BUS2QUAD(bus));

	switch (len) {
	case 1:
		*value = inb_quad(0xCFC + (reg & 3), BUS2QUAD(bus));
		break;
	case 2:
		*value = inw_quad(0xCFC + (reg & 2), BUS2QUAD(bus));
		break;
	case 4:
		*value = inl_quad(0xCFC, BUS2QUAD(bus));
		break;
	}

	spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

static int pci_conf1_mq_write(unsigned int seg, unsigned int bus,
			      unsigned int devfn, int reg, int len, u32 value)
{
	unsigned long flags;

	if ((bus >= MAX_MP_BUSSES) || (devfn > 255) || (reg > 255)) 
		return -EINVAL;

	spin_lock_irqsave(&pci_config_lock, flags);

	outl_quad(PCI_CONF1_MQ_ADDRESS(bus, devfn, reg), 0xCF8, BUS2QUAD(bus));

	switch (len) {
	case 1:
		outb_quad((u8)value, 0xCFC + (reg & 3), BUS2QUAD(bus));
		break;
	case 2:
		outw_quad((u16)value, 0xCFC + (reg & 2), BUS2QUAD(bus));
		break;
	case 4:
		outl_quad((u32)value, 0xCFC, BUS2QUAD(bus));
		break;
	}

	spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

#undef PCI_CONF1_MQ_ADDRESS

static struct pci_raw_ops pci_direct_conf1_mq = {
	.read	= pci_conf1_mq_read,
	.write	= pci_conf1_mq_write
};


static void __devinit pci_fixup_i450nx(struct pci_dev *d)
{
	/*
	 * i450NX -- Find and scan all secondary buses on all PXB's.
	 */
	int pxb, reg;
	u8 busno, suba, subb;
	int quad = BUS2QUAD(d->bus->number);

	printk("PCI: Searching for i450NX host bridges on %s\n", pci_name(d));
	reg = 0xd0;
	for(pxb=0; pxb<2; pxb++) {
		pci_read_config_byte(d, reg++, &busno);
		pci_read_config_byte(d, reg++, &suba);
		pci_read_config_byte(d, reg++, &subb);
		DBG("i450NX PXB %d: %02x/%02x/%02x\n", pxb, busno, suba, subb);
		if (busno) {
			/* Bus A */
			pci_scan_bus_with_sysdata(QUADLOCAL2BUS(quad, busno));
		}
		if (suba < subb) {
			/* Bus B */
			pci_scan_bus_with_sysdata(QUADLOCAL2BUS(quad, suba+1));
		}
	}
	pcibios_last_bus = -1;
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_DEVICE_ID_INTEL_82451NX, pci_fixup_i450nx);

static int __init pci_numa_init(void)
{
	int quad;

	raw_pci_ops = &pci_direct_conf1_mq;

	if (pcibios_scanned++)
		return 0;

	pci_root_bus = pcibios_scan_root(0);
	if (pci_root_bus)
		pci_bus_add_devices(pci_root_bus);
	if (num_online_nodes() > 1)
		for_each_online_node(quad) {
			if (quad == 0)
				continue;
			printk("Scanning PCI bus %d for quad %d\n", 
				QUADLOCAL2BUS(quad,0), quad);
			pci_scan_bus_with_sysdata(QUADLOCAL2BUS(quad, 0));
		}
	return 0;
}

subsys_initcall(pci_numa_init);
