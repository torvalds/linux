/*
 * Copyright (c) 2018, Mellanox Technologies. All rights reserved.
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

#include "port.h"

void mlx5_port_query_eth_autoneg(struct mlx5_core_dev *dev, u8 *an_status,
				 u8 *an_disable_cap, u8 *an_disable_admin)
{
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];

	*an_status = 0;
	*an_disable_cap = 0;
	*an_disable_admin = 0;

	if (mlx5_query_port_ptys(dev, out, sizeof(out), MLX5_PTYS_EN, 1, 0))
		return;

	*an_status = MLX5_GET(ptys_reg, out, an_status);
	*an_disable_cap = MLX5_GET(ptys_reg, out, an_disable_cap);
	*an_disable_admin = MLX5_GET(ptys_reg, out, an_disable_admin);
}

int mlx5_port_set_eth_ptys(struct mlx5_core_dev *dev, bool an_disable,
			   u32 proto_admin, bool ext)
{
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];
	u32 in[MLX5_ST_SZ_DW(ptys_reg)];
	u8 an_disable_admin;
	u8 an_disable_cap;
	u8 an_status;

	mlx5_port_query_eth_autoneg(dev, &an_status, &an_disable_cap,
				    &an_disable_admin);
	if (!an_disable_cap && an_disable)
		return -EPERM;

	memset(in, 0, sizeof(in));

	MLX5_SET(ptys_reg, in, local_port, 1);
	MLX5_SET(ptys_reg, in, an_disable_admin, an_disable);
	MLX5_SET(ptys_reg, in, proto_mask, MLX5_PTYS_EN);
	if (ext)
		MLX5_SET(ptys_reg, in, ext_eth_proto_admin, proto_admin);
	else
		MLX5_SET(ptys_reg, in, eth_proto_admin, proto_admin);

	return mlx5_core_access_reg(dev, in, sizeof(in), out,
			    sizeof(out), MLX5_REG_PTYS, 0, 1);
}

int mlx5e_port_linkspeed(struct mlx5_core_dev *mdev, u32 *speed)
{
	struct mlx5_port_eth_proto eproto;
	bool force_legacy = false;
	bool ext;
	int err;

	ext = mlx5_ptys_ext_supported(mdev);
	err = mlx5_port_query_eth_proto(mdev, 1, ext, &eproto);
	if (err)
		goto out;
	if (ext && !eproto.admin) {
		force_legacy = true;
		err = mlx5_port_query_eth_proto(mdev, 1, false, &eproto);
		if (err)
			goto out;
	}
	*speed = mlx5_port_ptys2speed(mdev, eproto.oper, force_legacy);
	if (!(*speed))
		err = -EINVAL;

out:
	return err;
}

int mlx5e_port_query_pbmc(struct mlx5_core_dev *mdev, void *out)
{
	int sz = MLX5_ST_SZ_BYTES(pbmc_reg);
	void *in;
	int err;

	in = kzalloc(sz, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(pbmc_reg, in, local_port, 1);
	err = mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PBMC, 0, 0);

	kfree(in);
	return err;
}

int mlx5e_port_set_pbmc(struct mlx5_core_dev *mdev, void *in)
{
	int sz = MLX5_ST_SZ_BYTES(pbmc_reg);
	void *out;
	int err;

	out = kzalloc(sz, GFP_KERNEL);
	if (!out)
		return -ENOMEM;

	MLX5_SET(pbmc_reg, in, local_port, 1);
	err = mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PBMC, 0, 1);

	kfree(out);
	return err;
}

int mlx5e_port_query_sbpr(struct mlx5_core_dev *mdev, u32 desc, u8 dir,
			  u8 pool_idx, void *out, int size_out)
{
	u32 in[MLX5_ST_SZ_DW(sbpr_reg)] = {};

	MLX5_SET(sbpr_reg, in, desc, desc);
	MLX5_SET(sbpr_reg, in, dir, dir);
	MLX5_SET(sbpr_reg, in, pool, pool_idx);

	return mlx5_core_access_reg(mdev, in, sizeof(in), out, size_out, MLX5_REG_SBPR, 0, 0);
}

int mlx5e_port_set_sbpr(struct mlx5_core_dev *mdev, u32 desc, u8 dir,
			u8 pool_idx, u32 infi_size, u32 size)
{
	u32 out[MLX5_ST_SZ_DW(sbpr_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(sbpr_reg)] = {};

	MLX5_SET(sbpr_reg, in, desc, desc);
	MLX5_SET(sbpr_reg, in, dir, dir);
	MLX5_SET(sbpr_reg, in, pool, pool_idx);
	MLX5_SET(sbpr_reg, in, infi_size, infi_size);
	MLX5_SET(sbpr_reg, in, size, size);
	MLX5_SET(sbpr_reg, in, mode, 1);

	return mlx5_core_access_reg(mdev, in, sizeof(in), out, sizeof(out), MLX5_REG_SBPR, 0, 1);
}

static int mlx5e_port_query_sbcm(struct mlx5_core_dev *mdev, u32 desc,
				 u8 pg_buff_idx, u8 dir, void *out,
				 int size_out)
{
	u32 in[MLX5_ST_SZ_DW(sbcm_reg)] = {};

	MLX5_SET(sbcm_reg, in, desc, desc);
	MLX5_SET(sbcm_reg, in, local_port, 1);
	MLX5_SET(sbcm_reg, in, pg_buff, pg_buff_idx);
	MLX5_SET(sbcm_reg, in, dir, dir);

	return mlx5_core_access_reg(mdev, in, sizeof(in), out, size_out, MLX5_REG_SBCM, 0, 0);
}

int mlx5e_port_set_sbcm(struct mlx5_core_dev *mdev, u32 desc, u8 pg_buff_idx,
			u8 dir, u8 infi_size, u32 max_buff, u8 pool_idx)
{
	u32 out[MLX5_ST_SZ_DW(sbcm_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(sbcm_reg)] = {};
	u32 min_buff;
	int err;
	u8 exc;

	err = mlx5e_port_query_sbcm(mdev, desc, pg_buff_idx, dir, out,
				    sizeof(out));
	if (err)
		return err;

	exc = MLX5_GET(sbcm_reg, out, exc);
	min_buff = MLX5_GET(sbcm_reg, out, min_buff);

	MLX5_SET(sbcm_reg, in, desc, desc);
	MLX5_SET(sbcm_reg, in, local_port, 1);
	MLX5_SET(sbcm_reg, in, pg_buff, pg_buff_idx);
	MLX5_SET(sbcm_reg, in, dir, dir);
	MLX5_SET(sbcm_reg, in, exc, exc);
	MLX5_SET(sbcm_reg, in, min_buff, min_buff);
	MLX5_SET(sbcm_reg, in, infi_max, infi_size);
	MLX5_SET(sbcm_reg, in, max_buff, max_buff);
	MLX5_SET(sbcm_reg, in, pool, pool_idx);

	return mlx5_core_access_reg(mdev, in, sizeof(in), out, sizeof(out), MLX5_REG_SBCM, 0, 1);
}

/* buffer[i]: buffer that priority i mapped to */
int mlx5e_port_query_priority2buffer(struct mlx5_core_dev *mdev, u8 *buffer)
{
	int sz = MLX5_ST_SZ_BYTES(pptb_reg);
	u32 prio_x_buff;
	void *out;
	void *in;
	int prio;
	int err;

	in = kzalloc(sz, GFP_KERNEL);
	out = kzalloc(sz, GFP_KERNEL);
	if (!in || !out) {
		err = -ENOMEM;
		goto out;
	}

	MLX5_SET(pptb_reg, in, local_port, 1);
	err = mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPTB, 0, 0);
	if (err)
		goto out;

	prio_x_buff = MLX5_GET(pptb_reg, out, prio_x_buff);
	for (prio = 0; prio < 8; prio++) {
		buffer[prio] = (u8)(prio_x_buff >> (4 * prio)) & 0xF;
		mlx5_core_dbg(mdev, "prio %d, buffer %d\n", prio, buffer[prio]);
	}
out:
	kfree(in);
	kfree(out);
	return err;
}

int mlx5e_port_set_priority2buffer(struct mlx5_core_dev *mdev, u8 *buffer)
{
	int sz = MLX5_ST_SZ_BYTES(pptb_reg);
	u32 prio_x_buff;
	void *out;
	void *in;
	int prio;
	int err;

	in = kzalloc(sz, GFP_KERNEL);
	out = kzalloc(sz, GFP_KERNEL);
	if (!in || !out) {
		err = -ENOMEM;
		goto out;
	}

	/* First query the pptb register */
	MLX5_SET(pptb_reg, in, local_port, 1);
	err = mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPTB, 0, 0);
	if (err)
		goto out;

	memcpy(in, out, sz);
	MLX5_SET(pptb_reg, in, local_port, 1);

	/* Update the pm and prio_x_buff */
	MLX5_SET(pptb_reg, in, pm, 0xFF);

	prio_x_buff = 0;
	for (prio = 0; prio < 8; prio++)
		prio_x_buff |= (buffer[prio] << (4 * prio));
	MLX5_SET(pptb_reg, in, prio_x_buff, prio_x_buff);

	err = mlx5_core_access_reg(mdev, in, sz, out, sz, MLX5_REG_PPTB, 0, 1);

out:
	kfree(in);
	kfree(out);
	return err;
}

enum mlx5e_fec_supported_link_mode {
	MLX5E_FEC_SUPPORTED_LINK_MODES_10G_40G,
	MLX5E_FEC_SUPPORTED_LINK_MODES_25G,
	MLX5E_FEC_SUPPORTED_LINK_MODES_50G,
	MLX5E_FEC_SUPPORTED_LINK_MODES_56G,
	MLX5E_FEC_SUPPORTED_LINK_MODES_100G,
	MLX5E_FEC_SUPPORTED_LINK_MODE_50G_1X,
	MLX5E_FEC_SUPPORTED_LINK_MODE_100G_2X,
	MLX5E_FEC_SUPPORTED_LINK_MODE_200G_4X,
	MLX5E_FEC_SUPPORTED_LINK_MODE_400G_8X,
	MLX5E_FEC_SUPPORTED_LINK_MODE_100G_1X,
	MLX5E_FEC_SUPPORTED_LINK_MODE_200G_2X,
	MLX5E_FEC_SUPPORTED_LINK_MODE_400G_4X,
	MLX5E_FEC_SUPPORTED_LINK_MODE_800G_8X,
	MLX5E_MAX_FEC_SUPPORTED_LINK_MODE,
};

#define MLX5E_FEC_FIRST_50G_PER_LANE_MODE MLX5E_FEC_SUPPORTED_LINK_MODE_50G_1X
#define MLX5E_FEC_FIRST_100G_PER_LANE_MODE MLX5E_FEC_SUPPORTED_LINK_MODE_100G_1X

#define MLX5E_FEC_OVERRIDE_ADMIN_POLICY(buf, policy, write, link)			\
	do {										\
		u16 *_policy = &(policy);						\
		u32 *_buf = buf;							\
											\
		if (write)								\
			MLX5_SET(pplm_reg, _buf, fec_override_admin_##link, *_policy);	\
		else									\
			*_policy = MLX5_GET(pplm_reg, _buf, fec_override_admin_##link);	\
	} while (0)

/* Returns true if FEC can be set for a given link mode. */
static bool mlx5e_is_fec_supported_link_mode(struct mlx5_core_dev *dev,
					     enum mlx5e_fec_supported_link_mode link_mode)
{
	return link_mode < MLX5E_FEC_FIRST_50G_PER_LANE_MODE ||
	       (link_mode < MLX5E_FEC_FIRST_100G_PER_LANE_MODE &&
		MLX5_CAP_PCAM_FEATURE(dev, fec_50G_per_lane_in_pplm)) ||
	       (link_mode >= MLX5E_FEC_FIRST_100G_PER_LANE_MODE &&
		MLX5_CAP_PCAM_FEATURE(dev, fec_100G_per_lane_in_pplm));
}

/* get/set FEC admin field for a given speed */
static int mlx5e_fec_admin_field(u32 *pplm, u16 *fec_policy, bool write,
				 enum mlx5e_fec_supported_link_mode link_mode)
{
	switch (link_mode) {
	case MLX5E_FEC_SUPPORTED_LINK_MODES_10G_40G:
		MLX5E_FEC_OVERRIDE_ADMIN_POLICY(pplm, *fec_policy, write, 10g_40g);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODES_25G:
		MLX5E_FEC_OVERRIDE_ADMIN_POLICY(pplm, *fec_policy, write, 25g);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODES_50G:
		MLX5E_FEC_OVERRIDE_ADMIN_POLICY(pplm, *fec_policy, write, 50g);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODES_56G:
		MLX5E_FEC_OVERRIDE_ADMIN_POLICY(pplm, *fec_policy, write, 56g);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODES_100G:
		MLX5E_FEC_OVERRIDE_ADMIN_POLICY(pplm, *fec_policy, write, 100g);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_50G_1X:
		MLX5E_FEC_OVERRIDE_ADMIN_POLICY(pplm, *fec_policy, write, 50g_1x);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_100G_2X:
		MLX5E_FEC_OVERRIDE_ADMIN_POLICY(pplm, *fec_policy, write, 100g_2x);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_200G_4X:
		MLX5E_FEC_OVERRIDE_ADMIN_POLICY(pplm, *fec_policy, write, 200g_4x);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_400G_8X:
		MLX5E_FEC_OVERRIDE_ADMIN_POLICY(pplm, *fec_policy, write, 400g_8x);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_100G_1X:
		MLX5E_FEC_OVERRIDE_ADMIN_POLICY(pplm, *fec_policy, write, 100g_1x);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_200G_2X:
		MLX5E_FEC_OVERRIDE_ADMIN_POLICY(pplm, *fec_policy, write, 200g_2x);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_400G_4X:
		MLX5E_FEC_OVERRIDE_ADMIN_POLICY(pplm, *fec_policy, write, 400g_4x);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_800G_8X:
		MLX5E_FEC_OVERRIDE_ADMIN_POLICY(pplm, *fec_policy, write, 800g_8x);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

#define MLX5E_GET_FEC_OVERRIDE_CAP(buf, link)  \
	MLX5_GET(pplm_reg, buf, fec_override_cap_##link)

/* returns FEC capabilities for a given speed */
static int mlx5e_get_fec_cap_field(u32 *pplm, u16 *fec_cap,
				   enum mlx5e_fec_supported_link_mode link_mode)
{
	switch (link_mode) {
	case MLX5E_FEC_SUPPORTED_LINK_MODES_10G_40G:
		*fec_cap = MLX5E_GET_FEC_OVERRIDE_CAP(pplm, 10g_40g);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODES_25G:
		*fec_cap = MLX5E_GET_FEC_OVERRIDE_CAP(pplm, 25g);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODES_50G:
		*fec_cap = MLX5E_GET_FEC_OVERRIDE_CAP(pplm, 50g);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODES_56G:
		*fec_cap = MLX5E_GET_FEC_OVERRIDE_CAP(pplm, 56g);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODES_100G:
		*fec_cap = MLX5E_GET_FEC_OVERRIDE_CAP(pplm, 100g);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_50G_1X:
		*fec_cap = MLX5E_GET_FEC_OVERRIDE_CAP(pplm, 50g_1x);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_100G_2X:
		*fec_cap = MLX5E_GET_FEC_OVERRIDE_CAP(pplm, 100g_2x);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_200G_4X:
		*fec_cap = MLX5E_GET_FEC_OVERRIDE_CAP(pplm, 200g_4x);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_400G_8X:
		*fec_cap = MLX5E_GET_FEC_OVERRIDE_CAP(pplm, 400g_8x);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_100G_1X:
		*fec_cap = MLX5E_GET_FEC_OVERRIDE_CAP(pplm, 100g_1x);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_200G_2X:
		*fec_cap = MLX5E_GET_FEC_OVERRIDE_CAP(pplm, 200g_2x);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_400G_4X:
		*fec_cap = MLX5E_GET_FEC_OVERRIDE_CAP(pplm, 400g_4x);
		break;
	case MLX5E_FEC_SUPPORTED_LINK_MODE_800G_8X:
		*fec_cap = MLX5E_GET_FEC_OVERRIDE_CAP(pplm, 800g_8x);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

bool mlx5e_fec_in_caps(struct mlx5_core_dev *dev, int fec_policy)
{
	u32 out[MLX5_ST_SZ_DW(pplm_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(pplm_reg)] = {};
	int sz = MLX5_ST_SZ_BYTES(pplm_reg);
	int err;
	int i;

	if (!MLX5_CAP_GEN(dev, pcam_reg) || !MLX5_CAP_PCAM_REG(dev, pplm))
		return false;

	MLX5_SET(pplm_reg, in, local_port, 1);
	err =  mlx5_core_access_reg(dev, in, sz, out, sz, MLX5_REG_PPLM, 0, 0);
	if (err)
		return false;

	for (i = 0; i < MLX5E_MAX_FEC_SUPPORTED_LINK_MODE; i++) {
		u16 fec_caps;

		if (!mlx5e_is_fec_supported_link_mode(dev, i))
			break;

		mlx5e_get_fec_cap_field(out, &fec_caps, i);
		if (fec_caps & fec_policy)
			return true;
	}
	return false;
}

int mlx5e_get_fec_mode(struct mlx5_core_dev *dev, u32 *fec_mode_active,
		       u16 *fec_configured_mode)
{
	u32 out[MLX5_ST_SZ_DW(pplm_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(pplm_reg)] = {};
	int sz = MLX5_ST_SZ_BYTES(pplm_reg);
	int err;
	int i;

	if (!MLX5_CAP_GEN(dev, pcam_reg))
		return -EOPNOTSUPP;

	if (!MLX5_CAP_PCAM_REG(dev, pplm))
		return -EOPNOTSUPP;

	MLX5_SET(pplm_reg, in, local_port, 1);
	err =  mlx5_core_access_reg(dev, in, sz, out, sz, MLX5_REG_PPLM, 0, 0);
	if (err)
		return err;

	*fec_mode_active = MLX5_GET(pplm_reg, out, fec_mode_active);

	if (!fec_configured_mode)
		goto out;

	*fec_configured_mode = 0;
	for (i = 0; i < MLX5E_MAX_FEC_SUPPORTED_LINK_MODE; i++) {
		if (!mlx5e_is_fec_supported_link_mode(dev, i))
			break;

		mlx5e_fec_admin_field(out, fec_configured_mode, 0, i);
		if (*fec_configured_mode != 0)
			goto out;
	}
out:
	return 0;
}

int mlx5e_set_fec_mode(struct mlx5_core_dev *dev, u16 fec_policy)
{
	bool fec_50g_per_lane = MLX5_CAP_PCAM_FEATURE(dev, fec_50G_per_lane_in_pplm);
	u32 out[MLX5_ST_SZ_DW(pplm_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(pplm_reg)] = {};
	int sz = MLX5_ST_SZ_BYTES(pplm_reg);
	u16 fec_policy_auto = 0;
	int err;
	int i;

	if (!MLX5_CAP_GEN(dev, pcam_reg))
		return -EOPNOTSUPP;

	if (!MLX5_CAP_PCAM_REG(dev, pplm))
		return -EOPNOTSUPP;

	if (fec_policy >= (1 << MLX5E_FEC_LLRS_272_257_1) && !fec_50g_per_lane)
		return -EOPNOTSUPP;

	if (fec_policy && !mlx5e_fec_in_caps(dev, fec_policy))
		return -EOPNOTSUPP;

	MLX5_SET(pplm_reg, in, local_port, 1);
	err = mlx5_core_access_reg(dev, in, sz, out, sz, MLX5_REG_PPLM, 0, 0);
	if (err)
		return err;

	MLX5_SET(pplm_reg, out, local_port, 1);

	for (i = 0; i < MLX5E_MAX_FEC_SUPPORTED_LINK_MODE; i++) {
		u16 conf_fec = fec_policy;
		u16 fec_caps = 0;

		if (!mlx5e_is_fec_supported_link_mode(dev, i))
			break;

		/* RS fec in ethtool is mapped to MLX5E_FEC_RS_528_514
		 * to link modes up to 25G per lane and to
		 * MLX5E_FEC_RS_544_514 in the new link modes based on
		 * 50G or 100G per lane
		 */
		if (conf_fec == (1 << MLX5E_FEC_RS_528_514) &&
		    i >= MLX5E_FEC_FIRST_50G_PER_LANE_MODE)
			conf_fec = (1 << MLX5E_FEC_RS_544_514);

		mlx5e_get_fec_cap_field(out, &fec_caps, i);

		/* policy supported for link speed */
		if (fec_caps & conf_fec)
			mlx5e_fec_admin_field(out, &conf_fec, 1, i);
		else
			/* set FEC to auto*/
			mlx5e_fec_admin_field(out, &fec_policy_auto, 1, i);
	}

	return mlx5_core_access_reg(dev, out, sz, out, sz, MLX5_REG_PPLM, 0, 1);
}
