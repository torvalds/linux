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
#include <mach/rk29_iomap.h>
#include <mach/gpio.h>
#include <asm/dma.h>
#include <mach/rk29-dma-pl330.h>
#include <asm/scatterlist.h>

#include "rk2818-sdmmc.h"

#define RK29_SDMMC_DATA_ERROR_FLAGS	(SDMMC_INT_DRTO | SDMMC_INT_DCRC | SDMMC_INT_HTO | SDMMC_INT_SBE | SDMMC_INT_EBE | SDMMC_INT_FRUN)
#define RK29_SDMMC_CMD_ERROR_FLAGS	(SDMMC_INT_RTO | SDMMC_INT_RCRC | SDMMC_INT_RE | SDMMC_INT_HLE)
#define RK29_SDMMC_ERROR_FLAGS		(RK29_SDMMC_DATA_ERROR_FLAGS | RK29_SDMMC_CMD_ERROR_FLAGS | SDMMC_INT_HLE)
#define RK29_SDMMC_SEND_STATUS		1
#define RK29_SDMMC_RECV_STATUS		2
#define RK29_SDMMC_DMA_THRESHOLD	512

enum {
	EVENT_CMD_COMPLETE = 0,
	EVENT_XFER_COMPLETE,
	EVENT_DATA_COMPLETE,
	EVENT_DATA_ERROR,
	EVENT_XFER_ERROR
};

enum rk29_sdmmc_state {
	STATE_IDLE = 0,
	STATE_SENDING_CMD,
	STATE_SENDING_DATA,
	STATE_DATA_BUSY,
	STATE_SENDING_STOP,
	STATE_DATA_ERROR,
};

static struct rk29_dma_client rk29_dma_sdmmc0_client = {
        .name = "rk29-dma-sdmmc0",
};

static struct rk29_dma_client rk29_dma_sdio1_client = {
        .name = "rk29-dma-sdio1",
};

struct rk29_sdmmc {
	spinlock_t		lock;
	void __iomem		*regs;
	struct clk 		*clk;
	struct scatterlist	*sg;
	unsigned int		pio_offset;
	struct mmc_request	*mrq;
	struct mmc_request	*curr_mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;
	int			dma_chn;
	dma_addr_t		dma_addr;;
	//dma_sg_ll_t		*sg_cpu;
	unsigned int	use_dma:1;
	char			dma_name[8];
	u32			cmd_status;
	u32			data_status;
	u32			stop_cmdr;
	u32			dir_status;
	struct tasklet_struct	tasklet;
	unsigned long		pending_events;
	unsigned long		completed_events;
	enum rk29_sdmmc_state	state;
	struct list_head	queue;
	u32			bus_hz;
	u32			current_speed;
	struct platform_device	*pdev;
	struct mmc_host		*mmc;
	u32			ctype;
	struct list_head	queue_node;
	unsigned int		clock;
	unsigned long		flags;
#define RK29_SDMMC_CARD_PRESENT	0
#define RK29_SDMMC_CARD_NEED_INIT	1
#define RK29_SDMMC_SHUTDOWN		2
	int			id;
	int			irq;
	struct timer_list	detect_timer;
        unsigned int            oldstatus;
};

#define rk29_sdmmc_test_and_clear_pending(host, event)		\
	test_and_clear_bit(event, &host->pending_events)
#define rk29_sdmmc_set_completed(host, event)			\
	set_bit(event, &host->completed_events)

#define rk29_sdmmc_set_pending(host, event)				\
	set_bit(event, &host->pending_events)

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

static void rk29_sdmmc_init_debugfs(struct rk29_sdmmc *host)
{
	struct mmc_host		*mmc = host->mmc;
	struct dentry		*root;
	struct dentry		*node;

	root = mmc->debugfs_root;
	if (!root)
		return;

	node = debugfs_create_file("regs", S_IRUSR, root, host,
			&rk29_sdmmc_regs_fops);
	if (IS_ERR(node))
		return;
	if (!node)
		goto err;

	node = debugfs_create_file("req", S_IRUSR, root, host, &rk29_sdmmc_req_fops);
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

	timeout = ns_to_clocks(host->clock, data->timeout_ns) + data->timeout_clks;
	rk29_sdmmc_write(host->regs, SDMMC_TMOUT, 0xffffffff);
	///rk29_sdmmc_write(host->regs, SDMMC_TMOUT, (timeout << 8) | (70));
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


static void rk29_sdmmc_start_command(struct rk29_sdmmc *host,
		struct mmc_command *cmd, u32 cmd_flags)
{
 	int tmo = 5000;
	unsigned long flags;
 	host->cmd = cmd;
	dev_vdbg(&host->pdev->dev,
			"start cmd:%d ARGR=0x%08x CMDR=0x%08x\n",
			cmd->opcode, cmd->arg, cmd_flags);
	local_irq_save(flags);
	rk29_sdmmc_write(host->regs, SDMMC_CMDARG, cmd->arg); // write to SDMMC_CMDARG register
	rk29_sdmmc_write(host->regs, SDMMC_CMD, cmd_flags | SDMMC_CMD_START); // write to SDMMC_CMD register
	local_irq_restore(flags);

	/* wait until CIU accepts the command */
	while (--tmo && (rk29_sdmmc_read(host->regs, SDMMC_CMD) & SDMMC_CMD_START)) 
		cpu_relax();
	if(!tmo){
		printk("Enter:%s %d start tmo err!!\n",__FUNCTION__,__LINE__);	
	}
}

static void rk29_sdmmc_reset_fifo(struct rk29_sdmmc *host)
{
	unsigned long flags;

	local_irq_save(flags);
	rk29_sdmmc_write(host->regs, SDMMC_CTRL, rk29_sdmmc_read(host->regs, SDMMC_CTRL) | SDMMC_CTRL_FIFO_RESET);
	/* wait till resets clear */
	while (rk29_sdmmc_read(host->regs, SDMMC_CTRL) & SDMMC_CTRL_FIFO_RESET);
	local_irq_restore(flags);
}

static int rk29_sdmmc_wait_unbusy(struct rk29_sdmmc *host)
{
	const int time_out_us = 500000;
	int time_out = time_out_us, time_out2 = 3;

	while (rk29_sdmmc_read(host->regs, SDMMC_STATUS) & SDMMC_STAUTS_DATA_BUSY) {
		udelay(1);
		time_out--;
		if (!time_out) {
			time_out = time_out_us;
			rk29_sdmmc_reset_fifo(host);			
			if (!time_out2)
				break;
			time_out2--;
		}
	}

	return time_out_us - time_out;
}

static void send_stop_cmd(struct rk29_sdmmc *host, struct mmc_data *data)
{
	rk29_sdmmc_wait_unbusy(host);

	if(!(rk29_sdmmc_read(host->regs, SDMMC_STATUS) & SDMMC_STAUTS_FIFO_EMPTY)) {
		rk29_sdmmc_reset_fifo(host);
	}
	rk29_sdmmc_start_command(host, data->stop, host->stop_cmdr);
}

static void rk29_sdmmc_dma_cleanup(struct rk29_sdmmc *host)
{
	struct mmc_data			*data = host->data;
	if (data) 
		dma_unmap_sg(&host->pdev->dev, data->sg, data->sg_len,
		     ((data->flags & MMC_DATA_WRITE)
		      ? DMA_TO_DEVICE : DMA_FROM_DEVICE));
}

static void rk29_sdmmc_stop_dma(struct rk29_sdmmc *host)
{
	if(host->use_dma == 0)
		return;
	if (host->dma_chn > 0) {
		//dma_stop_channel(host->dma_chn);
		rk29_dma_ctrl(host->dma_chn,RK29_DMAOP_STOP);
		rk29_sdmmc_dma_cleanup(host);
	} else {
		/* Data transfer was stopped by the interrupt handler */
		rk29_sdmmc_set_pending(host, EVENT_XFER_COMPLETE);
	}
}

/* This function is called by the DMA driver from tasklet context. */
static void rk29_sdmmc_dma_complete(void *arg, int size, enum rk29_dma_buffresult result)  ///(int chn, dma_irq_type_t type, void *arg)
{
	struct rk29_sdmmc	*host = arg;
	struct mmc_data		*data = host->data;

	if(host->use_dma == 0)
		return;
	dev_vdbg(&host->pdev->dev, "DMA complete\n");
			
	spin_lock(&host->lock);
	rk29_sdmmc_dma_cleanup(host);
	/*
	 * If the card was removed, data will be NULL. No point trying
	 * to send the stop command or waiting for NBUSY in this case.
	 */
	if (data) {
		rk29_sdmmc_set_pending(host, EVENT_XFER_COMPLETE);
		tasklet_schedule(&host->tasklet);
	}
	spin_unlock(&host->lock);
	if(result != RK29_RES_OK){
		rk29_dma_ctrl(host->dma_chn,RK29_DMAOP_STOP);
		rk29_dma_ctrl(host->dma_chn,RK29_DMAOP_FLUSH);
		rk29_sdmmc_write(host->regs, SDMMC_CTRL, (rk29_sdmmc_read(host->regs, SDMMC_CTRL))&(~SDMMC_CTRL_DMA_ENABLE));
		printk("%s: sdio dma complete err\n",__FUNCTION__);
	}
}

static int rk29_sdmmc_submit_data_dma(struct rk29_sdmmc *host, struct mmc_data *data)
{
	struct scatterlist		*sg;
	unsigned int			i,direction;
	int dma_len=0;
	
	if(host->use_dma == 0)
		return -ENOSYS;
	/* If we don't have a channel, we can't do DMA */
	if (host->dma_chn < 0)
		return -ENODEV;

	/*
	 * We don't do DMA on "complex" transfers, i.e. with
	 * non-word-aligned buffers or lengths. Also, we don't bother
	 * with all the DMA setup overhead for short transfers.
	 */
	if (data->blocks * data->blksz < RK29_SDMMC_DMA_THRESHOLD)
	{
		rk29_sdmmc_write(host->regs, SDMMC_INTMASK,rk29_sdmmc_read(host->regs,SDMMC_INTMASK) | SDMMC_INT_TXDR | SDMMC_INT_RXDR );	
		return -EINVAL;
	}
	else
	{
		rk29_sdmmc_write(host->regs, SDMMC_INTMASK,rk29_sdmmc_read(host->regs,SDMMC_INTMASK) & (~( SDMMC_INT_TXDR | SDMMC_INT_RXDR)));
	}
	if (data->blksz & 3)
		return -EINVAL;
	for_each_sg(data->sg, sg, data->sg_len, i) {
		if (sg->offset & 3 || sg->length & 3)
			return -EINVAL;
	}
	if (data->flags & MMC_DATA_READ)
		direction = RK29_DMASRC_HW;  
	else
		direction = RK29_DMASRC_MEM;  						
    rk29_dma_devconfig(host->dma_chn, direction, (unsigned long )(host->dma_addr));
	dma_len = dma_map_sg(&host->pdev->dev, data->sg, data->sg_len, 
			(data->flags & MMC_DATA_READ)? DMA_FROM_DEVICE : DMA_TO_DEVICE);						                	   
	for (i = 0; i < dma_len; i++)                              
    	rk29_dma_enqueue(host->dma_chn, host, sg_dma_address(&data->sg[i]),sg_dma_len(&data->sg[i]));  // data->sg->dma_address, data->sg->length);	    	
	rk29_sdmmc_write(host->regs, SDMMC_CTRL, (rk29_sdmmc_read(host->regs, SDMMC_CTRL))|SDMMC_CTRL_DMA_ENABLE);// enable dma
	rk29_dma_ctrl(host->dma_chn, RK29_DMAOP_START);	
	return 0;
}

static void rk29_sdmmc_submit_data(struct rk29_sdmmc *host, struct mmc_data *data)
{
	data->error = -EINPROGRESS;

	WARN_ON(host->data);
	host->sg = NULL;
	host->data = data;

	if (rk29_sdmmc_submit_data_dma(host, data)) {
		host->sg = data->sg;
		host->pio_offset = 0;
		if (data->flags & MMC_DATA_READ)
			host->dir_status = RK29_SDMMC_RECV_STATUS;
		else 
			host->dir_status = RK29_SDMMC_SEND_STATUS;

		rk29_sdmmc_write(host->regs, SDMMC_CTRL, (rk29_sdmmc_read(host->regs, SDMMC_CTRL))&(~SDMMC_CTRL_DMA_ENABLE));
	}

}

static void sdmmc_send_cmd(struct rk29_sdmmc *host, unsigned int cmd, int arg)
{
	int tmo = 10000;
	
	rk29_sdmmc_write(host->regs, SDMMC_CMDARG, arg);
	rk29_sdmmc_write(host->regs, SDMMC_CMD, SDMMC_CMD_START | cmd);		
	while (--tmo && readl(host->regs + SDMMC_CMD) & SDMMC_CMD_START); 
	if(!tmo) {
		printk("%s %d set cmd register timeout error!!!\n",__FUNCTION__,__LINE__);
	} 
}

void rk29_sdmmc_setup_bus(struct rk29_sdmmc *host)
{
	u32 div;

	if (host->clock != host->current_speed) {
		div  = (((host->bus_hz + (host->bus_hz / 5)) / host->clock)) >> 1;
		if(!div)
			div = 1;
		/* store the actual clock for calculations */
		host->clock = (host->bus_hz / div) >> 1;
		/* disable clock */
		rk29_sdmmc_write(host->regs, SDMMC_CLKENA, 0);
		rk29_sdmmc_write(host->regs, SDMMC_CLKSRC,0);
		/* inform CIU */
		sdmmc_send_cmd(host, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);
		/* set clock to desired speed */
		rk29_sdmmc_write(host->regs, SDMMC_CLKDIV, div);
		/* inform CIU */
		sdmmc_send_cmd(host, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);
		/* enable clock */
		rk29_sdmmc_write(host->regs, SDMMC_CLKENA, SDMMC_CLKEN_ENABLE);
		/* inform CIU */
		sdmmc_send_cmd(host, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);

		host->current_speed = host->clock;
	}

	/* Set the current  bus width */
	rk29_sdmmc_write(host->regs, SDMMC_CTYPE, host->ctype);
}

static void rk29_sdmmc_start_request(struct rk29_sdmmc *host)
{
	struct mmc_request	*mrq;
	struct mmc_command	*cmd;
	struct mmc_data		*data;
	u32			cmdflags;
	
	mrq = host->mrq;

	rk29_sdmmc_wait_unbusy(host);

	if(!(rk29_sdmmc_read(host->regs, SDMMC_STATUS) & SDMMC_STAUTS_FIFO_EMPTY)) {
		rk29_sdmmc_reset_fifo(host);
	}
	/* Slot specific timing and width adjustment */
	rk29_sdmmc_setup_bus(host);
	host->curr_mrq = mrq;
	host->pending_events = 0;
	host->completed_events = 0;
	host->data_status = 0;
	data = mrq->data;
	if (data) {
		rk29_sdmmc_set_timeout(host,data);
		rk29_sdmmc_write(host->regs, SDMMC_BYTCNT,data->blksz*data->blocks);
		rk29_sdmmc_write(host->regs, SDMMC_BLKSIZ,data->blksz);
	}
	cmd = mrq->cmd;
	cmdflags = rk29_sdmmc_prepare_command(host->mmc, cmd);
	if (unlikely(test_and_clear_bit(RK29_SDMMC_CARD_NEED_INIT, &host->flags))) 
	    cmdflags |= SDMMC_CMD_INIT; //this is the first command, let set send the initializtion clock
	
	if (data) //we may need to move this code to mci_start_command
		rk29_sdmmc_submit_data(host, data);

	rk29_sdmmc_start_command(host, cmd, cmdflags);

	if (mrq->stop) 
		host->stop_cmdr = rk29_sdmmc_prepare_command(host->mmc, mrq->stop);
	
}

static void rk29_sdmmc_queue_request(struct rk29_sdmmc *host,struct mmc_request *mrq)
{
	spin_lock(&host->lock);
	host->mrq = mrq;
	if (host->state == STATE_IDLE) {
		host->state = STATE_SENDING_CMD;
		rk29_sdmmc_start_request(host);
	} else {
		list_add_tail(&host->queue_node, &host->queue);
	}
	spin_unlock(&host->lock);
}
 
static void rk29_sdmmc_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct rk29_sdmmc *host = mmc_priv(mmc);

	WARN_ON(host->mrq);
	
	if (!test_bit(RK29_SDMMC_CARD_PRESENT, &host->flags)) {
		mrq->cmd->error = -ENOMEDIUM;
		mmc_request_done(mmc, mrq);
		return;
	}
	
	rk29_sdmmc_queue_request(host,mrq);
}

static void rk29_sdmmc_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct rk29_sdmmc *host = mmc_priv(mmc);;

	host->ctype = 0; // set default 1 bit mode

	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_1:
		host->ctype = 0;
		break;
	case MMC_BUS_WIDTH_4:
		host->ctype = SDMMC_CTYPE_4BIT;
		break;
	}
	if (ios->clock) {
		spin_lock(&host->lock);
		/*
		 * Use mirror of ios->clock to prevent race with mmc
		 * core ios update when finding the minimum.
		 */
		host->clock = ios->clock;

		spin_unlock(&host->lock);
	} else {
		spin_lock(&host->lock);
		host->clock = 0;
		spin_unlock(&host->lock);
	}
	switch (ios->power_mode) {
	case MMC_POWER_UP:
		set_bit(RK29_SDMMC_CARD_NEED_INIT, &host->flags);
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
	u32 cdetect = rk29_sdmmc_read(host->regs, SDMMC_CDETECT);
	return (cdetect & SDMMC_CARD_DETECT_N)?0:1;
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
	},
};

static void rk29_sdmmc_request_end(struct rk29_sdmmc *host, struct mmc_request *mrq)
	__releases(&host->lock)
	__acquires(&host->lock)
{
	struct mmc_host		*prev_mmc = host->mmc;
	
	WARN_ON(host->cmd || host->data);
	host->curr_mrq = NULL;
	host->mrq = NULL;

	rk29_sdmmc_wait_unbusy(host);

	if(!(rk29_sdmmc_read(host->regs, SDMMC_STATUS) & SDMMC_STAUTS_FIFO_EMPTY)) {
		rk29_sdmmc_reset_fifo(host);
	}
	if (!list_empty(&host->queue)) {
		host = list_entry(host->queue.next,
				struct rk29_sdmmc, queue_node);
		list_del(&host->queue_node);
		host->state = STATE_SENDING_CMD;
		rk29_sdmmc_start_request(host);
	} else {
		dev_vdbg(&host->pdev->dev, "list empty\n");
		host->state = STATE_IDLE;
	}

	spin_unlock(&host->lock);
	mmc_request_done(prev_mmc, mrq);

	spin_lock(&host->lock);
}

static void rk29_sdmmc_command_complete(struct rk29_sdmmc *host,
			struct mmc_command *cmd)
{
	u32		status = host->cmd_status;

	host->cmd_status = 0;

	if(cmd->flags & MMC_RSP_PRESENT) {

	    if(cmd->flags & MMC_RSP_136) {

		/* Read the response from the card (up to 16 bytes).
		 * RK29 SDMMC controller saves bits 127-96 in SDMMC_RESP3
		 * for easy parsing. But the UNSTUFF_BITS macro in core/mmc.c
		 * core/sd.c expect those bits be in resp[0]. Hence
		 * reverse the response word order.
		 */
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
		dev_vdbg(&host->pdev->dev,
			"command error: status=0x%08x resp=0x%08x\n"
			"cmd=0x%08x arg=0x%08x flg=0x%08x err=%d\n", 
			status, cmd->resp[0], 
			cmd->opcode, cmd->arg, cmd->flags, cmd->error);

		if (cmd->data) {
			host->data = NULL;
			rk29_sdmmc_stop_dma(host);
		}
	} 
}

static void rk29_sdmmc_tasklet_func(unsigned long priv)
{
	struct rk29_sdmmc	*host = (struct rk29_sdmmc *)priv;
	struct mmc_request	*mrq = host->curr_mrq;
	struct mmc_data		*data = host->data;
	struct mmc_command	*cmd = host->cmd;
	enum rk29_sdmmc_state	state = host->state;
	enum rk29_sdmmc_state	prev_state;
	u32			status;

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

			host->cmd = NULL;
			rk29_sdmmc_set_completed(host, EVENT_CMD_COMPLETE);
			rk29_sdmmc_command_complete(host, mrq->cmd);
			if (!mrq->data || cmd->error) {
				rk29_sdmmc_request_end(host, host->curr_mrq);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_DATA;
			/* fall through */

		case STATE_SENDING_DATA:
			if (rk29_sdmmc_test_and_clear_pending(host,
						EVENT_DATA_ERROR)) {
				rk29_sdmmc_stop_dma(host);
				if (data->stop)
					send_stop_cmd(host, data);
				state = STATE_DATA_ERROR;
				break;
			}

			if (!rk29_sdmmc_test_and_clear_pending(host,
						EVENT_XFER_COMPLETE))
				break;

			rk29_sdmmc_set_completed(host, EVENT_XFER_COMPLETE);
			prev_state = state = STATE_DATA_BUSY;
			/* fall through */

		case STATE_DATA_BUSY:
			if (!rk29_sdmmc_test_and_clear_pending(host,
						EVENT_DATA_COMPLETE))
				break;	
			host->data = NULL;
			rk29_sdmmc_set_completed(host, EVENT_DATA_COMPLETE);
			status = host->data_status;
			if (unlikely(status & RK29_SDMMC_DATA_ERROR_FLAGS)) {
				if (status & SDMMC_INT_DRTO) {
					dev_err(&host->pdev->dev,
							"data timeout error\n");
					data->error = -ETIMEDOUT;
				} else if (status & SDMMC_INT_DCRC) {
					dev_err(&host->pdev->dev,
							"data CRC error\n");
					data->error = -EILSEQ;
				} else {
					dev_err(&host->pdev->dev,
						"data FIFO error (status=%08x)\n",
						status);
					data->error = -EIO;
				}
			}else {
				data->bytes_xfered = data->blocks * data->blksz;
				data->error = 0;
			}

			if (!data->stop) {
				rk29_sdmmc_request_end(host, host->curr_mrq);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_STOP;
			if (!data->error)
				send_stop_cmd(host, data);
			/* fall through */

		case STATE_SENDING_STOP:
			if (!rk29_sdmmc_test_and_clear_pending(host,
						EVENT_CMD_COMPLETE))
				break;

			host->cmd = NULL;
			rk29_sdmmc_command_complete(host, mrq->stop);
			rk29_sdmmc_request_end(host, host->curr_mrq);
			goto unlock;
		case STATE_DATA_ERROR:
			if (!rk29_sdmmc_test_and_clear_pending(host,
						EVENT_XFER_COMPLETE))
				break;

			state = STATE_DATA_BUSY;
			break;
		}
	} while (state != prev_state);

	host->state = state;

unlock:
	spin_unlock(&host->lock);

}



inline static void rk29_sdmmc_push_data(struct rk29_sdmmc *host, void *buf,int cnt)
{
    u32* pData = (u32*)buf;

    if (cnt % 4 != 0) 
		cnt = (cnt>>2) +1;
	else
    	cnt = cnt >> 2;
    while (cnt > 0) {
        rk29_sdmmc_write(host->regs, SDMMC_DATA,*pData++);
        cnt--;
    }
}

inline static void rk29_sdmmc_pull_data(struct rk29_sdmmc *host,void *buf,int cnt)
{
    u32* pData = (u32*)buf;
    
    if (cnt % 4 != 0) 
		cnt = (cnt>>2) +1;
	else
    	cnt = cnt >> 2;
    while (cnt > 0) {       
        *pData++ = rk29_sdmmc_read(host->regs, SDMMC_DATA);
        cnt--;
    }
}

static void rk29_sdmmc_read_data_pio(struct rk29_sdmmc *host)
{
	struct scatterlist	*sg = host->sg;
	void			*buf = sg_virt(sg);
	unsigned int		offset = host->pio_offset;
	struct mmc_data		*data = host->data;
	u32			status;
	unsigned int		nbytes = 0,len,old_len,count =0;

	do {
		len = SDMMC_GET_FCNT(rk29_sdmmc_read(host->regs, SDMMC_STATUS)) << 2;
		if(count == 0) 
			old_len = len;
		if (likely(offset + len <= sg->length)) {
			rk29_sdmmc_pull_data(host, (void *)(buf + offset),len);
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
			rk29_sdmmc_pull_data(host, (void *)(buf + offset),remaining);
			nbytes += remaining;

			flush_dcache_page(sg_page(sg));
			host->sg = sg = sg_next(sg);
			if (!sg)
				goto done;
			offset = len - remaining;
			buf = sg_virt(sg);
			rk29_sdmmc_pull_data(host, buf,offset);
			nbytes += offset;
		}

		status = rk29_sdmmc_read(host->regs, SDMMC_MINTSTS);
		rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_RXDR); // clear RXDR interrupt
		if (status & RK29_SDMMC_DATA_ERROR_FLAGS) {
			host->data_status = status;
			if(data)
				data->bytes_xfered += nbytes;
			smp_wmb();
			rk29_sdmmc_set_pending(host, EVENT_DATA_ERROR);
			tasklet_schedule(&host->tasklet);
			return;
		}
		count ++;
	} while (status & SDMMC_INT_RXDR); // if the RXDR is ready let read again
	len = SDMMC_GET_FCNT(rk29_sdmmc_read(host->regs, SDMMC_STATUS));
	host->pio_offset = offset;
	if(data)
		data->bytes_xfered += nbytes;
	return;

done:
	if(data)
		data->bytes_xfered += nbytes;
	smp_wmb();
	rk29_sdmmc_set_pending(host, EVENT_XFER_COMPLETE);
}

static void rk29_sdmmc_write_data_pio(struct rk29_sdmmc *host)
{
	struct scatterlist	*sg = host->sg;
	void			*buf = sg_virt(sg);
	unsigned int		offset = host->pio_offset;
	struct mmc_data		*data = host->data;
	u32			status;
	unsigned int		nbytes = 0,len;

	do {

		len = SDMMC_FIFO_SZ - (SDMMC_GET_FCNT(rk29_sdmmc_read(host->regs, SDMMC_STATUS)) << 2);
		if (likely(offset + len <= sg->length)) {
			rk29_sdmmc_push_data(host, (void *)(buf + offset),len);

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
			rk29_sdmmc_push_data(host, (void *)(buf + offset), remaining);
			nbytes += remaining;

			host->sg = sg = sg_next(sg);
			if (!sg) {
				goto done;
			}

			offset = len - remaining;
			buf = sg_virt(sg);
			rk29_sdmmc_push_data(host, (void *)buf, offset);
			nbytes += offset;
		}

		status = rk29_sdmmc_read(host->regs, SDMMC_MINTSTS);
		rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_TXDR); // clear RXDR interrupt
		if (status & RK29_SDMMC_DATA_ERROR_FLAGS) {
			host->data_status = status;
			if(data)
				data->bytes_xfered += nbytes;
			smp_wmb();
			rk29_sdmmc_set_pending(host, EVENT_DATA_ERROR);
			tasklet_schedule(&host->tasklet);
			return;
		}
	} while (status & SDMMC_INT_TXDR); // if TXDR, let write again

	host->pio_offset = offset;
	if(data)
		data->bytes_xfered += nbytes;
	return;

done:
	if(data)
		data->bytes_xfered += nbytes;
	smp_wmb();
	rk29_sdmmc_set_pending(host, EVENT_XFER_COMPLETE);
}

static void rk29_sdmmc_cmd_interrupt(struct rk29_sdmmc *host, u32 status)
{
	if(!host->cmd_status) 
		host->cmd_status = status;

	smp_wmb();
	rk29_sdmmc_set_pending(host, EVENT_CMD_COMPLETE);
	tasklet_schedule(&host->tasklet);
}

#if 0
static int rk29_sdmmc1_card_get_cd(struct mmc_host *mmc)
{
        struct rk29_sdmmc *host = mmc_priv(mmc);
        struct rk29_sdmmc_platform_data *pdata = host->pdev->dev.platform_data;
        return gpio_get_value(pdata->detect_irq);
}

static int rk29_sdmmc1_card_change_cd_trigger_type(struct mmc_host *mmc, unsigned int type)
{
       struct rk29_sdmmc *host = mmc_priv(mmc);
       struct rk29_sdmmc_platform_data *pdata = host->pdev->dev.platform_data;
       return set_irq_type(gpio_to_irq(pdata->detect_irq), type);
}


static irqreturn_t rk29_sdmmc1_card_detect_interrupt(int irq, void *dev_id)
{
       struct rk29_sdmmc *host = dev_id;
       bool present, present_old; 
       
       present = rk29_sdmmc1_card_get_cd(host->mmc);
       present_old = test_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
       if (present != present_old) {
             if (present != 0) {
                  set_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
             } else {
                  clear_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
             }
             mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(200));
       }
       rk29_sdmmc1_card_change_cd_trigger_type(host->mmc, (present ? IRQF_TRIGGER_FALLING: IRQF_TRIGGER_RISING)); 
   
       return IRQ_HANDLED;

}
#endif

static irqreturn_t rk29_sdmmc_interrupt(int irq, void *dev_id)
{
	struct rk29_sdmmc	*host = dev_id;
	u32			status,  pending;
	unsigned int		pass_count = 0;
	bool present;
	bool present_old;

	spin_lock(&host->lock);
	do {
		status = rk29_sdmmc_read(host->regs, SDMMC_RINTSTS);
		pending = rk29_sdmmc_read(host->regs, SDMMC_MINTSTS);// read only mask reg
		if (!pending)
			break;	
		if(pending & SDMMC_INT_CD) {
		    rk29_sdmmc_write(host->regs, SDMMC_RINTSTS, SDMMC_INT_CD); // clear sd detect int
			present = rk29_sdmmc_get_cd(host->mmc);
			present_old = test_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
			if(present != present_old) {
				if (present != 0) {
					set_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
					mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(200));
				} else {					
					clear_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
					mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(10));					
				}							
			}
		}	
		if(pending & RK29_SDMMC_CMD_ERROR_FLAGS) {
		    rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,RK29_SDMMC_CMD_ERROR_FLAGS);  //  clear interrupt
		    host->cmd_status = status;
		    smp_wmb();
		    rk29_sdmmc_set_pending(host, EVENT_CMD_COMPLETE);
		    tasklet_schedule(&host->tasklet);
		}

		if (pending & RK29_SDMMC_DATA_ERROR_FLAGS) { // if there is an error, let report DATA_ERROR
			rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,RK29_SDMMC_DATA_ERROR_FLAGS);  // clear interrupt
			host->data_status = status;
			smp_wmb();
			rk29_sdmmc_set_pending(host, EVENT_DATA_ERROR);
			tasklet_schedule(&host->tasklet);
		}

		if(pending & SDMMC_INT_DTO) {
		    rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_DTO);  // clear interrupt
		    if (!host->data_status)
				host->data_status = status;
		    smp_wmb();
		    if(host->dir_status == RK29_SDMMC_RECV_STATUS) {
			if(host->sg != NULL) 
				rk29_sdmmc_read_data_pio(host);
		    }
		    rk29_sdmmc_set_pending(host, EVENT_DATA_COMPLETE);
		    tasklet_schedule(&host->tasklet);
		}

		if (pending & SDMMC_INT_RXDR) {
		    rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_RXDR);  //  clear interrupt
		    if(host->sg) 
			    rk29_sdmmc_read_data_pio(host);
		}

		if (pending & SDMMC_INT_TXDR) {
		    rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_TXDR);  //  clear interrupt
		    if(host->sg) 
				rk29_sdmmc_write_data_pio(host);
		    
		}

		if (pending & SDMMC_INT_CMD_DONE) {
		    rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_CMD_DONE);  //  clear interrupt
		    rk29_sdmmc_cmd_interrupt(host, status);
		}
		if(pending & SDMMC_INT_SDIO) {
				rk29_sdmmc_write(host->regs, SDMMC_RINTSTS,SDMMC_INT_SDIO);
				mmc_signal_sdio_irq(host->mmc);
		}
	} while (pass_count++ < 5);
	spin_unlock(&host->lock);
	return pass_count ? IRQ_HANDLED : IRQ_NONE;
}

/*
 *
 * MMC card detect thread, kicked off from detect interrupt, 1 timer 
 *
 */
static void rk29_sdmmc_detect_change(unsigned long data)
{
	struct mmc_request *mrq;
	struct rk29_sdmmc *host = (struct rk29_sdmmc *)data;
   
	smp_rmb();
	if (test_bit(RK29_SDMMC_SHUTDOWN, &host->flags))
		return;		
	spin_lock(&host->lock);	
	/* Clean up queue if present */
	mrq = host->mrq;
	if (mrq) {
		if (mrq == host->curr_mrq) {
		  	/* reset all blocks */
		  	rk29_sdmmc_write(host->regs, SDMMC_CTRL, (SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET));
		  	/* wait till resets clear */
			while (rk29_sdmmc_read(host->regs, SDMMC_CTRL) & (SDMMC_CTRL_RESET | SDMMC_CTRL_FIFO_RESET | SDMMC_CTRL_DMA_RESET));
			/* FIFO threshold settings  */
			rk29_sdmmc_write(host->regs, SDMMC_CTRL, rk29_sdmmc_read(host->regs, SDMMC_CTRL) | SDMMC_CTRL_INT_ENABLE);
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
				rk29_sdmmc_stop_dma(host);
				break;
			case STATE_DATA_BUSY:
			case STATE_DATA_ERROR:
				if (mrq->data->error == -EINPROGRESS)
					mrq->data->error = -ENOMEDIUM;
				if (!mrq->stop)
					break;
				/* fall through */
			case STATE_SENDING_STOP:
				mrq->stop->error = -ENOMEDIUM;
				break;
			}

				rk29_sdmmc_request_end(host, mrq);
			} else {
				list_del(&host->queue_node);
				mrq->cmd->error = -ENOMEDIUM;
				if (mrq->data)
					mrq->data->error = -ENOMEDIUM;
				if (mrq->stop)
					mrq->stop->error = -ENOMEDIUM;
				spin_unlock(&host->lock);
				mmc_request_done(host->mmc, mrq);
				spin_lock(&host->lock);
		}
		}	
	spin_unlock(&host->lock);	
	mmc_detect_change(host->mmc, 0);	
}

static void rk29_sdmmc1_check_status(unsigned long data)
{
        struct rk29_sdmmc *host = (struct rk29_sdmmc *)data;
        struct rk29_sdmmc_platform_data *pdata = host->pdev->dev.platform_data;
        unsigned int status;

        status = pdata->status(mmc_dev(host->mmc));

        if (status ^ host->oldstatus)
        {
                pr_info("%s: slot status change detected(%d-%d)\n",mmc_hostname(host->mmc), host->oldstatus, status);
                if (status) {
                    set_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
                    mod_timer(&host->detect_timer, jiffies + msecs_to_jiffies(200));
                }
                else {
                    clear_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
                    rk29_sdmmc_detect_change((unsigned long)host);
                }
        }

        host->oldstatus = status;
}

static void rk29_sdmmc1_status_notify_cb(int card_present, void *dev_id)
{
        struct rk29_sdmmc *host = dev_id;
        printk(KERN_INFO "%s, card_present %d\n", mmc_hostname(host->mmc), card_present);
        rk29_sdmmc1_check_status((unsigned long)host);
}

static int rk29_sdmmc_probe(struct platform_device *pdev)
{
	struct mmc_host 		*mmc;
	struct rk29_sdmmc		*host;
	struct resource			*regs;
	struct rk29_sdmmc_platform_data *pdata;
	int				irq;
	int				ret = 0;

	regs = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!regs)
		return -ENXIO;

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;
	mmc = mmc_alloc_host(sizeof(struct rk29_sdmmc), &pdev->dev);
	if (!mmc)
		return -ENOMEM;	
	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->pdev = pdev;
	pdata = pdev->dev.platform_data;
	if (!pdata) {
		dev_err(&pdev->dev, "Platform data missing\n");
		ret = -ENODEV;
		goto err_freehost;
	}
	if(pdata->io_init)
		pdata->io_init();
	spin_lock_init(&host->lock);
	INIT_LIST_HEAD(&host->queue);
	ret = -ENOMEM;
	host->regs = ioremap(regs->start, regs->end - regs->start);
	if (!host->regs)
	    goto err_freemap;   
	memcpy(host->dma_name, pdata->dma_name, 8);    
	host->use_dma = pdata->use_dma;     
	if(host->use_dma){
		if(strncmp(host->dma_name, "sdio", strlen("sdio")) == 0){
			rk29_dma_request(DMACH_SDIO, &rk29_dma_sdio1_client, NULL);
			host->dma_chn = DMACH_SDIO;
		}
		if(strncmp(host->dma_name, "sd_mmc", strlen("sd_mmc")) == 0){	
			rk29_dma_request(DMACH_SDMMC, &rk29_dma_sdmmc0_client, NULL);
			host->dma_chn = DMACH_SDMMC;
		}	
		rk29_dma_config(host->dma_chn, 16);
		rk29_dma_set_buffdone_fn(host->dma_chn, rk29_sdmmc_dma_complete);	
		host->dma_addr = regs->start + SDMMC_DATA;
	}		
	host->clk = clk_get(&pdev->dev, "mmc");
	clk_set_rate(host->clk,52000000);
	clk_enable(host->clk);
	clk_enable(clk_get(&pdev->dev, "hclk_mmc"));
	host->bus_hz = clk_get_rate(host->clk);  ///40000000;  ////cgu_get_clk_freq(CGU_SB_SD_MMC_CCLK_IN_ID); 

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
	tasklet_init(&host->tasklet, rk29_sdmmc_tasklet_func, (unsigned long)host);
	ret = request_irq(irq, rk29_sdmmc_interrupt, 0, dev_name(&pdev->dev), host);
	if (ret)
	    goto err_dmaunmap;
       
#if 0 
        /* register sdmmc1 card detect interrupt route */ 
        if ((pdev->id == 1) && (pdata->detect_irq != 0)) {
        	irq = gpio_to_irq(pdata->detect_irq);
        	if (irq < 0)  {
           		printk("%s: request gpio irq failed\n", __FUNCTION__);
           		goto err_dmaunmap;
        	}
        
        	ret = request_irq(irq, rk29_sdmmc1_card_detect_interrupt, IRQF_TRIGGER_RISING, dev_name(&pdev->dev), host);
        	if (ret) {
           		printk("%s: sdmmc1 request detect interrupt failed\n", __FUNCTION__);
           		goto err_dmaunmap;
        	}
         
    	}
#endif
        /* setup sdmmc1 wifi card detect change */
        if (pdata->register_status_notify) {
            pdata->register_status_notify(rk29_sdmmc1_status_notify_cb, host);
        }

        /* Assume card is present initially */
        if(rk29_sdmmc_get_cd(host->mmc))
                set_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
        else
                clear_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);

        /* sdmmc1 wifi card slot status initially */
        if (pdata->status) {
            host->oldstatus = pdata->status(mmc_dev(host->mmc));
            if (host->oldstatus)  {
                set_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
            }else {
                clear_bit(RK29_SDMMC_CARD_PRESENT, &host->flags);
            }
        }

	platform_set_drvdata(pdev, host); 	
	mmc->ops = &rk29_sdmmc_ops[pdev->id];
	mmc->f_min = host->bus_hz/510;
	mmc->f_max = host->bus_hz/2;  //2;  ///20; //max f is clock to mmc_clk/2
	mmc->ocr_avail = pdata->host_ocr_avail;
	mmc->caps = pdata->host_caps;
	mmc->max_phys_segs = 64;
	mmc->max_hw_segs = 64;
	mmc->max_blk_size = 4095;  //65536; /* SDMMC_BLKSIZ is 16 bits*/
	mmc->max_blk_count = 65535;  ///512;
	mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
	mmc->max_seg_size = mmc->max_req_size;	
        
	mmc_add_host(mmc);
#if defined (CONFIG_DEBUG_FS)
	rk29_sdmmc_init_debugfs(host);
#endif
	/* Create card detect handler thread  */
	setup_timer(&host->detect_timer, rk29_sdmmc_detect_change,(unsigned long)host);
	// enable interrupt for command done, data over, data empty, receive ready and error such as transmit, receive timeout, crc error
	rk29_sdmmc_write(host->regs, SDMMC_RINTSTS, 0xFFFFFFFF);
	if(host->use_dma)
		rk29_sdmmc_write(host->regs, SDMMC_INTMASK,SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | RK29_SDMMC_ERROR_FLAGS | SDMMC_INT_CD);
	else
		rk29_sdmmc_write(host->regs, SDMMC_INTMASK,SDMMC_INT_CMD_DONE | SDMMC_INT_DTO | SDMMC_INT_TXDR | SDMMC_INT_RXDR | RK29_SDMMC_ERROR_FLAGS | SDMMC_INT_CD);
	rk29_sdmmc_write(host->regs, SDMMC_CTRL,SDMMC_CTRL_INT_ENABLE); // enable mci interrupt
	rk29_sdmmc_write(host->regs, SDMMC_CLKENA,1);
	dev_info(&pdev->dev, "RK29 SDMMC controller at irq %d\n", irq);
	return 0;
err_dmaunmap:
	if(host->use_dma){
		if(strncmp(host->dma_name, "sdio", strlen("sdio")) == 0)
			rk29_dma_free(DMACH_SDIO, &rk29_dma_sdio1_client);
		if(strncmp(host->dma_name, "sd_mmc", strlen("sd_mmc")) == 0)	
			rk29_dma_free(DMACH_SDMMC, &rk29_dma_sdmmc0_client);
	}
err_freemap:
	iounmap(host->regs);
err_freehost:
	kfree(host);
	return ret;
}



static int __exit rk29_sdmmc_remove(struct platform_device *pdev)
{
	struct rk29_sdmmc *host = platform_get_drvdata(pdev);

	rk29_sdmmc_write(host->regs, SDMMC_RINTSTS, 0xFFFFFFFF);
	rk29_sdmmc_write(host->regs, SDMMC_INTMASK, 0); // disable all mmc interrupt first
	
	/* Shutdown detect IRQ and kill detect thread */
	del_timer_sync(&host->detect_timer);

	/* Debugfs stuff is cleaned up by mmc core */
	set_bit(RK29_SDMMC_SHUTDOWN, &host->flags);
	smp_wmb();
	mmc_remove_host(host->mmc);
	mmc_free_host(host->mmc);

	/* disable clock to CIU */
	rk29_sdmmc_write(host->regs, SDMMC_CLKENA,0);
	rk29_sdmmc_write(host->regs, SDMMC_CLKSRC,0);
	
	free_irq(platform_get_irq(pdev, 0), host);
	if(host->use_dma){
		if(strncmp(host->dma_name, "sdio", strlen("sdio")) == 0)
			rk29_dma_free(DMACH_SDIO, &rk29_dma_sdio1_client);
		if(strncmp(host->dma_name, "sd_mmc", strlen("sd_mmc")) == 0)	
			rk29_dma_free(DMACH_SDMMC, &rk29_dma_sdmmc0_client);
	}
	iounmap(host->regs);

	kfree(host);
	return 0;
}

static int rk29_sdmmc_suspend(struct platform_device *pdev, pm_message_t state)
{
#ifdef CONFIG_PM
	struct rk29_sdmmc *host = platform_get_drvdata(pdev);
	rk29_sdmmc_write(host->regs, SDMMC_CLKENA, 0);
	clk_disable(host->clk);
#endif
	return 0;
}

static int rk29_sdmmc_resume(struct platform_device *pdev)
{
#ifdef CONFIG_PM
	struct rk29_sdmmc *host = platform_get_drvdata(pdev);
	clk_enable(host->clk);
#endif
	return 0;
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
 
