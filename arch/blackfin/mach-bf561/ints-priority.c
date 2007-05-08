/*
 * File:         arch/blackfin/mach-bf561/ints-priority.c
 * Based on:     arch/blackfin/mach-bf537/ints-priority.c
 * Author:       Michael Hennerich
 *
 * Created:
 * Description:  Set up the interupt priorities
 *
 * Modified:
 *               Copyright 2004-2006 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
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
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

#include <linux/module.h>
#include <asm/blackfin.h>
#include <asm/irq.h>

void program_IAR(void)
{
	/* Program the IAR0 Register with the configured priority */
	bfin_write_SICA_IAR0(((CONFIG_IRQ_PLL_WAKEUP - 7) << IRQ_PLL_WAKEUP_POS) |
			     ((CONFIG_IRQ_DMA1_ERROR - 7) << IRQ_DMA1_ERROR_POS) |
			     ((CONFIG_IRQ_DMA2_ERROR - 7) << IRQ_DMA2_ERROR_POS) |
			     ((CONFIG_IRQ_IMDMA_ERROR - 7) << IRQ_IMDMA_ERROR_POS) |
			     ((CONFIG_IRQ_PPI0_ERROR - 7) << IRQ_PPI0_ERROR_POS) |
			     ((CONFIG_IRQ_PPI1_ERROR - 7) << IRQ_PPI1_ERROR_POS) |
			     ((CONFIG_IRQ_SPORT0_ERROR - 7) << IRQ_SPORT0_ERROR_POS) |
			     ((CONFIG_IRQ_SPORT1_ERROR - 7) << IRQ_SPORT1_ERROR_POS));

	bfin_write_SICA_IAR1(((CONFIG_IRQ_SPI_ERROR - 7) << IRQ_SPI_ERROR_POS) |
			     ((CONFIG_IRQ_UART_ERROR - 7) << IRQ_UART_ERROR_POS) |
			     ((CONFIG_IRQ_RESERVED_ERROR - 7) << IRQ_RESERVED_ERROR_POS) |
			     ((CONFIG_IRQ_DMA1_0 - 7) << IRQ_DMA1_0_POS) |
			     ((CONFIG_IRQ_DMA1_1 - 7) << IRQ_DMA1_1_POS) |
			     ((CONFIG_IRQ_DMA1_2 - 7) << IRQ_DMA1_2_POS) |
			     ((CONFIG_IRQ_DMA1_3 - 7) << IRQ_DMA1_3_POS) |
			     ((CONFIG_IRQ_DMA1_4 - 7) << IRQ_DMA1_4_POS));

	bfin_write_SICA_IAR2(((CONFIG_IRQ_DMA1_5 - 7) << IRQ_DMA1_5_POS) |
			     ((CONFIG_IRQ_DMA1_6 - 7) << IRQ_DMA1_6_POS) |
			     ((CONFIG_IRQ_DMA1_7 - 7) << IRQ_DMA1_7_POS) |
			     ((CONFIG_IRQ_DMA1_8 - 7) << IRQ_DMA1_8_POS) |
			     ((CONFIG_IRQ_DMA1_9 - 7) << IRQ_DMA1_9_POS) |
			     ((CONFIG_IRQ_DMA1_10 - 7) << IRQ_DMA1_10_POS) |
			     ((CONFIG_IRQ_DMA1_11 - 7) << IRQ_DMA1_11_POS) |
			     ((CONFIG_IRQ_DMA2_0 - 7) << IRQ_DMA2_0_POS));

	bfin_write_SICA_IAR3(((CONFIG_IRQ_DMA2_1 - 7) << IRQ_DMA2_1_POS) |
			     ((CONFIG_IRQ_DMA2_2 - 7) << IRQ_DMA2_2_POS) |
			     ((CONFIG_IRQ_DMA2_3 - 7) << IRQ_DMA2_3_POS) |
			     ((CONFIG_IRQ_DMA2_4 - 7) << IRQ_DMA2_4_POS) |
			     ((CONFIG_IRQ_DMA2_5 - 7) << IRQ_DMA2_5_POS) |
			     ((CONFIG_IRQ_DMA2_6 - 7) << IRQ_DMA2_6_POS) |
			     ((CONFIG_IRQ_DMA2_7 - 7) << IRQ_DMA2_7_POS) |
			     ((CONFIG_IRQ_DMA2_8 - 7) << IRQ_DMA2_8_POS));

	bfin_write_SICA_IAR4(((CONFIG_IRQ_DMA2_9 - 7) << IRQ_DMA2_9_POS) |
			     ((CONFIG_IRQ_DMA2_10 - 7) << IRQ_DMA2_10_POS) |
			     ((CONFIG_IRQ_DMA2_11 - 7) << IRQ_DMA2_11_POS) |
			     ((CONFIG_IRQ_TIMER0 - 7) << IRQ_TIMER0_POS) |
			     ((CONFIG_IRQ_TIMER1 - 7) << IRQ_TIMER1_POS) |
			     ((CONFIG_IRQ_TIMER2 - 7) << IRQ_TIMER2_POS) |
			     ((CONFIG_IRQ_TIMER3 - 7) << IRQ_TIMER3_POS) |
			     ((CONFIG_IRQ_TIMER4 - 7) << IRQ_TIMER4_POS));

	bfin_write_SICA_IAR5(((CONFIG_IRQ_TIMER5 - 7) << IRQ_TIMER5_POS) |
			     ((CONFIG_IRQ_TIMER6 - 7) << IRQ_TIMER6_POS) |
			     ((CONFIG_IRQ_TIMER7 - 7) << IRQ_TIMER7_POS) |
			     ((CONFIG_IRQ_TIMER8 - 7) << IRQ_TIMER8_POS) |
			     ((CONFIG_IRQ_TIMER9 - 7) << IRQ_TIMER9_POS) |
			     ((CONFIG_IRQ_TIMER10 - 7) << IRQ_TIMER10_POS) |
			     ((CONFIG_IRQ_TIMER11 - 7) << IRQ_TIMER11_POS) |
			     ((CONFIG_IRQ_PROG0_INTA - 7) << IRQ_PROG0_INTA_POS));

	bfin_write_SICA_IAR6(((CONFIG_IRQ_PROG0_INTB - 7) << IRQ_PROG0_INTB_POS) |
			     ((CONFIG_IRQ_PROG1_INTA - 7) << IRQ_PROG1_INTA_POS) |
			     ((CONFIG_IRQ_PROG1_INTB - 7) << IRQ_PROG1_INTB_POS) |
			     ((CONFIG_IRQ_PROG2_INTA - 7) << IRQ_PROG2_INTA_POS) |
			     ((CONFIG_IRQ_PROG2_INTB - 7) << IRQ_PROG2_INTB_POS) |
			     ((CONFIG_IRQ_DMA1_WRRD0 - 7) << IRQ_DMA1_WRRD0_POS) |
			     ((CONFIG_IRQ_DMA1_WRRD1 - 7) << IRQ_DMA1_WRRD1_POS) |
			     ((CONFIG_IRQ_DMA2_WRRD0 - 7) << IRQ_DMA2_WRRD0_POS));

	bfin_write_SICA_IAR7(((CONFIG_IRQ_DMA2_WRRD1 - 7) << IRQ_DMA2_WRRD1_POS) |
			     ((CONFIG_IRQ_IMDMA_WRRD0 - 7) << IRQ_IMDMA_WRRD0_POS) |
			     ((CONFIG_IRQ_IMDMA_WRRD1 - 7) << IRQ_IMDMA_WRRD1_POS) |
			     ((CONFIG_IRQ_WDTIMER - 7) << IRQ_WDTIMER_POS) |
			     (0 << IRQ_RESERVED_1_POS) | (0 << IRQ_RESERVED_2_POS) |
			     (0 << IRQ_SUPPLE_0_POS) | (0 << IRQ_SUPPLE_1_POS));

	SSYNC();
}
