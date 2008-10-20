/*
 * File:         include/asm-blackfin/mach-bf533/defBF532.h
 * Based on:
 * Author:
 *
 * Created:
 * Description:
 *
 * Rev:
 *
 * Modified:
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; see the file COPYING.
 * If not, write to the Free Software Foundation,
 * 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */

#ifndef _BF533_IRQ_H_
#define _BF533_IRQ_H_

/*
 * Interrupt source definitions
             Event Source    Core Event Name
Core        Emulation               **
 Events         (highest priority)  EMU         0
            Reset                   RST         1
            NMI                     NMI         2
            Exception               EVX         3
            Reserved                --          4
            Hardware Error          IVHW        5
            Core Timer              IVTMR       6 *
	    PLL Wakeup Interrupt    IVG7	7
	    DMA Error (generic)	    IVG7	8
	    PPI Error Interrupt     IVG7	9
	    SPORT0 Error Interrupt  IVG7	10
	    SPORT1 Error Interrupt  IVG7	11
	    SPI Error Interrupt	    IVG7	12
	    UART Error Interrupt    IVG7	13
	    RTC Interrupt	    IVG8        14
	    DMA0 Interrupt (PPI)    IVG8	15
	    DMA1 (SPORT0 RX)	    IVG9	16
	    DMA2 (SPORT0 TX)	    IVG9        17
	    DMA3 (SPORT1 RX)        IVG9	18
	    DMA4 (SPORT1 TX)	    IVG9	19
	    DMA5 (PPI)		    IVG10	20
	    DMA6 (UART RX)	    IVG10	21
	    DMA7 (UART TX)	    IVG10	22
	    Timer0		    IVG11	23
	    Timer1		    IVG11	24
	    Timer2		    IVG11	25
	    PF Interrupt A	    IVG12	26
	    PF Interrupt B	    IVG12	27
	    DMA8/9 Interrupt	    IVG13	28
	    DMA10/11 Interrupt	    IVG13	29
	    Watchdog Timer	    IVG13	30

            Softirq		    IVG14       31
            System Call    --
                 (lowest priority)  IVG15       32 *
 */
#define SYS_IRQS	31
#define NR_PERI_INTS	24

/* The ABSTRACT IRQ definitions */
/** the first seven of the following are fixed, the rest you change if you need to **/
#define	IRQ_EMU			0	/*Emulation */
#define	IRQ_RST			1	/*reset */
#define	IRQ_NMI			2	/*Non Maskable */
#define	IRQ_EVX			3	/*Exception */
#define	IRQ_UNUSED		4	/*- unused interrupt*/
#define	IRQ_HWERR		5	/*Hardware Error */
#define	IRQ_CORETMR		6	/*Core timer */

#define	IRQ_PLL_WAKEUP		7	/*PLL Wakeup Interrupt */
#define	IRQ_DMA_ERROR		8	/*DMA Error (general) */
#define	IRQ_PPI_ERROR		9	/*PPI Error Interrupt */
#define	IRQ_SPORT0_ERROR	10	/*SPORT0 Error Interrupt */
#define	IRQ_SPORT1_ERROR	11	/*SPORT1 Error Interrupt */
#define	IRQ_SPI_ERROR		12	/*SPI Error Interrupt */
#define	IRQ_UART_ERROR		13	/*UART Error Interrupt */
#define	IRQ_RTC			14	/*RTC Interrupt */
#define	IRQ_PPI			15	/*DMA0 Interrupt (PPI) */
#define	IRQ_SPORT0_RX		16	/*DMA1 Interrupt (SPORT0 RX) */
#define	IRQ_SPORT0_TX		17	/*DMA2 Interrupt (SPORT0 TX) */
#define	IRQ_SPORT1_RX		18	/*DMA3 Interrupt (SPORT1 RX) */
#define	IRQ_SPORT1_TX		19	/*DMA4 Interrupt (SPORT1 TX) */
#define 	IRQ_SPI			20	/*DMA5 Interrupt (SPI) */
#define	IRQ_UART_RX		21	/*DMA6 Interrupt (UART RX) */
#define	IRQ_UART_TX		22	/*DMA7 Interrupt (UART TX) */
#define	IRQ_TMR0		23	/*Timer 0 */
#define	IRQ_TMR1		24	/*Timer 1 */
#define	IRQ_TMR2		25	/*Timer 2 */
#define	IRQ_PROG_INTA		26	/*Programmable Flags A (8) */
#define	IRQ_PROG_INTB		27	/*Programmable Flags B (8) */
#define	IRQ_MEM_DMA0		28	/*DMA8/9 Interrupt (Memory DMA Stream 0) */
#define	IRQ_MEM_DMA1		29	/*DMA10/11 Interrupt (Memory DMA Stream 1) */
#define	IRQ_WATCH	   	30	/*Watch Dog Timer */

#define IRQ_PF0			33
#define IRQ_PF1			34
#define IRQ_PF2			35
#define IRQ_PF3			36
#define IRQ_PF4			37
#define IRQ_PF5			38
#define IRQ_PF6			39
#define IRQ_PF7			40
#define IRQ_PF8			41
#define IRQ_PF9			42
#define IRQ_PF10		43
#define IRQ_PF11		44
#define IRQ_PF12		45
#define IRQ_PF13		46
#define IRQ_PF14		47
#define IRQ_PF15		48

#define GPIO_IRQ_BASE		IRQ_PF0

#define	NR_IRQS		(IRQ_PF15+1)

#define IVG7			7
#define IVG8			8
#define IVG9			9
#define IVG10			10
#define IVG11			11
#define IVG12			12
#define IVG13			13
#define IVG14			14
#define IVG15			15

/* IAR0 BIT FIELDS*/
#define RTC_ERROR_POS			28
#define UART_ERROR_POS			24
#define SPORT1_ERROR_POS		20
#define SPI_ERROR_POS			16
#define SPORT0_ERROR_POS		12
#define PPI_ERROR_POS			8
#define DMA_ERROR_POS			4
#define PLLWAKE_ERROR_POS		0

/* IAR1 BIT FIELDS*/
#define DMA7_UARTTX_POS			28
#define DMA6_UARTRX_POS			24
#define DMA5_SPI_POS			20
#define DMA4_SPORT1TX_POS		16
#define DMA3_SPORT1RX_POS		12
#define DMA2_SPORT0TX_POS		8
#define DMA1_SPORT0RX_POS		4
#define DMA0_PPI_POS			0

/* IAR2 BIT FIELDS*/
#define WDTIMER_POS			28
#define MEMDMA1_POS			24
#define MEMDMA0_POS			20
#define PFB_POS				16
#define PFA_POS				12
#define TIMER2_POS			8
#define TIMER1_POS			4
#define TIMER0_POS			0

#endif				/* _BF533_IRQ_H_ */
