/*
 *  SMP related functions
 *
 *    Copyright IBM Corp. 1999, 2012
 *    Author(s): Denis Joseph Barrow,
 *		 Martin Schwidefsky <schwidefsky@de.ibm.com>,
 *		 Heiko Carstens <heiko.carstens@de.ibm.com>,
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
#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/err.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include <linux/irqflags.h>
#include <linux/cpu.h>
#include <linux/slab.h>
#include <linux/crash_dump.h>
#include <asm/asm-offsets.h>
#include <asm/switch_to.h>
#include <asm/facility.h>
#include <asm/ipl.h>
#include <asm/setup.h>
#include <asm/irq.h>
#include <asm/tlbflush.h>
#include <asm/vtimer.h>
#include <asm/lowcore.h>
#include <asm/sclp.h>
#include <asm/vdso.h>
#include <asm/debug.h>
#include <asm/os_info.h>
#include <asm/sigp.h>
#include "entry.h"

enum {
	ec_schedule = 0,
	ec_call_function,
	ec_call_function_single,
	ec_stop_cpu,
};

enum {
	CPU_STATE_STANDBY,
	CPU_STATE_CONFIGURED,
};

struct pcpu {
	struct cpu cpu;
	struct _lowcore *lowcore;	/* lowcore page(s) for the cpu */
	unsigned long async_stack;	/* async stack for the cpu */
	unsigned long panic_stack;	/* panic stack for the cpu */
	unsigned long ec_mask;		/* bit mask for ec_xxx functions */
	int state;			/* physical cpu state */
	int polarization;		/* physical polarization */
	u16 address;			/* physical cpu address */
};

static u8 boot_cpu_type;
static u16 boot_cpu_address;
static struct pcpu pcpu_devices[NR_CPUS];

/*
 * The smp_cpu_state_mutex must be held when changing the state or polarization
 * member of a pcpu data structure within the pcpu_devices arreay.
 */
DEFINE_MUTEX(smp_cpu_state_mutex);

/*
 * Signal processor helper functions.
 */
static inline int __pcpu_sigp(u16 addr, u8 order, u32 parm, u32 *status)
{
	register unsigned int reg1 asm ("1") = parm;
	int cc;

	asm volatile(
		"	sigp	%1,%2,0(%3)\n"
		"	ipm	%0\n"
		"	srl	%0,28\n"
		: "=d" (cc), "+d" (reg1) : "d" (addr), "a" (order) : "cc");
	if (status && cc == 1)
		*status = reg1;
	return cc;
}

static inline int __pcpu_sigp_relax(u16 addr, u8 order, u32 parm, u32 *status)
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
	u32 uninitialized_var(status);

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
static struct pcpu *pcpu_find_address(const struct cpumask *mask, int address)
{
	int cpu;

	for_each_cpu(cpu, mask)
		if (pcpu_devices[cpu].address == address)
			return pcpu_devices + cpu;
	return NULL;
}

static void pcpu_ec_call(struct pcpu *pcpu, int ec_bit)
{
	int order;

	set_bit(ec_bit, &pcpu->ec_mask);
	order = pcpu_running(pcpu) ?
		SIGP_EXTERNAL_CALL : SIGP_EMERGENCY_SIGNAL;
	pcpu_sigp_retry(pcpu, order, 0);
}

static int __cpuinit pcpu_alloc_lowcore(struct pcpu *pcpu, int cpu)
{
	struct _lowcore *lc;

	if (pcpu != &pcpu_devices[0]) {
		pcpu->lowcore =	(struct _lowcore *)
			__get_free_pages(GFP_KERNEL | GFP_DMA, LC_ORDER);
		pcpu->async_stack = __get_free_pages(GFP_KERNEL, ASYNC_ORDER);
		pcpu->panic_stack = __get_free_page(GFP_KERNEL);
		if (!pcpu->lowcore || !pcpu->panic_stack || !pcpu->async_stack)
			goto out;
	}
	lc = pcpu->lowcore;
	memcpy(lc, &S390_lowcore, 512);
	memset((char *) lc + 512, 0, sizeof(*lc) - 512);
	lc->async_stack = pcpu->async_stack + ASYNC_SIZE;
	lc->panic_stack = pcpu->panic_stack + PAGE_SIZE;
	lc->cpu_nr = cpu;
#ifndef CONFIG_64BIT
	if (MACHINE_HAS_IEEE) {
		lc->extended_save_area_addr = get_zeroed_page(GFP_KERNEL);
		if (!lc->extended_save_area_addr)
			goto out;
	}
#else
	if (vdso_alloc_per_cpu(lc))
		goto out;
#endif
	lowcore_ptr[cpu] = lc;
	pcpu_sigp_retry(pcpu, SIGP_SET_PREFIX, (u32)(unsigned long) lc);
	return 0;
out:
	if (pcpu != &pcpu_devices[0]) {
		free_page(pcpu->panic_stack);
		free_pages(pcpu->async_stack, ASYNC_ORDER);
		free_pages((unsigned long) pcpu->lowcore, LC_ORDER);
	}
	return -ENOMEM;
}

#ifdef CONFIG_HOTPLUG_CPU

static void pcpu_free_lowcore(struct pcpu *pcpu)
{
	pcpu_sigp_retry(pcpu, SIGP_SET_PREFIX, 0);
	lowcore_ptr[pcpu - pcpu_devices] = NULL;
#ifndef CONFIG_64BIT
	if (MACHINE_HAS_IEEE) {
		struct _lowcore *lc = pcpu->lowcore;

		free_page((unsigned long) lc->extended_save_area_addr);
		lc->extended_save_area_addr = 0;
	}
#else
	vdso_free_per_cpu(pcpu->lowcore);
#endif
	if (pcpu != &pcpu_devices[0]) {
		free_page(pcpu->panic_stack);
		free_pages(pcpu->async_stack, ASYNC_ORDER);
		free_pages((unsigned long) pcpu->lowcore, LC_ORDER);
	}
}

#endif /* CONFIG_HOTPLUG_CPU */

static void pcpu_prepare_secondary(struct pcpu *pcpu, int cpu)
{
	struct _lowcore *lc = pcpu->lowcore;

	atomic_inc(&init_mm.context.attach_count);
	lc->cpu_nr = cpu;
	lc->percpu_offset = __per_cpu_offset[cpu];
	lc->kernel_asce = S390_lowcore.kernel_asce;
	lc->machine_flags = S390_lowcore.machine_flags;
	lc->ftrace_func = S390_lowcore.ftrace_func;
	lc->user_timer = lc->system_timer = lc->steal_timer = 0;
	__ctl_store(lc->cregs_save_area, 0, 15);
	save_access_regs((unsigned int *) lc->access_regs_save_area);
	memcpy(lc->stfle_fac_list, S390_lowcore.stfle_fac_list,
	       MAX_FACILITY_BIT/8);
}

static void pcpu_attach_task(struct pcpu *pcpu, struct task_struct *tsk)
{
	struct _lowcore *lc = pcpu->lowcore;
	struct thread_info *ti = task_thread_info(tsk);

	lc->kernel_stack = (unsigned long) task_stack_page(tsk) + THREAD_SIZE;
	lc->thread_info = (unsigned long) task_thread_info(tsk);
	lc->current_task = (unsigned long) tsk;
	lc->user_timer = ti->user_timer;
	lc->system_timer = ti->system_timer;
	lc->steal_timer = 0;
}

static void pcpu_start_fn(struct pcpu *pcpu, void (*func)(void *), void *data)
{
	struct _lowcore *lc = pcpu->lowcore;

	lc->restart_stack = lc->kernel_stack;
	lc->restart_fn = (unsigned long) func;
	lc->restart_data = (unsigned long) data;
	lc->restart_source = -1UL;
	pcpu_sigp_retry(pcpu, SIGP_RESTART, 0);
}

/*
 * Call function via PSW restart on pcpu and stop the current cpu.
 */
static void pcpu_delegate(struct pcpu *pcpu, void (*func)(void *),
			  void *data, unsigned long stack)
{
	struct _lowcore *lc = lowcore_ptr[pcpu - pcpu_devices];
	unsigned long source_cpu = stap();

	__load_psw_mask(psw_kernel_bits);
	if (pcpu->address == source_cpu)
		func(data);	/* should not return */
	/* Stop target cpu (if func returns this stops the current cpu). */
	pcpu_sigp_retry(pcpu, SIGP_STOP, 0);
	/* Restart func on the target cpu and stop the current cpu. */
	mem_assign_absolute(lc->restart_stack, stack);
	mem_assign_absolute(lc->restart_fn, (unsigned long) func);
	mem_assign_absolute(lc->restart_data, (unsigned long) data);
	mem_assign_absolute(lc->restart_source, source_cpu);
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
 * Call function on an online CPU.
 */
void smp_call_online_cpu(void (*func)(void *), void *data)
{
	struct pcpu *pcpu;

	/* Use the current cpu if it is online. */
	pcpu = pcpu_find_address(cpu_online_mask, stap());
	if (!pcpu)
		/* Use the first online cpu. */
		pcpu = pcpu_devices + cpumask_first(cpu_online_mask);
	pcpu_delegate(pcpu, func, data, (unsigned long) restart_stack);
}

/*
 * Call function on the ipl CPU.
 */
void smp_call_ipl_cpu(void (*func)(void *), void *data)
{
	pcpu_delegate(&pcpu_devices[0], func, data,
		      pcpu_devices->panic_stack + PAGE_SIZE);
}

int smp_find_processor_id(u16 address)
{
	int cpu;

	for_each_present_cpu(cpu)
		if (pcpu_devices[cpu].address == address)
			return cpu;
	return -1;
}

int smp_vcpu_scheduled(int cpu)
{
	return pcpu_running(pcpu_devices + cpu);
}

void smp_yield(void)
{
	if (MACHINE_HAS_DIAG44)
		asm volatile("diag 0,0,0x44");
}

void smp_yield_cpu(int cpu)
{
	if (MACHINE_HAS_DIAG9C)
		asm volatile("diag %0,0,0x9c"
			     : : "d" (pcpu_devices[cpu].address));
	else if (MACHINE_HAS_DIAG44)
		asm volatile("diag 0,0,0x44");
}

/*
 * Send cpus emergency shutdown signal. This gives the cpus the
 * opportunity to complete outstanding interrupts.
 */
void smp_emergency_stop(cpumask_t *cpumask)
{
	u64 end;
	int cpu;

	end = get_clock() + (1000000UL << 12);
	for_each_cpu(cpu, cpumask) {
		struct pcpu *pcpu = pcpu_devices + cpu;
		set_bit(ec_stop_cpu, &pcpu->ec_mask);
		while (__pcpu_sigp(pcpu->address, SIGP_EMERGENCY_SIGNAL,
				   0, NULL) == SIGP_CC_BUSY &&
		       get_clock() < end)
			cpu_relax();
	}
	while (get_clock() < end) {
		for_each_cpu(cpu, cpumask)
			if (pcpu_stopped(pcpu_devices + cpu))
				cpumask_clear_cpu(cpu, cpumask);
		if (cpumask_empty(cpumask))
			break;
		cpu_relax();
	}
}

/*
 * Stop all cpus but the current one.
 */
void smp_send_stop(void)
{
	cpumask_t cpumask;
	int cpu;

	/* Disable all interrupts/machine checks */
	__load_psw_mask(psw_kernel_bits | PSW_MASK_DAT);
	trace_hardirqs_off();

	debug_set_critical();
	cpumask_copy(&cpumask, cpu_online_mask);
	cpumask_clear_cpu(smp_processor_id(), &cpumask);

	if (oops_in_progress)
		smp_emergency_stop(&cpumask);

	/* stop all processors */
	for_each_cpu(cpu, &cpumask) {
		struct pcpu *pcpu = pcpu_devices + cpu;
		pcpu_sigp_retry(pcpu, SIGP_STOP, 0);
		while (!pcpu_stopped(pcpu))
			cpu_relax();
	}
}

/*
 * Stop the current cpu.
 */
void smp_stop_cpu(void)
{
	pcpu_sigp_retry(pcpu_devices + smp_processor_id(), SIGP_STOP, 0);
	for (;;) ;
}

/*
 * This is the main routine where commands issued by other
 * cpus are handled.
 */
static void do_ext_call_interrupt(struct ext_code ext_code,
				  unsigned int param32, unsigned long param64)
{
	unsigned long bits;
	int cpu;

	cpu = smp_processor_id();
	if (ext_code.code == 0x1202)
		kstat_cpu(cpu).irqs[EXTINT_EXC]++;
	else
		kstat_cpu(cpu).irqs[EXTINT_EMS]++;
	/*
	 * handle bit signal external calls
	 */
	bits = xchg(&pcpu_devices[cpu].ec_mask, 0);

	if (test_bit(ec_stop_cpu, &bits))
		smp_stop_cpu();

	if (test_bit(ec_schedule, &bits))
		scheduler_ipi();

	if (test_bit(ec_call_function, &bits))
		generic_smp_call_function_interrupt();

	if (test_bit(ec_call_function_single, &bits))
		generic_smp_call_function_single_interrupt();

}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	int cpu;

	for_each_cpu(cpu, mask)
		pcpu_ec_call(pcpu_devices + cpu, ec_call_function);
}

void arch_send_call_function_single_ipi(int cpu)
{
	pcpu_ec_call(pcpu_devices + cpu, ec_call_function_single);
}

#ifndef CONFIG_64BIT
/*
 * this function sends a 'purge tlb' signal to another CPU.
 */
static void smp_ptlb_callback(void *info)
{
	__tlb_flush_local();
}

void smp_ptlb_all(void)
{
	on_each_cpu(smp_ptlb_callback, NULL, 1);
}
EXPORT_SYMBOL(smp_ptlb_all);
#endif /* ! CONFIG_64BIT */

/*
 * this function sends a 'reschedule' IPI to another CPU.
 * it goes straight through and wastes no time serializing
 * anything. Worst case is that we lose a reschedule ...
 */
void smp_send_reschedule(int cpu)
{
	pcpu_ec_call(pcpu_devices + cpu, ec_schedule);
}

/*
 * parameter area for the set/clear control bit callbacks
 */
struct ec_creg_mask_parms {
	unsigned long orval;
	unsigned long andval;
	int cr;
};

/*
 * callback for setting/clearing control bits
 */
static void smp_ctl_bit_callback(void *info)
{
	struct ec_creg_mask_parms *pp = info;
	unsigned long cregs[16];

	__ctl_store(cregs, 0, 15);
	cregs[pp->cr] = (cregs[pp->cr] & pp->andval) | pp->orval;
	__ctl_load(cregs, 0, 15);
}

/*
 * Set a bit in a control register of all cpus
 */
void smp_ctl_set_bit(int cr, int bit)
{
	struct ec_creg_mask_parms parms = { 1UL << bit, -1UL, cr };

	on_each_cpu(smp_ctl_bit_callback, &parms, 1);
}
EXPORT_SYMBOL(smp_ctl_set_bit);

/*
 * Clear a bit in a control register of all cpus
 */
void smp_ctl_clear_bit(int cr, int bit)
{
	struct ec_creg_mask_parms parms = { 0, ~(1UL << bit), cr };

	on_each_cpu(smp_ctl_bit_callback, &parms, 1);
}
EXPORT_SYMBOL(smp_ctl_clear_bit);

#if defined(CONFIG_ZFCPDUMP) || defined(CONFIG_CRASH_DUMP)

struct save_area *zfcpdump_save_areas[NR_CPUS + 1];
EXPORT_SYMBOL_GPL(zfcpdump_save_areas);

static void __init smp_get_save_area(int cpu, u16 address)
{
	void *lc = pcpu_devices[0].lowcore;
	struct save_area *save_area;

	if (is_kdump_kernel())
		return;
	if (!OLDMEM_BASE && (address == boot_cpu_address ||
			     ipl_info.type != IPL_TYPE_FCP_DUMP))
		return;
	if (cpu >= NR_CPUS) {
		pr_warning("CPU %i exceeds the maximum %i and is excluded "
			   "from the dump\n", cpu, NR_CPUS - 1);
		return;
	}
	save_area = kmalloc(sizeof(struct save_area), GFP_KERNEL);
	if (!save_area)
		panic("could not allocate memory for save area\n");
	zfcpdump_save_areas[cpu] = save_area;
#ifdef CONFIG_CRASH_DUMP
	if (address == boot_cpu_address) {
		/* Copy the registers of the boot cpu. */
		copy_oldmem_page(1, (void *) save_area, sizeof(*save_area),
				 SAVE_AREA_BASE - PAGE_SIZE, 0);
		return;
	}
#endif
	/* Get the registers of a non-boot cpu. */
	__pcpu_sigp_relax(address, SIGP_STOP_AND_STORE_STATUS, 0, NULL);
	memcpy_real(save_area, lc + SAVE_AREA_BASE, sizeof(*save_area));
}

int smp_store_status(int cpu)
{
	struct pcpu *pcpu;

	pcpu = pcpu_devices + cpu;
	if (__pcpu_sigp_relax(pcpu->address, SIGP_STOP_AND_STORE_STATUS,
			      0, NULL) != SIGP_CC_ORDER_CODE_ACCEPTED)
		return -EIO;
	return 0;
}

#else /* CONFIG_ZFCPDUMP || CONFIG_CRASH_DUMP */

static inline void smp_get_save_area(int cpu, u16 address) { }

#endif /* CONFIG_ZFCPDUMP || CONFIG_CRASH_DUMP */

void smp_cpu_set_polarization(int cpu, int val)
{
	pcpu_devices[cpu].polarization = val;
}

int smp_cpu_get_polarization(int cpu)
{
	return pcpu_devices[cpu].polarization;
}

static struct sclp_cpu_info *smp_get_cpu_info(void)
{
	static int use_sigp_detection;
	struct sclp_cpu_info *info;
	int address;

	info = kzalloc(sizeof(*info), GFP_KERNEL);
	if (info && (use_sigp_detection || sclp_get_cpu_info(info))) {
		use_sigp_detection = 1;
		for (address = 0; address <= MAX_CPU_ADDRESS; address++) {
			if (__pcpu_sigp_relax(address, SIGP_SENSE, 0, NULL) ==
			    SIGP_CC_NOT_OPERATIONAL)
				continue;
			info->cpu[info->configured].address = address;
			info->configured++;
		}
		info->combined = info->configured;
	}
	return info;
}

static int __devinit smp_add_present_cpu(int cpu);

static int __devinit __smp_rescan_cpus(struct sclp_cpu_info *info,
				       int sysfs_add)
{
	struct pcpu *pcpu;
	cpumask_t avail;
	int cpu, nr, i;

	nr = 0;
	cpumask_xor(&avail, cpu_possible_mask, cpu_present_mask);
	cpu = cpumask_first(&avail);
	for (i = 0; (i < info->combined) && (cpu < nr_cpu_ids); i++) {
		if (info->has_cpu_type && info->cpu[i].type != boot_cpu_type)
			continue;
		if (pcpu_find_address(cpu_present_mask, info->cpu[i].address))
			continue;
		pcpu = pcpu_devices + cpu;
		pcpu->address = info->cpu[i].address;
		pcpu->state = (cpu >= info->configured) ?
			CPU_STATE_STANDBY : CPU_STATE_CONFIGURED;
		smp_cpu_set_polarization(cpu, POLARIZATION_UNKNOWN);
		set_cpu_present(cpu, true);
		if (sysfs_add && smp_add_present_cpu(cpu) != 0)
			set_cpu_present(cpu, false);
		else
			nr++;
		cpu = cpumask_next(cpu, &avail);
	}
	return nr;
}

static void __init smp_detect_cpus(void)
{
	unsigned int cpu, c_cpus, s_cpus;
	struct sclp_cpu_info *info;

	info = smp_get_cpu_info();
	if (!info)
		panic("smp_detect_cpus failed to allocate memory\n");
	if (info->has_cpu_type) {
		for (cpu = 0; cpu < info->combined; cpu++) {
			if (info->cpu[cpu].address != boot_cpu_address)
				continue;
			/* The boot cpu dictates the cpu type. */
			boot_cpu_type = info->cpu[cpu].type;
			break;
		}
	}
	c_cpus = s_cpus = 0;
	for (cpu = 0; cpu < info->combined; cpu++) {
		if (info->has_cpu_type && info->cpu[cpu].type != boot_cpu_type)
			continue;
		if (cpu < info->configured) {
			smp_get_save_area(c_cpus, info->cpu[cpu].address);
			c_cpus++;
		} else
			s_cpus++;
	}
	pr_info("%d configured CPUs, %d standby CPUs\n", c_cpus, s_cpus);
	get_online_cpus();
	__smp_rescan_cpus(info, 0);
	put_online_cpus();
	kfree(info);
}

/*
 *	Activate a secondary processor.
 */
static void __cpuinit smp_start_secondary(void *cpuvoid)
{
	S390_lowcore.last_update_clock = get_clock();
	S390_lowcore.restart_stack = (unsigned long) restart_stack;
	S390_lowcore.restart_fn = (unsigned long) do_restart;
	S390_lowcore.restart_data = 0;
	S390_lowcore.restart_source = -1UL;
	restore_access_regs(S390_lowcore.access_regs_save_area);
	__ctl_load(S390_lowcore.cregs_save_area, 0, 15);
	__load_psw_mask(psw_kernel_bits | PSW_MASK_DAT);
	cpu_init();
	preempt_disable();
	init_cpu_timer();
	init_cpu_vtimer();
	pfault_init();
	notify_cpu_starting(smp_processor_id());
	set_cpu_online(smp_processor_id(), true);
	local_irq_enable();
	/* cpu_idle will call schedule for us */
	cpu_idle();
}

/* Upping and downing of CPUs */
int __cpuinit __cpu_up(unsigned int cpu, struct task_struct *tidle)
{
	struct pcpu *pcpu;
	int rc;

	pcpu = pcpu_devices + cpu;
	if (pcpu->state != CPU_STATE_CONFIGURED)
		return -EIO;
	if (pcpu_sigp_retry(pcpu, SIGP_INITIAL_CPU_RESET, 0) !=
	    SIGP_CC_ORDER_CODE_ACCEPTED)
		return -EIO;

	rc = pcpu_alloc_lowcore(pcpu, cpu);
	if (rc)
		return rc;
	pcpu_prepare_secondary(pcpu, cpu);
	pcpu_attach_task(pcpu, tidle);
	pcpu_start_fn(pcpu, smp_start_secondary, NULL);
	while (!cpu_online(cpu))
		cpu_relax();
	return 0;
}

static int __init setup_possible_cpus(char *s)
{
	int max, cpu;

	if (kstrtoint(s, 0, &max) < 0)
		return 0;
	init_cpu_possible(cpumask_of(0));
	for (cpu = 1; cpu < max && cpu < nr_cpu_ids; cpu++)
		set_cpu_possible(cpu, true);
	return 0;
}
early_param("possible_cpus", setup_possible_cpus);

#ifdef CONFIG_HOTPLUG_CPU

int __cpu_disable(void)
{
	unsigned long cregs[16];

	set_cpu_online(smp_processor_id(), false);
	/* Disable pseudo page faults on this cpu. */
	pfault_fini();
	/* Disable interrupt sources via control register. */
	__ctl_store(cregs, 0, 15);
	cregs[0]  &= ~0x0000ee70UL;	/* disable all external interrupts */
	cregs[6]  &= ~0xff000000UL;	/* disable all I/O interrupts */
	cregs[14] &= ~0x1f000000UL;	/* disable most machine checks */
	__ctl_load(cregs, 0, 15);
	return 0;
}

void __cpu_die(unsigned int cpu)
{
	struct pcpu *pcpu;

	/* Wait until target cpu is down */
	pcpu = pcpu_devices + cpu;
	while (!pcpu_stopped(pcpu))
		cpu_relax();
	pcpu_free_lowcore(pcpu);
	atomic_dec(&init_mm.context.attach_count);
}

void __noreturn cpu_die(void)
{
	idle_task_exit();
	pcpu_sigp_retry(pcpu_devices + smp_processor_id(), SIGP_STOP, 0);
	for (;;) ;
}

#endif /* CONFIG_HOTPLUG_CPU */

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	/* request the 0x1201 emergency signal external interrupt */
	if (register_external_interrupt(0x1201, do_ext_call_interrupt) != 0)
		panic("Couldn't request external interrupt 0x1201");
	/* request the 0x1202 external call external interrupt */
	if (register_external_interrupt(0x1202, do_ext_call_interrupt) != 0)
		panic("Couldn't request external interrupt 0x1202");
	smp_detect_cpus();
}

void __init smp_prepare_boot_cpu(void)
{
	struct pcpu *pcpu = pcpu_devices;

	boot_cpu_address = stap();
	pcpu->state = CPU_STATE_CONFIGURED;
	pcpu->address = boot_cpu_address;
	pcpu->lowcore = (struct _lowcore *)(unsigned long) store_prefix();
	pcpu->async_stack = S390_lowcore.async_stack - ASYNC_SIZE;
	pcpu->panic_stack = S390_lowcore.panic_stack - PAGE_SIZE;
	S390_lowcore.percpu_offset = __per_cpu_offset[0];
	smp_cpu_set_polarization(0, POLARIZATION_UNKNOWN);
	set_cpu_present(0, true);
	set_cpu_online(0, true);
}

void __init smp_cpus_done(unsigned int max_cpus)
{
}

void __init smp_setup_processor_id(void)
{
	S390_lowcore.cpu_nr = 0;
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

#ifdef CONFIG_HOTPLUG_CPU
static ssize_t cpu_configure_show(struct device *dev,
				  struct device_attribute *attr, char *buf)
{
	ssize_t count;

	mutex_lock(&smp_cpu_state_mutex);
	count = sprintf(buf, "%d\n", pcpu_devices[dev->id].state);
	mutex_unlock(&smp_cpu_state_mutex);
	return count;
}

static ssize_t cpu_configure_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct pcpu *pcpu;
	int cpu, val, rc;
	char delim;

	if (sscanf(buf, "%d %c", &val, &delim) != 1)
		return -EINVAL;
	if (val != 0 && val != 1)
		return -EINVAL;
	get_online_cpus();
	mutex_lock(&smp_cpu_state_mutex);
	rc = -EBUSY;
	/* disallow configuration changes of online cpus and cpu 0 */
	cpu = dev->id;
	if (cpu_online(cpu) || cpu == 0)
		goto out;
	pcpu = pcpu_devices + cpu;
	rc = 0;
	switch (val) {
	case 0:
		if (pcpu->state != CPU_STATE_CONFIGURED)
			break;
		rc = sclp_cpu_deconfigure(pcpu->address);
		if (rc)
			break;
		pcpu->state = CPU_STATE_STANDBY;
		smp_cpu_set_polarization(cpu, POLARIZATION_UNKNOWN);
		topology_expect_change();
		break;
	case 1:
		if (pcpu->state != CPU_STATE_STANDBY)
			break;
		rc = sclp_cpu_configure(pcpu->address);
		if (rc)
			break;
		pcpu->state = CPU_STATE_CONFIGURED;
		smp_cpu_set_polarization(cpu, POLARIZATION_UNKNOWN);
		topology_expect_change();
		break;
	default:
		break;
	}
out:
	mutex_unlock(&smp_cpu_state_mutex);
	put_online_cpus();
	return rc ? rc : count;
}
static DEVICE_ATTR(configure, 0644, cpu_configure_show, cpu_configure_store);
#endif /* CONFIG_HOTPLUG_CPU */

static ssize_t show_cpu_address(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", pcpu_devices[dev->id].address);
}
static DEVICE_ATTR(address, 0444, show_cpu_address, NULL);

static struct attribute *cpu_common_attrs[] = {
#ifdef CONFIG_HOTPLUG_CPU
	&dev_attr_configure.attr,
#endif
	&dev_attr_address.attr,
	NULL,
};

static struct attribute_group cpu_common_attr_group = {
	.attrs = cpu_common_attrs,
};

static ssize_t show_idle_count(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct s390_idle_data *idle = &per_cpu(s390_idle, dev->id);
	unsigned long long idle_count;
	unsigned int sequence;

	do {
		sequence = ACCESS_ONCE(idle->sequence);
		idle_count = ACCESS_ONCE(idle->idle_count);
		if (ACCESS_ONCE(idle->clock_idle_enter))
			idle_count++;
	} while ((sequence & 1) || (idle->sequence != sequence));
	return sprintf(buf, "%llu\n", idle_count);
}
static DEVICE_ATTR(idle_count, 0444, show_idle_count, NULL);

static ssize_t show_idle_time(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	struct s390_idle_data *idle = &per_cpu(s390_idle, dev->id);
	unsigned long long now, idle_time, idle_enter, idle_exit;
	unsigned int sequence;

	do {
		now = get_clock();
		sequence = ACCESS_ONCE(idle->sequence);
		idle_time = ACCESS_ONCE(idle->idle_time);
		idle_enter = ACCESS_ONCE(idle->clock_idle_enter);
		idle_exit = ACCESS_ONCE(idle->clock_idle_exit);
	} while ((sequence & 1) || (idle->sequence != sequence));
	idle_time += idle_enter ? ((idle_exit ? : now) - idle_enter) : 0;
	return sprintf(buf, "%llu\n", idle_time >> 12);
}
static DEVICE_ATTR(idle_time_us, 0444, show_idle_time, NULL);

static struct attribute *cpu_online_attrs[] = {
	&dev_attr_idle_count.attr,
	&dev_attr_idle_time_us.attr,
	NULL,
};

static struct attribute_group cpu_online_attr_group = {
	.attrs = cpu_online_attrs,
};

static int __cpuinit smp_cpu_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned int)(long)hcpu;
	struct cpu *c = &pcpu_devices[cpu].cpu;
	struct device *s = &c->dev;
	int err = 0;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_ONLINE:
		err = sysfs_create_group(&s->kobj, &cpu_online_attr_group);
		break;
	case CPU_DEAD:
		sysfs_remove_group(&s->kobj, &cpu_online_attr_group);
		break;
	}
	return notifier_from_errno(err);
}

static int __devinit smp_add_present_cpu(int cpu)
{
	struct cpu *c = &pcpu_devices[cpu].cpu;
	struct device *s = &c->dev;
	int rc;

	c->hotpluggable = 1;
	rc = register_cpu(c, cpu);
	if (rc)
		goto out;
	rc = sysfs_create_group(&s->kobj, &cpu_common_attr_group);
	if (rc)
		goto out_cpu;
	if (cpu_online(cpu)) {
		rc = sysfs_create_group(&s->kobj, &cpu_online_attr_group);
		if (rc)
			goto out_online;
	}
	rc = topology_cpu_init(c);
	if (rc)
		goto out_topology;
	return 0;

out_topology:
	if (cpu_online(cpu))
		sysfs_remove_group(&s->kobj, &cpu_online_attr_group);
out_online:
	sysfs_remove_group(&s->kobj, &cpu_common_attr_group);
out_cpu:
#ifdef CONFIG_HOTPLUG_CPU
	unregister_cpu(c);
#endif
out:
	return rc;
}

#ifdef CONFIG_HOTPLUG_CPU

int __ref smp_rescan_cpus(void)
{
	struct sclp_cpu_info *info;
	int nr;

	info = smp_get_cpu_info();
	if (!info)
		return -ENOMEM;
	get_online_cpus();
	mutex_lock(&smp_cpu_state_mutex);
	nr = __smp_rescan_cpus(info, 1);
	mutex_unlock(&smp_cpu_state_mutex);
	put_online_cpus();
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

	rc = smp_rescan_cpus();
	return rc ? rc : count;
}
static DEVICE_ATTR(rescan, 0200, NULL, rescan_store);
#endif /* CONFIG_HOTPLUG_CPU */

static int __init s390_smp_init(void)
{
	int cpu, rc;

	hotcpu_notifier(smp_cpu_notify, 0);
#ifdef CONFIG_HOTPLUG_CPU
	rc = device_create_file(cpu_subsys.dev_root, &dev_attr_rescan);
	if (rc)
		return rc;
#endif
	for_each_present_cpu(cpu) {
		rc = smp_add_present_cpu(cpu);
		if (rc)
			return rc;
	}
	return 0;
}
subsys_initcall(s390_smp_init);
