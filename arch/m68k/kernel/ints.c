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

#include <linux/config.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/init.h>

#include <asm/setup.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/page.h>
#include <asm/machdep.h>

#ifdef CONFIG_Q40
#include <asm/q40ints.h>
#endif

/* table for system interrupt handlers */
static struct irq_node *irq_list[SYS_IRQS];
static struct irq_controller *irq_controller[SYS_IRQS];

static struct irq_controller auto_irq_controller = {
	.name		= "auto",
	.lock		= SPIN_LOCK_UNLOCKED,
	.startup	= m68k_irq_startup,
	.shutdown	= m68k_irq_shutdown,
};

static const char *default_names[SYS_IRQS] = {
	[0] = "spurious int",
	[1] = "int1 handler",
	[2] = "int2 handler",
	[3] = "int3 handler",
	[4] = "int4 handler",
	[5] = "int5 handler",
	[6] = "int6 handler",
	[7] = "int7 handler"
};

/* The number of spurious interrupts */
volatile unsigned int num_spurious;

#define NUM_IRQ_NODES 100
static irq_node_t nodes[NUM_IRQ_NODES];

static void dummy_enable_irq(unsigned int irq);
static void dummy_disable_irq(unsigned int irq);
static int dummy_request_irq(unsigned int irq,
		irqreturn_t (*handler) (int, void *, struct pt_regs *),
		unsigned long flags, const char *devname, void *dev_id);
static void dummy_free_irq(unsigned int irq, void *dev_id);

void (*enable_irq) (unsigned int) = dummy_enable_irq;
void (*disable_irq) (unsigned int) = dummy_disable_irq;

int (*mach_request_irq) (unsigned int, irqreturn_t (*)(int, void *, struct pt_regs *),
                      unsigned long, const char *, void *) = dummy_request_irq;
void (*mach_free_irq) (unsigned int, void *) = dummy_free_irq;

void init_irq_proc(void);

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

	for (i = IRQ_AUTO_1; i <= IRQ_AUTO_7; i++) {
		irq_controller[i] = &auto_irq_controller;
		if (mach_default_handler && (*mach_default_handler)[i])
			cpu_request_irq(i, (*mach_default_handler)[i],
					0, default_names[i], NULL);
	}

	mach_init_IRQ ();
}

irq_node_t *new_irq_node(void)
{
	irq_node_t *node;
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

/*
 * We will keep these functions until I have convinced Linus to move
 * the declaration of them from include/linux/sched.h to
 * include/asm/irq.h.
 */
int request_irq(unsigned int irq,
		irqreturn_t (*handler) (int, void *, struct pt_regs *),
		unsigned long flags, const char *devname, void *dev_id)
{
	return mach_request_irq(irq, handler, flags, devname, dev_id);
}

EXPORT_SYMBOL(request_irq);

void free_irq(unsigned int irq, void *dev_id)
{
	mach_free_irq(irq, dev_id);
}

EXPORT_SYMBOL(free_irq);

int setup_irq(unsigned int irq, struct irq_node *node)
{
	struct irq_controller *contr;
	struct irq_node **prev;
	unsigned long flags;

	if (irq >= SYS_IRQS || !(contr = irq_controller[irq])) {
		printk("%s: Incorrect IRQ %d from %s\n",
		       __FUNCTION__, irq, node->devname);
		return -ENXIO;
	}

	spin_lock_irqsave(&contr->lock, flags);

	prev = irq_list + irq;
	if (*prev) {
		/* Can't share interrupts unless both agree to */
		if (!((*prev)->flags & node->flags & SA_SHIRQ)) {
			spin_unlock_irqrestore(&contr->lock, flags);
			return -EBUSY;
		}
		while (*prev)
			prev = &(*prev)->next;
	}

	if (!irq_list[irq]) {
		if (contr->startup)
			contr->startup(irq);
		else
			contr->enable(irq);
	}
	node->next = NULL;
	*prev = node;

	spin_unlock_irqrestore(&contr->lock, flags);

	return 0;
}

int cpu_request_irq(unsigned int irq,
                    irqreturn_t (*handler)(int, void *, struct pt_regs *),
                    unsigned long flags, const char *devname, void *dev_id)
{
	struct irq_node *node;
	int res;

	node = new_irq_node();
	if (!node)
		return -ENOMEM;

	node->handler = handler;
	node->flags   = flags;
	node->dev_id  = dev_id;
	node->devname = devname;

	res = setup_irq(irq, node);
	if (res)
		node->handler = NULL;

	return res;
}

void cpu_free_irq(unsigned int irq, void *dev_id)
{
	struct irq_controller *contr;
	struct irq_node **p, *node;
	unsigned long flags;

	if (irq >= SYS_IRQS || !(contr = irq_controller[irq])) {
		printk("%s: Incorrect IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	spin_lock_irqsave(&contr->lock, flags);

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
		       __FUNCTION__, irq);

	if (!irq_list[irq])
		contr->shutdown(irq);

	spin_unlock_irqrestore(&contr->lock, flags);
}

int m68k_irq_startup(unsigned int irq)
{
	if (irq <= IRQ_AUTO_7)
		vectors[VEC_SPUR + irq] = auto_inthandler;
	return 0;
}

void m68k_irq_shutdown(unsigned int irq)
{
	if (irq <= IRQ_AUTO_7)
		vectors[VEC_SPUR + irq] = bad_inthandler;
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

static void dummy_enable_irq(unsigned int irq)
{
	printk("calling uninitialized enable_irq()\n");
}

static void dummy_disable_irq(unsigned int irq)
{
	printk("calling uninitialized disable_irq()\n");
}

static int dummy_request_irq(unsigned int irq,
		irqreturn_t (*handler) (int, void *, struct pt_regs *),
		unsigned long flags, const char *devname, void *dev_id)
{
	printk("calling uninitialized request_irq()\n");
	return 0;
}

static void dummy_free_irq(unsigned int irq, void *dev_id)
{
	printk("calling uninitialized disable_irq()\n");
}

asmlinkage void m68k_handle_int(unsigned int irq, struct pt_regs *regs)
{
	struct irq_node *node;

	kstat_cpu(0).irqs[irq]++;
	node = irq_list[irq];
	do {
		node->handler(irq, node->dev_id, regs);
		node = node->next;
	} while (node);
}

asmlinkage void handle_badint(struct pt_regs *regs)
{
	kstat_cpu(0).irqs[0]++;
	printk("unexpected interrupt from %u\n", regs->vector);
}

int show_interrupts(struct seq_file *p, void *v)
{
	struct irq_controller *contr;
	struct irq_node *node;
	int i = *(loff_t *) v;

	/* autovector interrupts */
	if (i < SYS_IRQS && irq_list[i]) {
		contr = irq_controller[i];
		node = irq_list[i];
		seq_printf(p, "%s %u: %10u %s", contr->name, i, kstat_cpu(0).irqs[i], node->devname);
		while ((node = node->next))
			seq_printf(p, ", %s", node->devname);
		seq_puts(p, "\n");
	} else if (i == SYS_IRQS)
		mach_get_irq_list(p, v);
	return 0;
}

void init_irq_proc(void)
{
	/* Insert /proc/irq driver here */
}

