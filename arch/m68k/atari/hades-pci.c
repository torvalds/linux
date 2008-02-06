/*
 * hades-pci.c - Hardware specific PCI BIOS functions the Hades Atari clone.
 *
 * Written by Wout Klaren.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <asm/io.h>

#if 0
# define DBG_DEVS(args)		printk args
#else
# define DBG_DEVS(args)
#endif

#if defined(CONFIG_PCI) && defined(CONFIG_HADES)

#include <linux/slab.h>
#include <linux/mm.h>
#include <linux/pci.h>

#include <asm/atarihw.h>
#include <asm/atariints.h>
#include <asm/byteorder.h>
#include <asm/pci.h>

#define HADES_MEM_BASE		0x80000000
#define HADES_MEM_SIZE		0x20000000
#define HADES_CONFIG_BASE	0xA0000000
#define HADES_CONFIG_SIZE	0x10000000
#define HADES_IO_BASE		0xB0000000
#define HADES_IO_SIZE		0x10000000
#define HADES_VIRT_IO_SIZE	0x00010000	/* Only 64k is remapped and actually used. */

#define N_SLOTS				4			/* Number of PCI slots. */

static const char pci_mem_name[] = "PCI memory space";
static const char pci_io_name[] = "PCI I/O space";
static const char pci_config_name[] = "PCI config space";

static struct resource config_space = {
    .name = pci_config_name,
    .start = HADES_CONFIG_BASE,
    .end = HADES_CONFIG_BASE + HADES_CONFIG_SIZE - 1
};
static struct resource io_space = {
    .name = pci_io_name,
    .start = HADES_IO_BASE,
    .end = HADES_IO_BASE + HADES_IO_SIZE - 1
};

static const unsigned long pci_conf_base_phys[] = {
    0xA0080000, 0xA0040000, 0xA0020000, 0xA0010000
};
static unsigned long pci_conf_base_virt[N_SLOTS];
static unsigned long pci_io_base_virt;

/*
 * static void *mk_conf_addr(unsigned char bus, unsigned char device_fn,
 *			     unsigned char where)
 *
 * Calculate the address of the PCI configuration area of the given
 * device.
 *
 * BUG: boards with multiple functions are probably not correctly
 * supported.
 */

static void *mk_conf_addr(struct pci_dev *dev, int where)
{
	int device = dev->devfn >> 3, function = dev->devfn & 7;
	void *result;

	DBG_DEVS(("mk_conf_addr(bus=%d ,device_fn=0x%x, where=0x%x, pci_addr=0x%p)\n",
		  dev->bus->number, dev->devfn, where, pci_addr));

	if (device > 3)
	{
		DBG_DEVS(("mk_conf_addr: device (%d) > 3, returning NULL\n", device));
		return NULL;
	}

	if (dev->bus->number != 0)
	{
		DBG_DEVS(("mk_conf_addr: bus (%d) > 0, returning NULL\n", device));
		return NULL;
	}

	result = (void *) (pci_conf_base_virt[device] | (function << 8) | (where));
	DBG_DEVS(("mk_conf_addr: returning pci_addr 0x%lx\n", (unsigned long) result));
	return result;
}

static int hades_read_config_byte(struct pci_dev *dev, int where, u8 *value)
{
	volatile unsigned char *pci_addr;

	*value = 0xff;

	if ((pci_addr = (unsigned char *) mk_conf_addr(dev, where)) == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = *pci_addr;

	return PCIBIOS_SUCCESSFUL;
}

static int hades_read_config_word(struct pci_dev *dev, int where, u16 *value)
{
	volatile unsigned short *pci_addr;

	*value = 0xffff;

	if (where & 0x1)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if ((pci_addr = (unsigned short *) mk_conf_addr(dev, where)) == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = le16_to_cpu(*pci_addr);

	return PCIBIOS_SUCCESSFUL;
}

static int hades_read_config_dword(struct pci_dev *dev, int where, u32 *value)
{
	volatile unsigned int *pci_addr;
	unsigned char header_type;
	int result;

	*value = 0xffffffff;

	if (where & 0x3)
		return PCIBIOS_BAD_REGISTER_NUMBER;

	if ((pci_addr = (unsigned int *) mk_conf_addr(dev, where)) == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	*value = le32_to_cpu(*pci_addr);

	/*
	 * Check if the value is an address on the bus. If true, add the
	 * base address of the PCI memory or PCI I/O area on the Hades.
	 */

	if ((result = hades_read_config_byte(dev, PCI_HEADER_TYPE,
					     &header_type)) != PCIBIOS_SUCCESSFUL)
		return result;

	if (((where >= PCI_BASE_ADDRESS_0) && (where <= PCI_BASE_ADDRESS_1)) ||
	    ((header_type != PCI_HEADER_TYPE_BRIDGE) && ((where >= PCI_BASE_ADDRESS_2) &&
							 (where <= PCI_BASE_ADDRESS_5))))
	{
		if ((*value & PCI_BASE_ADDRESS_SPACE) == PCI_BASE_ADDRESS_SPACE_IO)
		{
			/*
			 * Base address register that contains an I/O address. If the
			 * address is valid on the Hades (0 <= *value < HADES_VIRT_IO_SIZE),
			 * add 'pci_io_base_virt' to the value.
			 */

			if (*value < HADES_VIRT_IO_SIZE)
				*value += pci_io_base_virt;
		}
		else
		{
			/*
			 * Base address register that contains an memory address. If the
			 * address is valid on the Hades (0 <= *value < HADES_MEM_SIZE),
			 * add HADES_MEM_BASE to the value.
			 */

			if (*value == 0)
			{
				/*
				 * Base address is 0. Test if this base
				 * address register is used.
				 */

				*pci_addr = 0xffffffff;
				if (*pci_addr != 0)
				{
					*pci_addr = *value;
					if (*value < HADES_MEM_SIZE)
						*value += HADES_MEM_BASE;
				}
			}
			else
			{
				if (*value < HADES_MEM_SIZE)
					*value += HADES_MEM_BASE;
			}
		}
	}

	return PCIBIOS_SUCCESSFUL;
}

static int hades_write_config_byte(struct pci_dev *dev, int where, u8 value)
{
	volatile unsigned char *pci_addr;

	if ((pci_addr = (unsigned char *) mk_conf_addr(dev, where)) == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	*pci_addr = value;

	return PCIBIOS_SUCCESSFUL;
}

static int hades_write_config_word(struct pci_dev *dev, int where, u16 value)
{
	volatile unsigned short *pci_addr;

	if ((pci_addr = (unsigned short *) mk_conf_addr(dev, where)) == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	*pci_addr = cpu_to_le16(value);

	return PCIBIOS_SUCCESSFUL;
}

static int hades_write_config_dword(struct pci_dev *dev, int where, u32 value)
{
	volatile unsigned int *pci_addr;
	unsigned char header_type;
	int result;

	if ((pci_addr = (unsigned int *) mk_conf_addr(dev, where)) == NULL)
		return PCIBIOS_DEVICE_NOT_FOUND;

	/*
	 * Check if the value is an address on the bus. If true, subtract the
	 * base address of the PCI memory or PCI I/O area on the Hades.
	 */

	if ((result = hades_read_config_byte(dev, PCI_HEADER_TYPE,
					     &header_type)) != PCIBIOS_SUCCESSFUL)
		return result;

	if (((where >= PCI_BASE_ADDRESS_0) && (where <= PCI_BASE_ADDRESS_1)) ||
	    ((header_type != PCI_HEADER_TYPE_BRIDGE) && ((where >= PCI_BASE_ADDRESS_2) &&
							 (where <= PCI_BASE_ADDRESS_5))))
	{
		if ((value & PCI_BASE_ADDRESS_SPACE) ==
		    PCI_BASE_ADDRESS_SPACE_IO)
		{
			/*
			 * I/O address. Check if the address is valid address on
			 * the Hades (pci_io_base_virt <= value < pci_io_base_virt +
			 * HADES_VIRT_IO_SIZE) or if the value is 0xffffffff. If not
			 * true do not write the base address register. If it is a
			 * valid base address subtract 'pci_io_base_virt' from the value.
			 */

			if ((value >= pci_io_base_virt) && (value < (pci_io_base_virt +
														 HADES_VIRT_IO_SIZE)))
				value -= pci_io_base_virt;
			else
			{
				if (value != 0xffffffff)
					return PCIBIOS_SET_FAILED;
			}
		}
		else
		{
			/*
			 * Memory address. Check if the address is valid address on
			 * the Hades (HADES_MEM_BASE <= value < HADES_MEM_BASE + HADES_MEM_SIZE) or
			 * if the value is 0xffffffff. If not true do not write
			 * the base address register. If it is a valid base address
			 * subtract HADES_MEM_BASE from the value.
			 */

			if ((value >= HADES_MEM_BASE) && (value < (HADES_MEM_BASE + HADES_MEM_SIZE)))
				value -= HADES_MEM_BASE;
			else
			{
				if (value != 0xffffffff)
					return PCIBIOS_SET_FAILED;
			}
		}
	}

	*pci_addr = cpu_to_le32(value);

	return PCIBIOS_SUCCESSFUL;
}

/*
 * static inline void hades_fixup(void)
 *
 * Assign IRQ numbers as used by Linux to the interrupt pins
 * of the PCI cards.
 */

static void __init hades_fixup(int pci_modify)
{
	char irq_tab[4] = {
		[0] = IRQ_TT_MFP_IO0,		/* Slot 0. */
		[1] = IRQ_TT_MFP_IO1,		/* Slot 1. */
		[2] = IRQ_TT_MFP_SCC,		/* Slot 2. */
		[3] = IRQ_TT_MFP_SCSIDMA	/* Slot 3. */
	};
	struct pci_dev *dev = NULL;
	unsigned char slot;

	/*
	 * Go through all devices, fixing up irqs as we see fit:
	 */

	while ((dev = pci_get_device(PCI_ANY_ID, PCI_ANY_ID, dev)) != NULL)
	{
		if (dev->class >> 16 != PCI_BASE_CLASS_BRIDGE)
		{
			slot = PCI_SLOT(dev->devfn);	/* Determine slot number. */
			dev->irq = irq_tab[slot];
			if (pci_modify)
				pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
		}
	}
}

/*
 * static void hades_conf_device(struct pci_dev *dev)
 *
 * Machine dependent Configure the given device.
 *
 * Parameters:
 *
 * dev		- the pci device.
 */

static void __init hades_conf_device(struct pci_dev *dev)
{
	pci_write_config_byte(dev, PCI_CACHE_LINE_SIZE, 0);
}

static struct pci_ops hades_pci_ops = {
	.read_byte =	hades_read_config_byte,
	.read_word =	hades_read_config_word,
	.read_dword =	hades_read_config_dword,
	.write_byte =	hades_write_config_byte,
	.write_word =	hades_write_config_word,
	.write_dword =	hades_write_config_dword
};

/*
 * struct pci_bus_info *init_hades_pci(void)
 *
 * Machine specific initialisation:
 *
 * - Allocate and initialise a 'pci_bus_info' structure
 * - Initialise hardware
 *
 * Result: pointer to 'pci_bus_info' structure.
 */

struct pci_bus_info * __init init_hades_pci(void)
{
	struct pci_bus_info *bus;
	int i;

	/*
	 * Remap I/O and configuration space.
	 */

	pci_io_base_virt = (unsigned long) ioremap(HADES_IO_BASE, HADES_VIRT_IO_SIZE);

	for (i = 0; i < N_SLOTS; i++)
		pci_conf_base_virt[i] = (unsigned long) ioremap(pci_conf_base_phys[i], 0x10000);

	/*
	 * Allocate memory for bus info structure.
	 */

	bus = kzalloc(sizeof(struct pci_bus_info), GFP_KERNEL);
	if (unlikely(!bus))
		goto iounmap_base_virt;

	/*
	 * Claim resources. The m68k has no separate I/O space, both
	 * PCI memory space and PCI I/O space are in memory space. Therefore
	 * the I/O resources are requested in memory space as well.
	 */

	if (unlikely(request_resource(&iomem_resource, &config_space) != 0))
		goto free_bus;

	if (unlikely(request_resource(&iomem_resource, &io_space) != 0))
		goto release_config_space;

	bus->mem_space.start = HADES_MEM_BASE;
	bus->mem_space.end = HADES_MEM_BASE + HADES_MEM_SIZE - 1;
	bus->mem_space.name = pci_mem_name;
#if 1
	if (unlikely(request_resource(&iomem_resource, &bus->mem_space) != 0))
		goto release_io_space;
#endif
	bus->io_space.start = pci_io_base_virt;
	bus->io_space.end = pci_io_base_virt + HADES_VIRT_IO_SIZE - 1;
	bus->io_space.name = pci_io_name;
#if 1
	if (unlikely(request_resource(&ioport_resource, &bus->io_space) != 0))
		goto release_bus_mem_space;
#endif
	/*
	 * Set hardware dependent functions.
	 */

	bus->m68k_pci_ops = &hades_pci_ops;
	bus->fixup = hades_fixup;
	bus->conf_device = hades_conf_device;

	/*
	 * Select high to low edge for PCI interrupts.
	 */

	tt_mfp.active_edge &= ~0x27;

	return bus;

release_bus_mem_space:
	release_resource(&bus->mem_space);
release_io_space:
	release_resource(&io_space);
release_config_space:
	release_resource(&config_space);
free_bus:
	kfree(bus);
iounmap_base_virt:
	iounmap((void *)pci_io_base_virt);

	for (i = 0; i < N_SLOTS; i++)
		iounmap((void *)pci_conf_base_virt[i]);

	return NULL;
}
#endif
