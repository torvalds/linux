// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Intel Corporation. */

#include "ice.h"
#include "ice_lib.h"
#include "ice_devlink.h"
#include "ice_fw_update.h"

/* context for devlink info version reporting */
struct ice_info_ctx {
	char buf[128];
	struct ice_orom_info pending_orom;
	struct ice_nvm_info pending_nvm;
	struct ice_netlist_info pending_netlist;
	struct ice_hw_dev_caps dev_caps;
};

/* The following functions are used to format specific strings for various
 * devlink info versions. The ctx parameter is used to provide the storage
 * buffer, as well as any ancillary information calculated when the info
 * request was made.
 *
 * If a version does not exist, for example when attempting to get the
 * inactive version of flash when there is no pending update, the function
 * should leave the buffer in the ctx structure empty and return 0.
 */

static void ice_info_get_dsn(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	u8 dsn[8];

	/* Copy the DSN into an array in Big Endian format */
	put_unaligned_be64(pci_get_dsn(pf->pdev), dsn);

	snprintf(ctx->buf, sizeof(ctx->buf), "%8phD", dsn);
}

static int ice_info_pba(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_hw *hw = &pf->hw;
	enum ice_status status;

	status = ice_read_pba_string(hw, (u8 *)ctx->buf, sizeof(ctx->buf));
	if (status)
		/* We failed to locate the PBA, so just skip this entry */
		dev_dbg(ice_pf_to_dev(pf), "Failed to read Product Board Assembly string, status %s\n",
			ice_stat_str(status));

	return 0;
}

static int ice_info_fw_mgmt(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(ctx->buf, sizeof(ctx->buf), "%u.%u.%u", hw->fw_maj_ver, hw->fw_min_ver,
		 hw->fw_patch);

	return 0;
}

static int ice_info_fw_api(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(ctx->buf, sizeof(ctx->buf), "%u.%u.%u", hw->api_maj_ver,
		 hw->api_min_ver, hw->api_patch);

	return 0;
}

static int ice_info_fw_build(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", hw->fw_build);

	return 0;
}

static int ice_info_orom_ver(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_orom_info *orom = &pf->hw.flash.orom;

	snprintf(ctx->buf, sizeof(ctx->buf), "%u.%u.%u", orom->major, orom->build, orom->patch);

	return 0;
}

static int
ice_info_pending_orom_ver(struct ice_pf __always_unused *pf, struct ice_info_ctx *ctx)
{
	struct ice_orom_info *orom = &ctx->pending_orom;

	if (ctx->dev_caps.common_cap.nvm_update_pending_orom)
		snprintf(ctx->buf, sizeof(ctx->buf), "%u.%u.%u",
			 orom->major, orom->build, orom->patch);

	return 0;
}

static int ice_info_nvm_ver(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_nvm_info *nvm = &pf->hw.flash.nvm;

	snprintf(ctx->buf, sizeof(ctx->buf), "%x.%02x", nvm->major, nvm->minor);

	return 0;
}

static int
ice_info_pending_nvm_ver(struct ice_pf __always_unused *pf, struct ice_info_ctx *ctx)
{
	struct ice_nvm_info *nvm = &ctx->pending_nvm;

	if (ctx->dev_caps.common_cap.nvm_update_pending_nvm)
		snprintf(ctx->buf, sizeof(ctx->buf), "%x.%02x", nvm->major, nvm->minor);

	return 0;
}

static int ice_info_eetrack(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_nvm_info *nvm = &pf->hw.flash.nvm;

	snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", nvm->eetrack);

	return 0;
}

static int
ice_info_pending_eetrack(struct ice_pf __always_unused *pf, struct ice_info_ctx *ctx)
{
	struct ice_nvm_info *nvm = &ctx->pending_nvm;

	if (ctx->dev_caps.common_cap.nvm_update_pending_nvm)
		snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", nvm->eetrack);

	return 0;
}

static int ice_info_ddp_pkg_name(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(ctx->buf, sizeof(ctx->buf), "%s", hw->active_pkg_name);

	return 0;
}

static int ice_info_ddp_pkg_version(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_pkg_ver *pkg = &pf->hw.active_pkg_ver;

	snprintf(ctx->buf, sizeof(ctx->buf), "%u.%u.%u.%u", pkg->major, pkg->minor, pkg->update,
		 pkg->draft);

	return 0;
}

static int ice_info_ddp_pkg_bundle_id(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", pf->hw.active_track_id);

	return 0;
}

static int ice_info_netlist_ver(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_netlist_info *netlist = &pf->hw.flash.netlist;

	/* The netlist version fields are BCD formatted */
	snprintf(ctx->buf, sizeof(ctx->buf), "%x.%x.%x-%x.%x.%x", netlist->major, netlist->minor,
		 netlist->type >> 16, netlist->type & 0xFFFF, netlist->rev,
		 netlist->cust_ver);

	return 0;
}

static int ice_info_netlist_build(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_netlist_info *netlist = &pf->hw.flash.netlist;

	snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", netlist->hash);

	return 0;
}

static int
ice_info_pending_netlist_ver(struct ice_pf __always_unused *pf, struct ice_info_ctx *ctx)
{
	struct ice_netlist_info *netlist = &ctx->pending_netlist;

	/* The netlist version fields are BCD formatted */
	if (ctx->dev_caps.common_cap.nvm_update_pending_netlist)
		snprintf(ctx->buf, sizeof(ctx->buf), "%x.%x.%x-%x.%x.%x",
			 netlist->major, netlist->minor,
			 netlist->type >> 16, netlist->type & 0xFFFF, netlist->rev,
			 netlist->cust_ver);

	return 0;
}

static int
ice_info_pending_netlist_build(struct ice_pf __always_unused *pf, struct ice_info_ctx *ctx)
{
	struct ice_netlist_info *netlist = &ctx->pending_netlist;

	if (ctx->dev_caps.common_cap.nvm_update_pending_netlist)
		snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", netlist->hash);

	return 0;
}

#define fixed(key, getter) { ICE_VERSION_FIXED, key, getter, NULL }
#define running(key, getter) { ICE_VERSION_RUNNING, key, getter, NULL }
#define stored(key, getter, fallback) { ICE_VERSION_STORED, key, getter, fallback }

/* The combined() macro inserts both the running entry as well as a stored
 * entry. The running entry will always report the version from the active
 * handler. The stored entry will first try the pending handler, and fallback
 * to the active handler if the pending function does not report a version.
 * The pending handler should check the status of a pending update for the
 * relevant flash component. It should only fill in the buffer in the case
 * where a valid pending version is available. This ensures that the related
 * stored and running versions remain in sync, and that stored versions are
 * correctly reported as expected.
 */
#define combined(key, active, pending) \
	running(key, active), \
	stored(key, pending, active)

enum ice_version_type {
	ICE_VERSION_FIXED,
	ICE_VERSION_RUNNING,
	ICE_VERSION_STORED,
};

static const struct ice_devlink_version {
	enum ice_version_type type;
	const char *key;
	int (*getter)(struct ice_pf *pf, struct ice_info_ctx *ctx);
	int (*fallback)(struct ice_pf *pf, struct ice_info_ctx *ctx);
} ice_devlink_versions[] = {
	fixed(DEVLINK_INFO_VERSION_GENERIC_BOARD_ID, ice_info_pba),
	running(DEVLINK_INFO_VERSION_GENERIC_FW_MGMT, ice_info_fw_mgmt),
	running("fw.mgmt.api", ice_info_fw_api),
	running("fw.mgmt.build", ice_info_fw_build),
	combined(DEVLINK_INFO_VERSION_GENERIC_FW_UNDI, ice_info_orom_ver, ice_info_pending_orom_ver),
	combined("fw.psid.api", ice_info_nvm_ver, ice_info_pending_nvm_ver),
	combined(DEVLINK_INFO_VERSION_GENERIC_FW_BUNDLE_ID, ice_info_eetrack, ice_info_pending_eetrack),
	running("fw.app.name", ice_info_ddp_pkg_name),
	running(DEVLINK_INFO_VERSION_GENERIC_FW_APP, ice_info_ddp_pkg_version),
	running("fw.app.bundle_id", ice_info_ddp_pkg_bundle_id),
	combined("fw.netlist", ice_info_netlist_ver, ice_info_pending_netlist_ver),
	combined("fw.netlist.build", ice_info_netlist_build, ice_info_pending_netlist_build),
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
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	struct ice_info_ctx *ctx;
	enum ice_status status;
	size_t i;
	int err;

	err = ice_wait_for_reset(pf, 10 * HZ);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Device is busy resetting");
		return err;
	}

	ctx = kzalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	/* discover capabilities first */
	status = ice_discover_dev_caps(hw, &ctx->dev_caps);
	if (status) {
		dev_dbg(dev, "Failed to discover device capabilities, status %s aq_err %s\n",
			ice_stat_str(status), ice_aq_str(hw->adminq.sq_last_status));
		NL_SET_ERR_MSG_MOD(extack, "Unable to discover device capabilities");
		err = -EIO;
		goto out_free_ctx;
	}

	if (ctx->dev_caps.common_cap.nvm_update_pending_orom) {
		status = ice_get_inactive_orom_ver(hw, &ctx->pending_orom);
		if (status) {
			dev_dbg(dev, "Unable to read inactive Option ROM version data, status %s aq_err %s\n",
				ice_stat_str(status), ice_aq_str(hw->adminq.sq_last_status));

			/* disable display of pending Option ROM */
			ctx->dev_caps.common_cap.nvm_update_pending_orom = false;
		}
	}

	if (ctx->dev_caps.common_cap.nvm_update_pending_nvm) {
		status = ice_get_inactive_nvm_ver(hw, &ctx->pending_nvm);
		if (status) {
			dev_dbg(dev, "Unable to read inactive NVM version data, status %s aq_err %s\n",
				ice_stat_str(status), ice_aq_str(hw->adminq.sq_last_status));

			/* disable display of pending Option ROM */
			ctx->dev_caps.common_cap.nvm_update_pending_nvm = false;
		}
	}

	if (ctx->dev_caps.common_cap.nvm_update_pending_netlist) {
		status = ice_get_inactive_netlist_ver(hw, &ctx->pending_netlist);
		if (status) {
			dev_dbg(dev, "Unable to read inactive Netlist version data, status %s aq_err %s\n",
				ice_stat_str(status), ice_aq_str(hw->adminq.sq_last_status));

			/* disable display of pending Option ROM */
			ctx->dev_caps.common_cap.nvm_update_pending_netlist = false;
		}
	}

	err = devlink_info_driver_name_put(req, KBUILD_MODNAME);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Unable to set driver name");
		goto out_free_ctx;
	}

	ice_info_get_dsn(pf, ctx);

	err = devlink_info_serial_number_put(req, ctx->buf);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Unable to set serial number");
		goto out_free_ctx;
	}

	for (i = 0; i < ARRAY_SIZE(ice_devlink_versions); i++) {
		enum ice_version_type type = ice_devlink_versions[i].type;
		const char *key = ice_devlink_versions[i].key;

		memset(ctx->buf, 0, sizeof(ctx->buf));

		err = ice_devlink_versions[i].getter(pf, ctx);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Unable to obtain version info");
			goto out_free_ctx;
		}

		/* If the default getter doesn't report a version, use the
		 * fallback function. This is primarily useful in the case of
		 * "stored" versions that want to report the same value as the
		 * running version in the normal case of no pending update.
		 */
		if (ctx->buf[0] == '\0' && ice_devlink_versions[i].fallback) {
			err = ice_devlink_versions[i].fallback(pf, ctx);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Unable to obtain version info");
				goto out_free_ctx;
			}
		}

		/* Do not report missing versions */
		if (ctx->buf[0] == '\0')
			continue;

		switch (type) {
		case ICE_VERSION_FIXED:
			err = devlink_info_version_fixed_put(req, key, ctx->buf);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Unable to set fixed version");
				goto out_free_ctx;
			}
			break;
		case ICE_VERSION_RUNNING:
			err = devlink_info_version_running_put(req, key, ctx->buf);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Unable to set running version");
				goto out_free_ctx;
			}
			break;
		case ICE_VERSION_STORED:
			err = devlink_info_version_stored_put(req, key, ctx->buf);
			if (err) {
				NL_SET_ERR_MSG_MOD(extack, "Unable to set stored version");
				goto out_free_ctx;
			}
			break;
		}
	}

out_free_ctx:
	kfree(ctx);
	return err;
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
	struct ice_hw *hw = &pf->hw;
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

	devlink_flash_update_status_notify(devlink, "Preparing to flash", NULL, 0, 0);

	return ice_flash_pldm_image(pf, params->fw, preservation, extack);
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

	devlink = devlink_alloc(&ice_devlink_ops, sizeof(struct ice_pf), dev);
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

	err = devlink_register(devlink);
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
 * ice_devlink_create_pf_port - Create a devlink port for this PF
 * @pf: the PF to create a devlink port for
 *
 * Create and register a devlink_port for this PF.
 *
 * Return: zero on success or an error code on failure.
 */
int ice_devlink_create_pf_port(struct ice_pf *pf)
{
	struct devlink_port_attrs attrs = {};
	struct devlink_port *devlink_port;
	struct devlink *devlink;
	struct ice_vsi *vsi;
	struct device *dev;
	int err;

	dev = ice_pf_to_dev(pf);

	devlink_port = &pf->devlink_port;

	vsi = ice_get_main_vsi(pf);
	if (!vsi)
		return -EIO;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	attrs.phys.port_number = pf->hw.bus.func;
	devlink_port_attrs_set(devlink_port, &attrs);
	devlink = priv_to_devlink(pf);

	err = devlink_port_register(devlink, devlink_port, vsi->idx);
	if (err) {
		dev_err(dev, "Failed to create devlink port for PF %d, error %d\n",
			pf->hw.pf_id, err);
		return err;
	}

	return 0;
}

/**
 * ice_devlink_destroy_pf_port - Destroy the devlink_port for this PF
 * @pf: the PF to cleanup
 *
 * Unregisters the devlink_port structure associated with this PF.
 */
void ice_devlink_destroy_pf_port(struct ice_pf *pf)
{
	struct devlink_port *devlink_port;

	devlink_port = &pf->devlink_port;

	devlink_port_type_clear(devlink_port);
	devlink_port_unregister(devlink_port);
}

/**
 * ice_devlink_create_vf_port - Create a devlink port for this VF
 * @vf: the VF to create a port for
 *
 * Create and register a devlink_port for this VF.
 *
 * Return: zero on success or an error code on failure.
 */
int ice_devlink_create_vf_port(struct ice_vf *vf)
{
	struct devlink_port_attrs attrs = {};
	struct devlink_port *devlink_port;
	struct devlink *devlink;
	struct ice_vsi *vsi;
	struct device *dev;
	struct ice_pf *pf;
	int err;

	pf = vf->pf;
	dev = ice_pf_to_dev(pf);
	vsi = ice_get_vf_vsi(vf);
	devlink_port = &vf->devlink_port;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PCI_VF;
	attrs.pci_vf.pf = pf->hw.bus.func;
	attrs.pci_vf.vf = vf->vf_id;

	devlink_port_attrs_set(devlink_port, &attrs);
	devlink = priv_to_devlink(pf);

	err = devlink_port_register(devlink, devlink_port, vsi->idx);
	if (err) {
		dev_err(dev, "Failed to create devlink port for VF %d, error %d\n",
			vf->vf_id, err);
		return err;
	}

	return 0;
}

/**
 * ice_devlink_destroy_vf_port - Destroy the devlink_port for this VF
 * @vf: the VF to cleanup
 *
 * Unregisters the devlink_port structure associated with this VF.
 */
void ice_devlink_destroy_vf_port(struct ice_vf *vf)
{
	struct devlink_port *devlink_port;

	devlink_port = &vf->devlink_port;

	devlink_port_type_clear(devlink_port);
	devlink_port_unregister(devlink_port);
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

	nvm_size = hw->flash.flash_size;
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

	nvm_size = pf->hw.flash.flash_size;
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
