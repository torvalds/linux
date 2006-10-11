#include <linux/module.h>
#include <linux/stddef.h>
#include <linux/init.h>
#include <linux/sched.h>
#include <linux/signal.h>
#include <linux/interrupt.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/8xx_immap.h>
#include <asm/mpc8xx.h>
#include "ppc8xx_pic.h"

extern int cpm_get_irq(void);

/* The 8xx internal interrupt controller.  It is usually
 * the only interrupt controller.  Some boards, like the MBX and
 * Sandpoint have the 8259 as a secondary controller.  Depending
 * upon the processor type, the internal controller can have as
 * few as 16 interrups or as many as 64.  We could use  the
 * "clear_bit()" and "set_bit()" functions like other platforms,
 * but they are overkill for us.
 */

static void m8xx_mask_irq(unsigned int irq_nr)
{
	int	bit, word;

	bit = irq_nr & 0x1f;
	word = irq_nr >> 5;

	ppc_cached_irq_mask[word] &= ~(1 << (31-bit));
	out_be32(&((immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask, ppc_cached_irq_mask[word]);
}

static void m8xx_unmask_irq(unsigned int irq_nr)
{
	int	bit, word;

	bit = irq_nr & 0x1f;
	word = irq_nr >> 5;

	ppc_cached_irq_mask[word] |= (1 << (31-bit));
	out_be32(&((immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask, ppc_cached_irq_mask[word]);
}

static void m8xx_end_irq(unsigned int irq_nr)
{
	if (!(irq_desc[irq_nr].status & (IRQ_DISABLED|IRQ_INPROGRESS))
			&& irq_desc[irq_nr].action) {
		int bit, word;

		bit = irq_nr & 0x1f;
		word = irq_nr >> 5;

		ppc_cached_irq_mask[word] |= (1 << (31-bit));
		out_be32(&((immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask, ppc_cached_irq_mask[word]);
	}
}


static void m8xx_mask_and_ack(unsigned int irq_nr)
{
	int	bit, word;

	bit = irq_nr & 0x1f;
	word = irq_nr >> 5;

	ppc_cached_irq_mask[word] &= ~(1 << (31-bit));
	out_be32(&((immap_t *)IMAP_ADDR)->im_siu_conf.sc_simask, ppc_cached_irq_mask[word]);
	out_be32(&((immap_t *)IMAP_ADDR)->im_siu_conf.sc_sipend, 1 << (31-bit));
}

struct hw_interrupt_type ppc8xx_pic = {
	.typename = " 8xx SIU  ",
	.enable = m8xx_unmask_irq,
	.disable = m8xx_mask_irq,
	.ack = m8xx_mask_and_ack,
	.end = m8xx_end_irq,
};

/*
 * We either return a valid interrupt or -1 if there is nothing pending
 */
int
m8xx_get_irq(struct pt_regs *regs)
{
	int irq;

	/* For MPC8xx, read the SIVEC register and shift the bits down
	 * to get the irq number.
	 */
	irq = in_be32(&((immap_t *)IMAP_ADDR)->im_siu_conf.sc_sivec) >> 26;

	/*
	 * When we read the sivec without an interrupt to process, we will
	 * get back SIU_LEVEL7.  In this case, return -1
	 */
        if (irq == CPM_INTERRUPT)
        	irq = CPM_IRQ_OFFSET + cpm_get_irq();
#if defined(CONFIG_PCI)
	else if (irq == ISA_BRIDGE_INT) {
		int isa_irq;

		if ((isa_irq = i8259_poll(regs)) >= 0)
			irq = I8259_IRQ_OFFSET + isa_irq;
	}
#endif	/* CONFIG_PCI */
	else if (irq == SIU_LEVEL7)
		irq = -1;

	return irq;
}

#if defined(CONFIG_MBX) && defined(CONFIG_PCI)
/* Only the MBX uses the external 8259.  This allows us to catch standard
 * drivers that may mess up the internal interrupt controllers, and also
 * allow them to run without modification on the MBX.
 */
void mbx_i8259_action(int irq, void *dev_id, struct pt_regs *regs)
{
	/* This interrupt handler never actually gets called.  It is
	 * installed only to unmask the 8259 cascade interrupt in the SIU
	 * and to make the 8259 cascade interrupt visible in /proc/interrupts.
	 */
}
#endif	/* CONFIG_PCI */
