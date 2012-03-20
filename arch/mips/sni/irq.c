/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 1992 Linus Torvalds
 * Copyright (C) 1994 - 2000 Ralf Baechle
 * Copyright (C) 2006 Thomas Bogendoerfer
 */
#include <linux/delay.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel.h>

#include <asm/i8259.h>
#include <asm/io.h>
#include <asm/sni.h>
#include <asm/irq.h>
#include <asm/irq_cpu.h>

void (*sni_hwint)(void);

asmlinkage void plat_irq_dispatch(void)
{
	sni_hwint();
}

/* ISA irq handler */
static irqreturn_t sni_isa_irq_handler(int dummy, void *p)
{
	int irq;

	irq = i8259_irq();
	if (unlikely(irq < 0))
		return IRQ_NONE;

	generic_handle_irq(irq);
	return IRQ_HANDLED;
}

struct irqaction sni_isa_irq = {
	.handler = sni_isa_irq_handler,
	.name = "ISA",
	.flags = IRQF_SHARED
};

/*
 * On systems with i8259-style interrupt controllers we assume for
 * driver compatibility reasons interrupts 0 - 15 to be the i8295
 * interrupts even if the hardware uses a different interrupt numbering.
 */
void __init arch_init_irq(void)
{
	init_i8259_irqs();			/* Integrated i8259  */
	switch (sni_brd_type) {
	case SNI_BRD_10:
	case SNI_BRD_10NEW:
	case SNI_BRD_TOWER_OASIC:
	case SNI_BRD_MINITOWER:
	        sni_a20r_irq_init();
	        break;

	case SNI_BRD_PCI_TOWER:
	        sni_pcit_irq_init();
	        break;

	case SNI_BRD_PCI_TOWER_CPLUS:
	        sni_pcit_cplus_irq_init();
	        break;

	case SNI_BRD_RM200:
	        sni_rm200_irq_init();
	        break;

	case SNI_BRD_PCI_MTOWER:
	case SNI_BRD_PCI_DESKTOP:
	case SNI_BRD_PCI_MTOWER_CPLUS:
	        sni_pcimt_irq_init();
	        break;
	}
}
