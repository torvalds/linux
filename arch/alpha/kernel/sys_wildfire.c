/*
 *  linux/arch/alpha/kernel/sys_wildfire.c
 *
 *  Wildfire support.
 *
 *  Copyright (C) 2000 Andrea Arcangeli <andrea@suse.de> SuSE
 */

#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/mm.h>
#include <linux/sched.h>
#include <linux/pci.h>
#include <linux/init.h>
#include <linux/bitops.h>

#include <asm/ptrace.h>
#include <asm/system.h>
#include <asm/dma.h>
#include <asm/irq.h>
#include <asm/mmu_context.h>
#include <asm/io.h>
#include <asm/pgtable.h>
#include <asm/core_wildfire.h>
#include <asm/hwrpb.h>
#include <asm/tlbflush.h>

#include "proto.h"
#include "irq_impl.h"
#include "pci_impl.h"
#include "machvec_impl.h"

static unsigned long cached_irq_mask[WILDFIRE_NR_IRQS/(sizeof(long)*8)];

DEFINE_SPINLOCK(wildfire_irq_lock);

static int doing_init_irq_hw = 0;

static void
wildfire_update_irq_hw(unsigned int irq)
{
	int qbbno = (irq >> 8) & (WILDFIRE_MAX_QBB - 1);
	int pcano = (irq >> 6) & (WILDFIRE_PCA_PER_QBB - 1);
	wildfire_pca *pca;
	volatile unsigned long * enable0;

	if (!WILDFIRE_PCA_EXISTS(qbbno, pcano)) {
		if (!doing_init_irq_hw) {
			printk(KERN_ERR "wildfire_update_irq_hw:"
			       " got irq %d for non-existent PCA %d"
			       " on QBB %d.\n",
			       irq, pcano, qbbno);
		}
		return;
	}

	pca = WILDFIRE_pca(qbbno, pcano);
	enable0 = (unsigned long *) &pca->pca_int[0].enable; /* ??? */

	*enable0 = cached_irq_mask[qbbno * WILDFIRE_PCA_PER_QBB + pcano];
	mb();
	*enable0;
}

static void __init
wildfire_init_irq_hw(void)
{
#if 0
	register wildfire_pca * pca = WILDFIRE_pca(0, 0);
	volatile unsigned long * enable0, * enable1, * enable2, *enable3;
	volatile unsigned long * target0, * target1, * target2, *target3;

	enable0 = (unsigned long *) &pca->pca_int[0].enable;
	enable1 = (unsigned long *) &pca->pca_int[1].enable;
	enable2 = (unsigned long *) &pca->pca_int[2].enable;
	enable3 = (unsigned long *) &pca->pca_int[3].enable;

	target0 = (unsigned long *) &pca->pca_int[0].target;
	target1 = (unsigned long *) &pca->pca_int[1].target;
	target2 = (unsigned long *) &pca->pca_int[2].target;
	target3 = (unsigned long *) &pca->pca_int[3].target;

	*enable0 = *enable1 = *enable2 = *enable3 = 0;

	*target0 = (1UL<<8) | WILDFIRE_QBB(0);
	*target1 = *target2 = *target3 = 0;

	mb();

	*enable0; *enable1; *enable2; *enable3;
	*target0; *target1; *target2; *target3;

#else
	int i;

	doing_init_irq_hw = 1;

	/* Need to update only once for every possible PCA. */
	for (i = 0; i < WILDFIRE_NR_IRQS; i+=WILDFIRE_IRQ_PER_PCA)
		wildfire_update_irq_hw(i);

	doing_init_irq_hw = 0;
#endif
}

static void
wildfire_enable_irq(unsigned int irq)
{
	if (irq < 16)
		i8259a_enable_irq(irq);

	spin_lock(&wildfire_irq_lock);
	set_bit(irq, &cached_irq_mask);
	wildfire_update_irq_hw(irq);
	spin_unlock(&wildfire_irq_lock);
}

static void
wildfire_disable_irq(unsigned int irq)
{
	if (irq < 16)
		i8259a_disable_irq(irq);

	spin_lock(&wildfire_irq_lock);
	clear_bit(irq, &cached_irq_mask);
	wildfire_update_irq_hw(irq);
	spin_unlock(&wildfire_irq_lock);
}

static void
wildfire_mask_and_ack_irq(unsigned int irq)
{
	if (irq < 16)
		i8259a_mask_and_ack_irq(irq);

	spin_lock(&wildfire_irq_lock);
	clear_bit(irq, &cached_irq_mask);
	wildfire_update_irq_hw(irq);
	spin_unlock(&wildfire_irq_lock);
}

static unsigned int
wildfire_startup_irq(unsigned int irq)
{ 
	wildfire_enable_irq(irq);
	return 0; /* never anything pending */
}

static void
wildfire_end_irq(unsigned int irq)
{ 
#if 0
	if (!irq_desc[irq].action)
		printk("got irq %d\n", irq);
#endif
	if (!(irq_desc[irq].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		wildfire_enable_irq(irq);
}

static struct irq_chip wildfire_irq_type = {
	.name		= "WILDFIRE",
	.startup	= wildfire_startup_irq,
	.shutdown	= wildfire_disable_irq,
	.enable		= wildfire_enable_irq,
	.disable	= wildfire_disable_irq,
	.ack		= wildfire_mask_and_ack_irq,
	.end		= wildfire_end_irq,
};

static void __init
wildfire_init_irq_per_pca(int qbbno, int pcano)
{
	int i, irq_bias;
	unsigned long io_bias;
	static struct irqaction isa_enable = {
		.handler	= no_action,
		.name		= "isa_enable",
	};

	irq_bias = qbbno * (WILDFIRE_PCA_PER_QBB * WILDFIRE_IRQ_PER_PCA)
		 + pcano * WILDFIRE_IRQ_PER_PCA;

	/* Only need the following for first PCI bus per PCA. */
	io_bias = WILDFIRE_IO(qbbno, pcano<<1) - WILDFIRE_IO_BIAS;

#if 0
	outb(0, DMA1_RESET_REG + io_bias);
	outb(0, DMA2_RESET_REG + io_bias);
	outb(DMA_MODE_CASCADE, DMA2_MODE_REG + io_bias);
	outb(0, DMA2_MASK_REG + io_bias);
#endif

#if 0
	/* ??? Not sure how to do this, yet... */
	init_i8259a_irqs(); /* ??? */
#endif

	for (i = 0; i < 16; ++i) {
		if (i == 2)
			continue;
		irq_desc[i+irq_bias].status = IRQ_DISABLED | IRQ_LEVEL;
		irq_desc[i+irq_bias].chip = &wildfire_irq_type;
	}

	irq_desc[36+irq_bias].status = IRQ_DISABLED | IRQ_LEVEL;
	irq_desc[36+irq_bias].chip = &wildfire_irq_type;
	for (i = 40; i < 64; ++i) {
		irq_desc[i+irq_bias].status = IRQ_DISABLED | IRQ_LEVEL;
		irq_desc[i+irq_bias].chip = &wildfire_irq_type;
	}

	setup_irq(32+irq_bias, &isa_enable);	
}

static void __init
wildfire_init_irq(void)
{
	int qbbno, pcano;

#if 1
	wildfire_init_irq_hw();
	init_i8259a_irqs();
#endif

	for (qbbno = 0; qbbno < WILDFIRE_MAX_QBB; qbbno++) {
	  if (WILDFIRE_QBB_EXISTS(qbbno)) {
	    for (pcano = 0; pcano < WILDFIRE_PCA_PER_QBB; pcano++) {
	      if (WILDFIRE_PCA_EXISTS(qbbno, pcano)) {
		wildfire_init_irq_per_pca(qbbno, pcano);
	      }
	    }
	  }
	}
}

static void 
wildfire_device_interrupt(unsigned long vector)
{
	int irq;

	irq = (vector - 0x800) >> 4;

	/*
	 * bits 10-8:	source QBB ID
	 * bits 7-6:	PCA
	 * bits 5-0:	irq in PCA
	 */

	handle_irq(irq);
	return;
}

/*
 * PCI Fixup configuration.
 *
 * Summary per PCA (2 PCI or HIPPI buses):
 *
 * Bit      Meaning
 * 0-15     ISA
 *
 *32        ISA summary
 *33        SMI
 *34        NMI
 *36        builtin QLogic SCSI (or slot 0 if no IO module)
 *40        Interrupt Line A from slot 2 PCI0
 *41        Interrupt Line B from slot 2 PCI0
 *42        Interrupt Line C from slot 2 PCI0
 *43        Interrupt Line D from slot 2 PCI0
 *44        Interrupt Line A from slot 3 PCI0
 *45        Interrupt Line B from slot 3 PCI0
 *46        Interrupt Line C from slot 3 PCI0
 *47        Interrupt Line D from slot 3 PCI0
 *
 *48        Interrupt Line A from slot 4 PCI1
 *49        Interrupt Line B from slot 4 PCI1
 *50        Interrupt Line C from slot 4 PCI1
 *51        Interrupt Line D from slot 4 PCI1
 *52        Interrupt Line A from slot 5 PCI1
 *53        Interrupt Line B from slot 5 PCI1
 *54        Interrupt Line C from slot 5 PCI1
 *55        Interrupt Line D from slot 5 PCI1
 *56        Interrupt Line A from slot 6 PCI1
 *57        Interrupt Line B from slot 6 PCI1
 *58        Interrupt Line C from slot 6 PCI1
 *50        Interrupt Line D from slot 6 PCI1
 *60        Interrupt Line A from slot 7 PCI1
 *61        Interrupt Line B from slot 7 PCI1
 *62        Interrupt Line C from slot 7 PCI1
 *63        Interrupt Line D from slot 7 PCI1
 * 
 *
 * IdSel	
 *   0	 Cypress Bridge I/O (ISA summary interrupt)
 *   1	 64 bit PCI 0 option slot 1 (SCSI QLogic builtin)
 *   2	 64 bit PCI 0 option slot 2
 *   3	 64 bit PCI 0 option slot 3
 *   4	 64 bit PCI 1 option slot 4
 *   5	 64 bit PCI 1 option slot 5
 *   6	 64 bit PCI 1 option slot 6
 *   7	 64 bit PCI 1 option slot 7
 */

static int __init
wildfire_map_irq(struct pci_dev *dev, u8 slot, u8 pin)
{
	static char irq_tab[8][5] __initdata = {
		/*INT    INTA   INTB   INTC   INTD */
		{ -1,    -1,    -1,    -1,    -1}, /* IdSel 0 ISA Bridge */
		{ 36,    36,    36+1, 36+2, 36+3}, /* IdSel 1 SCSI builtin */
		{ 40,    40,    40+1, 40+2, 40+3}, /* IdSel 2 PCI 0 slot 2 */
		{ 44,    44,    44+1, 44+2, 44+3}, /* IdSel 3 PCI 0 slot 3 */
		{ 48,    48,    48+1, 48+2, 48+3}, /* IdSel 4 PCI 1 slot 4 */
		{ 52,    52,    52+1, 52+2, 52+3}, /* IdSel 5 PCI 1 slot 5 */
		{ 56,    56,    56+1, 56+2, 56+3}, /* IdSel 6 PCI 1 slot 6 */
		{ 60,    60,    60+1, 60+2, 60+3}, /* IdSel 7 PCI 1 slot 7 */
	};
	long min_idsel = 0, max_idsel = 7, irqs_per_slot = 5;

	struct pci_controller *hose = dev->sysdata;
	int irq = COMMON_TABLE_LOOKUP;

	if (irq > 0) {
		int qbbno = hose->index >> 3;
		int pcano = (hose->index >> 1) & 3;
		irq += (qbbno << 8) + (pcano << 6);
	}
	return irq;
}


/*
 * The System Vectors
 */

struct alpha_machine_vector wildfire_mv __initmv = {
	.vector_name		= "WILDFIRE",
	DO_EV6_MMU,
	DO_DEFAULT_RTC,
	DO_WILDFIRE_IO,
	.machine_check		= wildfire_machine_check,
	.max_isa_dma_address	= ALPHA_MAX_ISA_DMA_ADDRESS,
	.min_io_address		= DEFAULT_IO_BASE,
	.min_mem_address	= DEFAULT_MEM_BASE,

	.nr_irqs		= WILDFIRE_NR_IRQS,
	.device_interrupt	= wildfire_device_interrupt,

	.init_arch		= wildfire_init_arch,
	.init_irq		= wildfire_init_irq,
	.init_rtc		= common_init_rtc,
	.init_pci		= common_init_pci,
	.kill_arch		= wildfire_kill_arch,
	.pci_map_irq		= wildfire_map_irq,
	.pci_swizzle		= common_swizzle,

	.pa_to_nid		= wildfire_pa_to_nid,
	.cpuid_to_nid		= wildfire_cpuid_to_nid,
	.node_mem_start		= wildfire_node_mem_start,
	.node_mem_size		= wildfire_node_mem_size,
};
ALIAS_MV(wildfire)
