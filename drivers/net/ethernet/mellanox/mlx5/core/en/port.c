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

/* speed in units of 1Mb */
static const u32 mlx5e_link_speed[MLX5E_LINK_MODES_NUMBER] = {
	[MLX5E_1000BASE_CX_SGMII] = 1000,
	[MLX5E_1000BASE_KX]       = 1000,
	[MLX5E_10GBASE_CX4]       = 10000,
	[MLX5E_10GBASE_KX4]       = 10000,
	[MLX5E_10GBASE_KR]        = 10000,
	[MLX5E_20GBASE_KR2]       = 20000,
	[MLX5E_40GBASE_CR4]       = 40000,
	[MLX5E_40GBASE_KR4]       = 40000,
	[MLX5E_56GBASE_R4]        = 56000,
	[MLX5E_10GBASE_CR]        = 10000,
	[MLX5E_10GBASE_SR]        = 10000,
	[MLX5E_10GBASE_ER]        = 10000,
	[MLX5E_40GBASE_SR4]       = 40000,
	[MLX5E_40GBASE_LR4]       = 40000,
	[MLX5E_50GBASE_SR2]       = 50000,
	[MLX5E_100GBASE_CR4]      = 100000,
	[MLX5E_100GBASE_SR4]      = 100000,
	[MLX5E_100GBASE_KR4]      = 100000,
	[MLX5E_100GBASE_LR4]      = 100000,
	[MLX5E_100BASE_TX]        = 100,
	[MLX5E_1000BASE_T]        = 1000,
	[MLX5E_10GBASE_T]         = 10000,
	[MLX5E_25GBASE_CR]        = 25000,
	[MLX5E_25GBASE_KR]        = 25000,
	[MLX5E_25GBASE_SR]        = 25000,
	[MLX5E_50GBASE_CR2]       = 50000,
	[MLX5E_50GBASE_KR2]       = 50000,
};

static const u32 mlx5e_ext_link_speed[MLX5E_EXT_LINK_MODES_NUMBER] = {
	[MLX5E_SGMII_100M]			= 100,
	[MLX5E_1000BASE_X_SGMII]		= 1000,
	[MLX5E_5GBASE_R]			= 5000,
	[MLX5E_10GBASE_XFI_XAUI_1]		= 10000,
	[MLX5E_40GBASE_XLAUI_4_XLPPI_4]		= 40000,
	[MLX5E_25GAUI_1_25GBASE_CR_KR]		= 25000,
	[MLX5E_50GAUI_2_LAUI_2_50GBASE_CR2_KR2]	= 50000,
	[MLX5E_50GAUI_1_LAUI_1_50GBASE_CR_KR]	= 50000,
	[MLX5E_CAUI_4_100GBASE_CR4_KR4]		= 100000,
	[MLX5E_200GAUI_4_200GBASE_CR4_KR4]	= 200000,
	[MLX5E_400GAUI_8]			= 400000,
};

static void mlx5e_port_get_speed_arr(struct mlx5_core_dev *mdev,
				     const u32 **arr, u32 *size,
				     bool force_legacy)
{
	bool ext = force_legacy ? false : MLX5_CAP_PCAM_FEATURE(mdev, ptys_extended_ethernet);

	*size = ext ? ARRAY_SIZE(mlx5e_ext_link_speed) :
		      ARRAY_SIZE(mlx5e_link_speed);
	*arr  = ext ? mlx5e_ext_link_speed : mlx5e_link_speed;
}

int mlx5_port_query_eth_proto(struct mlx5_core_dev *dev, u8 port, bool ext,
			      struct mlx5e_port_eth_proto *eproto)
{
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];
	int err;

	if (!eproto)
		return -EINVAL;

	err = mlx5_query_port_ptys(dev, out, sizeof(out), MLX5_PTYS_EN, port);
	if (err)
		return err;

	eproto->cap   = MLX5_GET_ETH_PROTO(ptys_reg, out, ext,
					   eth_proto_capability);
	eproto->admin = MLX5_GET_ETH_PROTO(ptys_reg, out, ext, eth_proto_admin);
	eproto->oper  = MLX5_GET_ETH_PROTO(ptys_reg, out, ext, eth_proto_oper);
	return 0;
}

void mlx5_port_query_eth_autoneg(struct mlx5_core_dev *dev, u8 *an_status,
				 u8 *an_disable_cap, u8 *an_disable_admin)
{
	u32 out[MLX5_ST_SZ_DW(ptys_reg)];

	*an_status = 0;
	*an_disable_cap = 0;
	*an_disable_admin = 0;

	if (mlx5_query_port_ptys(dev, out, sizeof(out), MLX5_PTYS_EN, 1))
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

u32 mlx5e_port_ptys2speed(struct mlx5_core_dev *mdev, u32 eth_proto_oper,
			  bool force_legacy)
{
	unsigned long temp = eth_proto_oper;
	const u32 *table;
	u32 speed = 0;
	u32 max_size;
	int i;

	mlx5e_port_get_speed_arr(mdev, &table, &max_size, force_legacy);
	i = find_first_bit(&temp, max_size);
	if (i < max_size)
		speed = table[i];
	return speed;
}

int mlx5e_port_linkspeed(struct mlx5_core_dev *mdev, u32 *speed)
{
	struct mlx5e_port_eth_proto eproto;
	bool force_legacy = false;
	bool ext;
	int err;

	ext = MLX5_CAP_PCAM_FEATURE(mdev, ptys_extended_ethernet);
	err = mlx5_port_query_eth_proto(mdev, 1, ext, &eproto);
	if (err)
		goto out;
	if (ext && !eproto.admin) {
		force_legacy = true;
		err = mlx5_port_query_eth_proto(mdev, 1, false, &eproto);
		if (err)
			goto out;
	}
	*speed = mlx5e_port_ptys2speed(mdev, eproto.oper, force_legacy);
	if (!(*speed))
		err = -EINVAL;

out:
	return err;
}

int mlx5e_port_max_linkspeed(struct mlx5_core_dev *mdev, u32 *speed)
{
	struct mlx5e_port_eth_proto eproto;
	u32 max_speed = 0;
	const u32 *table;
	u32 max_size;
	bool ext;
	int err;
	int i;

	ext = MLX5_CAP_PCAM_FEATURE(mdev, ptys_extended_ethernet);
	err = mlx5_port_query_eth_proto(mdev, 1, ext, &eproto);
	if (err)
		return err;

	mlx5e_port_get_speed_arr(mdev, &table, &max_size, false);
	for (i = 0; i < max_size; ++i)
		if (eproto.cap & MLX5E_PROT_MASK(i))
			max_speed = max(max_speed, table[i]);

	*speed = max_speed;
	return 0;
}

u32 mlx5e_port_speed2linkmodes(struct mlx5_core_dev *mdev, u32 speed,
			       bool force_legacy)
{
	u32 link_modes = 0;
	const u32 *table;
	u32 max_size;
	int i;

	mlx5e_port_get_speed_arr(mdev, &table, &max_size, force_legacy);
	for (i = 0; i < max_size; ++i) {
		if (table[i] == speed)
			link_modes |= MLX5E_PROT_MASK(i);
	}
	return link_modes;
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

static u32 fec_supported_speeds[] = {
	10000,
	40000,
	25000,
	50000,
	56000,
	100000
};

#define MLX5E_FEC_SUPPORTED_SPEEDS ARRAY_SIZE(fec_supported_speeds)

/* get/set FEC admin field for a given speed */
static int mlx5e_fec_admin_field(u32 *pplm,
				 u8 *fec_policy,
				 bool write,
				 u32 speed)
{
	switch (speed) {
	case 10000:
	case 40000:
		if (!write)
			*fec_policy = MLX5_GET(pplm_reg, pplm,
					       fec_override_admin_10g_40g);
		else
			MLX5_SET(pplm_reg, pplm,
				 fec_override_admin_10g_40g, *fec_policy);
		break;
	case 25000:
		if (!write)
			*fec_policy = MLX5_GET(pplm_reg, pplm,
					       fec_override_admin_25g);
		else
			MLX5_SET(pplm_reg, pplm,
				 fec_override_admin_25g, *fec_policy);
		break;
	case 50000:
		if (!write)
			*fec_policy = MLX5_GET(pplm_reg, pplm,
					       fec_override_admin_50g);
		else
			MLX5_SET(pplm_reg, pplm,
				 fec_override_admin_50g, *fec_policy);
		break;
	case 56000:
		if (!write)
			*fec_policy = MLX5_GET(pplm_reg, pplm,
					       fec_override_admin_56g);
		else
			MLX5_SET(pplm_reg, pplm,
				 fec_override_admin_56g, *fec_policy);
		break;
	case 100000:
		if (!write)
			*fec_policy = MLX5_GET(pplm_reg, pplm,
					       fec_override_admin_100g);
		else
			MLX5_SET(pplm_reg, pplm,
				 fec_override_admin_100g, *fec_policy);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

/* returns FEC capabilities for a given speed */
static int mlx5e_get_fec_cap_field(u32 *pplm,
				   u8 *fec_cap,
				   u32 speed)
{
	switch (speed) {
	case 10000:
	case 40000:
		*fec_cap = MLX5_GET(pplm_reg, pplm,
				    fec_override_cap_10g_40g);
		break;
	case 25000:
		*fec_cap = MLX5_GET(pplm_reg, pplm,
				    fec_override_cap_25g);
		break;
	case 50000:
		*fec_cap = MLX5_GET(pplm_reg, pplm,
				    fec_override_cap_50g);
		break;
	case 56000:
		*fec_cap = MLX5_GET(pplm_reg, pplm,
				    fec_override_cap_56g);
		break;
	case 100000:
		*fec_cap = MLX5_GET(pplm_reg, pplm,
				    fec_override_cap_100g);
		break;
	default:
		return -EINVAL;
	}
	return 0;
}

int mlx5e_get_fec_caps(struct mlx5_core_dev *dev, u8 *fec_caps)
{
	u32 out[MLX5_ST_SZ_DW(pplm_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(pplm_reg)] = {};
	int sz = MLX5_ST_SZ_BYTES(pplm_reg);
	u32 current_fec_speed;
	int err;

	if (!MLX5_CAP_GEN(dev, pcam_reg))
		return -EOPNOTSUPP;

	if (!MLX5_CAP_PCAM_REG(dev, pplm))
		return -EOPNOTSUPP;

	MLX5_SET(pplm_reg, in, local_port, 1);
	err =  mlx5_core_access_reg(dev, in, sz, out, sz, MLX5_REG_PPLM, 0, 0);
	if (err)
		return err;

	err = mlx5e_port_linkspeed(dev, &current_fec_speed);
	if (err)
		return err;

	return mlx5e_get_fec_cap_field(out, fec_caps, current_fec_speed);
}

int mlx5e_get_fec_mode(struct mlx5_core_dev *dev, u32 *fec_mode_active,
		       u8 *fec_configured_mode)
{
	u32 out[MLX5_ST_SZ_DW(pplm_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(pplm_reg)] = {};
	int sz = MLX5_ST_SZ_BYTES(pplm_reg);
	u32 link_speed;
	int err;

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
		return 0;

	err = mlx5e_port_linkspeed(dev, &link_speed);
	if (err)
		return err;

	return mlx5e_fec_admin_field(out, fec_configured_mode, 0, link_speed);
}

int mlx5e_set_fec_mode(struct mlx5_core_dev *dev, u8 fec_policy)
{
	u8 fec_policy_nofec = BIT(MLX5E_FEC_NOFEC);
	bool fec_mode_not_supp_in_speed = false;
	u32 out[MLX5_ST_SZ_DW(pplm_reg)] = {};
	u32 in[MLX5_ST_SZ_DW(pplm_reg)] = {};
	int sz = MLX5_ST_SZ_BYTES(pplm_reg);
	u8 fec_policy_auto = 0;
	u8 fec_caps = 0;
	int err;
	int i;

	if (!MLX5_CAP_GEN(dev, pcam_reg))
		return -EOPNOTSUPP;

	if (!MLX5_CAP_PCAM_REG(dev, pplm))
		return -EOPNOTSUPP;

	MLX5_SET(pplm_reg, in, local_port, 1);
	err = mlx5_core_access_reg(dev, in, sz, out, sz, MLX5_REG_PPLM, 0, 0);
	if (err)
		return err;

	MLX5_SET(pplm_reg, out, local_port, 1);

	for (i = 0; i < MLX5E_FEC_SUPPORTED_SPEEDS; i++) {
		mlx5e_get_fec_cap_field(out, &fec_caps, fec_supported_speeds[i]);
		/* policy supported for link speed, or policy is auto */
		if (fec_caps & fec_policy || fec_policy == fec_policy_auto) {
			mlx5e_fec_admin_field(out, &fec_policy, 1,
					      fec_supported_speeds[i]);
		} else {
			/* turn off FEC if supported. Else, leave it the same */
			if (fec_caps & fec_policy_nofec)
				mlx5e_fec_admin_field(out, &fec_policy_nofec, 1,
						      fec_supported_speeds[i]);
			fec_mode_not_supp_in_speed = true;
		}
	}

	if (fec_mode_not_supp_in_speed)
		mlx5_core_dbg(dev,
			      "FEC policy 0x%x is not supported for some speeds",
			      fec_policy);

	return mlx5_core_access_reg(dev, out, sz, out, sz, MLX5_REG_PPLM, 0, 1);
}
