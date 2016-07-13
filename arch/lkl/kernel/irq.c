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

static unsigned long irq_status;
static bool irqs_enabled;

#define TEST_AND_CLEAR_IRQ_STATUS(x)	__sync_fetch_and_and(&irq_status, 0)
#define IRQ_BIT(x)			BIT(x-1)
#define SET_IRQ_STATUS(x)		__sync_fetch_and_or(&irq_status, BIT(x - 1))

static struct irq_info {
	const char *user;
} irqs[NR_IRQS];

/**
 * DO NOT run any linux calls (e.g. printk) here as they may race with the
 * existing linux threads.
 */
int lkl_trigger_irq(int irq)
{
	if (!irq || irq > NR_IRQS)
		return -EINVAL;

	SET_IRQ_STATUS(irq);

	wakeup_cpu();

	return 0;
}

static void run_irqs(void)
{
	int i = 1;
	unsigned long status;

	if (!irq_status)
		return;

	status = TEST_AND_CLEAR_IRQ_STATUS(IRQS_MASK);

	while (status) {
		if (status & 1) {
			irq_enter();
			generic_handle_irq(i);
			irq_exit();
		}
		status = status >> 1;
		i++;
	}
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

void init_IRQ(void)
{
	int i;

	for (i = 0; i < NR_IRQS; i++)
		irq_set_chip_and_handler(i, &dummy_irq_chip, handle_simple_irq);

	pr_info("lkl: irqs initialized\n");
}

void cpu_yield_to_irqs(void)
{
	cpu_relax();
}
