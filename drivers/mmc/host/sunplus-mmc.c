// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (c) Sunplus Inc.
 * Author: Tony Huang <tonyhuang.sunplus@gmail.com>
 * Author: Li-hao Kuo <lhjeff911@gmail.com>
 */

#include <linux/bitfield.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/iopoll.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/pm.h>
#include <linux/pm_runtime.h>
#include <linux/reset.h>

#define SPMMC_MIN_CLK			400000
#define SPMMC_MAX_CLK			52000000
#define SPMMC_MAX_BLK_COUNT		65536
#define SPMMC_MAX_TUNABLE_DLY	7
#define SPMMC_TIMEOUT_US		500000
#define SPMMC_POLL_DELAY_US		10

#define SPMMC_CARD_MEDIATYPE_SRCDST_REG 0x0000
#define SPMMC_MEDIA_TYPE		GENMASK(2, 0)
#define SPMMC_DMA_SOURCE		GENMASK(6, 4)
#define SPMMC_DMA_DESTINATION		GENMASK(10, 8)
#define SPMMC_MEDIA_NONE	0
#define SPMMC_MEDIA_SD		6
#define SPMMC_MEDIA_MS		7

#define SPMMC_SDRAM_SECTOR_0_SIZE_REG	0x0008
#define SPMMC_DMA_BASE_ADDR_REG		0x000C
#define SPMMC_HW_DMA_CTRL_REG		0x0010
#define SPMMC_HW_DMA_RST	BIT(9)
#define SPMMC_DMAIDLE		BIT(10)

#define SPMMC_MAX_DMA_MEMORY_SECTORS	8

#define SPMMC_SDRAM_SECTOR_1_ADDR_REG 0x0018
#define SPMMC_SDRAM_SECTOR_1_LENG_REG 0x001C
#define SPMMC_SDRAM_SECTOR_2_ADDR_REG 0x0020
#define SPMMC_SDRAM_SECTOR_2_LENG_REG 0x0024
#define SPMMC_SDRAM_SECTOR_3_ADDR_REG 0x0028
#define SPMMC_SDRAM_SECTOR_3_LENG_REG 0x002C
#define SPMMC_SDRAM_SECTOR_4_ADDR_REG 0x0030
#define SPMMC_SDRAM_SECTOR_4_LENG_REG 0x0034
#define SPMMC_SDRAM_SECTOR_5_ADDR_REG 0x0038
#define SPMMC_SDRAM_SECTOR_5_LENG_REG 0x003C
#define SPMMC_SDRAM_SECTOR_6_ADDR_REG 0x0040
#define SPMMC_SDRAM_SECTOR_6_LENG_REG 0x0044
#define SPMMC_SDRAM_SECTOR_7_ADDR_REG 0x0048
#define SPMMC_SDRAM_SECTOR_7_LENG_REG 0x004C

#define SPMMC_SD_INT_REG	0x0088
#define SPMMC_SDINT_SDCMPEN	BIT(0)
#define SPMMC_SDINT_SDCMP	BIT(1)
#define SPMMC_SDINT_SDCMPCLR	BIT(2)
#define SPMMC_SDINT_SDIOEN	BIT(3)
#define SPMMC_SDINT_SDIO	BIT(4)
#define SPMMC_SDINT_SDIOCLR	BIT(5)

#define SPMMC_SD_PAGE_NUM_REG	0x008C

#define SPMMC_SD_CONFIG0_REG	0x0090
#define SPMMC_SD_PIO_MODE	BIT(0)
#define SPMMC_SD_DDR_MODE	BIT(1)
#define SPMMC_SD_LEN_MODE	BIT(2)
#define SPMMC_SD_TRANS_MODE	GENMASK(5, 4)
#define SPMMC_SD_AUTO_RESPONSE	BIT(6)
#define SPMMC_SD_CMD_DUMMY	BIT(7)
#define SPMMC_SD_RSP_CHK_EN	BIT(8)
#define SPMMC_SDIO_MODE		BIT(9)
#define SPMMC_SD_MMC_MODE	BIT(10)
#define SPMMC_SD_DATA_WD	BIT(11)
#define SPMMC_RX4_EN		BIT(14)
#define SPMMC_SD_RSP_TYPE	BIT(15)
#define SPMMC_MMC8_EN		BIT(18)
#define SPMMC_CLOCK_DIVISION	GENMASK(31, 20)

#define SPMMC_SDIO_CTRL_REG		0x0094
#define SPMMC_INT_MULTI_TRIG		BIT(6)

#define SPMMC_SD_RST_REG		0x0098
#define SPMMC_SD_CTRL_REG		0x009C
#define SPMMC_NEW_COMMAND_TRIGGER	BIT(0)
#define SPMMC_DUMMY_CLOCK_TRIGGER	BIT(1)

#define SPMMC_SD_STATUS_REG						0x00A0
#define SPMMC_SDSTATUS_DUMMY_READY				BIT(0)
#define SPMMC_SDSTATUS_RSP_BUF_FULL				BIT(1)
#define SPMMC_SDSTATUS_TX_DATA_BUF_EMPTY		BIT(2)
#define SPMMC_SDSTATUS_RX_DATA_BUF_FULL			BIT(3)
#define SPMMC_SDSTATUS_CMD_PIN_STATUS			BIT(4)
#define SPMMC_SDSTATUS_DAT0_PIN_STATUS			BIT(5)
#define SPMMC_SDSTATUS_RSP_TIMEOUT				BIT(6)
#define SPMMC_SDSTATUS_CARD_CRC_CHECK_TIMEOUT	BIT(7)
#define SPMMC_SDSTATUS_STB_TIMEOUT				BIT(8)
#define SPMMC_SDSTATUS_RSP_CRC7_ERROR			BIT(9)
#define SPMMC_SDSTATUS_CRC_TOKEN_CHECK_ERROR	BIT(10)
#define SPMMC_SDSTATUS_RDATA_CRC16_ERROR		BIT(11)
#define SPMMC_SDSTATUS_SUSPEND_STATE_READY		BIT(12)
#define SPMMC_SDSTATUS_BUSY_CYCLE				BIT(13)
#define SPMMC_SDSTATUS_DAT1_PIN_STATUS			BIT(14)
#define SPMMC_SDSTATUS_SD_SENSE_STATUS			BIT(15)
#define SPMMC_SDSTATUS_BOOT_ACK_TIMEOUT			BIT(16)
#define SPMMC_SDSTATUS_BOOT_DATA_TIMEOUT		BIT(17)
#define SPMMC_SDSTATUS_BOOT_ACK_ERROR			BIT(18)

#define SPMMC_SD_STATE_REG		0x00A4
#define SPMMC_CRCTOKEN_CHECK_RESULT	GENMASK(6, 4)
#define SPMMC_SDSTATE_ERROR		BIT(13)
#define SPMMC_SDSTATE_FINISH	BIT(14)

#define SPMMC_SD_HW_STATE_REG		0x00A8
#define SPMMC_SD_BLOCKSIZE_REG		0x00AC

#define SPMMC_SD_CONFIG1_REG		0x00B0
#define SPMMC_TX_DUMMY_NUM		GENMASK(8, 0)
#define SPMMC_SD_HIGH_SPEED_EN		BIT(31)

#define SPMMC_SD_TIMING_CONFIG0_REG 0x00B4
#define SPMMC_SD_CLOCK_DELAY	GENMASK(2, 0)
#define SPMMC_SD_WRITE_DATA_DELAY	GENMASK(6, 4)
#define SPMMC_SD_WRITE_COMMAND_DELAY	GENMASK(10, 8)
#define SPMMC_SD_READ_RESPONSE_DELAY	GENMASK(14, 12)
#define SPMMC_SD_READ_DATA_DELAY	GENMASK(18, 16)
#define SPMMC_SD_READ_CRC_DELAY	GENMASK(22, 20)

#define SPMMC_SD_PIODATATX_REG		0x00BC
#define SPMMC_SD_PIODATARX_REG		0x00C0
#define SPMMC_SD_CMDBUF0_3_REG		0x00C4
#define SPMMC_SD_CMDBUF4_REG		0x00C8
#define SPMMC_SD_RSPBUF0_3_REG		0x00CC
#define SPMMC_SD_RSPBUF4_5_REG		0x00D0

#define SPMMC_MAX_RETRIES (8 * 8)

struct spmmc_tuning_info {
	int enable_tuning;
	int need_tuning;
	int retried; /* how many times has been retried */
	u32 rd_crc_dly:3;
	u32 rd_dat_dly:3;
	u32 rd_rsp_dly:3;
	u32 wr_cmd_dly:3;
	u32 wr_dat_dly:3;
	u32 clk_dly:3;
};

#define SPMMC_DMA_MODE 0
#define	SPMMC_PIO_MODE 1

struct spmmc_host {
	void __iomem *base;
	struct clk *clk;
	struct reset_control *rstc;
	struct mmc_host *mmc;
	struct mmc_request *mrq; /* current mrq */
	int irq;
	int dmapio_mode;
	struct spmmc_tuning_info tuning_info;
	int dma_int_threshold;
	int dma_use_int;
};

static inline int spmmc_wait_finish(struct spmmc_host *host)
{
	u32 state;

	return readl_poll_timeout(host->base + SPMMC_SD_STATE_REG, state,
					(state & SPMMC_SDSTATE_FINISH),
					SPMMC_POLL_DELAY_US, SPMMC_TIMEOUT_US);
}

static inline int spmmc_wait_sdstatus(struct spmmc_host *host, unsigned int status_bit)
{
	u32 status;

	return readl_poll_timeout(host->base + SPMMC_SD_STATUS_REG, status,
					(status & status_bit),
					SPMMC_POLL_DELAY_US, SPMMC_TIMEOUT_US);
}

#define spmmc_wait_rspbuf_full(host) spmmc_wait_sdstatus(host, SPMMC_SDSTATUS_RSP_BUF_FULL)
#define spmmc_wait_rxbuf_full(host) spmmc_wait_sdstatus(host, SPMMC_SDSTATUS_RX_DATA_BUF_FULL)
#define spmmc_wait_txbuf_empty(host) spmmc_wait_sdstatus(host, SPMMC_SDSTATUS_TX_DATA_BUF_EMPTY)

static void spmmc_get_rsp(struct spmmc_host *host, struct mmc_command *cmd)
{
	u32 value0_3, value4_5;

	if (!(cmd->flags & MMC_RSP_PRESENT))
		return;
	if (cmd->flags & MMC_RSP_136) {
		if (spmmc_wait_rspbuf_full(host))
			return;
		value0_3 = readl(host->base + SPMMC_SD_RSPBUF0_3_REG);
		value4_5 = readl(host->base + SPMMC_SD_RSPBUF4_5_REG) & 0xffff;
		cmd->resp[0] = (value0_3 << 8) | (value4_5 >> 8);
		cmd->resp[1] = value4_5 << 24;
		value0_3 = readl(host->base + SPMMC_SD_RSPBUF0_3_REG);
		value4_5 = readl(host->base + SPMMC_SD_RSPBUF4_5_REG) & 0xffff;
		cmd->resp[1] |= value0_3 >> 8;
		cmd->resp[2] = value0_3 << 24;
		cmd->resp[2] |= value4_5 << 8;
		value0_3 = readl(host->base + SPMMC_SD_RSPBUF0_3_REG);
		value4_5 = readl(host->base + SPMMC_SD_RSPBUF4_5_REG) & 0xffff;
		cmd->resp[2] |= value0_3 >> 24;
		cmd->resp[3] = value0_3 << 8;
		cmd->resp[3] |= value4_5 >> 8;
	} else {
		if (spmmc_wait_rspbuf_full(host))
			return;
		value0_3 = readl(host->base + SPMMC_SD_RSPBUF0_3_REG);
		value4_5 = readl(host->base + SPMMC_SD_RSPBUF4_5_REG) & 0xffff;
		cmd->resp[0] = (value0_3 << 8) | (value4_5 >> 8);
		cmd->resp[1] = value4_5 << 24;
	}
}

static void spmmc_set_bus_clk(struct spmmc_host *host, int clk)
{
	unsigned int clkdiv;
	int f_min = host->mmc->f_min;
	int f_max = host->mmc->f_max;
	u32 value = readl(host->base + SPMMC_SD_CONFIG0_REG);

	if (clk < f_min)
		clk = f_min;
	if (clk > f_max)
		clk = f_max;

	clkdiv = (clk_get_rate(host->clk) + clk) / clk - 1;
	if (clkdiv > 0xfff)
		clkdiv = 0xfff;
	value &= ~SPMMC_CLOCK_DIVISION;
	value |= FIELD_PREP(SPMMC_CLOCK_DIVISION, clkdiv);
	writel(value, host->base + SPMMC_SD_CONFIG0_REG);
}

static void spmmc_set_bus_timing(struct spmmc_host *host, unsigned int timing)
{
	u32 value = readl(host->base + SPMMC_SD_CONFIG1_REG);
	int clkdiv = FIELD_GET(SPMMC_CLOCK_DIVISION, readl(host->base + SPMMC_SD_CONFIG0_REG));
	int delay = clkdiv / 2 < 7 ? clkdiv / 2 : 7;
	int hs_en = 1, ddr_enabled = 0;

	switch (timing) {
	case MMC_TIMING_LEGACY:
		hs_en = 0;
		break;
	case MMC_TIMING_MMC_HS:
	case MMC_TIMING_SD_HS:
	case MMC_TIMING_UHS_SDR50:
	case MMC_TIMING_UHS_SDR104:
	case MMC_TIMING_MMC_HS200:
		hs_en = 1;
		break;
	case MMC_TIMING_UHS_DDR50:
		ddr_enabled = 1;
		break;
	case MMC_TIMING_MMC_DDR52:
		ddr_enabled = 1;
		break;
	default:
		hs_en = 0;
		break;
	}

	if (hs_en) {
		value |= SPMMC_SD_HIGH_SPEED_EN;
		writel(value, host->base + SPMMC_SD_CONFIG1_REG);
		value = readl(host->base + SPMMC_SD_TIMING_CONFIG0_REG);
		value &= ~SPMMC_SD_WRITE_DATA_DELAY;
		value |= FIELD_PREP(SPMMC_SD_WRITE_DATA_DELAY, delay);
		value &= ~SPMMC_SD_WRITE_COMMAND_DELAY;
		value |= FIELD_PREP(SPMMC_SD_WRITE_COMMAND_DELAY, delay);
		writel(value, host->base + SPMMC_SD_TIMING_CONFIG0_REG);
	} else {
		value &= ~SPMMC_SD_HIGH_SPEED_EN;
		writel(value, host->base + SPMMC_SD_CONFIG1_REG);
	}
	if (ddr_enabled) {
		value = readl(host->base + SPMMC_SD_CONFIG0_REG);
		value |= SPMMC_SD_DDR_MODE;
		writel(value, host->base + SPMMC_SD_CONFIG0_REG);
	} else {
		value = readl(host->base + SPMMC_SD_CONFIG0_REG);
		value &= ~SPMMC_SD_DDR_MODE;
		writel(value, host->base + SPMMC_SD_CONFIG0_REG);
	}
}

static void spmmc_set_bus_width(struct spmmc_host *host, int width)
{
	u32 value = readl(host->base + SPMMC_SD_CONFIG0_REG);

	switch (width) {
	case MMC_BUS_WIDTH_8:
		value &= ~SPMMC_SD_DATA_WD;
		value |= SPMMC_MMC8_EN;
		break;
	case MMC_BUS_WIDTH_4:
		value |= SPMMC_SD_DATA_WD;
		value &= ~SPMMC_MMC8_EN;
		break;
	default:
		value &= ~SPMMC_SD_DATA_WD;
		value &= ~SPMMC_MMC8_EN;
		break;
	}
	writel(value, host->base + SPMMC_SD_CONFIG0_REG);
}

/*
 * select the working mode of controller: sd/sdio/emmc
 */
static void spmmc_set_sdmmc_mode(struct spmmc_host *host)
{
	u32 value = readl(host->base + SPMMC_SD_CONFIG0_REG);

	value |= SPMMC_SD_MMC_MODE;
	value &= ~SPMMC_SDIO_MODE;
	writel(value, host->base + SPMMC_SD_CONFIG0_REG);
}

static void spmmc_sw_reset(struct spmmc_host *host)
{
	u32 value;

	/*
	 * Must reset dma operation first, or it will
	 * be stuck on sd_state == 0x1c00 because of
	 * a controller software reset bug
	 */
	value = readl(host->base + SPMMC_HW_DMA_CTRL_REG);
	value |= SPMMC_DMAIDLE;
	writel(value, host->base + SPMMC_HW_DMA_CTRL_REG);
	value &= ~SPMMC_DMAIDLE;
	writel(value, host->base + SPMMC_HW_DMA_CTRL_REG);
	value = readl(host->base + SPMMC_HW_DMA_CTRL_REG);
	value |= SPMMC_HW_DMA_RST;
	writel(value, host->base + SPMMC_HW_DMA_CTRL_REG);
	writel(0x7, host->base + SPMMC_SD_RST_REG);
	readl_poll_timeout_atomic(host->base + SPMMC_SD_HW_STATE_REG, value,
				  !(value & BIT(6)), 1, SPMMC_TIMEOUT_US);
}

static void spmmc_prepare_cmd(struct spmmc_host *host, struct mmc_command *cmd)
{
	u32 value;

	/* add start bit, according to spec, command format */
	value = ((cmd->opcode | 0x40) << 24) | (cmd->arg >> 8);
	writel(value, host->base + SPMMC_SD_CMDBUF0_3_REG);
	writeb(cmd->arg & 0xff, host->base + SPMMC_SD_CMDBUF4_REG);

	/* disable interrupt if needed */
	value = readl(host->base + SPMMC_SD_INT_REG);
	value |= SPMMC_SDINT_SDCMPCLR;
	value &= ~SPMMC_SDINT_SDCMPEN;
	writel(value, host->base + SPMMC_SD_INT_REG);

	value = readl(host->base + SPMMC_SD_CONFIG0_REG);
	value &= ~SPMMC_SD_TRANS_MODE;
	value |= SPMMC_SD_CMD_DUMMY;
	if (cmd->flags & MMC_RSP_PRESENT) {
		value |= SPMMC_SD_AUTO_RESPONSE;
	} else {
		value &= ~SPMMC_SD_AUTO_RESPONSE;
		writel(value, host->base + SPMMC_SD_CONFIG0_REG);

		return;
	}
	/*
	 * Currently, host is not capable of checking R2's CRC7,
	 * thus, enable crc7 check only for 48 bit response commands
	 */
	if (cmd->flags & MMC_RSP_CRC && !(cmd->flags & MMC_RSP_136))
		value |= SPMMC_SD_RSP_CHK_EN;
	else
		value &= ~SPMMC_SD_RSP_CHK_EN;

	if (cmd->flags & MMC_RSP_136)
		value |= SPMMC_SD_RSP_TYPE;
	else
		value &= ~SPMMC_SD_RSP_TYPE;
	writel(value, host->base + SPMMC_SD_CONFIG0_REG);
}

static void spmmc_prepare_data(struct spmmc_host *host, struct mmc_data *data)
{
	u32 value, srcdst;

	writel(data->blocks - 1, host->base + SPMMC_SD_PAGE_NUM_REG);
	writel(data->blksz - 1, host->base + SPMMC_SD_BLOCKSIZE_REG);
	value = readl(host->base + SPMMC_SD_CONFIG0_REG);
	if (data->flags & MMC_DATA_READ) {
		value &= ~SPMMC_SD_TRANS_MODE;
		value |= FIELD_PREP(SPMMC_SD_TRANS_MODE, 2);
		value &= ~SPMMC_SD_AUTO_RESPONSE;
		value &= ~SPMMC_SD_CMD_DUMMY;
		srcdst = readl(host->base + SPMMC_CARD_MEDIATYPE_SRCDST_REG);
		srcdst &= ~SPMMC_DMA_SOURCE;
		srcdst |= FIELD_PREP(SPMMC_DMA_SOURCE, 0x2);
		srcdst &= ~SPMMC_DMA_DESTINATION;
		srcdst |= FIELD_PREP(SPMMC_DMA_DESTINATION, 0x1);
		writel(srcdst, host->base + SPMMC_CARD_MEDIATYPE_SRCDST_REG);
	} else {
		value &= ~SPMMC_SD_TRANS_MODE;
		value |= FIELD_PREP(SPMMC_SD_TRANS_MODE, 1);
		srcdst = readl(host->base + SPMMC_CARD_MEDIATYPE_SRCDST_REG);
		srcdst &= ~SPMMC_DMA_SOURCE;
		srcdst |= FIELD_PREP(SPMMC_DMA_SOURCE, 0x1);
		srcdst &= ~SPMMC_DMA_DESTINATION;
		srcdst |= FIELD_PREP(SPMMC_DMA_DESTINATION, 0x2);
		writel(srcdst, host->base + SPMMC_CARD_MEDIATYPE_SRCDST_REG);
	}

	value |= SPMMC_SD_LEN_MODE;
	if (host->dmapio_mode == SPMMC_DMA_MODE) {
		struct scatterlist *sg;
		dma_addr_t dma_addr;
		unsigned int dma_size;
		int i, count = 1;

		count = dma_map_sg(host->mmc->parent, data->sg, data->sg_len,
				   mmc_get_dma_dir(data));
		if (!count || count > SPMMC_MAX_DMA_MEMORY_SECTORS) {
			data->error = -EINVAL;

			return;
		}
		for_each_sg(data->sg, sg, count, i) {
			dma_addr = sg_dma_address(sg);
			dma_size = sg_dma_len(sg) / data->blksz - 1;
			if (i == 0) {
				writel(dma_addr, host->base + SPMMC_DMA_BASE_ADDR_REG);
				writel(dma_size, host->base + SPMMC_SDRAM_SECTOR_0_SIZE_REG);
			} else if (i == 1) {
				writel(dma_addr, host->base + SPMMC_SDRAM_SECTOR_1_ADDR_REG);
				writel(dma_size, host->base + SPMMC_SDRAM_SECTOR_1_LENG_REG);
			} else if (i == 2) {
				writel(dma_addr, host->base + SPMMC_SDRAM_SECTOR_2_ADDR_REG);
				writel(dma_size, host->base + SPMMC_SDRAM_SECTOR_2_LENG_REG);
			} else if (i == 3) {
				writel(dma_addr, host->base + SPMMC_SDRAM_SECTOR_3_ADDR_REG);
				writel(dma_size, host->base + SPMMC_SDRAM_SECTOR_3_LENG_REG);
			} else if (i == 4) {
				writel(dma_addr, host->base + SPMMC_SDRAM_SECTOR_4_ADDR_REG);
				writel(dma_size, host->base + SPMMC_SDRAM_SECTOR_4_LENG_REG);
			} else if (i == 5) {
				writel(dma_addr, host->base + SPMMC_SDRAM_SECTOR_5_ADDR_REG);
				writel(dma_size, host->base + SPMMC_SDRAM_SECTOR_5_LENG_REG);
			} else if (i == 6) {
				writel(dma_addr, host->base + SPMMC_SDRAM_SECTOR_6_ADDR_REG);
				writel(dma_size, host->base + SPMMC_SDRAM_SECTOR_6_LENG_REG);
			} else if (i == 7) {
				writel(dma_addr, host->base + SPMMC_SDRAM_SECTOR_7_ADDR_REG);
				writel(dma_size, host->base + SPMMC_SDRAM_SECTOR_7_LENG_REG);
			}
		}
		value &= ~SPMMC_SD_PIO_MODE;
		writel(value, host->base + SPMMC_SD_CONFIG0_REG);
		/* enable interrupt if needed */
		if (data->blksz * data->blocks > host->dma_int_threshold) {
			host->dma_use_int = 1;
			value = readl(host->base + SPMMC_SD_INT_REG);
			value &= ~SPMMC_SDINT_SDCMPEN;
			value |= FIELD_PREP(SPMMC_SDINT_SDCMPEN, 1); /* sdcmpen */
			writel(value, host->base + SPMMC_SD_INT_REG);
		}
	} else {
		value |= SPMMC_SD_PIO_MODE;
		value |= SPMMC_RX4_EN;
		writel(value, host->base + SPMMC_SD_CONFIG0_REG);
	}
}

static inline void spmmc_trigger_transaction(struct spmmc_host *host)
{
	u32 value = readl(host->base + SPMMC_SD_CTRL_REG);

	value |= SPMMC_NEW_COMMAND_TRIGGER;
	writel(value, host->base + SPMMC_SD_CTRL_REG);
}

static void spmmc_send_stop_cmd(struct spmmc_host *host)
{
	struct mmc_command stop = {};
	u32 value;

	stop.opcode = MMC_STOP_TRANSMISSION;
	stop.arg = 0;
	stop.flags = MMC_RSP_R1B;
	spmmc_prepare_cmd(host, &stop);
	value = readl(host->base + SPMMC_SD_INT_REG);
	value &= ~SPMMC_SDINT_SDCMPEN;
	value |= FIELD_PREP(SPMMC_SDINT_SDCMPEN, 0);
	writel(value, host->base + SPMMC_SD_INT_REG);
	spmmc_trigger_transaction(host);
	readl_poll_timeout(host->base + SPMMC_SD_STATE_REG, value,
			   (value & SPMMC_SDSTATE_FINISH), 1, SPMMC_TIMEOUT_US);
}

static int spmmc_check_error(struct spmmc_host *host, struct mmc_request *mrq)
{
	int ret = 0;
	struct mmc_command *cmd = mrq->cmd;
	struct mmc_data *data = mrq->data;

	u32 value = readl(host->base + SPMMC_SD_STATE_REG);
	u32 crc_token = FIELD_GET(SPMMC_CRCTOKEN_CHECK_RESULT, value);

	if (value & SPMMC_SDSTATE_ERROR) {
		u32 timing_cfg0 = 0;

		value = readl(host->base + SPMMC_SD_STATUS_REG);

		if (host->tuning_info.enable_tuning) {
			timing_cfg0 = readl(host->base + SPMMC_SD_TIMING_CONFIG0_REG);
			host->tuning_info.rd_crc_dly = FIELD_GET(SPMMC_SD_READ_CRC_DELAY,
								 timing_cfg0);
			host->tuning_info.rd_dat_dly = FIELD_GET(SPMMC_SD_READ_DATA_DELAY,
								 timing_cfg0);
			host->tuning_info.rd_rsp_dly = FIELD_GET(SPMMC_SD_READ_RESPONSE_DELAY,
								 timing_cfg0);
			host->tuning_info.wr_cmd_dly = FIELD_GET(SPMMC_SD_WRITE_COMMAND_DELAY,
								 timing_cfg0);
			host->tuning_info.wr_dat_dly = FIELD_GET(SPMMC_SD_WRITE_DATA_DELAY,
								 timing_cfg0);
		}

		if (value & SPMMC_SDSTATUS_RSP_TIMEOUT) {
			ret = -ETIMEDOUT;
			host->tuning_info.wr_cmd_dly++;
		} else if (value & SPMMC_SDSTATUS_RSP_CRC7_ERROR) {
			ret = -EILSEQ;
			host->tuning_info.rd_rsp_dly++;
		} else if (data) {
			if ((value & SPMMC_SDSTATUS_STB_TIMEOUT)) {
				ret = -ETIMEDOUT;
				host->tuning_info.rd_dat_dly++;
			} else if (value & SPMMC_SDSTATUS_RDATA_CRC16_ERROR) {
				ret = -EILSEQ;
				host->tuning_info.rd_dat_dly++;
			} else if (value & SPMMC_SDSTATUS_CARD_CRC_CHECK_TIMEOUT) {
				ret = -ETIMEDOUT;
				host->tuning_info.rd_crc_dly++;
			} else if (value & SPMMC_SDSTATUS_CRC_TOKEN_CHECK_ERROR) {
				ret = -EILSEQ;
				if (crc_token == 0x5)
					host->tuning_info.wr_dat_dly++;
				else
					host->tuning_info.rd_crc_dly++;
			}
		}
		cmd->error = ret;
		if (data) {
			data->error = ret;
			data->bytes_xfered = 0;
		}
		if (!host->tuning_info.need_tuning && host->tuning_info.enable_tuning)
			cmd->retries = SPMMC_MAX_RETRIES;
		spmmc_sw_reset(host);

		if (host->tuning_info.enable_tuning) {
			timing_cfg0 &= ~SPMMC_SD_READ_CRC_DELAY;
			timing_cfg0 |= FIELD_PREP(SPMMC_SD_READ_CRC_DELAY,
						       host->tuning_info.rd_crc_dly);
			timing_cfg0 &= ~SPMMC_SD_READ_DATA_DELAY;
			timing_cfg0 |= FIELD_PREP(SPMMC_SD_READ_DATA_DELAY,
						       host->tuning_info.rd_dat_dly);
			timing_cfg0 &= ~SPMMC_SD_READ_RESPONSE_DELAY;
			timing_cfg0 |= FIELD_PREP(SPMMC_SD_READ_RESPONSE_DELAY,
						       host->tuning_info.rd_rsp_dly);
			timing_cfg0 &= ~SPMMC_SD_WRITE_COMMAND_DELAY;
			timing_cfg0 |= FIELD_PREP(SPMMC_SD_WRITE_COMMAND_DELAY,
						       host->tuning_info.wr_cmd_dly);
			timing_cfg0 &= ~SPMMC_SD_WRITE_DATA_DELAY;
			timing_cfg0 |= FIELD_PREP(SPMMC_SD_WRITE_DATA_DELAY,
						       host->tuning_info.wr_dat_dly);
			writel(timing_cfg0, host->base + SPMMC_SD_TIMING_CONFIG0_REG);
		}
	} else if (data) {
		data->error = 0;
		data->bytes_xfered = data->blocks * data->blksz;
	}
	host->tuning_info.need_tuning = ret;

	return ret;
}

/*
 * the strategy is:
 * 1. if several continuous delays are acceptable, we choose a middle one;
 * 2. otherwise, we choose the first one.
 */
static inline int spmmc_find_best_delay(u8 candidate_dly)
{
	int f, w, value;

	if (!candidate_dly)
		return 0;
	f = ffs(candidate_dly) - 1;
	w = hweight8(candidate_dly);
	value = ((1 << w) - 1) << f;
	if (0xff == (value & ~candidate_dly))
		return (f + w / 2);
	else
		return (f);
}

static void spmmc_xfer_data_pio(struct spmmc_host *host, struct mmc_data *data)
{
	u32 *buf;
	int data_left = data->blocks * data->blksz;
	int consumed, remain;

	struct sg_mapping_iter sg_miter;
	unsigned int flags = 0;

	if (data->flags & MMC_DATA_WRITE)
		flags |= SG_MITER_FROM_SG;
	else
		flags |= SG_MITER_TO_SG;
	sg_miter_start(&sg_miter, data->sg, data->sg_len, flags);
	while (data_left > 0) {
		consumed = 0;
		if (!sg_miter_next(&sg_miter))
			break;
		buf = sg_miter.addr;
		remain = sg_miter.length;
		do {
			if (data->flags & MMC_DATA_WRITE) {
				if (spmmc_wait_txbuf_empty(host))
					goto done;
				writel(*buf, host->base + SPMMC_SD_PIODATATX_REG);
			} else {
				if (spmmc_wait_rxbuf_full(host))
					goto done;
				*buf = readl(host->base + SPMMC_SD_PIODATARX_REG);
			}
			buf++;
			/* tx/rx 4 bytes one time in pio mode */
			consumed += 4;
			remain -= 4;
		} while (remain);
		sg_miter.consumed = consumed;
		data_left -= consumed;
	}
done:
	sg_miter_stop(&sg_miter);
}

static void spmmc_controller_init(struct spmmc_host *host)
{
	u32 value;
	int ret = reset_control_assert(host->rstc);

	if (!ret) {
		usleep_range(1000, 1250);
		ret = reset_control_deassert(host->rstc);
	}

	value = readl(host->base + SPMMC_CARD_MEDIATYPE_SRCDST_REG);
	value &= ~SPMMC_MEDIA_TYPE;
	value |= FIELD_PREP(SPMMC_MEDIA_TYPE, SPMMC_MEDIA_SD);
	writel(value, host->base + SPMMC_CARD_MEDIATYPE_SRCDST_REG);
}

/*
 * 1. unmap scatterlist if needed;
 * 2. get response & check error conditions;
 * 3. notify mmc layer the request is done
 */
static void spmmc_finish_request(struct spmmc_host *host, struct mmc_request *mrq)
{
	struct mmc_command *cmd;
	struct mmc_data *data;

	if (!mrq)
		return;

	cmd = mrq->cmd;
	data = mrq->data;

	if (data && SPMMC_DMA_MODE == host->dmapio_mode) {
		dma_unmap_sg(host->mmc->parent, data->sg, data->sg_len, mmc_get_dma_dir(data));
		host->dma_use_int = 0;
	}

	spmmc_get_rsp(host, cmd);
	spmmc_check_error(host, mrq);
	if (mrq->stop)
		spmmc_send_stop_cmd(host);

	host->mrq = NULL;
	mmc_request_done(host->mmc, mrq);
}

/* Interrupt Service Routine */
static irqreturn_t spmmc_irq(int irq, void *dev_id)
{
	struct spmmc_host *host = dev_id;
	u32 value = readl(host->base + SPMMC_SD_INT_REG);

	if ((value & SPMMC_SDINT_SDCMP) && (value & SPMMC_SDINT_SDCMPEN)) {
		value &= ~SPMMC_SDINT_SDCMPEN;
		value |= SPMMC_SDINT_SDCMPCLR;
		writel(value, host->base + SPMMC_SD_INT_REG);
		return IRQ_WAKE_THREAD;
	}
	return IRQ_HANDLED;
}

static void spmmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct spmmc_host *host = mmc_priv(mmc);
	struct mmc_data *data;
	struct mmc_command *cmd;

	host->mrq = mrq;
	data = mrq->data;
	cmd = mrq->cmd;

	spmmc_prepare_cmd(host, cmd);
	/* we need manually read response R2. */
	if (cmd->flags & MMC_RSP_136) {
		spmmc_trigger_transaction(host);
		spmmc_get_rsp(host, cmd);
		spmmc_wait_finish(host);
		spmmc_check_error(host, mrq);
		host->mrq = NULL;
		mmc_request_done(host->mmc, mrq);
	} else {
		if (data)
			spmmc_prepare_data(host, data);

		if (host->dmapio_mode == SPMMC_PIO_MODE && data) {
			u32 value;
			/* pio data transfer do not use interrupt */
			value = readl(host->base + SPMMC_SD_INT_REG);
			value &= ~SPMMC_SDINT_SDCMPEN;
			writel(value, host->base + SPMMC_SD_INT_REG);
			spmmc_trigger_transaction(host);
			spmmc_xfer_data_pio(host, data);
			spmmc_wait_finish(host);
			spmmc_finish_request(host, mrq);
		} else {
			if (host->dma_use_int) {
				spmmc_trigger_transaction(host);
			} else {
				spmmc_trigger_transaction(host);
				spmmc_wait_finish(host);
				spmmc_finish_request(host, mrq);
			}
		}
	}
}

static void spmmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct spmmc_host *host = (struct spmmc_host *)mmc_priv(mmc);

	spmmc_set_bus_clk(host, ios->clock);
	spmmc_set_bus_timing(host, ios->timing);
	spmmc_set_bus_width(host, ios->bus_width);
	/* ensure mode is correct, because we might have hw reset the controller */
	spmmc_set_sdmmc_mode(host);
}

/*
 * Return values for the get_cd callback should be:
 *   0 for a absent card
 *   1 for a present card
 *   -ENOSYS when not supported (equal to NULL callback)
 *   or a negative errno value when something bad happened
 */
static int spmmc_get_cd(struct mmc_host *mmc)
{
	int ret = 0;

	if (mmc_can_gpio_cd(mmc))
		ret = mmc_gpio_get_cd(mmc);

	if (ret < 0)
		ret = 0;

	return ret;
}

static int spmmc_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct spmmc_host *host = mmc_priv(mmc);
	u8 smpl_dly = 0, candidate_dly = 0;
	u32 value;

	host->tuning_info.enable_tuning = 0;
	do {
		value = readl(host->base + SPMMC_SD_TIMING_CONFIG0_REG);
		value &= ~SPMMC_SD_READ_RESPONSE_DELAY;
		value |= FIELD_PREP(SPMMC_SD_READ_RESPONSE_DELAY, smpl_dly);
		value &= ~SPMMC_SD_READ_DATA_DELAY;
		value |= FIELD_PREP(SPMMC_SD_READ_DATA_DELAY, smpl_dly);
		value &= ~SPMMC_SD_READ_CRC_DELAY;
		value |= FIELD_PREP(SPMMC_SD_READ_CRC_DELAY, smpl_dly);
		writel(value, host->base + SPMMC_SD_TIMING_CONFIG0_REG);

		if (!mmc_send_tuning(mmc, opcode, NULL)) {
			candidate_dly |= (1 << smpl_dly);
			break;
		}
	} while (smpl_dly++ <= SPMMC_MAX_TUNABLE_DLY);
	host->tuning_info.enable_tuning = 1;

	if (candidate_dly) {
		smpl_dly = spmmc_find_best_delay(candidate_dly);
		value = readl(host->base + SPMMC_SD_TIMING_CONFIG0_REG);
		value &= ~SPMMC_SD_READ_RESPONSE_DELAY;
		value |= FIELD_PREP(SPMMC_SD_READ_RESPONSE_DELAY, smpl_dly);
		value &= ~SPMMC_SD_READ_DATA_DELAY;
		value |= FIELD_PREP(SPMMC_SD_READ_DATA_DELAY, smpl_dly);
		value &= ~SPMMC_SD_READ_CRC_DELAY;
		value |= FIELD_PREP(SPMMC_SD_READ_CRC_DELAY, smpl_dly);
		writel(value, host->base + SPMMC_SD_TIMING_CONFIG0_REG);
		return 0;
	}

	return -EIO;
}

static const struct mmc_host_ops spmmc_ops = {
	.request = spmmc_request,
	.set_ios = spmmc_set_ios,
	.get_cd = spmmc_get_cd,
	.execute_tuning = spmmc_execute_tuning,
};

static irqreturn_t spmmc_func_finish_req(int irq, void *dev_id)
{
	struct spmmc_host *host = dev_id;

	spmmc_finish_request(host, host->mrq);

	return IRQ_HANDLED;
}

static int spmmc_drv_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct resource *res;
	struct spmmc_host *host;
	int ret = 0;

	mmc = devm_mmc_alloc_host(&pdev->dev, sizeof(struct spmmc_host));
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->dmapio_mode = SPMMC_DMA_MODE;
	host->dma_int_threshold = 1024;

	host->base = devm_platform_get_and_ioremap_resource(pdev, 0, &res);
	if (IS_ERR(host->base))
		return PTR_ERR(host->base);

	host->clk = devm_clk_get(&pdev->dev, NULL);
	if (IS_ERR(host->clk))
		return dev_err_probe(&pdev->dev, PTR_ERR(host->clk), "clk get fail\n");

	host->rstc = devm_reset_control_get_exclusive(&pdev->dev, NULL);
	if (IS_ERR(host->rstc))
		return dev_err_probe(&pdev->dev, PTR_ERR(host->rstc), "rst get fail\n");

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0)
		return host->irq;

	ret = devm_request_threaded_irq(&pdev->dev, host->irq,
					spmmc_irq, spmmc_func_finish_req, IRQF_SHARED,
			NULL, host);
	if (ret)
		return ret;

	ret = clk_prepare_enable(host->clk);
	if (ret)
		return dev_err_probe(&pdev->dev, ret, "failed to enable clk\n");

	ret = mmc_of_parse(mmc);
	if (ret)
		goto clk_disable;

	mmc->ops = &spmmc_ops;
	mmc->f_min = SPMMC_MIN_CLK;
	if (mmc->f_max > SPMMC_MAX_CLK)
		mmc->f_max = SPMMC_MAX_CLK;

	ret = mmc_regulator_get_supply(mmc);
	if (ret)
		goto clk_disable;

	if (!mmc->ocr_avail)
		mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->max_seg_size = SPMMC_MAX_BLK_COUNT * 512;
	mmc->max_segs = SPMMC_MAX_DMA_MEMORY_SECTORS;
	mmc->max_req_size = SPMMC_MAX_BLK_COUNT * 512;
	mmc->max_blk_size = 512;
	mmc->max_blk_count = SPMMC_MAX_BLK_COUNT;

	dev_set_drvdata(&pdev->dev, host);
	spmmc_controller_init(host);
	spmmc_set_sdmmc_mode(host);
	host->tuning_info.enable_tuning = 1;
	pm_runtime_set_active(&pdev->dev);
	pm_runtime_enable(&pdev->dev);
	ret = mmc_add_host(mmc);
	if (ret)
		goto pm_disable;

	return 0;

pm_disable:
	pm_runtime_disable(&pdev->dev);

clk_disable:
	clk_disable_unprepare(host->clk);
	return ret;
}

static void spmmc_drv_remove(struct platform_device *dev)
{
	struct spmmc_host *host = platform_get_drvdata(dev);

	mmc_remove_host(host->mmc);
	pm_runtime_get_sync(&dev->dev);
	clk_disable_unprepare(host->clk);
	pm_runtime_put_noidle(&dev->dev);
	pm_runtime_disable(&dev->dev);
}

static int spmmc_pm_runtime_suspend(struct device *dev)
{
	struct spmmc_host *host;

	host = dev_get_drvdata(dev);
	clk_disable_unprepare(host->clk);

	return 0;
}

static int spmmc_pm_runtime_resume(struct device *dev)
{
	struct spmmc_host *host;

	host = dev_get_drvdata(dev);

	return clk_prepare_enable(host->clk);
}

static DEFINE_RUNTIME_DEV_PM_OPS(spmmc_pm_ops, spmmc_pm_runtime_suspend,
							spmmc_pm_runtime_resume, NULL);

static const struct of_device_id spmmc_of_table[] = {
	{
		.compatible = "sunplus,sp7021-mmc",
	},
	{/* sentinel */}
};
MODULE_DEVICE_TABLE(of, spmmc_of_table);

static struct platform_driver spmmc_driver = {
	.probe = spmmc_drv_probe,
	.remove_new = spmmc_drv_remove,
	.driver = {
		.name = "spmmc",
		.pm = pm_ptr(&spmmc_pm_ops),
		.of_match_table = spmmc_of_table,
	},
};
module_platform_driver(spmmc_driver);

MODULE_AUTHOR("Tony Huang <tonyhuang.sunplus@gmail.com>");
MODULE_AUTHOR("Li-hao Kuo <lhjeff911@gmail.com>");
MODULE_DESCRIPTION("Sunplus MMC controller driver");
MODULE_LICENSE("GPL");
