// SPDX-License-Identifier: GPL-2.0-only
/*
 * Loongson-2K MMC/SDIO controller driver
 *
 * Copyright (C) 2018-2025 Loongson Technology Corporation Limited.
 *
 */

#include <linux/bitfield.h>
#include <linux/bitrev.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#define LOONGSON2_MMC_REG_CTL		0x00 /* Control Register */
#define LOONGSON2_MMC_REG_PRE		0x04 /* Prescaler Register */
#define LOONGSON2_MMC_REG_CARG		0x08 /* Command Register */
#define LOONGSON2_MMC_REG_CCTL		0x0c /* Command Control Register */
#define LOONGSON2_MMC_REG_CSTS		0x10 /* Command Status Register */
#define LOONGSON2_MMC_REG_RSP0		0x14 /* Command Response Register 0 */
#define LOONGSON2_MMC_REG_RSP1		0x18 /* Command Response Register 1 */
#define LOONGSON2_MMC_REG_RSP2		0x1c /* Command Response Register 2 */
#define LOONGSON2_MMC_REG_RSP3		0x20 /* Command Response Register 3 */
#define LOONGSON2_MMC_REG_TIMER		0x24 /* Data Timeout Register */
#define LOONGSON2_MMC_REG_BSIZE		0x28 /* Block Size Register */
#define LOONGSON2_MMC_REG_DCTL		0x2c /* Data Control Register */
#define LOONGSON2_MMC_REG_DCNT		0x30 /* Data Counter Register */
#define LOONGSON2_MMC_REG_DSTS		0x34 /* Data Status Register */
#define LOONGSON2_MMC_REG_FSTS		0x38 /* FIFO Status Register */
#define LOONGSON2_MMC_REG_INT		0x3c /* Interrupt Register */
#define LOONGSON2_MMC_REG_DATA		0x40 /* Data Register */
#define LOONGSON2_MMC_REG_IEN		0x64 /* Interrupt Enable Register */

/* EMMC DLL Mode Registers */
#define LOONGSON2_MMC_REG_DLLVAL	0xf0 /* DLL Master Lock-value Register */
#define LOONGSON2_MMC_REG_DLLCTL	0xf4 /* DLL Control Register */
#define LOONGSON2_MMC_REG_DELAY		0xf8 /* DLL Delayed Parameter Register */
#define LOONGSON2_MMC_REG_SEL		0xfc /* Bus Mode Selection Register */

/* Exclusive DMA R/W Registers */
#define LOONGSON2_MMC_REG_WDMA_LO	0x400
#define LOONGSON2_MMC_REG_WDMA_HI	0x404
#define LOONGSON2_MMC_REG_RDMA_LO	0x800
#define LOONGSON2_MMC_REG_RDMA_HI	0x804

/* Bitfields of control register */
#define LOONGSON2_MMC_CTL_ENCLK		BIT(0)
#define LOONGSON2_MMC_CTL_EXTCLK	BIT(1)
#define LOONGSON2_MMC_CTL_RESET		BIT(8)

/* Bitfields of prescaler register */
#define LOONGSON2_MMC_PRE		GENMASK(9, 0)
#define LOONGSON2_MMC_PRE_EN		BIT(31)

/* Bitfields of command control register */
#define LOONGSON2_MMC_CCTL_INDEX	GENMASK(5, 0)
#define LOONGSON2_MMC_CCTL_HOST		BIT(6)
#define LOONGSON2_MMC_CCTL_START	BIT(8)
#define LOONGSON2_MMC_CCTL_WAIT_RSP	BIT(9)
#define LOONGSON2_MMC_CCTL_LONG_RSP	BIT(10)
#define LOONGSON2_MMC_CCTL_ABORT	BIT(12)
#define LOONGSON2_MMC_CCTL_CHECK	BIT(13)
#define LOONGSON2_MMC_CCTL_SDIO		BIT(14)
#define LOONGSON2_MMC_CCTL_CMD6		BIT(18)

/* Bitfields of command status register */
#define LOONGSON2_MMC_CSTS_INDEX	GENMASK(7, 0)
#define LOONGSON2_MMC_CSTS_ON		BIT(8)
#define LOONGSON2_MMC_CSTS_RSP		BIT(9)
#define LOONGSON2_MMC_CSTS_TIMEOUT	BIT(10)
#define LOONGSON2_MMC_CSTS_END		BIT(11)
#define LOONGSON2_MMC_CSTS_CRC_ERR	BIT(12)
#define LOONGSON2_MMC_CSTS_AUTO_STOP	BIT(13)
#define LOONGSON2_MMC_CSTS_FIN		BIT(14)

/* Bitfields of data timeout register */
#define LOONGSON2_MMC_DTIMR		GENMASK(23, 0)

/* Bitfields of block size register */
#define LOONGSON2_MMC_BSIZE		GENMASK(11, 0)

/* Bitfields of data control register */
#define LOONGSON2_MMC_DCTL_BNUM		GENMASK(11, 0)
#define LOONGSON2_MMC_DCTL_START	BIT(14)
#define LOONGSON2_MMC_DCTL_ENDMA	BIT(15)
#define LOONGSON2_MMC_DCTL_WIDE		BIT(16)
#define LOONGSON2_MMC_DCTL_RWAIT	BIT(17)
#define LOONGSON2_MMC_DCTL_IO_SUSPEND	BIT(18)
#define LOONGSON2_MMC_DCTL_IO_RESUME	BIT(19)
#define LOONGSON2_MMC_DCTL_RW_RESUME	BIT(20)
#define LOONGSON2_MMC_DCTL_8BIT_BUS	BIT(26)

/* Bitfields of sata counter register */
#define LOONGSON2_MMC_DCNT_BNUM		GENMASK(11, 0)
#define LOONGSON2_MMC_DCNT_BYTE		GENMASK(23, 12)

/* Bitfields of command status register */
#define LOONGSON2_MMC_DSTS_RXON		BIT(0)
#define LOONGSON2_MMC_DSTS_TXON		BIT(1)
#define LOONGSON2_MMC_DSTS_SBITERR	BIT(2)
#define LOONGSON2_MMC_DSTS_BUSYFIN	BIT(3)
#define LOONGSON2_MMC_DSTS_XFERFIN	BIT(4)
#define LOONGSON2_MMC_DSTS_DTIMEOUT	BIT(5)
#define LOONGSON2_MMC_DSTS_RXCRC	BIT(6)
#define LOONGSON2_MMC_DSTS_TXCRC	BIT(7)
#define LOONGSON2_MMC_DSTS_IRQ		BIT(8)
#define LOONGSON2_MMC_DSTS_START	BIT(13)
#define LOONGSON2_MMC_DSTS_RESUME	BIT(15)
#define LOONGSON2_MMC_DSTS_SUSPEND	BIT(16)

/* Bitfields of FIFO Status Register */
#define LOONGSON2_MMC_FSTS_TXFULL	BIT(11)

/* Bitfields of interrupt register */
#define LOONGSON2_MMC_INT_DFIN		BIT(0)
#define LOONGSON2_MMC_INT_DTIMEOUT	BIT(1)
#define LOONGSON2_MMC_INT_RXCRC		BIT(2)
#define LOONGSON2_MMC_INT_TXCRC		BIT(3)
#define LOONGSON2_MMC_INT_PROGERR	BIT(4)
#define LOONGSON2_MMC_INT_SDIOIRQ	BIT(5)
#define LOONGSON2_MMC_INT_CSENT		BIT(6)
#define LOONGSON2_MMC_INT_CTIMEOUT	BIT(7)
#define LOONGSON2_MMC_INT_RESPCRC	BIT(8)
#define LOONGSON2_MMC_INT_BUSYEND	BIT(9)

/* Bitfields of interrupt enable register */
#define LOONGSON2_MMC_IEN_DFIN		BIT(0)
#define LOONGSON2_MMC_IEN_DTIMEOUT	BIT(1)
#define LOONGSON2_MMC_IEN_RXCRC		BIT(2)
#define LOONGSON2_MMC_IEN_TXCRC		BIT(3)
#define LOONGSON2_MMC_IEN_PROGERR	BIT(4)
#define LOONGSON2_MMC_IEN_SDIOIRQ	BIT(5)
#define LOONGSON2_MMC_IEN_CSENT		BIT(6)
#define LOONGSON2_MMC_IEN_CTIMEOUT	BIT(7)
#define LOONGSON2_MMC_IEN_RESPCRC	BIT(8)
#define LOONGSON2_MMC_IEN_BUSYEND	BIT(9)

#define LOONGSON2_MMC_IEN_ALL		GENMASK(9, 0)
#define LOONGSON2_MMC_INT_CLEAR		GENMASK(9, 0)

/* Bitfields of DLL master lock-value register */
#define LOONGSON2_MMC_DLLVAL_DONE	BIT(8)

/* Bitfields of DLL control register */
#define LOONGSON2_MMC_DLLCTL_TIME	GENMASK(7, 0)
#define LOONGSON2_MMC_DLLCTL_INCRE	GENMASK(15, 8)
#define LOONGSON2_MMC_DLLCTL_START	GENMASK(23, 16)
#define LOONGSON2_MMC_DLLCTL_CLK_MODE	BIT(24)
#define LOONGSON2_MMC_DLLCTL_START_BIT	BIT(25)
#define LOONGSON2_MMC_DLLCTL_TIME_BPASS	GENMASK(29, 26)

#define LOONGSON2_MMC_DELAY_PAD		GENMASK(7, 0)
#define LOONGSON2_MMC_DELAY_RD		GENMASK(15, 8)

#define LOONGSON2_MMC_SEL_DATA		BIT(0)	/* 0: SDR, 1: DDR */
#define LOONGSON2_MMC_SEL_BUS		BIT(0)	/* 0: EMMC, 1: SDIO */

/* Internal dma controller registers */

/* Bitfields of Global Configuration Register */
#define LOONGSON2_MMC_DMA_64BIT_EN	BIT(0) /* 1: 64 bit support */
#define LOONGSON2_MMC_DMA_UNCOHERENT_EN	BIT(1) /* 0: cache, 1: uncache */
#define LOONGSON2_MMC_DMA_ASK_VALID	BIT(2)
#define LOONGSON2_MMC_DMA_START		BIT(3) /* DMA start operation */
#define LOONGSON2_MMC_DMA_STOP		BIT(4) /* DMA stop operation */
#define LOONGSON2_MMC_DMA_CONFIG_MASK	GENMASK_ULL(4, 0) /* DMA controller config bits mask */

/* Bitfields of ndesc_addr field of HW descriptor */
#define LOONGSON2_MMC_DMA_DESC_EN	BIT(0) /*1: The next descriptor is valid */
#define LOONGSON2_MMC_DMA_DESC_ADDR_LOW	GENMASK(31, 1)

/* Bitfields of cmd field of HW descriptor */
#define LOONGSON2_MMC_DMA_INT		BIT(1)	/* Enable DMA interrupts */
#define LOONGSON2_MMC_DMA_DATA_DIR	BIT(12) /* 1: write to device, 0: read from device */

#define LOONGSON2_MMC_DLLVAL_TIMEOUT_US		4000
#define LOONGSON2_MMC_TXFULL_TIMEOUT_US		500

/* Loongson-2K1000 SDIO2 DMA routing register */
#define LS2K1000_SDIO_DMA_MASK		GENMASK(17, 15)
#define LS2K1000_DMA0_CONF		0x0
#define LS2K1000_DMA1_CONF		0x1
#define LS2K1000_DMA2_CONF		0x2
#define LS2K1000_DMA3_CONF		0x3
#define LS2K1000_DMA4_CONF		0x4

/* Loongson-2K0500 SDIO2 DMA routing register */
#define LS2K0500_SDIO_DMA_MASK		GENMASK(15, 14)
#define LS2K0500_DMA0_CONF		0x1
#define LS2K0500_DMA1_CONF		0x2
#define LS2K0500_DMA2_CONF		0x3

enum loongson2_mmc_state {
	STATE_NONE,
	STATE_FINALIZE,
	STATE_CMDSENT,
	STATE_RSPFIN,
	STATE_XFERFINISH,
	STATE_XFERFINISH_RSPFIN,
};

struct loongson2_dma_desc {
	u32 ndesc_addr;
	u32 mem_addr;
	u32 apb_addr;
	u32 len;
	u32 step_len;
	u32 step_times;
	u32 cmd;
	u32 stats;
	u32 high_ndesc_addr;
	u32 high_mem_addr;
	u32 reserved[2];
} __packed;

struct loongson2_mmc_host {
	struct device *dev;
	struct mmc_request *mrq;
	struct regmap *regmap;
	struct resource *res;
	struct clk *clk;
	u32 current_clk;
	void *sg_cpu;
	dma_addr_t sg_dma;
	int dma_complete;
	struct dma_chan *chan;
	int cmd_is_stop;
	int bus_width;
	spinlock_t lock; /* Prevent races with irq handler */
	enum loongson2_mmc_state state;
	const struct loongson2_mmc_pdata *pdata;
};

struct loongson2_mmc_pdata {
	const struct regmap_config *regmap_config;
	void (*reorder_cmd_data)(struct loongson2_mmc_host *host, struct mmc_command *cmd);
	void (*fix_data_timeout)(struct loongson2_mmc_host *host, struct mmc_command *cmd);
	int (*setting_dma)(struct loongson2_mmc_host *host, struct platform_device *pdev);
	int (*prepare_dma)(struct loongson2_mmc_host *host, struct mmc_data *data);
	void (*release_dma)(struct loongson2_mmc_host *host, struct device *dev);
};

static void loongson2_mmc_send_command(struct loongson2_mmc_host *host,
				       struct mmc_command *cmd)
{
	u32 cctrl;

	if (cmd->data)
		host->state = STATE_XFERFINISH_RSPFIN;
	else if (cmd->flags & MMC_RSP_PRESENT)
		host->state = STATE_RSPFIN;
	else
		host->state = STATE_CMDSENT;

	regmap_write(host->regmap, LOONGSON2_MMC_REG_CARG, cmd->arg);

	cctrl = FIELD_PREP(LOONGSON2_MMC_CCTL_INDEX, cmd->opcode);
	cctrl |= LOONGSON2_MMC_CCTL_HOST | LOONGSON2_MMC_CCTL_START;

	if (cmd->opcode == SD_SWITCH && cmd->data)
		cctrl |= LOONGSON2_MMC_CCTL_CMD6;

	if (cmd->flags & MMC_RSP_PRESENT)
		cctrl |= LOONGSON2_MMC_CCTL_WAIT_RSP;

	if (cmd->flags & MMC_RSP_136)
		cctrl |= LOONGSON2_MMC_CCTL_LONG_RSP;

	regmap_write(host->regmap, LOONGSON2_MMC_REG_CCTL, cctrl);
}

static int loongson2_mmc_setup_data(struct loongson2_mmc_host *host,
				    struct mmc_data *data)
{
	u32 dctrl;

	if ((data->blksz & 3) != 0)
		return -EINVAL;

	dctrl = FIELD_PREP(LOONGSON2_MMC_DCTL_BNUM, data->blocks);
	dctrl |= LOONGSON2_MMC_DCTL_START | LOONGSON2_MMC_DCTL_ENDMA;

	if (host->bus_width == MMC_BUS_WIDTH_4)
		dctrl |= LOONGSON2_MMC_DCTL_WIDE;
	else if (host->bus_width == MMC_BUS_WIDTH_8)
		dctrl |= LOONGSON2_MMC_DCTL_8BIT_BUS;

	regmap_write(host->regmap, LOONGSON2_MMC_REG_DCTL, dctrl);
	regmap_write(host->regmap, LOONGSON2_MMC_REG_BSIZE, data->blksz);
	regmap_write(host->regmap, LOONGSON2_MMC_REG_TIMER, U32_MAX);

	return 0;
}

static int loongson2_mmc_prepare_dma(struct loongson2_mmc_host *host,
				     struct mmc_data *data)
{
	int ret;

	if (!data)
		return 0;

	ret = loongson2_mmc_setup_data(host, data);
	if (ret)
		return ret;

	host->dma_complete = 0;

	return host->pdata->prepare_dma(host, data);
}

static void loongson2_mmc_send_request(struct mmc_host *mmc)
{
	int ret;
	struct loongson2_mmc_host *host = mmc_priv(mmc);
	struct mmc_request *mrq = host->mrq;
	struct mmc_command *cmd = host->cmd_is_stop ? mrq->stop : mrq->cmd;

	ret = loongson2_mmc_prepare_dma(host, cmd->data);
	if (ret) {
		dev_err(host->dev, "DMA data prepared failed with %d\n", ret);
		cmd->error = ret;
		cmd->data->error = ret;
		mmc_request_done(mmc, mrq);
		return;
	}

	if (host->pdata->fix_data_timeout)
		host->pdata->fix_data_timeout(host, cmd);

	loongson2_mmc_send_command(host, cmd);

	/* Fix deselect card */
	if (cmd->opcode == MMC_SELECT_CARD && cmd->arg == 0) {
		cmd->error = 0;
		mmc_request_done(mmc, mrq);
	}
}

static irqreturn_t loongson2_mmc_irq_worker(int irq, void *devid)
{
	struct loongson2_mmc_host *host = (struct loongson2_mmc_host *)devid;
	struct mmc_host *mmc = mmc_from_priv(host);
	struct mmc_request *mrq = host->mrq;
	struct mmc_command *cmd = host->cmd_is_stop ? mrq->stop : mrq->cmd;

	if (cmd->data)
		dma_unmap_sg(mmc_dev(mmc), cmd->data->sg, cmd->data->sg_len,
			     mmc_get_dma_dir(cmd->data));

	if (cmd->data && !cmd->error &&
	    !cmd->data->error && !host->dma_complete)
		return IRQ_HANDLED;

	/* Read response from controller. */
	regmap_read(host->regmap, LOONGSON2_MMC_REG_RSP0, &cmd->resp[0]);
	regmap_read(host->regmap, LOONGSON2_MMC_REG_RSP1, &cmd->resp[1]);
	regmap_read(host->regmap, LOONGSON2_MMC_REG_RSP2, &cmd->resp[2]);
	regmap_read(host->regmap, LOONGSON2_MMC_REG_RSP3, &cmd->resp[3]);

	/* Cleanup controller */
	regmap_write(host->regmap, LOONGSON2_MMC_REG_CARG, 0);
	regmap_write(host->regmap, LOONGSON2_MMC_REG_CCTL, 0);

	if (cmd->data && cmd->error)
		cmd->data->error = cmd->error;

	if (cmd->data && cmd->data->stop && !host->cmd_is_stop) {
		host->cmd_is_stop = 1;
		loongson2_mmc_send_request(mmc);
		return IRQ_HANDLED;
	}

	/* If we have no data transfer we are finished here */
	if (!mrq->data)
		goto request_done;

	/* Calculate the amount of bytes transfer if there was no error */
	if (mrq->data->error == 0) {
		mrq->data->bytes_xfered =
			(mrq->data->blocks * mrq->data->blksz);
	} else {
		mrq->data->bytes_xfered = 0;
	}

request_done:
	host->state = STATE_NONE;
	host->mrq = NULL;
	mmc_request_done(mmc, mrq);
	return IRQ_HANDLED;
}

static irqreturn_t loongson2_mmc_irq(int irq, void *devid)
{
	struct loongson2_mmc_host *host = (struct loongson2_mmc_host *)devid;
	struct mmc_host *mmc = mmc_from_priv(host);
	struct mmc_command *cmd;
	unsigned long iflags;
	u32 dsts, imsk;

	regmap_read(host->regmap, LOONGSON2_MMC_REG_INT, &imsk);
	regmap_read(host->regmap, LOONGSON2_MMC_REG_DSTS, &dsts);

	if ((dsts & LOONGSON2_MMC_DSTS_IRQ) &&
	    (imsk & LOONGSON2_MMC_INT_SDIOIRQ)) {
		regmap_update_bits(host->regmap, LOONGSON2_MMC_REG_INT,
				   LOONGSON2_MMC_INT_SDIOIRQ, LOONGSON2_MMC_INT_SDIOIRQ);

		sdio_signal_irq(mmc);
		return IRQ_HANDLED;
	}

	spin_lock_irqsave(&host->lock, iflags);

	if (host->state == STATE_NONE || host->state == STATE_FINALIZE || !host->mrq)
		goto irq_out;

	cmd = host->cmd_is_stop ? host->mrq->stop : host->mrq->cmd;
	if (!cmd)
		goto irq_out;

	cmd->error = 0;

	if (imsk & LOONGSON2_MMC_INT_CTIMEOUT) {
		cmd->error = -ETIMEDOUT;
		goto close_transfer;
	}

	if (imsk & LOONGSON2_MMC_INT_CSENT) {
		if (host->state == STATE_RSPFIN || host->state == STATE_CMDSENT)
			goto close_transfer;

		if (host->state == STATE_XFERFINISH_RSPFIN)
			host->state = STATE_XFERFINISH;
	}

	if (!cmd->data)
		goto irq_out;

	if (imsk & (LOONGSON2_MMC_INT_RXCRC | LOONGSON2_MMC_INT_TXCRC)) {
		cmd->data->error = -EILSEQ;
		goto close_transfer;
	}

	if (imsk & LOONGSON2_MMC_INT_DTIMEOUT) {
		cmd->data->error = -ETIMEDOUT;
		goto close_transfer;
	}

	if (imsk & LOONGSON2_MMC_INT_DFIN) {
		if (host->state == STATE_XFERFINISH) {
			host->dma_complete = 1;
			goto close_transfer;
		}

		if (host->state == STATE_XFERFINISH_RSPFIN)
			host->state = STATE_RSPFIN;
	}

irq_out:
	regmap_write(host->regmap, LOONGSON2_MMC_REG_INT, imsk);
	spin_unlock_irqrestore(&host->lock, iflags);
	return IRQ_HANDLED;

close_transfer:
	host->state = STATE_FINALIZE;
	host->pdata->reorder_cmd_data(host, cmd);
	regmap_write(host->regmap, LOONGSON2_MMC_REG_INT, imsk);
	spin_unlock_irqrestore(&host->lock, iflags);
	return IRQ_WAKE_THREAD;
}

static void loongson2_mmc_dll_mode_init(struct loongson2_mmc_host *host)
{
	u32 val, pad_delay, delay;
	int ret;

	regmap_update_bits(host->regmap, LOONGSON2_MMC_REG_SEL,
			   LOONGSON2_MMC_SEL_DATA, LOONGSON2_MMC_SEL_DATA);

	val = FIELD_PREP(LOONGSON2_MMC_DLLCTL_TIME, 0xc8)
	    | FIELD_PREP(LOONGSON2_MMC_DLLCTL_INCRE, 0x1)
	    | FIELD_PREP(LOONGSON2_MMC_DLLCTL_START, 0x1)
	    | FIELD_PREP(LOONGSON2_MMC_DLLCTL_CLK_MODE, 0x1)
	    | FIELD_PREP(LOONGSON2_MMC_DLLCTL_START_BIT, 0x1)
	    | FIELD_PREP(LOONGSON2_MMC_DLLCTL_TIME_BPASS, 0xf);

	regmap_write(host->regmap, LOONGSON2_MMC_REG_DLLCTL, val);

	ret = regmap_read_poll_timeout(host->regmap, LOONGSON2_MMC_REG_DLLVAL, val,
				       (val & LOONGSON2_MMC_DLLVAL_DONE), 0,
				       LOONGSON2_MMC_DLLVAL_TIMEOUT_US);
	if (ret < 0)
		return;

	regmap_read(host->regmap, LOONGSON2_MMC_REG_DLLVAL, &val);
	pad_delay = FIELD_GET(GENMASK(7, 1), val);

	delay = FIELD_PREP(LOONGSON2_MMC_DELAY_PAD, pad_delay)
	      | FIELD_PREP(LOONGSON2_MMC_DELAY_RD, pad_delay + 1);

	regmap_write(host->regmap, LOONGSON2_MMC_REG_DELAY, delay);
}

static void loongson2_mmc_set_clk(struct loongson2_mmc_host *host, struct mmc_ios *ios)
{
	u32 pre;

	pre = DIV_ROUND_UP(host->current_clk, ios->clock);
	if (pre > 255)
		pre = 255;

	regmap_write(host->regmap, LOONGSON2_MMC_REG_PRE, pre | LOONGSON2_MMC_PRE_EN);

	regmap_update_bits(host->regmap, LOONGSON2_MMC_REG_CTL,
			   LOONGSON2_MMC_CTL_ENCLK, LOONGSON2_MMC_CTL_ENCLK);

	/* EMMC DLL mode setting */
	if (ios->timing == MMC_TIMING_UHS_DDR50 || ios->timing == MMC_TIMING_MMC_DDR52)
		loongson2_mmc_dll_mode_init(host);
}

static void loongson2_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct loongson2_mmc_host *host = mmc_priv(mmc);
	int ret;

	if (ios->power_mode == MMC_POWER_UP) {
		if (!IS_ERR(mmc->supply.vmmc)) {
			ret = mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, ios->vdd);
			if (ret) {
				dev_err(host->dev, "failed to enable vmmc regulator\n");
				return; /* return, if failed turn on vmmc */
			}
		}
		regmap_write(host->regmap, LOONGSON2_MMC_REG_CTL, LOONGSON2_MMC_CTL_RESET);
		mdelay(10);
		regmap_write(host->regmap, LOONGSON2_MMC_REG_CTL, LOONGSON2_MMC_CTL_EXTCLK);
		regmap_write(host->regmap, LOONGSON2_MMC_REG_INT, LOONGSON2_MMC_IEN_ALL);
		regmap_write(host->regmap, LOONGSON2_MMC_REG_IEN, LOONGSON2_MMC_INT_CLEAR);
	} else if (ios->power_mode == MMC_POWER_OFF) {
		regmap_update_bits(host->regmap, LOONGSON2_MMC_REG_CTL,
				   LOONGSON2_MMC_CTL_RESET, LOONGSON2_MMC_CTL_RESET);
		if (!IS_ERR(mmc->supply.vmmc))
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, 0);
		return;
	}

	loongson2_mmc_set_clk(host, ios);

	host->bus_width = ios->bus_width;
}

static void loongson2_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct loongson2_mmc_host *host = mmc_priv(mmc);

	host->cmd_is_stop = 0;
	host->mrq = mrq;
	loongson2_mmc_send_request(mmc);
}

static void loongson2_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct loongson2_mmc_host *host = mmc_priv(mmc);

	regmap_update_bits(host->regmap, LOONGSON2_MMC_REG_IEN, LOONGSON2_MMC_INT_SDIOIRQ, enable);
}

static void loongson2_mmc_ack_sdio_irq(struct mmc_host *mmc)
{
	loongson2_mmc_enable_sdio_irq(mmc, 1);
}

static struct mmc_host_ops loongson2_mmc_ops = {
	.request	= loongson2_mmc_request,
	.set_ios	= loongson2_mmc_set_ios,
	.get_ro		= mmc_gpio_get_ro,
	.get_cd		= mmc_gpio_get_cd,
	.enable_sdio_irq = loongson2_mmc_enable_sdio_irq,
	.ack_sdio_irq	= loongson2_mmc_ack_sdio_irq,
};

static const struct regmap_config ls2k0500_mmc_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = LOONGSON2_MMC_REG_IEN,
};

static int loongson2_reorder_cmd_list[] = { SD_APP_SEND_SCR, SD_APP_SEND_NUM_WR_BLKS,
					    SD_APP_SD_STATUS, MMC_SEND_WRITE_PROT, SD_SWITCH };

/*
 * According to SD spec, ACMD13, ACMD22, ACMD51 and CMD30
 * response datas has different byte order with usual data packets.
 * However sdio controller will send these datas in usual data format,
 * so we need to adjust these datas to a protocol consistent byte order.
 */
static void ls2k0500_mmc_reorder_cmd_data(struct loongson2_mmc_host *host,
					  struct mmc_command *cmd)
{
	struct scatterlist *sg;
	u32 *data;
	int i, j;

	if (mmc_cmd_type(cmd) != MMC_CMD_ADTC)
		return;

	for (i = 0; i < ARRAY_SIZE(loongson2_reorder_cmd_list); i++)
		if (cmd->opcode == loongson2_reorder_cmd_list[i])
			break;

	if (i == ARRAY_SIZE(loongson2_reorder_cmd_list))
		return;

	for_each_sg(cmd->data->sg, sg, cmd->data->sg_len, i) {
		data = sg_virt(&sg[i]);
		for (j = 0; j < (sg_dma_len(&sg[i]) / 4); j++)
			if (cmd->opcode == SD_SWITCH)
				data[j] = bitrev8x4(data[j]);
			else
				data[j] = (__force u32)cpu_to_be32(data[j]);
	}
}

static int loongson2_mmc_prepare_external_dma(struct loongson2_mmc_host *host,
					      struct mmc_data *data)
{
	struct mmc_host *mmc = mmc_from_priv(host);
	struct dma_slave_config dma_conf = { };
	struct dma_async_tx_descriptor *desc;
	int ret;

	ret = dma_map_sg(mmc_dev(mmc), data->sg, data->sg_len,
			 mmc_get_dma_dir(data));
	if (!ret)
		return -ENOMEM;

	dma_conf.src_addr = host->res->start + LOONGSON2_MMC_REG_DATA,
	dma_conf.dst_addr = host->res->start + LOONGSON2_MMC_REG_DATA,
	dma_conf.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
	dma_conf.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES,
	dma_conf.direction = !(data->flags & MMC_DATA_WRITE) ? DMA_DEV_TO_MEM : DMA_MEM_TO_DEV;

	dmaengine_slave_config(host->chan, &dma_conf);
	desc = dmaengine_prep_slave_sg(host->chan, data->sg, data->sg_len,
				       dma_conf.direction,
				       DMA_CTRL_ACK | DMA_PREP_INTERRUPT);
	if (!desc)
		goto unmap_exit;

	dmaengine_submit(desc);
	dma_async_issue_pending(host->chan);

	return 0;

unmap_exit:
	dma_unmap_sg(mmc_dev(mmc), data->sg, data->sg_len, mmc_get_dma_dir(data));
	return -ENOMEM;
}

static void loongson2_mmc_release_external_dma(struct loongson2_mmc_host *host,
					       struct device *dev)
{
	dma_release_channel(host->chan);
}

static int ls2k0500_mmc_set_external_dma(struct loongson2_mmc_host *host,
					 struct platform_device *pdev)
{
	int ret, val;
	void __iomem *regs;

	regs = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	val = readl(regs);
	val |= FIELD_PREP(LS2K0500_SDIO_DMA_MASK, LS2K0500_DMA2_CONF);
	writel(val, regs);

	host->chan = dma_request_chan(&pdev->dev, "rx-tx");
	ret = PTR_ERR_OR_ZERO(host->chan);
	if (ret) {
		dev_err(&pdev->dev, "Cannot get DMA channel.\n");
		return ret;
	}

	return 0;
}

static struct loongson2_mmc_pdata ls2k0500_mmc_pdata = {
	.regmap_config		= &ls2k0500_mmc_regmap_config,
	.reorder_cmd_data	= ls2k0500_mmc_reorder_cmd_data,
	.setting_dma		= ls2k0500_mmc_set_external_dma,
	.prepare_dma		= loongson2_mmc_prepare_external_dma,
	.release_dma		= loongson2_mmc_release_external_dma,
};

static int ls2k1000_mmc_set_external_dma(struct loongson2_mmc_host *host,
					 struct platform_device *pdev)
{
	int ret, val;
	void __iomem *regs;

	regs = devm_platform_ioremap_resource(pdev, 1);
	if (IS_ERR(regs))
		return PTR_ERR(regs);

	val = readl(regs);
	val |= FIELD_PREP(LS2K1000_SDIO_DMA_MASK, LS2K1000_DMA1_CONF);
	writel(val, regs);

	host->chan = dma_request_chan(&pdev->dev, "rx-tx");
	ret = PTR_ERR_OR_ZERO(host->chan);
	if (ret) {
		dev_err(&pdev->dev, "Cannot get DMA channel.\n");
		return ret;
	}

	return 0;
}

static struct loongson2_mmc_pdata ls2k1000_mmc_pdata = {
	.regmap_config		= &ls2k0500_mmc_regmap_config,
	.reorder_cmd_data	= ls2k0500_mmc_reorder_cmd_data,
	.setting_dma		= ls2k1000_mmc_set_external_dma,
	.prepare_dma		= loongson2_mmc_prepare_external_dma,
	.release_dma		= loongson2_mmc_release_external_dma,
};

static const struct regmap_config ls2k2000_mmc_regmap_config = {
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = LOONGSON2_MMC_REG_RDMA_HI,
};

static void ls2k2000_mmc_reorder_cmd_data(struct loongson2_mmc_host *host,
					  struct mmc_command *cmd)
{
	struct scatterlist *sg;
	u32 *data;
	int i, j;

	if (cmd->opcode != SD_SWITCH || mmc_cmd_type(cmd) != MMC_CMD_ADTC)
		return;

	for_each_sg(cmd->data->sg, sg, cmd->data->sg_len, i) {
		data = sg_virt(&sg[i]);
		for (j = 0; j < (sg_dma_len(&sg[i]) / 4); j++)
			data[j] = bitrev8x4(data[j]);
	}
}

/*
 * This is a controller hardware defect. Single/multiple block write commands
 * must be sent after the TX FULL flag is set, otherwise a data timeout interrupt
 * will occur.
 */
static void ls2k2000_mmc_fix_data_timeout(struct loongson2_mmc_host *host,
					  struct mmc_command *cmd)
{
	int val;

	if (cmd->opcode != MMC_WRITE_BLOCK && cmd->opcode != MMC_WRITE_MULTIPLE_BLOCK)
		return;

	regmap_read_poll_timeout(host->regmap, LOONGSON2_MMC_REG_FSTS, val,
				 (val & LOONGSON2_MMC_FSTS_TXFULL), 0,
				 LOONGSON2_MMC_TXFULL_TIMEOUT_US);
}

static int loongson2_mmc_prepare_internal_dma(struct loongson2_mmc_host *host,
					      struct mmc_data *data)
{
	struct loongson2_dma_desc *pdes = (struct loongson2_dma_desc *)host->sg_cpu;
	struct mmc_host *mmc = mmc_from_priv(host);
	dma_addr_t next_desc = host->sg_dma;
	struct scatterlist *sg;
	int reg_lo, reg_hi;
	u64 dma_order;
	int i, ret;

	ret = dma_map_sg(mmc_dev(mmc), data->sg, data->sg_len,
			 mmc_get_dma_dir(data));
	if (!ret)
		return -ENOMEM;

	for_each_sg(data->sg, sg, data->sg_len, i) {
		pdes[i].len = sg_dma_len(&sg[i]) / 4;
		pdes[i].step_len = 0;
		pdes[i].step_times = 1;
		pdes[i].mem_addr = lower_32_bits(sg_dma_address(&sg[i]));
		pdes[i].high_mem_addr = upper_32_bits(sg_dma_address(&sg[i]));
		pdes[i].apb_addr = host->res->start + LOONGSON2_MMC_REG_DATA;
		pdes[i].cmd = LOONGSON2_MMC_DMA_INT;

		if (data->flags & MMC_DATA_READ) {
			reg_lo = LOONGSON2_MMC_REG_RDMA_LO;
			reg_hi = LOONGSON2_MMC_REG_RDMA_HI;
		} else {
			pdes[i].cmd |= LOONGSON2_MMC_DMA_DATA_DIR;
			reg_lo = LOONGSON2_MMC_REG_WDMA_LO;
			reg_hi = LOONGSON2_MMC_REG_WDMA_HI;
		}

		next_desc += sizeof(struct loongson2_dma_desc);
		pdes[i].ndesc_addr = lower_32_bits(next_desc) |
				     LOONGSON2_MMC_DMA_DESC_EN;
		pdes[i].high_ndesc_addr = upper_32_bits(next_desc);
	}

	/* Setting the last descriptor enable bit */
	pdes[i - 1].ndesc_addr &= ~LOONGSON2_MMC_DMA_DESC_EN;

	dma_order = (host->sg_dma & ~LOONGSON2_MMC_DMA_CONFIG_MASK) |
		    LOONGSON2_MMC_DMA_64BIT_EN |
		    LOONGSON2_MMC_DMA_START;

	regmap_write(host->regmap, reg_hi, upper_32_bits(dma_order));
	regmap_write(host->regmap, reg_lo, lower_32_bits(dma_order));

	return 0;
}

static int ls2k2000_mmc_set_internal_dma(struct loongson2_mmc_host *host,
					 struct platform_device *pdev)
{
	host->sg_cpu = dma_alloc_coherent(&pdev->dev, PAGE_SIZE,
					  &host->sg_dma, GFP_KERNEL);
	if (!host->sg_cpu)
		return -ENOMEM;

	memset(host->sg_cpu, 0, PAGE_SIZE);
	return 0;
}

static void loongson2_mmc_release_internal_dma(struct loongson2_mmc_host *host,
					       struct device *dev)
{
	dma_free_coherent(dev, PAGE_SIZE, host->sg_cpu, host->sg_dma);
}

static struct loongson2_mmc_pdata ls2k2000_mmc_pdata = {
	.regmap_config		= &ls2k2000_mmc_regmap_config,
	.reorder_cmd_data	= ls2k2000_mmc_reorder_cmd_data,
	.fix_data_timeout	= ls2k2000_mmc_fix_data_timeout,
	.setting_dma		= ls2k2000_mmc_set_internal_dma,
	.prepare_dma		= loongson2_mmc_prepare_internal_dma,
	.release_dma		= loongson2_mmc_release_internal_dma,
};

static int loongson2_mmc_resource_request(struct platform_device *pdev,
					  struct loongson2_mmc_host *host)
{
	struct device *dev = &pdev->dev;
	void __iomem *base;
	int ret, irq;

	base = devm_platform_get_and_ioremap_resource(pdev, 0, &host->res);
	if (IS_ERR(base))
		return PTR_ERR(base);

	host->regmap = devm_regmap_init_mmio(dev, base, host->pdata->regmap_config);
	if (IS_ERR(host->regmap))
		return PTR_ERR(host->regmap);

	host->clk = devm_clk_get_optional_enabled(dev, NULL);
	if (IS_ERR(host->clk))
		return PTR_ERR(host->clk);

	if (host->clk) {
		ret = devm_clk_rate_exclusive_get(dev, host->clk);
		if (ret)
			return ret;

		host->current_clk = clk_get_rate(host->clk);
	} else {
		/* For ACPI, the clock is accessed via the clock-frequency attribute. */
		device_property_read_u32(dev, "clock-frequency", &host->current_clk);
	}

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	ret = devm_request_threaded_irq(dev, irq, loongson2_mmc_irq,
					loongson2_mmc_irq_worker,
					IRQF_ONESHOT, "loongson2-mmc", host);
	if (ret)
		return ret;

	ret = dma_set_mask_and_coherent(dev, DMA_BIT_MASK(64));
	if (ret)
		return ret;

	return host->pdata->setting_dma(host, pdev);
}

static int loongson2_mmc_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct loongson2_mmc_host *host;
	struct mmc_host	*mmc;
	int ret;

	mmc = devm_mmc_alloc_host(dev, sizeof(*host));
	if (!mmc)
		return -ENOMEM;

	platform_set_drvdata(pdev, mmc);

	host = mmc_priv(mmc);
	host->state = STATE_NONE;
	spin_lock_init(&host->lock);

	host->pdata = device_get_match_data(dev);
	if (!host->pdata)
		return dev_err_probe(dev, -EINVAL, "Failed to get match data\n");

	ret = loongson2_mmc_resource_request(pdev, host);
	if (ret)
		return dev_err_probe(dev, ret, "Failed to request resource\n");

	mmc->ops = &loongson2_mmc_ops;
	mmc->f_min = DIV_ROUND_UP(host->current_clk, 256);
	mmc->f_max = host->current_clk;
	mmc->max_blk_count = 4095;
	mmc->max_blk_size = 4095;
	mmc->max_req_size = mmc->max_blk_count * mmc->max_blk_size;
	mmc->max_segs = 1;
	mmc->max_seg_size = mmc->max_req_size;

	/* Process SDIO IRQs through the sdio_irq_work. */
	if (mmc->caps & MMC_CAP_SDIO_IRQ)
		mmc->caps2 |= MMC_CAP2_SDIO_IRQ_NOTHREAD;

	ret = mmc_regulator_get_supply(mmc);
	if (ret || mmc->ocr_avail == 0) {
		dev_warn(dev, "Can't get voltage, defaulting to 3.3V\n");
		mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	}

	ret = mmc_of_parse(mmc);
	if (ret) {
		dev_err(dev, "Failed to parse device node\n");
		goto free_dma;
	}

	ret = mmc_add_host(mmc);
	if (ret) {
		dev_err(dev, "Failed to add mmc host\n");
		goto free_dma;
	}

	return 0;

free_dma:
	host->pdata->release_dma(host, dev);
	return ret;
}

static void loongson2_mmc_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc  = platform_get_drvdata(pdev);
	struct loongson2_mmc_host *host = mmc_priv(mmc);

	mmc_remove_host(mmc);
	host->pdata->release_dma(host, &pdev->dev);
}

static const struct of_device_id loongson2_mmc_of_ids[] = {
	{ .compatible = "loongson,ls2k0500-mmc", .data = &ls2k0500_mmc_pdata },
	{ .compatible = "loongson,ls2k1000-mmc", .data = &ls2k1000_mmc_pdata },
	{ .compatible = "loongson,ls2k2000-mmc", .data = &ls2k2000_mmc_pdata },
	{ },
};
MODULE_DEVICE_TABLE(of, loongson2_mmc_of_ids);

static int loongson2_mmc_suspend(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct loongson2_mmc_host *host = mmc_priv(mmc);

	clk_disable_unprepare(host->clk);

	return 0;
}

static int loongson2_mmc_resume(struct device *dev)
{
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct loongson2_mmc_host *host = mmc_priv(mmc);

	return clk_prepare_enable(host->clk);
}

static DEFINE_SIMPLE_DEV_PM_OPS(loongson2_mmc_pm_ops, loongson2_mmc_suspend, loongson2_mmc_resume);

static struct platform_driver loongson2_mmc_driver = {
	.driver	= {
		.name = "loongson2-mmc",
		.of_match_table = loongson2_mmc_of_ids,
		.pm = pm_ptr(&loongson2_mmc_pm_ops),
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
	},
	.probe = loongson2_mmc_probe,
	.remove = loongson2_mmc_remove,
};

module_platform_driver(loongson2_mmc_driver);

MODULE_DESCRIPTION("Loongson-2K SD/SDIO/eMMC Interface driver");
MODULE_AUTHOR("Loongson Technology Corporation Limited");
MODULE_LICENSE("GPL");
