// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * polling/bitbanging SPI master controller driver utilities
 */

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

#define SPI_BITBANG_CS_DELAY	100


/*----------------------------------------------------------------------*/

/*
 * FIRST PART (OPTIONAL):  word-at-a-time spi_transfer support.
 * Use this for GPIO or shift-register level hardware APIs.
 *
 * spi_bitbang_cs is in spi_device->controller_state, which is unavailable
 * to glue code.  These bitbang setup() and cleanup() routines are always
 * used, though maybe they're called from controller-aware code.
 *
 * chipselect() and friends may use spi_device->controller_data and
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
					u32 word, u8 bits, unsigned flags);
	unsigned	(*txrx_bufs)(struct spi_device *,
					u32 (*txrx_word)(
						struct spi_device *spi,
						unsigned nsecs,
						u32 word, u8 bits,
						unsigned flags),
					unsigned, struct spi_transfer *,
					unsigned);
};

static unsigned bitbang_txrx_8(
	struct spi_device	*spi,
	u32			(*txrx_word)(struct spi_device *spi,
					unsigned nsecs,
					u32 word, u8 bits,
					unsigned flags),
	unsigned		ns,
	struct spi_transfer	*t,
	unsigned flags
)
{
	unsigned		bits = t->bits_per_word;
	unsigned		count = t->len;
	const u8		*tx = t->tx_buf;
	u8			*rx = t->rx_buf;

	while (likely(count > 0)) {
		u8		word = 0;

		if (tx)
			word = *tx++;
		word = txrx_word(spi, ns, word, bits, flags);
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
					u32 word, u8 bits,
					unsigned flags),
	unsigned		ns,
	struct spi_transfer	*t,
	unsigned flags
)
{
	unsigned		bits = t->bits_per_word;
	unsigned		count = t->len;
	const u16		*tx = t->tx_buf;
	u16			*rx = t->rx_buf;

	while (likely(count > 1)) {
		u16		word = 0;

		if (tx)
			word = *tx++;
		word = txrx_word(spi, ns, word, bits, flags);
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
					u32 word, u8 bits,
					unsigned flags),
	unsigned		ns,
	struct spi_transfer	*t,
	unsigned flags
)
{
	unsigned		bits = t->bits_per_word;
	unsigned		count = t->len;
	const u32		*tx = t->tx_buf;
	u32			*rx = t->rx_buf;

	while (likely(count > 3)) {
		u32		word = 0;

		if (tx)
			word = *tx++;
		word = txrx_word(spi, ns, word, bits, flags);
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

/*
 * spi_bitbang_setup - default setup for per-word I/O loops
 */
int spi_bitbang_setup(struct spi_device *spi)
{
	struct spi_bitbang_cs	*cs = spi->controller_state;
	struct spi_bitbang	*bitbang;

	bitbang = spi_master_get_devdata(spi->master);

	if (!cs) {
		cs = kzalloc(sizeof(*cs), GFP_KERNEL);
		if (!cs)
			return -ENOMEM;
		spi->controller_state = cs;
	}

	/* per-word shift register access, in hardware or bitbanging */
	cs->txrx_word = bitbang->txrx_word[spi->mode & (SPI_CPOL|SPI_CPHA)];
	if (!cs->txrx_word)
		return -EINVAL;

	if (bitbang->setup_transfer) {
		int retval = bitbang->setup_transfer(spi, NULL);
		if (retval < 0)
			return retval;
	}

	dev_dbg(&spi->dev, "%s, %u nsec/bit\n", __func__, 2 * cs->nsecs);

	return 0;
}
EXPORT_SYMBOL_GPL(spi_bitbang_setup);

/*
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
	struct spi_bitbang	*bitbang;

	bitbang = spi_master_get_devdata(spi->master);
	if (bitbang->set_line_direction) {
		int err;

		err = bitbang->set_line_direction(spi, !!(t->tx_buf));
		if (err < 0)
			return err;
	}

	if (spi->mode & SPI_3WIRE) {
		unsigned flags;

		flags = t->tx_buf ? SPI_MASTER_NO_RX : SPI_MASTER_NO_TX;
		return cs->txrx_bufs(spi, cs->txrx_word, nsecs, t, flags);
	}
	return cs->txrx_bufs(spi, cs->txrx_word, nsecs, t, 0);
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
	struct spi_bitbang	*bitbang;

	bitbang = spi_master_get_devdata(spi);

	mutex_lock(&bitbang->lock);
	bitbang->busy = 1;
	mutex_unlock(&bitbang->lock);

	return 0;
}

static int spi_bitbang_transfer_one(struct spi_master *master,
				    struct spi_device *spi,
				    struct spi_transfer *transfer)
{
	struct spi_bitbang *bitbang = spi_master_get_devdata(master);
	int status = 0;

	if (bitbang->setup_transfer) {
		status = bitbang->setup_transfer(spi, transfer);
		if (status < 0)
			goto out;
	}

	if (transfer->len)
		status = bitbang->txrx_bufs(spi, transfer);

	if (status == transfer->len)
		status = 0;
	else if (status >= 0)
		status = -EREMOTEIO;

out:
	spi_finalize_current_transfer(master);

	return status;
}

static int spi_bitbang_unprepare_hardware(struct spi_master *spi)
{
	struct spi_bitbang	*bitbang;

	bitbang = spi_master_get_devdata(spi);

	mutex_lock(&bitbang->lock);
	bitbang->busy = 0;
	mutex_unlock(&bitbang->lock);

	return 0;
}

static void spi_bitbang_set_cs(struct spi_device *spi, bool enable)
{
	struct spi_bitbang *bitbang = spi_master_get_devdata(spi->master);

	/* SPI core provides CS high / low, but bitbang driver
	 * expects CS active
	 * spi device driver takes care of handling SPI_CS_HIGH
	 */
	enable = (!!(spi->mode & SPI_CS_HIGH) == enable);

	ndelay(SPI_BITBANG_CS_DELAY);
	bitbang->chipselect(spi, enable ? BITBANG_CS_ACTIVE :
			    BITBANG_CS_INACTIVE);
	ndelay(SPI_BITBANG_CS_DELAY);
}

/*----------------------------------------------------------------------*/

int spi_bitbang_init(struct spi_bitbang *bitbang)
{
	struct spi_master *master = bitbang->master;
	bool custom_cs;

	if (!master)
		return -EINVAL;
	/*
	 * We only need the chipselect callback if we are actually using it.
	 * If we just use GPIO descriptors, it is surplus. If the
	 * SPI_MASTER_GPIO_SS flag is set, we always need to call the
	 * driver-specific chipselect routine.
	 */
	custom_cs = (!master->use_gpio_descriptors ||
		     (master->flags & SPI_MASTER_GPIO_SS));

	if (custom_cs && !bitbang->chipselect)
		return -EINVAL;

	mutex_init(&bitbang->lock);

	if (!master->mode_bits)
		master->mode_bits = SPI_CPOL | SPI_CPHA | bitbang->flags;

	if (master->transfer || master->transfer_one_message)
		return -EINVAL;

	master->prepare_transfer_hardware = spi_bitbang_prepare_hardware;
	master->unprepare_transfer_hardware = spi_bitbang_unprepare_hardware;
	master->transfer_one = spi_bitbang_transfer_one;
	/*
	 * When using GPIO descriptors, the ->set_cs() callback doesn't even
	 * get called unless SPI_MASTER_GPIO_SS is set.
	 */
	if (custom_cs)
		master->set_cs = spi_bitbang_set_cs;

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

	return 0;
}
EXPORT_SYMBOL_GPL(spi_bitbang_init);

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
 *
 * On success, this routine will take a reference to master. The caller is
 * responsible for calling spi_bitbang_stop() to decrement the reference and
 * spi_master_put() as counterpart of spi_alloc_master() to prevent a memory
 * leak.
 */
int spi_bitbang_start(struct spi_bitbang *bitbang)
{
	struct spi_master *master = bitbang->master;
	int ret;

	ret = spi_bitbang_init(bitbang);
	if (ret)
		return ret;

	/* driver may get busy before register() returns, especially
	 * if someone registered boardinfo for devices
	 */
	ret = spi_register_master(spi_master_get(master));
	if (ret)
		spi_master_put(master);

	return ret;
}
EXPORT_SYMBOL_GPL(spi_bitbang_start);

/*
 * spi_bitbang_stop - stops the task providing spi communication
 */
void spi_bitbang_stop(struct spi_bitbang *bitbang)
{
	spi_unregister_master(bitbang->master);
}
EXPORT_SYMBOL_GPL(spi_bitbang_stop);

MODULE_LICENSE("GPL");

