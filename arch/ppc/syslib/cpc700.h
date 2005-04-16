/*
 * arch/ppc/syslib/cpc700.h
 *
 * Header file for IBM CPC700 Host Bridge, et. al.
 *
 * Author: Mark A. Greer
 *         mgreer@mvista.com
 *
 * 2000-2002 (c) MontaVista, Software, Inc.  This file is licensed under
 * the terms of the GNU General Public License version 2.  This program
 * is licensed "as is" without any warranty of any kind, whether express
 * or implied.
 */

/*
 * This file contains the defines and macros for the IBM CPC700 host bridge,
 * memory controller, PIC, UARTs, IIC, and Timers.
 */

#ifndef	__PPC_SYSLIB_CPC700_H__
#define	__PPC_SYSLIB_CPC700_H__

#include <linux/stddef.h>
#include <linux/types.h>
#include <linux/init.h>

/* XXX no barriers? not even any volatiles?  -- paulus */
#define CPC700_OUT_32(a,d)  (*(u_int *)a = d)
#define CPC700_IN_32(a)     (*(u_int *)a)

/*
 * PCI Section
 */
#define CPC700_PCI_CONFIG_ADDR          0xfec00000
#define CPC700_PCI_CONFIG_DATA          0xfec00004

/* CPU -> PCI memory window 0 */
#define CPC700_PMM0_LOCAL		0xff400000	/* CPU physical addr */
#define CPC700_PMM0_MASK_ATTR		0xff400004	/* size and attrs */
#define CPC700_PMM0_PCI_LOW		0xff400008	/* PCI addr, low word */
#define CPC700_PMM0_PCI_HIGH		0xff40000c	/* PCI addr, high wd */
/* CPU -> PCI memory window 1 */
#define CPC700_PMM1_LOCAL		0xff400010
#define CPC700_PMM1_MASK_ATTR		0xff400014
#define CPC700_PMM1_PCI_LOW		0xff400018
#define CPC700_PMM1_PCI_HIGH		0xff40001c
/* CPU -> PCI memory window 2 */
#define CPC700_PMM2_LOCAL		0xff400020
#define CPC700_PMM2_MASK_ATTR		0xff400024
#define CPC700_PMM2_PCI_LOW		0xff400028
#define CPC700_PMM2_PCI_HIGH		0xff40002c
/* PCI memory -> CPU window 1 */
#define CPC700_PTM1_MEMSIZE		0xff400030	/* window size */
#define CPC700_PTM1_LOCAL		0xff400034	/* CPU phys addr */
/* PCI memory -> CPU window 2 */
#define CPC700_PTM2_MEMSIZE		0xff400038	/* size and enable */
#define CPC700_PTM2_LOCAL		0xff40003c

/*
 * PIC Section
 *
 * IBM calls the CPC700's programmable interrupt controller the Universal
 * Interrupt Controller or UIC.
 */

/*
 * UIC Register Addresses.
 */
#define	CPC700_UIC_UICSR		0xff500880	/* Status Reg (Rd/Clr)*/
#define	CPC700_UIC_UICSRS		0xff500884	/* Status Reg (Set) */
#define	CPC700_UIC_UICER		0xff500888	/* Enable Reg */
#define	CPC700_UIC_UICCR		0xff50088c	/* Critical Reg */
#define	CPC700_UIC_UICPR		0xff500890	/* Polarity Reg */
#define	CPC700_UIC_UICTR		0xff500894	/* Trigger Reg */
#define	CPC700_UIC_UICMSR		0xff500898	/* Masked Status Reg */
#define	CPC700_UIC_UICVR		0xff50089c	/* Vector Reg */
#define	CPC700_UIC_UICVCR		0xff5008a0	/* Vector Config Reg */

#define	CPC700_UIC_UICER_ENABLE		0x00000001	/* Enable an IRQ */

#define	CPC700_UIC_UICVCR_31_HI		0x00000000	/* IRQ 31 hi priority */
#define	CPC700_UIC_UICVCR_0_HI		0x00000001	/* IRQ 0 hi priority */
#define CPC700_UIC_UICVCR_BASE_MASK	0xfffffffc
#define CPC700_UIC_UICVCR_ORDER_MASK	0x00000001

/* Specify value of a bit for an IRQ. */
#define	CPC700_UIC_IRQ_BIT(i)		((0x00000001) << (31 - (i)))

/*
 * UIC Exports...
 */
extern struct hw_interrupt_type cpc700_pic;
extern unsigned int cpc700_irq_assigns[32][2];

extern void __init cpc700_init_IRQ(void);
extern int cpc700_get_irq(struct pt_regs *);

#endif	/* __PPC_SYSLIB_CPC700_H__ */
