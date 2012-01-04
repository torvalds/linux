/*******************************************************************************
  Copyright (C) 2007-2009  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <linux/io.h>
#include "common.h"
#include "dwmac_dma.h"

#undef DWMAC_DMA_DEBUG
#ifdef DWMAC_DMA_DEBUG
#define DWMAC_LIB_DBG(fmt, args...)  printk(fmt, ## args)
#else
#define DWMAC_LIB_DBG(fmt, args...)  do { } while (0)
#endif

/* CSR1 enables the transmit DMA to check for new descriptor */
void dwmac_enable_dma_transmission(void __iomem *ioaddr)
{
	writel(1, ioaddr + DMA_XMT_POLL_DEMAND);
}

void dwmac_enable_dma_irq(void __iomem *ioaddr)
{
	writel(DMA_INTR_DEFAULT_MASK, ioaddr + DMA_INTR_ENA);
}

void dwmac_disable_dma_irq(void __iomem *ioaddr)
{
	writel(0, ioaddr + DMA_INTR_ENA);
}

void dwmac_dma_start_tx(void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + DMA_CONTROL);
	value |= DMA_CONTROL_ST;
	writel(value, ioaddr + DMA_CONTROL);
}

void dwmac_dma_stop_tx(void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + DMA_CONTROL);
	value &= ~DMA_CONTROL_ST;
	writel(value, ioaddr + DMA_CONTROL);
}

void dwmac_dma_start_rx(void __iomem *ioaddr)
{
	u32 value = readl(ioaddr + DMA_CONTROL);
	value |= DMA_CONTROL_SR;
	writel(value, ioaddr + DMA_CONTROL);
}

void dwmac_dma_stop_rx(void __iomem *ioaddr)
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
		pr_info("- TX (Stopped): Reset or Stop command\n");
		break;
	case 1:
		pr_info("- TX (Running):Fetching the Tx desc\n");
		break;
	case 2:
		pr_info("- TX (Running): Waiting for end of tx\n");
		break;
	case 3:
		pr_info("- TX (Running): Reading the data "
		       "and queuing the data into the Tx buf\n");
		break;
	case 6:
		pr_info("- TX (Suspended): Tx Buff Underflow "
		       "or an unavailable Transmit descriptor\n");
		break;
	case 7:
		pr_info("- TX (Running): Closing Tx descriptor\n");
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
		pr_info("- RX (Stopped): Reset or Stop command\n");
		break;
	case 1:
		pr_info("- RX (Running): Fetching the Rx desc\n");
		break;
	case 2:
		pr_info("- RX (Running):Checking for end of pkt\n");
		break;
	case 3:
		pr_info("- RX (Running): Waiting for Rx pkt\n");
		break;
	case 4:
		pr_info("- RX (Suspended): Unavailable Rx buf\n");
		break;
	case 5:
		pr_info("- RX (Running): Closing Rx descriptor\n");
		break;
	case 6:
		pr_info("- RX(Running): Flushing the current frame"
		       " from the Rx buf\n");
		break;
	case 7:
		pr_info("- RX (Running): Queuing the Rx frame"
		       " from the Rx buf into memory\n");
		break;
	default:
		break;
	}
}
#endif

int dwmac_dma_interrupt(void __iomem *ioaddr,
			struct stmmac_extra_stats *x)
{
	int ret = 0;
	/* read the status register (CSR5) */
	u32 intr_status = readl(ioaddr + DMA_STATUS);

	DWMAC_LIB_DBG(KERN_INFO "%s: [CSR5: 0x%08x]\n", __func__, intr_status);
#ifdef DWMAC_DMA_DEBUG
	/* It displays the DMA process states (CSR5 register) */
	show_tx_process_state(intr_status);
	show_rx_process_state(intr_status);
#endif
	/* ABNORMAL interrupts */
	if (unlikely(intr_status & DMA_STATUS_AIS)) {
		DWMAC_LIB_DBG(KERN_INFO "CSR5[15] DMA ABNORMAL IRQ: ");
		if (unlikely(intr_status & DMA_STATUS_UNF)) {
			DWMAC_LIB_DBG(KERN_INFO "transmit underflow\n");
			ret = tx_hard_error_bump_tc;
			x->tx_undeflow_irq++;
		}
		if (unlikely(intr_status & DMA_STATUS_TJT)) {
			DWMAC_LIB_DBG(KERN_INFO "transmit jabber\n");
			x->tx_jabber_irq++;
		}
		if (unlikely(intr_status & DMA_STATUS_OVF)) {
			DWMAC_LIB_DBG(KERN_INFO "recv overflow\n");
			x->rx_overflow_irq++;
		}
		if (unlikely(intr_status & DMA_STATUS_RU)) {
			DWMAC_LIB_DBG(KERN_INFO "receive buffer unavailable\n");
			x->rx_buf_unav_irq++;
		}
		if (unlikely(intr_status & DMA_STATUS_RPS)) {
			DWMAC_LIB_DBG(KERN_INFO "receive process stopped\n");
			x->rx_process_stopped_irq++;
		}
		if (unlikely(intr_status & DMA_STATUS_RWT)) {
			DWMAC_LIB_DBG(KERN_INFO "receive watchdog\n");
			x->rx_watchdog_irq++;
		}
		if (unlikely(intr_status & DMA_STATUS_ETI)) {
			DWMAC_LIB_DBG(KERN_INFO "transmit early interrupt\n");
			x->tx_early_irq++;
		}
		if (unlikely(intr_status & DMA_STATUS_TPS)) {
			DWMAC_LIB_DBG(KERN_INFO "transmit process stopped\n");
			x->tx_process_stopped_irq++;
			ret = tx_hard_error;
		}
		if (unlikely(intr_status & DMA_STATUS_FBI)) {
			DWMAC_LIB_DBG(KERN_INFO "fatal bus error\n");
			x->fatal_bus_error_irq++;
			ret = tx_hard_error;
		}
	}
	/* TX/RX NORMAL interrupts */
	if (intr_status & DMA_STATUS_NIS) {
		x->normal_irq_n++;
		if (likely((intr_status & DMA_STATUS_RI) ||
			 (intr_status & (DMA_STATUS_TI))))
				ret = handle_tx_rx;
	}
	/* Optional hardware blocks, interrupts should be disabled */
	if (unlikely(intr_status &
		     (DMA_STATUS_GPI | DMA_STATUS_GMI | DMA_STATUS_GLI)))
		pr_info("%s: unexpected status %08x\n", __func__, intr_status);
	/* Clear the interrupt by writing a logic 1 to the CSR5[15-0] */
	writel((intr_status & 0x1ffff), ioaddr + DMA_STATUS);

	DWMAC_LIB_DBG(KERN_INFO "\n\n");
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
	writel(data, ioaddr + high);
	data = (addr[3] << 24) | (addr[2] << 16) | (addr[1] << 8) | addr[0];
	writel(data, ioaddr + low);
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

