// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>

#include "sfc.h"

static void __iomem *g_sfc_reg;

static void sfc_reset(void)
{
	int timeout = 10000;

	writel(SFC_RESET, g_sfc_reg + SFC_RCVR);
	while ((readl(g_sfc_reg + SFC_RCVR) == SFC_RESET) && (timeout > 0)) {
		sfc_delay(1);
		timeout--;
	}
	writel(0xFFFFFFFF, g_sfc_reg + SFC_ICLR);
}

u16 sfc_get_version(void)
{
	return  (u32)(readl(g_sfc_reg + SFC_VER) & 0xffff);
}

int sfc_init(void __iomem *reg_addr)
{
	g_sfc_reg = reg_addr;
	sfc_reset();
	writel(0, g_sfc_reg + SFC_CTRL);

	return SFC_OK;
}

void sfc_clean_irq(void)
{
	writel(0xFFFFFFFF, g_sfc_reg + SFC_ICLR);
	writel(0xFFFFFFFF, g_sfc_reg + SFC_IMR);
}

int sfc_request(u32 sfcmd, u32 sfctrl, u32 addr, void *data)
{
	int ret = SFC_OK;
	union SFCCMD_DATA cmd;
	int reg;
	int timeout = 0;

	reg = readl(g_sfc_reg + SFC_FSR);
	if (!(reg & SFC_TXEMPTY) || !(reg & SFC_RXEMPTY) ||
	    (readl(g_sfc_reg + SFC_SR) & SFC_BUSY))
		sfc_reset();

	cmd.d32 = sfcmd;
	if (cmd.b.addrbits == SFC_ADDR_XBITS) {
		union SFCCTRL_DATA ctrl;

		ctrl.d32 = sfctrl;
		if (!ctrl.b.addrbits)
			return SFC_PARAM_ERR;
		/* Controller plus 1 automatically */
		writel(ctrl.b.addrbits - 1, g_sfc_reg + SFC_ABIT);
	}
	/* shift in the data at negedge sclk_out */
	sfctrl |= 0x2;

	writel(sfctrl, g_sfc_reg + SFC_CTRL);
	writel(sfcmd, g_sfc_reg + SFC_CMD);
	if (cmd.b.addrbits)
		writel(addr, g_sfc_reg + SFC_ADDR);
	if (!cmd.b.datasize)
		goto exit_wait;
	if (SFC_ENABLE_DMA & sfctrl) {
		unsigned long dma_addr;
		u8 direction = (cmd.b.rw == SFC_WRITE) ? 1 : 0;

		dma_addr = rksfc_dma_map_single((unsigned long)data,
						cmd.b.datasize,
						direction);
		rksfc_irq_flag_init();
		writel(0xFFFFFFFF, g_sfc_reg + SFC_ICLR);
		writel(~(FINISH_INT), g_sfc_reg + SFC_IMR);
		writel((u32)dma_addr, g_sfc_reg + SFC_DMA_ADDR);
		writel(SFC_DMA_START, g_sfc_reg + SFC_DMA_TRIGGER);

		timeout = cmd.b.datasize * 10;
		rksfc_wait_for_irq_completed();
		while ((readl(g_sfc_reg + SFC_SR) & SFC_BUSY) &&
		       (timeout-- > 0))
			sfc_delay(1);
		writel(0xFFFFFFFF, g_sfc_reg + SFC_ICLR);
		if (timeout <= 0)
			ret = SFC_WAIT_TIMEOUT;
		direction = (cmd.b.rw == SFC_WRITE) ? 1 : 0;
		rksfc_dma_unmap_single(dma_addr,
				       cmd.b.datasize,
				       direction);
	} else {
		u32 i, words, count, bytes;
		union SFCFSR_DATA    fifostat;
		u32 *p_data = (u32 *)data;

		if (cmd.b.rw == SFC_WRITE) {
			words  = (cmd.b.datasize + 3) >> 2;
			while (words) {
				fifostat.d32 = readl(g_sfc_reg + SFC_FSR);
				if (fifostat.b.txlevel > 0) {
					count = words < fifostat.b.txlevel ?
						words : fifostat.b.txlevel;
					for (i = 0; i < count; i++) {
						writel(*p_data++,
						       g_sfc_reg + SFC_DATA);
						words--;
					}
					if (words == 0)
						break;
					timeout = 0;
				} else {
					sfc_delay(1);
					if (timeout++ > 10000) {
						ret = SFC_TX_TIMEOUT;
						break;
					}
				}
			}
		} else {
			/* SFC_READ == cmd.b.rw */
			bytes = cmd.b.datasize & 0x3;
			words = cmd.b.datasize >> 2;
			while (words) {
				fifostat.d32 = readl(g_sfc_reg + SFC_FSR);
				if (fifostat.b.rxlevel > 0) {
					u32 count;

					count = words < fifostat.b.rxlevel ?
						words : fifostat.b.rxlevel;

					for (i = 0; i < count; i++) {
						*p_data++ = readl(g_sfc_reg +
								  SFC_DATA);
						words--;
					}
					if (words == 0)
						break;
					timeout = 0;
				} else {
					sfc_delay(1);
					if (timeout++ > 10000) {
						ret = SFC_RX_TIMEOUT;
						break;
					}
				}
			}

			timeout = 0;
			while (bytes) {
				fifostat.d32 = readl(g_sfc_reg + SFC_FSR);
				if (fifostat.b.rxlevel > 0) {
					u8 *p_data1 = (u8 *)p_data;

					words = readl(g_sfc_reg + SFC_DATA);
					for (i = 0; i < bytes; i++)
						p_data1[i] =
						(u8)((words >> (i * 8)) & 0xFF);
					break;
				}

				sfc_delay(1);
				if (timeout++ > 10000) {
					ret = SFC_RX_TIMEOUT;
					break;
				}
			}
		}
	}

exit_wait:
	timeout = 0;    /* wait cmd or data send complete */
	while (!(readl(g_sfc_reg + SFC_FSR) & SFC_TXEMPTY)) {
		sfc_delay(1);
		if (timeout++ > 100000) {         /* wait 100ms */
			ret = SFC_TX_TIMEOUT;
			break;
		}
	}
	sfc_delay(1); /* CS# High Time (read/write) >100ns */
	return ret;
}
