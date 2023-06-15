/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (C) 2012,2013 - ARM Ltd
 * Author: Marc Zyngier <marc.zyngier@arm.com>
 */

#ifndef __ARM64_KVM_ARM_H__
#define __ARM64_KVM_ARM_H__

#include <asm/esr.h>
#include <asm/memory.h>
#include <asm/types.h>

/* Hyp Configuration Register (HCR) bits */

#define HCR_TID5	(UL(1) << 58)
#define HCR_DCT		(UL(1) << 57)
#define HCR_ATA_SHIFT	56
#define HCR_ATA		(UL(1) << HCR_ATA_SHIFT)
#define HCR_AMVOFFEN	(UL(1) << 51)
#define HCR_TID4	(UL(1) << 49)
#define HCR_FIEN	(UL(1) << 47)
#define HCR_FWB		(UL(1) << 46)
#define HCR_API		(UL(1) << 41)
#define HCR_APK		(UL(1) << 40)
#define HCR_TEA		(UL(1) << 37)
#define HCR_TERR	(UL(1) << 36)
#define HCR_TLOR	(UL(1) << 35)
#define HCR_E2H		(UL(1) << 34)
#define HCR_ID		(UL(1) << 33)
#define HCR_CD		(UL(1) << 32)
#define HCR_RW_SHIFT	31
#define HCR_RW		(UL(1) << HCR_RW_SHIFT)
#define HCR_TRVM	(UL(1) << 30)
#define HCR_HCD		(UL(1) << 29)
#define HCR_TDZ		(UL(1) << 28)
#define HCR_TGE		(UL(1) << 27)
#define HCR_TVM		(UL(1) << 26)
#define HCR_TTLB	(UL(1) << 25)
#define HCR_TPU		(UL(1) << 24)
#define HCR_TPC		(UL(1) << 23) /* HCR_TPCP if FEAT_DPB */
#define HCR_TSW		(UL(1) << 22)
#define HCR_TACR	(UL(1) << 21)
#define HCR_TIDCP	(UL(1) << 20)
#define HCR_TSC		(UL(1) << 19)
#define HCR_TID3	(UL(1) << 18)
#define HCR_TID2	(UL(1) << 17)
#define HCR_TID1	(UL(1) << 16)
#define HCR_TID0	(UL(1) << 15)
#define HCR_TWE		(UL(1) << 14)
#define HCR_TWI		(UL(1) << 13)
#define HCR_DC		(UL(1) << 12)
#define HCR_BSU		(3 << 10)
#define HCR_BSU_IS	(UL(1) << 10)
#define HCR_FB		(UL(1) << 9)
#define HCR_VSE		(UL(1) << 8)
#define HCR_VI		(UL(1) << 7)
#define HCR_VF		(UL(1) << 6)
#define HCR_AMO		(UL(1) << 5)
#define HCR_IMO		(UL(1) << 4)
#define HCR_FMO		(UL(1) << 3)
#define HCR_PTW		(UL(1) << 2)
#define HCR_SWIO	(UL(1) << 1)
#define HCR_VM		(UL(1) << 0)
#define HCR_RES0	((UL(1) << 48) | (UL(1) << 39))

/*
 * The bits we set in HCR:
 * TLOR:	Trap LORegion register accesses
 * RW:		64bit by default, can be overridden for 32bit VMs
 * TACR:	Trap ACTLR
 * TSC:		Trap SMC
 * TSW:		Trap cache operations by set/way
 * TWE:		Trap WFE
 * TWI:		Trap WFI
 * TIDCP:	Trap L2CTLR/L2ECTLR
 * BSU_IS:	Upgrade barriers to the inner shareable domain
 * FB:		Force broadcast of all maintenance operations
 * AMO:		Override CPSR.A and enable signaling with VA
 * IMO:		Override CPSR.I and enable signaling with VI
 * FMO:		Override CPSR.F and enable signaling with VF
 * SWIO:	Turn set/way invalidates into set/way clean+invalidate
 * PTW:		Take a stage2 fault if a stage1 walk steps in device memory
 * TID3:	Trap EL1 reads of group 3 ID registers
 * TID2:	Trap CTR_EL0, CCSIDR2_EL1, CLIDR_EL1, and CSSELR_EL1
 */
#define HCR_GUEST_FLAGS (HCR_TSC | HCR_TSW | HCR_TWE | HCR_TWI | HCR_VM | \
			 HCR_BSU_IS | HCR_FB | HCR_TACR | \
			 HCR_AMO | HCR_SWIO | HCR_TIDCP | HCR_RW | HCR_TLOR | \
			 HCR_FMO | HCR_IMO | HCR_PTW | HCR_TID3)
#define HCR_VIRT_EXCP_MASK (HCR_VSE | HCR_VI | HCR_VF)
#define HCR_HOST_NVHE_FLAGS (HCR_RW | HCR_API | HCR_APK | HCR_ATA)
#define HCR_HOST_NVHE_PROTECTED_FLAGS (HCR_HOST_NVHE_FLAGS | HCR_TSC)
#define HCR_HOST_VHE_FLAGS (HCR_RW | HCR_TGE | HCR_E2H)

/* TCR_EL2 Registers bits */
#define TCR_EL2_RES1		((1U << 31) | (1 << 23))
#define TCR_EL2_TBI		(1 << 20)
#define TCR_EL2_PS_SHIFT	16
#define TCR_EL2_PS_MASK		(7 << TCR_EL2_PS_SHIFT)
#define TCR_EL2_PS_40B		(2 << TCR_EL2_PS_SHIFT)
#define TCR_EL2_TG0_MASK	TCR_TG0_MASK
#define TCR_EL2_SH0_MASK	TCR_SH0_MASK
#define TCR_EL2_ORGN0_MASK	TCR_ORGN0_MASK
#define TCR_EL2_IRGN0_MASK	TCR_IRGN0_MASK
#define TCR_EL2_T0SZ_MASK	0x3f
#define TCR_EL2_MASK	(TCR_EL2_TG0_MASK | TCR_EL2_SH0_MASK | \
			 TCR_EL2_ORGN0_MASK | TCR_EL2_IRGN0_MASK | TCR_EL2_T0SZ_MASK)

/* VTCR_EL2 Registers bits */
#define VTCR_EL2_RES1		(1U << 31)
#define VTCR_EL2_HD		(1 << 22)
#define VTCR_EL2_HA		(1 << 21)
#define VTCR_EL2_PS_SHIFT	TCR_EL2_PS_SHIFT
#define VTCR_EL2_PS_MASK	TCR_EL2_PS_MASK
#define VTCR_EL2_TG0_MASK	TCR_TG0_MASK
#define VTCR_EL2_TG0_4K		TCR_TG0_4K
#define VTCR_EL2_TG0_16K	TCR_TG0_16K
#define VTCR_EL2_TG0_64K	TCR_TG0_64K
#define VTCR_EL2_SH0_MASK	TCR_SH0_MASK
#define VTCR_EL2_SH0_INNER	TCR_SH0_INNER
#define VTCR_EL2_ORGN0_MASK	TCR_ORGN0_MASK
#define VTCR_EL2_ORGN0_WBWA	TCR_ORGN0_WBWA
#define VTCR_EL2_IRGN0_MASK	TCR_IRGN0_MASK
#define VTCR_EL2_IRGN0_WBWA	TCR_IRGN0_WBWA
#define VTCR_EL2_SL0_SHIFT	6
#define VTCR_EL2_SL0_MASK	(3 << VTCR_EL2_SL0_SHIFT)
#define VTCR_EL2_T0SZ_MASK	0x3f
#define VTCR_EL2_VS_SHIFT	19
#define VTCR_EL2_VS_8BIT	(0 << VTCR_EL2_VS_SHIFT)
#define VTCR_EL2_VS_16BIT	(1 << VTCR_EL2_VS_SHIFT)

#define VTCR_EL2_T0SZ(x)	TCR_T0SZ(x)

/*
 * We configure the Stage-2 page tables to always restrict the IPA space to be
 * 40 bits wide (T0SZ = 24).  Systems with a PARange smaller than 40 bits are
 * not known to exist and will break with this configuration.
 *
 * The VTCR_EL2 is configured per VM and is initialised in kvm_init_stage2_mmu.
 *
 * Note that when using 4K pages, we concatenate two first level page tables
 * together. With 16K pages, we concatenate 16 first level page tables.
 *
 */

#define VTCR_EL2_COMMON_BITS	(VTCR_EL2_SH0_INNER | VTCR_EL2_ORGN0_WBWA | \
				 VTCR_EL2_IRGN0_WBWA | VTCR_EL2_RES1)

/*
 * VTCR_EL2:SL0 indicates the entry level for Stage2 translation.
 * Interestingly, it depends on the page size.
 * See D.10.2.121, VTCR_EL2, in ARM DDI 0487C.a
 *
 *	-----------------------------------------
 *	| Entry level		|  4K  | 16K/64K |
 *	------------------------------------------
 *	| Level: 0		|  2   |   -     |
 *	------------------------------------------
 *	| Level: 1		|  1   |   2     |
 *	------------------------------------------
 *	| Level: 2		|  0   |   1     |
 *	------------------------------------------
 *	| Level: 3		|  -   |   0     |
 *	------------------------------------------
 *
 * The table roughly translates to :
 *
 *	SL0(PAGE_SIZE, Entry_level) = TGRAN_SL0_BASE - Entry_Level
 *
 * Where TGRAN_SL0_BASE is a magic number depending on the page size:
 * 	TGRAN_SL0_BASE(4K) = 2
 *	TGRAN_SL0_BASE(16K) = 3
 *	TGRAN_SL0_BASE(64K) = 3
 * provided we take care of ruling out the unsupported cases and
 * Entry_Level = 4 - Number_of_levels.
 *
 */
#ifdef CONFIG_ARM64_64K_PAGES

#define VTCR_EL2_TGRAN			VTCR_EL2_TG0_64K
#define VTCR_EL2_TGRAN_SL0_BASE		3UL

#elif defined(CONFIG_ARM64_16K_PAGES)

#define VTCR_EL2_TGRAN			VTCR_EL2_TG0_16K
#define VTCR_EL2_TGRAN_SL0_BASE		3UL

#else	/* 4K */

#define VTCR_EL2_TGRAN			VTCR_EL2_TG0_4K
#define VTCR_EL2_TGRAN_SL0_BASE		2UL

#endif

#define VTCR_EL2_LVLS_TO_SL0(levels)	\
	((VTCR_EL2_TGRAN_SL0_BASE - (4 - (levels))) << VTCR_EL2_SL0_SHIFT)
#define VTCR_EL2_SL0_TO_LVLS(sl0)	\
	((sl0) + 4 - VTCR_EL2_TGRAN_SL0_BASE)
#define VTCR_EL2_LVLS(vtcr)		\
	VTCR_EL2_SL0_TO_LVLS(((vtcr) & VTCR_EL2_SL0_MASK) >> VTCR_EL2_SL0_SHIFT)

#define VTCR_EL2_FLAGS			(VTCR_EL2_COMMON_BITS | VTCR_EL2_TGRAN)
#define VTCR_EL2_IPA(vtcr)		(64 - ((vtcr) & VTCR_EL2_T0SZ_MASK))

/*
 * ARM VMSAv8-64 defines an algorithm for finding the translation table
 * descriptors in section D4.2.8 in ARM DDI 0487C.a.
 *
 * The algorithm defines the expectations on the translation table
 * addresses for each level, based on PAGE_SIZE, entry level
 * and the translation table size (T0SZ). The variable "x" in the
 * algorithm determines the alignment of a table base address at a given
 * level and thus determines the alignment of VTTBR:BADDR for stage2
 * page table entry level.
 * Since the number of bits resolved at the entry level could vary
 * depending on the T0SZ, the value of "x" is defined based on a
 * Magic constant for a given PAGE_SIZE and Entry Level. The
 * intermediate levels must be always aligned to the PAGE_SIZE (i.e,
 * x = PAGE_SHIFT).
 *
 * The value of "x" for entry level is calculated as :
 *    x = Magic_N - T0SZ
 *
 * where Magic_N is an integer depending on the page size and the entry
 * level of the page table as below:
 *
 *	--------------------------------------------
 *	| Entry level		|  4K    16K   64K |
 *	--------------------------------------------
 *	| Level: 0 (4 levels)	| 28   |  -  |  -  |
 *	--------------------------------------------
 *	| Level: 1 (3 levels)	| 37   | 31  | 25  |
 *	--------------------------------------------
 *	| Level: 2 (2 levels)	| 46   | 42  | 38  |
 *	--------------------------------------------
 *	| Level: 3 (1 level)	| -    | 53  | 51  |
 *	--------------------------------------------
 *
 * We have a magic formula for the Magic_N below:
 *
 *  Magic_N(PAGE_SIZE, Level) = 64 - ((PAGE_SHIFT - 3) * Number_of_levels)
 *
 * where Number_of_levels = (4 - Level). We are only interested in the
 * value for Entry_Level for the stage2 page table.
 *
 * So, given that T0SZ = (64 - IPA_SHIFT), we can compute 'x' as follows:
 *
 *	x = (64 - ((PAGE_SHIFT - 3) * Number_of_levels)) - (64 - IPA_SHIFT)
 *	  = IPA_SHIFT - ((PAGE_SHIFT - 3) * Number of levels)
 *
 * Here is one way to explain the Magic Formula:
 *
 *  x = log2(Size_of_Entry_Level_Table)
 *
 * Since, we can resolve (PAGE_SHIFT - 3) bits at each level, and another
 * PAGE_SHIFT bits in the PTE, we have :
 *
 *  Bits_Entry_level = IPA_SHIFT - ((PAGE_SHIFT - 3) * (n - 1) + PAGE_SHIFT)
 *		     = IPA_SHIFT - (PAGE_SHIFT - 3) * n - 3
 *  where n = number of levels, and since each pointer is 8bytes, we have:
 *
 *  x = Bits_Entry_Level + 3
 *    = IPA_SHIFT - (PAGE_SHIFT - 3) * n
 *
 * The only constraint here is that, we have to find the number of page table
 * levels for a given IPA size (which we do, see stage2_pt_levels())
 */
#define ARM64_VTTBR_X(ipa, levels)	((ipa) - ((levels) * (PAGE_SHIFT - 3)))

#define VTTBR_CNP_BIT     (UL(1))
#define VTTBR_VMID_SHIFT  (UL(48))
#define VTTBR_VMID_MASK(size) (_AT(u64, (1 << size) - 1) << VTTBR_VMID_SHIFT)

/* Hyp System Trap Register */
#define HSTR_EL2_T(x)	(1 << x)

/* Hyp Coprocessor Trap Register Shifts */
#define CPTR_EL2_TFP_SHIFT 10

/* Hyp Coprocessor Trap Register */
#define CPTR_EL2_TCPAC	(1U << 31)
#define CPTR_EL2_TAM	(1 << 30)
#define CPTR_EL2_TTA	(1 << 20)
#define CPTR_EL2_TSM	(1 << 12)
#define CPTR_EL2_TFP	(1 << CPTR_EL2_TFP_SHIFT)
#define CPTR_EL2_TZ	(1 << 8)
#define CPTR_NVHE_EL2_RES1	0x000032ff /* known RES1 bits in CPTR_EL2 (nVHE) */
#define CPTR_NVHE_EL2_RES0	(GENMASK(63, 32) |	\
				 GENMASK(29, 21) |	\
				 GENMASK(19, 14) |	\
				 BIT(11))

/* Hyp Debug Configuration Register bits */
#define MDCR_EL2_E2TB_MASK	(UL(0x3))
#define MDCR_EL2_E2TB_SHIFT	(UL(24))
#define MDCR_EL2_HPMFZS		(UL(1) << 36)
#define MDCR_EL2_HPMFZO		(UL(1) << 29)
#define MDCR_EL2_MTPME		(UL(1) << 28)
#define MDCR_EL2_TDCC		(UL(1) << 27)
#define MDCR_EL2_HLP		(UL(1) << 26)
#define MDCR_EL2_HCCD		(UL(1) << 23)
#define MDCR_EL2_TTRF		(UL(1) << 19)
#define MDCR_EL2_HPMD		(UL(1) << 17)
#define MDCR_EL2_TPMS		(UL(1) << 14)
#define MDCR_EL2_E2PB_MASK	(UL(0x3))
#define MDCR_EL2_E2PB_SHIFT	(UL(12))
#define MDCR_EL2_TDRA		(UL(1) << 11)
#define MDCR_EL2_TDOSA		(UL(1) << 10)
#define MDCR_EL2_TDA		(UL(1) << 9)
#define MDCR_EL2_TDE		(UL(1) << 8)
#define MDCR_EL2_HPME		(UL(1) << 7)
#define MDCR_EL2_TPM		(UL(1) << 6)
#define MDCR_EL2_TPMCR		(UL(1) << 5)
#define MDCR_EL2_HPMN_MASK	(UL(0x1F))
#define MDCR_EL2_RES0		(GENMASK(63, 37) |	\
				 GENMASK(35, 30) |	\
				 GENMASK(25, 24) |	\
				 GENMASK(22, 20) |	\
				 BIT(18) |		\
				 GENMASK(16, 15))

/* Hyp Prefetch Fault Address Register (HPFAR/HDFAR) */
#define HPFAR_MASK	(~UL(0xf))
/*
 * We have
 *	PAR	[PA_Shift - 1	: 12] = PA	[PA_Shift - 1 : 12]
 *	HPFAR	[PA_Shift - 9	: 4]  = FIPA	[PA_Shift - 1 : 12]
 *
 * Always assume 52 bit PA since at this point, we don't know how many PA bits
 * the page table has been set up for. This should be safe since unused address
 * bits in PAR are res0.
 */
#define PAR_TO_HPFAR(par)		\
	(((par) & GENMASK_ULL(52 - 1, 12)) >> 8)

#define ECN(x) { ESR_ELx_EC_##x, #x }

#define kvm_arm_exception_class \
	ECN(UNKNOWN), ECN(WFx), ECN(CP15_32), ECN(CP15_64), ECN(CP14_MR), \
	ECN(CP14_LS), ECN(FP_ASIMD), ECN(CP10_ID), ECN(PAC), ECN(CP14_64), \
	ECN(SVC64), ECN(HVC64), ECN(SMC64), ECN(SYS64), ECN(SVE), \
	ECN(IMP_DEF), ECN(IABT_LOW), ECN(IABT_CUR), \
	ECN(PC_ALIGN), ECN(DABT_LOW), ECN(DABT_CUR), \
	ECN(SP_ALIGN), ECN(FP_EXC32), ECN(FP_EXC64), ECN(SERROR), \
	ECN(BREAKPT_LOW), ECN(BREAKPT_CUR), ECN(SOFTSTP_LOW), \
	ECN(SOFTSTP_CUR), ECN(WATCHPT_LOW), ECN(WATCHPT_CUR), \
	ECN(BKPT32), ECN(VECTOR32), ECN(BRK64), ECN(ERET)

#define CPACR_EL1_TTA		(1 << 28)

#define kvm_mode_names				\
	{ PSR_MODE_EL0t,	"EL0t" },	\
	{ PSR_MODE_EL1t,	"EL1t" },	\
	{ PSR_MODE_EL1h,	"EL1h" },	\
	{ PSR_MODE_EL2t,	"EL2t" },	\
	{ PSR_MODE_EL2h,	"EL2h" },	\
	{ PSR_MODE_EL3t,	"EL3t" },	\
	{ PSR_MODE_EL3h,	"EL3h" },	\
	{ PSR_AA32_MODE_USR,	"32-bit USR" },	\
	{ PSR_AA32_MODE_FIQ,	"32-bit FIQ" },	\
	{ PSR_AA32_MODE_IRQ,	"32-bit IRQ" },	\
	{ PSR_AA32_MODE_SVC,	"32-bit SVC" },	\
	{ PSR_AA32_MODE_ABT,	"32-bit ABT" },	\
	{ PSR_AA32_MODE_HYP,	"32-bit HYP" },	\
	{ PSR_AA32_MODE_UND,	"32-bit UND" },	\
	{ PSR_AA32_MODE_SYS,	"32-bit SYS" }

#endif /* __ARM64_KVM_ARM_H__ */
