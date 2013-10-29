/*
 * Rockchip eMMC Interface driver
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
#include <linux/scatterlist.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/bitops.h>

#include <asm/dma.h>
#include <mach/dma-pl330.h>
#include <asm/scatterlist.h>
#include <mach/iomux.h>
#include <mach/board.h>

#include "rkemmc.h"
struct rk29_dma_client mmc_client;

static int rk_mmc_pre_dma_transfer(struct rk_mmc *host,
		   		   struct mmc_data *data,
				   bool next);
#if 0
static int rk_mmc_show_regs(struct rk_mmc *host)
{
	mmc_info(host, "CTRL:    0x%08x\n", mmc_readl(host, CTRL));
	mmc_info(host, "PWREN:   0x%08x\n", mmc_readl(host, PWREN));
	mmc_info(host, "CLKDIV:  0x%08x\n", mmc_readl(host, CLKDIV));
	mmc_info(host, "CLKENA:  0x%08x\n", mmc_readl(host, CLKENA));
	mmc_info(host, "CLKSRC:  0x%08x\n", mmc_readl(host, CLKSRC));
	mmc_info(host, "TMOUT:   0x%08x\n", mmc_readl(host, TMOUT));
	mmc_info(host, "CTYPE:   0x%08x\n", mmc_readl(host, CTYPE));
	mmc_info(host, "BLKSIZ:  0x%08x\n", mmc_readl(host, BLKSIZ));
	mmc_info(host, "BYTCNT:  0x%08x\n", mmc_readl(host, BYTCNT));
	mmc_info(host, "INTMASK: 0x%08x\n", mmc_readl(host, INTMASK));
	mmc_info(host, "CMDARG:  0x%08x\n", mmc_readl(host, CMDARG));
	mmc_info(host, "CMD:     0x%08x\n", mmc_readl(host, CMD));
	mmc_info(host, "RESP0:   0x%08x\n", mmc_readl(host, RESP0));
	mmc_info(host, "RESP1:   0x%08x\n", mmc_readl(host, RESP1));
	mmc_info(host, "RESP2:   0x%08x\n", mmc_readl(host, RESP2));
	mmc_info(host, "RESP3:   0x%08x\n", mmc_readl(host, RESP3));
	mmc_info(host, "MINTSTS: 0x%08x\n", mmc_readl(host, MINTSTS));
	mmc_info(host, "STATUS:  0x%08x\n", mmc_readl(host, STATUS));
	mmc_info(host, "FIFOTH:  0x%08x\n", mmc_readl(host, FIFOTH));
	mmc_info(host, "CDETECT: 0x%08x\n", mmc_readl(host, CDETECT));
	mmc_info(host, "WRTPRT:  0x%08x\n", mmc_readl(host, WRTPRT));
	mmc_info(host, "TCBCNT:  0x%08x\n", mmc_readl(host, TCBCNT));
	mmc_info(host, "TBBCNT:  0x%08x\n", mmc_readl(host, TBBCNT));
	mmc_info(host, "DEBNCE:  0x%08x\n", mmc_readl(host, DEBNCE));
	mmc_info(host, "USRID:   0x%08x\n", mmc_readl(host, USRID));
	mmc_info(host, "VERID:   0x%08x\n", mmc_readl(host, VERID));
	mmc_info(host, "UHS_REG: 0x%08x\n", mmc_readl(host, UHS_REG));
	mmc_info(host, "RST_N:   0x%08x\n", mmc_readl(host, RST_N));

	return 0;
}
#endif
/* Dma operation */
#define MMC_DMA_CHN	DMACH_EMMC
static void dma_callback_func(void *arg, int size, enum rk29_dma_buffresult result)
{
	struct rk_mmc *host  = (struct rk_mmc *)arg;
	
	host->dma_xfer_size += size;
	if (host->data) {
		mmc_dbg(host, "total: %u, xfer: %u\n", host->data->blocks * host->data->blksz, host->dma_xfer_size);
		if(host->dma_xfer_size == host->data->blocks * host->data->blksz){
			set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
			tasklet_schedule(&host->tasklet);
		}
	}

	return;
}
static int dma_init(struct rk_mmc *host)
{
	int res;

	res = rk29_dma_request(MMC_DMA_CHN, &mmc_client, NULL);
	if(res < 0)
		return res;

	res = rk29_dma_config(MMC_DMA_CHN, 4, 16);
	if(res < 0)
		return res;

	res = rk29_dma_set_buffdone_fn(MMC_DMA_CHN, dma_callback_func);

	return res;
}
static void dma_exit(struct rk_mmc *host)
{
	rk29_dma_free(MMC_DMA_CHN, NULL);
}
static int dma_start(struct rk_mmc *host)
{
	int i, res, direction, sg_len;
	enum rk29_dmasrc src;
	struct mmc_data *data = host->data;
	
	BUG_ON(!data);

	host->dma_xfer_size = 0;

	if (data->flags & MMC_DATA_READ){
		direction = DMA_FROM_DEVICE;
		src = RK29_DMASRC_HW;
	}else{
		direction = DMA_TO_DEVICE;
		src = RK29_DMASRC_MEM;
	}

	sg_len = rk_mmc_pre_dma_transfer(host, host->data, 0);
	if(sg_len < 0){
		host->ops->stop(host);
		return sg_len;
	}
	res = rk29_dma_devconfig(MMC_DMA_CHN, src, host->dma_addr);
	if(unlikely(res < 0))
		return res;

	for(i = 0; i < sg_len; i++){
		res = rk29_dma_enqueue(MMC_DMA_CHN, host, 
				sg_dma_address(&data->sg[i]),
				sg_dma_len(&data->sg[i]));
		if(unlikely(res < 0))
			return res;
	}
	res = rk29_dma_ctrl(MMC_DMA_CHN, RK29_DMAOP_START);
	if(unlikely(res < 0))
		return res;

	return res;
}
static int dma_stop(struct rk_mmc *host)
{	
	int res;
	u32 temp;
	
	/* Disable and reset the DMA interface */
	temp = mmc_readl(host, CTRL);
	temp &= ~MMC_CTRL_DMA_ENABLE;
	temp |= MMC_CTRL_DMA_RESET;
	mmc_writel(host, CTRL, temp);

	res = rk29_dma_ctrl(MMC_DMA_CHN, RK29_DMAOP_STOP);
	if(unlikely(res < 0))
		return res;

	rk29_dma_ctrl(MMC_DMA_CHN, RK29_DMAOP_FLUSH);

	return 0;
}
struct rk_mmc_dma_ops dma_ops = {
	.init = dma_init,
	.stop = dma_stop,
	.start = dma_start,
	.exit = dma_exit,
};

#if defined(CONFIG_DEBUG_FS)
static int rk_mmc_req_show(struct seq_file *s, void *v)
{
	struct rk_mmc *host = s->private;
	struct mmc_request *mrq;
	struct mmc_command *cmd;
	struct mmc_command *stop;
	struct mmc_data	*data;

	/* Make sure we get a consistent snapshot */
	spin_lock_bh(&host->lock);
	mrq = host->mrq;

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

	spin_unlock_bh(&host->lock);

	return 0;
}

static int rk_mmc_req_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_mmc_req_show, inode->i_private);
}

static const struct file_operations rk_mmc_req_fops = {
	.owner		= THIS_MODULE,
	.open		= rk_mmc_req_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int rk_mmc_regs_show(struct seq_file *s, void *v)
{
	struct rk_mmc *host = s->private;

	seq_printf(s, "CTRL:    0x%08x\n", mmc_readl(host, CTRL));
	seq_printf(s, "PWREN:   0x%08x\n", mmc_readl(host, PWREN));
	seq_printf(s, "CLKDIV:  0x%08x\n", mmc_readl(host, CLKDIV));
	seq_printf(s, "CLKENA:  0x%08x\n", mmc_readl(host, CLKENA));
	seq_printf(s, "CLKSRC:  0x%08x\n", mmc_readl(host, CLKSRC));
	seq_printf(s, "TMOUT:   0x%08x\n", mmc_readl(host, TMOUT));
	seq_printf(s, "CTYPE:   0x%08x\n", mmc_readl(host, CTYPE));
	seq_printf(s, "BLKSIZ:  0x%08x\n", mmc_readl(host, BLKSIZ));
	seq_printf(s, "BYTCNT:  0x%08x\n", mmc_readl(host, BYTCNT));
	seq_printf(s, "INTMASK: 0x%08x\n", mmc_readl(host, INTMASK));
	seq_printf(s, "CMDARG:  0x%08x\n", mmc_readl(host, CMDARG));
	seq_printf(s, "CMD:     0x%08x\n", mmc_readl(host, CMD));
	seq_printf(s, "RESP0:   0x%08x\n", mmc_readl(host, RESP0));
	seq_printf(s, "RESP1:   0x%08x\n", mmc_readl(host, RESP1));
	seq_printf(s, "RESP2:   0x%08x\n", mmc_readl(host, RESP2));
	seq_printf(s, "RESP3:   0x%08x\n", mmc_readl(host, RESP3));
	seq_printf(s, "MINTSTS: 0x%08x\n", mmc_readl(host, MINTSTS));
	seq_printf(s, "STATUS:  0x%08x\n", mmc_readl(host, STATUS));
	seq_printf(s, "FIFOTH:  0x%08x\n", mmc_readl(host, FIFOTH));
	seq_printf(s, "CDETECT: 0x%08x\n", mmc_readl(host, CDETECT));
	seq_printf(s, "WRTPRT:  0x%08x\n", mmc_readl(host, WRTPRT));
	seq_printf(s, "TCBCNT:  0x%08x\n", mmc_readl(host, TCBCNT));
	seq_printf(s, "TBBCNT:  0x%08x\n", mmc_readl(host, TBBCNT));
	seq_printf(s, "DEBNCE:  0x%08x\n", mmc_readl(host, DEBNCE));
	seq_printf(s, "USRID:   0x%08x\n", mmc_readl(host, USRID));
	seq_printf(s, "VERID:   0x%08x\n", mmc_readl(host, VERID));
	seq_printf(s, "UHS_REG: 0x%08x\n", mmc_readl(host, UHS_REG));
	seq_printf(s, "RST_N:   0x%08x\n", mmc_readl(host, RST_N));

	return 0;
}

static int rk_mmc_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk_mmc_regs_show, inode->i_private);
}

static const struct file_operations rk_mmc_regs_fops = {
	.owner		= THIS_MODULE,
	.open		= rk_mmc_regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void rk_mmc_init_debugfs(struct rk_mmc *host)
{
	struct mmc_host	*mmc = host->mmc;
	struct dentry *root;
	struct dentry *node;

	root = mmc->debugfs_root;
	if (!root)
		return;

	node = debugfs_create_file("regs", S_IRUSR, root, host,
				   &rk_mmc_regs_fops);
	if (!node)
		goto err;

	node = debugfs_create_file("req", S_IRUSR, root, host,
				   &rk_mmc_req_fops);
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

static void rk_mmc_set_timeout(struct rk_mmc *host)
{
	/* timeout (maximum) */
	mmc_writel(host, TMOUT, 0xffffffff);
}

static bool mci_wait_reset(struct rk_mmc *host)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(500);
	unsigned int ctrl;

	mmc_writel(host, CTRL, (MMC_CTRL_RESET | MMC_CTRL_FIFO_RESET |
				MMC_CTRL_DMA_RESET));

	/* wait till resets clear */
	do {
		ctrl = mmc_readl(host, CTRL);
		if (!(ctrl & (MMC_CTRL_RESET | MMC_CTRL_FIFO_RESET |
			      MMC_CTRL_DMA_RESET)))
			return true;
	} while (time_before(jiffies, timeout));

	mmc_err(host, "Timeout resetting block (ctrl %#x)\n", ctrl);

	return false;
}


static void mmc_wait_data_idle(struct rk_mmc *host)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(500);
	unsigned int status = 0;

	while (time_before(jiffies, timeout)) {
		status = mmc_readl(host, STATUS);
		if (!(status & MMC_DATA_BUSY) && !(status & MMC_MC_BUSY))
			return;
	}
	mmc_err(host, "Timeout waiting for data idle (status 0x%x)\n", status);
}

static u32 rk_mmc_prepare_command(struct mmc_host *mmc, struct mmc_command *cmd)
{
	struct mmc_data	*data;
	u32 cmdr;
	cmd->error = -EINPROGRESS;

	cmdr = cmd->opcode;

	if (cmdr == MMC_STOP_TRANSMISSION)
		cmdr |= MMC_CMD_STOP;
	else
		cmdr |= MMC_CMD_PRV_DAT_WAIT;

	if (cmd->flags & MMC_RSP_PRESENT) {
		/* We expect a response, so set this bit */
		cmdr |= MMC_CMD_RESP_EXP;
		if (cmd->flags & MMC_RSP_136)
			cmdr |= MMC_CMD_RESP_LONG;
	}

	if (cmd->flags & MMC_RSP_CRC)
		cmdr |= MMC_CMD_RESP_CRC;

	data = cmd->data;
	if (data) {
		cmdr |= MMC_CMD_DAT_EXP;
		if (data->flags & MMC_DATA_STREAM)
			cmdr |= MMC_CMD_STRM_MODE;
		if (data->flags & MMC_DATA_WRITE)
			cmdr |= MMC_CMD_DAT_WR;
	}

	return cmdr;
}

static void rk_mmc_start_command(struct rk_mmc *host,
				 struct mmc_command *cmd, u32 cmd_flags)
{
	host->cmd = cmd;

	mmc_writel(host, CMDARG, cmd->arg);

	mmc_writel(host, CMD, cmd_flags | MMC_CMD_START | MMC_USE_HOLD_REG);
}
static void send_stop_cmd_ex(struct rk_mmc *host)
{
	struct mmc_command cmd;
	u32 cmdflags;

	host->stop.opcode = MMC_STOP_TRANSMISSION;
	host->stop.flags  = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC;
	host->stop.arg = 0;
	host->stop.data = NULL;
	host->stop.mrq = NULL;
	host->stop.retries = 0;
	host->stop.error = 0;
	cmdflags = rk_mmc_prepare_command(host->mmc, &host->stop);

	host->stop_ex = 1;
	mmc_dbg(host,"stop command ex: CMD%d, ARGR=0x%08x CMDR=0x%08x\n",
		 	host->stop.opcode, host->stop.arg, cmdflags);
	rk_mmc_start_command(host, &cmd, cmdflags);

}
static void send_stop_cmd(struct rk_mmc *host, struct mmc_data *data)
{
	mmc_dbg(host,"stop command: CMD%d, ARGR=0x%08x CMDR=0x%08x\n",
		 	data->stop->opcode, data->stop->arg, host->stop_cmdr);
	rk_mmc_start_command(host, data->stop, host->stop_cmdr);
}

static void rk_mmc_dma_cleanup(struct rk_mmc *host)
{
	struct mmc_data *data = host->data;

	if (data)
		if (!data->host_cookie)
			dma_unmap_sg(host->dev, data->sg, data->sg_len,
			     ((data->flags & MMC_DATA_WRITE)
			      ? DMA_TO_DEVICE : DMA_FROM_DEVICE));
}

/* DMA interface functions */
static void rk_mmc_stop_dma(struct rk_mmc *host)
{
	if (host->use_dma) {
		mmc_dbg(host, "stop dma\n");
		host->ops->stop(host);
		rk_mmc_dma_cleanup(host);
	} else {
		/* Data transfer was stopped by the interrupt handler */
		set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
	}
}

static int rk_mmc_submit_data_dma(struct rk_mmc *host, struct mmc_data *data)
{
	int res;
	u32 temp;

	/* Enable the DMA interface */
	temp = mmc_readl(host, CTRL);
	temp |= MMC_CTRL_DMA_ENABLE;
	mmc_writel(host, CTRL, temp);

	/* Disable RX/TX IRQs, let DMA handle it */
	temp = mmc_readl(host, INTMASK);
	temp  &= ~(MMC_INT_RXDR | MMC_INT_TXDR);
	mmc_writel(host, INTMASK, temp);

	res =  host->ops->start(host);
	return res;
}

static void rk_mmc_submit_data(struct rk_mmc *host, struct mmc_data *data)
{
	u32 temp;

	data->error = -EINPROGRESS;

	WARN_ON(host->data);
	host->sg = NULL;
	host->data = data;
	
	if (rk_mmc_submit_data_dma(host, data)) {
		mmc_dbg(host, "FIFO transfer\n");
		host->sg = data->sg;
		host->pio_offset = 0;
		if (data->flags & MMC_DATA_READ)
			host->dir_status = MMC_RECV_DATA;
		else
			host->dir_status = MMC_SEND_DATA;

		mmc_writel(host, RINTSTS, MMC_INT_TXDR | MMC_INT_RXDR);
		temp = mmc_readl(host, INTMASK);
		temp |= MMC_INT_TXDR | MMC_INT_RXDR;
		mmc_writel(host, INTMASK, temp);

		temp = mmc_readl(host, CTRL);
		temp &= ~MMC_CTRL_DMA_ENABLE;
		mmc_writel(host, CTRL, temp);
		host->use_dma = 0;
	}else{
		mmc_dbg(host, "DMA transfer\n");
		host->use_dma = 1;
	}
}

static void __rk_mmc_start_request(struct rk_mmc *host, struct mmc_command *cmd)
{
	struct mmc_request *mrq = host->mrq;
	struct mmc_data	*data;
	u32 cmdflags;

	host->mrq = mrq;

	host->pending_events = 0;
	host->completed_events = 0;
	host->data_status = 0;

	data = cmd->data;
	if (data) {
		rk_mmc_set_timeout(host);
		mmc_writel(host, BYTCNT, data->blksz*data->blocks);
		mmc_writel(host, BLKSIZ, data->blksz);
	}

	cmdflags = rk_mmc_prepare_command(host->mmc, cmd);

	/* this is the first command, send the initialization clock */
	if (test_and_clear_bit(MMC_NEED_INIT, &host->flags))
		cmdflags |= MMC_CMD_INIT;

	if(cmd->opcode == 0)
		cmdflags |= MMC_CMD_INIT;

	if (data) {
		rk_mmc_submit_data(host, data);
	}
	if(cmd->opcode == MMC_BUS_TEST_R || cmd->opcode == MMC_BUS_TEST_W)
		host->bus_test = 1;
	else
		host->bus_test = 0;
	
	mmc_dbg(host,"start command: CMD%d, ARGR=0x%08x CMDR=0x%08x\n",
		 	cmd->opcode, cmd->arg, cmdflags);
	rk_mmc_start_command(host, cmd, cmdflags);

	if (mrq->stop)
		host->stop_cmdr = rk_mmc_prepare_command(host->mmc, mrq->stop);
}

static void rk_mmc_start_request(struct rk_mmc *host)
{
	struct mmc_request *mrq = host->mrq;
	struct mmc_command *cmd;

	cmd = mrq->sbc ? mrq->sbc : mrq->cmd;
	__rk_mmc_start_request(host, cmd);
}
static void rk_mmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct rk_mmc *host = mmc_priv(mmc);

	WARN_ON(host->mrq);
	WARN_ON(host->state != STATE_IDLE);
	WARN_ON(host->shutdown == 1);

	spin_lock_bh(&host->lock);
	host->state = STATE_SENDING_CMD;
	host->mrq = mrq;
	rk_mmc_start_request(host);
	spin_unlock_bh(&host->lock);
}

static void mci_send_cmd(struct rk_mmc *host, u32 cmd, u32 arg)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(500);
	unsigned int cmd_status = 0;

	mmc_writel(host, CMDARG, arg);
	mmc_writel(host, CMD, MMC_CMD_START | cmd);

	while (time_before(jiffies, timeout)) {
		cmd_status = mmc_readl(host, CMD);
		if (!(cmd_status & MMC_CMD_START))
			return;
	}
	mmc_err(host, "Timeout sending command (cmd %#x arg %#x status %#x)\n",
		cmd, arg, cmd_status);
}


static void rk_mmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct rk_mmc *host = mmc_priv(mmc);
	u32 regs, div;

	/* set default 1 bit mode */
	host->ctype = MMC_CTYPE_1BIT;

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		host->ctype = MMC_CTYPE_1BIT;
		break;
	case MMC_BUS_WIDTH_4:
		host->ctype = MMC_CTYPE_4BIT;
		break;
	case MMC_BUS_WIDTH_8:
		host->ctype = MMC_CTYPE_8BIT;
		break;
	}
	/* DDR mode set */
	if (ios->timing == MMC_TIMING_UHS_DDR50){
		regs = mmc_readl(host, UHS_REG);
		regs |= MMC_UHS_DDR_MODE;
		mmc_writel(host, UHS_REG, regs);
	}
	if (ios->clock && ios->clock != host->curr_clock) {
		if (host->bus_hz % ios->clock)
			div = ((host->bus_hz / ios->clock) >> 1) + 1;
		else
			div = (host->bus_hz / ios->clock) >> 1;

		mmc_dbg(host, "Bus clock: %dHz, req: %dHz, actual: %dHz, div: %d\n",
				host->bus_hz, ios->clock, 
				div ? ((host->bus_hz / div) >> 1) : host->bus_hz, div);

		/* disable clock */
		mmc_writel(host, CLKENA, 0);
		mmc_writel(host, CLKSRC, 0);

		/* inform CIU */
		mci_send_cmd(host,
			     MMC_CMD_UPD_CLK | MMC_CMD_PRV_DAT_WAIT, 0);

		/* set clock to desired speed */
		mmc_writel(host, CLKDIV, div);

		/* inform CIU */
		mci_send_cmd(host,
			     MMC_CMD_UPD_CLK | MMC_CMD_PRV_DAT_WAIT, 0);

		/* enable clock */
		mmc_writel(host, CLKENA, MMC_CLKEN_ENABLE | MMC_CLKEN_LOW_PWR);

		/* inform CIU */
		mci_send_cmd(host,
			     MMC_CMD_UPD_CLK | MMC_CMD_PRV_DAT_WAIT, 0);

		host->curr_clock = ios->clock;
	}

	switch (ios->power_mode) {
	case MMC_POWER_UP:
		mmc_dbg(host, "power up\n");
		mmc_writel(host, PWREN, MMC_PWREN_ON);
#if 0
		mmc_writel(host, RST_N, 0);
		mdelay(60);
		mmc_writel(host, RST_N, MMC_CARD_RESET);
#endif
		set_bit(MMC_NEED_INIT, &host->flags);
		break;
	case MMC_POWER_OFF:
		mmc_dbg(host, "power off\n");
		mmc_writel(host, PWREN, 0);
	default:
		break;
	}
	mmc_dbg(host, "ctype: 0x%x\n", host->ctype);
	mmc_writel(host, CTYPE, host->ctype);
}

static int rk_mmc_get_ro(struct mmc_host *mmc)
{
	//struct rk_mmc *host = mmc_priv(mmc);

	return 0;
}

static int rk_mmc_get_cd(struct mmc_host *mmc)
{
	//struct rk_mmc *host = mmc_priv(mmc);

	return 1;
}

static int rk_mmc_get_dma_dir(struct mmc_data *data)
{
	if (data->flags & MMC_DATA_WRITE)
		return DMA_TO_DEVICE;
	else
		return DMA_FROM_DEVICE;
}

static int rk_mmc_pre_dma_transfer(struct rk_mmc *host,
		                   struct mmc_data *data,
				   bool next)
{
        struct scatterlist *sg;
	unsigned int i, sg_len;

	if (!next && data->host_cookie)
		return data->host_cookie;

	/*
	 * We don't do DMA on "complex" transfers, i.e. with
	 * non-word-aligned buffers or lengths. Also, we don't bother
	 * with all the DMA setup overhead for short transfers.
	 */
	if (data->blocks * data->blksz < MMC_DMA_THRESHOLD)
		return -EINVAL;
	if (data->blksz & 3)
		return -EINVAL;

	for_each_sg(data->sg, sg, data->sg_len, i) {
		if (sg->offset & 3 || sg->length & 3)
			return -EINVAL;
	}

	sg_len = dma_map_sg(host->dev,
			    data->sg,
			    data->sg_len,
			    rk_mmc_get_dma_dir(data));
	if (sg_len == 0)
		return -EINVAL;
	if (next)
		data->host_cookie = sg_len;
	
	return sg_len;
}
static void rk_mmc_pre_req(struct mmc_host *mmc,
		           struct mmc_request *mrq,
			   bool is_first_req)
{
	struct rk_mmc *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if(!data)
		return;
	if (data->host_cookie) {
		data->host_cookie = 0;
		return;
	}
	if (rk_mmc_pre_dma_transfer(host, mrq->data, 1) < 0)
		data->host_cookie = 0;

}
static void rk_mmc_post_req(struct mmc_host *mmc,
		           struct mmc_request *mrq,
			   int err)
{
	struct rk_mmc *host = mmc_priv(mmc);
	struct mmc_data *data = mrq->data;

	if(!data)
		return;
	if (data->host_cookie)
		dma_unmap_sg(host->dev,
			     data->sg,
			     data->sg_len,
			     rk_mmc_get_dma_dir(data));
	data->host_cookie = 0;
}
			
static const struct mmc_host_ops rk_mmc_ops = {
	.request	= rk_mmc_request,
	.set_ios	= rk_mmc_set_ios,
	.get_ro		= rk_mmc_get_ro,
	.get_cd		= rk_mmc_get_cd,
	.pre_req        = rk_mmc_pre_req,
	.post_req       = rk_mmc_post_req,
};

static void rk_mmc_request_end(struct rk_mmc *host, struct mmc_request *mrq)
	__releases(&host->lock)
	__acquires(&host->lock)

{
	WARN_ON(host->cmd || host->data);
	host->mrq = NULL;
	host->state = STATE_IDLE;
	spin_unlock(&host->lock);
	mmc_wait_data_idle(host);
	mmc_dbg(host, "mmc request done, RINSTS: 0x%x, pending_events: %lu\n", 
			mmc_readl(host, RINTSTS), host->pending_events);
	if(host->bus_test && mrq->data && mrq->data->error == 0){
		u32 ctype, div;

		ctype = mmc_readl(host, CTYPE);
		div = mmc_readl(host, CLKDIV);

		if(ctype & MMC_CTYPE_8BIT)
			mmc_info(host, "bus width: 8 bit, clock: %uHz\n",
					host->bus_hz/(div+1));
		else if(ctype & MMC_CTYPE_4BIT)
			mmc_info(host, "bus width: 4 bit, clock: %uHz\n",
					host->bus_hz/(div+1));
		else
			mmc_info(host, "bus width: 1 bit, clock: %uHz\n",
					host->bus_hz/(div+1));
	}
	mmc_request_done(host->mmc, mrq);
	spin_lock(&host->lock);
}

static void rk_mmc_command_complete(struct rk_mmc *host, struct mmc_command *cmd)
{
	u32 status = host->cmd_status;

	host->cmd_status = 0;

	/* Read the response from the card (up to 16 bytes) */
	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			cmd->resp[3] = mmc_readl(host, RESP0);
			cmd->resp[2] = mmc_readl(host, RESP1);
			cmd->resp[1] = mmc_readl(host, RESP2);
			cmd->resp[0] = mmc_readl(host, RESP3);
		} else {
			cmd->resp[0] = mmc_readl(host, RESP0);
			cmd->resp[1] = 0;
			cmd->resp[2] = 0;
			cmd->resp[3] = 0;
		}
	}

	if (status & MMC_INT_RTO){
		mmc_dbg(host, "CMD%d response timeout\n", cmd->opcode);
		cmd->error = -ETIMEDOUT;
	}
	else if ((cmd->flags & MMC_RSP_CRC) && (status & MMC_INT_RCRC)){
		mmc_dbg(host, "CMD%d crc error\n", cmd->opcode);
		cmd->error = -EILSEQ;
	}
	else if (status & MMC_INT_RESP_ERR){
		mmc_dbg(host, "CMD%d response error\n", cmd->opcode);
		cmd->error = -EIO;
	}
	else
		cmd->error = 0;

	if (cmd->error) {
		/* newer ip versions need a delay between retries */
		mdelay(20);

		if (cmd->data) {
			host->data = NULL;
			rk_mmc_stop_dma(host);
		}
	}
}

static void rk_mmc_tasklet_func(unsigned long priv)
{
	struct rk_mmc *host = (struct rk_mmc *)priv;
	struct mmc_data	*data;
	struct mmc_command *cmd;
	enum rk_mmc_state state;
	enum rk_mmc_state prev_state;
	u32 status;

	spin_lock(&host->lock);

	state = host->state;
	data = host->data;

	do {
		prev_state = state;

		switch (state) {
		case STATE_IDLE:
			break;

		case STATE_SENDING_CMD:
			mmc_dbg(host, "sending cmd, pending_events: %lx\n", host->pending_events);
			if (!test_and_clear_bit(EVENT_CMD_COMPLETE,
						&host->pending_events))
				break;

			cmd = host->cmd;
			host->cmd = NULL;
			set_bit(EVENT_CMD_COMPLETE, &host->completed_events);
			rk_mmc_command_complete(host, cmd);
			if (cmd == host->mrq->sbc && !cmd->error) {
				prev_state = state = STATE_SENDING_CMD;
				__rk_mmc_start_request(host, host->mrq->cmd);
				goto unlock;
			}

			if (!host->mrq->data || cmd->error) {
				rk_mmc_request_end(host, host->mrq);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_DATA;
			/* fall through */

		case STATE_SENDING_DATA:
			mmc_dbg(host, "sending data, pending_events: %lx\n", host->pending_events);
			if (test_and_clear_bit(EVENT_DATA_ERROR,
					       &host->pending_events)) {
				rk_mmc_stop_dma(host);
				if (data->stop)
					send_stop_cmd(host, data);
				else
					send_stop_cmd_ex(host);
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
			mmc_dbg(host, "data busy, pending_events: %lx, data_status: %08x, status: %08x\n", 
					host->pending_events, host->data_status, mmc_readl(host, STATUS));
			if (!test_and_clear_bit(EVENT_DATA_COMPLETE,
						&host->pending_events)){
					break;
			}
			host->data = NULL;
			set_bit(EVENT_DATA_COMPLETE, &host->completed_events);
			status = host->data_status;
			
			if (status & MMC_DATA_ERROR_FLAGS) {
				if (status & MMC_INT_DTO) {
					if(!host->bus_test)
						mmc_err(host, "data timeout error "
							"(data_status=%08x)\n", status);
					data->error = -ETIMEDOUT;
				} else if (status & MMC_INT_DCRC) {
					if(!host->bus_test)
						mmc_err(host, "data CRC error "
							"(data_status=%08x)\n", status);
					data->error = -EILSEQ;
				} else {
					if(!host->bus_test)
						mmc_err(host, "data FIFO error "
							"(data_status=%08x)\n", status);
					data->error = -EIO;
				}
			} else {
				data->bytes_xfered = data->blocks * data->blksz;
				data->error = 0;
			}

			if (!data->stop && !host->stop_ex) {
				rk_mmc_request_end(host, host->mrq);
				goto unlock;
			}

			if (host->mrq->sbc && !data->error) {
				data->stop->error = 0;
				rk_mmc_request_end(host, host->mrq);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_STOP;
			if (!data->error && data->stop)
				send_stop_cmd(host, data);
			/* fall through */

		case STATE_SENDING_STOP:
			mmc_dbg(host, "sending stop, pending_events: %lx\n", host->pending_events);
			if (!test_and_clear_bit(EVENT_CMD_COMPLETE,
						&host->pending_events))
				break;

			host->cmd = NULL;
			if(host->stop_ex){
				host->stop_ex = 0;
				rk_mmc_command_complete(host, &host->stop);
			}
			else
				rk_mmc_command_complete(host, host->mrq->stop);
			rk_mmc_request_end(host, host->mrq);
			goto unlock;

		case STATE_DATA_ERROR:
			mmc_dbg(host, "data error, pending_events: %lx\n", host->pending_events);
			if (!test_and_clear_bit(EVENT_XFER_COMPLETE,
						&host->pending_events))
				break;

			state = STATE_DATA_BUSY;
			break;
		}
	} while (state != prev_state);

	host->state = state;
unlock:
	spin_unlock(&host->lock);

}


static void rk_mmc_push_data(struct rk_mmc *host, void *buf, int cnt)
{
	u32 *pdata = (u32 *)buf;

	WARN_ON(cnt % 4 != 0);
	WARN_ON((unsigned long)pdata & 0x3);

	cnt = cnt >> 2;
	while (cnt > 0) {
		mmc_writel(host, DATA, *pdata++);
		cnt--;
	}
}

static void rk_mmc_pull_data(struct rk_mmc *host, void *buf, int cnt)
{
	u32 *pdata = (u32 *)buf;

	WARN_ON(cnt % 4 != 0);
	WARN_ON((unsigned long)pdata & 0x3);

	cnt = cnt >> 2;
	while (cnt > 0) {
		*pdata++ = mmc_readl(host, DATA);
		cnt--;
	}
}

static void rk_mmc_read_data_pio(struct rk_mmc *host)
{
	struct scatterlist *sg = host->sg;
	void *buf = sg_virt(sg);
	unsigned int offset = host->pio_offset;
	struct mmc_data	*data = host->data;
	u32 status;
	unsigned int nbytes = 0, len;

	mmc_dbg(host, "read data pio\n");

	do {
		len = MMC_GET_FCNT(mmc_readl(host, STATUS)) << 2;
		if (offset + len <= sg->length) {
			rk_mmc_pull_data(host, (void *)(buf + offset), len);

			offset += len;
			nbytes += len;

			if (offset == sg->length) {
				flush_dcache_page(sg_page(sg));
				host->sg = sg = sg_next(sg);
				if (!sg)
					goto done;

				offset = 0;
				buf = sg_virt(sg);
			}
		} else {
			unsigned int remaining = sg->length - offset;
			rk_mmc_pull_data(host, (void *)(buf + offset),
					remaining);
			nbytes += remaining;

			flush_dcache_page(sg_page(sg));
			host->sg = sg = sg_next(sg);
			if (!sg)
				goto done;

			offset = len - remaining;
			buf = sg_virt(sg);
			rk_mmc_pull_data(host, buf, offset);
			nbytes += offset;
		}

		status = mmc_readl(host, MINTSTS);
		mmc_writel(host, RINTSTS, MMC_INT_RXDR);
		if (status & MMC_DATA_ERROR_FLAGS) {
			host->data_status = status;
			data->bytes_xfered += nbytes;

			set_bit(EVENT_DATA_ERROR, &host->pending_events);

			tasklet_schedule(&host->tasklet);
			return;
		}
	} while (status & MMC_INT_RXDR); /*if the RXDR is ready read again*/
	len = MMC_GET_FCNT(mmc_readl(host, STATUS));
	host->pio_offset = offset;
	data->bytes_xfered += nbytes;
	return;

done:
	data->bytes_xfered += nbytes;
	set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
}

static void rk_mmc_write_data_pio(struct rk_mmc *host)
{
	struct scatterlist *sg = host->sg;
	void *buf = sg_virt(sg);
	unsigned int offset = host->pio_offset;
	struct mmc_data	*data = host->data;
	u32 status;
	unsigned int nbytes = 0, len;

	mmc_dbg(host, "write data pio\n");
	do {
		len = FIFO_DETH -
			(MMC_GET_FCNT(mmc_readl(host, STATUS)) << 2);
		if (offset + len <= sg->length) {
			rk_mmc_push_data(host, (void *)(buf + offset), len);

			offset += len;
			nbytes += len;
			if (offset == sg->length) {
				host->sg = sg = sg_next(sg);
				if (!sg)
					goto done;

				offset = 0;
				buf = sg_virt(sg);
			}
		} else {
			unsigned int remaining = sg->length - offset;

			rk_mmc_push_data(host, (void *)(buf + offset),
					remaining);
			nbytes += remaining;

			host->sg = sg = sg_next(sg);
			if (!sg)
				goto done;

			offset = len - remaining;
			buf = sg_virt(sg);
			rk_mmc_push_data(host, (void *)buf, offset);
			nbytes += offset;
		}

		status = mmc_readl(host, MINTSTS);
		mmc_writel(host, RINTSTS, MMC_INT_TXDR);
		if (status & MMC_DATA_ERROR_FLAGS) {
			host->data_status = status;
			data->bytes_xfered += nbytes;

			set_bit(EVENT_DATA_ERROR, &host->pending_events);

			tasklet_schedule(&host->tasklet);
			return;
		}
	} while (status & MMC_INT_TXDR); /* if TXDR write again */

	host->pio_offset = offset;
	data->bytes_xfered += nbytes;

	return;

done:
	data->bytes_xfered += nbytes;
	set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
}

static void rk_mmc_cmd_interrupt(struct rk_mmc *host, u32 status)
{
	if (!host->cmd_status)
		host->cmd_status = status;

	set_bit(EVENT_CMD_COMPLETE, &host->pending_events);
	tasklet_schedule(&host->tasklet);
}

static irqreturn_t rk_mmc_interrupt(int irq, void *dev_id)
{
	struct rk_mmc *host = dev_id;
	u32 status, pending;
	unsigned int pass_count = 0;

	do {
		status = mmc_readl(host, RINTSTS);
		pending = mmc_readl(host, MINTSTS); /* read-only mask reg */
		mmc_dbg(host, "RINSTS: 0x%x, MINTSTS: 0x%x\n", status, pending);

		if (!pending)
			break;

		if (pending & MMC_CMD_ERROR_FLAGS) {
			mmc_writel(host, RINTSTS, MMC_CMD_ERROR_FLAGS);
			host->cmd_status = status;
			set_bit(EVENT_CMD_COMPLETE, &host->pending_events);
			tasklet_schedule(&host->tasklet);
		}

		if (pending & MMC_DATA_ERROR_FLAGS) {
			/* if there is an error report DATA_ERROR */
			mmc_writel(host, RINTSTS, MMC_DATA_ERROR_FLAGS);
			host->data_status = status;
			set_bit(EVENT_DATA_ERROR, &host->pending_events);
			tasklet_schedule(&host->tasklet);
		}

		if (pending & MMC_INT_DATA_OVER) {
			mmc_dbg(host, "data over int\n");
			mmc_writel(host, RINTSTS, MMC_INT_DATA_OVER);
			if (!host->data_status)
				host->data_status = status;
			if (host->dir_status == MMC_RECV_DATA) {
				if (host->sg != NULL)
					rk_mmc_read_data_pio(host);
			}
			set_bit(EVENT_DATA_COMPLETE, &host->pending_events);
			tasklet_schedule(&host->tasklet);
		}

		if (pending & MMC_INT_RXDR) {
			mmc_writel(host, RINTSTS, MMC_INT_RXDR);
			if (host->sg)
				rk_mmc_read_data_pio(host);
		}

		if (pending & MMC_INT_TXDR) {
			mmc_writel(host, RINTSTS, MMC_INT_TXDR);
			if (host->sg)
				rk_mmc_write_data_pio(host);
		}

		if (pending & MMC_INT_CMD_DONE) {
			mmc_writel(host, RINTSTS, MMC_INT_CMD_DONE);
			rk_mmc_cmd_interrupt(host, status);
		}
	} while (pass_count++ < 5);

	return IRQ_HANDLED;
}

#define EMMC_FLAHS_SEL	(1<<11)
static int internal_storage_is_emmc(void)
{
#ifdef CONFIG_ARCH_RK3026
	if((iomux_is_set(EMMC_CLKOUT) == 1) &&
	   (iomux_is_set(EMMC_CMD) == 1) &&
	   (iomux_is_set(EMMC_D0) == 1))
		return 1;
#else
	if(readl_relaxed(RK30_GRF_BASE + GRF_SOC_CON0) & EMMC_FLAHS_SEL)
		return 1;
#endif
	return 0;
}
static void rk_mmc_set_iomux(void)
{
	iomux_set(EMMC_CLKOUT);
	iomux_set(EMMC_CMD);
	iomux_set(EMMC_RSTNOUT);
#ifdef CONFIG_ARCH_RK3026
	iomux_set(EMMC_PWREN);
	iomux_set(EMMC_D0);
	iomux_set(EMMC_D1);
	iomux_set(EMMC_D2);
	iomux_set(EMMC_D3);
	iomux_set(EMMC_D4);
	iomux_set(EMMC_D5);
	iomux_set(EMMC_D6);
	iomux_set(EMMC_D7);
#endif
}

static int rk_mmc_probe(struct platform_device *pdev)
{
	struct rk_mmc *host;
	struct mmc_host *mmc;
	struct resource	*regs;
	int res;

	if(!internal_storage_is_emmc()){
		dev_err(&pdev->dev, "internal_storage is NOT emmc\n");
		return -ENXIO;
	}

	rk_mmc_set_iomux();

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENXIO;

	mmc = mmc_alloc_host(sizeof(struct rk_mmc), &pdev->dev);
	if (!mmc)
		return -ENOMEM;

	host = mmc_priv(mmc);
	host->mmc = mmc;

	mmc->ops = &rk_mmc_ops;
	mmc->unused = 1;

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0)
		return host->irq;

	host->dev = &pdev->dev;
	host->ops = &dma_ops;
	host->state = STATE_IDLE;

	res = -ENOMEM;
	host->clk = clk_get(&pdev->dev, "emmc");
	if(!host->clk)
		goto err_freehost;
	clk_set_rate(host->clk, MMC_BUS_CLOCK);
	host->bus_hz = clk_get_rate(host->clk);

        clk_enable(host->clk);
	clk_enable(clk_get(&pdev->dev, "hclk_emmc"));

	spin_lock_init(&host->lock);

	host->regs = ioremap(regs->start, regs->end - regs->start + 1);
	if (!host->regs)
		goto err_putclk;

	host->dma_addr = regs->start + MMC_DATA;

	res = host->ops->init(host);
	if(res < 0)
		goto err_iounmap;

	/* Reset all blocks */
	if (!mci_wait_reset(host)) {
		res = -ENODEV;
		goto err_exitdma;
	}

	/* Clear the interrupts for the host controller */
	mmc_writel(host, RINTSTS, 0xFFFFFFFF);
	mmc_writel(host, INTMASK, 0); /* disable all mmc interrupt first */

	/* Put in max timeout */
	mmc_writel(host, TMOUT, 0xFFFFFFFF);
	mmc_writel(host, FIFOTH, 
			(0x3 << 28) | ((FIFO_DETH/2 - 1) << 16) | ((FIFO_DETH/2) << 0));
	/* disable clock to CIU */
	mmc_writel(host, CLKENA, 0);
	mmc_writel(host, CLKSRC, 0);
	tasklet_init(&host->tasklet, rk_mmc_tasklet_func, (unsigned long)host);

	res = request_irq(host->irq, rk_mmc_interrupt, 0, "emmc", host);
	if (res < 0)
		goto err_exitdma;

	mmc->f_min = DIV_ROUND_UP(host->bus_hz, 510);
	mmc->f_max = host->bus_hz/2;

	mmc->ocr_avail = MMC_VDD_165_195| MMC_VDD_29_30 | MMC_VDD_30_31 | 
			 MMC_VDD_31_32 | MMC_VDD_32_33 | MMC_VDD_33_34;

	mmc->caps = MMC_CAP_4_BIT_DATA| MMC_CAP_8_BIT_DATA | MMC_CAP_NONREMOVABLE |
		    MMC_CAP_1_8V_DDR | MMC_CAP_UHS_DDR50 |
		    MMC_CAP_BUS_WIDTH_TEST |
		    MMC_CAP_ERASE |
		    MMC_CAP_CMD23 |
		    /*MMC_CAP_WAIT_WHILE_BUSY |*/
		    MMC_CAP_MMC_HIGHSPEED | MMC_CAP_SD_HIGHSPEED;	

	//mmc->caps2 = MMC_CAP2_CACHE_CTRL;

	mmc->max_segs = 64;
	mmc->max_blk_size = 512;
	mmc->max_blk_count = 4096;
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_seg_size = mmc->max_req_size;
#if 0
	if(grf_get_io_power_domain_voltage(IO_PD_FLASH) == IO_PD_VOLTAGE_1_8V)
		mmc_writel(host, UHS_REG, MMC_UHS_VOLT_18);
#endif
	mmc_writel(host, RINTSTS, 0xFFFFFFFF);
	mmc_writel(host, INTMASK, MMC_INT_CMD_DONE | MMC_INT_DATA_OVER |
		   MMC_INT_TXDR | MMC_INT_RXDR | MMC_ERROR_FLAGS);
	mmc_writel(host, CTRL, MMC_CTRL_INT_ENABLE); /* Enable mci interrupt */
	platform_set_drvdata(pdev, host);

	mmc_add_host(mmc);

#if defined(CONFIG_DEBUG_FS)
	rk_mmc_init_debugfs(host);
#endif

	mmc_info(host, "MMC controller initialized, bus_hz: %uHz\n", host->bus_hz);

	return 0;
err_exitdma:
	host->ops->exit(host);
err_iounmap:
	iounmap(host->regs);
err_putclk:
        clk_disable(host->clk);
	clk_disable(clk_get(&pdev->dev, "hclk_mmc"));
	clk_put(host->clk);
err_freehost:
	mmc_free_host(mmc);
	
	return res;
}
static void rk_mmc_shutdown(struct platform_device *pdev)
{
	struct rk_mmc *host = platform_get_drvdata(pdev);
	//struct mmc_host *mmc = host->mmc;

	mmc_info(host, "shutdown\n");

	host->shutdown = 1;
	//card go pre-idle state
	mmc_writel(host, CMDARG, 0xF0F0F0F0);
	mmc_writel(host, CMD, 0 | MMC_CMD_INIT | MMC_CMD_START | MMC_USE_HOLD_REG);
	mdelay(10);
#if 0
	host->shutdown = 1;
	mmc_remove_host(host->mmc);
	mmc_info(host, "mmc removed\n");
	platform_set_drvdata(pdev, NULL);

	host->ops->exit(host);

	free_irq(host->irq, host);
	mmc_writel(host, RINTSTS, 0xFFFFFFFF);
	mmc_writel(host, INTMASK, 0); /* disable all mmc interrupt first */
	mmc_writel(host, PWREN, 0);
	mmc_writel(host, RST_N, 0);

	/* disable clock to CIU */
	mmc_writel(host, CLKENA, 0);
	mmc_writel(host, CLKSRC, 0);
        clk_disable(host->clk);
	clk_disable(clk_get(&pdev->dev, "hclk_mmc"));
	clk_put(host->clk);

	iounmap(host->regs);

	mmc_free_host(mmc);
#endif
	mmc_writel(host, PWREN, 0);
	mmc_writel(host, RST_N, 0);

	return;
}
static int __exit rk_mmc_remove(struct platform_device *pdev)
{
	rk_mmc_shutdown(pdev);
	return 0;
}
#ifdef CONFIG_PM
static int rk_mmc_suspend(struct platform_device *pdev, pm_message_t mesg)
{
	int res = 0;
	struct rk_mmc *host = platform_get_drvdata(pdev);

	res = mmc_suspend_host(host->mmc);
	return res;
}

static int rk_mmc_resume(struct platform_device *pdev)
{
	int res = 0;

	struct rk_mmc *host = platform_get_drvdata(pdev);

	if (!mci_wait_reset(host)) {
		res = -ENODEV;
		return res;
	}
	mmc_writel(host, FIFOTH, 
			(0x3 << 28) | ((FIFO_DETH/2 - 1) << 16) | ((FIFO_DETH/2) << 0));

	mmc_writel(host, UHS_REG, 0);

	/* disable clock to CIU */
	mmc_writel(host, CLKENA, 0);
	mmc_writel(host, CLKSRC, 0);

	mmc_writel(host, RINTSTS, 0xFFFFFFFF);
	mmc_writel(host, INTMASK, MMC_INT_CMD_DONE | MMC_INT_DATA_OVER |
		   MMC_INT_TXDR | MMC_INT_RXDR | MMC_ERROR_FLAGS);
	mmc_writel(host, CTRL, MMC_CTRL_INT_ENABLE);

	res = mmc_resume_host(host->mmc);

	return res;
}
#else
#define rk_mmc_suspend	NULL
#define rk_mmc_resume	NULL
#endif /* CONFIG_PM */

static struct platform_driver rk_mmc_driver = {
	.remove		= __exit_p(rk_mmc_remove),
	.shutdown	= rk_mmc_shutdown,
	.suspend	= rk_mmc_suspend,
	.resume		= rk_mmc_resume,
	.driver		= {
		.name		= "emmc",
	},
};

static int __init rk_mmc_init(void)
{
	return platform_driver_probe(&rk_mmc_driver, rk_mmc_probe);
}

static void __exit rk_mmc_exit(void)
{
	platform_driver_unregister(&rk_mmc_driver);
}

fs_initcall(rk_mmc_init);
module_exit(rk_mmc_exit);
