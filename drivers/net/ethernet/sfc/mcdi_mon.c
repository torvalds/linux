/****************************************************************************
 * Driver for Solarflare Solarstorm network controllers and boards
 * Copyright 2011 Solarflare Communications Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#include <linux/bitops.h>
#include <linux/slab.h>
#include <linux/hwmon.h>
#include <linux/stat.h>

#include "net_driver.h"
#include "mcdi.h"
#include "mcdi_pcol.h"
#include "nic.h"

enum efx_hwmon_type {
	EFX_HWMON_UNKNOWN,
	EFX_HWMON_TEMP,         /* temperature */
	EFX_HWMON_COOL,         /* cooling device, probably a heatsink */
	EFX_HWMON_IN            /* input voltage */
};

static const struct {
	const char *label;
	enum efx_hwmon_type hwmon_type;
	int port;
} efx_mcdi_sensor_type[MC_CMD_SENSOR_ENTRY_MAXNUM] = {
#define SENSOR(name, label, hwmon_type, port)			\
	[MC_CMD_SENSOR_##name] = { label, hwmon_type, port }
	SENSOR(CONTROLLER_TEMP,	   "Controller temp.",	   EFX_HWMON_TEMP, -1),
	SENSOR(PHY_COMMON_TEMP,	   "PHY temp.",		   EFX_HWMON_TEMP, -1),
	SENSOR(CONTROLLER_COOLING, "Controller cooling",   EFX_HWMON_COOL, -1),
	SENSOR(PHY0_TEMP,	   "PHY temp.",		   EFX_HWMON_TEMP, 0),
	SENSOR(PHY0_COOLING,	   "PHY cooling",	   EFX_HWMON_COOL, 0),
	SENSOR(PHY1_TEMP,	   "PHY temp.",		   EFX_HWMON_TEMP, 1),
	SENSOR(PHY1_COOLING,	   "PHY cooling",	   EFX_HWMON_COOL, 1),
	SENSOR(IN_1V0,		   "1.0V supply",	   EFX_HWMON_IN,   -1),
	SENSOR(IN_1V2,		   "1.2V supply",	   EFX_HWMON_IN,   -1),
	SENSOR(IN_1V8,		   "1.8V supply",	   EFX_HWMON_IN,   -1),
	SENSOR(IN_2V5,		   "2.5V supply",	   EFX_HWMON_IN,   -1),
	SENSOR(IN_3V3,		   "3.3V supply",	   EFX_HWMON_IN,   -1),
	SENSOR(IN_12V0,		   "12.0V supply",	   EFX_HWMON_IN,   -1),
	SENSOR(IN_1V2A,		   "1.2V analogue supply", EFX_HWMON_IN,   -1),
	SENSOR(IN_VREF,		   "ref. voltage",	   EFX_HWMON_IN,   -1),
#undef SENSOR
};

static const char *const sensor_status_names[] = {
	[MC_CMD_SENSOR_STATE_OK] = "OK",
	[MC_CMD_SENSOR_STATE_WARNING] = "Warning",
	[MC_CMD_SENSOR_STATE_FATAL] = "Fatal",
	[MC_CMD_SENSOR_STATE_BROKEN] = "Device failure",
};

void efx_mcdi_sensor_event(struct efx_nic *efx, efx_qword_t *ev)
{
	unsigned int type, state, value;
	const char *name = NULL, *state_txt;

	type = EFX_QWORD_FIELD(*ev, MCDI_EVENT_SENSOREVT_MONITOR);
	state = EFX_QWORD_FIELD(*ev, MCDI_EVENT_SENSOREVT_STATE);
	value = EFX_QWORD_FIELD(*ev, MCDI_EVENT_SENSOREVT_VALUE);

	/* Deal gracefully with the board having more drivers than we
	 * know about, but do not expect new sensor states. */
	if (type < ARRAY_SIZE(efx_mcdi_sensor_type))
		name = efx_mcdi_sensor_type[type].label;
	if (!name)
		name = "No sensor name available";
	EFX_BUG_ON_PARANOID(state >= ARRAY_SIZE(sensor_status_names));
	state_txt = sensor_status_names[state];

	netif_err(efx, hw, efx->net_dev,
		  "Sensor %d (%s) reports condition '%s' for raw value %d\n",
		  type, name, state_txt, value);
}

#ifdef CONFIG_SFC_MCDI_MON

struct efx_mcdi_mon_attribute {
	struct device_attribute dev_attr;
	unsigned int index;
	unsigned int type;
	unsigned int limit_value;
	char name[12];
};

static int efx_mcdi_mon_update(struct efx_nic *efx)
{
	struct efx_mcdi_mon *hwmon = efx_mcdi_mon(efx);
	u8 inbuf[MC_CMD_READ_SENSORS_IN_LEN];
	int rc;

	MCDI_SET_DWORD(inbuf, READ_SENSORS_IN_DMA_ADDR_LO,
		       hwmon->dma_buf.dma_addr & 0xffffffff);
	MCDI_SET_DWORD(inbuf, READ_SENSORS_IN_DMA_ADDR_HI,
		       (u64)hwmon->dma_buf.dma_addr >> 32);

	rc = efx_mcdi_rpc(efx, MC_CMD_READ_SENSORS,
			  inbuf, sizeof(inbuf), NULL, 0, NULL);
	if (rc == 0)
		hwmon->last_update = jiffies;
	return rc;
}

static ssize_t efx_mcdi_mon_show_name(struct device *dev,
				      struct device_attribute *attr,
				      char *buf)
{
	return sprintf(buf, "%s\n", KBUILD_MODNAME);
}

static int efx_mcdi_mon_get_entry(struct device *dev, unsigned int index,
				  efx_dword_t *entry)
{
	struct efx_nic *efx = dev_get_drvdata(dev);
	struct efx_mcdi_mon *hwmon = efx_mcdi_mon(efx);
	int rc;

	BUILD_BUG_ON(MC_CMD_READ_SENSORS_OUT_LEN != 0);

	mutex_lock(&hwmon->update_lock);

	/* Use cached value if last update was < 1 s ago */
	if (time_before(jiffies, hwmon->last_update + HZ))
		rc = 0;
	else
		rc = efx_mcdi_mon_update(efx);

	/* Copy out the requested entry */
	*entry = ((efx_dword_t *)hwmon->dma_buf.addr)[index];

	mutex_unlock(&hwmon->update_lock);

	return rc;
}

static ssize_t efx_mcdi_mon_show_value(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct efx_mcdi_mon_attribute *mon_attr =
		container_of(attr, struct efx_mcdi_mon_attribute, dev_attr);
	efx_dword_t entry;
	unsigned int value;
	int rc;

	rc = efx_mcdi_mon_get_entry(dev, mon_attr->index, &entry);
	if (rc)
		return rc;

	value = EFX_DWORD_FIELD(entry, MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_VALUE);

	/* Convert temperature from degrees to milli-degrees Celsius */
	if (efx_mcdi_sensor_type[mon_attr->type].hwmon_type == EFX_HWMON_TEMP)
		value *= 1000;

	return sprintf(buf, "%u\n", value);
}

static ssize_t efx_mcdi_mon_show_limit(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct efx_mcdi_mon_attribute *mon_attr =
		container_of(attr, struct efx_mcdi_mon_attribute, dev_attr);
	unsigned int value;

	value = mon_attr->limit_value;

	/* Convert temperature from degrees to milli-degrees Celsius */
	if (efx_mcdi_sensor_type[mon_attr->type].hwmon_type == EFX_HWMON_TEMP)
		value *= 1000;

	return sprintf(buf, "%u\n", value);
}

static ssize_t efx_mcdi_mon_show_alarm(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct efx_mcdi_mon_attribute *mon_attr =
		container_of(attr, struct efx_mcdi_mon_attribute, dev_attr);
	efx_dword_t entry;
	int state;
	int rc;

	rc = efx_mcdi_mon_get_entry(dev, mon_attr->index, &entry);
	if (rc)
		return rc;

	state = EFX_DWORD_FIELD(entry, MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_STATE);
	return sprintf(buf, "%d\n", state != MC_CMD_SENSOR_STATE_OK);
}

static ssize_t efx_mcdi_mon_show_label(struct device *dev,
				       struct device_attribute *attr,
				       char *buf)
{
	struct efx_mcdi_mon_attribute *mon_attr =
		container_of(attr, struct efx_mcdi_mon_attribute, dev_attr);
	return sprintf(buf, "%s\n",
		       efx_mcdi_sensor_type[mon_attr->type].label);
}

static int
efx_mcdi_mon_add_attr(struct efx_nic *efx, const char *name,
		      ssize_t (*reader)(struct device *,
					struct device_attribute *, char *),
		      unsigned int index, unsigned int type,
		      unsigned int limit_value)
{
	struct efx_mcdi_mon *hwmon = efx_mcdi_mon(efx);
	struct efx_mcdi_mon_attribute *attr = &hwmon->attrs[hwmon->n_attrs];
	int rc;

	strlcpy(attr->name, name, sizeof(attr->name));
	attr->index = index;
	attr->type = type;
	attr->limit_value = limit_value;
	attr->dev_attr.attr.name = attr->name;
	attr->dev_attr.attr.mode = S_IRUGO;
	attr->dev_attr.show = reader;
	rc = device_create_file(&efx->pci_dev->dev, &attr->dev_attr);
	if (rc == 0)
		++hwmon->n_attrs;
	return rc;
}

int efx_mcdi_mon_probe(struct efx_nic *efx)
{
	struct efx_mcdi_mon *hwmon = efx_mcdi_mon(efx);
	unsigned int n_attrs, n_temp = 0, n_cool = 0, n_in = 0;
	u8 outbuf[MC_CMD_SENSOR_INFO_OUT_LENMAX];
	size_t outlen;
	char name[12];
	u32 mask;
	int rc, i, type;

	BUILD_BUG_ON(MC_CMD_SENSOR_INFO_IN_LEN != 0);

	rc = efx_mcdi_rpc(efx, MC_CMD_SENSOR_INFO, NULL, 0,
			  outbuf, sizeof(outbuf), &outlen);
	if (rc)
		return rc;
	if (outlen < MC_CMD_SENSOR_INFO_OUT_LENMIN)
		return -EIO;

	/* Find out which sensors are present.  Don't create a device
	 * if there are none.
	 */
	mask = MCDI_DWORD(outbuf, SENSOR_INFO_OUT_MASK);
	if (mask == 0)
		return 0;

	/* Check again for short response */
	if (outlen < MC_CMD_SENSOR_INFO_OUT_LEN(hweight32(mask)))
		return -EIO;

	rc = efx_nic_alloc_buffer(efx, &hwmon->dma_buf,
				  4 * MC_CMD_SENSOR_ENTRY_MAXNUM);
	if (rc)
		return rc;

	mutex_init(&hwmon->update_lock);
	efx_mcdi_mon_update(efx);

	/* Allocate space for the maximum possible number of
	 * attributes for this set of sensors: name of the driver plus
	 * value, min, max, crit, alarm and label for each sensor.
	 */
	n_attrs = 1 + 6 * hweight32(mask);
	hwmon->attrs = kcalloc(n_attrs, sizeof(*hwmon->attrs), GFP_KERNEL);
	if (!hwmon->attrs) {
		rc = -ENOMEM;
		goto fail;
	}

	hwmon->device = hwmon_device_register(&efx->pci_dev->dev);
	if (IS_ERR(hwmon->device)) {
		rc = PTR_ERR(hwmon->device);
		goto fail;
	}

	rc = efx_mcdi_mon_add_attr(efx, "name", efx_mcdi_mon_show_name, 0, 0, 0);
	if (rc)
		goto fail;

	for (i = 0, type = -1; ; i++) {
		const char *hwmon_prefix;
		unsigned hwmon_index;
		u16 min1, max1, min2, max2;

		/* Find next sensor type or exit if there is none */
		type++;
		while (!(mask & (1 << type))) {
			type++;
			if (type == 32)
				return 0;
		}

		/* Skip sensors specific to a different port */
		if (efx_mcdi_sensor_type[type].hwmon_type != EFX_HWMON_UNKNOWN &&
		    efx_mcdi_sensor_type[type].port >= 0 &&
		    efx_mcdi_sensor_type[type].port != efx_port_num(efx))
			continue;

		switch (efx_mcdi_sensor_type[type].hwmon_type) {
		case EFX_HWMON_TEMP:
			hwmon_prefix = "temp";
			hwmon_index = ++n_temp; /* 1-based */
			break;
		case EFX_HWMON_COOL:
			/* This is likely to be a heatsink, but there
			 * is no convention for representing cooling
			 * devices other than fans.
			 */
			hwmon_prefix = "fan";
			hwmon_index = ++n_cool; /* 1-based */
			break;
		default:
			hwmon_prefix = "in";
			hwmon_index = n_in++; /* 0-based */
			break;
		}

		min1 = MCDI_ARRAY_FIELD(outbuf, SENSOR_ENTRY,
					SENSOR_INFO_ENTRY, i, MIN1);
		max1 = MCDI_ARRAY_FIELD(outbuf, SENSOR_ENTRY,
					SENSOR_INFO_ENTRY, i, MAX1);
		min2 = MCDI_ARRAY_FIELD(outbuf, SENSOR_ENTRY,
					SENSOR_INFO_ENTRY, i, MIN2);
		max2 = MCDI_ARRAY_FIELD(outbuf, SENSOR_ENTRY,
					SENSOR_INFO_ENTRY, i, MAX2);

		if (min1 != max1) {
			snprintf(name, sizeof(name), "%s%u_input",
				 hwmon_prefix, hwmon_index);
			rc = efx_mcdi_mon_add_attr(
				efx, name, efx_mcdi_mon_show_value, i, type, 0);
			if (rc)
				goto fail;

			snprintf(name, sizeof(name), "%s%u_min",
				 hwmon_prefix, hwmon_index);
			rc = efx_mcdi_mon_add_attr(
				efx, name, efx_mcdi_mon_show_limit,
				i, type, min1);
			if (rc)
				goto fail;

			snprintf(name, sizeof(name), "%s%u_max",
				 hwmon_prefix, hwmon_index);
			rc = efx_mcdi_mon_add_attr(
				efx, name, efx_mcdi_mon_show_limit,
				i, type, max1);
			if (rc)
				goto fail;

			if (min2 != max2) {
				/* Assume max2 is critical value.
				 * But we have no good way to expose min2.
				 */
				snprintf(name, sizeof(name), "%s%u_crit",
					 hwmon_prefix, hwmon_index);
				rc = efx_mcdi_mon_add_attr(
					efx, name, efx_mcdi_mon_show_limit,
					i, type, max2);
				if (rc)
					goto fail;
			}
		}

		snprintf(name, sizeof(name), "%s%u_alarm",
			 hwmon_prefix, hwmon_index);
		rc = efx_mcdi_mon_add_attr(
			efx, name, efx_mcdi_mon_show_alarm, i, type, 0);
		if (rc)
			goto fail;

		if (efx_mcdi_sensor_type[type].label) {
			snprintf(name, sizeof(name), "%s%u_label",
				 hwmon_prefix, hwmon_index);
			rc = efx_mcdi_mon_add_attr(
				efx, name, efx_mcdi_mon_show_label, i, type, 0);
			if (rc)
				goto fail;
		}
	}

fail:
	efx_mcdi_mon_remove(efx);
	return rc;
}

void efx_mcdi_mon_remove(struct efx_nic *efx)
{
	struct siena_nic_data *nic_data = efx->nic_data;
	struct efx_mcdi_mon *hwmon = &nic_data->hwmon;
	unsigned int i;

	for (i = 0; i < hwmon->n_attrs; i++)
		device_remove_file(&efx->pci_dev->dev,
				   &hwmon->attrs[i].dev_attr);
	kfree(hwmon->attrs);
	if (hwmon->device)
		hwmon_device_unregister(hwmon->device);
	efx_nic_free_buffer(efx, &hwmon->dma_buf);
}

#endif /* CONFIG_SFC_MCDI_MON */
