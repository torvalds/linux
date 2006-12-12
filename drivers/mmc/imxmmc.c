/*
 *  linux/drivers/mmc/imxmmc.c - Motorola i.MX MMCI driver
 *
 *  Copyright (C) 2004 Sascha Hauer, Pengutronix <sascha@saschahauer.de>
 *  Copyright (C) 2006 Pavel Pisa, PiKRON <ppisa@pikron.com>
 *
 *  derived from pxamci.c by Russell King
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *  2005-04-17 Pavel Pisa <pisa@cmp.felk.cvut.cz>
 *             Changed to conform redesigned i.MX scatter gather DMA interface
 *
 *  2005-11-04 Pavel Pisa <pisa@cmp.felk.cvut.cz>
 *             Updated for 2.6.14 kernel
 *
 *  2005-12-13 Jay Monkman <jtm@smoothsmoothie.com>
 *             Found and corrected problems in the write path
 *
 *  2005-12-30 Pavel Pisa <pisa@cmp.felk.cvut.cz>
 *             The event handling rewritten right way in softirq.
 *             Added many ugly hacks and delays to overcome SDHC
 *             deficiencies
 *
 */

#ifdef CONFIG_MMC_DEBUG
#define DEBUG
#else
#undef  DEBUG
#endif

#include <linux/module.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/dma-mapping.h>
#include <linux/mmc/host.h>
#include <linux/mmc/card.h>
#include <linux/mmc/protocol.h>
#include <linux/delay.h>

#include <asm/dma.h>
#include <asm/io.h>
#include <asm/irq.h>
#include <asm/sizes.h>
#include <asm/arch/mmc.h>
#include <asm/arch/imx-dma.h>

#include "imxmmc.h"

#define DRIVER_NAME "imx-mmc"

#define IMXMCI_INT_MASK_DEFAULT (INT_MASK_BUF_READY | INT_MASK_DATA_TRAN | \
	              INT_MASK_WRITE_OP_DONE | INT_MASK_END_CMD_RES | \
		      INT_MASK_AUTO_CARD_DETECT | INT_MASK_DAT0_EN | INT_MASK_SDIO)

struct imxmci_host {
	struct mmc_host		*mmc;
	spinlock_t		lock;
	struct resource		*res;
	int			irq;
	imx_dmach_t		dma;
	unsigned int		clkrt;
	unsigned int		cmdat;
	volatile unsigned int	imask;
	unsigned int		power_mode;
	unsigned int		present;
	struct imxmmc_platform_data *pdata;

	struct mmc_request	*req;
	struct mmc_command	*cmd;
	struct mmc_data		*data;

	struct timer_list	timer;
	struct tasklet_struct	tasklet;
	unsigned int		status_reg;
	unsigned long		pending_events;
	/* Next to fields are there for CPU driven transfers to overcome SDHC deficiencies */
	u16			*data_ptr;
	unsigned int		data_cnt;
	atomic_t		stuck_timeout;

	unsigned int		dma_nents;
	unsigned int		dma_size;
	unsigned int		dma_dir;
	int			dma_allocated;

	unsigned char		actual_bus_width;

	int			prev_cmd_code;
};

#define IMXMCI_PEND_IRQ_b	0
#define IMXMCI_PEND_DMA_END_b	1
#define IMXMCI_PEND_DMA_ERR_b	2
#define IMXMCI_PEND_WAIT_RESP_b	3
#define IMXMCI_PEND_DMA_DATA_b	4
#define IMXMCI_PEND_CPU_DATA_b	5
#define IMXMCI_PEND_CARD_XCHG_b	6
#define IMXMCI_PEND_SET_INIT_b	7
#define IMXMCI_PEND_STARTED_b	8

#define IMXMCI_PEND_IRQ_m	(1 << IMXMCI_PEND_IRQ_b)
#define IMXMCI_PEND_DMA_END_m	(1 << IMXMCI_PEND_DMA_END_b)
#define IMXMCI_PEND_DMA_ERR_m	(1 << IMXMCI_PEND_DMA_ERR_b)
#define IMXMCI_PEND_WAIT_RESP_m	(1 << IMXMCI_PEND_WAIT_RESP_b)
#define IMXMCI_PEND_DMA_DATA_m	(1 << IMXMCI_PEND_DMA_DATA_b)
#define IMXMCI_PEND_CPU_DATA_m	(1 << IMXMCI_PEND_CPU_DATA_b)
#define IMXMCI_PEND_CARD_XCHG_m	(1 << IMXMCI_PEND_CARD_XCHG_b)
#define IMXMCI_PEND_SET_INIT_m	(1 << IMXMCI_PEND_SET_INIT_b)
#define IMXMCI_PEND_STARTED_m	(1 << IMXMCI_PEND_STARTED_b)

static void imxmci_stop_clock(struct imxmci_host *host)
{
	int i = 0;
	MMC_STR_STP_CLK &= ~STR_STP_CLK_START_CLK;
	while(i < 0x1000) {
	        if(!(i & 0x7f))
			MMC_STR_STP_CLK |= STR_STP_CLK_STOP_CLK;

		if(!(MMC_STATUS & STATUS_CARD_BUS_CLK_RUN)) {
			/* Check twice before cut */
			if(!(MMC_STATUS & STATUS_CARD_BUS_CLK_RUN))
				return;
		}

		i++;
	}
	dev_dbg(mmc_dev(host->mmc), "imxmci_stop_clock blocked, no luck\n");
}

static int imxmci_start_clock(struct imxmci_host *host)
{
	unsigned int trials = 0;
	unsigned int delay_limit = 128;
	unsigned long flags;

	MMC_STR_STP_CLK &= ~STR_STP_CLK_STOP_CLK;

	clear_bit(IMXMCI_PEND_STARTED_b, &host->pending_events);

	/*
	 * Command start of the clock, this usually succeeds in less
	 * then 6 delay loops, but during card detection (low clockrate)
	 * it takes up to 5000 delay loops and sometimes fails for the first time
	 */
	MMC_STR_STP_CLK |= STR_STP_CLK_START_CLK;

	do {
		unsigned int delay = delay_limit;

		while(delay--){
			if(MMC_STATUS & STATUS_CARD_BUS_CLK_RUN)
				/* Check twice before cut */
				if(MMC_STATUS & STATUS_CARD_BUS_CLK_RUN)
					return 0;

			if(test_bit(IMXMCI_PEND_STARTED_b, &host->pending_events))
				return 0;
		}

		local_irq_save(flags);
		/*
		 * Ensure, that request is not doubled under all possible circumstances.
		 * It is possible, that cock running state is missed, because some other
		 * IRQ or schedule delays this function execution and the clocks has
		 * been already stopped by other means (response processing, SDHC HW)
		 */
		if(!test_bit(IMXMCI_PEND_STARTED_b, &host->pending_events))
			MMC_STR_STP_CLK |= STR_STP_CLK_START_CLK;
		local_irq_restore(flags);

	} while(++trials<256);

	dev_err(mmc_dev(host->mmc), "imxmci_start_clock blocked, no luck\n");

	return -1;
}

static void imxmci_softreset(void)
{
	/* reset sequence */
	MMC_STR_STP_CLK = 0x8;
	MMC_STR_STP_CLK = 0xD;
	MMC_STR_STP_CLK = 0x5;
	MMC_STR_STP_CLK = 0x5;
	MMC_STR_STP_CLK = 0x5;
	MMC_STR_STP_CLK = 0x5;
	MMC_STR_STP_CLK = 0x5;
	MMC_STR_STP_CLK = 0x5;
	MMC_STR_STP_CLK = 0x5;
	MMC_STR_STP_CLK = 0x5;

	MMC_RES_TO = 0xff;
	MMC_BLK_LEN = 512;
	MMC_NOB = 1;
}

static int imxmci_busy_wait_for_status(struct imxmci_host *host,
			unsigned int *pstat, unsigned int stat_mask,
			int timeout, const char *where)
{
	int loops=0;
	while(!(*pstat & stat_mask)) {
		loops+=2;
		if(loops >= timeout) {
			dev_dbg(mmc_dev(host->mmc), "busy wait timeout in %s, STATUS = 0x%x (0x%x)\n",
				where, *pstat, stat_mask);
			return -1;
		}
		udelay(2);
		*pstat |= MMC_STATUS;
	}
	if(!loops)
		return 0;

	/* The busy-wait is expected there for clock <8MHz due to SDHC hardware flaws */
	if(!(stat_mask & STATUS_END_CMD_RESP) || (host->mmc->ios.clock>=8000000))
		dev_info(mmc_dev(host->mmc), "busy wait for %d usec in %s, STATUS = 0x%x (0x%x)\n",
			loops, where, *pstat, stat_mask);
	return loops;
}

static void imxmci_setup_data(struct imxmci_host *host, struct mmc_data *data)
{
	unsigned int nob = data->blocks;
	unsigned int blksz = data->blksz;
	unsigned int datasz = nob * blksz;
	int i;

	if (data->flags & MMC_DATA_STREAM)
		nob = 0xffff;

	host->data = data;
	data->bytes_xfered = 0;

	MMC_NOB = nob;
	MMC_BLK_LEN = blksz;

	/*
	 * DMA cannot be used for small block sizes, we have to use CPU driven transfers otherwise.
	 * We are in big troubles for non-512 byte transfers according to note in the paragraph
	 * 20.6.7 of User Manual anyway, but we need to be able to transfer SCR at least.
	 * The situation is even more complex in reality. The SDHC in not able to handle wll
	 * partial FIFO fills and reads. The length has to be rounded up to burst size multiple.
	 * This is required for SCR read at least.
	 */
	if (datasz < 512) {
		host->dma_size = datasz;
		if (data->flags & MMC_DATA_READ) {
			host->dma_dir = DMA_FROM_DEVICE;

			/* Hack to enable read SCR */
			MMC_NOB = 1;
			MMC_BLK_LEN = 512;
		} else {
			host->dma_dir = DMA_TO_DEVICE;
		}

		/* Convert back to virtual address */
		host->data_ptr = (u16*)(page_address(data->sg->page) + data->sg->offset);
		host->data_cnt = 0;

		clear_bit(IMXMCI_PEND_DMA_DATA_b, &host->pending_events);
		set_bit(IMXMCI_PEND_CPU_DATA_b, &host->pending_events);

		return;
	}

	if (data->flags & MMC_DATA_READ) {
		host->dma_dir = DMA_FROM_DEVICE;
		host->dma_nents = dma_map_sg(mmc_dev(host->mmc), data->sg,
						data->sg_len,  host->dma_dir);

		imx_dma_setup_sg(host->dma, data->sg, data->sg_len, datasz,
			host->res->start + MMC_BUFFER_ACCESS_OFS, DMA_MODE_READ);

		/*imx_dma_setup_mem2dev_ccr(host->dma, DMA_MODE_READ, IMX_DMA_WIDTH_16, CCR_REN);*/
		CCR(host->dma) = CCR_DMOD_LINEAR | CCR_DSIZ_32 | CCR_SMOD_FIFO | CCR_SSIZ_16 | CCR_REN;
	} else {
		host->dma_dir = DMA_TO_DEVICE;

		host->dma_nents = dma_map_sg(mmc_dev(host->mmc), data->sg,
						data->sg_len,  host->dma_dir);

		imx_dma_setup_sg(host->dma, data->sg, data->sg_len, datasz,
			host->res->start + MMC_BUFFER_ACCESS_OFS, DMA_MODE_WRITE);

		/*imx_dma_setup_mem2dev_ccr(host->dma, DMA_MODE_WRITE, IMX_DMA_WIDTH_16, CCR_REN);*/
		CCR(host->dma) = CCR_SMOD_LINEAR | CCR_SSIZ_32 | CCR_DMOD_FIFO | CCR_DSIZ_16 | CCR_REN;
	}

#if 1	/* This code is there only for consistency checking and can be disabled in future */
	host->dma_size = 0;
	for(i=0; i<host->dma_nents; i++)
		host->dma_size+=data->sg[i].length;

	if (datasz > host->dma_size) {
		dev_err(mmc_dev(host->mmc), "imxmci_setup_data datasz 0x%x > 0x%x dm_size\n",
		       datasz, host->dma_size);
	}
#endif

	host->dma_size = datasz;

	wmb();

	if(host->actual_bus_width == MMC_BUS_WIDTH_4)
		BLR(host->dma) = 0;	/* burst 64 byte read / 64 bytes write */
	else
		BLR(host->dma) = 16;	/* burst 16 byte read / 16 bytes write */

	RSSR(host->dma) = DMA_REQ_SDHC;

	set_bit(IMXMCI_PEND_DMA_DATA_b, &host->pending_events);
	clear_bit(IMXMCI_PEND_CPU_DATA_b, &host->pending_events);

	/* start DMA engine for read, write is delayed after initial response */
	if (host->dma_dir == DMA_FROM_DEVICE) {
		imx_dma_enable(host->dma);
	}
}

static void imxmci_start_cmd(struct imxmci_host *host, struct mmc_command *cmd, unsigned int cmdat)
{
	unsigned long flags;
	u32 imask;

	WARN_ON(host->cmd != NULL);
	host->cmd = cmd;

	/* Ensure, that clock are stopped else command programming and start fails */
	imxmci_stop_clock(host);

	if (cmd->flags & MMC_RSP_BUSY)
		cmdat |= CMD_DAT_CONT_BUSY;

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_R1: /* short CRC, OPCODE */
	case MMC_RSP_R1B:/* short CRC, OPCODE, BUSY */
		cmdat |= CMD_DAT_CONT_RESPONSE_FORMAT_R1;
		break;
	case MMC_RSP_R2: /* long 136 bit + CRC */
		cmdat |= CMD_DAT_CONT_RESPONSE_FORMAT_R2;
		break;
	case MMC_RSP_R3: /* short */
		cmdat |= CMD_DAT_CONT_RESPONSE_FORMAT_R3;
		break;
	case MMC_RSP_R6: /* short CRC */
		cmdat |= CMD_DAT_CONT_RESPONSE_FORMAT_R6;
		break;
	default:
		break;
	}

	if ( test_and_clear_bit(IMXMCI_PEND_SET_INIT_b, &host->pending_events) )
		cmdat |= CMD_DAT_CONT_INIT; /* This command needs init */

	if ( host->actual_bus_width == MMC_BUS_WIDTH_4 )
		cmdat |= CMD_DAT_CONT_BUS_WIDTH_4;

	MMC_CMD = cmd->opcode;
	MMC_ARGH = cmd->arg >> 16;
	MMC_ARGL = cmd->arg & 0xffff;
	MMC_CMD_DAT_CONT = cmdat;

	atomic_set(&host->stuck_timeout, 0);
	set_bit(IMXMCI_PEND_WAIT_RESP_b, &host->pending_events);


	imask = IMXMCI_INT_MASK_DEFAULT;
	imask &= ~INT_MASK_END_CMD_RES;
	if ( cmdat & CMD_DAT_CONT_DATA_ENABLE ) {
		/*imask &= ~INT_MASK_BUF_READY;*/
		imask &= ~INT_MASK_DATA_TRAN;
		if ( cmdat & CMD_DAT_CONT_WRITE )
			imask &= ~INT_MASK_WRITE_OP_DONE;
		if(test_bit(IMXMCI_PEND_CPU_DATA_b, &host->pending_events))
			imask &= ~INT_MASK_BUF_READY;
	}

	spin_lock_irqsave(&host->lock, flags);
	host->imask = imask;
	MMC_INT_MASK = host->imask;
	spin_unlock_irqrestore(&host->lock, flags);

	dev_dbg(mmc_dev(host->mmc), "CMD%02d (0x%02x) mask set to 0x%04x\n",
		cmd->opcode, cmd->opcode, imask);

	imxmci_start_clock(host);
}

static void imxmci_finish_request(struct imxmci_host *host, struct mmc_request *req)
{
	unsigned long flags;

	spin_lock_irqsave(&host->lock, flags);

	host->pending_events &= ~(IMXMCI_PEND_WAIT_RESP_m | IMXMCI_PEND_DMA_END_m |
			IMXMCI_PEND_DMA_DATA_m | IMXMCI_PEND_CPU_DATA_m);

	host->imask = IMXMCI_INT_MASK_DEFAULT;
	MMC_INT_MASK = host->imask;

	spin_unlock_irqrestore(&host->lock, flags);

	if(req && req->cmd)
		host->prev_cmd_code = req->cmd->opcode;

	host->req = NULL;
	host->cmd = NULL;
	host->data = NULL;
	mmc_request_done(host->mmc, req);
}

static int imxmci_finish_data(struct imxmci_host *host, unsigned int stat)
{
	struct mmc_data *data = host->data;
	int data_error;

	if(test_and_clear_bit(IMXMCI_PEND_DMA_DATA_b, &host->pending_events)){
		imx_dma_disable(host->dma);
		dma_unmap_sg(mmc_dev(host->mmc), data->sg, host->dma_nents,
			     host->dma_dir);
	}

	if ( stat & STATUS_ERR_MASK ) {
		dev_dbg(mmc_dev(host->mmc), "request failed. status: 0x%08x\n",stat);
		if(stat & (STATUS_CRC_READ_ERR | STATUS_CRC_WRITE_ERR))
			data->error = MMC_ERR_BADCRC;
		else if(stat & STATUS_TIME_OUT_READ)
			data->error = MMC_ERR_TIMEOUT;
		else
			data->error = MMC_ERR_FAILED;
	} else {
		data->bytes_xfered = host->dma_size;
	}

	data_error = data->error;

	host->data = NULL;

	return data_error;
}

static int imxmci_cmd_done(struct imxmci_host *host, unsigned int stat)
{
	struct mmc_command *cmd = host->cmd;
	int i;
	u32 a,b,c;
	struct mmc_data *data = host->data;

	if (!cmd)
		return 0;

	host->cmd = NULL;

	if (stat & STATUS_TIME_OUT_RESP) {
		dev_dbg(mmc_dev(host->mmc), "CMD TIMEOUT\n");
		cmd->error = MMC_ERR_TIMEOUT;
	} else if (stat & STATUS_RESP_CRC_ERR && cmd->flags & MMC_RSP_CRC) {
		dev_dbg(mmc_dev(host->mmc), "cmd crc error\n");
		cmd->error = MMC_ERR_BADCRC;
	}

	if(cmd->flags & MMC_RSP_PRESENT) {
		if(cmd->flags & MMC_RSP_136) {
			for (i = 0; i < 4; i++) {
				u32 a = MMC_RES_FIFO & 0xffff;
				u32 b = MMC_RES_FIFO & 0xffff;
				cmd->resp[i] = a<<16 | b;
			}
		} else {
			a = MMC_RES_FIFO & 0xffff;
			b = MMC_RES_FIFO & 0xffff;
			c = MMC_RES_FIFO & 0xffff;
			cmd->resp[0] = a<<24 | b<<8 | c>>8;
		}
	}

	dev_dbg(mmc_dev(host->mmc), "RESP 0x%08x, 0x%08x, 0x%08x, 0x%08x, error %d\n",
		cmd->resp[0], cmd->resp[1], cmd->resp[2], cmd->resp[3], cmd->error);

	if (data && (cmd->error == MMC_ERR_NONE) && !(stat & STATUS_ERR_MASK)) {
		if (host->req->data->flags & MMC_DATA_WRITE) {

			/* Wait for FIFO to be empty before starting DMA write */

			stat = MMC_STATUS;
			if(imxmci_busy_wait_for_status(host, &stat,
				STATUS_APPL_BUFF_FE,
				40, "imxmci_cmd_done DMA WR") < 0) {
				cmd->error = MMC_ERR_FIFO;
				imxmci_finish_data(host, stat);
				if(host->req)
					imxmci_finish_request(host, host->req);
				dev_warn(mmc_dev(host->mmc), "STATUS = 0x%04x\n",
				       stat);
				return 0;
			}

			if(test_bit(IMXMCI_PEND_DMA_DATA_b, &host->pending_events)) {
				imx_dma_enable(host->dma);
			}
		}
	} else {
		struct mmc_request *req;
		imxmci_stop_clock(host);
		req = host->req;

		if(data)
			imxmci_finish_data(host, stat);

		if( req ) {
			imxmci_finish_request(host, req);
		} else {
			dev_warn(mmc_dev(host->mmc), "imxmci_cmd_done: no request to finish\n");
		}
	}

	return 1;
}

static int imxmci_data_done(struct imxmci_host *host, unsigned int stat)
{
	struct mmc_data *data = host->data;
	int data_error;

	if (!data)
		return 0;

	data_error = imxmci_finish_data(host, stat);

	if (host->req->stop) {
		imxmci_stop_clock(host);
		imxmci_start_cmd(host, host->req->stop, 0);
	} else {
		struct mmc_request *req;
		req = host->req;
		if( req ) {
			imxmci_finish_request(host, req);
		} else {
			dev_warn(mmc_dev(host->mmc), "imxmci_data_done: no request to finish\n");
		}
	}

	return 1;
}

static int imxmci_cpu_driven_data(struct imxmci_host *host, unsigned int *pstat)
{
	int i;
	int burst_len;
	int trans_done = 0;
	unsigned int stat = *pstat;

	if(host->actual_bus_width != MMC_BUS_WIDTH_4)
		burst_len = 16;
	else
		burst_len = 64;

	/* This is unfortunately required */
	dev_dbg(mmc_dev(host->mmc), "imxmci_cpu_driven_data running STATUS = 0x%x\n",
		stat);

	udelay(20);	/* required for clocks < 8MHz*/

	if(host->dma_dir == DMA_FROM_DEVICE) {
		imxmci_busy_wait_for_status(host, &stat,
				STATUS_APPL_BUFF_FF | STATUS_DATA_TRANS_DONE,
				50, "imxmci_cpu_driven_data read");

		while((stat & (STATUS_APPL_BUFF_FF |  STATUS_DATA_TRANS_DONE)) &&
		      (host->data_cnt < 512)) {

			udelay(20);	/* required for clocks < 8MHz*/

			for(i = burst_len; i>=2 ; i-=2) {
				u16 data;
				data = MMC_BUFFER_ACCESS;
				udelay(10);	/* required for clocks < 8MHz*/
				if(host->data_cnt+2 <= host->dma_size) {
					*(host->data_ptr++) = data;
				} else {
					if(host->data_cnt < host->dma_size)
						*(u8*)(host->data_ptr) = data;
				}
				host->data_cnt += 2;
			}

			stat = MMC_STATUS;

			dev_dbg(mmc_dev(host->mmc), "imxmci_cpu_driven_data read %d burst %d STATUS = 0x%x\n",
				host->data_cnt, burst_len, stat);
		}

		if((stat & STATUS_DATA_TRANS_DONE) && (host->data_cnt >= 512))
			trans_done = 1;

		if(host->dma_size & 0x1ff)
			stat &= ~STATUS_CRC_READ_ERR;

	} else {
		imxmci_busy_wait_for_status(host, &stat,
				STATUS_APPL_BUFF_FE,
				20, "imxmci_cpu_driven_data write");

		while((stat & STATUS_APPL_BUFF_FE) &&
		      (host->data_cnt < host->dma_size)) {
			if(burst_len >= host->dma_size - host->data_cnt) {
				burst_len = host->dma_size - host->data_cnt;
				host->data_cnt = host->dma_size;
				trans_done = 1;
			} else {
				host->data_cnt += burst_len;
			}

			for(i = burst_len; i>0 ; i-=2)
				MMC_BUFFER_ACCESS = *(host->data_ptr++);

			stat = MMC_STATUS;

			dev_dbg(mmc_dev(host->mmc), "imxmci_cpu_driven_data write burst %d STATUS = 0x%x\n",
				burst_len, stat);
		}
	}

	*pstat = stat;

	return trans_done;
}

static void imxmci_dma_irq(int dma, void *devid)
{
	struct imxmci_host *host = devid;
	uint32_t stat = MMC_STATUS;

	atomic_set(&host->stuck_timeout, 0);
	host->status_reg = stat;
	set_bit(IMXMCI_PEND_DMA_END_b, &host->pending_events);
	tasklet_schedule(&host->tasklet);
}

static irqreturn_t imxmci_irq(int irq, void *devid)
{
	struct imxmci_host *host = devid;
	uint32_t stat = MMC_STATUS;
	int handled = 1;

	MMC_INT_MASK = host->imask | INT_MASK_SDIO | INT_MASK_AUTO_CARD_DETECT;

	atomic_set(&host->stuck_timeout, 0);
	host->status_reg = stat;
	set_bit(IMXMCI_PEND_IRQ_b, &host->pending_events);
	set_bit(IMXMCI_PEND_STARTED_b, &host->pending_events);
	tasklet_schedule(&host->tasklet);

	return IRQ_RETVAL(handled);;
}

static void imxmci_tasklet_fnc(unsigned long data)
{
	struct imxmci_host *host = (struct imxmci_host *)data;
	u32 stat;
	unsigned int data_dir_mask = 0;	/* STATUS_WR_CRC_ERROR_CODE_MASK */
	int timeout = 0;

	if(atomic_read(&host->stuck_timeout) > 4) {
		char *what;
		timeout = 1;
		stat = MMC_STATUS;
		host->status_reg = stat;
		if (test_bit(IMXMCI_PEND_WAIT_RESP_b, &host->pending_events))
			if (test_bit(IMXMCI_PEND_DMA_DATA_b, &host->pending_events))
				what = "RESP+DMA";
			else
				what = "RESP";
		else
			if (test_bit(IMXMCI_PEND_DMA_DATA_b, &host->pending_events))
				if(test_bit(IMXMCI_PEND_DMA_END_b, &host->pending_events))
					what = "DATA";
				else
					what = "DMA";
			else
				what = "???";

		dev_err(mmc_dev(host->mmc), "%s TIMEOUT, hardware stucked STATUS = 0x%04x IMASK = 0x%04x\n",
		       what, stat, MMC_INT_MASK);
		dev_err(mmc_dev(host->mmc), "CMD_DAT_CONT = 0x%04x, MMC_BLK_LEN = 0x%04x, MMC_NOB = 0x%04x, DMA_CCR = 0x%08x\n",
		       MMC_CMD_DAT_CONT, MMC_BLK_LEN, MMC_NOB, CCR(host->dma));
		dev_err(mmc_dev(host->mmc), "CMD%d, prevCMD%d, bus %d-bit, dma_size = 0x%x\n",
		       host->cmd?host->cmd->opcode:0, host->prev_cmd_code, 1<<host->actual_bus_width, host->dma_size);
	}

	if(!host->present || timeout)
		host->status_reg = STATUS_TIME_OUT_RESP | STATUS_TIME_OUT_READ |
				    STATUS_CRC_READ_ERR | STATUS_CRC_WRITE_ERR;

	if(test_bit(IMXMCI_PEND_IRQ_b, &host->pending_events) || timeout) {
		clear_bit(IMXMCI_PEND_IRQ_b, &host->pending_events);

		stat = MMC_STATUS;
		/*
		 * This is not required in theory, but there is chance to miss some flag
		 * which clears automatically by mask write, FreeScale original code keeps
		 * stat from IRQ time so do I
		 */
		stat |= host->status_reg;

		if(test_bit(IMXMCI_PEND_WAIT_RESP_b, &host->pending_events)) {
			imxmci_busy_wait_for_status(host, &stat,
					STATUS_END_CMD_RESP | STATUS_ERR_MASK,
					20, "imxmci_tasklet_fnc resp (ERRATUM #4)");
		}

		if(stat & (STATUS_END_CMD_RESP | STATUS_ERR_MASK)) {
			if(test_and_clear_bit(IMXMCI_PEND_WAIT_RESP_b, &host->pending_events))
				imxmci_cmd_done(host, stat);
			if(host->data && (stat & STATUS_ERR_MASK))
				imxmci_data_done(host, stat);
		}

		if(test_bit(IMXMCI_PEND_CPU_DATA_b, &host->pending_events)) {
			stat |= MMC_STATUS;
			if(imxmci_cpu_driven_data(host, &stat)){
				if(test_and_clear_bit(IMXMCI_PEND_WAIT_RESP_b, &host->pending_events))
					imxmci_cmd_done(host, stat);
				atomic_clear_mask(IMXMCI_PEND_IRQ_m|IMXMCI_PEND_CPU_DATA_m,
							&host->pending_events);
				imxmci_data_done(host, stat);
			}
		}
	}

	if(test_bit(IMXMCI_PEND_DMA_END_b, &host->pending_events) &&
	   !test_bit(IMXMCI_PEND_WAIT_RESP_b, &host->pending_events)) {

		stat = MMC_STATUS;
		/* Same as above */
		stat |= host->status_reg;

		if(host->dma_dir == DMA_TO_DEVICE) {
			data_dir_mask = STATUS_WRITE_OP_DONE;
		} else {
			data_dir_mask = STATUS_DATA_TRANS_DONE;
		}

		if(stat & data_dir_mask) {
			clear_bit(IMXMCI_PEND_DMA_END_b, &host->pending_events);
			imxmci_data_done(host, stat);
		}
	}

	if(test_and_clear_bit(IMXMCI_PEND_CARD_XCHG_b, &host->pending_events)) {

		if(host->cmd)
			imxmci_cmd_done(host, STATUS_TIME_OUT_RESP);

		if(host->data)
			imxmci_data_done(host, STATUS_TIME_OUT_READ |
					 STATUS_CRC_READ_ERR | STATUS_CRC_WRITE_ERR);

		if(host->req)
			imxmci_finish_request(host, host->req);

		mmc_detect_change(host->mmc, msecs_to_jiffies(100));

	}
}

static void imxmci_request(struct mmc_host *mmc, struct mmc_request *req)
{
	struct imxmci_host *host = mmc_priv(mmc);
	unsigned int cmdat;

	WARN_ON(host->req != NULL);

	host->req = req;

	cmdat = 0;

	if (req->data) {
		imxmci_setup_data(host, req->data);

		cmdat |= CMD_DAT_CONT_DATA_ENABLE;

		if (req->data->flags & MMC_DATA_WRITE)
			cmdat |= CMD_DAT_CONT_WRITE;

		if (req->data->flags & MMC_DATA_STREAM) {
			cmdat |= CMD_DAT_CONT_STREAM_BLOCK;
		}
	}

	imxmci_start_cmd(host, req->cmd, cmdat);
}

#define CLK_RATE 19200000

static void imxmci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct imxmci_host *host = mmc_priv(mmc);
	int prescaler;

	if( ios->bus_width==MMC_BUS_WIDTH_4 ) {
		host->actual_bus_width = MMC_BUS_WIDTH_4;
		imx_gpio_mode(PB11_PF_SD_DAT3);
	}else{
		host->actual_bus_width = MMC_BUS_WIDTH_1;
		imx_gpio_mode(GPIO_PORTB | GPIO_IN | GPIO_PUEN | 11);
	}

	if ( host->power_mode != ios->power_mode ) {
		switch (ios->power_mode) {
		case MMC_POWER_OFF:
        		break;
		case MMC_POWER_UP:
			set_bit(IMXMCI_PEND_SET_INIT_b, &host->pending_events);
        		break;
		case MMC_POWER_ON:
        		break;
		}
		host->power_mode = ios->power_mode;
	}

	if ( ios->clock ) {
		unsigned int clk;

		/* The prescaler is 5 for PERCLK2 equal to 96MHz
		 * then 96MHz / 5 = 19.2 MHz
		 */
		clk=imx_get_perclk2();
		prescaler=(clk+(CLK_RATE*7)/8)/CLK_RATE;
		switch(prescaler) {
		case 0:
		case 1:	prescaler = 0;
			break;
		case 2:	prescaler = 1;
			break;
		case 3:	prescaler = 2;
			break;
		case 4:	prescaler = 4;
			break;
		default:
		case 5:	prescaler = 5;
			break;
		}

		dev_dbg(mmc_dev(host->mmc), "PERCLK2 %d MHz -> prescaler %d\n",
			clk, prescaler);

		for(clk=0; clk<8; clk++) {
			int x;
			x = CLK_RATE / (1<<clk);
			if( x <= ios->clock)
				break;
		}

		MMC_STR_STP_CLK |= STR_STP_CLK_ENABLE; /* enable controller */

		imxmci_stop_clock(host);
		MMC_CLK_RATE = (prescaler<<3) | clk;
		/*
		 * Under my understanding, clock should not be started there, because it would
		 * initiate SDHC sequencer and send last or random command into card
		 */
		/*imxmci_start_clock(host);*/

		dev_dbg(mmc_dev(host->mmc), "MMC_CLK_RATE: 0x%08x\n", MMC_CLK_RATE);
	} else {
		imxmci_stop_clock(host);
	}
}

static const struct mmc_host_ops imxmci_ops = {
	.request	= imxmci_request,
	.set_ios	= imxmci_set_ios,
};

static struct resource *platform_device_resource(struct platform_device *dev, unsigned int mask, int nr)
{
	int i;

	for (i = 0; i < dev->num_resources; i++)
		if (dev->resource[i].flags == mask && nr-- == 0)
			return &dev->resource[i];
	return NULL;
}

static int platform_device_irq(struct platform_device *dev, int nr)
{
	int i;

	for (i = 0; i < dev->num_resources; i++)
		if (dev->resource[i].flags == IORESOURCE_IRQ && nr-- == 0)
			return dev->resource[i].start;
	return NO_IRQ;
}

static void imxmci_check_status(unsigned long data)
{
	struct imxmci_host *host = (struct imxmci_host *)data;

	if( host->pdata->card_present() != host->present ) {
		host->present ^= 1;
		dev_info(mmc_dev(host->mmc), "card %s\n",
		      host->present ? "inserted" : "removed");

		set_bit(IMXMCI_PEND_CARD_XCHG_b, &host->pending_events);
		tasklet_schedule(&host->tasklet);
	}

	if(test_bit(IMXMCI_PEND_WAIT_RESP_b, &host->pending_events) ||
	   test_bit(IMXMCI_PEND_DMA_DATA_b, &host->pending_events)) {
		atomic_inc(&host->stuck_timeout);
		if(atomic_read(&host->stuck_timeout) > 4)
			tasklet_schedule(&host->tasklet);
	} else {
		atomic_set(&host->stuck_timeout, 0);

	}

	mod_timer(&host->timer, jiffies + (HZ>>1));
}

static int imxmci_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct imxmci_host *host = NULL;
	struct resource *r;
	int ret = 0, irq;

	printk(KERN_INFO "i.MX mmc driver\n");

	r = platform_device_resource(pdev, IORESOURCE_MEM, 0);
	irq = platform_device_irq(pdev, 0);
	if (!r || irq == NO_IRQ)
		return -ENXIO;

	r = request_mem_region(r->start, 0x100, "IMXMCI");
	if (!r)
		return -EBUSY;

	mmc = mmc_alloc_host(sizeof(struct imxmci_host), &pdev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		goto out;
	}

	mmc->ops = &imxmci_ops;
	mmc->f_min = 150000;
	mmc->f_max = CLK_RATE/2;
	mmc->ocr_avail = MMC_VDD_32_33;
	mmc->caps = MMC_CAP_4_BIT_DATA | MMC_CAP_BYTEBLOCK;

	/* MMC core transfer sizes tunable parameters */
	mmc->max_hw_segs = 64;
	mmc->max_phys_segs = 64;
	mmc->max_sectors = 64;		/* default 1 << (PAGE_CACHE_SHIFT - 9) */
	mmc->max_seg_size = 64*512;	/* default PAGE_CACHE_SIZE */

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->dma_allocated = 0;
	host->pdata = pdev->dev.platform_data;

	spin_lock_init(&host->lock);
	host->res = r;
	host->irq = irq;

	imx_gpio_mode(PB8_PF_SD_DAT0);
	imx_gpio_mode(PB9_PF_SD_DAT1);
	imx_gpio_mode(PB10_PF_SD_DAT2);
	/* Configured as GPIO with pull-up to ensure right MCC card mode */
	/* Switched to PB11_PF_SD_DAT3 if 4 bit bus is configured */
	imx_gpio_mode(GPIO_PORTB | GPIO_IN | GPIO_PUEN | 11);
	/* imx_gpio_mode(PB11_PF_SD_DAT3); */
	imx_gpio_mode(PB12_PF_SD_CLK);
	imx_gpio_mode(PB13_PF_SD_CMD);

	imxmci_softreset();

	if ( MMC_REV_NO != 0x390 ) {
		dev_err(mmc_dev(host->mmc), "wrong rev.no. 0x%08x. aborting.\n",
		        MMC_REV_NO);
		goto out;
	}

	MMC_READ_TO = 0x2db4; /* recommended in data sheet */

	host->imask = IMXMCI_INT_MASK_DEFAULT;
	MMC_INT_MASK = host->imask;


	if(imx_dma_request_by_prio(&host->dma, DRIVER_NAME, DMA_PRIO_LOW)<0){
		dev_err(mmc_dev(host->mmc), "imx_dma_request_by_prio failed\n");
		ret = -EBUSY;
		goto out;
	}
	host->dma_allocated=1;
	imx_dma_setup_handlers(host->dma, imxmci_dma_irq, NULL, host);

	tasklet_init(&host->tasklet, imxmci_tasklet_fnc, (unsigned long)host);
	host->status_reg=0;
	host->pending_events=0;

	ret = request_irq(host->irq, imxmci_irq, 0, DRIVER_NAME, host);
	if (ret)
		goto out;

	host->present = host->pdata->card_present();
	init_timer(&host->timer);
	host->timer.data = (unsigned long)host;
	host->timer.function = imxmci_check_status;
	add_timer(&host->timer);
	mod_timer(&host->timer, jiffies + (HZ>>1));

	platform_set_drvdata(pdev, mmc);

	mmc_add_host(mmc);

	return 0;

out:
	if (host) {
		if(host->dma_allocated){
			imx_dma_free(host->dma);
			host->dma_allocated=0;
		}
	}
	if (mmc)
		mmc_free_host(mmc);
	release_resource(r);
	return ret;
}

static int imxmci_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);

	platform_set_drvdata(pdev, NULL);

	if (mmc) {
		struct imxmci_host *host = mmc_priv(mmc);

		tasklet_disable(&host->tasklet);

		del_timer_sync(&host->timer);
		mmc_remove_host(mmc);

		free_irq(host->irq, host);
		if(host->dma_allocated){
			imx_dma_free(host->dma);
			host->dma_allocated=0;
		}

		tasklet_kill(&host->tasklet);

		release_resource(host->res);

		mmc_free_host(mmc);
	}
	return 0;
}

#ifdef CONFIG_PM
static int imxmci_suspend(struct platform_device *dev, pm_message_t state)
{
	struct mmc_host *mmc = platform_get_drvdata(dev);
	int ret = 0;

	if (mmc)
		ret = mmc_suspend_host(mmc, state);

	return ret;
}

static int imxmci_resume(struct platform_device *dev)
{
	struct mmc_host *mmc = platform_get_drvdata(dev);
	struct imxmci_host *host;
	int ret = 0;

	if (mmc) {
		host = mmc_priv(mmc);
		if(host)
			set_bit(IMXMCI_PEND_SET_INIT_b, &host->pending_events);
		ret = mmc_resume_host(mmc);
	}

	return ret;
}
#else
#define imxmci_suspend  NULL
#define imxmci_resume   NULL
#endif /* CONFIG_PM */

static struct platform_driver imxmci_driver = {
	.probe		= imxmci_probe,
	.remove		= imxmci_remove,
	.suspend	= imxmci_suspend,
	.resume		= imxmci_resume,
	.driver		= {
		.name		= DRIVER_NAME,
	}
};

static int __init imxmci_init(void)
{
	return platform_driver_register(&imxmci_driver);
}

static void __exit imxmci_exit(void)
{
	platform_driver_unregister(&imxmci_driver);
}

module_init(imxmci_init);
module_exit(imxmci_exit);

MODULE_DESCRIPTION("i.MX Multimedia Card Interface Driver");
MODULE_AUTHOR("Sascha Hauer, Pengutronix");
MODULE_LICENSE("GPL");
