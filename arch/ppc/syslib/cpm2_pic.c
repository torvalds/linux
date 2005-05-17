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

#include "cpm2_pic.h"

static	u_char	irq_to_siureg[] = {
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
static	u_char	irq_to_siubit[] = {
	 0, 15, 14, 13, 12, 11, 10,  9,
	 8,  7,  6,  5,  4,  3,  2,  1,
	 2,  1, 15, 14, 13, 12, 11, 10,
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

	simr = &(cpm2_immr->im_intctl.ic_simrh);
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

	simr = &(cpm2_immr->im_intctl.ic_simrh);
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

	simr = &(cpm2_immr->im_intctl.ic_simrh);
	sipnr = &(cpm2_immr->im_intctl.ic_sipnrh);
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

		simr = &(cpm2_immr->im_intctl.ic_simrh);
		ppc_cached_irq_mask[word] |= 1 << bit;
		simr[word] = ppc_cached_irq_mask[word];
		/*
		 * Work around large numbers of spurious IRQs on PowerPC 82xx
		 * systems.
		 */
		mb();
	}
}

static struct hw_interrupt_type cpm2_pic = {
	.typename = " CPM2 SIU ",
	.enable = cpm2_unmask_irq,
	.disable = cpm2_mask_irq,
	.ack = cpm2_mask_and_ack,
	.end = cpm2_end_irq,
};

int cpm2_get_irq(struct pt_regs *regs)
{
	int irq;
        unsigned long bits;

        /* For CPM2, read the SIVEC register and shift the bits down
         * to get the irq number.         */
        bits = cpm2_immr->im_intctl.ic_sivec;
        irq = bits >> 26;

	if (irq == 0)
		return(-1);
	return irq+CPM_IRQ_OFFSET;
}

void cpm2_init_IRQ(void)
{
	int i;

	/* Clear the CPM IRQ controller, in case it has any bits set
	 * from the bootloader
	 */

	/* Mask out everything */
	cpm2_immr->im_intctl.ic_simrh = 0x00000000;
	cpm2_immr->im_intctl.ic_simrl = 0x00000000;
	wmb();

	/* Ack everything */
	cpm2_immr->im_intctl.ic_sipnrh = 0xffffffff;
	cpm2_immr->im_intctl.ic_sipnrl = 0xffffffff;
	wmb();

	/* Dummy read of the vector */
	i = cpm2_immr->im_intctl.ic_sivec;
	rmb();

	/* Initialize the default interrupt mapping priorities,
	 * in case the boot rom changed something on us.
	 */
	cpm2_immr->im_intctl.ic_sicr = 0;
	cpm2_immr->im_intctl.ic_scprrh = 0x05309770;
	cpm2_immr->im_intctl.ic_scprrl = 0x05309770;


	/* Enable chaining to OpenPIC, and make everything level
	 */
	for (i = 0; i < NR_CPM_INTS; i++) {
		irq_desc[i+CPM_IRQ_OFFSET].handler = &cpm2_pic;
		irq_desc[i+CPM_IRQ_OFFSET].status |= IRQ_LEVEL;
	}
}
