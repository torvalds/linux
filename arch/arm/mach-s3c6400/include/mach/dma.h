/* linux/arch/arm/mach-s3c6400/include/mach/dma.h
 *
 * Copyright 2008 Openmoko, Inc.
 * Copyright 2008 Simtec Electronics
 *      Ben Dooks <ben@simtec.co.uk>
 *      http://armlinux.simtec.co.uk/
 *
 * S3C6400 - DMA support
 */

#ifndef __ASM_ARCH_DMA_H
#define __ASM_ARCH_DMA_H __FILE__

#define S3C_DMA_CHANNELS	(16)

/* see mach-s3c2410/dma.h for notes on dma channel numbers */

/* Note, for the S3C64XX architecture we keep the DMACH_
 * defines in the order they are allocated to [S]DMA0/[S]DMA1
 * so that is easy to do DHACH_ -> DMA controller conversion
 */
enum dma_ch {
	/* DMA0/SDMA0 */
	DMACH_UART0 = 0,
	DMACH_UART0_SRC2,
	DMACH_UART1,
	DMACH_UART1_SRC2,
	DMACH_UART2,
	DMACH_UART2_SRC2,
	DMACH_UART3,
	DMACH_UART3_SRC2,
	DMACH_PCM0_TX,
	DMACH_PCM0_RX,
	DMACH_I2S0_OUT,
	DMACH_I2S0_IN,
	DMACH_SPI0_TX,
	DMACH_SPI0_RX,
	DMACH_HSI_I2SV40_TX,
	DMACH_HSI_I2SV40_RX,

	/* DMA1/SDMA1 */
	DMACH_PCM1_TX = 16,
	DMACH_PCM1_RX,
	DMACH_I2S1_OUT,
	DMACH_I2S1_IN,
	DMACH_SPI1_TX,
	DMACH_SPI1_RX,
	DMACH_AC97_PCMOUT,
	DMACH_AC97_PCMIN,
	DMACH_AC97_MICIN,
	DMACH_PWM,
	DMACH_IRDA,
	DMACH_EXTERNAL,
	DMACH_RES1,
	DMACH_RES2,
	DMACH_SECURITY_RX,	/* SDMA1 only */
	DMACH_SECURITY_TX,	/* SDMA1 only */
	DMACH_MAX		/* the end */
};

static __inline__ int s3c_dma_has_circular(void)
{
	/* we will be supporting ciruclar buffers as soon as we have DMA
	 * engine support.
	 */
	return 1;
}

#define S3C2410_DMAF_CIRCULAR		(1 << 0)

#include <plat/dma.h>

#endif /* __ASM_ARCH_IRQ_H */
