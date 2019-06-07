/*******************************************************************************
  This is the driver for the MAC 10/100 on-chip Ethernet controller
  currently tested on all the ST boards based on STb7109 and stx7200 SoCs.

  DWC Ether MAC 10/100 Universal version 4.0 has been used for developing
  this code.

  This contains the functions to handle the dma.

  Copyright (C) 2007-2009  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#include <asm/io.h>
#include "dwmac100.h"
#include "dwmac_dma.h"

static void dwmac100_dma_init(void __iomem *ioaddr,
			      struct stmmac_dma_cfg *dma_cfg, int atds)
{
	/* Enable Application Access by writing to DMA CSR0 */
	writel(DMA_BUS_MODE_DEFAULT | (dma_cfg->pbl << DMA_BUS_MODE_PBL_SHIFT),
	       ioaddr + DMA_BUS_MODE);

	/* Mask interrupts by writing to CSR7 */
	writel(DMA_INTR_DEFAULT_MASK, ioaddr + DMA_INTR_ENA);
}

static void dwmac100_dma_init_rx(void __iomem *ioaddr,
				 struct stmmac_dma_cfg *dma_cfg,
				 u32 dma_rx_phy, u32 chan)
{
	/* RX descriptor base addr lists must be written into DMA CSR3 */
	writel(dma_rx_phy, ioaddr + DMA_RCV_BASE_ADDR);
}

static void dwmac100_dma_init_tx(void __iomem *ioaddr,
				 struct stmmac_dma_cfg *dma_cfg,
				 u32 dma_tx_phy, u32 chan)
{
	/* TX descriptor base addr lists must be written into DMA CSR4 */
	writel(dma_tx_phy, ioaddr + DMA_TX_BASE_ADDR);
}

/* Store and Forward capability is not used at all.
 *
 * The transmit threshold can be programmed by setting the TTC bits in the DMA
 * control register.
 */
static void dwmac100_dma_operation_mode_tx(void __iomem *ioaddr, int mode,
					   u32 channel, int fifosz, u8 qmode)
{
	u32 csr6 = readl(ioaddr + DMA_CONTROL);

	if (mode <= 32)
		csr6 |= DMA_CONTROL_TTC_32;
	else if (mode <= 64)
		csr6 |= DMA_CONTROL_TTC_64;
	else
		csr6 |= DMA_CONTROL_TTC_128;

	writel(csr6, ioaddr + DMA_CONTROL);
}

static void dwmac100_dump_dma_regs(void __iomem *ioaddr, u32 *reg_space)
{
	int i;

	for (i = 0; i < NUM_DWMAC100_DMA_REGS; i++)
		reg_space[DMA_BUS_MODE / 4 + i] =
			readl(ioaddr + DMA_BUS_MODE + i * 4);

	reg_space[DMA_CUR_TX_BUF_ADDR / 4] =
		readl(ioaddr + DMA_CUR_TX_BUF_ADDR);
	reg_space[DMA_CUR_RX_BUF_ADDR / 4] =
		readl(ioaddr + DMA_CUR_RX_BUF_ADDR);
}

/* DMA controller has two counters to track the number of the missed frames. */
static void dwmac100_dma_diagnostic_fr(void *data, struct stmmac_extra_stats *x,
				       void __iomem *ioaddr)
{
	struct net_device_stats *stats = (struct net_device_stats *)data;
	u32 csr8 = readl(ioaddr + DMA_MISSED_FRAME_CTR);

	if (unlikely(csr8)) {
		if (csr8 & DMA_MISSED_FRAME_OVE) {
			stats->rx_over_errors += 0x800;
			x->rx_overflow_cntr += 0x800;
		} else {
			unsigned int ove_cntr;
			ove_cntr = ((csr8 & DMA_MISSED_FRAME_OVE_CNTR) >> 17);
			stats->rx_over_errors += ove_cntr;
			x->rx_overflow_cntr += ove_cntr;
		}

		if (csr8 & DMA_MISSED_FRAME_OVE_M) {
			stats->rx_missed_errors += 0xffff;
			x->rx_missed_cntr += 0xffff;
		} else {
			unsigned int miss_f = (csr8 & DMA_MISSED_FRAME_M_CNTR);
			stats->rx_missed_errors += miss_f;
			x->rx_missed_cntr += miss_f;
		}
	}
}

const struct stmmac_dma_ops dwmac100_dma_ops = {
	.reset = dwmac_dma_reset,
	.init = dwmac100_dma_init,
	.init_rx_chan = dwmac100_dma_init_rx,
	.init_tx_chan = dwmac100_dma_init_tx,
	.dump_regs = dwmac100_dump_dma_regs,
	.dma_tx_mode = dwmac100_dma_operation_mode_tx,
	.dma_diagnostic_fr = dwmac100_dma_diagnostic_fr,
	.enable_dma_transmission = dwmac_enable_dma_transmission,
	.enable_dma_irq = dwmac_enable_dma_irq,
	.disable_dma_irq = dwmac_disable_dma_irq,
	.start_tx = dwmac_dma_start_tx,
	.stop_tx = dwmac_dma_stop_tx,
	.start_rx = dwmac_dma_start_rx,
	.stop_rx = dwmac_dma_stop_rx,
	.dma_interrupt = dwmac_dma_interrupt,
};
