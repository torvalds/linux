#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/irq.h>
#include <linux/hardirq.h>
#include <asm/irq_regs.h>
#include <linux/sched.h>
#include <linux/seq_file.h>
#include <linux/tick.h>
#include <asm/irqflags.h>
#include <asm/host_ops.h>

static bool irqs_enabled;

#define MAX_IRQS	16
static struct irq_info {
	struct pt_regs regs[MAX_IRQS];
	const char *user;
	int count;
} irqs[NR_IRQS];
static void *irqs_lock;

static void do_IRQ(int irq, struct pt_regs *regs)
{
	struct pt_regs *old_regs = set_irq_regs(regs);

	irq_enter();
	generic_handle_irq(irq);
	irq_exit();

	set_irq_regs(old_regs);
}

int lkl_trigger_irq(int irq, void *data)
{
	struct pt_regs regs = {
		.irq_data = data,
	};
	int ret = 0;

	if (irq >= NR_IRQS)
		return -EINVAL;

	lkl_ops->sem_down(irqs_lock);
	if (irqs[irq].count < MAX_IRQS) {
		irqs[irq].regs[irqs[irq].count] = regs;
		irqs[irq].count++;
	} else {
		ret = -EOVERFLOW;
	}
	lkl_ops->sem_up(irqs_lock);

	wakeup_cpu();

	return ret;
}

static void run_irqs(void)
{
	int i, j;

	lkl_ops->sem_down(irqs_lock);
	for (i = 0; i < NR_IRQS; i++) {
		for (j = 0; j < irqs[i].count; j++)
			do_IRQ(i, &irqs[i].regs[j]);
		irqs[i].count = 0;
	}
	lkl_ops->sem_up(irqs_lock);
}

int show_interrupts(struct seq_file *p, void *v)
{
	return 0;
}

int lkl_get_free_irq(const char *user)
{
	int i;
	int ret = -EBUSY;

	/* 0 is not a valid IRQ */
	for (i = 1; i < NR_IRQS; i++) {
		if (!irqs[i].user) {
			irqs[i].user = user;
			ret = i;
			break;
		}
	}

	return ret;
}

void lkl_put_irq(int i, const char *user)
{
	if (!irqs[i].user || strcmp(irqs[i].user, user) != 0) {
		WARN("%s tried to release %s's irq %d", user, irqs[i].user, i);
		return;
	}

	irqs[i].user = NULL;
}

unsigned long arch_local_save_flags(void)
{
	return irqs_enabled;
}

void arch_local_irq_restore(unsigned long flags)
{
	if (flags == ARCH_IRQ_ENABLED && irqs_enabled == ARCH_IRQ_DISABLED &&
	    !in_interrupt())
		run_irqs();
	irqs_enabled = flags;
}

void free_IRQ(void)
{
	lkl_ops->sem_free(irqs_lock);
}

void init_IRQ(void)
{
	int i;

	irqs_lock = lkl_ops->sem_alloc(1);
	BUG_ON(!irqs_lock);

	for (i = 0; i < NR_IRQS; i++)
		irq_set_chip_and_handler(i, &dummy_irq_chip, handle_simple_irq);

	pr_info("lkl: irqs initialized\n");
}

void cpu_yield_to_irqs(void)
{
	cpu_relax();
}
