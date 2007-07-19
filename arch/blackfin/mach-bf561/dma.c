/*
 * File:         arch/blackfin/mach-bf561/dma.c
 * Based on:
 * Author:
 *
 * Created:
 * Description:  This file contains the simple DMA Implementation for Blackfin
 *
 * Modified:
 *               Copyright 2004-2007 Analog Devices Inc.
 *
 * Bugs:         Enter bugs at http://blackfin.uclinux.org/
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, see the file COPYING, or write
 * to the Free Software Foundation, Inc.,
 * 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */
#include <asm/blackfin.h>
#include <asm/dma.h>

struct dma_register *base_addr[MAX_BLACKFIN_DMA_CHANNEL] = {
	(struct dma_register *) DMA1_0_NEXT_DESC_PTR,
	(struct dma_register *) DMA1_1_NEXT_DESC_PTR,
	(struct dma_register *) DMA1_2_NEXT_DESC_PTR,
	(struct dma_register *) DMA1_3_NEXT_DESC_PTR,
	(struct dma_register *) DMA1_4_NEXT_DESC_PTR,
	(struct dma_register *) DMA1_5_NEXT_DESC_PTR,
	(struct dma_register *) DMA1_6_NEXT_DESC_PTR,
	(struct dma_register *) DMA1_7_NEXT_DESC_PTR,
	(struct dma_register *) DMA1_8_NEXT_DESC_PTR,
	(struct dma_register *) DMA1_9_NEXT_DESC_PTR,
	(struct dma_register *) DMA1_10_NEXT_DESC_PTR,
	(struct dma_register *) DMA1_11_NEXT_DESC_PTR,
	(struct dma_register *) DMA2_0_NEXT_DESC_PTR,
	(struct dma_register *) DMA2_1_NEXT_DESC_PTR,
	(struct dma_register *) DMA2_2_NEXT_DESC_PTR,
	(struct dma_register *) DMA2_3_NEXT_DESC_PTR,
	(struct dma_register *) DMA2_4_NEXT_DESC_PTR,
	(struct dma_register *) DMA2_5_NEXT_DESC_PTR,
	(struct dma_register *) DMA2_6_NEXT_DESC_PTR,
	(struct dma_register *) DMA2_7_NEXT_DESC_PTR,
	(struct dma_register *) DMA2_8_NEXT_DESC_PTR,
	(struct dma_register *) DMA2_9_NEXT_DESC_PTR,
	(struct dma_register *) DMA2_10_NEXT_DESC_PTR,
	(struct dma_register *) DMA2_11_NEXT_DESC_PTR,
	(struct dma_register *) MDMA1_D0_NEXT_DESC_PTR,
	(struct dma_register *) MDMA1_S0_NEXT_DESC_PTR,
	(struct dma_register *) MDMA1_D1_NEXT_DESC_PTR,
	(struct dma_register *) MDMA1_S1_NEXT_DESC_PTR,
	(struct dma_register *) MDMA2_D0_NEXT_DESC_PTR,
	(struct dma_register *) MDMA2_S0_NEXT_DESC_PTR,
	(struct dma_register *) MDMA2_D1_NEXT_DESC_PTR,
	(struct dma_register *) MDMA2_S1_NEXT_DESC_PTR,
	(struct dma_register *) IMDMA_D0_NEXT_DESC_PTR,
	(struct dma_register *) IMDMA_S0_NEXT_DESC_PTR,
	(struct dma_register *) IMDMA_D1_NEXT_DESC_PTR,
	(struct dma_register *) IMDMA_S1_NEXT_DESC_PTR,
};

int channel2irq(unsigned int channel)
{
	int ret_irq = -1;

	switch (channel) {
	case CH_PPI0:
		ret_irq = IRQ_PPI0;
		break;
	case CH_PPI1:
		ret_irq = IRQ_PPI1;
		break;
	case CH_SPORT0_RX:
		ret_irq = IRQ_SPORT0_RX;
		break;
	case CH_SPORT0_TX:
		ret_irq = IRQ_SPORT0_TX;
		break;
	case CH_SPORT1_RX:
		ret_irq = IRQ_SPORT1_RX;
		break;
	case CH_SPORT1_TX:
		ret_irq = IRQ_SPORT1_TX;
		break;
	case CH_SPI:
		ret_irq = IRQ_SPI;
		break;
	case CH_UART_RX:
		ret_irq = IRQ_UART_RX;
		break;
	case CH_UART_TX:
		ret_irq = IRQ_UART_TX;
		break;

	case CH_MEM_STREAM0_SRC:
	case CH_MEM_STREAM0_DEST:
		ret_irq = IRQ_MEM_DMA0;
		break;
	case CH_MEM_STREAM1_SRC:
	case CH_MEM_STREAM1_DEST:
		ret_irq = IRQ_MEM_DMA1;
		break;
	case CH_MEM_STREAM2_SRC:
	case CH_MEM_STREAM2_DEST:
		ret_irq = IRQ_MEM_DMA2;
		break;
	case CH_MEM_STREAM3_SRC:
	case CH_MEM_STREAM3_DEST:
		ret_irq = IRQ_MEM_DMA3;
		break;

	case CH_IMEM_STREAM0_SRC:
	case CH_IMEM_STREAM0_DEST:
		ret_irq = IRQ_IMEM_DMA0;
		break;
	case CH_IMEM_STREAM1_SRC:
	case CH_IMEM_STREAM1_DEST:
		ret_irq = IRQ_IMEM_DMA1;
		break;
	}
	return ret_irq;
}
