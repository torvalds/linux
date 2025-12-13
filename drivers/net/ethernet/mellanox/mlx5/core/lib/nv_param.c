// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2024, NVIDIA CORPORATION & AFFILIATES. All rights reserved. */

#include "nv_param.h"
#include "mlx5_core.h"

enum {
	MLX5_CLASS_0_CTRL_ID_NV_GLOBAL_PCI_CONF               = 0x80,
	MLX5_CLASS_0_CTRL_ID_NV_GLOBAL_PCI_CAP                = 0x81,
	MLX5_CLASS_0_CTRL_ID_NV_SW_OFFLOAD_CONFIG             = 0x10a,

	MLX5_CLASS_3_CTRL_ID_NV_PF_PCI_CONF                   = 0x80,
};

struct mlx5_ifc_configuration_item_type_class_global_bits {
	u8         type_class[0x8];
	u8         parameter_index[0x18];
};

struct mlx5_ifc_configuration_item_type_class_per_host_pf_bits {
	u8         type_class[0x8];
	u8         pf_index[0x6];
	u8         pci_bus_index[0x8];
	u8         parameter_index[0xa];
};

union mlx5_ifc_config_item_type_auto_bits {
	struct mlx5_ifc_configuration_item_type_class_global_bits
				configuration_item_type_class_global;
	struct mlx5_ifc_configuration_item_type_class_per_host_pf_bits
				configuration_item_type_class_per_host_pf;
	u8 reserved_at_0[0x20];
};

struct mlx5_ifc_config_item_bits {
	u8         valid[0x2];
	u8         priority[0x2];
	u8         header_type[0x2];
	u8         ovr_en[0x1];
	u8         rd_en[0x1];
	u8         access_mode[0x2];
	u8         reserved_at_a[0x1];
	u8         writer_id[0x5];
	u8         version[0x4];
	u8         reserved_at_14[0x2];
	u8         host_id_valid[0x1];
	u8         length[0x9];

	union mlx5_ifc_config_item_type_auto_bits type;

	u8         reserved_at_40[0x10];
	u8         crc16[0x10];
};

struct mlx5_ifc_mnvda_reg_bits {
	struct mlx5_ifc_config_item_bits configuration_item_header;

	u8         configuration_item_data[64][0x20];
};

struct mlx5_ifc_nv_global_pci_conf_bits {
	u8         sriov_valid[0x1];
	u8         reserved_at_1[0x10];
	u8         per_pf_total_vf[0x1];
	u8         reserved_at_12[0xe];

	u8         sriov_en[0x1];
	u8         reserved_at_21[0xf];
	u8         total_vfs[0x10];

	u8         reserved_at_40[0x20];
};

struct mlx5_ifc_nv_global_pci_cap_bits {
	u8         max_vfs_per_pf_valid[0x1];
	u8         reserved_at_1[0x13];
	u8         per_pf_total_vf_supported[0x1];
	u8         reserved_at_15[0xb];

	u8         sriov_support[0x1];
	u8         reserved_at_21[0xf];
	u8         max_vfs_per_pf[0x10];

	u8         reserved_at_40[0x60];
};

struct mlx5_ifc_nv_pf_pci_conf_bits {
	u8         reserved_at_0[0x9];
	u8         pf_total_vf_en[0x1];
	u8         reserved_at_a[0x16];

	u8         reserved_at_20[0x20];

	u8         reserved_at_40[0x10];
	u8         total_vf[0x10];

	u8         reserved_at_60[0x20];
};

struct mlx5_ifc_nv_sw_offload_conf_bits {
	u8         ip_over_vxlan_port[0x10];
	u8         tunnel_ecn_copy_offload_disable[0x1];
	u8         pci_atomic_mode[0x3];
	u8         sr_enable[0x1];
	u8         ptp_cyc2realtime[0x1];
	u8         vector_calc_disable[0x1];
	u8         uctx_en[0x1];
	u8         prio_tag_required_en[0x1];
	u8         esw_fdb_ipv4_ttl_modify_enable[0x1];
	u8         mkey_by_name[0x1];
	u8         ip_over_vxlan_en[0x1];
	u8         one_qp_per_recovery[0x1];
	u8         cqe_compression[0x3];
	u8         tunnel_udp_entropy_proto_disable[0x1];
	u8         reserved_at_21[0x1];
	u8         ar_enable[0x1];
	u8         log_max_outstanding_wqe[0x5];
	u8         vf_migration[0x2];
	u8         log_tx_psn_win[0x6];
	u8         lro_log_timeout3[0x4];
	u8         lro_log_timeout2[0x4];
	u8         lro_log_timeout1[0x4];
	u8         lro_log_timeout0[0x4];
};

#define MNVDA_HDR_SZ \
	(MLX5_ST_SZ_BYTES(mnvda_reg) - \
	 MLX5_BYTE_OFF(mnvda_reg, configuration_item_data))

#define MLX5_SET_CFG_ITEM_TYPE(_cls_name, _mnvda_ptr, _field, _val) \
	MLX5_SET(mnvda_reg, _mnvda_ptr, \
		 configuration_item_header.type.configuration_item_type_class_##_cls_name._field, \
		 _val)

#define MLX5_SET_CFG_HDR_LEN(_mnvda_ptr, _cls_name) \
	MLX5_SET(mnvda_reg, _mnvda_ptr, configuration_item_header.length, \
		 MLX5_ST_SZ_BYTES(_cls_name))

#define MLX5_GET_CFG_HDR_LEN(_mnvda_ptr) \
	MLX5_GET(mnvda_reg, _mnvda_ptr, configuration_item_header.length)

static int mlx5_nv_param_read(struct mlx5_core_dev *dev, void *mnvda,
			      size_t len)
{
	u32 param_idx, type_class;
	u32 header_len;
	void *cls_ptr;
	int err;

	if (WARN_ON(len > MLX5_ST_SZ_BYTES(mnvda_reg)) || len < MNVDA_HDR_SZ)
		return -EINVAL; /* A caller bug */

	err = mlx5_core_access_reg(dev, mnvda, len, mnvda, len, MLX5_REG_MNVDA,
				   0, 0);
	if (!err)
		return 0;

	cls_ptr = MLX5_ADDR_OF(mnvda_reg, mnvda,
			       configuration_item_header.type.configuration_item_type_class_global);

	type_class = MLX5_GET(configuration_item_type_class_global, cls_ptr,
			      type_class);
	param_idx = MLX5_GET(configuration_item_type_class_global, cls_ptr,
			     parameter_index);
	header_len = MLX5_GET_CFG_HDR_LEN(mnvda);

	mlx5_core_warn(dev, "Failed to read mnvda reg: type_class 0x%x, param_idx 0x%x, header_len %u, err %d\n",
		       type_class, param_idx, header_len, err);

	return -EOPNOTSUPP;
}

static int mlx5_nv_param_write(struct mlx5_core_dev *dev, void *mnvda,
			       size_t len)
{
	if (WARN_ON(len > MLX5_ST_SZ_BYTES(mnvda_reg)) || len < MNVDA_HDR_SZ)
		return -EINVAL;

	if (WARN_ON(MLX5_GET_CFG_HDR_LEN(mnvda) == 0))
		return -EINVAL;

	return mlx5_core_access_reg(dev, mnvda, len, mnvda, len, MLX5_REG_MNVDA,
				    0, 1);
}

static int
mlx5_nv_param_read_sw_offload_conf(struct mlx5_core_dev *dev, void *mnvda,
				   size_t len)
{
	MLX5_SET_CFG_ITEM_TYPE(global, mnvda, type_class, 0);
	MLX5_SET_CFG_ITEM_TYPE(global, mnvda, parameter_index,
			       MLX5_CLASS_0_CTRL_ID_NV_SW_OFFLOAD_CONFIG);
	MLX5_SET_CFG_HDR_LEN(mnvda, nv_sw_offload_conf);

	return mlx5_nv_param_read(dev, mnvda, len);
}

static const char *const
	cqe_compress_str[] = { "balanced", "aggressive" };

static int
mlx5_nv_param_devlink_cqe_compress_get(struct devlink *devlink, u32 id,
				       struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	u32 mnvda[MLX5_ST_SZ_DW(mnvda_reg)] = {};
	u8 value = U8_MAX;
	void *data;
	int err;

	err = mlx5_nv_param_read_sw_offload_conf(dev, mnvda, sizeof(mnvda));
	if (err)
		return err;

	data = MLX5_ADDR_OF(mnvda_reg, mnvda, configuration_item_data);
	value = MLX5_GET(nv_sw_offload_conf, data, cqe_compression);

	if (value >= ARRAY_SIZE(cqe_compress_str))
		return -EOPNOTSUPP;

	strscpy(ctx->val.vstr, cqe_compress_str[value], sizeof(ctx->val.vstr));
	return 0;
}

static int
mlx5_nv_param_devlink_cqe_compress_validate(struct devlink *devlink, u32 id,
					    union devlink_param_value val,
					    struct netlink_ext_ack *extack)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(cqe_compress_str); i++) {
		if (!strcmp(val.vstr, cqe_compress_str[i]))
			return 0;
	}

	NL_SET_ERR_MSG_MOD(extack,
			   "Invalid value, supported values are balanced/aggressive");
	return -EOPNOTSUPP;
}

static int
mlx5_nv_param_devlink_cqe_compress_set(struct devlink *devlink, u32 id,
				       struct devlink_param_gset_ctx *ctx,
				       struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	u32 mnvda[MLX5_ST_SZ_DW(mnvda_reg)] = {};
	int err = 0;
	void *data;
	u8 value;

	if (!strcmp(ctx->val.vstr, "aggressive"))
		value = 1;
	else /* balanced: can't be anything else already validated above */
		value = 0;

	err = mlx5_nv_param_read_sw_offload_conf(dev, mnvda, sizeof(mnvda));
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to read sw_offload_conf mnvda reg");
		return err;
	}

	data = MLX5_ADDR_OF(mnvda_reg, mnvda, configuration_item_data);
	MLX5_SET(nv_sw_offload_conf, data, cqe_compression, value);

	return mlx5_nv_param_write(dev, mnvda, sizeof(mnvda));
}

static int mlx5_nv_param_read_global_pci_conf(struct mlx5_core_dev *dev,
					      void *mnvda, size_t len)
{
	MLX5_SET_CFG_ITEM_TYPE(global, mnvda, type_class, 0);
	MLX5_SET_CFG_ITEM_TYPE(global, mnvda, parameter_index,
			       MLX5_CLASS_0_CTRL_ID_NV_GLOBAL_PCI_CONF);
	MLX5_SET_CFG_HDR_LEN(mnvda, nv_global_pci_conf);

	return mlx5_nv_param_read(dev, mnvda, len);
}

static int mlx5_nv_param_read_global_pci_cap(struct mlx5_core_dev *dev,
					     void *mnvda, size_t len)
{
	MLX5_SET_CFG_ITEM_TYPE(global, mnvda, type_class, 0);
	MLX5_SET_CFG_ITEM_TYPE(global, mnvda, parameter_index,
			       MLX5_CLASS_0_CTRL_ID_NV_GLOBAL_PCI_CAP);
	MLX5_SET_CFG_HDR_LEN(mnvda, nv_global_pci_cap);

	return mlx5_nv_param_read(dev, mnvda, len);
}

static int mlx5_nv_param_read_per_host_pf_conf(struct mlx5_core_dev *dev,
					       void *mnvda, size_t len)
{
	MLX5_SET_CFG_ITEM_TYPE(per_host_pf, mnvda, type_class, 3);
	MLX5_SET_CFG_ITEM_TYPE(per_host_pf, mnvda, parameter_index,
			       MLX5_CLASS_3_CTRL_ID_NV_PF_PCI_CONF);
	MLX5_SET_CFG_HDR_LEN(mnvda, nv_pf_pci_conf);

	return mlx5_nv_param_read(dev, mnvda, len);
}

static int mlx5_devlink_enable_sriov_get(struct devlink *devlink, u32 id,
					 struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	u32 mnvda[MLX5_ST_SZ_DW(mnvda_reg)] = {};
	bool sriov_en = false;
	void *data;
	int err;

	err = mlx5_nv_param_read_global_pci_cap(dev, mnvda, sizeof(mnvda));
	if (err)
		return err;

	data = MLX5_ADDR_OF(mnvda_reg, mnvda, configuration_item_data);
	if (!MLX5_GET(nv_global_pci_cap, data, sriov_support)) {
		ctx->val.vbool = false;
		return 0;
	}

	memset(mnvda, 0, sizeof(mnvda));
	err = mlx5_nv_param_read_global_pci_conf(dev, mnvda, sizeof(mnvda));
	if (err)
		return err;

	data = MLX5_ADDR_OF(mnvda_reg, mnvda, configuration_item_data);
	sriov_en = MLX5_GET(nv_global_pci_conf, data, sriov_en);
	if (!MLX5_GET(nv_global_pci_conf, data, per_pf_total_vf)) {
		ctx->val.vbool = sriov_en;
		return 0;
	}

	/* SRIOV is per PF */
	memset(mnvda, 0, sizeof(mnvda));
	err = mlx5_nv_param_read_per_host_pf_conf(dev, mnvda, sizeof(mnvda));
	if (err)
		return err;

	data = MLX5_ADDR_OF(mnvda_reg, mnvda, configuration_item_data);
	ctx->val.vbool = sriov_en &&
			 MLX5_GET(nv_pf_pci_conf, data, pf_total_vf_en);
	return 0;
}

static int mlx5_devlink_enable_sriov_set(struct devlink *devlink, u32 id,
					 struct devlink_param_gset_ctx *ctx,
					 struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	u32 mnvda[MLX5_ST_SZ_DW(mnvda_reg)] = {};
	bool per_pf_support;
	void *cap, *data;
	int err;

	err = mlx5_nv_param_read_global_pci_cap(dev, mnvda, sizeof(mnvda));
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Failed to read global PCI capability");
		return err;
	}

	cap = MLX5_ADDR_OF(mnvda_reg, mnvda, configuration_item_data);
	per_pf_support = MLX5_GET(nv_global_pci_cap, cap,
				  per_pf_total_vf_supported);

	if (!MLX5_GET(nv_global_pci_cap, cap, sriov_support)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "SRIOV is not supported on this device");
		return -EOPNOTSUPP;
	}

	if (!per_pf_support) {
		/* We don't allow global SRIOV setting on per PF devlink */
		NL_SET_ERR_MSG_MOD(extack,
				   "SRIOV is not per PF on this device");
		return -EOPNOTSUPP;
	}

	memset(mnvda, 0, sizeof(mnvda));
	err = mlx5_nv_param_read_global_pci_conf(dev, mnvda, sizeof(mnvda));
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Unable to read global PCI configuration");
		return err;
	}

	data = MLX5_ADDR_OF(mnvda_reg, mnvda, configuration_item_data);

	/* setup per PF sriov mode */
	MLX5_SET(nv_global_pci_conf, data, sriov_valid, 1);
	MLX5_SET(nv_global_pci_conf, data, sriov_en, 1);
	MLX5_SET(nv_global_pci_conf, data, per_pf_total_vf, 1);

	err = mlx5_nv_param_write(dev, mnvda, sizeof(mnvda));
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Unable to write global PCI configuration");
		return err;
	}

	/* enable/disable sriov on this PF */
	memset(mnvda, 0, sizeof(mnvda));
	err = mlx5_nv_param_read_per_host_pf_conf(dev, mnvda, sizeof(mnvda));
	if (err) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Unable to read per host PF configuration");
		return err;
	}
	MLX5_SET(nv_pf_pci_conf, data, pf_total_vf_en, ctx->val.vbool);
	return mlx5_nv_param_write(dev, mnvda, sizeof(mnvda));
}

static int mlx5_devlink_total_vfs_get(struct devlink *devlink, u32 id,
				      struct devlink_param_gset_ctx *ctx)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	u32 mnvda[MLX5_ST_SZ_DW(mnvda_reg)] = {};
	void *data;
	int err;

	data = MLX5_ADDR_OF(mnvda_reg, mnvda, configuration_item_data);

	err = mlx5_nv_param_read_global_pci_cap(dev, mnvda, sizeof(mnvda));
	if (err)
		return err;

	if (!MLX5_GET(nv_global_pci_cap, data, sriov_support)) {
		ctx->val.vu32 = 0;
		return 0;
	}

	memset(mnvda, 0, sizeof(mnvda));
	err = mlx5_nv_param_read_global_pci_conf(dev, mnvda, sizeof(mnvda));
	if (err)
		return err;

	if (!MLX5_GET(nv_global_pci_conf, data, per_pf_total_vf)) {
		ctx->val.vu32 = MLX5_GET(nv_global_pci_conf, data, total_vfs);
		return 0;
	}

	/* SRIOV is per PF */
	memset(mnvda, 0, sizeof(mnvda));
	err = mlx5_nv_param_read_per_host_pf_conf(dev, mnvda, sizeof(mnvda));
	if (err)
		return err;

	ctx->val.vu32 = MLX5_GET(nv_pf_pci_conf, data, total_vf);

	return 0;
}

static int mlx5_devlink_total_vfs_set(struct devlink *devlink, u32 id,
				      struct devlink_param_gset_ctx *ctx,
				      struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	u32 mnvda[MLX5_ST_SZ_DW(mnvda_reg)];
	void *data;
	int err;

	err = mlx5_nv_param_read_global_pci_cap(dev, mnvda, sizeof(mnvda));
	if (err) {
		NL_SET_ERR_MSG_MOD(extack, "Failed to read global pci cap");
		return err;
	}

	data = MLX5_ADDR_OF(mnvda_reg, mnvda, configuration_item_data);
	if (!MLX5_GET(nv_global_pci_cap, data, sriov_support)) {
		NL_SET_ERR_MSG_MOD(extack, "Not configurable on this device");
		return -EOPNOTSUPP;
	}

	if (!MLX5_GET(nv_global_pci_cap, data, per_pf_total_vf_supported)) {
		/* We don't allow global SRIOV setting on per PF devlink */
		NL_SET_ERR_MSG_MOD(extack,
				   "SRIOV is not per PF on this device");
		return -EOPNOTSUPP;
	}

	memset(mnvda, 0, sizeof(mnvda));
	err = mlx5_nv_param_read_global_pci_conf(dev, mnvda, sizeof(mnvda));
	if (err)
		return err;

	MLX5_SET(nv_global_pci_conf, data, sriov_valid, 1);
	MLX5_SET(nv_global_pci_conf, data, per_pf_total_vf, 1);

	err = mlx5_nv_param_write(dev, mnvda, sizeof(mnvda));
	if (err)
		return err;

	memset(mnvda, 0, sizeof(mnvda));
	err = mlx5_nv_param_read_per_host_pf_conf(dev, mnvda, sizeof(mnvda));
	if (err)
		return err;

	data = MLX5_ADDR_OF(mnvda_reg, mnvda, configuration_item_data);
	MLX5_SET(nv_pf_pci_conf, data, total_vf, ctx->val.vu32);
	return mlx5_nv_param_write(dev, mnvda, sizeof(mnvda));
}

static int mlx5_devlink_total_vfs_validate(struct devlink *devlink, u32 id,
					   union devlink_param_value val,
					   struct netlink_ext_ack *extack)
{
	struct mlx5_core_dev *dev = devlink_priv(devlink);
	u32 cap[MLX5_ST_SZ_DW(mnvda_reg)];
	void *data;
	u16 max;
	int err;

	data = MLX5_ADDR_OF(mnvda_reg, cap, configuration_item_data);

	err = mlx5_nv_param_read_global_pci_cap(dev, cap, sizeof(cap));
	if (err)
		return err;

	if (!MLX5_GET(nv_global_pci_cap, data, max_vfs_per_pf_valid))
		return 0; /* optimistic, but set might fail later */

	max = MLX5_GET(nv_global_pci_cap, data, max_vfs_per_pf);
	if (val.vu16 > max) {
		NL_SET_ERR_MSG_FMT_MOD(extack,
				       "Max allowed by device is %u", max);
		return -EINVAL;
	}

	return 0;
}

static const struct devlink_param mlx5_nv_param_devlink_params[] = {
	DEVLINK_PARAM_GENERIC(ENABLE_SRIOV, BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			      mlx5_devlink_enable_sriov_get,
			      mlx5_devlink_enable_sriov_set, NULL),
	DEVLINK_PARAM_GENERIC(TOTAL_VFS, BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			      mlx5_devlink_total_vfs_get,
			      mlx5_devlink_total_vfs_set,
			      mlx5_devlink_total_vfs_validate),
	DEVLINK_PARAM_DRIVER(MLX5_DEVLINK_PARAM_ID_CQE_COMPRESSION_TYPE,
			     "cqe_compress_type", DEVLINK_PARAM_TYPE_STRING,
			     BIT(DEVLINK_PARAM_CMODE_PERMANENT),
			     mlx5_nv_param_devlink_cqe_compress_get,
			     mlx5_nv_param_devlink_cqe_compress_set,
			     mlx5_nv_param_devlink_cqe_compress_validate),
};

int mlx5_nv_param_register_dl_params(struct devlink *devlink)
{
	if (!mlx5_core_is_pf(devlink_priv(devlink)))
		return 0;

	return devl_params_register(devlink, mlx5_nv_param_devlink_params,
				    ARRAY_SIZE(mlx5_nv_param_devlink_params));
}

void mlx5_nv_param_unregister_dl_params(struct devlink *devlink)
{
	if (!mlx5_core_is_pf(devlink_priv(devlink)))
		return;

	devl_params_unregister(devlink, mlx5_nv_param_devlink_params,
			       ARRAY_SIZE(mlx5_nv_param_devlink_params));
}

