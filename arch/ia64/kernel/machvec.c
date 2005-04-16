#include <linux/config.h>
#include <linux/module.h>

#include <asm/machvec.h>
#include <asm/system.h>

#ifdef CONFIG_IA64_GENERIC

#include <linux/kernel.h>
#include <linux/string.h>

#include <asm/page.h>

struct ia64_machine_vector ia64_mv;
EXPORT_SYMBOL(ia64_mv);

static struct ia64_machine_vector *
lookup_machvec (const char *name)
{
	extern struct ia64_machine_vector machvec_start[];
	extern struct ia64_machine_vector machvec_end[];
	struct ia64_machine_vector *mv;

	for (mv = machvec_start; mv < machvec_end; ++mv)
		if (strcmp (mv->name, name) == 0)
			return mv;

	return 0;
}

void
machvec_init (const char *name)
{
	struct ia64_machine_vector *mv;

	mv = lookup_machvec(name);
	if (!mv) {
		panic("generic kernel failed to find machine vector for platform %s!", name);
	}
	ia64_mv = *mv;
	printk(KERN_INFO "booting generic kernel on platform %s\n", name);
}

#endif /* CONFIG_IA64_GENERIC */

void
machvec_setup (char **arg)
{
}
EXPORT_SYMBOL(machvec_setup);

void
machvec_timer_interrupt (int irq, void *dev_id, struct pt_regs *regs)
{
}
EXPORT_SYMBOL(machvec_timer_interrupt);

void
machvec_dma_sync_single (struct device *hwdev, dma_addr_t dma_handle, size_t size, int dir)
{
	mb();
}
EXPORT_SYMBOL(machvec_dma_sync_single);

void
machvec_dma_sync_sg (struct device *hwdev, struct scatterlist *sg, int n, int dir)
{
	mb();
}
EXPORT_SYMBOL(machvec_dma_sync_sg);
