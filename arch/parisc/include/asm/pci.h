/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_PARISC_PCI_H
#define __ASM_PARISC_PCI_H

#include <linux/scatterlist.h>



/*
** HP PCI platforms generally support multiple bus adapters.
**    (workstations 1-~4, servers 2-~32)
**
** Newer platforms number the busses across PCI bus adapters *sparsely*.
** E.g. 0, 8, 16, ...
**
** Under a PCI bus, most HP platforms support PPBs up to two or three
** levels deep. See "Bit3" product line. 
*/
#define PCI_MAX_BUSSES	256


/* To be used as: mdelay(pci_post_reset_delay);
 *
 * post_reset is the time the kernel should stall to prevent anyone from
 * accessing the PCI bus once #RESET is de-asserted. 
 * PCI spec somewhere says 1 second but with multi-PCI bus systems,
 * this makes the boot time much longer than necessary.
 * 20ms seems to work for all the HP PCI implementations to date.
 */
#define pci_post_reset_delay 50


/*
** pci_hba_data (aka H2P_OBJECT in HP/UX)
**
** This is the "common" or "base" data structure which HBA drivers
** (eg Dino or LBA) are required to place at the top of their own
** platform_data structure.  I've heard this called "C inheritance" too.
**
** Data needed by pcibios layer belongs here.
*/
struct pci_hba_data {
	void __iomem   *base_addr;	/* aka Host Physical Address */
	const struct parisc_device *dev; /* device from PA bus walk */
	struct pci_bus *hba_bus;	/* primary PCI bus below HBA */
	int		hba_num;	/* I/O port space access "key" */
	struct resource bus_num;	/* PCI bus numbers */
	struct resource io_space;	/* PIOP */
	struct resource lmmio_space;	/* bus addresses < 4Gb */
	struct resource elmmio_space;	/* additional bus addresses < 4Gb */
	struct resource gmmio_space;	/* bus addresses > 4Gb */

	/* NOTE: Dino code assumes it can use *all* of the lmmio_space,
	 * elmmio_space and gmmio_space as a contiguous array of
	 * resources.  This #define represents the array size */
	#define DINO_MAX_LMMIO_RESOURCES	3

	unsigned long   lmmio_space_offset;  /* CPU view - PCI view */
	void *          iommu;          /* IOMMU this device is under */
	/* REVISIT - spinlock to protect resources? */

	#define HBA_NAME_SIZE 16
	char io_name[HBA_NAME_SIZE];
	char lmmio_name[HBA_NAME_SIZE];
	char elmmio_name[HBA_NAME_SIZE];
	char gmmio_name[HBA_NAME_SIZE];
};

#define HBA_DATA(d)		((struct pci_hba_data *) (d))

/* 
** We support 2^16 I/O ports per HBA.  These are set up in the form
** 0xbbxxxx, where bb is the bus number and xxxx is the I/O port
** space address.
*/
#define HBA_PORT_SPACE_BITS	16

#define HBA_PORT_BASE(h)	((h) << HBA_PORT_SPACE_BITS)
#define HBA_PORT_SPACE_SIZE	(1UL << HBA_PORT_SPACE_BITS)

#define PCI_PORT_HBA(a)		((a) >> HBA_PORT_SPACE_BITS)
#define PCI_PORT_ADDR(a)	((a) & (HBA_PORT_SPACE_SIZE - 1))

#ifdef CONFIG_64BIT
#define PCI_F_EXTEND		0xffffffff00000000UL
#else	/* !CONFIG_64BIT */
#define PCI_F_EXTEND		0UL
#endif /* !CONFIG_64BIT */

/*
** Most PCI devices (eg Tulip, NCR720) also export the same registers
** to both MMIO and I/O port space.  Due to poor performance of I/O Port
** access under HP PCI bus adapters, strongly recommend the use of MMIO
** address space.
**
** While I'm at it more PA programming notes:
**
** 1) MMIO stores (writes) are posted operations. This means the processor
**    gets an "ACK" before the write actually gets to the device. A read
**    to the same device (or typically the bus adapter above it) will
**    force in-flight write transaction(s) out to the targeted device
**    before the read can complete.
**
** 2) The Programmed I/O (PIO) data may not always be strongly ordered with
**    respect to DMA on all platforms. Ie PIO data can reach the processor
**    before in-flight DMA reaches memory. Since most SMP PA platforms
**    are I/O coherent, it generally doesn't matter...but sometimes
**    it does.
**
** I've helped device driver writers debug both types of problems.
*/
struct pci_port_ops {
	  u8 (*inb)  (struct pci_hba_data *hba, u16 port);
	 u16 (*inw)  (struct pci_hba_data *hba, u16 port);
	 u32 (*inl)  (struct pci_hba_data *hba, u16 port);
	void (*outb) (struct pci_hba_data *hba, u16 port,  u8 data);
	void (*outw) (struct pci_hba_data *hba, u16 port, u16 data);
	void (*outl) (struct pci_hba_data *hba, u16 port, u32 data);
};


struct pci_bios_ops {
	void (*init)(void);
	void (*fixup_bus)(struct pci_bus *bus);
};

/*
** Stuff declared in arch/parisc/kernel/pci.c
*/
extern struct pci_port_ops *pci_port;
extern struct pci_bios_ops *pci_bios;

#ifdef CONFIG_PCI
extern void pcibios_register_hba(struct pci_hba_data *);
#else
static inline void pcibios_register_hba(struct pci_hba_data *x)
{
}
#endif
extern void pcibios_init_bridge(struct pci_dev *);

/*
 * pcibios_assign_all_busses() is used in drivers/pci/pci.c:pci_do_scan_bus()
 *   0 == check if bridge is numbered before re-numbering.
 *   1 == pci_do_scan_bus() should automatically number all PCI-PCI bridges.
 *
 *   We *should* set this to zero for "legacy" platforms and one
 *   for PAT platforms.
 *
 *   But legacy platforms also need to renumber the busses below a Host
 *   Bus controller.  Adding a 4-port Tulip card on the first PCI root
 *   bus of a C200 resulted in the secondary bus being numbered as 1.
 *   The second PCI host bus controller's root bus had already been
 *   assigned bus number 1 by firmware and sysfs complained.
 *
 *   Firmware isn't doing anything wrong here since each controller
 *   is its own PCI domain.  It's simpler and easier for us to renumber
 *   the busses rather than treat each Dino as a separate PCI domain.
 *   Eventually, we may want to introduce PCI domains for Superdome or
 *   rp7420/8420 boxes and then revisit this issue.
 */
#define pcibios_assign_all_busses()     (1)

#define PCIBIOS_MIN_IO          0x10
#define PCIBIOS_MIN_MEM         0x1000 /* NBPG - but pci/setup-res.c dies */

static inline int pci_get_legacy_ide_irq(struct pci_dev *dev, int channel)
{
	return channel ? 15 : 14;
}

#define HAVE_PCI_MMAP
#define ARCH_GENERIC_PCI_MMAP_RESOURCE

#endif /* __ASM_PARISC_PCI_H */
