/*
 *  arch/arm/mach-pxa/include/mach/irqs.h
 *
 *  Author:	Nicolas Pitre
 *  Created:	Jun 15, 2001
 *  Copyright:	MontaVista Software Inc.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */
#ifndef __ASM_MACH_IRQS_H
#define __ASM_MACH_IRQS_H

#ifdef CONFIG_PXA_HAVE_ISA_IRQS
#define PXA_ISA_IRQ(x)	(x)
#define PXA_ISA_IRQ_NUM	(16)
#else
#define PXA_ISA_IRQ_NUM	(0)
#endif

#define PXA_IRQ(x)	(PXA_ISA_IRQ_NUM + (x))

#define IRQ_SSP3	PXA_IRQ(0)	/* SSP3 service request */
#define IRQ_MSL		PXA_IRQ(1)	/* MSL Interface interrupt */
#define IRQ_USBH2	PXA_IRQ(2)	/* USB Host interrupt 1 (OHCI,PXA27x) */
#define IRQ_USBH1	PXA_IRQ(3)	/* USB Host interrupt 2 (non-OHCI,PXA27x) */
#define IRQ_KEYPAD	PXA_IRQ(4)	/* Key pad controller */
#define IRQ_MEMSTK	PXA_IRQ(5)	/* Memory Stick interrupt (PXA27x) */
#define IRQ_ACIPC0	PXA_IRQ(5)	/* AP-CP Communication (PXA930) */
#define IRQ_PWRI2C	PXA_IRQ(6)	/* Power I2C interrupt */
#define IRQ_HWUART	PXA_IRQ(7)	/* HWUART Transmit/Receive/Error (PXA26x) */
#define IRQ_OST_4_11	PXA_IRQ(7)	/* OS timer 4-11 matches (PXA27x) */
#define	IRQ_GPIO0	PXA_IRQ(8)	/* GPIO0 Edge Detect */
#define	IRQ_GPIO1	PXA_IRQ(9)	/* GPIO1 Edge Detect */
#define	IRQ_GPIO_2_x	PXA_IRQ(10)	/* GPIO[2-x] Edge Detect */
#define	IRQ_USB		PXA_IRQ(11)	/* USB Service */
#define	IRQ_PMU		PXA_IRQ(12)	/* Performance Monitoring Unit */
#define	IRQ_I2S		PXA_IRQ(13)	/* I2S Interrupt (PXA27x) */
#define IRQ_SSP4	PXA_IRQ(13)	/* SSP4 service request (PXA3xx) */
#define	IRQ_AC97	PXA_IRQ(14)	/* AC97 Interrupt */
#define IRQ_ASSP	PXA_IRQ(15)	/* Audio SSP Service Request (PXA25x) */
#define IRQ_USIM	PXA_IRQ(15)     /* Smart Card interface interrupt (PXA27x) */
#define IRQ_NSSP	PXA_IRQ(16)	/* Network SSP Service Request (PXA25x) */
#define IRQ_SSP2	PXA_IRQ(16)	/* SSP2 interrupt (PXA27x) */
#define	IRQ_LCD		PXA_IRQ(17)	/* LCD Controller Service Request */
#define	IRQ_I2C		PXA_IRQ(18)	/* I2C Service Request */
#define	IRQ_ICP		PXA_IRQ(19)	/* ICP Transmit/Receive/Error */
#define IRQ_ACIPC2	PXA_IRQ(19)	/* AP-CP Communication (PXA930) */
#define	IRQ_STUART	PXA_IRQ(20)	/* STUART Transmit/Receive/Error */
#define	IRQ_BTUART	PXA_IRQ(21)	/* BTUART Transmit/Receive/Error */
#define	IRQ_FFUART	PXA_IRQ(22)	/* FFUART Transmit/Receive/Error*/
#define	IRQ_MMC		PXA_IRQ(23)	/* MMC Status/Error Detection */
#define	IRQ_SSP		PXA_IRQ(24)	/* SSP Service Request */
#define	IRQ_DMA 	PXA_IRQ(25)	/* DMA Channel Service Request */
#define	IRQ_OST0 	PXA_IRQ(26)	/* OS Timer match 0 */
#define	IRQ_OST1 	PXA_IRQ(27)	/* OS Timer match 1 */
#define	IRQ_OST2 	PXA_IRQ(28)	/* OS Timer match 2 */
#define	IRQ_OST3 	PXA_IRQ(29)	/* OS Timer match 3 */
#define	IRQ_RTC1Hz	PXA_IRQ(30)	/* RTC HZ Clock Tick */
#define	IRQ_RTCAlrm	PXA_IRQ(31)	/* RTC Alarm */

#define IRQ_TPM		PXA_IRQ(32)	/* TPM interrupt */
#define IRQ_CAMERA	PXA_IRQ(33)	/* Camera Interface */
#define IRQ_CIR		PXA_IRQ(34)	/* Consumer IR */
#define IRQ_COMM_WDT	PXA_IRQ(35) 	/* Comm WDT interrupt */
#define IRQ_TSI		PXA_IRQ(36)	/* Touch Screen Interface (PXA320) */
#define IRQ_ENHROT	PXA_IRQ(37)	/* Enhanced Rotary (PXA930) */
#define IRQ_USIM2	PXA_IRQ(38)	/* USIM2 Controller */
#define IRQ_GCU		PXA_IRQ(39)	/* Graphics Controller (PXA3xx) */
#define IRQ_ACIPC1	PXA_IRQ(40)	/* AP-CP Communication (PXA930) */
#define IRQ_MMC2	PXA_IRQ(41)	/* MMC2 Controller */
#define IRQ_TRKBALL	PXA_IRQ(43)	/* Track Ball (PXA930) */
#define IRQ_1WIRE	PXA_IRQ(44)	/* 1-Wire Controller */
#define IRQ_NAND	PXA_IRQ(45)	/* NAND Controller */
#define IRQ_USB2	PXA_IRQ(46)	/* USB 2.0 Device Controller */
#define IRQ_WAKEUP0	PXA_IRQ(49)	/* EXT_WAKEUP0 */
#define IRQ_WAKEUP1	PXA_IRQ(50)	/* EXT_WAKEUP1 */
#define IRQ_DMEMC	PXA_IRQ(51)	/* Dynamic Memory Controller */
#define IRQ_MMC3	PXA_IRQ(55)	/* MMC3 Controller (PXA310) */

#define IRQ_U2O		PXA_IRQ(64)	/* USB OTG 2.0 Controller (PXA935) */
#define IRQ_U2H		PXA_IRQ(65)	/* USB Host 2.0 Controller (PXA935) */
#define IRQ_PXA935_MMC0	PXA_IRQ(72)	/* MMC0 Controller (PXA935) */
#define IRQ_PXA935_MMC1	PXA_IRQ(73)	/* MMC1 Controller (PXA935) */
#define IRQ_PXA935_MMC2	PXA_IRQ(74)	/* MMC2 Controller (PXA935) */
#define IRQ_PXA955_MMC3	PXA_IRQ(75)	/* MMC3 Controller (PXA955) */
#define IRQ_U2P		PXA_IRQ(93)	/* USB PHY D+/D- Lines (PXA935) */

#define PXA_GPIO_IRQ_BASE	PXA_IRQ(96)
#define PXA_GPIO_IRQ_NUM	(192)

#define GPIO_2_x_TO_IRQ(x)	(PXA_GPIO_IRQ_BASE + (x))
#define IRQ_GPIO(x)	(((x) < 2) ? (IRQ_GPIO0 + (x)) : GPIO_2_x_TO_IRQ(x))

/*
 * The following interrupts are for board specific purposes. Since
 * the kernel can only run on one machine at a time, we can re-use
 * these.
 * By default, no board IRQ is reserved. It should be finished in
 * custom board since sparse IRQ is already enabled.
 */
#define IRQ_BOARD_START		(PXA_GPIO_IRQ_BASE + PXA_GPIO_IRQ_NUM)

#define NR_IRQS			(IRQ_BOARD_START)

#ifndef __ASSEMBLY__
struct irq_data;
struct pt_regs;

void pxa_mask_irq(struct irq_data *);
void pxa_unmask_irq(struct irq_data *);
void icip_handle_irq(struct pt_regs *);
void ichp_handle_irq(struct pt_regs *);

void pxa_init_irq(int irq_nr, int (*set_wake)(struct irq_data *, unsigned int));
#endif

#endif /* __ASM_MACH_IRQS_H */
