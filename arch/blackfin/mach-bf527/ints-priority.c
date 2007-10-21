/*
 * File:         arch/blackfin/mach-bf537/ints-priority.c
 * Based on:     arch/blackfin/mach-bf533/ints-priority.c
 * Author:       Michael Hennerich (michael.hennerich@analog.com)
 *
 * Created:
 * Description:  Set up the interrupt priorities
 *
 * Modified:
 *               Copyright 2004-2007 Analog Devices Inc.
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
#include <linux/irq.h>
#include <asm/blackfin.h>

void program_IAR(void)
{
	/* Program the IAR0 Register with the configured priority */
	bfin_write_SIC_IAR0(((CONFIG_IRQ_PLL_WAKEUP - 7) << IRQ_PLL_WAKEUP_POS) |
			((CONFIG_IRQ_DMA0_ERROR - 7) << IRQ_DMA0_ERROR_POS) |
			((CONFIG_IRQ_DMAR0_BLK - 7) << IRQ_DMAR0_BLK_POS) |
			((CONFIG_IRQ_DMAR1_BLK - 7) << IRQ_DMAR1_BLK_POS) |
			((CONFIG_IRQ_DMAR0_OVR - 7) << IRQ_DMAR0_OVR_POS) |
			((CONFIG_IRQ_DMAR1_OVR - 7) << IRQ_DMAR1_OVR_POS) |
			((CONFIG_IRQ_PPI_ERROR - 7) << IRQ_PPI_ERROR_POS) |
			((CONFIG_IRQ_MAC_ERROR - 7) << IRQ_MAC_ERROR_POS));


	bfin_write_SIC_IAR1(((CONFIG_IRQ_SPORT0_ERROR - 7) << IRQ_SPORT0_ERROR_POS) |
			((CONFIG_IRQ_SPORT1_ERROR - 7) << IRQ_SPORT1_ERROR_POS) |
			((CONFIG_IRQ_UART0_ERROR - 7) << IRQ_UART0_ERROR_POS) |
			((CONFIG_IRQ_UART1_ERROR - 7) << IRQ_UART1_ERROR_POS) |
			((CONFIG_IRQ_RTC - 7) << IRQ_RTC_POS) |
			((CONFIG_IRQ_PPI - 7) << IRQ_PPI_POS));

	bfin_write_SIC_IAR2(((CONFIG_IRQ_SPORT0_RX - 7) << IRQ_SPORT0_RX_POS) |
			((CONFIG_IRQ_SPORT0_TX - 7) << IRQ_SPORT0_TX_POS) |
			((CONFIG_IRQ_SPORT1_RX - 7) << IRQ_SPORT1_RX_POS) |
			((CONFIG_IRQ_SPORT1_TX - 7) << IRQ_SPORT1_TX_POS) |
			((CONFIG_IRQ_TWI - 7) << IRQ_TWI_POS) |
			((CONFIG_IRQ_SPI - 7) << IRQ_SPI_POS) |
			((CONFIG_IRQ_UART0_RX - 7) << IRQ_UART0_RX_POS) |
			((CONFIG_IRQ_UART0_TX - 7) << IRQ_UART0_TX_POS));

	bfin_write_SIC_IAR3(((CONFIG_IRQ_UART1_RX - 7) << IRQ_UART1_RX_POS) |
			((CONFIG_IRQ_UART1_TX - 7) << IRQ_UART1_TX_POS) |
			((CONFIG_IRQ_OPTSEC - 7) << IRQ_OPTSEC_POS) |
			((CONFIG_IRQ_CNT - 7) << IRQ_CNT_POS) |
			((CONFIG_IRQ_MAC_RX - 7) << IRQ_MAC_RX_POS) |
			((CONFIG_IRQ_PORTH_INTA - 7) << IRQ_PORTH_INTA_POS) |
			((CONFIG_IRQ_MAC_TX - 7) << IRQ_MAC_TX_POS) |
			((CONFIG_IRQ_PORTH_INTB - 7) << IRQ_PORTH_INTB_POS));

	bfin_write_SIC_IAR4(((CONFIG_IRQ_TMR0 - 7) << IRQ_TMR0_POS) |
			((CONFIG_IRQ_TMR1 - 7) << IRQ_TMR1_POS) |
			((CONFIG_IRQ_TMR2 - 7) << IRQ_TMR2_POS) |
			((CONFIG_IRQ_TMR3 - 7) << IRQ_TMR3_POS) |
			((CONFIG_IRQ_TMR4 - 7) << IRQ_TMR4_POS) |
			((CONFIG_IRQ_TMR5 - 7) << IRQ_TMR5_POS) |
			((CONFIG_IRQ_TMR6 - 7) << IRQ_TMR6_POS) |
			((CONFIG_IRQ_TMR7 - 7) << IRQ_TMR7_POS));

	bfin_write_SIC_IAR5(((CONFIG_IRQ_PORTG_INTA - 7) << IRQ_PORTG_INTA_POS) |
			((CONFIG_IRQ_PORTG_INTB - 7) << IRQ_PORTG_INTB_POS) |
			((CONFIG_IRQ_MEM_DMA0 - 7) << IRQ_MEM_DMA0_POS) |
			((CONFIG_IRQ_MEM_DMA1 - 7) << IRQ_MEM_DMA1_POS) |
			((CONFIG_IRQ_WATCH - 7) << IRQ_WATCH_POS) |
			((CONFIG_IRQ_PORTF_INTA - 7) << IRQ_PORTF_INTA_POS) |
			((CONFIG_IRQ_PORTF_INTB - 7) << IRQ_PORTF_INTB_POS) |
			((CONFIG_IRQ_SPI_ERROR - 7) << IRQ_SPI_ERROR_POS));

	bfin_write_SIC_IAR6(((CONFIG_IRQ_NFC_ERROR - 7) << IRQ_NFC_ERROR_POS) |
			((CONFIG_IRQ_HDMA_ERROR - 7) << IRQ_HDMA_ERROR_POS) |
			((CONFIG_IRQ_HDMA - 7) << IRQ_HDMA_POS) |
			((CONFIG_IRQ_USB_EINT - 7) << IRQ_USB_EINT_POS) |
			((CONFIG_IRQ_USB_INT0 - 7) << IRQ_USB_INT0_POS) |
			((CONFIG_IRQ_USB_INT1 - 7) << IRQ_USB_INT1_POS) |
			((CONFIG_IRQ_USB_INT2 - 7) << IRQ_USB_INT2_POS) |
			((CONFIG_IRQ_USB_DMA - 7) << IRQ_USB_DMA_POS));

	SSYNC();
}
