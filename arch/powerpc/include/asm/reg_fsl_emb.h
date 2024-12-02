/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Contains register definitions for the Freescale Embedded Performance
 * Monitor.
 */
#ifdef __KERNEL__
#ifndef __ASM_POWERPC_REG_FSL_EMB_H__
#define __ASM_POWERPC_REG_FSL_EMB_H__

#include <linux/stringify.h>

#ifndef __ASSEMBLY__
/* Performance Monitor Registers */
#define mfpmr(rn)	({unsigned int rval; \
			asm volatile(".machine push; " \
				     ".machine e300; " \
				     "mfpmr %0," __stringify(rn) ";" \
				     ".machine pop; " \
				     : "=r" (rval)); rval;})
#define mtpmr(rn, v)	asm volatile(".machine push; " \
				     ".machine e300; " \
				     "mtpmr " __stringify(rn) ",%0; " \
				     ".machine pop; " \
				     : : "r" (v))
#endif /* __ASSEMBLY__ */

/* Freescale Book E Performance Monitor APU Registers */
#define PMRN_PMC0	0x010	/* Performance Monitor Counter 0 */
#define PMRN_PMC1	0x011	/* Performance Monitor Counter 1 */
#define PMRN_PMC2	0x012	/* Performance Monitor Counter 2 */
#define PMRN_PMC3	0x013	/* Performance Monitor Counter 3 */
#define PMRN_PMC4	0x014	/* Performance Monitor Counter 4 */
#define PMRN_PMC5	0x015	/* Performance Monitor Counter 5 */
#define PMRN_PMLCA0	0x090	/* PM Local Control A0 */
#define PMRN_PMLCA1	0x091	/* PM Local Control A1 */
#define PMRN_PMLCA2	0x092	/* PM Local Control A2 */
#define PMRN_PMLCA3	0x093	/* PM Local Control A3 */
#define PMRN_PMLCA4	0x094	/* PM Local Control A4 */
#define PMRN_PMLCA5	0x095	/* PM Local Control A5 */

#define PMLCA_FC	0x80000000	/* Freeze Counter */
#define PMLCA_FCS	0x40000000	/* Freeze in Supervisor */
#define PMLCA_FCU	0x20000000	/* Freeze in User */
#define PMLCA_FCM1	0x10000000	/* Freeze when PMM==1 */
#define PMLCA_FCM0	0x08000000	/* Freeze when PMM==0 */
#define PMLCA_CE	0x04000000	/* Condition Enable */
#define PMLCA_FGCS1	0x00000002	/* Freeze in guest state */
#define PMLCA_FGCS0	0x00000001	/* Freeze in hypervisor state */

#define PMLCA_EVENT_MASK 0x01ff0000	/* Event field */
#define PMLCA_EVENT_SHIFT	16

#define PMRN_PMLCB0	0x110	/* PM Local Control B0 */
#define PMRN_PMLCB1	0x111	/* PM Local Control B1 */
#define PMRN_PMLCB2	0x112	/* PM Local Control B2 */
#define PMRN_PMLCB3	0x113	/* PM Local Control B3 */
#define PMRN_PMLCB4	0x114	/* PM Local Control B4 */
#define PMRN_PMLCB5	0x115	/* PM Local Control B5 */

#define PMLCB_THRESHMUL_MASK	0x0700	/* Threshold Multiple Field */
#define PMLCB_THRESHMUL_SHIFT	8

#define PMLCB_THRESHOLD_MASK	0x003f	/* Threshold Field */
#define PMLCB_THRESHOLD_SHIFT	0

#define PMRN_PMGC0	0x190	/* PM Global Control 0 */

#define PMGC0_FAC	0x80000000	/* Freeze all Counters */
#define PMGC0_PMIE	0x40000000	/* Interrupt Enable */
#define PMGC0_FCECE	0x20000000	/* Freeze countes on
					   Enabled Condition or
					   Event */

#define PMRN_UPMC0	0x000	/* User Performance Monitor Counter 0 */
#define PMRN_UPMC1	0x001	/* User Performance Monitor Counter 1 */
#define PMRN_UPMC2	0x002	/* User Performance Monitor Counter 2 */
#define PMRN_UPMC3	0x003	/* User Performance Monitor Counter 3 */
#define PMRN_UPMC4	0x004	/* User Performance Monitor Counter 4 */
#define PMRN_UPMC5	0x005	/* User Performance Monitor Counter 5 */
#define PMRN_UPMLCA0	0x080	/* User PM Local Control A0 */
#define PMRN_UPMLCA1	0x081	/* User PM Local Control A1 */
#define PMRN_UPMLCA2	0x082	/* User PM Local Control A2 */
#define PMRN_UPMLCA3	0x083	/* User PM Local Control A3 */
#define PMRN_UPMLCA4	0x084	/* User PM Local Control A4 */
#define PMRN_UPMLCA5	0x085	/* User PM Local Control A5 */
#define PMRN_UPMLCB0	0x100	/* User PM Local Control B0 */
#define PMRN_UPMLCB1	0x101	/* User PM Local Control B1 */
#define PMRN_UPMLCB2	0x102	/* User PM Local Control B2 */
#define PMRN_UPMLCB3	0x103	/* User PM Local Control B3 */
#define PMRN_UPMLCB4	0x104	/* User PM Local Control B4 */
#define PMRN_UPMLCB5	0x105	/* User PM Local Control B5 */
#define PMRN_UPMGC0	0x180	/* User PM Global Control 0 */


#endif /* __ASM_POWERPC_REG_FSL_EMB_H__ */
#endif /* __KERNEL__ */
