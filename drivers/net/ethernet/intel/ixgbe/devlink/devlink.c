// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025, Intel Corporation. */

#include "ixgbe.h"
#include "devlink.h"

struct ixgbe_info_ctx {
	char buf[128];
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
				struct ixgbe_info_ctx *ctx)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_nvm_version nvm_ver;

	ctx->buf[0] = '\0';

	if (hw->mac.type == ixgbe_mac_e610) {
		struct ixgbe_orom_info *orom = &adapter->hw.flash.orom;

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
			       struct ixgbe_info_ctx *ctx)
{
	struct ixgbe_hw *hw = &adapter->hw;
	struct ixgbe_nvm_version nvm_ver;

	if (hw->mac.type == ixgbe_mac_e610) {
		snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x",
			 hw->flash.nvm.eetrack);
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
			       struct ixgbe_info_ctx *ctx)
{
	struct ixgbe_nvm_info *nvm = &adapter->hw.flash.nvm;

	snprintf(ctx->buf, sizeof(ctx->buf), "%u", nvm->srev);
}

static void ixgbe_info_orom_srev(struct ixgbe_adapter *adapter,
				 struct ixgbe_info_ctx *ctx)
{
	struct ixgbe_orom_info *orom = &adapter->hw.flash.orom;

	snprintf(ctx->buf, sizeof(ctx->buf), "%u", orom->srev);
}

static void ixgbe_info_nvm_ver(struct ixgbe_adapter *adapter,
			       struct ixgbe_info_ctx *ctx)
{
	struct ixgbe_nvm_info *nvm = &adapter->hw.flash.nvm;

	snprintf(ctx->buf, sizeof(ctx->buf), "%x.%02x", nvm->major, nvm->minor);
}

static void ixgbe_info_netlist_ver(struct ixgbe_adapter *adapter,
				   struct ixgbe_info_ctx *ctx)
{
	struct ixgbe_netlist_info *netlist = &adapter->hw.flash.netlist;

	/* The netlist version fields are BCD formatted */
	snprintf(ctx->buf, sizeof(ctx->buf), "%x.%x.%x-%x.%x.%x",
		 netlist->major, netlist->minor,
		 netlist->type >> 16, netlist->type & 0xFFFF,
		 netlist->rev, netlist->cust_ver);
}

static void ixgbe_info_netlist_build(struct ixgbe_adapter *adapter,
				     struct ixgbe_info_ctx *ctx)
{
	struct ixgbe_netlist_info *netlist = &adapter->hw.flash.netlist;

	snprintf(ctx->buf, sizeof(ctx->buf), "0x%08x", netlist->hash);
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

	ixgbe_info_fw_srev(adapter, ctx);
	err = devlink_info_version_running_put(req, "fw.mgmt.srev", ctx->buf);
	if (err)
		return err;

	ixgbe_info_orom_srev(adapter, ctx);
	err = devlink_info_version_running_put(req, "fw.undi.srev", ctx->buf);
	if (err)
		return err;

	ixgbe_info_nvm_ver(adapter, ctx);
	err = devlink_info_version_running_put(req, "fw.psid.api", ctx->buf);
	if (err)
		return err;

	ixgbe_info_netlist_ver(adapter, ctx);
	err = devlink_info_version_running_put(req, "fw.netlist", ctx->buf);
	if (err)
		return err;

	ixgbe_info_netlist_build(adapter, ctx);
	return devlink_info_version_running_put(req, "fw.netlist.build",
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

	ixgbe_info_orom_ver(adapter, ctx);
	err = devlink_info_version_running_put(req,
					       DEVLINK_INFO_VERSION_GENERIC_FW_UNDI,
					       ctx->buf);
	if (err)
		goto free_ctx;

	ixgbe_info_eetrack(adapter, ctx);
	err = devlink_info_version_running_put(req,
					       DEVLINK_INFO_VERSION_GENERIC_FW_BUNDLE_ID,
					       ctx->buf);
	if (err || hw->mac.type != ixgbe_mac_e610)
		goto free_ctx;

	err = ixgbe_devlink_info_get_e610(adapter, req, ctx);
free_ctx:
	kfree(ctx);
	return err;
}

static const struct devlink_ops ixgbe_devlink_ops = {
	.info_get = ixgbe_devlink_info_get,
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
	ixgbe_devlink_set_switch_id(adapter, &attrs.switch_id);

	devlink_port_attrs_set(devlink_port, &attrs);

	err = devl_port_register(devlink, devlink_port, 0);
	if (err) {
		dev_err(dev,
			"devlink port registration failed, err %d\n", err);
	}

	return err;
}
