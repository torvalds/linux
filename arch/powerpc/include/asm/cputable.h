/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __ASM_POWERPC_CPUTABLE_H
#define __ASM_POWERPC_CPUTABLE_H


#include <linux/types.h>
#include <uapi/asm/cputable.h>
#include <asm/asm-const.h>

#ifndef __ASSEMBLY__

/* This structure can grow, it's real size is used by head.S code
 * via the mkdefs mechanism.
 */
struct cpu_spec;

typedef	void (*cpu_setup_t)(unsigned long offset, struct cpu_spec* spec);
typedef	void (*cpu_restore_t)(void);

enum powerpc_oprofile_type {
	PPC_OPROFILE_INVALID = 0,
	PPC_OPROFILE_RS64 = 1,
	PPC_OPROFILE_POWER4 = 2,
	PPC_OPROFILE_G4 = 3,
	PPC_OPROFILE_FSL_EMB = 4,
	PPC_OPROFILE_CELL = 5,
	PPC_OPROFILE_PA6T = 6,
};

enum powerpc_pmc_type {
	PPC_PMC_DEFAULT = 0,
	PPC_PMC_IBM = 1,
	PPC_PMC_PA6T = 2,
	PPC_PMC_G4 = 3,
};

struct pt_regs;

extern int machine_check_generic(struct pt_regs *regs);
extern int machine_check_4xx(struct pt_regs *regs);
extern int machine_check_440A(struct pt_regs *regs);
extern int machine_check_e500mc(struct pt_regs *regs);
extern int machine_check_e500(struct pt_regs *regs);
extern int machine_check_e200(struct pt_regs *regs);
extern int machine_check_47x(struct pt_regs *regs);
int machine_check_8xx(struct pt_regs *regs);

extern void cpu_down_flush_e500v2(void);
extern void cpu_down_flush_e500mc(void);
extern void cpu_down_flush_e5500(void);
extern void cpu_down_flush_e6500(void);

/* NOTE WELL: Update identify_cpu() if fields are added or removed! */
struct cpu_spec {
	/* CPU is matched via (PVR & pvr_mask) == pvr_value */
	unsigned int	pvr_mask;
	unsigned int	pvr_value;

	char		*cpu_name;
	unsigned long	cpu_features;		/* Kernel features */
	unsigned int	cpu_user_features;	/* Userland features */
	unsigned int	cpu_user_features2;	/* Userland features v2 */
	unsigned int	mmu_features;		/* MMU features */

	/* cache line sizes */
	unsigned int	icache_bsize;
	unsigned int	dcache_bsize;

	/* flush caches inside the current cpu */
	void (*cpu_down_flush)(void);

	/* number of performance monitor counters */
	unsigned int	num_pmcs;
	enum powerpc_pmc_type pmc_type;

	/* this is called to initialize various CPU bits like L1 cache,
	 * BHT, SPD, etc... from head.S before branching to identify_machine
	 */
	cpu_setup_t	cpu_setup;
	/* Used to restore cpu setup on secondary processors and at resume */
	cpu_restore_t	cpu_restore;

	/* Used by oprofile userspace to select the right counters */
	char		*oprofile_cpu_type;

	/* Processor specific oprofile operations */
	enum powerpc_oprofile_type oprofile_type;

	/* Bit locations inside the mmcra change */
	unsigned long	oprofile_mmcra_sihv;
	unsigned long	oprofile_mmcra_sipr;

	/* Bits to clear during an oprofile exception */
	unsigned long	oprofile_mmcra_clear;

	/* Name of processor class, for the ELF AT_PLATFORM entry */
	char		*platform;

	/* Processor specific machine check handling. Return negative
	 * if the error is fatal, 1 if it was fully recovered and 0 to
	 * pass up (not CPU originated) */
	int		(*machine_check)(struct pt_regs *regs);

	/*
	 * Processor specific early machine check handler which is
	 * called in real mode to handle SLB and TLB errors.
	 */
	long		(*machine_check_early)(struct pt_regs *regs);
};

extern struct cpu_spec		*cur_cpu_spec;

extern unsigned int __start___ftr_fixup, __stop___ftr_fixup;

extern void set_cur_cpu_spec(struct cpu_spec *s);
extern struct cpu_spec *identify_cpu(unsigned long offset, unsigned int pvr);
extern void identify_cpu_name(unsigned int pvr);
extern void do_feature_fixups(unsigned long value, void *fixup_start,
			      void *fixup_end);

extern const char *powerpc_base_platform;

#ifdef CONFIG_JUMP_LABEL_FEATURE_CHECKS
extern void cpu_feature_keys_init(void);
#else
static inline void cpu_feature_keys_init(void) { }
#endif

#endif /* __ASSEMBLY__ */

/* CPU kernel features */

/* Definitions for features that we have on both 32-bit and 64-bit chips */
#define CPU_FTR_COHERENT_ICACHE		ASM_CONST(0x00000001)
#define CPU_FTR_ALTIVEC			ASM_CONST(0x00000002)
#define CPU_FTR_DBELL			ASM_CONST(0x00000004)
#define CPU_FTR_CAN_NAP			ASM_CONST(0x00000008)
#define CPU_FTR_DEBUG_LVL_EXC		ASM_CONST(0x00000010)
#define CPU_FTR_NODSISRALIGN		ASM_CONST(0x00000020)
#define CPU_FTR_FPU_UNAVAILABLE		ASM_CONST(0x00000040)
#define CPU_FTR_LWSYNC			ASM_CONST(0x00000080)
#define CPU_FTR_NOEXECUTE		ASM_CONST(0x00000100)
#define CPU_FTR_EMB_HV			ASM_CONST(0x00000200)

/* Definitions for features that only exist on 32-bit chips */
#ifdef CONFIG_PPC32
#define CPU_FTR_601			ASM_CONST(0x00001000)
#define CPU_FTR_L2CR			ASM_CONST(0x00002000)
#define CPU_FTR_SPEC7450		ASM_CONST(0x00004000)
#define CPU_FTR_TAU			ASM_CONST(0x00008000)
#define CPU_FTR_CAN_DOZE		ASM_CONST(0x00010000)
#define CPU_FTR_USE_RTC			ASM_CONST(0x00020000)
#define CPU_FTR_L3CR			ASM_CONST(0x00040000)
#define CPU_FTR_L3_DISABLE_NAP		ASM_CONST(0x00080000)
#define CPU_FTR_NAP_DISABLE_L2_PR	ASM_CONST(0x00100000)
#define CPU_FTR_DUAL_PLL_750FX		ASM_CONST(0x00200000)
#define CPU_FTR_NO_DPM			ASM_CONST(0x00400000)
#define CPU_FTR_476_DD2			ASM_CONST(0x00800000)
#define CPU_FTR_NEED_COHERENT		ASM_CONST(0x01000000)
#define CPU_FTR_NO_BTIC			ASM_CONST(0x02000000)
#define CPU_FTR_PPC_LE			ASM_CONST(0x04000000)
#define CPU_FTR_UNIFIED_ID_CACHE	ASM_CONST(0x08000000)
#define CPU_FTR_SPE			ASM_CONST(0x10000000)
#define CPU_FTR_NEED_PAIRED_STWCX	ASM_CONST(0x20000000)
#define CPU_FTR_INDEXED_DCR		ASM_CONST(0x40000000)

#else	/* CONFIG_PPC32 */
/* Define these to 0 for the sake of tests in common code */
#define CPU_FTR_601			(0)
#define CPU_FTR_PPC_LE			(0)
#endif

/*
 * Definitions for the 64-bit processor unique features;
 * on 32-bit, make the names available but defined to be 0.
 */
#ifdef __powerpc64__
#define LONG_ASM_CONST(x)		ASM_CONST(x)
#else
#define LONG_ASM_CONST(x)		0
#endif

#define CPU_FTR_REAL_LE			LONG_ASM_CONST(0x0000000000001000)
#define CPU_FTR_HVMODE			LONG_ASM_CONST(0x0000000000002000)
#define CPU_FTR_ARCH_206		LONG_ASM_CONST(0x0000000000008000)
#define CPU_FTR_ARCH_207S		LONG_ASM_CONST(0x0000000000010000)
#define CPU_FTR_ARCH_300		LONG_ASM_CONST(0x0000000000020000)
#define CPU_FTR_MMCRA			LONG_ASM_CONST(0x0000000000040000)
#define CPU_FTR_CTRL			LONG_ASM_CONST(0x0000000000080000)
#define CPU_FTR_SMT			LONG_ASM_CONST(0x0000000000100000)
#define CPU_FTR_PAUSE_ZERO		LONG_ASM_CONST(0x0000000000200000)
#define CPU_FTR_PURR			LONG_ASM_CONST(0x0000000000400000)
#define CPU_FTR_CELL_TB_BUG		LONG_ASM_CONST(0x0000000000800000)
#define CPU_FTR_SPURR			LONG_ASM_CONST(0x0000000001000000)
#define CPU_FTR_DSCR			LONG_ASM_CONST(0x0000000002000000)
#define CPU_FTR_VSX			LONG_ASM_CONST(0x0000000004000000)
#define CPU_FTR_SAO			LONG_ASM_CONST(0x0000000008000000)
#define CPU_FTR_CP_USE_DCBTZ		LONG_ASM_CONST(0x0000000010000000)
#define CPU_FTR_UNALIGNED_LD_STD	LONG_ASM_CONST(0x0000000020000000)
#define CPU_FTR_ASYM_SMT		LONG_ASM_CONST(0x0000000040000000)
#define CPU_FTR_STCX_CHECKS_ADDRESS	LONG_ASM_CONST(0x0000000080000000)
#define CPU_FTR_POPCNTB			LONG_ASM_CONST(0x0000000100000000)
#define CPU_FTR_POPCNTD			LONG_ASM_CONST(0x0000000200000000)
#define CPU_FTR_PKEY			LONG_ASM_CONST(0x0000000400000000)
#define CPU_FTR_VMX_COPY		LONG_ASM_CONST(0x0000000800000000)
#define CPU_FTR_TM			LONG_ASM_CONST(0x0000001000000000)
#define CPU_FTR_CFAR			LONG_ASM_CONST(0x0000002000000000)
#define	CPU_FTR_HAS_PPR			LONG_ASM_CONST(0x0000004000000000)
#define CPU_FTR_DAWR			LONG_ASM_CONST(0x0000008000000000)
#define CPU_FTR_DABRX			LONG_ASM_CONST(0x0000010000000000)
#define CPU_FTR_PMAO_BUG		LONG_ASM_CONST(0x0000020000000000)
#define CPU_FTR_POWER9_DD2_1		LONG_ASM_CONST(0x0000080000000000)
#define CPU_FTR_P9_TM_HV_ASSIST		LONG_ASM_CONST(0x0000100000000000)
#define CPU_FTR_P9_TM_XER_SO_BUG	LONG_ASM_CONST(0x0000200000000000)
#define CPU_FTR_P9_TLBIE_STQ_BUG	LONG_ASM_CONST(0x0000400000000000)
#define CPU_FTR_P9_TIDR			LONG_ASM_CONST(0x0000800000000000)

#ifndef __ASSEMBLY__

#define CPU_FTR_PPCAS_ARCH_V2	(CPU_FTR_NOEXECUTE | CPU_FTR_NODSISRALIGN)

#define MMU_FTR_PPCAS_ARCH_V2 	(MMU_FTR_TLBIEL | MMU_FTR_16M_PAGE)

/* We only set the altivec features if the kernel was compiled with altivec
 * support
 */
#ifdef CONFIG_ALTIVEC
#define CPU_FTR_ALTIVEC_COMP	CPU_FTR_ALTIVEC
#define PPC_FEATURE_HAS_ALTIVEC_COMP PPC_FEATURE_HAS_ALTIVEC
#else
#define CPU_FTR_ALTIVEC_COMP	0
#define PPC_FEATURE_HAS_ALTIVEC_COMP    0
#endif

/* We only set the VSX features if the kernel was compiled with VSX
 * support
 */
#ifdef CONFIG_VSX
#define CPU_FTR_VSX_COMP	CPU_FTR_VSX
#define PPC_FEATURE_HAS_VSX_COMP PPC_FEATURE_HAS_VSX
#else
#define CPU_FTR_VSX_COMP	0
#define PPC_FEATURE_HAS_VSX_COMP    0
#endif

/* We only set the spe features if the kernel was compiled with spe
 * support
 */
#ifdef CONFIG_SPE
#define CPU_FTR_SPE_COMP	CPU_FTR_SPE
#define PPC_FEATURE_HAS_SPE_COMP PPC_FEATURE_HAS_SPE
#define PPC_FEATURE_HAS_EFP_SINGLE_COMP PPC_FEATURE_HAS_EFP_SINGLE
#define PPC_FEATURE_HAS_EFP_DOUBLE_COMP PPC_FEATURE_HAS_EFP_DOUBLE
#else
#define CPU_FTR_SPE_COMP	0
#define PPC_FEATURE_HAS_SPE_COMP    0
#define PPC_FEATURE_HAS_EFP_SINGLE_COMP 0
#define PPC_FEATURE_HAS_EFP_DOUBLE_COMP 0
#endif

/* We only set the TM feature if the kernel was compiled with TM supprt */
#ifdef CONFIG_PPC_TRANSACTIONAL_MEM
#define CPU_FTR_TM_COMP			CPU_FTR_TM
#define PPC_FEATURE2_HTM_COMP		PPC_FEATURE2_HTM
#define PPC_FEATURE2_HTM_NOSC_COMP	PPC_FEATURE2_HTM_NOSC
#else
#define CPU_FTR_TM_COMP			0
#define PPC_FEATURE2_HTM_COMP		0
#define PPC_FEATURE2_HTM_NOSC_COMP	0
#endif

/* We need to mark all pages as being coherent if we're SMP or we have a
 * 74[45]x and an MPC107 host bridge. Also 83xx and PowerQUICC II
 * require it for PCI "streaming/prefetch" to work properly.
 * This is also required by 52xx family.
 */
#if defined(CONFIG_SMP) || defined(CONFIG_MPC10X_BRIDGE) \
	|| defined(CONFIG_PPC_83xx) || defined(CONFIG_8260) \
	|| defined(CONFIG_PPC_MPC52xx)
#define CPU_FTR_COMMON                  CPU_FTR_NEED_COHERENT
#else
#define CPU_FTR_COMMON                  0
#endif

/* The powersave features NAP & DOZE seems to confuse BDI when
   debugging. So if a BDI is used, disable theses
 */
#ifndef CONFIG_BDI_SWITCH
#define CPU_FTR_MAYBE_CAN_DOZE	CPU_FTR_CAN_DOZE
#define CPU_FTR_MAYBE_CAN_NAP	CPU_FTR_CAN_NAP
#else
#define CPU_FTR_MAYBE_CAN_DOZE	0
#define CPU_FTR_MAYBE_CAN_NAP	0
#endif

#define CPU_FTRS_PPC601	(CPU_FTR_COMMON | CPU_FTR_601 | \
	CPU_FTR_COHERENT_ICACHE | CPU_FTR_UNIFIED_ID_CACHE | CPU_FTR_USE_RTC)
#define CPU_FTRS_603	(CPU_FTR_COMMON | CPU_FTR_MAYBE_CAN_DOZE | \
	    CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_PPC_LE)
#define CPU_FTRS_604	(CPU_FTR_COMMON | CPU_FTR_PPC_LE)
#define CPU_FTRS_740_NOTAU	(CPU_FTR_COMMON | \
	    CPU_FTR_MAYBE_CAN_DOZE | CPU_FTR_L2CR | \
	    CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_PPC_LE)
#define CPU_FTRS_740	(CPU_FTR_COMMON | \
	    CPU_FTR_MAYBE_CAN_DOZE | CPU_FTR_L2CR | \
	    CPU_FTR_TAU | CPU_FTR_MAYBE_CAN_NAP | \
	    CPU_FTR_PPC_LE)
#define CPU_FTRS_750	(CPU_FTR_COMMON | \
	    CPU_FTR_MAYBE_CAN_DOZE | CPU_FTR_L2CR | \
	    CPU_FTR_TAU | CPU_FTR_MAYBE_CAN_NAP | \
	    CPU_FTR_PPC_LE)
#define CPU_FTRS_750CL	(CPU_FTRS_750)
#define CPU_FTRS_750FX1	(CPU_FTRS_750 | CPU_FTR_DUAL_PLL_750FX | CPU_FTR_NO_DPM)
#define CPU_FTRS_750FX2	(CPU_FTRS_750 | CPU_FTR_NO_DPM)
#define CPU_FTRS_750FX	(CPU_FTRS_750 | CPU_FTR_DUAL_PLL_750FX)
#define CPU_FTRS_750GX	(CPU_FTRS_750FX)
#define CPU_FTRS_7400_NOTAU	(CPU_FTR_COMMON | \
	    CPU_FTR_MAYBE_CAN_DOZE | CPU_FTR_L2CR | \
	    CPU_FTR_ALTIVEC_COMP | \
	    CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_PPC_LE)
#define CPU_FTRS_7400	(CPU_FTR_COMMON | \
	    CPU_FTR_MAYBE_CAN_DOZE | CPU_FTR_L2CR | \
	    CPU_FTR_TAU | CPU_FTR_ALTIVEC_COMP | \
	    CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_PPC_LE)
#define CPU_FTRS_7450_20	(CPU_FTR_COMMON | \
	    CPU_FTR_L2CR | CPU_FTR_ALTIVEC_COMP | \
	    CPU_FTR_L3CR | CPU_FTR_SPEC7450 | \
	    CPU_FTR_NEED_COHERENT | CPU_FTR_PPC_LE | CPU_FTR_NEED_PAIRED_STWCX)
#define CPU_FTRS_7450_21	(CPU_FTR_COMMON | \
	    CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR | CPU_FTR_ALTIVEC_COMP | \
	    CPU_FTR_L3CR | CPU_FTR_SPEC7450 | \
	    CPU_FTR_NAP_DISABLE_L2_PR | CPU_FTR_L3_DISABLE_NAP | \
	    CPU_FTR_NEED_COHERENT | CPU_FTR_PPC_LE | CPU_FTR_NEED_PAIRED_STWCX)
#define CPU_FTRS_7450_23	(CPU_FTR_COMMON | \
	    CPU_FTR_NEED_PAIRED_STWCX | \
	    CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR | CPU_FTR_ALTIVEC_COMP | \
	    CPU_FTR_L3CR | CPU_FTR_SPEC7450 | \
	    CPU_FTR_NAP_DISABLE_L2_PR | CPU_FTR_NEED_COHERENT | CPU_FTR_PPC_LE)
#define CPU_FTRS_7455_1	(CPU_FTR_COMMON | \
	    CPU_FTR_NEED_PAIRED_STWCX | \
	    CPU_FTR_L2CR | CPU_FTR_ALTIVEC_COMP | CPU_FTR_L3CR | \
	    CPU_FTR_SPEC7450 | CPU_FTR_NEED_COHERENT | CPU_FTR_PPC_LE)
#define CPU_FTRS_7455_20	(CPU_FTR_COMMON | \
	    CPU_FTR_NEED_PAIRED_STWCX | \
	    CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR | CPU_FTR_ALTIVEC_COMP | \
	    CPU_FTR_L3CR | CPU_FTR_SPEC7450 | \
	    CPU_FTR_NAP_DISABLE_L2_PR | CPU_FTR_L3_DISABLE_NAP | \
	    CPU_FTR_NEED_COHERENT | CPU_FTR_PPC_LE)
#define CPU_FTRS_7455	(CPU_FTR_COMMON | \
	    CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR | CPU_FTR_ALTIVEC_COMP | \
	    CPU_FTR_L3CR | CPU_FTR_SPEC7450 | CPU_FTR_NAP_DISABLE_L2_PR | \
	    CPU_FTR_NEED_COHERENT | CPU_FTR_PPC_LE | CPU_FTR_NEED_PAIRED_STWCX)
#define CPU_FTRS_7447_10	(CPU_FTR_COMMON | \
	    CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR | CPU_FTR_ALTIVEC_COMP | \
	    CPU_FTR_L3CR | CPU_FTR_SPEC7450 | CPU_FTR_NAP_DISABLE_L2_PR | \
	    CPU_FTR_NEED_COHERENT | CPU_FTR_NO_BTIC | CPU_FTR_PPC_LE | \
	    CPU_FTR_NEED_PAIRED_STWCX)
#define CPU_FTRS_7447	(CPU_FTR_COMMON | \
	    CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR | CPU_FTR_ALTIVEC_COMP | \
	    CPU_FTR_L3CR | CPU_FTR_SPEC7450 | CPU_FTR_NAP_DISABLE_L2_PR | \
	    CPU_FTR_NEED_COHERENT | CPU_FTR_PPC_LE | CPU_FTR_NEED_PAIRED_STWCX)
#define CPU_FTRS_7447A	(CPU_FTR_COMMON | \
	    CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR | CPU_FTR_ALTIVEC_COMP | \
	    CPU_FTR_SPEC7450 | CPU_FTR_NAP_DISABLE_L2_PR | \
	    CPU_FTR_NEED_COHERENT | CPU_FTR_PPC_LE | CPU_FTR_NEED_PAIRED_STWCX)
#define CPU_FTRS_7448	(CPU_FTR_COMMON | \
	    CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_L2CR | CPU_FTR_ALTIVEC_COMP | \
	    CPU_FTR_SPEC7450 | CPU_FTR_NAP_DISABLE_L2_PR | \
	    CPU_FTR_PPC_LE | CPU_FTR_NEED_PAIRED_STWCX)
#define CPU_FTRS_82XX	(CPU_FTR_COMMON | CPU_FTR_MAYBE_CAN_DOZE)
#define CPU_FTRS_G2_LE	(CPU_FTR_COMMON | CPU_FTR_MAYBE_CAN_DOZE | \
	    CPU_FTR_MAYBE_CAN_NAP)
#define CPU_FTRS_E300	(CPU_FTR_MAYBE_CAN_DOZE | \
	    CPU_FTR_MAYBE_CAN_NAP | \
	    CPU_FTR_COMMON)
#define CPU_FTRS_E300C2	(CPU_FTR_MAYBE_CAN_DOZE | \
	    CPU_FTR_MAYBE_CAN_NAP | \
	    CPU_FTR_COMMON | CPU_FTR_FPU_UNAVAILABLE)
#define CPU_FTRS_CLASSIC32	(CPU_FTR_COMMON)
#define CPU_FTRS_8XX	(CPU_FTR_NOEXECUTE)
#define CPU_FTRS_40X	(CPU_FTR_NODSISRALIGN | CPU_FTR_NOEXECUTE)
#define CPU_FTRS_44X	(CPU_FTR_NODSISRALIGN | CPU_FTR_NOEXECUTE)
#define CPU_FTRS_440x6	(CPU_FTR_NODSISRALIGN | CPU_FTR_NOEXECUTE | \
	    CPU_FTR_INDEXED_DCR)
#define CPU_FTRS_47X	(CPU_FTRS_440x6)
#define CPU_FTRS_E200	(CPU_FTR_SPE_COMP | \
	    CPU_FTR_NODSISRALIGN | CPU_FTR_COHERENT_ICACHE | \
	    CPU_FTR_UNIFIED_ID_CACHE | CPU_FTR_NOEXECUTE | \
	    CPU_FTR_DEBUG_LVL_EXC)
#define CPU_FTRS_E500	(CPU_FTR_MAYBE_CAN_DOZE | \
	    CPU_FTR_SPE_COMP | CPU_FTR_MAYBE_CAN_NAP | CPU_FTR_NODSISRALIGN | \
	    CPU_FTR_NOEXECUTE)
#define CPU_FTRS_E500_2	(CPU_FTR_MAYBE_CAN_DOZE | \
	    CPU_FTR_SPE_COMP | CPU_FTR_MAYBE_CAN_NAP | \
	    CPU_FTR_NODSISRALIGN | CPU_FTR_NOEXECUTE)
#define CPU_FTRS_E500MC	(CPU_FTR_NODSISRALIGN | \
	    CPU_FTR_LWSYNC | CPU_FTR_NOEXECUTE | \
	    CPU_FTR_DBELL | CPU_FTR_DEBUG_LVL_EXC | CPU_FTR_EMB_HV)
/*
 * e5500/e6500 erratum A-006958 is a timebase bug that can use the
 * same workaround as CPU_FTR_CELL_TB_BUG.
 */
#define CPU_FTRS_E5500	(CPU_FTR_NODSISRALIGN | \
	    CPU_FTR_LWSYNC | CPU_FTR_NOEXECUTE | \
	    CPU_FTR_DBELL | CPU_FTR_POPCNTB | CPU_FTR_POPCNTD | \
	    CPU_FTR_DEBUG_LVL_EXC | CPU_FTR_EMB_HV | CPU_FTR_CELL_TB_BUG)
#define CPU_FTRS_E6500	(CPU_FTR_NODSISRALIGN | \
	    CPU_FTR_LWSYNC | CPU_FTR_NOEXECUTE | \
	    CPU_FTR_DBELL | CPU_FTR_POPCNTB | CPU_FTR_POPCNTD | \
	    CPU_FTR_DEBUG_LVL_EXC | CPU_FTR_EMB_HV | CPU_FTR_ALTIVEC_COMP | \
	    CPU_FTR_CELL_TB_BUG | CPU_FTR_SMT)
#define CPU_FTRS_GENERIC_32	(CPU_FTR_COMMON | CPU_FTR_NODSISRALIGN)

/* 64-bit CPUs */
#define CPU_FTRS_PPC970	(CPU_FTR_LWSYNC | \
	    CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_CTRL | \
	    CPU_FTR_ALTIVEC_COMP | CPU_FTR_CAN_NAP | CPU_FTR_MMCRA | \
	    CPU_FTR_CP_USE_DCBTZ | CPU_FTR_STCX_CHECKS_ADDRESS | \
	    CPU_FTR_HVMODE | CPU_FTR_DABRX)
#define CPU_FTRS_POWER5	(CPU_FTR_LWSYNC | \
	    CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_CTRL | \
	    CPU_FTR_MMCRA | CPU_FTR_SMT | \
	    CPU_FTR_COHERENT_ICACHE | CPU_FTR_PURR | \
	    CPU_FTR_STCX_CHECKS_ADDRESS | CPU_FTR_POPCNTB | CPU_FTR_DABRX)
#define CPU_FTRS_POWER6 (CPU_FTR_LWSYNC | \
	    CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_CTRL | \
	    CPU_FTR_MMCRA | CPU_FTR_SMT | \
	    CPU_FTR_COHERENT_ICACHE | \
	    CPU_FTR_PURR | CPU_FTR_SPURR | CPU_FTR_REAL_LE | \
	    CPU_FTR_DSCR | CPU_FTR_UNALIGNED_LD_STD | \
	    CPU_FTR_STCX_CHECKS_ADDRESS | CPU_FTR_POPCNTB | CPU_FTR_CFAR | \
	    CPU_FTR_DABRX)
#define CPU_FTRS_POWER7 (CPU_FTR_LWSYNC | \
	    CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_CTRL | CPU_FTR_ARCH_206 |\
	    CPU_FTR_MMCRA | CPU_FTR_SMT | \
	    CPU_FTR_COHERENT_ICACHE | \
	    CPU_FTR_PURR | CPU_FTR_SPURR | CPU_FTR_REAL_LE | \
	    CPU_FTR_DSCR | CPU_FTR_SAO  | CPU_FTR_ASYM_SMT | \
	    CPU_FTR_STCX_CHECKS_ADDRESS | CPU_FTR_POPCNTB | CPU_FTR_POPCNTD | \
	    CPU_FTR_CFAR | CPU_FTR_HVMODE | \
	    CPU_FTR_VMX_COPY | CPU_FTR_HAS_PPR | CPU_FTR_DABRX | CPU_FTR_PKEY)
#define CPU_FTRS_POWER8 (CPU_FTR_LWSYNC | \
	    CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_CTRL | CPU_FTR_ARCH_206 |\
	    CPU_FTR_MMCRA | CPU_FTR_SMT | \
	    CPU_FTR_COHERENT_ICACHE | \
	    CPU_FTR_PURR | CPU_FTR_SPURR | CPU_FTR_REAL_LE | \
	    CPU_FTR_DSCR | CPU_FTR_SAO  | \
	    CPU_FTR_STCX_CHECKS_ADDRESS | CPU_FTR_POPCNTB | CPU_FTR_POPCNTD | \
	    CPU_FTR_CFAR | CPU_FTR_HVMODE | CPU_FTR_VMX_COPY | \
	    CPU_FTR_DBELL | CPU_FTR_HAS_PPR | CPU_FTR_DAWR | \
	    CPU_FTR_ARCH_207S | CPU_FTR_TM_COMP | CPU_FTR_PKEY)
#define CPU_FTRS_POWER8E (CPU_FTRS_POWER8 | CPU_FTR_PMAO_BUG)
#define CPU_FTRS_POWER9 (CPU_FTR_LWSYNC | \
	    CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_CTRL | CPU_FTR_ARCH_206 |\
	    CPU_FTR_MMCRA | CPU_FTR_SMT | \
	    CPU_FTR_COHERENT_ICACHE | \
	    CPU_FTR_PURR | CPU_FTR_SPURR | CPU_FTR_REAL_LE | \
	    CPU_FTR_DSCR | CPU_FTR_SAO  | \
	    CPU_FTR_STCX_CHECKS_ADDRESS | CPU_FTR_POPCNTB | CPU_FTR_POPCNTD | \
	    CPU_FTR_CFAR | CPU_FTR_HVMODE | CPU_FTR_VMX_COPY | \
	    CPU_FTR_DBELL | CPU_FTR_HAS_PPR | CPU_FTR_ARCH_207S | \
	    CPU_FTR_TM_COMP | CPU_FTR_ARCH_300 | CPU_FTR_PKEY | \
	    CPU_FTR_P9_TLBIE_STQ_BUG | CPU_FTR_P9_TIDR)
#define CPU_FTRS_POWER9_DD2_0 CPU_FTRS_POWER9
#define CPU_FTRS_POWER9_DD2_1 (CPU_FTRS_POWER9 | CPU_FTR_POWER9_DD2_1)
#define CPU_FTRS_POWER9_DD2_2 (CPU_FTRS_POWER9 | CPU_FTR_POWER9_DD2_1 | \
			       CPU_FTR_P9_TM_HV_ASSIST | \
			       CPU_FTR_P9_TM_XER_SO_BUG)
#define CPU_FTRS_CELL	(CPU_FTR_LWSYNC | \
	    CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_CTRL | \
	    CPU_FTR_ALTIVEC_COMP | CPU_FTR_MMCRA | CPU_FTR_SMT | \
	    CPU_FTR_PAUSE_ZERO  | CPU_FTR_CELL_TB_BUG | CPU_FTR_CP_USE_DCBTZ | \
	    CPU_FTR_UNALIGNED_LD_STD | CPU_FTR_DABRX)
#define CPU_FTRS_PA6T (CPU_FTR_LWSYNC | \
	    CPU_FTR_PPCAS_ARCH_V2 | CPU_FTR_ALTIVEC_COMP | \
	    CPU_FTR_PURR | CPU_FTR_REAL_LE | CPU_FTR_DABRX)
#define CPU_FTRS_COMPATIBLE	(CPU_FTR_PPCAS_ARCH_V2)

#ifdef __powerpc64__
#ifdef CONFIG_PPC_BOOK3E
#define CPU_FTRS_POSSIBLE	(CPU_FTRS_E6500 | CPU_FTRS_E5500)
#else
#ifdef CONFIG_CPU_LITTLE_ENDIAN
#define CPU_FTRS_POSSIBLE	\
	    (CPU_FTRS_POWER7 | CPU_FTRS_POWER8E | CPU_FTRS_POWER8 | \
	     CPU_FTR_ALTIVEC_COMP | CPU_FTR_VSX_COMP | CPU_FTRS_POWER9 | \
	     CPU_FTRS_POWER9_DD2_1 | CPU_FTRS_POWER9_DD2_2)
#else
#define CPU_FTRS_POSSIBLE	\
	    (CPU_FTRS_PPC970 | CPU_FTRS_POWER5 | \
	     CPU_FTRS_POWER6 | CPU_FTRS_POWER7 | CPU_FTRS_POWER8E | \
	     CPU_FTRS_POWER8 | CPU_FTRS_CELL | CPU_FTRS_PA6T | \
	     CPU_FTR_VSX_COMP | CPU_FTR_ALTIVEC_COMP | CPU_FTRS_POWER9 | \
	     CPU_FTRS_POWER9_DD2_1 | CPU_FTRS_POWER9_DD2_2)
#endif /* CONFIG_CPU_LITTLE_ENDIAN */
#endif
#else
enum {
	CPU_FTRS_POSSIBLE =
#ifdef CONFIG_PPC_BOOK3S_32
	    CPU_FTRS_PPC601 | CPU_FTRS_603 | CPU_FTRS_604 | CPU_FTRS_740_NOTAU |
	    CPU_FTRS_740 | CPU_FTRS_750 | CPU_FTRS_750FX1 |
	    CPU_FTRS_750FX2 | CPU_FTRS_750FX | CPU_FTRS_750GX |
	    CPU_FTRS_7400_NOTAU | CPU_FTRS_7400 | CPU_FTRS_7450_20 |
	    CPU_FTRS_7450_21 | CPU_FTRS_7450_23 | CPU_FTRS_7455_1 |
	    CPU_FTRS_7455_20 | CPU_FTRS_7455 | CPU_FTRS_7447_10 |
	    CPU_FTRS_7447 | CPU_FTRS_7447A | CPU_FTRS_82XX |
	    CPU_FTRS_G2_LE | CPU_FTRS_E300 | CPU_FTRS_E300C2 |
	    CPU_FTRS_CLASSIC32 |
#else
	    CPU_FTRS_GENERIC_32 |
#endif
#ifdef CONFIG_PPC_8xx
	    CPU_FTRS_8XX |
#endif
#ifdef CONFIG_40x
	    CPU_FTRS_40X |
#endif
#ifdef CONFIG_44x
	    CPU_FTRS_44X | CPU_FTRS_440x6 |
#endif
#ifdef CONFIG_PPC_47x
	    CPU_FTRS_47X | CPU_FTR_476_DD2 |
#endif
#ifdef CONFIG_E200
	    CPU_FTRS_E200 |
#endif
#ifdef CONFIG_E500
	    CPU_FTRS_E500 | CPU_FTRS_E500_2 |
#endif
#ifdef CONFIG_PPC_E500MC
	    CPU_FTRS_E500MC | CPU_FTRS_E5500 | CPU_FTRS_E6500 |
#endif
	    0,
};
#endif /* __powerpc64__ */

#ifdef __powerpc64__
#ifdef CONFIG_PPC_BOOK3E
#define CPU_FTRS_ALWAYS		(CPU_FTRS_E6500 & CPU_FTRS_E5500)
#else

#ifdef CONFIG_PPC_DT_CPU_FTRS
#define CPU_FTRS_DT_CPU_BASE			\
	(CPU_FTR_LWSYNC |			\
	 CPU_FTR_FPU_UNAVAILABLE |		\
	 CPU_FTR_NODSISRALIGN |			\
	 CPU_FTR_NOEXECUTE |			\
	 CPU_FTR_COHERENT_ICACHE |		\
	 CPU_FTR_STCX_CHECKS_ADDRESS |		\
	 CPU_FTR_POPCNTB | CPU_FTR_POPCNTD |	\
	 CPU_FTR_DAWR |				\
	 CPU_FTR_ARCH_206 |			\
	 CPU_FTR_ARCH_207S)
#else
#define CPU_FTRS_DT_CPU_BASE	(~0ul)
#endif

#ifdef CONFIG_CPU_LITTLE_ENDIAN
#define CPU_FTRS_ALWAYS \
	    (CPU_FTRS_POSSIBLE & ~CPU_FTR_HVMODE & CPU_FTRS_POWER7 & \
	     CPU_FTRS_POWER8E & CPU_FTRS_POWER8 & CPU_FTRS_POWER9 & \
	     CPU_FTRS_POWER9_DD2_1 & CPU_FTRS_DT_CPU_BASE)
#else
#define CPU_FTRS_ALWAYS		\
	    (CPU_FTRS_PPC970 & CPU_FTRS_POWER5 & \
	     CPU_FTRS_POWER6 & CPU_FTRS_POWER7 & CPU_FTRS_CELL & \
	     CPU_FTRS_PA6T & CPU_FTRS_POWER8 & CPU_FTRS_POWER8E & \
	     ~CPU_FTR_HVMODE & CPU_FTRS_POSSIBLE & CPU_FTRS_POWER9 & \
	     CPU_FTRS_POWER9_DD2_1 & CPU_FTRS_DT_CPU_BASE)
#endif /* CONFIG_CPU_LITTLE_ENDIAN */
#endif
#else
enum {
	CPU_FTRS_ALWAYS =
#ifdef CONFIG_PPC_BOOK3S_32
	    CPU_FTRS_PPC601 & CPU_FTRS_603 & CPU_FTRS_604 & CPU_FTRS_740_NOTAU &
	    CPU_FTRS_740 & CPU_FTRS_750 & CPU_FTRS_750FX1 &
	    CPU_FTRS_750FX2 & CPU_FTRS_750FX & CPU_FTRS_750GX &
	    CPU_FTRS_7400_NOTAU & CPU_FTRS_7400 & CPU_FTRS_7450_20 &
	    CPU_FTRS_7450_21 & CPU_FTRS_7450_23 & CPU_FTRS_7455_1 &
	    CPU_FTRS_7455_20 & CPU_FTRS_7455 & CPU_FTRS_7447_10 &
	    CPU_FTRS_7447 & CPU_FTRS_7447A & CPU_FTRS_82XX &
	    CPU_FTRS_G2_LE & CPU_FTRS_E300 & CPU_FTRS_E300C2 &
	    CPU_FTRS_CLASSIC32 &
#else
	    CPU_FTRS_GENERIC_32 &
#endif
#ifdef CONFIG_PPC_8xx
	    CPU_FTRS_8XX &
#endif
#ifdef CONFIG_40x
	    CPU_FTRS_40X &
#endif
#ifdef CONFIG_44x
	    CPU_FTRS_44X & CPU_FTRS_440x6 &
#endif
#ifdef CONFIG_E200
	    CPU_FTRS_E200 &
#endif
#ifdef CONFIG_E500
	    CPU_FTRS_E500 & CPU_FTRS_E500_2 &
#endif
#ifdef CONFIG_PPC_E500MC
	    CPU_FTRS_E500MC & CPU_FTRS_E5500 & CPU_FTRS_E6500 &
#endif
	    ~CPU_FTR_EMB_HV &	/* can be removed at runtime */
	    CPU_FTRS_POSSIBLE,
};
#endif /* __powerpc64__ */

#define HBP_NUM 1

#endif /* !__ASSEMBLY__ */

#endif /* __ASM_POWERPC_CPUTABLE_H */
