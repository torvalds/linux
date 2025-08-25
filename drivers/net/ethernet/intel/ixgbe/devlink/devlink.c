// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025, Intel Corporation. */

#include "ixgbe.h"
#include "devlink.h"
#include "ixgbe_fw_update.h"

struct ixgbe_info_ctx {
	char buf[128];
	struct ixgbe_orom_info pending_orom;
	struct ixgbe_nvm_info pending_nvm;
	struct ixgbe_netlist_info pending_netlist;
	struct ixgbe_hw_dev_caps dev_caps;
};

enum ixgbe_devlink_version_type {
	IXGBE_DL_VERSION_RUNNING,
	IXGBE_DL_VERSION_STORED
};

static void ixgbe_info_get_dsn(struct ixgbe_adapter *adapter,
			       struct ixgbe_info_ctx *ctx)
{
	u8 dsn[8];

	/* Copy the DSN into an array in Big Endian format */
	put_unaligned_be64(pci_get_dsn(adapter->pdev), dsn);

	snprintf(ctx->buf, sizeof(ctx->buf), "%8phD", dsn);
}

static void ixgbe_info_orom_ver(struct ixgbe_adapter *adapter,
				struct ixgbe_info_ctx *ctx,
				enum ixgbe_devlink_version_type type)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_nvm_version nvm_ver;

	ctx->buf[0] = '\0';

	if (hw->mac.type == ixgbe_mac_e610) {
		struct ixgbe_orom_info *orom = &adapter->hw.flash.orom;

		if (type == IXGBE_DL_VERSION_STORED &&
		    ctx->dev_caps.common_cap.nvm_update_pending_orom)
			orom = &ctx->pending_orom;

		snprintf(ctx->buf, sizeof(ctx->buf), "%u.%u.%u",
			 orom->major, orom->build, orom->patch);
		return;
	}

	ixgbe_get_oem_prod_version(hw, &nvm_ver);
	if (nvm_ver.oem_valid) {
		snprintf(ctx->buf, sizeof(ctx->buf), "%x.%x.%x",
			 nvm_ver.oem_major, nvm_ver.oem_minor,
			 nvm_ver.oem_release);

		return;
	}

	ixgbe_get_orom_version(hw, &nvm_ver);
	if (nvm_ver.or_valid)
		snprintf(ctx->buf, sizeof(ctx->buf), "%d.%d.%d",
			 nvm_ver.or_major, nvm_ver.or_build, nvm_ver.or_patch);
}

static void ixgbe_info_eetrack(struct ixgbe_adapter *adapter,
			       struct ixgbe_info_ctx *ctx,
			       enum ixgbe_devlink_version_type type)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_nvm_version nvm_ver;

	if (hw->mac.type == ixgbe_mac_e610) {
		u32 eetrack = hw->flash.nvm.eetrack;

		if (type == IXGBE_DL_VERSION_STORED &&
		    ctx->dev_caps.common_cap.nvm_update_pending_nvm)
			eetrack = ctx->pending_nvm.eetrack;

		snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", eetrack);
		return;
	}

	ixgbe_get_oem_prod_version(hw, &nvm_ver);

	/* No ETRACK version for OEM */
	if (nvm_ver.oem_valid) {
		ctx->buf[0] = '\0';
		return;
	}

	ixgbe_get_etk_id(hw, &nvm_ver);
	snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", nvm_ver.etk_id);
}

static void ixgbe_info_fw_api(struct ixgbe_adapter *adapter,
			      struct ixgbe_info_ctx *ctx)
{
	struct ixgbe_hw *hw = &adapter->hw;

	snprintf(ctx->buf, sizeof(ctx->buf), "%u.%u.%u",
		 hw->api_maj_ver, hw->api_min_ver, hw->api_patch);
}

static void ixgbe_info_fw_build(struct ixgbe_adapter *adapter,
				struct ixgbe_info_ctx *ctx)
{
	struct ixgbe_hw *hw = &adapter->hw;

	snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", hw->fw_build);
}

static void ixgbe_info_fw_srev(struct ixgbe_adapter *adapter,
			       struct ixgbe_info_ctx *ctx,
			       enum ixgbe_devlink_version_type type)
{
	struct ixgbe_nvm_info *nvm = &adapter->hw.flash.nvm;

	if (type == IXGBE_DL_VERSION_STORED &&
	    ctx->dev_caps.common_cap.nvm_update_pending_nvm)
		nvm = &ctx->pending_nvm;

	snprintf(ctx->buf, sizeof(ctx->buf), "%u", nvm->srev);
}

static void ixgbe_info_orom_srev(struct ixgbe_adapter *adapter,
				 struct ixgbe_info_ctx *ctx,
				 enum ixgbe_devlink_version_type type)
{
	struct ixgbe_orom_info *orom = &adapter->hw.flash.orom;

	if (type == IXGBE_DL_VERSION_STORED &&
	    ctx->dev_caps.common_cap.nvm_update_pending_orom)
		orom = &ctx->pending_orom;

	snprintf(ctx->buf, sizeof(ctx->buf), "%u", orom->srev);
}

static void ixgbe_info_nvm_ver(struct ixgbe_adapter *adapter,
			       struct ixgbe_info_ctx *ctx,
			       enum ixgbe_devlink_version_type type)
{
	struct ixgbe_nvm_info *nvm = &adapter->hw.flash.nvm;

	if (type == IXGBE_DL_VERSION_STORED &&
	    ctx->dev_caps.common_cap.nvm_update_pending_nvm)
		nvm = &ctx->pending_nvm;

	snprintf(ctx->buf, sizeof(ctx->buf), "%x.%02x", nvm->major, nvm->minor);
}

static void ixgbe_info_netlist_ver(struct ixgbe_adapter *adapter,
				   struct ixgbe_info_ctx *ctx,
				   enum ixgbe_devlink_version_type type)
{
	struct ixgbe_netlist_info *netlist = &adapter->hw.flash.netlist;

	if (type == IXGBE_DL_VERSION_STORED &&
	    ctx->dev_caps.common_cap.nvm_update_pending_netlist)
		netlist = &ctx->pending_netlist;

	/* The netlist version fields are BCD formatted */
	snprintf(ctx->buf, sizeof(ctx->buf), "%x.%x.%x-%x.%x.%x",
		 netlist->major, netlist->minor,
		 netlist->type >> 16, netlist->type & 0xFFFF,
		 netlist->rev, netlist->cust_ver);
}

static void ixgbe_info_netlist_build(struct ixgbe_adapter *adapter,
				     struct ixgbe_info_ctx *ctx,
				     enum ixgbe_devlink_version_type type)
{
	struct ixgbe_netlist_info *netlist = &adapter->hw.flash.netlist;

	if (type == IXGBE_DL_VERSION_STORED &&
	    ctx->dev_caps.common_cap.nvm_update_pending_netlist)
		netlist = &ctx->pending_netlist;

	snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", netlist->hash);
}

static int ixgbe_set_ctx_dev_caps(struct ixgbe_hw *hw,
				  struct ixgbe_info_ctx *ctx,
				  struct netlink_ext_ack *extack)
{
	bool *pending_orom, *pending_nvm, *pending_netlist;
	int err;

	err = ixgbe_discover_dev_caps(hw, &ctx->dev_caps);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Unable to discover device capabilities");
		return err;
	}

	pending_orom = &ctx->dev_caps.common_cap.nvm_update_pending_orom;
	pending_nvm = &ctx->dev_caps.common_cap.nvm_update_pending_nvm;
	pending_netlist = &ctx->dev_caps.common_cap.nvm_update_pending_netlist;

	if (*pending_orom) {
		err = ixgbe_get_inactive_orom_ver(hw, &ctx->pending_orom);
		if (err)
			*pending_orom = false;
	}

	if (*pending_nvm) {
		err = ixgbe_get_inactive_nvm_ver(hw, &ctx->pending_nvm);
		if (err)
			*pending_nvm = false;
	}

	if (*pending_netlist) {
		err = ixgbe_get_inactive_netlist_ver(hw, &ctx->pending_netlist);
		if (err)
			*pending_netlist = false;
	}

	return 0;
}

static int ixgbe_devlink_info_get_e610(struct ixgbe_adapter *adapter,
				       struct devlink_info_req *req,
				       struct ixgbe_info_ctx *ctx)
{
	int err;

	ixgbe_info_fw_api(adapter, ctx);
	err = devlink_info_version_running_put(req,
					       DEVLINK_INFO_VERSION_GENERIC_FW_MGMT_API,
					       ctx->buf);
	if (err)
		return err;

	ixgbe_info_fw_build(adapter, ctx);
	err = devlink_info_version_running_put(req, "fw.mgmt.build", ctx->buf);
	if (err)
		return err;

	ixgbe_info_fw_srev(adapter, ctx, IXGBE_DL_VERSION_RUNNING);
	err = devlink_info_version_running_put(req, "fw.mgmt.srev", ctx->buf);
	if (err)
		return err;

	ixgbe_info_orom_srev(adapter, ctx, IXGBE_DL_VERSION_RUNNING);
	err = devlink_info_version_running_put(req, "fw.undi.srev", ctx->buf);
	if (err)
		return err;

	ixgbe_info_nvm_ver(adapter, ctx, IXGBE_DL_VERSION_RUNNING);
	err = devlink_info_version_running_put(req, "fw.psid.api", ctx->buf);
	if (err)
		return err;

	ixgbe_info_netlist_ver(adapter, ctx, IXGBE_DL_VERSION_RUNNING);
	err = devlink_info_version_running_put(req, "fw.netlist", ctx->buf);
	if (err)
		return err;

	ixgbe_info_netlist_build(adapter, ctx, IXGBE_DL_VERSION_RUNNING);
	return devlink_info_version_running_put(req, "fw.netlist.build",
						ctx->buf);
}

static int
ixgbe_devlink_pending_info_get_e610(struct ixgbe_adapter *adapter,
				    struct devlink_info_req *req,
				    struct ixgbe_info_ctx *ctx)
{
	int err;

	ixgbe_info_orom_ver(adapter, ctx, IXGBE_DL_VERSION_STORED);
	err = devlink_info_version_stored_put(req,
					      DEVLINK_INFO_VERSION_GENERIC_FW_UNDI,
					      ctx->buf);
	if (err)
		return err;

	ixgbe_info_eetrack(adapter, ctx, IXGBE_DL_VERSION_STORED);
	err = devlink_info_version_stored_put(req,
					      DEVLINK_INFO_VERSION_GENERIC_FW_BUNDLE_ID,
					      ctx->buf);
	if (err)
		return err;

	ixgbe_info_fw_srev(adapter, ctx, IXGBE_DL_VERSION_STORED);
	err = devlink_info_version_stored_put(req, "fw.mgmt.srev", ctx->buf);
	if (err)
		return err;

	ixgbe_info_orom_srev(adapter, ctx, IXGBE_DL_VERSION_STORED);
	err = devlink_info_version_stored_put(req, "fw.undi.srev", ctx->buf);
	if (err)
		return err;

	ixgbe_info_nvm_ver(adapter, ctx, IXGBE_DL_VERSION_STORED);
	err = devlink_info_version_stored_put(req, "fw.psid.api", ctx->buf);
	if (err)
		return err;

	ixgbe_info_netlist_ver(adapter, ctx, IXGBE_DL_VERSION_STORED);
	err = devlink_info_version_stored_put(req, "fw.netlist", ctx->buf);
	if (err)
		return err;

	ixgbe_info_netlist_build(adapter, ctx, IXGBE_DL_VERSION_STORED);
	return devlink_info_version_stored_put(req, "fw.netlist.build",
					       ctx->buf);
}

static int ixgbe_devlink_info_get(struct devlink *devlink,
				  struct devlink_info_req *req,
				  struct netlink_ext_ack *extack)
{
	struct ixgbe_adapter *adapter = devlink_priv(devlink);
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_info_ctx *ctx;
	int err;

	ctx = kmalloc(sizeof(*ctx), GFP_KERNEL);
	if (!ctx)
		return -ENOMEM;

	if (hw->mac.type == ixgbe_mac_e610)
		ixgbe_refresh_fw_version(adapter);

	ixgbe_info_get_dsn(adapter, ctx);
	err = devlink_info_serial_number_put(req, ctx->buf);
	if (err)
		goto free_ctx;

	err = hw->eeprom.ops.read_pba_string(hw, ctx->buf, sizeof(ctx->buf));
	if (err)
		goto free_ctx;

	err = devlink_info_version_fixed_put(req,
					     DEVLINK_INFO_VERSION_GENERIC_BOARD_ID,
					     ctx->buf);
	if (err)
		goto free_ctx;

	ixgbe_info_orom_ver(adapter, ctx, IXGBE_DL_VERSION_RUNNING);
	err = devlink_info_version_running_put(req,
					       DEVLINK_INFO_VERSION_GENERIC_FW_UNDI,
					       ctx->buf);
	if (err)
		goto free_ctx;

	ixgbe_info_eetrack(adapter, ctx, IXGBE_DL_VERSION_RUNNING);
	err = devlink_info_version_running_put(req,
					       DEVLINK_INFO_VERSION_GENERIC_FW_BUNDLE_ID,
					       ctx->buf);
	if (err || hw->mac.type != ixgbe_mac_e610)
		goto free_ctx;

	err = ixgbe_set_ctx_dev_caps(hw, ctx, extack);
	if (err)
		goto free_ctx;

	err = ixgbe_devlink_info_get_e610(adapter, req, ctx);
	if (err)
		goto free_ctx;

	err = ixgbe_devlink_pending_info_get_e610(adapter, req, ctx);
free_ctx:
	kfree(ctx);
	return err;
}

/**
 * ixgbe_devlink_reload_empr_start - Start EMP reset to activate new firmware
 * @devlink: pointer to the devlink instance to reload
 * @netns_change: if true, the network namespace is changing
 * @action: the action to perform. Must be DEVLINK_RELOAD_ACTION_FW_ACTIVATE
 * @limit: limits on what reload should do, such as not resetting
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
 *
 * Return: the exit code of the operation.
 */
static int ixgbe_devlink_reload_empr_start(struct devlink *devlink,
					   bool netns_change,
					   enum devlink_reload_action action,
					   enum devlink_reload_limit limit,
					   struct netlink_ext_ack *extack)
{
	struct ixgbe_adapter *adapter = devlink_priv(devlink);
	struct ixgbe_hw *hw = &adapter->hw;
	u8 pending;
	int err;

	if (hw->mac.type != ixgbe_mac_e610)
		return -EOPNOTSUPP;

	err = ixgbe_get_pending_updates(adapter, &pending, extack);
	if (err)
		return err;

	/* Pending is a bitmask of which flash banks have a pending update,
	 * including the main NVM bank, the Option ROM bank, and the netlist
	 * bank. If any of these bits are set, then there is a pending update
	 * waiting to be activated.
	 */
	if (!pending) {
		NL_SET_ERR_MSG_MOD(extack, "No pending firmware update");
		return -ECANCELED;
	}

	if (adapter->fw_emp_reset_disabled) {
		NL_SET_ERR_MSG_MOD(extack,
				   "EMP reset is not available. To activate firmware, a reboot or power cycle is needed");
		return -ECANCELED;
	}

	err = ixgbe_aci_nvm_update_empr(hw);
	if (err)
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to trigger EMP device reset to reload firmware");

	return err;
}

/*Wait for 10 sec with 0.5 sec tic. EMPR takes no less than half of a sec */
#define IXGBE_DEVLINK_RELOAD_TIMEOUT_SEC	20

/**
 * ixgbe_devlink_reload_empr_finish - finishes EMP reset
 * @devlink: pointer to the devlink instance
 * @action: the action to perform.
 * @limit: limits on what reload should do
 * @actions_performed: actions performed
 * @extack: netlink extended ACK structure
 *
 * Wait for new NVM to be loaded during EMP reset.
 *
 * Return: -ETIME when timer is exceeded, 0 on success.
 */
static int ixgbe_devlink_reload_empr_finish(struct devlink *devlink,
					    enum devlink_reload_action action,
					    enum devlink_reload_limit limit,
					    u32 *actions_performed,
					    struct netlink_ext_ack *extack)
{
	struct ixgbe_adapter *adapter = devlink_priv(devlink);
	struct ixgbe_hw *hw = &adapter->hw;
	int i = 0;
	u32 fwsm;

	do {
		/* Just right away after triggering EMP reset the FWSM register
		 * may be not cleared yet, so begin the loop with the delay
		 * in order to not check the not updated register.
		 */
		mdelay(500);

		fwsm = IXGBE_READ_REG(hw, IXGBE_FWSM(hw));

		if (i++ >= IXGBE_DEVLINK_RELOAD_TIMEOUT_SEC)
			return -ETIME;

	} while (!(fwsm & IXGBE_FWSM_FW_VAL_BIT));

	*actions_performed = BIT(DEVLINK_RELOAD_ACTION_FW_ACTIVATE);

	adapter->flags2 &= ~(IXGBE_FLAG2_API_MISMATCH |
			     IXGBE_FLAG2_FW_ROLLBACK);

	return 0;
}

static const struct devlink_ops ixgbe_devlink_ops = {
	.info_get = ixgbe_devlink_info_get,
	.supported_flash_update_params =
		DEVLINK_SUPPORT_FLASH_UPDATE_OVERWRITE_MASK,
	.flash_update = ixgbe_flash_pldm_image,
	.reload_actions = BIT(DEVLINK_RELOAD_ACTION_FW_ACTIVATE),
	.reload_down = ixgbe_devlink_reload_empr_start,
	.reload_up = ixgbe_devlink_reload_empr_finish,
};

/**
 * ixgbe_allocate_devlink - Allocate devlink instance
 * @dev: device to allocate devlink for
 *
 * Allocate a devlink instance for this physical function.
 *
 * Return: pointer to the device adapter structure on success,
 * ERR_PTR(-ENOMEM) when allocation failed.
 */
struct ixgbe_adapter *ixgbe_allocate_devlink(struct device *dev)
{
	struct ixgbe_adapter *adapter;
	struct devlink *devlink;

	devlink = devlink_alloc(&ixgbe_devlink_ops, sizeof(*adapter), dev);
	if (!devlink)
		return ERR_PTR(-ENOMEM);

	adapter = devlink_priv(devlink);
	adapter->devlink = devlink;

	return adapter;
}

/**
 * ixgbe_devlink_set_switch_id - Set unique switch ID based on PCI DSN
 * @adapter: pointer to the device adapter structure
 * @ppid: struct with switch id information
 */
static void ixgbe_devlink_set_switch_id(struct ixgbe_adapter *adapter,
					struct netdev_phys_item_id *ppid)
{
	u64 id = pci_get_dsn(adapter->pdev);

	ppid->id_len = sizeof(id);
	put_unaligned_be64(id, &ppid->id);
}

/**
 * ixgbe_devlink_register_port - Register devlink port
 * @adapter: pointer to the device adapter structure
 *
 * Create and register a devlink_port for this physical function.
 *
 * Return: 0 on success, error code on failure.
 */
int ixgbe_devlink_register_port(struct ixgbe_adapter *adapter)
{
	struct devlink_port *devlink_port = &adapter->devlink_port;
	struct devlink *devlink = adapter->devlink;
	struct device *dev = &adapter->pdev->dev;
	struct devlink_port_attrs attrs = {};
	int err;

	attrs.flavour = DEVLINK_PORT_FLAVOUR_PHYSICAL;
	attrs.phys.port_number = adapter->hw.bus.func;
	attrs.no_phys_port_name = 1;
	ixgbe_devlink_set_switch_id(adapter, &attrs.switch_id);

	devlink_port_attrs_set(devlink_port, &attrs);

	err = devl_port_register(devlink, devlink_port, 0);
	if (err) {
		dev_err(dev,
			"devlink port registration failed, err %d\n", err);
	}

	return err;
}
