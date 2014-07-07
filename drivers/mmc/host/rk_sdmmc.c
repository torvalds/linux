/*
 * Synopsys DesignWare Multimedia Card Interface driver
 *  (Based on NXP driver for lpc 31xx)
 *
 * Copyright (C) 2009 NXP Semiconductors
 * Copyright (C) 2009, 2010 Imagination Technologies Ltd.
 *
 * Copyright (C) 2014 Fuzhou Rockchip Electronics Co.Ltd.
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
#include <linux/dmaengine.h>
#include <linux/err.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/ioport.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/stat.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>
#include <linux/mmc/sd.h>
#include <linux/mmc/sdio.h>
#include <linux/mmc/rk_mmc.h>
#include <linux/bitops.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>
#include <linux/of.h>
#include <linux/of_gpio.h>
#include <linux/mmc/slot-gpio.h>
#include <linux/clk-private.h>
#include <linux/rockchip/cpu.h>

#include "rk_sdmmc.h"
#include "rk_sdmmc_dbg.h"
#include <linux/regulator/rockchip_io_vol_domain.h>
#include "../../clk/rockchip/clk-ops.h"

#define grf_writel(v, offset)   do { writel_relaxed(v, RK_GRF_VIRT + offset); dsb(); } while (0)

#define RK_SDMMC_DRIVER_VERSION "Ver 1.11 2014-06-05" 

/* Common flag combinations */
#define DW_MCI_DATA_ERROR_FLAGS	(SDMMC_INT_DRTO | SDMMC_INT_DCRC | \
				 /*SDMMC_INT_HTO | */SDMMC_INT_SBE  | \
				 SDMMC_INT_EBE)
#define DW_MCI_CMD_ERROR_FLAGS	(SDMMC_INT_RTO | SDMMC_INT_RCRC | \
				 SDMMC_INT_RESP_ERR)
#define DW_MCI_ERROR_FLAGS	(DW_MCI_DATA_ERROR_FLAGS | \
				 DW_MCI_CMD_ERROR_FLAGS  | SDMMC_INT_HLE)
#define DW_MCI_SEND_STATUS	1
#define DW_MCI_RECV_STATUS	2
#define DW_MCI_DMA_THRESHOLD	16

#define DW_MCI_FREQ_MAX	50000000//200000000	/* unit: HZ */
#define DW_MCI_FREQ_MIN	300000//400000		/* unit: HZ */

/*max is 250ms showed in Spec; Maybe adapt the value for the sick card.*/
#define SDMMC_DATA_TIMEOUT_SD	500
#define SDMMC_DATA_TIMEOUT_SDIO	250
#define SDMMC_DATA_TIMEOUT_EMMC	2500

#define SDMMC_CMD_RTO_MAX_HOLD  200
#define SDMMC_WAIT_FOR_UNBUSY	2500

#ifdef CONFIG_MMC_DW_IDMAC
#define IDMAC_INT_CLR		(SDMMC_IDMAC_INT_AI | SDMMC_IDMAC_INT_NI | \
				 SDMMC_IDMAC_INT_CES | SDMMC_IDMAC_INT_DU | \
				 SDMMC_IDMAC_INT_FBE | SDMMC_IDMAC_INT_RI | \
				 SDMMC_IDMAC_INT_TI)

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

static inline bool dw_mci_fifo_reset(struct dw_mci *host);
static inline bool dw_mci_ctrl_all_reset(struct dw_mci *host);
static bool dw_mci_ctrl_reset(struct dw_mci *host, u32 reset);
static void dw_mci_disable_low_power(struct dw_mci_slot *slot);

/*printk the all register of current host*/
#if 0
static int dw_mci_regs_printk(struct dw_mci *host)
{
    struct sdmmc_reg *regs = dw_mci_regs;

    while( regs->name != 0 ){
        printk("%s: (0x%04x) = 0x%08x\n", regs->name, regs->addr, mci_readreg(host,regs->addr));
        regs++;
    }
    printk("=======printk %s-register end =========\n", mmc_hostname(host->mmc));
    return 0;
}
#endif

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

static void dw_mci_set_timeout(struct dw_mci *host)
{
	/* timeout (maximum) */
	mci_writel(host, TMOUT, 0xffffffff);
}

static u32 dw_mci_prepare_command(struct mmc_host *mmc, struct mmc_command *cmd)
{
	struct mmc_data	*data;
	struct dw_mci_slot *slot = mmc_priv(mmc);
	const struct dw_mci_drv_data *drv_data = slot->host->drv_data;
	u32 cmdr;
	cmd->error = -EINPROGRESS;

	cmdr = cmd->opcode;

	if (cmdr == MMC_STOP_TRANSMISSION)
		cmdr |= SDMMC_CMD_STOP;
	else
		cmdr |= SDMMC_CMD_PRV_DAT_WAIT;

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

	if (drv_data && drv_data->prepare_command)
		drv_data->prepare_command(slot->host, &cmdr);

	return cmdr;
}

#if 0
static u32 dw_mci_prep_stop_abort(struct dw_mci *host, struct mmc_command *cmd)
{
	struct mmc_command *stop;
	u32 cmdr;

	if (!cmd->data)
		return 0;

	stop = &host->stop_abort;
	cmdr = cmd->opcode;
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
		stop->arg |= (1 << 31) | (0 << 28) | (SDIO_CCCR_ABORT << 9) |
			     ((cmd->arg >> 28) & 0x7);
		stop->flags = MMC_RSP_SPI_R5 | MMC_RSP_R5 | MMC_CMD_AC;
	} else {
		return 0;
	}

	cmdr = stop->opcode | SDMMC_CMD_STOP |
		SDMMC_CMD_RESP_CRC | SDMMC_CMD_RESP_EXP;

	return cmdr;
}
#endif
static void dw_mci_start_command(struct dw_mci *host,
				 struct mmc_command *cmd, u32 cmd_flags)
{
	struct dw_mci_slot *slot = host->slot[0];
	/*temporality fix slot[0] due to host->num_slots equal to 1*/

	host->pre_cmd = host->cmd;
	host->cmd = cmd;
	dev_vdbg(host->dev,
		"start command: ARGR=0x%08x CMDR=0x%08x\n",
		cmd->arg, cmd_flags);

	if(SD_SWITCH_VOLTAGE == cmd->opcode){
		/*confirm non-low-power mode*/
		mci_writel(host, CMDARG, 0);
		dw_mci_disable_low_power(slot);
		
		MMC_DBG_INFO_FUNC(host->mmc,"Line%d..%s before start cmd=11,[%s]",
			__LINE__, __FUNCTION__,mmc_hostname(host->mmc));

		cmd_flags |= SDMMC_CMD_VOLT_SWITCH;
	}

	mci_writel(host, CMDARG, cmd->arg);
	wmb();
	
	/* fix the value to 1 in some Soc,for example RK3188. */
	if(host->mmc->hold_reg_flag)
		cmd_flags |= SDMMC_CMD_USE_HOLD_REG;

	mci_writel(host, CMD, cmd_flags | SDMMC_CMD_START);
	wmb();
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
	}

	/* Data transfer was stopped by the interrupt handler */
	set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
}

static int dw_mci_get_dma_dir(struct mmc_data *data)
{
	if (data->flags & MMC_DATA_WRITE)
		return DMA_TO_DEVICE;
	else
		return DMA_FROM_DEVICE;
}

#ifdef CONFIG_MMC_DW_IDMAC
static void dw_mci_dma_cleanup(struct dw_mci *host)
{
	struct mmc_data *data = host->data;

	if (data)
		if (!data->host_cookie)
			dma_unmap_sg(host->dev,
				     data->sg,
				     data->sg_len,
				     dw_mci_get_dma_dir(data));
}

static void dw_mci_idmac_reset(struct dw_mci *host)
{
	u32 bmod = mci_readl(host, BMOD);
	/* Software reset of DMA */
	bmod |= SDMMC_IDMAC_SWRESET;
	mci_writel(host, BMOD, bmod);
}

static void dw_mci_idmac_stop_dma(struct dw_mci *host)
{
	u32 temp;

	/* Disable and reset the IDMAC interface */
	temp = mci_readl(host, CTRL);
	temp &= ~SDMMC_CTRL_USE_IDMAC;
	temp |= SDMMC_CTRL_DMA_RESET;
	mci_writel(host, CTRL, temp);

	/* Stop the IDMAC running */
	temp = mci_readl(host, BMOD);
	temp &= ~(SDMMC_IDMAC_ENABLE | SDMMC_IDMAC_FB);
	temp |= SDMMC_IDMAC_SWRESET;
	mci_writel(host, BMOD, temp);
}

static void dw_mci_idmac_complete_dma(void *arg)
{
        struct dw_mci *host = arg;
        struct mmc_data *data = host->data;

        dev_vdbg(host->dev, "DMA complete\n");

        /*
        MMC_DBG_CMD_FUNC(host->mmc," DMA complete cmd=%d(arg=0x%x), blocks=%d,blksz=%d[%s]", \
                host->mrq->cmd->opcode,host->mrq->cmd->arg,
                data->blocks,data->blksz,mmc_hostname(host->mmc));
        */

        host->dma_ops->cleanup(host);

	/*
	 * If the card was removed, data will be NULL. No point in trying to
	 * send the stop command or waiting for NBUSY in this case.
	 */
        if(data){
                set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
                tasklet_schedule(&host->tasklet);
        }
}

static void dw_mci_translate_sglist(struct dw_mci *host, struct mmc_data *data,
				    unsigned int sg_len)
{
	int i;
	struct idmac_desc *desc = host->sg_cpu;

	for (i = 0; i < sg_len; i++, desc++) {
		unsigned int length = sg_dma_len(&data->sg[i]);
		u32 mem_addr = sg_dma_address(&data->sg[i]);

		/* Set the OWN bit and disable interrupts for this descriptor */
		desc->des0 = IDMAC_DES0_OWN | IDMAC_DES0_DIC | IDMAC_DES0_CH;

		/* Buffer length */
		IDMAC_SET_BUFFER1_SIZE(desc, length);

		/* Physical address to DMA to/from */
		desc->des2 = mem_addr;
	}

	/* Set first descriptor */
	desc = host->sg_cpu;
	desc->des0 |= IDMAC_DES0_FD;

	/* Set last descriptor */
	desc = host->sg_cpu + (i - 1) * sizeof(struct idmac_desc);
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
	host->ring_size = PAGE_SIZE / sizeof(struct idmac_desc);

	/* Forward link the descriptor list */
	for (i = 0, p = host->sg_cpu; i < host->ring_size - 1; i++, p++)
		p->des3 = host->sg_dma + (sizeof(struct idmac_desc) * (i + 1));

	/* Set the last descriptor as the end-of-ring descriptor */
	p->des3 = host->sg_dma;
	p->des0 = IDMAC_DES0_ER;

	dw_mci_idmac_reset(host);

	/* Mask out interrupts - get Tx & Rx complete only */
	mci_writel(host, IDSTS, IDMAC_INT_CLR);
	mci_writel(host, IDINTEN, SDMMC_IDMAC_INT_NI | SDMMC_IDMAC_INT_RI |
		   SDMMC_IDMAC_INT_TI);

	/* Set the descriptor base address */
	mci_writel(host, DBADDR, host->sg_dma);
	return 0;
}

static const struct dw_mci_dma_ops dw_mci_idmac_ops = {
	.init = dw_mci_idmac_init,
	.start = dw_mci_idmac_start_dma,
	.stop = dw_mci_idmac_stop_dma,
	.complete = dw_mci_idmac_complete_dma,
	.cleanup = dw_mci_dma_cleanup,
};
#endif /* CONFIG_MMC_DW_IDMAC */

#ifdef CONFIG_MMC_DW_EDMAC
static void dw_mci_edma_cleanup(struct dw_mci *host)
{
	struct mmc_data *data = host->data;

	if (data)
                if (!data->host_cookie)
			dma_unmap_sg(host->dev,
                                        data->sg, data->sg_len,
                                        dw_mci_get_dma_dir(data));
}

static void dw_mci_edmac_stop_dma(struct dw_mci *host)
{
        dmaengine_terminate_all(host->dms->ch);
}

static void dw_mci_edmac_complete_dma(void *arg)
{
        struct dw_mci *host = arg;
        struct mmc_data *data = host->data;

        dev_vdbg(host->dev, "DMA complete\n");

        if(data)
                if(data->flags & MMC_DATA_READ)
                        /* Invalidate cache after read */
                        dma_sync_sg_for_cpu(mmc_dev(host->mmc), data->sg,
                                data->sg_len, DMA_FROM_DEVICE);

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

static void dw_mci_edmac_start_dma(struct dw_mci *host, unsigned int sg_len)
{
        struct dma_slave_config slave_config;
        struct dma_async_tx_descriptor *desc = NULL;
        struct scatterlist *sgl = host->data->sg;
        u32 sg_elems = host->data->sg_len;
        int ret = 0;

        /* Set external dma config: burst size, burst width*/
        slave_config.dst_addr = (dma_addr_t)(host->phy_regs + host->data_offset);
        slave_config.src_addr = slave_config.dst_addr;
        slave_config.dst_addr_width = DMA_SLAVE_BUSWIDTH_4_BYTES;
        slave_config.src_addr_width = slave_config.dst_addr_width;

        /* Match FIFO dma burst MSIZE with external dma config*/
        slave_config.dst_maxburst = ((host->fifoth_val) >> 28) && 0x7;
        slave_config.src_maxburst = slave_config.dst_maxburst;

        if(host->data->flags & MMC_DATA_WRITE){
                slave_config.direction = DMA_MEM_TO_DEV;
                ret = dmaengine_slave_config(host->dms->ch, &slave_config);
                if(ret){
                        dev_err(host->dev, "error in dw_mci edma configuration.\n");
                        return;
                }

                desc = dmaengine_prep_slave_sg(host->dms->ch, sgl, sg_len,
                                DMA_MEM_TO_DEV, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
                if(!desc){
                        dev_err(host->dev, "We cannot prepare for the dw_mci slave edma!\n");
                        return;
                }
                /* Set dw_mci_edmac_complete_dma as callback */
                desc->callback = dw_mci_edmac_complete_dma;
                desc->callback_param = (void *)host;
                dmaengine_submit(desc);

                /* Flush cache before write */
                dma_sync_sg_for_device(mmc_dev(host->mmc), sgl,
                                        sg_elems, DMA_TO_DEVICE);
                dma_async_issue_pending(host->dms->ch);
        }else{
                /* MMC_DATA_READ*/
                slave_config.direction = DMA_DEV_TO_MEM;
                ret = dmaengine_slave_config(host->dms->ch, &slave_config);
                if(ret){
                        dev_err(host->dev, "error in dw_mci edma configuration.\n");
                        return;
                }
                desc = dmaengine_prep_slave_sg(host->dms->ch, sgl, sg_len,
                                DMA_DEV_TO_MEM, DMA_PREP_INTERRUPT | DMA_CTRL_ACK);
                if(!desc){
                        dev_err(host->dev, "We cannot prepare for the dw_mci slave edma!\n");
                        return;
                }
                /* set dw_mci_edmac_complete_dma as callback */
                desc->callback = dw_mci_edmac_complete_dma;
                desc->callback_param = (void *)host;
                dmaengine_submit(desc);
                dma_async_issue_pending(host->dms->ch);
        }
}

static int dw_mci_edmac_init(struct dw_mci *host)
{
        MMC_DBG_BOOT_FUNC(host->mmc,"dw_mci_edmac_init: Soc is 0x%x [%s]\n",
                                (unsigned int)(rockchip_soc_id & ROCKCHIP_CPU_MASK), mmc_hostname(host->mmc));

        /* 1) request external dma channel, SHOULD decide chn in dts */
        host->dms = (struct dw_mci_dma_slave *)kmalloc(sizeof(struct dw_mci_dma_slave),GFP_KERNEL);
        host->dms->ch = dma_request_slave_channel(host->dev, "dw_mci");
        if (!host->dms->ch){
                dev_err(host->dev, "Failed to get external DMA channel: channel id = %d\n",
                                host->dms->ch->chan_id);
                goto err_exit;
        }

        /* anything? */

        return 0;

err_exit:
        return -ENODEV;

}

static void dw_mci_edmac_exit(struct dw_mci *host)
{
        dma_release_channel(host->dms->ch);
}

static const struct dw_mci_dma_ops dw_mci_edmac_ops = {
        .init = dw_mci_edmac_init,
        .exit = dw_mci_edmac_exit,
        .start = dw_mci_edmac_start_dma,
        .stop = dw_mci_edmac_stop_dma,
        .complete = dw_mci_edmac_complete_dma,
        .cleanup = dw_mci_edma_cleanup,
};
#endif
static int dw_mci_pre_dma_transfer(struct dw_mci *host,
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
	if (data->blocks * data->blksz < DW_MCI_DMA_THRESHOLD)
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
			    dw_mci_get_dma_dir(data));
	if (sg_len == 0)
		return -EINVAL;

	if (next)
		data->host_cookie = sg_len;

	return sg_len;
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
		dma_unmap_sg(slot->host->dev,
			     data->sg,
			     data->sg_len,
			     dw_mci_get_dma_dir(data));
	data->host_cookie = 0;
}

static void dw_mci_adjust_fifoth(struct dw_mci *host, struct mmc_data *data)
{
#if defined(CONFIG_MMC_DW_IDMAC) || defined(CONFIG_MMC_DW_EDMAC)
	unsigned int blksz = data->blksz;
	const u32 mszs[] = {1, 4, 8, 16, 32, 64, 128, 256};
	u32 fifo_width = 1 << host->data_shift;
	u32 blksz_depth = blksz / fifo_width, fifoth_val;
	u32 msize = 0, rx_wmark = 1, tx_wmark, tx_wmark_invers;
	int idx = (sizeof(mszs) / sizeof(mszs[0])) - 1;

	tx_wmark = (host->fifo_depth) / 2;
	tx_wmark_invers = host->fifo_depth - tx_wmark;

	/*
	 * MSIZE is '1',
	 * if blksz is not a multiple of the FIFO width
	 */
	if (blksz % fifo_width) {
		msize = 0;
		rx_wmark = 1;
		goto done;
	}

	do {
		if (!((blksz_depth % mszs[idx]) ||
		     (tx_wmark_invers % mszs[idx]))) {
			msize = idx;
			rx_wmark = mszs[idx] - 1;
			break;
		}
	} while (--idx > 0);
	/*
	 * If idx is '0', it won't be tried
	 * Thus, initial values are uesed
	 */
done:
	fifoth_val = SDMMC_SET_FIFOTH(msize, rx_wmark, tx_wmark);
	mci_writel(host, FIFOTH, fifoth_val);
#endif
}

static void dw_mci_ctrl_rd_thld(struct dw_mci *host, struct mmc_data *data)
{
	unsigned int blksz = data->blksz;
	u32 blksz_depth, fifo_depth;
	u16 thld_size;

	WARN_ON(!(data->flags & MMC_DATA_READ));

	if (host->timing != MMC_TIMING_MMC_HS200 &&
	    host->timing != MMC_TIMING_UHS_SDR104)
		goto disable;

	blksz_depth = blksz / (1 << host->data_shift);
	fifo_depth = host->fifo_depth;

	if (blksz_depth > fifo_depth)
		goto disable;

	/*
	 * If (blksz_depth) >= (fifo_depth >> 1), should be 'thld_size <= blksz'
	 * If (blksz_depth) <  (fifo_depth >> 1), should be thld_size = blksz
	 * Currently just choose blksz.
	 */
	thld_size = blksz;
	mci_writel(host, CDTHRCTL, SDMMC_SET_RD_THLD(thld_size, 1));
	return;

disable:
	mci_writel(host, CDTHRCTL, SDMMC_SET_RD_THLD(0, 0));
}

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

	dev_vdbg(host->dev,
		 "sd sg_cpu: %#lx sg_dma: %#lx sg_len: %d\n",
		 (unsigned long)host->sg_cpu, (unsigned long)host->sg_dma,
		 sg_len);

	/*
	 * Decide the MSIZE and RX/TX Watermark.
	 * If current block size is same with previous size,
	 * no need to update fifoth.
	 */
	if (host->prev_blksz != data->blksz)
		dw_mci_adjust_fifoth(host, data);

    	/* Reset DMA FIFO*/
	dw_mci_ctrl_reset(host, SDMMC_CTRL_DMA_RESET);// | SDMMC_CTRL_FIFO_RESET); //dange to fifo-reset; noted by xbw,at 2014-03-28

	/* Enable the DMA interface */
	temp = mci_readl(host, CTRL);
	temp |= SDMMC_CTRL_DMA_ENABLE;
	mci_writel(host, CTRL, temp);

	/* Disable RX/TX IRQs, let DMA handle it */
	temp = mci_readl(host, INTMASK);
	temp  &= ~(SDMMC_INT_RXDR | SDMMC_INT_TXDR);
	mci_writel(host, INTMASK, temp);

	host->dma_ops->start(host, sg_len);

	return 0;
}

static void dw_mci_submit_data(struct dw_mci *host, struct mmc_data *data)
{
	u32 temp;

	data->error = -EINPROGRESS;

	WARN_ON(host->data);
	host->sg = NULL;
	host->data = data;

	/* Reset FIFO*/
	dw_mci_ctrl_reset(host, SDMMC_CTRL_DMA_RESET);// | SDMMC_CTRL_FIFO_RESET);//dange to fifo-reset; noted by xbw,at 2014-03-28

	if (data->flags & MMC_DATA_READ) {
		host->dir_status = DW_MCI_RECV_STATUS;
		dw_mci_ctrl_rd_thld(host, data);
	} else {
		host->dir_status = DW_MCI_SEND_STATUS;
	}
	
    MMC_DBG_INFO_FUNC(host->mmc," dw_mci_submit_data,blocks=%d,blksz=%d [%s]",\
         data->blocks, data->blksz, mmc_hostname(host->mmc));

	if (dw_mci_submit_data_dma(host, data)) {
		int flags = SG_MITER_ATOMIC;
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

		/*
		 * Use the initial fifoth_val for PIO mode.
		 * If next issued data may be transfered by DMA mode,
		 * prev_blksz should be invalidated.
		 */
		mci_writel(host, FIFOTH, host->fifoth_val);
		host->prev_blksz = 0;
	} else {
		/*
		 * Keep the current block size.
		 * It will be used to decide whether to update
		 * fifoth register next time.
		 */
		host->prev_blksz = data->blksz;
	}
}

static void mci_send_cmd(struct dw_mci_slot *slot, u32 cmd, u32 arg)
{
	struct dw_mci *host = slot->host;	
	unsigned long timeout = jiffies + msecs_to_jiffies(500);//msecs_to_jiffies(5000);
	unsigned int cmd_status = 0;
#ifdef SDMMC_WAIT_FOR_UNBUSY
	bool ret = true;
	timeout = jiffies + msecs_to_jiffies(SDMMC_WAIT_FOR_UNBUSY);
	
    if(test_bit(DW_MMC_CARD_PRESENT, &slot->flags)) {
        while (ret) {
		ret =  time_before(jiffies, timeout);
        	cmd_status = mci_readl(host, STATUS);
        	if (!(cmd_status & (SDMMC_STAUTS_DATA_BUSY|SDMMC_STAUTS_MC_BUSY)))
        		break;
        } ;
        if(false == ret)
            printk("%d..%s:  wait for unbusy timeout.......[%s]\n", \
                __LINE__, __FUNCTION__, mmc_hostname(host->mmc));
    }
#endif    
	mci_writel(host, CMDARG, arg);
	wmb();
	mci_writel(host, CMD, SDMMC_CMD_START | cmd);
	if(cmd & SDMMC_CMD_UPD_CLK)
		timeout = jiffies + msecs_to_jiffies(50);
	else
		timeout = jiffies + msecs_to_jiffies(500);
	while (time_before(jiffies, timeout)) {
		cmd_status = mci_readl(host, CMD);
		if (!(cmd_status & SDMMC_CMD_START))
			return ;
	}
	dev_err(&slot->mmc->class_dev,
		"Timeout sending command (cmd %#x arg %#x status %#x)\n",
		cmd, arg, cmd_status);
}

static void dw_mci_setup_bus(struct dw_mci_slot *slot, bool force_clkinit)
{
	struct dw_mci *host = slot->host;
	unsigned int tempck,clock = slot->clock;
	u32 div;
	u32 clk_en_a;
	u32 sdio_int;

	MMC_DBG_INFO_FUNC(host->mmc,"%d..%s: clock=%d, current_speed=%d, bus_hz=%d,forc=%d[%s]\n", 
                __LINE__, __FUNCTION__, clock, host->current_speed,host->bus_hz,force_clkinit,mmc_hostname(host->mmc));

	if (!clock) {
		mci_writel(host, CLKENA, 0);
		#ifdef CONFIG_MMC_DW_ROCKCHIP_SWITCH_VOLTAGE
                if(host->svi_flags == 0)
                        mci_send_cmd(slot, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);
                #else
                mci_send_cmd(slot, SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);
                #endif
	} else if (clock != host->current_speed || force_clkinit) {
		div = host->bus_hz / clock;
		if (host->bus_hz % clock && host->bus_hz > clock)
			/*
			 * move the + 1 after the divide to prevent
			 * over-clocking the card.
			 */
			div += 1;

		div = (host->bus_hz != clock) ? DIV_ROUND_UP(div, 2) : 0;

		if ((clock << div) != slot->__clk_old || force_clkinit) {
		    tempck = div ? ((host->bus_hz / div) >> 1) :host->bus_hz;
			dev_info(&slot->mmc->class_dev,
				 "Bus speed (slot %d) = %dHz (slot req %dHz, actual %dHZ div = %d)\n",
				 slot->id, host->bus_hz, clock,
				 tempck, div);

			host->set_speed = tempck;
		        host->set_div = div;
		}

		/* disable clock */
		mci_writel(host, CLKENA, 0);
		mci_writel(host, CLKSRC, 0);

		/* inform CIU */
		mci_send_cmd(slot,
			     SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);
                        
                if(clock <= 400*1000){
	                MMC_DBG_BOOT_FUNC(host->mmc,
                                "dw_mci_setup_bus: argue clk_mmc workaround out %dHz for init[%s]",
                                clock * 2, mmc_hostname(host->mmc)); 
                        /* clk_mmc will change parents to 24MHz xtal*/
	                clk_set_rate(host->clk_mmc, clock * 2);	               

	                div = 0;
	                host->set_div = div;
	        }
	        else
	        {
	                MMC_DBG_BOOT_FUNC(host->mmc,
                                "dw_mci_setup_bus: argue clk_mmc workaround out normal clock [%s]",
                                mmc_hostname(host->mmc)); 
                        if(div > 1)
                        {
                                MMC_DBG_ERR_FUNC(host->mmc,
                                        "dw_mci_setup_bus: div SHOULD NOT LARGER THAN ONE! [%s]",
                                        mmc_hostname(host->mmc)); 
                                 div = 1;
                                 host->set_div = div;
                                 host->bus_hz = host->set_speed * 2;
                                 MMC_DBG_BOOT_FUNC(host->mmc,
                                        "dw_mci_setup_bus: workaround div = %d, host->bus_hz = %d [%s]",
                                        div, host->bus_hz, mmc_hostname(host->mmc));                                 
                        }
                        /* BUG may be here, come on,  Linux BSP engineer looks!
                           FIXME:  HS-DDR eMMC, div SHOULD be ONE, but we here cannot fetch eMMC bus mode!!!!!!!! 
                           WRONG dts set clk = 50M, and calc div be zero. Controller denied this setting!
                           some oops happened like that:
                           mmc_host mmc0: Bus speed (slot 0) = 50000000Hz (slot req 50000000Hz, actual 50000000HZ div = 0)
                           rk_sdmmc: BOOT dw_mci_setup_bus: argue clk_mmc workaround out normal clock [mmc0]
                           rk_sdmmc: BOOT Bus speed=50000000Hz,Bus width=8bits.[mmc0]
                           mmc0: new high speed DDR MMC card at address 0001
                           mmcblk0: mmc0:0001 M8G1GC 7.28 GiB 
                           ....
                           mmcblk0: error -84 transferring data, sector 606208, nr 32, cmd response 0x900, card status 0xb00
                           mmcblk0: retrying using single block read
                           mmcblk0: error -110 sending status command, retrying

                           How to: If eMMC HW version < 4.51, or > 4.51 but no caps2-mmc-hs200 support in dts
                                   Please set dts emmc clk to 100M or 150M, I will workaround it!
                         */

	                if (host->verid < DW_MMC_240A)
	                        clk_set_rate(host->clk_mmc,(host->bus_hz));
	                else
	                        clk_set_rate(host->clk_mmc,(host->bus_hz) * 2);


	                        
	        }
	                       
		/* set clock to desired speed */
		mci_writel(host, CLKDIV, div);

		/* inform CIU */
		mci_send_cmd(slot,
			     SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);

		/* enable clock; only low power if no SDIO */
		clk_en_a = SDMMC_CLKEN_ENABLE << slot->id;

		if (host->verid < DW_MMC_240A)
		    sdio_int = SDMMC_INT_SDIO(slot->id);
	        else
		    sdio_int = SDMMC_INT_SDIO((slot->id) + 8);

		if (!(mci_readl(host, INTMASK) & sdio_int))
			clk_en_a |= SDMMC_CLKEN_LOW_PWR << slot->id;
		mci_writel(host, CLKENA, clk_en_a);

		/* inform CIU */
		mci_send_cmd(slot,
			     SDMMC_CMD_UPD_CLK | SDMMC_CMD_PRV_DAT_WAIT, 0);
		/* keep the clock with reflecting clock dividor */
		slot->__clk_old = clock << div;
	}

	host->current_speed = clock;

    if(slot->ctype != slot->pre_ctype)
        MMC_DBG_BOOT_FUNC(host->mmc, "Bus speed=%dHz,Bus width=%s.[%s]", 
            host->set_speed,
            (slot->ctype == SDMMC_CTYPE_4BIT)?"4bits":"8bits", 
            mmc_hostname(host->mmc));
    slot->pre_ctype = slot->ctype;

	/* Set the current slot bus width */
	mci_writel(host, CTYPE, (slot->ctype << slot->id));
}

static void dw_mci_wait_unbusy(struct dw_mci *host)
{
   
    unsigned int    timeout= SDMMC_DATA_TIMEOUT_SDIO;
    unsigned long   time_loop;
    unsigned int    status;

    MMC_DBG_INFO_FUNC(host->mmc, "dw_mci_wait_unbusy, status=0x%x ", mci_readl(host, STATUS));
    
    if(host->mmc->restrict_caps & RESTRICT_CARD_TYPE_EMMC)
        timeout = SDMMC_DATA_TIMEOUT_EMMC;
    else if(host->mmc->restrict_caps & RESTRICT_CARD_TYPE_SD)
        timeout = SDMMC_DATA_TIMEOUT_SD;
        
    time_loop = jiffies + msecs_to_jiffies(timeout);
    do {
    	status = mci_readl(host, STATUS);
    	if (!(status & (SDMMC_STAUTS_DATA_BUSY | SDMMC_STAUTS_MC_BUSY)))
    		break;
    } while (time_before(jiffies, time_loop));
}


#ifdef CONFIG_MMC_DW_ROCKCHIP_SWITCH_VOLTAGE
/*
*   result: 
*   0--status is busy. 
*   1--status is unbusy.
*/
int dw_mci_card_busy(struct mmc_host *mmc)
{
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci *host = slot->host;

        MMC_DBG_INFO_FUNC(host->mmc, "dw_mci_card_busy: svi_flags = %d [%s]", \
                                host->svi_flags, mmc_hostname(host->mmc));	
    
        /* svi toggle*/
        if(host->svi_flags == 0){
                /*first svi*/
                host->svi_flags = 1;
                return host->svi_flags;           
    
        }else{
                host->svi_flags = 0;
                return host->svi_flags;   
    	}
    	

}
#endif
static void __dw_mci_start_request(struct dw_mci *host,
				   struct dw_mci_slot *slot,
				   struct mmc_command *cmd)
{
	struct mmc_request *mrq;
	struct mmc_data	*data;
	u32 cmdflags;

	mrq = slot->mrq;
	if (host->pdata->select_slot)
		host->pdata->select_slot(slot->id);

	host->cur_slot = slot;
	host->mrq = mrq;

        dw_mci_wait_unbusy(host);
    
	host->pending_events = 0;
	host->completed_events = 0;
	host->data_status = 0;

	data = cmd->data;
	if (data) {
		dw_mci_set_timeout(host);
		mci_writel(host, BYTCNT, data->blksz*data->blocks);
		mci_writel(host, BLKSIZ, data->blksz);
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
}

static void dw_mci_start_request(struct dw_mci *host,
				 struct dw_mci_slot *slot)
{
	struct mmc_request *mrq = slot->mrq;
	struct mmc_command *cmd;

	MMC_DBG_INFO_FUNC(host->mmc, " Begin to start the new request. cmd=%d(arg=0x%x)[%s]", \
                mrq->cmd->opcode, mrq->cmd->arg, mmc_hostname(host->mmc));
        
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

	/*
	 * The check for card presence and queueing of the request must be
	 * atomic, otherwise the card could be removed in between and the
	 * request wouldn't fail until another card was inserted.
	 */
	spin_lock_bh(&host->lock);

	if (!test_bit(DW_MMC_CARD_PRESENT, &slot->flags)) {
		spin_unlock_bh(&host->lock);
		mrq->cmd->error = -ENOMEDIUM;		
		MMC_DBG_CMD_FUNC(host->mmc, "%d..%s: no card,so reqeuest done, cmd=%d [%s]",\
		    __LINE__, __FUNCTION__, mrq->cmd->opcode, mmc_hostname(host->mmc));
            
		mmc_request_done(mmc, mrq);
		return;
	}

	MMC_DBG_CMD_FUNC(host->mmc, "======>\n    pull a new request from MMC-frame to dw_mci_queue. cmd=%d(arg=0x%x)[%s]", \
                                mrq->cmd->opcode, mrq->cmd->arg, mmc_hostname(host->mmc));

	dw_mci_queue_request(host, slot, mrq);

	spin_unlock_bh(&host->lock);
}

static void dw_mci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct dw_mci_slot *slot = mmc_priv(mmc);
	#ifdef CONFIG_MMC_DW_ROCKCHIP_SWITCH_VOLTAGE
        struct dw_mci *host = slot->host;
        #endif
	const struct dw_mci_drv_data *drv_data = slot->host->drv_data;
	u32 regs;
	
        #ifdef SDMMC_WAIT_FOR_UNBUSY
	unsigned long   time_loop;
	bool ret = true;

        #ifdef CONFIG_MMC_DW_ROCKCHIP_SWITCH_VOLTAGE
        if(host->svi_flags == 1)
                time_loop = jiffies + msecs_to_jiffies(SDMMC_DATA_TIMEOUT_SD);
        else
                time_loop = jiffies + msecs_to_jiffies(SDMMC_WAIT_FOR_UNBUSY);
        #else
                time_loop = jiffies + msecs_to_jiffies(SDMMC_WAIT_FOR_UNBUSY);
        #endif
        
	if(!test_bit(DW_MMC_CARD_PRESENT, &slot->flags)){
		printk("%d..%s:  no card. [%s]\n", \
			__LINE__, __FUNCTION__, mmc_hostname(mmc));
		goto EXIT_POWER;
	}
    
	while (ret) {
		ret = time_before(jiffies, time_loop);
		regs = mci_readl(slot->host, STATUS);
		if (!(regs & (SDMMC_STAUTS_DATA_BUSY|SDMMC_STAUTS_MC_BUSY)))
    			break;
	};
	
	if(false == ret)
	{
		printk("slot->flags = %lu ", slot->flags);
		#ifdef CONFIG_MMC_DW_ROCKCHIP_SWITCH_VOLTAGE
                if(host->svi_flags != 1)
                #endif
                        dump_stack();
		printk("%d..%s:  wait for unbusy timeout....... STATUS = 0x%x [%s]\n", \
			__LINE__, __FUNCTION__, regs, mmc_hostname(mmc));
	}
        #endif
        
	switch (ios->bus_width) {
	case MMC_BUS_WIDTH_4:
		slot->ctype = SDMMC_CTYPE_4BIT;
		break;			
	case MMC_BUS_WIDTH_8: 
		slot->ctype = SDMMC_CTYPE_8BIT;
		break;	
	default:
		/* set default 1 bit mode */
		slot->ctype = SDMMC_CTYPE_1BIT;
		slot->pre_ctype = SDMMC_CTYPE_1BIT;
	}

	regs = mci_readl(slot->host, UHS_REG);

	/* DDR mode set */
	if (ios->timing == MMC_TIMING_UHS_DDR50)
		regs |= ((0x1 << slot->id) << 16);
	else
		regs &= ~((0x1 << slot->id) << 16);

	mci_writel(slot->host, UHS_REG, regs);
	slot->host->timing = ios->timing;

	/*
	 * Use mirror of ios->clock to prevent race with mmc
	 * core ios update when finding the minimum.
	 */
	slot->clock = ios->clock;

	if (drv_data && drv_data->set_ios)
		drv_data->set_ios(slot->host, ios);

	/* Slot specific timing and width adjustment */
	dw_mci_setup_bus(slot, false);
		//return -EAGAIN;

EXIT_POWER:
	switch (ios->power_mode) {
	        case MMC_POWER_UP:
                        set_bit(DW_MMC_CARD_NEED_INIT, &slot->flags);
                        /* Power up slot */
		        if (slot->host->pdata->setpower)
			        slot->host->pdata->setpower(slot->id, mmc->ocr_avail);
		        regs = mci_readl(slot->host, PWREN);
		        regs |= (1 << slot->id);
		        mci_writel(slot->host, PWREN, regs);
		        break;
                case MMC_POWER_OFF:
		        /* Power down slot */
		        if(slot->host->pdata->setpower)
			        slot->host->pdata->setpower(slot->id, 0);
		        regs = mci_readl(slot->host, PWREN);
		        regs &= ~(1 << slot->id);
		        mci_writel(slot->host, PWREN, regs);		
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
	if(slot->quirks & DW_MCI_SLOT_QUIRK_NO_WRITE_PROTECT)
		read_only = 0;
	else if(brd->get_ro)
		read_only = brd->get_ro(slot->id);
	else if(gpio_is_valid(slot->wp_gpio))
		read_only = gpio_get_value(slot->wp_gpio);
	else
		read_only =
			mci_readl(slot->host, WRTPRT) & (1 << slot->id) ? 1 : 0;

	dev_dbg(&mmc->class_dev, "card is %s\n",
		read_only ? "read-only" : "read-write");

	return read_only;
}

static int dw_mci_set_sdio_status(struct mmc_host *mmc, int val)
{
        struct dw_mci_slot *slot = mmc_priv(mmc);
        struct dw_mci *host = slot->host;
	/*struct dw_mci_board *brd = slot->host->pdata;*/

	if (!(mmc->restrict_caps & RESTRICT_CARD_TYPE_SDIO))
                return 0;
                
        spin_lock_bh(&host->lock);

        if(val)
                set_bit(DW_MMC_CARD_PRESENT, &slot->flags);
        else
                clear_bit(DW_MMC_CARD_PRESENT, &slot->flags);

        spin_unlock_bh(&host->lock);

        if(test_bit(DW_MMC_CARD_PRESENT, &slot->flags)){
                if(__clk_is_enabled(host->hclk_mmc) == false)
                        clk_prepare_enable(host->hclk_mmc);
                if(__clk_is_enabled(host->clk_mmc) == false)
                        clk_prepare_enable(host->clk_mmc);
        }else{
                if(__clk_is_enabled(host->clk_mmc) == true)
                        clk_disable_unprepare(slot->host->clk_mmc);
                if(__clk_is_enabled(host->hclk_mmc) == true)
                        clk_disable_unprepare(slot->host->hclk_mmc);
        }

        mmc_detect_change(slot->mmc, 20);
        return 0;
}



static int dw_mci_get_cd(struct mmc_host *mmc)
{
	int present;
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci_board *brd = slot->host->pdata;
	struct dw_mci *host = slot->host;
	int gpio_cd = mmc_gpio_get_cd(mmc);
	
        if (mmc->restrict_caps & RESTRICT_CARD_TYPE_SDIO)
                return test_bit(DW_MMC_CARD_PRESENT, &slot->flags);

	/* Use platform get_cd function, else try onboard card detect */
	if (brd->quirks & DW_MCI_QUIRK_BROKEN_CARD_DETECTION)
		present = 1;
	else if (brd->get_cd)
		present = !brd->get_cd(slot->id);
	else if (!IS_ERR_VALUE(gpio_cd))
		present = gpio_cd;
	else
		present = (mci_readl(slot->host, CDETECT) & (1 << slot->id))
			== 0 ? 1 : 0;
	spin_lock_bh(&host->lock);
	if (present) {
		set_bit(DW_MMC_CARD_PRESENT, &slot->flags);
		dev_dbg(&mmc->class_dev, "card is present\n");
	} else {
		clear_bit(DW_MMC_CARD_PRESENT, &slot->flags);
		dev_dbg(&mmc->class_dev, "card is not present\n");
	}
	spin_unlock_bh(&host->lock);

	return present;
}


/*
 * Dts Should caps emmc controller with poll-hw-reset
 */
static void dw_mci_hw_reset(struct mmc_host *mmc)
{
        struct dw_mci_slot *slot = mmc_priv(mmc);
        struct dw_mci *host = slot->host;
        u32 regs;

        #if 0
        u32 cmd_flags;
        unsigned long timeout;
        bool ret = true;

        /* (1) CMD12 to end any transfer in process */
        cmd_flags = SDMMC_CMD_STOP | SDMMC_CMD_RESP_CRC
			| SDMMC_CMD_RESP_EXP | MMC_STOP_TRANSMISSION;

	if(host->mmc->hold_reg_flag)
		cmd_flags |= SDMMC_CMD_USE_HOLD_REG;
	mci_writel(host, CMDARG, 0);
	wmb();
	mci_writel(host, CMD, cmd_flags | SDMMC_CMD_START);
	wmb();
	timeout = jiffies + msecs_to_jiffies(500);
	while(ret){
                ret = time_before(jiffies, timeout);
                if(!(mci_readl(host, CMD) & SDMMC_CMD_START))
                        break;
	}
	
	if(false == ret)
		MMC_DBG_ERR_FUNC(host->mmc,
			"%s dw_mci_hw_reset: STOP_TRANSMISSION failed!!! [%s]\n",
			__func__, mmc_hostname(host->mmc));
        
	/* (2) wait DTO, even if no response is sent back by card */
	ret = true;
	timeout = jiffies + msecs_to_jiffies(5);
	while(ret){
		ret = time_before(jiffies, timeout);
		if(!(mci_readl(host, MINTSTS) & SDMMC_INT_DATA_OVER)){
			mci_writel(host, RINTSTS, SDMMC_INT_DATA_OVER);
			break;
		}
	}
        #endif

        /* (3) Reset following: DONNOT CHANGE RESET ORDER!*/

	/* Software reset - BMOD[0] for IDMA only */
	regs = mci_readl(host, BMOD);
	regs |= SDMMC_IDMAC_SWRESET;
	mci_writel(host, BMOD, regs);
	udelay(1); /* Auto cleared after 1 cycle, 1us is enough for hclk_mmc */
	regs = mci_readl(host, BMOD);
	if(regs & SDMMC_IDMAC_SWRESET)
		MMC_DBG_WARN_FUNC(host->mmc,
			"%s dw_mci_hw_reset: SDMMC_IDMAC_SWRESET failed!!! [%s]\n",
			__func__, mmc_hostname(host->mmc));

	/* DMA reset - CTRL[2] */
	regs = mci_readl(host, CTRL);
	regs |= SDMMC_CTRL_DMA_RESET;
	mci_writel(host, CTRL, regs);
	udelay(1); /* Auto cleared after 2 AHB clocks, 1us is enough plus mci_readl access */
	regs = mci_readl(host, CTRL);
	if(regs & SDMMC_CTRL_DMA_RESET)
		MMC_DBG_WARN_FUNC(host->mmc,
			"%s dw_mci_hw_reset: SDMMC_CTRL_DMA_RESET failed!!! [%s]\n",
			__func__, mmc_hostname(host->mmc));

	/* FIFO reset - CTRL[1] */
	regs = mci_readl(host, CTRL);
	regs |= SDMMC_CTRL_FIFO_RESET;
	mci_writel(host, CTRL, regs);
	mdelay(1); /* no timing limited, 1ms is random value */
	regs = mci_readl(host, CTRL);
	if(regs & SDMMC_CTRL_FIFO_RESET)
		MMC_DBG_WARN_FUNC(host->mmc,
			"%s dw_mci_hw_reset: SDMMC_CTRL_DMA_RESET failed!!! [%s]\n",
			__func__, mmc_hostname(host->mmc));

	/* (4) CARD_RESET
	According to eMMC spec
	tRstW >= 1us ;   RST_n pulse width
	tRSCA >= 200us ; RST_n to Command time
	tRSTH >= 1us ;   RST_n high period
	*/
	mci_writel(slot->host, PWREN, 0x0);
	mci_writel(slot->host, RST_N, 0x0);
	dsb();
	udelay(10); /* 10us for bad quality eMMc. */

	mci_writel(slot->host, PWREN, 0x1);
	mci_writel(slot->host, RST_N, 0x1);
	dsb();
	usleep_range(500, 1000); /* at least 500(> 200us) */
}

/*
 * Disable lower power mode.
 *
 * Low power mode will stop the card clock when idle.  According to the
 * description of the CLKENA register we should disable low power mode
 * for SDIO cards if we need SDIO interrupts to work.
 *
 * This function is fast if low power mode is already disabled.
 */
static void dw_mci_disable_low_power(struct dw_mci_slot *slot)
{
	struct dw_mci *host = slot->host;
	u32 clk_en_a;
	const u32 clken_low_pwr = SDMMC_CLKEN_LOW_PWR << slot->id;

	clk_en_a = mci_readl(host, CLKENA);

	if (clk_en_a & clken_low_pwr) {
		mci_writel(host, CLKENA, clk_en_a & ~clken_low_pwr);
		mci_send_cmd(slot, SDMMC_CMD_UPD_CLK |
			     SDMMC_CMD_PRV_DAT_WAIT, 0);
	}
}

static void dw_mci_enable_sdio_irq(struct mmc_host *mmc, int enb)
{
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci *host = slot->host;
	u32 int_mask;
	u32 sdio_int;

	/* Enable/disable Slot Specific SDIO interrupt */
	int_mask = mci_readl(host, INTMASK);

        if (host->verid < DW_MMC_240A)
		sdio_int = SDMMC_INT_SDIO(slot->id);
	else
		sdio_int = SDMMC_INT_SDIO((slot->id) + 8);
	
	if (enb) {
		/*
		 * Turn off low power mode if it was enabled.  This is a bit of
		 * a heavy operation and we disable / enable IRQs a lot, so
		 * we'll leave low power mode disabled and it will get
		 * re-enabled again in dw_mci_setup_bus().
		 */
		dw_mci_disable_low_power(slot);

		mci_writel(host, INTMASK,
			   (int_mask | sdio_int));
	} else {
		mci_writel(host, INTMASK,
			   (int_mask & ~sdio_int));
	}
}

#ifdef CONFIG_MMC_DW_ROCKCHIP_SWITCH_VOLTAGE
enum{
        IO_DOMAIN_12 = 1200,
        IO_DOMAIN_18 = 1800,
        IO_DOMAIN_33 = 3300,
};
static void dw_mci_do_grf_io_domain_switch(struct dw_mci *host, u32 voltage)
{
        switch(voltage){
                case IO_DOMAIN_33:
                        voltage = 0;
                        break;
                case IO_DOMAIN_18:
                        voltage = 1;
                        break;
                case IO_DOMAIN_12:
                        MMC_DBG_ERR_FUNC(host->mmc,"%s : Not support io domain voltage [%s]\n",
                                                        __FUNCTION__, mmc_hostname(host->mmc));
                        break;
                default:
                        MMC_DBG_ERR_FUNC(host->mmc,"%s : Err io domain voltage [%s]\n",
                                                        __FUNCTION__, mmc_hostname(host->mmc));
                        break;
        }

        if(cpu_is_rk3288()){
                if(host->mmc->restrict_caps & RESTRICT_CARD_TYPE_SD)	   
                        grf_writel((voltage << 7) | (1 << 23), RK3288_GRF_IO_VSEL);
                else
                        return ;
        }else{
                MMC_DBG_ERR_FUNC(host->mmc,"%s : unknown chip [%s]\n",
                                        __FUNCTION__, mmc_hostname(host->mmc));
        }
}

static int dw_mci_do_start_signal_voltage_switch(struct dw_mci *host,
						struct mmc_ios *ios)
{
	int ret;
	unsigned int value,uhs_reg;

	/*
	 * Signal Voltage Switching is only applicable for Host Controllers
	 * v3.00 and above.
	 */
	if (host->verid < DW_MMC_240A)
		return 0;

	uhs_reg = mci_readl(host, UHS_REG);
	MMC_DBG_SW_VOL_FUNC(host->mmc,"%d..%s: vol=%d.[%s]\n",
	        __LINE__, __FUNCTION__,ios->signal_voltage, mmc_hostname(host->mmc));

	switch (ios->signal_voltage) {
        case MMC_SIGNAL_VOLTAGE_330:
        	/* Set 1.8V Signal Enable in the Host Control2 register to 0 */
        	if (host->vmmc) {
        		ret = io_domain_regulator_set_voltage(host->vmmc, 3300000, 3300000);
        		/* regulator_put(host->vmmc); //to be done in remove function. */
        		
        		MMC_DBG_SW_VOL_FUNC(host->mmc,"%s   =%dmV  set 3.3end, ret=%d  \n", 
        		    __func__, regulator_get_voltage(host->vmmc), ret);
        		if (ret) {
        			MMC_DBG_SW_VOL_FUNC(host->mmc, "%s: Switching to 3.3V signalling voltage "
        					" failed\n", mmc_hostname(host->mmc));
        			return -EIO;
        		}
        		dw_mci_do_grf_io_domain_switch(host, IO_DOMAIN_33);
        	}
        	MMC_DBG_SW_VOL_FUNC(host->mmc,"%d..%s: [%s]\n",__LINE__, 
        	                        __FUNCTION__, mmc_hostname(host->mmc));

        	/* set High-power mode */
        	value = mci_readl(host, CLKENA);
        	value &= ~SDMMC_CLKEN_LOW_PWR;
        	mci_writel(host,CLKENA , value);
        	/* SDMMC_UHS_REG */
        	uhs_reg &= ~SDMMC_UHS_VOLT_REG_18; 
        	mci_writel(host,UHS_REG , uhs_reg);
        	
        	/* Wait for 5ms */
        	usleep_range(5000, 5500);

        	/* 3.3V regulator output should be stable within 5 ms */
        	uhs_reg = mci_readl(host, UHS_REG);
        	if( !(uhs_reg & SDMMC_UHS_VOLT_REG_18))
                        return 0;	

        	MMC_DBG_SW_VOL_FUNC(host->mmc, "%s: 3.3V regulator output did not became stable\n",
        			mmc_hostname(host->mmc));

        	return -EAGAIN;
        case MMC_SIGNAL_VOLTAGE_180:
        	if (host->vmmc) {
        		ret = io_domain_regulator_set_voltage(host->vmmc,1800000, 1800000);
                        /* regulator_put(host->vmmc);//to be done in remove function. */

        		MMC_DBG_SW_VOL_FUNC(host->mmc,"%d..%s   =%dmV  set 1.8end, ret=%d . \n",
        		    __LINE__, __func__, regulator_get_voltage(host->vmmc), ret);
        		if (ret) {
        			MMC_DBG_SW_VOL_FUNC(host->mmc, "%s: Switching to 1.8V signalling voltage "
        					" failed\n", mmc_hostname(host->mmc));
        			return -EIO;
        		}
        		dw_mci_do_grf_io_domain_switch(host, IO_DOMAIN_18);
        	}

        	/*
        	 * Enable 1.8V Signal Enable in the Host Control2
        	 * register
        	 */
                mci_writel(host,UHS_REG , uhs_reg | SDMMC_UHS_VOLT_REG_18);

        	/* Wait for 5ms */
        	usleep_range(5000, 5500);
                MMC_DBG_SW_VOL_FUNC(host->mmc,"%d..%s: .[%s]\n",__LINE__, 
                                        __FUNCTION__,mmc_hostname(host->mmc));

        	/* 1.8V regulator output should be stable within 5 ms */
        	uhs_reg = mci_readl(host, UHS_REG);
        	if(uhs_reg & SDMMC_UHS_VOLT_REG_18)
        	        return 0;
      
        	MMC_DBG_SW_VOL_FUNC(host->mmc, "%s: 1.8V regulator output did not became stable\n",
        			mmc_hostname(host->mmc));

        	return -EAGAIN;
        case MMC_SIGNAL_VOLTAGE_120:
        	if (host->vmmc) {
        		ret = io_domain_regulator_set_voltage(host->vmmc, 1200000, 1200000);
        		if (ret) {
        			MMC_DBG_SW_VOL_FUNC(host->mmc, "%s: Switching to 1.2V signalling voltage "
        					" failed\n", mmc_hostname(host->mmc));
        			return -EIO;
        		}
        	}
        	return 0;
        default:
        	/* No signal voltage switch required */
        	return 0;
	}
}


static int dw_mci_start_signal_voltage_switch(struct mmc_host *mmc,
	struct mmc_ios *ios)
{
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci *host = slot->host;
	int err;

	if (host->verid < DW_MMC_240A)
		return 0;
	
	err = dw_mci_do_start_signal_voltage_switch(host, ios);
	return err;
}

#endif

static int dw_mci_execute_tuning(struct mmc_host *mmc, u32 opcode)
{
	struct dw_mci_slot *slot = mmc_priv(mmc);
	struct dw_mci *host = slot->host;
	const struct dw_mci_drv_data *drv_data = host->drv_data;
	struct dw_mci_tuning_data tuning_data;
	int err = -ENOSYS;

        /* Fixme: 3036/3126 doesn't support 1.8 io domain, no sense exe tuning
        if(cpu_is_3036() || cpu_is_3126())
                return ENOSYS;
        AND
                what about audi-b?
        */

	if (opcode == MMC_SEND_TUNING_BLOCK_HS200) {
		if (mmc->ios.bus_width == MMC_BUS_WIDTH_8) {
			tuning_data.blk_pattern = tuning_blk_pattern_8bit;
			tuning_data.blksz = sizeof(tuning_blk_pattern_8bit);
		} else if (mmc->ios.bus_width == MMC_BUS_WIDTH_4) {
			tuning_data.blk_pattern = tuning_blk_pattern_4bit;
			tuning_data.blksz = sizeof(tuning_blk_pattern_4bit);
		} else {
			return -EINVAL;
		}
	} else if (opcode == MMC_SEND_TUNING_BLOCK) {
		tuning_data.blk_pattern = tuning_blk_pattern_4bit;
		tuning_data.blksz = sizeof(tuning_blk_pattern_4bit);
	} else {
		dev_err(host->dev,
			"Undefined command(%d) for tuning\n", opcode);
		return -EINVAL;
	}

    
	/* Recommend sample phase and delayline
           Fixme: Mix-use these three controllers will cause
           con_id mismatch.
         */
	if (mmc->restrict_caps & RESTRICT_CARD_TYPE_EMMC)
	    tuning_data.con_id = 3;
	else if(mmc->restrict_caps & RESTRICT_CARD_TYPE_SDIO)
	    tuning_data.con_id = 1;
	else
	    tuning_data.con_id = 0;	

	/* 0: driver, from host->devices
	   1: sample, from devices->host
	 */    
	tuning_data.tuning_type = 1; 
   
	if (drv_data && drv_data->execute_tuning)
		err = drv_data->execute_tuning(slot, opcode, &tuning_data);
		
	return err;
}

static const struct mmc_host_ops dw_mci_ops = {
	.request		= dw_mci_request,
	.pre_req		= dw_mci_pre_req,
	.post_req		= dw_mci_post_req,
	.set_ios		= dw_mci_set_ios,
	.get_ro			= dw_mci_get_ro,
	.get_cd			= dw_mci_get_cd,
	.set_sdio_status    = dw_mci_set_sdio_status,
	.hw_reset       = dw_mci_hw_reset,
	.enable_sdio_irq	= dw_mci_enable_sdio_irq,
	.execute_tuning		= dw_mci_execute_tuning,
        #ifdef CONFIG_MMC_DW_ROCKCHIP_SWITCH_VOLTAGE
        .start_signal_voltage_switch	= dw_mci_start_signal_voltage_switch,
        .card_busy  = dw_mci_card_busy,
        #endif
};

#if 0
static void dw_mci_enable_irq(struct dw_mci *host, bool irqflag)
{
	unsigned long flags;

    if(!host)
        return;
    
	local_irq_save(flags);
	if(host->irq_state != irqflag)
	{
	    host->irq_state = irqflag;
	    if(irqflag)
	    {
	        enable_irq(host->irq);
	    }
	    else
	    {
	        disable_irq(host->irq);
	    }
	}
	local_irq_restore(flags);
}
#endif

static void dw_mci_deal_data_end(struct dw_mci *host, struct mmc_request *mrq)
	__releases(&host->lock)
	__acquires(&host->lock)
{
        if(DW_MCI_SEND_STATUS == host->dir_status){
	    #if 0
	    if( MMC_BUS_TEST_W != host->cmd->opcode){
	        if(host->data_status & SDMMC_INT_DCRC)
	            host->data->error = -EILSEQ;
	        else if(host->data_status & SDMMC_INT_EBE)
	            host->data->error = -ETIMEDOUT;
	    } else {
	        dw_mci_wait_unbusy(host); 
	    }
	    #else
	    dw_mci_wait_unbusy(host);
	    #endif
        }
}

static void dw_mci_request_end(struct dw_mci *host, struct mmc_request *mrq)
	__releases(&host->lock)
	__acquires(&host->lock)
{
	struct dw_mci_slot *slot;
	struct mmc_host	*prev_mmc = host->cur_slot->mmc;

	WARN_ON(host->cmd || host->data);

	del_timer_sync(&host->dto_timer);
        dw_mci_deal_data_end(host, mrq);

        if(mrq->cmd)
                MMC_DBG_CMD_FUNC(host->mmc, " reqeust end--reqeuest done, cmd=%d, cmderr=%d, host->state=%d [%s]",\
                        mrq->cmd->opcode,mrq->cmd->error, host->state,mmc_hostname(host->mmc));
        if(mrq->data)
                MMC_DBG_CMD_FUNC(host->mmc, " reqeust end--reqeuest done, cmd=%d, dataerr=%d, host->state=%d [%s]",\
                        mrq->cmd->opcode,mrq->data->error, host->state, mmc_hostname(host->mmc));

	host->cur_slot->mrq = NULL;
	host->mrq = NULL;
	if (!list_empty(&host->queue)) {
		slot = list_entry(host->queue.next,
				  struct dw_mci_slot, queue_node);
		list_del(&slot->queue_node);
		dev_vdbg(host->dev, "list not empty: %s is next\n",
			 mmc_hostname(slot->mmc));
		host->state = STATE_SENDING_CMD;
		MMC_DBG_CMD_FUNC(host->mmc, " list is not empty. run the request in list. [%s]", mmc_hostname(host->mmc));
		dw_mci_start_request(host, slot);
	} else {
		dev_vdbg(host->dev, "list empty\n");
		host->state = STATE_IDLE;
	}

        spin_unlock(&host->lock);
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
			
            MMC_DBG_INFO_FUNC(host->mmc,"Line%d: command complete cmd=%d,resp[3]=0x%x, resp[2]=0x%x,resp[1]=0x%x,resp[0]=0x%x.[%s]", \
                    __LINE__,cmd->opcode,cmd->resp[3], cmd->resp[2], cmd->resp[1], cmd->resp[0], mmc_hostname(host->mmc));
		} else {
			cmd->resp[0] = mci_readl(host, RESP0);
			cmd->resp[1] = 0;
			cmd->resp[2] = 0;
			cmd->resp[3] = 0;			
            MMC_DBG_INFO_FUNC(host->mmc, "Line%d: command complete cmd=%d,resp[0]=0x%x. [%s]",\
                    __LINE__,cmd->opcode, cmd->resp[0], mmc_hostname(host->mmc));
		}
	}

	if (status & SDMMC_INT_RTO)
	{
	    if(host->mmc->restrict_caps & RESTRICT_CARD_TYPE_SDIO)
	        host->cmd_rto += 1;

	        cmd->error = -ETIMEDOUT;
                del_timer_sync(&host->dto_timer);
	}else if ((cmd->flags & MMC_RSP_CRC) && (status & SDMMC_INT_RCRC)){
                del_timer_sync(&host->dto_timer);
		cmd->error = -EILSEQ;
        }else if (status & SDMMC_INT_RESP_ERR){
                del_timer_sync(&host->dto_timer);
		cmd->error = -EIO;
        }else{
		cmd->error = 0;
	}
        MMC_DBG_CMD_FUNC(host->mmc, " command complete, cmd=%d,cmdError=%d [%s]",
                                cmd->opcode, cmd->error,mmc_hostname(host->mmc));

	if (cmd->error) {
                del_timer_sync(&host->dto_timer);
	    if(MMC_SEND_STATUS != cmd->opcode)
	        if(host->cmd_rto >= SDMMC_CMD_RTO_MAX_HOLD){
	                MMC_DBG_ERR_FUNC(host->mmc, " command complete, cmd=%d,cmdError=%d [%s]",\
                                cmd->opcode, cmd->error,mmc_hostname(host->mmc));
                        host->cmd_rto = 0;
    	        }
	        
		/* newer ip versions need a delay between retries */
		if (host->quirks & DW_MCI_QUIRK_RETRY_DELAY)
			mdelay(20);
	}

}

static void dw_mci_tasklet_func(unsigned long priv)
{
	struct dw_mci *host = (struct dw_mci *)priv;
        struct dw_mci_slot *slot = mmc_priv(host->mmc);
	struct mmc_data	*data;
	struct mmc_command *cmd;
	enum dw_mci_state state;
	enum dw_mci_state prev_state;
	u32 status, cmd_flags;
	unsigned long timeout = 0;
	bool ret = true;

	spin_lock(&host->lock);

	state = host->state;
	data = host->data;

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
			
                        if (cmd->data && cmd->error) {
                                del_timer_sync(&host->dto_timer); /* delete the timer for INT_DTO */
				dw_mci_stop_dma(host);
				#if 1
                                if (data->stop) {
                                        send_stop_cmd(host, data);
                                        state = STATE_SENDING_STOP;
                                        break;
                                }else{
                                        host->data = NULL;
                                }
				#else
				send_stop_abort(host, data);
				state = STATE_SENDING_STOP;
				break;
				#endif
                                set_bit(EVENT_DATA_COMPLETE, &host->completed_events);
			}

			if (!host->mrq->data || cmd->error) {
				dw_mci_request_end(host, host->mrq);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_DATA;
			/* fall through */

		case STATE_SENDING_DATA:
			if (test_and_clear_bit(EVENT_DATA_ERROR, &host->pending_events)) {
				dw_mci_stop_dma(host);
				#if 1
				if (data->stop){
					send_stop_cmd(host, data);
				}else{
                                        /*single block read/write, send stop cmd manually to prevent host controller halt*/
                                        MMC_DBG_INFO_FUNC(host->mmc, "%s status 1 0x%08x [%s]\n",
                                                        __func__, mci_readl(host, STATUS), mmc_hostname(host->mmc));
                        
					mci_writel(host, CMDARG, 0);
                                        wmb();
                                        cmd_flags = SDMMC_CMD_STOP | SDMMC_CMD_RESP_CRC 
                                                        | SDMMC_CMD_RESP_EXP | MMC_STOP_TRANSMISSION;

                                        if(host->mmc->hold_reg_flag)
                                                cmd_flags |= SDMMC_CMD_USE_HOLD_REG;
				
                                        mci_writel(host, CMD, cmd_flags | SDMMC_CMD_START);
                                        wmb();
                                        timeout = jiffies + msecs_to_jiffies(500);
                        
                                        while(ret){
                                                ret = time_before(jiffies, timeout);
                                                if(!(mci_readl(host, CMD) & SDMMC_CMD_START))
                                                        break;
                                        }
                                        if(false == ret)
                                                MMC_DBG_ERR_FUNC(host->mmc,
                                                        "%s EVENT_DATA_ERROR recovery failed!!! [%s]\n",
                                                        __func__, mmc_hostname(host->mmc));
                                }
				#else
				send_stop_abort(host, data);
				#endif
				state = STATE_DATA_ERROR;
				break;
			}

			MMC_DBG_CMD_FUNC(host->mmc, 
				"Pre-state[%d]-->NowState[%d]: STATE_SENDING_DATA, wait for EVENT_XFER_COMPLETE.[%s]",\
                        prev_state,state, mmc_hostname(host->mmc));

			if (!test_and_clear_bit(EVENT_XFER_COMPLETE,
						&host->pending_events))
				break;
			MMC_DBG_INFO_FUNC(host->mmc, 
				"Pre-state[%d]-->NowState[%d]:  STATE_SENDING_DATA, wait for EVENT_DATA_COMPLETE. [%s]",\
                                          prev_state,state,mmc_hostname(host->mmc));
            
			set_bit(EVENT_XFER_COMPLETE, &host->completed_events);
			prev_state = state = STATE_DATA_BUSY;
			/* fall through */

		case STATE_DATA_BUSY:
			if (!test_and_clear_bit(EVENT_DATA_COMPLETE,
						&host->pending_events))
				break;
				
			dw_mci_deal_data_end(host, host->mrq);			
			del_timer_sync(&host->dto_timer); //delete the timer for INT_DTO
                        MMC_DBG_INFO_FUNC(host->mmc, 
				"Pre-state[%d]-->NowState[%d]: STATE_DATA_BUSY, after EVENT_DATA_COMPLETE. [%s]", \
                                          prev_state,state,mmc_hostname(host->mmc));

			host->data = NULL;
			set_bit(EVENT_DATA_COMPLETE, &host->completed_events);
			status = host->data_status;

			if (status & DW_MCI_DATA_ERROR_FLAGS) {	
			    if((SDMMC_CTYPE_1BIT != slot->ctype)&&(MMC_SEND_EXT_CSD == host->mrq->cmd->opcode))
				MMC_DBG_ERR_FUNC(host->mmc, 
					"Pre-state[%d]-->NowState[%d]: DW_MCI_DATA_ERROR_FLAGS,datastatus=0x%x [%s]",\
                            prev_state,state, status, mmc_hostname(host->mmc));
                            
		        if (status & SDMMC_INT_DRTO) {
					data->error = -ETIMEDOUT;
				} else if (status & SDMMC_INT_DCRC) {
					data->error = -EILSEQ;
				} else if (status & SDMMC_INT_EBE &&
				host->dir_status == DW_MCI_SEND_STATUS){
					/*
					 * No data CRC status was returned.
					 * The number of bytes transferred will
					 * be exaggerated in PIO mode.
					 */
					data->bytes_xfered = 0;
					data->error = -ETIMEDOUT;
				} else {
					dev_err(host->dev,
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
				dw_mci_fifo_reset(host);
			} else {
				data->bytes_xfered = data->blocks * data->blksz;
				data->error = 0;
			}

			if (!data->stop) {
		                MMC_DBG_CMD_FUNC(host->mmc, "Pre-state[%d]-->NowState[%d]: no stop and no dataerr, exit. [%s]", \
                                      prev_state,state,mmc_hostname(host->mmc));
				dw_mci_request_end(host, host->mrq);
				goto unlock;
			}
                        MMC_DBG_CMD_FUNC(host->mmc, "Pre-state[%d]-->NowState[%d]: begin to stop . [%s]", \
                                                prev_state,state,mmc_hostname(host->mmc));

			if (host->mrq->sbc && !data->error) {
				data->stop->error = 0;
				
                        MMC_DBG_CMD_FUNC(host->mmc, "Pre-state[%d]-->NowState[%d]: have stop and sbc, exit. [%s]", \
                                                prev_state,state,mmc_hostname(host->mmc));

				dw_mci_request_end(host, host->mrq);
				goto unlock;
			}

			prev_state = state = STATE_SENDING_STOP;
			if (!data->error)
			    send_stop_cmd(host, data);
			#if 0
			if (data->stop && !data->error) {
				/* stop command for open-ended transfer*/
				
				send_stop_abort(host, data);
			}
			#endif
			/* fall through */
                        MMC_DBG_CMD_FUNC(host->mmc, "Pre-state[%d]-->NowState[%d]: begin to STATE_SENDING_STOP . [%s]", \
                                                prev_state,state,mmc_hostname(host->mmc));

		case STATE_SENDING_STOP:
			if (!test_and_clear_bit(EVENT_CMD_COMPLETE, &host->pending_events))
				break;

                        MMC_DBG_CMD_FUNC(host->mmc, "Pre-state[%d]-->NowState[%d]: begin to send cmd12 . [%s]", \
                                                prev_state, state, mmc_hostname(host->mmc));
                        
                        /* CMD error in data command */
			if (host->mrq->cmd->error && host->mrq->data) {
				dw_mci_fifo_reset(host);
			}

			host->cmd = NULL;
			host->data = NULL;
                        #if 1
                        dw_mci_command_complete(host, host->mrq->stop);
                        #else
			if (host->mrq->stop)
				dw_mci_command_complete(host, host->mrq->stop);
			else
				host->cmd_status = 0;
                        #endif
            
			dw_mci_request_end(host, host->mrq);
			goto unlock;

		case STATE_DATA_ERROR:
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
	struct mmc_data *data = host->data;
	int init_cnt = cnt;

	/* try and push anything in the part_buf */
	if (unlikely(host->part_buf_count)) {
		int len = dw_mci_push_part_bytes(host, buf, cnt);
		buf += len;
		cnt -= len;
		if (host->part_buf_count == 2) {
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
		dw_mci_set_part_bytes(host, buf, cnt);
		 /* Push data if we have reached the expected data length */
		if ((data->bytes_xfered + init_cnt) ==
		    (data->blksz * data->blocks))
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
	struct mmc_data *data = host->data;
	int init_cnt = cnt;

	/* try and push anything in the part_buf */
	if (unlikely(host->part_buf_count)) {
		int len = dw_mci_push_part_bytes(host, buf, cnt);
		buf += len;
		cnt -= len;
		if (host->part_buf_count == 4) {
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
		dw_mci_set_part_bytes(host, buf, cnt);
		 /* Push data if we have reached the expected data length */
		if ((data->bytes_xfered + init_cnt) ==
		    (data->blksz * data->blocks))
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
	struct mmc_data *data = host->data;
	int init_cnt = cnt;

	/* try and push anything in the part_buf */
	if (unlikely(host->part_buf_count)) {
		int len = dw_mci_push_part_bytes(host, buf, cnt);
		buf += len;
		cnt -= len;

		if (host->part_buf_count == 8) {
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
		dw_mci_set_part_bytes(host, buf, cnt);
		/* Push data if we have reached the expected data length */
		if ((data->bytes_xfered + init_cnt) ==
		    (data->blksz * data->blocks))
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

static void dw_mci_read_data_pio(struct dw_mci *host, bool dto)
{
	struct sg_mapping_iter *sg_miter = &host->sg_miter;
	void *buf;
	unsigned int offset;
	struct mmc_data	*data = host->data;
	int shift = host->data_shift;
	u32 status;
	unsigned int len;
	unsigned int remain, fcnt;

	if(!host->mmc->bus_refs){
		printk("Note: %s host->mmc->bus_refs is 0!!!\n", __func__);
		goto host_put;
	}
	do {
		if (!sg_miter_next(sg_miter))
			goto done;

		host->sg = sg_miter->piter.sg;
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
			data->bytes_xfered += len;
			offset += len;
			remain -= len;
		} while (remain);

		sg_miter->consumed = offset;
		status = mci_readl(host, MINTSTS);
		mci_writel(host, RINTSTS, SDMMC_INT_RXDR);
	/* if the RXDR is ready read again */
	} while ((status & SDMMC_INT_RXDR) ||
		 (dto && SDMMC_GET_FCNT(mci_readl(host, STATUS))));

	if (!remain) {
		if (!sg_miter_next(sg_miter))
			goto done;
		sg_miter->consumed = 0;
	}
	sg_miter_stop(sg_miter);
	return;

done:
	sg_miter_stop(sg_miter);
host_put:	
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
	unsigned int len;
	unsigned int fifo_depth = host->fifo_depth;
	unsigned int remain, fcnt;
	
	if(!host->mmc->bus_refs){
		printk("Note: %s host->mmc->bus_refs is 0!!!\n", __func__);
		goto host_put;
	}

	do {
		if (!sg_miter_next(sg_miter))
			goto done;

		host->sg = sg_miter->piter.sg;
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
			data->bytes_xfered += len;
			offset += len;
			remain -= len;
		} while (remain);

		sg_miter->consumed = offset;
		status = mci_readl(host, MINTSTS);
		mci_writel(host, RINTSTS, SDMMC_INT_TXDR);
	} while (status & SDMMC_INT_TXDR); /* if TXDR write again */

	if (!remain) {
		if (!sg_miter_next(sg_miter))
			goto done;
		sg_miter->consumed = 0;
	}
	sg_miter_stop(sg_miter);
	return;

done:
	sg_miter_stop(sg_miter);
host_put:	
	host->sg = NULL;
	smp_wmb();
	set_bit(EVENT_XFER_COMPLETE, &host->pending_events);
}

static void dw_mci_cmd_interrupt(struct dw_mci *host, u32 status)
{
        u32 multi, unit;

        if (!host->cmd_status)
	    host->cmd_status = status;
	    
	if(!host->cmd)
		goto cmd_exit;

	if((MMC_STOP_TRANSMISSION != host->cmd->opcode))
        {
                unit = 2*1024*1024;
                multi = mci_readl(host, BYTCNT)/unit;
                multi += ((mci_readl(host, BYTCNT) % unit) ? 1 :0 );
                multi = (multi > 0) ? multi : 1;
                multi += (host->cmd->retries > 2)? 2 : host->cmd->retries;
                mod_timer(&host->dto_timer, jiffies + msecs_to_jiffies(4500 * multi));//max wait 8s larger
        }

cmd_exit:
        smp_wmb();
        set_bit(EVENT_CMD_COMPLETE, &host->pending_events);
	tasklet_schedule(&host->tasklet);
}

static irqreturn_t dw_mci_interrupt(int irq, void *dev_id)
{
	struct dw_mci *host = dev_id;
	u32 pending, sdio_int;
	int i;

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

	if (pending) {
		if (pending & DW_MCI_CMD_ERROR_FLAGS) {
			mci_writel(host, RINTSTS, DW_MCI_CMD_ERROR_FLAGS);
			host->cmd_status = pending;
			smp_wmb();
			MMC_DBG_INFO_FUNC(host->mmc,"Line%d..%s cmd_status INT=0x%x,[%s]",
                                          __LINE__, __FUNCTION__,host->cmd_status,mmc_hostname(host->mmc));
            
			set_bit(EVENT_CMD_COMPLETE, &host->pending_events);
		}

		if (pending & DW_MCI_DATA_ERROR_FLAGS) {
			/* if there is an error report DATA_ERROR */
			mci_writel(host, RINTSTS, DW_MCI_DATA_ERROR_FLAGS);
			host->data_status = pending;
			smp_wmb();
			set_bit(EVENT_DATA_ERROR, &host->pending_events);

			MMC_DBG_INFO_FUNC(host->mmc,"Line%d..%s data_status INT=0x%x,[%s]",
                                          __LINE__, __FUNCTION__,host->data_status,mmc_hostname(host->mmc));
			tasklet_schedule(&host->tasklet);
		}

		if (pending & SDMMC_INT_DATA_OVER) {
			mci_writel(host, RINTSTS, SDMMC_INT_DATA_OVER);
			del_timer(&host->dto_timer); /* delete the timer for INT_DTO */
			MMC_DBG_CMD_FUNC(host->mmc, "SDMMC_INT_DATA_OVER, INT-pending=0x%x. [%s]",pending,mmc_hostname(host->mmc));
			if (!host->data_status)
				host->data_status = pending;
			smp_wmb();
			if (host->dir_status == DW_MCI_RECV_STATUS) {
				if (host->sg != NULL)
					dw_mci_read_data_pio(host, true);
			}
			set_bit(EVENT_DATA_COMPLETE, &host->pending_events);
			tasklet_schedule(&host->tasklet);
		}

		if (pending & SDMMC_INT_RXDR) {
			mci_writel(host, RINTSTS, SDMMC_INT_RXDR);
			if (host->dir_status == DW_MCI_RECV_STATUS && host->sg)
				dw_mci_read_data_pio(host, false);
		}

		if (pending & SDMMC_INT_TXDR) {
			mci_writel(host, RINTSTS, SDMMC_INT_TXDR);
			if (host->dir_status == DW_MCI_SEND_STATUS && host->sg)
				dw_mci_write_data_pio(host);
		}

		if (pending & SDMMC_INT_VSI) {
    		        MMC_DBG_SW_VOL_FUNC(host->mmc, "SDMMC_INT_VSI, INT-pending=0x%x. [%s]",pending,mmc_hostname(host->mmc));
			mci_writel(host, RINTSTS, SDMMC_INT_VSI);
			dw_mci_cmd_interrupt(host, pending);
		}

		if (pending & SDMMC_INT_CMD_DONE) {
    			MMC_DBG_CMD_FUNC(host->mmc, "SDMMC_INT_CMD_DONE, CMD = 0x%x, INT-pending=0x%x. [%s]",mci_readl(host, CMD),pending,mmc_hostname(host->mmc));
			mci_writel(host, RINTSTS, SDMMC_INT_CMD_DONE);
			dw_mci_cmd_interrupt(host, pending);
		}

		if (pending & SDMMC_INT_CD) {
			mci_writel(host, RINTSTS, SDMMC_INT_CD);
			MMC_DBG_INFO_FUNC(host->mmc, "SDMMC_INT_CD, INT-pending=0x%x. [%s]", pending, mmc_hostname(host->mmc)); 
			wake_lock_timeout(&host->mmc->detect_wake_lock, 5 * HZ);
			queue_work(host->card_workqueue, &host->card_work);
		}
		
		if (pending & SDMMC_INT_HLE) {
			mci_writel(host, RINTSTS, SDMMC_INT_HLE);
			MMC_DBG_CMD_FUNC(host->mmc, "SDMMC_INT_HLE INT-pending=0x%x. [%s]\n",pending,mmc_hostname(host->mmc));
			
		}

		/* Handle SDIO Interrupts */
		for (i = 0; i < host->num_slots; i++) {
			struct dw_mci_slot *slot = host->slot[i];

			if (host->verid < DW_MMC_240A)
				sdio_int = SDMMC_INT_SDIO(i);
			else
				sdio_int = SDMMC_INT_SDIO(i + 8);
    			
			if (pending & sdio_int) {
				mci_writel(host, RINTSTS, sdio_int);
				mmc_signal_sdio_irq(slot->mmc);
			}
		}

	}

#ifdef CONFIG_MMC_DW_IDMAC
	/* Handle DMA interrupts */
	pending = mci_readl(host, IDSTS);
	if (pending & (SDMMC_IDMAC_INT_TI | SDMMC_IDMAC_INT_RI)) {
		mci_writel(host, IDSTS, SDMMC_IDMAC_INT_TI | SDMMC_IDMAC_INT_RI);
		mci_writel(host, IDSTS, SDMMC_IDMAC_INT_NI);
		host->dma_ops->complete((void *)host);
	}
#endif

	return IRQ_HANDLED;
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
			MMC_DBG_BOOT_FUNC(mmc, "  The card is %s.  ===!!!!!!==[%s]\n",
				present ? "inserted" : "removed.", mmc_hostname(mmc));
	
			rk_send_wakeup_key();//wake up system
			spin_lock_bh(&host->lock);

                        del_timer(&host->dto_timer); /* delete the timer for INT_DTO */
			/* Card change detected */
			slot->last_detect_state = present;

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
						if (!mrq->stop)
							break;
						/* fall through */
					case STATE_SENDING_STOP:
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
						
                                        MMC_DBG_CMD_FUNC(host->mmc, "dw_mci_work--reqeuest done, cmd=%d [%s]",
                                                        mrq->cmd->opcode, mmc_hostname(mmc));

					spin_unlock(&host->lock);
					mmc_request_done(slot->mmc, mrq);
					spin_lock(&host->lock);
				}
			}

			/* Power down slot */
			if (present == 0) {
				/* Clear down the FIFO */
				dw_mci_fifo_reset(host);
#ifdef CONFIG_MMC_DW_IDMAC
				dw_mci_idmac_reset(host);
#endif

			}

			spin_unlock_bh(&host->lock);

			present = dw_mci_get_cd(mmc);
		}

		mmc_detect_change(slot->mmc,
                msecs_to_jiffies(host->pdata->detect_delay_ms));
	}
}

#ifdef CONFIG_OF
/* given a slot id, find out the device node representing that slot */
static struct device_node *dw_mci_of_find_slot_node(struct device *dev, u8 slot)
{
	struct device_node *np;
	const __be32 *addr;
	int len;

	if (!dev || !dev->of_node)
		return NULL;

	for_each_child_of_node(dev->of_node, np) {
		addr = of_get_property(np, "reg", &len);
		if (!addr || (len < sizeof(int)))
			continue;
		if (be32_to_cpup(addr) == slot)
			return np;
	}
	return NULL;
}

static struct dw_mci_of_slot_quirks {
	char *quirk;
	int id;
} of_slot_quirks[] = {
	{
		.quirk	= "disable-wp",
		.id	= DW_MCI_SLOT_QUIRK_NO_WRITE_PROTECT,
	},
};

static int dw_mci_of_get_slot_quirks(struct device *dev, u8 slot)
{
	struct device_node *np = dw_mci_of_find_slot_node(dev, slot);
	int quirks = 0;
	int idx;

	/* get quirks */
	for (idx = 0; idx < ARRAY_SIZE(of_slot_quirks); idx++)
		if (of_get_property(np, of_slot_quirks[idx].quirk, NULL))
			quirks |= of_slot_quirks[idx].id;

	return quirks;
}

/* find out bus-width for a given slot */
static u32 dw_mci_of_get_bus_wd(struct device *dev, u8 slot)
{
	struct device_node *np = dev->of_node;//dw_mci_of_find_slot_node(dev, slot);
	u32 bus_wd = 1;

	if (!np)
		return 1;

	if (of_property_read_u32(np, "bus-width", &bus_wd))
		dev_err(dev, "bus-width property not found, assuming width"
			       " as 1\n");
	return bus_wd;
}


/* find the pwr-en gpio for a given slot; or -1 if none specified */
static int dw_mci_of_get_pwr_en_gpio(struct device *dev, u8 slot)
{
	struct device_node *np = dev->of_node;//dw_mci_of_find_slot_node(dev, slot);
	int gpio;

	if (!np)
		return -EINVAL;

	gpio = of_get_named_gpio(np, "pwr-gpios", 0);

	/* Having a missing entry is valid; return silently */
	if (!gpio_is_valid(gpio))
		return -EINVAL;

	if (devm_gpio_request(dev, gpio, "dw-mci-pwr_en")) {
		dev_warn(dev, "gpio [%d] request failed\n", gpio);
		return -EINVAL;
	}

    gpio_direction_output(gpio, 0);//set 0 to pwr-en

	return gpio;
}


/* find the write protect gpio for a given slot; or -1 if none specified */
static int dw_mci_of_get_wp_gpio(struct device *dev, u8 slot)
{
	struct device_node *np = dw_mci_of_find_slot_node(dev, slot);
	int gpio;

	if (!np)
		return -EINVAL;

	gpio = of_get_named_gpio(np, "wp-gpios", 0);

	/* Having a missing entry is valid; return silently */
	if (!gpio_is_valid(gpio))
		return -EINVAL;

	if (devm_gpio_request(dev, gpio, "dw-mci-wp")) {
		dev_warn(dev, "gpio [%d] request failed\n", gpio);
		return -EINVAL;
	}

	return gpio;
}

/* find the cd gpio for a given slot */
static void dw_mci_of_get_cd_gpio(struct device *dev, u8 slot,
					struct mmc_host *mmc)
{
	struct device_node *np = dev->of_node;//dw_mci_of_find_slot_node(dev, slot);
	int gpio;

	if (!np)
		return;

	gpio = of_get_named_gpio(np, "cd-gpios", 0);

	/* Having a missing entry is valid; return silently */
	if (!gpio_is_valid(gpio))
		return;

	if (mmc_gpio_request_cd(mmc, gpio, 0))
		dev_warn(dev, "gpio [%d] request failed\n", gpio);
}
#else /* CONFIG_OF */
static int dw_mci_of_get_slot_quirks(struct device *dev, u8 slot)
{
	return 0;
}
static u32 dw_mci_of_get_bus_wd(struct device *dev, u8 slot)
{
	return 1;
}
static struct device_node *dw_mci_of_find_slot_node(struct device *dev, u8 slot)
{
	return NULL;
}
static int dw_mci_of_get_wp_gpio(struct device *dev, u8 slot)
{
	return -EINVAL;
}
static void dw_mci_of_get_cd_gpio(struct device *dev, u8 slot,
					struct mmc_host *mmc)
{
	return;
}
#endif /* CONFIG_OF */

static int dw_mci_init_slot(struct dw_mci *host, unsigned int id)
{
	struct mmc_host *mmc;
	struct dw_mci_slot *slot;
	const struct dw_mci_drv_data *drv_data = host->drv_data;
	int ctrl_id, ret;
	u32 freq[2];
	u8 bus_width;

	mmc = mmc_alloc_host(sizeof(struct dw_mci_slot), host->dev);
	if (!mmc)
		return -ENOMEM;

	slot = mmc_priv(mmc);
	slot->id = id;
	slot->mmc = mmc;
	slot->host = host;
	host->slot[id] = slot;
	host->mmc = mmc;

	slot->quirks = dw_mci_of_get_slot_quirks(host->dev, slot->id);

	mmc->ops = &dw_mci_ops;

	if (of_property_read_u32_array(host->dev->of_node,
				       "clock-freq-min-max", freq, 2)) {
		mmc->f_min = DW_MCI_FREQ_MIN;
		mmc->f_max = DW_MCI_FREQ_MAX;
		
        printk("%d..%s: fmin=%d, fmax=%d [%s]\n", __LINE__,__FUNCTION__,
                mmc->f_min, mmc->f_max, mmc_hostname(mmc));    
	} else {
		mmc->f_min = freq[0];
		mmc->f_max = freq[1];
		
        printk("%d..%s: fmin=%d, fmax=%d [%s]\n", __LINE__,__FUNCTION__,
                mmc->f_min, mmc->f_max,  mmc_hostname(mmc));    
	}
	
    if(strstr("mmc0",mmc_hostname(mmc)))
        printk("Line%d..%s: The rk_sdmmc %s",__LINE__, __FUNCTION__,RK_SDMMC_DRIVER_VERSION);
        
	if (of_find_property(host->dev->of_node, "supports-sd", NULL))
		mmc->restrict_caps |= RESTRICT_CARD_TYPE_SD;	
	if (of_find_property(host->dev->of_node, "supports-sdio", NULL))
		mmc->restrict_caps |= RESTRICT_CARD_TYPE_SDIO;	
	if (of_find_property(host->dev->of_node, "supports-emmc", NULL))
		mmc->restrict_caps |= RESTRICT_CARD_TYPE_EMMC;

	if (host->pdata->get_ocr)
		mmc->ocr_avail = host->pdata->get_ocr(id);
	else
	{
		mmc->ocr_avail =  MMC_VDD_27_28| MMC_VDD_28_29| MMC_VDD_29_30| MMC_VDD_30_31
                        | MMC_VDD_31_32| MMC_VDD_32_33| MMC_VDD_33_34| MMC_VDD_34_35| MMC_VDD_35_36
                        | MMC_VDD_26_27| MMC_VDD_25_26| MMC_VDD_24_25| MMC_VDD_23_24
                        | MMC_VDD_22_23| MMC_VDD_21_22| MMC_VDD_20_21| MMC_VDD_165_195;
	}

	/*
	 * Start with slot power disabled, it will be enabled when a card
	 * is detected.
	 */
	if (host->pdata->setpower)
		host->pdata->setpower(id, 0);

	if (host->pdata->caps)
		mmc->caps = host->pdata->caps;

	if (host->pdata->pm_caps)
		mmc->pm_caps = host->pdata->pm_caps;

	if (host->dev->of_node) {
		ctrl_id = of_alias_get_id(host->dev->of_node, "mshc");
		if (ctrl_id < 0)
			ctrl_id = 0;
	} else {
		ctrl_id = to_platform_device(host->dev)->id;
	}
	if (drv_data && drv_data->caps)
		mmc->caps |= drv_data->caps[ctrl_id];
	if (drv_data && drv_data->hold_reg_flag)
		mmc->hold_reg_flag |= drv_data->hold_reg_flag[ctrl_id];		

	/* set the compatibility of driver. */
	mmc->caps |= MMC_CAP_UHS_SDR12 | MMC_CAP_UHS_SDR25 | MMC_CAP_UHS_SDR50 
	                | MMC_CAP_UHS_SDR104 | MMC_CAP_ERASE ;

	if (host->pdata->caps2)
		mmc->caps2 = host->pdata->caps2;

	if (host->pdata->get_bus_wd)
		bus_width = host->pdata->get_bus_wd(slot->id);
	else if (host->dev->of_node)
		bus_width = dw_mci_of_get_bus_wd(host->dev, slot->id);
	else
		bus_width = 1;

	switch (bus_width) {
	        case 8:
		        mmc->caps |= MMC_CAP_8_BIT_DATA;
	        case 4:
		        mmc->caps |= MMC_CAP_4_BIT_DATA;
	}
	
	if (of_find_property(host->dev->of_node, "cap-power-off-card", NULL))
                mmc->caps |= MMC_CAP_POWER_OFF_CARD;
	if (of_find_property(host->dev->of_node, "cap-sdio-irq", NULL))
		mmc->caps |= MMC_CAP_SDIO_IRQ;
	if (of_find_property(host->dev->of_node, "poll-hw-reset", NULL))
		mmc->caps |= MMC_CAP_HW_RESET;
	if (of_find_property(host->dev->of_node, "full-pwr-cycle", NULL))
		mmc->caps2 |= MMC_CAP2_FULL_PWR_CYCLE;
	if (of_find_property(host->dev->of_node, "keep-power-in-suspend", NULL))
		mmc->pm_caps |= MMC_PM_KEEP_POWER;
	if (of_find_property(host->dev->of_node, "ignore-pm-notify", NULL))
                mmc->pm_caps |= MMC_PM_IGNORE_PM_NOTIFY;
	if (of_find_property(host->dev->of_node, "enable-sdio-wakeup", NULL))
		mmc->pm_caps |= MMC_PM_WAKE_SDIO_IRQ;

        /*Assign pm_caps pass to pm_flags*/
        mmc->pm_flags = mmc->pm_caps;

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
		mmc->max_blk_count = host->ring_size;
		mmc->max_seg_size = 0x1000;
		mmc->max_req_size = mmc->max_seg_size * mmc->max_blk_count;
#else
		mmc->max_segs = 64;
		mmc->max_blk_size = 65536; /* BLKSIZ is 16 bits */
		mmc->max_blk_count = 512;
		mmc->max_req_size = mmc->max_blk_size * mmc->max_blk_count;
		mmc->max_seg_size = mmc->max_req_size;
#endif /* CONFIG_MMC_DW_IDMAC */
	}
        /* pwr_en */   
        slot->pwr_en_gpio = dw_mci_of_get_pwr_en_gpio(host->dev, slot->id);

        if (!(mmc->restrict_caps & RESTRICT_CARD_TYPE_SD))
        {
                host->vmmc = NULL;
        }else{

                if(mmc->restrict_caps & RESTRICT_CARD_TYPE_SD)
                        host->vmmc = devm_regulator_get(mmc_dev(mmc), "vmmc");
                else
                        host->vmmc = NULL;
    	 
                if (IS_ERR(host->vmmc)) {
    		        pr_info("%s: no vmmc regulator found\n", mmc_hostname(mmc));
    		        host->vmmc = NULL;
    	        }else{
                        ret = regulator_enable(host->vmmc);
                        if (ret) {
    			        dev_err(host->dev,
    				        "failed to enable regulator: %d\n", ret);
    		                host->vmmc = NULL;
    			        goto err_setup_bus;
                        }
                }
        }
    
        slot->wp_gpio = dw_mci_of_get_wp_gpio(host->dev, slot->id);
	
        if (mmc->restrict_caps & RESTRICT_CARD_TYPE_SDIO)
                clear_bit(DW_MMC_CARD_PRESENT, &slot->flags);

        ret = mmc_add_host(mmc);
        if (ret)
                goto err_setup_bus;

        /* Pinctrl set default iomux state to fucntion port.
         * Fixme: DON'T TOUCH EMMC SETTING!
         */
        if(!(host->mmc->restrict_caps & RESTRICT_CARD_TYPE_EMMC))
        {
                host->pinctrl = devm_pinctrl_get(host->dev);
                if(IS_ERR(host->pinctrl)){
                        printk("%s: Warning : No pinctrl used!\n",mmc_hostname(host->mmc));
                }else{
                        host->pins_idle= pinctrl_lookup_state(host->pinctrl,PINCTRL_STATE_IDLE);
                        if(IS_ERR(host->pins_default)){
                                printk("%s: Warning : No IDLE pinctrl matched!\n", mmc_hostname(host->mmc));
                        }
                        else
                        { 
                                if(pinctrl_select_state(host->pinctrl, host->pins_idle) < 0)
                                        printk("%s: Warning :  Idle pinctrl setting failed!\n", mmc_hostname(host->mmc));  
                        }
        
                        host->pins_default = pinctrl_lookup_state(host->pinctrl,PINCTRL_STATE_DEFAULT);
                        if(IS_ERR(host->pins_default)){
                                printk("%s: Warning : No default pinctrl matched!\n", mmc_hostname(host->mmc));
                        }
                        else
                        {
                                if(pinctrl_select_state(host->pinctrl, host->pins_default) < 0)
                                        printk("%s: Warning :  Default pinctrl setting failed!\n", mmc_hostname(host->mmc));
                        }
                }
        }
    
    
#if defined(CONFIG_DEBUG_FS)
	dw_mci_init_debugfs(slot);
#endif

	/* Card initially undetected */
	slot->last_detect_state = 1;

	return 0;

err_setup_bus:
	mmc_free_host(mmc);
	return -EINVAL;
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
	/* Alloc memory for sg translation */
	host->sg_cpu = dmam_alloc_coherent(host->dev, PAGE_SIZE,
					  &host->sg_dma, GFP_KERNEL);
	if (!host->sg_cpu) {
		dev_err(host->dev, "%s: could not alloc DMA memory\n",
			__func__);
		goto no_dma;
	}

	/* Determine which DMA interface to use */
#if defined(CONFIG_MMC_DW_IDMAC)
	host->dma_ops = &dw_mci_idmac_ops;
	dev_info(host->dev, "Using internal DMA controller.\n");
#elif defined(CONFIG_MMC_DW_EDMAC)
        host->dma_ops = &dw_mci_edmac_ops;
        dev_info(host->dev, "Using external DMA controller.\n");
#endif

	if (!host->dma_ops)
		goto no_dma;

	if (host->dma_ops->init && host->dma_ops->start &&
	    host->dma_ops->stop && host->dma_ops->cleanup) {
		if (host->dma_ops->init(host)) {
			dev_err(host->dev, "%s: Unable to initialize "
				"DMA Controller.\n", __func__);
			goto no_dma;
		}
	} else {
		dev_err(host->dev, "DMA initialization not found.\n");
		goto no_dma;
	}

	host->use_dma = 1;
	return;

no_dma:
	dev_info(host->dev, "Using PIO mode.\n");
	host->use_dma = 0;
	return;
}

static bool dw_mci_ctrl_reset(struct dw_mci *host, u32 reset)
{
	unsigned long timeout = jiffies + msecs_to_jiffies(500);
	u32 ctrl;

	ctrl = mci_readl(host, CTRL);
	ctrl |= reset;
	mci_writel(host, CTRL, ctrl);

	/* wait till resets clear */
	do {
		ctrl = mci_readl(host, CTRL);
		if (!(ctrl & reset))
		        return true;
	} while (time_before(jiffies, timeout));

	dev_err(host->dev,
		"Timeout resetting block (ctrl reset %#x)\n",
		ctrl & reset);
		
	return false;
}

static inline bool dw_mci_fifo_reset(struct dw_mci *host)
{
	/*
	 * Reseting generates a block interrupt, hence setting
	 * the scatter-gather pointer to NULL.
	 */
	if (host->sg) {
		sg_miter_stop(&host->sg_miter);
		host->sg = NULL;
	}

	return dw_mci_ctrl_reset(host, SDMMC_CTRL_FIFO_RESET);
}

static inline bool dw_mci_ctrl_all_reset(struct dw_mci *host)
{
	return dw_mci_ctrl_reset(host,
				 SDMMC_CTRL_FIFO_RESET |
				 SDMMC_CTRL_RESET |
				 SDMMC_CTRL_DMA_RESET);
}

#ifdef CONFIG_OF
/*
static struct dw_mci_of_quirks {
	char *quirk;
	int id;
} of_quirks[] = {
	{
		.quirk	= "broken-cd",
		.id	= DW_MCI_QUIRK_BROKEN_CARD_DETECTION,
	},
};
*/
static struct dw_mci_board *dw_mci_parse_dt(struct dw_mci *host)
{
	struct dw_mci_board *pdata;
	struct device *dev = host->dev;
	struct device_node *np = dev->of_node;
	const struct dw_mci_drv_data *drv_data = host->drv_data;
	int  ret;
	u32 clock_frequency;

	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		dev_err(dev, "could not allocate memory for pdata\n");
		return ERR_PTR(-ENOMEM);
	}

	/* find out number of slots supported */
	if (of_property_read_u32(dev->of_node, "num-slots",
				&pdata->num_slots)) {
		dev_info(dev, "num-slots property not found, "
				"assuming 1 slot is available\n");
		pdata->num_slots = 1;
	}
#if 0
	/* get quirks */
	for (idx = 0; idx < ARRAY_SIZE(of_quirks); idx++)
		if (of_get_property(np, of_quirks[idx].quirk, NULL))
			pdata->quirks |= of_quirks[idx].id;
#endif

	if (of_property_read_u32(np, "fifo-depth", &pdata->fifo_depth))
		dev_info(dev, "fifo-depth property not found, using "
				"value of FIFOTH register as default\n");

	of_property_read_u32(np, "card-detect-delay", &pdata->detect_delay_ms);

	if (!of_property_read_u32(np, "clock-frequency", &clock_frequency))
		pdata->bus_hz = clock_frequency;

	if (drv_data && drv_data->parse_dt) {
		ret = drv_data->parse_dt(host);
		if (ret)
			return ERR_PTR(ret);
	}

	if (of_find_property(np, "keep-power-in-suspend", NULL))
		pdata->pm_caps |= MMC_PM_KEEP_POWER;
		
	if (of_find_property(np, "enable-sdio-wakeup", NULL))
		pdata->pm_caps |= MMC_PM_WAKE_SDIO_IRQ;

	if (of_find_property(np, "supports-highspeed", NULL))
		pdata->caps |= MMC_CAP_SD_HIGHSPEED | MMC_CAP_MMC_HIGHSPEED;
		
    if (of_find_property(np, "supports-UHS_SDR104", NULL))
		pdata->caps |= MMC_CAP_UHS_SDR104 | MMC_CAP_UHS_SDR50;

    if (of_find_property(np, "supports-DDR_MODE", NULL))
		pdata->caps |= MMC_CAP_1_8V_DDR | MMC_CAP_1_2V_DDR;

    if (of_find_property(np, "caps2-mmc-hs200", NULL))
		pdata->caps2 |= MMC_CAP2_HS200;

	if (of_find_property(np, "caps2-mmc-hs200-1_8v", NULL))
		pdata->caps2 |= MMC_CAP2_HS200_1_8V_SDR;

	if (of_find_property(np, "caps2-mmc-hs200-1_2v", NULL))
		pdata->caps2 |= MMC_CAP2_HS200_1_2V_SDR;

	if (of_get_property(np, "cd-inverted", NULL))
		pdata->caps2 |= MMC_CAP2_CD_ACTIVE_HIGH;
	if (of_get_property(np, "bootpart-no-access", NULL))
		pdata->caps2 |= MMC_CAP2_BOOTPART_NOACC;	

	return pdata;
}

#else /* CONFIG_OF */
static struct dw_mci_board *dw_mci_parse_dt(struct dw_mci *host)
{
	return ERR_PTR(-EINVAL);
}
#endif /* CONFIG_OF */

static void dw_mci_dealwith_timeout(struct dw_mci *host)
{
        u32 ret, i, regs;

        switch(host->state){
                case STATE_IDLE:
                        break;
                case STATE_SENDING_DATA:
                case STATE_DATA_BUSY:
		        host->data_status |= (SDMMC_INT_DCRC|SDMMC_INT_EBE);
		        mci_writel(host, RINTSTS, SDMMC_INT_DRTO);  // clear interrupt
                        set_bit(EVENT_DATA_COMPLETE, &host->pending_events);
                        host->state = STATE_DATA_BUSY;
	                if (!dw_mci_ctrl_all_reset(host)) {
                                ret = -ENODEV;
                                return ;
                        }

                        /* NO requirement to reclaim slave chn using external dmac */
                        #ifdef CONFIG_MMC_DW_IDMAC
                        if (host->use_dma && host->dma_ops->init)
	                        host->dma_ops->init(host);
                        #endif

                        /*
                         * Restore the initial value at FIFOTH register
                         * And Invalidate the prev_blksz with zero
                         */
                        mci_writel(host, FIFOTH, host->fifoth_val);
                        host->prev_blksz = 0;
                        mci_writel(host, TMOUT, 0xFFFFFFFF);
                        mci_writel(host, RINTSTS, 0xFFFFFFFF);
                        regs = SDMMC_INT_CMD_DONE | SDMMC_INT_DATA_OVER | SDMMC_INT_TXDR 
                                        | SDMMC_INT_RXDR | SDMMC_INT_VSI | DW_MCI_ERROR_FLAGS;
                        if(!(host->mmc->restrict_caps & RESTRICT_CARD_TYPE_SDIO))
                                regs |= SDMMC_INT_CD;
                                mci_writel(host, INTMASK, regs);
                        mci_writel(host, CTRL, SDMMC_CTRL_INT_ENABLE);
                        for (i = 0; i < host->num_slots; i++) {
                                struct dw_mci_slot *slot = host->slot[i];
                                if (!slot)
                                        continue;
                                if (slot->mmc->pm_flags & MMC_PM_KEEP_POWER) {
                                        dw_mci_set_ios(slot->mmc, &slot->mmc->ios);
                                        dw_mci_setup_bus(slot, true);
                                }
                        }
                        mci_writel(host, RINTSTS, 0xFFFFFFFF);
                        tasklet_schedule(&host->tasklet);
                        break;
                default:
                        break;
    }
}
static void dw_mci_dto_timeout(unsigned long host_data)
{
	struct dw_mci *host = (struct dw_mci *) host_data;

	disable_irq(host->irq);

	host->data_status = SDMMC_INT_EBE;
	mci_writel(host, RINTSTS, 0xFFFFFFFF);
	dw_mci_dealwith_timeout(host);

	enable_irq(host->irq);
}
int dw_mci_probe(struct dw_mci *host)
{
	const struct dw_mci_drv_data *drv_data = host->drv_data;
	int width, i, ret = 0;
	u32 fifo_size;
	int init_slots = 0;
	u32 regs;

	if (!host->pdata) {
		host->pdata = dw_mci_parse_dt(host);
		if (IS_ERR(host->pdata)) {
			dev_err(host->dev, "platform data not available\n");
			return -EINVAL;
		}
	}

	if (!host->pdata->select_slot && host->pdata->num_slots > 1) {
		dev_err(host->dev,
			"Platform data must supply select_slot function\n");
		return -ENODEV;
	}

	/*
	 * In 2.40a spec, Data offset is changed.
	 * Need to check the version-id and set data-offset for DATA register.
	 */
	host->verid = SDMMC_GET_VERID(mci_readl(host, VERID));
	dev_info(host->dev, "Version ID is %04x\n", host->verid);

	if (host->verid < DW_MMC_240A)
		host->data_offset = DATA_OFFSET;
	else
		host->data_offset = DATA_240A_OFFSET;

        //hclk enable
        host->hclk_mmc= devm_clk_get(host->dev, "hclk_mmc");
        if (IS_ERR(host->hclk_mmc)) {
                dev_err(host->dev, "failed to get hclk_mmc\n");
                ret = PTR_ERR(host->hclk_mmc);
                goto err_hclk_mmc;
        }

        clk_prepare_enable(host->hclk_mmc);

        //mmc clk enable
        host->clk_mmc = devm_clk_get(host->dev, "clk_mmc");
        if (IS_ERR(host->clk_mmc)) {
                dev_err(host->dev, "failed to get clk mmc_per\n");
                ret = PTR_ERR(host->clk_mmc);
                goto err_clk_mmc;
        }
    
        host->bus_hz = host->pdata->bus_hz;   
        if (!host->bus_hz) {
                dev_err(host->dev,"Platform data must supply bus speed\n");
                ret = -ENODEV;
                goto err_clk_mmc;
        }

        if (host->verid < DW_MMC_240A)
	        ret = clk_set_rate(host->clk_mmc, host->bus_hz);
	else
                //rockchip: fix divider 2 in clksum before controlller
	        ret = clk_set_rate(host->clk_mmc, host->bus_hz * 2);
	        
	if(ret < 0) {
	    dev_err(host->dev, "failed to set clk mmc\n");
	    goto err_clk_mmc;
	}
        clk_prepare_enable(host->clk_mmc);

	if (drv_data && drv_data->setup_clock) {
        ret = drv_data->setup_clock(host);
        if (ret) {
            dev_err(host->dev,
                "implementation specific clock setup failed\n");
            goto err_clk_mmc;
        }
    }

        host->quirks = host->pdata->quirks;
        host->irq_state = true;
        host->set_speed = 0;
        host->set_div = 0;
        host->svi_flags = 0;

	spin_lock_init(&host->lock);
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
	if (!dw_mci_ctrl_all_reset(host))
		return -ENODEV;

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
	host->fifoth_val =
		SDMMC_SET_FIFOTH(0x2, fifo_size / 2 - 1, fifo_size / 2);
	mci_writel(host, FIFOTH, host->fifoth_val);

	/* disable clock to CIU */
	mci_writel(host, CLKENA, 0);
	mci_writel(host, CLKSRC, 0);

	tasklet_init(&host->tasklet, dw_mci_tasklet_func, (unsigned long)host);
	host->card_workqueue = alloc_workqueue("dw-mci-card",
			WQ_MEM_RECLAIM | WQ_NON_REENTRANT, 1);
	if (!host->card_workqueue) {
		ret = -ENOMEM;
		goto err_dmaunmap;
	}
	INIT_WORK(&host->card_work, dw_mci_work_routine_card);
	ret = devm_request_irq(host->dev, host->irq, dw_mci_interrupt,
			       host->irq_flags, "dw-mci", host);
	if (ret)
		goto err_workqueue;

	if (host->pdata->num_slots)
		host->num_slots = host->pdata->num_slots;
	else
		host->num_slots = ((mci_readl(host, HCON) >> 1) & 0x1F) + 1;

        setup_timer(&host->dto_timer, dw_mci_dto_timeout, (unsigned long)host);
	/* We need at least one slot to succeed */
	for (i = 0; i < host->num_slots; i++) {
		ret = dw_mci_init_slot(host, i);
		if (ret)
			dev_dbg(host->dev, "slot %d init failed\n", i);
		else
			init_slots++;
	}
	
	/*
	 * Enable interrupts for command done, data over, data empty, card det,
	 * receive ready and error such as transmit, receive timeout, crc error
	 */
	mci_writel(host, RINTSTS, 0xFFFFFFFF);
	regs = SDMMC_INT_CMD_DONE | SDMMC_INT_DATA_OVER | SDMMC_INT_TXDR | SDMMC_INT_VSI |
	       SDMMC_INT_RXDR | DW_MCI_ERROR_FLAGS;
	if(!(host->mmc->restrict_caps & RESTRICT_CARD_TYPE_SDIO) 
	    && !(host->mmc->restrict_caps & RESTRICT_CARD_TYPE_EMMC))
	    regs |= SDMMC_INT_CD; 

	mci_writel(host, INTMASK, regs);

	mci_writel(host, CTRL, SDMMC_CTRL_INT_ENABLE); /* Enable mci interrupt */
	
	dev_info(host->dev, "DW MMC controller at irq %d, "
		 "%d bit host data width, "
		 "%u deep fifo\n",
		 host->irq, width, fifo_size);

	if (init_slots) {
		dev_info(host->dev, "%d slots initialized\n", init_slots);
	} else {
		dev_dbg(host->dev, "attempted to initialize %d slots, "
					"but failed on all\n", host->num_slots);
		goto err_workqueue;
	}


	if (host->quirks & DW_MCI_QUIRK_IDMAC_DTO)
		dev_info(host->dev, "Internal DMAC interrupt fix enabled.\n");

	return 0;

err_workqueue:
	destroy_workqueue(host->card_workqueue);

err_dmaunmap:
	if (host->use_dma && host->dma_ops->exit)
		host->dma_ops->exit(host);

	if (host->vmmc){
		regulator_disable(host->vmmc);
		regulator_put(host->vmmc);
	}

err_clk_mmc:
    if (!IS_ERR(host->clk_mmc))
		clk_disable_unprepare(host->clk_mmc);
err_hclk_mmc:
    if (!IS_ERR(host->hclk_mmc))
		clk_disable_unprepare(host->hclk_mmc);
		
	return ret;
}
EXPORT_SYMBOL(dw_mci_probe);

void dw_mci_remove(struct dw_mci *host)
{
	int i;
	del_timer_sync(&host->dto_timer);

        mci_writel(host, RINTSTS, 0xFFFFFFFF);
        mci_writel(host, INTMASK, 0); /* disable all mmc interrupt first */

        for(i = 0; i < host->num_slots; i++){
                dev_dbg(host->dev, "remove slot %d\n", i);
                if(host->slot[i])
                        dw_mci_cleanup_slot(host->slot[i], i);
        }

        /* disable clock to CIU */
        mci_writel(host, CLKENA, 0);
        mci_writel(host, CLKSRC, 0);

        destroy_workqueue(host->card_workqueue);

        if(host->use_dma && host->dma_ops->exit)
                host->dma_ops->exit(host);

        if(host->vmmc){
                regulator_disable(host->vmmc);
                regulator_put(host->vmmc);
        }
	if(!IS_ERR(host->clk_mmc))
                clk_disable_unprepare(host->clk_mmc);

        if(!IS_ERR(host->hclk_mmc))
                clk_disable_unprepare(host->hclk_mmc);
}
EXPORT_SYMBOL(dw_mci_remove);



#ifdef CONFIG_PM_SLEEP
/*
 * TODO: we should probably disable the clock to the card in the suspend path.
 */
int dw_mci_suspend(struct dw_mci *host)
{
        if(host->vmmc)
                regulator_disable(host->vmmc);

        if(host->use_dma && host->dma_ops->exit)
                host->dma_ops->exit(host);

        /*only for sdmmc controller*/
        if(host->mmc->restrict_caps & RESTRICT_CARD_TYPE_SD){
                host->mmc->rescan_disable = 1;
                if(cancel_delayed_work_sync(&host->mmc->detect))
			wake_unlock(&host->mmc->detect_wake_lock);

                disable_irq(host->irq);
                if(pinctrl_select_state(host->pinctrl, host->pins_idle) < 0)
                        MMC_DBG_ERR_FUNC(host->mmc, "Idle pinctrl setting failed! [%s]",
                                                mmc_hostname(host->mmc));
                dw_mci_of_get_cd_gpio(host->dev,0,host->mmc);
                mci_writel(host, RINTSTS, 0xFFFFFFFF);
                mci_writel(host, INTMASK, 0x00);
                mci_writel(host, CTRL, 0x00);
                enable_irq_wake(host->mmc->slot.cd_irq);
        }
        return 0;
}
EXPORT_SYMBOL(dw_mci_suspend);

int dw_mci_resume(struct dw_mci *host)
{
	int i, ret, retry_cnt = 0;
	u32 regs;
        struct dw_mci_slot *slot;
    
        if (host->mmc->restrict_caps & RESTRICT_CARD_TYPE_SDIO){
                slot = mmc_priv(host->mmc);

                if(!test_bit(DW_MMC_CARD_PRESENT, &slot->flags))
                        return 0;
        }
    	/*only for sdmmc controller*/
	if(host->mmc->restrict_caps & RESTRICT_CARD_TYPE_SD) {
		disable_irq_wake(host->mmc->slot.cd_irq);
                mmc_gpio_free_cd(host->mmc);
		if(pinctrl_select_state(host->pinctrl, host->pins_default) < 0)
                        MMC_DBG_ERR_FUNC(host->mmc, "Default pinctrl setting failed! [%s]",
                                                mmc_hostname(host->mmc));
		host->mmc->rescan_disable = 0;
		/* Disable jtag*/
		if(cpu_is_rk3288())
                        grf_writel(((1 << 12) << 16) | (0 << 12), RK3288_GRF_SOC_CON0);
                /*
                else if(cpu_is_rk3036())
                        grf_writel(((1 << 11) << 16) | (0 << 11), RK3036_GRF_SOC_CON0);
                else if(cpu_is_rk3126())
                        TODO;
                else if audi-b
                        TODO;
                */
	}
	if(host->vmmc){
		ret = regulator_enable(host->vmmc);
		if (ret){
			dev_err(host->dev,
				"failed to enable regulator: %d\n", ret);
			return ret;
		}
	}
	
	if(!dw_mci_ctrl_all_reset(host)){
		ret = -ENODEV;
		return ret;
	}

	if(host->use_dma && host->dma_ops->init)
		host->dma_ops->init(host);

	/*
	 * Restore the initial value at FIFOTH register
	 * And Invalidate the prev_blksz with zero
	 */
	mci_writel(host, FIFOTH, host->fifoth_val);
	host->prev_blksz = 0;
	/* Put in max timeout */
	mci_writel(host, TMOUT, 0xFFFFFFFF);

	mci_writel(host, RINTSTS, 0xFFFFFFFF);
	regs = SDMMC_INT_CMD_DONE | SDMMC_INT_DATA_OVER | SDMMC_INT_TXDR | SDMMC_INT_RXDR | SDMMC_INT_VSI |
		   DW_MCI_ERROR_FLAGS;
	if(!(host->mmc->restrict_caps & RESTRICT_CARD_TYPE_SDIO))
	    regs |= SDMMC_INT_CD;	   
	mci_writel(host, INTMASK, regs);
	mci_writel(host, CTRL, SDMMC_CTRL_INT_ENABLE);
	/*only for sdmmc controller*/
	if((host->mmc->restrict_caps & RESTRICT_CARD_TYPE_SD)&& (!retry_cnt)){
		enable_irq(host->irq);	
	}   

	for(i = 0; i < host->num_slots; i++){
		struct dw_mci_slot *slot = host->slot[i];
		if(!slot)
			continue;
		if(slot->mmc->pm_flags & MMC_PM_KEEP_POWER){
			dw_mci_set_ios(slot->mmc, &slot->mmc->ios);
			dw_mci_setup_bus(slot, true);
		}
	}

	return 0;
}
EXPORT_SYMBOL(dw_mci_resume);
#endif /* CONFIG_PM_SLEEP */

static int __init dw_mci_init(void)
{
	pr_info("Synopsys Designware Multimedia Card Interface Driver\n");
	return 0;
}

static void __exit dw_mci_exit(void)
{
}

module_init(dw_mci_init);
module_exit(dw_mci_exit);

MODULE_DESCRIPTION("Rockchip specific DW Multimedia Card Interface driver");
MODULE_AUTHOR("NXP Semiconductor VietNam");
MODULE_AUTHOR("Imagination Technologies Ltd");
MODULE_AUTHOR("Shawn Lin <lintao@rock-chips.com>");
MODULE_AUTHOR("Rockchip Electronics£¬Bangwang Xie < xbw@rock-chips.com> ");
MODULE_LICENSE("GPL v2");
