// SPDX-License-Identifier: GPL-2.0-or-later
// SPI init/core code
//
// Copyright (C) 2005 David Brownell
// Copyright (C) 2008 Secret Lab Technologies Ltd.

#include <linux/acpi.h>
#include <linux/cache.h>
#include <linux/clk/clk-conf.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmaengine.h>
#include <linux/dma-mapping.h>
#include <linux/export.h>
#include <linux/gpio/consumer.h>
#include <linux/highmem.h>
#include <linux/idr.h>
#include <linux/init.h>
#include <linux/ioport.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/mod_devicetable.h>
#include <linux/mutex.h>
#include <linux/of_device.h>
#include <linux/of_irq.h>
#include <linux/percpu.h>
#include <linux/platform_data/x86/apple.h>
#include <linux/pm_domain.h>
#include <linux/pm_runtime.h>
#include <linux/property.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/sched/rt.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/spi/spi-mem.h>
#include <uapi/linux/sched/types.h>

#define CREATE_TRACE_POINTS
#include <trace/events/spi.h>
EXPORT_TRACEPOINT_SYMBOL(spi_transfer_start);
EXPORT_TRACEPOINT_SYMBOL(spi_transfer_stop);

#include "internals.h"

static DEFINE_IDR(spi_master_idr);

static void spidev_release(struct device *dev)
{
	struct spi_device	*spi = to_spi_device(dev);

	spi_controller_put(spi->controller);
	kfree(spi->driver_override);
	free_percpu(spi->pcpu_statistics);
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

	return sysfs_emit(buf, "%s%s\n", SPI_MODULE_PREFIX, spi->modalias);
}
static DEVICE_ATTR_RO(modalias);

static ssize_t driver_override_store(struct device *dev,
				     struct device_attribute *a,
				     const char *buf, size_t count)
{
	struct spi_device *spi = to_spi_device(dev);
	int ret;

	ret = driver_set_override(dev, &spi->driver_override, buf, count);
	if (ret)
		return ret;

	return count;
}

static ssize_t driver_override_show(struct device *dev,
				    struct device_attribute *a, char *buf)
{
	const struct spi_device *spi = to_spi_device(dev);
	ssize_t len;

	device_lock(dev);
	len = sysfs_emit(buf, "%s\n", spi->driver_override ? : "");
	device_unlock(dev);
	return len;
}
static DEVICE_ATTR_RW(driver_override);

static struct spi_statistics __percpu *spi_alloc_pcpu_stats(struct device *dev)
{
	struct spi_statistics __percpu *pcpu_stats;

	if (dev)
		pcpu_stats = devm_alloc_percpu(dev, struct spi_statistics);
	else
		pcpu_stats = alloc_percpu_gfp(struct spi_statistics, GFP_KERNEL);

	if (pcpu_stats) {
		int cpu;

		for_each_possible_cpu(cpu) {
			struct spi_statistics *stat;

			stat = per_cpu_ptr(pcpu_stats, cpu);
			u64_stats_init(&stat->syncp);
		}
	}
	return pcpu_stats;
}

static ssize_t spi_emit_pcpu_stats(struct spi_statistics __percpu *stat,
				   char *buf, size_t offset)
{
	u64 val = 0;
	int i;

	for_each_possible_cpu(i) {
		const struct spi_statistics *pcpu_stats;
		u64_stats_t *field;
		unsigned int start;
		u64 inc;

		pcpu_stats = per_cpu_ptr(stat, i);
		field = (void *)pcpu_stats + offset;
		do {
			start = u64_stats_fetch_begin(&pcpu_stats->syncp);
			inc = u64_stats_read(field);
		} while (u64_stats_fetch_retry(&pcpu_stats->syncp, start));
		val += inc;
	}
	return sysfs_emit(buf, "%llu\n", val);
}

#define SPI_STATISTICS_ATTRS(field, file)				\
static ssize_t spi_controller_##field##_show(struct device *dev,	\
					     struct device_attribute *attr, \
					     char *buf)			\
{									\
	struct spi_controller *ctlr = container_of(dev,			\
					 struct spi_controller, dev);	\
	return spi_statistics_##field##_show(ctlr->pcpu_statistics, buf); \
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
	return spi_statistics_##field##_show(spi->pcpu_statistics, buf); \
}									\
static struct device_attribute dev_attr_spi_device_##field = {		\
	.attr = { .name = file, .mode = 0444 },				\
	.show = spi_device_##field##_show,				\
}

#define SPI_STATISTICS_SHOW_NAME(name, file, field)			\
static ssize_t spi_statistics_##name##_show(struct spi_statistics __percpu *stat, \
					    char *buf)			\
{									\
	return spi_emit_pcpu_stats(stat, buf,				\
			offsetof(struct spi_statistics, field));	\
}									\
SPI_STATISTICS_ATTRS(name, file)

#define SPI_STATISTICS_SHOW(field)					\
	SPI_STATISTICS_SHOW_NAME(field, __stringify(field),		\
				 field)

SPI_STATISTICS_SHOW(messages);
SPI_STATISTICS_SHOW(transfers);
SPI_STATISTICS_SHOW(errors);
SPI_STATISTICS_SHOW(timedout);

SPI_STATISTICS_SHOW(spi_sync);
SPI_STATISTICS_SHOW(spi_sync_immediate);
SPI_STATISTICS_SHOW(spi_async);

SPI_STATISTICS_SHOW(bytes);
SPI_STATISTICS_SHOW(bytes_rx);
SPI_STATISTICS_SHOW(bytes_tx);

#define SPI_STATISTICS_TRANSFER_BYTES_HISTO(index, number)		\
	SPI_STATISTICS_SHOW_NAME(transfer_bytes_histo##index,		\
				 "transfer_bytes_histo_" number,	\
				 transfer_bytes_histo[index])
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

SPI_STATISTICS_SHOW(transfers_split_maxsize);

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

static void spi_statistics_add_transfer_stats(struct spi_statistics __percpu *pcpu_stats,
					      struct spi_transfer *xfer,
					      struct spi_controller *ctlr)
{
	int l2len = min(fls(xfer->len), SPI_STATISTICS_HISTO_SIZE) - 1;
	struct spi_statistics *stats;

	if (l2len < 0)
		l2len = 0;

	get_cpu();
	stats = this_cpu_ptr(pcpu_stats);
	u64_stats_update_begin(&stats->syncp);

	u64_stats_inc(&stats->transfers);
	u64_stats_inc(&stats->transfer_bytes_histo[l2len]);

	u64_stats_add(&stats->bytes, xfer->len);
	if ((xfer->tx_buf) &&
	    (xfer->tx_buf != ctlr->dummy_tx))
		u64_stats_add(&stats->bytes_tx, xfer->len);
	if ((xfer->rx_buf) &&
	    (xfer->rx_buf != ctlr->dummy_rx))
		u64_stats_add(&stats->bytes_rx, xfer->len);

	u64_stats_update_end(&stats->syncp);
	put_cpu();
}

/*
 * modalias support makes "modprobe $MODALIAS" new-style hotplug work,
 * and the sysfs version makes coldplug work too.
 */
static const struct spi_device_id *spi_match_id(const struct spi_device_id *id, const char *name)
{
	while (id->name[0]) {
		if (!strcmp(name, id->name))
			return id;
		id++;
	}
	return NULL;
}

const struct spi_device_id *spi_get_device_id(const struct spi_device *sdev)
{
	const struct spi_driver *sdrv = to_spi_driver(sdev->dev.driver);

	return spi_match_id(sdrv->id_table, sdev->modalias);
}
EXPORT_SYMBOL_GPL(spi_get_device_id);

const void *spi_get_device_match_data(const struct spi_device *sdev)
{
	const void *match;

	match = device_get_match_data(&sdev->dev);
	if (match)
		return match;

	return (const void *)spi_get_device_id(sdev)->driver_data;
}
EXPORT_SYMBOL_GPL(spi_get_device_match_data);

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
		return !!spi_match_id(sdrv->id_table, spi->modalias);

	return strcmp(spi->modalias, drv->name) == 0;
}

static int spi_uevent(const struct device *dev, struct kobj_uevent_env *env)
{
	const struct spi_device		*spi = to_spi_device(dev);
	int rc;

	rc = acpi_device_uevent_modalias(dev, env);
	if (rc != -ENODEV)
		return rc;

	return add_uevent_var(env, "MODALIAS=%s%s", SPI_MODULE_PREFIX, spi->modalias);
}

static int spi_probe(struct device *dev)
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

	if (sdrv->probe) {
		ret = sdrv->probe(spi);
		if (ret)
			dev_pm_domain_detach(dev, true);
	}

	return ret;
}

static void spi_remove(struct device *dev)
{
	const struct spi_driver		*sdrv = to_spi_driver(dev->driver);

	if (sdrv->remove)
		sdrv->remove(to_spi_device(dev));

	dev_pm_domain_detach(dev, true);
}

static void spi_shutdown(struct device *dev)
{
	if (dev->driver) {
		const struct spi_driver	*sdrv = to_spi_driver(dev->driver);

		if (sdrv->shutdown)
			sdrv->shutdown(to_spi_device(dev));
	}
}

const struct bus_type spi_bus_type = {
	.name		= "spi",
	.dev_groups	= spi_dev_groups,
	.match		= spi_match_device,
	.uevent		= spi_uevent,
	.probe		= spi_probe,
	.remove		= spi_remove,
	.shutdown	= spi_shutdown,
};
EXPORT_SYMBOL_GPL(spi_bus_type);

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

	/*
	 * For Really Good Reasons we use spi: modaliases not of:
	 * modaliases for DT so module autoloading won't work if we
	 * don't have a spi_device_id as well as a compatible string.
	 */
	if (sdrv->driver.of_match_table) {
		const struct of_device_id *of_id;

		for (of_id = sdrv->driver.of_match_table; of_id->compatible[0];
		     of_id++) {
			const char *of_name;

			/* Strip off any vendor prefix */
			of_name = strnchr(of_id->compatible,
					  sizeof(of_id->compatible), ',');
			if (of_name)
				of_name++;
			else
				of_name = of_id->compatible;

			if (sdrv->id_table) {
				const struct spi_device_id *spi_id;

				spi_id = spi_match_id(sdrv->id_table, of_name);
				if (spi_id)
					continue;
			} else {
				if (strcmp(sdrv->driver.name, of_name) == 0)
					continue;
			}

			pr_warn("SPI driver %s has no spi_device_id for %s\n",
				sdrv->driver.name, of_id->compatible);
		}
	}

	return driver_register(&sdrv->driver);
}
EXPORT_SYMBOL_GPL(__spi_register_driver);

/*-------------------------------------------------------------------------*/

/*
 * SPI devices should normally not be created by SPI device drivers; that
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
 * Used to protect add/del operation for board_info list and
 * spi_controller list, and their matching process also used
 * to protect object of type struct idr.
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

	spi->pcpu_statistics = spi_alloc_pcpu_stats(NULL);
	if (!spi->pcpu_statistics) {
		kfree(spi);
		spi_controller_put(ctlr);
		return NULL;
	}

	spi->controller = ctlr;
	spi->dev.parent = &ctlr->dev;
	spi->dev.bus = &spi_bus_type;
	spi->dev.release = spidev_release;
	spi->mode = ctlr->buswidth_override_bits;

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
		     spi_get_chipselect(spi, 0));
}

/*
 * Zero(0) is a valid physical CS value and can be located at any
 * logical CS in the spi->chip_select[]. If all the physical CS
 * are initialized to 0 then It would be difficult to differentiate
 * between a valid physical CS 0 & an unused logical CS whose physical
 * CS can be 0. As a solution to this issue initialize all the CS to -1.
 * Now all the unused logical CS will have -1 physical CS value & can be
 * ignored while performing physical CS validity checks.
 */
#define SPI_INVALID_CS		((s8)-1)

static inline bool is_valid_cs(s8 chip_select)
{
	return chip_select != SPI_INVALID_CS;
}

static inline int spi_dev_check_cs(struct device *dev,
				   struct spi_device *spi, u8 idx,
				   struct spi_device *new_spi, u8 new_idx)
{
	u8 cs, cs_new;
	u8 idx_new;

	cs = spi_get_chipselect(spi, idx);
	for (idx_new = new_idx; idx_new < SPI_CS_CNT_MAX; idx_new++) {
		cs_new = spi_get_chipselect(new_spi, idx_new);
		if (is_valid_cs(cs) && is_valid_cs(cs_new) && cs == cs_new) {
			dev_err(dev, "chipselect %u already in use\n", cs_new);
			return -EBUSY;
		}
	}
	return 0;
}

static int spi_dev_check(struct device *dev, void *data)
{
	struct spi_device *spi = to_spi_device(dev);
	struct spi_device *new_spi = data;
	int status, idx;

	if (spi->controller == new_spi->controller) {
		for (idx = 0; idx < SPI_CS_CNT_MAX; idx++) {
			status = spi_dev_check_cs(dev, spi, idx, new_spi, 0);
			if (status)
				return status;
		}
	}
	return 0;
}

static void spi_cleanup(struct spi_device *spi)
{
	if (spi->controller->cleanup)
		spi->controller->cleanup(spi);
}

static int __spi_add_device(struct spi_device *spi)
{
	struct spi_controller *ctlr = spi->controller;
	struct device *dev = ctlr->dev.parent;
	int status, idx;
	u8 cs;

	for (idx = 0; idx < SPI_CS_CNT_MAX; idx++) {
		/* Chipselects are numbered 0..max; validate. */
		cs = spi_get_chipselect(spi, idx);
		if (is_valid_cs(cs) && cs >= ctlr->num_chipselect) {
			dev_err(dev, "cs%d >= max %d\n", spi_get_chipselect(spi, idx),
				ctlr->num_chipselect);
			return -EINVAL;
		}
	}

	/*
	 * Make sure that multiple logical CS doesn't map to the same physical CS.
	 * For example, spi->chip_select[0] != spi->chip_select[1] and so on.
	 */
	for (idx = 0; idx < SPI_CS_CNT_MAX; idx++) {
		status = spi_dev_check_cs(dev, spi, idx, spi, idx + 1);
		if (status)
			return status;
	}

	/* Set the bus ID string */
	spi_dev_set_name(spi);

	/*
	 * We need to make sure there's no other device with this
	 * chipselect **BEFORE** we call setup(), else we'll trash
	 * its configuration.
	 */
	status = bus_for_each_dev(&spi_bus_type, NULL, spi, spi_dev_check);
	if (status)
		return status;

	/* Controller may unregister concurrently */
	if (IS_ENABLED(CONFIG_SPI_DYNAMIC) &&
	    !device_is_registered(&ctlr->dev)) {
		return -ENODEV;
	}

	if (ctlr->cs_gpiods) {
		u8 cs;

		for (idx = 0; idx < SPI_CS_CNT_MAX; idx++) {
			cs = spi_get_chipselect(spi, idx);
			if (is_valid_cs(cs))
				spi_set_csgpiod(spi, idx, ctlr->cs_gpiods[cs]);
		}
	}

	/*
	 * Drivers may modify this initial i/o setup, but will
	 * normally rely on the device being setup.  Devices
	 * using SPI_CS_HIGH can't coexist well otherwise...
	 */
	status = spi_setup(spi);
	if (status < 0) {
		dev_err(dev, "can't setup %s, status %d\n",
				dev_name(&spi->dev), status);
		return status;
	}

	/* Device may be bound to an active driver when this returns */
	status = device_add(&spi->dev);
	if (status < 0) {
		dev_err(dev, "can't add %s, status %d\n",
				dev_name(&spi->dev), status);
		spi_cleanup(spi);
	} else {
		dev_dbg(dev, "registered child %s\n", dev_name(&spi->dev));
	}

	return status;
}

/**
 * spi_add_device - Add spi_device allocated with spi_alloc_device
 * @spi: spi_device to register
 *
 * Companion function to spi_alloc_device.  Devices allocated with
 * spi_alloc_device can be added onto the SPI bus with this function.
 *
 * Return: 0 on success; negative errno on failure
 */
int spi_add_device(struct spi_device *spi)
{
	struct spi_controller *ctlr = spi->controller;
	int status;

	/* Set the bus ID string */
	spi_dev_set_name(spi);

	mutex_lock(&ctlr->add_lock);
	status = __spi_add_device(spi);
	mutex_unlock(&ctlr->add_lock);
	return status;
}
EXPORT_SYMBOL_GPL(spi_add_device);

static void spi_set_all_cs_unused(struct spi_device *spi)
{
	u8 idx;

	for (idx = 0; idx < SPI_CS_CNT_MAX; idx++)
		spi_set_chipselect(spi, idx, SPI_INVALID_CS);
}

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

	/*
	 * NOTE:  caller did any chip->bus_num checks necessary.
	 *
	 * Also, unless we change the return value convention to use
	 * error-or-pointer (not NULL-or-pointer), troubleshootability
	 * suggests syslogged diagnostics are best here (ugh).
	 */

	proxy = spi_alloc_device(ctlr);
	if (!proxy)
		return NULL;

	WARN_ON(strlen(chip->modalias) >= sizeof(proxy->modalias));

	/* Use provided chip-select for proxy device */
	spi_set_all_cs_unused(proxy);
	spi_set_chipselect(proxy, 0, chip->chip_select);

	proxy->max_speed_hz = chip->max_speed_hz;
	proxy->mode = chip->mode;
	proxy->irq = chip->irq;
	strscpy(proxy->modalias, chip->modalias, sizeof(proxy->modalias));
	proxy->dev.platform_data = (void *) chip->platform_data;
	proxy->controller_data = chip->controller_data;
	proxy->controller_state = NULL;
	/*
	 * spi->chip_select[i] gives the corresponding physical CS for logical CS i
	 * logical CS number is represented by setting the ith bit in spi->cs_index_mask
	 * So, for example, if spi->cs_index_mask = 0x01 then logical CS number is 0 and
	 * spi->chip_select[0] will give the physical CS.
	 * By default spi->chip_select[0] will hold the physical CS number so, set
	 * spi->cs_index_mask as 0x01.
	 */
	proxy->cs_index_mask = 0x01;

	if (chip->swnode) {
		status = device_add_software_node(&proxy->dev, chip->swnode);
		if (status) {
			dev_err(&ctlr->dev, "failed to add software node to '%s': %d\n",
				chip->modalias, status);
			goto err_dev_put;
		}
	}

	status = spi_add_device(proxy);
	if (status < 0)
		goto err_dev_put;

	return proxy;

err_dev_put:
	device_remove_software_node(&proxy->dev);
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
	device_remove_software_node(&spi->dev);
	device_del(&spi->dev);
	spi_cleanup(spi);
	put_device(&spi->dev);
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

/* Core methods for SPI resource management */

/**
 * spi_res_alloc - allocate a spi resource that is life-cycle managed
 *                 during the processing of a spi_message while using
 *                 spi_transfer_one
 * @spi:     the SPI device for which we allocate memory
 * @release: the release code to execute for this resource
 * @size:    size to alloc and return
 * @gfp:     GFP allocation flags
 *
 * Return: the pointer to the allocated data
 *
 * This may get enhanced in the future to allocate from a memory pool
 * of the @spi_device or @spi_controller to avoid repeated allocations.
 */
static void *spi_res_alloc(struct spi_device *spi, spi_res_release_t release,
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

/**
 * spi_res_free - free an SPI resource
 * @res: pointer to the custom data of a resource
 */
static void spi_res_free(void *res)
{
	struct spi_res *sres = container_of(res, struct spi_res, data);

	if (!res)
		return;

	WARN_ON(!list_empty(&sres->entry));
	kfree(sres);
}

/**
 * spi_res_add - add a spi_res to the spi_message
 * @message: the SPI message
 * @res:     the spi_resource
 */
static void spi_res_add(struct spi_message *message, void *res)
{
	struct spi_res *sres = container_of(res, struct spi_res, data);

	WARN_ON(!list_empty(&sres->entry));
	list_add_tail(&sres->entry, &message->resources);
}

/**
 * spi_res_release - release all SPI resources for this message
 * @ctlr:  the @spi_controller
 * @message: the @spi_message
 */
static void spi_res_release(struct spi_controller *ctlr, struct spi_message *message)
{
	struct spi_res *res, *tmp;

	list_for_each_entry_safe_reverse(res, tmp, &message->resources, entry) {
		if (res->release)
			res->release(ctlr, message, res->data);

		list_del(&res->entry);

		kfree(res);
	}
}

/*-------------------------------------------------------------------------*/
static inline bool spi_is_last_cs(struct spi_device *spi)
{
	u8 idx;
	bool last = false;

	for (idx = 0; idx < SPI_CS_CNT_MAX; idx++) {
		if (spi->cs_index_mask & BIT(idx)) {
			if (spi->controller->last_cs[idx] == spi_get_chipselect(spi, idx))
				last = true;
		}
	}
	return last;
}


static void spi_set_cs(struct spi_device *spi, bool enable, bool force)
{
	bool activate = enable;
	u8 idx;

	/*
	 * Avoid calling into the driver (or doing delays) if the chip select
	 * isn't actually changing from the last time this was called.
	 */
	if (!force && ((enable && spi->controller->last_cs_index_mask == spi->cs_index_mask &&
			spi_is_last_cs(spi)) ||
		       (!enable && spi->controller->last_cs_index_mask == spi->cs_index_mask &&
			!spi_is_last_cs(spi))) &&
	    (spi->controller->last_cs_mode_high == (spi->mode & SPI_CS_HIGH)))
		return;

	trace_spi_set_cs(spi, activate);

	spi->controller->last_cs_index_mask = spi->cs_index_mask;
	for (idx = 0; idx < SPI_CS_CNT_MAX; idx++)
		spi->controller->last_cs[idx] = enable ? spi_get_chipselect(spi, 0) : SPI_INVALID_CS;
	spi->controller->last_cs_mode_high = spi->mode & SPI_CS_HIGH;

	if (spi->mode & SPI_CS_HIGH)
		enable = !enable;

	if (spi_is_csgpiod(spi)) {
		if (!spi->controller->set_cs_timing && !activate)
			spi_delay_exec(&spi->cs_hold, NULL);

		if (!(spi->mode & SPI_NO_CS)) {
			/*
			 * Historically ACPI has no means of the GPIO polarity and
			 * thus the SPISerialBus() resource defines it on the per-chip
			 * basis. In order to avoid a chain of negations, the GPIO
			 * polarity is considered being Active High. Even for the cases
			 * when _DSD() is involved (in the updated versions of ACPI)
			 * the GPIO CS polarity must be defined Active High to avoid
			 * ambiguity. That's why we use enable, that takes SPI_CS_HIGH
			 * into account.
			 */
			for (idx = 0; idx < SPI_CS_CNT_MAX; idx++) {
				if ((spi->cs_index_mask & BIT(idx)) && spi_get_csgpiod(spi, idx)) {
					if (has_acpi_companion(&spi->dev))
						gpiod_set_value_cansleep(spi_get_csgpiod(spi, idx),
									 !enable);
					else
						/* Polarity handled by GPIO library */
						gpiod_set_value_cansleep(spi_get_csgpiod(spi, idx),
									 activate);

					if (activate)
						spi_delay_exec(&spi->cs_setup, NULL);
					else
						spi_delay_exec(&spi->cs_inactive, NULL);
				}
			}
		}
		/* Some SPI masters need both GPIO CS & slave_select */
		if ((spi->controller->flags & SPI_CONTROLLER_GPIO_SS) &&
		    spi->controller->set_cs)
			spi->controller->set_cs(spi, !enable);

		if (!spi->controller->set_cs_timing) {
			if (activate)
				spi_delay_exec(&spi->cs_setup, NULL);
			else
				spi_delay_exec(&spi->cs_inactive, NULL);
		}
	} else if (spi->controller->set_cs) {
		spi->controller->set_cs(spi, !enable);
	}
}

#ifdef CONFIG_HAS_DMA
static int spi_map_buf_attrs(struct spi_controller *ctlr, struct device *dev,
			     struct sg_table *sgt, void *buf, size_t len,
			     enum dma_data_direction dir, unsigned long attrs)
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
		desc_len = min_t(unsigned long, max_seg_size, PAGE_SIZE);
		sgs = DIV_ROUND_UP(len + offset_in_page(buf), desc_len);
	} else if (virt_addr_valid(buf)) {
		desc_len = min_t(size_t, max_seg_size, ctlr->max_dma_len);
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

	ret = dma_map_sgtable(dev, sgt, dir, attrs);
	if (ret < 0) {
		sg_free_table(sgt);
		return ret;
	}

	return 0;
}

int spi_map_buf(struct spi_controller *ctlr, struct device *dev,
		struct sg_table *sgt, void *buf, size_t len,
		enum dma_data_direction dir)
{
	return spi_map_buf_attrs(ctlr, dev, sgt, buf, len, dir, 0);
}

static void spi_unmap_buf_attrs(struct spi_controller *ctlr,
				struct device *dev, struct sg_table *sgt,
				enum dma_data_direction dir,
				unsigned long attrs)
{
	if (sgt->orig_nents) {
		dma_unmap_sgtable(dev, sgt, dir, attrs);
		sg_free_table(sgt);
		sgt->orig_nents = 0;
		sgt->nents = 0;
	}
}

void spi_unmap_buf(struct spi_controller *ctlr, struct device *dev,
		   struct sg_table *sgt, enum dma_data_direction dir)
{
	spi_unmap_buf_attrs(ctlr, dev, sgt, dir, 0);
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
	else if (ctlr->dma_map_dev)
		tx_dev = ctlr->dma_map_dev;
	else
		tx_dev = ctlr->dev.parent;

	if (ctlr->dma_rx)
		rx_dev = ctlr->dma_rx->device->dev;
	else if (ctlr->dma_map_dev)
		rx_dev = ctlr->dma_map_dev;
	else
		rx_dev = ctlr->dev.parent;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		/* The sync is done before each transfer. */
		unsigned long attrs = DMA_ATTR_SKIP_CPU_SYNC;

		if (!ctlr->can_dma(ctlr, msg->spi, xfer))
			continue;

		if (xfer->tx_buf != NULL) {
			ret = spi_map_buf_attrs(ctlr, tx_dev, &xfer->tx_sg,
						(void *)xfer->tx_buf,
						xfer->len, DMA_TO_DEVICE,
						attrs);
			if (ret != 0)
				return ret;
		}

		if (xfer->rx_buf != NULL) {
			ret = spi_map_buf_attrs(ctlr, rx_dev, &xfer->rx_sg,
						xfer->rx_buf, xfer->len,
						DMA_FROM_DEVICE, attrs);
			if (ret != 0) {
				spi_unmap_buf_attrs(ctlr, tx_dev,
						&xfer->tx_sg, DMA_TO_DEVICE,
						attrs);

				return ret;
			}
		}
	}

	ctlr->cur_rx_dma_dev = rx_dev;
	ctlr->cur_tx_dma_dev = tx_dev;
	ctlr->cur_msg_mapped = true;

	return 0;
}

static int __spi_unmap_msg(struct spi_controller *ctlr, struct spi_message *msg)
{
	struct device *rx_dev = ctlr->cur_rx_dma_dev;
	struct device *tx_dev = ctlr->cur_tx_dma_dev;
	struct spi_transfer *xfer;

	if (!ctlr->cur_msg_mapped || !ctlr->can_dma)
		return 0;

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		/* The sync has already been done after each transfer. */
		unsigned long attrs = DMA_ATTR_SKIP_CPU_SYNC;

		if (!ctlr->can_dma(ctlr, msg->spi, xfer))
			continue;

		spi_unmap_buf_attrs(ctlr, rx_dev, &xfer->rx_sg,
				    DMA_FROM_DEVICE, attrs);
		spi_unmap_buf_attrs(ctlr, tx_dev, &xfer->tx_sg,
				    DMA_TO_DEVICE, attrs);
	}

	ctlr->cur_msg_mapped = false;

	return 0;
}

static void spi_dma_sync_for_device(struct spi_controller *ctlr,
				    struct spi_transfer *xfer)
{
	struct device *rx_dev = ctlr->cur_rx_dma_dev;
	struct device *tx_dev = ctlr->cur_tx_dma_dev;

	if (!ctlr->cur_msg_mapped)
		return;

	if (xfer->tx_sg.orig_nents)
		dma_sync_sgtable_for_device(tx_dev, &xfer->tx_sg, DMA_TO_DEVICE);
	if (xfer->rx_sg.orig_nents)
		dma_sync_sgtable_for_device(rx_dev, &xfer->rx_sg, DMA_FROM_DEVICE);
}

static void spi_dma_sync_for_cpu(struct spi_controller *ctlr,
				 struct spi_transfer *xfer)
{
	struct device *rx_dev = ctlr->cur_rx_dma_dev;
	struct device *tx_dev = ctlr->cur_tx_dma_dev;

	if (!ctlr->cur_msg_mapped)
		return;

	if (xfer->rx_sg.orig_nents)
		dma_sync_sgtable_for_cpu(rx_dev, &xfer->rx_sg, DMA_FROM_DEVICE);
	if (xfer->tx_sg.orig_nents)
		dma_sync_sgtable_for_cpu(tx_dev, &xfer->tx_sg, DMA_TO_DEVICE);
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

static void spi_dma_sync_for_device(struct spi_controller *ctrl,
				    struct spi_transfer *xfer)
{
}

static void spi_dma_sync_for_cpu(struct spi_controller *ctrl,
				 struct spi_transfer *xfer)
{
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

	if ((ctlr->flags & (SPI_CONTROLLER_MUST_RX | SPI_CONTROLLER_MUST_TX))
		&& !(msg->spi->mode & SPI_3WIRE)) {
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
				       GFP_KERNEL | GFP_DMA | __GFP_ZERO);
			if (!tmp)
				return -ENOMEM;
			ctlr->dummy_tx = tmp;
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
				if (!xfer->len)
					continue;
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
	struct spi_statistics __percpu *statm = ctlr->pcpu_statistics;
	struct spi_statistics __percpu *stats = msg->spi->pcpu_statistics;
	u32 speed_hz = xfer->speed_hz;
	unsigned long long ms;

	if (spi_controller_is_slave(ctlr)) {
		if (wait_for_completion_interruptible(&ctlr->xfer_completion)) {
			dev_dbg(&msg->spi->dev, "SPI transfer interrupted\n");
			return -EINTR;
		}
	} else {
		if (!speed_hz)
			speed_hz = 100000;

		/*
		 * For each byte we wait for 8 cycles of the SPI clock.
		 * Since speed is defined in Hz and we want milliseconds,
		 * use respective multiplier, but before the division,
		 * otherwise we may get 0 for short transfers.
		 */
		ms = 8LL * MSEC_PER_SEC * xfer->len;
		do_div(ms, speed_hz);

		/*
		 * Increase it twice and add 200 ms tolerance, use
		 * predefined maximum in case of overflow.
		 */
		ms += ms + 200;
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

		if (xfer->error & SPI_TRANS_FAIL_IO)
			return -EIO;
	}

	return 0;
}

static void _spi_transfer_delay_ns(u32 ns)
{
	if (!ns)
		return;
	if (ns <= NSEC_PER_USEC) {
		ndelay(ns);
	} else {
		u32 us = DIV_ROUND_UP(ns, NSEC_PER_USEC);

		if (us <= 10)
			udelay(us);
		else
			usleep_range(us, us + DIV_ROUND_UP(us, 10));
	}
}

int spi_delay_to_ns(struct spi_delay *_delay, struct spi_transfer *xfer)
{
	u32 delay = _delay->value;
	u32 unit = _delay->unit;
	u32 hz;

	if (!delay)
		return 0;

	switch (unit) {
	case SPI_DELAY_UNIT_USECS:
		delay *= NSEC_PER_USEC;
		break;
	case SPI_DELAY_UNIT_NSECS:
		/* Nothing to do here */
		break;
	case SPI_DELAY_UNIT_SCK:
		/* Clock cycles need to be obtained from spi_transfer */
		if (!xfer)
			return -EINVAL;
		/*
		 * If there is unknown effective speed, approximate it
		 * by underestimating with half of the requested Hz.
		 */
		hz = xfer->effective_speed_hz ?: xfer->speed_hz / 2;
		if (!hz)
			return -EINVAL;

		/* Convert delay to nanoseconds */
		delay *= DIV_ROUND_UP(NSEC_PER_SEC, hz);
		break;
	default:
		return -EINVAL;
	}

	return delay;
}
EXPORT_SYMBOL_GPL(spi_delay_to_ns);

int spi_delay_exec(struct spi_delay *_delay, struct spi_transfer *xfer)
{
	int delay;

	might_sleep();

	if (!_delay)
		return -EINVAL;

	delay = spi_delay_to_ns(_delay, xfer);
	if (delay < 0)
		return delay;

	_spi_transfer_delay_ns(delay);

	return 0;
}
EXPORT_SYMBOL_GPL(spi_delay_exec);

static void _spi_transfer_cs_change_delay(struct spi_message *msg,
					  struct spi_transfer *xfer)
{
	u32 default_delay_ns = 10 * NSEC_PER_USEC;
	u32 delay = xfer->cs_change_delay.value;
	u32 unit = xfer->cs_change_delay.unit;
	int ret;

	/* Return early on "fast" mode - for everything but USECS */
	if (!delay) {
		if (unit == SPI_DELAY_UNIT_USECS)
			_spi_transfer_delay_ns(default_delay_ns);
		return;
	}

	ret = spi_delay_exec(&xfer->cs_change_delay, xfer);
	if (ret) {
		dev_err_once(&msg->spi->dev,
			     "Use of unsupported delay unit %i, using default of %luus\n",
			     unit, default_delay_ns / NSEC_PER_USEC);
		_spi_transfer_delay_ns(default_delay_ns);
	}
}

void spi_transfer_cs_change_delay_exec(struct spi_message *msg,
						  struct spi_transfer *xfer)
{
	_spi_transfer_cs_change_delay(msg, xfer);
}
EXPORT_SYMBOL_GPL(spi_transfer_cs_change_delay_exec);

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
	struct spi_statistics __percpu *statm = ctlr->pcpu_statistics;
	struct spi_statistics __percpu *stats = msg->spi->pcpu_statistics;

	xfer = list_first_entry(&msg->transfers, struct spi_transfer, transfer_list);
	spi_set_cs(msg->spi, !xfer->cs_off, false);

	SPI_STATISTICS_INCREMENT_FIELD(statm, messages);
	SPI_STATISTICS_INCREMENT_FIELD(stats, messages);

	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		trace_spi_transfer_start(msg, xfer);

		spi_statistics_add_transfer_stats(statm, xfer, ctlr);
		spi_statistics_add_transfer_stats(stats, xfer, ctlr);

		if (!ctlr->ptp_sts_supported) {
			xfer->ptp_sts_word_pre = 0;
			ptp_read_system_prets(xfer->ptp_sts);
		}

		if ((xfer->tx_buf || xfer->rx_buf) && xfer->len) {
			reinit_completion(&ctlr->xfer_completion);

fallback_pio:
			spi_dma_sync_for_device(ctlr, xfer);
			ret = ctlr->transfer_one(ctlr, msg->spi, xfer);
			if (ret < 0) {
				spi_dma_sync_for_cpu(ctlr, xfer);

				if (ctlr->cur_msg_mapped &&
				   (xfer->error & SPI_TRANS_FAIL_NO_START)) {
					__spi_unmap_msg(ctlr, msg);
					ctlr->fallback = true;
					xfer->error &= ~SPI_TRANS_FAIL_NO_START;
					goto fallback_pio;
				}

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

			spi_dma_sync_for_cpu(ctlr, xfer);
		} else {
			if (xfer->len)
				dev_err(&msg->spi->dev,
					"Bufferless transfer has length %u\n",
					xfer->len);
		}

		if (!ctlr->ptp_sts_supported) {
			ptp_read_system_postts(xfer->ptp_sts);
			xfer->ptp_sts_word_post = xfer->len;
		}

		trace_spi_transfer_stop(msg, xfer);

		if (msg->status != -EINPROGRESS)
			goto out;

		spi_transfer_delay_exec(xfer);

		if (xfer->cs_change) {
			if (list_is_last(&xfer->transfer_list,
					 &msg->transfers)) {
				keep_cs = true;
			} else {
				if (!xfer->cs_off)
					spi_set_cs(msg->spi, false, false);
				_spi_transfer_cs_change_delay(msg, xfer);
				if (!list_next_entry(xfer, transfer_list)->cs_off)
					spi_set_cs(msg->spi, true, false);
			}
		} else if (!list_is_last(&xfer->transfer_list, &msg->transfers) &&
			   xfer->cs_off != list_next_entry(xfer, transfer_list)->cs_off) {
			spi_set_cs(msg->spi, xfer->cs_off, false);
		}

		msg->actual_length += xfer->len;
	}

out:
	if (ret != 0 || !keep_cs)
		spi_set_cs(msg->spi, false, false);

	if (msg->status == -EINPROGRESS)
		msg->status = ret;

	if (msg->status && ctlr->handle_err)
		ctlr->handle_err(ctlr, msg);

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

static void spi_idle_runtime_pm(struct spi_controller *ctlr)
{
	if (ctlr->auto_runtime_pm) {
		pm_runtime_mark_last_busy(ctlr->dev.parent);
		pm_runtime_put_autosuspend(ctlr->dev.parent);
	}
}

static int __spi_pump_transfer_message(struct spi_controller *ctlr,
		struct spi_message *msg, bool was_busy)
{
	struct spi_transfer *xfer;
	int ret;

	if (!was_busy && ctlr->auto_runtime_pm) {
		ret = pm_runtime_get_sync(ctlr->dev.parent);
		if (ret < 0) {
			pm_runtime_put_noidle(ctlr->dev.parent);
			dev_err(&ctlr->dev, "Failed to power device: %d\n",
				ret);

			msg->status = ret;
			spi_finalize_current_message(ctlr);

			return ret;
		}
	}

	if (!was_busy)
		trace_spi_controller_busy(ctlr);

	if (!was_busy && ctlr->prepare_transfer_hardware) {
		ret = ctlr->prepare_transfer_hardware(ctlr);
		if (ret) {
			dev_err(&ctlr->dev,
				"failed to prepare transfer hardware: %d\n",
				ret);

			if (ctlr->auto_runtime_pm)
				pm_runtime_put(ctlr->dev.parent);

			msg->status = ret;
			spi_finalize_current_message(ctlr);

			return ret;
		}
	}

	trace_spi_message_start(msg);

	if (ctlr->prepare_message) {
		ret = ctlr->prepare_message(ctlr, msg);
		if (ret) {
			dev_err(&ctlr->dev, "failed to prepare message: %d\n",
				ret);
			msg->status = ret;
			spi_finalize_current_message(ctlr);
			return ret;
		}
		msg->prepared = true;
	}

	ret = spi_map_msg(ctlr, msg);
	if (ret) {
		msg->status = ret;
		spi_finalize_current_message(ctlr);
		return ret;
	}

	if (!ctlr->ptp_sts_supported && !ctlr->transfer_one) {
		list_for_each_entry(xfer, &msg->transfers, transfer_list) {
			xfer->ptp_sts_word_pre = 0;
			ptp_read_system_prets(xfer->ptp_sts);
		}
	}

	/*
	 * Drivers implementation of transfer_one_message() must arrange for
	 * spi_finalize_current_message() to get called. Most drivers will do
	 * this in the calling context, but some don't. For those cases, a
	 * completion is used to guarantee that this function does not return
	 * until spi_finalize_current_message() is done accessing
	 * ctlr->cur_msg.
	 * Use of the following two flags enable to opportunistically skip the
	 * use of the completion since its use involves expensive spin locks.
	 * In case of a race with the context that calls
	 * spi_finalize_current_message() the completion will always be used,
	 * due to strict ordering of these flags using barriers.
	 */
	WRITE_ONCE(ctlr->cur_msg_incomplete, true);
	WRITE_ONCE(ctlr->cur_msg_need_completion, false);
	reinit_completion(&ctlr->cur_msg_completion);
	smp_wmb(); /* Make these available to spi_finalize_current_message() */

	ret = ctlr->transfer_one_message(ctlr, msg);
	if (ret) {
		dev_err(&ctlr->dev,
			"failed to transfer one message from queue\n");
		return ret;
	}

	WRITE_ONCE(ctlr->cur_msg_need_completion, true);
	smp_mb(); /* See spi_finalize_current_message()... */
	if (READ_ONCE(ctlr->cur_msg_incomplete))
		wait_for_completion(&ctlr->cur_msg_completion);

	return 0;
}

/**
 * __spi_pump_messages - function which processes SPI message queue
 * @ctlr: controller to process queue for
 * @in_kthread: true if we are in the context of the message pump thread
 *
 * This function checks if there is any SPI message in the queue that
 * needs processing and if so call out to the driver to initialize hardware
 * and transfer each message.
 *
 * Note that it is called both from the kthread itself and also from
 * inside spi_sync(); the queue extraction handling at the top of the
 * function should deal with this safely.
 */
static void __spi_pump_messages(struct spi_controller *ctlr, bool in_kthread)
{
	struct spi_message *msg;
	bool was_busy = false;
	unsigned long flags;
	int ret;

	/* Take the I/O mutex */
	mutex_lock(&ctlr->io_mutex);

	/* Lock queue */
	spin_lock_irqsave(&ctlr->queue_lock, flags);

	/* Make sure we are not already running a message */
	if (ctlr->cur_msg)
		goto out_unlock;

	/* Check if the queue is idle */
	if (list_empty(&ctlr->queue) || !ctlr->running) {
		if (!ctlr->busy)
			goto out_unlock;

		/* Defer any non-atomic teardown to the thread */
		if (!in_kthread) {
			if (!ctlr->dummy_rx && !ctlr->dummy_tx &&
			    !ctlr->unprepare_transfer_hardware) {
				spi_idle_runtime_pm(ctlr);
				ctlr->busy = false;
				ctlr->queue_empty = true;
				trace_spi_controller_idle(ctlr);
			} else {
				kthread_queue_work(ctlr->kworker,
						   &ctlr->pump_messages);
			}
			goto out_unlock;
		}

		ctlr->busy = false;
		spin_unlock_irqrestore(&ctlr->queue_lock, flags);

		kfree(ctlr->dummy_rx);
		ctlr->dummy_rx = NULL;
		kfree(ctlr->dummy_tx);
		ctlr->dummy_tx = NULL;
		if (ctlr->unprepare_transfer_hardware &&
		    ctlr->unprepare_transfer_hardware(ctlr))
			dev_err(&ctlr->dev,
				"failed to unprepare transfer hardware\n");
		spi_idle_runtime_pm(ctlr);
		trace_spi_controller_idle(ctlr);

		spin_lock_irqsave(&ctlr->queue_lock, flags);
		ctlr->queue_empty = true;
		goto out_unlock;
	}

	/* Extract head of queue */
	msg = list_first_entry(&ctlr->queue, struct spi_message, queue);
	ctlr->cur_msg = msg;

	list_del_init(&msg->queue);
	if (ctlr->busy)
		was_busy = true;
	else
		ctlr->busy = true;
	spin_unlock_irqrestore(&ctlr->queue_lock, flags);

	ret = __spi_pump_transfer_message(ctlr, msg, was_busy);
	kthread_queue_work(ctlr->kworker, &ctlr->pump_messages);

	ctlr->cur_msg = NULL;
	ctlr->fallback = false;

	mutex_unlock(&ctlr->io_mutex);

	/* Prod the scheduler in case transfer_one() was busy waiting */
	if (!ret)
		cond_resched();
	return;

out_unlock:
	spin_unlock_irqrestore(&ctlr->queue_lock, flags);
	mutex_unlock(&ctlr->io_mutex);
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

/**
 * spi_take_timestamp_pre - helper to collect the beginning of the TX timestamp
 * @ctlr: Pointer to the spi_controller structure of the driver
 * @xfer: Pointer to the transfer being timestamped
 * @progress: How many words (not bytes) have been transferred so far
 * @irqs_off: If true, will disable IRQs and preemption for the duration of the
 *	      transfer, for less jitter in time measurement. Only compatible
 *	      with PIO drivers. If true, must follow up with
 *	      spi_take_timestamp_post or otherwise system will crash.
 *	      WARNING: for fully predictable results, the CPU frequency must
 *	      also be under control (governor).
 *
 * This is a helper for drivers to collect the beginning of the TX timestamp
 * for the requested byte from the SPI transfer. The frequency with which this
 * function must be called (once per word, once for the whole transfer, once
 * per batch of words etc) is arbitrary as long as the @tx buffer offset is
 * greater than or equal to the requested byte at the time of the call. The
 * timestamp is only taken once, at the first such call. It is assumed that
 * the driver advances its @tx buffer pointer monotonically.
 */
void spi_take_timestamp_pre(struct spi_controller *ctlr,
			    struct spi_transfer *xfer,
			    size_t progress, bool irqs_off)
{
	if (!xfer->ptp_sts)
		return;

	if (xfer->timestamped)
		return;

	if (progress > xfer->ptp_sts_word_pre)
		return;

	/* Capture the resolution of the timestamp */
	xfer->ptp_sts_word_pre = progress;

	if (irqs_off) {
		local_irq_save(ctlr->irq_flags);
		preempt_disable();
	}

	ptp_read_system_prets(xfer->ptp_sts);
}
EXPORT_SYMBOL_GPL(spi_take_timestamp_pre);

/**
 * spi_take_timestamp_post - helper to collect the end of the TX timestamp
 * @ctlr: Pointer to the spi_controller structure of the driver
 * @xfer: Pointer to the transfer being timestamped
 * @progress: How many words (not bytes) have been transferred so far
 * @irqs_off: If true, will re-enable IRQs and preemption for the local CPU.
 *
 * This is a helper for drivers to collect the end of the TX timestamp for
 * the requested byte from the SPI transfer. Can be called with an arbitrary
 * frequency: only the first call where @tx exceeds or is equal to the
 * requested word will be timestamped.
 */
void spi_take_timestamp_post(struct spi_controller *ctlr,
			     struct spi_transfer *xfer,
			     size_t progress, bool irqs_off)
{
	if (!xfer->ptp_sts)
		return;

	if (xfer->timestamped)
		return;

	if (progress < xfer->ptp_sts_word_post)
		return;

	ptp_read_system_postts(xfer->ptp_sts);

	if (irqs_off) {
		local_irq_restore(ctlr->irq_flags);
		preempt_enable();
	}

	/* Capture the resolution of the timestamp */
	xfer->ptp_sts_word_post = progress;

	xfer->timestamped = 1;
}
EXPORT_SYMBOL_GPL(spi_take_timestamp_post);

/**
 * spi_set_thread_rt - set the controller to pump at realtime priority
 * @ctlr: controller to boost priority of
 *
 * This can be called because the controller requested realtime priority
 * (by setting the ->rt value before calling spi_register_controller()) or
 * because a device on the bus said that its transfers needed realtime
 * priority.
 *
 * NOTE: at the moment if any device on a bus says it needs realtime then
 * the thread will be at realtime priority for all transfers on that
 * controller.  If this eventually becomes a problem we may see if we can
 * find a way to boost the priority only temporarily during relevant
 * transfers.
 */
static void spi_set_thread_rt(struct spi_controller *ctlr)
{
	dev_info(&ctlr->dev,
		"will run message pump with realtime priority\n");
	sched_set_fifo(ctlr->kworker->task);
}

static int spi_init_queue(struct spi_controller *ctlr)
{
	ctlr->running = false;
	ctlr->busy = false;
	ctlr->queue_empty = true;

	ctlr->kworker = kthread_create_worker(0, dev_name(&ctlr->dev));
	if (IS_ERR(ctlr->kworker)) {
		dev_err(&ctlr->dev, "failed to create message pump kworker\n");
		return PTR_ERR(ctlr->kworker);
	}

	kthread_init_work(&ctlr->pump_messages, spi_pump_messages);

	/*
	 * Controller config will indicate if this controller should run the
	 * message pump with high (realtime) priority to reduce the transfer
	 * latency on the bus by minimising the delay between a transfer
	 * request and the scheduling of the message pump thread. Without this
	 * setting the message pump thread will remain at default priority.
	 */
	if (ctlr->rt)
		spi_set_thread_rt(ctlr);

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

	/* Get a pointer to the next message, if any */
	spin_lock_irqsave(&ctlr->queue_lock, flags);
	next = list_first_entry_or_null(&ctlr->queue, struct spi_message,
					queue);
	spin_unlock_irqrestore(&ctlr->queue_lock, flags);

	return next;
}
EXPORT_SYMBOL_GPL(spi_get_next_queued_message);

/*
 * __spi_unoptimize_message - shared implementation of spi_unoptimize_message()
 *                            and spi_maybe_unoptimize_message()
 * @msg: the message to unoptimize
 *
 * Peripheral drivers should use spi_unoptimize_message() and callers inside
 * core should use spi_maybe_unoptimize_message() rather than calling this
 * function directly.
 *
 * It is not valid to call this on a message that is not currently optimized.
 */
static void __spi_unoptimize_message(struct spi_message *msg)
{
	struct spi_controller *ctlr = msg->spi->controller;

	if (ctlr->unoptimize_message)
		ctlr->unoptimize_message(msg);

	spi_res_release(ctlr, msg);

	msg->optimized = false;
	msg->opt_state = NULL;
}

/*
 * spi_maybe_unoptimize_message - unoptimize msg not managed by a peripheral
 * @msg: the message to unoptimize
 *
 * This function is used to unoptimize a message if and only if it was
 * optimized by the core (via spi_maybe_optimize_message()).
 */
static void spi_maybe_unoptimize_message(struct spi_message *msg)
{
	if (!msg->pre_optimized && msg->optimized)
		__spi_unoptimize_message(msg);
}

/**
 * spi_finalize_current_message() - the current message is complete
 * @ctlr: the controller to return the message to
 *
 * Called by the driver to notify the core that the message in the front of the
 * queue is complete and can be removed from the queue.
 */
void spi_finalize_current_message(struct spi_controller *ctlr)
{
	struct spi_transfer *xfer;
	struct spi_message *mesg;
	int ret;

	mesg = ctlr->cur_msg;

	if (!ctlr->ptp_sts_supported && !ctlr->transfer_one) {
		list_for_each_entry(xfer, &mesg->transfers, transfer_list) {
			ptp_read_system_postts(xfer->ptp_sts);
			xfer->ptp_sts_word_post = xfer->len;
		}
	}

	if (unlikely(ctlr->ptp_sts_supported))
		list_for_each_entry(xfer, &mesg->transfers, transfer_list)
			WARN_ON_ONCE(xfer->ptp_sts && !xfer->timestamped);

	spi_unmap_msg(ctlr, mesg);

	if (mesg->prepared && ctlr->unprepare_message) {
		ret = ctlr->unprepare_message(ctlr, mesg);
		if (ret) {
			dev_err(&ctlr->dev, "failed to unprepare message: %d\n",
				ret);
		}
	}

	mesg->prepared = false;

	spi_maybe_unoptimize_message(mesg);

	WRITE_ONCE(ctlr->cur_msg_incomplete, false);
	smp_mb(); /* See __spi_pump_transfer_message()... */
	if (READ_ONCE(ctlr->cur_msg_need_completion))
		complete(&ctlr->cur_msg_completion);

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

	kthread_queue_work(ctlr->kworker, &ctlr->pump_messages);

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

	kthread_destroy_worker(ctlr->kworker);

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
	ctlr->queue_empty = false;
	if (!ctlr->busy && need_pump)
		kthread_queue_work(ctlr->kworker, &ctlr->pump_messages);

	spin_unlock_irqrestore(&ctlr->queue_lock, flags);
	return 0;
}

/**
 * spi_queued_transfer - transfer function for queued transfers
 * @spi: SPI device which is requesting transfer
 * @msg: SPI message which is to handled is queued to driver queue
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
static void of_spi_parse_dt_cs_delay(struct device_node *nc,
				     struct spi_delay *delay, const char *prop)
{
	u32 value;

	if (!of_property_read_u32(nc, prop, &value)) {
		if (value > U16_MAX) {
			delay->value = DIV_ROUND_UP(value, 1000);
			delay->unit = SPI_DELAY_UNIT_USECS;
		} else {
			delay->value = value;
			delay->unit = SPI_DELAY_UNIT_NSECS;
		}
	}
}

static int of_spi_parse_dt(struct spi_controller *ctlr, struct spi_device *spi,
			   struct device_node *nc)
{
	u32 value, cs[SPI_CS_CNT_MAX];
	int rc, idx;

	/* Mode (clock phase/polarity/etc.) */
	if (of_property_read_bool(nc, "spi-cpha"))
		spi->mode |= SPI_CPHA;
	if (of_property_read_bool(nc, "spi-cpol"))
		spi->mode |= SPI_CPOL;
	if (of_property_read_bool(nc, "spi-3wire"))
		spi->mode |= SPI_3WIRE;
	if (of_property_read_bool(nc, "spi-lsb-first"))
		spi->mode |= SPI_LSB_FIRST;
	if (of_property_read_bool(nc, "spi-cs-high"))
		spi->mode |= SPI_CS_HIGH;

	/* Device DUAL/QUAD mode */
	if (!of_property_read_u32(nc, "spi-tx-bus-width", &value)) {
		switch (value) {
		case 0:
			spi->mode |= SPI_NO_TX;
			break;
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
		case 0:
			spi->mode |= SPI_NO_RX;
			break;
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

	if (ctlr->num_chipselect > SPI_CS_CNT_MAX) {
		dev_err(&ctlr->dev, "No. of CS is more than max. no. of supported CS\n");
		return -EINVAL;
	}

	spi_set_all_cs_unused(spi);

	/* Device address */
	rc = of_property_read_variable_u32_array(nc, "reg", &cs[0], 1,
						 SPI_CS_CNT_MAX);
	if (rc < 0) {
		dev_err(&ctlr->dev, "%pOF has no valid 'reg' property (%d)\n",
			nc, rc);
		return rc;
	}
	if (rc > ctlr->num_chipselect) {
		dev_err(&ctlr->dev, "%pOF has number of CS > ctlr->num_chipselect (%d)\n",
			nc, rc);
		return rc;
	}
	if ((of_property_read_bool(nc, "parallel-memories")) &&
	    (!(ctlr->flags & SPI_CONTROLLER_MULTI_CS))) {
		dev_err(&ctlr->dev, "SPI controller doesn't support multi CS\n");
		return -EINVAL;
	}
	for (idx = 0; idx < rc; idx++)
		spi_set_chipselect(spi, idx, cs[idx]);

	/*
	 * By default spi->chip_select[0] will hold the physical CS number,
	 * so set bit 0 in spi->cs_index_mask.
	 */
	spi->cs_index_mask = BIT(0);

	/* Device speed */
	if (!of_property_read_u32(nc, "spi-max-frequency", &value))
		spi->max_speed_hz = value;

	/* Device CS delays */
	of_spi_parse_dt_cs_delay(nc, &spi->cs_setup, "spi-cs-setup-delay-ns");
	of_spi_parse_dt_cs_delay(nc, &spi->cs_hold, "spi-cs-hold-delay-ns");
	of_spi_parse_dt_cs_delay(nc, &spi->cs_inactive, "spi-cs-inactive-delay-ns");

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
	rc = of_alias_from_compatible(nc, spi->modalias,
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

	device_set_node(&spi->dev, of_fwnode_handle(nc));

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

/**
 * spi_new_ancillary_device() - Register ancillary SPI device
 * @spi:         Pointer to the main SPI device registering the ancillary device
 * @chip_select: Chip Select of the ancillary device
 *
 * Register an ancillary SPI device; for example some chips have a chip-select
 * for normal device usage and another one for setup/firmware upload.
 *
 * This may only be called from main SPI device's probe routine.
 *
 * Return: 0 on success; negative errno on failure
 */
struct spi_device *spi_new_ancillary_device(struct spi_device *spi,
					     u8 chip_select)
{
	struct spi_controller *ctlr = spi->controller;
	struct spi_device *ancillary;
	int rc = 0;

	/* Alloc an spi_device */
	ancillary = spi_alloc_device(ctlr);
	if (!ancillary) {
		rc = -ENOMEM;
		goto err_out;
	}

	strscpy(ancillary->modalias, "dummy", sizeof(ancillary->modalias));

	/* Use provided chip-select for ancillary device */
	spi_set_all_cs_unused(ancillary);
	spi_set_chipselect(ancillary, 0, chip_select);

	/* Take over SPI mode/speed from SPI main device */
	ancillary->max_speed_hz = spi->max_speed_hz;
	ancillary->mode = spi->mode;
	/*
	 * By default spi->chip_select[0] will hold the physical CS number,
	 * so set bit 0 in spi->cs_index_mask.
	 */
	ancillary->cs_index_mask = BIT(0);

	WARN_ON(!mutex_is_locked(&ctlr->add_lock));

	/* Register the new device */
	rc = __spi_add_device(ancillary);
	if (rc) {
		dev_err(&spi->dev, "failed to register ancillary device\n");
		goto err_out;
	}

	return ancillary;

err_out:
	spi_dev_put(ancillary);
	return ERR_PTR(rc);
}
EXPORT_SYMBOL_GPL(spi_new_ancillary_device);

#ifdef CONFIG_ACPI
struct acpi_spi_lookup {
	struct spi_controller 	*ctlr;
	u32			max_speed_hz;
	u32			mode;
	int			irq;
	u8			bits_per_word;
	u8			chip_select;
	int			n;
	int			index;
};

static int acpi_spi_count(struct acpi_resource *ares, void *data)
{
	struct acpi_resource_spi_serialbus *sb;
	int *count = data;

	if (ares->type != ACPI_RESOURCE_TYPE_SERIAL_BUS)
		return 1;

	sb = &ares->data.spi_serial_bus;
	if (sb->type != ACPI_RESOURCE_SERIAL_TYPE_SPI)
		return 1;

	*count = *count + 1;

	return 1;
}

/**
 * acpi_spi_count_resources - Count the number of SpiSerialBus resources
 * @adev:	ACPI device
 *
 * Return: the number of SpiSerialBus resources in the ACPI-device's
 * resource-list; or a negative error code.
 */
int acpi_spi_count_resources(struct acpi_device *adev)
{
	LIST_HEAD(r);
	int count = 0;
	int ret;

	ret = acpi_dev_get_resources(adev, &r, acpi_spi_count, &count);
	if (ret < 0)
		return ret;

	acpi_dev_free_resource_list(&r);

	return count;
}
EXPORT_SYMBOL_GPL(acpi_spi_count_resources);

static void acpi_spi_parse_apple_properties(struct acpi_device *dev,
					    struct acpi_spi_lookup *lookup)
{
	const union acpi_object *obj;

	if (!x86_apple_machine)
		return;

	if (!acpi_dev_get_property(dev, "spiSclkPeriod", ACPI_TYPE_BUFFER, &obj)
	    && obj->buffer.length >= 4)
		lookup->max_speed_hz  = NSEC_PER_SEC / *(u32 *)obj->buffer.pointer;

	if (!acpi_dev_get_property(dev, "spiWordSize", ACPI_TYPE_BUFFER, &obj)
	    && obj->buffer.length == 8)
		lookup->bits_per_word = *(u64 *)obj->buffer.pointer;

	if (!acpi_dev_get_property(dev, "spiBitOrder", ACPI_TYPE_BUFFER, &obj)
	    && obj->buffer.length == 8 && !*(u64 *)obj->buffer.pointer)
		lookup->mode |= SPI_LSB_FIRST;

	if (!acpi_dev_get_property(dev, "spiSPO", ACPI_TYPE_BUFFER, &obj)
	    && obj->buffer.length == 8 &&  *(u64 *)obj->buffer.pointer)
		lookup->mode |= SPI_CPOL;

	if (!acpi_dev_get_property(dev, "spiSPH", ACPI_TYPE_BUFFER, &obj)
	    && obj->buffer.length == 8 &&  *(u64 *)obj->buffer.pointer)
		lookup->mode |= SPI_CPHA;
}

static int acpi_spi_add_resource(struct acpi_resource *ares, void *data)
{
	struct acpi_spi_lookup *lookup = data;
	struct spi_controller *ctlr = lookup->ctlr;

	if (ares->type == ACPI_RESOURCE_TYPE_SERIAL_BUS) {
		struct acpi_resource_spi_serialbus *sb;
		acpi_handle parent_handle;
		acpi_status status;

		sb = &ares->data.spi_serial_bus;
		if (sb->type == ACPI_RESOURCE_SERIAL_TYPE_SPI) {

			if (lookup->index != -1 && lookup->n++ != lookup->index)
				return 1;

			status = acpi_get_handle(NULL,
						 sb->resource_source.string_ptr,
						 &parent_handle);

			if (ACPI_FAILURE(status))
				return -ENODEV;

			if (ctlr) {
				if (ACPI_HANDLE(ctlr->dev.parent) != parent_handle)
					return -ENODEV;
			} else {
				struct acpi_device *adev;

				adev = acpi_fetch_acpi_dev(parent_handle);
				if (!adev)
					return -ENODEV;

				ctlr = acpi_spi_find_controller_by_adev(adev);
				if (!ctlr)
					return -EPROBE_DEFER;

				lookup->ctlr = ctlr;
			}

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
				lookup->chip_select = cs;
			} else {
				lookup->chip_select = sb->device_selection;
			}

			lookup->max_speed_hz = sb->connection_speed;
			lookup->bits_per_word = sb->data_bit_length;

			if (sb->clock_phase == ACPI_SPI_SECOND_PHASE)
				lookup->mode |= SPI_CPHA;
			if (sb->clock_polarity == ACPI_SPI_START_HIGH)
				lookup->mode |= SPI_CPOL;
			if (sb->device_polarity == ACPI_SPI_ACTIVE_HIGH)
				lookup->mode |= SPI_CS_HIGH;
		}
	} else if (lookup->irq < 0) {
		struct resource r;

		if (acpi_dev_resource_interrupt(ares, 0, &r))
			lookup->irq = r.start;
	}

	/* Always tell the ACPI core to skip this resource */
	return 1;
}

/**
 * acpi_spi_device_alloc - Allocate a spi device, and fill it in with ACPI information
 * @ctlr: controller to which the spi device belongs
 * @adev: ACPI Device for the spi device
 * @index: Index of the spi resource inside the ACPI Node
 *
 * This should be used to allocate a new SPI device from and ACPI Device node.
 * The caller is responsible for calling spi_add_device to register the SPI device.
 *
 * If ctlr is set to NULL, the Controller for the SPI device will be looked up
 * using the resource.
 * If index is set to -1, index is not used.
 * Note: If index is -1, ctlr must be set.
 *
 * Return: a pointer to the new device, or ERR_PTR on error.
 */
struct spi_device *acpi_spi_device_alloc(struct spi_controller *ctlr,
					 struct acpi_device *adev,
					 int index)
{
	acpi_handle parent_handle = NULL;
	struct list_head resource_list;
	struct acpi_spi_lookup lookup = {};
	struct spi_device *spi;
	int ret;

	if (!ctlr && index == -1)
		return ERR_PTR(-EINVAL);

	lookup.ctlr		= ctlr;
	lookup.irq		= -1;
	lookup.index		= index;
	lookup.n		= 0;

	INIT_LIST_HEAD(&resource_list);
	ret = acpi_dev_get_resources(adev, &resource_list,
				     acpi_spi_add_resource, &lookup);
	acpi_dev_free_resource_list(&resource_list);

	if (ret < 0)
		/* Found SPI in _CRS but it points to another controller */
		return ERR_PTR(ret);

	if (!lookup.max_speed_hz &&
	    ACPI_SUCCESS(acpi_get_parent(adev->handle, &parent_handle)) &&
	    ACPI_HANDLE(lookup.ctlr->dev.parent) == parent_handle) {
		/* Apple does not use _CRS but nested devices for SPI slaves */
		acpi_spi_parse_apple_properties(adev, &lookup);
	}

	if (!lookup.max_speed_hz)
		return ERR_PTR(-ENODEV);

	spi = spi_alloc_device(lookup.ctlr);
	if (!spi) {
		dev_err(&lookup.ctlr->dev, "failed to allocate SPI device for %s\n",
			dev_name(&adev->dev));
		return ERR_PTR(-ENOMEM);
	}

	spi_set_all_cs_unused(spi);
	spi_set_chipselect(spi, 0, lookup.chip_select);

	ACPI_COMPANION_SET(&spi->dev, adev);
	spi->max_speed_hz	= lookup.max_speed_hz;
	spi->mode		|= lookup.mode;
	spi->irq		= lookup.irq;
	spi->bits_per_word	= lookup.bits_per_word;
	/*
	 * By default spi->chip_select[0] will hold the physical CS number,
	 * so set bit 0 in spi->cs_index_mask.
	 */
	spi->cs_index_mask	= BIT(0);

	return spi;
}
EXPORT_SYMBOL_GPL(acpi_spi_device_alloc);

static acpi_status acpi_register_spi_device(struct spi_controller *ctlr,
					    struct acpi_device *adev)
{
	struct spi_device *spi;

	if (acpi_bus_get_status(adev) || !adev->status.present ||
	    acpi_device_enumerated(adev))
		return AE_OK;

	spi = acpi_spi_device_alloc(ctlr, adev, -1);
	if (IS_ERR(spi)) {
		if (PTR_ERR(spi) == -ENOMEM)
			return AE_NO_MEMORY;
		else
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
	struct acpi_device *adev = acpi_fetch_acpi_dev(handle);
	struct spi_controller *ctlr = data;

	if (!adev)
		return AE_OK;

	return acpi_register_spi_device(ctlr, adev);
}

#define SPI_ACPI_ENUMERATE_MAX_DEPTH		32

static void acpi_register_spi_devices(struct spi_controller *ctlr)
{
	acpi_status status;
	acpi_handle handle;

	handle = ACPI_HANDLE(ctlr->dev.parent);
	if (!handle)
		return;

	status = acpi_walk_namespace(ACPI_TYPE_DEVICE, ACPI_ROOT_OBJECT,
				     SPI_ACPI_ENUMERATE_MAX_DEPTH,
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

int spi_target_abort(struct spi_device *spi)
{
	struct spi_controller *ctlr = spi->controller;

	if (spi_controller_is_target(ctlr) && ctlr->target_abort)
		return ctlr->target_abort(ctlr);

	return -ENOTSUPP;
}
EXPORT_SYMBOL_GPL(spi_target_abort);

static ssize_t slave_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct spi_controller *ctlr = container_of(dev, struct spi_controller,
						   dev);
	struct device *child;

	child = device_find_any_child(&ctlr->dev);
	return sysfs_emit(buf, "%s\n", child ? to_spi_device(child)->modalias : NULL);
}

static ssize_t slave_store(struct device *dev, struct device_attribute *attr,
			   const char *buf, size_t count)
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

	child = device_find_any_child(&ctlr->dev);
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

		strscpy(spi->modalias, name, sizeof(spi->modalias));

		rc = spi_add_device(spi);
		if (rc) {
			spi_dev_put(spi);
			return rc;
		}
	}

	return count;
}

static DEVICE_ATTR_RW(slave);

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
 *	memory is in the driver_data field of the returned device, accessible
 *	with spi_controller_get_devdata(); the memory is cacheline aligned;
 *	drivers granting DMA access to portions of their private data need to
 *	round up @size using ALIGN(size, dma_get_cache_alignment()).
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
	size_t ctlr_size = ALIGN(sizeof(*ctlr), dma_get_cache_alignment());

	if (!dev)
		return NULL;

	ctlr = kzalloc(size + ctlr_size, GFP_KERNEL);
	if (!ctlr)
		return NULL;

	device_initialize(&ctlr->dev);
	INIT_LIST_HEAD(&ctlr->queue);
	spin_lock_init(&ctlr->queue_lock);
	spin_lock_init(&ctlr->bus_lock_spinlock);
	mutex_init(&ctlr->bus_lock_mutex);
	mutex_init(&ctlr->io_mutex);
	mutex_init(&ctlr->add_lock);
	ctlr->bus_num = -1;
	ctlr->num_chipselect = 1;
	ctlr->slave = slave;
	if (IS_ENABLED(CONFIG_SPI_SLAVE) && slave)
		ctlr->dev.class = &spi_slave_class;
	else
		ctlr->dev.class = &spi_master_class;
	ctlr->dev.parent = dev;
	pm_suspend_ignore_children(&ctlr->dev, true);
	spi_controller_set_devdata(ctlr, (void *)ctlr + ctlr_size);

	return ctlr;
}
EXPORT_SYMBOL_GPL(__spi_alloc_controller);

static void devm_spi_release_controller(struct device *dev, void *ctlr)
{
	spi_controller_put(*(struct spi_controller **)ctlr);
}

/**
 * __devm_spi_alloc_controller - resource-managed __spi_alloc_controller()
 * @dev: physical device of SPI controller
 * @size: how much zeroed driver-private data to allocate
 * @slave: whether to allocate an SPI master (false) or SPI slave (true)
 * Context: can sleep
 *
 * Allocate an SPI controller and automatically release a reference on it
 * when @dev is unbound from its driver.  Drivers are thus relieved from
 * having to call spi_controller_put().
 *
 * The arguments to this function are identical to __spi_alloc_controller().
 *
 * Return: the SPI controller structure on success, else NULL.
 */
struct spi_controller *__devm_spi_alloc_controller(struct device *dev,
						   unsigned int size,
						   bool slave)
{
	struct spi_controller **ptr, *ctlr;

	ptr = devres_alloc(devm_spi_release_controller, sizeof(*ptr),
			   GFP_KERNEL);
	if (!ptr)
		return NULL;

	ctlr = __spi_alloc_controller(dev, size, slave);
	if (ctlr) {
		ctlr->devm_allocated = true;
		*ptr = ctlr;
		devres_add(dev, ptr);
	} else {
		devres_free(ptr);
	}

	return ctlr;
}
EXPORT_SYMBOL_GPL(__devm_spi_alloc_controller);

/**
 * spi_get_gpio_descs() - grab chip select GPIOs for the master
 * @ctlr: The SPI master to grab GPIO descriptors for
 */
static int spi_get_gpio_descs(struct spi_controller *ctlr)
{
	int nb, i;
	struct gpio_desc **cs;
	struct device *dev = &ctlr->dev;
	unsigned long native_cs_mask = 0;
	unsigned int num_cs_gpios = 0;

	nb = gpiod_count(dev, "cs");
	if (nb < 0) {
		/* No GPIOs at all is fine, else return the error */
		if (nb == -ENOENT)
			return 0;
		return nb;
	}

	ctlr->num_chipselect = max_t(int, nb, ctlr->num_chipselect);

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
		if (IS_ERR(cs[i]))
			return PTR_ERR(cs[i]);

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
			num_cs_gpios++;
			continue;
		}

		if (ctlr->max_native_cs && i >= ctlr->max_native_cs) {
			dev_err(dev, "Invalid native chip select %d\n", i);
			return -EINVAL;
		}
		native_cs_mask |= BIT(i);
	}

	ctlr->unused_native_cs = ffs(~native_cs_mask) - 1;

	if ((ctlr->flags & SPI_CONTROLLER_GPIO_SS) && num_cs_gpios &&
	    ctlr->max_native_cs && ctlr->unused_native_cs >= ctlr->max_native_cs) {
		dev_err(dev, "No unused native chip select available\n");
		return -EINVAL;
	}

	return 0;
}

static int spi_controller_check_ops(struct spi_controller *ctlr)
{
	/*
	 * The controller may implement only the high-level SPI-memory like
	 * operations if it does not support regular SPI transfers, and this is
	 * valid use case.
	 * If ->mem_ops or ->mem_ops->exec_op is NULL, we request that at least
	 * one of the ->transfer_xxx() method be implemented.
	 */
	if (!ctlr->mem_ops || !ctlr->mem_ops->exec_op) {
		if (!ctlr->transfer && !ctlr->transfer_one &&
		   !ctlr->transfer_one_message) {
			return -EINVAL;
		}
	}

	return 0;
}

/* Allocate dynamic bus number using Linux idr */
static int spi_controller_id_alloc(struct spi_controller *ctlr, int start, int end)
{
	int id;

	mutex_lock(&board_lock);
	id = idr_alloc(&spi_master_idr, ctlr, start, end, GFP_KERNEL);
	mutex_unlock(&board_lock);
	if (WARN(id < 0, "couldn't get idr"))
		return id == -ENOSPC ? -EBUSY : id;
	ctlr->bus_num = id;
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
	int			first_dynamic;
	int			status;
	int			idx;

	if (!dev)
		return -ENODEV;

	/*
	 * Make sure all necessary hooks are implemented before registering
	 * the SPI controller.
	 */
	status = spi_controller_check_ops(ctlr);
	if (status)
		return status;

	if (ctlr->bus_num < 0)
		ctlr->bus_num = of_alias_get_id(ctlr->dev.of_node, "spi");
	if (ctlr->bus_num >= 0) {
		/* Devices with a fixed bus num must check-in with the num */
		status = spi_controller_id_alloc(ctlr, ctlr->bus_num, ctlr->bus_num + 1);
		if (status)
			return status;
	}
	if (ctlr->bus_num < 0) {
		first_dynamic = of_alias_get_highest_id("spi");
		if (first_dynamic < 0)
			first_dynamic = 0;
		else
			first_dynamic++;

		status = spi_controller_id_alloc(ctlr, first_dynamic, 0);
		if (status)
			return status;
	}
	ctlr->bus_lock_flag = 0;
	init_completion(&ctlr->xfer_completion);
	init_completion(&ctlr->cur_msg_completion);
	if (!ctlr->max_dma_len)
		ctlr->max_dma_len = INT_MAX;

	/*
	 * Register the device, then userspace will see it.
	 * Registration fails if the bus ID is in use.
	 */
	dev_set_name(&ctlr->dev, "spi%u", ctlr->bus_num);

	if (!spi_controller_is_slave(ctlr) && ctlr->use_gpio_descriptors) {
		status = spi_get_gpio_descs(ctlr);
		if (status)
			goto free_bus_id;
		/*
		 * A controller using GPIO descriptors always
		 * supports SPI_CS_HIGH if need be.
		 */
		ctlr->mode_bits |= SPI_CS_HIGH;
	}

	/*
	 * Even if it's just one always-selected device, there must
	 * be at least one chipselect.
	 */
	if (!ctlr->num_chipselect) {
		status = -EINVAL;
		goto free_bus_id;
	}

	/* Setting last_cs to SPI_INVALID_CS means no chip selected */
	for (idx = 0; idx < SPI_CS_CNT_MAX; idx++)
		ctlr->last_cs[idx] = SPI_INVALID_CS;

	status = device_add(&ctlr->dev);
	if (status < 0)
		goto free_bus_id;
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
			goto free_bus_id;
		}
	}
	/* Add statistics */
	ctlr->pcpu_statistics = spi_alloc_pcpu_stats(dev);
	if (!ctlr->pcpu_statistics) {
		dev_err(dev, "Error allocating per-cpu statistics\n");
		status = -ENOMEM;
		goto destroy_queue;
	}

	mutex_lock(&board_lock);
	list_add_tail(&ctlr->list, &spi_controller_list);
	list_for_each_entry(bi, &board_list, list)
		spi_match_controller_to_boardinfo(ctlr, &bi->board_info);
	mutex_unlock(&board_lock);

	/* Register devices from the device tree and ACPI */
	of_register_spi_devices(ctlr);
	acpi_register_spi_devices(ctlr);
	return status;

destroy_queue:
	spi_destroy_queue(ctlr);
free_bus_id:
	mutex_lock(&board_lock);
	idr_remove(&spi_master_idr, ctlr->bus_num);
	mutex_unlock(&board_lock);
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

	/* Prevent addition of new devices, unregister existing ones */
	if (IS_ENABLED(CONFIG_SPI_DYNAMIC))
		mutex_lock(&ctlr->add_lock);

	device_for_each_child(&ctlr->dev, NULL, __unregister);

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

	device_del(&ctlr->dev);

	/* Free bus id */
	mutex_lock(&board_lock);
	if (found == ctlr)
		idr_remove(&spi_master_idr, id);
	mutex_unlock(&board_lock);

	if (IS_ENABLED(CONFIG_SPI_DYNAMIC))
		mutex_unlock(&ctlr->add_lock);

	/*
	 * Release the last reference on the controller if its driver
	 * has not yet been converted to devm_spi_alloc_master/slave().
	 */
	if (!ctlr->devm_allocated)
		put_device(&ctlr->dev);
}
EXPORT_SYMBOL_GPL(spi_unregister_controller);

static inline int __spi_check_suspended(const struct spi_controller *ctlr)
{
	return ctlr->flags & SPI_CONTROLLER_SUSPENDED ? -ESHUTDOWN : 0;
}

static inline void __spi_mark_suspended(struct spi_controller *ctlr)
{
	mutex_lock(&ctlr->bus_lock_mutex);
	ctlr->flags |= SPI_CONTROLLER_SUSPENDED;
	mutex_unlock(&ctlr->bus_lock_mutex);
}

static inline void __spi_mark_resumed(struct spi_controller *ctlr)
{
	mutex_lock(&ctlr->bus_lock_mutex);
	ctlr->flags &= ~SPI_CONTROLLER_SUSPENDED;
	mutex_unlock(&ctlr->bus_lock_mutex);
}

int spi_controller_suspend(struct spi_controller *ctlr)
{
	int ret = 0;

	/* Basically no-ops for non-queued controllers */
	if (ctlr->queued) {
		ret = spi_stop_queue(ctlr);
		if (ret)
			dev_err(&ctlr->dev, "queue stop failed\n");
	}

	__spi_mark_suspended(ctlr);
	return ret;
}
EXPORT_SYMBOL_GPL(spi_controller_suspend);

int spi_controller_resume(struct spi_controller *ctlr)
{
	int ret = 0;

	__spi_mark_resumed(ctlr);

	if (ctlr->queued) {
		ret = spi_start_queue(ctlr);
		if (ret)
			dev_err(&ctlr->dev, "queue restart failed\n");
	}
	return ret;
}
EXPORT_SYMBOL_GPL(spi_controller_resume);

/*-------------------------------------------------------------------------*/

/* Core methods for spi_message alterations */

static void __spi_replace_transfers_release(struct spi_controller *ctlr,
					    struct spi_message *msg,
					    void *res)
{
	struct spi_replaced_transfers *rxfer = res;
	size_t i;

	/* Call extra callback if requested */
	if (rxfer->release)
		rxfer->release(ctlr, msg, res);

	/* Insert replaced transfers back into the message */
	list_splice(&rxfer->replaced_transfers, rxfer->replaced_after);

	/* Remove the formerly inserted entries */
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
static struct spi_replaced_transfers *spi_replace_transfers(
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

	/* Allocate the structure using spi_res */
	rxfer = spi_res_alloc(msg->spi, __spi_replace_transfers_release,
			      struct_size(rxfer, inserted_transfers, insert)
			      + extradatasize,
			      gfp);
	if (!rxfer)
		return ERR_PTR(-ENOMEM);

	/* The release code to invoke before running the generic release */
	rxfer->release = release;

	/* Assign extradata */
	if (extradatasize)
		rxfer->extradata =
			&rxfer->inserted_transfers[insert];

	/* Init the replaced_transfers list */
	INIT_LIST_HEAD(&rxfer->replaced_transfers);

	/*
	 * Assign the list_entry after which we should reinsert
	 * the @replaced_transfers - it may be spi_message.messages!
	 */
	rxfer->replaced_after = xfer_first->transfer_list.prev;

	/* Remove the requested number of transfers */
	for (i = 0; i < remove; i++) {
		/*
		 * If the entry after replaced_after it is msg->transfers
		 * then we have been requested to remove more transfers
		 * than are in the list.
		 */
		if (rxfer->replaced_after->next == &msg->transfers) {
			dev_err(&msg->spi->dev,
				"requested to remove more spi_transfers than are available\n");
			/* Insert replaced transfers back into the message */
			list_splice(&rxfer->replaced_transfers,
				    rxfer->replaced_after);

			/* Free the spi_replace_transfer structure... */
			spi_res_free(rxfer);

			/* ...and return with an error */
			return ERR_PTR(-EINVAL);
		}

		/*
		 * Remove the entry after replaced_after from list of
		 * transfers and add it to list of replaced_transfers.
		 */
		list_move_tail(rxfer->replaced_after->next,
			       &rxfer->replaced_transfers);
	}

	/*
	 * Create copy of the given xfer with identical settings
	 * based on the first transfer to get removed.
	 */
	for (i = 0; i < insert; i++) {
		/* We need to run in reverse order */
		xfer = &rxfer->inserted_transfers[insert - 1 - i];

		/* Copy all spi_transfer data */
		memcpy(xfer, xfer_first, sizeof(*xfer));

		/* Add to list */
		list_add(&xfer->transfer_list, rxfer->replaced_after);

		/* Clear cs_change and delay for all but the last */
		if (i) {
			xfer->cs_change = false;
			xfer->delay.value = 0;
		}
	}

	/* Set up inserted... */
	rxfer->inserted = insert;

	/* ...and register it with spi_res/spi_message */
	spi_res_add(msg, rxfer);

	return rxfer;
}

static int __spi_split_transfer_maxsize(struct spi_controller *ctlr,
					struct spi_message *msg,
					struct spi_transfer **xferp,
					size_t maxsize)
{
	struct spi_transfer *xfer = *xferp, *xfers;
	struct spi_replaced_transfers *srt;
	size_t offset;
	size_t count, i;

	/* Calculate how many we have to replace */
	count = DIV_ROUND_UP(xfer->len, maxsize);

	/* Create replacement */
	srt = spi_replace_transfers(msg, xfer, 1, count, NULL, 0, GFP_KERNEL);
	if (IS_ERR(srt))
		return PTR_ERR(srt);
	xfers = srt->inserted_transfers;

	/*
	 * Now handle each of those newly inserted spi_transfers.
	 * Note that the replacements spi_transfers all are preset
	 * to the same values as *xferp, so tx_buf, rx_buf and len
	 * are all identical (as well as most others)
	 * so we just have to fix up len and the pointers.
	 *
	 * This also includes support for the depreciated
	 * spi_message.is_dma_mapped interface.
	 */

	/*
	 * The first transfer just needs the length modified, so we
	 * run it outside the loop.
	 */
	xfers[0].len = min_t(size_t, maxsize, xfer[0].len);

	/* All the others need rx_buf/tx_buf also set */
	for (i = 1, offset = maxsize; i < count; offset += maxsize, i++) {
		/* Update rx_buf, tx_buf and DMA */
		if (xfers[i].rx_buf)
			xfers[i].rx_buf += offset;
		if (xfers[i].rx_dma)
			xfers[i].rx_dma += offset;
		if (xfers[i].tx_buf)
			xfers[i].tx_buf += offset;
		if (xfers[i].tx_dma)
			xfers[i].tx_dma += offset;

		/* Update length */
		xfers[i].len = min(maxsize, xfers[i].len - offset);
	}

	/*
	 * We set up xferp to the last entry we have inserted,
	 * so that we skip those already split transfers.
	 */
	*xferp = &xfers[count - 1];

	/* Increment statistics counters */
	SPI_STATISTICS_INCREMENT_FIELD(ctlr->pcpu_statistics,
				       transfers_split_maxsize);
	SPI_STATISTICS_INCREMENT_FIELD(msg->spi->pcpu_statistics,
				       transfers_split_maxsize);

	return 0;
}

/**
 * spi_split_transfers_maxsize - split spi transfers into multiple transfers
 *                               when an individual transfer exceeds a
 *                               certain size
 * @ctlr:    the @spi_controller for this transfer
 * @msg:   the @spi_message to transform
 * @maxsize:  the maximum when to apply this
 *
 * This function allocates resources that are automatically freed during the
 * spi message unoptimize phase so this function should only be called from
 * optimize_message callbacks.
 *
 * Return: status of transformation
 */
int spi_split_transfers_maxsize(struct spi_controller *ctlr,
				struct spi_message *msg,
				size_t maxsize)
{
	struct spi_transfer *xfer;
	int ret;

	/*
	 * Iterate over the transfer_list,
	 * but note that xfer is advanced to the last transfer inserted
	 * to avoid checking sizes again unnecessarily (also xfer does
	 * potentially belong to a different list by the time the
	 * replacement has happened).
	 */
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		if (xfer->len > maxsize) {
			ret = __spi_split_transfer_maxsize(ctlr, msg, &xfer,
							   maxsize);
			if (ret)
				return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(spi_split_transfers_maxsize);


/**
 * spi_split_transfers_maxwords - split SPI transfers into multiple transfers
 *                                when an individual transfer exceeds a
 *                                certain number of SPI words
 * @ctlr:     the @spi_controller for this transfer
 * @msg:      the @spi_message to transform
 * @maxwords: the number of words to limit each transfer to
 *
 * This function allocates resources that are automatically freed during the
 * spi message unoptimize phase so this function should only be called from
 * optimize_message callbacks.
 *
 * Return: status of transformation
 */
int spi_split_transfers_maxwords(struct spi_controller *ctlr,
				 struct spi_message *msg,
				 size_t maxwords)
{
	struct spi_transfer *xfer;

	/*
	 * Iterate over the transfer_list,
	 * but note that xfer is advanced to the last transfer inserted
	 * to avoid checking sizes again unnecessarily (also xfer does
	 * potentially belong to a different list by the time the
	 * replacement has happened).
	 */
	list_for_each_entry(xfer, &msg->transfers, transfer_list) {
		size_t maxsize;
		int ret;

		maxsize = maxwords * roundup_pow_of_two(BITS_TO_BYTES(xfer->bits_per_word));
		if (xfer->len > maxsize) {
			ret = __spi_split_transfer_maxsize(ctlr, msg, &xfer,
							   maxsize);
			if (ret)
				return ret;
		}
	}

	return 0;
}
EXPORT_SYMBOL_GPL(spi_split_transfers_maxwords);

/*-------------------------------------------------------------------------*/

/*
 * Core methods for SPI controller protocol drivers. Some of the
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
 * spi_set_cs_timing - configure CS setup, hold, and inactive delays
 * @spi: the device that requires specific CS timing configuration
 *
 * Return: zero on success, else a negative error code.
 */
static int spi_set_cs_timing(struct spi_device *spi)
{
	struct device *parent = spi->controller->dev.parent;
	int status = 0;

	if (spi->controller->set_cs_timing && !spi_get_csgpiod(spi, 0)) {
		if (spi->controller->auto_runtime_pm) {
			status = pm_runtime_get_sync(parent);
			if (status < 0) {
				pm_runtime_put_noidle(parent);
				dev_err(&spi->controller->dev, "Failed to power device: %d\n",
					status);
				return status;
			}

			status = spi->controller->set_cs_timing(spi);
			pm_runtime_mark_last_busy(parent);
			pm_runtime_put_autosuspend(parent);
		} else {
			status = spi->controller->set_cs_timing(spi);
		}
	}
	return status;
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
 * or from it.  When this function returns, the SPI device is deselected.
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
	int		status = 0;

	/*
	 * Check mode to prevent that any two of DUAL, QUAD and NO_MOSI/MISO
	 * are set at the same time.
	 */
	if ((hweight_long(spi->mode &
		(SPI_TX_DUAL | SPI_TX_QUAD | SPI_NO_TX)) > 1) ||
	    (hweight_long(spi->mode &
		(SPI_RX_DUAL | SPI_RX_QUAD | SPI_NO_RX)) > 1)) {
		dev_err(&spi->dev,
		"setup: can not select any two of dual, quad and no-rx/tx at the same time\n");
		return -EINVAL;
	}
	/* If it is SPI_3WIRE mode, DUAL and QUAD should be forbidden */
	if ((spi->mode & SPI_3WIRE) && (spi->mode &
		(SPI_TX_DUAL | SPI_TX_QUAD | SPI_TX_OCTAL |
		 SPI_RX_DUAL | SPI_RX_QUAD | SPI_RX_OCTAL)))
		return -EINVAL;
	/*
	 * Help drivers fail *cleanly* when they need options
	 * that aren't supported with their current controller.
	 * SPI_CS_WORD has a fallback software implementation,
	 * so it is ignored here.
	 */
	bad_bits = spi->mode & ~(spi->controller->mode_bits | SPI_CS_WORD |
				 SPI_NO_TX | SPI_NO_RX);
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

	if (!spi->bits_per_word) {
		spi->bits_per_word = 8;
	} else {
		/*
		 * Some controllers may not support the default 8 bits-per-word
		 * so only perform the check when this is explicitly provided.
		 */
		status = __spi_validate_bits_per_word(spi->controller,
						      spi->bits_per_word);
		if (status)
			return status;
	}

	if (spi->controller->max_speed_hz &&
	    (!spi->max_speed_hz ||
	     spi->max_speed_hz > spi->controller->max_speed_hz))
		spi->max_speed_hz = spi->controller->max_speed_hz;

	mutex_lock(&spi->controller->io_mutex);

	if (spi->controller->setup) {
		status = spi->controller->setup(spi);
		if (status) {
			mutex_unlock(&spi->controller->io_mutex);
			dev_err(&spi->controller->dev, "Failed to setup device: %d\n",
				status);
			return status;
		}
	}

	status = spi_set_cs_timing(spi);
	if (status) {
		mutex_unlock(&spi->controller->io_mutex);
		return status;
	}

	if (spi->controller->auto_runtime_pm && spi->controller->set_cs) {
		status = pm_runtime_resume_and_get(spi->controller->dev.parent);
		if (status < 0) {
			mutex_unlock(&spi->controller->io_mutex);
			dev_err(&spi->controller->dev, "Failed to power device: %d\n",
				status);
			return status;
		}

		/*
		 * We do not want to return positive value from pm_runtime_get,
		 * there are many instances of devices calling spi_setup() and
		 * checking for a non-zero return value instead of a negative
		 * return value.
		 */
		status = 0;

		spi_set_cs(spi, false, true);
		pm_runtime_mark_last_busy(spi->controller->dev.parent);
		pm_runtime_put_autosuspend(spi->controller->dev.parent);
	} else {
		spi_set_cs(spi, false, true);
	}

	mutex_unlock(&spi->controller->io_mutex);

	if (spi->rt && !spi->controller->rt) {
		spi->controller->rt = true;
		spi_set_thread_rt(spi->controller);
	}

	trace_spi_setup(spi, status);

	dev_dbg(&spi->dev, "setup mode %lu, %s%s%s%s%u bits/w, %u Hz max --> %d\n",
			spi->mode & SPI_MODE_X_MASK,
			(spi->mode & SPI_CS_HIGH) ? "cs_high, " : "",
			(spi->mode & SPI_LSB_FIRST) ? "lsb, " : "",
			(spi->mode & SPI_3WIRE) ? "3wire, " : "",
			(spi->mode & SPI_LOOP) ? "loopback, " : "",
			spi->bits_per_word, spi->max_speed_hz,
			status);

	return status;
}
EXPORT_SYMBOL_GPL(spi_setup);

static int _spi_xfer_word_delay_update(struct spi_transfer *xfer,
				       struct spi_device *spi)
{
	int delay1, delay2;

	delay1 = spi_delay_to_ns(&xfer->word_delay, xfer);
	if (delay1 < 0)
		return delay1;

	delay2 = spi_delay_to_ns(&spi->word_delay, xfer);
	if (delay2 < 0)
		return delay2;

	if (delay1 < delay2)
		memcpy(&xfer->word_delay, &spi->word_delay,
		       sizeof(xfer->word_delay));

	return 0;
}

static int __spi_validate(struct spi_device *spi, struct spi_message *message)
{
	struct spi_controller *ctlr = spi->controller;
	struct spi_transfer *xfer;
	int w_size;

	if (list_empty(&message->transfers))
		return -EINVAL;

	message->spi = spi;

	/*
	 * Half-duplex links include original MicroWire, and ones with
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

	/*
	 * Set transfer bits_per_word and max speed as spi device default if
	 * it is not set for this transfer.
	 * Set transfer tx_nbits and rx_nbits as single transfer default
	 * (SPI_NBITS_SINGLE) if it is not set for this transfer.
	 * Ensure transfer word_delay is at least as long as that required by
	 * device itself.
	 */
	message->frame_length = 0;
	list_for_each_entry(xfer, &message->transfers, transfer_list) {
		xfer->effective_speed_hz = 0;
		message->frame_length += xfer->len;
		if (!xfer->bits_per_word)
			xfer->bits_per_word = spi->bits_per_word;

		if (!xfer->speed_hz)
			xfer->speed_hz = spi->max_speed_hz;

		if (ctlr->max_speed_hz && xfer->speed_hz > ctlr->max_speed_hz)
			xfer->speed_hz = ctlr->max_speed_hz;

		if (__spi_validate_bits_per_word(ctlr, xfer->bits_per_word))
			return -EINVAL;

		/*
		 * SPI transfer length should be multiple of SPI word size
		 * where SPI word size should be power-of-two multiple.
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
		/*
		 * Check transfer tx/rx_nbits:
		 * 1. check the value matches one of single, dual and quad
		 * 2. check tx/rx_nbits match the mode in spi_device
		 */
		if (xfer->tx_buf) {
			if (spi->mode & SPI_NO_TX)
				return -EINVAL;
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
		/* Check transfer rx_nbits */
		if (xfer->rx_buf) {
			if (spi->mode & SPI_NO_RX)
				return -EINVAL;
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

		if (_spi_xfer_word_delay_update(xfer, spi))
			return -EINVAL;
	}

	message->status = -EINPROGRESS;

	return 0;
}

/*
 * spi_split_transfers - generic handling of transfer splitting
 * @msg: the message to split
 *
 * Under certain conditions, a SPI controller may not support arbitrary
 * transfer sizes or other features required by a peripheral. This function
 * will split the transfers in the message into smaller transfers that are
 * supported by the controller.
 *
 * Controllers with special requirements not covered here can also split
 * transfers in the optimize_message() callback.
 *
 * Context: can sleep
 * Return: zero on success, else a negative error code
 */
static int spi_split_transfers(struct spi_message *msg)
{
	struct spi_controller *ctlr = msg->spi->controller;
	struct spi_transfer *xfer;
	int ret;

	/*
	 * If an SPI controller does not support toggling the CS line on each
	 * transfer (indicated by the SPI_CS_WORD flag) or we are using a GPIO
	 * for the CS line, we can emulate the CS-per-word hardware function by
	 * splitting transfers into one-word transfers and ensuring that
	 * cs_change is set for each transfer.
	 */
	if ((msg->spi->mode & SPI_CS_WORD) &&
	    (!(ctlr->mode_bits & SPI_CS_WORD) || spi_is_csgpiod(msg->spi))) {
		ret = spi_split_transfers_maxwords(ctlr, msg, 1);
		if (ret)
			return ret;

		list_for_each_entry(xfer, &msg->transfers, transfer_list) {
			/* Don't change cs_change on the last entry in the list */
			if (list_is_last(&xfer->transfer_list, &msg->transfers))
				break;

			xfer->cs_change = 1;
		}
	} else {
		ret = spi_split_transfers_maxsize(ctlr, msg,
						  spi_max_transfer_size(msg->spi));
		if (ret)
			return ret;
	}

	return 0;
}

/*
 * __spi_optimize_message - shared implementation for spi_optimize_message()
 *                          and spi_maybe_optimize_message()
 * @spi: the device that will be used for the message
 * @msg: the message to optimize
 *
 * Peripheral drivers will call spi_optimize_message() and the spi core will
 * call spi_maybe_optimize_message() instead of calling this directly.
 *
 * It is not valid to call this on a message that has already been optimized.
 *
 * Return: zero on success, else a negative error code
 */
static int __spi_optimize_message(struct spi_device *spi,
				  struct spi_message *msg)
{
	struct spi_controller *ctlr = spi->controller;
	int ret;

	ret = __spi_validate(spi, msg);
	if (ret)
		return ret;

	ret = spi_split_transfers(msg);
	if (ret)
		return ret;

	if (ctlr->optimize_message) {
		ret = ctlr->optimize_message(msg);
		if (ret) {
			spi_res_release(ctlr, msg);
			return ret;
		}
	}

	msg->optimized = true;

	return 0;
}

/*
 * spi_maybe_optimize_message - optimize message if it isn't already pre-optimized
 * @spi: the device that will be used for the message
 * @msg: the message to optimize
 * Return: zero on success, else a negative error code
 */
static int spi_maybe_optimize_message(struct spi_device *spi,
				      struct spi_message *msg)
{
	if (msg->pre_optimized)
		return 0;

	return __spi_optimize_message(spi, msg);
}

/**
 * spi_optimize_message - do any one-time validation and setup for a SPI message
 * @spi: the device that will be used for the message
 * @msg: the message to optimize
 *
 * Peripheral drivers that reuse the same message repeatedly may call this to
 * perform as much message prep as possible once, rather than repeating it each
 * time a message transfer is performed to improve throughput and reduce CPU
 * usage.
 *
 * Once a message has been optimized, it cannot be modified with the exception
 * of updating the contents of any xfer->tx_buf (the pointer can't be changed,
 * only the data in the memory it points to).
 *
 * Calls to this function must be balanced with calls to spi_unoptimize_message()
 * to avoid leaking resources.
 *
 * Context: can sleep
 * Return: zero on success, else a negative error code
 */
int spi_optimize_message(struct spi_device *spi, struct spi_message *msg)
{
	int ret;

	ret = __spi_optimize_message(spi, msg);
	if (ret)
		return ret;

	/*
	 * This flag indicates that the peripheral driver called spi_optimize_message()
	 * and therefore we shouldn't unoptimize message automatically when finalizing
	 * the message but rather wait until spi_unoptimize_message() is called
	 * by the peripheral driver.
	 */
	msg->pre_optimized = true;

	return 0;
}
EXPORT_SYMBOL_GPL(spi_optimize_message);

/**
 * spi_unoptimize_message - releases any resources allocated by spi_optimize_message()
 * @msg: the message to unoptimize
 *
 * Calls to this function must be balanced with calls to spi_optimize_message().
 *
 * Context: can sleep
 */
void spi_unoptimize_message(struct spi_message *msg)
{
	__spi_unoptimize_message(msg);
	msg->pre_optimized = false;
}
EXPORT_SYMBOL_GPL(spi_unoptimize_message);

static int __spi_async(struct spi_device *spi, struct spi_message *message)
{
	struct spi_controller *ctlr = spi->controller;
	struct spi_transfer *xfer;

	/*
	 * Some controllers do not support doing regular SPI transfers. Return
	 * ENOTSUPP when this is the case.
	 */
	if (!ctlr->transfer)
		return -ENOTSUPP;

	SPI_STATISTICS_INCREMENT_FIELD(ctlr->pcpu_statistics, spi_async);
	SPI_STATISTICS_INCREMENT_FIELD(spi->pcpu_statistics, spi_async);

	trace_spi_message_submit(message);

	if (!ctlr->ptp_sts_supported) {
		list_for_each_entry(xfer, &message->transfers, transfer_list) {
			xfer->ptp_sts_word_pre = 0;
			ptp_read_system_prets(xfer->ptp_sts);
		}
	}

	return ctlr->transfer(spi, message);
}

/**
 * spi_async - asynchronous SPI transfer
 * @spi: device with which data will be exchanged
 * @message: describes the data transfers, including completion callback
 * Context: any (IRQs may be blocked, etc)
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

	ret = spi_maybe_optimize_message(spi, message);
	if (ret)
		return ret;

	spin_lock_irqsave(&ctlr->bus_lock_spinlock, flags);

	if (ctlr->bus_lock_flag)
		ret = -EBUSY;
	else
		ret = __spi_async(spi, message);

	spin_unlock_irqrestore(&ctlr->bus_lock_spinlock, flags);

	spi_maybe_unoptimize_message(message);

	return ret;
}
EXPORT_SYMBOL_GPL(spi_async);

static void __spi_transfer_message_noqueue(struct spi_controller *ctlr, struct spi_message *msg)
{
	bool was_busy;
	int ret;

	mutex_lock(&ctlr->io_mutex);

	was_busy = ctlr->busy;

	ctlr->cur_msg = msg;
	ret = __spi_pump_transfer_message(ctlr, msg, was_busy);
	if (ret)
		dev_err(&ctlr->dev, "noqueue transfer failed\n");
	ctlr->cur_msg = NULL;
	ctlr->fallback = false;

	if (!was_busy) {
		kfree(ctlr->dummy_rx);
		ctlr->dummy_rx = NULL;
		kfree(ctlr->dummy_tx);
		ctlr->dummy_tx = NULL;
		if (ctlr->unprepare_transfer_hardware &&
		    ctlr->unprepare_transfer_hardware(ctlr))
			dev_err(&ctlr->dev,
				"failed to unprepare transfer hardware\n");
		spi_idle_runtime_pm(ctlr);
	}

	mutex_unlock(&ctlr->io_mutex);
}

/*-------------------------------------------------------------------------*/

/*
 * Utility methods for SPI protocol drivers, layered on
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
	unsigned long flags;
	int status;
	struct spi_controller *ctlr = spi->controller;

	if (__spi_check_suspended(ctlr)) {
		dev_warn_once(&spi->dev, "Attempted to sync while suspend\n");
		return -ESHUTDOWN;
	}

	status = spi_maybe_optimize_message(spi, message);
	if (status)
		return status;

	SPI_STATISTICS_INCREMENT_FIELD(ctlr->pcpu_statistics, spi_sync);
	SPI_STATISTICS_INCREMENT_FIELD(spi->pcpu_statistics, spi_sync);

	/*
	 * Checking queue_empty here only guarantees async/sync message
	 * ordering when coming from the same context. It does not need to
	 * guard against reentrancy from a different context. The io_mutex
	 * will catch those cases.
	 */
	if (READ_ONCE(ctlr->queue_empty) && !ctlr->must_async) {
		message->actual_length = 0;
		message->status = -EINPROGRESS;

		trace_spi_message_submit(message);

		SPI_STATISTICS_INCREMENT_FIELD(ctlr->pcpu_statistics, spi_sync_immediate);
		SPI_STATISTICS_INCREMENT_FIELD(spi->pcpu_statistics, spi_sync_immediate);

		__spi_transfer_message_noqueue(ctlr, message);

		return message->status;
	}

	/*
	 * There are messages in the async queue that could have originated
	 * from the same context, so we need to preserve ordering.
	 * Therefor we send the message to the async queue and wait until they
	 * are completed.
	 */
	message->complete = spi_complete;
	message->context = &done;

	spin_lock_irqsave(&ctlr->bus_lock_spinlock, flags);
	status = __spi_async(spi, message);
	spin_unlock_irqrestore(&ctlr->bus_lock_spinlock, flags);

	if (status == 0) {
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

	/* Mutex remains locked until spi_bus_unlock() is called */

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

/* Portable code must never pass more than 32 bytes */
#define	SPI_BUFSIZ	max(32, SMP_CACHE_BYTES)

static u8	*buf;

/**
 * spi_write_then_read - SPI synchronous write followed by read
 * @spi: device with which data will be exchanged
 * @txbuf: data to be written (need not be DMA-safe)
 * @n_tx: size of txbuf, in bytes
 * @rxbuf: buffer into which data will be read (need not be DMA-safe)
 * @n_rx: size of rxbuf, in bytes
 * Context: can sleep
 *
 * This performs a half duplex MicroWire style transaction with the
 * device, sending txbuf and then reading rxbuf.  The return value
 * is zero for success, else a negative errno status code.
 * This call may only be used from a context that may sleep.
 *
 * Parameters to this routine are always copied using a small buffer.
 * Performance-sensitive or bulk transfer code should instead use
 * spi_{async,sync}() calls with DMA-safe buffers.
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

	/*
	 * Use preallocated DMA-safe buffer if we can. We can't avoid
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

	/* Do the I/O */
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

#if IS_ENABLED(CONFIG_OF_DYNAMIC)
/* Must call put_device() when done with returned spi_device device */
static struct spi_device *of_find_spi_device_by_node(struct device_node *node)
{
	struct device *dev = bus_find_device_by_of_node(&spi_bus_type, node);

	return dev ? to_spi_device(dev) : NULL;
}

/* The spi controllers are not using spi_bus, so we find it with another way */
static struct spi_controller *of_find_spi_controller_by_node(struct device_node *node)
{
	struct device *dev;

	dev = class_find_device_by_of_node(&spi_master_class, node);
	if (!dev && IS_ENABLED(CONFIG_SPI_SLAVE))
		dev = class_find_device_by_of_node(&spi_slave_class, node);
	if (!dev)
		return NULL;

	/* Reference got in class_find_device */
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
			return NOTIFY_OK;	/* Not for us */

		if (of_node_test_and_set_flag(rd->dn, OF_POPULATED)) {
			put_device(&ctlr->dev);
			return NOTIFY_OK;
		}

		/*
		 * Clear the flag before adding the device so that fw_devlink
		 * doesn't skip adding consumers to this device.
		 */
		rd->dn->fwnode.flags &= ~FWNODE_FLAG_NOT_DEVICE;
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
		/* Already depopulated? */
		if (!of_node_check_flag(rd->dn, OF_POPULATED))
			return NOTIFY_OK;

		/* Find our device by node */
		spi = of_find_spi_device_by_node(rd->dn);
		if (spi == NULL)
			return NOTIFY_OK;	/* No? not meant for us */

		/* Unregister takes one ref away */
		spi_unregister_device(spi);

		/* And put the reference of the find */
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

struct spi_controller *acpi_spi_find_controller_by_adev(struct acpi_device *adev)
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
EXPORT_SYMBOL_GPL(acpi_spi_find_controller_by_adev);

static struct spi_device *acpi_spi_find_device_by_adev(struct acpi_device *adev)
{
	struct device *dev;

	dev = bus_find_device_by_acpi_dev(&spi_bus_type, adev);
	return to_spi_device(dev);
}

static int acpi_spi_notify(struct notifier_block *nb, unsigned long value,
			   void *arg)
{
	struct acpi_device *adev = arg;
	struct spi_controller *ctlr;
	struct spi_device *spi;

	switch (value) {
	case ACPI_RECONFIG_DEVICE_ADD:
		ctlr = acpi_spi_find_controller_by_adev(acpi_dev_parent(adev));
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

/*
 * A board_info is normally registered in arch_initcall(),
 * but even essential drivers wait till later.
 *
 * REVISIT only boardinfo really needs static linking. The rest (device and
 * driver registration) _could_ be dynamically linked (modular) ... Costs
 * include needing to have boardinfo data structures be much more public.
 */
postcore_initcall(spi_init);
