/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * nfp_main.c
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Alejandro Lucero <alejandro.lucero@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/pci.h>
#include <linux/firmware.h>
#include <linux/vermagic.h>
#include <net/devlink.h>

#include "nfpcore/nfp.h"
#include "nfpcore/nfp_cpp.h"
#include "nfpcore/nfp_nffw.h"
#include "nfpcore/nfp_nsp.h"

#include "nfpcore/nfp6000_pcie.h"

#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_net.h"

static const char nfp_driver_name[] = "nfp";
const char nfp_driver_version[] = VERMAGIC_STRING;

static const struct pci_device_id nfp_pci_device_ids[] = {
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_ID_NETRONOME_NFP6000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_ID_NETRONOME_NFP4000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0,
	},
	{ 0, } /* Required last entry. */
};
MODULE_DEVICE_TABLE(pci, nfp_pci_device_ids);

static int nfp_pcie_sriov_read_nfd_limit(struct nfp_pf *pf)
{
	int err;

	pf->limit_vfs = nfp_rtsym_read_le(pf->rtbl, "nfd_vf_cfg_max_vfs", &err);
	if (!err)
		return pci_sriov_set_totalvfs(pf->pdev, pf->limit_vfs);

	pf->limit_vfs = ~0;
	pci_sriov_set_totalvfs(pf->pdev, 0); /* 0 is unset */
	/* Allow any setting for backwards compatibility if symbol not found */
	if (err == -ENOENT)
		return 0;

	nfp_warn(pf->cpp, "Warning: VF limit read failed: %d\n", err);
	return err;
}

static int nfp_pcie_sriov_enable(struct pci_dev *pdev, int num_vfs)
{
#ifdef CONFIG_PCI_IOV
	struct nfp_pf *pf = pci_get_drvdata(pdev);
	int err;

	mutex_lock(&pf->lock);

	if (num_vfs > pf->limit_vfs) {
		nfp_info(pf->cpp, "Firmware limits number of VFs to %u\n",
			 pf->limit_vfs);
		err = -EINVAL;
		goto err_unlock;
	}

	err = pci_enable_sriov(pdev, num_vfs);
	if (err) {
		dev_warn(&pdev->dev, "Failed to enable PCI SR-IOV: %d\n", err);
		goto err_unlock;
	}

	err = nfp_app_sriov_enable(pf->app, num_vfs);
	if (err) {
		dev_warn(&pdev->dev,
			 "App specific PCI SR-IOV configuration failed: %d\n",
			 err);
		goto err_sriov_disable;
	}

	pf->num_vfs = num_vfs;

	dev_dbg(&pdev->dev, "Created %d VFs.\n", pf->num_vfs);

	mutex_unlock(&pf->lock);
	return num_vfs;

err_sriov_disable:
	pci_disable_sriov(pdev);
err_unlock:
	mutex_unlock(&pf->lock);
	return err;
#endif
	return 0;
}

static int nfp_pcie_sriov_disable(struct pci_dev *pdev)
{
#ifdef CONFIG_PCI_IOV
	struct nfp_pf *pf = pci_get_drvdata(pdev);

	mutex_lock(&pf->lock);

	/* If the VFs are assigned we cannot shut down SR-IOV without
	 * causing issues, so just leave the hardware available but
	 * disabled
	 */
	if (pci_vfs_assigned(pdev)) {
		dev_warn(&pdev->dev, "Disabling while VFs assigned - VFs will not be deallocated\n");
		mutex_unlock(&pf->lock);
		return -EPERM;
	}

	nfp_app_sriov_disable(pf->app);

	pf->num_vfs = 0;

	pci_disable_sriov(pdev);
	dev_dbg(&pdev->dev, "Removed VFs.\n");

	mutex_unlock(&pf->lock);
#endif
	return 0;
}

static int nfp_pcie_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	if (num_vfs == 0)
		return nfp_pcie_sriov_disable(pdev);
	else
		return nfp_pcie_sriov_enable(pdev, num_vfs);
}

/**
 * nfp_net_fw_find() - Find the correct firmware image for netdev mode
 * @pdev:	PCI Device structure
 * @pf:		NFP PF Device structure
 *
 * Return: firmware if found and requested successfully.
 */
static const struct firmware *
nfp_net_fw_find(struct pci_dev *pdev, struct nfp_pf *pf)
{
	const struct firmware *fw = NULL;
	struct nfp_eth_table_port *port;
	const char *fw_model;
	char fw_name[256];
	int spc, err = 0;
	int i, j;

	if (!pf->eth_tbl) {
		dev_err(&pdev->dev, "Error: can't identify media config\n");
		return NULL;
	}

	fw_model = nfp_hwinfo_lookup(pf->hwinfo, "assembly.partno");
	if (!fw_model) {
		dev_err(&pdev->dev, "Error: can't read part number\n");
		return NULL;
	}

	spc = ARRAY_SIZE(fw_name);
	spc -= snprintf(fw_name, spc, "netronome/nic_%s", fw_model);

	for (i = 0; spc > 0 && i < pf->eth_tbl->count; i += j) {
		port = &pf->eth_tbl->ports[i];
		j = 1;
		while (i + j < pf->eth_tbl->count &&
		       port->speed == port[j].speed)
			j++;

		spc -= snprintf(&fw_name[ARRAY_SIZE(fw_name) - spc], spc,
				"_%dx%d", j, port->speed / 1000);
	}

	if (spc <= 0)
		return NULL;

	spc -= snprintf(&fw_name[ARRAY_SIZE(fw_name) - spc], spc, ".nffw");
	if (spc <= 0)
		return NULL;

	err = request_firmware(&fw, fw_name, &pdev->dev);
	if (err)
		return NULL;

	dev_info(&pdev->dev, "Loading FW image: %s\n", fw_name);

	return fw;
}

/**
 * nfp_net_fw_load() - Load the firmware image
 * @pdev:       PCI Device structure
 * @pf:		NFP PF Device structure
 * @nsp:	NFP SP handle
 *
 * Return: -ERRNO, 0 for no firmware loaded, 1 for firmware loaded
 */
static int
nfp_fw_load(struct pci_dev *pdev, struct nfp_pf *pf, struct nfp_nsp *nsp)
{
	const struct firmware *fw;
	u16 interface;
	int err;

	interface = nfp_cpp_interface(pf->cpp);
	if (NFP_CPP_INTERFACE_UNIT_of(interface) != 0) {
		/* Only Unit 0 should reset or load firmware */
		dev_info(&pdev->dev, "Firmware will be loaded by partner\n");
		return 0;
	}

	fw = nfp_net_fw_find(pdev, pf);
	if (!fw)
		return 0;

	dev_info(&pdev->dev, "Soft-reset, loading FW image\n");
	err = nfp_nsp_device_soft_reset(nsp);
	if (err < 0) {
		dev_err(&pdev->dev, "Failed to soft reset the NFP: %d\n",
			err);
		goto exit_release_fw;
	}

	err = nfp_nsp_load_fw(nsp, fw);

	if (err < 0) {
		dev_err(&pdev->dev, "FW loading failed: %d\n", err);
		goto exit_release_fw;
	}

	dev_info(&pdev->dev, "Finished loading FW image\n");

exit_release_fw:
	release_firmware(fw);

	return err < 0 ? err : 1;
}

static int nfp_nsp_init(struct pci_dev *pdev, struct nfp_pf *pf)
{
	struct nfp_nsp *nsp;
	int err;

	nsp = nfp_nsp_open(pf->cpp);
	if (IS_ERR(nsp)) {
		err = PTR_ERR(nsp);
		dev_err(&pdev->dev, "Failed to access the NSP: %d\n", err);
		return err;
	}

	err = nfp_nsp_wait(nsp);
	if (err < 0)
		goto exit_close_nsp;

	pf->eth_tbl = __nfp_eth_read_ports(pf->cpp, nsp);

	pf->nspi = __nfp_nsp_identify(nsp);
	if (pf->nspi)
		dev_info(&pdev->dev, "BSP: %s\n", pf->nspi->version);

	err = nfp_fw_load(pdev, pf, nsp);
	if (err < 0) {
		kfree(pf->nspi);
		kfree(pf->eth_tbl);
		dev_err(&pdev->dev, "Failed to load FW\n");
		goto exit_close_nsp;
	}

	pf->fw_loaded = !!err;
	err = 0;

exit_close_nsp:
	nfp_nsp_close(nsp);

	return err;
}

static void nfp_fw_unload(struct nfp_pf *pf)
{
	struct nfp_nsp *nsp;
	int err;

	nsp = nfp_nsp_open(pf->cpp);
	if (IS_ERR(nsp)) {
		nfp_err(pf->cpp, "Reset failed, can't open NSP\n");
		return;
	}

	err = nfp_nsp_device_soft_reset(nsp);
	if (err < 0)
		dev_warn(&pf->pdev->dev, "Couldn't unload firmware: %d\n", err);
	else
		dev_info(&pf->pdev->dev, "Firmware safely unloaded\n");

	nfp_nsp_close(nsp);
}

static int nfp_pci_probe(struct pci_dev *pdev,
			 const struct pci_device_id *pci_id)
{
	struct devlink *devlink;
	struct nfp_pf *pf;
	int err;

	err = pci_enable_device(pdev);
	if (err < 0)
		return err;

	pci_set_master(pdev);

	err = dma_set_mask_and_coherent(&pdev->dev,
					DMA_BIT_MASK(NFP_NET_MAX_DMA_BITS));
	if (err)
		goto err_pci_disable;

	err = pci_request_regions(pdev, nfp_driver_name);
	if (err < 0) {
		dev_err(&pdev->dev, "Unable to reserve pci resources.\n");
		goto err_pci_disable;
	}

	devlink = devlink_alloc(&nfp_devlink_ops, sizeof(*pf));
	if (!devlink) {
		err = -ENOMEM;
		goto err_rel_regions;
	}
	pf = devlink_priv(devlink);
	INIT_LIST_HEAD(&pf->vnics);
	INIT_LIST_HEAD(&pf->ports);
	mutex_init(&pf->lock);
	pci_set_drvdata(pdev, pf);
	pf->pdev = pdev;

	pf->wq = alloc_workqueue("nfp-%s", 0, 2, pci_name(pdev));
	if (!pf->wq) {
		err = -ENOMEM;
		goto err_pci_priv_unset;
	}

	pf->cpp = nfp_cpp_from_nfp6000_pcie(pdev);
	if (IS_ERR_OR_NULL(pf->cpp)) {
		err = PTR_ERR(pf->cpp);
		if (err >= 0)
			err = -ENOMEM;
		goto err_disable_msix;
	}

	pf->hwinfo = nfp_hwinfo_read(pf->cpp);

	dev_info(&pdev->dev, "Assembly: %s%s%s-%s CPLD: %s\n",
		 nfp_hwinfo_lookup(pf->hwinfo, "assembly.vendor"),
		 nfp_hwinfo_lookup(pf->hwinfo, "assembly.partno"),
		 nfp_hwinfo_lookup(pf->hwinfo, "assembly.serial"),
		 nfp_hwinfo_lookup(pf->hwinfo, "assembly.revision"),
		 nfp_hwinfo_lookup(pf->hwinfo, "cpld.version"));

	err = devlink_register(devlink, &pdev->dev);
	if (err)
		goto err_hwinfo_free;

	err = nfp_nsp_init(pdev, pf);
	if (err)
		goto err_devlink_unreg;

	pf->mip = nfp_mip_open(pf->cpp);
	pf->rtbl = __nfp_rtsym_table_read(pf->cpp, pf->mip);

	err = nfp_pcie_sriov_read_nfd_limit(pf);
	if (err)
		goto err_fw_unload;

	pf->num_vfs = pci_num_vf(pdev);
	if (pf->num_vfs > pf->limit_vfs) {
		dev_err(&pdev->dev,
			"Error: %d VFs already enabled, but loaded FW can only support %d\n",
			pf->num_vfs, pf->limit_vfs);
		goto err_fw_unload;
	}

	err = nfp_net_pci_probe(pf);
	if (err)
		goto err_sriov_unlimit;

	err = nfp_hwmon_register(pf);
	if (err) {
		dev_err(&pdev->dev, "Failed to register hwmon info\n");
		goto err_net_remove;
	}

	return 0;

err_net_remove:
	nfp_net_pci_remove(pf);
err_sriov_unlimit:
	pci_sriov_set_totalvfs(pf->pdev, 0);
err_fw_unload:
	kfree(pf->rtbl);
	nfp_mip_close(pf->mip);
	if (pf->fw_loaded)
		nfp_fw_unload(pf);
	kfree(pf->eth_tbl);
	kfree(pf->nspi);
err_devlink_unreg:
	devlink_unregister(devlink);
err_hwinfo_free:
	kfree(pf->hwinfo);
	nfp_cpp_free(pf->cpp);
err_disable_msix:
	destroy_workqueue(pf->wq);
err_pci_priv_unset:
	pci_set_drvdata(pdev, NULL);
	mutex_destroy(&pf->lock);
	devlink_free(devlink);
err_rel_regions:
	pci_release_regions(pdev);
err_pci_disable:
	pci_disable_device(pdev);

	return err;
}

static void nfp_pci_remove(struct pci_dev *pdev)
{
	struct nfp_pf *pf = pci_get_drvdata(pdev);
	struct devlink *devlink;

	nfp_hwmon_unregister(pf);

	devlink = priv_to_devlink(pf);

	nfp_net_pci_remove(pf);

	nfp_pcie_sriov_disable(pdev);
	pci_sriov_set_totalvfs(pf->pdev, 0);

	devlink_unregister(devlink);

	kfree(pf->rtbl);
	nfp_mip_close(pf->mip);
	if (pf->fw_loaded)
		nfp_fw_unload(pf);

	destroy_workqueue(pf->wq);
	pci_set_drvdata(pdev, NULL);
	kfree(pf->hwinfo);
	nfp_cpp_free(pf->cpp);

	kfree(pf->eth_tbl);
	kfree(pf->nspi);
	mutex_destroy(&pf->lock);
	devlink_free(devlink);
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static struct pci_driver nfp_pci_driver = {
	.name			= nfp_driver_name,
	.id_table		= nfp_pci_device_ids,
	.probe			= nfp_pci_probe,
	.remove			= nfp_pci_remove,
	.sriov_configure	= nfp_pcie_sriov_configure,
};

static int __init nfp_main_init(void)
{
	int err;

	pr_info("%s: NFP PCIe Driver, Copyright (C) 2014-2017 Netronome Systems\n",
		nfp_driver_name);

	nfp_net_debugfs_create();

	err = pci_register_driver(&nfp_pci_driver);
	if (err < 0)
		goto err_destroy_debugfs;

	err = pci_register_driver(&nfp_netvf_pci_driver);
	if (err)
		goto err_unreg_pf;

	return err;

err_unreg_pf:
	pci_unregister_driver(&nfp_pci_driver);
err_destroy_debugfs:
	nfp_net_debugfs_destroy();
	return err;
}

static void __exit nfp_main_exit(void)
{
	pci_unregister_driver(&nfp_netvf_pci_driver);
	pci_unregister_driver(&nfp_pci_driver);
	nfp_net_debugfs_destroy();
}

module_init(nfp_main_init);
module_exit(nfp_main_exit);

MODULE_FIRMWARE("netronome/nic_AMDA0081-0001_1x40.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0081-0001_4x10.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0096-0001_2x10.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0097-0001_2x40.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0097-0001_4x10_1x40.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0097-0001_8x10.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0099-0001_2x10.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0099-0001_2x25.nffw");

MODULE_AUTHOR("Netronome Systems <oss-drivers@netronome.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("The Netronome Flow Processor (NFP) driver.");
