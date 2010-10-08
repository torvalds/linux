/* drivers/mmc/host/rk2818-sdmmc.c
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
 *
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

#include <mach/board.h>
#include <mach/rk2818_iomap.h>
#include <mach/gpio.h>
#include <asm/dma.h>
#include <asm/scatterlist.h>

#include "rk2818-sdmmc.h"

struct mmc_host *wifi_mmc_host = NULL;

int  sdmmc0_disable_Irq_ForRemoval;
int hotplug_global_ctl;
struct rk2818_sdmmc_host *mmc0_host;


#define RK2818_MCI_DATA_ERROR_FLAGS	(SDMMC_INT_DRTO | SDMMC_INT_DCRC | SDMMC_INT_HTO | SDMMC_INT_SBE | SDMMC_INT_EBE)
#define RK2818_MCI_CMD_ERROR_FLAGS	(SDMMC_INT_RTO | SDMMC_INT_RCRC | SDMMC_INT_RE | SDMMC_INT_HLE)
#define RK2818_MCI_ERROR_FLAGS		(RK2818_MCI_DATA_ERROR_FLAGS | RK2818_MCI_CMD_ERROR_FLAGS | SDMMC_INT_HLE)
#define RK2818_MCI_SEND_STATUS		1
#define RK2818_MCI_RECV_STATUS		2
#define RK2818_MCI_DMA_THRESHOLD	16

enum {
	EVENT_CMD_COMPLETE = 0,
	EVENT_XFER_COMPLETE,
	EVENT_DATA_COMPLETE,
	EVENT_DATA_ERROR,
	EVENT_XFER_ERROR
};


enum rk2818_sdmmc_state {
	STATE_IDLE = 0,
	STATE_SENDING_CMD,
	STATE_SENDING_DATA,
	STATE_DATA_BUSY,
	STATE_SENDING_STOP,
	STATE_DATA_ERROR,
};

struct rk2818_sdmmc_host {
	struct device           *dev;
	struct clk 		*clk;
	spinlock_t		lock;
	void __iomem		*regs;

	struct scatterlist	*sg;
	unsigned int		pio_offset;
	struct resource		*mem;
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;

	int				dma_chn;
	int 			id;
	unsigned int	use_dma:1;
	unsigned int	no_detect:1;
	char			dma_name[8];

	u32			cmd_status;
	u32			data_status;
	u32			stop_cmdr;
	u32			dir_status;
	struct tasklet_struct	tasklet;
	unsigned long		pending_events;
	unsigned long		completed_events;
	enum rk2818_sdmmc_state	state;
	struct list_head	queue;

	u32			bus_hz;
	u32			current_speed;
	struct platform_device	*pdev;

	struct mmc_host		*mmc;
	u32			ctype;

	struct list_head	queue_node;

	unsigned int		clock;
	unsigned long		flags;
#define RK2818_MMC_CARD_PRESENT	0
#define RK2818_MMC_CARD_NEED_INIT	1
#define RK2818_MMC_SHUTDOWN		2
	int			irq;
	unsigned int cmd_tmo;
	struct mmc_command stop_mannual;

	struct timer_list	detect_timer;
	unsigned int oldstatus;     /* save old status */
};

#define MMC_DEBUG 0
#if MMC_DEBUG
#define xjhprintk(msg...) printk(msg)
#else
#define xjhprintk(msg...)
#endif

#define rk2818_sdmmc_test_and_clear_pending(host, event)		\
	test_and_clear_bit(event, &host->pending_events)
#define rk2818_sdmmc_set_completed(host, event)			\
	set_bit(event, &host->completed_events)

#define rk2818_sdmmc_set_pending(host, event)				\
	set_bit(event, &host->pending_events)

static void rk2818_sdmmc_read_data_pio(struct rk2818_sdmmc_host *host);
#if defined (CONFIG_DEBUG_FS)

static int rk2818_sdmmc_req_show(struct seq_file *s, void *v)
{
	struct rk2818_sdmmc_host	*host = s->private;
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_command	*stop;
	struct mmc_data		*data;

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

static int rk2818_sdmmc_req_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk2818_sdmmc_req_show, inode->i_private);
}

static const struct file_operations rk2818_sdmmc_req_fops = {
	.owner		= THIS_MODULE,
	.open		= rk2818_sdmmc_req_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static int rk2818_sdmmc_regs_show(struct seq_file *s, void *v)
{
	struct rk2818_sdmmc_host	*host = s->private;

	seq_printf(s, "STATUS:   0x%08x\n",readl(host->regs + SDMMC_STATUS));
	seq_printf(s, "RINTSTS:  0x%08x\n",readl(host->regs + SDMMC_RINTSTS));
	seq_printf(s, "CMD:      0x%08x\n", readl(host->regs + SDMMC_CMD));
	seq_printf(s, "CTRL:     0x%08x\n", readl(host->regs + SDMMC_CTRL));
	seq_printf(s, "INTMASK:  0x%08x\n", readl(host->regs + SDMMC_INTMASK));
	seq_printf(s, "CLKENA:   0x%08x\n", readl(host->regs + SDMMC_CLKENA));

	return 0;
}

static int rk2818_sdmmc_regs_open(struct inode *inode, struct file *file)
{
	return single_open(file, rk2818_sdmmc_regs_show, inode->i_private);
}

static const struct file_operations rk2818_sdmmc_regs_fops = {
	.owner		= THIS_MODULE,
	.open		= rk2818_sdmmc_regs_open,
	.read		= seq_read,
	.llseek		= seq_lseek,
	.release	= single_release,
};

static void rk2818_sdmmc_init_debugfs(struct rk2818_sdmmc_host *host)
{
	struct mmc_host		*mmc = host->mmc;
	struct dentry		*root;
	struct dentry		*node;

	root = mmc->debugfs_root;
	if (!root)
		return;

	node = debugfs_create_file("regs", S_IRUSR, root, host,
			&rk2818_sdmmc_regs_fops);
	if (IS_ERR(node))
		return;
	if (!node)
		goto err;

	node = debugfs_create_file("req", S_IRUSR, root, host, &rk2818_sdmmc_req_fops);
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
	dev_err(&mmc->class_dev, "failed to initialize debugfs for host\n");
}
#endif
static void rk2818_show_regs(struct rk2818_sdmmc_host *host)
{
	unsigned long cpsr_tmp;

	xjhprintk("--------[xjh] SD/MMC/SDIO Registers-----------------\n");
	xjhprintk("SDMMC_CTRL:\t0x%x\n",readl(host->regs + SDMMC_CTRL));
	xjhprintk("SDMMC_PWREN:\t0x%x\n",readl(host->regs + SDMMC_PWREN));
	xjhprintk("SDMMC_CLKDIV:\t0x%x\n",readl(host->regs + SDMMC_CLKDIV));
	xjhprintk("SDMMC_CLKSRC:\t0x%x\n",readl(host->regs + SDMMC_CLKSRC));
	xjhprintk("SDMMC_CLKENA:\t0x%x\n",readl(host->regs + SDMMC_CLKENA));
	xjhprintk("SDMMC_TMOUT:\t0x%x\n",readl(host->regs + SDMMC_TMOUT));
	xjhprintk("SDMMC_CTYPE:\t0x%x\n",readl(host->regs + SDMMC_CTYPE));
	xjhprintk("SDMMC_BLKSIZ:\t0x%x\n",readl(host->regs + SDMMC_BLKSIZ));
	xjhprintk("SDMMC_BYTCNT:\t0x%x\n",readl(host->regs + SDMMC_BYTCNT));
	xjhprintk("SDMMC_INTMASK:\t0x%x\n",readl(host->regs + SDMMC_INTMASK));
	xjhprintk("SDMMC_CMDARG:\t0x%x\n",readl(host->regs + SDMMC_CMDARG));
	xjhprintk("SDMMC_CMD:\t0x%x\n",readl(host->regs + SDMMC_CMD));
	xjhprintk("SDMMC_RESP0:\t0x%x\n",readl(host->regs + SDMMC_RESP0));
	xjhprintk("SDMMC_RESP1:\t0x%x\n",readl(host->regs + SDMMC_RESP1));
	xjhprintk("SDMMC_RESP2:\t0x%x\n",readl(host->regs + SDMMC_RESP2));
	xjhprintk("SDMMC_RESP3:\t0x%x\n",readl(host->regs + SDMMC_RESP3));
	xjhprintk("SDMMC_MINTSTS:\t0x%x\n",readl(host->regs + SDMMC_MINTSTS));
	xjhprintk("SDMMC_RINTSTS:\t0x%x\n",readl(host->regs + SDMMC_RINTSTS));
	xjhprintk("SDMMC_STATUS:\t0x%x\n",readl(host->regs + SDMMC_STATUS));
	xjhprintk("SDMMC_FIFOTH:\t0x%x\n",readl(host->regs + SDMMC_FIFOTH));
	xjhprintk("SDMMC_CDETECT:\t0x%x\n",readl(host->regs + SDMMC_CDETECT));
	xjhprintk("SDMMC_WRTPRT:\t0x%x\n",readl(host->regs + SDMMC_WRTPRT));
	xjhprintk("SDMMC_TCBCNT:\t0x%x\n",readl(host->regs + SDMMC_TCBCNT));
	xjhprintk("SDMMC_TBBCNT:\t0x%x\n",readl(host->regs + SDMMC_TBBCNT));
	xjhprintk("SDMMC_DEBNCE:\t0x%x\n",readl(host->regs + SDMMC_DEBNCE));
	xjhprintk("-------- Host states-----------------\n");
	xjhprintk("host->state:\t0x%x\n",host->state);
	xjhprintk("host->pending_events:\t0x%x\n",host->pending_events);
	xjhprintk("host->cmd_status:\t0x%x\n",host->cmd_status);
	xjhprintk("host->data_status:\t0x%x\n",host->data_status);
	xjhprintk("host->stop_cmdr:\t0x%x\n",host->stop_cmdr);
	xjhprintk("host->dir_status:\t0x%x\n",host->dir_status);
	xjhprintk("host->completed_events:\t0x%x\n",host->completed_events);
	xjhprintk("host->dma_chn:\t0x%x\n",host->dma_chn);
	xjhprintk("host->use_dma:\t0x%x\n",host->use_dma);
	xjhprintk("host->no_detect:\t0x%x\n",host->no_detect);
	xjhprintk("host->bus_hz:\t0x%x\n",host->bus_hz);
	xjhprintk("host->current_speed:\t0x%x\n",host->current_speed);
	xjhprintk("host->ctype:\t0x%x\n",host->ctype);
	xjhprintk("host->clock:\t0x%x\n",host->clock);
	xjhprintk("host->flags:\t0x%x\n",host->flags);
	xjhprintk("host->irq:\t0x%x\n",host->irq);
	xjhprintk("-------- rk2818 CPU register-----------------\n");
	__asm__ volatile ("mrs	%0, cpsr		@ local_irq_save\n":"=r" (cpsr_tmp)::"memory", "cc" );
	xjhprintk("cpsr:\t0x%x\n",cpsr_tmp);
			


}

static void rk2818_sdmmc_set_power(struct rk2818_sdmmc_host *host, u32 ocr_avail)
{
	if(ocr_avail == 0)
		writel(0, host->regs + SDMMC_PWREN);
	else
		writel(1, host->regs + SDMMC_PWREN);
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

static void rk2818_sdmmc_set_timeout(struct rk2818_sdmmc_host *host, struct mmc_data *data)
{
	unsigned timeout;

	timeout = ns_to_clocks(host->clock, data->timeout_ns) + data->timeout_clks;

	dev_dbg(host->dev, "tmo req:%d + %d reg:%d clk:%d\n", 
		data->timeout_ns, data->timeout_clks, timeout, host->clock);
	writel((timeout << 8) | (80), host->regs + SDMMC_TMOUT);
}

static u32 rk2818_sdmmc_prepare_command(struct mmc_host *mmc,
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


static void rk2818_sdmmc_start_command(struct rk2818_sdmmc_host *host,
		struct mmc_command *cmd, u32 cmd_flags)
{
 	int tmo = 5000;
	unsigned long flags;
	int retry = 4;
 	host->cmd = cmd;
	dev_dbg(host->dev, "start cmd:%d ARGR=0x%08x CMDR=0x%08x\n",
					cmd->opcode, cmd->arg, cmd_flags);
	local_irq_save(flags);
	writel(cmd->arg, host->regs + SDMMC_CMDARG); // write to CMDARG register
	writel(cmd_flags | SDMMC_CMD_START, host->regs + SDMMC_CMD); // write to CMD register
	local_irq_restore(flags);

	/* wait until CIU accepts the command */
	while (--tmo && (readl(host->regs + SDMMC_CMD) & SDMMC_CMD_START)) 
		cpu_relax();
	if(!tmo) {
		tmo = 5000;
		xjhprintk("%s start cmd %d error. retry again!!!\n",__FUNCTION__ ,cmd->opcode);
		rk2818_show_regs(host);
		rk2818_sdmmc_set_completed(host, EVENT_CMD_COMPLETE);
		host->cmd_status |= SDMMC_INT_RE;
		tasklet_schedule(&host->tasklet);
		retry --;
	//	if(retry)
	//		goto START_CMD;
	}
	#if 0
	tmo = 60;
	host->cmd_tmo = 0;
	while(--tmo && !host->cmd_tmo)
	{
		mdelay(5);
		//cpu_relax();

	}
	if(!tmo)
	{
		xjhprintk("cmd %d response time out(not receive INT)\n",cmd->opcode);
		rk2818_sdmmc_set_completed(host, EVENT_CMD_COMPLETE);
		host->cmd_status |= SDMMC_INT_RE;
		tasklet_schedule(&host->tasklet);
	}
	#endif
}

static void send_stop_cmd(struct rk2818_sdmmc_host *host, struct mmc_data *data)
{
	unsigned long flags;
	unsigned long fifo_left;
	/*等待前面传输处理完成*/
	int time_out =60;
	while(readl(host->regs + SDMMC_STATUS) & (SDMMC_STAUTS_DATA_BUSY)) {
		mdelay(5);
		time_out --;
		
		if(!time_out){
			time_out =60;
			xjhprintk("card busy now,can not issuse req! \nreset DMA and FIFO\n");
			 local_irq_save(flags);
			
		//	writel(readl(host->regs + SDMMC_CTRL) | ( SDMMC_CTRL_DMA_RESET )  & ~SDMMC_CTRL_DMA_ENABLE, host->regs + SDMMC_CTRL);
			/* wait till resets clear */
		//	while (readl(host->regs + SDMMC_CTRL) & ( SDMMC_CTRL_DMA_RESET));
			
			writel(readl(host->regs + SDMMC_CTRL) | ( SDMMC_CTRL_FIFO_RESET ), host->regs + SDMMC_CTRL);
			 /* wait till resets clear */
			while (readl(host->regs + SDMMC_CTRL) & ( SDMMC_CTRL_FIFO_RESET));
			 local_irq_restore(flags);
		}
		//cpu_relax();
	}
	
	#if 1

	fifo_left = readl(host->regs + SDMMC_STATUS);

	if((fifo_left & SDMMC_STAUTS_FIFO_FULL) && (fifo_left & SDMMC_STAUTS_FIFO_RX_WATERMARK))
	{
		xjhprintk("%s read operation reach water mark\n",__FUNCTION__);
		rk2818_show_regs(host);
		while(SDMMC_GET_FCNT(readl(host->regs + SDMMC_STATUS))>>2)
			readl(host->regs + SDMMC_DATA);		//discard the no use data

		rk2818_show_regs(host);
		
	}
	#endif
	/*检查FIFO,如果不为空，清空*/
	if(!(readl(host->regs + SDMMC_STATUS) & SDMMC_STAUTS_FIFO_EMPTY)) {
		xjhprintk("%s: FIFO not empty, clear it\n",__FUNCTION__);
		rk2818_show_regs(host);
		 local_irq_save(flags);
	//	writel(readl(host->regs + SDMMC_CTRL) | ( SDMMC_CTRL_DMA_RESET ), host->regs + SDMMC_CTRL);
		/* wait till resets clear */
	//	while (readl(host->regs + SDMMC_CTRL) & ( SDMMC_CTRL_DMA_RESET));
			  
		writel(readl(host->regs + SDMMC_CTRL) | ( SDMMC_CTRL_FIFO_RESET ), host->regs + SDMMC_CTRL);
		 /* wait till resets clear */
		while (readl(host->regs + SDMMC_CTRL) & ( SDMMC_CTRL_FIFO_RESET));
		 local_irq_restore(flags);
		//cpu_relax();
	}

	rk2818_sdmmc_start_command(host, data->stop, host->stop_cmdr);
}

static void rk2818_sdmmc_dma_cleanup(struct rk2818_sdmmc_host *host)
{
	struct mmc_data			*data = host->data;
	if (data) 
		dma_unmap_sg(host->dev, data->sg, data->sg_len,
		     ((data->flags & MMC_DATA_WRITE)? DMA_TO_DEVICE : DMA_FROM_DEVICE));
}

static void rk2818_sdmmc_stop_dma(struct rk2818_sdmmc_host *host)
{
	if (host->dma_chn >= 0) {
		xjhprintk("[xjh] %s enter host->state %d\n", __FUNCTION__, host->state);
		writel(readl(host->regs + SDMMC_CTRL) & ~SDMMC_CTRL_DMA_ENABLE,
				host->regs +SDMMC_CTRL);
		//disable_dma(host->dma_chn);
		if (strncmp(host->dma_name, "sdio", strlen("sdio")) != 0)
			free_dma(host->dma_chn);
		host->dma_chn = -1;
		rk2818_sdmmc_dma_cleanup(host);
		rk2818_sdmmc_set_pending(host, EVENT_XFER_COMPLETE);//[xjh]  如果数据读写过程被拔掉，需要设置这个状态
		xjhprintk("[xjh] %s exit\n", __FUNCTION__);
	} else {
		/* Data transfer was stopped by the interrupt handler */
		rk2818_sdmmc_set_pending(host, EVENT_XFER_COMPLETE);
	}
}

/* This function is called by the DMA driver from tasklet context. */
static void rk2818_sdmmc_dma_complete(int chn, void *arg)
{
	struct rk2818_sdmmc_host	*host = arg;
	struct mmc_data		*data = host->data;

	dev_dbg(host->dev, "DMA complete\n");

	spin_lock(&host->lock);
	//disable dma
	writel(readl(host->regs + SDMMC_CTRL) & ~SDMMC_CTRL_DMA_ENABLE,
				host->regs +SDMMC_CTRL);
	rk2818_sdmmc_dma_cleanup(host);
	//disable_dma(host->dma_chn);
	if (strncmp(host->dma_name, "sdio", strlen("sdio")) != 0)
		free_dma(host->dma_chn);
	host->dma_chn = -1;
	if (data) {
		
		rk2818_sdmmc_set_pending(host, EVENT_XFER_COMPLETE);
		tasklet_schedule(&host->tasklet);
	}
	spin_unlock(&host->lock);
}
static int rk2818_sdmmc_submit_data_dma(struct rk2818_sdmmc_host *host, struct mmc_data *data)
{
	struct scatterlist		*sg;
	unsigned int			i;
	dev_dbg(host->dev, "sg_len=%d\n", data->sg_len);
	host->dma_chn = -1;
	if(host->use_dma == 0)
		return -ENOSYS;
#if 0
	if (data->blocks * data->blksz < RK2818_MCI_DMA_THRESHOLD)
		return -EINVAL;
#endif
	if (data->blksz & 3)
		return -EINVAL;
	for_each_sg(data->sg, sg, data->sg_len, i) {
		if (sg->offset & 3 || sg->length & 3)
			return -EINVAL;
	}
	if (strncmp(host->dma_name, "sdio", strlen("sdio")) != 0) {
		for(i = 0; i < MAX_SG_CHN; i++) {
			if(request_dma(i, host->dma_name) == 0) {
				host->dma_chn = i;
				break;
			}
		}
		if(i == MAX_SG_CHN) 
			return -EINVAL;
	}
	else
		host->dma_chn = 1;
	dma_map_sg(host->dev, data->sg, data->sg_len, 
			(data->flags & MMC_DATA_READ)? DMA_FROM_DEVICE : DMA_TO_DEVICE);
	for_each_sg(data->sg, sg, data->sg_len, i) {
		dev_dbg(host->dev, "sg[%d]  addr: 0x%08x, len: %d", i, sg->dma_address, sg->length);
	}
	set_dma_sg(host->dma_chn, data->sg, data->sg_len);
	//printk("2.....\n");
	set_dma_mode(host->dma_chn,
				(data->flags & MMC_DATA_READ)? DMA_MODE_READ : DMA_MODE_WRITE);
	//printk("3.....\n");
	set_dma_handler(host->dma_chn, rk2818_sdmmc_dma_complete, (void *)host, DMA_IRQ_DELAY_MODE);
	//printk("4.....\n");
	writel(readl(host->regs + SDMMC_CTRL) | SDMMC_CTRL_DMA_ENABLE,
				host->regs +SDMMC_CTRL);
	//printk("5.....\n");
	enable_dma(host->dma_chn);
	//printk("6.....\n");
	dev_dbg(host->dev,"DMA enable, \n");
	return 0;
}

static void rk2818_sdmmc_submit_data(struct rk2818_sdmmc_host *host, struct mmc_data *data)
{
	data->error = -EINPROGRESS;

	WARN_ON(host->data);
	host->sg = NULL;
	host->data = data;

	if (rk2818_sdmmc_submit_data_dma(host, data)) {
		host->sg = data->sg;
		host->pio_offset = 0;
		if (data->flags & MMC_DATA_READ)
			host->dir_status = RK2818_MCI_RECV_STATUS;
		else 
			host->dir_status = RK2818_MCI_SEND_STATUS;
		writel(readl(host->regs + SDMMC_CTRL) & ~SDMMC_CTRL_DMA_ENABLE,
				host->regs +SDMMC_CTRL);
	}
}
#if 0
#define mci_send_cmd(host,cmd,arg) {	\
    writel(arg, host->regs + SDMMC_CMDARG);		\
    writel(SDMMC_CMD_START | cmd, host->regs + SDMMC_CMD);		\
    while (readl(host->regs + SDMMC_CMD) & SDMMC_CMD_START); \
}
#else
static void mci_send_cmd(struct rk2818_sdmmc_host *host,unsigned int cmd,int arg) 
{
	int tmo = 5000;
	int retry = 4;

    writel(arg, host->regs + SDMMC_CMDARG);		
    writel(SDMMC_CMD_START | cmd, host->regs + SDMMC_CMD);		
	while (--tmo && readl(host->regs + SDMMC_CMD) & SDMMC_CMD_START); 
	if(!tmo) {
		tmo = 5000;
		xjhprintk("%s set register error. retry again!!!\n",__FUNCTION__);
		retry --;
	//	if(retry)
	//		goto START_CMD;
	}
}

#endif

#if 1
void inline rk2818_sdmmc_setup_bus(struct rk2818_sdmmc_host *host)
{
	;
}
#else
void rk2818_sdmmc_setup_bus(struct rk2818_sdmmc_host *host)
{
	u32 div;

	if (host->clock != host->current_speed) {
		div  = (((host->bus_hz + (host->bus_hz / 5)) / host->clock)) >> 1;

		dev_dbg(host->dev, "Bus speed = %dHz div:%d (actual %dHz)\n",
			host->clock, div, (host->bus_hz / div) >> 1);
		xjhprintk("Bus speed = %dHz div:%d (actual %dHz)\n",
			host->clock, div, (host->bus_hz / div) >> 1);
		
		/* store the actual clock for calculations */
		host->clock = (host->bus_hz / div) >> 1;
		/* disable clock */
		writel(0, host->regs + SDMMC_CLKENA);
		writel(0, host->regs + SDMMC_CLKSRC);
		/* inform CIU */
		mci_send_cmd(host, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);
		/* set clock to desired speed */
		writel(div, host->regs + SDMMC_CLKDIV);
		/* inform CIU */
		mci_send_cmd(host, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);
		/* enable clock */
		writel(SDMMC_CLKEN_ENABLE, host->regs + SDMMC_CLKENA);
		/* inform CIU */
		 mci_send_cmd(host, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);

		host->current_speed = host->clock;
	}

	/* Set the current host bus width */
	writel(host->ctype, host->regs + SDMMC_CTYPE);
}
#endif


static void rk2818_sdmmc_start_request(struct rk2818_sdmmc_host *host)
{
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;
	u32			cmdflags;
	unsigned long flags;

	int time_out =60;

	mrq = host->mrq;

	//rk2818_sdmmc_setup_bus(host);

	/*等待前面传输处理完成*/
	while(readl(host->regs + SDMMC_STATUS) & (SDMMC_STAUTS_DATA_BUSY)) {
		mdelay(5);
		time_out --;
		
		if(!time_out){
			time_out =60;
			xjhprintk("card busy now,can not issuse req! \nreset DMA and FIFO\n");
			 local_irq_save(flags);
			
		//	writel(readl(host->regs + SDMMC_CTRL) | ( SDMMC_CTRL_DMA_RESET )  & ~SDMMC_CTRL_DMA_ENABLE, host->regs + SDMMC_CTRL);
			/* wait till resets clear */
		//	while (readl(host->regs + SDMMC_CTRL) & ( SDMMC_CTRL_DMA_RESET));
				  
			writel(readl(host->regs + SDMMC_CTRL) | ( SDMMC_CTRL_FIFO_RESET ), host->regs + SDMMC_CTRL);
			 /* wait till resets clear */
			while (readl(host->regs + SDMMC_CTRL) & ( SDMMC_CTRL_FIFO_RESET));
			 local_irq_restore(flags);
		}
		//cpu_relax();
	}
	/*检查FIFO,如果不为空，清空*/
	if(!(readl(host->regs + SDMMC_STATUS) & SDMMC_STAUTS_FIFO_EMPTY)) {
		xjhprintk("%s: FIFO not empty, clear it\n",__FUNCTION__);
		rk2818_show_regs(host);
		 local_irq_save(flags);
	//	writel(readl(host->regs + SDMMC_CTRL) | ( SDMMC_CTRL_DMA_RESET ), host->regs + SDMMC_CTRL);
		/* wait till resets clear */
	//	while (readl(host->regs + SDMMC_CTRL) & ( SDMMC_CTRL_DMA_RESET));
			  
		writel(readl(host->regs + SDMMC_CTRL) | ( SDMMC_CTRL_FIFO_RESET ), host->regs + SDMMC_CTRL);
		 /* wait till resets clear */
		while (readl(host->regs + SDMMC_CTRL) & ( SDMMC_CTRL_FIFO_RESET));
		 local_irq_restore(flags);
		//cpu_relax();
	}

	host->mrq = mrq;

	host->pending_events = 0;
	host->completed_events = 0;
	host->data_status = 0;

	data = mrq->data;
	if (data) {
		rk2818_sdmmc_set_timeout(host, data);
		writel(data->blksz * data->blocks, host->regs + SDMMC_BYTCNT);
		writel(data->blksz, host->regs + SDMMC_BLKSIZ);
	}

	cmd = mrq->cmd;
	cmdflags = rk2818_sdmmc_prepare_command(host->mmc, cmd);

	if (unlikely(test_and_clear_bit(RK2818_MMC_CARD_NEED_INIT, &host->flags))) 
	    cmdflags |= SDMMC_CMD_INIT; 
	
	if (data)
		rk2818_sdmmc_submit_data(host, data);
	
	rk2818_sdmmc_start_command(host, cmd, cmdflags);
	if (mrq->stop) 
	{
		host->stop_cmdr = rk2818_sdmmc_prepare_command(host->mmc, mrq->stop);
		dev_dbg(host->dev, "mrq stop: stop_cmdr = %d", host->stop_cmdr);
	}

}



static void rk2818_sdmmc_queue_request(struct rk2818_sdmmc_host *host, struct mmc_request *mrq)
{
	dev_dbg(host->dev, "queue request: state=%d\n",
			host->state);

	spin_lock(&host->lock);
	host->mrq = mrq;
	if (host->state == STATE_IDLE) {
		host->state = STATE_SENDING_CMD;
		rk2818_sdmmc_start_request(host);
	} else {
		list_add_tail(&host->queue_node, &host->queue);
	}
	spin_unlock(&host->lock);
}


static void rk2818_sdmmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct rk2818_sdmmc_host	*host = mmc_priv(mmc);

	WARN_ON(host->mrq);
	#if 0
	if (!test_bit(RK2818_MMC_CARD_PRESENT, &host->flags)) {
		mrq->cmd->error = -ENOMEDIUM;
		mmc_request_done(mmc, mrq);
		return;
	}
	#endif

	rk2818_sdmmc_queue_request(host, mrq);
}

static void rk2818_sdmmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct rk2818_sdmmc_host	*host = mmc_priv(mmc);
	u32 div;
	unsigned long flags;
	
	xjhprintk("%s bus_width %x ios->clock %x host->clock %x ocr %x\n",
			__FUNCTION__,ios->bus_width, ios->clock, host->clock, host->mmc->ocr_avail);

	host->ctype = 0; // set default 1 bit mode
	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		host->ctype = 0;
		break;
	case MMC_BUS_WIDTH_4:
		host->ctype = SDMMC_CTYPE_4BIT;
		break;
	}

	spin_lock_irqsave(&host->lock,flags);
	/* Set the current host bus width */
	writel(host->ctype, host->regs + SDMMC_CTYPE);

	if(ios->bus_mode == MMC_BUSMODE_OPENDRAIN)
		writel(readl(host->regs + SDMMC_CTRL) | SDMMC_CTRL_OD_PULLUP, host->regs + SDMMC_CTRL);
	else
		writel(readl(host->regs + SDMMC_CTRL) & ~SDMMC_CTRL_OD_PULLUP, host->regs + SDMMC_CTRL);
	spin_unlock_irqrestore(&host->lock,flags);

	if (ios->clock && (host->current_speed != ios->clock)) {
		/*
		 * Use mirror of ios->clock to prevent race with mmc
		 * core ios update when finding the minimum.
		 */
		div  = (((host->bus_hz + (host->bus_hz / 5)) / ios->clock)) >> 1;
		xjhprintk("Bus speed = %dHz div:%d (actual %dHz)\n",
			host->clock, div, (host->bus_hz / div) >> 1);
		
		/* store the actual clock for calculations */
		host->clock = (host->bus_hz / div) >> 1;
		/*等待卡片传输完成*/
		while(readl(host->regs + SDMMC_STATUS) & (SDMMC_STAUTS_MC_BUSY | SDMMC_STAUTS_DATA_BUSY)){
			udelay(10);
			xjhprintk("SD/MMC busy now(status 0x%x),can not change clock\n",readl(host->regs + SDMMC_STATUS));
			//cpu_relax();
		}
		spin_lock_irqsave(&host->lock,flags);
		/* disable clock */
		writel(0, host->regs + SDMMC_CLKENA);
		writel(0, host->regs + SDMMC_CLKSRC);
		/* inform CIU */
		mci_send_cmd(host, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);
		/* set clock to desired speed */
		writel(div, host->regs + SDMMC_CLKDIV);
		/* inform CIU */
		mci_send_cmd(host, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);
		/* enable clock */
		if(host->pdev->id == 0) //sdmmc0 endable low power mode
			writel(SDMMC_CLKEN_LOW_PWR | SDMMC_CLKEN_ENABLE, host->regs + SDMMC_CLKENA);
		else
			writel(SDMMC_CLKEN_ENABLE, host->regs + SDMMC_CLKENA);
		/* inform CIU */
		 mci_send_cmd(host, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);

		host->current_speed= ios->clock; 
		
		spin_unlock_irqrestore(&host->lock,flags);

	} 
	#if 0
	else {
		spin_lock(&host->lock);
		host->clock = 0;
		spin_unlock(&host->lock);
	}
	#endif
	spin_lock_irqsave(&host->lock,flags);

	switch (ios->power_mode) {
	case MMC_POWER_UP:
		set_bit(RK2818_MMC_CARD_NEED_INIT, &host->flags);
		rk2818_sdmmc_set_power(host, host->mmc->ocr_avail);
		break;
	default:
		//rk2818_sdmmc_set_power(host, 0);
		break;
	}
	spin_unlock_irqrestore(&host->lock,flags);
	
}



static int rk2818_sdmmc_get_ro(struct mmc_host *mmc)
{
	struct rk2818_sdmmc_host *host = mmc_priv(mmc);
	u32 wrtprt = readl(host->regs + SDMMC_WRTPRT);

	return (wrtprt & SDMMC_WRITE_PROTECT)?1:0;
}


static int rk2818_sdmmc_get_cd(struct mmc_host *mmc)
{
	struct rk2818_sdmmc_host *host = mmc_priv(mmc);
	u32 cdetect = readl(host->regs + SDMMC_CDETECT);

#if defined(CONFIG_MACH_RAHO)||defined(CONFIG_MACH_RAHO_0928)
    return 1;
#else
	return (cdetect & SDMMC_CARD_DETECT_N)?0:1;
#endif

}

static void rk2818_sdmmc_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	u32 intmask;
	unsigned long flags;
	struct rk2818_sdmmc_host *host = mmc_priv(mmc);
	spin_lock_irqsave(&host->lock, flags);
	intmask = readl(host->regs + SDMMC_INTMASK);
	if(enable)
		writel(intmask | SDMMC_INT_SDIO, host->regs + SDMMC_INTMASK);
	else
		writel(intmask & ~SDMMC_INT_SDIO, host->regs + SDMMC_INTMASK);
	spin_unlock_irqrestore(&host->lock, flags);
}

static const struct mmc_host_ops rk2818_sdmmc_ops[] = {
	{
		.request	= rk2818_sdmmc_request,
		.set_ios	= rk2818_sdmmc_set_ios,
		.get_ro		= rk2818_sdmmc_get_ro,
		.get_cd		= rk2818_sdmmc_get_cd,
	},
	{
		.request	= rk2818_sdmmc_request,
		.set_ios	= rk2818_sdmmc_set_ios,
		.enable_sdio_irq = rk2818_sdmmc_enable_sdio_irq,
	},
};

static void rk2818_sdmmc_request_end(struct rk2818_sdmmc_host *host, struct mmc_request *mrq)
	__releases(&host->lock)
	__acquires(&host->lock)
{
	struct mmc_host		*prev_mmc = host->mmc;
	unsigned long flags;
	int time_out =60;
	unsigned long fifo_left;
	
	WARN_ON(host->cmd || host->data);
	host->mrq = NULL;

	
	/*等待前面传输处理完成*/
	while(readl(host->regs + SDMMC_STATUS) & (SDMMC_STAUTS_DATA_BUSY)) {
		mdelay(5);
		time_out --;
		
		if(!time_out){
			time_out =60;
			xjhprintk("req done:card busy for a long time!!!reset DMA and FIFO\n");
			 local_irq_save(flags);
			
	//		writel(readl(host->regs + SDMMC_CTRL) | ( SDMMC_CTRL_DMA_RESET )  & ~SDMMC_CTRL_DMA_ENABLE, host->regs + SDMMC_CTRL);
			/* wait till resets clear */
	//		while (readl(host->regs + SDMMC_CTRL) & ( SDMMC_CTRL_DMA_RESET));
 
			writel(readl(host->regs + SDMMC_CTRL) | ( SDMMC_CTRL_FIFO_RESET ), host->regs + SDMMC_CTRL);
			 /* wait till resets clear */
			while (readl(host->regs + SDMMC_CTRL) & ( SDMMC_CTRL_FIFO_RESET));
			 local_irq_restore(flags);
		}
		//cpu_relax();
	}

	#if 1
	fifo_left = readl(host->regs + SDMMC_STATUS);

	if((fifo_left & SDMMC_STAUTS_FIFO_FULL) && (fifo_left & SDMMC_STAUTS_FIFO_RX_WATERMARK))
	{
		xjhprintk("%s read operation reach water mark\n",__FUNCTION__);
		rk2818_show_regs(host);
		while(SDMMC_GET_FCNT(readl(host->regs + SDMMC_STATUS))>>2)
			readl(host->regs + SDMMC_DATA);		//discard the no use data
		rk2818_show_regs(host);
		
	}
	#endif
	/*检查FIFO,如果不为空，清空*/
	if(!(readl(host->regs + SDMMC_STATUS) & SDMMC_STAUTS_FIFO_EMPTY)) {
		xjhprintk("%s: FIFO not empty, clear it\n",__FUNCTION__);
		rk2818_show_regs(host);
		 local_irq_save(flags);
	//	writel(readl(host->regs + SDMMC_CTRL) | ( SDMMC_CTRL_DMA_RESET ), host->regs + SDMMC_CTRL);
		/* wait till resets clear */
	//	while (readl(host->regs + SDMMC_CTRL) & ( SDMMC_CTRL_DMA_RESET));
		  
		writel(readl(host->regs + SDMMC_CTRL) | ( SDMMC_CTRL_FIFO_RESET ), host->regs + SDMMC_CTRL);
		 /* wait till resets clear */
		while (readl(host->regs + SDMMC_CTRL) & ( SDMMC_CTRL_FIFO_RESET));
		 local_irq_restore(flags);
		//cpu_relax();
	}

	if (!list_empty(&host->queue)) {
		host = list_entry(host->queue.next,
				struct rk2818_sdmmc_host, queue_node);
		list_del(&host->queue_node);
		dev_dbg(host->dev, "list not empty: %s is next\n",
				mmc_hostname(host->mmc));
		host->state = STATE_SENDING_CMD;
		rk2818_sdmmc_start_request(host);
	} else {
		dev_dbg(host->dev, "list empty\n");
		host->state = STATE_IDLE;
	}

	spin_unlock(&host->lock);
	mmc_request_done(prev_mmc, mrq);

	spin_lock(&host->lock);
}

static void rk2818_sdmmc_command_complete(struct rk2818_sdmmc_host *host,
			struct mmc_command *cmd)
{
	u32		status = host->cmd_status;

	host->cmd_status = 0;

	if(cmd->flags & MMC_RSP_PRESENT) {

	    if(cmd->flags & MMC_RSP_136) {

			cmd->resp[3] = readl(host->regs + SDMMC_RESP0);
			cmd->resp[2] = readl(host->regs + SDMMC_RESP1);
			cmd->resp[1] = readl(host->regs + SDMMC_RESP2);
			cmd->resp[0] = readl(host->regs + SDMMC_RESP3);
	    } else {
	        cmd->resp[0] = readl(host->regs + SDMMC_RESP0);
			cmd->resp[1] = 0;
			cmd->resp[2] = 0;
			cmd->resp[3] = 0;
	    }
	}

	if (status & SDMMC_INT_RTO)
		cmd->error = -ETIMEDOUT;
	else if ((cmd->flags & MMC_RSP_CRC) && (status & SDMMC_INT_RCRC))
		cmd->error = -EILSEQ;
	else if (status & SDMMC_INT_RE)
		cmd->error = -EIO;
	else if(status & SDMMC_INT_HLE)
		cmd->error = -EIO;
	else
		cmd->error = 0;

	if (cmd->error) {
		dev_dbg(host->dev,
			"command error: status=0x%08x resp=0x%08x\n"
			"cmd=0x%08x arg=0x%08x flg=0x%08x err=%d\n", 
			status, cmd->resp[0], 
			cmd->opcode, cmd->arg, cmd->flags, cmd->error);
		

		if (cmd->data) {
			host->data = NULL;
			rk2818_sdmmc_stop_dma(host);
		}
	} 
}

static void rk2818_sdmmc_tasklet_func(unsigned long priv)
{
	struct rk2818_sdmmc_host	*host = (struct rk2818_sdmmc_host *)priv;
	struct mmc_request	*mrq = host->mrq;
	struct mmc_data		*data = host->data;
	enum rk2818_sdmmc_state	state = host->state;
	enum rk2818_sdmmc_state	prev_state;
	u32			status;
	int timeout=5000;

	spin_lock(&host->lock);

	state = host->state;
	do {
		prev_state = state;
		timeout --;
		if(!timeout)
		{
			timeout = 5000;
			xjhprintk("%s\n",__FUNCTION__);
		}

		switch (state) {
		case STATE_IDLE:
			break;

		case STATE_SENDING_CMD:
			if (!rk2818_sdmmc_test_and_clear_pending(host,
						EVENT_CMD_COMPLETE))
				break;

			host->cmd = NULL;
			rk2818_sdmmc_set_completed(host, EVENT_CMD_COMPLETE);
			rk2818_sdmmc_command_complete(host, mrq->cmd);
			//if (!mrq->data || cmd->error) {
			if (!mrq->data || mrq->cmd->error) {
				rk2818_sdmmc_request_end(host, host->mrq);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_DATA;
			/* fall through */

		case STATE_SENDING_DATA:
			if (rk2818_sdmmc_test_and_clear_pending(host,
						EVENT_DATA_ERROR)) {
				xjhprintk("[xjh] %s data->stop %p\n", __FUNCTION__,data->stop);
				rk2818_sdmmc_stop_dma(host);
				#if 0
				if (data->stop)
					send_stop_cmd(host, data);
				#else
				if (data->stop)
					send_stop_cmd(host, data);
				else //cmd17 or cmd24
				{
					xjhprintk("%s>> send stop command mannualy\n",__FUNCTION__);
					host->stop_mannual.opcode = MMC_STOP_TRANSMISSION;
					host->stop_mannual.arg = 0;
					host->stop_mannual.flags = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC;
					host->stop_cmdr = 0x414c;

					host->mrq->stop = &host->stop_mannual;
					data->stop = &host->stop_mannual;
					send_stop_cmd(host, data);
				}
				#endif
				state = STATE_DATA_ERROR;
				xjhprintk("[xjh] %s sendging data error\n", __FUNCTION__);
				break;
			}

			if (!rk2818_sdmmc_test_and_clear_pending(host,
						EVENT_XFER_COMPLETE))
				break;
			rk2818_sdmmc_set_completed(host, EVENT_XFER_COMPLETE);
			prev_state = state = STATE_DATA_BUSY;
			/* fall through */

		case STATE_DATA_BUSY:
			if (!rk2818_sdmmc_test_and_clear_pending(host,
						EVENT_DATA_COMPLETE))
				break;

			host->data = NULL;
			rk2818_sdmmc_set_completed(host, EVENT_DATA_COMPLETE);
			status = host->data_status;

			if (unlikely(status & RK2818_MCI_DATA_ERROR_FLAGS)) {
				xjhprintk("[xjh] %s data error\n", __FUNCTION__);
				if (status & SDMMC_INT_DRTO) {
					dev_err(host->dev,
							"data timeout error\n");
					data->error = -ETIMEDOUT;
				} else if (status & SDMMC_INT_DCRC) {
					dev_err(host->dev,
							"data CRC error\n");
					data->error = -EILSEQ;
				} else {
					dev_err(host->dev,
						"data FIFO error (status=%08x)\n",
						status);
					data->error = -EIO;
				}
			}
			else {
				data->bytes_xfered = data->blocks * data->blksz;
				data->error = 0;
			}

			if (!data->stop) {
				rk2818_sdmmc_request_end(host, host->mrq);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_STOP;
			if (!data->error)
				send_stop_cmd(host, data);
			/* fall through */

		case STATE_SENDING_STOP:
			if (!rk2818_sdmmc_test_and_clear_pending(host,
						EVENT_CMD_COMPLETE))
				break;
			
			//xjhprintk("[xjh] %s sending stop cmd end\n", __FUNCTION__);
			host->cmd = NULL;
			rk2818_sdmmc_command_complete(host, mrq->stop);
			rk2818_sdmmc_request_end(host, host->mrq);
			goto unlock;
		case STATE_DATA_ERROR:
			if (!rk2818_sdmmc_test_and_clear_pending(host,
						EVENT_XFER_COMPLETE))
				break;
			#if 0
			//[xjh]  cmd17没有产生DTO中断，EVENT_DATA_COMPLETE没有被设置，需要手工设置
			//为何cmd17出错后不能产生DTO中断，需要进一步查明???????
			if((host->mrq->cmd->opcode == 17)) {
				xjhprintk("%s cmd%d was interrupt(host->pending_events %x)\n",
						__FUNCTION__,host->mrq->cmd->opcode,host->pending_events);
			//	rk2818_sdmmc_set_pending(host, EVENT_DATA_COMPLETE);
			}
			#endif
			
			state = STATE_DATA_BUSY;
			break;
		}
	} while (state != prev_state);

	host->state = state;

unlock:
	spin_unlock(&host->lock);

}



inline static void rk2818_sdmmc_push_data(struct rk2818_sdmmc_host *host, void *buf, int cnt)
{
    u32* pData = (u32*)buf;

	dev_dbg(host->dev, "push data(cnt=%d)\n",cnt);

    if (cnt % 4 != 0) 
		cnt = (cnt>>2) +1;
	else
    	cnt = cnt >> 2;
    while (cnt > 0) {
		writel(*pData++, host->regs + SDMMC_DATA);
        cnt--;
    }
}

inline static void rk2818_sdmmc_pull_data(struct rk2818_sdmmc_host *host, void *buf,int cnt)
{
    u32* pData = (u32*)buf;

	dev_dbg(host->dev, "pull data(cnt=%d)\n",cnt);


    if (cnt % 4 != 0) 
		cnt = (cnt>>2) +1;
	else
    	cnt = cnt >> 2;
    while (cnt > 0) {
        *pData++ = readl(host->regs + SDMMC_DATA);
        cnt--;
    }
}

static void rk2818_sdmmc_read_data_pio(struct rk2818_sdmmc_host *host)
{
	struct scatterlist	*sg = host->sg;
	void			*buf = sg_virt(sg);
	unsigned int		offset = host->pio_offset;
	struct mmc_data		*data = host->data;
	u32			status;
	unsigned int		nbytes = 0,len,old_len,count =0;
	xjhprintk("[xjh] %s enter,sg->length 0x%x\n", __FUNCTION__, sg->length);

	do {
		len = SDMMC_GET_FCNT(readl(host->regs + SDMMC_STATUS)) << 2;
		if(count == 0) 
			old_len = len;
		if (likely(offset + len <= sg->length)) {
			rk2818_sdmmc_pull_data(host, (void *)(buf + offset),len);

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
			rk2818_sdmmc_pull_data(host, (void *)(buf + offset),remaining);
			nbytes += remaining;

			flush_dcache_page(sg_page(sg));
			host->sg = sg = sg_next(sg);
			if (!sg)
				goto done;
			offset = len - remaining;
			buf = sg_virt(sg);
			rk2818_sdmmc_pull_data(host, buf, offset);
			nbytes += offset;
		}

		status = readl(host->regs + SDMMC_MINTSTS);
		writel(SDMMC_INT_RXDR, host->regs + SDMMC_RINTSTS); // clear RXDR interrupt
		if (status & RK2818_MCI_DATA_ERROR_FLAGS) {
			host->data_status = status;
			data->bytes_xfered += nbytes;
			smp_wmb();
			rk2818_sdmmc_set_pending(host, EVENT_DATA_ERROR);
			tasklet_schedule(&host->tasklet);
			return;
		}
		count ++;
	} while (status & SDMMC_INT_RXDR); // if the RXDR is ready let read again
	len = SDMMC_GET_FCNT(readl(host->regs + SDMMC_STATUS));
	host->pio_offset = offset;
	data->bytes_xfered += nbytes;
	xjhprintk("[xjh] %s exit-1\n", __FUNCTION__);
	return;

done:
	data->bytes_xfered += nbytes;
	smp_wmb();
	rk2818_sdmmc_set_pending(host, EVENT_XFER_COMPLETE);
	xjhprintk("[xjh] %s exit-2\n", __FUNCTION__);
}

static void rk2818_sdmmc_write_data_pio(struct rk2818_sdmmc_host *host)
{
	struct scatterlist	*sg = host->sg;
	void			*buf = sg_virt(sg);
	unsigned int		offset = host->pio_offset;
	struct mmc_data		*data = host->data;
	u32			status;
	unsigned int		nbytes = 0,len;

	do {

		len = SDMMC_FIFO_SZ - (SDMMC_GET_FCNT(readl(host->regs + SDMMC_STATUS)) << 2);
		if (likely(offset + len <= sg->length)) {
			rk2818_sdmmc_push_data(host, (void *)(buf + offset),len);

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

			rk2818_sdmmc_push_data(host, (void *)(buf + offset), remaining);
			nbytes += remaining;

			host->sg = sg = sg_next(sg);
			if (!sg) {
				goto done;
			}

			offset = len - remaining;
			buf = sg_virt(sg);
			rk2818_sdmmc_push_data(host, (void *)buf, offset);
			nbytes += offset;
		}

		status = readl(host->regs + SDMMC_MINTSTS);
		writel(SDMMC_INT_TXDR, host->regs + SDMMC_RINTSTS); // clear RXDR interrupt
		if (status & RK2818_MCI_DATA_ERROR_FLAGS) {
			host->data_status = status;
			data->bytes_xfered += nbytes;
			smp_wmb();
			rk2818_sdmmc_set_pending(host, EVENT_DATA_ERROR);
			tasklet_schedule(&host->tasklet);
			return;
		}
	} while (status & SDMMC_INT_TXDR); // if TXDR, let write again

	host->pio_offset = offset;
	data->bytes_xfered += nbytes;

	return;

done:
	data->bytes_xfered += nbytes;
	smp_wmb();
	rk2818_sdmmc_set_pending(host, EVENT_XFER_COMPLETE);
}

static void rk2818_sdmmc_cmd_interrupt(struct rk2818_sdmmc_host *host, u32 status)
{
	if(!host->cmd_status) 
		host->cmd_status = status;
	smp_wmb();
	rk2818_sdmmc_set_pending(host, EVENT_CMD_COMPLETE);
	tasklet_schedule(&host->tasklet);
}

static irqreturn_t rk2818_sdmmc_interrupt(int irq, void *data)
{
	struct rk2818_sdmmc_host	*host = data;
	u32			status,  pending;
	unsigned int		pass_count = 0;
	bool present;
	bool present_old;
	
	spin_lock(&host->lock);
	do {
		status = readl(host->regs + SDMMC_RINTSTS);
		pending = readl(host->regs + SDMMC_MINTSTS);// read only mask reg
		if (!pending)
			break;
		if(pending & SDMMC_INT_CD) {
		    writel(SDMMC_INT_CD, host->regs + SDMMC_RINTSTS);  // clear interrupt
	
			present = rk2818_sdmmc_get_cd(host->mmc);
			present_old = test_bit(RK2818_MMC_CARD_PRESENT, &host->flags);
			if(present != present_old) {
				
				if (present != 0) {
					set_bit(RK2818_MMC_CARD_PRESENT, &host->flags);
				} else {
					clear_bit(RK2818_MMC_CARD_PRESENT, &host->flags);
				}			

				if(host->pdev->id ==0) {	//sdmmc0	
					xjhprintk("[xjh] %s >> %s >> %d sd/mmc insert/remove occur: Removal %d present %d present_old %d\n",
							__FILE__, __FUNCTION__,__LINE__,sdmmc0_disable_Irq_ForRemoval,present,present_old);
					if(0 == sdmmc0_disable_Irq_ForRemoval) {
					 	mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(20));
						if(!test_bit(RK2818_MMC_CARD_PRESENT, &host->flags))
							sdmmc0_disable_Irq_ForRemoval = 1;
					}
					else
				 		mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(RK28_SDMMC0_SWITCH_POLL_DELAY));
				} else {	//sdio
						mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(20));
				}
			}
		}
		
		if(pending & RK2818_MCI_CMD_ERROR_FLAGS) {
		    writel(RK2818_MCI_CMD_ERROR_FLAGS, host->regs + SDMMC_RINTSTS);  //  clear interrupt
		    if(status & SDMMC_INT_HLE)
		    {
			    xjhprintk("[xjh] %s :cmd transfer error(int status 0x%x cmd %d host->state %d pending_events %d)\n", 
					__FUNCTION__,status,host->cmd->opcode,host->state,host->pending_events);
		    	
		    }
		    host->cmd_status = status;
			smp_wmb();
		    rk2818_sdmmc_set_pending(host, EVENT_CMD_COMPLETE);
		    tasklet_schedule(&host->tasklet);
			host->cmd_tmo = 1;
			xjhprintk("[xjh] %s :cmd transfer error(int status 0x%x cmd %d host->state %d pending_events %d)\n", 
					__FUNCTION__,status,host->cmd->opcode,host->state,host->pending_events);
		}

		if (pending & RK2818_MCI_DATA_ERROR_FLAGS) { // if there is an error, let report DATA_ERROR
			writel(RK2818_MCI_DATA_ERROR_FLAGS, host->regs + SDMMC_RINTSTS);  // clear interrupt
			host->data_status = status;
			smp_wmb();
			rk2818_sdmmc_set_pending(host, EVENT_DATA_ERROR);
			tasklet_schedule(&host->tasklet);
			xjhprintk("[xjh] %s :data transfer error(int status 0x%x host->state %d pending_events %d)\n", 
					__FUNCTION__,status,host->state,host->pending_events);
		}


		if(pending & SDMMC_INT_DTO) {
		    writel(SDMMC_INT_DTO, host->regs + SDMMC_RINTSTS);  // clear interrupt
		    if (!host->data_status)
				host->data_status = status;
			smp_wmb();
		 /*   if(host->dir_status == RK2818_MCI_RECV_STATUS) {
				if(host->sg) 
					rk2818_sdmmc_read_data_pio(host);
		    }*/
		    rk2818_sdmmc_set_pending(host, EVENT_DATA_COMPLETE);
		    tasklet_schedule(&host->tasklet);
		}

		if (pending & SDMMC_INT_RXDR) {
		    writel(SDMMC_INT_RXDR, host->regs + SDMMC_RINTSTS);  //  clear interrupt
		    if(host->sg) 
			    rk2818_sdmmc_read_data_pio(host);
		}

		if (pending & SDMMC_INT_TXDR) {
		    writel(SDMMC_INT_TXDR, host->regs + SDMMC_RINTSTS);  //  clear interrupt
		    if(host->sg) {
				rk2818_sdmmc_write_data_pio(host);
		    }
		}

		if (pending & SDMMC_INT_CMD_DONE) {
		   	writel(SDMMC_INT_CMD_DONE, host->regs + SDMMC_RINTSTS);  //  clear interrupt
		    rk2818_sdmmc_cmd_interrupt(host, status);
			host->cmd_tmo = 1;
			
		}
		if(pending & SDMMC_INT_SDIO) {
			writel(SDMMC_INT_SDIO, host->regs + SDMMC_RINTSTS);
			mmc_signal_sdio_irq(host->mmc);
		}
	} while (pass_count++ < 5);
	
	spin_unlock(&host->lock);
	

	//return IRQ_HANDLED;
	return pass_count ? IRQ_HANDLED : IRQ_NONE;
}

static void rk2818_sdmmc_detect_change(unsigned long host_data)
{
	struct rk2818_sdmmc_host *host = (struct rk2818_sdmmc_host *) host_data;
	//bool present;
	//bool present_old;
	//unsigned long flags;
	#if 0
	mrq = host->mrq;
	if (mrq) {

		host->data = NULL;
		host->cmd = NULL;
		xjhprintk("[xjh] %s >> %s >> %d stage 2\n", __FILE__, __FUNCTION__,__LINE__);

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
			xjhprintk("[xjh] %s host %p\n", __FUNCTION__,host);
			rk2818_sdmmc_stop_dma(host);
			break;
		case STATE_DATA_BUSY:
		case STATE_DATA_ERROR:
			if (mrq->data->error == -EINPROGRESS)
				mrq->data->error = -ENOMEDIUM;
			if (!mrq->stop)
				break;
		case STATE_SENDING_STOP:
			mrq->stop->error = -ENOMEDIUM;
			break;
		}

		rk2818_sdmmc_request_end(host, mrq);
	}
	#endif

 // spin_lock( &sdmmc0_spinlock);
//	xjhprintk("%s....%s....%d	 **** timer open, then enable IRQ_OF_removal/insertion*****xbw****\n",__FUNCTION__,__FILE__,__LINE__);
//	sdmmc0_disable_Irq_ForRemoval = 0; //打开中断	   
//	spin_unlock( &sdmmc0_spinlock); 	   
	
	mmc_detect_change(host->mmc, 0);
	
}


/*--------------------add communication interface with applications ---------------------------------*/
int resetTimes = 0;//统计连续reset的次数。
ssize_t sdmmc_reset_store(struct kobject *kobj, struct kobj_attribute *attr,
			 const char *buf, size_t count)
{
	//或者从kobj追溯到rk2818_sdmmc_host，接下来尝试
    struct rk2818_sdmmc_host *host =  mmc0_host;  
    struct mmc_host *mmc = host->mmc;
    int currentTimes=0;
	unsigned long flags;
    
    if( !strncmp(buf,"RemoveDone" , strlen("RemoveDone")) )
    {
        xjhprintk("%s------mmc receive the message of %s -----\n", __func__ , buf);
       
        local_irq_save(flags);
        del_timer(&host->detect_timer);
        sdmmc0_disable_Irq_ForRemoval = 0; //打开中断      
        local_irq_restore(flags);
	   
       //主动执行一次检测, 若有卡存在
       if(rk2818_sdmmc_get_cd(host->mmc)) {
		//	mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(20));
			set_bit(RK2818_MMC_CARD_PRESENT, &host->flags);
		    mmc_detect_change(mmc, 0);
        }
    }
    else if( !strncmp(buf,"Removing" , strlen("Removing")) )
    {        
        xjhprintk("%s------mmc receive the message of %s -----\n", __func__ , buf);

        //vold is removing, so modify the timer.
        mod_timer(&host->detect_timer, jiffies +msecs_to_jiffies(RK28_SDMMC0_SWITCH_POLL_DELAY));        
    }
    else if( !strncmp(buf,"to_reset!" , strlen("to_reset!")) ) 
    {       
        xjhprintk("%s------mmc receive the message of %s -----\n", __func__ , buf);

        //设置reset的允许次数
        ++resetTimes;
        currentTimes = resetTimes;
        
        //连续调用reset超过次数，不执行。        
        if(currentTimes <= 3)
        {          
            xjhprintk("%s....%s....%d   **** Begin to call rk28_sdmmc_reset() Times=%d *****xbw****\n",__FUNCTION__,__FILE__,__LINE__, resetTimes);
           // rk28_sdmmc_reset();
        }
    }
    else if(!strncmp(buf,"mounted!" , strlen("mounted!")))
    {        
        xjhprintk("%s------mmc receive the message of %s -----\n", __func__ , buf);
        resetTimes = 0;        
    }
	else if(!strncmp(buf,"dump_reg" , strlen("dump_reg")))
	{
		unsigned long cpsr_tmp;
		xjhprintk("-------- SD/MMC/SDIO dump Registers-----------------\n");
		xjhprintk("SDMMC_CTRL:\t0x%x\n",readl(host->regs + SDMMC_CTRL));
		xjhprintk("SDMMC_PWREN:\t0x%x\n",readl(host->regs + SDMMC_PWREN));
		xjhprintk("SDMMC_CLKDIV:\t0x%x\n",readl(host->regs + SDMMC_CLKDIV));
		xjhprintk("SDMMC_CLKSRC:\t0x%x\n",readl(host->regs + SDMMC_CLKSRC));
		xjhprintk("SDMMC_CLKENA:\t0x%x\n",readl(host->regs + SDMMC_CLKENA));
		xjhprintk("SDMMC_TMOUT:\t0x%x\n",readl(host->regs + SDMMC_TMOUT));
		xjhprintk("SDMMC_CTYPE:\t0x%x\n",readl(host->regs + SDMMC_CTYPE));
		xjhprintk("SDMMC_BLKSIZ:\t0x%x\n",readl(host->regs + SDMMC_BLKSIZ));
		xjhprintk("SDMMC_BYTCNT:\t0x%x\n",readl(host->regs + SDMMC_BYTCNT));
		xjhprintk("SDMMC_INTMASK:\t0x%x\n",readl(host->regs + SDMMC_INTMASK));
		xjhprintk("SDMMC_CMDARG:\t0x%x\n",readl(host->regs + SDMMC_CMDARG));
		xjhprintk("SDMMC_CMD:\t0x%x\n",readl(host->regs + SDMMC_CMD));
		xjhprintk("SDMMC_RESP0:\t0x%x\n",readl(host->regs + SDMMC_RESP0));
		xjhprintk("SDMMC_RESP1:\t0x%x\n",readl(host->regs + SDMMC_RESP1));
		xjhprintk("SDMMC_RESP2:\t0x%x\n",readl(host->regs + SDMMC_RESP2));
		xjhprintk("SDMMC_RESP3:\t0x%x\n",readl(host->regs + SDMMC_RESP3));
		xjhprintk("SDMMC_MINTSTS:\t0x%x\n",readl(host->regs + SDMMC_MINTSTS));
		xjhprintk("SDMMC_RINTSTS:\t0x%x\n",readl(host->regs + SDMMC_RINTSTS));
		xjhprintk("SDMMC_STATUS:\t0x%x\n",readl(host->regs + SDMMC_STATUS));
		xjhprintk("SDMMC_FIFOTH:\t0x%x\n",readl(host->regs + SDMMC_FIFOTH));
		xjhprintk("SDMMC_CDETECT:\t0x%x\n",readl(host->regs + SDMMC_CDETECT));
		xjhprintk("SDMMC_WRTPRT:\t0x%x\n",readl(host->regs + SDMMC_WRTPRT));
		xjhprintk("SDMMC_TCBCNT:\t0x%x\n",readl(host->regs + SDMMC_TCBCNT));
		xjhprintk("SDMMC_TBBCNT:\t0x%x\n",readl(host->regs + SDMMC_TBBCNT));
		xjhprintk("SDMMC_DEBNCE:\t0x%x\n",readl(host->regs + SDMMC_DEBNCE));
		xjhprintk("-------- Host states-----------------\n");
		xjhprintk("host->state:\t0x%x\n",host->state);
		xjhprintk("host->pending_events:\t0x%x\n",host->pending_events);
		xjhprintk("host->cmd_status:\t0x%x\n",host->cmd_status);
		xjhprintk("host->data_status:\t0x%x\n",host->data_status);
		xjhprintk("host->stop_cmdr:\t0x%x\n",host->stop_cmdr);
		xjhprintk("host->dir_status:\t0x%x\n",host->dir_status);
		xjhprintk("host->completed_events:\t0x%x\n",host->completed_events);
		xjhprintk("host->dma_chn:\t0x%x\n",host->dma_chn);
		xjhprintk("host->use_dma:\t0x%x\n",host->use_dma);
		xjhprintk("host->no_detect:\t0x%x\n",host->no_detect);
		xjhprintk("host->bus_hz:\t0x%x\n",host->bus_hz);
		xjhprintk("host->current_speed:\t0x%x\n",host->current_speed);
		xjhprintk("host->ctype:\t0x%x\n",host->ctype);
		xjhprintk("host->clock:\t0x%x\n",host->clock);
		xjhprintk("host->flags:\t0x%x\n",host->flags);
		xjhprintk("host->irq:\t0x%x\n",host->irq);
		xjhprintk("-------- rk2818 CPU register-----------------\n");
		__asm__ volatile ("mrs	%0, cpsr		@ local_irq_save\n":"=r" (cpsr_tmp)::"memory", "cc"	);
		xjhprintk("cpsr:\t0x%x\n",cpsr_tmp);
		

	}

    return count;
}



struct kobj_attribute mmc_reset_attrs = 
{
        .attr = {
                .name = "rescan",
                .mode = 0777},
        .show = NULL,
        .store = sdmmc_reset_store,
};
struct attribute *mmc_attrs[] = 
{
        &mmc_reset_attrs.attr,
        NULL
};

static struct kobj_type mmc_kset_ktype = {
	.sysfs_ops	= &kobj_sysfs_ops,
	.default_attrs = &mmc_attrs[0],
};
static int rk28_sdmmc0_add_attr( struct platform_device *pdev )
{
        int result;
		 struct kobject *parentkobject; 
        struct kobject * me = kmalloc(sizeof(struct kobject) , GFP_KERNEL );
        if( !me )
                return -ENOMEM;
        memset(me ,0,sizeof(struct kobject));
        kobject_init( me , &mmc_kset_ktype );
        //result = kobject_add( me , &pdev->dev.kobj , "%s", "RESET" );
        parentkobject = &pdev->dev.kobj ;
		 result = kobject_add( me , parentkobject->parent->parent, "%s", "resetSdCard" );	
        return result;
}

static void rk2818_sdmmc_check_status(unsigned long data)
{
	struct rk2818_sdmmc_host *host = (struct rk2818_sdmmc_host *)data;
        struct rk2818_sdmmc_platform_data *pdata = host->pdev->dev.platform_data;
	unsigned int status;

 	if (!pdata->status)
	{
		mmc_detect_change(host->mmc, 0);	
		return;
	}
	
	status = pdata->status(mmc_dev(host->mmc));

	if (status ^ host->oldstatus)
	{
		pr_info("%s: slot status change detected(%d-%d)\n",mmc_hostname(host->mmc), host->oldstatus, status);
		if (status)
			mmc_detect_change(host->mmc,  100);
		else
			mmc_detect_change(host->mmc, 0);
	}
	
	host->oldstatus = status;
	
}	

static void rk2818_sdmmc_status_notify_cb(int card_present, void *dev_id)
{
	struct rk2818_sdmmc_host *host = dev_id;
        printk(KERN_INFO "%s, card_present %d\n", mmc_hostname(host->mmc), card_present);
	rk2818_sdmmc_check_status((unsigned long)host);
}

/*-------------------end of add communication interface with applications ---------------------------------*/

static int rk2818_sdmmc_probe(struct platform_device *pdev)
{
	struct rk2818_sdmmc_host *host = NULL;
	struct mmc_host *mmc;
	struct resource	*res;
	struct rk2818_sdmmc_platform_data *pdata;
	int	ret = 0;
	int tmo = 200;

	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "No platform data\n");
		return -EINVAL;
	}
	if(pdata->io_init)
		pdata->io_init();

	mmc = mmc_alloc_host(sizeof(struct rk2818_sdmmc_host), &pdev->dev);
	if (!mmc)
	{
		dev_err(&pdev->dev, "Mmc alloc host failture\n");
		return -ENOMEM;
	}
	host = mmc_priv(mmc);

	host->mmc = mmc;
	host->pdev = pdev;
	host->dev = &pdev->dev;
	host->dma_chn = -1;

	host->use_dma = pdata->use_dma;
	host->no_detect = pdata->no_detect;
	memcpy(host->dma_name, pdata->dma_name, 8);

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
	{
		dev_err(&pdev->dev, "Cannot find IO resource\n");
		ret = -ENOENT;
		goto err_free_host;
	}
	host->mem = request_mem_region(res->start, resource_size(res), pdev->name);
	if(host->mem == NULL)
	{
		dev_err(&pdev->dev, "Cannot request IO\n");
		ret = -ENXIO;
		goto err_free_host;
	}
	host->regs = ioremap(res->start, res->end - res->start + 1);
	if (!host->regs)
	{
		dev_err(&pdev->dev, "Cannot map IO\n");
		ret = -ENXIO;
	    goto err_release_resource;
	}

	host->irq = ret =  platform_get_irq(pdev, 0);
	if (ret < 0)
	{
		dev_err(&pdev->dev, "Cannot find IRQ\n");
		goto err_free_map;
	}

	/* setup wifi card detect change */	
	if (pdata->register_status_notify)
	{
		pdata->register_status_notify(rk2818_sdmmc_status_notify_cb, host);
	}

	spin_lock_init(&host->lock);
	INIT_LIST_HEAD(&host->queue);

	host->clk = clk_get(&pdev->dev, "sdmmc");
	if (IS_ERR(host->clk)) {    
		dev_err(&pdev->dev, "failed to find clock source.\n");
		ret = PTR_ERR(host->clk);
		host->clk = NULL;
		goto err_free_map;
	}    
    clk_enable(host->clk);
	host->bus_hz = clk_get_rate(host->clk);

  	writel((SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET | SDMMC_CTRL_INT_ENABLE), host->regs + SDMMC_CTRL);
  	while (--tmo && readl(host->regs + SDMMC_CTRL) & 
			(SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET))
		udelay(5);
	if(--tmo < 0){
		dev_err(&pdev->dev, "failed to reset controller and fifo(timeout = 1ms).\n");
	    goto err_free_clk;
	}
 
	writel(0xFFFFFFFF, host->regs + SDMMC_RINTSTS);
	writel(0, host->regs + SDMMC_INTMASK); // disable all mmc interrupt first

  	/* Put in max timeout */
  	writel(0xFFFFFFFF, host->regs + SDMMC_TMOUT);

    /* DMA Size = 8, RXMark = 15, TXMark = 16 */
	writel((3 << 28) | (15 << 16) | 16, host->regs + SDMMC_FIFOTH);
	/* disable clock to CIU */
	writel(0, host->regs + SDMMC_CLKENA);
	writel(0, host->regs + SDMMC_CLKSRC);

	tasklet_init(&host->tasklet, rk2818_sdmmc_tasklet_func, (unsigned long)host);

	ret = request_irq(host->irq, rk2818_sdmmc_interrupt, 0, dev_name(&pdev->dev), host);
	if (ret){
		dev_err(&pdev->dev, "Cannot claim IRQ %d.\n", host->irq);
	    goto err_free_clk;
	}


	platform_set_drvdata(pdev, host);
	dev_dbg(host->dev, "pdev->id = %d\n", pdev->id);
	mmc->ops = &(rk2818_sdmmc_ops[pdev->id]);

	mmc->f_min = host->bus_hz/510;
	mmc->f_max = host->bus_hz/2; 
	dev_dbg(&pdev->dev, "bus_hz = %u\n", host->bus_hz);
	mmc->ocr_avail = pdata->host_ocr_avail;
	mmc->caps = pdata->host_caps;
	
	mmc->max_phys_segs = 64;
	mmc->max_hw_segs = 64;
	mmc->max_blk_size = 4095;
	mmc->max_blk_count = 512;
	mmc->max_req_size = 4095 * 512;
	mmc->max_seg_size = 4095 * 4;

	rk2818_sdmmc_set_power(host, 0);

	/* Create card detect handler thread for the host */
	setup_timer(&host->detect_timer, rk2818_sdmmc_detect_change,
			(unsigned long)host);

	writel(0xFFFFFFFF, host->regs + SDMMC_RINTSTS);
	writel(SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | RK2818_MCI_ERROR_FLAGS | SDMMC_INT_CD,
			host->regs + SDMMC_INTMASK);

	if((strncmp(host->dma_name, "sdio", strlen("sdio")) == 0) &&
		(readl(host->regs + SDMMC_STATUS) & SDMMC_STAUTS_DATA_BUSY))
	{
		dev_err(host->dev, "sdio is busy, fail to init sdio.please check if wifi_d0's level is high?\n");
		ret = -EINVAL;
		return ret;
	}
#if 0	
	/* Assume card is present initially */
	if(rk2818_sdmmc_get_cd(host->mmc) == 0 &&strncmp(host->dma_name, "sdio", strlen("sdio")) == 0)
	{
		dev_err(&pdev->dev, "failed to detect sdio.\n");
		return 0;
	}
	else if(rk2818_sdmmc_get_cd(host->mmc) != 0)
		set_bit(RK2818_MMC_CARD_PRESENT, &host->flags);
	else
		clear_bit(RK2818_MMC_CARD_PRESENT, &host->flags);
#endif
	
	ret = mmc_add_host(mmc);

        
	if (strncmp(host->dma_name, "sdio", strlen("sdio")) == 0)
	{
		wifi_mmc_host = mmc;
		if(request_dma(1, host->dma_name) == 0)
			host->dma_chn = 1;
		else
		{
			ret = -EINVAL;
			goto err_remove_host;
		}
	}

	if(ret) 
	{
		dev_err(&pdev->dev, "failed to add mmc host.\n");
		goto err_free_irq;
	}
	if (strncmp(host->dma_name, "sd_mmc", strlen("sd_mmc")) == 0) {
		xjhprintk("[xjh] sdmmc0 add interface with application\n");
		sdmmc0_disable_Irq_ForRemoval = 0; //打开中断
		mmc0_host = host;
		rk28_sdmmc0_add_attr(pdev);
	}
	
#if defined (CONFIG_DEBUG_FS)
	rk2818_sdmmc_init_debugfs(host);
#endif
	writel(SDMMC_CTRL_INT_ENABLE, host->regs + SDMMC_CTRL); // enable mci interrupt

	dev_info(&pdev->dev, "RK2818 MMC controller used as %s, at irq %d\n", 
				host->dma_name, host->irq);
	return 0;
err_remove_host:
	mmc_remove_host(host->mmc);
err_free_irq:
	free_irq(host->irq, host);
err_free_clk:
	clk_disable(host->clk);
	clk_put(host->clk);
err_free_map:
	iounmap(host->regs);
err_release_resource:
	release_resource(host->mem);
err_free_host:
	kfree(host);
	return ret;
}

static int __exit rk2818_sdmmc_remove(struct platform_device *pdev)
{
	struct rk2818_sdmmc_host *host = platform_get_drvdata(pdev);

	if (strncmp(host->dma_name, "sdio", 4) == 0)
		wifi_mmc_host = NULL;

	writel(0xFFFFFFFF, host->regs + SDMMC_RINTSTS);
	writel(0, host->regs + SDMMC_INTMASK); // disable all mmc interrupt first

	platform_set_drvdata(pdev, NULL);

	dev_dbg(&pdev->dev, "remove host\n");

	writel(0, host->regs + SDMMC_CLKENA);
	writel(0, host->regs + SDMMC_CLKSRC);

	del_timer_sync(&host->detect_timer);
	set_bit(RK2818_MMC_SHUTDOWN, &host->flags);
	mmc_remove_host(host->mmc);
	mmc_free_host(host->mmc);

	clk_disable(host->clk);
	clk_put(host->clk);
	free_irq(host->irq, host);
	iounmap(host->regs);
	release_resource(host->mem);
	kfree(host);
	return 0;
}

static int rk2818_sdmmc_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct rk2818_sdmmc_host *host = platform_get_drvdata(pdev);
#ifdef CONFIG_PM
	writel(0, host->regs + SDMMC_CLKENA);
	clk_disable(host->clk);
#endif
	return 0;
}

static int rk2818_sdmmc_resume(struct platform_device *pdev)
{
	struct rk2818_sdmmc_host *host = platform_get_drvdata(pdev);
#ifdef CONFIG_PM
	clk_enable(host->clk);
	writel(SDMMC_CLKEN_ENABLE, host->regs + SDMMC_CLKENA);
#endif
	return 0;
}

static struct platform_driver rk2818_sdmmc_driver = {
	.probe		= rk2818_sdmmc_probe,
	.suspend    = rk2818_sdmmc_suspend,
	.resume     = rk2818_sdmmc_resume,
	.remove		= __exit_p(rk2818_sdmmc_remove),
	.driver		= {
		.name		= "rk2818_sdmmc",
	},
};

static int __init rk2818_sdmmc_init(void)
{
	return platform_driver_register(&rk2818_sdmmc_driver);
}

static void __exit rk2818_sdmmc_exit(void)
{
	platform_driver_unregister(&rk2818_sdmmc_driver);
}

module_init(rk2818_sdmmc_init);
module_exit(rk2818_sdmmc_exit);

MODULE_DESCRIPTION("Driver for RK2818 SDMMC controller");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("kfx kfx@rock-chips.com");

