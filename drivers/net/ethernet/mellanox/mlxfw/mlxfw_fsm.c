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

static const int mlxfw_fsm_state_errno[] = {
	[MLXFW_FSM_STATE_ERR_ERROR] = -EIO,
	[MLXFW_FSM_STATE_ERR_REJECTED_DIGEST_ERR] = -EBADMSG,
	[MLXFW_FSM_STATE_ERR_REJECTED_NOT_APPLICABLE] = -ENOENT,
	[MLXFW_FSM_STATE_ERR_REJECTED_UNKNOWN_KEY] = -ENOKEY,
	[MLXFW_FSM_STATE_ERR_REJECTED_AUTH_FAILED] = -EACCES,
	[MLXFW_FSM_STATE_ERR_REJECTED_UNSIGNED] = -EKEYREVOKED,
	[MLXFW_FSM_STATE_ERR_REJECTED_KEY_NOT_APPLICABLE] = -EKEYREJECTED,
	[MLXFW_FSM_STATE_ERR_REJECTED_BAD_FORMAT] = -ENOEXEC,
	[MLXFW_FSM_STATE_ERR_BLOCKED_PENDING_RESET] = -EBUSY,
	[MLXFW_FSM_STATE_ERR_MAX] = -EINVAL
};

#define MLXFW_ERR_PRFX "Firmware flash failed: "
#define MLXFW_ERR_MSG(fwdev, extack, msg, err) do { \
	mlxfw_err(fwdev, "%s, err (%d)\n", MLXFW_ERR_PRFX msg, err); \
	NL_SET_ERR_MSG_MOD(extack, MLXFW_ERR_PRFX msg); \
} while (0)

static int mlxfw_fsm_state_err(struct mlxfw_dev *mlxfw_dev,
			       struct netlink_ext_ack *extack,
			       enum mlxfw_fsm_state_err err)
{
	enum mlxfw_fsm_state_err fsm_state_err;

	fsm_state_err = min_t(enum mlxfw_fsm_state_err, err,
			      MLXFW_FSM_STATE_ERR_MAX);

	switch (fsm_state_err) {
	case MLXFW_FSM_STATE_ERR_ERROR:
		MLXFW_ERR_MSG(mlxfw_dev, extack, "general error", err);
		break;
	case MLXFW_FSM_STATE_ERR_REJECTED_DIGEST_ERR:
		MLXFW_ERR_MSG(mlxfw_dev, extack, "component hash mismatch", err);
		break;
	case MLXFW_FSM_STATE_ERR_REJECTED_NOT_APPLICABLE:
		MLXFW_ERR_MSG(mlxfw_dev, extack, "component not applicable", err);
		break;
	case MLXFW_FSM_STATE_ERR_REJECTED_UNKNOWN_KEY:
		MLXFW_ERR_MSG(mlxfw_dev, extack, "unknown key", err);
		break;
	case MLXFW_FSM_STATE_ERR_REJECTED_AUTH_FAILED:
		MLXFW_ERR_MSG(mlxfw_dev, extack, "authentication failed", err);
		break;
	case MLXFW_FSM_STATE_ERR_REJECTED_UNSIGNED:
		MLXFW_ERR_MSG(mlxfw_dev, extack, "component was not signed", err);
		break;
	case MLXFW_FSM_STATE_ERR_REJECTED_KEY_NOT_APPLICABLE:
		MLXFW_ERR_MSG(mlxfw_dev, extack, "key not applicable", err);
		break;
	case MLXFW_FSM_STATE_ERR_REJECTED_BAD_FORMAT:
		MLXFW_ERR_MSG(mlxfw_dev, extack, "bad format", err);
		break;
	case MLXFW_FSM_STATE_ERR_BLOCKED_PENDING_RESET:
		MLXFW_ERR_MSG(mlxfw_dev, extack, "pending reset", err);
		break;
	case MLXFW_FSM_STATE_ERR_OK:
	case MLXFW_FSM_STATE_ERR_MAX:
		MLXFW_ERR_MSG(mlxfw_dev, extack, "unknown error", err);
		break;
	}

	return mlxfw_fsm_state_errno[fsm_state_err];
};

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
	if (err) {
		MLXFW_ERR_MSG(mlxfw_dev, extack, "FSM state query failed", err);
		return err;
	}

	if (fsm_state_err != MLXFW_FSM_STATE_ERR_OK)
		return mlxfw_fsm_state_err(mlxfw_dev, extack, fsm_state_err);

	if (curr_fsm_state != fsm_state) {
		if (--times == 0) {
			MLXFW_ERR_MSG(mlxfw_dev, extack,
				      "Timeout reached on FSM state change", -ETIMEDOUT);
			return -ETIMEDOUT;
		}
		msleep(MLXFW_FSM_STATE_WAIT_CYCLE_MS);
		goto retry;
	}
	return 0;
}

static int
mlxfw_fsm_reactivate_err(struct mlxfw_dev *mlxfw_dev,
			 struct netlink_ext_ack *extack, u8 err)
{
	enum mlxfw_fsm_reactivate_status status;

#define MXFW_REACT_PRFX "Reactivate FSM: "
#define MLXFW_REACT_ERR(msg, err) \
	MLXFW_ERR_MSG(mlxfw_dev, extack, MXFW_REACT_PRFX msg, err)

	status = min_t(enum mlxfw_fsm_reactivate_status, err,
		       MLXFW_FSM_REACTIVATE_STATUS_MAX);

	switch (status) {
	case MLXFW_FSM_REACTIVATE_STATUS_BUSY:
		MLXFW_REACT_ERR("busy", err);
		break;
	case MLXFW_FSM_REACTIVATE_STATUS_PROHIBITED_FW_VER_ERR:
		MLXFW_REACT_ERR("prohibited fw ver", err);
		break;
	case MLXFW_FSM_REACTIVATE_STATUS_FIRST_PAGE_COPY_FAILED:
		MLXFW_REACT_ERR("first page copy failed", err);
		break;
	case MLXFW_FSM_REACTIVATE_STATUS_FIRST_PAGE_ERASE_FAILED:
		MLXFW_REACT_ERR("first page erase failed", err);
		break;
	case MLXFW_FSM_REACTIVATE_STATUS_FIRST_PAGE_RESTORE_FAILED:
		MLXFW_REACT_ERR("first page restore failed", err);
		break;
	case MLXFW_FSM_REACTIVATE_STATUS_CANDIDATE_FW_DEACTIVATION_FAILED:
		MLXFW_REACT_ERR("candidate fw deactivation failed", err);
		break;
	case MLXFW_FSM_REACTIVATE_STATUS_ERR_DEVICE_RESET_REQUIRED:
		MLXFW_REACT_ERR("device reset required", err);
		break;
	case MLXFW_FSM_REACTIVATE_STATUS_ERR_FW_PROGRAMMING_NEEDED:
		MLXFW_REACT_ERR("fw programming needed", err);
		break;
	case MLXFW_FSM_REACTIVATE_STATUS_FW_ALREADY_ACTIVATED:
		MLXFW_REACT_ERR("fw already activated", err);
		break;
	case MLXFW_FSM_REACTIVATE_STATUS_OK:
	case MLXFW_FSM_REACTIVATE_STATUS_MAX:
		MLXFW_REACT_ERR("unexpected error", err);
		break;
	}
	return -EREMOTEIO;
};

static int mlxfw_fsm_reactivate(struct mlxfw_dev *mlxfw_dev,
				struct netlink_ext_ack *extack,
				bool *supported)
{
	u8 status;
	int err;

	if (!mlxfw_dev->ops->fsm_reactivate)
		return 0;

	err = mlxfw_dev->ops->fsm_reactivate(mlxfw_dev, &status);
	if (err == -EOPNOTSUPP) {
		*supported = false;
		return 0;
	}

	if (err) {
		MLXFW_ERR_MSG(mlxfw_dev, extack,
			      "Could not reactivate firmware flash", err);
		return err;
	}

	if (status == MLXFW_FSM_REACTIVATE_STATUS_OK ||
	    status == MLXFW_FSM_REACTIVATE_STATUS_FW_ALREADY_ACTIVATED)
		return 0;

	return mlxfw_fsm_reactivate_err(mlxfw_dev, extack, status);
}

static void mlxfw_status_notify(struct mlxfw_dev *mlxfw_dev,
				const char *msg, const char *comp_name,
				u32 done_bytes, u32 total_bytes)
{
	devlink_flash_update_status_notify(mlxfw_dev->devlink, msg, comp_name,
					   done_bytes, total_bytes);
}

#define MLXFW_ALIGN_DOWN(x, align_bits) ((x) & ~((1 << (align_bits)) - 1))
#define MLXFW_ALIGN_UP(x, align_bits) \
		MLXFW_ALIGN_DOWN((x) + ((1 << (align_bits)) - 1), (align_bits))

static int mlxfw_flash_component(struct mlxfw_dev *mlxfw_dev,
				 u32 fwhandle,
				 struct mlxfw_mfa2_component *comp,
				 bool reactivate_supp,
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
	if (err) {
		MLXFW_ERR_MSG(mlxfw_dev, extack, "FSM component query failed", err);
		return err;
	}

	comp_max_size = min_t(u32, comp_max_size, MLXFW_FSM_MAX_COMPONENT_SIZE);
	if (comp->data_size > comp_max_size) {
		MLXFW_ERR_MSG(mlxfw_dev, extack,
			      "Component size is bigger than limit", -EINVAL);
		return -EINVAL;
	}

	comp_max_write_size = MLXFW_ALIGN_DOWN(comp_max_write_size,
					       comp_align_bits);

	mlxfw_dbg(mlxfw_dev, "Component update\n");
	mlxfw_status_notify(mlxfw_dev, "Updating component", comp_name, 0, 0);
	err = mlxfw_dev->ops->fsm_component_update(mlxfw_dev, fwhandle,
						   comp->index,
						   comp->data_size);
	if (err) {
		if (!reactivate_supp)
			MLXFW_ERR_MSG(mlxfw_dev, extack,
				      "FSM component update failed, FW reactivate is not supported",
				      err);
		else
			MLXFW_ERR_MSG(mlxfw_dev, extack,
				      "FSM component update failed", err);
		return err;
	}

	err = mlxfw_fsm_state_wait(mlxfw_dev, fwhandle,
				   MLXFW_FSM_STATE_DOWNLOAD, extack);
	if (err)
		goto err_out;

	mlxfw_dbg(mlxfw_dev, "Component download\n");
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
		if (err) {
			MLXFW_ERR_MSG(mlxfw_dev, extack,
				      "Component download failed", err);
			goto err_out;
		}
		mlxfw_status_notify(mlxfw_dev, "Downloading component",
				    comp_name, offset + block_size,
				    comp->data_size);
	}

	mlxfw_dbg(mlxfw_dev, "Component verify\n");
	mlxfw_status_notify(mlxfw_dev, "Verifying component", comp_name, 0, 0);
	err = mlxfw_dev->ops->fsm_component_verify(mlxfw_dev, fwhandle,
						   comp->index);
	if (err) {
		MLXFW_ERR_MSG(mlxfw_dev, extack,
			      "FSM component verify failed", err);
		goto err_out;
	}

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
				  bool reactivate_supp,
				  struct netlink_ext_ack *extack)
{
	u32 component_count;
	int err;
	int i;

	err = mlxfw_mfa2_file_component_count(mfa2_file, mlxfw_dev->psid,
					      mlxfw_dev->psid_size,
					      &component_count);
	if (err) {
		MLXFW_ERR_MSG(mlxfw_dev, extack,
			      "Could not find device PSID in MFA2 file", err);
		return err;
	}

	for (i = 0; i < component_count; i++) {
		struct mlxfw_mfa2_component *comp;

		comp = mlxfw_mfa2_file_component_get(mfa2_file, mlxfw_dev->psid,
						     mlxfw_dev->psid_size, i);
		if (IS_ERR(comp)) {
			err = PTR_ERR(comp);
			MLXFW_ERR_MSG(mlxfw_dev, extack,
				      "Failed to get MFA2 component", err);
			return err;
		}

		mlxfw_info(mlxfw_dev, "Flashing component type %d\n",
			   comp->index);
		err = mlxfw_flash_component(mlxfw_dev, fwhandle, comp,
					    reactivate_supp, extack);
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
	bool reactivate_supp = true;
	u32 fwhandle;
	int err;

	if (!mlxfw_mfa2_check(firmware)) {
		MLXFW_ERR_MSG(mlxfw_dev, extack,
			      "Firmware file is not MFA2", -EINVAL);
		return -EINVAL;
	}

	mfa2_file = mlxfw_mfa2_file_init(firmware);
	if (IS_ERR(mfa2_file)) {
		err = PTR_ERR(mfa2_file);
		MLXFW_ERR_MSG(mlxfw_dev, extack,
			      "Failed to initialize MFA2 firmware file", err);
		return err;
	}

	mlxfw_info(mlxfw_dev, "Initialize firmware flash process\n");
	devlink_flash_update_begin_notify(mlxfw_dev->devlink);
	mlxfw_status_notify(mlxfw_dev, "Initializing firmware flash process",
			    NULL, 0, 0);
	err = mlxfw_dev->ops->fsm_lock(mlxfw_dev, &fwhandle);
	if (err) {
		MLXFW_ERR_MSG(mlxfw_dev, extack,
			      "Could not lock the firmware FSM", err);
		goto err_fsm_lock;
	}

	err = mlxfw_fsm_state_wait(mlxfw_dev, fwhandle,
				   MLXFW_FSM_STATE_LOCKED, extack);
	if (err)
		goto err_state_wait_idle_to_locked;

	err = mlxfw_fsm_reactivate(mlxfw_dev, extack, &reactivate_supp);
	if (err)
		goto err_fsm_reactivate;

	err = mlxfw_fsm_state_wait(mlxfw_dev, fwhandle,
				   MLXFW_FSM_STATE_LOCKED, extack);
	if (err)
		goto err_state_wait_reactivate_to_locked;

	err = mlxfw_flash_components(mlxfw_dev, fwhandle, mfa2_file,
				     reactivate_supp, extack);
	if (err)
		goto err_flash_components;

	mlxfw_dbg(mlxfw_dev, "Activate image\n");
	mlxfw_status_notify(mlxfw_dev, "Activating image", NULL, 0, 0);
	err = mlxfw_dev->ops->fsm_activate(mlxfw_dev, fwhandle);
	if (err) {
		MLXFW_ERR_MSG(mlxfw_dev, extack,
			      "Could not activate the downloaded image", err);
		goto err_fsm_activate;
	}

	err = mlxfw_fsm_state_wait(mlxfw_dev, fwhandle,
				   MLXFW_FSM_STATE_LOCKED, extack);
	if (err)
		goto err_state_wait_activate_to_locked;

	mlxfw_dbg(mlxfw_dev, "Handle release\n");
	mlxfw_dev->ops->fsm_release(mlxfw_dev, fwhandle);

	mlxfw_info(mlxfw_dev, "Firmware flash done\n");
	mlxfw_status_notify(mlxfw_dev, "Firmware flash done", NULL, 0, 0);
	mlxfw_mfa2_file_fini(mfa2_file);
	devlink_flash_update_end_notify(mlxfw_dev->devlink);
	return 0;

err_state_wait_activate_to_locked:
err_fsm_activate:
err_flash_components:
err_state_wait_reactivate_to_locked:
err_fsm_reactivate:
err_state_wait_idle_to_locked:
	mlxfw_dev->ops->fsm_release(mlxfw_dev, fwhandle);
err_fsm_lock:
	mlxfw_mfa2_file_fini(mfa2_file);
	devlink_flash_update_end_notify(mlxfw_dev->devlink);
	return err;
}
EXPORT_SYMBOL(mlxfw_firmware_flash);

MODULE_LICENSE("Dual BSD/GPL");
MODULE_AUTHOR("Yotam Gigi <yotamg@mellanox.com>");
MODULE_DESCRIPTION("Mellanox firmware flash lib");
