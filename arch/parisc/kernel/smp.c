/*
** SMP Support
**
** Copyright (C) 1999 Walt Drummond <drummond@valinux.com>
** Copyright (C) 1999 David Mosberger-Tang <davidm@hpl.hp.com>
** Copyright (C) 2001,2004 Grant Grundler <grundler@parisc-linux.org>
** 
** Lots of stuff stolen from arch/alpha/kernel/smp.c
** ...and then parisc stole from arch/ia64/kernel/smp.c. Thanks David! :^)
**
** Thanks to John Curry and Ullas Ponnadi. I learned alot from their work.
** -grant (1/12/2001)
**
**	This program is free software; you can redistribute it and/or modify
**	it under the terms of the GNU General Public License as published by
**      the Free Software Foundation; either version 2 of the License, or
**      (at your option) any later version.
*/
#undef ENTRY_SYS_CPUS	/* syscall support for iCOD-like functionality */

#include <linux/config.h>

#include <linux/types.h>
#include <linux/spinlock.h>
#include <linux/slab.h>

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/smp.h>
#include <linux/kernel_stat.h>
#include <linux/mm.h>
#include <linux/delay.h>
#include <linux/bitops.h>

#include <asm/system.h>
#include <asm/atomic.h>
#include <asm/current.h>
#include <asm/delay.h>
#include <asm/tlbflush.h>

#include <asm/io.h>
#include <asm/irq.h>		/* for CPU_IRQ_REGION and friends */
#include <asm/mmu_context.h>
#include <asm/page.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/processor.h>
#include <asm/ptrace.h>
#include <asm/unistd.h>
#include <asm/cacheflush.h>

#define kDEBUG 0

DEFINE_SPINLOCK(smp_lock);

volatile struct task_struct *smp_init_current_idle_task;

static volatile int cpu_now_booting __read_mostly = 0;	/* track which CPU is booting */

static int parisc_max_cpus __read_mostly = 1;

/* online cpus are ones that we've managed to bring up completely
 * possible cpus are all valid cpu 
 * present cpus are all detected cpu
 *
 * On startup we bring up the "possible" cpus. Since we discover
 * CPUs later, we add them as hotplug, so the possible cpu mask is
 * empty in the beginning.
 */

cpumask_t cpu_online_map   __read_mostly = CPU_MASK_NONE;	/* Bitmap of online CPUs */
cpumask_t cpu_possible_map __read_mostly = CPU_MASK_ALL;	/* Bitmap of Present CPUs */

EXPORT_SYMBOL(cpu_online_map);
EXPORT_SYMBOL(cpu_possible_map);


struct smp_call_struct {
	void (*func) (void *info);
	void *info;
	long wait;
	atomic_t unstarted_count;
	atomic_t unfinished_count;
};
static volatile struct smp_call_struct *smp_call_function_data;

enum ipi_message_type {
	IPI_NOP=0,
	IPI_RESCHEDULE=1,
	IPI_CALL_FUNC,
	IPI_CPU_START,
	IPI_CPU_STOP,
	IPI_CPU_TEST
};


/********** SMP inter processor interrupt and communication routines */

#undef PER_CPU_IRQ_REGION
#ifdef PER_CPU_IRQ_REGION
/* XXX REVISIT Ignore for now.
**    *May* need this "hook" to register IPI handler
**    once we have perCPU ExtIntr switch tables.
*/
static void
ipi_init(int cpuid)
{

	/* If CPU is present ... */
#ifdef ENTRY_SYS_CPUS
	/* *and* running (not stopped) ... */
#error iCOD support wants state checked here.
#endif

#error verify IRQ_OFFSET(IPI_IRQ) is ipi_interrupt() in new IRQ region

	if(cpu_online(cpuid) )
	{
		switch_to_idle_task(current);
	}

	return;
}
#endif


/*
** Yoink this CPU from the runnable list... 
**
*/
static void
halt_processor(void) 
{
#ifdef ENTRY_SYS_CPUS
#error halt_processor() needs rework
/*
** o migrate I/O interrupts off this CPU.
** o leave IPI enabled - __cli() will disable IPI.
** o leave CPU in online map - just change the state
*/
	cpu_data[this_cpu].state = STATE_STOPPED;
	mark_bh(IPI_BH);
#else
	/* REVISIT : redirect I/O Interrupts to another CPU? */
	/* REVISIT : does PM *know* this CPU isn't available? */
	cpu_clear(smp_processor_id(), cpu_online_map);
	local_irq_disable();
	for (;;)
		;
#endif
}


irqreturn_t
ipi_interrupt(int irq, void *dev_id, struct pt_regs *regs) 
{
	int this_cpu = smp_processor_id();
	struct cpuinfo_parisc *p = &cpu_data[this_cpu];
	unsigned long ops;
	unsigned long flags;

	/* Count this now; we may make a call that never returns. */
	p->ipi_count++;

	mb();	/* Order interrupt and bit testing. */

	for (;;) {
		spin_lock_irqsave(&(p->lock),flags);
		ops = p->pending_ipi;
		p->pending_ipi = 0;
		spin_unlock_irqrestore(&(p->lock),flags);

		mb(); /* Order bit clearing and data access. */

		if (!ops)
		    break;

		while (ops) {
			unsigned long which = ffz(~ops);

			ops &= ~(1 << which);

			switch (which) {
			case IPI_NOP:
#if (kDEBUG>=100)
				printk(KERN_DEBUG "CPU%d IPI_NOP\n",this_cpu);
#endif /* kDEBUG */
				break;
				
			case IPI_RESCHEDULE:
#if (kDEBUG>=100)
				printk(KERN_DEBUG "CPU%d IPI_RESCHEDULE\n",this_cpu);
#endif /* kDEBUG */
				/*
				 * Reschedule callback.  Everything to be
				 * done is done by the interrupt return path.
				 */
				break;

			case IPI_CALL_FUNC:
#if (kDEBUG>=100)
				printk(KERN_DEBUG "CPU%d IPI_CALL_FUNC\n",this_cpu);
#endif /* kDEBUG */
				{
					volatile struct smp_call_struct *data;
					void (*func)(void *info);
					void *info;
					int wait;

					data = smp_call_function_data;
					func = data->func;
					info = data->info;
					wait = data->wait;

					mb();
					atomic_dec ((atomic_t *)&data->unstarted_count);

					/* At this point, *data can't
					 * be relied upon.
					 */

					(*func)(info);

					/* Notify the sending CPU that the
					 * task is done.
					 */
					mb();
					if (wait)
						atomic_dec ((atomic_t *)&data->unfinished_count);
				}
				break;

			case IPI_CPU_START:
#if (kDEBUG>=100)
				printk(KERN_DEBUG "CPU%d IPI_CPU_START\n",this_cpu);
#endif /* kDEBUG */
#ifdef ENTRY_SYS_CPUS
				p->state = STATE_RUNNING;
#endif
				break;

			case IPI_CPU_STOP:
#if (kDEBUG>=100)
				printk(KERN_DEBUG "CPU%d IPI_CPU_STOP\n",this_cpu);
#endif /* kDEBUG */
#ifdef ENTRY_SYS_CPUS
#else
				halt_processor();
#endif
				break;

			case IPI_CPU_TEST:
#if (kDEBUG>=100)
				printk(KERN_DEBUG "CPU%d is alive!\n",this_cpu);
#endif /* kDEBUG */
				break;

			default:
				printk(KERN_CRIT "Unknown IPI num on CPU%d: %lu\n",
					this_cpu, which);
				return IRQ_NONE;
			} /* Switch */
		} /* while (ops) */
	}
	return IRQ_HANDLED;
}


static inline void
ipi_send(int cpu, enum ipi_message_type op)
{
	struct cpuinfo_parisc *p = &cpu_data[cpu];
	unsigned long flags;

	spin_lock_irqsave(&(p->lock),flags);
	p->pending_ipi |= 1 << op;
	gsc_writel(IPI_IRQ - CPU_IRQ_BASE, cpu_data[cpu].hpa);
	spin_unlock_irqrestore(&(p->lock),flags);
}


static inline void
send_IPI_single(int dest_cpu, enum ipi_message_type op)
{
	if (dest_cpu == NO_PROC_ID) {
		BUG();
		return;
	}

	ipi_send(dest_cpu, op);
}

static inline void
send_IPI_allbutself(enum ipi_message_type op)
{
	int i;
	
	for_each_online_cpu(i) {
		if (i != smp_processor_id())
			send_IPI_single(i, op);
	}
}


inline void 
smp_send_stop(void)	{ send_IPI_allbutself(IPI_CPU_STOP); }

static inline void
smp_send_start(void)	{ send_IPI_allbutself(IPI_CPU_START); }

void 
smp_send_reschedule(int cpu) { send_IPI_single(cpu, IPI_RESCHEDULE); }

void
smp_send_all_nop(void)
{
	send_IPI_allbutself(IPI_NOP);
}


/**
 * Run a function on all other CPUs.
 *  <func>	The function to run. This must be fast and non-blocking.
 *  <info>	An arbitrary pointer to pass to the function.
 *  <retry>	If true, keep retrying until ready.
 *  <wait>	If true, wait until function has completed on other CPUs.
 *  [RETURNS]   0 on success, else a negative status code.
 *
 * Does not return until remote CPUs are nearly ready to execute <func>
 * or have executed.
 */

int
smp_call_function (void (*func) (void *info), void *info, int retry, int wait)
{
	struct smp_call_struct data;
	unsigned long timeout;
	static DEFINE_SPINLOCK(lock);
	int retries = 0;

	if (num_online_cpus() < 2)
		return 0;

	/* Can deadlock when called with interrupts disabled */
	WARN_ON(irqs_disabled());

	/* can also deadlock if IPIs are disabled */
	WARN_ON((get_eiem() & (1UL<<(CPU_IRQ_MAX - IPI_IRQ))) == 0);

	
	data.func = func;
	data.info = info;
	data.wait = wait;
	atomic_set(&data.unstarted_count, num_online_cpus() - 1);
	atomic_set(&data.unfinished_count, num_online_cpus() - 1);

	if (retry) {
		spin_lock (&lock);
		while (smp_call_function_data != 0)
			barrier();
	}
	else {
		spin_lock (&lock);
		if (smp_call_function_data) {
			spin_unlock (&lock);
			return -EBUSY;
		}
	}

	smp_call_function_data = &data;
	spin_unlock (&lock);
	
	/*  Send a message to all other CPUs and wait for them to respond  */
	send_IPI_allbutself(IPI_CALL_FUNC);

 retry:
	/*  Wait for response  */
	timeout = jiffies + HZ;
	while ( (atomic_read (&data.unstarted_count) > 0) &&
		time_before (jiffies, timeout) )
		barrier ();

	if (atomic_read (&data.unstarted_count) > 0) {
		printk(KERN_CRIT "SMP CALL FUNCTION TIMED OUT! (cpu=%d), try %d\n",
		      smp_processor_id(), ++retries);
		goto retry;
	}
	/* We either got one or timed out. Release the lock */

	mb();
	smp_call_function_data = NULL;

	while (wait && atomic_read (&data.unfinished_count) > 0)
			barrier ();

	return 0;
}

EXPORT_SYMBOL(smp_call_function);

/*
 * Flush all other CPU's tlb and then mine.  Do this with on_each_cpu()
 * as we want to ensure all TLB's flushed before proceeding.
 */

void
smp_flush_tlb_all(void)
{
	on_each_cpu(flush_tlb_all_local, NULL, 1, 1);
}


void 
smp_do_timer(struct pt_regs *regs)
{
	int cpu = smp_processor_id();
	struct cpuinfo_parisc *data = &cpu_data[cpu];

        if (!--data->prof_counter) {
		data->prof_counter = data->prof_multiplier;
		update_process_times(user_mode(regs));
	}
}

/*
 * Called by secondaries to update state and initialize CPU registers.
 */
static void __init
smp_cpu_init(int cpunum)
{
	extern int init_per_cpu(int);  /* arch/parisc/kernel/setup.c */
	extern void init_IRQ(void);    /* arch/parisc/kernel/irq.c */

	/* Set modes and Enable floating point coprocessor */
	(void) init_per_cpu(cpunum);

	disable_sr_hashing();

	mb();

	/* Well, support 2.4 linux scheme as well. */
	if (cpu_test_and_set(cpunum, cpu_online_map))
	{
		extern void machine_halt(void); /* arch/parisc.../process.c */

		printk(KERN_CRIT "CPU#%d already initialized!\n", cpunum);
		machine_halt();
	}  

	/* Initialise the idle task for this CPU */
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;
	if(current->mm)
		BUG();
	enter_lazy_tlb(&init_mm, current);

	init_IRQ();   /* make sure no IRQ's are enabled or pending */
}


/*
 * Slaves start using C here. Indirectly called from smp_slave_stext.
 * Do what start_kernel() and main() do for boot strap processor (aka monarch)
 */
void __init smp_callin(void)
{
	int slave_id = cpu_now_booting;
#if 0
	void *istack;
#endif

	smp_cpu_init(slave_id);
	preempt_disable();

#if 0	/* NOT WORKING YET - see entry.S */
	istack = (void *)__get_free_pages(GFP_KERNEL,ISTACK_ORDER);
	if (istack == NULL) {
	    printk(KERN_CRIT "Failed to allocate interrupt stack for cpu %d\n",slave_id);
	    BUG();
	}
	mtctl(istack,31);
#endif

	flush_cache_all_local(); /* start with known state */
	flush_tlb_all_local(NULL);

	local_irq_enable();  /* Interrupts have been off until now */

	cpu_idle();      /* Wait for timer to schedule some work */

	/* NOTREACHED */
	panic("smp_callin() AAAAaaaaahhhh....\n");
}

/*
 * Bring one cpu online.
 */
int __init smp_boot_one_cpu(int cpuid)
{
	struct task_struct *idle;
	long timeout;

	/* 
	 * Create an idle task for this CPU.  Note the address wed* give 
	 * to kernel_thread is irrelevant -- it's going to start
	 * where OS_BOOT_RENDEVZ vector in SAL says to start.  But
	 * this gets all the other task-y sort of data structures set
	 * up like we wish.   We need to pull the just created idle task 
	 * off the run queue and stuff it into the init_tasks[] array.  
	 * Sheesh . . .
	 */

	idle = fork_idle(cpuid);
	if (IS_ERR(idle))
		panic("SMP: fork failed for CPU:%d", cpuid);

	task_thread_info(idle)->cpu = cpuid;

	/* Let _start know what logical CPU we're booting
	** (offset into init_tasks[],cpu_data[])
	*/
	cpu_now_booting = cpuid;

	/* 
	** boot strap code needs to know the task address since
	** it also contains the process stack.
	*/
	smp_init_current_idle_task = idle ;
	mb();

	printk("Releasing cpu %d now, hpa=%lx\n", cpuid, cpu_data[cpuid].hpa);

	/*
	** This gets PDC to release the CPU from a very tight loop.
	**
	** From the PA-RISC 2.0 Firmware Architecture Reference Specification:
	** "The MEM_RENDEZ vector specifies the location of OS_RENDEZ which 
	** is executed after receiving the rendezvous signal (an interrupt to 
	** EIR{0}). MEM_RENDEZ is valid only when it is nonzero and the 
	** contents of memory are valid."
	*/
	gsc_writel(TIMER_IRQ - CPU_IRQ_BASE, cpu_data[cpuid].hpa);
	mb();

	/* 
	 * OK, wait a bit for that CPU to finish staggering about. 
	 * Slave will set a bit when it reaches smp_cpu_init().
	 * Once the "monarch CPU" sees the bit change, it can move on.
	 */
	for (timeout = 0; timeout < 10000; timeout++) {
		if(cpu_online(cpuid)) {
			/* Which implies Slave has started up */
			cpu_now_booting = 0;
			smp_init_current_idle_task = NULL;
			goto alive ;
		}
		udelay(100);
		barrier();
	}

	put_task_struct(idle);
	idle = NULL;

	printk(KERN_CRIT "SMP: CPU:%d is stuck.\n", cpuid);
	return -1;

alive:
	/* Remember the Slave data */
#if (kDEBUG>=100)
	printk(KERN_DEBUG "SMP: CPU:%d came alive after %ld _us\n",
		cpuid, timeout * 100);
#endif /* kDEBUG */
#ifdef ENTRY_SYS_CPUS
	cpu_data[cpuid].state = STATE_RUNNING;
#endif
	return 0;
}

void __devinit smp_prepare_boot_cpu(void)
{
	int bootstrap_processor=cpu_data[0].cpuid;	/* CPU ID of BSP */

#ifdef ENTRY_SYS_CPUS
	cpu_data[0].state = STATE_RUNNING;
#endif

	/* Setup BSP mappings */
	printk("SMP: bootstrap CPU ID is %d\n",bootstrap_processor);

	cpu_set(bootstrap_processor, cpu_online_map);
	cpu_set(bootstrap_processor, cpu_present_map);
}



/*
** inventory.c:do_inventory() hasn't yet been run and thus we
** don't 'discover' the additional CPU's until later.
*/
void __init smp_prepare_cpus(unsigned int max_cpus)
{
	cpus_clear(cpu_present_map);
	cpu_set(0, cpu_present_map);

	parisc_max_cpus = max_cpus;
	if (!max_cpus)
		printk(KERN_INFO "SMP mode deactivated.\n");
}


void smp_cpus_done(unsigned int cpu_max)
{
	return;
}


int __devinit __cpu_up(unsigned int cpu)
{
	if (cpu != 0 && cpu < parisc_max_cpus)
		smp_boot_one_cpu(cpu);

	return cpu_online(cpu) ? 0 : -ENOSYS;
}



#ifdef ENTRY_SYS_CPUS
/* Code goes along with:
**    entry.s:        ENTRY_NAME(sys_cpus)   / * 215, for cpu stat * /
*/
int sys_cpus(int argc, char **argv)
{
	int i,j=0;
	extern int current_pid(int cpu);

	if( argc > 2 ) {
		printk("sys_cpus:Only one argument supported\n");
		return (-1);
	}
	if ( argc == 1 ){
	
#ifdef DUMP_MORE_STATE
		for_each_online_cpu(i) {
			int cpus_per_line = 4;

			if (j++ % cpus_per_line)
				printk(" %3d",i);
			else
				printk("\n %3d",i);
		}
		printk("\n"); 
#else
	    	printk("\n 0\n"); 
#endif
	} else if((argc==2) && !(strcmp(argv[1],"-l"))) {
		printk("\nCPUSTATE  TASK CPUNUM CPUID HARDCPU(HPA)\n");
#ifdef DUMP_MORE_STATE
		for_each_online_cpu(i) {
			if (cpu_data[i].cpuid != NO_PROC_ID) {
				switch(cpu_data[i].state) {
					case STATE_RENDEZVOUS:
						printk("RENDEZVS ");
						break;
					case STATE_RUNNING:
						printk((current_pid(i)!=0) ? "RUNNING  " : "IDLING   ");
						break;
					case STATE_STOPPED:
						printk("STOPPED  ");
						break;
					case STATE_HALTED:
						printk("HALTED   ");
						break;
					default:
						printk("%08x?", cpu_data[i].state);
						break;
				}
				if(cpu_online(i)) {
					printk(" %4d",current_pid(i));
				}	
				printk(" %6d",cpu_number_map(i));
				printk(" %5d",i);
				printk(" 0x%lx\n",cpu_data[i].hpa);
			}	
		}
#else
		printk("\n%s  %4d      0     0 --------",
			(current->pid)?"RUNNING ": "IDLING  ",current->pid); 
#endif
	} else if ((argc==2) && !(strcmp(argv[1],"-s"))) { 
#ifdef DUMP_MORE_STATE
     		printk("\nCPUSTATE   CPUID\n");
		for_each_online_cpu(i) {
			if (cpu_data[i].cpuid != NO_PROC_ID) {
				switch(cpu_data[i].state) {
					case STATE_RENDEZVOUS:
						printk("RENDEZVS");break;
					case STATE_RUNNING:
						printk((current_pid(i)!=0) ? "RUNNING " : "IDLING");
						break;
					case STATE_STOPPED:
						printk("STOPPED ");break;
					case STATE_HALTED:
						printk("HALTED  ");break;
					default:
				}
				printk("  %5d\n",i);
			}	
		}
#else
		printk("\n%s    CPU0",(current->pid==0)?"RUNNING ":"IDLING  "); 
#endif
	} else {
		printk("sys_cpus:Unknown request\n");
		return (-1);
	}
	return 0;
}
#endif /* ENTRY_SYS_CPUS */

#ifdef CONFIG_PROC_FS
int __init
setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}
#endif
