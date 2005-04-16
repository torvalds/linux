/*
 * linux/arch/m68knommu/platform/68328/ints.c
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of this archive
 * for more details.
 *
 * Copyright 1996 Roman Zippel
 * Copyright 1999 D. Jeff Dionne <jeff@rt-control.com>
 */

#include <linux/module.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/kernel_stat.h>
#include <linux/errno.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/io.h>
#include <asm/machdep.h>
#include <asm/setup.h>

#if defined(CONFIG_M68328)
#include <asm/MC68328.h>
#elif defined(CONFIG_M68EZ328)
#include <asm/MC68EZ328.h>
#elif defined(CONFIG_M68VZ328)
#include <asm/MC68VZ328.h>
#endif

/* assembler routines */
asmlinkage void system_call(void);
asmlinkage void buserr(void);
asmlinkage void trap(void);
asmlinkage void trap3(void);
asmlinkage void trap4(void);
asmlinkage void trap5(void);
asmlinkage void trap6(void);
asmlinkage void trap7(void);
asmlinkage void trap8(void);
asmlinkage void trap9(void);
asmlinkage void trap10(void);
asmlinkage void trap11(void);
asmlinkage void trap12(void);
asmlinkage void trap13(void);
asmlinkage void trap14(void);
asmlinkage void trap15(void);
asmlinkage void trap33(void);
asmlinkage void trap34(void);
asmlinkage void trap35(void);
asmlinkage void trap36(void);
asmlinkage void trap37(void);
asmlinkage void trap38(void);
asmlinkage void trap39(void);
asmlinkage void trap40(void);
asmlinkage void trap41(void);
asmlinkage void trap42(void);
asmlinkage void trap43(void);
asmlinkage void trap44(void);
asmlinkage void trap45(void);
asmlinkage void trap46(void);
asmlinkage void trap47(void);
asmlinkage irqreturn_t bad_interrupt(int, void *, struct pt_regs *);
asmlinkage irqreturn_t inthandler(void);
asmlinkage irqreturn_t inthandler1(void);
asmlinkage irqreturn_t inthandler2(void);
asmlinkage irqreturn_t inthandler3(void);
asmlinkage irqreturn_t inthandler4(void);
asmlinkage irqreturn_t inthandler5(void);
asmlinkage irqreturn_t inthandler6(void);
asmlinkage irqreturn_t inthandler7(void);

extern e_vector *_ramvec;

/* The number of spurious interrupts */
volatile unsigned int num_spurious;
unsigned int local_irq_count[NR_CPUS];

/* irq node variables for the 32 (potential) on chip sources */
static irq_node_t int_irq_list[NR_IRQS];

#if !defined(CONFIG_DRAGEN2)
asm (".global _start, __ramend/n/t"
     ".section .romvec/n"
     "e_vectors:\n\t"
     ".long __ramend-4, _start, buserr, trap, trap, trap, trap, trap\n\t"
     ".long trap, trap, trap, trap, trap, trap, trap, trap\n\t"
     ".long trap, trap, trap, trap, trap, trap, trap, trap\n\t"
     ".long trap, trap, trap, trap\n\t"
     ".long trap, trap, trap, trap\n\t"
	/*.long inthandler, inthandler, inthandler, inthandler
	.long inthandler4, inthandler, inthandler, inthandler   */
	/* TRAP #0-15 */
     ".long system_call, trap, trap, trap, trap, trap, trap, trap\n\t"
     ".long trap, trap, trap, trap, trap, trap, trap, trap\n\t"
     ".long 0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,0\n\t"
     ".text\n"
     "ignore: rte");
#endif

/*
 * This function should be called during kernel startup to initialize
 * the IRQ handling routines.
 */
void init_IRQ(void)
{
	int i;

	/* set up the vectors */
	for (i = 72; i < 256; ++i)
		_ramvec[i] = (e_vector) bad_interrupt;

	_ramvec[32] = system_call;

	_ramvec[65] = (e_vector) inthandler1;
	_ramvec[66] = (e_vector) inthandler2;
	_ramvec[67] = (e_vector) inthandler3;
	_ramvec[68] = (e_vector) inthandler4;
	_ramvec[69] = (e_vector) inthandler5;
	_ramvec[70] = (e_vector) inthandler6;
	_ramvec[71] = (e_vector) inthandler7;
 
	IVR = 0x40; /* Set DragonBall IVR (interrupt base) to 64 */

	/* initialize handlers */
	for (i = 0; i < NR_IRQS; i++) {
		int_irq_list[i].handler = bad_interrupt;
		int_irq_list[i].flags   = IRQ_FLG_STD;
		int_irq_list[i].dev_id  = NULL;
		int_irq_list[i].devname = NULL;
	}

	/* turn off all interrupts */
	IMR = ~0;
}

int request_irq(
	unsigned int irq,
	irqreturn_t (*handler)(int, void *, struct pt_regs *),
	unsigned long flags,
	const char *devname,
	void *dev_id)
{
	if (irq >= NR_IRQS) {
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

	IMR &= ~(1<<irq);

	return 0;
}

EXPORT_SYMBOL(request_irq);

void free_irq(unsigned int irq, void *dev_id)
{
	if (irq >= NR_IRQS) {
		printk (KERN_ERR "%s: Unknown IRQ %d\n", __FUNCTION__, irq);
		return;
	}

	if (int_irq_list[irq].dev_id != dev_id)
		printk(KERN_INFO "%s: removing probably wrong IRQ %d from %s\n",
		       __FUNCTION__, irq, int_irq_list[irq].devname);

	int_irq_list[irq].handler = bad_interrupt;
	int_irq_list[irq].flags   = IRQ_FLG_STD;
	int_irq_list[irq].dev_id  = NULL;
	int_irq_list[irq].devname = NULL;

	IMR |= 1<<irq;
}

EXPORT_SYMBOL(free_irq);

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

	unsigned long pend = ISR;

	while (pend) {
		if (pend & 0x0000ffff) {
			if (pend & 0x000000ff) {
				if (pend & 0x0000000f) {
					mask = 0x00000001;
					irq = 0;
				} else {
					mask = 0x00000010;
					irq = 4;
				}
			} else {
				if (pend & 0x00000f00) {
					mask = 0x00000100;
					irq = 8;
				} else {
					mask = 0x00001000;
					irq = 12;
				}
			}
		} else {
			if (pend & 0x00ff0000) {
				if (pend & 0x000f0000) {
					mask = 0x00010000;
					irq = 16;
				} else {
					mask = 0x00100000;
					irq = 20;
				}
			} else {
				if (pend & 0x0f000000) {
					mask = 0x01000000;
					irq = 24;
				} else {
					mask = 0x10000000;
					irq = 28;
				}
			}
		}

		while (! (mask & pend)) {
			mask <<=1;
			irq++;
		}

		kstat_cpu(0).irqs[irq]++;

		if (int_irq_list[irq].handler) {
			int_irq_list[irq].handler(irq, int_irq_list[irq].dev_id, fp);
		} else {
			printk(KERN_ERR "unregistered interrupt %d!\nTurning it off in the IMR...\n", irq);
			IMR |= mask;
		}
		pend &= ~mask;
	}
}
