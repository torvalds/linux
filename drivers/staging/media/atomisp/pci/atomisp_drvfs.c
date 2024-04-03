// SPDX-License-Identifier: GPL-2.0
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

#include <linux/device.h>
#include <linux/err.h>
#include <linux/kernel.h>

#include "atomisp_compat.h"
#include "atomisp_internal.h"
#include "atomisp_ioctl.h"
#include "atomisp_drvfs.h"
#include "hmm/hmm.h"
#include "ia_css_debug.h"

#define OPTION_BIN_LIST			BIT(0)
#define OPTION_BIN_RUN			BIT(1)
#define OPTION_VALID			(OPTION_BIN_LIST | OPTION_BIN_RUN)

/*
 * dbgopt: iunit debug option:
 *        bit 0: binary list
 *        bit 1: running binary
 *        bit 2: memory statistic
 */
static unsigned int dbgopt = OPTION_BIN_LIST;

static inline int iunit_dump_dbgopt(struct atomisp_device *isp,
				    unsigned int opt)
{
	int ret = 0;

	if (opt & OPTION_VALID) {
		if (opt & OPTION_BIN_LIST) {
			ret = atomisp_css_dump_blob_infor(isp);
			if (ret) {
				dev_err(isp->dev, "%s dump blob infor err[ret:%d]\n",
					__func__, ret);
				goto opt_err;
			}
		}

		if (opt & OPTION_BIN_RUN) {
			if (isp->asd.streaming) {
				atomisp_css_dump_sp_raw_copy_linecount(true);
				atomisp_css_debug_dump_isp_binary();
			} else {
				ret = -EPERM;
				dev_err(isp->dev, "%s dump running bin err[ret:%d]\n",
					__func__, ret);
				goto opt_err;
			}
		}
	} else {
		ret = -EINVAL;
		dev_err(isp->dev, "%s dump nothing[ret=%d]\n", __func__, ret);
	}

opt_err:
	return ret;
}

static ssize_t dbglvl_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	unsigned int dbglvl = ia_css_debug_get_dtrace_level();

	return sysfs_emit(buf, "dtrace level:%u\n", dbglvl);
}

static ssize_t dbglvl_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t size)
{
	unsigned int dbglvl;
	int ret;

	ret = kstrtouint(buf, 10, &dbglvl);
	if (ret)
		return ret;

	if (dbglvl < 1 || dbglvl > 9)
		return -ERANGE;

	ia_css_debug_set_dtrace_level(dbglvl);
	return size;
}
static DEVICE_ATTR_RW(dbglvl);

static ssize_t dbgfun_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	unsigned int dbgfun = atomisp_get_css_dbgfunc();

	return sysfs_emit(buf, "dbgfun opt:%u\n", dbgfun);
}

static ssize_t dbgfun_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct atomisp_device *isp = dev_get_drvdata(dev);
	unsigned int opt;
	int ret;

	ret = kstrtouint(buf, 10, &opt);
	if (ret)
		return ret;

	return atomisp_set_css_dbgfunc(isp, opt);
}
static DEVICE_ATTR_RW(dbgfun);

static ssize_t dbgopt_show(struct device *dev, struct device_attribute *attr,
			   char *buf)
{
	return sysfs_emit(buf, "option:0x%x\n", dbgopt);
}

static ssize_t dbgopt_store(struct device *dev, struct device_attribute *attr,
			    const char *buf, size_t size)
{
	struct atomisp_device *isp = dev_get_drvdata(dev);
	unsigned int opt;
	int ret;

	ret = kstrtouint(buf, 10, &opt);
	if (ret)
		return ret;

	dbgopt = opt;
	ret = iunit_dump_dbgopt(isp, dbgopt);
	if (ret)
		return ret;

	return size;
}
static DEVICE_ATTR_RW(dbgopt);

static struct attribute *dbg_attrs[] = {
	&dev_attr_dbglvl.attr,
	&dev_attr_dbgfun.attr,
	&dev_attr_dbgopt.attr,
	NULL
};

static const struct attribute_group dbg_attr_group = {
	.attrs = dbg_attrs,
};

const struct attribute_group *dbg_attr_groups[] = {
	&dbg_attr_group,
	NULL
};
