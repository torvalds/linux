/*
 * linux/arch/h8300/platform/h8s/ints.c
 *
 * Yoshinori Sato <ysato@users.sourceforge.jp>
 *
 * Based on linux/arch/$(ARCH)/platform/$(PLATFORM)/ints.c
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
#include <linux/seq_file.h>
#include <linux/init.h>
#include <linux/bootmem.h>
#include <linux/random.h>
#include <linux/hardirq.h>

#include <asm/system.h>
#include <asm/irq.h>
#include <asm/traps.h>
#include <asm/io.h>
#include <asm/setup.h>
#include <asm/gpio.h>
#include <asm/regs267x.h>
#include <asm/errno.h>

/*
 * This structure has only 4 elements for speed reasons
 */
typedef struct irq_handler {
	irqreturn_t (*handler)(int, void *, struct pt_regs *);
	int         flags;
	int         count;
	void	    *dev_id;
	const char  *devname;
} irq_handler_t;

static irq_handler_t *irq_list[NR_IRQS];

/* IRQ pin assignment */
struct irq_pins {
	unsigned char port_no;
	unsigned char bit_no;
};
/* ISTR = 0 */
const static struct irq_pins irq_assign_table0[16]={
        {H8300_GPIO_P5,H8300_GPIO_B0},{H8300_GPIO_P5,H8300_GPIO_B1},
	{H8300_GPIO_P5,H8300_GPIO_B2},{H8300_GPIO_P5,H8300_GPIO_B3},
	{H8300_GPIO_P5,H8300_GPIO_B4},{H8300_GPIO_P5,H8300_GPIO_B5},
	{H8300_GPIO_P5,H8300_GPIO_B6},{H8300_GPIO_P5,H8300_GPIO_B7},
	{H8300_GPIO_P6,H8300_GPIO_B0},{H8300_GPIO_P6,H8300_GPIO_B1},
	{H8300_GPIO_P6,H8300_GPIO_B2},{H8300_GPIO_P6,H8300_GPIO_B3},
	{H8300_GPIO_P6,H8300_GPIO_B4},{H8300_GPIO_P6,H8300_GPIO_B5},
	{H8300_GPIO_PF,H8300_GPIO_B1},{H8300_GPIO_PF,H8300_GPIO_B2},
};
/* ISTR = 1 */
const static struct irq_pins irq_assign_table1[16]={
	{H8300_GPIO_P8,H8300_GPIO_B0},{H8300_GPIO_P8,H8300_GPIO_B1},
	{H8300_GPIO_P8,H8300_GPIO_B2},{H8300_GPIO_P8,H8300_GPIO_B3},
	{H8300_GPIO_P8,H8300_GPIO_B4},{H8300_GPIO_P8,H8300_GPIO_B5},
	{H8300_GPIO_PH,H8300_GPIO_B2},{H8300_GPIO_PH,H8300_GPIO_B3},
	{H8300_GPIO_P2,H8300_GPIO_B0},{H8300_GPIO_P2,H8300_GPIO_B1},
	{H8300_GPIO_P2,H8300_GPIO_B2},{H8300_GPIO_P2,H8300_GPIO_B3},
	{H8300_GPIO_P2,H8300_GPIO_B4},{H8300_GPIO_P2,H8300_GPIO_B5},
	{H8300_GPIO_P2,H8300_GPIO_B6},{H8300_GPIO_P2,H8300_GPIO_B7},
};

static short use_kmalloc = 0;

extern unsigned long *interrupt_redirect_table;

#define CPU_VECTOR ((unsigned long *)0x000000)
#define ADDR_MASK (0xffffff)

static inline unsigned long *get_vector_address(void)
{
	volatile unsigned long *rom_vector = CPU_VECTOR;
	unsigned long base,tmp;
	int vec_no;

	base = rom_vector[EXT_IRQ0] & ADDR_MASK;
	
	/* check romvector format */
	for (vec_no = EXT_IRQ1; vec_no <= EXT_IRQ15; vec_no++) {
		if ((base+(vec_no - EXT_IRQ0)*4) != (rom_vector[vec_no] & ADDR_MASK))
			return NULL;
	}

	/* ramvector base address */
	base -= EXT_IRQ0*4;

	/* writerble check */
	tmp = ~(*(unsigned long *)base);
	(*(unsigned long *)base) = tmp;
	if ((*(unsigned long *)base) != tmp)
		return NULL;
	return (unsigned long *)base;
}

void __init init_IRQ(void)
{
#if defined(CONFIG_RAMKERNEL)
	int i;
	unsigned long *ramvec,*ramvec_p;
	unsigned long break_vec;

	ramvec = get_vector_address();
	if (ramvec == NULL)
		panic("interrupt vector serup failed.");
	else
		printk("virtual vector at 0x%08lx\n",(unsigned long)ramvec);

#if defined(CONFIG_GDB_DEBUG)
	/* save orignal break vector */
	break_vec = ramvec[TRAP3_VEC];
#else
	break_vec = VECTOR(trace_break);
#endif

	/* create redirect table */
	for (ramvec_p = ramvec, i = 0; i < NR_IRQS; i++)
		*ramvec_p++ = REDIRECT(interrupt_entry);

	/* set special vector */
	ramvec[TRAP0_VEC] = VECTOR(system_call);
	ramvec[TRAP3_VEC] = break_vec;
	interrupt_redirect_table = ramvec;
#ifdef DUMP_VECTOR
	ramvec_p = ramvec;
	for (i = 0; i < NR_IRQS; i++) {
		if ((i % 8) == 0)
			printk("\n%p: ",ramvec_p);
		printk("%p ",*ramvec_p);
		ramvec_p++;
	}
	printk("\n");
#endif
#endif
}

int request_irq(unsigned int irq,
		irqreturn_t (*handler)(int, void *, struct pt_regs *),
                unsigned long flags, const char *devname, void *dev_id)
{
	unsigned short ptn = 1 << (irq - EXT_IRQ0);
	irq_handler_t *irq_handle;
	if (irq < 0 || irq >= NR_IRQS) {
		printk("Incorrect IRQ %d from %s\n", irq, devname);
		return -EINVAL;
	}
	if (irq_list[irq])
		return -EBUSY; /* already used */
	if (irq >= EXT_IRQ0 && irq <= EXT_IRQ15) {
		/* initialize IRQ pin */
		unsigned int port_no,bit_no;
		if (*(volatile unsigned short *)ITSR & ptn) {
			port_no = irq_assign_table1[irq - EXT_IRQ0].port_no;
			bit_no = irq_assign_table1[irq - EXT_IRQ0].bit_no;
		} else {
			port_no = irq_assign_table0[irq - EXT_IRQ0].port_no;
			bit_no = irq_assign_table0[irq - EXT_IRQ0].bit_no;
		}
		if (H8300_GPIO_RESERVE(port_no, bit_no) == 0)
			return -EBUSY;                   /* pin already use */
		H8300_GPIO_DDR(port_no, bit_no, H8300_GPIO_INPUT);
		*(volatile unsigned short *)ISR &= ~ptn; /* ISR clear */
	}		

	if (use_kmalloc)
		irq_handle = (irq_handler_t *)kmalloc(sizeof(irq_handler_t), GFP_ATOMIC);
	else {
		/* use bootmem allocater */
		irq_handle = (irq_handler_t *)alloc_bootmem(sizeof(irq_handler_t));
		irq_handle = (irq_handler_t *)((unsigned long)irq_handle | 0x80000000);
	}

	if (irq_handle == NULL)
		return -ENOMEM;

	irq_handle->handler = handler;
	irq_handle->flags   = flags;
	irq_handle->count   = 0;
	irq_handle->dev_id  = dev_id;
	irq_handle->devname = devname;
	irq_list[irq] = irq_handle;
	if (irq_handle->flags & SA_SAMPLE_RANDOM)
		rand_initialize_irq(irq);
	
	/* enable interrupt */
	/* compatible i386  */
	if (irq >= EXT_IRQ0 && irq <= EXT_IRQ15)
		*(volatile unsigned short *)IER |= ptn;
	return 0;
}

EXPORT_SYMBOL(request_irq);

void free_irq(unsigned int irq, void *dev_id)
{
	if (irq >= NR_IRQS)
		return;
	if (irq_list[irq]->dev_id != dev_id)
		printk("%s: Removing probably wrong IRQ %d from %s\n",
		       __FUNCTION__, irq, irq_list[irq]->devname);
	if (irq >= EXT_IRQ0 && irq <= EXT_IRQ15) {
		/* disable interrupt & release IRQ pin */
		unsigned short port_no,bit_no;
		*(volatile unsigned short *)ISR &= ~(1 << (irq - EXT_IRQ0));
		*(volatile unsigned short *)IER |= 1 << (irq - EXT_IRQ0);
		if (*(volatile unsigned short *)ITSR & (1 << (irq - EXT_IRQ0))) {
			port_no = irq_assign_table1[irq - EXT_IRQ0].port_no;
			bit_no = irq_assign_table1[irq - EXT_IRQ0].bit_no;
		} else {
			port_no = irq_assign_table0[irq - EXT_IRQ0].port_no;
			bit_no = irq_assign_table0[irq - EXT_IRQ0].bit_no;
		}
		H8300_GPIO_FREE(port_no, bit_no);
	}
	if (((unsigned long)irq_list[irq] & 0x80000000) == 0) {
		kfree(irq_list[irq]);
		irq_list[irq] = NULL;
	}
}

EXPORT_SYMBOL(free_irq);

unsigned long probe_irq_on (void)
{
	return 0;
}

EXPORT_SYMBOL(probe_irq_on);

int probe_irq_off (unsigned long irqs)
{
	return 0;
}

EXPORT_SYMBOL(probe_irq_off);

void enable_irq(unsigned int irq)
{
	if (irq >= EXT_IRQ0 && irq <= EXT_IRQ15)
		*(volatile unsigned short *)IER |= 1 << (irq - EXT_IRQ0);
}

void disable_irq(unsigned int irq)
{
	if (irq >= EXT_IRQ0 && irq <= EXT_IRQ15)
		*(volatile unsigned short *)IER &= ~(1 << (irq - EXT_IRQ0));
}

asmlinkage void process_int(unsigned long vec, struct pt_regs *fp)
{
	irq_enter();
	/* ISR clear       */
	/* compatible i386 */
	if (vec >= EXT_IRQ0 && vec <= EXT_IRQ15)
		*(volatile unsigned short *)ISR &= ~(1 << (vec - EXT_IRQ0));
	if (vec < NR_IRQS) {
		if (irq_list[vec]) {
			irq_list[vec]->handler(vec, irq_list[vec]->dev_id, fp);
			irq_list[vec]->count++;
			if (irq_list[vec]->flags & SA_SAMPLE_RANDOM)
				add_interrupt_randomness(vec);
		}
	} else {
		BUG();
	}
	irq_exit();
}

int show_interrupts(struct seq_file *p, void *v)
{
	int i = *(loff_t *) v;

	if ((i < NR_IRQS) && (irq_list[i] !=NULL)) {
		seq_printf(p, "%3d: %10u ",i,irq_list[i]->count);
		seq_printf(p, "%s\n", irq_list[i]->devname);
	}

	return 0;
}

void init_irq_proc(void)
{
}

static int __init enable_kmalloc(void)
{
	use_kmalloc = 1;
	return 0;
}
core_initcall(enable_kmalloc);
