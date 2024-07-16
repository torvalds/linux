/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *   Interrupt Control Unit
 */

#ifndef __ASM_MACH_ICU_H
#define __ASM_MACH_ICU_H

#include "addr-map.h"

#define ICU_VIRT_BASE	(AXI_VIRT_BASE + 0x82000)
#define ICU_REG(x)	(ICU_VIRT_BASE + (x))

#define ICU2_VIRT_BASE	(AXI_VIRT_BASE + 0x84000)
#define ICU2_REG(x)	(ICU2_VIRT_BASE + (x))

#define ICU_INT_CONF(n)		ICU_REG((n) << 2)
#define ICU_INT_CONF_MASK	(0xf)

/************ PXA168/PXA910 (MMP) *********************/
#define ICU_INT_CONF_AP_INT	(1 << 6)
#define ICU_INT_CONF_CP_INT	(1 << 5)
#define ICU_INT_CONF_IRQ	(1 << 4)

#define ICU_AP_FIQ_SEL_INT_NUM	ICU_REG(0x108)	/* AP FIQ Selected Interrupt */
#define ICU_AP_IRQ_SEL_INT_NUM	ICU_REG(0x10C)	/* AP IRQ Selected Interrupt */
#define ICU_AP_GBL_IRQ_MSK	ICU_REG(0x114)	/* AP Global Interrupt Mask */
#define ICU_INT_STATUS_0	ICU_REG(0x128)	/* Interrupt Stuats 0 */
#define ICU_INT_STATUS_1	ICU_REG(0x12C)	/* Interrupt Status 1 */

/************************** MMP2 ***********************/

/*
 * IRQ0/FIQ0 is routed to SP IRQ/FIQ.
 * IRQ1 is routed to PJ4 IRQ, and IRQ2 is routes to PJ4 FIQ.
 */
#define ICU_INT_ROUTE_SP_IRQ		(1 << 4)
#define ICU_INT_ROUTE_PJ4_IRQ		(1 << 5)
#define ICU_INT_ROUTE_PJ4_FIQ		(1 << 6)

#define MMP2_ICU_PJ4_IRQ_STATUS0	ICU_REG(0x138)
#define MMP2_ICU_PJ4_IRQ_STATUS1	ICU_REG(0x13c)
#define MMP2_ICU_PJ4_FIQ_STATUS0	ICU_REG(0x140)
#define MMP2_ICU_PJ4_FIQ_STATUS1	ICU_REG(0x144)

#define MMP2_ICU_INT4_STATUS		ICU_REG(0x150)
#define MMP2_ICU_INT5_STATUS		ICU_REG(0x154)
#define MMP2_ICU_INT17_STATUS		ICU_REG(0x158)
#define MMP2_ICU_INT35_STATUS		ICU_REG(0x15c)
#define MMP2_ICU_INT51_STATUS		ICU_REG(0x160)

#define MMP2_ICU_INT4_MASK		ICU_REG(0x168)
#define MMP2_ICU_INT5_MASK		ICU_REG(0x16C)
#define MMP2_ICU_INT17_MASK		ICU_REG(0x170)
#define MMP2_ICU_INT35_MASK		ICU_REG(0x174)
#define MMP2_ICU_INT51_MASK		ICU_REG(0x178)

#define MMP2_ICU_SP_IRQ_SEL		ICU_REG(0x100)
#define MMP2_ICU_PJ4_IRQ_SEL		ICU_REG(0x104)
#define MMP2_ICU_PJ4_FIQ_SEL		ICU_REG(0x108)

#define MMP2_ICU_INVERT			ICU_REG(0x164)

#define MMP2_ICU_INV_PMIC		(1 << 0)
#define MMP2_ICU_INV_PERF		(1 << 1)
#define MMP2_ICU_INV_COMMTX		(1 << 2)
#define MMP2_ICU_INV_COMMRX		(1 << 3)

#endif /* __ASM_MACH_ICU_H */
