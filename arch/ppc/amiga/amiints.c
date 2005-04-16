/*
 * arch/ppc/amiga/amiints.c -- Amiga Linux interrupt handling code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * 11/07/96: rewritten interrupt handling, irq lists are exists now only for
 *           this sources where it makes sense (VERTB/PORTS/EXTER) and you must
 *           be careful that dev_id for this sources is unique since this the
 *           only possibility to distinguish between different handlers for
 *           free_irq. irq lists also have different irq flags:
 *           - IRQ_FLG_FAST: handler is inserted at top of list (after other
 *                           fast handlers)
 *           - IRQ_FLG_SLOW: handler is inserted at bottom of list and before
 *                           they're executed irq level is set to the previous
 *                           one, but handlers don't need to be reentrant, if
 *                           reentrance occurred, slow handlers will be just
 *                           called again.
 *           The whole interrupt handling for CIAs is moved to cia.c
 *           /Roman Zippel
 *
 * 07/08/99: rewamp of the interrupt handling - we now have two types of
 *           interrupts, normal and fast handlers, fast handlers being
 *           marked with SA_INTERRUPT and runs with all other interrupts
 *           disabled. Normal interrupts disable their own source but
 *           run with all other interrupt sources enabled.
 *           PORTS and EXTER interrupts are always shared even if the
 *           drivers do not explicitly mark this when calling
 *           request_irq which they really should do.
 *           This is similar to the way interrupts are handled on all
 *           other architectures and makes a ton of sense besides
 *           having the advantage of making it easier to share
 *           drivers.
 *           /Jes
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/kernel_stat.h>
#include <linux/init.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/amipcmcia.h>

#ifdef CONFIG_APUS
#include <asm/amigappc.h>
#endif

extern void cia_init_IRQ(struct ciabase *base);

unsigned short ami_intena_vals[AMI_STD_IRQS] = {
	IF_VERTB, IF_COPER, IF_AUD0, IF_AUD1, IF_AUD2, IF_AUD3, IF_BLIT,
	IF_DSKSYN, IF_DSKBLK, IF_RBF, IF_TBE, IF_SOFT, IF_PORTS, IF_EXTER
};
static const unsigned char ami_servers[AMI_STD_IRQS] = {
	1, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1
};

static short ami_ablecount[AMI_IRQS];

static void ami_badint(int irq, void *dev_id, struct pt_regs *fp)
{
/*	num_spurious += 1;*/
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

__init
void amiga_init_IRQ(void)
{
	int i;

	for (i = 0; i < AMI_IRQS; i++)
		ami_ablecount[i] = 0;

	/* turn off PCMCIA interrupts */
	if (AMIGAHW_PRESENT(PCMCIA))
		gayle.inten = GAYLE_IRQ_IDE;

	/* turn off all interrupts... */
	custom.intena = 0x7fff;
	custom.intreq = 0x7fff;

#ifdef CONFIG_APUS
	/* Clear any inter-CPU interrupt requests. Circumvents bug in
           Blizzard IPL emulation HW (or so it appears). */
	APUS_WRITE(APUS_INT_LVL, INTLVL_SETRESET | INTLVL_MASK);

	/* Init IPL emulation. */
	APUS_WRITE(APUS_REG_INT, REGINT_INTMASTER | REGINT_ENABLEIPL);
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_DISABLEINT);
	APUS_WRITE(APUS_IPL_EMU, IPLEMU_SETRESET | IPLEMU_IPLMASK);
#endif
	/* ... and enable the master interrupt bit */
	custom.intena = IF_SETCLR | IF_INTEN;

	cia_init_IRQ(&ciaa_base);
	cia_init_IRQ(&ciab_base);
}

/*
 * Enable/disable a particular machine specific interrupt source.
 * Note that this may affect other interrupts in case of a shared interrupt.
 * This function should only be called for a _very_ short time to change some
 * internal data, that may not be changed by the interrupt at the same time.
 * ami_(enable|disable)_irq calls may also be nested.
 */

void amiga_enable_irq(unsigned int irq)
{
	if (irq >= AMI_IRQS) {
		printk("%s: Unknown IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	ami_ablecount[irq]--;
	if (ami_ablecount[irq]<0)
		ami_ablecount[irq]=0;
	else if (ami_ablecount[irq])
		return;

	/* No action for auto-vector interrupts */
	if (irq >= IRQ_AMIGA_AUTO){
		printk("%s: Trying to enable auto-vector IRQ %i\n",
		       __FUNCTION__, irq - IRQ_AMIGA_AUTO);
		return;
	}

	if (irq >= IRQ_AMIGA_CIAA) {
		cia_set_irq(irq, 0);
		cia_able_irq(irq, 1);
		return;
	}

	/* enable the interrupt */
	custom.intena = IF_SETCLR | ami_intena_vals[irq];
}

void amiga_disable_irq(unsigned int irq)
{
	if (irq >= AMI_IRQS) {
		printk("%s: Unknown IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	if (ami_ablecount[irq]++)
		return;

	/* No action for auto-vector interrupts */
	if (irq >= IRQ_AMIGA_AUTO) {
		printk("%s: Trying to disable auto-vector IRQ %i\n",
		       __FUNCTION__, irq - IRQ_AMIGA_AUTO);
		return;
	}

	if (irq >= IRQ_AMIGA_CIAA) {
		cia_able_irq(irq, 0);
		return;
	}

	/* disable the interrupt */
	custom.intena = ami_intena_vals[irq];
}

inline void amiga_do_irq(int irq, struct pt_regs *fp)
{
	irq_desc_t *desc = irq_desc + irq;
	struct irqaction *action = desc->action;

	kstat_cpu(0).irqs[irq]++;
	action->handler(irq, action->dev_id, fp);
}

void amiga_do_irq_list(int irq, struct pt_regs *fp)
{
	irq_desc_t *desc = irq_desc + irq;
	struct irqaction *action;

	kstat_cpu(0).irqs[irq]++;

	custom.intreq = ami_intena_vals[irq];

	for (action = desc->action; action; action = action->next)
		action->handler(irq, action->dev_id, fp);
}

/*
 * The builtin Amiga hardware interrupt handlers.
 */

static void ami_int1(int irq, void *dev_id, struct pt_regs *fp)
{
	unsigned short ints = custom.intreqr & custom.intenar;

	/* if serial transmit buffer empty, interrupt */
	if (ints & IF_TBE) {
		custom.intreq = IF_TBE;
		amiga_do_irq(IRQ_AMIGA_TBE, fp);
	}

	/* if floppy disk transfer complete, interrupt */
	if (ints & IF_DSKBLK) {
		custom.intreq = IF_DSKBLK;
		amiga_do_irq(IRQ_AMIGA_DSKBLK, fp);
	}

	/* if software interrupt set, interrupt */
	if (ints & IF_SOFT) {
		custom.intreq = IF_SOFT;
		amiga_do_irq(IRQ_AMIGA_SOFT, fp);
	}
}

static void ami_int3(int irq, void *dev_id, struct pt_regs *fp)
{
	unsigned short ints = custom.intreqr & custom.intenar;

	/* if a blitter interrupt */
	if (ints & IF_BLIT) {
		custom.intreq = IF_BLIT;
		amiga_do_irq(IRQ_AMIGA_BLIT, fp);
	}

	/* if a copper interrupt */
	if (ints & IF_COPER) {
		custom.intreq = IF_COPER;
		amiga_do_irq(IRQ_AMIGA_COPPER, fp);
	}

	/* if a vertical blank interrupt */
	if (ints & IF_VERTB)
		amiga_do_irq_list(IRQ_AMIGA_VERTB, fp);
}

static void ami_int4(int irq, void *dev_id, struct pt_regs *fp)
{
	unsigned short ints = custom.intreqr & custom.intenar;

	/* if audio 0 interrupt */
	if (ints & IF_AUD0) {
		custom.intreq = IF_AUD0;
		amiga_do_irq(IRQ_AMIGA_AUD0, fp);
	}

	/* if audio 1 interrupt */
	if (ints & IF_AUD1) {
		custom.intreq = IF_AUD1;
		amiga_do_irq(IRQ_AMIGA_AUD1, fp);
	}

	/* if audio 2 interrupt */
	if (ints & IF_AUD2) {
		custom.intreq = IF_AUD2;
		amiga_do_irq(IRQ_AMIGA_AUD2, fp);
	}

	/* if audio 3 interrupt */
	if (ints & IF_AUD3) {
		custom.intreq = IF_AUD3;
		amiga_do_irq(IRQ_AMIGA_AUD3, fp);
	}
}

static void ami_int5(int irq, void *dev_id, struct pt_regs *fp)
{
	unsigned short ints = custom.intreqr & custom.intenar;

	/* if serial receive buffer full interrupt */
	if (ints & IF_RBF) {
		/* acknowledge of IF_RBF must be done by the serial interrupt */
		amiga_do_irq(IRQ_AMIGA_RBF, fp);
	}

	/* if a disk sync interrupt */
	if (ints & IF_DSKSYN) {
		custom.intreq = IF_DSKSYN;
		amiga_do_irq(IRQ_AMIGA_DSKSYN, fp);
	}
}

static void ami_int7(int irq, void *dev_id, struct pt_regs *fp)
{
	panic ("level 7 interrupt received\n");
}

#ifdef CONFIG_APUS
/* The PPC irq handling links all handlers requested on the same vector
   and executes them in a loop. Having ami_badint at the end of the chain
   is a bad idea. */
struct irqaction amiga_sys_irqaction[AUTO_IRQS] = {
	{ .handler = ami_badint, .name = "spurious int" },
	{ .handler = ami_int1, .name = "int1 handler" },
	{ 0, /* CIAA */ },
	{ .handler = ami_int3, .name = "int3 handler" },
	{ .handler = ami_int4, .name = "int4 handler" },
	{ .handler = ami_int5, .name = "int5 handler" },
	{ 0, /* CIAB */ },
	{ .handler = ami_int7, .name = "int7 handler" },
};
#else
void (*amiga_default_handler[SYS_IRQS])(int, void *, struct pt_regs *) = {
	ami_badint, ami_int1, ami_badint, ami_int3,
	ami_int4, ami_int5, ami_badint, ami_int7
};
#endif
