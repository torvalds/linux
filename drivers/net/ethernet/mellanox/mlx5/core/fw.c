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
#include <linux/mlx5/eswitch.h>
#include "mlx5_core.h"
#include "../../mlxfw/mlxfw.h"
#include "lib/tout.h"

enum {
	MCQS_IDENTIFIER_BOOT_IMG	= 0x1,
	MCQS_IDENTIFIER_OEM_NVCONFIG	= 0x4,
	MCQS_IDENTIFIER_MLNX_NVCONFIG	= 0x5,
	MCQS_IDENTIFIER_CS_TOKEN	= 0x6,
	MCQS_IDENTIFIER_DBG_TOKEN	= 0x7,
	MCQS_IDENTIFIER_GEARBOX		= 0xA,
};

enum {
	MCQS_UPDATE_STATE_IDLE,
	MCQS_UPDATE_STATE_IN_PROGRESS,
	MCQS_UPDATE_STATE_APPLIED,
	MCQS_UPDATE_STATE_ACTIVE,
	MCQS_UPDATE_STATE_ACTIVE_PENDING_RESET,
	MCQS_UPDATE_STATE_FAILED,
	MCQS_UPDATE_STATE_CANCELED,
	MCQS_UPDATE_STATE_BUSY,
};

enum {
	MCQI_INFO_TYPE_CAPABILITIES	  = 0x0,
	MCQI_INFO_TYPE_VERSION		  = 0x1,
	MCQI_INFO_TYPE_ACTIVATION_METHOD  = 0x5,
};

enum {
	MCQI_FW_RUNNING_VERSION = 0,
	MCQI_FW_STORED_VERSION  = 1,
};

int mlx5_query_board_id(struct mlx5_core_dev *dev)
{
	u32 *out;
	int outlen = MLX5_ST_SZ_BYTES(query_adapter_out);
	u32 in[MLX5_ST_SZ_DW(query_adapter_in)] = {};
	int err;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	MLX5_SET(query_adapter_in, in, opcode, MLX5_CMD_OP_QUERY_ADAPTER);
	err = mlx5_cmd_exec_inout(dev, query_adapter, in, out);
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
	u32 in[MLX5_ST_SZ_DW(query_adapter_in)] = {};
	int err;

	out = kzalloc(outlen, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	MLX5_SET(query_adapter_in, in, opcode, MLX5_CMD_OP_QUERY_ADAPTER);
	err = mlx5_cmd_exec_inout(mdev, query_adapter, in, out);
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

static int mlx5_get_mcam_access_reg_group(struct mlx5_core_dev *dev,
					  enum mlx5_mcam_reg_groups group)
{
	return mlx5_query_mcam_reg(dev, dev->caps.mcam[group],
				   MLX5_MCAM_FEATURE_ENHANCED_FEATURES, group);
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

	err = mlx5_core_get_caps_mode(dev, MLX5_CAP_GENERAL, HCA_CAP_OPMOD_GET_CUR);
	if (err)
		return err;

	if (MLX5_CAP_GEN(dev, port_selection_cap)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_PORT_SELECTION, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, hca_cap_2)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_GENERAL_2, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, eth_net_offloads)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_ETHERNET_OFFLOADS,
					      HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, ipoib_enhanced_offloads)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_IPOIB_ENHANCED_OFFLOADS,
					      HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, pg)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_ODP, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, atomic)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_ATOMIC, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, roce)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_ROCE, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, nic_flow_table) ||
	    MLX5_CAP_GEN(dev, ipoib_enhanced_offloads)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_FLOW_TABLE, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_ESWITCH_MANAGER(dev)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_ESWITCH_FLOW_TABLE,
					      HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;

		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_ESWITCH, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, qos)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_QOS, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, debug))
		mlx5_core_get_caps_mode(dev, MLX5_CAP_DEBUG, HCA_CAP_OPMOD_GET_CUR);

	if (MLX5_CAP_GEN(dev, pcam_reg))
		mlx5_get_pcam_reg(dev);

	if (MLX5_CAP_GEN(dev, mcam_reg)) {
		mlx5_get_mcam_access_reg_group(dev, MLX5_MCAM_REGS_FIRST_128);
		mlx5_get_mcam_access_reg_group(dev, MLX5_MCAM_REGS_0x9100_0x917F);
		mlx5_get_mcam_access_reg_group(dev, MLX5_MCAM_REGS_0x9180_0x91FF);
	}

	if (MLX5_CAP_GEN(dev, qcam_reg))
		mlx5_get_qcam_reg(dev);

	if (MLX5_CAP_GEN(dev, device_memory)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_DEV_MEM, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, event_cap)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_DEV_EVENT, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, tls_tx) || MLX5_CAP_GEN(dev, tls_rx)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_TLS, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN_64(dev, general_obj_types) &
		MLX5_GENERAL_OBJ_TYPES_CAP_VIRTIO_NET_Q) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_VDPA_EMULATION, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, ipsec_offload)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_IPSEC, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, crypto)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_CRYPTO, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN_64(dev, general_obj_types) &
	    MLX5_GENERAL_OBJ_TYPES_CAP_MACSEC_OFFLOAD) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_MACSEC, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, adv_virtualization)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_ADV_VIRTUALIZATION,
					      HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, shampo)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_SHAMPO, HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	if (MLX5_CAP_GEN(dev, adv_rdma)) {
		err = mlx5_core_get_caps_mode(dev, MLX5_CAP_ADV_RDMA,
					      HCA_CAP_OPMOD_GET_CUR);
		if (err)
			return err;
	}

	return 0;
}

int mlx5_cmd_init_hca(struct mlx5_core_dev *dev, u32 *sw_owner_id)
{
	u32 in[MLX5_ST_SZ_DW(init_hca_in)] = {};
	int i;

	MLX5_SET(init_hca_in, in, opcode, MLX5_CMD_OP_INIT_HCA);

	if (MLX5_CAP_GEN(dev, sw_owner_id)) {
		for (i = 0; i < 4; i++)
			MLX5_ARRAY_SET(init_hca_in, in, sw_owner_id, i,
				       sw_owner_id[i]);
	}

	if (MLX5_CAP_GEN_2_MAX(dev, sw_vhca_id_valid) &&
	    dev->priv.sw_vhca_id > 0)
		MLX5_SET(init_hca_in, in, sw_vhca_id, dev->priv.sw_vhca_id);

	return mlx5_cmd_exec_in(dev, init_hca, in);
}

int mlx5_cmd_teardown_hca(struct mlx5_core_dev *dev)
{
	u32 in[MLX5_ST_SZ_DW(teardown_hca_in)] = {};

	MLX5_SET(teardown_hca_in, in, opcode, MLX5_CMD_OP_TEARDOWN_HCA);
	return mlx5_cmd_exec_in(dev, teardown_hca, in);
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

	force_state = MLX5_GET(teardown_hca_out, out, state);
	if (force_state == MLX5_TEARDOWN_HCA_OUT_FORCE_STATE_FAIL) {
		mlx5_core_warn(dev, "teardown with force mode failed, doing normal teardown\n");
		return -EIO;
	}

	return 0;
}

int mlx5_cmd_fast_teardown_hca(struct mlx5_core_dev *dev)
{
	unsigned long end, delay_ms = mlx5_tout_ms(dev, TEARDOWN);
	u32 out[MLX5_ST_SZ_DW(teardown_hca_out)] = {};
	u32 in[MLX5_ST_SZ_DW(teardown_hca_in)] = {};
	int state;
	int ret;

	if (!MLX5_CAP_GEN(dev, fast_teardown)) {
		mlx5_core_dbg(dev, "fast teardown is not supported in the firmware\n");
		return -EOPNOTSUPP;
	}

	MLX5_SET(teardown_hca_in, in, opcode, MLX5_CMD_OP_TEARDOWN_HCA);
	MLX5_SET(teardown_hca_in, in, profile,
		 MLX5_TEARDOWN_HCA_IN_PROFILE_PREPARE_FAST_TEARDOWN);

	ret = mlx5_cmd_exec_inout(dev, teardown_hca, in, out);
	if (ret)
		return ret;

	state = MLX5_GET(teardown_hca_out, out, state);
	if (state == MLX5_TEARDOWN_HCA_OUT_FORCE_STATE_FAIL) {
		mlx5_core_warn(dev, "teardown with fast mode failed\n");
		return -EIO;
	}

	mlx5_set_nic_state(dev, MLX5_INITIAL_SEG_NIC_INTERFACE_DISABLED);

	/* Loop until device state turns to disable */
	end = jiffies + msecs_to_jiffies(delay_ms);
	do {
		if (mlx5_get_nic_state(dev) == MLX5_INITIAL_SEG_NIC_INTERFACE_DISABLED)
			break;
		if (pci_channel_offline(dev->pdev)) {
			mlx5_core_err(dev, "PCI channel offline, stop waiting for NIC IFC\n");
			return -EACCES;
		}

		cond_resched();
	} while (!time_after(jiffies, end));

	if (mlx5_get_nic_state(dev) != MLX5_INITIAL_SEG_NIC_INTERFACE_DISABLED) {
		dev_err(&dev->pdev->dev, "NIC IFC still %d after %lums.\n",
			mlx5_get_nic_state(dev), delay_ms);
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
			       u16 component_index, bool read_pending,
			       u8 info_type, u16 data_size, void *mcqi_data)
{
	u32 out[MLX5_ST_SZ_DW(mcqi_reg) + MLX5_UN_SZ_DW(mcqi_reg_data)] = {};
	u32 in[MLX5_ST_SZ_DW(mcqi_reg)] = {};
	void *data;
	int err;

	MLX5_SET(mcqi_reg, in, component_index, component_index);
	MLX5_SET(mcqi_reg, in, read_pending_component, read_pending);
	MLX5_SET(mcqi_reg, in, info_type, info_type);
	MLX5_SET(mcqi_reg, in, data_size, data_size);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   MLX5_ST_SZ_BYTES(mcqi_reg) + data_size,
				   MLX5_REG_MCQI, 0, 0);
	if (err)
		return err;

	data = MLX5_ADDR_OF(mcqi_reg, out, data);
	memcpy(mcqi_data, data, data_size);

	return 0;
}

static int mlx5_reg_mcqi_caps_query(struct mlx5_core_dev *dev, u16 component_index,
				    u32 *max_component_size, u8 *log_mcda_word_size,
				    u16 *mcda_max_write_size)
{
	u32 mcqi_reg[MLX5_ST_SZ_DW(mcqi_cap)] = {};
	int err;

	err = mlx5_reg_mcqi_query(dev, component_index, 0,
				  MCQI_INFO_TYPE_CAPABILITIES,
				  MLX5_ST_SZ_BYTES(mcqi_cap), mcqi_reg);
	if (err)
		return err;

	*max_component_size = MLX5_GET(mcqi_cap, mcqi_reg, max_component_size);
	*log_mcda_word_size = MLX5_GET(mcqi_cap, mcqi_reg, log_mcda_word_size);
	*mcda_max_write_size = MLX5_GET(mcqi_cap, mcqi_reg, mcda_max_write_size);

	return 0;
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

	if (!MLX5_CAP_GEN(dev, mcam_reg) || !MLX5_CAP_MCAM_REG(dev, mcqi)) {
		mlx5_core_warn(dev, "caps query isn't supported by running FW\n");
		return -EOPNOTSUPP;
	}

	return mlx5_reg_mcqi_caps_query(dev, component_index, p_max_size,
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

static int mlx5_fsm_reactivate(struct mlxfw_dev *mlxfw_dev, u8 *status)
{
	struct mlx5_mlxfw_dev *mlx5_mlxfw_dev =
		container_of(mlxfw_dev, struct mlx5_mlxfw_dev, mlxfw_dev);
	struct mlx5_core_dev *dev = mlx5_mlxfw_dev->mlx5_core_dev;
	u32 out[MLX5_ST_SZ_DW(mirc_reg)];
	u32 in[MLX5_ST_SZ_DW(mirc_reg)];
	unsigned long exp_time;
	int err;

	exp_time = jiffies + msecs_to_jiffies(mlx5_tout_ms(dev, FSM_REACTIVATE));

	if (!MLX5_CAP_MCAM_REG2(dev, mirc))
		return -EOPNOTSUPP;

	memset(in, 0, sizeof(in));

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   sizeof(out), MLX5_REG_MIRC, 0, 1);
	if (err)
		return err;

	do {
		memset(out, 0, sizeof(out));
		err = mlx5_core_access_reg(dev, in, sizeof(in), out,
					   sizeof(out), MLX5_REG_MIRC, 0, 0);
		if (err)
			return err;

		*status = MLX5_GET(mirc_reg, out, status_code);
		if (*status != MLXFW_FSM_REACTIVATE_STATUS_BUSY)
			return 0;

		msleep(20);
	} while (time_before(jiffies, exp_time));

	return 0;
}

static const struct mlxfw_dev_ops mlx5_mlxfw_dev_ops = {
	.component_query	= mlx5_component_query,
	.fsm_lock		= mlx5_fsm_lock,
	.fsm_component_update	= mlx5_fsm_component_update,
	.fsm_block_download	= mlx5_fsm_block_download,
	.fsm_component_verify	= mlx5_fsm_component_verify,
	.fsm_activate		= mlx5_fsm_activate,
	.fsm_reactivate		= mlx5_fsm_reactivate,
	.fsm_query_state	= mlx5_fsm_query_state,
	.fsm_cancel		= mlx5_fsm_cancel,
	.fsm_release		= mlx5_fsm_release
};

int mlx5_firmware_flash(struct mlx5_core_dev *dev,
			const struct firmware *firmware,
			struct netlink_ext_ack *extack)
{
	struct mlx5_mlxfw_dev mlx5_mlxfw_dev = {
		.mlxfw_dev = {
			.ops = &mlx5_mlxfw_dev_ops,
			.psid = dev->board_id,
			.psid_size = strlen(dev->board_id),
			.devlink = priv_to_devlink(dev),
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

	return mlxfw_firmware_flash(&mlx5_mlxfw_dev.mlxfw_dev,
				    firmware, extack);
}

static int mlx5_reg_mcqi_version_query(struct mlx5_core_dev *dev,
				       u16 component_index, bool read_pending,
				       u32 *mcqi_version_out)
{
	return mlx5_reg_mcqi_query(dev, component_index, read_pending,
				   MCQI_INFO_TYPE_VERSION,
				   MLX5_ST_SZ_BYTES(mcqi_version),
				   mcqi_version_out);
}

static int mlx5_reg_mcqs_query(struct mlx5_core_dev *dev, u32 *out,
			       u16 component_index)
{
	u8 out_sz = MLX5_ST_SZ_BYTES(mcqs_reg);
	u32 in[MLX5_ST_SZ_DW(mcqs_reg)] = {};
	int err;

	memset(out, 0, out_sz);

	MLX5_SET(mcqs_reg, in, component_index, component_index);

	err = mlx5_core_access_reg(dev, in, sizeof(in), out,
				   out_sz, MLX5_REG_MCQS, 0, 0);
	return err;
}

/* scans component index sequentially, to find the boot img index */
static int mlx5_get_boot_img_component_index(struct mlx5_core_dev *dev)
{
	u32 out[MLX5_ST_SZ_DW(mcqs_reg)] = {};
	u16 identifier, component_idx = 0;
	bool quit;
	int err;

	do {
		err = mlx5_reg_mcqs_query(dev, out, component_idx);
		if (err)
			return err;

		identifier = MLX5_GET(mcqs_reg, out, identifier);
		quit = !!MLX5_GET(mcqs_reg, out, last_index_flag);
		quit |= identifier == MCQS_IDENTIFIER_BOOT_IMG;
	} while (!quit && ++component_idx);

	if (identifier != MCQS_IDENTIFIER_BOOT_IMG) {
		mlx5_core_warn(dev, "mcqs: can't find boot_img component ix, last scanned idx %d\n",
			       component_idx);
		return -EOPNOTSUPP;
	}

	return component_idx;
}

static int
mlx5_fw_image_pending(struct mlx5_core_dev *dev,
		      int component_index,
		      bool *pending_version_exists)
{
	u32 out[MLX5_ST_SZ_DW(mcqs_reg)];
	u8 component_update_state;
	int err;

	err = mlx5_reg_mcqs_query(dev, out, component_index);
	if (err)
		return err;

	component_update_state = MLX5_GET(mcqs_reg, out, component_update_state);

	if (component_update_state == MCQS_UPDATE_STATE_IDLE) {
		*pending_version_exists = false;
	} else if (component_update_state == MCQS_UPDATE_STATE_ACTIVE_PENDING_RESET) {
		*pending_version_exists = true;
	} else {
		mlx5_core_warn(dev,
			       "mcqs: can't read pending fw version while fw state is %d\n",
			       component_update_state);
		return -ENODATA;
	}
	return 0;
}

int mlx5_fw_version_query(struct mlx5_core_dev *dev,
			  u32 *running_ver, u32 *pending_ver)
{
	u32 reg_mcqi_version[MLX5_ST_SZ_DW(mcqi_version)] = {};
	bool pending_version_exists;
	int component_index;
	int err;

	if (!MLX5_CAP_GEN(dev, mcam_reg) || !MLX5_CAP_MCAM_REG(dev, mcqi) ||
	    !MLX5_CAP_MCAM_REG(dev, mcqs)) {
		mlx5_core_warn(dev, "fw query isn't supported by the FW\n");
		return -EOPNOTSUPP;
	}

	component_index = mlx5_get_boot_img_component_index(dev);
	if (component_index < 0)
		return component_index;

	err = mlx5_reg_mcqi_version_query(dev, component_index,
					  MCQI_FW_RUNNING_VERSION,
					  reg_mcqi_version);
	if (err)
		return err;

	*running_ver = MLX5_GET(mcqi_version, reg_mcqi_version, version);

	err = mlx5_fw_image_pending(dev, component_index, &pending_version_exists);
	if (err)
		return err;

	if (!pending_version_exists) {
		*pending_ver = 0;
		return 0;
	}

	err = mlx5_reg_mcqi_version_query(dev, component_index,
					  MCQI_FW_STORED_VERSION,
					  reg_mcqi_version);
	if (err)
		return err;

	*pending_ver = MLX5_GET(mcqi_version, reg_mcqi_version, version);

	return 0;
}
