/*
 *  arch/ppc/kernel/open_pic.c -- OpenPIC Interrupt Handling
 *
 *  Copyright (C) 1997 Geert Uytterhoeven
 *
 *  This file is subject to the terms and conditions of the GNU General Public
 *  License.  See the file COPYING in the main directory of this archive
 *  for more details.
 *
 *  This is a duplicate of open_pic.c that deals with U3s MPIC on
 *  G5 PowerMacs. It's the same file except it's using big endian
 *  register accesses
 */

#include <linux/config.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/sysdev.h>
#include <linux/errno.h>
#include <asm/ptrace.h>
#include <asm/signal.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/sections.h>
#include <asm/open_pic.h>
#include <asm/i8259.h>
#include <asm/machdep.h>

#include "open_pic_defs.h"

void *OpenPIC2_Addr;
static volatile struct OpenPIC *OpenPIC2 = NULL;
/*
 * We define OpenPIC_InitSenses table thusly:
 * bit 0x1: sense, 0 for edge and 1 for level.
 * bit 0x2: polarity, 0 for negative, 1 for positive.
 */
extern  u_int OpenPIC_NumInitSenses;
extern u_char *OpenPIC_InitSenses;
extern int use_of_interrupt_tree;

static u_int NumProcessors;
static u_int NumSources;
static int open_pic2_irq_offset;
static volatile OpenPIC_Source *ISR[NR_IRQS];

/* Global Operations */
static void openpic2_disable_8259_pass_through(void);
static void openpic2_set_priority(u_int pri);
static void openpic2_set_spurious(u_int vector);

/* Timer Interrupts */
static void openpic2_inittimer(u_int timer, u_int pri, u_int vector);
static void openpic2_maptimer(u_int timer, u_int cpumask);

/* Interrupt Sources */
static void openpic2_enable_irq(u_int irq);
static void openpic2_disable_irq(u_int irq);
static void openpic2_initirq(u_int irq, u_int pri, u_int vector, int polarity,
			    int is_level);
static void openpic2_mapirq(u_int irq, u_int cpumask, u_int keepmask);

/*
 * These functions are not used but the code is kept here
 * for completeness and future reference.
 */
static void openpic2_reset(void);
#ifdef notused
static void openpic2_enable_8259_pass_through(void);
static u_int openpic2_get_priority(void);
static u_int openpic2_get_spurious(void);
static void openpic2_set_sense(u_int irq, int sense);
#endif /* notused */

/*
 * Description of the openpic for the higher-level irq code
 */
static void openpic2_end_irq(unsigned int irq_nr);
static void openpic2_ack_irq(unsigned int irq_nr);

struct hw_interrupt_type open_pic2 = {
	.typename = " OpenPIC2 ",
	.enable = openpic2_enable_irq,
	.disable = openpic2_disable_irq,
	.ack = openpic2_ack_irq,
	.end = openpic2_end_irq,
};

/*
 *  Accesses to the current processor's openpic registers
 *  On cascaded controller, this is only CPU 0
 */
#define THIS_CPU		Processor[0]
#define DECL_THIS_CPU
#define CHECK_THIS_CPU

#if 1
#define check_arg_ipi(ipi) \
    if (ipi < 0 || ipi >= OPENPIC_NUM_IPI) \
	printk("open_pic.c:%d: illegal ipi %d\n", __LINE__, ipi);
#define check_arg_timer(timer) \
    if (timer < 0 || timer >= OPENPIC_NUM_TIMERS) \
	printk("open_pic.c:%d: illegal timer %d\n", __LINE__, timer);
#define check_arg_vec(vec) \
    if (vec < 0 || vec >= OPENPIC_NUM_VECTORS) \
	printk("open_pic.c:%d: illegal vector %d\n", __LINE__, vec);
#define check_arg_pri(pri) \
    if (pri < 0 || pri >= OPENPIC_NUM_PRI) \
	printk("open_pic.c:%d: illegal priority %d\n", __LINE__, pri);
/*
 * Print out a backtrace if it's out of range, since if it's larger than NR_IRQ's
 * data has probably been corrupted and we're going to panic or deadlock later
 * anyway --Troy
 */
extern unsigned long* _get_SP(void);
#define check_arg_irq(irq) \
    if (irq < open_pic2_irq_offset || irq >= NumSources+open_pic2_irq_offset \
	|| ISR[irq - open_pic2_irq_offset] == 0) { \
      printk("open_pic.c:%d: illegal irq %d\n", __LINE__, irq); \
      /*print_backtrace(_get_SP());*/ }
#define check_arg_cpu(cpu) \
    if (cpu < 0 || cpu >= NumProcessors){ \
	printk("open_pic2.c:%d: illegal cpu %d\n", __LINE__, cpu); \
	/*print_backtrace(_get_SP());*/ }
#else
#define check_arg_ipi(ipi)	do {} while (0)
#define check_arg_timer(timer)	do {} while (0)
#define check_arg_vec(vec)	do {} while (0)
#define check_arg_pri(pri)	do {} while (0)
#define check_arg_irq(irq)	do {} while (0)
#define check_arg_cpu(cpu)	do {} while (0)
#endif

static u_int openpic2_read(volatile u_int *addr)
{
	u_int val;

	val = in_be32(addr);
	return val;
}

static inline void openpic2_write(volatile u_int *addr, u_int val)
{
	out_be32(addr, val);
}

static inline u_int openpic2_readfield(volatile u_int *addr, u_int mask)
{
	u_int val = openpic2_read(addr);
	return val & mask;
}

inline void openpic2_writefield(volatile u_int *addr, u_int mask,
			       u_int field)
{
	u_int val = openpic2_read(addr);
	openpic2_write(addr, (val & ~mask) | (field & mask));
}

static inline void openpic2_clearfield(volatile u_int *addr, u_int mask)
{
	openpic2_writefield(addr, mask, 0);
}

static inline void openpic2_setfield(volatile u_int *addr, u_int mask)
{
	openpic2_writefield(addr, mask, mask);
}

static void openpic2_safe_writefield(volatile u_int *addr, u_int mask,
				    u_int field)
{
	openpic2_setfield(addr, OPENPIC_MASK);
	while (openpic2_read(addr) & OPENPIC_ACTIVITY);
	openpic2_writefield(addr, mask | OPENPIC_MASK, field | OPENPIC_MASK);
}

static void openpic2_reset(void)
{
	openpic2_setfield(&OpenPIC2->Global.Global_Configuration0,
			 OPENPIC_CONFIG_RESET);
	while (openpic2_readfield(&OpenPIC2->Global.Global_Configuration0,
				 OPENPIC_CONFIG_RESET))
		mb();
}

void __init openpic2_set_sources(int first_irq, int num_irqs, void *first_ISR)
{
	volatile OpenPIC_Source *src = first_ISR;
	int i, last_irq;

	last_irq = first_irq + num_irqs;
	if (last_irq > NumSources)
		NumSources = last_irq;
	if (src == 0)
		src = &((struct OpenPIC *)OpenPIC2_Addr)->Source[first_irq];
	for (i = first_irq; i < last_irq; ++i, ++src)
		ISR[i] = src;
}

/*
 * The `offset' parameter defines where the interrupts handled by the
 * OpenPIC start in the space of interrupt numbers that the kernel knows
 * about.  In other words, the OpenPIC's IRQ0 is numbered `offset' in the
 * kernel's interrupt numbering scheme.
 * We assume there is only one OpenPIC.
 */
void __init openpic2_init(int offset)
{
	u_int t, i;
	u_int timerfreq;
	const char *version;

	if (!OpenPIC2_Addr) {
		printk("No OpenPIC2 found !\n");
		return;
	}
	OpenPIC2 = (volatile struct OpenPIC *)OpenPIC2_Addr;

	if (ppc_md.progress) ppc_md.progress("openpic: enter", 0x122);

	t = openpic2_read(&OpenPIC2->Global.Feature_Reporting0);
	switch (t & OPENPIC_FEATURE_VERSION_MASK) {
	case 1:
		version = "1.0";
		break;
	case 2:
		version = "1.2";
		break;
	case 3:
		version = "1.3";
		break;
	default:
		version = "?";
		break;
	}
	NumProcessors = ((t & OPENPIC_FEATURE_LAST_PROCESSOR_MASK) >>
			 OPENPIC_FEATURE_LAST_PROCESSOR_SHIFT) + 1;
	if (NumSources == 0)
		openpic2_set_sources(0,
				    ((t & OPENPIC_FEATURE_LAST_SOURCE_MASK) >>
				     OPENPIC_FEATURE_LAST_SOURCE_SHIFT) + 1,
				    NULL);
	printk("OpenPIC (2) Version %s (%d CPUs and %d IRQ sources) at %p\n",
	       version, NumProcessors, NumSources, OpenPIC2);
	timerfreq = openpic2_read(&OpenPIC2->Global.Timer_Frequency);
	if (timerfreq)
		printk("OpenPIC timer frequency is %d.%06d MHz\n",
		       timerfreq / 1000000, timerfreq % 1000000);

	open_pic2_irq_offset = offset;

	/* Initialize timer interrupts */
	if ( ppc_md.progress ) ppc_md.progress("openpic2: timer",0x3ba);
	for (i = 0; i < OPENPIC_NUM_TIMERS; i++) {
		/* Disabled, Priority 0 */
		openpic2_inittimer(i, 0, OPENPIC2_VEC_TIMER+i+offset);
		/* No processor */
		openpic2_maptimer(i, 0);
	}

	/* Initialize external interrupts */
	if (ppc_md.progress) ppc_md.progress("openpic2: external",0x3bc);

	openpic2_set_priority(0xf);

	/* Init all external sources, including possibly the cascade. */
	for (i = 0; i < NumSources; i++) {
		int sense;

		if (ISR[i] == 0)
			continue;

		/* the bootloader may have left it enabled (bad !) */
		openpic2_disable_irq(i+offset);

		sense = (i < OpenPIC_NumInitSenses)? OpenPIC_InitSenses[i]: \
				(IRQ_SENSE_LEVEL | IRQ_POLARITY_NEGATIVE);

		if (sense & IRQ_SENSE_MASK)
			irq_desc[i+offset].status = IRQ_LEVEL;

		/* Enabled, Priority 8 */
		openpic2_initirq(i, 8, i+offset, (sense & IRQ_POLARITY_MASK),
				(sense & IRQ_SENSE_MASK));
		/* Processor 0 */
		openpic2_mapirq(i, 1<<0, 0);
	}

	/* Init descriptors */
	for (i = offset; i < NumSources + offset; i++)
		irq_desc[i].handler = &open_pic2;

	/* Initialize the spurious interrupt */
	if (ppc_md.progress) ppc_md.progress("openpic2: spurious",0x3bd);
	openpic2_set_spurious(OPENPIC2_VEC_SPURIOUS+offset);

	openpic2_disable_8259_pass_through();
	openpic2_set_priority(0);

	if (ppc_md.progress) ppc_md.progress("openpic2: exit",0x222);
}

#ifdef notused
static void openpic2_enable_8259_pass_through(void)
{
	openpic2_clearfield(&OpenPIC2->Global.Global_Configuration0,
			   OPENPIC_CONFIG_8259_PASSTHROUGH_DISABLE);
}
#endif /* notused */

/* This can't be __init, it is used in openpic_sleep_restore_intrs */
static void openpic2_disable_8259_pass_through(void)
{
	openpic2_setfield(&OpenPIC2->Global.Global_Configuration0,
			 OPENPIC_CONFIG_8259_PASSTHROUGH_DISABLE);
}

/*
 *  Find out the current interrupt
 */
u_int openpic2_irq(void)
{
	u_int vec;
	DECL_THIS_CPU;

	CHECK_THIS_CPU;
	vec = openpic2_readfield(&OpenPIC2->THIS_CPU.Interrupt_Acknowledge,
				OPENPIC_VECTOR_MASK);
	return vec;
}

void openpic2_eoi(void)
{
	DECL_THIS_CPU;

	CHECK_THIS_CPU;
	openpic2_write(&OpenPIC2->THIS_CPU.EOI, 0);
	/* Handle PCI write posting */
	(void)openpic2_read(&OpenPIC2->THIS_CPU.EOI);
}

#ifdef notused
static u_int openpic2_get_priority(void)
{
	DECL_THIS_CPU;

	CHECK_THIS_CPU;
	return openpic2_readfield(&OpenPIC2->THIS_CPU.Current_Task_Priority,
				 OPENPIC_CURRENT_TASK_PRIORITY_MASK);
}
#endif /* notused */

static void __init openpic2_set_priority(u_int pri)
{
	DECL_THIS_CPU;

	CHECK_THIS_CPU;
	check_arg_pri(pri);
	openpic2_writefield(&OpenPIC2->THIS_CPU.Current_Task_Priority,
			   OPENPIC_CURRENT_TASK_PRIORITY_MASK, pri);
}

/*
 *  Get/set the spurious vector
 */
#ifdef notused
static u_int openpic2_get_spurious(void)
{
	return openpic2_readfield(&OpenPIC2->Global.Spurious_Vector,
				 OPENPIC_VECTOR_MASK);
}
#endif /* notused */

/* This can't be __init, it is used in openpic_sleep_restore_intrs */
static void openpic2_set_spurious(u_int vec)
{
	check_arg_vec(vec);
	openpic2_writefield(&OpenPIC2->Global.Spurious_Vector, OPENPIC_VECTOR_MASK,
			   vec);
}

static DEFINE_SPINLOCK(openpic2_setup_lock);

/*
 *  Initialize a timer interrupt (and disable it)
 *
 *  timer: OpenPIC timer number
 *  pri: interrupt source priority
 *  vec: the vector it will produce
 */
static void __init openpic2_inittimer(u_int timer, u_int pri, u_int vec)
{
	check_arg_timer(timer);
	check_arg_pri(pri);
	check_arg_vec(vec);
	openpic2_safe_writefield(&OpenPIC2->Global.Timer[timer].Vector_Priority,
				OPENPIC_PRIORITY_MASK | OPENPIC_VECTOR_MASK,
				(pri << OPENPIC_PRIORITY_SHIFT) | vec);
}

/*
 *  Map a timer interrupt to one or more CPUs
 */
static void __init openpic2_maptimer(u_int timer, u_int cpumask)
{
	check_arg_timer(timer);
	openpic2_write(&OpenPIC2->Global.Timer[timer].Destination,
		      cpumask);
}

/*
 * Initalize the interrupt source which will generate an NMI.
 * This raises the interrupt's priority from 8 to 9.
 *
 * irq: The logical IRQ which generates an NMI.
 */
void __init
openpic2_init_nmi_irq(u_int irq)
{
	check_arg_irq(irq);
	openpic2_safe_writefield(&ISR[irq - open_pic2_irq_offset]->Vector_Priority,
				OPENPIC_PRIORITY_MASK,
				9 << OPENPIC_PRIORITY_SHIFT);
}

/*
 *
 * All functions below take an offset'ed irq argument
 *
 */


/*
 *  Enable/disable an external interrupt source
 *
 *  Externally called, irq is an offseted system-wide interrupt number
 */
static void openpic2_enable_irq(u_int irq)
{
	volatile u_int *vpp;

	check_arg_irq(irq);
	vpp = &ISR[irq - open_pic2_irq_offset]->Vector_Priority;
       	openpic2_clearfield(vpp, OPENPIC_MASK);
	/* make sure mask gets to controller before we return to user */
       	do {
       		mb(); /* sync is probably useless here */
       	} while (openpic2_readfield(vpp, OPENPIC_MASK));
}

static void openpic2_disable_irq(u_int irq)
{
	volatile u_int *vpp;
	u32 vp;

	check_arg_irq(irq);
	vpp = &ISR[irq - open_pic2_irq_offset]->Vector_Priority;
	openpic2_setfield(vpp, OPENPIC_MASK);
	/* make sure mask gets to controller before we return to user */
	do {
		mb();  /* sync is probably useless here */
		vp = openpic2_readfield(vpp, OPENPIC_MASK | OPENPIC_ACTIVITY);
	} while((vp & OPENPIC_ACTIVITY) && !(vp & OPENPIC_MASK));
}


/*
 *  Initialize an interrupt source (and disable it!)
 *
 *  irq: OpenPIC interrupt number
 *  pri: interrupt source priority
 *  vec: the vector it will produce
 *  pol: polarity (1 for positive, 0 for negative)
 *  sense: 1 for level, 0 for edge
 */
static void __init
openpic2_initirq(u_int irq, u_int pri, u_int vec, int pol, int sense)
{
	openpic2_safe_writefield(&ISR[irq]->Vector_Priority,
				OPENPIC_PRIORITY_MASK | OPENPIC_VECTOR_MASK |
				OPENPIC_SENSE_MASK | OPENPIC_POLARITY_MASK,
				(pri << OPENPIC_PRIORITY_SHIFT) | vec |
				(pol ? OPENPIC_POLARITY_POSITIVE :
			    		OPENPIC_POLARITY_NEGATIVE) |
				(sense ? OPENPIC_SENSE_LEVEL : OPENPIC_SENSE_EDGE));
}

/*
 *  Map an interrupt source to one or more CPUs
 */
static void openpic2_mapirq(u_int irq, u_int physmask, u_int keepmask)
{
	if (ISR[irq] == 0)
		return;
	if (keepmask != 0)
		physmask |= openpic2_read(&ISR[irq]->Destination) & keepmask;
	openpic2_write(&ISR[irq]->Destination, physmask);
}

#ifdef notused
/*
 *  Set the sense for an interrupt source (and disable it!)
 *
 *  sense: 1 for level, 0 for edge
 */
static void openpic2_set_sense(u_int irq, int sense)
{
	if (ISR[irq] != 0)
		openpic2_safe_writefield(&ISR[irq]->Vector_Priority,
					OPENPIC_SENSE_LEVEL,
					(sense ? OPENPIC_SENSE_LEVEL : 0));
}
#endif /* notused */

/* No spinlocks, should not be necessary with the OpenPIC
 * (1 register = 1 interrupt and we have the desc lock).
 */
static void openpic2_ack_irq(unsigned int irq_nr)
{
	openpic2_disable_irq(irq_nr);
	openpic2_eoi();
}

static void openpic2_end_irq(unsigned int irq_nr)
{
	if (!(irq_desc[irq_nr].status & (IRQ_DISABLED|IRQ_INPROGRESS)))
		openpic2_enable_irq(irq_nr);
}

int
openpic2_get_irq(struct pt_regs *regs)
{
	int irq = openpic2_irq();

	if (irq == (OPENPIC2_VEC_SPURIOUS + open_pic2_irq_offset))
		irq = -1;
	return irq;
}

#ifdef CONFIG_PM

/*
 * We implement the IRQ controller as a sysdev and put it
 * to sleep at powerdown stage (the callback is named suspend,
 * but it's old semantics, for the Device Model, it's really
 * powerdown). The possible problem is that another sysdev that
 * happens to be suspend after this one will have interrupts off,
 * that may be an issue... For now, this isn't an issue on pmac
 * though...
 */

static u32 save_ipi_vp[OPENPIC_NUM_IPI];
static u32 save_irq_src_vp[OPENPIC_MAX_SOURCES];
static u32 save_irq_src_dest[OPENPIC_MAX_SOURCES];
static u32 save_cpu_task_pri[OPENPIC_MAX_PROCESSORS];
static int openpic_suspend_count;

static void openpic2_cached_enable_irq(u_int irq)
{
	check_arg_irq(irq);
	save_irq_src_vp[irq - open_pic2_irq_offset] &= ~OPENPIC_MASK;
}

static void openpic2_cached_disable_irq(u_int irq)
{
	check_arg_irq(irq);
	save_irq_src_vp[irq - open_pic2_irq_offset] |= OPENPIC_MASK;
}

/* WARNING: Can be called directly by the cpufreq code with NULL parameter,
 * we need something better to deal with that... Maybe switch to S1 for
 * cpufreq changes
 */
int openpic2_suspend(struct sys_device *sysdev, pm_message_t state)
{
	int	i;
	unsigned long flags;

	spin_lock_irqsave(&openpic2_setup_lock, flags);

	if (openpic_suspend_count++ > 0) {
		spin_unlock_irqrestore(&openpic2_setup_lock, flags);
		return 0;
	}

	open_pic2.enable = openpic2_cached_enable_irq;
	open_pic2.disable = openpic2_cached_disable_irq;

	for (i=0; i<NumProcessors; i++) {
		save_cpu_task_pri[i] = openpic2_read(&OpenPIC2->Processor[i].Current_Task_Priority);
		openpic2_writefield(&OpenPIC2->Processor[i].Current_Task_Priority,
				   OPENPIC_CURRENT_TASK_PRIORITY_MASK, 0xf);
	}

	for (i=0; i<OPENPIC_NUM_IPI; i++)
		save_ipi_vp[i] = openpic2_read(&OpenPIC2->Global.IPI_Vector_Priority(i));
	for (i=0; i<NumSources; i++) {
		if (ISR[i] == 0)
			continue;
		save_irq_src_vp[i] = openpic2_read(&ISR[i]->Vector_Priority) & ~OPENPIC_ACTIVITY;
		save_irq_src_dest[i] = openpic2_read(&ISR[i]->Destination);
	}

	spin_unlock_irqrestore(&openpic2_setup_lock, flags);

	return 0;
}

/* WARNING: Can be called directly by the cpufreq code with NULL parameter,
 * we need something better to deal with that... Maybe switch to S1 for
 * cpufreq changes
 */
int openpic2_resume(struct sys_device *sysdev)
{
	int		i;
	unsigned long	flags;
	u32		vppmask =	OPENPIC_PRIORITY_MASK | OPENPIC_VECTOR_MASK |
					OPENPIC_SENSE_MASK | OPENPIC_POLARITY_MASK |
					OPENPIC_MASK;

	spin_lock_irqsave(&openpic2_setup_lock, flags);

	if ((--openpic_suspend_count) > 0) {
		spin_unlock_irqrestore(&openpic2_setup_lock, flags);
		return 0;
	}

	openpic2_reset();

	/* OpenPIC sometimes seem to need some time to be fully back up... */
	do {
		openpic2_set_spurious(OPENPIC2_VEC_SPURIOUS+open_pic2_irq_offset);
	} while(openpic2_readfield(&OpenPIC2->Global.Spurious_Vector, OPENPIC_VECTOR_MASK)
			!= (OPENPIC2_VEC_SPURIOUS + open_pic2_irq_offset));
	
	openpic2_disable_8259_pass_through();

	for (i=0; i<OPENPIC_NUM_IPI; i++)
		openpic2_write(&OpenPIC2->Global.IPI_Vector_Priority(i),
			      save_ipi_vp[i]);
	for (i=0; i<NumSources; i++) {
		if (ISR[i] == 0)
			continue;
		openpic2_write(&ISR[i]->Destination, save_irq_src_dest[i]);
		openpic2_write(&ISR[i]->Vector_Priority, save_irq_src_vp[i]);
		/* make sure mask gets to controller before we return to user */
		do {
			openpic2_write(&ISR[i]->Vector_Priority, save_irq_src_vp[i]);
		} while (openpic2_readfield(&ISR[i]->Vector_Priority, vppmask)
			 != (save_irq_src_vp[i] & vppmask));
	}
	for (i=0; i<NumProcessors; i++)
		openpic2_write(&OpenPIC2->Processor[i].Current_Task_Priority,
			      save_cpu_task_pri[i]);

	open_pic2.enable = openpic2_enable_irq;
	open_pic2.disable = openpic2_disable_irq;

	spin_unlock_irqrestore(&openpic2_setup_lock, flags);

	return 0;
}

#endif /* CONFIG_PM */

/* HACK ALERT */
static struct sysdev_class openpic2_sysclass = {
	set_kset_name("openpic2"),
};

static struct sys_device device_openpic2 = {
	.id		= 0,
	.cls		= &openpic2_sysclass,
};

static struct sysdev_driver driver_openpic2 = {
#ifdef CONFIG_PM
	.suspend	= &openpic2_suspend,
	.resume		= &openpic2_resume,
#endif /* CONFIG_PM */
};

static int __init init_openpic2_sysfs(void)
{
	int rc;

	if (!OpenPIC2_Addr)
		return -ENODEV;
	printk(KERN_DEBUG "Registering openpic2 with sysfs...\n");
	rc = sysdev_class_register(&openpic2_sysclass);
	if (rc) {
		printk(KERN_ERR "Failed registering openpic sys class\n");
		return -ENODEV;
	}
	rc = sysdev_register(&device_openpic2);
	if (rc) {
		printk(KERN_ERR "Failed registering openpic sys device\n");
		return -ENODEV;
	}
	rc = sysdev_driver_register(&openpic2_sysclass, &driver_openpic2);
	if (rc) {
		printk(KERN_ERR "Failed registering openpic sys driver\n");
		return -ENODEV;
	}
	return 0;
}

subsys_initcall(init_openpic2_sysfs);

