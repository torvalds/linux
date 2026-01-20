/* SPDX-License-Identifier: GPL-2.0 */
/*
 * cpu.h: Values of the PRID register used to match up
 *	  various LoongArch CPU types.
 *
 * Copyright (C) 2020-2022 Loongson Technology Corporation Limited
 */
#ifndef _ASM_CPU_H
#define _ASM_CPU_H

/*
 * As described in LoongArch specs from Loongson Technology, the PRID register
 * (CPUCFG.00) has the following layout:
 *
 * +---------------+----------------+------------+--------------------+
 * | Reserved      | Company ID     | Series ID  |  Product ID        |
 * +---------------+----------------+------------+--------------------+
 *  31		 24 23		  16 15	       12 11		     0
 */

/*
 * Assigned Company values for bits 23:16 of the PRID register.
 */

#define PRID_COMP_MASK		0xff0000

#define PRID_COMP_LOONGSON	0x140000

/*
 * Assigned Series ID values for bits 15:12 of the PRID register. In order
 * to detect a certain CPU type exactly eventually additional registers may
 * need to be examined.
 */

#define PRID_SERIES_MASK	0xf000

#define PRID_SERIES_LA132	0x8000  /* Loongson 32bit */
#define PRID_SERIES_LA264	0xa000  /* Loongson 64bit, 2-issue */
#define PRID_SERIES_LA364	0xb000  /* Loongson 64bit, 3-issue */
#define PRID_SERIES_LA464	0xc000  /* Loongson 64bit, 4-issue */
#define PRID_SERIES_LA664	0xd000  /* Loongson 64bit, 6-issue */

/*
 * Particular Product ID values for bits 11:0 of the PRID register.
 */

#define PRID_PRODUCT_MASK	0x0fff

#if !defined(__ASSEMBLER__)

enum cpu_type_enum {
	CPU_UNKNOWN,
	CPU_LOONGSON32,
	CPU_LOONGSON64,
	CPU_LAST
};

static inline char *id_to_core_name(unsigned int id)
{
	if ((id & PRID_COMP_MASK) != PRID_COMP_LOONGSON)
		return "Unknown";

	switch (id & PRID_SERIES_MASK) {
	case PRID_SERIES_LA132:
		return "LA132";
	case PRID_SERIES_LA264:
		return "LA264";
	case PRID_SERIES_LA364:
		return "LA364";
	case PRID_SERIES_LA464:
		return "LA464";
	case PRID_SERIES_LA664:
		return "LA664";
	default:
		return "Unknown";
	}
}

#endif /* !__ASSEMBLER__ */

/*
 * ISA Level encodings
 *
 */

#define LOONGARCH_CPU_ISA_LA32R 0x00000001
#define LOONGARCH_CPU_ISA_LA32S 0x00000002
#define LOONGARCH_CPU_ISA_LA64  0x00000004

#define LOONGARCH_CPU_ISA_32BIT (LOONGARCH_CPU_ISA_LA32R | LOONGARCH_CPU_ISA_LA32S)
#define LOONGARCH_CPU_ISA_64BIT LOONGARCH_CPU_ISA_LA64

/*
 * CPU Option encodings
 */
#define CPU_FEATURE_CPUCFG		0	/* CPU has CPUCFG */
#define CPU_FEATURE_LAM			1	/* CPU has Atomic instructions */
#define CPU_FEATURE_UAL			2	/* CPU supports unaligned access */
#define CPU_FEATURE_FPU			3	/* CPU has FPU */
#define CPU_FEATURE_LSX			4	/* CPU has LSX (128-bit SIMD) */
#define CPU_FEATURE_LASX		5	/* CPU has LASX (256-bit SIMD) */
#define CPU_FEATURE_CRC32		6	/* CPU has CRC32 instructions */
#define CPU_FEATURE_COMPLEX		7	/* CPU has Complex instructions */
#define CPU_FEATURE_CRYPTO		8	/* CPU has Crypto instructions */
#define CPU_FEATURE_LVZ			9	/* CPU has Virtualization extension */
#define CPU_FEATURE_LBT_X86		10	/* CPU has X86 Binary Translation */
#define CPU_FEATURE_LBT_ARM		11	/* CPU has ARM Binary Translation */
#define CPU_FEATURE_LBT_MIPS		12	/* CPU has MIPS Binary Translation */
#define CPU_FEATURE_TLB			13	/* CPU has TLB */
#define CPU_FEATURE_CSR			14	/* CPU has CSR */
#define CPU_FEATURE_IOCSR		15	/* CPU has IOCSR */
#define CPU_FEATURE_WATCH		16	/* CPU has watchpoint registers */
#define CPU_FEATURE_VINT		17	/* CPU has vectored interrupts */
#define CPU_FEATURE_CSRIPI		18	/* CPU has CSR-IPI */
#define CPU_FEATURE_EXTIOI		19	/* CPU has EXT-IOI */
#define CPU_FEATURE_PREFETCH		20	/* CPU has prefetch instructions */
#define CPU_FEATURE_PMP			21	/* CPU has perfermance counter */
#define CPU_FEATURE_SCALEFREQ		22	/* CPU supports cpufreq scaling */
#define CPU_FEATURE_FLATMODE		23	/* CPU has flat mode */
#define CPU_FEATURE_EIODECODE		24	/* CPU has EXTIOI interrupt pin decode mode */
#define CPU_FEATURE_GUESTID		25	/* CPU has GuestID feature */
#define CPU_FEATURE_HYPERVISOR		26	/* CPU has hypervisor (running in VM) */
#define CPU_FEATURE_PTW			27	/* CPU has hardware page table walker */
#define CPU_FEATURE_LSPW		28	/* CPU has LSPW (lddir/ldpte instructions) */
#define CPU_FEATURE_MSGINT		29	/* CPU has MSG interrupt */
#define CPU_FEATURE_AVECINT		30	/* CPU has AVEC interrupt */
#define CPU_FEATURE_REDIRECTINT		31	/* CPU has interrupt remapping */

#define LOONGARCH_CPU_CPUCFG		BIT_ULL(CPU_FEATURE_CPUCFG)
#define LOONGARCH_CPU_LAM		BIT_ULL(CPU_FEATURE_LAM)
#define LOONGARCH_CPU_UAL		BIT_ULL(CPU_FEATURE_UAL)
#define LOONGARCH_CPU_FPU		BIT_ULL(CPU_FEATURE_FPU)
#define LOONGARCH_CPU_LSX		BIT_ULL(CPU_FEATURE_LSX)
#define LOONGARCH_CPU_LASX		BIT_ULL(CPU_FEATURE_LASX)
#define LOONGARCH_CPU_CRC32		BIT_ULL(CPU_FEATURE_CRC32)
#define LOONGARCH_CPU_COMPLEX		BIT_ULL(CPU_FEATURE_COMPLEX)
#define LOONGARCH_CPU_CRYPTO		BIT_ULL(CPU_FEATURE_CRYPTO)
#define LOONGARCH_CPU_LVZ		BIT_ULL(CPU_FEATURE_LVZ)
#define LOONGARCH_CPU_LBT_X86		BIT_ULL(CPU_FEATURE_LBT_X86)
#define LOONGARCH_CPU_LBT_ARM		BIT_ULL(CPU_FEATURE_LBT_ARM)
#define LOONGARCH_CPU_LBT_MIPS		BIT_ULL(CPU_FEATURE_LBT_MIPS)
#define LOONGARCH_CPU_TLB		BIT_ULL(CPU_FEATURE_TLB)
#define LOONGARCH_CPU_IOCSR		BIT_ULL(CPU_FEATURE_IOCSR)
#define LOONGARCH_CPU_CSR		BIT_ULL(CPU_FEATURE_CSR)
#define LOONGARCH_CPU_WATCH		BIT_ULL(CPU_FEATURE_WATCH)
#define LOONGARCH_CPU_VINT		BIT_ULL(CPU_FEATURE_VINT)
#define LOONGARCH_CPU_CSRIPI		BIT_ULL(CPU_FEATURE_CSRIPI)
#define LOONGARCH_CPU_EXTIOI		BIT_ULL(CPU_FEATURE_EXTIOI)
#define LOONGARCH_CPU_PREFETCH		BIT_ULL(CPU_FEATURE_PREFETCH)
#define LOONGARCH_CPU_PMP		BIT_ULL(CPU_FEATURE_PMP)
#define LOONGARCH_CPU_SCALEFREQ		BIT_ULL(CPU_FEATURE_SCALEFREQ)
#define LOONGARCH_CPU_FLATMODE		BIT_ULL(CPU_FEATURE_FLATMODE)
#define LOONGARCH_CPU_EIODECODE		BIT_ULL(CPU_FEATURE_EIODECODE)
#define LOONGARCH_CPU_GUESTID		BIT_ULL(CPU_FEATURE_GUESTID)
#define LOONGARCH_CPU_HYPERVISOR	BIT_ULL(CPU_FEATURE_HYPERVISOR)
#define LOONGARCH_CPU_PTW		BIT_ULL(CPU_FEATURE_PTW)
#define LOONGARCH_CPU_LSPW		BIT_ULL(CPU_FEATURE_LSPW)
#define LOONGARCH_CPU_MSGINT		BIT_ULL(CPU_FEATURE_MSGINT)
#define LOONGARCH_CPU_AVECINT		BIT_ULL(CPU_FEATURE_AVECINT)
#define LOONGARCH_CPU_REDIRECTINT	BIT_ULL(CPU_FEATURE_REDIRECTINT)

#endif /* _ASM_CPU_H */
