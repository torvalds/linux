/*  sun4m_irq.c
 *  arch/sparc/kernel/sun4m_irq.c:
 *
 *  djhr: Hacked out of irq.c into a CPU dependent version.
 *
 *  Copyright (C) 1995 David S. Miller (davem@caip.rutgers.edu)
 *  Copyright (C) 1995 Miguel de Icaza (miguel@nuclecu.unam.mx)
 *  Copyright (C) 1995 Pete A. Zaitcev (zaitcev@yahoo.com)
 *  Copyright (C) 1996 Dave Redman (djhr@tadpole.co.uk)
 */

#include <linux/errno.h>
#include <linux/linkage.h>
#include <linux/kernel_stat.h>
#include <linux/signal.h>
#include <linux/sched.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/slab.h>
#include <linux/init.h>
#include <linux/ioport.h>

#include <asm/ptrace.h>
#include <asm/processor.h>
#include <asm/system.h>
#include <asm/psr.h>
#include <asm/vaddrs.h>
#include <asm/timer.h>
#include <asm/openprom.h>
#include <asm/oplib.h>
#include <asm/traps.h>
#include <asm/pgalloc.h>
#include <asm/pgtable.h>
#include <asm/smp.h>
#include <asm/irq.h>
#include <asm/io.h>
#include <asm/sbus.h>
#include <asm/cacheflush.h>

#include "irq.h"

/* On the sun4m, just like the timers, we have both per-cpu and master
 * interrupt registers.
 */

/* These registers are used for sending/receiving irqs from/to
 * different cpu's.
 */
struct sun4m_intreg_percpu {
	unsigned int tbt;        /* Interrupts still pending for this cpu. */

	/* These next two registers are WRITE-ONLY and are only
	 * "on bit" sensitive, "off bits" written have NO affect.
	 */
	unsigned int clear;  /* Clear this cpus irqs here. */
	unsigned int set;    /* Set this cpus irqs here. */
	unsigned char space[PAGE_SIZE - 12];
};

/*
 * djhr
 * Actually the clear and set fields in this struct are misleading..
 * according to the SLAVIO manual (and the same applies for the SEC)
 * the clear field clears bits in the mask which will ENABLE that IRQ
 * the set field sets bits in the mask to DISABLE the IRQ.
 *
 * Also the undirected_xx address in the SLAVIO is defined as
 * RESERVED and write only..
 *
 * DAVEM_NOTE: The SLAVIO only specifies behavior on uniprocessor
 *             sun4m machines, for MP the layout makes more sense.
 */
struct sun4m_intregs {
	struct sun4m_intreg_percpu cpu_intregs[SUN4M_NCPUS];
	unsigned int tbt;                /* IRQ's that are still pending. */
	unsigned int irqs;               /* Master IRQ bits. */

	/* Again, like the above, two these registers are WRITE-ONLY. */
	unsigned int clear;              /* Clear master IRQ's by setting bits here. */
	unsigned int set;                /* Set master IRQ's by setting bits here. */

	/* This register is both READ and WRITE. */
	unsigned int undirected_target;  /* Which cpu gets undirected irqs. */
};

static unsigned long dummy;

struct sun4m_intregs *sun4m_interrupts;
unsigned long *irq_rcvreg = &dummy;

/* Dave Redman (djhr@tadpole.co.uk)
 * The sun4m interrupt registers.
 */
#define SUN4M_INT_ENABLE  	0x80000000
#define SUN4M_INT_E14     	0x00000080
#define SUN4M_INT_E10     	0x00080000

#define SUN4M_HARD_INT(x)	(0x000000001 << (x))
#define SUN4M_SOFT_INT(x)	(0x000010000 << (x))

#define	SUN4M_INT_MASKALL	0x80000000	  /* mask all interrupts */
#define	SUN4M_INT_MODULE_ERR	0x40000000	  /* module error */
#define	SUN4M_INT_M2S_WRITE	0x20000000	  /* write buffer error */
#define	SUN4M_INT_ECC		0x10000000	  /* ecc memory error */
#define	SUN4M_INT_FLOPPY	0x00400000	  /* floppy disk */
#define	SUN4M_INT_MODULE	0x00200000	  /* module interrupt */
#define	SUN4M_INT_VIDEO		0x00100000	  /* onboard video */
#define	SUN4M_INT_REALTIME	0x00080000	  /* system timer */
#define	SUN4M_INT_SCSI		0x00040000	  /* onboard scsi */
#define	SUN4M_INT_AUDIO		0x00020000	  /* audio/isdn */
#define	SUN4M_INT_ETHERNET	0x00010000	  /* onboard ethernet */
#define	SUN4M_INT_SERIAL	0x00008000	  /* serial ports */
#define	SUN4M_INT_KBDMS		0x00004000	  /* keyboard/mouse */
#define	SUN4M_INT_SBUSBITS	0x00003F80	  /* sbus int bits */

#define SUN4M_INT_SBUS(x)	(1 << (x+7))
#define SUN4M_INT_VME(x)	(1 << (x))

/* These tables only apply for interrupts greater than 15..
 * 
 * any intr value below 0x10 is considered to be a soft-int
 * this may be useful or it may not.. but that's how I've done it.
 * and it won't clash with what OBP is telling us about devices.
 *
 * take an encoded intr value and lookup if it's valid
 * then get the mask bits that match from irq_mask
 *
 * P3: Translation from irq 0x0d to mask 0x2000 is for MrCoffee.
 */
static unsigned char irq_xlate[32] = {
    /*  0,  1,  2,  3,  4,  5,  6,  7,  8,  9,  a,  b,  c,  d,  e,  f */
	0,  0,  0,  0,  1,  0,  2,  0,  3,  0,  4,  5,  6, 14,  0,  7,
	0,  0,  8,  9,  0, 10,  0, 11,  0, 12,  0, 13,  0, 14,  0,  0
};

static unsigned long irq_mask[] = {
	0,						  /* illegal index */
	SUN4M_INT_SCSI,				  	  /*  1 irq 4 */
	SUN4M_INT_ETHERNET,				  /*  2 irq 6 */
	SUN4M_INT_VIDEO,				  /*  3 irq 8 */
	SUN4M_INT_REALTIME,				  /*  4 irq 10 */
	SUN4M_INT_FLOPPY,				  /*  5 irq 11 */
	(SUN4M_INT_SERIAL | SUN4M_INT_KBDMS),	  	  /*  6 irq 12 */
	SUN4M_INT_MODULE_ERR,			  	  /*  7 irq 15 */
	SUN4M_INT_SBUS(0),				  /*  8 irq 2 */
	SUN4M_INT_SBUS(1),				  /*  9 irq 3 */
	SUN4M_INT_SBUS(2),				  /* 10 irq 5 */
	SUN4M_INT_SBUS(3),				  /* 11 irq 7 */
	SUN4M_INT_SBUS(4),				  /* 12 irq 9 */
	SUN4M_INT_SBUS(5),				  /* 13 irq 11 */
	SUN4M_INT_SBUS(6)				  /* 14 irq 13 */
};

static int sun4m_pil_map[] = { 0, 2, 3, 5, 7, 9, 11, 13 };

static unsigned int sun4m_sbint_to_irq(struct sbus_dev *sdev,
				       unsigned int sbint)
{
	if (sbint >= sizeof(sun4m_pil_map)) {
		printk(KERN_ERR "%s: bogus SBINT %d\n", sdev->prom_name, sbint);
		BUG();
	}
	return sun4m_pil_map[sbint] | 0x30;
}

static unsigned long sun4m_get_irqmask(unsigned int irq)
{
	unsigned long mask;
    
	if (irq > 0x20) {
		/* OBIO/SBUS interrupts */
		irq &= 0x1f;
		mask = irq_mask[irq_xlate[irq]];
		if (!mask)
			printk("sun4m_get_irqmask: IRQ%d has no valid mask!\n",irq);
	} else {
		/* Soft Interrupts will come here.
		 * Currently there is no way to trigger them but I'm sure
		 * something could be cooked up.
		 */
		irq &= 0xf;
		mask = SUN4M_SOFT_INT(irq);
	}
	return mask;
}

static void sun4m_disable_irq(unsigned int irq_nr)
{
	unsigned long mask, flags;
	int cpu = smp_processor_id();

	mask = sun4m_get_irqmask(irq_nr);
	local_irq_save(flags);
	if (irq_nr > 15)
		sun4m_interrupts->set = mask;
	else
		sun4m_interrupts->cpu_intregs[cpu].set = mask;
	local_irq_restore(flags);    
}

static void sun4m_enable_irq(unsigned int irq_nr)
{
	unsigned long mask, flags;
	int cpu = smp_processor_id();

	/* Dreadful floppy hack. When we use 0x2b instead of
         * 0x0b the system blows (it starts to whistle!).
         * So we continue to use 0x0b. Fixme ASAP. --P3
         */
        if (irq_nr != 0x0b) {
		mask = sun4m_get_irqmask(irq_nr);
		local_irq_save(flags);
		if (irq_nr > 15)
			sun4m_interrupts->clear = mask;
		else
			sun4m_interrupts->cpu_intregs[cpu].clear = mask;
		local_irq_restore(flags);    
	} else {
		local_irq_save(flags);
		sun4m_interrupts->clear = SUN4M_INT_FLOPPY;
		local_irq_restore(flags);
	}
}

static unsigned long cpu_pil_to_imask[16] = {
/*0*/	0x00000000,
/*1*/	0x00000000,
/*2*/	SUN4M_INT_SBUS(0) | SUN4M_INT_VME(0),
/*3*/	SUN4M_INT_SBUS(1) | SUN4M_INT_VME(1),
/*4*/	SUN4M_INT_SCSI,
/*5*/	SUN4M_INT_SBUS(2) | SUN4M_INT_VME(2),
/*6*/	SUN4M_INT_ETHERNET,
/*7*/	SUN4M_INT_SBUS(3) | SUN4M_INT_VME(3),
/*8*/	SUN4M_INT_VIDEO,
/*9*/	SUN4M_INT_SBUS(4) | SUN4M_INT_VME(4) | SUN4M_INT_MODULE_ERR,
/*10*/	SUN4M_INT_REALTIME,
/*11*/	SUN4M_INT_SBUS(5) | SUN4M_INT_VME(5) | SUN4M_INT_FLOPPY,
/*12*/	SUN4M_INT_SERIAL | SUN4M_INT_KBDMS,
/*13*/	SUN4M_INT_AUDIO,
/*14*/	SUN4M_INT_E14,
/*15*/	0x00000000
};

/* We assume the caller has disabled local interrupts when these are called,
 * or else very bizarre behavior will result.
 */
static void sun4m_disable_pil_irq(unsigned int pil)
{
	sun4m_interrupts->set = cpu_pil_to_imask[pil];
}

static void sun4m_enable_pil_irq(unsigned int pil)
{
	sun4m_interrupts->clear = cpu_pil_to_imask[pil];
}

#ifdef CONFIG_SMP
static void sun4m_send_ipi(int cpu, int level)
{
	unsigned long mask;

	mask = sun4m_get_irqmask(level);
	sun4m_interrupts->cpu_intregs[cpu].set = mask;
}

static void sun4m_clear_ipi(int cpu, int level)
{
	unsigned long mask;

	mask = sun4m_get_irqmask(level);
	sun4m_interrupts->cpu_intregs[cpu].clear = mask;
}

static void sun4m_set_udt(int cpu)
{
	sun4m_interrupts->undirected_target = cpu;
}
#endif

#define OBIO_INTR	0x20
#define TIMER_IRQ  	(OBIO_INTR | 10)
#define PROFILE_IRQ	(OBIO_INTR | 14)

static struct sun4m_timer_regs *sun4m_timers;
unsigned int lvl14_resolution = (((1000000/HZ) + 1) << 10);

static void sun4m_clear_clock_irq(void)
{
	volatile unsigned int clear_intr;
	clear_intr = sun4m_timers->l10_timer_limit;
}

static void sun4m_clear_profile_irq(int cpu)
{
	volatile unsigned int clear;
    
	clear = sun4m_timers->cpu_timers[cpu].l14_timer_limit;
}

static void sun4m_load_profile_irq(int cpu, unsigned int limit)
{
	sun4m_timers->cpu_timers[cpu].l14_timer_limit = limit;
}

static void __init sun4m_init_timers(irq_handler_t counter_fn)
{
	int reg_count, irq, cpu;
	struct linux_prom_registers cnt_regs[PROMREG_MAX];
	int obio_node, cnt_node;
	struct resource r;

	cnt_node = 0;
	if((obio_node =
	    prom_searchsiblings (prom_getchild(prom_root_node), "obio")) == 0 ||
	   (obio_node = prom_getchild (obio_node)) == 0 ||
	   (cnt_node = prom_searchsiblings (obio_node, "counter")) == 0) {
		prom_printf("Cannot find /obio/counter node\n");
		prom_halt();
	}
	reg_count = prom_getproperty(cnt_node, "reg",
				     (void *) cnt_regs, sizeof(cnt_regs));
	reg_count = (reg_count/sizeof(struct linux_prom_registers));
    
	/* Apply the obio ranges to the timer registers. */
	prom_apply_obio_ranges(cnt_regs, reg_count);
    
	cnt_regs[4].phys_addr = cnt_regs[reg_count-1].phys_addr;
	cnt_regs[4].reg_size = cnt_regs[reg_count-1].reg_size;
	cnt_regs[4].which_io = cnt_regs[reg_count-1].which_io;
	for(obio_node = 1; obio_node < 4; obio_node++) {
		cnt_regs[obio_node].phys_addr =
			cnt_regs[obio_node-1].phys_addr + PAGE_SIZE;
		cnt_regs[obio_node].reg_size = cnt_regs[obio_node-1].reg_size;
		cnt_regs[obio_node].which_io = cnt_regs[obio_node-1].which_io;
	}

	memset((char*)&r, 0, sizeof(struct resource));
	/* Map the per-cpu Counter registers. */
	r.flags = cnt_regs[0].which_io;
	r.start = cnt_regs[0].phys_addr;
	sun4m_timers = (struct sun4m_timer_regs *) sbus_ioremap(&r, 0,
	    PAGE_SIZE*SUN4M_NCPUS, "sun4m_cpu_cnt");
	/* Map the system Counter register. */
	/* XXX Here we expect consequent calls to yeld adjusent maps. */
	r.flags = cnt_regs[4].which_io;
	r.start = cnt_regs[4].phys_addr;
	sbus_ioremap(&r, 0, cnt_regs[4].reg_size, "sun4m_sys_cnt");

	sun4m_timers->l10_timer_limit =  (((1000000/HZ) + 1) << 10);
	master_l10_counter = &sun4m_timers->l10_cur_count;
	master_l10_limit = &sun4m_timers->l10_timer_limit;

	irq = request_irq(TIMER_IRQ,
			  counter_fn,
			  (IRQF_DISABLED | SA_STATIC_ALLOC),
			  "timer", NULL);
	if (irq) {
		prom_printf("time_init: unable to attach IRQ%d\n",TIMER_IRQ);
		prom_halt();
	}
   
	if (!cpu_find_by_instance(1, NULL, NULL)) {
		for(cpu = 0; cpu < 4; cpu++)
			sun4m_timers->cpu_timers[cpu].l14_timer_limit = 0;
		sun4m_interrupts->set = SUN4M_INT_E14;
	} else {
		sun4m_timers->cpu_timers[0].l14_timer_limit = 0;
	}
#ifdef CONFIG_SMP
	{
		unsigned long flags;
		extern unsigned long lvl14_save[4];
		struct tt_entry *trap_table = &sparc_ttable[SP_TRAP_IRQ1 + (14 - 1)];

		/* For SMP we use the level 14 ticker, however the bootup code
		 * has copied the firmware's level 14 vector into the boot cpu's
		 * trap table, we must fix this now or we get squashed.
		 */
		local_irq_save(flags);
		trap_table->inst_one = lvl14_save[0];
		trap_table->inst_two = lvl14_save[1];
		trap_table->inst_three = lvl14_save[2];
		trap_table->inst_four = lvl14_save[3];
		local_flush_cache_all();
		local_irq_restore(flags);
	}
#endif
}

void __init sun4m_init_IRQ(void)
{
	int ie_node,i;
	struct linux_prom_registers int_regs[PROMREG_MAX];
	int num_regs;
	struct resource r;
	int mid;
    
	local_irq_disable();
	if((ie_node = prom_searchsiblings(prom_getchild(prom_root_node), "obio")) == 0 ||
	   (ie_node = prom_getchild (ie_node)) == 0 ||
	   (ie_node = prom_searchsiblings (ie_node, "interrupt")) == 0) {
		prom_printf("Cannot find /obio/interrupt node\n");
		prom_halt();
	}
	num_regs = prom_getproperty(ie_node, "reg", (char *) int_regs,
				    sizeof(int_regs));
	num_regs = (num_regs/sizeof(struct linux_prom_registers));
    
	/* Apply the obio ranges to these registers. */
	prom_apply_obio_ranges(int_regs, num_regs);
    
	int_regs[4].phys_addr = int_regs[num_regs-1].phys_addr;
	int_regs[4].reg_size = int_regs[num_regs-1].reg_size;
	int_regs[4].which_io = int_regs[num_regs-1].which_io;
	for(ie_node = 1; ie_node < 4; ie_node++) {
		int_regs[ie_node].phys_addr = int_regs[ie_node-1].phys_addr + PAGE_SIZE;
		int_regs[ie_node].reg_size = int_regs[ie_node-1].reg_size;
		int_regs[ie_node].which_io = int_regs[ie_node-1].which_io;
	}

	memset((char *)&r, 0, sizeof(struct resource));
	/* Map the interrupt registers for all possible cpus. */
	r.flags = int_regs[0].which_io;
	r.start = int_regs[0].phys_addr;
	sun4m_interrupts = (struct sun4m_intregs *) sbus_ioremap(&r, 0,
	    PAGE_SIZE*SUN4M_NCPUS, "interrupts_percpu");

	/* Map the system interrupt control registers. */
	r.flags = int_regs[4].which_io;
	r.start = int_regs[4].phys_addr;
	sbus_ioremap(&r, 0, int_regs[4].reg_size, "interrupts_system");

	sun4m_interrupts->set = ~SUN4M_INT_MASKALL;
	for (i = 0; !cpu_find_by_instance(i, NULL, &mid); i++)
		sun4m_interrupts->cpu_intregs[mid].clear = ~0x17fff;

	if (!cpu_find_by_instance(1, NULL, NULL)) {
		/* system wide interrupts go to cpu 0, this should always
		 * be safe because it is guaranteed to be fitted or OBP doesn't
		 * come up
		 *
		 * Not sure, but writing here on SLAVIO systems may puke
		 * so I don't do it unless there is more than 1 cpu.
		 */
		irq_rcvreg = (unsigned long *)
				&sun4m_interrupts->undirected_target;
		sun4m_interrupts->undirected_target = 0;
	}
	BTFIXUPSET_CALL(sbint_to_irq, sun4m_sbint_to_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(enable_irq, sun4m_enable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_irq, sun4m_disable_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(enable_pil_irq, sun4m_enable_pil_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(disable_pil_irq, sun4m_disable_pil_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_clock_irq, sun4m_clear_clock_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_profile_irq, sun4m_clear_profile_irq, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(load_profile_irq, sun4m_load_profile_irq, BTFIXUPCALL_NORM);
	sparc_init_timers = sun4m_init_timers;
#ifdef CONFIG_SMP
	BTFIXUPSET_CALL(set_cpu_int, sun4m_send_ipi, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(clear_cpu_int, sun4m_clear_ipi, BTFIXUPCALL_NORM);
	BTFIXUPSET_CALL(set_irq_udt, sun4m_set_udt, BTFIXUPCALL_NORM);
#endif
	/* Cannot enable interrupts until OBP ticker is disabled. */
}
