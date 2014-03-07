/*
 * Intel MID PCI support
 *   Copyright (c) 2008 Intel Corporation
 *     Jesse Barnes <jesse.barnes@intel.com>
 *
 * Moorestown has an interesting PCI implementation:
 *   - configuration space is memory mapped (as defined by MCFG)
 *   - Lincroft devices also have a real, type 1 configuration space
 *   - Early Lincroft silicon has a type 1 access bug that will cause
 *     a hang if non-existent devices are accessed
 *   - some devices have the "fixed BAR" capability, which means
 *     they can't be relocated or modified; check for that during
 *     BAR sizing
 *
 * So, we use the MCFG space for all reads and writes, but also send
 * Lincroft writes to type 1 space.  But only read/write if the device
 * actually exists, otherwise return all 1s for reads and bit bucket
 * the writes.
 */

#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/ioport.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/acpi.h>
#include <linux/io.h>
#include <linux/smp.h>

#include <asm/segment.h>
#include <asm/pci_x86.h>
#include <asm/hw_irq.h>
#include <asm/io_apic.h>

#define PCIE_CAP_OFFSET	0x100

/* Fixed BAR fields */
#define PCIE_VNDR_CAP_ID_FIXED_BAR 0x00	/* Fixed BAR (TBD) */
#define PCI_FIXED_BAR_0_SIZE	0x04
#define PCI_FIXED_BAR_1_SIZE	0x08
#define PCI_FIXED_BAR_2_SIZE	0x0c
#define PCI_FIXED_BAR_3_SIZE	0x10
#define PCI_FIXED_BAR_4_SIZE	0x14
#define PCI_FIXED_BAR_5_SIZE	0x1c

static int pci_soc_mode;

/**
 * fixed_bar_cap - return the offset of the fixed BAR cap if found
 * @bus: PCI bus
 * @devfn: device in question
 *
 * Look for the fixed BAR cap on @bus and @devfn, returning its offset
 * if found or 0 otherwise.
 */
static int fixed_bar_cap(struct pci_bus *bus, unsigned int devfn)
{
	int pos;
	u32 pcie_cap = 0, cap_data;

	pos = PCIE_CAP_OFFSET;

	if (!raw_pci_ext_ops)
		return 0;

	while (pos) {
		if (raw_pci_ext_ops->read(pci_domain_nr(bus), bus->number,
					  devfn, pos, 4, &pcie_cap))
			return 0;

		if (PCI_EXT_CAP_ID(pcie_cap) == 0x0000 ||
			PCI_EXT_CAP_ID(pcie_cap) == 0xffff)
			break;

		if (PCI_EXT_CAP_ID(pcie_cap) == PCI_EXT_CAP_ID_VNDR) {
			raw_pci_ext_ops->read(pci_domain_nr(bus), bus->number,
					      devfn, pos + 4, 4, &cap_data);
			if ((cap_data & 0xffff) == PCIE_VNDR_CAP_ID_FIXED_BAR)
				return pos;
		}

		pos = PCI_EXT_CAP_NEXT(pcie_cap);
	}

	return 0;
}

static int pci_device_update_fixed(struct pci_bus *bus, unsigned int devfn,
				   int reg, int len, u32 val, int offset)
{
	u32 size;
	unsigned int domain, busnum;
	int bar = (reg - PCI_BASE_ADDRESS_0) >> 2;

	domain = pci_domain_nr(bus);
	busnum = bus->number;

	if (val == ~0 && len == 4) {
		unsigned long decode;

		raw_pci_ext_ops->read(domain, busnum, devfn,
			       offset + 8 + (bar * 4), 4, &size);

		/* Turn the size into a decode pattern for the sizing code */
		if (size) {
			decode = size - 1;
			decode |= decode >> 1;
			decode |= decode >> 2;
			decode |= decode >> 4;
			decode |= decode >> 8;
			decode |= decode >> 16;
			decode++;
			decode = ~(decode - 1);
		} else {
			decode = 0;
		}

		/*
		 * If val is all ones, the core code is trying to size the reg,
		 * so update the mmconfig space with the real size.
		 *
		 * Note: this assumes the fixed size we got is a power of two.
		 */
		return raw_pci_ext_ops->write(domain, busnum, devfn, reg, 4,
				       decode);
	}

	/* This is some other kind of BAR write, so just do it. */
	return raw_pci_ext_ops->write(domain, busnum, devfn, reg, len, val);
}

/**
 * type1_access_ok - check whether to use type 1
 * @bus: bus number
 * @devfn: device & function in question
 *
 * If the bus is on a Lincroft chip and it exists, or is not on a Lincroft at
 * all, the we can go ahead with any reads & writes.  If it's on a Lincroft,
 * but doesn't exist, avoid the access altogether to keep the chip from
 * hanging.
 */
static bool type1_access_ok(unsigned int bus, unsigned int devfn, int reg)
{
	/*
	 * This is a workaround for A0 LNC bug where PCI status register does
	 * not have new CAP bit set. can not be written by SW either.
	 *
	 * PCI header type in real LNC indicates a single function device, this
	 * will prevent probing other devices under the same function in PCI
	 * shim. Therefore, use the header type in shim instead.
	 */
	if (reg >= 0x100 || reg == PCI_STATUS || reg == PCI_HEADER_TYPE)
		return false;
	if (bus == 0 && (devfn == PCI_DEVFN(2, 0)
				|| devfn == PCI_DEVFN(0, 0)
				|| devfn == PCI_DEVFN(3, 0)))
		return true;
	return false; /* Langwell on others */
}

static int pci_read(struct pci_bus *bus, unsigned int devfn, int where,
		    int size, u32 *value)
{
	if (type1_access_ok(bus->number, devfn, where))
		return pci_direct_conf1.read(pci_domain_nr(bus), bus->number,
					devfn, where, size, value);
	return raw_pci_ext_ops->read(pci_domain_nr(bus), bus->number,
			      devfn, where, size, value);
}

static int pci_write(struct pci_bus *bus, unsigned int devfn, int where,
		     int size, u32 value)
{
	int offset;

	/*
	 * On MRST, there is no PCI ROM BAR, this will cause a subsequent read
	 * to ROM BAR return 0 then being ignored.
	 */
	if (where == PCI_ROM_ADDRESS)
		return 0;

	/*
	 * Devices with fixed BARs need special handling:
	 *   - BAR sizing code will save, write ~0, read size, restore
	 *   - so writes to fixed BARs need special handling
	 *   - other writes to fixed BAR devices should go through mmconfig
	 */
	offset = fixed_bar_cap(bus, devfn);
	if (offset &&
	    (where >= PCI_BASE_ADDRESS_0 && where <= PCI_BASE_ADDRESS_5)) {
		return pci_device_update_fixed(bus, devfn, where, size, value,
					       offset);
	}

	/*
	 * On Moorestown update both real & mmconfig space
	 * Note: early Lincroft silicon can't handle type 1 accesses to
	 *       non-existent devices, so just eat the write in that case.
	 */
	if (type1_access_ok(bus->number, devfn, where))
		return pci_direct_conf1.write(pci_domain_nr(bus), bus->number,
					      devfn, where, size, value);
	return raw_pci_ext_ops->write(pci_domain_nr(bus), bus->number, devfn,
			       where, size, value);
}

static int intel_mid_pci_irq_enable(struct pci_dev *dev)
{
	u8 pin;
	struct io_apic_irq_attr irq_attr;

	pci_read_config_byte(dev, PCI_INTERRUPT_PIN, &pin);

	/*
	 * MRST only have IOAPIC, the PCI irq lines are 1:1 mapped to
	 * IOAPIC RTE entries, so we just enable RTE for the device.
	 */
	irq_attr.ioapic = mp_find_ioapic(dev->irq);
	irq_attr.ioapic_pin = dev->irq;
	irq_attr.trigger = 1; /* level */
	irq_attr.polarity = 1; /* active low */
	io_apic_set_pci_routing(&dev->dev, dev->irq, &irq_attr);

	return 0;
}

struct pci_ops intel_mid_pci_ops = {
	.read = pci_read,
	.write = pci_write,
};

/**
 * intel_mid_pci_init - installs intel_mid_pci_ops
 *
 * Moorestown has an interesting PCI implementation (see above).
 * Called when the early platform detection installs it.
 */
int __init intel_mid_pci_init(void)
{
	pr_info("Intel MID platform detected, using MID PCI ops\n");
	pci_mmcfg_late_init();
	pcibios_enable_irq = intel_mid_pci_irq_enable;
	pci_root_ops = intel_mid_pci_ops;
	pci_soc_mode = 1;
	/* Continue with standard init */
	return 1;
}

/*
 * Langwell devices are not true PCI devices; they are not subject to 10 ms
 * d3 to d0 delay required by PCI spec.
 */
static void pci_d3delay_fixup(struct pci_dev *dev)
{
	/*
	 * PCI fixups are effectively decided compile time. If we have a dual
	 * SoC/non-SoC kernel we don't want to mangle d3 on non-SoC devices.
	 */
	if (!pci_soc_mode)
		return;
	/*
	 * True PCI devices in Lincroft should allow type 1 access, the rest
	 * are Langwell fake PCI devices.
	 */
	if (type1_access_ok(dev->bus->number, dev->devfn, PCI_DEVICE_ID))
		return;
	dev->d3_delay = 0;
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, PCI_ANY_ID, pci_d3delay_fixup);

static void mrst_power_off_unused_dev(struct pci_dev *dev)
{
	pci_set_power_state(dev, PCI_D3hot);
}
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x0801, mrst_power_off_unused_dev);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x0809, mrst_power_off_unused_dev);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x080C, mrst_power_off_unused_dev);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x0812, mrst_power_off_unused_dev);
DECLARE_PCI_FIXUP_FINAL(PCI_VENDOR_ID_INTEL, 0x0815, mrst_power_off_unused_dev);

/*
 * Langwell devices reside at fixed offsets, don't try to move them.
 */
static void pci_fixed_bar_fixup(struct pci_dev *dev)
{
	unsigned long offset;
	u32 size;
	int i;

	if (!pci_soc_mode)
		return;

	/* Must have extended configuration space */
	if (dev->cfg_size < PCIE_CAP_OFFSET + 4)
		return;

	/* Fixup the BAR sizes for fixed BAR devices and make them unmoveable */
	offset = fixed_bar_cap(dev->bus, dev->devfn);
	if (!offset || PCI_DEVFN(2, 0) == dev->devfn ||
	    PCI_DEVFN(2, 2) == dev->devfn)
		return;

	for (i = 0; i < PCI_ROM_RESOURCE; i++) {
		pci_read_config_dword(dev, offset + 8 + (i * 4), &size);
		dev->resource[i].end = dev->resource[i].start + size - 1;
		dev->resource[i].flags |= IORESOURCE_PCI_FIXED;
	}
}
DECLARE_PCI_FIXUP_HEADER(PCI_VENDOR_ID_INTEL, PCI_ANY_ID, pci_fixed_bar_fixup);
