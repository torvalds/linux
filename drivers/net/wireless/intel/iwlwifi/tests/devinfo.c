// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * KUnit tests for the iwlwifi device info table
 *
 * Copyright (C) 2023-2025 Intel Corporation
 */
#include <kunit/test.h>
#include <linux/pci.h>
#include "iwl-drv.h"
#include "iwl-config.h"

MODULE_IMPORT_NS("EXPORTED_FOR_KUNIT_TESTING");

static void iwl_pci_print_dev_info(const char *pfx, const struct iwl_dev_info *di)
{
	printk(KERN_DEBUG "%sdev=%.4x,subdev=%.4x,mac_type=%.4x,mac_step=%.4x,rf_type=%.4x,cdb=%d,jacket=%d,rf_id=%.2x,bw_limit=%d,cores=%.2x\n",
	       pfx, di->device, di->subdevice, di->mac_type, di->mac_step,
	       di->rf_type, di->cdb, di->jacket, di->rf_id, di->bw_limit,
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
					    di->bw_limit,
					    di->cores, di->rf_step);
		if (!ret) {
			iwl_pci_print_dev_info("No entry found for: ", di);
			KUNIT_FAIL(test,
				   "No entry found for entry at index %d\n", idx);
		} else if (ret != di) {
			iwl_pci_print_dev_info("searched: ", di);
			iwl_pci_print_dev_info("found:    ", ret);
			KUNIT_FAIL(test,
				   "unusable entry at index %d (found index %d instead)\n",
				   idx, (int)(ret - iwl_dev_info_table));
		}
	}
}

static void devinfo_names(struct kunit *test)
{
	int idx;

	for (idx = 0; idx < iwl_dev_info_table_size; idx++) {
		const struct iwl_dev_info *di = &iwl_dev_info_table[idx];

		KUNIT_ASSERT_TRUE(test, di->name);
	}
}

static void devinfo_no_cfg_dups(struct kunit *test)
{
	/* allocate iwl_dev_info_table_size as upper bound */
	const struct iwl_cfg **cfgs = kunit_kcalloc(test,
						    iwl_dev_info_table_size,
						    sizeof(*cfgs), GFP_KERNEL);
	int p = 0;

	KUNIT_ASSERT_NOT_NULL(test, cfgs);

	/* build a list of unique (by pointer) configs first */
	for (int i = 0; i < iwl_dev_info_table_size; i++) {
		bool found = false;

		for (int j = 0; j < p; j++) {
			if (cfgs[j] == iwl_dev_info_table[i].cfg) {
				found = true;
				break;
			}
		}
		if (!found) {
			cfgs[p] = iwl_dev_info_table[i].cfg;
			p++;
		}
	}

	/* check that they're really all different */
	for (int i = 0; i < p; i++) {
		struct iwl_cfg cfg_i = *cfgs[i];

		for (int j = 0; j < i; j++) {
			struct iwl_cfg cfg_j = *cfgs[j];

			KUNIT_EXPECT_NE_MSG(test, memcmp(&cfg_i, &cfg_j,
							 sizeof(cfg_i)), 0,
					    "identical configs: %ps and %ps\n",
					    cfgs[i], cfgs[j]);
		}
	}
}

static void devinfo_no_name_dups(struct kunit *test)
{
	for (int i = 0; i < iwl_dev_info_table_size; i++) {
		for (int j = 0; j < i; j++) {
			if (iwl_dev_info_table[i].name == iwl_dev_info_table[j].name)
				continue;

			KUNIT_EXPECT_NE_MSG(test,
					    strcmp(iwl_dev_info_table[i].name,
						   iwl_dev_info_table[j].name),
					    0,
					    "name dup: %ps/%ps",
					    iwl_dev_info_table[i].name,
					    iwl_dev_info_table[j].name);
		}
	}
}

static void devinfo_check_subdev_match(struct kunit *test)
{
	for (int i = 0; i < iwl_dev_info_table_size; i++) {
		const struct iwl_dev_info *di = &iwl_dev_info_table[i];

		/* if BW limit bit is matched then must have a limit */
		if (di->bw_limit == 1)
			KUNIT_EXPECT_NE(test, di->cfg->bw_limit, 0);

		if (di->subdevice == (u16)IWL_CFG_ANY)
			continue;

		KUNIT_EXPECT_EQ(test, di->rf_id, (u8)IWL_CFG_ANY);
		KUNIT_EXPECT_EQ(test, di->bw_limit, (u8)IWL_CFG_ANY);
		KUNIT_EXPECT_EQ(test, di->cores, (u8)IWL_CFG_ANY);
	}
}

static void devinfo_check_killer_subdev(struct kunit *test)
{
	for (int i = 0; i < iwl_dev_info_table_size; i++) {
		const struct iwl_dev_info *di = &iwl_dev_info_table[i];

		if (!strstr(di->name, "Killer"))
			continue;

		KUNIT_EXPECT_NE(test, di->subdevice, (u16)IWL_CFG_ANY);
	}
}

static void devinfo_pci_ids(struct kunit *test)
{
	struct pci_dev *dev;

	dev = kunit_kmalloc(test, sizeof(*dev), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, dev);

	for (int i = 0; iwl_hw_card_ids[i].vendor; i++) {
		const struct pci_device_id *s, *t;

		s = &iwl_hw_card_ids[i];
		dev->vendor = s->vendor;
		dev->device = s->device;
		dev->subsystem_vendor = s->subvendor;
		dev->subsystem_device = s->subdevice;
		dev->class = s->class;

		t = pci_match_id(iwl_hw_card_ids, dev);
		KUNIT_EXPECT_PTR_EQ(test, t, s);
	}
}

static void devinfo_no_trans_cfg_dups(struct kunit *test)
{
	/* allocate iwl_dev_info_table_size as upper bound */
	const struct iwl_cfg_trans_params **cfgs;
	int count = 0;
	int p = 0;

	for (int i = 0; iwl_hw_card_ids[i].vendor; i++)
		count++;

	cfgs = kunit_kcalloc(test, count, sizeof(*cfgs), GFP_KERNEL);
	KUNIT_ASSERT_NOT_NULL(test, cfgs);

	/* build a list of unique (by pointer) configs first */
	for (int i = 0; iwl_hw_card_ids[i].vendor; i++) {
		struct iwl_cfg_trans_params *cfg;
		bool found = false;

		cfg = (void *)iwl_hw_card_ids[i].driver_data;

		for (int j = 0; j < p; j++) {
			if (cfgs[j] == cfg) {
				found = true;
				break;
			}
		}
		if (!found) {
			cfgs[p] = cfg;
			p++;
		}
	}

	/* check that they're really all different */
	for (int i = 0; i < p; i++) {
		for (int j = 0; j < i; j++) {
			KUNIT_EXPECT_NE_MSG(test, memcmp(cfgs[i], cfgs[j],
							 sizeof(*cfgs[i])), 0,
					    "identical configs: %ps and %ps\n",
					    cfgs[i], cfgs[j]);
		}
	}
}

static struct kunit_case devinfo_test_cases[] = {
	KUNIT_CASE(devinfo_table_order),
	KUNIT_CASE(devinfo_names),
	KUNIT_CASE(devinfo_no_cfg_dups),
	KUNIT_CASE(devinfo_no_name_dups),
	KUNIT_CASE(devinfo_check_subdev_match),
	KUNIT_CASE(devinfo_check_killer_subdev),
	KUNIT_CASE(devinfo_pci_ids),
	KUNIT_CASE(devinfo_no_trans_cfg_dups),
	{}
};

static struct kunit_suite iwlwifi_devinfo = {
	.name = "iwlwifi-devinfo",
	.test_cases = devinfo_test_cases,
};

kunit_test_suite(iwlwifi_devinfo);
