/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * linux/arch/unicore32/include/asm/irq.h
 *
 * Code specific to PKUnity SoC and UniCore ISA
 *
 * Copyright (C) 2001-2010 GUAN Xue-tao
 */
#ifndef __UNICORE_IRQ_H__
#define __UNICORE_IRQ_H__

#include <asm-generic/irq.h>

#define	IRQ_GPIOLOW0		0x00
#define	IRQ_GPIOLOW1		0x01
#define	IRQ_GPIOLOW2		0x02
#define	IRQ_GPIOLOW3		0x03
#define	IRQ_GPIOLOW4		0x04
#define	IRQ_GPIOLOW5		0x05
#define	IRQ_GPIOLOW6		0x06
#define	IRQ_GPIOLOW7		0x07
#define IRQ_GPIOHIGH		0x08
#define IRQ_USB			0x09
#define IRQ_SDC			0x0a
#define IRQ_AC97		0x0b
#define IRQ_SATA		0x0c
#define IRQ_MME			0x0d
#define IRQ_PCI_BRIDGE		0x0e
#define	IRQ_DDR			0x0f
#define	IRQ_SPI			0x10
#define	IRQ_UNIGFX		0x11
#define	IRQ_I2C			0x11
#define	IRQ_UART1		0x12
#define	IRQ_UART0		0x13
#define IRQ_UMAL		0x14
#define IRQ_NAND		0x15
#define IRQ_PS2_KBD		0x16
#define IRQ_PS2_AUX		0x17
#define IRQ_DMA			0x18
#define IRQ_DMAERR		0x19
#define	IRQ_TIMER0		0x1a
#define	IRQ_TIMER1		0x1b
#define	IRQ_TIMER2		0x1c
#define	IRQ_TIMER3		0x1d
#define	IRQ_RTC			0x1e
#define	IRQ_RTCAlarm		0x1f

#define	IRQ_GPIO0		0x20
#define	IRQ_GPIO1		0x21
#define	IRQ_GPIO2		0x22
#define	IRQ_GPIO3		0x23
#define	IRQ_GPIO4		0x24
#define	IRQ_GPIO5		0x25
#define	IRQ_GPIO6		0x26
#define	IRQ_GPIO7		0x27
#define IRQ_GPIO8		0x28
#define IRQ_GPIO9		0x29
#define IRQ_GPIO10		0x2a
#define IRQ_GPIO11		0x2b
#define IRQ_GPIO12		0x2c
#define IRQ_GPIO13		0x2d
#define IRQ_GPIO14		0x2e
#define IRQ_GPIO15		0x2f
#define IRQ_GPIO16		0x30
#define IRQ_GPIO17		0x31
#define IRQ_GPIO18		0x32
#define IRQ_GPIO19		0x33
#define IRQ_GPIO20		0x34
#define IRQ_GPIO21		0x35
#define IRQ_GPIO22		0x36
#define IRQ_GPIO23		0x37
#define IRQ_GPIO24		0x38
#define IRQ_GPIO25		0x39
#define IRQ_GPIO26		0x3a
#define IRQ_GPIO27		0x3b

#ifdef CONFIG_ARCH_FPGA
#define IRQ_PCIINTA             IRQ_GPIOLOW2
#define IRQ_PCIINTB             IRQ_GPIOLOW1
#define IRQ_PCIINTC             IRQ_GPIOLOW0
#define IRQ_PCIINTD             IRQ_GPIOLOW6
#endif

#if defined(CONFIG_PUV3_DB0913) || defined(CONFIG_PUV3_NB0916)	\
	|| defined(CONFIG_PUV3_SMW0919)
#define IRQ_PCIINTA             IRQ_GPIOLOW1
#define IRQ_PCIINTB             IRQ_GPIOLOW2
#define IRQ_PCIINTC             IRQ_GPIOLOW3
#define IRQ_PCIINTD             IRQ_GPIOLOW4
#endif

#define IRQ_SD_CD               IRQ_GPIO6 /* falling or rising trigger */

#ifndef __ASSEMBLY__
struct pt_regs;

extern void asm_do_IRQ(unsigned int, struct pt_regs *);

#endif

#endif

