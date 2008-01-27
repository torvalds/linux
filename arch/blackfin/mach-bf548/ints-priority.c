/*
 * File:         arch/blackfin/mach-bf537/ints-priority.c
 * Based on:     arch/blackfin/mach-bf533/ints-priority.c
 * Author:       Michael Hennerich
 *
 * Created:
 * Description:  Set up the interrupt priorities
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
#include <linux/irq.h>
#include <asm/blackfin.h>

void program_IAR(void)
{
	/* Program the IAR0 Register with the configured priority */
	bfin_write_SIC_IAR0(((CONFIG_IRQ_PLL_WAKEUP - 7) << IRQ_PLL_WAKEUP_POS) |
			    ((CONFIG_IRQ_DMAC0_ERR - 7) << IRQ_DMAC0_ERR_POS) |
			    ((CONFIG_IRQ_EPPI0_ERR - 7) << IRQ_EPPI0_ERR_POS) |
			    ((CONFIG_IRQ_SPORT0_ERR - 7) << IRQ_SPORT0_ERR_POS) |
			    ((CONFIG_IRQ_SPORT1_ERR - 7) << IRQ_SPORT1_ERR_POS) |
			    ((CONFIG_IRQ_SPI0_ERR - 7) << IRQ_SPI0_ERR_POS) |
			    ((CONFIG_IRQ_UART0_ERR - 7) << IRQ_UART0_ERR_POS) |
			    ((CONFIG_IRQ_RTC - 7) << IRQ_RTC_POS));

	bfin_write_SIC_IAR1(((CONFIG_IRQ_EPPI0 - 7) << IRQ_EPPI0_POS) |
			    ((CONFIG_IRQ_SPORT0_RX - 7) << IRQ_SPORT0_RX_POS) |
			    ((CONFIG_IRQ_SPORT0_TX - 7) << IRQ_SPORT0_TX_POS) |
			    ((CONFIG_IRQ_SPORT1_RX - 7) << IRQ_SPORT1_RX_POS) |
			    ((CONFIG_IRQ_SPORT1_TX - 7) << IRQ_SPORT1_TX_POS) |
			    ((CONFIG_IRQ_SPI0 - 7) << IRQ_SPI0_POS) |
			    ((CONFIG_IRQ_UART0_RX - 7) << IRQ_UART0_RX_POS) |
			    ((CONFIG_IRQ_UART0_TX - 7) << IRQ_UART0_TX_POS));

	bfin_write_SIC_IAR2(((CONFIG_IRQ_TIMER8 - 7) << IRQ_TIMER8_POS) |
			    ((CONFIG_IRQ_TIMER9 - 7) << IRQ_TIMER9_POS) |
			    ((CONFIG_IRQ_PINT0 - 7) << IRQ_PINT0_POS) |
			    ((CONFIG_IRQ_PINT1 - 7) << IRQ_PINT1_POS) |
			    ((CONFIG_IRQ_MDMAS0 - 7) << IRQ_MDMAS0_POS) |
			    ((CONFIG_IRQ_MDMAS1 - 7) << IRQ_MDMAS1_POS) |
			    ((CONFIG_IRQ_WATCHDOG - 7) << IRQ_WATCH_POS));

	bfin_write_SIC_IAR3(((CONFIG_IRQ_DMAC1_ERR - 7) << IRQ_DMAC1_ERR_POS) |
			    ((CONFIG_IRQ_SPORT2_ERR - 7) << IRQ_SPORT2_ERR_POS) |
			    ((CONFIG_IRQ_SPORT3_ERR - 7) << IRQ_SPORT3_ERR_POS) |
			    ((CONFIG_IRQ_MXVR_DATA - 7) << IRQ_MXVR_DATA_POS) |
			    ((CONFIG_IRQ_SPI1_ERR - 7) << IRQ_SPI1_ERR_POS) |
			    ((CONFIG_IRQ_SPI2_ERR - 7) << IRQ_SPI2_ERR_POS) |
			    ((CONFIG_IRQ_UART1_ERR - 7) << IRQ_UART1_ERR_POS) |
			    ((CONFIG_IRQ_UART2_ERR - 7) << IRQ_UART2_ERR_POS));

	bfin_write_SIC_IAR4(((CONFIG_IRQ_CAN0_ERR - 7) << IRQ_CAN0_ERR_POS) |
			    ((CONFIG_IRQ_SPORT2_RX - 7) << IRQ_SPORT2_RX_POS) |
			    ((CONFIG_IRQ_SPORT2_TX - 7) << IRQ_SPORT2_TX_POS) |
			    ((CONFIG_IRQ_SPORT3_RX - 7) << IRQ_SPORT3_RX_POS) |
			    ((CONFIG_IRQ_SPORT3_TX - 7) << IRQ_SPORT3_TX_POS) |
			    ((CONFIG_IRQ_EPPI1 - 7) << IRQ_EPPI1_POS) |
			    ((CONFIG_IRQ_EPPI2 - 7) << IRQ_EPPI2_POS) |
			    ((CONFIG_IRQ_SPI1 - 7) << IRQ_SPI1_POS));

	bfin_write_SIC_IAR5(((CONFIG_IRQ_SPI2 - 7) << IRQ_SPI2_POS) |
			    ((CONFIG_IRQ_UART1_RX - 7) << IRQ_UART1_RX_POS) |
			    ((CONFIG_IRQ_UART1_TX - 7) << IRQ_UART1_TX_POS) |
			    ((CONFIG_IRQ_ATAPI_RX - 7) << IRQ_ATAPI_RX_POS) |
			    ((CONFIG_IRQ_ATAPI_TX - 7) << IRQ_ATAPI_TX_POS) |
			    ((CONFIG_IRQ_TWI0 - 7) << IRQ_TWI0_POS) |
			    ((CONFIG_IRQ_TWI1 - 7) << IRQ_TWI1_POS) |
			    ((CONFIG_IRQ_CAN0_RX - 7) << IRQ_CAN0_RX_POS));

	bfin_write_SIC_IAR6(((CONFIG_IRQ_CAN0_TX - 7) << IRQ_CAN0_TX_POS) |
			    ((CONFIG_IRQ_MDMAS2 - 7) << IRQ_MDMAS2_POS) |
			    ((CONFIG_IRQ_MDMAS3 - 7) << IRQ_MDMAS3_POS) |
			    ((CONFIG_IRQ_MXVR_ERR - 7) << IRQ_MXVR_ERR_POS) |
			    ((CONFIG_IRQ_MXVR_MSG - 7) << IRQ_MXVR_MSG_POS) |
			    ((CONFIG_IRQ_MXVR_PKT - 7) << IRQ_MXVR_PKT_POS) |
			    ((CONFIG_IRQ_EPPI1_ERR - 7) << IRQ_EPPI1_ERR_POS) |
			    ((CONFIG_IRQ_EPPI2_ERR - 7) << IRQ_EPPI2_ERR_POS));

	bfin_write_SIC_IAR7(((CONFIG_IRQ_UART3_ERR - 7) << IRQ_UART3_ERR_POS) |
			    ((CONFIG_IRQ_HOST_ERR - 7) << IRQ_HOST_ERR_POS) |
			    ((CONFIG_IRQ_PIXC_ERR - 7) << IRQ_PIXC_ERR_POS) |
			    ((CONFIG_IRQ_NFC_ERR - 7) << IRQ_NFC_ERR_POS) |
			    ((CONFIG_IRQ_ATAPI_ERR - 7) << IRQ_ATAPI_ERR_POS) |
			    ((CONFIG_IRQ_CAN1_ERR - 7) << IRQ_CAN1_ERR_POS) |
			    ((CONFIG_IRQ_HS_DMA_ERR - 7) << IRQ_HS_DMA_ERR_POS));

	bfin_write_SIC_IAR8(((CONFIG_IRQ_PIXC_IN0 - 7) << IRQ_PIXC_IN1_POS) |
			    ((CONFIG_IRQ_PIXC_IN1 - 7) << IRQ_PIXC_IN1_POS) |
			    ((CONFIG_IRQ_PIXC_OUT - 7) << IRQ_PIXC_OUT_POS) |
			    ((CONFIG_IRQ_SDH - 7) << IRQ_SDH_POS) |
			    ((CONFIG_IRQ_CNT - 7) << IRQ_CNT_POS) |
			    ((CONFIG_IRQ_KEY - 7) << IRQ_KEY_POS) |
			    ((CONFIG_IRQ_CAN1_RX - 7) << IRQ_CAN1_RX_POS) |
			    ((CONFIG_IRQ_CAN1_TX - 7) << IRQ_CAN1_TX_POS));

	bfin_write_SIC_IAR9(((CONFIG_IRQ_SDH_MASK0 - 7) << IRQ_SDH_MASK0_POS) |
			    ((CONFIG_IRQ_SDH_MASK1 - 7) << IRQ_SDH_MASK1_POS) |
			    ((CONFIG_IRQ_USB_INT0 - 7) << IRQ_USB_INT0_POS) |
			    ((CONFIG_IRQ_USB_INT1 - 7) << IRQ_USB_INT1_POS) |
			    ((CONFIG_IRQ_USB_INT2 - 7) << IRQ_USB_INT2_POS) |
			    ((CONFIG_IRQ_USB_DMA - 7) << IRQ_USB_DMA_POS) |
			    ((CONFIG_IRQ_OTPSEC - 7) << IRQ_OTPSEC_POS));

	bfin_write_SIC_IAR10(((CONFIG_IRQ_TIMER0 - 7) << IRQ_TIMER0_POS) |
			     ((CONFIG_IRQ_TIMER1 - 7) << IRQ_TIMER1_POS));

	bfin_write_SIC_IAR11(((CONFIG_IRQ_TIMER2 - 7) << IRQ_TIMER2_POS) |
			     ((CONFIG_IRQ_TIMER3 - 7) << IRQ_TIMER3_POS) |
			     ((CONFIG_IRQ_TIMER4 - 7) << IRQ_TIMER4_POS) |
			     ((CONFIG_IRQ_TIMER5 - 7) << IRQ_TIMER5_POS) |
			     ((CONFIG_IRQ_TIMER6 - 7) << IRQ_TIMER6_POS) |
			     ((CONFIG_IRQ_TIMER7 - 7) << IRQ_TIMER7_POS) |
			     ((CONFIG_IRQ_PINT2 - 7) << IRQ_PINT2_POS) |
			     ((CONFIG_IRQ_PINT3 - 7) << IRQ_PINT3_POS));

	SSYNC();
}
