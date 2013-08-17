/*
 * Synopsys DesignWare Multimedia Card Interface driver
 *  (Based on NXP driver for lpc 31xx)
 *
 * Copyright (C) 2009 NXP Semiconductors
 * Copyright (C) 2009, 2010 Imagination Technologies Ltd.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/blkdev.h>
#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/device.h>
#include <linux/dma-mapping.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/ratelimit.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/card.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/dw_mmc.h>
#include <linux/mmc/mmc_trace.h>
#include <linux/bitops.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

#include "dw_mmc.h"
#include "../card/queue.h"

/* Common flag combinations */
#define DW_MCI_DATA_ERROR_FLAGS	(SDMMC_INT_DTO | SDMMC_INT_DCRC | \
				 SDMMC_INT_HTO | SDMMC_INT_SBE  | \
				 SDMMC_INT_EBE)
#define DW_MCI_CMD_ERROR_FLAGS	(SDMMC_INT_RTO | SDMMC_INT_RCRC | \
				 SDMMC_INT_RESP_ERR)
#define DW_MCI_ERROR_FLAGS	(DW_MCI_DATA_ERROR_FLAGS | \
				 DW_MCI_CMD_ERROR_FLAGS  | SDMMC_INT_HLE)
#define DW_MCI_SEND_STATUS	1
#define DW_MCI_RECV_STATUS	2
#define DW_MCI_DMA_THRESHOLD	4

#ifdef CONFIG_MMC_DW_IDMAC
struct idmac_desc {
	u32		des0;	/* Control Descriptor */
#define IDMAC_DES0_DIC	BIT(1)
#define IDMAC_DES0_LD	BIT(2)
#define IDMAC_DES0_FD	BIT(3)
#define IDMAC_DES0_CH	BIT(4)
#define IDMAC_DES0_ER	BIT(5)
#define IDMAC_DES0_CES	BIT(30)
#define IDMAC_DES0_OWN	BIT(31)

	u32		des1;	/* Buffer sizes */
#define IDMAC_SET_BUFFER1_SIZE(d, s) \
	((d)->des1 = ((d)->des1 & 0x03ffe000) | ((s) & 0x1fff))

	u32		des2;	/* buffer 1 physical address */

	u32		des3;	/* buffer 2 physical address */
};
#endif /* CONFIG_MMC_DW_IDMAC */

#define MAX_TUNING_LOOP	40
static const u8 tuning_blk_pattern_4bit[] = {
	0xff, 0x0f, 0xff, 0x00, 0xff, 0xcc, 0xc3, 0xcc,
	0xc3, 0x3c, 0xcc, 0xff, 0xfe, 0xff, 0xfe, 0xef,
	0xff, 0xdf, 0xff, 0xdd, 0xff, 0xfb, 0xff, 0xfb,
	0xbf, 0xff, 0x7f, 0xff, 0x77, 0xf7, 0xbd, 0xef,
	0xff, 0xf0, 0xff, 0xf0, 0x0f, 0xfc, 0xcc, 0x3c,
	0xcc, 0x33, 0xcc, 0xcf, 0xff, 0xef, 0xff, 0xee,
	0xff, 0xfd, 0xff, 0xfd, 0xdf, 0xff, 0xbf, 0xff,
	0xbb, 0xff, 0xf7, 0xff, 0xf7, 0x7f, 0x7b, 0xde,
};

static const u8 tuning_blk_pattern_8bit[] = {
	0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00, 0x00,
	0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc, 0xcc,
	0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff, 0xff,
	0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee, 0xff,
	0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd, 0xdd,
	0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff, 0xbb,
	0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff, 0xff,
	0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee, 0xff,
	0xff, 0xff, 0xff, 0x00, 0xff, 0xff, 0xff, 0x00,
	0x00, 0xff, 0xff, 0xcc, 0xcc, 0xcc, 0x33, 0xcc,
	0xcc, 0xcc, 0x33, 0x33, 0xcc, 0xcc, 0xcc, 0xff,
	0xff, 0xff, 0xee, 0xff, 0xff, 0xff, 0xee, 0xee,
	0xff, 0xff, 0xff, 0xdd, 0xff, 0xff, 0xff, 0xdd,
	0xdd, 0xff, 0xff, 0xff, 0xbb, 0xff, 0xff, 0xff,
	0xbb, 0xbb, 0xff, 0xff, 0xff, 0x77, 0xff, 0xff,
	0xff, 0x77, 0x77, 0xff, 0x77, 0xbb, 0xdd, 0xee,
};

#define INT_QOS	160000
#define QOS_DELAY	10

/**
 * struct dw_mci_slot - MMC slot state
 * @mmc: The mmc_host representing this slot.
 * @host: The MMC controller this slot is using.
 * @ctype: Card type for this slot.
 * @mrq: mmc_request currently being processed or waiting to be
 *	processed, or NULL when the slot is idle.
 * @queue_node: List node for placing this node in the @queue list of
 *	&struct dw_mci.
 * @clock: Clock rate configured by set_ios(). Protected by host->lock.
 * @flags: Random state bits associated with the slot.
 * @id: Number of this slot.
 * @last_detect_state: Most recently observed card detect state.
 */
struct dw_mci_slot {
	struct mmc_host		*mmc;
	struct dw_mci		*host;

	u32			ctype;

	struct mmc_request	*mrq;
	struct list_head	queue_node;

	unsigned int		clock;
	unsigned long		flags;
#define DW_MMC_CARD_PRESENT	0
#define DW_MMC_CARD_NEED_INIT	1
	int			id;
	int			last_detect_state;
};

static void dw_mci_ciu_clk_en(struct dw_mci *host)
{
	if (!atomic_read(&host->cclk_cnt)) {
		clk_enable(host->cclk);
		atomic_inc_return(&host->cclk_cnt);
	}
}

static void dw_mci_ciu_clk_dis(struct dw_mci *host)
{
	if (atomic_read(&host->cclk_cnt)) {
		clk_disable(host->cclk);
		atomic_dec_return(&host->cclk_cnt);
	}
}

static void dw_mci_biu_clk_en(struct dw_mci *host)
{
	if (!atomic_read(&host->hclk_cnt)) {
		clk_enable(host->hclk);
		atomic_inc_return(&host->hclk_cnt);
	}
}

static void dw_mci_biu_clk_dis(struct dw_mci *host)
{
	if (atomic_read(&host->hclk_cnt)) {
		clk_disable(host->hclk);
		atomic_dec_return(&host->hclk_cnt);
	}
}

static void dw_mci_qos_work(struct work_struct *work)
{
	struct dw_mci *host = container_of(work, struct dw_mci, qos_work.work);
	pm_qos_update_request(&host->pm_qos_int, 0);
}

static void dw_mci_qos_init(struct dw_mci *host)
{
	INIT_DELAYED_WORK(&host->qos_work, dw_mci_qos_work);
	pm_qos_add_request(&host->pm_qos_int, PM_QOS_DEVICE_THROUGHPUT, 0);
}

static void dw_mci_qos_exit(struct dw_mci *host)
{
	pm_qos_remove_request(&host->pm_qos_int);
}

static void dw_mci_qos_get(struct dw_mci *host)
{
	if (delayed_work_pending(&host->qos_work))
		cancel_delayed_work_sync(&host->qos_work);
	pm_qos_update_request(&host->pm_qos_int, INT_QOS);
}

static void dw_mci_qos_put(struct dw_mci *host)
{
	queue_delayed_work(system_nrt_wq, &host->qos_work,
			msecs_to_jiffies(QOS_DELAY));
}

#if defined(CONFIG_DEBUG_FS)
static int dw_mci_req_show(struct seq_file *s, void *v)
{
	struct dw_mci_slot *slot = s->private;
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_command *stop;
	struct mmc_data	*data;

	/* Make sure we get a consistent snapshot */
	spin_lock_bh(&slot->host->lock);
	mrq = slot->mrq;

	if (mrq) {
		cmd = mrq->cmd;
		data = mrq->data;
		stop = mrq->stop;

		if (cmd)
			seq_printf(s,
				   "CMD%u(0x%x) flg %x rsp %x %x %x %x err %d\n",
				   cmd->opcode, cmd->arg, cmd->flags,
				   cmd->resp[0], cmd->resp[1], cmd->resp[2],
				   cmd->resp[2], cmd->error);
		if (data)
			seq_printf(s, "DATA %u / %u * %u flg %x err %d\n",
				   data->bytes_xfered, data->blocks,
				   data->blksz, data->flags, data->error);
		if (stop)
			seq_printf(s,
				   "CMD%u(0x%x) flg %x rsp %x %x %x %x err %d\n",
				   stop->opcode, stop->arg, stop->flags,
				   stop->resp[0], stop->resp[1], stop->resp[2],
				   stop->resp[2], stop->error);
	}

	spin_unlock_bh(&slot->host->lock);

	return 0;
}

static int dw_mci_req_open(struct inode *inode, struct file *file)
{
	return single_open(file, dw_mci_req_show, inode->i_private);
}

static const struct file_operations dw_mci_req_fops = {
	.owner		= THIS_MODULE,
	.open		= dw_mci_req_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int dw_mci_regs_show(struct seq_file *s, void *v)
{
	seq_printf(s, "STATUS:\t0x%08x\n", SDMMC_STATUS);
	seq_printf(s, "RINTSTS:\t0x%08x\n", SDMMC_RINTSTS);
	seq_printf(s, "CMD:\t0x%08x\n", SDMMC_CMD);
	seq_printf(s, "CTRL:\t0x%08x\n", SDMMC_CTRL);
	seq_printf(s, "INTMASK:\t0x%08x\n", SDMMC_INTMASK);
	seq_printf(s, "CLKENA:\t0x%08x\n", SDMMC_CLKENA);

	return 0;
}

static int dw_mci_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, dw_mci_regs_show, inode->i_private);
}

static const struct file_operations dw_mci_regs_fops = {
	.owner		= THIS_MODULE,
	.open		= dw_mci_regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void dw_mci_init_debugfs(struct dw_mci_slot *slot)
{
	struct mmc_host	*mmc = slot->mmc;
	struct dw_mci *host = slot->host;
	struct dentry *root;
	struct dentry *node;

	root = mmc->debugfs_root;
	if (!root)
		return;

	node = debugfs_create_file("regs", S_IRUSR, root, host,
				   &dw_mci_regs_fops);
	if (!node)
		goto err;

	node = debugfs_create_file("req", S_IRUSR, root, slot,
				   &dw_mci_req_fops);
	if (!node)
		goto err;

	node = debugfs_create_u32("state", S_IRUSR, root, (u32 *)&host->state);
	if (!node)
		goto err;

	node = debugfs_create_x32("pending_events", S_IRUSR, root,
				  (u32 *)&host->pending_events);
	if (!node)
		goto err;

	node = debugfs_create_x32("completed_events", S_IRUSR, root,
				  (u32 *)&host->completed_events);
	if (!node)
		goto err;

	return;

err:
	dev_err(&mmc->class_dev, "failed to initialize debugfs for slot\n");
}
#endif /* defined(CONFIG_DEBUG_FS) */

void dw_mci_reg_dump(struct dw_mci *host)
{
	dev_err(&host->dev, ": ============== REGISTER DUMP ==============\n");
	dev_err(&host->dev, ": CTRL:	0x%08x\n", mci_readl(host, CTRL));
	dev_err(&host->dev, ": PWREN:	0x%08x\n", mci_readl(host, PWREN));
	dev_err(&host->dev, ": CLKDIV:	0x%08x\n", mci_readl(host, CLKDIV));
	dev_err(&host->dev, ": CLKSRC:	0x%08x\n", mci_readl(host, CLKSRC));
	dev_err(&host->dev, ": CLKENA:	0x%08x\n", mci_readl(host, CLKENA));
	dev_err(&host->dev, ": TMOUT:	0x%08x\n", mci_readl(host, TMOUT));
	dev_err(&host->dev, ": CTYPE:	0x%08x\n", mci_readl(host, CTYPE));
	dev_err(&host->dev, ": BLKSIZ:	0x%08x\n", mci_readl(host, BLKSIZ));
	dev_err(&host->dev, ": BYTCNT:	0x%08x\n", mci_readl(host, BYTCNT));
	dev_err(&host->dev, ": INTMSK:	0x%08x\n", mci_readl(host, INTMASK));
	dev_err(&host->dev, ": CMDARG:	0x%08x\n", mci_readl(host, CMDARG));
	dev_err(&host->dev, ": CMD:	0x%08x\n", mci_readl(host, CMD));
	dev_err(&host->dev, ": MINTSTS:	0x%08x\n", mci_readl(host, MINTSTS));
	dev_err(&host->dev, ": RINTSTS:	0x%08x\n", mci_readl(host, RINTSTS));
	dev_err(&host->dev, ": STATUS:	0x%08x\n", mci_readl(host, STATUS));
	dev_err(&host->dev, ": FIFOTH:	0x%08x\n", mci_readl(host, FIFOTH));
	dev_err(&host->dev, ": CDETECT:	0x%08x\n", mci_readl(host, CDETECT));
	dev_err(&host->dev, ": WRTPRT:	0x%08x\n", mci_readl(host, WRTPRT));
	dev_err(&host->dev, ": GPIO:	0x%08x\n", mci_readl(host, GPIO));
	dev_err(&host->dev, ": TCBCNT:	0x%08x\n", mci_readl(host, TCBCNT));
	dev_err(&host->dev, ": TBBCNT:	0x%08x\n", mci_readl(host, TBBCNT));
	dev_err(&host->dev, ": DEBNCE:	0x%08x\n", mci_readl(host, DEBNCE));
	dev_err(&host->dev, ": USRID:	0x%08x\n", mci_readl(host, USRID));
	dev_err(&host->dev, ": VERID:	0x%08x\n", mci_readl(host, VERID));
	dev_err(&host->dev, ": HCON:	0x%08x\n", mci_readl(host, HCON));
	dev_err(&host->dev, ": UHS_REG:	0x%08x\n", mci_readl(host, UHS_REG));
	dev_err(&host->dev, ": BMOD:	0x%08x\n", mci_readl(host, BMOD));
	dev_err(&host->dev, ": PLDMND:	0x%08x\n", mci_readl(host, PLDMND));
	dev_err(&host->dev, ": DBADDR:	0x%08x\n", mci_readl(host, DBADDR));
	dev_err(&host->dev, ": IDSTS:	0x%08x\n", mci_readl(host, IDSTS));
	dev_err(&host->dev, ": IDINTEN:	0x%08x\n", mci_readl(host, IDINTEN));
	dev_err(&host->dev, ": DSCADDR:	0x%08x\n", mci_readl(host, DSCADDR));
	dev_err(&host->dev, ": BUFADDR:	0x%08x\n", mci_readl(host, BUFADDR));
	dev_err(&host->dev, ": CLKSEL:	0x%08x\n", mci_readl(host, CLKSEL));
	dev_err(&host->dev, ": ============== STATUS DUMP ================\n");
	dev_err(&host->dev, ": cmd_status:      0x%08x\n", host->cmd_status);
	dev_err(&host->dev, ": data_status:     0x%08x\n", host->data_status);
	dev_err(&host->dev, ": pending_events:  0x%08lx\n", host->pending_events);
	dev_err(&host->dev, ": completed_events:0x%08lx\n", host->completed_events);
	dev_err(&host->dev, ": state:           %d\n", host->state);
	dev_err(&host->dev, ": cclk:            %s\n",
			      atomic_read(&host->cclk_cnt) ? "enable" : "disable");
	dev_err(&host->dev, ": ===========================================\n");
}

static void dw_mci_set_timeout(struct dw_mci *host)
{
	/* timeout (maximum) */
	mci_writel(host, TMOUT, 0xffffffff);
}

static inline bool dw_mci_stop_abort_cmd(struct mmc_command *cmd)
{
	u32 op = cmd->opcode;

	if ((op == MMC_STOP_TRANSMISSION) ||
	    (op == MMC_GO_IDLE_STATE) ||
	    (op == MMC_GO_INACTIVE_STATE) ||
	    ((op == SD_IO_RW_DIRECT) && (cmd->arg & 0x80000000) &&
	     ((cmd->arg >> 9) & 0x1FFFF) == SDIO_CCCR_ABORT))
		return true;
	return false;
}

static u32 dw_mci_prepare_command(struct mmc_host *mmc, struct mmc_command *cmd)
{
	struct mmc_data	*data;
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci *host = slot->host;

	u32 cmdr, argr;
	cmd->error = -EINPROGRESS;

	cmdr = cmd->opcode;
	argr = ((cmd->arg >> 9) & 0x1FFFF);

	if (!(host->quirks & DW_MMC_QUIRK_NO_VOLSW_INT) &&
				cmdr == SD_SWITCH_VOLTAGE)
		cmdr |= SDMMC_VOLT_SWITCH;

	if (cmdr == MMC_STOP_TRANSMISSION)
		cmdr |= SDMMC_CMD_STOP;
	else if (cmdr != MMC_SEND_STATUS)
		cmdr |= SDMMC_CMD_PRV_DAT_WAIT;

	if ((cmd->opcode == SD_IO_RW_DIRECT) &&
			(argr == SDIO_CCCR_ABORT)) {
		cmdr &= ~SDMMC_CMD_PRV_DAT_WAIT;
		cmdr |= SDMMC_CMD_STOP;
	}

	if (cmd->flags & MMC_RSP_PRESENT) {
		/* We expect a response, so set this bit */
		cmdr |= SDMMC_CMD_RESP_EXP;
		if (cmd->flags & MMC_RSP_136)
			cmdr |= SDMMC_CMD_RESP_LONG;
	}

	if (cmd->flags & MMC_RSP_CRC)
		cmdr |= SDMMC_CMD_RESP_CRC;

	data = cmd->data;
	if (data) {
		cmdr |= SDMMC_CMD_DAT_EXP;
		if (data->flags & MMC_DATA_STREAM)
			cmdr |= SDMMC_CMD_STRM_MODE;
		if (data->flags & MMC_DATA_WRITE)
			cmdr |= SDMMC_CMD_DAT_WR;
	}

	/* Use hold bit register */
	if (slot->host->pdata->set_io_timing)
		cmdr |= SDMMC_USE_HOLD_REG;

	return cmdr;
}

static u32 dw_mci_prep_stop(struct dw_mci *host, struct mmc_command *cmd)
{
	struct mmc_command *stop = &host->stop;
	u32 cmdr = cmd->opcode;

	memset(stop, 0, sizeof(struct mmc_command));

	if (cmdr == MMC_READ_SINGLE_BLOCK ||
			cmdr == MMC_READ_MULTIPLE_BLOCK ||
			cmdr == MMC_WRITE_BLOCK ||
			cmdr == MMC_WRITE_MULTIPLE_BLOCK) {
		stop->opcode = MMC_STOP_TRANSMISSION;
		stop->arg = 0;
		stop->flags = MMC_RSP_R1B | MMC_CMD_AC;
	} else if (cmdr == SD_IO_RW_EXTENDED) {
		stop->opcode = SD_IO_RW_DIRECT;
		stop->arg = 0x80000000;
		stop->arg |= (cmd->arg >> 28) & 0x7;
		stop->arg |= SDIO_CCCR_ABORT << 9;
		stop->flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_AC;
	} else
		return 0;

	cmdr = stop->opcode | SDMMC_CMD_STOP |
		SDMMC_CMD_RESP_CRC | SDMMC_CMD_RESP_EXP;

	/* Use hold bit register */
	if (host->pdata->set_io_timing)
		cmdr |= SDMMC_USE_HOLD_REG;

	return cmdr;
}

static void dw_mci_start_command(struct dw_mci *host,
				 struct mmc_command *cmd, u32 cmd_flags)
{
	struct mmc_data *data;
	u32 mask;

	host->cmd = cmd;
	data = cmd->data;
	mask = mci_readl(host, INTMASK);

	dev_vdbg(&host->dev,
		 "start command: ARGR=0x%08x CMDR=0x%08x\n",
		 cmd->arg, cmd_flags);

	if ((host->quirks & DW_MCI_QUIRK_NO_DETECT_EBIT) &&
			data && (data->flags & MMC_DATA_READ)) {
		mask &= ~SDMMC_INT_EBE;
	} else {
		mask |= SDMMC_INT_EBE;
		mci_writel(host, RINTSTS, SDMMC_INT_EBE);
	}

	mci_writel(host, INTMASK, mask);

	mci_writel(host, CMDARG, cmd->arg);
	wmb();

	mci_writel(host, CMD, cmd_flags | SDMMC_CMD_START);
}

static void send_stop_cmd(struct dw_mci *host, struct mmc_data *data)
{
	dw_mci_start_command(host, data->stop, host->stop_cmdr);
}

/* DMA interface functions */
static void dw_mci_stop_dma(struct dw_mci *host)
{
	if (host->using_dma) {
		host->dma_ops->stop(host);
		host->dma_ops->cleanup(host);
		host->dma_ops->reset(host);
	} else {
		/* Data transfer was stopped by the interrupt handler */
		set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
	}
}

static bool dw_mci_wait_reset(struct device *dev, struct dw_mci *host,
		unsigned int reset_val)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(500);
	unsigned int ctrl;

	ctrl = mci_readl(host, CTRL);
	ctrl |= reset_val;
	mci_writel(host, CTRL, ctrl);

	/* wait till resets clear */
	do {
		if (!(mci_readl(host, CTRL) & reset_val))
			return true;
	} while (time_before(jiffies, timeout));

	dev_err(dev, "Timeout resetting block (ctrl %#x)\n", ctrl);

	return false;
}

static void mci_send_cmd(struct dw_mci_slot *slot, u32 cmd, u32 arg)
{
	struct dw_mci *host = slot->host;
	unsigned long timeout = jiffies + msecs_to_jiffies(500);
	unsigned int cmd_status = 0;
	int try = 3;

	mci_writel(host, CMDARG, arg);
	wmb();
	mci_writel(host, CMD, SDMMC_CMD_START | cmd);

	do {
		while (time_before(jiffies, timeout)) {
			cmd_status = mci_readl(host, CMD);
			if (!(cmd_status & SDMMC_CMD_START))
				return;
		}

		dw_mci_wait_reset(&host->dev, host, SDMMC_CTRL_RESET);
		mci_writel(host, CMD, SDMMC_CMD_START | cmd);
		timeout = jiffies + msecs_to_jiffies(500);
	} while (--try);

	dev_err(&slot->mmc->class_dev,
		"Timeout sending command (cmd %#x arg %#x status %#x)\n",
		cmd, arg, cmd_status);
}

static void dw_mci_ciu_reset(struct device *dev, struct dw_mci *host)
{
	struct dw_mci_slot *slot = host->cur_slot;

	if (slot) {
		dw_mci_wait_reset(dev, host, SDMMC_CTRL_RESET);
		mci_send_cmd(slot, SDMMC_CMD_UPD_CLK |
			SDMMC_CMD_PRV_DAT_WAIT, 0);
	}
}

static bool dw_mci_fifo_reset(struct device *dev, struct dw_mci *host)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	unsigned int ctrl;
	bool result;

	do {
		result = dw_mci_wait_reset(&host->dev, host, SDMMC_CTRL_FIFO_RESET);

		if (!result)
			break;

		ctrl = mci_readl(host, STATUS);
		if (!(ctrl & SDMMC_STATUS_DMA_REQ)) {
			result = dw_mci_wait_reset(&host->dev, host,
					SDMMC_CTRL_FIFO_RESET);
			if (result) {
				/* clear exception raw interrupts can not be handled
				   ex) fifo full => RXDR interrupt rising */
				ctrl = mci_readl(host, RINTSTS);
				ctrl = ctrl & ~(mci_readl(host, MINTSTS));
				if (ctrl)
					mci_writel(host, RINTSTS, ctrl);

				return true;
			}
		}
	} while (time_before(jiffies, timeout));

	dev_err(dev, "%s: Timeout while resetting host controller after err\n",
		__func__);

	return false;
}

#ifdef CONFIG_MMC_DW_IDMAC
static int dw_mci_get_dma_dir(struct mmc_data *data)
{
	if (data->flags & MMC_DATA_WRITE)
		return DMA_TO_DEVICE;
	else
		return DMA_FROM_DEVICE;
}

static void dw_mci_dma_cleanup(struct dw_mci *host)
{
	struct mmc_data *data = host->data;

	if (data)
		if (!data->host_cookie)
			dma_unmap_sg(&host->dev,
				     data->sg,
				     data->sg_len,
				     dw_mci_get_dma_dir(data));
}

static void dw_mci_idmac_stop_dma(struct dw_mci *host)
{
	u32 temp;

	/* Disable and reset the IDMAC interface */
	temp = mci_readl(host, CTRL);
	temp &= ~SDMMC_CTRL_USE_IDMAC;
	mci_writel(host, CTRL, temp);

	/* reset the IDMAC interface */
	dw_mci_wait_reset(&host->dev, host, SDMMC_CTRL_DMA_RESET);

	/* Stop the IDMAC running */
	temp = mci_readl(host, BMOD);
	temp &= ~(SDMMC_IDMAC_ENABLE | SDMMC_IDMAC_FB);
	mci_writel(host, BMOD, temp);
}

static void dw_mci_idma_reset_dma(struct dw_mci *host)
{
	u32 temp;

	temp = mci_readl(host, BMOD);
	/* Software reset of DMA */
	temp |= SDMMC_IDMAC_SWRESET;
	mci_writel(host, BMOD, temp);
}

static void dw_mci_idmac_complete_dma(struct dw_mci *host)
{
	struct mmc_data *data = host->data;

	dev_vdbg(&host->dev, "DMA complete\n");

	host->dma_ops->cleanup(host);

	/*
	 * If the card was removed, data will be NULL. No point in trying to
	 * send the stop command or waiting for NBUSY in this case.
	 */
	if (data) {
		set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
		tasklet_schedule(&host->tasklet);
	}
}

static void dw_mci_translate_sglist(struct dw_mci *host, struct mmc_data *data,
				    unsigned int sg_len)
{
	int i, j;
	int desc_cnt = 0;
	struct idmac_desc *desc = host->sg_cpu;

	for (i = 0; i < sg_len; i++) {
		unsigned int length = sg_dma_len(&data->sg[i]);
		unsigned int sz_per_desc;
		int left = (int)length;
		u32 mem_addr = sg_dma_address(&data->sg[i]);

		for (j = 0; j < (length + 4096 - 1) / 4096; j++) {
			/*
			 * Set the OWN bit
			 * and disable interrupts for this descriptor
			 */
			desc->des0 = IDMAC_DES0_OWN | IDMAC_DES0_DIC |
			             IDMAC_DES0_CH;

			/* Buffer length */
			sz_per_desc = min(left, 4096);
			IDMAC_SET_BUFFER1_SIZE(desc, sz_per_desc);

			/* Physical address to DMA to/from */
			desc->des2 = mem_addr;

			desc++;
			desc_cnt++;
			mem_addr += sz_per_desc;
			left -= sz_per_desc;
		}
	}

	/* Set first descriptor */
	desc = host->sg_cpu;
	desc->des0 |= IDMAC_DES0_FD;

	/* Set last descriptor */
	desc = host->sg_cpu + (desc_cnt - 1) * sizeof(struct idmac_desc);
	desc->des0 &= ~(IDMAC_DES0_CH | IDMAC_DES0_DIC);
	desc->des0 |= IDMAC_DES0_LD;

	wmb();
}

static void dw_mci_idmac_start_dma(struct dw_mci *host, unsigned int sg_len)
{
	u32 temp;

	dw_mci_translate_sglist(host, host->data, sg_len);

	/* Select IDMAC interface */
	temp = mci_readl(host, CTRL);
	temp |= SDMMC_CTRL_USE_IDMAC;
	mci_writel(host, CTRL, temp);

	wmb();

	/* Enable the IDMAC */
	temp = mci_readl(host, BMOD);
	temp |= SDMMC_IDMAC_ENABLE | SDMMC_IDMAC_FB;
	mci_writel(host, BMOD, temp);

	/* Start it running */
	mci_writel(host, PLDMND, 1);
}

static int dw_mci_idmac_init(struct dw_mci *host)
{
	struct idmac_desc *p;
	int i;

	/* Number of descriptors in the ring buffer */
	host->ring_size = host->desc_sz * PAGE_SIZE / sizeof(struct idmac_desc);

	/* Forward link the descriptor list */
	for (i = 0, p = host->sg_cpu; i < host->ring_size - 1; i++, p++)
		p->des3 = host->sg_dma + (sizeof(struct idmac_desc) * (i + 1));

	/* Set the last descriptor as the end-of-ring descriptor */
	p->des3 = host->sg_dma;
	p->des0 = IDMAC_DES0_ER;

	mci_writel(host, BMOD, SDMMC_IDMAC_SWRESET);

	/* Mask out interrupts - get Tx & Rx complete only */
	mci_writel(host, IDINTEN, SDMMC_IDMAC_INT_NI | SDMMC_IDMAC_INT_RI |
		   SDMMC_IDMAC_INT_TI);

	/* Set the descriptor base address */
	mci_writel(host, DBADDR, host->sg_dma);

	host->align_size = (host->data_shift == 3) ? 8 : 4;

	return 0;
}

static int dw_mci_pre_dma_transfer(struct dw_mci *host,
				   struct mmc_data *data,
				   bool next)
{
	struct scatterlist *sg;
	unsigned int i, sg_len;
	unsigned int align_mask = host->align_size - 1;

	if (!next && data->host_cookie)
		return data->host_cookie;

	/*
	 * We don't do DMA on "complex" transfers, i.e. with
	 * non-word-aligned buffers or lengths. Also, we don't bother
	 * with all the DMA setup overhead for short transfers.
	 */
	if (data->blocks * data->blksz < DW_MCI_DMA_THRESHOLD)
		return -EINVAL;

	if (data->blksz & align_mask)
		return -EINVAL;

	for_each_sg(data->sg, sg, data->sg_len, i) {
		if (sg->offset & align_mask || sg->length & align_mask)
			return -EINVAL;
	}

	sg_len = dma_map_sg(&host->dev,
			    data->sg,
			    data->sg_len,
			    dw_mci_get_dma_dir(data));
	if (sg_len == 0)
		return -EINVAL;

	if (next)
		data->host_cookie = sg_len;

	return sg_len;
}

static void dw_mci_ttp_apply(struct dw_mci *host, u32 level, u32 time)
{
	struct dw_mci_mon_table *ttp_tbl = host->pdata->tp_mon_tbl;

	if (ttp_tbl) {
		ttp_tbl = &ttp_tbl[level];
		if (ttp_tbl->range) {
			dev_dbg(&host->dev, "%s: sync_cnt=%d mif=%d cpu=%d\n",
						__func__,
						host->sync_pre_cnt,
						ttp_tbl->mif_lock_value,
						ttp_tbl->cpu_lock_value);

			pm_qos_update_request_timeout(&host->pm_qos_mif,
							ttp_tbl->mif_lock_value,
							time);
			pm_qos_update_request_timeout(&host->pm_qos_cpu,
							ttp_tbl->cpu_lock_value,
							time);
		}
	}
}

static void dw_mci_ttp_request(struct dw_mci *host)
{
	struct request_list *rq;
	struct dw_mci_mon_table *ttp_tbl = host->pdata->tp_mon_tbl;
	unsigned long ttp_timeout = host->pdata->ttp_timeout;
	u32 sync_cnt;
	int pre_step = -1;
	unsigned long timeout_try, timeout;

	if (!host->mqrq->req || !host->mqrq->req->q)
		return;

	if (!ttp_timeout)
		ttp_timeout = 500000;

	rq = &host->mqrq->req->q->rq;
	sync_cnt = rq->count[BLK_RW_SYNC];

	if (!sync_cnt) {
		if (host->pm_qos_time && host->pm_qos_step > 0 &&
			time_before(jiffies,host->pm_qos_time +
				usecs_to_jiffies(ttp_timeout)))
			dw_mci_ttp_apply(host,0, 0);
		host->pm_qos_time = 0;
		host->sync_pre_cnt = 0;
		host->pm_qos_step = 0;
		return;
	}

	/* if it's still under processing to apply pm_qos */
	if (host->pm_qos_time) {
		timeout_try = host->pm_qos_time +
				usecs_to_jiffies(ttp_timeout / 2);
		timeout = host->pm_qos_time +
				usecs_to_jiffies(ttp_timeout);
		/* checking applying pm_qos time */
		if (time_before(jiffies, timeout_try))
			return;

		if (time_before(jiffies, timeout)
			&& abs(host->sync_pre_cnt - sync_cnt) < 2)
			return;

		pre_step = host->pm_qos_step;

		if (host->sync_pre_cnt < sync_cnt) {
			ttp_tbl = &ttp_tbl[host->pm_qos_step + 1];
			if (ttp_tbl->range)
				host->pm_qos_step++;
		} else {
			if (host->pm_qos_step)
				host->pm_qos_step--;
		}
	} else {
		/* This is first applying */
		host->pm_qos_step = 0;
		while (ttp_tbl->range) {
			if (sync_cnt < ttp_tbl->range)
				break;
			host->pm_qos_step++;
			ttp_tbl = &ttp_tbl[host->pm_qos_step];
		}
	}

	if (host->pm_qos_step != pre_step) {
		host->sync_pre_cnt = sync_cnt;
		dw_mci_ttp_apply(host, host->pm_qos_step, ttp_timeout);
		host->pm_qos_time = jiffies;
	}
}

static void dw_mci_pre_req(struct mmc_host *mmc,
			   struct mmc_request *mrq,
			   bool is_first_req)
{
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if (!slot->host->use_dma || !data)
		return;

	if (data->host_cookie) {
		data->host_cookie = 0;
		return;
	}

	if (slot->host->pdata->tp_mon_tbl &&
		slot->host->pdata->ttp_enabled &&
		slot->host->mqrq)
		dw_mci_ttp_request(slot->host);

	if (dw_mci_pre_dma_transfer(slot->host, mrq->data, 1) < 0)
		data->host_cookie = 0;
}

static void dw_mci_post_req(struct mmc_host *mmc,
			    struct mmc_request *mrq,
			    int err)
{
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if (!slot->host->use_dma || !data)
		return;

	if (data->host_cookie)
		dma_unmap_sg(&slot->host->dev,
			     data->sg,
			     data->sg_len,
			     dw_mci_get_dma_dir(data));
	data->host_cookie = 0;
}

static struct dw_mci_dma_ops dw_mci_idmac_ops = {
	.init = dw_mci_idmac_init,
	.start = dw_mci_idmac_start_dma,
	.stop = dw_mci_idmac_stop_dma,
	.reset = dw_mci_idma_reset_dma,
	.complete = dw_mci_idmac_complete_dma,
	.cleanup = dw_mci_dma_cleanup,
};
#else
static int dw_mci_pre_dma_transfer(struct dw_mci *host,
				   struct mmc_data *data,
				   bool next)
{
	return -ENOSYS;
}
#define dw_mci_pre_req		NULL
#define dw_mci_post_req		NULL
#endif /* CONFIG_MMC_DW_IDMAC */

static int dw_mci_submit_data_dma(struct dw_mci *host, struct mmc_data *data)
{
	int sg_len;
	u32 temp;

	host->using_dma = 0;

	/* If we don't have a channel, we can't do DMA */
	if (!host->use_dma)
		return -ENODEV;

	sg_len = dw_mci_pre_dma_transfer(host, data, 0);
	if (sg_len < 0) {
		host->dma_ops->stop(host);
		return sg_len;
	}

	host->using_dma = 1;

	dev_vdbg(&host->dev,
		 "sd sg_cpu: %#lx sg_dma: %#lx sg_len: %d\n",
		 (unsigned long)host->sg_cpu, (unsigned long)host->sg_dma,
		 sg_len);

	/* Enable the DMA interface */
	temp = mci_readl(host, CTRL);
	temp |= SDMMC_CTRL_DMA_ENABLE;
	mci_writel(host, CTRL, temp);

	/* Disable RX/TX IRQs, let DMA handle it */
	mci_writel(host, RINTSTS, SDMMC_INT_TXDR | SDMMC_INT_RXDR);
	temp = mci_readl(host, INTMASK);
	temp  &= ~(SDMMC_INT_RXDR | SDMMC_INT_TXDR);
	mci_writel(host, INTMASK, temp);

	host->dma_ops->start(host, sg_len);

	return 0;
}

static void dw_mci_submit_data(struct dw_mci *host, struct mmc_data *data)
{
	u32 temp;
	struct dw_mci_slot *slot = host->cur_slot;
	struct mmc_card *card = slot->mmc->card;

	data->error = -EINPROGRESS;

	WARN_ON(host->data);
	host->sg = NULL;
	host->data = data;

	if (card && mmc_card_sdio(card)) {
		unsigned int rxwmark_val, msize_val, i;
		unsigned int msize[8] = {1, 4, 8, 16, 32, 64, 128, 256};

		for (i = 1; i < sizeof(msize) / sizeof(unsigned int); i++) {
			if (data->blksz != 0 &&
				(data->blksz / 4) % msize[i] == 0)
				continue;
			else
				break;
		}
		if (data->blksz < host->fifo_depth / 2) {
			if (i > 1) {
				msize_val = i - 1;
				rxwmark_val = msize[i-1] - 1;
			} else {
				msize_val = 0;
				rxwmark_val = 1;
			}
		} else {
			if (i > 2) {
				msize_val = i - 2;
				rxwmark_val = msize[i-2] - 1;
			} else {
				msize_val = 0;
				rxwmark_val = 1;
			}
		}
		dev_dbg(&slot->mmc->class_dev,
			"msize_val : %d, rxwmark_val : %d\n",
			msize_val, rxwmark_val);

		host->fifoth_val = ((msize_val << 28) | (rxwmark_val << 16) |
				   ((host->fifo_depth/2) << 0));

		mci_writel(host, FIFOTH, host->fifoth_val);

		if (mmc_card_uhs(card)
				&& card->host->caps & MMC_CAP_UHS_SDR104
				&& data->flags & MMC_DATA_READ)
			mci_writel(host, CDTHRCTL, data->blksz << 16 | 1);
	}

	if (data->flags & MMC_DATA_READ)
		host->dir_status = DW_MCI_RECV_STATUS;
	else
		host->dir_status = DW_MCI_SEND_STATUS;

	if (dw_mci_submit_data_dma(host, data)) {
		int flags = SG_MITER_ATOMIC;

		if (SDMMC_GET_FCNT(mci_readl(host, STATUS)))
			dw_mci_wait_reset(&host->dev, host, SDMMC_CTRL_FIFO_RESET);

		if (host->data->flags & MMC_DATA_READ)
			flags |= SG_MITER_TO_SG;
		else
			flags |= SG_MITER_FROM_SG;

		sg_miter_start(&host->sg_miter, data->sg, data->sg_len, flags);
		host->sg = data->sg;
		host->part_buf_start = 0;
		host->part_buf_count = 0;

		mci_writel(host, RINTSTS, SDMMC_INT_TXDR | SDMMC_INT_RXDR);
		temp = mci_readl(host, INTMASK);
		temp |= SDMMC_INT_TXDR | SDMMC_INT_RXDR;
		mci_writel(host, INTMASK, temp);

		temp = mci_readl(host, CTRL);
		temp &= ~SDMMC_CTRL_DMA_ENABLE;
		mci_writel(host, CTRL, temp);
	}
}

static bool dw_mci_wait_data_busy(struct dw_mci *host, struct mmc_request *mrq)
{
	u32 status;
	unsigned long timeout = jiffies + msecs_to_jiffies(1000);
	int try = 3;

	do {
		do {
			status = mci_readl(host, STATUS);
			if (!(status & SDMMC_DATA_BUSY))
				return true;

			usleep_range(10, 20);
		} while (time_before(jiffies, timeout));

		/* card is checked every 1s by CMD13 at least */
		if (mrq->cmd->opcode == MMC_SEND_STATUS)
			return true;

		dw_mci_wait_reset(&host->dev, host, SDMMC_CTRL_RESET);
		/* After CTRL Reset, Should be needed clk val to CIU */
		if (host->cur_slot)
			mci_send_cmd(host->cur_slot,
				SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);
		timeout = jiffies + msecs_to_jiffies(1000);
	} while (--try);

	dev_err(&host->dev, "Data[0]: data is busy\n");

	return false;
}

static void dw_mci_setup_bus(struct dw_mci_slot *slot, int force)
{
	struct dw_mci *host = slot->host;
	u32 div, actual_speed;
	bool reset_div = false;

	if (slot->clock && ((slot->clock != host->current_speed) || force)) {
		do {
			div = host->bus_hz / slot->clock;
			if ((host->bus_hz % slot->clock) &&
				(host->bus_hz > slot->clock))
				/*
				 * move the + 1 after the divide to prevent
				 * over-clocking the card.
				 */
				div++;

			div = (host->bus_hz != slot->clock) ?
				DIV_ROUND_UP(div, 2) : 0;

			/* CLKDIV limitation is 0xFF */
			if (div > 0xFF)
				div = 0xFF;

			actual_speed = div ?
				(host->bus_hz / div) >> 1 : host->bus_hz;

			/* Change SCLK_MMC */
			if (actual_speed > slot->clock &&
				host->bus_hz != 0 && !reset_div) {
				dev_err(&host->dev,
					"Actual clock is high than a reqeust clock."
					"Source clock is needed to change\n");
				reset_div = true;
				slot->host->pdata->set_io_timing(slot->host, MMC_TIMING_LEGACY);
			} else
				reset_div = false;
		} while(reset_div);

		dev_info(&slot->mmc->class_dev,
			 "Bus speed (slot %d) = %dHz (slot req %dHz, actual %dHZ"
			 " div = %d)\n", slot->id, host->bus_hz, slot->clock,
			 div ? ((host->bus_hz / div) >> 1) : host->bus_hz, div);

		/* disable clock */
		mci_writel(host, CLKENA, 0);
		mci_writel(host, CLKSRC, 0);

		/* inform CIU */
		mci_send_cmd(slot,
			     SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);

		/* set clock to desired speed */
		mci_writel(host, CLKDIV, div);

		/* inform CIU */
		mci_send_cmd(slot,
			     SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);

		/* enable clock */
		mci_writel(host, CLKENA, ((SDMMC_CLKEN_ENABLE |
			   SDMMC_CLKEN_LOW_PWR) << slot->id));

		/* inform CIU */
		mci_send_cmd(slot,
			     SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);

		host->current_speed = slot->clock;
	}

	/* Set the current slot bus width */
	mci_writel(host, CTYPE, (slot->ctype << slot->id));
}

static void __dw_mci_start_request(struct dw_mci *host,
				   struct dw_mci_slot *slot,
				   struct mmc_command *cmd)
{
	struct mmc_request *mrq;
	struct mmc_data	*data;
	u32 cmdflags;

	mrq = slot->mrq;
	host->stop_cmdr = 0;
	host->stop_snd = false;
	if (host->pdata->select_slot)
		host->pdata->select_slot(slot->id);

	/* Slot specific timing and width adjustment */
	dw_mci_setup_bus(slot, 0);

	mod_timer(&host->timer, jiffies + msecs_to_jiffies(10000));

	host->cur_slot = slot;
	host->mrq = mrq;

	host->pending_events = 0;
	host->completed_events = 0;
	host->cmd_status = 0;
	host->data_status = 0;
	host->dir_status = 0;
	host->bytcnt = 0;

	if (host->pdata->tp_mon_tbl)
		host->cmd_cnt++;

	data = cmd->data;
	if (data) {
		dw_mci_set_timeout(host);
		host->bytcnt = data->blksz * data->blocks;
		mci_writel(host, BYTCNT, host->bytcnt);
		mci_writel(host, BLKSIZ, data->blksz);
		if (host->pdata->tp_mon_tbl)
			host->transferred_cnt += data->blksz * data->blocks;
	}

	cmdflags = dw_mci_prepare_command(slot->mmc, cmd);

	/* this is the first command, send the initialization clock */
	if (test_and_clear_bit(DW_MMC_CARD_NEED_INIT, &slot->flags))
		cmdflags |= SDMMC_CMD_INIT;

	if (data) {
		dw_mci_submit_data(host, data);
		wmb();
	}

	dw_mci_start_command(host, cmd, cmdflags);

	if (mrq->stop)
		host->stop_cmdr = dw_mci_prepare_command(slot->mmc, mrq->stop);
	else {
		if (data)
			host->stop_cmdr = dw_mci_prep_stop(host, cmd);
	}
}

static void dw_mci_start_request(struct dw_mci *host,
				 struct dw_mci_slot *slot)
{
	struct mmc_request *mrq = slot->mrq;
	struct mmc_command *cmd;

	cmd = mrq->sbc ? mrq->sbc : mrq->cmd;
	__dw_mci_start_request(host, slot, cmd);
}

/* must be called with host->lock held */
static void dw_mci_queue_request(struct dw_mci *host, struct dw_mci_slot *slot,
				 struct mmc_request *mrq)
{
	dev_vdbg(&slot->mmc->class_dev, "queue request: state=%d\n",
		 host->state);

	slot->mrq = mrq;

	if (host->state == STATE_IDLE) {
		host->state = STATE_SENDING_CMD;
		dw_mci_start_request(host, slot);
	} else {
		list_add_tail(&slot->queue_node, &host->queue);
	}
}

static void dw_mci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci *host = slot->host;

	WARN_ON(slot->mrq);

	dw_mci_qos_get(host);

	/* for mmc trace */
	host->mqrq = mmc->mqrq_cur;

	if (!test_bit(DW_MMC_CARD_PRESENT, &slot->flags)) {
		dw_mci_qos_put(host);
		mrq->cmd->error = -ENOMEDIUM;
		mmc_request_done(mmc, mrq);
		return;
	}

	if (!dw_mci_stop_abort_cmd(mrq->cmd)) {
		if (!dw_mci_wait_data_busy(host, mrq)) {
			dw_mci_qos_put(host);
			mrq->cmd->error = -ENOTRECOVERABLE;
			mmc_request_done(mmc, mrq);
			return;
		}
	}

	spin_lock_bh(&host->lock);

	dw_mci_queue_request(host, slot, mrq);

	spin_unlock_bh(&host->lock);
}

static void dw_mci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct dw_mci_slot *slot = mmc_priv(mmc);
	u32 regs;

	/* set default 1 bit mode */
	slot->ctype = SDMMC_CTYPE_1BIT;

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		slot->ctype = SDMMC_CTYPE_1BIT;
		break;
	case MMC_BUS_WIDTH_4:
		slot->ctype = SDMMC_CTYPE_4BIT;
		break;
	case MMC_BUS_WIDTH_8:
		slot->ctype = SDMMC_CTYPE_8BIT;
		break;
	}

	regs = mci_readl(slot->host, UHS_REG);

	/* DDR mode set */
	if (ios->timing == MMC_TIMING_UHS_DDR50 ||
	    ios->timing == MMC_TIMING_MMC_HS200_DDR)
		regs |= (0x1 << slot->id) << 16;
	else
		regs &= ~(0x1 << slot->id) << 16;

	if (slot->host->pdata->caps &
				(MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 |
				 MMC_CAP_UHS_SDR50 | MMC_CAP_UHS_SDR104 |
				 MMC_CAP_UHS_DDR50))
		regs |= (0x1 << slot->id);

	mci_writel(slot->host, UHS_REG, regs);

	if (ios->timing == MMC_TIMING_MMC_HS200_DDR)
		mci_writel(slot->host, CDTHRCTL, 512 << 16 | 1);

	if (slot->host->pdata->set_io_timing)
		slot->host->pdata->set_io_timing(slot->host, ios->timing);

	if (ios->clock) {
		/*
		 * Use mirror of ios->clock to prevent race with mmc
		 * core ios update when finding the minimum.
		 */
		slot->clock = ios->clock;
		spin_lock(&slot->host->cclk_lock);
		dw_mci_ciu_clk_en(slot->host);
		spin_unlock(&slot->host->cclk_lock);
	} else {
		spin_lock(&slot->host->cclk_lock);
		dw_mci_ciu_clk_dis(slot->host);
		spin_unlock(&slot->host->cclk_lock);
	}

	switch (ios->power_mode) {
	case MMC_POWER_UP:
		spin_lock(&slot->host->cclk_lock);
		dw_mci_ciu_clk_en(slot->host);
		spin_unlock(&slot->host->cclk_lock);

		set_bit(DW_MMC_CARD_NEED_INIT, &slot->flags);
		if (slot->host->pdata->tp_mon_tbl &&
			!slot->host->pdata->ttp_enabled)
			schedule_delayed_work(&slot->host->tp_mon, HZ);
		break;
	case MMC_POWER_OFF:
		spin_lock(&slot->host->cclk_lock);
		dw_mci_ciu_clk_dis(slot->host);
		spin_unlock(&slot->host->cclk_lock);

		if (slot->host->pdata->tp_mon_tbl) {
			if (!slot->host->pdata->ttp_enabled)
			cancel_delayed_work_sync(&slot->host->tp_mon);
			pm_qos_update_request(&slot->host->pm_qos_mif, 0);
			pm_qos_update_request(&slot->host->pm_qos_cpu, 0);
		}
		break;
	case MMC_POWER_ON:
		/* To cheat supporting hardware reset using power off/on
		 * as reset function to modify reset function value of ext_csd reg
		 */
		if (mmc->caps & MMC_CAP_HW_RESET && mmc->card &&
			slot->host->quirks & DW_MMC_QUIRK_HW_RESET_PW)
			mmc->card->ext_csd.rst_n_function |= EXT_CSD_RST_N_ENABLED;
		break;
	default:
		break;
	}
}

static int dw_mci_get_ro(struct mmc_host *mmc)
{
	int read_only;
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci_board *brd = slot->host->pdata;

	/* Use platform get_ro function, else try on board write protect */
	if (brd->get_ro)
		read_only = brd->get_ro(slot->id);
	else
		read_only =
			mci_readl(slot->host, WRTPRT) & (1 << slot->id) ? 1 : 0;

	dev_dbg(&mmc->class_dev, "card is %s\n",
		read_only ? "read-only" : "read-write");

	return read_only;
}

static int dw_mci_get_cd(struct mmc_host *mmc)
{
	int present;
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci_board *brd = slot->host->pdata;
	struct dw_mci *host = slot->host;

	/* Use platform get_cd function, else try onboard card detect */
	if (brd->quirks & DW_MCI_QUIRK_BROKEN_CARD_DETECTION)
		present = 1;
	else if (brd->get_cd)
		present = !brd->get_cd(slot->id);
	else
		present = (mci_readl(host, CDETECT) & (1 << slot->id))
			== 0 ? 1 : 0;

	if (present)
		dev_dbg(&mmc->class_dev, "card is present\n");
	else
		dev_dbg(&mmc->class_dev, "card is not present\n");

	return present;
}

static void dw_mci_enable_sdio_irq(struct mmc_host *mmc, int enb)
{
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci *host = slot->host;
	u32 int_mask;

	/* Enable/disable Slot Specific SDIO interrupt */
	int_mask = mci_readl(host, INTMASK);
	if (enb) {
		mci_writel(host, INTMASK,
			   (int_mask | SDMMC_INT_SDIO(slot->id)));
	} else {
		mci_writel(host, INTMASK,
			   (int_mask & ~SDMMC_INT_SDIO(slot->id)));
	}
}

static void dw_mci_set_quirk_endbit(struct dw_mci *host, s8 mid)
{
	u32 clksel, phase;
	u32 shift;

	clksel = mci_readl(host, CLKSEL);
	phase = (((clksel >> 24) & 0x7) + 1) << 1;
	shift = 360 / phase;

	if (host->verid < DW_MMC_260A && (shift * mid) % 360 >= 225)
		host->quirks |= DW_MCI_QUIRK_NO_DETECT_EBIT;
	else
		host->quirks &= ~DW_MCI_QUIRK_NO_DETECT_EBIT;
}

static u8 dw_mci_tuning_sampling(struct dw_mci *host)
{
	u32 clksel;
	u8 sample;

	clksel = mci_readl(host, CLKSEL);
	sample = (clksel + 1) & 0x7;
	clksel = (clksel & 0xfffffff8) | sample;
	mci_writel(host, CLKSEL, clksel);

	return sample;
}

static void dw_mci_set_sampling(struct dw_mci *host, u8 sample)
{
	u32 clksel;

	clksel = mci_readl(host, CLKSEL);
	clksel = (clksel & 0xfffffff8) | (sample & 0x7);
	mci_writel(host, CLKSEL, clksel);
	host->pdata->clk_smpl = sample;
	dw_mci_set_quirk_endbit(host, sample);
}

static u8 dw_mci_get_sampling(struct dw_mci *host)
{
	u32 clksel;
	u8 sample;

	clksel = mci_readl(host, CLKSEL);
	sample = clksel & 0x7;

	return sample;
}

static s8 get_median_sample(u8 map)
{
	const u8 iter = 8;
	u8 __map;
	s8 i, sel = -1;

	for (i = 0; i < iter; i++) {
		__map = ror8(map, i);
		if ((__map & 0xc7) == 0xc7) {
			sel = i;
			goto out;
		}
	}

	for (i = 0; i < iter; i++) {
		__map = ror8(map, i);
		if ((__map & 0x83) == 0x83) {
			sel = i;
			goto out;
		}
	}

out:
	return sel;
}

static void dw_mci_save_drv_st(struct dw_mci_slot *slot)
{
	struct dw_mci_board *brd = slot->host->pdata;

	if (brd->save_drv_st)
		brd->save_drv_st(slot->host, slot->id);
}

static void dw_mci_restore_drv_st(struct dw_mci_slot *slot)
{
	struct dw_mci_board *brd = slot->host->pdata;

	if (brd->restore_drv_st)
		brd->restore_drv_st(slot->host, slot->id);
}

static void dw_mci_tuning_drv_st(struct dw_mci_slot *slot)
{
	struct dw_mci_board *brd = slot->host->pdata;

	if (brd->tuning_drv_st)
		brd->tuning_drv_st(slot->host, slot->id);
}

static int dw_mci_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci *host = slot->host;
	unsigned int tuning_loop = MAX_TUNING_LOOP;
	unsigned int retries = 4;
	const u8 *tuning_blk_pattern;
	bool tuned = false;
	int ret = 0;
	u8 *tuning_blk;
	u8 blksz;
	u8 tune, start_tune, map = 0;
	s8 mid;

	if (opcode == MMC_SEND_TUNING_BLOCK_HS200) {
		if (mmc->ios.bus_width == MMC_BUS_WIDTH_8) {
			tuning_blk_pattern = tuning_blk_pattern_8bit;
			blksz = 128;
		} else if (mmc->ios.bus_width == MMC_BUS_WIDTH_4) {
			tuning_blk_pattern = tuning_blk_pattern_4bit;
			blksz = 64;
		} else
			return -EINVAL;
	} else if (opcode == MMC_SEND_TUNING_BLOCK) {
		tuning_blk_pattern = tuning_blk_pattern_4bit;
		blksz = 64;
	} else {
		dev_err(&mmc->class_dev,
			"Undefined command(%d) for tuning\n",
			opcode);
		return -EINVAL;
	}

	if (host->pdata->tuned) {
		dw_mci_set_sampling(host, host->pdata->clk_smpl);
		mci_writel(host, CDTHRCTL, host->cd_rd_thr << 16 | 1);
		return 0;
	}

	tuning_blk = kmalloc(blksz, GFP_KERNEL);
	if (!tuning_blk)
		return -ENOMEM;

	start_tune = dw_mci_get_sampling(host);
	host->cd_rd_thr = 512;
	mci_writel(host, CDTHRCTL, host->cd_rd_thr << 16 | 1);

	dw_mci_save_drv_st(slot);

	do {
		struct mmc_request mrq = {NULL};
		struct mmc_command cmd = {0};
		struct mmc_command stop = {0};
		struct mmc_data data = {0};
		struct scatterlist sg;

		if (!tuning_loop)
			break;

		cmd.opcode = opcode;
		cmd.arg = 0;
		cmd.flags = MMC_RSP_R1 | MMC_CMD_ADTC;

		stop.opcode = MMC_STOP_TRANSMISSION;
		stop.arg = 0;
		stop.flags = MMC_RSP_R1B | MMC_CMD_AC;

		data.blksz = blksz;
		data.blocks = 1;
		data.flags = MMC_DATA_READ;
		data.sg = &sg;
		data.sg_len = 1;

		sg_init_one(&sg, tuning_blk, blksz);
		dw_mci_set_timeout(host);

		mrq.cmd = &cmd;
		mrq.stop = &stop;
		mrq.data = &data;
		host->mrq = &mrq;

		tune = dw_mci_tuning_sampling(host);

		mmc_wait_for_req(mmc, &mrq);

		if (!cmd.error && !data.error) {
			if (!memcmp(tuning_blk_pattern, tuning_blk, blksz))
				map |= (1 << tune);
		} else {
			dev_dbg(&mmc->class_dev,
				"Tuning error: cmd.error:%d, data.error:%d\n",
				cmd.error, data.error);
		}

		if (start_tune == tune) {
			mid = get_median_sample(map);
			retries--;
			if (mid >= 0) {
				if (map != 0xff || !retries)
					tuned = true;
			}

			if (retries) {
				dw_mci_tuning_drv_st(slot);
				map = 0;
			}
		}
		tuning_loop--;
	} while (!tuned && retries);

	if (tuned) {
		dw_mci_set_sampling(host, mid);
		if (host->pdata->only_once_tune)
			host->pdata->tuned = true;
	} else {
		mci_writel(host, CDTHRCTL, 0 << 16 | 0);
		dw_mci_set_sampling(host, start_tune);
		ret = -EIO;
	}

	dw_mci_restore_drv_st(slot);
	kfree(tuning_blk);

	return ret;
}

static int dw_mci_3_3v_signal_voltage_switch(struct dw_mci_slot *slot)
{
	struct dw_mci *host = slot->host;
	u32 reg;
	int ret = 0;

	if (host->vqmmc) {
		ret = regulator_set_voltage(host->vqmmc, 2800000, 2800000);
		if (ret) {
			dev_warn(&host->dev, "Switching to 3.3V signalling "
					"voltage failed\n");
			return -EIO;
		}
	} else {
		reg = mci_readl(slot->host, UHS_REG);
		reg &= ~(0x1 << slot->id);
		mci_writel(slot->host, UHS_REG, reg);
	}

	/* Wait for 5ms */
	usleep_range(5000, 5500);

	return ret;
}

static int dw_mci_1_8v_signal_voltage_switch(struct dw_mci_slot *slot)
{
	struct dw_mci *host = slot->host;
	u32 reg;
	int ret = 0;

	dw_mci_wait_reset(&host->dev, host, SDMMC_CTRL_RESET);
	reg = mci_readl(host, CLKENA);
	reg &= ~((SDMMC_CLKEN_LOW_PWR | SDMMC_CLKEN_ENABLE) << slot->id);
	mci_writel(host, CLKENA, reg);
	mci_send_cmd(slot, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);

	if (host->vqmmc) {
		ret = regulator_set_voltage(host->vqmmc, 1800000, 1800000);
		if (ret) {
			dev_warn(&host->dev, "Switching to 1.8V signalling "
					"voltage failed\n");
			return -EIO;
		}
	} else {
		reg = mci_readl(slot->host, UHS_REG);
		reg |= (0x1 << slot->id);
		mci_writel(slot->host, UHS_REG, reg);
	}

	/* Wait for 5ms */
	usleep_range(5000, 5500);

	reg = mci_readl(host, CLKENA);
	reg |= SDMMC_CLKEN_ENABLE << slot->id;
	mci_writel(host, CLKENA, reg);
	mci_send_cmd(slot, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);

	return ret;
}

static int dw_mci_start_signal_voltage_switch(struct mmc_host *mmc,
		struct mmc_ios *ios)
{
	struct dw_mci_slot *slot = mmc_priv(mmc);

	if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_330)
		return dw_mci_3_3v_signal_voltage_switch(slot);
	else if (ios->signal_voltage == MMC_SIGNAL_VOLTAGE_180)
		return dw_mci_1_8v_signal_voltage_switch(slot);
	else
		return 0;
}

static void dw_mci_hw_reset(struct mmc_host *host)
{
	struct dw_mci_slot *slot = mmc_priv(host);
	struct dw_mci_board *brd = slot->host->pdata;

	dev_warn(&host->class_dev, "device is being hw reset\n");

	/* Use platform hw_reset function */
	if (brd->hw_reset)
		brd->hw_reset(slot->id);
}

static const struct mmc_host_ops dw_mci_ops = {
	.request		= dw_mci_request,
	.pre_req		= dw_mci_pre_req,
	.post_req		= dw_mci_post_req,
	.set_ios		= dw_mci_set_ios,
	.get_ro			= dw_mci_get_ro,
	.get_cd			= dw_mci_get_cd,
	.enable_sdio_irq	= dw_mci_enable_sdio_irq,
	.execute_tuning		= dw_mci_execute_tuning,
	.start_signal_voltage_switch	= dw_mci_start_signal_voltage_switch,
	.hw_reset		= dw_mci_hw_reset,
};

static void dw_mci_request_end(struct dw_mci *host, struct mmc_request *mrq)
	__releases(&host->lock)
	__acquires(&host->lock)
{
	struct dw_mci_slot *slot;
	struct mmc_host	*prev_mmc = host->cur_slot->mmc;

	WARN_ON(host->cmd || host->data);

	del_timer(&host->timer);

	host->cur_slot->mrq = NULL;
	host->mrq = NULL;
	if (!list_empty(&host->queue)) {
		slot = list_entry(host->queue.next,
				  struct dw_mci_slot, queue_node);
		list_del(&slot->queue_node);
		dev_vdbg(&host->dev, "list not empty: %s is next\n",
			 mmc_hostname(slot->mmc));
		host->state = STATE_SENDING_CMD;
		dw_mci_start_request(host, slot);
	} else {
		dev_vdbg(&host->dev, "list empty\n");
		host->state = STATE_IDLE;
	}

	spin_unlock(&host->lock);
	dw_mci_qos_put(host);
	mmc_request_done(prev_mmc, mrq);
	spin_lock(&host->lock);
}

static void dw_mci_command_complete(struct dw_mci *host, struct mmc_command *cmd)
{
	u32 status = host->cmd_status;

	host->cmd_status = 0;

	/* Read the response from the card (up to 16 bytes) */
	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			cmd->resp[3] = mci_readl(host, RESP0);
			cmd->resp[2] = mci_readl(host, RESP1);
			cmd->resp[1] = mci_readl(host, RESP2);
			cmd->resp[0] = mci_readl(host, RESP3);
		} else {
			cmd->resp[0] = mci_readl(host, RESP0);
			cmd->resp[1] = 0;
			cmd->resp[2] = 0;
			cmd->resp[3] = 0;
		}
	}

	if (status & SDMMC_INT_RTO)
		cmd->error = -ETIMEDOUT;
	else if ((cmd->flags & MMC_RSP_CRC) && (status & SDMMC_INT_RCRC))
		cmd->error = -EILSEQ;
	else if (status & SDMMC_INT_RESP_ERR)
		cmd->error = -EIO;
	else
		cmd->error = 0;

	if (cmd->error) {
		/* newer ip versions need a delay between retries */
		if (host->quirks & DW_MCI_QUIRK_RETRY_DELAY)
			mdelay(20);
	}
}

static void dw_mci_tasklet_func(unsigned long priv)
{
	struct dw_mci *host = (struct dw_mci *)priv;
	struct mmc_data	*data;
	struct mmc_command *cmd;
	enum dw_mci_state state;
	enum dw_mci_state prev_state;
	u32 status;

	spin_lock(&host->lock);

	state = host->state;
	data = host->data;

	if (host->cmd_status & SDMMC_INT_HLE) {
		clear_bit(EVENT_CMD_COMPLETE, &host->pending_events);
		dev_err(&host->dev, "hardware locked write error\n");
		goto unlock;
	}

	do {
		prev_state = state;

		switch (state) {
		case STATE_IDLE:
			break;

		case STATE_SENDING_CMD:
			if (!test_and_clear_bit(EVENT_CMD_COMPLETE,
						&host->pending_events))
				break;

			cmd = host->cmd;
			host->cmd = NULL;
			set_bit(EVENT_CMD_COMPLETE, &host->completed_events);
			dw_mci_command_complete(host, cmd);
			if (cmd == host->mrq->sbc && !cmd->error) {
				prev_state = state = STATE_SENDING_CMD;
				__dw_mci_start_request(host, host->cur_slot,
						       host->mrq->cmd);
				goto unlock;
			}

			if (data && cmd->error &&
					cmd != data->stop) {
				if (host->mrq->data->stop)
					send_stop_cmd(host, host->mrq->data);
				else {
					dw_mci_start_command(host, &host->stop,
							host->stop_cmdr);
					host->stop_snd = true;
				}
				/* To avoid fifo full condition */
				dw_mci_fifo_reset(&host->dev, host);
				state = STATE_SENDING_STOP;
				break;
			}

			if (!host->mrq->data || cmd->error) {
				dw_mci_request_end(host, host->mrq);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_DATA;
			/* fall through */

		case STATE_SENDING_DATA:
			if (test_and_clear_bit(EVENT_DATA_ERROR,
					       &host->pending_events)) {
				set_bit(EVENT_XFER_COMPLETE,
						&host->pending_events);
				if (data->stop)
					send_stop_cmd(host, data);
				else {
					dw_mci_start_command(host,
							&host->stop,
							host->stop_cmdr);
					host->stop_snd = true;
				}
				/* To avoid fifo full condition */
				dw_mci_fifo_reset(&host->dev, host);
				state = STATE_DATA_ERROR;
				break;
			}

			if (!test_and_clear_bit(EVENT_XFER_COMPLETE,
						&host->pending_events))
				break;

			set_bit(EVENT_XFER_COMPLETE, &host->completed_events);
			prev_state = state = STATE_DATA_BUSY;
			/* fall through */

		case STATE_DATA_BUSY:
			if (!test_and_clear_bit(EVENT_DATA_COMPLETE,
						&host->pending_events))
				break;

			set_bit(EVENT_DATA_COMPLETE, &host->completed_events);
			status = host->data_status;

			if (status & DW_MCI_DATA_ERROR_FLAGS) {
				if (status & SDMMC_INT_DTO) {
					dev_err(&host->dev,
						"data timeout error\n");
					data->error = -ETIMEDOUT;
				} else if (status & SDMMC_INT_DCRC) {
					dev_err(&host->dev,
						"data CRC error\n");
					data->error = -EILSEQ;
				} else if (status & SDMMC_INT_EBE) {
					if (host->dir_status ==
							DW_MCI_SEND_STATUS) {
						/*
						 * No data CRC status was returned.
						 * The number of bytes transferred will
						 * be exaggerated in PIO mode.
						 */
						data->bytes_xfered = 0;
						data->error = -ETIMEDOUT;
						dev_err(&host->dev,
							"Write no CRC\n");
					} else {
						data->error = -EIO;
						dev_err(&host->dev,
							"End bit error\n");
					}

				} else if (status & SDMMC_INT_SBE) {
					dev_err(&host->dev,
						"Start bit error "
						"(status=%08x)\n",
						status);
					data->error = -EIO;
				} else {
					dev_err(&host->dev,
						"data FIFO error "
						"(status=%08x)\n",
						status);
					data->error = -EIO;
				}
				/*
				 * After an error, there may be data lingering
				 * in the FIFO, so reset it - doing so
				 * generates a block interrupt, hence setting
				 * the scatter-gather pointer to NULL.
				 */
				sg_miter_stop(&host->sg_miter);
				host->sg = NULL;
				dw_mci_fifo_reset(&host->dev, host);
			} else {
				data->bytes_xfered = data->blocks * data->blksz;
				data->error = 0;
			}

			host->data = NULL;

			if (!data->stop && !host->stop_snd) {
				dw_mci_request_end(host, host->mrq);
				goto unlock;
			}

			if (host->mrq->sbc && !data->error) {
				if (data->stop)
					data->stop->error = 0;
				dw_mci_request_end(host, host->mrq);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_STOP;
			if (!data->error)
				send_stop_cmd(host, data);

			if (test_and_clear_bit(EVENT_DATA_ERROR,
						&host->pending_events)) {
				if (data->stop)
					send_stop_cmd(host, data);
				else {
					dw_mci_start_command(host,
							&host->stop,
							host->stop_cmdr);
					host->stop_snd = true;
				}
			}
			/* fall through */

		case STATE_SENDING_STOP:
			if (!test_and_clear_bit(EVENT_CMD_COMPLETE,
						&host->pending_events))
				break;

			if (host->mrq->cmd->error &&
					host->mrq->data) {
				dw_mci_stop_dma(host);
				sg_miter_stop(&host->sg_miter);
				host->sg = NULL;
				dw_mci_fifo_reset(&host->dev, host);
				dw_mci_ciu_reset(&host->dev, host);
			}

			host->cmd = NULL;
			host->data = NULL;

			if (host->mrq->stop)
				dw_mci_command_complete(host, host->mrq->stop);
			else
				host->cmd_status = 0;

			dw_mci_request_end(host, host->mrq);
			goto unlock;

		case STATE_DATA_ERROR:
			if (!test_and_clear_bit(EVENT_XFER_COMPLETE,
						&host->pending_events))
				break;

			dw_mci_stop_dma(host);
			set_bit(EVENT_XFER_COMPLETE, &host->completed_events);

			state = STATE_DATA_BUSY;
			break;
		}
	} while (state != prev_state);

	host->state = state;
unlock:
	spin_unlock(&host->lock);

}

/* push final bytes to part_buf, only use during push */
static void dw_mci_set_part_bytes(struct dw_mci *host, void *buf, int cnt)
{
	memcpy((void *)&host->part_buf, buf, cnt);
	host->part_buf_count = cnt;
}

/* append bytes to part_buf, only use during push */
static int dw_mci_push_part_bytes(struct dw_mci *host, void *buf, int cnt)
{
	cnt = min(cnt, (1 << host->data_shift) - host->part_buf_count);
	memcpy((void *)&host->part_buf + host->part_buf_count, buf, cnt);
	host->part_buf_count += cnt;
	return cnt;
}

/* pull first bytes from part_buf, only use during pull */
static int dw_mci_pull_part_bytes(struct dw_mci *host, void *buf, int cnt)
{
	cnt = min(cnt, (int)host->part_buf_count);
	if (cnt) {
		memcpy(buf, (void *)&host->part_buf + host->part_buf_start,
		       cnt);
		host->part_buf_count -= cnt;
		host->part_buf_start += cnt;
	}
	return cnt;
}

/* pull final bytes from the part_buf, assuming it's just been filled */
static void dw_mci_pull_final_bytes(struct dw_mci *host, void *buf, int cnt)
{
	memcpy(buf, &host->part_buf, cnt);
	host->part_buf_start = cnt;
	host->part_buf_count = (1 << host->data_shift) - cnt;
}

static void dw_mci_push_data16(struct dw_mci *host, void *buf, int cnt)
{
	u32 tbb;

	/* try and push anything in the part_buf */
	if (unlikely(host->part_buf_count)) {
		int len = dw_mci_push_part_bytes(host, buf, cnt);
		tbb = mci_readl(host, TBBCNT);
		buf += len;
		cnt -= len;
		if (tbb + len == host->bytcnt || host->part_buf_count == 2) {
			mci_writew(host, DATA(host->data_offset),
					host->part_buf16);
			host->part_buf_count = 0;
		}
	}
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (unlikely((unsigned long)buf & 0x1)) {
		while (cnt >= 2) {
			u16 aligned_buf[64];
			int len = min(cnt & -2, (int)sizeof(aligned_buf));
			int items = len >> 1;
			int i;
			/* memcpy from input buffer into aligned buffer */
			memcpy(aligned_buf, buf, len);
			buf += len;
			cnt -= len;
			/* push data from aligned buffer into fifo */
			for (i = 0; i < items; ++i)
				mci_writew(host, DATA(host->data_offset),
						aligned_buf[i]);
		}
	} else
#endif
	{
		u16 *pdata = buf;
		for (; cnt >= 2; cnt -= 2)
			mci_writew(host, DATA(host->data_offset), *pdata++);
		buf = pdata;
	}
	/* put anything remaining in the part_buf */
	if (cnt) {
		tbb = mci_readl(host, TBBCNT);
		dw_mci_set_part_bytes(host, buf, cnt);
		if (tbb + cnt == host->bytcnt)
			mci_writew(host, DATA(host->data_offset),
					host->part_buf16);
	}
}

static void dw_mci_pull_data16(struct dw_mci *host, void *buf, int cnt)
{
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (unlikely((unsigned long)buf & 0x1)) {
		while (cnt >= 2) {
			/* pull data from fifo into aligned buffer */
			u16 aligned_buf[64];
			int len = min(cnt & -2, (int)sizeof(aligned_buf));
			int items = len >> 1;
			int i;
			for (i = 0; i < items; ++i)
				aligned_buf[i] = mci_readw(host,
						DATA(host->data_offset));
			/* memcpy from aligned buffer into output buffer */
			memcpy(buf, aligned_buf, len);
			buf += len;
			cnt -= len;
		}
	} else
#endif
	{
		u16 *pdata = buf;
		for (; cnt >= 2; cnt -= 2)
			*pdata++ = mci_readw(host, DATA(host->data_offset));
		buf = pdata;
	}
	if (cnt) {
		host->part_buf16 = mci_readw(host, DATA(host->data_offset));
		dw_mci_pull_final_bytes(host, buf, cnt);
	}
}

static void dw_mci_push_data32(struct dw_mci *host, void *buf, int cnt)
{
	u32 tbb;

	/* try and push anything in the part_buf */
	if (unlikely(host->part_buf_count)) {
		int len = dw_mci_push_part_bytes(host, buf, cnt);
		tbb = mci_readl(host, TBBCNT);
		buf += len;
		cnt -= len;
		if (tbb + len == host->bytcnt || host->part_buf_count == 4) {
			mci_writel(host, DATA(host->data_offset),
					host->part_buf32);
			host->part_buf_count = 0;
		}
	}
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (unlikely((unsigned long)buf & 0x3)) {
		while (cnt >= 4) {
			u32 aligned_buf[32];
			int len = min(cnt & -4, (int)sizeof(aligned_buf));
			int items = len >> 2;
			int i;
			/* memcpy from input buffer into aligned buffer */
			memcpy(aligned_buf, buf, len);
			buf += len;
			cnt -= len;
			/* push data from aligned buffer into fifo */
			for (i = 0; i < items; ++i)
				mci_writel(host, DATA(host->data_offset),
						aligned_buf[i]);
		}
	} else
#endif
	{
		u32 *pdata = buf;
		for (; cnt >= 4; cnt -= 4)
			mci_writel(host, DATA(host->data_offset), *pdata++);
		buf = pdata;
	}
	/* put anything remaining in the part_buf */
	if (cnt) {
		tbb = mci_readl(host, TBBCNT);
		dw_mci_set_part_bytes(host, buf, cnt);
		if (tbb + cnt == host->bytcnt)
			mci_writel(host, DATA(host->data_offset),
						host->part_buf32);
	}
}

static void dw_mci_pull_data32(struct dw_mci *host, void *buf, int cnt)
{
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (unlikely((unsigned long)buf & 0x3)) {
		while (cnt >= 4) {
			/* pull data from fifo into aligned buffer */
			u32 aligned_buf[32];
			int len = min(cnt & -4, (int)sizeof(aligned_buf));
			int items = len >> 2;
			int i;
			for (i = 0; i < items; ++i)
				aligned_buf[i] = mci_readl(host,
						DATA(host->data_offset));
			/* memcpy from aligned buffer into output buffer */
			memcpy(buf, aligned_buf, len);
			buf += len;
			cnt -= len;
		}
	} else
#endif
	{
		u32 *pdata = buf;
		for (; cnt >= 4; cnt -= 4)
			*pdata++ = mci_readl(host, DATA(host->data_offset));
		buf = pdata;
	}
	if (cnt) {
		host->part_buf32 = mci_readl(host, DATA(host->data_offset));
		dw_mci_pull_final_bytes(host, buf, cnt);
	}
}

static void dw_mci_push_data64(struct dw_mci *host, void *buf, int cnt)
{
	u32 tbb;

	/* try and push anything in the part_buf */
	if (unlikely(host->part_buf_count)) {
		int len = dw_mci_push_part_bytes(host, buf, cnt);
		tbb = mci_readl(host, TBBCNT);
		buf += len;
		cnt -= len;
		if (tbb + len == host->bytcnt || host->part_buf_count == 8) {
			mci_writeq(host, DATA(host->data_offset),
					host->part_buf);
			host->part_buf_count = 0;
		}
	}
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (unlikely((unsigned long)buf & 0x7)) {
		while (cnt >= 8) {
			u64 aligned_buf[16];
			int len = min(cnt & -8, (int)sizeof(aligned_buf));
			int items = len >> 3;
			int i;
			/* memcpy from input buffer into aligned buffer */
			memcpy(aligned_buf, buf, len);
			buf += len;
			cnt -= len;
			/* push data from aligned buffer into fifo */
			for (i = 0; i < items; ++i)
				mci_writeq(host, DATA(host->data_offset),
						aligned_buf[i]);
		}
	} else
#endif
	{
		u64 *pdata = buf;
		for (; cnt >= 8; cnt -= 8)
			mci_writeq(host, DATA(host->data_offset), *pdata++);
		buf = pdata;
	}
	/* put anything remaining in the part_buf */
	if (cnt) {
		tbb = mci_readl(host, TBBCNT);
		dw_mci_set_part_bytes(host, buf, cnt);
		if (tbb + cnt == host->bytcnt)
			mci_writeq(host, DATA(host->data_offset),
					host->part_buf);
	}
}

static void dw_mci_pull_data64(struct dw_mci *host, void *buf, int cnt)
{
#ifndef CONFIG_HAVE_EFFICIENT_UNALIGNED_ACCESS
	if (unlikely((unsigned long)buf & 0x7)) {
		while (cnt >= 8) {
			/* pull data from fifo into aligned buffer */
			u64 aligned_buf[16];
			int len = min(cnt & -8, (int)sizeof(aligned_buf));
			int items = len >> 3;
			int i;
			for (i = 0; i < items; ++i)
				aligned_buf[i] = mci_readq(host,
						DATA(host->data_offset));
			/* memcpy from aligned buffer into output buffer */
			memcpy(buf, aligned_buf, len);
			buf += len;
			cnt -= len;
		}
	} else
#endif
	{
		u64 *pdata = buf;
		for (; cnt >= 8; cnt -= 8)
			*pdata++ = mci_readq(host, DATA(host->data_offset));
		buf = pdata;
	}
	if (cnt) {
		host->part_buf = mci_readq(host, DATA(host->data_offset));
		dw_mci_pull_final_bytes(host, buf, cnt);
	}
}

static void dw_mci_pull_data(struct dw_mci *host, void *buf, int cnt)
{
	int len;

	/* get remaining partial bytes */
	len = dw_mci_pull_part_bytes(host, buf, cnt);
	if (unlikely(len == cnt))
		return;
	buf += len;
	cnt -= len;

	/* get the rest of the data */
	host->pull_data(host, buf, cnt);
}

static void dw_mci_read_data_pio(struct dw_mci *host, u8 int_data_over)
{
	struct sg_mapping_iter *sg_miter = &host->sg_miter;
	void *buf;
	unsigned int offset;
	struct mmc_data	*data = host->data;
	int shift = host->data_shift;
	u32 status;
	unsigned int nbytes = 0, len;
	unsigned int remain, fcnt;
	u32 temp;

	do {
		if (!sg_miter_next(sg_miter))
			goto done;

		host->sg = sg_miter->__sg;
		buf = sg_miter->addr;
		remain = sg_miter->length;
		offset = 0;

		do {
			fcnt = (SDMMC_GET_FCNT(mci_readl(host, STATUS))
					<< shift) + host->part_buf_count;
			len = min(remain, fcnt);
			if (!len)
				break;
			dw_mci_pull_data(host, (void *)(buf + offset), len);
			offset += len;
			nbytes += len;
			remain -= len;
		} while (remain);
		sg_miter->consumed = offset;

		status = mci_readl(host, MINTSTS);
		mci_writel(host, RINTSTS, SDMMC_INT_RXDR);
	/* if the RXDR is ready read again */
	} while ((status & SDMMC_INT_RXDR) ||
			(int_data_over &&
			 SDMMC_GET_FCNT(mci_readl(host, STATUS))));
	data->bytes_xfered += nbytes;

	if (!remain) {
		if (!sg_miter_next(sg_miter))
			goto done;
		sg_miter->consumed = 0;
	}
	sg_miter_stop(sg_miter);
	return;

done:

	/* Disable RX/TX IRQs */
	mci_writel(host, RINTSTS, SDMMC_INT_TXDR | SDMMC_INT_RXDR);
	temp = mci_readl(host, INTMASK);
	temp  &= ~(SDMMC_INT_RXDR | SDMMC_INT_TXDR);
	mci_writel(host, INTMASK, temp);

	data->bytes_xfered += nbytes;
	sg_miter_stop(sg_miter);
	host->sg = NULL;
	smp_wmb();
	set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
}

static void dw_mci_write_data_pio(struct dw_mci *host)
{
	struct sg_mapping_iter *sg_miter = &host->sg_miter;
	void *buf;
	unsigned int offset;
	struct mmc_data	*data = host->data;
	int shift = host->data_shift;
	u32 status;
	unsigned int nbytes = 0, len;
	unsigned int fifo_depth = host->fifo_depth;
	unsigned int remain, fcnt;
	u32 temp;

	do {
		if (!sg_miter_next(sg_miter))
			goto done;

		host->sg = sg_miter->__sg;
		buf = sg_miter->addr;
		remain = sg_miter->length;
		offset = 0;

		do {
			fcnt = ((fifo_depth -
				 SDMMC_GET_FCNT(mci_readl(host, STATUS)))
					<< shift) - host->part_buf_count;
			len = min(remain, fcnt);
			if (!len)
				break;
			host->push_data(host, (void *)(buf + offset), len);
			offset += len;
			nbytes += len;
			remain -= len;
		} while (remain);
		sg_miter->consumed = offset;

		status = mci_readl(host, MINTSTS);
		mci_writel(host, RINTSTS, SDMMC_INT_TXDR);
	} while (status & SDMMC_INT_TXDR); /* if TXDR write again */
	data->bytes_xfered += nbytes;

	if (!remain) {
		if (!sg_miter_next(sg_miter))
			goto done;
		sg_miter->consumed = 0;
	}
	sg_miter_stop(sg_miter);
	return;

done:

	/* Disable RX/TX IRQs */
	mci_writel(host, RINTSTS, SDMMC_INT_TXDR | SDMMC_INT_RXDR);
	temp = mci_readl(host, INTMASK);
	temp  &= ~(SDMMC_INT_RXDR | SDMMC_INT_TXDR);
	mci_writel(host, INTMASK, temp);

	data->bytes_xfered += nbytes;
	sg_miter_stop(sg_miter);
	host->sg = NULL;
	smp_wmb();
	set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
}

static void dw_mci_cmd_interrupt(struct dw_mci *host, u32 status)
{
	if (!host->cmd_status)
		host->cmd_status = status;

	smp_wmb();

	set_bit(EVENT_CMD_COMPLETE, &host->pending_events);
	tasklet_schedule(&host->tasklet);
}

static irqreturn_t dw_mci_interrupt(int irq, void *dev_id)
{
	struct dw_mci *host = dev_id;
	u32 status, pending;
	unsigned int pass_count = 0;
	int i;
	int ret = IRQ_NONE;

	do {
		status = mci_readl(host, RINTSTS);
		pending = mci_readl(host, MINTSTS); /* read-only mask reg */

		/*
		 * DTO fix - version 2.10a and below, and only if internal DMA
		 * is configured.
		 */
		if (host->quirks & DW_MCI_QUIRK_IDMAC_DTO) {
			if (!pending &&
			    ((mci_readl(host, STATUS) >> 17) & 0x1fff))
				pending |= SDMMC_INT_DATA_OVER;
		}

		if (host->quirks & DW_MMC_QUIRK_NO_VOLSW_INT &&
						pending & SDMMC_INT_CMD_DONE) {
			u32 cmd = mci_readl(host, CMD) & 0x3f;
			if (cmd == SD_SWITCH_VOLTAGE &&
				!(mci_readl(host, STATUS) & SDMMC_DATA_BUSY)) {
					pending |= SDMMC_INT_RTO;
			}
		}

		if (host->quirks & DW_MCI_QUIRK_NO_DETECT_EBIT &&
				host->dir_status == DW_MCI_RECV_STATUS) {
			if (status & SDMMC_INT_EBE)
				mci_writel(host, RINTSTS, SDMMC_INT_EBE);
		}

		if (!pending)
			break;

		if (pending & SDMMC_INT_HLE) {
			mci_writel(host, RINTSTS, SDMMC_INT_HLE);
			host->cmd_status = pending;
			tasklet_schedule(&host->tasklet);
			ret = IRQ_HANDLED;
		}

		if (pending & DW_MCI_CMD_ERROR_FLAGS) {
			mci_writel(host, RINTSTS, DW_MCI_CMD_ERROR_FLAGS);
			host->cmd_status = pending;
			smp_wmb();
			set_bit(EVENT_CMD_COMPLETE, &host->pending_events);
			ret = IRQ_HANDLED;
		}

		if (pending & SDMMC_INT_VOLT_SW) {
			u32 cmd = mci_readl(host, CMD) & 0x3f;
			if (cmd == SD_SWITCH_VOLTAGE) {
				mci_writel(host, RINTSTS, SDMMC_INT_VOLT_SW);
				dw_mci_cmd_interrupt(host, pending);
			}
		}

		if (pending & DW_MCI_DATA_ERROR_FLAGS) {
			/* if there is an error report DATA_ERROR */
			mci_writel(host, RINTSTS, DW_MCI_DATA_ERROR_FLAGS);
			host->data_status = pending;
			smp_wmb();
			set_bit(EVENT_DATA_ERROR, &host->pending_events);
			if (pending & SDMMC_INT_SBE)
				set_bit(EVENT_DATA_COMPLETE,
					&host->pending_events);
			tasklet_schedule(&host->tasklet);
			ret = IRQ_HANDLED;
		}

		if (pending & SDMMC_INT_DATA_OVER) {
			mci_writel(host, RINTSTS, SDMMC_INT_DATA_OVER);
			/* for mmc trace */
			mmc_add_trace(__MMC_TA_MMC_DMA_DONE, host->mqrq);
			if (!host->data_status)
				host->data_status = pending;
			smp_wmb();
			if (host->dir_status == DW_MCI_RECV_STATUS) {
				if (host->sg != NULL)
					dw_mci_read_data_pio(host, 1);
			}
			set_bit(EVENT_DATA_COMPLETE, &host->pending_events);
			tasklet_schedule(&host->tasklet);
			ret = IRQ_HANDLED;
		}

		if (pending & SDMMC_INT_RXDR) {
			mci_writel(host, RINTSTS, SDMMC_INT_RXDR);
			if (host->dir_status == DW_MCI_RECV_STATUS && host->sg)
				dw_mci_read_data_pio(host, 0);
			ret = IRQ_HANDLED;
		}

		if (pending & SDMMC_INT_TXDR) {
			mci_writel(host, RINTSTS, SDMMC_INT_TXDR);
			if (host->dir_status == DW_MCI_SEND_STATUS && host->sg)
				dw_mci_write_data_pio(host);
			ret = IRQ_HANDLED;
		}

		if (pending & SDMMC_INT_CMD_DONE) {
			mci_writel(host, RINTSTS, SDMMC_INT_CMD_DONE);
			dw_mci_cmd_interrupt(host, pending);
			ret = IRQ_HANDLED;
		}

		if (pending & SDMMC_INT_CD) {
			mci_writel(host, RINTSTS, SDMMC_INT_CD);
			queue_work(host->card_workqueue, &host->card_work);
		}

		/* Handle SDIO Interrupts */
		for (i = 0; i < host->num_slots; i++) {
			struct dw_mci_slot *slot = host->slot[i];
			if (pending & SDMMC_INT_SDIO(i)) {
				mci_writel(host, RINTSTS, SDMMC_INT_SDIO(i));
				mmc_signal_sdio_irq(slot->mmc);
				ret = IRQ_HANDLED;
			}
		}

	} while (pass_count++ < 5);

#ifdef CONFIG_MMC_DW_IDMAC
	/* Handle DMA interrupts */
	pending = mci_readl(host, IDSTS);
	if (pending & (SDMMC_IDMAC_INT_TI | SDMMC_IDMAC_INT_RI)) {
		mci_writel(host, IDSTS, SDMMC_IDMAC_INT_TI | SDMMC_IDMAC_INT_RI);
		mci_writel(host, IDSTS, SDMMC_IDMAC_INT_NI);
		host->dma_ops->complete(host);
		ret = IRQ_HANDLED;
	}
#endif

	if (ret == IRQ_NONE)
		pr_warn_ratelimited("%s: no interrupts handled, pending %08x %08x\n",
				dev_name(&host->dev),
				mci_readl(host, MINTSTS),
				mci_readl(host, IDSTS));

	return ret;
}

static void dw_mci_timeout_timer(unsigned long data)
{
	struct dw_mci *host = (struct dw_mci *)data;
	struct mmc_request *mrq;

	if (host && host->mrq) {
		mrq = host->mrq;

		spin_lock(&host->lock);

		host->sg = NULL;
		host->data = NULL;
		host->cmd = NULL;

		switch (host->state) {
		case STATE_IDLE:
			break;
		case STATE_SENDING_CMD:
			mrq->cmd->error = -ENOMEDIUM;
			if (!mrq->data)
				break;
			/* fall through */
		case STATE_SENDING_DATA:
			mrq->data->error = -ENOMEDIUM;
			dw_mci_stop_dma(host);
			break;
		case STATE_DATA_BUSY:
		case STATE_DATA_ERROR:
			if (mrq->data->error == -EINPROGRESS)
				mrq->data->error = -ENOMEDIUM;
			/* fall through */
		case STATE_SENDING_STOP:
			if (mrq->stop)
				mrq->stop->error = -ENOMEDIUM;
			break;
		}

		spin_unlock(&host->lock);
		dev_err(&host->dev,
			"Timeout waiting for hardware interrupt."
			" state = %d\n", host->state);
		dw_mci_reg_dump(host);
		dw_mci_fifo_reset(&host->dev, host);
		dw_mci_ciu_reset(&host->dev, host);
		spin_lock(&host->lock);

		dw_mci_request_end(host, mrq);
		spin_unlock(&host->lock);
	}
}

static void dw_mci_tp_mon(struct work_struct *work)
{
	struct dw_mci *host = container_of(work, struct dw_mci, tp_mon.work);
	struct dw_mci_mon_table *tp_tbl = host->pdata->tp_mon_tbl;
	s32 mif_lock_value = 0;
	s32 cpu_lock_value = 0;

	while (tp_tbl->range) {
		if (host->transferred_cnt > tp_tbl->range)
			break;
		tp_tbl++;
	}

	mif_lock_value = tp_tbl->mif_lock_value;
	cpu_lock_value = tp_tbl->cpu_lock_value;

	dev_dbg(&host->dev, "%d byte/s cnt=%d mif=%d cpu=%d\n",
						host->transferred_cnt,
						host->cmd_cnt,
						mif_lock_value,
						cpu_lock_value);

	pm_qos_update_request_timeout(&host->pm_qos_mif,
					mif_lock_value, 2000000);
	pm_qos_update_request_timeout(&host->pm_qos_cpu,
					cpu_lock_value, 2000000);

	host->transferred_cnt = 0;
	host->cmd_cnt = 0;
	schedule_delayed_work(&host->tp_mon, HZ);
}

static void dw_mci_work_routine_card(struct work_struct *work)
{
	struct dw_mci *host = container_of(work, struct dw_mci, card_work);
	int i;

	for (i = 0; i < host->num_slots; i++) {
		struct dw_mci_slot *slot = host->slot[i];
		struct mmc_host *mmc = slot->mmc;
		struct mmc_request *mrq;
		int present;

		present = dw_mci_get_cd(mmc);
		while (present != slot->last_detect_state) {
			dev_dbg(&slot->mmc->class_dev, "card %s\n",
				present ? "inserted" : "removed");

			/* Power up slot (before spin_lock, may sleep) */
			if (present != 0 && host->pdata->setpower)
				host->pdata->setpower(slot->id, mmc->ocr_avail);

			spin_lock_bh(&host->lock);

			/* Card change detected */
			slot->last_detect_state = present;

			/* Mark card as present if applicable */
			if (present != 0) {
				if (host->pdata->setpower)
					host->pdata->setpower(slot->id,
							      mmc->ocr_avail);
				set_bit(DW_MMC_CARD_PRESENT, &slot->flags);
			}

			/* Clean up queue if present */
			mrq = slot->mrq;
			if (mrq) {
				if (mrq == host->mrq) {
					host->data = NULL;
					host->cmd = NULL;

					switch (host->state) {
					case STATE_IDLE:
						break;
					case STATE_SENDING_CMD:
						mrq->cmd->error = -ENOMEDIUM;
						if (!mrq->data)
							break;
						/* fall through */
					case STATE_SENDING_DATA:
						mrq->data->error = -ENOMEDIUM;
						dw_mci_stop_dma(host);
						break;
					case STATE_DATA_BUSY:
					case STATE_DATA_ERROR:
						if (mrq->data->error == -EINPROGRESS)
							mrq->data->error = -ENOMEDIUM;
						/* fall through */
					case STATE_SENDING_STOP:
						if (mrq->stop)
							mrq->stop->error = -ENOMEDIUM;
						break;
					}

					dw_mci_request_end(host, mrq);
				} else {
					list_del(&slot->queue_node);
					mrq->cmd->error = -ENOMEDIUM;
					if (mrq->data)
						mrq->data->error = -ENOMEDIUM;
					if (mrq->stop)
						mrq->stop->error = -ENOMEDIUM;

					del_timer(&host->timer);
					spin_unlock(&host->lock);
					dw_mci_qos_put(host);
					mmc_request_done(slot->mmc, mrq);
					spin_lock(&host->lock);
				}
			}

			/* Power down slot */
			if (present == 0) {
				if (host->pdata->setpower)
					host->pdata->setpower(slot->id, 0);
				clear_bit(DW_MMC_CARD_PRESENT, &slot->flags);

				/*
				 * Clear down the FIFO - doing so generates a
				 * block interrupt, hence setting the
				 * scatter-gather pointer to NULL.
				 */
				sg_miter_stop(&host->sg_miter);
				host->sg = NULL;

				spin_lock(&host->cclk_lock);
				if (!atomic_read(&host->cclk_cnt)) {
					dw_mci_ciu_clk_en(host);
					dw_mci_fifo_reset(&host->dev, host);
					dw_mci_ciu_reset(&host->dev, host);
					dw_mci_ciu_clk_dis(host);
				} else {
					dw_mci_fifo_reset(&host->dev, host);
					dw_mci_ciu_reset(&host->dev, host);
				}
				spin_unlock(&host->cclk_lock);
#ifdef CONFIG_MMC_DW_IDMAC
				dw_mci_idma_reset_dma(host);
#endif

			} else if (host->cur_slot) {
				spin_lock(&host->cclk_lock);
				if (!atomic_read(&host->cclk_cnt)) {
					dw_mci_ciu_clk_en(host);
					dw_mci_ciu_reset(&host->dev, host);
					dw_mci_ciu_clk_dis(host);
				} else
					dw_mci_ciu_reset(&host->dev, host);
				spin_unlock(&host->cclk_lock);
			}

			spin_unlock_bh(&host->lock);

			/* Power down slot (after spin_unlock, may sleep) */
			if (present == 0 && host->pdata->setpower)
				host->pdata->setpower(slot->id, 0);

			present = dw_mci_get_cd(mmc);
		}

		mmc_detect_change(slot->mmc,
			msecs_to_jiffies(host->pdata->detect_delay_ms));
	}
}

static void dw_mci_notify_change(struct platform_device *dev, int state)
{
	struct dw_mci *host = platform_get_drvdata(dev);
	unsigned long flags;

	if (host) {
		spin_lock_irqsave(&host->lock, flags);
		if (state) {
			dev_dbg(&dev->dev, "card inserted.\n");
			host->pdata->quirks |= DW_MCI_QUIRK_BROKEN_CARD_DETECTION;
		} else {
			dev_dbg(&dev->dev, "card removed.\n");
			host->pdata->quirks &= ~DW_MCI_QUIRK_BROKEN_CARD_DETECTION;
		}
		queue_work(host->card_workqueue, &host->card_work);
		spin_unlock_irqrestore(&host->lock, flags);
	}
}

static irqreturn_t dw_mci_detect_interrupt(int irq, void *dev_id)
{
	struct dw_mci *host = dev_id;

	queue_work(host->card_workqueue, &host->card_work);

	return IRQ_HANDLED;
}

static int __devinit dw_mci_init_slot(struct dw_mci *host, unsigned int id)
{
	struct mmc_host *mmc;
	struct dw_mci_slot *slot;

	mmc = mmc_alloc_host(sizeof(struct dw_mci_slot), &host->dev);
	if (!mmc)
		return -ENOMEM;

	slot = mmc_priv(mmc);
	slot->id = id;
#ifdef CONFIG_MMC_CLKGATE
	mmc->clkgate_delay = 10;
#endif
	slot->mmc = mmc;
	slot->host = host;

	mmc->ops = &dw_mci_ops;
	mmc->f_min = DIV_ROUND_UP(host->bus_hz, 510);
	mmc->f_max = host->bus_hz;

	if (host->pdata->get_ocr)
		mmc->ocr_avail = host->pdata->get_ocr(id);
	else
		mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;

	/*
	 * Start with slot power disabled, it will be enabled when a card
	 * is detected.
	 */
	if (host->pdata->setpower)
		host->pdata->setpower(id, 0);

	if (host->pdata->caps)
		mmc->caps = host->pdata->caps;
	else
		mmc->caps = 0;

	if (host->pdata->caps2)
		mmc->caps2 = host->pdata->caps2;
	else
		mmc->caps2 = 0;

	if (host->pdata->pm_caps) {
		mmc->pm_caps |= host->pdata->pm_caps;
		mmc->pm_flags = mmc->pm_caps;
	}

	if (host->pdata->get_bus_wd) {
		unsigned int bus_width = host->pdata->get_bus_wd(slot->id);
		switch (bus_width) {
		case 8:
			mmc->caps |= MMC_CAP_8_BIT_DATA;
			/* fall through */
		case 4:
			mmc->caps |= MMC_CAP_4_BIT_DATA;
			/* fall through */
		default:
			if (host->pdata->cfg_gpio)
				host->pdata->cfg_gpio(bus_width);
			break;
		}
	}

	if (host->pdata->quirks & DW_MCI_QUIRK_HIGHSPEED)
		mmc->caps |= MMC_CAP_SD_HIGHSPEED | MMC_CAP_MMC_HIGHSPEED;

	if (mmc->caps2 & MMC_CAP2_POWEROFF_NOTIFY)
		mmc->power_notify_type = MMC_HOST_PW_NOTIFY_SHORT;
	else
		mmc->power_notify_type = MMC_HOST_PW_NOTIFY_NONE;

	if (host->pdata->blk_settings) {
		mmc->max_segs = host->pdata->blk_settings->max_segs;
		mmc->max_blk_size = host->pdata->blk_settings->max_blk_size;
		mmc->max_blk_count = host->pdata->blk_settings->max_blk_count;
		mmc->max_req_size = host->pdata->blk_settings->max_req_size;
		mmc->max_seg_size = host->pdata->blk_settings->max_seg_size;
	} else {
		/* Useful defaults if platform data is unset. */
#ifdef CONFIG_MMC_DW_IDMAC
		mmc->max_segs = host->ring_size;
		mmc->max_blk_size = 65536;
		mmc->max_seg_size = 0x1000;
		mmc->max_req_size = mmc->max_seg_size * host->ring_size;
		mmc->max_blk_count = mmc->max_req_size / 512;
#else
		mmc->max_segs = 64;
		mmc->max_blk_size = 65536; /* BLKSIZ is 16 bits */
		mmc->max_blk_count = 512;
		mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
		mmc->max_seg_size = mmc->max_req_size;
#endif /* CONFIG_MMC_DW_IDMAC */
	}

	if (host->align_size)
		mmc->align_size = host->align_size;

	host->vmmc = regulator_get(mmc_dev(mmc), "vmmc");
	if (IS_ERR(host->vmmc)) {
		dev_info(&host->dev, "no vmmc regulator found\n");
		host->vmmc = NULL;
	} else {
		dev_info(&host->dev, "vmmc regulator found\n");
		regulator_enable(host->vmmc);
	}

	host->vqmmc = regulator_get(mmc_dev(mmc), "vqmmc");
	if (IS_ERR(host->vqmmc)) {
		dev_info(&host->dev, "no vqmmc regulator found\n");
		host->vqmmc = NULL;
	} else {
		dev_info(&host->dev, "vqmmc regulator found\n");
		regulator_enable(host->vqmmc);
	}

	host->pdata->init(id, dw_mci_detect_interrupt, host);

	if (dw_mci_get_cd(mmc))
		set_bit(DW_MMC_CARD_PRESENT, &slot->flags);
	else
		clear_bit(DW_MMC_CARD_PRESENT, &slot->flags);

	host->slot[id] = slot;
	mmc_add_host(mmc);

#if defined(CONFIG_DEBUG_FS)
	dw_mci_init_debugfs(slot);
#endif

	/* Card initially undetected */
	slot->last_detect_state = 0;

	/*
	 * Card may have been plugged in prior to boot so we
	 * need to run the detect tasklet
	 */
	queue_work(host->card_workqueue, &host->card_work);

	return 0;
}

static void dw_mci_cleanup_slot(struct dw_mci_slot *slot, unsigned int id)
{
	/* Shutdown detect IRQ */
	if (slot->host->pdata->exit)
		slot->host->pdata->exit(id);

	/* Debugfs stuff is cleaned up by mmc core */
	mmc_remove_host(slot->mmc);
	slot->host->slot[id] = NULL;
	mmc_free_host(slot->mmc);
}

static void dw_mci_init_dma(struct dw_mci *host)
{
	if (host->pdata->desc_sz)
		host->desc_sz = host->pdata->desc_sz;
	else
		host->desc_sz = 1;

	/* Alloc memory for sg translation */
	host->sg_cpu = dma_alloc_coherent(&host->dev, host->desc_sz * PAGE_SIZE,
					  &host->sg_dma, GFP_KERNEL);
	if (!host->sg_cpu) {
		dev_err(&host->dev, "%s: could not alloc DMA memory\n",
			__func__);
		goto no_dma;
	}

	/* Determine which DMA interface to use */
#ifdef CONFIG_MMC_DW_IDMAC
	host->dma_ops = &dw_mci_idmac_ops;
	dev_info(&host->dev, "Using internal DMA controller.\n");
#endif

	if (!host->dma_ops)
		goto no_dma;

	if (host->dma_ops->init && host->dma_ops->start &&
	    host->dma_ops->stop && host->dma_ops->cleanup) {
		if (host->dma_ops->init(host)) {
			dev_err(&host->dev, "%s: Unable to initialize "
				"DMA Controller.\n", __func__);
			goto no_dma;
		}
	} else {
		dev_err(&host->dev, "DMA initialization not found.\n");
		goto no_dma;
	}

	host->use_dma = 1;
	return;

no_dma:
	dev_info(&host->dev, "Using PIO mode.\n");
	host->use_dma = 0;
	return;
}

static bool mci_wait_reset(struct device *dev, struct dw_mci *host)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(500);
	unsigned int ctrl;

	mci_writel(host, CTRL, (SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET |
				SDMMC_CTRL_DMA_RESET));

	/* wait till resets clear */
	do {
		ctrl = mci_readl(host, CTRL);
		if (!(ctrl & (SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET |
			      SDMMC_CTRL_DMA_RESET)))
			return true;
	} while (time_before(jiffies, timeout));

	dev_err(dev, "Timeout resetting block (ctrl %#x)\n", ctrl);

	return false;
}

int __devinit dw_mci_probe(struct dw_mci *host)
{
	int width, i, ret = 0;
	u32 fifo_size;

	if (!host->pdata || !host->pdata->init) {
		dev_err(&host->dev,
			"Platform data must supply init function\n");
		return -ENODEV;
	}

	if (!host->pdata->select_slot && host->pdata->num_slots > 1) {
		dev_err(&host->dev,
			"Platform data must supply select_slot function\n");
		return -ENODEV;
	}

	if (!host->pdata->bus_hz) {
		dev_err(&host->dev,
			"Platform data must supply bus speed\n");
		return -ENODEV;
	}

	host->hclk = clk_get(&host->dev, host->pdata->hclk_name);
	if (IS_ERR(host->hclk)) {
		dev_err(&host->dev,
				"failed to get hclk\n");
		ret = PTR_ERR(host->hclk);
		goto err_freehost;
	}
	dw_mci_biu_clk_en(host);

	host->cclk = clk_get(&host->dev, host->pdata->cclk_name);
	if (IS_ERR(host->cclk)) {
		dev_err(&host->dev,
				"failed to get cclk\n");
		ret = PTR_ERR(host->cclk);
		goto err_free_hclk;
	}
	dw_mci_ciu_clk_en(host);

	host->bus_hz = host->pdata->bus_hz;
	host->quirks = host->pdata->quirks;

	spin_lock_init(&host->lock);
	spin_lock_init(&host->cclk_lock);
	INIT_LIST_HEAD(&host->queue);

	/*
	 * Get the host data width - this assumes that HCON has been set with
	 * the correct values.
	 */
	i = (mci_readl(host, HCON) >> 7) & 0x7;
	if (!i) {
		host->push_data = dw_mci_push_data16;
		host->pull_data = dw_mci_pull_data16;
		width = 16;
		host->data_shift = 1;
	} else if (i == 2) {
		host->push_data = dw_mci_push_data64;
		host->pull_data = dw_mci_pull_data64;
		width = 64;
		host->data_shift = 3;
	} else {
		/* Check for a reserved value, and warn if it is */
		WARN((i != 1),
		     "HCON reports a reserved host data width!\n"
		     "Defaulting to 32-bit access.\n");
		host->push_data = dw_mci_push_data32;
		host->pull_data = dw_mci_pull_data32;
		width = 32;
		host->data_shift = 2;
	}

	/* Reset all blocks */
	if (!mci_wait_reset(&host->dev, host)) {
		ret = -ENODEV;
		goto err_free_cclk;
	}

	host->dma_ops = host->pdata->dma_ops;
	dw_mci_init_dma(host);

	/* Clear the interrupts for the host controller */
	mci_writel(host, RINTSTS, 0xFFFFFFFF);
	mci_writel(host, INTMASK, 0); /* disable all mmc interrupt first */

	/* Put in max timeout */
	mci_writel(host, TMOUT, 0xFFFFFFFF);

	/*
	 * FIFO threshold settings  RxMark  = fifo_size / 2 - 1,
	 *                          Tx Mark = fifo_size / 2 DMA Size = 8
	 */
	if (!host->pdata->fifo_depth) {
		/*
		 * Power-on value of RX_WMark is FIFO_DEPTH-1, but this may
		 * have been overwritten by the bootloader, just like we're
		 * about to do, so if you know the value for your hardware, you
		 * should put it in the platform data.
		 */
		fifo_size = mci_readl(host, FIFOTH);
		fifo_size = 1 + ((fifo_size >> 16) & 0xfff);
	} else {
		fifo_size = host->pdata->fifo_depth;
	}
	host->fifo_depth = fifo_size;
	host->fifoth_val = ((0x4 << 28) | ((fifo_size/2 - 1) << 16) |
			((fifo_size/2) << 0));
	mci_writel(host, FIFOTH, host->fifoth_val);

	/* disable clock to CIU */
	mci_writel(host, CLKENA, 0);
	mci_writel(host, CLKSRC, 0);

	tasklet_init(&host->tasklet, dw_mci_tasklet_func, (unsigned long)host);
	host->card_workqueue = alloc_workqueue("dw-mci-card",
			WQ_MEM_RECLAIM | WQ_NON_REENTRANT, 1);
	if (!host->card_workqueue)
		goto err_dmaunmap;
	INIT_WORK(&host->card_work, dw_mci_work_routine_card);

	dw_mci_qos_init(host);
	if (host->pdata->tp_mon_tbl) {
		if (!host->pdata->ttp_enabled)
			INIT_DELAYED_WORK(&host->tp_mon, dw_mci_tp_mon);
		pm_qos_add_request(&host->pm_qos_mif,
					PM_QOS_BUS_THROUGHPUT, 0);
		pm_qos_add_request(&host->pm_qos_cpu,
					PM_QOS_CPU_FREQ_MIN, 0);
	}
	setup_timer(&host->timer, dw_mci_timeout_timer, (unsigned long)host);
	ret = request_irq(host->irq, dw_mci_interrupt, host->irq_flags, "dw-mci", host);
	if (ret)
		goto err_workqueue;

	if (host->pdata->num_slots)
		host->num_slots = host->pdata->num_slots;
	else
		host->num_slots = ((mci_readl(host, HCON) >> 1) & 0x1F) + 1;

	/* We need at least one slot to succeed */
	for (i = 0; i < host->num_slots; i++) {
		ret = dw_mci_init_slot(host, i);
		if (ret) {
			ret = -ENODEV;
			goto err_init_slot;
		}
	}

	/*
	 * In 2.40a spec, Data offset is changed.
	 * Need to check the version-id and set data-offset for DATA register.
	 */
	host->verid = SDMMC_GET_VERID(mci_readl(host, VERID));
	dev_info(&host->dev, "Version ID is %04x\n", host->verid);

	if (host->verid < DW_MMC_240A)
		host->data_offset = DATA_OFFSET;
	else
		host->data_offset = DATA_240A_OFFSET;

	if (host->pdata->cd_type == DW_MCI_CD_EXTERNAL)
		host->pdata->ext_cd_init(&dw_mci_notify_change);

	/*
	 * Enable interrupts for command done, data over, data empty, card det,
	 * receive ready and error such as transmit, receive timeout, crc error
	 */
	mci_writel(host, RINTSTS, 0xFFFFFFFF);
	if (host->pdata->cd_type == DW_MCI_CD_INTERNAL)
		mci_writel(host, INTMASK, SDMMC_INT_CMD_DONE |
		   SDMMC_INT_DATA_OVER | SDMMC_INT_TXDR | SDMMC_INT_RXDR |
		   DW_MCI_ERROR_FLAGS | SDMMC_INT_CD);
	else
		mci_writel(host, INTMASK, SDMMC_INT_CMD_DONE |
		   SDMMC_INT_DATA_OVER | SDMMC_INT_TXDR | SDMMC_INT_RXDR |
		   DW_MCI_ERROR_FLAGS);
	mci_writel(host, CTRL, SDMMC_CTRL_INT_ENABLE); /* Enable mci interrupt */

	dev_info(&host->dev, "DW MMC controller at irq %d, "
		 "%d bit host data width, "
		 "%u deep fifo\n",
		 host->irq, width, fifo_size);
	if (host->quirks & DW_MCI_QUIRK_IDMAC_DTO)
		dev_info(&host->dev, "Internal DMAC interrupt fix enabled.\n");

	return 0;

err_init_slot:
	/* De-init any initialized slots */
	while (i > 0) {
		if (host->slot[i])
			dw_mci_cleanup_slot(host->slot[i], i);
		i--;
	}
	free_irq(host->irq, host);

err_workqueue:
	destroy_workqueue(host->card_workqueue);
	dw_mci_qos_exit(host);
	if (host->pdata->tp_mon_tbl) {
		pm_qos_remove_request(&host->pm_qos_mif);
		pm_qos_remove_request(&host->pm_qos_cpu);
	}

err_dmaunmap:
	if (host->use_dma && host->dma_ops->exit)
		host->dma_ops->exit(host);
	dma_free_coherent(&host->dev, host->desc_sz * PAGE_SIZE,
			  host->sg_cpu, host->sg_dma);

	if (host->vmmc) {
		regulator_disable(host->vmmc);
		regulator_put(host->vmmc);
	}

err_free_cclk:
	dw_mci_ciu_clk_dis(host);
	clk_put(host->cclk);

err_free_hclk:
	dw_mci_biu_clk_dis(host);
	clk_put(host->hclk);

err_freehost:
	kfree(host);
	return ret;
}
EXPORT_SYMBOL(dw_mci_probe);

void dw_mci_remove(struct dw_mci *host)
{
	int i;

	mci_writel(host, RINTSTS, 0xFFFFFFFF);
	mci_writel(host, INTMASK, 0); /* disable all mmc interrupt first */

	if (host->pdata->cd_type == DW_MCI_CD_EXTERNAL)
		host->pdata->ext_cd_cleanup(&dw_mci_notify_change);

	for (i = 0; i < host->num_slots; i++) {
		dev_dbg(&host->dev, "remove slot %d\n", i);
		if (host->slot[i])
			dw_mci_cleanup_slot(host->slot[i], i);
	}

	/* disable clock to CIU */
	mci_writel(host, CLKENA, 0);
	mci_writel(host, CLKSRC, 0);

	free_irq(host->irq, host);
	del_timer_sync(&host->timer);
	destroy_workqueue(host->card_workqueue);

	dw_mci_qos_exit(host);
	if (host->pdata->tp_mon_tbl) {
		if (!host->pdata->ttp_enabled)
			cancel_delayed_work_sync(&host->tp_mon);
		pm_qos_remove_request(&host->pm_qos_mif);
		pm_qos_remove_request(&host->pm_qos_cpu);
	}
	dma_free_coherent(&host->dev, host->desc_sz * PAGE_SIZE,
			host->sg_cpu, host->sg_dma);

	if (host->use_dma && host->dma_ops->exit)
		host->dma_ops->exit(host);

	if (host->vmmc) {
		regulator_disable(host->vmmc);
		regulator_put(host->vmmc);
	}

	clk_put(host->cclk);
	dw_mci_biu_clk_dis(host);
	clk_put(host->hclk);
}
EXPORT_SYMBOL(dw_mci_remove);



#ifdef CONFIG_PM_SLEEP
/*
 * TODO: we should probably disable the clock to the card in the suspend path.
 */
int dw_mci_suspend(struct dw_mci *host)
{
	int i, ret = 0;
	u32 clkena;

	for (i = 0; i < host->num_slots; i++) {
		struct dw_mci_slot *slot = host->slot[i];
		if (!slot)
			continue;
		if (slot->mmc) {
//#if defined(CONFIG_MACH_JA_KOR_SKT) || defined(CONFIG_MACH_JA_KOR_KT) || defined(CONFIG_MACH_JA_KOR_LGT)
//			int cclk_disabled;
//			spin_lock(&host->cclk_lock);
//			cclk_disabled = !atomic_read(&host->cclk_cnt);
//			if (cclk_disabled)
//			dw_mci_ciu_clk_en(host);
//#else
			dw_mci_ciu_clk_en(host);
			clkena = mci_readl(host, CLKENA);
			clkena &= ~((SDMMC_CLKEN_LOW_PWR) << slot->id);
			mci_writel(host, CLKENA, clkena);
			mci_send_cmd(slot,
				SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);

			slot->mmc->pm_flags |= slot->mmc->pm_caps;
			ret = mmc_suspend_host(slot->mmc);
			if (ret < 0) {
				while (--i >= 0) {
					slot = host->slot[i];
					if (slot)
						mmc_resume_host(host->slot[i]->mmc);
				}
				return ret;
			}
		}
	}

	if (host->pdata->tp_mon_tbl &&
		(host->pdata->pm_caps & MMC_PM_KEEP_POWER)) {
		if (!host->pdata->ttp_enabled) {
			cancel_delayed_work_sync(&host->tp_mon);
			host->transferred_cnt = 0;
			host->cmd_cnt = 0;
		} else {
			host->pm_qos_time = 0;
			host->sync_pre_cnt = 0;
			host->pm_qos_step = 0;
		}
		pm_qos_update_request(&host->pm_qos_mif, 0);
		pm_qos_update_request(&host->pm_qos_cpu, 0);
	}
	if (host->vmmc)
		regulator_disable(host->vmmc);

	return 0;
}
EXPORT_SYMBOL(dw_mci_suspend);

int dw_mci_resume(struct dw_mci *host)
{
	int i, ret;

	if (host->vmmc)
		regulator_enable(host->vmmc);

//#if defined(CONFIG_MACH_JA_KOR_SKT) || defined(CONFIG_MACH_JA_KOR_KT) || defined(CONFIG_MACH_JA_KOR_LGT)
//	spin_lock(&host->cclk_lock);
//	cclk_disabled = !atomic_read(&host->cclk_cnt);
//	if (cclk_disabled)
//		dw_mci_ciu_clk_en(host);
//#else
	dw_mci_ciu_clk_en(host);
//#endif

	if (!mci_wait_reset(&host->dev, host)) {
		ret = -ENODEV;
		return ret;
	}

	if (host->dma_ops->init)
		host->dma_ops->init(host);

	/* Restore the old value at FIFOTH register */
	mci_writel(host, FIFOTH, host->fifoth_val);

	mci_writel(host, RINTSTS, 0xFFFFFFFF);
	if (host->pdata->cd_type == DW_MCI_CD_INTERNAL)
		mci_writel(host, INTMASK, SDMMC_INT_CMD_DONE |
		   SDMMC_INT_DATA_OVER | SDMMC_INT_TXDR | SDMMC_INT_RXDR |
		   DW_MCI_ERROR_FLAGS | SDMMC_INT_CD);
	else
		mci_writel(host, INTMASK, SDMMC_INT_CMD_DONE |
		   SDMMC_INT_DATA_OVER | SDMMC_INT_TXDR | SDMMC_INT_RXDR |
		   DW_MCI_ERROR_FLAGS);
	mci_writel(host, CTRL, SDMMC_CTRL_INT_ENABLE);

	for (i = 0; i < host->num_slots; i++) {
		struct dw_mci_slot *slot = host->slot[i];
		struct mmc_ios ios;
		if (!slot)
			continue;

		if (slot->mmc->pm_flags & MMC_PM_KEEP_POWER &&
					dw_mci_get_cd(slot->mmc)) {
			memcpy(&ios, &slot->mmc->ios,
						sizeof(struct mmc_ios));
			ios.timing = MMC_TIMING_LEGACY;
			dw_mci_set_ios(slot->mmc, &ios);
			dw_mci_set_ios(slot->mmc, &slot->mmc->ios);
			dw_mci_setup_bus(slot, 1);
			if (host->pdata->tuned) {
				dw_mci_set_sampling(host,
						host->pdata->clk_smpl);
				mci_writel(host, CDTHRCTL,
						host->cd_rd_thr << 16 | 1);
			}
		}

		if (dw_mci_get_cd(slot->mmc))
			set_bit(DW_MMC_CARD_PRESENT, &slot->flags);
		else
			clear_bit(DW_MMC_CARD_PRESENT, &slot->flags);

		ret = mmc_resume_host(host->slot[i]->mmc);
		if (ret < 0)
			return ret;
	}

	if (host->pdata->tp_mon_tbl &&
		(host->pdata->pm_caps & MMC_PM_KEEP_POWER)) {
		if (!host->pdata->ttp_enabled) {
			host->transferred_cnt = 0;
			host->cmd_cnt = 0;
			schedule_delayed_work(&host->tp_mon, HZ);
		} else {
			host->pm_qos_time = 0;
			host->sync_pre_cnt = 0;
			host->pm_qos_step = 0;
		}
	}

	return 0;
}
EXPORT_SYMBOL(dw_mci_resume);
void dw_mci_shutdown(struct dw_mci *host)
{
	int i;

	for (i = 0; i < host->num_slots; i++) {
		struct dw_mci_slot *slot = host->slot[i];
		if (!slot)
			continue;

		mmc_poweroff_notify(slot->mmc);
	}

	if (host->pdata->cfg_gpio)
		host->pdata->cfg_gpio(0);
}
EXPORT_SYMBOL(dw_mci_shutdown);
#endif /* CONFIG_PM_SLEEP */

static int __init dw_mci_init(void)
{
	printk(KERN_INFO "Synopsys Designware Multimedia Card Interface Driver");
	return 0;
}

static void __exit dw_mci_exit(void)
{
}

module_init(dw_mci_init);
module_exit(dw_mci_exit);

MODULE_DESCRIPTION("DW Multimedia Card Interface driver");
MODULE_AUTHOR("NXP Semiconductor VietNam");
MODULE_AUTHOR("Imagination Technologies Ltd");
MODULE_LICENSE("GPL v2");
