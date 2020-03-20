// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2019, The Linaro Limited. All rights reserved.
 */

#include <linux/of.h>

#include "coresight-cti.h"

/* get the hardware configuration & connection data. */
int cti_plat_get_hw_data(struct device *dev,
			 struct cti_drvdata *drvdata)
{
	int rc = 0;
	struct cti_device *cti_dev = &drvdata->ctidev;

	/* if no connections, just add a single default based on max IN-OUT */
	if (cti_dev->nr_trig_con == 0)
		rc = cti_add_default_connection(dev, drvdata);
	return rc;
}

struct coresight_platform_data *
coresight_cti_get_platform_data(struct device *dev)
{
	int ret = -ENOENT;
	struct coresight_platform_data *pdata = NULL;
	struct fwnode_handle *fwnode = dev_fwnode(dev);
	struct cti_drvdata *drvdata = dev_get_drvdata(dev);

	if (IS_ERR_OR_NULL(fwnode))
		goto error;

	/*
	 * Alloc platform data but leave it zero init. CTI does not use the
	 * same connection infrastructuree as trace path components but an
	 * empty struct enables us to use the standard coresight component
	 * registration code.
	 */
	pdata = devm_kzalloc(dev, sizeof(*pdata), GFP_KERNEL);
	if (!pdata) {
		ret = -ENOMEM;
		goto error;
	}

	/* get some CTI specifics */
	ret = cti_plat_get_hw_data(dev, drvdata);

	if (!ret)
		return pdata;
error:
	return ERR_PTR(ret);
}
