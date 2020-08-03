// SPDX-License-Identifier: GPL-2.0
/*
 *	linux/arch/alpha/kernel/sys_sx164.c
 *
 *	Copyright (C) 1995 David A Rusling
 *	Copyright (C) 1996 Jay A Estabrook
 *	Copyright (C) 1998, 1999, 2000 Richard Henderson
 *
 * Code supporting the SX164 (PCA56+PYXIS).
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/bitops.h>

#include <asm/ptrace.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/core_cia.h>
#include <asm/hwrpb.h>
#include <asm/tlbflush.h>
#include <asm/special_insns.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"


static void __init
sx164_init_irq(void)
{
	outb(0, DMA1_RESET_REG);
	outb(0, DMA2_RESET_REG);
	outb(DMA_MODE_CASCADE, DMA2_MODE_REG);
	outb(0, DMA2_MASK_REG);

	if (alpha_using_srm)
		alpha_mv.device_interrupt = srm_device_interrupt;

	init_i8259a_irqs();

	/* Not interested in the bogus interrupts (0,3,4,5,40-47),
	   NMI (1), or HALT (2).  */
	if (alpha_using_srm)
		init_srm_irqs(40, 0x3f0000);
	else
		init_pyxis_irqs(0xff00003f0000UL);

	if (request_irq(16 + 6, no_action, 0, "timer-cascade", NULL))
		pr_err("Failed to register timer-cascade interrupt\n");
}

/*
 * PCI Fixup configuration.
 *
 * Summary @ PYXIS_INT_REQ:
 * Bit      Meaning
 * 0        RSVD
 * 1        NMI
 * 2        Halt/Reset switch
 * 3        MBZ
 * 4        RAZ
 * 5        RAZ
 * 6        Interval timer (RTC)
 * 7        PCI-ISA Bridge
 * 8        Interrupt Line A from slot 3
 * 9        Interrupt Line A from slot 2
 *10        Interrupt Line A from slot 1
 *11        Interrupt Line A from slot 0
 *12        Interrupt Line B from slot 3
 *13        Interrupt Line B from slot 2
 *14        Interrupt Line B from slot 1
 *15        Interrupt line B from slot 0
 *16        Interrupt Line C from slot 3
 *17        Interrupt Line C from slot 2
 *18        Interrupt Line C from slot 1
 *19        Interrupt Line C from slot 0
 *20        Interrupt Line D from slot 3
 *21        Interrupt Line D from slot 2
 *22        Interrupt Line D from slot 1
 *23        Interrupt Line D from slot 0
 *
 * IdSel       
 *   5  32 bit PCI option slot 2
 *   6  64 bit PCI option slot 0
 *   7  64 bit PCI option slot 1
 *   8  Cypress I/O
 *   9  32 bit PCI option slot 3
 */

static int
sx164_map_irq(const struct pci_dev *dev, u8 slot, u8 pin)
{
	static char irq_tab[5][5] = {
		/*INT    INTA   INTB   INTC   INTD */
		{ 16+ 9, 16+ 9, 16+13, 16+17, 16+21}, /* IdSel 5 slot 2 J17 */
		{ 16+11, 16+11, 16+15, 16+19, 16+23}, /* IdSel 6 slot 0 J19 */
		{ 16+10, 16+10, 16+14, 16+18, 16+22}, /* IdSel 7 slot 1 J18 */
		{    -1,    -1,    -1,	  -1,    -1}, /* IdSel 8 SIO        */
		{ 16+ 8, 16+ 8, 16+12, 16+16, 16+20}  /* IdSel 9 slot 3 J15 */
	};
	const long min_idsel = 5, max_idsel = 9, irqs_per_slot = 5;
	return COMMON_TABLE_LOOKUP;
}

static void __init
sx164_init_pci(void)
{
	cia_init_pci();
	SMC669_Init(0);
}

static void __init
sx164_init_arch(void)
{
	/*
	 * OSF palcode v1.23 forgets to enable PCA56 Motion Video
	 * Instructions. Let's enable it.
	 * We have to check palcode revision because CSERVE interface
	 * is subject to change without notice. For example, it
	 * has been changed completely since v1.16 (found in MILO
	 * distribution). -ink
	 */
	struct percpu_struct *cpu = (struct percpu_struct*)
		((char*)hwrpb + hwrpb->processor_offset);

	if (amask(AMASK_MAX) != 0
	    && alpha_using_srm
	    && (cpu->pal_revision & 0xffff) <= 0x117) {
		__asm__ __volatile__(
		"lda	$16,8($31)\n"
		"call_pal 9\n"		/* Allow PALRES insns in kernel mode */
		".long  0x64000118\n\n"	/* hw_mfpr $0,icsr */
		"ldah	$16,(1<<(19-16))($31)\n"
		"or	$0,$16,$0\n"	/* set MVE bit */
		".long  0x74000118\n"	/* hw_mtpr $0,icsr */
		"lda	$16,9($31)\n"
		"call_pal 9"		/* Disable PALRES insns */
		: : : "$0", "$16");
		printk("PCA56 MVI set enabled\n");
	}

	pyxis_init_arch();
}

/*
 * The System Vector
 */

struct alpha_machine_vector sx164_mv __initmv = {
	.vector_name		= "SX164",
	DO_EV5_MMU,
	DO_DEFAULT_RTC,
	DO_PYXIS_IO,
	.machine_check		= cia_machine_check,
	.max_isa_dma_address	= ALPHA_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= DEFAULT_MEM_BASE,
	.pci_dac_offset		= PYXIS_DAC_OFFSET,

	.nr_irqs		= 48,
	.device_interrupt	= pyxis_device_interrupt,

	.init_arch		= sx164_init_arch,
	.init_irq		= sx164_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= sx164_init_pci,
	.kill_arch		= cia_kill_arch,
	.pci_map_irq		= sx164_map_irq,
	.pci_swizzle		= common_swizzle,
};
ALIAS_MV(sx164)
