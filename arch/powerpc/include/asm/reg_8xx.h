/*
 * Contains register definitions common to PowerPC 8xx CPUs.  Notice
 */
#ifndef _ASM_POWERPC_REG_8xx_H
#define _ASM_POWERPC_REG_8xx_H

#include <asm/mmu.h>

/* Cache control on the MPC8xx is provided through some additional
 * special purpose registers.
 */
#define SPRN_IC_CST	560	/* Instruction cache control/status */
#define SPRN_IC_ADR	561	/* Address needed for some commands */
#define SPRN_IC_DAT	562	/* Read-only data register */
#define SPRN_DC_CST	568	/* Data cache control/status */
#define SPRN_DC_ADR	569	/* Address needed for some commands */
#define SPRN_DC_DAT	570	/* Read-only data register */

/* Misc Debug */
#define SPRN_DPDR	630
#define SPRN_MI_CAM	816
#define SPRN_MI_RAM0	817
#define SPRN_MI_RAM1	818
#define SPRN_MD_CAM	824
#define SPRN_MD_RAM0	825
#define SPRN_MD_RAM1	826

/* Special MSR manipulation registers */
#define SPRN_EIE	80	/* External interrupt enable (EE=1, RI=1) */
#define SPRN_EID	81	/* External interrupt disable (EE=0, RI=1) */
#define SPRN_NRI	82	/* Non recoverable interrupt (EE=0, RI=0) */

/* Debug registers */
#define SPRN_CMPA	144
#define SPRN_COUNTA	150
#define SPRN_CMPE	152
#define SPRN_CMPF	153
#define SPRN_LCTRL1	156
#define SPRN_LCTRL2	157
#define SPRN_ICTRL	158
#define SPRN_BAR	159

/* Commands.  Only the first few are available to the instruction cache.
*/
#define	IDC_ENABLE	0x02000000	/* Cache enable */
#define IDC_DISABLE	0x04000000	/* Cache disable */
#define IDC_LDLCK	0x06000000	/* Load and lock */
#define IDC_UNLINE	0x08000000	/* Unlock line */
#define IDC_UNALL	0x0a000000	/* Unlock all */
#define IDC_INVALL	0x0c000000	/* Invalidate all */

#define DC_FLINE	0x0e000000	/* Flush data cache line */
#define DC_SFWT		0x01000000	/* Set forced writethrough mode */
#define DC_CFWT		0x03000000	/* Clear forced writethrough mode */
#define DC_SLES		0x05000000	/* Set little endian swap mode */
#define DC_CLES		0x07000000	/* Clear little endian swap mode */

/* Status.
*/
#define IDC_ENABLED	0x80000000	/* Cache is enabled */
#define IDC_CERR1	0x00200000	/* Cache error 1 */
#define IDC_CERR2	0x00100000	/* Cache error 2 */
#define IDC_CERR3	0x00080000	/* Cache error 3 */

#define DC_DFWT		0x40000000	/* Data cache is forced write through */
#define DC_LES		0x20000000	/* Caches are little endian mode */

#ifdef CONFIG_8xx_CPU6
#define do_mtspr_cpu6(rn, rn_addr, v)	\
	do {								\
		int _reg_cpu6 = rn_addr, _tmp_cpu6;		\
		asm volatile("stw %0, %1;"				\
			     "lwz %0, %1;"				\
			     "mtspr " __stringify(rn) ",%2" :		\
			     : "r" (_reg_cpu6), "m"(_tmp_cpu6),		\
			       "r" ((unsigned long)(v))			\
			     : "memory");				\
	} while (0)

#define do_mtspr(rn, v)	asm volatile("mtspr " __stringify(rn) ",%0" :	\
				     : "r" ((unsigned long)(v))		\
				     : "memory")
#define mtspr(rn, v) \
	do {								\
		if (rn == SPRN_IMMR)					\
			do_mtspr_cpu6(rn, 0x3d30, v);			\
		else if (rn == SPRN_IC_CST)				\
			do_mtspr_cpu6(rn, 0x2110, v);			\
		else if (rn == SPRN_IC_ADR)				\
			do_mtspr_cpu6(rn, 0x2310, v);			\
		else if (rn == SPRN_IC_DAT)				\
			do_mtspr_cpu6(rn, 0x2510, v);			\
		else if (rn == SPRN_DC_CST)				\
			do_mtspr_cpu6(rn, 0x3110, v);			\
		else if (rn == SPRN_DC_ADR)				\
			do_mtspr_cpu6(rn, 0x3310, v);			\
		else if (rn == SPRN_DC_DAT)				\
			do_mtspr_cpu6(rn, 0x3510, v);			\
		else if (rn == SPRN_MI_CTR)				\
			do_mtspr_cpu6(rn, 0x2180, v);			\
		else if (rn == SPRN_MI_AP)				\
			do_mtspr_cpu6(rn, 0x2580, v);			\
		else if (rn == SPRN_MI_EPN)				\
			do_mtspr_cpu6(rn, 0x2780, v);			\
		else if (rn == SPRN_MI_TWC)				\
			do_mtspr_cpu6(rn, 0x2b80, v);			\
		else if (rn == SPRN_MI_RPN)				\
			do_mtspr_cpu6(rn, 0x2d80, v);			\
		else if (rn == SPRN_MI_CAM)				\
			do_mtspr_cpu6(rn, 0x2190, v);			\
		else if (rn == SPRN_MI_RAM0)				\
			do_mtspr_cpu6(rn, 0x2390, v);			\
		else if (rn == SPRN_MI_RAM1)				\
			do_mtspr_cpu6(rn, 0x2590, v);			\
		else if (rn == SPRN_MD_CTR)				\
			do_mtspr_cpu6(rn, 0x3180, v);			\
		else if (rn == SPRN_M_CASID)				\
			do_mtspr_cpu6(rn, 0x3380, v);			\
		else if (rn == SPRN_MD_AP)				\
			do_mtspr_cpu6(rn, 0x3580, v);			\
		else if (rn == SPRN_MD_EPN)				\
			do_mtspr_cpu6(rn, 0x3780, v);			\
		else if (rn == SPRN_M_TWB)				\
			do_mtspr_cpu6(rn, 0x3980, v);			\
		else if (rn == SPRN_MD_TWC)				\
			do_mtspr_cpu6(rn, 0x3b80, v);			\
		else if (rn == SPRN_MD_RPN)				\
			do_mtspr_cpu6(rn, 0x3d80, v);			\
		else if (rn == SPRN_M_TW)				\
			do_mtspr_cpu6(rn, 0x3f80, v);			\
		else if (rn == SPRN_MD_CAM)				\
			do_mtspr_cpu6(rn, 0x3190, v);			\
		else if (rn == SPRN_MD_RAM0)				\
			do_mtspr_cpu6(rn, 0x3390, v);			\
		else if (rn == SPRN_MD_RAM1)				\
			do_mtspr_cpu6(rn, 0x3590, v);			\
		else if (rn == SPRN_DEC)				\
			do_mtspr_cpu6(rn, 0x2c00, v);			\
		else if (rn == SPRN_TBWL)				\
			do_mtspr_cpu6(rn, 0x3880, v);			\
		else if (rn == SPRN_TBWU)				\
			do_mtspr_cpu6(rn, 0x3a80, v);			\
		else if (rn == SPRN_DPDR)				\
			do_mtspr_cpu6(rn, 0x2d30, v);			\
		else							\
			do_mtspr(rn, v);				\
	} while (0)
#endif

#endif /* _ASM_POWERPC_REG_8xx_H */
