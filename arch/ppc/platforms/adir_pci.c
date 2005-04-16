/*
 * arch/ppc/platforms/adir_pci.c
 *
 * PCI support for SBS Adirondack
 *
 * By Michael Sokolov <msokolov@ivan.Harhan.ORG>
 * based on the K2 version by Matt Porter <mporter@mvista.com>
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/pci.h>
#include <linux/slab.h>

#include <asm/byteorder.h>
#include <asm/io.h>
#include <asm/uaccess.h>
#include <asm/machdep.h>
#include <asm/pci-bridge.h>

#include <syslib/cpc710.h>
#include "adir.h"

#undef DEBUG
#ifdef DEBUG
#define DBG(x...) printk(x)
#else
#define DBG(x...)
#endif /* DEBUG */

static inline int __init
adir_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
#define	PCIIRQ(a,b,c,d)	{ADIR_IRQ_##a,ADIR_IRQ_##b,ADIR_IRQ_##c,ADIR_IRQ_##d},
	struct pci_controller *hose = pci_bus_to_hose(dev->bus->number);
	/*
	 * The three PCI devices on the motherboard have dedicated lines to the
	 * CPLD interrupt controller, bypassing the standard PCI INTA-D and the
	 * PC interrupt controller. All other PCI devices (slots) have usual
	 * staggered INTA-D lines, resulting in 8 lines total (PCI0 INTA-D and
	 * PCI1 INTA-D). All 8 go to the CPLD interrupt controller. PCI0 INTA-D
	 * also go to the south bridge, so we have the option of taking them
	 * via the CPLD interrupt controller or via the south bridge 8259
	 * 8258 thingy. PCI1 INTA-D can only be taken via the CPLD interrupt
	 * controller. We take all PCI interrupts via the CPLD interrupt
	 * controller as recommended by SBS.
	 *
	 * We also have some monkey business with the PCI devices within the
	 * VT82C686B south bridge itself. This chip actually has 7 functions on
	 * its IDSEL. Function 0 is the actual south bridge, function 1 is IDE,
	 * and function 4 is some special stuff. The other 4 functions are just
	 * regular PCI devices bundled in the chip. 2 and 3 are USB UHCIs and 5
	 * and 6 are audio (not supported on the Adirondack).
	 *
	 * This is where the monkey business begins. PCI devices are supposed
	 * to signal normal PCI interrupts. But the 4 functions in question are
	 * located in the south bridge chip, which is designed with the
	 * assumption that it will be fielding PCI INTA-D interrupts rather
	 * than generating them. Here's what it does. Each of the functions in
	 * question routes its interrupt to one of the IRQs on the 8259 thingy.
	 * Which one? It looks at the Interrupt Line register in the PCI config
	 * space, even though the PCI spec says it's for BIOS/OS interaction
	 * only.
	 *
	 * How do we deal with this? We take these interrupts via 8259 IRQs as
	 * we have to. We return the desired IRQ numbers from this routine when
	 * called for the functions in question. The PCI scan code will then
	 * stick our return value into the Interrupt Line register in the PCI
	 * config space, and the interrupt will actually go there. We identify
	 * these functions within the south bridge IDSEL by their interrupt pin
	 * numbers, as the VT82C686B has 04 in the Interrupt Pin register for
	 * USB and 03 for audio.
	 */
	if (!hose->index) {
		static char pci_irq_table[][4] =
		/*
		 *             PCI IDSEL/INTPIN->INTLINE
		 *             A          B          C          D
		 */
		{
    /* south bridge */	PCIIRQ(IDE0,      NONE,      VIA_AUDIO, VIA_USB)
    /* Ethernet 0 */	PCIIRQ(MBETH0,    MBETH0,    MBETH0,    MBETH0)
    /* PCI0 slot 1 */	PCIIRQ(PCI0_INTB, PCI0_INTC, PCI0_INTD, PCI0_INTA)
    /* PCI0 slot 2 */	PCIIRQ(PCI0_INTC, PCI0_INTD, PCI0_INTA, PCI0_INTB)
    /* PCI0 slot 3 */	PCIIRQ(PCI0_INTD, PCI0_INTA, PCI0_INTB, PCI0_INTC)
		};
		const long min_idsel = 3, max_idsel = 7, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	} else {
		static char pci_irq_table[][4] =
		/*
		 *             PCI IDSEL/INTPIN->INTLINE
		 *             A          B          C          D
		 */
		{
    /* Ethernet 1 */	PCIIRQ(MBETH1,    MBETH1,    MBETH1,    MBETH1)
    /* SCSI */		PCIIRQ(MBSCSI,    MBSCSI,    MBSCSI,    MBSCSI)
    /* PCI1 slot 1 */	PCIIRQ(PCI1_INTB, PCI1_INTC, PCI1_INTD, PCI1_INTA)
    /* PCI1 slot 2 */	PCIIRQ(PCI1_INTC, PCI1_INTD, PCI1_INTA, PCI1_INTB)
    /* PCI1 slot 3 */	PCIIRQ(PCI1_INTD, PCI1_INTA, PCI1_INTB, PCI1_INTC)
		};
		const long min_idsel = 3, max_idsel = 7, irqs_per_slot = 4;
		return PCI_IRQ_TABLE_LOOKUP;
	}
#undef PCIIRQ
}

static void
adir_pcibios_fixup_resources(struct pci_dev *dev)
{
	int i;

	if ((dev->vendor == PCI_VENDOR_ID_IBM) &&
			(dev->device == PCI_DEVICE_ID_IBM_CPC710_PCI64))
	{
		DBG("Fixup CPC710 resources\n");
		for (i=0; i<DEVICE_COUNT_RESOURCE; i++)
		{
			dev->resource[i].start = 0;
			dev->resource[i].end = 0;
		}
	}
}

/*
 * CPC710 DD3 has an errata causing it to hang the system if a type 0 config
 * cycle is attempted on its PCI32 interface with a device number > 21.
 * CPC710's PCI bridges map device numbers 1 through 21 to AD11 through AD31.
 * Per the PCI spec it MUST accept all other device numbers and do nothing, and
 * software MUST scan all device numbers without assuming how IDSELs are
 * mapped. However, as the CPC710 DD3's errata causes such correct scanning
 * procedure to hang the system, we have no choice but to introduce this hack
 * of knowingly avoiding device numbers > 21 on PCI0,
 */
static int
adir_exclude_device(u_char bus, u_char devfn)
{
	if ((bus == 0) && (PCI_SLOT(devfn) > 21))
		return PCIBIOS_DEVICE_NOT_FOUND;
	else
		return PCIBIOS_SUCCESSFUL;
}

void adir_find_bridges(void)
{
	struct pci_controller *hose_a, *hose_b;

	/* Setup PCI32 hose */
	hose_a = pcibios_alloc_controller();
	if (!hose_a)
		return;

	hose_a->first_busno = 0;
	hose_a->last_busno = 0xff;
	hose_a->pci_mem_offset = ADIR_PCI32_MEM_BASE;
	hose_a->io_space.start = 0;
	hose_a->io_space.end = ADIR_PCI32_VIRT_IO_SIZE - 1;
	hose_a->mem_space.start = 0;
	hose_a->mem_space.end = ADIR_PCI32_MEM_SIZE - 1;
	hose_a->io_resource.start = 0;
	hose_a->io_resource.end = ADIR_PCI32_VIRT_IO_SIZE - 1;
	hose_a->io_resource.flags = IORESOURCE_IO;
	hose_a->mem_resources[0].start = ADIR_PCI32_MEM_BASE;
	hose_a->mem_resources[0].end = ADIR_PCI32_MEM_BASE +
					ADIR_PCI32_MEM_SIZE - 1;
	hose_a->mem_resources[0].flags = IORESOURCE_MEM;
	hose_a->io_base_phys = ADIR_PCI32_IO_BASE;
	hose_a->io_base_virt = (void *) ADIR_PCI32_VIRT_IO_BASE;

	ppc_md.pci_exclude_device = adir_exclude_device;
	setup_indirect_pci(hose_a, ADIR_PCI32_CONFIG_ADDR,
			   ADIR_PCI32_CONFIG_DATA);

	/* Initialize PCI32 bus registers */
	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(0, 0),
			CPC710_BUS_NUMBER,
			hose_a->first_busno);
	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(0, 0),
			CPC710_SUB_BUS_NUMBER,
			hose_a->last_busno);

	hose_a->last_busno = pciauto_bus_scan(hose_a, hose_a->first_busno);

	/* Write out correct max subordinate bus number for hose A */
	early_write_config_byte(hose_a,
			hose_a->first_busno,
			PCI_DEVFN(0, 0),
			CPC710_SUB_BUS_NUMBER,
			hose_a->last_busno);

	/* Setup PCI64 hose */
	hose_b = pcibios_alloc_controller();
	if (!hose_b)
		return;

	hose_b->first_busno = hose_a->last_busno + 1;
	hose_b->last_busno = 0xff;
	hose_b->pci_mem_offset = ADIR_PCI64_MEM_BASE;
	hose_b->io_space.start = 0;
	hose_b->io_space.end = ADIR_PCI64_VIRT_IO_SIZE - 1;
	hose_b->mem_space.start = 0;
	hose_b->mem_space.end = ADIR_PCI64_MEM_SIZE - 1;
	hose_b->io_resource.start = 0;
	hose_b->io_resource.end = ADIR_PCI64_VIRT_IO_SIZE - 1;
	hose_b->io_resource.flags = IORESOURCE_IO;
	hose_b->mem_resources[0].start = ADIR_PCI64_MEM_BASE;
	hose_b->mem_resources[0].end = ADIR_PCI64_MEM_BASE +
					ADIR_PCI64_MEM_SIZE - 1;
	hose_b->mem_resources[0].flags = IORESOURCE_MEM;
	hose_b->io_base_phys = ADIR_PCI64_IO_BASE;
	hose_b->io_base_virt = (void *) ADIR_PCI64_VIRT_IO_BASE;

	setup_indirect_pci(hose_b, ADIR_PCI64_CONFIG_ADDR,
			   ADIR_PCI64_CONFIG_DATA);

	/* Initialize PCI64 bus registers */
	early_write_config_byte(hose_b,
			0,
			PCI_DEVFN(0, 0),
			CPC710_SUB_BUS_NUMBER,
			0xff);

	early_write_config_byte(hose_b,
			0,
			PCI_DEVFN(0, 0),
			CPC710_BUS_NUMBER,
			hose_b->first_busno);

	hose_b->last_busno = pciauto_bus_scan(hose_b,
			hose_b->first_busno);

	/* Write out correct max subordinate bus number for hose B */
	early_write_config_byte(hose_b,
			hose_b->first_busno,
			PCI_DEVFN(0, 0),
			CPC710_SUB_BUS_NUMBER,
			hose_b->last_busno);

	ppc_md.pcibios_fixup = NULL;
	ppc_md.pcibios_fixup_resources = adir_pcibios_fixup_resources;
	ppc_md.pci_swizzle = common_swizzle;
	ppc_md.pci_map_irq = adir_map_irq;
}
