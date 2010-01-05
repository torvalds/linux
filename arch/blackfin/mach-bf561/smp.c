/*
 * Copyright 2007-2009 Analog Devices Inc.
 *               Philippe Gerum <rpm@xenomai.org>
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <asm/smp.h>
#include <asm/dma.h>

static DEFINE_SPINLOCK(boot_lock);

static cpumask_t cpu_callin_map;

/*
 * platform_init_cpus() - Tell the world about how many cores we
 * have. This is called while setting up the architecture support
 * (setup_arch()), so don't be too demanding here with respect to
 * available kernel services.
 */

void __init platform_init_cpus(void)
{
	cpu_set(0, cpu_possible_map); /* CoreA */
	cpu_set(1, cpu_possible_map); /* CoreB */
}

void __init platform_prepare_cpus(unsigned int max_cpus)
{
	int len;

	len = &coreb_trampoline_end - &coreb_trampoline_start + 1;
	BUG_ON(len > L1_CODE_LENGTH);

	dma_memcpy((void *)COREB_L1_CODE_START, &coreb_trampoline_start, len);

	/* Both cores ought to be present on a bf561! */
	cpu_set(0, cpu_present_map); /* CoreA */
	cpu_set(1, cpu_present_map); /* CoreB */

	printk(KERN_INFO "CoreB bootstrap code to SRAM %p via DMA.\n", (void *)COREB_L1_CODE_START);
}

int __init setup_profiling_timer(unsigned int multiplier) /* not supported */
{
	return -EINVAL;
}

void __cpuinit platform_secondary_init(unsigned int cpu)
{
	/* Clone setup for peripheral interrupt sources from CoreA. */
	bfin_write_SICB_IMASK0(bfin_read_SICA_IMASK0());
	bfin_write_SICB_IMASK1(bfin_read_SICA_IMASK1());
	SSYNC();

	/* Clone setup for IARs from CoreA. */
	bfin_write_SICB_IAR0(bfin_read_SICA_IAR0());
	bfin_write_SICB_IAR1(bfin_read_SICA_IAR1());
	bfin_write_SICB_IAR2(bfin_read_SICA_IAR2());
	bfin_write_SICB_IAR3(bfin_read_SICA_IAR3());
	bfin_write_SICB_IAR4(bfin_read_SICA_IAR4());
	bfin_write_SICB_IAR5(bfin_read_SICA_IAR5());
	bfin_write_SICB_IAR6(bfin_read_SICA_IAR6());
	bfin_write_SICB_IAR7(bfin_read_SICA_IAR7());
	SSYNC();

	/* Store CPU-private information to the cpu_data array. */
	bfin_setup_cpudata(cpu);

	/* We are done with local CPU inits, unblock the boot CPU. */
	cpu_set(cpu, cpu_callin_map);
	spin_lock(&boot_lock);
	spin_unlock(&boot_lock);
}

int __cpuinit platform_boot_secondary(unsigned int cpu, struct task_struct *idle)
{
	unsigned long timeout;

	/* CoreB already running?! */
	BUG_ON((bfin_read_SICA_SYSCR() & COREB_SRAM_INIT) == 0);

	printk(KERN_INFO "Booting Core B.\n");

	spin_lock(&boot_lock);

	/* Kick CoreB, which should start execution from CORE_SRAM_BASE. */
	SSYNC();
	bfin_write_SICA_SYSCR(bfin_read_SICA_SYSCR() & ~COREB_SRAM_INIT);
	SSYNC();

	timeout = jiffies + 1 * HZ;
	while (time_before(jiffies, timeout)) {
		if (cpu_isset(cpu, cpu_callin_map))
			break;
		udelay(100);
		barrier();
	}

	if (cpu_isset(cpu, cpu_callin_map)) {
		cpu_set(cpu, cpu_online_map);
		/* release the lock and let coreb run */
		spin_unlock(&boot_lock);
		return 0;
	} else
		panic("CPU%u: processor failed to boot\n", cpu);
}

void __init platform_request_ipi(irq_handler_t handler)
{
	int ret;

	ret = request_irq(IRQ_SUPPLE_0, handler, IRQF_DISABLED,
			  "Supplemental Interrupt0", handler);
	if (ret)
		panic("Cannot request supplemental interrupt 0 for IPI service");
}

void platform_send_ipi(cpumask_t callmap)
{
	unsigned int cpu;

	for_each_cpu_mask(cpu, callmap) {
		BUG_ON(cpu >= 2);
		SSYNC();
		bfin_write_SICB_SYSCR(bfin_read_SICB_SYSCR() | (1 << (6 + cpu)));
		SSYNC();
	}
}

void platform_send_ipi_cpu(unsigned int cpu)
{
	BUG_ON(cpu >= 2);
	SSYNC();
	bfin_write_SICB_SYSCR(bfin_read_SICB_SYSCR() | (1 << (6 + cpu)));
	SSYNC();
}

void platform_clear_ipi(unsigned int cpu)
{
	BUG_ON(cpu >= 2);
	SSYNC();
	bfin_write_SICB_SYSCR(bfin_read_SICB_SYSCR() | (1 << (10 + cpu)));
	SSYNC();
}
