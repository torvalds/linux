// SPDX-License-Identifier: GPL-2.0
/*
 * linux/drivers/staging/erofs/inode.c
 *
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 */
#include "xattr.h"

/* no locking */
static int read_inode(struct inode *inode, void *data)
{
	struct erofs_vnode *vi = EROFS_V(inode);
	struct erofs_inode_v1 *v1 = data;
	const unsigned advise = le16_to_cpu(v1->i_advise);

	vi->data_mapping_mode = __inode_data_mapping(advise);

	if (unlikely(vi->data_mapping_mode >= EROFS_INODE_LAYOUT_MAX)) {
		errln("unknown data mapping mode %u of nid %llu",
			vi->data_mapping_mode, vi->nid);
		DBG_BUGON(1);
		return -EIO;
	}

	if (__inode_version(advise) == EROFS_INODE_LAYOUT_V2) {
		struct erofs_inode_v2 *v2 = data;

		vi->inode_isize = sizeof(struct erofs_inode_v2);
		vi->xattr_isize = ondisk_xattr_ibody_size(v2->i_xattr_icount);

		vi->raw_blkaddr = le32_to_cpu(v2->i_u.raw_blkaddr);
		inode->i_mode = le16_to_cpu(v2->i_mode);

		i_uid_write(inode, le32_to_cpu(v2->i_uid));
		i_gid_write(inode, le32_to_cpu(v2->i_gid));
		set_nlink(inode, le32_to_cpu(v2->i_nlink));

		/* ns timestamp */
		inode->i_mtime.tv_sec = inode->i_ctime.tv_sec =
			le64_to_cpu(v2->i_ctime);
		inode->i_mtime.tv_nsec = inode->i_ctime.tv_nsec =
			le32_to_cpu(v2->i_ctime_nsec);

		inode->i_size = le64_to_cpu(v2->i_size);
	} else if (__inode_version(advise) == EROFS_INODE_LAYOUT_V1) {
		struct erofs_sb_info *sbi = EROFS_SB(inode->i_sb);

		vi->inode_isize = sizeof(struct erofs_inode_v1);
		vi->xattr_isize = ondisk_xattr_ibody_size(v1->i_xattr_icount);

		vi->raw_blkaddr = le32_to_cpu(v1->i_u.raw_blkaddr);
		inode->i_mode = le16_to_cpu(v1->i_mode);

		i_uid_write(inode, le16_to_cpu(v1->i_uid));
		i_gid_write(inode, le16_to_cpu(v1->i_gid));
		set_nlink(inode, le16_to_cpu(v1->i_nlink));

		/* use build time to derive all file time */
		inode->i_mtime.tv_sec = inode->i_ctime.tv_sec =
			sbi->build_time;
		inode->i_mtime.tv_nsec = inode->i_ctime.tv_nsec =
			sbi->build_time_nsec;

		inode->i_size = le32_to_cpu(v1->i_size);
	} else {
		errln("unsupported on-disk inode version %u of nid %llu",
			__inode_version(advise), vi->nid);
		DBG_BUGON(1);
		return -EIO;
	}

	/* measure inode.i_blocks as the generic filesystem */
	inode->i_blocks = ((inode->i_size - 1) >> 9) + 1;
	return 0;
}

/*
 * try_lock can be required since locking order is:
 *   file data(fs_inode)
 *        meta(bd_inode)
 * but the majority of the callers is "iget",
 * in that case we are pretty sure no deadlock since
 * no data operations exist. However I tend to
 * try_lock since it takes no much overhead and
 * will success immediately.
 */
static int fill_inline_data(struct inode *inode, void *data, unsigned m_pofs)
{
	struct erofs_vnode *vi = EROFS_V(inode);
	int mode = vi->data_mapping_mode;

	DBG_BUGON(mode >= EROFS_INODE_LAYOUT_MAX);

	/* should be inode inline C */
	if (mode != EROFS_INODE_LAYOUT_INLINE)
		return 0;

	/* fast symlink (following ext4) */
	if (S_ISLNK(inode->i_mode) && inode->i_size < PAGE_SIZE) {
		char *lnk = kmalloc(inode->i_size + 1, GFP_KERNEL);

		if (unlikely(lnk == NULL))
			return -ENOMEM;

		m_pofs += vi->inode_isize + vi->xattr_isize;
		BUG_ON(m_pofs + inode->i_size > PAGE_SIZE);

		/* get in-page inline data */
		memcpy(lnk, data + m_pofs, inode->i_size);
		lnk[inode->i_size] = '\0';

		inode->i_link = lnk;
		set_inode_fast_symlink(inode);
	}
	return -EAGAIN;
}

static int fill_inode(struct inode *inode, int isdir)
{
	struct erofs_sb_info *sbi = EROFS_SB(inode->i_sb);
	struct erofs_vnode *vi = EROFS_V(inode);
	struct page *page;
	void *data;
	int err;
	erofs_blk_t blkaddr;
	unsigned ofs;

	blkaddr = erofs_blknr(iloc(sbi, vi->nid));
	ofs = erofs_blkoff(iloc(sbi, vi->nid));

	debugln("%s, reading inode nid %llu at %u of blkaddr %u",
		__func__, vi->nid, ofs, blkaddr);

	page = erofs_get_meta_page(inode->i_sb, blkaddr, isdir);

	if (IS_ERR(page)) {
		errln("failed to get inode (nid: %llu) page, err %ld",
			vi->nid, PTR_ERR(page));
		return PTR_ERR(page);
	}

	BUG_ON(!PageUptodate(page));
	data = page_address(page);

	err = read_inode(inode, data + ofs);
	if (!err) {
		/* setup the new inode */
		if (S_ISREG(inode->i_mode)) {
#ifdef CONFIG_EROFS_FS_XATTR
			if (vi->xattr_isize)
				inode->i_op = &erofs_generic_xattr_iops;
#endif
			inode->i_fop = &generic_ro_fops;
		} else if (S_ISDIR(inode->i_mode)) {
			inode->i_op =
#ifdef CONFIG_EROFS_FS_XATTR
				vi->xattr_isize ? &erofs_dir_xattr_iops :
#endif
				&erofs_dir_iops;
			inode->i_fop = &erofs_dir_fops;
		} else if (S_ISLNK(inode->i_mode)) {
			/* by default, page_get_link is used for symlink */
			inode->i_op =
#ifdef CONFIG_EROFS_FS_XATTR
				&erofs_symlink_xattr_iops,
#else
				&page_symlink_inode_operations;
#endif
			inode_nohighmem(inode);
		} else {
			err = -EIO;
			goto out_unlock;
		}

		if (is_inode_layout_compression(inode)) {
			err = -ENOTSUPP;
			goto out_unlock;
		}

		inode->i_mapping->a_ops = &erofs_raw_access_aops;

		/* fill last page if inline data is available */
		fill_inline_data(inode, data, ofs);
	}

out_unlock:
	unlock_page(page);
	put_page(page);
	return err;
}

struct inode *erofs_iget(struct super_block *sb,
	erofs_nid_t nid, bool isdir)
{
	struct inode *inode = iget_locked(sb, nid);

	if (unlikely(inode == NULL))
		return ERR_PTR(-ENOMEM);

	if (inode->i_state & I_NEW) {
		int err;
		struct erofs_vnode *vi = EROFS_V(inode);
		vi->nid = nid;

		err = fill_inode(inode, isdir);
		if (likely(!err))
			unlock_new_inode(inode);
		else {
			iget_failed(inode);
			inode = ERR_PTR(err);
		}
	}
	return inode;
}

#ifdef CONFIG_EROFS_FS_XATTR
const struct inode_operations erofs_generic_xattr_iops = {
	.listxattr = erofs_listxattr,
};
#endif

#ifdef CONFIG_EROFS_FS_XATTR
const struct inode_operations erofs_symlink_xattr_iops = {
	.get_link = page_get_link,
	.listxattr = erofs_listxattr,
};
#endif

#ifdef CONFIG_EROFS_FS_XATTR
const struct inode_operations erofs_fast_symlink_xattr_iops = {
	.get_link = simple_get_link,
	.listxattr = erofs_listxattr,
};
#endif

