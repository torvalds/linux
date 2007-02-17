/*
 * Board and PCI setup routines for MCG PowerPlus
 *
 * Author: Randy Vinson <rvinson@mvista.com>
 *
 * Derived from original PowerPlus PReP work by
 * Cort Dougan, Johnnie Peters, Matt Porter, and
 * Troy Benjegerdes.
 *
 * 2001-2004 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

#include <linux/kernel.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/console.h>
#include <linux/pci.h>
#include <linux/ide.h>
#include <linux/seq_file.h>
#include <linux/root_dev.h>

#include <asm/system.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/dma.h>
#include <asm/machdep.h>
#include <asm/prep_nvram.h>
#include <asm/vga.h>
#include <asm/i8259.h>
#include <asm/open_pic.h>
#include <asm/hawk.h>
#include <asm/todc.h>
#include <asm/bootinfo.h>
#include <asm/kgdb.h>
#include <asm/reg.h>

#include "pplus.h"

#undef DUMP_DBATS

TODC_ALLOC();

extern void pplus_setup_hose(void);
extern void pplus_set_VIA_IDE_native(void);

extern unsigned long loops_per_jiffy;
unsigned char *Motherboard_map_name;

/* Tables for known hardware */

/* Motorola Mesquite */
static inline int
mesquite_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	    /*
	     *      MPIC interrupts for various IDSEL values (MPIC IRQ0 =
	     *      Linux IRQ16 (to leave room for ISA IRQs at 0-15).
	     *      PCI IDSEL/INTPIN->INTLINE
	     *         A   B   C   D
	     */
	{
		{18,  0,  0,  0},	/* IDSEL 14 - Enet 0 */
		{ 0,  0,  0,  0},	/* IDSEL 15 - unused */
		{19, 19, 19, 19},	/* IDSEL 16 - PMC Slot 1 */
		{ 0,  0,  0,  0},	/* IDSEL 17 - unused */
		{ 0,  0,  0,  0},	/* IDSEL 18 - unused */
		{ 0,  0,  0,  0},	/* IDSEL 19 - unused */
		{24, 25, 26, 27},	/* IDSEL 20 - P2P bridge (to cPCI 1) */
		{ 0,  0,  0,  0},	/* IDSEL 21 - unused */
		{28, 29, 30, 31}	/* IDSEL 22 - P2P bridge (to cPCI 2) */
	};

	const long min_idsel = 14, max_idsel = 22, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

/* Motorola Sitka */
static inline int
sitka_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	    /*
	     *      MPIC interrupts for various IDSEL values (MPIC IRQ0 =
	     *      Linux IRQ16 (to leave room for ISA IRQs at 0-15).
	     *      PCI IDSEL/INTPIN->INTLINE
	     *         A   B   C   D
	     */
	{
		{18,  0,  0,  0},	/* IDSEL 14 - Enet 0 */
		{ 0,  0,  0,  0},	/* IDSEL 15 - unused */
		{25, 26, 27, 28},	/* IDSEL 16 - PMC Slot 1 */
		{28, 25, 26, 27},	/* IDSEL 17 - PMC Slot 2 */
		{ 0,  0,  0,  0},	/* IDSEL 18 - unused */
		{ 0,  0,  0,  0},	/* IDSEL 19 - unused */
		{20,  0,  0,  0}	/* IDSEL 20 - P2P bridge (to cPCI) */
	};

	const long min_idsel = 14, max_idsel = 20, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

/* Motorola MTX */
static inline int
MTX_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	    /*
	     *      MPIC interrupts for various IDSEL values (MPIC IRQ0 =
	     *      Linux IRQ16 (to leave room for ISA IRQs at 0-15).
	     *      PCI IDSEL/INTPIN->INTLINE
	     *         A   B   C   D
	     */
	{
		{19,  0,  0,  0},	/* IDSEL 12 - SCSI   */
		{ 0,  0,  0,  0},	/* IDSEL 13 - unused */
		{18,  0,  0,  0},	/* IDSEL 14 - Enet   */
		{ 0,  0,  0,  0},	/* IDSEL 15 - unused */
		{25, 26, 27, 28},	/* IDSEL 16 - PMC Slot 1 */
		{26, 27, 28, 25},	/* IDSEL 17 - PMC Slot 2 */
		{27, 28, 25, 26}	/* IDSEL 18 - PCI Slot 3 */
	};

	const long min_idsel = 12, max_idsel = 18, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

/* Motorola MTX Plus */
/* Secondary bus interrupt routing is not supported yet */
static inline int
MTXplus_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	static char pci_irq_table[][4] =
	    /*
	     *      MPIC interrupts for various IDSEL values (MPIC IRQ0 =
	     *      Linux IRQ16 (to leave room for ISA IRQs at 0-15).
	     *      PCI IDSEL/INTPIN->INTLINE
	     *         A   B   C   D
	     */
	{
		{19,  0,  0,  0},	/* IDSEL 12 - SCSI   */
		{ 0,  0,  0,  0},	/* IDSEL 13 - unused */
		{18,  0,  0,  0},	/* IDSEL 14 - Enet 1 */
		{ 0,  0,  0,  0},	/* IDSEL 15 - unused */
		{25, 26, 27, 28},	/* IDSEL 16 - PCI Slot 1P */
		{26, 27, 28, 25},	/* IDSEL 17 - PCI Slot 2P */
		{27, 28, 25, 26},	/* IDSEL 18 - PCI Slot 3P */
		{26,  0,  0,  0},	/* IDSEL 19 - Enet 2 */
		{ 0,  0,  0,  0}	/* IDSEL 20 - P2P Bridge */
	};

	const long min_idsel = 12, max_idsel = 20, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

static inline int
Genesis2_map_irq(struct pci_dev *dev, unsigned char idsel, unsigned char pin)
{
	/* 2600
	 * Raven 31
	 * ISA   11
	 * SCSI  12 - IRQ3
	 * Univ  13
	 * eth   14 - IRQ2
	 * VGA   15 - IRQ4
	 * PMC1  16 - IRQ9,10,11,12 = PMC1 A-D
	 * PMC2  17 - IRQ12,9,10,11 = A-D
	 * SCSI2 18 - IRQ11
	 * eth2  19 - IRQ10
	 * PCIX  20 - IRQ9,10,11,12 = PCI A-D
	 */

	/* 2400
	 * Hawk 31
	 * ISA  11
	 * Univ 13
	 * eth  14 - IRQ2
	 * PMC1 16 - IRQ9,10,11,12 = PMC A-D
	 * PMC2 17 - IRQ12,9,10,11 = PMC A-D
	 * PCIX 20 - IRQ9,10,11,12 = PMC A-D
	 */

	/* 2300
	 * Raven 31
	 * ISA   11
	 * Univ  13
	 * eth   14 - IRQ2
	 * PMC1  16 - 9,10,11,12 = A-D
	 * PMC2  17 - 9,10,11,12 = B,C,D,A
	 */

	static char pci_irq_table[][4] =
	    /*
	     *      MPIC interrupts for various IDSEL values (MPIC IRQ0 =
	     *      Linux IRQ16 (to leave room for ISA IRQs at 0-15).
	     *      PCI IDSEL/INTPIN->INTLINE
	     *         A   B   C   D
	     */
	{
		{19,  0,  0,  0},	/* IDSEL 12 - SCSI   */
		{ 0,  0,  0,  0},	/* IDSEL 13 - Universe PCI - VME */
		{18,  0,  0,  0},	/* IDSEL 14 - Enet 1 */
		{ 0,  0,  0,  0},	/* IDSEL 15 - unused */
		{25, 26, 27, 28},	/* IDSEL 16 - PCI/PMC Slot 1P */
		{28, 25, 26, 27},	/* IDSEL 17 - PCI/PMC Slot 2P */
		{27, 28, 25, 26},	/* IDSEL 18 - PCI Slot 3P */
		{26,  0,  0,  0},	/* IDSEL 19 - Enet 2 */
		{25, 26, 27, 28}	/* IDSEL 20 - P2P Bridge */
	};

	const long min_idsel = 12, max_idsel = 20, irqs_per_slot = 4;
	return PCI_IRQ_TABLE_LOOKUP;
}

#define MOTOROLA_CPUTYPE_REG	0x800
#define MOTOROLA_BASETYPE_REG	0x803
#define MPIC_RAVEN_ID		0x48010000
#define	MPIC_HAWK_ID		0x48030000
#define	MOT_PROC2_BIT		0x800

static u_char pplus_openpic_initsenses[] __initdata = {
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_POSITIVE), /* MVME2600_INT_SIO */
	(IRQ_SENSE_EDGE | IRQ_POLARITY_NEGATIVE),/*MVME2600_INT_FALCN_ECC_ERR */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/*MVME2600_INT_PCI_ETHERNET */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_SCSI */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),/*MVME2600_INT_PCI_GRAPHICS */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_VME0 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_VME1 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_VME2 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_VME3 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_INTA */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_INTB */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_INTC */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_PCI_INTD */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_LM_SIG0 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE), /* MVME2600_INT_LM_SIG1 */
	(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE),
};

int mot_entry = -1;
int prep_keybd_present = 1;
int mot_multi = 0;

struct brd_info {
	/* 0x100 mask assumes for Raven and Hawk boards that the level/edge
	 * are set */
	int cpu_type;
	/* 0x200 if this board has a Hawk chip. */
	int base_type;
	/* or'ed with 0x80 if this board should be checked for multi CPU */
	int max_cpu;
	const char *name;
	int (*map_irq) (struct pci_dev *, unsigned char, unsigned char);
};
struct brd_info mot_info[] = {
	{0x300, 0x00, 0x00, "MVME 2400", Genesis2_map_irq},
	{0x1E0, 0xE0, 0x00, "Mesquite cPCI (MCP750)", mesquite_map_irq},
	{0x1E0, 0xE1, 0x00, "Sitka cPCI (MCPN750)", sitka_map_irq},
	{0x1E0, 0xE2, 0x00, "Mesquite cPCI (MCP750) w/ HAC", mesquite_map_irq},
	{0x1E0, 0xF6, 0x80, "MTX Plus", MTXplus_map_irq},
	{0x1E0, 0xF6, 0x81, "Dual MTX Plus", MTXplus_map_irq},
	{0x1E0, 0xF7, 0x80, "MTX wo/ Parallel Port", MTX_map_irq},
	{0x1E0, 0xF7, 0x81, "Dual MTX wo/ Parallel Port", MTX_map_irq},
	{0x1E0, 0xF8, 0x80, "MTX w/ Parallel Port", MTX_map_irq},
	{0x1E0, 0xF8, 0x81, "Dual MTX w/ Parallel Port", MTX_map_irq},
	{0x1E0, 0xF9, 0x00, "MVME 2300", Genesis2_map_irq},
	{0x1E0, 0xFA, 0x00, "MVME 2300SC/2600", Genesis2_map_irq},
	{0x1E0, 0xFB, 0x00, "MVME 2600 with MVME712M", Genesis2_map_irq},
	{0x1E0, 0xFC, 0x00, "MVME 2600/2700 with MVME761", Genesis2_map_irq},
	{0x1E0, 0xFD, 0x80, "MVME 3600 with MVME712M", Genesis2_map_irq},
	{0x1E0, 0xFD, 0x81, "MVME 4600 with MVME712M", Genesis2_map_irq},
	{0x1E0, 0xFE, 0x80, "MVME 3600 with MVME761", Genesis2_map_irq},
	{0x1E0, 0xFE, 0x81, "MVME 4600 with MVME761", Genesis2_map_irq},
	{0x000, 0x00, 0x00, "", NULL}
};

void __init pplus_set_board_type(void)
{
	unsigned char cpu_type;
	unsigned char base_mod;
	int entry;
	unsigned short devid;
	unsigned long *ProcInfo = NULL;

	cpu_type = inb(MOTOROLA_CPUTYPE_REG) & 0xF0;
	base_mod = inb(MOTOROLA_BASETYPE_REG);
	early_read_config_word(0, 0, 0, PCI_VENDOR_ID, &devid);

	for (entry = 0; mot_info[entry].cpu_type != 0; entry++) {
		/* Check for Hawk chip */
		if (mot_info[entry].cpu_type & 0x200) {
			if (devid != PCI_DEVICE_ID_MOTOROLA_HAWK)
				continue;
		} else {
			/* store the system config register for later use. */
			ProcInfo =
			    (unsigned long *)ioremap(PPLUS_SYS_CONFIG_REG, 4);

			/* Check non hawk boards */
			if ((mot_info[entry].cpu_type & 0xff) != cpu_type)
				continue;

			if (mot_info[entry].base_type == 0) {
				mot_entry = entry;
				break;
			}

			if (mot_info[entry].base_type != base_mod)
				continue;
		}

		if (!(mot_info[entry].max_cpu & 0x80)) {
			mot_entry = entry;
			break;
		}

		/* processor 1 not present and max processor zero indicated */
		if ((*ProcInfo & MOT_PROC2_BIT)
		    && !(mot_info[entry].max_cpu & 0x7f)) {
			mot_entry = entry;
			break;
		}

		/* processor 1 present and max processor zero indicated */
		if (!(*ProcInfo & MOT_PROC2_BIT)
		    && (mot_info[entry].max_cpu & 0x7f)) {
			mot_entry = entry;
			break;
		}

		/* Indicate to system if this is a multiprocessor board */
		if (!(*ProcInfo & MOT_PROC2_BIT))
			mot_multi = 1;
	}

	if (mot_entry == -1)
		/* No particular cpu type found - assume Mesquite (MCP750) */
		mot_entry = 1;

	Motherboard_map_name = (unsigned char *)mot_info[mot_entry].name;
	ppc_md.pci_map_irq = mot_info[mot_entry].map_irq;
}
void __init pplus_pib_init(void)
{
	unsigned char reg;
	unsigned short short_reg;

	struct pci_dev *dev = NULL;

	/*
	 * Perform specific configuration for the Via Tech or
	 * or Winbond PCI-ISA-Bridge part.
	 */
	if ((dev = pci_get_device(PCI_VENDOR_ID_VIA,
				   PCI_DEVICE_ID_VIA_82C586_1, dev))) {
		/*
		 * PPCBUG does not set the enable bits
		 * for the IDE device. Force them on here.
		 */
		pci_read_config_byte(dev, 0x40, &reg);

		reg |= 0x03;	/* IDE: Chip Enable Bits */
		pci_write_config_byte(dev, 0x40, reg);
	}

	if ((dev = pci_get_device(PCI_VENDOR_ID_VIA,
				   PCI_DEVICE_ID_VIA_82C586_2,
				   dev)) && (dev->devfn = 0x5a)) {
		/* Force correct USB interrupt */
		dev->irq = 11;
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
	}

	if ((dev = pci_get_device(PCI_VENDOR_ID_WINBOND,
				   PCI_DEVICE_ID_WINBOND_83C553, dev))) {
		/* Clear PCI Interrupt Routing Control Register. */
		short_reg = 0x0000;
		pci_write_config_word(dev, 0x44, short_reg);
		/* Route IDE interrupts to IRQ 14 */
		reg = 0xEE;
		pci_write_config_byte(dev, 0x43, reg);
	}

	if ((dev = pci_get_device(PCI_VENDOR_ID_WINBOND,
				   PCI_DEVICE_ID_WINBOND_82C105, dev))) {
		/*
		 * Disable LEGIRQ mode so PCI INTS are routed
		 * directly to the 8259 and enable both channels
		 */
		pci_write_config_dword(dev, 0x40, 0x10ff0033);

		/* Force correct IDE interrupt */
		dev->irq = 14;
		pci_write_config_byte(dev, PCI_INTERRUPT_LINE, dev->irq);
	}
	pci_dev_put(dev);
}

void __init pplus_set_VIA_IDE_legacy(void)
{
	unsigned short vend, dev;

	early_read_config_word(0, 0, PCI_DEVFN(0xb, 1), PCI_VENDOR_ID, &vend);
	early_read_config_word(0, 0, PCI_DEVFN(0xb, 1), PCI_DEVICE_ID, &dev);

	if ((vend == PCI_VENDOR_ID_VIA) &&
			(dev == PCI_DEVICE_ID_VIA_82C586_1)) {
		unsigned char temp;

		/* put back original "standard" port base addresses */
		early_write_config_dword(0, 0, PCI_DEVFN(0xb, 1),
					 PCI_BASE_ADDRESS_0, 0x1f1);
		early_write_config_dword(0, 0, PCI_DEVFN(0xb, 1),
					 PCI_BASE_ADDRESS_1, 0x3f5);
		early_write_config_dword(0, 0, PCI_DEVFN(0xb, 1),
					 PCI_BASE_ADDRESS_2, 0x171);
		early_write_config_dword(0, 0, PCI_DEVFN(0xb, 1),
					 PCI_BASE_ADDRESS_3, 0x375);
		early_write_config_dword(0, 0, PCI_DEVFN(0xb, 1),
					 PCI_BASE_ADDRESS_4, 0xcc01);

		/* put into legacy mode */
		early_read_config_byte(0, 0, PCI_DEVFN(0xb, 1), PCI_CLASS_PROG,
				       &temp);
		temp &= ~0x05;
		early_write_config_byte(0, 0, PCI_DEVFN(0xb, 1), PCI_CLASS_PROG,
					temp);
	}
}

void pplus_set_VIA_IDE_native(void)
{
	unsigned short vend, dev;

	early_read_config_word(0, 0, PCI_DEVFN(0xb, 1), PCI_VENDOR_ID, &vend);
	early_read_config_word(0, 0, PCI_DEVFN(0xb, 1), PCI_DEVICE_ID, &dev);

	if ((vend == PCI_VENDOR_ID_VIA) &&
			(dev == PCI_DEVICE_ID_VIA_82C586_1)) {
		unsigned char temp;

		/* put into native mode */
		early_read_config_byte(0, 0, PCI_DEVFN(0xb, 1), PCI_CLASS_PROG,
				       &temp);
		temp |= 0x05;
		early_write_config_byte(0, 0, PCI_DEVFN(0xb, 1), PCI_CLASS_PROG,
					temp);
	}
}

void __init pplus_pcibios_fixup(void)
{

	unsigned char reg;
	unsigned short devid;
	unsigned char base_mod;

	printk(KERN_INFO "Setting PCI interrupts for a \"%s\"\n",
			Motherboard_map_name);

	/* Setup the Winbond or Via PIB */
	pplus_pib_init();

	/* Set up floppy in PS/2 mode */
	outb(0x09, SIO_CONFIG_RA);
	reg = inb(SIO_CONFIG_RD);
	reg = (reg & 0x3F) | 0x40;
	outb(reg, SIO_CONFIG_RD);
	outb(reg, SIO_CONFIG_RD);	/* Have to write twice to change! */

	/* This is a hack.  If this is a 2300 or 2400 mot board then there is
	 * no keyboard controller and we have to indicate that.
	 */

	early_read_config_word(0, 0, 0, PCI_VENDOR_ID, &devid);
	base_mod = inb(MOTOROLA_BASETYPE_REG);
	if ((devid == PCI_DEVICE_ID_MOTOROLA_HAWK) ||
	    (base_mod == 0xF9) || (base_mod == 0xFA) || (base_mod == 0xE1))
		prep_keybd_present = 0;
}

void __init pplus_find_bridges(void)
{
	struct pci_controller *hose;

	hose = pcibios_alloc_controller();
	if (!hose)
		return;

	hose->first_busno = 0;
	hose->last_busno = 0xff;

	hose->pci_mem_offset = PREP_ISA_MEM_BASE;
	hose->io_base_virt = (void *)PREP_ISA_IO_BASE;

	pci_init_resource(&hose->io_resource, PPLUS_PCI_IO_START,
			  PPLUS_PCI_IO_END, IORESOURCE_IO, "PCI host bridge");
	pci_init_resource(&hose->mem_resources[0], PPLUS_PROC_PCI_MEM_START,
			  PPLUS_PROC_PCI_MEM_END, IORESOURCE_MEM,
			  "PCI host bridge");

	hose->io_space.start = PPLUS_PCI_IO_START;
	hose->io_space.end = PPLUS_PCI_IO_END;
	hose->mem_space.start = PPLUS_PCI_MEM_START;
	hose->mem_space.end = PPLUS_PCI_MEM_END - HAWK_MPIC_SIZE;

	if (hawk_init(hose, PPLUS_HAWK_PPC_REG_BASE, PPLUS_PROC_PCI_MEM_START,
				PPLUS_PROC_PCI_MEM_END - HAWK_MPIC_SIZE,
				PPLUS_PROC_PCI_IO_START, PPLUS_PROC_PCI_IO_END,
				PPLUS_PROC_PCI_MEM_END - HAWK_MPIC_SIZE + 1)
			!= 0) {
		printk(KERN_CRIT "Could not initialize host bridge\n");

	}

	pplus_set_VIA_IDE_legacy();

	hose->last_busno = pciauto_bus_scan(hose, hose->first_busno);

	ppc_md.pcibios_fixup = pplus_pcibios_fixup;
	ppc_md.pci_swizzle = common_swizzle;
}

static int pplus_show_cpuinfo(struct seq_file *m)
{
	seq_printf(m, "vendor\t\t: Motorola MCG\n");
	seq_printf(m, "machine\t\t: %s\n", Motherboard_map_name);

	return 0;
}

static void __init pplus_setup_arch(void)
{
	struct pci_controller *hose;

	if (ppc_md.progress)
		ppc_md.progress("pplus_setup_arch: enter", 0);

	/* init to some ~sane value until calibrate_delay() runs */
	loops_per_jiffy = 50000000;

	if (ppc_md.progress)
		ppc_md.progress("pplus_setup_arch: find_bridges", 0);

	/* Setup PCI host bridge */
	pplus_find_bridges();

	hose = pci_bus_to_hose(0);
	isa_io_base = (ulong) hose->io_base_virt;

	if (ppc_md.progress)
		ppc_md.progress("pplus_setup_arch: set_board_type", 0);

	pplus_set_board_type();

	/* Enable L2.  Assume we don't need to flush -- Cort */
	*(unsigned char *)(PPLUS_L2_CONTROL_REG) |= 3;

#ifdef CONFIG_BLK_DEV_INITRD
	if (initrd_start)
		ROOT_DEV = Root_RAM0;
	else
#endif
#ifdef CONFIG_ROOT_NFS
		ROOT_DEV = Root_NFS;
#else
		ROOT_DEV = Root_SDA2;
#endif

	printk(KERN_INFO "Motorola PowerPlus Platform\n");
	printk(KERN_INFO
	       "Port by MontaVista Software, Inc. (source@mvista.com)\n");

#ifdef CONFIG_VGA_CONSOLE
	/* remap the VGA memory */
	vgacon_remap_base = (unsigned long)ioremap(PPLUS_ISA_MEM_BASE,
						   0x08000000);
	conswitchp = &vga_con;
#endif
#ifdef CONFIG_PPCBUG_NVRAM
	/* Read in NVRAM data */
	init_prep_nvram();

	/* if no bootargs, look in NVRAM */
	if (cmd_line[0] == '\0') {
		char *bootargs;
		bootargs = prep_nvram_get_var("bootargs");
		if (bootargs != NULL) {
			strcpy(cmd_line, bootargs);
			/* again.. */
			strcpy(boot_command_line, cmd_line);
		}
	}
#endif
	if (ppc_md.progress)
		ppc_md.progress("pplus_setup_arch: exit", 0);
}

static void pplus_restart(char *cmd)
{
	unsigned long i = 10000;

	local_irq_disable();

	/* set VIA IDE controller into native mode */
	pplus_set_VIA_IDE_native();

	/* set exception prefix high - to the prom */
	_nmask_and_or_msr(0, MSR_IP);

	/* make sure bit 0 (reset) is a 0 */
	outb(inb(0x92) & ~1L, 0x92);
	/* signal a reset to system control port A - soft reset */
	outb(inb(0x92) | 1, 0x92);

	while (i != 0)
		i++;
	panic("restart failed\n");
}

static void pplus_halt(void)
{
	/* set exception prefix high - to the prom */
	_nmask_and_or_msr(MSR_EE, MSR_IP);

	/* make sure bit 0 (reset) is a 0 */
	outb(inb(0x92) & ~1L, 0x92);
	/* signal a reset to system control port A - soft reset */
	outb(inb(0x92) | 1, 0x92);

	while (1) ;
	/*
	 * Not reached
	 */
}

static void pplus_power_off(void)
{
	pplus_halt();
}

static void __init pplus_init_IRQ(void)
{
	int i;

	if (ppc_md.progress)
		ppc_md.progress("init_irq: enter", 0);

	OpenPIC_InitSenses = pplus_openpic_initsenses;
	OpenPIC_NumInitSenses = sizeof(pplus_openpic_initsenses);

	if (OpenPIC_Addr != NULL) {

		openpic_set_sources(0, 16, OpenPIC_Addr + 0x10000);
		openpic_init(NUM_8259_INTERRUPTS);
		openpic_hookup_cascade(NUM_8259_INTERRUPTS, "82c59 cascade",
					i8259_irq);
		ppc_md.get_irq = openpic_get_irq;
	}

	i8259_init(0, 0);

	if (ppc_md.progress)
		ppc_md.progress("init_irq: exit", 0);
}

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
/*
 * IDE stuff.
 */
static int pplus_ide_default_irq(unsigned long base)
{
	switch (base) {
	case 0x1f0:
		return 14;
	case 0x170:
		return 15;
	default:
		return 0;
	}
}

static unsigned long pplus_ide_default_io_base(int index)
{
	switch (index) {
	case 0:
		return 0x1f0;
	case 1:
		return 0x170;
	default:
		return 0;
	}
}

static void __init
pplus_ide_init_hwif_ports(hw_regs_t * hw, unsigned long data_port,
			  unsigned long ctrl_port, int *irq)
{
	unsigned long reg = data_port;
	int i;

	for (i = IDE_DATA_OFFSET; i <= IDE_STATUS_OFFSET; i++) {
		hw->io_ports[i] = reg;
		reg += 1;
	}

	if (ctrl_port)
		hw->io_ports[IDE_CONTROL_OFFSET] = ctrl_port;
	else
		hw->io_ports[IDE_CONTROL_OFFSET] =
		    hw->io_ports[IDE_DATA_OFFSET] + 0x206;

	if (irq != NULL)
		*irq = pplus_ide_default_irq(data_port);
}
#endif

#ifdef CONFIG_SMP
/* PowerPlus (MTX) support */
static int __init smp_pplus_probe(void)
{
	extern int mot_multi;

	if (mot_multi) {
		openpic_request_IPIs();
		smp_hw_index[1] = 1;
		return 2;
	}

	return 1;
}

static void __init smp_pplus_kick_cpu(int nr)
{
	*(unsigned long *)KERNELBASE = nr;
	asm volatile ("dcbf 0,%0"::"r" (KERNELBASE):"memory");
	printk(KERN_INFO "CPU1 reset, waiting\n");
}

static void __init smp_pplus_setup_cpu(int cpu_nr)
{
	if (OpenPIC_Addr)
		do_openpic_setup_cpu();
}

static struct smp_ops_t pplus_smp_ops = {
	smp_openpic_message_pass,
	smp_pplus_probe,
	smp_pplus_kick_cpu,
	smp_pplus_setup_cpu,
	.give_timebase = smp_generic_give_timebase,
	.take_timebase = smp_generic_take_timebase,
};
#endif				/* CONFIG_SMP */

#ifdef DUMP_DBATS
static void print_dbat(int idx, u32 bat)
{

	char str[64];

	sprintf(str, "DBAT%c%c = 0x%08x\n",
		(char)((idx - DBAT0U) / 2) + '0', (idx & 1) ? 'L' : 'U', bat);
	ppc_md.progress(str, 0);
}

#define DUMP_DBAT(x) \
	do { \
	u32 __temp = mfspr(x);\
	print_dbat(x, __temp); \
	} while (0)

static void dump_dbats(void)
{
	if (ppc_md.progress) {
		DUMP_DBAT(DBAT0U);
		DUMP_DBAT(DBAT0L);
		DUMP_DBAT(DBAT1U);
		DUMP_DBAT(DBAT1L);
		DUMP_DBAT(DBAT2U);
		DUMP_DBAT(DBAT2L);
		DUMP_DBAT(DBAT3U);
		DUMP_DBAT(DBAT3L);
	}
}
#endif

static unsigned long __init pplus_find_end_of_memory(void)
{
	unsigned long total;

	if (ppc_md.progress)
		ppc_md.progress("pplus_find_end_of_memory", 0);

#ifdef DUMP_DBATS
	dump_dbats();
#endif

	total = hawk_get_mem_size(PPLUS_HAWK_SMC_BASE);
	return (total);
}

static void __init pplus_map_io(void)
{
	io_block_mapping(PPLUS_ISA_IO_BASE, PPLUS_ISA_IO_BASE, 0x10000000,
			 _PAGE_IO);
	io_block_mapping(0xfef80000, 0xfef80000, 0x00080000, _PAGE_IO);
}

static void __init pplus_init2(void)
{
#ifdef CONFIG_NVRAM
	request_region(PREP_NVRAM_AS0, 0x8, "nvram");
#endif
	request_region(0x20, 0x20, "pic1");
	request_region(0xa0, 0x20, "pic2");
	request_region(0x00, 0x20, "dma1");
	request_region(0x40, 0x20, "timer");
	request_region(0x80, 0x10, "dma page reg");
	request_region(0xc0, 0x20, "dma2");
}

/*
 * Set BAT 2 to access 0x8000000 so progress messages will work and set BAT 3
 * to 0xf0000000 to access Falcon/Raven or Hawk registers
 */
static __inline__ void pplus_set_bat(void)
{
	/* wait for all outstanding memory accesses to complete */
	mb();

	/* setup DBATs */
	mtspr(SPRN_DBAT2U, 0x80001ffe);
	mtspr(SPRN_DBAT2L, 0x8000002a);
	mtspr(SPRN_DBAT3U, 0xf0001ffe);
	mtspr(SPRN_DBAT3L, 0xf000002a);

	/* wait for updates */
	mb();
}

void __init
platform_init(unsigned long r3, unsigned long r4, unsigned long r5,
	      unsigned long r6, unsigned long r7)
{
	parse_bootinfo(find_bootinfo());

	/* Map in board regs, etc. */
	pplus_set_bat();

	isa_io_base = PREP_ISA_IO_BASE;
	isa_mem_base = PREP_ISA_MEM_BASE;
	pci_dram_offset = PREP_PCI_DRAM_OFFSET;
	ISA_DMA_THRESHOLD = 0x00ffffff;
	DMA_MODE_READ = 0x44;
	DMA_MODE_WRITE = 0x48;
	ppc_do_canonicalize_irqs = 1;

	ppc_md.setup_arch = pplus_setup_arch;
	ppc_md.show_cpuinfo = pplus_show_cpuinfo;
	ppc_md.init_IRQ = pplus_init_IRQ;
	/* this gets changed later on if we have an OpenPIC -- Cort */
	ppc_md.get_irq = i8259_irq;
	ppc_md.init = pplus_init2;

	ppc_md.restart = pplus_restart;
	ppc_md.power_off = pplus_power_off;
	ppc_md.halt = pplus_halt;

	TODC_INIT(TODC_TYPE_MK48T59, PREP_NVRAM_AS0, PREP_NVRAM_AS1,
		  PREP_NVRAM_DATA, 8);

	ppc_md.time_init = todc_time_init;
	ppc_md.set_rtc_time = todc_set_rtc_time;
	ppc_md.get_rtc_time = todc_get_rtc_time;
	ppc_md.calibrate_decr = todc_calibrate_decr;
	ppc_md.nvram_read_val = todc_m48txx_read_val;
	ppc_md.nvram_write_val = todc_m48txx_write_val;

	ppc_md.find_end_of_memory = pplus_find_end_of_memory;
	ppc_md.setup_io_mappings = pplus_map_io;

#if defined(CONFIG_BLK_DEV_IDE) || defined(CONFIG_BLK_DEV_IDE_MODULE)
	ppc_ide_md.default_irq = pplus_ide_default_irq;
	ppc_ide_md.default_io_base = pplus_ide_default_io_base;
	ppc_ide_md.ide_init_hwif = pplus_ide_init_hwif_ports;
#endif

#ifdef CONFIG_SERIAL_TEXT_DEBUG
	ppc_md.progress = gen550_progress;
#endif				/* CONFIG_SERIAL_TEXT_DEBUG */
#ifdef CONFIG_KGDB
	ppc_md.kgdb_map_scc = gen550_kgdb_map_scc;
#endif
#ifdef CONFIG_SMP
	smp_ops = &pplus_smp_ops;
#endif				/* CONFIG_SMP */
}
