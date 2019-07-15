// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * MIPS idle loop and WAIT instruction support.
 *
 * Copyright (C) xxxx  the Anonymous
 * Copyright (C) 1994 - 2006 Ralf Baechle
 * Copyright (C) 2003, 2004  Maciej W. Rozycki
 * Copyright (C) 2001, 2004, 2011, 2012	 MIPS Technologies, Inc.
 */
#include <linux/cpu.h>
#include <linux/export.h>
#include <linux/init.h>
#include <linux/irqflags.h>
#include <linux/printk.h>
#include <linux/sched.h>
#include <asm/cpu.h>
#include <asm/cpu-info.h>
#include <asm/cpu-type.h>
#include <asm/idle.h>
#include <asm/mipsregs.h>

/*
 * Not all of the MIPS CPUs have the "wait" instruction available. Moreover,
 * the implementation of the "wait" feature differs between CPU families. This
 * points to the function that implements CPU specific wait.
 * The wait instruction stops the pipeline and reduces the power consumption of
 * the CPU very much.
 */
void (*cpu_wait)(void);
EXPORT_SYMBOL(cpu_wait);

static void __cpuidle r3081_wait(void)
{
	unsigned long cfg = read_c0_conf();
	write_c0_conf(cfg | R30XX_CONF_HALT);
	local_irq_enable();
}

static void __cpuidle r39xx_wait(void)
{
	if (!need_resched())
		write_c0_conf(read_c0_conf() | TX39_CONF_HALT);
	local_irq_enable();
}

void __cpuidle r4k_wait(void)
{
	local_irq_enable();
	__r4k_wait();
}

/*
 * This variant is preferable as it allows testing need_resched and going to
 * sleep depending on the outcome atomically.  Unfortunately the "It is
 * implementation-dependent whether the pipeline restarts when a non-enabled
 * interrupt is requested" restriction in the MIPS32/MIPS64 architecture makes
 * using this version a gamble.
 */
void __cpuidle r4k_wait_irqoff(void)
{
	if (!need_resched())
		__asm__(
		"	.set	push		\n"
		"	.set	arch=r4000	\n"
		"	wait			\n"
		"	.set	pop		\n");
	local_irq_enable();
}

/*
 * The RM7000 variant has to handle erratum 38.	 The workaround is to not
 * have any pending stores when the WAIT instruction is executed.
 */
static void __cpuidle rm7k_wait_irqoff(void)
{
	if (!need_resched())
		__asm__(
		"	.set	push					\n"
		"	.set	arch=r4000				\n"
		"	.set	noat					\n"
		"	mfc0	$1, $12					\n"
		"	sync						\n"
		"	mtc0	$1, $12		# stalls until W stage	\n"
		"	wait						\n"
		"	mtc0	$1, $12		# stalls until W stage	\n"
		"	.set	pop					\n");
	local_irq_enable();
}

/*
 * Au1 'wait' is only useful when the 32kHz counter is used as timer,
 * since coreclock (and the cp0 counter) stops upon executing it. Only an
 * interrupt can wake it, so they must be enabled before entering idle modes.
 */
static void __cpuidle au1k_wait(void)
{
	unsigned long c0status = read_c0_status() | 1;	/* irqs on */

	__asm__(
	"	.set	push			\n"
	"	.set	arch=r4000		\n"
	"	cache	0x14, 0(%0)		\n"
	"	cache	0x14, 32(%0)		\n"
	"	sync				\n"
	"	mtc0	%1, $12			\n" /* wr c0status */
	"	wait				\n"
	"	nop				\n"
	"	nop				\n"
	"	nop				\n"
	"	nop				\n"
	"	.set	pop			\n"
	: : "r" (au1k_wait), "r" (c0status));
}

static int __initdata nowait;

static int __init wait_disable(char *s)
{
	nowait = 1;

	return 1;
}

__setup("nowait", wait_disable);

void __init check_wait(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;

	if (nowait) {
		printk("Wait instruction disabled.\n");
		return;
	}

	/*
	 * MIPSr6 specifies that masked interrupts should unblock an executing
	 * wait instruction, and thus that it is safe for us to use
	 * r4k_wait_irqoff. Yippee!
	 */
	if (cpu_has_mips_r6) {
		cpu_wait = r4k_wait_irqoff;
		return;
	}

	switch (current_cpu_type()) {
	case CPU_R3081:
	case CPU_R3081E:
		cpu_wait = r3081_wait;
		break;
	case CPU_TX3927:
		cpu_wait = r39xx_wait;
		break;
	case CPU_R4200:
/*	case CPU_R4300: */
	case CPU_R4600:
	case CPU_R4640:
	case CPU_R4650:
	case CPU_R4700:
	case CPU_R5000:
	case CPU_R5500:
	case CPU_NEVADA:
	case CPU_4KC:
	case CPU_4KEC:
	case CPU_4KSC:
	case CPU_5KC:
	case CPU_5KE:
	case CPU_25KF:
	case CPU_PR4450:
	case CPU_BMIPS3300:
	case CPU_BMIPS4350:
	case CPU_BMIPS4380:
	case CPU_CAVIUM_OCTEON:
	case CPU_CAVIUM_OCTEON_PLUS:
	case CPU_CAVIUM_OCTEON2:
	case CPU_CAVIUM_OCTEON3:
	case CPU_JZRISC:
	case CPU_LOONGSON1:
	case CPU_XLR:
	case CPU_XLP:
		cpu_wait = r4k_wait;
		break;
	case CPU_LOONGSON3:
		if ((c->processor_id & PRID_REV_MASK) >= PRID_REV_LOONGSON3A_R2_0)
			cpu_wait = r4k_wait;
		break;

	case CPU_BMIPS5000:
		cpu_wait = r4k_wait_irqoff;
		break;
	case CPU_RM7000:
		cpu_wait = rm7k_wait_irqoff;
		break;

	case CPU_PROAPTIV:
	case CPU_P5600:
		/*
		 * Incoming Fast Debug Channel (FDC) data during a wait
		 * instruction causes the wait never to resume, even if an
		 * interrupt is received. Avoid using wait at all if FDC data is
		 * likely to be received.
		 */
		if (IS_ENABLED(CONFIG_MIPS_EJTAG_FDC_TTY))
			break;
		/* fall through */
	case CPU_M14KC:
	case CPU_M14KEC:
	case CPU_24K:
	case CPU_34K:
	case CPU_1004K:
	case CPU_1074K:
	case CPU_INTERAPTIV:
	case CPU_M5150:
	case CPU_QEMU_GENERIC:
		cpu_wait = r4k_wait;
		if (read_c0_config7() & MIPS_CONF7_WII)
			cpu_wait = r4k_wait_irqoff;
		break;

	case CPU_74K:
		cpu_wait = r4k_wait;
		if ((c->processor_id & 0xff) >= PRID_REV_ENCODE_332(2, 1, 0))
			cpu_wait = r4k_wait_irqoff;
		break;

	case CPU_TX49XX:
		cpu_wait = r4k_wait_irqoff;
		break;
	case CPU_ALCHEMY:
		cpu_wait = au1k_wait;
		break;
	case CPU_20KC:
		/*
		 * WAIT on Rev1.0 has E1, E2, E3 and E16.
		 * WAIT on Rev2.0 and Rev3.0 has E16.
		 * Rev3.1 WAIT is nop, why bother
		 */
		if ((c->processor_id & 0xff) <= 0x64)
			break;

		/*
		 * Another rev is incremeting c0_count at a reduced clock
		 * rate while in WAIT mode.  So we basically have the choice
		 * between using the cp0 timer as clocksource or avoiding
		 * the WAIT instruction.  Until more details are known,
		 * disable the use of WAIT for 20Kc entirely.
		   cpu_wait = r4k_wait;
		 */
		break;
	default:
		break;
	}
}

void arch_cpu_idle(void)
{
	if (cpu_wait)
		cpu_wait();
	else
		local_irq_enable();
}

#ifdef CONFIG_CPU_IDLE

int mips_cpuidle_wait_enter(struct cpuidle_device *dev,
			    struct cpuidle_driver *drv, int index)
{
	arch_cpu_idle();
	return index;
}

#endif
