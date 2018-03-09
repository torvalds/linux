/*
 * Copyright (C) 2013 Imagination Technologies
 * Author: Paul Burton <paul.burton@imgtec.com>
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License as published by the
 * Free Software Foundation;  either version 2 of the  License, or (at your
 * option) any later version.
 */

#ifndef __MIPS_ASM_MIPS_CM_H__
#define __MIPS_ASM_MIPS_CM_H__

#include <linux/bitops.h>
#include <linux/errno.h>
#include <linux/io.h>
#include <linux/types.h>

/* The base address of the CM GCR block */
extern void __iomem *mips_cm_base;

/* The base address of the CM L2-only sync region */
extern void __iomem *mips_cm_l2sync_base;

/**
 * __mips_cm_phys_base - retrieve the physical base address of the CM
 *
 * This function returns the physical base address of the Coherence Manager
 * global control block, or 0 if no Coherence Manager is present. It provides
 * a default implementation which reads the CMGCRBase register where available,
 * and may be overridden by platforms which determine this address in a
 * different way by defining a function with the same prototype except for the
 * name mips_cm_phys_base (without underscores).
 */
extern phys_addr_t __mips_cm_phys_base(void);

/*
 * mips_cm_is64 - determine CM register width
 *
 * The CM register width is determined by the version of the CM, with CM3
 * introducing 64 bit GCRs and all prior CM versions having 32 bit GCRs.
 * However we may run a kernel built for MIPS32 on a system with 64 bit GCRs,
 * or vice-versa. This variable indicates the width of the memory accesses
 * that the kernel will perform to GCRs, which may differ from the actual
 * width of the GCRs.
 *
 * It's set to 0 for 32-bit accesses and 1 for 64-bit accesses.
 */
extern int mips_cm_is64;

/**
 * mips_cm_error_report - Report CM cache errors
 */
#ifdef CONFIG_MIPS_CM
extern void mips_cm_error_report(void);
#else
static inline void mips_cm_error_report(void) {}
#endif

/**
 * mips_cm_probe - probe for a Coherence Manager
 *
 * Attempt to detect the presence of a Coherence Manager. Returns 0 if a CM
 * is successfully detected, else -errno.
 */
#ifdef CONFIG_MIPS_CM
extern int mips_cm_probe(void);
#else
static inline int mips_cm_probe(void)
{
	return -ENODEV;
}
#endif

/**
 * mips_cm_present - determine whether a Coherence Manager is present
 *
 * Returns true if a CM is present in the system, else false.
 */
static inline bool mips_cm_present(void)
{
#ifdef CONFIG_MIPS_CM
	return mips_cm_base != NULL;
#else
	return false;
#endif
}

/**
 * mips_cm_has_l2sync - determine whether an L2-only sync region is present
 *
 * Returns true if the system implements an L2-only sync region, else false.
 */
static inline bool mips_cm_has_l2sync(void)
{
#ifdef CONFIG_MIPS_CM
	return mips_cm_l2sync_base != NULL;
#else
	return false;
#endif
}

/* Offsets to register blocks from the CM base address */
#define MIPS_CM_GCB_OFS		0x0000 /* Global Control Block */
#define MIPS_CM_CLCB_OFS	0x2000 /* Core Local Control Block */
#define MIPS_CM_COCB_OFS	0x4000 /* Core Other Control Block */
#define MIPS_CM_GDB_OFS		0x6000 /* Global Debug Block */

/* Total size of the CM memory mapped registers */
#define MIPS_CM_GCR_SIZE	0x8000

/* Size of the L2-only sync region */
#define MIPS_CM_L2SYNC_SIZE	0x1000

/* Macros to ease the creation of register access functions */
#define BUILD_CM_R_(name, off)					\
static inline unsigned long __iomem *addr_gcr_##name(void)	\
{								\
	return (unsigned long __iomem *)(mips_cm_base + (off));	\
}								\
								\
static inline u32 read32_gcr_##name(void)			\
{								\
	return __raw_readl(addr_gcr_##name());			\
}								\
								\
static inline u64 read64_gcr_##name(void)			\
{								\
	void __iomem *addr = addr_gcr_##name();			\
	u64 ret;						\
								\
	if (mips_cm_is64) {					\
		ret = __raw_readq(addr);			\
	} else {						\
		ret = __raw_readl(addr);			\
		ret |= (u64)__raw_readl(addr + 0x4) << 32;	\
	}							\
								\
	return ret;						\
}								\
								\
static inline unsigned long read_gcr_##name(void)		\
{								\
	if (mips_cm_is64)					\
		return read64_gcr_##name();			\
	else							\
		return read32_gcr_##name();			\
}

#define BUILD_CM__W(name, off)					\
static inline void write32_gcr_##name(u32 value)		\
{								\
	__raw_writel(value, addr_gcr_##name());			\
}								\
								\
static inline void write64_gcr_##name(u64 value)		\
{								\
	__raw_writeq(value, addr_gcr_##name());			\
}								\
								\
static inline void write_gcr_##name(unsigned long value)	\
{								\
	if (mips_cm_is64)					\
		write64_gcr_##name(value);			\
	else							\
		write32_gcr_##name(value);			\
}

#define BUILD_CM_RW(name, off)					\
	BUILD_CM_R_(name, off)					\
	BUILD_CM__W(name, off)

#define BUILD_CM_Cx_R_(name, off)				\
	BUILD_CM_R_(cl_##name, MIPS_CM_CLCB_OFS + (off))	\
	BUILD_CM_R_(co_##name, MIPS_CM_COCB_OFS + (off))

#define BUILD_CM_Cx__W(name, off)				\
	BUILD_CM__W(cl_##name, MIPS_CM_CLCB_OFS + (off))	\
	BUILD_CM__W(co_##name, MIPS_CM_COCB_OFS + (off))

#define BUILD_CM_Cx_RW(name, off)				\
	BUILD_CM_Cx_R_(name, off)				\
	BUILD_CM_Cx__W(name, off)

/* GCB register accessor functions */
BUILD_CM_R_(config,		MIPS_CM_GCB_OFS + 0x00)
BUILD_CM_RW(base,		MIPS_CM_GCB_OFS + 0x08)
BUILD_CM_RW(access,		MIPS_CM_GCB_OFS + 0x20)
BUILD_CM_R_(rev,		MIPS_CM_GCB_OFS + 0x30)
BUILD_CM_RW(error_mask,		MIPS_CM_GCB_OFS + 0x40)
BUILD_CM_RW(error_cause,	MIPS_CM_GCB_OFS + 0x48)
BUILD_CM_RW(error_addr,		MIPS_CM_GCB_OFS + 0x50)
BUILD_CM_RW(error_mult,		MIPS_CM_GCB_OFS + 0x58)
BUILD_CM_RW(l2_only_sync_base,	MIPS_CM_GCB_OFS + 0x70)
BUILD_CM_RW(gic_base,		MIPS_CM_GCB_OFS + 0x80)
BUILD_CM_RW(cpc_base,		MIPS_CM_GCB_OFS + 0x88)
BUILD_CM_RW(reg0_base,		MIPS_CM_GCB_OFS + 0x90)
BUILD_CM_RW(reg0_mask,		MIPS_CM_GCB_OFS + 0x98)
BUILD_CM_RW(reg1_base,		MIPS_CM_GCB_OFS + 0xa0)
BUILD_CM_RW(reg1_mask,		MIPS_CM_GCB_OFS + 0xa8)
BUILD_CM_RW(reg2_base,		MIPS_CM_GCB_OFS + 0xb0)
BUILD_CM_RW(reg2_mask,		MIPS_CM_GCB_OFS + 0xb8)
BUILD_CM_RW(reg3_base,		MIPS_CM_GCB_OFS + 0xc0)
BUILD_CM_RW(reg3_mask,		MIPS_CM_GCB_OFS + 0xc8)
BUILD_CM_R_(gic_status,		MIPS_CM_GCB_OFS + 0xd0)
BUILD_CM_R_(cpc_status,		MIPS_CM_GCB_OFS + 0xf0)
BUILD_CM_RW(l2_config,		MIPS_CM_GCB_OFS + 0x130)
BUILD_CM_RW(sys_config2,	MIPS_CM_GCB_OFS + 0x150)
BUILD_CM_RW(l2_pft_control,	MIPS_CM_GCB_OFS + 0x300)
BUILD_CM_RW(l2_pft_control_b,	MIPS_CM_GCB_OFS + 0x308)
BUILD_CM_RW(bev_base,		MIPS_CM_GCB_OFS + 0x680)

/* Core Local & Core Other register accessor functions */
BUILD_CM_Cx_RW(reset_release,	0x00)
BUILD_CM_Cx_RW(coherence,	0x08)
BUILD_CM_Cx_R_(config,		0x10)
BUILD_CM_Cx_RW(other,		0x18)
BUILD_CM_Cx_RW(reset_base,	0x20)
BUILD_CM_Cx_R_(id,		0x28)
BUILD_CM_Cx_RW(reset_ext_base,	0x30)
BUILD_CM_Cx_R_(tcid_0_priority,	0x40)
BUILD_CM_Cx_R_(tcid_1_priority,	0x48)
BUILD_CM_Cx_R_(tcid_2_priority,	0x50)
BUILD_CM_Cx_R_(tcid_3_priority,	0x58)
BUILD_CM_Cx_R_(tcid_4_priority,	0x60)
BUILD_CM_Cx_R_(tcid_5_priority,	0x68)
BUILD_CM_Cx_R_(tcid_6_priority,	0x70)
BUILD_CM_Cx_R_(tcid_7_priority,	0x78)
BUILD_CM_Cx_R_(tcid_8_priority,	0x80)

/* GCR_CONFIG register fields */
#define CM_GCR_CONFIG_NUMIOCU_SHF		8
#define CM_GCR_CONFIG_NUMIOCU_MSK		(_ULCAST_(0xf) << 8)
#define CM_GCR_CONFIG_PCORES_SHF		0
#define CM_GCR_CONFIG_PCORES_MSK		(_ULCAST_(0xff) << 0)

/* GCR_BASE register fields */
#define CM_GCR_BASE_GCRBASE_SHF			15
#define CM_GCR_BASE_GCRBASE_MSK			(_ULCAST_(0x1ffff) << 15)
#define CM_GCR_BASE_CMDEFTGT_SHF		0
#define CM_GCR_BASE_CMDEFTGT_MSK		(_ULCAST_(0x3) << 0)
#define  CM_GCR_BASE_CMDEFTGT_MEM		0
#define  CM_GCR_BASE_CMDEFTGT_RESERVED		1
#define  CM_GCR_BASE_CMDEFTGT_IOCU0		2
#define  CM_GCR_BASE_CMDEFTGT_IOCU1		3

/* GCR_ACCESS register fields */
#define CM_GCR_ACCESS_ACCESSEN_SHF		0
#define CM_GCR_ACCESS_ACCESSEN_MSK		(_ULCAST_(0xff) << 0)

/* GCR_REV register fields */
#define CM_GCR_REV_MAJOR_SHF			8
#define CM_GCR_REV_MAJOR_MSK			(_ULCAST_(0xff) << 8)
#define CM_GCR_REV_MINOR_SHF			0
#define CM_GCR_REV_MINOR_MSK			(_ULCAST_(0xff) << 0)

#define CM_ENCODE_REV(major, minor) \
		(((major) << CM_GCR_REV_MAJOR_SHF) | \
		 ((minor) << CM_GCR_REV_MINOR_SHF))

#define CM_REV_CM2				CM_ENCODE_REV(6, 0)
#define CM_REV_CM2_5				CM_ENCODE_REV(7, 0)
#define CM_REV_CM3				CM_ENCODE_REV(8, 0)

/* GCR_ERROR_CAUSE register fields */
#define CM_GCR_ERROR_CAUSE_ERRTYPE_SHF		27
#define CM_GCR_ERROR_CAUSE_ERRTYPE_MSK		(_ULCAST_(0x1f) << 27)
#define CM3_GCR_ERROR_CAUSE_ERRTYPE_SHF		58
#define CM3_GCR_ERROR_CAUSE_ERRTYPE_MSK		GENMASK_ULL(63, 58)
#define CM_GCR_ERROR_CAUSE_ERRINFO_SHF		0
#define CM_GCR_ERROR_CAUSE_ERRINGO_MSK		(_ULCAST_(0x7ffffff) << 0)

/* GCR_ERROR_MULT register fields */
#define CM_GCR_ERROR_MULT_ERR2ND_SHF		0
#define CM_GCR_ERROR_MULT_ERR2ND_MSK		(_ULCAST_(0x1f) << 0)

/* GCR_L2_ONLY_SYNC_BASE register fields */
#define CM_GCR_L2_ONLY_SYNC_BASE_SYNCBASE_SHF	12
#define CM_GCR_L2_ONLY_SYNC_BASE_SYNCBASE_MSK	(_ULCAST_(0xfffff) << 12)
#define CM_GCR_L2_ONLY_SYNC_BASE_SYNCEN_SHF	0
#define CM_GCR_L2_ONLY_SYNC_BASE_SYNCEN_MSK	(_ULCAST_(0x1) << 0)

/* GCR_GIC_BASE register fields */
#define CM_GCR_GIC_BASE_GICBASE_SHF		17
#define CM_GCR_GIC_BASE_GICBASE_MSK		(_ULCAST_(0x7fff) << 17)
#define CM_GCR_GIC_BASE_GICEN_SHF		0
#define CM_GCR_GIC_BASE_GICEN_MSK		(_ULCAST_(0x1) << 0)

/* GCR_CPC_BASE register fields */
#define CM_GCR_CPC_BASE_CPCBASE_SHF		17
#define CM_GCR_CPC_BASE_CPCBASE_MSK		(_ULCAST_(0x7fff) << 17)
#define CM_GCR_CPC_BASE_CPCEN_SHF		0
#define CM_GCR_CPC_BASE_CPCEN_MSK		(_ULCAST_(0x1) << 0)

/* GCR_GIC_STATUS register fields */
#define CM_GCR_GIC_STATUS_GICEX_SHF		0
#define CM_GCR_GIC_STATUS_GICEX_MSK		(_ULCAST_(0x1) << 0)

/* GCR_REGn_BASE register fields */
#define CM_GCR_REGn_BASE_BASEADDR_SHF		16
#define CM_GCR_REGn_BASE_BASEADDR_MSK		(_ULCAST_(0xffff) << 16)

/* GCR_REGn_MASK register fields */
#define CM_GCR_REGn_MASK_ADDRMASK_SHF		16
#define CM_GCR_REGn_MASK_ADDRMASK_MSK		(_ULCAST_(0xffff) << 16)
#define CM_GCR_REGn_MASK_CCAOVR_SHF		5
#define CM_GCR_REGn_MASK_CCAOVR_MSK		(_ULCAST_(0x3) << 5)
#define CM_GCR_REGn_MASK_CCAOVREN_SHF		4
#define CM_GCR_REGn_MASK_CCAOVREN_MSK		(_ULCAST_(0x1) << 4)
#define CM_GCR_REGn_MASK_DROPL2_SHF		2
#define CM_GCR_REGn_MASK_DROPL2_MSK		(_ULCAST_(0x1) << 2)
#define CM_GCR_REGn_MASK_CMTGT_SHF		0
#define CM_GCR_REGn_MASK_CMTGT_MSK		(_ULCAST_(0x3) << 0)
#define  CM_GCR_REGn_MASK_CMTGT_DISABLED	(_ULCAST_(0x0) << 0)
#define  CM_GCR_REGn_MASK_CMTGT_MEM		(_ULCAST_(0x1) << 0)
#define  CM_GCR_REGn_MASK_CMTGT_IOCU0		(_ULCAST_(0x2) << 0)
#define  CM_GCR_REGn_MASK_CMTGT_IOCU1		(_ULCAST_(0x3) << 0)

/* GCR_GIC_STATUS register fields */
#define CM_GCR_GIC_STATUS_EX_SHF		0
#define CM_GCR_GIC_STATUS_EX_MSK		(_ULCAST_(0x1) << 0)

/* GCR_CPC_STATUS register fields */
#define CM_GCR_CPC_STATUS_EX_SHF		0
#define CM_GCR_CPC_STATUS_EX_MSK		(_ULCAST_(0x1) << 0)

/* GCR_L2_CONFIG register fields */
#define CM_GCR_L2_CONFIG_BYPASS_SHF		20
#define CM_GCR_L2_CONFIG_BYPASS_MSK		(_ULCAST_(0x1) << 20)
#define CM_GCR_L2_CONFIG_SET_SIZE_SHF		12
#define CM_GCR_L2_CONFIG_SET_SIZE_MSK		(_ULCAST_(0xf) << 12)
#define CM_GCR_L2_CONFIG_LINE_SIZE_SHF		8
#define CM_GCR_L2_CONFIG_LINE_SIZE_MSK		(_ULCAST_(0xf) << 8)
#define CM_GCR_L2_CONFIG_ASSOC_SHF		0
#define CM_GCR_L2_CONFIG_ASSOC_MSK		(_ULCAST_(0xff) << 0)

/* GCR_SYS_CONFIG2 register fields */
#define CM_GCR_SYS_CONFIG2_MAXVPW_SHF		0
#define CM_GCR_SYS_CONFIG2_MAXVPW_MSK		(_ULCAST_(0xf) << 0)

/* GCR_L2_PFT_CONTROL register fields */
#define CM_GCR_L2_PFT_CONTROL_PAGEMASK_SHF	12
#define CM_GCR_L2_PFT_CONTROL_PAGEMASK_MSK	(_ULCAST_(0xfffff) << 12)
#define CM_GCR_L2_PFT_CONTROL_PFTEN_SHF		8
#define CM_GCR_L2_PFT_CONTROL_PFTEN_MSK		(_ULCAST_(0x1) << 8)
#define CM_GCR_L2_PFT_CONTROL_NPFT_SHF		0
#define CM_GCR_L2_PFT_CONTROL_NPFT_MSK		(_ULCAST_(0xff) << 0)

/* GCR_L2_PFT_CONTROL_B register fields */
#define CM_GCR_L2_PFT_CONTROL_B_CEN_SHF		8
#define CM_GCR_L2_PFT_CONTROL_B_CEN_MSK		(_ULCAST_(0x1) << 8)
#define CM_GCR_L2_PFT_CONTROL_B_PORTID_SHF	0
#define CM_GCR_L2_PFT_CONTROL_B_PORTID_MSK	(_ULCAST_(0xff) << 0)

/* GCR_Cx_COHERENCE register fields */
#define CM_GCR_Cx_COHERENCE_COHDOMAINEN_SHF	0
#define CM_GCR_Cx_COHERENCE_COHDOMAINEN_MSK	(_ULCAST_(0xff) << 0)

/* GCR_Cx_CONFIG register fields */
#define CM_GCR_Cx_CONFIG_IOCUTYPE_SHF		10
#define CM_GCR_Cx_CONFIG_IOCUTYPE_MSK		(_ULCAST_(0x3) << 10)
#define CM_GCR_Cx_CONFIG_PVPE_SHF		0
#define CM_GCR_Cx_CONFIG_PVPE_MSK		(_ULCAST_(0x3ff) << 0)

/* GCR_Cx_OTHER register fields */
#define CM_GCR_Cx_OTHER_CORENUM_SHF		16
#define CM_GCR_Cx_OTHER_CORENUM_MSK		(_ULCAST_(0xffff) << 16)
#define CM3_GCR_Cx_OTHER_CORE_SHF		8
#define CM3_GCR_Cx_OTHER_CORE_MSK		(_ULCAST_(0x3f) << 8)
#define CM3_GCR_Cx_OTHER_VP_SHF			0
#define CM3_GCR_Cx_OTHER_VP_MSK			(_ULCAST_(0x7) << 0)

/* GCR_Cx_RESET_BASE register fields */
#define CM_GCR_Cx_RESET_BASE_BEVEXCBASE_SHF	12
#define CM_GCR_Cx_RESET_BASE_BEVEXCBASE_MSK	(_ULCAST_(0xfffff) << 12)

/* GCR_Cx_RESET_EXT_BASE register fields */
#define CM_GCR_Cx_RESET_EXT_BASE_EVARESET_SHF	31
#define CM_GCR_Cx_RESET_EXT_BASE_EVARESET_MSK	(_ULCAST_(0x1) << 31)
#define CM_GCR_Cx_RESET_EXT_BASE_UEB_SHF	30
#define CM_GCR_Cx_RESET_EXT_BASE_UEB_MSK	(_ULCAST_(0x1) << 30)
#define CM_GCR_Cx_RESET_EXT_BASE_BEVEXCMASK_SHF	20
#define CM_GCR_Cx_RESET_EXT_BASE_BEVEXCMASK_MSK	(_ULCAST_(0xff) << 20)
#define CM_GCR_Cx_RESET_EXT_BASE_BEVEXCPA_SHF	1
#define CM_GCR_Cx_RESET_EXT_BASE_BEVEXCPA_MSK	(_ULCAST_(0x7f) << 1)
#define CM_GCR_Cx_RESET_EXT_BASE_PRESENT_SHF	0
#define CM_GCR_Cx_RESET_EXT_BASE_PRESENT_MSK	(_ULCAST_(0x1) << 0)

/**
 * mips_cm_numcores - return the number of cores present in the system
 *
 * Returns the value of the PCORES field of the GCR_CONFIG register plus 1, or
 * zero if no Coherence Manager is present.
 */
static inline unsigned mips_cm_numcores(void)
{
	if (!mips_cm_present())
		return 0;

	return ((read_gcr_config() & CM_GCR_CONFIG_PCORES_MSK)
		>> CM_GCR_CONFIG_PCORES_SHF) + 1;
}

/**
 * mips_cm_numiocu - return the number of IOCUs present in the system
 *
 * Returns the value of the NUMIOCU field of the GCR_CONFIG register, or zero
 * if no Coherence Manager is present.
 */
static inline unsigned mips_cm_numiocu(void)
{
	if (!mips_cm_present())
		return 0;

	return (read_gcr_config() & CM_GCR_CONFIG_NUMIOCU_MSK)
		>> CM_GCR_CONFIG_NUMIOCU_SHF;
}

/**
 * mips_cm_l2sync - perform an L2-only sync operation
 *
 * If an L2-only sync region is present in the system then this function
 * performs and L2-only sync and returns zero. Otherwise it returns -ENODEV.
 */
static inline int mips_cm_l2sync(void)
{
	if (!mips_cm_has_l2sync())
		return -ENODEV;

	writel(0, mips_cm_l2sync_base);
	return 0;
}

/**
 * mips_cm_revision() - return CM revision
 *
 * Return: The revision of the CM, from GCR_REV, or 0 if no CM is present. The
 * return value should be checked against the CM_REV_* macros.
 */
static inline int mips_cm_revision(void)
{
	if (!mips_cm_present())
		return 0;

	return read_gcr_rev();
}

/**
 * mips_cm_max_vp_width() - return the width in bits of VP indices
 *
 * Return: the width, in bits, of VP indices in fields that combine core & VP
 * indices.
 */
static inline unsigned int mips_cm_max_vp_width(void)
{
	extern int smp_num_siblings;

	if (mips_cm_revision() >= CM_REV_CM3)
		return read_gcr_sys_config2() & CM_GCR_SYS_CONFIG2_MAXVPW_MSK;

	if (config_enabled(CONFIG_SMP))
		return smp_num_siblings;

	return 1;
}

/**
 * mips_cm_vp_id() - calculate the hardware VP ID for a CPU
 * @cpu: the CPU whose VP ID to calculate
 *
 * Hardware such as the GIC uses identifiers for VPs which may not match the
 * CPU numbers used by Linux. This function calculates the hardware VP
 * identifier corresponding to a given CPU.
 *
 * Return: the VP ID for the CPU.
 */
static inline unsigned int mips_cm_vp_id(unsigned int cpu)
{
	unsigned int core = cpu_data[cpu].core;
	unsigned int vp = cpu_vpe_id(&cpu_data[cpu]);

	return (core * mips_cm_max_vp_width()) + vp;
}

#ifdef CONFIG_MIPS_CM

/**
 * mips_cm_lock_other - lock access to another core
 * @core: the other core to be accessed
 * @vp: the VP within the other core to be accessed
 *
 * Call before operating upon a core via the 'other' register region in
 * order to prevent the region being moved during access. Must be followed
 * by a call to mips_cm_unlock_other.
 */
extern void mips_cm_lock_other(unsigned int core, unsigned int vp);

/**
 * mips_cm_unlock_other - unlock access to another core
 *
 * Call after operating upon another core via the 'other' register region.
 * Must be called after mips_cm_lock_other.
 */
extern void mips_cm_unlock_other(void);

#else /* !CONFIG_MIPS_CM */

static inline void mips_cm_lock_other(unsigned int core) { }
static inline void mips_cm_unlock_other(void) { }

#endif /* !CONFIG_MIPS_CM */

#endif /* __MIPS_ASM_MIPS_CM_H__ */
