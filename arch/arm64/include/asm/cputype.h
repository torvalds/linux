/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012 ARM Ltd.
 */
#ifndef __ASM_CPUTYPE_H
#define __ASM_CPUTYPE_H

#define INVALID_HWID		ULONG_MAX

#define MPIDR_UP_BITMASK	(0x1 << 30)
#define MPIDR_MT_BITMASK	(0x1 << 24)
#define MPIDR_HWID_BITMASK	UL(0xff00ffffff)

#define MPIDR_LEVEL_BITS_SHIFT	3
#define MPIDR_LEVEL_BITS	(1 << MPIDR_LEVEL_BITS_SHIFT)
#define MPIDR_LEVEL_MASK	((1 << MPIDR_LEVEL_BITS) - 1)

#define MPIDR_LEVEL_SHIFT(level) \
	(((1 << level) >> 1) << MPIDR_LEVEL_BITS_SHIFT)

#define MPIDR_AFFINITY_LEVEL(mpidr, level) \
	((mpidr >> MPIDR_LEVEL_SHIFT(level)) & MPIDR_LEVEL_MASK)

#define MIDR_REVISION_MASK	0xf
#define MIDR_REVISION(midr)	((midr) & MIDR_REVISION_MASK)
#define MIDR_PARTNUM_SHIFT	4
#define MIDR_PARTNUM_MASK	(0xfff << MIDR_PARTNUM_SHIFT)
#define MIDR_PARTNUM(midr)	\
	(((midr) & MIDR_PARTNUM_MASK) >> MIDR_PARTNUM_SHIFT)
#define MIDR_ARCHITECTURE_SHIFT	16
#define MIDR_ARCHITECTURE_MASK	(0xf << MIDR_ARCHITECTURE_SHIFT)
#define MIDR_ARCHITECTURE(midr)	\
	(((midr) & MIDR_ARCHITECTURE_MASK) >> MIDR_ARCHITECTURE_SHIFT)
#define MIDR_VARIANT_SHIFT	20
#define MIDR_VARIANT_MASK	(0xf << MIDR_VARIANT_SHIFT)
#define MIDR_VARIANT(midr)	\
	(((midr) & MIDR_VARIANT_MASK) >> MIDR_VARIANT_SHIFT)
#define MIDR_IMPLEMENTOR_SHIFT	24
#define MIDR_IMPLEMENTOR_MASK	(0xff << MIDR_IMPLEMENTOR_SHIFT)
#define MIDR_IMPLEMENTOR(midr)	\
	(((midr) & MIDR_IMPLEMENTOR_MASK) >> MIDR_IMPLEMENTOR_SHIFT)

#define MIDR_CPU_MODEL(imp, partnum) \
	(((imp)			<< MIDR_IMPLEMENTOR_SHIFT) | \
	(0xf			<< MIDR_ARCHITECTURE_SHIFT) | \
	((partnum)		<< MIDR_PARTNUM_SHIFT))

#define MIDR_CPU_VAR_REV(var, rev) \
	(((var)	<< MIDR_VARIANT_SHIFT) | (rev))

#define MIDR_CPU_MODEL_MASK (MIDR_IMPLEMENTOR_MASK | MIDR_PARTNUM_MASK | \
			     MIDR_ARCHITECTURE_MASK)

#define ARM_CPU_IMP_ARM			0x41
#define ARM_CPU_IMP_APM			0x50
#define ARM_CPU_IMP_CAVIUM		0x43
#define ARM_CPU_IMP_BRCM		0x42
#define ARM_CPU_IMP_QCOM		0x51
#define ARM_CPU_IMP_NVIDIA		0x4E
#define ARM_CPU_IMP_FUJITSU		0x46
#define ARM_CPU_IMP_HISI		0x48

#define ARM_CPU_PART_AEM_V8		0xD0F
#define ARM_CPU_PART_FOUNDATION		0xD00
#define ARM_CPU_PART_CORTEX_A57		0xD07
#define ARM_CPU_PART_CORTEX_A72		0xD08
#define ARM_CPU_PART_CORTEX_A53		0xD03
#define ARM_CPU_PART_CORTEX_A73		0xD09
#define ARM_CPU_PART_CORTEX_A75		0xD0A
#define ARM_CPU_PART_CORTEX_A35		0xD04
#define ARM_CPU_PART_CORTEX_A55		0xD05
#define ARM_CPU_PART_CORTEX_A76		0xD0B
#define ARM_CPU_PART_NEOVERSE_N1	0xD0C

#define APM_CPU_PART_POTENZA		0x000

#define CAVIUM_CPU_PART_THUNDERX	0x0A1
#define CAVIUM_CPU_PART_THUNDERX_81XX	0x0A2
#define CAVIUM_CPU_PART_THUNDERX_83XX	0x0A3
#define CAVIUM_CPU_PART_THUNDERX2	0x0AF

#define BRCM_CPU_PART_VULCAN		0x516

#define QCOM_CPU_PART_FALKOR_V1		0x800
#define QCOM_CPU_PART_FALKOR		0xC00
#define QCOM_CPU_PART_KRYO		0x200

#define NVIDIA_CPU_PART_DENVER		0x003
#define NVIDIA_CPU_PART_CARMEL		0x004

#define FUJITSU_CPU_PART_A64FX		0x001

#define HISI_CPU_PART_TSV110		0xD01

#define MIDR_CORTEX_A53 MIDR_CPU_MODEL(ARM_CPU_IMP_ARM, ARM_CPU_PART_CORTEX_A53)
#define MIDR_CORTEX_A57 MIDR_CPU_MODEL(ARM_CPU_IMP_ARM, ARM_CPU_PART_CORTEX_A57)
#define MIDR_CORTEX_A72 MIDR_CPU_MODEL(ARM_CPU_IMP_ARM, ARM_CPU_PART_CORTEX_A72)
#define MIDR_CORTEX_A73 MIDR_CPU_MODEL(ARM_CPU_IMP_ARM, ARM_CPU_PART_CORTEX_A73)
#define MIDR_CORTEX_A75 MIDR_CPU_MODEL(ARM_CPU_IMP_ARM, ARM_CPU_PART_CORTEX_A75)
#define MIDR_CORTEX_A35 MIDR_CPU_MODEL(ARM_CPU_IMP_ARM, ARM_CPU_PART_CORTEX_A35)
#define MIDR_CORTEX_A55 MIDR_CPU_MODEL(ARM_CPU_IMP_ARM, ARM_CPU_PART_CORTEX_A55)
#define MIDR_CORTEX_A76	MIDR_CPU_MODEL(ARM_CPU_IMP_ARM, ARM_CPU_PART_CORTEX_A76)
#define MIDR_NEOVERSE_N1 MIDR_CPU_MODEL(ARM_CPU_IMP_ARM, ARM_CPU_PART_NEOVERSE_N1)
#define MIDR_THUNDERX	MIDR_CPU_MODEL(ARM_CPU_IMP_CAVIUM, CAVIUM_CPU_PART_THUNDERX)
#define MIDR_THUNDERX_81XX MIDR_CPU_MODEL(ARM_CPU_IMP_CAVIUM, CAVIUM_CPU_PART_THUNDERX_81XX)
#define MIDR_THUNDERX_83XX MIDR_CPU_MODEL(ARM_CPU_IMP_CAVIUM, CAVIUM_CPU_PART_THUNDERX_83XX)
#define MIDR_CAVIUM_THUNDERX2 MIDR_CPU_MODEL(ARM_CPU_IMP_CAVIUM, CAVIUM_CPU_PART_THUNDERX2)
#define MIDR_BRCM_VULCAN MIDR_CPU_MODEL(ARM_CPU_IMP_BRCM, BRCM_CPU_PART_VULCAN)
#define MIDR_QCOM_FALKOR_V1 MIDR_CPU_MODEL(ARM_CPU_IMP_QCOM, QCOM_CPU_PART_FALKOR_V1)
#define MIDR_QCOM_FALKOR MIDR_CPU_MODEL(ARM_CPU_IMP_QCOM, QCOM_CPU_PART_FALKOR)
#define MIDR_QCOM_KRYO MIDR_CPU_MODEL(ARM_CPU_IMP_QCOM, QCOM_CPU_PART_KRYO)
#define MIDR_NVIDIA_DENVER MIDR_CPU_MODEL(ARM_CPU_IMP_NVIDIA, NVIDIA_CPU_PART_DENVER)
#define MIDR_NVIDIA_CARMEL MIDR_CPU_MODEL(ARM_CPU_IMP_NVIDIA, NVIDIA_CPU_PART_CARMEL)
#define MIDR_FUJITSU_A64FX MIDR_CPU_MODEL(ARM_CPU_IMP_FUJITSU, FUJITSU_CPU_PART_A64FX)
#define MIDR_HISI_TSV110 MIDR_CPU_MODEL(ARM_CPU_IMP_HISI, HISI_CPU_PART_TSV110)

/* Fujitsu Erratum 010001 affects A64FX 1.0 and 1.1, (v0r0 and v1r0) */
#define MIDR_FUJITSU_ERRATUM_010001		MIDR_FUJITSU_A64FX
#define MIDR_FUJITSU_ERRATUM_010001_MASK	(~MIDR_CPU_VAR_REV(1, 0))
#define TCR_CLEAR_FUJITSU_ERRATUM_010001	(TCR_NFD1 | TCR_NFD0)

#ifndef __ASSEMBLY__

#include <asm/sysreg.h>

#define read_cpuid(reg)			read_sysreg_s(SYS_ ## reg)

/*
 * Represent a range of MIDR values for a given CPU model and a
 * range of variant/revision values.
 *
 * @model	- CPU model as defined by MIDR_CPU_MODEL
 * @rv_min	- Minimum value for the revision/variant as defined by
 *		  MIDR_CPU_VAR_REV
 * @rv_max	- Maximum value for the variant/revision for the range.
 */
struct midr_range {
	u32 model;
	u32 rv_min;
	u32 rv_max;
};

#define MIDR_RANGE(m, v_min, r_min, v_max, r_max)		\
	{							\
		.model = m,					\
		.rv_min = MIDR_CPU_VAR_REV(v_min, r_min),	\
		.rv_max = MIDR_CPU_VAR_REV(v_max, r_max),	\
	}

#define MIDR_REV_RANGE(m, v, r_min, r_max) MIDR_RANGE(m, v, r_min, v, r_max)
#define MIDR_REV(m, v, r) MIDR_RANGE(m, v, r, v, r)
#define MIDR_ALL_VERSIONS(m) MIDR_RANGE(m, 0, 0, 0xf, 0xf)

static inline bool midr_is_cpu_model_range(u32 midr, u32 model, u32 rv_min,
					   u32 rv_max)
{
	u32 _model = midr & MIDR_CPU_MODEL_MASK;
	u32 rv = midr & (MIDR_REVISION_MASK | MIDR_VARIANT_MASK);

	return _model == model && rv >= rv_min && rv <= rv_max;
}

static inline bool is_midr_in_range(u32 midr, struct midr_range const *range)
{
	return midr_is_cpu_model_range(midr, range->model,
				       range->rv_min, range->rv_max);
}

static inline bool
is_midr_in_range_list(u32 midr, struct midr_range const *ranges)
{
	while (ranges->model)
		if (is_midr_in_range(midr, ranges++))
			return true;
	return false;
}

/*
 * The CPU ID never changes at run time, so we might as well tell the
 * compiler that it's constant.  Use this function to read the CPU ID
 * rather than directly reading processor_id or read_cpuid() directly.
 */
static inline u32 __attribute_const__ read_cpuid_id(void)
{
	return read_cpuid(MIDR_EL1);
}

static inline u64 __attribute_const__ read_cpuid_mpidr(void)
{
	return read_cpuid(MPIDR_EL1);
}

static inline unsigned int __attribute_const__ read_cpuid_implementor(void)
{
	return MIDR_IMPLEMENTOR(read_cpuid_id());
}

static inline unsigned int __attribute_const__ read_cpuid_part_number(void)
{
	return MIDR_PARTNUM(read_cpuid_id());
}

static inline u32 __attribute_const__ read_cpuid_cachetype(void)
{
	return read_cpuid(CTR_EL0);
}
#endif /* __ASSEMBLY__ */

#endif
