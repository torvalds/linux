/*
 * Copyright (c) 2017, Mellanox Technologies. All rights reserved.
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

#include "cmd.h"

int mlx5_cmd_dump_fill_mkey(struct mlx5_core_dev *dev, u32 *mkey)
{
	u32 out[MLX5_ST_SZ_DW(query_special_contexts_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(query_special_contexts_in)]   = {0};
	int err;

	MLX5_SET(query_special_contexts_in, in, opcode,
		 MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*mkey = MLX5_GET(query_special_contexts_out, out,
				 dump_fill_mkey);
	return err;
}

int mlx5_cmd_null_mkey(struct mlx5_core_dev *dev, u32 *null_mkey)
{
	u32 out[MLX5_ST_SZ_DW(query_special_contexts_out)] = {};
	u32 in[MLX5_ST_SZ_DW(query_special_contexts_in)]   = {};
	int err;

	MLX5_SET(query_special_contexts_in, in, opcode,
		 MLX5_CMD_OP_QUERY_SPECIAL_CONTEXTS);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*null_mkey = MLX5_GET(query_special_contexts_out, out,
				      null_mkey);
	return err;
}

int mlx5_cmd_query_cong_params(struct mlx5_core_dev *dev, int cong_point,
			       void *out, int out_size)
{
	u32 in[MLX5_ST_SZ_DW(query_cong_params_in)] = { };

	MLX5_SET(query_cong_params_in, in, opcode,
		 MLX5_CMD_OP_QUERY_CONG_PARAMS);
	MLX5_SET(query_cong_params_in, in, cong_protocol, cong_point);

	return mlx5_cmd_exec(dev, in, sizeof(in), out, out_size);
}

int mlx5_cmd_modify_cong_params(struct mlx5_core_dev *dev,
				void *in, int in_size)
{
	u32 out[MLX5_ST_SZ_DW(modify_cong_params_out)] = { };

	return mlx5_cmd_exec(dev, in, in_size, out, sizeof(out));
}

int mlx5_cmd_alloc_memic(struct mlx5_dm *dm, phys_addr_t *addr,
			 u64 length, u32 alignment)
{
	struct mlx5_core_dev *dev = dm->dev;
	u64 num_memic_hw_pages = MLX5_CAP_DEV_MEM(dev, memic_bar_size)
					>> PAGE_SHIFT;
	u64 hw_start_addr = MLX5_CAP64_DEV_MEM(dev, memic_bar_start_addr);
	u32 max_alignment = MLX5_CAP_DEV_MEM(dev, log_max_memic_addr_alignment);
	u32 num_pages = DIV_ROUND_UP(length, PAGE_SIZE);
	u32 out[MLX5_ST_SZ_DW(alloc_memic_out)] = {};
	u32 in[MLX5_ST_SZ_DW(alloc_memic_in)] = {};
	u32 mlx5_alignment;
	u64 page_idx = 0;
	int ret = 0;

	if (!length || (length & MLX5_MEMIC_ALLOC_SIZE_MASK))
		return -EINVAL;

	/* mlx5 device sets alignment as 64*2^driver_value
	 * so normalizing is needed.
	 */
	mlx5_alignment = (alignment < MLX5_MEMIC_BASE_ALIGN) ? 0 :
			 alignment - MLX5_MEMIC_BASE_ALIGN;
	if (mlx5_alignment > max_alignment)
		return -EINVAL;

	MLX5_SET(alloc_memic_in, in, opcode, MLX5_CMD_OP_ALLOC_MEMIC);
	MLX5_SET(alloc_memic_in, in, range_size, num_pages * PAGE_SIZE);
	MLX5_SET(alloc_memic_in, in, memic_size, length);
	MLX5_SET(alloc_memic_in, in, log_memic_addr_alignment,
		 mlx5_alignment);

	while (page_idx < num_memic_hw_pages) {
		spin_lock(&dm->lock);
		page_idx = bitmap_find_next_zero_area(dm->memic_alloc_pages,
						      num_memic_hw_pages,
						      page_idx,
						      num_pages, 0);

		if (page_idx < num_memic_hw_pages)
			bitmap_set(dm->memic_alloc_pages,
				   page_idx, num_pages);

		spin_unlock(&dm->lock);

		if (page_idx >= num_memic_hw_pages)
			break;

		MLX5_SET64(alloc_memic_in, in, range_start_addr,
			   hw_start_addr + (page_idx * PAGE_SIZE));

		ret = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
		if (ret) {
			spin_lock(&dm->lock);
			bitmap_clear(dm->memic_alloc_pages,
				     page_idx, num_pages);
			spin_unlock(&dm->lock);

			if (ret == -EAGAIN) {
				page_idx++;
				continue;
			}

			return ret;
		}

		*addr = dev->bar_addr +
			MLX5_GET64(alloc_memic_out, out, memic_start_addr);

		return 0;
	}

	return -ENOMEM;
}

void mlx5_cmd_dealloc_memic(struct mlx5_dm *dm, phys_addr_t addr, u64 length)
{
	struct mlx5_core_dev *dev = dm->dev;
	u64 hw_start_addr = MLX5_CAP64_DEV_MEM(dev, memic_bar_start_addr);
	u32 num_pages = DIV_ROUND_UP(length, PAGE_SIZE);
	u32 out[MLX5_ST_SZ_DW(dealloc_memic_out)] = {0};
	u32 in[MLX5_ST_SZ_DW(dealloc_memic_in)] = {0};
	u64 start_page_idx;
	int err;

	addr -= dev->bar_addr;
	start_page_idx = (addr - hw_start_addr) >> PAGE_SHIFT;

	MLX5_SET(dealloc_memic_in, in, opcode, MLX5_CMD_OP_DEALLOC_MEMIC);
	MLX5_SET64(dealloc_memic_in, in, memic_start_addr, addr);
	MLX5_SET(dealloc_memic_in, in, memic_size, length);

	err =  mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (err)
		return;

	spin_lock(&dm->lock);
	bitmap_clear(dm->memic_alloc_pages,
		     start_page_idx, num_pages);
	spin_unlock(&dm->lock);
}

int mlx5_cmd_query_ext_ppcnt_counters(struct mlx5_core_dev *dev, void *out)
{
	u32 in[MLX5_ST_SZ_DW(ppcnt_reg)] = {};
	int sz = MLX5_ST_SZ_BYTES(ppcnt_reg);

	MLX5_SET(ppcnt_reg, in, local_port, 1);

	MLX5_SET(ppcnt_reg, in, grp, MLX5_ETHERNET_EXTENDED_COUNTERS_GROUP);
	return  mlx5_core_access_reg(dev, in, sz, out, sz, MLX5_REG_PPCNT,
				     0, 0);
}

void mlx5_cmd_destroy_tir(struct mlx5_core_dev *dev, u32 tirn, u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(destroy_tir_in)]   = {};
	u32 out[MLX5_ST_SZ_DW(destroy_tir_out)] = {};

	MLX5_SET(destroy_tir_in, in, opcode, MLX5_CMD_OP_DESTROY_TIR);
	MLX5_SET(destroy_tir_in, in, tirn, tirn);
	MLX5_SET(destroy_tir_in, in, uid, uid);
	mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

void mlx5_cmd_destroy_tis(struct mlx5_core_dev *dev, u32 tisn, u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(destroy_tis_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(destroy_tis_out)] = {0};

	MLX5_SET(destroy_tis_in, in, opcode, MLX5_CMD_OP_DESTROY_TIS);
	MLX5_SET(destroy_tis_in, in, tisn, tisn);
	MLX5_SET(destroy_tis_in, in, uid, uid);
	mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

void mlx5_cmd_destroy_rqt(struct mlx5_core_dev *dev, u32 rqtn, u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(destroy_rqt_in)]   = {};
	u32 out[MLX5_ST_SZ_DW(destroy_rqt_out)] = {};

	MLX5_SET(destroy_rqt_in, in, opcode, MLX5_CMD_OP_DESTROY_RQT);
	MLX5_SET(destroy_rqt_in, in, rqtn, rqtn);
	MLX5_SET(destroy_rqt_in, in, uid, uid);
	mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_cmd_alloc_transport_domain(struct mlx5_core_dev *dev, u32 *tdn,
				    u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(alloc_transport_domain_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(alloc_transport_domain_out)] = {0};
	int err;

	MLX5_SET(alloc_transport_domain_in, in, opcode,
		 MLX5_CMD_OP_ALLOC_TRANSPORT_DOMAIN);
	MLX5_SET(alloc_transport_domain_in, in, uid, uid);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*tdn = MLX5_GET(alloc_transport_domain_out, out,
				transport_domain);

	return err;
}

void mlx5_cmd_dealloc_transport_domain(struct mlx5_core_dev *dev, u32 tdn,
				       u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(dealloc_transport_domain_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(dealloc_transport_domain_out)] = {0};

	MLX5_SET(dealloc_transport_domain_in, in, opcode,
		 MLX5_CMD_OP_DEALLOC_TRANSPORT_DOMAIN);
	MLX5_SET(dealloc_transport_domain_in, in, uid, uid);
	MLX5_SET(dealloc_transport_domain_in, in, transport_domain, tdn);
	mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

void mlx5_cmd_dealloc_pd(struct mlx5_core_dev *dev, u32 pdn, u16 uid)
{
	u32 out[MLX5_ST_SZ_DW(dealloc_pd_out)] = {};
	u32 in[MLX5_ST_SZ_DW(dealloc_pd_in)]   = {};

	MLX5_SET(dealloc_pd_in, in, opcode, MLX5_CMD_OP_DEALLOC_PD);
	MLX5_SET(dealloc_pd_in, in, pd, pdn);
	MLX5_SET(dealloc_pd_in, in, uid, uid);
	mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_cmd_attach_mcg(struct mlx5_core_dev *dev, union ib_gid *mgid,
			u32 qpn, u16 uid)
{
	u32 out[MLX5_ST_SZ_DW(attach_to_mcg_out)] = {};
	u32 in[MLX5_ST_SZ_DW(attach_to_mcg_in)]   = {};
	void *gid;

	MLX5_SET(attach_to_mcg_in, in, opcode, MLX5_CMD_OP_ATTACH_TO_MCG);
	MLX5_SET(attach_to_mcg_in, in, qpn, qpn);
	MLX5_SET(attach_to_mcg_in, in, uid, uid);
	gid = MLX5_ADDR_OF(attach_to_mcg_in, in, multicast_gid);
	memcpy(gid, mgid, sizeof(*mgid));
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_cmd_detach_mcg(struct mlx5_core_dev *dev, union ib_gid *mgid,
			u32 qpn, u16 uid)
{
	u32 out[MLX5_ST_SZ_DW(detach_from_mcg_out)] = {};
	u32 in[MLX5_ST_SZ_DW(detach_from_mcg_in)]   = {};
	void *gid;

	MLX5_SET(detach_from_mcg_in, in, opcode, MLX5_CMD_OP_DETACH_FROM_MCG);
	MLX5_SET(detach_from_mcg_in, in, qpn, qpn);
	MLX5_SET(detach_from_mcg_in, in, uid, uid);
	gid = MLX5_ADDR_OF(detach_from_mcg_in, in, multicast_gid);
	memcpy(gid, mgid, sizeof(*mgid));
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_cmd_xrcd_alloc(struct mlx5_core_dev *dev, u32 *xrcdn, u16 uid)
{
	u32 out[MLX5_ST_SZ_DW(alloc_xrcd_out)] = {};
	u32 in[MLX5_ST_SZ_DW(alloc_xrcd_in)]   = {};
	int err;

	MLX5_SET(alloc_xrcd_in, in, opcode, MLX5_CMD_OP_ALLOC_XRCD);
	MLX5_SET(alloc_xrcd_in, in, uid, uid);
	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*xrcdn = MLX5_GET(alloc_xrcd_out, out, xrcd);
	return err;
}

int mlx5_cmd_xrcd_dealloc(struct mlx5_core_dev *dev, u32 xrcdn, u16 uid)
{
	u32 out[MLX5_ST_SZ_DW(dealloc_xrcd_out)] = {};
	u32 in[MLX5_ST_SZ_DW(dealloc_xrcd_in)]   = {};

	MLX5_SET(dealloc_xrcd_in, in, opcode, MLX5_CMD_OP_DEALLOC_XRCD);
	MLX5_SET(dealloc_xrcd_in, in, xrcd, xrcdn);
	MLX5_SET(dealloc_xrcd_in, in, uid, uid);
	return mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
}

int mlx5_cmd_alloc_q_counter(struct mlx5_core_dev *dev, u16 *counter_id,
			     u16 uid)
{
	u32 in[MLX5_ST_SZ_DW(alloc_q_counter_in)]   = {0};
	u32 out[MLX5_ST_SZ_DW(alloc_q_counter_out)] = {0};
	int err;

	MLX5_SET(alloc_q_counter_in, in, opcode, MLX5_CMD_OP_ALLOC_Q_COUNTER);
	MLX5_SET(alloc_q_counter_in, in, uid, uid);

	err = mlx5_cmd_exec(dev, in, sizeof(in), out, sizeof(out));
	if (!err)
		*counter_id = MLX5_GET(alloc_q_counter_out, out,
				       counter_set_id);
	return err;
}

int mlx5_cmd_mad_ifc(struct mlx5_core_dev *dev, const void *inb, void *outb,
		     u16 opmod, u8 port)
{
	int outlen = MLX5_ST_SZ_BYTES(mad_ifc_out);
	int inlen = MLX5_ST_SZ_BYTES(mad_ifc_in);
	int err = -ENOMEM;
	void *data;
	void *resp;
	u32 *out;
	u32 *in;

	in = kzalloc(inlen, GFP_KERNEL);
	out = kzalloc(outlen, GFP_KERNEL);
	if (!in || !out)
		goto out;

	MLX5_SET(mad_ifc_in, in, opcode, MLX5_CMD_OP_MAD_IFC);
	MLX5_SET(mad_ifc_in, in, op_mod, opmod);
	MLX5_SET(mad_ifc_in, in, port, port);

	data = MLX5_ADDR_OF(mad_ifc_in, in, mad);
	memcpy(data, inb, MLX5_FLD_SZ_BYTES(mad_ifc_in, mad));

	err = mlx5_cmd_exec(dev, in, inlen, out, outlen);
	if (err)
		goto out;

	resp = MLX5_ADDR_OF(mad_ifc_out, out, response_mad_packet);
	memcpy(outb, resp,
	       MLX5_FLD_SZ_BYTES(mad_ifc_out, response_mad_packet));

out:
	kfree(out);
	kfree(in);
	return err;
}
