/*
 * numaq_32.c - Low-level PCI access for NUMA-Q machines
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/nodemask.h>
#include <asm/apic.h>
#include <asm/mpspec.h>
#include <asm/pci_x86.h>
#include <asm/numaq.h>

#define BUS2QUAD(global) (mp_bus_id_to_node[global])

#define BUS2LOCAL(global) (mp_bus_id_to_local[global])

#define QUADLOCAL2BUS(quad,local) (quad_local_to_mp_bus_id[quad][local])

#define PCI_CONF1_MQ_ADDRESS(bus, devfn, reg) \
	(0x80000000 | (BUS2LOCAL(bus) << 16) | (devfn << 8) | (reg & ~3))

static void write_cf8(unsigned bus, unsigned devfn, unsigned reg)
{
	unsigned val = PCI_CONF1_MQ_ADDRESS(bus, devfn, reg);
	if (xquad_portio)
		writel(val, XQUAD_PORT_ADDR(0xcf8, BUS2QUAD(bus)));
	else
		outl(val, 0xCF8);
}

static int pci_conf1_mq_read(unsigned int seg, unsigned int bus,
			     unsigned int devfn, int reg, int len, u32 *value)
{
	unsigned long flags;
	void *adr __iomem = XQUAD_PORT_ADDR(0xcfc, BUS2QUAD(bus));

	WARN_ON(seg);
	if (!value || (bus >= MAX_MP_BUSSES) || (devfn > 255) || (reg > 255))
		return -EINVAL;

	raw_spin_lock_irqsave(&pci_config_lock, flags);

	write_cf8(bus, devfn, reg);

	switch (len) {
	case 1:
		if (xquad_portio)
			*value = readb(adr + (reg & 3));
		else
			*value = inb(0xCFC + (reg & 3));
		break;
	case 2:
		if (xquad_portio)
			*value = readw(adr + (reg & 2));
		else
			*value = inw(0xCFC + (reg & 2));
		break;
	case 4:
		if (xquad_portio)
			*value = readl(adr);
		else
			*value = inl(0xCFC);
		break;
	}

	raw_spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

static int pci_conf1_mq_write(unsigned int seg, unsigned int bus,
			      unsigned int devfn, int reg, int len, u32 value)
{
	unsigned long flags;
	void *adr __iomem = XQUAD_PORT_ADDR(0xcfc, BUS2QUAD(bus));

	WARN_ON(seg);
	if ((bus >= MAX_MP_BUSSES) || (devfn > 255) || (reg > 255)) 
		return -EINVAL;

	raw_spin_lock_irqsave(&pci_config_lock, flags);

	write_cf8(bus, devfn, reg);

	switch (len) {
	case 1:
		if (xquad_portio)
			writeb(value, adr + (reg & 3));
		else
			outb((u8)value, 0xCFC + (reg & 3));
		break;
	case 2:
		if (xquad_portio)
			writew(value, adr + (reg & 2));
		else
			outw((u16)value, 0xCFC + (reg & 2));
		break;
	case 4:
		if (xquad_portio)
			writel(value, adr + reg);
		else
			outl((u32)value, 0xCFC);
		break;
	}

	raw_spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

#undef PCI_CONF1_MQ_ADDRESS

static const struct pci_raw_ops pci_direct_conf1_mq = {
	.read	= pci_conf1_mq_read,
	.write	= pci_conf1_mq_write
};


static void pci_fixup_i450nx(struct pci_dev *d)
{
	/*
	 * i450NX -- Find and scan all secondary buses on all PXB's.
	 */
	int pxb, reg;
	u8 busno, suba, subb;
	int quad = BUS2QUAD(d->bus->number);

	dev_info(&d->dev, "searching for i450NX host bridges\n");
	reg = 0xd0;
	for(pxb=0; pxb<2; pxb++) {
		pci_read_config_byte(d, reg++, &busno);
		pci_read_config_byte(d, reg++, &suba);
		pci_read_config_byte(d, reg++, &subb);
		dev_dbg(&d->dev, "i450NX PXB %d: %02x/%02x/%02x\n",
			pxb, busno, suba, subb);
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

int __init pci_numaq_init(void)
{
	int quad;

	raw_pci_ops = &pci_direct_conf1_mq;

	pcibios_scan_root(0);
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
