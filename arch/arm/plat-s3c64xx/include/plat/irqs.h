/* linux/arch/arm/plat-s3c64xx/include/mach/irqs.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C64XX - Common IRQ support
 */

#ifndef __ASM_PLAT_S3C64XX_IRQS_H
#define __ASM_PLAT_S3C64XX_IRQS_H __FILE__

/* we keep the first set of CPU IRQs out of the range of
 * the ISA space, so that the PC104 has them to itself
 * and we don't end up having to do horrible things to the
 * standard ISA drivers....
 *
 * note, since we're using the VICs, our start must be a
 * mulitple of 32 to allow the common code to work
 */

#define S3C_IRQ_OFFSET	(32)

#define S3C_IRQ(x)	((x) + S3C_IRQ_OFFSET)

#define S3C_VIC0_BASE	S3C_IRQ(0)
#define S3C_VIC1_BASE	S3C_IRQ(32)

/* UART interrupts, each UART has 4 intterupts per channel so
 * use the space between the ISA and S3C main interrupts. Note, these
 * are not in the same order as the S3C24XX series! */

#define IRQ_S3CUART_BASE0	(16)
#define IRQ_S3CUART_BASE1	(20)
#define IRQ_S3CUART_BASE2	(24)
#define IRQ_S3CUART_BASE3	(28)

#define UART_IRQ_RXD		(0)
#define UART_IRQ_ERR		(1)
#define UART_IRQ_TXD		(2)
#define UART_IRQ_MODEM		(3)

#define IRQ_S3CUART_RX0		(IRQ_S3CUART_BASE0 + UART_IRQ_RXD)
#define IRQ_S3CUART_TX0		(IRQ_S3CUART_BASE0 + UART_IRQ_TXD)
#define IRQ_S3CUART_ERR0	(IRQ_S3CUART_BASE0 + UART_IRQ_ERR)

#define IRQ_S3CUART_RX1		(IRQ_S3CUART_BASE1 + UART_IRQ_RXD)
#define IRQ_S3CUART_TX1		(IRQ_S3CUART_BASE1 + UART_IRQ_TXD)
#define IRQ_S3CUART_ERR1	(IRQ_S3CUART_BASE1 + UART_IRQ_ERR)

#define IRQ_S3CUART_RX2		(IRQ_S3CUART_BASE2 + UART_IRQ_RXD)
#define IRQ_S3CUART_TX2		(IRQ_S3CUART_BASE2 + UART_IRQ_TXD)
#define IRQ_S3CUART_ERR2	(IRQ_S3CUART_BASE2 + UART_IRQ_ERR)

#define IRQ_S3CUART_RX3		(IRQ_S3CUART_BASE3 + UART_IRQ_RXD)
#define IRQ_S3CUART_TX3		(IRQ_S3CUART_BASE3 + UART_IRQ_TXD)
#define IRQ_S3CUART_ERR3	(IRQ_S3CUART_BASE3 + UART_IRQ_ERR)

/* VIC based IRQs */

#define S3C64XX_IRQ_VIC0(x)	(S3C_VIC0_BASE + (x))
#define S3C64XX_IRQ_VIC1(x)	(S3C_VIC1_BASE + (x))

/* VIC0 */

#define IRQ_EINT0_3		S3C64XX_IRQ_VIC0(0)
#define IRQ_EINT4_11		S3C64XX_IRQ_VIC0(1)
#define IRQ_RTC_TIC		S3C64XX_IRQ_VIC0(2)
#define IRQ_CAMIF_C		S3C64XX_IRQ_VIC0(3)
#define IRQ_CAMIF_P		S3C64XX_IRQ_VIC0(4)
#define IRQ_CAMIF_MC		S3C64XX_IRQ_VIC0(5)
#define IRQ_S3C6410_IIS		S3C64XX_IRQ_VIC0(6)
#define IRQ_S3C6400_CAMIF_MP	S3C64XX_IRQ_VIC0(6)
#define IRQ_CAMIF_WE_C		S3C64XX_IRQ_VIC0(7)
#define IRQ_S3C6410_G3D		S3C64XX_IRQ_VIC0(8)
#define IRQ_S3C6400_CAMIF_WE_P	S3C64XX_IRQ_VIC0(8)
#define IRQ_POST0		S3C64XX_IRQ_VIC0(9)
#define IRQ_ROTATOR		S3C64XX_IRQ_VIC0(10)
#define IRQ_2D			S3C64XX_IRQ_VIC0(11)
#define IRQ_TVENC		S3C64XX_IRQ_VIC0(12)
#define IRQ_SCALER		S3C64XX_IRQ_VIC0(13)
#define IRQ_BATF		S3C64XX_IRQ_VIC0(14)
#define IRQ_JPEG		S3C64XX_IRQ_VIC0(15)
#define IRQ_MFC			S3C64XX_IRQ_VIC0(16)
#define IRQ_SDMA0		S3C64XX_IRQ_VIC0(17)
#define IRQ_SDMA1		S3C64XX_IRQ_VIC0(18)
#define IRQ_ARM_DMAERR		S3C64XX_IRQ_VIC0(19)
#define IRQ_ARM_DMA		S3C64XX_IRQ_VIC0(20)
#define IRQ_ARM_DMAS		S3C64XX_IRQ_VIC0(21)
#define IRQ_KEYPAD		S3C64XX_IRQ_VIC0(22)
#define IRQ_TIMER0		S3C64XX_IRQ_VIC0(23)
#define IRQ_TIMER1		S3C64XX_IRQ_VIC0(24)
#define IRQ_TIMER2		S3C64XX_IRQ_VIC0(25)
#define IRQ_WDT			S3C64XX_IRQ_VIC0(26)
#define IRQ_TIMER3		S3C64XX_IRQ_VIC0(27)
#define IRQ_TIMER4		S3C64XX_IRQ_VIC0(28)
#define IRQ_LCD_FIFO		S3C64XX_IRQ_VIC0(29)
#define IRQ_LCD_VSYNC		S3C64XX_IRQ_VIC0(30)
#define IRQ_LCD_SYSTEM		S3C64XX_IRQ_VIC0(31)

/* VIC1 */

#define IRQ_EINT12_19		S3C64XX_IRQ_VIC1(0)
#define IRQ_EINT20_27		S3C64XX_IRQ_VIC1(1)
#define IRQ_PCM0		S3C64XX_IRQ_VIC1(2)
#define IRQ_PCM1		S3C64XX_IRQ_VIC1(3)
#define IRQ_AC97		S3C64XX_IRQ_VIC1(4)
#define IRQ_UART0		S3C64XX_IRQ_VIC1(5)
#define IRQ_UART1		S3C64XX_IRQ_VIC1(6)
#define IRQ_UART2		S3C64XX_IRQ_VIC1(7)
#define IRQ_UART3		S3C64XX_IRQ_VIC1(8)
#define IRQ_DMA0		S3C64XX_IRQ_VIC1(9)
#define IRQ_DMA1		S3C64XX_IRQ_VIC1(10)
#define IRQ_ONENAND0		S3C64XX_IRQ_VIC1(11)
#define IRQ_ONENAND1		S3C64XX_IRQ_VIC1(12)
#define IRQ_NFC			S3C64XX_IRQ_VIC1(13)
#define IRQ_CFCON		S3C64XX_IRQ_VIC1(14)
#define IRQ_UHOST		S3C64XX_IRQ_VIC1(15)
#define IRQ_SPI0		S3C64XX_IRQ_VIC1(16)
#define IRQ_SPI1		S3C64XX_IRQ_VIC1(17)
#define IRQ_IIC			S3C64XX_IRQ_VIC1(18)
#define IRQ_HSItx		S3C64XX_IRQ_VIC1(19)
#define IRQ_HSIrx		S3C64XX_IRQ_VIC1(20)
#define IRQ_RESERVED		S3C64XX_IRQ_VIC1(21)
#define IRQ_MSM			S3C64XX_IRQ_VIC1(22)
#define IRQ_HOSTIF		S3C64XX_IRQ_VIC1(23)
#define IRQ_HSMMC0		S3C64XX_IRQ_VIC1(24)
#define IRQ_HSMMC1		S3C64XX_IRQ_VIC1(25)
#define IRQ_HSMMC2		IRQ_SPI1	/* shared with SPI1 */
#define IRQ_OTG			S3C64XX_IRQ_VIC1(26)
#define IRQ_IRDA		S3C64XX_IRQ_VIC1(27)
#define IRQ_RTC_ALARM		S3C64XX_IRQ_VIC1(28)
#define IRQ_SEC			S3C64XX_IRQ_VIC1(29)
#define IRQ_PENDN		S3C64XX_IRQ_VIC1(30)
#define IRQ_TC			IRQ_PENDN
#define IRQ_ADC			S3C64XX_IRQ_VIC1(31)

/* Since the IRQ_EINT(x) are a linear mapping on current s3c64xx series
 * we just defined them as an IRQ_EINT(x) macro from S3C_IRQ_EINT_BASE
 * which we place after the pair of VICs. */

#define S3C_IRQ_EINT_BASE	S3C_IRQ(64)

#define S3C_EINT(x)	((x) + S3C_IRQ_EINT_BASE)

/* Define NR_IRQs here, machine specific can always re-define.
 * Currently the IRQ_EINT27 is the last one we can have. */

#define NR_IRQS	(S3C_EINT(27) + 1)

#endif /* __ASM_PLAT_S3C64XX_IRQS_H */

