/*
 *	linux/arch/alpha/kernel/sys_sio.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998, 1999 Richard Henderson
 *
 * Code for all boards that route the PCI interrupts through the SIO
 * PCI/ISA bridge.  This includes Noname (AXPpci33), Multia (UDB),
 * Kenetics's Platform 2000, Avanti (AlphaStation), XL, and AlphaBook1.
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/screen_info.h>

#include <asm/compiler.h>
#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_apecs.h>
#include <asm/core_lca.h>
#include <asm/tlbflush.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"
#include "pc873xx.h"

#if defined(ALPHA_RESTORE_SRM_SETUP)
/* Save LCA configuration data as the console had it set up.  */
struct 
{
	unsigned int orig_route_tab; /* for SAVE/RESTORE */
} saved_config __attribute((common));
#endif


static void __init
sio_init_irq(void)
{
	if (alpha_using_srm)
		alpha_mv.device_interrupt = srm_device_interrupt;

	init_i8259a_irqs();
	common_init_isa_dma();
}

static inline void __init
alphabook1_init_arch(void)
{
	/* The AlphaBook1 has LCD video fixed at 800x600,
	   37 rows and 100 cols. */
	screen_info.orig_y = 37;
	screen_info.orig_video_cols = 100;
	screen_info.orig_video_lines = 37;

	lca_init_arch();
}


/*
 * sio_route_tab selects irq routing in PCI/ISA bridge so that:
 *		PIRQ0 -> irq 15
 *		PIRQ1 -> irq  9
 *		PIRQ2 -> irq 10
 *		PIRQ3 -> irq 11
 *
 * This probably ought to be configurable via MILO.  For
 * example, sound boards seem to like using IRQ 9.
 *
 * This is NOT how we should do it. PIRQ0-X should have
 * their own IRQs, the way intel uses the IO-APIC IRQs.
 */

static void __init
sio_pci_route(void)
{
	unsigned int orig_route_tab;

	/* First, ALWAYS read and print the original setting. */
	pci_bus_read_config_dword(pci_isa_hose->bus, PCI_DEVFN(7, 0), 0x60,
				  &orig_route_tab);
	printk("%s: PIRQ original 0x%x new 0x%x\n", __func__,
	       orig_route_tab, alpha_mv.sys.sio.route_tab);

#if defined(ALPHA_RESTORE_SRM_SETUP)
	saved_config.orig_route_tab = orig_route_tab;
#endif

	/* Now override with desired setting. */
	pci_bus_write_config_dword(pci_isa_hose->bus, PCI_DEVFN(7, 0), 0x60,
				   alpha_mv.sys.sio.route_tab);
}

static unsigned int __init
sio_collect_irq_levels(void)
{
	unsigned int level_bits = 0;
	struct pci_dev *dev = NULL;

	/* Iterate through the devices, collecting IRQ levels.  */
	for_each_pci_dev(dev) {
		if ((dev->class >> 16 == PCI_BASE_CLASS_BRIDGE) &&
		    (dev->class >> 8 != PCI_CLASS_BRIDGE_PCMCIA))
			continue;

		if (dev->irq)
			level_bits |= (1 << dev->irq);
	}
	return level_bits;
}

static void __init
sio_fixup_irq_levels(unsigned int level_bits)
{
	unsigned int old_level_bits;

	/*
	 * Now, make all PCI interrupts level sensitive.  Notice:
	 * these registers must be accessed byte-wise.  inw()/outw()
	 * don't work.
	 *
	 * Make sure to turn off any level bits set for IRQs 9,10,11,15,
	 *  so that the only bits getting set are for devices actually found.
	 * Note that we do preserve the remainder of the bits, which we hope
	 *  will be set correctly by ARC/SRM.
	 *
	 * Note: we at least preserve any level-set bits on AlphaBook1
	 */
	old_level_bits = inb(0x4d0) | (inb(0x4d1) << 8);

	level_bits |= (old_level_bits & 0x71ff);

	outb((level_bits >> 0) & 0xff, 0x4d0);
	outb((level_bits >> 8) & 0xff, 0x4d1);
}

static inline int __init
noname_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	/*
	 * The Noname board has 5 PCI slots with each of the 4
	 * interrupt pins routed to different pins on the PCI/ISA
	 * bridge (PIRQ0-PIRQ3).  The table below is based on
	 * information available at:
	 *
	 *   http://ftp.digital.com/pub/DEC/axppci/ref_interrupts.txt
	 *
	 * I have no information on the Avanti interrupt routing, but
	 * the routing seems to be identical to the Noname except
	 * that the Avanti has an additional slot whose routing I'm
	 * unsure of.
	 *
	 * pirq_tab[0] is a fake entry to deal with old PCI boards
	 * that have the interrupt pin number hardwired to 0 (meaning
	 * that they use the default INTA line, if they are interrupt
	 * driven at all).
	 */
	static char irq_tab[][5] __initdata = {
		/*INT A   B   C   D */
		{ 3,  3,  3,  3,  3}, /* idsel  6 (53c810) */ 
		{-1, -1, -1, -1, -1}, /* idsel  7 (SIO: PCI/ISA bridge) */
		{ 2,  2, -1, -1, -1}, /* idsel  8 (Hack: slot closest ISA) */
		{-1, -1, -1, -1, -1}, /* idsel  9 (unused) */
		{-1, -1, -1, -1, -1}, /* idsel 10 (unused) */
		{ 0,  0,  2,  1,  0}, /* idsel 11 KN25_PCI_SLOT0 */
		{ 1,  1,  0,  2,  1}, /* idsel 12 KN25_PCI_SLOT1 */
		{ 2,  2,  1,  0,  2}, /* idsel 13 KN25_PCI_SLOT2 */
		{ 0,  0,  0,  0,  0}, /* idsel 14 AS255 TULIP */
	};
	const long min_idsel = 6, max_idsel = 14, irqs_per_slot = 5;
	int irq = COMMON_TABLE_LOOKUP, tmp;
	tmp = __kernel_extbl(alpha_mv.sys.sio.route_tab, irq);
	return irq >= 0 ? tmp : -1;
}

static inline int __init
p2k_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	static char irq_tab[][5] __initdata = {
		/*INT A   B   C   D */
		{ 0,  0, -1, -1, -1}, /* idsel  6 (53c810) */
		{-1, -1, -1, -1, -1}, /* idsel  7 (SIO: PCI/ISA bridge) */
		{ 1,  1,  2,  3,  0}, /* idsel  8 (slot A) */
		{ 2,  2,  3,  0,  1}, /* idsel  9 (slot B) */
		{-1, -1, -1, -1, -1}, /* idsel 10 (unused) */
		{-1, -1, -1, -1, -1}, /* idsel 11 (unused) */
		{ 3,  3, -1, -1, -1}, /* idsel 12 (CMD0646) */
	};
	const long min_idsel = 6, max_idsel = 12, irqs_per_slot = 5;
	int irq = COMMON_TABLE_LOOKUP, tmp;
	tmp = __kernel_extbl(alpha_mv.sys.sio.route_tab, irq);
	return irq >= 0 ? tmp : -1;
}

static inline void __init
noname_init_pci(void)
{
	common_init_pci();
	sio_pci_route();
	sio_fixup_irq_levels(sio_collect_irq_levels());

	if (pc873xx_probe() == -1) {
		printk(KERN_ERR "Probing for PC873xx Super IO chip failed.\n");
	} else {
		printk(KERN_INFO "Found %s Super IO chip at 0x%x\n",
			pc873xx_get_model(), pc873xx_get_base());

		/* Enabling things in the Super IO chip doesn't actually
		 * configure and enable things, the legacy drivers still
		 * need to do the actual configuration and enabling.
		 * This only unblocks them.
		 */

#if !defined(CONFIG_ALPHA_AVANTI)
		/* Don't bother on the Avanti family.
		 * None of them had on-board IDE.
		 */
		pc873xx_enable_ide();
#endif
		pc873xx_enable_epp19();
	}
}

static inline void __init
alphabook1_init_pci(void)
{
	struct pci_dev *dev;
	unsigned char orig, config;

	common_init_pci();
	sio_pci_route();

	/*
	 * On the AlphaBook1, the PCMCIA chip (Cirrus 6729)
	 * is sensitive to PCI bus bursts, so we must DISABLE
	 * burst mode for the NCR 8xx SCSI... :-(
	 *
	 * Note that the NCR810 SCSI driver must preserve the
	 * setting of the bit in order for this to work.  At the
	 * moment (2.0.29), ncr53c8xx.c does NOT do this, but
	 * 53c7,8xx.c DOES.
	 */

	dev = NULL;
	while ((dev = pci_get_device(PCI_VENDOR_ID_NCR, PCI_ANY_ID, dev))) {
		if (dev->device == PCI_DEVICE_ID_NCR_53C810
		    || dev->device == PCI_DEVICE_ID_NCR_53C815
		    || dev->device == PCI_DEVICE_ID_NCR_53C820
		    || dev->device == PCI_DEVICE_ID_NCR_53C825) {
			unsigned long io_port;
			unsigned char ctest4;

			io_port = dev->resource[0].start;
			ctest4 = inb(io_port+0x21);
			if (!(ctest4 & 0x80)) {
				printk("AlphaBook1 NCR init: setting"
				       " burst disable\n");
				outb(ctest4 | 0x80, io_port+0x21);
			}
                }
	}

	/* Do not set *ANY* level triggers for AlphaBook1. */
	sio_fixup_irq_levels(0);

	/* Make sure that register PR1 indicates 1Mb mem */
	outb(0x0f, 0x3ce); orig = inb(0x3cf);   /* read PR5  */
	outb(0x0f, 0x3ce); outb(0x05, 0x3cf);   /* unlock PR0-4 */
	outb(0x0b, 0x3ce); config = inb(0x3cf); /* read PR1 */
	if ((config & 0xc0) != 0xc0) {
		printk("AlphaBook1 VGA init: setting 1Mb memory\n");
		config |= 0xc0;
		outb(0x0b, 0x3ce); outb(config, 0x3cf); /* write PR1 */
	}
	outb(0x0f, 0x3ce); outb(orig, 0x3cf); /* (re)lock PR0-4 */
}

void
sio_kill_arch(int mode)
{
#if defined(ALPHA_RESTORE_SRM_SETUP)
	/* Since we cannot read the PCI DMA Window CSRs, we
	 * cannot restore them here.
	 *
	 * However, we CAN read the PIRQ route register, so restore it
	 * now...
	 */
 	pci_bus_write_config_dword(pci_isa_hose->bus, PCI_DEVFN(7, 0), 0x60,
				   saved_config.orig_route_tab);
#endif
}


/*
 * The System Vectors
 */

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_BOOK1)
struct alpha_machine_vector alphabook1_mv __initmv = {
	.vector_name		= "AlphaBook1",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_LCA_IO,
	.machine_check		= lca_machine_check,
	.max_isa_dma_address	= ALPHA_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= APECS_AND_LCA_DEFAULT_MEM_BASE,

	.nr_irqs		= 16,
	.device_interrupt	= isa_device_interrupt,

	.init_arch		= alphabook1_init_arch,
	.init_irq		= sio_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= alphabook1_init_pci,
	.kill_arch		= sio_kill_arch,
	.pci_map_irq		= noname_map_irq,
	.pci_swizzle		= common_swizzle,

	.sys = { .sio = {
		/* NCR810 SCSI is 14, PCMCIA controller is 15.  */
		.route_tab	= 0x0e0f0a0a,
	}}
};
ALIAS_MV(alphabook1)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_AVANTI)
struct alpha_machine_vector avanti_mv __initmv = {
	.vector_name		= "Avanti",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_APECS_IO,
	.machine_check		= apecs_machine_check,
	.max_isa_dma_address	= ALPHA_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= APECS_AND_LCA_DEFAULT_MEM_BASE,

	.nr_irqs		= 16,
	.device_interrupt	= isa_device_interrupt,

	.init_arch		= apecs_init_arch,
	.init_irq		= sio_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= noname_init_pci,
	.kill_arch		= sio_kill_arch,
	.pci_map_irq		= noname_map_irq,
	.pci_swizzle		= common_swizzle,

	.sys = { .sio = {
		.route_tab	= 0x0b0a050f, /* leave 14 for IDE, 9 for SND */
	}}
};
ALIAS_MV(avanti)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_NONAME)
struct alpha_machine_vector noname_mv __initmv = {
	.vector_name		= "Noname",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_LCA_IO,
	.machine_check		= lca_machine_check,
	.max_isa_dma_address	= ALPHA_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= APECS_AND_LCA_DEFAULT_MEM_BASE,

	.nr_irqs		= 16,
	.device_interrupt	= srm_device_interrupt,

	.init_arch		= lca_init_arch,
	.init_irq		= sio_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= noname_init_pci,
	.kill_arch		= sio_kill_arch,
	.pci_map_irq		= noname_map_irq,
	.pci_swizzle		= common_swizzle,

	.sys = { .sio = {
		/* For UDB, the only available PCI slot must not map to IRQ 9,
		   since that's the builtin MSS sound chip. That PCI slot
		   will map to PIRQ1 (for INTA at least), so we give it IRQ 15
		   instead.

		   Unfortunately we have to do this for NONAME as well, since
		   they are co-indicated when the platform type "Noname" is
		   selected... :-(  */

		.route_tab	= 0x0b0a0f0d,
	}}
};
ALIAS_MV(noname)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_P2K)
struct alpha_machine_vector p2k_mv __initmv = {
	.vector_name		= "Platform2000",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_LCA_IO,
	.machine_check		= lca_machine_check,
	.max_isa_dma_address	= ALPHA_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= APECS_AND_LCA_DEFAULT_MEM_BASE,

	.nr_irqs		= 16,
	.device_interrupt	= srm_device_interrupt,

	.init_arch		= lca_init_arch,
	.init_irq		= sio_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= noname_init_pci,
	.kill_arch		= sio_kill_arch,
	.pci_map_irq		= p2k_map_irq,
	.pci_swizzle		= common_swizzle,

	.sys = { .sio = {
		.route_tab	= 0x0b0a090f,
	}}
};
ALIAS_MV(p2k)
#endif

#if defined(CONFIG_ALPHA_GENERIC) || defined(CONFIG_ALPHA_XL)
struct alpha_machine_vector xl_mv __initmv = {
	.vector_name		= "XL",
	DO_EV4_MMU,
	DO_DEFAULT_RTC,
	DO_APECS_IO,
	.machine_check		= apecs_machine_check,
	.max_isa_dma_address	= ALPHA_XL_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= XL_DEFAULT_MEM_BASE,

	.nr_irqs		= 16,
	.device_interrupt	= isa_device_interrupt,

	.init_arch		= apecs_init_arch,
	.init_irq		= sio_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= noname_init_pci,
	.kill_arch		= sio_kill_arch,
	.pci_map_irq		= noname_map_irq,
	.pci_swizzle		= common_swizzle,

	.sys = { .sio = {
		.route_tab	= 0x0b0a090f,
	}}
};
ALIAS_MV(xl)
#endif
