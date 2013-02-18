/******************************************************************************
 *
 * This file is provided under a dual BSD/GPLv2 license.  When using or
 * redistributing this file, you may do so under either license.
 *
 * GPL LICENSE SUMMARY
 *
 * Copyright(c) 2007 - 2013 Intel Corporation. All rights reserved.
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
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110,
 * USA
 *
 * The full GNU General Public License is included in this distribution
 * in the file called COPYING.
 *
 * Contact Information:
 *  Intel Linux Wireless <ilw@linux.intel.com>
 * Intel Corporation, 5200 N.E. Elam Young Parkway, Hillsboro, OR 97124-6497
 *
 * BSD LICENSE
 *
 * Copyright(c) 2005 - 2013 Intel Corporation. All rights reserved.
 * All rights reserved.
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
#include <linux/pci-aspm.h>

#include "iwl-trans.h"
#include "iwl-drv.h"

#include "cfg.h"
#include "internal.h"

#define IWL_PCI_DEVICE(dev, subdev, cfg) \
	.vendor = PCI_VENDOR_ID_INTEL,  .device = (dev), \
	.subvendor = PCI_ANY_ID, .subdevice = (subdev), \
	.driver_data = (kernel_ulong_t)&(cfg)

/* Hardware specific file defines the PCI IDs table for that hardware module */
static DEFINE_PCI_DEVICE_TABLE(iwl_hw_card_ids) = {
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

	{IWL_PCI_DEVICE(0x423D, 0x1211, iwl5150_agn_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423D, 0x1311, iwl5150_agn_cfg)}, /* Half Mini Card */
	{IWL_PCI_DEVICE(0x423D, 0x1216, iwl5150_abg_cfg)}, /* Mini Card */
	{IWL_PCI_DEVICE(0x423D, 0x1316, iwl5150_abg_cfg)}, /* Half Mini Card */

/* 6x00 Series */
	{IWL_PCI_DEVICE(0x422B, 0x1101, iwl6000_3agn_cfg)},
	{IWL_PCI_DEVICE(0x422B, 0x1121, iwl6000_3agn_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1301, iwl6000i_2agn_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1306, iwl6000i_2abg_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1307, iwl6000i_2bg_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1321, iwl6000i_2agn_cfg)},
	{IWL_PCI_DEVICE(0x422C, 0x1326, iwl6000i_2abg_cfg)},
	{IWL_PCI_DEVICE(0x4238, 0x1111, iwl6000_3agn_cfg)},
	{IWL_PCI_DEVICE(0x4239, 0x1311, iwl6000i_2agn_cfg)},
	{IWL_PCI_DEVICE(0x4239, 0x1316, iwl6000i_2abg_cfg)},

/* 6x05 Series */
	{IWL_PCI_DEVICE(0x0082, 0x1301, iwl6005_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1306, iwl6005_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1307, iwl6005_2bg_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1321, iwl6005_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0x1326, iwl6005_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0x1311, iwl6005_2agn_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0x1316, iwl6005_2abg_cfg)},
	{IWL_PCI_DEVICE(0x0082, 0xC020, iwl6005_2agn_sff_cfg)},
	{IWL_PCI_DEVICE(0x0085, 0xC220, iwl6005_2agn_sff_cfg)},
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
	{IWL_PCI_DEVICE(0x088F, 0x4260, iwl6035_2agn_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x4460, iwl6035_2agn_cfg)},
	{IWL_PCI_DEVICE(0x088E, 0x4860, iwl6035_2agn_cfg)},

/* 105 Series */
	{IWL_PCI_DEVICE(0x0894, 0x0022, iwl105_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0895, 0x0222, iwl105_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0894, 0x0422, iwl105_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0894, 0x0822, iwl105_bgn_d_cfg)},

/* 135 Series */
	{IWL_PCI_DEVICE(0x0892, 0x0062, iwl135_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0893, 0x0262, iwl135_bgn_cfg)},
	{IWL_PCI_DEVICE(0x0892, 0x0462, iwl135_bgn_cfg)},

/* 7000 Series */
	{IWL_PCI_DEVICE(0x08B1, 0x4070, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B1, 0xC070, iwl7260_2ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x0070, iwl3160_ac_cfg)},
	{IWL_PCI_DEVICE(0x08B3, 0x8070, iwl3160_ac_cfg)},

	{0}
};
MODULE_DEVICE_TABLE(pci, iwl_hw_card_ids);

/* PCI registers */
#define PCI_CFG_RETRY_TIMEOUT	0x041

static int iwl_pci_probe(struct pci_dev *pdev, const struct pci_device_id *ent)
{
	const struct iwl_cfg *cfg = (struct iwl_cfg *)(ent->driver_data);
	struct iwl_trans *iwl_trans;
	struct iwl_trans_pcie *trans_pcie;
	int ret;

	iwl_trans = iwl_trans_pcie_alloc(pdev, ent, cfg);
	if (iwl_trans == NULL)
		return -ENOMEM;

	pci_set_drvdata(pdev, iwl_trans);

	trans_pcie = IWL_TRANS_GET_PCIE_TRANS(iwl_trans);
	trans_pcie->drv = iwl_drv_start(iwl_trans, cfg);

	if (IS_ERR_OR_NULL(trans_pcie->drv)) {
		ret = PTR_ERR(trans_pcie->drv);
		goto out_free_trans;
	}

	/* register transport layer debugfs here */
	ret = iwl_trans_dbgfs_register(iwl_trans, iwl_trans->dbgfs_dir);
	if (ret)
		goto out_free_drv;

	return 0;

out_free_drv:
	iwl_drv_stop(trans_pcie->drv);
out_free_trans:
	iwl_trans_pcie_free(iwl_trans);
	pci_set_drvdata(pdev, NULL);
	return ret;
}

static void iwl_pci_remove(struct pci_dev *pdev)
{
	struct iwl_trans *trans = pci_get_drvdata(pdev);
	struct iwl_trans_pcie *trans_pcie = IWL_TRANS_GET_PCIE_TRANS(trans);

	iwl_drv_stop(trans_pcie->drv);
	iwl_trans_pcie_free(trans);

	pci_set_drvdata(pdev, NULL);
}

#ifdef CONFIG_PM_SLEEP

static int iwl_pci_suspend(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct iwl_trans *iwl_trans = pci_get_drvdata(pdev);

	/* Before you put code here, think about WoWLAN. You cannot check here
	 * whether WoWLAN is enabled or not, and your code will run even if
	 * WoWLAN is enabled - don't kill the NIC, someone may need it in Sx.
	 */

	return iwl_trans_suspend(iwl_trans);
}

static int iwl_pci_resume(struct device *device)
{
	struct pci_dev *pdev = to_pci_dev(device);
	struct iwl_trans *iwl_trans = pci_get_drvdata(pdev);

	/* Before you put code here, think about WoWLAN. You cannot check here
	 * whether WoWLAN is enabled or not, and your code will run even if
	 * WoWLAN is enabled - the NIC may be alive.
	 */

	/*
	 * We disable the RETRY_TIMEOUT register (0x41) to keep
	 * PCI Tx retries from interfering with C3 CPU state.
	 */
	pci_write_config_byte(pdev, PCI_CFG_RETRY_TIMEOUT, 0x00);

	return iwl_trans_resume(iwl_trans);
}

static SIMPLE_DEV_PM_OPS(iwl_dev_pm_ops, iwl_pci_suspend, iwl_pci_resume);

#define IWL_PM_OPS	(&iwl_dev_pm_ops)

#else

#define IWL_PM_OPS	NULL

#endif

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
