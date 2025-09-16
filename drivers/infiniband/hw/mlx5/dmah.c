// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2025, NVIDIA CORPORATION & AFFILIATES. All rights reserved
 */

#include <rdma/uverbs_std_types.h>
#include <linux/pci-tph.h>
#include "dmah.h"

#define UVERBS_MODULE_NAME mlx5_ib
#include <rdma/uverbs_named_ioctl.h>

static int mlx5_ib_alloc_dmah(struct ib_dmah *ibdmah,
			      struct uverbs_attr_bundle *attrs)
{
	struct mlx5_core_dev *mdev = to_mdev(ibdmah->device)->mdev;
	struct mlx5_ib_dmah *dmah = to_mdmah(ibdmah);
	u16 st_bits = BIT(IB_DMAH_CPU_ID_EXISTS) |
		      BIT(IB_DMAH_MEM_TYPE_EXISTS);
	int err;

	/* PH is a must for TPH following PCIe spec 6.2-1.0 */
	if (!(ibdmah->valid_fields & BIT(IB_DMAH_PH_EXISTS)))
		return -EINVAL;

	/* ST is optional; however, partial data for it is not allowed */
	if (ibdmah->valid_fields & st_bits) {
		if ((ibdmah->valid_fields & st_bits) != st_bits)
			return -EINVAL;
		err = mlx5_st_alloc_index(mdev, ibdmah->mem_type,
					  ibdmah->cpu_id, &dmah->st_index);
		if (err)
			return err;
	}

	return 0;
}

static int mlx5_ib_dealloc_dmah(struct ib_dmah *ibdmah,
				struct uverbs_attr_bundle *attrs)
{
	struct mlx5_ib_dmah *dmah = to_mdmah(ibdmah);
	struct mlx5_core_dev *mdev = to_mdev(ibdmah->device)->mdev;

	if (ibdmah->valid_fields & BIT(IB_DMAH_CPU_ID_EXISTS))
		return mlx5_st_dealloc_index(mdev, dmah->st_index);

	return 0;
}

const struct ib_device_ops mlx5_ib_dev_dmah_ops = {
	.alloc_dmah = mlx5_ib_alloc_dmah,
	.dealloc_dmah = mlx5_ib_dealloc_dmah,
};
