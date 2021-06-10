// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/* Copyright (c) 2020 Mellanox Technologies Ltd. */

#include <linux/vdpa.h>
#include <linux/gcd.h>
#include <linux/string.h>
#include <linux/mlx5/qp.h>
#include "mlx5_vdpa.h"

/* DIV_ROUND_UP where the divider is a power of 2 give by its log base 2 value */
#define MLX5_DIV_ROUND_UP_POW2(_n, _s) \
({ \
	u64 __s = _s; \
	u64 _res; \
	_res = (((_n) + (1 << (__s)) - 1) >> (__s)); \
	_res; \
})

static int get_octo_len(u64 len, int page_shift)
{
	u64 page_size = 1ULL << page_shift;
	int npages;

	npages = ALIGN(len, page_size) >> page_shift;
	return (npages + 1) / 2;
}

static void mlx5_set_access_mode(void *mkc, int mode)
{
	MLX5_SET(mkc, mkc, access_mode_1_0, mode & 0x3);
	MLX5_SET(mkc, mkc, access_mode_4_2, mode >> 2);
}

static void populate_mtts(struct mlx5_vdpa_direct_mr *mr, __be64 *mtt)
{
	struct scatterlist *sg;
	int nsg = mr->nsg;
	u64 dma_addr;
	u64 dma_len;
	int j = 0;
	int i;

	for_each_sg(mr->sg_head.sgl, sg, mr->nent, i) {
		for (dma_addr = sg_dma_address(sg), dma_len = sg_dma_len(sg);
		     nsg && dma_len;
		     nsg--, dma_addr += BIT(mr->log_size), dma_len -= BIT(mr->log_size))
			mtt[j++] = cpu_to_be64(dma_addr);
	}
}

static int create_direct_mr(struct mlx5_vdpa_dev *mvdev, struct mlx5_vdpa_direct_mr *mr)
{
	int inlen;
	void *mkc;
	void *in;
	int err;

	inlen = MLX5_ST_SZ_BYTES(create_mkey_in) + roundup(MLX5_ST_SZ_BYTES(mtt) * mr->nsg, 16);
	in = kvzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_mkey_in, in, uid, mvdev->res.uid);
	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, lw, !!(mr->perm & VHOST_MAP_WO));
	MLX5_SET(mkc, mkc, lr, !!(mr->perm & VHOST_MAP_RO));
	mlx5_set_access_mode(mkc, MLX5_MKC_ACCESS_MODE_MTT);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, pd, mvdev->res.pdn);
	MLX5_SET64(mkc, mkc, start_addr, mr->offset);
	MLX5_SET64(mkc, mkc, len, mr->end - mr->start);
	MLX5_SET(mkc, mkc, log_page_size, mr->log_size);
	MLX5_SET(mkc, mkc, translations_octword_size,
		 get_octo_len(mr->end - mr->start, mr->log_size));
	MLX5_SET(create_mkey_in, in, translations_octword_actual_size,
		 get_octo_len(mr->end - mr->start, mr->log_size));
	populate_mtts(mr, MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt));
	err = mlx5_vdpa_create_mkey(mvdev, &mr->mr, in, inlen);
	kvfree(in);
	if (err) {
		mlx5_vdpa_warn(mvdev, "Failed to create direct MR\n");
		return err;
	}

	return 0;
}

static void destroy_direct_mr(struct mlx5_vdpa_dev *mvdev, struct mlx5_vdpa_direct_mr *mr)
{
	mlx5_vdpa_destroy_mkey(mvdev, &mr->mr);
}

static u64 map_start(struct vhost_iotlb_map *map, struct mlx5_vdpa_direct_mr *mr)
{
	return max_t(u64, map->start, mr->start);
}

static u64 map_end(struct vhost_iotlb_map *map, struct mlx5_vdpa_direct_mr *mr)
{
	return min_t(u64, map->last + 1, mr->end);
}

static u64 maplen(struct vhost_iotlb_map *map, struct mlx5_vdpa_direct_mr *mr)
{
	return map_end(map, mr) - map_start(map, mr);
}

#define MLX5_VDPA_INVALID_START_ADDR ((u64)-1)
#define MLX5_VDPA_INVALID_LEN ((u64)-1)

static u64 indir_start_addr(struct mlx5_vdpa_mr *mkey)
{
	struct mlx5_vdpa_direct_mr *s;

	s = list_first_entry_or_null(&mkey->head, struct mlx5_vdpa_direct_mr, list);
	if (!s)
		return MLX5_VDPA_INVALID_START_ADDR;

	return s->start;
}

static u64 indir_len(struct mlx5_vdpa_mr *mkey)
{
	struct mlx5_vdpa_direct_mr *s;
	struct mlx5_vdpa_direct_mr *e;

	s = list_first_entry_or_null(&mkey->head, struct mlx5_vdpa_direct_mr, list);
	if (!s)
		return MLX5_VDPA_INVALID_LEN;

	e = list_last_entry(&mkey->head, struct mlx5_vdpa_direct_mr, list);

	return e->end - s->start;
}

#define LOG_MAX_KLM_SIZE 30
#define MAX_KLM_SIZE BIT(LOG_MAX_KLM_SIZE)

static u32 klm_bcount(u64 size)
{
	return (u32)size;
}

static void fill_indir(struct mlx5_vdpa_dev *mvdev, struct mlx5_vdpa_mr *mkey, void *in)
{
	struct mlx5_vdpa_direct_mr *dmr;
	struct mlx5_klm *klmarr;
	struct mlx5_klm *klm;
	bool first = true;
	u64 preve;
	int i;

	klmarr = MLX5_ADDR_OF(create_mkey_in, in, klm_pas_mtt);
	i = 0;
	list_for_each_entry(dmr, &mkey->head, list) {
again:
		klm = &klmarr[i++];
		if (first) {
			preve = dmr->start;
			first = false;
		}

		if (preve == dmr->start) {
			klm->key = cpu_to_be32(dmr->mr.key);
			klm->bcount = cpu_to_be32(klm_bcount(dmr->end - dmr->start));
			preve = dmr->end;
		} else {
			klm->key = cpu_to_be32(mvdev->res.null_mkey);
			klm->bcount = cpu_to_be32(klm_bcount(dmr->start - preve));
			preve = dmr->start;
			goto again;
		}
	}
}

static int klm_byte_size(int nklms)
{
	return 16 * ALIGN(nklms, 4);
}

static int create_indirect_key(struct mlx5_vdpa_dev *mvdev, struct mlx5_vdpa_mr *mr)
{
	int inlen;
	void *mkc;
	void *in;
	int err;
	u64 start;
	u64 len;

	start = indir_start_addr(mr);
	len = indir_len(mr);
	if (start == MLX5_VDPA_INVALID_START_ADDR || len == MLX5_VDPA_INVALID_LEN)
		return -EINVAL;

	inlen = MLX5_ST_SZ_BYTES(create_mkey_in) + klm_byte_size(mr->num_klms);
	in = kzalloc(inlen, GFP_KERNEL);
	if (!in)
		return -ENOMEM;

	MLX5_SET(create_mkey_in, in, uid, mvdev->res.uid);
	mkc = MLX5_ADDR_OF(create_mkey_in, in, memory_key_mkey_entry);
	MLX5_SET(mkc, mkc, lw, 1);
	MLX5_SET(mkc, mkc, lr, 1);
	mlx5_set_access_mode(mkc, MLX5_MKC_ACCESS_MODE_KLMS);
	MLX5_SET(mkc, mkc, qpn, 0xffffff);
	MLX5_SET(mkc, mkc, pd, mvdev->res.pdn);
	MLX5_SET64(mkc, mkc, start_addr, start);
	MLX5_SET64(mkc, mkc, len, len);
	MLX5_SET(mkc, mkc, translations_octword_size, klm_byte_size(mr->num_klms) / 16);
	MLX5_SET(create_mkey_in, in, translations_octword_actual_size, mr->num_klms);
	fill_indir(mvdev, mr, in);
	err = mlx5_vdpa_create_mkey(mvdev, &mr->mkey, in, inlen);
	kfree(in);
	return err;
}

static void destroy_indirect_key(struct mlx5_vdpa_dev *mvdev, struct mlx5_vdpa_mr *mkey)
{
	mlx5_vdpa_destroy_mkey(mvdev, &mkey->mkey);
}

static int map_direct_mr(struct mlx5_vdpa_dev *mvdev, struct mlx5_vdpa_direct_mr *mr,
			 struct vhost_iotlb *iotlb)
{
	struct vhost_iotlb_map *map;
	unsigned long lgcd = 0;
	int log_entity_size;
	unsigned long size;
	u64 start = 0;
	int err;
	struct page *pg;
	unsigned int nsg;
	int sglen;
	u64 pa;
	u64 paend;
	struct scatterlist *sg;
	struct device *dma = mvdev->mdev->device;

	for (map = vhost_iotlb_itree_first(iotlb, mr->start, mr->end - 1);
	     map; map = vhost_iotlb_itree_next(map, start, mr->end - 1)) {
		size = maplen(map, mr);
		lgcd = gcd(lgcd, size);
		start += size;
	}
	log_entity_size = ilog2(lgcd);

	sglen = 1 << log_entity_size;
	nsg = MLX5_DIV_ROUND_UP_POW2(mr->end - mr->start, log_entity_size);

	err = sg_alloc_table(&mr->sg_head, nsg, GFP_KERNEL);
	if (err)
		return err;

	sg = mr->sg_head.sgl;
	for (map = vhost_iotlb_itree_first(iotlb, mr->start, mr->end - 1);
	     map; map = vhost_iotlb_itree_next(map, mr->start, mr->end - 1)) {
		paend = map->addr + maplen(map, mr);
		for (pa = map->addr; pa < paend; pa += sglen) {
			pg = pfn_to_page(__phys_to_pfn(pa));
			if (!sg) {
				mlx5_vdpa_warn(mvdev, "sg null. start 0x%llx, end 0x%llx\n",
					       map->start, map->last + 1);
				err = -ENOMEM;
				goto err_map;
			}
			sg_set_page(sg, pg, sglen, 0);
			sg = sg_next(sg);
			if (!sg)
				goto done;
		}
	}
done:
	mr->log_size = log_entity_size;
	mr->nsg = nsg;
	mr->nent = dma_map_sg_attrs(dma, mr->sg_head.sgl, mr->nsg, DMA_BIDIRECTIONAL, 0);
	if (!mr->nent) {
		err = -ENOMEM;
		goto err_map;
	}

	err = create_direct_mr(mvdev, mr);
	if (err)
		goto err_direct;

	return 0;

err_direct:
	dma_unmap_sg_attrs(dma, mr->sg_head.sgl, mr->nsg, DMA_BIDIRECTIONAL, 0);
err_map:
	sg_free_table(&mr->sg_head);
	return err;
}

static void unmap_direct_mr(struct mlx5_vdpa_dev *mvdev, struct mlx5_vdpa_direct_mr *mr)
{
	struct device *dma = mvdev->mdev->device;

	destroy_direct_mr(mvdev, mr);
	dma_unmap_sg_attrs(dma, mr->sg_head.sgl, mr->nsg, DMA_BIDIRECTIONAL, 0);
	sg_free_table(&mr->sg_head);
}

static int add_direct_chain(struct mlx5_vdpa_dev *mvdev, u64 start, u64 size, u8 perm,
			    struct vhost_iotlb *iotlb)
{
	struct mlx5_vdpa_mr *mr = &mvdev->mr;
	struct mlx5_vdpa_direct_mr *dmr;
	struct mlx5_vdpa_direct_mr *n;
	LIST_HEAD(tmp);
	u64 st;
	u64 sz;
	int err;
	int i = 0;

	st = start;
	while (size) {
		sz = (u32)min_t(u64, MAX_KLM_SIZE, size);
		dmr = kzalloc(sizeof(*dmr), GFP_KERNEL);
		if (!dmr) {
			err = -ENOMEM;
			goto err_alloc;
		}

		dmr->start = st;
		dmr->end = st + sz;
		dmr->perm = perm;
		err = map_direct_mr(mvdev, dmr, iotlb);
		if (err) {
			kfree(dmr);
			goto err_alloc;
		}

		list_add_tail(&dmr->list, &tmp);
		size -= sz;
		mr->num_directs++;
		mr->num_klms++;
		st += sz;
		i++;
	}
	list_splice_tail(&tmp, &mr->head);
	return 0;

err_alloc:
	list_for_each_entry_safe(dmr, n, &mr->head, list) {
		list_del_init(&dmr->list);
		unmap_direct_mr(mvdev, dmr);
		kfree(dmr);
	}
	return err;
}

/* The iotlb pointer contains a list of maps. Go over the maps, possibly
 * merging mergeable maps, and create direct memory keys that provide the
 * device access to memory. The direct mkeys are then referred to by the
 * indirect memory key that provides access to the enitre address space given
 * by iotlb.
 */
static int _mlx5_vdpa_create_mr(struct mlx5_vdpa_dev *mvdev, struct vhost_iotlb *iotlb)
{
	struct mlx5_vdpa_mr *mr = &mvdev->mr;
	struct mlx5_vdpa_direct_mr *dmr;
	struct mlx5_vdpa_direct_mr *n;
	struct vhost_iotlb_map *map;
	u32 pperm = U16_MAX;
	u64 last = U64_MAX;
	u64 ps = U64_MAX;
	u64 pe = U64_MAX;
	u64 start = 0;
	int err = 0;
	int nnuls;

	if (mr->initialized)
		return 0;

	INIT_LIST_HEAD(&mr->head);
	for (map = vhost_iotlb_itree_first(iotlb, start, last); map;
	     map = vhost_iotlb_itree_next(map, start, last)) {
		start = map->start;
		if (pe == map->start && pperm == map->perm) {
			pe = map->last + 1;
		} else {
			if (ps != U64_MAX) {
				if (pe < map->start) {
					/* We have a hole in the map. Check how
					 * many null keys are required to fill it.
					 */
					nnuls = MLX5_DIV_ROUND_UP_POW2(map->start - pe,
								       LOG_MAX_KLM_SIZE);
					mr->num_klms += nnuls;
				}
				err = add_direct_chain(mvdev, ps, pe - ps, pperm, iotlb);
				if (err)
					goto err_chain;
			}
			ps = map->start;
			pe = map->last + 1;
			pperm = map->perm;
		}
	}
	err = add_direct_chain(mvdev, ps, pe - ps, pperm, iotlb);
	if (err)
		goto err_chain;

	/* Create the memory key that defines the guests's address space. This
	 * memory key refers to the direct keys that contain the MTT
	 * translations
	 */
	err = create_indirect_key(mvdev, mr);
	if (err)
		goto err_chain;

	mr->initialized = true;
	return 0;

err_chain:
	list_for_each_entry_safe_reverse(dmr, n, &mr->head, list) {
		list_del_init(&dmr->list);
		unmap_direct_mr(mvdev, dmr);
		kfree(dmr);
	}
	return err;
}

int mlx5_vdpa_create_mr(struct mlx5_vdpa_dev *mvdev, struct vhost_iotlb *iotlb)
{
	struct mlx5_vdpa_mr *mr = &mvdev->mr;
	int err;

	mutex_lock(&mr->mkey_mtx);
	err = _mlx5_vdpa_create_mr(mvdev, iotlb);
	mutex_unlock(&mr->mkey_mtx);
	return err;
}

void mlx5_vdpa_destroy_mr(struct mlx5_vdpa_dev *mvdev)
{
	struct mlx5_vdpa_mr *mr = &mvdev->mr;
	struct mlx5_vdpa_direct_mr *dmr;
	struct mlx5_vdpa_direct_mr *n;

	mutex_lock(&mr->mkey_mtx);
	if (!mr->initialized)
		goto out;

	destroy_indirect_key(mvdev, mr);
	list_for_each_entry_safe_reverse(dmr, n, &mr->head, list) {
		list_del_init(&dmr->list);
		unmap_direct_mr(mvdev, dmr);
		kfree(dmr);
	}
	memset(mr, 0, sizeof(*mr));
	mr->initialized = false;
out:
	mutex_unlock(&mr->mkey_mtx);
}

static bool map_empty(struct vhost_iotlb *iotlb)
{
	return !vhost_iotlb_itree_first(iotlb, 0, U64_MAX);
}

int mlx5_vdpa_handle_set_map(struct mlx5_vdpa_dev *mvdev, struct vhost_iotlb *iotlb,
			     bool *change_map)
{
	struct mlx5_vdpa_mr *mr = &mvdev->mr;
	int err = 0;

	*change_map = false;
	if (map_empty(iotlb)) {
		mlx5_vdpa_destroy_mr(mvdev);
		return 0;
	}
	mutex_lock(&mr->mkey_mtx);
	if (mr->initialized) {
		mlx5_vdpa_info(mvdev, "memory map update\n");
		*change_map = true;
	}
	if (!*change_map)
		err = _mlx5_vdpa_create_mr(mvdev, iotlb);
	mutex_unlock(&mr->mkey_mtx);

	return err;
}
