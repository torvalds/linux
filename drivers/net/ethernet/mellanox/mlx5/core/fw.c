/*
 * Copyright (c) 2013-2015, Mellanox Technologies. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <linux/mlx5/driver.h>
#include <linux/mlx5/cmd.h>
#include <linux/module.h>
#include "mlx5_core.h"
#include "../../mlxfw/mlxfw.h"

static int mlx5_cmd_query_adapter(struct mlx5_core_dev *dev, u32 *out,
				  int outlen)
{
	u32 in[MLX5_ST_SZ_DW(query_adapter_in)] = {0};

	MLX5_SET(query_adapter_in, in, opcode, MLX5_CMD_OP_QUERY_ADAPTER);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, outlen);
}

int mlx5_query_board_id(struct mlx5_core_dev *dev)
{
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_adapter_out);
	int err;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5_cmd_query_adapter(dev, out, outlen);
	if (err)
		goto out;

	memcpy(dev->board_id,
	       MLX5_ADDR_OF(query_adapter_out, out,
			    query_adapter_struct.vsd_contd_psid),
	       MLX5_FLD_SZ_BYTES(query_adapter_out,
				 query_adapter_struct.vsd_contd_psid));

out:
	kfree(out);
	return err;
}

int mlx5_core_query_vendor_id(struct mlx5_core_dev *mdev, u32 *vendor_id)
{
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_adapter_out);
	int err;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	err = mlx5_cmd_query_adapter(mdev, out, outlen);
	if (err)
		goto out;

	*vendor_id = MLX5_GET(query_adapter_out, out,
			      query_adapter_struct.ieee_vendor_id);
out:
	kfree(out);
	return err;
}
EXPORT_SYMBOL(mlx5_core_query_vendor_id);

static int mlx5_get_pcam_reg(struct mlx5_core_dev *dev)
{
	return mlx5_query_pcam_reg(dev, dev->caps.pcam,
				   MLX5_PCAM_FEATURE_ENHANCED_FEATURES,
				   MLX5_PCAM_REGS_5000_TO_507F);
}

static int mlx5_get_mcam_reg(struct mlx5_core_dev *dev)
{
	return mlx5_query_mcam_reg(dev, dev->caps.mcam,
				   MLX5_MCAM_FEATURE_ENHANCED_FEATURES,
				   MLX5_MCAM_REGS_FIRST_128);
}

static int mlx5_get_qcam_reg(struct mlx5_core_dev *dev)
{
	return mlx5_query_qcam_reg(dev, dev->caps.qcam,
				   MLX5_QCAM_FEATURE_ENHANCED_FEATURES,
				   MLX5_QCAM_REGS_FIRST_128);
}

int mlx5_query_hca_caps(struct mlx5_core_dev *dev)
{
	int err;

	err = mlx5_core_get_caps(dev, MLX5_CAP_GENERAL);
	if (err)
		return err;

	if (MLX5_CAP_GEN(dev, eth_net_offloads)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ETHERNET_OFFLOADS);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, ipoib_enhanced_offloads)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_IPOIB_ENHANCED_OFFLOADS);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, pg)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ODP);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, atomic)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ATOMIC);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, roce)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ROCE);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, nic_flow_table) ||
	    MLX5_CAP_GEN(dev, ipoib_enhanced_offloads)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_FLOW_TABLE);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, vport_group_manager) &&
	    MLX5_CAP_GEN(dev, eswitch_flow_table)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ESWITCH_FLOW_TABLE);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, eswitch_flow_table)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_ESWITCH);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, vector_calc)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_VECTOR_CALC);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, qos)) {
		err = mlx5_core_get_caps(dev, MLX5_CAP_QOS);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, pcam_reg))
		mlx5_get_pcam_reg(dev);

	if (MLX5_CAP_GEN(dev, mcam_reg))
		mlx5_get_mcam_reg(dev);

	if (MLX5_CAP_GEN(dev, qcam_reg))
		mlx5_get_qcam_reg(dev);

	return 0;
}

int mlx5_cmd_init_hca(struct mlx5_core_dev *dev)
{
	u32 out[MLX5_ST_SZ_DW(init_hca_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(init_hca_in)]   = {0};

	MLX5_SET(init_hca_in, in, opcode, MLX5_CMD_OP_INIT_HCA);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_cmd_teardown_hca(struct mlx5_core_dev *dev)
{
	u32 out[MLX5_ST_SZ_DW(teardown_hca_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(teardown_hca_in)]   = {0};

	MLX5_SET(teardown_hca_in, in, opcode, MLX5_CMD_OP_TEARDOWN_HCA);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_cmd_force_teardown_hca(struct mlx5_core_dev *dev)
{
	u32 out[MLX5_ST_SZ_DW(teardown_hca_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(teardown_hca_in)] = {0};
	int force_state;
	int ret;

	if (!MLX5_CAP_GEN(dev, force_teardown)) {
		mlx5_core_dbg(dev, "force teardown is not supported in the firmware\n");
		return -EOPNOTSUPP;
	}

	MLX5_SET(teardown_hca_in, in, opcode, MLX5_CMD_OP_TEARDOWN_HCA);
	MLX5_SET(teardown_hca_in, in, profile, MLX5_TEARDOWN_HCA_IN_PROFILE_FORCE_CLOSE);

	ret = mlx5_cmd_exec_polling(dev, in, sizeof(in), out, sizeof(out));
	if (ret)
		return ret;

	force_state = MLX5_GET(teardown_hca_out, out, force_state);
	if (force_state == MLX5_TEARDOWN_HCA_OUT_FORCE_STATE_FAIL) {
		mlx5_core_err(dev, "teardown with force mode failed\n");
		return -EIO;
	}

	return 0;
}

enum mlxsw_reg_mcc_instruction {
	MLX5_REG_MCC_INSTRUCTION_LOCK_UPDATE_HANDLE = 0x01,
	MLX5_REG_MCC_INSTRUCTION_RELEASE_UPDATE_HANDLE = 0x02,
	MLX5_REG_MCC_INSTRUCTION_UPDATE_COMPONENT = 0x03,
	MLX5_REG_MCC_INSTRUCTION_VERIFY_COMPONENT = 0x04,
	MLX5_REG_MCC_INSTRUCTION_ACTIVATE = 0x06,
	MLX5_REG_MCC_INSTRUCTION_CANCEL = 0x08,
};

static int mlx5_reg_mcc_set(struct mlx5_core_dev *dev,
			    enum mlxsw_reg_mcc_instruction instr,
			    u16 component_index, u32 update_handle,
			    u32 component_size)
{
	u32 out[MLX5_ST_SZ_DW(mcc_reg)];
	u32 in[MLX5_ST_SZ_DW(mcc_reg)];

	memset(in, 0, sizeof(in));

	MLX5_SET(mcc_reg, in, instruction, instr);
	MLX5_SET(mcc_reg, in, component_index, component_index);
	MLX5_SET(mcc_reg, in, update_handle, update_handle);
	MLX5_SET(mcc_reg, in, component_size, component_size);

	return mlx5_core_access_reg(dev, in, sizeof(in), out,
				    sizeof(out), MLX5_REG_MCC, 0, 1);
}

static int mlx5_reg_mcc_query(struct mlx5_core_dev *dev,
			      u32 *update_handle, u8 *error_code,
			      u8 *control_state)
{
	u32 out[MLX5_ST_SZ_DW(mcc_reg)];
	u32 in[MLX5_ST_SZ_DW(mcc_reg)];
	int err;

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));
	MLX5_SET(mcc_reg, in, update_handle, *update_handle);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_MCC, 0, 0);
	if (err)
		goto out;

	*update_handle = MLX5_GET(mcc_reg, out, update_handle);
	*error_code = MLX5_GET(mcc_reg, out, error_code);
	*control_state = MLX5_GET(mcc_reg, out, control_state);

out:
	return err;
}

static int mlx5_reg_mcda_set(struct mlx5_core_dev *dev,
			     u32 update_handle,
			     u32 offset, u16 size,
			     u8 *data)
{
	int err, in_size = MLX5_ST_SZ_BYTES(mcda_reg) + size;
	u32 out[MLX5_ST_SZ_DW(mcda_reg)];
	int i, j, dw_size = size >> 2;
	__be32 data_element;
	u32 *in;

	in = kzalloc(in_size, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(mcda_reg, in, update_handle, update_handle);
	MLX5_SET(mcda_reg, in, offset, offset);
	MLX5_SET(mcda_reg, in, size, size);

	for (i = 0; i < dw_size; i++) {
		j = i * 4;
		data_element = htonl(*(u32 *)&data[j]);
		memcpy(MLX5_ADDR_OF(mcda_reg, in, data) + j, &data_element, 4);
	}

	err = mlx5_core_access_reg(dev, in, in_size, out,
				   sizeof(out), MLX5_REG_MCDA, 0, 1);
	kfree(in);
	return err;
}

static int mlx5_reg_mcqi_query(struct mlx5_core_dev *dev,
			       u16 component_index,
			       u32 *max_component_size,
			       u8 *log_mcda_word_size,
			       u16 *mcda_max_write_size)
{
	u32 out[MLX5_ST_SZ_DW(mcqi_reg) + MLX5_ST_SZ_DW(mcqi_cap)];
	int offset = MLX5_ST_SZ_DW(mcqi_reg);
	u32 in[MLX5_ST_SZ_DW(mcqi_reg)];
	int err;

	memset(in, 0, sizeof(in));
	memset(out, 0, sizeof(out));

	MLX5_SET(mcqi_reg, in, component_index, component_index);
	MLX5_SET(mcqi_reg, in, data_size, MLX5_ST_SZ_BYTES(mcqi_cap));

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_MCQI, 0, 0);
	if (err)
		goto out;

	*max_component_size = MLX5_GET(mcqi_cap, out + offset, max_component_size);
	*log_mcda_word_size = MLX5_GET(mcqi_cap, out + offset, log_mcda_word_size);
	*mcda_max_write_size = MLX5_GET(mcqi_cap, out + offset, mcda_max_write_size);

out:
	return err;
}

struct mlx5_mlxfw_dev {
	struct mlxfw_dev mlxfw_dev;
	struct mlx5_core_dev *mlx5_core_dev;
};

static int mlx5_component_query(struct mlxfw_dev *mlxfw_dev,
				u16 component_index, u32 *p_max_size,
				u8 *p_align_bits, u16 *p_max_write_size)
{
	struct mlx5_mlxfw_dev *mlx5_mlxfw_dev =
		container_of(mlxfw_dev, struct mlx5_mlxfw_dev, mlxfw_dev);
	struct mlx5_core_dev *dev = mlx5_mlxfw_dev->mlx5_core_dev;

	return mlx5_reg_mcqi_query(dev, component_index, p_max_size,
				   p_align_bits, p_max_write_size);
}

static int mlx5_fsm_lock(struct mlxfw_dev *mlxfw_dev, u32 *fwhandle)
{
	struct mlx5_mlxfw_dev *mlx5_mlxfw_dev =
		container_of(mlxfw_dev, struct mlx5_mlxfw_dev, mlxfw_dev);
	struct mlx5_core_dev *dev = mlx5_mlxfw_dev->mlx5_core_dev;
	u8 control_state, error_code;
	int err;

	*fwhandle = 0;
	err = mlx5_reg_mcc_query(dev, fwhandle, &error_code, &control_state);
	if (err)
		return err;

	if (control_state != MLXFW_FSM_STATE_IDLE)
		return -EBUSY;

	return mlx5_reg_mcc_set(dev, MLX5_REG_MCC_INSTRUCTION_LOCK_UPDATE_HANDLE,
				0, *fwhandle, 0);
}

static int mlx5_fsm_component_update(struct mlxfw_dev *mlxfw_dev, u32 fwhandle,
				     u16 component_index, u32 component_size)
{
	struct mlx5_mlxfw_dev *mlx5_mlxfw_dev =
		container_of(mlxfw_dev, struct mlx5_mlxfw_dev, mlxfw_dev);
	struct mlx5_core_dev *dev = mlx5_mlxfw_dev->mlx5_core_dev;

	return mlx5_reg_mcc_set(dev, MLX5_REG_MCC_INSTRUCTION_UPDATE_COMPONENT,
				component_index, fwhandle, component_size);
}

static int mlx5_fsm_block_download(struct mlxfw_dev *mlxfw_dev, u32 fwhandle,
				   u8 *data, u16 size, u32 offset)
{
	struct mlx5_mlxfw_dev *mlx5_mlxfw_dev =
		container_of(mlxfw_dev, struct mlx5_mlxfw_dev, mlxfw_dev);
	struct mlx5_core_dev *dev = mlx5_mlxfw_dev->mlx5_core_dev;

	return mlx5_reg_mcda_set(dev, fwhandle, offset, size, data);
}

static int mlx5_fsm_component_verify(struct mlxfw_dev *mlxfw_dev, u32 fwhandle,
				     u16 component_index)
{
	struct mlx5_mlxfw_dev *mlx5_mlxfw_dev =
		container_of(mlxfw_dev, struct mlx5_mlxfw_dev, mlxfw_dev);
	struct mlx5_core_dev *dev = mlx5_mlxfw_dev->mlx5_core_dev;

	return mlx5_reg_mcc_set(dev, MLX5_REG_MCC_INSTRUCTION_VERIFY_COMPONENT,
				component_index, fwhandle, 0);
}

static int mlx5_fsm_activate(struct mlxfw_dev *mlxfw_dev, u32 fwhandle)
{
	struct mlx5_mlxfw_dev *mlx5_mlxfw_dev =
		container_of(mlxfw_dev, struct mlx5_mlxfw_dev, mlxfw_dev);
	struct mlx5_core_dev *dev = mlx5_mlxfw_dev->mlx5_core_dev;

	return mlx5_reg_mcc_set(dev, MLX5_REG_MCC_INSTRUCTION_ACTIVATE,	0,
				fwhandle, 0);
}

static int mlx5_fsm_query_state(struct mlxfw_dev *mlxfw_dev, u32 fwhandle,
				enum mlxfw_fsm_state *fsm_state,
				enum mlxfw_fsm_state_err *fsm_state_err)
{
	struct mlx5_mlxfw_dev *mlx5_mlxfw_dev =
		container_of(mlxfw_dev, struct mlx5_mlxfw_dev, mlxfw_dev);
	struct mlx5_core_dev *dev = mlx5_mlxfw_dev->mlx5_core_dev;
	u8 control_state, error_code;
	int err;

	err = mlx5_reg_mcc_query(dev, &fwhandle, &error_code, &control_state);
	if (err)
		return err;

	*fsm_state = control_state;
	*fsm_state_err = min_t(enum mlxfw_fsm_state_err, error_code,
			       MLXFW_FSM_STATE_ERR_MAX);
	return 0;
}

static void mlx5_fsm_cancel(struct mlxfw_dev *mlxfw_dev, u32 fwhandle)
{
	struct mlx5_mlxfw_dev *mlx5_mlxfw_dev =
		container_of(mlxfw_dev, struct mlx5_mlxfw_dev, mlxfw_dev);
	struct mlx5_core_dev *dev = mlx5_mlxfw_dev->mlx5_core_dev;

	mlx5_reg_mcc_set(dev, MLX5_REG_MCC_INSTRUCTION_CANCEL, 0, fwhandle, 0);
}

static void mlx5_fsm_release(struct mlxfw_dev *mlxfw_dev, u32 fwhandle)
{
	struct mlx5_mlxfw_dev *mlx5_mlxfw_dev =
		container_of(mlxfw_dev, struct mlx5_mlxfw_dev, mlxfw_dev);
	struct mlx5_core_dev *dev = mlx5_mlxfw_dev->mlx5_core_dev;

	mlx5_reg_mcc_set(dev, MLX5_REG_MCC_INSTRUCTION_RELEASE_UPDATE_HANDLE, 0,
			 fwhandle, 0);
}

static const struct mlxfw_dev_ops mlx5_mlxfw_dev_ops = {
	.component_query	= mlx5_component_query,
	.fsm_lock		= mlx5_fsm_lock,
	.fsm_component_update	= mlx5_fsm_component_update,
	.fsm_block_download	= mlx5_fsm_block_download,
	.fsm_component_verify	= mlx5_fsm_component_verify,
	.fsm_activate		= mlx5_fsm_activate,
	.fsm_query_state	= mlx5_fsm_query_state,
	.fsm_cancel		= mlx5_fsm_cancel,
	.fsm_release		= mlx5_fsm_release
};

int mlx5_firmware_flash(struct mlx5_core_dev *dev,
			const struct firmware *firmware)
{
	struct mlx5_mlxfw_dev mlx5_mlxfw_dev = {
		.mlxfw_dev = {
			.ops = &mlx5_mlxfw_dev_ops,
			.psid = dev->board_id,
			.psid_size = strlen(dev->board_id),
		},
		.mlx5_core_dev = dev
	};

	if (!MLX5_CAP_GEN(dev, mcam_reg)  ||
	    !MLX5_CAP_MCAM_REG(dev, mcqi) ||
	    !MLX5_CAP_MCAM_REG(dev, mcc)  ||
	    !MLX5_CAP_MCAM_REG(dev, mcda)) {
		pr_info("%s flashing isn't supported by the running FW\n", __func__);
		return -EOPNOTSUPP;
	}

	return mlxfw_firmware_flash(&mlx5_mlxfw_dev.mlxfw_dev, firmware);
}
