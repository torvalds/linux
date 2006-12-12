/*
 * Platform information definitions.
 *
 * Copied from arch/ppc/syslib/cpm2_pic.c with minor subsequent updates
 * to make in work in arch/powerpc/. Original (c) belongs to Dan Malek.
 *
 * Author:  Vitaly Bordug <vbordug@ru.mvista.com>
 *
 * 1999-2001 (c) Dan Malek <dan@embeddedalley.com>
 * 2006 (c) MontaVista Software, Inc.
 *
 * This file is licensed under the terms of the GNU General Public License
 * version 2. This program is licensed "as is" without any warranty of any
 * kind, whether express or implied.
 */

/* The CPM2 internal interrupt controller.  It is usually
 * the only interrupt controller.
 * There are two 32-bit registers (high/low) for up to 64
 * possible interrupts.
 *
 * Now, the fun starts.....Interrupt Numbers DO NOT MAP
 * in a simple arithmetic fashion to mask or pending registers.
 * That is, interrupt 4 does not map to bit position 4.
 * We create two tables, indexed by vector number, to indicate
 * which register to use and which bit in the register to use.
 */

#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/irq.h>

#include <asm/immap_cpm2.h>
#include <asm/mpc8260.h>
#include <asm/io.h>
#include <asm/prom.h>

#include "cpm2_pic.h"

static struct device_node *cpm2_pic_node;
static struct irq_host *cpm2_pic_host;
#define NR_MASK_WORDS   ((NR_IRQS + 31) / 32)
static unsigned long ppc_cached_irq_mask[NR_MASK_WORDS];

static const u_char irq_to_siureg[] = {
	1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0,
	1, 1, 1, 1, 1, 1, 1, 1,
	1, 1, 1, 1, 1, 1, 1, 1,
	0, 0, 0, 0, 0, 0, 0, 0,
	0, 0, 0, 0, 0, 0, 0, 0
};

/* bit numbers do not match the docs, these are precomputed so the bit for
 * a given irq is (1 << irq_to_siubit[irq]) */
static const u_char irq_to_siubit[] = {
	 0, 15, 14, 13, 12, 11, 10,  9,
	 8,  7,  6,  5,  4,  3,  2,  1,
	 2,  1,  0, 14, 13, 12, 11, 10,
	 9,  8,  7,  6,  5,  4,  3,  0,
	31, 30, 29, 28, 27, 26, 25, 24,
	23, 22, 21, 20, 19, 18, 17, 16,
	16, 17, 18, 19, 20, 21, 22, 23,
	24, 25, 26, 27, 28, 29, 30, 31,
};

static void cpm2_mask_irq(unsigned int irq_nr)
{
	int	bit, word;
	volatile uint	*simr;

	irq_nr -= CPM_IRQ_OFFSET;

	bit = irq_to_siubit[irq_nr];
	word = irq_to_siureg[irq_nr];

	simr = &(cpm2_intctl->ic_simrh);
	ppc_cached_irq_mask[word] &= ~(1 << bit);
	simr[word] = ppc_cached_irq_mask[word];
}

static void cpm2_unmask_irq(unsigned int irq_nr)
{
	int	bit, word;
	volatile uint	*simr;

	irq_nr -= CPM_IRQ_OFFSET;

	bit = irq_to_siubit[irq_nr];
	word = irq_to_siureg[irq_nr];

	simr = &(cpm2_intctl->ic_simrh);
	ppc_cached_irq_mask[word] |= 1 << bit;
	simr[word] = ppc_cached_irq_mask[word];
}

static void cpm2_mask_and_ack(unsigned int irq_nr)
{
	int	bit, word;
	volatile uint	*simr, *sipnr;

	irq_nr -= CPM_IRQ_OFFSET;

	bit = irq_to_siubit[irq_nr];
	word = irq_to_siureg[irq_nr];

	simr = &(cpm2_intctl->ic_simrh);
	sipnr = &(cpm2_intctl->ic_sipnrh);
	ppc_cached_irq_mask[word] &= ~(1 << bit);
	simr[word] = ppc_cached_irq_mask[word];
	sipnr[word] = 1 << bit;
}

static void cpm2_end_irq(unsigned int irq_nr)
{
	int	bit, word;
	volatile uint	*simr;

	if (!(irq_desc[irq_nr].status & (IRQ_DISABLED|IRQ_INPROGRESS))
			&& irq_desc[irq_nr].action) {

		irq_nr -= CPM_IRQ_OFFSET;
		bit = irq_to_siubit[irq_nr];
		word = irq_to_siureg[irq_nr];

		simr = &(cpm2_intctl->ic_simrh);
		ppc_cached_irq_mask[word] |= 1 << bit;
		simr[word] = ppc_cached_irq_mask[word];
		/*
		 * Work around large numbers of spurious IRQs on PowerPC 82xx
		 * systems.
		 */
		mb();
	}
}

static struct irq_chip cpm2_pic = {
	.typename = " CPM2 SIU ",
	.enable = cpm2_unmask_irq,
	.disable = cpm2_mask_irq,
	.unmask = cpm2_unmask_irq,
	.mask_ack = cpm2_mask_and_ack,
	.end = cpm2_end_irq,
};

unsigned int cpm2_get_irq(void)
{
	int irq;
	unsigned long bits;

       /* For CPM2, read the SIVEC register and shift the bits down
         * to get the irq number.         */
        bits = cpm2_intctl->ic_sivec;
        irq = bits >> 26;

	if (irq == 0)
		return(-1);
	return irq+CPM_IRQ_OFFSET;
}

static int cpm2_pic_host_match(struct irq_host *h, struct device_node *node)
{
	return cpm2_pic_node == NULL || cpm2_pic_node == node;
}

static int cpm2_pic_host_map(struct irq_host *h, unsigned int virq,
			  irq_hw_number_t hw)
{
	pr_debug("cpm2_pic_host_map(%d, 0x%lx)\n", virq, hw);

	get_irq_desc(virq)->status |= IRQ_LEVEL;
	set_irq_chip_and_handler(virq, &cpm2_pic, handle_level_irq);
	return 0;
}

static void cpm2_host_unmap(struct irq_host *h, unsigned int virq)
{
	/* Make sure irq is masked in hardware */
	cpm2_mask_irq(virq);

	/* remove chip and handler */
	set_irq_chip_and_handler(virq, NULL, NULL);
}

static int cpm2_pic_host_xlate(struct irq_host *h, struct device_node *ct,
			    u32 *intspec, unsigned int intsize,
			    irq_hw_number_t *out_hwirq, unsigned int *out_flags)
{
	static const unsigned char map_cpm2_senses[4] = {
		IRQ_TYPE_LEVEL_LOW,
		IRQ_TYPE_LEVEL_HIGH,
		IRQ_TYPE_EDGE_FALLING,
		IRQ_TYPE_EDGE_RISING,
	};

	*out_hwirq = intspec[0];
	if (intsize > 1 && intspec[1] < 4)
		*out_flags = map_cpm2_senses[intspec[1]];
	else
		*out_flags = IRQ_TYPE_NONE;

	return 0;
}

static struct irq_host_ops cpm2_pic_host_ops = {
	.match = cpm2_pic_host_match,
	.map = cpm2_pic_host_map,
	.unmap = cpm2_host_unmap,
	.xlate = cpm2_pic_host_xlate,
};

void cpm2_pic_init(struct device_node *node)
{
	int i;

	/* Clear the CPM IRQ controller, in case it has any bits set
	 * from the bootloader
	 */

	/* Mask out everything */

	cpm2_intctl->ic_simrh = 0x00000000;
	cpm2_intctl->ic_simrl = 0x00000000;

	wmb();

	/* Ack everything */
	cpm2_intctl->ic_sipnrh = 0xffffffff;
	cpm2_intctl->ic_sipnrl = 0xffffffff;
	wmb();

	/* Dummy read of the vector */
	i = cpm2_intctl->ic_sivec;
	rmb();

	/* Initialize the default interrupt mapping priorities,
	 * in case the boot rom changed something on us.
	 */
	cpm2_intctl->ic_sicr = 0;
	cpm2_intctl->ic_scprrh = 0x05309770;
	cpm2_intctl->ic_scprrl = 0x05309770;

	/* create a legacy host */
	if (node)
		cpm2_pic_node = of_node_get(node);

	cpm2_pic_host = irq_alloc_host(IRQ_HOST_MAP_LINEAR, 64, &cpm2_pic_host_ops, 64);
	if (cpm2_pic_host == NULL) {
		printk(KERN_ERR "CPM2 PIC: failed to allocate irq host!\n");
		return;
	}
}
