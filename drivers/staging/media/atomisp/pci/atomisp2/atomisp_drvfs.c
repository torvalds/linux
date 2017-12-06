/*
 * Support for atomisp driver sysfs interface
 *
 * Copyright (c) 2014 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */

#include <linux/err.h>
#include <linux/kernel.h>
#include <linux/pci.h>

#include "atomisp_compat.h"
#include "atomisp_internal.h"
#include "atomisp_ioctl.h"
#include "hmm/hmm.h"

/*
 * _iunit_debug:
 * dbglvl: iunit css driver trace level
 * dbgopt: iunit debug option:
 *        bit 0: binary list
 *        bit 1: running binary
 *        bit 2: memory statistic
*/
struct _iunit_debug {
	struct pci_driver	*drv;
	struct atomisp_device	*isp;
	unsigned int		dbglvl;
	unsigned int		dbgfun;
	unsigned int		dbgopt;
};

#define OPTION_BIN_LIST			(1<<0)
#define OPTION_BIN_RUN			(1<<1)
#define OPTION_MEM_STAT			(1<<2)
#define OPTION_VALID			(OPTION_BIN_LIST \
					| OPTION_BIN_RUN \
					| OPTION_MEM_STAT)

static struct _iunit_debug iunit_debug = {
	.dbglvl = 0,
	.dbgopt = OPTION_BIN_LIST,
};

static inline int iunit_dump_dbgopt(struct atomisp_device *isp,
				unsigned int opt)
{
	int ret = 0;

	if (opt & OPTION_VALID) {
		if (opt & OPTION_BIN_LIST) {
			ret = atomisp_css_dump_blob_infor();
			if (ret) {
				dev_err(atomisp_dev, "%s dump blob infor err[ret:%d]\n",
					__func__, ret);
				goto opt_err;
			}
		}

		if (opt & OPTION_BIN_RUN) {
			if (atomisp_streaming_count(isp)) {
				atomisp_css_dump_sp_raw_copy_linecount(true);
				atomisp_css_debug_dump_isp_binary();
			} else {
				ret = -EPERM;
				dev_err(atomisp_dev, "%s dump running bin err[ret:%d]\n",
					__func__, ret);
				goto opt_err;
			}
		}

		if (opt & OPTION_MEM_STAT)
			hmm_show_mem_stat(__func__, __LINE__);
	} else {
		ret = -EINVAL;
		dev_err(atomisp_dev, "%s dump nothing[ret=%d]\n", __func__,
			ret);
	}

opt_err:
	return ret;
}

static ssize_t iunit_dbglvl_show(struct device_driver *drv, char *buf)
{
	iunit_debug.dbglvl = atomisp_css_debug_get_dtrace_level();
	return sprintf(buf, "dtrace level:%u\n", iunit_debug.dbglvl);
}

static ssize_t iunit_dbglvl_store(struct device_driver *drv, const char *buf,
				size_t size)
{
	if (kstrtouint(buf, 10, &iunit_debug.dbglvl)
		|| iunit_debug.dbglvl < 1
		|| iunit_debug.dbglvl > 9) {
		return -ERANGE;
	}
	atomisp_css_debug_set_dtrace_level(iunit_debug.dbglvl);

	return size;
}

static ssize_t iunit_dbgfun_show(struct device_driver *drv, char *buf)
{
	iunit_debug.dbgfun = atomisp_get_css_dbgfunc();
	return sprintf(buf, "dbgfun opt:%u\n", iunit_debug.dbgfun);
}

static ssize_t iunit_dbgfun_store(struct device_driver *drv, const char *buf,
				size_t size)
{
	unsigned int opt;
	int ret;

	ret = kstrtouint(buf, 10, &opt);
	if (ret)
		return ret;

	ret = atomisp_set_css_dbgfunc(iunit_debug.isp, opt);
	if (ret)
		return ret;

	iunit_debug.dbgfun = opt;

	return size;
}

static ssize_t iunit_dbgopt_show(struct device_driver *drv, char *buf)
{
	return sprintf(buf, "option:0x%x\n", iunit_debug.dbgopt);
}

static ssize_t iunit_dbgopt_store(struct device_driver *drv, const char *buf,
				size_t size)
{
	unsigned int opt;
	int ret;

	ret = kstrtouint(buf, 10, &opt);
	if (ret)
		return ret;

	iunit_debug.dbgopt = opt;
	ret = iunit_dump_dbgopt(iunit_debug.isp, iunit_debug.dbgopt);
	if (ret)
		return ret;

	return size;
}

static const struct driver_attribute iunit_drvfs_attrs[] = {
	__ATTR(dbglvl, 0644, iunit_dbglvl_show, iunit_dbglvl_store),
	__ATTR(dbgfun, 0644, iunit_dbgfun_show, iunit_dbgfun_store),
	__ATTR(dbgopt, 0644, iunit_dbgopt_show, iunit_dbgopt_store),
};

static int iunit_drvfs_create_files(struct pci_driver *drv)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(iunit_drvfs_attrs); i++)
		ret |= driver_create_file(&(drv->driver),
					&iunit_drvfs_attrs[i]);

	return ret;
}

static void iunit_drvfs_remove_files(struct pci_driver *drv)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(iunit_drvfs_attrs); i++)
		driver_remove_file(&(drv->driver), &iunit_drvfs_attrs[i]);
}

int atomisp_drvfs_init(struct pci_driver *drv, struct atomisp_device *isp)
{
	int ret;

	iunit_debug.isp = isp;
	iunit_debug.drv = drv;

	ret = iunit_drvfs_create_files(iunit_debug.drv);
	if (ret) {
		dev_err(atomisp_dev, "drvfs_create_files error: %d\n", ret);
		iunit_drvfs_remove_files(drv);
	}

	return ret;
}

void atomisp_drvfs_exit(void)
{
	iunit_drvfs_remove_files(iunit_debug.drv);
}
