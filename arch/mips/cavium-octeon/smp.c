/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2008, 2009, 2010 Cavium Networks
 */
#include <linux/cpu.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/module.h>

#include <asm/mmu_context.h>
#include <asm/time.h>
#include <asm/setup.h>

#include <asm/octeon/octeon.h>

#include "octeon_boot.h"

volatile unsigned long octeon_processor_boot = 0xff;
volatile unsigned long octeon_processor_sp;
volatile unsigned long octeon_processor_gp;

#ifdef CONFIG_HOTPLUG_CPU
uint64_t octeon_bootloader_entry_addr;
EXPORT_SYMBOL(octeon_bootloader_entry_addr);
#endif

static void octeon_icache_flush(void)
{
	asm volatile ("synci 0($0)\n");
}

static void (*octeon_message_functions[8])(void) = {
	scheduler_ipi,
	generic_smp_call_function_interrupt,
	octeon_icache_flush,
};

static irqreturn_t mailbox_interrupt(int irq, void *dev_id)
{
	u64 mbox_clrx = CVMX_CIU_MBOX_CLRX(cvmx_get_core_num());
	u64 action;
	int i;

	/*
	 * Make sure the function array initialization remains
	 * correct.
	 */
	BUILD_BUG_ON(SMP_RESCHEDULE_YOURSELF != (1 << 0));
	BUILD_BUG_ON(SMP_CALL_FUNCTION       != (1 << 1));
	BUILD_BUG_ON(SMP_ICACHE_FLUSH        != (1 << 2));

	/*
	 * Load the mailbox register to figure out what we're supposed
	 * to do.
	 */
	action = cvmx_read_csr(mbox_clrx);

	if (OCTEON_IS_MODEL(OCTEON_CN68XX))
		action &= 0xff;
	else
		action &= 0xffff;

	/* Clear the mailbox to clear the interrupt */
	cvmx_write_csr(mbox_clrx, action);

	for (i = 0; i < ARRAY_SIZE(octeon_message_functions) && action;) {
		if (action & 1) {
			void (*fn)(void) = octeon_message_functions[i];

			if (fn)
				fn();
		}
		action >>= 1;
		i++;
	}
	return IRQ_HANDLED;
}

/**
 * Cause the function described by call_data to be executed on the passed
 * cpu.	 When the function has finished, increment the finished field of
 * call_data.
 */
void octeon_send_ipi_single(int cpu, unsigned int action)
{
	int coreid = cpu_logical_map(cpu);
	/*
	pr_info("SMP: Mailbox send cpu=%d, coreid=%d, action=%u\n", cpu,
	       coreid, action);
	*/
	cvmx_write_csr(CVMX_CIU_MBOX_SETX(coreid), action);
}

static inline void octeon_send_ipi_mask(const struct cpumask *mask,
					unsigned int action)
{
	unsigned int i;

	for_each_cpu(i, mask)
		octeon_send_ipi_single(i, action);
}

/**
 * Detect available CPUs, populate cpu_possible_mask
 */
static void octeon_smp_hotplug_setup(void)
{
#ifdef CONFIG_HOTPLUG_CPU
	struct linux_app_boot_info *labi;

	if (!setup_max_cpus)
		return;

	labi = (struct linux_app_boot_info *)PHYS_TO_XKSEG_CACHED(LABI_ADDR_IN_BOOTLOADER);
	if (labi->labi_signature != LABI_SIGNATURE) {
		pr_info("The bootloader on this board does not support HOTPLUG_CPU.");
		return;
	}

	octeon_bootloader_entry_addr = labi->InitTLBStart_addr;
#endif
}

static void __init octeon_smp_setup(void)
{
	const int coreid = cvmx_get_core_num();
	int cpus;
	int id;
	struct cvmx_sysinfo *sysinfo = cvmx_sysinfo_get();

#ifdef CONFIG_HOTPLUG_CPU
	int core_mask = octeon_get_boot_coremask();
	unsigned int num_cores = cvmx_octeon_num_cores();
#endif

	/* The present CPUs are initially just the boot cpu (CPU 0). */
	for (id = 0; id < NR_CPUS; id++) {
		set_cpu_possible(id, id == 0);
		set_cpu_present(id, id == 0);
	}

	__cpu_number_map[coreid] = 0;
	__cpu_logical_map[0] = coreid;

	/* The present CPUs get the lowest CPU numbers. */
	cpus = 1;
	for (id = 0; id < NR_CPUS; id++) {
		if ((id != coreid) && cvmx_coremask_is_core_set(&sysinfo->core_mask, id)) {
			set_cpu_possible(cpus, true);
			set_cpu_present(cpus, true);
			__cpu_number_map[id] = cpus;
			__cpu_logical_map[cpus] = id;
			cpus++;
		}
	}

#ifdef CONFIG_HOTPLUG_CPU
	/*
	 * The possible CPUs are all those present on the chip.	 We
	 * will assign CPU numbers for possible cores as well.	Cores
	 * are always consecutively numberd from 0.
	 */
	for (id = 0; setup_max_cpus && octeon_bootloader_entry_addr &&
		     id < num_cores && id < NR_CPUS; id++) {
		if (!(core_mask & (1 << id))) {
			set_cpu_possible(cpus, true);
			__cpu_number_map[id] = cpus;
			__cpu_logical_map[cpus] = id;
			cpus++;
		}
	}
#endif

	octeon_smp_hotplug_setup();
}

/**
 * Firmware CPU startup hook
 *
 */
static void octeon_boot_secondary(int cpu, struct task_struct *idle)
{
	int count;

	pr_info("SMP: Booting CPU%02d (CoreId %2d)...\n", cpu,
		cpu_logical_map(cpu));

	octeon_processor_sp = __KSTK_TOS(idle);
	octeon_processor_gp = (unsigned long)(task_thread_info(idle));
	octeon_processor_boot = cpu_logical_map(cpu);
	mb();

	count = 10000;
	while (octeon_processor_sp && count) {
		/* Waiting for processor to get the SP and GP */
		udelay(1);
		count--;
	}
	if (count == 0)
		pr_err("Secondary boot timeout\n");
}

/**
 * After we've done initial boot, this function is called to allow the
 * board code to clean up state, if needed
 */
static void octeon_init_secondary(void)
{
	unsigned int sr;

	sr = set_c0_status(ST0_BEV);
	write_c0_ebase((u32)ebase);
	write_c0_status(sr);

	octeon_check_cpu_bist();
	octeon_init_cvmcount();

	octeon_irq_setup_secondary();
}

/**
 * Callout to firmware before smp_init
 *
 */
static void __init octeon_prepare_cpus(unsigned int max_cpus)
{
	/*
	 * Only the low order mailbox bits are used for IPIs, leave
	 * the other bits alone.
	 */
	cvmx_write_csr(CVMX_CIU_MBOX_CLRX(cvmx_get_core_num()), 0xffff);
	if (request_irq(OCTEON_IRQ_MBOX0, mailbox_interrupt,
			IRQF_PERCPU | IRQF_NO_THREAD, "SMP-IPI",
			mailbox_interrupt)) {
		panic("Cannot request_irq(OCTEON_IRQ_MBOX0)");
	}
}

/**
 * Last chance for the board code to finish SMP initialization before
 * the CPU is "online".
 */
static void octeon_smp_finish(void)
{
	octeon_user_io_init();

	/* to generate the first CPU timer interrupt */
	write_c0_compare(read_c0_count() + mips_hpt_frequency / HZ);
	local_irq_enable();
}

#ifdef CONFIG_HOTPLUG_CPU

/* State of each CPU. */
DEFINE_PER_CPU(int, cpu_state);

static int octeon_cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();

	if (cpu == 0)
		return -EBUSY;

	if (!octeon_bootloader_entry_addr)
		return -ENOTSUPP;

	set_cpu_online(cpu, false);
	calculate_cpu_foreign_map();
	cpumask_clear_cpu(cpu, &cpu_callin_map);
	octeon_fixup_irqs();

	__flush_cache_all();
	local_flush_tlb_all();

	return 0;
}

static void octeon_cpu_die(unsigned int cpu)
{
	int coreid = cpu_logical_map(cpu);
	uint32_t mask, new_mask;
	const struct cvmx_bootmem_named_block_desc *block_desc;

	while (per_cpu(cpu_state, cpu) != CPU_DEAD)
		cpu_relax();

	/*
	 * This is a bit complicated strategics of getting/settig available
	 * cores mask, copied from bootloader
	 */

	mask = 1 << coreid;
	/* LINUX_APP_BOOT_BLOCK is initialized in bootoct binary */
	block_desc = cvmx_bootmem_find_named_block(LINUX_APP_BOOT_BLOCK_NAME);

	if (!block_desc) {
		struct linux_app_boot_info *labi;

		labi = (struct linux_app_boot_info *)PHYS_TO_XKSEG_CACHED(LABI_ADDR_IN_BOOTLOADER);

		labi->avail_coremask |= mask;
		new_mask = labi->avail_coremask;
	} else {		       /* alternative, already initialized */
		uint32_t *p = (uint32_t *)PHYS_TO_XKSEG_CACHED(block_desc->base_addr +
							       AVAIL_COREMASK_OFFSET_IN_LINUX_APP_BOOT_BLOCK);
		*p |= mask;
		new_mask = *p;
	}

	pr_info("Reset core %d. Available Coremask = 0x%x \n", coreid, new_mask);
	mb();
	cvmx_write_csr(CVMX_CIU_PP_RST, 1 << coreid);
	cvmx_write_csr(CVMX_CIU_PP_RST, 0);
}

void play_dead(void)
{
	int cpu = cpu_number_map(cvmx_get_core_num());

	idle_task_exit();
	octeon_processor_boot = 0xff;
	per_cpu(cpu_state, cpu) = CPU_DEAD;

	mb();

	while (1)	/* core will be reset here */
		;
}

extern void kernel_entry(unsigned long arg1, ...);

static void start_after_reset(void)
{
	kernel_entry(0, 0, 0);	/* set a2 = 0 for secondary core */
}

static int octeon_update_boot_vector(unsigned int cpu)
{

	int coreid = cpu_logical_map(cpu);
	uint32_t avail_coremask;
	const struct cvmx_bootmem_named_block_desc *block_desc;
	struct boot_init_vector *boot_vect =
		(struct boot_init_vector *)PHYS_TO_XKSEG_CACHED(BOOTLOADER_BOOT_VECTOR);

	block_desc = cvmx_bootmem_find_named_block(LINUX_APP_BOOT_BLOCK_NAME);

	if (!block_desc) {
		struct linux_app_boot_info *labi;

		labi = (struct linux_app_boot_info *)PHYS_TO_XKSEG_CACHED(LABI_ADDR_IN_BOOTLOADER);

		avail_coremask = labi->avail_coremask;
		labi->avail_coremask &= ~(1 << coreid);
	} else {		       /* alternative, already initialized */
		avail_coremask = *(uint32_t *)PHYS_TO_XKSEG_CACHED(
			block_desc->base_addr + AVAIL_COREMASK_OFFSET_IN_LINUX_APP_BOOT_BLOCK);
	}

	if (!(avail_coremask & (1 << coreid))) {
		/* core not available, assume, that caught by simple-executive */
		cvmx_write_csr(CVMX_CIU_PP_RST, 1 << coreid);
		cvmx_write_csr(CVMX_CIU_PP_RST, 0);
	}

	boot_vect[coreid].app_start_func_addr =
		(uint32_t) (unsigned long) start_after_reset;
	boot_vect[coreid].code_addr = octeon_bootloader_entry_addr;

	mb();

	cvmx_write_csr(CVMX_CIU_NMI, (1 << coreid) & avail_coremask);

	return 0;
}

static int octeon_cpu_callback(struct notifier_block *nfb,
	unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action & ~CPU_TASKS_FROZEN) {
	case CPU_UP_PREPARE:
		octeon_update_boot_vector(cpu);
		break;
	case CPU_ONLINE:
		pr_info("Cpu %d online\n", cpu);
		break;
	case CPU_DEAD:
		break;
	}

	return NOTIFY_OK;
}

static int register_cavium_notifier(void)
{
	hotcpu_notifier(octeon_cpu_callback, 0);
	return 0;
}
late_initcall(register_cavium_notifier);

#endif	/* CONFIG_HOTPLUG_CPU */

struct plat_smp_ops octeon_smp_ops = {
	.send_ipi_single	= octeon_send_ipi_single,
	.send_ipi_mask		= octeon_send_ipi_mask,
	.init_secondary		= octeon_init_secondary,
	.smp_finish		= octeon_smp_finish,
	.boot_secondary		= octeon_boot_secondary,
	.smp_setup		= octeon_smp_setup,
	.prepare_cpus		= octeon_prepare_cpus,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable		= octeon_cpu_disable,
	.cpu_die		= octeon_cpu_die,
#endif
};

static irqreturn_t octeon_78xx_reched_interrupt(int irq, void *dev_id)
{
	scheduler_ipi();
	return IRQ_HANDLED;
}

static irqreturn_t octeon_78xx_call_function_interrupt(int irq, void *dev_id)
{
	generic_smp_call_function_interrupt();
	return IRQ_HANDLED;
}

static irqreturn_t octeon_78xx_icache_flush_interrupt(int irq, void *dev_id)
{
	octeon_icache_flush();
	return IRQ_HANDLED;
}

/*
 * Callout to firmware before smp_init
 */
static void octeon_78xx_prepare_cpus(unsigned int max_cpus)
{
	if (request_irq(OCTEON_IRQ_MBOX0 + 0,
			octeon_78xx_reched_interrupt,
			IRQF_PERCPU | IRQF_NO_THREAD, "Scheduler",
			octeon_78xx_reched_interrupt)) {
		panic("Cannot request_irq for SchedulerIPI");
	}
	if (request_irq(OCTEON_IRQ_MBOX0 + 1,
			octeon_78xx_call_function_interrupt,
			IRQF_PERCPU | IRQF_NO_THREAD, "SMP-Call",
			octeon_78xx_call_function_interrupt)) {
		panic("Cannot request_irq for SMP-Call");
	}
	if (request_irq(OCTEON_IRQ_MBOX0 + 2,
			octeon_78xx_icache_flush_interrupt,
			IRQF_PERCPU | IRQF_NO_THREAD, "ICache-Flush",
			octeon_78xx_icache_flush_interrupt)) {
		panic("Cannot request_irq for ICache-Flush");
	}
}

static void octeon_78xx_send_ipi_single(int cpu, unsigned int action)
{
	int i;

	for (i = 0; i < 8; i++) {
		if (action & 1)
			octeon_ciu3_mbox_send(cpu, i);
		action >>= 1;
	}
}

static void octeon_78xx_send_ipi_mask(const struct cpumask *mask,
				      unsigned int action)
{
	unsigned int cpu;

	for_each_cpu(cpu, mask)
		octeon_78xx_send_ipi_single(cpu, action);
}

static struct plat_smp_ops octeon_78xx_smp_ops = {
	.send_ipi_single	= octeon_78xx_send_ipi_single,
	.send_ipi_mask		= octeon_78xx_send_ipi_mask,
	.init_secondary		= octeon_init_secondary,
	.smp_finish		= octeon_smp_finish,
	.boot_secondary		= octeon_boot_secondary,
	.smp_setup		= octeon_smp_setup,
	.prepare_cpus		= octeon_78xx_prepare_cpus,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable		= octeon_cpu_disable,
	.cpu_die		= octeon_cpu_die,
#endif
};

void __init octeon_setup_smp(void)
{
	struct plat_smp_ops *ops;

	if (octeon_has_feature(OCTEON_FEATURE_CIU3))
		ops = &octeon_78xx_smp_ops;
	else
		ops = &octeon_smp_ops;

	register_smp_ops(ops);
}
