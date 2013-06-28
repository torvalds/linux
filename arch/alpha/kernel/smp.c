/*
 *	linux/arch/alpha/kernel/smp.c
 *
 *      2001-07-09 Phil Ezolt (Phillip.Ezolt@compaq.com)
 *            Renamed modified smp_call_function to smp_call_function_on_cpu()
 *            Created an function that conforms to the old calling convention
 *            of smp_call_function().
 *
 *            This is helpful for DCPI.
 *
 */

#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/kernel_stat.h>
#include <linux/module.h>
#include <linux/sched.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/threads.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/spinlock.h>
#include <linux/irq.h>
#include <linux/cache.h>
#include <linux/profile.h>
#include <linux/bitops.h>
#include <linux/cpu.h>

#include <asm/hwrpb.h>
#include <asm/ptrace.h>
#include <linux/atomic.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/pgtable.h>
#include <asm/pgalloc.h>
#include <asm/mmu_context.h>
#include <asm/tlbflush.h>

#include "proto.h"
#include "irq_impl.h"


#define DEBUG_SMP 0
#if DEBUG_SMP
#define DBGS(args)	printk args
#else
#define DBGS(args)
#endif

/* A collection of per-processor data.  */
struct cpuinfo_alpha cpu_data[NR_CPUS];
EXPORT_SYMBOL(cpu_data);

/* A collection of single bit ipi messages.  */
static struct {
	unsigned long bits ____cacheline_aligned;
} ipi_data[NR_CPUS] __cacheline_aligned;

enum ipi_message_type {
	IPI_RESCHEDULE,
	IPI_CALL_FUNC,
	IPI_CALL_FUNC_SINGLE,
	IPI_CPU_STOP,
};

/* Set to a secondary's cpuid when it comes online.  */
static int smp_secondary_alive = 0;

int smp_num_probed;		/* Internal processor count */
int smp_num_cpus = 1;		/* Number that came online.  */
EXPORT_SYMBOL(smp_num_cpus);

/*
 * Called by both boot and secondaries to move global data into
 *  per-processor storage.
 */
static inline void __init
smp_store_cpu_info(int cpuid)
{
	cpu_data[cpuid].loops_per_jiffy = loops_per_jiffy;
	cpu_data[cpuid].last_asn = ASN_FIRST_VERSION;
	cpu_data[cpuid].need_new_asn = 0;
	cpu_data[cpuid].asn_lock = 0;
}

/*
 * Ideally sets up per-cpu profiling hooks.  Doesn't do much now...
 */
static inline void __init
smp_setup_percpu_timer(int cpuid)
{
	cpu_data[cpuid].prof_counter = 1;
	cpu_data[cpuid].prof_multiplier = 1;
}

static void __init
wait_boot_cpu_to_stop(int cpuid)
{
	unsigned long stop = jiffies + 10*HZ;

	while (time_before(jiffies, stop)) {
	        if (!smp_secondary_alive)
			return;
		barrier();
	}

	printk("wait_boot_cpu_to_stop: FAILED on CPU %d, hanging now\n", cpuid);
	for (;;)
		barrier();
}

/*
 * Where secondaries begin a life of C.
 */
void __cpuinit
smp_callin(void)
{
	int cpuid = hard_smp_processor_id();

	if (cpu_online(cpuid)) {
		printk("??, cpu 0x%x already present??\n", cpuid);
		BUG();
	}
	set_cpu_online(cpuid, true);

	/* Turn on machine checks.  */
	wrmces(7);

	/* Set trap vectors.  */
	trap_init();

	/* Set interrupt vector.  */
	wrent(entInt, 0);

	/* Get our local ticker going. */
	smp_setup_percpu_timer(cpuid);

	/* Call platform-specific callin, if specified */
	if (alpha_mv.smp_callin) alpha_mv.smp_callin();

	/* All kernel threads share the same mm context.  */
	atomic_inc(&init_mm.mm_count);
	current->active_mm = &init_mm;

	/* inform the notifiers about the new cpu */
	notify_cpu_starting(cpuid);

	/* Must have completely accurate bogos.  */
	local_irq_enable();

	/* Wait boot CPU to stop with irq enabled before running
	   calibrate_delay. */
	wait_boot_cpu_to_stop(cpuid);
	mb();
	calibrate_delay();

	smp_store_cpu_info(cpuid);
	/* Allow master to continue only after we written loops_per_jiffy.  */
	wmb();
	smp_secondary_alive = 1;

	DBGS(("smp_callin: commencing CPU %d current %p active_mm %p\n",
	      cpuid, current, current->active_mm));

	preempt_disable();
	cpu_startup_entry(CPUHP_ONLINE);
}

/* Wait until hwrpb->txrdy is clear for cpu.  Return -1 on timeout.  */
static int
wait_for_txrdy (unsigned long cpumask)
{
	unsigned long timeout;

	if (!(hwrpb->txrdy & cpumask))
		return 0;

	timeout = jiffies + 10*HZ;
	while (time_before(jiffies, timeout)) {
		if (!(hwrpb->txrdy & cpumask))
			return 0;
		udelay(10);
		barrier();
	}

	return -1;
}

/*
 * Send a message to a secondary's console.  "START" is one such
 * interesting message.  ;-)
 */
static void __cpuinit
send_secondary_console_msg(char *str, int cpuid)
{
	struct percpu_struct *cpu;
	register char *cp1, *cp2;
	unsigned long cpumask;
	size_t len;

	cpu = (struct percpu_struct *)
		((char*)hwrpb
		 + hwrpb->processor_offset
		 + cpuid * hwrpb->processor_size);

	cpumask = (1UL << cpuid);
	if (wait_for_txrdy(cpumask))
		goto timeout;

	cp2 = str;
	len = strlen(cp2);
	*(unsigned int *)&cpu->ipc_buffer[0] = len;
	cp1 = (char *) &cpu->ipc_buffer[1];
	memcpy(cp1, cp2, len);

	/* atomic test and set */
	wmb();
	set_bit(cpuid, &hwrpb->rxrdy);

	if (wait_for_txrdy(cpumask))
		goto timeout;
	return;

 timeout:
	printk("Processor %x not ready\n", cpuid);
}

/*
 * A secondary console wants to send a message.  Receive it.
 */
static void
recv_secondary_console_msg(void)
{
	int mycpu, i, cnt;
	unsigned long txrdy = hwrpb->txrdy;
	char *cp1, *cp2, buf[80];
	struct percpu_struct *cpu;

	DBGS(("recv_secondary_console_msg: TXRDY 0x%lx.\n", txrdy));

	mycpu = hard_smp_processor_id();

	for (i = 0; i < NR_CPUS; i++) {
		if (!(txrdy & (1UL << i)))
			continue;

		DBGS(("recv_secondary_console_msg: "
		      "TXRDY contains CPU %d.\n", i));

		cpu = (struct percpu_struct *)
		  ((char*)hwrpb
		   + hwrpb->processor_offset
		   + i * hwrpb->processor_size);

 		DBGS(("recv_secondary_console_msg: on %d from %d"
		      " HALT_REASON 0x%lx FLAGS 0x%lx\n",
		      mycpu, i, cpu->halt_reason, cpu->flags));

		cnt = cpu->ipc_buffer[0] >> 32;
		if (cnt <= 0 || cnt >= 80)
			strcpy(buf, "<<< BOGUS MSG >>>");
		else {
			cp1 = (char *) &cpu->ipc_buffer[11];
			cp2 = buf;
			strcpy(cp2, cp1);
			
			while ((cp2 = strchr(cp2, '\r')) != 0) {
				*cp2 = ' ';
				if (cp2[1] == '\n')
					cp2[1] = ' ';
			}
		}

		DBGS((KERN_INFO "recv_secondary_console_msg: on %d "
		      "message is '%s'\n", mycpu, buf));
	}

	hwrpb->txrdy = 0;
}

/*
 * Convince the console to have a secondary cpu begin execution.
 */
static int __cpuinit
secondary_cpu_start(int cpuid, struct task_struct *idle)
{
	struct percpu_struct *cpu;
	struct pcb_struct *hwpcb, *ipcb;
	unsigned long timeout;
	  
	cpu = (struct percpu_struct *)
		((char*)hwrpb
		 + hwrpb->processor_offset
		 + cpuid * hwrpb->processor_size);
	hwpcb = (struct pcb_struct *) cpu->hwpcb;
	ipcb = &task_thread_info(idle)->pcb;

	/* Initialize the CPU's HWPCB to something just good enough for
	   us to get started.  Immediately after starting, we'll swpctx
	   to the target idle task's pcb.  Reuse the stack in the mean
	   time.  Precalculate the target PCBB.  */
	hwpcb->ksp = (unsigned long)ipcb + sizeof(union thread_union) - 16;
	hwpcb->usp = 0;
	hwpcb->ptbr = ipcb->ptbr;
	hwpcb->pcc = 0;
	hwpcb->asn = 0;
	hwpcb->unique = virt_to_phys(ipcb);
	hwpcb->flags = ipcb->flags;
	hwpcb->res1 = hwpcb->res2 = 0;

#if 0
	DBGS(("KSP 0x%lx PTBR 0x%lx VPTBR 0x%lx UNIQUE 0x%lx\n",
	      hwpcb->ksp, hwpcb->ptbr, hwrpb->vptb, hwpcb->unique));
#endif
	DBGS(("Starting secondary cpu %d: state 0x%lx pal_flags 0x%lx\n",
	      cpuid, idle->state, ipcb->flags));

	/* Setup HWRPB fields that SRM uses to activate secondary CPU */
	hwrpb->CPU_restart = __smp_callin;
	hwrpb->CPU_restart_data = (unsigned long) __smp_callin;

	/* Recalculate and update the HWRPB checksum */
	hwrpb_update_checksum(hwrpb);

	/*
	 * Send a "start" command to the specified processor.
	 */

	/* SRM III 3.4.1.3 */
	cpu->flags |= 0x22;	/* turn on Context Valid and Restart Capable */
	cpu->flags &= ~1;	/* turn off Bootstrap In Progress */
	wmb();

	send_secondary_console_msg("START\r\n", cpuid);

	/* Wait 10 seconds for an ACK from the console.  */
	timeout = jiffies + 10*HZ;
	while (time_before(jiffies, timeout)) {
		if (cpu->flags & 1)
			goto started;
		udelay(10);
		barrier();
	}
	printk(KERN_ERR "SMP: Processor %d failed to start.\n", cpuid);
	return -1;

 started:
	DBGS(("secondary_cpu_start: SUCCESS for CPU %d!!!\n", cpuid));
	return 0;
}

/*
 * Bring one cpu online.
 */
static int __cpuinit
smp_boot_one_cpu(int cpuid, struct task_struct *idle)
{
	unsigned long timeout;

	/* Signal the secondary to wait a moment.  */
	smp_secondary_alive = -1;

	/* Whirrr, whirrr, whirrrrrrrrr... */
	if (secondary_cpu_start(cpuid, idle))
		return -1;

	/* Notify the secondary CPU it can run calibrate_delay.  */
	mb();
	smp_secondary_alive = 0;

	/* We've been acked by the console; wait one second for
	   the task to start up for real.  */
	timeout = jiffies + 1*HZ;
	while (time_before(jiffies, timeout)) {
		if (smp_secondary_alive == 1)
			goto alive;
		udelay(10);
		barrier();
	}

	/* We failed to boot the CPU.  */

	printk(KERN_ERR "SMP: Processor %d is stuck.\n", cpuid);
	return -1;

 alive:
	/* Another "Red Snapper". */
	return 0;
}

/*
 * Called from setup_arch.  Detect an SMP system and which processors
 * are present.
 */
void __init
setup_smp(void)
{
	struct percpu_struct *cpubase, *cpu;
	unsigned long i;

	if (boot_cpuid != 0) {
		printk(KERN_WARNING "SMP: Booting off cpu %d instead of 0?\n",
		       boot_cpuid);
	}

	if (hwrpb->nr_processors > 1) {
		int boot_cpu_palrev;

		DBGS(("setup_smp: nr_processors %ld\n",
		      hwrpb->nr_processors));

		cpubase = (struct percpu_struct *)
			((char*)hwrpb + hwrpb->processor_offset);
		boot_cpu_palrev = cpubase->pal_revision;

		for (i = 0; i < hwrpb->nr_processors; i++) {
			cpu = (struct percpu_struct *)
				((char *)cpubase + i*hwrpb->processor_size);
			if ((cpu->flags & 0x1cc) == 0x1cc) {
				smp_num_probed++;
				set_cpu_possible(i, true);
				set_cpu_present(i, true);
				cpu->pal_revision = boot_cpu_palrev;
			}

			DBGS(("setup_smp: CPU %d: flags 0x%lx type 0x%lx\n",
			      i, cpu->flags, cpu->type));
			DBGS(("setup_smp: CPU %d: PAL rev 0x%lx\n",
			      i, cpu->pal_revision));
		}
	} else {
		smp_num_probed = 1;
	}

	printk(KERN_INFO "SMP: %d CPUs probed -- cpu_present_mask = %lx\n",
	       smp_num_probed, cpumask_bits(cpu_present_mask)[0]);
}

/*
 * Called by smp_init prepare the secondaries
 */
void __init
smp_prepare_cpus(unsigned int max_cpus)
{
	/* Take care of some initial bookkeeping.  */
	memset(ipi_data, 0, sizeof(ipi_data));

	current_thread_info()->cpu = boot_cpuid;

	smp_store_cpu_info(boot_cpuid);
	smp_setup_percpu_timer(boot_cpuid);

	/* Nothing to do on a UP box, or when told not to.  */
	if (smp_num_probed == 1 || max_cpus == 0) {
		init_cpu_possible(cpumask_of(boot_cpuid));
		init_cpu_present(cpumask_of(boot_cpuid));
		printk(KERN_INFO "SMP mode deactivated.\n");
		return;
	}

	printk(KERN_INFO "SMP starting up secondaries.\n");

	smp_num_cpus = smp_num_probed;
}

void
smp_prepare_boot_cpu(void)
{
}

int __cpuinit
__cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	smp_boot_one_cpu(cpu, tidle);

	return cpu_online(cpu) ? 0 : -ENOSYS;
}

void __init
smp_cpus_done(unsigned int max_cpus)
{
	int cpu;
	unsigned long bogosum = 0;

	for(cpu = 0; cpu < NR_CPUS; cpu++) 
		if (cpu_online(cpu))
			bogosum += cpu_data[cpu].loops_per_jiffy;
	
	printk(KERN_INFO "SMP: Total of %d processors activated "
	       "(%lu.%02lu BogoMIPS).\n",
	       num_online_cpus(), 
	       (bogosum + 2500) / (500000/HZ),
	       ((bogosum + 2500) / (5000/HZ)) % 100);
}


void
smp_percpu_timer_interrupt(struct pt_regs *regs)
{
	struct pt_regs *old_regs;
	int cpu = smp_processor_id();
	unsigned long user = user_mode(regs);
	struct cpuinfo_alpha *data = &cpu_data[cpu];

	old_regs = set_irq_regs(regs);

	/* Record kernel PC.  */
	profile_tick(CPU_PROFILING);

	if (!--data->prof_counter) {
		/* We need to make like a normal interrupt -- otherwise
		   timer interrupts ignore the global interrupt lock,
		   which would be a Bad Thing.  */
		irq_enter();

		update_process_times(user);

		data->prof_counter = data->prof_multiplier;

		irq_exit();
	}
	set_irq_regs(old_regs);
}

int
setup_profiling_timer(unsigned int multiplier)
{
	return -EINVAL;
}


static void
send_ipi_message(const struct cpumask *to_whom, enum ipi_message_type operation)
{
	int i;

	mb();
	for_each_cpu(i, to_whom)
		set_bit(operation, &ipi_data[i].bits);

	mb();
	for_each_cpu(i, to_whom)
		wripir(i);
}

void
handle_ipi(struct pt_regs *regs)
{
	int this_cpu = smp_processor_id();
	unsigned long *pending_ipis = &ipi_data[this_cpu].bits;
	unsigned long ops;

#if 0
	DBGS(("handle_ipi: on CPU %d ops 0x%lx PC 0x%lx\n",
	      this_cpu, *pending_ipis, regs->pc));
#endif

	mb();	/* Order interrupt and bit testing. */
	while ((ops = xchg(pending_ipis, 0)) != 0) {
	  mb();	/* Order bit clearing and data access. */
	  do {
		unsigned long which;

		which = ops & -ops;
		ops &= ~which;
		which = __ffs(which);

		switch (which) {
		case IPI_RESCHEDULE:
			scheduler_ipi();
			break;

		case IPI_CALL_FUNC:
			generic_smp_call_function_interrupt();
			break;

		case IPI_CALL_FUNC_SINGLE:
			generic_smp_call_function_single_interrupt();
			break;

		case IPI_CPU_STOP:
			halt();

		default:
			printk(KERN_CRIT "Unknown IPI on CPU %d: %lu\n",
			       this_cpu, which);
			break;
		}
	  } while (ops);

	  mb();	/* Order data access and bit testing. */
	}

	cpu_data[this_cpu].ipi_count++;

	if (hwrpb->txrdy)
		recv_secondary_console_msg();
}

void
smp_send_reschedule(int cpu)
{
#ifdef DEBUG_IPI_MSG
	if (cpu == hard_smp_processor_id())
		printk(KERN_WARNING
		       "smp_send_reschedule: Sending IPI to self.\n");
#endif
	send_ipi_message(cpumask_of(cpu), IPI_RESCHEDULE);
}

void
smp_send_stop(void)
{
	cpumask_t to_whom;
	cpumask_copy(&to_whom, cpu_possible_mask);
	cpumask_clear_cpu(smp_processor_id(), &to_whom);
#ifdef DEBUG_IPI_MSG
	if (hard_smp_processor_id() != boot_cpu_id)
		printk(KERN_WARNING "smp_send_stop: Not on boot cpu.\n");
#endif
	send_ipi_message(&to_whom, IPI_CPU_STOP);
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	send_ipi_message(mask, IPI_CALL_FUNC);
}

void arch_send_call_function_single_ipi(int cpu)
{
	send_ipi_message(cpumask_of(cpu), IPI_CALL_FUNC_SINGLE);
}

static void
ipi_imb(void *ignored)
{
	imb();
}

void
smp_imb(void)
{
	/* Must wait other processors to flush their icache before continue. */
	if (on_each_cpu(ipi_imb, NULL, 1))
		printk(KERN_CRIT "smp_imb: timed out\n");
}
EXPORT_SYMBOL(smp_imb);

static void
ipi_flush_tlb_all(void *ignored)
{
	tbia();
}

void
flush_tlb_all(void)
{
	/* Although we don't have any data to pass, we do want to
	   synchronize with the other processors.  */
	if (on_each_cpu(ipi_flush_tlb_all, NULL, 1)) {
		printk(KERN_CRIT "flush_tlb_all: timed out\n");
	}
}

#define asn_locked() (cpu_data[smp_processor_id()].asn_lock)

static void
ipi_flush_tlb_mm(void *x)
{
	struct mm_struct *mm = (struct mm_struct *) x;
	if (mm == current->active_mm && !asn_locked())
		flush_tlb_current(mm);
	else
		flush_tlb_other(mm);
}

void
flush_tlb_mm(struct mm_struct *mm)
{
	preempt_disable();

	if (mm == current->active_mm) {
		flush_tlb_current(mm);
		if (atomic_read(&mm->mm_users) <= 1) {
			int cpu, this_cpu = smp_processor_id();
			for (cpu = 0; cpu < NR_CPUS; cpu++) {
				if (!cpu_online(cpu) || cpu == this_cpu)
					continue;
				if (mm->context[cpu])
					mm->context[cpu] = 0;
			}
			preempt_enable();
			return;
		}
	}

	if (smp_call_function(ipi_flush_tlb_mm, mm, 1)) {
		printk(KERN_CRIT "flush_tlb_mm: timed out\n");
	}

	preempt_enable();
}
EXPORT_SYMBOL(flush_tlb_mm);

struct flush_tlb_page_struct {
	struct vm_area_struct *vma;
	struct mm_struct *mm;
	unsigned long addr;
};

static void
ipi_flush_tlb_page(void *x)
{
	struct flush_tlb_page_struct *data = (struct flush_tlb_page_struct *)x;
	struct mm_struct * mm = data->mm;

	if (mm == current->active_mm && !asn_locked())
		flush_tlb_current_page(mm, data->vma, data->addr);
	else
		flush_tlb_other(mm);
}

void
flush_tlb_page(struct vm_area_struct *vma, unsigned long addr)
{
	struct flush_tlb_page_struct data;
	struct mm_struct *mm = vma->vm_mm;

	preempt_disable();

	if (mm == current->active_mm) {
		flush_tlb_current_page(mm, vma, addr);
		if (atomic_read(&mm->mm_users) <= 1) {
			int cpu, this_cpu = smp_processor_id();
			for (cpu = 0; cpu < NR_CPUS; cpu++) {
				if (!cpu_online(cpu) || cpu == this_cpu)
					continue;
				if (mm->context[cpu])
					mm->context[cpu] = 0;
			}
			preempt_enable();
			return;
		}
	}

	data.vma = vma;
	data.mm = mm;
	data.addr = addr;

	if (smp_call_function(ipi_flush_tlb_page, &data, 1)) {
		printk(KERN_CRIT "flush_tlb_page: timed out\n");
	}

	preempt_enable();
}
EXPORT_SYMBOL(flush_tlb_page);

void
flush_tlb_range(struct vm_area_struct *vma, unsigned long start, unsigned long end)
{
	/* On the Alpha we always flush the whole user tlb.  */
	flush_tlb_mm(vma->vm_mm);
}
EXPORT_SYMBOL(flush_tlb_range);

static void
ipi_flush_icache_page(void *x)
{
	struct mm_struct *mm = (struct mm_struct *) x;
	if (mm == current->active_mm && !asn_locked())
		__load_new_mm_context(mm);
	else
		flush_tlb_other(mm);
}

void
flush_icache_user_range(struct vm_area_struct *vma, struct page *page,
			unsigned long addr, int len)
{
	struct mm_struct *mm = vma->vm_mm;

	if ((vma->vm_flags & VM_EXEC) == 0)
		return;

	preempt_disable();

	if (mm == current->active_mm) {
		__load_new_mm_context(mm);
		if (atomic_read(&mm->mm_users) <= 1) {
			int cpu, this_cpu = smp_processor_id();
			for (cpu = 0; cpu < NR_CPUS; cpu++) {
				if (!cpu_online(cpu) || cpu == this_cpu)
					continue;
				if (mm->context[cpu])
					mm->context[cpu] = 0;
			}
			preempt_enable();
			return;
		}
	}

	if (smp_call_function(ipi_flush_icache_page, mm, 1)) {
		printk(KERN_CRIT "flush_icache_page: timed out\n");
	}

	preempt_enable();
}
