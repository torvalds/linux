// SPDX-License-Identifier: GPL-2.0
/*
 * linux/drivers/staging/erofs/xattr.c
 *
 * Copyright (C) 2017-2018 HUAWEI, Inc.
 *             http://www.huawei.com/
 * Created by Gao Xiang <gaoxiang25@huawei.com>
 *
 * This file is subject to the terms and conditions of the GNU General Public
 * License.  See the file COPYING in the main directory of the Linux
 * distribution for more details.
 */
#include <linux/security.h>
#include "xattr.h"

struct xattr_iter {
	struct super_block *sb;
	struct page *page;
	void *kaddr;

	erofs_blk_t blkaddr;
	unsigned int ofs;
};

static inline void xattr_iter_end(struct xattr_iter *it, bool atomic)
{
	/* the only user of kunmap() is 'init_inode_xattrs' */
	if (unlikely(!atomic))
		kunmap(it->page);
	else
		kunmap_atomic(it->kaddr);

	unlock_page(it->page);
	put_page(it->page);
}

static inline void xattr_iter_end_final(struct xattr_iter *it)
{
	if (it->page == NULL)
		return;

	xattr_iter_end(it, true);
}

static int init_inode_xattrs(struct inode *inode)
{
	struct erofs_vnode *const vi = EROFS_V(inode);
	struct xattr_iter it;
	unsigned int i;
	struct erofs_xattr_ibody_header *ih;
	struct super_block *sb;
	struct erofs_sb_info *sbi;
	bool atomic_map;
	int ret = 0;

	/* the most case is that xattrs of this inode are initialized. */
	if (test_bit(EROFS_V_EA_INITED_BIT, &vi->flags))
		return 0;

	if (wait_on_bit_lock(&vi->flags, EROFS_V_BL_XATTR_BIT, TASK_KILLABLE))
		return -ERESTARTSYS;

	/* someone has initialized xattrs for us? */
	if (test_bit(EROFS_V_EA_INITED_BIT, &vi->flags))
		goto out_unlock;

	/*
	 * bypass all xattr operations if ->xattr_isize is not greater than
	 * sizeof(struct erofs_xattr_ibody_header), in detail:
	 * 1) it is not enough to contain erofs_xattr_ibody_header then
	 *    ->xattr_isize should be 0 (it means no xattr);
	 * 2) it is just to contain erofs_xattr_ibody_header, which is on-disk
	 *    undefined right now (maybe use later with some new sb feature).
	 */
	if (vi->xattr_isize == sizeof(struct erofs_xattr_ibody_header)) {
		errln("xattr_isize %d of nid %llu is not supported yet",
		      vi->xattr_isize, vi->nid);
		ret = -ENOTSUPP;
		goto out_unlock;
	} else if (vi->xattr_isize < sizeof(struct erofs_xattr_ibody_header)) {
		if (unlikely(vi->xattr_isize)) {
			DBG_BUGON(1);
			ret = -EIO;
			goto out_unlock;	/* xattr ondisk layout error */
		}
		ret = -ENOATTR;
		goto out_unlock;
	}

	sb = inode->i_sb;
	sbi = EROFS_SB(sb);
	it.blkaddr = erofs_blknr(iloc(sbi, vi->nid) + vi->inode_isize);
	it.ofs = erofs_blkoff(iloc(sbi, vi->nid) + vi->inode_isize);

	it.page = erofs_get_inline_page(inode, it.blkaddr);
	if (IS_ERR(it.page)) {
		ret = PTR_ERR(it.page);
		goto out_unlock;
	}

	/* read in shared xattr array (non-atomic, see kmalloc below) */
	it.kaddr = kmap(it.page);
	atomic_map = false;

	ih = (struct erofs_xattr_ibody_header *)(it.kaddr + it.ofs);

	vi->xattr_shared_count = ih->h_shared_count;
	vi->xattr_shared_xattrs = kmalloc_array(vi->xattr_shared_count,
						sizeof(uint), GFP_KERNEL);
	if (vi->xattr_shared_xattrs == NULL) {
		xattr_iter_end(&it, atomic_map);
		ret = -ENOMEM;
		goto out_unlock;
	}

	/* let's skip ibody header */
	it.ofs += sizeof(struct erofs_xattr_ibody_header);

	for (i = 0; i < vi->xattr_shared_count; ++i) {
		if (unlikely(it.ofs >= EROFS_BLKSIZ)) {
			/* cannot be unaligned */
			BUG_ON(it.ofs != EROFS_BLKSIZ);
			xattr_iter_end(&it, atomic_map);

			it.page = erofs_get_meta_page(sb,
				++it.blkaddr, S_ISDIR(inode->i_mode));
			if (IS_ERR(it.page)) {
				kfree(vi->xattr_shared_xattrs);
				vi->xattr_shared_xattrs = NULL;
				ret = PTR_ERR(it.page);
				goto out_unlock;
			}

			it.kaddr = kmap_atomic(it.page);
			atomic_map = true;
			it.ofs = 0;
		}
		vi->xattr_shared_xattrs[i] =
			le32_to_cpu(*(__le32 *)(it.kaddr + it.ofs));
		it.ofs += sizeof(__le32);
	}
	xattr_iter_end(&it, atomic_map);

	set_bit(EROFS_V_EA_INITED_BIT, &vi->flags);

out_unlock:
	clear_and_wake_up_bit(EROFS_V_BL_XATTR_BIT, &vi->flags);
	return ret;
}

/*
 * the general idea for these return values is
 * if    0 is returned, go on processing the current xattr;
 *       1 (> 0) is returned, skip this round to process the next xattr;
 *    -err (< 0) is returned, an error (maybe ENOXATTR) occurred
 *                            and need to be handled
 */
struct xattr_iter_handlers {
	int (*entry)(struct xattr_iter *_it, struct erofs_xattr_entry *entry);
	int (*name)(struct xattr_iter *_it, unsigned int processed, char *buf,
		    unsigned int len);
	int (*alloc_buffer)(struct xattr_iter *_it, unsigned int value_sz);
	void (*value)(struct xattr_iter *_it, unsigned int processed, char *buf,
		      unsigned int len);
};

static inline int xattr_iter_fixup(struct xattr_iter *it)
{
	if (it->ofs < EROFS_BLKSIZ)
		return 0;

	xattr_iter_end(it, true);

	it->blkaddr += erofs_blknr(it->ofs);

	it->page = erofs_get_meta_page(it->sb, it->blkaddr, false);
	if (IS_ERR(it->page)) {
		int err = PTR_ERR(it->page);

		it->page = NULL;
		return err;
	}

	it->kaddr = kmap_atomic(it->page);
	it->ofs = erofs_blkoff(it->ofs);
	return 0;
}

static int inline_xattr_iter_begin(struct xattr_iter *it,
	struct inode *inode)
{
	struct erofs_vnode *const vi = EROFS_V(inode);
	struct erofs_sb_info *const sbi = EROFS_SB(inode->i_sb);
	unsigned int xattr_header_sz, inline_xattr_ofs;

	xattr_header_sz = inlinexattr_header_size(inode);
	if (unlikely(xattr_header_sz >= vi->xattr_isize)) {
		BUG_ON(xattr_header_sz > vi->xattr_isize);
		return -ENOATTR;
	}

	inline_xattr_ofs = vi->inode_isize + xattr_header_sz;

	it->blkaddr = erofs_blknr(iloc(sbi, vi->nid) + inline_xattr_ofs);
	it->ofs = erofs_blkoff(iloc(sbi, vi->nid) + inline_xattr_ofs);

	it->page = erofs_get_inline_page(inode, it->blkaddr);
	if (IS_ERR(it->page))
		return PTR_ERR(it->page);

	it->kaddr = kmap_atomic(it->page);
	return vi->xattr_isize - xattr_header_sz;
}

/*
 * Regardless of success or failure, `xattr_foreach' will end up with
 * `ofs' pointing to the next xattr item rather than an arbitrary position.
 */
static int xattr_foreach(struct xattr_iter *it,
	const struct xattr_iter_handlers *op, unsigned int *tlimit)
{
	struct erofs_xattr_entry entry;
	unsigned int value_sz, processed, slice;
	int err;

	/* 0. fixup blkaddr, ofs, ipage */
	err = xattr_iter_fixup(it);
	if (err)
		return err;

	/*
	 * 1. read xattr entry to the memory,
	 *    since we do EROFS_XATTR_ALIGN
	 *    therefore entry should be in the page
	 */
	entry = *(struct erofs_xattr_entry *)(it->kaddr + it->ofs);
	if (tlimit != NULL) {
		unsigned int entry_sz = EROFS_XATTR_ENTRY_SIZE(&entry);

		BUG_ON(*tlimit < entry_sz);
		*tlimit -= entry_sz;
	}

	it->ofs += sizeof(struct erofs_xattr_entry);
	value_sz = le16_to_cpu(entry.e_value_size);

	/* handle entry */
	err = op->entry(it, &entry);
	if (err) {
		it->ofs += entry.e_name_len + value_sz;
		goto out;
	}

	/* 2. handle xattr name (ofs will finally be at the end of name) */
	processed = 0;

	while (processed < entry.e_name_len) {
		if (it->ofs >= EROFS_BLKSIZ) {
			BUG_ON(it->ofs > EROFS_BLKSIZ);

			err = xattr_iter_fixup(it);
			if (err)
				goto out;
			it->ofs = 0;
		}

		slice = min_t(unsigned int, PAGE_SIZE - it->ofs,
			      entry.e_name_len - processed);

		/* handle name */
		err = op->name(it, processed, it->kaddr + it->ofs, slice);
		if (err) {
			it->ofs += entry.e_name_len - processed + value_sz;
			goto out;
		}

		it->ofs += slice;
		processed += slice;
	}

	/* 3. handle xattr value */
	processed = 0;

	if (op->alloc_buffer != NULL) {
		err = op->alloc_buffer(it, value_sz);
		if (err) {
			it->ofs += value_sz;
			goto out;
		}
	}

	while (processed < value_sz) {
		if (it->ofs >= EROFS_BLKSIZ) {
			BUG_ON(it->ofs > EROFS_BLKSIZ);

			err = xattr_iter_fixup(it);
			if (err)
				goto out;
			it->ofs = 0;
		}

		slice = min_t(unsigned int, PAGE_SIZE - it->ofs,
			      value_sz - processed);
		op->value(it, processed, it->kaddr + it->ofs, slice);
		it->ofs += slice;
		processed += slice;
	}

out:
	/* xattrs should be 4-byte aligned (on-disk constraint) */
	it->ofs = EROFS_XATTR_ALIGN(it->ofs);
	return err < 0 ? err : 0;
}

struct getxattr_iter {
	struct xattr_iter it;

	char *buffer;
	int buffer_size, index;
	struct qstr name;
};

static int xattr_entrymatch(struct xattr_iter *_it,
	struct erofs_xattr_entry *entry)
{
	struct getxattr_iter *it = container_of(_it, struct getxattr_iter, it);

	return (it->index != entry->e_name_index ||
		it->name.len != entry->e_name_len) ? -ENOATTR : 0;
}

static int xattr_namematch(struct xattr_iter *_it,
	unsigned int processed, char *buf, unsigned int len)
{
	struct getxattr_iter *it = container_of(_it, struct getxattr_iter, it);

	return memcmp(buf, it->name.name + processed, len) ? -ENOATTR : 0;
}

static int xattr_checkbuffer(struct xattr_iter *_it,
	unsigned int value_sz)
{
	struct getxattr_iter *it = container_of(_it, struct getxattr_iter, it);
	int err = it->buffer_size < value_sz ? -ERANGE : 0;

	it->buffer_size = value_sz;
	return it->buffer == NULL ? 1 : err;
}

static void xattr_copyvalue(struct xattr_iter *_it,
	unsigned int processed, char *buf, unsigned int len)
{
	struct getxattr_iter *it = container_of(_it, struct getxattr_iter, it);

	memcpy(it->buffer + processed, buf, len);
}

static const struct xattr_iter_handlers find_xattr_handlers = {
	.entry = xattr_entrymatch,
	.name = xattr_namematch,
	.alloc_buffer = xattr_checkbuffer,
	.value = xattr_copyvalue
};

static int inline_getxattr(struct inode *inode, struct getxattr_iter *it)
{
	int ret;
	unsigned int remaining;

	ret = inline_xattr_iter_begin(&it->it, inode);
	if (ret < 0)
		return ret;

	remaining = ret;
	while (remaining) {
		ret = xattr_foreach(&it->it, &find_xattr_handlers, &remaining);
		if (ret != -ENOATTR)
			break;
	}
	xattr_iter_end_final(&it->it);

	return ret ? ret : it->buffer_size;
}

static int shared_getxattr(struct inode *inode, struct getxattr_iter *it)
{
	struct erofs_vnode *const vi = EROFS_V(inode);
	struct super_block *const sb = inode->i_sb;
	struct erofs_sb_info *const sbi = EROFS_SB(sb);
	unsigned int i;
	int ret = -ENOATTR;

	for (i = 0; i < vi->xattr_shared_count; ++i) {
		erofs_blk_t blkaddr =
			xattrblock_addr(sbi, vi->xattr_shared_xattrs[i]);

		it->it.ofs = xattrblock_offset(sbi, vi->xattr_shared_xattrs[i]);

		if (!i || blkaddr != it->it.blkaddr) {
			if (i)
				xattr_iter_end(&it->it, true);

			it->it.page = erofs_get_meta_page(sb, blkaddr, false);
			if (IS_ERR(it->it.page))
				return PTR_ERR(it->it.page);

			it->it.kaddr = kmap_atomic(it->it.page);
			it->it.blkaddr = blkaddr;
		}

		ret = xattr_foreach(&it->it, &find_xattr_handlers, NULL);
		if (ret != -ENOATTR)
			break;
	}
	if (vi->xattr_shared_count)
		xattr_iter_end_final(&it->it);

	return ret ? ret : it->buffer_size;
}

static bool erofs_xattr_user_list(struct dentry *dentry)
{
	return test_opt(EROFS_SB(dentry->d_sb), XATTR_USER);
}

static bool erofs_xattr_trusted_list(struct dentry *dentry)
{
	return capable(CAP_SYS_ADMIN);
}

int erofs_getxattr(struct inode *inode, int index,
	const char *name,
	void *buffer, size_t buffer_size)
{
	int ret;
	struct getxattr_iter it;

	if (unlikely(name == NULL))
		return -EINVAL;

	ret = init_inode_xattrs(inode);
	if (ret)
		return ret;

	it.index = index;

	it.name.len = strlen(name);
	if (it.name.len > EROFS_NAME_LEN)
		return -ERANGE;
	it.name.name = name;

	it.buffer = buffer;
	it.buffer_size = buffer_size;

	it.it.sb = inode->i_sb;
	ret = inline_getxattr(inode, &it);
	if (ret == -ENOATTR)
		ret = shared_getxattr(inode, &it);
	return ret;
}

static int erofs_xattr_generic_get(const struct xattr_handler *handler,
		struct dentry *unused, struct inode *inode,
		const char *name, void *buffer, size_t size)
{
	struct erofs_sb_info *const sbi = EROFS_I_SB(inode);

	switch (handler->flags) {
	case EROFS_XATTR_INDEX_USER:
		if (!test_opt(sbi, XATTR_USER))
			return -EOPNOTSUPP;
		break;
	case EROFS_XATTR_INDEX_TRUSTED:
		if (!capable(CAP_SYS_ADMIN))
			return -EPERM;
		break;
	case EROFS_XATTR_INDEX_SECURITY:
		break;
	default:
		return -EINVAL;
	}

	return erofs_getxattr(inode, handler->flags, name, buffer, size);
}

const struct xattr_handler erofs_xattr_user_handler = {
	.prefix	= XATTR_USER_PREFIX,
	.flags	= EROFS_XATTR_INDEX_USER,
	.list	= erofs_xattr_user_list,
	.get	= erofs_xattr_generic_get,
};

const struct xattr_handler erofs_xattr_trusted_handler = {
	.prefix	= XATTR_TRUSTED_PREFIX,
	.flags	= EROFS_XATTR_INDEX_TRUSTED,
	.list	= erofs_xattr_trusted_list,
	.get	= erofs_xattr_generic_get,
};

#ifdef CONFIG_EROFS_FS_SECURITY
const struct xattr_handler __maybe_unused erofs_xattr_security_handler = {
	.prefix	= XATTR_SECURITY_PREFIX,
	.flags	= EROFS_XATTR_INDEX_SECURITY,
	.get	= erofs_xattr_generic_get,
};
#endif

const struct xattr_handler *erofs_xattr_handlers[] = {
	&erofs_xattr_user_handler,
#ifdef CONFIG_EROFS_FS_POSIX_ACL
	&posix_acl_access_xattr_handler,
	&posix_acl_default_xattr_handler,
#endif
	&erofs_xattr_trusted_handler,
#ifdef CONFIG_EROFS_FS_SECURITY
	&erofs_xattr_security_handler,
#endif
	NULL,
};

struct listxattr_iter {
	struct xattr_iter it;

	struct dentry *dentry;
	char *buffer;
	int buffer_size, buffer_ofs;
};

static int xattr_entrylist(struct xattr_iter *_it,
	struct erofs_xattr_entry *entry)
{
	struct listxattr_iter *it =
		container_of(_it, struct listxattr_iter, it);
	unsigned int prefix_len;
	const char *prefix;

	const struct xattr_handler *h =
		erofs_xattr_handler(entry->e_name_index);

	if (h == NULL || (h->list != NULL && !h->list(it->dentry)))
		return 1;

	prefix = xattr_prefix(h);
	prefix_len = strlen(prefix);

	if (it->buffer == NULL) {
		it->buffer_ofs += prefix_len + entry->e_name_len + 1;
		return 1;
	}

	if (it->buffer_ofs + prefix_len
		+ entry->e_name_len + 1 > it->buffer_size)
		return -ERANGE;

	memcpy(it->buffer + it->buffer_ofs, prefix, prefix_len);
	it->buffer_ofs += prefix_len;
	return 0;
}

static int xattr_namelist(struct xattr_iter *_it,
	unsigned int processed, char *buf, unsigned int len)
{
	struct listxattr_iter *it =
		container_of(_it, struct listxattr_iter, it);

	memcpy(it->buffer + it->buffer_ofs, buf, len);
	it->buffer_ofs += len;
	return 0;
}

static int xattr_skipvalue(struct xattr_iter *_it,
	unsigned int value_sz)
{
	struct listxattr_iter *it =
		container_of(_it, struct listxattr_iter, it);

	it->buffer[it->buffer_ofs++] = '\0';
	return 1;
}

static const struct xattr_iter_handlers list_xattr_handlers = {
	.entry = xattr_entrylist,
	.name = xattr_namelist,
	.alloc_buffer = xattr_skipvalue,
	.value = NULL
};

static int inline_listxattr(struct listxattr_iter *it)
{
	int ret;
	unsigned int remaining;

	ret = inline_xattr_iter_begin(&it->it, d_inode(it->dentry));
	if (ret < 0)
		return ret;

	remaining = ret;
	while (remaining) {
		ret = xattr_foreach(&it->it, &list_xattr_handlers, &remaining);
		if (ret)
			break;
	}
	xattr_iter_end_final(&it->it);
	return ret ? ret : it->buffer_ofs;
}

static int shared_listxattr(struct listxattr_iter *it)
{
	struct inode *const inode = d_inode(it->dentry);
	struct erofs_vnode *const vi = EROFS_V(inode);
	struct super_block *const sb = inode->i_sb;
	struct erofs_sb_info *const sbi = EROFS_SB(sb);
	unsigned int i;
	int ret = 0;

	for (i = 0; i < vi->xattr_shared_count; ++i) {
		erofs_blk_t blkaddr =
			xattrblock_addr(sbi, vi->xattr_shared_xattrs[i]);

		it->it.ofs = xattrblock_offset(sbi, vi->xattr_shared_xattrs[i]);
		if (!i || blkaddr != it->it.blkaddr) {
			if (i)
				xattr_iter_end(&it->it, true);

			it->it.page = erofs_get_meta_page(sb, blkaddr, false);
			if (IS_ERR(it->it.page))
				return PTR_ERR(it->it.page);

			it->it.kaddr = kmap_atomic(it->it.page);
			it->it.blkaddr = blkaddr;
		}

		ret = xattr_foreach(&it->it, &list_xattr_handlers, NULL);
		if (ret)
			break;
	}
	if (vi->xattr_shared_count)
		xattr_iter_end_final(&it->it);

	return ret ? ret : it->buffer_ofs;
}

ssize_t erofs_listxattr(struct dentry *dentry,
	char *buffer, size_t buffer_size)
{
	int ret;
	struct listxattr_iter it;

	ret = init_inode_xattrs(d_inode(dentry));
	if (ret)
		return ret;

	it.dentry = dentry;
	it.buffer = buffer;
	it.buffer_size = buffer_size;
	it.buffer_ofs = 0;

	it.it.sb = dentry->d_sb;

	ret = inline_listxattr(&it);
	if (ret < 0 && ret != -ENOATTR)
		return ret;
	return shared_listxattr(&it);
}

#ifdef CONFIG_EROFS_FS_POSIX_ACL
struct posix_acl *erofs_get_acl(struct inode *inode, int type)
{
	struct posix_acl *acl;
	int prefix, rc;
	char *value = NULL;

	switch (type) {
	case ACL_TYPE_ACCESS:
		prefix = EROFS_XATTR_INDEX_POSIX_ACL_ACCESS;
		break;
	case ACL_TYPE_DEFAULT:
		prefix = EROFS_XATTR_INDEX_POSIX_ACL_DEFAULT;
		break;
	default:
		return ERR_PTR(-EINVAL);
	}

	rc = erofs_getxattr(inode, prefix, "", NULL, 0);
	if (rc > 0) {
		value = kmalloc(rc, GFP_KERNEL);
		if (!value)
			return ERR_PTR(-ENOMEM);
		rc = erofs_getxattr(inode, prefix, "", value, rc);
	}

	if (rc == -ENOATTR)
		acl = NULL;
	else if (rc < 0)
		acl = ERR_PTR(rc);
	else
		acl = posix_acl_from_xattr(&init_user_ns, value, rc);
	kfree(value);
	return acl;
}
#endif

