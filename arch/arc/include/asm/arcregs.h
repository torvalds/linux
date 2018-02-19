/*
 * Copyright (C) 2004, 2007-2010, 2011-2012 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef _ASM_ARC_ARCREGS_H
#define _ASM_ARC_ARCREGS_H

/* Build Configuration Registers */
#define ARC_REG_AUX_DCCM	0x18	/* DCCM Base Addr ARCv2 */
#define ARC_REG_ERP_CTRL	0x3F	/* ARCv2 Error protection control */
#define ARC_REG_DCCM_BASE_BUILD	0x61	/* DCCM Base Addr ARCompact */
#define ARC_REG_CRC_BCR		0x62
#define ARC_REG_VECBASE_BCR	0x68
#define ARC_REG_PERIBASE_BCR	0x69
#define ARC_REG_FP_BCR		0x6B	/* ARCompact: Single-Precision FPU */
#define ARC_REG_DPFP_BCR	0x6C	/* ARCompact: Dbl Precision FPU */
#define ARC_REG_ERP_BUILD	0xc7	/* ARCv2 Error protection Build: ECC/Parity */
#define ARC_REG_FP_V2_BCR	0xc8	/* ARCv2 FPU */
#define ARC_REG_SLC_BCR		0xce
#define ARC_REG_DCCM_BUILD	0x74	/* DCCM size (common) */
#define ARC_REG_AP_BCR		0x76
#define ARC_REG_ICCM_BUILD	0x78	/* ICCM size (common) */
#define ARC_REG_XY_MEM_BCR	0x79
#define ARC_REG_MAC_BCR		0x7a
#define ARC_REG_MUL_BCR		0x7b
#define ARC_REG_SWAP_BCR	0x7c
#define ARC_REG_NORM_BCR	0x7d
#define ARC_REG_MIXMAX_BCR	0x7e
#define ARC_REG_BARREL_BCR	0x7f
#define ARC_REG_D_UNCACH_BCR	0x6A
#define ARC_REG_BPU_BCR		0xc0
#define ARC_REG_ISA_CFG_BCR	0xc1
#define ARC_REG_LPB_BUILD	0xE9	/* ARCv2 Loop Buffer Build */
#define ARC_REG_RTT_BCR		0xF2
#define ARC_REG_IRQ_BCR		0xF3
#define ARC_REG_MICRO_ARCH_BCR	0xF9	/* ARCv2 Product revision */
#define ARC_REG_SMART_BCR	0xFF
#define ARC_REG_CLUSTER_BCR	0xcf
#define ARC_REG_AUX_ICCM	0x208	/* ICCM Base Addr (ARCv2) */
#define ARC_REG_LPB_CTRL	0x488	/* ARCv2 Loop Buffer control */

/* Common for ARCompact and ARCv2 status register */
#define ARC_REG_STATUS32	0x0A

/* status32 Bits Positions */
#define STATUS_AE_BIT		5	/* Exception active */
#define STATUS_DE_BIT		6	/* PC is in delay slot */
#define STATUS_U_BIT		7	/* User/Kernel mode */
#define STATUS_Z_BIT            11
#define STATUS_L_BIT		12	/* Loop inhibit */

/* These masks correspond to the status word(STATUS_32) bits */
#define STATUS_AE_MASK		(1<<STATUS_AE_BIT)
#define STATUS_DE_MASK		(1<<STATUS_DE_BIT)
#define STATUS_U_MASK		(1<<STATUS_U_BIT)
#define STATUS_Z_MASK		(1<<STATUS_Z_BIT)
#define STATUS_L_MASK		(1<<STATUS_L_BIT)

/*
 * ECR: Exception Cause Reg bits-n-pieces
 * [23:16] = Exception Vector
 * [15: 8] = Exception Cause Code
 * [ 7: 0] = Exception Parameters (for certain types only)
 */
#ifdef CONFIG_ISA_ARCOMPACT
#define ECR_V_MEM_ERR			0x01
#define ECR_V_INSN_ERR			0x02
#define ECR_V_MACH_CHK			0x20
#define ECR_V_ITLB_MISS			0x21
#define ECR_V_DTLB_MISS			0x22
#define ECR_V_PROTV			0x23
#define ECR_V_TRAP			0x25
#else
#define ECR_V_MEM_ERR			0x01
#define ECR_V_INSN_ERR			0x02
#define ECR_V_MACH_CHK			0x03
#define ECR_V_ITLB_MISS			0x04
#define ECR_V_DTLB_MISS			0x05
#define ECR_V_PROTV			0x06
#define ECR_V_TRAP			0x09
#endif

/* DTLB Miss and Protection Violation Cause Codes */

#define ECR_C_PROTV_INST_FETCH		0x00
#define ECR_C_PROTV_LOAD		0x01
#define ECR_C_PROTV_STORE		0x02
#define ECR_C_PROTV_XCHG		0x03
#define ECR_C_PROTV_MISALIG_DATA	0x04

#define ECR_C_BIT_PROTV_MISALIG_DATA	10

/* Machine Check Cause Code Values */
#define ECR_C_MCHK_DUP_TLB		0x01

/* DTLB Miss Exception Cause Code Values */
#define ECR_C_BIT_DTLB_LD_MISS		8
#define ECR_C_BIT_DTLB_ST_MISS		9

/* Auxiliary registers */
#define AUX_IDENTITY		4
#define AUX_EXEC_CTRL		8
#define AUX_INTR_VEC_BASE	0x25
#define AUX_VOL			0x5e

/*
 * Floating Pt Registers
 * Status regs are read-only (build-time) so need not be saved/restored
 */
#define ARC_AUX_FP_STAT         0x300
#define ARC_AUX_DPFP_1L         0x301
#define ARC_AUX_DPFP_1H         0x302
#define ARC_AUX_DPFP_2L         0x303
#define ARC_AUX_DPFP_2H         0x304
#define ARC_AUX_DPFP_STAT       0x305

#ifndef __ASSEMBLY__

#include <soc/arc/aux.h>

/* Helpers */
#define TO_KB(bytes)		((bytes) >> 10)
#define TO_MB(bytes)		(TO_KB(bytes) >> 10)
#define PAGES_TO_KB(n_pages)	((n_pages) << (PAGE_SHIFT - 10))
#define PAGES_TO_MB(n_pages)	(PAGES_TO_KB(n_pages) >> 10)


/*
 ***************************************************************
 * Build Configuration Registers, with encoded hardware config
 */
struct bcr_identity {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int chip_id:16, cpu_id:8, family:8;
#else
	unsigned int family:8, cpu_id:8, chip_id:16;
#endif
};

struct bcr_isa_arcv2 {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int div_rem:4, pad2:4, ldd:1, unalign:1, atomic:1, be:1,
		     pad1:12, ver:8;
#else
	unsigned int ver:8, pad1:12, be:1, atomic:1, unalign:1,
		     ldd:1, pad2:4, div_rem:4;
#endif
};

struct bcr_mpy {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int pad:8, x1616:8, dsp:4, cycles:2, type:2, ver:8;
#else
	unsigned int ver:8, type:2, cycles:2, dsp:4, x1616:8, pad:8;
#endif
};

struct bcr_extn_xymem {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int ram_org:2, num_banks:4, bank_sz:4, ver:8;
#else
	unsigned int ver:8, bank_sz:4, num_banks:4, ram_org:2;
#endif
};

struct bcr_iccm_arcompact {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int base:16, pad:5, sz:3, ver:8;
#else
	unsigned int ver:8, sz:3, pad:5, base:16;
#endif
};

struct bcr_iccm_arcv2 {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int pad:8, sz11:4, sz01:4, sz10:4, sz00:4, ver:8;
#else
	unsigned int ver:8, sz00:4, sz10:4, sz01:4, sz11:4, pad:8;
#endif
};

struct bcr_dccm_arcompact {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int res:21, sz:3, ver:8;
#else
	unsigned int ver:8, sz:3, res:21;
#endif
};

struct bcr_dccm_arcv2 {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int pad2:12, cyc:3, pad1:1, sz1:4, sz0:4, ver:8;
#else
	unsigned int ver:8, sz0:4, sz1:4, pad1:1, cyc:3, pad2:12;
#endif
};

/* ARCompact: Both SP and DP FPU BCRs have same format */
struct bcr_fp_arcompact {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int fast:1, ver:8;
#else
	unsigned int ver:8, fast:1;
#endif
};

struct bcr_fp_arcv2 {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int pad2:15, dp:1, pad1:7, sp:1, ver:8;
#else
	unsigned int ver:8, sp:1, pad1:7, dp:1, pad2:15;
#endif
};

#include <soc/arc/timers.h>

struct bcr_bpu_arcompact {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int pad2:19, fam:1, pad:2, ent:2, ver:8;
#else
	unsigned int ver:8, ent:2, pad:2, fam:1, pad2:19;
#endif
};

struct bcr_bpu_arcv2 {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int pad:6, fbe:2, tqe:2, ts:4, ft:1, rse:2, pte:3, bce:3, ver:8;
#else
	unsigned int ver:8, bce:3, pte:3, rse:2, ft:1, ts:4, tqe:2, fbe:2, pad:6;
#endif
};

/* Error Protection Build: ECC/Parity */
struct bcr_erp {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int pad3:5, mmu:3, pad2:4, ic:3, dc:3, pad1:6, ver:8;
#else
	unsigned int ver:8, pad1:6, dc:3, ic:3, pad2:4, mmu:3, pad3:5;
#endif
};

/* Error Protection Control */
struct ctl_erp {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int pad2:27, mpd:1, pad1:2, dpd:1, dpi:1;
#else
	unsigned int dpi:1, dpd:1, pad1:2, mpd:1, pad2:27;
#endif
};

struct bcr_lpb {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int pad:16, entries:8, ver:8;
#else
	unsigned int ver:8, entries:8, pad:16;
#endif
};

struct bcr_generic {
#ifdef CONFIG_CPU_BIG_ENDIAN
	unsigned int info:24, ver:8;
#else
	unsigned int ver:8, info:24;
#endif
};

/*
 *******************************************************************
 * Generic structures to hold build configuration used at runtime
 */

struct cpuinfo_arc_mmu {
	unsigned int ver:4, pg_sz_k:8, s_pg_sz_m:8, pad:10, sasid:1, pae:1;
	unsigned int sets:12, ways:4, u_dtlb:8, u_itlb:8;
};

struct cpuinfo_arc_cache {
	unsigned int sz_k:14, line_len:8, assoc:4, alias:1, vipt:1, pad:4;
};

struct cpuinfo_arc_bpu {
	unsigned int ver, full, num_cache, num_pred;
};

struct cpuinfo_arc_ccm {
	unsigned int base_addr, sz;
};

struct cpuinfo_arc {
	struct cpuinfo_arc_cache icache, dcache, slc;
	struct cpuinfo_arc_mmu mmu;
	struct cpuinfo_arc_bpu bpu;
	struct bcr_identity core;
	struct bcr_isa_arcv2 isa;
	const char *details, *name;
	unsigned int vec_base;
	struct cpuinfo_arc_ccm iccm, dccm;
	struct {
		unsigned int swap:1, norm:1, minmax:1, barrel:1, crc:1, swape:1, pad1:2,
			     fpu_sp:1, fpu_dp:1, dual:1, dual_enb:1, pad2:4,
			     debug:1, ap:1, smart:1, rtt:1, pad3:4,
			     timer0:1, timer1:1, rtc:1, gfrc:1, pad4:4;
	} extn;
	struct bcr_mpy extn_mpy;
	struct bcr_extn_xymem extn_xymem;
};

extern struct cpuinfo_arc cpuinfo_arc700[];

static inline int is_isa_arcv2(void)
{
	return IS_ENABLED(CONFIG_ISA_ARCV2);
}

static inline int is_isa_arcompact(void)
{
	return IS_ENABLED(CONFIG_ISA_ARCOMPACT);
}

#endif /* __ASEMBLY__ */

#endif /* _ASM_ARC_ARCREGS_H */
