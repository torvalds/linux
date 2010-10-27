/*
 * Support for the MicroBlaze PVR (Processor Version Register)
 *
 * Copyright (C) 2009 Michal Simek <monstr@monstr.eu>
 * Copyright (C) 2007 John Williams <john.williams@petalogix.com>
 * Copyright (C) 2007 - 2009 PetaLogix
 *
 * This file is subject to the terms and conditions of the GNU General
 * Public License. See the file COPYING in the main directory of this
 * archive for more details.
 */

#ifndef _ASM_MICROBLAZE_PVR_H
#define _ASM_MICROBLAZE_PVR_H

#define PVR_MSR_BIT 0x400

struct pvr_s {
	unsigned pvr[12];
};

/* The following taken from Xilinx's standalone BSP pvr.h */

/* Basic PVR mask */
#define PVR0_PVR_FULL_MASK		0x80000000
#define PVR0_USE_BARREL_MASK		0x40000000
#define PVR0_USE_DIV_MASK		0x20000000
#define PVR0_USE_HW_MUL_MASK		0x10000000
#define PVR0_USE_FPU_MASK		0x08000000
#define PVR0_USE_EXC_MASK		0x04000000
#define PVR0_USE_ICACHE_MASK		0x02000000
#define PVR0_USE_DCACHE_MASK		0x01000000
#define PVR0_USE_MMU			0x00800000	/* new */
#define PVR0_VERSION_MASK		0x0000FF00
#define PVR0_USER1_MASK			0x000000FF

/* User 2 PVR mask */
#define PVR1_USER2_MASK			0xFFFFFFFF

/* Configuration PVR masks */
#define PVR2_D_OPB_MASK			0x80000000
#define PVR2_D_LMB_MASK			0x40000000
#define PVR2_I_OPB_MASK			0x20000000
#define PVR2_I_LMB_MASK			0x10000000
#define PVR2_INTERRUPT_IS_EDGE_MASK	0x08000000
#define PVR2_EDGE_IS_POSITIVE_MASK	0x04000000
#define PVR2_D_PLB_MASK			0x02000000	/* new */
#define PVR2_I_PLB_MASK			0x01000000	/* new */
#define PVR2_INTERCONNECT		0x00800000	/* new */
#define PVR2_USE_EXTEND_FSL		0x00080000	/* new */
#define PVR2_USE_FSL_EXC		0x00040000	/* new */
#define PVR2_USE_MSR_INSTR		0x00020000
#define PVR2_USE_PCMP_INSTR		0x00010000
#define PVR2_AREA_OPTIMISED		0x00008000
#define PVR2_USE_BARREL_MASK		0x00004000
#define PVR2_USE_DIV_MASK		0x00002000
#define PVR2_USE_HW_MUL_MASK		0x00001000
#define PVR2_USE_FPU_MASK		0x00000800
#define PVR2_USE_MUL64_MASK		0x00000400
#define PVR2_USE_FPU2_MASK		0x00000200	/* new */
#define PVR2_USE_IPLBEXC 		0x00000100
#define PVR2_USE_DPLBEXC		0x00000080
#define PVR2_OPCODE_0x0_ILL_MASK	0x00000040
#define PVR2_UNALIGNED_EXC_MASK		0x00000020
#define PVR2_ILL_OPCODE_EXC_MASK	0x00000010
#define PVR2_IOPB_BUS_EXC_MASK		0x00000008
#define PVR2_DOPB_BUS_EXC_MASK		0x00000004
#define PVR2_DIV_ZERO_EXC_MASK		0x00000002
#define PVR2_FPU_EXC_MASK		0x00000001

/* Debug and exception PVR masks */
#define PVR3_DEBUG_ENABLED_MASK		0x80000000
#define PVR3_NUMBER_OF_PC_BRK_MASK	0x1E000000
#define PVR3_NUMBER_OF_RD_ADDR_BRK_MASK	0x00380000
#define PVR3_NUMBER_OF_WR_ADDR_BRK_MASK	0x0000E000
#define PVR3_FSL_LINKS_MASK		0x00000380

/* ICache config PVR masks */
#define PVR4_USE_ICACHE_MASK		0x80000000 /* ICU */
#define PVR4_ICACHE_ADDR_TAG_BITS_MASK	0x7C000000 /* ICTS */
#define PVR4_ICACHE_ALLOW_WR_MASK	0x01000000 /* ICW */
#define PVR4_ICACHE_LINE_LEN_MASK	0x00E00000 /* ICLL */
#define PVR4_ICACHE_BYTE_SIZE_MASK	0x001F0000 /* ICBS */
#define PVR4_ICACHE_ALWAYS_USED		0x00008000 /* IAU */
#define PVR4_ICACHE_INTERFACE		0x00002000 /* ICI */

/* DCache config PVR masks */
#define PVR5_USE_DCACHE_MASK		0x80000000 /* DCU */
#define PVR5_DCACHE_ADDR_TAG_BITS_MASK	0x7C000000 /* DCTS */
#define PVR5_DCACHE_ALLOW_WR_MASK	0x01000000 /* DCW */
#define PVR5_DCACHE_LINE_LEN_MASK	0x00E00000 /* DCLL */
#define PVR5_DCACHE_BYTE_SIZE_MASK	0x001F0000 /* DCBS */
#define PVR5_DCACHE_ALWAYS_USED		0x00008000 /* DAU */
#define PVR5_DCACHE_USE_WRITEBACK	0x00004000 /* DWB */
#define PVR5_DCACHE_INTERFACE		0x00002000 /* DCI */

/* ICache base address PVR mask */
#define PVR6_ICACHE_BASEADDR_MASK	0xFFFFFFFF

/* ICache high address PVR mask */
#define PVR7_ICACHE_HIGHADDR_MASK	0xFFFFFFFF

/* DCache base address PVR mask */
#define PVR8_DCACHE_BASEADDR_MASK	0xFFFFFFFF

/* DCache high address PVR mask */
#define PVR9_DCACHE_HIGHADDR_MASK	0xFFFFFFFF

/* Target family PVR mask */
#define PVR10_TARGET_FAMILY_MASK	0xFF000000

/* MMU descrtiption */
#define PVR11_USE_MMU			0xC0000000
#define PVR11_MMU_ITLB_SIZE		0x38000000
#define PVR11_MMU_DTLB_SIZE		0x07000000
#define PVR11_MMU_TLB_ACCESS		0x00C00000
#define PVR11_MMU_ZONES			0x003C0000
/* MSR Reset value PVR mask */
#define PVR11_MSR_RESET_VALUE_MASK	0x000007FF


/* PVR access macros */
#define PVR_IS_FULL(pvr)		(pvr.pvr[0] & PVR0_PVR_FULL_MASK)
#define PVR_USE_BARREL(pvr)		(pvr.pvr[0] & PVR0_USE_BARREL_MASK)
#define PVR_USE_DIV(pvr)		(pvr.pvr[0] & PVR0_USE_DIV_MASK)
#define PVR_USE_HW_MUL(pvr)		(pvr.pvr[0] & PVR0_USE_HW_MUL_MASK)
#define PVR_USE_FPU(pvr)		(pvr.pvr[0] & PVR0_USE_FPU_MASK)
#define PVR_USE_FPU2(pvr)		(pvr.pvr[2] & PVR2_USE_FPU2_MASK)
#define PVR_USE_ICACHE(pvr)		(pvr.pvr[0] & PVR0_USE_ICACHE_MASK)
#define PVR_USE_DCACHE(pvr)		(pvr.pvr[0] & PVR0_USE_DCACHE_MASK)
#define PVR_VERSION(pvr)	((pvr.pvr[0] & PVR0_VERSION_MASK) >> 8)
#define PVR_USER1(pvr)			(pvr.pvr[0] & PVR0_USER1_MASK)
#define PVR_USER2(pvr)			(pvr.pvr[1] & PVR1_USER2_MASK)

#define PVR_D_OPB(pvr)			(pvr.pvr[2] & PVR2_D_OPB_MASK)
#define PVR_D_LMB(pvr)			(pvr.pvr[2] & PVR2_D_LMB_MASK)
#define PVR_I_OPB(pvr)			(pvr.pvr[2] & PVR2_I_OPB_MASK)
#define PVR_I_LMB(pvr)			(pvr.pvr[2] & PVR2_I_LMB_MASK)
#define PVR_INTERRUPT_IS_EDGE(pvr) \
			(pvr.pvr[2] & PVR2_INTERRUPT_IS_EDGE_MASK)
#define PVR_EDGE_IS_POSITIVE(pvr) \
			(pvr.pvr[2] & PVR2_EDGE_IS_POSITIVE_MASK)
#define PVR_USE_MSR_INSTR(pvr)		(pvr.pvr[2] & PVR2_USE_MSR_INSTR)
#define PVR_USE_PCMP_INSTR(pvr)		(pvr.pvr[2] & PVR2_USE_PCMP_INSTR)
#define PVR_AREA_OPTIMISED(pvr)		(pvr.pvr[2] & PVR2_AREA_OPTIMISED)
#define PVR_USE_MUL64(pvr)		(pvr.pvr[2] & PVR2_USE_MUL64_MASK)
#define PVR_OPCODE_0x0_ILLEGAL(pvr) \
			(pvr.pvr[2] & PVR2_OPCODE_0x0_ILL_MASK)
#define PVR_UNALIGNED_EXCEPTION(pvr) \
			(pvr.pvr[2] & PVR2_UNALIGNED_EXC_MASK)
#define PVR_ILL_OPCODE_EXCEPTION(pvr) \
			(pvr.pvr[2] & PVR2_ILL_OPCODE_EXC_MASK)
#define PVR_IOPB_BUS_EXCEPTION(pvr) \
			(pvr.pvr[2] & PVR2_IOPB_BUS_EXC_MASK)
#define PVR_DOPB_BUS_EXCEPTION(pvr) \
			(pvr.pvr[2] & PVR2_DOPB_BUS_EXC_MASK)
#define PVR_DIV_ZERO_EXCEPTION(pvr) \
			(pvr.pvr[2] & PVR2_DIV_ZERO_EXC_MASK)
#define PVR_FPU_EXCEPTION(pvr)		(pvr.pvr[2] & PVR2_FPU_EXC_MASK)
#define PVR_FSL_EXCEPTION(pvr)		(pvr.pvr[2] & PVR2_USE_EXTEND_FSL)

#define PVR_DEBUG_ENABLED(pvr)		(pvr.pvr[3] & PVR3_DEBUG_ENABLED_MASK)
#define PVR_NUMBER_OF_PC_BRK(pvr) \
			((pvr.pvr[3] & PVR3_NUMBER_OF_PC_BRK_MASK) >> 25)
#define PVR_NUMBER_OF_RD_ADDR_BRK(pvr) \
			((pvr.pvr[3] & PVR3_NUMBER_OF_RD_ADDR_BRK_MASK) >> 19)
#define PVR_NUMBER_OF_WR_ADDR_BRK(pvr) \
			((pvr.pvr[3] & PVR3_NUMBER_OF_WR_ADDR_BRK_MASK) >> 13)
#define PVR_FSL_LINKS(pvr)	((pvr.pvr[3] & PVR3_FSL_LINKS_MASK) >> 7)

#define PVR_ICACHE_ADDR_TAG_BITS(pvr) \
			((pvr.pvr[4] & PVR4_ICACHE_ADDR_TAG_BITS_MASK) >> 26)
#define PVR_ICACHE_USE_FSL(pvr)		(pvr.pvr[4] & PVR4_ICACHE_USE_FSL_MASK)
#define PVR_ICACHE_ALLOW_WR(pvr)	(pvr.pvr[4] & PVR4_ICACHE_ALLOW_WR_MASK)
#define PVR_ICACHE_LINE_LEN(pvr) \
			(1 << ((pvr.pvr[4] & PVR4_ICACHE_LINE_LEN_MASK) >> 21))
#define PVR_ICACHE_BYTE_SIZE(pvr) \
			(1 << ((pvr.pvr[4] & PVR4_ICACHE_BYTE_SIZE_MASK) >> 16))

#define PVR_DCACHE_ADDR_TAG_BITS(pvr) \
			((pvr.pvr[5] & PVR5_DCACHE_ADDR_TAG_BITS_MASK) >> 26)
#define PVR_DCACHE_USE_FSL(pvr)		(pvr.pvr[5] & PVR5_DCACHE_USE_FSL_MASK)
#define PVR_DCACHE_ALLOW_WR(pvr)	(pvr.pvr[5] & PVR5_DCACHE_ALLOW_WR_MASK)
/* FIXME two shifts on one line needs any comment */
#define PVR_DCACHE_LINE_LEN(pvr) \
			(1 << ((pvr.pvr[5] & PVR5_DCACHE_LINE_LEN_MASK) >> 21))
#define PVR_DCACHE_BYTE_SIZE(pvr) \
			(1 << ((pvr.pvr[5] & PVR5_DCACHE_BYTE_SIZE_MASK) >> 16))

#define PVR_DCACHE_USE_WRITEBACK(pvr) \
			((pvr.pvr[5] & PVR5_DCACHE_USE_WRITEBACK) >> 14)

#define PVR_ICACHE_BASEADDR(pvr)	(pvr.pvr[6] & PVR6_ICACHE_BASEADDR_MASK)
#define PVR_ICACHE_HIGHADDR(pvr)	(pvr.pvr[7] & PVR7_ICACHE_HIGHADDR_MASK)

#define PVR_DCACHE_BASEADDR(pvr)	(pvr.pvr[8] & PVR8_DCACHE_BASEADDR_MASK)
#define PVR_DCACHE_HIGHADDR(pvr)	(pvr.pvr[9] & PVR9_DCACHE_HIGHADDR_MASK)

#define PVR_TARGET_FAMILY(pvr)	((pvr.pvr[10] & PVR10_TARGET_FAMILY_MASK) >> 24)

#define PVR_MSR_RESET_VALUE(pvr) \
				(pvr.pvr[11] & PVR11_MSR_RESET_VALUE_MASK)

/* mmu */
#define PVR_USE_MMU(pvr)	((pvr.pvr[11] & PVR11_USE_MMU) >> 30)
#define PVR_MMU_ITLB_SIZE(pvr)	(pvr.pvr[11] & PVR11_MMU_ITLB_SIZE)
#define PVR_MMU_DTLB_SIZE(pvr)	(pvr.pvr[11] & PVR11_MMU_DTLB_SIZE)
#define PVR_MMU_TLB_ACCESS(pvr)	(pvr.pvr[11] & PVR11_MMU_TLB_ACCESS)
#define PVR_MMU_ZONES(pvr)	(pvr.pvr[11] & PVR11_MMU_ZONES)


int cpu_has_pvr(void);
void get_pvr(struct pvr_s *pvr);

#endif /* _ASM_MICROBLAZE_PVR_H */
