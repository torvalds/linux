// SPDX-License-Identifier: GPL-2.0
/* Copyright (c) 2025, Intel Corporation. */

#include "ixgbe.h"
#include "devlink.h"

#define IXGBE_DEVLINK_READ_BLK_SIZE (1024 * 1024)

static const struct devlink_region_ops ixgbe_nvm_region_ops;
static const struct devlink_region_ops ixgbe_sram_region_ops;

static int ixgbe_devlink_parse_region(struct ixgbe_hw *hw,
				      const struct devlink_region_ops *ops,
				      bool *read_shadow_ram, u32 *nvm_size)
{
	if (ops == &ixgbe_nvm_region_ops) {
		*read_shadow_ram = false;
		*nvm_size = hw->flash.flash_size;
	} else if (ops == &ixgbe_sram_region_ops) {
		*read_shadow_ram = true;
		*nvm_size = hw->flash.sr_words * 2u;
	} else {
		return -EOPNOTSUPP;
	}

	return 0;
}

/**
 * ixgbe_devlink_nvm_snapshot - Capture a snapshot of the NVM content
 * @devlink: the devlink instance
 * @ops: the devlink region being snapshotted
 * @extack: extended ACK response structure
 * @data: on exit points to snapshot data buffer
 *
 * This function is called in response to the DEVLINK_CMD_REGION_NEW cmd.
 *
 * Capture a snapshot of the whole requested NVM region.
 *
 * No need to worry with freeing @data, devlink core takes care if it.
 *
 * Return: 0 on success, -EOPNOTSUPP for unsupported regions, -EBUSY when
 * cannot lock NVM, -ENOMEM when cannot alloc mem and -EIO when error
 * occurs during reading.
 */
static int ixgbe_devlink_nvm_snapshot(struct devlink *devlink,
				      const struct devlink_region_ops *ops,
				      struct netlink_ext_ack *extack, u8 **data)
{
	struct ixgbe_adapter *adapter = devlink_priv(devlink);
	struct ixgbe_hw *hw = &adapter->hw;
	bool read_shadow_ram;
	u8 *nvm_data, *buf;
	u32 nvm_size, left;
	u8 num_blks;
	int err;

	err = ixgbe_devlink_parse_region(hw, ops, &read_shadow_ram, &nvm_size);
	if (err)
		return err;

	nvm_data = kvzalloc(nvm_size, GFP_KERNEL);
	if (!nvm_data)
		return -ENOMEM;

	num_blks = DIV_ROUND_UP(nvm_size, IXGBE_DEVLINK_READ_BLK_SIZE);
	buf = nvm_data;
	left = nvm_size;

	for (int i = 0; i < num_blks; i++) {
		u32 read_sz = min_t(u32, IXGBE_DEVLINK_READ_BLK_SIZE, left);

		/* Need to acquire NVM lock during each loop run because the
		 * total period of reading whole NVM is longer than the maximum
		 * period the lock can be taken defined by the IXGBE_NVM_TIMEOUT.
		 */
		err = ixgbe_acquire_nvm(hw, LIBIE_AQC_RES_ACCESS_READ);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Failed to acquire NVM semaphore");
			kvfree(nvm_data);
			return -EBUSY;
		}

		err = ixgbe_read_flat_nvm(hw, i * IXGBE_DEVLINK_READ_BLK_SIZE,
					  &read_sz, buf, read_shadow_ram);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack,
					   "Failed to read RAM content");
			ixgbe_release_nvm(hw);
			kvfree(nvm_data);
			return -EIO;
		}

		ixgbe_release_nvm(hw);

		buf += read_sz;
		left -= read_sz;
	}

	*data = nvm_data;
	return 0;
}

/**
 * ixgbe_devlink_devcaps_snapshot - Capture a snapshot of device capabilities
 * @devlink: the devlink instance
 * @ops: the devlink region being snapshotted
 * @extack: extended ACK response structure
 * @data: on exit points to snapshot data buffer
 *
 * This function is called in response to the DEVLINK_CMD_REGION_NEW for
 * the device-caps devlink region.
 *
 * Capture a snapshot of the device capabilities reported by firmware.
 *
 * No need to worry with freeing @data, devlink core takes care if it.
 *
 * Return: 0 on success, -ENOMEM when cannot alloc mem, or return code of
 * the reading operation.
 */
static int ixgbe_devlink_devcaps_snapshot(struct devlink *devlink,
					  const struct devlink_region_ops *ops,
					  struct netlink_ext_ack *extack,
					  u8 **data)
{
	struct ixgbe_adapter *adapter = devlink_priv(devlink);
	struct ixgbe_aci_cmd_list_caps_elem *caps;
	struct ixgbe_hw *hw = &adapter->hw;
	int err;

	caps = kvzalloc(IXGBE_ACI_MAX_BUFFER_SIZE, GFP_KERNEL);
	if (!caps)
		return -ENOMEM;

	err = ixgbe_aci_list_caps(hw, caps, IXGBE_ACI_MAX_BUFFER_SIZE, NULL,
				  ixgbe_aci_opc_list_dev_caps);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to read device capabilities");
		kvfree(caps);
		return err;
	}

	*data = (u8 *)caps;
	return 0;
}

/**
 * ixgbe_devlink_nvm_read - Read a portion of NVM flash content
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
 * Read from either the nvm-flash region either shadow-ram region.
 *
 * Return: 0 on success, -EOPNOTSUPP for unsupported regions, -EBUSY when
 * cannot lock NVM, -ERANGE when buffer limit exceeded and -EIO when error
 * occurs during reading.
 */
static int ixgbe_devlink_nvm_read(struct devlink *devlink,
				  const struct devlink_region_ops *ops,
				  struct netlink_ext_ack *extack,
				  u64 offset, u32 size, u8 *data)
{
	struct ixgbe_adapter *adapter = devlink_priv(devlink);
	struct ixgbe_hw *hw = &adapter->hw;
	bool read_shadow_ram;
	u32 nvm_size;
	int err;

	err = ixgbe_devlink_parse_region(hw, ops, &read_shadow_ram, &nvm_size);
	if (err)
		return err;

	if (offset + size > nvm_size) {
		NL_SET_ERR_MSG_MOD(extack, "Cannot read beyond the region size");
		return -ERANGE;
	}

	err = ixgbe_acquire_nvm(hw, LIBIE_AQC_RES_ACCESS_READ);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to acquire NVM semaphore");
		return -EBUSY;
	}

	err = ixgbe_read_flat_nvm(hw, (u32)offset, &size, data, read_shadow_ram);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to read NVM contents");
		ixgbe_release_nvm(hw);
		return -EIO;
	}

	ixgbe_release_nvm(hw);
	return 0;
}

static const struct devlink_region_ops ixgbe_nvm_region_ops = {
	.name = "nvm-flash",
	.destructor = kvfree,
	.snapshot = ixgbe_devlink_nvm_snapshot,
	.read = ixgbe_devlink_nvm_read,
};

static const struct devlink_region_ops ixgbe_sram_region_ops = {
	.name = "shadow-ram",
	.destructor = kvfree,
	.snapshot = ixgbe_devlink_nvm_snapshot,
	.read = ixgbe_devlink_nvm_read,
};

static const struct devlink_region_ops ixgbe_devcaps_region_ops = {
	.name = "device-caps",
	.destructor = kvfree,
	.snapshot = ixgbe_devlink_devcaps_snapshot,
};

/**
 * ixgbe_devlink_init_regions - Initialize devlink regions
 * @adapter: adapter instance
 *
 * Create devlink regions used to enable access to dump the contents of the
 * flash memory of the device.
 */
void ixgbe_devlink_init_regions(struct ixgbe_adapter *adapter)
{
	struct devlink *devlink = adapter->devlink;
	struct device *dev = &adapter->pdev->dev;
	u64 nvm_size, sram_size;

	if (adapter->hw.mac.type != ixgbe_mac_e610)
		return;

	nvm_size = adapter->hw.flash.flash_size;
	adapter->nvm_region = devl_region_create(devlink, &ixgbe_nvm_region_ops,
						 1, nvm_size);
	if (IS_ERR(adapter->nvm_region)) {
		dev_err(dev,
			"Failed to create NVM devlink region, err %ld\n",
			PTR_ERR(adapter->nvm_region));
		adapter->nvm_region = NULL;
	}

	sram_size = adapter->hw.flash.sr_words * 2u;
	adapter->sram_region = devl_region_create(devlink, &ixgbe_sram_region_ops,
						  1, sram_size);
	if (IS_ERR(adapter->sram_region)) {
		dev_err(dev,
			"Failed to create shadow-ram devlink region, err %ld\n",
			PTR_ERR(adapter->sram_region));
		adapter->sram_region = NULL;
	}

	adapter->devcaps_region = devl_region_create(devlink,
						     &ixgbe_devcaps_region_ops,
						     10, IXGBE_ACI_MAX_BUFFER_SIZE);
	if (IS_ERR(adapter->devcaps_region)) {
		dev_err(dev,
			"Failed to create device-caps devlink region, err %ld\n",
			PTR_ERR(adapter->devcaps_region));
		adapter->devcaps_region = NULL;
	}
}

/**
 * ixgbe_devlink_destroy_regions - Destroy devlink regions
 * @adapter: adapter instance
 *
 * Remove previously created regions for this adapter instance.
 */
void ixgbe_devlink_destroy_regions(struct ixgbe_adapter *adapter)
{
	if (adapter->hw.mac.type != ixgbe_mac_e610)
		return;

	if (adapter->nvm_region)
		devl_region_destroy(adapter->nvm_region);

	if (adapter->sram_region)
		devl_region_destroy(adapter->sram_region);

	if (adapter->devcaps_region)
		devl_region_destroy(adapter->devcaps_region);
}
