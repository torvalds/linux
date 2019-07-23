// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2017-2019 Mellanox Technologies. All rights reserved */

#define pr_fmt(fmt) "mlxfw: " fmt

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/delay.h>

#include "mlxfw.h"
#include "mlxfw_mfa2.h"

#define MLXFW_FSM_STATE_WAIT_CYCLE_MS 200
#define MLXFW_FSM_STATE_WAIT_TIMEOUT_MS 30000
#define MLXFW_FSM_STATE_WAIT_ROUNDS \
	(MLXFW_FSM_STATE_WAIT_TIMEOUT_MS / MLXFW_FSM_STATE_WAIT_CYCLE_MS)
#define MLXFW_FSM_MAX_COMPONENT_SIZE (10 * (1 << 20))

static const char * const mlxfw_fsm_state_err_str[] = {
	[MLXFW_FSM_STATE_ERR_ERROR] =
		"general error",
	[MLXFW_FSM_STATE_ERR_REJECTED_DIGEST_ERR] =
		"component hash mismatch",
	[MLXFW_FSM_STATE_ERR_REJECTED_NOT_APPLICABLE] =
		"component not applicable",
	[MLXFW_FSM_STATE_ERR_REJECTED_UNKNOWN_KEY] =
		"unknown key",
	[MLXFW_FSM_STATE_ERR_REJECTED_AUTH_FAILED] =
		"authentication failed",
	[MLXFW_FSM_STATE_ERR_REJECTED_UNSIGNED] =
		"component was not signed",
	[MLXFW_FSM_STATE_ERR_REJECTED_KEY_NOT_APPLICABLE] =
		"key not applicable",
	[MLXFW_FSM_STATE_ERR_REJECTED_BAD_FORMAT] =
		"bad format",
	[MLXFW_FSM_STATE_ERR_BLOCKED_PENDING_RESET] =
		"pending reset",
	[MLXFW_FSM_STATE_ERR_MAX] =
		"unknown error"
};

static void mlxfw_status_notify(struct mlxfw_dev *mlxfw_dev,
				const char *msg, const char *comp_name,
				u32 done_bytes, u32 total_bytes)
{
	if (!mlxfw_dev->ops->status_notify)
		return;
	mlxfw_dev->ops->status_notify(mlxfw_dev, msg, comp_name,
				      done_bytes, total_bytes);
}

static int mlxfw_fsm_state_wait(struct mlxfw_dev *mlxfw_dev, u32 fwhandle,
				enum mlxfw_fsm_state fsm_state,
				struct netlink_ext_ack *extack)
{
	enum mlxfw_fsm_state_err fsm_state_err;
	enum mlxfw_fsm_state curr_fsm_state;
	int times;
	int err;

	times = MLXFW_FSM_STATE_WAIT_ROUNDS;
retry:
	err = mlxfw_dev->ops->fsm_query_state(mlxfw_dev, fwhandle,
					      &curr_fsm_state, &fsm_state_err);
	if (err)
		return err;

	if (fsm_state_err != MLXFW_FSM_STATE_ERR_OK) {
		pr_err("Firmware flash failed: %s\n",
		       mlxfw_fsm_state_err_str[fsm_state_err]);
		NL_SET_ERR_MSG_MOD(extack, "Firmware flash failed");
		return -EINVAL;
	}
	if (curr_fsm_state != fsm_state) {
		if (--times == 0) {
			pr_err("Timeout reached on FSM state change");
			NL_SET_ERR_MSG_MOD(extack, "Timeout reached on FSM state change");
			return -ETIMEDOUT;
		}
		msleep(MLXFW_FSM_STATE_WAIT_CYCLE_MS);
		goto retry;
	}
	return 0;
}

#define MLXFW_ALIGN_DOWN(x, align_bits) ((x) & ~((1 << (align_bits)) - 1))
#define MLXFW_ALIGN_UP(x, align_bits) \
		MLXFW_ALIGN_DOWN((x) + ((1 << (align_bits)) - 1), (align_bits))

static int mlxfw_flash_component(struct mlxfw_dev *mlxfw_dev,
				 u32 fwhandle,
				 struct mlxfw_mfa2_component *comp,
				 struct netlink_ext_ack *extack)
{
	u16 comp_max_write_size;
	u8 comp_align_bits;
	u32 comp_max_size;
	char comp_name[8];
	u16 block_size;
	u8 *block_ptr;
	u32 offset;
	int err;

	sprintf(comp_name, "%u", comp->index);

	err = mlxfw_dev->ops->component_query(mlxfw_dev, comp->index,
					      &comp_max_size, &comp_align_bits,
					      &comp_max_write_size);
	if (err)
		return err;

	comp_max_size = min_t(u32, comp_max_size, MLXFW_FSM_MAX_COMPONENT_SIZE);
	if (comp->data_size > comp_max_size) {
		pr_err("Component %d is of size %d which is bigger than limit %d\n",
		       comp->index, comp->data_size, comp_max_size);
		NL_SET_ERR_MSG_MOD(extack, "Component is bigger than limit");
		return -EINVAL;
	}

	comp_max_write_size = MLXFW_ALIGN_DOWN(comp_max_write_size,
					       comp_align_bits);

	pr_debug("Component update\n");
	mlxfw_status_notify(mlxfw_dev, "Updating component", comp_name, 0, 0);
	err = mlxfw_dev->ops->fsm_component_update(mlxfw_dev, fwhandle,
						   comp->index,
						   comp->data_size);
	if (err)
		return err;

	err = mlxfw_fsm_state_wait(mlxfw_dev, fwhandle,
				   MLXFW_FSM_STATE_DOWNLOAD, extack);
	if (err)
		goto err_out;

	pr_debug("Component download\n");
	mlxfw_status_notify(mlxfw_dev, "Downloading component",
			    comp_name, 0, comp->data_size);
	for (offset = 0;
	     offset < MLXFW_ALIGN_UP(comp->data_size, comp_align_bits);
	     offset += comp_max_write_size) {
		block_ptr = comp->data + offset;
		block_size = (u16) min_t(u32, comp->data_size - offset,
					 comp_max_write_size);
		err = mlxfw_dev->ops->fsm_block_download(mlxfw_dev, fwhandle,
							 block_ptr, block_size,
							 offset);
		if (err)
			goto err_out;
		mlxfw_status_notify(mlxfw_dev, "Downloading component",
				    comp_name, offset + block_size,
				    comp->data_size);
	}

	pr_debug("Component verify\n");
	mlxfw_status_notify(mlxfw_dev, "Verifying component", comp_name, 0, 0);
	err = mlxfw_dev->ops->fsm_component_verify(mlxfw_dev, fwhandle,
						   comp->index);
	if (err)
		goto err_out;

	err = mlxfw_fsm_state_wait(mlxfw_dev, fwhandle,
				   MLXFW_FSM_STATE_LOCKED, extack);
	if (err)
		goto err_out;
	return 0;

err_out:
	mlxfw_dev->ops->fsm_cancel(mlxfw_dev, fwhandle);
	return err;
}

static int mlxfw_flash_components(struct mlxfw_dev *mlxfw_dev, u32 fwhandle,
				  struct mlxfw_mfa2_file *mfa2_file,
				  struct netlink_ext_ack *extack)
{
	u32 component_count;
	int err;
	int i;

	err = mlxfw_mfa2_file_component_count(mfa2_file, mlxfw_dev->psid,
					      mlxfw_dev->psid_size,
					      &component_count);
	if (err) {
		pr_err("Could not find device PSID in MFA2 file\n");
		NL_SET_ERR_MSG_MOD(extack, "Could not find device PSID in MFA2 file");
		return err;
	}

	for (i = 0; i < component_count; i++) {
		struct mlxfw_mfa2_component *comp;

		comp = mlxfw_mfa2_file_component_get(mfa2_file, mlxfw_dev->psid,
						     mlxfw_dev->psid_size, i);
		if (IS_ERR(comp))
			return PTR_ERR(comp);

		pr_info("Flashing component type %d\n", comp->index);
		err = mlxfw_flash_component(mlxfw_dev, fwhandle, comp, extack);
		mlxfw_mfa2_file_component_put(comp);
		if (err)
			return err;
	}
	return 0;
}

int mlxfw_firmware_flash(struct mlxfw_dev *mlxfw_dev,
			 const struct firmware *firmware,
			 struct netlink_ext_ack *extack)
{
	struct mlxfw_mfa2_file *mfa2_file;
	u32 fwhandle;
	int err;

	if (!mlxfw_mfa2_check(firmware)) {
		pr_err("Firmware file is not MFA2\n");
		NL_SET_ERR_MSG_MOD(extack, "Firmware file is not MFA2");
		return -EINVAL;
	}

	mfa2_file = mlxfw_mfa2_file_init(firmware);
	if (IS_ERR(mfa2_file))
		return PTR_ERR(mfa2_file);

	pr_info("Initialize firmware flash process\n");
	mlxfw_status_notify(mlxfw_dev, "Initializing firmware flash process",
			    NULL, 0, 0);
	err = mlxfw_dev->ops->fsm_lock(mlxfw_dev, &fwhandle);
	if (err) {
		pr_err("Could not lock the firmware FSM\n");
		NL_SET_ERR_MSG_MOD(extack, "Could not lock the firmware FSM");
		goto err_fsm_lock;
	}

	err = mlxfw_fsm_state_wait(mlxfw_dev, fwhandle,
				   MLXFW_FSM_STATE_LOCKED, extack);
	if (err)
		goto err_state_wait_idle_to_locked;

	err = mlxfw_flash_components(mlxfw_dev, fwhandle, mfa2_file, extack);
	if (err)
		goto err_flash_components;

	pr_debug("Activate image\n");
	mlxfw_status_notify(mlxfw_dev, "Activating image", NULL, 0, 0);
	err = mlxfw_dev->ops->fsm_activate(mlxfw_dev, fwhandle);
	if (err) {
		pr_err("Could not activate the downloaded image\n");
		NL_SET_ERR_MSG_MOD(extack, "Could not activate the downloaded image");
		goto err_fsm_activate;
	}

	err = mlxfw_fsm_state_wait(mlxfw_dev, fwhandle,
				   MLXFW_FSM_STATE_LOCKED, extack);
	if (err)
		goto err_state_wait_activate_to_locked;

	pr_debug("Handle release\n");
	mlxfw_dev->ops->fsm_release(mlxfw_dev, fwhandle);

	pr_info("Firmware flash done.\n");
	mlxfw_status_notify(mlxfw_dev, "Firmware flash done", NULL, 0, 0);
	mlxfw_mfa2_file_fini(mfa2_file);
	return 0;

err_state_wait_activate_to_locked:
err_fsm_activate:
err_flash_components:
err_state_wait_idle_to_locked:
	mlxfw_dev->ops->fsm_release(mlxfw_dev, fwhandle);
err_fsm_lock:
	mlxfw_mfa2_file_fini(mfa2_file);
	return err;
}
EXPORT_SYMBOL(mlxfw_firmware_flash);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Yotam Gigi <yotamg@mellanox.com>");
MODULE_DESCRIPTION("Mellanox firmware flash lib");
