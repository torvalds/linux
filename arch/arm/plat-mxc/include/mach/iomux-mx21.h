/*
 * Copyright (C) 2009 by Holger Schurig <hs4233@mail.mn-solutions.de>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston,
 * MA 02110-1301, USA.
 */
#ifndef __MACH_IOMUX_MX21_H__
#define __MACH_IOMUX_MX21_H__

#include <mach/iomux-mx2x.h>
#include <mach/iomux-v1.h>

/* Primary GPIO pin functions */

#define PB22_PF_USBH1_BYP	(GPIO_PORTB | GPIO_PF | 22)
#define PB25_PF_USBH1_ON	(GPIO_PORTB | GPIO_PF | 25)
#define PC5_PF_USBOTG_SDA	(GPIO_PORTC | GPIO_PF | 5)
#define PC6_PF_USBOTG_SCL	(GPIO_PORTC | GPIO_PF | 6)
#define PC7_PF_USBOTG_ON	(GPIO_PORTC | GPIO_PF | 7)
#define PC8_PF_USBOTG_FS	(GPIO_PORTC | GPIO_PF | 8)
#define PC9_PF_USBOTG_OE	(GPIO_PORTC | GPIO_PF | 9)
#define PC10_PF_USBOTG_TXDM	(GPIO_PORTC | GPIO_PF | 10)
#define PC11_PF_USBOTG_TXDP	(GPIO_PORTC | GPIO_PF | 11)
#define PC12_PF_USBOTG_RXDM	(GPIO_PORTC | GPIO_PF | 12)
#define PC13_PF_USBOTG_RXDP	(GPIO_PORTC | GPIO_PF | 13)
#define PC16_PF_SAP_FS		(GPIO_PORTC | GPIO_PF | 16)
#define PC17_PF_SAP_RXD		(GPIO_PORTC | GPIO_PF | 17)
#define PC18_PF_SAP_TXD		(GPIO_PORTC | GPIO_PF | 18)
#define PC19_PF_SAP_CLK		(GPIO_PORTC | GPIO_PF | 19)
#define PE0_PF_TEST_WB2		(GPIO_PORTE | GPIO_PF | 0)
#define PE1_PF_TEST_WB1		(GPIO_PORTE | GPIO_PF | 1)
#define PE2_PF_TEST_WB0		(GPIO_PORTE | GPIO_PF | 2)
#define PF1_PF_NFCE		(GPIO_PORTF | GPIO_PF | 1)
#define PF3_PF_NFCLE		(GPIO_PORTF | GPIO_PF | 3)
#define PF7_PF_NFIO0		(GPIO_PORTF | GPIO_PF | 7)
#define PF8_PF_NFIO1		(GPIO_PORTF | GPIO_PF | 8)
#define PF9_PF_NFIO2		(GPIO_PORTF | GPIO_PF | 9)
#define PF10_PF_NFIO3		(GPIO_PORTF | GPIO_PF | 10)
#define PF11_PF_NFIO4		(GPIO_PORTF | GPIO_PF | 11)
#define PF12_PF_NFIO5		(GPIO_PORTF | GPIO_PF | 12)
#define PF13_PF_NFIO6		(GPIO_PORTF | GPIO_PF | 13)
#define PF14_PF_NFIO7		(GPIO_PORTF | GPIO_PF | 14)
#define PF16_PF_RES		(GPIO_PORTF | GPIO_PF | 16)

/* Alternate GPIO pin functions */

#define PA5_AF_BMI_CLK_CS	(GPIO_PORTA | GPIO_AF | 5)
#define PA6_AF_BMI_D0		(GPIO_PORTA | GPIO_AF | 6)
#define PA7_AF_BMI_D1		(GPIO_PORTA | GPIO_AF | 7)
#define PA8_AF_BMI_D2		(GPIO_PORTA | GPIO_AF | 8)
#define PA9_AF_BMI_D3		(GPIO_PORTA | GPIO_AF | 9)
#define PA10_AF_BMI_D4		(GPIO_PORTA | GPIO_AF | 10)
#define PA11_AF_BMI_D5		(GPIO_PORTA | GPIO_AF | 11)
#define PA12_AF_BMI_D6		(GPIO_PORTA | GPIO_AF | 12)
#define PA13_AF_BMI_D7		(GPIO_PORTA | GPIO_AF | 13)
#define PA14_AF_BMI_D8		(GPIO_PORTA | GPIO_AF | 14)
#define PA15_AF_BMI_D9		(GPIO_PORTA | GPIO_AF | 15)
#define PA16_AF_BMI_D10		(GPIO_PORTA | GPIO_AF | 16)
#define PA17_AF_BMI_D11		(GPIO_PORTA | GPIO_AF | 17)
#define PA18_AF_BMI_D12		(GPIO_PORTA | GPIO_AF | 18)
#define PA19_AF_BMI_D13		(GPIO_PORTA | GPIO_AF | 19)
#define PA20_AF_BMI_D14		(GPIO_PORTA | GPIO_AF | 20)
#define PA21_AF_BMI_D15		(GPIO_PORTA | GPIO_AF | 21)
#define PA22_AF_BMI_READ_REQ	(GPIO_PORTA | GPIO_AF | 22)
#define PA23_AF_BMI_WRITE	(GPIO_PORTA | GPIO_AF | 23)
#define PA29_AF_BMI_RX_FULL	(GPIO_PORTA | GPIO_AF | 29)
#define PA30_AF_BMI_READ	(GPIO_PORTA | GPIO_AF | 30)

/* AIN GPIO pin functions */

#define PC14_AIN_SYS_CLK	(GPIO_PORTC | GPIO_AIN | GPIO_OUT | 14)
#define PD21_AIN_USBH2_FS	(GPIO_PORTD | GPIO_AIN | GPIO_OUT | 21)
#define PD22_AIN_USBH2_OE	(GPIO_PORTD | GPIO_AIN | GPIO_OUT | 22)
#define PD23_AIN_USBH2_TXDM	(GPIO_PORTD | GPIO_AIN | GPIO_OUT | 23)
#define PD24_AIN_USBH2_TXDP	(GPIO_PORTD | GPIO_AIN | GPIO_OUT | 24)
#define PE8_AIN_IR_TXD		(GPIO_PORTE | GPIO_AIN | GPIO_OUT | 8)
#define PF0_AIN_PC_RST		(GPIO_PORTF | GPIO_AIN | GPIO_OUT | 0)
#define PF1_AIN_PC_CE1		(GPIO_PORTF | GPIO_AIN | GPIO_OUT | 1)
#define PF2_AIN_PC_CE2		(GPIO_PORTF | GPIO_AIN | GPIO_OUT | 2)
#define PF3_AIN_PC_POE		(GPIO_PORTF | GPIO_AIN | GPIO_OUT | 3)
#define PF4_AIN_PC_OE		(GPIO_PORTF | GPIO_AIN | GPIO_OUT | 4)
#define PF5_AIN_PC_RW		(GPIO_PORTF | GPIO_AIN | GPIO_OUT | 5)

/* BIN GPIO pin functions */

#define PC14_BIN_SYS_CLK	(GPIO_PORTC | GPIO_BIN | GPIO_OUT | 14)
#define PD27_BIN_EXT_DMA_GRANT	(GPIO_PORTD | GPIO_BIN | GPIO_OUT | 27)

/* CIN GPIO pin functions */

#define PB26_CIN_USBH1_RXDAT	(GPIO_PORTB | GPIO_CIN | GPIO_OUT | 26)

/* AOUT GPIO pin functions */

#define PA29_AOUT_BMI_WAIT	(GPIO_PORTA | GPIO_AOUT | GPIO_IN | 29)
#define PD19_AOUT_USBH2_RXDM	(GPIO_PORTD | GPIO_AOUT | GPIO_IN | 19)
#define PD20_AOUT_USBH2_RXDP	(GPIO_PORTD | GPIO_AOUT | GPIO_IN | 20)
#define PD25_AOUT_EXT_DMAREQ	(GPIO_PORTD | GPIO_AOUT | GPIO_IN | 25)
#define PD26_AOUT_USBOTG_RXDAT	(GPIO_PORTD | GPIO_AOUT | GPIO_IN | 26)
#define PE9_AOUT_IR_RXD		(GPIO_PORTE | GPIO_AOUT | GPIO_IN | 9)
#define PF6_AOUT_PC_BVD2	(GPIO_PORTF | GPIO_AOUT | GPIO_IN | 6)
#define PF7_AOUT_PC_BVD1	(GPIO_PORTF | GPIO_AOUT | GPIO_IN | 7)
#define PF8_AOUT_PC_VS2		(GPIO_PORTF | GPIO_AOUT | GPIO_IN | 8)
#define PF9_AOUT_PC_VS1		(GPIO_PORTF | GPIO_AOUT | GPIO_IN | 9)
#define PF10_AOUT_PC_WP		(GPIO_PORTF | GPIO_AOUT | GPIO_IN | 10)
#define PF11_AOUT_PC_READY	(GPIO_PORTF | GPIO_AOUT | GPIO_IN | 11)
#define PF12_AOUT_PC_WAIT	(GPIO_PORTF | GPIO_AOUT | GPIO_IN | 12)
#define PF13_AOUT_PC_CD2	(GPIO_PORTF | GPIO_AOUT | GPIO_IN | 13)
#define PF14_AOUT_PC_CD1	(GPIO_PORTF | GPIO_AOUT | GPIO_IN | 14)

#endif /* ifndef __MACH_IOMUX_MX21_H__ */
