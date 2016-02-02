/*
 * Contains CPU feature definitions
 *
 * Copyright (C) 2015 ARM Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#define pr_fmt(fmt) "CPU features: " fmt

#include <linux/bsearch.h>
#include <linux/sort.h>
#include <linux/types.h>
#include <asm/cpu.h>
#include <asm/cpufeature.h>
#include <asm/cpu_ops.h>
#include <asm/processor.h>
#include <asm/sysreg.h>

unsigned long elf_hwcap __read_mostly;
EXPORT_SYMBOL_GPL(elf_hwcap);

#ifdef CONFIG_COMPAT
#define COMPAT_ELF_HWCAP_DEFAULT	\
				(COMPAT_HWCAP_HALF|COMPAT_HWCAP_THUMB|\
				 COMPAT_HWCAP_FAST_MULT|COMPAT_HWCAP_EDSP|\
				 COMPAT_HWCAP_TLS|COMPAT_HWCAP_VFP|\
				 COMPAT_HWCAP_VFPv3|COMPAT_HWCAP_VFPv4|\
				 COMPAT_HWCAP_NEON|COMPAT_HWCAP_IDIV|\
				 COMPAT_HWCAP_LPAE)
unsigned int compat_elf_hwcap __read_mostly = COMPAT_ELF_HWCAP_DEFAULT;
unsigned int compat_elf_hwcap2 __read_mostly;
#endif

DECLARE_BITMAP(cpu_hwcaps, ARM64_NCAPS);

#define __ARM64_FTR_BITS(SIGNED, STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL) \
	{						\
		.sign = SIGNED,				\
		.strict = STRICT,			\
		.type = TYPE,				\
		.shift = SHIFT,				\
		.width = WIDTH,				\
		.safe_val = SAFE_VAL,			\
	}

/* Define a feature with signed values */
#define ARM64_FTR_BITS(STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL) \
	__ARM64_FTR_BITS(FTR_SIGNED, STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL)

/* Define a feature with unsigned value */
#define U_ARM64_FTR_BITS(STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL) \
	__ARM64_FTR_BITS(FTR_UNSIGNED, STRICT, TYPE, SHIFT, WIDTH, SAFE_VAL)

#define ARM64_FTR_END					\
	{						\
		.width = 0,				\
	}

static struct arm64_ftr_bits ftr_id_aa64isar0[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 32, 32, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64ISAR0_RDM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 24, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_ATOMICS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_CRC32_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_SHA2_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_SHA1_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64ISAR0_AES_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 4, 0),	/* RAZ */
	ARM64_FTR_END,
};

static struct arm64_ftr_bits ftr_id_aa64pfr0[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 32, 32, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 28, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64PFR0_GIC_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR0_ASIMD_SHIFT, 4, ID_AA64PFR0_ASIMD_NI),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64PFR0_FP_SHIFT, 4, ID_AA64PFR0_FP_NI),
	/* Linux doesn't care about the EL3 */
	ARM64_FTR_BITS(FTR_NONSTRICT, FTR_EXACT, ID_AA64PFR0_EL3_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64PFR0_EL2_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64PFR0_EL1_SHIFT, 4, ID_AA64PFR0_EL1_64BIT_ONLY),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64PFR0_EL0_SHIFT, 4, ID_AA64PFR0_EL0_64BIT_ONLY),
	ARM64_FTR_END,
};

static struct arm64_ftr_bits ftr_id_aa64mmfr0[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 32, 32, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR0_TGRAN4_SHIFT, 4, ID_AA64MMFR0_TGRAN4_NI),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR0_TGRAN64_SHIFT, 4, ID_AA64MMFR0_TGRAN64_NI),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR0_TGRAN16_SHIFT, 4, ID_AA64MMFR0_TGRAN16_NI),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR0_BIGENDEL0_SHIFT, 4, 0),
	/* Linux shouldn't care about secure memory */
	ARM64_FTR_BITS(FTR_NONSTRICT, FTR_EXACT, ID_AA64MMFR0_SNSMEM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR0_BIGENDEL_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR0_ASID_SHIFT, 4, 0),
	/*
	 * Differing PARange is fine as long as all peripherals and memory are mapped
	 * within the minimum PARange of all CPUs
	 */
	U_ARM64_FTR_BITS(FTR_NONSTRICT, FTR_LOWER_SAFE, ID_AA64MMFR0_PARANGE_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static struct arm64_ftr_bits ftr_id_aa64mmfr1[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 32, 32, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64MMFR1_PAN_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR1_LOR_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR1_HPD_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR1_VHE_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR1_VMIDBITS_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64MMFR1_HADBS_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static struct arm64_ftr_bits ftr_ctr[] = {
	U_ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 31, 1, 1),	/* RAO */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 28, 3, 0),
	U_ARM64_FTR_BITS(FTR_STRICT, FTR_HIGHER_SAFE, 24, 4, 0),	/* CWG */
	U_ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 20, 4, 0),	/* ERG */
	U_ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 16, 4, 1),	/* DminLine */
	/*
	 * Linux can handle differing I-cache policies. Userspace JITs will
	 * make use of *minLine
	 */
	U_ARM64_FTR_BITS(FTR_NONSTRICT, FTR_EXACT, 14, 2, 0),	/* L1Ip */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 4, 10, 0),	/* RAZ */
	U_ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 0, 4, 0),	/* IminLine */
	ARM64_FTR_END,
};

static struct arm64_ftr_bits ftr_id_mmfr0[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 28, 4, 0),	/* InnerShr */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 24, 4, 0),	/* FCSE */
	ARM64_FTR_BITS(FTR_NONSTRICT, FTR_LOWER_SAFE, 20, 4, 0),	/* AuxReg */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 16, 4, 0),	/* TCM */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 12, 4, 0),	/* ShareLvl */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 8, 4, 0),	/* OuterShr */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 4, 4, 0),	/* PMSA */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 4, 0),	/* VMSA */
	ARM64_FTR_END,
};

static struct arm64_ftr_bits ftr_id_aa64dfr0[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 32, 32, 0),
	U_ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64DFR0_CTX_CMPS_SHIFT, 4, 0),
	U_ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64DFR0_WRPS_SHIFT, 4, 0),
	U_ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, ID_AA64DFR0_BRPS_SHIFT, 4, 0),
	U_ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64DFR0_PMUVER_SHIFT, 4, 0),
	U_ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64DFR0_TRACEVER_SHIFT, 4, 0),
	U_ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_AA64DFR0_DEBUGVER_SHIFT, 4, 0x6),
	ARM64_FTR_END,
};

static struct arm64_ftr_bits ftr_mvfr2[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 8, 24, 0),	/* RAZ */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 4, 4, 0),		/* FPMisc */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 4, 0),		/* SIMDMisc */
	ARM64_FTR_END,
};

static struct arm64_ftr_bits ftr_dczid[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 5, 27, 0),	/* RAZ */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 4, 1, 1),		/* DZP */
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 0, 4, 0),	/* BS */
	ARM64_FTR_END,
};


static struct arm64_ftr_bits ftr_id_isar5[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_ISAR5_RDM_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 20, 4, 0),	/* RAZ */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_ISAR5_CRC32_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_ISAR5_SHA2_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_ISAR5_SHA1_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_ISAR5_AES_SHIFT, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, ID_ISAR5_SEVL_SHIFT, 4, 0),
	ARM64_FTR_END,
};

static struct arm64_ftr_bits ftr_id_mmfr4[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 8, 24, 0),	/* RAZ */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 4, 4, 0),		/* ac2 */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 4, 0),		/* RAZ */
	ARM64_FTR_END,
};

static struct arm64_ftr_bits ftr_id_pfr0[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 16, 16, 0),	/* RAZ */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 12, 4, 0),	/* State3 */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 8, 4, 0),		/* State2 */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 4, 4, 0),		/* State1 */
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 4, 0),		/* State0 */
	ARM64_FTR_END,
};

/*
 * Common ftr bits for a 32bit register with all hidden, strict
 * attributes, with 4bit feature fields and a default safe value of
 * 0. Covers the following 32bit registers:
 * id_isar[0-4], id_mmfr[1-3], id_pfr1, mvfr[0-1]
 */
static struct arm64_ftr_bits ftr_generic_32bits[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 28, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 24, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 20, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 16, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 12, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 8, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 4, 4, 0),
	ARM64_FTR_BITS(FTR_STRICT, FTR_LOWER_SAFE, 0, 4, 0),
	ARM64_FTR_END,
};

static struct arm64_ftr_bits ftr_generic[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 64, 0),
	ARM64_FTR_END,
};

static struct arm64_ftr_bits ftr_generic32[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 32, 0),
	ARM64_FTR_END,
};

static struct arm64_ftr_bits ftr_aa64raz[] = {
	ARM64_FTR_BITS(FTR_STRICT, FTR_EXACT, 0, 64, 0),
	ARM64_FTR_END,
};

#define ARM64_FTR_REG(id, table)		\
	{					\
		.sys_id = id,			\
		.name = #id,			\
		.ftr_bits = &((table)[0]),	\
	}

static struct arm64_ftr_reg arm64_ftr_regs[] = {

	/* Op1 = 0, CRn = 0, CRm = 1 */
	ARM64_FTR_REG(SYS_ID_PFR0_EL1, ftr_id_pfr0),
	ARM64_FTR_REG(SYS_ID_PFR1_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_DFR0_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_MMFR0_EL1, ftr_id_mmfr0),
	ARM64_FTR_REG(SYS_ID_MMFR1_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_MMFR2_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_MMFR3_EL1, ftr_generic_32bits),

	/* Op1 = 0, CRn = 0, CRm = 2 */
	ARM64_FTR_REG(SYS_ID_ISAR0_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_ISAR1_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_ISAR2_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_ISAR3_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_ISAR4_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_ID_ISAR5_EL1, ftr_id_isar5),
	ARM64_FTR_REG(SYS_ID_MMFR4_EL1, ftr_id_mmfr4),

	/* Op1 = 0, CRn = 0, CRm = 3 */
	ARM64_FTR_REG(SYS_MVFR0_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_MVFR1_EL1, ftr_generic_32bits),
	ARM64_FTR_REG(SYS_MVFR2_EL1, ftr_mvfr2),

	/* Op1 = 0, CRn = 0, CRm = 4 */
	ARM64_FTR_REG(SYS_ID_AA64PFR0_EL1, ftr_id_aa64pfr0),
	ARM64_FTR_REG(SYS_ID_AA64PFR1_EL1, ftr_aa64raz),

	/* Op1 = 0, CRn = 0, CRm = 5 */
	ARM64_FTR_REG(SYS_ID_AA64DFR0_EL1, ftr_id_aa64dfr0),
	ARM64_FTR_REG(SYS_ID_AA64DFR1_EL1, ftr_generic),

	/* Op1 = 0, CRn = 0, CRm = 6 */
	ARM64_FTR_REG(SYS_ID_AA64ISAR0_EL1, ftr_id_aa64isar0),
	ARM64_FTR_REG(SYS_ID_AA64ISAR1_EL1, ftr_aa64raz),

	/* Op1 = 0, CRn = 0, CRm = 7 */
	ARM64_FTR_REG(SYS_ID_AA64MMFR0_EL1, ftr_id_aa64mmfr0),
	ARM64_FTR_REG(SYS_ID_AA64MMFR1_EL1, ftr_id_aa64mmfr1),

	/* Op1 = 3, CRn = 0, CRm = 0 */
	ARM64_FTR_REG(SYS_CTR_EL0, ftr_ctr),
	ARM64_FTR_REG(SYS_DCZID_EL0, ftr_dczid),

	/* Op1 = 3, CRn = 14, CRm = 0 */
	ARM64_FTR_REG(SYS_CNTFRQ_EL0, ftr_generic32),
};

static int search_cmp_ftr_reg(const void *id, const void *regp)
{
	return (int)(unsigned long)id - (int)((const struct arm64_ftr_reg *)regp)->sys_id;
}

/*
 * get_arm64_ftr_reg - Lookup a feature register entry using its
 * sys_reg() encoding. With the array arm64_ftr_regs sorted in the
 * ascending order of sys_id , we use binary search to find a matching
 * entry.
 *
 * returns - Upon success,  matching ftr_reg entry for id.
 *         - NULL on failure. It is upto the caller to decide
 *	     the impact of a failure.
 */
static struct arm64_ftr_reg *get_arm64_ftr_reg(u32 sys_id)
{
	return bsearch((const void *)(unsigned long)sys_id,
			arm64_ftr_regs,
			ARRAY_SIZE(arm64_ftr_regs),
			sizeof(arm64_ftr_regs[0]),
			search_cmp_ftr_reg);
}

static u64 arm64_ftr_set_value(struct arm64_ftr_bits *ftrp, s64 reg, s64 ftr_val)
{
	u64 mask = arm64_ftr_mask(ftrp);

	reg &= ~mask;
	reg |= (ftr_val << ftrp->shift) & mask;
	return reg;
}

static s64 arm64_ftr_safe_value(struct arm64_ftr_bits *ftrp, s64 new, s64 cur)
{
	s64 ret = 0;

	switch (ftrp->type) {
	case FTR_EXACT:
		ret = ftrp->safe_val;
		break;
	case FTR_LOWER_SAFE:
		ret = new < cur ? new : cur;
		break;
	case FTR_HIGHER_SAFE:
		ret = new > cur ? new : cur;
		break;
	default:
		BUG();
	}

	return ret;
}

static int __init sort_cmp_ftr_regs(const void *a, const void *b)
{
	return ((const struct arm64_ftr_reg *)a)->sys_id -
		 ((const struct arm64_ftr_reg *)b)->sys_id;
}

static void __init swap_ftr_regs(void *a, void *b, int size)
{
	struct arm64_ftr_reg tmp = *(struct arm64_ftr_reg *)a;
	*(struct arm64_ftr_reg *)a = *(struct arm64_ftr_reg *)b;
	*(struct arm64_ftr_reg *)b = tmp;
}

static void __init sort_ftr_regs(void)
{
	/* Keep the array sorted so that we can do the binary search */
	sort(arm64_ftr_regs,
		ARRAY_SIZE(arm64_ftr_regs),
		sizeof(arm64_ftr_regs[0]),
		sort_cmp_ftr_regs,
		swap_ftr_regs);
}

/*
 * Initialise the CPU feature register from Boot CPU values.
 * Also initiliases the strict_mask for the register.
 */
static void __init init_cpu_ftr_reg(u32 sys_reg, u64 new)
{
	u64 val = 0;
	u64 strict_mask = ~0x0ULL;
	struct arm64_ftr_bits *ftrp;
	struct arm64_ftr_reg *reg = get_arm64_ftr_reg(sys_reg);

	BUG_ON(!reg);

	for (ftrp  = reg->ftr_bits; ftrp->width; ftrp++) {
		s64 ftr_new = arm64_ftr_value(ftrp, new);

		val = arm64_ftr_set_value(ftrp, val, ftr_new);
		if (!ftrp->strict)
			strict_mask &= ~arm64_ftr_mask(ftrp);
	}
	reg->sys_val = val;
	reg->strict_mask = strict_mask;
}

void __init init_cpu_features(struct cpuinfo_arm64 *info)
{
	/* Before we start using the tables, make sure it is sorted */
	sort_ftr_regs();

	init_cpu_ftr_reg(SYS_CTR_EL0, info->reg_ctr);
	init_cpu_ftr_reg(SYS_DCZID_EL0, info->reg_dczid);
	init_cpu_ftr_reg(SYS_CNTFRQ_EL0, info->reg_cntfrq);
	init_cpu_ftr_reg(SYS_ID_AA64DFR0_EL1, info->reg_id_aa64dfr0);
	init_cpu_ftr_reg(SYS_ID_AA64DFR1_EL1, info->reg_id_aa64dfr1);
	init_cpu_ftr_reg(SYS_ID_AA64ISAR0_EL1, info->reg_id_aa64isar0);
	init_cpu_ftr_reg(SYS_ID_AA64ISAR1_EL1, info->reg_id_aa64isar1);
	init_cpu_ftr_reg(SYS_ID_AA64MMFR0_EL1, info->reg_id_aa64mmfr0);
	init_cpu_ftr_reg(SYS_ID_AA64MMFR1_EL1, info->reg_id_aa64mmfr1);
	init_cpu_ftr_reg(SYS_ID_AA64PFR0_EL1, info->reg_id_aa64pfr0);
	init_cpu_ftr_reg(SYS_ID_AA64PFR1_EL1, info->reg_id_aa64pfr1);
	init_cpu_ftr_reg(SYS_ID_DFR0_EL1, info->reg_id_dfr0);
	init_cpu_ftr_reg(SYS_ID_ISAR0_EL1, info->reg_id_isar0);
	init_cpu_ftr_reg(SYS_ID_ISAR1_EL1, info->reg_id_isar1);
	init_cpu_ftr_reg(SYS_ID_ISAR2_EL1, info->reg_id_isar2);
	init_cpu_ftr_reg(SYS_ID_ISAR3_EL1, info->reg_id_isar3);
	init_cpu_ftr_reg(SYS_ID_ISAR4_EL1, info->reg_id_isar4);
	init_cpu_ftr_reg(SYS_ID_ISAR5_EL1, info->reg_id_isar5);
	init_cpu_ftr_reg(SYS_ID_MMFR0_EL1, info->reg_id_mmfr0);
	init_cpu_ftr_reg(SYS_ID_MMFR1_EL1, info->reg_id_mmfr1);
	init_cpu_ftr_reg(SYS_ID_MMFR2_EL1, info->reg_id_mmfr2);
	init_cpu_ftr_reg(SYS_ID_MMFR3_EL1, info->reg_id_mmfr3);
	init_cpu_ftr_reg(SYS_ID_PFR0_EL1, info->reg_id_pfr0);
	init_cpu_ftr_reg(SYS_ID_PFR1_EL1, info->reg_id_pfr1);
	init_cpu_ftr_reg(SYS_MVFR0_EL1, info->reg_mvfr0);
	init_cpu_ftr_reg(SYS_MVFR1_EL1, info->reg_mvfr1);
	init_cpu_ftr_reg(SYS_MVFR2_EL1, info->reg_mvfr2);
}

static void update_cpu_ftr_reg(struct arm64_ftr_reg *reg, u64 new)
{
	struct arm64_ftr_bits *ftrp;

	for (ftrp = reg->ftr_bits; ftrp->width; ftrp++) {
		s64 ftr_cur = arm64_ftr_value(ftrp, reg->sys_val);
		s64 ftr_new = arm64_ftr_value(ftrp, new);

		if (ftr_cur == ftr_new)
			continue;
		/* Find a safe value */
		ftr_new = arm64_ftr_safe_value(ftrp, ftr_new, ftr_cur);
		reg->sys_val = arm64_ftr_set_value(ftrp, reg->sys_val, ftr_new);
	}

}

static int check_update_ftr_reg(u32 sys_id, int cpu, u64 val, u64 boot)
{
	struct arm64_ftr_reg *regp = get_arm64_ftr_reg(sys_id);

	BUG_ON(!regp);
	update_cpu_ftr_reg(regp, val);
	if ((boot & regp->strict_mask) == (val & regp->strict_mask))
		return 0;
	pr_warn("SANITY CHECK: Unexpected variation in %s. Boot CPU: %#016llx, CPU%d: %#016llx\n",
			regp->name, boot, cpu, val);
	return 1;
}

/*
 * Update system wide CPU feature registers with the values from a
 * non-boot CPU. Also performs SANITY checks to make sure that there
 * aren't any insane variations from that of the boot CPU.
 */
void update_cpu_features(int cpu,
			 struct cpuinfo_arm64 *info,
			 struct cpuinfo_arm64 *boot)
{
	int taint = 0;

	/*
	 * The kernel can handle differing I-cache policies, but otherwise
	 * caches should look identical. Userspace JITs will make use of
	 * *minLine.
	 */
	taint |= check_update_ftr_reg(SYS_CTR_EL0, cpu,
				      info->reg_ctr, boot->reg_ctr);

	/*
	 * Userspace may perform DC ZVA instructions. Mismatched block sizes
	 * could result in too much or too little memory being zeroed if a
	 * process is preempted and migrated between CPUs.
	 */
	taint |= check_update_ftr_reg(SYS_DCZID_EL0, cpu,
				      info->reg_dczid, boot->reg_dczid);

	/* If different, timekeeping will be broken (especially with KVM) */
	taint |= check_update_ftr_reg(SYS_CNTFRQ_EL0, cpu,
				      info->reg_cntfrq, boot->reg_cntfrq);

	/*
	 * The kernel uses self-hosted debug features and expects CPUs to
	 * support identical debug features. We presently need CTX_CMPs, WRPs,
	 * and BRPs to be identical.
	 * ID_AA64DFR1 is currently RES0.
	 */
	taint |= check_update_ftr_reg(SYS_ID_AA64DFR0_EL1, cpu,
				      info->reg_id_aa64dfr0, boot->reg_id_aa64dfr0);
	taint |= check_update_ftr_reg(SYS_ID_AA64DFR1_EL1, cpu,
				      info->reg_id_aa64dfr1, boot->reg_id_aa64dfr1);
	/*
	 * Even in big.LITTLE, processors should be identical instruction-set
	 * wise.
	 */
	taint |= check_update_ftr_reg(SYS_ID_AA64ISAR0_EL1, cpu,
				      info->reg_id_aa64isar0, boot->reg_id_aa64isar0);
	taint |= check_update_ftr_reg(SYS_ID_AA64ISAR1_EL1, cpu,
				      info->reg_id_aa64isar1, boot->reg_id_aa64isar1);

	/*
	 * Differing PARange support is fine as long as all peripherals and
	 * memory are mapped within the minimum PARange of all CPUs.
	 * Linux should not care about secure memory.
	 */
	taint |= check_update_ftr_reg(SYS_ID_AA64MMFR0_EL1, cpu,
				      info->reg_id_aa64mmfr0, boot->reg_id_aa64mmfr0);
	taint |= check_update_ftr_reg(SYS_ID_AA64MMFR1_EL1, cpu,
				      info->reg_id_aa64mmfr1, boot->reg_id_aa64mmfr1);

	/*
	 * EL3 is not our concern.
	 * ID_AA64PFR1 is currently RES0.
	 */
	taint |= check_update_ftr_reg(SYS_ID_AA64PFR0_EL1, cpu,
				      info->reg_id_aa64pfr0, boot->reg_id_aa64pfr0);
	taint |= check_update_ftr_reg(SYS_ID_AA64PFR1_EL1, cpu,
				      info->reg_id_aa64pfr1, boot->reg_id_aa64pfr1);

	/*
	 * If we have AArch32, we care about 32-bit features for compat. These
	 * registers should be RES0 otherwise.
	 */
	taint |= check_update_ftr_reg(SYS_ID_DFR0_EL1, cpu,
					info->reg_id_dfr0, boot->reg_id_dfr0);
	taint |= check_update_ftr_reg(SYS_ID_ISAR0_EL1, cpu,
					info->reg_id_isar0, boot->reg_id_isar0);
	taint |= check_update_ftr_reg(SYS_ID_ISAR1_EL1, cpu,
					info->reg_id_isar1, boot->reg_id_isar1);
	taint |= check_update_ftr_reg(SYS_ID_ISAR2_EL1, cpu,
					info->reg_id_isar2, boot->reg_id_isar2);
	taint |= check_update_ftr_reg(SYS_ID_ISAR3_EL1, cpu,
					info->reg_id_isar3, boot->reg_id_isar3);
	taint |= check_update_ftr_reg(SYS_ID_ISAR4_EL1, cpu,
					info->reg_id_isar4, boot->reg_id_isar4);
	taint |= check_update_ftr_reg(SYS_ID_ISAR5_EL1, cpu,
					info->reg_id_isar5, boot->reg_id_isar5);

	/*
	 * Regardless of the value of the AuxReg field, the AIFSR, ADFSR, and
	 * ACTLR formats could differ across CPUs and therefore would have to
	 * be trapped for virtualization anyway.
	 */
	taint |= check_update_ftr_reg(SYS_ID_MMFR0_EL1, cpu,
					info->reg_id_mmfr0, boot->reg_id_mmfr0);
	taint |= check_update_ftr_reg(SYS_ID_MMFR1_EL1, cpu,
					info->reg_id_mmfr1, boot->reg_id_mmfr1);
	taint |= check_update_ftr_reg(SYS_ID_MMFR2_EL1, cpu,
					info->reg_id_mmfr2, boot->reg_id_mmfr2);
	taint |= check_update_ftr_reg(SYS_ID_MMFR3_EL1, cpu,
					info->reg_id_mmfr3, boot->reg_id_mmfr3);
	taint |= check_update_ftr_reg(SYS_ID_PFR0_EL1, cpu,
					info->reg_id_pfr0, boot->reg_id_pfr0);
	taint |= check_update_ftr_reg(SYS_ID_PFR1_EL1, cpu,
					info->reg_id_pfr1, boot->reg_id_pfr1);
	taint |= check_update_ftr_reg(SYS_MVFR0_EL1, cpu,
					info->reg_mvfr0, boot->reg_mvfr0);
	taint |= check_update_ftr_reg(SYS_MVFR1_EL1, cpu,
					info->reg_mvfr1, boot->reg_mvfr1);
	taint |= check_update_ftr_reg(SYS_MVFR2_EL1, cpu,
					info->reg_mvfr2, boot->reg_mvfr2);

	/*
	 * Mismatched CPU features are a recipe for disaster. Don't even
	 * pretend to support them.
	 */
	WARN_TAINT_ONCE(taint, TAINT_CPU_OUT_OF_SPEC,
			"Unsupported CPU feature variation.\n");
}

u64 read_system_reg(u32 id)
{
	struct arm64_ftr_reg *regp = get_arm64_ftr_reg(id);

	/* We shouldn't get a request for an unsupported register */
	BUG_ON(!regp);
	return regp->sys_val;
}

#include <linux/irqchip/arm-gic-v3.h>

static bool
feature_matches(u64 reg, const struct arm64_cpu_capabilities *entry)
{
	int val = cpuid_feature_extract_field(reg, entry->field_pos);

	return val >= entry->min_field_value;
}

static bool
has_cpuid_feature(const struct arm64_cpu_capabilities *entry)
{
	u64 val;

	val = read_system_reg(entry->sys_reg);
	return feature_matches(val, entry);
}

static bool has_useable_gicv3_cpuif(const struct arm64_cpu_capabilities *entry)
{
	bool has_sre;

	if (!has_cpuid_feature(entry))
		return false;

	has_sre = gic_enable_sre();
	if (!has_sre)
		pr_warn_once("%s present but disabled by higher exception level\n",
			     entry->desc);

	return has_sre;
}

static bool has_no_hw_prefetch(const struct arm64_cpu_capabilities *entry)
{
	u32 midr = read_cpuid_id();
	u32 rv_min, rv_max;

	/* Cavium ThunderX pass 1.x and 2.x */
	rv_min = 0;
	rv_max = (1 << MIDR_VARIANT_SHIFT) | MIDR_REVISION_MASK;

	return MIDR_IS_CPU_MODEL_RANGE(midr, MIDR_THUNDERX, rv_min, rv_max);
}

static const struct arm64_cpu_capabilities arm64_features[] = {
	{
		.desc = "GIC system register CPU interface",
		.capability = ARM64_HAS_SYSREG_GIC_CPUIF,
		.matches = has_useable_gicv3_cpuif,
		.sys_reg = SYS_ID_AA64PFR0_EL1,
		.field_pos = ID_AA64PFR0_GIC_SHIFT,
		.min_field_value = 1,
	},
#ifdef CONFIG_ARM64_PAN
	{
		.desc = "Privileged Access Never",
		.capability = ARM64_HAS_PAN,
		.matches = has_cpuid_feature,
		.sys_reg = SYS_ID_AA64MMFR1_EL1,
		.field_pos = ID_AA64MMFR1_PAN_SHIFT,
		.min_field_value = 1,
		.enable = cpu_enable_pan,
	},
#endif /* CONFIG_ARM64_PAN */
#if defined(CONFIG_AS_LSE) && defined(CONFIG_ARM64_LSE_ATOMICS)
	{
		.desc = "LSE atomic instructions",
		.capability = ARM64_HAS_LSE_ATOMICS,
		.matches = has_cpuid_feature,
		.sys_reg = SYS_ID_AA64ISAR0_EL1,
		.field_pos = ID_AA64ISAR0_ATOMICS_SHIFT,
		.min_field_value = 2,
	},
#endif /* CONFIG_AS_LSE && CONFIG_ARM64_LSE_ATOMICS */
	{
		.desc = "Software prefetching using PRFM",
		.capability = ARM64_HAS_NO_HW_PREFETCH,
		.matches = has_no_hw_prefetch,
	},
	{},
};

#define HWCAP_CAP(reg, field, min_value, type, cap)		\
	{							\
		.desc = #cap,					\
		.matches = has_cpuid_feature,			\
		.sys_reg = reg,					\
		.field_pos = field,				\
		.min_field_value = min_value,			\
		.hwcap_type = type,				\
		.hwcap = cap,					\
	}

static const struct arm64_cpu_capabilities arm64_hwcaps[] = {
	HWCAP_CAP(SYS_ID_AA64ISAR0_EL1, ID_AA64ISAR0_AES_SHIFT, 2, CAP_HWCAP, HWCAP_PMULL),
	HWCAP_CAP(SYS_ID_AA64ISAR0_EL1, ID_AA64ISAR0_AES_SHIFT, 1, CAP_HWCAP, HWCAP_AES),
	HWCAP_CAP(SYS_ID_AA64ISAR0_EL1, ID_AA64ISAR0_SHA1_SHIFT, 1, CAP_HWCAP, HWCAP_SHA1),
	HWCAP_CAP(SYS_ID_AA64ISAR0_EL1, ID_AA64ISAR0_SHA2_SHIFT, 1, CAP_HWCAP, HWCAP_SHA2),
	HWCAP_CAP(SYS_ID_AA64ISAR0_EL1, ID_AA64ISAR0_CRC32_SHIFT, 1, CAP_HWCAP, HWCAP_CRC32),
	HWCAP_CAP(SYS_ID_AA64ISAR0_EL1, ID_AA64ISAR0_ATOMICS_SHIFT, 2, CAP_HWCAP, HWCAP_ATOMICS),
	HWCAP_CAP(SYS_ID_AA64PFR0_EL1, ID_AA64PFR0_FP_SHIFT, 0, CAP_HWCAP, HWCAP_FP),
	HWCAP_CAP(SYS_ID_AA64PFR0_EL1, ID_AA64PFR0_ASIMD_SHIFT, 0, CAP_HWCAP, HWCAP_ASIMD),
#ifdef CONFIG_COMPAT
	HWCAP_CAP(SYS_ID_ISAR5_EL1, ID_ISAR5_AES_SHIFT, 2, CAP_COMPAT_HWCAP2, COMPAT_HWCAP2_PMULL),
	HWCAP_CAP(SYS_ID_ISAR5_EL1, ID_ISAR5_AES_SHIFT, 1, CAP_COMPAT_HWCAP2, COMPAT_HWCAP2_AES),
	HWCAP_CAP(SYS_ID_ISAR5_EL1, ID_ISAR5_SHA1_SHIFT, 1, CAP_COMPAT_HWCAP2, COMPAT_HWCAP2_SHA1),
	HWCAP_CAP(SYS_ID_ISAR5_EL1, ID_ISAR5_SHA2_SHIFT, 1, CAP_COMPAT_HWCAP2, COMPAT_HWCAP2_SHA2),
	HWCAP_CAP(SYS_ID_ISAR5_EL1, ID_ISAR5_CRC32_SHIFT, 1, CAP_COMPAT_HWCAP2, COMPAT_HWCAP2_CRC32),
#endif
	{},
};

static void __init cap_set_hwcap(const struct arm64_cpu_capabilities *cap)
{
	switch (cap->hwcap_type) {
	case CAP_HWCAP:
		elf_hwcap |= cap->hwcap;
		break;
#ifdef CONFIG_COMPAT
	case CAP_COMPAT_HWCAP:
		compat_elf_hwcap |= (u32)cap->hwcap;
		break;
	case CAP_COMPAT_HWCAP2:
		compat_elf_hwcap2 |= (u32)cap->hwcap;
		break;
#endif
	default:
		WARN_ON(1);
		break;
	}
}

/* Check if we have a particular HWCAP enabled */
static bool __maybe_unused cpus_have_hwcap(const struct arm64_cpu_capabilities *cap)
{
	bool rc;

	switch (cap->hwcap_type) {
	case CAP_HWCAP:
		rc = (elf_hwcap & cap->hwcap) != 0;
		break;
#ifdef CONFIG_COMPAT
	case CAP_COMPAT_HWCAP:
		rc = (compat_elf_hwcap & (u32)cap->hwcap) != 0;
		break;
	case CAP_COMPAT_HWCAP2:
		rc = (compat_elf_hwcap2 & (u32)cap->hwcap) != 0;
		break;
#endif
	default:
		WARN_ON(1);
		rc = false;
	}

	return rc;
}

static void __init setup_cpu_hwcaps(void)
{
	int i;
	const struct arm64_cpu_capabilities *hwcaps = arm64_hwcaps;

	for (i = 0; hwcaps[i].desc; i++)
		if (hwcaps[i].matches(&hwcaps[i]))
			cap_set_hwcap(&hwcaps[i]);
}

void update_cpu_capabilities(const struct arm64_cpu_capabilities *caps,
			    const char *info)
{
	int i;

	for (i = 0; caps[i].desc; i++) {
		if (!caps[i].matches(&caps[i]))
			continue;

		if (!cpus_have_cap(caps[i].capability))
			pr_info("%s %s\n", info, caps[i].desc);
		cpus_set_cap(caps[i].capability);
	}
}

/*
 * Run through the enabled capabilities and enable() it on all active
 * CPUs
 */
static void __init
enable_cpu_capabilities(const struct arm64_cpu_capabilities *caps)
{
	int i;

	for (i = 0; caps[i].desc; i++)
		if (caps[i].enable && cpus_have_cap(caps[i].capability))
			on_each_cpu(caps[i].enable, NULL, true);
}

#ifdef CONFIG_HOTPLUG_CPU

/*
 * Flag to indicate if we have computed the system wide
 * capabilities based on the boot time active CPUs. This
 * will be used to determine if a new booting CPU should
 * go through the verification process to make sure that it
 * supports the system capabilities, without using a hotplug
 * notifier.
 */
static bool sys_caps_initialised;

static inline void set_sys_caps_initialised(void)
{
	sys_caps_initialised = true;
}

/*
 * __raw_read_system_reg() - Used by a STARTING cpu before cpuinfo is populated.
 */
static u64 __raw_read_system_reg(u32 sys_id)
{
	switch (sys_id) {
	case SYS_ID_PFR0_EL1:		return (u64)read_cpuid(ID_PFR0_EL1);
	case SYS_ID_PFR1_EL1:		return (u64)read_cpuid(ID_PFR1_EL1);
	case SYS_ID_DFR0_EL1:		return (u64)read_cpuid(ID_DFR0_EL1);
	case SYS_ID_MMFR0_EL1:		return (u64)read_cpuid(ID_MMFR0_EL1);
	case SYS_ID_MMFR1_EL1:		return (u64)read_cpuid(ID_MMFR1_EL1);
	case SYS_ID_MMFR2_EL1:		return (u64)read_cpuid(ID_MMFR2_EL1);
	case SYS_ID_MMFR3_EL1:		return (u64)read_cpuid(ID_MMFR3_EL1);
	case SYS_ID_ISAR0_EL1:		return (u64)read_cpuid(ID_ISAR0_EL1);
	case SYS_ID_ISAR1_EL1:		return (u64)read_cpuid(ID_ISAR1_EL1);
	case SYS_ID_ISAR2_EL1:		return (u64)read_cpuid(ID_ISAR2_EL1);
	case SYS_ID_ISAR3_EL1:		return (u64)read_cpuid(ID_ISAR3_EL1);
	case SYS_ID_ISAR4_EL1:		return (u64)read_cpuid(ID_ISAR4_EL1);
	case SYS_ID_ISAR5_EL1:		return (u64)read_cpuid(ID_ISAR4_EL1);
	case SYS_MVFR0_EL1:		return (u64)read_cpuid(MVFR0_EL1);
	case SYS_MVFR1_EL1:		return (u64)read_cpuid(MVFR1_EL1);
	case SYS_MVFR2_EL1:		return (u64)read_cpuid(MVFR2_EL1);

	case SYS_ID_AA64PFR0_EL1:	return (u64)read_cpuid(ID_AA64PFR0_EL1);
	case SYS_ID_AA64PFR1_EL1:	return (u64)read_cpuid(ID_AA64PFR0_EL1);
	case SYS_ID_AA64DFR0_EL1:	return (u64)read_cpuid(ID_AA64DFR0_EL1);
	case SYS_ID_AA64DFR1_EL1:	return (u64)read_cpuid(ID_AA64DFR0_EL1);
	case SYS_ID_AA64MMFR0_EL1:	return (u64)read_cpuid(ID_AA64MMFR0_EL1);
	case SYS_ID_AA64MMFR1_EL1:	return (u64)read_cpuid(ID_AA64MMFR1_EL1);
	case SYS_ID_AA64ISAR0_EL1:	return (u64)read_cpuid(ID_AA64ISAR0_EL1);
	case SYS_ID_AA64ISAR1_EL1:	return (u64)read_cpuid(ID_AA64ISAR1_EL1);

	case SYS_CNTFRQ_EL0:		return (u64)read_cpuid(CNTFRQ_EL0);
	case SYS_CTR_EL0:		return (u64)read_cpuid(CTR_EL0);
	case SYS_DCZID_EL0:		return (u64)read_cpuid(DCZID_EL0);
	default:
		BUG();
		return 0;
	}
}

/*
 * Park the CPU which doesn't have the capability as advertised
 * by the system.
 */
static void fail_incapable_cpu(char *cap_type,
				 const struct arm64_cpu_capabilities *cap)
{
	int cpu = smp_processor_id();

	pr_crit("CPU%d: missing %s : %s\n", cpu, cap_type, cap->desc);
	/* Mark this CPU absent */
	set_cpu_present(cpu, 0);

	/* Check if we can park ourselves */
	if (cpu_ops[cpu] && cpu_ops[cpu]->cpu_die)
		cpu_ops[cpu]->cpu_die(cpu);
	asm(
	"1:	wfe\n"
	"	wfi\n"
	"	b	1b");
}

/*
 * Run through the enabled system capabilities and enable() it on this CPU.
 * The capabilities were decided based on the available CPUs at the boot time.
 * Any new CPU should match the system wide status of the capability. If the
 * new CPU doesn't have a capability which the system now has enabled, we
 * cannot do anything to fix it up and could cause unexpected failures. So
 * we park the CPU.
 */
void verify_local_cpu_capabilities(void)
{
	int i;
	const struct arm64_cpu_capabilities *caps;

	/*
	 * If we haven't computed the system capabilities, there is nothing
	 * to verify.
	 */
	if (!sys_caps_initialised)
		return;

	caps = arm64_features;
	for (i = 0; caps[i].desc; i++) {
		if (!cpus_have_cap(caps[i].capability) || !caps[i].sys_reg)
			continue;
		/*
		 * If the new CPU misses an advertised feature, we cannot proceed
		 * further, park the cpu.
		 */
		if (!feature_matches(__raw_read_system_reg(caps[i].sys_reg), &caps[i]))
			fail_incapable_cpu("arm64_features", &caps[i]);
		if (caps[i].enable)
			caps[i].enable(NULL);
	}

	for (i = 0, caps = arm64_hwcaps; caps[i].desc; i++) {
		if (!cpus_have_hwcap(&caps[i]))
			continue;
		if (!feature_matches(__raw_read_system_reg(caps[i].sys_reg), &caps[i]))
			fail_incapable_cpu("arm64_hwcaps", &caps[i]);
	}
}

#else	/* !CONFIG_HOTPLUG_CPU */

static inline void set_sys_caps_initialised(void)
{
}

#endif	/* CONFIG_HOTPLUG_CPU */

static void __init setup_feature_capabilities(void)
{
	update_cpu_capabilities(arm64_features, "detected feature:");
	enable_cpu_capabilities(arm64_features);
}

void __init setup_cpu_features(void)
{
	u32 cwg;
	int cls;

	/* Set the CPU feature capabilies */
	setup_feature_capabilities();
	setup_cpu_hwcaps();

	/* Advertise that we have computed the system capabilities */
	set_sys_caps_initialised();

	/*
	 * Check for sane CTR_EL0.CWG value.
	 */
	cwg = cache_type_cwg();
	cls = cache_line_size();
	if (!cwg)
		pr_warn("No Cache Writeback Granule information, assuming cache line size %d\n",
			cls);
	if (L1_CACHE_BYTES < cls)
		pr_warn("L1_CACHE_BYTES smaller than the Cache Writeback Granule (%d < %d)\n",
			L1_CACHE_BYTES, cls);
}
