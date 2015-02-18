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

typedef int (*toupper_t)(int);

static int	 affs_toupper(int ch);
static int	 affs_hash_dentry(const struct dentry *, struct qstr *);
static int       affs_compare_dentry(const struct dentry *parent, const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name);
static int	 affs_intl_toupper(int ch);
static int	 affs_intl_hash_dentry(const struct dentry *, struct qstr *);
static int       affs_intl_compare_dentry(const struct dentry *parent, const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name);

const struct dentry_operations affs_dentry_operations = {
	.d_hash		= affs_hash_dentry,
	.d_compare	= affs_compare_dentry,
};

const struct dentry_operations affs_intl_dentry_operations = {
	.d_hash		= affs_intl_hash_dentry,
	.d_compare	= affs_intl_compare_dentry,
};


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
	return AFFS_SB(sb)->s_flags & SF_INTL ? affs_intl_toupper : affs_toupper;
}

/*
 * Note: the dentry argument is the parent dentry.
 */
static inline int
__affs_hash_dentry(struct qstr *qstr, toupper_t toupper, bool notruncate)
{
	const u8 *name = qstr->name;
	unsigned long hash;
	int retval;
	u32 len;

	retval = affs_check_name(qstr->name, qstr->len, notruncate);
	if (retval)
		return retval;

	hash = init_name_hash();
	len = min(qstr->len, AFFSNAMEMAX);
	for (; len > 0; name++, len--)
		hash = partial_name_hash(toupper(*name), hash);
	qstr->hash = end_name_hash(hash);

	return 0;
}

static int
affs_hash_dentry(const struct dentry *dentry, struct qstr *qstr)
{
	return __affs_hash_dentry(qstr, affs_toupper,
				  affs_nofilenametruncate(dentry));

}

static int
affs_intl_hash_dentry(const struct dentry *dentry, struct qstr *qstr)
{
	return __affs_hash_dentry(qstr, affs_intl_toupper,
				  affs_nofilenametruncate(dentry));

}

static inline int __affs_compare_dentry(unsigned int len,
		const char *str, const struct qstr *name, toupper_t toupper,
		bool notruncate)
{
	const u8 *aname = str;
	const u8 *bname = name->name;

	/*
	 * 'str' is the name of an already existing dentry, so the name
	 * must be valid. 'name' must be validated first.
	 */

	if (affs_check_name(name->name, name->len, notruncate))
		return 1;

	/*
	 * If the names are longer than the allowed 30 chars,
	 * the excess is ignored, so their length may differ.
	 */
	if (len >= AFFSNAMEMAX) {
		if (name->len < AFFSNAMEMAX)
			return 1;
		len = AFFSNAMEMAX;
	} else if (len != name->len)
		return 1;

	for (; len > 0; len--)
		if (toupper(*aname++) != toupper(*bname++))
			return 1;

	return 0;
}

static int
affs_compare_dentry(const struct dentry *parent, const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name)
{

	return __affs_compare_dentry(len, str, name, affs_toupper,
				     affs_nofilenametruncate(parent));
}

static int
affs_intl_compare_dentry(const struct dentry *parent, const struct dentry *dentry,
		unsigned int len, const char *str, const struct qstr *name)
{
	return __affs_compare_dentry(len, str, name, affs_intl_toupper,
				     affs_nofilenametruncate(parent));

}

/*
 * NOTE! unlike strncmp, affs_match returns 1 for success, 0 for failure.
 */

static inline int
affs_match(struct dentry *dentry, const u8 *name2, toupper_t toupper)
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
		if (toupper(*name++) != toupper(*name2++))
			return 0;
	return 1;
}

int
affs_hash_name(struct super_block *sb, const u8 *name, unsigned int len)
{
	toupper_t toupper = affs_get_toupper(sb);
	u32 hash;

	hash = len = min(len, AFFSNAMEMAX);
	for (; len > 0; len--)
		hash = (hash * 13 + toupper(*name++)) & 0x7ff;

	return hash % AFFS_SB(sb)->s_hashsize;
}

static struct buffer_head *
affs_find_entry(struct inode *dir, struct dentry *dentry)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	toupper_t toupper = affs_get_toupper(sb);
	u32 key;

	pr_debug("%s(\"%pd\")\n", __func__, dentry);

	bh = affs_bread(sb, dir->i_ino);
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
		if (affs_match(dentry, AFFS_TAIL(sb, bh)->name, toupper))
			return bh;
		key = be32_to_cpu(AFFS_TAIL(sb, bh)->hash_chain);
	}
}

struct dentry *
affs_lookup(struct inode *dir, struct dentry *dentry, unsigned int flags)
{
	struct super_block *sb = dir->i_sb;
	struct buffer_head *bh;
	struct inode *inode = NULL;

	pr_debug("%s(\"%pd\")\n", __func__, dentry);

	affs_lock_dir(dir);
	bh = affs_find_entry(dir, dentry);
	affs_unlock_dir(dir);
	if (IS_ERR(bh))
		return ERR_CAST(bh);
	if (bh) {
		u32 ino = bh->b_blocknr;

		/* store the real header ino in d_fsdata for faster lookups */
		dentry->d_fsdata = (void *)(long)ino;
		switch (be32_to_cpu(AFFS_TAIL(sb, bh)->stype)) {
		//link to dirs disabled
		//case ST_LINKDIR:
		case ST_LINKFILE:
			ino = be32_to_cpu(AFFS_TAIL(sb, bh)->original);
		}
		affs_brelse(bh);
		inode = affs_iget(sb, ino);
		if (IS_ERR(inode))
			return ERR_CAST(inode);
	}
	d_add(dentry, inode);
	return NULL;
}

int
affs_unlink(struct inode *dir, struct dentry *dentry)
{
	pr_debug("%s(dir=%lu, %lu \"%pd\")\n", __func__, dir->i_ino,
		 dentry->d_inode->i_ino, dentry);

	return affs_remove_header(dentry);
}

int
affs_create(struct inode *dir, struct dentry *dentry, umode_t mode, bool excl)
{
	struct super_block *sb = dir->i_sb;
	struct inode	*inode;
	int		 error;

	pr_debug("%s(%lu,\"%pd\",0%ho)\n",
		 __func__, dir->i_ino, dentry, mode);

	inode = affs_new_inode(dir);
	if (!inode)
		return -ENOSPC;

	inode->i_mode = mode;
	mode_to_prot(inode);
	mark_inode_dirty(inode);

	inode->i_op = &affs_file_inode_operations;
	inode->i_fop = &affs_file_operations;
	inode->i_mapping->a_ops = (AFFS_SB(sb)->s_flags & SF_OFS) ? &affs_aops_ofs : &affs_aops;
	error = affs_add_entry(dir, inode, dentry, ST_FILE);
	if (error) {
		clear_nlink(inode);
		iput(inode);
		return error;
	}
	return 0;
}

int
affs_mkdir(struct inode *dir, struct dentry *dentry, umode_t mode)
{
	struct inode		*inode;
	int			 error;

	pr_debug("%s(%lu,\"%pd\",0%ho)\n",
		 __func__, dir->i_ino, dentry, mode);

	inode = affs_new_inode(dir);
	if (!inode)
		return -ENOSPC;

	inode->i_mode = S_IFDIR | mode;
	mode_to_prot(inode);

	inode->i_op = &affs_dir_inode_operations;
	inode->i_fop = &affs_dir_operations;

	error = affs_add_entry(dir, inode, dentry, ST_USERDIR);
	if (error) {
		clear_nlink(inode);
		mark_inode_dirty(inode);
		iput(inode);
		return error;
	}
	return 0;
}

int
affs_rmdir(struct inode *dir, struct dentry *dentry)
{
	pr_debug("%s(dir=%lu, %lu \"%pd\")\n", __func__, dir->i_ino,
		 dentry->d_inode->i_ino, dentry);

	return affs_remove_header(dentry);
}

int
affs_symlink(struct inode *dir, struct dentry *dentry, const char *symname)
{
	struct super_block	*sb = dir->i_sb;
	struct buffer_head	*bh;
	struct inode		*inode;
	char			*p;
	int			 i, maxlen, error;
	char			 c, lc;

	pr_debug("%s(%lu,\"%pd\" -> \"%s\")\n",
		 __func__, dir->i_ino, dentry, symname);

	maxlen = AFFS_SB(sb)->s_hashsize * sizeof(u32) - 1;
	inode  = affs_new_inode(dir);
	if (!inode)
		return -ENOSPC;

	inode->i_op = &affs_symlink_inode_operations;
	inode->i_data.a_ops = &affs_symlink_aops;
	inode->i_mode = S_IFLNK | 0777;
	mode_to_prot(inode);

	error = -EIO;
	bh = affs_bread(sb, inode->i_ino);
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
		while (sbi->s_volume[i])	/* Cannot overflow */
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
	mark_buffer_dirty_inode(bh, inode);
	affs_brelse(bh);
	mark_inode_dirty(inode);

	error = affs_add_entry(dir, inode, dentry, ST_SOFTLINK);
	if (error)
		goto err;

	return 0;

err:
	clear_nlink(inode);
	mark_inode_dirty(inode);
	iput(inode);
	return error;
}

int
affs_link(struct dentry *old_dentry, struct inode *dir, struct dentry *dentry)
{
	struct inode *inode = old_dentry->d_inode;

	pr_debug("%s(%lu, %lu, \"%pd\")\n", __func__, inode->i_ino, dir->i_ino,
		 dentry);

	return affs_add_entry(dir, inode, dentry, ST_LINKFILE);
}

int
affs_rename(struct inode *old_dir, struct dentry *old_dentry,
	    struct inode *new_dir, struct dentry *new_dentry)
{
	struct super_block *sb = old_dir->i_sb;
	struct buffer_head *bh = NULL;
	int retval;

	pr_debug("%s(old=%lu,\"%pd\" to new=%lu,\"%pd\")\n", __func__,
		 old_dir->i_ino, old_dentry, new_dir->i_ino, new_dentry);

	retval = affs_check_name(new_dentry->d_name.name,
				 new_dentry->d_name.len,
				 affs_nofilenametruncate(old_dentry));

	if (retval)
		return retval;

	/* Unlink destination if it already exists */
	if (new_dentry->d_inode) {
		retval = affs_remove_header(new_dentry);
		if (retval)
			return retval;
	}

	bh = affs_bread(sb, old_dentry->d_inode->i_ino);
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
	mark_buffer_dirty_inode(bh, retval ? old_dir : new_dir);
	affs_brelse(bh);
	return retval;
}
