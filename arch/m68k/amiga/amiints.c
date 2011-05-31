/*
 * Amiga Linux interrupt handling code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 */

#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/errno.h>

#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/amipcmcia.h>


/*
 * Enable/disable a particular machine specific interrupt source.
 * Note that this may affect other interrupts in case of a shared interrupt.
 * This function should only be called for a _very_ short time to change some
 * internal data, that may not be changed by the interrupt at the same time.
 */

static void amiga_irq_enable(struct irq_data *data)
{
	amiga_custom.intena = IF_SETCLR | (1 << (data->irq - IRQ_USER));
}

static void amiga_irq_disable(struct irq_data *data)
{
	amiga_custom.intena = 1 << (data->irq - IRQ_USER);
}

static struct irq_chip amiga_irq_chip = {
	.name		= "amiga",
	.irq_enable	= amiga_irq_enable,
	.irq_disable	= amiga_irq_disable,
};


/*
 * The builtin Amiga hardware interrupt handlers.
 */

static irqreturn_t ami_int1(int irq, void *dev_id)
{
	unsigned short ints = amiga_custom.intreqr & amiga_custom.intenar;

	/* if serial transmit buffer empty, interrupt */
	if (ints & IF_TBE) {
		amiga_custom.intreq = IF_TBE;
		generic_handle_irq(IRQ_AMIGA_TBE);
	}

	/* if floppy disk transfer complete, interrupt */
	if (ints & IF_DSKBLK) {
		amiga_custom.intreq = IF_DSKBLK;
		generic_handle_irq(IRQ_AMIGA_DSKBLK);
	}

	/* if software interrupt set, interrupt */
	if (ints & IF_SOFT) {
		amiga_custom.intreq = IF_SOFT;
		generic_handle_irq(IRQ_AMIGA_SOFT);
	}
	return IRQ_HANDLED;
}

static irqreturn_t ami_int3(int irq, void *dev_id)
{
	unsigned short ints = amiga_custom.intreqr & amiga_custom.intenar;

	/* if a blitter interrupt */
	if (ints & IF_BLIT) {
		amiga_custom.intreq = IF_BLIT;
		generic_handle_irq(IRQ_AMIGA_BLIT);
	}

	/* if a copper interrupt */
	if (ints & IF_COPER) {
		amiga_custom.intreq = IF_COPER;
		generic_handle_irq(IRQ_AMIGA_COPPER);
	}

	/* if a vertical blank interrupt */
	if (ints & IF_VERTB) {
		amiga_custom.intreq = IF_VERTB;
		generic_handle_irq(IRQ_AMIGA_VERTB);
	}
	return IRQ_HANDLED;
}

static irqreturn_t ami_int4(int irq, void *dev_id)
{
	unsigned short ints = amiga_custom.intreqr & amiga_custom.intenar;

	/* if audio 0 interrupt */
	if (ints & IF_AUD0) {
		amiga_custom.intreq = IF_AUD0;
		generic_handle_irq(IRQ_AMIGA_AUD0);
	}

	/* if audio 1 interrupt */
	if (ints & IF_AUD1) {
		amiga_custom.intreq = IF_AUD1;
		generic_handle_irq(IRQ_AMIGA_AUD1);
	}

	/* if audio 2 interrupt */
	if (ints & IF_AUD2) {
		amiga_custom.intreq = IF_AUD2;
		generic_handle_irq(IRQ_AMIGA_AUD2);
	}

	/* if audio 3 interrupt */
	if (ints & IF_AUD3) {
		amiga_custom.intreq = IF_AUD3;
		generic_handle_irq(IRQ_AMIGA_AUD3);
	}
	return IRQ_HANDLED;
}

static irqreturn_t ami_int5(int irq, void *dev_id)
{
	unsigned short ints = amiga_custom.intreqr & amiga_custom.intenar;

	/* if serial receive buffer full interrupt */
	if (ints & IF_RBF) {
		/* acknowledge of IF_RBF must be done by the serial interrupt */
		generic_handle_irq(IRQ_AMIGA_RBF);
	}

	/* if a disk sync interrupt */
	if (ints & IF_DSKSYN) {
		amiga_custom.intreq = IF_DSKSYN;
		generic_handle_irq(IRQ_AMIGA_DSKSYN);
	}
	return IRQ_HANDLED;
}


/*
 * void amiga_init_IRQ(void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function should be called during kernel startup to initialize
 * the amiga IRQ handling routines.
 */

void __init amiga_init_IRQ(void)
{
	if (request_irq(IRQ_AUTO_1, ami_int1, 0, "int1", NULL))
		pr_err("Couldn't register int%d\n", 1);
	if (request_irq(IRQ_AUTO_3, ami_int3, 0, "int3", NULL))
		pr_err("Couldn't register int%d\n", 3);
	if (request_irq(IRQ_AUTO_4, ami_int4, 0, "int4", NULL))
		pr_err("Couldn't register int%d\n", 4);
	if (request_irq(IRQ_AUTO_5, ami_int5, 0, "int5", NULL))
		pr_err("Couldn't register int%d\n", 5);

	m68k_setup_irq_controller(&amiga_irq_chip, handle_simple_irq, IRQ_USER,
				  AMI_STD_IRQS);

	/* turn off PCMCIA interrupts */
	if (AMIGAHW_PRESENT(PCMCIA))
		gayle.inten = GAYLE_IRQ_IDE;

	/* turn off all interrupts and enable the master interrupt bit */
	amiga_custom.intena = 0x7fff;
	amiga_custom.intreq = 0x7fff;
	amiga_custom.intena = IF_SETCLR | IF_INTEN;

	cia_init_IRQ(&ciaa_base);
	cia_init_IRQ(&ciab_base);
}
