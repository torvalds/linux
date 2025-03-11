// SPDX-License-Identifier: GPL-2.0-only
/*
 * isst_tpmi.c: SST TPMI interface
 *
 * Copyright (c) 2023, Intel Corporation.
 * All Rights Reserved.
 *
 */

#include <linux/auxiliary_bus.h>
#include <linux/module.h>
#include <linux/intel_tpmi.h>

#include "isst_tpmi_core.h"

static int intel_sst_probe(struct auxiliary_device *auxdev, const struct auxiliary_device_id *id)
{
	int ret;

	ret = tpmi_sst_init();
	if (ret)
		return ret;

	ret = tpmi_sst_dev_add(auxdev);
	if (ret)
		tpmi_sst_exit();

	return ret;
}

static void intel_sst_remove(struct auxiliary_device *auxdev)
{
	tpmi_sst_dev_remove(auxdev);
	tpmi_sst_exit();
}

static int intel_sst_suspend(struct device *dev)
{
	tpmi_sst_dev_suspend(to_auxiliary_dev(dev));

	return 0;
}

static int intel_sst_resume(struct device *dev)
{
	tpmi_sst_dev_resume(to_auxiliary_dev(dev));

	return 0;
}

static DEFINE_SIMPLE_DEV_PM_OPS(intel_sst_pm, intel_sst_suspend, intel_sst_resume);

static const struct auxiliary_device_id intel_sst_id_table[] = {
	{ .name = "intel_vsec.tpmi-sst" },
	{}
};
MODULE_DEVICE_TABLE(auxiliary, intel_sst_id_table);

static struct auxiliary_driver intel_sst_aux_driver = {
	.id_table       = intel_sst_id_table,
	.remove         = intel_sst_remove,
	.probe          = intel_sst_probe,
	.driver = {
		.pm = pm_sleep_ptr(&intel_sst_pm),
	},
};

module_auxiliary_driver(intel_sst_aux_driver);

MODULE_IMPORT_NS("INTEL_TPMI_SST");
MODULE_DESCRIPTION("Intel TPMI SST Driver");
MODULE_LICENSE("GPL");
