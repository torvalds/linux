/* arch/arm/mach-s3c2410/include/mach/irqs.h
 *
 * Copyright (c) 2003-2005 Simtec Electronics
 *   Ben Dooks <ben@simtec.co.uk>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/


#ifndef __ASM_ARCH_IRQS_H
#define __ASM_ARCH_IRQS_H __FILE__

/* we keep the first set of CPU IRQs out of the range of
 * the ISA space, so that the PC104 has them to itself
 * and we don't end up having to do horrible things to the
 * standard ISA drivers....
 */

#define S3C2410_CPUIRQ_OFFSET	 (16)

#define S3C2410_IRQ(x) ((x) + S3C2410_CPUIRQ_OFFSET)

/* main cpu interrupts */
#define IRQ_EINT0      S3C2410_IRQ(0)	    /* 16 */
#define IRQ_EINT1      S3C2410_IRQ(1)
#define IRQ_EINT2      S3C2410_IRQ(2)
#define IRQ_EINT3      S3C2410_IRQ(3)
#define IRQ_EINT4t7    S3C2410_IRQ(4)	    /* 20 */
#define IRQ_EINT8t23   S3C2410_IRQ(5)
#define IRQ_RESERVED6  S3C2410_IRQ(6)	    /* for s3c2410 */
#define IRQ_CAM        S3C2410_IRQ(6)	    /* for s3c2440,s3c2443 */
#define IRQ_BATT_FLT   S3C2410_IRQ(7)
#define IRQ_TICK       S3C2410_IRQ(8)	    /* 24 */
#define IRQ_WDT	       S3C2410_IRQ(9)	    /* WDT/AC97 for s3c2443 */
#define IRQ_TIMER0     S3C2410_IRQ(10)
#define IRQ_TIMER1     S3C2410_IRQ(11)
#define IRQ_TIMER2     S3C2410_IRQ(12)
#define IRQ_TIMER3     S3C2410_IRQ(13)
#define IRQ_TIMER4     S3C2410_IRQ(14)
#define IRQ_UART2      S3C2410_IRQ(15)
#define IRQ_LCD	       S3C2410_IRQ(16)	    /* 32 */
#define IRQ_DMA0       S3C2410_IRQ(17)	    /* IRQ_DMA for s3c2443 */
#define IRQ_DMA1       S3C2410_IRQ(18)
#define IRQ_DMA2       S3C2410_IRQ(19)
#define IRQ_DMA3       S3C2410_IRQ(20)
#define IRQ_SDI	       S3C2410_IRQ(21)
#define IRQ_SPI0       S3C2410_IRQ(22)
#define IRQ_UART1      S3C2410_IRQ(23)
#define IRQ_RESERVED24 S3C2410_IRQ(24)	    /* 40 */
#define IRQ_NFCON      S3C2410_IRQ(24)	    /* for s3c2440 */
#define IRQ_USBD       S3C2410_IRQ(25)
#define IRQ_USBH       S3C2410_IRQ(26)
#define IRQ_IIC	       S3C2410_IRQ(27)
#define IRQ_UART0      S3C2410_IRQ(28)	    /* 44 */
#define IRQ_SPI1       S3C2410_IRQ(29)
#define IRQ_RTC	       S3C2410_IRQ(30)
#define IRQ_ADCPARENT  S3C2410_IRQ(31)

/* interrupts generated from the external interrupts sources */
#define IRQ_EINT4      S3C2410_IRQ(32)	   /* 48 */
#define IRQ_EINT5      S3C2410_IRQ(33)
#define IRQ_EINT6      S3C2410_IRQ(34)
#define IRQ_EINT7      S3C2410_IRQ(35)
#define IRQ_EINT8      S3C2410_IRQ(36)
#define IRQ_EINT9      S3C2410_IRQ(37)
#define IRQ_EINT10     S3C2410_IRQ(38)
#define IRQ_EINT11     S3C2410_IRQ(39)
#define IRQ_EINT12     S3C2410_IRQ(40)
#define IRQ_EINT13     S3C2410_IRQ(41)
#define IRQ_EINT14     S3C2410_IRQ(42)
#define IRQ_EINT15     S3C2410_IRQ(43)
#define IRQ_EINT16     S3C2410_IRQ(44)
#define IRQ_EINT17     S3C2410_IRQ(45)
#define IRQ_EINT18     S3C2410_IRQ(46)
#define IRQ_EINT19     S3C2410_IRQ(47)
#define IRQ_EINT20     S3C2410_IRQ(48)	   /* 64 */
#define IRQ_EINT21     S3C2410_IRQ(49)
#define IRQ_EINT22     S3C2410_IRQ(50)
#define IRQ_EINT23     S3C2410_IRQ(51)

#define IRQ_EINT_BIT(x)	((x) - IRQ_EINT4 + 4)
#define IRQ_EINT(x)    (((x) >= 4) ? (IRQ_EINT4 + (x) - 4) : (IRQ_EINT0 + (x)))

#define IRQ_LCD_FIFO   S3C2410_IRQ(52)
#define IRQ_LCD_FRAME  S3C2410_IRQ(53)

/* IRQs for the interal UARTs, and ADC
 * these need to be ordered in number of appearance in the
 * SUBSRC mask register
*/

#define S3C2410_IRQSUB(x)	S3C2410_IRQ((x)+54)

#define IRQ_S3CUART_RX0		S3C2410_IRQSUB(0)	/* 70 */
#define IRQ_S3CUART_TX0		S3C2410_IRQSUB(1)
#define IRQ_S3CUART_ERR0	S3C2410_IRQSUB(2)

#define IRQ_S3CUART_RX1		S3C2410_IRQSUB(3)	/* 73 */
#define IRQ_S3CUART_TX1		S3C2410_IRQSUB(4)
#define IRQ_S3CUART_ERR1	S3C2410_IRQSUB(5)

#define IRQ_S3CUART_RX2		S3C2410_IRQSUB(6)	/* 76 */
#define IRQ_S3CUART_TX2		S3C2410_IRQSUB(7)
#define IRQ_S3CUART_ERR2	S3C2410_IRQSUB(8)

#define IRQ_TC			S3C2410_IRQSUB(9)
#define IRQ_ADC			S3C2410_IRQSUB(10)

/* extra irqs for s3c2412 */

#define IRQ_S3C2412_CFSDI	S3C2410_IRQ(21)

#define IRQ_S3C2412_SDI		S3C2410_IRQSUB(13)
#define IRQ_S3C2412_CF		S3C2410_IRQSUB(14)


#define IRQ_S3C2416_EINT8t15	S3C2410_IRQ(5)
#define IRQ_S3C2416_DMA		S3C2410_IRQ(17)
#define IRQ_S3C2416_UART3	S3C2410_IRQ(18)
#define IRQ_S3C2416_SDI1	S3C2410_IRQ(20)
#define IRQ_S3C2416_SDI0	S3C2410_IRQ(21)

#define IRQ_S3C2416_LCD2	S3C2410_IRQSUB(15)
#define IRQ_S3C2416_LCD3	S3C2410_IRQSUB(16)
#define IRQ_S3C2416_LCD4	S3C2410_IRQSUB(17)
#define IRQ_S3C2416_DMA0	S3C2410_IRQSUB(18)
#define IRQ_S3C2416_DMA1	S3C2410_IRQSUB(19)
#define IRQ_S3C2416_DMA2	S3C2410_IRQSUB(20)
#define IRQ_S3C2416_DMA3	S3C2410_IRQSUB(21)
#define IRQ_S3C2416_DMA4	S3C2410_IRQSUB(22)
#define IRQ_S3C2416_DMA5	S3C2410_IRQSUB(23)
#define IRQ_S32416_WDT		S3C2410_IRQSUB(27)
#define IRQ_S32416_AC97		S3C2410_IRQSUB(28)


/* extra irqs for s3c2440 */

#define IRQ_S3C2440_CAM_C	S3C2410_IRQSUB(11)	/* S3C2443 too */
#define IRQ_S3C2440_CAM_P	S3C2410_IRQSUB(12)	/* S3C2443 too */
#define IRQ_S3C2440_WDT		S3C2410_IRQSUB(13)
#define IRQ_S3C2440_AC97	S3C2410_IRQSUB(14)

/* irqs for s3c2443 */

#define IRQ_S3C2443_DMA		S3C2410_IRQ(17)		/* IRQ_DMA1 */
#define IRQ_S3C2443_UART3	S3C2410_IRQ(18)		/* IRQ_DMA2 */
#define IRQ_S3C2443_CFCON	S3C2410_IRQ(19)		/* IRQ_DMA3 */
#define IRQ_S3C2443_HSMMC	S3C2410_IRQ(20)		/* IRQ_SDI */
#define IRQ_S3C2443_NAND	S3C2410_IRQ(24)		/* reserved */

#define IRQ_S3C2416_HSMMC0	S3C2410_IRQ(21)		/* S3C2416/S3C2450 */

#define IRQ_HSMMC0		IRQ_S3C2443_HSMMC
#define IRQ_HSMMC1		IRQ_S3C2416_HSMMC0

#define IRQ_S3C2443_LCD1	S3C2410_IRQSUB(14)
#define IRQ_S3C2443_LCD2	S3C2410_IRQSUB(15)
#define IRQ_S3C2443_LCD3	S3C2410_IRQSUB(16)
#define IRQ_S3C2443_LCD4	S3C2410_IRQSUB(17)

#define IRQ_S3C2443_DMA0	S3C2410_IRQSUB(18)
#define IRQ_S3C2443_DMA1	S3C2410_IRQSUB(19)
#define IRQ_S3C2443_DMA2	S3C2410_IRQSUB(20)
#define IRQ_S3C2443_DMA3	S3C2410_IRQSUB(21)
#define IRQ_S3C2443_DMA4	S3C2410_IRQSUB(22)
#define IRQ_S3C2443_DMA5	S3C2410_IRQSUB(23)

/* UART3 */
#define IRQ_S3C2443_RX3		S3C2410_IRQSUB(24)
#define IRQ_S3C2443_TX3		S3C2410_IRQSUB(25)
#define IRQ_S3C2443_ERR3	S3C2410_IRQSUB(26)

#define IRQ_S3C2443_WDT		S3C2410_IRQSUB(27)
#define IRQ_S3C2443_AC97	S3C2410_IRQSUB(28)

#if defined(CONFIG_CPU_S3C2443) || defined(CONFIG_CPU_S3C2416)
#define NR_IRQS (IRQ_S3C2443_AC97+1)
#else
#define NR_IRQS (IRQ_S3C2440_AC97+1)
#endif

/* compatibility define. */
#define IRQ_UART3		IRQ_S3C2443_UART3
#define IRQ_S3CUART_RX3		IRQ_S3C2443_RX3
#define IRQ_S3CUART_TX3		IRQ_S3C2443_TX3
#define IRQ_S3CUART_ERR3	IRQ_S3C2443_ERR3

#define IRQ_LCD_VSYNC		IRQ_S3C2443_LCD3
#define IRQ_LCD_SYSTEM		IRQ_S3C2443_LCD2

#ifdef CONFIG_CPU_S3C2440
#define IRQ_S3C244x_AC97 IRQ_S3C2440_AC97
#else
#define IRQ_S3C244x_AC97 IRQ_S3C2443_AC97
#endif

/* Our FIQs are routable from IRQ_EINT0 to IRQ_ADCPARENT */
#define FIQ_START		IRQ_EINT0

#endif /* __ASM_ARCH_IRQ_H */
