/*
 * CTC / LCS ccw_device driver
 *
 * Copyright (C) 2002 IBM Deutschland Entwicklung GmbH, IBM Corporation
 * Author(s): Arnd Bergmann <arndb@de.ibm.com>
 *            Cornelia Huck <cornelia.huck@de.ibm.com>
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
 *
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/err.h>

#include <asm/s390_rdev.h>
#include <asm/ccwdev.h>
#include <asm/ccwgroup.h>

#include "cu3088.h"

const char *cu3088_type[] = {
	"not a channel",
	"CTC/A",
	"ESCON channel",
	"FICON channel",
	"P390 LCS card",
	"OSA LCS card",
	"CLAW channel device",
	"unknown channel type",
	"unsupported channel type",
};

/* static definitions */

static struct ccw_device_id cu3088_ids[] = {
	{ CCW_DEVICE(0x3088, 0x08), .driver_info = channel_type_parallel },
	{ CCW_DEVICE(0x3088, 0x1f), .driver_info = channel_type_escon },
	{ CCW_DEVICE(0x3088, 0x1e), .driver_info = channel_type_ficon },
	{ CCW_DEVICE(0x3088, 0x01), .driver_info = channel_type_p390 },
	{ CCW_DEVICE(0x3088, 0x60), .driver_info = channel_type_osa2 },
	{ CCW_DEVICE(0x3088, 0x61), .driver_info = channel_type_claw },
	{ /* end of list */ }
};

static struct ccw_driver cu3088_driver;

struct device *cu3088_root_dev;

static ssize_t
group_write(struct device_driver *drv, const char *buf, size_t count)
{
	const char *start, *end;
	char bus_ids[2][BUS_ID_SIZE], *argv[2];
	int i;
	int ret;
	struct ccwgroup_driver *cdrv;

	cdrv = to_ccwgroupdrv(drv);
	if (!cdrv)
		return -EINVAL;
	start = buf;
	for (i=0; i<2; i++) {
		static const char delim[] = {',', '\n'};
		int len;

		if (!(end = strchr(start, delim[i])))
			return -EINVAL;
		len = min_t(ptrdiff_t, BUS_ID_SIZE, end - start + 1);
		strlcpy (bus_ids[i], start, len);
		argv[i] = bus_ids[i];
		start = end + 1;
	}

	ret = ccwgroup_create(cu3088_root_dev, cdrv->driver_id,
			      &cu3088_driver, 2, argv);

	return (ret == 0) ? count : ret;
}

static DRIVER_ATTR(group, 0200, NULL, group_write);

/* Register-unregister for ctc&lcs */
int
register_cu3088_discipline(struct ccwgroup_driver *dcp) 
{
	int rc;

	if (!dcp)
		return -EINVAL;

	/* Register discipline.*/
	rc = ccwgroup_driver_register(dcp);
	if (rc)
		return rc;

	rc = driver_create_file(&dcp->driver, &driver_attr_group);
	if (rc)
		ccwgroup_driver_unregister(dcp);
		
	return rc;

}

void
unregister_cu3088_discipline(struct ccwgroup_driver *dcp)
{
	if (!dcp)
		return;

	driver_remove_file(&dcp->driver, &driver_attr_group);
	ccwgroup_driver_unregister(dcp);
}

static struct ccw_driver cu3088_driver = {
	.owner	     = THIS_MODULE,
	.ids	     = cu3088_ids,
	.name        = "cu3088",
	.probe	     = ccwgroup_probe_ccwdev,
	.remove	     = ccwgroup_remove_ccwdev,
};

/* module setup */
static int __init
cu3088_init (void)
{
	int rc;
	
	cu3088_root_dev = s390_root_dev_register("cu3088");
	if (IS_ERR(cu3088_root_dev))
		return PTR_ERR(cu3088_root_dev);
	rc = ccw_driver_register(&cu3088_driver);
	if (rc)
		s390_root_dev_unregister(cu3088_root_dev);

	return rc;
}

static void __exit
cu3088_exit (void)
{
	ccw_driver_unregister(&cu3088_driver);
	s390_root_dev_unregister(cu3088_root_dev);
}

MODULE_DEVICE_TABLE(ccw,cu3088_ids);
MODULE_AUTHOR("Arnd Bergmann <arndb@de.ibm.com>");
MODULE_LICENSE("GPL");

module_init(cu3088_init);
module_exit(cu3088_exit);

EXPORT_SYMBOL_GPL(cu3088_type);
EXPORT_SYMBOL_GPL(register_cu3088_discipline);
EXPORT_SYMBOL_GPL(unregister_cu3088_discipline);
