/*
 * direct.c - Low-level direct PCI config space access
 */

#include <linux/pci.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <asm/pci_x86.h>

/*
 * Functions for accessing PCI base (first 256 bytes) and extended
 * (4096 bytes per PCI function) configuration space with type 1
 * accesses.
 */

#define PCI_CONF1_ADDRESS(bus, devfn, reg) \
	(0x80000000 | ((reg & 0xF00) << 16) | (bus << 16) \
	| (devfn << 8) | (reg & 0xFC))

static int pci_conf1_read(unsigned int seg, unsigned int bus,
			  unsigned int devfn, int reg, int len, u32 *value)
{
	unsigned long flags;

	if (seg || (bus > 255) || (devfn > 255) || (reg > 4095)) {
		*value = -1;
		return -EINVAL;
	}

	raw_spin_lock_irqsave(&pci_config_lock, flags);

	outl(PCI_CONF1_ADDRESS(bus, devfn, reg), 0xCF8);

	switch (len) {
	case 1:
		*value = inb(0xCFC + (reg & 3));
		break;
	case 2:
		*value = inw(0xCFC + (reg & 2));
		break;
	case 4:
		*value = inl(0xCFC);
		break;
	}

	raw_spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

static int pci_conf1_write(unsigned int seg, unsigned int bus,
			   unsigned int devfn, int reg, int len, u32 value)
{
	unsigned long flags;

	if (seg || (bus > 255) || (devfn > 255) || (reg > 4095))
		return -EINVAL;

	raw_spin_lock_irqsave(&pci_config_lock, flags);

	outl(PCI_CONF1_ADDRESS(bus, devfn, reg), 0xCF8);

	switch (len) {
	case 1:
		outb((u8)value, 0xCFC + (reg & 3));
		break;
	case 2:
		outw((u16)value, 0xCFC + (reg & 2));
		break;
	case 4:
		outl((u32)value, 0xCFC);
		break;
	}

	raw_spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

#undef PCI_CONF1_ADDRESS

struct pci_raw_ops pci_direct_conf1 = {
	.read =		pci_conf1_read,
	.write =	pci_conf1_write,
};


/*
 * Functions for accessing PCI configuration space with type 2 accesses
 */

#define PCI_CONF2_ADDRESS(dev, reg)	(u16)(0xC000 | (dev << 8) | reg)

static int pci_conf2_read(unsigned int seg, unsigned int bus,
			  unsigned int devfn, int reg, int len, u32 *value)
{
	unsigned long flags;
	int dev, fn;

	WARN_ON(seg);
	if ((bus > 255) || (devfn > 255) || (reg > 255)) {
		*value = -1;
		return -EINVAL;
	}

	dev = PCI_SLOT(devfn);
	fn = PCI_FUNC(devfn);

	if (dev & 0x10) 
		return PCIBIOS_DEVICE_NOT_FOUND;

	raw_spin_lock_irqsave(&pci_config_lock, flags);

	outb((u8)(0xF0 | (fn << 1)), 0xCF8);
	outb((u8)bus, 0xCFA);

	switch (len) {
	case 1:
		*value = inb(PCI_CONF2_ADDRESS(dev, reg));
		break;
	case 2:
		*value = inw(PCI_CONF2_ADDRESS(dev, reg));
		break;
	case 4:
		*value = inl(PCI_CONF2_ADDRESS(dev, reg));
		break;
	}

	outb(0, 0xCF8);

	raw_spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

static int pci_conf2_write(unsigned int seg, unsigned int bus,
			   unsigned int devfn, int reg, int len, u32 value)
{
	unsigned long flags;
	int dev, fn;

	WARN_ON(seg);
	if ((bus > 255) || (devfn > 255) || (reg > 255)) 
		return -EINVAL;

	dev = PCI_SLOT(devfn);
	fn = PCI_FUNC(devfn);

	if (dev & 0x10) 
		return PCIBIOS_DEVICE_NOT_FOUND;

	raw_spin_lock_irqsave(&pci_config_lock, flags);

	outb((u8)(0xF0 | (fn << 1)), 0xCF8);
	outb((u8)bus, 0xCFA);

	switch (len) {
	case 1:
		outb((u8)value, PCI_CONF2_ADDRESS(dev, reg));
		break;
	case 2:
		outw((u16)value, PCI_CONF2_ADDRESS(dev, reg));
		break;
	case 4:
		outl((u32)value, PCI_CONF2_ADDRESS(dev, reg));
		break;
	}

	outb(0, 0xCF8);    

	raw_spin_unlock_irqrestore(&pci_config_lock, flags);

	return 0;
}

#undef PCI_CONF2_ADDRESS

struct pci_raw_ops pci_direct_conf2 = {
	.read =		pci_conf2_read,
	.write =	pci_conf2_write,
};


/*
 * Before we decide to use direct hardware access mechanisms, we try to do some
 * trivial checks to ensure it at least _seems_ to be working -- we just test
 * whether bus 00 contains a host bridge (this is similar to checking
 * techniques used in XFree86, but ours should be more reliable since we
 * attempt to make use of direct access hints provided by the PCI BIOS).
 *
 * This should be close to trivial, but it isn't, because there are buggy
 * chipsets (yes, you guessed it, by Intel and Compaq) that have no class ID.
 */
static int __init pci_sanity_check(struct pci_raw_ops *o)
{
	u32 x = 0;
	int year, devfn;

	if (pci_probe & PCI_NO_CHECKS)
		return 1;
	/* Assume Type 1 works for newer systems.
	   This handles machines that don't have anything on PCI Bus 0. */
	dmi_get_date(DMI_BIOS_DATE, &year, NULL, NULL);
	if (year >= 2001)
		return 1;

	for (devfn = 0; devfn < 0x100; devfn++) {
		if (o->read(0, 0, devfn, PCI_CLASS_DEVICE, 2, &x))
			continue;
		if (x == PCI_CLASS_BRIDGE_HOST || x == PCI_CLASS_DISPLAY_VGA)
			return 1;

		if (o->read(0, 0, devfn, PCI_VENDOR_ID, 2, &x))
			continue;
		if (x == PCI_VENDOR_ID_INTEL || x == PCI_VENDOR_ID_COMPAQ)
			return 1;
	}

	DBG(KERN_WARNING "PCI: Sanity check failed\n");
	return 0;
}

static int __init pci_check_type1(void)
{
	unsigned long flags;
	unsigned int tmp;
	int works = 0;

	local_irq_save(flags);

	outb(0x01, 0xCFB);
	tmp = inl(0xCF8);
	outl(0x80000000, 0xCF8);
	if (inl(0xCF8) == 0x80000000 && pci_sanity_check(&pci_direct_conf1)) {
		works = 1;
	}
	outl(tmp, 0xCF8);
	local_irq_restore(flags);

	return works;
}

static int __init pci_check_type2(void)
{
	unsigned long flags;
	int works = 0;

	local_irq_save(flags);

	outb(0x00, 0xCFB);
	outb(0x00, 0xCF8);
	outb(0x00, 0xCFA);
	if (inb(0xCF8) == 0x00 && inb(0xCFA) == 0x00 &&
	    pci_sanity_check(&pci_direct_conf2)) {
		works = 1;
	}

	local_irq_restore(flags);

	return works;
}

void __init pci_direct_init(int type)
{
	if (type == 0)
		return;
	printk(KERN_INFO "PCI: Using configuration type %d for base access\n",
		 type);
	if (type == 1) {
		raw_pci_ops = &pci_direct_conf1;
		if (raw_pci_ext_ops)
			return;
		if (!(pci_probe & PCI_HAS_IO_ECS))
			return;
		printk(KERN_INFO "PCI: Using configuration type 1 "
		       "for extended access\n");
		raw_pci_ext_ops = &pci_direct_conf1;
		return;
	}
	raw_pci_ops = &pci_direct_conf2;
}

int __init pci_direct_probe(void)
{
	if ((pci_probe & PCI_PROBE_CONF1) == 0)
		goto type2;
	if (!request_region(0xCF8, 8, "PCI conf1"))
		goto type2;

	if (pci_check_type1()) {
		raw_pci_ops = &pci_direct_conf1;
		port_cf9_safe = true;
		return 1;
	}
	release_region(0xCF8, 8);

 type2:
	if ((pci_probe & PCI_PROBE_CONF2) == 0)
		return 0;
	if (!request_region(0xCF8, 4, "PCI conf2"))
		return 0;
	if (!request_region(0xC000, 0x1000, "PCI conf2"))
		goto fail2;

	if (pci_check_type2()) {
		raw_pci_ops = &pci_direct_conf2;
		port_cf9_safe = true;
		return 2;
	}

	release_region(0xC000, 0x1000);
 fail2:
	release_region(0xCF8, 4);
	return 0;
}
