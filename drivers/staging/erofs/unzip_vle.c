// SPDX-License-Identifier: GPL-2.0
/*
 * linux/drivers/staging/erofs/unzip_vle.c
 *
 * Copyright (C) 2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 */
#include "internal.h"

#define __vle_cluster_advise(x, bit, bits) \
	((le16_to_cpu(x) >> (bit)) & ((1 << (bits)) - 1))

#define __vle_cluster_type(advise) __vle_cluster_advise(advise, \
	Z_EROFS_VLE_DI_CLUSTER_TYPE_BIT, Z_EROFS_VLE_DI_CLUSTER_TYPE_BITS)

enum {
	Z_EROFS_VLE_CLUSTER_TYPE_PLAIN,
	Z_EROFS_VLE_CLUSTER_TYPE_HEAD,
	Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD,
	Z_EROFS_VLE_CLUSTER_TYPE_RESERVED,
	Z_EROFS_VLE_CLUSTER_TYPE_MAX
};

#define vle_cluster_type(di)	\
	__vle_cluster_type((di)->di_advise)

static inline unsigned
vle_compressed_index_clusterofs(unsigned clustersize,
	struct z_erofs_vle_decompressed_index *di)
{
	debugln("%s, vle=%pK, advise=%x (type %u), clusterofs=%x blkaddr=%x",
		__func__, di, di->di_advise, vle_cluster_type(di),
		di->di_clusterofs, di->di_u.blkaddr);

	switch (vle_cluster_type(di)) {
	case Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD:
		break;
	case Z_EROFS_VLE_CLUSTER_TYPE_PLAIN:
	case Z_EROFS_VLE_CLUSTER_TYPE_HEAD:
		return di->di_clusterofs;
	default:
		BUG_ON(1);
	}
	return clustersize;
}

static inline erofs_blk_t
vle_extent_blkaddr(struct inode *inode, pgoff_t index)
{
	struct erofs_sb_info *sbi = EROFS_I_SB(inode);
	struct erofs_vnode *vi = EROFS_V(inode);

	unsigned ofs = Z_EROFS_VLE_EXTENT_ALIGN(vi->inode_isize +
		vi->xattr_isize) + sizeof(struct erofs_extent_header) +
		index * sizeof(struct z_erofs_vle_decompressed_index);

	return erofs_blknr(iloc(sbi, vi->nid) + ofs);
}

static inline unsigned int
vle_extent_blkoff(struct inode *inode, pgoff_t index)
{
	struct erofs_sb_info *sbi = EROFS_I_SB(inode);
	struct erofs_vnode *vi = EROFS_V(inode);

	unsigned ofs = Z_EROFS_VLE_EXTENT_ALIGN(vi->inode_isize +
		vi->xattr_isize) + sizeof(struct erofs_extent_header) +
		index * sizeof(struct z_erofs_vle_decompressed_index);

	return erofs_blkoff(iloc(sbi, vi->nid) + ofs);
}

/*
 * Variable-sized Logical Extent (Fixed Physical Cluster) Compression Mode
 * ---
 * VLE compression mode attempts to compress a number of logical data into
 * a physical cluster with a fixed size.
 * VLE compression mode uses "struct z_erofs_vle_decompressed_index".
 */
static erofs_off_t vle_get_logical_extent_head(
	struct inode *inode,
	struct page **page_iter,
	void **kaddr_iter,
	unsigned lcn,	/* logical cluster number */
	erofs_blk_t *pcn,
	unsigned *flags)
{
	/* for extent meta */
	struct page *page = *page_iter;
	erofs_blk_t blkaddr = vle_extent_blkaddr(inode, lcn);
	struct z_erofs_vle_decompressed_index *di;
	unsigned long long ofs;
	const unsigned int clusterbits = EROFS_SB(inode->i_sb)->clusterbits;
	const unsigned int clustersize = 1 << clusterbits;

	if (page->index != blkaddr) {
		kunmap_atomic(*kaddr_iter);
		unlock_page(page);
		put_page(page);

		*page_iter = page = erofs_get_meta_page(inode->i_sb,
			blkaddr, false);
		*kaddr_iter = kmap_atomic(page);
	}

	di = *kaddr_iter + vle_extent_blkoff(inode, lcn);
	switch (vle_cluster_type(di)) {
	case Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD:
		BUG_ON(!di->di_u.delta[0]);
		BUG_ON(lcn < di->di_u.delta[0]);

		ofs = vle_get_logical_extent_head(inode,
			page_iter, kaddr_iter,
			lcn - di->di_u.delta[0], pcn, flags);
		break;
	case Z_EROFS_VLE_CLUSTER_TYPE_PLAIN:
		*flags ^= EROFS_MAP_ZIPPED;
	case Z_EROFS_VLE_CLUSTER_TYPE_HEAD:
		/* clustersize should be a power of two */
		ofs = ((unsigned long long)lcn << clusterbits) +
			(le16_to_cpu(di->di_clusterofs) & (clustersize - 1));
		*pcn = le32_to_cpu(di->di_u.blkaddr);
		break;
	default:
		BUG_ON(1);
	}
	return ofs;
}

int z_erofs_map_blocks_iter(struct inode *inode,
	struct erofs_map_blocks *map,
	struct page **mpage_ret, int flags)
{
	/* logicial extent (start, end) offset */
	unsigned long long ofs, end;
	struct z_erofs_vle_decompressed_index *di;
	erofs_blk_t e_blkaddr, pcn;
	unsigned lcn, logical_cluster_ofs;
	u32 ofs_rem;
	struct page *mpage = *mpage_ret;
	void *kaddr;
	bool initial;
	const unsigned int clusterbits = EROFS_SB(inode->i_sb)->clusterbits;
	const unsigned int clustersize = 1 << clusterbits;

	/* if both m_(l,p)len are 0, regularize l_lblk, l_lofs, etc... */
	initial = !map->m_llen;

	/* when trying to read beyond EOF, leave it unmapped */
	if (unlikely(map->m_la >= inode->i_size)) {
		BUG_ON(!initial);
		map->m_llen = map->m_la + 1 - inode->i_size;
		map->m_la = inode->i_size - 1;
		map->m_flags = 0;
		goto out;
	}

	debugln("%s, m_la %llu m_llen %llu --- start", __func__,
		map->m_la, map->m_llen);

	ofs = map->m_la + map->m_llen;

	/* clustersize should be power of two */
	lcn = ofs >> clusterbits;
	ofs_rem = ofs & (clustersize - 1);

	e_blkaddr = vle_extent_blkaddr(inode, lcn);

	if (mpage == NULL || mpage->index != e_blkaddr) {
		if (mpage != NULL)
			put_page(mpage);

		mpage = erofs_get_meta_page(inode->i_sb, e_blkaddr, false);
		*mpage_ret = mpage;
	} else {
		lock_page(mpage);
		DBG_BUGON(!PageUptodate(mpage));
	}

	kaddr = kmap_atomic(mpage);
	di = kaddr + vle_extent_blkoff(inode, lcn);

	debugln("%s, lcn %u e_blkaddr %u e_blkoff %u", __func__, lcn,
		e_blkaddr, vle_extent_blkoff(inode, lcn));

	logical_cluster_ofs = vle_compressed_index_clusterofs(clustersize, di);
	if (!initial) {
		/* [walking mode] 'map' has been already initialized */
		map->m_llen += logical_cluster_ofs;
		goto unmap_out;
	}

	/* by default, compressed */
	map->m_flags |= EROFS_MAP_ZIPPED;

	end = (u64)(lcn + 1) * clustersize;

	switch (vle_cluster_type(di)) {
	case Z_EROFS_VLE_CLUSTER_TYPE_PLAIN:
		if (ofs_rem >= logical_cluster_ofs)
			map->m_flags ^= EROFS_MAP_ZIPPED;
	case Z_EROFS_VLE_CLUSTER_TYPE_HEAD:
		if (ofs_rem == logical_cluster_ofs) {
			pcn = le32_to_cpu(di->di_u.blkaddr);
			goto exact_hitted;
		}

		if (ofs_rem > logical_cluster_ofs) {
			ofs = lcn * clustersize | logical_cluster_ofs;
			pcn = le32_to_cpu(di->di_u.blkaddr);
			break;
		}

		BUG_ON(!lcn);	/* logical cluster number >= 1 */
		end = (lcn-- * clustersize) | logical_cluster_ofs;
	case Z_EROFS_VLE_CLUSTER_TYPE_NONHEAD:
		/* get the correspoinding first chunk */
		ofs = vle_get_logical_extent_head(inode, mpage_ret,
			&kaddr, lcn, &pcn, &map->m_flags);
		mpage = *mpage_ret;
	}

	map->m_la = ofs;
exact_hitted:
	map->m_llen = end - ofs;
	map->m_plen = clustersize;
	map->m_pa = blknr_to_addr(pcn);
	map->m_flags |= EROFS_MAP_MAPPED;
unmap_out:
	kunmap_atomic(kaddr);
	unlock_page(mpage);
out:
	debugln("%s, m_la %llu m_pa %llu m_llen %llu m_plen %llu m_flags 0%o",
		__func__, map->m_la, map->m_pa,
		map->m_llen, map->m_plen, map->m_flags);
	return 0;
}

