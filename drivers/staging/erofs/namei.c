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

struct erofs_qstr {
	const unsigned char *name;
	const unsigned char *end;
};

/* based on the end of qn is accurate and it must have the trailing '\0' */
static inline int dirnamecmp(const struct erofs_qstr *qn,
			     const struct erofs_qstr *qd,
			     unsigned int *matched)
{
	unsigned int i = *matched;

	/*
	 * on-disk error, let's only BUG_ON in the debugging mode.
	 * otherwise, it will return 1 to just skip the invalid name
	 * and go on (in consideration of the lookup performance).
	 */
	DBG_BUGON(qd->name > qd->end);

	/* qd could not have trailing '\0' */
	/* However it is absolutely safe if < qd->end */
	while (qd->name + i < qd->end && qd->name[i] != '\0') {
		if (qn->name[i] != qd->name[i]) {
			*matched = i;
			return qn->name[i] > qd->name[i] ? 1 : -1;
		}
		++i;
	}
	*matched = i;
	/* See comments in __d_alloc on the terminating NUL character */
	return qn->name[i] == '\0' ? 0 : 1;
}

#define nameoff_from_disk(off, sz)	(le16_to_cpu(off) & ((sz) - 1))

static struct erofs_dirent *find_target_dirent(struct erofs_qstr *name,
					       u8 *data,
					       unsigned int dirblksize,
					       const int ndirents)
{
	int head, back;
	unsigned int startprfx, endprfx;
	struct erofs_dirent *const de = (struct erofs_dirent *)data;

	/* since the 1st dirent has been evaluated previously */
	head = 1;
	back = ndirents - 1;
	startprfx = endprfx = 0;

	while (head <= back) {
		const int mid = head + (back - head) / 2;
		const int nameoff = nameoff_from_disk(de[mid].nameoff,
						      dirblksize);
		unsigned int matched = min(startprfx, endprfx);
		struct erofs_qstr dname = {
			.name = data + nameoff,
			.end = unlikely(mid >= ndirents - 1) ?
				data + dirblksize :
				data + nameoff_from_disk(de[mid + 1].nameoff,
							 dirblksize)
		};

		/* string comparison without already matched prefix */
		int ret = dirnamecmp(name, &dname, &matched);

		if (unlikely(!ret)) {
			return de + mid;
		} else if (ret > 0) {
			head = mid + 1;
			startprfx = matched;
		} else {
			back = mid - 1;
			endprfx = matched;
		}
	}

	return ERR_PTR(-ENOENT);
}

static struct page *find_target_block_classic(struct inode *dir,
					      struct erofs_qstr *name,
					      int *_ndirents)
{
	unsigned int startprfx, endprfx;
	int head, back;
	struct address_space *const mapping = dir->i_mapping;
	struct page *candidate = ERR_PTR(-ENOENT);

	startprfx = endprfx = 0;
	head = 0;
	back = inode_datablocks(dir) - 1;

	while (head <= back) {
		const int mid = head + (back - head) / 2;
		struct page *page = read_mapping_page(mapping, mid, NULL);

		if (!IS_ERR(page)) {
			struct erofs_dirent *de = kmap_atomic(page);
			const int nameoff = nameoff_from_disk(de->nameoff,
							      EROFS_BLKSIZ);
			const int ndirents = nameoff / sizeof(*de);
			int diff;
			unsigned int matched;
			struct erofs_qstr dname;

			if (unlikely(!ndirents)) {
				DBG_BUGON(1);
				kunmap_atomic(de);
				put_page(page);
				page = ERR_PTR(-EIO);
				goto out;
			}

			matched = min(startprfx, endprfx);

			dname.name = (u8 *)de + nameoff;
			if (ndirents == 1)
				dname.end = (u8 *)de + EROFS_BLKSIZ;
			else
				dname.end = (u8 *)de +
					nameoff_from_disk(de[1].nameoff,
							  EROFS_BLKSIZ);

			/* string comparison without already matched prefix */
			diff = dirnamecmp(name, &dname, &matched);
			kunmap_atomic(de);

			if (unlikely(!diff)) {
				*_ndirents = 0;
				goto out;
			} else if (diff > 0) {
				head = mid + 1;
				startprfx = matched;

				if (!IS_ERR(candidate))
					put_page(candidate);
				candidate = page;
				*_ndirents = ndirents;
			} else {
				put_page(page);

				back = mid - 1;
				endprfx = matched;
			}
			continue;
		}
out:		/* free if the candidate is valid */
		if (!IS_ERR(candidate))
			put_page(candidate);
		return page;
	}
	return candidate;
}

int erofs_namei(struct inode *dir,
		struct qstr *name,
		erofs_nid_t *nid, unsigned int *d_type)
{
	int ndirents;
	struct page *page;
	void *data;
	struct erofs_dirent *de;
	struct erofs_qstr qn;

	if (unlikely(!dir->i_size))
		return -ENOENT;

	qn.name = name->name;
	qn.end = name->name + name->len;

	ndirents = 0;
	page = find_target_block_classic(dir, &qn, &ndirents);

	if (IS_ERR(page))
		return PTR_ERR(page);

	data = kmap_atomic(page);
	/* the target page has been mapped */
	if (ndirents)
		de = find_target_dirent(&qn, data, EROFS_BLKSIZ, ndirents);
	else
		de = (struct erofs_dirent *)data;

	if (!IS_ERR(de)) {
		*nid = le64_to_cpu(de->nid);
		*d_type = de->file_type;
	}

	kunmap_atomic(data);
	put_page(page);

	return PTR_ERR_OR_ZERO(de);
}

/* NOTE: i_mutex is already held by vfs */
static struct dentry *erofs_lookup(struct inode *dir,
				   struct dentry *dentry,
				   unsigned int flags)
{
	int err;
	erofs_nid_t nid;
	unsigned int d_type;
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
	} else if (unlikely(err)) {
		inode = ERR_PTR(err);
	} else {
		debugln("%s, %s (nid %llu) found, d_type %u", __func__,
			dentry->d_name.name, nid, d_type);
		inode = erofs_iget(dir->i_sb, nid, d_type == EROFS_FT_DIR);
	}
	return d_splice_alias(inode, dentry);
}

const struct inode_operations erofs_dir_iops = {
	.lookup = erofs_lookup,
	.getattr = erofs_getattr,
#ifdef CONFIG_EROFS_FS_XATTR
	.listxattr = erofs_listxattr,
#endif
	.get_acl = erofs_get_acl,
};

