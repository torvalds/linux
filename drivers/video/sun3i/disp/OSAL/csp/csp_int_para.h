/*
*********************************************************************************************************
*											        eBase
*						                the Abstract of Hardware
*									           the OAL of CSP
*
*						        (c) Copyright 2006-2010, AW China
*											All	Rights Reserved
*
* File    	: 	csp.c
* Date		:	2010-05-26
* By      	: 	holigun
* Version 	: 	V1.00
*********************************************************************************************************
*/

#ifndef	_CSP_INTC_PARA_H_
#define	_CSP_INTC_PARA_H_


#define CSP_IRQ_SOUCE_COUNT     (64)

//=======================================================================================================

#define INTC_IRQNO_FIQ           0
#define INTC_IRQNO_UART0         1
#define INTC_IRQNO_UART1         2
#define INTC_IRQNO_UART2         3
#define INTC_IRQNO_UART3         4
#define INTC_IRQNO_CAN           5
#define INTC_IRQNO_IR            6
#define INTC_IRQNO_TWI0          7
#define INTC_IRQNO_TWI1          8

#define INTC_IRQNO_SPI0          10
#define INTC_IRQNO_SPI1          11
#define INTC_IRQNO_SPI2          12
#define INTC_IRQNO_SPDIF         13
#define INTC_IRQNO_AC97          14
#define INTC_IRQNO_TS            15
#define INTC_IRQNO_IIS           16

#define INTC_IRQNO_UART4         17
#define INTC_IRQNO_UART5         18
#define INTC_IRQNO_UART6         19
#define INTC_IRQNO_UART7         20

#define INTC_IRQNO_PS20          21
#define INTC_IRQNO_TIMER0        22
#define INTC_IRQNO_TIMER1        23
#define INTC_IRQNO_TIMER2345     24
#define INTC_IRQNO_ALARM         25
#define INTC_IRQNO_PS21          26
#define INTC_IRQNO_DMA           27
#define INTC_IRQNO_PIO           28
#define INTC_IRQNO_TP            29

#define INTC_IRQNO_ADDA          30
#define INTC_IRQNO_LRADC         31
#define INTC_IRQNO_SDMMC0        32
#define INTC_IRQNO_SDMMC1        33
#define INTC_IRQNO_SDMMC2        34
#define INTC_IRQNO_SDMMC3        35
#define INTC_IRQNO_MS            36
#define INTC_IRQNO_NAND          37
#define INTC_IRQNO_USB0          38
#define INTC_IRQNO_USB1          39
#define INTC_IRQNO_USB2          40

#define INTC_IRQNO_AUDIO_IMAGE   41

#define INTC_IRQNO_CSI0          42
#define INTC_IRQNO_TVENC         43
#define INTC_IRQNO_TCON0         44
#define INTC_IRQNO_DE_SCALE0     45
#define INTC_IRQNO_DE_IMAGE0     46
#define INTC_IRQNO_DE_MIX        47
#define INTC_IRQNO_VE            48

#define INTC_IRQNO_SS            50
#define INTC_IRQNO_EMAC          51
#define INTC_IRQNO_SOFTIRQ0      52
#define INTC_IRQNO_SMC           53

#define INTC_IRQNO_TCON1         54
#define INTC_IRQNO_DE_SCALE1     55
#define INTC_IRQNO_DE_IMAGE1     56

#define INTC_IRQNO_SYS_EX        57
#define INTC_IRQNO_CSI1          58
#define INTC_IRQNO_ATA           59
#define INTC_IRQNO_GPS           60
#define INTC_IRQNO_CE            61
#define INTC_IRQNO_TWI2          62
#define INTC_IRQNO_KEYPAD        63

//=======================================================================================================

#define INTC_NMI_TRIGGER_MOD_LOW_LEVEL		0x00
#define INTC_NMI_TRIGGER_MOD_HIGH_LEVEL		0x01
#define INTC_NMI_TRIGGER_MOD_NEGATIVE_EDGE	0x02
#define INTC_NMI_TRIGGER_MOD_POSITIVE_EDGE	0x03
#define INTC_NMI_TRIGGER_MOD_MAX			0x03
//=======================================================================================================





#endif	//_CSP_INTC_PARA_H_
