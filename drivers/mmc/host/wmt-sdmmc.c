// SPDX-License-Identifier: GPL-2.0-only
/*
 *  WM8505/WM8650 SD/MMC Host Controller
 *
 *  Copyright (C) 2010 Tony Prisk
 *  Copyright (C) 2008 WonderMedia Technologies, Inc.
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/ioport.h>
#include <linux/errno.h>
#include <linux/dma-mapping.h>
#include <linux/delay.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/clk.h>
#include <linux/interrupt.h>

#include <linux/of.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/of_device.h>

#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>

#include <asm/byteorder.h>


#define DRIVER_NAME "wmt-sdhc"


/* MMC/SD controller registers */
#define SDMMC_CTLR			0x00
#define SDMMC_CMD			0x01
#define SDMMC_RSPTYPE			0x02
#define SDMMC_ARG			0x04
#define SDMMC_BUSMODE			0x08
#define SDMMC_BLKLEN			0x0C
#define SDMMC_BLKCNT			0x0E
#define SDMMC_RSP			0x10
#define SDMMC_CBCR			0x20
#define SDMMC_INTMASK0			0x24
#define SDMMC_INTMASK1			0x25
#define SDMMC_STS0			0x28
#define SDMMC_STS1			0x29
#define SDMMC_STS2			0x2A
#define SDMMC_STS3			0x2B
#define SDMMC_RSPTIMEOUT		0x2C
#define SDMMC_CLK			0x30	/* VT8500 only */
#define SDMMC_EXTCTRL			0x34
#define SDMMC_SBLKLEN			0x38
#define SDMMC_DMATIMEOUT		0x3C


/* SDMMC_CTLR bit fields */
#define CTLR_CMD_START			0x01
#define CTLR_CMD_WRITE			0x04
#define CTLR_FIFO_RESET			0x08

/* SDMMC_BUSMODE bit fields */
#define BM_SPI_MODE			0x01
#define BM_FOURBIT_MODE			0x02
#define BM_EIGHTBIT_MODE		0x04
#define BM_SD_OFF			0x10
#define BM_SPI_CS			0x20
#define BM_SD_POWER			0x40
#define BM_SOFT_RESET			0x80

/* SDMMC_BLKLEN bit fields */
#define BLKL_CRCERR_ABORT		0x0800
#define BLKL_CD_POL_HIGH		0x1000
#define BLKL_GPI_CD			0x2000
#define BLKL_DATA3_CD			0x4000
#define BLKL_INT_ENABLE			0x8000

/* SDMMC_INTMASK0 bit fields */
#define INT0_MBLK_TRAN_DONE_INT_EN	0x10
#define INT0_BLK_TRAN_DONE_INT_EN	0x20
#define INT0_CD_INT_EN			0x40
#define INT0_DI_INT_EN			0x80

/* SDMMC_INTMASK1 bit fields */
#define INT1_CMD_RES_TRAN_DONE_INT_EN	0x02
#define INT1_CMD_RES_TOUT_INT_EN	0x04
#define INT1_MBLK_AUTO_STOP_INT_EN	0x08
#define INT1_DATA_TOUT_INT_EN		0x10
#define INT1_RESCRC_ERR_INT_EN		0x20
#define INT1_RCRC_ERR_INT_EN		0x40
#define INT1_WCRC_ERR_INT_EN		0x80

/* SDMMC_STS0 bit fields */
#define STS0_WRITE_PROTECT		0x02
#define STS0_CD_DATA3			0x04
#define STS0_CD_GPI			0x08
#define STS0_MBLK_DONE			0x10
#define STS0_BLK_DONE			0x20
#define STS0_CARD_DETECT		0x40
#define STS0_DEVICE_INS			0x80

/* SDMMC_STS1 bit fields */
#define STS1_SDIO_INT			0x01
#define STS1_CMDRSP_DONE		0x02
#define STS1_RSP_TIMEOUT		0x04
#define STS1_AUTOSTOP_DONE		0x08
#define STS1_DATA_TIMEOUT		0x10
#define STS1_RSP_CRC_ERR		0x20
#define STS1_RCRC_ERR			0x40
#define STS1_WCRC_ERR			0x80

/* SDMMC_STS2 bit fields */
#define STS2_CMD_RES_BUSY		0x10
#define STS2_DATARSP_BUSY		0x20
#define STS2_DIS_FORCECLK		0x80

/* SDMMC_EXTCTRL bit fields */
#define EXT_EIGHTBIT			0x04

/* MMC/SD DMA Controller Registers */
#define SDDMA_GCR			0x100
#define SDDMA_IER			0x104
#define SDDMA_ISR			0x108
#define SDDMA_DESPR			0x10C
#define SDDMA_RBR			0x110
#define SDDMA_DAR			0x114
#define SDDMA_BAR			0x118
#define SDDMA_CPR			0x11C
#define SDDMA_CCR			0x120


/* SDDMA_GCR bit fields */
#define DMA_GCR_DMA_EN			0x00000001
#define DMA_GCR_SOFT_RESET		0x00000100

/* SDDMA_IER bit fields */
#define DMA_IER_INT_EN			0x00000001

/* SDDMA_ISR bit fields */
#define DMA_ISR_INT_STS			0x00000001

/* SDDMA_RBR bit fields */
#define DMA_RBR_FORMAT			0x40000000
#define DMA_RBR_END			0x80000000

/* SDDMA_CCR bit fields */
#define DMA_CCR_RUN			0x00000080
#define DMA_CCR_IF_TO_PERIPHERAL	0x00000000
#define DMA_CCR_PERIPHERAL_TO_IF	0x00400000

/* SDDMA_CCR event status */
#define DMA_CCR_EVT_NO_STATUS		0x00000000
#define DMA_CCR_EVT_UNDERRUN		0x00000001
#define DMA_CCR_EVT_OVERRUN		0x00000002
#define DMA_CCR_EVT_DESP_READ		0x00000003
#define DMA_CCR_EVT_DATA_RW		0x00000004
#define DMA_CCR_EVT_EARLY_END		0x00000005
#define DMA_CCR_EVT_SUCCESS		0x0000000F

#define PDMA_READ			0x00
#define PDMA_WRITE			0x01

#define WMT_SD_POWER_OFF		0
#define WMT_SD_POWER_ON			1

struct wmt_dma_descriptor {
	u32 flags;
	u32 data_buffer_addr;
	u32 branch_addr;
	u32 reserved1;
};

struct wmt_mci_caps {
	unsigned int	f_min;
	unsigned int	f_max;
	u32		ocr_avail;
	u32		caps;
	u32		max_seg_size;
	u32		max_segs;
	u32		max_blk_size;
};

struct wmt_mci_priv {
	struct mmc_host *mmc;
	void __iomem *sdmmc_base;

	int irq_regular;
	int irq_dma;

	void *dma_desc_buffer;
	dma_addr_t dma_desc_device_addr;

	struct completion cmdcomp;
	struct completion datacomp;

	struct completion *comp_cmd;
	struct completion *comp_dma;

	struct mmc_request *req;
	struct mmc_command *cmd;

	struct clk *clk_sdmmc;
	struct device *dev;

	u8 power_inverted;
	u8 cd_inverted;
};

static void wmt_set_sd_power(struct wmt_mci_priv *priv, int enable)
{
	u32 reg_tmp = readb(priv->sdmmc_base + SDMMC_BUSMODE);

	if (enable ^ priv->power_inverted)
		reg_tmp &= ~BM_SD_OFF;
	else
		reg_tmp |= BM_SD_OFF;

	writeb(reg_tmp, priv->sdmmc_base + SDMMC_BUSMODE);
}

static void wmt_mci_read_response(struct mmc_host *mmc)
{
	struct wmt_mci_priv *priv;
	int idx1, idx2;
	u8 tmp_resp;
	u32 response;

	priv = mmc_priv(mmc);

	for (idx1 = 0; idx1 < 4; idx1++) {
		response = 0;
		for (idx2 = 0; idx2 < 4; idx2++) {
			if ((idx1 == 3) && (idx2 == 3))
				tmp_resp = readb(priv->sdmmc_base + SDMMC_RSP);
			else
				tmp_resp = readb(priv->sdmmc_base + SDMMC_RSP +
						 (idx1*4) + idx2 + 1);
			response |= (tmp_resp << (idx2 * 8));
		}
		priv->cmd->resp[idx1] = cpu_to_be32(response);
	}
}

static void wmt_mci_start_command(struct wmt_mci_priv *priv)
{
	u32 reg_tmp;

	reg_tmp = readb(priv->sdmmc_base + SDMMC_CTLR);
	writeb(reg_tmp | CTLR_CMD_START, priv->sdmmc_base + SDMMC_CTLR);
}

static int wmt_mci_send_command(struct mmc_host *mmc, u8 command, u8 cmdtype,
				u32 arg, u8 rsptype)
{
	struct wmt_mci_priv *priv;
	u32 reg_tmp;

	priv = mmc_priv(mmc);

	/* write command, arg, resptype registers */
	writeb(command, priv->sdmmc_base + SDMMC_CMD);
	writel(arg, priv->sdmmc_base + SDMMC_ARG);
	writeb(rsptype, priv->sdmmc_base + SDMMC_RSPTYPE);

	/* reset response FIFO */
	reg_tmp = readb(priv->sdmmc_base + SDMMC_CTLR);
	writeb(reg_tmp | CTLR_FIFO_RESET, priv->sdmmc_base + SDMMC_CTLR);

	/* ensure clock enabled - VT3465 */
	wmt_set_sd_power(priv, WMT_SD_POWER_ON);

	/* clear status bits */
	writeb(0xFF, priv->sdmmc_base + SDMMC_STS0);
	writeb(0xFF, priv->sdmmc_base + SDMMC_STS1);
	writeb(0xFF, priv->sdmmc_base + SDMMC_STS2);
	writeb(0xFF, priv->sdmmc_base + SDMMC_STS3);

	/* set command type */
	reg_tmp = readb(priv->sdmmc_base + SDMMC_CTLR);
	writeb((reg_tmp & 0x0F) | (cmdtype << 4),
	       priv->sdmmc_base + SDMMC_CTLR);

	return 0;
}

static void wmt_mci_disable_dma(struct wmt_mci_priv *priv)
{
	writel(DMA_ISR_INT_STS, priv->sdmmc_base + SDDMA_ISR);
	writel(0, priv->sdmmc_base + SDDMA_IER);
}

static void wmt_complete_data_request(struct wmt_mci_priv *priv)
{
	struct mmc_request *req;
	req = priv->req;

	req->data->bytes_xfered = req->data->blksz * req->data->blocks;

	/* unmap the DMA pages used for write data */
	if (req->data->flags & MMC_DATA_WRITE)
		dma_unmap_sg(mmc_dev(priv->mmc), req->data->sg,
			     req->data->sg_len, DMA_TO_DEVICE);
	else
		dma_unmap_sg(mmc_dev(priv->mmc), req->data->sg,
			     req->data->sg_len, DMA_FROM_DEVICE);

	/* Check if the DMA ISR returned a data error */
	if ((req->cmd->error) || (req->data->error))
		mmc_request_done(priv->mmc, req);
	else {
		wmt_mci_read_response(priv->mmc);
		if (!req->data->stop) {
			/* single-block read/write requests end here */
			mmc_request_done(priv->mmc, req);
		} else {
			/*
			 * we change the priv->cmd variable so the response is
			 * stored in the stop struct rather than the original
			 * calling command struct
			 */
			priv->comp_cmd = &priv->cmdcomp;
			init_completion(priv->comp_cmd);
			priv->cmd = req->data->stop;
			wmt_mci_send_command(priv->mmc, req->data->stop->opcode,
					     7, req->data->stop->arg, 9);
			wmt_mci_start_command(priv);
		}
	}
}

static irqreturn_t wmt_mci_dma_isr(int irq_num, void *data)
{
	struct wmt_mci_priv *priv;

	int status;

	priv = (struct wmt_mci_priv *)data;

	status = readl(priv->sdmmc_base + SDDMA_CCR) & 0x0F;

	if (status != DMA_CCR_EVT_SUCCESS) {
		dev_err(priv->dev, "DMA Error: Status = %d\n", status);
		priv->req->data->error = -ETIMEDOUT;
		complete(priv->comp_dma);
		return IRQ_HANDLED;
	}

	priv->req->data->error = 0;

	wmt_mci_disable_dma(priv);

	complete(priv->comp_dma);

	if (priv->comp_cmd) {
		if (completion_done(priv->comp_cmd)) {
			/*
			 * if the command (regular) interrupt has already
			 * completed, finish off the request otherwise we wait
			 * for the command interrupt and finish from there.
			 */
			wmt_complete_data_request(priv);
		}
	}

	return IRQ_HANDLED;
}

static irqreturn_t wmt_mci_regular_isr(int irq_num, void *data)
{
	struct wmt_mci_priv *priv;
	u32 status0;
	u32 status1;
	u32 status2;
	u32 reg_tmp;
	int cmd_done;

	priv = (struct wmt_mci_priv *)data;
	cmd_done = 0;
	status0 = readb(priv->sdmmc_base + SDMMC_STS0);
	status1 = readb(priv->sdmmc_base + SDMMC_STS1);
	status2 = readb(priv->sdmmc_base + SDMMC_STS2);

	/* Check for card insertion */
	reg_tmp = readb(priv->sdmmc_base + SDMMC_INTMASK0);
	if ((reg_tmp & INT0_DI_INT_EN) && (status0 & STS0_DEVICE_INS)) {
		mmc_detect_change(priv->mmc, 0);
		if (priv->cmd)
			priv->cmd->error = -ETIMEDOUT;
		if (priv->comp_cmd)
			complete(priv->comp_cmd);
		if (priv->comp_dma) {
			wmt_mci_disable_dma(priv);
			complete(priv->comp_dma);
		}
		writeb(STS0_DEVICE_INS, priv->sdmmc_base + SDMMC_STS0);
		return IRQ_HANDLED;
	}

	if ((!priv->req->data) ||
	    ((priv->req->data->stop) && (priv->cmd == priv->req->data->stop))) {
		/* handle non-data & stop_transmission requests */
		if (status1 & STS1_CMDRSP_DONE) {
			priv->cmd->error = 0;
			cmd_done = 1;
		} else if ((status1 & STS1_RSP_TIMEOUT) ||
			   (status1 & STS1_DATA_TIMEOUT)) {
			priv->cmd->error = -ETIMEDOUT;
			cmd_done = 1;
		}

		if (cmd_done) {
			priv->comp_cmd = NULL;

			if (!priv->cmd->error)
				wmt_mci_read_response(priv->mmc);

			priv->cmd = NULL;

			mmc_request_done(priv->mmc, priv->req);
		}
	} else {
		/* handle data requests */
		if (status1 & STS1_CMDRSP_DONE) {
			if (priv->cmd)
				priv->cmd->error = 0;
			if (priv->comp_cmd)
				complete(priv->comp_cmd);
		}

		if ((status1 & STS1_RSP_TIMEOUT) ||
		    (status1 & STS1_DATA_TIMEOUT)) {
			if (priv->cmd)
				priv->cmd->error = -ETIMEDOUT;
			if (priv->comp_cmd)
				complete(priv->comp_cmd);
			if (priv->comp_dma) {
				wmt_mci_disable_dma(priv);
				complete(priv->comp_dma);
			}
		}

		if (priv->comp_dma) {
			/*
			 * If the dma interrupt has already completed, finish
			 * off the request; otherwise we wait for the DMA
			 * interrupt and finish from there.
			 */
			if (completion_done(priv->comp_dma))
				wmt_complete_data_request(priv);
		}
	}

	writeb(status0, priv->sdmmc_base + SDMMC_STS0);
	writeb(status1, priv->sdmmc_base + SDMMC_STS1);
	writeb(status2, priv->sdmmc_base + SDMMC_STS2);

	return IRQ_HANDLED;
}

static void wmt_reset_hardware(struct mmc_host *mmc)
{
	struct wmt_mci_priv *priv;
	u32 reg_tmp;

	priv = mmc_priv(mmc);

	/* reset controller */
	reg_tmp = readb(priv->sdmmc_base + SDMMC_BUSMODE);
	writeb(reg_tmp | BM_SOFT_RESET, priv->sdmmc_base + SDMMC_BUSMODE);

	/* reset response FIFO */
	reg_tmp = readb(priv->sdmmc_base + SDMMC_CTLR);
	writeb(reg_tmp | CTLR_FIFO_RESET, priv->sdmmc_base + SDMMC_CTLR);

	/* enable GPI pin to detect card */
	writew(BLKL_INT_ENABLE | BLKL_GPI_CD, priv->sdmmc_base + SDMMC_BLKLEN);

	/* clear interrupt status */
	writeb(0xFF, priv->sdmmc_base + SDMMC_STS0);
	writeb(0xFF, priv->sdmmc_base + SDMMC_STS1);

	/* setup interrupts */
	writeb(INT0_CD_INT_EN | INT0_DI_INT_EN, priv->sdmmc_base +
	       SDMMC_INTMASK0);
	writeb(INT1_DATA_TOUT_INT_EN | INT1_CMD_RES_TRAN_DONE_INT_EN |
	       INT1_CMD_RES_TOUT_INT_EN, priv->sdmmc_base + SDMMC_INTMASK1);

	/* set the DMA timeout */
	writew(8191, priv->sdmmc_base + SDMMC_DMATIMEOUT);

	/* auto clock freezing enable */
	reg_tmp = readb(priv->sdmmc_base + SDMMC_STS2);
	writeb(reg_tmp | STS2_DIS_FORCECLK, priv->sdmmc_base + SDMMC_STS2);

	/* set a default clock speed of 400Khz */
	clk_set_rate(priv->clk_sdmmc, 400000);
}

static int wmt_dma_init(struct mmc_host *mmc)
{
	struct wmt_mci_priv *priv;

	priv = mmc_priv(mmc);

	writel(DMA_GCR_SOFT_RESET, priv->sdmmc_base + SDDMA_GCR);
	writel(DMA_GCR_DMA_EN, priv->sdmmc_base + SDDMA_GCR);
	if ((readl(priv->sdmmc_base + SDDMA_GCR) & DMA_GCR_DMA_EN) != 0)
		return 0;
	else
		return 1;
}

static void wmt_dma_init_descriptor(struct wmt_dma_descriptor *desc,
		u16 req_count, u32 buffer_addr, u32 branch_addr, int end)
{
	desc->flags = 0x40000000 | req_count;
	if (end)
		desc->flags |= 0x80000000;
	desc->data_buffer_addr = buffer_addr;
	desc->branch_addr = branch_addr;
}

static void wmt_dma_config(struct mmc_host *mmc, u32 descaddr, u8 dir)
{
	struct wmt_mci_priv *priv;
	u32 reg_tmp;

	priv = mmc_priv(mmc);

	/* Enable DMA Interrupts */
	writel(DMA_IER_INT_EN, priv->sdmmc_base + SDDMA_IER);

	/* Write DMA Descriptor Pointer Register */
	writel(descaddr, priv->sdmmc_base + SDDMA_DESPR);

	writel(0x00, priv->sdmmc_base + SDDMA_CCR);

	if (dir == PDMA_WRITE) {
		reg_tmp = readl(priv->sdmmc_base + SDDMA_CCR);
		writel(reg_tmp & DMA_CCR_IF_TO_PERIPHERAL, priv->sdmmc_base +
		       SDDMA_CCR);
	} else {
		reg_tmp = readl(priv->sdmmc_base + SDDMA_CCR);
		writel(reg_tmp | DMA_CCR_PERIPHERAL_TO_IF, priv->sdmmc_base +
		       SDDMA_CCR);
	}
}

static void wmt_dma_start(struct wmt_mci_priv *priv)
{
	u32 reg_tmp;

	reg_tmp = readl(priv->sdmmc_base + SDDMA_CCR);
	writel(reg_tmp | DMA_CCR_RUN, priv->sdmmc_base + SDDMA_CCR);
}

static void wmt_mci_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct wmt_mci_priv *priv;
	struct wmt_dma_descriptor *desc;
	u8 command;
	u8 cmdtype;
	u32 arg;
	u8 rsptype;
	u32 reg_tmp;

	struct scatterlist *sg;
	int i;
	int sg_cnt;
	int offset;
	u32 dma_address;
	int desc_cnt;

	priv = mmc_priv(mmc);
	priv->req = req;

	/*
	 * Use the cmd variable to pass a pointer to the resp[] structure
	 * This is required on multi-block requests to pass the pointer to the
	 * stop command
	 */
	priv->cmd = req->cmd;

	command = req->cmd->opcode;
	arg = req->cmd->arg;
	rsptype = mmc_resp_type(req->cmd);
	cmdtype = 0;

	/* rsptype=7 only valid for SPI commands - should be =2 for SD */
	if (rsptype == 7)
		rsptype = 2;
	/* rsptype=21 is R1B, convert for controller */
	if (rsptype == 21)
		rsptype = 9;

	if (!req->data) {
		wmt_mci_send_command(mmc, command, cmdtype, arg, rsptype);
		wmt_mci_start_command(priv);
		/* completion is now handled in the regular_isr() */
	}
	if (req->data) {
		priv->comp_cmd = &priv->cmdcomp;
		init_completion(priv->comp_cmd);

		wmt_dma_init(mmc);

		/* set controller data length */
		reg_tmp = readw(priv->sdmmc_base + SDMMC_BLKLEN);
		writew((reg_tmp & 0xF800) | (req->data->blksz - 1),
		       priv->sdmmc_base + SDMMC_BLKLEN);

		/* set controller block count */
		writew(req->data->blocks, priv->sdmmc_base + SDMMC_BLKCNT);

		desc = (struct wmt_dma_descriptor *)priv->dma_desc_buffer;

		if (req->data->flags & MMC_DATA_WRITE) {
			sg_cnt = dma_map_sg(mmc_dev(mmc), req->data->sg,
					    req->data->sg_len, DMA_TO_DEVICE);
			cmdtype = 1;
			if (req->data->blocks > 1)
				cmdtype = 3;
		} else {
			sg_cnt = dma_map_sg(mmc_dev(mmc), req->data->sg,
					    req->data->sg_len, DMA_FROM_DEVICE);
			cmdtype = 2;
			if (req->data->blocks > 1)
				cmdtype = 4;
		}

		dma_address = priv->dma_desc_device_addr + 16;
		desc_cnt = 0;

		for_each_sg(req->data->sg, sg, sg_cnt, i) {
			offset = 0;
			while (offset < sg_dma_len(sg)) {
				wmt_dma_init_descriptor(desc, req->data->blksz,
						sg_dma_address(sg)+offset,
						dma_address, 0);
				desc++;
				desc_cnt++;
				offset += req->data->blksz;
				dma_address += 16;
				if (desc_cnt == req->data->blocks)
					break;
			}
		}
		desc--;
		desc->flags |= 0x80000000;

		if (req->data->flags & MMC_DATA_WRITE)
			wmt_dma_config(mmc, priv->dma_desc_device_addr,
				       PDMA_WRITE);
		else
			wmt_dma_config(mmc, priv->dma_desc_device_addr,
				       PDMA_READ);

		wmt_mci_send_command(mmc, command, cmdtype, arg, rsptype);

		priv->comp_dma = &priv->datacomp;
		init_completion(priv->comp_dma);

		wmt_dma_start(priv);
		wmt_mci_start_command(priv);
	}
}

static void wmt_mci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct wmt_mci_priv *priv;
	u32 busmode, extctrl;

	priv = mmc_priv(mmc);

	if (ios->power_mode == MMC_POWER_UP) {
		wmt_reset_hardware(mmc);

		wmt_set_sd_power(priv, WMT_SD_POWER_ON);
	}
	if (ios->power_mode == MMC_POWER_OFF)
		wmt_set_sd_power(priv, WMT_SD_POWER_OFF);

	if (ios->clock != 0)
		clk_set_rate(priv->clk_sdmmc, ios->clock);

	busmode = readb(priv->sdmmc_base + SDMMC_BUSMODE);
	extctrl = readb(priv->sdmmc_base + SDMMC_EXTCTRL);

	busmode &= ~(BM_EIGHTBIT_MODE | BM_FOURBIT_MODE);
	extctrl &= ~EXT_EIGHTBIT;

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_8:
		busmode |= BM_EIGHTBIT_MODE;
		extctrl |= EXT_EIGHTBIT;
		break;
	case MMC_BUS_WIDTH_4:
		busmode |= BM_FOURBIT_MODE;
		break;
	case MMC_BUS_WIDTH_1:
		break;
	}

	writeb(busmode, priv->sdmmc_base + SDMMC_BUSMODE);
	writeb(extctrl, priv->sdmmc_base + SDMMC_EXTCTRL);
}

static int wmt_mci_get_ro(struct mmc_host *mmc)
{
	struct wmt_mci_priv *priv = mmc_priv(mmc);

	return !(readb(priv->sdmmc_base + SDMMC_STS0) & STS0_WRITE_PROTECT);
}

static int wmt_mci_get_cd(struct mmc_host *mmc)
{
	struct wmt_mci_priv *priv = mmc_priv(mmc);
	u32 cd = (readb(priv->sdmmc_base + SDMMC_STS0) & STS0_CD_GPI) >> 3;

	return !(cd ^ priv->cd_inverted);
}

static const struct mmc_host_ops wmt_mci_ops = {
	.request = wmt_mci_request,
	.set_ios = wmt_mci_set_ios,
	.get_ro = wmt_mci_get_ro,
	.get_cd = wmt_mci_get_cd,
};

/* Controller capabilities */
static struct wmt_mci_caps wm8505_caps = {
	.f_min = 390425,
	.f_max = 50000000,
	.ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34,
	.caps = MMC_CAP_4_BIT_DATA | MMC_CAP_MMC_HIGHSPEED |
		MMC_CAP_SD_HIGHSPEED,
	.max_seg_size = 65024,
	.max_segs = 128,
	.max_blk_size = 2048,
};

static const struct of_device_id wmt_mci_dt_ids[] = {
	{ .compatible = "wm,wm8505-sdhc", .data = &wm8505_caps },
	{ /* Sentinel */ },
};

static int wmt_mci_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct wmt_mci_priv *priv;
	struct device_node *np = pdev->dev.of_node;
	const struct wmt_mci_caps *wmt_caps;
	int ret;
	int regular_irq, dma_irq;

	wmt_caps = of_device_get_match_data(&pdev->dev);
	if (!wmt_caps) {
		dev_err(&pdev->dev, "Controller capabilities data missing\n");
		return -EFAULT;
	}

	if (!np) {
		dev_err(&pdev->dev, "Missing SDMMC description in devicetree\n");
		return -EFAULT;
	}

	regular_irq = irq_of_parse_and_map(np, 0);
	dma_irq = irq_of_parse_and_map(np, 1);

	if (!regular_irq || !dma_irq) {
		dev_err(&pdev->dev, "Getting IRQs failed!\n");
		ret = -ENXIO;
		goto fail1;
	}

	mmc = mmc_alloc_host(sizeof(struct wmt_mci_priv), &pdev->dev);
	if (!mmc) {
		dev_err(&pdev->dev, "Failed to allocate mmc_host\n");
		ret = -ENOMEM;
		goto fail1;
	}

	mmc->ops = &wmt_mci_ops;
	mmc->f_min = wmt_caps->f_min;
	mmc->f_max = wmt_caps->f_max;
	mmc->ocr_avail = wmt_caps->ocr_avail;
	mmc->caps = wmt_caps->caps;

	mmc->max_seg_size = wmt_caps->max_seg_size;
	mmc->max_segs = wmt_caps->max_segs;
	mmc->max_blk_size = wmt_caps->max_blk_size;

	mmc->max_req_size = (16*512*mmc->max_segs);
	mmc->max_blk_count = mmc->max_req_size / 512;

	priv = mmc_priv(mmc);
	priv->mmc = mmc;
	priv->dev = &pdev->dev;

	priv->power_inverted = 0;
	priv->cd_inverted = 0;

	priv->power_inverted = of_property_read_bool(np, "sdon-inverted");
	priv->cd_inverted = of_property_read_bool(np, "cd-inverted");

	priv->sdmmc_base = of_iomap(np, 0);
	if (!priv->sdmmc_base) {
		dev_err(&pdev->dev, "Failed to map IO space\n");
		ret = -ENOMEM;
		goto fail2;
	}

	priv->irq_regular = regular_irq;
	priv->irq_dma = dma_irq;

	ret = request_irq(regular_irq, wmt_mci_regular_isr, 0, "sdmmc", priv);
	if (ret) {
		dev_err(&pdev->dev, "Register regular IRQ fail\n");
		goto fail3;
	}

	ret = request_irq(dma_irq, wmt_mci_dma_isr, 0, "sdmmc", priv);
	if (ret) {
		dev_err(&pdev->dev, "Register DMA IRQ fail\n");
		goto fail4;
	}

	/* alloc some DMA buffers for descriptors/transfers */
	priv->dma_desc_buffer = dma_alloc_coherent(&pdev->dev,
						   mmc->max_blk_count * 16,
						   &priv->dma_desc_device_addr,
						   GFP_KERNEL);
	if (!priv->dma_desc_buffer) {
		dev_err(&pdev->dev, "DMA alloc fail\n");
		ret = -EPERM;
		goto fail5;
	}

	platform_set_drvdata(pdev, mmc);

	priv->clk_sdmmc = of_clk_get(np, 0);
	if (IS_ERR(priv->clk_sdmmc)) {
		dev_err(&pdev->dev, "Error getting clock\n");
		ret = PTR_ERR(priv->clk_sdmmc);
		goto fail5_and_a_half;
	}

	ret = clk_prepare_enable(priv->clk_sdmmc);
	if (ret)
		goto fail6;

	/* configure the controller to a known 'ready' state */
	wmt_reset_hardware(mmc);

	ret = mmc_add_host(mmc);
	if (ret)
		goto fail7;

	dev_info(&pdev->dev, "WMT SDHC Controller initialized\n");

	return 0;
fail7:
	clk_disable_unprepare(priv->clk_sdmmc);
fail6:
	clk_put(priv->clk_sdmmc);
fail5_and_a_half:
	dma_free_coherent(&pdev->dev, mmc->max_blk_count * 16,
			  priv->dma_desc_buffer, priv->dma_desc_device_addr);
fail5:
	free_irq(dma_irq, priv);
fail4:
	free_irq(regular_irq, priv);
fail3:
	iounmap(priv->sdmmc_base);
fail2:
	mmc_free_host(mmc);
fail1:
	return ret;
}

static int wmt_mci_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct wmt_mci_priv *priv;
	struct resource *res;
	u32 reg_tmp;

	mmc = platform_get_drvdata(pdev);
	priv = mmc_priv(mmc);

	/* reset SD controller */
	reg_tmp = readb(priv->sdmmc_base + SDMMC_BUSMODE);
	writel(reg_tmp | BM_SOFT_RESET, priv->sdmmc_base + SDMMC_BUSMODE);
	reg_tmp = readw(priv->sdmmc_base + SDMMC_BLKLEN);
	writew(reg_tmp & ~(0xA000), priv->sdmmc_base + SDMMC_BLKLEN);
	writeb(0xFF, priv->sdmmc_base + SDMMC_STS0);
	writeb(0xFF, priv->sdmmc_base + SDMMC_STS1);

	/* release the dma buffers */
	dma_free_coherent(&pdev->dev, priv->mmc->max_blk_count * 16,
			  priv->dma_desc_buffer, priv->dma_desc_device_addr);

	mmc_remove_host(mmc);

	free_irq(priv->irq_regular, priv);
	free_irq(priv->irq_dma, priv);

	iounmap(priv->sdmmc_base);

	clk_disable_unprepare(priv->clk_sdmmc);
	clk_put(priv->clk_sdmmc);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	mmc_free_host(mmc);

	dev_info(&pdev->dev, "WMT MCI device removed\n");

	return 0;
}

#ifdef CONFIG_PM
static int wmt_mci_suspend(struct device *dev)
{
	u32 reg_tmp;
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct wmt_mci_priv *priv;

	if (!mmc)
		return 0;

	priv = mmc_priv(mmc);
	reg_tmp = readb(priv->sdmmc_base + SDMMC_BUSMODE);
	writeb(reg_tmp | BM_SOFT_RESET, priv->sdmmc_base +
	       SDMMC_BUSMODE);

	reg_tmp = readw(priv->sdmmc_base + SDMMC_BLKLEN);
	writew(reg_tmp & 0x5FFF, priv->sdmmc_base + SDMMC_BLKLEN);

	writeb(0xFF, priv->sdmmc_base + SDMMC_STS0);
	writeb(0xFF, priv->sdmmc_base + SDMMC_STS1);

	clk_disable(priv->clk_sdmmc);
	return 0;
}

static int wmt_mci_resume(struct device *dev)
{
	u32 reg_tmp;
	struct mmc_host *mmc = dev_get_drvdata(dev);
	struct wmt_mci_priv *priv;

	if (mmc) {
		priv = mmc_priv(mmc);
		clk_enable(priv->clk_sdmmc);

		reg_tmp = readb(priv->sdmmc_base + SDMMC_BUSMODE);
		writeb(reg_tmp | BM_SOFT_RESET, priv->sdmmc_base +
		       SDMMC_BUSMODE);

		reg_tmp = readw(priv->sdmmc_base + SDMMC_BLKLEN);
		writew(reg_tmp | (BLKL_GPI_CD | BLKL_INT_ENABLE),
		       priv->sdmmc_base + SDMMC_BLKLEN);

		reg_tmp = readb(priv->sdmmc_base + SDMMC_INTMASK0);
		writeb(reg_tmp | INT0_DI_INT_EN, priv->sdmmc_base +
		       SDMMC_INTMASK0);

	}

	return 0;
}

static const struct dev_pm_ops wmt_mci_pm = {
	.suspend        = wmt_mci_suspend,
	.resume         = wmt_mci_resume,
};

#define wmt_mci_pm_ops (&wmt_mci_pm)

#else	/* !CONFIG_PM */

#define wmt_mci_pm_ops NULL

#endif

static struct platform_driver wmt_mci_driver = {
	.probe = wmt_mci_probe,
	.remove = wmt_mci_remove,
	.driver = {
		.name = DRIVER_NAME,
		.probe_type = PROBE_PREFER_ASYNCHRONOUS,
		.pm = wmt_mci_pm_ops,
		.of_match_table = wmt_mci_dt_ids,
	},
};

module_platform_driver(wmt_mci_driver);

MODULE_DESCRIPTION("Wondermedia MMC/SD Driver");
MODULE_AUTHOR("Tony Prisk");
MODULE_LICENSE("GPL v2");
MODULE_DEVICE_TABLE(of, wmt_mci_dt_ids);
