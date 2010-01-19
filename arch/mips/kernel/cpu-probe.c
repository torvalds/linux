/*
 * Processor capabilities determination functions.
 *
 * Copyright (C) xxxx  the Anonymous
 * Copyright (C) 1994 - 2006 Ralf Baechle
 * Copyright (C) 2003, 2004  Maciej W. Rozycki
 * Copyright (C) 2001, 2004  MIPS Inc.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version
 * 2 of the License, or (at your option) any later version.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/module.h>

#include <asm/bugs.h>
#include <asm/cpu.h>
#include <asm/fpu.h>
#include <asm/mipsregs.h>
#include <asm/system.h>
#include <asm/watch.h>
#include <asm/spram.h>
/*
 * Not all of the MIPS CPUs have the "wait" instruction available. Moreover,
 * the implementation of the "wait" feature differs between CPU families. This
 * points to the function that implements CPU specific wait.
 * The wait instruction stops the pipeline and reduces the power consumption of
 * the CPU very much.
 */
void (*cpu_wait)(void);
EXPORT_SYMBOL(cpu_wait);

static void r3081_wait(void)
{
	unsigned long cfg = read_c0_conf();
	write_c0_conf(cfg | R30XX_CONF_HALT);
}

static void r39xx_wait(void)
{
	local_irq_disable();
	if (!need_resched())
		write_c0_conf(read_c0_conf() | TX39_CONF_HALT);
	local_irq_enable();
}

extern void r4k_wait(void);

/*
 * This variant is preferable as it allows testing need_resched and going to
 * sleep depending on the outcome atomically.  Unfortunately the "It is
 * implementation-dependent whether the pipeline restarts when a non-enabled
 * interrupt is requested" restriction in the MIPS32/MIPS64 architecture makes
 * using this version a gamble.
 */
void r4k_wait_irqoff(void)
{
	local_irq_disable();
	if (!need_resched())
		__asm__("	.set	push		\n"
			"	.set	mips3		\n"
			"	wait			\n"
			"	.set	pop		\n");
	local_irq_enable();
	__asm__(" 	.globl __pastwait	\n"
		"__pastwait:			\n");
	return;
}

/*
 * The RM7000 variant has to handle erratum 38.  The workaround is to not
 * have any pending stores when the WAIT instruction is executed.
 */
static void rm7k_wait_irqoff(void)
{
	local_irq_disable();
	if (!need_resched())
		__asm__(
		"	.set	push					\n"
		"	.set	mips3					\n"
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
 * The Au1xxx wait is available only if using 32khz counter or
 * external timer source, but specifically not CP0 Counter.
 * alchemy/common/time.c may override cpu_wait!
 */
static void au1k_wait(void)
{
	__asm__("	.set	mips3			\n"
		"	cache	0x14, 0(%0)		\n"
		"	cache	0x14, 32(%0)		\n"
		"	sync				\n"
		"	nop				\n"
		"	wait				\n"
		"	nop				\n"
		"	nop				\n"
		"	nop				\n"
		"	nop				\n"
		"	.set	mips0			\n"
		: : "r" (au1k_wait));
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

	switch (c->cputype) {
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
	case CPU_25KF:
	case CPU_PR4450:
	case CPU_BCM3302:
	case CPU_BCM6338:
	case CPU_BCM6348:
	case CPU_BCM6358:
	case CPU_CAVIUM_OCTEON:
		cpu_wait = r4k_wait;
		break;

	case CPU_RM7000:
		cpu_wait = rm7k_wait_irqoff;
		break;

	case CPU_24K:
	case CPU_34K:
	case CPU_1004K:
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
	case CPU_RM9000:
		if ((c->processor_id & 0x00ff) >= 0x40)
			cpu_wait = r4k_wait;
		break;
	default:
		break;
	}
}

static inline void check_errata(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;

	switch (c->cputype) {
	case CPU_34K:
		/*
		 * Erratum "RPS May Cause Incorrect Instruction Execution"
		 * This code only handles VPE0, any SMP/SMTC/RTOS code
		 * making use of VPE1 will be responsable for that VPE.
		 */
		if ((c->processor_id & PRID_REV_MASK) <= PRID_REV_34K_V1_0_2)
			write_c0_config7(read_c0_config7() | MIPS_CONF7_RPS);
		break;
	default:
		break;
	}
}

void __init check_bugs32(void)
{
	check_errata();
}

/*
 * Probe whether cpu has config register by trying to play with
 * alternate cache bit and see whether it matters.
 * It's used by cpu_probe to distinguish between R3000A and R3081.
 */
static inline int cpu_has_confreg(void)
{
#ifdef CONFIG_CPU_R3000
	extern unsigned long r3k_cache_size(unsigned long);
	unsigned long size1, size2;
	unsigned long cfg = read_c0_conf();

	size1 = r3k_cache_size(ST0_ISC);
	write_c0_conf(cfg ^ R30XX_CONF_AC);
	size2 = r3k_cache_size(ST0_ISC);
	write_c0_conf(cfg);
	return size1 != size2;
#else
	return 0;
#endif
}

/*
 * Get the FPU Implementation/Revision.
 */
static inline unsigned long cpu_get_fpu_id(void)
{
	unsigned long tmp, fpu_id;

	tmp = read_c0_status();
	__enable_fpu();
	fpu_id = read_32bit_cp1_register(CP1_REVISION);
	write_c0_status(tmp);
	return fpu_id;
}

/*
 * Check the CPU has an FPU the official way.
 */
static inline int __cpu_has_fpu(void)
{
	return ((cpu_get_fpu_id() & 0xff00) != FPIR_IMP_NONE);
}

static inline void cpu_probe_vmbits(struct cpuinfo_mips *c)
{
#ifdef __NEED_VMBITS_PROBE
	write_c0_entryhi(0x3fffffffffffe000ULL);
	back_to_back_c0_hazard();
	c->vmbits = fls64(read_c0_entryhi() & 0x3fffffffffffe000ULL);
#endif
}

#define R4K_OPTS (MIPS_CPU_TLB | MIPS_CPU_4KEX | MIPS_CPU_4K_CACHE \
		| MIPS_CPU_COUNTER)

static inline void cpu_probe_legacy(struct cpuinfo_mips *c, unsigned int cpu)
{
	switch (c->processor_id & 0xff00) {
	case PRID_IMP_R2000:
		c->cputype = CPU_R2000;
		__cpu_name[cpu] = "R2000";
		c->isa_level = MIPS_CPU_ISA_I;
		c->options = MIPS_CPU_TLB | MIPS_CPU_3K_CACHE |
		             MIPS_CPU_NOFPUEX;
		if (__cpu_has_fpu())
			c->options |= MIPS_CPU_FPU;
		c->tlbsize = 64;
		break;
	case PRID_IMP_R3000:
		if ((c->processor_id & 0xff) == PRID_REV_R3000A) {
			if (cpu_has_confreg()) {
				c->cputype = CPU_R3081E;
				__cpu_name[cpu] = "R3081";
			} else {
				c->cputype = CPU_R3000A;
				__cpu_name[cpu] = "R3000A";
			}
			break;
		} else {
			c->cputype = CPU_R3000;
			__cpu_name[cpu] = "R3000";
		}
		c->isa_level = MIPS_CPU_ISA_I;
		c->options = MIPS_CPU_TLB | MIPS_CPU_3K_CACHE |
		             MIPS_CPU_NOFPUEX;
		if (__cpu_has_fpu())
			c->options |= MIPS_CPU_FPU;
		c->tlbsize = 64;
		break;
	case PRID_IMP_R4000:
		if (read_c0_config() & CONF_SC) {
			if ((c->processor_id & 0xff) >= PRID_REV_R4400) {
				c->cputype = CPU_R4400PC;
				__cpu_name[cpu] = "R4400PC";
			} else {
				c->cputype = CPU_R4000PC;
				__cpu_name[cpu] = "R4000PC";
			}
		} else {
			if ((c->processor_id & 0xff) >= PRID_REV_R4400) {
				c->cputype = CPU_R4400SC;
				__cpu_name[cpu] = "R4400SC";
			} else {
				c->cputype = CPU_R4000SC;
				__cpu_name[cpu] = "R4000SC";
			}
		}

		c->isa_level = MIPS_CPU_ISA_III;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_WATCH | MIPS_CPU_VCE |
		             MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_VR41XX:
		switch (c->processor_id & 0xf0) {
		case PRID_REV_VR4111:
			c->cputype = CPU_VR4111;
			__cpu_name[cpu] = "NEC VR4111";
			break;
		case PRID_REV_VR4121:
			c->cputype = CPU_VR4121;
			__cpu_name[cpu] = "NEC VR4121";
			break;
		case PRID_REV_VR4122:
			if ((c->processor_id & 0xf) < 0x3) {
				c->cputype = CPU_VR4122;
				__cpu_name[cpu] = "NEC VR4122";
			} else {
				c->cputype = CPU_VR4181A;
				__cpu_name[cpu] = "NEC VR4181A";
			}
			break;
		case PRID_REV_VR4130:
			if ((c->processor_id & 0xf) < 0x4) {
				c->cputype = CPU_VR4131;
				__cpu_name[cpu] = "NEC VR4131";
			} else {
				c->cputype = CPU_VR4133;
				__cpu_name[cpu] = "NEC VR4133";
			}
			break;
		default:
			printk(KERN_INFO "Unexpected CPU of NEC VR4100 series\n");
			c->cputype = CPU_VR41XX;
			__cpu_name[cpu] = "NEC Vr41xx";
			break;
		}
		c->isa_level = MIPS_CPU_ISA_III;
		c->options = R4K_OPTS;
		c->tlbsize = 32;
		break;
	case PRID_IMP_R4300:
		c->cputype = CPU_R4300;
		__cpu_name[cpu] = "R4300";
		c->isa_level = MIPS_CPU_ISA_III;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_LLSC;
		c->tlbsize = 32;
		break;
	case PRID_IMP_R4600:
		c->cputype = CPU_R4600;
		__cpu_name[cpu] = "R4600";
		c->isa_level = MIPS_CPU_ISA_III;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	#if 0
 	case PRID_IMP_R4650:
		/*
		 * This processor doesn't have an MMU, so it's not
		 * "real easy" to run Linux on it. It is left purely
		 * for documentation.  Commented out because it shares
		 * it's c0_prid id number with the TX3900.
		 */
		c->cputype = CPU_R4650;
		__cpu_name[cpu] = "R4650";
	 	c->isa_level = MIPS_CPU_ISA_III;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_LLSC;
	        c->tlbsize = 48;
		break;
	#endif
	case PRID_IMP_TX39:
		c->isa_level = MIPS_CPU_ISA_I;
		c->options = MIPS_CPU_TLB | MIPS_CPU_TX39_CACHE;

		if ((c->processor_id & 0xf0) == (PRID_REV_TX3927 & 0xf0)) {
			c->cputype = CPU_TX3927;
			__cpu_name[cpu] = "TX3927";
			c->tlbsize = 64;
		} else {
			switch (c->processor_id & 0xff) {
			case PRID_REV_TX3912:
				c->cputype = CPU_TX3912;
				__cpu_name[cpu] = "TX3912";
				c->tlbsize = 32;
				break;
			case PRID_REV_TX3922:
				c->cputype = CPU_TX3922;
				__cpu_name[cpu] = "TX3922";
				c->tlbsize = 64;
				break;
			}
		}
		break;
	case PRID_IMP_R4700:
		c->cputype = CPU_R4700;
		__cpu_name[cpu] = "R4700";
		c->isa_level = MIPS_CPU_ISA_III;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_TX49:
		c->cputype = CPU_TX49XX;
		__cpu_name[cpu] = "R49XX";
		c->isa_level = MIPS_CPU_ISA_III;
		c->options = R4K_OPTS | MIPS_CPU_LLSC;
		if (!(c->processor_id & 0x08))
			c->options |= MIPS_CPU_FPU | MIPS_CPU_32FPR;
		c->tlbsize = 48;
		break;
	case PRID_IMP_R5000:
		c->cputype = CPU_R5000;
		__cpu_name[cpu] = "R5000";
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_R5432:
		c->cputype = CPU_R5432;
		__cpu_name[cpu] = "R5432";
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_WATCH | MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_R5500:
		c->cputype = CPU_R5500;
		__cpu_name[cpu] = "R5500";
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_WATCH | MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_NEVADA:
		c->cputype = CPU_NEVADA;
		__cpu_name[cpu] = "Nevada";
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_DIVEC | MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_R6000:
		c->cputype = CPU_R6000;
		__cpu_name[cpu] = "R6000";
		c->isa_level = MIPS_CPU_ISA_II;
		c->options = MIPS_CPU_TLB | MIPS_CPU_FPU |
		             MIPS_CPU_LLSC;
		c->tlbsize = 32;
		break;
	case PRID_IMP_R6000A:
		c->cputype = CPU_R6000A;
		__cpu_name[cpu] = "R6000A";
		c->isa_level = MIPS_CPU_ISA_II;
		c->options = MIPS_CPU_TLB | MIPS_CPU_FPU |
		             MIPS_CPU_LLSC;
		c->tlbsize = 32;
		break;
	case PRID_IMP_RM7000:
		c->cputype = CPU_RM7000;
		__cpu_name[cpu] = "RM7000";
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_LLSC;
		/*
		 * Undocumented RM7000:  Bit 29 in the info register of
		 * the RM7000 v2.0 indicates if the TLB has 48 or 64
		 * entries.
		 *
		 * 29      1 =>    64 entry JTLB
		 *         0 =>    48 entry JTLB
		 */
		c->tlbsize = (read_c0_info() & (1 << 29)) ? 64 : 48;
		break;
	case PRID_IMP_RM9000:
		c->cputype = CPU_RM9000;
		__cpu_name[cpu] = "RM9000";
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_LLSC;
		/*
		 * Bit 29 in the info register of the RM9000
		 * indicates if the TLB has 48 or 64 entries.
		 *
		 * 29      1 =>    64 entry JTLB
		 *         0 =>    48 entry JTLB
		 */
		c->tlbsize = (read_c0_info() & (1 << 29)) ? 64 : 48;
		break;
	case PRID_IMP_R8000:
		c->cputype = CPU_R8000;
		__cpu_name[cpu] = "RM8000";
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = MIPS_CPU_TLB | MIPS_CPU_4KEX |
		             MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_LLSC;
		c->tlbsize = 384;      /* has weird TLB: 3-way x 128 */
		break;
	case PRID_IMP_R10000:
		c->cputype = CPU_R10000;
		__cpu_name[cpu] = "R10000";
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = MIPS_CPU_TLB | MIPS_CPU_4K_CACHE | MIPS_CPU_4KEX |
		             MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_COUNTER | MIPS_CPU_WATCH |
		             MIPS_CPU_LLSC;
		c->tlbsize = 64;
		break;
	case PRID_IMP_R12000:
		c->cputype = CPU_R12000;
		__cpu_name[cpu] = "R12000";
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = MIPS_CPU_TLB | MIPS_CPU_4K_CACHE | MIPS_CPU_4KEX |
		             MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_COUNTER | MIPS_CPU_WATCH |
		             MIPS_CPU_LLSC;
		c->tlbsize = 64;
		break;
	case PRID_IMP_R14000:
		c->cputype = CPU_R14000;
		__cpu_name[cpu] = "R14000";
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = MIPS_CPU_TLB | MIPS_CPU_4K_CACHE | MIPS_CPU_4KEX |
		             MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_COUNTER | MIPS_CPU_WATCH |
		             MIPS_CPU_LLSC;
		c->tlbsize = 64;
		break;
	case PRID_IMP_LOONGSON2:
		c->cputype = CPU_LOONGSON2;
		__cpu_name[cpu] = "ICT Loongson-2";
		c->isa_level = MIPS_CPU_ISA_III;
		c->options = R4K_OPTS |
			     MIPS_CPU_FPU | MIPS_CPU_LLSC |
			     MIPS_CPU_32FPR;
		c->tlbsize = 64;
		break;
	}
}

static char unknown_isa[] __cpuinitdata = KERN_ERR \
	"Unsupported ISA type, c0.config0: %d.";

static inline unsigned int decode_config0(struct cpuinfo_mips *c)
{
	unsigned int config0;
	int isa;

	config0 = read_c0_config();

	if (((config0 & MIPS_CONF_MT) >> 7) == 1)
		c->options |= MIPS_CPU_TLB;
	isa = (config0 & MIPS_CONF_AT) >> 13;
	switch (isa) {
	case 0:
		switch ((config0 & MIPS_CONF_AR) >> 10) {
		case 0:
			c->isa_level = MIPS_CPU_ISA_M32R1;
			break;
		case 1:
			c->isa_level = MIPS_CPU_ISA_M32R2;
			break;
		default:
			goto unknown;
		}
		break;
	case 2:
		switch ((config0 & MIPS_CONF_AR) >> 10) {
		case 0:
			c->isa_level = MIPS_CPU_ISA_M64R1;
			break;
		case 1:
			c->isa_level = MIPS_CPU_ISA_M64R2;
			break;
		default:
			goto unknown;
		}
		break;
	default:
		goto unknown;
	}

	return config0 & MIPS_CONF_M;

unknown:
	panic(unknown_isa, config0);
}

static inline unsigned int decode_config1(struct cpuinfo_mips *c)
{
	unsigned int config1;

	config1 = read_c0_config1();

	if (config1 & MIPS_CONF1_MD)
		c->ases |= MIPS_ASE_MDMX;
	if (config1 & MIPS_CONF1_WR)
		c->options |= MIPS_CPU_WATCH;
	if (config1 & MIPS_CONF1_CA)
		c->ases |= MIPS_ASE_MIPS16;
	if (config1 & MIPS_CONF1_EP)
		c->options |= MIPS_CPU_EJTAG;
	if (config1 & MIPS_CONF1_FP) {
		c->options |= MIPS_CPU_FPU;
		c->options |= MIPS_CPU_32FPR;
	}
	if (cpu_has_tlb)
		c->tlbsize = ((config1 & MIPS_CONF1_TLBS) >> 25) + 1;

	return config1 & MIPS_CONF_M;
}

static inline unsigned int decode_config2(struct cpuinfo_mips *c)
{
	unsigned int config2;

	config2 = read_c0_config2();

	if (config2 & MIPS_CONF2_SL)
		c->scache.flags &= ~MIPS_CACHE_NOT_PRESENT;

	return config2 & MIPS_CONF_M;
}

static inline unsigned int decode_config3(struct cpuinfo_mips *c)
{
	unsigned int config3;

	config3 = read_c0_config3();

	if (config3 & MIPS_CONF3_SM)
		c->ases |= MIPS_ASE_SMARTMIPS;
	if (config3 & MIPS_CONF3_DSP)
		c->ases |= MIPS_ASE_DSP;
	if (config3 & MIPS_CONF3_VINT)
		c->options |= MIPS_CPU_VINT;
	if (config3 & MIPS_CONF3_VEIC)
		c->options |= MIPS_CPU_VEIC;
	if (config3 & MIPS_CONF3_MT)
	        c->ases |= MIPS_ASE_MIPSMT;
	if (config3 & MIPS_CONF3_ULRI)
		c->options |= MIPS_CPU_ULRI;

	return config3 & MIPS_CONF_M;
}

static inline unsigned int decode_config4(struct cpuinfo_mips *c)
{
	unsigned int config4;

	config4 = read_c0_config4();

	if ((config4 & MIPS_CONF4_MMUEXTDEF) == MIPS_CONF4_MMUEXTDEF_MMUSIZEEXT
	    && cpu_has_tlb)
		c->tlbsize += (config4 & MIPS_CONF4_MMUSIZEEXT) * 0x40;

	return config4 & MIPS_CONF_M;
}

static void __cpuinit decode_configs(struct cpuinfo_mips *c)
{
	int ok;

	/* MIPS32 or MIPS64 compliant CPU.  */
	c->options = MIPS_CPU_4KEX | MIPS_CPU_4K_CACHE | MIPS_CPU_COUNTER |
	             MIPS_CPU_DIVEC | MIPS_CPU_LLSC | MIPS_CPU_MCHECK;

	c->scache.flags = MIPS_CACHE_NOT_PRESENT;

	ok = decode_config0(c);			/* Read Config registers.  */
	BUG_ON(!ok);				/* Arch spec violation!  */
	if (ok)
		ok = decode_config1(c);
	if (ok)
		ok = decode_config2(c);
	if (ok)
		ok = decode_config3(c);
	if (ok)
		ok = decode_config4(c);

	mips_probe_watch_registers(c);
}

static inline void cpu_probe_mips(struct cpuinfo_mips *c, unsigned int cpu)
{
	decode_configs(c);
	switch (c->processor_id & 0xff00) {
	case PRID_IMP_4KC:
		c->cputype = CPU_4KC;
		__cpu_name[cpu] = "MIPS 4Kc";
		break;
	case PRID_IMP_4KEC:
	case PRID_IMP_4KECR2:
		c->cputype = CPU_4KEC;
		__cpu_name[cpu] = "MIPS 4KEc";
		break;
	case PRID_IMP_4KSC:
	case PRID_IMP_4KSD:
		c->cputype = CPU_4KSC;
		__cpu_name[cpu] = "MIPS 4KSc";
		break;
	case PRID_IMP_5KC:
		c->cputype = CPU_5KC;
		__cpu_name[cpu] = "MIPS 5Kc";
		break;
	case PRID_IMP_20KC:
		c->cputype = CPU_20KC;
		__cpu_name[cpu] = "MIPS 20Kc";
		break;
	case PRID_IMP_24K:
	case PRID_IMP_24KE:
		c->cputype = CPU_24K;
		__cpu_name[cpu] = "MIPS 24Kc";
		break;
	case PRID_IMP_25KF:
		c->cputype = CPU_25KF;
		__cpu_name[cpu] = "MIPS 25Kc";
		break;
	case PRID_IMP_34K:
		c->cputype = CPU_34K;
		__cpu_name[cpu] = "MIPS 34Kc";
		break;
	case PRID_IMP_74K:
		c->cputype = CPU_74K;
		__cpu_name[cpu] = "MIPS 74Kc";
		break;
	case PRID_IMP_1004K:
		c->cputype = CPU_1004K;
		__cpu_name[cpu] = "MIPS 1004Kc";
		break;
	}

	spram_config();
}

static inline void cpu_probe_alchemy(struct cpuinfo_mips *c, unsigned int cpu)
{
	decode_configs(c);
	switch (c->processor_id & 0xff00) {
	case PRID_IMP_AU1_REV1:
	case PRID_IMP_AU1_REV2:
		c->cputype = CPU_ALCHEMY;
		switch ((c->processor_id >> 24) & 0xff) {
		case 0:
			__cpu_name[cpu] = "Au1000";
			break;
		case 1:
			__cpu_name[cpu] = "Au1500";
			break;
		case 2:
			__cpu_name[cpu] = "Au1100";
			break;
		case 3:
			__cpu_name[cpu] = "Au1550";
			break;
		case 4:
			__cpu_name[cpu] = "Au1200";
			if ((c->processor_id & 0xff) == 2)
				__cpu_name[cpu] = "Au1250";
			break;
		case 5:
			__cpu_name[cpu] = "Au1210";
			break;
		default:
			__cpu_name[cpu] = "Au1xxx";
			break;
		}
		break;
	}
}

static inline void cpu_probe_sibyte(struct cpuinfo_mips *c, unsigned int cpu)
{
	decode_configs(c);

	switch (c->processor_id & 0xff00) {
	case PRID_IMP_SB1:
		c->cputype = CPU_SB1;
		__cpu_name[cpu] = "SiByte SB1";
		/* FPU in pass1 is known to have issues. */
		if ((c->processor_id & 0xff) < 0x02)
			c->options &= ~(MIPS_CPU_FPU | MIPS_CPU_32FPR);
		break;
	case PRID_IMP_SB1A:
		c->cputype = CPU_SB1A;
		__cpu_name[cpu] = "SiByte SB1A";
		break;
	}
}

static inline void cpu_probe_sandcraft(struct cpuinfo_mips *c, unsigned int cpu)
{
	decode_configs(c);
	switch (c->processor_id & 0xff00) {
	case PRID_IMP_SR71000:
		c->cputype = CPU_SR71000;
		__cpu_name[cpu] = "Sandcraft SR71000";
		c->scache.ways = 8;
		c->tlbsize = 64;
		break;
	}
}

static inline void cpu_probe_nxp(struct cpuinfo_mips *c, unsigned int cpu)
{
	decode_configs(c);
	switch (c->processor_id & 0xff00) {
	case PRID_IMP_PR4450:
		c->cputype = CPU_PR4450;
		__cpu_name[cpu] = "Philips PR4450";
		c->isa_level = MIPS_CPU_ISA_M32R1;
		break;
	}
}

static inline void cpu_probe_broadcom(struct cpuinfo_mips *c, unsigned int cpu)
{
	decode_configs(c);
	switch (c->processor_id & 0xff00) {
	case PRID_IMP_BCM3302:
	 /* same as PRID_IMP_BCM6338 */
		c->cputype = CPU_BCM3302;
		__cpu_name[cpu] = "Broadcom BCM3302";
		break;
	case PRID_IMP_BCM4710:
		c->cputype = CPU_BCM4710;
		__cpu_name[cpu] = "Broadcom BCM4710";
		break;
	case PRID_IMP_BCM6345:
		c->cputype = CPU_BCM6345;
		__cpu_name[cpu] = "Broadcom BCM6345";
		break;
	case PRID_IMP_BCM6348:
		c->cputype = CPU_BCM6348;
		__cpu_name[cpu] = "Broadcom BCM6348";
		break;
	case PRID_IMP_BCM4350:
		switch (c->processor_id & 0xf0) {
		case PRID_REV_BCM6358:
			c->cputype = CPU_BCM6358;
			__cpu_name[cpu] = "Broadcom BCM6358";
			break;
		default:
			c->cputype = CPU_UNKNOWN;
			break;
		}
		break;
	}
}

static inline void cpu_probe_cavium(struct cpuinfo_mips *c, unsigned int cpu)
{
	decode_configs(c);
	switch (c->processor_id & 0xff00) {
	case PRID_IMP_CAVIUM_CN38XX:
	case PRID_IMP_CAVIUM_CN31XX:
	case PRID_IMP_CAVIUM_CN30XX:
	case PRID_IMP_CAVIUM_CN58XX:
	case PRID_IMP_CAVIUM_CN56XX:
	case PRID_IMP_CAVIUM_CN50XX:
	case PRID_IMP_CAVIUM_CN52XX:
		c->cputype = CPU_CAVIUM_OCTEON;
		__cpu_name[cpu] = "Cavium Octeon";
		break;
	default:
		printk(KERN_INFO "Unknown Octeon chip!\n");
		c->cputype = CPU_UNKNOWN;
		break;
	}
}

const char *__cpu_name[NR_CPUS];

__cpuinit void cpu_probe(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;
	unsigned int cpu = smp_processor_id();

	c->processor_id	= PRID_IMP_UNKNOWN;
	c->fpu_id	= FPIR_IMP_NONE;
	c->cputype	= CPU_UNKNOWN;

	c->processor_id = read_c0_prid();
	switch (c->processor_id & 0xff0000) {
	case PRID_COMP_LEGACY:
		cpu_probe_legacy(c, cpu);
		break;
	case PRID_COMP_MIPS:
		cpu_probe_mips(c, cpu);
		break;
	case PRID_COMP_ALCHEMY:
		cpu_probe_alchemy(c, cpu);
		break;
	case PRID_COMP_SIBYTE:
		cpu_probe_sibyte(c, cpu);
		break;
	case PRID_COMP_BROADCOM:
		cpu_probe_broadcom(c, cpu);
		break;
	case PRID_COMP_SANDCRAFT:
		cpu_probe_sandcraft(c, cpu);
		break;
	case PRID_COMP_NXP:
		cpu_probe_nxp(c, cpu);
		break;
	case PRID_COMP_CAVIUM:
		cpu_probe_cavium(c, cpu);
		break;
	}

	BUG_ON(!__cpu_name[cpu]);
	BUG_ON(c->cputype == CPU_UNKNOWN);

	/*
	 * Platform code can force the cpu type to optimize code
	 * generation. In that case be sure the cpu type is correctly
	 * manually setup otherwise it could trigger some nasty bugs.
	 */
	BUG_ON(current_cpu_type() != c->cputype);

	if (c->options & MIPS_CPU_FPU) {
		c->fpu_id = cpu_get_fpu_id();

		if (c->isa_level == MIPS_CPU_ISA_M32R1 ||
		    c->isa_level == MIPS_CPU_ISA_M32R2 ||
		    c->isa_level == MIPS_CPU_ISA_M64R1 ||
		    c->isa_level == MIPS_CPU_ISA_M64R2) {
			if (c->fpu_id & MIPS_FPIR_3D)
				c->ases |= MIPS_ASE_MIPS3D;
		}
	}

	if (cpu_has_mips_r2)
		c->srsets = ((read_c0_srsctl() >> 26) & 0x0f) + 1;
	else
		c->srsets = 1;

	cpu_probe_vmbits(c);
}

__cpuinit void cpu_report(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;

	printk(KERN_INFO "CPU revision is: %08x (%s)\n",
	       c->processor_id, cpu_name_string());
	if (c->options & MIPS_CPU_FPU)
		printk(KERN_INFO "FPU revision is: %08x\n", c->fpu_id);
}
