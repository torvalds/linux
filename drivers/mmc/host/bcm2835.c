/*
 * bcm2835 sdhost driver.
 *
 * The 2835 has two SD controllers: The Arasan sdhci controller
 * (supported by the iproc driver) and a custom sdhost controller
 * (supported by this driver).
 *
 * The sdhci controller supports both sdcard and sdio.  The sdhost
 * controller supports the sdcard only, but has better performance.
 * Also note that the rpi3 has sdio wifi, so driving the sdcard with
 * the sdhost controller allows to use the sdhci controller for wifi
 * support.
 *
 * The configuration is done by devicetree via pin muxing.  Both
 * SD controller are available on the same pins (2 pin groups = pin 22
 * to 27 + pin 48 to 53).  So it's possible to use both SD controllers
 * at the same time with different pin groups.
 *
 * Author:      Phil Elwell <phil@raspberrypi.org>
 *              Copyright (C) 2015-2016 Raspberry Pi (Trading) Ltd.
 *
 * Based on
 *  mmc-bcm2835.c by Gellert Weisz
 * which is, in turn, based on
 *  sdhci-bcm2708.c by Broadcom
 *  sdhci-bcm2835.c by Stephen Warren and Oleksandr Tymoshenko
 *  sdhci.c and sdhci-pci.c by Pierre Ossman
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/highmem.h>
#include <linux/interrupt.h>
#include <linux/io.h>
#include <linux/iopoll.h>
#include <linux/module.h>
#include <linux/of_address.h>
#include <linux/of_irq.h>
#include <linux/platform_device.h>
#include <linux/scatterlist.h>
#include <linux/time.h>
#include <linux/workqueue.h>

#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>

#define SDCMD  0x00 /* Command to SD card              - 16 R/W */
#define SDARG  0x04 /* Argument to SD card             - 32 R/W */
#define SDTOUT 0x08 /* Start value for timeout counter - 32 R/W */
#define SDCDIV 0x0c /* Start value for clock divider   - 11 R/W */
#define SDRSP0 0x10 /* SD card response (31:0)         - 32 R   */
#define SDRSP1 0x14 /* SD card response (63:32)        - 32 R   */
#define SDRSP2 0x18 /* SD card response (95:64)        - 32 R   */
#define SDRSP3 0x1c /* SD card response (127:96)       - 32 R   */
#define SDHSTS 0x20 /* SD host status                  - 11 R/W */
#define SDVDD  0x30 /* SD card power control           -  1 R/W */
#define SDEDM  0x34 /* Emergency Debug Mode            - 13 R/W */
#define SDHCFG 0x38 /* Host configuration              -  2 R/W */
#define SDHBCT 0x3c /* Host byte count (debug)         - 32 R/W */
#define SDDATA 0x40 /* Data to/from SD card            - 32 R/W */
#define SDHBLC 0x50 /* Host block count (SDIO/SDHC)    -  9 R/W */

#define SDCMD_NEW_FLAG			0x8000
#define SDCMD_FAIL_FLAG			0x4000
#define SDCMD_BUSYWAIT			0x800
#define SDCMD_NO_RESPONSE		0x400
#define SDCMD_LONG_RESPONSE		0x200
#define SDCMD_WRITE_CMD			0x80
#define SDCMD_READ_CMD			0x40
#define SDCMD_CMD_MASK			0x3f

#define SDCDIV_MAX_CDIV			0x7ff

#define SDHSTS_BUSY_IRPT		0x400
#define SDHSTS_BLOCK_IRPT		0x200
#define SDHSTS_SDIO_IRPT		0x100
#define SDHSTS_REW_TIME_OUT		0x80
#define SDHSTS_CMD_TIME_OUT		0x40
#define SDHSTS_CRC16_ERROR		0x20
#define SDHSTS_CRC7_ERROR		0x10
#define SDHSTS_FIFO_ERROR		0x08
/* Reserved */
/* Reserved */
#define SDHSTS_DATA_FLAG		0x01

#define SDHSTS_TRANSFER_ERROR_MASK	(SDHSTS_CRC7_ERROR | \
					 SDHSTS_CRC16_ERROR | \
					 SDHSTS_REW_TIME_OUT | \
					 SDHSTS_FIFO_ERROR)

#define SDHSTS_ERROR_MASK		(SDHSTS_CMD_TIME_OUT | \
					 SDHSTS_TRANSFER_ERROR_MASK)

#define SDHCFG_BUSY_IRPT_EN	BIT(10)
#define SDHCFG_BLOCK_IRPT_EN	BIT(8)
#define SDHCFG_SDIO_IRPT_EN	BIT(5)
#define SDHCFG_DATA_IRPT_EN	BIT(4)
#define SDHCFG_SLOW_CARD	BIT(3)
#define SDHCFG_WIDE_EXT_BUS	BIT(2)
#define SDHCFG_WIDE_INT_BUS	BIT(1)
#define SDHCFG_REL_CMD_LINE	BIT(0)

#define SDVDD_POWER_OFF		0
#define SDVDD_POWER_ON		1

#define SDEDM_FORCE_DATA_MODE	BIT(19)
#define SDEDM_CLOCK_PULSE	BIT(20)
#define SDEDM_BYPASS		BIT(21)

#define SDEDM_WRITE_THRESHOLD_SHIFT	9
#define SDEDM_READ_THRESHOLD_SHIFT	14
#define SDEDM_THRESHOLD_MASK		0x1f

#define SDEDM_FSM_MASK		0xf
#define SDEDM_FSM_IDENTMODE	0x0
#define SDEDM_FSM_DATAMODE	0x1
#define SDEDM_FSM_READDATA	0x2
#define SDEDM_FSM_WRITEDATA	0x3
#define SDEDM_FSM_READWAIT	0x4
#define SDEDM_FSM_READCRC	0x5
#define SDEDM_FSM_WRITECRC	0x6
#define SDEDM_FSM_WRITEWAIT1	0x7
#define SDEDM_FSM_POWERDOWN	0x8
#define SDEDM_FSM_POWERUP	0x9
#define SDEDM_FSM_WRITESTART1	0xa
#define SDEDM_FSM_WRITESTART2	0xb
#define SDEDM_FSM_GENPULSES	0xc
#define SDEDM_FSM_WRITEWAIT2	0xd
#define SDEDM_FSM_STARTPOWDOWN	0xf

#define SDDATA_FIFO_WORDS	16

#define FIFO_READ_THRESHOLD	4
#define FIFO_WRITE_THRESHOLD	4
#define SDDATA_FIFO_PIO_BURST	8

#define PIO_THRESHOLD	1  /* Maximum block count for PIO (0 = always DMA) */

struct bcm2835_host {
	spinlock_t		lock;
	struct mutex		mutex;

	void __iomem		*ioaddr;
	u32			phys_addr;

	struct mmc_host		*mmc;
	struct platform_device	*pdev;

	int			clock;		/* Current clock speed */
	unsigned int		max_clk;	/* Max possible freq */
	struct work_struct	dma_work;
	struct delayed_work	timeout_work;	/* Timer for timeouts */
	struct sg_mapping_iter	sg_miter;	/* SG state for PIO */
	unsigned int		blocks;		/* remaining PIO blocks */
	int			irq;		/* Device IRQ */

	u32			ns_per_fifo_word;

	/* cached registers */
	u32			hcfg;
	u32			cdiv;

	struct mmc_request	*mrq;		/* Current request */
	struct mmc_command	*cmd;		/* Current command */
	struct mmc_data		*data;		/* Current data request */
	bool			data_complete:1;/* Data finished before cmd */
	bool			use_busy:1;	/* Wait for busy interrupt */
	bool			use_sbc:1;	/* Send CMD23 */

	/* for threaded irq handler */
	bool			irq_block;
	bool			irq_busy;
	bool			irq_data;

	/* DMA part */
	struct dma_chan		*dma_chan_rxtx;
	struct dma_chan		*dma_chan;
	struct dma_slave_config dma_cfg_rx;
	struct dma_slave_config dma_cfg_tx;
	struct dma_async_tx_descriptor	*dma_desc;
	u32			dma_dir;
	u32			drain_words;
	struct page		*drain_page;
	u32			drain_offset;
	bool			use_dma;
};

static void bcm2835_dumpcmd(struct bcm2835_host *host, struct mmc_command *cmd,
			    const char *label)
{
	struct device *dev = &host->pdev->dev;

	if (!cmd)
		return;

	dev_dbg(dev, "%c%s op %d arg 0x%x flags 0x%x - resp %08x %08x %08x %08x, err %d\n",
		(cmd == host->cmd) ? '>' : ' ',
		label, cmd->opcode, cmd->arg, cmd->flags,
		cmd->resp[0], cmd->resp[1], cmd->resp[2], cmd->resp[3],
		cmd->error);
}

static void bcm2835_dumpregs(struct bcm2835_host *host)
{
	struct mmc_request *mrq = host->mrq;
	struct device *dev = &host->pdev->dev;

	if (mrq) {
		bcm2835_dumpcmd(host, mrq->sbc, "sbc");
		bcm2835_dumpcmd(host, mrq->cmd, "cmd");
		if (mrq->data) {
			dev_dbg(dev, "data blocks %x blksz %x - err %d\n",
				mrq->data->blocks,
				mrq->data->blksz,
				mrq->data->error);
		}
		bcm2835_dumpcmd(host, mrq->stop, "stop");
	}

	dev_dbg(dev, "=========== REGISTER DUMP ===========\n");
	dev_dbg(dev, "SDCMD  0x%08x\n", readl(host->ioaddr + SDCMD));
	dev_dbg(dev, "SDARG  0x%08x\n", readl(host->ioaddr + SDARG));
	dev_dbg(dev, "SDTOUT 0x%08x\n", readl(host->ioaddr + SDTOUT));
	dev_dbg(dev, "SDCDIV 0x%08x\n", readl(host->ioaddr + SDCDIV));
	dev_dbg(dev, "SDRSP0 0x%08x\n", readl(host->ioaddr + SDRSP0));
	dev_dbg(dev, "SDRSP1 0x%08x\n", readl(host->ioaddr + SDRSP1));
	dev_dbg(dev, "SDRSP2 0x%08x\n", readl(host->ioaddr + SDRSP2));
	dev_dbg(dev, "SDRSP3 0x%08x\n", readl(host->ioaddr + SDRSP3));
	dev_dbg(dev, "SDHSTS 0x%08x\n", readl(host->ioaddr + SDHSTS));
	dev_dbg(dev, "SDVDD  0x%08x\n", readl(host->ioaddr + SDVDD));
	dev_dbg(dev, "SDEDM  0x%08x\n", readl(host->ioaddr + SDEDM));
	dev_dbg(dev, "SDHCFG 0x%08x\n", readl(host->ioaddr + SDHCFG));
	dev_dbg(dev, "SDHBCT 0x%08x\n", readl(host->ioaddr + SDHBCT));
	dev_dbg(dev, "SDHBLC 0x%08x\n", readl(host->ioaddr + SDHBLC));
	dev_dbg(dev, "===========================================\n");
}

static void bcm2835_reset_internal(struct bcm2835_host *host)
{
	u32 temp;

	writel(SDVDD_POWER_OFF, host->ioaddr + SDVDD);
	writel(0, host->ioaddr + SDCMD);
	writel(0, host->ioaddr + SDARG);
	writel(0xf00000, host->ioaddr + SDTOUT);
	writel(0, host->ioaddr + SDCDIV);
	writel(0x7f8, host->ioaddr + SDHSTS); /* Write 1s to clear */
	writel(0, host->ioaddr + SDHCFG);
	writel(0, host->ioaddr + SDHBCT);
	writel(0, host->ioaddr + SDHBLC);

	/* Limit fifo usage due to silicon bug */
	temp = readl(host->ioaddr + SDEDM);
	temp &= ~((SDEDM_THRESHOLD_MASK << SDEDM_READ_THRESHOLD_SHIFT) |
		  (SDEDM_THRESHOLD_MASK << SDEDM_WRITE_THRESHOLD_SHIFT));
	temp |= (FIFO_READ_THRESHOLD << SDEDM_READ_THRESHOLD_SHIFT) |
		(FIFO_WRITE_THRESHOLD << SDEDM_WRITE_THRESHOLD_SHIFT);
	writel(temp, host->ioaddr + SDEDM);
	msleep(20);
	writel(SDVDD_POWER_ON, host->ioaddr + SDVDD);
	msleep(20);
	host->clock = 0;
	writel(host->hcfg, host->ioaddr + SDHCFG);
	writel(host->cdiv, host->ioaddr + SDCDIV);
}

static void bcm2835_reset(struct mmc_host *mmc)
{
	struct bcm2835_host *host = mmc_priv(mmc);

	if (host->dma_chan)
		dmaengine_terminate_sync(host->dma_chan);
	bcm2835_reset_internal(host);
}

static void bcm2835_finish_command(struct bcm2835_host *host);

static void bcm2835_wait_transfer_complete(struct bcm2835_host *host)
{
	int timediff;
	u32 alternate_idle;

	alternate_idle = (host->mrq->data->flags & MMC_DATA_READ) ?
		SDEDM_FSM_READWAIT : SDEDM_FSM_WRITESTART1;

	timediff = 0;

	while (1) {
		u32 edm, fsm;

		edm = readl(host->ioaddr + SDEDM);
		fsm = edm & SDEDM_FSM_MASK;

		if ((fsm == SDEDM_FSM_IDENTMODE) ||
		    (fsm == SDEDM_FSM_DATAMODE))
			break;
		if (fsm == alternate_idle) {
			writel(edm | SDEDM_FORCE_DATA_MODE,
			       host->ioaddr + SDEDM);
			break;
		}

		timediff++;
		if (timediff == 100000) {
			dev_err(&host->pdev->dev,
				"wait_transfer_complete - still waiting after %d retries\n",
				timediff);
			bcm2835_dumpregs(host);
			host->mrq->data->error = -ETIMEDOUT;
			return;
		}
		cpu_relax();
	}
}

static void bcm2835_dma_complete(void *param)
{
	struct bcm2835_host *host = param;

	schedule_work(&host->dma_work);
}

static void bcm2835_transfer_block_pio(struct bcm2835_host *host, bool is_read)
{
	unsigned long flags;
	size_t blksize;
	unsigned long wait_max;

	blksize = host->data->blksz;

	wait_max = jiffies + msecs_to_jiffies(500);

	local_irq_save(flags);

	while (blksize) {
		int copy_words;
		u32 hsts = 0;
		size_t len;
		u32 *buf;

		if (!sg_miter_next(&host->sg_miter)) {
			host->data->error = -EINVAL;
			break;
		}

		len = min(host->sg_miter.length, blksize);
		if (len % 4) {
			host->data->error = -EINVAL;
			break;
		}

		blksize -= len;
		host->sg_miter.consumed = len;

		buf = (u32 *)host->sg_miter.addr;

		copy_words = len / 4;

		while (copy_words) {
			int burst_words, words;
			u32 edm;

			burst_words = min(SDDATA_FIFO_PIO_BURST, copy_words);
			edm = readl(host->ioaddr + SDEDM);
			if (is_read)
				words = ((edm >> 4) & 0x1f);
			else
				words = SDDATA_FIFO_WORDS - ((edm >> 4) & 0x1f);

			if (words < burst_words) {
				int fsm_state = (edm & SDEDM_FSM_MASK);
				struct device *dev = &host->pdev->dev;

				if ((is_read &&
				     (fsm_state != SDEDM_FSM_READDATA &&
				      fsm_state != SDEDM_FSM_READWAIT &&
				      fsm_state != SDEDM_FSM_READCRC)) ||
				    (!is_read &&
				     (fsm_state != SDEDM_FSM_WRITEDATA &&
				      fsm_state != SDEDM_FSM_WRITESTART1 &&
				      fsm_state != SDEDM_FSM_WRITESTART2))) {
					hsts = readl(host->ioaddr + SDHSTS);
					dev_err(dev, "fsm %x, hsts %08x\n",
						fsm_state, hsts);
					if (hsts & SDHSTS_ERROR_MASK)
						break;
				}

				if (time_after(jiffies, wait_max)) {
					dev_err(dev, "PIO %s timeout - EDM %08x\n",
						is_read ? "read" : "write",
						edm);
					hsts = SDHSTS_REW_TIME_OUT;
					break;
				}
				ndelay((burst_words - words) *
				       host->ns_per_fifo_word);
				continue;
			} else if (words > copy_words) {
				words = copy_words;
			}

			copy_words -= words;

			while (words) {
				if (is_read)
					*(buf++) = readl(host->ioaddr + SDDATA);
				else
					writel(*(buf++), host->ioaddr + SDDATA);
				words--;
			}
		}

		if (hsts & SDHSTS_ERROR_MASK)
			break;
	}

	sg_miter_stop(&host->sg_miter);

	local_irq_restore(flags);
}

static void bcm2835_transfer_pio(struct bcm2835_host *host)
{
	struct device *dev = &host->pdev->dev;
	u32 sdhsts;
	bool is_read;

	is_read = (host->data->flags & MMC_DATA_READ) != 0;
	bcm2835_transfer_block_pio(host, is_read);

	sdhsts = readl(host->ioaddr + SDHSTS);
	if (sdhsts & (SDHSTS_CRC16_ERROR |
		      SDHSTS_CRC7_ERROR |
		      SDHSTS_FIFO_ERROR)) {
		dev_err(dev, "%s transfer error - HSTS %08x\n",
			is_read ? "read" : "write", sdhsts);
		host->data->error = -EILSEQ;
	} else if ((sdhsts & (SDHSTS_CMD_TIME_OUT |
			      SDHSTS_REW_TIME_OUT))) {
		dev_err(dev, "%s timeout error - HSTS %08x\n",
			is_read ? "read" : "write", sdhsts);
		host->data->error = -ETIMEDOUT;
	}
}

static
void bcm2835_prepare_dma(struct bcm2835_host *host, struct mmc_data *data)
{
	int len, dir_data, dir_slave;
	struct dma_async_tx_descriptor *desc = NULL;
	struct dma_chan *dma_chan;

	dma_chan = host->dma_chan_rxtx;
	if (data->flags & MMC_DATA_READ) {
		dir_data = DMA_FROM_DEVICE;
		dir_slave = DMA_DEV_TO_MEM;
	} else {
		dir_data = DMA_TO_DEVICE;
		dir_slave = DMA_MEM_TO_DEV;
	}

	/* The block doesn't manage the FIFO DREQs properly for
	 * multi-block transfers, so don't attempt to DMA the final
	 * few words.  Unfortunately this requires the final sg entry
	 * to be trimmed.  N.B. This code demands that the overspill
	 * is contained in a single sg entry.
	 */

	host->drain_words = 0;
	if ((data->blocks > 1) && (dir_data == DMA_FROM_DEVICE)) {
		struct scatterlist *sg;
		u32 len;
		int i;

		len = min((u32)(FIFO_READ_THRESHOLD - 1) * 4,
			  (u32)data->blocks * data->blksz);

		for_each_sg(data->sg, sg, data->sg_len, i) {
			if (sg_is_last(sg)) {
				WARN_ON(sg->length < len);
				sg->length -= len;
				host->drain_page = sg_page(sg);
				host->drain_offset = sg->offset + sg->length;
			}
		}
		host->drain_words = len / 4;
	}

	/* The parameters have already been validated, so this will not fail */
	(void)dmaengine_slave_config(dma_chan,
				     (dir_data == DMA_FROM_DEVICE) ?
				     &host->dma_cfg_rx :
				     &host->dma_cfg_tx);

	len = dma_map_sg(dma_chan->device->dev, data->sg, data->sg_len,
			 dir_data);

	if (len > 0) {
		desc = dmaengine_prep_slave_sg(dma_chan, data->sg,
					       len, dir_slave,
					       DMA_PREP_INTERRUPT |
					       DMA_CTRL_ACK);
	}

	if (desc) {
		desc->callback = bcm2835_dma_complete;
		desc->callback_param = host;
		host->dma_desc = desc;
		host->dma_chan = dma_chan;
		host->dma_dir = dir_data;
	}
}

static void bcm2835_start_dma(struct bcm2835_host *host)
{
	dmaengine_submit(host->dma_desc);
	dma_async_issue_pending(host->dma_chan);
}

static void bcm2835_set_transfer_irqs(struct bcm2835_host *host)
{
	u32 all_irqs = SDHCFG_DATA_IRPT_EN | SDHCFG_BLOCK_IRPT_EN |
		SDHCFG_BUSY_IRPT_EN;

	if (host->dma_desc) {
		host->hcfg = (host->hcfg & ~all_irqs) |
			SDHCFG_BUSY_IRPT_EN;
	} else {
		host->hcfg = (host->hcfg & ~all_irqs) |
			SDHCFG_DATA_IRPT_EN |
			SDHCFG_BUSY_IRPT_EN;
	}

	writel(host->hcfg, host->ioaddr + SDHCFG);
}

static
void bcm2835_prepare_data(struct bcm2835_host *host, struct mmc_command *cmd)
{
	struct mmc_data *data = cmd->data;

	WARN_ON(host->data);

	host->data = data;
	if (!data)
		return;

	host->data_complete = false;
	host->data->bytes_xfered = 0;

	if (!host->dma_desc) {
		/* Use PIO */
		int flags = SG_MITER_ATOMIC;

		if (data->flags & MMC_DATA_READ)
			flags |= SG_MITER_TO_SG;
		else
			flags |= SG_MITER_FROM_SG;
		sg_miter_start(&host->sg_miter, data->sg, data->sg_len, flags);
		host->blocks = data->blocks;
	}

	bcm2835_set_transfer_irqs(host);

	writel(data->blksz, host->ioaddr + SDHBCT);
	writel(data->blocks, host->ioaddr + SDHBLC);
}

static u32 bcm2835_read_wait_sdcmd(struct bcm2835_host *host, u32 max_ms)
{
	struct device *dev = &host->pdev->dev;
	u32 value;
	int ret;

	ret = readl_poll_timeout(host->ioaddr + SDCMD, value,
				 !(value & SDCMD_NEW_FLAG), 1, 10);
	if (ret == -ETIMEDOUT)
		/* if it takes a while make poll interval bigger */
		ret = readl_poll_timeout(host->ioaddr + SDCMD, value,
					 !(value & SDCMD_NEW_FLAG),
					 10, max_ms * 1000);
	if (ret == -ETIMEDOUT)
		dev_err(dev, "%s: timeout (%d ms)\n", __func__, max_ms);

	return value;
}

static void bcm2835_finish_request(struct bcm2835_host *host)
{
	struct dma_chan *terminate_chan = NULL;
	struct mmc_request *mrq;

	cancel_delayed_work(&host->timeout_work);

	mrq = host->mrq;

	host->mrq = NULL;
	host->cmd = NULL;
	host->data = NULL;

	host->dma_desc = NULL;
	terminate_chan = host->dma_chan;
	host->dma_chan = NULL;

	if (terminate_chan) {
		int err = dmaengine_terminate_all(terminate_chan);

		if (err)
			dev_err(&host->pdev->dev,
				"failed to terminate DMA (%d)\n", err);
	}

	mmc_request_done(host->mmc, mrq);
}

static
bool bcm2835_send_command(struct bcm2835_host *host, struct mmc_command *cmd)
{
	struct device *dev = &host->pdev->dev;
	u32 sdcmd, sdhsts;
	unsigned long timeout;

	WARN_ON(host->cmd);

	sdcmd = bcm2835_read_wait_sdcmd(host, 100);
	if (sdcmd & SDCMD_NEW_FLAG) {
		dev_err(dev, "previous command never completed.\n");
		bcm2835_dumpregs(host);
		cmd->error = -EILSEQ;
		bcm2835_finish_request(host);
		return false;
	}

	if (!cmd->data && cmd->busy_timeout > 9000)
		timeout = DIV_ROUND_UP(cmd->busy_timeout, 1000) * HZ + HZ;
	else
		timeout = 10 * HZ;
	schedule_delayed_work(&host->timeout_work, timeout);

	host->cmd = cmd;

	/* Clear any error flags */
	sdhsts = readl(host->ioaddr + SDHSTS);
	if (sdhsts & SDHSTS_ERROR_MASK)
		writel(sdhsts, host->ioaddr + SDHSTS);

	if ((cmd->flags & MMC_RSP_136) && (cmd->flags & MMC_RSP_BUSY)) {
		dev_err(dev, "unsupported response type!\n");
		cmd->error = -EINVAL;
		bcm2835_finish_request(host);
		return false;
	}

	bcm2835_prepare_data(host, cmd);

	writel(cmd->arg, host->ioaddr + SDARG);

	sdcmd = cmd->opcode & SDCMD_CMD_MASK;

	host->use_busy = false;
	if (!(cmd->flags & MMC_RSP_PRESENT)) {
		sdcmd |= SDCMD_NO_RESPONSE;
	} else {
		if (cmd->flags & MMC_RSP_136)
			sdcmd |= SDCMD_LONG_RESPONSE;
		if (cmd->flags & MMC_RSP_BUSY) {
			sdcmd |= SDCMD_BUSYWAIT;
			host->use_busy = true;
		}
	}

	if (cmd->data) {
		if (cmd->data->flags & MMC_DATA_WRITE)
			sdcmd |= SDCMD_WRITE_CMD;
		if (cmd->data->flags & MMC_DATA_READ)
			sdcmd |= SDCMD_READ_CMD;
	}

	writel(sdcmd | SDCMD_NEW_FLAG, host->ioaddr + SDCMD);

	return true;
}

static void bcm2835_transfer_complete(struct bcm2835_host *host)
{
	struct mmc_data *data;

	WARN_ON(!host->data_complete);

	data = host->data;
	host->data = NULL;

	/* Need to send CMD12 if -
	 * a) open-ended multiblock transfer (no CMD23)
	 * b) error in multiblock transfer
	 */
	if (host->mrq->stop && (data->error || !host->use_sbc)) {
		if (bcm2835_send_command(host, host->mrq->stop)) {
			/* No busy, so poll for completion */
			if (!host->use_busy)
				bcm2835_finish_command(host);
		}
	} else {
		bcm2835_wait_transfer_complete(host);
		bcm2835_finish_request(host);
	}
}

static void bcm2835_finish_data(struct bcm2835_host *host)
{
	struct device *dev = &host->pdev->dev;
	struct mmc_data *data;

	data = host->data;

	host->hcfg &= ~(SDHCFG_DATA_IRPT_EN | SDHCFG_BLOCK_IRPT_EN);
	writel(host->hcfg, host->ioaddr + SDHCFG);

	data->bytes_xfered = data->error ? 0 : (data->blksz * data->blocks);

	host->data_complete = true;

	if (host->cmd) {
		/* Data managed to finish before the
		 * command completed. Make sure we do
		 * things in the proper order.
		 */
		dev_dbg(dev, "Finished early - HSTS %08x\n",
			readl(host->ioaddr + SDHSTS));
	} else {
		bcm2835_transfer_complete(host);
	}
}

static void bcm2835_finish_command(struct bcm2835_host *host)
{
	struct device *dev = &host->pdev->dev;
	struct mmc_command *cmd = host->cmd;
	u32 sdcmd;

	sdcmd = bcm2835_read_wait_sdcmd(host, 100);

	/* Check for errors */
	if (sdcmd & SDCMD_NEW_FLAG) {
		dev_err(dev, "command never completed.\n");
		bcm2835_dumpregs(host);
		host->cmd->error = -EIO;
		bcm2835_finish_request(host);
		return;
	} else if (sdcmd & SDCMD_FAIL_FLAG) {
		u32 sdhsts = readl(host->ioaddr + SDHSTS);

		/* Clear the errors */
		writel(SDHSTS_ERROR_MASK, host->ioaddr + SDHSTS);

		if (!(sdhsts & SDHSTS_CRC7_ERROR) ||
		    (host->cmd->opcode != MMC_SEND_OP_COND)) {
			if (sdhsts & SDHSTS_CMD_TIME_OUT) {
				host->cmd->error = -ETIMEDOUT;
			} else {
				dev_err(dev, "unexpected command %d error\n",
					host->cmd->opcode);
				bcm2835_dumpregs(host);
				host->cmd->error = -EILSEQ;
			}
			bcm2835_finish_request(host);
			return;
		}
	}

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			int i;

			for (i = 0; i < 4; i++) {
				cmd->resp[3 - i] =
					readl(host->ioaddr + SDRSP0 + i * 4);
			}
		} else {
			cmd->resp[0] = readl(host->ioaddr + SDRSP0);
		}
	}

	if (cmd == host->mrq->sbc) {
		/* Finished CMD23, now send actual command. */
		host->cmd = NULL;
		if (bcm2835_send_command(host, host->mrq->cmd)) {
			if (host->data && host->dma_desc)
				/* DMA transfer starts now, PIO starts
				 * after irq
				 */
				bcm2835_start_dma(host);

			if (!host->use_busy)
				bcm2835_finish_command(host);
		}
	} else if (cmd == host->mrq->stop) {
		/* Finished CMD12 */
		bcm2835_finish_request(host);
	} else {
		/* Processed actual command. */
		host->cmd = NULL;
		if (!host->data)
			bcm2835_finish_request(host);
		else if (host->data_complete)
			bcm2835_transfer_complete(host);
	}
}

static void bcm2835_timeout(struct work_struct *work)
{
	struct delayed_work *d = to_delayed_work(work);
	struct bcm2835_host *host =
		container_of(d, struct bcm2835_host, timeout_work);
	struct device *dev = &host->pdev->dev;

	mutex_lock(&host->mutex);

	if (host->mrq) {
		dev_err(dev, "timeout waiting for hardware interrupt.\n");
		bcm2835_dumpregs(host);

		if (host->data) {
			host->data->error = -ETIMEDOUT;
			bcm2835_finish_data(host);
		} else {
			if (host->cmd)
				host->cmd->error = -ETIMEDOUT;
			else
				host->mrq->cmd->error = -ETIMEDOUT;

			bcm2835_finish_request(host);
		}
	}

	mutex_unlock(&host->mutex);
}

static bool bcm2835_check_cmd_error(struct bcm2835_host *host, u32 intmask)
{
	struct device *dev = &host->pdev->dev;

	if (!(intmask & SDHSTS_ERROR_MASK))
		return false;

	if (!host->cmd)
		return true;

	dev_err(dev, "sdhost_busy_irq: intmask %08x\n", intmask);
	if (intmask & SDHSTS_CRC7_ERROR) {
		host->cmd->error = -EILSEQ;
	} else if (intmask & (SDHSTS_CRC16_ERROR |
			      SDHSTS_FIFO_ERROR)) {
		if (host->mrq->data)
			host->mrq->data->error = -EILSEQ;
		else
			host->cmd->error = -EILSEQ;
	} else if (intmask & SDHSTS_REW_TIME_OUT) {
		if (host->mrq->data)
			host->mrq->data->error = -ETIMEDOUT;
		else
			host->cmd->error = -ETIMEDOUT;
	} else if (intmask & SDHSTS_CMD_TIME_OUT) {
		host->cmd->error = -ETIMEDOUT;
	}
	bcm2835_dumpregs(host);
	return true;
}

static void bcm2835_check_data_error(struct bcm2835_host *host, u32 intmask)
{
	if (!host->data)
		return;
	if (intmask & (SDHSTS_CRC16_ERROR | SDHSTS_FIFO_ERROR))
		host->data->error = -EILSEQ;
	if (intmask & SDHSTS_REW_TIME_OUT)
		host->data->error = -ETIMEDOUT;
}

static void bcm2835_busy_irq(struct bcm2835_host *host)
{
	if (WARN_ON(!host->cmd)) {
		bcm2835_dumpregs(host);
		return;
	}

	if (WARN_ON(!host->use_busy)) {
		bcm2835_dumpregs(host);
		return;
	}
	host->use_busy = false;

	bcm2835_finish_command(host);
}

static void bcm2835_data_irq(struct bcm2835_host *host, u32 intmask)
{
	/* There are no dedicated data/space available interrupt
	 * status bits, so it is necessary to use the single shared
	 * data/space available FIFO status bits. It is therefore not
	 * an error to get here when there is no data transfer in
	 * progress.
	 */
	if (!host->data)
		return;

	bcm2835_check_data_error(host, intmask);
	if (host->data->error)
		goto finished;

	if (host->data->flags & MMC_DATA_WRITE) {
		/* Use the block interrupt for writes after the first block */
		host->hcfg &= ~(SDHCFG_DATA_IRPT_EN);
		host->hcfg |= SDHCFG_BLOCK_IRPT_EN;
		writel(host->hcfg, host->ioaddr + SDHCFG);
		bcm2835_transfer_pio(host);
	} else {
		bcm2835_transfer_pio(host);
		host->blocks--;
		if ((host->blocks == 0) || host->data->error)
			goto finished;
	}
	return;

finished:
	host->hcfg &= ~(SDHCFG_DATA_IRPT_EN | SDHCFG_BLOCK_IRPT_EN);
	writel(host->hcfg, host->ioaddr + SDHCFG);
}

static void bcm2835_data_threaded_irq(struct bcm2835_host *host)
{
	if (!host->data)
		return;
	if ((host->blocks == 0) || host->data->error)
		bcm2835_finish_data(host);
}

static void bcm2835_block_irq(struct bcm2835_host *host)
{
	if (WARN_ON(!host->data)) {
		bcm2835_dumpregs(host);
		return;
	}

	if (!host->dma_desc) {
		WARN_ON(!host->blocks);
		if (host->data->error || (--host->blocks == 0))
			bcm2835_finish_data(host);
		else
			bcm2835_transfer_pio(host);
	} else if (host->data->flags & MMC_DATA_WRITE) {
		bcm2835_finish_data(host);
	}
}

static irqreturn_t bcm2835_irq(int irq, void *dev_id)
{
	irqreturn_t result = IRQ_NONE;
	struct bcm2835_host *host = dev_id;
	u32 intmask;

	spin_lock(&host->lock);

	intmask = readl(host->ioaddr + SDHSTS);

	writel(SDHSTS_BUSY_IRPT |
	       SDHSTS_BLOCK_IRPT |
	       SDHSTS_SDIO_IRPT |
	       SDHSTS_DATA_FLAG,
	       host->ioaddr + SDHSTS);

	if (intmask & SDHSTS_BLOCK_IRPT) {
		bcm2835_check_data_error(host, intmask);
		host->irq_block = true;
		result = IRQ_WAKE_THREAD;
	}

	if (intmask & SDHSTS_BUSY_IRPT) {
		if (!bcm2835_check_cmd_error(host, intmask)) {
			host->irq_busy = true;
			result = IRQ_WAKE_THREAD;
		} else {
			result = IRQ_HANDLED;
		}
	}

	/* There is no true data interrupt status bit, so it is
	 * necessary to qualify the data flag with the interrupt
	 * enable bit.
	 */
	if ((intmask & SDHSTS_DATA_FLAG) &&
	    (host->hcfg & SDHCFG_DATA_IRPT_EN)) {
		bcm2835_data_irq(host, intmask);
		host->irq_data = true;
		result = IRQ_WAKE_THREAD;
	}

	spin_unlock(&host->lock);

	return result;
}

static irqreturn_t bcm2835_threaded_irq(int irq, void *dev_id)
{
	struct bcm2835_host *host = dev_id;
	unsigned long flags;
	bool block, busy, data;

	spin_lock_irqsave(&host->lock, flags);

	block = host->irq_block;
	busy  = host->irq_busy;
	data  = host->irq_data;
	host->irq_block = false;
	host->irq_busy  = false;
	host->irq_data  = false;

	spin_unlock_irqrestore(&host->lock, flags);

	mutex_lock(&host->mutex);

	if (block)
		bcm2835_block_irq(host);
	if (busy)
		bcm2835_busy_irq(host);
	if (data)
		bcm2835_data_threaded_irq(host);

	mutex_unlock(&host->mutex);

	return IRQ_HANDLED;
}

static void bcm2835_dma_complete_work(struct work_struct *work)
{
	struct bcm2835_host *host =
		container_of(work, struct bcm2835_host, dma_work);
	struct mmc_data *data = host->data;

	mutex_lock(&host->mutex);

	if (host->dma_chan) {
		dma_unmap_sg(host->dma_chan->device->dev,
			     data->sg, data->sg_len,
			     host->dma_dir);

		host->dma_chan = NULL;
	}

	if (host->drain_words) {
		unsigned long flags;
		void *page;
		u32 *buf;

		if (host->drain_offset & PAGE_MASK) {
			host->drain_page += host->drain_offset >> PAGE_SHIFT;
			host->drain_offset &= ~PAGE_MASK;
		}
		local_irq_save(flags);
		page = kmap_atomic(host->drain_page);
		buf = page + host->drain_offset;

		while (host->drain_words) {
			u32 edm = readl(host->ioaddr + SDEDM);

			if ((edm >> 4) & 0x1f)
				*(buf++) = readl(host->ioaddr + SDDATA);
			host->drain_words--;
		}

		kunmap_atomic(page);
		local_irq_restore(flags);
	}

	bcm2835_finish_data(host);

	mutex_unlock(&host->mutex);
}

static void bcm2835_set_clock(struct bcm2835_host *host, unsigned int clock)
{
	int div;

	/* The SDCDIV register has 11 bits, and holds (div - 2).  But
	 * in data mode the max is 50MHz wihout a minimum, and only
	 * the bottom 3 bits are used. Since the switch over is
	 * automatic (unless we have marked the card as slow...),
	 * chosen values have to make sense in both modes.  Ident mode
	 * must be 100-400KHz, so can range check the requested
	 * clock. CMD15 must be used to return to data mode, so this
	 * can be monitored.
	 *
	 * clock 250MHz -> 0->125MHz, 1->83.3MHz, 2->62.5MHz, 3->50.0MHz
	 *                 4->41.7MHz, 5->35.7MHz, 6->31.3MHz, 7->27.8MHz
	 *
	 *		 623->400KHz/27.8MHz
	 *		 reset value (507)->491159/50MHz
	 *
	 * BUT, the 3-bit clock divisor in data mode is too small if
	 * the core clock is higher than 250MHz, so instead use the
	 * SLOW_CARD configuration bit to force the use of the ident
	 * clock divisor at all times.
	 */

	if (clock < 100000) {
		/* Can't stop the clock, but make it as slow as possible
		 * to show willing
		 */
		host->cdiv = SDCDIV_MAX_CDIV;
		writel(host->cdiv, host->ioaddr + SDCDIV);
		return;
	}

	div = host->max_clk / clock;
	if (div < 2)
		div = 2;
	if ((host->max_clk / div) > clock)
		div++;
	div -= 2;

	if (div > SDCDIV_MAX_CDIV)
		div = SDCDIV_MAX_CDIV;

	clock = host->max_clk / (div + 2);
	host->mmc->actual_clock = clock;

	/* Calibrate some delays */

	host->ns_per_fifo_word = (1000000000 / clock) *
		((host->mmc->caps & MMC_CAP_4_BIT_DATA) ? 8 : 32);

	host->cdiv = div;
	writel(host->cdiv, host->ioaddr + SDCDIV);

	/* Set the timeout to 500ms */
	writel(host->mmc->actual_clock / 2, host->ioaddr + SDTOUT);
}

static void bcm2835_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct bcm2835_host *host = mmc_priv(mmc);
	struct device *dev = &host->pdev->dev;
	u32 edm, fsm;

	/* Reset the error statuses in case this is a retry */
	if (mrq->sbc)
		mrq->sbc->error = 0;
	if (mrq->cmd)
		mrq->cmd->error = 0;
	if (mrq->data)
		mrq->data->error = 0;
	if (mrq->stop)
		mrq->stop->error = 0;

	if (mrq->data && !is_power_of_2(mrq->data->blksz)) {
		dev_err(dev, "unsupported block size (%d bytes)\n",
			mrq->data->blksz);
		mrq->cmd->error = -EINVAL;
		mmc_request_done(mmc, mrq);
		return;
	}

	if (host->use_dma && mrq->data && (mrq->data->blocks > PIO_THRESHOLD))
		bcm2835_prepare_dma(host, mrq->data);

	mutex_lock(&host->mutex);

	WARN_ON(host->mrq);
	host->mrq = mrq;

	edm = readl(host->ioaddr + SDEDM);
	fsm = edm & SDEDM_FSM_MASK;

	if ((fsm != SDEDM_FSM_IDENTMODE) &&
	    (fsm != SDEDM_FSM_DATAMODE)) {
		dev_err(dev, "previous command (%d) not complete (EDM %08x)\n",
			readl(host->ioaddr + SDCMD) & SDCMD_CMD_MASK,
			edm);
		bcm2835_dumpregs(host);
		mrq->cmd->error = -EILSEQ;
		bcm2835_finish_request(host);
		mutex_unlock(&host->mutex);
		return;
	}

	host->use_sbc = !!mrq->sbc && (host->mrq->data->flags & MMC_DATA_READ);
	if (host->use_sbc) {
		if (bcm2835_send_command(host, mrq->sbc)) {
			if (!host->use_busy)
				bcm2835_finish_command(host);
		}
	} else if (bcm2835_send_command(host, mrq->cmd)) {
		if (host->data && host->dma_desc) {
			/* DMA transfer starts now, PIO starts after irq */
			bcm2835_start_dma(host);
		}

		if (!host->use_busy)
			bcm2835_finish_command(host);
	}

	mutex_unlock(&host->mutex);
}

static void bcm2835_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct bcm2835_host *host = mmc_priv(mmc);

	mutex_lock(&host->mutex);

	if (!ios->clock || ios->clock != host->clock) {
		bcm2835_set_clock(host, ios->clock);
		host->clock = ios->clock;
	}

	/* set bus width */
	host->hcfg &= ~SDHCFG_WIDE_EXT_BUS;
	if (ios->bus_width == MMC_BUS_WIDTH_4)
		host->hcfg |= SDHCFG_WIDE_EXT_BUS;

	host->hcfg |= SDHCFG_WIDE_INT_BUS;

	/* Disable clever clock switching, to cope with fast core clocks */
	host->hcfg |= SDHCFG_SLOW_CARD;

	writel(host->hcfg, host->ioaddr + SDHCFG);

	mutex_unlock(&host->mutex);
}

static struct mmc_host_ops bcm2835_ops = {
	.request = bcm2835_request,
	.set_ios = bcm2835_set_ios,
	.hw_reset = bcm2835_reset,
};

static int bcm2835_add_host(struct bcm2835_host *host)
{
	struct mmc_host *mmc = host->mmc;
	struct device *dev = &host->pdev->dev;
	char pio_limit_string[20];
	int ret;

	mmc->f_max = host->max_clk;
	mmc->f_min = host->max_clk / SDCDIV_MAX_CDIV;

	mmc->max_busy_timeout = ~0 / (mmc->f_max / 1000);

	dev_dbg(dev, "f_max %d, f_min %d, max_busy_timeout %d\n",
		mmc->f_max, mmc->f_min, mmc->max_busy_timeout);

	/* host controller capabilities */
	mmc->caps |= MMC_CAP_SD_HIGHSPEED | MMC_CAP_MMC_HIGHSPEED |
		     MMC_CAP_NEEDS_POLL | MMC_CAP_HW_RESET | MMC_CAP_ERASE |
		     MMC_CAP_CMD23;

	spin_lock_init(&host->lock);
	mutex_init(&host->mutex);

	if (IS_ERR_OR_NULL(host->dma_chan_rxtx)) {
		dev_warn(dev, "unable to initialise DMA channel. Falling back to PIO\n");
		host->use_dma = false;
	} else {
		host->use_dma = true;

		host->dma_cfg_tx.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		host->dma_cfg_tx.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		host->dma_cfg_tx.slave_id = 13;		/* DREQ channel */
		host->dma_cfg_tx.direction = DMA_MEM_TO_DEV;
		host->dma_cfg_tx.src_addr = 0;
		host->dma_cfg_tx.dst_addr = host->phys_addr + SDDATA;

		host->dma_cfg_rx.src_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		host->dma_cfg_rx.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
		host->dma_cfg_rx.slave_id = 13;		/* DREQ channel */
		host->dma_cfg_rx.direction = DMA_DEV_TO_MEM;
		host->dma_cfg_rx.src_addr = host->phys_addr + SDDATA;
		host->dma_cfg_rx.dst_addr = 0;

		if (dmaengine_slave_config(host->dma_chan_rxtx,
					   &host->dma_cfg_tx) != 0 ||
		    dmaengine_slave_config(host->dma_chan_rxtx,
					   &host->dma_cfg_rx) != 0)
			host->use_dma = false;
	}

	mmc->max_segs = 128;
	mmc->max_req_size = 524288;
	mmc->max_seg_size = mmc->max_req_size;
	mmc->max_blk_size = 1024;
	mmc->max_blk_count =  65535;

	/* report supported voltage ranges */
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	INIT_WORK(&host->dma_work, bcm2835_dma_complete_work);
	INIT_DELAYED_WORK(&host->timeout_work, bcm2835_timeout);

	/* Set interrupt enables */
	host->hcfg = SDHCFG_BUSY_IRPT_EN;

	bcm2835_reset_internal(host);

	ret = request_threaded_irq(host->irq, bcm2835_irq,
				   bcm2835_threaded_irq,
				   0, mmc_hostname(mmc), host);
	if (ret) {
		dev_err(dev, "failed to request IRQ %d: %d\n", host->irq, ret);
		return ret;
	}

	ret = mmc_add_host(mmc);
	if (ret) {
		free_irq(host->irq, host);
		return ret;
	}

	pio_limit_string[0] = '\0';
	if (host->use_dma && (PIO_THRESHOLD > 0))
		sprintf(pio_limit_string, " (>%d)", PIO_THRESHOLD);
	dev_info(dev, "loaded - DMA %s%s\n",
		 host->use_dma ? "enabled" : "disabled", pio_limit_string);

	return 0;
}

static int bcm2835_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct clk *clk;
	struct resource *iomem;
	struct bcm2835_host *host;
	struct mmc_host *mmc;
	const __be32 *regaddr_p;
	int ret;

	dev_dbg(dev, "%s\n", __func__);
	mmc = mmc_alloc_host(sizeof(*host), dev);
	if (!mmc)
		return -ENOMEM;

	mmc->ops = &bcm2835_ops;
	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->pdev = pdev;
	spin_lock_init(&host->lock);

	iomem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	host->ioaddr = devm_ioremap_resource(dev, iomem);
	if (IS_ERR(host->ioaddr)) {
		ret = PTR_ERR(host->ioaddr);
		goto err;
	}

	/* Parse OF address directly to get the physical address for
	 * DMA to our registers.
	 */
	regaddr_p = of_get_address(pdev->dev.of_node, 0, NULL, NULL);
	if (!regaddr_p) {
		dev_err(dev, "Can't get phys address\n");
		ret = -EINVAL;
		goto err;
	}

	host->phys_addr = be32_to_cpup(regaddr_p);

	host->dma_chan = NULL;
	host->dma_desc = NULL;

	host->dma_chan_rxtx = dma_request_slave_channel(dev, "rx-tx");

	clk = devm_clk_get(dev, NULL);
	if (IS_ERR(clk)) {
		ret = PTR_ERR(clk);
		if (ret != -EPROBE_DEFER)
			dev_err(dev, "could not get clk: %d\n", ret);
		goto err;
	}

	host->max_clk = clk_get_rate(clk);

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq <= 0) {
		dev_err(dev, "get IRQ failed\n");
		ret = -EINVAL;
		goto err;
	}

	ret = mmc_of_parse(mmc);
	if (ret)
		goto err;

	ret = bcm2835_add_host(host);
	if (ret)
		goto err;

	platform_set_drvdata(pdev, host);

	dev_dbg(dev, "%s -> OK\n", __func__);

	return 0;

err:
	dev_dbg(dev, "%s -> err %d\n", __func__, ret);
	mmc_free_host(mmc);

	return ret;
}

static int bcm2835_remove(struct platform_device *pdev)
{
	struct bcm2835_host *host = platform_get_drvdata(pdev);

	mmc_remove_host(host->mmc);

	writel(SDVDD_POWER_OFF, host->ioaddr + SDVDD);

	free_irq(host->irq, host);

	cancel_work_sync(&host->dma_work);
	cancel_delayed_work_sync(&host->timeout_work);

	mmc_free_host(host->mmc);
	platform_set_drvdata(pdev, NULL);

	return 0;
}

static const struct of_device_id bcm2835_match[] = {
	{ .compatible = "brcm,bcm2835-sdhost" },
	{ }
};
MODULE_DEVICE_TABLE(of, bcm2835_match);

static struct platform_driver bcm2835_driver = {
	.probe      = bcm2835_probe,
	.remove     = bcm2835_remove,
	.driver     = {
		.name		= "sdhost-bcm2835",
		.of_match_table	= bcm2835_match,
	},
};
module_platform_driver(bcm2835_driver);

MODULE_ALIAS("platform:sdhost-bcm2835");
MODULE_DESCRIPTION("BCM2835 SDHost driver");
MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Phil Elwell");
