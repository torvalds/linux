// SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause)
/* Copyright (C) 2018 Netronome Systems, Inc. */

#include <linux/kernel.h>
#include <net/devlink.h>

#include "nfpcore/nfp_cpp.h"
#include "nfpcore/nfp_nffw.h"
#include "nfp_abi.h"
#include "nfp_app.h"
#include "nfp_main.h"

static u32 nfp_shared_buf_pool_unit(struct nfp_pf *pf, unsigned int sb)
{
	__le32 sb_id = cpu_to_le32(sb);
	unsigned int i;

	for (i = 0; i < pf->num_shared_bufs; i++)
		if (pf->shared_bufs[i].id == sb_id)
			return le32_to_cpu(pf->shared_bufs[i].pool_size_unit);

	WARN_ON_ONCE(1);
	return 0;
}

int nfp_shared_buf_pool_get(struct nfp_pf *pf, unsigned int sb, u16 pool_index,
			    struct devlink_sb_pool_info *pool_info)
{
	struct nfp_shared_buf_pool_info_get get_data;
	struct nfp_shared_buf_pool_id id = {
		.shared_buf	= cpu_to_le32(sb),
		.pool		= cpu_to_le32(pool_index),
	};
	unsigned int unit_size;
	int n;

	unit_size = nfp_shared_buf_pool_unit(pf, sb);
	if (!unit_size)
		return -EINVAL;

	n = nfp_mbox_cmd(pf, NFP_MBOX_POOL_GET, &id, sizeof(id),
			 &get_data, sizeof(get_data));
	if (n < 0)
		return n;
	if (n < sizeof(get_data))
		return -EIO;

	pool_info->pool_type = le32_to_cpu(get_data.pool_type);
	pool_info->threshold_type = le32_to_cpu(get_data.threshold_type);
	pool_info->size = le32_to_cpu(get_data.size) * unit_size;

	return 0;
}

int nfp_shared_buf_pool_set(struct nfp_pf *pf, unsigned int sb,
			    u16 pool_index, u32 size,
			    enum devlink_sb_threshold_type threshold_type)
{
	struct nfp_shared_buf_pool_info_set set_data = {
		.id = {
			.shared_buf	= cpu_to_le32(sb),
			.pool		= cpu_to_le32(pool_index),
		},
		.threshold_type	= cpu_to_le32(threshold_type),
	};
	unsigned int unit_size;

	unit_size = nfp_shared_buf_pool_unit(pf, sb);
	if (!unit_size || size % unit_size)
		return -EINVAL;
	set_data.size = cpu_to_le32(size / unit_size);

	return nfp_mbox_cmd(pf, NFP_MBOX_POOL_SET, &set_data, sizeof(set_data),
			    NULL, 0);
}

int nfp_shared_buf_register(struct nfp_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	unsigned int i, num_entries, entry_sz;
	struct nfp_cpp_area *sb_desc_area;
	u8 __iomem *sb_desc;
	int n, err;

	if (!pf->mbox)
		return 0;

	n = nfp_pf_rtsym_read_optional(pf, NFP_SHARED_BUF_COUNT_SYM_NAME, 0);
	if (n <= 0)
		return n;
	num_entries = n;

	sb_desc = nfp_pf_map_rtsym(pf, "sb_tbl", NFP_SHARED_BUF_TABLE_SYM_NAME,
				   num_entries * sizeof(pf->shared_bufs[0]),
				   &sb_desc_area);
	if (IS_ERR(sb_desc))
		return PTR_ERR(sb_desc);

	entry_sz = nfp_cpp_area_size(sb_desc_area) / num_entries;

	pf->shared_bufs = kmalloc_array(num_entries, sizeof(pf->shared_bufs[0]),
					GFP_KERNEL);
	if (!pf->shared_bufs) {
		err = -ENOMEM;
		goto err_release_area;
	}

	for (i = 0; i < num_entries; i++) {
		struct nfp_shared_buf *sb = &pf->shared_bufs[i];

		/* Entries may be larger in future FW */
		memcpy_fromio(sb, sb_desc + i * entry_sz, sizeof(*sb));

		err = devlink_sb_register(devlink,
					  le32_to_cpu(sb->id),
					  le32_to_cpu(sb->size),
					  le16_to_cpu(sb->ingress_pools_count),
					  le16_to_cpu(sb->egress_pools_count),
					  le16_to_cpu(sb->ingress_tc_count),
					  le16_to_cpu(sb->egress_tc_count));
		if (err)
			goto err_unreg_prev;
	}
	pf->num_shared_bufs = num_entries;

	nfp_cpp_area_release_free(sb_desc_area);

	return 0;

err_unreg_prev:
	while (i--)
		devlink_sb_unregister(devlink,
				      le32_to_cpu(pf->shared_bufs[i].id));
	kfree(pf->shared_bufs);
err_release_area:
	nfp_cpp_area_release_free(sb_desc_area);
	return err;
}

void nfp_shared_buf_unregister(struct nfp_pf *pf)
{
	struct devlink *devlink = priv_to_devlink(pf);
	unsigned int i;

	for (i = 0; i < pf->num_shared_bufs; i++)
		devlink_sb_unregister(devlink,
				      le32_to_cpu(pf->shared_bufs[i].id));
	kfree(pf->shared_bufs);
}
