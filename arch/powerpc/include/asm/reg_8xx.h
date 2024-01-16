/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Contains register definitions common to PowerPC 8xx CPUs.  Notice
 */
#ifndef _ASM_POWERPC_REG_8xx_H
#define _ASM_POWERPC_REG_8xx_H

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
#define   LCTRL1_CTE_GT		0xc0000000
#define   LCTRL1_CTF_LT		0x14000000
#define   LCTRL1_CRWE_RW	0x00000000
#define   LCTRL1_CRWE_RO	0x00040000
#define   LCTRL1_CRWE_WO	0x000c0000
#define   LCTRL1_CRWF_RW	0x00000000
#define   LCTRL1_CRWF_RO	0x00010000
#define   LCTRL1_CRWF_WO	0x00030000
#define SPRN_LCTRL2	157
#define   LCTRL2_LW0EN		0x80000000
#define   LCTRL2_LW0LA_E	0x00000000
#define   LCTRL2_LW0LA_F	0x04000000
#define   LCTRL2_LW0LA_EandF	0x08000000
#define   LCTRL2_LW0LADC	0x02000000
#define   LCTRL2_SLW0EN		0x00000002
#ifdef CONFIG_PPC_8xx
#define SPRN_ICTRL	158
#endif
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

#endif /* _ASM_POWERPC_REG_8xx_H */
