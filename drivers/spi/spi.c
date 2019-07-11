// SPDX-License-Identifier: GPL-2.0-or-later
// SPI init/core code
//
// Copyright (C) 2005 David Brownell
// Copyright (C) 2008 Secret Lab Technologies Ltd.

#include <linux/kernel.h>
#include <linux/device.h>
#include <linux/init.h>
#include <linux/cache.h>
#include <linux/dma-mapping.h>
#include <linux/dmaengine.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/clk/clk-conf.h>
#include <linux/slab.h>
#include <linux/mod_devicetable.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <linux/of_gpio.h>
#include <linux/gpio/consumer.h>
#include <linux/pm_runtime.h>
#include <linux/pm_domain.h>
#include <linux/property.h>
#include <linux/export.h>
#include <linux/sched/rt.h>
#include <uapi/linux/sched/types.h>
#include <linux/delay.h>
#include <linux/kthread.h>
#include <linux/ioport.h>
#include <linux/acpi.h>
#include <linux/highmem.h>
#include <linux/idr.h>
#include <linux/platform_data/x86/apple.h>

#define CREATE_TRACE_POINTS
#include <trace/events/spi.h>

#include "internals.h"

static DEFINE_IDR(spi_master_idr);

static void spidev_release(struct device *dev)
{
	struct spi_device	*spi = to_spi_device(dev);

	/* spi controllers may cleanup for released devices */
	if (spi->controller->cleanup)
		spi->controller->cleanup(spi);

	spi_controller_put(spi->controller);
	kfree(spi->driver_override);
	kfree(spi);
}

static ssize_t
modalias_show(struct device *dev, struct device_attribute *a, char *buf)
{
	const struct spi_device	*spi = to_spi_device(dev);
	int len;

	len = acpi_device_modalias(dev, buf, PAGE_SIZE - 1);
	if (len != -ENODEV)
		return len;

	return sprintf(buf, "%s%s\n", SPI_MODULE_PREFIX, spi->modalias);
}
static DEVICE_ATTR_RO(modalias);

static ssize_t driver_override_store(struct device *dev,
				     struct device_attribute *a,
				     const char *buf, size_t count)
{
	struct spi_device *spi = to_spi_device(dev);
	const char *end = memchr(buf, '\n', count);
	const size_t len = end ? end - buf : count;
	const char *driver_override, *old;

	/* We need to keep extra room for a newline when displaying value */
	if (len >= (PAGE_SIZE - 1))
		return -EINVAL;

	driver_override = kstrndup(buf, len, GFP_KERNEL);
	if (!driver_override)
		return -ENOMEM;

	device_lock(dev);
	old = spi->driver_override;
	if (len) {
		spi->driver_override = driver_override;
	} else {
		/* Emptry string, disable driver override */
		spi->driver_override = NULL;
		kfree(driver_override);
	}
	device_unlock(dev);
	kfree(old);

	return count;
}

static ssize_t driver_override_show(struct device *dev,
				    struct device_attribute *a, char *buf)
{
	const struct spi_device *spi = to_spi_device(dev);
	ssize_t len;

	device_lock(dev);
	len = snprintf(buf, PAGE_SIZE, "%s\n", spi->driver_override ? : "");
	device_unlock(dev);
	return len;
}
static DEVICE_ATTR_RW(driver_override);

#define SPI_STATISTICS_ATTRS(field, file)				\
static ssize_t spi_controller_##field##_show(struct device *dev,	\
					     struct device_attribute *attr, \
					     char *buf)			\
{									\
	struct spi_controller *ctlr = container_of(dev,			\
					 struct spi_controller, dev);	\
	return spi_statistics_##field##_show(&ctlr->statistics, buf);	\
}									\
static struct device_attribute dev_attr_spi_controller_##field = {	\
	.attr = { .name = file, .mode = 0444 },				\
	.show = spi_controller_##field##_show,				\
};									\
static ssize_t spi_device_##field##_show(struct device *dev,		\
					 struct device_attribute *attr,	\
					char *buf)			\
{									\
	struct spi_device *spi = to_spi_device(dev);			\
	return spi_statistics_##field##_show(&spi->statistics, buf);	\
}									\
static struct device_attribute dev_attr_spi_device_##field = {		\
	.attr = { .name = file, .mode = 0444 },				\
	.show = spi_device_##field##_show,				\
}

#define SPI_STATISTICS_SHOW_NAME(name, file, field, format_string)	\
static ssize_t spi_statistics_##name##_show(struct spi_statistics *stat, \
					    char *buf)			\
{									\
	unsigned long flags;						\
	ssize_t len;							\
	spin_lock_irqsave(&stat->lock, flags);				\
	len = sprintf(buf, format_string, stat->field);			\
	spin_unlock_irqrestore(&stat->lock, flags);			\
	return len;							\
}									\
SPI_STATISTICS_ATTRS(name, file)

#define SPI_STATISTICS_SHOW(field, format_string)			\
	SPI_STATISTICS_SHOW_NAME(field, __stringify(field),		\
				 field, format_string)

SPI_STATISTICS_SHOW(messages, "%lu");
SPI_STATISTICS_SHOW(transfers, "%lu");
SPI_STATISTICS_SHOW(errors, "%lu");
SPI_STATISTICS_SHOW(timedout, "%lu");

SPI_STATISTICS_SHOW(spi_sync, "%lu");
SPI_STATISTICS_SHOW(spi_sync_immediate, "%lu");
SPI_STATISTICS_SHOW(spi_async, "%lu");

SPI_STATISTICS_SHOW(bytes, "%llu");
SPI_STATISTICS_SHOW(bytes_rx, "%llu");
SPI_STATISTICS_SHOW(bytes_tx, "%llu");

#define SPI_STATISTICS_TRANSFER_BYTES_HISTO(index, number)		\
	SPI_STATISTICS_SHOW_NAME(transfer_bytes_histo##index,		\
				 "transfer_bytes_histo_" number,	\
				 transfer_bytes_histo[index],  "%lu")
SPI_STATISTICS_TRANSFER_BYTES_HISTO(0,  "0-1");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(1,  "2-3");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(2,  "4-7");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(3,  "8-15");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(4,  "16-31");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(5,  "32-63");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(6,  "64-127");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(7,  "128-255");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(8,  "256-511");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(9,  "512-1023");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(10, "1024-2047");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(11, "2048-4095");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(12, "4096-8191");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(13, "8192-16383");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(14, "16384-32767");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(15, "32768-65535");
SPI_STATISTICS_TRANSFER_BYTES_HISTO(16, "65536+");

SPI_STATISTICS_SHOW(transfers_split_maxsize, "%lu");

static struct attribute *spi_dev_attrs[] = {
	&dev_attr_modalias.attr,
	&dev_attr_driver_override.attr,
	NULL,
};

static const struct attribute_group spi_dev_group = {
	.attrs  = spi_dev_attrs,
};

static struct attribute *spi_device_statistics_attrs[] = {
	&dev_attr_spi_device_messages.attr,
	&dev_attr_spi_device_transfers.attr,
	&dev_attr_spi_device_errors.attr,
	&dev_attr_spi_device_timedout.attr,
	&dev_attr_spi_device_spi_sync.attr,
	&dev_attr_spi_device_spi_sync_immediate.attr,
	&dev_attr_spi_device_spi_async.attr,
	&dev_attr_spi_device_bytes.attr,
	&dev_attr_spi_device_bytes_rx.attr,
	&dev_attr_spi_device_bytes_tx.attr,
	&dev_attr_spi_device_transfer_bytes_histo0.attr,
	&dev_attr_spi_device_transfer_bytes_histo1.attr,
	&dev_attr_spi_device_transfer_bytes_histo2.attr,
	&dev_attr_spi_device_transfer_bytes_histo3.attr,
	&dev_attr_spi_device_transfer_bytes_histo4.attr,
	&dev_attr_spi_device_transfer_bytes_histo5.attr,
	&dev_attr_spi_device_transfer_bytes_histo6.attr,
	&dev_attr_spi_device_transfer_bytes_histo7.attr,
	&dev_attr_spi_device_transfer_bytes_histo8.attr,
	&dev_attr_spi_device_transfer_bytes_histo9.attr,
	&dev_attr_spi_device_transfer_bytes_histo10.attr,
	&dev_attr_spi_device_transfer_bytes_histo11.attr,
	&dev_attr_spi_device_transfer_bytes_histo12.attr,
	&dev_attr_spi_device_transfer_bytes_histo13.attr,
	&dev_attr_spi_device_transfer_bytes_histo14.attr,
	&dev_attr_spi_device_transfer_bytes_histo15.attr,
	&dev_attr_spi_device_transfer_bytes_histo16.attr,
	&dev_attr_spi_device_transfers_split_maxsize.attr,
	NULL,
};

static const struct attribute_group spi_device_statistics_group = {
	.name  = "statistics",
	.attrs  = spi_device_statistics_attrs,
};

static const struct attribute_group *spi_dev_groups[] = {
	&spi_dev_group,
	&spi_device_statistics_group,
	NULL,
};

static struct attribute *spi_controller_statistics_attrs[] = {
	&dev_attr_spi_controller_messages.attr,
	&dev_attr_spi_controller_transfers.attr,
	&dev_attr_spi_controller_errors.attr,
	&dev_attr_spi_controller_timedout.attr,
	&dev_attr_spi_controller_spi_sync.attr,
	&dev_attr_spi_controller_spi_sync_immediate.attr,
	&dev_attr_spi_controller_spi_async.attr,
	&dev_attr_spi_controller_bytes.attr,
	&dev_attr_spi_controller_bytes_rx.attr,
	&dev_attr_spi_controller_bytes_tx.attr,
	&dev_attr_spi_controller_transfer_bytes_histo0.attr,
	&dev_attr_spi_controller_transfer_bytes_histo1.attr,
	&dev_attr_spi_controller_transfer_bytes_histo2.attr,
	&dev_attr_spi_controller_transfer_bytes_histo3.attr,
	&dev_attr_spi_controller_transfer_bytes_histo4.attr,
	&dev_attr_spi_controller_transfer_bytes_histo5.attr,
	&dev_attr_spi_controller_transfer_bytes_histo6.attr,
	&dev_attr_spi_controller_transfer_bytes_histo7.attr,
	&dev_attr_spi_controller_transfer_bytes_histo8.attr,
	&dev_attr_spi_controller_transfer_bytes_histo9.attr,
	&dev_attr_spi_controller_transfer_bytes_histo10.attr,
	&dev_attr_spi_controller_transfer_bytes_histo11.attr,
	&dev_attr_spi_controller_transfer_bytes_histo12.attr,
	&dev_attr_spi_controller_transfer_bytes_histo13.attr,
	&dev_attr_spi_controller_transfer_bytes_histo14.attr,
	&dev_attr_spi_controller_transfer_bytes_histo15.attr,
	&dev_attr_spi_controller_transfer_bytes_histo16.attr,
	&dev_attr_spi_controller_transfers_split_maxsize.attr,
	NULL,
};

static const struct attribute_group spi_controller_statistics_group = {
	.name  = "statistics",
	.attrs  = spi_controller_statistics_attrs,
};

static const struct attribute_group *spi_master_groups[] = {
	&spi_controller_statistics_group,
	NULL,
};

void spi_statistics_add_transfer_stats(struct spi_statistics *stats,
				       struct spi_transfer *xfer,
				       struct spi_controller *ctlr)
{
	unsigned long flags;
	int l2len = min(fls(xfer->len), SPI_STATISTICS_HISTO_SIZE) - 1;

	if (l2len < 0)
		l2len = 0;

	spin_lock_irqsave(&stats->lock, flags);

	stats->transfers++;
	stats->transfer_bytes_histo[l2len]++;

	stats->bytes += xfer->len;
	if ((xfer->tx_buf) &&
	    (xfer->tx_buf != ctlr->dummy_tx))
		stats->bytes_tx += xfer->len;
	if ((xfer->rx_buf) &&
	    (xfer->rx_buf != ctlr->dummy_rx))
		stats->bytes_rx += xfer->len;

	spin_unlock_irqrestore(&stats->lock, flags);
}
EXPORT_SYMBOL_GPL(spi_statistics_add_transfer_stats);

/* modalias support makes "modprobe $MODALIAS" new-style hotplug work,
 * and the sysfs version makes coldplug work too.
 */

static const struct spi_device_id *spi_match_id(const struct spi_device_id *id,
						const struct spi_device *sdev)
{
	while (id->name[0]) {
		if (!strcmp(sdev->modalias, id->name))
			return id;
		id++;
	}
	return NULL;
}

const struct spi_device_id *spi_get_device_id(const struct spi_device *sdev)
{
	const struct spi_driver *sdrv = to_spi_driver(sdev->dev.driver);

	return spi_match_id(sdrv->id_table, sdev);
}
EXPORT_SYMBOL_GPL(spi_get_device_id);

static int spi_match_device(struct device *dev, struct device_driver *drv)
{
	const struct spi_device	*spi = to_spi_device(dev);
	const struct spi_driver	*sdrv = to_spi_driver(drv);

	/* Check override first, and if set, only use the named driver */
	if (spi->driver_override)
		return strcmp(spi->driver_override, drv->name) == 0;

	/* Attempt an OF style match */
	if (of_driver_match_device(dev, drv))
		return 1;

	/* Then try ACPI */
	if (acpi_driver_match_device(dev, drv))
		return 1;

	if (sdrv->id_table)
		return !!spi_match_id(sdrv->id_table, spi);

	return strcmp(spi->modalias, drv->name) == 0;
}

static int spi_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	const struct spi_device		*spi = to_spi_device(dev);
	int rc;

	rc = acpi_device_uevent_modalias(dev, env);
	if (rc != -ENODEV)
		return rc;

	return add_uevent_var(env, "MODALIAS=%s%s", SPI_MODULE_PREFIX, spi->modalias);
}

struct bus_type spi_bus_type = {
	.name		= "spi",
	.dev_groups	= spi_dev_groups,
	.match		= spi_match_device,
	.uevent		= spi_uevent,
};
EXPORT_SYMBOL_GPL(spi_bus_type);


static int spi_drv_probe(struct device *dev)
{
	const struct spi_driver		*sdrv = to_spi_driver(dev->driver);
	struct spi_device		*spi = to_spi_device(dev);
	int ret;

	ret = of_clk_set_defaults(dev->of_node, false);
	if (ret)
		return ret;

	if (dev->of_node) {
		spi->irq = of_irq_get(dev->of_node, 0);
		if (spi->irq == -EPROBE_DEFER)
			return -EPROBE_DEFER;
		if (spi->irq < 0)
			spi->irq = 0;
	}

	ret = dev_pm_domain_attach(dev, true);
	if (ret)
		return ret;

	ret = sdrv->probe(spi);
	if (ret)
		dev_pm_domain_detach(dev, true);

	return ret;
}

static int spi_drv_remove(struct device *dev)
{
	const struct spi_driver		*sdrv = to_spi_driver(dev->driver);
	int ret;

	ret = sdrv->remove(to_spi_device(dev));
	dev_pm_domain_detach(dev, true);

	return ret;
}

static void spi_drv_shutdown(struct device *dev)
{
	const struct spi_driver		*sdrv = to_spi_driver(dev->driver);

	sdrv->shutdown(to_spi_device(dev));
}

/**
 * __spi_register_driver - register a SPI driver
 * @owner: owner module of the driver to register
 * @sdrv: the driver to register
 * Context: can sleep
 *
 * Return: zero on success, else a negative error code.
 */
int __spi_register_driver(struct module *owner, struct spi_driver *sdrv)
{
	sdrv->driver.owner = owner;
	sdrv->driver.bus = &spi_bus_type;
	if (sdrv->probe)
		sdrv->driver.probe = spi_drv_probe;
	if (sdrv->remove)
		sdrv->driver.remove = spi_drv_remove;
	if (sdrv->shutdown)
		sdrv->driver.shutdown = spi_drv_shutdown;
	return driver_register(&sdrv->driver);
}
EXPORT_SYMBOL_GPL(__spi_register_driver);

/*-------------------------------------------------------------------------*/

/* SPI devices should normally not be created by SPI device drivers; that
 * would make them board-specific.  Similarly with SPI controller drivers.
 * Device registration normally goes into like arch/.../mach.../board-YYY.c
 * with other readonly (flashable) information about mainboard devices.
 */

struct boardinfo {
	struct list_head	list;
	struct spi_board_info	board_info;
};

static LIST_HEAD(board_list);
static LIST_HEAD(spi_controller_list);

/*
 * Used to protect add/del opertion for board_info list and
 * spi_controller list, and their matching process
 * also used to protect object of type struct idr
 */
static DEFINE_MUTEX(board_lock);

/**
 * spi_alloc_device - Allocate a new SPI device
 * @ctlr: Controller to which device is connected
 * Context: can sleep
 *
 * Allows a driver to allocate and initialize a spi_device without
 * registering it immediately.  This allows a driver to directly
 * fill the spi_device with device parameters before calling
 * spi_add_device() on it.
 *
 * Caller is responsible to call spi_add_device() on the returned
 * spi_device structure to add it to the SPI controller.  If the caller
 * needs to discard the spi_device without adding it, then it should
 * call spi_dev_put() on it.
 *
 * Return: a pointer to the new device, or NULL.
 */
struct spi_device *spi_alloc_device(struct spi_controller *ctlr)
{
	struct spi_device	*spi;

	if (!spi_controller_get(ctlr))
		return NULL;

	spi = kzalloc(sizeof(*spi), GFP_KERNEL);
	if (!spi) {
		spi_controller_put(ctlr);
		return NULL;
	}

	spi->master = spi->controller = ctlr;
	spi->dev.parent = &ctlr->dev;
	spi->dev.bus = &spi_bus_type;
	spi->dev.release = spidev_release;
	spi->cs_gpio = -ENOENT;

	spin_lock_init(&spi->statistics.lock);

	device_initialize(&spi->dev);
	return spi;
}
EXPORT_SYMBOL_GPL(spi_alloc_device);

static void spi_dev_set_name(struct spi_device *spi)
{
	struct acpi_device *adev = ACPI_COMPANION(&spi->dev);

	if (adev) {
		dev_set_name(&spi->dev, "spi-%s", acpi_dev_name(adev));
		return;
	}

	dev_set_name(&spi->dev, "%s.%u", dev_name(&spi->controller->dev),
		     spi->chip_select);
}

static int spi_dev_check(struct device *dev, void *data)
{
	struct spi_device *spi = to_spi_device(dev);
	struct spi_device *new_spi = data;

	if (spi->controller == new_spi->controller &&
	    spi->chip_select == new_spi->chip_select)
		return -EBUSY;
	return 0;
}

/**
 * spi_add_device - Add spi_device allocated with spi_alloc_device
 * @spi: spi_device to register
 *
 * Companion function to spi_alloc_device.  Devices allocated with
 * spi_alloc_device can be added onto the spi bus with this function.
 *
 * Return: 0 on success; negative errno on failure
 */
int spi_add_device(struct spi_device *spi)
{
	static DEFINE_MUTEX(spi_add_lock);
	struct spi_controller *ctlr = spi->controller;
	struct device *dev = ctlr->dev.parent;
	int status;

	/* Chipselects are numbered 0..max; validate. */
	if (spi->chip_select >= ctlr->num_chipselect) {
		dev_err(dev, "cs%d >= max %d\n", spi->chip_select,
			ctlr->num_chipselect);
		return -EINVAL;
	}

	/* Set the bus ID string */
	spi_dev_set_name(spi);

	/* We need to make sure there's no other device with this
	 * chipselect **BEFORE** we call setup(), else we'll trash
	 * its configuration.  Lock against concurrent add() calls.
	 */
	mutex_lock(&spi_add_lock);

	status = bus_for_each_dev(&spi_bus_type, NULL, spi, spi_dev_check);
	if (status) {
		dev_err(dev, "chipselect %d already in use\n",
				spi->chip_select);
		goto done;
	}

	/* Descriptors take precedence */
	if (ctlr->cs_gpiods)
		spi->cs_gpiod = ctlr->cs_gpiods[spi->chip_select];
	else if (ctlr->cs_gpios)
		spi->cs_gpio = ctlr->cs_gpios[spi->chip_select];

	/* Drivers may modify this initial i/o setup, but will
	 * normally rely on the device being setup.  Devices
	 * using SPI_CS_HIGH can't coexist well otherwise...
	 */
	status = spi_setup(spi);
	if (status < 0) {
		dev_err(dev, "can't setup %s, status %d\n",
				dev_name(&spi->dev), status);
		goto done;
	}

	/* Device may be bound to an active driver when this returns */
	status = device_add(&spi->dev);
	if (status < 0)
		dev_err(dev, "can't add %s, status %d\n",
				dev_name(&spi->dev), status);
	else
		dev_dbg(dev, "registered child %s\n", dev_name(&spi->dev));

done:
	mutex_unlock(&spi_add_lock);
	return status;
}
EXPORT_SYMBOL_GPL(spi_add_device);

/**
 * spi_new_device - instantiate one new SPI device
 * @ctlr: Controller to which device is connected
 * @chip: Describes the SPI device
 * Context: can sleep
 *
 * On typical mainboards, this is purely internal; and it's not needed
 * after board init creates the hard-wired devices.  Some development
 * platforms may not be able to use spi_register_board_info though, and
 * this is exported so that for example a USB or parport based adapter
 * driver could add devices (which it would learn about out-of-band).
 *
 * Return: the new device, or NULL.
 */
struct spi_device *spi_new_device(struct spi_controller *ctlr,
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

	proxy = spi_alloc_device(ctlr);
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

	if (chip->properties) {
		status = device_add_properties(&proxy->dev, chip->properties);
		if (status) {
			dev_err(&ctlr->dev,
				"failed to add properties to '%s': %d\n",
				chip->modalias, status);
			goto err_dev_put;
		}
	}

	status = spi_add_device(proxy);
	if (status < 0)
		goto err_remove_props;

	return proxy;

err_remove_props:
	if (chip->properties)
		device_remove_properties(&proxy->dev);
err_dev_put:
	spi_dev_put(proxy);
	return NULL;
}
EXPORT_SYMBOL_GPL(spi_new_device);

/**
 * spi_unregister_device - unregister a single SPI device
 * @spi: spi_device to unregister
 *
 * Start making the passed SPI device vanish. Normally this would be handled
 * by spi_unregister_controller().
 */
void spi_unregister_device(struct spi_device *spi)
{
	if (!spi)
		return;

	if (spi->dev.of_node) {
		of_node_clear_flag(spi->dev.of_node, OF_POPULATED);
		of_node_put(spi->dev.of_node);
	}
	if (ACPI_COMPANION(&spi->dev))
		acpi_device_clear_enumerated(ACPI_COMPANION(&spi->dev));
	device_unregister(&spi->dev);
}
EXPORT_SYMBOL_GPL(spi_unregister_device);

static void spi_match_controller_to_boardinfo(struct spi_controller *ctlr,
					      struct spi_board_info *bi)
{
	struct spi_device *dev;

	if (ctlr->bus_num != bi->bus_num)
		return;

	dev = spi_new_device(ctlr, bi);
	if (!dev)
		dev_err(ctlr->dev.parent, "can't create new device for %s\n",
			bi->modalias);
}

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
 * Device properties are deep-copied though.
 *
 * Return: zero on success, else a negative error code.
 */
int spi_register_board_info(struct spi_board_info const *info, unsigned n)
{
	struct boardinfo *bi;
	int i;

	if (!n)
		return 0;

	bi = kcalloc(n, sizeof(*bi), GFP_KERNEL);
	if (!bi)
		return -ENOMEM;

	for (i = 0; i < n; i++, bi++, info++) {
		struct spi_controller *ctlr;

		memcpy(&bi->board_info, info, sizeof(*info));
		if (info->properties) {
			bi->board_info.properties =
					property_entries_dup(info->properties);
			if (IS_ERR(bi->board_info.properties))
				return PTR_ERR(bi->board_info.properties);
		}

		mutex_lock(&board_lock);
		list_add_tail(&bi->list, &board_list);
		list_for_each_entry(ctlr, &spi_controller_list, list)
			spi_match_controller_to_boardinfo(ctlr,
							  &bi->board_info);
		mutex_unlock(&board_lock);
	}

	return 0;
}

/*-------------------------------------------------------------------------*/

static void spi_set_cs(struct spi_device *spi, bool enable)
{
	if (spi->mode & SPI_CS_HIGH)
		enable = !enable;

	if (spi->cs_gpiod || gpio_is_valid(spi->cs_gpio)) {
		/*
		 * Honour the SPI_NO_CS flag and invert the enable line, as
		 * active low is default for SPI. Execution paths that handle
		 * polarity inversion in gpiolib (such as device tree) will
		 * enforce active high using the SPI_CS_HIGH resulting in a
		 * double inversion through the code above.
		 */
		if (!(spi->mode & SPI_NO_CS)) {
			if (spi->cs_gpiod)
				gpiod_set_value_cansleep(spi->cs_gpiod,
							 !enable);
			else
				gpio_set_value_cansleep(spi->cs_gpio, !enable);
		}
		/* Some SPI masters need both GPIO CS & slave_select */
		if ((spi->controller->flags & SPI_MASTER_GPIO_SS) &&
		    spi->controller->set_cs)
			spi->controller->set_cs(spi, !enable);
	} else if (spi->controller->set_cs) {
		spi->controller->set_cs(spi, !enable);
	}
}

#ifdef CONFIG_HAS_DMA
int spi_map_buf(struct spi_controller *ctlr, struct device *dev,
		struct sg_table *sgt, void *buf, size_t len,
		enum dma_data_direction dir)
{
	const bool vmalloced_buf = is_vmalloc_addr(buf);
	unsigned int max_seg_size = dma_get_max_seg_size(dev);
#ifdef CONFIG_HIGHMEM
	const bool kmap_buf = ((unsigned long)buf >= PKMAP_BASE &&
				(unsigned long)buf < (PKMAP_BASE +
					(LAST_PKMAP * PAGE_SIZE)));
#else
	const bool kmap_buf = false;
#endif
	int desc_len;
	int sgs;
	struct page *vm_page;
	struct scatterlist *sg;
	void *sg_buf;
	size_t min;
	int i, ret;

	if (vmalloced_buf || kmap_buf) {
		desc_len = min_t(int, max_seg_size, PAGE_SIZE);
		sgs = DIV_ROUND_UP(len + offset_in_page(buf), desc_len);
	} else if (virt_addr_valid(buf)) {
		desc_len = min_t(int, max_seg_size, ctlr->max_dma_len);
		sgs = DIV_ROUND_UP(len, desc_len);
	} else {
		return -EINVAL;
	}

	ret = sg_alloc_table(sgt, sgs, GFP_KERNEL);
	if (ret != 0)
		return ret;

	sg = &sgt->sgl[0];
	for (i = 0; i < sgs; i++) {

		if (vmalloced_buf || kmap_buf) {
			/*
			 * Next scatterlist entry size is the minimum between
			 * the desc_len and the remaining buffer length that
			 * fits in a page.
			 */
			min = min_t(size_t, desc_len,
				    min_t(size_t, len,
					  PAGE_SIZE - offset_in_page(buf)));
			if (vmalloced_buf)
				vm_page = vmalloc_to_page(buf);
			else
				vm_page = kmap_to_page(buf);
			if (!vm_page) {
				sg_free_table(sgt);
				return -ENOMEM;
			}
			sg_set_page(sg, vm_page,
				    min, offset_in_page(buf));
		} else {
			min = min_t(size_t, len, desc_len);
			sg_buf = buf;
			sg_set_buf(sg, sg_buf, min);
		}

		buf += min;
		len -= min;
		sg = sg_next(sg);
	}

	ret = dma_map_sg(dev, sgt->sgl, sgt->nents, dir);
	if (!ret)
		ret = -ENOMEM;
	if (ret < 0) {
		sg_free_table(sgt);
		return ret;
	}

	sgt->nents = ret;

	return 0;
}

void spi_unmap_buf(struct spi_controller *ctlr, struct device *dev,
		   struct sg_table *sgt, enum dma_data_direction dir)
{
	if (sgt->orig_nents) {
		dma_unmap_sg(dev, sgt->sgl, sgt->orig_nents, dir);
		sg_free_table(sgt);
	}
}

static int __spi_map_msg(struct spi_controller *ctlr, struct spi_message *msg)
{
	struct device *tx_dev, *rx_dev;
	struct spi_transfer *xfer;
	int ret;

	if (!ctlr->can_dma)
		return 0;

	if (ctlr->dma_tx)
		tx_dev = ctlr->dma_tx->device->dev;
	else
		tx_dev = ctlr->dev.parent;

	if (ctlr->dma_rx)
		rx_dev = ctlr->dma_rx->device->dev;
	else
		rx_dev = ctlr->dev.parent;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		if (!ctlr->can_dma(ctlr, msg->spi, xfer))
			continue;

		if (xfer->tx_buf != NULL) {
			ret = spi_map_buf(ctlr, tx_dev, &xfer->tx_sg,
					  (void *)xfer->tx_buf, xfer->len,
					  DMA_TO_DEVICE);
			if (ret != 0)
				return ret;
		}

		if (xfer->rx_buf != NULL) {
			ret = spi_map_buf(ctlr, rx_dev, &xfer->rx_sg,
					  xfer->rx_buf, xfer->len,
					  DMA_FROM_DEVICE);
			if (ret != 0) {
				spi_unmap_buf(ctlr, tx_dev, &xfer->tx_sg,
					      DMA_TO_DEVICE);
				return ret;
			}
		}
	}

	ctlr->cur_msg_mapped = true;

	return 0;
}

static int __spi_unmap_msg(struct spi_controller *ctlr, struct spi_message *msg)
{
	struct spi_transfer *xfer;
	struct device *tx_dev, *rx_dev;

	if (!ctlr->cur_msg_mapped || !ctlr->can_dma)
		return 0;

	if (ctlr->dma_tx)
		tx_dev = ctlr->dma_tx->device->dev;
	else
		tx_dev = ctlr->dev.parent;

	if (ctlr->dma_rx)
		rx_dev = ctlr->dma_rx->device->dev;
	else
		rx_dev = ctlr->dev.parent;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		if (!ctlr->can_dma(ctlr, msg->spi, xfer))
			continue;

		spi_unmap_buf(ctlr, rx_dev, &xfer->rx_sg, DMA_FROM_DEVICE);
		spi_unmap_buf(ctlr, tx_dev, &xfer->tx_sg, DMA_TO_DEVICE);
	}

	return 0;
}
#else /* !CONFIG_HAS_DMA */
static inline int __spi_map_msg(struct spi_controller *ctlr,
				struct spi_message *msg)
{
	return 0;
}

static inline int __spi_unmap_msg(struct spi_controller *ctlr,
				  struct spi_message *msg)
{
	return 0;
}
#endif /* !CONFIG_HAS_DMA */

static inline int spi_unmap_msg(struct spi_controller *ctlr,
				struct spi_message *msg)
{
	struct spi_transfer *xfer;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		/*
		 * Restore the original value of tx_buf or rx_buf if they are
		 * NULL.
		 */
		if (xfer->tx_buf == ctlr->dummy_tx)
			xfer->tx_buf = NULL;
		if (xfer->rx_buf == ctlr->dummy_rx)
			xfer->rx_buf = NULL;
	}

	return __spi_unmap_msg(ctlr, msg);
}

static int spi_map_msg(struct spi_controller *ctlr, struct spi_message *msg)
{
	struct spi_transfer *xfer;
	void *tmp;
	unsigned int max_tx, max_rx;

	if (ctlr->flags & (SPI_CONTROLLER_MUST_RX | SPI_CONTROLLER_MUST_TX)) {
		max_tx = 0;
		max_rx = 0;

		list_for_each_entry(xfer, &msg->transfers, transfer_list) {
			if ((ctlr->flags & SPI_CONTROLLER_MUST_TX) &&
			    !xfer->tx_buf)
				max_tx = max(xfer->len, max_tx);
			if ((ctlr->flags & SPI_CONTROLLER_MUST_RX) &&
			    !xfer->rx_buf)
				max_rx = max(xfer->len, max_rx);
		}

		if (max_tx) {
			tmp = krealloc(ctlr->dummy_tx, max_tx,
				       GFP_KERNEL | GFP_DMA);
			if (!tmp)
				return -ENOMEM;
			ctlr->dummy_tx = tmp;
			memset(tmp, 0, max_tx);
		}

		if (max_rx) {
			tmp = krealloc(ctlr->dummy_rx, max_rx,
				       GFP_KERNEL | GFP_DMA);
			if (!tmp)
				return -ENOMEM;
			ctlr->dummy_rx = tmp;
		}

		if (max_tx || max_rx) {
			list_for_each_entry(xfer, &msg->transfers,
					    transfer_list) {
				if (!xfer->tx_buf)
					xfer->tx_buf = ctlr->dummy_tx;
				if (!xfer->rx_buf)
					xfer->rx_buf = ctlr->dummy_rx;
			}
		}
	}

	return __spi_map_msg(ctlr, msg);
}

static int spi_transfer_wait(struct spi_controller *ctlr,
			     struct spi_message *msg,
			     struct spi_transfer *xfer)
{
	struct spi_statistics *statm = &ctlr->statistics;
	struct spi_statistics *stats = &msg->spi->statistics;
	unsigned long long ms = 1;

	if (spi_controller_is_slave(ctlr)) {
		if (wait_for_completion_interruptible(&ctlr->xfer_completion)) {
			dev_dbg(&msg->spi->dev, "SPI transfer interrupted\n");
			return -EINTR;
		}
	} else {
		ms = 8LL * 1000LL * xfer->len;
		do_div(ms, xfer->speed_hz);
		ms += ms + 200; /* some tolerance */

		if (ms > UINT_MAX)
			ms = UINT_MAX;

		ms = wait_for_completion_timeout(&ctlr->xfer_completion,
						 msecs_to_jiffies(ms));

		if (ms == 0) {
			SPI_STATISTICS_INCREMENT_FIELD(statm, timedout);
			SPI_STATISTICS_INCREMENT_FIELD(stats, timedout);
			dev_err(&msg->spi->dev,
				"SPI transfer timed out\n");
			return -ETIMEDOUT;
		}
	}

	return 0;
}

/*
 * spi_transfer_one_message - Default implementation of transfer_one_message()
 *
 * This is a standard implementation of transfer_one_message() for
 * drivers which implement a transfer_one() operation.  It provides
 * standard handling of delays and chip select management.
 */
static int spi_transfer_one_message(struct spi_controller *ctlr,
				    struct spi_message *msg)
{
	struct spi_transfer *xfer;
	bool keep_cs = false;
	int ret = 0;
	struct spi_statistics *statm = &ctlr->statistics;
	struct spi_statistics *stats = &msg->spi->statistics;

	spi_set_cs(msg->spi, true);

	SPI_STATISTICS_INCREMENT_FIELD(statm, messages);
	SPI_STATISTICS_INCREMENT_FIELD(stats, messages);

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		trace_spi_transfer_start(msg, xfer);

		spi_statistics_add_transfer_stats(statm, xfer, ctlr);
		spi_statistics_add_transfer_stats(stats, xfer, ctlr);

		if (xfer->tx_buf || xfer->rx_buf) {
			reinit_completion(&ctlr->xfer_completion);

			ret = ctlr->transfer_one(ctlr, msg->spi, xfer);
			if (ret < 0) {
				SPI_STATISTICS_INCREMENT_FIELD(statm,
							       errors);
				SPI_STATISTICS_INCREMENT_FIELD(stats,
							       errors);
				dev_err(&msg->spi->dev,
					"SPI transfer failed: %d\n", ret);
				goto out;
			}

			if (ret > 0) {
				ret = spi_transfer_wait(ctlr, msg, xfer);
				if (ret < 0)
					msg->status = ret;
			}
		} else {
			if (xfer->len)
				dev_err(&msg->spi->dev,
					"Bufferless transfer has length %u\n",
					xfer->len);
		}

		trace_spi_transfer_stop(msg, xfer);

		if (msg->status != -EINPROGRESS)
			goto out;

		if (xfer->delay_usecs) {
			u16 us = xfer->delay_usecs;

			if (us <= 10)
				udelay(us);
			else
				usleep_range(us, us + DIV_ROUND_UP(us, 10));
		}

		if (xfer->cs_change) {
			if (list_is_last(&xfer->transfer_list,
					 &msg->transfers)) {
				keep_cs = true;
			} else {
				spi_set_cs(msg->spi, false);
				udelay(10);
				spi_set_cs(msg->spi, true);
			}
		}

		msg->actual_length += xfer->len;
	}

out:
	if (ret != 0 || !keep_cs)
		spi_set_cs(msg->spi, false);

	if (msg->status == -EINPROGRESS)
		msg->status = ret;

	if (msg->status && ctlr->handle_err)
		ctlr->handle_err(ctlr, msg);

	spi_res_release(ctlr, msg);

	spi_finalize_current_message(ctlr);

	return ret;
}

/**
 * spi_finalize_current_transfer - report completion of a transfer
 * @ctlr: the controller reporting completion
 *
 * Called by SPI drivers using the core transfer_one_message()
 * implementation to notify it that the current interrupt driven
 * transfer has finished and the next one may be scheduled.
 */
void spi_finalize_current_transfer(struct spi_controller *ctlr)
{
	complete(&ctlr->xfer_completion);
}
EXPORT_SYMBOL_GPL(spi_finalize_current_transfer);

/**
 * __spi_pump_messages - function which processes spi message queue
 * @ctlr: controller to process queue for
 * @in_kthread: true if we are in the context of the message pump thread
 *
 * This function checks if there is any spi message in the queue that
 * needs processing and if so call out to the driver to initialize hardware
 * and transfer each message.
 *
 * Note that it is called both from the kthread itself and also from
 * inside spi_sync(); the queue extraction handling at the top of the
 * function should deal with this safely.
 */
static void __spi_pump_messages(struct spi_controller *ctlr, bool in_kthread)
{
	unsigned long flags;
	bool was_busy = false;
	int ret;

	/* Lock queue */
	spin_lock_irqsave(&ctlr->queue_lock, flags);

	/* Make sure we are not already running a message */
	if (ctlr->cur_msg) {
		spin_unlock_irqrestore(&ctlr->queue_lock, flags);
		return;
	}

	/* If another context is idling the device then defer */
	if (ctlr->idling) {
		kthread_queue_work(&ctlr->kworker, &ctlr->pump_messages);
		spin_unlock_irqrestore(&ctlr->queue_lock, flags);
		return;
	}

	/* Check if the queue is idle */
	if (list_empty(&ctlr->queue) || !ctlr->running) {
		if (!ctlr->busy) {
			spin_unlock_irqrestore(&ctlr->queue_lock, flags);
			return;
		}

		/* Only do teardown in the thread */
		if (!in_kthread) {
			kthread_queue_work(&ctlr->kworker,
					   &ctlr->pump_messages);
			spin_unlock_irqrestore(&ctlr->queue_lock, flags);
			return;
		}

		ctlr->busy = false;
		ctlr->idling = true;
		spin_unlock_irqrestore(&ctlr->queue_lock, flags);

		kfree(ctlr->dummy_rx);
		ctlr->dummy_rx = NULL;
		kfree(ctlr->dummy_tx);
		ctlr->dummy_tx = NULL;
		if (ctlr->unprepare_transfer_hardware &&
		    ctlr->unprepare_transfer_hardware(ctlr))
			dev_err(&ctlr->dev,
				"failed to unprepare transfer hardware\n");
		if (ctlr->auto_runtime_pm) {
			pm_runtime_mark_last_busy(ctlr->dev.parent);
			pm_runtime_put_autosuspend(ctlr->dev.parent);
		}
		trace_spi_controller_idle(ctlr);

		spin_lock_irqsave(&ctlr->queue_lock, flags);
		ctlr->idling = false;
		spin_unlock_irqrestore(&ctlr->queue_lock, flags);
		return;
	}

	/* Extract head of queue */
	ctlr->cur_msg =
		list_first_entry(&ctlr->queue, struct spi_message, queue);

	list_del_init(&ctlr->cur_msg->queue);
	if (ctlr->busy)
		was_busy = true;
	else
		ctlr->busy = true;
	spin_unlock_irqrestore(&ctlr->queue_lock, flags);

	mutex_lock(&ctlr->io_mutex);

	if (!was_busy && ctlr->auto_runtime_pm) {
		ret = pm_runtime_get_sync(ctlr->dev.parent);
		if (ret < 0) {
			pm_runtime_put_noidle(ctlr->dev.parent);
			dev_err(&ctlr->dev, "Failed to power device: %d\n",
				ret);
			mutex_unlock(&ctlr->io_mutex);
			return;
		}
	}

	if (!was_busy)
		trace_spi_controller_busy(ctlr);

	if (!was_busy && ctlr->prepare_transfer_hardware) {
		ret = ctlr->prepare_transfer_hardware(ctlr);
		if (ret) {
			dev_err(&ctlr->dev,
				"failed to prepare transfer hardware\n");

			if (ctlr->auto_runtime_pm)
				pm_runtime_put(ctlr->dev.parent);
			mutex_unlock(&ctlr->io_mutex);
			return;
		}
	}

	trace_spi_message_start(ctlr->cur_msg);

	if (ctlr->prepare_message) {
		ret = ctlr->prepare_message(ctlr, ctlr->cur_msg);
		if (ret) {
			dev_err(&ctlr->dev, "failed to prepare message: %d\n",
				ret);
			ctlr->cur_msg->status = ret;
			spi_finalize_current_message(ctlr);
			goto out;
		}
		ctlr->cur_msg_prepared = true;
	}

	ret = spi_map_msg(ctlr, ctlr->cur_msg);
	if (ret) {
		ctlr->cur_msg->status = ret;
		spi_finalize_current_message(ctlr);
		goto out;
	}

	ret = ctlr->transfer_one_message(ctlr, ctlr->cur_msg);
	if (ret) {
		dev_err(&ctlr->dev,
			"failed to transfer one message from queue\n");
		goto out;
	}

out:
	mutex_unlock(&ctlr->io_mutex);

	/* Prod the scheduler in case transfer_one() was busy waiting */
	if (!ret)
		cond_resched();
}

/**
 * spi_pump_messages - kthread work function which processes spi message queue
 * @work: pointer to kthread work struct contained in the controller struct
 */
static void spi_pump_messages(struct kthread_work *work)
{
	struct spi_controller *ctlr =
		container_of(work, struct spi_controller, pump_messages);

	__spi_pump_messages(ctlr, true);
}

static int spi_init_queue(struct spi_controller *ctlr)
{
	struct sched_param param = { .sched_priority = MAX_RT_PRIO - 1 };

	ctlr->running = false;
	ctlr->busy = false;

	kthread_init_worker(&ctlr->kworker);
	ctlr->kworker_task = kthread_run(kthread_worker_fn, &ctlr->kworker,
					 "%s", dev_name(&ctlr->dev));
	if (IS_ERR(ctlr->kworker_task)) {
		dev_err(&ctlr->dev, "failed to create message pump task\n");
		return PTR_ERR(ctlr->kworker_task);
	}
	kthread_init_work(&ctlr->pump_messages, spi_pump_messages);

	/*
	 * Controller config will indicate if this controller should run the
	 * message pump with high (realtime) priority to reduce the transfer
	 * latency on the bus by minimising the delay between a transfer
	 * request and the scheduling of the message pump thread. Without this
	 * setting the message pump thread will remain at default priority.
	 */
	if (ctlr->rt) {
		dev_info(&ctlr->dev,
			"will run message pump with realtime priority\n");
		sched_setscheduler(ctlr->kworker_task, SCHED_FIFO, &param);
	}

	return 0;
}

/**
 * spi_get_next_queued_message() - called by driver to check for queued
 * messages
 * @ctlr: the controller to check for queued messages
 *
 * If there are more messages in the queue, the next message is returned from
 * this call.
 *
 * Return: the next message in the queue, else NULL if the queue is empty.
 */
struct spi_message *spi_get_next_queued_message(struct spi_controller *ctlr)
{
	struct spi_message *next;
	unsigned long flags;

	/* get a pointer to the next message, if any */
	spin_lock_irqsave(&ctlr->queue_lock, flags);
	next = list_first_entry_or_null(&ctlr->queue, struct spi_message,
					queue);
	spin_unlock_irqrestore(&ctlr->queue_lock, flags);

	return next;
}
EXPORT_SYMBOL_GPL(spi_get_next_queued_message);

/**
 * spi_finalize_current_message() - the current message is complete
 * @ctlr: the controller to return the message to
 *
 * Called by the driver to notify the core that the message in the front of the
 * queue is complete and can be removed from the queue.
 */
void spi_finalize_current_message(struct spi_controller *ctlr)
{
	struct spi_message *mesg;
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ctlr->queue_lock, flags);
	mesg = ctlr->cur_msg;
	spin_unlock_irqrestore(&ctlr->queue_lock, flags);

	spi_unmap_msg(ctlr, mesg);

	if (ctlr->cur_msg_prepared && ctlr->unprepare_message) {
		ret = ctlr->unprepare_message(ctlr, mesg);
		if (ret) {
			dev_err(&ctlr->dev, "failed to unprepare message: %d\n",
				ret);
		}
	}

	spin_lock_irqsave(&ctlr->queue_lock, flags);
	ctlr->cur_msg = NULL;
	ctlr->cur_msg_prepared = false;
	kthread_queue_work(&ctlr->kworker, &ctlr->pump_messages);
	spin_unlock_irqrestore(&ctlr->queue_lock, flags);

	trace_spi_message_done(mesg);

	mesg->state = NULL;
	if (mesg->complete)
		mesg->complete(mesg->context);
}
EXPORT_SYMBOL_GPL(spi_finalize_current_message);

static int spi_start_queue(struct spi_controller *ctlr)
{
	unsigned long flags;

	spin_lock_irqsave(&ctlr->queue_lock, flags);

	if (ctlr->running || ctlr->busy) {
		spin_unlock_irqrestore(&ctlr->queue_lock, flags);
		return -EBUSY;
	}

	ctlr->running = true;
	ctlr->cur_msg = NULL;
	spin_unlock_irqrestore(&ctlr->queue_lock, flags);

	kthread_queue_work(&ctlr->kworker, &ctlr->pump_messages);

	return 0;
}

static int spi_stop_queue(struct spi_controller *ctlr)
{
	unsigned long flags;
	unsigned limit = 500;
	int ret = 0;

	spin_lock_irqsave(&ctlr->queue_lock, flags);

	/*
	 * This is a bit lame, but is optimized for the common execution path.
	 * A wait_queue on the ctlr->busy could be used, but then the common
	 * execution path (pump_messages) would be required to call wake_up or
	 * friends on every SPI message. Do this instead.
	 */
	while ((!list_empty(&ctlr->queue) || ctlr->busy) && limit--) {
		spin_unlock_irqrestore(&ctlr->queue_lock, flags);
		usleep_range(10000, 11000);
		spin_lock_irqsave(&ctlr->queue_lock, flags);
	}

	if (!list_empty(&ctlr->queue) || ctlr->busy)
		ret = -EBUSY;
	else
		ctlr->running = false;

	spin_unlock_irqrestore(&ctlr->queue_lock, flags);

	if (ret) {
		dev_warn(&ctlr->dev, "could not stop message queue\n");
		return ret;
	}
	return ret;
}

static int spi_destroy_queue(struct spi_controller *ctlr)
{
	int ret;

	ret = spi_stop_queue(ctlr);

	/*
	 * kthread_flush_worker will block until all work is done.
	 * If the reason that stop_queue timed out is that the work will never
	 * finish, then it does no good to call flush/stop thread, so
	 * return anyway.
	 */
	if (ret) {
		dev_err(&ctlr->dev, "problem destroying queue\n");
		return ret;
	}

	kthread_flush_worker(&ctlr->kworker);
	kthread_stop(ctlr->kworker_task);

	return 0;
}

static int __spi_queued_transfer(struct spi_device *spi,
				 struct spi_message *msg,
				 bool need_pump)
{
	struct spi_controller *ctlr = spi->controller;
	unsigned long flags;

	spin_lock_irqsave(&ctlr->queue_lock, flags);

	if (!ctlr->running) {
		spin_unlock_irqrestore(&ctlr->queue_lock, flags);
		return -ESHUTDOWN;
	}
	msg->actual_length = 0;
	msg->status = -EINPROGRESS;

	list_add_tail(&msg->queue, &ctlr->queue);
	if (!ctlr->busy && need_pump)
		kthread_queue_work(&ctlr->kworker, &ctlr->pump_messages);

	spin_unlock_irqrestore(&ctlr->queue_lock, flags);
	return 0;
}

/**
 * spi_queued_transfer - transfer function for queued transfers
 * @spi: spi device which is requesting transfer
 * @msg: spi message which is to handled is queued to driver queue
 *
 * Return: zero on success, else a negative error code.
 */
static int spi_queued_transfer(struct spi_device *spi, struct spi_message *msg)
{
	return __spi_queued_transfer(spi, msg, true);
}

static int spi_controller_initialize_queue(struct spi_controller *ctlr)
{
	int ret;

	ctlr->transfer = spi_queued_transfer;
	if (!ctlr->transfer_one_message)
		ctlr->transfer_one_message = spi_transfer_one_message;

	/* Initialize and start queue */
	ret = spi_init_queue(ctlr);
	if (ret) {
		dev_err(&ctlr->dev, "problem initializing queue\n");
		goto err_init_queue;
	}
	ctlr->queued = true;
	ret = spi_start_queue(ctlr);
	if (ret) {
		dev_err(&ctlr->dev, "problem starting queue\n");
		goto err_start_queue;
	}

	return 0;

err_start_queue:
	spi_destroy_queue(ctlr);
err_init_queue:
	return ret;
}

/**
 * spi_flush_queue - Send all pending messages in the queue from the callers'
 *		     context
 * @ctlr: controller to process queue for
 *
 * This should be used when one wants to ensure all pending messages have been
 * sent before doing something. Is used by the spi-mem code to make sure SPI
 * memory operations do not preempt regular SPI transfers that have been queued
 * before the spi-mem operation.
 */
void spi_flush_queue(struct spi_controller *ctlr)
{
	if (ctlr->transfer == spi_queued_transfer)
		__spi_pump_messages(ctlr, false);
}

/*-------------------------------------------------------------------------*/

#if defined(CONFIG_OF)
static int of_spi_parse_dt(struct spi_controller *ctlr, struct spi_device *spi,
			   struct device_node *nc)
{
	u32 value;
	int rc;

	/* Mode (clock phase/polarity/etc.) */
	if (of_property_read_bool(nc, "spi-cpha"))
		spi->mode |= SPI_CPHA;
	if (of_property_read_bool(nc, "spi-cpol"))
		spi->mode |= SPI_CPOL;
	if (of_property_read_bool(nc, "spi-3wire"))
		spi->mode |= SPI_3WIRE;
	if (of_property_read_bool(nc, "spi-lsb-first"))
		spi->mode |= SPI_LSB_FIRST;

	/*
	 * For descriptors associated with the device, polarity inversion is
	 * handled in the gpiolib, so all chip selects are "active high" in
	 * the logical sense, the gpiolib will invert the line if need be.
	 */
	if (ctlr->use_gpio_descriptors)
		spi->mode |= SPI_CS_HIGH;
	else if (of_property_read_bool(nc, "spi-cs-high"))
		spi->mode |= SPI_CS_HIGH;

	/* Device DUAL/QUAD mode */
	if (!of_property_read_u32(nc, "spi-tx-bus-width", &value)) {
		switch (value) {
		case 1:
			break;
		case 2:
			spi->mode |= SPI_TX_DUAL;
			break;
		case 4:
			spi->mode |= SPI_TX_QUAD;
			break;
		case 8:
			spi->mode |= SPI_TX_OCTAL;
			break;
		default:
			dev_warn(&ctlr->dev,
				"spi-tx-bus-width %d not supported\n",
				value);
			break;
		}
	}

	if (!of_property_read_u32(nc, "spi-rx-bus-width", &value)) {
		switch (value) {
		case 1:
			break;
		case 2:
			spi->mode |= SPI_RX_DUAL;
			break;
		case 4:
			spi->mode |= SPI_RX_QUAD;
			break;
		case 8:
			spi->mode |= SPI_RX_OCTAL;
			break;
		default:
			dev_warn(&ctlr->dev,
				"spi-rx-bus-width %d not supported\n",
				value);
			break;
		}
	}

	if (spi_controller_is_slave(ctlr)) {
		if (!of_node_name_eq(nc, "slave")) {
			dev_err(&ctlr->dev, "%pOF is not called 'slave'\n",
				nc);
			return -EINVAL;
		}
		return 0;
	}

	/* Device address */
	rc = of_property_read_u32(nc, "reg", &value);
	if (rc) {
		dev_err(&ctlr->dev, "%pOF has no valid 'reg' property (%d)\n",
			nc, rc);
		return rc;
	}
	spi->chip_select = value;

	/* Device speed */
	rc = of_property_read_u32(nc, "spi-max-frequency", &value);
	if (rc) {
		dev_err(&ctlr->dev,
			"%pOF has no valid 'spi-max-frequency' property (%d)\n", nc, rc);
		return rc;
	}
	spi->max_speed_hz = value;

	return 0;
}

static struct spi_device *
of_register_spi_device(struct spi_controller *ctlr, struct device_node *nc)
{
	struct spi_device *spi;
	int rc;

	/* Alloc an spi_device */
	spi = spi_alloc_device(ctlr);
	if (!spi) {
		dev_err(&ctlr->dev, "spi_device alloc error for %pOF\n", nc);
		rc = -ENOMEM;
		goto err_out;
	}

	/* Select device driver */
	rc = of_modalias_node(nc, spi->modalias,
				sizeof(spi->modalias));
	if (rc < 0) {
		dev_err(&ctlr->dev, "cannot find modalias for %pOF\n", nc);
		goto err_out;
	}

	rc = of_spi_parse_dt(ctlr, spi, nc);
	if (rc)
		goto err_out;

	/* Store a pointer to the node in the device structure */
	of_node_get(nc);
	spi->dev.of_node = nc;

	/* Register the new device */
	rc = spi_add_device(spi);
	if (rc) {
		dev_err(&ctlr->dev, "spi_device register error %pOF\n", nc);
		goto err_of_node_put;
	}

	return spi;

err_of_node_put:
	of_node_put(nc);
err_out:
	spi_dev_put(spi);
	return ERR_PTR(rc);
}

/**
 * of_register_spi_devices() - Register child devices onto the SPI bus
 * @ctlr:	Pointer to spi_controller device
 *
 * Registers an spi_device for each child node of controller node which
 * represents a valid SPI slave.
 */
static void of_register_spi_devices(struct spi_controller *ctlr)
{
	struct spi_device *spi;
	struct device_node *nc;

	if (!ctlr->dev.of_node)
		return;

	for_each_available_child_of_node(ctlr->dev.of_node, nc) {
		if (of_node_test_and_set_flag(nc, OF_POPULATED))
			continue;
		spi = of_register_spi_device(ctlr, nc);
		if (IS_ERR(spi)) {
			dev_warn(&ctlr->dev,
				 "Failed to create SPI device for %pOF\n", nc);
			of_node_clear_flag(nc, OF_POPULATED);
		}
	}
}
#else
static void of_register_spi_devices(struct spi_controller *ctlr) { }
#endif

#ifdef CONFIG_ACPI
static void acpi_spi_parse_apple_properties(struct spi_device *spi)
{
	struct acpi_device *dev = ACPI_COMPANION(&spi->dev);
	const union acpi_object *obj;

	if (!x86_apple_machine)
		return;

	if (!acpi_dev_get_property(dev, "spiSclkPeriod", ACPI_TYPE_BUFFER, &obj)
	    && obj->buffer.length >= 4)
		spi->max_speed_hz  = NSEC_PER_SEC / *(u32 *)obj->buffer.pointer;

	if (!acpi_dev_get_property(dev, "spiWordSize", ACPI_TYPE_BUFFER, &obj)
	    && obj->buffer.length == 8)
		spi->bits_per_word = *(u64 *)obj->buffer.pointer;

	if (!acpi_dev_get_property(dev, "spiBitOrder", ACPI_TYPE_BUFFER, &obj)
	    && obj->buffer.length == 8 && !*(u64 *)obj->buffer.pointer)
		spi->mode |= SPI_LSB_FIRST;

	if (!acpi_dev_get_property(dev, "spiSPO", ACPI_TYPE_BUFFER, &obj)
	    && obj->buffer.length == 8 &&  *(u64 *)obj->buffer.pointer)
		spi->mode |= SPI_CPOL;

	if (!acpi_dev_get_property(dev, "spiSPH", ACPI_TYPE_BUFFER, &obj)
	    && obj->buffer.length == 8 &&  *(u64 *)obj->buffer.pointer)
		spi->mode |= SPI_CPHA;
}

static int acpi_spi_add_resource(struct acpi_resource *ares, void *data)
{
	struct spi_device *spi = data;
	struct spi_controller *ctlr = spi->controller;

	if (ares->type == ACPI_RESOURCE_TYPE_SERIAL_BUS) {
		struct acpi_resource_spi_serialbus *sb;

		sb = &ares->data.spi_serial_bus;
		if (sb->type == ACPI_RESOURCE_SERIAL_TYPE_SPI) {
			/*
			 * ACPI DeviceSelection numbering is handled by the
			 * host controller driver in Windows and can vary
			 * from driver to driver. In Linux we always expect
			 * 0 .. max - 1 so we need to ask the driver to
			 * translate between the two schemes.
			 */
			if (ctlr->fw_translate_cs) {
				int cs = ctlr->fw_translate_cs(ctlr,
						sb->device_selection);
				if (cs < 0)
					return cs;
				spi->chip_select = cs;
			} else {
				spi->chip_select = sb->device_selection;
			}

			spi->max_speed_hz = sb->connection_speed;

			if (sb->clock_phase == ACPI_SPI_SECOND_PHASE)
				spi->mode |= SPI_CPHA;
			if (sb->clock_polarity == ACPI_SPI_START_HIGH)
				spi->mode |= SPI_CPOL;
			if (sb->device_polarity == ACPI_SPI_ACTIVE_HIGH)
				spi->mode |= SPI_CS_HIGH;
		}
	} else if (spi->irq < 0) {
		struct resource r;

		if (acpi_dev_resource_interrupt(ares, 0, &r))
			spi->irq = r.start;
	}

	/* Always tell the ACPI core to skip this resource */
	return 1;
}

static acpi_status acpi_register_spi_device(struct spi_controller *ctlr,
					    struct acpi_device *adev)
{
	struct list_head resource_list;
	struct spi_device *spi;
	int ret;

	if (acpi_bus_get_status(adev) || !adev->status.present ||
	    acpi_device_enumerated(adev))
		return AE_OK;

	spi = spi_alloc_device(ctlr);
	if (!spi) {
		dev_err(&ctlr->dev, "failed to allocate SPI device for %s\n",
			dev_name(&adev->dev));
		return AE_NO_MEMORY;
	}

	ACPI_COMPANION_SET(&spi->dev, adev);
	spi->irq = -1;

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list,
				     acpi_spi_add_resource, spi);
	acpi_dev_free_resource_list(&resource_list);

	acpi_spi_parse_apple_properties(spi);

	if (ret < 0 || !spi->max_speed_hz) {
		spi_dev_put(spi);
		return AE_OK;
	}

	acpi_set_modalias(adev, acpi_device_hid(adev), spi->modalias,
			  sizeof(spi->modalias));

	if (spi->irq < 0)
		spi->irq = acpi_dev_gpio_irq_get(adev, 0);

	acpi_device_set_enumerated(adev);

	adev->power.flags.ignore_parent = true;
	if (spi_add_device(spi)) {
		adev->power.flags.ignore_parent = false;
		dev_err(&ctlr->dev, "failed to add SPI device %s from ACPI\n",
			dev_name(&adev->dev));
		spi_dev_put(spi);
	}

	return AE_OK;
}

static acpi_status acpi_spi_add_device(acpi_handle handle, u32 level,
				       void *data, void **return_value)
{
	struct spi_controller *ctlr = data;
	struct acpi_device *adev;

	if (acpi_bus_get_device(handle, &adev))
		return AE_OK;

	return acpi_register_spi_device(ctlr, adev);
}

static void acpi_register_spi_devices(struct spi_controller *ctlr)
{
	acpi_status status;
	acpi_handle handle;

	handle = ACPI_HANDLE(ctlr->dev.parent);
	if (!handle)
		return;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, handle, 1,
				     acpi_spi_add_device, NULL, ctlr, NULL);
	if (ACPI_FAILURE(status))
		dev_warn(&ctlr->dev, "failed to enumerate SPI slaves\n");
}
#else
static inline void acpi_register_spi_devices(struct spi_controller *ctlr) {}
#endif /* CONFIG_ACPI */

static void spi_controller_release(struct device *dev)
{
	struct spi_controller *ctlr;

	ctlr = container_of(dev, struct spi_controller, dev);
	kfree(ctlr);
}

static struct class spi_master_class = {
	.name		= "spi_master",
	.owner		= THIS_MODULE,
	.dev_release	= spi_controller_release,
	.dev_groups	= spi_master_groups,
};

#ifdef CONFIG_SPI_SLAVE
/**
 * spi_slave_abort - abort the ongoing transfer request on an SPI slave
 *		     controller
 * @spi: device used for the current transfer
 */
int spi_slave_abort(struct spi_device *spi)
{
	struct spi_controller *ctlr = spi->controller;

	if (spi_controller_is_slave(ctlr) && ctlr->slave_abort)
		return ctlr->slave_abort(ctlr);

	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(spi_slave_abort);

static int match_true(struct device *dev, void *data)
{
	return 1;
}

static ssize_t spi_slave_show(struct device *dev,
			      struct device_attribute *attr, char *buf)
{
	struct spi_controller *ctlr = container_of(dev, struct spi_controller,
						   dev);
	struct device *child;

	child = device_find_child(&ctlr->dev, NULL, match_true);
	return sprintf(buf, "%s\n",
		       child ? to_spi_device(child)->modalias : NULL);
}

static ssize_t spi_slave_store(struct device *dev,
			       struct device_attribute *attr, const char *buf,
			       size_t count)
{
	struct spi_controller *ctlr = container_of(dev, struct spi_controller,
						   dev);
	struct spi_device *spi;
	struct device *child;
	char name[32];
	int rc;

	rc = sscanf(buf, "%31s", name);
	if (rc != 1 || !name[0])
		return -EINVAL;

	child = device_find_child(&ctlr->dev, NULL, match_true);
	if (child) {
		/* Remove registered slave */
		device_unregister(child);
		put_device(child);
	}

	if (strcmp(name, "(null)")) {
		/* Register new slave */
		spi = spi_alloc_device(ctlr);
		if (!spi)
			return -ENOMEM;

		strlcpy(spi->modalias, name, sizeof(spi->modalias));

		rc = spi_add_device(spi);
		if (rc) {
			spi_dev_put(spi);
			return rc;
		}
	}

	return count;
}

static DEVICE_ATTR(slave, 0644, spi_slave_show, spi_slave_store);

static struct attribute *spi_slave_attrs[] = {
	&dev_attr_slave.attr,
	NULL,
};

static const struct attribute_group spi_slave_group = {
	.attrs = spi_slave_attrs,
};

static const struct attribute_group *spi_slave_groups[] = {
	&spi_controller_statistics_group,
	&spi_slave_group,
	NULL,
};

static struct class spi_slave_class = {
	.name		= "spi_slave",
	.owner		= THIS_MODULE,
	.dev_release	= spi_controller_release,
	.dev_groups	= spi_slave_groups,
};
#else
extern struct class spi_slave_class;	/* dummy */
#endif

/**
 * __spi_alloc_controller - allocate an SPI master or slave controller
 * @dev: the controller, possibly using the platform_bus
 * @size: how much zeroed driver-private data to allocate; the pointer to this
 *	memory is in the driver_data field of the returned device,
 *	accessible with spi_controller_get_devdata().
 * @slave: flag indicating whether to allocate an SPI master (false) or SPI
 *	slave (true) controller
 * Context: can sleep
 *
 * This call is used only by SPI controller drivers, which are the
 * only ones directly touching chip registers.  It's how they allocate
 * an spi_controller structure, prior to calling spi_register_controller().
 *
 * This must be called from context that can sleep.
 *
 * The caller is responsible for assigning the bus number and initializing the
 * controller's methods before calling spi_register_controller(); and (after
 * errors adding the device) calling spi_controller_put() to prevent a memory
 * leak.
 *
 * Return: the SPI controller structure on success, else NULL.
 */
struct spi_controller *__spi_alloc_controller(struct device *dev,
					      unsigned int size, bool slave)
{
	struct spi_controller	*ctlr;

	if (!dev)
		return NULL;

	ctlr = kzalloc(size + sizeof(*ctlr), GFP_KERNEL);
	if (!ctlr)
		return NULL;

	device_initialize(&ctlr->dev);
	ctlr->bus_num = -1;
	ctlr->num_chipselect = 1;
	ctlr->slave = slave;
	if (IS_ENABLED(CONFIG_SPI_SLAVE) && slave)
		ctlr->dev.class = &spi_slave_class;
	else
		ctlr->dev.class = &spi_master_class;
	ctlr->dev.parent = dev;
	pm_suspend_ignore_children(&ctlr->dev, true);
	spi_controller_set_devdata(ctlr, &ctlr[1]);

	return ctlr;
}
EXPORT_SYMBOL_GPL(__spi_alloc_controller);

#ifdef CONFIG_OF
static int of_spi_register_master(struct spi_controller *ctlr)
{
	int nb, i, *cs;
	struct device_node *np = ctlr->dev.of_node;

	if (!np)
		return 0;

	nb = of_gpio_named_count(np, "cs-gpios");
	ctlr->num_chipselect = max_t(int, nb, ctlr->num_chipselect);

	/* Return error only for an incorrectly formed cs-gpios property */
	if (nb == 0 || nb == -ENOENT)
		return 0;
	else if (nb < 0)
		return nb;

	cs = devm_kcalloc(&ctlr->dev, ctlr->num_chipselect, sizeof(int),
			  GFP_KERNEL);
	ctlr->cs_gpios = cs;

	if (!ctlr->cs_gpios)
		return -ENOMEM;

	for (i = 0; i < ctlr->num_chipselect; i++)
		cs[i] = -ENOENT;

	for (i = 0; i < nb; i++)
		cs[i] = of_get_named_gpio(np, "cs-gpios", i);

	return 0;
}
#else
static int of_spi_register_master(struct spi_controller *ctlr)
{
	return 0;
}
#endif

/**
 * spi_get_gpio_descs() - grab chip select GPIOs for the master
 * @ctlr: The SPI master to grab GPIO descriptors for
 */
static int spi_get_gpio_descs(struct spi_controller *ctlr)
{
	int nb, i;
	struct gpio_desc **cs;
	struct device *dev = &ctlr->dev;

	nb = gpiod_count(dev, "cs");
	ctlr->num_chipselect = max_t(int, nb, ctlr->num_chipselect);

	/* No GPIOs at all is fine, else return the error */
	if (nb == 0 || nb == -ENOENT)
		return 0;
	else if (nb < 0)
		return nb;

	cs = devm_kcalloc(dev, ctlr->num_chipselect, sizeof(*cs),
			  GFP_KERNEL);
	if (!cs)
		return -ENOMEM;
	ctlr->cs_gpiods = cs;

	for (i = 0; i < nb; i++) {
		/*
		 * Most chipselects are active low, the inverted
		 * semantics are handled by special quirks in gpiolib,
		 * so initializing them GPIOD_OUT_LOW here means
		 * "unasserted", in most cases this will drive the physical
		 * line high.
		 */
		cs[i] = devm_gpiod_get_index_optional(dev, "cs", i,
						      GPIOD_OUT_LOW);

		if (cs[i]) {
			/*
			 * If we find a CS GPIO, name it after the device and
			 * chip select line.
			 */
			char *gpioname;

			gpioname = devm_kasprintf(dev, GFP_KERNEL, "%s CS%d",
						  dev_name(dev), i);
			if (!gpioname)
				return -ENOMEM;
			gpiod_set_consumer_name(cs[i], gpioname);
		}
	}

	return 0;
}

static int spi_controller_check_ops(struct spi_controller *ctlr)
{
	/*
	 * The controller may implement only the high-level SPI-memory like
	 * operations if it does not support regular SPI transfers, and this is
	 * valid use case.
	 * If ->mem_ops is NULL, we request that at least one of the
	 * ->transfer_xxx() method be implemented.
	 */
	if (ctlr->mem_ops) {
		if (!ctlr->mem_ops->exec_op)
			return -EINVAL;
	} else if (!ctlr->transfer && !ctlr->transfer_one &&
		   !ctlr->transfer_one_message) {
		return -EINVAL;
	}

	return 0;
}

/**
 * spi_register_controller - register SPI master or slave controller
 * @ctlr: initialized master, originally from spi_alloc_master() or
 *	spi_alloc_slave()
 * Context: can sleep
 *
 * SPI controllers connect to their drivers using some non-SPI bus,
 * such as the platform bus.  The final stage of probe() in that code
 * includes calling spi_register_controller() to hook up to this SPI bus glue.
 *
 * SPI controllers use board specific (often SOC specific) bus numbers,
 * and board-specific addressing for SPI devices combines those numbers
 * with chip select numbers.  Since SPI does not directly support dynamic
 * device identification, boards need configuration tables telling which
 * chip is at which address.
 *
 * This must be called from context that can sleep.  It returns zero on
 * success, else a negative error code (dropping the controller's refcount).
 * After a successful return, the caller is responsible for calling
 * spi_unregister_controller().
 *
 * Return: zero on success, else a negative error code.
 */
int spi_register_controller(struct spi_controller *ctlr)
{
	struct device		*dev = ctlr->dev.parent;
	struct boardinfo	*bi;
	int			status = -ENODEV;
	int			id, first_dynamic;

	if (!dev)
		return -ENODEV;

	/*
	 * Make sure all necessary hooks are implemented before registering
	 * the SPI controller.
	 */
	status = spi_controller_check_ops(ctlr);
	if (status)
		return status;

	if (!spi_controller_is_slave(ctlr)) {
		if (ctlr->use_gpio_descriptors) {
			status = spi_get_gpio_descs(ctlr);
			if (status)
				return status;
			/*
			 * A controller using GPIO descriptors always
			 * supports SPI_CS_HIGH if need be.
			 */
			ctlr->mode_bits |= SPI_CS_HIGH;
		} else {
			/* Legacy code path for GPIOs from DT */
			status = of_spi_register_master(ctlr);
			if (status)
				return status;
		}
	}

	/* even if it's just one always-selected device, there must
	 * be at least one chipselect
	 */
	if (ctlr->num_chipselect == 0)
		return -EINVAL;
	if (ctlr->bus_num >= 0) {
		/* devices with a fixed bus num must check-in with the num */
		mutex_lock(&board_lock);
		id = idr_alloc(&spi_master_idr, ctlr, ctlr->bus_num,
			ctlr->bus_num + 1, GFP_KERNEL);
		mutex_unlock(&board_lock);
		if (WARN(id < 0, "couldn't get idr"))
			return id == -ENOSPC ? -EBUSY : id;
		ctlr->bus_num = id;
	} else if (ctlr->dev.of_node) {
		/* allocate dynamic bus number using Linux idr */
		id = of_alias_get_id(ctlr->dev.of_node, "spi");
		if (id >= 0) {
			ctlr->bus_num = id;
			mutex_lock(&board_lock);
			id = idr_alloc(&spi_master_idr, ctlr, ctlr->bus_num,
				       ctlr->bus_num + 1, GFP_KERNEL);
			mutex_unlock(&board_lock);
			if (WARN(id < 0, "couldn't get idr"))
				return id == -ENOSPC ? -EBUSY : id;
		}
	}
	if (ctlr->bus_num < 0) {
		first_dynamic = of_alias_get_highest_id("spi");
		if (first_dynamic < 0)
			first_dynamic = 0;
		else
			first_dynamic++;

		mutex_lock(&board_lock);
		id = idr_alloc(&spi_master_idr, ctlr, first_dynamic,
			       0, GFP_KERNEL);
		mutex_unlock(&board_lock);
		if (WARN(id < 0, "couldn't get idr"))
			return id;
		ctlr->bus_num = id;
	}
	INIT_LIST_HEAD(&ctlr->queue);
	spin_lock_init(&ctlr->queue_lock);
	spin_lock_init(&ctlr->bus_lock_spinlock);
	mutex_init(&ctlr->bus_lock_mutex);
	mutex_init(&ctlr->io_mutex);
	ctlr->bus_lock_flag = 0;
	init_completion(&ctlr->xfer_completion);
	if (!ctlr->max_dma_len)
		ctlr->max_dma_len = INT_MAX;

	/* register the device, then userspace will see it.
	 * registration fails if the bus ID is in use.
	 */
	dev_set_name(&ctlr->dev, "spi%u", ctlr->bus_num);
	status = device_add(&ctlr->dev);
	if (status < 0) {
		/* free bus id */
		mutex_lock(&board_lock);
		idr_remove(&spi_master_idr, ctlr->bus_num);
		mutex_unlock(&board_lock);
		goto done;
	}
	dev_dbg(dev, "registered %s %s\n",
			spi_controller_is_slave(ctlr) ? "slave" : "master",
			dev_name(&ctlr->dev));

	/*
	 * If we're using a queued driver, start the queue. Note that we don't
	 * need the queueing logic if the driver is only supporting high-level
	 * memory operations.
	 */
	if (ctlr->transfer) {
		dev_info(dev, "controller is unqueued, this is deprecated\n");
	} else if (ctlr->transfer_one || ctlr->transfer_one_message) {
		status = spi_controller_initialize_queue(ctlr);
		if (status) {
			device_del(&ctlr->dev);
			/* free bus id */
			mutex_lock(&board_lock);
			idr_remove(&spi_master_idr, ctlr->bus_num);
			mutex_unlock(&board_lock);
			goto done;
		}
	}
	/* add statistics */
	spin_lock_init(&ctlr->statistics.lock);

	mutex_lock(&board_lock);
	list_add_tail(&ctlr->list, &spi_controller_list);
	list_for_each_entry(bi, &board_list, list)
		spi_match_controller_to_boardinfo(ctlr, &bi->board_info);
	mutex_unlock(&board_lock);

	/* Register devices from the device tree and ACPI */
	of_register_spi_devices(ctlr);
	acpi_register_spi_devices(ctlr);
done:
	return status;
}
EXPORT_SYMBOL_GPL(spi_register_controller);

static void devm_spi_unregister(struct device *dev, void *res)
{
	spi_unregister_controller(*(struct spi_controller **)res);
}

/**
 * devm_spi_register_controller - register managed SPI master or slave
 *	controller
 * @dev:    device managing SPI controller
 * @ctlr: initialized controller, originally from spi_alloc_master() or
 *	spi_alloc_slave()
 * Context: can sleep
 *
 * Register a SPI device as with spi_register_controller() which will
 * automatically be unregistered and freed.
 *
 * Return: zero on success, else a negative error code.
 */
int devm_spi_register_controller(struct device *dev,
				 struct spi_controller *ctlr)
{
	struct spi_controller **ptr;
	int ret;

	ptr = devres_alloc(devm_spi_unregister, sizeof(*ptr), GFP_KERNEL);
	if (!ptr)
		return -ENOMEM;

	ret = spi_register_controller(ctlr);
	if (!ret) {
		*ptr = ctlr;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return ret;
}
EXPORT_SYMBOL_GPL(devm_spi_register_controller);

static int __unregister(struct device *dev, void *null)
{
	spi_unregister_device(to_spi_device(dev));
	return 0;
}

/**
 * spi_unregister_controller - unregister SPI master or slave controller
 * @ctlr: the controller being unregistered
 * Context: can sleep
 *
 * This call is used only by SPI controller drivers, which are the
 * only ones directly touching chip registers.
 *
 * This must be called from context that can sleep.
 *
 * Note that this function also drops a reference to the controller.
 */
void spi_unregister_controller(struct spi_controller *ctlr)
{
	struct spi_controller *found;
	int id = ctlr->bus_num;
	int dummy;

	/* First make sure that this controller was ever added */
	mutex_lock(&board_lock);
	found = idr_find(&spi_master_idr, id);
	mutex_unlock(&board_lock);
	if (ctlr->queued) {
		if (spi_destroy_queue(ctlr))
			dev_err(&ctlr->dev, "queue remove failed\n");
	}
	mutex_lock(&board_lock);
	list_del(&ctlr->list);
	mutex_unlock(&board_lock);

	dummy = device_for_each_child(&ctlr->dev, NULL, __unregister);
	device_unregister(&ctlr->dev);
	/* free bus id */
	mutex_lock(&board_lock);
	if (found == ctlr)
		idr_remove(&spi_master_idr, id);
	mutex_unlock(&board_lock);
}
EXPORT_SYMBOL_GPL(spi_unregister_controller);

int spi_controller_suspend(struct spi_controller *ctlr)
{
	int ret;

	/* Basically no-ops for non-queued controllers */
	if (!ctlr->queued)
		return 0;

	ret = spi_stop_queue(ctlr);
	if (ret)
		dev_err(&ctlr->dev, "queue stop failed\n");

	return ret;
}
EXPORT_SYMBOL_GPL(spi_controller_suspend);

int spi_controller_resume(struct spi_controller *ctlr)
{
	int ret;

	if (!ctlr->queued)
		return 0;

	ret = spi_start_queue(ctlr);
	if (ret)
		dev_err(&ctlr->dev, "queue restart failed\n");

	return ret;
}
EXPORT_SYMBOL_GPL(spi_controller_resume);

static int __spi_controller_match(struct device *dev, const void *data)
{
	struct spi_controller *ctlr;
	const u16 *bus_num = data;

	ctlr = container_of(dev, struct spi_controller, dev);
	return ctlr->bus_num == *bus_num;
}

/**
 * spi_busnum_to_master - look up master associated with bus_num
 * @bus_num: the master's bus number
 * Context: can sleep
 *
 * This call may be used with devices that are registered after
 * arch init time.  It returns a refcounted pointer to the relevant
 * spi_controller (which the caller must release), or NULL if there is
 * no such master registered.
 *
 * Return: the SPI master structure on success, else NULL.
 */
struct spi_controller *spi_busnum_to_master(u16 bus_num)
{
	struct device		*dev;
	struct spi_controller	*ctlr = NULL;

	dev = class_find_device(&spi_master_class, NULL, &bus_num,
				__spi_controller_match);
	if (dev)
		ctlr = container_of(dev, struct spi_controller, dev);
	/* reference got in class_find_device */
	return ctlr;
}
EXPORT_SYMBOL_GPL(spi_busnum_to_master);

/*-------------------------------------------------------------------------*/

/* Core methods for SPI resource management */

/**
 * spi_res_alloc - allocate a spi resource that is life-cycle managed
 *                 during the processing of a spi_message while using
 *                 spi_transfer_one
 * @spi:     the spi device for which we allocate memory
 * @release: the release code to execute for this resource
 * @size:    size to alloc and return
 * @gfp:     GFP allocation flags
 *
 * Return: the pointer to the allocated data
 *
 * This may get enhanced in the future to allocate from a memory pool
 * of the @spi_device or @spi_controller to avoid repeated allocations.
 */
void *spi_res_alloc(struct spi_device *spi,
		    spi_res_release_t release,
		    size_t size, gfp_t gfp)
{
	struct spi_res *sres;

	sres = kzalloc(sizeof(*sres) + size, gfp);
	if (!sres)
		return NULL;

	INIT_LIST_HEAD(&sres->entry);
	sres->release = release;

	return sres->data;
}
EXPORT_SYMBOL_GPL(spi_res_alloc);

/**
 * spi_res_free - free an spi resource
 * @res: pointer to the custom data of a resource
 *
 */
void spi_res_free(void *res)
{
	struct spi_res *sres = container_of(res, struct spi_res, data);

	if (!res)
		return;

	WARN_ON(!list_empty(&sres->entry));
	kfree(sres);
}
EXPORT_SYMBOL_GPL(spi_res_free);

/**
 * spi_res_add - add a spi_res to the spi_message
 * @message: the spi message
 * @res:     the spi_resource
 */
void spi_res_add(struct spi_message *message, void *res)
{
	struct spi_res *sres = container_of(res, struct spi_res, data);

	WARN_ON(!list_empty(&sres->entry));
	list_add_tail(&sres->entry, &message->resources);
}
EXPORT_SYMBOL_GPL(spi_res_add);

/**
 * spi_res_release - release all spi resources for this message
 * @ctlr:  the @spi_controller
 * @message: the @spi_message
 */
void spi_res_release(struct spi_controller *ctlr, struct spi_message *message)
{
	struct spi_res *res;

	while (!list_empty(&message->resources)) {
		res = list_last_entry(&message->resources,
				      struct spi_res, entry);

		if (res->release)
			res->release(ctlr, message, res->data);

		list_del(&res->entry);

		kfree(res);
	}
}
EXPORT_SYMBOL_GPL(spi_res_release);

/*-------------------------------------------------------------------------*/

/* Core methods for spi_message alterations */

static void __spi_replace_transfers_release(struct spi_controller *ctlr,
					    struct spi_message *msg,
					    void *res)
{
	struct spi_replaced_transfers *rxfer = res;
	size_t i;

	/* call extra callback if requested */
	if (rxfer->release)
		rxfer->release(ctlr, msg, res);

	/* insert replaced transfers back into the message */
	list_splice(&rxfer->replaced_transfers, rxfer->replaced_after);

	/* remove the formerly inserted entries */
	for (i = 0; i < rxfer->inserted; i++)
		list_del(&rxfer->inserted_transfers[i].transfer_list);
}

/**
 * spi_replace_transfers - replace transfers with several transfers
 *                         and register change with spi_message.resources
 * @msg:           the spi_message we work upon
 * @xfer_first:    the first spi_transfer we want to replace
 * @remove:        number of transfers to remove
 * @insert:        the number of transfers we want to insert instead
 * @release:       extra release code necessary in some circumstances
 * @extradatasize: extra data to allocate (with alignment guarantees
 *                 of struct @spi_transfer)
 * @gfp:           gfp flags
 *
 * Returns: pointer to @spi_replaced_transfers,
 *          PTR_ERR(...) in case of errors.
 */
struct spi_replaced_transfers *spi_replace_transfers(
	struct spi_message *msg,
	struct spi_transfer *xfer_first,
	size_t remove,
	size_t insert,
	spi_replaced_release_t release,
	size_t extradatasize,
	gfp_t gfp)
{
	struct spi_replaced_transfers *rxfer;
	struct spi_transfer *xfer;
	size_t i;

	/* allocate the structure using spi_res */
	rxfer = spi_res_alloc(msg->spi, __spi_replace_transfers_release,
			      insert * sizeof(struct spi_transfer)
			      + sizeof(struct spi_replaced_transfers)
			      + extradatasize,
			      gfp);
	if (!rxfer)
		return ERR_PTR(-ENOMEM);

	/* the release code to invoke before running the generic release */
	rxfer->release = release;

	/* assign extradata */
	if (extradatasize)
		rxfer->extradata =
			&rxfer->inserted_transfers[insert];

	/* init the replaced_transfers list */
	INIT_LIST_HEAD(&rxfer->replaced_transfers);

	/* assign the list_entry after which we should reinsert
	 * the @replaced_transfers - it may be spi_message.messages!
	 */
	rxfer->replaced_after = xfer_first->transfer_list.prev;

	/* remove the requested number of transfers */
	for (i = 0; i < remove; i++) {
		/* if the entry after replaced_after it is msg->transfers
		 * then we have been requested to remove more transfers
		 * than are in the list
		 */
		if (rxfer->replaced_after->next == &msg->transfers) {
			dev_err(&msg->spi->dev,
				"requested to remove more spi_transfers than are available\n");
			/* insert replaced transfers back into the message */
			list_splice(&rxfer->replaced_transfers,
				    rxfer->replaced_after);

			/* free the spi_replace_transfer structure */
			spi_res_free(rxfer);

			/* and return with an error */
			return ERR_PTR(-EINVAL);
		}

		/* remove the entry after replaced_after from list of
		 * transfers and add it to list of replaced_transfers
		 */
		list_move_tail(rxfer->replaced_after->next,
			       &rxfer->replaced_transfers);
	}

	/* create copy of the given xfer with identical settings
	 * based on the first transfer to get removed
	 */
	for (i = 0; i < insert; i++) {
		/* we need to run in reverse order */
		xfer = &rxfer->inserted_transfers[insert - 1 - i];

		/* copy all spi_transfer data */
		memcpy(xfer, xfer_first, sizeof(*xfer));

		/* add to list */
		list_add(&xfer->transfer_list, rxfer->replaced_after);

		/* clear cs_change and delay_usecs for all but the last */
		if (i) {
			xfer->cs_change = false;
			xfer->delay_usecs = 0;
		}
	}

	/* set up inserted */
	rxfer->inserted = insert;

	/* and register it with spi_res/spi_message */
	spi_res_add(msg, rxfer);

	return rxfer;
}
EXPORT_SYMBOL_GPL(spi_replace_transfers);

static int __spi_split_transfer_maxsize(struct spi_controller *ctlr,
					struct spi_message *msg,
					struct spi_transfer **xferp,
					size_t maxsize,
					gfp_t gfp)
{
	struct spi_transfer *xfer = *xferp, *xfers;
	struct spi_replaced_transfers *srt;
	size_t offset;
	size_t count, i;

	/* warn once about this fact that we are splitting a transfer */
	dev_warn_once(&msg->spi->dev,
		      "spi_transfer of length %i exceed max length of %zu - needed to split transfers\n",
		      xfer->len, maxsize);

	/* calculate how many we have to replace */
	count = DIV_ROUND_UP(xfer->len, maxsize);

	/* create replacement */
	srt = spi_replace_transfers(msg, xfer, 1, count, NULL, 0, gfp);
	if (IS_ERR(srt))
		return PTR_ERR(srt);
	xfers = srt->inserted_transfers;

	/* now handle each of those newly inserted spi_transfers
	 * note that the replacements spi_transfers all are preset
	 * to the same values as *xferp, so tx_buf, rx_buf and len
	 * are all identical (as well as most others)
	 * so we just have to fix up len and the pointers.
	 *
	 * this also includes support for the depreciated
	 * spi_message.is_dma_mapped interface
	 */

	/* the first transfer just needs the length modified, so we
	 * run it outside the loop
	 */
	xfers[0].len = min_t(size_t, maxsize, xfer[0].len);

	/* all the others need rx_buf/tx_buf also set */
	for (i = 1, offset = maxsize; i < count; offset += maxsize, i++) {
		/* update rx_buf, tx_buf and dma */
		if (xfers[i].rx_buf)
			xfers[i].rx_buf += offset;
		if (xfers[i].rx_dma)
			xfers[i].rx_dma += offset;
		if (xfers[i].tx_buf)
			xfers[i].tx_buf += offset;
		if (xfers[i].tx_dma)
			xfers[i].tx_dma += offset;

		/* update length */
		xfers[i].len = min(maxsize, xfers[i].len - offset);
	}

	/* we set up xferp to the last entry we have inserted,
	 * so that we skip those already split transfers
	 */
	*xferp = &xfers[count - 1];

	/* increment statistics counters */
	SPI_STATISTICS_INCREMENT_FIELD(&ctlr->statistics,
				       transfers_split_maxsize);
	SPI_STATISTICS_INCREMENT_FIELD(&msg->spi->statistics,
				       transfers_split_maxsize);

	return 0;
}

/**
 * spi_split_tranfers_maxsize - split spi transfers into multiple transfers
 *                              when an individual transfer exceeds a
 *                              certain size
 * @ctlr:    the @spi_controller for this transfer
 * @msg:   the @spi_message to transform
 * @maxsize:  the maximum when to apply this
 * @gfp: GFP allocation flags
 *
 * Return: status of transformation
 */
int spi_split_transfers_maxsize(struct spi_controller *ctlr,
				struct spi_message *msg,
				size_t maxsize,
				gfp_t gfp)
{
	struct spi_transfer *xfer;
	int ret;

	/* iterate over the transfer_list,
	 * but note that xfer is advanced to the last transfer inserted
	 * to avoid checking sizes again unnecessarily (also xfer does
	 * potentiall belong to a different list by the time the
	 * replacement has happened
	 */
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		if (xfer->len > maxsize) {
			ret = __spi_split_transfer_maxsize(ctlr, msg, &xfer,
							   maxsize, gfp);
			if (ret)
				return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(spi_split_transfers_maxsize);

/*-------------------------------------------------------------------------*/

/* Core methods for SPI controller protocol drivers.  Some of the
 * other core methods are currently defined as inline functions.
 */

static int __spi_validate_bits_per_word(struct spi_controller *ctlr,
					u8 bits_per_word)
{
	if (ctlr->bits_per_word_mask) {
		/* Only 32 bits fit in the mask */
		if (bits_per_word > 32)
			return -EINVAL;
		if (!(ctlr->bits_per_word_mask & SPI_BPW_MASK(bits_per_word)))
			return -EINVAL;
	}

	return 0;
}

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
 *
 * Return: zero on success, else a negative error code.
 */
int spi_setup(struct spi_device *spi)
{
	unsigned	bad_bits, ugly_bits;
	int		status;

	/* check mode to prevent that DUAL and QUAD set at the same time
	 */
	if (((spi->mode & SPI_TX_DUAL) && (spi->mode & SPI_TX_QUAD)) ||
		((spi->mode & SPI_RX_DUAL) && (spi->mode & SPI_RX_QUAD))) {
		dev_err(&spi->dev,
		"setup: can not select dual and quad at the same time\n");
		return -EINVAL;
	}
	/* if it is SPI_3WIRE mode, DUAL and QUAD should be forbidden
	 */
	if ((spi->mode & SPI_3WIRE) && (spi->mode &
		(SPI_TX_DUAL | SPI_TX_QUAD | SPI_TX_OCTAL |
		 SPI_RX_DUAL | SPI_RX_QUAD | SPI_RX_OCTAL)))
		return -EINVAL;
	/* help drivers fail *cleanly* when they need options
	 * that aren't supported with their current controller
	 * SPI_CS_WORD has a fallback software implementation,
	 * so it is ignored here.
	 */
	bad_bits = spi->mode & ~(spi->controller->mode_bits | SPI_CS_WORD);
	ugly_bits = bad_bits &
		    (SPI_TX_DUAL | SPI_TX_QUAD | SPI_TX_OCTAL |
		     SPI_RX_DUAL | SPI_RX_QUAD | SPI_RX_OCTAL);
	if (ugly_bits) {
		dev_warn(&spi->dev,
			 "setup: ignoring unsupported mode bits %x\n",
			 ugly_bits);
		spi->mode &= ~ugly_bits;
		bad_bits &= ~ugly_bits;
	}
	if (bad_bits) {
		dev_err(&spi->dev, "setup: unsupported mode bits %x\n",
			bad_bits);
		return -EINVAL;
	}

	if (!spi->bits_per_word)
		spi->bits_per_word = 8;

	status = __spi_validate_bits_per_word(spi->controller,
					      spi->bits_per_word);
	if (status)
		return status;

	if (!spi->max_speed_hz)
		spi->max_speed_hz = spi->controller->max_speed_hz;

	if (spi->controller->setup)
		status = spi->controller->setup(spi);

	spi_set_cs(spi, false);

	dev_dbg(&spi->dev, "setup mode %d, %s%s%s%s%u bits/w, %u Hz max --> %d\n",
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

static int __spi_validate(struct spi_device *spi, struct spi_message *message)
{
	struct spi_controller *ctlr = spi->controller;
	struct spi_transfer *xfer;
	int w_size;

	if (list_empty(&message->transfers))
		return -EINVAL;

	/* If an SPI controller does not support toggling the CS line on each
	 * transfer (indicated by the SPI_CS_WORD flag) or we are using a GPIO
	 * for the CS line, we can emulate the CS-per-word hardware function by
	 * splitting transfers into one-word transfers and ensuring that
	 * cs_change is set for each transfer.
	 */
	if ((spi->mode & SPI_CS_WORD) && (!(ctlr->mode_bits & SPI_CS_WORD) ||
					  spi->cs_gpiod ||
					  gpio_is_valid(spi->cs_gpio))) {
		size_t maxsize;
		int ret;

		maxsize = (spi->bits_per_word + 7) / 8;

		/* spi_split_transfers_maxsize() requires message->spi */
		message->spi = spi;

		ret = spi_split_transfers_maxsize(ctlr, message, maxsize,
						  GFP_KERNEL);
		if (ret)
			return ret;

		list_for_each_entry(xfer, &message->transfers, transfer_list) {
			/* don't change cs_change on the last entry in the list */
			if (list_is_last(&xfer->transfer_list, &message->transfers))
				break;
			xfer->cs_change = 1;
		}
	}

	/* Half-duplex links include original MicroWire, and ones with
	 * only one data pin like SPI_3WIRE (switches direction) or where
	 * either MOSI or MISO is missing.  They can also be caused by
	 * software limitations.
	 */
	if ((ctlr->flags & SPI_CONTROLLER_HALF_DUPLEX) ||
	    (spi->mode & SPI_3WIRE)) {
		unsigned flags = ctlr->flags;

		list_for_each_entry(xfer, &message->transfers, transfer_list) {
			if (xfer->rx_buf && xfer->tx_buf)
				return -EINVAL;
			if ((flags & SPI_CONTROLLER_NO_TX) && xfer->tx_buf)
				return -EINVAL;
			if ((flags & SPI_CONTROLLER_NO_RX) && xfer->rx_buf)
				return -EINVAL;
		}
	}

	/**
	 * Set transfer bits_per_word and max speed as spi device default if
	 * it is not set for this transfer.
	 * Set transfer tx_nbits and rx_nbits as single transfer default
	 * (SPI_NBITS_SINGLE) if it is not set for this transfer.
	 * Ensure transfer word_delay is at least as long as that required by
	 * device itself.
	 */
	message->frame_length = 0;
	list_for_each_entry(xfer, &message->transfers, transfer_list) {
		message->frame_length += xfer->len;
		if (!xfer->bits_per_word)
			xfer->bits_per_word = spi->bits_per_word;

		if (!xfer->speed_hz)
			xfer->speed_hz = spi->max_speed_hz;
		if (!xfer->speed_hz)
			xfer->speed_hz = ctlr->max_speed_hz;

		if (ctlr->max_speed_hz && xfer->speed_hz > ctlr->max_speed_hz)
			xfer->speed_hz = ctlr->max_speed_hz;

		if (__spi_validate_bits_per_word(ctlr, xfer->bits_per_word))
			return -EINVAL;

		/*
		 * SPI transfer length should be multiple of SPI word size
		 * where SPI word size should be power-of-two multiple
		 */
		if (xfer->bits_per_word <= 8)
			w_size = 1;
		else if (xfer->bits_per_word <= 16)
			w_size = 2;
		else
			w_size = 4;

		/* No partial transfers accepted */
		if (xfer->len % w_size)
			return -EINVAL;

		if (xfer->speed_hz && ctlr->min_speed_hz &&
		    xfer->speed_hz < ctlr->min_speed_hz)
			return -EINVAL;

		if (xfer->tx_buf && !xfer->tx_nbits)
			xfer->tx_nbits = SPI_NBITS_SINGLE;
		if (xfer->rx_buf && !xfer->rx_nbits)
			xfer->rx_nbits = SPI_NBITS_SINGLE;
		/* check transfer tx/rx_nbits:
		 * 1. check the value matches one of single, dual and quad
		 * 2. check tx/rx_nbits match the mode in spi_device
		 */
		if (xfer->tx_buf) {
			if (xfer->tx_nbits != SPI_NBITS_SINGLE &&
				xfer->tx_nbits != SPI_NBITS_DUAL &&
				xfer->tx_nbits != SPI_NBITS_QUAD)
				return -EINVAL;
			if ((xfer->tx_nbits == SPI_NBITS_DUAL) &&
				!(spi->mode & (SPI_TX_DUAL | SPI_TX_QUAD)))
				return -EINVAL;
			if ((xfer->tx_nbits == SPI_NBITS_QUAD) &&
				!(spi->mode & SPI_TX_QUAD))
				return -EINVAL;
		}
		/* check transfer rx_nbits */
		if (xfer->rx_buf) {
			if (xfer->rx_nbits != SPI_NBITS_SINGLE &&
				xfer->rx_nbits != SPI_NBITS_DUAL &&
				xfer->rx_nbits != SPI_NBITS_QUAD)
				return -EINVAL;
			if ((xfer->rx_nbits == SPI_NBITS_DUAL) &&
				!(spi->mode & (SPI_RX_DUAL | SPI_RX_QUAD)))
				return -EINVAL;
			if ((xfer->rx_nbits == SPI_NBITS_QUAD) &&
				!(spi->mode & SPI_RX_QUAD))
				return -EINVAL;
		}

		if (xfer->word_delay_usecs < spi->word_delay_usecs)
			xfer->word_delay_usecs = spi->word_delay_usecs;
	}

	message->status = -EINPROGRESS;

	return 0;
}

static int __spi_async(struct spi_device *spi, struct spi_message *message)
{
	struct spi_controller *ctlr = spi->controller;

	/*
	 * Some controllers do not support doing regular SPI transfers. Return
	 * ENOTSUPP when this is the case.
	 */
	if (!ctlr->transfer)
		return -ENOTSUPP;

	message->spi = spi;

	SPI_STATISTICS_INCREMENT_FIELD(&ctlr->statistics, spi_async);
	SPI_STATISTICS_INCREMENT_FIELD(&spi->statistics, spi_async);

	trace_spi_message_submit(message);

	return ctlr->transfer(spi, message);
}

/**
 * spi_async - asynchronous SPI transfer
 * @spi: device with which data will be exchanged
 * @message: describes the data transfers, including completion callback
 * Context: any (irqs may be blocked, etc)
 *
 * This call may be used in_irq and other contexts which can't sleep,
 * as well as from task contexts which can sleep.
 *
 * The completion callback is invoked in a context which can't sleep.
 * Before that invocation, the value of message->status is undefined.
 * When the callback is issued, message->status holds either zero (to
 * indicate complete success) or a negative error code.  After that
 * callback returns, the driver which issued the transfer request may
 * deallocate the associated memory; it's no longer in use by any SPI
 * core or controller driver code.
 *
 * Note that although all messages to a spi_device are handled in
 * FIFO order, messages may go to different devices in other orders.
 * Some device might be higher priority, or have various "hard" access
 * time requirements, for example.
 *
 * On detection of any fault during the transfer, processing of
 * the entire message is aborted, and the device is deselected.
 * Until returning from the associated message completion callback,
 * no other spi_message queued to that device will be processed.
 * (This rule applies equally to all the synchronous transfer calls,
 * which are wrappers around this core asynchronous primitive.)
 *
 * Return: zero on success, else a negative error code.
 */
int spi_async(struct spi_device *spi, struct spi_message *message)
{
	struct spi_controller *ctlr = spi->controller;
	int ret;
	unsigned long flags;

	ret = __spi_validate(spi, message);
	if (ret != 0)
		return ret;

	spin_lock_irqsave(&ctlr->bus_lock_spinlock, flags);

	if (ctlr->bus_lock_flag)
		ret = -EBUSY;
	else
		ret = __spi_async(spi, message);

	spin_unlock_irqrestore(&ctlr->bus_lock_spinlock, flags);

	return ret;
}
EXPORT_SYMBOL_GPL(spi_async);

/**
 * spi_async_locked - version of spi_async with exclusive bus usage
 * @spi: device with which data will be exchanged
 * @message: describes the data transfers, including completion callback
 * Context: any (irqs may be blocked, etc)
 *
 * This call may be used in_irq and other contexts which can't sleep,
 * as well as from task contexts which can sleep.
 *
 * The completion callback is invoked in a context which can't sleep.
 * Before that invocation, the value of message->status is undefined.
 * When the callback is issued, message->status holds either zero (to
 * indicate complete success) or a negative error code.  After that
 * callback returns, the driver which issued the transfer request may
 * deallocate the associated memory; it's no longer in use by any SPI
 * core or controller driver code.
 *
 * Note that although all messages to a spi_device are handled in
 * FIFO order, messages may go to different devices in other orders.
 * Some device might be higher priority, or have various "hard" access
 * time requirements, for example.
 *
 * On detection of any fault during the transfer, processing of
 * the entire message is aborted, and the device is deselected.
 * Until returning from the associated message completion callback,
 * no other spi_message queued to that device will be processed.
 * (This rule applies equally to all the synchronous transfer calls,
 * which are wrappers around this core asynchronous primitive.)
 *
 * Return: zero on success, else a negative error code.
 */
int spi_async_locked(struct spi_device *spi, struct spi_message *message)
{
	struct spi_controller *ctlr = spi->controller;
	int ret;
	unsigned long flags;

	ret = __spi_validate(spi, message);
	if (ret != 0)
		return ret;

	spin_lock_irqsave(&ctlr->bus_lock_spinlock, flags);

	ret = __spi_async(spi, message);

	spin_unlock_irqrestore(&ctlr->bus_lock_spinlock, flags);

	return ret;

}
EXPORT_SYMBOL_GPL(spi_async_locked);

/*-------------------------------------------------------------------------*/

/* Utility methods for SPI protocol drivers, layered on
 * top of the core.  Some other utility methods are defined as
 * inline functions.
 */

static void spi_complete(void *arg)
{
	complete(arg);
}

static int __spi_sync(struct spi_device *spi, struct spi_message *message)
{
	DECLARE_COMPLETION_ONSTACK(done);
	int status;
	struct spi_controller *ctlr = spi->controller;
	unsigned long flags;

	status = __spi_validate(spi, message);
	if (status != 0)
		return status;

	message->complete = spi_complete;
	message->context = &done;
	message->spi = spi;

	SPI_STATISTICS_INCREMENT_FIELD(&ctlr->statistics, spi_sync);
	SPI_STATISTICS_INCREMENT_FIELD(&spi->statistics, spi_sync);

	/* If we're not using the legacy transfer method then we will
	 * try to transfer in the calling context so special case.
	 * This code would be less tricky if we could remove the
	 * support for driver implemented message queues.
	 */
	if (ctlr->transfer == spi_queued_transfer) {
		spin_lock_irqsave(&ctlr->bus_lock_spinlock, flags);

		trace_spi_message_submit(message);

		status = __spi_queued_transfer(spi, message, false);

		spin_unlock_irqrestore(&ctlr->bus_lock_spinlock, flags);
	} else {
		status = spi_async_locked(spi, message);
	}

	if (status == 0) {
		/* Push out the messages in the calling context if we
		 * can.
		 */
		if (ctlr->transfer == spi_queued_transfer) {
			SPI_STATISTICS_INCREMENT_FIELD(&ctlr->statistics,
						       spi_sync_immediate);
			SPI_STATISTICS_INCREMENT_FIELD(&spi->statistics,
						       spi_sync_immediate);
			__spi_pump_messages(ctlr, false);
		}

		wait_for_completion(&done);
		status = message->status;
	}
	message->context = NULL;
	return status;
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
 * Return: zero on success, else a negative error code.
 */
int spi_sync(struct spi_device *spi, struct spi_message *message)
{
	int ret;

	mutex_lock(&spi->controller->bus_lock_mutex);
	ret = __spi_sync(spi, message);
	mutex_unlock(&spi->controller->bus_lock_mutex);

	return ret;
}
EXPORT_SYMBOL_GPL(spi_sync);

/**
 * spi_sync_locked - version of spi_sync with exclusive bus usage
 * @spi: device with which data will be exchanged
 * @message: describes the data transfers
 * Context: can sleep
 *
 * This call may only be used from a context that may sleep.  The sleep
 * is non-interruptible, and has no timeout.  Low-overhead controller
 * drivers may DMA directly into and out of the message buffers.
 *
 * This call should be used by drivers that require exclusive access to the
 * SPI bus. It has to be preceded by a spi_bus_lock call. The SPI bus must
 * be released by a spi_bus_unlock call when the exclusive access is over.
 *
 * Return: zero on success, else a negative error code.
 */
int spi_sync_locked(struct spi_device *spi, struct spi_message *message)
{
	return __spi_sync(spi, message);
}
EXPORT_SYMBOL_GPL(spi_sync_locked);

/**
 * spi_bus_lock - obtain a lock for exclusive SPI bus usage
 * @ctlr: SPI bus master that should be locked for exclusive bus access
 * Context: can sleep
 *
 * This call may only be used from a context that may sleep.  The sleep
 * is non-interruptible, and has no timeout.
 *
 * This call should be used by drivers that require exclusive access to the
 * SPI bus. The SPI bus must be released by a spi_bus_unlock call when the
 * exclusive access is over. Data transfer must be done by spi_sync_locked
 * and spi_async_locked calls when the SPI bus lock is held.
 *
 * Return: always zero.
 */
int spi_bus_lock(struct spi_controller *ctlr)
{
	unsigned long flags;

	mutex_lock(&ctlr->bus_lock_mutex);

	spin_lock_irqsave(&ctlr->bus_lock_spinlock, flags);
	ctlr->bus_lock_flag = 1;
	spin_unlock_irqrestore(&ctlr->bus_lock_spinlock, flags);

	/* mutex remains locked until spi_bus_unlock is called */

	return 0;
}
EXPORT_SYMBOL_GPL(spi_bus_lock);

/**
 * spi_bus_unlock - release the lock for exclusive SPI bus usage
 * @ctlr: SPI bus master that was locked for exclusive bus access
 * Context: can sleep
 *
 * This call may only be used from a context that may sleep.  The sleep
 * is non-interruptible, and has no timeout.
 *
 * This call releases an SPI bus lock previously obtained by an spi_bus_lock
 * call.
 *
 * Return: always zero.
 */
int spi_bus_unlock(struct spi_controller *ctlr)
{
	ctlr->bus_lock_flag = 0;

	mutex_unlock(&ctlr->bus_lock_mutex);

	return 0;
}
EXPORT_SYMBOL_GPL(spi_bus_unlock);

/* portable code must never pass more than 32 bytes */
#define	SPI_BUFSIZ	max(32, SMP_CACHE_BYTES)

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
 *
 * Return: zero on success, else a negative error code.
 */
int spi_write_then_read(struct spi_device *spi,
		const void *txbuf, unsigned n_tx,
		void *rxbuf, unsigned n_rx)
{
	static DEFINE_MUTEX(lock);

	int			status;
	struct spi_message	message;
	struct spi_transfer	x[2];
	u8			*local_buf;

	/* Use preallocated DMA-safe buffer if we can.  We can't avoid
	 * copying here, (as a pure convenience thing), but we can
	 * keep heap costs out of the hot path unless someone else is
	 * using the pre-allocated buffer or the transfer is too large.
	 */
	if ((n_tx + n_rx) > SPI_BUFSIZ || !mutex_trylock(&lock)) {
		local_buf = kmalloc(max((unsigned)SPI_BUFSIZ, n_tx + n_rx),
				    GFP_KERNEL | GFP_DMA);
		if (!local_buf)
			return -ENOMEM;
	} else {
		local_buf = buf;
	}

	spi_message_init(&message);
	memset(x, 0, sizeof(x));
	if (n_tx) {
		x[0].len = n_tx;
		spi_message_add_tail(&x[0], &message);
	}
	if (n_rx) {
		x[1].len = n_rx;
		spi_message_add_tail(&x[1], &message);
	}

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

#if IS_ENABLED(CONFIG_OF)
static int __spi_of_device_match(struct device *dev, void *data)
{
	return dev->of_node == data;
}

/* must call put_device() when done with returned spi_device device */
struct spi_device *of_find_spi_device_by_node(struct device_node *node)
{
	struct device *dev = bus_find_device(&spi_bus_type, NULL, node,
						__spi_of_device_match);
	return dev ? to_spi_device(dev) : NULL;
}
EXPORT_SYMBOL_GPL(of_find_spi_device_by_node);
#endif /* IS_ENABLED(CONFIG_OF) */

#if IS_ENABLED(CONFIG_OF_DYNAMIC)
static int __spi_of_controller_match(struct device *dev, const void *data)
{
	return dev->of_node == data;
}

/* the spi controllers are not using spi_bus, so we find it with another way */
static struct spi_controller *of_find_spi_controller_by_node(struct device_node *node)
{
	struct device *dev;

	dev = class_find_device(&spi_master_class, NULL, node,
				__spi_of_controller_match);
	if (!dev && IS_ENABLED(CONFIG_SPI_SLAVE))
		dev = class_find_device(&spi_slave_class, NULL, node,
					__spi_of_controller_match);
	if (!dev)
		return NULL;

	/* reference got in class_find_device */
	return container_of(dev, struct spi_controller, dev);
}

static int of_spi_notify(struct notifier_block *nb, unsigned long action,
			 void *arg)
{
	struct of_reconfig_data *rd = arg;
	struct spi_controller *ctlr;
	struct spi_device *spi;

	switch (of_reconfig_get_state_change(action, arg)) {
	case OF_RECONFIG_CHANGE_ADD:
		ctlr = of_find_spi_controller_by_node(rd->dn->parent);
		if (ctlr == NULL)
			return NOTIFY_OK;	/* not for us */

		if (of_node_test_and_set_flag(rd->dn, OF_POPULATED)) {
			put_device(&ctlr->dev);
			return NOTIFY_OK;
		}

		spi = of_register_spi_device(ctlr, rd->dn);
		put_device(&ctlr->dev);

		if (IS_ERR(spi)) {
			pr_err("%s: failed to create for '%pOF'\n",
					__func__, rd->dn);
			of_node_clear_flag(rd->dn, OF_POPULATED);
			return notifier_from_errno(PTR_ERR(spi));
		}
		break;

	case OF_RECONFIG_CHANGE_REMOVE:
		/* already depopulated? */
		if (!of_node_check_flag(rd->dn, OF_POPULATED))
			return NOTIFY_OK;

		/* find our device by node */
		spi = of_find_spi_device_by_node(rd->dn);
		if (spi == NULL)
			return NOTIFY_OK;	/* no? not meant for us */

		/* unregister takes one ref away */
		spi_unregister_device(spi);

		/* and put the reference of the find */
		put_device(&spi->dev);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block spi_of_notifier = {
	.notifier_call = of_spi_notify,
};
#else /* IS_ENABLED(CONFIG_OF_DYNAMIC) */
extern struct notifier_block spi_of_notifier;
#endif /* IS_ENABLED(CONFIG_OF_DYNAMIC) */

#if IS_ENABLED(CONFIG_ACPI)
static int spi_acpi_controller_match(struct device *dev, const void *data)
{
	return ACPI_COMPANION(dev->parent) == data;
}

static int spi_acpi_device_match(struct device *dev, void *data)
{
	return ACPI_COMPANION(dev) == data;
}

static struct spi_controller *acpi_spi_find_controller_by_adev(struct acpi_device *adev)
{
	struct device *dev;

	dev = class_find_device(&spi_master_class, NULL, adev,
				spi_acpi_controller_match);
	if (!dev && IS_ENABLED(CONFIG_SPI_SLAVE))
		dev = class_find_device(&spi_slave_class, NULL, adev,
					spi_acpi_controller_match);
	if (!dev)
		return NULL;

	return container_of(dev, struct spi_controller, dev);
}

static struct spi_device *acpi_spi_find_device_by_adev(struct acpi_device *adev)
{
	struct device *dev;

	dev = bus_find_device(&spi_bus_type, NULL, adev, spi_acpi_device_match);

	return dev ? to_spi_device(dev) : NULL;
}

static int acpi_spi_notify(struct notifier_block *nb, unsigned long value,
			   void *arg)
{
	struct acpi_device *adev = arg;
	struct spi_controller *ctlr;
	struct spi_device *spi;

	switch (value) {
	case ACPI_RECONFIG_DEVICE_ADD:
		ctlr = acpi_spi_find_controller_by_adev(adev->parent);
		if (!ctlr)
			break;

		acpi_register_spi_device(ctlr, adev);
		put_device(&ctlr->dev);
		break;
	case ACPI_RECONFIG_DEVICE_REMOVE:
		if (!acpi_device_enumerated(adev))
			break;

		spi = acpi_spi_find_device_by_adev(adev);
		if (!spi)
			break;

		spi_unregister_device(spi);
		put_device(&spi->dev);
		break;
	}

	return NOTIFY_OK;
}

static struct notifier_block spi_acpi_notifier = {
	.notifier_call = acpi_spi_notify,
};
#else
extern struct notifier_block spi_acpi_notifier;
#endif

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

	if (IS_ENABLED(CONFIG_SPI_SLAVE)) {
		status = class_register(&spi_slave_class);
		if (status < 0)
			goto err3;
	}

	if (IS_ENABLED(CONFIG_OF_DYNAMIC))
		WARN_ON(of_reconfig_notifier_register(&spi_of_notifier));
	if (IS_ENABLED(CONFIG_ACPI))
		WARN_ON(acpi_reconfig_notifier_register(&spi_acpi_notifier));

	return 0;

err3:
	class_unregister(&spi_master_class);
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

