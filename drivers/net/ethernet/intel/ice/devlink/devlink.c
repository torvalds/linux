// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2020, Intel Corporation. */

#include <linux/vmalloc.h>

#include "ice.h"
#include "ice_lib.h"
#include "devlink.h"
#include "port.h"
#include "ice_eswitch.h"
#include "ice_fw_update.h"
#include "ice_dcb_lib.h"
#include "ice_sf_eth.h"

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
 * should leave the buffer in the ctx structure empty.
 */

static void ice_info_get_dsn(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	u8 dsn[8];

	/* Copy the DSN into an array in Big Endian format */
	put_unaligned_be64(pci_get_dsn(pf->pdev), dsn);

	snprintf(ctx->buf, sizeof(ctx->buf), "%8phD", dsn);
}

static void ice_info_pba(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_hw *hw = &pf->hw;
	int status;

	status = ice_read_pba_string(hw, (u8 *)ctx->buf, sizeof(ctx->buf));
	if (status)
		/* We failed to locate the PBA, so just skip this entry */
		dev_dbg(ice_pf_to_dev(pf), "Failed to read Product Board Assembly string, status %d\n",
			status);
}

static void ice_info_fw_mgmt(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(ctx->buf, sizeof(ctx->buf), "%u.%u.%u",
		 hw->fw_maj_ver, hw->fw_min_ver, hw->fw_patch);
}

static void ice_info_fw_api(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(ctx->buf, sizeof(ctx->buf), "%u.%u.%u", hw->api_maj_ver,
		 hw->api_min_ver, hw->api_patch);
}

static void ice_info_fw_build(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", hw->fw_build);
}

static void ice_info_orom_ver(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_orom_info *orom = &pf->hw.flash.orom;

	snprintf(ctx->buf, sizeof(ctx->buf), "%u.%u.%u",
		 orom->major, orom->build, orom->patch);
}

static void
ice_info_pending_orom_ver(struct ice_pf __always_unused *pf,
			  struct ice_info_ctx *ctx)
{
	struct ice_orom_info *orom = &ctx->pending_orom;

	if (ctx->dev_caps.common_cap.nvm_update_pending_orom)
		snprintf(ctx->buf, sizeof(ctx->buf), "%u.%u.%u",
			 orom->major, orom->build, orom->patch);
}

static void ice_info_nvm_ver(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_nvm_info *nvm = &pf->hw.flash.nvm;

	snprintf(ctx->buf, sizeof(ctx->buf), "%x.%02x", nvm->major, nvm->minor);
}

static void
ice_info_pending_nvm_ver(struct ice_pf __always_unused *pf,
			 struct ice_info_ctx *ctx)
{
	struct ice_nvm_info *nvm = &ctx->pending_nvm;

	if (ctx->dev_caps.common_cap.nvm_update_pending_nvm)
		snprintf(ctx->buf, sizeof(ctx->buf), "%x.%02x",
			 nvm->major, nvm->minor);
}

static void ice_info_eetrack(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_nvm_info *nvm = &pf->hw.flash.nvm;

	snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", nvm->eetrack);
}

static void
ice_info_pending_eetrack(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_nvm_info *nvm = &ctx->pending_nvm;

	if (ctx->dev_caps.common_cap.nvm_update_pending_nvm)
		snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", nvm->eetrack);
}

static void ice_info_ddp_pkg_name(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_hw *hw = &pf->hw;

	snprintf(ctx->buf, sizeof(ctx->buf), "%s", hw->active_pkg_name);
}

static void
ice_info_ddp_pkg_version(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_pkg_ver *pkg = &pf->hw.active_pkg_ver;

	snprintf(ctx->buf, sizeof(ctx->buf), "%u.%u.%u.%u",
		 pkg->major, pkg->minor, pkg->update, pkg->draft);
}

static void
ice_info_ddp_pkg_bundle_id(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", pf->hw.active_track_id);
}

static void ice_info_netlist_ver(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_netlist_info *netlist = &pf->hw.flash.netlist;

	/* The netlist version fields are BCD formatted */
	snprintf(ctx->buf, sizeof(ctx->buf), "%x.%x.%x-%x.%x.%x",
		 netlist->major, netlist->minor,
		 netlist->type >> 16, netlist->type & 0xFFFF,
		 netlist->rev, netlist->cust_ver);
}

static void ice_info_netlist_build(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	struct ice_netlist_info *netlist = &pf->hw.flash.netlist;

	snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", netlist->hash);
}

static void
ice_info_pending_netlist_ver(struct ice_pf __always_unused *pf,
			     struct ice_info_ctx *ctx)
{
	struct ice_netlist_info *netlist = &ctx->pending_netlist;

	/* The netlist version fields are BCD formatted */
	if (ctx->dev_caps.common_cap.nvm_update_pending_netlist)
		snprintf(ctx->buf, sizeof(ctx->buf), "%x.%x.%x-%x.%x.%x",
			 netlist->major, netlist->minor,
			 netlist->type >> 16, netlist->type & 0xFFFF,
			 netlist->rev, netlist->cust_ver);
}

static void
ice_info_pending_netlist_build(struct ice_pf __always_unused *pf,
			       struct ice_info_ctx *ctx)
{
	struct ice_netlist_info *netlist = &ctx->pending_netlist;

	if (ctx->dev_caps.common_cap.nvm_update_pending_netlist)
		snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", netlist->hash);
}

static void ice_info_cgu_fw_build(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	u32 id, cfg_ver, fw_ver;

	if (!ice_is_feature_supported(pf, ICE_F_CGU))
		return;
	if (ice_aq_get_cgu_info(&pf->hw, &id, &cfg_ver, &fw_ver))
		return;
	snprintf(ctx->buf, sizeof(ctx->buf), "%u.%u.%u", id, cfg_ver, fw_ver);
}

static void ice_info_cgu_id(struct ice_pf *pf, struct ice_info_ctx *ctx)
{
	if (!ice_is_feature_supported(pf, ICE_F_CGU))
		return;
	snprintf(ctx->buf, sizeof(ctx->buf), "%u", pf->hw.cgu_part_number);
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
	void (*getter)(struct ice_pf *pf, struct ice_info_ctx *ctx);
	void (*fallback)(struct ice_pf *pf, struct ice_info_ctx *ctx);
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
	fixed("cgu.id", ice_info_cgu_id),
	running("fw.cgu", ice_info_cgu_fw_build),
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
	err = ice_discover_dev_caps(hw, &ctx->dev_caps);
	if (err) {
		dev_dbg(dev, "Failed to discover device capabilities, status %d aq_err %s\n",
			err, ice_aq_str(hw->adminq.sq_last_status));
		NL_SET_ERR_MSG_MOD(extack, "Unable to discover device capabilities");
		goto out_free_ctx;
	}

	if (ctx->dev_caps.common_cap.nvm_update_pending_orom) {
		err = ice_get_inactive_orom_ver(hw, &ctx->pending_orom);
		if (err) {
			dev_dbg(dev, "Unable to read inactive Option ROM version data, status %d aq_err %s\n",
				err, ice_aq_str(hw->adminq.sq_last_status));

			/* disable display of pending Option ROM */
			ctx->dev_caps.common_cap.nvm_update_pending_orom = false;
		}
	}

	if (ctx->dev_caps.common_cap.nvm_update_pending_nvm) {
		err = ice_get_inactive_nvm_ver(hw, &ctx->pending_nvm);
		if (err) {
			dev_dbg(dev, "Unable to read inactive NVM version data, status %d aq_err %s\n",
				err, ice_aq_str(hw->adminq.sq_last_status));

			/* disable display of pending Option ROM */
			ctx->dev_caps.common_cap.nvm_update_pending_nvm = false;
		}
	}

	if (ctx->dev_caps.common_cap.nvm_update_pending_netlist) {
		err = ice_get_inactive_netlist_ver(hw, &ctx->pending_netlist);
		if (err) {
			dev_dbg(dev, "Unable to read inactive Netlist version data, status %d aq_err %s\n",
				err, ice_aq_str(hw->adminq.sq_last_status));

			/* disable display of pending Option ROM */
			ctx->dev_caps.common_cap.nvm_update_pending_netlist = false;
		}
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

		ice_devlink_versions[i].getter(pf, ctx);

		/* If the default getter doesn't report a version, use the
		 * fallback function. This is primarily useful in the case of
		 * "stored" versions that want to report the same value as the
		 * running version in the normal case of no pending update.
		 */
		if (ctx->buf[0] == '\0' && ice_devlink_versions[i].fallback)
			ice_devlink_versions[i].fallback(pf, ctx);

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
 * ice_devlink_reload_empr_start - Start EMP reset to activate new firmware
 * @pf: pointer to the pf instance
 * @extack: netlink extended ACK structure
 *
 * Allow user to activate new Embedded Management Processor firmware by
 * issuing device specific EMP reset. Called in response to
 * a DEVLINK_CMD_RELOAD with the DEVLINK_RELOAD_ACTION_FW_ACTIVATE.
 *
 * Note that teardown and rebuild of the driver state happens automatically as
 * part of an interrupt and watchdog task. This is because all physical
 * functions on the device must be able to reset when an EMP reset occurs from
 * any source.
 */
static int
ice_devlink_reload_empr_start(struct ice_pf *pf,
			      struct netlink_ext_ack *extack)
{
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	u8 pending;
	int err;

	err = ice_get_pending_updates(pf, &pending, extack);
	if (err)
		return err;

	/* pending is a bitmask of which flash banks have a pending update,
	 * including the main NVM bank, the Option ROM bank, and the netlist
	 * bank. If any of these bits are set, then there is a pending update
	 * waiting to be activated.
	 */
	if (!pending) {
		NL_SET_ERR_MSG_MOD(extack, "No pending firmware update");
		return -ECANCELED;
	}

	if (pf->fw_emp_reset_disabled) {
		NL_SET_ERR_MSG_MOD(extack, "EMP reset is not available. To activate firmware, a reboot or power cycle is needed");
		return -ECANCELED;
	}

	dev_dbg(dev, "Issuing device EMP reset to activate firmware\n");

	err = ice_aq_nvm_update_empr(hw);
	if (err) {
		dev_err(dev, "Failed to trigger EMP device reset to reload firmware, err %d aq_err %s\n",
			err, ice_aq_str(hw->adminq.sq_last_status));
		NL_SET_ERR_MSG_MOD(extack, "Failed to trigger EMP device reset to reload firmware");
		return err;
	}

	return 0;
}

/**
 * ice_devlink_reinit_down - unload given PF
 * @pf: pointer to the PF struct
 */
static void ice_devlink_reinit_down(struct ice_pf *pf)
{
	/* No need to take devl_lock, it's already taken by devlink API */
	ice_unload(pf);
	rtnl_lock();
	ice_vsi_decfg(ice_get_main_vsi(pf));
	rtnl_unlock();
	ice_deinit_dev(pf);
}

/**
 * ice_devlink_reload_down - prepare for reload
 * @devlink: pointer to the devlink instance to reload
 * @netns_change: if true, the network namespace is changing
 * @action: the action to perform
 * @limit: limits on what reload should do, such as not resetting
 * @extack: netlink extended ACK structure
 */
static int
ice_devlink_reload_down(struct devlink *devlink, bool netns_change,
			enum devlink_reload_action action,
			enum devlink_reload_limit limit,
			struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_priv(devlink);

	switch (action) {
	case DEVLINK_RELOAD_ACTION_DRIVER_REINIT:
		if (ice_is_eswitch_mode_switchdev(pf)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Go to legacy mode before doing reinit");
			return -EOPNOTSUPP;
		}
		if (ice_is_adq_active(pf)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Turn off ADQ before doing reinit");
			return -EOPNOTSUPP;
		}
		if (ice_has_vfs(pf)) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Remove all VFs before doing reinit");
			return -EOPNOTSUPP;
		}
		ice_devlink_reinit_down(pf);
		return 0;
	case DEVLINK_RELOAD_ACTION_FW_ACTIVATE:
		return ice_devlink_reload_empr_start(pf, extack);
	default:
		WARN_ON(1);
		return -EOPNOTSUPP;
	}
}

/**
 * ice_devlink_reload_empr_finish - Wait for EMP reset to finish
 * @pf: pointer to the pf instance
 * @extack: netlink extended ACK structure
 *
 * Wait for driver to finish rebuilding after EMP reset is completed. This
 * includes time to wait for both the actual device reset as well as the time
 * for the driver's rebuild to complete.
 */
static int
ice_devlink_reload_empr_finish(struct ice_pf *pf,
			       struct netlink_ext_ack *extack)
{
	int err;

	err = ice_wait_for_reset(pf, 60 * HZ);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Device still resetting after 1 minute");
		return err;
	}

	return 0;
}

/**
 * ice_get_tx_topo_user_sel - Read user's choice from flash
 * @pf: pointer to pf structure
 * @layers: value read from flash will be saved here
 *
 * Reads user's preference for Tx Scheduler Topology Tree from PFA TLV.
 *
 * Return: zero when read was successful, negative values otherwise.
 */
static int ice_get_tx_topo_user_sel(struct ice_pf *pf, uint8_t *layers)
{
	struct ice_aqc_nvm_tx_topo_user_sel usr_sel = {};
	struct ice_hw *hw = &pf->hw;
	int err;

	err = ice_acquire_nvm(hw, ICE_RES_READ);
	if (err)
		return err;

	err = ice_aq_read_nvm(hw, ICE_AQC_NVM_TX_TOPO_MOD_ID, 0,
			      sizeof(usr_sel), &usr_sel, true, true, NULL);
	if (err)
		goto exit_release_res;

	if (usr_sel.data & ICE_AQC_NVM_TX_TOPO_USER_SEL)
		*layers = ICE_SCHED_5_LAYERS;
	else
		*layers = ICE_SCHED_9_LAYERS;

exit_release_res:
	ice_release_nvm(hw);

	return err;
}

/**
 * ice_update_tx_topo_user_sel - Save user's preference in flash
 * @pf: pointer to pf structure
 * @layers: value to be saved in flash
 *
 * Variable "layers" defines user's preference about number of layers in Tx
 * Scheduler Topology Tree. This choice should be stored in PFA TLV field
 * and be picked up by driver, next time during init.
 *
 * Return: zero when save was successful, negative values otherwise.
 */
static int ice_update_tx_topo_user_sel(struct ice_pf *pf, int layers)
{
	struct ice_aqc_nvm_tx_topo_user_sel usr_sel = {};
	struct ice_hw *hw = &pf->hw;
	int err;

	err = ice_acquire_nvm(hw, ICE_RES_WRITE);
	if (err)
		return err;

	err = ice_aq_read_nvm(hw, ICE_AQC_NVM_TX_TOPO_MOD_ID, 0,
			      sizeof(usr_sel), &usr_sel, true, true, NULL);
	if (err)
		goto exit_release_res;

	if (layers == ICE_SCHED_5_LAYERS)
		usr_sel.data |= ICE_AQC_NVM_TX_TOPO_USER_SEL;
	else
		usr_sel.data &= ~ICE_AQC_NVM_TX_TOPO_USER_SEL;

	err = ice_write_one_nvm_block(pf, ICE_AQC_NVM_TX_TOPO_MOD_ID, 2,
				      sizeof(usr_sel.data), &usr_sel.data,
				      true, NULL, NULL);
exit_release_res:
	ice_release_nvm(hw);

	return err;
}

/**
 * ice_devlink_tx_sched_layers_get - Get tx_scheduling_layers parameter
 * @devlink: pointer to the devlink instance
 * @id: the parameter ID to set
 * @ctx: context to store the parameter value
 *
 * Return: zero on success and negative value on failure.
 */
static int ice_devlink_tx_sched_layers_get(struct devlink *devlink, u32 id,
					   struct devlink_param_gset_ctx *ctx)
{
	struct ice_pf *pf = devlink_priv(devlink);
	int err;

	err = ice_get_tx_topo_user_sel(pf, &ctx->val.vu8);
	if (err)
		return err;

	return 0;
}

/**
 * ice_devlink_tx_sched_layers_set - Set tx_scheduling_layers parameter
 * @devlink: pointer to the devlink instance
 * @id: the parameter ID to set
 * @ctx: context to get the parameter value
 * @extack: netlink extended ACK structure
 *
 * Return: zero on success and negative value on failure.
 */
static int ice_devlink_tx_sched_layers_set(struct devlink *devlink, u32 id,
					   struct devlink_param_gset_ctx *ctx,
					   struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_priv(devlink);
	int err;

	err = ice_update_tx_topo_user_sel(pf, ctx->val.vu8);
	if (err)
		return err;

	NL_SET_ERR_MSG_MOD(extack,
			   "Tx scheduling layers have been changed on this device. You must do the PCI slot powercycle for the change to take effect.");

	return 0;
}

/**
 * ice_devlink_tx_sched_layers_validate - Validate passed tx_scheduling_layers
 *                                        parameter value
 * @devlink: unused pointer to devlink instance
 * @id: the parameter ID to validate
 * @val: value to validate
 * @extack: netlink extended ACK structure
 *
 * Supported values are:
 * - 5 - five layers Tx Scheduler Topology Tree
 * - 9 - nine layers Tx Scheduler Topology Tree
 *
 * Return: zero when passed parameter value is supported. Negative value on
 * error.
 */
static int ice_devlink_tx_sched_layers_validate(struct devlink *devlink, u32 id,
						union devlink_param_value val,
						struct netlink_ext_ack *extack)
{
	if (val.vu8 != ICE_SCHED_5_LAYERS && val.vu8 != ICE_SCHED_9_LAYERS) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Wrong number of tx scheduler layers provided.");
		return -EINVAL;
	}

	return 0;
}

/**
 * ice_tear_down_devlink_rate_tree - removes devlink-rate exported tree
 * @pf: pf struct
 *
 * This function tears down tree exported during VF's creation.
 */
void ice_tear_down_devlink_rate_tree(struct ice_pf *pf)
{
	struct devlink *devlink;
	struct ice_vf *vf;
	unsigned int bkt;

	devlink = priv_to_devlink(pf);

	devl_lock(devlink);
	mutex_lock(&pf->vfs.table_lock);
	ice_for_each_vf(pf, bkt, vf) {
		if (vf->devlink_port.devlink_rate)
			devl_rate_leaf_destroy(&vf->devlink_port);
	}
	mutex_unlock(&pf->vfs.table_lock);

	devl_rate_nodes_destroy(devlink);
	devl_unlock(devlink);
}

/**
 * ice_enable_custom_tx - try to enable custom Tx feature
 * @pf: pf struct
 *
 * This function tries to enable custom Tx feature,
 * it's not possible to enable it, if DCB or ADQ is active.
 */
static bool ice_enable_custom_tx(struct ice_pf *pf)
{
	struct ice_port_info *pi = ice_get_main_vsi(pf)->port_info;
	struct device *dev = ice_pf_to_dev(pf);

	if (pi->is_custom_tx_enabled)
		/* already enabled, return true */
		return true;

	if (ice_is_adq_active(pf)) {
		dev_err(dev, "ADQ active, can't modify Tx scheduler tree\n");
		return false;
	}

	if (ice_is_dcb_active(pf)) {
		dev_err(dev, "DCB active, can't modify Tx scheduler tree\n");
		return false;
	}

	pi->is_custom_tx_enabled = true;

	return true;
}

/**
 * ice_traverse_tx_tree - traverse Tx scheduler tree
 * @devlink: devlink struct
 * @node: current node, used for recursion
 * @tc_node: tc_node struct, that is treated as a root
 * @pf: pf struct
 *
 * This function traverses Tx scheduler tree and exports
 * entire structure to the devlink-rate.
 */
static void ice_traverse_tx_tree(struct devlink *devlink, struct ice_sched_node *node,
				 struct ice_sched_node *tc_node, struct ice_pf *pf)
{
	struct devlink_rate *rate_node = NULL;
	struct ice_dynamic_port *sf;
	struct ice_vf *vf;
	int i;

	if (node->rate_node)
		/* already added, skip to the next */
		goto traverse_children;

	if (node->parent == tc_node) {
		/* create root node */
		rate_node = devl_rate_node_create(devlink, node, node->name, NULL);
	} else if (node->vsi_handle &&
		   pf->vsi[node->vsi_handle]->type == ICE_VSI_VF &&
		   pf->vsi[node->vsi_handle]->vf) {
		vf = pf->vsi[node->vsi_handle]->vf;
		if (!vf->devlink_port.devlink_rate)
			/* leaf nodes doesn't have children
			 * so we don't set rate_node
			 */
			devl_rate_leaf_create(&vf->devlink_port, node,
					      node->parent->rate_node);
	} else if (node->vsi_handle &&
		   pf->vsi[node->vsi_handle]->type == ICE_VSI_SF &&
		   pf->vsi[node->vsi_handle]->sf) {
		sf = pf->vsi[node->vsi_handle]->sf;
		if (!sf->devlink_port.devlink_rate)
			/* leaf nodes doesn't have children
			 * so we don't set rate_node
			 */
			devl_rate_leaf_create(&sf->devlink_port, node,
					      node->parent->rate_node);
	} else if (node->info.data.elem_type != ICE_AQC_ELEM_TYPE_LEAF &&
		   node->parent->rate_node) {
		rate_node = devl_rate_node_create(devlink, node, node->name,
						  node->parent->rate_node);
	}

	if (rate_node && !IS_ERR(rate_node))
		node->rate_node = rate_node;

traverse_children:
	for (i = 0; i < node->num_children; i++)
		ice_traverse_tx_tree(devlink, node->children[i], tc_node, pf);
}

/**
 * ice_devlink_rate_init_tx_topology - export Tx scheduler tree to devlink rate
 * @devlink: devlink struct
 * @vsi: main vsi struct
 *
 * This function finds a root node, then calls ice_traverse_tx tree, which
 * traverses the tree and exports it's contents to devlink rate.
 */
int ice_devlink_rate_init_tx_topology(struct devlink *devlink, struct ice_vsi *vsi)
{
	struct ice_port_info *pi = vsi->port_info;
	struct ice_sched_node *tc_node;
	struct ice_pf *pf = vsi->back;
	int i;

	tc_node = pi->root->children[0];
	mutex_lock(&pi->sched_lock);
	for (i = 0; i < tc_node->num_children; i++)
		ice_traverse_tx_tree(devlink, tc_node->children[i], tc_node, pf);
	mutex_unlock(&pi->sched_lock);

	return 0;
}

static void ice_clear_rate_nodes(struct ice_sched_node *node)
{
	node->rate_node = NULL;

	for (int i = 0; i < node->num_children; i++)
		ice_clear_rate_nodes(node->children[i]);
}

/**
 * ice_devlink_rate_clear_tx_topology - clear node->rate_node
 * @vsi: main vsi struct
 *
 * Clear rate_node to cleanup creation of Tx topology.
 *
 */
void ice_devlink_rate_clear_tx_topology(struct ice_vsi *vsi)
{
	struct ice_port_info *pi = vsi->port_info;

	mutex_lock(&pi->sched_lock);
	ice_clear_rate_nodes(pi->root->children[0]);
	mutex_unlock(&pi->sched_lock);
}

/**
 * ice_set_object_tx_share - sets node scheduling parameter
 * @pi: devlink struct instance
 * @node: node struct instance
 * @bw: bandwidth in bytes per second
 * @extack: extended netdev ack structure
 *
 * This function sets ICE_MIN_BW scheduling BW limit.
 */
static int ice_set_object_tx_share(struct ice_port_info *pi, struct ice_sched_node *node,
				   u64 bw, struct netlink_ext_ack *extack)
{
	int status;

	mutex_lock(&pi->sched_lock);
	/* converts bytes per second to kilo bits per second */
	node->tx_share = div_u64(bw, 125);
	status = ice_sched_set_node_bw_lmt(pi, node, ICE_MIN_BW, node->tx_share);
	mutex_unlock(&pi->sched_lock);

	if (status)
		NL_SET_ERR_MSG_MOD(extack, "Can't set scheduling node tx_share");

	return status;
}

/**
 * ice_set_object_tx_max - sets node scheduling parameter
 * @pi: devlink struct instance
 * @node: node struct instance
 * @bw: bandwidth in bytes per second
 * @extack: extended netdev ack structure
 *
 * This function sets ICE_MAX_BW scheduling BW limit.
 */
static int ice_set_object_tx_max(struct ice_port_info *pi, struct ice_sched_node *node,
				 u64 bw, struct netlink_ext_ack *extack)
{
	int status;

	mutex_lock(&pi->sched_lock);
	/* converts bytes per second value to kilo bits per second */
	node->tx_max = div_u64(bw, 125);
	status = ice_sched_set_node_bw_lmt(pi, node, ICE_MAX_BW, node->tx_max);
	mutex_unlock(&pi->sched_lock);

	if (status)
		NL_SET_ERR_MSG_MOD(extack, "Can't set scheduling node tx_max");

	return status;
}

/**
 * ice_set_object_tx_priority - sets node scheduling parameter
 * @pi: devlink struct instance
 * @node: node struct instance
 * @priority: value representing priority for strict priority arbitration
 * @extack: extended netdev ack structure
 *
 * This function sets priority of node among siblings.
 */
static int ice_set_object_tx_priority(struct ice_port_info *pi, struct ice_sched_node *node,
				      u32 priority, struct netlink_ext_ack *extack)
{
	int status;

	if (priority >= 8) {
		NL_SET_ERR_MSG_MOD(extack, "Priority should be less than 8");
		return -EINVAL;
	}

	mutex_lock(&pi->sched_lock);
	node->tx_priority = priority;
	status = ice_sched_set_node_priority(pi, node, node->tx_priority);
	mutex_unlock(&pi->sched_lock);

	if (status)
		NL_SET_ERR_MSG_MOD(extack, "Can't set scheduling node tx_priority");

	return status;
}

/**
 * ice_set_object_tx_weight - sets node scheduling parameter
 * @pi: devlink struct instance
 * @node: node struct instance
 * @weight: value represeting relative weight for WFQ arbitration
 * @extack: extended netdev ack structure
 *
 * This function sets node weight for WFQ algorithm.
 */
static int ice_set_object_tx_weight(struct ice_port_info *pi, struct ice_sched_node *node,
				    u32 weight, struct netlink_ext_ack *extack)
{
	int status;

	if (weight > 200 || weight < 1) {
		NL_SET_ERR_MSG_MOD(extack, "Weight must be between 1 and 200");
		return -EINVAL;
	}

	mutex_lock(&pi->sched_lock);
	node->tx_weight = weight;
	status = ice_sched_set_node_weight(pi, node, node->tx_weight);
	mutex_unlock(&pi->sched_lock);

	if (status)
		NL_SET_ERR_MSG_MOD(extack, "Can't set scheduling node tx_weight");

	return status;
}

/**
 * ice_get_pi_from_dev_rate - get port info from devlink_rate
 * @rate_node: devlink struct instance
 *
 * This function returns corresponding port_info struct of devlink_rate
 */
static struct ice_port_info *ice_get_pi_from_dev_rate(struct devlink_rate *rate_node)
{
	struct ice_pf *pf = devlink_priv(rate_node->devlink);

	return ice_get_main_vsi(pf)->port_info;
}

static int ice_devlink_rate_node_new(struct devlink_rate *rate_node, void **priv,
				     struct netlink_ext_ack *extack)
{
	struct ice_sched_node *node;
	struct ice_port_info *pi;

	pi = ice_get_pi_from_dev_rate(rate_node);

	if (!ice_enable_custom_tx(devlink_priv(rate_node->devlink)))
		return -EBUSY;

	/* preallocate memory for ice_sched_node */
	node = devm_kzalloc(ice_hw_to_dev(pi->hw), sizeof(*node), GFP_KERNEL);
	*priv = node;

	return 0;
}

static int ice_devlink_rate_node_del(struct devlink_rate *rate_node, void *priv,
				     struct netlink_ext_ack *extack)
{
	struct ice_sched_node *node, *tc_node;
	struct ice_port_info *pi;

	pi = ice_get_pi_from_dev_rate(rate_node);
	tc_node = pi->root->children[0];
	node = priv;

	if (!rate_node->parent || !node || tc_node == node || !extack)
		return 0;

	if (!ice_enable_custom_tx(devlink_priv(rate_node->devlink)))
		return -EBUSY;

	/* can't allow to delete a node with children */
	if (node->num_children)
		return -EINVAL;

	mutex_lock(&pi->sched_lock);
	ice_free_sched_node(pi, node);
	mutex_unlock(&pi->sched_lock);

	return 0;
}

static int ice_devlink_rate_leaf_tx_max_set(struct devlink_rate *rate_leaf, void *priv,
					    u64 tx_max, struct netlink_ext_ack *extack)
{
	struct ice_sched_node *node = priv;

	if (!ice_enable_custom_tx(devlink_priv(rate_leaf->devlink)))
		return -EBUSY;

	if (!node)
		return 0;

	return ice_set_object_tx_max(ice_get_pi_from_dev_rate(rate_leaf),
				     node, tx_max, extack);
}

static int ice_devlink_rate_leaf_tx_share_set(struct devlink_rate *rate_leaf, void *priv,
					      u64 tx_share, struct netlink_ext_ack *extack)
{
	struct ice_sched_node *node = priv;

	if (!ice_enable_custom_tx(devlink_priv(rate_leaf->devlink)))
		return -EBUSY;

	if (!node)
		return 0;

	return ice_set_object_tx_share(ice_get_pi_from_dev_rate(rate_leaf), node,
				       tx_share, extack);
}

static int ice_devlink_rate_leaf_tx_priority_set(struct devlink_rate *rate_leaf, void *priv,
						 u32 tx_priority, struct netlink_ext_ack *extack)
{
	struct ice_sched_node *node = priv;

	if (!ice_enable_custom_tx(devlink_priv(rate_leaf->devlink)))
		return -EBUSY;

	if (!node)
		return 0;

	return ice_set_object_tx_priority(ice_get_pi_from_dev_rate(rate_leaf), node,
					  tx_priority, extack);
}

static int ice_devlink_rate_leaf_tx_weight_set(struct devlink_rate *rate_leaf, void *priv,
					       u32 tx_weight, struct netlink_ext_ack *extack)
{
	struct ice_sched_node *node = priv;

	if (!ice_enable_custom_tx(devlink_priv(rate_leaf->devlink)))
		return -EBUSY;

	if (!node)
		return 0;

	return ice_set_object_tx_weight(ice_get_pi_from_dev_rate(rate_leaf), node,
					tx_weight, extack);
}

static int ice_devlink_rate_node_tx_max_set(struct devlink_rate *rate_node, void *priv,
					    u64 tx_max, struct netlink_ext_ack *extack)
{
	struct ice_sched_node *node = priv;

	if (!ice_enable_custom_tx(devlink_priv(rate_node->devlink)))
		return -EBUSY;

	if (!node)
		return 0;

	return ice_set_object_tx_max(ice_get_pi_from_dev_rate(rate_node),
				     node, tx_max, extack);
}

static int ice_devlink_rate_node_tx_share_set(struct devlink_rate *rate_node, void *priv,
					      u64 tx_share, struct netlink_ext_ack *extack)
{
	struct ice_sched_node *node = priv;

	if (!ice_enable_custom_tx(devlink_priv(rate_node->devlink)))
		return -EBUSY;

	if (!node)
		return 0;

	return ice_set_object_tx_share(ice_get_pi_from_dev_rate(rate_node),
				       node, tx_share, extack);
}

static int ice_devlink_rate_node_tx_priority_set(struct devlink_rate *rate_node, void *priv,
						 u32 tx_priority, struct netlink_ext_ack *extack)
{
	struct ice_sched_node *node = priv;

	if (!ice_enable_custom_tx(devlink_priv(rate_node->devlink)))
		return -EBUSY;

	if (!node)
		return 0;

	return ice_set_object_tx_priority(ice_get_pi_from_dev_rate(rate_node),
					  node, tx_priority, extack);
}

static int ice_devlink_rate_node_tx_weight_set(struct devlink_rate *rate_node, void *priv,
					       u32 tx_weight, struct netlink_ext_ack *extack)
{
	struct ice_sched_node *node = priv;

	if (!ice_enable_custom_tx(devlink_priv(rate_node->devlink)))
		return -EBUSY;

	if (!node)
		return 0;

	return ice_set_object_tx_weight(ice_get_pi_from_dev_rate(rate_node),
					node, tx_weight, extack);
}

static int ice_devlink_set_parent(struct devlink_rate *devlink_rate,
				  struct devlink_rate *parent,
				  void *priv, void *parent_priv,
				  struct netlink_ext_ack *extack)
{
	struct ice_port_info *pi = ice_get_pi_from_dev_rate(devlink_rate);
	struct ice_sched_node *tc_node, *node, *parent_node;
	u16 num_nodes_added;
	u32 first_node_teid;
	u32 node_teid;
	int status;

	tc_node = pi->root->children[0];
	node = priv;

	if (!extack)
		return 0;

	if (!ice_enable_custom_tx(devlink_priv(devlink_rate->devlink)))
		return -EBUSY;

	if (!parent) {
		if (!node || tc_node == node || node->num_children)
			return -EINVAL;

		mutex_lock(&pi->sched_lock);
		ice_free_sched_node(pi, node);
		mutex_unlock(&pi->sched_lock);

		return 0;
	}

	parent_node = parent_priv;

	/* if the node doesn't exist, create it */
	if (!node->parent) {
		mutex_lock(&pi->sched_lock);
		status = ice_sched_add_elems(pi, tc_node, parent_node,
					     parent_node->tx_sched_layer + 1,
					     1, &num_nodes_added, &first_node_teid,
					     &node);
		mutex_unlock(&pi->sched_lock);

		if (status) {
			NL_SET_ERR_MSG_MOD(extack, "Can't add a new node");
			return status;
		}

		if (devlink_rate->tx_share)
			ice_set_object_tx_share(pi, node, devlink_rate->tx_share, extack);
		if (devlink_rate->tx_max)
			ice_set_object_tx_max(pi, node, devlink_rate->tx_max, extack);
		if (devlink_rate->tx_priority)
			ice_set_object_tx_priority(pi, node, devlink_rate->tx_priority, extack);
		if (devlink_rate->tx_weight)
			ice_set_object_tx_weight(pi, node, devlink_rate->tx_weight, extack);
	} else {
		node_teid = le32_to_cpu(node->info.node_teid);
		mutex_lock(&pi->sched_lock);
		status = ice_sched_move_nodes(pi, parent_node, 1, &node_teid);
		mutex_unlock(&pi->sched_lock);

		if (status)
			NL_SET_ERR_MSG_MOD(extack, "Can't move existing node to a new parent");
	}

	return status;
}

/**
 * ice_devlink_reinit_up - do reinit of the given PF
 * @pf: pointer to the PF struct
 */
static int ice_devlink_reinit_up(struct ice_pf *pf)
{
	struct ice_vsi *vsi = ice_get_main_vsi(pf);
	int err;

	err = ice_init_dev(pf);
	if (err)
		return err;

	vsi->flags = ICE_VSI_FLAG_INIT;

	rtnl_lock();
	err = ice_vsi_cfg(vsi);
	rtnl_unlock();
	if (err)
		goto err_vsi_cfg;

	/* No need to take devl_lock, it's already taken by devlink API */
	err = ice_load(pf);
	if (err)
		goto err_load;

	return 0;

err_load:
	rtnl_lock();
	ice_vsi_decfg(vsi);
	rtnl_unlock();
err_vsi_cfg:
	ice_deinit_dev(pf);
	return err;
}

/**
 * ice_devlink_reload_up - do reload up after reinit
 * @devlink: pointer to the devlink instance reloading
 * @action: the action requested
 * @limit: limits imposed by userspace, such as not resetting
 * @actions_performed: on return, indicate what actions actually performed
 * @extack: netlink extended ACK structure
 */
static int
ice_devlink_reload_up(struct devlink *devlink,
		      enum devlink_reload_action action,
		      enum devlink_reload_limit limit,
		      u32 *actions_performed,
		      struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_priv(devlink);

	switch (action) {
	case DEVLINK_RELOAD_ACTION_DRIVER_REINIT:
		*actions_performed = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT);
		return ice_devlink_reinit_up(pf);
	case DEVLINK_RELOAD_ACTION_FW_ACTIVATE:
		*actions_performed = BIT(DEVLINK_RELOAD_ACTION_FW_ACTIVATE);
		return ice_devlink_reload_empr_finish(pf, extack);
	default:
		WARN_ON(1);
		return -EOPNOTSUPP;
	}
}

static const struct devlink_ops ice_devlink_ops = {
	.supported_flash_update_params = DEVLINK_SUPPORT_FLASH_UPDATE_OVERWRITE_MASK,
	.reload_actions = BIT(DEVLINK_RELOAD_ACTION_DRIVER_REINIT) |
			  BIT(DEVLINK_RELOAD_ACTION_FW_ACTIVATE),
	.reload_down = ice_devlink_reload_down,
	.reload_up = ice_devlink_reload_up,
	.eswitch_mode_get = ice_eswitch_mode_get,
	.eswitch_mode_set = ice_eswitch_mode_set,
	.info_get = ice_devlink_info_get,
	.flash_update = ice_devlink_flash_update,

	.rate_node_new = ice_devlink_rate_node_new,
	.rate_node_del = ice_devlink_rate_node_del,

	.rate_leaf_tx_max_set = ice_devlink_rate_leaf_tx_max_set,
	.rate_leaf_tx_share_set = ice_devlink_rate_leaf_tx_share_set,
	.rate_leaf_tx_priority_set = ice_devlink_rate_leaf_tx_priority_set,
	.rate_leaf_tx_weight_set = ice_devlink_rate_leaf_tx_weight_set,

	.rate_node_tx_max_set = ice_devlink_rate_node_tx_max_set,
	.rate_node_tx_share_set = ice_devlink_rate_node_tx_share_set,
	.rate_node_tx_priority_set = ice_devlink_rate_node_tx_priority_set,
	.rate_node_tx_weight_set = ice_devlink_rate_node_tx_weight_set,

	.rate_leaf_parent_set = ice_devlink_set_parent,
	.rate_node_parent_set = ice_devlink_set_parent,

	.port_new = ice_devlink_port_new,
};

static const struct devlink_ops ice_sf_devlink_ops;

static int
ice_devlink_enable_roce_get(struct devlink *devlink, u32 id,
			    struct devlink_param_gset_ctx *ctx)
{
	struct ice_pf *pf = devlink_priv(devlink);

	ctx->val.vbool = pf->rdma_mode & IIDC_RDMA_PROTOCOL_ROCEV2 ? true : false;

	return 0;
}

static int ice_devlink_enable_roce_set(struct devlink *devlink, u32 id,
				       struct devlink_param_gset_ctx *ctx,
				       struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_priv(devlink);
	bool roce_ena = ctx->val.vbool;
	int ret;

	if (!roce_ena) {
		ice_unplug_aux_dev(pf);
		pf->rdma_mode &= ~IIDC_RDMA_PROTOCOL_ROCEV2;
		return 0;
	}

	pf->rdma_mode |= IIDC_RDMA_PROTOCOL_ROCEV2;
	ret = ice_plug_aux_dev(pf);
	if (ret)
		pf->rdma_mode &= ~IIDC_RDMA_PROTOCOL_ROCEV2;

	return ret;
}

static int
ice_devlink_enable_roce_validate(struct devlink *devlink, u32 id,
				 union devlink_param_value val,
				 struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_priv(devlink);

	if (!test_bit(ICE_FLAG_RDMA_ENA, pf->flags))
		return -EOPNOTSUPP;

	if (pf->rdma_mode & IIDC_RDMA_PROTOCOL_IWARP) {
		NL_SET_ERR_MSG_MOD(extack, "iWARP is currently enabled. This device cannot enable iWARP and RoCEv2 simultaneously");
		return -EOPNOTSUPP;
	}

	return 0;
}

static int
ice_devlink_enable_iw_get(struct devlink *devlink, u32 id,
			  struct devlink_param_gset_ctx *ctx)
{
	struct ice_pf *pf = devlink_priv(devlink);

	ctx->val.vbool = pf->rdma_mode & IIDC_RDMA_PROTOCOL_IWARP;

	return 0;
}

static int ice_devlink_enable_iw_set(struct devlink *devlink, u32 id,
				     struct devlink_param_gset_ctx *ctx,
				     struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_priv(devlink);
	bool iw_ena = ctx->val.vbool;
	int ret;

	if (!iw_ena) {
		ice_unplug_aux_dev(pf);
		pf->rdma_mode &= ~IIDC_RDMA_PROTOCOL_IWARP;
		return 0;
	}

	pf->rdma_mode |= IIDC_RDMA_PROTOCOL_IWARP;
	ret = ice_plug_aux_dev(pf);
	if (ret)
		pf->rdma_mode &= ~IIDC_RDMA_PROTOCOL_IWARP;

	return ret;
}

static int
ice_devlink_enable_iw_validate(struct devlink *devlink, u32 id,
			       union devlink_param_value val,
			       struct netlink_ext_ack *extack)
{
	struct ice_pf *pf = devlink_priv(devlink);

	if (!test_bit(ICE_FLAG_RDMA_ENA, pf->flags))
		return -EOPNOTSUPP;

	if (pf->rdma_mode & IIDC_RDMA_PROTOCOL_ROCEV2) {
		NL_SET_ERR_MSG_MOD(extack, "RoCEv2 is currently enabled. This device cannot enable iWARP and RoCEv2 simultaneously");
		return -EOPNOTSUPP;
	}

	return 0;
}

#define DEVLINK_LOCAL_FWD_DISABLED_STR "disabled"
#define DEVLINK_LOCAL_FWD_ENABLED_STR "enabled"
#define DEVLINK_LOCAL_FWD_PRIORITIZED_STR "prioritized"

/**
 * ice_devlink_local_fwd_mode_to_str - Get string for local_fwd mode.
 * @mode: local forwarding for mode used in port_info struct.
 *
 * Return: Mode respective string or "Invalid".
 */
static const char *
ice_devlink_local_fwd_mode_to_str(enum ice_local_fwd_mode mode)
{
	switch (mode) {
	case ICE_LOCAL_FWD_MODE_ENABLED:
		return DEVLINK_LOCAL_FWD_ENABLED_STR;
	case ICE_LOCAL_FWD_MODE_PRIORITIZED:
		return DEVLINK_LOCAL_FWD_PRIORITIZED_STR;
	case ICE_LOCAL_FWD_MODE_DISABLED:
		return DEVLINK_LOCAL_FWD_DISABLED_STR;
	}

	return "Invalid";
}

/**
 * ice_devlink_local_fwd_str_to_mode - Get local_fwd mode from string name.
 * @mode_str: local forwarding mode string.
 *
 * Return: Mode value or negative number if invalid.
 */
static int ice_devlink_local_fwd_str_to_mode(const char *mode_str)
{
	if (!strcmp(mode_str, DEVLINK_LOCAL_FWD_ENABLED_STR))
		return ICE_LOCAL_FWD_MODE_ENABLED;
	else if (!strcmp(mode_str, DEVLINK_LOCAL_FWD_PRIORITIZED_STR))
		return ICE_LOCAL_FWD_MODE_PRIORITIZED;
	else if (!strcmp(mode_str, DEVLINK_LOCAL_FWD_DISABLED_STR))
		return ICE_LOCAL_FWD_MODE_DISABLED;

	return -EINVAL;
}

/**
 * ice_devlink_local_fwd_get - Get local_fwd parameter.
 * @devlink: Pointer to the devlink instance.
 * @id: The parameter ID to set.
 * @ctx: Context to store the parameter value.
 *
 * Return: Zero.
 */
static int ice_devlink_local_fwd_get(struct devlink *devlink, u32 id,
				     struct devlink_param_gset_ctx *ctx)
{
	struct ice_pf *pf = devlink_priv(devlink);
	struct ice_port_info *pi;
	const char *mode_str;

	pi = pf->hw.port_info;
	mode_str = ice_devlink_local_fwd_mode_to_str(pi->local_fwd_mode);
	snprintf(ctx->val.vstr, sizeof(ctx->val.vstr), "%s", mode_str);

	return 0;
}

/**
 * ice_devlink_local_fwd_set - Set local_fwd parameter.
 * @devlink: Pointer to the devlink instance.
 * @id: The parameter ID to set.
 * @ctx: Context to get the parameter value.
 * @extack: Netlink extended ACK structure.
 *
 * Return: Zero.
 */
static int ice_devlink_local_fwd_set(struct devlink *devlink, u32 id,
				     struct devlink_param_gset_ctx *ctx,
				     struct netlink_ext_ack *extack)
{
	int new_local_fwd_mode = ice_devlink_local_fwd_str_to_mode(ctx->val.vstr);
	struct ice_pf *pf = devlink_priv(devlink);
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_port_info *pi;

	pi = pf->hw.port_info;
	if (pi->local_fwd_mode != new_local_fwd_mode) {
		pi->local_fwd_mode = new_local_fwd_mode;
		dev_info(dev, "Setting local_fwd to %s\n", ctx->val.vstr);
		ice_schedule_reset(pf, ICE_RESET_CORER);
	}

	return 0;
}

/**
 * ice_devlink_local_fwd_validate - Validate passed local_fwd parameter value.
 * @devlink: Unused pointer to devlink instance.
 * @id: The parameter ID to validate.
 * @val: Value to validate.
 * @extack: Netlink extended ACK structure.
 *
 * Supported values are:
 * "enabled" - local_fwd is enabled, "disabled" - local_fwd is disabled
 * "prioritized" - local_fwd traffic is prioritized in scheduling.
 *
 * Return: Zero when passed parameter value is supported. Negative value on
 * error.
 */
static int ice_devlink_local_fwd_validate(struct devlink *devlink, u32 id,
					  union devlink_param_value val,
					  struct netlink_ext_ack *extack)
{
	if (ice_devlink_local_fwd_str_to_mode(val.vstr) < 0) {
		NL_SET_ERR_MSG_MOD(extack, "Error: Requested value is not supported.");
		return -EINVAL;
	}

	return 0;
}

enum ice_param_id {
	ICE_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	ICE_DEVLINK_PARAM_ID_TX_SCHED_LAYERS,
	ICE_DEVLINK_PARAM_ID_LOCAL_FWD,
};

static const struct devlink_param ice_dvl_rdma_params[] = {
	DEVLINK_PARAM_GENERIC(ENABLE_ROCE, BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			      ice_devlink_enable_roce_get,
			      ice_devlink_enable_roce_set,
			      ice_devlink_enable_roce_validate),
	DEVLINK_PARAM_GENERIC(ENABLE_IWARP, BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			      ice_devlink_enable_iw_get,
			      ice_devlink_enable_iw_set,
			      ice_devlink_enable_iw_validate),
};

static const struct devlink_param ice_dvl_sched_params[] = {
	DEVLINK_PARAM_DRIVER(ICE_DEVLINK_PARAM_ID_TX_SCHED_LAYERS,
			     "tx_scheduling_layers",
			     DEVLINK_PARAM_TYPE_U8,
			     BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			     ice_devlink_tx_sched_layers_get,
			     ice_devlink_tx_sched_layers_set,
			     ice_devlink_tx_sched_layers_validate),
	DEVLINK_PARAM_DRIVER(ICE_DEVLINK_PARAM_ID_LOCAL_FWD,
			     "local_forwarding", DEVLINK_PARAM_TYPE_STRING,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     ice_devlink_local_fwd_get,
			     ice_devlink_local_fwd_set,
			     ice_devlink_local_fwd_validate),
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
	if (devm_add_action_or_reset(dev, ice_devlink_free, devlink))
		return NULL;

	return devlink_priv(devlink);
}

/**
 * ice_allocate_sf - Allocate devlink and return SF structure pointer
 * @dev: the device to allocate for
 * @pf: pointer to the PF structure
 *
 * Allocate a devlink instance for SF.
 *
 * Return: ice_sf_priv pointer to allocated memory or ERR_PTR in case of error
 */
struct ice_sf_priv *ice_allocate_sf(struct device *dev, struct ice_pf *pf)
{
	struct devlink *devlink;
	int err;

	devlink = devlink_alloc(&ice_sf_devlink_ops, sizeof(struct ice_sf_priv),
				dev);
	if (!devlink)
		return ERR_PTR(-ENOMEM);

	err = devl_nested_devlink_set(priv_to_devlink(pf), devlink);
	if (err) {
		devlink_free(devlink);
		return ERR_PTR(err);
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
void ice_devlink_register(struct ice_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);

	devl_register(devlink);
}

/**
 * ice_devlink_unregister - Unregister devlink resources for this PF.
 * @pf: the PF structure to cleanup
 *
 * Releases resources used by devlink and cleans up associated memory.
 */
void ice_devlink_unregister(struct ice_pf *pf)
{
	devl_unregister(priv_to_devlink(pf));
}

int ice_devlink_register_params(struct ice_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct ice_hw *hw = &pf->hw;
	int status;

	status = devl_params_register(devlink, ice_dvl_rdma_params,
				      ARRAY_SIZE(ice_dvl_rdma_params));
	if (status)
		return status;

	if (hw->func_caps.common_cap.tx_sched_topo_comp_mode_en)
		status = devl_params_register(devlink, ice_dvl_sched_params,
					      ARRAY_SIZE(ice_dvl_sched_params));

	return status;
}

void ice_devlink_unregister_params(struct ice_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	struct ice_hw *hw = &pf->hw;

	devl_params_unregister(devlink, ice_dvl_rdma_params,
			       ARRAY_SIZE(ice_dvl_rdma_params));

	if (hw->func_caps.common_cap.tx_sched_topo_comp_mode_en)
		devl_params_unregister(devlink, ice_dvl_sched_params,
				       ARRAY_SIZE(ice_dvl_sched_params));
}

#define ICE_DEVLINK_READ_BLK_SIZE (1024 * 1024)

static const struct devlink_region_ops ice_nvm_region_ops;
static const struct devlink_region_ops ice_sram_region_ops;

/**
 * ice_devlink_nvm_snapshot - Capture a snapshot of the NVM flash contents
 * @devlink: the devlink instance
 * @ops: the devlink region to snapshot
 * @extack: extended ACK response structure
 * @data: on exit points to snapshot data buffer
 *
 * This function is called in response to a DEVLINK_CMD_REGION_NEW for either
 * the nvm-flash or shadow-ram region.
 *
 * It captures a snapshot of the NVM or Shadow RAM flash contents. This
 * snapshot can then later be viewed via the DEVLINK_CMD_REGION_READ netlink
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
	bool read_shadow_ram;
	u8 *nvm_data, *tmp, i;
	u32 nvm_size, left;
	s8 num_blks;
	int status;

	if (ops == &ice_nvm_region_ops) {
		read_shadow_ram = false;
		nvm_size = hw->flash.flash_size;
	} else if (ops == &ice_sram_region_ops) {
		read_shadow_ram = true;
		nvm_size = hw->flash.sr_words * 2u;
	} else {
		NL_SET_ERR_MSG_MOD(extack, "Unexpected region in snapshot function");
		return -EOPNOTSUPP;
	}

	nvm_data = vzalloc(nvm_size);
	if (!nvm_data)
		return -ENOMEM;

	num_blks = DIV_ROUND_UP(nvm_size, ICE_DEVLINK_READ_BLK_SIZE);
	tmp = nvm_data;
	left = nvm_size;

	/* Some systems take longer to read the NVM than others which causes the
	 * FW to reclaim the NVM lock before the entire NVM has been read. Fix
	 * this by breaking the reads of the NVM into smaller chunks that will
	 * probably not take as long. This has some overhead since we are
	 * increasing the number of AQ commands, but it should always work
	 */
	for (i = 0; i < num_blks; i++) {
		u32 read_sz = min_t(u32, ICE_DEVLINK_READ_BLK_SIZE, left);

		status = ice_acquire_nvm(hw, ICE_RES_READ);
		if (status) {
			dev_dbg(dev, "ice_acquire_nvm failed, err %d aq_err %d\n",
				status, hw->adminq.sq_last_status);
			NL_SET_ERR_MSG_MOD(extack, "Failed to acquire NVM semaphore");
			vfree(nvm_data);
			return -EIO;
		}

		status = ice_read_flat_nvm(hw, i * ICE_DEVLINK_READ_BLK_SIZE,
					   &read_sz, tmp, read_shadow_ram);
		if (status) {
			dev_dbg(dev, "ice_read_flat_nvm failed after reading %u bytes, err %d aq_err %d\n",
				read_sz, status, hw->adminq.sq_last_status);
			NL_SET_ERR_MSG_MOD(extack, "Failed to read NVM contents");
			ice_release_nvm(hw);
			vfree(nvm_data);
			return -EIO;
		}
		ice_release_nvm(hw);

		tmp += read_sz;
		left -= read_sz;
	}

	*data = nvm_data;

	return 0;
}

/**
 * ice_devlink_nvm_read - Read a portion of NVM flash contents
 * @devlink: the devlink instance
 * @ops: the devlink region to snapshot
 * @extack: extended ACK response structure
 * @offset: the offset to start at
 * @size: the amount to read
 * @data: the data buffer to read into
 *
 * This function is called in response to DEVLINK_CMD_REGION_READ to directly
 * read a section of the NVM contents.
 *
 * It reads from either the nvm-flash or shadow-ram region contents.
 *
 * @returns zero on success, and updates the data pointer. Returns a non-zero
 * error code on failure.
 */
static int ice_devlink_nvm_read(struct devlink *devlink,
				const struct devlink_region_ops *ops,
				struct netlink_ext_ack *extack,
				u64 offset, u32 size, u8 *data)
{
	struct ice_pf *pf = devlink_priv(devlink);
	struct device *dev = ice_pf_to_dev(pf);
	struct ice_hw *hw = &pf->hw;
	bool read_shadow_ram;
	u64 nvm_size;
	int status;

	if (ops == &ice_nvm_region_ops) {
		read_shadow_ram = false;
		nvm_size = hw->flash.flash_size;
	} else if (ops == &ice_sram_region_ops) {
		read_shadow_ram = true;
		nvm_size = hw->flash.sr_words * 2u;
	} else {
		NL_SET_ERR_MSG_MOD(extack, "Unexpected region in snapshot function");
		return -EOPNOTSUPP;
	}

	if (offset + size >= nvm_size) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot read beyond the region size");
		return -ERANGE;
	}

	status = ice_acquire_nvm(hw, ICE_RES_READ);
	if (status) {
		dev_dbg(dev, "ice_acquire_nvm failed, err %d aq_err %d\n",
			status, hw->adminq.sq_last_status);
		NL_SET_ERR_MSG_MOD(extack, "Failed to acquire NVM semaphore");
		return -EIO;
	}

	status = ice_read_flat_nvm(hw, (u32)offset, &size, data,
				   read_shadow_ram);
	if (status) {
		dev_dbg(dev, "ice_read_flat_nvm failed after reading %u bytes, err %d aq_err %d\n",
			size, status, hw->adminq.sq_last_status);
		NL_SET_ERR_MSG_MOD(extack, "Failed to read NVM contents");
		ice_release_nvm(hw);
		return -EIO;
	}
	ice_release_nvm(hw);

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
	void *devcaps;
	int status;

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
		return status;
	}

	*data = (u8 *)devcaps;

	return 0;
}

static const struct devlink_region_ops ice_nvm_region_ops = {
	.name = "nvm-flash",
	.destructor = vfree,
	.snapshot = ice_devlink_nvm_snapshot,
	.read = ice_devlink_nvm_read,
};

static const struct devlink_region_ops ice_sram_region_ops = {
	.name = "shadow-ram",
	.destructor = vfree,
	.snapshot = ice_devlink_nvm_snapshot,
	.read = ice_devlink_nvm_read,
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
	u64 nvm_size, sram_size;

	nvm_size = pf->hw.flash.flash_size;
	pf->nvm_region = devl_region_create(devlink, &ice_nvm_region_ops, 1,
					    nvm_size);
	if (IS_ERR(pf->nvm_region)) {
		dev_err(dev, "failed to create NVM devlink region, err %ld\n",
			PTR_ERR(pf->nvm_region));
		pf->nvm_region = NULL;
	}

	sram_size = pf->hw.flash.sr_words * 2u;
	pf->sram_region = devl_region_create(devlink, &ice_sram_region_ops,
					     1, sram_size);
	if (IS_ERR(pf->sram_region)) {
		dev_err(dev, "failed to create shadow-ram devlink region, err %ld\n",
			PTR_ERR(pf->sram_region));
		pf->sram_region = NULL;
	}

	pf->devcaps_region = devl_region_create(devlink,
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
		devl_region_destroy(pf->nvm_region);

	if (pf->sram_region)
		devl_region_destroy(pf->sram_region);

	if (pf->devcaps_region)
		devl_region_destroy(pf->devcaps_region);
}
