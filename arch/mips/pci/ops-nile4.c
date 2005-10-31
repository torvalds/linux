#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <asm/bootinfo.h>

#include <asm/lasat/lasat.h>
#include <asm/gt64120.h>
#include <asm/nile4.h>

#define PCI_ACCESS_READ  0
#define PCI_ACCESS_WRITE 1

#define LO(reg) (reg / 4)
#define HI(reg) (reg / 4 + 1)

volatile unsigned long *const vrc_pciregs = (void *) Vrc5074_BASE;

static DEFINE_SPINLOCK(nile4_pci_lock);

static int nile4_pcibios_config_access(unsigned char access_type,
	struct pci_bus *bus, unsigned int devfn, int where, u32 * val)
{
	unsigned char busnum = bus->number;
	u32 adr, mask, err;

	if ((busnum == 0) && (PCI_SLOT(devfn) > 8))
		/* The addressing scheme chosen leaves room for just
		 * 8 devices on the first busnum (besides the PCI
		 * controller itself) */
		return PCIBIOS_DEVICE_NOT_FOUND;

	if ((busnum == 0) && (devfn == PCI_DEVFN(0, 0))) {
		/* Access controller registers directly */
		if (access_type == PCI_ACCESS_WRITE) {
			vrc_pciregs[(0x200 + where) >> 2] = *val;
		} else {
			*val = vrc_pciregs[(0x200 + where) >> 2];
		}
		return PCIBIOS_SUCCESSFUL;
	}

	/* Temporarily map PCI Window 1 to config space */
	mask = vrc_pciregs[LO(NILE4_PCIINIT1)];
	vrc_pciregs[LO(NILE4_PCIINIT1)] = 0x0000001a | (busnum ? 0x200 : 0);

	/* Clear PCI Error register. This also clears the Error Type
	 * bits in the Control register */
	vrc_pciregs[LO(NILE4_PCIERR)] = 0;
	vrc_pciregs[HI(NILE4_PCIERR)] = 0;

	/* Setup address */
	if (busnum == 0)
		adr =
		    KSEG1ADDR(PCI_WINDOW1) +
		    ((1 << (PCI_SLOT(devfn) + 15)) | (PCI_FUNC(devfn) << 8)
		     | (where & ~3));
	else
		adr = KSEG1ADDR(PCI_WINDOW1) | (busnum << 16) | (devfn << 8) |
		      (where & ~3);

	if (access_type == PCI_ACCESS_WRITE)
		*(u32 *) adr = *val;
	else
		*val = *(u32 *) adr;

	/* Check for master or target abort */
	err = (vrc_pciregs[HI(NILE4_PCICTRL)] >> 5) & 0x7;

	/* Restore PCI Window 1 */
	vrc_pciregs[LO(NILE4_PCIINIT1)] = mask;

	if (err)
		return PCIBIOS_DEVICE_NOT_FOUND;

	return PCIBIOS_SUCCESSFUL;
}

static int nile4_pcibios_read(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 * val)
{
	unsigned long flags;
	u32 data = 0;
	int err;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	spin_lock_irqsave(&nile4_pci_lock, flags);
	err = nile4_pcibios_config_access(PCI_ACCESS_READ, bus, devfn, where,
					&data);
	spin_unlock_irqrestore(&nile4_pci_lock, flags);

	if (err)
		return err;

	if (size == 1)
		*val = (data >> ((where & 3) << 3)) & 0xff;
	else if (size == 2)
		*val = (data >> ((where & 3) << 3)) & 0xffff;
	else
		*val = data;

	return PCIBIOS_SUCCESSFUL;
}

static int nile4_pcibios_write(struct pci_bus *bus, unsigned int devfn,
	int where, int size, u32 val)
{
	unsigned long flags;
	u32 data = 0;
	int err;

	if ((size == 2) && (where & 1))
		return PCIBIOS_BAD_REGISTER_NUMBER;
	else if ((size == 4) && (where & 3))
		return PCIBIOS_BAD_REGISTER_NUMBER;

	spin_lock_irqsave(&nile4_pci_lock, flags);
	err = nile4_pcibios_config_access(PCI_ACCESS_READ, bus, devfn, where,
	                                  &data);
	spin_unlock_irqrestore(&nile4_pci_lock, flags);

	if (err)
		return err;

	if (size == 1)
		data = (data & ~(0xff << ((where & 3) << 3))) |
		    (val << ((where & 3) << 3));
	else if (size == 2)
		data = (data & ~(0xffff << ((where & 3) << 3))) |
		    (val << ((where & 3) << 3));
	else
		data = val;

	if (nile4_pcibios_config_access
	    (PCI_ACCESS_WRITE, bus, devfn, where, &data))
		return -1;

	return PCIBIOS_SUCCESSFUL;
}

struct pci_ops nile4_pci_ops = {
	.read = nile4_pcibios_read,
	.write = nile4_pcibios_write,
};
