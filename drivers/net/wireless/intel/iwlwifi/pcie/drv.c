/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016-2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of version 2 of the GNU General Public License as
 * published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <linuxwifi@intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2014 Intel Corporation. All rights reserved.
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * All rights reserved.
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2018 - 2019 Intel Corporation
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *  * Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 *  * Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in
 *    the documentation and/or other materials provided with the
 *    distribution.
 *  * Neither the name Intel Corporation nor the names of its
 *    contributors may be used to endorse or promote products derived
 *    from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
 * A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
 * OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
 * LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
 * DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
 * THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
 * OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 *****************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/pci.h>
#include <linux/acpi.h>

#include "fw/acpi.h"

#include "iwl-trans.h"
#include "iwl-drv.h"
#include "internal.h"

#define IWL_PCI_DEVICE(dev, subdev, cfg) \
	.vendor = PCI_VENDOR_ID_INTEL,  .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = (subdev), \
	.driver_data = (kernel_ulong_t)&(cfg)

/* Hardware specific file defines the PCI IDs table for that hardware module */
static const struct pci_device_id iwl_hw_card_ids[] = {
#if IS_ENABLED(CONFIG_IWLDVM)
	{IWL_PCI_DEVICE(0x4232, 0x1201, iwl5100_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1301, iwl5100_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1204, iwl5100_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1304, iwl5100_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1205, iwl5100_bgn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1305, iwl5100_bgn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1206, iwl5100_abg_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1306, iwl5100_abg_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1221, iwl5100_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1321, iwl5100_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1224, iwl5100_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1324, iwl5100_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1225, iwl5100_bgn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1325, iwl5100_bgn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1226, iwl5100_abg_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4232, 0x1326, iwl5100_abg_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1211, iwl5100_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1311, iwl5100_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1214, iwl5100_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1314, iwl5100_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1215, iwl5100_bgn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1315, iwl5100_bgn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1216, iwl5100_abg_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4237, 0x1316, iwl5100_abg_cfg)}, /* Half Mini Card */

/* 5300 Series WiFi */
	{IWL_PCI_DEVICE(0x4235, 0x1021, iwl5300_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1121, iwl5300_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1024, iwl5300_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1124, iwl5300_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1001, iwl5300_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1101, iwl5300_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1004, iwl5300_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4235, 0x1104, iwl5300_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4236, 0x1011, iwl5300_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4236, 0x1111, iwl5300_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x4236, 0x1014, iwl5300_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x4236, 0x1114, iwl5300_agn_cfg)}, /* Half Mini Card */

/* 5350 Series WiFi/WiMax */
	{IWL_PCI_DEVICE(0x423A, 0x1001, iwl5350_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423A, 0x1021, iwl5350_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423B, 0x1011, iwl5350_agn_cfg)}, /* Mini Card */

/* 5150 Series Wifi/WiMax */
	{IWL_PCI_DEVICE(0x423C, 0x1201, iwl5150_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1301, iwl5150_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1206, iwl5150_abg_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1306, iwl5150_abg_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1221, iwl5150_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1321, iwl5150_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x423C, 0x1326, iwl5150_abg_cfg)}, /* Half Mini Card */

	{IWL_PCI_DEVICE(0x423D, 0x1211, iwl5150_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423D, 0x1311, iwl5150_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x423D, 0x1216, iwl5150_abg_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423D, 0x1316, iwl5150_abg_cfg)}, /* Half Mini Card */

/* 6x00 Series */
	{IWL_PCI_DEVICE(0x422B, 0x1101, iwl6000_3agn_cfg)},
	{IWL_PCI_DEVICE(0x422B, 0x1108, iwl6000_3agn_cfg)},
	{IWL_PCI_DEVICE(0x422B, 0x1121, iwl6000_3agn_cfg)},
	{IWL_PCI_DEVICE(0x422B, 0x1128, iwl6000_3agn_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1301, iwl6000i_2agn_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1306, iwl6000i_2abg_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1307, iwl6000i_2bg_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1321, iwl6000i_2agn_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1326, iwl6000i_2abg_cfg)},
	{IWL_PCI_DEVICE(0x4238, 0x1111, iwl6000_3agn_cfg)},
	{IWL_PCI_DEVICE(0x4238, 0x1118, iwl6000_3agn_cfg)},
	{IWL_PCI_DEVICE(0x4239, 0x1311, iwl6000i_2agn_cfg)},
	{IWL_PCI_DEVICE(0x4239, 0x1316, iwl6000i_2abg_cfg)},

/* 6x05 Series */
	{IWL_PCI_DEVICE(0x0082, 0x1301, iwl6005_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1306, iwl6005_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1307, iwl6005_2bg_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1308, iwl6005_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1321, iwl6005_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1326, iwl6005_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1328, iwl6005_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0x1311, iwl6005_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0x1318, iwl6005_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0x1316, iwl6005_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0xC020, iwl6005_2agn_sff_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0xC220, iwl6005_2agn_sff_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0xC228, iwl6005_2agn_sff_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x4820, iwl6005_2agn_d_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1304, iwl6005_2agn_mow1_cfg)},/* low 5GHz active */
	{IWL_PCI_DEVICE(0x0082, 0x1305, iwl6005_2agn_mow2_cfg)},/* high 5GHz active */

/* 6x30 Series */
	{IWL_PCI_DEVICE(0x008A, 0x5305, iwl1030_bgn_cfg)},
	{IWL_PCI_DEVICE(0x008A, 0x5307, iwl1030_bg_cfg)},
	{IWL_PCI_DEVICE(0x008A, 0x5325, iwl1030_bgn_cfg)},
	{IWL_PCI_DEVICE(0x008A, 0x5327, iwl1030_bg_cfg)},
	{IWL_PCI_DEVICE(0x008B, 0x5315, iwl1030_bgn_cfg)},
	{IWL_PCI_DEVICE(0x008B, 0x5317, iwl1030_bg_cfg)},
	{IWL_PCI_DEVICE(0x0090, 0x5211, iwl6030_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0090, 0x5215, iwl6030_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0090, 0x5216, iwl6030_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5201, iwl6030_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5205, iwl6030_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5206, iwl6030_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5207, iwl6030_2bg_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5221, iwl6030_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5225, iwl6030_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0091, 0x5226, iwl6030_2abg_cfg)},

/* 6x50 WiFi/WiMax Series */
	{IWL_PCI_DEVICE(0x0087, 0x1301, iwl6050_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0087, 0x1306, iwl6050_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0087, 0x1321, iwl6050_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0087, 0x1326, iwl6050_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0089, 0x1311, iwl6050_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0089, 0x1316, iwl6050_2abg_cfg)},

/* 6150 WiFi/WiMax Series */
	{IWL_PCI_DEVICE(0x0885, 0x1305, iwl6150_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0885, 0x1307, iwl6150_bg_cfg)},
	{IWL_PCI_DEVICE(0x0885, 0x1325, iwl6150_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0885, 0x1327, iwl6150_bg_cfg)},
	{IWL_PCI_DEVICE(0x0886, 0x1315, iwl6150_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0886, 0x1317, iwl6150_bg_cfg)},

/* 1000 Series WiFi */
	{IWL_PCI_DEVICE(0x0083, 0x1205, iwl1000_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1305, iwl1000_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1225, iwl1000_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1325, iwl1000_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0084, 0x1215, iwl1000_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0084, 0x1315, iwl1000_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1206, iwl1000_bg_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1306, iwl1000_bg_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1226, iwl1000_bg_cfg)},
	{IWL_PCI_DEVICE(0x0083, 0x1326, iwl1000_bg_cfg)},
	{IWL_PCI_DEVICE(0x0084, 0x1216, iwl1000_bg_cfg)},
	{IWL_PCI_DEVICE(0x0084, 0x1316, iwl1000_bg_cfg)},

/* 100 Series WiFi */
	{IWL_PCI_DEVICE(0x08AE, 0x1005, iwl100_bgn_cfg)},
	{IWL_PCI_DEVICE(0x08AE, 0x1007, iwl100_bg_cfg)},
	{IWL_PCI_DEVICE(0x08AF, 0x1015, iwl100_bgn_cfg)},
	{IWL_PCI_DEVICE(0x08AF, 0x1017, iwl100_bg_cfg)},
	{IWL_PCI_DEVICE(0x08AE, 0x1025, iwl100_bgn_cfg)},
	{IWL_PCI_DEVICE(0x08AE, 0x1027, iwl100_bg_cfg)},

/* 130 Series WiFi */
	{IWL_PCI_DEVICE(0x0896, 0x5005, iwl130_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0896, 0x5007, iwl130_bg_cfg)},
	{IWL_PCI_DEVICE(0x0897, 0x5015, iwl130_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0897, 0x5017, iwl130_bg_cfg)},
	{IWL_PCI_DEVICE(0x0896, 0x5025, iwl130_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0896, 0x5027, iwl130_bg_cfg)},

/* 2x00 Series */
	{IWL_PCI_DEVICE(0x0890, 0x4022, iwl2000_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0891, 0x4222, iwl2000_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0890, 0x4422, iwl2000_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0890, 0x4822, iwl2000_2bgn_d_cfg)},

/* 2x30 Series */
	{IWL_PCI_DEVICE(0x0887, 0x4062, iwl2030_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0888, 0x4262, iwl2030_2bgn_cfg)},
	{IWL_PCI_DEVICE(0x0887, 0x4462, iwl2030_2bgn_cfg)},

/* 6x35 Series */
	{IWL_PCI_DEVICE(0x088E, 0x4060, iwl6035_2agn_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x406A, iwl6035_2agn_sff_cfg)},
	{IWL_PCI_DEVICE(0x088F, 0x4260, iwl6035_2agn_cfg)},
	{IWL_PCI_DEVICE(0x088F, 0x426A, iwl6035_2agn_sff_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x4460, iwl6035_2agn_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x446A, iwl6035_2agn_sff_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x4860, iwl6035_2agn_cfg)},
	{IWL_PCI_DEVICE(0x088F, 0x5260, iwl6035_2agn_cfg)},

/* 105 Series */
	{IWL_PCI_DEVICE(0x0894, 0x0022, iwl105_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0895, 0x0222, iwl105_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0894, 0x0422, iwl105_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0894, 0x0822, iwl105_bgn_d_cfg)},

/* 135 Series */
	{IWL_PCI_DEVICE(0x0892, 0x0062, iwl135_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0893, 0x0262, iwl135_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0892, 0x0462, iwl135_bgn_cfg)},
#endif /* CONFIG_IWLDVM */

#if IS_ENABLED(CONFIG_IWLMVM)
/* 7260 Series */
	{IWL_PCI_DEVICE(0x08B1, 0x4070, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4072, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4170, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4C60, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4C70, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4060, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x406A, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4160, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4062, iwl7260_n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4162, iwl7260_n_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4270, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4272, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4260, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x426A, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4262, iwl7260_n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4470, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4472, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4460, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x446A, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4462, iwl7260_n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4870, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x486E, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4A70, iwl7260_2ac_cfg_high_temp)},
	{IWL_PCI_DEVICE(0x08B1, 0x4A6E, iwl7260_2ac_cfg_high_temp)},
	{IWL_PCI_DEVICE(0x08B1, 0x4A6C, iwl7260_2ac_cfg_high_temp)},
	{IWL_PCI_DEVICE(0x08B1, 0x4570, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4560, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4370, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4360, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x5070, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x5072, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x5170, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x5770, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4020, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x402A, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0x4220, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0x4420, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC070, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC072, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC170, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC060, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC06A, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC160, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC062, iwl7260_n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC162, iwl7260_n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC770, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC760, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC270, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xCC70, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xCC60, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC272, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC260, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC26A, iwl7260_n_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC262, iwl7260_n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC470, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC472, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC460, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC462, iwl7260_n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC570, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC560, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC370, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC360, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC020, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC02A, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B2, 0xC220, iwl7260_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC420, iwl7260_2n_cfg)},

/* 3160 Series */
	{IWL_PCI_DEVICE(0x08B3, 0x0070, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0072, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0170, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0172, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0060, iwl3160_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0062, iwl3160_n_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x0270, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x0272, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0470, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0472, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x0370, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8070, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8072, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8170, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8172, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8060, iwl3160_2n_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8062, iwl3160_n_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x8270, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x8370, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B4, 0x8272, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8470, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8570, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x1070, iwl3160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x1170, iwl3160_2ac_cfg)},

/* 3165 Series */
	{IWL_PCI_DEVICE(0x3165, 0x4010, iwl3165_2ac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x4012, iwl3165_2ac_cfg)},
	{IWL_PCI_DEVICE(0x3166, 0x4212, iwl3165_2ac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x4410, iwl3165_2ac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x4510, iwl3165_2ac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x4110, iwl3165_2ac_cfg)},
	{IWL_PCI_DEVICE(0x3166, 0x4310, iwl3165_2ac_cfg)},
	{IWL_PCI_DEVICE(0x3166, 0x4210, iwl3165_2ac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x8010, iwl3165_2ac_cfg)},
	{IWL_PCI_DEVICE(0x3165, 0x8110, iwl3165_2ac_cfg)},

/* 3168 Series */
	{IWL_PCI_DEVICE(0x24FB, 0x2010, iwl3168_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FB, 0x2110, iwl3168_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FB, 0x2050, iwl3168_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FB, 0x2150, iwl3168_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FB, 0x0000, iwl3168_2ac_cfg)},

/* 7265 Series */
	{IWL_PCI_DEVICE(0x095A, 0x5010, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5110, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5100, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5310, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5302, iwl7265_n_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5210, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5C10, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5012, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5412, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5410, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5510, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5400, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x1010, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5000, iwl7265_2n_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x500A, iwl7265_2n_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5200, iwl7265_2n_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5002, iwl7265_n_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5102, iwl7265_n_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5202, iwl7265_n_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9010, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9012, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x900A, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9110, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9112, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x9210, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x9200, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9510, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x9310, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9410, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5020, iwl7265_2n_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x502A, iwl7265_2n_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5420, iwl7265_2n_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5090, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5190, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5590, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5290, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5490, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x5F10, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x5212, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095B, 0x520A, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9000, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9400, iwl7265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x095A, 0x9E10, iwl7265_2ac_cfg)},

/* 8000 Series */
	{IWL_PCI_DEVICE(0x24F3, 0x0010, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1010, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x10B0, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0130, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1130, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0132, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1132, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0110, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x01F0, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0012, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1012, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1110, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0050, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0250, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1050, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0150, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x1150, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0x0030, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0x1030, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xC010, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xC110, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xD010, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xC050, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xD050, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xD0B0, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0xB0B0, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8010, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8110, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9010, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9110, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0x8030, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0x9030, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0xC030, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F4, 0xD030, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8130, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9130, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8132, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9132, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8050, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x8150, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9050, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x9150, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0004, iwl8260_2n_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0044, iwl8260_2n_cfg)},
	{IWL_PCI_DEVICE(0x24F5, 0x0010, iwl4165_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F6, 0x0030, iwl4165_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0810, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0910, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0850, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0950, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0930, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x0000, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24F3, 0x4010, iwl8260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0010, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0110, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1110, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1130, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0130, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1010, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x10D0, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0050, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0150, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x9010, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x8110, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x8050, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x8010, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0810, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x9110, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x8130, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0910, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0930, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0950, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0850, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1014, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x3E02, iwl8275_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x3E01, iwl8275_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x1012, iwl8275_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0012, iwl8275_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x0014, iwl8265_2ac_cfg)},
	{IWL_PCI_DEVICE(0x24FD, 0x9074, iwl8265_2ac_cfg)},

/* 9000 Series */
	{IWL_PCI_DEVICE(0x02F0, 0x0030, iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x0034, iwl9560_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x0038, iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x003C, iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x0060, iwl9461_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x0064, iwl9461_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x00A0, iwl9462_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x00A4, iwl9462_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x0230, iwl9560_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x0234, iwl9560_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x0238, iwl9560_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x023C, iwl9560_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x0260, iwl9461_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x0264, iwl9461_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x02A0, iwl9462_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x02A4, iwl9462_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x1551, iwl9560_killer_s_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x1552, iwl9560_killer_i_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x2030, iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x2034, iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x4030, iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x4034, iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x40A4, iwl9462_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x4234, iwl9560_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x02F0, 0x42A4, iwl9462_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x0030, iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x0034, iwl9560_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x0038, iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x003C, iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x0060, iwl9461_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x0064, iwl9461_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x00A0, iwl9462_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x00A4, iwl9462_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x0230, iwl9560_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x0234, iwl9560_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x0238, iwl9560_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x023C, iwl9560_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x0260, iwl9461_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x0264, iwl9461_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x02A0, iwl9462_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x02A4, iwl9462_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x1551, iwl9560_killer_s_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x1552, iwl9560_killer_i_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x2030, iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x2034, iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x4030, iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x4034, iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x40A4, iwl9462_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x4234, iwl9560_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x06F0, 0x42A4, iwl9462_2ac_cfg_quz_a0_jf_b0_soc)},
	{IWL_PCI_DEVICE(0x2526, 0x0010, iwl9260_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x0014, iwl9260_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x0018, iwl9260_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x001C, iwl9260_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x0030, iwl9560_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x0034, iwl9560_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x0038, iwl9560_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x003C, iwl9560_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x0060, iwl9460_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x0064, iwl9460_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x00A0, iwl9460_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x00A4, iwl9460_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x0210, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x0214, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x0230, iwl9560_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x0234, iwl9560_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x0238, iwl9560_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x023C, iwl9560_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x0260, iwl9460_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x0264, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2526, 0x02A0, iwl9460_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x02A4, iwl9460_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x1010, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x1030, iwl9560_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x1210, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x1410, iwl9270_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x1420, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2526, 0x1550, iwl9260_killer_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x1551, iwl9560_killer_s_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2526, 0x1552, iwl9560_killer_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2526, 0x1610, iwl9270_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x2030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x2526, 0x2034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x2526, 0x4010, iwl9260_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x4018, iwl9260_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x401C, iwl9260_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x4030, iwl9560_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x4034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x2526, 0x40A4, iwl9460_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x4234, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2526, 0x42A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2526, 0x6010, iwl9260_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x6014, iwl9260_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x8014, iwl9260_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0x8010, iwl9260_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0xA014, iwl9260_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0xE010, iwl9260_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2526, 0xE014, iwl9260_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x271B, 0x0010, iwl9160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x271B, 0x0014, iwl9160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x271B, 0x0210, iwl9160_2ac_cfg)},
	{IWL_PCI_DEVICE(0x271B, 0x0214, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x271C, 0x0214, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2720, 0x0034, iwl9560_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2720, 0x0038, iwl9560_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2720, 0x003C, iwl9560_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2720, 0x0060, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x0064, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x00A0, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x00A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x0230, iwl9560_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2720, 0x0234, iwl9560_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2720, 0x0238, iwl9560_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2720, 0x023C, iwl9560_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2720, 0x0260, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x0264, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x02A0, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x02A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x1010, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2720, 0x1030, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x1210, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x2720, 0x1551, iwl9560_killer_s_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x1552, iwl9560_killer_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x2030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x2034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x4030, iwl9560_2ac_160_cfg)},
	{IWL_PCI_DEVICE(0x2720, 0x4034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x40A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x4234, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x42A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x0030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x0034, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x0038, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x003C, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x0060, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x0064, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x00A0, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x00A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x0230, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x0234, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x0238, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x023C, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x0260, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x0264, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x02A0, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x02A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x1010, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x30DC, 0x1030, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x1210, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x30DC, 0x1551, iwl9560_killer_s_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x1552, iwl9560_killer_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x2030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x2034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x4030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x4034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x40A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x4234, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x30DC, 0x42A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x31DC, 0x0030, iwl9560_2ac_160_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x0034, iwl9560_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x0038, iwl9560_2ac_160_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x003C, iwl9560_2ac_160_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x0060, iwl9460_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x0064, iwl9461_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x00A0, iwl9462_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x00A4, iwl9462_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x0230, iwl9560_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x0234, iwl9560_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x0238, iwl9560_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x023C, iwl9560_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x0260, iwl9461_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x0264, iwl9461_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x02A0, iwl9462_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x02A4, iwl9462_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x1010, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x31DC, 0x1030, iwl9560_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x1210, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x31DC, 0x1551, iwl9560_killer_s_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x1552, iwl9560_killer_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x2030, iwl9560_2ac_160_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x2034, iwl9560_2ac_160_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x4030, iwl9560_2ac_160_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x4034, iwl9560_2ac_160_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x40A4, iwl9462_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x4234, iwl9560_2ac_cfg_shared_clk)},
	{IWL_PCI_DEVICE(0x31DC, 0x42A4, iwl9462_2ac_cfg_shared_clk)},

	{IWL_PCI_DEVICE(0x34F0, 0x0030, iwl9560_2ac_160_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x0034, iwl9560_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x0038, iwl9560_2ac_160_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x003C, iwl9560_2ac_160_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x0060, iwl9461_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x0064, iwl9461_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x00A0, iwl9462_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x00A4, iwl9462_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x0230, iwl9560_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x0234, iwl9560_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x0238, iwl9560_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x023C, iwl9560_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x0260, iwl9461_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x0264, iwl9461_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x02A0, iwl9462_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x02A4, iwl9462_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x1551, killer1550s_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x1552, killer1550i_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x2030, iwl9560_2ac_160_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x2034, iwl9560_2ac_160_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x4030, iwl9560_2ac_160_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x4034, iwl9560_2ac_160_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x40A4, iwl9462_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x4234, iwl9560_2ac_cfg_qu_b0_jf_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x42A4, iwl9462_2ac_cfg_qu_b0_jf_b0)},

	{IWL_PCI_DEVICE(0x3DF0, 0x0030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x0034, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x0038, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x003C, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x0060, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x0064, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x00A0, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x00A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x0230, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x0234, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x0238, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x023C, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x0260, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x0264, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x02A0, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x02A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x1010, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x3DF0, 0x1030, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x1210, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x3DF0, 0x1551, iwl9560_killer_s_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x1552, iwl9560_killer_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x2030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x2034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x4030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x4034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x40A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x4234, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x3DF0, 0x42A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x0030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x0034, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x0038, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x003C, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x0060, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x0064, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x00A0, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x00A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x0230, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x0234, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x0238, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x023C, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x0260, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x0264, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x02A0, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x02A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x1010, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x43F0, 0x1030, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x1210, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x43F0, 0x1551, iwl9560_killer_s_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x1552, iwl9560_killer_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x2030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x2034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x4030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x4034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x40A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x4234, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x43F0, 0x42A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0000, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0010, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0034, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0038, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x003C, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0060, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0064, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x00A0, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x00A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0210, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0230, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0234, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0238, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x023C, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0260, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0264, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x02A0, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x02A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0310, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0410, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0510, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0610, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0710, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x0A10, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x1010, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x9DF0, 0x1030, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x1210, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x9DF0, 0x1551, iwl9560_killer_s_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x1552, iwl9560_killer_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x2010, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x2030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x2034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x2A10, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x4030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x4034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x40A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x4234, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x9DF0, 0x42A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0034, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0038, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x003C, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0060, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0064, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x00A0, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x00A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0230, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0234, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0238, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x023C, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0260, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0264, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x02A0, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x02A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x1010, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0xA0F0, 0x1030, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x1210, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0xA0F0, 0x1551, iwl9560_killer_s_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x1552, iwl9560_killer_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x2030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x2034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x4030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x4034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x40A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x4234, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA0F0, 0x42A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x0030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x0034, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x0038, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x003C, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x0060, iwl9460_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x0064, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x00A0, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x00A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x0230, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x0234, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x0238, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x023C, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x0260, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x0264, iwl9461_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x02A0, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x02A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x1010, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0xA370, 0x1030, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x1210, iwl9260_2ac_cfg)},
	{IWL_PCI_DEVICE(0xA370, 0x1551, iwl9560_killer_s_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x1552, iwl9560_killer_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x2030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x2034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x4030, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x4034, iwl9560_2ac_160_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x40A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x4234, iwl9560_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0xA370, 0x42A4, iwl9462_2ac_cfg_soc)},
	{IWL_PCI_DEVICE(0x2720, 0x0030, iwl9560_2ac_cfg_qnj_jf_b0)},

/* 22000 Series */
	{IWL_PCI_DEVICE(0x02F0, 0x0070, iwl_ax201_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x02F0, 0x0074, iwl_ax201_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x02F0, 0x0078, iwl_ax201_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x02F0, 0x007C, iwl_ax201_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x02F0, 0x0244, iwl_ax101_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x02F0, 0x0310, iwl_ax201_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x02F0, 0x1651, iwl_ax1650s_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x02F0, 0x1652, iwl_ax1650i_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x02F0, 0x2074, iwl_ax201_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x02F0, 0x4070, iwl_ax201_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x02F0, 0x4244, iwl_ax101_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x06F0, 0x0070, iwl_ax201_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x06F0, 0x0074, iwl_ax201_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x06F0, 0x0078, iwl_ax201_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x06F0, 0x007C, iwl_ax201_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x06F0, 0x0244, iwl_ax101_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x06F0, 0x0310, iwl_ax201_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x06F0, 0x1651, iwl_ax1650s_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x06F0, 0x1652, iwl_ax1650i_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x06F0, 0x2074, iwl_ax201_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x06F0, 0x4070, iwl_ax201_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x06F0, 0x4244, iwl_ax101_cfg_quz_hr)},
	{IWL_PCI_DEVICE(0x2720, 0x0000, iwl_ax101_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x2720, 0x0040, iwl_ax101_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x2720, 0x0044, iwl_ax101_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x2720, 0x0070, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x2720, 0x0074, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x2720, 0x0078, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x2720, 0x007C, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x2720, 0x0090, iwl22000_2ac_cfg_hr_cdb)},
	{IWL_PCI_DEVICE(0x2720, 0x0244, iwl_ax101_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x2720, 0x0310, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x2720, 0x0A10, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x2720, 0x1080, iwl_ax101_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x2720, 0x1651, killer1650s_2ax_cfg_qu_b0_hr_b0)},
	{IWL_PCI_DEVICE(0x2720, 0x1652, killer1650i_2ax_cfg_qu_b0_hr_b0)},
	{IWL_PCI_DEVICE(0x2720, 0x2074, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x2720, 0x4070, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x2720, 0x4244, iwl_ax101_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x34F0, 0x0044, iwl_ax101_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x34F0, 0x0070, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x34F0, 0x0074, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x34F0, 0x0078, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x34F0, 0x007C, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x34F0, 0x0244, iwl_ax101_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x34F0, 0x0310, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x34F0, 0x1651, killer1650s_2ax_cfg_qu_b0_hr_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x1652, killer1650i_2ax_cfg_qu_b0_hr_b0)},
	{IWL_PCI_DEVICE(0x34F0, 0x2074, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x34F0, 0x4070, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x34F0, 0x4244, iwl_ax101_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x43F0, 0x0044, iwl_ax101_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x43F0, 0x0070, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x43F0, 0x0074, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x43F0, 0x0078, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x43F0, 0x007C, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x43F0, 0x0244, iwl_ax101_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x43F0, 0x1651, killer1650s_2ax_cfg_qu_b0_hr_b0)},
	{IWL_PCI_DEVICE(0x43F0, 0x1652, killer1650i_2ax_cfg_qu_b0_hr_b0)},
	{IWL_PCI_DEVICE(0x43F0, 0x2074, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x43F0, 0x4070, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0x43F0, 0x4244, iwl_ax101_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0044, iwl_ax101_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0070, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0074, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0078, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0xA0F0, 0x007C, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0244, iwl_ax101_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0xA0F0, 0x0A10, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0xA0F0, 0x1651, killer1650s_2ax_cfg_qu_b0_hr_b0)},
	{IWL_PCI_DEVICE(0xA0F0, 0x1652, killer1650i_2ax_cfg_qu_b0_hr_b0)},
	{IWL_PCI_DEVICE(0xA0F0, 0x2074, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0xA0F0, 0x4070, iwl_ax201_cfg_qu_hr)},
	{IWL_PCI_DEVICE(0xA0F0, 0x4244, iwl_ax101_cfg_qu_hr)},

	{IWL_PCI_DEVICE(0x2723, 0x0080, iwl_ax200_cfg_cc)},
	{IWL_PCI_DEVICE(0x2723, 0x0084, iwl_ax200_cfg_cc)},
	{IWL_PCI_DEVICE(0x2723, 0x0088, iwl_ax200_cfg_cc)},
	{IWL_PCI_DEVICE(0x2723, 0x008C, iwl_ax200_cfg_cc)},
	{IWL_PCI_DEVICE(0x2723, 0x1653, killer1650w_2ax_cfg)},
	{IWL_PCI_DEVICE(0x2723, 0x1654, killer1650x_2ax_cfg)},
	{IWL_PCI_DEVICE(0x2723, 0x2080, iwl_ax200_cfg_cc)},
	{IWL_PCI_DEVICE(0x2723, 0x4080, iwl_ax200_cfg_cc)},
	{IWL_PCI_DEVICE(0x2723, 0x4088, iwl_ax200_cfg_cc)},

	{IWL_PCI_DEVICE(0x2725, 0x0090, iwlax211_2ax_cfg_so_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0x0020, iwlax210_2ax_cfg_ty_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0x0310, iwlax210_2ax_cfg_ty_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0x0510, iwlax210_2ax_cfg_ty_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0x0A10, iwlax210_2ax_cfg_ty_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0x00B0, iwlax411_2ax_cfg_so_gf4_a0)},
	{IWL_PCI_DEVICE(0x7A70, 0x0090, iwlax211_2ax_cfg_so_gf_a0)},
	{IWL_PCI_DEVICE(0x7A70, 0x0310, iwlax211_2ax_cfg_so_gf_a0)},
	{IWL_PCI_DEVICE(0x7A70, 0x0510, iwlax211_2ax_cfg_so_gf_a0)},
	{IWL_PCI_DEVICE(0x7A70, 0x0A10, iwlax211_2ax_cfg_so_gf_a0)},
	{IWL_PCI_DEVICE(0x7AF0, 0x0090, iwlax211_2ax_cfg_so_gf_a0)},
	{IWL_PCI_DEVICE(0x7AF0, 0x0310, iwlax211_2ax_cfg_so_gf_a0)},
	{IWL_PCI_DEVICE(0x7AF0, 0x0510, iwlax211_2ax_cfg_so_gf_a0)},
	{IWL_PCI_DEVICE(0x7AF0, 0x0A10, iwlax211_2ax_cfg_so_gf_a0)},

#endif /* CONFIG_IWLMVM */

	{0}
};
MODULE_DEVICE_TABLE(pci, iwl_hw_card_ids);

/* PCI registers */
#define PCI_CFG_RETRY_TIMEOUT	0x041

static int iwl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct iwl_cfg *cfg = (struct iwl_cfg *)(ent->driver_data);
	const struct iwl_cfg *cfg_7265d __maybe_unused = NULL;
	struct iwl_trans *iwl_trans;
	int ret;

	if (WARN_ONCE(!cfg->csr, "CSR addresses aren't configured\n"))
		return -EINVAL;

	iwl_trans = iwl_trans_pcie_alloc(pdev, ent, cfg);
	if (IS_ERR(iwl_trans))
		return PTR_ERR(iwl_trans);

#if IS_ENABLED(CONFIG_IWLMVM)
	/*
	 * special-case 7265D, it has the same PCI IDs.
	 *
	 * Note that because we already pass the cfg to the transport above,
	 * all the parameters that the transport uses must, until that is
	 * changed, be identical to the ones in the 7265D configuration.
	 */
	if (cfg == &iwl7265_2ac_cfg)
		cfg_7265d = &iwl7265d_2ac_cfg;
	else if (cfg == &iwl7265_2n_cfg)
		cfg_7265d = &iwl7265d_2n_cfg;
	else if (cfg == &iwl7265_n_cfg)
		cfg_7265d = &iwl7265d_n_cfg;
	if (cfg_7265d &&
	    (iwl_trans->hw_rev & CSR_HW_REV_TYPE_MSK) == CSR_HW_REV_TYPE_7265D) {
		cfg = cfg_7265d;
		iwl_trans->cfg = cfg_7265d;
	}

	if (iwl_trans->cfg->rf_id && cfg == &iwl22000_2ac_cfg_hr_cdb &&
	    iwl_trans->hw_rev != CSR_HW_REV_TYPE_HR_CDB) {
		u32 rf_id_chp = CSR_HW_RF_ID_TYPE_CHIP_ID(iwl_trans->hw_rf_id);
		u32 jf_chp_id = CSR_HW_RF_ID_TYPE_CHIP_ID(CSR_HW_RF_ID_TYPE_JF);
		u32 hr_chp_id = CSR_HW_RF_ID_TYPE_CHIP_ID(CSR_HW_RF_ID_TYPE_HR);

		if (rf_id_chp == jf_chp_id) {
			if (iwl_trans->hw_rev == CSR_HW_REV_TYPE_QNJ)
				cfg = &iwl9560_2ac_cfg_qnj_jf_b0;
			else
				cfg = &iwl22000_2ac_cfg_jf;
		} else if (rf_id_chp == hr_chp_id) {
			if (iwl_trans->hw_rev == CSR_HW_REV_TYPE_QNJ)
				cfg = &iwl22000_2ax_cfg_qnj_hr_a0;
			else
				cfg = &iwl22000_2ac_cfg_hr;
		}
		iwl_trans->cfg = cfg;
	}

	/*
	 * This is a hack to switch from Qu B0 to Qu C0.  We need to
	 * do this for all cfgs that use Qu B0.  All this code is in
	 * urgent need for a refactor, but for now this is the easiest
	 * thing to do to support Qu C-step.
	 */
	if (iwl_trans->hw_rev == CSR_HW_REV_TYPE_QU_C0) {
		if (iwl_trans->cfg == &iwl_ax101_cfg_qu_hr)
			iwl_trans->cfg = &iwl_ax101_cfg_qu_c0_hr_b0;
		else if (iwl_trans->cfg == &iwl_ax201_cfg_qu_hr)
			iwl_trans->cfg = &iwl_ax201_cfg_qu_c0_hr_b0;
		else if (iwl_trans->cfg == &iwl9461_2ac_cfg_qu_b0_jf_b0)
			iwl_trans->cfg = &iwl9461_2ac_cfg_qu_c0_jf_b0;
		else if (iwl_trans->cfg == &iwl9462_2ac_cfg_qu_b0_jf_b0)
			iwl_trans->cfg = &iwl9462_2ac_cfg_qu_c0_jf_b0;
		else if (iwl_trans->cfg == &iwl9560_2ac_cfg_qu_b0_jf_b0)
			iwl_trans->cfg = &iwl9560_2ac_cfg_qu_c0_jf_b0;
		else if (iwl_trans->cfg == &iwl9560_2ac_160_cfg_qu_b0_jf_b0)
			iwl_trans->cfg = &iwl9560_2ac_160_cfg_qu_c0_jf_b0;
		else if (iwl_trans->cfg == &killer1650s_2ax_cfg_qu_b0_hr_b0)
			iwl_trans->cfg = &killer1650s_2ax_cfg_qu_c0_hr_b0;
		else if (iwl_trans->cfg == &killer1650i_2ax_cfg_qu_b0_hr_b0)
			iwl_trans->cfg = &killer1650i_2ax_cfg_qu_c0_hr_b0;
	}

	/* same thing for QuZ... */
	if (iwl_trans->hw_rev == CSR_HW_REV_TYPE_QUZ) {
		if (cfg == &iwl_ax101_cfg_qu_hr)
			cfg = &iwl_ax101_cfg_quz_hr;
		else if (cfg == &iwl_ax201_cfg_qu_hr)
			cfg = &iwl_ax201_cfg_quz_hr;
		else if (cfg == &iwl9461_2ac_cfg_qu_b0_jf_b0)
			cfg = &iwl9461_2ac_cfg_quz_a0_jf_b0_soc;
		else if (cfg == &iwl9462_2ac_cfg_qu_b0_jf_b0)
			cfg = &iwl9462_2ac_cfg_quz_a0_jf_b0_soc;
		else if (cfg == &iwl9560_2ac_cfg_qu_b0_jf_b0)
			cfg = &iwl9560_2ac_cfg_quz_a0_jf_b0_soc;
		else if (cfg == &iwl9560_2ac_160_cfg_qu_b0_jf_b0)
			cfg = &iwl9560_2ac_160_cfg_quz_a0_jf_b0_soc;
	}

#endif

	pci_set_drvdata(pdev, iwl_trans);
	iwl_trans->drv = iwl_drv_start(iwl_trans);

	if (IS_ERR(iwl_trans->drv)) {
		ret = PTR_ERR(iwl_trans->drv);
		goto out_free_trans;
	}

	/* register transport layer debugfs here */
	iwl_trans_pcie_dbgfs_register(iwl_trans);

	/* if RTPM is in use, enable it in our device */
	if (iwl_trans->runtime_pm_mode != IWL_PLAT_PM_MODE_DISABLED) {
		/* We explicitly set the device to active here to
		 * clear contingent errors.
		 */
		pm_runtime_set_active(&pdev->dev);

		pm_runtime_set_autosuspend_delay(&pdev->dev,
					 iwlwifi_mod_params.d0i3_timeout);
		pm_runtime_use_autosuspend(&pdev->dev);

		/* We are not supposed to call pm_runtime_allow() by
		 * ourselves, but let userspace enable runtime PM via
		 * sysfs.  However, since we don't enable this from
		 * userspace yet, we need to allow/forbid() ourselves.
		*/
		pm_runtime_allow(&pdev->dev);
	}

	/* The PCI device starts with a reference taken and we are
	 * supposed to release it here.  But to simplify the
	 * interaction with the opmode, we don't do it now, but let
	 * the opmode release it when it's ready.
	 */

	return 0;

out_free_trans:
	iwl_trans_pcie_free(iwl_trans);
	return ret;
}

static void iwl_pci_remove(struct pci_dev *pdev)
{
	struct iwl_trans *trans = pci_get_drvdata(pdev);

	/* if RTPM was in use, restore it to the state before probe */
	if (trans->runtime_pm_mode != IWL_PLAT_PM_MODE_DISABLED) {
		/* We should not call forbid here, but we do for now.
		 * Check the comment to pm_runtime_allow() in
		 * iwl_pci_probe().
		 */
		pm_runtime_forbid(trans->dev);
	}

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

static int iwl_pci_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct iwl_trans *trans = pci_get_drvdata(pdev);
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

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

int iwl_pci_fw_enter_d0i3(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int ret;

	if (test_bit(STATUS_FW_ERROR, &trans->status))
		return 0;

	set_bit(STATUS_TRANS_GOING_IDLE, &trans->status);

	/* config the fw */
	ret = iwl_op_mode_enter_d0i3(trans->op_mode);
	if (ret == 1) {
		IWL_DEBUG_RPM(trans, "aborting d0i3 entrance\n");
		clear_bit(STATUS_TRANS_GOING_IDLE, &trans->status);
		return -EBUSY;
	}
	if (ret)
		goto err;

	ret = wait_event_timeout(trans_pcie->d0i3_waitq,
				 test_bit(STATUS_TRANS_IDLE, &trans->status),
				 msecs_to_jiffies(IWL_TRANS_IDLE_TIMEOUT));
	if (!ret) {
		IWL_ERR(trans, "Timeout entering D0i3\n");
		ret = -ETIMEDOUT;
		goto err;
	}

	clear_bit(STATUS_TRANS_GOING_IDLE, &trans->status);

	return 0;
err:
	clear_bit(STATUS_TRANS_GOING_IDLE, &trans->status);
	iwl_trans_fw_error(trans);
	return ret;
}

int iwl_pci_fw_exit_d0i3(struct iwl_trans *trans)
{
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);
	int ret;

	/* sometimes a D0i3 entry is not followed through */
	if (!test_bit(STATUS_TRANS_IDLE, &trans->status))
		return 0;

	/* config the fw */
	ret = iwl_op_mode_exit_d0i3(trans->op_mode);
	if (ret)
		goto err;

	/* we clear STATUS_TRANS_IDLE only when D0I3_END command is completed */

	ret = wait_event_timeout(trans_pcie->d0i3_waitq,
				 !test_bit(STATUS_TRANS_IDLE, &trans->status),
				 msecs_to_jiffies(IWL_TRANS_IDLE_TIMEOUT));
	if (!ret) {
		IWL_ERR(trans, "Timeout exiting D0i3\n");
		ret = -ETIMEDOUT;
		goto err;
	}

	return 0;
err:
	clear_bit(STATUS_TRANS_IDLE, &trans->status);
	iwl_trans_fw_error(trans);
	return ret;
}

#ifdef CONFIG_IWLWIFI_PCIE_RTPM
static int iwl_pci_runtime_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct iwl_trans *trans = pci_get_drvdata(pdev);
	int ret;

	IWL_DEBUG_RPM(trans, "entering runtime suspend\n");

	if (test_bit(STATUS_DEVICE_ENABLED, &trans->status)) {
		ret = iwl_pci_fw_enter_d0i3(trans);
		if (ret < 0)
			return ret;
	}

	trans->system_pm_mode = IWL_PLAT_PM_MODE_D0I3;

	iwl_trans_d3_suspend(trans, false, false);

	return 0;
}

static int iwl_pci_runtime_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct iwl_trans *trans = pci_get_drvdata(pdev);
	enum iwl_d3_status d3_status;

	IWL_DEBUG_RPM(trans, "exiting runtime suspend (resume)\n");

	iwl_trans_d3_resume(trans, &d3_status, false, false);

	if (test_bit(STATUS_DEVICE_ENABLED, &trans->status))
		return iwl_pci_fw_exit_d0i3(trans);

	return 0;
}

static int iwl_pci_system_prepare(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct iwl_trans *trans = pci_get_drvdata(pdev);

	IWL_DEBUG_RPM(trans, "preparing for system suspend\n");

	/* This is called before entering system suspend and before
	 * the runtime resume is called.  Set the suspending flag to
	 * prevent the wakelock from being taken.
	 */
	trans->suspending = true;

	/* Wake the device up from runtime suspend before going to
	 * platform suspend.  This is needed because we don't know
	 * whether wowlan any is set and, if it's not, mac80211 will
	 * disconnect (in which case, we can't be in D0i3).
	 */
	pm_runtime_resume(device);

	return 0;
}

static void iwl_pci_system_complete(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct iwl_trans *trans = pci_get_drvdata(pdev);

	IWL_DEBUG_RPM(trans, "completing system suspend\n");

	/* This is called as a counterpart to the prepare op.  It is
	 * called either when suspending fails or when suspend
	 * completed successfully.  Now there's no risk of grabbing
	 * the wakelock anymore, so we can release the suspending
	 * flag.
	 */
	trans->suspending = false;
}
#endif /* CONFIG_IWLWIFI_PCIE_RTPM */

static const struct dev_pm_ops iwl_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(iwl_pci_suspend,
				iwl_pci_resume)
#ifdef CONFIG_IWLWIFI_PCIE_RTPM
	SET_RUNTIME_PM_OPS(iwl_pci_runtime_suspend,
			   iwl_pci_runtime_resume,
			   NULL)
	.prepare = iwl_pci_system_prepare,
	.complete = iwl_pci_system_complete,
#endif /* CONFIG_IWLWIFI_PCIE_RTPM */
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
