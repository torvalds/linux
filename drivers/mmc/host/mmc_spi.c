// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Access SD/MMC cards through SPI master controllers
 *
 * (C) Copyright 2005, Intec Automation,
 *		Mike Lavender (mike@steroidmicros)
 * (C) Copyright 2006-2007, David Brownell
 * (C) Copyright 2007, Axis Communications,
 *		Hans-Peter Nilsson (hp@axis.com)
 * (C) Copyright 2007, ATRON electronic GmbH,
 *		Jan Nikitenko <jan.nikitenko@gmail.com>
 */
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/module.h>
#include <linux/bio.h>
#include <linux/dma-mapping.h>
#include <linux/crc7.h>
#include <linux/crc-itu-t.h>
#include <linux/scatterlist.h>

#include <linux/mmc/host.h>
#include <linux/mmc/mmc.h>		/* for R1_SPI_* bit values */
#include <linux/mmc/slot-gpio.h>

#include <linux/spi/spi.h>
#include <linux/spi/mmc_spi.h>

#include <asm/unaligned.h>


/* NOTES:
 *
 * - For now, we won't try to interoperate with a real mmc/sd/sdio
 *   controller, although some of them do have hardware support for
 *   SPI protocol.  The main reason for such configs would be mmc-ish
 *   cards like DataFlash, which don't support that "native" protocol.
 *
 *   We don't have a "DataFlash/MMC/SD/SDIO card slot" abstraction to
 *   switch between driver stacks, and in any case if "native" mode
 *   is available, it will be faster and hence preferable.
 *
 * - MMC depends on a different chipselect management policy than the
 *   SPI interface currently supports for shared bus segments:  it needs
 *   to issue multiple spi_message requests with the chipselect active,
 *   using the results of one message to decide the next one to issue.
 *
 *   Pending updates to the programming interface, this driver expects
 *   that it not share the bus with other drivers (precluding conflicts).
 *
 * - We tell the controller to keep the chipselect active from the
 *   beginning of an mmc_host_ops.request until the end.  So beware
 *   of SPI controller drivers that mis-handle the cs_change flag!
 *
 *   However, many cards seem OK with chipselect flapping up/down
 *   during that time ... at least on unshared bus segments.
 */


/*
 * Local protocol constants, internal to data block protocols.
 */

/* Response tokens used to ack each block written: */
#define SPI_MMC_RESPONSE_CODE(x)	((x) & 0x1f)
#define SPI_RESPONSE_ACCEPTED		((2 << 1)|1)
#define SPI_RESPONSE_CRC_ERR		((5 << 1)|1)
#define SPI_RESPONSE_WRITE_ERR		((6 << 1)|1)

/* Read and write blocks start with these tokens and end with crc;
 * on error, read tokens act like a subset of R2_SPI_* values.
 */
#define SPI_TOKEN_SINGLE	0xfe	/* single block r/w, multiblock read */
#define SPI_TOKEN_MULTI_WRITE	0xfc	/* multiblock write */
#define SPI_TOKEN_STOP_TRAN	0xfd	/* terminate multiblock write */

#define MMC_SPI_BLOCKSIZE	512


/* These fixed timeouts come from the latest SD specs, which say to ignore
 * the CSD values.  The R1B value is for card erase (e.g. the "I forgot the
 * card's password" scenario); it's mostly applied to STOP_TRANSMISSION after
 * reads which takes nowhere near that long.  Older cards may be able to use
 * shorter timeouts ... but why bother?
 */
#define r1b_timeout		(HZ * 3)

/* One of the critical speed parameters is the amount of data which may
 * be transferred in one command. If this value is too low, the SD card
 * controller has to do multiple partial block writes (argggh!). With
 * today (2008) SD cards there is little speed gain if we transfer more
 * than 64 KBytes at a time. So use this value until there is any indication
 * that we should do more here.
 */
#define MMC_SPI_BLOCKSATONCE	128

/****************************************************************************/

/*
 * Local Data Structures
 */

/* "scratch" is per-{command,block} data exchanged with the card */
struct scratch {
	u8			status[29];
	u8			data_token;
	__be16			crc_val;
};

struct mmc_spi_host {
	struct mmc_host		*mmc;
	struct spi_device	*spi;

	unsigned char		power_mode;
	u16			powerup_msecs;

	struct mmc_spi_platform_data	*pdata;

	/* for bulk data transfers */
	struct spi_transfer	token, t, crc, early_status;
	struct spi_message	m;

	/* for status readback */
	struct spi_transfer	status;
	struct spi_message	readback;

	/* underlying DMA-aware controller, or null */
	struct device		*dma_dev;

	/* buffer used for commands and for message "overhead" */
	struct scratch		*data;
	dma_addr_t		data_dma;

	/* Specs say to write ones most of the time, even when the card
	 * has no need to read its input data; and many cards won't care.
	 * This is our source of those ones.
	 */
	void			*ones;
	dma_addr_t		ones_dma;
};


/****************************************************************************/

/*
 * MMC-over-SPI protocol glue, used by the MMC stack interface
 */

static inline int mmc_cs_off(struct mmc_spi_host *host)
{
	/* chipselect will always be inactive after setup() */
	return spi_setup(host->spi);
}

static int
mmc_spi_readbytes(struct mmc_spi_host *host, unsigned len)
{
	int status;

	if (len > sizeof(*host->data)) {
		WARN_ON(1);
		return -EIO;
	}

	host->status.len = len;

	if (host->dma_dev)
		dma_sync_single_for_device(host->dma_dev,
				host->data_dma, sizeof(*host->data),
				DMA_FROM_DEVICE);

	status = spi_sync_locked(host->spi, &host->readback);

	if (host->dma_dev)
		dma_sync_single_for_cpu(host->dma_dev,
				host->data_dma, sizeof(*host->data),
				DMA_FROM_DEVICE);

	return status;
}

static int mmc_spi_skip(struct mmc_spi_host *host, unsigned long timeout,
			unsigned n, u8 byte)
{
	u8 *cp = host->data->status;
	unsigned long start = jiffies;

	while (1) {
		int		status;
		unsigned	i;

		status = mmc_spi_readbytes(host, n);
		if (status < 0)
			return status;

		for (i = 0; i < n; i++) {
			if (cp[i] != byte)
				return cp[i];
		}

		if (time_is_before_jiffies(start + timeout))
			break;

		/* If we need long timeouts, we may release the CPU.
		 * We use jiffies here because we want to have a relation
		 * between elapsed time and the blocking of the scheduler.
		 */
		if (time_is_before_jiffies(start + 1))
			schedule();
	}
	return -ETIMEDOUT;
}

static inline int
mmc_spi_wait_unbusy(struct mmc_spi_host *host, unsigned long timeout)
{
	return mmc_spi_skip(host, timeout, sizeof(host->data->status), 0);
}

static int mmc_spi_readtoken(struct mmc_spi_host *host, unsigned long timeout)
{
	return mmc_spi_skip(host, timeout, 1, 0xff);
}


/*
 * Note that for SPI, cmd->resp[0] is not the same data as "native" protocol
 * hosts return!  The low byte holds R1_SPI bits.  The next byte may hold
 * R2_SPI bits ... for SEND_STATUS, or after data read errors.
 *
 * cmd->resp[1] holds any four-byte response, for R3 (READ_OCR) and on
 * newer cards R7 (IF_COND).
 */

static char *maptype(struct mmc_command *cmd)
{
	switch (mmc_spi_resp_type(cmd)) {
	case MMC_RSP_SPI_R1:	return "R1";
	case MMC_RSP_SPI_R1B:	return "R1B";
	case MMC_RSP_SPI_R2:	return "R2/R5";
	case MMC_RSP_SPI_R3:	return "R3/R4/R7";
	default:		return "?";
	}
}

/* return zero, else negative errno after setting cmd->error */
static int mmc_spi_response_get(struct mmc_spi_host *host,
		struct mmc_command *cmd, int cs_on)
{
	u8	*cp = host->data->status;
	u8	*end = cp + host->t.len;
	int	value = 0;
	int	bitshift;
	u8 	leftover = 0;
	unsigned short rotator;
	int 	i;
	char	tag[32];

	snprintf(tag, sizeof(tag), "  ... CMD%d response SPI_%s",
		cmd->opcode, maptype(cmd));

	/* Except for data block reads, the whole response will already
	 * be stored in the scratch buffer.  It's somewhere after the
	 * command and the first byte we read after it.  We ignore that
	 * first byte.  After STOP_TRANSMISSION command it may include
	 * two data bits, but otherwise it's all ones.
	 */
	cp += 8;
	while (cp < end && *cp == 0xff)
		cp++;

	/* Data block reads (R1 response types) may need more data... */
	if (cp == end) {
		cp = host->data->status;
		end = cp+1;

		/* Card sends N(CR) (== 1..8) bytes of all-ones then one
		 * status byte ... and we already scanned 2 bytes.
		 *
		 * REVISIT block read paths use nasty byte-at-a-time I/O
		 * so it can always DMA directly into the target buffer.
		 * It'd probably be better to memcpy() the first chunk and
		 * avoid extra i/o calls...
		 *
		 * Note we check for more than 8 bytes, because in practice,
		 * some SD cards are slow...
		 */
		for (i = 2; i < 16; i++) {
			value = mmc_spi_readbytes(host, 1);
			if (value < 0)
				goto done;
			if (*cp != 0xff)
				goto checkstatus;
		}
		value = -ETIMEDOUT;
		goto done;
	}

checkstatus:
	bitshift = 0;
	if (*cp & 0x80)	{
		/* Houston, we have an ugly card with a bit-shifted response */
		rotator = *cp++ << 8;
		/* read the next byte */
		if (cp == end) {
			value = mmc_spi_readbytes(host, 1);
			if (value < 0)
				goto done;
			cp = host->data->status;
			end = cp+1;
		}
		rotator |= *cp++;
		while (rotator & 0x8000) {
			bitshift++;
			rotator <<= 1;
		}
		cmd->resp[0] = rotator >> 8;
		leftover = rotator;
	} else {
		cmd->resp[0] = *cp++;
	}
	cmd->error = 0;

	/* Status byte: the entire seven-bit R1 response.  */
	if (cmd->resp[0] != 0) {
		if ((R1_SPI_PARAMETER | R1_SPI_ADDRESS)
				& cmd->resp[0])
			value = -EFAULT; /* Bad address */
		else if (R1_SPI_ILLEGAL_COMMAND & cmd->resp[0])
			value = -ENOSYS; /* Function not implemented */
		else if (R1_SPI_COM_CRC & cmd->resp[0])
			value = -EILSEQ; /* Illegal byte sequence */
		else if ((R1_SPI_ERASE_SEQ | R1_SPI_ERASE_RESET)
				& cmd->resp[0])
			value = -EIO;    /* I/O error */
		/* else R1_SPI_IDLE, "it's resetting" */
	}

	switch (mmc_spi_resp_type(cmd)) {

	/* SPI R1B == R1 + busy; STOP_TRANSMISSION (for multiblock reads)
	 * and less-common stuff like various erase operations.
	 */
	case MMC_RSP_SPI_R1B:
		/* maybe we read all the busy tokens already */
		while (cp < end && *cp == 0)
			cp++;
		if (cp == end)
			mmc_spi_wait_unbusy(host, r1b_timeout);
		break;

	/* SPI R2 == R1 + second status byte; SEND_STATUS
	 * SPI R5 == R1 + data byte; IO_RW_DIRECT
	 */
	case MMC_RSP_SPI_R2:
		/* read the next byte */
		if (cp == end) {
			value = mmc_spi_readbytes(host, 1);
			if (value < 0)
				goto done;
			cp = host->data->status;
			end = cp+1;
		}
		if (bitshift) {
			rotator = leftover << 8;
			rotator |= *cp << bitshift;
			cmd->resp[0] |= (rotator & 0xFF00);
		} else {
			cmd->resp[0] |= *cp << 8;
		}
		break;

	/* SPI R3, R4, or R7 == R1 + 4 bytes */
	case MMC_RSP_SPI_R3:
		rotator = leftover << 8;
		cmd->resp[1] = 0;
		for (i = 0; i < 4; i++) {
			cmd->resp[1] <<= 8;
			/* read the next byte */
			if (cp == end) {
				value = mmc_spi_readbytes(host, 1);
				if (value < 0)
					goto done;
				cp = host->data->status;
				end = cp+1;
			}
			if (bitshift) {
				rotator |= *cp++ << bitshift;
				cmd->resp[1] |= (rotator >> 8);
				rotator <<= 8;
			} else {
				cmd->resp[1] |= *cp++;
			}
		}
		break;

	/* SPI R1 == just one status byte */
	case MMC_RSP_SPI_R1:
		break;

	default:
		dev_dbg(&host->spi->dev, "bad response type %04x\n",
			mmc_spi_resp_type(cmd));
		if (value >= 0)
			value = -EINVAL;
		goto done;
	}

	if (value < 0)
		dev_dbg(&host->spi->dev, "%s: resp %04x %08x\n",
			tag, cmd->resp[0], cmd->resp[1]);

	/* disable chipselect on errors and some success cases */
	if (value >= 0 && cs_on)
		return value;
done:
	if (value < 0)
		cmd->error = value;
	mmc_cs_off(host);
	return value;
}

/* Issue command and read its response.
 * Returns zero on success, negative for error.
 *
 * On error, caller must cope with mmc core retry mechanism.  That
 * means immediate low-level resubmit, which affects the bus lock...
 */
static int
mmc_spi_command_send(struct mmc_spi_host *host,
		struct mmc_request *mrq,
		struct mmc_command *cmd, int cs_on)
{
	struct scratch		*data = host->data;
	u8			*cp = data->status;
	int			status;
	struct spi_transfer	*t;

	/* We can handle most commands (except block reads) in one full
	 * duplex I/O operation before either starting the next transfer
	 * (data block or command) or else deselecting the card.
	 *
	 * First, write 7 bytes:
	 *  - an all-ones byte to ensure the card is ready
	 *  - opcode byte (plus start and transmission bits)
	 *  - four bytes of big-endian argument
	 *  - crc7 (plus end bit) ... always computed, it's cheap
	 *
	 * We init the whole buffer to all-ones, which is what we need
	 * to write while we're reading (later) response data.
	 */
	memset(cp, 0xff, sizeof(data->status));

	cp[1] = 0x40 | cmd->opcode;
	put_unaligned_be32(cmd->arg, cp + 2);
	cp[6] = crc7_be(0, cp + 1, 5) | 0x01;
	cp += 7;

	/* Then, read up to 13 bytes (while writing all-ones):
	 *  - N(CR) (== 1..8) bytes of all-ones
	 *  - status byte (for all response types)
	 *  - the rest of the response, either:
	 *      + nothing, for R1 or R1B responses
	 *	+ second status byte, for R2 responses
	 *	+ four data bytes, for R3 and R7 responses
	 *
	 * Finally, read some more bytes ... in the nice cases we know in
	 * advance how many, and reading 1 more is always OK:
	 *  - N(EC) (== 0..N) bytes of all-ones, before deselect/finish
	 *  - N(RC) (== 1..N) bytes of all-ones, before next command
	 *  - N(WR) (== 1..N) bytes of all-ones, before data write
	 *
	 * So in those cases one full duplex I/O of at most 21 bytes will
	 * handle the whole command, leaving the card ready to receive a
	 * data block or new command.  We do that whenever we can, shaving
	 * CPU and IRQ costs (especially when using DMA or FIFOs).
	 *
	 * There are two other cases, where it's not generally practical
	 * to rely on a single I/O:
	 *
	 *  - R1B responses need at least N(EC) bytes of all-zeroes.
	 *
	 *    In this case we can *try* to fit it into one I/O, then
	 *    maybe read more data later.
	 *
	 *  - Data block reads are more troublesome, since a variable
	 *    number of padding bytes precede the token and data.
	 *      + N(CX) (== 0..8) bytes of all-ones, before CSD or CID
	 *      + N(AC) (== 1..many) bytes of all-ones
	 *
	 *    In this case we currently only have minimal speedups here:
	 *    when N(CR) == 1 we can avoid I/O in response_get().
	 */
	if (cs_on && (mrq->data->flags & MMC_DATA_READ)) {
		cp += 2;	/* min(N(CR)) + status */
		/* R1 */
	} else {
		cp += 10;	/* max(N(CR)) + status + min(N(RC),N(WR)) */
		if (cmd->flags & MMC_RSP_SPI_S2)	/* R2/R5 */
			cp++;
		else if (cmd->flags & MMC_RSP_SPI_B4)	/* R3/R4/R7 */
			cp += 4;
		else if (cmd->flags & MMC_RSP_BUSY)	/* R1B */
			cp = data->status + sizeof(data->status);
		/* else:  R1 (most commands) */
	}

	dev_dbg(&host->spi->dev, "  mmc_spi: CMD%d, resp %s\n",
		cmd->opcode, maptype(cmd));

	/* send command, leaving chipselect active */
	spi_message_init(&host->m);

	t = &host->t;
	memset(t, 0, sizeof(*t));
	t->tx_buf = t->rx_buf = data->status;
	t->tx_dma = t->rx_dma = host->data_dma;
	t->len = cp - data->status;
	t->cs_change = 1;
	spi_message_add_tail(t, &host->m);

	if (host->dma_dev) {
		host->m.is_dma_mapped = 1;
		dma_sync_single_for_device(host->dma_dev,
				host->data_dma, sizeof(*host->data),
				DMA_BIDIRECTIONAL);
	}
	status = spi_sync_locked(host->spi, &host->m);

	if (host->dma_dev)
		dma_sync_single_for_cpu(host->dma_dev,
				host->data_dma, sizeof(*host->data),
				DMA_BIDIRECTIONAL);
	if (status < 0) {
		dev_dbg(&host->spi->dev, "  ... write returned %d\n", status);
		cmd->error = status;
		return status;
	}

	/* after no-data commands and STOP_TRANSMISSION, chipselect off */
	return mmc_spi_response_get(host, cmd, cs_on);
}

/* Build data message with up to four separate transfers.  For TX, we
 * start by writing the data token.  And in most cases, we finish with
 * a status transfer.
 *
 * We always provide TX data for data and CRC.  The MMC/SD protocol
 * requires us to write ones; but Linux defaults to writing zeroes;
 * so we explicitly initialize it to all ones on RX paths.
 *
 * We also handle DMA mapping, so the underlying SPI controller does
 * not need to (re)do it for each message.
 */
static void
mmc_spi_setup_data_message(
	struct mmc_spi_host	*host,
	int			multiple,
	enum dma_data_direction	direction)
{
	struct spi_transfer	*t;
	struct scratch		*scratch = host->data;
	dma_addr_t		dma = host->data_dma;

	spi_message_init(&host->m);
	if (dma)
		host->m.is_dma_mapped = 1;

	/* for reads, readblock() skips 0xff bytes before finding
	 * the token; for writes, this transfer issues that token.
	 */
	if (direction == DMA_TO_DEVICE) {
		t = &host->token;
		memset(t, 0, sizeof(*t));
		t->len = 1;
		if (multiple)
			scratch->data_token = SPI_TOKEN_MULTI_WRITE;
		else
			scratch->data_token = SPI_TOKEN_SINGLE;
		t->tx_buf = &scratch->data_token;
		if (dma)
			t->tx_dma = dma + offsetof(struct scratch, data_token);
		spi_message_add_tail(t, &host->m);
	}

	/* Body of transfer is buffer, then CRC ...
	 * either TX-only, or RX with TX-ones.
	 */
	t = &host->t;
	memset(t, 0, sizeof(*t));
	t->tx_buf = host->ones;
	t->tx_dma = host->ones_dma;
	/* length and actual buffer info are written later */
	spi_message_add_tail(t, &host->m);

	t = &host->crc;
	memset(t, 0, sizeof(*t));
	t->len = 2;
	if (direction == DMA_TO_DEVICE) {
		/* the actual CRC may get written later */
		t->tx_buf = &scratch->crc_val;
		if (dma)
			t->tx_dma = dma + offsetof(struct scratch, crc_val);
	} else {
		t->tx_buf = host->ones;
		t->tx_dma = host->ones_dma;
		t->rx_buf = &scratch->crc_val;
		if (dma)
			t->rx_dma = dma + offsetof(struct scratch, crc_val);
	}
	spi_message_add_tail(t, &host->m);

	/*
	 * A single block read is followed by N(EC) [0+] all-ones bytes
	 * before deselect ... don't bother.
	 *
	 * Multiblock reads are followed by N(AC) [1+] all-ones bytes before
	 * the next block is read, or a STOP_TRANSMISSION is issued.  We'll
	 * collect that single byte, so readblock() doesn't need to.
	 *
	 * For a write, the one-byte data response follows immediately, then
	 * come zero or more busy bytes, then N(WR) [1+] all-ones bytes.
	 * Then single block reads may deselect, and multiblock ones issue
	 * the next token (next data block, or STOP_TRAN).  We can try to
	 * minimize I/O ops by using a single read to collect end-of-busy.
	 */
	if (multiple || direction == DMA_TO_DEVICE) {
		t = &host->early_status;
		memset(t, 0, sizeof(*t));
		t->len = (direction == DMA_TO_DEVICE) ? sizeof(scratch->status) : 1;
		t->tx_buf = host->ones;
		t->tx_dma = host->ones_dma;
		t->rx_buf = scratch->status;
		if (dma)
			t->rx_dma = dma + offsetof(struct scratch, status);
		t->cs_change = 1;
		spi_message_add_tail(t, &host->m);
	}
}

/*
 * Write one block:
 *  - caller handled preceding N(WR) [1+] all-ones bytes
 *  - data block
 *	+ token
 *	+ data bytes
 *	+ crc16
 *  - an all-ones byte ... card writes a data-response byte
 *  - followed by N(EC) [0+] all-ones bytes, card writes zero/'busy'
 *
 * Return negative errno, else success.
 */
static int
mmc_spi_writeblock(struct mmc_spi_host *host, struct spi_transfer *t,
	unsigned long timeout)
{
	struct spi_device	*spi = host->spi;
	int			status, i;
	struct scratch		*scratch = host->data;
	u32			pattern;

	if (host->mmc->use_spi_crc)
		scratch->crc_val = cpu_to_be16(crc_itu_t(0, t->tx_buf, t->len));
	if (host->dma_dev)
		dma_sync_single_for_device(host->dma_dev,
				host->data_dma, sizeof(*scratch),
				DMA_BIDIRECTIONAL);

	status = spi_sync_locked(spi, &host->m);

	if (status != 0) {
		dev_dbg(&spi->dev, "write error (%d)\n", status);
		return status;
	}

	if (host->dma_dev)
		dma_sync_single_for_cpu(host->dma_dev,
				host->data_dma, sizeof(*scratch),
				DMA_BIDIRECTIONAL);

	/*
	 * Get the transmission data-response reply.  It must follow
	 * immediately after the data block we transferred.  This reply
	 * doesn't necessarily tell whether the write operation succeeded;
	 * it just says if the transmission was ok and whether *earlier*
	 * writes succeeded; see the standard.
	 *
	 * In practice, there are (even modern SDHC-)cards which are late
	 * in sending the response, and miss the time frame by a few bits,
	 * so we have to cope with this situation and check the response
	 * bit-by-bit. Arggh!!!
	 */
	pattern = get_unaligned_be32(scratch->status);

	/* First 3 bit of pattern are undefined */
	pattern |= 0xE0000000;

	/* left-adjust to leading 0 bit */
	while (pattern & 0x80000000)
		pattern <<= 1;
	/* right-adjust for pattern matching. Code is in bit 4..0 now. */
	pattern >>= 27;

	switch (pattern) {
	case SPI_RESPONSE_ACCEPTED:
		status = 0;
		break;
	case SPI_RESPONSE_CRC_ERR:
		/* host shall then issue MMC_STOP_TRANSMISSION */
		status = -EILSEQ;
		break;
	case SPI_RESPONSE_WRITE_ERR:
		/* host shall then issue MMC_STOP_TRANSMISSION,
		 * and should MMC_SEND_STATUS to sort it out
		 */
		status = -EIO;
		break;
	default:
		status = -EPROTO;
		break;
	}
	if (status != 0) {
		dev_dbg(&spi->dev, "write error %02x (%d)\n",
			scratch->status[0], status);
		return status;
	}

	t->tx_buf += t->len;
	if (host->dma_dev)
		t->tx_dma += t->len;

	/* Return when not busy.  If we didn't collect that status yet,
	 * we'll need some more I/O.
	 */
	for (i = 4; i < sizeof(scratch->status); i++) {
		/* card is non-busy if the most recent bit is 1 */
		if (scratch->status[i] & 0x01)
			return 0;
	}
	return mmc_spi_wait_unbusy(host, timeout);
}

/*
 * Read one block:
 *  - skip leading all-ones bytes ... either
 *      + N(AC) [1..f(clock,CSD)] usually, else
 *      + N(CX) [0..8] when reading CSD or CID
 *  - data block
 *	+ token ... if error token, no data or crc
 *	+ data bytes
 *	+ crc16
 *
 * After single block reads, we're done; N(EC) [0+] all-ones bytes follow
 * before dropping chipselect.
 *
 * For multiblock reads, caller either reads the next block or issues a
 * STOP_TRANSMISSION command.
 */
static int
mmc_spi_readblock(struct mmc_spi_host *host, struct spi_transfer *t,
	unsigned long timeout)
{
	struct spi_device	*spi = host->spi;
	int			status;
	struct scratch		*scratch = host->data;
	unsigned int 		bitshift;
	u8			leftover;

	/* At least one SD card sends an all-zeroes byte when N(CX)
	 * applies, before the all-ones bytes ... just cope with that.
	 */
	status = mmc_spi_readbytes(host, 1);
	if (status < 0)
		return status;
	status = scratch->status[0];
	if (status == 0xff || status == 0)
		status = mmc_spi_readtoken(host, timeout);

	if (status < 0) {
		dev_dbg(&spi->dev, "read error %02x (%d)\n", status, status);
		return status;
	}

	/* The token may be bit-shifted...
	 * the first 0-bit precedes the data stream.
	 */
	bitshift = 7;
	while (status & 0x80) {
		status <<= 1;
		bitshift--;
	}
	leftover = status << 1;

	if (host->dma_dev) {
		dma_sync_single_for_device(host->dma_dev,
				host->data_dma, sizeof(*scratch),
				DMA_BIDIRECTIONAL);
		dma_sync_single_for_device(host->dma_dev,
				t->rx_dma, t->len,
				DMA_FROM_DEVICE);
	}

	status = spi_sync_locked(spi, &host->m);
	if (status < 0) {
		dev_dbg(&spi->dev, "read error %d\n", status);
		return status;
	}

	if (host->dma_dev) {
		dma_sync_single_for_cpu(host->dma_dev,
				host->data_dma, sizeof(*scratch),
				DMA_BIDIRECTIONAL);
		dma_sync_single_for_cpu(host->dma_dev,
				t->rx_dma, t->len,
				DMA_FROM_DEVICE);
	}

	if (bitshift) {
		/* Walk through the data and the crc and do
		 * all the magic to get byte-aligned data.
		 */
		u8 *cp = t->rx_buf;
		unsigned int len;
		unsigned int bitright = 8 - bitshift;
		u8 temp;
		for (len = t->len; len; len--) {
			temp = *cp;
			*cp++ = leftover | (temp >> bitshift);
			leftover = temp << bitright;
		}
		cp = (u8 *) &scratch->crc_val;
		temp = *cp;
		*cp++ = leftover | (temp >> bitshift);
		leftover = temp << bitright;
		temp = *cp;
		*cp = leftover | (temp >> bitshift);
	}

	if (host->mmc->use_spi_crc) {
		u16 crc = crc_itu_t(0, t->rx_buf, t->len);

		be16_to_cpus(&scratch->crc_val);
		if (scratch->crc_val != crc) {
			dev_dbg(&spi->dev,
				"read - crc error: crc_val=0x%04x, computed=0x%04x len=%d\n",
				scratch->crc_val, crc, t->len);
			return -EILSEQ;
		}
	}

	t->rx_buf += t->len;
	if (host->dma_dev)
		t->rx_dma += t->len;

	return 0;
}

/*
 * An MMC/SD data stage includes one or more blocks, optional CRCs,
 * and inline handshaking.  That handhaking makes it unlike most
 * other SPI protocol stacks.
 */
static void
mmc_spi_data_do(struct mmc_spi_host *host, struct mmc_command *cmd,
		struct mmc_data *data, u32 blk_size)
{
	struct spi_device	*spi = host->spi;
	struct device		*dma_dev = host->dma_dev;
	struct spi_transfer	*t;
	enum dma_data_direction	direction;
	struct scatterlist	*sg;
	unsigned		n_sg;
	int			multiple = (data->blocks > 1);
	u32			clock_rate;
	unsigned long		timeout;

	direction = mmc_get_dma_dir(data);
	mmc_spi_setup_data_message(host, multiple, direction);
	t = &host->t;

	if (t->speed_hz)
		clock_rate = t->speed_hz;
	else
		clock_rate = spi->max_speed_hz;

	timeout = data->timeout_ns +
		  data->timeout_clks * 1000000 / clock_rate;
	timeout = usecs_to_jiffies((unsigned int)(timeout / 1000)) + 1;

	/* Handle scatterlist segments one at a time, with synch for
	 * each 512-byte block
	 */
	for_each_sg(data->sg, sg, data->sg_len, n_sg) {
		int			status = 0;
		dma_addr_t		dma_addr = 0;
		void			*kmap_addr;
		unsigned		length = sg->length;
		enum dma_data_direction	dir = direction;

		/* set up dma mapping for controller drivers that might
		 * use DMA ... though they may fall back to PIO
		 */
		if (dma_dev) {
			/* never invalidate whole *shared* pages ... */
			if ((sg->offset != 0 || length != PAGE_SIZE)
					&& dir == DMA_FROM_DEVICE)
				dir = DMA_BIDIRECTIONAL;

			dma_addr = dma_map_page(dma_dev, sg_page(sg), 0,
						PAGE_SIZE, dir);
			if (dma_mapping_error(dma_dev, dma_addr)) {
				data->error = -EFAULT;
				break;
			}
			if (direction == DMA_TO_DEVICE)
				t->tx_dma = dma_addr + sg->offset;
			else
				t->rx_dma = dma_addr + sg->offset;
		}

		/* allow pio too; we don't allow highmem */
		kmap_addr = kmap(sg_page(sg));
		if (direction == DMA_TO_DEVICE)
			t->tx_buf = kmap_addr + sg->offset;
		else
			t->rx_buf = kmap_addr + sg->offset;

		/* transfer each block, and update request status */
		while (length) {
			t->len = min(length, blk_size);

			dev_dbg(&host->spi->dev,
				"    mmc_spi: %s block, %d bytes\n",
				(direction == DMA_TO_DEVICE) ? "write" : "read",
				t->len);

			if (direction == DMA_TO_DEVICE)
				status = mmc_spi_writeblock(host, t, timeout);
			else
				status = mmc_spi_readblock(host, t, timeout);
			if (status < 0)
				break;

			data->bytes_xfered += t->len;
			length -= t->len;

			if (!multiple)
				break;
		}

		/* discard mappings */
		if (direction == DMA_FROM_DEVICE)
			flush_kernel_dcache_page(sg_page(sg));
		kunmap(sg_page(sg));
		if (dma_dev)
			dma_unmap_page(dma_dev, dma_addr, PAGE_SIZE, dir);

		if (status < 0) {
			data->error = status;
			dev_dbg(&spi->dev, "%s status %d\n",
				(direction == DMA_TO_DEVICE) ? "write" : "read",
				status);
			break;
		}
	}

	/* NOTE some docs describe an MMC-only SET_BLOCK_COUNT (CMD23) that
	 * can be issued before multiblock writes.  Unlike its more widely
	 * documented analogue for SD cards (SET_WR_BLK_ERASE_COUNT, ACMD23),
	 * that can affect the STOP_TRAN logic.   Complete (and current)
	 * MMC specs should sort that out before Linux starts using CMD23.
	 */
	if (direction == DMA_TO_DEVICE && multiple) {
		struct scratch	*scratch = host->data;
		int		tmp;
		const unsigned	statlen = sizeof(scratch->status);

		dev_dbg(&spi->dev, "    mmc_spi: STOP_TRAN\n");

		/* Tweak the per-block message we set up earlier by morphing
		 * it to hold single buffer with the token followed by some
		 * all-ones bytes ... skip N(BR) (0..1), scan the rest for
		 * "not busy any longer" status, and leave chip selected.
		 */
		INIT_LIST_HEAD(&host->m.transfers);
		list_add(&host->early_status.transfer_list,
				&host->m.transfers);

		memset(scratch->status, 0xff, statlen);
		scratch->status[0] = SPI_TOKEN_STOP_TRAN;

		host->early_status.tx_buf = host->early_status.rx_buf;
		host->early_status.tx_dma = host->early_status.rx_dma;
		host->early_status.len = statlen;

		if (host->dma_dev)
			dma_sync_single_for_device(host->dma_dev,
					host->data_dma, sizeof(*scratch),
					DMA_BIDIRECTIONAL);

		tmp = spi_sync_locked(spi, &host->m);

		if (host->dma_dev)
			dma_sync_single_for_cpu(host->dma_dev,
					host->data_dma, sizeof(*scratch),
					DMA_BIDIRECTIONAL);

		if (tmp < 0) {
			if (!data->error)
				data->error = tmp;
			return;
		}

		/* Ideally we collected "not busy" status with one I/O,
		 * avoiding wasteful byte-at-a-time scanning... but more
		 * I/O is often needed.
		 */
		for (tmp = 2; tmp < statlen; tmp++) {
			if (scratch->status[tmp] != 0)
				return;
		}
		tmp = mmc_spi_wait_unbusy(host, timeout);
		if (tmp < 0 && !data->error)
			data->error = tmp;
	}
}

/****************************************************************************/

/*
 * MMC driver implementation -- the interface to the MMC stack
 */

static void mmc_spi_request(struct mmc_host *mmc, struct mmc_request *mrq)
{
	struct mmc_spi_host	*host = mmc_priv(mmc);
	int			status = -EINVAL;
	int			crc_retry = 5;
	struct mmc_command	stop;

#ifdef DEBUG
	/* MMC core and layered drivers *MUST* issue SPI-aware commands */
	{
		struct mmc_command	*cmd;
		int			invalid = 0;

		cmd = mrq->cmd;
		if (!mmc_spi_resp_type(cmd)) {
			dev_dbg(&host->spi->dev, "bogus command\n");
			cmd->error = -EINVAL;
			invalid = 1;
		}

		cmd = mrq->stop;
		if (cmd && !mmc_spi_resp_type(cmd)) {
			dev_dbg(&host->spi->dev, "bogus STOP command\n");
			cmd->error = -EINVAL;
			invalid = 1;
		}

		if (invalid) {
			dump_stack();
			mmc_request_done(host->mmc, mrq);
			return;
		}
	}
#endif

	/* request exclusive bus access */
	spi_bus_lock(host->spi->master);

crc_recover:
	/* issue command; then optionally data and stop */
	status = mmc_spi_command_send(host, mrq, mrq->cmd, mrq->data != NULL);
	if (status == 0 && mrq->data) {
		mmc_spi_data_do(host, mrq->cmd, mrq->data, mrq->data->blksz);

		/*
		 * The SPI bus is not always reliable for large data transfers.
		 * If an occasional crc error is reported by the SD device with
		 * data read/write over SPI, it may be recovered by repeating
		 * the last SD command again. The retry count is set to 5 to
		 * ensure the driver passes stress tests.
		 */
		if (mrq->data->error == -EILSEQ && crc_retry) {
			stop.opcode = MMC_STOP_TRANSMISSION;
			stop.arg = 0;
			stop.flags = MMC_RSP_SPI_R1B | MMC_RSP_R1B | MMC_CMD_AC;
			status = mmc_spi_command_send(host, mrq, &stop, 0);
			crc_retry--;
			mrq->data->error = 0;
			goto crc_recover;
		}

		if (mrq->stop)
			status = mmc_spi_command_send(host, mrq, mrq->stop, 0);
		else
			mmc_cs_off(host);
	}

	/* release the bus */
	spi_bus_unlock(host->spi->master);

	mmc_request_done(host->mmc, mrq);
}

/* See Section 6.4.1, in SD "Simplified Physical Layer Specification 2.0"
 *
 * NOTE that here we can't know that the card has just been powered up;
 * not all MMC/SD sockets support power switching.
 *
 * FIXME when the card is still in SPI mode, e.g. from a previous kernel,
 * this doesn't seem to do the right thing at all...
 */
static void mmc_spi_initsequence(struct mmc_spi_host *host)
{
	/* Try to be very sure any previous command has completed;
	 * wait till not-busy, skip debris from any old commands.
	 */
	mmc_spi_wait_unbusy(host, r1b_timeout);
	mmc_spi_readbytes(host, 10);

	/*
	 * Do a burst with chipselect active-high.  We need to do this to
	 * meet the requirement of 74 clock cycles with both chipselect
	 * and CMD (MOSI) high before CMD0 ... after the card has been
	 * powered up to Vdd(min), and so is ready to take commands.
	 *
	 * Some cards are particularly needy of this (e.g. Viking "SD256")
	 * while most others don't seem to care.
	 *
	 * Note that this is one of the places MMC/SD plays games with the
	 * SPI protocol.  Another is that when chipselect is released while
	 * the card returns BUSY status, the clock must issue several cycles
	 * with chipselect high before the card will stop driving its output.
	 *
	 * SPI_CS_HIGH means "asserted" here. In some cases like when using
	 * GPIOs for chip select, SPI_CS_HIGH is set but this will be logically
	 * inverted by gpiolib, so if we want to ascertain to drive it high
	 * we should toggle the default with an XOR as we do here.
	 */
	host->spi->mode ^= SPI_CS_HIGH;
	if (spi_setup(host->spi) != 0) {
		/* Just warn; most cards work without it. */
		dev_warn(&host->spi->dev,
				"can't change chip-select polarity\n");
		host->spi->mode ^= SPI_CS_HIGH;
	} else {
		mmc_spi_readbytes(host, 18);

		host->spi->mode ^= SPI_CS_HIGH;
		if (spi_setup(host->spi) != 0) {
			/* Wot, we can't get the same setup we had before? */
			dev_err(&host->spi->dev,
					"can't restore chip-select polarity\n");
		}
	}
}

static char *mmc_powerstring(u8 power_mode)
{
	switch (power_mode) {
	case MMC_POWER_OFF: return "off";
	case MMC_POWER_UP:  return "up";
	case MMC_POWER_ON:  return "on";
	}
	return "?";
}

static void mmc_spi_set_ios(struct mmc_host *mmc, struct mmc_ios *ios)
{
	struct mmc_spi_host *host = mmc_priv(mmc);

	if (host->power_mode != ios->power_mode) {
		int		canpower;

		canpower = host->pdata && host->pdata->setpower;

		dev_dbg(&host->spi->dev, "mmc_spi: power %s (%d)%s\n",
				mmc_powerstring(ios->power_mode),
				ios->vdd,
				canpower ? ", can switch" : "");

		/* switch power on/off if possible, accounting for
		 * max 250msec powerup time if needed.
		 */
		if (canpower) {
			switch (ios->power_mode) {
			case MMC_POWER_OFF:
			case MMC_POWER_UP:
				host->pdata->setpower(&host->spi->dev,
						ios->vdd);
				if (ios->power_mode == MMC_POWER_UP)
					msleep(host->powerup_msecs);
			}
		}

		/* See 6.4.1 in the simplified SD card physical spec 2.0 */
		if (ios->power_mode == MMC_POWER_ON)
			mmc_spi_initsequence(host);

		/* If powering down, ground all card inputs to avoid power
		 * delivery from data lines!  On a shared SPI bus, this
		 * will probably be temporary; 6.4.2 of the simplified SD
		 * spec says this must last at least 1msec.
		 *
		 *   - Clock low means CPOL 0, e.g. mode 0
		 *   - MOSI low comes from writing zero
		 *   - Chipselect is usually active low...
		 */
		if (canpower && ios->power_mode == MMC_POWER_OFF) {
			int mres;
			u8 nullbyte = 0;

			host->spi->mode &= ~(SPI_CPOL|SPI_CPHA);
			mres = spi_setup(host->spi);
			if (mres < 0)
				dev_dbg(&host->spi->dev,
					"switch to SPI mode 0 failed\n");

			if (spi_write(host->spi, &nullbyte, 1) < 0)
				dev_dbg(&host->spi->dev,
					"put spi signals to low failed\n");

			/*
			 * Now clock should be low due to spi mode 0;
			 * MOSI should be low because of written 0x00;
			 * chipselect should be low (it is active low)
			 * power supply is off, so now MMC is off too!
			 *
			 * FIXME no, chipselect can be high since the
			 * device is inactive and SPI_CS_HIGH is clear...
			 */
			msleep(10);
			if (mres == 0) {
				host->spi->mode |= (SPI_CPOL|SPI_CPHA);
				mres = spi_setup(host->spi);
				if (mres < 0)
					dev_dbg(&host->spi->dev,
						"switch back to SPI mode 3 failed\n");
			}
		}

		host->power_mode = ios->power_mode;
	}

	if (host->spi->max_speed_hz != ios->clock && ios->clock != 0) {
		int		status;

		host->spi->max_speed_hz = ios->clock;
		status = spi_setup(host->spi);
		dev_dbg(&host->spi->dev,
			"mmc_spi:  clock to %d Hz, %d\n",
			host->spi->max_speed_hz, status);
	}
}

static const struct mmc_host_ops mmc_spi_ops = {
	.request	= mmc_spi_request,
	.set_ios	= mmc_spi_set_ios,
	.get_ro		= mmc_gpio_get_ro,
	.get_cd		= mmc_gpio_get_cd,
};


/****************************************************************************/

/*
 * SPI driver implementation
 */

static irqreturn_t
mmc_spi_detect_irq(int irq, void *mmc)
{
	struct mmc_spi_host *host = mmc_priv(mmc);
	u16 delay_msec = max(host->pdata->detect_delay, (u16)100);

	mmc_detect_change(mmc, msecs_to_jiffies(delay_msec));
	return IRQ_HANDLED;
}

static int mmc_spi_probe(struct spi_device *spi)
{
	void			*ones;
	struct mmc_host		*mmc;
	struct mmc_spi_host	*host;
	int			status;
	bool			has_ro = false;

	/* We rely on full duplex transfers, mostly to reduce
	 * per-transfer overheads (by making fewer transfers).
	 */
	if (spi->master->flags & SPI_MASTER_HALF_DUPLEX)
		return -EINVAL;

	/* MMC and SD specs only seem to care that sampling is on the
	 * rising edge ... meaning SPI modes 0 or 3.  So either SPI mode
	 * should be legit.  We'll use mode 0 since the steady state is 0,
	 * which is appropriate for hotplugging, unless the platform data
	 * specify mode 3 (if hardware is not compatible to mode 0).
	 */
	if (spi->mode != SPI_MODE_3)
		spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;

	status = spi_setup(spi);
	if (status < 0) {
		dev_dbg(&spi->dev, "needs SPI mode %02x, %d KHz; %d\n",
				spi->mode, spi->max_speed_hz / 1000,
				status);
		return status;
	}

	/* We need a supply of ones to transmit.  This is the only time
	 * the CPU touches these, so cache coherency isn't a concern.
	 *
	 * NOTE if many systems use more than one MMC-over-SPI connector
	 * it'd save some memory to share this.  That's evidently rare.
	 */
	status = -ENOMEM;
	ones = kmalloc(MMC_SPI_BLOCKSIZE, GFP_KERNEL);
	if (!ones)
		goto nomem;
	memset(ones, 0xff, MMC_SPI_BLOCKSIZE);

	mmc = mmc_alloc_host(sizeof(*host), &spi->dev);
	if (!mmc)
		goto nomem;

	mmc->ops = &mmc_spi_ops;
	mmc->max_blk_size = MMC_SPI_BLOCKSIZE;
	mmc->max_segs = MMC_SPI_BLOCKSATONCE;
	mmc->max_req_size = MMC_SPI_BLOCKSATONCE * MMC_SPI_BLOCKSIZE;
	mmc->max_blk_count = MMC_SPI_BLOCKSATONCE;

	mmc->caps = MMC_CAP_SPI;

	/* SPI doesn't need the lowspeed device identification thing for
	 * MMC or SD cards, since it never comes up in open drain mode.
	 * That's good; some SPI masters can't handle very low speeds!
	 *
	 * However, low speed SDIO cards need not handle over 400 KHz;
	 * that's the only reason not to use a few MHz for f_min (until
	 * the upper layer reads the target frequency from the CSD).
	 */
	mmc->f_min = 400000;
	mmc->f_max = spi->max_speed_hz;

	host = mmc_priv(mmc);
	host->mmc = mmc;
	host->spi = spi;

	host->ones = ones;

	/* Platform data is used to hook up things like card sensing
	 * and power switching gpios.
	 */
	host->pdata = mmc_spi_get_pdata(spi);
	if (host->pdata)
		mmc->ocr_avail = host->pdata->ocr_mask;
	if (!mmc->ocr_avail) {
		dev_warn(&spi->dev, "ASSUMING 3.2-3.4 V slot power\n");
		mmc->ocr_avail = MMC_VDD_32_33|MMC_VDD_33_34;
	}
	if (host->pdata && host->pdata->setpower) {
		host->powerup_msecs = host->pdata->powerup_msecs;
		if (!host->powerup_msecs || host->powerup_msecs > 250)
			host->powerup_msecs = 250;
	}

	dev_set_drvdata(&spi->dev, mmc);

	/* preallocate dma buffers */
	host->data = kmalloc(sizeof(*host->data), GFP_KERNEL);
	if (!host->data)
		goto fail_nobuf1;

	if (spi->master->dev.parent->dma_mask) {
		struct device	*dev = spi->master->dev.parent;

		host->dma_dev = dev;
		host->ones_dma = dma_map_single(dev, ones,
				MMC_SPI_BLOCKSIZE, DMA_TO_DEVICE);
		if (dma_mapping_error(dev, host->ones_dma))
			goto fail_ones_dma;
		host->data_dma = dma_map_single(dev, host->data,
				sizeof(*host->data), DMA_BIDIRECTIONAL);
		if (dma_mapping_error(dev, host->data_dma))
			goto fail_data_dma;

		dma_sync_single_for_cpu(host->dma_dev,
				host->data_dma, sizeof(*host->data),
				DMA_BIDIRECTIONAL);
	}

	/* setup message for status/busy readback */
	spi_message_init(&host->readback);
	host->readback.is_dma_mapped = (host->dma_dev != NULL);

	spi_message_add_tail(&host->status, &host->readback);
	host->status.tx_buf = host->ones;
	host->status.tx_dma = host->ones_dma;
	host->status.rx_buf = &host->data->status;
	host->status.rx_dma = host->data_dma + offsetof(struct scratch, status);
	host->status.cs_change = 1;

	/* register card detect irq */
	if (host->pdata && host->pdata->init) {
		status = host->pdata->init(&spi->dev, mmc_spi_detect_irq, mmc);
		if (status != 0)
			goto fail_glue_init;
	}

	/* pass platform capabilities, if any */
	if (host->pdata) {
		mmc->caps |= host->pdata->caps;
		mmc->caps2 |= host->pdata->caps2;
	}

	status = mmc_add_host(mmc);
	if (status != 0)
		goto fail_add_host;

	/*
	 * Index 0 is card detect
	 * Old boardfiles were specifying 1 ms as debounce
	 */
	status = mmc_gpiod_request_cd(mmc, NULL, 0, false, 1000, NULL);
	if (status == -EPROBE_DEFER)
		goto fail_add_host;
	if (!status) {
		/*
		 * The platform has a CD GPIO signal that may support
		 * interrupts, so let mmc_gpiod_request_cd_irq() decide
		 * if polling is needed or not.
		 */
		mmc->caps &= ~MMC_CAP_NEEDS_POLL;
		mmc_gpiod_request_cd_irq(mmc);
	}
	mmc_detect_change(mmc, 0);

	/* Index 1 is write protect/read only */
	status = mmc_gpiod_request_ro(mmc, NULL, 1, 0, NULL);
	if (status == -EPROBE_DEFER)
		goto fail_add_host;
	if (!status)
		has_ro = true;

	dev_info(&spi->dev, "SD/MMC host %s%s%s%s%s\n",
			dev_name(&mmc->class_dev),
			host->dma_dev ? "" : ", no DMA",
			has_ro ? "" : ", no WP",
			(host->pdata && host->pdata->setpower)
				? "" : ", no poweroff",
			(mmc->caps & MMC_CAP_NEEDS_POLL)
				? ", cd polling" : "");
	return 0;

fail_add_host:
	mmc_remove_host(mmc);
fail_glue_init:
	if (host->dma_dev)
		dma_unmap_single(host->dma_dev, host->data_dma,
				sizeof(*host->data), DMA_BIDIRECTIONAL);
fail_data_dma:
	if (host->dma_dev)
		dma_unmap_single(host->dma_dev, host->ones_dma,
				MMC_SPI_BLOCKSIZE, DMA_TO_DEVICE);
fail_ones_dma:
	kfree(host->data);

fail_nobuf1:
	mmc_free_host(mmc);
	mmc_spi_put_pdata(spi);

nomem:
	kfree(ones);
	return status;
}


static int mmc_spi_remove(struct spi_device *spi)
{
	struct mmc_host		*mmc = dev_get_drvdata(&spi->dev);
	struct mmc_spi_host	*host = mmc_priv(mmc);

	/* prevent new mmc_detect_change() calls */
	if (host->pdata && host->pdata->exit)
		host->pdata->exit(&spi->dev, mmc);

	mmc_remove_host(mmc);

	if (host->dma_dev) {
		dma_unmap_single(host->dma_dev, host->ones_dma,
			MMC_SPI_BLOCKSIZE, DMA_TO_DEVICE);
		dma_unmap_single(host->dma_dev, host->data_dma,
			sizeof(*host->data), DMA_BIDIRECTIONAL);
	}

	kfree(host->data);
	kfree(host->ones);

	spi->max_speed_hz = mmc->f_max;
	mmc_free_host(mmc);
	mmc_spi_put_pdata(spi);
	return 0;
}

static const struct of_device_id mmc_spi_of_match_table[] = {
	{ .compatible = "mmc-spi-slot", },
	{},
};
MODULE_DEVICE_TABLE(of, mmc_spi_of_match_table);

static struct spi_driver mmc_spi_driver = {
	.driver = {
		.name =		"mmc_spi",
		.of_match_table = mmc_spi_of_match_table,
	},
	.probe =	mmc_spi_probe,
	.remove =	mmc_spi_remove,
};

module_spi_driver(mmc_spi_driver);

MODULE_AUTHOR("Mike Lavender, David Brownell, Hans-Peter Nilsson, Jan Nikitenko");
MODULE_DESCRIPTION("SPI SD/MMC host driver");
MODULE_LICENSE("GPL");
MODULE_ALIAS("spi:mmc_spi");
