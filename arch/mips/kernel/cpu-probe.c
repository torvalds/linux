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
#include <linux/config.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/stddef.h>

#include <asm/cpu.h>
#include <asm/fpu.h>
#include <asm/mipsregs.h>
#include <asm/system.h>

/*
 * Not all of the MIPS CPUs have the "wait" instruction available. Moreover,
 * the implementation of the "wait" feature differs between CPU families. This
 * points to the function that implements CPU specific wait.
 * The wait instruction stops the pipeline and reduces the power consumption of
 * the CPU very much.
 */
void (*cpu_wait)(void) = NULL;

static void r3081_wait(void)
{
	unsigned long cfg = read_c0_conf();
	write_c0_conf(cfg | R30XX_CONF_HALT);
}

static void r39xx_wait(void)
{
	unsigned long cfg = read_c0_conf();
	write_c0_conf(cfg | TX39_CONF_HALT);
}

static void r4k_wait(void)
{
	__asm__(".set\tmips3\n\t"
		"wait\n\t"
		".set\tmips0");
}

/* The Au1xxx wait is available only if using 32khz counter or
 * external timer source, but specifically not CP0 Counter. */
int allow_au1k_wait;

static void au1k_wait(void)
{
	/* using the wait instruction makes CP0 counter unusable */
	__asm__(".set mips3\n\t"
		"cache 0x14, 0(%0)\n\t"
		"cache 0x14, 32(%0)\n\t"
		"sync\n\t"
		"nop\n\t"
		"wait\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		"nop\n\t"
		".set mips0\n\t"
		: : "r" (au1k_wait));
}

static int __initdata nowait = 0;

int __init wait_disable(char *s)
{
	nowait = 1;

	return 1;
}

__setup("nowait", wait_disable);

static inline void check_wait(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;

	printk("Checking for 'wait' instruction... ");
	if (nowait) {
		printk (" disabled.\n");
		return;
	}

	switch (c->cputype) {
	case CPU_R3081:
	case CPU_R3081E:
		cpu_wait = r3081_wait;
		printk(" available.\n");
		break;
	case CPU_TX3927:
		cpu_wait = r39xx_wait;
		printk(" available.\n");
		break;
	case CPU_R4200:
/*	case CPU_R4300: */
	case CPU_R4600:
	case CPU_R4640:
	case CPU_R4650:
	case CPU_R4700:
	case CPU_R5000:
	case CPU_NEVADA:
	case CPU_RM7000:
	case CPU_RM9000:
	case CPU_TX49XX:
	case CPU_4KC:
	case CPU_4KEC:
	case CPU_4KSC:
	case CPU_5KC:
/*	case CPU_20KC:*/
	case CPU_24K:
	case CPU_25KF:
	case CPU_34K:
	case CPU_74K:
 	case CPU_PR4450:
		cpu_wait = r4k_wait;
		printk(" available.\n");
		break;
	case CPU_AU1000:
	case CPU_AU1100:
	case CPU_AU1500:
	case CPU_AU1550:
	case CPU_AU1200:
		if (allow_au1k_wait) {
			cpu_wait = au1k_wait;
			printk(" available.\n");
		} else
			printk(" unavailable.\n");
		break;
	default:
		printk(" unavailable.\n");
		break;
	}
}

void __init check_bugs32(void)
{
	check_wait();
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

#define R4K_OPTS (MIPS_CPU_TLB | MIPS_CPU_4KEX | MIPS_CPU_4K_CACHE \
		| MIPS_CPU_COUNTER)

static inline void cpu_probe_legacy(struct cpuinfo_mips *c)
{
	switch (c->processor_id & 0xff00) {
	case PRID_IMP_R2000:
		c->cputype = CPU_R2000;
		c->isa_level = MIPS_CPU_ISA_I;
		c->options = MIPS_CPU_TLB | MIPS_CPU_3K_CACHE |
		             MIPS_CPU_NOFPUEX;
		if (__cpu_has_fpu())
			c->options |= MIPS_CPU_FPU;
		c->tlbsize = 64;
		break;
	case PRID_IMP_R3000:
		if ((c->processor_id & 0xff) == PRID_REV_R3000A)
			if (cpu_has_confreg())
				c->cputype = CPU_R3081E;
			else
				c->cputype = CPU_R3000A;
		else
			c->cputype = CPU_R3000;
		c->isa_level = MIPS_CPU_ISA_I;
		c->options = MIPS_CPU_TLB | MIPS_CPU_3K_CACHE |
		             MIPS_CPU_NOFPUEX;
		if (__cpu_has_fpu())
			c->options |= MIPS_CPU_FPU;
		c->tlbsize = 64;
		break;
	case PRID_IMP_R4000:
		if (read_c0_config() & CONF_SC) {
			if ((c->processor_id & 0xff) >= PRID_REV_R4400)
				c->cputype = CPU_R4400PC;
			else
				c->cputype = CPU_R4000PC;
		} else {
			if ((c->processor_id & 0xff) >= PRID_REV_R4400)
				c->cputype = CPU_R4400SC;
			else
				c->cputype = CPU_R4000SC;
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
			break;
		case PRID_REV_VR4121:
			c->cputype = CPU_VR4121;
			break;
		case PRID_REV_VR4122:
			if ((c->processor_id & 0xf) < 0x3)
				c->cputype = CPU_VR4122;
			else
				c->cputype = CPU_VR4181A;
			break;
		case PRID_REV_VR4130:
			if ((c->processor_id & 0xf) < 0x4)
				c->cputype = CPU_VR4131;
			else
				c->cputype = CPU_VR4133;
			break;
		default:
			printk(KERN_INFO "Unexpected CPU of NEC VR4100 series\n");
			c->cputype = CPU_VR41XX;
			break;
		}
		c->isa_level = MIPS_CPU_ISA_III;
		c->options = R4K_OPTS;
		c->tlbsize = 32;
		break;
	case PRID_IMP_R4300:
		c->cputype = CPU_R4300;
		c->isa_level = MIPS_CPU_ISA_III;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_LLSC;
		c->tlbsize = 32;
		break;
	case PRID_IMP_R4600:
		c->cputype = CPU_R4600;
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
			c->tlbsize = 64;
		} else {
			switch (c->processor_id & 0xff) {
			case PRID_REV_TX3912:
				c->cputype = CPU_TX3912;
				c->tlbsize = 32;
				break;
			case PRID_REV_TX3922:
				c->cputype = CPU_TX3922;
				c->tlbsize = 64;
				break;
			default:
				c->cputype = CPU_UNKNOWN;
				break;
			}
		}
		break;
	case PRID_IMP_R4700:
		c->cputype = CPU_R4700;
		c->isa_level = MIPS_CPU_ISA_III;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_TX49:
		c->cputype = CPU_TX49XX;
		c->isa_level = MIPS_CPU_ISA_III;
		c->options = R4K_OPTS | MIPS_CPU_LLSC;
		if (!(c->processor_id & 0x08))
			c->options |= MIPS_CPU_FPU | MIPS_CPU_32FPR;
		c->tlbsize = 48;
		break;
	case PRID_IMP_R5000:
		c->cputype = CPU_R5000;
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_R5432:
		c->cputype = CPU_R5432;
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_WATCH | MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_R5500:
		c->cputype = CPU_R5500;
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_WATCH | MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_NEVADA:
		c->cputype = CPU_NEVADA;
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_DIVEC | MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_R6000:
		c->cputype = CPU_R6000;
		c->isa_level = MIPS_CPU_ISA_II;
		c->options = MIPS_CPU_TLB | MIPS_CPU_FPU |
		             MIPS_CPU_LLSC;
		c->tlbsize = 32;
		break;
	case PRID_IMP_R6000A:
		c->cputype = CPU_R6000A;
		c->isa_level = MIPS_CPU_ISA_II;
		c->options = MIPS_CPU_TLB | MIPS_CPU_FPU |
		             MIPS_CPU_LLSC;
		c->tlbsize = 32;
		break;
	case PRID_IMP_RM7000:
		c->cputype = CPU_RM7000;
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
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = MIPS_CPU_TLB | MIPS_CPU_4KEX |
		             MIPS_CPU_FPU | MIPS_CPU_32FPR |
		             MIPS_CPU_LLSC;
		c->tlbsize = 384;      /* has weird TLB: 3-way x 128 */
		break;
	case PRID_IMP_R10000:
		c->cputype = CPU_R10000;
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = MIPS_CPU_TLB | MIPS_CPU_4K_CACHE | MIPS_CPU_4KEX |
		             MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_COUNTER | MIPS_CPU_WATCH |
		             MIPS_CPU_LLSC;
		c->tlbsize = 64;
		break;
	case PRID_IMP_R12000:
		c->cputype = CPU_R12000;
		c->isa_level = MIPS_CPU_ISA_IV;
		c->options = MIPS_CPU_TLB | MIPS_CPU_4K_CACHE | MIPS_CPU_4KEX |
		             MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_COUNTER | MIPS_CPU_WATCH |
		             MIPS_CPU_LLSC;
		c->tlbsize = 64;
		break;
	}
}

static char unknown_isa[] __initdata = KERN_ERR \
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
		switch ((config0 >> 10) & 7) {
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
		switch ((config0 >> 10) & 7) {
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

	return config3 & MIPS_CONF_M;
}

static inline void decode_configs(struct cpuinfo_mips *c)
{
	/* MIPS32 or MIPS64 compliant CPU.  */
	c->options = MIPS_CPU_4KEX | MIPS_CPU_4K_CACHE | MIPS_CPU_COUNTER |
	             MIPS_CPU_DIVEC | MIPS_CPU_LLSC | MIPS_CPU_MCHECK;

	c->scache.flags = MIPS_CACHE_NOT_PRESENT;

	/* Read Config registers.  */
	if (!decode_config0(c))
		return;			/* actually worth a panic() */
	if (!decode_config1(c))
		return;
	if (!decode_config2(c))
		return;
	if (!decode_config3(c))
		return;
}

static inline void cpu_probe_mips(struct cpuinfo_mips *c)
{
	decode_configs(c);
	switch (c->processor_id & 0xff00) {
	case PRID_IMP_4KC:
		c->cputype = CPU_4KC;
		break;
	case PRID_IMP_4KEC:
		c->cputype = CPU_4KEC;
		break;
	case PRID_IMP_4KECR2:
		c->cputype = CPU_4KEC;
		break;
	case PRID_IMP_4KSC:
	case PRID_IMP_4KSD:
		c->cputype = CPU_4KSC;
		break;
	case PRID_IMP_5KC:
		c->cputype = CPU_5KC;
		break;
	case PRID_IMP_20KC:
		c->cputype = CPU_20KC;
		break;
	case PRID_IMP_24K:
	case PRID_IMP_24KE:
		c->cputype = CPU_24K;
		break;
	case PRID_IMP_25KF:
		c->cputype = CPU_25KF;
		/* Probe for L2 cache */
		c->scache.flags &= ~MIPS_CACHE_NOT_PRESENT;
		break;
	case PRID_IMP_34K:
		c->cputype = CPU_34K;
		break;
	case PRID_IMP_74K:
		c->cputype = CPU_74K;
		break;
	}
}

static inline void cpu_probe_alchemy(struct cpuinfo_mips *c)
{
	decode_configs(c);
	switch (c->processor_id & 0xff00) {
	case PRID_IMP_AU1_REV1:
	case PRID_IMP_AU1_REV2:
		switch ((c->processor_id >> 24) & 0xff) {
		case 0:
			c->cputype = CPU_AU1000;
			break;
		case 1:
			c->cputype = CPU_AU1500;
			break;
		case 2:
			c->cputype = CPU_AU1100;
			break;
		case 3:
			c->cputype = CPU_AU1550;
			break;
		case 4:
			c->cputype = CPU_AU1200;
			break;
		default:
			panic("Unknown Au Core!");
			break;
		}
		break;
	}
}

static inline void cpu_probe_sibyte(struct cpuinfo_mips *c)
{
	decode_configs(c);

	/*
	 * For historical reasons the SB1 comes with it's own variant of
	 * cache code which eventually will be folded into c-r4k.c.  Until
	 * then we pretend it's got it's own cache architecture.
	 */
	c->options &= ~MIPS_CPU_4K_CACHE;
	c->options |= MIPS_CPU_SB1_CACHE;

	switch (c->processor_id & 0xff00) {
	case PRID_IMP_SB1:
		c->cputype = CPU_SB1;
		/* FPU in pass1 is known to have issues. */
		if ((c->processor_id & 0xff) < 0x20)
			c->options &= ~(MIPS_CPU_FPU | MIPS_CPU_32FPR);
		break;
	case PRID_IMP_SB1A:
		c->cputype = CPU_SB1A;
		break;
	}
}

static inline void cpu_probe_sandcraft(struct cpuinfo_mips *c)
{
	decode_configs(c);
	switch (c->processor_id & 0xff00) {
	case PRID_IMP_SR71000:
		c->cputype = CPU_SR71000;
		c->scache.ways = 8;
		c->tlbsize = 64;
		break;
	}
}

static inline void cpu_probe_philips(struct cpuinfo_mips *c)
{
	decode_configs(c);
	switch (c->processor_id & 0xff00) {
	case PRID_IMP_PR4450:
		c->cputype = CPU_PR4450;
		c->isa_level = MIPS_CPU_ISA_M32R1;
		break;
	default:
		panic("Unknown Philips Core!"); /* REVISIT: die? */
		break;
	}
}


__init void cpu_probe(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;

	c->processor_id	= PRID_IMP_UNKNOWN;
	c->fpu_id	= FPIR_IMP_NONE;
	c->cputype	= CPU_UNKNOWN;

	c->processor_id = read_c0_prid();
	switch (c->processor_id & 0xff0000) {
	case PRID_COMP_LEGACY:
		cpu_probe_legacy(c);
		break;
	case PRID_COMP_MIPS:
		cpu_probe_mips(c);
		break;
	case PRID_COMP_ALCHEMY:
		cpu_probe_alchemy(c);
		break;
	case PRID_COMP_SIBYTE:
		cpu_probe_sibyte(c);
		break;
	case PRID_COMP_SANDCRAFT:
		cpu_probe_sandcraft(c);
		break;
 	case PRID_COMP_PHILIPS:
		cpu_probe_philips(c);
		break;
	default:
		c->cputype = CPU_UNKNOWN;
	}
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
}

__init void cpu_report(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;

	printk("CPU revision is: %08x\n", c->processor_id);
	if (c->options & MIPS_CPU_FPU)
		printk("FPU revision is: %08x\n", c->fpu_id);
}
