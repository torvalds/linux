/*
 *  arch/ppc/amiga/ints.c
 *
 *  Linux/m68k general interrupt handling code from arch/m68k/kernel/ints.c
 *  Needed to drive the m68k emulating IRQ hardware on the PowerUp boards.
 */

#include <linux/types.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/seq_file.h>

#include <asm/setup.h>
#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/page.h>
#include <asm/machdep.h>

/* table for system interrupt handlers */
static irq_handler_t irq_list[SYS_IRQS];

static const char *default_names[SYS_IRQS] = {
	"spurious int", "int1 handler", "int2 handler", "int3 handler",
	"int4 handler", "int5 handler", "int6 handler", "int7 handler"
};

/* The number of spurious interrupts */
volatile unsigned int num_spurious;

#define NUM_IRQ_NODES 100
static irq_node_t nodes[NUM_IRQ_NODES];


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

__init
void m68k_init_IRQ(void)
{
	int i;

	for (i = 0; i < SYS_IRQS; i++) {
		if (mach_default_handler)
			irq_list[i].handler = (*mach_default_handler)[i];
		irq_list[i].flags   = 0;
		irq_list[i].dev_id  = NULL;
		irq_list[i].devname = default_names[i];
	}

	for (i = 0; i < NUM_IRQ_NODES; i++)
		nodes[i].handler = NULL;

	mach_init_IRQ ();
}

irq_node_t *new_irq_node(void)
{
	irq_node_t *node;
	short i;

	for (node = nodes, i = NUM_IRQ_NODES-1; i >= 0; node++, i--)
		if (!node->handler)
			return node;

	printk ("new_irq_node: out of nodes\n");
	return NULL;
}

int sys_request_irq(unsigned int irq,
                    void (*handler)(int, void *, struct pt_regs *),
                    unsigned long flags, const char *devname, void *dev_id)
{
	if (irq < IRQ1 || irq > IRQ7) {
		printk("%s: Incorrect IRQ %d from %s\n",
		       __FUNCTION__, irq, devname);
		return -ENXIO;
	}

#if 0
	if (!(irq_list[irq].flags & IRQ_FLG_STD)) {
		if (irq_list[irq].flags & IRQ_FLG_LOCK) {
			printk("%s: IRQ %d from %s is not replaceable\n",
			       __FUNCTION__, irq, irq_list[irq].devname);
			return -EBUSY;
		}
		if (!(flags & IRQ_FLG_REPLACE)) {
			printk("%s: %s can't replace IRQ %d from %s\n",
			       __FUNCTION__, devname, irq, irq_list[irq].devname);
			return -EBUSY;
		}
	}
#endif

	irq_list[irq].handler = handler;
	irq_list[irq].flags   = flags;
	irq_list[irq].dev_id  = dev_id;
	irq_list[irq].devname = devname;
	return 0;
}

void sys_free_irq(unsigned int irq, void *dev_id)
{
	if (irq < IRQ1 || irq > IRQ7) {
		printk("%s: Incorrect IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	if (irq_list[irq].dev_id != dev_id)
		printk("%s: Removing probably wrong IRQ %d from %s\n",
		       __FUNCTION__, irq, irq_list[irq].devname);

	irq_list[irq].handler = (*mach_default_handler)[irq];
	irq_list[irq].flags   = 0;
	irq_list[irq].dev_id  = NULL;
	irq_list[irq].devname = default_names[irq];
}

asmlinkage void process_int(unsigned long vec, struct pt_regs *fp)
{
	if (vec >= VEC_INT1 && vec <= VEC_INT7 && !MACH_IS_BVME6000) {
		vec -= VEC_SPUR;
		kstat_cpu(0).irqs[vec]++;
		irq_list[vec].handler(vec, irq_list[vec].dev_id, fp);
	} else {
		if (mach_process_int)
			mach_process_int(vec, fp);
		else
			panic("Can't process interrupt vector %ld\n", vec);
		return;
	}
}

int m68k_get_irq_list(struct seq_file *p, void *v)
{
	int i;

	/* autovector interrupts */
	if (mach_default_handler) {
		for (i = 0; i < SYS_IRQS; i++) {
			seq_printf(p, "auto %2d: %10u ", i,
			               i ? kstat_cpu(0).irqs[i] : num_spurious);
			seq_puts(p, "  ");
			seq_printf(p, "%s\n", irq_list[i].devname);
		}
	}

	mach_get_irq_list(p, v);
	return 0;
}
