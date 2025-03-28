/* SPDX-License-Identifier: GPL-2.0 */
/*
 * arch/arm/include/asm/vfp.h
 *
 * VFP register definitions.
 * First, the standard VFP set.
 */

#ifndef __ASM_VFP_H
#define __ASM_VFP_H

/* FPSID bits */
#define FPSID_IMPLEMENTER_BIT	(24)
#define FPSID_IMPLEMENTER_MASK	(0xff << FPSID_IMPLEMENTER_BIT)
#define FPSID_SOFTWARE		(1<<23)
#define FPSID_FORMAT_BIT	(21)
#define FPSID_FORMAT_MASK	(0x3  << FPSID_FORMAT_BIT)
#define FPSID_NODOUBLE		(1<<20)
#define FPSID_ARCH_BIT		(16)
#define FPSID_ARCH_MASK		(0xF  << FPSID_ARCH_BIT)
#define FPSID_CPUID_ARCH_MASK	(0x7F  << FPSID_ARCH_BIT)
#define FPSID_PART_BIT		(8)
#define FPSID_PART_MASK		(0xFF << FPSID_PART_BIT)
#define FPSID_VARIANT_BIT	(4)
#define FPSID_VARIANT_MASK	(0xF  << FPSID_VARIANT_BIT)
#define FPSID_REV_BIT		(0)
#define FPSID_REV_MASK		(0xF  << FPSID_REV_BIT)

/* FPEXC bits */
#define FPEXC_EX		(1 << 31)
#define FPEXC_EN		(1 << 30)
#define FPEXC_DEX		(1 << 29)
#define FPEXC_FP2V		(1 << 28)
#define FPEXC_VV		(1 << 27)
#define FPEXC_TFV		(1 << 26)
#define FPEXC_LENGTH_BIT	(8)
#define FPEXC_LENGTH_MASK	(7 << FPEXC_LENGTH_BIT)
#define FPEXC_IDF		(1 << 7)
#define FPEXC_IXF		(1 << 4)
#define FPEXC_UFF		(1 << 3)
#define FPEXC_OFF		(1 << 2)
#define FPEXC_DZF		(1 << 1)
#define FPEXC_IOF		(1 << 0)
#define FPEXC_TRAP_MASK		(FPEXC_IDF|FPEXC_IXF|FPEXC_UFF|FPEXC_OFF|FPEXC_DZF|FPEXC_IOF)

/* FPSCR bits */
#define FPSCR_DEFAULT_NAN	(1<<25)
#define FPSCR_FLUSHTOZERO	(1<<24)
#define FPSCR_ROUND_NEAREST	(0<<22)
#define FPSCR_ROUND_PLUSINF	(1<<22)
#define FPSCR_ROUND_MINUSINF	(2<<22)
#define FPSCR_ROUND_TOZERO	(3<<22)
#define FPSCR_RMODE_BIT		(22)
#define FPSCR_RMODE_MASK	(3 << FPSCR_RMODE_BIT)
#define FPSCR_STRIDE_BIT	(20)
#define FPSCR_STRIDE_MASK	(3 << FPSCR_STRIDE_BIT)
#define FPSCR_LENGTH_BIT	(16)
#define FPSCR_LENGTH_MASK	(7 << FPSCR_LENGTH_BIT)
#define FPSCR_IOE		(1<<8)
#define FPSCR_DZE		(1<<9)
#define FPSCR_OFE		(1<<10)
#define FPSCR_UFE		(1<<11)
#define FPSCR_IXE		(1<<12)
#define FPSCR_IDE		(1<<15)
#define FPSCR_IOC		(1<<0)
#define FPSCR_DZC		(1<<1)
#define FPSCR_OFC		(1<<2)
#define FPSCR_UFC		(1<<3)
#define FPSCR_IXC		(1<<4)
#define FPSCR_IDC		(1<<7)

/* MVFR0 bits */
#define MVFR0_A_SIMD_BIT	(0)
#define MVFR0_A_SIMD_MASK	(0xf << MVFR0_A_SIMD_BIT)
#define MVFR0_SP_BIT		(4)
#define MVFR0_SP_MASK		(0xf << MVFR0_SP_BIT)
#define MVFR0_DP_BIT		(8)
#define MVFR0_DP_MASK		(0xf << MVFR0_DP_BIT)

/* MVFR1 bits */
#define MVFR1_ASIMDHP_BIT	(20)
#define MVFR1_ASIMDHP_MASK	(0xf << MVFR1_ASIMDHP_BIT)
#define MVFR1_FPHP_BIT		(24)
#define MVFR1_FPHP_MASK		(0xf << MVFR1_FPHP_BIT)

/* Bit patterns for decoding the packaged operation descriptors */
#define VFPOPDESC_LENGTH_BIT	(9)
#define VFPOPDESC_LENGTH_MASK	(0x07 << VFPOPDESC_LENGTH_BIT)
#define VFPOPDESC_UNUSED_BIT	(24)
#define VFPOPDESC_UNUSED_MASK	(0xFF << VFPOPDESC_UNUSED_BIT)
#define VFPOPDESC_OPDESC_MASK	(~(VFPOPDESC_LENGTH_MASK | VFPOPDESC_UNUSED_MASK))

#ifndef __ASSEMBLY__
void vfp_disable(void);
#endif

#endif /* __ASM_VFP_H */
