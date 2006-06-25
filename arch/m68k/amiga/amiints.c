/*
 * linux/arch/m68k/amiga/amiints.c -- Amiga Linux interrupt handling code
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

#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/errno.h>
#include <linux/seq_file.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/amigahw.h>
#include <asm/amigaints.h>
#include <asm/amipcmcia.h>

extern int cia_request_irq(struct ciabase *base,int irq,
                           irqreturn_t (*handler)(int, void *, struct pt_regs *),
                           unsigned long flags, const char *devname, void *dev_id);
extern void cia_free_irq(struct ciabase *base, unsigned int irq, void *dev_id);
extern void cia_init_IRQ(struct ciabase *base);
extern int cia_get_irq_list(struct ciabase *base, struct seq_file *p);

/* irq node variables for amiga interrupt sources */
static irq_node_t *ami_irq_list[AMI_STD_IRQS];

static unsigned short amiga_intena_vals[AMI_STD_IRQS] = {
	[IRQ_AMIGA_VERTB-IRQ_USER]	= IF_VERTB,
	[IRQ_AMIGA_COPPER-IRQ_USER]	= IF_COPER,
	[IRQ_AMIGA_AUD0-IRQ_USER]	= IF_AUD0,
	[IRQ_AMIGA_AUD1-IRQ_USER]	= IF_AUD1,
	[IRQ_AMIGA_AUD2-IRQ_USER]	= IF_AUD2,
	[IRQ_AMIGA_AUD3-IRQ_USER]	= IF_AUD3,
	[IRQ_AMIGA_BLIT-IRQ_USER]	= IF_BLIT,
	[IRQ_AMIGA_DSKSYN-IRQ_USER]	= IF_DSKSYN,
	[IRQ_AMIGA_DSKBLK-IRQ_USER]	= IF_DSKBLK,
	[IRQ_AMIGA_RBF-IRQ_USER]	= IF_RBF,
	[IRQ_AMIGA_TBE-IRQ_USER]	= IF_TBE,
	[IRQ_AMIGA_SOFT-IRQ_USER]	= IF_SOFT,
	[IRQ_AMIGA_PORTS-IRQ_USER]	= IF_PORTS,
	[IRQ_AMIGA_EXTER-IRQ_USER]	= IF_EXTER
};
static const unsigned char ami_servers[AMI_STD_IRQS] = {
	[IRQ_AMIGA_VERTB-IRQ_USER]	= 1,
	[IRQ_AMIGA_PORTS-IRQ_USER]	= 1,
	[IRQ_AMIGA_EXTER-IRQ_USER]	= 1
};

static short ami_ablecount[AMI_IRQS];

static irqreturn_t ami_badint(int irq, void *dev_id, struct pt_regs *fp)
{
	num_spurious += 1;
	return IRQ_NONE;
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
	int i;

	/* initialize handlers */
	for (i = 0; i < AMI_STD_IRQS; i++) {
		if (ami_servers[i]) {
			ami_irq_list[i] = NULL;
		} else {
			ami_irq_list[i] = new_irq_node();
			ami_irq_list[i]->handler = ami_badint;
			ami_irq_list[i]->flags   = 0;
			ami_irq_list[i]->dev_id  = NULL;
			ami_irq_list[i]->devname = NULL;
			ami_irq_list[i]->next    = NULL;
		}
	}
	for (i = 0; i < AMI_IRQS; i++)
		ami_ablecount[i] = 0;

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

static inline int amiga_insert_irq(irq_node_t **list, irq_node_t *node)
{
	unsigned long flags;
	irq_node_t *cur;

	if (!node->dev_id)
		printk("%s: Warning: dev_id of %s is zero\n",
		       __FUNCTION__, node->devname);

	local_irq_save(flags);

	cur = *list;

	if (node->flags & SA_INTERRUPT) {
		if (node->flags & SA_SHIRQ)
			return -EBUSY;
		/*
		 * There should never be more than one
		 */
		while (cur && cur->flags & SA_INTERRUPT) {
			list = &cur->next;
			cur = cur->next;
		}
	} else {
		while (cur) {
			list = &cur->next;
			cur = cur->next;
		}
	}

	node->next = cur;
	*list = node;

	local_irq_restore(flags);
	return 0;
}

static inline void amiga_delete_irq(irq_node_t **list, void *dev_id)
{
	unsigned long flags;
	irq_node_t *node;

	local_irq_save(flags);

	for (node = *list; node; list = &node->next, node = *list) {
		if (node->dev_id == dev_id) {
			*list = node->next;
			/* Mark it as free. */
			node->handler = NULL;
			local_irq_restore(flags);
			return;
		}
	}
	local_irq_restore(flags);
	printk ("%s: tried to remove invalid irq\n", __FUNCTION__);
}

/*
 * amiga_request_irq : add an interrupt service routine for a particular
 *                     machine specific interrupt source.
 *                     If the addition was successful, it returns 0.
 */

int amiga_request_irq(unsigned int irq,
		      irqreturn_t (*handler)(int, void *, struct pt_regs *),
                      unsigned long flags, const char *devname, void *dev_id)
{
	irq_node_t *node;
	int error = 0;

	if (irq >= AMI_IRQS) {
		printk ("%s: Unknown IRQ %d from %s\n", __FUNCTION__,
			irq, devname);
		return -ENXIO;
	}

	if (irq < IRQ_USER)
		return cpu_request_irq(irq, handler, flags, devname, dev_id);

	if (irq >= IRQ_AMIGA_CIAB)
		return cia_request_irq(&ciab_base, irq - IRQ_AMIGA_CIAB,
		                       handler, flags, devname, dev_id);

	if (irq >= IRQ_AMIGA_CIAA)
		return cia_request_irq(&ciaa_base, irq - IRQ_AMIGA_CIAA,
		                       handler, flags, devname, dev_id);

	irq -= IRQ_USER;
	/*
	 * IRQ_AMIGA_PORTS & IRQ_AMIGA_EXTER defaults to shared,
	 * we could add a check here for the SA_SHIRQ flag but all drivers
	 * should be aware of sharing anyway.
	 */
	if (ami_servers[irq]) {
		if (!(node = new_irq_node()))
			return -ENOMEM;
		node->handler = handler;
		node->flags   = flags;
		node->dev_id  = dev_id;
		node->devname = devname;
		node->next    = NULL;
		error = amiga_insert_irq(&ami_irq_list[irq], node);
	} else {
		ami_irq_list[irq]->handler = handler;
		ami_irq_list[irq]->flags   = flags;
		ami_irq_list[irq]->dev_id  = dev_id;
		ami_irq_list[irq]->devname = devname;
	}

	/* enable the interrupt */
	if (irq < IRQ_AMIGA_PORTS && !ami_ablecount[irq])
		amiga_custom.intena = IF_SETCLR | amiga_intena_vals[irq];

	return error;
}

void amiga_free_irq(unsigned int irq, void *dev_id)
{
	if (irq >= AMI_IRQS) {
		printk ("%s: Unknown IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	if (irq < IRQ_USER)
		cpu_free_irq(irq, dev_id);

	if (irq >= IRQ_AMIGA_CIAB) {
		cia_free_irq(&ciab_base, irq - IRQ_AMIGA_CIAB, dev_id);
		return;
	}

	if (irq >= IRQ_AMIGA_CIAA) {
		cia_free_irq(&ciaa_base, irq - IRQ_AMIGA_CIAA, dev_id);
		return;
	}

	irq -= IRQ_USER;
	if (ami_servers[irq]) {
		amiga_delete_irq(&ami_irq_list[irq], dev_id);
		/* if server list empty, disable the interrupt */
		if (!ami_irq_list[irq] && irq < IRQ_AMIGA_PORTS)
			amiga_custom.intena = amiga_intena_vals[irq];
	} else {
		if (ami_irq_list[irq]->dev_id != dev_id)
			printk("%s: removing probably wrong IRQ %d from %s\n",
			       __FUNCTION__, irq, ami_irq_list[irq]->devname);
		ami_irq_list[irq]->handler = ami_badint;
		ami_irq_list[irq]->flags   = 0;
		ami_irq_list[irq]->dev_id  = NULL;
		ami_irq_list[irq]->devname = NULL;
		amiga_custom.intena = amiga_intena_vals[irq];
	}
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

	if (--ami_ablecount[irq])
		return;

	/* No action for auto-vector interrupts */
	if (irq < IRQ_USER) {
		printk("%s: Trying to enable auto-vector IRQ %i\n",
		       __FUNCTION__, irq);
		return;
	}

	if (irq >= IRQ_AMIGA_CIAB) {
		cia_set_irq(&ciab_base, (1 << (irq - IRQ_AMIGA_CIAB)));
		cia_able_irq(&ciab_base, CIA_ICR_SETCLR |
		             (1 << (irq - IRQ_AMIGA_CIAB)));
		return;
	}

	if (irq >= IRQ_AMIGA_CIAA) {
		cia_set_irq(&ciaa_base, (1 << (irq - IRQ_AMIGA_CIAA)));
		cia_able_irq(&ciaa_base, CIA_ICR_SETCLR |
		             (1 << (irq - IRQ_AMIGA_CIAA)));
		return;
	}

	/* enable the interrupt */
	amiga_custom.intena = IF_SETCLR | amiga_intena_vals[irq-IRQ_USER];
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
	if (irq < IRQ_USER) {
		printk("%s: Trying to disable auto-vector IRQ %i\n",
		       __FUNCTION__, irq);
		return;
	}

	if (irq >= IRQ_AMIGA_CIAB) {
		cia_able_irq(&ciab_base, 1 << (irq - IRQ_AMIGA_CIAB));
		return;
	}

	if (irq >= IRQ_AMIGA_CIAA) {
		cia_able_irq(&ciaa_base, 1 << (irq - IRQ_AMIGA_CIAA));
		return;
	}

	/* disable the interrupt */
	amiga_custom.intena = amiga_intena_vals[irq-IRQ_USER];
}

inline void amiga_do_irq(int irq, struct pt_regs *fp)
{
	kstat_cpu(0).irqs[irq]++;
	ami_irq_list[irq-IRQ_USER]->handler(irq, ami_irq_list[irq-IRQ_USER]->dev_id, fp);
}

void amiga_do_irq_list(int irq, struct pt_regs *fp)
{
	irq_node_t *node;

	kstat_cpu(0).irqs[irq]++;

	amiga_custom.intreq = amiga_intena_vals[irq-IRQ_USER];

	for (node = ami_irq_list[irq-IRQ_USER]; node; node = node->next)
		node->handler(irq, node->dev_id, fp);
}

/*
 * The builtin Amiga hardware interrupt handlers.
 */

static irqreturn_t ami_int1(int irq, void *dev_id, struct pt_regs *fp)
{
	unsigned short ints = amiga_custom.intreqr & amiga_custom.intenar;

	/* if serial transmit buffer empty, interrupt */
	if (ints & IF_TBE) {
		amiga_custom.intreq = IF_TBE;
		amiga_do_irq(IRQ_AMIGA_TBE, fp);
	}

	/* if floppy disk transfer complete, interrupt */
	if (ints & IF_DSKBLK) {
		amiga_custom.intreq = IF_DSKBLK;
		amiga_do_irq(IRQ_AMIGA_DSKBLK, fp);
	}

	/* if software interrupt set, interrupt */
	if (ints & IF_SOFT) {
		amiga_custom.intreq = IF_SOFT;
		amiga_do_irq(IRQ_AMIGA_SOFT, fp);
	}
	return IRQ_HANDLED;
}

static irqreturn_t ami_int3(int irq, void *dev_id, struct pt_regs *fp)
{
	unsigned short ints = amiga_custom.intreqr & amiga_custom.intenar;

	/* if a blitter interrupt */
	if (ints & IF_BLIT) {
		amiga_custom.intreq = IF_BLIT;
		amiga_do_irq(IRQ_AMIGA_BLIT, fp);
	}

	/* if a copper interrupt */
	if (ints & IF_COPER) {
		amiga_custom.intreq = IF_COPER;
		amiga_do_irq(IRQ_AMIGA_COPPER, fp);
	}

	/* if a vertical blank interrupt */
	if (ints & IF_VERTB)
		amiga_do_irq_list(IRQ_AMIGA_VERTB, fp);
	return IRQ_HANDLED;
}

static irqreturn_t ami_int4(int irq, void *dev_id, struct pt_regs *fp)
{
	unsigned short ints = amiga_custom.intreqr & amiga_custom.intenar;

	/* if audio 0 interrupt */
	if (ints & IF_AUD0) {
		amiga_custom.intreq = IF_AUD0;
		amiga_do_irq(IRQ_AMIGA_AUD0, fp);
	}

	/* if audio 1 interrupt */
	if (ints & IF_AUD1) {
		amiga_custom.intreq = IF_AUD1;
		amiga_do_irq(IRQ_AMIGA_AUD1, fp);
	}

	/* if audio 2 interrupt */
	if (ints & IF_AUD2) {
		amiga_custom.intreq = IF_AUD2;
		amiga_do_irq(IRQ_AMIGA_AUD2, fp);
	}

	/* if audio 3 interrupt */
	if (ints & IF_AUD3) {
		amiga_custom.intreq = IF_AUD3;
		amiga_do_irq(IRQ_AMIGA_AUD3, fp);
	}
	return IRQ_HANDLED;
}

static irqreturn_t ami_int5(int irq, void *dev_id, struct pt_regs *fp)
{
	unsigned short ints = amiga_custom.intreqr & amiga_custom.intenar;

	/* if serial receive buffer full interrupt */
	if (ints & IF_RBF) {
		/* acknowledge of IF_RBF must be done by the serial interrupt */
		amiga_do_irq(IRQ_AMIGA_RBF, fp);
	}

	/* if a disk sync interrupt */
	if (ints & IF_DSKSYN) {
		amiga_custom.intreq = IF_DSKSYN;
		amiga_do_irq(IRQ_AMIGA_DSKSYN, fp);
	}
	return IRQ_HANDLED;
}

static irqreturn_t ami_int7(int irq, void *dev_id, struct pt_regs *fp)
{
	panic ("level 7 interrupt received\n");
}

irqreturn_t (*amiga_default_handler[SYS_IRQS])(int, void *, struct pt_regs *) = {
	[1] = ami_int1,
	[3] = ami_int3,
	[4] = ami_int4,
	[5] = ami_int5,
	[7] = ami_int7
};

int show_amiga_interrupts(struct seq_file *p, void *v)
{
	int i;
	irq_node_t *node;

	for (i = IRQ_USER; i < IRQ_AMIGA_CIAA; i++) {
		node = ami_irq_list[i - IRQ_USER];
		if (!node)
			continue;
		seq_printf(p, "ami  %2d: %10u ", i,
		               kstat_cpu(0).irqs[i]);
		do {
			if (node->flags & SA_INTERRUPT)
				seq_puts(p, "F ");
			else
				seq_puts(p, "  ");
			seq_printf(p, "%s\n", node->devname);
			if ((node = node->next))
				seq_puts(p, "                    ");
		} while (node);
	}

	cia_get_irq_list(&ciaa_base, p);
	cia_get_irq_list(&ciab_base, p);
	return 0;
}
