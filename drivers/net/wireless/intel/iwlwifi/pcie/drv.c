/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * Copyright(c) 2016-2017 Intel Deutschland GmbH
 * Copyright(c) 2007 - 2014, 2018 - 2020 Intel Corporation
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
 * Copyright(c) 2013 - 2015 Intel Mobile Communications GmbH
 * All rights reserved.
 * Copyright(c) 2017 Intel Deutschland GmbH
 * Copyright(c) 2005 - 2014, 2018 - 2020 Intel Corporation
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
#include <linux/pci.h>
#include <linux/acpi.h>

#include "fw/acpi.h"

#include "iwl-trans.h"
#include "iwl-drv.h"
#include "iwl-prph.h"
#include "internal.h"

#define TRANS_CFG_MARKER BIT(0)
#define _IS_A(cfg, _struct) __builtin_types_compatible_p(typeof(cfg),	\
							 struct _struct)
extern int _invalid_type;
#define _TRANS_CFG_MARKER(cfg)						\
	(__builtin_choose_expr(_IS_A(cfg, iwl_cfg_trans_params),	\
			       TRANS_CFG_MARKER,			\
	 __builtin_choose_expr(_IS_A(cfg, iwl_cfg), 0, _invalid_type)))
#define _ASSIGN_CFG(cfg) (_TRANS_CFG_MARKER(cfg) + (kernel_ulong_t)&(cfg))

#define IWL_PCI_DEVICE(dev, subdev, cfg) \
	.vendor = PCI_VENDOR_ID_INTEL,  .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = (subdev), \
	.driver_data = _ASSIGN_CFG(cfg)

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
	{IWL_PCI_DEVICE(0x2526, PCI_ANY_ID, iwl9000_trans_cfg)},
	{IWL_PCI_DEVICE(0x271B, PCI_ANY_ID, iwl9000_trans_cfg)},
	{IWL_PCI_DEVICE(0x271C, PCI_ANY_ID, iwl9000_trans_cfg)},
	{IWL_PCI_DEVICE(0x30DC, PCI_ANY_ID, iwl9560_long_latency_trans_cfg)},
	{IWL_PCI_DEVICE(0x31DC, PCI_ANY_ID, iwl9560_shared_clk_trans_cfg)},
	{IWL_PCI_DEVICE(0x9DF0, PCI_ANY_ID, iwl9560_trans_cfg)},
	{IWL_PCI_DEVICE(0xA370, PCI_ANY_ID, iwl9560_trans_cfg)},

/* Qu devices */
	{IWL_PCI_DEVICE(0x02F0, PCI_ANY_ID, iwl_qu_trans_cfg)},
	{IWL_PCI_DEVICE(0x06F0, PCI_ANY_ID, iwl_qu_trans_cfg)},

	{IWL_PCI_DEVICE(0x34F0, PCI_ANY_ID, iwl_qu_medium_latency_trans_cfg)},
	{IWL_PCI_DEVICE(0x3DF0, PCI_ANY_ID, iwl_qu_medium_latency_trans_cfg)},
	{IWL_PCI_DEVICE(0x4DF0, PCI_ANY_ID, iwl_qu_medium_latency_trans_cfg)},

	{IWL_PCI_DEVICE(0x43F0, PCI_ANY_ID, iwl_qu_long_latency_trans_cfg)},
	{IWL_PCI_DEVICE(0xA0F0, PCI_ANY_ID, iwl_qu_long_latency_trans_cfg)},

	{IWL_PCI_DEVICE(0x2720, PCI_ANY_ID, iwl_qnj_trans_cfg)},

	{IWL_PCI_DEVICE(0x2723, PCI_ANY_ID, iwl_ax200_trans_cfg)},

	{IWL_PCI_DEVICE(0x2725, 0x0090, iwlax211_2ax_cfg_so_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0x0020, iwlax210_2ax_cfg_ty_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0x0024, iwlax210_2ax_cfg_ty_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0x0310, iwlax210_2ax_cfg_ty_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0x0510, iwlax210_2ax_cfg_ty_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0x0A10, iwlax210_2ax_cfg_ty_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0xE020, iwlax210_2ax_cfg_ty_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0xE024, iwlax210_2ax_cfg_ty_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0x4020, iwlax210_2ax_cfg_ty_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0x6020, iwlax210_2ax_cfg_ty_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0x6024, iwlax210_2ax_cfg_ty_gf_a0)},
	{IWL_PCI_DEVICE(0x2725, 0x00B0, iwlax411_2ax_cfg_sosnj_gf4_a0)},
	{IWL_PCI_DEVICE(0x2726, 0x0070, iwlax201_cfg_snj_hr_b0)},
	{IWL_PCI_DEVICE(0x2726, 0x0074, iwlax201_cfg_snj_hr_b0)},
	{IWL_PCI_DEVICE(0x2726, 0x0078, iwlax201_cfg_snj_hr_b0)},
	{IWL_PCI_DEVICE(0x2726, 0x007C, iwlax201_cfg_snj_hr_b0)},
	{IWL_PCI_DEVICE(0x2726, 0x0090, iwlax211_cfg_snj_gf_a0)},
	{IWL_PCI_DEVICE(0x2726, 0x0098, iwlax211_cfg_snj_gf_a0)},
	{IWL_PCI_DEVICE(0x2726, 0x00B0, iwlax411_2ax_cfg_sosnj_gf4_a0)},
	{IWL_PCI_DEVICE(0x2726, 0x0510, iwlax211_cfg_snj_gf_a0)},
	{IWL_PCI_DEVICE(0x2726, 0x2074, iwlax201_cfg_snj_hr_b0)},
	{IWL_PCI_DEVICE(0x2726, 0x4070, iwlax201_cfg_snj_hr_b0)},
	{IWL_PCI_DEVICE(0x7A70, 0x0090, iwlax211_2ax_cfg_so_gf_a0_long)},
	{IWL_PCI_DEVICE(0x7A70, 0x0098, iwlax211_2ax_cfg_so_gf_a0_long)},
	{IWL_PCI_DEVICE(0x7A70, 0x00B0, iwlax411_2ax_cfg_so_gf4_a0_long)},
	{IWL_PCI_DEVICE(0x7A70, 0x0310, iwlax211_2ax_cfg_so_gf_a0_long)},
	{IWL_PCI_DEVICE(0x7A70, 0x0510, iwlax211_2ax_cfg_so_gf_a0_long)},
	{IWL_PCI_DEVICE(0x7A70, 0x0A10, iwlax211_2ax_cfg_so_gf_a0_long)},
	{IWL_PCI_DEVICE(0x7AF0, 0x0090, iwlax211_2ax_cfg_so_gf_a0)},
	{IWL_PCI_DEVICE(0x7AF0, 0x0098, iwlax211_2ax_cfg_so_gf_a0)},
	{IWL_PCI_DEVICE(0x7AF0, 0x00B0, iwlax411_2ax_cfg_so_gf4_a0)},
	{IWL_PCI_DEVICE(0x7AF0, 0x0310, iwlax211_2ax_cfg_so_gf_a0)},
	{IWL_PCI_DEVICE(0x7AF0, 0x0510, iwlax211_2ax_cfg_so_gf_a0)},
	{IWL_PCI_DEVICE(0x7AF0, 0x0A10, iwlax211_2ax_cfg_so_gf_a0)},

/* Ma devices */
	{IWL_PCI_DEVICE(0x2729, PCI_ANY_ID, iwl_ma_trans_cfg)},
	{IWL_PCI_DEVICE(0x7E80, PCI_ANY_ID, iwl_ma_trans_cfg)},

#endif /* CONFIG_IWLMVM */

	{0}
};
MODULE_DEVICE_TABLE(pci, iwl_hw_card_ids);

#define _IWL_DEV_INFO(_device, _subdevice, _mac_type, _mac_step, _rf_type, \
		      _rf_id, _no_160, _cores, _cfg, _name)		   \
	{ .device = (_device), .subdevice = (_subdevice), .cfg = &(_cfg),  \
	  .name = _name, .mac_type = _mac_type, .rf_type = _rf_type,	   \
	  .no_160 = _no_160, .cores = _cores, .rf_id = _rf_id,		   \
	  .mac_step = _mac_step }

#define IWL_DEV_INFO(_device, _subdevice, _cfg, _name) \
	_IWL_DEV_INFO(_device, _subdevice, IWL_CFG_ANY, IWL_CFG_ANY,	   \
		      IWL_CFG_ANY, IWL_CFG_ANY, IWL_CFG_ANY, IWL_CFG_ANY,  \
		      _cfg, _name)

static const struct iwl_dev_info iwl_dev_info_table[] = {
#if IS_ENABLED(CONFIG_IWLMVM)
/* 9000 */
	IWL_DEV_INFO(0x2526, 0x1550, iwl9260_2ac_cfg, iwl9260_killer_1550_name),
	IWL_DEV_INFO(0x2526, 0x1551, iwl9560_2ac_cfg_soc, iwl9560_killer_1550s_name),
	IWL_DEV_INFO(0x2526, 0x1552, iwl9560_2ac_cfg_soc, iwl9560_killer_1550i_name),
	IWL_DEV_INFO(0x30DC, 0x1551, iwl9560_2ac_cfg_soc, iwl9560_killer_1550s_name),
	IWL_DEV_INFO(0x30DC, 0x1552, iwl9560_2ac_cfg_soc, iwl9560_killer_1550i_name),
	IWL_DEV_INFO(0x31DC, 0x1551, iwl9560_2ac_cfg_soc, iwl9560_killer_1550s_name),
	IWL_DEV_INFO(0x31DC, 0x1552, iwl9560_2ac_cfg_soc, iwl9560_killer_1550i_name),
	IWL_DEV_INFO(0xA370, 0x1551, iwl9560_2ac_cfg_soc, iwl9560_killer_1550s_name),
	IWL_DEV_INFO(0xA370, 0x1552, iwl9560_2ac_cfg_soc, iwl9560_killer_1550i_name),

	IWL_DEV_INFO(0x271C, 0x0214, iwl9260_2ac_cfg, iwl9260_1_name),

/* AX200 */
	IWL_DEV_INFO(0x2723, 0x1653, iwl_ax200_cfg_cc, iwl_ax200_killer_1650w_name),
	IWL_DEV_INFO(0x2723, 0x1654, iwl_ax200_cfg_cc, iwl_ax200_killer_1650x_name),
	IWL_DEV_INFO(0x2723, IWL_CFG_ANY, iwl_ax200_cfg_cc, iwl_ax200_name),

	/* QnJ with Hr */
	IWL_DEV_INFO(0x2720, IWL_CFG_ANY, iwl_qnj_b0_hr_b0_cfg, iwl_ax201_name),

	/* SnJ with HR*/
	IWL_DEV_INFO(0x2726, 0x0244, iwlax201_cfg_snj_hr_b0, iwl_ax101_name),
	IWL_DEV_INFO(0x2726, 0x1651, iwlax201_cfg_snj_hr_b0, iwl_ax201_killer_1650s_name),
	IWL_DEV_INFO(0x2726, 0x1652, iwlax201_cfg_snj_hr_b0, iwl_ax201_killer_1650i_name),
	IWL_DEV_INFO(0x2726, 0x4244, iwlax201_cfg_snj_hr_b0, iwl_ax101_name),

	/* Qu with Hr */
	IWL_DEV_INFO(0x43F0, 0x0070, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x43F0, 0x0074, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x43F0, 0x0078, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x43F0, 0x007C, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x43F0, 0x2074, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x43F0, 0x4070, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0xA0F0, 0x0070, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0xA0F0, 0x0074, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0xA0F0, 0x0078, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0xA0F0, 0x007C, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0xA0F0, 0x0A10, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0xA0F0, 0x1651, killer1650s_2ax_cfg_qu_b0_hr_b0, NULL),
	IWL_DEV_INFO(0xA0F0, 0x1652, killer1650i_2ax_cfg_qu_b0_hr_b0, NULL),
	IWL_DEV_INFO(0xA0F0, 0x2074, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0xA0F0, 0x4070, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0xA0F0, 0x6074, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x02F0, 0x0070, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x02F0, 0x0074, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x02F0, 0x6074, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x02F0, 0x0078, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x02F0, 0x007C, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x02F0, 0x0310, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x02F0, 0x1651, iwl_ax1650s_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x02F0, 0x1652, iwl_ax1650i_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x02F0, 0x2074, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x02F0, 0x4070, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x06F0, 0x0070, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x06F0, 0x0074, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x06F0, 0x0078, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x06F0, 0x007C, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x06F0, 0x0310, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x06F0, 0x1651, iwl_ax1650s_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x06F0, 0x1652, iwl_ax1650i_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x06F0, 0x2074, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x06F0, 0x4070, iwl_ax201_cfg_quz_hr, NULL),
	IWL_DEV_INFO(0x34F0, 0x0070, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x34F0, 0x0074, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x34F0, 0x0078, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x34F0, 0x007C, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x34F0, 0x0310, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x34F0, 0x1651, killer1650s_2ax_cfg_qu_b0_hr_b0, NULL),
	IWL_DEV_INFO(0x34F0, 0x1652, killer1650i_2ax_cfg_qu_b0_hr_b0, NULL),
	IWL_DEV_INFO(0x34F0, 0x2074, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x34F0, 0x4070, iwl_ax201_cfg_qu_hr, NULL),

	IWL_DEV_INFO(0x3DF0, 0x0070, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x3DF0, 0x0074, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x3DF0, 0x0078, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x3DF0, 0x007C, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x3DF0, 0x0310, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x3DF0, 0x1651, killer1650s_2ax_cfg_qu_b0_hr_b0, NULL),
	IWL_DEV_INFO(0x3DF0, 0x1652, killer1650i_2ax_cfg_qu_b0_hr_b0, NULL),
	IWL_DEV_INFO(0x3DF0, 0x2074, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x3DF0, 0x4070, iwl_ax201_cfg_qu_hr, NULL),

	IWL_DEV_INFO(0x4DF0, 0x0070, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x4DF0, 0x0074, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x4DF0, 0x0078, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x4DF0, 0x007C, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x4DF0, 0x0310, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x4DF0, 0x1651, killer1650s_2ax_cfg_qu_b0_hr_b0, NULL),
	IWL_DEV_INFO(0x4DF0, 0x1652, killer1650i_2ax_cfg_qu_b0_hr_b0, NULL),
	IWL_DEV_INFO(0x4DF0, 0x2074, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x4DF0, 0x4070, iwl_ax201_cfg_qu_hr, NULL),
	IWL_DEV_INFO(0x4DF0, 0x6074, iwl_ax201_cfg_qu_hr, NULL),

	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_PU, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_2ac_cfg_soc, iwl9461_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_PU, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_2ac_cfg_soc, iwl9461_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_PU, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1_DIV,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_2ac_cfg_soc, iwl9462_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_PU, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1_DIV,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_2ac_cfg_soc, iwl9462_name),

	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_PU, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_2ac_cfg_soc, iwl9560_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_PU, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_2ac_cfg_soc, iwl9560_name),

	_IWL_DEV_INFO(0x2526, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_PNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9260_2ac_cfg, iwl9461_160_name),
	_IWL_DEV_INFO(0x2526, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_PNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9260_2ac_cfg, iwl9461_name),
	_IWL_DEV_INFO(0x2526, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_PNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1_DIV,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9260_2ac_cfg, iwl9462_160_name),
	_IWL_DEV_INFO(0x2526, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_PNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1_DIV,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9260_2ac_cfg, iwl9462_name),

	_IWL_DEV_INFO(0x2526, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_PNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9260_2ac_cfg, iwl9560_160_name),
	_IWL_DEV_INFO(0x2526, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_PNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9260_2ac_cfg, iwl9560_name),

	_IWL_DEV_INFO(0x2526, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_TH, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_TH, IWL_CFG_ANY,
		      IWL_CFG_160, IWL_CFG_CORES_BT_GNSS,
		      iwl9260_2ac_cfg, iwl9270_160_name),
	_IWL_DEV_INFO(0x2526, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_TH, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_TH, IWL_CFG_ANY,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT_GNSS,
		      iwl9260_2ac_cfg, iwl9270_name),

	_IWL_DEV_INFO(0x271B, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_TH, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_TH1, IWL_CFG_ANY,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9260_2ac_cfg, iwl9162_160_name),
	_IWL_DEV_INFO(0x271B, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_TH, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_TH1, IWL_CFG_ANY,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9260_2ac_cfg, iwl9162_name),

	_IWL_DEV_INFO(0x2526, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_TH, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_TH, IWL_CFG_ANY,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9260_2ac_cfg, iwl9260_160_name),
	_IWL_DEV_INFO(0x2526, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_TH, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_TH, IWL_CFG_ANY,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9260_2ac_cfg, iwl9260_name),

/* Qu with Jf */
	/* Qu B step */
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_B_STEP,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_b0_jf_b0_cfg, iwl9461_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_B_STEP,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_b0_jf_b0_cfg, iwl9461_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_B_STEP,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1_DIV,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_b0_jf_b0_cfg, iwl9462_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_B_STEP,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1_DIV,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_b0_jf_b0_cfg, iwl9462_name),

	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_B_STEP,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_b0_jf_b0_cfg, iwl9560_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_B_STEP,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_b0_jf_b0_cfg, iwl9560_name),

	_IWL_DEV_INFO(IWL_CFG_ANY, 0x1551,
		      IWL_CFG_MAC_TYPE_QU, SILICON_B_STEP,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_b0_jf_b0_cfg, iwl9560_killer_1550s_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, 0x1552,
		      IWL_CFG_MAC_TYPE_QU, SILICON_B_STEP,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_b0_jf_b0_cfg, iwl9560_killer_1550i_name),

	/* Qu C step */
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_C_STEP,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_c0_jf_b0_cfg, iwl9461_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_C_STEP,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_c0_jf_b0_cfg, iwl9461_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_C_STEP,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1_DIV,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_c0_jf_b0_cfg, iwl9462_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_C_STEP,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1_DIV,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_c0_jf_b0_cfg, iwl9462_name),

	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_C_STEP,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_c0_jf_b0_cfg, iwl9560_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_C_STEP,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_c0_jf_b0_cfg, iwl9560_name),

	_IWL_DEV_INFO(IWL_CFG_ANY, 0x1551,
		      IWL_CFG_MAC_TYPE_QU, SILICON_C_STEP,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_c0_jf_b0_cfg, iwl9560_killer_1550s_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, 0x1552,
		      IWL_CFG_MAC_TYPE_QU, SILICON_C_STEP,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_qu_c0_jf_b0_cfg, iwl9560_killer_1550i_name),

	/* QuZ */
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QUZ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_quz_a0_jf_b0_cfg, iwl9461_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QUZ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_quz_a0_jf_b0_cfg, iwl9461_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QUZ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1_DIV,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_quz_a0_jf_b0_cfg, iwl9462_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QUZ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1_DIV,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_quz_a0_jf_b0_cfg, iwl9462_name),

	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QUZ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_quz_a0_jf_b0_cfg, iwl9560_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QUZ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_quz_a0_jf_b0_cfg, iwl9560_name),

	_IWL_DEV_INFO(IWL_CFG_ANY, 0x1551,
		      IWL_CFG_MAC_TYPE_QUZ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_quz_a0_jf_b0_cfg, iwl9560_killer_1550s_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, 0x1552,
		      IWL_CFG_MAC_TYPE_QUZ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_quz_a0_jf_b0_cfg, iwl9560_killer_1550i_name),

	/* QnJ */
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_qnj_b0_jf_b0_cfg, iwl9461_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_qnj_b0_jf_b0_cfg, iwl9461_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1_DIV,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_qnj_b0_jf_b0_cfg, iwl9462_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF1, IWL_CFG_RF_ID_JF1_DIV,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_qnj_b0_jf_b0_cfg, iwl9462_name),

	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_qnj_b0_jf_b0_cfg, iwl9560_160_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_qnj_b0_jf_b0_cfg, iwl9560_name),

	_IWL_DEV_INFO(IWL_CFG_ANY, 0x1551,
		      IWL_CFG_MAC_TYPE_QNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_160, IWL_CFG_CORES_BT,
		      iwl9560_qnj_b0_jf_b0_cfg, iwl9560_killer_1550s_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, 0x1552,
		      IWL_CFG_MAC_TYPE_QNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_JF2, IWL_CFG_RF_ID_JF,
		      IWL_CFG_NO_160, IWL_CFG_CORES_BT,
		      iwl9560_qnj_b0_jf_b0_cfg, iwl9560_killer_1550i_name),

/* Qu with Hr */
	/* Qu B step */
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_B_STEP,
		      IWL_CFG_RF_TYPE_HR1, IWL_CFG_ANY,
		      IWL_CFG_ANY, IWL_CFG_ANY,
		      iwl_qu_b0_hr1_b0, iwl_ax101_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_C_STEP,
		      IWL_CFG_RF_TYPE_HR2, IWL_CFG_ANY,
		      IWL_CFG_ANY, IWL_CFG_ANY,
		      iwl_qu_b0_hr_b0, iwl_ax203_name),

	/* Qu C step */
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_C_STEP,
		      IWL_CFG_RF_TYPE_HR1, IWL_CFG_ANY,
		      IWL_CFG_ANY, IWL_CFG_ANY,
		      iwl_qu_c0_hr1_b0, iwl_ax101_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QU, SILICON_C_STEP,
		      IWL_CFG_RF_TYPE_HR2, IWL_CFG_ANY,
		      IWL_CFG_ANY, IWL_CFG_ANY,
		      iwl_qu_c0_hr_b0, iwl_ax203_name),

	/* QuZ */
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_QUZ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_HR1, IWL_CFG_ANY,
		      IWL_CFG_ANY, IWL_CFG_ANY,
		      iwl_quz_a0_hr1_b0, iwl_ax101_name),

/* Ma */
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_MA, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_GF, IWL_CFG_ANY,
		      IWL_CFG_ANY, IWL_CFG_ANY,
		      iwl_cfg_ma_a0_gf_a0, iwl_ax211_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_MA, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_MR, IWL_CFG_ANY,
		      IWL_CFG_ANY, IWL_CFG_ANY,
		      iwl_cfg_ma_a0_mr_a0, iwl_ma_name),
	_IWL_DEV_INFO(IWL_CFG_ANY, IWL_CFG_ANY,
		      IWL_CFG_MAC_TYPE_SNJ, IWL_CFG_ANY,
		      IWL_CFG_RF_TYPE_MR, IWL_CFG_ANY,
		      IWL_CFG_ANY, IWL_CFG_ANY,
		      iwl_cfg_snj_a0_mr_a0, iwl_ma_name),


#endif /* CONFIG_IWLMVM */
};

/* PCI registers */
#define PCI_CFG_RETRY_TIMEOUT	0x041

static int iwl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct iwl_cfg_trans_params *trans;
	const struct iwl_cfg *cfg_7265d __maybe_unused = NULL;
	struct iwl_trans *iwl_trans;
	struct iwl_trans_pcie *trans_pcie;
	unsigned long flags;
	int i, ret;
	const struct iwl_cfg *cfg;

	trans = (void *)(ent->driver_data & ~TRANS_CFG_MARKER);

	/*
	 * This is needed for backwards compatibility with the old
	 * tables, so we don't need to change all the config structs
	 * at the same time.  The cfg is used to compare with the old
	 * full cfg structs.
	 */
	cfg = (void *)(ent->driver_data & ~TRANS_CFG_MARKER);

	/* make sure trans is the first element in iwl_cfg */
	BUILD_BUG_ON(offsetof(struct iwl_cfg, trans));

	iwl_trans = iwl_trans_pcie_alloc(pdev, ent, trans);
	if (IS_ERR(iwl_trans))
		return PTR_ERR(iwl_trans);

	trans_pcie = IWL_TRANS_GET_PCIE_TRANS(iwl_trans);

	iwl_trans->hw_rf_id = iwl_read32(iwl_trans, CSR_HW_RF_ID);

	for (i = 0; i < ARRAY_SIZE(iwl_dev_info_table); i++) {
		const struct iwl_dev_info *dev_info = &iwl_dev_info_table[i];
		if ((dev_info->device == (u16)IWL_CFG_ANY ||
		     dev_info->device == pdev->device) &&
		    (dev_info->subdevice == (u16)IWL_CFG_ANY ||
		     dev_info->subdevice == pdev->subsystem_device) &&
		    (dev_info->mac_type == (u16)IWL_CFG_ANY ||
		     dev_info->mac_type ==
		     CSR_HW_REV_TYPE(iwl_trans->hw_rev)) &&
		    (dev_info->mac_step == (u8)IWL_CFG_ANY ||
		     dev_info->mac_step ==
		     CSR_HW_REV_STEP(iwl_trans->hw_rev)) &&
		    (dev_info->rf_type == (u16)IWL_CFG_ANY ||
		     dev_info->rf_type ==
		     CSR_HW_RFID_TYPE(iwl_trans->hw_rf_id)) &&
		    (dev_info->rf_id == (u8)IWL_CFG_ANY ||
		     dev_info->rf_id ==
		     IWL_SUBDEVICE_RF_ID(pdev->subsystem_device)) &&
		    (dev_info->no_160 == (u8)IWL_CFG_ANY ||
		     dev_info->no_160 ==
		     IWL_SUBDEVICE_NO_160(pdev->subsystem_device)) &&
		    (dev_info->cores == (u8)IWL_CFG_ANY ||
		     dev_info->cores ==
		     IWL_SUBDEVICE_CORES(pdev->subsystem_device))) {
			iwl_trans->cfg = dev_info->cfg;
			iwl_trans->name = dev_info->name;
		}
	}

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
	    (iwl_trans->hw_rev & CSR_HW_REV_TYPE_MSK) == CSR_HW_REV_TYPE_7265D)
		iwl_trans->cfg = cfg_7265d;

	if (cfg == &iwlax210_2ax_cfg_so_hr_a0) {
		if (iwl_trans->hw_rev == CSR_HW_REV_TYPE_TY) {
			iwl_trans->cfg = &iwlax210_2ax_cfg_ty_gf_a0;
		} else if (CSR_HW_RF_ID_TYPE_CHIP_ID(iwl_trans->hw_rf_id) ==
			   CSR_HW_RF_ID_TYPE_CHIP_ID(CSR_HW_RF_ID_TYPE_JF)) {
			iwl_trans->cfg = &iwlax210_2ax_cfg_so_jf_a0;
		} else if (CSR_HW_RF_ID_TYPE_CHIP_ID(iwl_trans->hw_rf_id) ==
			   CSR_HW_RF_ID_TYPE_CHIP_ID(CSR_HW_RF_ID_TYPE_GF)) {
			iwl_trans->cfg = &iwlax211_2ax_cfg_so_gf_a0;
		} else if (CSR_HW_RF_ID_TYPE_CHIP_ID(iwl_trans->hw_rf_id) ==
			   CSR_HW_RF_ID_TYPE_CHIP_ID(CSR_HW_RF_ID_TYPE_GF4)) {
			iwl_trans->cfg = &iwlax411_2ax_cfg_so_gf4_a0;
		}
	}

	/*
	 * This is a hack to switch from Qu B0 to Qu C0.  We need to
	 * do this for all cfgs that use Qu B0, except for those using
	 * Jf, which have already been moved to the new table.  The
	 * rest must be removed once we convert Qu with Hr as well.
	 */
	if (iwl_trans->hw_rev == CSR_HW_REV_TYPE_QU_C0) {
		if (iwl_trans->cfg == &iwl_ax201_cfg_qu_hr)
			iwl_trans->cfg = &iwl_ax201_cfg_qu_c0_hr_b0;
		else if (iwl_trans->cfg == &killer1650s_2ax_cfg_qu_b0_hr_b0)
			iwl_trans->cfg = &killer1650s_2ax_cfg_qu_c0_hr_b0;
		else if (iwl_trans->cfg == &killer1650i_2ax_cfg_qu_b0_hr_b0)
			iwl_trans->cfg = &killer1650i_2ax_cfg_qu_c0_hr_b0;
	}

	/* same thing for QuZ... */
	if (iwl_trans->hw_rev == CSR_HW_REV_TYPE_QUZ) {
		if (iwl_trans->cfg == &iwl_ax201_cfg_qu_hr)
			iwl_trans->cfg = &iwl_ax201_cfg_quz_hr;
		else if (iwl_trans->cfg == &killer1650s_2ax_cfg_qu_b0_hr_b0)
			iwl_trans->cfg = &iwl_ax1650s_cfg_quz_hr;
		else if (iwl_trans->cfg == &killer1650i_2ax_cfg_qu_b0_hr_b0)
			iwl_trans->cfg = &iwl_ax1650i_cfg_quz_hr;
	}

#endif
	/*
	 * If we didn't set the cfg yet, the PCI ID table entry should have
	 * been a full config - if yes, use it, otherwise fail.
	 */
	if (!iwl_trans->cfg) {
		if (ent->driver_data & TRANS_CFG_MARKER) {
			pr_err("No config found for PCI dev %04x/%04x, rev=0x%x, rfid=0x%x\n",
			       pdev->device, pdev->subsystem_device,
			       iwl_trans->hw_rev, iwl_trans->hw_rf_id);
			ret = -EINVAL;
			goto out_free_trans;
		}
		iwl_trans->cfg = cfg;
	}

	/* if we don't have a name yet, copy name from the old cfg */
	if (!iwl_trans->name)
		iwl_trans->name = iwl_trans->cfg->name;

	if (iwl_trans->trans_cfg->mq_rx_supported) {
		if (WARN_ON(!iwl_trans->cfg->num_rbds)) {
			ret = -EINVAL;
			goto out_free_trans;
		}
		trans_pcie->num_rx_bufs = iwl_trans->cfg->num_rbds;
	} else {
		trans_pcie->num_rx_bufs = RX_QUEUE_SIZE;
	}

	if (iwl_trans->trans_cfg->device_family >= IWL_DEVICE_FAMILY_8000 &&
	    iwl_trans_grab_nic_access(iwl_trans, &flags)) {
		u32 hw_step;

		hw_step = iwl_read_umac_prph_no_grab(iwl_trans, WFPM_CTRL_REG);
		hw_step |= ENABLE_WFPM;
		iwl_write_umac_prph_no_grab(iwl_trans, WFPM_CTRL_REG, hw_step);
		hw_step = iwl_read_prph_no_grab(iwl_trans, CNVI_AUX_MISC_CHIP);
		hw_step = (hw_step >> HW_STEP_LOCATION_BITS) & 0xF;
		if (hw_step == 0x3)
			iwl_trans->hw_rev = (iwl_trans->hw_rev & 0xFFFFFFF3) |
				(SILICON_C_STEP << 2);
		iwl_trans_release_nic_access(iwl_trans, &flags);
	}

	pci_set_drvdata(pdev, iwl_trans);
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

static const struct dev_pm_ops iwl_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(iwl_pci_suspend,
				iwl_pci_resume)
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
