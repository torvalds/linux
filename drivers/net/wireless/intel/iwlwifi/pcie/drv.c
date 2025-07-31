// SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause
/*
 * Copyright (C) 2005-2014, 2018-2025 Intel Corporation
 * Copyright (C) 2013-2015 Intel Mobile Communications GmbH
 * Copyright (C) 2016-2017 Intel Deutschland GmbH
 */
#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/pci.h>
#include <linux/acpi.h>

#include "fw/acpi.h"

#include "iwl-trans.h"
#include "iwl-drv.h"
#include "iwl-prph.h"
#include "internal.h"

#define _IS_A(cfg, _struct) __builtin_types_compatible_p(typeof(cfg),	\
							 struct _struct)
extern int _invalid_type;
#define _TRANS_CFG_CHECK(cfg)						\
	(__builtin_choose_expr(_IS_A(cfg, iwl_mac_cfg),	\
			       0, _invalid_type))
#define _ASSIGN_CFG(cfg) (_TRANS_CFG_CHECK(cfg) + (kernel_ulong_t)&(cfg))

#define IWL_PCI_DEVICE(dev, subdev, cfg) \
	.vendor = PCI_VENDOR_ID_INTEL,  .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = (subdev), \
	.driver_data = _ASSIGN_CFG(cfg)

/* Hardware specific file defines the PCI IDs table for that hardware module */
VISIBLE_IF_IWLWIFI_KUNIT const struct pci_device_id iwl_hw_card_ids[] = {
#if IS_ENABLED(CONFIG_IWLDVM)
	{IWL_PCI_DEVICE(0x4232, 0x1201, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1301, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1204, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1304, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1205, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1305, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1206, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1306, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1221, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1321, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1224, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1324, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1225, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1325, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1226, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1326, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1211, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1311, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1214, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1314, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1215, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1315, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1216, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1316, iwl5000_mac_cfg)}, /* Half Mini Card */

/* 5300 Series WiFi */
	{IWL_PCI_DEVICE(0x4235, 0x1021, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1121, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1024, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1124, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1001, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1101, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1004, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1104, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4236, 0x1011, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4236, 0x1111, iwl5000_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4236, 0x1014, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4236, 0x1114, iwl5000_mac_cfg)}, /* Half Mini Card */

/* 5350 Series WiFi/WiMax */
	{IWL_PCI_DEVICE(0x423A, 0x1001, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423A, 0x1021, iwl5000_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423B, 0x1011, iwl5000_mac_cfg)}, /* Mini Card */

/* 5150 Series Wifi/WiMax */
	{IWL_PCI_DEVICE(0x423C, 0x1201, iwl5150_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1301, iwl5150_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1206, iwl5150_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1306, iwl5150_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1221, iwl5150_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1321, iwl5150_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1326, iwl5150_mac_cfg)}, /* Half Mini Card */

	{IWL_PCI_DEVICE(0x423D, 0x1211, iwl5150_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423D, 0x1311, iwl5150_mac_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x423D, 0x1216, iwl5150_mac_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423D, 0x1316, iwl5150_mac_cfg)}, /* Half Mini Card */

/* 6x00 Series */
	{IWL_PCI_DEVICE(0x422B, 0x1101, iwl6000_mac_cfg)},
	{IWL_PCI_DEVICE(0x422B, 0x1108, iwl6000_mac_cfg)},
	{IWL_PCI_DEVICE(0x422B, 0x1121, iwl6000_mac_cfg)},
	{IWL_PCI_DEVICE(0x422B, 0x1128, iwl6000_mac_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1301, iwl6000i_mac_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1306, iwl6000i_mac_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1307, iwl6000i_mac_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1321, iwl6000i_mac_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1326, iwl6000i_mac_cfg)},
	{IWL_PCI_DEVICE(0x4238, 0x1111, iwl6000_mac_cfg)},
	{IWL_PCI_DEVICE(0x4238, 0x1118, iwl6000_mac_cfg)},
	{IWL_PCI_DEVICE(0x4239, 0x1311, iwl6000i_mac_cfg)},
	{IWL_PCI_DEVICE(0x4239, 0x1316, iwl6000i_mac_cfg)},

/* 6x05 Series */
	{IWL_PCI_DEVICE(0x0082, 0x1301, iwl6005_mac_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1306, iwl6005_mac_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1307, iwl6005_mac_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1308, iwl6005_mac_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1321, iwl6005_mac_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1326, iwl6005_mac_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1328, iwl6005_mac_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0x1311, iwl6005_mac_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0x1318, iwl6005_mac_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0x1316, iwl6005_mac_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0xC020, iwl6005_mac_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0xC220, iwl6005_mac_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0xC228, iwl6005_mac_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x4820, iwl6005_mac_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1304, iwl6005_mac_cfg)},/* low 5GHz active */
	{IWL_PCI_DEVICE(0x0082, 0x1305, iwl6005_mac_cfg)},/* high 5GHz active */

/* 6x30 Series */
	{IWL_PCI_DEVICE(0x008A, 0x5305, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x008A, 0x5307, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x008A, 0x5325, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x008A, 0x5327, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x008B, 0x5315, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x008B, 0x5317, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0090, 0x5211, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x0090, 0x5215, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x0090, 0x5216, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5201, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5205, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5206, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5207, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5221, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5225, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5226, iwl6030_mac_cfg)},

/* 6x50 WiFi/WiMax Series */
	{IWL_PCI_DEVICE(0x0087, 0x1301, iwl6050_mac_cfg)},
	{IWL_PCI_DEVICE(0x0087, 0x1306, iwl6050_mac_cfg)},
	{IWL_PCI_DEVICE(0x0087, 0x1321, iwl6050_mac_cfg)},
	{IWL_PCI_DEVICE(0x0087, 0x1326, iwl6050_mac_cfg)},
	{IWL_PCI_DEVICE(0x0089, 0x1311, iwl6050_mac_cfg)},
	{IWL_PCI_DEVICE(0x0089, 0x1316, iwl6050_mac_cfg)},

/* 6150 WiFi/WiMax Series */
	{IWL_PCI_DEVICE(0x0885, 0x1305, iwl6150_mac_cfg)},
	{IWL_PCI_DEVICE(0x0885, 0x1307, iwl6150_mac_cfg)},
	{IWL_PCI_DEVICE(0x0885, 0x1325, iwl6150_mac_cfg)},
	{IWL_PCI_DEVICE(0x0885, 0x1327, iwl6150_mac_cfg)},
	{IWL_PCI_DEVICE(0x0886, 0x1315, iwl6150_mac_cfg)},
	{IWL_PCI_DEVICE(0x0886, 0x1317, iwl6150_mac_cfg)},

/* 1000 Series WiFi */
	{IWL_PCI_DEVICE(0x0083, 0x1205, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1305, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1225, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1325, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0084, 0x1215, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0084, 0x1315, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1206, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1306, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1226, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1326, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0084, 0x1216, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0084, 0x1316, iwl1000_mac_cfg)},

/* 100 Series WiFi */
	{IWL_PCI_DEVICE(0x08AE, 0x1005, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08AE, 0x1007, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08AF, 0x1015, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08AF, 0x1017, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08AE, 0x1025, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08AE, 0x1027, iwl1000_mac_cfg)},

/* 130 Series WiFi */
	{IWL_PCI_DEVICE(0x0896, 0x5005, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0896, 0x5007, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0897, 0x5015, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0897, 0x5017, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0896, 0x5025, iwl1000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0896, 0x5027, iwl1000_mac_cfg)},

/* 2x00 Series */
	{IWL_PCI_DEVICE(0x0890, 0x4022, iwl2000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0891, 0x4222, iwl2000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0890, 0x4422, iwl2000_mac_cfg)},
	{IWL_PCI_DEVICE(0x0890, 0x4822, iwl2000_mac_cfg)},

/* 2x30 Series */
	{IWL_PCI_DEVICE(0x0887, 0x4062, iwl2030_mac_cfg)},
	{IWL_PCI_DEVICE(0x0888, 0x4262, iwl2030_mac_cfg)},
	{IWL_PCI_DEVICE(0x0887, 0x4462, iwl2030_mac_cfg)},

/* 6x35 Series */
	{IWL_PCI_DEVICE(0x088E, 0x4060, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x406A, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x088F, 0x4260, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x088F, 0x426A, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x4460, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x446A, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x4860, iwl6030_mac_cfg)},
	{IWL_PCI_DEVICE(0x088F, 0x5260, iwl6030_mac_cfg)},

/* 105 Series */
	{IWL_PCI_DEVICE(0x0894, 0x0022, iwl105_mac_cfg)},
	{IWL_PCI_DEVICE(0x0895, 0x0222, iwl105_mac_cfg)},
	{IWL_PCI_DEVICE(0x0894, 0x0422, iwl105_mac_cfg)},
	{IWL_PCI_DEVICE(0x0894, 0x0822, iwl105_mac_cfg)},

/* 135 Series */
	{IWL_PCI_DEVICE(0x0892, 0x0062, iwl135_mac_cfg)},
	{IWL_PCI_DEVICE(0x0893, 0x0262, iwl135_mac_cfg)},
	{IWL_PCI_DEVICE(0x0892, 0x0462, iwl135_mac_cfg)},
#endif /* CONFIG_IWLDVM */

#if IS_ENABLED(CONFIG_IWLMVM)
/* 7260 Series */
	{IWL_PCI_DEVICE(0x08B1, 0x4070, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4072, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4170, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4C60, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4C70, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4060, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x406A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4160, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4062, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4162, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4270, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4272, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4260, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x426A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4262, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4470, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4472, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4460, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x446A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4462, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4870, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x486E, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4A70, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4A6E, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4A6C, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4570, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4560, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4370, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4360, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x5070, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x5072, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x5170, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x5770, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4020, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x402A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4220, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4420, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC070, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC072, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC170, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC060, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC06A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC160, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC062, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC162, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC770, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC760, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC270, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xCC70, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xCC60, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC272, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC260, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC26A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC262, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC470, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC472, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC460, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC462, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC570, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC560, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC370, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC360, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC020, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC02A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC220, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC420, iwl7000_mac_cfg)},

/* 3160 Series */
	{IWL_PCI_DEVICE(0x08B3, 0x0070, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0072, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0170, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0172, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0060, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0062, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x0270, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x0272, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0470, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0472, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x0370, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8070, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8072, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8170, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8172, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8060, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8062, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x8270, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x8370, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x8272, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8470, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8570, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x1070, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x1170, iwl7000_mac_cfg)},

/* 3165 Series */
	{IWL_PCI_DEVICE(0x3165, 0x4010, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x4012, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3166, 0x4212, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x4410, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x4510, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x4110, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3166, 0x4310, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3166, 0x4210, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x8010, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x8110, iwl7000_mac_cfg)},

/* 3168 Series */
	{IWL_PCI_DEVICE(0x24FB, 0x2010, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FB, 0x2110, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FB, 0x2050, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FB, 0x2150, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FB, 0x0000, iwl7000_mac_cfg)},

/* 7265 Series */
	{IWL_PCI_DEVICE(0x095A, 0x5010, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5110, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5100, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5310, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5302, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5210, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5C10, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5012, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5412, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5410, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5510, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5400, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x1010, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5000, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x500A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5200, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5002, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5102, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5202, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9010, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9012, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x900A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9110, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9112, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x9210, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x9200, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9510, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x9310, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9410, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5020, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x502A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5420, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5090, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5190, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5590, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5290, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5490, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5F10, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5212, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x520A, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9000, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9400, iwl7000_mac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9E10, iwl7000_mac_cfg)},

/* 8000 Series */
	{IWL_PCI_DEVICE(0x24F3, 0x0010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x10B0, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0130, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1130, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0132, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1132, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x01F0, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0012, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1012, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0250, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0150, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1150, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0x0030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0x1030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xC010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xC110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xD010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xC050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xD050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xD0B0, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xB0B0, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0x8030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0x9030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0xC030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0xD030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8130, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9130, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8132, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9132, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8150, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9150, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0004, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0044, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F5, 0x0010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F6, 0x0030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0810, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0910, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0850, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0950, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0930, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0000, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x4010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xC030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xD030, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1130, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0130, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x10D0, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0150, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x9010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x8110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x8050, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x8010, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0810, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x9110, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x8130, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0910, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0930, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0950, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0850, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1014, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x3E02, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x3E01, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1012, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0012, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0014, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x9074, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1431, iwl8000_mac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1432, iwl8000_mac_cfg)},

/* 9000 Series */
	{IWL_PCI_DEVICE(0x2526, PCI_ANY_ID, iwl9000_mac_cfg)},
	{IWL_PCI_DEVICE(0x271B, PCI_ANY_ID, iwl9000_mac_cfg)},
	{IWL_PCI_DEVICE(0x271C, PCI_ANY_ID, iwl9000_mac_cfg)},
	{IWL_PCI_DEVICE(0x30DC, PCI_ANY_ID, iwl9560_long_latency_mac_cfg)},
	{IWL_PCI_DEVICE(0x31DC, PCI_ANY_ID, iwl9560_shared_clk_mac_cfg)},
	{IWL_PCI_DEVICE(0x9DF0, PCI_ANY_ID, iwl9560_mac_cfg)},
	{IWL_PCI_DEVICE(0xA370, PCI_ANY_ID, iwl9560_mac_cfg)},

/* Qu devices */
	{IWL_PCI_DEVICE(0x02F0, PCI_ANY_ID, iwl_qu_mac_cfg)},
	{IWL_PCI_DEVICE(0x06F0, PCI_ANY_ID, iwl_qu_mac_cfg)},

	{IWL_PCI_DEVICE(0x34F0, PCI_ANY_ID, iwl_qu_medium_latency_mac_cfg)},
	{IWL_PCI_DEVICE(0x3DF0, PCI_ANY_ID, iwl_qu_medium_latency_mac_cfg)},
	{IWL_PCI_DEVICE(0x4DF0, PCI_ANY_ID, iwl_qu_medium_latency_mac_cfg)},

	{IWL_PCI_DEVICE(0x43F0, PCI_ANY_ID, iwl_qu_long_latency_mac_cfg)},
	{IWL_PCI_DEVICE(0xA0F0, PCI_ANY_ID, iwl_qu_long_latency_mac_cfg)},

	{IWL_PCI_DEVICE(0x2723, PCI_ANY_ID, iwl_ax200_mac_cfg)},

/* Ty/So devices */
	{IWL_PCI_DEVICE(0x2725, PCI_ANY_ID, iwl_ty_mac_cfg)},
	{IWL_PCI_DEVICE(0x7A70, PCI_ANY_ID, iwl_so_long_latency_imr_mac_cfg)},
	{IWL_PCI_DEVICE(0x7AF0, PCI_ANY_ID, iwl_so_mac_cfg)},
	{IWL_PCI_DEVICE(0x51F0, PCI_ANY_ID, iwl_so_long_latency_mac_cfg)},
	{IWL_PCI_DEVICE(0x51F1, PCI_ANY_ID, iwl_so_long_latency_imr_mac_cfg)},
	{IWL_PCI_DEVICE(0x54F0, PCI_ANY_ID, iwl_so_long_latency_mac_cfg)},
	{IWL_PCI_DEVICE(0x7F70, PCI_ANY_ID, iwl_so_mac_cfg)},

/* Ma devices */
	{IWL_PCI_DEVICE(0x2729, PCI_ANY_ID, iwl_ma_mac_cfg)},
	{IWL_PCI_DEVICE(0x7E40, PCI_ANY_ID, iwl_ma_mac_cfg)},
#endif /* CONFIG_IWLMVM */
#if IS_ENABLED(CONFIG_IWLMLD)
/* Bz devices */
	{IWL_PCI_DEVICE(0x272b, PCI_ANY_ID, iwl_gl_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0000, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0090, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0094, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0098, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x009C, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x00C0, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x00C4, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x00E0, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x00E4, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x00E8, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x00EC, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0100, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0110, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0114, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0118, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x011C, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0310, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0314, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0510, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x0A10, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1671, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1672, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1771, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1772, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1791, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1792, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x4090, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x40C4, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x40E0, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x4110, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x4314, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1775, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0xA840, 0x1776, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0x7740, PCI_ANY_ID, iwl_bz_mac_cfg)},
	{IWL_PCI_DEVICE(0x4D40, PCI_ANY_ID, iwl_bz_mac_cfg)},

/* Sc devices */
	{IWL_PCI_DEVICE(0xE440, PCI_ANY_ID, iwl_sc_mac_cfg)},
	{IWL_PCI_DEVICE(0xE340, PCI_ANY_ID, iwl_sc_mac_cfg)},
	{IWL_PCI_DEVICE(0xD340, PCI_ANY_ID, iwl_sc_mac_cfg)},
	{IWL_PCI_DEVICE(0x6E70, PCI_ANY_ID, iwl_sc_mac_cfg)},
#endif /* CONFIG_IWLMLD */

	{0}
};
MODULE_DEVICE_TABLE(pci, iwl_hw_card_ids);
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_hw_card_ids);

#define _IWL_DEV_INFO(_cfg, _name, ...) {	\
	.cfg = &_cfg,				\
	.name = _name,				\
	.device = IWL_CFG_ANY,			\
	.subdevice = IWL_CFG_ANY,		\
	.subdevice_m_h = 15,			\
	__VA_ARGS__				\
}
#define IWL_DEV_INFO(_cfg, _name, ...)		\
	_IWL_DEV_INFO(_cfg, _name, __VA_ARGS__)

#define DEVICE(n)		.device = (n)
#define SUBDEV(n)		.subdevice = (n)
#define _LOWEST_BIT(n)		(__builtin_ffs(n) - 1)
#define _BIT_ABOVE_MASK(n)	((n) + (1 << _LOWEST_BIT(n)))
#define _HIGHEST_BIT(n)		(__builtin_ffs(_BIT_ABOVE_MASK(n)) - 2)
#define _IS_POW2(n)		(((n) & ((n) - 1)) == 0)
#define _IS_CONTIG(n)		_IS_POW2(_BIT_ABOVE_MASK(n))
#define _CHECK_MASK(m)		BUILD_BUG_ON_ZERO(!_IS_CONTIG(m))
#define SUBDEV_MASKED(v, m)	.subdevice = (v) + _CHECK_MASK(m),	\
				.subdevice_m_l = _LOWEST_BIT(m),	\
				.subdevice_m_h = _HIGHEST_BIT(m)
#define RF_TYPE(n)		.match_rf_type = 1,			\
				.rf_type = IWL_CFG_RF_TYPE_##n
#define RF_STEP(n)		.match_rf_step = 1,			\
				.rf_step = SILICON_##n##_STEP
#define RF_ID(n)		.match_rf_id = 1,			\
				.rf_id = IWL_CFG_RF_ID_##n
#define NO_CDB			.match_cdb = 1, .cdb = 0
#define CDB			.match_cdb = 1, .cdb = 1
#define BW_NOT_LIMITED		.match_bw_limit = 1, .bw_limit = 0
#define BW_LIMITED		.match_bw_limit = 1, .bw_limit = 1

VISIBLE_IF_IWLWIFI_KUNIT const struct iwl_dev_info iwl_dev_info_table[] = {
#if IS_ENABLED(CONFIG_IWLDVM)
	IWL_DEV_INFO(iwl5100_n_cfg, iwl5100_agn_name,
		     DEVICE(0x4232), SUBDEV_MASKED(0x1, 0xF)),
	IWL_DEV_INFO(iwl5100_n_cfg, iwl5100_agn_name,
		     DEVICE(0x4232), SUBDEV_MASKED(0x4, 0xF)),
	IWL_DEV_INFO(iwl5100_n_cfg, iwl5100_bgn_name,
		     DEVICE(0x4232), SUBDEV_MASKED(0x5, 0xF)),
	IWL_DEV_INFO(iwl5100_abg_cfg, iwl5100_abg_name,
		     DEVICE(0x4232), SUBDEV_MASKED(0x6, 0xF)),
	IWL_DEV_INFO(iwl5100_n_cfg, iwl5100_agn_name,
		     DEVICE(0x4237), SUBDEV_MASKED(0x1, 0xF)),
	IWL_DEV_INFO(iwl5100_n_cfg, iwl5100_agn_name,
		     DEVICE(0x4237), SUBDEV_MASKED(0x4, 0xF)),
	IWL_DEV_INFO(iwl5100_n_cfg, iwl5100_bgn_name,
		     DEVICE(0x4237), SUBDEV_MASKED(0x5, 0xF)),
	IWL_DEV_INFO(iwl5100_abg_cfg, iwl5100_abg_name,
		     DEVICE(0x4237), SUBDEV_MASKED(0x6, 0xF)),

/* 5300 Series WiFi */
	IWL_DEV_INFO(iwl5300_agn_cfg, iwl5300_agn_name,
		     DEVICE(0x4235), SUBDEV_MASKED(0x1, 0xF)),
	IWL_DEV_INFO(iwl5300_agn_cfg, iwl5300_agn_name,
		     DEVICE(0x4235), SUBDEV_MASKED(0x4, 0xF)),
	IWL_DEV_INFO(iwl5300_agn_cfg, iwl5300_agn_name,
		     DEVICE(0x4236), SUBDEV_MASKED(0x1, 0xF)),
	IWL_DEV_INFO(iwl5300_agn_cfg, iwl5300_agn_name,
		     DEVICE(0x4236), SUBDEV_MASKED(0x4, 0xF)),

/* 5350 Series WiFi/WiMax */
	IWL_DEV_INFO(iwl5350_agn_cfg, iwl5350_agn_name,
		     DEVICE(0x423A)),
	IWL_DEV_INFO(iwl5350_agn_cfg, iwl5350_agn_name,
		     DEVICE(0x423B)),

/* 5150 Series Wifi/WiMax */
	IWL_DEV_INFO(iwl5150_agn_cfg, iwl5150_agn_name,
		     DEVICE(0x423C), SUBDEV_MASKED(0x1, 0xF)),
	IWL_DEV_INFO(iwl5150_abg_cfg, iwl5150_abg_name,
		     DEVICE(0x423C), SUBDEV_MASKED(0x6, 0xF)),

	IWL_DEV_INFO(iwl5150_agn_cfg, iwl5150_agn_name,
		     DEVICE(0x423D), SUBDEV_MASKED(0x1, 0xF)),
	IWL_DEV_INFO(iwl5150_abg_cfg, iwl5150_abg_name,
		     DEVICE(0x423D), SUBDEV_MASKED(0x6, 0xF)),

/* 6x00 Series */
	IWL_DEV_INFO(iwl6000_3agn_cfg, iwl6000_3agn_name,
		     DEVICE(0x422B), SUBDEV_MASKED(0x1, 0xF)),
	IWL_DEV_INFO(iwl6000_3agn_cfg, iwl6000_3agn_name,
		     DEVICE(0x422B), SUBDEV_MASKED(0x8, 0xF)),
	IWL_DEV_INFO(iwl6000i_2agn_cfg, iwl6000i_2agn_name,
		     DEVICE(0x422C), SUBDEV_MASKED(0x1, 0xF)),
	IWL_DEV_INFO(iwl6000i_non_n_cfg, iwl6000i_2abg_name,
		     DEVICE(0x422C), SUBDEV_MASKED(0x6, 0xF)),
	IWL_DEV_INFO(iwl6000i_non_n_cfg, iwl6000i_2bg_name,
		     DEVICE(0x422C), SUBDEV_MASKED(0x7, 0xF)),
	IWL_DEV_INFO(iwl6000_3agn_cfg, iwl6000_3agn_name,
		     DEVICE(0x4238), SUBDEV(0x1111)),
	IWL_DEV_INFO(iwl6000_3agn_cfg, iwl6000_3agn_name,
		     DEVICE(0x4238), SUBDEV(0x1118)),
	IWL_DEV_INFO(iwl6000i_2agn_cfg, iwl6000i_2agn_name,
		     DEVICE(0x4239), SUBDEV(0x1311)),
	IWL_DEV_INFO(iwl6000i_non_n_cfg, iwl6000i_2abg_name,
		     DEVICE(0x4239), SUBDEV(0x1316)),

/* 6x05 Series */
	IWL_DEV_INFO(iwl6005_n_cfg, iwl6005_2agn_name,
		     DEVICE(0x0082), SUBDEV_MASKED(0x1, 0xF)),
	IWL_DEV_INFO(iwl6005_non_n_cfg, iwl6005_2abg_name,
		     DEVICE(0x0082), SUBDEV_MASKED(0x6, 0xF)),
	IWL_DEV_INFO(iwl6005_non_n_cfg, iwl6005_2bg_name,
		     DEVICE(0x0082), SUBDEV_MASKED(0x7, 0xF)),
	IWL_DEV_INFO(iwl6005_n_cfg, iwl6005_2agn_name,
		     DEVICE(0x0082), SUBDEV_MASKED(0x8, 0xF)),

	IWL_DEV_INFO(iwl6005_n_cfg, iwl6005_2agn_name,
		     DEVICE(0x0085), SUBDEV_MASKED(0x1, 0xF)),
	IWL_DEV_INFO(iwl6005_n_cfg, iwl6005_2agn_name,
		     DEVICE(0x0085), SUBDEV_MASKED(0x8, 0xF)),
	IWL_DEV_INFO(iwl6005_non_n_cfg, iwl6005_2abg_name,
		     DEVICE(0x0085), SUBDEV_MASKED(0x6, 0xF)),

	IWL_DEV_INFO(iwl6005_n_cfg, iwl6005_2agn_sff_name,
		     DEVICE(0x0082), SUBDEV_MASKED(0xC000, 0xF000)),
	IWL_DEV_INFO(iwl6005_n_cfg, iwl6005_2agn_d_name,
		     DEVICE(0x0082), SUBDEV(0x4820)),
	IWL_DEV_INFO(iwl6005_n_cfg, iwl6005_2agn_mow1_name,
		     DEVICE(0x0082), SUBDEV(0x1304)),/* low 5GHz active */
	IWL_DEV_INFO(iwl6005_n_cfg, iwl6005_2agn_mow2_name,
		     DEVICE(0x0082), SUBDEV(0x1305)),/* high 5GHz active */

/* 6x30 Series */
	IWL_DEV_INFO(iwl6030_n_cfg, iwl1030_bgn_name,
		     DEVICE(0x008A), SUBDEV_MASKED(0x5, 0xF)),
	IWL_DEV_INFO(iwl6030_non_n_cfg, iwl1030_bg_name,
		     DEVICE(0x008A), SUBDEV_MASKED(0x7, 0xF)),
	IWL_DEV_INFO(iwl6030_n_cfg, iwl1030_bgn_name,
		     DEVICE(0x008B), SUBDEV(0x5315)),
	IWL_DEV_INFO(iwl6030_non_n_cfg, iwl1030_bg_name,
		     DEVICE(0x008B), SUBDEV(0x5317)),
	IWL_DEV_INFO(iwl6030_n_cfg, iwl6030_2agn_name,
		     DEVICE(0x0090), SUBDEV(0x5211)),
	IWL_DEV_INFO(iwl6030_n_cfg, iwl6030_2bgn_name,
		     DEVICE(0x0090), SUBDEV(0x5215)),
	IWL_DEV_INFO(iwl6030_non_n_cfg, iwl6030_2abg_name,
		     DEVICE(0x0090), SUBDEV(0x5216)),
	IWL_DEV_INFO(iwl6030_n_cfg, iwl6030_2agn_name,
		     DEVICE(0x0091), SUBDEV_MASKED(0x1, 0xF)),
	IWL_DEV_INFO(iwl6030_n_cfg, iwl6030_2bgn_name,
		     DEVICE(0x0091), SUBDEV_MASKED(0x5, 0xF)),
	IWL_DEV_INFO(iwl6030_non_n_cfg, iwl6030_2abg_name,
		     DEVICE(0x0091), SUBDEV_MASKED(0x6, 0xF)),
	IWL_DEV_INFO(iwl6030_non_n_cfg, iwl6030_2bg_name,
		     DEVICE(0x0091), SUBDEV(0x5207)),

/* 6x50 WiFi/WiMax Series */
	IWL_DEV_INFO(iwl6050_2agn_cfg, iwl6050_2agn_name,
		     DEVICE(0x0087), SUBDEV_MASKED(0x1, 0xF)),
	IWL_DEV_INFO(iwl6050_2abg_cfg, iwl6050_2abg_name,
		     DEVICE(0x0087), SUBDEV_MASKED(0x6, 0xF)),
	IWL_DEV_INFO(iwl6050_2agn_cfg, iwl6050_2agn_name,
		     DEVICE(0x0089), SUBDEV(0x1311)),
	IWL_DEV_INFO(iwl6050_2abg_cfg, iwl6050_2abg_name,
		     DEVICE(0x0089), SUBDEV(0x1316)),

/* 6150 WiFi/WiMax Series */
	IWL_DEV_INFO(iwl6150_bgn_cfg, iwl6150_bgn_name,
		     DEVICE(0x0885), SUBDEV_MASKED(0x5, 0xF)),
	IWL_DEV_INFO(iwl6150_bg_cfg, iwl6150_bg_name,
		     DEVICE(0x0885), SUBDEV_MASKED(0x7, 0xF)),
	IWL_DEV_INFO(iwl6150_bgn_cfg, iwl6150_bgn_name,
		     DEVICE(0x0886), SUBDEV(0x1315)),
	IWL_DEV_INFO(iwl6150_bg_cfg, iwl6150_bg_name,
		     DEVICE(0x0886), SUBDEV(0x1317)),

/* 1000 Series WiFi */
	IWL_DEV_INFO(iwl1000_bgn_cfg, iwl1000_bgn_name,
		     DEVICE(0x0083), SUBDEV_MASKED(0x5, 0xF)),
	IWL_DEV_INFO(iwl1000_bg_cfg, iwl1000_bg_name,
		     DEVICE(0x0083), SUBDEV_MASKED(0x6, 0xF)),
	IWL_DEV_INFO(iwl1000_bg_cfg, iwl1000_bg_name,
		     DEVICE(0x0084), SUBDEV(0x1216)),
	IWL_DEV_INFO(iwl1000_bg_cfg, iwl1000_bg_name,
		     DEVICE(0x0084), SUBDEV(0x1316)),

/* 100 Series WiFi */
	IWL_DEV_INFO(iwl100_bgn_cfg, iwl100_bgn_name,
		     DEVICE(0x08AE), SUBDEV_MASKED(0x5, 0xF)),
	IWL_DEV_INFO(iwl100_bg_cfg, iwl100_bg_name,
		     DEVICE(0x08AE), SUBDEV_MASKED(0x7, 0xF)),
	IWL_DEV_INFO(iwl100_bgn_cfg, iwl100_bgn_name,
		     DEVICE(0x08AF), SUBDEV(0x1015)),
	IWL_DEV_INFO(iwl100_bg_cfg, iwl100_bg_name,
		     DEVICE(0x08AF), SUBDEV(0x1017)),

/* 130 Series WiFi */
	IWL_DEV_INFO(iwl130_bgn_cfg, iwl130_bgn_name,
		     DEVICE(0x0896), SUBDEV_MASKED(0x5, 0xF)),
	IWL_DEV_INFO(iwl130_bg_cfg, iwl130_bg_name,
		     DEVICE(0x0896), SUBDEV_MASKED(0x7, 0xF)),
	IWL_DEV_INFO(iwl130_bgn_cfg, iwl130_bgn_name,
		     DEVICE(0x0897), SUBDEV(0x5015)),
	IWL_DEV_INFO(iwl130_bg_cfg, iwl130_bg_name,
		     DEVICE(0x0897), SUBDEV(0x5017)),

/* 2x00 Series */
	IWL_DEV_INFO(iwl2000_2bgn_cfg, iwl2000_2bgn_name,
		     DEVICE(0x0890), SUBDEV(0x4022)),
	IWL_DEV_INFO(iwl2000_2bgn_cfg, iwl2000_2bgn_name,
		     DEVICE(0x0891), SUBDEV(0x4222)),
	IWL_DEV_INFO(iwl2000_2bgn_cfg, iwl2000_2bgn_name,
		     DEVICE(0x0890), SUBDEV(0x4422)),
	IWL_DEV_INFO(iwl2000_2bgn_cfg, iwl2000_2bgn_d_name,
		     DEVICE(0x0890), SUBDEV(0x4822)),

/* 2x30 Series */
	IWL_DEV_INFO(iwl2030_2bgn_cfg, iwl2030_2bgn_name,
		     DEVICE(0x0887)),
	IWL_DEV_INFO(iwl2030_2bgn_cfg, iwl2030_2bgn_name,
		     DEVICE(0x0888), SUBDEV(0x4262)),

/* 6x35 Series */
	IWL_DEV_INFO(iwl6035_2agn_cfg, iwl6035_2agn_name,
		     DEVICE(0x088E), SUBDEV_MASKED(0x0, 0xF)),
	IWL_DEV_INFO(iwl6035_2agn_cfg, iwl6035_2agn_sff_name,
		     DEVICE(0x088E), SUBDEV_MASKED(0xA, 0xF)),
	IWL_DEV_INFO(iwl6035_2agn_cfg, iwl6035_2agn_name,
		     DEVICE(0x088F), SUBDEV_MASKED(0x0, 0xF)),
	IWL_DEV_INFO(iwl6035_2agn_cfg, iwl6035_2agn_sff_name,
		     DEVICE(0x088F), SUBDEV_MASKED(0xA, 0xF)),

/* 105 Series */
	IWL_DEV_INFO(iwl105_bgn_cfg, iwl105_bgn_name,
		     DEVICE(0x0894)),
	IWL_DEV_INFO(iwl105_bgn_cfg, iwl105_bgn_name,
		     DEVICE(0x0895), SUBDEV(0x0222)),

/* 135 Series */
	IWL_DEV_INFO(iwl135_bgn_cfg, iwl135_bgn_name,
		     DEVICE(0x0892)),
	IWL_DEV_INFO(iwl135_bgn_cfg, iwl135_bgn_name,
		     DEVICE(0x0893), SUBDEV(0x0262)),
#endif /* CONFIG_IWLDVM */

#if IS_ENABLED(CONFIG_IWLMVM)
/* 7260 Series */
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B1)), // unlisted ones fall through to here
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x4060)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x406A)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x4160)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B1), SUBDEV(0x4062)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B1), SUBDEV(0x4162)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x4460)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x446A)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B1), SUBDEV(0x4462)),
	IWL_DEV_INFO(iwl7260_high_temp_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B1), SUBDEV(0x4A70)),
	IWL_DEV_INFO(iwl7260_high_temp_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B1), SUBDEV(0x4A6E)),
	IWL_DEV_INFO(iwl7260_high_temp_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B1), SUBDEV(0x4A6C)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x4560)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x4020)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x402A)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0x4420)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC060)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC06A)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC160)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B1), SUBDEV(0xC062)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B1), SUBDEV(0xC162)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC760)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC460)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B1), SUBDEV(0xC462)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC560)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC360)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC020)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC02A)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B1), SUBDEV(0xC420)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B2), SUBDEV(0x4270)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B2), SUBDEV(0x4272)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B2), SUBDEV(0x4260)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B2), SUBDEV(0x426A)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B2), SUBDEV(0x4262)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B2), SUBDEV(0x4370)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B2), SUBDEV(0x4360)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B2), SUBDEV(0x4220)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B2), SUBDEV(0xC270)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B2), SUBDEV(0xC272)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B2), SUBDEV(0xC260)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B2), SUBDEV(0xC26A)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_n_name,
		     DEVICE(0x08B2), SUBDEV(0xC262)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2ac_name,
		     DEVICE(0x08B2), SUBDEV(0xC370)),
	IWL_DEV_INFO(iwl7260_cfg, iwl7260_2n_name,
		     DEVICE(0x08B2), SUBDEV(0xC220)),

/* 3160 Series */
	IWL_DEV_INFO(iwl3160_cfg, iwl3160_2ac_name,
		     DEVICE(0x08B3)),

	IWL_DEV_INFO(iwl3160_cfg, iwl3160_n_name,
		     DEVICE(0x08B3), SUBDEV_MASKED(0x62, 0xFF)),
	IWL_DEV_INFO(iwl3160_cfg, iwl3160_2n_name,
		     DEVICE(0x08B3), SUBDEV_MASKED(0x60, 0xFF)),

	IWL_DEV_INFO(iwl3160_cfg, iwl3160_2ac_name,
		     DEVICE(0x08B4)),

/* 3165 Series */
	IWL_DEV_INFO(iwl3165_2ac_cfg, iwl3165_2ac_name,
		     DEVICE(0x3165)),
	IWL_DEV_INFO(iwl3165_2ac_cfg, iwl3165_2ac_name,
		     DEVICE(0x3166)),

/* 3168 Series */
	IWL_DEV_INFO(iwl3168_2ac_cfg, iwl3168_2ac_name,
		     DEVICE(0x24FB)),

/* 7265 Series */
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2ac_name,
		     DEVICE(0x095A)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5000)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x500A)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_n_name,
		     DEVICE(0x095A), SUBDEV(0x5002)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_n_name,
		     DEVICE(0x095A), SUBDEV(0x5102)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5020)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x502A)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5090)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5190)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5100)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5400)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5420)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5490)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5C10)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x5590)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x9000)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x900A)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095A), SUBDEV(0x9400)),

	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2ac_name,
		     DEVICE(0x095B)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095B), SUBDEV(0x520A)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_n_name,
		     DEVICE(0x095B), SUBDEV(0x5302)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095B), SUBDEV(0x5200)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_n_name,
		     DEVICE(0x095B), SUBDEV(0x5202)),
	IWL_DEV_INFO(iwl7265_cfg, iwl7265_2n_name,
		     DEVICE(0x095B), SUBDEV(0x9200)),

/* 8000 Series */
	IWL_DEV_INFO(iwl8260_cfg, iwl8260_2ac_name,
		     DEVICE(0x24F3)),
	IWL_DEV_INFO(iwl8260_cfg, iwl8260_2n_name,
		     DEVICE(0x24F3), SUBDEV(0x0004)),
	IWL_DEV_INFO(iwl8260_cfg, iwl8260_2n_name,
		     DEVICE(0x24F3), SUBDEV(0x0044)),
	IWL_DEV_INFO(iwl8265_cfg, iwl8265_2ac_name,
		     DEVICE(0x24FD)),
	IWL_DEV_INFO(iwl8265_cfg, iwl8275_2ac_name,
		     DEVICE(0x24FD), SUBDEV(0x3E02)),
	IWL_DEV_INFO(iwl8265_cfg, iwl8275_2ac_name,
		     DEVICE(0x24FD), SUBDEV(0x3E01)),
	IWL_DEV_INFO(iwl8265_cfg, iwl8275_2ac_name,
		     DEVICE(0x24FD), SUBDEV(0x1012)),
	IWL_DEV_INFO(iwl8265_cfg, iwl8275_2ac_name,
		     DEVICE(0x24FD), SUBDEV(0x0012)),
	IWL_DEV_INFO(iwl8265_cfg, iwl_killer_1435i_name,
		     DEVICE(0x24FD), SUBDEV(0x1431)),
	IWL_DEV_INFO(iwl8265_cfg, iwl_killer_1434_kix_name,
		     DEVICE(0x24FD), SUBDEV(0x1432)),

/* JF1 RF */
	IWL_DEV_INFO(iwl_rf_jf, iwl9461_160_name,
		     RF_TYPE(JF1)),
	IWL_DEV_INFO(iwl_rf_jf_80mhz, iwl9461_name,
		     RF_TYPE(JF1), BW_LIMITED),
	IWL_DEV_INFO(iwl_rf_jf, iwl9462_160_name,
		     RF_TYPE(JF1), RF_ID(JF1_DIV)),
	IWL_DEV_INFO(iwl_rf_jf_80mhz, iwl9462_name,
		     RF_TYPE(JF1), RF_ID(JF1_DIV), BW_LIMITED),
/* JF2 RF */
	IWL_DEV_INFO(iwl_rf_jf, iwl9260_160_name,
		     RF_TYPE(JF2)),
	IWL_DEV_INFO(iwl_rf_jf_80mhz, iwl9260_name,
		     RF_TYPE(JF2), BW_LIMITED),
	IWL_DEV_INFO(iwl_rf_jf, iwl9560_160_name,
		     RF_TYPE(JF2), RF_ID(JF)),
	IWL_DEV_INFO(iwl_rf_jf_80mhz, iwl9560_name,
		     RF_TYPE(JF2), RF_ID(JF), BW_LIMITED),

/* HR RF */
	IWL_DEV_INFO(iwl_rf_hr, iwl_ax201_name, RF_TYPE(HR2)),
	IWL_DEV_INFO(iwl_rf_hr_80mhz, iwl_ax101_name, RF_TYPE(HR1)),
	IWL_DEV_INFO(iwl_rf_hr_80mhz, iwl_ax203_name, RF_TYPE(HR2), BW_LIMITED),
	IWL_DEV_INFO(iwl_rf_hr, iwl_ax200_name, DEVICE(0x2723)),

/* GF RF */
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax211_name, RF_TYPE(GF)),
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax411_name, RF_TYPE(GF), CDB),
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax210_name, DEVICE(0x2725)),

/* Killer CRFs */
	IWL_DEV_INFO(iwl_rf_jf, iwl9260_killer_1550_name, SUBDEV(0x1550)),
	IWL_DEV_INFO(iwl_rf_jf, iwl9560_killer_1550s_name, SUBDEV(0x1551)),
	IWL_DEV_INFO(iwl_rf_jf, iwl9560_killer_1550i_name, SUBDEV(0x1552)),

	IWL_DEV_INFO(iwl_rf_hr, iwl_ax201_killer_1650s_name, SUBDEV(0x1651)),
	IWL_DEV_INFO(iwl_rf_hr, iwl_ax201_killer_1650i_name, SUBDEV(0x1652)),

	IWL_DEV_INFO(iwl_rf_gf, iwl_ax211_killer_1675s_name, SUBDEV(0x1671)),
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax211_killer_1675i_name, SUBDEV(0x1672)),
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax210_killer_1675w_name, SUBDEV(0x1673)),
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax210_killer_1675x_name, SUBDEV(0x1674)),
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax411_killer_1690s_name, SUBDEV(0x1691)),
	IWL_DEV_INFO(iwl_rf_gf, iwl_ax411_killer_1690i_name, SUBDEV(0x1692)),

/* Killer discrete */
	IWL_DEV_INFO(iwl_rf_hr, iwl_ax200_killer_1650w_name,
		     DEVICE(0x2723), SUBDEV(0x1653)),
	IWL_DEV_INFO(iwl_rf_hr, iwl_ax200_killer_1650x_name,
		     DEVICE(0x2723), SUBDEV(0x1654)),
#endif /* CONFIG_IWLMVM */
#if IS_ENABLED(CONFIG_IWLMLD)
/* FM RF */
	IWL_DEV_INFO(iwl_rf_fm, iwl_be201_name, RF_TYPE(FM)),
	IWL_DEV_INFO(iwl_rf_fm, iwl_be401_name, RF_TYPE(FM), CDB),
	/* the discrete NICs got the RF B0, it's only for the name anyway */
	IWL_DEV_INFO(iwl_rf_fm, iwl_be200_name, RF_TYPE(FM),
		     DEVICE(0x272B), RF_STEP(B)),
	IWL_DEV_INFO(iwl_rf_fm_160mhz, iwl_be202_name,
		     RF_TYPE(FM), BW_LIMITED),

/* Killer CRFs */
	IWL_DEV_INFO(iwl_rf_fm, iwl_killer_be1750s_name, SUBDEV(0x1771)),
	IWL_DEV_INFO(iwl_rf_fm, iwl_killer_be1750i_name, SUBDEV(0x1772)),
	IWL_DEV_INFO(iwl_rf_fm, iwl_killer_be1790s_name, SUBDEV(0x1791)),
	IWL_DEV_INFO(iwl_rf_fm, iwl_killer_be1790i_name, SUBDEV(0x1792)),

/* Killer discrete */
	IWL_DEV_INFO(iwl_rf_fm, iwl_killer_be1750w_name,
		     DEVICE(0x272B), SUBDEV(0x1773)),
	IWL_DEV_INFO(iwl_rf_fm, iwl_killer_be1750x_name,
		     DEVICE(0x272B), SUBDEV(0x1774)),

/* WH RF */
	IWL_DEV_INFO(iwl_rf_wh, iwl_be211_name, RF_TYPE(WH)),
	IWL_DEV_INFO(iwl_rf_wh_160mhz, iwl_be213_name, RF_TYPE(WH), BW_LIMITED),

/* PE RF */
	IWL_DEV_INFO(iwl_rf_pe, iwl_bn201_name, RF_TYPE(PE)),
	IWL_DEV_INFO(iwl_rf_pe, iwl_be223_name, RF_TYPE(PE), SUBDEV(0x0524)),
	IWL_DEV_INFO(iwl_rf_pe, iwl_be221_name, RF_TYPE(PE), SUBDEV(0x0324)),

/* Killer */
	IWL_DEV_INFO(iwl_rf_wh, iwl_killer_be1775s_name, SUBDEV(0x1776)),
	IWL_DEV_INFO(iwl_rf_wh, iwl_killer_be1775i_name, SUBDEV(0x1775)),

	IWL_DEV_INFO(iwl_rf_pe, iwl_killer_bn1850w2_name, SUBDEV(0x1851)),
	IWL_DEV_INFO(iwl_rf_pe, iwl_killer_bn1850i_name, SUBDEV(0x1852)),
#endif /* CONFIG_IWLMLD */
};
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_dev_info_table);

#if IS_ENABLED(CONFIG_IWLWIFI_KUNIT_TESTS)
const unsigned int iwl_dev_info_table_size = ARRAY_SIZE(iwl_dev_info_table);
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_dev_info_table_size);
#endif

/*
 * Read rf id and cdb info from prph register and store it
 */
static void get_crf_id(struct iwl_trans *iwl_trans,
		       struct iwl_trans_info *info)
{
	u32 sd_reg_ver_addr;
	u32 hw_wfpm_id;
	u32 val = 0;
	u8 step;

	if (iwl_trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_AX210)
		sd_reg_ver_addr = SD_REG_VER_GEN2;
	else
		sd_reg_ver_addr = SD_REG_VER;

	/* Enable access to peripheral registers */
	val = iwl_read_umac_prph_no_grab(iwl_trans, WFPM_CTRL_REG);
	val |= WFPM_AUX_CTL_AUX_IF_MAC_OWNER_MSK;
	iwl_write_umac_prph_no_grab(iwl_trans, WFPM_CTRL_REG, val);

	/* Read crf info */
	info->hw_crf_id = iwl_read_prph_no_grab(iwl_trans, sd_reg_ver_addr);

	/* Read cnv info */
	info->hw_cnv_id = iwl_read_prph_no_grab(iwl_trans, CNVI_AUX_MISC_CHIP);

	/* For BZ-W, take B step also when A step is indicated */
	if (CSR_HW_REV_TYPE(info->hw_rev) == IWL_CFG_MAC_TYPE_BZ_W)
		step = SILICON_B_STEP;

	/* In BZ, the MAC step must be read from the CNVI aux register */
	if (CSR_HW_REV_TYPE(info->hw_rev) == IWL_CFG_MAC_TYPE_BZ) {
		step = CNVI_AUX_MISC_CHIP_MAC_STEP(info->hw_cnv_id);

		/* For BZ-U, take B step also when A step is indicated */
		if ((CNVI_AUX_MISC_CHIP_PROD_TYPE(info->hw_cnv_id) ==
		    CNVI_AUX_MISC_CHIP_PROD_TYPE_BZ_U) &&
		    step == SILICON_A_STEP)
			step = SILICON_B_STEP;
	}

	if (CSR_HW_REV_TYPE(info->hw_rev) == IWL_CFG_MAC_TYPE_BZ ||
	    CSR_HW_REV_TYPE(info->hw_rev) == IWL_CFG_MAC_TYPE_BZ_W) {
		info->hw_rev_step = step;
		info->hw_rev |= step;
	}

	/* Read cdb info (also contains the jacket info if needed in the future */
	hw_wfpm_id = iwl_read_umac_prph_no_grab(iwl_trans, WFPM_OTP_CFG1_ADDR);
	IWL_INFO(iwl_trans, "Detected crf-id 0x%x, cnv-id 0x%x wfpm id 0x%x\n",
		 info->hw_crf_id, info->hw_cnv_id, hw_wfpm_id);
}

/*
 * In case that there is no OTP on the NIC, map the rf id and cdb info
 * from the prph registers.
 */
static int map_crf_id(struct iwl_trans *iwl_trans,
		      struct iwl_trans_info *info)
{
	int ret = 0;
	u32 val = info->hw_crf_id;
	u32 step_id = REG_CRF_ID_STEP(val);
	u32 slave_id = REG_CRF_ID_SLAVE(val);
	u32 jacket_id_cnv = REG_CRF_ID_SLAVE(info->hw_cnv_id);
	u32 hw_wfpm_id = iwl_read_umac_prph_no_grab(iwl_trans,
						    WFPM_OTP_CFG1_ADDR);
	u32 jacket_id_wfpm = WFPM_OTP_CFG1_IS_JACKET(hw_wfpm_id);
	u32 cdb_id_wfpm = WFPM_OTP_CFG1_IS_CDB(hw_wfpm_id);

	/* Map between crf id to rf id */
	switch (REG_CRF_ID_TYPE(val)) {
	case REG_CRF_ID_TYPE_JF_1:
		info->hw_rf_id = (IWL_CFG_RF_TYPE_JF1 << 12);
		break;
	case REG_CRF_ID_TYPE_JF_2:
		info->hw_rf_id = (IWL_CFG_RF_TYPE_JF2 << 12);
		break;
	case REG_CRF_ID_TYPE_HR_NONE_CDB_1X1:
		info->hw_rf_id = (IWL_CFG_RF_TYPE_HR1 << 12);
		break;
	case REG_CRF_ID_TYPE_HR_NONE_CDB:
		info->hw_rf_id = (IWL_CFG_RF_TYPE_HR2 << 12);
		break;
	case REG_CRF_ID_TYPE_HR_CDB:
		info->hw_rf_id = (IWL_CFG_RF_TYPE_HR2 << 12);
		break;
	case REG_CRF_ID_TYPE_GF:
		info->hw_rf_id = (IWL_CFG_RF_TYPE_GF << 12);
		break;
	case REG_CRF_ID_TYPE_FM:
		info->hw_rf_id = (IWL_CFG_RF_TYPE_FM << 12);
		break;
	case REG_CRF_ID_TYPE_WHP:
		info->hw_rf_id = (IWL_CFG_RF_TYPE_WH << 12);
		break;
	case REG_CRF_ID_TYPE_PE:
		info->hw_rf_id = (IWL_CFG_RF_TYPE_PE << 12);
		break;
	default:
		ret = -EIO;
		IWL_ERR(iwl_trans,
			"Can't find a correct rfid for crf id 0x%x\n",
			REG_CRF_ID_TYPE(val));
		goto out;

	}

	/* Set Step-id */
	info->hw_rf_id |= (step_id << 8);

	/* Set CDB capabilities */
	if (cdb_id_wfpm || slave_id) {
		info->hw_rf_id += BIT(28);
		IWL_INFO(iwl_trans, "Adding cdb to rf id\n");
	}

	/* Set Jacket capabilities */
	if (jacket_id_wfpm || jacket_id_cnv) {
		info->hw_rf_id += BIT(29);
		IWL_INFO(iwl_trans, "Adding jacket to rf id\n");
	}

	IWL_INFO(iwl_trans,
		 "Detected rf-type 0x%x step-id 0x%x slave-id 0x%x from crf id 0x%x\n",
		 REG_CRF_ID_TYPE(val), step_id, slave_id, info->hw_rf_id);
	IWL_INFO(iwl_trans,
		 "Detected cdb-id 0x%x jacket-id 0x%x from wfpm id 0x%x\n",
		 cdb_id_wfpm, jacket_id_wfpm, hw_wfpm_id);
	IWL_INFO(iwl_trans, "Detected jacket-id 0x%x from cnvi id 0x%x\n",
		 jacket_id_cnv, info->hw_cnv_id);

out:
	return ret;
}

/* PCI registers */
#define PCI_CFG_RETRY_TIMEOUT	0x041

VISIBLE_IF_IWLWIFI_KUNIT const struct iwl_dev_info *
iwl_pci_find_dev_info(u16 device, u16 subsystem_device, u16 rf_type, u8 cdb,
		      u8 rf_id, u8 bw_limit, u8 rf_step)
{
	int num_devices = ARRAY_SIZE(iwl_dev_info_table);
	int i;

	if (!num_devices)
		return NULL;

	for (i = num_devices - 1; i >= 0; i--) {
		const struct iwl_dev_info *dev_info = &iwl_dev_info_table[i];
		u16 subdevice_mask;

		if (dev_info->device != (u16)IWL_CFG_ANY &&
		    dev_info->device != device)
			continue;

		subdevice_mask = GENMASK(dev_info->subdevice_m_h,
					 dev_info->subdevice_m_l);

		if (dev_info->subdevice != (u16)IWL_CFG_ANY &&
		    dev_info->subdevice != (subsystem_device & subdevice_mask))
			continue;

		if (dev_info->match_rf_type && dev_info->rf_type != rf_type)
			continue;

		if (dev_info->match_cdb && dev_info->cdb != cdb)
			continue;

		if (dev_info->match_rf_id && dev_info->rf_id != rf_id)
			continue;

		if (dev_info->match_bw_limit && dev_info->bw_limit != bw_limit)
			continue;

		if (dev_info->match_rf_step && dev_info->rf_step != rf_step)
			continue;

		return dev_info;
	}

	return NULL;
}
EXPORT_SYMBOL_IF_IWLWIFI_KUNIT(iwl_pci_find_dev_info);

static void iwl_pcie_recheck_me_status(struct work_struct *wk)
{
	struct iwl_trans_pcie *trans_pcie = container_of(wk,
							 typeof(*trans_pcie),
							 me_recheck_wk.work);
	u32 val;

	val = iwl_read32(trans_pcie->trans, CSR_HW_IF_CONFIG_REG);
	trans_pcie->me_present = !!(val & CSR_HW_IF_CONFIG_REG_IAMT_UP);
}

static void iwl_pcie_check_me_status(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	u32 val;

	trans_pcie->me_present = -1;

	INIT_DELAYED_WORK(&trans_pcie->me_recheck_wk,
			  iwl_pcie_recheck_me_status);

	/* we don't have a good way of determining this until BZ */
	if (trans->mac_cfg->device_family < IWL_DEVICE_FAMILY_BZ)
		return;

	val = iwl_read_prph(trans, CNVI_SCU_REG_FOR_ECO_1);
	if (val & CNVI_SCU_REG_FOR_ECO_1_WIAMT_KNOWN) {
		trans_pcie->me_present =
			!!(val & CNVI_SCU_REG_FOR_ECO_1_WIAMT_PRESENT);
		return;
	}

	val = iwl_read32(trans, CSR_HW_IF_CONFIG_REG);
	if (val & (CSR_HW_IF_CONFIG_REG_ME_OWN |
		   CSR_HW_IF_CONFIG_REG_IAMT_UP)) {
		trans_pcie->me_present = 1;
		return;
	}

	/* recheck again later, ME might still be initializing */
	schedule_delayed_work(&trans_pcie->me_recheck_wk, HZ);
}

static int iwl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct iwl_mac_cfg *trans;
	const struct iwl_dev_info *dev_info;
	struct iwl_trans_info info = {
		.hw_id = (pdev->device << 16) + pdev->subsystem_device,
	};
	struct iwl_trans *iwl_trans;
	struct iwl_trans_pcie *trans_pcie;
	int ret;

	trans = (void *)ent->driver_data;

	iwl_trans = iwl_trans_pcie_alloc(pdev, trans, &info);
	if (IS_ERR(iwl_trans))
		return PTR_ERR(iwl_trans);

	trans_pcie = IWL_TRANS_GET_PCIE_TRANS(iwl_trans);

	iwl_trans_pcie_check_product_reset_status(pdev);
	iwl_trans_pcie_check_product_reset_mode(pdev);

	/* set the things we know so far for the grab NIC access */
	iwl_trans_set_info(iwl_trans, &info);

	/*
	 * Let's try to grab NIC access early here. Sometimes, NICs may
	 * fail to initialize, and if that happens it's better if we see
	 * issues early on (and can reprobe, per the logic inside), than
	 * first trying to load the firmware etc. and potentially only
	 * detecting any problems when the first interface is brought up.
	 */
	ret = iwl_pcie_prepare_card_hw(iwl_trans);
	if (!ret) {
		ret = iwl_finish_nic_init(iwl_trans);
		if (ret)
			goto out_free_trans;
		if (iwl_trans_grab_nic_access(iwl_trans)) {
			get_crf_id(iwl_trans, &info);
			/* all good */
			iwl_trans_release_nic_access(iwl_trans);
		} else {
			ret = -EIO;
			goto out_free_trans;
		}
	}

	info.hw_rf_id = iwl_read32(iwl_trans, CSR_HW_RF_ID);

	/*
	 * The RF_ID is set to zero in blank OTP so read version to
	 * extract the RF_ID.
	 * This is relevant only for family 9000 and up.
	 */
	if (iwl_trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_9000 &&
	    !CSR_HW_RFID_TYPE(info.hw_rf_id) && map_crf_id(iwl_trans, &info)) {
		ret = -EINVAL;
		goto out_free_trans;
	}

	IWL_INFO(iwl_trans, "PCI dev %04x/%04x, rev=0x%x, rfid=0x%x\n",
		 pdev->device, pdev->subsystem_device,
		 info.hw_rev, info.hw_rf_id);

	dev_info = iwl_pci_find_dev_info(pdev->device, pdev->subsystem_device,
					 CSR_HW_RFID_TYPE(info.hw_rf_id),
					 CSR_HW_RFID_IS_CDB(info.hw_rf_id),
					 IWL_SUBDEVICE_RF_ID(pdev->subsystem_device),
					 IWL_SUBDEVICE_BW_LIM(pdev->subsystem_device),
					 CSR_HW_RFID_STEP(info.hw_rf_id));
	if (dev_info) {
		iwl_trans->cfg = dev_info->cfg;
		info.name = dev_info->name;
	}

#if IS_ENABLED(CONFIG_IWLMVM)
	/*
	 * special-case 7265D, it has the same PCI IDs.
	 *
	 * Note that because we already pass the cfg to the transport above,
	 * all the parameters that the transport uses must, until that is
	 * changed, be identical to the ones in the 7265D configuration.
	 */
	if (iwl_trans->cfg == &iwl7265_cfg &&
	    (info.hw_rev & CSR_HW_REV_TYPE_MSK) == CSR_HW_REV_TYPE_7265D)
		iwl_trans->cfg = &iwl7265d_cfg;
#endif
	if (!iwl_trans->cfg) {
		pr_err("No config found for PCI dev %04x/%04x, rev=0x%x, rfid=0x%x\n",
		       pdev->device, pdev->subsystem_device,
		       info.hw_rev, info.hw_rf_id);
		ret = -EINVAL;
		goto out_free_trans;
	}

	IWL_INFO(iwl_trans, "Detected %s\n", info.name);

	if (iwl_trans->mac_cfg->mq_rx_supported) {
		if (WARN_ON(!iwl_trans->cfg->num_rbds)) {
			ret = -EINVAL;
			goto out_free_trans;
		}
		trans_pcie->num_rx_bufs = iwl_trans_get_num_rbds(iwl_trans);
	} else {
		trans_pcie->num_rx_bufs = RX_QUEUE_SIZE;
	}

	if (!iwl_trans->mac_cfg->integrated) {
		u16 link_status;

		pcie_capability_read_word(pdev, PCI_EXP_LNKSTA, &link_status);

		info.pcie_link_speed =
			u16_get_bits(link_status, PCI_EXP_LNKSTA_CLS);
	}

	iwl_trans_set_info(iwl_trans, &info);

	ret = iwl_trans_init(iwl_trans);
	if (ret)
		goto out_free_trans;

	pci_set_drvdata(pdev, iwl_trans);

	iwl_pcie_check_me_status(iwl_trans);

	/* try to get ownership so that we'll know if we don't own it */
	iwl_pcie_prepare_card_hw(iwl_trans);

	iwl_trans->drv = iwl_drv_start(iwl_trans);

	if (IS_ERR(iwl_trans->drv)) {
		ret = PTR_ERR(iwl_trans->drv);
		goto out_free_trans;
	}

	/* register transport layer debugfs here */
	iwl_trans_pcie_dbgfs_register(iwl_trans);

	return 0;

out_free_trans:
	iwl_trans_pcie_free(iwl_trans);
	return ret;
}

static void iwl_pci_remove(struct pci_dev *pdev)
{
	struct iwl_trans *trans = pci_get_drvdata(pdev);
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	if (!trans)
		return;

	cancel_delayed_work_sync(&trans_pcie->me_recheck_wk);

	iwl_drv_stop(trans->drv);

	iwl_trans_pcie_free(trans);
}

#ifdef CONFIG_PM_SLEEP

static int iwl_pci_suspend(struct device *device)
{
	/* Before you put code here, think about WoWLAN. You cannot check here
	 * whether WoWLAN is enabled or not, and your code will run even if
	 * WoWLAN is enabled - don't kill the NIC, someone may need it in Sx.
	 */

	return 0;
}

static int _iwl_pci_resume(struct device *device, bool restore)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct iwl_trans *trans = pci_get_drvdata(pdev);
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	bool device_was_powered_off = false;

	/* Before you put code here, think about WoWLAN. You cannot check here
	 * whether WoWLAN is enabled or not, and your code will run even if
	 * WoWLAN is enabled - the NIC may be alive.
	 */

	/*
	 * We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

	if (!trans->op_mode)
		return 0;

	/*
	 * Scratch value was altered, this means the device was powered off, we
	 * need to reset it completely.
	 * Note: MAC (bits 0:7) will be cleared upon suspend even with wowlan,
	 * but not bits [15:8]. So if we have bits set in lower word, assume
	 * the device is alive.
	 * For older devices, just try silently to grab the NIC.
	 */
	if (trans->mac_cfg->device_family >= IWL_DEVICE_FAMILY_BZ) {
		if (!(iwl_read32(trans, CSR_FUNC_SCRATCH) &
		      CSR_FUNC_SCRATCH_POWER_OFF_MASK))
			device_was_powered_off = true;
	} else {
		/*
		 * bh are re-enabled by iwl_trans_pcie_release_nic_access,
		 * so re-enable them if _iwl_trans_pcie_grab_nic_access fails.
		 */
		local_bh_disable();
		if (_iwl_trans_pcie_grab_nic_access(trans, true)) {
			iwl_trans_pcie_release_nic_access(trans);
		} else {
			device_was_powered_off = true;
			local_bh_enable();
		}
	}

	if (restore || device_was_powered_off) {
		trans->state = IWL_TRANS_NO_FW;
		/* Hope for the best here ... If one of those steps fails we
		 * won't really know how to recover.
		 */
		iwl_pcie_prepare_card_hw(trans);
		iwl_finish_nic_init(trans);
		iwl_op_mode_device_powered_off(trans->op_mode);
	}

	/* In WOWLAN, let iwl_trans_pcie_d3_resume do the rest of the work */
	if (test_bit(STATUS_DEVICE_ENABLED, &trans->status))
		return 0;

	/* reconfigure the MSI-X mapping to get the correct IRQ for rfkill */
	iwl_pcie_conf_msix_hw(trans_pcie);

	/*
	 * Enable rfkill interrupt (in order to keep track of the rfkill
	 * status). Must be locked to avoid processing a possible rfkill
	 * interrupt while in iwl_pcie_check_hw_rf_kill().
	 */
	mutex_lock(&trans_pcie->mutex);
	iwl_enable_rfkill_int(trans);
	iwl_pcie_check_hw_rf_kill(trans);
	mutex_unlock(&trans_pcie->mutex);

	return 0;
}

static int iwl_pci_restore(struct device *device)
{
	return _iwl_pci_resume(device, true);
}

static int iwl_pci_resume(struct device *device)
{
	return _iwl_pci_resume(device, false);
}

static const struct dev_pm_ops iwl_dev_pm_ops = {
	.suspend = pm_sleep_ptr(iwl_pci_suspend),
	.resume = pm_sleep_ptr(iwl_pci_resume),
	.freeze = pm_sleep_ptr(iwl_pci_suspend),
	.thaw = pm_sleep_ptr(iwl_pci_resume),
	.poweroff = pm_sleep_ptr(iwl_pci_suspend),
	.restore = pm_sleep_ptr(iwl_pci_restore),
};

#define IWL_PM_OPS	(&iwl_dev_pm_ops)

#else /* CONFIG_PM_SLEEP */

#define IWL_PM_OPS	NULL

#endif /* CONFIG_PM_SLEEP */

static struct pci_driver iwl_pci_driver = {
	.name = DRV_NAME,
	.id_table = iwl_hw_card_ids,
	.probe = iwl_pci_probe,
	.remove = iwl_pci_remove,
	.driver.pm = IWL_PM_OPS,
};

int __must_check iwl_pci_register_driver(void)
{
	int ret;
	ret = pci_register_driver(&iwl_pci_driver);
	if (ret)
		pr_err("Unable to initialize PCI module\n");

	return ret;
}

void iwl_pci_unregister_driver(void)
{
	pci_unregister_driver(&iwl_pci_driver);
}
