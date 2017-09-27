/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2011-2013 Solarflare Communications Inc.
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
	EFX_HWMON_IN,		/* voltage */
	EFX_HWMON_CURR,		/* current */
	EFX_HWMON_POWER,	/* power */
	EFX_HWMON_TYPES_COUNT
};

static const char *const efx_hwmon_unit[EFX_HWMON_TYPES_COUNT] = {
	[EFX_HWMON_TEMP]  = " degC",
	[EFX_HWMON_COOL]  = " rpm", /* though nonsense for a heatsink */
	[EFX_HWMON_IN]    = " mV",
	[EFX_HWMON_CURR]  = " mA",
	[EFX_HWMON_POWER] = " W",
};

static const struct {
	const char *label;
	enum efx_hwmon_type hwmon_type;
	int port;
} efx_mcdi_sensor_type[] = {
#define SENSOR(name, label, hwmon_type, port)				\
	[MC_CMD_SENSOR_##name] = { label, EFX_HWMON_ ## hwmon_type, port }
	SENSOR(CONTROLLER_TEMP,		"Controller board temp.",   TEMP,  -1),
	SENSOR(PHY_COMMON_TEMP,		"PHY temp.",		    TEMP,  -1),
	SENSOR(CONTROLLER_COOLING,	"Controller heat sink",	    COOL,  -1),
	SENSOR(PHY0_TEMP,		"PHY temp.",		    TEMP,  0),
	SENSOR(PHY0_COOLING,		"PHY heat sink",	    COOL,  0),
	SENSOR(PHY1_TEMP,		"PHY temp.",		    TEMP,  1),
	SENSOR(PHY1_COOLING,		"PHY heat sink",	    COOL,  1),
	SENSOR(IN_1V0,			"1.0V supply",		    IN,    -1),
	SENSOR(IN_1V2,			"1.2V supply",		    IN,    -1),
	SENSOR(IN_1V8,			"1.8V supply",		    IN,    -1),
	SENSOR(IN_2V5,			"2.5V supply",		    IN,    -1),
	SENSOR(IN_3V3,			"3.3V supply",		    IN,    -1),
	SENSOR(IN_12V0,			"12.0V supply",		    IN,    -1),
	SENSOR(IN_1V2A,			"1.2V analogue supply",	    IN,    -1),
	SENSOR(IN_VREF,			"Ref. voltage",		    IN,    -1),
	SENSOR(OUT_VAOE,		"AOE FPGA supply",	    IN,    -1),
	SENSOR(AOE_TEMP,		"AOE FPGA temp.",	    TEMP,  -1),
	SENSOR(PSU_AOE_TEMP,		"AOE regulator temp.",	    TEMP,  -1),
	SENSOR(PSU_TEMP,		"Controller regulator temp.",
								    TEMP,  -1),
	SENSOR(FAN_0,			"Fan 0",		    COOL,  -1),
	SENSOR(FAN_1,			"Fan 1",		    COOL,  -1),
	SENSOR(FAN_2,			"Fan 2",		    COOL,  -1),
	SENSOR(FAN_3,			"Fan 3",		    COOL,  -1),
	SENSOR(FAN_4,			"Fan 4",		    COOL,  -1),
	SENSOR(IN_VAOE,			"AOE input supply",	    IN,    -1),
	SENSOR(OUT_IAOE,		"AOE output current",	    CURR,  -1),
	SENSOR(IN_IAOE,			"AOE input current",	    CURR,  -1),
	SENSOR(NIC_POWER,		"Board power use",	    POWER, -1),
	SENSOR(IN_0V9,			"0.9V supply",		    IN,    -1),
	SENSOR(IN_I0V9,			"0.9V supply current",	    CURR,  -1),
	SENSOR(IN_I1V2,			"1.2V supply current",	    CURR,  -1),
	SENSOR(IN_0V9_ADC,		"0.9V supply (ext. ADC)",   IN,    -1),
	SENSOR(CONTROLLER_2_TEMP,	"Controller board temp. 2", TEMP,  -1),
	SENSOR(VREG_INTERNAL_TEMP,	"Regulator die temp.",	    TEMP,  -1),
	SENSOR(VREG_0V9_TEMP,		"0.9V regulator temp.",     TEMP,  -1),
	SENSOR(VREG_1V2_TEMP,		"1.2V regulator temp.",     TEMP,  -1),
	SENSOR(CONTROLLER_VPTAT,
			      "Controller PTAT voltage (int. ADC)", IN,    -1),
	SENSOR(CONTROLLER_INTERNAL_TEMP,
				 "Controller die temp. (int. ADC)", TEMP,  -1),
	SENSOR(CONTROLLER_VPTAT_EXTADC,
			      "Controller PTAT voltage (ext. ADC)", IN,    -1),
	SENSOR(CONTROLLER_INTERNAL_TEMP_EXTADC,
				 "Controller die temp. (ext. ADC)", TEMP,  -1),
	SENSOR(AMBIENT_TEMP,		"Ambient temp.",	    TEMP,  -1),
	SENSOR(AIRFLOW,			"Air flow raw",		    IN,    -1),
	SENSOR(VDD08D_VSS08D_CSR,	"0.9V die (int. ADC)",	    IN,    -1),
	SENSOR(VDD08D_VSS08D_CSR_EXTADC, "0.9V die (ext. ADC)",	    IN,    -1),
	SENSOR(HOTPOINT_TEMP,  "Controller board temp. (hotpoint)", TEMP,  -1),
#undef SENSOR
};

static const char *const sensor_status_names[] = {
	[MC_CMD_SENSOR_STATE_OK] = "OK",
	[MC_CMD_SENSOR_STATE_WARNING] = "Warning",
	[MC_CMD_SENSOR_STATE_FATAL] = "Fatal",
	[MC_CMD_SENSOR_STATE_BROKEN] = "Device failure",
	[MC_CMD_SENSOR_STATE_NO_READING] = "No reading",
};

void efx_mcdi_sensor_event(struct efx_nic *efx, efx_qword_t *ev)
{
	unsigned int type, state, value;
	enum efx_hwmon_type hwmon_type = EFX_HWMON_UNKNOWN;
	const char *name = NULL, *state_txt, *unit;

	type = EFX_QWORD_FIELD(*ev, MCDI_EVENT_SENSOREVT_MONITOR);
	state = EFX_QWORD_FIELD(*ev, MCDI_EVENT_SENSOREVT_STATE);
	value = EFX_QWORD_FIELD(*ev, MCDI_EVENT_SENSOREVT_VALUE);

	/* Deal gracefully with the board having more drivers than we
	 * know about, but do not expect new sensor states. */
	if (type < ARRAY_SIZE(efx_mcdi_sensor_type)) {
		name = efx_mcdi_sensor_type[type].label;
		hwmon_type = efx_mcdi_sensor_type[type].hwmon_type;
	}
	if (!name)
		name = "No sensor name available";
	EFX_WARN_ON_PARANOID(state >= ARRAY_SIZE(sensor_status_names));
	state_txt = sensor_status_names[state];
	EFX_WARN_ON_PARANOID(hwmon_type >= EFX_HWMON_TYPES_COUNT);
	unit = efx_hwmon_unit[hwmon_type];
	if (!unit)
		unit = "";

	netif_err(efx, hw, efx->net_dev,
		  "Sensor %d (%s) reports condition '%s' for value %d%s\n",
		  type, name, state_txt, value, unit);
}

#ifdef CONFIG_SFC_MCDI_MON

struct efx_mcdi_mon_attribute {
	struct device_attribute dev_attr;
	unsigned int index;
	unsigned int type;
	enum efx_hwmon_type hwmon_type;
	unsigned int limit_value;
	char name[12];
};

static int efx_mcdi_mon_update(struct efx_nic *efx)
{
	struct efx_mcdi_mon *hwmon = efx_mcdi_mon(efx);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_READ_SENSORS_EXT_IN_LEN);
	int rc;

	MCDI_SET_QWORD(inbuf, READ_SENSORS_EXT_IN_DMA_ADDR,
		       hwmon->dma_buf.dma_addr);
	MCDI_SET_DWORD(inbuf, READ_SENSORS_EXT_IN_LENGTH, hwmon->dma_buf.len);

	rc = efx_mcdi_rpc(efx, MC_CMD_READ_SENSORS,
			  inbuf, sizeof(inbuf), NULL, 0, NULL);
	if (rc == 0)
		hwmon->last_update = jiffies;
	return rc;
}

static int efx_mcdi_mon_get_entry(struct device *dev, unsigned int index,
				  efx_dword_t *entry)
{
	struct efx_nic *efx = dev_get_drvdata(dev->parent);
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
	unsigned int value, state;
	int rc;

	rc = efx_mcdi_mon_get_entry(dev, mon_attr->index, &entry);
	if (rc)
		return rc;

	state = EFX_DWORD_FIELD(entry, MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_STATE);
	if (state == MC_CMD_SENSOR_STATE_NO_READING)
		return -EBUSY;

	value = EFX_DWORD_FIELD(entry, MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_VALUE);

	switch (mon_attr->hwmon_type) {
	case EFX_HWMON_TEMP:
		/* Convert temperature from degrees to milli-degrees Celsius */
		value *= 1000;
		break;
	case EFX_HWMON_POWER:
		/* Convert power from watts to microwatts */
		value *= 1000000;
		break;
	default:
		/* No conversion needed */
		break;
	}

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

	switch (mon_attr->hwmon_type) {
	case EFX_HWMON_TEMP:
		/* Convert temperature from degrees to milli-degrees Celsius */
		value *= 1000;
		break;
	case EFX_HWMON_POWER:
		/* Convert power from watts to microwatts */
		value *= 1000000;
		break;
	default:
		/* No conversion needed */
		break;
	}

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

static void
efx_mcdi_mon_add_attr(struct efx_nic *efx, const char *name,
		      ssize_t (*reader)(struct device *,
					struct device_attribute *, char *),
		      unsigned int index, unsigned int type,
		      unsigned int limit_value)
{
	struct efx_mcdi_mon *hwmon = efx_mcdi_mon(efx);
	struct efx_mcdi_mon_attribute *attr = &hwmon->attrs[hwmon->n_attrs];

	strlcpy(attr->name, name, sizeof(attr->name));
	attr->index = index;
	attr->type = type;
	if (type < ARRAY_SIZE(efx_mcdi_sensor_type))
		attr->hwmon_type = efx_mcdi_sensor_type[type].hwmon_type;
	else
		attr->hwmon_type = EFX_HWMON_UNKNOWN;
	attr->limit_value = limit_value;
	sysfs_attr_init(&attr->dev_attr.attr);
	attr->dev_attr.attr.name = attr->name;
	attr->dev_attr.attr.mode = S_IRUGO;
	attr->dev_attr.show = reader;
	hwmon->group.attrs[hwmon->n_attrs++] = &attr->dev_attr.attr;
}

int efx_mcdi_mon_probe(struct efx_nic *efx)
{
	unsigned int n_temp = 0, n_cool = 0, n_in = 0, n_curr = 0, n_power = 0;
	struct efx_mcdi_mon *hwmon = efx_mcdi_mon(efx);
	MCDI_DECLARE_BUF(inbuf, MC_CMD_SENSOR_INFO_EXT_IN_LEN);
	MCDI_DECLARE_BUF(outbuf, MC_CMD_SENSOR_INFO_OUT_LENMAX);
	unsigned int n_pages, n_sensors, n_attrs, page;
	size_t outlen;
	char name[12];
	u32 mask;
	int rc, i, j, type;

	/* Find out how many sensors are present */
	n_sensors = 0;
	page = 0;
	do {
		MCDI_SET_DWORD(inbuf, SENSOR_INFO_EXT_IN_PAGE, page);

		rc = efx_mcdi_rpc(efx, MC_CMD_SENSOR_INFO, inbuf, sizeof(inbuf),
				  outbuf, sizeof(outbuf), &outlen);
		if (rc)
			return rc;
		if (outlen < MC_CMD_SENSOR_INFO_OUT_LENMIN)
			return -EIO;

		mask = MCDI_DWORD(outbuf, SENSOR_INFO_OUT_MASK);
		n_sensors += hweight32(mask & ~(1 << MC_CMD_SENSOR_PAGE0_NEXT));
		++page;
	} while (mask & (1 << MC_CMD_SENSOR_PAGE0_NEXT));
	n_pages = page;

	/* Don't create a device if there are none */
	if (n_sensors == 0)
		return 0;

	rc = efx_nic_alloc_buffer(
		efx, &hwmon->dma_buf,
		n_sensors * MC_CMD_SENSOR_VALUE_ENTRY_TYPEDEF_LEN,
		GFP_KERNEL);
	if (rc)
		return rc;

	mutex_init(&hwmon->update_lock);
	efx_mcdi_mon_update(efx);

	/* Allocate space for the maximum possible number of
	 * attributes for this set of sensors:
	 * value, min, max, crit, alarm and label for each sensor.
	 */
	n_attrs = 6 * n_sensors;
	hwmon->attrs = kcalloc(n_attrs, sizeof(*hwmon->attrs), GFP_KERNEL);
	if (!hwmon->attrs) {
		rc = -ENOMEM;
		goto fail;
	}
	hwmon->group.attrs = kcalloc(n_attrs + 1, sizeof(struct attribute *),
				     GFP_KERNEL);
	if (!hwmon->group.attrs) {
		rc = -ENOMEM;
		goto fail;
	}

	for (i = 0, j = -1, type = -1; ; i++) {
		enum efx_hwmon_type hwmon_type;
		const char *hwmon_prefix;
		unsigned hwmon_index;
		u16 min1, max1, min2, max2;

		/* Find next sensor type or exit if there is none */
		do {
			type++;

			if ((type % 32) == 0) {
				page = type / 32;
				j = -1;
				if (page == n_pages)
					goto hwmon_register;

				MCDI_SET_DWORD(inbuf, SENSOR_INFO_EXT_IN_PAGE,
					       page);
				rc = efx_mcdi_rpc(efx, MC_CMD_SENSOR_INFO,
						  inbuf, sizeof(inbuf),
						  outbuf, sizeof(outbuf),
						  &outlen);
				if (rc)
					goto fail;
				if (outlen < MC_CMD_SENSOR_INFO_OUT_LENMIN) {
					rc = -EIO;
					goto fail;
				}

				mask = (MCDI_DWORD(outbuf,
						   SENSOR_INFO_OUT_MASK) &
					~(1 << MC_CMD_SENSOR_PAGE0_NEXT));

				/* Check again for short response */
				if (outlen <
				    MC_CMD_SENSOR_INFO_OUT_LEN(hweight32(mask))) {
					rc = -EIO;
					goto fail;
				}
			}
		} while (!(mask & (1 << type % 32)));
		j++;

		if (type < ARRAY_SIZE(efx_mcdi_sensor_type)) {
			hwmon_type = efx_mcdi_sensor_type[type].hwmon_type;

			/* Skip sensors specific to a different port */
			if (hwmon_type != EFX_HWMON_UNKNOWN &&
			    efx_mcdi_sensor_type[type].port >= 0 &&
			    efx_mcdi_sensor_type[type].port !=
			    efx_port_num(efx))
				continue;
		} else {
			hwmon_type = EFX_HWMON_UNKNOWN;
		}

		switch (hwmon_type) {
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
		case EFX_HWMON_CURR:
			hwmon_prefix = "curr";
			hwmon_index = ++n_curr; /* 1-based */
			break;
		case EFX_HWMON_POWER:
			hwmon_prefix = "power";
			hwmon_index = ++n_power; /* 1-based */
			break;
		}

		min1 = MCDI_ARRAY_FIELD(outbuf, SENSOR_ENTRY,
					SENSOR_INFO_ENTRY, j, MIN1);
		max1 = MCDI_ARRAY_FIELD(outbuf, SENSOR_ENTRY,
					SENSOR_INFO_ENTRY, j, MAX1);
		min2 = MCDI_ARRAY_FIELD(outbuf, SENSOR_ENTRY,
					SENSOR_INFO_ENTRY, j, MIN2);
		max2 = MCDI_ARRAY_FIELD(outbuf, SENSOR_ENTRY,
					SENSOR_INFO_ENTRY, j, MAX2);

		if (min1 != max1) {
			snprintf(name, sizeof(name), "%s%u_input",
				 hwmon_prefix, hwmon_index);
			efx_mcdi_mon_add_attr(
				efx, name, efx_mcdi_mon_show_value, i, type, 0);

			if (hwmon_type != EFX_HWMON_POWER) {
				snprintf(name, sizeof(name), "%s%u_min",
					 hwmon_prefix, hwmon_index);
				efx_mcdi_mon_add_attr(
					efx, name, efx_mcdi_mon_show_limit,
					i, type, min1);
			}

			snprintf(name, sizeof(name), "%s%u_max",
				 hwmon_prefix, hwmon_index);
			efx_mcdi_mon_add_attr(
				efx, name, efx_mcdi_mon_show_limit,
				i, type, max1);

			if (min2 != max2) {
				/* Assume max2 is critical value.
				 * But we have no good way to expose min2.
				 */
				snprintf(name, sizeof(name), "%s%u_crit",
					 hwmon_prefix, hwmon_index);
				efx_mcdi_mon_add_attr(
					efx, name, efx_mcdi_mon_show_limit,
					i, type, max2);
			}
		}

		snprintf(name, sizeof(name), "%s%u_alarm",
			 hwmon_prefix, hwmon_index);
		efx_mcdi_mon_add_attr(
			efx, name, efx_mcdi_mon_show_alarm, i, type, 0);

		if (type < ARRAY_SIZE(efx_mcdi_sensor_type) &&
		    efx_mcdi_sensor_type[type].label) {
			snprintf(name, sizeof(name), "%s%u_label",
				 hwmon_prefix, hwmon_index);
			efx_mcdi_mon_add_attr(
				efx, name, efx_mcdi_mon_show_label, i, type, 0);
		}
	}

hwmon_register:
	hwmon->groups[0] = &hwmon->group;
	hwmon->device = hwmon_device_register_with_groups(&efx->pci_dev->dev,
							  KBUILD_MODNAME, NULL,
							  hwmon->groups);
	if (IS_ERR(hwmon->device)) {
		rc = PTR_ERR(hwmon->device);
		goto fail;
	}

	return 0;

fail:
	efx_mcdi_mon_remove(efx);
	return rc;
}

void efx_mcdi_mon_remove(struct efx_nic *efx)
{
	struct efx_mcdi_mon *hwmon = efx_mcdi_mon(efx);

	if (hwmon->device)
		hwmon_device_unregister(hwmon->device);
	kfree(hwmon->attrs);
	kfree(hwmon->group.attrs);
	efx_nic_free_buffer(efx, &hwmon->dma_buf);
}

#endif /* CONFIG_SFC_MCDI_MON */
