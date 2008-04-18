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

#include <linux/mmc/host.h>

#include <asm/io.h>
#include <asm/irq.h>
#include <asm/gpio.h>

#include <asm/mach/mmc.h>
#include <asm/arch/board.h>
#include <asm/arch/cpu.h>
#include <asm/arch/at91_mci.h>

#define DRIVER_NAME "at91_mci"

#define FL_SENT_COMMAND	(1 << 0)
#define FL_SENT_STOP	(1 << 1)

#define AT91_MCI_ERRORS	(AT91_MCI_RINDE | AT91_MCI_RDIRE | AT91_MCI_RCRCE	\
		| AT91_MCI_RENDE | AT91_MCI_RTOE | AT91_MCI_DCRCE		\
		| AT91_MCI_DTOE | AT91_MCI_OVRE | AT91_MCI_UNRE)

#define at91_mci_read(host, reg)	__raw_readl((host)->baseaddr + (reg))
#define at91_mci_write(host, reg, val)	__raw_writel((val), (host)->baseaddr + (reg))


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
};

/*
 * Copy from sg to a dma block - used for transfers
 */
static inline void at91_mci_sg_to_dma(struct at91mci_host *host, struct mmc_data *data)
{
	unsigned int len, i, size;
	unsigned *dmabuf = host->buffer;

	size = host->total_length;
	len = data->sg_len;

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
		}
		else
			memcpy(dmabuf, sgbuffer, amount);

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
 * Prepare a dma read
 */
static void at91_mci_pre_dma_read(struct at91mci_host *host)
{
	int i;
	struct scatterlist *sg;
	struct mmc_command *cmd;
	struct mmc_data *data;

	pr_debug("pre dma read\n");

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

	for (i = 0; i < 2; i++) {
		/* nothing left to transfer */
		if (host->transfer_index >= data->sg_len) {
			pr_debug("Nothing left to transfer (index = %d)\n", host->transfer_index);
			break;
		}

		/* Check to see if this needs filling */
		if (i == 0) {
			if (at91_mci_read(host, ATMEL_PDC_RCR) != 0) {
				pr_debug("Transfer active in current\n");
				continue;
			}
		}
		else {
			if (at91_mci_read(host, ATMEL_PDC_RNCR) != 0) {
				pr_debug("Transfer active in next\n");
				continue;
			}
		}

		/* Setup the next transfer */
		pr_debug("Using transfer index %d\n", host->transfer_index);

		sg = &data->sg[host->transfer_index++];
		pr_debug("sg = %p\n", sg);

		sg->dma_address = dma_map_page(NULL, sg_page(sg), sg->offset, sg->length, DMA_FROM_DEVICE);

		pr_debug("dma address = %08X, length = %d\n", sg->dma_address, sg->length);

		if (i == 0) {
			at91_mci_write(host, ATMEL_PDC_RPR, sg->dma_address);
			at91_mci_write(host, ATMEL_PDC_RCR, sg->length / 4);
		}
		else {
			at91_mci_write(host, ATMEL_PDC_RNPR, sg->dma_address);
			at91_mci_write(host, ATMEL_PDC_RNCR, sg->length / 4);
		}
	}

	pr_debug("pre dma read done\n");
}

/*
 * Handle after a dma read
 */
static void at91_mci_post_dma_read(struct at91mci_host *host)
{
	struct mmc_command *cmd;
	struct mmc_data *data;

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

	while (host->in_use_index < host->transfer_index) {
		struct scatterlist *sg;

		pr_debug("finishing index %d\n", host->in_use_index);

		sg = &data->sg[host->in_use_index++];

		pr_debug("Unmapping page %08X\n", sg->dma_address);

		dma_unmap_page(NULL, sg->dma_address, sg->length, DMA_FROM_DEVICE);

		data->bytes_xfered += sg->length;

		if (cpu_is_at91rm9200()) {	/* AT91RM9200 errata */
			unsigned int *buffer;
			int index;

			/* Swap the contents of the buffer */
			buffer = kmap_atomic(sg_page(sg), KM_BIO_SRC_IRQ) + sg->offset;
			pr_debug("buffer = %p, length = %d\n", buffer, sg->length);

			for (index = 0; index < (sg->length / 4); index++)
				buffer[index] = swab32(buffer[index]);

			kunmap_atomic(buffer, KM_BIO_SRC_IRQ);
		}

		flush_dcache_page(sg_page(sg));
	}

	/* Is there another transfer to trigger? */
	if (host->transfer_index < data->sg_len)
		at91_mci_pre_dma_read(host);
	else {
		at91_mci_write(host, AT91_MCI_IDR, AT91_MCI_ENDRX);
		at91_mci_write(host, AT91_MCI_IER, AT91_MCI_RXBUFF);
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

	data->bytes_xfered = host->total_length;
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
		at91_mci_write(host, AT91_MCI_IER, AT91_MCI_TXBUFE);
		at91_mci_write(host, AT91_MCI_IER, AT91_MCI_BLKE);
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

	if (cpu_is_at91sam9260() || cpu_is_at91sam9263())
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

		if ( data->blksz & 0x3 ) {
			pr_debug("Unsupported block size\n");
			cmd->error = -EINVAL;
			mmc_request_done(host->mmc, host->request);
			return;
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
		mr = at91_mci_read(host, AT91_MCI_MR) & 0x7fff;
		at91_mci_write(host, AT91_MCI_MR, mr | (block_length << 16) | AT91_MCI_PDCMODE);

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
				host->buffer = NULL;
				host->total_length = 0;

				at91_mci_pre_dma_read(host);
				ier = AT91_MCI_ENDRX /* | AT91_MCI_RXBUFF */;
			}
			else {
				/*
				 * Handle a write
				 */
				host->total_length = block_length * blocks;
				host->buffer = dma_alloc_coherent(NULL,
						host->total_length,
						&host->physical_address, GFP_KERNEL);

				at91_mci_sg_to_dma(host, data);

				pr_debug("Transmitting %d bytes\n", host->total_length);

				at91_mci_write(host, ATMEL_PDC_TPR, host->physical_address);
				at91_mci_write(host, ATMEL_PDC_TCR, host->total_length / 4);
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
	}
	else
		mmc_request_done(host->mmc, host->request);
}

/*
 * Handle a command that has been completed
 */
static void at91_mci_completed_command(struct at91mci_host *host)
{
	struct mmc_command *cmd = host->cmd;
	unsigned int status;

	at91_mci_write(host, AT91_MCI_IDR, 0xffffffff);

	cmd->resp[0] = at91_mci_read(host, AT91_MCI_RSPR(0));
	cmd->resp[1] = at91_mci_read(host, AT91_MCI_RSPR(1));
	cmd->resp[2] = at91_mci_read(host, AT91_MCI_RSPR(2));
	cmd->resp[3] = at91_mci_read(host, AT91_MCI_RSPR(3));

	if (host->buffer) {
		dma_free_coherent(NULL, host->total_length, host->buffer, host->physical_address);
		host->buffer = NULL;
	}

	status = at91_mci_read(host, AT91_MCI_SR);

	pr_debug("Status = %08X [%08X %08X %08X %08X]\n",
		 status, cmd->resp[0], cmd->resp[1], cmd->resp[2], cmd->resp[3]);

	if (status & AT91_MCI_ERRORS) {
		if ((status & AT91_MCI_RCRCE) && !(mmc_resp_type(cmd) & MMC_RSP_CRC)) {
			cmd->error = 0;
		}
		else {
			if (status & (AT91_MCI_RTOE | AT91_MCI_DTOE))
				cmd->error = -ETIMEDOUT;
			else if (status & (AT91_MCI_RCRCE | AT91_MCI_DCRCE))
				cmd->error = -EILSEQ;
			else
				cmd->error = -EIO;

			pr_debug("Error detected and set to %d (cmd = %d, retries = %d)\n",
				 cmd->error, cmd->opcode, cmd->retries);
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
			case MMC_POWER_ON:
				gpio_set_value(host->board->vcc_pin, 1);
				break;
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
			completed = 1;
		}

		if (int_status & AT91_MCI_DTIP)
			pr_debug("Data transfer in progress\n");

		if (int_status & AT91_MCI_BLKE) {
			pr_debug("Block transfer has ended\n");
			completed = 1;
		}

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
		at91_mci_write(host, AT91_MCI_IDR, 0xffffffff);
		at91_mci_completed_command(host);
	} else
		at91_mci_write(host, AT91_MCI_IDR, int_status);

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
		mmc_detect_change(host->mmc, msecs_to_jiffies(100));
	}
	return IRQ_HANDLED;
}

static int at91_mci_get_ro(struct mmc_host *mmc)
{
	int read_only = 0;
	struct at91mci_host *host = mmc_priv(mmc);

	if (host->board->wp_pin) {
		read_only = gpio_get_value(host->board->wp_pin);
		printk(KERN_WARNING "%s: card is %s\n", mmc_hostname(mmc),
				(read_only ? "read-only" : "read-write") );
	}
	else {
		printk(KERN_WARNING "%s: host does not support reading read-only "
				"switch.  Assuming write-enable.\n", mmc_hostname(mmc));
	}
	return read_only;
}

static const struct mmc_host_ops at91_mci_ops = {
	.request	= at91_mci_request,
	.set_ios	= at91_mci_set_ios,
	.get_ro		= at91_mci_get_ro,
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

	if (!request_mem_region(res->start, res->end - res->start + 1, DRIVER_NAME))
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

	mmc->max_blk_size = 4095;
	mmc->max_blk_count = mmc->max_req_size;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->buffer = NULL;
	host->bus_mode = 0;
	host->board = pdev->dev.platform_data;
	if (host->board->wire4) {
		if (cpu_is_at91sam9260() || cpu_is_at91sam9263())
			mmc->caps |= MMC_CAP_4_BIT_DATA;
		else
			dev_warn(&pdev->dev, "4 wire bus mode not supported"
				" - using 1 wire\n");
	}

	/*
	 * Reserve GPIOs ... board init code makes sure these pins are set
	 * up as GPIOs with the right direction (input, except for vcc)
	 */
	if (host->board->det_pin) {
		ret = gpio_request(host->board->det_pin, "mmc_detect");
		if (ret < 0) {
			dev_dbg(&pdev->dev, "couldn't claim card detect pin\n");
			goto fail5;
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
	host->baseaddr = ioremap(res->start, res->end - res->start + 1);
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
fail5:
	mmc_free_host(mmc);
fail6:
	release_mem_region(res->start, res->end - res->start + 1);
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

	if (host->board->det_pin) {
		if (device_can_wakeup(&pdev->dev))
			free_irq(gpio_to_irq(host->board->det_pin), host);
		device_init_wakeup(&pdev->dev, 0);
		gpio_free(host->board->det_pin);
	}

	at91_mci_disable(host);
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
	release_mem_region(res->start, res->end - res->start + 1);

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
		ret = mmc_suspend_host(mmc, state);

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
