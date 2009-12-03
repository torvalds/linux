/*
 * Copyright 2005-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 *
 * Set up the interrupt priorities
 */

#include <linux/module.h>
#include <linux/irq.h>
#include <asm/blackfin.h>

void __init program_IAR(void)
{
	/* Program the IAR0 Register with the configured priority */
	bfin_write_SIC_IAR0(((CONFIG_IRQ_PLL_WAKEUP - 7) << IRQ_PLL_WAKEUP_POS) |
			    ((CONFIG_IRQ_DMA_ERROR - 7) << IRQ_DMA_ERROR_POS) |
			    ((CONFIG_IRQ_ERROR - 7) << IRQ_ERROR_POS) |
			    ((CONFIG_IRQ_RTC - 7) << IRQ_RTC_POS) |
			    ((CONFIG_IRQ_PPI - 7) << IRQ_PPI_POS) |
			    ((CONFIG_IRQ_SPORT0_RX - 7) << IRQ_SPORT0_RX_POS) |
			    ((CONFIG_IRQ_SPORT0_TX - 7) << IRQ_SPORT0_TX_POS) |
			    ((CONFIG_IRQ_SPORT1_RX - 7) << IRQ_SPORT1_RX_POS));

	bfin_write_SIC_IAR1(((CONFIG_IRQ_SPORT1_TX - 7) << IRQ_SPORT1_TX_POS) |
			    ((CONFIG_IRQ_TWI - 7) << IRQ_TWI_POS) |
			    ((CONFIG_IRQ_SPI - 7) << IRQ_SPI_POS) |
			    ((CONFIG_IRQ_UART0_RX - 7) << IRQ_UART0_RX_POS) |
			    ((CONFIG_IRQ_UART0_TX - 7) << IRQ_UART0_TX_POS) |
			    ((CONFIG_IRQ_UART1_RX - 7) << IRQ_UART1_RX_POS) |
			    ((CONFIG_IRQ_UART1_TX - 7) << IRQ_UART1_TX_POS) |
			    ((CONFIG_IRQ_CAN_RX - 7) << IRQ_CAN_RX_POS));

	bfin_write_SIC_IAR2(((CONFIG_IRQ_CAN_TX - 7) << IRQ_CAN_TX_POS) |
			    ((CONFIG_IRQ_MAC_RX - 7) << IRQ_MAC_RX_POS) |
			    ((CONFIG_IRQ_MAC_TX - 7) << IRQ_MAC_TX_POS) |
			    ((CONFIG_IRQ_TIMER0 - 7) << IRQ_TIMER0_POS) |
			    ((CONFIG_IRQ_TIMER1 - 7) << IRQ_TIMER1_POS) |
			    ((CONFIG_IRQ_TIMER2 - 7) << IRQ_TIMER2_POS) |
			    ((CONFIG_IRQ_TIMER3 - 7) << IRQ_TIMER3_POS) |
			    ((CONFIG_IRQ_TIMER4 - 7) << IRQ_TIMER4_POS));

	bfin_write_SIC_IAR3(((CONFIG_IRQ_TIMER5 - 7) << IRQ_TIMER5_POS) |
			    ((CONFIG_IRQ_TIMER6 - 7) << IRQ_TIMER6_POS) |
			    ((CONFIG_IRQ_TIMER7 - 7) << IRQ_TIMER7_POS) |
			    ((CONFIG_IRQ_PROG_INTA - 7) << IRQ_PROG_INTA_POS) |
			    ((CONFIG_IRQ_PORTG_INTB - 7) << IRQ_PORTG_INTB_POS) |
			    ((CONFIG_IRQ_MEM_DMA0 - 7) << IRQ_MEM_DMA0_POS) |
			    ((CONFIG_IRQ_MEM_DMA1 - 7) << IRQ_MEM_DMA1_POS) |
			    ((CONFIG_IRQ_WATCH - 7) << IRQ_WATCH_POS));

	SSYNC();
}
