/*
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file "COPYING" in the main directory of this archive
 * for more details.
 *
 * Copyright (C) 2004-2008 Cavium Networks
 */
#include <linux/cpu.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/smp.h>
#include <linux/interrupt.h>
#include <linux/kernel_stat.h>
#include <linux/sched.h>
#include <linux/module.h>

#include <asm/mmu_context.h>
#include <asm/system.h>
#include <asm/time.h>

#include <asm/octeon/octeon.h>

#include "octeon_boot.h"

volatile unsigned long octeon_processor_boot = 0xff;
volatile unsigned long octeon_processor_sp;
volatile unsigned long octeon_processor_gp;

#ifdef CONFIG_HOTPLUG_CPU
static unsigned int InitTLBStart_addr;
#endif

static irqreturn_t mailbox_interrupt(int irq, void *dev_id)
{
	const int coreid = cvmx_get_core_num();
	uint64_t action;

	/* Load the mailbox register to figure out what we're supposed to do */
	action = cvmx_read_csr(CVMX_CIU_MBOX_CLRX(coreid));

	/* Clear the mailbox to clear the interrupt */
	cvmx_write_csr(CVMX_CIU_MBOX_CLRX(coreid), action);

	if (action & SMP_CALL_FUNCTION)
		smp_call_function_interrupt();

	/* Check if we've been told to flush the icache */
	if (action & SMP_ICACHE_FLUSH)
		asm volatile ("synci 0($0)\n");
	return IRQ_HANDLED;
}

/**
 * Cause the function described by call_data to be executed on the passed
 * cpu.  When the function has finished, increment the finished field of
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

	for_each_cpu_mask(i, *mask)
		octeon_send_ipi_single(i, action);
}

/**
 * Detect available CPUs, populate cpu_possible_map
 */
static void octeon_smp_hotplug_setup(void)
{
#ifdef CONFIG_HOTPLUG_CPU
	uint32_t labi_signature;

	labi_signature =
		cvmx_read64_uint32(CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS,
					LABI_ADDR_IN_BOOTLOADER +
					offsetof(struct linux_app_boot_info,
						    labi_signature)));
	if (labi_signature != LABI_SIGNATURE)
		pr_err("The bootloader version on this board is incorrect\n");
	InitTLBStart_addr =
		cvmx_read64_uint32(CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS,
				   LABI_ADDR_IN_BOOTLOADER +
					   offsetof(struct linux_app_boot_info,
						    InitTLBStart_addr)));
#endif
}

static void octeon_smp_setup(void)
{
	const int coreid = cvmx_get_core_num();
	int cpus;
	int id;

	int core_mask = octeon_get_boot_coremask();

	cpus_clear(cpu_possible_map);
	__cpu_number_map[coreid] = 0;
	__cpu_logical_map[0] = coreid;
	cpu_set(0, cpu_possible_map);

	cpus = 1;
	for (id = 0; id < 16; id++) {
		if ((id != coreid) && (core_mask & (1 << id))) {
			cpu_set(cpus, cpu_possible_map);
			__cpu_number_map[id] = cpus;
			__cpu_logical_map[cpus] = id;
			cpus++;
		}
	}
	cpu_present_map = cpu_possible_map;

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
	const int coreid = cvmx_get_core_num();
	union cvmx_ciu_intx_sum0 interrupt_enable;

#ifdef CONFIG_HOTPLUG_CPU
	unsigned int cur_exception_base;

	cur_exception_base = cvmx_read64_uint32(
		CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS,
			     LABI_ADDR_IN_BOOTLOADER +
			     offsetof(struct linux_app_boot_info,
				      cur_exception_base)));
	/* cur_exception_base is incremented in bootloader after setting */
	write_c0_ebase((unsigned int)(cur_exception_base - EXCEPTION_BASE_INCR));
#endif
	octeon_check_cpu_bist();
	octeon_init_cvmcount();
	/*
	pr_info("SMP: CPU%d (CoreId %lu) started\n", cpu, coreid);
	*/
	/* Enable Mailbox interrupts to this core. These are the only
	   interrupts allowed on line 3 */
	cvmx_write_csr(CVMX_CIU_MBOX_CLRX(coreid), 0xffffffff);
	interrupt_enable.u64 = 0;
	interrupt_enable.s.mbox = 0x3;
	cvmx_write_csr(CVMX_CIU_INTX_EN0((coreid * 2)), interrupt_enable.u64);
	cvmx_write_csr(CVMX_CIU_INTX_EN0((coreid * 2 + 1)), 0);
	cvmx_write_csr(CVMX_CIU_INTX_EN1((coreid * 2)), 0);
	cvmx_write_csr(CVMX_CIU_INTX_EN1((coreid * 2 + 1)), 0);
	/* Enable core interrupt processing for 2,3 and 7 */
	set_c0_status(0x8c01);
}

/**
 * Callout to firmware before smp_init
 *
 */
void octeon_prepare_cpus(unsigned int max_cpus)
{
	cvmx_write_csr(CVMX_CIU_MBOX_CLRX(cvmx_get_core_num()), 0xffffffff);
	if (request_irq(OCTEON_IRQ_MBOX0, mailbox_interrupt, IRQF_DISABLED,
			"mailbox0", mailbox_interrupt)) {
		panic("Cannot request_irq(OCTEON_IRQ_MBOX0)\n");
	}
	if (request_irq(OCTEON_IRQ_MBOX1, mailbox_interrupt, IRQF_DISABLED,
			"mailbox1", mailbox_interrupt)) {
		panic("Cannot request_irq(OCTEON_IRQ_MBOX1)\n");
	}
}

/**
 * Last chance for the board code to finish SMP initialization before
 * the CPU is "online".
 */
static void octeon_smp_finish(void)
{
#ifdef CONFIG_CAVIUM_GDB
	unsigned long tmp;
	/* Pulse MCD0 signal on Ctrl-C to stop all the cores. Also set the MCD0
	   to be not masked by this core so we know the signal is received by
	   someone */
	asm volatile ("dmfc0 %0, $22\n"
		      "ori   %0, %0, 0x9100\n" "dmtc0 %0, $22\n" : "=r" (tmp));
#endif

	octeon_user_io_init();

	/* to generate the first CPU timer interrupt */
	write_c0_compare(read_c0_count() + mips_hpt_frequency / HZ);
}

/**
 * Hook for after all CPUs are online
 */
static void octeon_cpus_done(void)
{
#ifdef CONFIG_CAVIUM_GDB
	unsigned long tmp;
	/* Pulse MCD0 signal on Ctrl-C to stop all the cores. Also set the MCD0
	   to be not masked by this core so we know the signal is received by
	   someone */
	asm volatile ("dmfc0 %0, $22\n"
		      "ori   %0, %0, 0x9100\n" "dmtc0 %0, $22\n" : "=r" (tmp));
#endif
}

#ifdef CONFIG_HOTPLUG_CPU

/* State of each CPU. */
DEFINE_PER_CPU(int, cpu_state);

extern void fixup_irqs(void);

static DEFINE_SPINLOCK(smp_reserve_lock);

static int octeon_cpu_disable(void)
{
	unsigned int cpu = smp_processor_id();

	if (cpu == 0)
		return -EBUSY;

	spin_lock(&smp_reserve_lock);

	cpu_clear(cpu, cpu_online_map);
	cpu_clear(cpu, cpu_callin_map);
	local_irq_disable();
	fixup_irqs();
	local_irq_enable();

	flush_cache_all();
	local_flush_tlb_all();

	spin_unlock(&smp_reserve_lock);

	return 0;
}

static void octeon_cpu_die(unsigned int cpu)
{
	int coreid = cpu_logical_map(cpu);
	uint32_t avail_coremask;
	struct cvmx_bootmem_named_block_desc *block_desc;

#ifdef CONFIG_CAVIUM_OCTEON_WATCHDOG
	/* Disable the watchdog */
	cvmx_ciu_wdogx_t ciu_wdog;
	ciu_wdog.u64 = cvmx_read_csr(CVMX_CIU_WDOGX(cpu));
	ciu_wdog.s.mode = 0;
	cvmx_write_csr(CVMX_CIU_WDOGX(cpu), ciu_wdog.u64);
#endif

	while (per_cpu(cpu_state, cpu) != CPU_DEAD)
		cpu_relax();

	/*
	 * This is a bit complicated strategics of getting/settig available
	 * cores mask, copied from bootloader
	 */
	/* LINUX_APP_BOOT_BLOCK is initialized in bootoct binary */
	block_desc = cvmx_bootmem_find_named_block(LINUX_APP_BOOT_BLOCK_NAME);

	if (!block_desc) {
		avail_coremask =
			cvmx_read64_uint32(CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS,
						   LABI_ADDR_IN_BOOTLOADER +
						   offsetof
						   (struct linux_app_boot_info,
						    avail_coremask)));
	} else {		       /* alternative, already initialized */
	       avail_coremask =
		   cvmx_read64_uint32(CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS,
						   block_desc->base_addr +
						  AVAIL_COREMASK_OFFSET_IN_LINUX_APP_BOOT_BLOCK));
	}

	avail_coremask |= 1 << coreid;

	/* Setting avail_coremask for bootoct binary */
	if (!block_desc) {
		cvmx_write64_uint32(CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS,
						LABI_ADDR_IN_BOOTLOADER +
						offsetof(struct linux_app_boot_info,
							 avail_coremask)),
				   avail_coremask);
	} else {
		cvmx_write64_uint32(CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS,
						block_desc->base_addr +
						AVAIL_COREMASK_OFFSET_IN_LINUX_APP_BOOT_BLOCK),
				   avail_coremask);
	}

	pr_info("Reset core %d. Available Coremask = %x\n", coreid,
		avail_coremask);
	cvmx_write_csr(CVMX_CIU_PP_RST, 1 << coreid);
	cvmx_write_csr(CVMX_CIU_PP_RST, 0);
}

void play_dead(void)
{
	int coreid = cvmx_get_core_num();

	idle_task_exit();
	octeon_processor_boot = 0xff;
	per_cpu(cpu_state, coreid) = CPU_DEAD;

	while (1)	/* core will be reset here */
		;
}

extern void kernel_entry(unsigned long arg1, ...);

static void start_after_reset(void)
{
	kernel_entry(0, 0, 0);  /* set a2 = 0 for secondary core */
}

int octeon_update_boot_vector(unsigned int cpu)
{

	int coreid = cpu_logical_map(cpu);
	unsigned int avail_coremask;
	struct cvmx_bootmem_named_block_desc *block_desc;
	struct boot_init_vector *boot_vect =
		(struct boot_init_vector *) cvmx_phys_to_ptr(0x0 +
						  BOOTLOADER_BOOT_VECTOR);

	block_desc = cvmx_bootmem_find_named_block(LINUX_APP_BOOT_BLOCK_NAME);

	if (!block_desc) {
		avail_coremask =
			cvmx_read64_uint32(CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS,
					   LABI_ADDR_IN_BOOTLOADER +
						offsetof(struct linux_app_boot_info,
						avail_coremask)));
	} else {		       /* alternative, already initialized */
	       avail_coremask =
		   cvmx_read64_uint32(CVMX_ADD_SEG(CVMX_MIPS_SPACE_XKPHYS,
						   block_desc->base_addr +
						   AVAIL_COREMASK_OFFSET_IN_LINUX_APP_BOOT_BLOCK));
	}

	if (!(avail_coremask & (1 << coreid))) {
		/* core not available, assume, that catched by simple-executive */
		cvmx_write_csr(CVMX_CIU_PP_RST, 1 << coreid);
		cvmx_write_csr(CVMX_CIU_PP_RST, 0);
	}

	boot_vect[coreid].app_start_func_addr =
		(uint32_t) (unsigned long) start_after_reset;
	boot_vect[coreid].code_addr = InitTLBStart_addr;

	CVMX_SYNC;

	cvmx_write_csr(CVMX_CIU_NMI, (1 << coreid) & avail_coremask);

	return 0;
}

static int __cpuinit octeon_cpu_callback(struct notifier_block *nfb,
	unsigned long action, void *hcpu)
{
	unsigned int cpu = (unsigned long)hcpu;

	switch (action) {
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

static struct notifier_block __cpuinitdata octeon_cpu_notifier = {
	.notifier_call = octeon_cpu_callback,
};

static int __cpuinit register_cavium_notifier(void)
{
	register_hotcpu_notifier(&octeon_cpu_notifier);

	return 0;
}

late_initcall(register_cavium_notifier);

#endif  /* CONFIG_HOTPLUG_CPU */

struct plat_smp_ops octeon_smp_ops = {
	.send_ipi_single	= octeon_send_ipi_single,
	.send_ipi_mask		= octeon_send_ipi_mask,
	.init_secondary		= octeon_init_secondary,
	.smp_finish		= octeon_smp_finish,
	.cpus_done		= octeon_cpus_done,
	.boot_secondary		= octeon_boot_secondary,
	.smp_setup		= octeon_smp_setup,
	.prepare_cpus		= octeon_prepare_cpus,
#ifdef CONFIG_HOTPLUG_CPU
	.cpu_disable		= octeon_cpu_disable,
	.cpu_die		= octeon_cpu_die,
#endif
};
