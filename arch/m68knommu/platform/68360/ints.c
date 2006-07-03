/*
 * linux/arch/$(ARCH)/platform/$(PLATFORM)/ints.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Copyright (c) 2000  Michael Leslie <mleslie@lineo.com>
 * Copyright (c) 1996 Roman Zippel
 * Copyright (c) 1999 D. Jeff Dionne <jeff@uclinux.org>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/irqnode.h>
#include <asm/traps.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/setup.h>
#include <asm/m68360.h>

/* from quicc/commproc.c: */
extern QUICC *pquicc;
extern void cpm_interrupt_init(void);

#define INTERNAL_IRQS (96)

/* assembler routines */
asmlinkage void system_call(void);
asmlinkage void buserr(void);
asmlinkage void trap(void);
asmlinkage irqreturn_t bad_interrupt(void);
asmlinkage irqreturn_t inthandler(void);

extern void *_ramvec[];

/* The number of spurious interrupts */
volatile unsigned int num_spurious;
unsigned int local_irq_count[NR_CPUS];

/* irq node variables for the 32 (potential) on chip sources */
static irq_node_t int_irq_list[INTERNAL_IRQS];

static short int_irq_ablecount[INTERNAL_IRQS];

/*
 * This function should be called during kernel startup to initialize
 * IRQ handling routines.
 */

void init_IRQ(void)
{
	int i;
	int vba = (CPM_VECTOR_BASE<<4);

	/* set up the vectors */
	_ramvec[2] = buserr;
	_ramvec[3] = trap;
	_ramvec[4] = trap;
	_ramvec[5] = trap;
	_ramvec[6] = trap;
	_ramvec[7] = trap;
	_ramvec[8] = trap;
	_ramvec[9] = trap;
	_ramvec[10] = trap;
	_ramvec[11] = trap;
	_ramvec[12] = trap;
	_ramvec[13] = trap;
	_ramvec[14] = trap;
	_ramvec[15] = trap;

	_ramvec[32] = system_call;
	_ramvec[33] = trap;


	cpm_interrupt_init();

	/* set up CICR for vector base address and irq level */
	/* irl = 4, hp = 1f - see MC68360UM p 7-377 */
	pquicc->intr_cicr = 0x00e49f00 | vba;

	/* CPM interrupt vectors: (p 7-376) */
	_ramvec[vba+CPMVEC_ERROR]       = bad_interrupt; /* Error */
	_ramvec[vba+CPMVEC_PIO_PC11]    = inthandler;   /* pio - pc11 */
	_ramvec[vba+CPMVEC_PIO_PC10]    = inthandler;   /* pio - pc10 */
	_ramvec[vba+CPMVEC_SMC2]        = inthandler;   /* smc2/pip */
	_ramvec[vba+CPMVEC_SMC1]        = inthandler;   /* smc1 */
	_ramvec[vba+CPMVEC_SPI]         = inthandler;   /* spi */
	_ramvec[vba+CPMVEC_PIO_PC9]     = inthandler;   /* pio - pc9 */
	_ramvec[vba+CPMVEC_TIMER4]      = inthandler;   /* timer 4 */
	_ramvec[vba+CPMVEC_RESERVED1]   = inthandler;   /* reserved */
	_ramvec[vba+CPMVEC_PIO_PC8]     = inthandler;   /* pio - pc8 */
	_ramvec[vba+CPMVEC_PIO_PC7]     = inthandler;  /* pio - pc7 */
	_ramvec[vba+CPMVEC_PIO_PC6]     = inthandler;  /* pio - pc6 */
	_ramvec[vba+CPMVEC_TIMER3]      = inthandler;  /* timer 3 */
	_ramvec[vba+CPMVEC_RISCTIMER]   = inthandler;  /* reserved */
	_ramvec[vba+CPMVEC_PIO_PC5]     = inthandler;  /* pio - pc5 */
	_ramvec[vba+CPMVEC_PIO_PC4]     = inthandler;  /* pio - pc4 */
	_ramvec[vba+CPMVEC_RESERVED2]   = inthandler;  /* reserved */
	_ramvec[vba+CPMVEC_RISCTIMER]   = inthandler;  /* timer table */
	_ramvec[vba+CPMVEC_TIMER2]      = inthandler;  /* timer 2 */
	_ramvec[vba+CPMVEC_RESERVED3]   = inthandler;  /* reserved */
	_ramvec[vba+CPMVEC_IDMA2]       = inthandler;  /* idma 2 */
	_ramvec[vba+CPMVEC_IDMA1]       = inthandler;  /* idma 1 */
	_ramvec[vba+CPMVEC_SDMA_CB_ERR] = inthandler;  /* sdma channel bus error */
	_ramvec[vba+CPMVEC_PIO_PC3]     = inthandler;  /* pio - pc3 */
	_ramvec[vba+CPMVEC_PIO_PC2]     = inthandler;  /* pio - pc2 */
	/* _ramvec[vba+CPMVEC_TIMER1]      = cpm_isr_timer1; */  /* timer 1 */
	_ramvec[vba+CPMVEC_TIMER1]      = inthandler;  /* timer 1 */
	_ramvec[vba+CPMVEC_PIO_PC1]     = inthandler;  /* pio - pc1 */
	_ramvec[vba+CPMVEC_SCC4]        = inthandler;  /* scc 4 */
	_ramvec[vba+CPMVEC_SCC3]        = inthandler;  /* scc 3 */
	_ramvec[vba+CPMVEC_SCC2]        = inthandler;  /* scc 2 */
	_ramvec[vba+CPMVEC_SCC1]        = inthandler;  /* scc 1 */
	_ramvec[vba+CPMVEC_PIO_PC0]     = inthandler;  /* pio - pc0 */


	/* turn off all CPM interrupts */
	pquicc->intr_cimr = 0x00000000;

	/* initialize handlers */
	for (i = 0; i < INTERNAL_IRQS; i++) {
		int_irq_list[i].handler = NULL;
		int_irq_list[i].flags   = IRQ_FLG_STD;
		int_irq_list[i].dev_id  = NULL;
		int_irq_list[i].devname = NULL;
	}
}

#if 0
void M68360_insert_irq(irq_node_t **list, irq_node_t *node)
{
	unsigned long flags;
	irq_node_t *cur;

	if (!node->dev_id)
		printk(KERN_INFO "%s: Warning: dev_id of %s is zero\n",
		       __FUNCTION__, node->devname);

	local_irq_save(flags);

	cur = *list;

	while (cur) {
		list = &cur->next;
		cur = cur->next;
	}

	node->next = cur;
	*list = node;

	local_irq_restore(flags);
}

void M68360_delete_irq(irq_node_t **list, void *dev_id)
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
	printk (KERN_INFO "%s: tried to remove invalid irq\n", __FUNCTION__);
}
#endif

int request_irq(
	unsigned int irq,
	irqreturn_t (*handler)(int, void *, struct pt_regs *),
	unsigned long flags,
	const char *devname,
	void *dev_id)
{
	int mask = (1<<irq);

	irq += (CPM_VECTOR_BASE<<4);

	if (irq >= INTERNAL_IRQS) {
		printk (KERN_ERR "%s: Unknown IRQ %d from %s\n", __FUNCTION__, irq, devname);
		return -ENXIO;
	}

	if (!(int_irq_list[irq].flags & IRQ_FLG_STD)) {
		if (int_irq_list[irq].flags & IRQ_FLG_LOCK) {
			printk(KERN_ERR "%s: IRQ %d from %s is not replaceable\n",
			       __FUNCTION__, irq, int_irq_list[irq].devname);
			return -EBUSY;
		}
		if (flags & IRQ_FLG_REPLACE) {
			printk(KERN_ERR "%s: %s can't replace IRQ %d from %s\n",
			       __FUNCTION__, devname, irq, int_irq_list[irq].devname);
			return -EBUSY;
		}
	}
	int_irq_list[irq].handler = handler;
	int_irq_list[irq].flags   = flags;
	int_irq_list[irq].dev_id  = dev_id;
	int_irq_list[irq].devname = devname;

	/* enable in the CIMR */
	if (!int_irq_ablecount[irq])
		pquicc->intr_cimr |= mask;
	/*      *(volatile unsigned long *)0xfffff304 &= ~(1<<irq); */

	return 0;
}

EXPORT_SYMBOL(request_irq);

void free_irq(unsigned int irq, void *dev_id)
{
	if (irq >= INTERNAL_IRQS) {
		printk (KERN_ERR "%s: Unknown IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	if (int_irq_list[irq].dev_id != dev_id)
		printk(KERN_INFO "%s: removing probably wrong IRQ %d from %s\n",
		       __FUNCTION__, irq, int_irq_list[irq].devname);
	int_irq_list[irq].handler = NULL;
	int_irq_list[irq].flags   = IRQ_FLG_STD;
	int_irq_list[irq].dev_id  = NULL;
	int_irq_list[irq].devname = NULL;

	*(volatile unsigned long *)0xfffff304 |= 1<<irq;
}

EXPORT_SYMBOL(free_irq);

#if 0
/*
 * Enable/disable a particular machine specific interrupt source.
 * Note that this may affect other interrupts in case of a shared interrupt.
 * This function should only be called for a _very_ short time to change some
 * internal data, that may not be changed by the interrupt at the same time.
 * int_(enable|disable)_irq calls may also be nested.
 */
void M68360_enable_irq(unsigned int irq)
{
	if (irq >= INTERNAL_IRQS) {
		printk(KERN_ERR "%s: Unknown IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	if (--int_irq_ablecount[irq])
		return;

	/* enable the interrupt */
	*(volatile unsigned long *)0xfffff304 &= ~(1<<irq);
}

void M68360_disable_irq(unsigned int irq)
{
	if (irq >= INTERNAL_IRQS) {
		printk(KERN_ERR "%s: Unknown IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	if (int_irq_ablecount[irq]++)
		return;

	/* disable the interrupt */
	*(volatile unsigned long *)0xfffff304 |= 1<<irq;
}
#endif

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v;

	if (i < NR_IRQS) {
		if (int_irq_list[i].devname) {
			seq_printf(p, "%3d: %10u ", i, kstat_cpu(0).irqs[i]);
			if (int_irq_list[i].flags & IRQ_FLG_LOCK)
				seq_printf(p, "L ");
			else
				seq_printf(p, "  ");
			seq_printf(p, "%s\n", int_irq_list[i].devname);
		}
	}
	if (i == NR_IRQS)
		seq_printf(p, "   : %10u   spurious\n", num_spurious);

	return 0;
}

/* The 68k family did not have a good way to determine the source
 * of interrupts until later in the family.  The EC000 core does
 * not provide the vector number on the stack, we vector everything
 * into one vector and look in the blasted mask register...
 * This code is designed to be fast, almost constant time, not clean!
 */
void process_int(int vec, struct pt_regs *fp)
{
	int irq;
	int mask;

	/* unsigned long pend = *(volatile unsigned long *)0xfffff30c; */

	/* irq = vec + (CPM_VECTOR_BASE<<4); */
	irq = vec;

	/* unsigned long pend = *(volatile unsigned long *)pquicc->intr_cipr; */

	/* Bugger all that weirdness. For the moment, I seem to know where I came from;
	 * vec is passed from a specific ISR, so I'll use it. */

	if (int_irq_list[irq].handler) {
		int_irq_list[irq].handler(irq , int_irq_list[irq].dev_id, fp);
		kstat_cpu(0).irqs[irq]++;
		pquicc->intr_cisr = (1 << vec); /* indicate that irq has been serviced */
	} else {
		printk(KERN_ERR "unregistered interrupt %d!\nTurning it off in the CIMR...\n", irq);
		/* *(volatile unsigned long *)0xfffff304 |= mask; */
		pquicc->intr_cimr &= ~(1 << vec);
		num_spurious += 1;
	}
	return(IRQ_HANDLED);
}
