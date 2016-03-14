/*
 *   Timers Module
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#ifndef __ASM_MACH_REGS_TIMERS_H
#define __ASM_MACH_REGS_TIMERS_H

#include "addr-map.h"

#define TIMERS1_VIRT_BASE	(APB_VIRT_BASE + 0x14000)
#define TIMERS2_VIRT_BASE	(APB_VIRT_BASE + 0x16000)

#define TMR_CCR		(0x0000)
#define TMR_TN_MM(n, m)	(0x0004 + ((n) << 3) + (((n) + (m)) << 2))
#define TMR_CR(n)	(0x0028 + ((n) << 2))
#define TMR_SR(n)	(0x0034 + ((n) << 2))
#define TMR_IER(n)	(0x0040 + ((n) << 2))
#define TMR_PLVR(n)	(0x004c + ((n) << 2))
#define TMR_PLCR(n)	(0x0058 + ((n) << 2))
#define TMR_WMER	(0x0064)
#define TMR_WMR		(0x0068)
#define TMR_WVR		(0x006c)
#define TMR_WSR		(0x0070)
#define TMR_ICR(n)	(0x0074 + ((n) << 2))
#define TMR_WICR	(0x0080)
#define TMR_CER		(0x0084)
#define TMR_CMR		(0x0088)
#define TMR_ILR(n)	(0x008c + ((n) << 2))
#define TMR_WCR		(0x0098)
#define TMR_WFAR	(0x009c)
#define TMR_WSAR	(0x00A0)
#define TMR_CVWR(n)	(0x00A4 + ((n) << 2))

#define TMR_CCR_CS_0(x)	(((x) & 0x3) << 0)
#define TMR_CCR_CS_1(x)	(((x) & 0x7) << 2)
#define TMR_CCR_CS_2(x)	(((x) & 0x3) << 5)

#endif /* __ASM_MACH_REGS_TIMERS_H */
