/*
 *  arch/s390/kernel/smp.c
 *
 *    Copyright IBM Corp. 1999,2007
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

#include <linux/module.h>
#include <linux/init.h>
#include <linux/mm.h>
#include <linux/spinlock.h>
#include <linux/kernel_stat.h>
#include <linux/delay.h>
#include <linux/cache.h>
#include <linux/interrupt.h>
#include <linux/cpu.h>
#include <linux/timex.h>
#include <linux/bootmem.h>
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

/*
 * An array with a pointer the lowcore of every CPU.
 */
struct _lowcore *lowcore_ptr[NR_CPUS];
EXPORT_SYMBOL(lowcore_ptr);

cpumask_t cpu_online_map = CPU_MASK_NONE;
EXPORT_SYMBOL(cpu_online_map);

cpumask_t cpu_possible_map = CPU_MASK_NONE;
EXPORT_SYMBOL(cpu_possible_map);

static struct task_struct *current_set[NR_CPUS];

static void smp_ext_bitcall(int, ec_bit_sig);

/*
 * Structure and data for __smp_call_function_map(). This is designed to
 * minimise static memory requirements. It also looks cleaner.
 */
static DEFINE_SPINLOCK(call_lock);

struct call_data_struct {
	void (*func) (void *info);
	void *info;
	cpumask_t started;
	cpumask_t finished;
	int wait;
};

static struct call_data_struct *call_data;

/*
 * 'Call function' interrupt callback
 */
static void do_call_function(void)
{
	void (*func) (void *info) = call_data->func;
	void *info = call_data->info;
	int wait = call_data->wait;

	cpu_set(smp_processor_id(), call_data->started);
	(*func)(info);
	if (wait)
		cpu_set(smp_processor_id(), call_data->finished);;
}

static void __smp_call_function_map(void (*func) (void *info), void *info,
				    int nonatomic, int wait, cpumask_t map)
{
	struct call_data_struct data;
	int cpu, local = 0;

	/*
	 * Can deadlock when interrupts are disabled or if in wrong context.
	 */
	WARN_ON(irqs_disabled() || in_irq());

	/*
	 * Check for local function call. We have to have the same call order
	 * as in on_each_cpu() because of machine_restart_smp().
	 */
	if (cpu_isset(smp_processor_id(), map)) {
		local = 1;
		cpu_clear(smp_processor_id(), map);
	}

	cpus_and(map, map, cpu_online_map);
	if (cpus_empty(map))
		goto out;

	data.func = func;
	data.info = info;
	data.started = CPU_MASK_NONE;
	data.wait = wait;
	if (wait)
		data.finished = CPU_MASK_NONE;

	spin_lock_bh(&call_lock);
	call_data = &data;

	for_each_cpu_mask(cpu, map)
		smp_ext_bitcall(cpu, ec_call_function);

	/* Wait for response */
	while (!cpus_equal(map, data.started))
		cpu_relax();

	if (wait)
		while (!cpus_equal(map, data.finished))
			cpu_relax();

	spin_unlock_bh(&call_lock);

out:
	local_irq_disable();
	if (local)
		func(info);
	local_irq_enable();
}

/*
 * smp_call_function:
 * @func: the function to run; this must be fast and non-blocking
 * @info: an arbitrary pointer to pass to the function
 * @nonatomic: unused
 * @wait: if true, wait (atomically) until function has completed on other CPUs
 *
 * Run a function on all other CPUs.
 *
 * You must not call this function with disabled interrupts, from a
 * hardware interrupt handler or from a bottom half.
 */
int smp_call_function(void (*func) (void *info), void *info, int nonatomic,
		      int wait)
{
	cpumask_t map;

	preempt_disable();
	map = cpu_online_map;
	cpu_clear(smp_processor_id(), map);
	__smp_call_function_map(func, info, nonatomic, wait, map);
	preempt_enable();
	return 0;
}
EXPORT_SYMBOL(smp_call_function);

/*
 * smp_call_function_on:
 * @func: the function to run; this must be fast and non-blocking
 * @info: an arbitrary pointer to pass to the function
 * @nonatomic: unused
 * @wait: if true, wait (atomically) until function has completed on other CPUs
 * @cpu: the CPU where func should run
 *
 * Run a function on one processor.
 *
 * You must not call this function with disabled interrupts, from a
 * hardware interrupt handler or from a bottom half.
 */
int smp_call_function_on(void (*func) (void *info), void *info, int nonatomic,
			 int wait, int cpu)
{
	cpumask_t map = CPU_MASK_NONE;

	preempt_disable();
	cpu_set(cpu, map);
	__smp_call_function_map(func, info, nonatomic, wait, map);
	preempt_enable();
	return 0;
}
EXPORT_SYMBOL(smp_call_function_on);

static void do_send_stop(void)
{
	int cpu, rc;

	/* stop all processors */
	for_each_online_cpu(cpu) {
		if (cpu == smp_processor_id())
			continue;
		do {
			rc = signal_processor(cpu, sigp_stop);
		} while (rc == sigp_busy);
	}
}

static void do_store_status(void)
{
	int cpu, rc;

	/* store status of all processors in their lowcores (real 0) */
	for_each_online_cpu(cpu) {
		if (cpu == smp_processor_id())
			continue;
		do {
			rc = signal_processor_p(
				(__u32)(unsigned long) lowcore_ptr[cpu], cpu,
				sigp_store_status_at_address);
		} while (rc == sigp_busy);
	}
}

static void do_wait_for_stop(void)
{
	int cpu;

	/* Wait for all other cpus to enter stopped state */
	for_each_online_cpu(cpu) {
		if (cpu == smp_processor_id())
			continue;
		while (!smp_cpu_not_running(cpu))
			cpu_relax();
	}
}

/*
 * this function sends a 'stop' sigp to all other CPUs in the system.
 * it goes straight through.
 */
void smp_send_stop(void)
{
	/* Disable all interrupts/machine checks */
	__load_psw_mask(psw_kernel_bits & ~PSW_MASK_MCHECK);

	/* write magic number to zero page (absolute 0) */
	lowcore_ptr[smp_processor_id()]->panic_magic = __PANIC_MAGIC;

	/* stop other processors. */
	do_send_stop();

	/* wait until other processors are stopped */
	do_wait_for_stop();

	/* store status of other processors. */
	do_store_status();
}

/*
 * Reboot, halt and power_off routines for SMP.
 */
void machine_restart_smp(char *__unused)
{
	smp_send_stop();
	do_reipl();
}

void machine_halt_smp(void)
{
	smp_send_stop();
	if (MACHINE_IS_VM && strlen(vmhalt_cmd) > 0)
		__cpcmd(vmhalt_cmd, NULL, 0, NULL);
	signal_processor(smp_processor_id(), sigp_stop_and_store_status);
	for (;;);
}

void machine_power_off_smp(void)
{
	smp_send_stop();
	if (MACHINE_IS_VM && strlen(vmpoff_cmd) > 0)
		__cpcmd(vmpoff_cmd, NULL, 0, NULL);
	signal_processor(smp_processor_id(), sigp_stop_and_store_status);
	for (;;);
}

/*
 * This is the main routine where commands issued by other
 * cpus are handled.
 */

static void do_ext_call_interrupt(__u16 code)
{
	unsigned long bits;

	/*
	 * handle bit signal external calls
	 *
	 * For the ec_schedule signal we have to do nothing. All the work
	 * is done automatically when we return from the interrupt.
	 */
	bits = xchg(&S390_lowcore.ext_call_fast, 0);

	if (test_bit(ec_call_function, &bits))
		do_call_function();
}

/*
 * Send an external call sigp to another cpu and return without waiting
 * for its completion.
 */
static void smp_ext_bitcall(int cpu, ec_bit_sig sig)
{
	/*
	 * Set signaling bit in lowcore of target cpu and kick it
	 */
	set_bit(sig, (unsigned long *) &lowcore_ptr[cpu]->ext_call_fast);
	while (signal_processor(cpu, sigp_emergency_signal) == sigp_busy)
		udelay(10);
}

#ifndef CONFIG_64BIT
/*
 * this function sends a 'purge tlb' signal to another CPU.
 */
void smp_ptlb_callback(void *info)
{
	local_flush_tlb();
}

void smp_ptlb_all(void)
{
	on_each_cpu(smp_ptlb_callback, NULL, 0, 1);
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
	on_each_cpu(smp_ctl_bit_callback, &parms, 0, 1);
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
	on_each_cpu(smp_ctl_bit_callback, &parms, 0, 1);
}
EXPORT_SYMBOL(smp_ctl_clear_bit);

#if defined(CONFIG_ZFCPDUMP) || defined(CONFIG_ZFCPDUMP_MODULE)

/*
 * zfcpdump_prefix_array holds prefix registers for the following scenario:
 * 64 bit zfcpdump kernel and 31 bit kernel which is to be dumped. We have to
 * save its prefix registers, since they get lost, when switching from 31 bit
 * to 64 bit.
 */
unsigned int zfcpdump_prefix_array[NR_CPUS + 1] \
	__attribute__((__section__(".data")));

static void __init smp_get_save_areas(void)
{
	unsigned int cpu, cpu_num, rc;
	__u16 boot_cpu_addr;

	if (ipl_info.type != IPL_TYPE_FCP_DUMP)
		return;
	boot_cpu_addr = S390_lowcore.cpu_data.cpu_addr;
	cpu_num = 1;
	for (cpu = 0; cpu <= 65535; cpu++) {
		if ((u16) cpu == boot_cpu_addr)
			continue;
		__cpu_logical_map[1] = (__u16) cpu;
		if (signal_processor(1, sigp_sense) == sigp_not_operational)
			continue;
		if (cpu_num >= NR_CPUS) {
			printk("WARNING: Registers for cpu %i are not "
			       "saved, since dump kernel was compiled with"
			       "NR_CPUS=%i!\n", cpu_num, NR_CPUS);
			continue;
		}
		zfcpdump_save_areas[cpu_num] =
			alloc_bootmem(sizeof(union save_area));
		while (1) {
			rc = signal_processor(1, sigp_stop_and_store_status);
			if (rc != sigp_busy)
				break;
			cpu_relax();
		}
		memcpy(zfcpdump_save_areas[cpu_num],
		       (void *)(unsigned long) store_prefix() +
		       SAVE_AREA_BASE, SAVE_AREA_SIZE);
#ifdef __s390x__
		/* copy original prefix register */
		zfcpdump_save_areas[cpu_num]->s390x.pref_reg =
			zfcpdump_prefix_array[cpu_num];
#endif
		cpu_num++;
	}
}

union save_area *zfcpdump_save_areas[NR_CPUS + 1];
EXPORT_SYMBOL_GPL(zfcpdump_save_areas);

#else
#define smp_get_save_areas() do { } while (0)
#endif

/*
 * Lets check how many CPUs we have.
 */

static unsigned int __init smp_count_cpus(void)
{
	unsigned int cpu, num_cpus;
	__u16 boot_cpu_addr;

	/*
	 * cpu 0 is the boot cpu. See smp_prepare_boot_cpu.
	 */

	boot_cpu_addr = S390_lowcore.cpu_data.cpu_addr;
	current_thread_info()->cpu = 0;
	num_cpus = 1;
	for (cpu = 0; cpu <= 65535; cpu++) {
		if ((__u16) cpu == boot_cpu_addr)
			continue;
		__cpu_logical_map[1] = (__u16) cpu;
		if (signal_processor(1, sigp_sense) == sigp_not_operational)
			continue;
		num_cpus++;
	}

	printk("Detected %d CPU's\n", (int) num_cpus);
	printk("Boot cpu address %2X\n", boot_cpu_addr);

	return num_cpus;
}

/*
 *	Activate a secondary processor.
 */
int __devinit start_secondary(void *cpuvoid)
{
	/* Setup the cpu */
	cpu_init();
	preempt_disable();
	/* Enable TOD clock interrupts on the secondary cpu. */
	init_cpu_timer();
#ifdef CONFIG_VIRT_TIMER
	/* Enable cpu timer interrupts on the secondary cpu. */
	init_cpu_vtimer();
#endif
	/* Enable pfault pseudo page faults on this cpu. */
	pfault_init();

	/* Mark this cpu as online */
	cpu_set(smp_processor_id(), cpu_online_map);
	/* Switch on interrupts */
	local_irq_enable();
	/* Print info about this processor */
	print_cpu_info(&S390_lowcore.cpu_data);
	/* cpu_idle will call schedule for us */
	cpu_idle();
	return 0;
}

static void __init smp_create_idle(unsigned int cpu)
{
	struct task_struct *p;

	/*
	 *  don't care about the psw and regs settings since we'll never
	 *  reschedule the forked task.
	 */
	p = fork_idle(cpu);
	if (IS_ERR(p))
		panic("failed fork for CPU %u: %li", cpu, PTR_ERR(p));
	current_set[cpu] = p;
}

static int cpu_stopped(int cpu)
{
	__u32 status;

	/* Check for stopped state */
	if (signal_processor_ps(&status, 0, cpu, sigp_sense) ==
	    sigp_status_stored) {
		if (status & 0x40)
			return 1;
	}
	return 0;
}

/* Upping and downing of CPUs */

int __cpu_up(unsigned int cpu)
{
	struct task_struct *idle;
	struct _lowcore *cpu_lowcore;
	struct stack_frame *sf;
	sigp_ccode ccode;
	int curr_cpu;

	for (curr_cpu = 0; curr_cpu <= 65535; curr_cpu++) {
		__cpu_logical_map[cpu] = (__u16) curr_cpu;
		if (cpu_stopped(cpu))
			break;
	}

	if (!cpu_stopped(cpu))
		return -ENODEV;

	ccode = signal_processor_p((__u32)(unsigned long)(lowcore_ptr[cpu]),
				   cpu, sigp_set_prefix);
	if (ccode) {
		printk("sigp_set_prefix failed for cpu %d "
		       "with condition code %d\n",
		       (int) cpu, (int) ccode);
		return -EIO;
	}

	idle = current_set[cpu];
	cpu_lowcore = lowcore_ptr[cpu];
	cpu_lowcore->kernel_stack = (unsigned long)
		task_stack_page(idle) + THREAD_SIZE;
	sf = (struct stack_frame *) (cpu_lowcore->kernel_stack
				     - sizeof(struct pt_regs)
				     - sizeof(struct stack_frame));
	memset(sf, 0, sizeof(struct stack_frame));
	sf->gprs[9] = (unsigned long) sf;
	cpu_lowcore->save_area[15] = (unsigned long) sf;
	__ctl_store(cpu_lowcore->cregs_save_area[0], 0, 15);
	asm volatile(
		"	stam	0,15,0(%0)"
		: : "a" (&cpu_lowcore->access_regs_save_area) : "memory");
	cpu_lowcore->percpu_offset = __per_cpu_offset[cpu];
	cpu_lowcore->current_task = (unsigned long) idle;
	cpu_lowcore->cpu_data.cpu_nr = cpu;
	eieio();

	while (signal_processor(cpu, sigp_restart) == sigp_busy)
		udelay(10);

	while (!cpu_online(cpu))
		cpu_relax();
	return 0;
}

static unsigned int __initdata additional_cpus;
static unsigned int __initdata possible_cpus;

void __init smp_setup_cpu_possible_map(void)
{
	unsigned int phy_cpus, pos_cpus, cpu;

	smp_get_save_areas();
	phy_cpus = smp_count_cpus();
	pos_cpus = min(phy_cpus + additional_cpus, (unsigned int) NR_CPUS);

	if (possible_cpus)
		pos_cpus = min(possible_cpus, (unsigned int) NR_CPUS);

	for (cpu = 0; cpu < pos_cpus; cpu++)
		cpu_set(cpu, cpu_possible_map);

	phy_cpus = min(phy_cpus, pos_cpus);

	for (cpu = 0; cpu < phy_cpus; cpu++)
		cpu_set(cpu, cpu_present_map);
}

#ifdef CONFIG_HOTPLUG_CPU

static int __init setup_additional_cpus(char *s)
{
	additional_cpus = simple_strtoul(s, NULL, 0);
	return 0;
}
early_param("additional_cpus", setup_additional_cpus);

static int __init setup_possible_cpus(char *s)
{
	possible_cpus = simple_strtoul(s, NULL, 0);
	return 0;
}
early_param("possible_cpus", setup_possible_cpus);

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
	while (!smp_cpu_not_running(cpu))
		cpu_relax();
	printk("Processor %d spun down\n", cpu);
}

void cpu_die(void)
{
	idle_task_exit();
	signal_processor(smp_processor_id(), sigp_stop);
	BUG();
	for (;;);
}

#endif /* CONFIG_HOTPLUG_CPU */

/*
 *	Cycle through the processors and setup structures.
 */

void __init smp_prepare_cpus(unsigned int max_cpus)
{
	unsigned long stack;
	unsigned int cpu;
	int i;

	/* request the 0x1201 emergency signal external interrupt */
	if (register_external_interrupt(0x1201, do_ext_call_interrupt) != 0)
		panic("Couldn't request external interrupt 0x1201");
	memset(lowcore_ptr, 0, sizeof(lowcore_ptr));
	/*
	 *  Initialize prefix pages and stacks for all possible cpus
	 */
	print_cpu_info(&S390_lowcore.cpu_data);

	for_each_possible_cpu(i) {
		lowcore_ptr[i] = (struct _lowcore *)
			__get_free_pages(GFP_KERNEL | GFP_DMA,
					 sizeof(void*) == 8 ? 1 : 0);
		stack = __get_free_pages(GFP_KERNEL, ASYNC_ORDER);
		if (!lowcore_ptr[i] || !stack)
			panic("smp_boot_cpus failed to allocate memory\n");

		*(lowcore_ptr[i]) = S390_lowcore;
		lowcore_ptr[i]->async_stack = stack + ASYNC_SIZE;
		stack = __get_free_pages(GFP_KERNEL, 0);
		if (!stack)
			panic("smp_boot_cpus failed to allocate memory\n");
		lowcore_ptr[i]->panic_stack = stack + PAGE_SIZE;
#ifndef CONFIG_64BIT
		if (MACHINE_HAS_IEEE) {
			lowcore_ptr[i]->extended_save_area_addr =
				(__u32) __get_free_pages(GFP_KERNEL, 0);
			if (!lowcore_ptr[i]->extended_save_area_addr)
				panic("smp_boot_cpus failed to "
				      "allocate memory\n");
		}
#endif
	}
#ifndef CONFIG_64BIT
	if (MACHINE_HAS_IEEE)
		ctl_set_bit(14, 29); /* enable extended save area */
#endif
	set_prefix((u32)(unsigned long) lowcore_ptr[smp_processor_id()]);

	for_each_possible_cpu(cpu)
		if (cpu != smp_processor_id())
			smp_create_idle(cpu);
}

void __devinit smp_prepare_boot_cpu(void)
{
	BUG_ON(smp_processor_id() != 0);

	cpu_set(0, cpu_online_map);
	S390_lowcore.percpu_offset = __per_cpu_offset[0];
	current_set[0] = current;
}

void smp_cpus_done(unsigned int max_cpus)
{
	cpu_present_map = cpu_possible_map;
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

static DEFINE_PER_CPU(struct cpu, cpu_devices);

static ssize_t show_capability(struct sys_device *dev, char *buf)
{
	unsigned int capability;
	int rc;

	rc = get_cpu_capability(&capability);
	if (rc)
		return rc;
	return sprintf(buf, "%u\n", capability);
}
static SYSDEV_ATTR(capability, 0444, show_capability, NULL);

static int __cpuinit smp_cpu_notify(struct notifier_block *self,
				    unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned int)(long)hcpu;
	struct cpu *c = &per_cpu(cpu_devices, cpu);
	struct sys_device *s = &c->sysdev;

	switch (action) {
	case CPU_ONLINE:
	case CPU_ONLINE_FROZEN:
		if (sysdev_create_file(s, &attr_capability))
			return NOTIFY_BAD;
		break;
	case CPU_DEAD:
	case CPU_DEAD_FROZEN:
		sysdev_remove_file(s, &attr_capability);
		break;
	}
	return NOTIFY_OK;
}

static struct notifier_block __cpuinitdata smp_cpu_nb = {
	.notifier_call = smp_cpu_notify,
};

static int __init topology_init(void)
{
	int cpu;

	register_cpu_notifier(&smp_cpu_nb);

	for_each_possible_cpu(cpu) {
		struct cpu *c = &per_cpu(cpu_devices, cpu);
		struct sys_device *s = &c->sysdev;

		c->hotpluggable = 1;
		register_cpu(c, cpu);
		if (!cpu_online(cpu))
			continue;
		s = &c->sysdev;
		sysdev_create_file(s, &attr_capability);
	}
	return 0;
}
subsys_initcall(topology_init);
