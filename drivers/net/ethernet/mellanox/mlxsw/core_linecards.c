// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2022 NVIDIA Corporation and Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/err.h>
#include <linux/types.h>
#include <linux/string.h>
#include <linux/workqueue.h>
#include <linux/gfp.h>
#include <linux/slab.h>
#include <linux/list.h>
#include <linux/vmalloc.h>

#include "core.h"

struct mlxsw_linecard_ini_file {
	__le16 size;
	union {
		u8 data[0];
		struct {
			__be16 hw_revision;
			__be16 ini_version;
			u8 __dontcare[3];
			u8 type;
			u8 name[20];
		} format;
	};
};

struct mlxsw_linecard_types_info {
	struct mlxsw_linecard_ini_file **ini_files;
	unsigned int count;
	size_t data_size;
	char *data;
};

#define MLXSW_LINECARD_STATUS_EVENT_TO (10 * MSEC_PER_SEC)

static void
mlxsw_linecard_status_event_to_schedule(struct mlxsw_linecard *linecard,
					enum mlxsw_linecard_status_event_type status_event_type)
{
	cancel_delayed_work_sync(&linecard->status_event_to_dw);
	linecard->status_event_type_to = status_event_type;
	mlxsw_core_schedule_dw(&linecard->status_event_to_dw,
			       msecs_to_jiffies(MLXSW_LINECARD_STATUS_EVENT_TO));
}

static void
mlxsw_linecard_status_event_done(struct mlxsw_linecard *linecard,
				 enum mlxsw_linecard_status_event_type status_event_type)
{
	if (linecard->status_event_type_to == status_event_type)
		cancel_delayed_work_sync(&linecard->status_event_to_dw);
}

static const char *
mlxsw_linecard_types_lookup(struct mlxsw_linecards *linecards, u8 card_type)
{
	struct mlxsw_linecard_types_info *types_info;
	struct mlxsw_linecard_ini_file *ini_file;
	int i;

	types_info = linecards->types_info;
	if (!types_info)
		return NULL;
	for (i = 0; i < types_info->count; i++) {
		ini_file = linecards->types_info->ini_files[i];
		if (ini_file->format.type == card_type)
			return ini_file->format.name;
	}
	return NULL;
}

static const char *mlxsw_linecard_type_name(struct mlxsw_linecard *linecard)
{
	struct mlxsw_core *mlxsw_core = linecard->linecards->mlxsw_core;
	char mddq_pl[MLXSW_REG_MDDQ_LEN];
	int err;

	mlxsw_reg_mddq_slot_name_pack(mddq_pl, linecard->slot_index);
	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mddq), mddq_pl);
	if (err)
		return ERR_PTR(err);
	mlxsw_reg_mddq_slot_name_unpack(mddq_pl, linecard->name);
	return linecard->name;
}

struct mlxsw_linecard_device {
	struct list_head list;
	u8 index;
	struct mlxsw_linecard *linecard;
	struct devlink_linecard_device *devlink_device;
};

static int mlxsw_linecard_device_attach(struct mlxsw_core *mlxsw_core,
					struct mlxsw_linecard *linecard,
					u8 device_index, bool flash_owner)
{
	struct mlxsw_linecard_device *device;
	int err;

	device = kzalloc(sizeof(*device), GFP_KERNEL);
	if (!device)
		return -ENOMEM;
	device->index = device_index;
	device->linecard = linecard;

	device->devlink_device = devlink_linecard_device_create(linecard->devlink_linecard,
								device_index, NULL);
	if (IS_ERR(device->devlink_device)) {
		err = PTR_ERR(device->devlink_device);
		goto err_devlink_linecard_device_attach;
	}

	list_add_tail(&device->list, &linecard->device_list);
	return 0;

err_devlink_linecard_device_attach:
	kfree(device);
	return err;
}

static void mlxsw_linecard_device_detach(struct mlxsw_core *mlxsw_core,
					 struct mlxsw_linecard *linecard,
					 struct mlxsw_linecard_device *device)
{
	list_del(&device->list);
	devlink_linecard_device_destroy(linecard->devlink_linecard,
					device->devlink_device);
	kfree(device);
}

static void mlxsw_linecard_devices_detach(struct mlxsw_linecard *linecard)
{
	struct mlxsw_core *mlxsw_core = linecard->linecards->mlxsw_core;
	struct mlxsw_linecard_device *device, *tmp;

	list_for_each_entry_safe(device, tmp, &linecard->device_list, list)
		mlxsw_linecard_device_detach(mlxsw_core, linecard, device);
}

static int mlxsw_linecard_devices_attach(struct mlxsw_linecard *linecard)
{
	struct mlxsw_core *mlxsw_core = linecard->linecards->mlxsw_core;
	u8 msg_seq = 0;
	int err;

	do {
		char mddq_pl[MLXSW_REG_MDDQ_LEN];
		bool flash_owner;
		bool data_valid;
		u8 device_index;

		mlxsw_reg_mddq_device_info_pack(mddq_pl, linecard->slot_index,
						msg_seq);
		err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mddq), mddq_pl);
		if (err)
			return err;
		mlxsw_reg_mddq_device_info_unpack(mddq_pl, &msg_seq,
						  &data_valid, &flash_owner,
						  &device_index);
		if (!data_valid)
			break;
		err = mlxsw_linecard_device_attach(mlxsw_core, linecard,
						   device_index, flash_owner);
		if (err)
			goto rollback;
	} while (msg_seq);

	return 0;

rollback:
	mlxsw_linecard_devices_detach(linecard);
	return err;
}

static void mlxsw_linecard_provision_fail(struct mlxsw_linecard *linecard)
{
	linecard->provisioned = false;
	linecard->ready = false;
	linecard->active = false;
	mlxsw_linecard_devices_detach(linecard);
	devlink_linecard_provision_fail(linecard->devlink_linecard);
}

struct mlxsw_linecards_event_ops_item {
	struct list_head list;
	const struct mlxsw_linecards_event_ops *event_ops;
	void *priv;
};

static void
mlxsw_linecard_event_op_call(struct mlxsw_linecard *linecard,
			     mlxsw_linecards_event_op_t *op, void *priv)
{
	struct mlxsw_core *mlxsw_core = linecard->linecards->mlxsw_core;

	if (!op)
		return;
	op(mlxsw_core, linecard->slot_index, priv);
}

static void
mlxsw_linecard_active_ops_call(struct mlxsw_linecard *linecard)
{
	struct mlxsw_linecards *linecards = linecard->linecards;
	struct mlxsw_linecards_event_ops_item *item;

	mutex_lock(&linecards->event_ops_list_lock);
	list_for_each_entry(item, &linecards->event_ops_list, list)
		mlxsw_linecard_event_op_call(linecard,
					     item->event_ops->got_active,
					     item->priv);
	mutex_unlock(&linecards->event_ops_list_lock);
}

static void
mlxsw_linecard_inactive_ops_call(struct mlxsw_linecard *linecard)
{
	struct mlxsw_linecards *linecards = linecard->linecards;
	struct mlxsw_linecards_event_ops_item *item;

	mutex_lock(&linecards->event_ops_list_lock);
	list_for_each_entry(item, &linecards->event_ops_list, list)
		mlxsw_linecard_event_op_call(linecard,
					     item->event_ops->got_inactive,
					     item->priv);
	mutex_unlock(&linecards->event_ops_list_lock);
}

static void
mlxsw_linecards_event_ops_register_call(struct mlxsw_linecards *linecards,
					const struct mlxsw_linecards_event_ops_item *item)
{
	struct mlxsw_linecard *linecard;
	int i;

	for (i = 0; i < linecards->count; i++) {
		linecard = mlxsw_linecard_get(linecards, i + 1);
		mutex_lock(&linecard->lock);
		if (linecard->active)
			mlxsw_linecard_event_op_call(linecard,
						     item->event_ops->got_active,
						     item->priv);
		mutex_unlock(&linecard->lock);
	}
}

static void
mlxsw_linecards_event_ops_unregister_call(struct mlxsw_linecards *linecards,
					  const struct mlxsw_linecards_event_ops_item *item)
{
	struct mlxsw_linecard *linecard;
	int i;

	for (i = 0; i < linecards->count; i++) {
		linecard = mlxsw_linecard_get(linecards, i + 1);
		mutex_lock(&linecard->lock);
		if (linecard->active)
			mlxsw_linecard_event_op_call(linecard,
						     item->event_ops->got_inactive,
						     item->priv);
		mutex_unlock(&linecard->lock);
	}
}

int mlxsw_linecards_event_ops_register(struct mlxsw_core *mlxsw_core,
				       struct mlxsw_linecards_event_ops *ops,
				       void *priv)
{
	struct mlxsw_linecards *linecards = mlxsw_core_linecards(mlxsw_core);
	struct mlxsw_linecards_event_ops_item *item;

	if (!linecards)
		return 0;
	item = kzalloc(sizeof(*item), GFP_KERNEL);
	if (!item)
		return -ENOMEM;
	item->event_ops = ops;
	item->priv = priv;

	mutex_lock(&linecards->event_ops_list_lock);
	list_add_tail(&item->list, &linecards->event_ops_list);
	mutex_unlock(&linecards->event_ops_list_lock);
	mlxsw_linecards_event_ops_register_call(linecards, item);
	return 0;
}
EXPORT_SYMBOL(mlxsw_linecards_event_ops_register);

void mlxsw_linecards_event_ops_unregister(struct mlxsw_core *mlxsw_core,
					  struct mlxsw_linecards_event_ops *ops,
					  void *priv)
{
	struct mlxsw_linecards *linecards = mlxsw_core_linecards(mlxsw_core);
	struct mlxsw_linecards_event_ops_item *item, *tmp;
	bool found = false;

	if (!linecards)
		return;
	mutex_lock(&linecards->event_ops_list_lock);
	list_for_each_entry_safe(item, tmp, &linecards->event_ops_list, list) {
		if (item->event_ops == ops && item->priv == priv) {
			list_del(&item->list);
			found = true;
			break;
		}
	}
	mutex_unlock(&linecards->event_ops_list_lock);

	if (!found)
		return;
	mlxsw_linecards_event_ops_unregister_call(linecards, item);
	kfree(item);
}
EXPORT_SYMBOL(mlxsw_linecards_event_ops_unregister);

static int
mlxsw_linecard_provision_set(struct mlxsw_linecard *linecard, u8 card_type,
			     u16 hw_revision, u16 ini_version)
{
	struct mlxsw_linecards *linecards = linecard->linecards;
	const char *type;
	int err;

	type = mlxsw_linecard_types_lookup(linecards, card_type);
	mlxsw_linecard_status_event_done(linecard,
					 MLXSW_LINECARD_STATUS_EVENT_TYPE_PROVISION);
	if (!type) {
		/* It is possible for a line card to be provisioned before
		 * driver initialization. Due to a missing INI bundle file
		 * or an outdated one, the queried card's type might not
		 * be recognized by the driver. In this case, try to query
		 * the card's name from the device.
		 */
		type = mlxsw_linecard_type_name(linecard);
		if (IS_ERR(type)) {
			mlxsw_linecard_provision_fail(linecard);
			return PTR_ERR(type);
		}
	}
	err = mlxsw_linecard_devices_attach(linecard);
	if (err) {
		mlxsw_linecard_provision_fail(linecard);
		return err;
	}
	linecard->provisioned = true;
	linecard->hw_revision = hw_revision;
	linecard->ini_version = ini_version;
	devlink_linecard_provision_set(linecard->devlink_linecard, type);
	return 0;
}

static void mlxsw_linecard_provision_clear(struct mlxsw_linecard *linecard)
{
	mlxsw_linecard_status_event_done(linecard,
					 MLXSW_LINECARD_STATUS_EVENT_TYPE_UNPROVISION);
	linecard->provisioned = false;
	mlxsw_linecard_devices_detach(linecard);
	devlink_linecard_provision_clear(linecard->devlink_linecard);
}

static int mlxsw_linecard_ready_set(struct mlxsw_linecard *linecard)
{
	struct mlxsw_core *mlxsw_core = linecard->linecards->mlxsw_core;
	char mddc_pl[MLXSW_REG_MDDC_LEN];
	int err;

	mlxsw_reg_mddc_pack(mddc_pl, linecard->slot_index, false, true);
	err = mlxsw_reg_write(mlxsw_core, MLXSW_REG(mddc), mddc_pl);
	if (err)
		return err;
	linecard->ready = true;
	return 0;
}

static int mlxsw_linecard_ready_clear(struct mlxsw_linecard *linecard)
{
	struct mlxsw_core *mlxsw_core = linecard->linecards->mlxsw_core;
	char mddc_pl[MLXSW_REG_MDDC_LEN];
	int err;

	mlxsw_reg_mddc_pack(mddc_pl, linecard->slot_index, false, false);
	err = mlxsw_reg_write(mlxsw_core, MLXSW_REG(mddc), mddc_pl);
	if (err)
		return err;
	linecard->ready = false;
	return 0;
}

static void mlxsw_linecard_active_set(struct mlxsw_linecard *linecard)
{
	mlxsw_linecard_active_ops_call(linecard);
	linecard->active = true;
	devlink_linecard_activate(linecard->devlink_linecard);
}

static void mlxsw_linecard_active_clear(struct mlxsw_linecard *linecard)
{
	mlxsw_linecard_inactive_ops_call(linecard);
	linecard->active = false;
	devlink_linecard_deactivate(linecard->devlink_linecard);
}

static int mlxsw_linecard_status_process(struct mlxsw_linecards *linecards,
					 struct mlxsw_linecard *linecard,
					 const char *mddq_pl)
{
	enum mlxsw_reg_mddq_slot_info_ready ready;
	bool provisioned, sr_valid, active;
	u16 ini_version, hw_revision;
	u8 slot_index, card_type;
	int err = 0;

	mlxsw_reg_mddq_slot_info_unpack(mddq_pl, &slot_index, &provisioned,
					&sr_valid, &ready, &active,
					&hw_revision, &ini_version,
					&card_type);

	if (linecard) {
		if (WARN_ON(slot_index != linecard->slot_index))
			return -EINVAL;
	} else {
		if (WARN_ON(slot_index > linecards->count))
			return -EINVAL;
		linecard = mlxsw_linecard_get(linecards, slot_index);
	}

	mutex_lock(&linecard->lock);

	if (provisioned && linecard->provisioned != provisioned) {
		err = mlxsw_linecard_provision_set(linecard, card_type,
						   hw_revision, ini_version);
		if (err)
			goto out;
	}

	if (ready == MLXSW_REG_MDDQ_SLOT_INFO_READY_READY && !linecard->ready) {
		err = mlxsw_linecard_ready_set(linecard);
		if (err)
			goto out;
	}

	if (active && linecard->active != active)
		mlxsw_linecard_active_set(linecard);

	if (!active && linecard->active != active)
		mlxsw_linecard_active_clear(linecard);

	if (ready != MLXSW_REG_MDDQ_SLOT_INFO_READY_READY &&
	    linecard->ready) {
		err = mlxsw_linecard_ready_clear(linecard);
		if (err)
			goto out;
	}

	if (!provisioned && linecard->provisioned != provisioned)
		mlxsw_linecard_provision_clear(linecard);

out:
	mutex_unlock(&linecard->lock);
	return err;
}

static int mlxsw_linecard_status_get_and_process(struct mlxsw_core *mlxsw_core,
						 struct mlxsw_linecards *linecards,
						 struct mlxsw_linecard *linecard)
{
	char mddq_pl[MLXSW_REG_MDDQ_LEN];
	int err;

	mlxsw_reg_mddq_slot_info_pack(mddq_pl, linecard->slot_index, false);
	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mddq), mddq_pl);
	if (err)
		return err;

	return mlxsw_linecard_status_process(linecards, linecard, mddq_pl);
}

static const char * const mlxsw_linecard_status_event_type_name[] = {
	[MLXSW_LINECARD_STATUS_EVENT_TYPE_PROVISION] = "provision",
	[MLXSW_LINECARD_STATUS_EVENT_TYPE_UNPROVISION] = "unprovision",
};

static void mlxsw_linecard_status_event_to_work(struct work_struct *work)
{
	struct mlxsw_linecard *linecard =
		container_of(work, struct mlxsw_linecard,
			     status_event_to_dw.work);

	mutex_lock(&linecard->lock);
	dev_err(linecard->linecards->bus_info->dev, "linecard %u: Timeout reached waiting on %s status event",
		linecard->slot_index,
		mlxsw_linecard_status_event_type_name[linecard->status_event_type_to]);
	mlxsw_linecard_provision_fail(linecard);
	mutex_unlock(&linecard->lock);
}

static int __mlxsw_linecard_fix_fsm_state(struct mlxsw_linecard *linecard)
{
	dev_info(linecard->linecards->bus_info->dev, "linecard %u: Clearing FSM state error",
		 linecard->slot_index);
	mlxsw_reg_mbct_pack(linecard->mbct_pl, linecard->slot_index,
			    MLXSW_REG_MBCT_OP_CLEAR_ERRORS, false);
	return mlxsw_reg_write(linecard->linecards->mlxsw_core,
			       MLXSW_REG(mbct), linecard->mbct_pl);
}

static int mlxsw_linecard_fix_fsm_state(struct mlxsw_linecard *linecard,
					enum mlxsw_reg_mbct_fsm_state fsm_state)
{
	if (fsm_state != MLXSW_REG_MBCT_FSM_STATE_ERROR)
		return 0;
	return __mlxsw_linecard_fix_fsm_state(linecard);
}

static int
mlxsw_linecard_query_ini_status(struct mlxsw_linecard *linecard,
				enum mlxsw_reg_mbct_status *status,
				enum mlxsw_reg_mbct_fsm_state *fsm_state,
				struct netlink_ext_ack *extack)
{
	int err;

	mlxsw_reg_mbct_pack(linecard->mbct_pl, linecard->slot_index,
			    MLXSW_REG_MBCT_OP_QUERY_STATUS, false);
	err = mlxsw_reg_query(linecard->linecards->mlxsw_core, MLXSW_REG(mbct),
			      linecard->mbct_pl);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to query linecard INI status");
		return err;
	}
	mlxsw_reg_mbct_unpack(linecard->mbct_pl, NULL, status, fsm_state);
	return err;
}

static int
mlxsw_linecard_ini_transfer(struct mlxsw_core *mlxsw_core,
			    struct mlxsw_linecard *linecard,
			    const struct mlxsw_linecard_ini_file *ini_file,
			    struct netlink_ext_ack *extack)
{
	enum mlxsw_reg_mbct_fsm_state fsm_state;
	enum mlxsw_reg_mbct_status status;
	size_t size_left;
	const u8 *data;
	int err;

	size_left = le16_to_cpu(ini_file->size);
	data = ini_file->data;
	while (size_left) {
		size_t data_size = MLXSW_REG_MBCT_DATA_LEN;
		bool is_last = false;

		if (size_left <= MLXSW_REG_MBCT_DATA_LEN) {
			data_size = size_left;
			is_last = true;
		}

		mlxsw_reg_mbct_pack(linecard->mbct_pl, linecard->slot_index,
				    MLXSW_REG_MBCT_OP_DATA_TRANSFER, false);
		mlxsw_reg_mbct_dt_pack(linecard->mbct_pl, data_size,
				       is_last, data);
		err = mlxsw_reg_write(mlxsw_core, MLXSW_REG(mbct),
				      linecard->mbct_pl);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to issue linecard INI data transfer");
			return err;
		}
		mlxsw_reg_mbct_unpack(linecard->mbct_pl, NULL,
				      &status, &fsm_state);
		if ((!is_last && status != MLXSW_REG_MBCT_STATUS_PART_DATA) ||
		    (is_last && status != MLXSW_REG_MBCT_STATUS_LAST_DATA)) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to transfer linecard INI data");
			mlxsw_linecard_fix_fsm_state(linecard, fsm_state);
			return -EINVAL;
		}
		size_left -= data_size;
		data += data_size;
	}

	return 0;
}

static int
mlxsw_linecard_ini_erase(struct mlxsw_core *mlxsw_core,
			 struct mlxsw_linecard *linecard,
			 struct netlink_ext_ack *extack)
{
	enum mlxsw_reg_mbct_fsm_state fsm_state;
	enum mlxsw_reg_mbct_status status;
	int err;

	mlxsw_reg_mbct_pack(linecard->mbct_pl, linecard->slot_index,
			    MLXSW_REG_MBCT_OP_ERASE_INI_IMAGE, false);
	err = mlxsw_reg_write(mlxsw_core, MLXSW_REG(mbct),
			      linecard->mbct_pl);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to issue linecard INI erase");
		return err;
	}
	mlxsw_reg_mbct_unpack(linecard->mbct_pl, NULL, &status, &fsm_state);
	switch (status) {
	case MLXSW_REG_MBCT_STATUS_ERASE_COMPLETE:
		break;
	default:
		/* Should not happen */
		fallthrough;
	case MLXSW_REG_MBCT_STATUS_ERASE_FAILED:
		NL_SET_ERR_MSG_MOD(extack, "Failed to erase linecard INI");
		goto fix_fsm_err_out;
	case MLXSW_REG_MBCT_STATUS_ERROR_INI_IN_USE:
		NL_SET_ERR_MSG_MOD(extack, "Failed to erase linecard INI while being used");
		goto fix_fsm_err_out;
	}
	return 0;

fix_fsm_err_out:
	mlxsw_linecard_fix_fsm_state(linecard, fsm_state);
	return -EINVAL;
}

static void mlxsw_linecard_bct_process(struct mlxsw_core *mlxsw_core,
				       const char *mbct_pl)
{
	struct mlxsw_linecards *linecards = mlxsw_core_linecards(mlxsw_core);
	enum mlxsw_reg_mbct_fsm_state fsm_state;
	enum mlxsw_reg_mbct_status status;
	struct mlxsw_linecard *linecard;
	u8 slot_index;

	mlxsw_reg_mbct_unpack(mbct_pl, &slot_index, &status, &fsm_state);
	if (WARN_ON(slot_index > linecards->count))
		return;
	linecard = mlxsw_linecard_get(linecards, slot_index);
	mutex_lock(&linecard->lock);
	if (status == MLXSW_REG_MBCT_STATUS_ACTIVATION_FAILED) {
		dev_err(linecards->bus_info->dev, "linecard %u: Failed to activate INI",
			linecard->slot_index);
		goto fix_fsm_out;
	}
	mutex_unlock(&linecard->lock);
	return;

fix_fsm_out:
	mlxsw_linecard_fix_fsm_state(linecard, fsm_state);
	mlxsw_linecard_provision_fail(linecard);
	mutex_unlock(&linecard->lock);
}

static int
mlxsw_linecard_ini_activate(struct mlxsw_core *mlxsw_core,
			    struct mlxsw_linecard *linecard,
			    struct netlink_ext_ack *extack)
{
	enum mlxsw_reg_mbct_fsm_state fsm_state;
	enum mlxsw_reg_mbct_status status;
	int err;

	mlxsw_reg_mbct_pack(linecard->mbct_pl, linecard->slot_index,
			    MLXSW_REG_MBCT_OP_ACTIVATE, true);
	err = mlxsw_reg_write(mlxsw_core, MLXSW_REG(mbct), linecard->mbct_pl);
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to issue linecard INI activation");
		return err;
	}
	mlxsw_reg_mbct_unpack(linecard->mbct_pl, NULL, &status, &fsm_state);
	if (status == MLXSW_REG_MBCT_STATUS_ACTIVATION_FAILED) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to activate linecard INI");
		goto fix_fsm_err_out;
	}

	return 0;

fix_fsm_err_out:
	mlxsw_linecard_fix_fsm_state(linecard, fsm_state);
	return -EINVAL;
}

#define MLXSW_LINECARD_INI_WAIT_RETRIES 10
#define MLXSW_LINECARD_INI_WAIT_MS 500

static int
mlxsw_linecard_ini_in_use_wait(struct mlxsw_core *mlxsw_core,
			       struct mlxsw_linecard *linecard,
			       struct netlink_ext_ack *extack)
{
	enum mlxsw_reg_mbct_fsm_state fsm_state;
	enum mlxsw_reg_mbct_status status;
	unsigned int ini_wait_retries = 0;
	int err;

query_ini_status:
	err = mlxsw_linecard_query_ini_status(linecard, &status,
					      &fsm_state, extack);
	if (err)
		return err;

	switch (fsm_state) {
	case MLXSW_REG_MBCT_FSM_STATE_INI_IN_USE:
		if (ini_wait_retries++ > MLXSW_LINECARD_INI_WAIT_RETRIES) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to wait for linecard INI to be unused");
			return -EINVAL;
		}
		mdelay(MLXSW_LINECARD_INI_WAIT_MS);
		goto query_ini_status;
	default:
		break;
	}
	return 0;
}

static bool mlxsw_linecard_port_selector(void *priv, u16 local_port)
{
	struct mlxsw_linecard *linecard = priv;
	struct mlxsw_core *mlxsw_core;

	mlxsw_core = linecard->linecards->mlxsw_core;
	return linecard == mlxsw_core_port_linecard_get(mlxsw_core, local_port);
}

static int mlxsw_linecard_provision(struct devlink_linecard *devlink_linecard,
				    void *priv, const char *type,
				    const void *type_priv,
				    struct netlink_ext_ack *extack)
{
	const struct mlxsw_linecard_ini_file *ini_file = type_priv;
	struct mlxsw_linecard *linecard = priv;
	struct mlxsw_core *mlxsw_core;
	int err;

	mutex_lock(&linecard->lock);

	mlxsw_core = linecard->linecards->mlxsw_core;

	err = mlxsw_linecard_ini_erase(mlxsw_core, linecard, extack);
	if (err)
		goto err_out;

	err = mlxsw_linecard_ini_transfer(mlxsw_core, linecard,
					  ini_file, extack);
	if (err)
		goto err_out;

	mlxsw_linecard_status_event_to_schedule(linecard,
						MLXSW_LINECARD_STATUS_EVENT_TYPE_PROVISION);
	err = mlxsw_linecard_ini_activate(mlxsw_core, linecard, extack);
	if (err)
		goto err_out;

	goto out;

err_out:
	mlxsw_linecard_provision_fail(linecard);
out:
	mutex_unlock(&linecard->lock);
	return err;
}

static int mlxsw_linecard_unprovision(struct devlink_linecard *devlink_linecard,
				      void *priv,
				      struct netlink_ext_ack *extack)
{
	struct mlxsw_linecard *linecard = priv;
	struct mlxsw_core *mlxsw_core;
	int err;

	mutex_lock(&linecard->lock);

	mlxsw_core = linecard->linecards->mlxsw_core;

	mlxsw_core_ports_remove_selected(mlxsw_core,
					 mlxsw_linecard_port_selector,
					 linecard);

	err = mlxsw_linecard_ini_in_use_wait(mlxsw_core, linecard, extack);
	if (err)
		goto err_out;

	mlxsw_linecard_status_event_to_schedule(linecard,
						MLXSW_LINECARD_STATUS_EVENT_TYPE_UNPROVISION);
	err = mlxsw_linecard_ini_erase(mlxsw_core, linecard, extack);
	if (err)
		goto err_out;

	goto out;

err_out:
	mlxsw_linecard_provision_fail(linecard);
out:
	mutex_unlock(&linecard->lock);
	return err;
}

static bool mlxsw_linecard_same_provision(struct devlink_linecard *devlink_linecard,
					  void *priv, const char *type,
					  const void *type_priv)
{
	const struct mlxsw_linecard_ini_file *ini_file = type_priv;
	struct mlxsw_linecard *linecard = priv;
	bool ret;

	mutex_lock(&linecard->lock);
	ret = linecard->hw_revision == be16_to_cpu(ini_file->format.hw_revision) &&
	      linecard->ini_version == be16_to_cpu(ini_file->format.ini_version);
	mutex_unlock(&linecard->lock);
	return ret;
}

static unsigned int
mlxsw_linecard_types_count(struct devlink_linecard *devlink_linecard,
			   void *priv)
{
	struct mlxsw_linecard *linecard = priv;

	return linecard->linecards->types_info ?
	       linecard->linecards->types_info->count : 0;
}

static void mlxsw_linecard_types_get(struct devlink_linecard *devlink_linecard,
				     void *priv, unsigned int index,
				     const char **type, const void **type_priv)
{
	struct mlxsw_linecard_types_info *types_info;
	struct mlxsw_linecard_ini_file *ini_file;
	struct mlxsw_linecard *linecard = priv;

	types_info = linecard->linecards->types_info;
	if (WARN_ON_ONCE(!types_info))
		return;
	ini_file = types_info->ini_files[index];
	*type = ini_file->format.name;
	*type_priv = ini_file;
}

static const struct devlink_linecard_ops mlxsw_linecard_ops = {
	.provision = mlxsw_linecard_provision,
	.unprovision = mlxsw_linecard_unprovision,
	.same_provision = mlxsw_linecard_same_provision,
	.types_count = mlxsw_linecard_types_count,
	.types_get = mlxsw_linecard_types_get,
};

struct mlxsw_linecard_status_event {
	struct mlxsw_core *mlxsw_core;
	char mddq_pl[MLXSW_REG_MDDQ_LEN];
	struct work_struct work;
};

static void mlxsw_linecard_status_event_work(struct work_struct *work)
{
	struct mlxsw_linecard_status_event *event;
	struct mlxsw_linecards *linecards;
	struct mlxsw_core *mlxsw_core;

	event = container_of(work, struct mlxsw_linecard_status_event, work);
	mlxsw_core = event->mlxsw_core;
	linecards = mlxsw_core_linecards(mlxsw_core);
	mlxsw_linecard_status_process(linecards, NULL, event->mddq_pl);
	kfree(event);
}

static void
mlxsw_linecard_status_listener_func(const struct mlxsw_reg_info *reg,
				    char *mddq_pl, void *priv)
{
	struct mlxsw_linecard_status_event *event;
	struct mlxsw_core *mlxsw_core = priv;

	event = kmalloc(sizeof(*event), GFP_ATOMIC);
	if (!event)
		return;
	event->mlxsw_core = mlxsw_core;
	memcpy(event->mddq_pl, mddq_pl, sizeof(event->mddq_pl));
	INIT_WORK(&event->work, mlxsw_linecard_status_event_work);
	mlxsw_core_schedule_work(&event->work);
}

struct mlxsw_linecard_bct_event {
	struct mlxsw_core *mlxsw_core;
	char mbct_pl[MLXSW_REG_MBCT_LEN];
	struct work_struct work;
};

static void mlxsw_linecard_bct_event_work(struct work_struct *work)
{
	struct mlxsw_linecard_bct_event *event;
	struct mlxsw_core *mlxsw_core;

	event = container_of(work, struct mlxsw_linecard_bct_event, work);
	mlxsw_core = event->mlxsw_core;
	mlxsw_linecard_bct_process(mlxsw_core, event->mbct_pl);
	kfree(event);
}

static void
mlxsw_linecard_bct_listener_func(const struct mlxsw_reg_info *reg,
				 char *mbct_pl, void *priv)
{
	struct mlxsw_linecard_bct_event *event;
	struct mlxsw_core *mlxsw_core = priv;

	event = kmalloc(sizeof(*event), GFP_ATOMIC);
	if (!event)
		return;
	event->mlxsw_core = mlxsw_core;
	memcpy(event->mbct_pl, mbct_pl, sizeof(event->mbct_pl));
	INIT_WORK(&event->work, mlxsw_linecard_bct_event_work);
	mlxsw_core_schedule_work(&event->work);
}

static const struct mlxsw_listener mlxsw_linecard_listener[] = {
	MLXSW_CORE_EVENTL(mlxsw_linecard_status_listener_func, DSDSC),
	MLXSW_CORE_EVENTL(mlxsw_linecard_bct_listener_func, BCTOE),
};

static int mlxsw_linecard_event_delivery_set(struct mlxsw_core *mlxsw_core,
					     struct mlxsw_linecard *linecard,
					     bool enable)
{
	char mddq_pl[MLXSW_REG_MDDQ_LEN];

	mlxsw_reg_mddq_slot_info_pack(mddq_pl, linecard->slot_index, enable);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(mddq), mddq_pl);
}

static int mlxsw_linecard_init(struct mlxsw_core *mlxsw_core,
			       struct mlxsw_linecards *linecards,
			       u8 slot_index)
{
	struct devlink_linecard *devlink_linecard;
	struct mlxsw_linecard *linecard;
	int err;

	linecard = mlxsw_linecard_get(linecards, slot_index);
	linecard->slot_index = slot_index;
	linecard->linecards = linecards;
	mutex_init(&linecard->lock);
	INIT_LIST_HEAD(&linecard->device_list);

	devlink_linecard = devlink_linecard_create(priv_to_devlink(mlxsw_core),
						   slot_index, &mlxsw_linecard_ops,
						   linecard);
	if (IS_ERR(devlink_linecard)) {
		err = PTR_ERR(devlink_linecard);
		goto err_devlink_linecard_create;
	}
	linecard->devlink_linecard = devlink_linecard;
	INIT_DELAYED_WORK(&linecard->status_event_to_dw,
			  &mlxsw_linecard_status_event_to_work);

	err = mlxsw_linecard_event_delivery_set(mlxsw_core, linecard, true);
	if (err)
		goto err_event_delivery_set;

	err = mlxsw_linecard_status_get_and_process(mlxsw_core, linecards,
						    linecard);
	if (err)
		goto err_status_get_and_process;

	return 0;

err_status_get_and_process:
	mlxsw_linecard_event_delivery_set(mlxsw_core, linecard, false);
err_event_delivery_set:
	devlink_linecard_destroy(linecard->devlink_linecard);
err_devlink_linecard_create:
	mutex_destroy(&linecard->lock);
	return err;
}

static void mlxsw_linecard_fini(struct mlxsw_core *mlxsw_core,
				struct mlxsw_linecards *linecards,
				u8 slot_index)
{
	struct mlxsw_linecard *linecard;

	linecard = mlxsw_linecard_get(linecards, slot_index);
	mlxsw_linecard_event_delivery_set(mlxsw_core, linecard, false);
	cancel_delayed_work_sync(&linecard->status_event_to_dw);
	/* Make sure all scheduled events are processed */
	mlxsw_core_flush_owq();
	if (linecard->active)
		mlxsw_linecard_active_clear(linecard);
	mlxsw_linecard_devices_detach(linecard);
	devlink_linecard_destroy(linecard->devlink_linecard);
	mutex_destroy(&linecard->lock);
}

/*       LINECARDS INI BUNDLE FILE
 *  +----------------------------------+
 *  |        MAGIC ("NVLCINI+")        |
 *  +----------------------------------+     +--------------------+
 *  |  INI 0                           +---> | __le16 size        |
 *  +----------------------------------+     | __be16 hw_revision |
 *  |  INI 1                           |     | __be16 ini_version |
 *  +----------------------------------+     | u8 __dontcare[3]   |
 *  |  ...                             |     | u8 type            |
 *  +----------------------------------+     | u8 name[20]        |
 *  |  INI N                           |     | ...                |
 *  +----------------------------------+     +--------------------+
 */

#define MLXSW_LINECARDS_INI_BUNDLE_MAGIC "NVLCINI+"

static int
mlxsw_linecard_types_file_validate(struct mlxsw_linecards *linecards,
				   struct mlxsw_linecard_types_info *types_info)
{
	size_t magic_size = strlen(MLXSW_LINECARDS_INI_BUNDLE_MAGIC);
	struct mlxsw_linecard_ini_file *ini_file;
	size_t size = types_info->data_size;
	const u8 *data = types_info->data;
	unsigned int count = 0;
	u16 ini_file_size;

	if (size < magic_size) {
		dev_warn(linecards->bus_info->dev, "Invalid linecards INIs file size, smaller than magic size\n");
		return -EINVAL;
	}
	if (memcmp(data, MLXSW_LINECARDS_INI_BUNDLE_MAGIC, magic_size)) {
		dev_warn(linecards->bus_info->dev, "Invalid linecards INIs file magic pattern\n");
		return -EINVAL;
	}

	data += magic_size;
	size -= magic_size;

	while (size > 0) {
		if (size < sizeof(*ini_file)) {
			dev_warn(linecards->bus_info->dev, "Linecards INIs file contains INI which is smaller than bare minimum\n");
			return -EINVAL;
		}
		ini_file = (struct mlxsw_linecard_ini_file *) data;
		ini_file_size = le16_to_cpu(ini_file->size);
		if (ini_file_size + sizeof(__le16) > size) {
			dev_warn(linecards->bus_info->dev, "Linecards INIs file appears to be truncated\n");
			return -EINVAL;
		}
		if (ini_file_size % 4) {
			dev_warn(linecards->bus_info->dev, "Linecards INIs file contains INI with invalid size\n");
			return -EINVAL;
		}
		data += ini_file_size + sizeof(__le16);
		size -= ini_file_size + sizeof(__le16);
		count++;
	}
	if (!count) {
		dev_warn(linecards->bus_info->dev, "Linecards INIs file does not contain any INI\n");
		return -EINVAL;
	}
	types_info->count = count;
	return 0;
}

static void
mlxsw_linecard_types_file_parse(struct mlxsw_linecard_types_info *types_info)
{
	size_t magic_size = strlen(MLXSW_LINECARDS_INI_BUNDLE_MAGIC);
	size_t size = types_info->data_size - magic_size;
	const u8 *data = types_info->data + magic_size;
	struct mlxsw_linecard_ini_file *ini_file;
	unsigned int count = 0;
	u16 ini_file_size;
	int i;

	while (size) {
		ini_file = (struct mlxsw_linecard_ini_file *) data;
		ini_file_size = le16_to_cpu(ini_file->size);
		for (i = 0; i < ini_file_size / 4; i++) {
			u32 *val = &((u32 *) ini_file->data)[i];

			*val = swab32(*val);
		}
		types_info->ini_files[count] = ini_file;
		data += ini_file_size + sizeof(__le16);
		size -= ini_file_size + sizeof(__le16);
		count++;
	}
}

#define MLXSW_LINECARDS_INI_BUNDLE_FILENAME_FMT \
	"mellanox/lc_ini_bundle_%u_%u.bin"
#define MLXSW_LINECARDS_INI_BUNDLE_FILENAME_LEN \
	(sizeof(MLXSW_LINECARDS_INI_BUNDLE_FILENAME_FMT) + 4)

static int mlxsw_linecard_types_init(struct mlxsw_core *mlxsw_core,
				     struct mlxsw_linecards *linecards)
{
	const struct mlxsw_fw_rev *rev = &linecards->bus_info->fw_rev;
	char filename[MLXSW_LINECARDS_INI_BUNDLE_FILENAME_LEN];
	struct mlxsw_linecard_types_info *types_info;
	const struct firmware *firmware;
	int err;

	err = snprintf(filename, sizeof(filename),
		       MLXSW_LINECARDS_INI_BUNDLE_FILENAME_FMT,
		       rev->minor, rev->subminor);
	WARN_ON(err >= sizeof(filename));

	err = request_firmware_direct(&firmware, filename,
				      linecards->bus_info->dev);
	if (err) {
		dev_warn(linecards->bus_info->dev, "Could not request linecards INI file \"%s\", provisioning will not be possible\n",
			 filename);
		return 0;
	}

	types_info = kzalloc(sizeof(*types_info), GFP_KERNEL);
	if (!types_info) {
		release_firmware(firmware);
		return -ENOMEM;
	}
	linecards->types_info = types_info;

	types_info->data_size = firmware->size;
	types_info->data = vmalloc(types_info->data_size);
	if (!types_info->data) {
		err = -ENOMEM;
		release_firmware(firmware);
		goto err_data_alloc;
	}
	memcpy(types_info->data, firmware->data, types_info->data_size);
	release_firmware(firmware);

	err = mlxsw_linecard_types_file_validate(linecards, types_info);
	if (err) {
		err = 0;
		goto err_type_file_file_validate;
	}

	types_info->ini_files = kmalloc_array(types_info->count,
					      sizeof(struct mlxsw_linecard_ini_file *),
					      GFP_KERNEL);
	if (!types_info->ini_files) {
		err = -ENOMEM;
		goto err_ini_files_alloc;
	}

	mlxsw_linecard_types_file_parse(types_info);

	return 0;

err_ini_files_alloc:
err_type_file_file_validate:
	vfree(types_info->data);
err_data_alloc:
	kfree(types_info);
	return err;
}

static void mlxsw_linecard_types_fini(struct mlxsw_linecards *linecards)
{
	struct mlxsw_linecard_types_info *types_info = linecards->types_info;

	if (!types_info)
		return;
	kfree(types_info->ini_files);
	vfree(types_info->data);
	kfree(types_info);
}

int mlxsw_linecards_init(struct mlxsw_core *mlxsw_core,
			 const struct mlxsw_bus_info *bus_info)
{
	char mgpir_pl[MLXSW_REG_MGPIR_LEN];
	struct mlxsw_linecards *linecards;
	u8 slot_count;
	int err;
	int i;

	mlxsw_reg_mgpir_pack(mgpir_pl, 0);
	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mgpir), mgpir_pl);
	if (err)
		return err;

	mlxsw_reg_mgpir_unpack(mgpir_pl, NULL, NULL, NULL,
			       NULL, &slot_count);
	if (!slot_count)
		return 0;

	linecards = vzalloc(struct_size(linecards, linecards, slot_count));
	if (!linecards)
		return -ENOMEM;
	linecards->count = slot_count;
	linecards->mlxsw_core = mlxsw_core;
	linecards->bus_info = bus_info;
	INIT_LIST_HEAD(&linecards->event_ops_list);
	mutex_init(&linecards->event_ops_list_lock);

	err = mlxsw_linecard_types_init(mlxsw_core, linecards);
	if (err)
		goto err_types_init;

	err = mlxsw_core_traps_register(mlxsw_core, mlxsw_linecard_listener,
					ARRAY_SIZE(mlxsw_linecard_listener),
					mlxsw_core);
	if (err)
		goto err_traps_register;

	mlxsw_core_linecards_set(mlxsw_core, linecards);

	for (i = 0; i < linecards->count; i++) {
		err = mlxsw_linecard_init(mlxsw_core, linecards, i + 1);
		if (err)
			goto err_linecard_init;
	}

	return 0;

err_linecard_init:
	for (i--; i >= 0; i--)
		mlxsw_linecard_fini(mlxsw_core, linecards, i + 1);
	mlxsw_core_traps_unregister(mlxsw_core, mlxsw_linecard_listener,
				    ARRAY_SIZE(mlxsw_linecard_listener),
				    mlxsw_core);
err_traps_register:
	mlxsw_linecard_types_fini(linecards);
err_types_init:
	vfree(linecards);
	return err;
}

void mlxsw_linecards_fini(struct mlxsw_core *mlxsw_core)
{
	struct mlxsw_linecards *linecards = mlxsw_core_linecards(mlxsw_core);
	int i;

	if (!linecards)
		return;
	for (i = 0; i < linecards->count; i++)
		mlxsw_linecard_fini(mlxsw_core, linecards, i + 1);
	mlxsw_core_traps_unregister(mlxsw_core, mlxsw_linecard_listener,
				    ARRAY_SIZE(mlxsw_linecard_listener),
				    mlxsw_core);
	mlxsw_linecard_types_fini(linecards);
	mutex_destroy(&linecards->event_ops_list_lock);
	WARN_ON(!list_empty(&linecards->event_ops_list));
	vfree(linecards);
}
