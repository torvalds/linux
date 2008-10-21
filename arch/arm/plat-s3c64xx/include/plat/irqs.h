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

/* Since the IRQ_EINT(x) are a linear mapping on current s3c64xx series
 * we just defined them as an IRQ_EINT(x) macro from S3C_IRQ_EINT_BASE
 * which we place after the pair of VICs. */

#define S3C_IRQ_EINT_BASE	S3C_IRQ(64)

#define S3C_EINT(x)	((x) + S3C_IRQ_EINT_BASE)

/* Define NR_IRQs here, machine specific can always re-define.
 * Currently the IRQ_EINT27 is the last one we can have. */

#define NR_IRQS	(S3C_EINT(27) + 1)

#endif /* __ASM_PLAT_S3C64XX_IRQS_H */

