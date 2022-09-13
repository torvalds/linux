// SPDX-License-Identifier: GPL-2.0-only
/*******************************************************************************
  Copyright (C) 2007-2009  STMicroelectronics Ltd


  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/io.h>
#include <linux/iopoll.h>
#include "common.h"
#include "dwmac_dma.h"

#define GMAC_HI_REG_AE		0x80000000

int dwmac_dma_reset(void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + DMA_BUS_MODE);

	/* DMA SW reset */
	value |= DMA_BUS_MODE_SFT_RESET;
	writel(value, ioaddr + DMA_BUS_MODE);

	return readl_poll_timeout(ioaddr + DMA_BUS_MODE, value,
				 !(value & DMA_BUS_MODE_SFT_RESET),
				 10000, 200000);
}

/* CSR1 enables the transmit DMA to check for new descriptor */
void dwmac_enable_dma_transmission(void __iomem *ioaddr)
{
	writel(1, ioaddr + DMA_XMT_POLL_DEMAND);
}

void dwmac_enable_dma_irq(void __iomem *ioaddr, u32 chan, bool rx, bool tx)
{
	u32 value = readl(ioaddr + DMA_INTR_ENA);

	if (rx)
		value |= DMA_INTR_DEFAULT_RX;
	if (tx)
		value |= DMA_INTR_DEFAULT_TX;

	writel(value, ioaddr + DMA_INTR_ENA);
}

void dwmac_disable_dma_irq(void __iomem *ioaddr, u32 chan, bool rx, bool tx)
{
	u32 value = readl(ioaddr + DMA_INTR_ENA);

	if (rx)
		value &= ~DMA_INTR_DEFAULT_RX;
	if (tx)
		value &= ~DMA_INTR_DEFAULT_TX;

	writel(value, ioaddr + DMA_INTR_ENA);
}

void dwmac_dma_start_tx(void __iomem *ioaddr, u32 chan)
{
	u32 value = readl(ioaddr + DMA_CONTROL);
	value |= DMA_CONTROL_ST;
	writel(value, ioaddr + DMA_CONTROL);
}

void dwmac_dma_stop_tx(void __iomem *ioaddr, u32 chan)
{
	u32 value = readl(ioaddr + DMA_CONTROL);
	value &= ~DMA_CONTROL_ST;
	writel(value, ioaddr + DMA_CONTROL);
}

void dwmac_dma_start_rx(void __iomem *ioaddr, u32 chan)
{
	u32 value = readl(ioaddr + DMA_CONTROL);
	value |= DMA_CONTROL_SR;
	writel(value, ioaddr + DMA_CONTROL);
}

void dwmac_dma_stop_rx(void __iomem *ioaddr, u32 chan)
{
	u32 value = readl(ioaddr + DMA_CONTROL);
	value &= ~DMA_CONTROL_SR;
	writel(value, ioaddr + DMA_CONTROL);
}

#ifdef DWMAC_DMA_DEBUG
static void show_tx_process_state(unsigned int status)
{
	unsigned int state;
	state = (status & DMA_STATUS_TS_MASK) >> DMA_STATUS_TS_SHIFT;

	switch (state) {
	case 0:
		pr_debug("- TX (Stopped): Reset or Stop command\n");
		break;
	case 1:
		pr_debug("- TX (Running): Fetching the Tx desc\n");
		break;
	case 2:
		pr_debug("- TX (Running): Waiting for end of tx\n");
		break;
	case 3:
		pr_debug("- TX (Running): Reading the data "
		       "and queuing the data into the Tx buf\n");
		break;
	case 6:
		pr_debug("- TX (Suspended): Tx Buff Underflow "
		       "or an unavailable Transmit descriptor\n");
		break;
	case 7:
		pr_debug("- TX (Running): Closing Tx descriptor\n");
		break;
	default:
		break;
	}
}

static void show_rx_process_state(unsigned int status)
{
	unsigned int state;
	state = (status & DMA_STATUS_RS_MASK) >> DMA_STATUS_RS_SHIFT;

	switch (state) {
	case 0:
		pr_debug("- RX (Stopped): Reset or Stop command\n");
		break;
	case 1:
		pr_debug("- RX (Running): Fetching the Rx desc\n");
		break;
	case 2:
		pr_debug("- RX (Running): Checking for end of pkt\n");
		break;
	case 3:
		pr_debug("- RX (Running): Waiting for Rx pkt\n");
		break;
	case 4:
		pr_debug("- RX (Suspended): Unavailable Rx buf\n");
		break;
	case 5:
		pr_debug("- RX (Running): Closing Rx descriptor\n");
		break;
	case 6:
		pr_debug("- RX(Running): Flushing the current frame"
		       " from the Rx buf\n");
		break;
	case 7:
		pr_debug("- RX (Running): Queuing the Rx frame"
		       " from the Rx buf into memory\n");
		break;
	default:
		break;
	}
}
#endif

int dwmac_dma_interrupt(void __iomem *ioaddr,
			struct stmmac_extra_stats *x, u32 chan, u32 dir)
{
	int ret = 0;
	/* read the status register (CSR5) */
	u32 intr_status = readl(ioaddr + DMA_STATUS);

#ifdef DWMAC_DMA_DEBUG
	/* Enable it to monitor DMA rx/tx status in case of critical problems */
	pr_debug("%s: [CSR5: 0x%08x]\n", __func__, intr_status);
	show_tx_process_state(intr_status);
	show_rx_process_state(intr_status);
#endif

	if (dir == DMA_DIR_RX)
		intr_status &= DMA_STATUS_MSK_RX;
	else if (dir == DMA_DIR_TX)
		intr_status &= DMA_STATUS_MSK_TX;

	/* ABNORMAL interrupts */
	if (unlikely(intr_status & DMA_STATUS_AIS)) {
		if (unlikely(intr_status & DMA_STATUS_UNF)) {
			ret = tx_hard_error_bump_tc;
			x->tx_undeflow_irq++;
		}
		if (unlikely(intr_status & DMA_STATUS_TJT))
			x->tx_jabber_irq++;

		if (unlikely(intr_status & DMA_STATUS_OVF))
			x->rx_overflow_irq++;

		if (unlikely(intr_status & DMA_STATUS_RU))
			x->rx_buf_unav_irq++;
		if (unlikely(intr_status & DMA_STATUS_RPS))
			x->rx_process_stopped_irq++;
		if (unlikely(intr_status & DMA_STATUS_RWT))
			x->rx_watchdog_irq++;
		if (unlikely(intr_status & DMA_STATUS_ETI))
			x->tx_early_irq++;
		if (unlikely(intr_status & DMA_STATUS_TPS)) {
			x->tx_process_stopped_irq++;
			ret = tx_hard_error;
		}
		if (unlikely(intr_status & DMA_STATUS_FBI)) {
			x->fatal_bus_error_irq++;
			ret = tx_hard_error;
		}
	}
	/* TX/RX NORMAL interrupts */
	if (likely(intr_status & DMA_STATUS_NIS)) {
		x->normal_irq_n++;
		if (likely(intr_status & DMA_STATUS_RI)) {
			u32 value = readl(ioaddr + DMA_INTR_ENA);
			/* to schedule NAPI on real RIE event. */
			if (likely(value & DMA_INTR_ENA_RIE)) {
				x->rx_normal_irq_n++;
				ret |= handle_rx;
			}
		}
		if (likely(intr_status & DMA_STATUS_TI)) {
			x->tx_normal_irq_n++;
			ret |= handle_tx;
		}
		if (unlikely(intr_status & DMA_STATUS_ERI))
			x->rx_early_irq++;
	}
	/* Optional hardware blocks, interrupts should be disabled */
	if (unlikely(intr_status &
		     (DMA_STATUS_GPI | DMA_STATUS_GMI | DMA_STATUS_GLI)))
		pr_warn("%s: unexpected status %08x\n", __func__, intr_status);

	/* Clear the interrupt by writing a logic 1 to the CSR5[15-0] */
	writel((intr_status & 0x1ffff), ioaddr + DMA_STATUS);

	return ret;
}

void dwmac_dma_flush_tx_fifo(void __iomem *ioaddr)
{
	u32 csr6 = readl(ioaddr + DMA_CONTROL);
	writel((csr6 | DMA_CONTROL_FTF), ioaddr + DMA_CONTROL);

	do {} while ((readl(ioaddr + DMA_CONTROL) & DMA_CONTROL_FTF));
}

void stmmac_set_mac_addr(void __iomem *ioaddr, u8 addr[6],
			 unsigned int high, unsigned int low)
{
	unsigned long data;

	data = (addr[5] << 8) | addr[4];
	/* For MAC Addr registers we have to set the Address Enable (AE)
	 * bit that has no effect on the High Reg 0 where the bit 31 (MO)
	 * is RO.
	 */
	writel(data | GMAC_HI_REG_AE, ioaddr + high);
	data = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	writel(data, ioaddr + low);
}
EXPORT_SYMBOL_GPL(stmmac_set_mac_addr);

/* Enable disable MAC RX/TX */
void stmmac_set_mac(void __iomem *ioaddr, bool enable)
{
	u32 old_val, value;

	old_val = readl(ioaddr + MAC_CTRL_REG);
	value = old_val;

	if (enable)
		value |= MAC_ENABLE_RX | MAC_ENABLE_TX;
	else
		value &= ~(MAC_ENABLE_TX | MAC_ENABLE_RX);

	if (value != old_val)
		writel(value, ioaddr + MAC_CTRL_REG);
}

void stmmac_get_mac_addr(void __iomem *ioaddr, unsigned char *addr,
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
EXPORT_SYMBOL_GPL(stmmac_get_mac_addr);
