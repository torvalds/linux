// SPDX-License-Identifier: GPL-2.0
/* Copyright(c) 2023 Intel Corporation. */

#include <net/devlink.h>
#include "i40e.h"
#include "i40e_devlink.h"

static void i40e_info_get_dsn(struct i40e_pf *pf, char *buf, size_t len)
{
	u8 dsn[8];

	put_unaligned_be64(pci_get_dsn(pf->pdev), dsn);

	snprintf(buf, len, "%8phD", dsn);
}

static void i40e_info_fw_mgmt(struct i40e_hw *hw, char *buf, size_t len)
{
	struct i40e_adminq_info *aq = &hw->aq;

	snprintf(buf, len, "%u.%u", aq->fw_maj_ver, aq->fw_min_ver);
}

static void i40e_info_fw_mgmt_build(struct i40e_hw *hw, char *buf, size_t len)
{
	struct i40e_adminq_info *aq = &hw->aq;

	snprintf(buf, len, "%05d", aq->fw_build);
}

static void i40e_info_fw_api(struct i40e_hw *hw, char *buf, size_t len)
{
	struct i40e_adminq_info *aq = &hw->aq;

	snprintf(buf, len, "%u.%u", aq->api_maj_ver, aq->api_min_ver);
}

static void i40e_info_pba(struct i40e_hw *hw, char *buf, size_t len)
{
	buf[0] = '\0';
	if (hw->pba_id)
		strscpy(buf, hw->pba_id, len);
}

enum i40e_devlink_version_type {
	I40E_DL_VERSION_FIXED,
	I40E_DL_VERSION_RUNNING,
};

static int i40e_devlink_info_put(struct devlink_info_req *req,
				 enum i40e_devlink_version_type type,
				 const char *key, const char *value)
{
	if (!strlen(value))
		return 0;

	switch (type) {
	case I40E_DL_VERSION_FIXED:
		return devlink_info_version_fixed_put(req, key, value);
	case I40E_DL_VERSION_RUNNING:
		return devlink_info_version_running_put(req, key, value);
	}
	return 0;
}

static int i40e_devlink_info_get(struct devlink *dl,
				 struct devlink_info_req *req,
				 struct netlink_ext_ack *extack)
{
	struct i40e_pf *pf = devlink_priv(dl);
	struct i40e_hw *hw = &pf->hw;
	char buf[32];
	int err;

	i40e_info_get_dsn(pf, buf, sizeof(buf));
	err = devlink_info_serial_number_put(req, buf);
	if (err)
		return err;

	i40e_info_fw_mgmt(hw, buf, sizeof(buf));
	err = i40e_devlink_info_put(req, I40E_DL_VERSION_RUNNING,
				    DEVLINK_INFO_VERSION_GENERIC_FW_MGMT, buf);
	if (err)
		return err;

	i40e_info_fw_mgmt_build(hw, buf, sizeof(buf));
	err = i40e_devlink_info_put(req, I40E_DL_VERSION_RUNNING,
				    "fw.mgmt.build", buf);
	if (err)
		return err;

	i40e_info_fw_api(hw, buf, sizeof(buf));
	err = i40e_devlink_info_put(req, I40E_DL_VERSION_RUNNING,
				    DEVLINK_INFO_VERSION_GENERIC_FW_MGMT_API,
				    buf);
	if (err)
		return err;

	i40e_info_nvm_ver(hw, buf, sizeof(buf));
	err = i40e_devlink_info_put(req, I40E_DL_VERSION_RUNNING,
				    "fw.psid.api", buf);
	if (err)
		return err;

	i40e_info_eetrack(hw, buf, sizeof(buf));
	err = i40e_devlink_info_put(req, I40E_DL_VERSION_RUNNING,
				    DEVLINK_INFO_VERSION_GENERIC_FW_BUNDLE_ID,
				    buf);
	if (err)
		return err;

	i40e_info_civd_ver(hw, buf, sizeof(buf));
	err = i40e_devlink_info_put(req, I40E_DL_VERSION_RUNNING,
				    DEVLINK_INFO_VERSION_GENERIC_FW_UNDI, buf);
	if (err)
		return err;

	i40e_info_pba(hw, buf, sizeof(buf));
	err = i40e_devlink_info_put(req, I40E_DL_VERSION_FIXED,
				    DEVLINK_INFO_VERSION_GENERIC_BOARD_ID, buf);

	return err;
}

static const struct devlink_ops i40e_devlink_ops = {
	.info_get = i40e_devlink_info_get,
};

/**
 * i40e_alloc_pf - Allocate devlink and return i40e_pf structure pointer
 * @dev: the device to allocate for
 *
 * Allocate a devlink instance for this device and return the private
 * area as the i40e_pf structure.
 **/
struct i40e_pf *i40e_alloc_pf(struct device *dev)
{
	struct devlink *devlink;

	devlink = devlink_alloc(&i40e_devlink_ops, sizeof(struct i40e_pf), dev);
	if (!devlink)
		return NULL;

	return devlink_priv(devlink);
}

/**
 * i40e_free_pf - Free i40e_pf structure and associated devlink
 * @pf: the PF structure
 *
 * Free i40e_pf structure and devlink allocated by devlink_alloc.
 **/
void i40e_free_pf(struct i40e_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);

	devlink_free(devlink);
}

/**
 * i40e_devlink_register - Register devlink interface for this PF
 * @pf: the PF to register the devlink for.
 *
 * Register the devlink instance associated with this physical function.
 **/
void i40e_devlink_register(struct i40e_pf *pf)
{
	devlink_register(priv_to_devlink(pf));
}

/**
 * i40e_devlink_unregister - Unregister devlink resources for this PF.
 * @pf: the PF structure to cleanup
 *
 * Releases resources used by devlink and cleans up associated memory.
 **/
void i40e_devlink_unregister(struct i40e_pf *pf)
{
	devlink_unregister(priv_to_devlink(pf));
}

/**
 * i40e_devlink_set_switch_id - Set unique switch id based on pci dsn
 * @pf: the PF to create a devlink port for
 * @ppid: struct with switch id information
 */
static void i40e_devlink_set_switch_id(struct i40e_pf *pf,
				       struct netdev_phys_item_id *ppid)
{
	u64 id = pci_get_dsn(pf->pdev);

	ppid->id_len = sizeof(id);
	put_unaligned_be64(id, &ppid->id);
}

/**
 * i40e_devlink_create_port - Create a devlink port for this PF
 * @pf: the PF to create a port for
 *
 * Create and register a devlink_port for this PF. Note that although each
 * physical function is connected to a separate devlink instance, the port
 * will still be numbered according to the physical function id.
 *
 * Return: zero on success or an error code on failure.
 **/
int i40e_devlink_create_port(struct i40e_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct devlink_port_attrs attrs = {};
	struct device *dev = &pf->pdev->dev;
	int err;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	attrs.phys.port_number = pf->hw.pf_id;
	i40e_devlink_set_switch_id(pf, &attrs.switch_id);
	devlink_port_attrs_set(&pf->devlink_port, &attrs);
	err = devlink_port_register(devlink, &pf->devlink_port, pf->hw.pf_id);
	if (err) {
		dev_err(dev, "devlink_port_register failed: %d\n", err);
		return err;
	}

	return 0;
}

/**
 * i40e_devlink_destroy_port - Destroy the devlink_port for this PF
 * @pf: the PF to cleanup
 *
 * Unregisters the devlink_port structure associated with this PF.
 **/
void i40e_devlink_destroy_port(struct i40e_pf *pf)
{
	devlink_port_unregister(&pf->devlink_port);
}
