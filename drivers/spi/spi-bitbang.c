/*
 * polling/bitbanging SPI master controller driver utilities
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/init.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/platform_device.h>
#include <linux/slab.h>

#include <linux/spi/spi.h>
#include <linux/spi/spi_bitbang.h>


/*----------------------------------------------------------------------*/

/*
 * FIRST PART (OPTIONAL):  word-at-a-time spi_transfer support.
 * Use this for GPIO or shift-register level hardware APIs.
 *
 * spi_bitbang_cs is in spi_device->controller_state, which is unavailable
 * to glue code.  These bitbang setup() and cleanup() routines are always
 * used, though maybe they're called from controller-aware code.
 *
 * chipselect() and friends may use use spi_device->controller_data and
 * controller registers as appropriate.
 *
 *
 * NOTE:  SPI controller pins can often be used as GPIO pins instead,
 * which means you could use a bitbang driver either to get hardware
 * working quickly, or testing for differences that aren't speed related.
 */

struct spi_bitbang_cs {
	unsigned	nsecs;	/* (clock cycle time)/2 */
	u32		(*txrx_word)(struct spi_device *spi, unsigned nsecs,
					u32 word, u8 bits);
	unsigned	(*txrx_bufs)(struct spi_device *,
					u32 (*txrx_word)(
						struct spi_device *spi,
						unsigned nsecs,
						u32 word, u8 bits),
					unsigned, struct spi_transfer *);
};

static unsigned bitbang_txrx_8(
	struct spi_device	*spi,
	u32			(*txrx_word)(struct spi_device *spi,
					unsigned nsecs,
					u32 word, u8 bits),
	unsigned		ns,
	struct spi_transfer	*t
) {
	unsigned		bits = t->bits_per_word;
	unsigned		count = t->len;
	const u8		*tx = t->tx_buf;
	u8			*rx = t->rx_buf;

	while (likely(count > 0)) {
		u8		word = 0;

		if (tx)
			word = *tx++;
		word = txrx_word(spi, ns, word, bits);
		if (rx)
			*rx++ = word;
		count -= 1;
	}
	return t->len - count;
}

static unsigned bitbang_txrx_16(
	struct spi_device	*spi,
	u32			(*txrx_word)(struct spi_device *spi,
					unsigned nsecs,
					u32 word, u8 bits),
	unsigned		ns,
	struct spi_transfer	*t
) {
	unsigned		bits = t->bits_per_word;
	unsigned		count = t->len;
	const u16		*tx = t->tx_buf;
	u16			*rx = t->rx_buf;

	while (likely(count > 1)) {
		u16		word = 0;

		if (tx)
			word = *tx++;
		word = txrx_word(spi, ns, word, bits);
		if (rx)
			*rx++ = word;
		count -= 2;
	}
	return t->len - count;
}

static unsigned bitbang_txrx_32(
	struct spi_device	*spi,
	u32			(*txrx_word)(struct spi_device *spi,
					unsigned nsecs,
					u32 word, u8 bits),
	unsigned		ns,
	struct spi_transfer	*t
) {
	unsigned		bits = t->bits_per_word;
	unsigned		count = t->len;
	const u32		*tx = t->tx_buf;
	u32			*rx = t->rx_buf;

	while (likely(count > 3)) {
		u32		word = 0;

		if (tx)
			word = *tx++;
		word = txrx_word(spi, ns, word, bits);
		if (rx)
			*rx++ = word;
		count -= 4;
	}
	return t->len - count;
}

int spi_bitbang_setup_transfer(struct spi_device *spi, struct spi_transfer *t)
{
	struct spi_bitbang_cs	*cs = spi->controller_state;
	u8			bits_per_word;
	u32			hz;

	if (t) {
		bits_per_word = t->bits_per_word;
		hz = t->speed_hz;
	} else {
		bits_per_word = 0;
		hz = 0;
	}

	/* spi_transfer level calls that work per-word */
	if (!bits_per_word)
		bits_per_word = spi->bits_per_word;
	if (bits_per_word <= 8)
		cs->txrx_bufs = bitbang_txrx_8;
	else if (bits_per_word <= 16)
		cs->txrx_bufs = bitbang_txrx_16;
	else if (bits_per_word <= 32)
		cs->txrx_bufs = bitbang_txrx_32;
	else
		return -EINVAL;

	/* nsecs = (clock period)/2 */
	if (!hz)
		hz = spi->max_speed_hz;
	if (hz) {
		cs->nsecs = (1000000000/2) / hz;
		if (cs->nsecs > (MAX_UDELAY_MS * 1000 * 1000))
			return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(spi_bitbang_setup_transfer);

/**
 * spi_bitbang_setup - default setup for per-word I/O loops
 */
int spi_bitbang_setup(struct spi_device *spi)
{
	struct spi_bitbang_cs	*cs = spi->controller_state;
	struct spi_bitbang	*bitbang;
	int			retval;
	unsigned long		flags;

	bitbang = spi_master_get_devdata(spi->master);

	if (!cs) {
		cs = kzalloc(sizeof *cs, GFP_KERNEL);
		if (!cs)
			return -ENOMEM;
		spi->controller_state = cs;
	}

	/* per-word shift register access, in hardware or bitbanging */
	cs->txrx_word = bitbang->txrx_word[spi->mode & (SPI_CPOL|SPI_CPHA)];
	if (!cs->txrx_word)
		return -EINVAL;

	retval = bitbang->setup_transfer(spi, NULL);
	if (retval < 0)
		return retval;

	dev_dbg(&spi->dev, "%s, %u nsec/bit\n", __func__, 2 * cs->nsecs);

	/* NOTE we _need_ to call chipselect() early, ideally with adapter
	 * setup, unless the hardware defaults cooperate to avoid confusion
	 * between normal (active low) and inverted chipselects.
	 */

	/* deselect chip (low or high) */
	spin_lock_irqsave(&bitbang->lock, flags);
	if (!bitbang->busy) {
		bitbang->chipselect(spi, BITBANG_CS_INACTIVE);
		ndelay(cs->nsecs);
	}
	spin_unlock_irqrestore(&bitbang->lock, flags);

	return 0;
}
EXPORT_SYMBOL_GPL(spi_bitbang_setup);

/**
 * spi_bitbang_cleanup - default cleanup for per-word I/O loops
 */
void spi_bitbang_cleanup(struct spi_device *spi)
{
	kfree(spi->controller_state);
}
EXPORT_SYMBOL_GPL(spi_bitbang_cleanup);

static int spi_bitbang_bufs(struct spi_device *spi, struct spi_transfer *t)
{
	struct spi_bitbang_cs	*cs = spi->controller_state;
	unsigned		nsecs = cs->nsecs;

	return cs->txrx_bufs(spi, cs->txrx_word, nsecs, t);
}

/*----------------------------------------------------------------------*/

/*
 * SECOND PART ... simple transfer queue runner.
 *
 * This costs a task context per controller, running the queue by
 * performing each transfer in sequence.  Smarter hardware can queue
 * several DMA transfers at once, and process several controller queues
 * in parallel; this driver doesn't match such hardware very well.
 *
 * Drivers can provide word-at-a-time i/o primitives, or provide
 * transfer-at-a-time ones to leverage dma or fifo hardware.
 */

static int spi_bitbang_prepare_hardware(struct spi_master *spi)
{
	struct spi_bitbang 	*bitbang;
	unsigned long		flags;

	bitbang = spi_master_get_devdata(spi);

	spin_lock_irqsave(&bitbang->lock, flags);
	bitbang->busy = 1;
	spin_unlock_irqrestore(&bitbang->lock, flags);

	return 0;
}

static int spi_bitbang_transfer_one(struct spi_master *master,
				    struct spi_message *m)
{
	struct spi_bitbang 	*bitbang;
	unsigned		nsecs;
	struct spi_transfer	*t = NULL;
	unsigned		cs_change;
	int			status;
	int			do_setup = -1;
	struct spi_device	*spi = m->spi;

	bitbang = spi_master_get_devdata(master);

	/* FIXME this is made-up ... the correct value is known to
	 * word-at-a-time bitbang code, and presumably chipselect()
	 * should enforce these requirements too?
	 */
	nsecs = 100;

	cs_change = 1;
	status = 0;

	list_for_each_entry (t, &m->transfers, transfer_list) {

		/* override speed or wordsize? */
		if (t->speed_hz || t->bits_per_word)
			do_setup = 1;

		/* init (-1) or override (1) transfer params */
		if (do_setup != 0) {
			status = bitbang->setup_transfer(spi, t);
			if (status < 0)
				break;
			if (do_setup == -1)
				do_setup = 0;
		}

		/* set up default clock polarity, and activate chip;
		 * this implicitly updates clock and spi modes as
		 * previously recorded for this device via setup().
		 * (and also deselects any other chip that might be
		 * selected ...)
		 */
		if (cs_change) {
			bitbang->chipselect(spi, BITBANG_CS_ACTIVE);
			ndelay(nsecs);
		}
		cs_change = t->cs_change;
		if (!t->tx_buf && !t->rx_buf && t->len) {
			status = -EINVAL;
			break;
		}

		/* transfer data.  the lower level code handles any
		 * new dma mappings it needs. our caller always gave
		 * us dma-safe buffers.
		 */
		if (t->len) {
			/* REVISIT dma API still needs a designated
			 * DMA_ADDR_INVALID; ~0 might be better.
			 */
			if (!m->is_dma_mapped)
				t->rx_dma = t->tx_dma = 0;
			status = bitbang->txrx_bufs(spi, t);
		}
		if (status > 0)
			m->actual_length += status;
		if (status != t->len) {
			/* always report some kind of error */
			if (status >= 0)
				status = -EREMOTEIO;
			break;
		}
		status = 0;

		/* protocol tweaks before next transfer */
		if (t->delay_usecs)
			udelay(t->delay_usecs);

		if (cs_change && !list_is_last(&t->transfer_list, &m->transfers)) {
			/* sometimes a short mid-message deselect of the chip
			 * may be needed to terminate a mode or command
			 */
			ndelay(nsecs);
			bitbang->chipselect(spi, BITBANG_CS_INACTIVE);
			ndelay(nsecs);
		}
	}

	m->status = status;

	/* normally deactivate chipselect ... unless no error and
	 * cs_change has hinted that the next message will probably
	 * be for this chip too.
	 */
	if (!(status == 0 && cs_change)) {
		ndelay(nsecs);
		bitbang->chipselect(spi, BITBANG_CS_INACTIVE);
		ndelay(nsecs);
	}

	spi_finalize_current_message(master);

	return status;
}

static int spi_bitbang_unprepare_hardware(struct spi_master *spi)
{
	struct spi_bitbang 	*bitbang;
	unsigned long		flags;

	bitbang = spi_master_get_devdata(spi);

	spin_lock_irqsave(&bitbang->lock, flags);
	bitbang->busy = 0;
	spin_unlock_irqrestore(&bitbang->lock, flags);

	return 0;
}

/*----------------------------------------------------------------------*/

/**
 * spi_bitbang_start - start up a polled/bitbanging SPI master driver
 * @bitbang: driver handle
 *
 * Caller should have zero-initialized all parts of the structure, and then
 * provided callbacks for chip selection and I/O loops.  If the master has
 * a transfer method, its final step should call spi_bitbang_transfer; or,
 * that's the default if the transfer routine is not initialized.  It should
 * also set up the bus number and number of chipselects.
 *
 * For i/o loops, provide callbacks either per-word (for bitbanging, or for
 * hardware that basically exposes a shift register) or per-spi_transfer
 * (which takes better advantage of hardware like fifos or DMA engines).
 *
 * Drivers using per-word I/O loops should use (or call) spi_bitbang_setup,
 * spi_bitbang_cleanup and spi_bitbang_setup_transfer to handle those spi
 * master methods.  Those methods are the defaults if the bitbang->txrx_bufs
 * routine isn't initialized.
 *
 * This routine registers the spi_master, which will process requests in a
 * dedicated task, keeping IRQs unblocked most of the time.  To stop
 * processing those requests, call spi_bitbang_stop().
 */
int spi_bitbang_start(struct spi_bitbang *bitbang)
{
	struct spi_master *master = bitbang->master;

	if (!master || !bitbang->chipselect)
		return -EINVAL;

	spin_lock_init(&bitbang->lock);

	if (!master->mode_bits)
		master->mode_bits = SPI_CPOL | SPI_CPHA | bitbang->flags;

	if (master->transfer || master->transfer_one_message)
		return -EINVAL;

	master->prepare_transfer_hardware = spi_bitbang_prepare_hardware;
	master->unprepare_transfer_hardware = spi_bitbang_unprepare_hardware;
	master->transfer_one_message = spi_bitbang_transfer_one;

	if (!bitbang->txrx_bufs) {
		bitbang->use_dma = 0;
		bitbang->txrx_bufs = spi_bitbang_bufs;
		if (!master->setup) {
			if (!bitbang->setup_transfer)
				bitbang->setup_transfer =
					 spi_bitbang_setup_transfer;
			master->setup = spi_bitbang_setup;
			master->cleanup = spi_bitbang_cleanup;
		}
	}

	/* driver may get busy before register() returns, especially
	 * if someone registered boardinfo for devices
	 */
	return spi_register_master(master);
}
EXPORT_SYMBOL_GPL(spi_bitbang_start);

/**
 * spi_bitbang_stop - stops the task providing spi communication
 */
int spi_bitbang_stop(struct spi_bitbang *bitbang)
{
	spi_unregister_master(bitbang->master);

	return 0;
}
EXPORT_SYMBOL_GPL(spi_bitbang_stop);

MODULE_LICENSE("GPL");

