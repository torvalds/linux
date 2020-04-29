// SPDX-License-Identifier: GPL-2.0-only
/* 10G controller driver for Samsung SoCs
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Siva Reddy Kallam <siva.kallam@samsung.com>
 */
#include <linux/delay.h>
#include <linux/export.h>
#include <linux/io.h>
#include <linux/netdevice.h>
#include <linux/phy.h>

#include "sxgbe_common.h"
#include "sxgbe_dma.h"
#include "sxgbe_reg.h"
#include "sxgbe_desc.h"

/* DMA core initialization */
static int sxgbe_dma_init(void __iomem *ioaddr, int fix_burst, int burst_map)
{
	u32 reg_val;

	reg_val = readl(ioaddr + SXGBE_DMA_SYSBUS_MODE_REG);

	/* if fix_burst = 0, Set UNDEF = 1 of DMA_Sys_Mode Register.
	 * if fix_burst = 1, Set UNDEF = 0 of DMA_Sys_Mode Register.
	 * burst_map is bitmap for  BLEN[4, 8, 16, 32, 64, 128 and 256].
	 * Set burst_map irrespective of fix_burst value.
	 */
	if (!fix_burst)
		reg_val |= SXGBE_DMA_AXI_UNDEF_BURST;

	/* write burst len map */
	reg_val |= (burst_map << SXGBE_DMA_BLENMAP_LSHIFT);

	writel(reg_val,	ioaddr + SXGBE_DMA_SYSBUS_MODE_REG);

	return 0;
}

static void sxgbe_dma_channel_init(void __iomem *ioaddr, int cha_num,
				   int fix_burst, int pbl, dma_addr_t dma_tx,
				   dma_addr_t dma_rx, int t_rsize, int r_rsize)
{
	u32 reg_val;
	dma_addr_t dma_addr;

	reg_val = readl(ioaddr + SXGBE_DMA_CHA_CTL_REG(cha_num));
	/* set the pbl */
	if (fix_burst) {
		reg_val |= SXGBE_DMA_PBL_X8MODE;
		writel(reg_val, ioaddr + SXGBE_DMA_CHA_CTL_REG(cha_num));
		/* program the TX pbl */
		reg_val = readl(ioaddr + SXGBE_DMA_CHA_TXCTL_REG(cha_num));
		reg_val |= (pbl << SXGBE_DMA_TXPBL_LSHIFT);
		writel(reg_val, ioaddr + SXGBE_DMA_CHA_TXCTL_REG(cha_num));
		/* program the RX pbl */
		reg_val = readl(ioaddr + SXGBE_DMA_CHA_RXCTL_REG(cha_num));
		reg_val |= (pbl << SXGBE_DMA_RXPBL_LSHIFT);
		writel(reg_val, ioaddr + SXGBE_DMA_CHA_RXCTL_REG(cha_num));
	}

	/* program desc registers */
	writel(upper_32_bits(dma_tx),
	       ioaddr + SXGBE_DMA_CHA_TXDESC_HADD_REG(cha_num));
	writel(lower_32_bits(dma_tx),
	       ioaddr + SXGBE_DMA_CHA_TXDESC_LADD_REG(cha_num));

	writel(upper_32_bits(dma_rx),
	       ioaddr + SXGBE_DMA_CHA_RXDESC_HADD_REG(cha_num));
	writel(lower_32_bits(dma_rx),
	       ioaddr + SXGBE_DMA_CHA_RXDESC_LADD_REG(cha_num));

	/* program tail pointers */
	/* assumption: upper 32 bits are constant and
	 * same as TX/RX desc list
	 */
	dma_addr = dma_tx + ((t_rsize - 1) * SXGBE_DESC_SIZE_BYTES);
	writel(lower_32_bits(dma_addr),
	       ioaddr + SXGBE_DMA_CHA_TXDESC_TAILPTR_REG(cha_num));

	dma_addr = dma_rx + ((r_rsize - 1) * SXGBE_DESC_SIZE_BYTES);
	writel(lower_32_bits(dma_addr),
	       ioaddr + SXGBE_DMA_CHA_RXDESC_LADD_REG(cha_num));
	/* program the ring sizes */
	writel(t_rsize - 1, ioaddr + SXGBE_DMA_CHA_TXDESC_RINGLEN_REG(cha_num));
	writel(r_rsize - 1, ioaddr + SXGBE_DMA_CHA_RXDESC_RINGLEN_REG(cha_num));

	/* Enable TX/RX interrupts */
	writel(SXGBE_DMA_ENA_INT,
	       ioaddr + SXGBE_DMA_CHA_INT_ENABLE_REG(cha_num));
}

static void sxgbe_enable_dma_transmission(void __iomem *ioaddr, int cha_num)
{
	u32 tx_config;

	tx_config = readl(ioaddr + SXGBE_DMA_CHA_TXCTL_REG(cha_num));
	tx_config |= SXGBE_TX_START_DMA;
	writel(tx_config, ioaddr + SXGBE_DMA_CHA_TXCTL_REG(cha_num));
}

static void sxgbe_enable_dma_irq(void __iomem *ioaddr, int dma_cnum)
{
	/* Enable TX/RX interrupts */
	writel(SXGBE_DMA_ENA_INT,
	       ioaddr + SXGBE_DMA_CHA_INT_ENABLE_REG(dma_cnum));
}

static void sxgbe_disable_dma_irq(void __iomem *ioaddr, int dma_cnum)
{
	/* Disable TX/RX interrupts */
	writel(0, ioaddr + SXGBE_DMA_CHA_INT_ENABLE_REG(dma_cnum));
}

static void sxgbe_dma_start_tx(void __iomem *ioaddr, int tchannels)
{
	int cnum;
	u32 tx_ctl_reg;

	for (cnum = 0; cnum < tchannels; cnum++) {
		tx_ctl_reg = readl(ioaddr + SXGBE_DMA_CHA_TXCTL_REG(cnum));
		tx_ctl_reg |= SXGBE_TX_ENABLE;
		writel(tx_ctl_reg,
		       ioaddr + SXGBE_DMA_CHA_TXCTL_REG(cnum));
	}
}

static void sxgbe_dma_start_tx_queue(void __iomem *ioaddr, int dma_cnum)
{
	u32 tx_ctl_reg;

	tx_ctl_reg = readl(ioaddr + SXGBE_DMA_CHA_TXCTL_REG(dma_cnum));
	tx_ctl_reg |= SXGBE_TX_ENABLE;
	writel(tx_ctl_reg, ioaddr + SXGBE_DMA_CHA_TXCTL_REG(dma_cnum));
}

static void sxgbe_dma_stop_tx_queue(void __iomem *ioaddr, int dma_cnum)
{
	u32 tx_ctl_reg;

	tx_ctl_reg = readl(ioaddr + SXGBE_DMA_CHA_TXCTL_REG(dma_cnum));
	tx_ctl_reg &= ~(SXGBE_TX_ENABLE);
	writel(tx_ctl_reg, ioaddr + SXGBE_DMA_CHA_TXCTL_REG(dma_cnum));
}

static void sxgbe_dma_stop_tx(void __iomem *ioaddr, int tchannels)
{
	int cnum;
	u32 tx_ctl_reg;

	for (cnum = 0; cnum < tchannels; cnum++) {
		tx_ctl_reg = readl(ioaddr + SXGBE_DMA_CHA_TXCTL_REG(cnum));
		tx_ctl_reg &= ~(SXGBE_TX_ENABLE);
		writel(tx_ctl_reg, ioaddr + SXGBE_DMA_CHA_TXCTL_REG(cnum));
	}
}

static void sxgbe_dma_start_rx(void __iomem *ioaddr, int rchannels)
{
	int cnum;
	u32 rx_ctl_reg;

	for (cnum = 0; cnum < rchannels; cnum++) {
		rx_ctl_reg = readl(ioaddr + SXGBE_DMA_CHA_RXCTL_REG(cnum));
		rx_ctl_reg |= SXGBE_RX_ENABLE;
		writel(rx_ctl_reg,
		       ioaddr + SXGBE_DMA_CHA_RXCTL_REG(cnum));
	}
}

static void sxgbe_dma_stop_rx(void __iomem *ioaddr, int rchannels)
{
	int cnum;
	u32 rx_ctl_reg;

	for (cnum = 0; cnum < rchannels; cnum++) {
		rx_ctl_reg = readl(ioaddr + SXGBE_DMA_CHA_RXCTL_REG(cnum));
		rx_ctl_reg &= ~(SXGBE_RX_ENABLE);
		writel(rx_ctl_reg, ioaddr + SXGBE_DMA_CHA_RXCTL_REG(cnum));
	}
}

static int sxgbe_tx_dma_int_status(void __iomem *ioaddr, int channel_no,
				   struct sxgbe_extra_stats *x)
{
	u32 int_status = readl(ioaddr + SXGBE_DMA_CHA_STATUS_REG(channel_no));
	u32 clear_val = 0;
	u32 ret_val = 0;

	/* TX Normal Interrupt Summary */
	if (likely(int_status & SXGBE_DMA_INT_STATUS_NIS)) {
		x->normal_irq_n++;
		if (int_status & SXGBE_DMA_INT_STATUS_TI) {
			ret_val |= handle_tx;
			x->tx_normal_irq_n++;
			clear_val |= SXGBE_DMA_INT_STATUS_TI;
		}

		if (int_status & SXGBE_DMA_INT_STATUS_TBU) {
			x->tx_underflow_irq++;
			ret_val |= tx_bump_tc;
			clear_val |= SXGBE_DMA_INT_STATUS_TBU;
		}
	} else if (unlikely(int_status & SXGBE_DMA_INT_STATUS_AIS)) {
		/* TX Abnormal Interrupt Summary */
		if (int_status & SXGBE_DMA_INT_STATUS_TPS) {
			ret_val |= tx_hard_error;
			clear_val |= SXGBE_DMA_INT_STATUS_TPS;
			x->tx_process_stopped_irq++;
		}

		if (int_status & SXGBE_DMA_INT_STATUS_FBE) {
			ret_val |= tx_hard_error;
			x->fatal_bus_error_irq++;

			/* Assumption: FBE bit is the combination of
			 * all the bus access erros and cleared when
			 * the respective error bits cleared
			 */

			/* check for actual cause */
			if (int_status & SXGBE_DMA_INT_STATUS_TEB0) {
				x->tx_read_transfer_err++;
				clear_val |= SXGBE_DMA_INT_STATUS_TEB0;
			} else {
				x->tx_write_transfer_err++;
			}

			if (int_status & SXGBE_DMA_INT_STATUS_TEB1) {
				x->tx_desc_access_err++;
				clear_val |= SXGBE_DMA_INT_STATUS_TEB1;
			} else {
				x->tx_buffer_access_err++;
			}

			if (int_status & SXGBE_DMA_INT_STATUS_TEB2) {
				x->tx_data_transfer_err++;
				clear_val |= SXGBE_DMA_INT_STATUS_TEB2;
			}
		}

		/* context descriptor error */
		if (int_status & SXGBE_DMA_INT_STATUS_CTXTERR) {
			x->tx_ctxt_desc_err++;
			clear_val |= SXGBE_DMA_INT_STATUS_CTXTERR;
		}
	}

	/* clear the served bits */
	writel(clear_val, ioaddr + SXGBE_DMA_CHA_STATUS_REG(channel_no));

	return ret_val;
}

static int sxgbe_rx_dma_int_status(void __iomem *ioaddr, int channel_no,
				   struct sxgbe_extra_stats *x)
{
	u32 int_status = readl(ioaddr + SXGBE_DMA_CHA_STATUS_REG(channel_no));
	u32 clear_val = 0;
	u32 ret_val = 0;

	/* RX Normal Interrupt Summary */
	if (likely(int_status & SXGBE_DMA_INT_STATUS_NIS)) {
		x->normal_irq_n++;
		if (int_status & SXGBE_DMA_INT_STATUS_RI) {
			ret_val |= handle_rx;
			x->rx_normal_irq_n++;
			clear_val |= SXGBE_DMA_INT_STATUS_RI;
		}
	} else if (unlikely(int_status & SXGBE_DMA_INT_STATUS_AIS)) {
		/* RX Abnormal Interrupt Summary */
		if (int_status & SXGBE_DMA_INT_STATUS_RBU) {
			ret_val |= rx_bump_tc;
			clear_val |= SXGBE_DMA_INT_STATUS_RBU;
			x->rx_underflow_irq++;
		}

		if (int_status & SXGBE_DMA_INT_STATUS_RPS) {
			ret_val |= rx_hard_error;
			clear_val |= SXGBE_DMA_INT_STATUS_RPS;
			x->rx_process_stopped_irq++;
		}

		if (int_status & SXGBE_DMA_INT_STATUS_FBE) {
			ret_val |= rx_hard_error;
			x->fatal_bus_error_irq++;

			/* Assumption: FBE bit is the combination of
			 * all the bus access erros and cleared when
			 * the respective error bits cleared
			 */

			/* check for actual cause */
			if (int_status & SXGBE_DMA_INT_STATUS_REB0) {
				x->rx_read_transfer_err++;
				clear_val |= SXGBE_DMA_INT_STATUS_REB0;
			} else {
				x->rx_write_transfer_err++;
			}

			if (int_status & SXGBE_DMA_INT_STATUS_REB1) {
				x->rx_desc_access_err++;
				clear_val |= SXGBE_DMA_INT_STATUS_REB1;
			} else {
				x->rx_buffer_access_err++;
			}

			if (int_status & SXGBE_DMA_INT_STATUS_REB2) {
				x->rx_data_transfer_err++;
				clear_val |= SXGBE_DMA_INT_STATUS_REB2;
			}
		}
	}

	/* clear the served bits */
	writel(clear_val, ioaddr + SXGBE_DMA_CHA_STATUS_REG(channel_no));

	return ret_val;
}

/* Program the HW RX Watchdog */
static void sxgbe_dma_rx_watchdog(void __iomem *ioaddr, u32 riwt)
{
	u32 que_num;

	SXGBE_FOR_EACH_QUEUE(SXGBE_RX_QUEUES, que_num) {
		writel(riwt,
		       ioaddr + SXGBE_DMA_CHA_INT_RXWATCHTMR_REG(que_num));
	}
}

static void sxgbe_enable_tso(void __iomem *ioaddr, u8 chan_num)
{
	u32 ctrl;

	ctrl = readl(ioaddr + SXGBE_DMA_CHA_TXCTL_REG(chan_num));
	ctrl |= SXGBE_DMA_CHA_TXCTL_TSE_ENABLE;
	writel(ctrl, ioaddr + SXGBE_DMA_CHA_TXCTL_REG(chan_num));
}

static const struct sxgbe_dma_ops sxgbe_dma_ops = {
	.init				= sxgbe_dma_init,
	.cha_init			= sxgbe_dma_channel_init,
	.enable_dma_transmission	= sxgbe_enable_dma_transmission,
	.enable_dma_irq			= sxgbe_enable_dma_irq,
	.disable_dma_irq		= sxgbe_disable_dma_irq,
	.start_tx			= sxgbe_dma_start_tx,
	.start_tx_queue			= sxgbe_dma_start_tx_queue,
	.stop_tx			= sxgbe_dma_stop_tx,
	.stop_tx_queue			= sxgbe_dma_stop_tx_queue,
	.start_rx			= sxgbe_dma_start_rx,
	.stop_rx			= sxgbe_dma_stop_rx,
	.tx_dma_int_status		= sxgbe_tx_dma_int_status,
	.rx_dma_int_status		= sxgbe_rx_dma_int_status,
	.rx_watchdog			= sxgbe_dma_rx_watchdog,
	.enable_tso			= sxgbe_enable_tso,
};

const struct sxgbe_dma_ops *sxgbe_get_dma_ops(void)
{
	return &sxgbe_dma_ops;
}
