// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * KUnit tests for the iwlwifi device info table
 *
 * Copyright (C) 2023 Intel Corporation
 */
#include <kunit/test.h>
#include "iwl-drv.h"
#include "iwl-config.h"

MODULE_IMPORT_NS(EXPORTED_FOR_KUNIT_TESTING);

static void iwl_pci_print_dev_info(const char *pfx, const struct iwl_dev_info *di)
{
	printk(KERN_DEBUG "%sdev=%.4x,subdev=%.4x,mac_type=%.4x,mac_step=%.4x,rf_type=%.4x,cdb=%d,jacket=%d,rf_id=%.2x,no_160=%d,cores=%.2x\n",
	       pfx, di->device, di->subdevice, di->mac_type, di->mac_step,
	       di->rf_type, di->cdb, di->jacket, di->rf_id, di->no_160,
	       di->cores);
}

static void devinfo_table_order(struct kunit *test)
{
	int idx;

	for (idx = 0; idx < iwl_dev_info_table_size; idx++) {
		const struct iwl_dev_info *di = &iwl_dev_info_table[idx];
		const struct iwl_dev_info *ret;

		ret = iwl_pci_find_dev_info(di->device, di->subdevice,
					    di->mac_type, di->mac_step,
					    di->rf_type, di->cdb,
					    di->jacket, di->rf_id,
					    di->no_160, di->cores, di->rf_step);
		if (ret != di) {
			iwl_pci_print_dev_info("searched: ", di);
			iwl_pci_print_dev_info("found:    ", ret);
			KUNIT_FAIL(test,
				   "unusable entry at index %d (found index %d instead)\n",
				   idx, (int)(ret - iwl_dev_info_table));
		}
	}
}

static struct kunit_case devinfo_test_cases[] = {
	KUNIT_CASE(devinfo_table_order),
	{}
};

static struct kunit_suite iwlwifi_devinfo = {
	.name = "iwlwifi-devinfo",
	.test_cases = devinfo_test_cases,
};

kunit_test_suite(iwlwifi_devinfo);
