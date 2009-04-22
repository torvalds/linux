/*
 * Freescale STMP378X interrupts
 *
 * Copyright (C) 2005 Sigmatel Inc
 *
 * Copyright 2008 Freescale Semiconductor, Inc. All Rights Reserved.
 * Copyright 2008 Embedded Alley Solutions, Inc All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

#define IRQ_DEBUG_UART			0
#define IRQ_COMMS_RX			1
#define IRQ_COMMS_TX			1
#define IRQ_SSP2_ERROR			2
#define IRQ_VDD5V			3
#define IRQ_HEADPHONE_SHORT		4
#define IRQ_DAC_DMA			5
#define IRQ_DAC_ERROR			6
#define IRQ_ADC_DMA			7
#define IRQ_ADC_ERROR			8
#define IRQ_SPDIF_DMA			9
#define IRQ_SAIF2_DMA			9
#define IRQ_SPDIF_ERROR			10
#define IRQ_SAIF1_IRQ			10
#define IRQ_SAIF2_IRQ			10
#define IRQ_USB_CTRL			11
#define IRQ_USB_WAKEUP			12
#define IRQ_GPMI_DMA			13
#define IRQ_SSP1_DMA			14
#define IRQ_SSP_ERROR			15
#define IRQ_GPIO0			16
#define IRQ_GPIO1			17
#define IRQ_GPIO2			18
#define IRQ_SAIF1_DMA			19
#define IRQ_SSP2_DMA			20
#define IRQ_ECC8_IRQ			21
#define IRQ_RTC_ALARM			22
#define IRQ_UARTAPP_TX_DMA		23
#define IRQ_UARTAPP_INTERNAL		24
#define IRQ_UARTAPP_RX_DMA		25
#define IRQ_I2C_DMA			26
#define IRQ_I2C_ERROR			27
#define IRQ_TIMER0			28
#define IRQ_TIMER1			29
#define IRQ_TIMER2			30
#define IRQ_TIMER3			31
#define IRQ_BATT_BRNOUT			32
#define IRQ_VDDD_BRNOUT			33
#define IRQ_VDDIO_BRNOUT		34
#define IRQ_VDD18_BRNOUT		35
#define IRQ_TOUCH_DETECT		36
#define IRQ_LRADC_CH0			37
#define IRQ_LRADC_CH1			38
#define IRQ_LRADC_CH2			39
#define IRQ_LRADC_CH3			40
#define IRQ_LRADC_CH4			41
#define IRQ_LRADC_CH5			42
#define IRQ_LRADC_CH6			43
#define IRQ_LRADC_CH7			44
#define IRQ_LCDIF_DMA			45
#define IRQ_LCDIF_ERROR			46
#define IRQ_DIGCTL_DEBUG_TRAP		47
#define IRQ_RTC_1MSEC			48
#define IRQ_DRI_DMA			49
#define IRQ_DRI_ATTENTION		50
#define IRQ_GPMI_ATTENTION		51
#define IRQ_IR				52
#define IRQ_DCP_VMI			53
#define IRQ_DCP				54
#define IRQ_BCH				56
#define IRQ_PXP				57
#define IRQ_UARTAPP2_TX_DMA		58
#define IRQ_UARTAPP2_INTERNAL		59
#define IRQ_UARTAPP2_RX_DMA		60
#define IRQ_VDAC_DETECT			61
#define IRQ_VDD5V_DROOP			64
#define IRQ_DCDC4P2_BO			65


#define NR_REAL_IRQS	128
#define NR_IRQS		(NR_REAL_IRQS + 32 * 3)

/* All interrupts are FIQ capable */
#define FIQ_START		IRQ_DEBUG_UART

/* Hard disk IRQ is a GPMI attention IRQ */
#define IRQ_HARDDISK		IRQ_GPMI_ATTENTION
