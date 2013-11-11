/*
 * the simple DMA Implementation for Blackfin
 *
 * Copyright 2007-2009 Analog Devices Inc.
 *
 * Licensed under the GPL-2 or later.
 */

#include <linux/module.h>

#include <asm/blackfin.h>
#include <asm/dma.h>

struct dma_register * const dma_io_base_addr[MAX_DMA_CHANNELS] = {
	(struct dma_register *) DMA0_NEXT_DESC_PTR,
	(struct dma_register *) DMA1_NEXT_DESC_PTR,
	(struct dma_register *) DMA2_NEXT_DESC_PTR,
	(struct dma_register *) DMA3_NEXT_DESC_PTR,
	(struct dma_register *) DMA4_NEXT_DESC_PTR,
	(struct dma_register *) DMA5_NEXT_DESC_PTR,
	(struct dma_register *) DMA6_NEXT_DESC_PTR,
	(struct dma_register *) DMA7_NEXT_DESC_PTR,
	(struct dma_register *) DMA8_NEXT_DESC_PTR,
	(struct dma_register *) DMA9_NEXT_DESC_PTR,
	(struct dma_register *) DMA10_NEXT_DESC_PTR,
	(struct dma_register *) DMA11_NEXT_DESC_PTR,
	(struct dma_register *) DMA12_NEXT_DESC_PTR,
	(struct dma_register *) DMA13_NEXT_DESC_PTR,
	(struct dma_register *) DMA14_NEXT_DESC_PTR,
	(struct dma_register *) DMA15_NEXT_DESC_PTR,
	(struct dma_register *) DMA16_NEXT_DESC_PTR,
	(struct dma_register *) DMA17_NEXT_DESC_PTR,
	(struct dma_register *) DMA18_NEXT_DESC_PTR,
	(struct dma_register *) DMA19_NEXT_DESC_PTR,
	(struct dma_register *) DMA20_NEXT_DESC_PTR,
	(struct dma_register *) MDMA0_SRC_CRC0_NEXT_DESC_PTR,
	(struct dma_register *) MDMA0_DEST_CRC0_NEXT_DESC_PTR,
	(struct dma_register *) MDMA1_SRC_CRC1_NEXT_DESC_PTR,
	(struct dma_register *) MDMA1_DEST_CRC1_NEXT_DESC_PTR,
	(struct dma_register *) MDMA2_SRC_NEXT_DESC_PTR,
	(struct dma_register *) MDMA2_DEST_NEXT_DESC_PTR,
	(struct dma_register *) MDMA3_SRC_NEXT_DESC_PTR,
	(struct dma_register *) MDMA3_DEST_NEXT_DESC_PTR,
	(struct dma_register *) DMA29_NEXT_DESC_PTR,
	(struct dma_register *) DMA30_NEXT_DESC_PTR,
	(struct dma_register *) DMA31_NEXT_DESC_PTR,
	(struct dma_register *) DMA32_NEXT_DESC_PTR,
	(struct dma_register *) DMA33_NEXT_DESC_PTR,
	(struct dma_register *) DMA34_NEXT_DESC_PTR,
	(struct dma_register *) DMA35_NEXT_DESC_PTR,
	(struct dma_register *) DMA36_NEXT_DESC_PTR,
	(struct dma_register *) DMA37_NEXT_DESC_PTR,
	(struct dma_register *) DMA38_NEXT_DESC_PTR,
	(struct dma_register *) DMA39_NEXT_DESC_PTR,
	(struct dma_register *) DMA40_NEXT_DESC_PTR,
	(struct dma_register *) DMA41_NEXT_DESC_PTR,
	(struct dma_register *) DMA42_NEXT_DESC_PTR,
	(struct dma_register *) DMA43_NEXT_DESC_PTR,
	(struct dma_register *) DMA44_NEXT_DESC_PTR,
	(struct dma_register *) DMA45_NEXT_DESC_PTR,
	(struct dma_register *) DMA46_NEXT_DESC_PTR,
};
EXPORT_SYMBOL(dma_io_base_addr);

int channel2irq(unsigned int channel)
{
	int ret_irq = -1;

	switch (channel) {
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
	case CH_SPORT2_RX:
		ret_irq = IRQ_SPORT2_RX;
		break;
	case CH_SPORT2_TX:
		ret_irq = IRQ_SPORT2_TX;
		break;
	case CH_SPI0_TX:
		ret_irq = IRQ_SPI0_TX;
		break;
	case CH_SPI0_RX:
		ret_irq = IRQ_SPI0_RX;
		break;
	case CH_SPI1_TX:
		ret_irq = IRQ_SPI1_TX;
		break;
	case CH_SPI1_RX:
		ret_irq = IRQ_SPI1_RX;
		break;
	case CH_RSI:
		ret_irq = IRQ_RSI;
		break;
	case CH_SDU:
		ret_irq = IRQ_SDU;
		break;
	case CH_LP0:
		ret_irq = IRQ_LP0;
		break;
	case CH_LP1:
		ret_irq = IRQ_LP1;
		break;
	case CH_LP2:
		ret_irq = IRQ_LP2;
		break;
	case CH_LP3:
		ret_irq = IRQ_LP3;
		break;
	case CH_UART0_RX:
		ret_irq = IRQ_UART0_RX;
		break;
	case CH_UART0_TX:
		ret_irq = IRQ_UART0_TX;
		break;
	case CH_UART1_RX:
		ret_irq = IRQ_UART1_RX;
		break;
	case CH_UART1_TX:
		ret_irq = IRQ_UART1_TX;
		break;
	case CH_EPPI0_CH0:
		ret_irq = IRQ_EPPI0_CH0;
		break;
	case CH_EPPI0_CH1:
		ret_irq = IRQ_EPPI0_CH1;
		break;
	case CH_EPPI1_CH0:
		ret_irq = IRQ_EPPI1_CH0;
		break;
	case CH_EPPI1_CH1:
		ret_irq = IRQ_EPPI1_CH1;
		break;
	case CH_EPPI2_CH0:
		ret_irq = IRQ_EPPI2_CH0;
		break;
	case CH_EPPI2_CH1:
		ret_irq = IRQ_EPPI2_CH1;
		break;
	case CH_PIXC_CH0:
		ret_irq = IRQ_PIXC_CH0;
		break;
	case CH_PIXC_CH1:
		ret_irq = IRQ_PIXC_CH1;
		break;
	case CH_PIXC_CH2:
		ret_irq = IRQ_PIXC_CH2;
		break;
	case CH_PVP_CPDOB:
		ret_irq = IRQ_PVP_CPDOB;
		break;
	case CH_PVP_CPDOC:
		ret_irq = IRQ_PVP_CPDOC;
		break;
	case CH_PVP_CPSTAT:
		ret_irq = IRQ_PVP_CPSTAT;
		break;
	case CH_PVP_CPCI:
		ret_irq = IRQ_PVP_CPCI;
		break;
	case CH_PVP_MPDO:
		ret_irq = IRQ_PVP_MPDO;
		break;
	case CH_PVP_MPDI:
		ret_irq = IRQ_PVP_MPDI;
		break;
	case CH_PVP_MPSTAT:
		ret_irq = IRQ_PVP_MPSTAT;
		break;
	case CH_PVP_MPCI:
		ret_irq = IRQ_PVP_MPCI;
		break;
	case CH_PVP_CPDOA:
		ret_irq = IRQ_PVP_CPDOA;
		break;
	case CH_MEM_STREAM0_SRC:
	case CH_MEM_STREAM0_DEST:
		ret_irq = IRQ_MDMAS0;
		break;
	case CH_MEM_STREAM1_SRC:
	case CH_MEM_STREAM1_DEST:
		ret_irq = IRQ_MDMAS1;
		break;
	case CH_MEM_STREAM2_SRC:
	case CH_MEM_STREAM2_DEST:
		ret_irq = IRQ_MDMAS2;
		break;
	case CH_MEM_STREAM3_SRC:
	case CH_MEM_STREAM3_DEST:
		ret_irq = IRQ_MDMAS3;
		break;
	}
	return ret_irq;
}
