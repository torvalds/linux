/* arch/arm/mach-s3c2410/include/mach/regs-spi.h
 *
 * Copyright (c) 2004 Fetron GmbH
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * S3C2410 SPI register definition
*/

#ifndef __ASM_ARCH_REGS_SPI_H
#define __ASM_ARCH_REGS_SPI_H

#define S3C2410_SPI1	(0x20)
#define S3C2412_SPI1	(0x100)

#define S3C2410_SPCON	(0x00)

#define S3C2412_SPCON_RXFIFO_RB2	(0<<14)
#define S3C2412_SPCON_RXFIFO_RB4	(1<<14)
#define S3C2412_SPCON_RXFIFO_RB12	(2<<14)
#define S3C2412_SPCON_RXFIFO_RB14	(3<<14)
#define S3C2412_SPCON_TXFIFO_RB2	(0<<12)
#define S3C2412_SPCON_TXFIFO_RB4	(1<<12)
#define S3C2412_SPCON_TXFIFO_RB12	(2<<12)
#define S3C2412_SPCON_TXFIFO_RB14	(3<<12)
#define S3C2412_SPCON_RXFIFO_RESET	(1<<11) /* RxFIFO reset */
#define S3C2412_SPCON_TXFIFO_RESET	(1<<10) /* TxFIFO reset */
#define S3C2412_SPCON_RXFIFO_EN		(1<<9)  /* RxFIFO Enable */
#define S3C2412_SPCON_TXFIFO_EN		(1<<8)  /* TxFIFO Enable */

#define S3C2412_SPCON_DIRC_RX	  (1<<7)

#define S3C2410_SPCON_SMOD_DMA	  (2<<5)	/* DMA mode */
#define S3C2410_SPCON_SMOD_INT	  (1<<5)	/* interrupt mode */
#define S3C2410_SPCON_SMOD_POLL   (0<<5)	/* polling mode */
#define S3C2410_SPCON_ENSCK	  (1<<4)	/* Enable SCK */
#define S3C2410_SPCON_MSTR	  (1<<3)	/* Master/Slave select
						   0: slave, 1: master */
#define S3C2410_SPCON_CPOL_HIGH	  (1<<2)	/* Clock polarity select */
#define S3C2410_SPCON_CPOL_LOW	  (0<<2)	/* Clock polarity select */

#define S3C2410_SPCON_CPHA_FMTB	  (1<<1)	/* Clock Phase Select */
#define S3C2410_SPCON_CPHA_FMTA	  (0<<1)	/* Clock Phase Select */

#define S3C2410_SPCON_TAGD	  (1<<0)	/* Tx auto garbage data mode */


#define S3C2410_SPSTA	 (0x04)

#define S3C2412_SPSTA_RXFIFO_AE		(1<<11)
#define S3C2412_SPSTA_TXFIFO_AE		(1<<10)
#define S3C2412_SPSTA_RXFIFO_ERROR	(1<<9)
#define S3C2412_SPSTA_TXFIFO_ERROR	(1<<8)
#define S3C2412_SPSTA_RXFIFO_FIFO	(1<<7)
#define S3C2412_SPSTA_RXFIFO_EMPTY	(1<<6)
#define S3C2412_SPSTA_TXFIFO_NFULL	(1<<5)
#define S3C2412_SPSTA_TXFIFO_EMPTY	(1<<4)

#define S3C2410_SPSTA_DCOL	  (1<<2)	/* Data Collision Error */
#define S3C2410_SPSTA_MULD	  (1<<1)	/* Multi Master Error */
#define S3C2410_SPSTA_READY	  (1<<0)	/* Data Tx/Rx ready */
#define S3C2412_SPSTA_READY_ORG	  (1<<3)

#define S3C2410_SPPIN	 (0x08)

#define S3C2410_SPPIN_ENMUL	  (1<<2)	/* Multi Master Error detect */
#define S3C2410_SPPIN_RESERVED	  (1<<1)
#define S3C2410_SPPIN_KEEP	  (1<<0)	/* Master Out keep */

#define S3C2410_SPPRE	 (0x0C)
#define S3C2410_SPTDAT	 (0x10)
#define S3C2410_SPRDAT	 (0x14)

#define S3C2412_TXFIFO	 (0x18)
#define S3C2412_RXFIFO	 (0x18)
#define S3C2412_SPFIC	 (0x24)


#endif /* __ASM_ARCH_REGS_SPI_H */
