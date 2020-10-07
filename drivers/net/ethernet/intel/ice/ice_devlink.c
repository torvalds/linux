// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"
#include "ice_devlink.h"
#include "ice_fw_update.h"

static void ice_info_get_dsn(struct ice_pf *pf, char *buf, size_t len)
{
	u8 dsn[8];

	/* Copy the DSN into an array in Big Endian format */
	put_unaligned_be64(pci_get_dsn(pf->pdev), dsn);

	snprintf(buf, len, "%8phD", dsn);
}

static int ice_info_pba(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;

	status = ice_read_pba_string(hw, (u8 *)buf, len);
	if (status)
		return -EIO;

	return 0;
}

static int ice_info_fw_mgmt(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(buf, len, "%u.%u.%u", hw->fw_maj_ver, hw->fw_min_ver,
		 hw->fw_patch);

	return 0;
}

static int ice_info_fw_api(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(buf, len, "%u.%u", hw->api_maj_ver, hw->api_min_ver);

	return 0;
}

static int ice_info_fw_build(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(buf, len, "0x%08x", hw->fw_build);

	return 0;
}

static int ice_info_orom_ver(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_orom_info *orom = &pf->hw.nvm.orom;

	snprintf(buf, len, "%u.%u.%u", orom->major, orom->build, orom->patch);

	return 0;
}

static int ice_info_nvm_ver(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_nvm_info *nvm = &pf->hw.nvm;

	snprintf(buf, len, "%x.%02x", nvm->major_ver, nvm->minor_ver);

	return 0;
}

static int ice_info_eetrack(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_nvm_info *nvm = &pf->hw.nvm;

	snprintf(buf, len, "0x%08x", nvm->eetrack);

	return 0;
}

static int ice_info_ddp_pkg_name(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(buf, len, "%s", hw->active_pkg_name);

	return 0;
}

static int ice_info_ddp_pkg_version(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_pkg_ver *pkg = &pf->hw.active_pkg_ver;

	snprintf(buf, len, "%u.%u.%u.%u", pkg->major, pkg->minor, pkg->update,
		 pkg->draft);

	return 0;
}

static int ice_info_netlist_ver(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_netlist_ver_info *netlist = &pf->hw.netlist_ver;

	/* The netlist version fields are BCD formatted */
	snprintf(buf, len, "%x.%x.%x-%x.%x.%x", netlist->major, netlist->minor,
		 netlist->type >> 16, netlist->type & 0xFFFF, netlist->rev,
		 netlist->cust_ver);

	return 0;
}

static int ice_info_netlist_build(struct ice_pf *pf, char *buf, size_t len)
{
	struct ice_netlist_ver_info *netlist = &pf->hw.netlist_ver;

	snprintf(buf, len, "0x%08x", netlist->hash);

	return 0;
}

#define fixed(key, getter) { ICE_VERSION_FIXED, key, getter }
#define running(key, getter) { ICE_VERSION_RUNNING, key, getter }

enum ice_version_type {
	ICE_VERSION_FIXED,
	ICE_VERSION_RUNNING,
	ICE_VERSION_STORED,
};

static const struct ice_devlink_version {
	enum ice_version_type type;
	const char *key;
	int (*getter)(struct ice_pf *pf, char *buf, size_t len);
} ice_devlink_versions[] = {
	fixed(DEVLINK_INFO_VERSION_GENERIC_BOARD_ID, ice_info_pba),
	running(DEVLINK_INFO_VERSION_GENERIC_FW_MGMT, ice_info_fw_mgmt),
	running("fw.mgmt.api", ice_info_fw_api),
	running("fw.mgmt.build", ice_info_fw_build),
	running(DEVLINK_INFO_VERSION_GENERIC_FW_UNDI, ice_info_orom_ver),
	running("fw.psid.api", ice_info_nvm_ver),
	running(DEVLINK_INFO_VERSION_GENERIC_FW_BUNDLE_ID, ice_info_eetrack),
	running("fw.app.name", ice_info_ddp_pkg_name),
	running(DEVLINK_INFO_VERSION_GENERIC_FW_APP, ice_info_ddp_pkg_version),
	running("fw.netlist", ice_info_netlist_ver),
	running("fw.netlist.build", ice_info_netlist_build),
};

/**
 * ice_devlink_info_get - .info_get devlink handler
 * @devlink: devlink instance structure
 * @req: the devlink info request
 * @extack: extended netdev ack structure
 *
 * Callback for the devlink .info_get operation. Reports information about the
 * device.
 *
 * Return: zero on success or an error code on failure.
 */
static int ice_devlink_info_get(struct devlink *devlink,
				struct devlink_info_req *req,
				struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_priv(devlink);
	char buf[100];
	size_t i;
	int err;

	err = devlink_info_driver_name_put(req, KBUILD_MODNAME);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Unable to set driver name");
		return err;
	}

	ice_info_get_dsn(pf, buf, sizeof(buf));

	err = devlink_info_serial_number_put(req, buf);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Unable to set serial number");
		return err;
	}

	for (i = 0; i < ARRAY_SIZE(ice_devlink_versions); i++) {
		enum ice_version_type type = ice_devlink_versions[i].type;
		const char *key = ice_devlink_versions[i].key;

		err = ice_devlink_versions[i].getter(pf, buf, sizeof(buf));
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Unable to obtain version info");
			return err;
		}

		switch (type) {
		case ICE_VERSION_FIXED:
			err = devlink_info_version_fixed_put(req, key, buf);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Unable to set fixed version");
				return err;
			}
			break;
		case ICE_VERSION_RUNNING:
			err = devlink_info_version_running_put(req, key, buf);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Unable to set running version");
				return err;
			}
			break;
		case ICE_VERSION_STORED:
			err = devlink_info_version_stored_put(req, key, buf);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Unable to set stored version");
				return err;
			}
			break;
		}
	}

	return 0;
}

/**
 * ice_devlink_flash_update - Update firmware stored in flash on the device
 * @devlink: pointer to devlink associated with device to update
 * @params: flash update parameters
 * @extack: netlink extended ACK structure
 *
 * Perform a device flash update. The bulk of the update logic is contained
 * within the ice_flash_pldm_image function.
 *
 * Returns: zero on success, or an error code on failure.
 */
static int
ice_devlink_flash_update(struct devlink *devlink,
			 struct devlink_flash_update_params *params,
			 struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_priv(devlink);
	struct device *dev = &pf->pdev->dev;
	struct ice_hw *hw = &pf->hw;
	const struct firmware *fw;
	u8 preservation;
	int err;

	if (!params->overwrite_mask) {
		/* preserve all settings and identifiers */
		preservation = ICE_AQC_NVM_PRESERVE_ALL;
	} else if (params->overwrite_mask == DEVLINK_FLASH_OVERWRITE_SETTINGS) {
		/* overwrite settings, but preserve the vital device identifiers */
		preservation = ICE_AQC_NVM_PRESERVE_SELECTED;
	} else if (params->overwrite_mask == (DEVLINK_FLASH_OVERWRITE_SETTINGS |
					      DEVLINK_FLASH_OVERWRITE_IDENTIFIERS)) {
		/* overwrite both settings and identifiers, preserve nothing */
		preservation = ICE_AQC_NVM_NO_PRESERVATION;
	} else {
		NL_SET_ERR_MSG_MOD(extack, "Requested overwrite mask is not supported");
		return -EOPNOTSUPP;
	}

	if (!hw->dev_caps.common_cap.nvm_unified_update) {
		NL_SET_ERR_MSG_MOD(extack, "Current firmware does not support unified update");
		return -EOPNOTSUPP;
	}

	err = ice_check_for_pending_update(pf, NULL, extack);
	if (err)
		return err;

	err = request_firmware(&fw, params->file_name, dev);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Unable to read file from disk");
		return err;
	}

	devlink_flash_update_begin_notify(devlink);
	devlink_flash_update_status_notify(devlink, "Preparing to flash", NULL, 0, 0);
	err = ice_flash_pldm_image(pf, fw, preservation, extack);
	devlink_flash_update_end_notify(devlink);

	release_firmware(fw);

	return err;
}

static const struct devlink_ops ice_devlink_ops = {
	.supported_flash_update_params = DEVLINK_SUPPORT_FLASH_UPDATE_OVERWRITE_MASK,
	.info_get = ice_devlink_info_get,
	.flash_update = ice_devlink_flash_update,
};

static void ice_devlink_free(void *devlink_ptr)
{
	devlink_free((struct devlink *)devlink_ptr);
}

/**
 * ice_allocate_pf - Allocate devlink and return PF structure pointer
 * @dev: the device to allocate for
 *
 * Allocate a devlink instance for this device and return the private area as
 * the PF structure. The devlink memory is kept track of through devres by
 * adding an action to remove it when unwinding.
 */
struct ice_pf *ice_allocate_pf(struct device *dev)
{
	struct devlink *devlink;

	devlink = devlink_alloc(&ice_devlink_ops, sizeof(struct ice_pf));
	if (!devlink)
		return NULL;

	/* Add an action to teardown the devlink when unwinding the driver */
	if (devm_add_action(dev, ice_devlink_free, devlink)) {
		devlink_free(devlink);
		return NULL;
	}

	return devlink_priv(devlink);
}

/**
 * ice_devlink_register - Register devlink interface for this PF
 * @pf: the PF to register the devlink for.
 *
 * Register the devlink instance associated with this physical function.
 *
 * Return: zero on success or an error code on failure.
 */
int ice_devlink_register(struct ice_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct device *dev = ice_pf_to_dev(pf);
	int err;

	err = devlink_register(devlink, dev);
	if (err) {
		dev_err(dev, "devlink registration failed: %d\n", err);
		return err;
	}

	return 0;
}

/**
 * ice_devlink_unregister - Unregister devlink resources for this PF.
 * @pf: the PF structure to cleanup
 *
 * Releases resources used by devlink and cleans up associated memory.
 */
void ice_devlink_unregister(struct ice_pf *pf)
{
	devlink_unregister(priv_to_devlink(pf));
}

/**
 * ice_devlink_create_port - Create a devlink port for this PF
 * @pf: the PF to create a port for
 *
 * Create and register a devlink_port for this PF. Note that although each
 * physical function is connected to a separate devlink instance, the port
 * will still be numbered according to the physical function ID.
 *
 * Return: zero on success or an error code on failure.
 */
int ice_devlink_create_port(struct ice_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct ice_vsi *vsi = ice_get_main_vsi(pf);
	struct device *dev = ice_pf_to_dev(pf);
	struct devlink_port_attrs attrs = {};
	int err;

	if (!vsi) {
		dev_err(dev, "%s: unable to find main VSI\n", __func__);
		return -EIO;
	}

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	attrs.phys.port_number = pf->hw.pf_id;
	devlink_port_attrs_set(&pf->devlink_port, &attrs);
	err = devlink_port_register(devlink, &pf->devlink_port, pf->hw.pf_id);
	if (err) {
		dev_err(dev, "devlink_port_register failed: %d\n", err);
		return err;
	}

	return 0;
}

/**
 * ice_devlink_destroy_port - Destroy the devlink_port for this PF
 * @pf: the PF to cleanup
 *
 * Unregisters the devlink_port structure associated with this PF.
 */
void ice_devlink_destroy_port(struct ice_pf *pf)
{
	devlink_port_type_clear(&pf->devlink_port);
	devlink_port_unregister(&pf->devlink_port);
}

/**
 * ice_devlink_nvm_snapshot - Capture a snapshot of the Shadow RAM contents
 * @devlink: the devlink instance
 * @ops: the devlink region being snapshotted
 * @extack: extended ACK response structure
 * @data: on exit points to snapshot data buffer
 *
 * This function is called in response to the DEVLINK_CMD_REGION_TRIGGER for
 * the shadow-ram devlink region. It captures a snapshot of the shadow ram
 * contents. This snapshot can later be viewed via the devlink-region
 * interface.
 *
 * @returns zero on success, and updates the data pointer. Returns a non-zero
 * error code on failure.
 */
static int ice_devlink_nvm_snapshot(struct devlink *devlink,
				    const struct devlink_region_ops *ops,
				    struct netlink_ext_ack *extack, u8 **data)
{
	struct ice_pf *pf = devlink_priv(devlink);
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	void *nvm_data;
	u32 nvm_size;

	nvm_size = hw->nvm.flash_size;
	nvm_data = vzalloc(nvm_size);
	if (!nvm_data)
		return -ENOMEM;

	status = ice_acquire_nvm(hw, ICE_RES_READ);
	if (status) {
		dev_dbg(dev, "ice_acquire_nvm failed, err %d aq_err %d\n",
			status, hw->adminq.sq_last_status);
		NL_SET_ERR_MSG_MOD(extack, "Failed to acquire NVM semaphore");
		vfree(nvm_data);
		return -EIO;
	}

	status = ice_read_flat_nvm(hw, 0, &nvm_size, nvm_data, false);
	if (status) {
		dev_dbg(dev, "ice_read_flat_nvm failed after reading %u bytes, err %d aq_err %d\n",
			nvm_size, status, hw->adminq.sq_last_status);
		NL_SET_ERR_MSG_MOD(extack, "Failed to read NVM contents");
		ice_release_nvm(hw);
		vfree(nvm_data);
		return -EIO;
	}

	ice_release_nvm(hw);

	*data = nvm_data;

	return 0;
}

/**
 * ice_devlink_devcaps_snapshot - Capture snapshot of device capabilities
 * @devlink: the devlink instance
 * @ops: the devlink region being snapshotted
 * @extack: extended ACK response structure
 * @data: on exit points to snapshot data buffer
 *
 * This function is called in response to the DEVLINK_CMD_REGION_TRIGGER for
 * the device-caps devlink region. It captures a snapshot of the device
 * capabilities reported by firmware.
 *
 * @returns zero on success, and updates the data pointer. Returns a non-zero
 * error code on failure.
 */
static int
ice_devlink_devcaps_snapshot(struct devlink *devlink,
			     const struct devlink_region_ops *ops,
			     struct netlink_ext_ack *extack, u8 **data)
{
	struct ice_pf *pf = devlink_priv(devlink);
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;
	void *devcaps;

	devcaps = vzalloc(ICE_AQ_MAX_BUF_LEN);
	if (!devcaps)
		return -ENOMEM;

	status = ice_aq_list_caps(hw, devcaps, ICE_AQ_MAX_BUF_LEN, NULL,
				  ice_aqc_opc_list_dev_caps, NULL);
	if (status) {
		dev_dbg(dev, "ice_aq_list_caps: failed to read device capabilities, err %d aq_err %d\n",
			status, hw->adminq.sq_last_status);
		NL_SET_ERR_MSG_MOD(extack, "Failed to read device capabilities");
		vfree(devcaps);
		return -EIO;
	}

	*data = (u8 *)devcaps;

	return 0;
}

static const struct devlink_region_ops ice_nvm_region_ops = {
	.name = "nvm-flash",
	.destructor = vfree,
	.snapshot = ice_devlink_nvm_snapshot,
};

static const struct devlink_region_ops ice_devcaps_region_ops = {
	.name = "device-caps",
	.destructor = vfree,
	.snapshot = ice_devlink_devcaps_snapshot,
};

/**
 * ice_devlink_init_regions - Initialize devlink regions
 * @pf: the PF device structure
 *
 * Create devlink regions used to enable access to dump the contents of the
 * flash memory on the device.
 */
void ice_devlink_init_regions(struct ice_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct device *dev = ice_pf_to_dev(pf);
	u64 nvm_size;

	nvm_size = pf->hw.nvm.flash_size;
	pf->nvm_region = devlink_region_create(devlink, &ice_nvm_region_ops, 1,
					       nvm_size);
	if (IS_ERR(pf->nvm_region)) {
		dev_err(dev, "failed to create NVM devlink region, err %ld\n",
			PTR_ERR(pf->nvm_region));
		pf->nvm_region = NULL;
	}

	pf->devcaps_region = devlink_region_create(devlink,
						   &ice_devcaps_region_ops, 10,
						   ICE_AQ_MAX_BUF_LEN);
	if (IS_ERR(pf->devcaps_region)) {
		dev_err(dev, "failed to create device-caps devlink region, err %ld\n",
			PTR_ERR(pf->devcaps_region));
		pf->devcaps_region = NULL;
	}
}

/**
 * ice_devlink_destroy_regions - Destroy devlink regions
 * @pf: the PF device structure
 *
 * Remove previously created regions for this PF.
 */
void ice_devlink_destroy_regions(struct ice_pf *pf)
{
	if (pf->nvm_region)
		devlink_region_destroy(pf->nvm_region);
	if (pf->devcaps_region)
		devlink_region_destroy(pf->devcaps_region);
}
