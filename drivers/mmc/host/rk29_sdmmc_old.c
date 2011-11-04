/* drivers/mmc/host/rk29_sdmmc.c
 *
 * Copyright (C) 2010 ROCKCHIP, Inc.
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 * mount -t debugfs debugfs /data/debugfs;cat /data/debugfs/mmc0/status
 * echo 't' >/proc/sysrq-trigger
 * echo 19 >/sys/module/wakelock/parameters/debug_mask
 * vdc volume uevent on
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
#include <linux/stat.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/card.h>
#include <linux/earlysuspend.h>

#include <mach/board.h>
#include <mach/rk29_iomap.h>
#include <mach/gpio.h>
#include <mach/iomux.h>

#include <asm/dma.h>
#include <mach/rk29-dma-pl330.h>
#include <asm/scatterlist.h>

#include "rk2818-sdmmc.h"

#define RK29_SDMMC_DATA_ERROR_FLAGS	(SDMMC_INT_DRTO | SDMMC_INT_DCRC | SDMMC_INT_HTO | SDMMC_INT_SBE | SDMMC_INT_EBE | SDMMC_INT_FRUN)
#define RK29_SDMMC_CMD_ERROR_FLAGS	(SDMMC_INT_RTO | SDMMC_INT_RCRC | SDMMC_INT_RE | SDMMC_INT_HLE)
#define RK29_SDMMC_ERROR_FLAGS		(RK29_SDMMC_DATA_ERROR_FLAGS | RK29_SDMMC_CMD_ERROR_FLAGS | SDMMC_INT_HLE)

#define RK29_SDMMC_TMO_COUNT		10000

#define RK29_SDCARD_CLK				50  //48Mhz
#define RK29_SDIO_CLK				50	//36Mhz

enum {
	EVENT_CMD_COMPLETE = 0,
	EVENT_XFER_COMPLETE,
	EVENT_DATA_COMPLETE,
	EVENT_DATA_ERROR,
	EVENT_XFER_ERROR
};
enum {
	MRQ_REQUEST_START = 0,
	MRQ_INT_CMD_DONE,  //1
	MRQ_INT_CMD_ERR,  //2
	MRQ_INT_DATA_DONE,  //3
	MRQ_INT_DATA_ERR,  //4
	MRQ_INT_CD,  //5
	MRQ_INT_SDIO,  //6
	MRQ_HAS_DATA,  //7
	MRQ_HAS_STOP,  //8
	MRQ_CMD_START_TMO,  //9
	MRQ_CMD_START_DONE,  //10
	MRQ_STOP_START_TMO,  //11
	MRQ_STOP_START_DONE,  //12
	MRQ_CLK_START_TMO,  //13
	MRQ_CLK_START_DONE,  //14
	MRQ_RESET_CTRL_ERR,  //15
	MRQ_RESET_CTRL_DONE,  //16
	MRQ_DMA_SET_ERR,  //17
	MRQ_START_DMA,	//18
	MRQ_STOP_DMA,  //19
	MRQ_DMA_DONE,  //20
	MRQ_REQUEST_DONE,  //21
};
enum rk29_sdmmc_state {
	STATE_IDLE = 0,
	STATE_SENDING_CMD,
	STATE_SENDING_DATA,
	STATE_DATA_BUSY,
	STATE_SENDING_STOP,
	STATE_DATA_ERROR,
};
#define rk29_sdmmc_test_and_clear_pending(host, event)		\
	test_and_clear_bit(event, &host->pending_events)
#define rk29_sdmmc_set_completed(host, event)			\
		set_bit(event, &host->completed_events)
#define rk29_sdmmc_set_pending(host, event)				\
	set_bit(event, &host->pending_events)
#define rk29_sdmmc_set_mrq_status(host, status)				\
		set_bit(status, &host->mrq_status)
#define rk29_sdmmc_test_mrq_status(host, status)				\
			test_bit(status, &host->mrq_status)

struct rk29_sdmmc_dma_info {
	enum dma_ch chn;
	char *name;
	struct rk29_dma_client client;
};
static struct rk29_sdmmc_dma_info dma_infos[] = {
	{
		.chn = DMACH_SDMMC,
		.client = {
			.name = "rk29-dma-sdmmc0",
		}
	},
	{
		.chn = DMACH_SDIO,
		.client = {
			.name = "rk29-dma-sdio1",
		}
	},
};
static int rk29_sdmmc_is_sdio(struct rk29_sdmmc_platform_data *pdata)
{
	if(strncmp(pdata->dma_name, "sdio", strlen("sdio")) == 0)
		return 1;
	else
		return 0;
}

struct rk29_sdmmc {
	struct device 				*dev;
	
	int							is_sdio;
	int							is_init;
	int							gpio_det;
	int							gpio_irq;
	int							irq;

	int							enable_sd_warkup;

	unsigned int				clock;
	unsigned int				ios_clock;
	unsigned int				div;
	spinlock_t					lock;
	unsigned int				stop_cmdr;
	unsigned int				cmd_intsts;
	unsigned int				data_intsts;
	unsigned long				pending_events;
	unsigned long				completed_events;
	unsigned long				mrq_status;
	unsigned long				old_mrq_status;

	unsigned int				bus_hz;
	

	void __iomem				*regs;

	struct mmc_host 			*mmc;
	struct delayed_work			work;
	struct rk29_sdmmc_dma_info 	dma_info;
	struct tasklet_struct		tasklet;
	struct mmc_request			*mrq;
	struct mmc_command			*cmd;
	struct clk 					*clk;
	struct timer_list			monitor_timer;

	enum rk29_sdmmc_state		state;
	dma_addr_t      			dma_addr;
	
	int (*get_wifi_cd)(struct device *);
#ifdef CONFIG_HAS_EARLYSUSPEND
	struct early_suspend		early_suspend;
#endif
};
struct rk29_sdmmc *sdio_host = NULL;
static void rk29_sdmmc_write(unsigned char  __iomem	*regbase, unsigned int regOff,unsigned int val)
{
	__raw_writel(val,regbase + regOff);
}

static unsigned int rk29_sdmmc_read(unsigned char  __iomem	*regbase, unsigned int regOff)
{
	return __raw_readl(regbase + regOff);
}


#if defined (CONFIG_DEBUG_FS)
/*
 * The debugfs stuff below is mostly optimized away when
 * CONFIG_DEBUG_FS is not set.
 */
static int rk29_sdmmc_req_show(struct seq_file *s, void *v)
{
	struct rk29_sdmmc	*host = s->private;
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_command	*stop;
	struct mmc_data		*data;

	/* Make sure we get a consistent snapshot */
	spin_lock(&host->lock);
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

	spin_unlock(&host->lock);

	return 0;
}

static int rk29_sdmmc_regs_show(struct seq_file *s, void *v)
{
	struct rk29_sdmmc	*host = s->private;

	seq_printf(s, "SDMMC_CTRL:   \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CTRL));
	seq_printf(s, "SDMMC_PWREN:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_PWREN));
	seq_printf(s, "SDMMC_CLKDIV: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CLKDIV));
	seq_printf(s, "SDMMC_CLKSRC: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CLKSRC));
	seq_printf(s, "SDMMC_CLKENA: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CLKENA));
	seq_printf(s, "SDMMC_TMOUT:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_TMOUT));
	seq_printf(s, "SDMMC_CTYPE:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CTYPE));
	seq_printf(s, "SDMMC_BLKSIZ: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_BLKSIZ));
	seq_printf(s, "SDMMC_BYTCNT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_BYTCNT));
	seq_printf(s, "SDMMC_INTMASK:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_INTMASK));
	seq_printf(s, "SDMMC_CMDARG: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CMDARG));
	seq_printf(s, "SDMMC_CMD:    \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CMD));
	seq_printf(s, "SDMMC_RESP0:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP0));
	seq_printf(s, "SDMMC_RESP1:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP1));
	seq_printf(s, "SDMMC_RESP2:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP2));
	seq_printf(s, "SDMMC_RESP3:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP3));
	seq_printf(s, "SDMMC_MINTSTS:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_MINTSTS));
	seq_printf(s, "SDMMC_RINTSTS:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RINTSTS));
	seq_printf(s, "SDMMC_STATUS: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_STATUS));
	seq_printf(s, "SDMMC_FIFOTH: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_FIFOTH));
	seq_printf(s, "SDMMC_CDETECT:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CDETECT));
	seq_printf(s, "SDMMC_WRTPRT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_WRTPRT));
	seq_printf(s, "SDMMC_TCBCNT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_TCBCNT));
	seq_printf(s, "SDMMC_TBBCNT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_TBBCNT));
	seq_printf(s, "SDMMC_DEBNCE: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_DEBNCE));

	return 0;
}

static int rk29_sdmmc_status_show(struct seq_file *s, void *v)
{
	struct rk29_sdmmc	*host = s->private;

	seq_printf(s, "state:   \t\t0x%08x\n", host->state);
	seq_printf(s, "pending_events:   \t0x%08lx\n", host->pending_events);
	seq_printf(s, "completed_events:   \t0x%08lx\n", host->completed_events);
	seq_printf(s, "mrq_status:   \t\t0x%08lx\n", host->mrq_status);
	seq_printf(s, "old_mrq_status:   \t0x%08lx\n", host->old_mrq_status);

	return 0;
}


static int rk29_sdmmc_req_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk29_sdmmc_req_show, inode->i_private);
}

static const struct file_operations rk29_sdmmc_req_fops = {
	.owner		= THIS_MODULE,
	.open		= rk29_sdmmc_req_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int rk29_sdmmc_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk29_sdmmc_regs_show, inode->i_private);
}

static const struct file_operations rk29_sdmmc_regs_fops = {
	.owner		= THIS_MODULE,
	.open		= rk29_sdmmc_regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};
static int rk29_sdmmc_status_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk29_sdmmc_status_show, inode->i_private);
}

static const struct file_operations rk29_sdmmc_status_fops = {
	.owner		= THIS_MODULE,
	.open		= rk29_sdmmc_status_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void rk29_sdmmc_init_debugfs(struct rk29_sdmmc *host)
{
	struct mmc_host		*mmc = host->mmc;
	struct dentry		*root;
	struct dentry		*node;

	root = mmc->debugfs_root;
	if (!root)
		return;

	node = debugfs_create_file("regs", S_IRUSR, root, host,&rk29_sdmmc_regs_fops);
	if (!node)
		goto err;

	node = debugfs_create_file("req", S_IRUSR, root, host, &rk29_sdmmc_req_fops);
	if (!node)
		goto err;

	node = debugfs_create_file("status", S_IRUSR, root, host, &rk29_sdmmc_status_fops);
	if (!node)
		goto err;

	return;

err:
	dev_err(&mmc->class_dev, "failed to initialize debugfs for host\n");
}
#endif
static void rk29_sdmmc_show_info(struct rk29_sdmmc *host)
{
	dev_info(host->dev, "state:   \t\t0x%08x\n", host->state);
	dev_info(host->dev, "pending_events:   \t0x%08lx\n", host->pending_events);
	dev_info(host->dev, "completed_events:   \t0x%08lx\n", host->completed_events);
	dev_info(host->dev, "mrq_status:   \t\t0x%08lx\n\n", host->mrq_status);
	dev_info(host->dev, "old_mrq_status:   \t0x%08lx\n\n", host->old_mrq_status);
	dev_info(host->dev, "SDMMC_CTRL:   \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CTRL));
	dev_info(host->dev, "SDMMC_PWREN:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_PWREN));
	dev_info(host->dev, "SDMMC_CLKDIV: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CLKDIV));
	dev_info(host->dev, "SDMMC_CLKSRC: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CLKSRC));
	dev_info(host->dev, "SDMMC_CLKENA: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CLKENA));
	dev_info(host->dev, "SDMMC_TMOUT:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_TMOUT));
	dev_info(host->dev, "SDMMC_CTYPE:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CTYPE));
	dev_info(host->dev, "SDMMC_BLKSIZ: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_BLKSIZ));
	dev_info(host->dev, "SDMMC_BYTCNT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_BYTCNT));
	dev_info(host->dev, "SDMMC_INTMASK:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_INTMASK));
	dev_info(host->dev, "SDMMC_CMDARG: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CMDARG));
	dev_info(host->dev, "SDMMC_CMD:    \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CMD));
	dev_info(host->dev, "SDMMC_RESP0:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP0));
	dev_info(host->dev, "SDMMC_RESP1:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP1));
	dev_info(host->dev, "SDMMC_RESP2:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP2));
	dev_info(host->dev, "SDMMC_RESP3:  \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RESP3));
	dev_info(host->dev, "SDMMC_MINTSTS:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_MINTSTS));
	dev_info(host->dev, "SDMMC_RINTSTS:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_RINTSTS));
	dev_info(host->dev, "SDMMC_STATUS: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_STATUS));
	dev_info(host->dev, "SDMMC_FIFOTH: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_FIFOTH));
	dev_info(host->dev, "SDMMC_CDETECT:\t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_CDETECT));
	dev_info(host->dev, "SDMMC_WRTPRT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_WRTPRT));
	dev_info(host->dev, "SDMMC_TCBCNT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_TCBCNT));
	dev_info(host->dev, "SDMMC_TBBCNT: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_TBBCNT));
	dev_info(host->dev, "SDMMC_DEBNCE: \t0x%08x\n", rk29_sdmmc_read(host->regs, SDMMC_DEBNCE));
}
static int rk29_sdmmc_reset_fifo(struct rk29_sdmmc *host)
{
	int tmo;
	int retry = RK29_SDMMC_TMO_COUNT;
	if(!(rk29_sdmmc_read(host->regs, SDMMC_STATUS) & (SDMMC_STAUTS_MC_BUSY|SDMMC_STAUTS_DATA_BUSY)) &&
		(rk29_sdmmc_read(host->regs, SDMMC_STATUS) & SDMMC_STAUTS_FIFO_EMPTY))
		return 0;

	while(retry--) 
	{
		tmo = RK29_SDMMC_TMO_COUNT;
		rk29_sdmmc_write(host->regs, SDMMC_CTRL, rk29_sdmmc_read(host->regs, SDMMC_CTRL) | SDMMC_CTRL_FIFO_RESET);
		while (--tmo && rk29_sdmmc_read(host->regs, SDMMC_CTRL) & SDMMC_CTRL_FIFO_RESET);
		if(rk29_sdmmc_read(host->regs, SDMMC_STATUS) & (SDMMC_STAUTS_MC_BUSY|SDMMC_STAUTS_DATA_BUSY))
			udelay(5);
		else
			break;
	}
	if(retry <= 0){
		dev_dbg(host->dev, "%s error\n", __func__);
		return -1;
	}
	else
		return 0;
}
static int rk29_sdmmc_reset_ctrl(struct rk29_sdmmc *host)
{
	int tmo = RK29_SDMMC_TMO_COUNT;
	/*
	if(!(rk29_sdmmc_read(host->regs, SDMMC_STATUS) & (SDMMC_STAUTS_MC_BUSY|SDMMC_STAUTS_DATA_BUSY)))
		return 0;
	*/
	rk29_sdmmc_write(host->regs, SDMMC_CTRL, (SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET));
	while (--tmo && rk29_sdmmc_read(host->regs, SDMMC_CTRL) & (SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET));
	rk29_sdmmc_write(host->regs, SDMMC_CTRL, rk29_sdmmc_read(host->regs, SDMMC_CTRL) | SDMMC_CTRL_INT_ENABLE);

	if(tmo > 0) {
		rk29_sdmmc_set_mrq_status(host, MRQ_RESET_CTRL_DONE);
		host->is_init = 1;
		return 0;
	}
	else {
		rk29_sdmmc_set_mrq_status(host, MRQ_RESET_CTRL_ERR);
		dev_err(host->dev, "%s error\n", __func__);
		return -1;
	}
}

static int rk29_sdmmc_start_command(struct rk29_sdmmc *host,
		struct mmc_command *cmd, u32 cmd_flags)
{
	int tmo = RK29_SDMMC_TMO_COUNT;
	unsigned long flags;

	local_irq_save(flags);
	
	rk29_sdmmc_write(host->regs, SDMMC_CMDARG, cmd->arg); 
	rk29_sdmmc_write(host->regs, SDMMC_CMD, cmd_flags | SDMMC_CMD_START); 
	local_irq_restore(flags);

	while (--tmo && rk29_sdmmc_read(host->regs, SDMMC_CMD) & SDMMC_CMD_START)
		cpu_relax();
	if(tmo > 0)
		return 0;
	else
		return -1;
}

static int send_stop_cmd(struct rk29_sdmmc *host)
{
	dev_dbg(host->dev,"start cmd:%d ARGR=0x%08x CMDR=0x%08x\n",
						host->mrq->data->stop->opcode, host->mrq->data->stop->arg, host->stop_cmdr);
	if(rk29_sdmmc_start_command(host, host->mrq->data->stop, host->stop_cmdr)) {
		rk29_sdmmc_set_mrq_status(host, MRQ_STOP_START_TMO);
		return -1;
	}
	else {
		rk29_sdmmc_set_mrq_status(host, MRQ_STOP_START_DONE);
		return 0;
	}
}
static void rk29_sdmmc_dma_cleanup(struct rk29_sdmmc *host)
{
	struct mmc_data	*data = host->mrq->data;
	if (data) 
		dma_unmap_sg(host->dev, data->sg, data->sg_len,
		     ((data->flags & MMC_DATA_WRITE)
		      ? DMA_TO_DEVICE : DMA_FROM_DEVICE));		      	
}

static void rk29_sdmmc_stop_dma(struct rk29_sdmmc *host)
{
	int ret = 0;
	rk29_sdmmc_set_mrq_status(host, MRQ_STOP_DMA);
	rk29_sdmmc_dma_cleanup(host);
	ret = rk29_dma_ctrl(host->dma_info.chn,RK29_DMAOP_STOP);
	if(ret < 0)
			dev_err(host->dev, "stop dma:rk29_dma_ctrl stop error\n");
	ret = rk29_dma_ctrl(host->dma_info.chn,RK29_DMAOP_FLUSH);
	if(ret < 0)
			dev_err(host->dev, "stop dma:rk29_dma_ctrl flush error\n");
	rk29_sdmmc_write(host->regs, SDMMC_CTRL, 
			(rk29_sdmmc_read(host->regs, SDMMC_CTRL))&(~SDMMC_CTRL_DMA_ENABLE));
}

static void rk29_sdmmc_request_done(struct rk29_sdmmc *host,struct mmc_request	*mrq)
{
	int tmo = RK29_SDMMC_TMO_COUNT;

	spin_unlock(&host->lock);
	rk29_sdmmc_write(host->regs, SDMMC_RINTSTS, ~SDMMC_INT_SDIO);
	rk29_sdmmc_write(host->regs, SDMMC_INTMASK,
		rk29_sdmmc_read(host->regs, SDMMC_INTMASK) & 
		~(SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | RK29_SDMMC_ERROR_FLAGS));

	if(!rk29_sdmmc_test_mrq_status(host, MRQ_STOP_DMA) && 
		rk29_sdmmc_test_mrq_status(host, MRQ_START_DMA))
		rk29_sdmmc_stop_dma(host);
	if(mrq->stop && !rk29_sdmmc_test_mrq_status(host, MRQ_STOP_START_DONE))
		send_stop_cmd(host);
	if(mrq->cmd->opcode == 17 && (host->data_intsts & SDMMC_INT_SBE)){
		rk29_sdmmc_write(host->regs, SDMMC_CMD, 12|SDMMC_CMD_STOP | SDMMC_CMD_START); 
		while (--tmo && rk29_sdmmc_read(host->regs, SDMMC_CMD) & SDMMC_CMD_START);
	}
	if(rk29_sdmmc_read(host->regs, SDMMC_STATUS) & SDMMC_STAUTS_FIFO_FULL ){
		rk29_sdmmc_read(host->regs, SDMMC_DATA);
		rk29_sdmmc_read(host->regs, SDMMC_DATA);
	}
	//if(mrq->data && mrq->data->error)
		rk29_sdmmc_reset_fifo(host);
	
	host->mrq = NULL;
	host->state = STATE_IDLE;
	rk29_sdmmc_set_mrq_status(host, MRQ_REQUEST_DONE);
	mmc_request_done(host->mmc, mrq);
	del_timer(&host->monitor_timer);
	
	spin_lock(&host->lock);
}

static int sdmmc_send_cmd(struct rk29_sdmmc *host, unsigned int cmd, int arg)
{
	int tmo = RK29_SDMMC_TMO_COUNT;
	rk29_sdmmc_write(host->regs, SDMMC_CMDARG, arg);
	rk29_sdmmc_write(host->regs, SDMMC_CMD, SDMMC_CMD_START | cmd);		
	while (--tmo && readl(host->regs + SDMMC_CMD) & SDMMC_CMD_START);
	if(tmo > 0)
		return 0;
	else
		return -1;
}
static int rk29_sdmmc_get_div(unsigned int bus_hz, unsigned int ios_clock)
{
     unsigned int div, real_clock;

	 if(ios_clock >= bus_hz)
		 return 0;
	for(div = 1; div < 255; div++)
	{
		real_clock = bus_hz/(2*div);
		if(real_clock <= ios_clock)
			break;
	}
	if(div > 255)
		div = 255;
	return div;
}

int rk29_sdmmc_set_clock(struct rk29_sdmmc *host)
{
	unsigned int div;
	if(!host->ios_clock)
		return 0;
	//div  = (((host->bus_hz + (host->bus_hz / 5)) / host->ios_clock)) >> 1;
	//if(host->mrq && host->mrq->cmd->opcode == 25)
		//host->ios_clock = 500000;
	div = rk29_sdmmc_get_div(host->bus_hz, host->ios_clock);
/*
	if(div == 0)
		div = 1;
*/
	if(host->div == div)
		return 0;
	dev_info(host->dev, "div = %u, bus_hz = %u, ios_clock = %u\n",
		div, host->bus_hz, host->ios_clock);
	rk29_sdmmc_write(host->regs, SDMMC_CLKENA, 0);
	rk29_sdmmc_write(host->regs, SDMMC_CLKSRC,0);
	if(sdmmc_send_cmd(host, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0))
		goto send_cmd_err;
	rk29_sdmmc_write(host->regs, SDMMC_CLKDIV, div);
	host->div = div;
	host->clock = (div == 0)? host->bus_hz :(host->bus_hz / div) >> 1;
	if(sdmmc_send_cmd(host, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0))
		goto send_cmd_err;
	rk29_sdmmc_write(host->regs, SDMMC_CLKENA, SDMMC_CLKEN_ENABLE);
	if(sdmmc_send_cmd(host, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0))
		goto send_cmd_err;

	rk29_sdmmc_set_mrq_status(host, MRQ_CLK_START_DONE);
	return 0;
send_cmd_err:
	rk29_sdmmc_set_mrq_status(host, MRQ_CLK_START_TMO);
	return -1;	
}

static void rk29_sdmmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct rk29_sdmmc *host = mmc_priv(mmc);;


	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		rk29_sdmmc_write(host->regs, SDMMC_CTYPE, 0);
		break;
	case MMC_BUS_WIDTH_4:
		rk29_sdmmc_write(host->regs, SDMMC_CTYPE, SDMMC_CTYPE_4BIT);
		break;
	}
	host->ios_clock = ios->clock;
	
	switch (ios->power_mode) {
	case MMC_POWER_UP:
		host->is_init = 1;
		rk29_sdmmc_write(host->regs, SDMMC_PWREN, 1);
		break;
	default:
		break;
	}
}

static int rk29_sdmmc_get_ro(struct mmc_host *mmc)
{
	struct rk29_sdmmc *host = mmc_priv(mmc);
	u32 wrtprt = rk29_sdmmc_read(host->regs, SDMMC_WRTPRT);

	return (wrtprt & SDMMC_WRITE_PROTECT)?1:0;
}


static int rk29_sdmmc_get_cd(struct mmc_host *mmc)
{
	struct rk29_sdmmc *host = mmc_priv(mmc);

	if(host->is_sdio)
		return host->get_wifi_cd(mmc_dev(host->mmc));
	else if(host->gpio_det == INVALID_GPIO)
		return 1;
	else
		return gpio_get_value(host->gpio_det)?0:1;
}

static inline unsigned ns_to_clocks(unsigned clkrate, unsigned ns)
{
	u32 clks;
	if (clkrate > 1000000)
		clks =  (ns * (clkrate / 1000000) + 999) / 1000;
	else
		clks =  ((ns/1000) * (clkrate / 1000) + 999) / 1000;

	return clks;
}

static void rk29_sdmmc_set_timeout(struct rk29_sdmmc *host,struct mmc_data *data)
{
	unsigned timeout;
	unsigned int clock;
	
	if(host->div == -1)
		return;
	else if(host->div == 0)
		clock = host->bus_hz;
	else
		clock = (host->bus_hz / host->div) >> 1;
	timeout = ns_to_clocks(clock, data->timeout_ns) + data->timeout_clks;
	rk29_sdmmc_write(host->regs, SDMMC_TMOUT, 0xffffffff);
	//rk29_sdmmc_write(host->regs, SDMMC_TMOUT, (timeout << 8) | (70));
}
static u32 rk29_sdmmc_prepare_command(struct mmc_host *mmc,
				 struct mmc_command *cmd)
{
	struct mmc_data	*data;
	u32		cmdr;
	
	cmd->error = -EINPROGRESS;
	cmdr = cmd->opcode;

	if(cmdr == 12) 
		cmdr |= SDMMC_CMD_STOP;
	else if(cmdr == 13) 
		cmdr &= ~SDMMC_CMD_PRV_DAT_WAIT;
	else 
		cmdr |= SDMMC_CMD_PRV_DAT_WAIT;

	if (cmd->flags & MMC_RSP_PRESENT) {
		cmdr |= SDMMC_CMD_RESP_EXP; // expect the respond, need to set this bit
		if (cmd->flags & MMC_RSP_136) 
			cmdr |= SDMMC_CMD_RESP_LONG; // expect long respond
		
		if(cmd->flags & MMC_RSP_CRC) 
			cmdr |= SDMMC_CMD_RESP_CRC;
	}

	data = cmd->data;
	if (data) {
		cmdr |= SDMMC_CMD_DAT_EXP;
		if (data->flags & MMC_DATA_STREAM) 
			cmdr |= SDMMC_CMD_STRM_MODE; //  set stream mode
		if (data->flags & MMC_DATA_WRITE) 
		    cmdr |= SDMMC_CMD_DAT_WR;
	}
	return cmdr;
}
static int rk29_sdmmc_submit_data(struct rk29_sdmmc *host, struct mmc_data *data)
{
	struct scatterlist		*sg;
	unsigned int			i,direction;
	int dma_len=0, ret = 0;

	if (data->blksz & 3){
		dev_info(host->dev, "data->blksz = %d\n", data->blksz);
		return -EINVAL;
	}
	for_each_sg(data->sg, sg, data->sg_len, i) {
		if (sg->offset & 3 || sg->length & 3){
			dev_info(host->dev, "sg->offset = %d, sg->length = %d\n",
				sg->offset, sg->length);
			return -EINVAL;
		}
	}
	if (data->flags & MMC_DATA_READ)
		direction = RK29_DMASRC_HW;  
	else
		direction = RK29_DMASRC_MEM;  
	
	ret = rk29_dma_ctrl(host->dma_info.chn,RK29_DMAOP_STOP);
	if(ret < 0)
			dev_err(host->dev, "rk29_dma_ctrl stop error\n");
	ret = rk29_dma_ctrl(host->dma_info.chn,RK29_DMAOP_FLUSH);	
	if(ret < 0)
			dev_err(host->dev, "rk29_dma_ctrl flush error\n");
    ret = rk29_dma_devconfig(host->dma_info.chn, direction, (unsigned long )(host->dma_addr));
	if(ret < 0)
			dev_err(host->dev, "rk29_dma_devconfig error\n");
	dma_len = dma_map_sg(host->dev, data->sg, data->sg_len, 
			(data->flags & MMC_DATA_READ)? DMA_FROM_DEVICE : DMA_TO_DEVICE);						                	   
	for (i = 0; i < dma_len; i++) {                             
    	ret = rk29_dma_enqueue(host->dma_info.chn, host, sg_dma_address(&data->sg[i]),sg_dma_len(&data->sg[i]));  // data->sg->dma_address, data->sg->length);	    	
		if(ret < 0)
			dev_err(host->dev, "rk29 dma enqueue error\n");
	}
	rk29_sdmmc_write(host->regs, SDMMC_CTRL, (rk29_sdmmc_read(host->regs, SDMMC_CTRL))|SDMMC_CTRL_DMA_ENABLE);// enable dma
	ret = rk29_dma_ctrl(host->dma_info.chn, RK29_DMAOP_START);
	if(ret < 0)
			dev_err(host->dev, "rk29_dma_ctrl start error\n");
	rk29_sdmmc_set_mrq_status(host, MRQ_START_DMA);
	return 0;
}
static int rk29_sdmmc_test_cmd_start(struct rk29_sdmmc *host)
{
	return rk29_sdmmc_read(host->regs, SDMMC_CMD) & SDMMC_CMD_START;
}
static int rk29_sdmmc_start_request(struct rk29_sdmmc *host,struct mmc_request *mrq)
{
	int ret = 0;
	struct mmc_command	*cmd;
	u32	cmdflags;
	
	BUG_ON(host->state != STATE_IDLE);
	
	spin_lock(&host->lock);

	if(rk29_sdmmc_test_cmd_start(host)){
		dev_info(host->dev, "cmd_start bit is set ,reset ctroller\n");
		ret = rk29_sdmmc_reset_ctrl(host);
	}
	if(ret < 0)
		goto start_err;
	
	host->state = STATE_SENDING_CMD;
	
	if (mrq->data) {
		rk29_sdmmc_set_timeout(host,mrq->data);
		rk29_sdmmc_write(host->regs, SDMMC_BYTCNT,mrq->data->blksz*mrq->data->blocks);
		rk29_sdmmc_write(host->regs, SDMMC_BLKSIZ,mrq->data->blksz);
	}
	cmd = mrq->cmd;
	cmdflags = rk29_sdmmc_prepare_command(host->mmc, cmd);
	if (host->is_init) {
		host->is_init = 0;
	    cmdflags |= SDMMC_CMD_INIT; 
	}

	if (mrq->data) {
		rk29_sdmmc_set_mrq_status(host, MRQ_HAS_DATA);
		ret = rk29_sdmmc_submit_data(host, mrq->data);
	}
	if(ret < 0) {
		rk29_sdmmc_set_mrq_status(host, MRQ_DMA_SET_ERR);
		goto start_err;
	}
	dev_dbg(host->dev,"start cmd:%d ARGR=0x%08x CMDR=0x%08x\n",
						cmd->opcode, cmd->arg, cmdflags);

	rk29_sdmmc_write(host->regs, SDMMC_RINTSTS, ~SDMMC_INT_SDIO);
	rk29_sdmmc_write(host->regs, SDMMC_INTMASK,
		rk29_sdmmc_read(host->regs, SDMMC_INTMASK) | 
		(SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | RK29_SDMMC_ERROR_FLAGS));
	ret = rk29_sdmmc_start_command(host, cmd, cmdflags);
	if(ret < 0) {
		rk29_sdmmc_set_mrq_status(host, MRQ_CMD_START_TMO);
		goto start_err;
	}
	rk29_sdmmc_set_mrq_status(host, MRQ_CMD_START_DONE);
	if (mrq->stop) {
		rk29_sdmmc_set_mrq_status(host, MRQ_HAS_STOP);
		host->stop_cmdr = rk29_sdmmc_prepare_command(host->mmc, mrq->stop);
		if(mrq->cmd->opcode == 25)
			host->stop_cmdr |= SDMMC_CMD_DAT_WR;
	}
	spin_unlock(&host->lock);
	return 0;
start_err:
	spin_unlock(&host->lock);
	return ret;
}

static void rk29_sdmmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	int timeout;
	struct rk29_sdmmc *host = mmc_priv(mmc);

	if(!mrq)
		dev_info(host->dev, "mrq = NULL!!!!!\n");
	if(host->mrq){
				dev_info(host->dev, "%s-> host->mrq = NULL\n", __func__);
				rk29_sdmmc_show_info(host);
			}
	if((!rk29_sdmmc_test_mrq_status(host, MRQ_STOP_DMA) && 
		rk29_sdmmc_test_mrq_status(host, MRQ_START_DMA)) ||
		(rk29_sdmmc_test_mrq_status(host, MRQ_STOP_DMA) && 
		!rk29_sdmmc_test_mrq_status(host, MRQ_START_DMA)))
		dev_warn(host->dev, "start_dma but no stop_dma, or no start_dma but stop_dma\n");
	WARN_ON(host->mrq);
	host->old_mrq_status = host->mrq_status;
	host->mrq_status = 0;
	host->pending_events = 0;
	host->completed_events= 0;
	host->cmd_intsts = 0;
	host->data_intsts = 0;
	host->mrq = mrq;

	if(!mrq->data)
		timeout = 5000;
	else
		timeout = 5000 + mrq->data->timeout_ns/1000000;
	mod_timer(&host->monitor_timer, jiffies + msecs_to_jiffies(timeout));
	
	if (!rk29_sdmmc_get_cd(mmc)) {
		mrq->cmd->error = -ENOMEDIUM;
		rk29_sdmmc_request_done(host, mrq);
		dev_dbg(host->dev, "mrq_status = 0x%08lx\n", host->mrq_status);
		return;
	}

	if(rk29_sdmmc_set_clock(host)) {
		mrq->cmd->error = -EINPROGRESS;
		dev_info(host->dev, "rk29_sdmmc_set_clock timeout, ios_clock = %d, clock = %d\n", host->ios_clock, host->clock);
		rk29_sdmmc_request_done(host, mrq);
		rk29_sdmmc_reset_ctrl(host);
		rk29_sdmmc_show_info(host);
		return;
	}
	if(rk29_sdmmc_start_request(host,mrq)) {
		dev_info(host->dev, "rk29_sdmmc_start_request timeout\n");
		mrq->cmd->error = -EINPROGRESS;
		rk29_sdmmc_request_done(host, mrq);
		rk29_sdmmc_reset_ctrl(host);
		rk29_sdmmc_show_info(host);
	}
	return;
}

static void rk29_sdmmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	u32 intmask;
	unsigned long flags;
	struct rk29_sdmmc *host = mmc_priv(mmc);
	
	spin_lock_irqsave(&host->lock, flags);
	intmask = rk29_sdmmc_read(host->regs, SDMMC_INTMASK);
	if(enable)
		rk29_sdmmc_write(host->regs, SDMMC_INTMASK, intmask | SDMMC_INT_SDIO);
	else
		rk29_sdmmc_write(host->regs, SDMMC_INTMASK, intmask & ~SDMMC_INT_SDIO);
	spin_unlock_irqrestore(&host->lock, flags);
}

static void  rk29_sdmmc_init_card(struct mmc_host *mmc, struct mmc_card *card)
{
        card->quirks = MMC_QUIRK_BLKSZ_FOR_BYTE_MODE;

}
static const struct mmc_host_ops rk29_sdmmc_ops[] = {
	{
		.request	= rk29_sdmmc_request,
		.set_ios	= rk29_sdmmc_set_ios,
		.get_ro		= rk29_sdmmc_get_ro,
		.get_cd		= rk29_sdmmc_get_cd,
	},
	{
		.request	= rk29_sdmmc_request,
		.set_ios	= rk29_sdmmc_set_ios,
		.enable_sdio_irq = rk29_sdmmc_enable_sdio_irq,
        .init_card       = rk29_sdmmc_init_card,
	},
};

static void rk29_sdmmc_request_end(struct rk29_sdmmc *host)
	__releases(&host->lock)
	__acquires(&host->lock)
{
	if(host->mrq)
		rk29_sdmmc_request_done(host, host->mrq);
}

static void rk29_sdmmc_command_complete(struct rk29_sdmmc *host,
	struct mmc_command *cmd)
{
	unsigned int intsts = host->cmd_intsts;

	host->cmd_intsts = 0;
	if(cmd->flags & MMC_RSP_PRESENT) {

	    if(cmd->flags & MMC_RSP_136) {
		cmd->resp[3] = rk29_sdmmc_read(host->regs, SDMMC_RESP0);
		cmd->resp[2] = rk29_sdmmc_read(host->regs, SDMMC_RESP1);
		cmd->resp[1] = rk29_sdmmc_read(host->regs, SDMMC_RESP2);
		cmd->resp[0] = rk29_sdmmc_read(host->regs, SDMMC_RESP3);
	    } else {
	        cmd->resp[0] = rk29_sdmmc_read(host->regs, SDMMC_RESP0);
			cmd->resp[1] = 0;
			cmd->resp[2] = 0;
			cmd->resp[3] = 0;
	    }
	}

	if (intsts & SDMMC_INT_RTO)
		cmd->error = -ETIMEDOUT;
	else if ((cmd->flags & MMC_RSP_CRC) && (intsts & SDMMC_INT_RCRC))
		cmd->error = -EILSEQ;
	else if (intsts & SDMMC_INT_RE)
		cmd->error = -EIO;
	else if(intsts & SDMMC_INT_HLE)
		cmd->error = -EIO;
	else
		cmd->error = 0;

	if (cmd->error) {
		dev_dbg(host->dev,
			"command error: status=0x%08x resp=0x%08x\n"
			"cmd=0x%08x arg=0x%08x flg=0x%08x err=%d\n", 
			intsts, cmd->resp[0], 
			cmd->opcode, cmd->arg, cmd->flags, cmd->error);

		if (cmd->data) {
			rk29_sdmmc_stop_dma(host);
		}
	} 
}

static void rk29_sdmmc_tasklet_func(unsigned long priv)
{
	struct rk29_sdmmc	*host = (struct rk29_sdmmc *)priv;
	enum rk29_sdmmc_state	state;
	enum rk29_sdmmc_state	prev_state;
	unsigned int			intsts;

	spin_lock(&host->lock);
	state = host->state;

	do {
		prev_state = state;
		switch (state) {
		case STATE_IDLE:
			break;

		case STATE_SENDING_CMD:			
			if (!rk29_sdmmc_test_and_clear_pending(host,
						EVENT_CMD_COMPLETE))
				break;
			rk29_sdmmc_set_completed(host, EVENT_CMD_COMPLETE);
			if(!host->mrq){
				dev_info(host->dev, "sending cmd, host->mrq = NULL\n");
				rk29_sdmmc_show_info(host);
			}else{
				rk29_sdmmc_command_complete(host, host->mrq->cmd);
				if (!host->mrq->data || (host->mrq->cmd->error)) {
					rk29_sdmmc_request_end(host);
					goto unlock;
				}
			prev_state = state = STATE_SENDING_DATA;
			}
		case STATE_SENDING_DATA:
			if (rk29_sdmmc_test_and_clear_pending(host,
						EVENT_DATA_ERROR)) {
				if(!host->mrq){
					dev_info(host->dev, "sending data, host->mrq = NULL\n");
					rk29_sdmmc_show_info(host);
				}
				if(!rk29_sdmmc_test_mrq_status(host, MRQ_DMA_DONE) && 
		rk29_sdmmc_test_mrq_status(host, MRQ_START_DMA))
			dev_info(host->dev, "dma is running...\n");
				rk29_sdmmc_stop_dma(host);
				if (host->mrq->data->stop)
					send_stop_cmd(host);
				state = STATE_DATA_ERROR;
				break;
			}
			prev_state = state = STATE_DATA_BUSY;

		case STATE_DATA_BUSY:
			if (!rk29_sdmmc_test_and_clear_pending(host,EVENT_DATA_COMPLETE) &&
				!(host->data_intsts & SDMMC_INT_SBE))
				break;	
			
			rk29_sdmmc_set_completed(host, EVENT_DATA_COMPLETE);
			intsts = host->data_intsts;
			if(!host->mrq){
				dev_info(host->dev, "%s-> host->mrq = NULL\n", __func__);
				rk29_sdmmc_show_info(host);
			}
			if(host->mrq->data) {
				if (unlikely(intsts & RK29_SDMMC_DATA_ERROR_FLAGS)) {
					if (intsts & SDMMC_INT_DRTO) {
						dev_err(host->dev,"data timeout error\n");
						host->mrq->data->error = -ETIMEDOUT;
					} else if (intsts & SDMMC_INT_DCRC) {
						dev_err(host->dev,"data CRC error\n");
						host->mrq->data->error = -EILSEQ;
					} else if (intsts & SDMMC_INT_SBE) {
						dev_err(host->dev,"data start bit error\n");
						host->mrq->data->error = -EILSEQ;
					}else {
						dev_err(host->dev,"data FIFO error (status=%08x)\n",intsts);
						host->mrq->data->error = -EIO;
					}
					rk29_sdmmc_show_info(host);
				}else {
					host->mrq->data->bytes_xfered = host->mrq->data->blocks * host->mrq->data->blksz;
					host->mrq->data->error = 0;
				}
			}
			if (!host->mrq->data->stop) {
				rk29_sdmmc_request_end(host);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_STOP;
			if (host->mrq->data && !host->mrq->data->error)
				send_stop_cmd(host);
			/* fall through */

		case STATE_SENDING_STOP:
			if (!rk29_sdmmc_test_and_clear_pending(host,
						EVENT_CMD_COMPLETE))
				break;
			if(!host->mrq){
				dev_info(host->dev, "%s-> host->mrq = NULL\n", __func__);
				rk29_sdmmc_show_info(host);
			}
			if(host->mrq->stop)
				rk29_sdmmc_command_complete(host, host->mrq->stop);
			rk29_sdmmc_request_end(host);
			goto unlock;
		case STATE_DATA_ERROR:
			state = STATE_DATA_BUSY;
			break;
		}
	} while (state != prev_state);

	host->state = state;

unlock:
	spin_unlock(&host->lock);

}



static irqreturn_t rk29_sdmmc_isr(int irq, void *dev_id)
{
	struct rk29_sdmmc *host = dev_id;
	unsigned int intsts;

	intsts = rk29_sdmmc_read(host->regs, SDMMC_MINTSTS);

	spin_lock(&host->lock);
	if(intsts & RK29_SDMMC_CMD_ERROR_FLAGS) {
		rk29_sdmmc_set_mrq_status(host, MRQ_INT_CMD_ERR);
	    rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,RK29_SDMMC_CMD_ERROR_FLAGS); 
	    host->cmd_intsts = intsts;
	    smp_wmb();
	    rk29_sdmmc_set_pending(host, EVENT_CMD_COMPLETE);
		if(!host->mrq){
				dev_info(host->dev, "%s-> host->mrq = NULL\n", __func__);
				rk29_sdmmc_show_info(host);
			}
		else
			dev_info(host->dev, "[cmd%d] cmd error(intsts 0x%x, host->state %d, pending_events %ld)\n", 
				host->mrq->cmd->opcode,intsts,host->state,host->pending_events);
		tasklet_schedule(&host->tasklet);
	}

	if (intsts & RK29_SDMMC_DATA_ERROR_FLAGS) {
		rk29_sdmmc_set_mrq_status(host, MRQ_INT_DATA_ERR);
		rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,RK29_SDMMC_DATA_ERROR_FLAGS);
		host->data_intsts = intsts;
		smp_wmb();
		rk29_sdmmc_set_pending(host, EVENT_DATA_ERROR);
		if(!host->mrq){
				dev_info(host->dev, "%s-> host->mrq = NULL\n", __func__);
				rk29_sdmmc_show_info(host);
			}
		else
			dev_info(host->dev, "[cmd%d] data error(intsts 0x%x, host->state %d, pending_events %ld)\n", 
				host->mrq->cmd->opcode, intsts,host->state,host->pending_events);
		tasklet_schedule(&host->tasklet);
	}

	if(intsts & SDMMC_INT_DTO) {
		rk29_sdmmc_set_mrq_status(host, MRQ_INT_DATA_DONE);
	    rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_DTO); 
	    if (!host->data_intsts)
			host->data_intsts = intsts;
	    smp_wmb();
	    rk29_sdmmc_set_pending(host, EVENT_DATA_COMPLETE);
	    tasklet_schedule(&host->tasklet);
	}
	if (intsts & SDMMC_INT_CMD_DONE) {
		rk29_sdmmc_set_mrq_status(host, MRQ_INT_CMD_DONE);
	    rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_CMD_DONE);
	    if(!host->cmd_intsts) 
			host->cmd_intsts = intsts;

		smp_wmb();
		rk29_sdmmc_set_pending(host, EVENT_CMD_COMPLETE);
		tasklet_schedule(&host->tasklet);
	}
	if(host->is_sdio && (intsts & SDMMC_INT_SDIO)) {
		rk29_sdmmc_set_mrq_status(host, MRQ_INT_SDIO);
		rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_SDIO);
		mmc_signal_sdio_irq(host->mmc);
	}
	spin_unlock(&host->lock);
	return IRQ_HANDLED;
}

static void rk29_sdmmc_dma_complete(void *arg, int size, enum rk29_dma_buffresult result)
{
	struct rk29_sdmmc	*host = arg;
	
	dev_dbg(host->dev, "DMA complete\n");
	rk29_sdmmc_set_mrq_status(host, MRQ_DMA_DONE);
}

static void rk29_sdmmc_detect_change(struct rk29_sdmmc *host)
{
	spin_lock(&host->lock);	

	del_timer(&host->monitor_timer);
	rk29_sdmmc_write(host->regs, SDMMC_RINTSTS, ~SDMMC_INT_SDIO);
	rk29_sdmmc_write(host->regs, SDMMC_INTMASK,
		rk29_sdmmc_read(host->regs, SDMMC_INTMASK) & 
		~(SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | RK29_SDMMC_ERROR_FLAGS));
	if (host->mrq) {
		switch (host->state) {
		case STATE_IDLE:
			break;
		case STATE_SENDING_CMD:
			if(host->mrq->cmd)
				host->mrq->cmd->error = -ENOMEDIUM;
			if (!host->mrq->data)
				break;
			/* fall through */
		case STATE_SENDING_DATA:
			if(host->mrq->data)
				host->mrq->data->error = -ENOMEDIUM;
			rk29_sdmmc_stop_dma(host);
			break;
		case STATE_DATA_BUSY:
		case STATE_DATA_ERROR:
			if (host->mrq->data && host->mrq->data->error == -EINPROGRESS)
				host->mrq->data->error = -ENOMEDIUM;
			if (!host->mrq->stop)
				break;
			/* fall through */
		case STATE_SENDING_STOP:
			if(host->mrq->stop) {
				host->mrq->stop->error = -ENOMEDIUM;
			}
			break;
		}
		rk29_sdmmc_request_end(host);
	}
	rk29_sdmmc_reset_fifo(host);
	spin_unlock(&host->lock);
	mmc_detect_change(host->mmc, 0);
}

static void rk29_sdmmc1_status_notify_cb(int card_present, void *dev_id)
{
    struct rk29_sdmmc *host = dev_id;

    card_present = rk29_sdmmc_get_cd(host->mmc);
    dev_info(host->dev, "sdio change detected,status is %d\n",card_present);
	
    rk29_sdmmc_detect_change(host);
}

static void rk29_sdmmc_get_dma_dma_info(struct rk29_sdmmc *host)
{
	if(host->is_sdio)
		host->dma_info = dma_infos[1];
	else
		host->dma_info = dma_infos[0];
}

static irqreturn_t rk29_sdmmc_detect_change_isr(int irq, void *dev_id);
static void rk29_sdmmc_detect_change_work(struct work_struct *work)
{
	int ret;
    struct rk29_sdmmc *host =  container_of(work, struct rk29_sdmmc, work.work);

	dev_info(host->dev, "sd detect change, card is %s\n", 
		rk29_sdmmc_get_cd(host->mmc)?"inserted":"removed");
	if(host->enable_sd_warkup && rk29_sdmmc_get_cd(host->mmc))
		rk28_send_wakeup_key();
	rk29_sdmmc_detect_change(host);

	free_irq(host->gpio_irq, host);
	ret = request_irq(host->gpio_irq,
    				 rk29_sdmmc_detect_change_isr,
                	 rk29_sdmmc_get_cd(host->mmc)?IRQF_TRIGGER_RISING : IRQF_TRIGGER_FALLING,
                	 "sd_detect",
                	 host);
	if(ret < 0)
		dev_err(host->dev, "gpio request_irq error\n");
}
static irqreturn_t rk29_sdmmc_detect_change_isr(int irq, void *dev_id)
{
	struct rk29_sdmmc *host = dev_id;

	disable_irq_nosync(host->gpio_irq);
	if(rk29_sdmmc_get_cd(host->mmc))
		schedule_delayed_work(&host->work, msecs_to_jiffies(500));
	else
		schedule_delayed_work(&host->work, 0);
	
	return IRQ_HANDLED;
}
static void rk29_sdmmc_monitor_timer(unsigned long data)
{
	struct rk29_sdmmc *host = (struct rk29_sdmmc *)data;
	
	if(!rk29_sdmmc_test_mrq_status(host, MRQ_REQUEST_DONE)){
		dev_info(host->dev, "no dto interrupt\n");
		rk29_sdmmc_show_info(host);
		host->mrq->cmd->error = -ETIMEDOUT;
		if(host->mrq->data)
			host->mrq->data->error = -ETIMEDOUT;
		rk29_sdmmc_request_end(host);
		//rk29_sdmmc_reset_ctrl(host);
	}
	
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static void rk29_sdmmc_early_suspend(struct early_suspend *h)
{
	struct rk29_sdmmc *host = container_of(h,
							struct rk29_sdmmc,
							early_suspend);

	dev_dbg(host->dev, "Enter rk29_sdmmc_early_suspend\n");
}

static void rk29_sdmmc_early_resume(struct early_suspend *h)
{
	struct rk29_sdmmc *host = container_of(h,
							struct rk29_sdmmc,
							early_suspend);

	dev_dbg(host->dev, "Exit rk29_sdmmc_early_resume\n");
}
#endif


static int rk29_sdmmc_probe(struct platform_device *pdev)
{
	struct mmc_host 		*mmc;
	struct rk29_sdmmc		*host;
	struct resource			*regs;
	struct rk29_sdmmc_platform_data *pdata;
	int				ret = 0;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "Platform data missing\n");
		return -ENODEV;
	}
	if(pdata->io_init)
		pdata->io_init();

	mmc = mmc_alloc_host(sizeof(struct rk29_sdmmc), &pdev->dev);
	if (!mmc)
		return -ENOMEM;	

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->dev = &pdev->dev;
	host->mrq = NULL;
	host->state = STATE_IDLE;
	host->div = -1;
	host->is_init = 1;
	host->is_sdio = rk29_sdmmc_is_sdio(pdata);

	if(host->is_sdio)
		sdio_host = host;
	host->get_wifi_cd = pdata->status;

	host->irq = platform_get_irq(pdev, 0);
	if (host->irq < 0) {
		dev_err(&pdev->dev, "platform_get_irq error\n");
		ret = host->irq;
		goto err_mmc_free_host;
	}
	
	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs) {
		dev_err(&pdev->dev, "platform_get_resource error\n");
		ret = -ENXIO;
		goto err_mmc_free_host;
	}
	
	host->regs = ioremap(regs->start, regs->end - regs->start);
	if (!host->regs){
		dev_err(&pdev->dev, "ioremap error\n");
		ret = ENXIO;
	    goto err_mmc_free_host; 
	}
	spin_lock_init(&host->lock);

	/* dma init */
	rk29_sdmmc_get_dma_dma_info(host);
	ret = rk29_dma_request(host->dma_info.chn, &(host->dma_info.client), NULL); 
	if (ret < 0){
		dev_err(&pdev->dev, "rk29_dma_request error\n");
	    goto err_iounmap; 
	}
	ret = rk29_dma_config(host->dma_info.chn, 4, 1);

	if (ret < 0){
		dev_err(&pdev->dev, "rk29_dma_config error\n");
	    //goto err_rk29_dma_free; 
	}
	ret = rk29_dma_set_buffdone_fn(host->dma_info.chn, rk29_sdmmc_dma_complete);
	if (ret < 0){
		dev_err(&pdev->dev, "rk29_dma_set_buffdone_fn error\n");
	    goto err_rk29_dma_free; 
	}
	host->dma_addr = regs->start + SDMMC_DATA;

	/* clk init */
	host->clk = clk_get(&pdev->dev, "mmc");
	if(host->is_sdio)
		clk_set_rate(host->clk,RK29_SDIO_CLK * 1000000);
	else
		clk_set_rate(host->clk,RK29_SDCARD_CLK * 1000000);
	clk_enable(host->clk);
	clk_enable(clk_get(&pdev->dev, "hclk_mmc"));
	host->bus_hz = clk_get_rate(host->clk); 

	/* reset all blocks */
  	rk29_sdmmc_write(host->regs, SDMMC_CTRL,(SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET));
  	/* wait till resets clear */
  	while (rk29_sdmmc_read(host->regs, SDMMC_CTRL) & (SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET));
	 /* Clear the interrupts for the host controller */
	rk29_sdmmc_write(host->regs, SDMMC_RINTSTS, 0xFFFFFFFF);
	rk29_sdmmc_write(host->regs, SDMMC_INTMASK, 0); // disable all mmc interrupt first
  	/* Put in max timeout */
  	rk29_sdmmc_write(host->regs, SDMMC_TMOUT, 0xFFFFFFFF);

  	/* FIFO threshold settings  */
  	rk29_sdmmc_write(host->regs, SDMMC_FIFOTH, ((0x3 << 28) | (0x0f << 16) | (0x10 << 0))); // RXMark = 15, TXMark = 16, DMA Size = 16
	/* disable clock to CIU */
	rk29_sdmmc_write(host->regs, SDMMC_CLKENA,0);
	rk29_sdmmc_write(host->regs, SDMMC_CLKSRC,0);
	rk29_sdmmc_write(host->regs, SDMMC_PWREN, 1);

	ret = request_irq(host->irq, rk29_sdmmc_isr, 0, dev_name(&pdev->dev), host);
	if (ret < 0){
		dev_err(&pdev->dev, "request_irq error\n");
	    goto err_rk29_dma_free; 
	}

	/* card insert flags init*/  
    if (pdata->register_status_notify) {
        pdata->register_status_notify(rk29_sdmmc1_status_notify_cb, host);
    }

	/* add host */
	if(host->is_sdio)
		mmc->ops = &rk29_sdmmc_ops[1];
	else
		mmc->ops = &rk29_sdmmc_ops[0];
	if (host->is_sdio) 
		mmc->pm_flags = MMC_PM_IGNORE_PM_NOTIFY;   //ignore pm notify   
	
	mmc->f_min = DIV_ROUND_UP(host->bus_hz, 510);
	mmc->f_max = host->bus_hz; 
	mmc->ocr_avail = pdata->host_ocr_avail;
	mmc->caps = pdata->host_caps;
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 37))
	mmc->max_segs = 64;
#else
	mmc->max_phys_segs = 64;
	mmc->max_hw_segs = 64;
#endif
	mmc->max_blk_size = 4096; 
	mmc->max_blk_count = 65535; 
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_seg_size = mmc->max_req_size;	
	
	ret = mmc_add_host(mmc);
	if (ret < 0){
		dev_err(&pdev->dev, "mmc_add_host error\n");
	    goto err_free_irq; 
	}
	
#if defined (CONFIG_DEBUG_FS)
	rk29_sdmmc_init_debugfs(host);
#endif
	tasklet_init(&host->tasklet, rk29_sdmmc_tasklet_func, (unsigned long)host);
	setup_timer(&host->monitor_timer, rk29_sdmmc_monitor_timer,(unsigned long)host);
	
	host->gpio_det = pdata->detect_irq;
	if(!host->is_sdio && host->gpio_det != INVALID_GPIO) {
		INIT_DELAYED_WORK(&host->work, rk29_sdmmc_detect_change_work);
		ret = gpio_request(host->gpio_det, "sd_detect");
		if(ret < 0) {
			dev_err(&pdev->dev, "gpio_request error\n");
			goto err_mmc_remove_host;
		}
		gpio_direction_input(host->gpio_det);
		host->gpio_irq = gpio_to_irq(host->gpio_det);

		ret = request_irq(host->gpio_irq,
                		  rk29_sdmmc_detect_change_isr,
                		  rk29_sdmmc_get_cd(host->mmc)?IRQF_TRIGGER_RISING : IRQF_TRIGGER_FALLING,
                		  "sd_detect",
                		  host);
		if(ret < 0) {
			dev_err(&pdev->dev, "gpio request_irq error\n");
			goto err_gpio_free;
		}
		host->enable_sd_warkup = pdata->enable_sd_wakeup;
		if(host->enable_sd_warkup)
			enable_irq_wake(host->gpio_irq);
	}
	platform_set_drvdata(pdev, host);
	rk29_sdmmc_write(host->regs, SDMMC_CTYPE, 0);
	rk29_sdmmc_write(host->regs, SDMMC_RINTSTS, 0xFFFFFFFF);
	rk29_sdmmc_write(host->regs, SDMMC_INTMASK,SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | RK29_SDMMC_ERROR_FLAGS);
	rk29_sdmmc_write(host->regs, SDMMC_CTRL,SDMMC_CTRL_INT_ENABLE);
	rk29_sdmmc_write(host->regs, SDMMC_CLKENA,1);
#ifdef CONFIG_HAS_EARLYSUSPEND
	host->early_suspend.suspend = rk29_sdmmc_early_suspend;
	host->early_suspend.resume = rk29_sdmmc_early_resume;
	host->early_suspend.level = EARLY_SUSPEND_LEVEL_BLANK_SCREEN + 1;
	register_early_suspend(&host->early_suspend);
#endif	
	dev_info(host->dev, "RK29 SDMMC controller at irq %d\n", host->irq);
	return 0;
	free_irq(host->gpio_irq, host);
err_gpio_free:
	gpio_free(host->gpio_det);
err_mmc_remove_host:
	mmc_remove_host(host->mmc);
err_free_irq:
	free_irq(host->irq, host);
err_rk29_dma_free:
	rk29_dma_free(host->dma_info.chn, &host->dma_info.client);
err_iounmap:	
	iounmap(host->regs);
err_mmc_free_host:
	mmc_free_host(host->mmc);

	while(1);
	return ret;

}



static int __exit rk29_sdmmc_remove(struct platform_device *pdev)
{
	struct rk29_sdmmc *host = platform_get_drvdata(pdev);

	rk29_sdmmc_write(host->regs, SDMMC_RINTSTS, 0xFFFFFFFF);
	rk29_sdmmc_write(host->regs, SDMMC_INTMASK, 0); // disable all mmc interrupt first
	
	smp_wmb();
	free_irq(host->gpio_irq, host);
	gpio_free(host->gpio_det);
	mmc_remove_host(host->mmc);
	free_irq(host->irq, host);
	rk29_dma_free(host->dma_info.chn, &host->dma_info.client);
	
	/* disable clock to CIU */
	rk29_sdmmc_write(host->regs, SDMMC_CLKENA,0);
	rk29_sdmmc_write(host->regs, SDMMC_CLKSRC,0);
	
	iounmap(host->regs);
	mmc_free_host(host->mmc);
	return 0;
}
static int rk29_sdmmc_suspend(struct platform_device *pdev, pm_message_t state)
{
	int ret = 0;
#ifdef CONFIG_PM
	struct rk29_sdmmc *host = platform_get_drvdata(pdev);

	dev_info(host->dev, "Enter rk29_sdmmc_suspend\n");
	if(host->mmc && !host->is_sdio && host->gpio_det != INVALID_GPIO){
#if (LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 35))
		ret = mmc_suspend_host(host->mmc);
#else
		ret = mmc_suspend_host(host->mmc,state);
#endif
		if(!host->enable_sd_warkup)
			free_irq(host->gpio_irq, host);
	}
	rk29_sdmmc_write(host->regs, SDMMC_CLKENA, 0);
	clk_disable(host->clk);
#endif
	return ret;
}

static int rk29_sdmmc_resume(struct platform_device *pdev)
{
	int ret = 0;
#ifdef CONFIG_PM
	struct rk29_sdmmc *host = platform_get_drvdata(pdev);

	dev_info(host->dev, "Exit rk29_sdmmc_suspend\n");
	clk_enable(host->clk);
    rk29_sdmmc_write(host->regs, SDMMC_CLKENA, 1);
	if(host->mmc && !host->is_sdio && host->gpio_det != INVALID_GPIO){
		if(!host->enable_sd_warkup){
			ret = request_irq(host->gpio_irq,
                		  rk29_sdmmc_detect_change_isr,
                		  rk29_sdmmc_get_cd(host->mmc)?IRQF_TRIGGER_RISING : IRQF_TRIGGER_FALLING,
                		  "sd_detect",
                		  host);
			if(ret < 0)
			dev_err(host->dev, "gpio request_irq error\n");
		}
		host->is_init = 1;
		ret = mmc_resume_host(host->mmc);
		mmc_detect_change(host->mmc, 0);
	}
#endif
	return ret;
}
static struct platform_driver rk29_sdmmc_driver = {
	.suspend    = rk29_sdmmc_suspend,
	.resume     = rk29_sdmmc_resume,
	.remove		= __exit_p(rk29_sdmmc_remove),
	.driver		= {
		.name		= "rk29_sdmmc",
	},
};

static int __init rk29_sdmmc_init(void)
{
	return platform_driver_probe(&rk29_sdmmc_driver, rk29_sdmmc_probe);
}

static void __exit rk29_sdmmc_exit(void)
{
	platform_driver_unregister(&rk29_sdmmc_driver);
}

module_init(rk29_sdmmc_init);
module_exit(rk29_sdmmc_exit);

MODULE_DESCRIPTION("Rk29 Multimedia Card Interface driver");
MODULE_AUTHOR("Rockchips");
MODULE_LICENSE("GPL v2");
 
