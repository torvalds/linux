/*
 * IRQ definitions for TCC8xxx
 *
 * Copyright (C) 2008-2009 Telechips
 * Copyright (C) 2009 Hans J. Koch <hjk@linutronix.de>
 *
 * Licensed under the terms of the GPL v2.
 *
 */

#ifndef __ASM_ARCH_TCC_IRQS_H
#define __ASM_ARCH_TCC_IRQS_H

#define NR_IRQS 64

/* PIC0 interrupts */
#define INT_ADMA1	0
#define INT_BDMA	1
#define INT_ADMA0	2
#define INT_GDMA1	3
#define INT_I2S0RX	4
#define INT_I2S0TX	5
#define INT_TC		6
#define INT_UART0	7
#define INT_USBD	8
#define INT_SPI0TX	9
#define INT_UDMA	10
#define INT_LIRQ	11
#define INT_GDMA2	12
#define INT_GDMA0	13
#define INT_TC32	14
#define INT_LCD		15
#define INT_ADC		16
#define INT_I2C		17
#define INT_RTCP	18
#define INT_RTCA	19
#define INT_NFC		20
#define INT_SD0		21
#define INT_GSB0	22
#define INT_PK		23
#define INT_USBH0	24
#define INT_USBH1	25
#define INT_G2D		26
#define INT_ECC		27
#define INT_SPI0RX	28
#define INT_UART1	29
#define INT_MSCL	30
#define INT_GSB1	31
/* PIC1 interrupts */
#define INT_E0		32
#define INT_E1		33
#define INT_E2		34
#define INT_E3		35
#define INT_E4		36
#define INT_E5		37
#define INT_E6		38
#define INT_E7		39
#define INT_UART2	40
#define INT_UART3	41
#define INT_SPI1TX	42
#define INT_SPI1RX	43
#define INT_GSB2	44
#define INT_SPDIF	45
#define INT_CDIF	46
#define INT_VBON	47
#define INT_VBOFF	48
#define INT_SD1		49
#define INT_UART4	50
#define INT_GDMA3	51
#define INT_I2S1RX	52
#define INT_I2S1TX	53
#define INT_CAN0	54
#define INT_CAN1	55
#define INT_GSB3	56
#define INT_KRST	57
#define INT_UNUSED	58
#define INT_SD0D3	59
#define INT_SD1D3	60
#define INT_GPS0	61
#define INT_GPS1	62
#define INT_GPS2	63

#endif  /* ASM_ARCH_TCC_IRQS_H */
