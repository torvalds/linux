// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Processor capabilities determination functions.
 *
 * Copyright (C) xxxx  the Anonymous
 * Copyright (C) 1994 - 2006 Ralf Baechle
 * Copyright (C) 2003, 2004  Maciej W. Rozycki
 * Copyright (C) 2001, 2004, 2011, 2012	 MIPS Technologies, Inc.
 */
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/ptrace.h>
#include <linux/smp.h>
#include <linux/stddef.h>
#include <linux/export.h>

#include <asm/bugs.h>
#include <asm/cpu.h>
#include <asm/cpu-features.h>
#include <asm/cpu-type.h>
#include <asm/fpu.h>
#include <asm/mipsregs.h>
#include <asm/mipsmtregs.h>
#include <asm/msa.h>
#include <asm/watch.h>
#include <asm/elf.h>
#include <asm/pgtable-bits.h>
#include <asm/spram.h>
#include <asm/traps.h>
#include <linux/uaccess.h>

#include "fpu-probe.h"

#include <asm/mach-loongson64/cpucfg-emul.h>

/* Hardware capabilities */
unsigned int elf_hwcap __read_mostly;
EXPORT_SYMBOL_GPL(elf_hwcap);

static inline unsigned long cpu_get_msa_id(void)
{
	unsigned long status, msa_id;

	status = read_c0_status();
	__enable_fpu(FPU_64BIT);
	enable_msa();
	msa_id = read_msa_ir();
	disable_msa();
	write_c0_status(status);
	return msa_id;
}

static int mips_dsp_disabled;

static int __init dsp_disable(char *s)
{
	cpu_data[0].ases &= ~(MIPS_ASE_DSP | MIPS_ASE_DSP2P);
	mips_dsp_disabled = 1;

	return 1;
}

__setup("nodsp", dsp_disable);

static int mips_htw_disabled;

static int __init htw_disable(char *s)
{
	mips_htw_disabled = 1;
	cpu_data[0].options &= ~MIPS_CPU_HTW;
	write_c0_pwctl(read_c0_pwctl() &
		       ~(1 << MIPS_PWCTL_PWEN_SHIFT));

	return 1;
}

__setup("nohtw", htw_disable);

static int mips_ftlb_disabled;
static int mips_has_ftlb_configured;

enum ftlb_flags {
	FTLB_EN		= 1 << 0,
	FTLB_SET_PROB	= 1 << 1,
};

static int set_ftlb_enable(struct cpuinfo_mips *c, enum ftlb_flags flags);

static int __init ftlb_disable(char *s)
{
	unsigned int config4, mmuextdef;

	/*
	 * If the core hasn't done any FTLB configuration, there is nothing
	 * for us to do here.
	 */
	if (!mips_has_ftlb_configured)
		return 1;

	/* Disable it in the boot cpu */
	if (set_ftlb_enable(&cpu_data[0], 0)) {
		pr_warn("Can't turn FTLB off\n");
		return 1;
	}

	config4 = read_c0_config4();

	/* Check that FTLB has been disabled */
	mmuextdef = config4 & MIPS_CONF4_MMUEXTDEF;
	/* MMUSIZEEXT == VTLB ON, FTLB OFF */
	if (mmuextdef == MIPS_CONF4_MMUEXTDEF_FTLBSIZEEXT) {
		/* This should never happen */
		pr_warn("FTLB could not be disabled!\n");
		return 1;
	}

	mips_ftlb_disabled = 1;
	mips_has_ftlb_configured = 0;

	/*
	 * noftlb is mainly used for debug purposes so print
	 * an informative message instead of using pr_debug()
	 */
	pr_info("FTLB has been disabled\n");

	/*
	 * Some of these bits are duplicated in the decode_config4.
	 * MIPS_CONF4_MMUEXTDEF_MMUSIZEEXT is the only possible case
	 * once FTLB has been disabled so undo what decode_config4 did.
	 */
	cpu_data[0].tlbsize -= cpu_data[0].tlbsizeftlbways *
			       cpu_data[0].tlbsizeftlbsets;
	cpu_data[0].tlbsizeftlbsets = 0;
	cpu_data[0].tlbsizeftlbways = 0;

	return 1;
}

__setup("noftlb", ftlb_disable);

/*
 * Check if the CPU has per tc perf counters
 */
static inline void cpu_set_mt_per_tc_perf(struct cpuinfo_mips *c)
{
	if (read_c0_config7() & MTI_CONF7_PTC)
		c->options |= MIPS_CPU_MT_PER_TC_PERF_COUNTERS;
}

static inline void check_errata(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;

	switch (current_cpu_type()) {
	case CPU_34K:
		/*
		 * Erratum "RPS May Cause Incorrect Instruction Execution"
		 * This code only handles VPE0, any SMP/RTOS code
		 * making use of VPE1 will be responsible for that VPE.
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

static inline void set_elf_platform(int cpu, const char *plat)
{
	if (cpu == 0)
		__elf_platform = plat;
}

static inline void set_elf_base_platform(const char *plat)
{
	if (__elf_base_platform == NULL) {
		__elf_base_platform = plat;
	}
}

static inline void cpu_probe_vmbits(struct cpuinfo_mips *c)
{
#ifdef __NEED_VMBITS_PROBE
	write_c0_entryhi(0x3fffffffffffe000ULL);
	back_to_back_c0_hazard();
	c->vmbits = fls64(read_c0_entryhi() & 0x3fffffffffffe000ULL);
#endif
}

static void set_isa(struct cpuinfo_mips *c, unsigned int isa)
{
	switch (isa) {
	case MIPS_CPU_ISA_M64R5:
		c->isa_level |= MIPS_CPU_ISA_M32R5 | MIPS_CPU_ISA_M64R5;
		set_elf_base_platform("mips64r5");
		fallthrough;
	case MIPS_CPU_ISA_M64R2:
		c->isa_level |= MIPS_CPU_ISA_M32R2 | MIPS_CPU_ISA_M64R2;
		set_elf_base_platform("mips64r2");
		fallthrough;
	case MIPS_CPU_ISA_M64R1:
		c->isa_level |= MIPS_CPU_ISA_M32R1 | MIPS_CPU_ISA_M64R1;
		set_elf_base_platform("mips64");
		fallthrough;
	case MIPS_CPU_ISA_V:
		c->isa_level |= MIPS_CPU_ISA_V;
		set_elf_base_platform("mips5");
		fallthrough;
	case MIPS_CPU_ISA_IV:
		c->isa_level |= MIPS_CPU_ISA_IV;
		set_elf_base_platform("mips4");
		fallthrough;
	case MIPS_CPU_ISA_III:
		c->isa_level |= MIPS_CPU_ISA_II | MIPS_CPU_ISA_III;
		set_elf_base_platform("mips3");
		break;

	/* R6 incompatible with everything else */
	case MIPS_CPU_ISA_M64R6:
		c->isa_level |= MIPS_CPU_ISA_M32R6 | MIPS_CPU_ISA_M64R6;
		set_elf_base_platform("mips64r6");
		fallthrough;
	case MIPS_CPU_ISA_M32R6:
		c->isa_level |= MIPS_CPU_ISA_M32R6;
		set_elf_base_platform("mips32r6");
		/* Break here so we don't add incompatible ISAs */
		break;
	case MIPS_CPU_ISA_M32R5:
		c->isa_level |= MIPS_CPU_ISA_M32R5;
		set_elf_base_platform("mips32r5");
		fallthrough;
	case MIPS_CPU_ISA_M32R2:
		c->isa_level |= MIPS_CPU_ISA_M32R2;
		set_elf_base_platform("mips32r2");
		fallthrough;
	case MIPS_CPU_ISA_M32R1:
		c->isa_level |= MIPS_CPU_ISA_M32R1;
		set_elf_base_platform("mips32");
		fallthrough;
	case MIPS_CPU_ISA_II:
		c->isa_level |= MIPS_CPU_ISA_II;
		set_elf_base_platform("mips2");
		break;
	}
}

static char unknown_isa[] = KERN_ERR \
	"Unsupported ISA type, c0.config0: %d.";

static unsigned int calculate_ftlb_probability(struct cpuinfo_mips *c)
{

	unsigned int probability = c->tlbsize / c->tlbsizevtlb;

	/*
	 * 0 = All TLBWR instructions go to FTLB
	 * 1 = 15:1: For every 16 TBLWR instructions, 15 go to the
	 * FTLB and 1 goes to the VTLB.
	 * 2 = 7:1: As above with 7:1 ratio.
	 * 3 = 3:1: As above with 3:1 ratio.
	 *
	 * Use the linear midpoint as the probability threshold.
	 */
	if (probability >= 12)
		return 1;
	else if (probability >= 6)
		return 2;
	else
		/*
		 * So FTLB is less than 4 times bigger than VTLB.
		 * A 3:1 ratio can still be useful though.
		 */
		return 3;
}

static int set_ftlb_enable(struct cpuinfo_mips *c, enum ftlb_flags flags)
{
	unsigned int config;

	/* It's implementation dependent how the FTLB can be enabled */
	switch (c->cputype) {
	case CPU_PROAPTIV:
	case CPU_P5600:
	case CPU_P6600:
		/* proAptiv & related cores use Config6 to enable the FTLB */
		config = read_c0_config6();

		if (flags & FTLB_EN)
			config |= MTI_CONF6_FTLBEN;
		else
			config &= ~MTI_CONF6_FTLBEN;

		if (flags & FTLB_SET_PROB) {
			config &= ~(3 << MTI_CONF6_FTLBP_SHIFT);
			config |= calculate_ftlb_probability(c)
				  << MTI_CONF6_FTLBP_SHIFT;
		}

		write_c0_config6(config);
		back_to_back_c0_hazard();
		break;
	case CPU_I6400:
	case CPU_I6500:
		/* There's no way to disable the FTLB */
		if (!(flags & FTLB_EN))
			return 1;
		return 0;
	case CPU_LOONGSON64:
		/* Flush ITLB, DTLB, VTLB and FTLB */
		write_c0_diag(LOONGSON_DIAG_ITLB | LOONGSON_DIAG_DTLB |
			      LOONGSON_DIAG_VTLB | LOONGSON_DIAG_FTLB);
		/* Loongson-3 cores use Config6 to enable the FTLB */
		config = read_c0_config6();
		if (flags & FTLB_EN)
			/* Enable FTLB */
			write_c0_config6(config & ~LOONGSON_CONF6_FTLBDIS);
		else
			/* Disable FTLB */
			write_c0_config6(config | LOONGSON_CONF6_FTLBDIS);
		break;
	default:
		return 1;
	}

	return 0;
}

static int mm_config(struct cpuinfo_mips *c)
{
	unsigned int config0, update, mm;

	config0 = read_c0_config();
	mm = config0 & MIPS_CONF_MM;

	/*
	 * It's implementation dependent what type of write-merge is supported
	 * and whether it can be enabled/disabled. If it is settable lets make
	 * the merging allowed by default. Some platforms might have
	 * write-through caching unsupported. In this case just ignore the
	 * CP0.Config.MM bit field value.
	 */
	switch (c->cputype) {
	case CPU_24K:
	case CPU_34K:
	case CPU_74K:
	case CPU_P5600:
	case CPU_P6600:
		c->options |= MIPS_CPU_MM_FULL;
		update = MIPS_CONF_MM_FULL;
		break;
	case CPU_1004K:
	case CPU_1074K:
	case CPU_INTERAPTIV:
	case CPU_PROAPTIV:
		mm = 0;
		fallthrough;
	default:
		update = 0;
		break;
	}

	if (update) {
		config0 = (config0 & ~MIPS_CONF_MM) | update;
		write_c0_config(config0);
	} else if (mm == MIPS_CONF_MM_SYSAD) {
		c->options |= MIPS_CPU_MM_SYSAD;
	} else if (mm == MIPS_CONF_MM_FULL) {
		c->options |= MIPS_CPU_MM_FULL;
	}

	return 0;
}

static inline unsigned int decode_config0(struct cpuinfo_mips *c)
{
	unsigned int config0;
	int isa, mt;

	config0 = read_c0_config();

	/*
	 * Look for Standard TLB or Dual VTLB and FTLB
	 */
	mt = config0 & MIPS_CONF_MT;
	if (mt == MIPS_CONF_MT_TLB)
		c->options |= MIPS_CPU_TLB;
	else if (mt == MIPS_CONF_MT_FTLB)
		c->options |= MIPS_CPU_TLB | MIPS_CPU_FTLB;

	isa = (config0 & MIPS_CONF_AT) >> 13;
	switch (isa) {
	case 0:
		switch ((config0 & MIPS_CONF_AR) >> 10) {
		case 0:
			set_isa(c, MIPS_CPU_ISA_M32R1);
			break;
		case 1:
			set_isa(c, MIPS_CPU_ISA_M32R2);
			break;
		case 2:
			set_isa(c, MIPS_CPU_ISA_M32R6);
			break;
		default:
			goto unknown;
		}
		break;
	case 2:
		switch ((config0 & MIPS_CONF_AR) >> 10) {
		case 0:
			set_isa(c, MIPS_CPU_ISA_M64R1);
			break;
		case 1:
			set_isa(c, MIPS_CPU_ISA_M64R2);
			break;
		case 2:
			set_isa(c, MIPS_CPU_ISA_M64R6);
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
	if (config1 & MIPS_CONF1_PC)
		c->options |= MIPS_CPU_PERF;
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
	if (cpu_has_tlb) {
		c->tlbsize = ((config1 & MIPS_CONF1_TLBS) >> 25) + 1;
		c->tlbsizevtlb = c->tlbsize;
		c->tlbsizeftlbsets = 0;
	}

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

	if (config3 & MIPS_CONF3_SM) {
		c->ases |= MIPS_ASE_SMARTMIPS;
		c->options |= MIPS_CPU_RIXI | MIPS_CPU_CTXTC;
	}
	if (config3 & MIPS_CONF3_RXI)
		c->options |= MIPS_CPU_RIXI;
	if (config3 & MIPS_CONF3_CTXTC)
		c->options |= MIPS_CPU_CTXTC;
	if (config3 & MIPS_CONF3_DSP)
		c->ases |= MIPS_ASE_DSP;
	if (config3 & MIPS_CONF3_DSP2P) {
		c->ases |= MIPS_ASE_DSP2P;
		if (cpu_has_mips_r6)
			c->ases |= MIPS_ASE_DSP3;
	}
	if (config3 & MIPS_CONF3_VINT)
		c->options |= MIPS_CPU_VINT;
	if (config3 & MIPS_CONF3_VEIC)
		c->options |= MIPS_CPU_VEIC;
	if (config3 & MIPS_CONF3_LPA)
		c->options |= MIPS_CPU_LPA;
	if (config3 & MIPS_CONF3_MT)
		c->ases |= MIPS_ASE_MIPSMT;
	if (config3 & MIPS_CONF3_ULRI)
		c->options |= MIPS_CPU_ULRI;
	if (config3 & MIPS_CONF3_ISA)
		c->options |= MIPS_CPU_MICROMIPS;
	if (config3 & MIPS_CONF3_VZ)
		c->ases |= MIPS_ASE_VZ;
	if (config3 & MIPS_CONF3_SC)
		c->options |= MIPS_CPU_SEGMENTS;
	if (config3 & MIPS_CONF3_BI)
		c->options |= MIPS_CPU_BADINSTR;
	if (config3 & MIPS_CONF3_BP)
		c->options |= MIPS_CPU_BADINSTRP;
	if (config3 & MIPS_CONF3_MSA)
		c->ases |= MIPS_ASE_MSA;
	if (config3 & MIPS_CONF3_PW) {
		c->htw_seq = 0;
		c->options |= MIPS_CPU_HTW;
	}
	if (config3 & MIPS_CONF3_CDMM)
		c->options |= MIPS_CPU_CDMM;
	if (config3 & MIPS_CONF3_SP)
		c->options |= MIPS_CPU_SP;

	return config3 & MIPS_CONF_M;
}

static inline unsigned int decode_config4(struct cpuinfo_mips *c)
{
	unsigned int config4;
	unsigned int newcf4;
	unsigned int mmuextdef;
	unsigned int ftlb_page = MIPS_CONF4_FTLBPAGESIZE;
	unsigned long asid_mask;

	config4 = read_c0_config4();

	if (cpu_has_tlb) {
		if (((config4 & MIPS_CONF4_IE) >> 29) == 2)
			c->options |= MIPS_CPU_TLBINV;

		/*
		 * R6 has dropped the MMUExtDef field from config4.
		 * On R6 the fields always describe the FTLB, and only if it is
		 * present according to Config.MT.
		 */
		if (!cpu_has_mips_r6)
			mmuextdef = config4 & MIPS_CONF4_MMUEXTDEF;
		else if (cpu_has_ftlb)
			mmuextdef = MIPS_CONF4_MMUEXTDEF_VTLBSIZEEXT;
		else
			mmuextdef = 0;

		switch (mmuextdef) {
		case MIPS_CONF4_MMUEXTDEF_MMUSIZEEXT:
			c->tlbsize += (config4 & MIPS_CONF4_MMUSIZEEXT) * 0x40;
			c->tlbsizevtlb = c->tlbsize;
			break;
		case MIPS_CONF4_MMUEXTDEF_VTLBSIZEEXT:
			c->tlbsizevtlb +=
				((config4 & MIPS_CONF4_VTLBSIZEEXT) >>
				  MIPS_CONF4_VTLBSIZEEXT_SHIFT) * 0x40;
			c->tlbsize = c->tlbsizevtlb;
			ftlb_page = MIPS_CONF4_VFTLBPAGESIZE;
			fallthrough;
		case MIPS_CONF4_MMUEXTDEF_FTLBSIZEEXT:
			if (mips_ftlb_disabled)
				break;
			newcf4 = (config4 & ~ftlb_page) |
				(page_size_ftlb(mmuextdef) <<
				 MIPS_CONF4_FTLBPAGESIZE_SHIFT);
			write_c0_config4(newcf4);
			back_to_back_c0_hazard();
			config4 = read_c0_config4();
			if (config4 != newcf4) {
				pr_err("PAGE_SIZE 0x%lx is not supported by FTLB (config4=0x%x)\n",
				       PAGE_SIZE, config4);
				/* Switch FTLB off */
				set_ftlb_enable(c, 0);
				mips_ftlb_disabled = 1;
				break;
			}
			c->tlbsizeftlbsets = 1 <<
				((config4 & MIPS_CONF4_FTLBSETS) >>
				 MIPS_CONF4_FTLBSETS_SHIFT);
			c->tlbsizeftlbways = ((config4 & MIPS_CONF4_FTLBWAYS) >>
					      MIPS_CONF4_FTLBWAYS_SHIFT) + 2;
			c->tlbsize += c->tlbsizeftlbways * c->tlbsizeftlbsets;
			mips_has_ftlb_configured = 1;
			break;
		}
	}

	c->kscratch_mask = (config4 & MIPS_CONF4_KSCREXIST)
				>> MIPS_CONF4_KSCREXIST_SHIFT;

	asid_mask = MIPS_ENTRYHI_ASID;
	if (config4 & MIPS_CONF4_AE)
		asid_mask |= MIPS_ENTRYHI_ASIDX;
	set_cpu_asid_mask(c, asid_mask);

	/*
	 * Warn if the computed ASID mask doesn't match the mask the kernel
	 * is built for. This may indicate either a serious problem or an
	 * easy optimisation opportunity, but either way should be addressed.
	 */
	WARN_ON(asid_mask != cpu_asid_mask(c));

	return config4 & MIPS_CONF_M;
}

static inline unsigned int decode_config5(struct cpuinfo_mips *c)
{
	unsigned int config5, max_mmid_width;
	unsigned long asid_mask;

	config5 = read_c0_config5();
	config5 &= ~(MIPS_CONF5_UFR | MIPS_CONF5_UFE);

	if (cpu_has_mips_r6) {
		if (!__builtin_constant_p(cpu_has_mmid) || cpu_has_mmid)
			config5 |= MIPS_CONF5_MI;
		else
			config5 &= ~MIPS_CONF5_MI;
	}

	write_c0_config5(config5);

	if (config5 & MIPS_CONF5_EVA)
		c->options |= MIPS_CPU_EVA;
	if (config5 & MIPS_CONF5_MRP)
		c->options |= MIPS_CPU_MAAR;
	if (config5 & MIPS_CONF5_LLB)
		c->options |= MIPS_CPU_RW_LLB;
	if (config5 & MIPS_CONF5_MVH)
		c->options |= MIPS_CPU_MVH;
	if (cpu_has_mips_r6 && (config5 & MIPS_CONF5_VP))
		c->options |= MIPS_CPU_VP;
	if (config5 & MIPS_CONF5_CA2)
		c->ases |= MIPS_ASE_MIPS16E2;

	if (config5 & MIPS_CONF5_CRCP)
		elf_hwcap |= HWCAP_MIPS_CRC32;

	if (cpu_has_mips_r6) {
		/* Ensure the write to config5 above takes effect */
		back_to_back_c0_hazard();

		/* Check whether we successfully enabled MMID support */
		config5 = read_c0_config5();
		if (config5 & MIPS_CONF5_MI)
			c->options |= MIPS_CPU_MMID;

		/*
		 * Warn if we've hardcoded cpu_has_mmid to a value unsuitable
		 * for the CPU we're running on, or if CPUs in an SMP system
		 * have inconsistent MMID support.
		 */
		WARN_ON(!!cpu_has_mmid != !!(config5 & MIPS_CONF5_MI));

		if (cpu_has_mmid) {
			write_c0_memorymapid(~0ul);
			back_to_back_c0_hazard();
			asid_mask = read_c0_memorymapid();

			/*
			 * We maintain a bitmap to track MMID allocation, and
			 * need a sensible upper bound on the size of that
			 * bitmap. The initial CPU with MMID support (I6500)
			 * supports 16 bit MMIDs, which gives us an 8KiB
			 * bitmap. The architecture recommends that hardware
			 * support 32 bit MMIDs, which would give us a 512MiB
			 * bitmap - that's too big in most cases.
			 *
			 * Cap MMID width at 16 bits for now & we can revisit
			 * this if & when hardware supports anything wider.
			 */
			max_mmid_width = 16;
			if (asid_mask > GENMASK(max_mmid_width - 1, 0)) {
				pr_info("Capping MMID width at %d bits",
					max_mmid_width);
				asid_mask = GENMASK(max_mmid_width - 1, 0);
			}

			set_cpu_asid_mask(c, asid_mask);
		}
	}

	return config5 & MIPS_CONF_M;
}

static void decode_configs(struct cpuinfo_mips *c)
{
	int ok;

	/* MIPS32 or MIPS64 compliant CPU.  */
	c->options = MIPS_CPU_4KEX | MIPS_CPU_4K_CACHE | MIPS_CPU_COUNTER |
		     MIPS_CPU_DIVEC | MIPS_CPU_LLSC | MIPS_CPU_MCHECK;

	c->scache.flags = MIPS_CACHE_NOT_PRESENT;

	/* Enable FTLB if present and not disabled */
	set_ftlb_enable(c, mips_ftlb_disabled ? 0 : FTLB_EN);

	ok = decode_config0(c);			/* Read Config registers.  */
	BUG_ON(!ok);				/* Arch spec violation!	 */
	if (ok)
		ok = decode_config1(c);
	if (ok)
		ok = decode_config2(c);
	if (ok)
		ok = decode_config3(c);
	if (ok)
		ok = decode_config4(c);
	if (ok)
		ok = decode_config5(c);

	/* Probe the EBase.WG bit */
	if (cpu_has_mips_r2_r6) {
		u64 ebase;
		unsigned int status;

		/* {read,write}_c0_ebase_64() may be UNDEFINED prior to r6 */
		ebase = cpu_has_mips64r6 ? read_c0_ebase_64()
					 : (s32)read_c0_ebase();
		if (ebase & MIPS_EBASE_WG) {
			/* WG bit already set, we can avoid the clumsy probe */
			c->options |= MIPS_CPU_EBASE_WG;
		} else {
			/* Its UNDEFINED to change EBase while BEV=0 */
			status = read_c0_status();
			write_c0_status(status | ST0_BEV);
			irq_enable_hazard();
			/*
			 * On pre-r6 cores, this may well clobber the upper bits
			 * of EBase. This is hard to avoid without potentially
			 * hitting UNDEFINED dm*c0 behaviour if EBase is 32-bit.
			 */
			if (cpu_has_mips64r6)
				write_c0_ebase_64(ebase | MIPS_EBASE_WG);
			else
				write_c0_ebase(ebase | MIPS_EBASE_WG);
			back_to_back_c0_hazard();
			/* Restore BEV */
			write_c0_status(status);
			if (read_c0_ebase() & MIPS_EBASE_WG) {
				c->options |= MIPS_CPU_EBASE_WG;
				write_c0_ebase(ebase);
			}
		}
	}

	/* configure the FTLB write probability */
	set_ftlb_enable(c, (mips_ftlb_disabled ? 0 : FTLB_EN) | FTLB_SET_PROB);

	mips_probe_watch_registers(c);

#ifndef CONFIG_MIPS_CPS
	if (cpu_has_mips_r2_r6) {
		unsigned int core;

		core = get_ebase_cpunum();
		if (cpu_has_mipsmt)
			core >>= fls(core_nvpes()) - 1;
		cpu_set_core(c, core);
	}
#endif
}

/*
 * Probe for certain guest capabilities by writing config bits and reading back.
 * Finally write back the original value.
 */
#define probe_gc0_config(name, maxconf, bits)				\
do {									\
	unsigned int tmp;						\
	tmp = read_gc0_##name();					\
	write_gc0_##name(tmp | (bits));					\
	back_to_back_c0_hazard();					\
	maxconf = read_gc0_##name();					\
	write_gc0_##name(tmp);						\
} while (0)

/*
 * Probe for dynamic guest capabilities by changing certain config bits and
 * reading back to see if they change. Finally write back the original value.
 */
#define probe_gc0_config_dyn(name, maxconf, dynconf, bits)		\
do {									\
	maxconf = read_gc0_##name();					\
	write_gc0_##name(maxconf ^ (bits));				\
	back_to_back_c0_hazard();					\
	dynconf = maxconf ^ read_gc0_##name();				\
	write_gc0_##name(maxconf);					\
	maxconf |= dynconf;						\
} while (0)

static inline unsigned int decode_guest_config0(struct cpuinfo_mips *c)
{
	unsigned int config0;

	probe_gc0_config(config, config0, MIPS_CONF_M);

	if (config0 & MIPS_CONF_M)
		c->guest.conf |= BIT(1);
	return config0 & MIPS_CONF_M;
}

static inline unsigned int decode_guest_config1(struct cpuinfo_mips *c)
{
	unsigned int config1, config1_dyn;

	probe_gc0_config_dyn(config1, config1, config1_dyn,
			     MIPS_CONF_M | MIPS_CONF1_PC | MIPS_CONF1_WR |
			     MIPS_CONF1_FP);

	if (config1 & MIPS_CONF1_FP)
		c->guest.options |= MIPS_CPU_FPU;
	if (config1_dyn & MIPS_CONF1_FP)
		c->guest.options_dyn |= MIPS_CPU_FPU;

	if (config1 & MIPS_CONF1_WR)
		c->guest.options |= MIPS_CPU_WATCH;
	if (config1_dyn & MIPS_CONF1_WR)
		c->guest.options_dyn |= MIPS_CPU_WATCH;

	if (config1 & MIPS_CONF1_PC)
		c->guest.options |= MIPS_CPU_PERF;
	if (config1_dyn & MIPS_CONF1_PC)
		c->guest.options_dyn |= MIPS_CPU_PERF;

	if (config1 & MIPS_CONF_M)
		c->guest.conf |= BIT(2);
	return config1 & MIPS_CONF_M;
}

static inline unsigned int decode_guest_config2(struct cpuinfo_mips *c)
{
	unsigned int config2;

	probe_gc0_config(config2, config2, MIPS_CONF_M);

	if (config2 & MIPS_CONF_M)
		c->guest.conf |= BIT(3);
	return config2 & MIPS_CONF_M;
}

static inline unsigned int decode_guest_config3(struct cpuinfo_mips *c)
{
	unsigned int config3, config3_dyn;

	probe_gc0_config_dyn(config3, config3, config3_dyn,
			     MIPS_CONF_M | MIPS_CONF3_MSA | MIPS_CONF3_ULRI |
			     MIPS_CONF3_CTXTC);

	if (config3 & MIPS_CONF3_CTXTC)
		c->guest.options |= MIPS_CPU_CTXTC;
	if (config3_dyn & MIPS_CONF3_CTXTC)
		c->guest.options_dyn |= MIPS_CPU_CTXTC;

	if (config3 & MIPS_CONF3_PW)
		c->guest.options |= MIPS_CPU_HTW;

	if (config3 & MIPS_CONF3_ULRI)
		c->guest.options |= MIPS_CPU_ULRI;

	if (config3 & MIPS_CONF3_SC)
		c->guest.options |= MIPS_CPU_SEGMENTS;

	if (config3 & MIPS_CONF3_BI)
		c->guest.options |= MIPS_CPU_BADINSTR;
	if (config3 & MIPS_CONF3_BP)
		c->guest.options |= MIPS_CPU_BADINSTRP;

	if (config3 & MIPS_CONF3_MSA)
		c->guest.ases |= MIPS_ASE_MSA;
	if (config3_dyn & MIPS_CONF3_MSA)
		c->guest.ases_dyn |= MIPS_ASE_MSA;

	if (config3 & MIPS_CONF_M)
		c->guest.conf |= BIT(4);
	return config3 & MIPS_CONF_M;
}

static inline unsigned int decode_guest_config4(struct cpuinfo_mips *c)
{
	unsigned int config4;

	probe_gc0_config(config4, config4,
			 MIPS_CONF_M | MIPS_CONF4_KSCREXIST);

	c->guest.kscratch_mask = (config4 & MIPS_CONF4_KSCREXIST)
				>> MIPS_CONF4_KSCREXIST_SHIFT;

	if (config4 & MIPS_CONF_M)
		c->guest.conf |= BIT(5);
	return config4 & MIPS_CONF_M;
}

static inline unsigned int decode_guest_config5(struct cpuinfo_mips *c)
{
	unsigned int config5, config5_dyn;

	probe_gc0_config_dyn(config5, config5, config5_dyn,
			 MIPS_CONF_M | MIPS_CONF5_MVH | MIPS_CONF5_MRP);

	if (config5 & MIPS_CONF5_MRP)
		c->guest.options |= MIPS_CPU_MAAR;
	if (config5_dyn & MIPS_CONF5_MRP)
		c->guest.options_dyn |= MIPS_CPU_MAAR;

	if (config5 & MIPS_CONF5_LLB)
		c->guest.options |= MIPS_CPU_RW_LLB;

	if (config5 & MIPS_CONF5_MVH)
		c->guest.options |= MIPS_CPU_MVH;

	if (config5 & MIPS_CONF_M)
		c->guest.conf |= BIT(6);
	return config5 & MIPS_CONF_M;
}

static inline void decode_guest_configs(struct cpuinfo_mips *c)
{
	unsigned int ok;

	ok = decode_guest_config0(c);
	if (ok)
		ok = decode_guest_config1(c);
	if (ok)
		ok = decode_guest_config2(c);
	if (ok)
		ok = decode_guest_config3(c);
	if (ok)
		ok = decode_guest_config4(c);
	if (ok)
		decode_guest_config5(c);
}

static inline void cpu_probe_guestctl0(struct cpuinfo_mips *c)
{
	unsigned int guestctl0, temp;

	guestctl0 = read_c0_guestctl0();

	if (guestctl0 & MIPS_GCTL0_G0E)
		c->options |= MIPS_CPU_GUESTCTL0EXT;
	if (guestctl0 & MIPS_GCTL0_G1)
		c->options |= MIPS_CPU_GUESTCTL1;
	if (guestctl0 & MIPS_GCTL0_G2)
		c->options |= MIPS_CPU_GUESTCTL2;
	if (!(guestctl0 & MIPS_GCTL0_RAD)) {
		c->options |= MIPS_CPU_GUESTID;

		/*
		 * Probe for Direct Root to Guest (DRG). Set GuestCtl1.RID = 0
		 * first, otherwise all data accesses will be fully virtualised
		 * as if they were performed by guest mode.
		 */
		write_c0_guestctl1(0);
		tlbw_use_hazard();

		write_c0_guestctl0(guestctl0 | MIPS_GCTL0_DRG);
		back_to_back_c0_hazard();
		temp = read_c0_guestctl0();

		if (temp & MIPS_GCTL0_DRG) {
			write_c0_guestctl0(guestctl0);
			c->options |= MIPS_CPU_DRG;
		}
	}
}

static inline void cpu_probe_guestctl1(struct cpuinfo_mips *c)
{
	if (cpu_has_guestid) {
		/* determine the number of bits of GuestID available */
		write_c0_guestctl1(MIPS_GCTL1_ID);
		back_to_back_c0_hazard();
		c->guestid_mask = (read_c0_guestctl1() & MIPS_GCTL1_ID)
						>> MIPS_GCTL1_ID_SHIFT;
		write_c0_guestctl1(0);
	}
}

static inline void cpu_probe_gtoffset(struct cpuinfo_mips *c)
{
	/* determine the number of bits of GTOffset available */
	write_c0_gtoffset(0xffffffff);
	back_to_back_c0_hazard();
	c->gtoffset_mask = read_c0_gtoffset();
	write_c0_gtoffset(0);
}

static inline void cpu_probe_vz(struct cpuinfo_mips *c)
{
	cpu_probe_guestctl0(c);
	if (cpu_has_guestctl1)
		cpu_probe_guestctl1(c);

	cpu_probe_gtoffset(c);

	decode_guest_configs(c);
}

#define R4K_OPTS (MIPS_CPU_TLB | MIPS_CPU_4KEX | MIPS_CPU_4K_CACHE \
		| MIPS_CPU_COUNTER)

static inline void cpu_probe_legacy(struct cpuinfo_mips *c, unsigned int cpu)
{
	switch (c->processor_id & PRID_IMP_MASK) {
	case PRID_IMP_R2000:
		c->cputype = CPU_R2000;
		__cpu_name[cpu] = "R2000";
		c->fpu_msk31 |= FPU_CSR_CONDX | FPU_CSR_FS;
		c->options = MIPS_CPU_TLB | MIPS_CPU_3K_CACHE |
			     MIPS_CPU_NOFPUEX;
		if (__cpu_has_fpu())
			c->options |= MIPS_CPU_FPU;
		c->tlbsize = 64;
		break;
	case PRID_IMP_R3000:
		if ((c->processor_id & PRID_REV_MASK) == PRID_REV_R3000A) {
			if (cpu_has_confreg()) {
				c->cputype = CPU_R3081E;
				__cpu_name[cpu] = "R3081";
			} else {
				c->cputype = CPU_R3000A;
				__cpu_name[cpu] = "R3000A";
			}
		} else {
			c->cputype = CPU_R3000;
			__cpu_name[cpu] = "R3000";
		}
		c->fpu_msk31 |= FPU_CSR_CONDX | FPU_CSR_FS;
		c->options = MIPS_CPU_TLB | MIPS_CPU_3K_CACHE |
			     MIPS_CPU_NOFPUEX;
		if (__cpu_has_fpu())
			c->options |= MIPS_CPU_FPU;
		c->tlbsize = 64;
		break;
	case PRID_IMP_R4000:
		if (read_c0_config() & CONF_SC) {
			if ((c->processor_id & PRID_REV_MASK) >=
			    PRID_REV_R4400) {
				c->cputype = CPU_R4400PC;
				__cpu_name[cpu] = "R4400PC";
			} else {
				c->cputype = CPU_R4000PC;
				__cpu_name[cpu] = "R4000PC";
			}
		} else {
			int cca = read_c0_config() & CONF_CM_CMASK;
			int mc;

			/*
			 * SC and MC versions can't be reliably told apart,
			 * but only the latter support coherent caching
			 * modes so assume the firmware has set the KSEG0
			 * coherency attribute reasonably (if uncached, we
			 * assume SC).
			 */
			switch (cca) {
			case CONF_CM_CACHABLE_CE:
			case CONF_CM_CACHABLE_COW:
			case CONF_CM_CACHABLE_CUW:
				mc = 1;
				break;
			default:
				mc = 0;
				break;
			}
			if ((c->processor_id & PRID_REV_MASK) >=
			    PRID_REV_R4400) {
				c->cputype = mc ? CPU_R4400MC : CPU_R4400SC;
				__cpu_name[cpu] = mc ? "R4400MC" : "R4400SC";
			} else {
				c->cputype = mc ? CPU_R4000MC : CPU_R4000SC;
				__cpu_name[cpu] = mc ? "R4000MC" : "R4000SC";
			}
		}

		set_isa(c, MIPS_CPU_ISA_III);
		c->fpu_msk31 |= FPU_CSR_CONDX;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_WATCH | MIPS_CPU_VCE |
			     MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_VR41XX:
		set_isa(c, MIPS_CPU_ISA_III);
		c->fpu_msk31 |= FPU_CSR_CONDX;
		c->options = R4K_OPTS;
		c->tlbsize = 32;
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
				c->options |= MIPS_CPU_LLSC;
				__cpu_name[cpu] = "NEC VR4133";
			}
			break;
		default:
			printk(KERN_INFO "Unexpected CPU of NEC VR4100 series\n");
			c->cputype = CPU_VR41XX;
			__cpu_name[cpu] = "NEC Vr41xx";
			break;
		}
		break;
	case PRID_IMP_R4300:
		c->cputype = CPU_R4300;
		__cpu_name[cpu] = "R4300";
		set_isa(c, MIPS_CPU_ISA_III);
		c->fpu_msk31 |= FPU_CSR_CONDX;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_LLSC;
		c->tlbsize = 32;
		break;
	case PRID_IMP_R4600:
		c->cputype = CPU_R4600;
		__cpu_name[cpu] = "R4600";
		set_isa(c, MIPS_CPU_ISA_III);
		c->fpu_msk31 |= FPU_CSR_CONDX;
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
		set_isa(c, MIPS_CPU_ISA_III);
		c->fpu_msk31 |= FPU_CSR_CONDX;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	#endif
	case PRID_IMP_R4700:
		c->cputype = CPU_R4700;
		__cpu_name[cpu] = "R4700";
		set_isa(c, MIPS_CPU_ISA_III);
		c->fpu_msk31 |= FPU_CSR_CONDX;
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_TX49:
		c->cputype = CPU_TX49XX;
		__cpu_name[cpu] = "R49XX";
		set_isa(c, MIPS_CPU_ISA_III);
		c->fpu_msk31 |= FPU_CSR_CONDX;
		c->options = R4K_OPTS | MIPS_CPU_LLSC;
		if (!(c->processor_id & 0x08))
			c->options |= MIPS_CPU_FPU | MIPS_CPU_32FPR;
		c->tlbsize = 48;
		break;
	case PRID_IMP_R5000:
		c->cputype = CPU_R5000;
		__cpu_name[cpu] = "R5000";
		set_isa(c, MIPS_CPU_ISA_IV);
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_R5500:
		c->cputype = CPU_R5500;
		__cpu_name[cpu] = "R5500";
		set_isa(c, MIPS_CPU_ISA_IV);
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_WATCH | MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_NEVADA:
		c->cputype = CPU_NEVADA;
		__cpu_name[cpu] = "Nevada";
		set_isa(c, MIPS_CPU_ISA_IV);
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_DIVEC | MIPS_CPU_LLSC;
		c->tlbsize = 48;
		break;
	case PRID_IMP_RM7000:
		c->cputype = CPU_RM7000;
		__cpu_name[cpu] = "RM7000";
		set_isa(c, MIPS_CPU_ISA_IV);
		c->options = R4K_OPTS | MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_LLSC;
		/*
		 * Undocumented RM7000:	 Bit 29 in the info register of
		 * the RM7000 v2.0 indicates if the TLB has 48 or 64
		 * entries.
		 *
		 * 29	   1 =>	   64 entry JTLB
		 *	   0 =>	   48 entry JTLB
		 */
		c->tlbsize = (read_c0_info() & (1 << 29)) ? 64 : 48;
		break;
	case PRID_IMP_R10000:
		c->cputype = CPU_R10000;
		__cpu_name[cpu] = "R10000";
		set_isa(c, MIPS_CPU_ISA_IV);
		c->options = MIPS_CPU_TLB | MIPS_CPU_4K_CACHE | MIPS_CPU_4KEX |
			     MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_COUNTER | MIPS_CPU_WATCH |
			     MIPS_CPU_LLSC;
		c->tlbsize = 64;
		break;
	case PRID_IMP_R12000:
		c->cputype = CPU_R12000;
		__cpu_name[cpu] = "R12000";
		set_isa(c, MIPS_CPU_ISA_IV);
		c->options = MIPS_CPU_TLB | MIPS_CPU_4K_CACHE | MIPS_CPU_4KEX |
			     MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_COUNTER | MIPS_CPU_WATCH |
			     MIPS_CPU_LLSC;
		c->tlbsize = 64;
		write_c0_r10k_diag(read_c0_r10k_diag() | R10K_DIAG_E_GHIST);
		break;
	case PRID_IMP_R14000:
		if (((c->processor_id >> 4) & 0x0f) > 2) {
			c->cputype = CPU_R16000;
			__cpu_name[cpu] = "R16000";
		} else {
			c->cputype = CPU_R14000;
			__cpu_name[cpu] = "R14000";
		}
		set_isa(c, MIPS_CPU_ISA_IV);
		c->options = MIPS_CPU_TLB | MIPS_CPU_4K_CACHE | MIPS_CPU_4KEX |
			     MIPS_CPU_FPU | MIPS_CPU_32FPR |
			     MIPS_CPU_COUNTER | MIPS_CPU_WATCH |
			     MIPS_CPU_LLSC;
		c->tlbsize = 64;
		write_c0_r10k_diag(read_c0_r10k_diag() | R10K_DIAG_E_GHIST);
		break;
	case PRID_IMP_LOONGSON_64C:  /* Loongson-2/3 */
		switch (c->processor_id & PRID_REV_MASK) {
		case PRID_REV_LOONGSON2E:
			c->cputype = CPU_LOONGSON2EF;
			__cpu_name[cpu] = "ICT Loongson-2";
			set_elf_platform(cpu, "loongson2e");
			set_isa(c, MIPS_CPU_ISA_III);
			c->fpu_msk31 |= FPU_CSR_CONDX;
			break;
		case PRID_REV_LOONGSON2F:
			c->cputype = CPU_LOONGSON2EF;
			__cpu_name[cpu] = "ICT Loongson-2";
			set_elf_platform(cpu, "loongson2f");
			set_isa(c, MIPS_CPU_ISA_III);
			c->fpu_msk31 |= FPU_CSR_CONDX;
			break;
		case PRID_REV_LOONGSON3A_R1:
			c->cputype = CPU_LOONGSON64;
			__cpu_name[cpu] = "ICT Loongson-3";
			set_elf_platform(cpu, "loongson3a");
			set_isa(c, MIPS_CPU_ISA_M64R1);
			c->ases |= (MIPS_ASE_LOONGSON_MMI | MIPS_ASE_LOONGSON_CAM |
				MIPS_ASE_LOONGSON_EXT);
			break;
		case PRID_REV_LOONGSON3B_R1:
		case PRID_REV_LOONGSON3B_R2:
			c->cputype = CPU_LOONGSON64;
			__cpu_name[cpu] = "ICT Loongson-3";
			set_elf_platform(cpu, "loongson3b");
			set_isa(c, MIPS_CPU_ISA_M64R1);
			c->ases |= (MIPS_ASE_LOONGSON_MMI | MIPS_ASE_LOONGSON_CAM |
				MIPS_ASE_LOONGSON_EXT);
			break;
		}

		c->options = R4K_OPTS |
			     MIPS_CPU_FPU | MIPS_CPU_LLSC |
			     MIPS_CPU_32FPR;
		c->tlbsize = 64;
		set_cpu_asid_mask(c, MIPS_ENTRYHI_ASID);
		c->writecombine = _CACHE_UNCACHED_ACCELERATED;
		break;
	case PRID_IMP_LOONGSON_32:  /* Loongson-1 */
		decode_configs(c);

		c->cputype = CPU_LOONGSON32;

		switch (c->processor_id & PRID_REV_MASK) {
		case PRID_REV_LOONGSON1B:
			__cpu_name[cpu] = "Loongson 1B";
			break;
		}

		break;
	}
}

static inline void cpu_probe_mips(struct cpuinfo_mips *c, unsigned int cpu)
{
	c->writecombine = _CACHE_UNCACHED_ACCELERATED;
	switch (c->processor_id & PRID_IMP_MASK) {
	case PRID_IMP_QEMU_GENERIC:
		c->writecombine = _CACHE_UNCACHED;
		c->cputype = CPU_QEMU_GENERIC;
		__cpu_name[cpu] = "MIPS GENERIC QEMU";
		break;
	case PRID_IMP_4KC:
		c->cputype = CPU_4KC;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS 4Kc";
		break;
	case PRID_IMP_4KEC:
	case PRID_IMP_4KECR2:
		c->cputype = CPU_4KEC;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS 4KEc";
		break;
	case PRID_IMP_4KSC:
	case PRID_IMP_4KSD:
		c->cputype = CPU_4KSC;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS 4KSc";
		break;
	case PRID_IMP_5KC:
		c->cputype = CPU_5KC;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS 5Kc";
		break;
	case PRID_IMP_5KE:
		c->cputype = CPU_5KE;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS 5KE";
		break;
	case PRID_IMP_20KC:
		c->cputype = CPU_20KC;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS 20Kc";
		break;
	case PRID_IMP_24K:
		c->cputype = CPU_24K;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS 24Kc";
		break;
	case PRID_IMP_24KE:
		c->cputype = CPU_24K;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS 24KEc";
		break;
	case PRID_IMP_25KF:
		c->cputype = CPU_25KF;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS 25Kc";
		break;
	case PRID_IMP_34K:
		c->cputype = CPU_34K;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS 34Kc";
		cpu_set_mt_per_tc_perf(c);
		break;
	case PRID_IMP_74K:
		c->cputype = CPU_74K;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS 74Kc";
		break;
	case PRID_IMP_M14KC:
		c->cputype = CPU_M14KC;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS M14Kc";
		break;
	case PRID_IMP_M14KEC:
		c->cputype = CPU_M14KEC;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS M14KEc";
		break;
	case PRID_IMP_1004K:
		c->cputype = CPU_1004K;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS 1004Kc";
		cpu_set_mt_per_tc_perf(c);
		break;
	case PRID_IMP_1074K:
		c->cputype = CPU_1074K;
		c->writecombine = _CACHE_UNCACHED;
		__cpu_name[cpu] = "MIPS 1074Kc";
		break;
	case PRID_IMP_INTERAPTIV_UP:
		c->cputype = CPU_INTERAPTIV;
		__cpu_name[cpu] = "MIPS interAptiv";
		cpu_set_mt_per_tc_perf(c);
		break;
	case PRID_IMP_INTERAPTIV_MP:
		c->cputype = CPU_INTERAPTIV;
		__cpu_name[cpu] = "MIPS interAptiv (multi)";
		cpu_set_mt_per_tc_perf(c);
		break;
	case PRID_IMP_PROAPTIV_UP:
		c->cputype = CPU_PROAPTIV;
		__cpu_name[cpu] = "MIPS proAptiv";
		break;
	case PRID_IMP_PROAPTIV_MP:
		c->cputype = CPU_PROAPTIV;
		__cpu_name[cpu] = "MIPS proAptiv (multi)";
		break;
	case PRID_IMP_P5600:
		c->cputype = CPU_P5600;
		__cpu_name[cpu] = "MIPS P5600";
		break;
	case PRID_IMP_P6600:
		c->cputype = CPU_P6600;
		__cpu_name[cpu] = "MIPS P6600";
		break;
	case PRID_IMP_I6400:
		c->cputype = CPU_I6400;
		__cpu_name[cpu] = "MIPS I6400";
		break;
	case PRID_IMP_I6500:
		c->cputype = CPU_I6500;
		__cpu_name[cpu] = "MIPS I6500";
		break;
	case PRID_IMP_M5150:
		c->cputype = CPU_M5150;
		__cpu_name[cpu] = "MIPS M5150";
		break;
	case PRID_IMP_M6250:
		c->cputype = CPU_M6250;
		__cpu_name[cpu] = "MIPS M6250";
		break;
	}

	decode_configs(c);

	spram_config();

	mm_config(c);

	switch (__get_cpu_type(c->cputype)) {
	case CPU_M5150:
	case CPU_P5600:
		set_isa(c, MIPS_CPU_ISA_M32R5);
		break;
	case CPU_I6500:
		c->options |= MIPS_CPU_SHARED_FTLB_ENTRIES;
		fallthrough;
	case CPU_I6400:
		c->options |= MIPS_CPU_SHARED_FTLB_RAM;
		fallthrough;
	default:
		break;
	}

	/* Recent MIPS cores use the implementation-dependent ExcCode 16 for
	 * cache/FTLB parity exceptions.
	 */
	switch (__get_cpu_type(c->cputype)) {
	case CPU_PROAPTIV:
	case CPU_P5600:
	case CPU_P6600:
	case CPU_I6400:
	case CPU_I6500:
		c->options |= MIPS_CPU_FTLBPAREX;
		break;
	}
}

static inline void cpu_probe_alchemy(struct cpuinfo_mips *c, unsigned int cpu)
{
	decode_configs(c);
	switch (c->processor_id & PRID_IMP_MASK) {
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
			if ((c->processor_id & PRID_REV_MASK) == 2)
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

	c->writecombine = _CACHE_UNCACHED_ACCELERATED;
	switch (c->processor_id & PRID_IMP_MASK) {
	case PRID_IMP_SB1:
		c->cputype = CPU_SB1;
		__cpu_name[cpu] = "SiByte SB1";
		/* FPU in pass1 is known to have issues. */
		if ((c->processor_id & PRID_REV_MASK) < 0x02)
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
	switch (c->processor_id & PRID_IMP_MASK) {
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
	switch (c->processor_id & PRID_IMP_MASK) {
	case PRID_IMP_PR4450:
		c->cputype = CPU_PR4450;
		__cpu_name[cpu] = "Philips PR4450";
		set_isa(c, MIPS_CPU_ISA_M32R1);
		break;
	}
}

static inline void cpu_probe_broadcom(struct cpuinfo_mips *c, unsigned int cpu)
{
	decode_configs(c);
	switch (c->processor_id & PRID_IMP_MASK) {
	case PRID_IMP_BMIPS32_REV4:
	case PRID_IMP_BMIPS32_REV8:
		c->cputype = CPU_BMIPS32;
		__cpu_name[cpu] = "Broadcom BMIPS32";
		set_elf_platform(cpu, "bmips32");
		break;
	case PRID_IMP_BMIPS3300:
	case PRID_IMP_BMIPS3300_ALT:
	case PRID_IMP_BMIPS3300_BUG:
		c->cputype = CPU_BMIPS3300;
		__cpu_name[cpu] = "Broadcom BMIPS3300";
		set_elf_platform(cpu, "bmips3300");
		reserve_exception_space(0x400, VECTORSPACING * 64);
		break;
	case PRID_IMP_BMIPS43XX: {
		int rev = c->processor_id & PRID_REV_MASK;

		if (rev >= PRID_REV_BMIPS4380_LO &&
				rev <= PRID_REV_BMIPS4380_HI) {
			c->cputype = CPU_BMIPS4380;
			__cpu_name[cpu] = "Broadcom BMIPS4380";
			set_elf_platform(cpu, "bmips4380");
			c->options |= MIPS_CPU_RIXI;
			reserve_exception_space(0x400, VECTORSPACING * 64);
		} else {
			c->cputype = CPU_BMIPS4350;
			__cpu_name[cpu] = "Broadcom BMIPS4350";
			set_elf_platform(cpu, "bmips4350");
		}
		break;
	}
	case PRID_IMP_BMIPS5000:
	case PRID_IMP_BMIPS5200:
		c->cputype = CPU_BMIPS5000;
		if ((c->processor_id & PRID_IMP_MASK) == PRID_IMP_BMIPS5200)
			__cpu_name[cpu] = "Broadcom BMIPS5200";
		else
			__cpu_name[cpu] = "Broadcom BMIPS5000";
		set_elf_platform(cpu, "bmips5000");
		c->options |= MIPS_CPU_ULRI | MIPS_CPU_RIXI;
		reserve_exception_space(0x1000, VECTORSPACING * 64);
		break;
	}
}

static inline void cpu_probe_cavium(struct cpuinfo_mips *c, unsigned int cpu)
{
	decode_configs(c);
	switch (c->processor_id & PRID_IMP_MASK) {
	case PRID_IMP_CAVIUM_CN38XX:
	case PRID_IMP_CAVIUM_CN31XX:
	case PRID_IMP_CAVIUM_CN30XX:
		c->cputype = CPU_CAVIUM_OCTEON;
		__cpu_name[cpu] = "Cavium Octeon";
		goto platform;
	case PRID_IMP_CAVIUM_CN58XX:
	case PRID_IMP_CAVIUM_CN56XX:
	case PRID_IMP_CAVIUM_CN50XX:
	case PRID_IMP_CAVIUM_CN52XX:
		c->cputype = CPU_CAVIUM_OCTEON_PLUS;
		__cpu_name[cpu] = "Cavium Octeon+";
platform:
		set_elf_platform(cpu, "octeon");
		break;
	case PRID_IMP_CAVIUM_CN61XX:
	case PRID_IMP_CAVIUM_CN63XX:
	case PRID_IMP_CAVIUM_CN66XX:
	case PRID_IMP_CAVIUM_CN68XX:
	case PRID_IMP_CAVIUM_CNF71XX:
		c->cputype = CPU_CAVIUM_OCTEON2;
		__cpu_name[cpu] = "Cavium Octeon II";
		set_elf_platform(cpu, "octeon2");
		break;
	case PRID_IMP_CAVIUM_CN70XX:
	case PRID_IMP_CAVIUM_CN73XX:
	case PRID_IMP_CAVIUM_CNF75XX:
	case PRID_IMP_CAVIUM_CN78XX:
		c->cputype = CPU_CAVIUM_OCTEON3;
		__cpu_name[cpu] = "Cavium Octeon III";
		set_elf_platform(cpu, "octeon3");
		break;
	default:
		printk(KERN_INFO "Unknown Octeon chip!\n");
		c->cputype = CPU_UNKNOWN;
		break;
	}
}

#ifdef CONFIG_CPU_LOONGSON64
#include <loongson_regs.h>

static inline void decode_cpucfg(struct cpuinfo_mips *c)
{
	u32 cfg1 = read_cpucfg(LOONGSON_CFG1);
	u32 cfg2 = read_cpucfg(LOONGSON_CFG2);
	u32 cfg3 = read_cpucfg(LOONGSON_CFG3);

	if (cfg1 & LOONGSON_CFG1_MMI)
		c->ases |= MIPS_ASE_LOONGSON_MMI;

	if (cfg2 & LOONGSON_CFG2_LEXT1)
		c->ases |= MIPS_ASE_LOONGSON_EXT;

	if (cfg2 & LOONGSON_CFG2_LEXT2)
		c->ases |= MIPS_ASE_LOONGSON_EXT2;

	if (cfg2 & LOONGSON_CFG2_LSPW) {
		c->options |= MIPS_CPU_LDPTE;
		c->guest.options |= MIPS_CPU_LDPTE;
	}

	if (cfg3 & LOONGSON_CFG3_LCAMP)
		c->ases |= MIPS_ASE_LOONGSON_CAM;
}

static inline void cpu_probe_loongson(struct cpuinfo_mips *c, unsigned int cpu)
{
	/* All Loongson processors covered here define ExcCode 16 as GSExc. */
	c->options |= MIPS_CPU_GSEXCEX;

	switch (c->processor_id & PRID_IMP_MASK) {
	case PRID_IMP_LOONGSON_64R: /* Loongson-64 Reduced */
		switch (c->processor_id & PRID_REV_MASK) {
		case PRID_REV_LOONGSON2K_R1_0:
		case PRID_REV_LOONGSON2K_R1_1:
		case PRID_REV_LOONGSON2K_R1_2:
		case PRID_REV_LOONGSON2K_R1_3:
			c->cputype = CPU_LOONGSON64;
			__cpu_name[cpu] = "Loongson-2K";
			set_elf_platform(cpu, "gs264e");
			set_isa(c, MIPS_CPU_ISA_M64R2);
			break;
		}
		c->ases |= (MIPS_ASE_LOONGSON_MMI | MIPS_ASE_LOONGSON_EXT |
				MIPS_ASE_LOONGSON_EXT2);
		break;
	case PRID_IMP_LOONGSON_64C:  /* Loongson-3 Classic */
		switch (c->processor_id & PRID_REV_MASK) {
		case PRID_REV_LOONGSON3A_R2_0:
		case PRID_REV_LOONGSON3A_R2_1:
			c->cputype = CPU_LOONGSON64;
			__cpu_name[cpu] = "ICT Loongson-3";
			set_elf_platform(cpu, "loongson3a");
			set_isa(c, MIPS_CPU_ISA_M64R2);
			break;
		case PRID_REV_LOONGSON3A_R3_0:
		case PRID_REV_LOONGSON3A_R3_1:
			c->cputype = CPU_LOONGSON64;
			__cpu_name[cpu] = "ICT Loongson-3";
			set_elf_platform(cpu, "loongson3a");
			set_isa(c, MIPS_CPU_ISA_M64R2);
			break;
		}
		/*
		 * Loongson-3 Classic did not implement MIPS standard TLBINV
		 * but implemented TLBINVF and EHINV. As currently we're only
		 * using these two features, enable MIPS_CPU_TLBINV as well.
		 *
		 * Also some early Loongson-3A2000 had wrong TLB type in Config
		 * register, we correct it here.
		 */
		c->options |= MIPS_CPU_FTLB | MIPS_CPU_TLBINV | MIPS_CPU_LDPTE;
		c->ases |= (MIPS_ASE_LOONGSON_MMI | MIPS_ASE_LOONGSON_CAM |
			MIPS_ASE_LOONGSON_EXT | MIPS_ASE_LOONGSON_EXT2);
		c->ases &= ~MIPS_ASE_VZ; /* VZ of Loongson-3A2000/3000 is incomplete */
		break;
	case PRID_IMP_LOONGSON_64G:
		c->cputype = CPU_LOONGSON64;
		__cpu_name[cpu] = "ICT Loongson-3";
		set_elf_platform(cpu, "loongson3a");
		set_isa(c, MIPS_CPU_ISA_M64R2);
		decode_cpucfg(c);
		break;
	default:
		panic("Unknown Loongson Processor ID!");
		break;
	}

	decode_configs(c);
}
#else
static inline void cpu_probe_loongson(struct cpuinfo_mips *c, unsigned int cpu) { }
#endif

static inline void cpu_probe_ingenic(struct cpuinfo_mips *c, unsigned int cpu)
{
	decode_configs(c);

	/*
	 * XBurst misses a config2 register, so config3 decode was skipped in
	 * decode_configs().
	 */
	decode_config3(c);

	/* XBurst does not implement the CP0 counter. */
	c->options &= ~MIPS_CPU_COUNTER;
	BUG_ON(__builtin_constant_p(cpu_has_counter) && cpu_has_counter);

	/* XBurst has virtually tagged icache */
	c->icache.flags |= MIPS_CACHE_VTAG;

	switch (c->processor_id & PRID_IMP_MASK) {

	/* XBurst®1 with MXU1.0/MXU1.1 SIMD ISA */
	case PRID_IMP_XBURST_REV1:

		/*
		 * The XBurst core by default attempts to avoid branch target
		 * buffer lookups by detecting & special casing loops. This
		 * feature will cause BogoMIPS and lpj calculate in error.
		 * Set cp0 config7 bit 4 to disable this feature.
		 */
		set_c0_config7(MIPS_CONF7_BTB_LOOP_EN);

		switch (c->processor_id & PRID_COMP_MASK) {

		/*
		 * The config0 register in the XBurst CPUs with a processor ID of
		 * PRID_COMP_INGENIC_D0 report themselves as MIPS32r2 compatible,
		 * but they don't actually support this ISA.
		 */
		case PRID_COMP_INGENIC_D0:
			c->isa_level &= ~MIPS_CPU_ISA_M32R2;

			/* FPU is not properly detected on JZ4760(B). */
			if (c->processor_id == 0x2ed0024f)
				c->options |= MIPS_CPU_FPU;

			fallthrough;

		/*
		 * The config0 register in the XBurst CPUs with a processor ID of
		 * PRID_COMP_INGENIC_D0 or PRID_COMP_INGENIC_D1 has an abandoned
		 * huge page tlb mode, this mode is not compatible with the MIPS
		 * standard, it will cause tlbmiss and into an infinite loop
		 * (line 21 in the tlb-funcs.S) when starting the init process.
		 * After chip reset, the default is HPTLB mode, Write 0xa9000000
		 * to cp0 register 5 sel 4 to switch back to VTLB mode to prevent
		 * getting stuck.
		 */
		case PRID_COMP_INGENIC_D1:
			write_c0_page_ctrl(XBURST_PAGECTRL_HPTLB_DIS);
			break;

		default:
			break;
		}
		fallthrough;

	/* XBurst®1 with MXU2.0 SIMD ISA */
	case PRID_IMP_XBURST_REV2:
		/* Ingenic uses the WA bit to achieve write-combine memory writes */
		c->writecombine = _CACHE_CACHABLE_WA;
		c->cputype = CPU_XBURST;
		__cpu_name[cpu] = "Ingenic XBurst";
		break;

	/* XBurst®2 with MXU2.1 SIMD ISA */
	case PRID_IMP_XBURST2:
		c->cputype = CPU_XBURST;
		__cpu_name[cpu] = "Ingenic XBurst II";
		break;

	default:
		panic("Unknown Ingenic Processor ID!");
		break;
	}
}

#ifdef CONFIG_64BIT
/* For use by uaccess.h */
u64 __ua_limit;
EXPORT_SYMBOL(__ua_limit);
#endif

const char *__cpu_name[NR_CPUS];
const char *__elf_platform;
const char *__elf_base_platform;

void cpu_probe(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;
	unsigned int cpu = smp_processor_id();

	/*
	 * Set a default elf platform, cpu probe may later
	 * overwrite it with a more precise value
	 */
	set_elf_platform(cpu, "mips");

	c->processor_id = PRID_IMP_UNKNOWN;
	c->fpu_id	= FPIR_IMP_NONE;
	c->cputype	= CPU_UNKNOWN;
	c->writecombine = _CACHE_UNCACHED;

	c->fpu_csr31	= FPU_CSR_RN;
	c->fpu_msk31	= FPU_CSR_RSVD | FPU_CSR_ABS2008 | FPU_CSR_NAN2008;

	c->processor_id = read_c0_prid();
	switch (c->processor_id & PRID_COMP_MASK) {
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
	case PRID_COMP_LOONGSON:
		cpu_probe_loongson(c, cpu);
		break;
	case PRID_COMP_INGENIC_13:
	case PRID_COMP_INGENIC_D0:
	case PRID_COMP_INGENIC_D1:
	case PRID_COMP_INGENIC_E1:
		cpu_probe_ingenic(c, cpu);
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

	if (cpu_has_rixi) {
		/* Enable the RIXI exceptions */
		set_c0_pagegrain(PG_IEC);
		back_to_back_c0_hazard();
		/* Verify the IEC bit is set */
		if (read_c0_pagegrain() & PG_IEC)
			c->options |= MIPS_CPU_RIXIEX;
	}

	if (mips_fpu_disabled)
		c->options &= ~MIPS_CPU_FPU;

	if (mips_dsp_disabled)
		c->ases &= ~(MIPS_ASE_DSP | MIPS_ASE_DSP2P);

	if (mips_htw_disabled) {
		c->options &= ~MIPS_CPU_HTW;
		write_c0_pwctl(read_c0_pwctl() &
			       ~(1 << MIPS_PWCTL_PWEN_SHIFT));
	}

	if (c->options & MIPS_CPU_FPU)
		cpu_set_fpu_opts(c);
	else
		cpu_set_nofpu_opts(c);

	if (cpu_has_mips_r2_r6) {
		c->srsets = ((read_c0_srsctl() >> 26) & 0x0f) + 1;
		/* R2 has Performance Counter Interrupt indicator */
		c->options |= MIPS_CPU_PCI;
	}
	else
		c->srsets = 1;

	if (cpu_has_mips_r6)
		elf_hwcap |= HWCAP_MIPS_R6;

	if (cpu_has_msa) {
		c->msa_id = cpu_get_msa_id();
		WARN(c->msa_id & MSA_IR_WRPF,
		     "Vector register partitioning unimplemented!");
		elf_hwcap |= HWCAP_MIPS_MSA;
	}

	if (cpu_has_mips16)
		elf_hwcap |= HWCAP_MIPS_MIPS16;

	if (cpu_has_mdmx)
		elf_hwcap |= HWCAP_MIPS_MDMX;

	if (cpu_has_mips3d)
		elf_hwcap |= HWCAP_MIPS_MIPS3D;

	if (cpu_has_smartmips)
		elf_hwcap |= HWCAP_MIPS_SMARTMIPS;

	if (cpu_has_dsp)
		elf_hwcap |= HWCAP_MIPS_DSP;

	if (cpu_has_dsp2)
		elf_hwcap |= HWCAP_MIPS_DSP2;

	if (cpu_has_dsp3)
		elf_hwcap |= HWCAP_MIPS_DSP3;

	if (cpu_has_mips16e2)
		elf_hwcap |= HWCAP_MIPS_MIPS16E2;

	if (cpu_has_loongson_mmi)
		elf_hwcap |= HWCAP_LOONGSON_MMI;

	if (cpu_has_loongson_ext)
		elf_hwcap |= HWCAP_LOONGSON_EXT;

	if (cpu_has_loongson_ext2)
		elf_hwcap |= HWCAP_LOONGSON_EXT2;

	if (cpu_has_vz)
		cpu_probe_vz(c);

	cpu_probe_vmbits(c);

	/* Synthesize CPUCFG data if running on Loongson processors;
	 * no-op otherwise.
	 *
	 * This looks at previously probed features, so keep this at bottom.
	 */
	loongson3_cpucfg_synthesize_data(c);

#ifdef CONFIG_64BIT
	if (cpu == 0)
		__ua_limit = ~((1ull << cpu_vmbits) - 1);
#endif

	reserve_exception_space(0, 0x1000);
}

void cpu_report(void)
{
	struct cpuinfo_mips *c = &current_cpu_data;

	pr_info("CPU%d revision is: %08x (%s)\n",
		smp_processor_id(), c->processor_id, cpu_name_string());
	if (c->options & MIPS_CPU_FPU)
		printk(KERN_INFO "FPU revision is: %08x\n", c->fpu_id);
	if (cpu_has_msa)
		pr_info("MSA revision is: %08x\n", c->msa_id);
}

void cpu_set_cluster(struct cpuinfo_mips *cpuinfo, unsigned int cluster)
{
	/* Ensure the core number fits in the field */
	WARN_ON(cluster > (MIPS_GLOBALNUMBER_CLUSTER >>
			   MIPS_GLOBALNUMBER_CLUSTER_SHF));

	cpuinfo->globalnumber &= ~MIPS_GLOBALNUMBER_CLUSTER;
	cpuinfo->globalnumber |= cluster << MIPS_GLOBALNUMBER_CLUSTER_SHF;
}

void cpu_set_core(struct cpuinfo_mips *cpuinfo, unsigned int core)
{
	/* Ensure the core number fits in the field */
	WARN_ON(core > (MIPS_GLOBALNUMBER_CORE >> MIPS_GLOBALNUMBER_CORE_SHF));

	cpuinfo->globalnumber &= ~MIPS_GLOBALNUMBER_CORE;
	cpuinfo->globalnumber |= core << MIPS_GLOBALNUMBER_CORE_SHF;
}

void cpu_set_vpe_id(struct cpuinfo_mips *cpuinfo, unsigned int vpe)
{
	/* Ensure the VP(E) ID fits in the field */
	WARN_ON(vpe > (MIPS_GLOBALNUMBER_VP >> MIPS_GLOBALNUMBER_VP_SHF));

	/* Ensure we're not using VP(E)s without support */
	WARN_ON(vpe && !IS_ENABLED(CONFIG_MIPS_MT_SMP) &&
		!IS_ENABLED(CONFIG_CPU_MIPSR6));

	cpuinfo->globalnumber &= ~MIPS_GLOBALNUMBER_VP;
	cpuinfo->globalnumber |= vpe << MIPS_GLOBALNUMBER_VP_SHF;
}
