/*
 * linux/arch/m68k/kernel/ints.c -- Linux/m68k general interrupt handling code
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * 07/03/96: Timer initialization, and thus mach_sched_init(),
 *           removed from request_irq() and moved to init_time().
 *           We should therefore consider renaming our add_isr() and
 *           remove_isr() to request_irq() and free_irq()
 *           respectively, so they are compliant with the other
 *           architectures.                                     /Jes
 * 11/07/96: Changed all add_/remove_isr() to request_/free_irq() calls.
 *           Removed irq list support, if any machine needs an irq server
 *           it must implement this itself (as it's already done), instead
 *           only default handler are used with mach_default_handler.
 *           request_irq got some flags different from other architectures:
 *           - IRQ_FLG_REPLACE : Replace an existing handler (the default one
 *                               can be replaced without this flag)
 *           - IRQ_FLG_LOCK : handler can't be replaced
 *           There are other machine depending flags, see there
 *           If you want to replace a default handler you should know what
 *           you're doing, since it might handle different other irq sources
 *           which must be served                               /Roman Zippel
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/page.h>
#include <asm/machdep.h>
#include <asm/cacheflush.h>
#include <asm/irq_regs.h>

#ifdef CONFIG_Q40
#include <asm/q40ints.h>
#endif

extern u32 auto_irqhandler_fixup[];
extern u32 user_irqhandler_fixup[];
extern u16 user_irqvec_fixup[];

/* table for system interrupt handlers */
static struct irq_data *irq_list[NR_IRQS];
static struct irq_chip *irq_chip[NR_IRQS];
static int irq_depth[NR_IRQS];

static inline int irq_set_chip(unsigned int irq, struct irq_chip *chip)
{
	irq_chip[irq] = chip;
	return 0;
}

static int m68k_first_user_vec;

static struct irq_chip auto_irq_chip = {
	.name		= "auto",
	.irq_startup	= m68k_irq_startup,
	.irq_shutdown	= m68k_irq_shutdown,
};

static struct irq_chip user_irq_chip = {
	.name		= "user",
	.irq_startup	= m68k_irq_startup,
	.irq_shutdown	= m68k_irq_shutdown,
};

#define NUM_IRQ_NODES 100
static struct irq_data nodes[NUM_IRQ_NODES];

/*
 * void init_IRQ(void)
 *
 * Parameters:	None
 *
 * Returns:	Nothing
 *
 * This function should be called during kernel startup to initialize
 * the IRQ handling routines.
 */

void __init init_IRQ(void)
{
	int i;

	/* assembly irq entry code relies on this... */
	if (HARDIRQ_MASK != 0x00ff0000) {
		extern void hardirq_mask_is_broken(void);
		hardirq_mask_is_broken();
	}

	for (i = IRQ_AUTO_1; i <= IRQ_AUTO_7; i++)
		irq_set_chip(i, &auto_irq_chip);

	mach_init_IRQ();
}

/**
 * m68k_setup_auto_interrupt
 * @handler: called from auto vector interrupts
 *
 * setup the handler to be called from auto vector interrupts instead of the
 * standard do_IRQ(), it will be called with irq numbers in the range
 * from IRQ_AUTO_1 - IRQ_AUTO_7.
 */
void __init m68k_setup_auto_interrupt(void (*handler)(unsigned int, struct pt_regs *))
{
	if (handler)
		*auto_irqhandler_fixup = (u32)handler;
	flush_icache();
}

/**
 * m68k_setup_user_interrupt
 * @vec: first user vector interrupt to handle
 * @cnt: number of active user vector interrupts
 * @handler: called from user vector interrupts
 *
 * setup user vector interrupts, this includes activating the specified range
 * of interrupts, only then these interrupts can be requested (note: this is
 * different from auto vector interrupts). An optional handler can be installed
 * to be called instead of the default do_IRQ(), it will be called
 * with irq numbers starting from IRQ_USER.
 */
void __init m68k_setup_user_interrupt(unsigned int vec, unsigned int cnt,
				      void (*handler)(unsigned int, struct pt_regs *))
{
	int i;

	BUG_ON(IRQ_USER + cnt > NR_IRQS);
	m68k_first_user_vec = vec;
	for (i = 0; i < cnt; i++)
		irq_set_chip(IRQ_USER + i, &user_irq_chip);
	*user_irqvec_fixup = vec - IRQ_USER;
	if (handler)
		*user_irqhandler_fixup = (u32)handler;
	flush_icache();
}

/**
 * m68k_setup_irq_chip
 * @contr: irq controller which controls specified irq
 * @irq: first irq to be managed by the controller
 *
 * Change the controller for the specified range of irq, which will be used to
 * manage these irq. auto/user irq already have a default controller, which can
 * be changed as well, but the controller probably should use m68k_irq_startup/
 * m68k_irq_shutdown.
 */
void m68k_setup_irq_chip(struct irq_chip *contr, unsigned int irq,
			       unsigned int cnt)
{
	int i;

	for (i = 0; i < cnt; i++)
		irq_set_chip(irq + i, contr);
}

struct irq_data *new_irq_node(void)
{
	struct irq_data *node;
	short i;

	for (node = nodes, i = NUM_IRQ_NODES-1; i >= 0; node++, i--) {
		if (!node->handler) {
			memset(node, 0, sizeof(*node));
			return node;
		}
	}

	printk ("new_irq_node: out of nodes\n");
	return NULL;
}

static int m68k_setup_irq(unsigned int irq, struct irq_data *node)
{
	struct irq_chip *contr;
	struct irq_data **prev;
	unsigned long flags;

	if (irq >= NR_IRQS || !(contr = irq_chip[irq])) {
		printk("%s: Incorrect IRQ %d from %s\n",
		       __func__, irq, node->devname);
		return -ENXIO;
	}

	local_irq_save(flags);

	prev = irq_list + irq;
	if (*prev) {
		/* Can't share interrupts unless both agree to */
		if (!((*prev)->flags & node->flags & IRQF_SHARED)) {
			local_irq_restore(flags);
			return -EBUSY;
		}
		while (*prev)
			prev = &(*prev)->next;
	}

	if (!irq_list[irq]) {
		if (contr->irq_startup)
			contr->irq_startup(node);
		else
			contr->irq_enable(node);
	}
	node->next = NULL;
	*prev = node;

	local_irq_restore(flags);

	return 0;
}

int request_irq(unsigned int irq,
		irq_handler_t handler,
		unsigned long flags, const char *devname, void *dev_id)
{
	struct irq_data *node;
	int res;

	node = new_irq_node();
	if (!node)
		return -ENOMEM;

	node->irq     = irq;
	node->handler = handler;
	node->flags   = flags;
	node->dev_id  = dev_id;
	node->devname = devname;

	res = m68k_setup_irq(irq, node);
	if (res)
		node->handler = NULL;

	return res;
}

EXPORT_SYMBOL(request_irq);

void free_irq(unsigned int irq, void *dev_id)
{
	struct irq_chip *contr;
	struct irq_data **p, *node;
	unsigned long flags;

	if (irq >= NR_IRQS || !(contr = irq_chip[irq])) {
		printk("%s: Incorrect IRQ %d\n", __func__, irq);
		return;
	}

	local_irq_save(flags);

	p = irq_list + irq;
	while ((node = *p)) {
		if (node->dev_id == dev_id)
			break;
		p = &node->next;
	}

	if (node) {
		*p = node->next;
		node->handler = NULL;
	} else
		printk("%s: Removing probably wrong IRQ %d\n",
		       __func__, irq);

	if (!irq_list[irq]) {
		if (contr->irq_shutdown)
			contr->irq_shutdown(node);
		else
			contr->irq_disable(node);
	}

	local_irq_restore(flags);
}

EXPORT_SYMBOL(free_irq);

void enable_irq(unsigned int irq)
{
	struct irq_chip *contr;
	unsigned long flags;

	if (irq >= NR_IRQS || !(contr = irq_chip[irq])) {
		printk("%s: Incorrect IRQ %d\n",
		       __func__, irq);
		return;
	}

	local_irq_save(flags);
	if (irq_depth[irq]) {
		if (!--irq_depth[irq]) {
			if (contr->irq_enable)
				contr->irq_enable(irq_list[irq]);
		}
	} else
		WARN_ON(1);
	local_irq_restore(flags);
}

EXPORT_SYMBOL(enable_irq);

void disable_irq(unsigned int irq)
{
	struct irq_chip *contr;
	unsigned long flags;

	if (irq >= NR_IRQS || !(contr = irq_chip[irq])) {
		printk("%s: Incorrect IRQ %d\n",
		       __func__, irq);
		return;
	}

	local_irq_save(flags);
	if (!irq_depth[irq]++) {
		if (contr->irq_disable)
			contr->irq_disable(irq_list[irq]);
	}
	local_irq_restore(flags);
}

EXPORT_SYMBOL(disable_irq);

void disable_irq_nosync(unsigned int irq) __attribute__((alias("disable_irq")));

EXPORT_SYMBOL(disable_irq_nosync);

unsigned int m68k_irq_startup_irq(unsigned int irq)
{
	if (irq <= IRQ_AUTO_7)
		vectors[VEC_SPUR + irq] = auto_inthandler;
	else
		vectors[m68k_first_user_vec + irq - IRQ_USER] = user_inthandler;
	return 0;
}

unsigned int m68k_irq_startup(struct irq_data *data)
{
	return m68k_irq_startup_irq(data->irq);
}

void m68k_irq_shutdown(struct irq_data *data)
{
	unsigned int irq = data->irq;

	if (irq <= IRQ_AUTO_7)
		vectors[VEC_SPUR + irq] = bad_inthandler;
	else
		vectors[m68k_first_user_vec + irq - IRQ_USER] = bad_inthandler;
}


/*
 * Do we need these probe functions on the m68k?
 *
 *  ... may be useful with ISA devices
 */
unsigned long probe_irq_on (void)
{
#ifdef CONFIG_Q40
	if (MACH_IS_Q40)
		return q40_probe_irq_on();
#endif
	return 0;
}

EXPORT_SYMBOL(probe_irq_on);

int probe_irq_off (unsigned long irqs)
{
#ifdef CONFIG_Q40
	if (MACH_IS_Q40)
		return q40_probe_irq_off(irqs);
#endif
	return 0;
}

EXPORT_SYMBOL(probe_irq_off);

unsigned int irq_canonicalize(unsigned int irq)
{
#ifdef CONFIG_Q40
	if (MACH_IS_Q40 && irq == 11)
		irq = 10;
#endif
	return irq;
}

EXPORT_SYMBOL(irq_canonicalize);

void generic_handle_irq(unsigned int irq)
{
	struct irq_data *node;
	kstat_cpu(0).irqs[irq]++;
	node = irq_list[irq];
	do {
		node->handler(irq, node->dev_id);
		node = node->next;
	} while (node);
}

asmlinkage void do_IRQ(int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs;
	old_regs = set_irq_regs(regs);
	generic_handle_irq(irq);
	set_irq_regs(old_regs);
}

asmlinkage void handle_badint(struct pt_regs *regs)
{
	kstat_cpu(0).irqs[0]++;
	printk("unexpected interrupt from %u\n", regs->vector);
}

int show_interrupts(struct seq_file *p, void *v)
{
	struct irq_chip *contr;
	struct irq_data *node;
	int i = *(loff_t *) v;

	/* autovector interrupts */
	if (irq_list[i]) {
		contr = irq_chip[i];
		node = irq_list[i];
		seq_printf(p, "%-8s %3u: %10u %s", contr->name, i, kstat_cpu(0).irqs[i], node->devname);
		while ((node = node->next))
			seq_printf(p, ", %s", node->devname);
		seq_puts(p, "\n");
	}
	return 0;
}

#ifdef CONFIG_PROC_FS
void init_irq_proc(void)
{
	/* Insert /proc/irq driver here */
}
#endif
