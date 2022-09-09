// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2015-2018 Netronome Systems, Inc. */

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
#include <linux/vmalloc.h>
#include <net/devlink.h>

#include "nfpcore/nfp.h"
#include "nfpcore/nfp_cpp.h"
#include "nfpcore/nfp_dev.h"
#include "nfpcore/nfp_nffw.h"
#include "nfpcore/nfp_nsp.h"

#include "nfpcore/nfp6000_pcie.h"

#include "nfp_abi.h"
#include "nfp_app.h"
#include "nfp_main.h"
#include "nfp_net.h"

static const char nfp_driver_name[] = "nfp";

static const struct pci_device_id nfp_pci_device_ids[] = {
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_ID_NFP3800,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0, NFP_DEV_NFP3800,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_ID_NFP4000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0, NFP_DEV_NFP6000,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_ID_NFP5000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0, NFP_DEV_NFP6000,
	},
	{ PCI_VENDOR_ID_NETRONOME, PCI_DEVICE_ID_NFP6000,
	  PCI_VENDOR_ID_NETRONOME, PCI_ANY_ID,
	  PCI_ANY_ID, 0, NFP_DEV_NFP6000,
	},
	{ PCI_VENDOR_ID_CORIGINE, PCI_DEVICE_ID_NFP3800,
	  PCI_VENDOR_ID_CORIGINE, PCI_ANY_ID,
	  PCI_ANY_ID, 0, NFP_DEV_NFP3800,
	},
	{ PCI_VENDOR_ID_CORIGINE, PCI_DEVICE_ID_NFP4000,
	  PCI_VENDOR_ID_CORIGINE, PCI_ANY_ID,
	  PCI_ANY_ID, 0, NFP_DEV_NFP6000,
	},
	{ PCI_VENDOR_ID_CORIGINE, PCI_DEVICE_ID_NFP5000,
	  PCI_VENDOR_ID_CORIGINE, PCI_ANY_ID,
	  PCI_ANY_ID, 0, NFP_DEV_NFP6000,
	},
	{ PCI_VENDOR_ID_CORIGINE, PCI_DEVICE_ID_NFP6000,
	  PCI_VENDOR_ID_CORIGINE, PCI_ANY_ID,
	  PCI_ANY_ID, 0, NFP_DEV_NFP6000,
	},
	{ 0, } /* Required last entry. */
};
MODULE_DEVICE_TABLE(pci, nfp_pci_device_ids);

int nfp_pf_rtsym_read_optional(struct nfp_pf *pf, const char *format,
			       unsigned int default_val)
{
	char name[256];
	int err = 0;
	u64 val;

	snprintf(name, sizeof(name), format, nfp_cppcore_pcie_unit(pf->cpp));

	val = nfp_rtsym_read_le(pf->rtbl, name, &err);
	if (err) {
		if (err == -ENOENT)
			return default_val;
		nfp_err(pf->cpp, "Unable to read symbol %s\n", name);
		return err;
	}

	return val;
}

u8 __iomem *
nfp_pf_map_rtsym(struct nfp_pf *pf, const char *name, const char *sym_fmt,
		 unsigned int min_size, struct nfp_cpp_area **area)
{
	char pf_symbol[256];

	snprintf(pf_symbol, sizeof(pf_symbol), sym_fmt,
		 nfp_cppcore_pcie_unit(pf->cpp));

	return nfp_rtsym_map(pf->rtbl, pf_symbol, name, min_size, area);
}

/* Callers should hold the devlink instance lock */
int nfp_mbox_cmd(struct nfp_pf *pf, u32 cmd, void *in_data, u64 in_length,
		 void *out_data, u64 out_length)
{
	unsigned long err_at;
	u64 max_data_sz;
	u32 val = 0;
	int n, err;

	if (!pf->mbox)
		return -EOPNOTSUPP;

	max_data_sz = nfp_rtsym_size(pf->mbox) - NFP_MBOX_SYM_MIN_SIZE;

	/* Check if cmd field is clear */
	err = nfp_rtsym_readl(pf->cpp, pf->mbox, NFP_MBOX_CMD, &val);
	if (err || val) {
		nfp_warn(pf->cpp, "failed to issue command (%u): %u, err: %d\n",
			 cmd, val, err);
		return err ?: -EBUSY;
	}

	in_length = min(in_length, max_data_sz);
	n = nfp_rtsym_write(pf->cpp, pf->mbox, NFP_MBOX_DATA, in_data,
			    in_length);
	if (n != in_length)
		return -EIO;
	/* Write data_len and wipe reserved */
	err = nfp_rtsym_writeq(pf->cpp, pf->mbox, NFP_MBOX_DATA_LEN, in_length);
	if (err)
		return err;

	/* Read back for ordering */
	err = nfp_rtsym_readl(pf->cpp, pf->mbox, NFP_MBOX_DATA_LEN, &val);
	if (err)
		return err;

	/* Write cmd and wipe return value */
	err = nfp_rtsym_writeq(pf->cpp, pf->mbox, NFP_MBOX_CMD, cmd);
	if (err)
		return err;

	err_at = jiffies + 5 * HZ;
	while (true) {
		/* Wait for command to go to 0 (NFP_MBOX_NO_CMD) */
		err = nfp_rtsym_readl(pf->cpp, pf->mbox, NFP_MBOX_CMD, &val);
		if (err)
			return err;
		if (!val)
			break;

		if (time_is_before_eq_jiffies(err_at))
			return -ETIMEDOUT;

		msleep(5);
	}

	/* Copy output if any (could be error info, do it before reading ret) */
	err = nfp_rtsym_readl(pf->cpp, pf->mbox, NFP_MBOX_DATA_LEN, &val);
	if (err)
		return err;

	out_length = min_t(u32, val, min(out_length, max_data_sz));
	n = nfp_rtsym_read(pf->cpp, pf->mbox, NFP_MBOX_DATA,
			   out_data, out_length);
	if (n != out_length)
		return -EIO;

	/* Check if there is an error */
	err = nfp_rtsym_readl(pf->cpp, pf->mbox, NFP_MBOX_RET, &val);
	if (err)
		return err;
	if (val)
		return -val;

	return out_length;
}

static bool nfp_board_ready(struct nfp_pf *pf)
{
	const char *cp;
	long state;
	int err;

	cp = nfp_hwinfo_lookup(pf->hwinfo, "board.state");
	if (!cp)
		return false;

	err = kstrtol(cp, 0, &state);
	if (err < 0)
		return false;

	return state == 15;
}

static int nfp_pf_board_state_wait(struct nfp_pf *pf)
{
	const unsigned long wait_until = jiffies + 10 * HZ;

	while (!nfp_board_ready(pf)) {
		if (time_is_before_eq_jiffies(wait_until)) {
			nfp_err(pf->cpp, "NFP board initialization timeout\n");
			return -EINVAL;
		}

		nfp_info(pf->cpp, "waiting for board initialization\n");
		if (msleep_interruptible(500))
			return -ERESTARTSYS;

		/* Refresh cached information */
		kfree(pf->hwinfo);
		pf->hwinfo = nfp_hwinfo_read(pf->cpp);
	}

	return 0;
}

static int nfp_pcie_sriov_read_nfd_limit(struct nfp_pf *pf)
{
	int err;

	pf->limit_vfs = nfp_rtsym_read_le(pf->rtbl, "nfd_vf_cfg_max_vfs", &err);
	if (err) {
		/* For backwards compatibility if symbol not found allow all */
		pf->limit_vfs = ~0;
		if (err == -ENOENT)
			return 0;

		nfp_warn(pf->cpp, "Warning: VF limit read failed: %d\n", err);
		return err;
	}

	err = pci_sriov_set_totalvfs(pf->pdev, pf->limit_vfs);
	if (err)
		nfp_warn(pf->cpp, "Failed to set VF count in sysfs: %d\n", err);
	return 0;
}

static int nfp_pcie_sriov_enable(struct pci_dev *pdev, int num_vfs)
{
#ifdef CONFIG_PCI_IOV
	struct nfp_pf *pf = pci_get_drvdata(pdev);
	struct devlink *devlink;
	int err;

	if (num_vfs > pf->limit_vfs) {
		nfp_info(pf->cpp, "Firmware limits number of VFs to %u\n",
			 pf->limit_vfs);
		return -EINVAL;
	}

	err = pci_enable_sriov(pdev, num_vfs);
	if (err) {
		dev_warn(&pdev->dev, "Failed to enable PCI SR-IOV: %d\n", err);
		return err;
	}

	devlink = priv_to_devlink(pf);
	devl_lock(devlink);

	err = nfp_app_sriov_enable(pf->app, num_vfs);
	if (err) {
		dev_warn(&pdev->dev,
			 "App specific PCI SR-IOV configuration failed: %d\n",
			 err);
		goto err_sriov_disable;
	}

	pf->num_vfs = num_vfs;

	dev_dbg(&pdev->dev, "Created %d VFs.\n", pf->num_vfs);

	devl_unlock(devlink);
	return num_vfs;

err_sriov_disable:
	devl_unlock(devlink);
	pci_disable_sriov(pdev);
	return err;
#endif
	return 0;
}

static int nfp_pcie_sriov_disable(struct pci_dev *pdev)
{
#ifdef CONFIG_PCI_IOV
	struct nfp_pf *pf = pci_get_drvdata(pdev);
	struct devlink *devlink;

	devlink = priv_to_devlink(pf);
	devl_lock(devlink);

	/* If the VFs are assigned we cannot shut down SR-IOV without
	 * causing issues, so just leave the hardware available but
	 * disabled
	 */
	if (pci_vfs_assigned(pdev)) {
		dev_warn(&pdev->dev, "Disabling while VFs assigned - VFs will not be deallocated\n");
		devl_unlock(devlink);
		return -EPERM;
	}

	nfp_app_sriov_disable(pf->app);

	pf->num_vfs = 0;

	devl_unlock(devlink);

	pci_disable_sriov(pdev);
	dev_dbg(&pdev->dev, "Removed VFs.\n");
#endif
	return 0;
}

static int nfp_pcie_sriov_configure(struct pci_dev *pdev, int num_vfs)
{
	if (!pci_get_drvdata(pdev))
		return -ENOENT;

	if (num_vfs == 0)
		return nfp_pcie_sriov_disable(pdev);
	else
		return nfp_pcie_sriov_enable(pdev, num_vfs);
}

int nfp_flash_update_common(struct nfp_pf *pf, const struct firmware *fw,
			    struct netlink_ext_ack *extack)
{
	struct device *dev = &pf->pdev->dev;
	struct nfp_nsp *nsp;
	int err;

	nsp = nfp_nsp_open(pf->cpp);
	if (IS_ERR(nsp)) {
		err = PTR_ERR(nsp);
		if (extack)
			NL_SET_ERR_MSG_MOD(extack, "can't access NSP");
		else
			dev_err(dev, "Failed to access the NSP: %d\n", err);
		return err;
	}

	err = nfp_nsp_write_flash(nsp, fw);
	if (err < 0)
		goto exit_close_nsp;
	dev_info(dev, "Finished writing flash image\n");
	err = 0;

exit_close_nsp:
	nfp_nsp_close(nsp);
	return err;
}

static const struct firmware *
nfp_net_fw_request(struct pci_dev *pdev, struct nfp_pf *pf, const char *name)
{
	const struct firmware *fw = NULL;
	int err;

	err = request_firmware_direct(&fw, name, &pdev->dev);
	nfp_info(pf->cpp, "  %s: %s\n",
		 name, err ? "not found" : "found");
	if (err)
		return NULL;

	return fw;
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
	struct nfp_eth_table_port *port;
	const struct firmware *fw;
	const char *fw_model;
	char fw_name[256];
	const u8 *serial;
	u16 interface;
	int spc, i, j;

	nfp_info(pf->cpp, "Looking for firmware file in order of priority:\n");

	/* First try to find a firmware image specific for this device */
	interface = nfp_cpp_interface(pf->cpp);
	nfp_cpp_serial(pf->cpp, &serial);
	sprintf(fw_name, "netronome/serial-%pMF-%02x-%02x.nffw",
		serial, interface >> 8, interface & 0xff);
	fw = nfp_net_fw_request(pdev, pf, fw_name);
	if (fw)
		return fw;

	/* Then try the PCI name */
	sprintf(fw_name, "netronome/pci-%s.nffw", pci_name(pdev));
	fw = nfp_net_fw_request(pdev, pf, fw_name);
	if (fw)
		return fw;

	/* Finally try the card type and media */
	if (!pf->eth_tbl) {
		dev_err(&pdev->dev, "Error: can't identify media config\n");
		return NULL;
	}

	fw_model = nfp_hwinfo_lookup(pf->hwinfo, "nffw.partno");
	if (!fw_model)
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

	return nfp_net_fw_request(pdev, pf, fw_name);
}

static int
nfp_get_fw_policy_value(struct pci_dev *pdev, struct nfp_nsp *nsp,
			const char *key, const char *default_val, int max_val,
			int *value)
{
	char hwinfo[64];
	long hi_val;
	int err;

	snprintf(hwinfo, sizeof(hwinfo), key);
	err = nfp_nsp_hwinfo_lookup_optional(nsp, hwinfo, sizeof(hwinfo),
					     default_val);
	if (err)
		return err;

	err = kstrtol(hwinfo, 0, &hi_val);
	if (err || hi_val < 0 || hi_val > max_val) {
		dev_warn(&pdev->dev,
			 "Invalid value '%s' from '%s', ignoring\n",
			 hwinfo, key);
		err = kstrtol(default_val, 0, &hi_val);
	}

	*value = hi_val;
	return err;
}

/**
 * nfp_fw_load() - Load the firmware image
 * @pdev:       PCI Device structure
 * @pf:		NFP PF Device structure
 * @nsp:	NFP SP handle
 *
 * Return: -ERRNO, 0 for no firmware loaded, 1 for firmware loaded
 */
static int
nfp_fw_load(struct pci_dev *pdev, struct nfp_pf *pf, struct nfp_nsp *nsp)
{
	bool do_reset, fw_loaded = false;
	const struct firmware *fw = NULL;
	int err, reset, policy, ifcs = 0;
	char *token, *ptr;
	char hwinfo[64];
	u16 interface;

	snprintf(hwinfo, sizeof(hwinfo), "abi_drv_load_ifc");
	err = nfp_nsp_hwinfo_lookup_optional(nsp, hwinfo, sizeof(hwinfo),
					     NFP_NSP_DRV_LOAD_IFC_DEFAULT);
	if (err)
		return err;

	interface = nfp_cpp_interface(pf->cpp);
	ptr = hwinfo;
	while ((token = strsep(&ptr, ","))) {
		unsigned long interface_hi;

		err = kstrtoul(token, 0, &interface_hi);
		if (err) {
			dev_err(&pdev->dev,
				"Failed to parse interface '%s': %d\n",
				token, err);
			return err;
		}

		ifcs++;
		if (interface == interface_hi)
			break;
	}

	if (!token) {
		dev_info(&pdev->dev, "Firmware will be loaded by partner\n");
		return 0;
	}

	err = nfp_get_fw_policy_value(pdev, nsp, "abi_drv_reset",
				      NFP_NSP_DRV_RESET_DEFAULT,
				      NFP_NSP_DRV_RESET_NEVER, &reset);
	if (err)
		return err;

	err = nfp_get_fw_policy_value(pdev, nsp, "app_fw_from_flash",
				      NFP_NSP_APP_FW_LOAD_DEFAULT,
				      NFP_NSP_APP_FW_LOAD_PREF, &policy);
	if (err)
		return err;

	fw = nfp_net_fw_find(pdev, pf);
	do_reset = reset == NFP_NSP_DRV_RESET_ALWAYS ||
		   (fw && reset == NFP_NSP_DRV_RESET_DISK);

	if (do_reset) {
		dev_info(&pdev->dev, "Soft-resetting the NFP\n");
		err = nfp_nsp_device_soft_reset(nsp);
		if (err < 0) {
			dev_err(&pdev->dev,
				"Failed to soft reset the NFP: %d\n", err);
			goto exit_release_fw;
		}
	}

	if (fw && policy != NFP_NSP_APP_FW_LOAD_FLASH) {
		if (nfp_nsp_has_fw_loaded(nsp) && nfp_nsp_fw_loaded(nsp))
			goto exit_release_fw;

		err = nfp_nsp_load_fw(nsp, fw);
		if (err < 0) {
			dev_err(&pdev->dev, "FW loading failed: %d\n",
				err);
			goto exit_release_fw;
		}
		dev_info(&pdev->dev, "Finished loading FW image\n");
		fw_loaded = true;
	} else if (policy != NFP_NSP_APP_FW_LOAD_DISK &&
		   nfp_nsp_has_stored_fw_load(nsp)) {

		/* Don't propagate this error to stick with legacy driver
		 * behavior, failure will be detected later during init.
		 */
		if (!nfp_nsp_load_stored_fw(nsp))
			dev_info(&pdev->dev, "Finished loading stored FW image\n");

		/* Don't flag the fw_loaded in this case since other devices
		 * may reuse the firmware when configured this way
		 */
	} else {
		dev_warn(&pdev->dev, "Didn't load firmware, please update flash or reconfigure card\n");
	}

exit_release_fw:
	release_firmware(fw);

	/* We don't want to unload firmware when other devices may still be
	 * dependent on it, which could be the case if there are multiple
	 * devices that could load firmware.
	 */
	if (fw_loaded && ifcs == 1)
		pf->unload_fw_on_remove = true;

	return err < 0 ? err : fw_loaded;
}

static void
nfp_nsp_init_ports(struct pci_dev *pdev, struct nfp_pf *pf,
		   struct nfp_nsp *nsp)
{
	bool needs_reinit = false;
	int i;

	pf->eth_tbl = __nfp_eth_read_ports(pf->cpp, nsp);
	if (!pf->eth_tbl)
		return;

	if (!nfp_nsp_has_mac_reinit(nsp))
		return;

	for (i = 0; i < pf->eth_tbl->count; i++)
		needs_reinit |= pf->eth_tbl->ports[i].override_changed;
	if (!needs_reinit)
		return;

	kfree(pf->eth_tbl);
	if (nfp_nsp_mac_reinit(nsp))
		dev_warn(&pdev->dev, "MAC reinit failed\n");

	pf->eth_tbl = __nfp_eth_read_ports(pf->cpp, nsp);
}

static int nfp_nsp_init(struct pci_dev *pdev, struct nfp_pf *pf)
{
	struct nfp_nsp *nsp;
	int err;

	err = nfp_resource_wait(pf->cpp, NFP_RESOURCE_NSP, 30);
	if (err)
		return err;

	nsp = nfp_nsp_open(pf->cpp);
	if (IS_ERR(nsp)) {
		err = PTR_ERR(nsp);
		dev_err(&pdev->dev, "Failed to access the NSP: %d\n", err);
		return err;
	}

	err = nfp_nsp_wait(nsp);
	if (err < 0)
		goto exit_close_nsp;

	nfp_nsp_init_ports(pdev, pf, nsp);

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

static int nfp_pf_find_rtsyms(struct nfp_pf *pf)
{
	char pf_symbol[256];
	unsigned int pf_id;

	pf_id = nfp_cppcore_pcie_unit(pf->cpp);

	/* Optional per-PCI PF mailbox */
	snprintf(pf_symbol, sizeof(pf_symbol), NFP_MBOX_SYM_NAME, pf_id);
	pf->mbox = nfp_rtsym_lookup(pf->rtbl, pf_symbol);
	if (pf->mbox && nfp_rtsym_size(pf->mbox) < NFP_MBOX_SYM_MIN_SIZE) {
		nfp_err(pf->cpp, "PF mailbox symbol too small: %llu < %d\n",
			nfp_rtsym_size(pf->mbox), NFP_MBOX_SYM_MIN_SIZE);
		return -EINVAL;
	}

	return 0;
}

static int nfp_pci_probe(struct pci_dev *pdev,
			 const struct pci_device_id *pci_id)
{
	const struct nfp_dev_info *dev_info;
	struct devlink *devlink;
	struct nfp_pf *pf;
	int err;

	if ((pdev->vendor == PCI_VENDOR_ID_NETRONOME ||
	     pdev->vendor == PCI_VENDOR_ID_CORIGINE) &&
	    (pdev->device == PCI_DEVICE_ID_NFP3800_VF ||
	     pdev->device == PCI_DEVICE_ID_NFP6000_VF))
		dev_warn(&pdev->dev, "Binding NFP VF device to the NFP PF driver, the VF driver is called 'nfp_netvf'\n");

	dev_info = &nfp_dev_info[pci_id->driver_data];

	err = pci_enable_device(pdev);
	if (err < 0)
		return err;

	pci_set_master(pdev);

	err = dma_set_mask_and_coherent(&pdev->dev, dev_info->dma_mask);
	if (err)
		goto err_pci_disable;

	err = pci_request_regions(pdev, nfp_driver_name);
	if (err < 0) {
		dev_err(&pdev->dev, "Unable to reserve pci resources.\n");
		goto err_pci_disable;
	}

	devlink = devlink_alloc(&nfp_devlink_ops, sizeof(*pf), &pdev->dev);
	if (!devlink) {
		err = -ENOMEM;
		goto err_rel_regions;
	}
	pf = devlink_priv(devlink);
	INIT_LIST_HEAD(&pf->vnics);
	INIT_LIST_HEAD(&pf->ports);
	pci_set_drvdata(pdev, pf);
	pf->pdev = pdev;
	pf->dev_info = dev_info;

	pf->wq = alloc_workqueue("nfp-%s", 0, 2, pci_name(pdev));
	if (!pf->wq) {
		err = -ENOMEM;
		goto err_pci_priv_unset;
	}

	pf->cpp = nfp_cpp_from_nfp6000_pcie(pdev, dev_info);
	if (IS_ERR(pf->cpp)) {
		err = PTR_ERR(pf->cpp);
		goto err_disable_msix;
	}

	err = nfp_resource_table_init(pf->cpp);
	if (err)
		goto err_cpp_free;

	pf->hwinfo = nfp_hwinfo_read(pf->cpp);

	dev_info(&pdev->dev, "Assembly: %s%s%s-%s CPLD: %s\n",
		 nfp_hwinfo_lookup(pf->hwinfo, "assembly.vendor"),
		 nfp_hwinfo_lookup(pf->hwinfo, "assembly.partno"),
		 nfp_hwinfo_lookup(pf->hwinfo, "assembly.serial"),
		 nfp_hwinfo_lookup(pf->hwinfo, "assembly.revision"),
		 nfp_hwinfo_lookup(pf->hwinfo, "cpld.version"));

	err = nfp_pf_board_state_wait(pf);
	if (err)
		goto err_hwinfo_free;

	err = nfp_nsp_init(pdev, pf);
	if (err)
		goto err_hwinfo_free;

	pf->mip = nfp_mip_open(pf->cpp);
	pf->rtbl = __nfp_rtsym_table_read(pf->cpp, pf->mip);

	err = nfp_pf_find_rtsyms(pf);
	if (err)
		goto err_fw_unload;

	pf->dump_flag = NFP_DUMP_NSP_DIAG;
	pf->dumpspec = nfp_net_dump_load_dumpspec(pf->cpp, pf->rtbl);

	err = nfp_pcie_sriov_read_nfd_limit(pf);
	if (err)
		goto err_fw_unload;

	pf->num_vfs = pci_num_vf(pdev);
	if (pf->num_vfs > pf->limit_vfs) {
		dev_err(&pdev->dev,
			"Error: %d VFs already enabled, but loaded FW can only support %d\n",
			pf->num_vfs, pf->limit_vfs);
		err = -EINVAL;
		goto err_fw_unload;
	}

	err = nfp_net_pci_probe(pf);
	if (err)
		goto err_fw_unload;

	err = nfp_hwmon_register(pf);
	if (err) {
		dev_err(&pdev->dev, "Failed to register hwmon info\n");
		goto err_net_remove;
	}

	return 0;

err_net_remove:
	nfp_net_pci_remove(pf);
err_fw_unload:
	kfree(pf->rtbl);
	nfp_mip_close(pf->mip);
	if (pf->unload_fw_on_remove)
		nfp_fw_unload(pf);
	kfree(pf->eth_tbl);
	kfree(pf->nspi);
	vfree(pf->dumpspec);
err_hwinfo_free:
	kfree(pf->hwinfo);
err_cpp_free:
	nfp_cpp_free(pf->cpp);
err_disable_msix:
	destroy_workqueue(pf->wq);
err_pci_priv_unset:
	pci_set_drvdata(pdev, NULL);
	devlink_free(devlink);
err_rel_regions:
	pci_release_regions(pdev);
err_pci_disable:
	pci_disable_device(pdev);

	return err;
}

static void __nfp_pci_shutdown(struct pci_dev *pdev, bool unload_fw)
{
	struct nfp_pf *pf;

	pf = pci_get_drvdata(pdev);
	if (!pf)
		return;

	nfp_hwmon_unregister(pf);

	nfp_pcie_sriov_disable(pdev);

	nfp_net_pci_remove(pf);

	vfree(pf->dumpspec);
	kfree(pf->rtbl);
	nfp_mip_close(pf->mip);
	if (unload_fw && pf->unload_fw_on_remove)
		nfp_fw_unload(pf);

	destroy_workqueue(pf->wq);
	pci_set_drvdata(pdev, NULL);
	kfree(pf->hwinfo);
	nfp_cpp_free(pf->cpp);

	kfree(pf->eth_tbl);
	kfree(pf->nspi);
	devlink_free(priv_to_devlink(pf));
	pci_release_regions(pdev);
	pci_disable_device(pdev);
}

static void nfp_pci_remove(struct pci_dev *pdev)
{
	__nfp_pci_shutdown(pdev, true);
}

static void nfp_pci_shutdown(struct pci_dev *pdev)
{
	__nfp_pci_shutdown(pdev, false);
}

static struct pci_driver nfp_pci_driver = {
	.name			= nfp_driver_name,
	.id_table		= nfp_pci_device_ids,
	.probe			= nfp_pci_probe,
	.remove			= nfp_pci_remove,
	.shutdown		= nfp_pci_shutdown,
	.sriov_configure	= nfp_pcie_sriov_configure,
};

static int __init nfp_main_init(void)
{
	int err;

	pr_info("%s: NFP PCIe Driver, Copyright (C) 2014-2020 Netronome Systems\n",
		nfp_driver_name);
	pr_info("%s: NFP PCIe Driver, Copyright (C) 2021-2022 Corigine Inc.\n",
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

MODULE_FIRMWARE("netronome/nic_AMDA0058-0011_2x40.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0058-0012_2x40.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0081-0001_1x40.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0081-0001_4x10.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0096-0001_2x10.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0097-0001_2x40.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0097-0001_4x10_1x40.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0097-0001_8x10.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0099-0001_2x10.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0099-0001_2x25.nffw");
MODULE_FIRMWARE("netronome/nic_AMDA0099-0001_1x10_1x25.nffw");

MODULE_AUTHOR("Corigine, Inc. <oss-drivers@corigine.com>");
MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("The Network Flow Processor (NFP) driver.");
