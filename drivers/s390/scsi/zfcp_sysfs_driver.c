/*
 * This file is part of the zfcp device driver for
 * FCP adapters for IBM System z9 and zSeries.
 *
 * (C) Copyright IBM Corp. 2002, 2006
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
 * ZFCP_DEFINE_DRIVER_ATTR - define for all loglevels sysfs attributes
 * @_name:       name of attribute
 * @_define:     name of ZFCP loglevel define
 *
 * Generates store function for a sysfs loglevel attribute of zfcp driver.
 */
#define ZFCP_DEFINE_DRIVER_ATTR(_name, _define)                               \
static ssize_t zfcp_sysfs_loglevel_##_name##_store(struct device_driver *drv, \
						   const char *buf,           \
						   size_t count)              \
{                                                                             \
	unsigned int loglevel;                                                \
	unsigned int new_loglevel;                                            \
	char *endp;                                                           \
                                                                              \
	new_loglevel = simple_strtoul(buf, &endp, 0);                         \
	if ((endp + 1) < (buf + count))                                       \
		return -EINVAL;                                               \
	if (new_loglevel > 3)                                                 \
		return -EINVAL;                                               \
	down(&zfcp_data.config_sema);                                         \
	loglevel = atomic_read(&zfcp_data.loglevel);                          \
	loglevel &= ~((unsigned int) 0xf << (ZFCP_LOG_AREA_##_define << 2));  \
	loglevel |= new_loglevel << (ZFCP_LOG_AREA_##_define << 2);           \
	atomic_set(&zfcp_data.loglevel, loglevel);                            \
	up(&zfcp_data.config_sema);                                           \
	return count;                                                         \
}                                                                             \
                                                                              \
static ssize_t zfcp_sysfs_loglevel_##_name##_show(struct device_driver *dev,  \
						  char *buf)                  \
{                                                                             \
	return sprintf(buf,"%d\n", (unsigned int)                             \
		       ZFCP_GET_LOG_VALUE(ZFCP_LOG_AREA_##_define));          \
}                                                                             \
                                                                              \
static DRIVER_ATTR(loglevel_##_name, S_IWUSR | S_IRUGO,                       \
		   zfcp_sysfs_loglevel_##_name##_show,                        \
		   zfcp_sysfs_loglevel_##_name##_store);

ZFCP_DEFINE_DRIVER_ATTR(other, OTHER);
ZFCP_DEFINE_DRIVER_ATTR(scsi, SCSI);
ZFCP_DEFINE_DRIVER_ATTR(fsf, FSF);
ZFCP_DEFINE_DRIVER_ATTR(config, CONFIG);
ZFCP_DEFINE_DRIVER_ATTR(cio, CIO);
ZFCP_DEFINE_DRIVER_ATTR(qdio, QDIO);
ZFCP_DEFINE_DRIVER_ATTR(erp, ERP);
ZFCP_DEFINE_DRIVER_ATTR(fc, FC);

static ssize_t zfcp_sysfs_version_show(struct device_driver *dev,
					      char *buf)
{
	return sprintf(buf, "%s\n", zfcp_data.driver_version);
}

static DRIVER_ATTR(version, S_IRUGO, zfcp_sysfs_version_show, NULL);

static struct attribute *zfcp_driver_attrs[] = {
	&driver_attr_loglevel_other.attr,
	&driver_attr_loglevel_scsi.attr,
	&driver_attr_loglevel_fsf.attr,
	&driver_attr_loglevel_config.attr,
	&driver_attr_loglevel_cio.attr,
	&driver_attr_loglevel_qdio.attr,
	&driver_attr_loglevel_erp.attr,
	&driver_attr_loglevel_fc.attr,
	&driver_attr_version.attr,
	NULL
};

static struct attribute_group zfcp_driver_attr_group = {
	.attrs = zfcp_driver_attrs,
};

/**
 * zfcp_sysfs_create_driver_files - create sysfs driver files
 * @dev: pointer to belonging device
 *
 * Create all sysfs attributes of the zfcp device driver
 */
int
zfcp_sysfs_driver_create_files(struct device_driver *drv)
{
	return sysfs_create_group(&drv->kobj, &zfcp_driver_attr_group);
}

/**
 * zfcp_sysfs_remove_driver_files - remove sysfs driver files
 * @dev: pointer to belonging device
 *
 * Remove all sysfs attributes of the zfcp device driver
 */
void
zfcp_sysfs_driver_remove_files(struct device_driver *drv)
{
	sysfs_remove_group(&drv->kobj, &zfcp_driver_attr_group);
}

#undef ZFCP_LOG_AREA
