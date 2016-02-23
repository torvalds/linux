/*
 * Copyright 2007-2008 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 *
 * Set up the interrupt priorities
 */

#include <linux/module.h>
#include <linux/irq.h>
#include <asm/blackfin.h>

u8 sec_int_priority[] = {
	255,	/* IRQ_SEC_ERR */
	255,	/* IRQ_CGU_EVT */
	254,	/* IRQ_WATCH0 */
	254,	/* IRQ_WATCH1 */
	253,	/* IRQ_L2CTL0_ECC_ERR */
	253,	/* IRQ_L2CTL0_ECC_WARN */
	253,	/* IRQ_C0_DBL_FAULT */
	253,	/* IRQ_C1_DBL_FAULT */
	252,	/* IRQ_C0_HW_ERR */
	252,	/* IRQ_C1_HW_ERR */
	255,	/* IRQ_C0_NMI_L1_PARITY_ERR */
	255,	/* IRQ_C1_NMI_L1_PARITY_ERR */

	50,	/* IRQ_TIMER0 */
	50,	/* IRQ_TIMER1 */
	50,	/* IRQ_TIMER2 */
	50,	/* IRQ_TIMER3 */
	50,	/* IRQ_TIMER4 */
	50,	/* IRQ_TIMER5 */
	50,	/* IRQ_TIMER6 */
	50,	/* IRQ_TIMER7 */
	50,	/* IRQ_TIMER_STAT */
	0,	/* IRQ_PINT0 */
	0,	/* IRQ_PINT1 */
	0,	/* IRQ_PINT2 */
	0,	/* IRQ_PINT3 */
	0,	/* IRQ_PINT4 */
	0,	/* IRQ_PINT5 */
	0,	/* IRQ_CNT */
	50,	/* RQ_PWM0_TRIP */
	50,	/* IRQ_PWM0_SYNC */
	50,	/* IRQ_PWM1_TRIP */
	50,	/* IRQ_PWM1_SYNC */
	0,	/* IRQ_TWI0 */
	0,	/* IRQ_TWI1 */
	10,	/* IRQ_SOFT0 */
	10,	/* IRQ_SOFT1 */
	10,	/* IRQ_SOFT2 */
	10,	/* IRQ_SOFT3 */
	0,	/* IRQ_ACM_EVT_MISS */
	0,	/* IRQ_ACM_EVT_COMPLETE */
	0,	/* IRQ_CAN0_RX */
	0,	/* IRQ_CAN0_TX */
	0,	/* IRQ_CAN0_STAT */
	100,	/* IRQ_SPORT0_TX */
	100,	/* IRQ_SPORT0_TX_STAT */
	100,	/* IRQ_SPORT0_RX */
	100,	/* IRQ_SPORT0_RX_STAT */
	100,	/* IRQ_SPORT1_TX */
	100,	/* IRQ_SPORT1_TX_STAT */
	100,	/* IRQ_SPORT1_RX */
	100,	/* IRQ_SPORT1_RX_STAT */
	100,	/* IRQ_SPORT2_TX */
	100,	/* IRQ_SPORT2_TX_STAT */
	100,	/* IRQ_SPORT2_RX */
	100,	/* IRQ_SPORT2_RX_STAT */
	0,	/* IRQ_SPI0_TX */
	0,	/* IRQ_SPI0_RX */
	0,	/* IRQ_SPI0_STAT */
	0,	/* IRQ_SPI1_TX */
	0,	/* IRQ_SPI1_RX */
	0,	/* IRQ_SPI1_STAT */
	0,	/* IRQ_RSI */
	0,	/* IRQ_RSI_INT0 */
	0,	/* IRQ_RSI_INT1 */
	0,	/* DMA11 Data (SDU) */
	0,	/* DMA12 Data (Reserved) */
	0,	/* Reserved */
	0,	/* Reserved */
	30,	/* IRQ_EMAC0_STAT */
	0,	/* EMAC0 Power (Reserved) */
	30,	/* IRQ_EMAC1_STAT */
	0,	/* EMAC1 Power (Reserved) */
	0,	/* IRQ_LP0 */
	0,	/* IRQ_LP0_STAT */
	0,	/* IRQ_LP1 */
	0,	/* IRQ_LP1_STAT */
	0,	/* IRQ_LP2 */
	0,	/* IRQ_LP2_STAT */
	0,	/* IRQ_LP3 */
	0,	/* IRQ_LP3_STAT */
	0,	/* IRQ_UART0_TX */
	0,	/* IRQ_UART0_RX */
	0,	/* IRQ_UART0_STAT */
	0,	/* IRQ_UART1_TX */
	0,	/* IRQ_UART1_RX */
	0,	/* IRQ_UART1_STAT */
	0,	/* IRQ_MDMA0_SRC_CRC0 */
	0,	/* IRQ_MDMA0_DEST_CRC0 */
	0,	/* IRQ_CRC0_DCNTEXP */
	0,	/* IRQ_CRC0_ERR */
	0,	/* IRQ_MDMA1_SRC_CRC1 */
	0,	/* IRQ_MDMA1_DEST_CRC1 */
	0,	/* IRQ_CRC1_DCNTEXP */
	0,	/* IRQ_CRC1_ERR */
	0,	/* IRQ_MDMA2_SRC */
	0,	/* IRQ_MDMA2_DEST */
	0,	/* IRQ_MDMA3_SRC */
	0,	/* IRQ_MDMA3_DEST */
	120,	/* IRQ_EPPI0_CH0 */
	120,	/* IRQ_EPPI0_CH1 */
	120,	/* IRQ_EPPI0_STAT */
	120,	/* IRQ_EPPI2_CH0 */
	120,	/* IRQ_EPPI2_CH1 */
	120,	/* IRQ_EPPI2_STAT */
	120,	/* IRQ_EPPI1_CH0 */
	120,	/* IRQ_EPPI1_CH1 */
	120,	/* IRQ_EPPI1_STAT */
	120,	/* IRQ_PIXC_CH0 */
	120,	/* IRQ_PIXC_CH1 */
	120,	/* IRQ_PIXC_CH2 */
	120,	/* IRQ_PIXC_STAT */
	120,	/* IRQ_PVP_CPDOB */
	120,	/* IRQ_PVP_CPDOC */
	120,	/* IRQ_PVP_CPSTAT */
	120,	/* IRQ_PVP_CPCI */
	120,	/* IRQ_PVP_STAT0 */
	120,	/* IRQ_PVP_MPDO */
	120,	/* IRQ_PVP_MPDI */
	120,	/* IRQ_PVP_MPSTAT */
	120,	/* IRQ_PVP_MPCI */
	120,	/* IRQ_PVP_CPDOA */
	120,	/* IRQ_PVP_STAT1 */
	0,	/* IRQ_USB_STAT */
	0,	/* IRQ_USB_DMA */
	0,	/* IRQ_TRU_INT0 */
	0,	/* IRQ_TRU_INT1 */
	0,	/* IRQ_TRU_INT2	*/
	0,	/* IRQ_TRU_INT3 */
	0,	/* IRQ_DMAC0_ERROR */
	0,	/* IRQ_CGU0_ERROR */
	0,	/* Reserved */
	0,	/* IRQ_DPM */
	0,	/* Reserved */
	0,	/* IRQ_SWU0 */
	0,	/* IRQ_SWU1 */
	0,	/* IRQ_SWU2 */
	0,	/* IRQ_SWU3 */
	0,	/* IRQ_SWU4 */
	0,	/* IRQ_SWU4 */
	0,	/* IRQ_SWU6 */
};

