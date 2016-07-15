/*
 * Contains the definition of registers common to all PowerPC variants.
 * If a register definition has been changed in a different PowerPC
 * variant, we will case it in #ifndef XXX ... #endif, and have the
 * number used in the Programming Environments Manual For 32-Bit
 * Implementations of the PowerPC Architecture (a.k.a. Green Book) here.
 */

#ifndef _ASM_POWERPC_REG_H
#define _ASM_POWERPC_REG_H
#ifdef __KERNEL__

#include <linux/stringify.h>
#include <asm/cputable.h>

/* Pickup Book E specific registers. */
#if defined(CONFIG_BOOKE) || defined(CONFIG_40x)
#include <asm/reg_booke.h>
#endif /* CONFIG_BOOKE || CONFIG_40x */

#ifdef CONFIG_FSL_EMB_PERFMON
#include <asm/reg_fsl_emb.h>
#endif

#ifdef CONFIG_8xx
#include <asm/reg_8xx.h>
#endif /* CONFIG_8xx */

#define MSR_SF_LG	63              /* Enable 64 bit mode */
#define MSR_ISF_LG	61              /* Interrupt 64b mode valid on 630 */
#define MSR_HV_LG 	60              /* Hypervisor state */
#define MSR_TS_T_LG	34		/* Trans Mem state: Transactional */
#define MSR_TS_S_LG	33		/* Trans Mem state: Suspended */
#define MSR_TS_LG	33		/* Trans Mem state (2 bits) */
#define MSR_TM_LG	32		/* Trans Mem Available */
#define MSR_VEC_LG	25	        /* Enable AltiVec */
#define MSR_VSX_LG	23		/* Enable VSX */
#define MSR_POW_LG	18		/* Enable Power Management */
#define MSR_WE_LG	18		/* Wait State Enable */
#define MSR_TGPR_LG	17		/* TLB Update registers in use */
#define MSR_CE_LG	17		/* Critical Interrupt Enable */
#define MSR_ILE_LG	16		/* Interrupt Little Endian */
#define MSR_EE_LG	15		/* External Interrupt Enable */
#define MSR_PR_LG	14		/* Problem State / Privilege Level */
#define MSR_FP_LG	13		/* Floating Point enable */
#define MSR_ME_LG	12		/* Machine Check Enable */
#define MSR_FE0_LG	11		/* Floating Exception mode 0 */
#define MSR_SE_LG	10		/* Single Step */
#define MSR_BE_LG	9		/* Branch Trace */
#define MSR_DE_LG	9 		/* Debug Exception Enable */
#define MSR_FE1_LG	8		/* Floating Exception mode 1 */
#define MSR_IP_LG	6		/* Exception prefix 0x000/0xFFF */
#define MSR_IR_LG	5 		/* Instruction Relocate */
#define MSR_DR_LG	4 		/* Data Relocate */
#define MSR_PE_LG	3		/* Protection Enable */
#define MSR_PX_LG	2		/* Protection Exclusive Mode */
#define MSR_PMM_LG	2		/* Performance monitor */
#define MSR_RI_LG	1		/* Recoverable Exception */
#define MSR_LE_LG	0 		/* Little Endian */

#ifdef __ASSEMBLY__
#define __MASK(X)	(1<<(X))
#else
#define __MASK(X)	(1UL<<(X))
#endif

#ifdef CONFIG_PPC64
#define MSR_SF		__MASK(MSR_SF_LG)	/* Enable 64 bit mode */
#define MSR_ISF		__MASK(MSR_ISF_LG)	/* Interrupt 64b mode valid on 630 */
#define MSR_HV 		__MASK(MSR_HV_LG)	/* Hypervisor state */
#else
/* so tests for these bits fail on 32-bit */
#define MSR_SF		0
#define MSR_ISF		0
#define MSR_HV		0
#endif

/*
 * To be used in shared book E/book S, this avoids needing to worry about
 * book S/book E in shared code
 */
#ifndef MSR_SPE
#define MSR_SPE 	0
#endif

#define MSR_VEC		__MASK(MSR_VEC_LG)	/* Enable AltiVec */
#define MSR_VSX		__MASK(MSR_VSX_LG)	/* Enable VSX */
#define MSR_POW		__MASK(MSR_POW_LG)	/* Enable Power Management */
#define MSR_WE		__MASK(MSR_WE_LG)	/* Wait State Enable */
#define MSR_TGPR	__MASK(MSR_TGPR_LG)	/* TLB Update registers in use */
#define MSR_CE		__MASK(MSR_CE_LG)	/* Critical Interrupt Enable */
#define MSR_ILE		__MASK(MSR_ILE_LG)	/* Interrupt Little Endian */
#define MSR_EE		__MASK(MSR_EE_LG)	/* External Interrupt Enable */
#define MSR_PR		__MASK(MSR_PR_LG)	/* Problem State / Privilege Level */
#define MSR_FP		__MASK(MSR_FP_LG)	/* Floating Point enable */
#define MSR_ME		__MASK(MSR_ME_LG)	/* Machine Check Enable */
#define MSR_FE0		__MASK(MSR_FE0_LG)	/* Floating Exception mode 0 */
#define MSR_SE		__MASK(MSR_SE_LG)	/* Single Step */
#define MSR_BE		__MASK(MSR_BE_LG)	/* Branch Trace */
#define MSR_DE		__MASK(MSR_DE_LG)	/* Debug Exception Enable */
#define MSR_FE1		__MASK(MSR_FE1_LG)	/* Floating Exception mode 1 */
#define MSR_IP		__MASK(MSR_IP_LG)	/* Exception prefix 0x000/0xFFF */
#define MSR_IR		__MASK(MSR_IR_LG)	/* Instruction Relocate */
#define MSR_DR		__MASK(MSR_DR_LG)	/* Data Relocate */
#define MSR_PE		__MASK(MSR_PE_LG)	/* Protection Enable */
#define MSR_PX		__MASK(MSR_PX_LG)	/* Protection Exclusive Mode */
#ifndef MSR_PMM
#define MSR_PMM		__MASK(MSR_PMM_LG)	/* Performance monitor */
#endif
#define MSR_RI		__MASK(MSR_RI_LG)	/* Recoverable Exception */
#define MSR_LE		__MASK(MSR_LE_LG)	/* Little Endian */

#define MSR_TM		__MASK(MSR_TM_LG)	/* Transactional Mem Available */
#define MSR_TS_N	0			/*  Non-transactional */
#define MSR_TS_S	__MASK(MSR_TS_S_LG)	/*  Transaction Suspended */
#define MSR_TS_T	__MASK(MSR_TS_T_LG)	/*  Transaction Transactional */
#define MSR_TS_MASK	(MSR_TS_T | MSR_TS_S)   /* Transaction State bits */
#define MSR_TM_ACTIVE(x) (((x) & MSR_TS_MASK) != 0) /* Transaction active? */
#define MSR_TM_RESV(x) (((x) & MSR_TS_MASK) == MSR_TS_MASK) /* Reserved */
#define MSR_TM_TRANSACTIONAL(x)	(((x) & MSR_TS_MASK) == MSR_TS_T)
#define MSR_TM_SUSPENDED(x)	(((x) & MSR_TS_MASK) == MSR_TS_S)

#if defined(CONFIG_PPC_BOOK3S_64)
#define MSR_64BIT	MSR_SF

/* Server variant */
#define __MSR		(MSR_ME | MSR_RI | MSR_IR | MSR_DR | MSR_ISF |MSR_HV)
#ifdef __BIG_ENDIAN__
#define MSR_		__MSR
#define MSR_IDLE	(MSR_ME | MSR_SF | MSR_HV)
#else
#define MSR_		(__MSR | MSR_LE)
#define MSR_IDLE	(MSR_ME | MSR_SF | MSR_HV | MSR_LE)
#endif
#define MSR_KERNEL	(MSR_ | MSR_64BIT)
#define MSR_USER32	(MSR_ | MSR_PR | MSR_EE)
#define MSR_USER64	(MSR_USER32 | MSR_64BIT)
#elif defined(CONFIG_PPC_BOOK3S_32) || defined(CONFIG_8xx)
/* Default MSR for kernel mode. */
#define MSR_KERNEL	(MSR_ME|MSR_RI|MSR_IR|MSR_DR)
#define MSR_USER	(MSR_KERNEL|MSR_PR|MSR_EE)
#endif

#ifndef MSR_64BIT
#define MSR_64BIT	0
#endif

/* Power Management - Processor Stop Status and Control Register Fields */
#define PSSCR_RL_MASK		0x0000000F /* Requested Level */
#define PSSCR_MTL_MASK		0x000000F0 /* Maximum Transition Level */
#define PSSCR_TR_MASK		0x00000300 /* Transition State */
#define PSSCR_PSLL_MASK		0x000F0000 /* Power-Saving Level Limit */
#define PSSCR_EC		0x00100000 /* Exit Criterion */
#define PSSCR_ESL		0x00200000 /* Enable State Loss */
#define PSSCR_SD		0x00400000 /* Status Disable */

/* Floating Point Status and Control Register (FPSCR) Fields */
#define FPSCR_FX	0x80000000	/* FPU exception summary */
#define FPSCR_FEX	0x40000000	/* FPU enabled exception summary */
#define FPSCR_VX	0x20000000	/* Invalid operation summary */
#define FPSCR_OX	0x10000000	/* Overflow exception summary */
#define FPSCR_UX	0x08000000	/* Underflow exception summary */
#define FPSCR_ZX	0x04000000	/* Zero-divide exception summary */
#define FPSCR_XX	0x02000000	/* Inexact exception summary */
#define FPSCR_VXSNAN	0x01000000	/* Invalid op for SNaN */
#define FPSCR_VXISI	0x00800000	/* Invalid op for Inv - Inv */
#define FPSCR_VXIDI	0x00400000	/* Invalid op for Inv / Inv */
#define FPSCR_VXZDZ	0x00200000	/* Invalid op for Zero / Zero */
#define FPSCR_VXIMZ	0x00100000	/* Invalid op for Inv * Zero */
#define FPSCR_VXVC	0x00080000	/* Invalid op for Compare */
#define FPSCR_FR	0x00040000	/* Fraction rounded */
#define FPSCR_FI	0x00020000	/* Fraction inexact */
#define FPSCR_FPRF	0x0001f000	/* FPU Result Flags */
#define FPSCR_FPCC	0x0000f000	/* FPU Condition Codes */
#define FPSCR_VXSOFT	0x00000400	/* Invalid op for software request */
#define FPSCR_VXSQRT	0x00000200	/* Invalid op for square root */
#define FPSCR_VXCVI	0x00000100	/* Invalid op for integer convert */
#define FPSCR_VE	0x00000080	/* Invalid op exception enable */
#define FPSCR_OE	0x00000040	/* IEEE overflow exception enable */
#define FPSCR_UE	0x00000020	/* IEEE underflow exception enable */
#define FPSCR_ZE	0x00000010	/* IEEE zero divide exception enable */
#define FPSCR_XE	0x00000008	/* FP inexact exception enable */
#define FPSCR_NI	0x00000004	/* FPU non IEEE-Mode */
#define FPSCR_RN	0x00000003	/* FPU rounding control */

/* Bit definitions for SPEFSCR. */
#define SPEFSCR_SOVH	0x80000000	/* Summary integer overflow high */
#define SPEFSCR_OVH	0x40000000	/* Integer overflow high */
#define SPEFSCR_FGH	0x20000000	/* Embedded FP guard bit high */
#define SPEFSCR_FXH	0x10000000	/* Embedded FP sticky bit high */
#define SPEFSCR_FINVH	0x08000000	/* Embedded FP invalid operation high */
#define SPEFSCR_FDBZH	0x04000000	/* Embedded FP div by zero high */
#define SPEFSCR_FUNFH	0x02000000	/* Embedded FP underflow high */
#define SPEFSCR_FOVFH	0x01000000	/* Embedded FP overflow high */
#define SPEFSCR_FINXS	0x00200000	/* Embedded FP inexact sticky */
#define SPEFSCR_FINVS	0x00100000	/* Embedded FP invalid op. sticky */
#define SPEFSCR_FDBZS	0x00080000	/* Embedded FP div by zero sticky */
#define SPEFSCR_FUNFS	0x00040000	/* Embedded FP underflow sticky */
#define SPEFSCR_FOVFS	0x00020000	/* Embedded FP overflow sticky */
#define SPEFSCR_MODE	0x00010000	/* Embedded FP mode */
#define SPEFSCR_SOV	0x00008000	/* Integer summary overflow */
#define SPEFSCR_OV	0x00004000	/* Integer overflow */
#define SPEFSCR_FG	0x00002000	/* Embedded FP guard bit */
#define SPEFSCR_FX	0x00001000	/* Embedded FP sticky bit */
#define SPEFSCR_FINV	0x00000800	/* Embedded FP invalid operation */
#define SPEFSCR_FDBZ	0x00000400	/* Embedded FP div by zero */
#define SPEFSCR_FUNF	0x00000200	/* Embedded FP underflow */
#define SPEFSCR_FOVF	0x00000100	/* Embedded FP overflow */
#define SPEFSCR_FINXE	0x00000040	/* Embedded FP inexact enable */
#define SPEFSCR_FINVE	0x00000020	/* Embedded FP invalid op. enable */
#define SPEFSCR_FDBZE	0x00000010	/* Embedded FP div by zero enable */
#define SPEFSCR_FUNFE	0x00000008	/* Embedded FP underflow enable */
#define SPEFSCR_FOVFE	0x00000004	/* Embedded FP overflow enable */
#define SPEFSCR_FRMC 	0x00000003	/* Embedded FP rounding mode control */

/* Special Purpose Registers (SPRNs)*/

#ifdef CONFIG_40x
#define SPRN_PID	0x3B1	/* Process ID */
#else
#define SPRN_PID	0x030	/* Process ID */
#ifdef CONFIG_BOOKE
#define SPRN_PID0	SPRN_PID/* Process ID Register 0 */
#endif
#endif

#define SPRN_CTR	0x009	/* Count Register */
#define SPRN_DSCR	0x11
#define SPRN_CFAR	0x1c	/* Come From Address Register */
#define SPRN_AMR	0x1d	/* Authority Mask Register */
#define SPRN_UAMOR	0x9d	/* User Authority Mask Override Register */
#define SPRN_AMOR	0x15d	/* Authority Mask Override Register */
#define SPRN_ACOP	0x1F	/* Available Coprocessor Register */
#define SPRN_TFIAR	0x81	/* Transaction Failure Inst Addr   */
#define SPRN_TEXASR	0x82	/* Transaction EXception & Summary */
#define SPRN_TEXASRU	0x83	/* ''	   ''	   ''	 Upper 32  */
#define   TEXASR_FS	__MASK(63-36) /* TEXASR Failure Summary */
#define SPRN_TFHAR	0x80	/* Transaction Failure Handler Addr */
#define SPRN_CTRLF	0x088
#define SPRN_CTRLT	0x098
#define   CTRL_CT	0xc0000000	/* current thread */
#define   CTRL_CT0	0x80000000	/* thread 0 */
#define   CTRL_CT1	0x40000000	/* thread 1 */
#define   CTRL_TE	0x00c00000	/* thread enable */
#define   CTRL_RUNLATCH	0x1
#define SPRN_DAWR	0xB4
#define SPRN_RPR	0xBA	/* Relative Priority Register */
#define SPRN_CIABR	0xBB
#define   CIABR_PRIV		0x3
#define   CIABR_PRIV_USER	1
#define   CIABR_PRIV_SUPER	2
#define   CIABR_PRIV_HYPER	3
#define SPRN_DAWRX	0xBC
#define   DAWRX_USER	__MASK(0)
#define   DAWRX_KERNEL	__MASK(1)
#define   DAWRX_HYP	__MASK(2)
#define   DAWRX_WTI	__MASK(3)
#define   DAWRX_WT	__MASK(4)
#define   DAWRX_DR	__MASK(5)
#define   DAWRX_DW	__MASK(6)
#define SPRN_DABR	0x3F5	/* Data Address Breakpoint Register */
#define SPRN_DABR2	0x13D	/* e300 */
#define SPRN_DABRX	0x3F7	/* Data Address Breakpoint Register Extension */
#define   DABRX_USER	__MASK(0)
#define   DABRX_KERNEL	__MASK(1)
#define   DABRX_HYP	__MASK(2)
#define   DABRX_BTI	__MASK(3)
#define   DABRX_ALL     (DABRX_BTI | DABRX_HYP | DABRX_KERNEL | DABRX_USER)
#define SPRN_DAR	0x013	/* Data Address Register */
#define SPRN_DBCR	0x136	/* e300 Data Breakpoint Control Reg */
#define SPRN_DSISR	0x012	/* Data Storage Interrupt Status Register */
#define   DSISR_NOHPTE		0x40000000	/* no translation found */
#define   DSISR_PROTFAULT	0x08000000	/* protection fault */
#define   DSISR_ISSTORE		0x02000000	/* access was a store */
#define   DSISR_DABRMATCH	0x00400000	/* hit data breakpoint */
#define   DSISR_NOSEGMENT	0x00200000	/* SLB miss */
#define   DSISR_KEYFAULT	0x00200000	/* Key fault */
#define SPRN_TBRL	0x10C	/* Time Base Read Lower Register (user, R/O) */
#define SPRN_TBRU	0x10D	/* Time Base Read Upper Register (user, R/O) */
#define SPRN_CIR	0x11B	/* Chip Information Register (hyper, R/0) */
#define SPRN_TBWL	0x11C	/* Time Base Lower Register (super, R/W) */
#define SPRN_TBWU	0x11D	/* Time Base Upper Register (super, R/W) */
#define SPRN_TBU40	0x11E	/* Timebase upper 40 bits (hyper, R/W) */
#define SPRN_SPURR	0x134	/* Scaled PURR */
#define SPRN_HSPRG0	0x130	/* Hypervisor Scratch 0 */
#define SPRN_HSPRG1	0x131	/* Hypervisor Scratch 1 */
#define SPRN_HDSISR     0x132
#define SPRN_HDAR       0x133
#define SPRN_HDEC	0x136	/* Hypervisor Decrementer */
#define SPRN_HIOR	0x137	/* 970 Hypervisor interrupt offset */
#define SPRN_RMOR	0x138	/* Real mode offset register */
#define SPRN_HRMOR	0x139	/* Real mode offset register */
#define SPRN_HSRR0	0x13A	/* Hypervisor Save/Restore 0 */
#define SPRN_HSRR1	0x13B	/* Hypervisor Save/Restore 1 */
#define SPRN_LMRR	0x32D	/* Load Monitor Region Register */
#define SPRN_LMSER	0x32E	/* Load Monitor Section Enable Register */
#define SPRN_IC		0x350	/* Virtual Instruction Count */
#define SPRN_VTB	0x351	/* Virtual Time Base */
#define SPRN_LDBAR	0x352	/* LD Base Address Register */
#define SPRN_PMICR	0x354   /* Power Management Idle Control Reg */
#define SPRN_PMSR	0x355   /* Power Management Status Reg */
#define SPRN_PMMAR	0x356	/* Power Management Memory Activity Register */
#define SPRN_PSSCR	0x357	/* Processor Stop Status and Control Register (ISA 3.0) */
#define SPRN_PMCR	0x374	/* Power Management Control Register */

/* HFSCR and FSCR bit numbers are the same */
#define FSCR_LM_LG	11	/* Enable Load Monitor Registers */
#define FSCR_TAR_LG	8	/* Enable Target Address Register */
#define FSCR_EBB_LG	7	/* Enable Event Based Branching */
#define FSCR_TM_LG	5	/* Enable Transactional Memory */
#define FSCR_BHRB_LG	4	/* Enable Branch History Rolling Buffer*/
#define FSCR_PM_LG	3	/* Enable prob/priv access to PMU SPRs */
#define FSCR_DSCR_LG	2	/* Enable Data Stream Control Register */
#define FSCR_VECVSX_LG	1	/* Enable VMX/VSX  */
#define FSCR_FP_LG	0	/* Enable Floating Point */
#define SPRN_FSCR	0x099	/* Facility Status & Control Register */
#define   FSCR_LM	__MASK(FSCR_LM_LG)
#define   FSCR_TAR	__MASK(FSCR_TAR_LG)
#define   FSCR_EBB	__MASK(FSCR_EBB_LG)
#define   FSCR_DSCR	__MASK(FSCR_DSCR_LG)
#define SPRN_HFSCR	0xbe	/* HV=1 Facility Status & Control Register */
#define   HFSCR_LM	__MASK(FSCR_LM_LG)
#define   HFSCR_TAR	__MASK(FSCR_TAR_LG)
#define   HFSCR_EBB	__MASK(FSCR_EBB_LG)
#define   HFSCR_TM	__MASK(FSCR_TM_LG)
#define   HFSCR_PM	__MASK(FSCR_PM_LG)
#define   HFSCR_BHRB	__MASK(FSCR_BHRB_LG)
#define   HFSCR_DSCR	__MASK(FSCR_DSCR_LG)
#define   HFSCR_VECVSX	__MASK(FSCR_VECVSX_LG)
#define   HFSCR_FP	__MASK(FSCR_FP_LG)
#define SPRN_TAR	0x32f	/* Target Address Register */
#define SPRN_LPCR	0x13E	/* LPAR Control Register */
#define   LPCR_VPM0		ASM_CONST(0x8000000000000000)
#define   LPCR_VPM1		ASM_CONST(0x4000000000000000)
#define   LPCR_ISL		ASM_CONST(0x2000000000000000)
#define   LPCR_VC_SH		61
#define   LPCR_DPFD_SH		52
#define   LPCR_DPFD		(ASM_CONST(7) << LPCR_DPFD_SH)
#define   LPCR_VRMASD_SH	47
#define   LPCR_VRMASD		(ASM_CONST(1) << LPCR_VRMASD_SH)
#define   LPCR_VRMA_L		ASM_CONST(0x0008000000000000)
#define   LPCR_VRMA_LP0		ASM_CONST(0x0001000000000000)
#define   LPCR_VRMA_LP1		ASM_CONST(0x0000800000000000)
#define   LPCR_RMLS		0x1C000000	/* Implementation dependent RMO limit sel */
#define   LPCR_RMLS_SH		26
#define   LPCR_ILE		ASM_CONST(0x0000000002000000)   /* !HV irqs set MSR:LE */
#define   LPCR_AIL		ASM_CONST(0x0000000001800000)	/* Alternate interrupt location */
#define   LPCR_AIL_0		ASM_CONST(0x0000000000000000)	/* MMU off exception offset 0x0 */
#define   LPCR_AIL_3		ASM_CONST(0x0000000001800000)   /* MMU on exception offset 0xc00...4xxx */
#define   LPCR_ONL		ASM_CONST(0x0000000000040000)	/* online - PURR/SPURR count */
#define   LPCR_LD		ASM_CONST(0x0000000000020000)	/* large decremeter */
#define   LPCR_PECE		ASM_CONST(0x000000000001f000)	/* powersave exit cause enable */
#define     LPCR_PECEDP	ASM_CONST(0x0000000000010000)	/* directed priv dbells cause exit */
#define     LPCR_PECEDH	ASM_CONST(0x0000000000008000)	/* directed hyp dbells cause exit */
#define     LPCR_PECE0		ASM_CONST(0x0000000000004000)	/* ext. exceptions can cause exit */
#define     LPCR_PECE1		ASM_CONST(0x0000000000002000)	/* decrementer can cause exit */
#define     LPCR_PECE2		ASM_CONST(0x0000000000001000)	/* machine check etc can cause exit */
#define   LPCR_MER		ASM_CONST(0x0000000000000800)	/* Mediated External Exception */
#define   LPCR_MER_SH		11
#define   LPCR_TC		ASM_CONST(0x0000000000000200)	/* Translation control */
#define   LPCR_LPES		0x0000000c
#define   LPCR_LPES0		ASM_CONST(0x0000000000000008)      /* LPAR Env selector 0 */
#define   LPCR_LPES1		ASM_CONST(0x0000000000000004)      /* LPAR Env selector 1 */
#define   LPCR_LPES_SH		2
#define   LPCR_RMI		ASM_CONST(0x0000000000000002)      /* real mode is cache inhibit */
#define   LPCR_HVICE		ASM_CONST(0x0000000000000002)      /* P9: HV interrupt enable */
#define   LPCR_HDICE		ASM_CONST(0x0000000000000001)      /* Hyp Decr enable (HV,PR,EE) */
#define   LPCR_UPRT		ASM_CONST(0x0000000000400000)      /* Use Process Table (ISA 3) */
#define   LPCR_HR		ASM_CONST(0x0000000000100000)
#ifndef SPRN_LPID
#define SPRN_LPID	0x13F	/* Logical Partition Identifier */
#endif
#define   LPID_RSVD	0x3ff		/* Reserved LPID for partn switching */
#define	SPRN_HMER	0x150	/* Hardware m? error recovery */
#define	SPRN_HMEER	0x151	/* Hardware m? enable error recovery */
#define SPRN_PCR	0x152	/* Processor compatibility register */
#define   PCR_VEC_DIS	(1ul << (63-0))	/* Vec. disable (bit NA since POWER8) */
#define   PCR_VSX_DIS	(1ul << (63-1))	/* VSX disable (bit NA since POWER8) */
#define   PCR_TM_DIS	(1ul << (63-2))	/* Trans. memory disable (POWER8) */
#define   PCR_ARCH_206	0x4		/* Architecture 2.06 */
#define   PCR_ARCH_205	0x2		/* Architecture 2.05 */
#define	SPRN_HEIR	0x153	/* Hypervisor Emulated Instruction Register */
#define SPRN_TLBINDEXR	0x154	/* P7 TLB control register */
#define SPRN_TLBVPNR	0x155	/* P7 TLB control register */
#define SPRN_TLBRPNR	0x156	/* P7 TLB control register */
#define SPRN_TLBLPIDR	0x157	/* P7 TLB control register */
#define SPRN_DBAT0L	0x219	/* Data BAT 0 Lower Register */
#define SPRN_DBAT0U	0x218	/* Data BAT 0 Upper Register */
#define SPRN_DBAT1L	0x21B	/* Data BAT 1 Lower Register */
#define SPRN_DBAT1U	0x21A	/* Data BAT 1 Upper Register */
#define SPRN_DBAT2L	0x21D	/* Data BAT 2 Lower Register */
#define SPRN_DBAT2U	0x21C	/* Data BAT 2 Upper Register */
#define SPRN_DBAT3L	0x21F	/* Data BAT 3 Lower Register */
#define SPRN_DBAT3U	0x21E	/* Data BAT 3 Upper Register */
#define SPRN_DBAT4L	0x239	/* Data BAT 4 Lower Register */
#define SPRN_DBAT4U	0x238	/* Data BAT 4 Upper Register */
#define SPRN_DBAT5L	0x23B	/* Data BAT 5 Lower Register */
#define SPRN_DBAT5U	0x23A	/* Data BAT 5 Upper Register */
#define SPRN_DBAT6L	0x23D	/* Data BAT 6 Lower Register */
#define SPRN_DBAT6U	0x23C	/* Data BAT 6 Upper Register */
#define SPRN_DBAT7L	0x23F	/* Data BAT 7 Lower Register */
#define SPRN_DBAT7U	0x23E	/* Data BAT 7 Upper Register */
#define SPRN_PPR	0x380	/* SMT Thread status Register */
#define SPRN_TSCR	0x399	/* Thread Switch Control Register */

#define SPRN_DEC	0x016		/* Decrement Register */
#define SPRN_DER	0x095		/* Debug Enable Register */
#define DER_RSTE	0x40000000	/* Reset Interrupt */
#define DER_CHSTPE	0x20000000	/* Check Stop */
#define DER_MCIE	0x10000000	/* Machine Check Interrupt */
#define DER_EXTIE	0x02000000	/* External Interrupt */
#define DER_ALIE	0x01000000	/* Alignment Interrupt */
#define DER_PRIE	0x00800000	/* Program Interrupt */
#define DER_FPUVIE	0x00400000	/* FP Unavailable Interrupt */
#define DER_DECIE	0x00200000	/* Decrementer Interrupt */
#define DER_SYSIE	0x00040000	/* System Call Interrupt */
#define DER_TRE		0x00020000	/* Trace Interrupt */
#define DER_SEIE	0x00004000	/* FP SW Emulation Interrupt */
#define DER_ITLBMSE	0x00002000	/* Imp. Spec. Instruction TLB Miss */
#define DER_ITLBERE	0x00001000	/* Imp. Spec. Instruction TLB Error */
#define DER_DTLBMSE	0x00000800	/* Imp. Spec. Data TLB Miss */
#define DER_DTLBERE	0x00000400	/* Imp. Spec. Data TLB Error */
#define DER_LBRKE	0x00000008	/* Load/Store Breakpoint Interrupt */
#define DER_IBRKE	0x00000004	/* Instruction Breakpoint Interrupt */
#define DER_EBRKE	0x00000002	/* External Breakpoint Interrupt */
#define DER_DPIE	0x00000001	/* Dev. Port Nonmaskable Request */
#define SPRN_DMISS	0x3D0		/* Data TLB Miss Register */
#define SPRN_DHDES	0x0B1		/* Directed Hyp. Doorbell Exc. State */
#define SPRN_DPDES	0x0B0		/* Directed Priv. Doorbell Exc. State */
#define SPRN_EAR	0x11A		/* External Address Register */
#define SPRN_HASH1	0x3D2		/* Primary Hash Address Register */
#define SPRN_HASH2	0x3D3		/* Secondary Hash Address Register */
#define SPRN_HID0	0x3F0		/* Hardware Implementation Register 0 */
#define HID0_HDICE_SH	(63 - 23)	/* 970 HDEC interrupt enable */
#define HID0_EMCP	(1<<31)		/* Enable Machine Check pin */
#define HID0_EBA	(1<<29)		/* Enable Bus Address Parity */
#define HID0_EBD	(1<<28)		/* Enable Bus Data Parity */
#define HID0_SBCLK	(1<<27)
#define HID0_EICE	(1<<26)
#define HID0_TBEN	(1<<26)		/* Timebase enable - 745x */
#define HID0_ECLK	(1<<25)
#define HID0_PAR	(1<<24)
#define HID0_STEN	(1<<24)		/* Software table search enable - 745x */
#define HID0_HIGH_BAT	(1<<23)		/* Enable high BATs - 7455 */
#define HID0_DOZE	(1<<23)
#define HID0_NAP	(1<<22)
#define HID0_SLEEP	(1<<21)
#define HID0_DPM	(1<<20)
#define HID0_BHTCLR	(1<<18)		/* Clear branch history table - 7450 */
#define HID0_XAEN	(1<<17)		/* Extended addressing enable - 7450 */
#define HID0_NHR	(1<<16)		/* Not hard reset (software bit-7450)*/
#define HID0_ICE	(1<<15)		/* Instruction Cache Enable */
#define HID0_DCE	(1<<14)		/* Data Cache Enable */
#define HID0_ILOCK	(1<<13)		/* Instruction Cache Lock */
#define HID0_DLOCK	(1<<12)		/* Data Cache Lock */
#define HID0_ICFI	(1<<11)		/* Instr. Cache Flash Invalidate */
#define HID0_DCI	(1<<10)		/* Data Cache Invalidate */
#define HID0_SPD	(1<<9)		/* Speculative disable */
#define HID0_DAPUEN	(1<<8)		/* Debug APU enable */
#define HID0_SGE	(1<<7)		/* Store Gathering Enable */
#define HID0_SIED	(1<<7)		/* Serial Instr. Execution [Disable] */
#define HID0_DCFA	(1<<6)		/* Data Cache Flush Assist */
#define HID0_LRSTK	(1<<4)		/* Link register stack - 745x */
#define HID0_BTIC	(1<<5)		/* Branch Target Instr Cache Enable */
#define HID0_ABE	(1<<3)		/* Address Broadcast Enable */
#define HID0_FOLD	(1<<3)		/* Branch Folding enable - 745x */
#define HID0_BHTE	(1<<2)		/* Branch History Table Enable */
#define HID0_BTCD	(1<<1)		/* Branch target cache disable */
#define HID0_NOPDST	(1<<1)		/* No-op dst, dstt, etc. instr. */
#define HID0_NOPTI	(1<<0)		/* No-op dcbt and dcbst instr. */
/* POWER8 HID0 bits */
#define HID0_POWER8_4LPARMODE	__MASK(61)
#define HID0_POWER8_2LPARMODE	__MASK(57)
#define HID0_POWER8_1TO2LPAR	__MASK(52)
#define HID0_POWER8_1TO4LPAR	__MASK(51)
#define HID0_POWER8_DYNLPARDIS	__MASK(48)

#define SPRN_HID1	0x3F1		/* Hardware Implementation Register 1 */
#ifdef CONFIG_6xx
#define HID1_EMCP	(1<<31)		/* 7450 Machine Check Pin Enable */
#define HID1_DFS	(1<<22)		/* 7447A Dynamic Frequency Scaling */
#define HID1_PC0	(1<<16)		/* 7450 PLL_CFG[0] */
#define HID1_PC1	(1<<15)		/* 7450 PLL_CFG[1] */
#define HID1_PC2	(1<<14)		/* 7450 PLL_CFG[2] */
#define HID1_PC3	(1<<13)		/* 7450 PLL_CFG[3] */
#define HID1_SYNCBE	(1<<11)		/* 7450 ABE for sync, eieio */
#define HID1_ABE	(1<<10)		/* 7450 Address Broadcast Enable */
#define HID1_PS		(1<<16)		/* 750FX PLL selection */
#endif
#define SPRN_HID2	0x3F8		/* Hardware Implementation Register 2 */
#define SPRN_HID2_GEKKO	0x398		/* Gekko HID2 Register */
#define SPRN_IABR	0x3F2	/* Instruction Address Breakpoint Register */
#define SPRN_IABR2	0x3FA		/* 83xx */
#define SPRN_IBCR	0x135		/* 83xx Insn Breakpoint Control Reg */
#define SPRN_IAMR	0x03D		/* Instr. Authority Mask Reg */
#define SPRN_HID4	0x3F4		/* 970 HID4 */
#define  HID4_LPES0	 (1ul << (63-0)) /* LPAR env. sel. bit 0 */
#define	 HID4_RMLS2_SH	 (63 - 2)	/* Real mode limit bottom 2 bits */
#define	 HID4_LPID5_SH	 (63 - 6)	/* partition ID bottom 4 bits */
#define	 HID4_RMOR_SH	 (63 - 22)	/* real mode offset (16 bits) */
#define  HID4_RMOR	 (0xFFFFul << HID4_RMOR_SH)
#define  HID4_LPES1	 (1 << (63-57))	/* LPAR env. sel. bit 1 */
#define  HID4_RMLS0_SH	 (63 - 58)	/* Real mode limit top bit */
#define	 HID4_LPID1_SH	 0		/* partition ID top 2 bits */
#define SPRN_HID4_GEKKO	0x3F3		/* Gekko HID4 */
#define SPRN_HID5	0x3F6		/* 970 HID5 */
#define SPRN_HID6	0x3F9	/* BE HID 6 */
#define   HID6_LB	(0x0F<<12) /* Concurrent Large Page Modes */
#define   HID6_DLP	(1<<20)	/* Disable all large page modes (4K only) */
#define SPRN_TSC_CELL	0x399	/* Thread switch control on Cell */
#define   TSC_CELL_DEC_ENABLE_0	0x400000 /* Decrementer Interrupt */
#define   TSC_CELL_DEC_ENABLE_1	0x200000 /* Decrementer Interrupt */
#define   TSC_CELL_EE_ENABLE	0x100000 /* External Interrupt */
#define   TSC_CELL_EE_BOOST	0x080000 /* External Interrupt Boost */
#define SPRN_TSC 	0x3FD	/* Thread switch control on others */
#define SPRN_TST 	0x3FC	/* Thread switch timeout on others */
#if !defined(SPRN_IAC1) && !defined(SPRN_IAC2)
#define SPRN_IAC1	0x3F4		/* Instruction Address Compare 1 */
#define SPRN_IAC2	0x3F5		/* Instruction Address Compare 2 */
#endif
#define SPRN_IBAT0L	0x211		/* Instruction BAT 0 Lower Register */
#define SPRN_IBAT0U	0x210		/* Instruction BAT 0 Upper Register */
#define SPRN_IBAT1L	0x213		/* Instruction BAT 1 Lower Register */
#define SPRN_IBAT1U	0x212		/* Instruction BAT 1 Upper Register */
#define SPRN_IBAT2L	0x215		/* Instruction BAT 2 Lower Register */
#define SPRN_IBAT2U	0x214		/* Instruction BAT 2 Upper Register */
#define SPRN_IBAT3L	0x217		/* Instruction BAT 3 Lower Register */
#define SPRN_IBAT3U	0x216		/* Instruction BAT 3 Upper Register */
#define SPRN_IBAT4L	0x231		/* Instruction BAT 4 Lower Register */
#define SPRN_IBAT4U	0x230		/* Instruction BAT 4 Upper Register */
#define SPRN_IBAT5L	0x233		/* Instruction BAT 5 Lower Register */
#define SPRN_IBAT5U	0x232		/* Instruction BAT 5 Upper Register */
#define SPRN_IBAT6L	0x235		/* Instruction BAT 6 Lower Register */
#define SPRN_IBAT6U	0x234		/* Instruction BAT 6 Upper Register */
#define SPRN_IBAT7L	0x237		/* Instruction BAT 7 Lower Register */
#define SPRN_IBAT7U	0x236		/* Instruction BAT 7 Upper Register */
#define SPRN_ICMP	0x3D5		/* Instruction TLB Compare Register */
#define SPRN_ICTC	0x3FB	/* Instruction Cache Throttling Control Reg */
#define SPRN_ICTRL	0x3F3	/* 1011 7450 icache and interrupt ctrl */
#define ICTRL_EICE	0x08000000	/* enable icache parity errs */
#define ICTRL_EDC	0x04000000	/* enable dcache parity errs */
#define ICTRL_EICP	0x00000100	/* enable icache par. check */
#define SPRN_IMISS	0x3D4		/* Instruction TLB Miss Register */
#define SPRN_IMMR	0x27E		/* Internal Memory Map Register */
#define SPRN_L2CR	0x3F9		/* Level 2 Cache Control Register */
#define SPRN_L2CR2	0x3f8
#define L2CR_L2E		0x80000000	/* L2 enable */
#define L2CR_L2PE		0x40000000	/* L2 parity enable */
#define L2CR_L2SIZ_MASK		0x30000000	/* L2 size mask */
#define L2CR_L2SIZ_256KB	0x10000000	/* L2 size 256KB */
#define L2CR_L2SIZ_512KB	0x20000000	/* L2 size 512KB */
#define L2CR_L2SIZ_1MB		0x30000000	/* L2 size 1MB */
#define L2CR_L2CLK_MASK		0x0e000000	/* L2 clock mask */
#define L2CR_L2CLK_DISABLED	0x00000000	/* L2 clock disabled */
#define L2CR_L2CLK_DIV1		0x02000000	/* L2 clock / 1 */
#define L2CR_L2CLK_DIV1_5	0x04000000	/* L2 clock / 1.5 */
#define L2CR_L2CLK_DIV2		0x08000000	/* L2 clock / 2 */
#define L2CR_L2CLK_DIV2_5	0x0a000000	/* L2 clock / 2.5 */
#define L2CR_L2CLK_DIV3		0x0c000000	/* L2 clock / 3 */
#define L2CR_L2RAM_MASK		0x01800000	/* L2 RAM type mask */
#define L2CR_L2RAM_FLOW		0x00000000	/* L2 RAM flow through */
#define L2CR_L2RAM_PIPE		0x01000000	/* L2 RAM pipelined */
#define L2CR_L2RAM_PIPE_LW	0x01800000	/* L2 RAM pipelined latewr */
#define L2CR_L2DO		0x00400000	/* L2 data only */
#define L2CR_L2I		0x00200000	/* L2 global invalidate */
#define L2CR_L2CTL		0x00100000	/* L2 RAM control */
#define L2CR_L2WT		0x00080000	/* L2 write-through */
#define L2CR_L2TS		0x00040000	/* L2 test support */
#define L2CR_L2OH_MASK		0x00030000	/* L2 output hold mask */
#define L2CR_L2OH_0_5		0x00000000	/* L2 output hold 0.5 ns */
#define L2CR_L2OH_1_0		0x00010000	/* L2 output hold 1.0 ns */
#define L2CR_L2SL		0x00008000	/* L2 DLL slow */
#define L2CR_L2DF		0x00004000	/* L2 differential clock */
#define L2CR_L2BYP		0x00002000	/* L2 DLL bypass */
#define L2CR_L2IP		0x00000001	/* L2 GI in progress */
#define L2CR_L2IO_745x		0x00100000	/* L2 instr. only (745x) */
#define L2CR_L2DO_745x		0x00010000	/* L2 data only (745x) */
#define L2CR_L2REP_745x		0x00001000	/* L2 repl. algorithm (745x) */
#define L2CR_L2HWF_745x		0x00000800	/* L2 hardware flush (745x) */
#define SPRN_L3CR		0x3FA	/* Level 3 Cache Control Register */
#define L3CR_L3E		0x80000000	/* L3 enable */
#define L3CR_L3PE		0x40000000	/* L3 data parity enable */
#define L3CR_L3APE		0x20000000	/* L3 addr parity enable */
#define L3CR_L3SIZ		0x10000000	/* L3 size */
#define L3CR_L3CLKEN		0x08000000	/* L3 clock enable */
#define L3CR_L3RES		0x04000000	/* L3 special reserved bit */
#define L3CR_L3CLKDIV		0x03800000	/* L3 clock divisor */
#define L3CR_L3IO		0x00400000	/* L3 instruction only */
#define L3CR_L3SPO		0x00040000	/* L3 sample point override */
#define L3CR_L3CKSP		0x00030000	/* L3 clock sample point */
#define L3CR_L3PSP		0x0000e000	/* L3 P-clock sample point */
#define L3CR_L3REP		0x00001000	/* L3 replacement algorithm */
#define L3CR_L3HWF		0x00000800	/* L3 hardware flush */
#define L3CR_L3I		0x00000400	/* L3 global invalidate */
#define L3CR_L3RT		0x00000300	/* L3 SRAM type */
#define L3CR_L3NIRCA		0x00000080	/* L3 non-integer ratio clock adj. */
#define L3CR_L3DO		0x00000040	/* L3 data only mode */
#define L3CR_PMEN		0x00000004	/* L3 private memory enable */
#define L3CR_PMSIZ		0x00000001	/* L3 private memory size */

#define SPRN_MSSCR0	0x3f6	/* Memory Subsystem Control Register 0 */
#define SPRN_MSSSR0	0x3f7	/* Memory Subsystem Status Register 1 */
#define SPRN_LDSTCR	0x3f8	/* Load/Store control register */
#define SPRN_LDSTDB	0x3f4	/* */
#define SPRN_LR		0x008	/* Link Register */
#ifndef SPRN_PIR
#define SPRN_PIR	0x3FF	/* Processor Identification Register */
#endif
#define SPRN_TIR	0x1BE	/* Thread Identification Register */
#define SPRN_PTCR	0x1D0	/* Partition table control Register */
#define SPRN_PSPB	0x09F	/* Problem State Priority Boost reg */
#define SPRN_PTEHI	0x3D5	/* 981 7450 PTE HI word (S/W TLB load) */
#define SPRN_PTELO	0x3D6	/* 982 7450 PTE LO word (S/W TLB load) */
#define SPRN_PURR	0x135	/* Processor Utilization of Resources Reg */
#define SPRN_PVR	0x11F	/* Processor Version Register */
#define SPRN_RPA	0x3D6	/* Required Physical Address Register */
#define SPRN_SDA	0x3BF	/* Sampled Data Address Register */
#define SPRN_SDR1	0x019	/* MMU Hash Base Register */
#define SPRN_ASR	0x118   /* Address Space Register */
#define SPRN_SIA	0x3BB	/* Sampled Instruction Address Register */
#define SPRN_SPRG0	0x110	/* Special Purpose Register General 0 */
#define SPRN_SPRG1	0x111	/* Special Purpose Register General 1 */
#define SPRN_SPRG2	0x112	/* Special Purpose Register General 2 */
#define SPRN_SPRG3	0x113	/* Special Purpose Register General 3 */
#define SPRN_USPRG3	0x103	/* SPRG3 userspace read */
#define SPRN_SPRG4	0x114	/* Special Purpose Register General 4 */
#define SPRN_USPRG4	0x104	/* SPRG4 userspace read */
#define SPRN_SPRG5	0x115	/* Special Purpose Register General 5 */
#define SPRN_USPRG5	0x105	/* SPRG5 userspace read */
#define SPRN_SPRG6	0x116	/* Special Purpose Register General 6 */
#define SPRN_USPRG6	0x106	/* SPRG6 userspace read */
#define SPRN_SPRG7	0x117	/* Special Purpose Register General 7 */
#define SPRN_USPRG7	0x107	/* SPRG7 userspace read */
#define SPRN_SRR0	0x01A	/* Save/Restore Register 0 */
#define SPRN_SRR1	0x01B	/* Save/Restore Register 1 */
#define   SRR1_ISI_NOPT		0x40000000 /* ISI: Not found in hash */
#define   SRR1_ISI_N_OR_G	0x10000000 /* ISI: Access is no-exec or G */
#define   SRR1_ISI_PROT		0x08000000 /* ISI: Other protection fault */
#define   SRR1_WAKEMASK		0x00380000 /* reason for wakeup */
#define   SRR1_WAKEMASK_P8	0x003c0000 /* reason for wakeup on POWER8 */
#define   SRR1_WAKESYSERR	0x00300000 /* System error */
#define   SRR1_WAKEEE		0x00200000 /* External interrupt */
#define   SRR1_WAKEMT		0x00280000 /* mtctrl */
#define	  SRR1_WAKEHMI		0x00280000 /* Hypervisor maintenance */
#define   SRR1_WAKEDEC		0x00180000 /* Decrementer interrupt */
#define   SRR1_WAKEDBELL	0x00140000 /* Privileged doorbell on P8 */
#define   SRR1_WAKETHERM	0x00100000 /* Thermal management interrupt */
#define	  SRR1_WAKERESET	0x00100000 /* System reset */
#define   SRR1_WAKEHDBELL	0x000c0000 /* Hypervisor doorbell on P8 */
#define	  SRR1_WAKESTATE	0x00030000 /* Powersave exit mask [46:47] */
#define	  SRR1_WS_DEEPEST	0x00030000 /* Some resources not maintained,
					  * may not be recoverable */
#define	  SRR1_WS_DEEPER	0x00020000 /* Some resources not maintained */
#define	  SRR1_WS_DEEP		0x00010000 /* All resources maintained */
#define   SRR1_PROGFPE		0x00100000 /* Floating Point Enabled */
#define   SRR1_PROGILL		0x00080000 /* Illegal instruction */
#define   SRR1_PROGPRIV		0x00040000 /* Privileged instruction */
#define   SRR1_PROGTRAP		0x00020000 /* Trap */
#define   SRR1_PROGADDR		0x00010000 /* SRR0 contains subsequent addr */

#define SPRN_HSRR0	0x13A	/* Save/Restore Register 0 */
#define SPRN_HSRR1	0x13B	/* Save/Restore Register 1 */
#define   HSRR1_DENORM		0x00100000 /* Denorm exception */

#define SPRN_TBCTL	0x35f	/* PA6T Timebase control register */
#define   TBCTL_FREEZE		0x0000000000000000ull /* Freeze all tbs */
#define   TBCTL_RESTART		0x0000000100000000ull /* Restart all tbs */
#define   TBCTL_UPDATE_UPPER	0x0000000200000000ull /* Set upper 32 bits */
#define   TBCTL_UPDATE_LOWER	0x0000000300000000ull /* Set lower 32 bits */

#ifndef SPRN_SVR
#define SPRN_SVR	0x11E	/* System Version Register */
#endif
#define SPRN_THRM1	0x3FC		/* Thermal Management Register 1 */
/* these bits were defined in inverted endian sense originally, ugh, confusing */
#define THRM1_TIN	(1 << 31)
#define THRM1_TIV	(1 << 30)
#define THRM1_THRES(x)	((x&0x7f)<<23)
#define THRM3_SITV(x)	((x&0x3fff)<<1)
#define THRM1_TID	(1<<2)
#define THRM1_TIE	(1<<1)
#define THRM1_V		(1<<0)
#define SPRN_THRM2	0x3FD		/* Thermal Management Register 2 */
#define SPRN_THRM3	0x3FE		/* Thermal Management Register 3 */
#define THRM3_E		(1<<0)
#define SPRN_TLBMISS	0x3D4		/* 980 7450 TLB Miss Register */
#define SPRN_UMMCR0	0x3A8	/* User Monitor Mode Control Register 0 */
#define SPRN_UMMCR1	0x3AC	/* User Monitor Mode Control Register 0 */
#define SPRN_UPMC1	0x3A9	/* User Performance Counter Register 1 */
#define SPRN_UPMC2	0x3AA	/* User Performance Counter Register 2 */
#define SPRN_UPMC3	0x3AD	/* User Performance Counter Register 3 */
#define SPRN_UPMC4	0x3AE	/* User Performance Counter Register 4 */
#define SPRN_USIA	0x3AB	/* User Sampled Instruction Address Register */
#define SPRN_VRSAVE	0x100	/* Vector Register Save Register */
#define SPRN_XER	0x001	/* Fixed Point Exception Register */

#define SPRN_MMCR0_GEKKO 0x3B8 /* Gekko Monitor Mode Control Register 0 */
#define SPRN_MMCR1_GEKKO 0x3BC /* Gekko Monitor Mode Control Register 1 */
#define SPRN_PMC1_GEKKO  0x3B9 /* Gekko Performance Monitor Control 1 */
#define SPRN_PMC2_GEKKO  0x3BA /* Gekko Performance Monitor Control 2 */
#define SPRN_PMC3_GEKKO  0x3BD /* Gekko Performance Monitor Control 3 */
#define SPRN_PMC4_GEKKO  0x3BE /* Gekko Performance Monitor Control 4 */
#define SPRN_WPAR_GEKKO  0x399 /* Gekko Write Pipe Address Register */

#define SPRN_SCOMC	0x114	/* SCOM Access Control */
#define SPRN_SCOMD	0x115	/* SCOM Access DATA */

/* Performance monitor SPRs */
#ifdef CONFIG_PPC64
#define SPRN_MMCR0	795
#define   MMCR0_FC	0x80000000UL /* freeze counters */
#define   MMCR0_FCS	0x40000000UL /* freeze in supervisor state */
#define   MMCR0_KERNEL_DISABLE MMCR0_FCS
#define   MMCR0_FCP	0x20000000UL /* freeze in problem state */
#define   MMCR0_PROBLEM_DISABLE MMCR0_FCP
#define   MMCR0_FCM1	0x10000000UL /* freeze counters while MSR mark = 1 */
#define   MMCR0_FCM0	0x08000000UL /* freeze counters while MSR mark = 0 */
#define   MMCR0_PMXE	ASM_CONST(0x04000000) /* perf mon exception enable */
#define   MMCR0_FCECE	ASM_CONST(0x02000000) /* freeze ctrs on enabled cond or event */
#define   MMCR0_TBEE	0x00400000UL /* time base exception enable */
#define   MMCR0_BHRBA	0x00200000UL /* BHRB Access allowed in userspace */
#define   MMCR0_EBE	0x00100000UL /* Event based branch enable */
#define   MMCR0_PMCC	0x000c0000UL /* PMC control */
#define   MMCR0_PMCC_U6	0x00080000UL /* PMC1-6 are R/W by user (PR) */
#define   MMCR0_PMC1CE	0x00008000UL /* PMC1 count enable*/
#define   MMCR0_PMCjCE	ASM_CONST(0x00004000) /* PMCj count enable*/
#define   MMCR0_TRIGGER	0x00002000UL /* TRIGGER enable */
#define   MMCR0_PMAO_SYNC ASM_CONST(0x00000800) /* PMU intr is synchronous */
#define   MMCR0_C56RUN	ASM_CONST(0x00000100) /* PMC5/6 count when RUN=0 */
/* performance monitor alert has occurred, set to 0 after handling exception */
#define   MMCR0_PMAO	ASM_CONST(0x00000080)
#define   MMCR0_SHRFC	0x00000040UL /* SHRre freeze conditions between threads */
#define   MMCR0_FC56	0x00000010UL /* freeze counters 5 and 6 */
#define   MMCR0_FCTI	0x00000008UL /* freeze counters in tags inactive mode */
#define   MMCR0_FCTA	0x00000004UL /* freeze counters in tags active mode */
#define   MMCR0_FCWAIT	0x00000002UL /* freeze counter in WAIT state */
#define   MMCR0_FCHV	0x00000001UL /* freeze conditions in hypervisor mode */
#define SPRN_MMCR1	798
#define SPRN_MMCR2	785
#define SPRN_MMCRA	0x312
#define   MMCRA_SDSYNC	0x80000000UL /* SDAR synced with SIAR */
#define   MMCRA_SDAR_DCACHE_MISS 0x40000000UL
#define   MMCRA_SDAR_ERAT_MISS   0x20000000UL
#define   MMCRA_SIHV	0x10000000UL /* state of MSR HV when SIAR set */
#define   MMCRA_SIPR	0x08000000UL /* state of MSR PR when SIAR set */
#define   MMCRA_SLOT	0x07000000UL /* SLOT bits (37-39) */
#define   MMCRA_SLOT_SHIFT	24
#define   MMCRA_SAMPLE_ENABLE 0x00000001UL /* enable sampling */
#define   POWER6_MMCRA_SDSYNC 0x0000080000000000ULL	/* SDAR/SIAR synced */
#define   POWER6_MMCRA_SIHV   0x0000040000000000ULL
#define   POWER6_MMCRA_SIPR   0x0000020000000000ULL
#define   POWER6_MMCRA_THRM	0x00000020UL
#define   POWER6_MMCRA_OTHER	0x0000000EUL

#define   POWER7P_MMCRA_SIAR_VALID 0x10000000	/* P7+ SIAR contents valid */
#define   POWER7P_MMCRA_SDAR_VALID 0x08000000	/* P7+ SDAR contents valid */

#define SPRN_MMCRH	316	/* Hypervisor monitor mode control register */
#define SPRN_MMCRS	894	/* Supervisor monitor mode control register */
#define SPRN_MMCRC	851	/* Core monitor mode control register */
#define SPRN_EBBHR	804	/* Event based branch handler register */
#define SPRN_EBBRR	805	/* Event based branch return register */
#define SPRN_BESCR	806	/* Branch event status and control register */
#define   BESCR_GE	0x8000000000000000ULL /* Global Enable */
#define SPRN_WORT	895	/* Workload optimization register - thread */
#define SPRN_WORC	863	/* Workload optimization register - core */

#define SPRN_PMC1	787
#define SPRN_PMC2	788
#define SPRN_PMC3	789
#define SPRN_PMC4	790
#define SPRN_PMC5	791
#define SPRN_PMC6	792
#define SPRN_PMC7	793
#define SPRN_PMC8	794
#define SPRN_SIER	784
#define   SIER_SIPR		0x2000000	/* Sampled MSR_PR */
#define   SIER_SIHV		0x1000000	/* Sampled MSR_HV */
#define   SIER_SIAR_VALID	0x0400000	/* SIAR contents valid */
#define   SIER_SDAR_VALID	0x0200000	/* SDAR contents valid */
#define SPRN_SIAR	796
#define SPRN_SDAR	797
#define SPRN_TACR	888
#define SPRN_TCSCR	889
#define SPRN_CSIGR	890
#define SPRN_SPMC1	892
#define SPRN_SPMC2	893

/* When EBB is enabled, some of MMCR0/MMCR2/SIER are user accessible */
#define MMCR0_USER_MASK	(MMCR0_FC | MMCR0_PMXE | MMCR0_PMAO)
#define MMCR2_USER_MASK	0x4020100804020000UL /* (FC1P|FC2P|FC3P|FC4P|FC5P|FC6P) */
#define SIER_USER_MASK	0x7fffffUL

#define SPRN_PA6T_MMCR0 795
#define   PA6T_MMCR0_EN0	0x0000000000000001UL
#define   PA6T_MMCR0_EN1	0x0000000000000002UL
#define   PA6T_MMCR0_EN2	0x0000000000000004UL
#define   PA6T_MMCR0_EN3	0x0000000000000008UL
#define   PA6T_MMCR0_EN4	0x0000000000000010UL
#define   PA6T_MMCR0_EN5	0x0000000000000020UL
#define   PA6T_MMCR0_SUPEN	0x0000000000000040UL
#define   PA6T_MMCR0_PREN	0x0000000000000080UL
#define   PA6T_MMCR0_HYPEN	0x0000000000000100UL
#define   PA6T_MMCR0_FCM0	0x0000000000000200UL
#define   PA6T_MMCR0_FCM1	0x0000000000000400UL
#define   PA6T_MMCR0_INTGEN	0x0000000000000800UL
#define   PA6T_MMCR0_INTEN0	0x0000000000001000UL
#define   PA6T_MMCR0_INTEN1	0x0000000000002000UL
#define   PA6T_MMCR0_INTEN2	0x0000000000004000UL
#define   PA6T_MMCR0_INTEN3	0x0000000000008000UL
#define   PA6T_MMCR0_INTEN4	0x0000000000010000UL
#define   PA6T_MMCR0_INTEN5	0x0000000000020000UL
#define   PA6T_MMCR0_DISCNT	0x0000000000040000UL
#define   PA6T_MMCR0_UOP	0x0000000000080000UL
#define   PA6T_MMCR0_TRG	0x0000000000100000UL
#define   PA6T_MMCR0_TRGEN	0x0000000000200000UL
#define   PA6T_MMCR0_TRGREG	0x0000000001600000UL
#define   PA6T_MMCR0_SIARLOG	0x0000000002000000UL
#define   PA6T_MMCR0_SDARLOG	0x0000000004000000UL
#define   PA6T_MMCR0_PROEN	0x0000000008000000UL
#define   PA6T_MMCR0_PROLOG	0x0000000010000000UL
#define   PA6T_MMCR0_DAMEN2	0x0000000020000000UL
#define   PA6T_MMCR0_DAMEN3	0x0000000040000000UL
#define   PA6T_MMCR0_DAMEN4	0x0000000080000000UL
#define   PA6T_MMCR0_DAMEN5	0x0000000100000000UL
#define   PA6T_MMCR0_DAMSEL2	0x0000000200000000UL
#define   PA6T_MMCR0_DAMSEL3	0x0000000400000000UL
#define   PA6T_MMCR0_DAMSEL4	0x0000000800000000UL
#define   PA6T_MMCR0_DAMSEL5	0x0000001000000000UL
#define   PA6T_MMCR0_HANDDIS	0x0000002000000000UL
#define   PA6T_MMCR0_PCTEN	0x0000004000000000UL
#define   PA6T_MMCR0_SOCEN	0x0000008000000000UL
#define   PA6T_MMCR0_SOCMOD	0x0000010000000000UL

#define SPRN_PA6T_MMCR1 798
#define   PA6T_MMCR1_ES2	0x00000000000000ffUL
#define   PA6T_MMCR1_ES3	0x000000000000ff00UL
#define   PA6T_MMCR1_ES4	0x0000000000ff0000UL
#define   PA6T_MMCR1_ES5	0x00000000ff000000UL

#define SPRN_PA6T_UPMC0 771	/* User PerfMon Counter 0 */
#define SPRN_PA6T_UPMC1 772	/* ... */
#define SPRN_PA6T_UPMC2 773
#define SPRN_PA6T_UPMC3 774
#define SPRN_PA6T_UPMC4 775
#define SPRN_PA6T_UPMC5 776
#define SPRN_PA6T_UMMCR0 779	/* User Monitor Mode Control Register 0 */
#define SPRN_PA6T_SIAR	780	/* Sampled Instruction Address */
#define SPRN_PA6T_UMMCR1 782	/* User Monitor Mode Control Register 1 */
#define SPRN_PA6T_SIER	785	/* Sampled Instruction Event Register */
#define SPRN_PA6T_PMC0	787
#define SPRN_PA6T_PMC1	788
#define SPRN_PA6T_PMC2	789
#define SPRN_PA6T_PMC3	790
#define SPRN_PA6T_PMC4	791
#define SPRN_PA6T_PMC5	792
#define SPRN_PA6T_TSR0	793	/* Timestamp Register 0 */
#define SPRN_PA6T_TSR1	794	/* Timestamp Register 1 */
#define SPRN_PA6T_TSR2	799	/* Timestamp Register 2 */
#define SPRN_PA6T_TSR3	784	/* Timestamp Register 3 */

#define SPRN_PA6T_IER	981	/* Icache Error Register */
#define SPRN_PA6T_DER	982	/* Dcache Error Register */
#define SPRN_PA6T_BER	862	/* BIU Error Address Register */
#define SPRN_PA6T_MER	849	/* MMU Error Register */

#define SPRN_PA6T_IMA0	880	/* Instruction Match Array 0 */
#define SPRN_PA6T_IMA1	881	/* ... */
#define SPRN_PA6T_IMA2	882
#define SPRN_PA6T_IMA3	883
#define SPRN_PA6T_IMA4	884
#define SPRN_PA6T_IMA5	885
#define SPRN_PA6T_IMA6	886
#define SPRN_PA6T_IMA7	887
#define SPRN_PA6T_IMA8	888
#define SPRN_PA6T_IMA9	889
#define SPRN_PA6T_BTCR	978	/* Breakpoint and Tagging Control Register */
#define SPRN_PA6T_IMAAT	979	/* Instruction Match Array Action Table */
#define SPRN_PA6T_PCCR	1019	/* Power Counter Control Register */
#define SPRN_BKMK	1020	/* Cell Bookmark Register */
#define SPRN_PA6T_RPCCR	1021	/* Retire PC Trace Control Register */


#else /* 32-bit */
#define SPRN_MMCR0	952	/* Monitor Mode Control Register 0 */
#define   MMCR0_FC	0x80000000UL /* freeze counters */
#define   MMCR0_FCS	0x40000000UL /* freeze in supervisor state */
#define   MMCR0_FCP	0x20000000UL /* freeze in problem state */
#define   MMCR0_FCM1	0x10000000UL /* freeze counters while MSR mark = 1 */
#define   MMCR0_FCM0	0x08000000UL /* freeze counters while MSR mark = 0 */
#define   MMCR0_PMXE	0x04000000UL /* performance monitor exception enable */
#define   MMCR0_FCECE	0x02000000UL /* freeze ctrs on enabled cond or event */
#define   MMCR0_TBEE	0x00400000UL /* time base exception enable */
#define   MMCR0_PMC1CE	0x00008000UL /* PMC1 count enable*/
#define   MMCR0_PMCnCE	0x00004000UL /* count enable for all but PMC 1*/
#define   MMCR0_TRIGGER	0x00002000UL /* TRIGGER enable */
#define   MMCR0_PMC1SEL	0x00001fc0UL /* PMC 1 Event */
#define   MMCR0_PMC2SEL	0x0000003fUL /* PMC 2 Event */

#define SPRN_MMCR1	956
#define   MMCR1_PMC3SEL	0xf8000000UL /* PMC 3 Event */
#define   MMCR1_PMC4SEL	0x07c00000UL /* PMC 4 Event */
#define   MMCR1_PMC5SEL	0x003e0000UL /* PMC 5 Event */
#define   MMCR1_PMC6SEL 0x0001f800UL /* PMC 6 Event */
#define SPRN_MMCR2	944
#define SPRN_PMC1	953	/* Performance Counter Register 1 */
#define SPRN_PMC2	954	/* Performance Counter Register 2 */
#define SPRN_PMC3	957	/* Performance Counter Register 3 */
#define SPRN_PMC4	958	/* Performance Counter Register 4 */
#define SPRN_PMC5	945	/* Performance Counter Register 5 */
#define SPRN_PMC6	946	/* Performance Counter Register 6 */

#define SPRN_SIAR	955	/* Sampled Instruction Address Register */

/* Bit definitions for MMCR0 and PMC1 / PMC2. */
#define MMCR0_PMC1_CYCLES	(1 << 7)
#define MMCR0_PMC1_ICACHEMISS	(5 << 7)
#define MMCR0_PMC1_DTLB		(6 << 7)
#define MMCR0_PMC2_DCACHEMISS	0x6
#define MMCR0_PMC2_CYCLES	0x1
#define MMCR0_PMC2_ITLB		0x7
#define MMCR0_PMC2_LOADMISSTIME	0x5
#endif

/*
 * SPRG usage:
 *
 * All 64-bit:
 *	- SPRG1 stores PACA pointer except 64-bit server in
 *        HV mode in which case it is HSPRG0
 *
 * 64-bit server:
 *	- SPRG0 scratch for TM recheckpoint/reclaim (reserved for HV on Power4)
 *	- SPRG2 scratch for exception vectors
 *	- SPRG3 CPU and NUMA node for VDSO getcpu (user visible)
 *      - HSPRG0 stores PACA in HV mode
 *      - HSPRG1 scratch for "HV" exceptions
 *
 * 64-bit embedded
 *	- SPRG0 generic exception scratch
 *	- SPRG2 TLB exception stack
 *	- SPRG3 critical exception scratch (user visible, sorry!)
 *	- SPRG4 unused (user visible)
 *	- SPRG6 TLB miss scratch (user visible, sorry !)
 *	- SPRG7 CPU and NUMA node for VDSO getcpu (user visible)
 *	- SPRG8 machine check exception scratch
 *	- SPRG9 debug exception scratch
 *
 * All 32-bit:
 *	- SPRG3 current thread_info pointer
 *        (virtual on BookE, physical on others)
 *
 * 32-bit classic:
 *	- SPRG0 scratch for exception vectors
 *	- SPRG1 scratch for exception vectors
 *	- SPRG2 indicator that we are in RTAS
 *	- SPRG4 (603 only) pseudo TLB LRU data
 *
 * 32-bit 40x:
 *	- SPRG0 scratch for exception vectors
 *	- SPRG1 scratch for exception vectors
 *	- SPRG2 scratch for exception vectors
 *	- SPRG4 scratch for exception vectors (not 403)
 *	- SPRG5 scratch for exception vectors (not 403)
 *	- SPRG6 scratch for exception vectors (not 403)
 *	- SPRG7 scratch for exception vectors (not 403)
 *
 * 32-bit 440 and FSL BookE:
 *	- SPRG0 scratch for exception vectors
 *	- SPRG1 scratch for exception vectors (*)
 *	- SPRG2 scratch for crit interrupts handler
 *	- SPRG4 scratch for exception vectors
 *	- SPRG5 scratch for exception vectors
 *	- SPRG6 scratch for machine check handler
 *	- SPRG7 scratch for exception vectors
 *	- SPRG9 scratch for debug vectors (e500 only)
 *
 *      Additionally, BookE separates "read" and "write"
 *      of those registers. That allows to use the userspace
 *      readable variant for reads, which can avoid a fault
 *      with KVM type virtualization.
 *
 * 32-bit 8xx:
 *	- SPRG0 scratch for exception vectors
 *	- SPRG1 scratch for exception vectors
 *	- SPRG2 scratch for exception vectors
 *
 */
#ifdef CONFIG_PPC64
#define SPRN_SPRG_PACA 		SPRN_SPRG1
#else
#define SPRN_SPRG_THREAD 	SPRN_SPRG3
#endif

#ifdef CONFIG_PPC_BOOK3S_64
#define SPRN_SPRG_SCRATCH0	SPRN_SPRG2
#define SPRN_SPRG_HPACA		SPRN_HSPRG0
#define SPRN_SPRG_HSCRATCH0	SPRN_HSPRG1
#define SPRN_SPRG_VDSO_READ	SPRN_USPRG3
#define SPRN_SPRG_VDSO_WRITE	SPRN_SPRG3

#define GET_PACA(rX)					\
	BEGIN_FTR_SECTION_NESTED(66);			\
	mfspr	rX,SPRN_SPRG_PACA;			\
	FTR_SECTION_ELSE_NESTED(66);			\
	mfspr	rX,SPRN_SPRG_HPACA;			\
	ALT_FTR_SECTION_END_NESTED_IFCLR(CPU_FTR_HVMODE, 66)

#define SET_PACA(rX)					\
	BEGIN_FTR_SECTION_NESTED(66);			\
	mtspr	SPRN_SPRG_PACA,rX;			\
	FTR_SECTION_ELSE_NESTED(66);			\
	mtspr	SPRN_SPRG_HPACA,rX;			\
	ALT_FTR_SECTION_END_NESTED_IFCLR(CPU_FTR_HVMODE, 66)

#define GET_SCRATCH0(rX)				\
	BEGIN_FTR_SECTION_NESTED(66);			\
	mfspr	rX,SPRN_SPRG_SCRATCH0;			\
	FTR_SECTION_ELSE_NESTED(66);			\
	mfspr	rX,SPRN_SPRG_HSCRATCH0;			\
	ALT_FTR_SECTION_END_NESTED_IFCLR(CPU_FTR_HVMODE, 66)

#define SET_SCRATCH0(rX)				\
	BEGIN_FTR_SECTION_NESTED(66);			\
	mtspr	SPRN_SPRG_SCRATCH0,rX;			\
	FTR_SECTION_ELSE_NESTED(66);			\
	mtspr	SPRN_SPRG_HSCRATCH0,rX;			\
	ALT_FTR_SECTION_END_NESTED_IFCLR(CPU_FTR_HVMODE, 66)

#else /* CONFIG_PPC_BOOK3S_64 */
#define GET_SCRATCH0(rX)	mfspr	rX,SPRN_SPRG_SCRATCH0
#define SET_SCRATCH0(rX)	mtspr	SPRN_SPRG_SCRATCH0,rX

#endif

#ifdef CONFIG_PPC_BOOK3E_64
#define SPRN_SPRG_MC_SCRATCH	SPRN_SPRG8
#define SPRN_SPRG_CRIT_SCRATCH	SPRN_SPRG3
#define SPRN_SPRG_DBG_SCRATCH	SPRN_SPRG9
#define SPRN_SPRG_TLB_EXFRAME	SPRN_SPRG2
#define SPRN_SPRG_TLB_SCRATCH	SPRN_SPRG6
#define SPRN_SPRG_GEN_SCRATCH	SPRN_SPRG0
#define SPRN_SPRG_GDBELL_SCRATCH SPRN_SPRG_GEN_SCRATCH
#define SPRN_SPRG_VDSO_READ	SPRN_USPRG7
#define SPRN_SPRG_VDSO_WRITE	SPRN_SPRG7

#define SET_PACA(rX)	mtspr	SPRN_SPRG_PACA,rX
#define GET_PACA(rX)	mfspr	rX,SPRN_SPRG_PACA

#endif

#ifdef CONFIG_PPC_BOOK3S_32
#define SPRN_SPRG_SCRATCH0	SPRN_SPRG0
#define SPRN_SPRG_SCRATCH1	SPRN_SPRG1
#define SPRN_SPRG_RTAS		SPRN_SPRG2
#define SPRN_SPRG_603_LRU	SPRN_SPRG4
#endif

#ifdef CONFIG_40x
#define SPRN_SPRG_SCRATCH0	SPRN_SPRG0
#define SPRN_SPRG_SCRATCH1	SPRN_SPRG1
#define SPRN_SPRG_SCRATCH2	SPRN_SPRG2
#define SPRN_SPRG_SCRATCH3	SPRN_SPRG4
#define SPRN_SPRG_SCRATCH4	SPRN_SPRG5
#define SPRN_SPRG_SCRATCH5	SPRN_SPRG6
#define SPRN_SPRG_SCRATCH6	SPRN_SPRG7
#endif

#ifdef CONFIG_BOOKE
#define SPRN_SPRG_RSCRATCH0	SPRN_SPRG0
#define SPRN_SPRG_WSCRATCH0	SPRN_SPRG0
#define SPRN_SPRG_RSCRATCH1	SPRN_SPRG1
#define SPRN_SPRG_WSCRATCH1	SPRN_SPRG1
#define SPRN_SPRG_RSCRATCH_CRIT	SPRN_SPRG2
#define SPRN_SPRG_WSCRATCH_CRIT	SPRN_SPRG2
#define SPRN_SPRG_RSCRATCH2	SPRN_SPRG4R
#define SPRN_SPRG_WSCRATCH2	SPRN_SPRG4W
#define SPRN_SPRG_RSCRATCH3	SPRN_SPRG5R
#define SPRN_SPRG_WSCRATCH3	SPRN_SPRG5W
#define SPRN_SPRG_RSCRATCH_MC	SPRN_SPRG1
#define SPRN_SPRG_WSCRATCH_MC	SPRN_SPRG1
#define SPRN_SPRG_RSCRATCH4	SPRN_SPRG7R
#define SPRN_SPRG_WSCRATCH4	SPRN_SPRG7W
#ifdef CONFIG_E200
#define SPRN_SPRG_RSCRATCH_DBG	SPRN_SPRG6R
#define SPRN_SPRG_WSCRATCH_DBG	SPRN_SPRG6W
#else
#define SPRN_SPRG_RSCRATCH_DBG	SPRN_SPRG9
#define SPRN_SPRG_WSCRATCH_DBG	SPRN_SPRG9
#endif
#endif

#ifdef CONFIG_8xx
#define SPRN_SPRG_SCRATCH0	SPRN_SPRG0
#define SPRN_SPRG_SCRATCH1	SPRN_SPRG1
#define SPRN_SPRG_SCRATCH2	SPRN_SPRG2
#endif



/*
 * An mtfsf instruction with the L bit set. On CPUs that support this a
 * full 64bits of FPSCR is restored and on other CPUs the L bit is ignored.
 *
 * Until binutils gets the new form of mtfsf, hardwire the instruction.
 */
#ifdef CONFIG_PPC64
#define MTFSF_L(REG) \
	.long (0xfc00058e | ((0xff) << 17) | ((REG) << 11) | (1 << 25))
#else
#define MTFSF_L(REG)	mtfsf	0xff, (REG)
#endif

/* Processor Version Register (PVR) field extraction */

#define PVR_VER(pvr)	(((pvr) >>  16) & 0xFFFF)	/* Version field */
#define PVR_REV(pvr)	(((pvr) >>   0) & 0xFFFF)	/* Revison field */

#define pvr_version_is(pvr)	(PVR_VER(mfspr(SPRN_PVR)) == (pvr))

/*
 * IBM has further subdivided the standard PowerPC 16-bit version and
 * revision subfields of the PVR for the PowerPC 403s into the following:
 */

#define PVR_FAM(pvr)	(((pvr) >> 20) & 0xFFF)	/* Family field */
#define PVR_MEM(pvr)	(((pvr) >> 16) & 0xF)	/* Member field */
#define PVR_CORE(pvr)	(((pvr) >> 12) & 0xF)	/* Core field */
#define PVR_CFG(pvr)	(((pvr) >>  8) & 0xF)	/* Configuration field */
#define PVR_MAJ(pvr)	(((pvr) >>  4) & 0xF)	/* Major revision field */
#define PVR_MIN(pvr)	(((pvr) >>  0) & 0xF)	/* Minor revision field */

/* Processor Version Numbers */

#define PVR_403GA	0x00200000
#define PVR_403GB	0x00200100
#define PVR_403GC	0x00200200
#define PVR_403GCX	0x00201400
#define PVR_405GP	0x40110000
#define PVR_476		0x11a52000
#define PVR_476FPE	0x7ff50000
#define PVR_STB03XXX	0x40310000
#define PVR_NP405H	0x41410000
#define PVR_NP405L	0x41610000
#define PVR_601		0x00010000
#define PVR_602		0x00050000
#define PVR_603		0x00030000
#define PVR_603e	0x00060000
#define PVR_603ev	0x00070000
#define PVR_603r	0x00071000
#define PVR_604		0x00040000
#define PVR_604e	0x00090000
#define PVR_604r	0x000A0000
#define PVR_620		0x00140000
#define PVR_740		0x00080000
#define PVR_750		PVR_740
#define PVR_740P	0x10080000
#define PVR_750P	PVR_740P
#define PVR_7400	0x000C0000
#define PVR_7410	0x800C0000
#define PVR_7450	0x80000000
#define PVR_8540	0x80200000
#define PVR_8560	0x80200000
#define PVR_VER_E500V1	0x8020
#define PVR_VER_E500V2	0x8021
#define PVR_VER_E500MC	0x8023
#define PVR_VER_E5500	0x8024
#define PVR_VER_E6500	0x8040

/*
 * For the 8xx processors, all of them report the same PVR family for
 * the PowerPC core. The various versions of these processors must be
 * differentiated by the version number in the Communication Processor
 * Module (CPM).
 */
#define PVR_821		0x00500000
#define PVR_823		PVR_821
#define PVR_850		PVR_821
#define PVR_860		PVR_821
#define PVR_8240	0x00810100
#define PVR_8245	0x80811014
#define PVR_8260	PVR_8240

/* 476 Simulator seems to currently have the PVR of the 602... */
#define PVR_476_ISS	0x00052000

/* 64-bit processors */
#define PVR_NORTHSTAR	0x0033
#define PVR_PULSAR	0x0034
#define PVR_POWER4	0x0035
#define PVR_ICESTAR	0x0036
#define PVR_SSTAR	0x0037
#define PVR_POWER4p	0x0038
#define PVR_970		0x0039
#define PVR_POWER5	0x003A
#define PVR_POWER5p	0x003B
#define PVR_970FX	0x003C
#define PVR_POWER6	0x003E
#define PVR_POWER7	0x003F
#define PVR_630		0x0040
#define PVR_630p	0x0041
#define PVR_970MP	0x0044
#define PVR_970GX	0x0045
#define PVR_POWER7p	0x004A
#define PVR_POWER8E	0x004B
#define PVR_POWER8NVL	0x004C
#define PVR_POWER8	0x004D
#define PVR_BE		0x0070
#define PVR_PA6T	0x0090

/* "Logical" PVR values defined in PAPR, representing architecture levels */
#define PVR_ARCH_204	0x0f000001
#define PVR_ARCH_205	0x0f000002
#define PVR_ARCH_206	0x0f000003
#define PVR_ARCH_206p	0x0f100003
#define PVR_ARCH_207	0x0f000004

/* Macros for setting and retrieving special purpose registers */
#ifndef __ASSEMBLY__
#define mfmsr()		({unsigned long rval; \
			asm volatile("mfmsr %0" : "=r" (rval) : \
						: "memory"); rval;})
#ifdef CONFIG_PPC_BOOK3S_64
#define __mtmsrd(v, l)	asm volatile("mtmsrd %0," __stringify(l) \
				     : : "r" (v) : "memory")
#define mtmsr(v)	__mtmsrd((v), 0)
#define __MTMSR		"mtmsrd"
#else
#define mtmsr(v)	asm volatile("mtmsr %0" : \
				     : "r" ((unsigned long)(v)) \
				     : "memory")
#define __MTMSR		"mtmsr"
#endif

static inline void mtmsr_isync(unsigned long val)
{
	asm volatile(__MTMSR " %0; " ASM_FTR_IFCLR("isync", "nop", %1) : :
			"r" (val), "i" (CPU_FTR_ARCH_206) : "memory");
}

#define mfspr(rn)	({unsigned long rval; \
			asm volatile("mfspr %0," __stringify(rn) \
				: "=r" (rval)); rval;})
#ifndef mtspr
#define mtspr(rn, v)	asm volatile("mtspr " __stringify(rn) ",%0" : \
				     : "r" ((unsigned long)(v)) \
				     : "memory")
#endif

extern void msr_check_and_set(unsigned long bits);
extern bool strict_msr_control;
extern void __msr_check_and_clear(unsigned long bits);
static inline void msr_check_and_clear(unsigned long bits)
{
	if (strict_msr_control)
		__msr_check_and_clear(bits);
}

static inline unsigned long mfvtb (void)
{
#ifdef CONFIG_PPC_BOOK3S_64
	if (cpu_has_feature(CPU_FTR_ARCH_207S))
		return mfspr(SPRN_VTB);
#endif
	return 0;
}

#ifdef __powerpc64__
#if defined(CONFIG_PPC_CELL) || defined(CONFIG_PPC_FSL_BOOK3E)
#define mftb()		({unsigned long rval;				\
			asm volatile(					\
				"90:	mfspr %0, %2;\n"		\
				"97:	cmpwi %0,0;\n"			\
				"	beq- 90b;\n"			\
				"99:\n"					\
				".section __ftr_fixup,\"a\"\n"		\
				".align 3\n"				\
				"98:\n"					\
				"	.llong %1\n"			\
				"	.llong %1\n"			\
				"	.llong 97b-98b\n"		\
				"	.llong 99b-98b\n"		\
				"	.llong 0\n"			\
				"	.llong 0\n"			\
				".previous"				\
			: "=r" (rval) \
			: "i" (CPU_FTR_CELL_TB_BUG), "i" (SPRN_TBRL)); \
			rval;})
#else
#define mftb()		({unsigned long rval;	\
			asm volatile("mfspr %0, %1" : \
				     "=r" (rval) : "i" (SPRN_TBRL)); rval;})
#endif /* !CONFIG_PPC_CELL */

#else /* __powerpc64__ */

#if defined(CONFIG_8xx)
#define mftbl()		({unsigned long rval;	\
			asm volatile("mftbl %0" : "=r" (rval)); rval;})
#define mftbu()		({unsigned long rval;	\
			asm volatile("mftbu %0" : "=r" (rval)); rval;})
#else
#define mftbl()		({unsigned long rval;	\
			asm volatile("mfspr %0, %1" : "=r" (rval) : \
				"i" (SPRN_TBRL)); rval;})
#define mftbu()		({unsigned long rval;	\
			asm volatile("mfspr %0, %1" : "=r" (rval) : \
				"i" (SPRN_TBRU)); rval;})
#endif
#endif /* !__powerpc64__ */

#define mttbl(v)	asm volatile("mttbl %0":: "r"(v))
#define mttbu(v)	asm volatile("mttbu %0":: "r"(v))

#ifdef CONFIG_PPC32
#define mfsrin(v)	({unsigned int rval; \
			asm volatile("mfsrin %0,%1" : "=r" (rval) : "r" (v)); \
					rval;})
#endif

#define proc_trap()	asm volatile("trap")

extern unsigned long current_stack_pointer(void);

extern unsigned long scom970_read(unsigned int address);
extern void scom970_write(unsigned int address, unsigned long value);

struct pt_regs;

extern void ppc_save_regs(struct pt_regs *regs);

static inline void update_power8_hid0(unsigned long hid0)
{
	/*
	 *  The HID0 update on Power8 should at the very least be
	 *  preceded by a a SYNC instruction followed by an ISYNC
	 *  instruction
	 */
	asm volatile("sync; mtspr %0,%1; isync":: "i"(SPRN_HID0), "r"(hid0));
}
#endif /* __ASSEMBLY__ */
#endif /* __KERNEL__ */
#endif /* _ASM_POWERPC_REG_H */
