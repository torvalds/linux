/*
 *  arch/arm/mach-integrator/include/mach/irqs.h
 *
 *  Copyright (C) 1999 ARM Limited
 *  Copyright (C) 2000 Deep Blue Solutions Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

/*
 * Interrupt numbers, all of the above are just static reservations
 * used so they can be encoded into device resources. They will finally
 * be done away with when switching to device tree.
 */
#define IRQ_PIC_START			64
#define IRQ_SOFTINT			(IRQ_PIC_START+0)
#define IRQ_UARTINT0			(IRQ_PIC_START+1)
#define IRQ_UARTINT1			(IRQ_PIC_START+2)
#define IRQ_KMIINT0			(IRQ_PIC_START+3)
#define IRQ_KMIINT1			(IRQ_PIC_START+4)
#define IRQ_TIMERINT0			(IRQ_PIC_START+5)
#define IRQ_TIMERINT1			(IRQ_PIC_START+6)
#define IRQ_TIMERINT2			(IRQ_PIC_START+7)
#define IRQ_RTCINT			(IRQ_PIC_START+8)
#define IRQ_AP_EXPINT0			(IRQ_PIC_START+9)
#define IRQ_AP_EXPINT1			(IRQ_PIC_START+10)
#define IRQ_AP_EXPINT2			(IRQ_PIC_START+11)
#define IRQ_AP_EXPINT3			(IRQ_PIC_START+12)
#define IRQ_AP_PCIINT0			(IRQ_PIC_START+13)
#define IRQ_AP_PCIINT1			(IRQ_PIC_START+14)
#define IRQ_AP_PCIINT2			(IRQ_PIC_START+15)
#define IRQ_AP_PCIINT3			(IRQ_PIC_START+16)
#define IRQ_AP_V3INT			(IRQ_PIC_START+17)
#define IRQ_AP_CPINT0			(IRQ_PIC_START+18)
#define IRQ_AP_CPINT1			(IRQ_PIC_START+19)
#define IRQ_AP_LBUSTIMEOUT 		(IRQ_PIC_START+20)
#define IRQ_AP_APCINT			(IRQ_PIC_START+21)
#define IRQ_CP_CLCDCINT			(IRQ_PIC_START+22)
#define IRQ_CP_MMCIINT0			(IRQ_PIC_START+23)
#define IRQ_CP_MMCIINT1			(IRQ_PIC_START+24)
#define IRQ_CP_AACIINT			(IRQ_PIC_START+25)
#define IRQ_CP_CPPLDINT			(IRQ_PIC_START+26)
#define IRQ_CP_ETHINT			(IRQ_PIC_START+27)
#define IRQ_CP_TSPENINT			(IRQ_PIC_START+28)
#define IRQ_PIC_END			(IRQ_PIC_START+28)

#define IRQ_CIC_START			(IRQ_PIC_END+1)
#define IRQ_CM_SOFTINT			(IRQ_CIC_START+0)
#define IRQ_CM_COMMRX			(IRQ_CIC_START+1)
#define IRQ_CM_COMMTX			(IRQ_CIC_START+2)
#define IRQ_CIC_END			(IRQ_CIC_START+2)

/*
 * IntegratorCP only
 */
#define IRQ_SIC_START			(IRQ_CIC_END+1)
#define IRQ_SIC_CP_SOFTINT		(IRQ_SIC_START+0)
#define IRQ_SIC_CP_RI0			(IRQ_SIC_START+1)
#define IRQ_SIC_CP_RI1			(IRQ_SIC_START+2)
#define IRQ_SIC_CP_CARDIN		(IRQ_SIC_START+3)
#define IRQ_SIC_CP_LMINT0		(IRQ_SIC_START+4)
#define IRQ_SIC_CP_LMINT1		(IRQ_SIC_START+5)
#define IRQ_SIC_CP_LMINT2		(IRQ_SIC_START+6)
#define IRQ_SIC_CP_LMINT3		(IRQ_SIC_START+7)
#define IRQ_SIC_CP_LMINT4		(IRQ_SIC_START+8)
#define IRQ_SIC_CP_LMINT5		(IRQ_SIC_START+9)
#define IRQ_SIC_CP_LMINT6		(IRQ_SIC_START+10)
#define IRQ_SIC_CP_LMINT7		(IRQ_SIC_START+11)
#define IRQ_SIC_END			(IRQ_SIC_START+11)
