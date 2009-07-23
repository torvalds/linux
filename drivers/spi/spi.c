/*
 * spi.c - SPI init/core code
 *
 * Copyright (C) 2005 David Brownell
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
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 */

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/cache.h>
#include <linux/mutex.h>
#include <linux/spi/spi.h>


/* SPI bustype and spi_master class are registered after board init code
 * provides the SPI device tables, ensuring that both are present by the
 * time controller driver registration causes spi_devices to "enumerate".
 */
static void spidev_release(struct device *dev)
{
	struct spi_device	*spi = to_spi_device(dev);

	/* spi masters may cleanup for released devices */
	if (spi->master->cleanup)
		spi->master->cleanup(spi);

	spi_master_put(spi->master);
	kfree(dev);
}

static ssize_t
modalias_show(struct device *dev, struct device_attribute *a, char *buf)
{
	const struct spi_device	*spi = to_spi_device(dev);

	return sprintf(buf, "%s\n", spi->modalias);
}

static struct device_attribute spi_dev_attrs[] = {
	__ATTR_RO(modalias),
	__ATTR_NULL,
};

/* modalias support makes "modprobe $MODALIAS" new-style hotplug work,
 * and the sysfs version makes coldplug work too.
 */

static int spi_match_device(struct device *dev, struct device_driver *drv)
{
	const struct spi_device	*spi = to_spi_device(dev);

	return strcmp(spi->modalias, drv->name) == 0;
}

static int spi_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	const struct spi_device		*spi = to_spi_device(dev);

	add_uevent_var(env, "MODALIAS=%s", spi->modalias);
	return 0;
}

#ifdef	CONFIG_PM

static int spi_suspend(struct device *dev, pm_message_t message)
{
	int			value = 0;
	struct spi_driver	*drv = to_spi_driver(dev->driver);

	/* suspend will stop irqs and dma; no more i/o */
	if (drv) {
		if (drv->suspend)
			value = drv->suspend(to_spi_device(dev), message);
		else
			dev_dbg(dev, "... can't suspend\n");
	}
	return value;
}

static int spi_resume(struct device *dev)
{
	int			value = 0;
	struct spi_driver	*drv = to_spi_driver(dev->driver);

	/* resume may restart the i/o queue */
	if (drv) {
		if (drv->resume)
			value = drv->resume(to_spi_device(dev));
		else
			dev_dbg(dev, "... can't resume\n");
	}
	return value;
}

#else
#define spi_suspend	NULL
#define spi_resume	NULL
#endif

struct bus_type spi_bus_type = {
	.name		= "spi",
	.dev_attrs	= spi_dev_attrs,
	.match		= spi_match_device,
	.uevent		= spi_uevent,
	.suspend	= spi_suspend,
	.resume		= spi_resume,
};
EXPORT_SYMBOL_GPL(spi_bus_type);


static int spi_drv_probe(struct device *dev)
{
	const struct spi_driver		*sdrv = to_spi_driver(dev->driver);

	return sdrv->probe(to_spi_device(dev));
}

static int spi_drv_remove(struct device *dev)
{
	const struct spi_driver		*sdrv = to_spi_driver(dev->driver);

	return sdrv->remove(to_spi_device(dev));
}

static void spi_drv_shutdown(struct device *dev)
{
	const struct spi_driver		*sdrv = to_spi_driver(dev->driver);

	sdrv->shutdown(to_spi_device(dev));
}

/**
 * spi_register_driver - register a SPI driver
 * @sdrv: the driver to register
 * Context: can sleep
 */
int spi_register_driver(struct spi_driver *sdrv)
{
	sdrv->driver.bus = &spi_bus_type;
	if (sdrv->probe)
		sdrv->driver.probe = spi_drv_probe;
	if (sdrv->remove)
		sdrv->driver.remove = spi_drv_remove;
	if (sdrv->shutdown)
		sdrv->driver.shutdown = spi_drv_shutdown;
	return driver_register(&sdrv->driver);
}
EXPORT_SYMBOL_GPL(spi_register_driver);

/*-------------------------------------------------------------------------*/

/* SPI devices should normally not be created by SPI device drivers; that
 * would make them board-specific.  Similarly with SPI master drivers.
 * Device registration normally goes into like arch/.../mach.../board-YYY.c
 * with other readonly (flashable) information about mainboard devices.
 */

struct boardinfo {
	struct list_head	list;
	unsigned		n_board_info;
	struct spi_board_info	board_info[0];
};

static LIST_HEAD(board_list);
static DEFINE_MUTEX(board_lock);

/**
 * spi_alloc_device - Allocate a new SPI device
 * @master: Controller to which device is connected
 * Context: can sleep
 *
 * Allows a driver to allocate and initialize a spi_device without
 * registering it immediately.  This allows a driver to directly
 * fill the spi_device with device parameters before calling
 * spi_add_device() on it.
 *
 * Caller is responsible to call spi_add_device() on the returned
 * spi_device structure to add it to the SPI master.  If the caller
 * needs to discard the spi_device without adding it, then it should
 * call spi_dev_put() on it.
 *
 * Returns a pointer to the new device, or NULL.
 */
struct spi_device *spi_alloc_device(struct spi_master *master)
{
	struct spi_device	*spi;
	struct device		*dev = master->dev.parent;

	if (!spi_master_get(master))
		return NULL;

	spi = kzalloc(sizeof *spi, GFP_KERNEL);
	if (!spi) {
		dev_err(dev, "cannot alloc spi_device\n");
		spi_master_put(master);
		return NULL;
	}

	spi->master = master;
	spi->dev.parent = dev;
	spi->dev.bus = &spi_bus_type;
	spi->dev.release = spidev_release;
	device_initialize(&spi->dev);
	return spi;
}
EXPORT_SYMBOL_GPL(spi_alloc_device);

/**
 * spi_add_device - Add spi_device allocated with spi_alloc_device
 * @spi: spi_device to register
 *
 * Companion function to spi_alloc_device.  Devices allocated with
 * spi_alloc_device can be added onto the spi bus with this function.
 *
 * Returns 0 on success; negative errno on failure
 */
int spi_add_device(struct spi_device *spi)
{
	static DEFINE_MUTEX(spi_add_lock);
	struct device *dev = spi->master->dev.parent;
	int status;

	/* Chipselects are numbered 0..max; validate. */
	if (spi->chip_select >= spi->master->num_chipselect) {
		dev_err(dev, "cs%d >= max %d\n",
			spi->chip_select,
			spi->master->num_chipselect);
		return -EINVAL;
	}

	/* Set the bus ID string */
	dev_set_name(&spi->dev, "%s.%u", dev_name(&spi->master->dev),
			spi->chip_select);


	/* We need to make sure there's no other device with this
	 * chipselect **BEFORE** we call setup(), else we'll trash
	 * its configuration.  Lock against concurrent add() calls.
	 */
	mutex_lock(&spi_add_lock);

	if (bus_find_device_by_name(&spi_bus_type, NULL, dev_name(&spi->dev))
			!= NULL) {
		dev_err(dev, "chipselect %d already in use\n",
				spi->chip_select);
		status = -EBUSY;
		goto done;
	}

	/* Drivers may modify this initial i/o setup, but will
	 * normally rely on the device being setup.  Devices
	 * using SPI_CS_HIGH can't coexist well otherwise...
	 */
	status = spi_setup(spi);
	if (status < 0) {
		dev_err(dev, "can't %s %s, status %d\n",
				"setup", dev_name(&spi->dev), status);
		goto done;
	}

	/* Device may be bound to an active driver when this returns */
	status = device_add(&spi->dev);
	if (status < 0)
		dev_err(dev, "can't %s %s, status %d\n",
				"add", dev_name(&spi->dev), status);
	else
		dev_dbg(dev, "registered child %s\n", dev_name(&spi->dev));

done:
	mutex_unlock(&spi_add_lock);
	return status;
}
EXPORT_SYMBOL_GPL(spi_add_device);

/**
 * spi_new_device - instantiate one new SPI device
 * @master: Controller to which device is connected
 * @chip: Describes the SPI device
 * Context: can sleep
 *
 * On typical mainboards, this is purely internal; and it's not needed
 * after board init creates the hard-wired devices.  Some development
 * platforms may not be able to use spi_register_board_info though, and
 * this is exported so that for example a USB or parport based adapter
 * driver could add devices (which it would learn about out-of-band).
 *
 * Returns the new device, or NULL.
 */
struct spi_device *spi_new_device(struct spi_master *master,
				  struct spi_board_info *chip)
{
	struct spi_device	*proxy;
	int			status;

	/* NOTE:  caller did any chip->bus_num checks necessary.
	 *
	 * Also, unless we change the return value convention to use
	 * error-or-pointer (not NULL-or-pointer), troubleshootability
	 * suggests syslogged diagnostics are best here (ugh).
	 */

	proxy = spi_alloc_device(master);
	if (!proxy)
		return NULL;

	WARN_ON(strlen(chip->modalias) >= sizeof(proxy->modalias));

	proxy->chip_select = chip->chip_select;
	proxy->max_speed_hz = chip->max_speed_hz;
	proxy->mode = chip->mode;
	proxy->irq = chip->irq;
	strlcpy(proxy->modalias, chip->modalias, sizeof(proxy->modalias));
	proxy->dev.platform_data = (void *) chip->platform_data;
	proxy->controller_data = chip->controller_data;
	proxy->controller_state = NULL;

	status = spi_add_device(proxy);
	if (status < 0) {
		spi_dev_put(proxy);
		return NULL;
	}

	return proxy;
}
EXPORT_SYMBOL_GPL(spi_new_device);

/**
 * spi_register_board_info - register SPI devices for a given board
 * @info: array of chip descriptors
 * @n: how many descriptors are provided
 * Context: can sleep
 *
 * Board-specific early init code calls this (probably during arch_initcall)
 * with segments of the SPI device table.  Any device nodes are created later,
 * after the relevant parent SPI controller (bus_num) is defined.  We keep
 * this table of devices forever, so that reloading a controller driver will
 * not make Linux forget about these hard-wired devices.
 *
 * Other code can also call this, e.g. a particular add-on board might provide
 * SPI devices through its expansion connector, so code initializing that board
 * would naturally declare its SPI devices.
 *
 * The board info passed can safely be __initdata ... but be careful of
 * any embedded pointers (platform_data, etc), they're copied as-is.
 */
int __init
spi_register_board_info(struct spi_board_info const *info, unsigned n)
{
	struct boardinfo	*bi;

	bi = kmalloc(sizeof(*bi) + n * sizeof *info, GFP_KERNEL);
	if (!bi)
		return -ENOMEM;
	bi->n_board_info = n;
	memcpy(bi->board_info, info, n * sizeof *info);

	mutex_lock(&board_lock);
	list_add_tail(&bi->list, &board_list);
	mutex_unlock(&board_lock);
	return 0;
}

/* FIXME someone should add support for a __setup("spi", ...) that
 * creates board info from kernel command lines
 */

static void scan_boardinfo(struct spi_master *master)
{
	struct boardinfo	*bi;

	mutex_lock(&board_lock);
	list_for_each_entry(bi, &board_list, list) {
		struct spi_board_info	*chip = bi->board_info;
		unsigned		n;

		for (n = bi->n_board_info; n > 0; n--, chip++) {
			if (chip->bus_num != master->bus_num)
				continue;
			/* NOTE: this relies on spi_new_device to
			 * issue diagnostics when given bogus inputs
			 */
			(void) spi_new_device(master, chip);
		}
	}
	mutex_unlock(&board_lock);
}

/*-------------------------------------------------------------------------*/

static void spi_master_release(struct device *dev)
{
	struct spi_master *master;

	master = container_of(dev, struct spi_master, dev);
	kfree(master);
}

static struct class spi_master_class = {
	.name		= "spi_master",
	.owner		= THIS_MODULE,
	.dev_release	= spi_master_release,
};


/**
 * spi_alloc_master - allocate SPI master controller
 * @dev: the controller, possibly using the platform_bus
 * @size: how much zeroed driver-private data to allocate; the pointer to this
 *	memory is in the driver_data field of the returned device,
 *	accessible with spi_master_get_devdata().
 * Context: can sleep
 *
 * This call is used only by SPI master controller drivers, which are the
 * only ones directly touching chip registers.  It's how they allocate
 * an spi_master structure, prior to calling spi_register_master().
 *
 * This must be called from context that can sleep.  It returns the SPI
 * master structure on success, else NULL.
 *
 * The caller is responsible for assigning the bus number and initializing
 * the master's methods before calling spi_register_master(); and (after errors
 * adding the device) calling spi_master_put() to prevent a memory leak.
 */
struct spi_master *spi_alloc_master(struct device *dev, unsigned size)
{
	struct spi_master	*master;

	if (!dev)
		return NULL;

	master = kzalloc(size + sizeof *master, GFP_KERNEL);
	if (!master)
		return NULL;

	device_initialize(&master->dev);
	master->dev.class = &spi_master_class;
	master->dev.parent = get_device(dev);
	spi_master_set_devdata(master, &master[1]);

	return master;
}
EXPORT_SYMBOL_GPL(spi_alloc_master);

/**
 * spi_register_master - register SPI master controller
 * @master: initialized master, originally from spi_alloc_master()
 * Context: can sleep
 *
 * SPI master controllers connect to their drivers using some non-SPI bus,
 * such as the platform bus.  The final stage of probe() in that code
 * includes calling spi_register_master() to hook up to this SPI bus glue.
 *
 * SPI controllers use board specific (often SOC specific) bus numbers,
 * and board-specific addressing for SPI devices combines those numbers
 * with chip select numbers.  Since SPI does not directly support dynamic
 * device identification, boards need configuration tables telling which
 * chip is at which address.
 *
 * This must be called from context that can sleep.  It returns zero on
 * success, else a negative error code (dropping the master's refcount).
 * After a successful return, the caller is responsible for calling
 * spi_unregister_master().
 */
int spi_register_master(struct spi_master *master)
{
	static atomic_t		dyn_bus_id = ATOMIC_INIT((1<<15) - 1);
	struct device		*dev = master->dev.parent;
	int			status = -ENODEV;
	int			dynamic = 0;

	if (!dev)
		return -ENODEV;

	/* even if it's just one always-selected device, there must
	 * be at least one chipselect
	 */
	if (master->num_chipselect == 0)
		return -EINVAL;

	/* convention:  dynamically assigned bus IDs count down from the max */
	if (master->bus_num < 0) {
		/* FIXME switch to an IDR based scheme, something like
		 * I2C now uses, so we can't run out of "dynamic" IDs
		 */
		master->bus_num = atomic_dec_return(&dyn_bus_id);
		dynamic = 1;
	}

	/* register the device, then userspace will see it.
	 * registration fails if the bus ID is in use.
	 */
	dev_set_name(&master->dev, "spi%u", master->bus_num);
	status = device_add(&master->dev);
	if (status < 0)
		goto done;
	dev_dbg(dev, "registered master %s%s\n", dev_name(&master->dev),
			dynamic ? " (dynamic)" : "");

	/* populate children from any spi device tables */
	scan_boardinfo(master);
	status = 0;
done:
	return status;
}
EXPORT_SYMBOL_GPL(spi_register_master);


static int __unregister(struct device *dev, void *master_dev)
{
	/* note: before about 2.6.14-rc1 this would corrupt memory: */
	if (dev != master_dev)
		spi_unregister_device(to_spi_device(dev));
	return 0;
}

/**
 * spi_unregister_master - unregister SPI master controller
 * @master: the master being unregistered
 * Context: can sleep
 *
 * This call is used only by SPI master controller drivers, which are the
 * only ones directly touching chip registers.
 *
 * This must be called from context that can sleep.
 */
void spi_unregister_master(struct spi_master *master)
{
	int dummy;

	dummy = device_for_each_child(master->dev.parent, &master->dev,
					__unregister);
	device_unregister(&master->dev);
}
EXPORT_SYMBOL_GPL(spi_unregister_master);

static int __spi_master_match(struct device *dev, void *data)
{
	struct spi_master *m;
	u16 *bus_num = data;

	m = container_of(dev, struct spi_master, dev);
	return m->bus_num == *bus_num;
}

/**
 * spi_busnum_to_master - look up master associated with bus_num
 * @bus_num: the master's bus number
 * Context: can sleep
 *
 * This call may be used with devices that are registered after
 * arch init time.  It returns a refcounted pointer to the relevant
 * spi_master (which the caller must release), or NULL if there is
 * no such master registered.
 */
struct spi_master *spi_busnum_to_master(u16 bus_num)
{
	struct device		*dev;
	struct spi_master	*master = NULL;

	dev = class_find_device(&spi_master_class, NULL, &bus_num,
				__spi_master_match);
	if (dev)
		master = container_of(dev, struct spi_master, dev);
	/* reference got in class_find_device */
	return master;
}
EXPORT_SYMBOL_GPL(spi_busnum_to_master);


/*-------------------------------------------------------------------------*/

/* Core methods for SPI master protocol drivers.  Some of the
 * other core methods are currently defined as inline functions.
 */

/**
 * spi_setup - setup SPI mode and clock rate
 * @spi: the device whose settings are being modified
 * Context: can sleep, and no requests are queued to the device
 *
 * SPI protocol drivers may need to update the transfer mode if the
 * device doesn't work with its default.  They may likewise need
 * to update clock rates or word sizes from initial values.  This function
 * changes those settings, and must be called from a context that can sleep.
 * Except for SPI_CS_HIGH, which takes effect immediately, the changes take
 * effect the next time the device is selected and data is transferred to
 * or from it.  When this function returns, the spi device is deselected.
 *
 * Note that this call will fail if the protocol driver specifies an option
 * that the underlying controller or its driver does not support.  For
 * example, not all hardware supports wire transfers using nine bit words,
 * LSB-first wire encoding, or active-high chipselects.
 */
int spi_setup(struct spi_device *spi)
{
	unsigned	bad_bits;
	int		status;

	/* help drivers fail *cleanly* when they need options
	 * that aren't supported with their current master
	 */
	bad_bits = spi->mode & ~spi->master->mode_bits;
	if (bad_bits) {
		dev_dbg(&spi->dev, "setup: unsupported mode bits %x\n",
			bad_bits);
		return -EINVAL;
	}

	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	status = spi->master->setup(spi);

	dev_dbg(&spi->dev, "setup mode %d, %s%s%s%s"
				"%u bits/w, %u Hz max --> %d\n",
			(int) (spi->mode & (SPI_CPOL | SPI_CPHA)),
			(spi->mode & SPI_CS_HIGH) ? "cs_high, " : "",
			(spi->mode & SPI_LSB_FIRST) ? "lsb, " : "",
			(spi->mode & SPI_3WIRE) ? "3wire, " : "",
			(spi->mode & SPI_LOOP) ? "loopback, " : "",
			spi->bits_per_word, spi->max_speed_hz,
			status);

	return status;
}
EXPORT_SYMBOL_GPL(spi_setup);


/*-------------------------------------------------------------------------*/

/* Utility methods for SPI master protocol drivers, layered on
 * top of the core.  Some other utility methods are defined as
 * inline functions.
 */

static void spi_complete(void *arg)
{
	complete(arg);
}

/**
 * spi_sync - blocking/synchronous SPI data transfers
 * @spi: device with which data will be exchanged
 * @message: describes the data transfers
 * Context: can sleep
 *
 * This call may only be used from a context that may sleep.  The sleep
 * is non-interruptible, and has no timeout.  Low-overhead controller
 * drivers may DMA directly into and out of the message buffers.
 *
 * Note that the SPI device's chip select is active during the message,
 * and then is normally disabled between messages.  Drivers for some
 * frequently-used devices may want to minimize costs of selecting a chip,
 * by leaving it selected in anticipation that the next message will go
 * to the same chip.  (That may increase power usage.)
 *
 * Also, the caller is guaranteeing that the memory associated with the
 * message will not be freed before this call returns.
 *
 * It returns zero on success, else a negative error code.
 */
int spi_sync(struct spi_device *spi, struct spi_message *message)
{
	DECLARE_COMPLETION_ONSTACK(done);
	int status;

	message->complete = spi_complete;
	message->context = &done;
	status = spi_async(spi, message);
	if (status == 0) {
		wait_for_completion(&done);
		status = message->status;
	}
	message->context = NULL;
	return status;
}
EXPORT_SYMBOL_GPL(spi_sync);

/* portable code must never pass more than 32 bytes */
#define	SPI_BUFSIZ	max(32,SMP_CACHE_BYTES)

static u8	*buf;

/**
 * spi_write_then_read - SPI synchronous write followed by read
 * @spi: device with which data will be exchanged
 * @txbuf: data to be written (need not be dma-safe)
 * @n_tx: size of txbuf, in bytes
 * @rxbuf: buffer into which data will be read (need not be dma-safe)
 * @n_rx: size of rxbuf, in bytes
 * Context: can sleep
 *
 * This performs a half duplex MicroWire style transaction with the
 * device, sending txbuf and then reading rxbuf.  The return value
 * is zero for success, else a negative errno status code.
 * This call may only be used from a context that may sleep.
 *
 * Parameters to this routine are always copied using a small buffer;
 * portable code should never use this for more than 32 bytes.
 * Performance-sensitive or bulk transfer code should instead use
 * spi_{async,sync}() calls with dma-safe buffers.
 */
int spi_write_then_read(struct spi_device *spi,
		const u8 *txbuf, unsigned n_tx,
		u8 *rxbuf, unsigned n_rx)
{
	static DEFINE_MUTEX(lock);

	int			status;
	struct spi_message	message;
	struct spi_transfer	x[2];
	u8			*local_buf;

	/* Use preallocated DMA-safe buffer.  We can't avoid copying here,
	 * (as a pure convenience thing), but we can keep heap costs
	 * out of the hot path ...
	 */
	if ((n_tx + n_rx) > SPI_BUFSIZ)
		return -EINVAL;

	spi_message_init(&message);
	memset(x, 0, sizeof x);
	if (n_tx) {
		x[0].len = n_tx;
		spi_message_add_tail(&x[0], &message);
	}
	if (n_rx) {
		x[1].len = n_rx;
		spi_message_add_tail(&x[1], &message);
	}

	/* ... unless someone else is using the pre-allocated buffer */
	if (!mutex_trylock(&lock)) {
		local_buf = kmalloc(SPI_BUFSIZ, GFP_KERNEL);
		if (!local_buf)
			return -ENOMEM;
	} else
		local_buf = buf;

	memcpy(local_buf, txbuf, n_tx);
	x[0].tx_buf = local_buf;
	x[1].rx_buf = local_buf + n_tx;

	/* do the i/o */
	status = spi_sync(spi, &message);
	if (status == 0)
		memcpy(rxbuf, x[1].rx_buf, n_rx);

	if (x[0].tx_buf == buf)
		mutex_unlock(&lock);
	else
		kfree(local_buf);

	return status;
}
EXPORT_SYMBOL_GPL(spi_write_then_read);

/*-------------------------------------------------------------------------*/

static int __init spi_init(void)
{
	int	status;

	buf = kmalloc(SPI_BUFSIZ, GFP_KERNEL);
	if (!buf) {
		status = -ENOMEM;
		goto err0;
	}

	status = bus_register(&spi_bus_type);
	if (status < 0)
		goto err1;

	status = class_register(&spi_master_class);
	if (status < 0)
		goto err2;
	return 0;

err2:
	bus_unregister(&spi_bus_type);
err1:
	kfree(buf);
	buf = NULL;
err0:
	return status;
}

/* board_info is normally registered in arch_initcall(),
 * but even essential drivers wait till later
 *
 * REVISIT only boardinfo really needs static linking. the rest (device and
 * driver registration) _could_ be dynamically linked (modular) ... costs
 * include needing to have boardinfo data structures be much more public.
 */
postcore_initcall(spi_init);

