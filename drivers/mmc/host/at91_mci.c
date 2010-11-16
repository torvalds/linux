/*
 *  linux/drivers/mmc/host/at91_mci.c - ATMEL AT91 MCI Driver
 *
 *  Copyright (C) 2005 Cougar Creek Computing Devices Ltd, All Rights Reserved
 *
 *  Copyright (C) 2006 Malcolm Noyes
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

/*
   This is the AT91 MCI driver that has been tested with both MMC cards
   and SD-cards.  Boards that support write protect are now supported.
   The CCAT91SBC001 board does not support SD cards.

   The three entry points are at91_mci_request, at91_mci_set_ios
   and at91_mci_get_ro.

   SET IOS
     This configures the device to put it into the correct mode and clock speed
     required.

   MCI REQUEST
     MCI request processes the commands sent in the mmc_request structure. This
     can consist of a processing command and a stop command in the case of
     multiple block transfers.

     There are three main types of request, commands, reads and writes.

     Commands are straight forward. The command is submitted to the controller and
     the request function returns. When the controller generates an interrupt to indicate
     the command is finished, the response to the command are read and the mmc_request_done
     function called to end the request.

     Reads and writes work in a similar manner to normal commands but involve the PDC (DMA)
     controller to manage the transfers.

     A read is done from the controller directly to the scatterlist passed in from the request.
     Due to a bug in the AT91RM9200 controller, when a read is completed, all the words are byte
     swapped in the scatterlist buffers.  AT91SAM926x are not affected by this bug.

     The sequence of read interrupts is: ENDRX, RXBUFF, CMDRDY

     A write is slightly different in that the bytes to write are read from the scatterlist
     into a dma memory buffer (this is in case the source buffer should be read only). The
     entire write buffer is then done from this single dma memory buffer.

     The sequence of write interrupts is: ENDTX, TXBUFE, NOTBUSY, CMDRDY

   GET RO
     Gets the status of the write protect pin, if available.
*/

#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/blkdev.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/dma-mapping.h>
#include <linux/clk.h>
#include <linux/atmel_pdc.h>
#include <linux/gfp.h>
#include <linux/highmem.h>

#include <linux/mmc/host.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/gpio.h>

#include <mach/board.h>
#include <mach/cpu.h>
#include <mach/at91_mci.h>

#define DRIVER_NAME "at91_mci"

static inline int at91mci_is_mci1rev2xx(void)
{
	return (   cpu_is_at91sam9260()
		|| cpu_is_at91sam9263()
		|| cpu_is_at91cap9()
		|| cpu_is_at91sam9rl()
		|| cpu_is_at91sam9g10()
		|| cpu_is_at91sam9g20()
		);
}

#define FL_SENT_COMMAND	(1 << 0)
#define FL_SENT_STOP	(1 << 1)

#define AT91_MCI_ERRORS	(AT91_MCI_RINDE | AT91_MCI_RDIRE | AT91_MCI_RCRCE	\
		| AT91_MCI_RENDE | AT91_MCI_RTOE | AT91_MCI_DCRCE		\
		| AT91_MCI_DTOE | AT91_MCI_OVRE | AT91_MCI_UNRE)

#define at91_mci_read(host, reg)	__raw_readl((host)->baseaddr + (reg))
#define at91_mci_write(host, reg, val)	__raw_writel((val), (host)->baseaddr + (reg))

#define MCI_BLKSIZE 		512
#define MCI_MAXBLKSIZE 		4095
#define MCI_BLKATONCE 		256
#define MCI_BUFSIZE 		(MCI_BLKSIZE * MCI_BLKATONCE)

/*
 * Low level type for this driver
 */
struct at91mci_host
{
	struct mmc_host *mmc;
	struct mmc_command *cmd;
	struct mmc_request *request;

	void __iomem *baseaddr;
	int irq;

	struct at91_mmc_data *board;
	int present;

	struct clk *mci_clk;

	/*
	 * Flag indicating when the command has been sent. This is used to
	 * work out whether or not to send the stop
	 */
	unsigned int flags;
	/* flag for current bus settings */
	u32 bus_mode;

	/* DMA buffer used for transmitting */
	unsigned int* buffer;
	dma_addr_t physical_address;
	unsigned int total_length;

	/* Latest in the scatterlist that has been enabled for transfer, but not freed */
	int in_use_index;

	/* Latest in the scatterlist that has been enabled for transfer */
	int transfer_index;

	/* Timer for timeouts */
	struct timer_list timer;
};

/*
 * Reset the controller and restore most of the state
 */
static void at91_reset_host(struct at91mci_host *host)
{
	unsigned long flags;
	u32 mr;
	u32 sdcr;
	u32 dtor;
	u32 imr;

	local_irq_save(flags);
	imr = at91_mci_read(host, AT91_MCI_IMR);

	at91_mci_write(host, AT91_MCI_IDR, 0xffffffff);

	/* save current state */
	mr = at91_mci_read(host, AT91_MCI_MR) & 0x7fff;
	sdcr = at91_mci_read(host, AT91_MCI_SDCR);
	dtor = at91_mci_read(host, AT91_MCI_DTOR);

	/* reset the controller */
	at91_mci_write(host, AT91_MCI_CR, AT91_MCI_MCIDIS | AT91_MCI_SWRST);

	/* restore state */
	at91_mci_write(host, AT91_MCI_CR, AT91_MCI_MCIEN);
	at91_mci_write(host, AT91_MCI_MR, mr);
	at91_mci_write(host, AT91_MCI_SDCR, sdcr);
	at91_mci_write(host, AT91_MCI_DTOR, dtor);
	at91_mci_write(host, AT91_MCI_IER, imr);

	/* make sure sdio interrupts will fire */
	at91_mci_read(host, AT91_MCI_SR);

	local_irq_restore(flags);
}

static void at91_timeout_timer(unsigned long data)
{
	struct at91mci_host *host;

	host = (struct at91mci_host *)data;

	if (host->request) {
		dev_err(host->mmc->parent, "Timeout waiting end of packet\n");

		if (host->cmd && host->cmd->data) {
			host->cmd->data->error = -ETIMEDOUT;
		} else {
			if (host->cmd)
				host->cmd->error = -ETIMEDOUT;
			else
				host->request->cmd->error = -ETIMEDOUT;
		}

		at91_reset_host(host);
		mmc_request_done(host->mmc, host->request);
	}
}

/*
 * Copy from sg to a dma block - used for transfers
 */
static inline void at91_mci_sg_to_dma(struct at91mci_host *host, struct mmc_data *data)
{
	unsigned int len, i, size;
	unsigned *dmabuf = host->buffer;

	size = data->blksz * data->blocks;
	len = data->sg_len;

	/* MCI1 rev2xx Data Write Operation and number of bytes erratum */
	if (at91mci_is_mci1rev2xx())
		if (host->total_length == 12)
			memset(dmabuf, 0, 12);

	/*
	 * Just loop through all entries. Size might not
	 * be the entire list though so make sure that
	 * we do not transfer too much.
	 */
	for (i = 0; i < len; i++) {
		struct scatterlist *sg;
		int amount;
		unsigned int *sgbuffer;

		sg = &data->sg[i];

		sgbuffer = kmap_atomic(sg_page(sg), KM_BIO_SRC_IRQ) + sg->offset;
		amount = min(size, sg->length);
		size -= amount;

		if (cpu_is_at91rm9200()) {	/* AT91RM9200 errata */
			int index;

			for (index = 0; index < (amount / 4); index++)
				*dmabuf++ = swab32(sgbuffer[index]);
		} else {
			char *tmpv = (char *)dmabuf;
			memcpy(tmpv, sgbuffer, amount);
			tmpv += amount;
			dmabuf = (unsigned *)tmpv;
		}

		kunmap_atomic(sgbuffer, KM_BIO_SRC_IRQ);

		if (size == 0)
			break;
	}

	/*
	 * Check that we didn't get a request to transfer
	 * more data than can fit into the SG list.
	 */
	BUG_ON(size != 0);
}

/*
 * Handle after a dma read
 */
static void at91_mci_post_dma_read(struct at91mci_host *host)
{
	struct mmc_command *cmd;
	struct mmc_data *data;
	unsigned int len, i, size;
	unsigned *dmabuf = host->buffer;

	pr_debug("post dma read\n");

	cmd = host->cmd;
	if (!cmd) {
		pr_debug("no command\n");
		return;
	}

	data = cmd->data;
	if (!data) {
		pr_debug("no data\n");
		return;
	}

	size = data->blksz * data->blocks;
	len = data->sg_len;

	at91_mci_write(host, AT91_MCI_IDR, AT91_MCI_ENDRX);
	at91_mci_write(host, AT91_MCI_IER, AT91_MCI_RXBUFF);

	for (i = 0; i < len; i++) {
		struct scatterlist *sg;
		int amount;
		unsigned int *sgbuffer;

		sg = &data->sg[i];

		sgbuffer = kmap_atomic(sg_page(sg), KM_BIO_SRC_IRQ) + sg->offset;
		amount = min(size, sg->length);
		size -= amount;

		if (cpu_is_at91rm9200()) {	/* AT91RM9200 errata */
			int index;
			for (index = 0; index < (amount / 4); index++)
				sgbuffer[index] = swab32(*dmabuf++);
		} else {
			char *tmpv = (char *)dmabuf;
			memcpy(sgbuffer, tmpv, amount);
			tmpv += amount;
			dmabuf = (unsigned *)tmpv;
		}

		flush_kernel_dcache_page(sg_page(sg));
		kunmap_atomic(sgbuffer, KM_BIO_SRC_IRQ);
		data->bytes_xfered += amount;
		if (size == 0)
			break;
	}

	pr_debug("post dma read done\n");
}

/*
 * Handle transmitted data
 */
static void at91_mci_handle_transmitted(struct at91mci_host *host)
{
	struct mmc_command *cmd;
	struct mmc_data *data;

	pr_debug("Handling the transmit\n");

	/* Disable the transfer */
	at91_mci_write(host, ATMEL_PDC_PTCR, ATMEL_PDC_RXTDIS | ATMEL_PDC_TXTDIS);

	/* Now wait for cmd ready */
	at91_mci_write(host, AT91_MCI_IDR, AT91_MCI_TXBUFE);

	cmd = host->cmd;
	if (!cmd) return;

	data = cmd->data;
	if (!data) return;

	if (cmd->data->blocks > 1) {
		pr_debug("multiple write : wait for BLKE...\n");
		at91_mci_write(host, AT91_MCI_IER, AT91_MCI_BLKE);
	} else
		at91_mci_write(host, AT91_MCI_IER, AT91_MCI_NOTBUSY);
}

/*
 * Update bytes tranfered count during a write operation
 */
static void at91_mci_update_bytes_xfered(struct at91mci_host *host)
{
	struct mmc_data *data;

	/* always deal with the effective request (and not the current cmd) */

	if (host->request->cmd && host->request->cmd->error != 0)
		return;

	if (host->request->data) {
		data = host->request->data;
		if (data->flags & MMC_DATA_WRITE) {
			/* card is in IDLE mode now */
			pr_debug("-> bytes_xfered %d, total_length = %d\n",
				data->bytes_xfered, host->total_length);
			data->bytes_xfered = data->blksz * data->blocks;
		}
	}
}


/*Handle after command sent ready*/
static int at91_mci_handle_cmdrdy(struct at91mci_host *host)
{
	if (!host->cmd)
		return 1;
	else if (!host->cmd->data) {
		if (host->flags & FL_SENT_STOP) {
			/*After multi block write, we must wait for NOTBUSY*/
			at91_mci_write(host, AT91_MCI_IER, AT91_MCI_NOTBUSY);
		} else return 1;
	} else if (host->cmd->data->flags & MMC_DATA_WRITE) {
		/*After sendding multi-block-write command, start DMA transfer*/
		at91_mci_write(host, AT91_MCI_IER, AT91_MCI_TXBUFE | AT91_MCI_BLKE);
		at91_mci_write(host, ATMEL_PDC_PTCR, ATMEL_PDC_TXTEN);
	}

	/* command not completed, have to wait */
	return 0;
}


/*
 * Enable the controller
 */
static void at91_mci_enable(struct at91mci_host *host)
{
	unsigned int mr;

	at91_mci_write(host, AT91_MCI_CR, AT91_MCI_MCIEN);
	at91_mci_write(host, AT91_MCI_IDR, 0xffffffff);
	at91_mci_write(host, AT91_MCI_DTOR, AT91_MCI_DTOMUL_1M | AT91_MCI_DTOCYC);
	mr = AT91_MCI_PDCMODE | 0x34a;

	if (at91mci_is_mci1rev2xx())
		mr |= AT91_MCI_RDPROOF | AT91_MCI_WRPROOF;

	at91_mci_write(host, AT91_MCI_MR, mr);

	/* use Slot A or B (only one at same time) */
	at91_mci_write(host, AT91_MCI_SDCR, host->board->slot_b);
}

/*
 * Disable the controller
 */
static void at91_mci_disable(struct at91mci_host *host)
{
	at91_mci_write(host, AT91_MCI_CR, AT91_MCI_MCIDIS | AT91_MCI_SWRST);
}

/*
 * Send a command
 */
static void at91_mci_send_command(struct at91mci_host *host, struct mmc_command *cmd)
{
	unsigned int cmdr, mr;
	unsigned int block_length;
	struct mmc_data *data = cmd->data;

	unsigned int blocks;
	unsigned int ier = 0;

	host->cmd = cmd;

	/* Needed for leaving busy state before CMD1 */
	if ((at91_mci_read(host, AT91_MCI_SR) & AT91_MCI_RTOE) && (cmd->opcode == 1)) {
		pr_debug("Clearing timeout\n");
		at91_mci_write(host, AT91_MCI_ARGR, 0);
		at91_mci_write(host, AT91_MCI_CMDR, AT91_MCI_OPDCMD);
		while (!(at91_mci_read(host, AT91_MCI_SR) & AT91_MCI_CMDRDY)) {
			/* spin */
			pr_debug("Clearing: SR = %08X\n", at91_mci_read(host, AT91_MCI_SR));
		}
	}

	cmdr = cmd->opcode;

	if (mmc_resp_type(cmd) == MMC_RSP_NONE)
		cmdr |= AT91_MCI_RSPTYP_NONE;
	else {
		/* if a response is expected then allow maximum response latancy */
		cmdr |= AT91_MCI_MAXLAT;
		/* set 136 bit response for R2, 48 bit response otherwise */
		if (mmc_resp_type(cmd) == MMC_RSP_R2)
			cmdr |= AT91_MCI_RSPTYP_136;
		else
			cmdr |= AT91_MCI_RSPTYP_48;
	}

	if (data) {

		if (cpu_is_at91rm9200() || cpu_is_at91sam9261()) {
			if (data->blksz & 0x3) {
				pr_debug("Unsupported block size\n");
				cmd->error = -EINVAL;
				mmc_request_done(host->mmc, host->request);
				return;
			}
			if (data->flags & MMC_DATA_STREAM) {
				pr_debug("Stream commands not supported\n");
				cmd->error = -EINVAL;
				mmc_request_done(host->mmc, host->request);
				return;
			}
		}

		block_length = data->blksz;
		blocks = data->blocks;

		/* always set data start - also set direction flag for read */
		if (data->flags & MMC_DATA_READ)
			cmdr |= (AT91_MCI_TRDIR | AT91_MCI_TRCMD_START);
		else if (data->flags & MMC_DATA_WRITE)
			cmdr |= AT91_MCI_TRCMD_START;

		if (data->flags & MMC_DATA_STREAM)
			cmdr |= AT91_MCI_TRTYP_STREAM;
		if (data->blocks > 1)
			cmdr |= AT91_MCI_TRTYP_MULTIPLE;
	}
	else {
		block_length = 0;
		blocks = 0;
	}

	if (host->flags & FL_SENT_STOP)
		cmdr |= AT91_MCI_TRCMD_STOP;

	if (host->bus_mode == MMC_BUSMODE_OPENDRAIN)
		cmdr |= AT91_MCI_OPDCMD;

	/*
	 * Set the arguments and send the command
	 */
	pr_debug("Sending command %d as %08X, arg = %08X, blocks = %d, length = %d (MR = %08X)\n",
		cmd->opcode, cmdr, cmd->arg, blocks, block_length, at91_mci_read(host, AT91_MCI_MR));

	if (!data) {
		at91_mci_write(host, ATMEL_PDC_PTCR, ATMEL_PDC_TXTDIS | ATMEL_PDC_RXTDIS);
		at91_mci_write(host, ATMEL_PDC_RPR, 0);
		at91_mci_write(host, ATMEL_PDC_RCR, 0);
		at91_mci_write(host, ATMEL_PDC_RNPR, 0);
		at91_mci_write(host, ATMEL_PDC_RNCR, 0);
		at91_mci_write(host, ATMEL_PDC_TPR, 0);
		at91_mci_write(host, ATMEL_PDC_TCR, 0);
		at91_mci_write(host, ATMEL_PDC_TNPR, 0);
		at91_mci_write(host, ATMEL_PDC_TNCR, 0);
		ier = AT91_MCI_CMDRDY;
	} else {
		/* zero block length and PDC mode */
		mr = at91_mci_read(host, AT91_MCI_MR) & 0x5fff;
		mr |= (data->blksz & 0x3) ? AT91_MCI_PDCFBYTE : 0;
		mr |= (block_length << 16);
		mr |= AT91_MCI_PDCMODE;
		at91_mci_write(host, AT91_MCI_MR, mr);

		if (!(cpu_is_at91rm9200() || cpu_is_at91sam9261()))
			at91_mci_write(host, AT91_MCI_BLKR,
				AT91_MCI_BLKR_BCNT(blocks) |
				AT91_MCI_BLKR_BLKLEN(block_length));

		/*
		 * Disable the PDC controller
		 */
		at91_mci_write(host, ATMEL_PDC_PTCR, ATMEL_PDC_RXTDIS | ATMEL_PDC_TXTDIS);

		if (cmdr & AT91_MCI_TRCMD_START) {
			data->bytes_xfered = 0;
			host->transfer_index = 0;
			host->in_use_index = 0;
			if (cmdr & AT91_MCI_TRDIR) {
				/*
				 * Handle a read
				 */
				host->total_length = 0;

				at91_mci_write(host, ATMEL_PDC_RPR, host->physical_address);
				at91_mci_write(host, ATMEL_PDC_RCR, (data->blksz & 0x3) ?
					(blocks * block_length) : (blocks * block_length) / 4);
				at91_mci_write(host, ATMEL_PDC_RNPR, 0);
				at91_mci_write(host, ATMEL_PDC_RNCR, 0);

				ier = AT91_MCI_ENDRX /* | AT91_MCI_RXBUFF */;
			}
			else {
				/*
				 * Handle a write
				 */
				host->total_length = block_length * blocks;
				/*
				 * MCI1 rev2xx Data Write Operation and
				 * number of bytes erratum
				 */
				if (at91mci_is_mci1rev2xx())
					if (host->total_length < 12)
						host->total_length = 12;

				at91_mci_sg_to_dma(host, data);

				pr_debug("Transmitting %d bytes\n", host->total_length);

				at91_mci_write(host, ATMEL_PDC_TPR, host->physical_address);
				at91_mci_write(host, ATMEL_PDC_TCR, (data->blksz & 0x3) ?
						host->total_length : host->total_length / 4);

				ier = AT91_MCI_CMDRDY;
			}
		}
	}

	/*
	 * Send the command and then enable the PDC - not the other way round as
	 * the data sheet says
	 */

	at91_mci_write(host, AT91_MCI_ARGR, cmd->arg);
	at91_mci_write(host, AT91_MCI_CMDR, cmdr);

	if (cmdr & AT91_MCI_TRCMD_START) {
		if (cmdr & AT91_MCI_TRDIR)
			at91_mci_write(host, ATMEL_PDC_PTCR, ATMEL_PDC_RXTEN);
	}

	/* Enable selected interrupts */
	at91_mci_write(host, AT91_MCI_IER, AT91_MCI_ERRORS | ier);
}

/*
 * Process the next step in the request
 */
static void at91_mci_process_next(struct at91mci_host *host)
{
	if (!(host->flags & FL_SENT_COMMAND)) {
		host->flags |= FL_SENT_COMMAND;
		at91_mci_send_command(host, host->request->cmd);
	}
	else if ((!(host->flags & FL_SENT_STOP)) && host->request->stop) {
		host->flags |= FL_SENT_STOP;
		at91_mci_send_command(host, host->request->stop);
	} else {
		del_timer(&host->timer);
		/* the at91rm9200 mci controller hangs after some transfers,
		 * and the workaround is to reset it after each transfer.
		 */
		if (cpu_is_at91rm9200())
			at91_reset_host(host);
		mmc_request_done(host->mmc, host->request);
	}
}

/*
 * Handle a command that has been completed
 */
static void at91_mci_completed_command(struct at91mci_host *host, unsigned int status)
{
	struct mmc_command *cmd = host->cmd;
	struct mmc_data *data = cmd->data;

	at91_mci_write(host, AT91_MCI_IDR, 0xffffffff & ~(AT91_MCI_SDIOIRQA | AT91_MCI_SDIOIRQB));

	cmd->resp[0] = at91_mci_read(host, AT91_MCI_RSPR(0));
	cmd->resp[1] = at91_mci_read(host, AT91_MCI_RSPR(1));
	cmd->resp[2] = at91_mci_read(host, AT91_MCI_RSPR(2));
	cmd->resp[3] = at91_mci_read(host, AT91_MCI_RSPR(3));

	pr_debug("Status = %08X/%08x [%08X %08X %08X %08X]\n",
		 status, at91_mci_read(host, AT91_MCI_SR),
		 cmd->resp[0], cmd->resp[1], cmd->resp[2], cmd->resp[3]);

	if (status & AT91_MCI_ERRORS) {
		if ((status & AT91_MCI_RCRCE) && !(mmc_resp_type(cmd) & MMC_RSP_CRC)) {
			cmd->error = 0;
		}
		else {
			if (status & (AT91_MCI_DTOE | AT91_MCI_DCRCE)) {
				if (data) {
					if (status & AT91_MCI_DTOE)
						data->error = -ETIMEDOUT;
					else if (status & AT91_MCI_DCRCE)
						data->error = -EILSEQ;
				}
			} else {
				if (status & AT91_MCI_RTOE)
					cmd->error = -ETIMEDOUT;
				else if (status & AT91_MCI_RCRCE)
					cmd->error = -EILSEQ;
				else
					cmd->error = -EIO;
			}

			pr_debug("Error detected and set to %d/%d (cmd = %d, retries = %d)\n",
				cmd->error, data ? data->error : 0,
				 cmd->opcode, cmd->retries);
		}
	}
	else
		cmd->error = 0;

	at91_mci_process_next(host);
}

/*
 * Handle an MMC request
 */
static void at91_mci_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct at91mci_host *host = mmc_priv(mmc);
	host->request = mrq;
	host->flags = 0;

	/* more than 1s timeout needed with slow SD cards */
	mod_timer(&host->timer, jiffies +  msecs_to_jiffies(2000));

	at91_mci_process_next(host);
}

/*
 * Set the IOS
 */
static void at91_mci_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	int clkdiv;
	struct at91mci_host *host = mmc_priv(mmc);
	unsigned long at91_master_clock = clk_get_rate(host->mci_clk);

	host->bus_mode = ios->bus_mode;

	if (ios->clock == 0) {
		/* Disable the MCI controller */
		at91_mci_write(host, AT91_MCI_CR, AT91_MCI_MCIDIS);
		clkdiv = 0;
	}
	else {
		/* Enable the MCI controller */
		at91_mci_write(host, AT91_MCI_CR, AT91_MCI_MCIEN);

		if ((at91_master_clock % (ios->clock * 2)) == 0)
			clkdiv = ((at91_master_clock / ios->clock) / 2) - 1;
		else
			clkdiv = (at91_master_clock / ios->clock) / 2;

		pr_debug("clkdiv = %d. mcck = %ld\n", clkdiv,
			at91_master_clock / (2 * (clkdiv + 1)));
	}
	if (ios->bus_width == MMC_BUS_WIDTH_4 && host->board->wire4) {
		pr_debug("MMC: Setting controller bus width to 4\n");
		at91_mci_write(host, AT91_MCI_SDCR, at91_mci_read(host, AT91_MCI_SDCR) | AT91_MCI_SDCBUS);
	}
	else {
		pr_debug("MMC: Setting controller bus width to 1\n");
		at91_mci_write(host, AT91_MCI_SDCR, at91_mci_read(host, AT91_MCI_SDCR) & ~AT91_MCI_SDCBUS);
	}

	/* Set the clock divider */
	at91_mci_write(host, AT91_MCI_MR, (at91_mci_read(host, AT91_MCI_MR) & ~AT91_MCI_CLKDIV) | clkdiv);

	/* maybe switch power to the card */
	if (host->board->vcc_pin) {
		switch (ios->power_mode) {
			case MMC_POWER_OFF:
				gpio_set_value(host->board->vcc_pin, 0);
				break;
			case MMC_POWER_UP:
				gpio_set_value(host->board->vcc_pin, 1);
				break;
			case MMC_POWER_ON:
				break;
			default:
				WARN_ON(1);
		}
	}
}

/*
 * Handle an interrupt
 */
static irqreturn_t at91_mci_irq(int irq, void *devid)
{
	struct at91mci_host *host = devid;
	int completed = 0;
	unsigned int int_status, int_mask;

	int_status = at91_mci_read(host, AT91_MCI_SR);
	int_mask = at91_mci_read(host, AT91_MCI_IMR);

	pr_debug("MCI irq: status = %08X, %08X, %08X\n", int_status, int_mask,
		int_status & int_mask);

	int_status = int_status & int_mask;

	if (int_status & AT91_MCI_ERRORS) {
		completed = 1;

		if (int_status & AT91_MCI_UNRE)
			pr_debug("MMC: Underrun error\n");
		if (int_status & AT91_MCI_OVRE)
			pr_debug("MMC: Overrun error\n");
		if (int_status & AT91_MCI_DTOE)
			pr_debug("MMC: Data timeout\n");
		if (int_status & AT91_MCI_DCRCE)
			pr_debug("MMC: CRC error in data\n");
		if (int_status & AT91_MCI_RTOE)
			pr_debug("MMC: Response timeout\n");
		if (int_status & AT91_MCI_RENDE)
			pr_debug("MMC: Response end bit error\n");
		if (int_status & AT91_MCI_RCRCE)
			pr_debug("MMC: Response CRC error\n");
		if (int_status & AT91_MCI_RDIRE)
			pr_debug("MMC: Response direction error\n");
		if (int_status & AT91_MCI_RINDE)
			pr_debug("MMC: Response index error\n");
	} else {
		/* Only continue processing if no errors */

		if (int_status & AT91_MCI_TXBUFE) {
			pr_debug("TX buffer empty\n");
			at91_mci_handle_transmitted(host);
		}

		if (int_status & AT91_MCI_ENDRX) {
			pr_debug("ENDRX\n");
			at91_mci_post_dma_read(host);
		}

		if (int_status & AT91_MCI_RXBUFF) {
			pr_debug("RX buffer full\n");
			at91_mci_write(host, ATMEL_PDC_PTCR, ATMEL_PDC_RXTDIS | ATMEL_PDC_TXTDIS);
			at91_mci_write(host, AT91_MCI_IDR, AT91_MCI_RXBUFF | AT91_MCI_ENDRX);
			completed = 1;
		}

		if (int_status & AT91_MCI_ENDTX)
			pr_debug("Transmit has ended\n");

		if (int_status & AT91_MCI_NOTBUSY) {
			pr_debug("Card is ready\n");
			at91_mci_update_bytes_xfered(host);
			completed = 1;
		}

		if (int_status & AT91_MCI_DTIP)
			pr_debug("Data transfer in progress\n");

		if (int_status & AT91_MCI_BLKE) {
			pr_debug("Block transfer has ended\n");
			if (host->request->data && host->request->data->blocks > 1) {
				/* multi block write : complete multi write
				 * command and send stop */
				completed = 1;
			} else {
				at91_mci_write(host, AT91_MCI_IER, AT91_MCI_NOTBUSY);
			}
		}

		if (int_status & AT91_MCI_SDIOIRQA)
			mmc_signal_sdio_irq(host->mmc);

		if (int_status & AT91_MCI_SDIOIRQB)
			mmc_signal_sdio_irq(host->mmc);

		if (int_status & AT91_MCI_TXRDY)
			pr_debug("Ready to transmit\n");

		if (int_status & AT91_MCI_RXRDY)
			pr_debug("Ready to receive\n");

		if (int_status & AT91_MCI_CMDRDY) {
			pr_debug("Command ready\n");
			completed = at91_mci_handle_cmdrdy(host);
		}
	}

	if (completed) {
		pr_debug("Completed command\n");
		at91_mci_write(host, AT91_MCI_IDR, 0xffffffff & ~(AT91_MCI_SDIOIRQA | AT91_MCI_SDIOIRQB));
		at91_mci_completed_command(host, int_status);
	} else
		at91_mci_write(host, AT91_MCI_IDR, int_status & ~(AT91_MCI_SDIOIRQA | AT91_MCI_SDIOIRQB));

	return IRQ_HANDLED;
}

static irqreturn_t at91_mmc_det_irq(int irq, void *_host)
{
	struct at91mci_host *host = _host;
	int present = !gpio_get_value(irq_to_gpio(irq));

	/*
	 * we expect this irq on both insert and remove,
	 * and use a short delay to debounce.
	 */
	if (present != host->present) {
		host->present = present;
		pr_debug("%s: card %s\n", mmc_hostname(host->mmc),
			present ? "insert" : "remove");
		if (!present) {
			pr_debug("****** Resetting SD-card bus width ******\n");
			at91_mci_write(host, AT91_MCI_SDCR, at91_mci_read(host, AT91_MCI_SDCR) & ~AT91_MCI_SDCBUS);
		}
		/* 0.5s needed because of early card detect switch firing */
		mmc_detect_change(host->mmc, msecs_to_jiffies(500));
	}
	return IRQ_HANDLED;
}

static int at91_mci_get_ro(struct mmc_host *mmc)
{
	struct at91mci_host *host = mmc_priv(mmc);

	if (host->board->wp_pin)
		return !!gpio_get_value(host->board->wp_pin);
	/*
	 * Board doesn't support read only detection; let the mmc core
	 * decide what to do.
	 */
	return -ENOSYS;
}

static void at91_mci_enable_sdio_irq(struct mmc_host *mmc, int enable)
{
	struct at91mci_host *host = mmc_priv(mmc);

	pr_debug("%s: sdio_irq %c : %s\n", mmc_hostname(host->mmc),
		host->board->slot_b ? 'B':'A', enable ? "enable" : "disable");
	at91_mci_write(host, enable ? AT91_MCI_IER : AT91_MCI_IDR,
		host->board->slot_b ? AT91_MCI_SDIOIRQB : AT91_MCI_SDIOIRQA);

}

static const struct mmc_host_ops at91_mci_ops = {
	.request	= at91_mci_request,
	.set_ios	= at91_mci_set_ios,
	.get_ro		= at91_mci_get_ro,
	.enable_sdio_irq = at91_mci_enable_sdio_irq,
};

/*
 * Probe for the device
 */
static int __init at91_mci_probe(struct platform_device *pdev)
{
	struct mmc_host *mmc;
	struct at91mci_host *host;
	struct resource *res;
	int ret;

	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	if (!res)
		return -ENXIO;

	if (!request_mem_region(res->start, resource_size(res), DRIVER_NAME))
		return -EBUSY;

	mmc = mmc_alloc_host(sizeof(struct at91mci_host), &pdev->dev);
	if (!mmc) {
		ret = -ENOMEM;
		dev_dbg(&pdev->dev, "couldn't allocate mmc host\n");
		goto fail6;
	}

	mmc->ops = &at91_mci_ops;
	mmc->f_min = 375000;
	mmc->f_max = 25000000;
	mmc->ocr_avail = MMC_VDD_32_33 | MMC_VDD_33_34;
	mmc->caps = 0;

	mmc->max_blk_size  = MCI_MAXBLKSIZE;
	mmc->max_blk_count = MCI_BLKATONCE;
	mmc->max_req_size  = MCI_BUFSIZE;
	mmc->max_segs      = MCI_BLKATONCE;
	mmc->max_seg_size  = MCI_BUFSIZE;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->bus_mode = 0;
	host->board = pdev->dev.platform_data;
	if (host->board->wire4) {
		if (at91mci_is_mci1rev2xx())
			mmc->caps |= MMC_CAP_4_BIT_DATA;
		else
			dev_warn(&pdev->dev, "4 wire bus mode not supported"
				" - using 1 wire\n");
	}

	host->buffer = dma_alloc_coherent(&pdev->dev, MCI_BUFSIZE,
					&host->physical_address, GFP_KERNEL);
	if (!host->buffer) {
		ret = -ENOMEM;
		dev_err(&pdev->dev, "Can't allocate transmit buffer\n");
		goto fail5;
	}

	/* Add SDIO capability when available */
	if (at91mci_is_mci1rev2xx()) {
		/* at91mci MCI1 rev2xx sdio interrupt erratum */
		if (host->board->wire4 || !host->board->slot_b)
			mmc->caps |= MMC_CAP_SDIO_IRQ;
	}

	/*
	 * Reserve GPIOs ... board init code makes sure these pins are set
	 * up as GPIOs with the right direction (input, except for vcc)
	 */
	if (host->board->det_pin) {
		ret = gpio_request(host->board->det_pin, "mmc_detect");
		if (ret < 0) {
			dev_dbg(&pdev->dev, "couldn't claim card detect pin\n");
			goto fail4b;
		}
	}
	if (host->board->wp_pin) {
		ret = gpio_request(host->board->wp_pin, "mmc_wp");
		if (ret < 0) {
			dev_dbg(&pdev->dev, "couldn't claim wp sense pin\n");
			goto fail4;
		}
	}
	if (host->board->vcc_pin) {
		ret = gpio_request(host->board->vcc_pin, "mmc_vcc");
		if (ret < 0) {
			dev_dbg(&pdev->dev, "couldn't claim vcc switch pin\n");
			goto fail3;
		}
	}

	/*
	 * Get Clock
	 */
	host->mci_clk = clk_get(&pdev->dev, "mci_clk");
	if (IS_ERR(host->mci_clk)) {
		ret = -ENODEV;
		dev_dbg(&pdev->dev, "no mci_clk?\n");
		goto fail2;
	}

	/*
	 * Map I/O region
	 */
	host->baseaddr = ioremap(res->start, resource_size(res));
	if (!host->baseaddr) {
		ret = -ENOMEM;
		goto fail1;
	}

	/*
	 * Reset hardware
	 */
	clk_enable(host->mci_clk);		/* Enable the peripheral clock */
	at91_mci_disable(host);
	at91_mci_enable(host);

	/*
	 * Allocate the MCI interrupt
	 */
	host->irq = platform_get_irq(pdev, 0);
	ret = request_irq(host->irq, at91_mci_irq, IRQF_SHARED,
			mmc_hostname(mmc), host);
	if (ret) {
		dev_dbg(&pdev->dev, "request MCI interrupt failed\n");
		goto fail0;
	}

	setup_timer(&host->timer, at91_timeout_timer, (unsigned long)host);

	platform_set_drvdata(pdev, mmc);

	/*
	 * Add host to MMC layer
	 */
	if (host->board->det_pin) {
		host->present = !gpio_get_value(host->board->det_pin);
	}
	else
		host->present = -1;

	mmc_add_host(mmc);

	/*
	 * monitor card insertion/removal if we can
	 */
	if (host->board->det_pin) {
		ret = request_irq(gpio_to_irq(host->board->det_pin),
				at91_mmc_det_irq, 0, mmc_hostname(mmc), host);
		if (ret)
			dev_warn(&pdev->dev, "request MMC detect irq failed\n");
		else
			device_init_wakeup(&pdev->dev, 1);
	}

	pr_debug("Added MCI driver\n");

	return 0;

fail0:
	clk_disable(host->mci_clk);
	iounmap(host->baseaddr);
fail1:
	clk_put(host->mci_clk);
fail2:
	if (host->board->vcc_pin)
		gpio_free(host->board->vcc_pin);
fail3:
	if (host->board->wp_pin)
		gpio_free(host->board->wp_pin);
fail4:
	if (host->board->det_pin)
		gpio_free(host->board->det_pin);
fail4b:
	if (host->buffer)
		dma_free_coherent(&pdev->dev, MCI_BUFSIZE,
				host->buffer, host->physical_address);
fail5:
	mmc_free_host(mmc);
fail6:
	release_mem_region(res->start, resource_size(res));
	dev_err(&pdev->dev, "probe failed, err %d\n", ret);
	return ret;
}

/*
 * Remove a device
 */
static int __exit at91_mci_remove(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct at91mci_host *host;
	struct resource *res;

	if (!mmc)
		return -1;

	host = mmc_priv(mmc);

	if (host->buffer)
		dma_free_coherent(&pdev->dev, MCI_BUFSIZE,
				host->buffer, host->physical_address);

	if (host->board->det_pin) {
		if (device_can_wakeup(&pdev->dev))
			free_irq(gpio_to_irq(host->board->det_pin), host);
		device_init_wakeup(&pdev->dev, 0);
		gpio_free(host->board->det_pin);
	}

	at91_mci_disable(host);
	del_timer_sync(&host->timer);
	mmc_remove_host(mmc);
	free_irq(host->irq, host);

	clk_disable(host->mci_clk);			/* Disable the peripheral clock */
	clk_put(host->mci_clk);

	if (host->board->vcc_pin)
		gpio_free(host->board->vcc_pin);
	if (host->board->wp_pin)
		gpio_free(host->board->wp_pin);

	iounmap(host->baseaddr);
	res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	release_mem_region(res->start, resource_size(res));

	mmc_free_host(mmc);
	platform_set_drvdata(pdev, NULL);
	pr_debug("MCI Removed\n");

	return 0;
}

#ifdef CONFIG_PM
static int at91_mci_suspend(struct platform_device *pdev, pm_message_t state)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct at91mci_host *host = mmc_priv(mmc);
	int ret = 0;

	if (host->board->det_pin && device_may_wakeup(&pdev->dev))
		enable_irq_wake(host->board->det_pin);

	if (mmc)
		ret = mmc_suspend_host(mmc);

	return ret;
}

static int at91_mci_resume(struct platform_device *pdev)
{
	struct mmc_host *mmc = platform_get_drvdata(pdev);
	struct at91mci_host *host = mmc_priv(mmc);
	int ret = 0;

	if (host->board->det_pin && device_may_wakeup(&pdev->dev))
		disable_irq_wake(host->board->det_pin);

	if (mmc)
		ret = mmc_resume_host(mmc);

	return ret;
}
#else
#define at91_mci_suspend	NULL
#define at91_mci_resume		NULL
#endif

static struct platform_driver at91_mci_driver = {
	.remove		= __exit_p(at91_mci_remove),
	.suspend	= at91_mci_suspend,
	.resume		= at91_mci_resume,
	.driver		= {
		.name	= DRIVER_NAME,
		.owner	= THIS_MODULE,
	},
};

static int __init at91_mci_init(void)
{
	return platform_driver_probe(&at91_mci_driver, at91_mci_probe);
}

static void __exit at91_mci_exit(void)
{
	platform_driver_unregister(&at91_mci_driver);
}

module_init(at91_mci_init);
module_exit(at91_mci_exit);

MODULE_DESCRIPTION("AT91 Multimedia Card Interface driver");
MODULE_AUTHOR("Nick Randell");
MODULE_LICENSE("GPL");
MODULE_ALIAS("platform:at91_mci");
