/*
 * linux/drivers/mmc/au1xmmc.c - AU1XX0 MMC driver
 *
 *  Copyright (c) 2005, Advanced Micro Devices, Inc.
 *
 *  Developed with help from the 2.4.30 MMC AU1XXX controller including
 *  the following copyright notices:
 *     Copyright (c) 2003-2004 Embedded Edge, LLC.
 *     Portions Copyright (C) 2002 Embedix, Inc
 *     Copyright 2002 Hewlett-Packard Company

 *  2.6 version of this driver inspired by:
 *     (drivers/mmc/wbsd.c) Copyright (C) 2004-2005 Pierre Ossman,
 *     All Rights Reserved.
 *     (drivers/mmc/pxa.c) Copyright (C) 2003 Russell King,
 *     All Rights Reserved.
 *

 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/* Why is a timer used to detect insert events?
 *
 * From the AU1100 MMC application guide:
 * If the Au1100-based design is intended to support both MultiMediaCards
 * and 1- or 4-data bit SecureDigital cards, then the solution is to
 * connect a weak (560KOhm) pull-up resistor to connector pin 1.
 * In doing so, a MMC card never enters SPI-mode communications,
 * but now the SecureDigital card-detect feature of CD/DAT3 is ineffective
 * (the low to high transition will not occur).
 *
 * So we use the timer to check the status manually.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/mm.h>
#include <linux/interrupt.h>
#include <linux/dma-mapping.h>

#include <linux/mmc/host.h>
#include <linux/mmc/protocol.h>
#include <asm/io.h>
#include <asm/mach-au1x00/au1000.h>
#include <asm/mach-au1x00/au1xxx_dbdma.h>
#include <asm/mach-au1x00/au1100_mmc.h>
#include <asm/scatterlist.h>

#include <au1xxx.h>
#include "au1xmmc.h"

#define DRIVER_NAME "au1xxx-mmc"

/* Set this to enable special debugging macros */
/* #define MMC_DEBUG */

#ifdef MMC_DEBUG
#define DEBUG(fmt, idx, args...) printk("au1xx(%d): DEBUG: " fmt, idx, ##args)
#else
#define DEBUG(fmt, idx, args...)
#endif

const struct {
	u32 iobase;
	u32 tx_devid, rx_devid;
	u16 bcsrpwr;
	u16 bcsrstatus;
	u16 wpstatus;
} au1xmmc_card_table[] = {
	{ SD0_BASE, DSCR_CMD0_SDMS_TX0, DSCR_CMD0_SDMS_RX0,
	  BCSR_BOARD_SD0PWR, BCSR_INT_SD0INSERT, BCSR_STATUS_SD0WP },
#ifndef CONFIG_MIPS_DB1200
	{ SD1_BASE, DSCR_CMD0_SDMS_TX1, DSCR_CMD0_SDMS_RX1,
	  BCSR_BOARD_DS1PWR, BCSR_INT_SD1INSERT, BCSR_STATUS_SD1WP }
#endif
};

#define AU1XMMC_CONTROLLER_COUNT \
	(sizeof(au1xmmc_card_table) / sizeof(au1xmmc_card_table[0]))

/* This array stores pointers for the hosts (used by the IRQ handler) */
struct au1xmmc_host *au1xmmc_hosts[AU1XMMC_CONTROLLER_COUNT];
static int dma = 1;

#ifdef MODULE
module_param(dma, bool, 0);
MODULE_PARM_DESC(dma, "Use DMA engine for data transfers (0 = disabled)");
#endif

static inline void IRQ_ON(struct au1xmmc_host *host, u32 mask)
{
	u32 val = au_readl(HOST_CONFIG(host));
	val |= mask;
	au_writel(val, HOST_CONFIG(host));
	au_sync();
}

static inline void FLUSH_FIFO(struct au1xmmc_host *host)
{
	u32 val = au_readl(HOST_CONFIG2(host));

	au_writel(val | SD_CONFIG2_FF, HOST_CONFIG2(host));
	au_sync_delay(1);

	/* SEND_STOP will turn off clock control - this re-enables it */
	val &= ~SD_CONFIG2_DF;

	au_writel(val, HOST_CONFIG2(host));
	au_sync();
}

static inline void IRQ_OFF(struct au1xmmc_host *host, u32 mask)
{
	u32 val = au_readl(HOST_CONFIG(host));
	val &= ~mask;
	au_writel(val, HOST_CONFIG(host));
	au_sync();
}

static inline void SEND_STOP(struct au1xmmc_host *host)
{

	/* We know the value of CONFIG2, so avoid a read we don't need */
	u32 mask = SD_CONFIG2_EN;

	WARN_ON(host->status != HOST_S_DATA);
	host->status = HOST_S_STOP;

	au_writel(mask | SD_CONFIG2_DF, HOST_CONFIG2(host));
	au_sync();

	/* Send the stop commmand */
	au_writel(STOP_CMD, HOST_CMD(host));
}

static void au1xmmc_set_power(struct au1xmmc_host *host, int state)
{

	u32 val = au1xmmc_card_table[host->id].bcsrpwr;

	bcsr->board &= ~val;
	if (state) bcsr->board |= val;

	au_sync_delay(1);
}

static inline int au1xmmc_card_inserted(struct au1xmmc_host *host)
{
	return (bcsr->sig_status & au1xmmc_card_table[host->id].bcsrstatus)
		? 1 : 0;
}

static inline int au1xmmc_card_readonly(struct au1xmmc_host *host)
{
	return (bcsr->status & au1xmmc_card_table[host->id].wpstatus)
		? 1 : 0;
}

static void au1xmmc_finish_request(struct au1xmmc_host *host)
{

	struct mmc_request *mrq = host->mrq;

	host->mrq = NULL;
	host->flags &= HOST_F_ACTIVE;

	host->dma.len = 0;
	host->dma.dir = 0;

	host->pio.index  = 0;
	host->pio.offset = 0;
	host->pio.len = 0;

	host->status = HOST_S_IDLE;

	bcsr->disk_leds |= (1 << 8);

	mmc_request_done(host->mmc, mrq);
}

static void au1xmmc_tasklet_finish(unsigned long param)
{
	struct au1xmmc_host *host = (struct au1xmmc_host *) param;
	au1xmmc_finish_request(host);
}

static int au1xmmc_send_command(struct au1xmmc_host *host, int wait,
				struct mmc_command *cmd)
{

	u32 mmccmd = (cmd->opcode << SD_CMD_CI_SHIFT);

	switch (mmc_resp_type(cmd)) {
	case MMC_RSP_R1:
		mmccmd |= SD_CMD_RT_1;
		break;
	case MMC_RSP_R1B:
		mmccmd |= SD_CMD_RT_1B;
		break;
	case MMC_RSP_R2:
		mmccmd |= SD_CMD_RT_2;
		break;
	case MMC_RSP_R3:
		mmccmd |= SD_CMD_RT_3;
		break;
	}

	switch(cmd->opcode) {
	case MMC_READ_SINGLE_BLOCK:
	case SD_APP_SEND_SCR:
		mmccmd |= SD_CMD_CT_2;
		break;
	case MMC_READ_MULTIPLE_BLOCK:
		mmccmd |= SD_CMD_CT_4;
		break;
	case MMC_WRITE_BLOCK:
		mmccmd |= SD_CMD_CT_1;
		break;

	case MMC_WRITE_MULTIPLE_BLOCK:
		mmccmd |= SD_CMD_CT_3;
		break;
	case MMC_STOP_TRANSMISSION:
		mmccmd |= SD_CMD_CT_7;
		break;
	}

	au_writel(cmd->arg, HOST_CMDARG(host));
	au_sync();

	if (wait)
		IRQ_OFF(host, SD_CONFIG_CR);

	au_writel((mmccmd | SD_CMD_GO), HOST_CMD(host));
	au_sync();

	/* Wait for the command to go on the line */

	while(1) {
		if (!(au_readl(HOST_CMD(host)) & SD_CMD_GO))
			break;
	}

	/* Wait for the command to come back */

	if (wait) {
		u32 status = au_readl(HOST_STATUS(host));

		while(!(status & SD_STATUS_CR))
			status = au_readl(HOST_STATUS(host));

		/* Clear the CR status */
		au_writel(SD_STATUS_CR, HOST_STATUS(host));

		IRQ_ON(host, SD_CONFIG_CR);
	}

	return MMC_ERR_NONE;
}

static void au1xmmc_data_complete(struct au1xmmc_host *host, u32 status)
{

	struct mmc_request *mrq = host->mrq;
	struct mmc_data *data;
	u32 crc;

	WARN_ON(host->status != HOST_S_DATA && host->status != HOST_S_STOP);

	if (host->mrq == NULL)
		return;

	data = mrq->cmd->data;

	if (status == 0)
		status = au_readl(HOST_STATUS(host));

	/* The transaction is really over when the SD_STATUS_DB bit is clear */

	while((host->flags & HOST_F_XMIT) && (status & SD_STATUS_DB))
		status = au_readl(HOST_STATUS(host));

	data->error = MMC_ERR_NONE;
	dma_unmap_sg(mmc_dev(host->mmc), data->sg, data->sg_len, host->dma.dir);

        /* Process any errors */

	crc = (status & (SD_STATUS_WC | SD_STATUS_RC));
	if (host->flags & HOST_F_XMIT)
		crc |= ((status & 0x07) == 0x02) ? 0 : 1;

	if (crc)
		data->error = MMC_ERR_BADCRC;

	/* Clear the CRC bits */
	au_writel(SD_STATUS_WC | SD_STATUS_RC, HOST_STATUS(host));

	data->bytes_xfered = 0;

	if (data->error == MMC_ERR_NONE) {
		if (host->flags & HOST_F_DMA) {
			u32 chan = DMA_CHANNEL(host);

			chan_tab_t *c = *((chan_tab_t **) chan);
			au1x_dma_chan_t *cp = c->chan_ptr;
			data->bytes_xfered = cp->ddma_bytecnt;
		}
		else
			data->bytes_xfered =
				(data->blocks * (1 << data->blksz_bits)) -
				host->pio.len;
	}

	au1xmmc_finish_request(host);
}

static void au1xmmc_tasklet_data(unsigned long param)
{
	struct au1xmmc_host *host = (struct au1xmmc_host *) param;

	u32 status = au_readl(HOST_STATUS(host));
	au1xmmc_data_complete(host, status);
}

#define AU1XMMC_MAX_TRANSFER 8

static void au1xmmc_send_pio(struct au1xmmc_host *host)
{

	struct mmc_data *data = 0;
	int sg_len, max, count = 0;
	unsigned char *sg_ptr;
	u32 status = 0;
	struct scatterlist *sg;

	data = host->mrq->data;

	if (!(host->flags & HOST_F_XMIT))
		return;

	/* This is the pointer to the data buffer */
	sg = &data->sg[host->pio.index];
	sg_ptr = page_address(sg->page) + sg->offset + host->pio.offset;

	/* This is the space left inside the buffer */
	sg_len = data->sg[host->pio.index].length - host->pio.offset;

	/* Check to if we need less then the size of the sg_buffer */

	max = (sg_len > host->pio.len) ? host->pio.len : sg_len;
	if (max > AU1XMMC_MAX_TRANSFER) max = AU1XMMC_MAX_TRANSFER;

	for(count = 0; count < max; count++ ) {
		unsigned char val;

		status = au_readl(HOST_STATUS(host));

		if (!(status & SD_STATUS_TH))
			break;

		val = *sg_ptr++;

		au_writel((unsigned long) val, HOST_TXPORT(host));
		au_sync();
	}

	host->pio.len -= count;
	host->pio.offset += count;

	if (count == sg_len) {
		host->pio.index++;
		host->pio.offset = 0;
	}

	if (host->pio.len == 0) {
		IRQ_OFF(host, SD_CONFIG_TH);

		if (host->flags & HOST_F_STOP)
			SEND_STOP(host);

		tasklet_schedule(&host->data_task);
	}
}

static void au1xmmc_receive_pio(struct au1xmmc_host *host)
{

	struct mmc_data *data = 0;
	int sg_len = 0, max = 0, count = 0;
	unsigned char *sg_ptr = 0;
	u32 status = 0;
	struct scatterlist *sg;

	data = host->mrq->data;

	if (!(host->flags & HOST_F_RECV))
		return;

	max = host->pio.len;

	if (host->pio.index < host->dma.len) {
		sg = &data->sg[host->pio.index];
		sg_ptr = page_address(sg->page) + sg->offset + host->pio.offset;

		/* This is the space left inside the buffer */
		sg_len = sg_dma_len(&data->sg[host->pio.index]) - host->pio.offset;

		/* Check to if we need less then the size of the sg_buffer */
		if (sg_len < max) max = sg_len;
	}

	if (max > AU1XMMC_MAX_TRANSFER)
		max = AU1XMMC_MAX_TRANSFER;

	for(count = 0; count < max; count++ ) {
		u32 val;
		status = au_readl(HOST_STATUS(host));

		if (!(status & SD_STATUS_NE))
			break;

		if (status & SD_STATUS_RC) {
			DEBUG("RX CRC Error [%d + %d].\n", host->id,
					host->pio.len, count);
			break;
		}

		if (status & SD_STATUS_RO) {
			DEBUG("RX Overrun [%d + %d]\n", host->id,
					host->pio.len, count);
			break;
		}
		else if (status & SD_STATUS_RU) {
			DEBUG("RX Underrun [%d + %d]\n", host->id,
					host->pio.len,	count);
			break;
		}

		val = au_readl(HOST_RXPORT(host));

		if (sg_ptr)
			*sg_ptr++ = (unsigned char) (val & 0xFF);
	}

	host->pio.len -= count;
	host->pio.offset += count;

	if (sg_len && count == sg_len) {
		host->pio.index++;
		host->pio.offset = 0;
	}

	if (host->pio.len == 0) {
		//IRQ_OFF(host, SD_CONFIG_RA | SD_CONFIG_RF);
		IRQ_OFF(host, SD_CONFIG_NE);

		if (host->flags & HOST_F_STOP)
			SEND_STOP(host);

		tasklet_schedule(&host->data_task);
	}
}

/* static void au1xmmc_cmd_complete
   This is called when a command has been completed - grab the response
   and check for errors.  Then start the data transfer if it is indicated.
*/

static void au1xmmc_cmd_complete(struct au1xmmc_host *host, u32 status)
{

	struct mmc_request *mrq = host->mrq;
	struct mmc_command *cmd;
	int trans;

	if (!host->mrq)
		return;

	cmd = mrq->cmd;
	cmd->error = MMC_ERR_NONE;

	if (cmd->flags & MMC_RSP_PRESENT) {
		if (cmd->flags & MMC_RSP_136) {
			u32 r[4];
			int i;

			r[0] = au_readl(host->iobase + SD_RESP3);
			r[1] = au_readl(host->iobase + SD_RESP2);
			r[2] = au_readl(host->iobase + SD_RESP1);
			r[3] = au_readl(host->iobase + SD_RESP0);

			/* The CRC is omitted from the response, so really
			 * we only got 120 bytes, but the engine expects
			 * 128 bits, so we have to shift things up
			 */

			for(i = 0; i < 4; i++) {
				cmd->resp[i] = (r[i] & 0x00FFFFFF) << 8;
				if (i != 3)
					cmd->resp[i] |= (r[i + 1] & 0xFF000000) >> 24;
			}
		} else {
			/* Techincally, we should be getting all 48 bits of
			 * the response (SD_RESP1 + SD_RESP2), but because
			 * our response omits the CRC, our data ends up
			 * being shifted 8 bits to the right.  In this case,
			 * that means that the OSR data starts at bit 31,
			 * so we can just read RESP0 and return that
			 */
			cmd->resp[0] = au_readl(host->iobase + SD_RESP0);
		}
	}

        /* Figure out errors */

	if (status & (SD_STATUS_SC | SD_STATUS_WC | SD_STATUS_RC))
		cmd->error = MMC_ERR_BADCRC;

	trans = host->flags & (HOST_F_XMIT | HOST_F_RECV);

	if (!trans || cmd->error != MMC_ERR_NONE) {

		IRQ_OFF(host, SD_CONFIG_TH | SD_CONFIG_RA|SD_CONFIG_RF);
		tasklet_schedule(&host->finish_task);
		return;
	}

	host->status = HOST_S_DATA;

	if (host->flags & HOST_F_DMA) {
		u32 channel = DMA_CHANNEL(host);

		/* Start the DMA as soon as the buffer gets something in it */

		if (host->flags & HOST_F_RECV) {
			u32 mask = SD_STATUS_DB | SD_STATUS_NE;

			while((status & mask) != mask)
				status = au_readl(HOST_STATUS(host));
		}

		au1xxx_dbdma_start(channel);
	}
}

static void au1xmmc_set_clock(struct au1xmmc_host *host, int rate)
{

	unsigned int pbus = get_au1x00_speed();
	unsigned int divisor;
	u32 config;

	/* From databook:
	   divisor = ((((cpuclock / sbus_divisor) / 2) / mmcclock) / 2) - 1
	*/

	pbus /= ((au_readl(SYS_POWERCTRL) & 0x3) + 2);
	pbus /= 2;

	divisor = ((pbus / rate) / 2) - 1;

	config = au_readl(HOST_CONFIG(host));

	config &= ~(SD_CONFIG_DIV);
	config |= (divisor & SD_CONFIG_DIV) | SD_CONFIG_DE;

	au_writel(config, HOST_CONFIG(host));
	au_sync();
}

static int
au1xmmc_prepare_data(struct au1xmmc_host *host, struct mmc_data *data)
{

	int datalen = data->blocks * (1 << data->blksz_bits);

	if (dma != 0)
		host->flags |= HOST_F_DMA;

	if (data->flags & MMC_DATA_READ)
		host->flags |= HOST_F_RECV;
	else
		host->flags |= HOST_F_XMIT;

	if (host->mrq->stop)
		host->flags |= HOST_F_STOP;

	host->dma.dir = DMA_BIDIRECTIONAL;

	host->dma.len = dma_map_sg(mmc_dev(host->mmc), data->sg,
				   data->sg_len, host->dma.dir);

	if (host->dma.len == 0)
		return MMC_ERR_TIMEOUT;

	au_writel((1 << data->blksz_bits) - 1, HOST_BLKSIZE(host));

	if (host->flags & HOST_F_DMA) {
		int i;
		u32 channel = DMA_CHANNEL(host);

		au1xxx_dbdma_stop(channel);

		for(i = 0; i < host->dma.len; i++) {
			u32 ret = 0, flags = DDMA_FLAGS_NOIE;
			struct scatterlist *sg = &data->sg[i];
			int sg_len = sg->length;

			int len = (datalen > sg_len) ? sg_len : datalen;

			if (i == host->dma.len - 1)
				flags = DDMA_FLAGS_IE;

    			if (host->flags & HOST_F_XMIT){
      				ret = au1xxx_dbdma_put_source_flags(channel,
					(void *) (page_address(sg->page) +
						  sg->offset),
					len, flags);
			}
    			else {
      				ret = au1xxx_dbdma_put_dest_flags(channel,
					(void *) (page_address(sg->page) +
						  sg->offset),
					len, flags);
			}

    			if (!ret)
				goto dataerr;

			datalen -= len;
		}
	}
	else {
		host->pio.index = 0;
		host->pio.offset = 0;
		host->pio.len = datalen;

		if (host->flags & HOST_F_XMIT)
			IRQ_ON(host, SD_CONFIG_TH);
		else
			IRQ_ON(host, SD_CONFIG_NE);
			//IRQ_ON(host, SD_CONFIG_RA|SD_CONFIG_RF);
	}

	return MMC_ERR_NONE;

 dataerr:
	dma_unmap_sg(mmc_dev(host->mmc),data->sg,data->sg_len,host->dma.dir);
	return MMC_ERR_TIMEOUT;
}

/* static void au1xmmc_request
   This actually starts a command or data transaction
*/

static void au1xmmc_request(struct mmc_host* mmc, struct mmc_request* mrq)
{

	struct au1xmmc_host *host = mmc_priv(mmc);
	int ret = MMC_ERR_NONE;

	WARN_ON(irqs_disabled());
	WARN_ON(host->status != HOST_S_IDLE);

	host->mrq = mrq;
	host->status = HOST_S_CMD;

	bcsr->disk_leds &= ~(1 << 8);

	if (mrq->data) {
		FLUSH_FIFO(host);
		ret = au1xmmc_prepare_data(host, mrq->data);
	}

	if (ret == MMC_ERR_NONE)
		ret = au1xmmc_send_command(host, 0, mrq->cmd);

	if (ret != MMC_ERR_NONE) {
		mrq->cmd->error = ret;
		au1xmmc_finish_request(host);
	}
}

static void au1xmmc_reset_controller(struct au1xmmc_host *host)
{

	/* Apply the clock */
	au_writel(SD_ENABLE_CE, HOST_ENABLE(host));
        au_sync_delay(1);

	au_writel(SD_ENABLE_R | SD_ENABLE_CE, HOST_ENABLE(host));
	au_sync_delay(5);

	au_writel(~0, HOST_STATUS(host));
	au_sync();

	au_writel(0, HOST_BLKSIZE(host));
	au_writel(0x001fffff, HOST_TIMEOUT(host));
	au_sync();

	au_writel(SD_CONFIG2_EN, HOST_CONFIG2(host));
        au_sync();

	au_writel(SD_CONFIG2_EN | SD_CONFIG2_FF, HOST_CONFIG2(host));
	au_sync_delay(1);

	au_writel(SD_CONFIG2_EN, HOST_CONFIG2(host));
	au_sync();

	/* Configure interrupts */
	au_writel(AU1XMMC_INTERRUPTS, HOST_CONFIG(host));
	au_sync();
}


static void au1xmmc_set_ios(struct mmc_host* mmc, struct mmc_ios* ios)
{
	struct au1xmmc_host *host = mmc_priv(mmc);

	DEBUG("set_ios (power=%u, clock=%uHz, vdd=%u, mode=%u)\n",
	      host->id, ios->power_mode, ios->clock, ios->vdd,
	      ios->bus_mode);

	if (ios->power_mode == MMC_POWER_OFF)
		au1xmmc_set_power(host, 0);
	else if (ios->power_mode == MMC_POWER_ON) {
		au1xmmc_set_power(host, 1);
	}

	if (ios->clock && ios->clock != host->clock) {
		au1xmmc_set_clock(host, ios->clock);
		host->clock = ios->clock;
	}
}

static void au1xmmc_dma_callback(int irq, void *dev_id, struct pt_regs *regs)
{
	struct au1xmmc_host *host = (struct au1xmmc_host *) dev_id;

	/* Avoid spurious interrupts */

	if (!host->mrq)
		return;

	if (host->flags & HOST_F_STOP)
		SEND_STOP(host);

	tasklet_schedule(&host->data_task);
}

#define STATUS_TIMEOUT (SD_STATUS_RAT | SD_STATUS_DT)
#define STATUS_DATA_IN  (SD_STATUS_NE)
#define STATUS_DATA_OUT (SD_STATUS_TH)

static irqreturn_t au1xmmc_irq(int irq, void *dev_id, struct pt_regs *regs)
{

	u32 status;
	int i, ret = 0;

	disable_irq(AU1100_SD_IRQ);

	for(i = 0; i < AU1XMMC_CONTROLLER_COUNT; i++) {
		struct au1xmmc_host * host = au1xmmc_hosts[i];
		u32 handled = 1;

		status = au_readl(HOST_STATUS(host));

		if (host->mrq && (status & STATUS_TIMEOUT)) {
			if (status & SD_STATUS_RAT)
				host->mrq->cmd->error = MMC_ERR_TIMEOUT;

			else if (status & SD_STATUS_DT)
				host->mrq->data->error = MMC_ERR_TIMEOUT;

			/* In PIO mode, interrupts might still be enabled */
			IRQ_OFF(host, SD_CONFIG_NE | SD_CONFIG_TH);

			//IRQ_OFF(host, SD_CONFIG_TH|SD_CONFIG_RA|SD_CONFIG_RF);
			tasklet_schedule(&host->finish_task);
		}
#if 0
		else if (status & SD_STATUS_DD) {

			/* Sometimes we get a DD before a NE in PIO mode */

			if (!(host->flags & HOST_F_DMA) &&
					(status & SD_STATUS_NE))
				au1xmmc_receive_pio(host);
			else {
				au1xmmc_data_complete(host, status);
				//tasklet_schedule(&host->data_task);
			}
		}
#endif
		else if (status & (SD_STATUS_CR)) {
			if (host->status == HOST_S_CMD)
				au1xmmc_cmd_complete(host,status);
		}
		else if (!(host->flags & HOST_F_DMA)) {
			if ((host->flags & HOST_F_XMIT) &&
			    (status & STATUS_DATA_OUT))
				au1xmmc_send_pio(host);
			else if ((host->flags & HOST_F_RECV) &&
			    (status & STATUS_DATA_IN))
				au1xmmc_receive_pio(host);
		}
		else if (status & 0x203FBC70) {
			DEBUG("Unhandled status %8.8x\n", host->id, status);
			handled = 0;
		}

		au_writel(status, HOST_STATUS(host));
		au_sync();

		ret |= handled;
	}

	enable_irq(AU1100_SD_IRQ);
	return ret;
}

static void au1xmmc_poll_event(unsigned long arg)
{
	struct au1xmmc_host *host = (struct au1xmmc_host *) arg;

	int card = au1xmmc_card_inserted(host);
        int controller = (host->flags & HOST_F_ACTIVE) ? 1 : 0;

	if (card != controller) {
		host->flags &= ~HOST_F_ACTIVE;
		if (card) host->flags |= HOST_F_ACTIVE;
		mmc_detect_change(host->mmc, 0);
	}

	if (host->mrq != NULL) {
		u32 status = au_readl(HOST_STATUS(host));
		DEBUG("PENDING - %8.8x\n", host->id, status);
	}

	mod_timer(&host->timer, jiffies + AU1XMMC_DETECT_TIMEOUT);
}

static dbdev_tab_t au1xmmc_mem_dbdev =
{
	DSCR_CMD0_ALWAYS, DEV_FLAGS_ANYUSE, 0, 8, 0x00000000, 0, 0
};

static void au1xmmc_init_dma(struct au1xmmc_host *host)
{

	u32 rxchan, txchan;

	int txid = au1xmmc_card_table[host->id].tx_devid;
	int rxid = au1xmmc_card_table[host->id].rx_devid;

	/* DSCR_CMD0_ALWAYS has a stride of 32 bits, we need a stride
	   of 8 bits.  And since devices are shared, we need to create
	   our own to avoid freaking out other devices
	*/

	int memid = au1xxx_ddma_add_device(&au1xmmc_mem_dbdev);

	txchan = au1xxx_dbdma_chan_alloc(memid, txid,
					 au1xmmc_dma_callback, (void *) host);

	rxchan = au1xxx_dbdma_chan_alloc(rxid, memid,
					 au1xmmc_dma_callback, (void *) host);

	au1xxx_dbdma_set_devwidth(txchan, 8);
	au1xxx_dbdma_set_devwidth(rxchan, 8);

	au1xxx_dbdma_ring_alloc(txchan, AU1XMMC_DESCRIPTOR_COUNT);
	au1xxx_dbdma_ring_alloc(rxchan, AU1XMMC_DESCRIPTOR_COUNT);

	host->tx_chan = txchan;
	host->rx_chan = rxchan;
}

struct mmc_host_ops au1xmmc_ops = {
	.request	= au1xmmc_request,
	.set_ios	= au1xmmc_set_ios,
};

static int __devinit au1xmmc_probe(struct platform_device *pdev)
{

	int i, ret = 0;

	/* THe interrupt is shared among all controllers */
	ret = request_irq(AU1100_SD_IRQ, au1xmmc_irq, SA_INTERRUPT, "MMC", 0);

	if (ret) {
		printk(DRIVER_NAME "ERROR: Couldn't get int %d: %d\n",
				AU1100_SD_IRQ, ret);
		return -ENXIO;
	}

	disable_irq(AU1100_SD_IRQ);

	for(i = 0; i < AU1XMMC_CONTROLLER_COUNT; i++) {
		struct mmc_host *mmc = mmc_alloc_host(sizeof(struct au1xmmc_host), &pdev->dev);
		struct au1xmmc_host *host = 0;

		if (!mmc) {
			printk(DRIVER_NAME "ERROR: no mem for host %d\n", i);
			au1xmmc_hosts[i] = 0;
			continue;
		}

		mmc->ops = &au1xmmc_ops;

		mmc->f_min =   450000;
		mmc->f_max = 24000000;

		mmc->max_seg_size = AU1XMMC_DESCRIPTOR_SIZE;
		mmc->max_phys_segs = AU1XMMC_DESCRIPTOR_COUNT;

		mmc->ocr_avail = AU1XMMC_OCR;

		host = mmc_priv(mmc);
		host->mmc = mmc;

		host->id = i;
		host->iobase = au1xmmc_card_table[host->id].iobase;
		host->clock = 0;
		host->power_mode = MMC_POWER_OFF;

		host->flags = au1xmmc_card_inserted(host) ? HOST_F_ACTIVE : 0;
		host->status = HOST_S_IDLE;

		init_timer(&host->timer);

		host->timer.function = au1xmmc_poll_event;
		host->timer.data = (unsigned long) host;
		host->timer.expires = jiffies + AU1XMMC_DETECT_TIMEOUT;

		tasklet_init(&host->data_task, au1xmmc_tasklet_data,
				(unsigned long) host);

		tasklet_init(&host->finish_task, au1xmmc_tasklet_finish,
				(unsigned long) host);

		spin_lock_init(&host->lock);

		if (dma != 0)
			au1xmmc_init_dma(host);

		au1xmmc_reset_controller(host);

		mmc_add_host(mmc);
		au1xmmc_hosts[i] = host;

		add_timer(&host->timer);

		printk(KERN_INFO DRIVER_NAME ": MMC Controller %d set up at %8.8X (mode=%s)\n",
		       host->id, host->iobase, dma ? "dma" : "pio");
	}

	enable_irq(AU1100_SD_IRQ);

	return 0;
}

static int __devexit au1xmmc_remove(struct platform_device *pdev)
{

	int i;

	disable_irq(AU1100_SD_IRQ);

	for(i = 0; i < AU1XMMC_CONTROLLER_COUNT; i++) {
		struct au1xmmc_host *host = au1xmmc_hosts[i];
		if (!host) continue;

		tasklet_kill(&host->data_task);
		tasklet_kill(&host->finish_task);

		del_timer_sync(&host->timer);
		au1xmmc_set_power(host, 0);

		mmc_remove_host(host->mmc);

		au1xxx_dbdma_chan_free(host->tx_chan);
		au1xxx_dbdma_chan_free(host->rx_chan);

		au_writel(0x0, HOST_ENABLE(host));
		au_sync();
	}

	free_irq(AU1100_SD_IRQ, 0);
	return 0;
}

static struct platform_driver au1xmmc_driver = {
	.probe         = au1xmmc_probe,
	.remove        = au1xmmc_remove,
	.suspend       = NULL,
	.resume        = NULL,
	.driver        = {
		.name  = DRIVER_NAME,
	},
};

static int __init au1xmmc_init(void)
{
	return platform_driver_register(&au1xmmc_driver);
}

static void __exit au1xmmc_exit(void)
{
	platform_driver_unregister(&au1xmmc_driver);
}

module_init(au1xmmc_init);
module_exit(au1xmmc_exit);

#ifdef MODULE
MODULE_AUTHOR("Advanced Micro Devices, Inc");
MODULE_DESCRIPTION("MMC/SD driver for the Alchemy Au1XXX");
MODULE_LICENSE("GPL");
#endif

