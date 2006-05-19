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

#include <linux/autoconf.h>
#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/cache.h>
#include <linux/spi/spi.h>


/* SPI bustype and spi_master class are registered after board init code
 * provides the SPI device tables, ensuring that both are present by the
 * time controller driver registration causes spi_devices to "enumerate".
 */
static void spidev_release(struct device *dev)
{
	const struct spi_device	*spi = to_spi_device(dev);

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

	return snprintf(buf, BUS_ID_SIZE + 1, "%s\n", spi->modalias);
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

	return strncmp(spi->modalias, drv->name, BUS_ID_SIZE) == 0;
}

static int spi_uevent(struct device *dev, char **envp, int num_envp,
		char *buffer, int buffer_size)
{
	const struct spi_device		*spi = to_spi_device(dev);

	envp[0] = buffer;
	snprintf(buffer, buffer_size, "MODALIAS=%s", spi->modalias);
	envp[1] = NULL;
	return 0;
}

#ifdef	CONFIG_PM

/*
 * NOTE:  the suspend() method for an spi_master controller driver
 * should verify that all its child devices are marked as suspended;
 * suspend requests delivered through sysfs power/state files don't
 * enforce such constraints.
 */
static int spi_suspend(struct device *dev, pm_message_t message)
{
	int			value;
	struct spi_driver	*drv = to_spi_driver(dev->driver);

	if (!drv || !drv->suspend)
		return 0;

	/* suspend will stop irqs and dma; no more i/o */
	value = drv->suspend(to_spi_device(dev), message);
	if (value == 0)
		dev->power.power_state = message;
	return value;
}

static int spi_resume(struct device *dev)
{
	int			value;
	struct spi_driver	*drv = to_spi_driver(dev->driver);

	if (!drv || !drv->resume)
		return 0;

	/* resume may restart the i/o queue */
	value = drv->resume(to_spi_device(dev));
	if (value == 0)
		dev->power.power_state = PMSG_ON;
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
static DECLARE_MUTEX(board_lock);


/* On typical mainboards, this is purely internal; and it's not needed
 * after board init creates the hard-wired devices.  Some development
 * platforms may not be able to use spi_register_board_info though, and
 * this is exported so that for example a USB or parport based adapter
 * driver could add devices (which it would learn about out-of-band).
 */
struct spi_device *__init_or_module
spi_new_device(struct spi_master *master, struct spi_board_info *chip)
{
	struct spi_device	*proxy;
	struct device		*dev = master->cdev.dev;
	int			status;

	/* NOTE:  caller did any chip->bus_num checks necessary */

	if (!spi_master_get(master))
		return NULL;

	proxy = kzalloc(sizeof *proxy, GFP_KERNEL);
	if (!proxy) {
		dev_err(dev, "can't alloc dev for cs%d\n",
			chip->chip_select);
		goto fail;
	}
	proxy->master = master;
	proxy->chip_select = chip->chip_select;
	proxy->max_speed_hz = chip->max_speed_hz;
	proxy->irq = chip->irq;
	proxy->modalias = chip->modalias;

	snprintf(proxy->dev.bus_id, sizeof proxy->dev.bus_id,
			"%s.%u", master->cdev.class_id,
			chip->chip_select);
	proxy->dev.parent = dev;
	proxy->dev.bus = &spi_bus_type;
	proxy->dev.platform_data = (void *) chip->platform_data;
	proxy->controller_data = chip->controller_data;
	proxy->controller_state = NULL;
	proxy->dev.release = spidev_release;

	/* drivers may modify this default i/o setup */
	status = master->setup(proxy);
	if (status < 0) {
		dev_dbg(dev, "can't %s %s, status %d\n",
				"setup", proxy->dev.bus_id, status);
		goto fail;
	}

	/* driver core catches callers that misbehave by defining
	 * devices that already exist.
	 */
	status = device_register(&proxy->dev);
	if (status < 0) {
		dev_dbg(dev, "can't %s %s, status %d\n",
				"add", proxy->dev.bus_id, status);
		goto fail;
	}
	dev_dbg(dev, "registered child %s\n", proxy->dev.bus_id);
	return proxy;

fail:
	spi_master_put(master);
	kfree(proxy);
	return NULL;
}
EXPORT_SYMBOL_GPL(spi_new_device);

/*
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

	down(&board_lock);
	list_add_tail(&bi->list, &board_list);
	up(&board_lock);
	return 0;
}
EXPORT_SYMBOL_GPL(spi_register_board_info);

/* FIXME someone should add support for a __setup("spi", ...) that
 * creates board info from kernel command lines
 */

static void __init_or_module
scan_boardinfo(struct spi_master *master)
{
	struct boardinfo	*bi;
	struct device		*dev = master->cdev.dev;

	down(&board_lock);
	list_for_each_entry(bi, &board_list, list) {
		struct spi_board_info	*chip = bi->board_info;
		unsigned		n;

		for (n = bi->n_board_info; n > 0; n--, chip++) {
			if (chip->bus_num != master->bus_num)
				continue;
			/* some controllers only have one chip, so they
			 * might not use chipselects.  otherwise, the
			 * chipselects are numbered 0..max.
			 */
			if (chip->chip_select >= master->num_chipselect
					&& master->num_chipselect) {
				dev_dbg(dev, "cs%d > max %d\n",
					chip->chip_select,
					master->num_chipselect);
				continue;
			}
			(void) spi_new_device(master, chip);
		}
	}
	up(&board_lock);
}

/*-------------------------------------------------------------------------*/

static void spi_master_release(struct class_device *cdev)
{
	struct spi_master *master;

	master = container_of(cdev, struct spi_master, cdev);
	kfree(master);
}

static struct class spi_master_class = {
	.name		= "spi_master",
	.owner		= THIS_MODULE,
	.release	= spi_master_release,
};


/**
 * spi_alloc_master - allocate SPI master controller
 * @dev: the controller, possibly using the platform_bus
 * @size: how much driver-private data to preallocate; the pointer to this
 * 	memory is in the class_data field of the returned class_device,
 *	accessible with spi_master_get_devdata().
 *
 * This call is used only by SPI master controller drivers, which are the
 * only ones directly touching chip registers.  It's how they allocate
 * an spi_master structure, prior to calling spi_add_master().
 *
 * This must be called from context that can sleep.  It returns the SPI
 * master structure on success, else NULL.
 *
 * The caller is responsible for assigning the bus number and initializing
 * the master's methods before calling spi_add_master(); and (after errors
 * adding the device) calling spi_master_put() to prevent a memory leak.
 */
struct spi_master * __init_or_module
spi_alloc_master(struct device *dev, unsigned size)
{
	struct spi_master	*master;

	if (!dev)
		return NULL;

	master = kzalloc(size + sizeof *master, SLAB_KERNEL);
	if (!master)
		return NULL;

	class_device_initialize(&master->cdev);
	master->cdev.class = &spi_master_class;
	master->cdev.dev = get_device(dev);
	spi_master_set_devdata(master, &master[1]);

	return master;
}
EXPORT_SYMBOL_GPL(spi_alloc_master);

/**
 * spi_register_master - register SPI master controller
 * @master: initialized master, originally from spi_alloc_master()
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
int __init_or_module
spi_register_master(struct spi_master *master)
{
	static atomic_t		dyn_bus_id = ATOMIC_INIT((1<<16) - 1);
	struct device		*dev = master->cdev.dev;
	int			status = -ENODEV;
	int			dynamic = 0;

	if (!dev)
		return -ENODEV;

	/* convention:  dynamically assigned bus IDs count down from the max */
	if (master->bus_num < 0) {
		master->bus_num = atomic_dec_return(&dyn_bus_id);
		dynamic = 1;
	}

	/* register the device, then userspace will see it.
	 * registration fails if the bus ID is in use.
	 */
	snprintf(master->cdev.class_id, sizeof master->cdev.class_id,
		"spi%u", master->bus_num);
	status = class_device_add(&master->cdev);
	if (status < 0)
		goto done;
	dev_dbg(dev, "registered master %s%s\n", master->cdev.class_id,
			dynamic ? " (dynamic)" : "");

	/* populate children from any spi device tables */
	scan_boardinfo(master);
	status = 0;
done:
	return status;
}
EXPORT_SYMBOL_GPL(spi_register_master);


static int __unregister(struct device *dev, void *unused)
{
	/* note: before about 2.6.14-rc1 this would corrupt memory: */
	spi_unregister_device(to_spi_device(dev));
	return 0;
}

/**
 * spi_unregister_master - unregister SPI master controller
 * @master: the master being unregistered
 *
 * This call is used only by SPI master controller drivers, which are the
 * only ones directly touching chip registers.
 *
 * This must be called from context that can sleep.
 */
void spi_unregister_master(struct spi_master *master)
{
	(void) device_for_each_child(master->cdev.dev, NULL, __unregister);
	class_device_unregister(&master->cdev);
}
EXPORT_SYMBOL_GPL(spi_unregister_master);

/**
 * spi_busnum_to_master - look up master associated with bus_num
 * @bus_num: the master's bus number
 *
 * This call may be used with devices that are registered after
 * arch init time.  It returns a refcounted pointer to the relevant
 * spi_master (which the caller must release), or NULL if there is
 * no such master registered.
 */
struct spi_master *spi_busnum_to_master(u16 bus_num)
{
	if (bus_num) {
		char			name[8];
		struct kobject		*bus;

		snprintf(name, sizeof name, "spi%u", bus_num);
		bus = kset_find_obj(&spi_master_class.subsys.kset, name);
		if (bus)
			return container_of(bus, struct spi_master, cdev.kobj);
	}
	return NULL;
}
EXPORT_SYMBOL_GPL(spi_busnum_to_master);


/*-------------------------------------------------------------------------*/

static void spi_complete(void *arg)
{
	complete(arg);
}

/**
 * spi_sync - blocking/synchronous SPI data transfers
 * @spi: device with which data will be exchanged
 * @message: describes the data transfers
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
 * The return value is a negative error code if the message could not be
 * submitted, else zero.  When the value is zero, then message->status is
 * also defined:  it's the completion code for the transfer, either zero
 * or a negative error code from the controller driver.
 */
int spi_sync(struct spi_device *spi, struct spi_message *message)
{
	DECLARE_COMPLETION(done);
	int status;

	message->complete = spi_complete;
	message->context = &done;
	status = spi_async(spi, message);
	if (status == 0)
		wait_for_completion(&done);
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
 * @rxbuf: buffer into which data will be read
 * @n_rx: size of rxbuf, in bytes (need not be dma-safe)
 *
 * This performs a half duplex MicroWire style transaction with the
 * device, sending txbuf and then reading rxbuf.  The return value
 * is zero for success, else a negative errno status code.
 * This call may only be used from a context that may sleep.
 *
 * Parameters to this routine are always copied using a small buffer;
 * performance-sensitive or bulk transfer code should instead use
 * spi_{async,sync}() calls with dma-safe buffers.
 */
int spi_write_then_read(struct spi_device *spi,
		const u8 *txbuf, unsigned n_tx,
		u8 *rxbuf, unsigned n_rx)
{
	static DECLARE_MUTEX(lock);

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
	if (down_trylock(&lock)) {
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
	if (status == 0) {
		memcpy(rxbuf, x[1].rx_buf, n_rx);
		status = message.status;
	}

	if (x[0].tx_buf == buf)
		up(&lock);
	else
		kfree(local_buf);

	return status;
}
EXPORT_SYMBOL_GPL(spi_write_then_read);

/*-------------------------------------------------------------------------*/

static int __init spi_init(void)
{
	int	status;

	buf = kmalloc(SPI_BUFSIZ, SLAB_KERNEL);
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
subsys_initcall(spi_init);

