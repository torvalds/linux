// SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note
/*
 *
 * (C) COPYRIGHT 2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#include <linux/device.h>
#include <linux/coresight.h>

#include <coresight-priv.h>
#include "coresight_mali_common.h"

int coresight_mali_enable_component(struct coresight_device *csdev, u32 mode)
{
	struct coresight_mali_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	int res = 0;

	if (mode != CS_MODE_SYSFS) {
		dev_err(drvdata->dev, "Unsupported Mali CS_MODE: %d, expected: %d\n", mode,
			CS_MODE_SYSFS);
		return -EINVAL;
	}

	drvdata->mode = mode;

	res = kbase_debug_coresight_csf_config_enable(drvdata->config);
	if (res) {
		dev_err(drvdata->dev, "Config failed to enable with error code %d\n", res);
		drvdata->mode = CS_MODE_DISABLED;
	}

	return res;
}

int coresight_mali_disable_component(struct coresight_device *csdev)
{
	struct coresight_mali_drvdata *drvdata = dev_get_drvdata(csdev->dev.parent);
	int res = 0;

	res = kbase_debug_coresight_csf_config_disable(drvdata->config);
	if (res)
		dev_err(drvdata->dev, "config failed to disable with error code %d\n", res);

	drvdata->mode = CS_MODE_DISABLED;

	return res;
}
