// SPDX-License-Identifier: GPL-2.0
/*
 * linux/drivers/staging/erofs/namei.c
 *
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 */
#include "internal.h"
#include "xattr.h"

#include <trace/events/erofs.h>

/* based on the value of qn->len is accurate */
static inline int dirnamecmp(struct qstr *qn,
	struct qstr *qd, unsigned *matched)
{
	unsigned i = *matched, len = min(qn->len, qd->len);
loop:
	if (unlikely(i >= len)) {
		*matched = i;
		if (qn->len < qd->len) {
			/*
			 * actually (qn->len == qd->len)
			 * when qd->name[i] == '\0'
			 */
			return qd->name[i] == '\0' ? 0 : -1;
		}
		return (qn->len > qd->len);
	}

	if (qn->name[i] != qd->name[i]) {
		*matched = i;
		return qn->name[i] > qd->name[i] ? 1 : -1;
	}

	++i;
	goto loop;
}

static struct erofs_dirent *find_target_dirent(
	struct qstr *name,
	u8 *data, int maxsize)
{
	unsigned ndirents, head, back;
	unsigned startprfx, endprfx;
	struct erofs_dirent *const de = (struct erofs_dirent *)data;

	/* make sure that maxsize is valid */
	BUG_ON(maxsize < sizeof(struct erofs_dirent));

	ndirents = le16_to_cpu(de->nameoff) / sizeof(*de);

	/* corrupted dir (may be unnecessary...) */
	BUG_ON(!ndirents);

	head = 0;
	back = ndirents - 1;
	startprfx = endprfx = 0;

	while (head <= back) {
		unsigned mid = head + (back - head) / 2;
		unsigned nameoff = le16_to_cpu(de[mid].nameoff);
		unsigned matched = min(startprfx, endprfx);

		struct qstr dname = QSTR_INIT(data + nameoff,
			unlikely(mid >= ndirents - 1) ?
				maxsize - nameoff :
				le16_to_cpu(de[mid + 1].nameoff) - nameoff);

		/* string comparison without already matched prefix */
		int ret = dirnamecmp(name, &dname, &matched);

		if (unlikely(!ret))
			return de + mid;
		else if (ret > 0) {
			head = mid + 1;
			startprfx = matched;
		} else if (unlikely(mid < 1))	/* fix "mid" overflow */
			break;
		else {
			back = mid - 1;
			endprfx = matched;
		}
	}

	return ERR_PTR(-ENOENT);
}

static struct page *find_target_block_classic(
	struct inode *dir,
	struct qstr *name, int *_diff)
{
	unsigned startprfx, endprfx;
	unsigned head, back;
	struct address_space *const mapping = dir->i_mapping;
	struct page *candidate = ERR_PTR(-ENOENT);

	startprfx = endprfx = 0;
	head = 0;
	back = inode_datablocks(dir) - 1;

	while (head <= back) {
		unsigned mid = head + (back - head) / 2;
		struct page *page = read_mapping_page(mapping, mid, NULL);

		if (IS_ERR(page)) {
exact_out:
			if (!IS_ERR(candidate)) /* valid candidate */
				put_page(candidate);
			return page;
		} else {
			int diff;
			unsigned ndirents, matched;
			struct qstr dname;
			struct erofs_dirent *de = kmap_atomic(page);
			unsigned nameoff = le16_to_cpu(de->nameoff);

			ndirents = nameoff / sizeof(*de);

			/* corrupted dir (should have one entry at least) */
			BUG_ON(!ndirents || nameoff > PAGE_SIZE);

			matched = min(startprfx, endprfx);

			dname.name = (u8 *)de + nameoff;
			dname.len = ndirents == 1 ?
				/* since the rest of the last page is 0 */
				EROFS_BLKSIZ - nameoff
				: le16_to_cpu(de[1].nameoff) - nameoff;

			/* string comparison without already matched prefix */
			diff = dirnamecmp(name, &dname, &matched);
			kunmap_atomic(de);

			if (unlikely(!diff)) {
				*_diff = 0;
				goto exact_out;
			} else if (diff > 0) {
				head = mid + 1;
				startprfx = matched;

				if (likely(!IS_ERR(candidate)))
					put_page(candidate);
				candidate = page;
			} else {
				put_page(page);

				if (unlikely(mid < 1))	/* fix "mid" overflow */
					break;

				back = mid - 1;
				endprfx = matched;
			}
		}
	}
	*_diff = 1;
	return candidate;
}

int erofs_namei(struct inode *dir,
	struct qstr *name,
	erofs_nid_t *nid, unsigned *d_type)
{
	int diff;
	struct page *page;
	u8 *data;
	struct erofs_dirent *de;

	if (unlikely(!dir->i_size))
		return -ENOENT;

	diff = 1;
	page = find_target_block_classic(dir, name, &diff);

	if (unlikely(IS_ERR(page)))
		return PTR_ERR(page);

	data = kmap_atomic(page);
	/* the target page has been mapped */
	de = likely(diff) ?
		/* since the rest of the last page is 0 */
		find_target_dirent(name, data, EROFS_BLKSIZ) :
		(struct erofs_dirent *)data;

	if (likely(!IS_ERR(de))) {
		*nid = le64_to_cpu(de->nid);
		*d_type = de->file_type;
	}

	kunmap_atomic(data);
	put_page(page);

	return IS_ERR(de) ? PTR_ERR(de) : 0;
}

/* NOTE: i_mutex is already held by vfs */
static struct dentry *erofs_lookup(struct inode *dir,
	struct dentry *dentry, unsigned int flags)
{
	int err;
	erofs_nid_t nid;
	unsigned d_type;
	struct inode *inode;

	DBG_BUGON(!d_really_is_negative(dentry));
	/* dentry must be unhashed in lookup, no need to worry about */
	DBG_BUGON(!d_unhashed(dentry));

	trace_erofs_lookup(dir, dentry, flags);

	/* file name exceeds fs limit */
	if (unlikely(dentry->d_name.len > EROFS_NAME_LEN))
		return ERR_PTR(-ENAMETOOLONG);

	/* false uninitialized warnings on gcc 4.8.x */
	err = erofs_namei(dir, &dentry->d_name, &nid, &d_type);

	if (err == -ENOENT) {
		/* negative dentry */
		inode = NULL;
		goto negative_out;
	} else if (unlikely(err))
		return ERR_PTR(err);

	debugln("%s, %s (nid %llu) found, d_type %u", __func__,
		dentry->d_name.name, nid, d_type);

	inode = erofs_iget(dir->i_sb, nid, d_type == EROFS_FT_DIR);
	if (IS_ERR(inode))
		return ERR_CAST(inode);

negative_out:
	return d_splice_alias(inode, dentry);
}

const struct inode_operations erofs_dir_iops = {
	.lookup = erofs_lookup,
};

const struct inode_operations erofs_dir_xattr_iops = {
	.lookup = erofs_lookup,
#ifdef CONFIG_EROFS_FS_XATTR
	.listxattr = erofs_listxattr,
#endif
};

