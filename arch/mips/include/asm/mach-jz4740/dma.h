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

#endif	/* __ASM_JZ4740_DMA_H__ */
