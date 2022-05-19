// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2019 Mellanox Technologies. */

#include "rsc_dump.h"
#include "lib/mlx5.h"

#define MLX5_SGMT_TYPE(SGMT) MLX5_SGMT_TYPE_##SGMT
#define MLX5_SGMT_STR_ASSING(SGMT)[MLX5_SGMT_TYPE(SGMT)] = #SGMT
static const char *const mlx5_rsc_sgmt_name[] = {
	MLX5_SGMT_STR_ASSING(HW_CQPC),
	MLX5_SGMT_STR_ASSING(HW_SQPC),
	MLX5_SGMT_STR_ASSING(HW_RQPC),
	MLX5_SGMT_STR_ASSING(FULL_SRQC),
	MLX5_SGMT_STR_ASSING(FULL_CQC),
	MLX5_SGMT_STR_ASSING(FULL_EQC),
	MLX5_SGMT_STR_ASSING(FULL_QPC),
	MLX5_SGMT_STR_ASSING(SND_BUFF),
	MLX5_SGMT_STR_ASSING(RCV_BUFF),
	MLX5_SGMT_STR_ASSING(SRQ_BUFF),
	MLX5_SGMT_STR_ASSING(CQ_BUFF),
	MLX5_SGMT_STR_ASSING(EQ_BUFF),
	MLX5_SGMT_STR_ASSING(SX_SLICE),
	MLX5_SGMT_STR_ASSING(SX_SLICE_ALL),
	MLX5_SGMT_STR_ASSING(RDB),
	MLX5_SGMT_STR_ASSING(RX_SLICE_ALL),
	MLX5_SGMT_STR_ASSING(PRM_QUERY_QP),
	MLX5_SGMT_STR_ASSING(PRM_QUERY_CQ),
	MLX5_SGMT_STR_ASSING(PRM_QUERY_MKEY),
};

struct mlx5_rsc_dump {
	u32 pdn;
	struct mlx5_core_mkey mkey;
	u32 number_of_menu_items;
	u16 fw_segment_type[MLX5_SGMT_TYPE_NUM];
};

struct mlx5_rsc_dump_cmd {
	u64 mem_size;
	u8 cmd[MLX5_ST_SZ_BYTES(resource_dump)];
};

static int mlx5_rsc_dump_sgmt_get_by_name(char *name)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(mlx5_rsc_sgmt_name); i++)
		if (!strcmp(name, mlx5_rsc_sgmt_name[i]))
			return i;

	return -EINVAL;
}

#define MLX5_RSC_DUMP_MENU_HEADER_SIZE (MLX5_ST_SZ_BYTES(resource_dump_info_segment) + \
					MLX5_ST_SZ_BYTES(resource_dump_command_segment) + \
					MLX5_ST_SZ_BYTES(resource_dump_menu_segment))

static int mlx5_rsc_dump_read_menu_sgmt(struct mlx5_rsc_dump *rsc_dump, struct page *page,
					int read_size, int start_idx)
{
	void *data = page_address(page);
	enum mlx5_sgmt_type sgmt_idx;
	int num_of_items;
	char *sgmt_name;
	void *member;
	int size = 0;
	void *menu;
	int i;

	if (!start_idx) {
		menu = MLX5_ADDR_OF(menu_resource_dump_response, data, menu);
		rsc_dump->number_of_menu_items = MLX5_GET(resource_dump_menu_segment, menu,
							  num_of_records);
		size = MLX5_RSC_DUMP_MENU_HEADER_SIZE;
		data += size;
	}
	num_of_items = rsc_dump->number_of_menu_items;

	for (i = 0; start_idx + i < num_of_items; i++) {
		size += MLX5_ST_SZ_BYTES(resource_dump_menu_record);
		if (size >= read_size)
			return start_idx + i;

		member = data + MLX5_ST_SZ_BYTES(resource_dump_menu_record) * i;
		sgmt_name =  MLX5_ADDR_OF(resource_dump_menu_record, member, segment_name);
		sgmt_idx = mlx5_rsc_dump_sgmt_get_by_name(sgmt_name);
		if (sgmt_idx == -EINVAL)
			continue;
		rsc_dump->fw_segment_type[sgmt_idx] = MLX5_GET(resource_dump_menu_record,
							       member, segment_type);
	}
	return 0;
}

static int mlx5_rsc_dump_trigger(struct mlx5_core_dev *dev, struct mlx5_rsc_dump_cmd *cmd,
				 struct page *page)
{
	struct mlx5_rsc_dump *rsc_dump = dev->rsc_dump;
	struct device *ddev = mlx5_core_dma_dev(dev);
	u32 out_seq_num;
	u32 in_seq_num;
	dma_addr_t dma;
	int err;

	dma = dma_map_page(ddev, page, 0, cmd->mem_size, DMA_FROM_DEVICE);
	if (unlikely(dma_mapping_error(ddev, dma)))
		return -ENOMEM;

	in_seq_num = MLX5_GET(resource_dump, cmd->cmd, seq_num);
	MLX5_SET(resource_dump, cmd->cmd, mkey, rsc_dump->mkey.key);
	MLX5_SET64(resource_dump, cmd->cmd, address, dma);

	err = mlx5_core_access_reg(dev, cmd->cmd, sizeof(cmd->cmd), cmd->cmd,
				   sizeof(cmd->cmd), MLX5_REG_RESOURCE_DUMP, 0, 1);
	if (err) {
		mlx5_core_err(dev, "Resource dump: Failed to access err %d\n", err);
		goto out;
	}
	out_seq_num = MLX5_GET(resource_dump, cmd->cmd, seq_num);
	if (out_seq_num && (in_seq_num + 1 != out_seq_num))
		err = -EIO;
out:
	dma_unmap_page(ddev, dma, cmd->mem_size, DMA_FROM_DEVICE);
	return err;
}

struct mlx5_rsc_dump_cmd *mlx5_rsc_dump_cmd_create(struct mlx5_core_dev *dev,
						   struct mlx5_rsc_key *key)
{
	struct mlx5_rsc_dump_cmd *cmd;
	int sgmt_type;

	if (IS_ERR_OR_NULL(dev->rsc_dump))
		return ERR_PTR(-EOPNOTSUPP);

	sgmt_type = dev->rsc_dump->fw_segment_type[key->rsc];
	if (!sgmt_type && key->rsc != MLX5_SGMT_TYPE_MENU)
		return ERR_PTR(-EOPNOTSUPP);

	cmd = kzalloc(sizeof(*cmd), GFP_KERNEL);
	if (!cmd) {
		mlx5_core_err(dev, "Resource dump: Failed to allocate command\n");
		return ERR_PTR(-ENOMEM);
	}
	MLX5_SET(resource_dump, cmd->cmd, segment_type, sgmt_type);
	MLX5_SET(resource_dump, cmd->cmd, index1, key->index1);
	MLX5_SET(resource_dump, cmd->cmd, index2, key->index2);
	MLX5_SET(resource_dump, cmd->cmd, num_of_obj1, key->num_of_obj1);
	MLX5_SET(resource_dump, cmd->cmd, num_of_obj2, key->num_of_obj2);
	MLX5_SET(resource_dump, cmd->cmd, size, key->size);
	cmd->mem_size = key->size;
	return cmd;
}
EXPORT_SYMBOL(mlx5_rsc_dump_cmd_create);

void mlx5_rsc_dump_cmd_destroy(struct mlx5_rsc_dump_cmd *cmd)
{
	kfree(cmd);
}
EXPORT_SYMBOL(mlx5_rsc_dump_cmd_destroy);

int mlx5_rsc_dump_next(struct mlx5_core_dev *dev, struct mlx5_rsc_dump_cmd *cmd,
		       struct page *page, int *size)
{
	bool more_dump;
	int err;

	if (IS_ERR_OR_NULL(dev->rsc_dump))
		return -EOPNOTSUPP;

	err = mlx5_rsc_dump_trigger(dev, cmd, page);
	if (err) {
		mlx5_core_err(dev, "Resource dump: Failed to trigger dump, %d\n", err);
		return err;
	}
	*size = MLX5_GET(resource_dump, cmd->cmd, size);
	more_dump = MLX5_GET(resource_dump, cmd->cmd, more_dump);

	return more_dump;
}
EXPORT_SYMBOL(mlx5_rsc_dump_next);

#define MLX5_RSC_DUMP_MENU_SEGMENT 0xffff
static int mlx5_rsc_dump_menu(struct mlx5_core_dev *dev)
{
	struct mlx5_rsc_dump_cmd *cmd = NULL;
	struct mlx5_rsc_key key = {};
	struct page *page;
	int start_idx = 0;
	int size;
	int err;

	page = alloc_page(GFP_KERNEL);
	if (!page)
		return -ENOMEM;

	key.rsc = MLX5_SGMT_TYPE_MENU;
	key.size = PAGE_SIZE;
	cmd  = mlx5_rsc_dump_cmd_create(dev, &key);
	if (IS_ERR(cmd)) {
		err = PTR_ERR(cmd);
		goto free_page;
	}
	MLX5_SET(resource_dump, cmd->cmd, segment_type, MLX5_RSC_DUMP_MENU_SEGMENT);

	do {
		err = mlx5_rsc_dump_next(dev, cmd, page, &size);
		if (err < 0)
			goto destroy_cmd;

		start_idx = mlx5_rsc_dump_read_menu_sgmt(dev->rsc_dump, page, size, start_idx);

	} while (err > 0);

destroy_cmd:
	mlx5_rsc_dump_cmd_destroy(cmd);
free_page:
	__free_page(page);

	return err;
}

static int mlx5_rsc_dump_create_mkey(struct mlx5_core_dev *mdev, u32 pdn,
				     struct mlx5_core_mkey *mkey)
{
	int inlen = MLX5_ST_SZ_BYTES(create_mkey_in);
	void *mkc;
	u32 *in;
	int err;

	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, access_mode_1_0, MLX5_MKC_ACCESS_MODE_PA);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);

	MLX5_SET(mkc, mkc, pd, pdn);
	MLX5_SET(mkc, mkc, length64, 1);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);

	err = mlx5_core_create_mkey(mdev, mkey, in, inlen);

	kvfree(in);
	return err;
}

struct mlx5_rsc_dump *mlx5_rsc_dump_create(struct mlx5_core_dev *dev)
{
	struct mlx5_rsc_dump *rsc_dump;

	if (!MLX5_CAP_DEBUG(dev, resource_dump)) {
		mlx5_core_dbg(dev, "Resource dump: capability not present\n");
		return NULL;
	}
	rsc_dump = kzalloc(sizeof(*rsc_dump), GFP_KERNEL);
	if (!rsc_dump)
		return ERR_PTR(-ENOMEM);

	return rsc_dump;
}

void mlx5_rsc_dump_destroy(struct mlx5_core_dev *dev)
{
	if (IS_ERR_OR_NULL(dev->rsc_dump))
		return;
	kfree(dev->rsc_dump);
}

int mlx5_rsc_dump_init(struct mlx5_core_dev *dev)
{
	struct mlx5_rsc_dump *rsc_dump = dev->rsc_dump;
	int err;

	if (IS_ERR_OR_NULL(dev->rsc_dump))
		return 0;

	err = mlx5_core_alloc_pd(dev, &rsc_dump->pdn);
	if (err) {
		mlx5_core_warn(dev, "Resource dump: Failed to allocate PD %d\n", err);
		return err;
	}
	err = mlx5_rsc_dump_create_mkey(dev, rsc_dump->pdn, &rsc_dump->mkey);
	if (err) {
		mlx5_core_err(dev, "Resource dump: Failed to create mkey, %d\n", err);
		goto free_pd;
	}
	err = mlx5_rsc_dump_menu(dev);
	if (err) {
		mlx5_core_err(dev, "Resource dump: Failed to read menu, %d\n", err);
		goto destroy_mkey;
	}
	return err;

destroy_mkey:
	mlx5_core_destroy_mkey(dev, &rsc_dump->mkey);
free_pd:
	mlx5_core_dealloc_pd(dev, rsc_dump->pdn);
	return err;
}

void mlx5_rsc_dump_cleanup(struct mlx5_core_dev *dev)
{
	if (IS_ERR_OR_NULL(dev->rsc_dump))
		return;

	mlx5_core_destroy_mkey(dev, &dev->rsc_dump->mkey);
	mlx5_core_dealloc_pd(dev, dev->rsc_dump->pdn);
}
