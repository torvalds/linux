// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2021, Mellanox Technologies inc. All rights reserved.
 */

#include <rdma/uverbs_std_types.h>
#include "dm.h"

#define UVERBS_MODULE_NAME mlx5_ib
#include <rdma/uverbs_named_ioctl.h>

static int mlx5_cmd_alloc_memic(struct mlx5_dm *dm, phys_addr_t *addr,
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

		ret = mlx5_cmd_exec_inout(dev, alloc_memic, in, out);
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

void mlx5_cmd_dealloc_memic(struct mlx5_dm *dm, phys_addr_t addr,
			    u64 length)
{
	struct mlx5_core_dev *dev = dm->dev;
	u64 hw_start_addr = MLX5_CAP64_DEV_MEM(dev, memic_bar_start_addr);
	u32 num_pages = DIV_ROUND_UP(length, PAGE_SIZE);
	u32 in[MLX5_ST_SZ_DW(dealloc_memic_in)] = {};
	u64 start_page_idx;
	int err;

	addr -= dev->bar_addr;
	start_page_idx = (addr - hw_start_addr) >> PAGE_SHIFT;

	MLX5_SET(dealloc_memic_in, in, opcode, MLX5_CMD_OP_DEALLOC_MEMIC);
	MLX5_SET64(dealloc_memic_in, in, memic_start_addr, addr);
	MLX5_SET(dealloc_memic_in, in, memic_size, length);

	err =  mlx5_cmd_exec_in(dev, dealloc_memic, in);
	if (err)
		return;

	spin_lock(&dm->lock);
	bitmap_clear(dm->memic_alloc_pages,
		     start_page_idx, num_pages);
	spin_unlock(&dm->lock);
}

static int add_dm_mmap_entry(struct ib_ucontext *context,
			     struct mlx5_ib_dm_memic *mdm, u64 address)
{
	mdm->mentry.mmap_flag = MLX5_IB_MMAP_TYPE_MEMIC;
	mdm->mentry.address = address;
	return rdma_user_mmap_entry_insert_range(
		context, &mdm->mentry.rdma_entry, mdm->base.size,
		MLX5_IB_MMAP_DEVICE_MEM << 16,
		(MLX5_IB_MMAP_DEVICE_MEM << 16) + (1UL << 16) - 1);
}

static struct ib_dm *handle_alloc_dm_memic(struct ib_ucontext *ctx,
					   struct ib_dm_alloc_attr *attr,
					   struct uverbs_attr_bundle *attrs)
{
	struct mlx5_dm *dm_db = &to_mdev(ctx->device)->dm;
	struct mlx5_ib_dm_memic *dm;
	u64 start_offset;
	u16 page_idx;
	int err;
	u64 address;

	if (!MLX5_CAP_DEV_MEM(dm_db->dev, memic))
		return ERR_PTR(-EOPNOTSUPP);

	dm = kzalloc(sizeof(*dm), GFP_KERNEL);
	if (!dm)
		return ERR_PTR(-ENOMEM);

	dm->base.type = MLX5_IB_UAPI_DM_TYPE_MEMIC;
	dm->base.size = roundup(attr->length, MLX5_MEMIC_BASE_SIZE);

	err = mlx5_cmd_alloc_memic(dm_db, &dm->base.dev_addr,
				   dm->base.size, attr->alignment);
	if (err) {
		kfree(dm);
		return ERR_PTR(err);
	}

	address = dm->base.dev_addr & PAGE_MASK;
	err = add_dm_mmap_entry(ctx, dm, address);
	if (err) {
		mlx5_cmd_dealloc_memic(dm_db, dm->base.dev_addr, dm->base.size);
		kfree(dm);
		return ERR_PTR(err);
	}

	page_idx = dm->mentry.rdma_entry.start_pgoff & 0xFFFF;
	err = uverbs_copy_to(attrs,
			     MLX5_IB_ATTR_ALLOC_DM_RESP_PAGE_INDEX,
			     &page_idx,
			     sizeof(page_idx));
	if (err)
		goto err_copy;

	start_offset = dm->base.dev_addr & ~PAGE_MASK;
	err = uverbs_copy_to(attrs,
			     MLX5_IB_ATTR_ALLOC_DM_RESP_START_OFFSET,
			     &start_offset, sizeof(start_offset));
	if (err)
		goto err_copy;

	return &dm->base.ibdm;

err_copy:
	rdma_user_mmap_entry_remove(&dm->mentry.rdma_entry);
	return ERR_PTR(err);
}

static struct ib_dm *handle_alloc_dm_sw_icm(struct ib_ucontext *ctx,
					    struct ib_dm_alloc_attr *attr,
					    struct uverbs_attr_bundle *attrs,
					    int type)
{
	struct mlx5_core_dev *dev = to_mdev(ctx->device)->mdev;
	struct mlx5_ib_dm_icm *dm;
	u64 act_size;
	int err;

	dm = kzalloc(sizeof(*dm), GFP_KERNEL);
	if (!dm)
		return ERR_PTR(-ENOMEM);

	dm->base.type = type;

	if (!capable(CAP_SYS_RAWIO) || !capable(CAP_NET_RAW)) {
		err = -EPERM;
		goto free;
	}

	if (!(MLX5_CAP_FLOWTABLE_NIC_RX(dev, sw_owner) ||
	      MLX5_CAP_FLOWTABLE_NIC_TX(dev, sw_owner) ||
	      MLX5_CAP_FLOWTABLE_NIC_RX(dev, sw_owner_v2) ||
	      MLX5_CAP_FLOWTABLE_NIC_TX(dev, sw_owner_v2))) {
		err = -EOPNOTSUPP;
		goto free;
	}

	/* Allocation size must a multiple of the basic block size
	 * and a power of 2.
	 */
	act_size = round_up(attr->length, MLX5_SW_ICM_BLOCK_SIZE(dev));
	act_size = roundup_pow_of_two(act_size);

	dm->base.size = act_size;
	err = mlx5_dm_sw_icm_alloc(dev, type, act_size, attr->alignment,
				   to_mucontext(ctx)->devx_uid,
				   &dm->base.dev_addr, &dm->obj_id);
	if (err)
		goto free;

	err = uverbs_copy_to(attrs, MLX5_IB_ATTR_ALLOC_DM_RESP_START_OFFSET,
			     &dm->base.dev_addr, sizeof(dm->base.dev_addr));
	if (err) {
		mlx5_dm_sw_icm_dealloc(dev, type, dm->base.size,
				       to_mucontext(ctx)->devx_uid,
				       dm->base.dev_addr, dm->obj_id);
		goto free;
	}
	return &dm->base.ibdm;
free:
	kfree(dm);
	return ERR_PTR(err);
}

struct ib_dm *mlx5_ib_alloc_dm(struct ib_device *ibdev,
			       struct ib_ucontext *context,
			       struct ib_dm_alloc_attr *attr,
			       struct uverbs_attr_bundle *attrs)
{
	enum mlx5_ib_uapi_dm_type type;
	int err;

	err = uverbs_get_const_default(&type, attrs,
				       MLX5_IB_ATTR_ALLOC_DM_REQ_TYPE,
				       MLX5_IB_UAPI_DM_TYPE_MEMIC);
	if (err)
		return ERR_PTR(err);

	mlx5_ib_dbg(to_mdev(ibdev), "alloc_dm req: dm_type=%d user_length=0x%llx log_alignment=%d\n",
		    type, attr->length, attr->alignment);

	switch (type) {
	case MLX5_IB_UAPI_DM_TYPE_MEMIC:
		return handle_alloc_dm_memic(context, attr, attrs);
	case MLX5_IB_UAPI_DM_TYPE_STEERING_SW_ICM:
		return handle_alloc_dm_sw_icm(context, attr, attrs,
					     MLX5_SW_ICM_TYPE_STEERING);
	case MLX5_IB_UAPI_DM_TYPE_HEADER_MODIFY_SW_ICM:
		return handle_alloc_dm_sw_icm(context, attr, attrs,
					      MLX5_SW_ICM_TYPE_HEADER_MODIFY);
	default:
		return ERR_PTR(-EOPNOTSUPP);
	}
}

static void mlx5_dm_memic_dealloc(struct mlx5_ib_dm_memic *dm)
{
	rdma_user_mmap_entry_remove(&dm->mentry.rdma_entry);
}

static int mlx5_dm_icm_dealloc(struct mlx5_ib_ucontext *ctx,
			       struct mlx5_ib_dm_icm *dm)
{
	enum mlx5_sw_icm_type type =
		dm->base.type == MLX5_IB_UAPI_DM_TYPE_STEERING_SW_ICM ?
			MLX5_SW_ICM_TYPE_STEERING :
			MLX5_SW_ICM_TYPE_HEADER_MODIFY;
	struct mlx5_core_dev *dev = to_mdev(dm->base.ibdm.device)->mdev;
	int err;

	err = mlx5_dm_sw_icm_dealloc(dev, type, dm->base.size, ctx->devx_uid,
				     dm->base.dev_addr, dm->obj_id);
	if (!err)
		kfree(dm);
	return 0;
}

static int mlx5_ib_dealloc_dm(struct ib_dm *ibdm,
			      struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_ucontext *ctx = rdma_udata_to_drv_context(
		&attrs->driver_udata, struct mlx5_ib_ucontext, ibucontext);
	struct mlx5_ib_dm *dm = to_mdm(ibdm);

	switch (dm->type) {
	case MLX5_IB_UAPI_DM_TYPE_MEMIC:
		mlx5_dm_memic_dealloc(to_memic(ibdm));
		return 0;
	case MLX5_IB_UAPI_DM_TYPE_STEERING_SW_ICM:
	case MLX5_IB_UAPI_DM_TYPE_HEADER_MODIFY_SW_ICM:
		return mlx5_dm_icm_dealloc(ctx, to_icm(ibdm));
	default:
		return -EOPNOTSUPP;
	}
}

ADD_UVERBS_ATTRIBUTES_SIMPLE(
	mlx5_ib_dm, UVERBS_OBJECT_DM, UVERBS_METHOD_DM_ALLOC,
	UVERBS_ATTR_PTR_OUT(MLX5_IB_ATTR_ALLOC_DM_RESP_START_OFFSET,
			    UVERBS_ATTR_TYPE(u64), UA_MANDATORY),
	UVERBS_ATTR_PTR_OUT(MLX5_IB_ATTR_ALLOC_DM_RESP_PAGE_INDEX,
			    UVERBS_ATTR_TYPE(u16), UA_OPTIONAL),
	UVERBS_ATTR_CONST_IN(MLX5_IB_ATTR_ALLOC_DM_REQ_TYPE,
			     enum mlx5_ib_uapi_dm_type, UA_OPTIONAL));

const struct uapi_definition mlx5_ib_dm_defs[] = {
	UAPI_DEF_CHAIN_OBJ_TREE(UVERBS_OBJECT_DM, &mlx5_ib_dm),
	{},
};

const struct ib_device_ops mlx5_ib_dev_dm_ops = {
	.alloc_dm = mlx5_ib_alloc_dm,
	.dealloc_dm = mlx5_ib_dealloc_dm,
	.reg_dm_mr = mlx5_ib_reg_dm_mr,
};
