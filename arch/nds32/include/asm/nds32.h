/* SPDX-License-Identifier: GPL-2.0 */
// Copyright (C) 2005-2017 Andes Technology Corporation

#ifndef _ASM_NDS32_NDS32_H_
#define _ASM_NDS32_NDS32_H_

#include <asm/bitfield.h>
#include <asm/cachectl.h>

#ifndef __ASSEMBLY__
#include <linux/init.h>
#include <asm/barrier.h>
#include <nds32_intrinsic.h>

#ifdef CONFIG_CC_OPTIMIZE_FOR_SIZE
#define FP_OFFSET (-3)
#else
#define FP_OFFSET (-2)
#endif
#define LP_OFFSET (-1)

extern void __init early_trap_init(void);
static inline void GIE_ENABLE(void)
{
	mb();
	__nds32__gie_en();
}

static inline void GIE_DISABLE(void)
{
	mb();
	__nds32__gie_dis();
}

static inline unsigned long CACHE_SET(unsigned char cache)
{

	if (cache == ICACHE)
		return 64 << ((__nds32__mfsr(NDS32_SR_ICM_CFG) & ICM_CFG_mskISET) >>
			      ICM_CFG_offISET);
	else
		return 64 << ((__nds32__mfsr(NDS32_SR_DCM_CFG) & DCM_CFG_mskDSET) >>
			      DCM_CFG_offDSET);
}

static inline unsigned long CACHE_WAY(unsigned char cache)
{

	if (cache == ICACHE)
		return 1 +
		    ((__nds32__mfsr(NDS32_SR_ICM_CFG) & ICM_CFG_mskIWAY) >> ICM_CFG_offIWAY);
	else
		return 1 +
		    ((__nds32__mfsr(NDS32_SR_DCM_CFG) & DCM_CFG_mskDWAY) >> DCM_CFG_offDWAY);
}

static inline unsigned long CACHE_LINE_SIZE(unsigned char cache)
{

	if (cache == ICACHE)
		return 8 <<
		    (((__nds32__mfsr(NDS32_SR_ICM_CFG) & ICM_CFG_mskISZ) >> ICM_CFG_offISZ) - 1);
	else
		return 8 <<
		    (((__nds32__mfsr(NDS32_SR_DCM_CFG) & DCM_CFG_mskDSZ) >> DCM_CFG_offDSZ) - 1);
}

#endif /* __ASSEMBLY__ */

#define IVB_BASE		PHYS_OFFSET	/* in user space for intr/exc/trap/break table base, 64KB aligned
						 * We defined at the start of the physical memory */

/* dispatched sub-entry exception handler numbering */
#define RD_PROT			0	/* read protrection */
#define WRT_PROT		1	/* write protection */
#define NOEXEC			2	/* non executable */
#define PAGE_MODIFY		3	/* page modified */
#define ACC_BIT			4	/* access bit */
#define RESVED_PTE		5	/* reserved PTE attribute */
/* reserved 6 ~ 16 */

#endif /* _ASM_NDS32_NDS32_H_ */
