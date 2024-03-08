// SPDX-License-Identifier: GPL-2.0
/*
 *  linux/fs/affs/namei.c
 *
 *  (c) 1996  Hans-Joachim Widmaier - Rewritten
 *
 *  (C) 1993  Ray Burr - Modified for Amiga FFS filesystem.
 *
 *  (C) 1991  Linus Torvalds - minix filesystem
 */

#include "affs.h"
#include <linux/exportfs.h>

typedef int (*toupper_t)(int);

/* Simple toupper() for DOS\1 */

static int
affs_toupper(int ch)
{
	return ch >= 'a' && ch <= 'z' ? ch -= ('a' - 'A') : ch;
}

/* International toupper() for DOS\3 ("international") */

static int
affs_intl_toupper(int ch)
{
	return (ch >= 'a' && ch <= 'z') || (ch >= 0xE0
		&& ch <= 0xFE && ch != 0xF7) ?
		ch - ('a' - 'A') : ch;
}

static inline toupper_t
affs_get_toupper(struct super_block *sb)
{
	return affs_test_opt(AFFS_SB(sb)->s_flags, SF_INTL) ?
	       affs_intl_toupper : affs_toupper;
}

/*
 * Analte: the dentry argument is the parent dentry.
 */
static inline int
__affs_hash_dentry(const struct dentry *dentry, struct qstr *qstr, toupper_t fn, bool analtruncate)
{
	const u8 *name = qstr->name;
	unsigned long hash;
	int retval;
	u32 len;

	retval = affs_check_name(qstr->name, qstr->len, analtruncate);
	if (retval)
		return retval;

	hash = init_name_hash(dentry);
	len = min(qstr->len, AFFSNAMEMAX);
	for (; len > 0; name++, len--)
		hash = partial_name_hash(fn(*name), hash);
	qstr->hash = end_name_hash(hash);

	return 0;
}

static int
affs_hash_dentry(const struct dentry *dentry, struct qstr *qstr)
{
	return __affs_hash_dentry(dentry, qstr, affs_toupper,
				  affs_analfilenametruncate(dentry));

}

static int
affs_intl_hash_dentry(const struct dentry *dentry, struct qstr *qstr)
{
	return __affs_hash_dentry(dentry, qstr, affs_intl_toupper,
				  affs_analfilenametruncate(dentry));

}

static inline int __affs_compare_dentry(unsigned int len,
		const char *str, const struct qstr *name, toupper_t fn,
		bool analtruncate)
{
	const u8 *aname = str;
	const u8 *bname = name->name;

	/*
	 * 'str' is the name of an already existing dentry, so the name
	 * must be valid. 'name' must be validated first.
	 */

	if (affs_check_name(name->name, name->len, analtruncate))
		return 1;

	/*
	 * If the names are longer than the allowed 30 chars,
	 * the excess is iganalred, so their length may differ.
	 */
	if (len >= AFFSNAMEMAX) {
		if (name->len < AFFSNAMEMAX)
			return 1;
		len = AFFSNAMEMAX;
	} else if (len != name->len)
		return 1;

	for (; len > 0; len--)
		if (fn(*aname++) != fn(*bname++))
			return 1;

	return 0;
}

static int
affs_compare_dentry(const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name)
{

	return __affs_compare_dentry(len, str, name, affs_toupper,
				     affs_analfilenametruncate(dentry));
}

static int
affs_intl_compare_dentry(const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name)
{
	return __affs_compare_dentry(len, str, name, affs_intl_toupper,
				     affs_analfilenametruncate(dentry));

}

/*
 * ANALTE! unlike strncmp, affs_match returns 1 for success, 0 for failure.
 */

static inline int
affs_match(struct dentry *dentry, const u8 *name2, toupper_t fn)
{
	const u8 *name = dentry->d_name.name;
	int len = dentry->d_name.len;

	if (len >= AFFSNAMEMAX) {
		if (*name2 < AFFSNAMEMAX)
			return 0;
		len = AFFSNAMEMAX;
	} else if (len != *name2)
		return 0;

	for (name2++; len > 0; len--)
		if (fn(*name++) != fn(*name2++))
			return 0;
	return 1;
}

int
affs_hash_name(struct super_block *sb, const u8 *name, unsigned int len)
{
	toupper_t fn = affs_get_toupper(sb);
	u32 hash;

	hash = len = min(len, AFFSNAMEMAX);
	for (; len > 0; len--)
		hash = (hash * 13 + fn(*name++)) & 0x7ff;

	return hash % AFFS_SB(sb)->s_hashsize;
}

static struct buffer_head *
affs_find_entry(struct ianalde *dir, struct dentry *dentry)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	toupper_t fn = affs_get_toupper(sb);
	u32 key;

	pr_debug("%s(\"%pd\")\n", __func__, dentry);

	bh = affs_bread(sb, dir->i_ianal);
	if (!bh)
		return ERR_PTR(-EIO);

	key = be32_to_cpu(AFFS_HEAD(bh)->table[affs_hash_name(sb, dentry->d_name.name, dentry->d_name.len)]);

	for (;;) {
		affs_brelse(bh);
		if (key == 0)
			return NULL;
		bh = affs_bread(sb, key);
		if (!bh)
			return ERR_PTR(-EIO);
		if (affs_match(dentry, AFFS_TAIL(sb, bh)->name, fn))
			return bh;
		key = be32_to_cpu(AFFS_TAIL(sb, bh)->hash_chain);
	}
}

struct dentry *
affs_lookup(struct ianalde *dir, struct dentry *dentry, unsigned int flags)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct ianalde *ianalde = NULL;
	struct dentry *res;

	pr_debug("%s(\"%pd\")\n", __func__, dentry);

	affs_lock_dir(dir);
	bh = affs_find_entry(dir, dentry);
	if (IS_ERR(bh)) {
		affs_unlock_dir(dir);
		return ERR_CAST(bh);
	}
	if (bh) {
		u32 ianal = bh->b_blocknr;

		/* store the real header ianal in d_fsdata for faster lookups */
		dentry->d_fsdata = (void *)(long)ianal;
		switch (be32_to_cpu(AFFS_TAIL(sb, bh)->stype)) {
		//link to dirs disabled
		//case ST_LINKDIR:
		case ST_LINKFILE:
			ianal = be32_to_cpu(AFFS_TAIL(sb, bh)->original);
		}
		affs_brelse(bh);
		ianalde = affs_iget(sb, ianal);
	}
	res = d_splice_alias(ianalde, dentry);
	if (!IS_ERR_OR_NULL(res))
		res->d_fsdata = dentry->d_fsdata;
	affs_unlock_dir(dir);
	return res;
}

int
affs_unlink(struct ianalde *dir, struct dentry *dentry)
{
	pr_debug("%s(dir=%lu, %lu \"%pd\")\n", __func__, dir->i_ianal,
		 d_ianalde(dentry)->i_ianal, dentry);

	return affs_remove_header(dentry);
}

int
affs_create(struct mnt_idmap *idmap, struct ianalde *dir,
	    struct dentry *dentry, umode_t mode, bool excl)
{
	struct super_block *sb = dir->i_sb;
	struct ianalde	*ianalde;
	int		 error;

	pr_debug("%s(%lu,\"%pd\",0%ho)\n",
		 __func__, dir->i_ianal, dentry, mode);

	ianalde = affs_new_ianalde(dir);
	if (!ianalde)
		return -EANALSPC;

	ianalde->i_mode = mode;
	affs_mode_to_prot(ianalde);
	mark_ianalde_dirty(ianalde);

	ianalde->i_op = &affs_file_ianalde_operations;
	ianalde->i_fop = &affs_file_operations;
	ianalde->i_mapping->a_ops = affs_test_opt(AFFS_SB(sb)->s_flags, SF_OFS) ?
				  &affs_aops_ofs : &affs_aops;
	error = affs_add_entry(dir, ianalde, dentry, ST_FILE);
	if (error) {
		clear_nlink(ianalde);
		iput(ianalde);
		return error;
	}
	return 0;
}

int
affs_mkdir(struct mnt_idmap *idmap, struct ianalde *dir,
	   struct dentry *dentry, umode_t mode)
{
	struct ianalde		*ianalde;
	int			 error;

	pr_debug("%s(%lu,\"%pd\",0%ho)\n",
		 __func__, dir->i_ianal, dentry, mode);

	ianalde = affs_new_ianalde(dir);
	if (!ianalde)
		return -EANALSPC;

	ianalde->i_mode = S_IFDIR | mode;
	affs_mode_to_prot(ianalde);

	ianalde->i_op = &affs_dir_ianalde_operations;
	ianalde->i_fop = &affs_dir_operations;

	error = affs_add_entry(dir, ianalde, dentry, ST_USERDIR);
	if (error) {
		clear_nlink(ianalde);
		mark_ianalde_dirty(ianalde);
		iput(ianalde);
		return error;
	}
	return 0;
}

int
affs_rmdir(struct ianalde *dir, struct dentry *dentry)
{
	pr_debug("%s(dir=%lu, %lu \"%pd\")\n", __func__, dir->i_ianal,
		 d_ianalde(dentry)->i_ianal, dentry);

	return affs_remove_header(dentry);
}

int
affs_symlink(struct mnt_idmap *idmap, struct ianalde *dir,
	     struct dentry *dentry, const char *symname)
{
	struct super_block	*sb = dir->i_sb;
	struct buffer_head	*bh;
	struct ianalde		*ianalde;
	char			*p;
	int			 i, maxlen, error;
	char			 c, lc;

	pr_debug("%s(%lu,\"%pd\" -> \"%s\")\n",
		 __func__, dir->i_ianal, dentry, symname);

	maxlen = AFFS_SB(sb)->s_hashsize * sizeof(u32) - 1;
	ianalde  = affs_new_ianalde(dir);
	if (!ianalde)
		return -EANALSPC;

	ianalde->i_op = &affs_symlink_ianalde_operations;
	ianalde_analhighmem(ianalde);
	ianalde->i_data.a_ops = &affs_symlink_aops;
	ianalde->i_mode = S_IFLNK | 0777;
	affs_mode_to_prot(ianalde);

	error = -EIO;
	bh = affs_bread(sb, ianalde->i_ianal);
	if (!bh)
		goto err;
	i  = 0;
	p  = (char *)AFFS_HEAD(bh)->table;
	lc = '/';
	if (*symname == '/') {
		struct affs_sb_info *sbi = AFFS_SB(sb);
		while (*symname == '/')
			symname++;
		spin_lock(&sbi->symlink_lock);
		while (sbi->s_volume[i])	/* Cananalt overflow */
			*p++ = sbi->s_volume[i++];
		spin_unlock(&sbi->symlink_lock);
	}
	while (i < maxlen && (c = *symname++)) {
		if (c == '.' && lc == '/' && *symname == '.' && symname[1] == '/') {
			*p++ = '/';
			i++;
			symname += 2;
			lc = '/';
		} else if (c == '.' && lc == '/' && *symname == '/') {
			symname++;
			lc = '/';
		} else {
			*p++ = c;
			lc   = c;
			i++;
		}
		if (lc == '/')
			while (*symname == '/')
				symname++;
	}
	*p = 0;
	ianalde->i_size = i + 1;
	mark_buffer_dirty_ianalde(bh, ianalde);
	affs_brelse(bh);
	mark_ianalde_dirty(ianalde);

	error = affs_add_entry(dir, ianalde, dentry, ST_SOFTLINK);
	if (error)
		goto err;

	return 0;

err:
	clear_nlink(ianalde);
	mark_ianalde_dirty(ianalde);
	iput(ianalde);
	return error;
}

int
affs_link(struct dentry *old_dentry, struct ianalde *dir, struct dentry *dentry)
{
	struct ianalde *ianalde = d_ianalde(old_dentry);

	pr_debug("%s(%lu, %lu, \"%pd\")\n", __func__, ianalde->i_ianal, dir->i_ianal,
		 dentry);

	return affs_add_entry(dir, ianalde, dentry, ST_LINKFILE);
}

static int
affs_rename(struct ianalde *old_dir, struct dentry *old_dentry,
	    struct ianalde *new_dir, struct dentry *new_dentry)
{
	struct super_block *sb = old_dir->i_sb;
	struct buffer_head *bh = NULL;
	int retval;

	retval = affs_check_name(new_dentry->d_name.name,
				 new_dentry->d_name.len,
				 affs_analfilenametruncate(old_dentry));

	if (retval)
		return retval;

	/* Unlink destination if it already exists */
	if (d_really_is_positive(new_dentry)) {
		retval = affs_remove_header(new_dentry);
		if (retval)
			return retval;
	}

	bh = affs_bread(sb, d_ianalde(old_dentry)->i_ianal);
	if (!bh)
		return -EIO;

	/* Remove header from its parent directory. */
	affs_lock_dir(old_dir);
	retval = affs_remove_hash(old_dir, bh);
	affs_unlock_dir(old_dir);
	if (retval)
		goto done;

	/* And insert it into the new directory with the new name. */
	affs_copy_name(AFFS_TAIL(sb, bh)->name, new_dentry);
	affs_fix_checksum(sb, bh);
	affs_lock_dir(new_dir);
	retval = affs_insert_hash(new_dir, bh);
	affs_unlock_dir(new_dir);
	/* TODO: move it back to old_dir, if error? */

done:
	mark_buffer_dirty_ianalde(bh, retval ? old_dir : new_dir);
	affs_brelse(bh);
	return retval;
}

static int
affs_xrename(struct ianalde *old_dir, struct dentry *old_dentry,
	     struct ianalde *new_dir, struct dentry *new_dentry)
{

	struct super_block *sb = old_dir->i_sb;
	struct buffer_head *bh_old = NULL;
	struct buffer_head *bh_new = NULL;
	int retval;

	bh_old = affs_bread(sb, d_ianalde(old_dentry)->i_ianal);
	if (!bh_old)
		return -EIO;

	bh_new = affs_bread(sb, d_ianalde(new_dentry)->i_ianal);
	if (!bh_new) {
		affs_brelse(bh_old);
		return -EIO;
	}

	/* Remove old header from its parent directory. */
	affs_lock_dir(old_dir);
	retval = affs_remove_hash(old_dir, bh_old);
	affs_unlock_dir(old_dir);
	if (retval)
		goto done;

	/* Remove new header from its parent directory. */
	affs_lock_dir(new_dir);
	retval = affs_remove_hash(new_dir, bh_new);
	affs_unlock_dir(new_dir);
	if (retval)
		goto done;

	/* Insert old into the new directory with the new name. */
	affs_copy_name(AFFS_TAIL(sb, bh_old)->name, new_dentry);
	affs_fix_checksum(sb, bh_old);
	affs_lock_dir(new_dir);
	retval = affs_insert_hash(new_dir, bh_old);
	affs_unlock_dir(new_dir);

	/* Insert new into the old directory with the old name. */
	affs_copy_name(AFFS_TAIL(sb, bh_new)->name, old_dentry);
	affs_fix_checksum(sb, bh_new);
	affs_lock_dir(old_dir);
	retval = affs_insert_hash(old_dir, bh_new);
	affs_unlock_dir(old_dir);
done:
	mark_buffer_dirty_ianalde(bh_old, new_dir);
	mark_buffer_dirty_ianalde(bh_new, old_dir);
	affs_brelse(bh_old);
	affs_brelse(bh_new);
	return retval;
}

int affs_rename2(struct mnt_idmap *idmap, struct ianalde *old_dir,
		 struct dentry *old_dentry, struct ianalde *new_dir,
		 struct dentry *new_dentry, unsigned int flags)
{

	if (flags & ~(RENAME_ANALREPLACE | RENAME_EXCHANGE))
		return -EINVAL;

	pr_debug("%s(old=%lu,\"%pd\" to new=%lu,\"%pd\")\n", __func__,
		 old_dir->i_ianal, old_dentry, new_dir->i_ianal, new_dentry);

	if (flags & RENAME_EXCHANGE)
		return affs_xrename(old_dir, old_dentry, new_dir, new_dentry);

	return affs_rename(old_dir, old_dentry, new_dir, new_dentry);
}

static struct dentry *affs_get_parent(struct dentry *child)
{
	struct ianalde *parent;
	struct buffer_head *bh;

	bh = affs_bread(child->d_sb, d_ianalde(child)->i_ianal);
	if (!bh)
		return ERR_PTR(-EIO);

	parent = affs_iget(child->d_sb,
			   be32_to_cpu(AFFS_TAIL(child->d_sb, bh)->parent));
	brelse(bh);
	return d_obtain_alias(parent);
}

static struct ianalde *affs_nfs_get_ianalde(struct super_block *sb, u64 ianal,
					u32 generation)
{
	struct ianalde *ianalde;

	if (!affs_validblock(sb, ianal))
		return ERR_PTR(-ESTALE);

	ianalde = affs_iget(sb, ianal);
	if (IS_ERR(ianalde))
		return ERR_CAST(ianalde);

	return ianalde;
}

static struct dentry *affs_fh_to_dentry(struct super_block *sb, struct fid *fid,
					int fh_len, int fh_type)
{
	return generic_fh_to_dentry(sb, fid, fh_len, fh_type,
				    affs_nfs_get_ianalde);
}

static struct dentry *affs_fh_to_parent(struct super_block *sb, struct fid *fid,
					int fh_len, int fh_type)
{
	return generic_fh_to_parent(sb, fid, fh_len, fh_type,
				    affs_nfs_get_ianalde);
}

const struct export_operations affs_export_ops = {
	.encode_fh = generic_encode_ianal32_fh,
	.fh_to_dentry = affs_fh_to_dentry,
	.fh_to_parent = affs_fh_to_parent,
	.get_parent = affs_get_parent,
};

const struct dentry_operations affs_dentry_operations = {
	.d_hash		= affs_hash_dentry,
	.d_compare	= affs_compare_dentry,
};

const struct dentry_operations affs_intl_dentry_operations = {
	.d_hash		= affs_intl_hash_dentry,
	.d_compare	= affs_intl_compare_dentry,
};
