/*
 *  Copyright (C) 2010, Lars-Peter Clausen <lars@metafoo.de>
 *  JZ7420/JZ4740 DMA definitions
 *
 *  This program is free software; you can redistribute it and/or modify it
 *  under  the terms of the GNU General	 Public License as published by the
 *  Free Software Foundation;  either version 2 of the License, or (at your
 *  option) any later version.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */

#ifndef __ASM_MACH_JZ4740_DMA_H__
#define __ASM_MACH_JZ4740_DMA_H__

struct jz4740_dma_chan;

enum jz4740_dma_request_type {
	JZ4740_DMA_TYPE_AUTO_REQUEST	= 8,
	JZ4740_DMA_TYPE_UART_TRANSMIT	= 20,
	JZ4740_DMA_TYPE_UART_RECEIVE	= 21,
	JZ4740_DMA_TYPE_SPI_TRANSMIT	= 22,
	JZ4740_DMA_TYPE_SPI_RECEIVE	= 23,
	JZ4740_DMA_TYPE_AIC_TRANSMIT	= 24,
	JZ4740_DMA_TYPE_AIC_RECEIVE	= 25,
	JZ4740_DMA_TYPE_MMC_TRANSMIT	= 26,
	JZ4740_DMA_TYPE_MMC_RECEIVE	= 27,
	JZ4740_DMA_TYPE_TCU		= 28,
	JZ4740_DMA_TYPE_SADC		= 29,
	JZ4740_DMA_TYPE_SLCD		= 30,
};

enum jz4740_dma_width {
	JZ4740_DMA_WIDTH_32BIT	= 0,
	JZ4740_DMA_WIDTH_8BIT	= 1,
	JZ4740_DMA_WIDTH_16BIT	= 2,
};

enum jz4740_dma_transfer_size {
	JZ4740_DMA_TRANSFER_SIZE_4BYTE	= 0,
	JZ4740_DMA_TRANSFER_SIZE_1BYTE	= 1,
	JZ4740_DMA_TRANSFER_SIZE_2BYTE	= 2,
	JZ4740_DMA_TRANSFER_SIZE_16BYTE = 3,
	JZ4740_DMA_TRANSFER_SIZE_32BYTE = 4,
};

enum jz4740_dma_flags {
	JZ4740_DMA_SRC_AUTOINC = 0x2,
	JZ4740_DMA_DST_AUTOINC = 0x1,
};

enum jz4740_dma_mode {
	JZ4740_DMA_MODE_SINGLE	= 0,
	JZ4740_DMA_MODE_BLOCK	= 1,
};

struct jz4740_dma_config {
	enum jz4740_dma_width src_width;
	enum jz4740_dma_width dst_width;
	enum jz4740_dma_transfer_size transfer_size;
	enum jz4740_dma_request_type request_type;
	enum jz4740_dma_flags flags;
	enum jz4740_dma_mode mode;
};

typedef void (*jz4740_dma_complete_callback_t)(struct jz4740_dma_chan *, int, void *);

struct jz4740_dma_chan *jz4740_dma_request(void *dev, const char *name);
void jz4740_dma_free(struct jz4740_dma_chan *dma);

void jz4740_dma_configure(struct jz4740_dma_chan *dma,
	const struct jz4740_dma_config *config);


void jz4740_dma_enable(struct jz4740_dma_chan *dma);
void jz4740_dma_disable(struct jz4740_dma_chan *dma);

void jz4740_dma_set_src_addr(struct jz4740_dma_chan *dma, dma_addr_t src);
void jz4740_dma_set_dst_addr(struct jz4740_dma_chan *dma, dma_addr_t dst);
void jz4740_dma_set_transfer_count(struct jz4740_dma_chan *dma, uint32_t count);

uint32_t jz4740_dma_get_residue(const struct jz4740_dma_chan *dma);

void jz4740_dma_set_complete_cb(struct jz4740_dma_chan *dma,
	jz4740_dma_complete_callback_t cb);

#endif	/* __ASM_JZ4740_DMA_H__ */
