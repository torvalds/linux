// SPDX-License-Identifier: GPL-2.0
/*
 *  SMP related functions
 *
 *    Copyright IBM Corp. 1999, 2012
 *    Author(s): Denis Joseph Barrow,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *
 *  based on other smp stuff by
 *    (c) 1995 Alan Cox, CymruNET Ltd  <alan@cymru.net>
 *    (c) 1998 Ingo Molnar
 *
 * The code outside of smp.c uses logical cpu numbers, only smp.c does
 * the translation of logical to physical cpu ids. All new code that
 * operates on physical cpu numbers needs to go into smp.c.
 */

#define KMSG_COMPONENT "cpu"
#define pr_fmt(fmt) KMSG_COMPONENT ": " fmt

#include <linux/workqueue.h>
#include <linux/memblock.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irqflags.h>
#include <linux/irq_work.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/sched/hotplug.h>
#include <linux/sched/task_stack.h>
#include <linux/crash_dump.h>
#include <linux/kprobes.h>
#include <asm/access-regs.h>
#include <asm/asm-offsets.h>
#include <asm/ctlreg.h>
#include <asm/pfault.h>
#include <asm/diag.h>
#include <asm/facility.h>
#include <asm/fpu.h>
#include <asm/ipl.h>
#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/tlbflush.h>
#include <asm/vtimer.h>
#include <asm/abs_lowcore.h>
#include <asm/sclp.h>
#include <asm/debug.h>
#include <asm/os_info.h>
#include <asm/sigp.h>
#include <asm/idle.h>
#include <asm/nmi.h>
#include <asm/stacktrace.h>
#include <asm/topology.h>
#include <asm/vdso.h>
#include <asm/maccess.h>
#include "entry.h"

enum {
	ec_schedule = 0,
	ec_call_function_single,
	ec_stop_cpu,
	ec_mcck_pending,
	ec_irq_work,
};

enum {
	CPU_STATE_STANDBY,
	CPU_STATE_CONFIGURED,
};

static u8 boot_core_type;
DEFINE_PER_CPU(struct pcpu, pcpu_devices);
/*
 * Pointer to the pcpu area of the boot CPU. This is required when a restart
 * interrupt is triggered on an offline CPU. For that case accessing percpu
 * data with the common primitives does not work, since the percpu offset is
 * stored in a non existent lowcore.
 */
static struct pcpu *ipl_pcpu;

unsigned int smp_cpu_mt_shift;
EXPORT_SYMBOL(smp_cpu_mt_shift);

unsigned int smp_cpu_mtid;
EXPORT_SYMBOL(smp_cpu_mtid);

#ifdef CONFIG_CRASH_DUMP
__vector128 __initdata boot_cpu_vector_save_area[__NUM_VXRS];
#endif

static unsigned int smp_max_threads __initdata = -1U;
cpumask_t cpu_setup_mask;

static int __init early_nosmt(char *s)
{
	smp_max_threads = 1;
	return 0;
}
early_param("nosmt", early_nosmt);

static int __init early_smt(char *s)
{
	get_option(&s, &smp_max_threads);
	return 0;
}
early_param("smt", early_smt);

/*
 * The smp_cpu_state_mutex must be held when changing the state or polarization
 * member of a pcpu data structure within the pcpu_devices array.
 */
DEFINE_MUTEX(smp_cpu_state_mutex);

/*
 * Signal processor helper functions.
 */
static inline int __pcpu_sigp_relax(u16 addr, u8 order, unsigned long parm)
{
	int cc;

	while (1) {
		cc = __pcpu_sigp(addr, order, parm, NULL);
		if (cc != SIGP_CC_BUSY)
			return cc;
		cpu_relax();
	}
}

static int pcpu_sigp_retry(struct pcpu *pcpu, u8 order, u32 parm)
{
	int cc, retry;

	for (retry = 0; ; retry++) {
		cc = __pcpu_sigp(pcpu->address, order, parm, NULL);
		if (cc != SIGP_CC_BUSY)
			break;
		if (retry >= 3)
			udelay(10);
	}
	return cc;
}

static inline int pcpu_stopped(struct pcpu *pcpu)
{
	u32 status;

	if (__pcpu_sigp(pcpu->address, SIGP_SENSE,
			0, &status) != SIGP_CC_STATUS_STORED)
		return 0;
	return !!(status & (SIGP_STATUS_CHECK_STOP|SIGP_STATUS_STOPPED));
}

static inline int pcpu_running(struct pcpu *pcpu)
{
	if (__pcpu_sigp(pcpu->address, SIGP_SENSE_RUNNING,
			0, NULL) != SIGP_CC_STATUS_STORED)
		return 1;
	/* Status stored condition code is equivalent to cpu not running. */
	return 0;
}

/*
 * Find struct pcpu by cpu address.
 */
static struct pcpu *pcpu_find_address(const struct cpumask *mask, u16 address)
{
	int cpu;

	for_each_cpu(cpu, mask)
		if (per_cpu(pcpu_devices, cpu).address == address)
			return &per_cpu(pcpu_devices, cpu);
	return NULL;
}

static void pcpu_ec_call(struct pcpu *pcpu, int ec_bit)
{
	int order;

	if (test_and_set_bit(ec_bit, &pcpu->ec_mask))
		return;
	order = pcpu_running(pcpu) ? SIGP_EXTERNAL_CALL : SIGP_EMERGENCY_SIGNAL;
	pcpu->ec_clk = get_tod_clock_fast();
	pcpu_sigp_retry(pcpu, order, 0);
}

static int pcpu_alloc_lowcore(struct pcpu *pcpu, int cpu)
{
	unsigned long async_stack, nodat_stack, mcck_stack;
	struct lowcore *lc;

	lc = (struct lowcore *) __get_free_pages(GFP_KERNEL | GFP_DMA, LC_ORDER);
	nodat_stack = __get_free_pages(GFP_KERNEL, THREAD_SIZE_ORDER);
	async_stack = stack_alloc();
	mcck_stack = stack_alloc();
	if (!lc || !nodat_stack || !async_stack || !mcck_stack)
		goto out;
	memcpy(lc, get_lowcore(), 512);
	memset((char *) lc + 512, 0, sizeof(*lc) - 512);
	lc->async_stack = async_stack + STACK_INIT_OFFSET;
	lc->nodat_stack = nodat_stack + STACK_INIT_OFFSET;
	lc->mcck_stack = mcck_stack + STACK_INIT_OFFSET;
	lc->cpu_nr = cpu;
	lc->spinlock_lockval = arch_spin_lockval(cpu);
	lc->spinlock_index = 0;
	lc->return_lpswe = gen_lpswe(__LC_RETURN_PSW);
	lc->return_mcck_lpswe = gen_lpswe(__LC_RETURN_MCCK_PSW);
	lc->preempt_count = PREEMPT_DISABLED;
	if (nmi_alloc_mcesa(&lc->mcesad))
		goto out;
	if (abs_lowcore_map(cpu, lc, true))
		goto out_mcesa;
	lowcore_ptr[cpu] = lc;
	pcpu_sigp_retry(pcpu, SIGP_SET_PREFIX, __pa(lc));
	return 0;

out_mcesa:
	nmi_free_mcesa(&lc->mcesad);
out:
	stack_free(mcck_stack);
	stack_free(async_stack);
	free_pages(nodat_stack, THREAD_SIZE_ORDER);
	free_pages((unsigned long) lc, LC_ORDER);
	return -ENOMEM;
}

static void pcpu_free_lowcore(struct pcpu *pcpu, int cpu)
{
	unsigned long async_stack, nodat_stack, mcck_stack;
	struct lowcore *lc;

	lc = lowcore_ptr[cpu];
	nodat_stack = lc->nodat_stack - STACK_INIT_OFFSET;
	async_stack = lc->async_stack - STACK_INIT_OFFSET;
	mcck_stack = lc->mcck_stack - STACK_INIT_OFFSET;
	pcpu_sigp_retry(pcpu, SIGP_SET_PREFIX, 0);
	lowcore_ptr[cpu] = NULL;
	abs_lowcore_unmap(cpu);
	nmi_free_mcesa(&lc->mcesad);
	stack_free(async_stack);
	stack_free(mcck_stack);
	free_pages(nodat_stack, THREAD_SIZE_ORDER);
	free_pages((unsigned long) lc, LC_ORDER);
}

static void pcpu_prepare_secondary(struct pcpu *pcpu, int cpu)
{
	struct lowcore *lc, *abs_lc;

	lc = lowcore_ptr[cpu];
	cpumask_set_cpu(cpu, &init_mm.context.cpu_attach_mask);
	cpumask_set_cpu(cpu, mm_cpumask(&init_mm));
	lc->cpu_nr = cpu;
	lc->pcpu = (unsigned long)pcpu;
	lc->restart_flags = RESTART_FLAG_CTLREGS;
	lc->spinlock_lockval = arch_spin_lockval(cpu);
	lc->spinlock_index = 0;
	lc->percpu_offset = __per_cpu_offset[cpu];
	lc->kernel_asce = get_lowcore()->kernel_asce;
	lc->user_asce = s390_invalid_asce;
	lc->machine_flags = get_lowcore()->machine_flags;
	lc->user_timer = lc->system_timer =
		lc->steal_timer = lc->avg_steal_timer = 0;
	abs_lc = get_abs_lowcore();
	memcpy(lc->cregs_save_area, abs_lc->cregs_save_area, sizeof(lc->cregs_save_area));
	put_abs_lowcore(abs_lc);
	lc->cregs_save_area[1] = lc->kernel_asce;
	lc->cregs_save_area[7] = lc->user_asce;
	save_access_regs((unsigned int *) lc->access_regs_save_area);
	arch_spin_lock_setup(cpu);
}

static void pcpu_attach_task(int cpu, struct task_struct *tsk)
{
	struct lowcore *lc;

	lc = lowcore_ptr[cpu];
	lc->kernel_stack = (unsigned long)task_stack_page(tsk) + STACK_INIT_OFFSET;
	lc->current_task = (unsigned long)tsk;
	lc->lpp = LPP_MAGIC;
	lc->current_pid = tsk->pid;
	lc->user_timer = tsk->thread.user_timer;
	lc->guest_timer = tsk->thread.guest_timer;
	lc->system_timer = tsk->thread.system_timer;
	lc->hardirq_timer = tsk->thread.hardirq_timer;
	lc->softirq_timer = tsk->thread.softirq_timer;
	lc->steal_timer = 0;
}

static void pcpu_start_fn(int cpu, void (*func)(void *), void *data)
{
	struct lowcore *lc;

	lc = lowcore_ptr[cpu];
	lc->restart_stack = lc->kernel_stack;
	lc->restart_fn = (unsigned long) func;
	lc->restart_data = (unsigned long) data;
	lc->restart_source = -1U;
	pcpu_sigp_retry(per_cpu_ptr(&pcpu_devices, cpu), SIGP_RESTART, 0);
}

typedef void (pcpu_delegate_fn)(void *);

/*
 * Call function via PSW restart on pcpu and stop the current cpu.
 */
static void __pcpu_delegate(pcpu_delegate_fn *func, void *data)
{
	func(data);	/* should not return */
}

static void pcpu_delegate(struct pcpu *pcpu, int cpu,
			  pcpu_delegate_fn *func,
			  void *data, unsigned long stack)
{
	struct lowcore *lc, *abs_lc;
	unsigned int source_cpu;

	lc = lowcore_ptr[cpu];
	source_cpu = stap();

	if (pcpu->address == source_cpu) {
		call_on_stack(2, stack, void, __pcpu_delegate,
			      pcpu_delegate_fn *, func, void *, data);
	}
	/* Stop target cpu (if func returns this stops the current cpu). */
	pcpu_sigp_retry(pcpu, SIGP_STOP, 0);
	pcpu_sigp_retry(pcpu, SIGP_CPU_RESET, 0);
	/* Restart func on the target cpu and stop the current cpu. */
	if (lc) {
		lc->restart_stack = stack;
		lc->restart_fn = (unsigned long)func;
		lc->restart_data = (unsigned long)data;
		lc->restart_source = source_cpu;
	} else {
		abs_lc = get_abs_lowcore();
		abs_lc->restart_stack = stack;
		abs_lc->restart_fn = (unsigned long)func;
		abs_lc->restart_data = (unsigned long)data;
		abs_lc->restart_source = source_cpu;
		put_abs_lowcore(abs_lc);
	}
	asm volatile(
		"0:	sigp	0,%0,%2	# sigp restart to target cpu\n"
		"	brc	2,0b	# busy, try again\n"
		"1:	sigp	0,%1,%3	# sigp stop to current cpu\n"
		"	brc	2,1b	# busy, try again\n"
		: : "d" (pcpu->address), "d" (source_cpu),
		    "K" (SIGP_RESTART), "K" (SIGP_STOP)
		: "0", "1", "cc");
	for (;;) ;
}

/*
 * Enable additional logical cpus for multi-threading.
 */
static int pcpu_set_smt(unsigned int mtid)
{
	int cc;

	if (smp_cpu_mtid == mtid)
		return 0;
	cc = __pcpu_sigp(0, SIGP_SET_MULTI_THREADING, mtid, NULL);
	if (cc == 0) {
		smp_cpu_mtid = mtid;
		smp_cpu_mt_shift = 0;
		while (smp_cpu_mtid >= (1U << smp_cpu_mt_shift))
			smp_cpu_mt_shift++;
		per_cpu(pcpu_devices, 0).address = stap();
	}
	return cc;
}

/*
 * Call function on the ipl CPU.
 */
void smp_call_ipl_cpu(void (*func)(void *), void *data)
{
	struct lowcore *lc = lowcore_ptr[0];

	if (ipl_pcpu->address == stap())
		lc = get_lowcore();

	pcpu_delegate(ipl_pcpu, 0, func, data, lc->nodat_stack);
}

int smp_find_processor_id(u16 address)
{
	int cpu;

	for_each_present_cpu(cpu)
		if (per_cpu(pcpu_devices, cpu).address == address)
			return cpu;
	return -1;
}

void schedule_mcck_handler(void)
{
	pcpu_ec_call(this_cpu_ptr(&pcpu_devices), ec_mcck_pending);
}

bool notrace arch_vcpu_is_preempted(int cpu)
{
	if (test_cpu_flag_of(CIF_ENABLED_WAIT, cpu))
		return false;
	if (pcpu_running(per_cpu_ptr(&pcpu_devices, cpu)))
		return false;
	return true;
}
EXPORT_SYMBOL(arch_vcpu_is_preempted);

void notrace smp_yield_cpu(int cpu)
{
	if (!MACHINE_HAS_DIAG9C)
		return;
	diag_stat_inc_norecursion(DIAG_STAT_X09C);
	asm volatile("diag %0,0,0x9c"
		     : : "d" (per_cpu(pcpu_devices, cpu).address));
}
EXPORT_SYMBOL_GPL(smp_yield_cpu);

/*
 * Send cpus emergency shutdown signal. This gives the cpus the
 * opportunity to complete outstanding interrupts.
 */
void notrace smp_emergency_stop(void)
{
	static arch_spinlock_t lock = __ARCH_SPIN_LOCK_UNLOCKED;
	static cpumask_t cpumask;
	u64 end;
	int cpu;

	arch_spin_lock(&lock);
	cpumask_copy(&cpumask, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), &cpumask);

	end = get_tod_clock() + (1000000UL << 12);
	for_each_cpu(cpu, &cpumask) {
		struct pcpu *pcpu = per_cpu_ptr(&pcpu_devices, cpu);
		set_bit(ec_stop_cpu, &pcpu->ec_mask);
		while (__pcpu_sigp(pcpu->address, SIGP_EMERGENCY_SIGNAL,
				   0, NULL) == SIGP_CC_BUSY &&
		       get_tod_clock() < end)
			cpu_relax();
	}
	while (get_tod_clock() < end) {
		for_each_cpu(cpu, &cpumask)
			if (pcpu_stopped(per_cpu_ptr(&pcpu_devices, cpu)))
				cpumask_clear_cpu(cpu, &cpumask);
		if (cpumask_empty(&cpumask))
			break;
		cpu_relax();
	}
	arch_spin_unlock(&lock);
}
NOKPROBE_SYMBOL(smp_emergency_stop);

/*
 * Stop all cpus but the current one.
 */
void smp_send_stop(void)
{
	struct pcpu *pcpu;
	int cpu;

	/* Disable all interrupts/machine checks */
	__load_psw_mask(PSW_KERNEL_BITS);
	trace_hardirqs_off();

	debug_set_critical();

	if (oops_in_progress)
		smp_emergency_stop();

	/* stop all processors */
	for_each_online_cpu(cpu) {
		if (cpu == smp_processor_id())
			continue;
		pcpu = per_cpu_ptr(&pcpu_devices, cpu);
		pcpu_sigp_retry(pcpu, SIGP_STOP, 0);
		while (!pcpu_stopped(pcpu))
			cpu_relax();
	}
}

/*
 * This is the main routine where commands issued by other
 * cpus are handled.
 */
static void smp_handle_ext_call(void)
{
	unsigned long bits;

	/* handle bit signal external calls */
	bits = this_cpu_xchg(pcpu_devices.ec_mask, 0);
	if (test_bit(ec_stop_cpu, &bits))
		smp_stop_cpu();
	if (test_bit(ec_schedule, &bits))
		scheduler_ipi();
	if (test_bit(ec_call_function_single, &bits))
		generic_smp_call_function_single_interrupt();
	if (test_bit(ec_mcck_pending, &bits))
		s390_handle_mcck();
	if (test_bit(ec_irq_work, &bits))
		irq_work_run();
}

static void do_ext_call_interrupt(struct ext_code ext_code,
				  unsigned int param32, unsigned long param64)
{
	inc_irq_stat(ext_code.code == 0x1202 ? IRQEXT_EXC : IRQEXT_EMS);
	smp_handle_ext_call();
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	int cpu;

	for_each_cpu(cpu, mask)
		pcpu_ec_call(per_cpu_ptr(&pcpu_devices, cpu), ec_call_function_single);
}

void arch_send_call_function_single_ipi(int cpu)
{
	pcpu_ec_call(per_cpu_ptr(&pcpu_devices, cpu), ec_call_function_single);
}

/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */
void arch_smp_send_reschedule(int cpu)
{
	pcpu_ec_call(per_cpu_ptr(&pcpu_devices, cpu), ec_schedule);
}

#ifdef CONFIG_IRQ_WORK
void arch_irq_work_raise(void)
{
	pcpu_ec_call(this_cpu_ptr(&pcpu_devices), ec_irq_work);
}
#endif

#ifdef CONFIG_CRASH_DUMP

int smp_store_status(int cpu)
{
	struct lowcore *lc;
	struct pcpu *pcpu;
	unsigned long pa;

	pcpu = per_cpu_ptr(&pcpu_devices, cpu);
	lc = lowcore_ptr[cpu];
	pa = __pa(&lc->floating_pt_save_area);
	if (__pcpu_sigp_relax(pcpu->address, SIGP_STORE_STATUS_AT_ADDRESS,
			      pa) != SIGP_CC_ORDER_CODE_ACCEPTED)
		return -EIO;
	if (!cpu_has_vx() && !MACHINE_HAS_GS)
		return 0;
	pa = lc->mcesad & MCESA_ORIGIN_MASK;
	if (MACHINE_HAS_GS)
		pa |= lc->mcesad & MCESA_LC_MASK;
	if (__pcpu_sigp_relax(pcpu->address, SIGP_STORE_ADDITIONAL_STATUS,
			      pa) != SIGP_CC_ORDER_CODE_ACCEPTED)
		return -EIO;
	return 0;
}

/*
 * Collect CPU state of the previous, crashed system.
 * There are three cases:
 * 1) standard zfcp/nvme dump
 *    condition: OLDMEM_BASE == NULL && is_ipl_type_dump() == true
 *    The state for all CPUs except the boot CPU needs to be collected
 *    with sigp stop-and-store-status. The boot CPU state is located in
 *    the absolute lowcore of the memory stored in the HSA. The zcore code
 *    will copy the boot CPU state from the HSA.
 * 2) stand-alone kdump for SCSI/NVMe (zfcp/nvme dump with swapped memory)
 *    condition: OLDMEM_BASE != NULL && is_ipl_type_dump() == true
 *    The state for all CPUs except the boot CPU needs to be collected
 *    with sigp stop-and-store-status. The firmware or the boot-loader
 *    stored the registers of the boot CPU in the absolute lowcore in the
 *    memory of the old system.
 * 3) kdump or stand-alone kdump for DASD
 *    condition: OLDMEM_BASE != NULL && is_ipl_type_dump() == false
 *    The state for all CPUs except the boot CPU needs to be collected
 *    with sigp stop-and-store-status. The kexec code or the boot-loader
 *    stored the registers of the boot CPU in the memory of the old system.
 *
 * Note that the legacy kdump mode where the old kernel stored the CPU states
 * does no longer exist: setup_arch() explicitly deactivates the elfcorehdr=
 * kernel parameter. The is_kdump_kernel() implementation on s390 is independent
 * of the elfcorehdr= parameter.
 */
static bool dump_available(void)
{
	return oldmem_data.start || is_ipl_type_dump();
}

void __init smp_save_dump_ipl_cpu(void)
{
	struct save_area *sa;
	void *regs;

	if (!dump_available())
		return;
	sa = save_area_alloc(true);
	regs = memblock_alloc(512, 8);
	if (!sa || !regs)
		panic("could not allocate memory for boot CPU save area\n");
	copy_oldmem_kernel(regs, __LC_FPREGS_SAVE_AREA, 512);
	save_area_add_regs(sa, regs);
	memblock_free(regs, 512);
	if (cpu_has_vx())
		save_area_add_vxrs(sa, boot_cpu_vector_save_area);
}

void __init smp_save_dump_secondary_cpus(void)
{
	int addr, boot_cpu_addr, max_cpu_addr;
	struct save_area *sa;
	void *page;

	if (!dump_available())
		return;
	/* Allocate a page as dumping area for the store status sigps */
	page = memblock_alloc_low(PAGE_SIZE, PAGE_SIZE);
	if (!page)
		panic("ERROR: Failed to allocate %lx bytes below %lx\n",
		      PAGE_SIZE, 1UL << 31);

	/* Set multi-threading state to the previous system. */
	pcpu_set_smt(sclp.mtid_prev);
	boot_cpu_addr = stap();
	max_cpu_addr = SCLP_MAX_CORES << sclp.mtid_prev;
	for (addr = 0; addr <= max_cpu_addr; addr++) {
		if (addr == boot_cpu_addr)
			continue;
		if (__pcpu_sigp_relax(addr, SIGP_SENSE, 0) ==
		    SIGP_CC_NOT_OPERATIONAL)
			continue;
		sa = save_area_alloc(false);
		if (!sa)
			panic("could not allocate memory for save area\n");
		__pcpu_sigp_relax(addr, SIGP_STORE_STATUS_AT_ADDRESS, __pa(page));
		save_area_add_regs(sa, page);
		if (cpu_has_vx()) {
			__pcpu_sigp_relax(addr, SIGP_STORE_ADDITIONAL_STATUS, __pa(page));
			save_area_add_vxrs(sa, page);
		}
	}
	memblock_free(page, PAGE_SIZE);
	diag_amode31_ops.diag308_reset();
	pcpu_set_smt(0);
}
#endif /* CONFIG_CRASH_DUMP */

void smp_cpu_set_polarization(int cpu, int val)
{
	per_cpu(pcpu_devices, cpu).polarization = val;
}

int smp_cpu_get_polarization(int cpu)
{
	return per_cpu(pcpu_devices, cpu).polarization;
}

void smp_cpu_set_capacity(int cpu, unsigned long val)
{
	per_cpu(pcpu_devices, cpu).capacity = val;
}

unsigned long smp_cpu_get_capacity(int cpu)
{
	return per_cpu(pcpu_devices, cpu).capacity;
}

void smp_set_core_capacity(int cpu, unsigned long val)
{
	int i;

	cpu = smp_get_base_cpu(cpu);
	for (i = cpu; (i <= cpu + smp_cpu_mtid) && (i < nr_cpu_ids); i++)
		smp_cpu_set_capacity(i, val);
}

int smp_cpu_get_cpu_address(int cpu)
{
	return per_cpu(pcpu_devices, cpu).address;
}

static void __ref smp_get_core_info(struct sclp_core_info *info, int early)
{
	static int use_sigp_detection;
	int address;

	if (use_sigp_detection || sclp_get_core_info(info, early)) {
		use_sigp_detection = 1;
		for (address = 0;
		     address < (SCLP_MAX_CORES << smp_cpu_mt_shift);
		     address += (1U << smp_cpu_mt_shift)) {
			if (__pcpu_sigp_relax(address, SIGP_SENSE, 0) ==
			    SIGP_CC_NOT_OPERATIONAL)
				continue;
			info->core[info->configured].core_id =
				address >> smp_cpu_mt_shift;
			info->configured++;
		}
		info->combined = info->configured;
	}
}

static int smp_add_core(struct sclp_core_entry *core, cpumask_t *avail,
			bool configured, bool early)
{
	struct pcpu *pcpu;
	int cpu, nr, i;
	u16 address;

	nr = 0;
	if (sclp.has_core_type && core->type != boot_core_type)
		return nr;
	cpu = cpumask_first(avail);
	address = core->core_id << smp_cpu_mt_shift;
	for (i = 0; (i <= smp_cpu_mtid) && (cpu < nr_cpu_ids); i++) {
		if (pcpu_find_address(cpu_present_mask, address + i))
			continue;
		pcpu = per_cpu_ptr(&pcpu_devices, cpu);
		pcpu->address = address + i;
		if (configured)
			pcpu->state = CPU_STATE_CONFIGURED;
		else
			pcpu->state = CPU_STATE_STANDBY;
		smp_cpu_set_polarization(cpu, POLARIZATION_UNKNOWN);
		smp_cpu_set_capacity(cpu, CPU_CAPACITY_HIGH);
		set_cpu_present(cpu, true);
		if (!early && arch_register_cpu(cpu))
			set_cpu_present(cpu, false);
		else
			nr++;
		cpumask_clear_cpu(cpu, avail);
		cpu = cpumask_next(cpu, avail);
	}
	return nr;
}

static int __smp_rescan_cpus(struct sclp_core_info *info, bool early)
{
	struct sclp_core_entry *core;
	static cpumask_t avail;
	bool configured;
	u16 core_id;
	int nr, i;

	cpus_read_lock();
	mutex_lock(&smp_cpu_state_mutex);
	nr = 0;
	cpumask_xor(&avail, cpu_possible_mask, cpu_present_mask);
	/*
	 * Add IPL core first (which got logical CPU number 0) to make sure
	 * that all SMT threads get subsequent logical CPU numbers.
	 */
	if (early) {
		core_id = per_cpu(pcpu_devices, 0).address >> smp_cpu_mt_shift;
		for (i = 0; i < info->configured; i++) {
			core = &info->core[i];
			if (core->core_id == core_id) {
				nr += smp_add_core(core, &avail, true, early);
				break;
			}
		}
	}
	for (i = 0; i < info->combined; i++) {
		configured = i < info->configured;
		nr += smp_add_core(&info->core[i], &avail, configured, early);
	}
	mutex_unlock(&smp_cpu_state_mutex);
	cpus_read_unlock();
	return nr;
}

void __init smp_detect_cpus(void)
{
	unsigned int cpu, mtid, c_cpus, s_cpus;
	struct sclp_core_info *info;
	u16 address;

	/* Get CPU information */
	info = memblock_alloc(sizeof(*info), 8);
	if (!info)
		panic("%s: Failed to allocate %zu bytes align=0x%x\n",
		      __func__, sizeof(*info), 8);
	smp_get_core_info(info, 1);
	/* Find boot CPU type */
	if (sclp.has_core_type) {
		address = stap();
		for (cpu = 0; cpu < info->combined; cpu++)
			if (info->core[cpu].core_id == address) {
				/* The boot cpu dictates the cpu type. */
				boot_core_type = info->core[cpu].type;
				break;
			}
		if (cpu >= info->combined)
			panic("Could not find boot CPU type");
	}

	/* Set multi-threading state for the current system */
	mtid = boot_core_type ? sclp.mtid : sclp.mtid_cp;
	mtid = (mtid < smp_max_threads) ? mtid : smp_max_threads - 1;
	pcpu_set_smt(mtid);

	/* Print number of CPUs */
	c_cpus = s_cpus = 0;
	for (cpu = 0; cpu < info->combined; cpu++) {
		if (sclp.has_core_type &&
		    info->core[cpu].type != boot_core_type)
			continue;
		if (cpu < info->configured)
			c_cpus += smp_cpu_mtid + 1;
		else
			s_cpus += smp_cpu_mtid + 1;
	}
	pr_info("%d configured CPUs, %d standby CPUs\n", c_cpus, s_cpus);
	memblock_free(info, sizeof(*info));
}

/*
 *	Activate a secondary processor.
 */
static void smp_start_secondary(void *cpuvoid)
{
	struct lowcore *lc = get_lowcore();
	int cpu = raw_smp_processor_id();

	lc->last_update_clock = get_tod_clock();
	lc->restart_stack = (unsigned long)restart_stack;
	lc->restart_fn = (unsigned long)do_restart;
	lc->restart_data = 0;
	lc->restart_source = -1U;
	lc->restart_flags = 0;
	restore_access_regs(lc->access_regs_save_area);
	cpu_init();
	rcutree_report_cpu_starting(cpu);
	init_cpu_timer();
	vtime_init();
	vdso_getcpu_init();
	pfault_init();
	cpumask_set_cpu(cpu, &cpu_setup_mask);
	update_cpu_masks();
	notify_cpu_starting(cpu);
	if (topology_cpu_dedicated(cpu))
		set_cpu_flag(CIF_DEDICATED_CPU);
	else
		clear_cpu_flag(CIF_DEDICATED_CPU);
	set_cpu_online(cpu, true);
	inc_irq_stat(CPU_RST);
	local_irq_enable();
	cpu_startup_entry(CPUHP_AP_ONLINE_IDLE);
}

/* Upping and downing of CPUs */
int __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	struct pcpu *pcpu = per_cpu_ptr(&pcpu_devices, cpu);
	int rc;

	if (pcpu->state != CPU_STATE_CONFIGURED)
		return -EIO;
	if (pcpu_sigp_retry(pcpu, SIGP_INITIAL_CPU_RESET, 0) !=
	    SIGP_CC_ORDER_CODE_ACCEPTED)
		return -EIO;

	rc = pcpu_alloc_lowcore(pcpu, cpu);
	if (rc)
		return rc;
	/*
	 * Make sure global control register contents do not change
	 * until new CPU has initialized control registers.
	 */
	system_ctlreg_lock();
	pcpu_prepare_secondary(pcpu, cpu);
	pcpu_attach_task(cpu, tidle);
	pcpu_start_fn(cpu, smp_start_secondary, NULL);
	/* Wait until cpu puts itself in the online & active maps */
	while (!cpu_online(cpu))
		cpu_relax();
	system_ctlreg_unlock();
	return 0;
}

static unsigned int setup_possible_cpus __initdata;

static int __init _setup_possible_cpus(char *s)
{
	get_option(&s, &setup_possible_cpus);
	return 0;
}
early_param("possible_cpus", _setup_possible_cpus);

int __cpu_disable(void)
{
	struct ctlreg cregs[16];
	int cpu;

	/* Handle possible pending IPIs */
	smp_handle_ext_call();
	cpu = smp_processor_id();
	set_cpu_online(cpu, false);
	cpumask_clear_cpu(cpu, &cpu_setup_mask);
	update_cpu_masks();
	/* Disable pseudo page faults on this cpu. */
	pfault_fini();
	/* Disable interrupt sources via control register. */
	__local_ctl_store(0, 15, cregs);
	cregs[0].val  &= ~0x0000ee70UL;	/* disable all external interrupts */
	cregs[6].val  &= ~0xff000000UL;	/* disable all I/O interrupts */
	cregs[14].val &= ~0x1f000000UL;	/* disable most machine checks */
	__local_ctl_load(0, 15, cregs);
	clear_cpu_flag(CIF_NOHZ_DELAY);
	return 0;
}

void __cpu_die(unsigned int cpu)
{
	struct pcpu *pcpu;

	/* Wait until target cpu is down */
	pcpu = per_cpu_ptr(&pcpu_devices, cpu);
	while (!pcpu_stopped(pcpu))
		cpu_relax();
	pcpu_free_lowcore(pcpu, cpu);
	cpumask_clear_cpu(cpu, mm_cpumask(&init_mm));
	cpumask_clear_cpu(cpu, &init_mm.context.cpu_attach_mask);
	pcpu->flags = 0;
}

void __noreturn cpu_die(void)
{
	idle_task_exit();
	pcpu_sigp_retry(this_cpu_ptr(&pcpu_devices), SIGP_STOP, 0);
	for (;;) ;
}

void __init smp_fill_possible_mask(void)
{
	unsigned int possible, sclp_max, cpu;

	sclp_max = max(sclp.mtid, sclp.mtid_cp) + 1;
	sclp_max = min(smp_max_threads, sclp_max);
	sclp_max = (sclp.max_cores * sclp_max) ?: nr_cpu_ids;
	possible = setup_possible_cpus ?: nr_cpu_ids;
	possible = min(possible, sclp_max);
	for (cpu = 0; cpu < possible && cpu < nr_cpu_ids; cpu++)
		set_cpu_possible(cpu, true);
}

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	if (register_external_irq(EXT_IRQ_EMERGENCY_SIG, do_ext_call_interrupt))
		panic("Couldn't request external interrupt 0x1201");
	system_ctl_set_bit(0, 14);
	if (register_external_irq(EXT_IRQ_EXTERNAL_CALL, do_ext_call_interrupt))
		panic("Couldn't request external interrupt 0x1202");
	system_ctl_set_bit(0, 13);
	smp_rescan_cpus(true);
}

void __init smp_prepare_boot_cpu(void)
{
	struct lowcore *lc = get_lowcore();

	WARN_ON(!cpu_present(0) || !cpu_online(0));
	lc->percpu_offset = __per_cpu_offset[0];
	ipl_pcpu = per_cpu_ptr(&pcpu_devices, 0);
	ipl_pcpu->state = CPU_STATE_CONFIGURED;
	lc->pcpu = (unsigned long)ipl_pcpu;
	smp_cpu_set_polarization(0, POLARIZATION_UNKNOWN);
	smp_cpu_set_capacity(0, CPU_CAPACITY_HIGH);
}

void __init smp_setup_processor_id(void)
{
	struct lowcore *lc = get_lowcore();

	lc->cpu_nr = 0;
	per_cpu(pcpu_devices, 0).address = stap();
	lc->spinlock_lockval = arch_spin_lockval(0);
	lc->spinlock_index = 0;
}

/*
 * the frequency of the profiling timer can be changed
 * by writing a multiplier value into /proc/profile.
 *
 * usually you want to run this on all CPUs ;)
 */
int setup_profiling_timer(unsigned int multiplier)
{
	return 0;
}

static ssize_t cpu_configure_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	ssize_t count;

	mutex_lock(&smp_cpu_state_mutex);
	count = sysfs_emit(buf, "%d\n", per_cpu(pcpu_devices, dev->id).state);
	mutex_unlock(&smp_cpu_state_mutex);
	return count;
}

static ssize_t cpu_configure_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct pcpu *pcpu;
	int cpu, val, rc, i;
	char delim;

	if (sscanf(buf, "%d %c", &val, &delim) != 1)
		return -EINVAL;
	if (val != 0 && val != 1)
		return -EINVAL;
	cpus_read_lock();
	mutex_lock(&smp_cpu_state_mutex);
	rc = -EBUSY;
	/* disallow configuration changes of online cpus */
	cpu = dev->id;
	cpu = smp_get_base_cpu(cpu);
	for (i = 0; i <= smp_cpu_mtid; i++)
		if (cpu_online(cpu + i))
			goto out;
	pcpu = per_cpu_ptr(&pcpu_devices, cpu);
	rc = 0;
	switch (val) {
	case 0:
		if (pcpu->state != CPU_STATE_CONFIGURED)
			break;
		rc = sclp_core_deconfigure(pcpu->address >> smp_cpu_mt_shift);
		if (rc)
			break;
		for (i = 0; i <= smp_cpu_mtid; i++) {
			if (cpu + i >= nr_cpu_ids || !cpu_present(cpu + i))
				continue;
			per_cpu(pcpu_devices, cpu + i).state = CPU_STATE_STANDBY;
			smp_cpu_set_polarization(cpu + i,
						 POLARIZATION_UNKNOWN);
		}
		topology_expect_change();
		break;
	case 1:
		if (pcpu->state != CPU_STATE_STANDBY)
			break;
		rc = sclp_core_configure(pcpu->address >> smp_cpu_mt_shift);
		if (rc)
			break;
		for (i = 0; i <= smp_cpu_mtid; i++) {
			if (cpu + i >= nr_cpu_ids || !cpu_present(cpu + i))
				continue;
			per_cpu(pcpu_devices, cpu + i).state = CPU_STATE_CONFIGURED;
			smp_cpu_set_polarization(cpu + i,
						 POLARIZATION_UNKNOWN);
		}
		topology_expect_change();
		break;
	default:
		break;
	}
out:
	mutex_unlock(&smp_cpu_state_mutex);
	cpus_read_unlock();
	return rc ? rc : count;
}
static DEVICE_ATTR(configure, 0644, cpu_configure_show, cpu_configure_store);

static ssize_t show_cpu_address(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sysfs_emit(buf, "%d\n", per_cpu(pcpu_devices, dev->id).address);
}
static DEVICE_ATTR(address, 0444, show_cpu_address, NULL);

static struct attribute *cpu_common_attrs[] = {
	&dev_attr_configure.attr,
	&dev_attr_address.attr,
	NULL,
};

static struct attribute_group cpu_common_attr_group = {
	.attrs = cpu_common_attrs,
};

static struct attribute *cpu_online_attrs[] = {
	&dev_attr_idle_count.attr,
	&dev_attr_idle_time_us.attr,
	NULL,
};

static struct attribute_group cpu_online_attr_group = {
	.attrs = cpu_online_attrs,
};

static int smp_cpu_online(unsigned int cpu)
{
	struct cpu *c = per_cpu_ptr(&cpu_devices, cpu);

	return sysfs_create_group(&c->dev.kobj, &cpu_online_attr_group);
}

static int smp_cpu_pre_down(unsigned int cpu)
{
	struct cpu *c = per_cpu_ptr(&cpu_devices, cpu);

	sysfs_remove_group(&c->dev.kobj, &cpu_online_attr_group);
	return 0;
}

bool arch_cpu_is_hotpluggable(int cpu)
{
	return !!cpu;
}

int arch_register_cpu(int cpu)
{
	struct cpu *c = per_cpu_ptr(&cpu_devices, cpu);
	int rc;

	c->hotpluggable = arch_cpu_is_hotpluggable(cpu);
	rc = register_cpu(c, cpu);
	if (rc)
		goto out;
	rc = sysfs_create_group(&c->dev.kobj, &cpu_common_attr_group);
	if (rc)
		goto out_cpu;
	rc = topology_cpu_init(c);
	if (rc)
		goto out_topology;
	return 0;

out_topology:
	sysfs_remove_group(&c->dev.kobj, &cpu_common_attr_group);
out_cpu:
	unregister_cpu(c);
out:
	return rc;
}

int __ref smp_rescan_cpus(bool early)
{
	struct sclp_core_info *info;
	int nr;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	smp_get_core_info(info, 0);
	nr = __smp_rescan_cpus(info, early);
	kfree(info);
	if (nr)
		topology_schedule_update();
	return 0;
}

static ssize_t __ref rescan_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf,
				  size_t count)
{
	int rc;

	rc = lock_device_hotplug_sysfs();
	if (rc)
		return rc;
	rc = smp_rescan_cpus(false);
	unlock_device_hotplug();
	return rc ? rc : count;
}
static DEVICE_ATTR_WO(rescan);

static int __init s390_smp_init(void)
{
	struct device *dev_root;
	int rc;

	dev_root = bus_get_dev_root(&cpu_subsys);
	if (dev_root) {
		rc = device_create_file(dev_root, &dev_attr_rescan);
		put_device(dev_root);
		if (rc)
			return rc;
	}
	rc = cpuhp_setup_state(CPUHP_AP_ONLINE_DYN, "s390/smp:online",
			       smp_cpu_online, smp_cpu_pre_down);
	rc = rc <= 0 ? rc : 0;
	return rc;
}
subsys_initcall(s390_smp_init);
