/*
 *  arch/s390/kernel/smp.c
 *
 *    Copyright IBM Corp. 1999, 2009
 *    Author(s): Denis Joseph Barrow (djbarrow@de.ibm.com,barrow_dj@yahoo.com),
 *		 Martin Schwidefsky (schwidefsky@de.ibm.com)
 *		 Heiko Carstens (heiko.carstens@de.ibm.com)
 *
 *  based on other smp stuff by
 *    (c) 1995 Alan Cox, CymruNET Ltd  <alan@cymru.net>
 *    (c) 1998 Ingo Molnar
 *
 * We work with logical cpu numbering everywhere we can. The only
 * functions using the real cpu address (got from STAP) are the sigp
 * functions. For all other functions we use the identity mapping.
 * That means that cpu_number_map[i] == i for every cpu. cpu_number_map is
 * used e.g. to find the idle task belonging to a logical cpu. Every array
 * in the kernel is sorted by the logical cpu number and not by the physical
 * one which is causing all the confusion with __cpu_logical_map and
 * cpu_number_map in other architectures.
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
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/irqflags.h>
#include <linux/cpu.h>
#include <linux/timex.h>
#include <linux/bootmem.h>
#include <linux/slab.h>
#include <asm/asm-offsets.h>
#include <asm/ipl.h>
#include <asm/setup.h>
#include <asm/sigp.h>
#include <asm/pgalloc.h>
#include <asm/irq.h>
#include <asm/s390_ext.h>
#include <asm/cpcmd.h>
#include <asm/tlbflush.h>
#include <asm/timer.h>
#include <asm/lowcore.h>
#include <asm/sclp.h>
#include <asm/cputime.h>
#include <asm/vdso.h>
#include <asm/cpu.h>
#include "entry.h"

/* logical cpu to cpu address */
unsigned short __cpu_logical_map[NR_CPUS];

static struct task_struct *current_set[NR_CPUS];

static u8 smp_cpu_type;
static int smp_use_sigp_detection;

enum s390_cpu_state {
	CPU_STATE_STANDBY,
	CPU_STATE_CONFIGURED,
};

DEFINE_MUTEX(smp_cpu_state_mutex);
int smp_cpu_polarization[NR_CPUS];
static int smp_cpu_state[NR_CPUS];
static int cpu_management;

static DEFINE_PER_CPU(struct cpu, cpu_devices);

static void smp_ext_bitcall(int, int);

static int raw_cpu_stopped(int cpu)
{
	u32 status;

	switch (raw_sigp_ps(&status, 0, cpu, sigp_sense)) {
	case sigp_status_stored:
		/* Check for stopped and check stop state */
		if (status & 0x50)
			return 1;
		break;
	default:
		break;
	}
	return 0;
}

static inline int cpu_stopped(int cpu)
{
	return raw_cpu_stopped(cpu_logical_map(cpu));
}

void smp_switch_to_ipl_cpu(void (*func)(void *), void *data)
{
	struct _lowcore *lc, *current_lc;
	struct stack_frame *sf;
	struct pt_regs *regs;
	unsigned long sp;

	if (smp_processor_id() == 0)
		func(data);
	__load_psw_mask(PSW_BASE_BITS | PSW_DEFAULT_KEY);
	/* Disable lowcore protection */
	__ctl_clear_bit(0, 28);
	current_lc = lowcore_ptr[smp_processor_id()];
	lc = lowcore_ptr[0];
	if (!lc)
		lc = current_lc;
	lc->restart_psw.mask = PSW_BASE_BITS | PSW_DEFAULT_KEY;
	lc->restart_psw.addr = PSW_ADDR_AMODE | (unsigned long) smp_restart_cpu;
	if (!cpu_online(0))
		smp_switch_to_cpu(func, data, 0, stap(), __cpu_logical_map[0]);
	while (sigp(0, sigp_stop_and_store_status) == sigp_busy)
		cpu_relax();
	sp = lc->panic_stack;
	sp -= sizeof(struct pt_regs);
	regs = (struct pt_regs *) sp;
	memcpy(&regs->gprs, &current_lc->gpregs_save_area, sizeof(regs->gprs));
	regs->psw = lc->psw_save_area;
	sp -= STACK_FRAME_OVERHEAD;
	sf = (struct stack_frame *) sp;
	sf->back_chain = regs->gprs[15];
	smp_switch_to_cpu(func, data, sp, stap(), __cpu_logical_map[0]);
}

void smp_send_stop(void)
{
	int cpu, rc;

	/* Disable all interrupts/machine checks */
	__load_psw_mask(psw_kernel_bits & ~PSW_MASK_MCHECK);
	trace_hardirqs_off();

	/* stop all processors */
	for_each_online_cpu(cpu) {
		if (cpu == smp_processor_id())
			continue;
		do {
			rc = sigp(cpu, sigp_stop);
		} while (rc == sigp_busy);

		while (!cpu_stopped(cpu))
			cpu_relax();
	}
}

/*
 * This is the main routine where commands issued by other
 * cpus are handled.
 */

static void do_ext_call_interrupt(unsigned int ext_int_code,
				  unsigned int param32, unsigned long param64)
{
	unsigned long bits;

	kstat_cpu(smp_processor_id()).irqs[EXTINT_IPI]++;
	/*
	 * handle bit signal external calls
	 *
	 * For the ec_schedule signal we have to do nothing. All the work
	 * is done automatically when we return from the interrupt.
	 */
	bits = xchg(&S390_lowcore.ext_call_fast, 0);

	if (test_bit(ec_call_function, &bits))
		generic_smp_call_function_interrupt();

	if (test_bit(ec_call_function_single, &bits))
		generic_smp_call_function_single_interrupt();
}

/*
 * Send an external call sigp to another cpu and return without waiting
 * for its completion.
 */
static void smp_ext_bitcall(int cpu, int sig)
{
	/*
	 * Set signaling bit in lowcore of target cpu and kick it
	 */
	set_bit(sig, (unsigned long *) &lowcore_ptr[cpu]->ext_call_fast);
	while (sigp(cpu, sigp_emergency_signal) == sigp_busy)
		udelay(10);
}

void arch_send_call_function_ipi_mask(const struct cpumask *mask)
{
	int cpu;

	for_each_cpu(cpu, mask)
		smp_ext_bitcall(cpu, ec_call_function);
}

void arch_send_call_function_single_ipi(int cpu)
{
	smp_ext_bitcall(cpu, ec_call_function_single);
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
	smp_ext_bitcall(cpu, ec_schedule);
}

/*
 * parameter area for the set/clear control bit callbacks
 */
struct ec_creg_mask_parms {
	unsigned long orvals[16];
	unsigned long andvals[16];
};

/*
 * callback for setting/clearing control bits
 */
static void smp_ctl_bit_callback(void *info)
{
	struct ec_creg_mask_parms *pp = info;
	unsigned long cregs[16];
	int i;

	__ctl_store(cregs, 0, 15);
	for (i = 0; i <= 15; i++)
		cregs[i] = (cregs[i] & pp->andvals[i]) | pp->orvals[i];
	__ctl_load(cregs, 0, 15);
}

/*
 * Set a bit in a control register of all cpus
 */
void smp_ctl_set_bit(int cr, int bit)
{
	struct ec_creg_mask_parms parms;

	memset(&parms.orvals, 0, sizeof(parms.orvals));
	memset(&parms.andvals, 0xff, sizeof(parms.andvals));
	parms.orvals[cr] = 1 << bit;
	on_each_cpu(smp_ctl_bit_callback, &parms, 1);
}
EXPORT_SYMBOL(smp_ctl_set_bit);

/*
 * Clear a bit in a control register of all cpus
 */
void smp_ctl_clear_bit(int cr, int bit)
{
	struct ec_creg_mask_parms parms;

	memset(&parms.orvals, 0, sizeof(parms.orvals));
	memset(&parms.andvals, 0xff, sizeof(parms.andvals));
	parms.andvals[cr] = ~(1L << bit);
	on_each_cpu(smp_ctl_bit_callback, &parms, 1);
}
EXPORT_SYMBOL(smp_ctl_clear_bit);

#ifdef CONFIG_ZFCPDUMP

static void __init smp_get_save_area(unsigned int cpu, unsigned int phy_cpu)
{
	if (ipl_info.type != IPL_TYPE_FCP_DUMP)
		return;
	if (cpu >= NR_CPUS) {
		pr_warning("CPU %i exceeds the maximum %i and is excluded from "
			   "the dump\n", cpu, NR_CPUS - 1);
		return;
	}
	zfcpdump_save_areas[cpu] = kmalloc(sizeof(struct save_area), GFP_KERNEL);
	while (raw_sigp(phy_cpu, sigp_stop_and_store_status) == sigp_busy)
		cpu_relax();
	memcpy_real(zfcpdump_save_areas[cpu],
		    (void *)(unsigned long) store_prefix() + SAVE_AREA_BASE,
		    sizeof(struct save_area));
}

struct save_area *zfcpdump_save_areas[NR_CPUS + 1];
EXPORT_SYMBOL_GPL(zfcpdump_save_areas);

#else

static inline void smp_get_save_area(unsigned int cpu, unsigned int phy_cpu) { }

#endif /* CONFIG_ZFCPDUMP */

static int cpu_known(int cpu_id)
{
	int cpu;

	for_each_present_cpu(cpu) {
		if (__cpu_logical_map[cpu] == cpu_id)
			return 1;
	}
	return 0;
}

static int smp_rescan_cpus_sigp(cpumask_t avail)
{
	int cpu_id, logical_cpu;

	logical_cpu = cpumask_first(&avail);
	if (logical_cpu >= nr_cpu_ids)
		return 0;
	for (cpu_id = 0; cpu_id <= MAX_CPU_ADDRESS; cpu_id++) {
		if (cpu_known(cpu_id))
			continue;
		__cpu_logical_map[logical_cpu] = cpu_id;
		smp_cpu_polarization[logical_cpu] = POLARIZATION_UNKNWN;
		if (!cpu_stopped(logical_cpu))
			continue;
		cpu_set(logical_cpu, cpu_present_map);
		smp_cpu_state[logical_cpu] = CPU_STATE_CONFIGURED;
		logical_cpu = cpumask_next(logical_cpu, &avail);
		if (logical_cpu >= nr_cpu_ids)
			break;
	}
	return 0;
}

static int smp_rescan_cpus_sclp(cpumask_t avail)
{
	struct sclp_cpu_info *info;
	int cpu_id, logical_cpu, cpu;
	int rc;

	logical_cpu = cpumask_first(&avail);
	if (logical_cpu >= nr_cpu_ids)
		return 0;
	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		return -ENOMEM;
	rc = sclp_get_cpu_info(info);
	if (rc)
		goto out;
	for (cpu = 0; cpu < info->combined; cpu++) {
		if (info->has_cpu_type && info->cpu[cpu].type != smp_cpu_type)
			continue;
		cpu_id = info->cpu[cpu].address;
		if (cpu_known(cpu_id))
			continue;
		__cpu_logical_map[logical_cpu] = cpu_id;
		smp_cpu_polarization[logical_cpu] = POLARIZATION_UNKNWN;
		cpu_set(logical_cpu, cpu_present_map);
		if (cpu >= info->configured)
			smp_cpu_state[logical_cpu] = CPU_STATE_STANDBY;
		else
			smp_cpu_state[logical_cpu] = CPU_STATE_CONFIGURED;
		logical_cpu = cpumask_next(logical_cpu, &avail);
		if (logical_cpu >= nr_cpu_ids)
			break;
	}
out:
	kfree(info);
	return rc;
}

static int __smp_rescan_cpus(void)
{
	cpumask_t avail;

	cpus_xor(avail, cpu_possible_map, cpu_present_map);
	if (smp_use_sigp_detection)
		return smp_rescan_cpus_sigp(avail);
	else
		return smp_rescan_cpus_sclp(avail);
}

static void __init smp_detect_cpus(void)
{
	unsigned int cpu, c_cpus, s_cpus;
	struct sclp_cpu_info *info;
	u16 boot_cpu_addr, cpu_addr;

	c_cpus = 1;
	s_cpus = 0;
	boot_cpu_addr = __cpu_logical_map[0];
	info = kmalloc(sizeof(*info), GFP_KERNEL);
	if (!info)
		panic("smp_detect_cpus failed to allocate memory\n");
	/* Use sigp detection algorithm if sclp doesn't work. */
	if (sclp_get_cpu_info(info)) {
		smp_use_sigp_detection = 1;
		for (cpu = 0; cpu <= MAX_CPU_ADDRESS; cpu++) {
			if (cpu == boot_cpu_addr)
				continue;
			if (!raw_cpu_stopped(cpu))
				continue;
			smp_get_save_area(c_cpus, cpu);
			c_cpus++;
		}
		goto out;
	}

	if (info->has_cpu_type) {
		for (cpu = 0; cpu < info->combined; cpu++) {
			if (info->cpu[cpu].address == boot_cpu_addr) {
				smp_cpu_type = info->cpu[cpu].type;
				break;
			}
		}
	}

	for (cpu = 0; cpu < info->combined; cpu++) {
		if (info->has_cpu_type && info->cpu[cpu].type != smp_cpu_type)
			continue;
		cpu_addr = info->cpu[cpu].address;
		if (cpu_addr == boot_cpu_addr)
			continue;
		if (!raw_cpu_stopped(cpu_addr)) {
			s_cpus++;
			continue;
		}
		smp_get_save_area(c_cpus, cpu_addr);
		c_cpus++;
	}
out:
	kfree(info);
	pr_info("%d configured CPUs, %d standby CPUs\n", c_cpus, s_cpus);
	get_online_cpus();
	__smp_rescan_cpus();
	put_online_cpus();
}

/*
 *	Activate a secondary processor.
 */
int __cpuinit start_secondary(void *cpuvoid)
{
	/* Setup the cpu */
	cpu_init();
	preempt_disable();
	/* Enable TOD clock interrupts on the secondary cpu. */
	init_cpu_timer();
	/* Enable cpu timer interrupts on the secondary cpu. */
	init_cpu_vtimer();
	/* Enable pfault pseudo page faults on this cpu. */
	pfault_init();

	/* call cpu notifiers */
	notify_cpu_starting(smp_processor_id());
	/* Mark this cpu as online */
	ipi_call_lock();
	cpu_set(smp_processor_id(), cpu_online_map);
	ipi_call_unlock();
	/* Switch on interrupts */
	local_irq_enable();
	/* cpu_idle will call schedule for us */
	cpu_idle();
	return 0;
}

struct create_idle {
	struct work_struct work;
	struct task_struct *idle;
	struct completion done;
	int cpu;
};

static void __cpuinit smp_fork_idle(struct work_struct *work)
{
	struct create_idle *c_idle;

	c_idle = container_of(work, struct create_idle, work);
	c_idle->idle = fork_idle(c_idle->cpu);
	complete(&c_idle->done);
}

static int __cpuinit smp_alloc_lowcore(int cpu)
{
	unsigned long async_stack, panic_stack;
	struct _lowcore *lowcore;

	lowcore = (void *) __get_free_pages(GFP_KERNEL | GFP_DMA, LC_ORDER);
	if (!lowcore)
		return -ENOMEM;
	async_stack = __get_free_pages(GFP_KERNEL, ASYNC_ORDER);
	panic_stack = __get_free_page(GFP_KERNEL);
	if (!panic_stack || !async_stack)
		goto out;
	memcpy(lowcore, &S390_lowcore, 512);
	memset((char *)lowcore + 512, 0, sizeof(*lowcore) - 512);
	lowcore->async_stack = async_stack + ASYNC_SIZE;
	lowcore->panic_stack = panic_stack + PAGE_SIZE;

#ifndef CONFIG_64BIT
	if (MACHINE_HAS_IEEE) {
		unsigned long save_area;

		save_area = get_zeroed_page(GFP_KERNEL);
		if (!save_area)
			goto out;
		lowcore->extended_save_area_addr = (u32) save_area;
	}
#else
	if (vdso_alloc_per_cpu(cpu, lowcore))
		goto out;
#endif
	lowcore_ptr[cpu] = lowcore;
	return 0;

out:
	free_page(panic_stack);
	free_pages(async_stack, ASYNC_ORDER);
	free_pages((unsigned long) lowcore, LC_ORDER);
	return -ENOMEM;
}

static void smp_free_lowcore(int cpu)
{
	struct _lowcore *lowcore;

	lowcore = lowcore_ptr[cpu];
#ifndef CONFIG_64BIT
	if (MACHINE_HAS_IEEE)
		free_page((unsigned long) lowcore->extended_save_area_addr);
#else
	vdso_free_per_cpu(cpu, lowcore);
#endif
	free_page(lowcore->panic_stack - PAGE_SIZE);
	free_pages(lowcore->async_stack - ASYNC_SIZE, ASYNC_ORDER);
	free_pages((unsigned long) lowcore, LC_ORDER);
	lowcore_ptr[cpu] = NULL;
}

/* Upping and downing of CPUs */
int __cpuinit __cpu_up(unsigned int cpu)
{
	struct _lowcore *cpu_lowcore;
	struct create_idle c_idle;
	struct task_struct *idle;
	struct stack_frame *sf;
	u32 lowcore;
	int ccode;

	if (smp_cpu_state[cpu] != CPU_STATE_CONFIGURED)
		return -EIO;
	idle = current_set[cpu];
	if (!idle) {
		c_idle.done = COMPLETION_INITIALIZER_ONSTACK(c_idle.done);
		INIT_WORK_ONSTACK(&c_idle.work, smp_fork_idle);
		c_idle.cpu = cpu;
		schedule_work(&c_idle.work);
		wait_for_completion(&c_idle.done);
		if (IS_ERR(c_idle.idle))
			return PTR_ERR(c_idle.idle);
		idle = c_idle.idle;
		current_set[cpu] = c_idle.idle;
	}
	init_idle(idle, cpu);
	if (smp_alloc_lowcore(cpu))
		return -ENOMEM;
	do {
		ccode = sigp(cpu, sigp_initial_cpu_reset);
		if (ccode == sigp_busy)
			udelay(10);
		if (ccode == sigp_not_operational)
			goto err_out;
	} while (ccode == sigp_busy);

	lowcore = (u32)(unsigned long)lowcore_ptr[cpu];
	while (sigp_p(lowcore, cpu, sigp_set_prefix) == sigp_busy)
		udelay(10);

	cpu_lowcore = lowcore_ptr[cpu];
	cpu_lowcore->kernel_stack = (unsigned long)
		task_stack_page(idle) + THREAD_SIZE;
	cpu_lowcore->thread_info = (unsigned long) task_thread_info(idle);
	sf = (struct stack_frame *) (cpu_lowcore->kernel_stack
				     - sizeof(struct pt_regs)
				     - sizeof(struct stack_frame));
	memset(sf, 0, sizeof(struct stack_frame));
	sf->gprs[9] = (unsigned long) sf;
	cpu_lowcore->save_area[15] = (unsigned long) sf;
	__ctl_store(cpu_lowcore->cregs_save_area, 0, 15);
	atomic_inc(&init_mm.context.attach_count);
	asm volatile(
		"	stam	0,15,0(%0)"
		: : "a" (&cpu_lowcore->access_regs_save_area) : "memory");
	cpu_lowcore->percpu_offset = __per_cpu_offset[cpu];
	cpu_lowcore->current_task = (unsigned long) idle;
	cpu_lowcore->cpu_nr = cpu;
	cpu_lowcore->kernel_asce = S390_lowcore.kernel_asce;
	cpu_lowcore->machine_flags = S390_lowcore.machine_flags;
	cpu_lowcore->ftrace_func = S390_lowcore.ftrace_func;
	memcpy(cpu_lowcore->stfle_fac_list, S390_lowcore.stfle_fac_list,
	       MAX_FACILITY_BIT/8);
	eieio();

	while (sigp(cpu, sigp_restart) == sigp_busy)
		udelay(10);

	while (!cpu_online(cpu))
		cpu_relax();
	return 0;

err_out:
	smp_free_lowcore(cpu);
	return -EIO;
}

static int __init setup_possible_cpus(char *s)
{
	int pcpus, cpu;

	pcpus = simple_strtoul(s, NULL, 0);
	init_cpu_possible(cpumask_of(0));
	for (cpu = 1; cpu < pcpus && cpu < nr_cpu_ids; cpu++)
		set_cpu_possible(cpu, true);
	return 0;
}
early_param("possible_cpus", setup_possible_cpus);

#ifdef CONFIG_HOTPLUG_CPU

int __cpu_disable(void)
{
	struct ec_creg_mask_parms cr_parms;
	int cpu = smp_processor_id();

	cpu_clear(cpu, cpu_online_map);

	/* Disable pfault pseudo page faults on this cpu. */
	pfault_fini();

	memset(&cr_parms.orvals, 0, sizeof(cr_parms.orvals));
	memset(&cr_parms.andvals, 0xff, sizeof(cr_parms.andvals));

	/* disable all external interrupts */
	cr_parms.orvals[0] = 0;
	cr_parms.andvals[0] = ~(1 << 15 | 1 << 14 | 1 << 13 | 1 << 12 |
				1 << 11 | 1 << 10 | 1 <<  6 | 1 <<  4);
	/* disable all I/O interrupts */
	cr_parms.orvals[6] = 0;
	cr_parms.andvals[6] = ~(1 << 31 | 1 << 30 | 1 << 29 | 1 << 28 |
				1 << 27 | 1 << 26 | 1 << 25 | 1 << 24);
	/* disable most machine checks */
	cr_parms.orvals[14] = 0;
	cr_parms.andvals[14] = ~(1 << 28 | 1 << 27 | 1 << 26 |
				 1 << 25 | 1 << 24);

	smp_ctl_bit_callback(&cr_parms);

	return 0;
}

void __cpu_die(unsigned int cpu)
{
	/* Wait until target cpu is down */
	while (!cpu_stopped(cpu))
		cpu_relax();
	while (sigp_p(0, cpu, sigp_set_prefix) == sigp_busy)
		udelay(10);
	smp_free_lowcore(cpu);
	atomic_dec(&init_mm.context.attach_count);
}

void cpu_die(void)
{
	idle_task_exit();
	while (sigp(smp_processor_id(), sigp_stop) == sigp_busy)
		cpu_relax();
	for (;;);
}

#endif /* CONFIG_HOTPLUG_CPU */

void __init smp_prepare_cpus(unsigned int max_cpus)
{
#ifndef CONFIG_64BIT
	unsigned long save_area = 0;
#endif
	unsigned long async_stack, panic_stack;
	struct _lowcore *lowcore;

	smp_detect_cpus();

	/* request the 0x1201 emergency signal external interrupt */
	if (register_external_interrupt(0x1201, do_ext_call_interrupt) != 0)
		panic("Couldn't request external interrupt 0x1201");

	/* Reallocate current lowcore, but keep its contents. */
	lowcore = (void *) __get_free_pages(GFP_KERNEL | GFP_DMA, LC_ORDER);
	panic_stack = __get_free_page(GFP_KERNEL);
	async_stack = __get_free_pages(GFP_KERNEL, ASYNC_ORDER);
	BUG_ON(!lowcore || !panic_stack || !async_stack);
#ifndef CONFIG_64BIT
	if (MACHINE_HAS_IEEE)
		save_area = get_zeroed_page(GFP_KERNEL);
#endif
	local_irq_disable();
	local_mcck_disable();
	lowcore_ptr[smp_processor_id()] = lowcore;
	*lowcore = S390_lowcore;
	lowcore->panic_stack = panic_stack + PAGE_SIZE;
	lowcore->async_stack = async_stack + ASYNC_SIZE;
#ifndef CONFIG_64BIT
	if (MACHINE_HAS_IEEE)
		lowcore->extended_save_area_addr = (u32) save_area;
#endif
	set_prefix((u32)(unsigned long) lowcore);
	local_mcck_enable();
	local_irq_enable();
#ifdef CONFIG_64BIT
	if (vdso_alloc_per_cpu(smp_processor_id(), &S390_lowcore))
		BUG();
#endif
}

void __init smp_prepare_boot_cpu(void)
{
	BUG_ON(smp_processor_id() != 0);

	current_thread_info()->cpu = 0;
	cpu_set(0, cpu_present_map);
	cpu_set(0, cpu_online_map);
	S390_lowcore.percpu_offset = __per_cpu_offset[0];
	current_set[0] = current;
	smp_cpu_state[0] = CPU_STATE_CONFIGURED;
	smp_cpu_polarization[0] = POLARIZATION_UNKNWN;
}

void __init smp_cpus_done(unsigned int max_cpus)
{
}

void __init smp_setup_processor_id(void)
{
	S390_lowcore.cpu_nr = 0;
	__cpu_logical_map[0] = stap();
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
static ssize_t cpu_configure_show(struct sys_device *dev,
				struct sysdev_attribute *attr, char *buf)
{
	ssize_t count;

	mutex_lock(&smp_cpu_state_mutex);
	count = sprintf(buf, "%d\n", smp_cpu_state[dev->id]);
	mutex_unlock(&smp_cpu_state_mutex);
	return count;
}

static ssize_t cpu_configure_store(struct sys_device *dev,
				  struct sysdev_attribute *attr,
				  const char *buf, size_t count)
{
	int cpu = dev->id;
	int val, rc;
	char delim;

	if (sscanf(buf, "%d %c", &val, &delim) != 1)
		return -EINVAL;
	if (val != 0 && val != 1)
		return -EINVAL;

	get_online_cpus();
	mutex_lock(&smp_cpu_state_mutex);
	rc = -EBUSY;
	/* disallow configuration changes of online cpus and cpu 0 */
	if (cpu_online(cpu) || cpu == 0)
		goto out;
	rc = 0;
	switch (val) {
	case 0:
		if (smp_cpu_state[cpu] == CPU_STATE_CONFIGURED) {
			rc = sclp_cpu_deconfigure(__cpu_logical_map[cpu]);
			if (!rc) {
				smp_cpu_state[cpu] = CPU_STATE_STANDBY;
				smp_cpu_polarization[cpu] = POLARIZATION_UNKNWN;
			}
		}
		break;
	case 1:
		if (smp_cpu_state[cpu] == CPU_STATE_STANDBY) {
			rc = sclp_cpu_configure(__cpu_logical_map[cpu]);
			if (!rc) {
				smp_cpu_state[cpu] = CPU_STATE_CONFIGURED;
				smp_cpu_polarization[cpu] = POLARIZATION_UNKNWN;
			}
		}
		break;
	default:
		break;
	}
out:
	mutex_unlock(&smp_cpu_state_mutex);
	put_online_cpus();
	return rc ? rc : count;
}
static SYSDEV_ATTR(configure, 0644, cpu_configure_show, cpu_configure_store);
#endif /* CONFIG_HOTPLUG_CPU */

static ssize_t cpu_polarization_show(struct sys_device *dev,
				     struct sysdev_attribute *attr, char *buf)
{
	int cpu = dev->id;
	ssize_t count;

	mutex_lock(&smp_cpu_state_mutex);
	switch (smp_cpu_polarization[cpu]) {
	case POLARIZATION_HRZ:
		count = sprintf(buf, "horizontal\n");
		break;
	case POLARIZATION_VL:
		count = sprintf(buf, "vertical:low\n");
		break;
	case POLARIZATION_VM:
		count = sprintf(buf, "vertical:medium\n");
		break;
	case POLARIZATION_VH:
		count = sprintf(buf, "vertical:high\n");
		break;
	default:
		count = sprintf(buf, "unknown\n");
		break;
	}
	mutex_unlock(&smp_cpu_state_mutex);
	return count;
}
static SYSDEV_ATTR(polarization, 0444, cpu_polarization_show, NULL);

static ssize_t show_cpu_address(struct sys_device *dev,
				struct sysdev_attribute *attr, char *buf)
{
	return sprintf(buf, "%d\n", __cpu_logical_map[dev->id]);
}
static SYSDEV_ATTR(address, 0444, show_cpu_address, NULL);


static struct attribute *cpu_common_attrs[] = {
#ifdef CONFIG_HOTPLUG_CPU
	&attr_configure.attr,
#endif
	&attr_address.attr,
	&attr_polarization.attr,
	NULL,
};

static struct attribute_group cpu_common_attr_group = {
	.attrs = cpu_common_attrs,
};

static ssize_t show_capability(struct sys_device *dev,
				struct sysdev_attribute *attr, char *buf)
{
	unsigned int capability;
	int rc;

	rc = get_cpu_capability(&capability);
	if (rc)
		return rc;
	return sprintf(buf, "%u\n", capability);
}
static SYSDEV_ATTR(capability, 0444, show_capability, NULL);

static ssize_t show_idle_count(struct sys_device *dev,
				struct sysdev_attribute *attr, char *buf)
{
	struct s390_idle_data *idle;
	unsigned long long idle_count;
	unsigned int sequence;

	idle = &per_cpu(s390_idle, dev->id);
repeat:
	sequence = idle->sequence;
	smp_rmb();
	if (sequence & 1)
		goto repeat;
	idle_count = idle->idle_count;
	if (idle->idle_enter)
		idle_count++;
	smp_rmb();
	if (idle->sequence != sequence)
		goto repeat;
	return sprintf(buf, "%llu\n", idle_count);
}
static SYSDEV_ATTR(idle_count, 0444, show_idle_count, NULL);

static ssize_t show_idle_time(struct sys_device *dev,
				struct sysdev_attribute *attr, char *buf)
{
	struct s390_idle_data *idle;
	unsigned long long now, idle_time, idle_enter;
	unsigned int sequence;

	idle = &per_cpu(s390_idle, dev->id);
	now = get_clock();
repeat:
	sequence = idle->sequence;
	smp_rmb();
	if (sequence & 1)
		goto repeat;
	idle_time = idle->idle_time;
	idle_enter = idle->idle_enter;
	if (idle_enter != 0ULL && idle_enter < now)
		idle_time += now - idle_enter;
	smp_rmb();
	if (idle->sequence != sequence)
		goto repeat;
	return sprintf(buf, "%llu\n", idle_time >> 12);
}
static SYSDEV_ATTR(idle_time_us, 0444, show_idle_time, NULL);

static struct attribute *cpu_online_attrs[] = {
	&attr_capability.attr,
	&attr_idle_count.attr,
	&attr_idle_time_us.attr,
	NULL,
};

static struct attribute_group cpu_online_attr_group = {
	.attrs = cpu_online_attrs,
};

static int __cpuinit smp_cpu_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned int)(long)hcpu;
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	struct sys_device *s = &c->sysdev;
	struct s390_idle_data *idle;
	int err = 0;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		idle = &per_cpu(s390_idle, cpu);
		memset(idle, 0, sizeof(struct s390_idle_data));
		err = sysfs_create_group(&s->kobj, &cpu_online_attr_group);
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		sysfs_remove_group(&s->kobj, &cpu_online_attr_group);
		break;
	}
	return notifier_from_errno(err);
}

static struct notifier_block __cpuinitdata smp_cpu_nb = {
	.notifier_call = smp_cpu_notify,
};

static int __devinit smp_add_present_cpu(int cpu)
{
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	struct sys_device *s = &c->sysdev;
	int rc;

	c->hotpluggable = 1;
	rc = register_cpu(c, cpu);
	if (rc)
		goto out;
	rc = sysfs_create_group(&s->kobj, &cpu_common_attr_group);
	if (rc)
		goto out_cpu;
	if (!cpu_online(cpu))
		goto out;
	rc = sysfs_create_group(&s->kobj, &cpu_online_attr_group);
	if (!rc)
		return 0;
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
	cpumask_t newcpus;
	int cpu;
	int rc;

	get_online_cpus();
	mutex_lock(&smp_cpu_state_mutex);
	newcpus = cpu_present_map;
	rc = __smp_rescan_cpus();
	if (rc)
		goto out;
	cpus_andnot(newcpus, cpu_present_map, newcpus);
	for_each_cpu_mask(cpu, newcpus) {
		rc = smp_add_present_cpu(cpu);
		if (rc)
			cpu_clear(cpu, cpu_present_map);
	}
	rc = 0;
out:
	mutex_unlock(&smp_cpu_state_mutex);
	put_online_cpus();
	if (!cpus_empty(newcpus))
		topology_schedule_update();
	return rc;
}

static ssize_t __ref rescan_store(struct sysdev_class *class,
				  struct sysdev_class_attribute *attr,
				  const char *buf,
				  size_t count)
{
	int rc;

	rc = smp_rescan_cpus();
	return rc ? rc : count;
}
static SYSDEV_CLASS_ATTR(rescan, 0200, NULL, rescan_store);
#endif /* CONFIG_HOTPLUG_CPU */

static ssize_t dispatching_show(struct sysdev_class *class,
				struct sysdev_class_attribute *attr,
				char *buf)
{
	ssize_t count;

	mutex_lock(&smp_cpu_state_mutex);
	count = sprintf(buf, "%d\n", cpu_management);
	mutex_unlock(&smp_cpu_state_mutex);
	return count;
}

static ssize_t dispatching_store(struct sysdev_class *dev,
				 struct sysdev_class_attribute *attr,
				 const char *buf,
				 size_t count)
{
	int val, rc;
	char delim;

	if (sscanf(buf, "%d %c", &val, &delim) != 1)
		return -EINVAL;
	if (val != 0 && val != 1)
		return -EINVAL;
	rc = 0;
	get_online_cpus();
	mutex_lock(&smp_cpu_state_mutex);
	if (cpu_management == val)
		goto out;
	rc = topology_set_cpu_management(val);
	if (!rc)
		cpu_management = val;
out:
	mutex_unlock(&smp_cpu_state_mutex);
	put_online_cpus();
	return rc ? rc : count;
}
static SYSDEV_CLASS_ATTR(dispatching, 0644, dispatching_show,
			 dispatching_store);

static int __init topology_init(void)
{
	int cpu;
	int rc;

	register_cpu_notifier(&smp_cpu_nb);

#ifdef CONFIG_HOTPLUG_CPU
	rc = sysdev_class_create_file(&cpu_sysdev_class, &attr_rescan);
	if (rc)
		return rc;
#endif
	rc = sysdev_class_create_file(&cpu_sysdev_class, &attr_dispatching);
	if (rc)
		return rc;
	for_each_present_cpu(cpu) {
		rc = smp_add_present_cpu(cpu);
		if (rc)
			return rc;
	}
	return 0;
}
subsys_initcall(topology_init);
