/*
 *  linux/drivers/mmc/wbsd.c - Winbond W83L51xD SD/MMC driver
 *
 *  Copyright (C) 2004-2005 Pierre Ossman, All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 *
 * Warning!
 *
 * Changes to the FIFO system should be done with extreme care since
 * the hardware is full of bugs related to the FIFO. Known issues are:
 *
 * - FIFO size field in FSR is always zero.
 *
 * - FIFO interrupts tend not to work as they should. Interrupts are
 *   triggered only for full/empty events, not for threshold values.
 *
 * - On APIC systems the FIFO empty interrupt is sometimes lost.
 */

#include <linux/config.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/device.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/highmem.h>
#include <linux/mmc/host.h>
#include <linux/mmc/protocol.h>

#include <asm/io.h>
#include <asm/dma.h>
#include <asm/scatterlist.h>

#include "wbsd.h"

#define DRIVER_NAME "wbsd"
#define DRIVER_VERSION "1.1"

#ifdef CONFIG_MMC_DEBUG
#define DBG(x...) \
	printk(KERN_DEBUG DRIVER_NAME ": " x)
#define DBGF(f, x...) \
	printk(KERN_DEBUG DRIVER_NAME " [%s()]: " f, __func__ , ##x)
#else
#define DBG(x...)	do { } while (0)
#define DBGF(x...)	do { } while (0)
#endif

static unsigned int io = 0x248;
static unsigned int irq = 6;
static int dma = 2;

#ifdef CONFIG_MMC_DEBUG
void DBG_REG(int reg, u8 value)
{
	int i;
	
	printk(KERN_DEBUG "wbsd: Register %d: 0x%02X %3d '%c' ",
		reg, (int)value, (int)value, (value < 0x20)?'.':value);
	
	for (i = 7;i >= 0;i--)
	{
		if (value & (1 << i))
			printk("x");
		else
			printk(".");
	}
	
	printk("\n");
}
#else
#define DBG_REG(r, v) do {}  while (0)
#endif

/*
 * Basic functions
 */

static inline void wbsd_unlock_config(struct wbsd_host* host)
{
	outb(host->unlock_code, host->config);
	outb(host->unlock_code, host->config);
}

static inline void wbsd_lock_config(struct wbsd_host* host)
{
	outb(LOCK_CODE, host->config);
}

static inline void wbsd_write_config(struct wbsd_host* host, u8 reg, u8 value)
{
	outb(reg, host->config);
	outb(value, host->config + 1);
}

static inline u8 wbsd_read_config(struct wbsd_host* host, u8 reg)
{
	outb(reg, host->config);
	return inb(host->config + 1);
}

static inline void wbsd_write_index(struct wbsd_host* host, u8 index, u8 value)
{
	outb(index, host->base + WBSD_IDXR);
	outb(value, host->base + WBSD_DATAR);
}

static inline u8 wbsd_read_index(struct wbsd_host* host, u8 index)
{
	outb(index, host->base + WBSD_IDXR);
	return inb(host->base + WBSD_DATAR);
}

/*
 * Common routines
 */

static void wbsd_init_device(struct wbsd_host* host)
{
	u8 setup, ier;
	
	/*
	 * Reset chip (SD/MMC part) and fifo.
	 */
	setup = wbsd_read_index(host, WBSD_IDX_SETUP);
	setup |= WBSD_FIFO_RESET | WBSD_SOFT_RESET;
	wbsd_write_index(host, WBSD_IDX_SETUP, setup);
	
	/*
	 * Read back default clock.
	 */
	host->clk = wbsd_read_index(host, WBSD_IDX_CLK);

	/*
	 * Power down port.
	 */
	outb(WBSD_POWER_N, host->base + WBSD_CSR);
	
	/*
	 * Set maximum timeout.
	 */
	wbsd_write_index(host, WBSD_IDX_TAAC, 0x7F);
	
	/*
	 * Enable interesting interrupts.
	 */
	ier = 0;
	ier |= WBSD_EINT_CARD;
	ier |= WBSD_EINT_FIFO_THRE;
	ier |= WBSD_EINT_CCRC;
	ier |= WBSD_EINT_TIMEOUT;
	ier |= WBSD_EINT_CRC;
	ier |= WBSD_EINT_TC;

	outb(ier, host->base + WBSD_EIR);

	/*
	 * Clear interrupts.
	 */
	inb(host->base + WBSD_ISR);
}

static void wbsd_reset(struct wbsd_host* host)
{
	u8 setup;
	
	printk(KERN_ERR DRIVER_NAME ": Resetting chip\n");
	
	/*
	 * Soft reset of chip (SD/MMC part).
	 */
	setup = wbsd_read_index(host, WBSD_IDX_SETUP);
	setup |= WBSD_SOFT_RESET;
	wbsd_write_index(host, WBSD_IDX_SETUP, setup);
}

static void wbsd_request_end(struct wbsd_host* host, struct mmc_request* mrq)
{
	unsigned long dmaflags;
	
	DBGF("Ending request, cmd (%x)\n", mrq->cmd->opcode);
	
	if (host->dma >= 0)
	{
		/*
		 * Release ISA DMA controller.
		 */
		dmaflags = claim_dma_lock();
		disable_dma(host->dma);
		clear_dma_ff(host->dma);
		release_dma_lock(dmaflags);

		/*
		 * Disable DMA on host.
		 */
		wbsd_write_index(host, WBSD_IDX_DMA, 0);
	}
	
	host->mrq = NULL;

	/*
	 * MMC layer might call back into the driver so first unlock.
	 */
	spin_unlock(&host->lock);
	mmc_request_done(host->mmc, mrq);
	spin_lock(&host->lock);
}

/*
 * Scatter/gather functions
 */

static inline void wbsd_init_sg(struct wbsd_host* host, struct mmc_data* data)
{
	/*
	 * Get info. about SG list from data structure.
	 */
	host->cur_sg = data->sg;
	host->num_sg = data->sg_len;

	host->offset = 0;
	host->remain = host->cur_sg->length;
}

static inline int wbsd_next_sg(struct wbsd_host* host)
{
	/*
	 * Skip to next SG entry.
	 */
	host->cur_sg++;
	host->num_sg--;

	/*
	 * Any entries left?
	 */
	if (host->num_sg > 0)
	  {
	    host->offset = 0;
	    host->remain = host->cur_sg->length;
	  }
	
	return host->num_sg;
}

static inline char* wbsd_kmap_sg(struct wbsd_host* host)
{
	host->mapped_sg = kmap_atomic(host->cur_sg->page, KM_BIO_SRC_IRQ) +
		host->cur_sg->offset;
	return host->mapped_sg;
}

static inline void wbsd_kunmap_sg(struct wbsd_host* host)
{
	kunmap_atomic(host->mapped_sg, KM_BIO_SRC_IRQ);
}

static inline void wbsd_sg_to_dma(struct wbsd_host* host, struct mmc_data* data)
{
	unsigned int len, i, size;
	struct scatterlist* sg;
	char* dmabuf = host->dma_buffer;
	char* sgbuf;
	
	size = host->size;
	
	sg = data->sg;
	len = data->sg_len;
	
	/*
	 * Just loop through all entries. Size might not
	 * be the entire list though so make sure that
	 * we do not transfer too much.
	 */
	for (i = 0;i < len;i++)
	{
		sgbuf = kmap_atomic(sg[i].page, KM_BIO_SRC_IRQ) + sg[i].offset;
		if (size < sg[i].length)
			memcpy(dmabuf, sgbuf, size);
		else
			memcpy(dmabuf, sgbuf, sg[i].length);
		kunmap_atomic(sgbuf, KM_BIO_SRC_IRQ);
		dmabuf += sg[i].length;
		
		if (size < sg[i].length)
			size = 0;
		else
			size -= sg[i].length;
	
		if (size == 0)
			break;
	}
	
	/*
	 * Check that we didn't get a request to transfer
	 * more data than can fit into the SG list.
	 */
	
	BUG_ON(size != 0);
	
	host->size -= size;
}

static inline void wbsd_dma_to_sg(struct wbsd_host* host, struct mmc_data* data)
{
	unsigned int len, i, size;
	struct scatterlist* sg;
	char* dmabuf = host->dma_buffer;
	char* sgbuf;
	
	size = host->size;
	
	sg = data->sg;
	len = data->sg_len;
	
	/*
	 * Just loop through all entries. Size might not
	 * be the entire list though so make sure that
	 * we do not transfer too much.
	 */
	for (i = 0;i < len;i++)
	{
		sgbuf = kmap_atomic(sg[i].page, KM_BIO_SRC_IRQ) + sg[i].offset;
		if (size < sg[i].length)
			memcpy(sgbuf, dmabuf, size);
		else
			memcpy(sgbuf, dmabuf, sg[i].length);
		kunmap_atomic(sgbuf, KM_BIO_SRC_IRQ);
		dmabuf += sg[i].length;
		
		if (size < sg[i].length)
			size = 0;
		else
			size -= sg[i].length;
		
		if (size == 0)
			break;
	}
	
	/*
	 * Check that we didn't get a request to transfer
	 * more data than can fit into the SG list.
	 */
	
	BUG_ON(size != 0);
	
	host->size -= size;
}

/*
 * Command handling
 */
 
static inline void wbsd_get_short_reply(struct wbsd_host* host,
	struct mmc_command* cmd)
{
	/*
	 * Correct response type?
	 */
	if (wbsd_read_index(host, WBSD_IDX_RSPLEN) != WBSD_RSP_SHORT)
	{
		cmd->error = MMC_ERR_INVALID;
		return;
	}
	
	cmd->resp[0] =
		wbsd_read_index(host, WBSD_IDX_RESP12) << 24;
	cmd->resp[0] |=
		wbsd_read_index(host, WBSD_IDX_RESP13) << 16;
	cmd->resp[0] |=
		wbsd_read_index(host, WBSD_IDX_RESP14) << 8;
	cmd->resp[0] |=
		wbsd_read_index(host, WBSD_IDX_RESP15) << 0;
	cmd->resp[1] =
		wbsd_read_index(host, WBSD_IDX_RESP16) << 24;
}

static inline void wbsd_get_long_reply(struct wbsd_host* host,
	struct mmc_command* cmd)
{
	int i;
	
	/*
	 * Correct response type?
	 */
	if (wbsd_read_index(host, WBSD_IDX_RSPLEN) != WBSD_RSP_LONG)
	{
		cmd->error = MMC_ERR_INVALID;
		return;
	}
	
	for (i = 0;i < 4;i++)
	{
		cmd->resp[i] =
			wbsd_read_index(host, WBSD_IDX_RESP1 + i * 4) << 24;
		cmd->resp[i] |=
			wbsd_read_index(host, WBSD_IDX_RESP2 + i * 4) << 16;
		cmd->resp[i] |=
			wbsd_read_index(host, WBSD_IDX_RESP3 + i * 4) << 8;
		cmd->resp[i] |=
			wbsd_read_index(host, WBSD_IDX_RESP4 + i * 4) << 0;
	}
}

static irqreturn_t wbsd_irq(int irq, void *dev_id, struct pt_regs *regs);

static void wbsd_send_command(struct wbsd_host* host, struct mmc_command* cmd)
{
	int i;
	u8 status, isr;
	
	DBGF("Sending cmd (%x)\n", cmd->opcode);

	/*
	 * Clear accumulated ISR. The interrupt routine
	 * will fill this one with events that occur during
	 * transfer.
	 */
	host->isr = 0;
	
	/*
	 * Send the command (CRC calculated by host).
	 */
	outb(cmd->opcode, host->base + WBSD_CMDR);
	for (i = 3;i >= 0;i--)
		outb((cmd->arg >> (i * 8)) & 0xff, host->base + WBSD_CMDR);
	
	cmd->error = MMC_ERR_NONE;
	
	/*
	 * Wait for the request to complete.
	 */
	do {
		status = wbsd_read_index(host, WBSD_IDX_STATUS);
	} while (status & WBSD_CARDTRAFFIC);

	/*
	 * Do we expect a reply?
	 */
	if ((cmd->flags & MMC_RSP_MASK) != MMC_RSP_NONE)
	{
		/*
		 * Read back status.
		 */
		isr = host->isr;
		
		/* Card removed? */
		if (isr & WBSD_INT_CARD)
			cmd->error = MMC_ERR_TIMEOUT;
		/* Timeout? */
		else if (isr & WBSD_INT_TIMEOUT)
			cmd->error = MMC_ERR_TIMEOUT;
		/* CRC? */
		else if ((cmd->flags & MMC_RSP_CRC) && (isr & WBSD_INT_CRC))
			cmd->error = MMC_ERR_BADCRC;
		/* All ok */
		else
		{
			if ((cmd->flags & MMC_RSP_MASK) == MMC_RSP_SHORT)
				wbsd_get_short_reply(host, cmd);
			else
				wbsd_get_long_reply(host, cmd);
		}
	}

	DBGF("Sent cmd (%x), res %d\n", cmd->opcode, cmd->error);
}

/*
 * Data functions
 */

static void wbsd_empty_fifo(struct wbsd_host* host)
{
	struct mmc_data* data = host->mrq->cmd->data;
	char* buffer;
	int i, fsr, fifo;
	
	/*
	 * Handle excessive data.
	 */
	if (data->bytes_xfered == host->size)
		return;
	
	buffer = wbsd_kmap_sg(host) + host->offset;

	/*
	 * Drain the fifo. This has a tendency to loop longer
	 * than the FIFO length (usually one block).
	 */
	while (!((fsr = inb(host->base + WBSD_FSR)) & WBSD_FIFO_EMPTY))
	{
		/*
		 * The size field in the FSR is broken so we have to
		 * do some guessing.
		 */		
		if (fsr & WBSD_FIFO_FULL)
			fifo = 16;
		else if (fsr & WBSD_FIFO_FUTHRE)
			fifo = 8;
		else
			fifo = 1;
		
		for (i = 0;i < fifo;i++)
		{
			*buffer = inb(host->base + WBSD_DFR);
			buffer++;
			host->offset++;
			host->remain--;

			data->bytes_xfered++;
			
			/*
			 * Transfer done?
			 */
			if (data->bytes_xfered == host->size)
			{
				wbsd_kunmap_sg(host);				
				return;
			}
			
			/*
			 * End of scatter list entry?
			 */
			if (host->remain == 0)
			{
				wbsd_kunmap_sg(host);
				
				/*
				 * Get next entry. Check if last.
				 */
				if (!wbsd_next_sg(host))
				{
					/*
					 * We should never reach this point.
					 * It means that we're trying to
					 * transfer more blocks than can fit
					 * into the scatter list.
					 */
					BUG_ON(1);
					
					host->size = data->bytes_xfered;
					
					return;
				}
				
				buffer = wbsd_kmap_sg(host);
			}
		}
	}
	
	wbsd_kunmap_sg(host);

	/*
	 * This is a very dirty hack to solve a
	 * hardware problem. The chip doesn't trigger
	 * FIFO threshold interrupts properly.
	 */
	if ((host->size - data->bytes_xfered) < 16)
		tasklet_schedule(&host->fifo_tasklet);
}

static void wbsd_fill_fifo(struct wbsd_host* host)
{
	struct mmc_data* data = host->mrq->cmd->data;
	char* buffer;
	int i, fsr, fifo;
	
	/*
	 * Check that we aren't being called after the
	 * entire buffer has been transfered.
	 */
	if (data->bytes_xfered == host->size)
		return;

	buffer = wbsd_kmap_sg(host) + host->offset;

	/*
	 * Fill the fifo. This has a tendency to loop longer
	 * than the FIFO length (usually one block).
	 */
	while (!((fsr = inb(host->base + WBSD_FSR)) & WBSD_FIFO_FULL))
	{
		/*
		 * The size field in the FSR is broken so we have to
		 * do some guessing.
		 */		
		if (fsr & WBSD_FIFO_EMPTY)
			fifo = 0;
		else if (fsr & WBSD_FIFO_EMTHRE)
			fifo = 8;
		else
			fifo = 15;

		for (i = 16;i > fifo;i--)
		{
			outb(*buffer, host->base + WBSD_DFR);
			buffer++;
			host->offset++;
			host->remain--;
			
			data->bytes_xfered++;
			
			/*
			 * Transfer done?
			 */
			if (data->bytes_xfered == host->size)
			{
				wbsd_kunmap_sg(host);
				return;
			}

			/*
			 * End of scatter list entry?
			 */
			if (host->remain == 0)
			{
				wbsd_kunmap_sg(host);
				
				/*
				 * Get next entry. Check if last.
				 */
				if (!wbsd_next_sg(host))
				{
					/*
					 * We should never reach this point.
					 * It means that we're trying to
					 * transfer more blocks than can fit
					 * into the scatter list.
					 */
					BUG_ON(1);
					
					host->size = data->bytes_xfered;
					
					return;
				}
				
				buffer = wbsd_kmap_sg(host);
			}
		}
	}
	
	wbsd_kunmap_sg(host);
}

static void wbsd_prepare_data(struct wbsd_host* host, struct mmc_data* data)
{
	u16 blksize;
	u8 setup;
	unsigned long dmaflags;

	DBGF("blksz %04x blks %04x flags %08x\n",
		1 << data->blksz_bits, data->blocks, data->flags);
	DBGF("tsac %d ms nsac %d clk\n",
		data->timeout_ns / 1000000, data->timeout_clks);
	
	/*
	 * Calculate size.
	 */
	host->size = data->blocks << data->blksz_bits;

	/*
	 * Check timeout values for overflow.
	 * (Yes, some cards cause this value to overflow).
	 */
	if (data->timeout_ns > 127000000)
		wbsd_write_index(host, WBSD_IDX_TAAC, 127);
	else
		wbsd_write_index(host, WBSD_IDX_TAAC, data->timeout_ns/1000000);
	
	if (data->timeout_clks > 255)
		wbsd_write_index(host, WBSD_IDX_NSAC, 255);
	else
		wbsd_write_index(host, WBSD_IDX_NSAC, data->timeout_clks);
	
	/*
	 * Inform the chip of how large blocks will be
	 * sent. It needs this to determine when to
	 * calculate CRC.
	 *
	 * Space for CRC must be included in the size.
	 */
	blksize = (1 << data->blksz_bits) + 2;
	
	wbsd_write_index(host, WBSD_IDX_PBSMSB, (blksize >> 4) & 0xF0);
	wbsd_write_index(host, WBSD_IDX_PBSLSB, blksize & 0xFF);

	/*
	 * Clear the FIFO. This is needed even for DMA
	 * transfers since the chip still uses the FIFO
	 * internally.
	 */
	setup = wbsd_read_index(host, WBSD_IDX_SETUP);
	setup |= WBSD_FIFO_RESET;
	wbsd_write_index(host, WBSD_IDX_SETUP, setup);
	
	/*
	 * DMA transfer?
	 */
	if (host->dma >= 0)
	{	
		/*
		 * The buffer for DMA is only 64 kB.
		 */
		BUG_ON(host->size > 0x10000);
		if (host->size > 0x10000)
		{
			data->error = MMC_ERR_INVALID;
			return;
		}
		
		/*
		 * Transfer data from the SG list to
		 * the DMA buffer.
		 */
		if (data->flags & MMC_DATA_WRITE)
			wbsd_sg_to_dma(host, data);
		
		/*
		 * Initialise the ISA DMA controller.
		 */	
		dmaflags = claim_dma_lock();
		disable_dma(host->dma);
		clear_dma_ff(host->dma);
		if (data->flags & MMC_DATA_READ)
			set_dma_mode(host->dma, DMA_MODE_READ & ~0x40);
		else
			set_dma_mode(host->dma, DMA_MODE_WRITE & ~0x40);
		set_dma_addr(host->dma, host->dma_addr);
		set_dma_count(host->dma, host->size);

		enable_dma(host->dma);
		release_dma_lock(dmaflags);

		/*
		 * Enable DMA on the host.
		 */
		wbsd_write_index(host, WBSD_IDX_DMA, WBSD_DMA_ENABLE);
	}
	else
	{
		/*
		 * This flag is used to keep printk
		 * output to a minimum.
		 */
		host->firsterr = 1;
		
		/*
		 * Initialise the SG list.
		 */
		wbsd_init_sg(host, data);
	
		/*
		 * Turn off DMA.
		 */
		wbsd_write_index(host, WBSD_IDX_DMA, 0);
	
		/*
		 * Set up FIFO threshold levels (and fill
		 * buffer if doing a write).
		 */
		if (data->flags & MMC_DATA_READ)
		{
			wbsd_write_index(host, WBSD_IDX_FIFOEN,
				WBSD_FIFOEN_FULL | 8);
		}
		else
		{
			wbsd_write_index(host, WBSD_IDX_FIFOEN,
				WBSD_FIFOEN_EMPTY | 8);
			wbsd_fill_fifo(host);
		}
	}	
		
	data->error = MMC_ERR_NONE;
}

static void wbsd_finish_data(struct wbsd_host* host, struct mmc_data* data)
{
	unsigned long dmaflags;
	int count;
	u8 status;
	
	WARN_ON(host->mrq == NULL);

	/*
	 * Send a stop command if needed.
	 */
	if (data->stop)
		wbsd_send_command(host, data->stop);

	/*
	 * Wait for the controller to leave data
	 * transfer state.
	 */
	do
	{
		status = wbsd_read_index(host, WBSD_IDX_STATUS);
	} while (status & (WBSD_BLOCK_READ | WBSD_BLOCK_WRITE));
	
	/*
	 * DMA transfer?
	 */
	if (host->dma >= 0)
	{
		/*
		 * Disable DMA on the host.
		 */
		wbsd_write_index(host, WBSD_IDX_DMA, 0);
		
		/*
		 * Turn of ISA DMA controller.
		 */
		dmaflags = claim_dma_lock();
		disable_dma(host->dma);
		clear_dma_ff(host->dma);
		count = get_dma_residue(host->dma);
		release_dma_lock(dmaflags);
		
		/*
		 * Any leftover data?
		 */
		if (count)
		{
			printk(KERN_ERR DRIVER_NAME ": Incomplete DMA "
				"transfer. %d bytes left.\n", count);
			
			data->error = MMC_ERR_FAILED;
		}
		else
		{
			/*
			 * Transfer data from DMA buffer to
			 * SG list.
			 */
			if (data->flags & MMC_DATA_READ)
				wbsd_dma_to_sg(host, data);
			
			data->bytes_xfered = host->size;
		}
	}
	
	DBGF("Ending data transfer (%d bytes)\n", data->bytes_xfered);
	
	wbsd_request_end(host, host->mrq);
}

/*
 * MMC Callbacks
 */

static void wbsd_request(struct mmc_host* mmc, struct mmc_request* mrq)
{
	struct wbsd_host* host = mmc_priv(mmc);
	struct mmc_command* cmd;

	/*
	 * Disable tasklets to avoid a deadlock.
	 */
	spin_lock_bh(&host->lock);

	BUG_ON(host->mrq != NULL);

	cmd = mrq->cmd;

	host->mrq = mrq;
	
	/*
	 * If there is no card in the slot then
	 * timeout immediatly.
	 */
	if (!(inb(host->base + WBSD_CSR) & WBSD_CARDPRESENT))
	{
		cmd->error = MMC_ERR_TIMEOUT;
		goto done;
	}

	/*
	 * Does the request include data?
	 */
	if (cmd->data)
	{
		wbsd_prepare_data(host, cmd->data);
		
		if (cmd->data->error != MMC_ERR_NONE)
			goto done;
	}
	
	wbsd_send_command(host, cmd);

	/*
	 * If this is a data transfer the request
	 * will be finished after the data has
	 * transfered.
	 */	
	if (cmd->data && (cmd->error == MMC_ERR_NONE))
	{
		/*
		 * Dirty fix for hardware bug.
		 */
		if (host->dma == -1)
			tasklet_schedule(&host->fifo_tasklet);

		spin_unlock_bh(&host->lock);

		return;
	}
		
done:
	wbsd_request_end(host, mrq);

	spin_unlock_bh(&host->lock);
}

static void wbsd_set_ios(struct mmc_host* mmc, struct mmc_ios* ios)
{
	struct wbsd_host* host = mmc_priv(mmc);
	u8 clk, setup, pwr;
	
	DBGF("clock %uHz busmode %u powermode %u Vdd %u\n",
		ios->clock, ios->bus_mode, ios->power_mode, ios->vdd);

	spin_lock_bh(&host->lock);

	/*
	 * Reset the chip on each power off.
	 * Should clear out any weird states.
	 */
	if (ios->power_mode == MMC_POWER_OFF)
		wbsd_init_device(host);
	
	if (ios->clock >= 24000000)
		clk = WBSD_CLK_24M;
	else if (ios->clock >= 16000000)
		clk = WBSD_CLK_16M;
	else if (ios->clock >= 12000000)
		clk = WBSD_CLK_12M;
	else
		clk = WBSD_CLK_375K;

	/*
	 * Only write to the clock register when
	 * there is an actual change.
	 */
	if (clk != host->clk)
	{
		wbsd_write_index(host, WBSD_IDX_CLK, clk);
		host->clk = clk;
	}

	if (ios->power_mode != MMC_POWER_OFF)
	{
		/*
		 * Power up card.
		 */
		pwr = inb(host->base + WBSD_CSR);
		pwr &= ~WBSD_POWER_N;
		outb(pwr, host->base + WBSD_CSR);

		/*
		 * This behaviour is stolen from the
		 * Windows driver. Don't know why, but
		 * it is needed.
		 */
		setup = wbsd_read_index(host, WBSD_IDX_SETUP);
		if (ios->bus_mode == MMC_BUSMODE_OPENDRAIN)
			setup |= WBSD_DAT3_H;
		else
			setup &= ~WBSD_DAT3_H;
		wbsd_write_index(host, WBSD_IDX_SETUP, setup);

		mdelay(1);
	}

	spin_unlock_bh(&host->lock);
}

/*
 * Tasklets
 */

inline static struct mmc_data* wbsd_get_data(struct wbsd_host* host)
{
	WARN_ON(!host->mrq);
	if (!host->mrq)
		return NULL;

	WARN_ON(!host->mrq->cmd);
	if (!host->mrq->cmd)
		return NULL;

	WARN_ON(!host->mrq->cmd->data);
	if (!host->mrq->cmd->data)
		return NULL;
	
	return host->mrq->cmd->data;
}

static void wbsd_tasklet_card(unsigned long param)
{
	struct wbsd_host* host = (struct wbsd_host*)param;
	u8 csr;
	
	spin_lock(&host->lock);
	
	csr = inb(host->base + WBSD_CSR);
	WARN_ON(csr == 0xff);
	
	if (csr & WBSD_CARDPRESENT)
		DBG("Card inserted\n");
	else
	{
		DBG("Card removed\n");
		
		if (host->mrq)
		{
			printk(KERN_ERR DRIVER_NAME
				": Card removed during transfer!\n");
			wbsd_reset(host);
			
			host->mrq->cmd->error = MMC_ERR_FAILED;
			tasklet_schedule(&host->finish_tasklet);
		}
	}
	
	/*
	 * Unlock first since we might get a call back.
	 */
	spin_unlock(&host->lock);

	mmc_detect_change(host->mmc);
}

static void wbsd_tasklet_fifo(unsigned long param)
{
	struct wbsd_host* host = (struct wbsd_host*)param;
	struct mmc_data* data;
	
	spin_lock(&host->lock);
		
	if (!host->mrq)
		goto end;
	
	data = wbsd_get_data(host);
	if (!data)
		goto end;

	if (data->flags & MMC_DATA_WRITE)
		wbsd_fill_fifo(host);
	else
		wbsd_empty_fifo(host);

	/*
	 * Done?
	 */
	if (host->size == data->bytes_xfered)
	{
		wbsd_write_index(host, WBSD_IDX_FIFOEN, 0);
		tasklet_schedule(&host->finish_tasklet);
	}

end:	
	spin_unlock(&host->lock);
}

static void wbsd_tasklet_crc(unsigned long param)
{
	struct wbsd_host* host = (struct wbsd_host*)param;
	struct mmc_data* data;
	
	spin_lock(&host->lock);
	
	if (!host->mrq)
		goto end;
	
	data = wbsd_get_data(host);
	if (!data)
		goto end;
	
	DBGF("CRC error\n");

	data->error = MMC_ERR_BADCRC;
	
	tasklet_schedule(&host->finish_tasklet);

end:		
	spin_unlock(&host->lock);
}

static void wbsd_tasklet_timeout(unsigned long param)
{
	struct wbsd_host* host = (struct wbsd_host*)param;
	struct mmc_data* data;
	
	spin_lock(&host->lock);
	
	if (!host->mrq)
		goto end;
	
	data = wbsd_get_data(host);
	if (!data)
		goto end;
	
	DBGF("Timeout\n");

	data->error = MMC_ERR_TIMEOUT;
	
	tasklet_schedule(&host->finish_tasklet);

end:	
	spin_unlock(&host->lock);
}

static void wbsd_tasklet_finish(unsigned long param)
{
	struct wbsd_host* host = (struct wbsd_host*)param;
	struct mmc_data* data;
	
	spin_lock(&host->lock);
	
	WARN_ON(!host->mrq);
	if (!host->mrq)
		goto end;
	
	data = wbsd_get_data(host);
	if (!data)
		goto end;

	wbsd_finish_data(host, data);
	
end:	
	spin_unlock(&host->lock);
}

static void wbsd_tasklet_block(unsigned long param)
{
	struct wbsd_host* host = (struct wbsd_host*)param;
	struct mmc_data* data;
	
	spin_lock(&host->lock);

	if ((wbsd_read_index(host, WBSD_IDX_CRCSTATUS) & WBSD_CRC_MASK) !=
		WBSD_CRC_OK)
	{
		data = wbsd_get_data(host);
		if (!data)
			goto end;
		
		DBGF("CRC error\n");

		data->error = MMC_ERR_BADCRC;
	
		tasklet_schedule(&host->finish_tasklet);
	}

end:	
	spin_unlock(&host->lock);
}

/*
 * Interrupt handling
 */

static irqreturn_t wbsd_irq(int irq, void *dev_id, struct pt_regs *regs)
{
	struct wbsd_host* host = dev_id;
	int isr;
	
	isr = inb(host->base + WBSD_ISR);

	/*
	 * Was it actually our hardware that caused the interrupt?
	 */
	if (isr == 0xff || isr == 0x00)
		return IRQ_NONE;
	
	host->isr |= isr;

	/*
	 * Schedule tasklets as needed.
	 */
	if (isr & WBSD_INT_CARD)
		tasklet_schedule(&host->card_tasklet);
	if (isr & WBSD_INT_FIFO_THRE)
		tasklet_schedule(&host->fifo_tasklet);
	if (isr & WBSD_INT_CRC)
		tasklet_hi_schedule(&host->crc_tasklet);
	if (isr & WBSD_INT_TIMEOUT)
		tasklet_hi_schedule(&host->timeout_tasklet);
	if (isr & WBSD_INT_BUSYEND)
		tasklet_hi_schedule(&host->block_tasklet);
	if (isr & WBSD_INT_TC)
		tasklet_schedule(&host->finish_tasklet);
	
	return IRQ_HANDLED;
}

/*
 * Support functions for probe
 */

static int wbsd_scan(struct wbsd_host* host)
{
	int i, j, k;
	int id;
	
	/*
	 * Iterate through all ports, all codes to
	 * find hardware that is in our known list.
	 */
	for (i = 0;i < sizeof(config_ports)/sizeof(int);i++)
	{
		if (!request_region(config_ports[i], 2, DRIVER_NAME))
			continue;
			
		for (j = 0;j < sizeof(unlock_codes)/sizeof(int);j++)
		{
			id = 0xFFFF;
			
			outb(unlock_codes[j], config_ports[i]);
			outb(unlock_codes[j], config_ports[i]);
			
			outb(WBSD_CONF_ID_HI, config_ports[i]);
			id = inb(config_ports[i] + 1) << 8;

			outb(WBSD_CONF_ID_LO, config_ports[i]);
			id |= inb(config_ports[i] + 1);
			
			for (k = 0;k < sizeof(valid_ids)/sizeof(int);k++)
			{
				if (id == valid_ids[k])
				{				
					host->chip_id = id;
					host->config = config_ports[i];
					host->unlock_code = unlock_codes[i];
				
					return 0;
				}
			}
			
			if (id != 0xFFFF)
			{
				DBG("Unknown hardware (id %x) found at %x\n",
					id, config_ports[i]);
			}

			outb(LOCK_CODE, config_ports[i]);
		}
		
		release_region(config_ports[i], 2);
	}
	
	return -ENODEV;
}

static int wbsd_request_regions(struct wbsd_host* host)
{
	if (io & 0x7)
		return -EINVAL;
	
	if (!request_region(io, 8, DRIVER_NAME))
		return -EIO;
	
	host->base = io;
		
	return 0;
}

static void wbsd_release_regions(struct wbsd_host* host)
{
	if (host->base)
		release_region(host->base, 8);

	if (host->config)
		release_region(host->config, 2);
}

static void wbsd_init_dma(struct wbsd_host* host)
{
	host->dma = -1;
	
	if (dma < 0)
		return;
	
	if (request_dma(dma, DRIVER_NAME))
		goto err;
	
	/*
	 * We need to allocate a special buffer in
	 * order for ISA to be able to DMA to it.
	 */
	host->dma_buffer = kmalloc(65536,
		GFP_NOIO | GFP_DMA | __GFP_REPEAT | __GFP_NOWARN);
	if (!host->dma_buffer)
		goto free;

	/*
	 * Translate the address to a physical address.
	 */
	host->dma_addr = isa_virt_to_bus(host->dma_buffer);
			
	/*
	 * ISA DMA must be aligned on a 64k basis.
	 */
	if ((host->dma_addr & 0xffff) != 0)
		goto kfree;
	/*
	 * ISA cannot access memory above 16 MB.
	 */
	else if (host->dma_addr >= 0x1000000)
		goto kfree;

	host->dma = dma;
	
	return;
	
kfree:
	/*
	 * If we've gotten here then there is some kind of alignment bug
	 */
	BUG_ON(1);
	
	kfree(host->dma_buffer);
	host->dma_buffer = NULL;

free:
	free_dma(dma);

err:
	printk(KERN_WARNING DRIVER_NAME ": Unable to allocate DMA %d. "
		"Falling back on FIFO.\n", dma);
}

static struct mmc_host_ops wbsd_ops = {
	.request	= wbsd_request,
	.set_ios	= wbsd_set_ios,
};

/*
 * Device probe
 */

static int wbsd_probe(struct device* dev)
{
	struct wbsd_host* host = NULL;
	struct mmc_host* mmc = NULL;
	int ret;
	
	/*
	 * Allocate MMC structure.
	 */
	mmc = mmc_alloc_host(sizeof(struct wbsd_host), dev);
	if (!mmc)
		return -ENOMEM;
	
	host = mmc_priv(mmc);
	host->mmc = mmc;
	
	/*
	 * Scan for hardware.
	 */
	ret = wbsd_scan(host);
	if (ret)
		goto freemmc;

	/*
	 * Reset the chip.
	 */	
	wbsd_write_config(host, WBSD_CONF_SWRST, 1);
	wbsd_write_config(host, WBSD_CONF_SWRST, 0);

	/*
	 * Allocate I/O ports.
	 */
	ret = wbsd_request_regions(host);
	if (ret)
		goto release;

	/*
	 * Set host parameters.
	 */
	mmc->ops = &wbsd_ops;
	mmc->f_min = 375000;
	mmc->f_max = 24000000;
	mmc->ocr_avail = MMC_VDD_32_33|MMC_VDD_33_34;
	
	spin_lock_init(&host->lock);

	/*
	 * Select SD/MMC function.
	 */
	wbsd_write_config(host, WBSD_CONF_DEVICE, DEVICE_SD);
	
	/*
	 * Set up card detection.
	 */
	wbsd_write_config(host, WBSD_CONF_PINS, 0x02);
	
	/*
	 * Configure I/O port.
	 */
	wbsd_write_config(host, WBSD_CONF_PORT_HI, host->base >> 8);
	wbsd_write_config(host, WBSD_CONF_PORT_LO, host->base & 0xff);

	/*
	 * Allocate interrupt.
	 */
	ret = request_irq(irq, wbsd_irq, SA_SHIRQ, DRIVER_NAME, host);
	if (ret)
		goto release;
	
	host->irq = irq;
	
	/*
	 * Set up tasklets.
	 */
	tasklet_init(&host->card_tasklet, wbsd_tasklet_card, (unsigned long)host);
	tasklet_init(&host->fifo_tasklet, wbsd_tasklet_fifo, (unsigned long)host);
	tasklet_init(&host->crc_tasklet, wbsd_tasklet_crc, (unsigned long)host);
	tasklet_init(&host->timeout_tasklet, wbsd_tasklet_timeout, (unsigned long)host);
	tasklet_init(&host->finish_tasklet, wbsd_tasklet_finish, (unsigned long)host);
	tasklet_init(&host->block_tasklet, wbsd_tasklet_block, (unsigned long)host);
	
	/*
	 * Configure interrupt.
	 */
	wbsd_write_config(host, WBSD_CONF_IRQ, host->irq);
	
	/*
	 * Allocate DMA.
	 */
	wbsd_init_dma(host);
	
	/*
	 * If all went well, then configure DMA.
	 */
	if (host->dma >= 0)
		wbsd_write_config(host, WBSD_CONF_DRQ, host->dma);
	
	/*
	 * Maximum number of segments. Worst case is one sector per segment
	 * so this will be 64kB/512.
	 */
	mmc->max_hw_segs = 128;
	mmc->max_phys_segs = 128;
	
	/*
	 * Maximum number of sectors in one transfer. Also limited by 64kB
	 * buffer.
	 */
	mmc->max_sectors = 128;
	
	/*
	 * Maximum segment size. Could be one segment with the maximum number
	 * of segments.
	 */
	mmc->max_seg_size = mmc->max_sectors * 512;
	
	/*
	 * Enable chip.
	 */
	wbsd_write_config(host, WBSD_CONF_ENABLE, 1);
	
	/*
	 * Power up chip.
	 */
	wbsd_write_config(host, WBSD_CONF_POWER, 0x20);
	
	/*
	 * Power Management stuff. No idea how this works.
	 * Not tested.
	 */
#ifdef CONFIG_PM
	wbsd_write_config(host, WBSD_CONF_PME, 0xA0);
#endif

	/*
	 * Reset the chip into a known state.
	 */
	wbsd_init_device(host);
	
	dev_set_drvdata(dev, mmc);
	
	/*
	 * Add host to MMC layer.
	 */
	mmc_add_host(mmc);

	printk(KERN_INFO "%s: W83L51xD id %x at 0x%x irq %d dma %d\n",
		mmc->host_name, (int)host->chip_id, (int)host->base,
		(int)host->irq, (int)host->dma);

	return 0;

release:
	wbsd_release_regions(host);

freemmc:
	mmc_free_host(mmc);

	return ret;
}

/*
 * Device remove
 */

static int wbsd_remove(struct device* dev)
{
	struct mmc_host* mmc = dev_get_drvdata(dev);
	struct wbsd_host* host;
	
	if (!mmc)
		return 0;

	host = mmc_priv(mmc);
	
	/*
	 * Unregister host with MMC layer.
	 */
	mmc_remove_host(mmc);

	/*
	 * Power down the SD/MMC function.
	 */
	wbsd_unlock_config(host);
	wbsd_write_config(host, WBSD_CONF_DEVICE, DEVICE_SD);
	wbsd_write_config(host, WBSD_CONF_ENABLE, 0);
	wbsd_lock_config(host);
	
	/*
	 * Free resources.
	 */
	if (host->dma_buffer)
		kfree(host->dma_buffer);
	
	if (host->dma >= 0)
		free_dma(host->dma);

	free_irq(host->irq, host);
	
	tasklet_kill(&host->card_tasklet);
	tasklet_kill(&host->fifo_tasklet);
	tasklet_kill(&host->crc_tasklet);
	tasklet_kill(&host->timeout_tasklet);
	tasklet_kill(&host->finish_tasklet);
	tasklet_kill(&host->block_tasklet);
	
	wbsd_release_regions(host);
	
	mmc_free_host(mmc);

	return 0;
}

/*
 * Power management
 */

#ifdef CONFIG_PM
static int wbsd_suspend(struct device *dev, u32 state, u32 level)
{
	DBGF("Not yet supported\n");

	return 0;
}

static int wbsd_resume(struct device *dev, u32 level)
{
	DBGF("Not yet supported\n");

	return 0;
}
#else
#define wbsd_suspend NULL
#define wbsd_resume NULL
#endif

static void wbsd_release(struct device *dev)
{
}

static struct platform_device wbsd_device = {
	.name		= DRIVER_NAME,
	.id			= -1,
	.dev		= {
		.release = wbsd_release,
	},
};

static struct device_driver wbsd_driver = {
	.name		= DRIVER_NAME,
	.bus		= &platform_bus_type,
	.probe		= wbsd_probe,
	.remove		= wbsd_remove,
	
	.suspend	= wbsd_suspend,
	.resume		= wbsd_resume,
};

/*
 * Module loading/unloading
 */

static int __init wbsd_drv_init(void)
{
	int result;
	
	printk(KERN_INFO DRIVER_NAME
		": Winbond W83L51xD SD/MMC card interface driver, "
		DRIVER_VERSION "\n");
	printk(KERN_INFO DRIVER_NAME ": Copyright(c) Pierre Ossman\n");
	
	result = driver_register(&wbsd_driver);
	if (result < 0)
		return result;

	result = platform_device_register(&wbsd_device);
	if (result < 0)
		return result;

	return 0;
}

static void __exit wbsd_drv_exit(void)
{
	platform_device_unregister(&wbsd_device);
	
	driver_unregister(&wbsd_driver);

	DBG("unloaded\n");
}

module_init(wbsd_drv_init);
module_exit(wbsd_drv_exit);
module_param(io, uint, 0444);
module_param(irq, uint, 0444);
module_param(dma, int, 0444);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Winbond W83L51xD SD/MMC card interface driver");
MODULE_VERSION(DRIVER_VERSION);

MODULE_PARM_DESC(io, "I/O base to allocate. Must be 8 byte aligned. (default 0x248)");
MODULE_PARM_DESC(irq, "IRQ to allocate. (default 6)");
MODULE_PARM_DESC(dma, "DMA channel to allocate. -1 for no DMA. (default 2)");
