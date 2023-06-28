// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for sunxi SD/MMC host controllers
 * (C) Copyright 2007-2011 Reuuimlla Technology Co., Ltd.
 * (C) Copyright 2007-2011 Aaron Maoye <leafy.myeh@reuuimllatech.com>
 * (C) Copyright 2013-2014 O2S GmbH <www.o2s.ch>
 * (C) Copyright 2013-2014 David Lanzendörfer <david.lanzendoerfer@o2s.ch>
 * (C) Copyright 2013-2014 Hans de Goede <hdegoede@redhat.com>
 * (C) Copyright 2017 Sootech SA
 */

#include <linux/clk.h>
#include <linux/clk/sunxi-ng.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/kernel.h>
#include <linux/mmc/card.h>
#include <linux/mmc/core.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/of_address.h>
#include <linux/of_platform.h>
#include <linux/platform_device.h>
#include <linux/pm_runtime.h>
#include <linux/regulator/consumer.h>
#include <linux/reset.h>
#include <linux/scatterlist.h>
#include <linux/slab.h>
#include <linux/spinlock.h>

/* register offset definitions */
#define SDXC_REG_GCTRL	(0x00) /* SMC Global Control Register */
#define SDXC_REG_CLKCR	(0x04) /* SMC Clock Control Register */
#define SDXC_REG_TMOUT	(0x08) /* SMC Time Out Register */
#define SDXC_REG_WIDTH	(0x0C) /* SMC Bus Width Register */
#define SDXC_REG_BLKSZ	(0x10) /* SMC Block Size Register */
#define SDXC_REG_BCNTR	(0x14) /* SMC Byte Count Register */
#define SDXC_REG_CMDR	(0x18) /* SMC Command Register */
#define SDXC_REG_CARG	(0x1C) /* SMC Argument Register */
#define SDXC_REG_RESP0	(0x20) /* SMC Response Register 0 */
#define SDXC_REG_RESP1	(0x24) /* SMC Response Register 1 */
#define SDXC_REG_RESP2	(0x28) /* SMC Response Register 2 */
#define SDXC_REG_RESP3	(0x2C) /* SMC Response Register 3 */
#define SDXC_REG_IMASK	(0x30) /* SMC Interrupt Mask Register */
#define SDXC_REG_MISTA	(0x34) /* SMC Masked Interrupt Status Register */
#define SDXC_REG_RINTR	(0x38) /* SMC Raw Interrupt Status Register */
#define SDXC_REG_STAS	(0x3C) /* SMC Status Register */
#define SDXC_REG_FTRGL	(0x40) /* SMC FIFO Threshold Watermark Registe */
#define SDXC_REG_FUNS	(0x44) /* SMC Function Select Register */
#define SDXC_REG_CBCR	(0x48) /* SMC CIU Byte Count Register */
#define SDXC_REG_BBCR	(0x4C) /* SMC BIU Byte Count Register */
#define SDXC_REG_DBGC	(0x50) /* SMC Debug Enable Register */
#define SDXC_REG_HWRST	(0x78) /* SMC Card Hardware Reset for Register */
#define SDXC_REG_DMAC	(0x80) /* SMC IDMAC Control Register */
#define SDXC_REG_DLBA	(0x84) /* SMC IDMAC Descriptor List Base Addre */
#define SDXC_REG_IDST	(0x88) /* SMC IDMAC Status Register */
#define SDXC_REG_IDIE	(0x8C) /* SMC IDMAC Interrupt Enable Register */
#define SDXC_REG_CHDA	(0x90)
#define SDXC_REG_CBDA	(0x94)

/* New registers introduced in A64 */
#define SDXC_REG_A12A		0x058 /* SMC Auto Command 12 Register */
#define SDXC_REG_SD_NTSR	0x05C /* SMC New Timing Set Register */
#define SDXC_REG_DRV_DL		0x140 /* Drive Delay Control Register */
#define SDXC_REG_SAMP_DL_REG	0x144 /* SMC sample delay control */
#define SDXC_REG_DS_DL_REG	0x148 /* SMC data strobe delay control */

#define mmc_readl(host, reg) \
	readl((host)->reg_base + SDXC_##reg)
#define mmc_writel(host, reg, value) \
	writel((value), (host)->reg_base + SDXC_##reg)

/* global control register bits */
#define SDXC_SOFT_RESET			BIT(0)
#define SDXC_FIFO_RESET			BIT(1)
#define SDXC_DMA_RESET			BIT(2)
#define SDXC_INTERRUPT_ENABLE_BIT	BIT(4)
#define SDXC_DMA_ENABLE_BIT		BIT(5)
#define SDXC_DEBOUNCE_ENABLE_BIT	BIT(8)
#define SDXC_POSEDGE_LATCH_DATA		BIT(9)
#define SDXC_DDR_MODE			BIT(10)
#define SDXC_MEMORY_ACCESS_DONE		BIT(29)
#define SDXC_ACCESS_DONE_DIRECT		BIT(30)
#define SDXC_ACCESS_BY_AHB		BIT(31)
#define SDXC_ACCESS_BY_DMA		(0 << 31)
#define SDXC_HARDWARE_RESET \
	(SDXC_SOFT_RESET | SDXC_FIFO_RESET | SDXC_DMA_RESET)

/* clock control bits */
#define SDXC_MASK_DATA0			BIT(31)
#define SDXC_CARD_CLOCK_ON		BIT(16)
#define SDXC_LOW_POWER_ON		BIT(17)

/* bus width */
#define SDXC_WIDTH1			0
#define SDXC_WIDTH4			1
#define SDXC_WIDTH8			2

/* smc command bits */
#define SDXC_RESP_EXPIRE		BIT(6)
#define SDXC_LONG_RESPONSE		BIT(7)
#define SDXC_CHECK_RESPONSE_CRC		BIT(8)
#define SDXC_DATA_EXPIRE		BIT(9)
#define SDXC_WRITE			BIT(10)
#define SDXC_SEQUENCE_MODE		BIT(11)
#define SDXC_SEND_AUTO_STOP		BIT(12)
#define SDXC_WAIT_PRE_OVER		BIT(13)
#define SDXC_STOP_ABORT_CMD		BIT(14)
#define SDXC_SEND_INIT_SEQUENCE		BIT(15)
#define SDXC_UPCLK_ONLY			BIT(21)
#define SDXC_READ_CEATA_DEV		BIT(22)
#define SDXC_CCS_EXPIRE			BIT(23)
#define SDXC_ENABLE_BIT_BOOT		BIT(24)
#define SDXC_ALT_BOOT_OPTIONS		BIT(25)
#define SDXC_BOOT_ACK_EXPIRE		BIT(26)
#define SDXC_BOOT_ABORT			BIT(27)
#define SDXC_VOLTAGE_SWITCH	        BIT(28)
#define SDXC_USE_HOLD_REGISTER	        BIT(29)
#define SDXC_START			BIT(31)

/* interrupt bits */
#define SDXC_RESP_ERROR			BIT(1)
#define SDXC_COMMAND_DONE		BIT(2)
#define SDXC_DATA_OVER			BIT(3)
#define SDXC_TX_DATA_REQUEST		BIT(4)
#define SDXC_RX_DATA_REQUEST		BIT(5)
#define SDXC_RESP_CRC_ERROR		BIT(6)
#define SDXC_DATA_CRC_ERROR		BIT(7)
#define SDXC_RESP_TIMEOUT		BIT(8)
#define SDXC_DATA_TIMEOUT		BIT(9)
#define SDXC_VOLTAGE_CHANGE_DONE	BIT(10)
#define SDXC_FIFO_RUN_ERROR		BIT(11)
#define SDXC_HARD_WARE_LOCKED		BIT(12)
#define SDXC_START_BIT_ERROR		BIT(13)
#define SDXC_AUTO_COMMAND_DONE		BIT(14)
#define SDXC_END_BIT_ERROR		BIT(15)
#define SDXC_SDIO_INTERRUPT		BIT(16)
#define SDXC_CARD_INSERT		BIT(30)
#define SDXC_CARD_REMOVE		BIT(31)
#define SDXC_INTERRUPT_ERROR_BIT \
	(SDXC_RESP_ERROR | SDXC_RESP_CRC_ERROR | SDXC_DATA_CRC_ERROR | \
	 SDXC_RESP_TIMEOUT | SDXC_DATA_TIMEOUT | SDXC_FIFO_RUN_ERROR | \
	 SDXC_HARD_WARE_LOCKED | SDXC_START_BIT_ERROR | SDXC_END_BIT_ERROR)
#define SDXC_INTERRUPT_DONE_BIT \
	(SDXC_AUTO_COMMAND_DONE | SDXC_DATA_OVER | \
	 SDXC_COMMAND_DONE | SDXC_VOLTAGE_CHANGE_DONE)

/* status */
#define SDXC_RXWL_FLAG			BIT(0)
#define SDXC_TXWL_FLAG			BIT(1)
#define SDXC_FIFO_EMPTY			BIT(2)
#define SDXC_FIFO_FULL			BIT(3)
#define SDXC_CARD_PRESENT		BIT(8)
#define SDXC_CARD_DATA_BUSY		BIT(9)
#define SDXC_DATA_FSM_BUSY		BIT(10)
#define SDXC_DMA_REQUEST		BIT(31)
#define SDXC_FIFO_SIZE			16

/* Function select */
#define SDXC_CEATA_ON			(0xceaa << 16)
#define SDXC_SEND_IRQ_RESPONSE		BIT(0)
#define SDXC_SDIO_READ_WAIT		BIT(1)
#define SDXC_ABORT_READ_DATA		BIT(2)
#define SDXC_SEND_CCSD			BIT(8)
#define SDXC_SEND_AUTO_STOPCCSD		BIT(9)
#define SDXC_CEATA_DEV_IRQ_ENABLE	BIT(10)

/* IDMA controller bus mod bit field */
#define SDXC_IDMAC_SOFT_RESET		BIT(0)
#define SDXC_IDMAC_FIX_BURST		BIT(1)
#define SDXC_IDMAC_IDMA_ON		BIT(7)
#define SDXC_IDMAC_REFETCH_DES		BIT(31)

/* IDMA status bit field */
#define SDXC_IDMAC_TRANSMIT_INTERRUPT		BIT(0)
#define SDXC_IDMAC_RECEIVE_INTERRUPT		BIT(1)
#define SDXC_IDMAC_FATAL_BUS_ERROR		BIT(2)
#define SDXC_IDMAC_DESTINATION_INVALID		BIT(4)
#define SDXC_IDMAC_CARD_ERROR_SUM		BIT(5)
#define SDXC_IDMAC_NORMAL_INTERRUPT_SUM		BIT(8)
#define SDXC_IDMAC_ABNORMAL_INTERRUPT_SUM	BIT(9)
#define SDXC_IDMAC_HOST_ABORT_INTERRUPT		BIT(10)
#define SDXC_IDMAC_IDLE				(0 << 13)
#define SDXC_IDMAC_SUSPEND			(1 << 13)
#define SDXC_IDMAC_DESC_READ			(2 << 13)
#define SDXC_IDMAC_DESC_CHECK			(3 << 13)
#define SDXC_IDMAC_READ_REQUEST_WAIT		(4 << 13)
#define SDXC_IDMAC_WRITE_REQUEST_WAIT		(5 << 13)
#define SDXC_IDMAC_READ				(6 << 13)
#define SDXC_IDMAC_WRITE			(7 << 13)
#define SDXC_IDMAC_DESC_CLOSE			(8 << 13)

/*
* If the idma-des-size-bits of property is ie 13, bufsize bits are:
*  Bits  0-12: buf1 size
*  Bits 13-25: buf2 size
*  Bits 26-31: not used
* Since we only ever set buf1 size, we can simply store it directly.
*/
#define SDXC_IDMAC_DES0_DIC	BIT(1)  /* disable interrupt on completion */
#define SDXC_IDMAC_DES0_LD	BIT(2)  /* last descriptor */
#define SDXC_IDMAC_DES0_FD	BIT(3)  /* first descriptor */
#define SDXC_IDMAC_DES0_CH	BIT(4)  /* chain mode */
#define SDXC_IDMAC_DES0_ER	BIT(5)  /* end of ring */
#define SDXC_IDMAC_DES0_CES	BIT(30) /* card error summary */
#define SDXC_IDMAC_DES0_OWN	BIT(31) /* 1-idma owns it, 0-host owns it */

#define SDXC_CLK_400K		0
#define SDXC_CLK_25M		1
#define SDXC_CLK_50M		2
#define SDXC_CLK_50M_DDR	3
#define SDXC_CLK_50M_DDR_8BIT	4

#define SDXC_2X_TIMING_MODE	BIT(31)

#define SDXC_CAL_START		BIT(15)
#define SDXC_CAL_DONE		BIT(14)
#define SDXC_CAL_DL_SHIFT	8
#define SDXC_CAL_DL_SW_EN	BIT(7)
#define SDXC_CAL_DL_SW_SHIFT	0
#define SDXC_CAL_DL_MASK	0x3f

#define SDXC_CAL_TIMEOUT	3	/* in seconds, 3s is enough*/

struct sunxi_mmc_clk_delay {
	u32 output;
	u32 sample;
};

struct sunxi_idma_des {
	__le32 config;
	__le32 buf_size;
	__le32 buf_addr_ptr1;
	__le32 buf_addr_ptr2;
};

struct sunxi_mmc_cfg {
	u32 idma_des_size_bits;
	u32 idma_des_shift;
	const struct sunxi_mmc_clk_delay *clk_delays;

	/* does the IP block support autocalibration? */
	bool can_calibrate;

	/* Does DATA0 needs to be masked while the clock is updated */
	bool mask_data0;

	/*
	 * hardware only supports new timing mode, either due to lack of
	 * a mode switch in the clock controller, or the mmc controller
	 * is permanently configured in the new timing mode, without the
	 * NTSR mode switch.
	 */
	bool needs_new_timings;

	/* clock hardware can switch between old and new timing modes */
	bool ccu_has_timings_switch;
};

struct sunxi_mmc_host {
	struct device *dev;
	struct mmc_host	*mmc;
	struct reset_control *reset;
	const struct sunxi_mmc_cfg *cfg;

	/* IO mapping base */
	void __iomem	*reg_base;

	/* clock management */
	struct clk	*clk_ahb;
	struct clk	*clk_mmc;
	struct clk	*clk_sample;
	struct clk	*clk_output;

	/* irq */
	spinlock_t	lock;
	int		irq;
	u32		int_sum;
	u32		sdio_imask;

	/* dma */
	dma_addr_t	sg_dma;
	void		*sg_cpu;
	bool		wait_dma;

	struct mmc_request *mrq;
	struct mmc_request *manual_stop_mrq;
	int		ferror;

	/* vqmmc */
	bool		vqmmc_enabled;

	/* timings */
	bool		use_new_timings;
};

static int sunxi_mmc_reset_host(struct sunxi_mmc_host *host)
{
	unsigned long expire = jiffies + msecs_to_jiffies(250);
	u32 rval;

	mmc_writel(host, REG_GCTRL, SDXC_HARDWARE_RESET);
	do {
		rval = mmc_readl(host, REG_GCTRL);
	} while (time_before(jiffies, expire) && (rval & SDXC_HARDWARE_RESET));

	if (rval & SDXC_HARDWARE_RESET) {
		dev_err(mmc_dev(host->mmc), "fatal err reset timeout\n");
		return -EIO;
	}

	return 0;
}

static int sunxi_mmc_init_host(struct sunxi_mmc_host *host)
{
	u32 rval;

	if (sunxi_mmc_reset_host(host))
		return -EIO;

	/*
	 * Burst 8 transfers, RX trigger level: 7, TX trigger level: 8
	 *
	 * TODO: sun9i has a larger FIFO and supports higher trigger values
	 */
	mmc_writel(host, REG_FTRGL, 0x20070008);
	/* Maximum timeout value */
	mmc_writel(host, REG_TMOUT, 0xffffffff);
	/* Unmask SDIO interrupt if needed */
	mmc_writel(host, REG_IMASK, host->sdio_imask);
	/* Clear all pending interrupts */
	mmc_writel(host, REG_RINTR, 0xffffffff);
	/* Debug register? undocumented */
	mmc_writel(host, REG_DBGC, 0xdeb);
	/* Enable CEATA support */
	mmc_writel(host, REG_FUNS, SDXC_CEATA_ON);
	/* Set DMA descriptor list base address */
	mmc_writel(host, REG_DLBA, host->sg_dma >> host->cfg->idma_des_shift);

	rval = mmc_readl(host, REG_GCTRL);
	rval |= SDXC_INTERRUPT_ENABLE_BIT;
	/* Undocumented, but found in Allwinner code */
	rval &= ~SDXC_ACCESS_DONE_DIRECT;
	mmc_writel(host, REG_GCTRL, rval);

	return 0;
}

static void sunxi_mmc_init_idma_des(struct sunxi_mmc_host *host,
				    struct mmc_data *data)
{
	struct sunxi_idma_des *pdes = (struct sunxi_idma_des *)host->sg_cpu;
	dma_addr_t next_desc = host->sg_dma;
	int i, max_len = (1 << host->cfg->idma_des_size_bits);

	for (i = 0; i < data->sg_len; i++) {
		pdes[i].config = cpu_to_le32(SDXC_IDMAC_DES0_CH |
					     SDXC_IDMAC_DES0_OWN |
					     SDXC_IDMAC_DES0_DIC);

		if (data->sg[i].length == max_len)
			pdes[i].buf_size = 0; /* 0 == max_len */
		else
			pdes[i].buf_size = cpu_to_le32(data->sg[i].length);

		next_desc += sizeof(struct sunxi_idma_des);
		pdes[i].buf_addr_ptr1 =
			cpu_to_le32(sg_dma_address(&data->sg[i]) >>
				    host->cfg->idma_des_shift);
		pdes[i].buf_addr_ptr2 =
			cpu_to_le32(next_desc >>
				    host->cfg->idma_des_shift);
	}

	pdes[0].config |= cpu_to_le32(SDXC_IDMAC_DES0_FD);
	pdes[i - 1].config |= cpu_to_le32(SDXC_IDMAC_DES0_LD |
					  SDXC_IDMAC_DES0_ER);
	pdes[i - 1].config &= cpu_to_le32(~SDXC_IDMAC_DES0_DIC);
	pdes[i - 1].buf_addr_ptr2 = 0;

	/*
	 * Avoid the io-store starting the idmac hitting io-mem before the
	 * descriptors hit the main-mem.
	 */
	wmb();
}

static int sunxi_mmc_map_dma(struct sunxi_mmc_host *host,
			     struct mmc_data *data)
{
	u32 i, dma_len;
	struct scatterlist *sg;

	dma_len = dma_map_sg(mmc_dev(host->mmc), data->sg, data->sg_len,
			     mmc_get_dma_dir(data));
	if (dma_len == 0) {
		dev_err(mmc_dev(host->mmc), "dma_map_sg failed\n");
		return -ENOMEM;
	}

	for_each_sg(data->sg, sg, data->sg_len, i) {
		if (sg->offset & 3 || sg->length & 3) {
			dev_err(mmc_dev(host->mmc),
				"unaligned scatterlist: os %x length %d\n",
				sg->offset, sg->length);
			return -EINVAL;
		}
	}

	return 0;
}

static void sunxi_mmc_start_dma(struct sunxi_mmc_host *host,
				struct mmc_data *data)
{
	u32 rval;

	sunxi_mmc_init_idma_des(host, data);

	rval = mmc_readl(host, REG_GCTRL);
	rval |= SDXC_DMA_ENABLE_BIT;
	mmc_writel(host, REG_GCTRL, rval);
	rval |= SDXC_DMA_RESET;
	mmc_writel(host, REG_GCTRL, rval);

	mmc_writel(host, REG_DMAC, SDXC_IDMAC_SOFT_RESET);

	if (!(data->flags & MMC_DATA_WRITE))
		mmc_writel(host, REG_IDIE, SDXC_IDMAC_RECEIVE_INTERRUPT);

	mmc_writel(host, REG_DMAC,
		   SDXC_IDMAC_FIX_BURST | SDXC_IDMAC_IDMA_ON);
}

static void sunxi_mmc_send_manual_stop(struct sunxi_mmc_host *host,
				       struct mmc_request *req)
{
	u32 arg, cmd_val, ri;
	unsigned long expire = jiffies + msecs_to_jiffies(1000);

	cmd_val = SDXC_START | SDXC_RESP_EXPIRE |
		  SDXC_STOP_ABORT_CMD | SDXC_CHECK_RESPONSE_CRC;

	if (req->cmd->opcode == SD_IO_RW_EXTENDED) {
		cmd_val |= SD_IO_RW_DIRECT;
		arg = (1 << 31) | (0 << 28) | (SDIO_CCCR_ABORT << 9) |
		      ((req->cmd->arg >> 28) & 0x7);
	} else {
		cmd_val |= MMC_STOP_TRANSMISSION;
		arg = 0;
	}

	mmc_writel(host, REG_CARG, arg);
	mmc_writel(host, REG_CMDR, cmd_val);

	do {
		ri = mmc_readl(host, REG_RINTR);
	} while (!(ri & (SDXC_COMMAND_DONE | SDXC_INTERRUPT_ERROR_BIT)) &&
		 time_before(jiffies, expire));

	if (!(ri & SDXC_COMMAND_DONE) || (ri & SDXC_INTERRUPT_ERROR_BIT)) {
		dev_err(mmc_dev(host->mmc), "send stop command failed\n");
		if (req->stop)
			req->stop->resp[0] = -ETIMEDOUT;
	} else {
		if (req->stop)
			req->stop->resp[0] = mmc_readl(host, REG_RESP0);
	}

	mmc_writel(host, REG_RINTR, 0xffff);
}

static void sunxi_mmc_dump_errinfo(struct sunxi_mmc_host *host)
{
	struct mmc_command *cmd = host->mrq->cmd;
	struct mmc_data *data = host->mrq->data;

	/* For some cmds timeout is normal with sd/mmc cards */
	if ((host->int_sum & SDXC_INTERRUPT_ERROR_BIT) ==
		SDXC_RESP_TIMEOUT && (cmd->opcode == SD_IO_SEND_OP_COND ||
				      cmd->opcode == SD_IO_RW_DIRECT))
		return;

	dev_dbg(mmc_dev(host->mmc),
		"smc %d err, cmd %d,%s%s%s%s%s%s%s%s%s%s !!\n",
		host->mmc->index, cmd->opcode,
		data ? (data->flags & MMC_DATA_WRITE ? " WR" : " RD") : "",
		host->int_sum & SDXC_RESP_ERROR     ? " RE"     : "",
		host->int_sum & SDXC_RESP_CRC_ERROR  ? " RCE"    : "",
		host->int_sum & SDXC_DATA_CRC_ERROR  ? " DCE"    : "",
		host->int_sum & SDXC_RESP_TIMEOUT ? " RTO"    : "",
		host->int_sum & SDXC_DATA_TIMEOUT ? " DTO"    : "",
		host->int_sum & SDXC_FIFO_RUN_ERROR  ? " FE"     : "",
		host->int_sum & SDXC_HARD_WARE_LOCKED ? " HL"     : "",
		host->int_sum & SDXC_START_BIT_ERROR ? " SBE"    : "",
		host->int_sum & SDXC_END_BIT_ERROR   ? " EBE"    : ""
		);
}

/* Called in interrupt context! */
static irqreturn_t sunxi_mmc_finalize_request(struct sunxi_mmc_host *host)
{
	struct mmc_request *mrq = host->mrq;
	struct mmc_data *data = mrq->data;
	u32 rval;

	mmc_writel(host, REG_IMASK, host->sdio_imask);
	mmc_writel(host, REG_IDIE, 0);

	if (host->int_sum & SDXC_INTERRUPT_ERROR_BIT) {
		sunxi_mmc_dump_errinfo(host);
		mrq->cmd->error = -ETIMEDOUT;

		if (data) {
			data->error = -ETIMEDOUT;
			host->manual_stop_mrq = mrq;
		}

		if (mrq->stop)
			mrq->stop->error = -ETIMEDOUT;
	} else {
		if (mrq->cmd->flags & MMC_RSP_136) {
			mrq->cmd->resp[0] = mmc_readl(host, REG_RESP3);
			mrq->cmd->resp[1] = mmc_readl(host, REG_RESP2);
			mrq->cmd->resp[2] = mmc_readl(host, REG_RESP1);
			mrq->cmd->resp[3] = mmc_readl(host, REG_RESP0);
		} else {
			mrq->cmd->resp[0] = mmc_readl(host, REG_RESP0);
		}

		if (data)
			data->bytes_xfered = data->blocks * data->blksz;
	}

	if (data) {
		mmc_writel(host, REG_IDST, 0x337);
		mmc_writel(host, REG_DMAC, 0);
		rval = mmc_readl(host, REG_GCTRL);
		rval |= SDXC_DMA_RESET;
		mmc_writel(host, REG_GCTRL, rval);
		rval &= ~SDXC_DMA_ENABLE_BIT;
		mmc_writel(host, REG_GCTRL, rval);
		rval |= SDXC_FIFO_RESET;
		mmc_writel(host, REG_GCTRL, rval);
		dma_unmap_sg(mmc_dev(host->mmc), data->sg, data->sg_len,
			     mmc_get_dma_dir(data));
	}

	mmc_writel(host, REG_RINTR, 0xffff);

	host->mrq = NULL;
	host->int_sum = 0;
	host->wait_dma = false;

	return host->manual_stop_mrq ? IRQ_WAKE_THREAD : IRQ_HANDLED;
}

static irqreturn_t sunxi_mmc_irq(int irq, void *dev_id)
{
	struct sunxi_mmc_host *host = dev_id;
	struct mmc_request *mrq;
	u32 msk_int, idma_int;
	bool finalize = false;
	bool sdio_int = false;
	irqreturn_t ret = IRQ_HANDLED;

	spin_lock(&host->lock);

	idma_int  = mmc_readl(host, REG_IDST);
	msk_int   = mmc_readl(host, REG_MISTA);

	dev_dbg(mmc_dev(host->mmc), "irq: rq %p mi %08x idi %08x\n",
		host->mrq, msk_int, idma_int);

	mrq = host->mrq;
	if (mrq) {
		if (idma_int & SDXC_IDMAC_RECEIVE_INTERRUPT)
			host->wait_dma = false;

		host->int_sum |= msk_int;

		/* Wait for COMMAND_DONE on RESPONSE_TIMEOUT before finalize */
		if ((host->int_sum & SDXC_RESP_TIMEOUT) &&
				!(host->int_sum & SDXC_COMMAND_DONE))
			mmc_writel(host, REG_IMASK,
				   host->sdio_imask | SDXC_COMMAND_DONE);
		/* Don't wait for dma on error */
		else if (host->int_sum & SDXC_INTERRUPT_ERROR_BIT)
			finalize = true;
		else if ((host->int_sum & SDXC_INTERRUPT_DONE_BIT) &&
				!host->wait_dma)
			finalize = true;
	}

	if (msk_int & SDXC_SDIO_INTERRUPT)
		sdio_int = true;

	mmc_writel(host, REG_RINTR, msk_int);
	mmc_writel(host, REG_IDST, idma_int);

	if (finalize)
		ret = sunxi_mmc_finalize_request(host);

	spin_unlock(&host->lock);

	if (finalize && ret == IRQ_HANDLED)
		mmc_request_done(host->mmc, mrq);

	if (sdio_int)
		mmc_signal_sdio_irq(host->mmc);

	return ret;
}

static irqreturn_t sunxi_mmc_handle_manual_stop(int irq, void *dev_id)
{
	struct sunxi_mmc_host *host = dev_id;
	struct mmc_request *mrq;
	unsigned long iflags;

	spin_lock_irqsave(&host->lock, iflags);
	mrq = host->manual_stop_mrq;
	spin_unlock_irqrestore(&host->lock, iflags);

	if (!mrq) {
		dev_err(mmc_dev(host->mmc), "no request for manual stop\n");
		return IRQ_HANDLED;
	}

	dev_err(mmc_dev(host->mmc), "data error, sending stop command\n");

	/*
	 * We will never have more than one outstanding request,
	 * and we do not complete the request until after
	 * we've cleared host->manual_stop_mrq so we do not need to
	 * spin lock this function.
	 * Additionally we have wait states within this function
	 * so having it in a lock is a very bad idea.
	 */
	sunxi_mmc_send_manual_stop(host, mrq);

	spin_lock_irqsave(&host->lock, iflags);
	host->manual_stop_mrq = NULL;
	spin_unlock_irqrestore(&host->lock, iflags);

	mmc_request_done(host->mmc, mrq);

	return IRQ_HANDLED;
}

static int sunxi_mmc_oclk_onoff(struct sunxi_mmc_host *host, u32 oclk_en)
{
	unsigned long expire = jiffies + msecs_to_jiffies(750);
	u32 rval;

	dev_dbg(mmc_dev(host->mmc), "%sabling the clock\n",
		oclk_en ? "en" : "dis");

	rval = mmc_readl(host, REG_CLKCR);
	rval &= ~(SDXC_CARD_CLOCK_ON | SDXC_LOW_POWER_ON | SDXC_MASK_DATA0);

	if (oclk_en)
		rval |= SDXC_CARD_CLOCK_ON;
	if (host->cfg->mask_data0)
		rval |= SDXC_MASK_DATA0;

	mmc_writel(host, REG_CLKCR, rval);

	rval = SDXC_START | SDXC_UPCLK_ONLY | SDXC_WAIT_PRE_OVER;
	mmc_writel(host, REG_CMDR, rval);

	do {
		rval = mmc_readl(host, REG_CMDR);
	} while (time_before(jiffies, expire) && (rval & SDXC_START));

	/* clear irq status bits set by the command */
	mmc_writel(host, REG_RINTR,
		   mmc_readl(host, REG_RINTR) & ~SDXC_SDIO_INTERRUPT);

	if (rval & SDXC_START) {
		dev_err(mmc_dev(host->mmc), "fatal err update clk timeout\n");
		return -EIO;
	}

	if (host->cfg->mask_data0) {
		rval = mmc_readl(host, REG_CLKCR);
		mmc_writel(host, REG_CLKCR, rval & ~SDXC_MASK_DATA0);
	}

	return 0;
}

static int sunxi_mmc_calibrate(struct sunxi_mmc_host *host, int reg_off)
{
	if (!host->cfg->can_calibrate)
		return 0;

	/*
	 * FIXME:
	 * This is not clear how the calibration is supposed to work
	 * yet. The best rate have been obtained by simply setting the
	 * delay to 0, as Allwinner does in its BSP.
	 *
	 * The only mode that doesn't have such a delay is HS400, that
	 * is in itself a TODO.
	 */
	writel(SDXC_CAL_DL_SW_EN, host->reg_base + reg_off);

	return 0;
}

static int sunxi_mmc_clk_set_phase(struct sunxi_mmc_host *host,
				   struct mmc_ios *ios, u32 rate)
{
	int index;

	/* clk controller delays not used under new timings mode */
	if (host->use_new_timings)
		return 0;

	/* some old controllers don't support delays */
	if (!host->cfg->clk_delays)
		return 0;

	/* determine delays */
	if (rate <= 400000) {
		index = SDXC_CLK_400K;
	} else if (rate <= 25000000) {
		index = SDXC_CLK_25M;
	} else if (rate <= 52000000) {
		if (ios->timing != MMC_TIMING_UHS_DDR50 &&
		    ios->timing != MMC_TIMING_MMC_DDR52) {
			index = SDXC_CLK_50M;
		} else if (ios->bus_width == MMC_BUS_WIDTH_8) {
			index = SDXC_CLK_50M_DDR_8BIT;
		} else {
			index = SDXC_CLK_50M_DDR;
		}
	} else {
		dev_dbg(mmc_dev(host->mmc), "Invalid clock... returning\n");
		return -EINVAL;
	}

	clk_set_phase(host->clk_sample, host->cfg->clk_delays[index].sample);
	clk_set_phase(host->clk_output, host->cfg->clk_delays[index].output);

	return 0;
}

static int sunxi_mmc_clk_set_rate(struct sunxi_mmc_host *host,
				  struct mmc_ios *ios)
{
	struct mmc_host *mmc = host->mmc;
	long rate;
	u32 rval, clock = ios->clock, div = 1;
	int ret;

	ret = sunxi_mmc_oclk_onoff(host, 0);
	if (ret)
		return ret;

	/* Our clock is gated now */
	mmc->actual_clock = 0;

	if (!ios->clock)
		return 0;

	/*
	 * Under the old timing mode, 8 bit DDR requires the module
	 * clock to be double the card clock. Under the new timing
	 * mode, all DDR modes require a doubled module clock.
	 *
	 * We currently only support the standard MMC DDR52 mode.
	 * This block should be updated once support for other DDR
	 * modes is added.
	 */
	if (ios->timing == MMC_TIMING_MMC_DDR52 &&
	    (host->use_new_timings ||
	     ios->bus_width == MMC_BUS_WIDTH_8)) {
		div = 2;
		clock <<= 1;
	}

	if (host->use_new_timings && host->cfg->ccu_has_timings_switch) {
		ret = sunxi_ccu_set_mmc_timing_mode(host->clk_mmc, true);
		if (ret) {
			dev_err(mmc_dev(mmc),
				"error setting new timing mode\n");
			return ret;
		}
	}

	rate = clk_round_rate(host->clk_mmc, clock);
	if (rate < 0) {
		dev_err(mmc_dev(mmc), "error rounding clk to %d: %ld\n",
			clock, rate);
		return rate;
	}
	dev_dbg(mmc_dev(mmc), "setting clk to %d, rounded %ld\n",
		clock, rate);

	/* setting clock rate */
	ret = clk_set_rate(host->clk_mmc, rate);
	if (ret) {
		dev_err(mmc_dev(mmc), "error setting clk to %ld: %d\n",
			rate, ret);
		return ret;
	}

	/* set internal divider */
	rval = mmc_readl(host, REG_CLKCR);
	rval &= ~0xff;
	rval |= div - 1;
	mmc_writel(host, REG_CLKCR, rval);

	/* update card clock rate to account for internal divider */
	rate /= div;

	/*
	 * Configure the controller to use the new timing mode if needed.
	 * On controllers that only support the new timing mode, such as
	 * the eMMC controller on the A64, this register does not exist,
	 * and any writes to it are ignored.
	 */
	if (host->use_new_timings) {
		/* Don't touch the delay bits */
		rval = mmc_readl(host, REG_SD_NTSR);
		rval |= SDXC_2X_TIMING_MODE;
		mmc_writel(host, REG_SD_NTSR, rval);
	}

	/* sunxi_mmc_clk_set_phase expects the actual card clock rate */
	ret = sunxi_mmc_clk_set_phase(host, ios, rate);
	if (ret)
		return ret;

	ret = sunxi_mmc_calibrate(host, SDXC_REG_SAMP_DL_REG);
	if (ret)
		return ret;

	/*
	 * FIXME:
	 *
	 * In HS400 we'll also need to calibrate the data strobe
	 * signal. This should only happen on the MMC2 controller (at
	 * least on the A64).
	 */

	ret = sunxi_mmc_oclk_onoff(host, 1);
	if (ret)
		return ret;

	/* And we just enabled our clock back */
	mmc->actual_clock = rate;

	return 0;
}

static void sunxi_mmc_set_bus_width(struct sunxi_mmc_host *host,
				   unsigned char width)
{
	switch (width) {
	case MMC_BUS_WIDTH_1:
		mmc_writel(host, REG_WIDTH, SDXC_WIDTH1);
		break;
	case MMC_BUS_WIDTH_4:
		mmc_writel(host, REG_WIDTH, SDXC_WIDTH4);
		break;
	case MMC_BUS_WIDTH_8:
		mmc_writel(host, REG_WIDTH, SDXC_WIDTH8);
		break;
	}
}

static void sunxi_mmc_set_clk(struct sunxi_mmc_host *host, struct mmc_ios *ios)
{
	u32 rval;

	/* set ddr mode */
	rval = mmc_readl(host, REG_GCTRL);
	if (ios->timing == MMC_TIMING_UHS_DDR50 ||
	    ios->timing == MMC_TIMING_MMC_DDR52)
		rval |= SDXC_DDR_MODE;
	else
		rval &= ~SDXC_DDR_MODE;
	mmc_writel(host, REG_GCTRL, rval);

	host->ferror = sunxi_mmc_clk_set_rate(host, ios);
	/* Android code had a usleep_range(50000, 55000); here */
}

static void sunxi_mmc_card_power(struct sunxi_mmc_host *host,
				 struct mmc_ios *ios)
{
	struct mmc_host *mmc = host->mmc;

	switch (ios->power_mode) {
	case MMC_POWER_UP:
		dev_dbg(mmc_dev(mmc), "Powering card up\n");

		if (!IS_ERR(mmc->supply.vmmc)) {
			host->ferror = mmc_regulator_set_ocr(mmc,
							     mmc->supply.vmmc,
							     ios->vdd);
			if (host->ferror)
				return;
		}

		if (!IS_ERR(mmc->supply.vqmmc)) {
			host->ferror = regulator_enable(mmc->supply.vqmmc);
			if (host->ferror) {
				dev_err(mmc_dev(mmc),
					"failed to enable vqmmc\n");
				return;
			}
			host->vqmmc_enabled = true;
		}
		break;

	case MMC_POWER_OFF:
		dev_dbg(mmc_dev(mmc), "Powering card off\n");

		if (!IS_ERR(mmc->supply.vmmc))
			mmc_regulator_set_ocr(mmc, mmc->supply.vmmc, 0);

		if (!IS_ERR(mmc->supply.vqmmc) && host->vqmmc_enabled)
			regulator_disable(mmc->supply.vqmmc);

		host->vqmmc_enabled = false;
		break;

	default:
		dev_dbg(mmc_dev(mmc), "Ignoring unknown card power state\n");
		break;
	}
}

static void sunxi_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct sunxi_mmc_host *host = mmc_priv(mmc);

	sunxi_mmc_card_power(host, ios);
	sunxi_mmc_set_bus_width(host, ios->bus_width);
	sunxi_mmc_set_clk(host, ios);
}

static int sunxi_mmc_volt_switch(struct mmc_host *mmc, struct mmc_ios *ios)
{
	int ret;

	/* vqmmc regulator is available */
	if (!IS_ERR(mmc->supply.vqmmc)) {
		ret = mmc_regulator_set_vqmmc(mmc, ios);
		return ret < 0 ? ret : 0;
	}

	/* no vqmmc regulator, assume fixed regulator at 3/3.3V */
	if (mmc->ios.signal_voltage == MMC_SIGNAL_VOLTAGE_330)
		return 0;

	return -EINVAL;
}

static void sunxi_mmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct sunxi_mmc_host *host = mmc_priv(mmc);
	unsigned long flags;
	u32 imask;

	if (enable)
		pm_runtime_get_noresume(host->dev);

	spin_lock_irqsave(&host->lock, flags);

	imask = mmc_readl(host, REG_IMASK);
	if (enable) {
		host->sdio_imask = SDXC_SDIO_INTERRUPT;
		imask |= SDXC_SDIO_INTERRUPT;
	} else {
		host->sdio_imask = 0;
		imask &= ~SDXC_SDIO_INTERRUPT;
	}
	mmc_writel(host, REG_IMASK, imask);
	spin_unlock_irqrestore(&host->lock, flags);

	if (!enable)
		pm_runtime_put_noidle(host->mmc->parent);
}

static void sunxi_mmc_hw_reset(struct mmc_host *mmc)
{
	struct sunxi_mmc_host *host = mmc_priv(mmc);
	mmc_writel(host, REG_HWRST, 0);
	udelay(10);
	mmc_writel(host, REG_HWRST, 1);
	udelay(300);
}

static void sunxi_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct sunxi_mmc_host *host = mmc_priv(mmc);
	struct mmc_command *cmd = mrq->cmd;
	struct mmc_data *data = mrq->data;
	unsigned long iflags;
	u32 imask = SDXC_INTERRUPT_ERROR_BIT;
	u32 cmd_val = SDXC_START | (cmd->opcode & 0x3f);
	bool wait_dma = host->wait_dma;
	int ret;

	/* Check for set_ios errors (should never happen) */
	if (host->ferror) {
		mrq->cmd->error = host->ferror;
		mmc_request_done(mmc, mrq);
		return;
	}

	if (data) {
		ret = sunxi_mmc_map_dma(host, data);
		if (ret < 0) {
			dev_err(mmc_dev(mmc), "map DMA failed\n");
			cmd->error = ret;
			data->error = ret;
			mmc_request_done(mmc, mrq);
			return;
		}
	}

	if (cmd->opcode == MMC_GO_IDLE_STATE) {
		cmd_val |= SDXC_SEND_INIT_SEQUENCE;
		imask |= SDXC_COMMAND_DONE;
	}

	if (cmd->flags & MMC_RSP_PRESENT) {
		cmd_val |= SDXC_RESP_EXPIRE;
		if (cmd->flags & MMC_RSP_136)
			cmd_val |= SDXC_LONG_RESPONSE;
		if (cmd->flags & MMC_RSP_CRC)
			cmd_val |= SDXC_CHECK_RESPONSE_CRC;

		if ((cmd->flags & MMC_CMD_MASK) == MMC_CMD_ADTC) {
			cmd_val |= SDXC_DATA_EXPIRE | SDXC_WAIT_PRE_OVER;

			if (cmd->data->stop) {
				imask |= SDXC_AUTO_COMMAND_DONE;
				cmd_val |= SDXC_SEND_AUTO_STOP;
			} else {
				imask |= SDXC_DATA_OVER;
			}

			if (cmd->data->flags & MMC_DATA_WRITE)
				cmd_val |= SDXC_WRITE;
			else
				wait_dma = true;
		} else {
			imask |= SDXC_COMMAND_DONE;
		}
	} else {
		imask |= SDXC_COMMAND_DONE;
	}

	dev_dbg(mmc_dev(mmc), "cmd %d(%08x) arg %x ie 0x%08x len %d\n",
		cmd_val & 0x3f, cmd_val, cmd->arg, imask,
		mrq->data ? mrq->data->blksz * mrq->data->blocks : 0);

	spin_lock_irqsave(&host->lock, iflags);

	if (host->mrq || host->manual_stop_mrq) {
		spin_unlock_irqrestore(&host->lock, iflags);

		if (data)
			dma_unmap_sg(mmc_dev(mmc), data->sg, data->sg_len,
				     mmc_get_dma_dir(data));

		dev_err(mmc_dev(mmc), "request already pending\n");
		mrq->cmd->error = -EBUSY;
		mmc_request_done(mmc, mrq);
		return;
	}

	if (data) {
		mmc_writel(host, REG_BLKSZ, data->blksz);
		mmc_writel(host, REG_BCNTR, data->blksz * data->blocks);
		sunxi_mmc_start_dma(host, data);
	}

	host->mrq = mrq;
	host->wait_dma = wait_dma;
	mmc_writel(host, REG_IMASK, host->sdio_imask | imask);
	mmc_writel(host, REG_CARG, cmd->arg);
	mmc_writel(host, REG_CMDR, cmd_val);

	spin_unlock_irqrestore(&host->lock, iflags);
}

static int sunxi_mmc_card_busy(struct mmc_host *mmc)
{
	struct sunxi_mmc_host *host = mmc_priv(mmc);

	return !!(mmc_readl(host, REG_STAS) & SDXC_CARD_DATA_BUSY);
}

static const struct mmc_host_ops sunxi_mmc_ops = {
	.request	 = sunxi_mmc_request,
	.set_ios	 = sunxi_mmc_set_ios,
	.get_ro		 = mmc_gpio_get_ro,
	.get_cd		 = mmc_gpio_get_cd,
	.enable_sdio_irq = sunxi_mmc_enable_sdio_irq,
	.start_signal_voltage_switch = sunxi_mmc_volt_switch,
	.card_hw_reset	 = sunxi_mmc_hw_reset,
	.card_busy	 = sunxi_mmc_card_busy,
};

static const struct sunxi_mmc_clk_delay sunxi_mmc_clk_delays[] = {
	[SDXC_CLK_400K]		= { .output = 180, .sample = 180 },
	[SDXC_CLK_25M]		= { .output = 180, .sample =  75 },
	[SDXC_CLK_50M]		= { .output =  90, .sample = 120 },
	[SDXC_CLK_50M_DDR]	= { .output =  60, .sample = 120 },
	/* Value from A83T "new timing mode". Works but might not be right. */
	[SDXC_CLK_50M_DDR_8BIT]	= { .output =  90, .sample = 180 },
};

static const struct sunxi_mmc_clk_delay sun9i_mmc_clk_delays[] = {
	[SDXC_CLK_400K]		= { .output = 180, .sample = 180 },
	[SDXC_CLK_25M]		= { .output = 180, .sample =  75 },
	[SDXC_CLK_50M]		= { .output = 150, .sample = 120 },
	[SDXC_CLK_50M_DDR]	= { .output =  54, .sample =  36 },
	[SDXC_CLK_50M_DDR_8BIT]	= { .output =  72, .sample =  72 },
};

static const struct sunxi_mmc_cfg sun4i_a10_cfg = {
	.idma_des_size_bits = 13,
	.clk_delays = NULL,
	.can_calibrate = false,
};

static const struct sunxi_mmc_cfg sun5i_a13_cfg = {
	.idma_des_size_bits = 16,
	.clk_delays = NULL,
	.can_calibrate = false,
};

static const struct sunxi_mmc_cfg sun7i_a20_cfg = {
	.idma_des_size_bits = 16,
	.clk_delays = sunxi_mmc_clk_delays,
	.can_calibrate = false,
};

static const struct sunxi_mmc_cfg sun8i_a83t_emmc_cfg = {
	.idma_des_size_bits = 16,
	.clk_delays = sunxi_mmc_clk_delays,
	.can_calibrate = false,
	.ccu_has_timings_switch = true,
};

static const struct sunxi_mmc_cfg sun9i_a80_cfg = {
	.idma_des_size_bits = 16,
	.clk_delays = sun9i_mmc_clk_delays,
	.can_calibrate = false,
};

static const struct sunxi_mmc_cfg sun20i_d1_cfg = {
	.idma_des_size_bits = 13,
	.idma_des_shift = 2,
	.can_calibrate = true,
	.mask_data0 = true,
	.needs_new_timings = true,
};

static const struct sunxi_mmc_cfg sun50i_a64_cfg = {
	.idma_des_size_bits = 16,
	.clk_delays = NULL,
	.can_calibrate = true,
	.mask_data0 = true,
	.needs_new_timings = true,
};

static const struct sunxi_mmc_cfg sun50i_a64_emmc_cfg = {
	.idma_des_size_bits = 13,
	.clk_delays = NULL,
	.can_calibrate = true,
	.needs_new_timings = true,
};

static const struct sunxi_mmc_cfg sun50i_a100_cfg = {
	.idma_des_size_bits = 16,
	.idma_des_shift = 2,
	.clk_delays = NULL,
	.can_calibrate = true,
	.mask_data0 = true,
	.needs_new_timings = true,
};

static const struct sunxi_mmc_cfg sun50i_a100_emmc_cfg = {
	.idma_des_size_bits = 13,
	.idma_des_shift = 2,
	.clk_delays = NULL,
	.can_calibrate = true,
	.needs_new_timings = true,
};

static const struct of_device_id sunxi_mmc_of_match[] = {
	{ .compatible = "allwinner,sun4i-a10-mmc", .data = &sun4i_a10_cfg },
	{ .compatible = "allwinner,sun5i-a13-mmc", .data = &sun5i_a13_cfg },
	{ .compatible = "allwinner,sun7i-a20-mmc", .data = &sun7i_a20_cfg },
	{ .compatible = "allwinner,sun8i-a83t-emmc", .data = &sun8i_a83t_emmc_cfg },
	{ .compatible = "allwinner,sun9i-a80-mmc", .data = &sun9i_a80_cfg },
	{ .compatible = "allwinner,sun20i-d1-mmc", .data = &sun20i_d1_cfg },
	{ .compatible = "allwinner,sun50i-a64-mmc", .data = &sun50i_a64_cfg },
	{ .compatible = "allwinner,sun50i-a64-emmc", .data = &sun50i_a64_emmc_cfg },
	{ .compatible = "allwinner,sun50i-a100-mmc", .data = &sun50i_a100_cfg },
	{ .compatible = "allwinner,sun50i-a100-emmc", .data = &sun50i_a100_emmc_cfg },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sunxi_mmc_of_match);

static int sunxi_mmc_enable(struct sunxi_mmc_host *host)
{
	int ret;

	if (!IS_ERR(host->reset)) {
		ret = reset_control_reset(host->reset);
		if (ret) {
			dev_err(host->dev, "Couldn't reset the MMC controller (%d)\n",
				ret);
			return ret;
		}
	}

	ret = clk_prepare_enable(host->clk_ahb);
	if (ret) {
		dev_err(host->dev, "Couldn't enable the bus clocks (%d)\n", ret);
		goto error_assert_reset;
	}

	ret = clk_prepare_enable(host->clk_mmc);
	if (ret) {
		dev_err(host->dev, "Enable mmc clk err %d\n", ret);
		goto error_disable_clk_ahb;
	}

	ret = clk_prepare_enable(host->clk_output);
	if (ret) {
		dev_err(host->dev, "Enable output clk err %d\n", ret);
		goto error_disable_clk_mmc;
	}

	ret = clk_prepare_enable(host->clk_sample);
	if (ret) {
		dev_err(host->dev, "Enable sample clk err %d\n", ret);
		goto error_disable_clk_output;
	}

	/*
	 * Sometimes the controller asserts the irq on boot for some reason,
	 * make sure the controller is in a sane state before enabling irqs.
	 */
	ret = sunxi_mmc_reset_host(host);
	if (ret)
		goto error_disable_clk_sample;

	return 0;

error_disable_clk_sample:
	clk_disable_unprepare(host->clk_sample);
error_disable_clk_output:
	clk_disable_unprepare(host->clk_output);
error_disable_clk_mmc:
	clk_disable_unprepare(host->clk_mmc);
error_disable_clk_ahb:
	clk_disable_unprepare(host->clk_ahb);
error_assert_reset:
	if (!IS_ERR(host->reset))
		reset_control_assert(host->reset);
	return ret;
}

static void sunxi_mmc_disable(struct sunxi_mmc_host *host)
{
	sunxi_mmc_reset_host(host);

	clk_disable_unprepare(host->clk_sample);
	clk_disable_unprepare(host->clk_output);
	clk_disable_unprepare(host->clk_mmc);
	clk_disable_unprepare(host->clk_ahb);

	if (!IS_ERR(host->reset))
		reset_control_assert(host->reset);
}

static int sunxi_mmc_resource_request(struct sunxi_mmc_host *host,
				      struct platform_device *pdev)
{
	int ret;

	host->cfg = of_device_get_match_data(&pdev->dev);
	if (!host->cfg)
		return -EINVAL;

	ret = mmc_regulator_get_supply(host->mmc);
	if (ret)
		return ret;

	host->reg_base = devm_platform_ioremap_resource(pdev, 0);
	if (IS_ERR(host->reg_base))
		return PTR_ERR(host->reg_base);

	host->clk_ahb = devm_clk_get(&pdev->dev, "ahb");
	if (IS_ERR(host->clk_ahb)) {
		dev_err(&pdev->dev, "Could not get ahb clock\n");
		return PTR_ERR(host->clk_ahb);
	}

	host->clk_mmc = devm_clk_get(&pdev->dev, "mmc");
	if (IS_ERR(host->clk_mmc)) {
		dev_err(&pdev->dev, "Could not get mmc clock\n");
		return PTR_ERR(host->clk_mmc);
	}

	if (host->cfg->clk_delays) {
		host->clk_output = devm_clk_get(&pdev->dev, "output");
		if (IS_ERR(host->clk_output)) {
			dev_err(&pdev->dev, "Could not get output clock\n");
			return PTR_ERR(host->clk_output);
		}

		host->clk_sample = devm_clk_get(&pdev->dev, "sample");
		if (IS_ERR(host->clk_sample)) {
			dev_err(&pdev->dev, "Could not get sample clock\n");
			return PTR_ERR(host->clk_sample);
		}
	}

	host->reset = devm_reset_control_get_optional_exclusive(&pdev->dev,
								"ahb");
	if (PTR_ERR(host->reset) == -EPROBE_DEFER)
		return PTR_ERR(host->reset);

	ret = sunxi_mmc_enable(host);
	if (ret)
		return ret;

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0) {
		ret = host->irq;
		goto error_disable_mmc;
	}

	return devm_request_threaded_irq(&pdev->dev, host->irq, sunxi_mmc_irq,
			sunxi_mmc_handle_manual_stop, 0, "sunxi-mmc", host);

error_disable_mmc:
	sunxi_mmc_disable(host);
	return ret;
}

static int sunxi_mmc_probe(struct platform_device *pdev)
{
	struct sunxi_mmc_host *host;
	struct mmc_host *mmc;
	int ret;

	mmc = mmc_alloc_host(sizeof(struct sunxi_mmc_host), &pdev->dev);
	if (!mmc) {
		dev_err(&pdev->dev, "mmc alloc host failed\n");
		return -ENOMEM;
	}
	platform_set_drvdata(pdev, mmc);

	host = mmc_priv(mmc);
	host->dev = &pdev->dev;
	host->mmc = mmc;
	spin_lock_init(&host->lock);

	ret = sunxi_mmc_resource_request(host, pdev);
	if (ret)
		goto error_free_host;

	host->sg_cpu = dma_alloc_coherent(&pdev->dev, PAGE_SIZE,
					  &host->sg_dma, GFP_KERNEL);
	if (!host->sg_cpu) {
		dev_err(&pdev->dev, "Failed to allocate DMA descriptor mem\n");
		ret = -ENOMEM;
		goto error_free_host;
	}

	if (host->cfg->ccu_has_timings_switch) {
		/*
		 * Supports both old and new timing modes.
		 * Try setting the clk to new timing mode.
		 */
		sunxi_ccu_set_mmc_timing_mode(host->clk_mmc, true);

		/* And check the result */
		ret = sunxi_ccu_get_mmc_timing_mode(host->clk_mmc);
		if (ret < 0) {
			/*
			 * For whatever reason we were not able to get
			 * the current active mode. Default to old mode.
			 */
			dev_warn(&pdev->dev, "MMC clk timing mode unknown\n");
			host->use_new_timings = false;
		} else {
			host->use_new_timings = !!ret;
		}
	} else if (host->cfg->needs_new_timings) {
		/* Supports new timing mode only */
		host->use_new_timings = true;
	}

	mmc->ops		= &sunxi_mmc_ops;
	mmc->max_blk_count	= 8192;
	mmc->max_blk_size	= 4096;
	mmc->max_segs		= PAGE_SIZE / sizeof(struct sunxi_idma_des);
	mmc->max_seg_size	= (1 << host->cfg->idma_des_size_bits);
	mmc->max_req_size	= mmc->max_seg_size * mmc->max_segs;
	/* 400kHz ~ 52MHz */
	mmc->f_min		=   400000;
	mmc->f_max		= 52000000;
	mmc->caps	       |= MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED |
				  MMC_CAP_SDIO_IRQ;

	/*
	 * Some H5 devices do not have signal traces precise enough to
	 * use HS DDR mode for their eMMC chips.
	 *
	 * We still enable HS DDR modes for all the other controller
	 * variants that support them.
	 */
	if ((host->cfg->clk_delays || host->use_new_timings) &&
	    !of_device_is_compatible(pdev->dev.of_node,
				     "allwinner,sun50i-h5-emmc"))
		mmc->caps      |= MMC_CAP_1_8V_DDR | MMC_CAP_3_3V_DDR;

	ret = mmc_of_parse(mmc);
	if (ret)
		goto error_free_dma;

	/*
	 * If we don't support delay chains in the SoC, we can't use any
	 * of the higher speed modes. Mask them out in case the device
	 * tree specifies the properties for them, which gets added to
	 * the caps by mmc_of_parse() above.
	 */
	if (!(host->cfg->clk_delays || host->use_new_timings)) {
		mmc->caps &= ~(MMC_CAP_3_3V_DDR | MMC_CAP_1_8V_DDR |
			       MMC_CAP_1_2V_DDR | MMC_CAP_UHS);
		mmc->caps2 &= ~MMC_CAP2_HS200;
	}

	/* TODO: This driver doesn't support HS400 mode yet */
	mmc->caps2 &= ~MMC_CAP2_HS400;

	ret = sunxi_mmc_init_host(host);
	if (ret)
		goto error_free_dma;

	pm_runtime_set_active(&pdev->dev);
	pm_runtime_set_autosuspend_delay(&pdev->dev, 50);
	pm_runtime_use_autosuspend(&pdev->dev);
	pm_runtime_enable(&pdev->dev);

	ret = mmc_add_host(mmc);
	if (ret)
		goto error_free_dma;

	dev_info(&pdev->dev, "initialized, max. request size: %u KB%s\n",
		 mmc->max_req_size >> 10,
		 host->use_new_timings ? ", uses new timings mode" : "");

	return 0;

error_free_dma:
	dma_free_coherent(&pdev->dev, PAGE_SIZE, host->sg_cpu, host->sg_dma);
error_free_host:
	mmc_free_host(mmc);
	return ret;
}

static int sunxi_mmc_remove(struct platform_device *pdev)
{
	struct mmc_host	*mmc = platform_get_drvdata(pdev);
	struct sunxi_mmc_host *host = mmc_priv(mmc);

	mmc_remove_host(mmc);
	pm_runtime_disable(&pdev->dev);
	if (!pm_runtime_status_suspended(&pdev->dev)) {
		disable_irq(host->irq);
		sunxi_mmc_disable(host);
	}
	dma_free_coherent(&pdev->dev, PAGE_SIZE, host->sg_cpu, host->sg_dma);
	mmc_free_host(mmc);

	return 0;
}

#ifdef CONFIG_PM
static int sunxi_mmc_runtime_resume(struct device *dev)
{
	struct mmc_host	*mmc = dev_get_drvdata(dev);
	struct sunxi_mmc_host *host = mmc_priv(mmc);
	int ret;

	ret = sunxi_mmc_enable(host);
	if (ret)
		return ret;

	sunxi_mmc_init_host(host);
	sunxi_mmc_set_bus_width(host, mmc->ios.bus_width);
	sunxi_mmc_set_clk(host, &mmc->ios);
	enable_irq(host->irq);

	return 0;
}

static int sunxi_mmc_runtime_suspend(struct device *dev)
{
	struct mmc_host	*mmc = dev_get_drvdata(dev);
	struct sunxi_mmc_host *host = mmc_priv(mmc);

	/*
	 * When clocks are off, it's possible receiving
	 * fake interrupts, which will stall the system.
	 * Disabling the irq  will prevent this.
	 */
	disable_irq(host->irq);
	sunxi_mmc_reset_host(host);
	sunxi_mmc_disable(host);

	return 0;
}
#endif

static const struct dev_pm_ops sunxi_mmc_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(sunxi_mmc_runtime_suspend,
			   sunxi_mmc_runtime_resume,
			   NULL)
};

static struct platform_driver sunxi_mmc_driver = {
	.driver = {
		.name	= "sunxi-mmc",
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.of_match_table = sunxi_mmc_of_match,
		.pm = &sunxi_mmc_pm_ops,
	},
	.probe		= sunxi_mmc_probe,
	.remove		= sunxi_mmc_remove,
};
module_platform_driver(sunxi_mmc_driver);

MODULE_DESCRIPTION("Allwinner's SD/MMC Card Controller Driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("David Lanzendörfer <david.lanzendoerfer@o2s.ch>");
MODULE_ALIAS("platform:sunxi-mmc");
