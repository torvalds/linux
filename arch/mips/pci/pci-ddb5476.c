#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/pci.h>

#include <asm/debug.h>

#include <asm/ddb5xxx/ddb5xxx.h>

static struct resource extpci_io_resource = {
	.start	= 0x1000,		/* leave some room for ISA bus */
	.end	= DDB_PCI_IO_SIZE - 1,
	.name	= "pci IO space",
	.flags	= IORESOURCE_IO
};

static struct resource extpci_mem_resource = {
	.start	= DDB_PCI_MEM_BASE + 0x00100000,	/* leave 1 MB for RTC */
	.end	= DDB_PCI_MEM_BASE + DDB_PCI_MEM_SIZE - 1,
	.name	= "pci memory space",
	.flags	= IORESOURCE_MEM
};

extern struct pci_ops ddb5476_ext_pci_ops;

struct pci_controller ddb5476_controller = {
	.pci_ops	= &ddb5476_ext_pci_ops,
	.io_resource	= &extpci_io_resource,
	.mem_resource	= &extpci_mem_resource
};


/*
 * we fix up irqs based on the slot number.
 * The first entry is at AD:11.
 *
 * This does not work for devices on sub-buses yet.
 */

/*
 * temporary
 */

#define		PCI_EXT_INTA		8
#define		PCI_EXT_INTB		9
#define		PCI_EXT_INTC		10
#define		PCI_EXT_INTD		11
#define		PCI_EXT_INTE		12

/*
 * based on ddb5477 manual page 11
 */
#define		MAX_SLOT_NUM		21
static unsigned char irq_map[MAX_SLOT_NUM] = {
 [ 2] = 9,				/* AD:13	USB		*/
 [ 3] = 10,				/* AD:14	PMU		*/
 [ 5] = 0,				/* AD:16 	P2P bridge	*/
 [ 6] = nile4_to_irq(PCI_EXT_INTB),	/* AD:17			*/
 [ 7] =	nile4_to_irq(PCI_EXT_INTC),	/* AD:18			*/
 [ 8] = nile4_to_irq(PCI_EXT_INTD),	/* AD:19			*/
 [ 9] = nile4_to_irq(PCI_EXT_INTA),	/* AD:20			*/
 [13] = 14,				/* AD:24 HD controller, M5229	*/
};

int __init pcibios_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	return irq_map[slot];
}

/* Do platform specific device initialization at pci_enable_device() time */
int pcibios_plat_dev_init(struct pci_dev *dev)
{
	return 0;
}

void __init ddb_pci_reset_bus(void)
{
	u32 temp;

	/*
	 * I am not sure about the "official" procedure, the following
	 * steps work as far as I know:
	 * We first set PCI cold reset bit (bit 31) in PCICTRL-H.
	 * Then we clear the PCI warm reset bit (bit 30) to 0 in PCICTRL-H.
	 * The same is true for both PCI channels.
	 */
	temp = ddb_in32(DDB_PCICTRL + 4);
	temp |= 0x80000000;
	ddb_out32(DDB_PCICTRL + 4, temp);
	temp &= ~0xc0000000;
	ddb_out32(DDB_PCICTRL + 4, temp);

}
