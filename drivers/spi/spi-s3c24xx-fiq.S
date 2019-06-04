/* SPDX-License-Identifier: GPL-2.0-only */
/* linux/drivers/spi/spi_s3c24xx_fiq.S
 *
 * Copyright 2009 Simtec Electronics
 *	Ben Dooks <ben@simtec.co.uk>
 *
 * S3C24XX SPI - FIQ pseudo-DMA transfer code
*/

#include <linux/linkage.h>
#include <asm/assembler.h>

#include <mach/map.h>
#include <mach/regs-irq.h>
#include <plat/regs-spi.h>

#include "spi-s3c24xx-fiq.h"

	.text

	@ entry to these routines is as follows, with the register names
	@ defined in fiq.h so that they can be shared with the C files which
	@ setup the calling registers.
	@
	@ fiq_rirq	The base of the IRQ registers to find S3C2410_SRCPND
	@ fiq_rtmp	Temporary register to hold tx/rx data
	@ fiq_rspi	The base of the SPI register block
	@ fiq_rtx	The tx buffer pointer
	@ fiq_rrx	The rx buffer pointer
	@ fiq_rcount	The number of bytes to move

	@ each entry starts with a word entry of how long it is
	@ and an offset to the irq acknowledgment word

ENTRY(s3c24xx_spi_fiq_rx)
s3c24xx_spi_fix_rx:
	.word	fiq_rx_end - fiq_rx_start
	.word	fiq_rx_irq_ack - fiq_rx_start
fiq_rx_start:
	ldr	fiq_rtmp, fiq_rx_irq_ack
	str	fiq_rtmp, [ fiq_rirq, # S3C2410_SRCPND - S3C24XX_VA_IRQ ]

	ldrb	fiq_rtmp, [ fiq_rspi, #  S3C2410_SPRDAT ]
	strb	fiq_rtmp, [ fiq_rrx ], #1

	mov	fiq_rtmp, #0xff
	strb	fiq_rtmp, [ fiq_rspi, # S3C2410_SPTDAT ]

	subs	fiq_rcount, fiq_rcount, #1
	subnes	pc, lr, #4		@@ return, still have work to do

	@@ set IRQ controller so that next op will trigger IRQ
	mov	fiq_rtmp, #0
	str	fiq_rtmp, [ fiq_rirq, # S3C2410_INTMOD  - S3C24XX_VA_IRQ ]
	subs	pc, lr, #4

fiq_rx_irq_ack:
	.word	0
fiq_rx_end:

ENTRY(s3c24xx_spi_fiq_txrx)
s3c24xx_spi_fiq_txrx:
	.word	fiq_txrx_end - fiq_txrx_start
	.word	fiq_txrx_irq_ack - fiq_txrx_start
fiq_txrx_start:

	ldrb	fiq_rtmp, [ fiq_rspi, #  S3C2410_SPRDAT ]
	strb	fiq_rtmp, [ fiq_rrx ], #1

	ldr	fiq_rtmp, fiq_txrx_irq_ack
	str	fiq_rtmp, [ fiq_rirq, # S3C2410_SRCPND - S3C24XX_VA_IRQ ]

	ldrb	fiq_rtmp, [ fiq_rtx ], #1
	strb	fiq_rtmp, [ fiq_rspi, # S3C2410_SPTDAT ]

	subs	fiq_rcount, fiq_rcount, #1
	subnes	pc, lr, #4		@@ return, still have work to do

	mov	fiq_rtmp, #0
	str	fiq_rtmp, [ fiq_rirq, # S3C2410_INTMOD  - S3C24XX_VA_IRQ ]
	subs	pc, lr, #4

fiq_txrx_irq_ack:
	.word	0

fiq_txrx_end:

ENTRY(s3c24xx_spi_fiq_tx)
s3c24xx_spi_fix_tx:
	.word	fiq_tx_end - fiq_tx_start
	.word	fiq_tx_irq_ack - fiq_tx_start
fiq_tx_start:
	ldrb	fiq_rtmp, [ fiq_rspi, #  S3C2410_SPRDAT ]

	ldr	fiq_rtmp, fiq_tx_irq_ack
	str	fiq_rtmp, [ fiq_rirq, # S3C2410_SRCPND - S3C24XX_VA_IRQ ]

	ldrb	fiq_rtmp, [ fiq_rtx ], #1
	strb	fiq_rtmp, [ fiq_rspi, # S3C2410_SPTDAT ]

	subs	fiq_rcount, fiq_rcount, #1
	subnes	pc, lr, #4		@@ return, still have work to do

	mov	fiq_rtmp, #0
	str	fiq_rtmp, [ fiq_rirq, # S3C2410_INTMOD  - S3C24XX_VA_IRQ ]
	subs	pc, lr, #4

fiq_tx_irq_ack:
	.word	0

fiq_tx_end:

	.end
