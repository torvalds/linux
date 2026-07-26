// SPDX-License-Identifier: GPL-2.0
/*
 * Hardware monitoring (hwmon) support for the LSI / Broadcom mpt3sas
 * SAS HBA driver. Exposes the IOC and board temperature sensors by
 * reading MPI IO Unit Page 7.
 */

#include <linux/err.h>
#include <linux/hwmon.h>
#include <linux/slab.h>

#include "mpt3sas_base.h"

struct mpt3sas_hwmon {
	struct MPT3SAS_ADAPTER *ioc;
	struct device *hwmon_dev;
	bool ioc_present;
	bool board_present;
};

/*
 * Convert a (raw, units) reading to millidegrees Celsius.
 * Returns -ENODATA when the sensor reports "not present" or
 * unknown units. Temperature values are interpreted as signed
 * two's-complement integers.
 *
 * The MPI2_IOUNITPAGE7_IOC_TEMP_* and MPI2_IOUNITPAGE7_BOARD_TEMP_*
 * defines in mpi2_cnfg.h share the same values; the IOC ones are
 * used for both channels.
 */
static int _hwmon_to_mdegc(s16 raw, u8 units, long *out)
{
	switch (units) {
	case MPI2_IOUNITPAGE7_IOC_TEMP_CELSIUS:
		*out = (long)raw * 1000;
		return 0;
	case MPI2_IOUNITPAGE7_IOC_TEMP_FAHRENHEIT:
		/* (F - 32) * 5 / 9, expressed in milli-units */
		*out = ((long)raw - 32) * 5000 / 9;
		return 0;
	default:
		return -ENODATA;
	}
}

static umode_t _hwmon_is_visible(const void *drvdata,
				 enum hwmon_sensor_types type,
				 u32 attr, int channel)
{
	const struct mpt3sas_hwmon *h = drvdata;

	if (type != hwmon_temp)
		return 0;
	if (attr != hwmon_temp_input && attr != hwmon_temp_label)
		return 0;
	if (channel == 0 && h->ioc_present)
		return 0444;
	if (channel == 1 && h->board_present)
		return 0444;
	return 0;
}

static int _hwmon_read(struct device *dev, enum hwmon_sensor_types type,
		       u32 attr, int channel, long *val)
{
	struct mpt3sas_hwmon *h = dev_get_drvdata(dev);
	Mpi2ConfigReply_t mpi_reply;
	Mpi2IOUnitPage7_t page;
	int r;

	if (type != hwmon_temp || attr != hwmon_temp_input)
		return -EOPNOTSUPP;

	r = mpt3sas_config_get_iounit_pg7(h->ioc, &mpi_reply, &page);
	if (r)
		return r;

	if (channel == 0)
		return _hwmon_to_mdegc((s16)le16_to_cpu(page.IOCTemperature),
				       page.IOCTemperatureUnits, val);
	if (channel == 1)
		return _hwmon_to_mdegc((s16)le16_to_cpu(page.BoardTemperature),
				       page.BoardTemperatureUnits, val);
	return -EOPNOTSUPP;
}

static const char * const mpt3sas_hwmon_temp_labels[] = {
	"IOC",
	"Board",
};

static int _hwmon_read_string(struct device *dev,
			      enum hwmon_sensor_types type,
			      u32 attr, int channel, const char **str)
{
	if (type != hwmon_temp || attr != hwmon_temp_label)
		return -EOPNOTSUPP;
	*str = mpt3sas_hwmon_temp_labels[channel];
	return 0;
}

static const struct hwmon_channel_info * const mpt3sas_hwmon_info[] = {
	HWMON_CHANNEL_INFO(temp,
			   HWMON_T_INPUT | HWMON_T_LABEL,
			   HWMON_T_INPUT | HWMON_T_LABEL),
	NULL,
};

static const struct hwmon_ops mpt3sas_hwmon_ops = {
	.is_visible	= _hwmon_is_visible,
	.read		= _hwmon_read,
	.read_string	= _hwmon_read_string,
};

static const struct hwmon_chip_info mpt3sas_hwmon_chip_info = {
	.ops	= &mpt3sas_hwmon_ops,
	.info	= mpt3sas_hwmon_info,
};

/**
 * mpt3sas_hwmon_register - register an hwmon device for the IOC
 * @ioc: per adapter object
 * Context: sleep.
 *
 * Succeeds without registering when no temperature sensors are present,
 * so cards without thermal monitoring do not expose an empty hwmon node.
 * Paired with mpt3sas_hwmon_unregister() from the driver's remove path.
 *
 * Return: 0 for success, non-zero for failure.
 */
int mpt3sas_hwmon_register(struct MPT3SAS_ADAPTER *ioc)
{
	struct device *parent = &ioc->pdev->dev;
	struct mpt3sas_hwmon *h;
	struct device *hwdev;
	Mpi2ConfigReply_t mpi_reply;
	Mpi2IOUnitPage7_t page;
	int r;

	r = mpt3sas_config_get_iounit_pg7(ioc, &mpi_reply, &page);
	if (r)
		return r;

	/*
	 * A page where both *TemperatureUnits are NOT_PRESENT covers
	 * two cases: cards that genuinely lack sensors, and firmware
	 * errors that left the page zero-filled (the accessor mirrors
	 * _config_request() behaviour). Either way: skip registration.
	 */
	if (page.IOCTemperatureUnits == MPI2_IOUNITPAGE7_IOC_TEMP_NOT_PRESENT &&
	    page.BoardTemperatureUnits == MPI2_IOUNITPAGE7_BOARD_TEMP_NOT_PRESENT)
		return 0;

	h = kzalloc_obj(*h);
	if (!h)
		return -ENOMEM;

	h->ioc = ioc;
	h->ioc_present = page.IOCTemperatureUnits != MPI2_IOUNITPAGE7_IOC_TEMP_NOT_PRESENT;
	h->board_present = page.BoardTemperatureUnits != MPI2_IOUNITPAGE7_BOARD_TEMP_NOT_PRESENT;

	hwdev = hwmon_device_register_with_info(parent, "mpt3sas", h,
						&mpt3sas_hwmon_chip_info,
						NULL);
	if (IS_ERR(hwdev)) {
		kfree(h);
		return PTR_ERR(hwdev);
	}

	h->hwmon_dev = hwdev;
	ioc->hwmon = h;
	return 0;
}

/**
 * mpt3sas_hwmon_unregister - tear down the hwmon device, if any
 * @ioc: per adapter object
 *
 * Safe to call when registration was skipped (no sensors) or
 * failed; in those cases ioc->hwmon is NULL and this is a no-op.
 */
void mpt3sas_hwmon_unregister(struct MPT3SAS_ADAPTER *ioc)
{
	struct mpt3sas_hwmon *h = ioc->hwmon;

	if (!h)
		return;
	hwmon_device_unregister(h->hwmon_dev);
	kfree(h);
	ioc->hwmon = NULL;
}
