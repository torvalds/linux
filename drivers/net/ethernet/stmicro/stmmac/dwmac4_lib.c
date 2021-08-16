// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2007-2015  STMicroelectronics Ltd
 *
 * Author: Alexandre Torgue <alexandre.torgue@st.com>
 */

#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/delay.h>
#include "common.h"
#include "dwmac4_dma.h"
#include "dwmac4.h"

int dwmac4_dma_reset(void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + DMA_BUS_MODE);

	/* DMA SW reset */
	value |= DMA_BUS_MODE_SFT_RESET;
	writel(value, ioaddr + DMA_BUS_MODE);

	return readl_poll_timeout(ioaddr + DMA_BUS_MODE, value,
				 !(value & DMA_BUS_MODE_SFT_RESET),
				 10000, 1000000);
}

void dwmac4_set_rx_tail_ptr(void __iomem *ioaddr, u32 tail_ptr, u32 chan)
{
	writel(tail_ptr, ioaddr + DMA_CHAN_RX_END_ADDR(chan));
}

void dwmac4_set_tx_tail_ptr(void __iomem *ioaddr, u32 tail_ptr, u32 chan)
{
	writel(tail_ptr, ioaddr + DMA_CHAN_TX_END_ADDR(chan));
}

void dwmac4_dma_start_tx(void __iomem *ioaddr, u32 chan)
{
	u32 value = readl(ioaddr + DMA_CHAN_TX_CONTROL(chan));

	value |= DMA_CONTROL_ST;
	writel(value, ioaddr + DMA_CHAN_TX_CONTROL(chan));

	value = readl(ioaddr + GMAC_CONFIG);
	value |= GMAC_CONFIG_TE;
	writel(value, ioaddr + GMAC_CONFIG);
}

void dwmac4_dma_stop_tx(void __iomem *ioaddr, u32 chan)
{
	u32 value = readl(ioaddr + DMA_CHAN_TX_CONTROL(chan));

	value &= ~DMA_CONTROL_ST;
	writel(value, ioaddr + DMA_CHAN_TX_CONTROL(chan));
}

void dwmac4_dma_start_rx(void __iomem *ioaddr, u32 chan)
{
	u32 value = readl(ioaddr + DMA_CHAN_RX_CONTROL(chan));

	value |= DMA_CONTROL_SR;

	writel(value, ioaddr + DMA_CHAN_RX_CONTROL(chan));

	value = readl(ioaddr + GMAC_CONFIG);
	value |= GMAC_CONFIG_RE;
	writel(value, ioaddr + GMAC_CONFIG);
}

void dwmac4_dma_stop_rx(void __iomem *ioaddr, u32 chan)
{
	u32 value = readl(ioaddr + DMA_CHAN_RX_CONTROL(chan));

	value &= ~DMA_CONTROL_SR;
	writel(value, ioaddr + DMA_CHAN_RX_CONTROL(chan));
}

void dwmac4_set_tx_ring_len(void __iomem *ioaddr, u32 len, u32 chan)
{
	writel(len, ioaddr + DMA_CHAN_TX_RING_LEN(chan));
}

void dwmac4_set_rx_ring_len(void __iomem *ioaddr, u32 len, u32 chan)
{
	writel(len, ioaddr + DMA_CHAN_RX_RING_LEN(chan));
}

void dwmac4_enable_dma_irq(void __iomem *ioaddr, u32 chan, bool rx, bool tx)
{
	u32 value = readl(ioaddr + DMA_CHAN_INTR_ENA(chan));

	if (rx)
		value |= DMA_CHAN_INTR_DEFAULT_RX;
	if (tx)
		value |= DMA_CHAN_INTR_DEFAULT_TX;

	writel(value, ioaddr + DMA_CHAN_INTR_ENA(chan));
}

void dwmac410_enable_dma_irq(void __iomem *ioaddr, u32 chan, bool rx, bool tx)
{
	u32 value = readl(ioaddr + DMA_CHAN_INTR_ENA(chan));

	if (rx)
		value |= DMA_CHAN_INTR_DEFAULT_RX_4_10;
	if (tx)
		value |= DMA_CHAN_INTR_DEFAULT_TX_4_10;

	writel(value, ioaddr + DMA_CHAN_INTR_ENA(chan));
}

void dwmac4_disable_dma_irq(void __iomem *ioaddr, u32 chan, bool rx, bool tx)
{
	u32 value = readl(ioaddr + DMA_CHAN_INTR_ENA(chan));

	if (rx)
		value &= ~DMA_CHAN_INTR_DEFAULT_RX;
	if (tx)
		value &= ~DMA_CHAN_INTR_DEFAULT_TX;

	writel(value, ioaddr + DMA_CHAN_INTR_ENA(chan));
}

void dwmac410_disable_dma_irq(void __iomem *ioaddr, u32 chan, bool rx, bool tx)
{
	u32 value = readl(ioaddr + DMA_CHAN_INTR_ENA(chan));

	if (rx)
		value &= ~DMA_CHAN_INTR_DEFAULT_RX_4_10;
	if (tx)
		value &= ~DMA_CHAN_INTR_DEFAULT_TX_4_10;

	writel(value, ioaddr + DMA_CHAN_INTR_ENA(chan));
}

int dwmac4_dma_interrupt(void __iomem *ioaddr,
			 struct stmmac_extra_stats *x, u32 chan, u32 dir)
{
	u32 intr_status = readl(ioaddr + DMA_CHAN_STATUS(chan));
	u32 intr_en = readl(ioaddr + DMA_CHAN_INTR_ENA(chan));
	int ret = 0;

	if (dir == DMA_DIR_RX)
		intr_status &= DMA_CHAN_STATUS_MSK_RX;
	else if (dir == DMA_DIR_TX)
		intr_status &= DMA_CHAN_STATUS_MSK_TX;

	/* ABNORMAL interrupts */
	if (unlikely(intr_status & DMA_CHAN_STATUS_AIS)) {
		if (unlikely(intr_status & DMA_CHAN_STATUS_RBU))
			x->rx_buf_unav_irq++;
		if (unlikely(intr_status & DMA_CHAN_STATUS_RPS))
			x->rx_process_stopped_irq++;
		if (unlikely(intr_status & DMA_CHAN_STATUS_RWT))
			x->rx_watchdog_irq++;
		if (unlikely(intr_status & DMA_CHAN_STATUS_ETI))
			x->tx_early_irq++;
		if (unlikely(intr_status & DMA_CHAN_STATUS_TPS)) {
			x->tx_process_stopped_irq++;
			ret = tx_hard_error;
		}
		if (unlikely(intr_status & DMA_CHAN_STATUS_FBE)) {
			x->fatal_bus_error_irq++;
			ret = tx_hard_error;
		}
	}
	/* TX/RX NORMAL interrupts */
	if (likely(intr_status & DMA_CHAN_STATUS_NIS))
		x->normal_irq_n++;
	if (likely(intr_status & DMA_CHAN_STATUS_RI)) {
		x->rx_normal_irq_n++;
		ret |= handle_rx;
	}
	if (likely(intr_status & DMA_CHAN_STATUS_TI)) {
		x->tx_normal_irq_n++;
		ret |= handle_tx;
	}
	if (unlikely(intr_status & DMA_CHAN_STATUS_TBU))
		ret |= handle_tx;
	if (unlikely(intr_status & DMA_CHAN_STATUS_ERI))
		x->rx_early_irq++;

	writel(intr_status & intr_en, ioaddr + DMA_CHAN_STATUS(chan));
	return ret;
}

void stmmac_dwmac4_set_mac_addr(void __iomem *ioaddr, u8 addr[6],
				unsigned int high, unsigned int low)
{
	unsigned long data;

	data = (addr[5] << 8) | addr[4];
	/* For MAC Addr registers se have to set the Address Enable (AE)
	 * bit that has no effect on the High Reg 0 where the bit 31 (MO)
	 * is RO.
	 */
	data |= (STMMAC_CHAN0 << GMAC_HI_DCS_SHIFT);
	writel(data | GMAC_HI_REG_AE, ioaddr + high);
	data = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	writel(data, ioaddr + low);
}

/* Enable disable MAC RX/TX */
void stmmac_dwmac4_set_mac(void __iomem *ioaddr, bool enable)
{
	u32 value = readl(ioaddr + GMAC_CONFIG);

	if (enable)
		value |= GMAC_CONFIG_RE | GMAC_CONFIG_TE;
	else
		value &= ~(GMAC_CONFIG_TE | GMAC_CONFIG_RE);

	writel(value, ioaddr + GMAC_CONFIG);
}

void stmmac_dwmac4_get_mac_addr(void __iomem *ioaddr, unsigned char *addr,
				unsigned int high, unsigned int low)
{
	unsigned int hi_addr, lo_addr;

	/* Read the MAC address from the hardware */
	hi_addr = readl(ioaddr + high);
	lo_addr = readl(ioaddr + low);

	/* Extract the MAC address from the high and low words */
	addr[0] = lo_addr & 0xff;
	addr[1] = (lo_addr >> 8) & 0xff;
	addr[2] = (lo_addr >> 16) & 0xff;
	addr[3] = (lo_addr >> 24) & 0xff;
	addr[4] = hi_addr & 0xff;
	addr[5] = (hi_addr >> 8) & 0xff;
}
