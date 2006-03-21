/*
 * linux/drivers/s390/scsi/zfcp_sysfs_port.c
 *
 * FCP adapter driver for IBM eServer zSeries
 *
 * sysfs port related routines
 *
 * (C) Copyright IBM Corp. 2003, 2004
 *
 * Authors:
 *      Martin Peschke <mpeschke@de.ibm.com>
 *	Heiko Carstens <heiko.carstens@de.ibm.com>
 *      Andreas Herrmann <aherrman@de.ibm.com>
 *      Volker Sameske <sameske@de.ibm.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2, or (at your option)
 * any later version.
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

#include "zfcp_ext.h"

#define ZFCP_LOG_AREA                   ZFCP_LOG_AREA_CONFIG

/**
 * zfcp_sysfs_port_release - gets called when a struct device port is released
 * @dev: pointer to belonging device
 */
void
zfcp_sysfs_port_release(struct device *dev)
{
	kfree(dev);
}

/**
 * ZFCP_DEFINE_PORT_ATTR
 * @_name:   name of show attribute
 * @_format: format string
 * @_value:  value to print
 *
 * Generates attributes for a port.
 */
#define ZFCP_DEFINE_PORT_ATTR(_name, _format, _value)                    \
static ssize_t zfcp_sysfs_port_##_name##_show(struct device *dev, struct device_attribute *attr,        \
                                              char *buf)                 \
{                                                                        \
        struct zfcp_port *port;                                          \
                                                                         \
        port = dev_get_drvdata(dev);                                     \
        return sprintf(buf, _format, _value);                            \
}                                                                        \
                                                                         \
static DEVICE_ATTR(_name, S_IRUGO, zfcp_sysfs_port_##_name##_show, NULL);

ZFCP_DEFINE_PORT_ATTR(status, "0x%08x\n", atomic_read(&port->status));
ZFCP_DEFINE_PORT_ATTR(in_recovery, "%d\n", atomic_test_mask
		      (ZFCP_STATUS_COMMON_ERP_INUSE, &port->status));
ZFCP_DEFINE_PORT_ATTR(access_denied, "%d\n", atomic_test_mask
		      (ZFCP_STATUS_COMMON_ACCESS_DENIED, &port->status));

/**
 * zfcp_sysfs_unit_add_store - add a unit to sysfs tree
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 * @count: number of bytes in buffer
 *
 * Store function of the "unit_add" attribute of a port.
 */
static ssize_t
zfcp_sysfs_unit_add_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	fcp_lun_t fcp_lun;
	char *endp;
	struct zfcp_port *port;
	struct zfcp_unit *unit;
	int retval = -EINVAL;

	down(&zfcp_data.config_sema);

	port = dev_get_drvdata(dev);
	if (atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status)) {
		retval = -EBUSY;
		goto out;
	}

	fcp_lun = simple_strtoull(buf, &endp, 0);
	if ((endp + 1) < (buf + count))
		goto out;

	unit = zfcp_unit_enqueue(port, fcp_lun);
	if (!unit)
		goto out;

	retval = 0;

	zfcp_erp_unit_reopen(unit, 0);
	zfcp_erp_wait(unit->port->adapter);
	zfcp_unit_put(unit);
 out:
	up(&zfcp_data.config_sema);
	return retval ? retval : (ssize_t) count;
}

static DEVICE_ATTR(unit_add, S_IWUSR, NULL, zfcp_sysfs_unit_add_store);

/**
 * zfcp_sysfs_unit_remove_store - remove a unit from sysfs tree
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 * @count: number of bytes in buffer
 */
static ssize_t
zfcp_sysfs_unit_remove_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct zfcp_port *port;
	struct zfcp_unit *unit;
	fcp_lun_t fcp_lun;
	char *endp;
	int retval = 0;

	down(&zfcp_data.config_sema);

	port = dev_get_drvdata(dev);
	if (atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status)) {
		retval = -EBUSY;
		goto out;
	}

	fcp_lun = simple_strtoull(buf, &endp, 0);
	if ((endp + 1) < (buf + count)) {
		retval = -EINVAL;
		goto out;
	}

	write_lock_irq(&zfcp_data.config_lock);
	unit = zfcp_get_unit_by_lun(port, fcp_lun);
	if (unit && (atomic_read(&unit->refcount) == 0)) {
		zfcp_unit_get(unit);
		atomic_set_mask(ZFCP_STATUS_COMMON_REMOVE, &unit->status);
		list_move(&unit->list, &port->unit_remove_lh);
	}
	else {
		unit = NULL;
	}
	write_unlock_irq(&zfcp_data.config_lock);

	if (!unit) {
		retval = -ENXIO;
		goto out;
	}

	zfcp_erp_unit_shutdown(unit, 0);
	zfcp_erp_wait(unit->port->adapter);
	zfcp_unit_put(unit);
	zfcp_unit_dequeue(unit);
 out:
	up(&zfcp_data.config_sema);
	return retval ? retval : (ssize_t) count;
}

static DEVICE_ATTR(unit_remove, S_IWUSR, NULL, zfcp_sysfs_unit_remove_store);

/**
 * zfcp_sysfs_port_failed_store - failed state of port
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 * @count: number of bytes in buffer
 *
 * Store function of the "failed" attribute of a port.
 * If a "0" gets written to "failed", error recovery will be
 * started for the belonging port.
 */
static ssize_t
zfcp_sysfs_port_failed_store(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
{
	struct zfcp_port *port;
	unsigned int val;
	char *endp;
	int retval = 0;

	down(&zfcp_data.config_sema);

	port = dev_get_drvdata(dev);
	if (atomic_test_mask(ZFCP_STATUS_COMMON_REMOVE, &port->status)) {
		retval = -EBUSY;
		goto out;
	}

	val = simple_strtoul(buf, &endp, 0);
	if (((endp + 1) < (buf + count)) || (val != 0)) {
		retval = -EINVAL;
		goto out;
	}

	zfcp_erp_modify_port_status(port, ZFCP_STATUS_COMMON_RUNNING, ZFCP_SET);
	zfcp_erp_port_reopen(port, ZFCP_STATUS_COMMON_ERP_FAILED);
	zfcp_erp_wait(port->adapter);
 out:
	up(&zfcp_data.config_sema);
	return retval ? retval : (ssize_t) count;
}

/**
 * zfcp_sysfs_port_failed_show - failed state of port
 * @dev: pointer to belonging device
 * @buf: pointer to input buffer
 *
 * Show function of "failed" attribute of port. Will be
 * "0" if port is working, otherwise "1".
 */
static ssize_t
zfcp_sysfs_port_failed_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct zfcp_port *port;

	port = dev_get_drvdata(dev);
	if (atomic_test_mask(ZFCP_STATUS_COMMON_ERP_FAILED, &port->status))
		return sprintf(buf, "1\n");
	else
		return sprintf(buf, "0\n");
}

static DEVICE_ATTR(failed, S_IWUSR | S_IRUGO, zfcp_sysfs_port_failed_show,
		   zfcp_sysfs_port_failed_store);

/**
 * zfcp_port_common_attrs
 * sysfs attributes that are common for all kind of fc ports.
 */
static struct attribute *zfcp_port_common_attrs[] = {
	&dev_attr_failed.attr,
	&dev_attr_in_recovery.attr,
	&dev_attr_status.attr,
	&dev_attr_access_denied.attr,
	NULL
};

static struct attribute_group zfcp_port_common_attr_group = {
	.attrs = zfcp_port_common_attrs,
};

/**
 * zfcp_port_no_ns_attrs
 * sysfs attributes not to be used for nameserver ports.
 */
static struct attribute *zfcp_port_no_ns_attrs[] = {
	&dev_attr_unit_add.attr,
	&dev_attr_unit_remove.attr,
	NULL
};

static struct attribute_group zfcp_port_no_ns_attr_group = {
	.attrs = zfcp_port_no_ns_attrs,
};

/**
 * zfcp_sysfs_port_create_files - create sysfs port files
 * @dev: pointer to belonging device
 *
 * Create all attributes of the sysfs representation of a port.
 */
int
zfcp_sysfs_port_create_files(struct device *dev, u32 flags)
{
	int retval;

	retval = sysfs_create_group(&dev->kobj, &zfcp_port_common_attr_group);

	if ((flags & ZFCP_STATUS_PORT_WKA) || retval)
		return retval;

	retval = sysfs_create_group(&dev->kobj, &zfcp_port_no_ns_attr_group);
	if (retval)
		sysfs_remove_group(&dev->kobj, &zfcp_port_common_attr_group);

	return retval;
}

/**
 * zfcp_sysfs_port_remove_files - remove sysfs port files
 * @dev: pointer to belonging device
 *
 * Remove all attributes of the sysfs representation of a port.
 */
void
zfcp_sysfs_port_remove_files(struct device *dev, u32 flags)
{
	sysfs_remove_group(&dev->kobj, &zfcp_port_common_attr_group);
	if (!(flags & ZFCP_STATUS_PORT_WKA))
		sysfs_remove_group(&dev->kobj, &zfcp_port_no_ns_attr_group);
}

#undef ZFCP_LOG_AREA
