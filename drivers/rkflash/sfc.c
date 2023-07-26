// SPDX-License-Identifier: GPL-2.0

/* Copyright (c) 2018 Rockchip Electronics Co. Ltd. */

#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/kernel.h>

#include "sfc.h"

#define SFC_MAX_IOSIZE_VER3		(1024 * 8)
#define SFC_MAX_IOSIZE_VER4		(0xFFFFFFFF)

static void __iomem *g_sfc_reg;
static u32 sfc_version;

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

u32 sfc_get_max_iosize(void)
{
	if (sfc_get_version() >= SFC_VER_4)
		return SFC_MAX_IOSIZE_VER4;
	else
		return SFC_MAX_IOSIZE_VER3;
}

u32 sfc_get_max_dll_cells(void)
{
	switch (sfc_get_version()) {
	case SFC_VER_8:
	case SFC_VER_6:
	case SFC_VER_5:
		return SCLK_SMP_SEL_MAX_V5;
	case SFC_VER_4:
		return SCLK_SMP_SEL_MAX_V4;
	default:
		return 0;
	}
}

void sfc_set_delay_lines(u16 cells)
{
	u16 cell_max = (u16)sfc_get_max_dll_cells();
	u32 val = 0;

	if (cells > cell_max)
		cells = cell_max;

	if (cells)
		val = SCLK_SMP_SEL_EN | cells;

	writel(val, g_sfc_reg + SFC_DLL_CTRL0);
}

int sfc_init(void __iomem *reg_addr)
{
	g_sfc_reg = reg_addr;
	writel(0, g_sfc_reg + SFC_CTRL);

	if (sfc_get_version() >= SFC_VER_4)
		writel(1, g_sfc_reg + SFC_LEN_CTRL);
	sfc_version = sfc_get_version();

	return SFC_OK;
}

void sfc_clean_irq(void)
{
	writel(0xFFFFFFFF, g_sfc_reg + SFC_ICLR);
	writel(0xFFFFFFFF, g_sfc_reg + SFC_IMR);
}

int sfc_request(struct rk_sfc_op *op, u32 addr, void *data, u32 size)
{
	int ret = SFC_OK;
	union SFCCMD_DATA cmd;
	int reg;
	int timeout = 0;

	reg = readl(g_sfc_reg + SFC_FSR);

	if (!(reg & SFC_TXEMPTY) || !(reg & SFC_RXEMPTY) ||
	    (readl(g_sfc_reg + SFC_SR) & SFC_BUSY))
		sfc_reset();

	cmd.d32 = op->sfcmd.d32;

	if (cmd.b.addrbits == SFC_ADDR_XBITS) {
		union SFCCTRL_DATA ctrl;

		ctrl.d32 = op->sfctrl.d32;

		if (!ctrl.b.addrbits)
			return SFC_PARAM_ERR;

		/* Controller plus 1 automatically */
		writel(ctrl.b.addrbits - 1, g_sfc_reg + SFC_ABIT);
	}

	/* shift in the data at negedge sclk_out */
	op->sfctrl.d32 |= 0x2;
	cmd.b.datasize = size;

	if (sfc_version >= SFC_VER_4)
		writel(size, g_sfc_reg + SFC_LEN_EXT);
	else
		cmd.b.datasize = size;

	writel(op->sfctrl.d32, g_sfc_reg + SFC_CTRL);
	writel(cmd.d32, g_sfc_reg + SFC_CMD);

	if (cmd.b.addrbits)
		writel(addr, g_sfc_reg + SFC_ADDR);

	if (!size)
		goto exit_wait;
	if (op->sfctrl.b.enbledma) {
		unsigned long dma_addr;
		u8 direction = (cmd.b.rw == SFC_WRITE) ? 1 : 0;

		dma_addr = rksfc_dma_map_single((unsigned long)data,
						size,
						direction);
		rksfc_irq_flag_init();
		writel(0xFFFFFFFF, g_sfc_reg + SFC_ICLR);
		writel(~((u32)DMA_INT), g_sfc_reg + SFC_IMR);
		writel((u32)dma_addr, g_sfc_reg + SFC_DMA_ADDR);
		writel(SFC_DMA_START, g_sfc_reg + SFC_DMA_TRIGGER);

		rksfc_wait_for_irq_completed();
		timeout = size * 10;
		while ((readl(g_sfc_reg + SFC_SR) & SFC_BUSY) &&
		       (timeout-- > 0))
			sfc_delay(1);
		if (timeout <= 0)
			ret = SFC_WAIT_TIMEOUT;
		direction = (cmd.b.rw == SFC_WRITE) ? 1 : 0;
		rksfc_dma_unmap_single(dma_addr,
				       size,
				       direction);
	} else {
		u32 i, words, count, bytes;
		union SFCFSR_DATA    fifostat;
		u32 *p_data = (u32 *)data;

		if (cmd.b.rw == SFC_WRITE) {
			words  = (size + 3) >> 2;

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
			bytes = size & 0x3;
			words = size >> 2;

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

				if (!bytes)
					break;
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

	while (readl(g_sfc_reg + SFC_SR) & SFC_BUSY) {
		sfc_delay(1);

		if (timeout++ > 100000) {         /* wait 100ms */
			ret = SFC_TX_TIMEOUT;
			break;
		}
	}

	sfc_delay(1); /* CS# High Time (read/write) >100ns */
	return ret;
}
