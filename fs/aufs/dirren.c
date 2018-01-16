/*
 * Copyright (C) 2017 Junjiro R. Okajima
 *
 * This program, aufs is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

/*
 * special handling in renaming a directoy
 * in order to support looking-up the before-renamed name on the lower readonly
 * branches
 */

#include <linux/byteorder/generic.h>
#include "aufs.h"

static void au_dr_hino_del(struct au_dr_br *dr, struct au_dr_hino *ent)
{
	int idx;

	idx = au_dr_ihash(ent->dr_h_ino);
	au_hbl_del(&ent->dr_hnode, dr->dr_h_ino + idx);
}

static int au_dr_hino_test_empty(struct au_dr_br *dr)
{
	int ret, i;
	struct hlist_bl_head *hbl;

	ret = 1;
	for (i = 0; ret && i < AuDirren_NHASH; i++) {
		hbl = dr->dr_h_ino + i;
		hlist_bl_lock(hbl);
		ret &= hlist_bl_empty(hbl);
		hlist_bl_unlock(hbl);
	}

	return ret;
}

static struct au_dr_hino *au_dr_hino_find(struct au_dr_br *dr, ino_t ino)
{
	struct au_dr_hino *found, *ent;
	struct hlist_bl_head *hbl;
	struct hlist_bl_node *pos;
	int idx;

	found = NULL;
	idx = au_dr_ihash(ino);
	hbl = dr->dr_h_ino + idx;
	hlist_bl_lock(hbl);
	hlist_bl_for_each_entry(ent, pos, hbl, dr_hnode)
		if (ent->dr_h_ino == ino) {
			found = ent;
			break;
		}
	hlist_bl_unlock(hbl);

	return found;
}

int au_dr_hino_test_add(struct au_dr_br *dr, ino_t ino,
			struct au_dr_hino *add_ent)
{
	int found, idx;
	struct hlist_bl_head *hbl;
	struct hlist_bl_node *pos;
	struct au_dr_hino *ent;

	found = 0;
	idx = au_dr_ihash(ino);
	hbl = dr->dr_h_ino + idx;
#if 0
	{
		struct hlist_bl_node *tmp;

		hlist_bl_for_each_entry_safe(ent, pos, tmp, hbl, dr_hnode)
			AuDbg("hi%llu\n", (unsigned long long)ent->dr_h_ino);
	}
#endif
	hlist_bl_lock(hbl);
	hlist_bl_for_each_entry(ent, pos, hbl, dr_hnode)
		if (ent->dr_h_ino == ino) {
			found = 1;
			break;
		}
	if (!found && add_ent)
		hlist_bl_add_head(&add_ent->dr_hnode, hbl);
	hlist_bl_unlock(hbl);

	if (!found && add_ent)
		AuDbg("i%llu added\n", (unsigned long long)add_ent->dr_h_ino);

	return found;
}

void au_dr_hino_free(struct au_dr_br *dr)
{
	int i;
	struct hlist_bl_head *hbl;
	struct hlist_bl_node *pos, *tmp;
	struct au_dr_hino *ent;

	/* SiMustWriteLock(sb); */

	for (i = 0; i < AuDirren_NHASH; i++) {
		hbl = dr->dr_h_ino + i;
		/* no spinlock since sbinfo must be write-locked */
		hlist_bl_for_each_entry_safe(ent, pos, tmp, hbl, dr_hnode)
			kfree(ent);
		INIT_HLIST_BL_HEAD(hbl);
	}
}

/* returns the number of inodes or an error */
static int au_dr_hino_store(struct super_block *sb, struct au_branch *br,
			    struct file *hinofile)
{
	int err, i;
	ssize_t ssz;
	loff_t pos, oldsize;
	__be64 u64;
	struct inode *hinoinode;
	struct hlist_bl_head *hbl;
	struct hlist_bl_node *n1, *n2;
	struct au_dr_hino *ent;

	SiMustWriteLock(sb);
	AuDebugOn(!au_br_writable(br->br_perm));

	hinoinode = file_inode(hinofile);
	oldsize = i_size_read(hinoinode);

	err = 0;
	pos = 0;
	hbl = br->br_dirren.dr_h_ino;
	for (i = 0; !err && i < AuDirren_NHASH; i++, hbl++) {
		/* no bit-lock since sbinfo must be write-locked */
		hlist_bl_for_each_entry_safe(ent, n1, n2, hbl, dr_hnode) {
			AuDbg("hi%llu, %pD2\n",
			      (unsigned long long)ent->dr_h_ino, hinofile);
			u64 = cpu_to_be64(ent->dr_h_ino);
			ssz = vfsub_write_k(hinofile, &u64, sizeof(u64), &pos);
			if (ssz == sizeof(u64))
				continue;

			/* write error */
			pr_err("ssz %zd, %pD2\n", ssz, hinofile);
			err = -ENOSPC;
			if (ssz < 0)
				err = ssz;
			break;
		}
	}
	/* regardless the error */
	if (pos < oldsize) {
		err = vfsub_trunc(&hinofile->f_path, pos, /*attr*/0, hinofile);
		AuTraceErr(err);
	}

	AuTraceErr(err);
	return err;
}

static int au_dr_hino_load(struct au_dr_br *dr, struct file *hinofile)
{
	int err, hidx;
	ssize_t ssz;
	size_t sz, n;
	loff_t pos;
	uint64_t u64;
	struct au_dr_hino *ent;
	struct inode *hinoinode;
	struct hlist_bl_head *hbl;

	err = 0;
	pos = 0;
	hbl = dr->dr_h_ino;
	hinoinode = file_inode(hinofile);
	sz = i_size_read(hinoinode);
	AuDebugOn(sz % sizeof(u64));
	n = sz / sizeof(u64);
	while (n--) {
		ssz = vfsub_read_k(hinofile, &u64, sizeof(u64), &pos);
		if (unlikely(ssz != sizeof(u64))) {
			pr_err("ssz %zd, %pD2\n", ssz, hinofile);
			err = -EINVAL;
			if (ssz < 0)
				err = ssz;
			goto out_free;
		}

		ent = kmalloc(sizeof(*ent), GFP_NOFS);
		if (!ent) {
			err = -ENOMEM;
			AuTraceErr(err);
			goto out_free;
		}
		ent->dr_h_ino = be64_to_cpu((__force __be64)u64);
		AuDbg("hi%llu, %pD2\n",
		      (unsigned long long)ent->dr_h_ino, hinofile);
		hidx = au_dr_ihash(ent->dr_h_ino);
		au_hbl_add(&ent->dr_hnode, hbl + hidx);
	}
	goto out; /* success */

out_free:
	au_dr_hino_free(dr);
out:
	AuTraceErr(err);
	return err;
}

/*
 * @bindex/@br is a switch to distinguish whether suspending hnotify or not.
 * @path is a switch to distinguish load and store.
 */
static int au_dr_hino(struct super_block *sb, aufs_bindex_t bindex,
		      struct au_branch *br, const struct path *path)
{
	int err, flags;
	unsigned char load, suspend;
	struct file *hinofile;
	struct au_hinode *hdir;
	struct inode *dir, *delegated;
	struct path hinopath;
	struct qstr hinoname = QSTR_INIT(AUFS_WH_DR_BRHINO,
					 sizeof(AUFS_WH_DR_BRHINO) - 1);

	AuDebugOn(bindex < 0 && !br);
	AuDebugOn(bindex >= 0 && br);

	err = -EINVAL;
	suspend = !br;
	if (suspend)
		br = au_sbr(sb, bindex);
	load = !!path;
	if (!load) {
		path = &br->br_path;
		AuDebugOn(!au_br_writable(br->br_perm));
		if (unlikely(!au_br_writable(br->br_perm)))
			goto out;
	}

	hdir = NULL;
	if (suspend) {
		dir = d_inode(sb->s_root);
		hdir = au_hinode(au_ii(dir), bindex);
		dir = hdir->hi_inode;
		au_hn_inode_lock_nested(hdir, AuLsc_I_CHILD);
	} else {
		dir = d_inode(path->dentry);
		inode_lock_nested(dir, AuLsc_I_CHILD);
	}
	hinopath.dentry = vfsub_lkup_one(&hinoname, path->dentry);
	err = PTR_ERR(hinopath.dentry);
	if (IS_ERR(hinopath.dentry))
		goto out_unlock;

	err = 0;
	flags = O_RDONLY;
	if (load) {
		if (d_is_negative(hinopath.dentry))
			goto out_dput; /* success */
	} else {
		if (au_dr_hino_test_empty(&br->br_dirren)) {
			if (d_is_positive(hinopath.dentry)) {
				delegated = NULL;
				err = vfsub_unlink(dir, &hinopath, &delegated,
						   /*force*/0);
				AuTraceErr(err);
				if (unlikely(err))
					pr_err("ignored err %d, %pd2\n",
					       err, hinopath.dentry);
				if (unlikely(err == -EWOULDBLOCK))
					iput(delegated);
				err = 0;
			}
			goto out_dput;
		} else if (!d_is_positive(hinopath.dentry)) {
			err = vfsub_create(dir, &hinopath, 0600,
					   /*want_excl*/false);
			AuTraceErr(err);
			if (unlikely(err))
				goto out_dput;
		}
		flags = O_WRONLY;
	}
	hinopath.mnt = path->mnt;
	hinofile = vfsub_dentry_open(&hinopath, flags);
	if (suspend)
		au_hn_inode_unlock(hdir);
	else
		inode_unlock(dir);
	dput(hinopath.dentry);
	AuTraceErrPtr(hinofile);
	if (IS_ERR(hinofile)) {
		err = PTR_ERR(hinofile);
		goto out;
	}

	if (load)
		err = au_dr_hino_load(&br->br_dirren, hinofile);
	else
		err = au_dr_hino_store(sb, br, hinofile);
	fput(hinofile);
	goto out;

out_dput:
	dput(hinopath.dentry);
out_unlock:
	if (suspend)
		au_hn_inode_unlock(hdir);
	else
		inode_unlock(dir);
out:
	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

static int au_dr_brid_init(struct au_dr_brid *brid, const struct path *path)
{
	int err;
	struct kstatfs kstfs;
	dev_t dev;
	struct dentry *dentry;
	struct super_block *sb;

	err = vfs_statfs((void *)path, &kstfs);
	AuTraceErr(err);
	if (unlikely(err))
		goto out;

	/* todo: support for UUID */

	if (kstfs.f_fsid.val[0] || kstfs.f_fsid.val[1]) {
		brid->type = AuBrid_FSID;
		brid->fsid = kstfs.f_fsid;
	} else {
		dentry = path->dentry;
		sb = dentry->d_sb;
		dev = sb->s_dev;
		if (dev) {
			brid->type = AuBrid_DEV;
			brid->dev = dev;
		}
	}

out:
	return err;
}

int au_dr_br_init(struct super_block *sb, struct au_branch *br,
		  const struct path *path)
{
	int err, i;
	struct au_dr_br *dr;
	struct hlist_bl_head *hbl;

	dr = &br->br_dirren;
	hbl = dr->dr_h_ino;
	for (i = 0; i < AuDirren_NHASH; i++, hbl++)
		INIT_HLIST_BL_HEAD(hbl);

	err = au_dr_brid_init(&dr->dr_brid, path);
	if (unlikely(err))
		goto out;

	if (au_opt_test(au_mntflags(sb), DIRREN))
		err = au_dr_hino(sb, /*bindex*/-1, br, path);

out:
	AuTraceErr(err);
	return err;
}

int au_dr_br_fin(struct super_block *sb, struct au_branch *br)
{
	int err;

	err = 0;
	if (au_br_writable(br->br_perm))
		err = au_dr_hino(sb, /*bindex*/-1, br, /*path*/NULL);
	if (!err)
		au_dr_hino_free(&br->br_dirren);

	return err;
}

/* ---------------------------------------------------------------------- */

static int au_brid_str(struct au_dr_brid *brid, struct inode *h_inode,
		       char *buf, size_t sz)
{
	int err;
	unsigned int major, minor;
	char *p;

	p = buf;
	err = snprintf(p, sz, "%d_", brid->type);
	AuDebugOn(err > sz);
	p += err;
	sz -= err;
	switch (brid->type) {
	case AuBrid_Unset:
		return -EINVAL;
	case AuBrid_UUID:
		err = snprintf(p, sz, "%pU", brid->uuid.b);
		break;
	case AuBrid_FSID:
		err = snprintf(p, sz, "%08x-%08x",
			       brid->fsid.val[0], brid->fsid.val[1]);
		break;
	case AuBrid_DEV:
		major = MAJOR(brid->dev);
		minor = MINOR(brid->dev);
		if (major <= 0xff && minor <= 0xff)
			err = snprintf(p, sz, "%02x%02x", major, minor);
		else
			err = snprintf(p, sz, "%03x:%05x", major, minor);
		break;
	}
	AuDebugOn(err > sz);
	p += err;
	sz -= err;
	err = snprintf(p, sz, "_%llu", (unsigned long long)h_inode->i_ino);
	AuDebugOn(err > sz);
	p += err;
	sz -= err;

	return p - buf;
}

static int au_drinfo_name(struct au_branch *br, char *name, int len)
{
	int rlen;
	struct dentry *br_dentry;
	struct inode *br_inode;

	br_dentry = au_br_dentry(br);
	br_inode = d_inode(br_dentry);
	rlen = au_brid_str(&br->br_dirren.dr_brid, br_inode, name, len);
	AuDebugOn(rlen >= AUFS_DIRREN_ENV_VAL_SZ);
	AuDebugOn(rlen > len);

	return rlen;
}

/* ---------------------------------------------------------------------- */

/*
 * from the given @h_dentry, construct drinfo at @*fdata.
 * when the size of @*fdata is not enough, reallocate and return new @fdata and
 * @allocated.
 */
static int au_drinfo_construct(struct au_drinfo_fdata **fdata,
			       struct dentry *h_dentry,
			       unsigned char *allocated)
{
	int err, v;
	struct au_drinfo_fdata *f, *p;
	struct au_drinfo *drinfo;
	struct inode *h_inode;
	struct qstr *qname;

	err = 0;
	f = *fdata;
	h_inode = d_inode(h_dentry);
	qname = &h_dentry->d_name;
	drinfo = &f->drinfo;
	drinfo->ino = (__force uint64_t)cpu_to_be64(h_inode->i_ino);
	drinfo->oldnamelen = qname->len;
	if (*allocated < sizeof(*f) + qname->len) {
		v = roundup_pow_of_two(*allocated + qname->len);
		p = au_krealloc(f, v, GFP_NOFS, /*may_shrink*/0);
		if (unlikely(!p)) {
			err = -ENOMEM;
			AuTraceErr(err);
			goto out;
		}
		f = p;
		*fdata = f;
		*allocated = v;
		drinfo = &f->drinfo;
	}
	memcpy(drinfo->oldname, qname->name, qname->len);
	AuDbg("i%llu, %.*s\n",
	      be64_to_cpu((__force __be64)drinfo->ino), drinfo->oldnamelen,
	      drinfo->oldname);

out:
	AuTraceErr(err);
	return err;
}

/* callers have to free the return value */
static struct au_drinfo *au_drinfo_read_k(struct file *file, ino_t h_ino)
{
	struct au_drinfo *ret, *drinfo;
	struct au_drinfo_fdata fdata;
	int len;
	loff_t pos;
	ssize_t ssz;

	ret = ERR_PTR(-EIO);
	pos = 0;
	ssz = vfsub_read_k(file, &fdata, sizeof(fdata), &pos);
	if (unlikely(ssz != sizeof(fdata))) {
		AuIOErr("ssz %zd, %u, %pD2\n",
			ssz, (unsigned int)sizeof(fdata), file);
		goto out;
	}

	fdata.magic = ntohl((__force __be32)fdata.magic);
	switch (fdata.magic) {
	case AUFS_DRINFO_MAGIC_V1:
		break;
	default:
		AuIOErr("magic-num 0x%x, 0x%x, %pD2\n",
			fdata.magic, AUFS_DRINFO_MAGIC_V1, file);
		goto out;
	}

	drinfo = &fdata.drinfo;
	len = drinfo->oldnamelen;
	if (!len) {
		AuIOErr("broken drinfo %pD2\n", file);
		goto out;
	}

	ret = NULL;
	drinfo->ino = be64_to_cpu((__force __be64)drinfo->ino);
	if (unlikely(h_ino && drinfo->ino != h_ino)) {
		AuDbg("ignored i%llu, i%llu, %pD2\n",
		      (unsigned long long)drinfo->ino,
		      (unsigned long long)h_ino, file);
		goto out; /* success */
	}

	ret = kmalloc(sizeof(*ret) + len, GFP_NOFS);
	if (unlikely(!ret)) {
		ret = ERR_PTR(-ENOMEM);
		AuTraceErrPtr(ret);
		goto out;
	}

	*ret = *drinfo;
	ssz = vfsub_read_k(file, (void *)ret->oldname, len, &pos);
	if (unlikely(ssz != len)) {
		kfree(ret);
		ret = ERR_PTR(-EIO);
		AuIOErr("ssz %zd, %u, %pD2\n", ssz, len, file);
		goto out;
	}

	AuDbg("oldname %.*s\n", ret->oldnamelen, ret->oldname);

out:
	return ret;
}

/* ---------------------------------------------------------------------- */

/* in order to be revertible */
struct au_drinfo_rev_elm {
	int			created;
	struct dentry		*info_dentry;
	struct au_drinfo	*info_last;
};

struct au_drinfo_rev {
	unsigned char			already;
	aufs_bindex_t			nelm;
	struct au_drinfo_rev_elm	elm[0];
};

/* todo: isn't it too large? */
struct au_drinfo_store {
	struct path h_ppath;
	struct dentry *h_dentry;
	struct au_drinfo_fdata *fdata;
	char *infoname;			/* inside of whname, just after PFX */
	char whname[sizeof(AUFS_WH_DR_INFO_PFX) + AUFS_DIRREN_ENV_VAL_SZ];
	aufs_bindex_t btgt, btail;
	unsigned char no_sio,
		allocated,		/* current size of *fdata */
		infonamelen,		/* room size for p */
		whnamelen,		/* length of the genarated name */
		renameback;		/* renamed back */
};

/* on rename(2) error, the caller should revert it using @elm */
static int au_drinfo_do_store(struct au_drinfo_store *w,
			      struct au_drinfo_rev_elm *elm)
{
	int err, len;
	ssize_t ssz;
	loff_t pos;
	struct path infopath = {
		.mnt = w->h_ppath.mnt
	};
	struct inode *h_dir, *h_inode, *delegated;
	struct file *infofile;
	struct qstr *qname;

	AuDebugOn(elm
		  && memcmp(elm, page_address(ZERO_PAGE(0)), sizeof(*elm)));

	infopath.dentry = vfsub_lookup_one_len(w->whname, w->h_ppath.dentry,
					       w->whnamelen);
	AuTraceErrPtr(infopath.dentry);
	if (IS_ERR(infopath.dentry)) {
		err = PTR_ERR(infopath.dentry);
		goto out;
	}

	err = 0;
	h_dir = d_inode(w->h_ppath.dentry);
	if (elm && d_is_negative(infopath.dentry)) {
		err = vfsub_create(h_dir, &infopath, 0600, /*want_excl*/true);
		AuTraceErr(err);
		if (unlikely(err))
			goto out_dput;
		elm->created = 1;
		elm->info_dentry = dget(infopath.dentry);
	}

	infofile = vfsub_dentry_open(&infopath, O_RDWR);
	AuTraceErrPtr(infofile);
	if (IS_ERR(infofile)) {
		err = PTR_ERR(infofile);
		goto out_dput;
	}

	h_inode = d_inode(infopath.dentry);
	if (elm && i_size_read(h_inode)) {
		h_inode = d_inode(w->h_dentry);
		elm->info_last = au_drinfo_read_k(infofile, h_inode->i_ino);
		AuTraceErrPtr(elm->info_last);
		if (IS_ERR(elm->info_last)) {
			err = PTR_ERR(elm->info_last);
			elm->info_last = NULL;
			AuDebugOn(elm->info_dentry);
			goto out_fput;
		}
	}

	if (elm && w->renameback) {
		delegated = NULL;
		err = vfsub_unlink(h_dir, &infopath, &delegated, /*force*/0);
		AuTraceErr(err);
		if (unlikely(err == -EWOULDBLOCK))
			iput(delegated);
		goto out_fput;
	}

	pos = 0;
	qname = &w->h_dentry->d_name;
	len = sizeof(*w->fdata) + qname->len;
	if (!elm)
		len = sizeof(*w->fdata) + w->fdata->drinfo.oldnamelen;
	ssz = vfsub_write_k(infofile, w->fdata, len, &pos);
	if (ssz == len) {
		AuDbg("hi%llu, %.*s\n", w->fdata->drinfo.ino,
		      w->fdata->drinfo.oldnamelen, w->fdata->drinfo.oldname);
		goto out_fput; /* success */
	} else {
		err = -EIO;
		if (ssz < 0)
			err = ssz;
		/* the caller should revert it using @elm */
	}

out_fput:
	fput(infofile);
out_dput:
	dput(infopath.dentry);
out:
	AuTraceErr(err);
	return err;
}

struct au_call_drinfo_do_store_args {
	int *errp;
	struct au_drinfo_store *w;
	struct au_drinfo_rev_elm *elm;
};

static void au_call_drinfo_do_store(void *args)
{
	struct au_call_drinfo_do_store_args *a = args;

	*a->errp = au_drinfo_do_store(a->w, a->elm);
}

static int au_drinfo_store_sio(struct au_drinfo_store *w,
			       struct au_drinfo_rev_elm *elm)
{
	int err, wkq_err;

	if (w->no_sio)
		err = au_drinfo_do_store(w, elm);
	else {
		struct au_call_drinfo_do_store_args a = {
			.errp	= &err,
			.w	= w,
			.elm	= elm
		};
		wkq_err = au_wkq_wait(au_call_drinfo_do_store, &a);
		if (unlikely(wkq_err))
			err = wkq_err;
	}
	AuTraceErr(err);

	return err;
}

static int au_drinfo_store_work_init(struct au_drinfo_store *w,
				     aufs_bindex_t btgt)
{
	int err;

	memset(w, 0, sizeof(*w));
	w->allocated = roundup_pow_of_two(sizeof(*w->fdata) + 40);
	strcpy(w->whname, AUFS_WH_DR_INFO_PFX);
	w->infoname = w->whname + sizeof(AUFS_WH_DR_INFO_PFX) - 1;
	w->infonamelen = sizeof(w->whname) - sizeof(AUFS_WH_DR_INFO_PFX);
	w->btgt = btgt;
	w->no_sio = !!uid_eq(current_fsuid(), GLOBAL_ROOT_UID);

	err = -ENOMEM;
	w->fdata = kcalloc(1, w->allocated, GFP_NOFS);
	if (unlikely(!w->fdata)) {
		AuTraceErr(err);
		goto out;
	}
	w->fdata->magic = (__force uint32_t)htonl(AUFS_DRINFO_MAGIC_V1);
	err = 0;

out:
	return err;
}

static void au_drinfo_store_work_fin(struct au_drinfo_store *w)
{
	kfree(w->fdata);
}

static void au_drinfo_store_rev(struct au_drinfo_rev *rev,
				struct au_drinfo_store *w)
{
	struct au_drinfo_rev_elm *elm;
	struct inode *h_dir, *delegated;
	int err, nelm;
	struct path infopath = {
		.mnt = w->h_ppath.mnt
	};

	h_dir = d_inode(w->h_ppath.dentry);
	IMustLock(h_dir);

	err = 0;
	elm = rev->elm;
	for (nelm = rev->nelm; nelm > 0; nelm--, elm++) {
		AuDebugOn(elm->created && elm->info_last);
		if (elm->created) {
			AuDbg("here\n");
			delegated = NULL;
			infopath.dentry = elm->info_dentry;
			err = vfsub_unlink(h_dir, &infopath, &delegated,
					   !w->no_sio);
			AuTraceErr(err);
			if (unlikely(err == -EWOULDBLOCK))
				iput(delegated);
			dput(elm->info_dentry);
		} else if (elm->info_last) {
			AuDbg("here\n");
			w->fdata->drinfo = *elm->info_last;
			memcpy(w->fdata->drinfo.oldname,
			       elm->info_last->oldname,
			       elm->info_last->oldnamelen);
			err = au_drinfo_store_sio(w, /*elm*/NULL);
			kfree(elm->info_last);
		}
		if (unlikely(err))
			AuIOErr("%d, %s\n", err, w->whname);
		/* go on even if err */
	}
}

/* caller has to call au_dr_rename_fin() later */
static int au_drinfo_store(struct dentry *dentry, aufs_bindex_t btgt,
			   struct qstr *dst_name, void *_rev)
{
	int err, sz, nelm;
	aufs_bindex_t bindex, btail;
	struct au_drinfo_store work;
	struct au_drinfo_rev *rev, **p;
	struct au_drinfo_rev_elm *elm;
	struct super_block *sb;
	struct au_branch *br;
	struct au_hinode *hdir;

	err = au_drinfo_store_work_init(&work, btgt);
	AuTraceErr(err);
	if (unlikely(err))
		goto out;

	err = -ENOMEM;
	btail = au_dbtaildir(dentry);
	nelm = btail - btgt;
	sz = sizeof(*rev) + sizeof(*elm) * nelm;
	rev = kcalloc(1, sz, GFP_NOFS);
	if (unlikely(!rev)) {
		AuTraceErr(err);
		goto out_args;
	}
	rev->nelm = nelm;
	elm = rev->elm;
	p = _rev;
	*p = rev;

	err = 0;
	sb = dentry->d_sb;
	work.h_ppath.dentry = au_h_dptr(dentry, btgt);
	work.h_ppath.mnt = au_sbr_mnt(sb, btgt);
	hdir = au_hi(d_inode(dentry), btgt);
	au_hn_inode_lock_nested(hdir, AuLsc_I_CHILD);
	for (bindex = btgt + 1; bindex <= btail; bindex++, elm++) {
		work.h_dentry = au_h_dptr(dentry, bindex);
		if (!work.h_dentry)
			continue;

		err = au_drinfo_construct(&work.fdata, work.h_dentry,
					  &work.allocated);
		AuTraceErr(err);
		if (unlikely(err))
			break;

		work.renameback = au_qstreq(&work.h_dentry->d_name, dst_name);
		br = au_sbr(sb, bindex);
		work.whnamelen = sizeof(AUFS_WH_DR_INFO_PFX) - 1;
		work.whnamelen += au_drinfo_name(br, work.infoname,
						 work.infonamelen);
		AuDbg("whname %.*s, i%llu, %.*s\n",
		      work.whnamelen, work.whname,
		      be64_to_cpu((__force __be64)work.fdata->drinfo.ino),
		      work.fdata->drinfo.oldnamelen,
		      work.fdata->drinfo.oldname);

		err = au_drinfo_store_sio(&work, elm);
		AuTraceErr(err);
		if (unlikely(err))
			break;
	}
	if (unlikely(err)) {
		/* revert all drinfo */
		au_drinfo_store_rev(rev, &work);
		kfree(rev);
		*p = NULL;
	}
	au_hn_inode_unlock(hdir);

out_args:
	au_drinfo_store_work_fin(&work);
out:
	return err;
}

/* ---------------------------------------------------------------------- */

int au_dr_rename(struct dentry *src, aufs_bindex_t bindex,
		 struct qstr *dst_name, void *_rev)
{
	int err, already;
	ino_t ino;
	struct super_block *sb;
	struct au_branch *br;
	struct au_dr_br *dr;
	struct dentry *h_dentry;
	struct inode *h_inode;
	struct au_dr_hino *ent;
	struct au_drinfo_rev *rev, **p;

	AuDbg("bindex %d\n", bindex);

	err = -ENOMEM;
	ent = kmalloc(sizeof(*ent), GFP_NOFS);
	if (unlikely(!ent))
		goto out;

	sb = src->d_sb;
	br = au_sbr(sb, bindex);
	dr = &br->br_dirren;
	h_dentry = au_h_dptr(src, bindex);
	h_inode = d_inode(h_dentry);
	ino = h_inode->i_ino;
	ent->dr_h_ino = ino;
	already = au_dr_hino_test_add(dr, ino, ent);
	AuDbg("b%d, hi%llu, already %d\n",
	      bindex, (unsigned long long)ino, already);

	err = au_drinfo_store(src, bindex, dst_name, _rev);
	AuTraceErr(err);
	if (!err) {
		p = _rev;
		rev = *p;
		rev->already = already;
		goto out; /* success */
	}

	/* revert */
	if (!already)
		au_dr_hino_del(dr, ent);
	kfree(ent);

out:
	AuTraceErr(err);
	return err;
}

void au_dr_rename_fin(struct dentry *src, aufs_bindex_t btgt, void *_rev)
{
	struct au_drinfo_rev *rev;
	struct au_drinfo_rev_elm *elm;
	int nelm;

	rev = _rev;
	elm = rev->elm;
	for (nelm = rev->nelm; nelm > 0; nelm--, elm++) {
		dput(elm->info_dentry);
		kfree(elm->info_last);
	}
	kfree(rev);
}

void au_dr_rename_rev(struct dentry *src, aufs_bindex_t btgt, void *_rev)
{
	int err;
	struct au_drinfo_store work;
	struct au_drinfo_rev *rev = _rev;
	struct super_block *sb;
	struct au_branch *br;
	struct inode *h_inode;
	struct au_dr_br *dr;
	struct au_dr_hino *ent;

	err = au_drinfo_store_work_init(&work, btgt);
	if (unlikely(err))
		goto out;

	sb = src->d_sb;
	br = au_sbr(sb, btgt);
	work.h_ppath.dentry = au_h_dptr(src, btgt);
	work.h_ppath.mnt = au_br_mnt(br);
	au_drinfo_store_rev(rev, &work);
	au_drinfo_store_work_fin(&work);
	if (rev->already)
		goto out;

	dr = &br->br_dirren;
	h_inode = d_inode(work.h_ppath.dentry);
	ent = au_dr_hino_find(dr, h_inode->i_ino);
	BUG_ON(!ent);
	au_dr_hino_del(dr, ent);
	kfree(ent);

out:
	kfree(rev);
	if (unlikely(err))
		pr_err("failed to remove dirren info\n");
}

/* ---------------------------------------------------------------------- */

static struct au_drinfo *au_drinfo_do_load(struct path *h_ppath,
					   char *whname, int whnamelen,
					   struct dentry **info_dentry)
{
	struct au_drinfo *drinfo;
	struct file *f;
	struct inode *h_dir;
	struct path infopath;
	int unlocked;

	AuDbg("%pd/%.*s\n", h_ppath->dentry, whnamelen, whname);

	*info_dentry = NULL;
	drinfo = NULL;
	unlocked = 0;
	h_dir = d_inode(h_ppath->dentry);
	vfsub_inode_lock_shared_nested(h_dir, AuLsc_I_PARENT);
	infopath.dentry = vfsub_lookup_one_len(whname, h_ppath->dentry,
					       whnamelen);
	if (IS_ERR(infopath.dentry)) {
		drinfo = (void *)infopath.dentry;
		goto out;
	}

	if (d_is_negative(infopath.dentry))
		goto out_dput; /* success */

	infopath.mnt = h_ppath->mnt;
	f = vfsub_dentry_open(&infopath, O_RDONLY);
	inode_unlock_shared(h_dir);
	unlocked = 1;
	if (IS_ERR(f)) {
		drinfo = (void *)f;
		goto out_dput;
	}

	drinfo = au_drinfo_read_k(f, /*h_ino*/0);
	if (IS_ERR_OR_NULL(drinfo))
		goto out_fput;

	AuDbg("oldname %.*s\n", drinfo->oldnamelen, drinfo->oldname);
	*info_dentry = dget(infopath.dentry); /* keep it alive */

out_fput:
	fput(f);
out_dput:
	dput(infopath.dentry);
out:
	if (!unlocked)
		inode_unlock_shared(h_dir);
	AuTraceErrPtr(drinfo);
	return drinfo;
}

struct au_drinfo_do_load_args {
	struct au_drinfo **drinfop;
	struct path *h_ppath;
	char *whname;
	int whnamelen;
	struct dentry **info_dentry;
};

static void au_call_drinfo_do_load(void *args)
{
	struct au_drinfo_do_load_args *a = args;

	*a->drinfop = au_drinfo_do_load(a->h_ppath, a->whname, a->whnamelen,
					a->info_dentry);
}

struct au_drinfo_load {
	struct path h_ppath;
	struct qstr *qname;
	unsigned char no_sio;

	aufs_bindex_t ninfo;
	struct au_drinfo **drinfo;
};

static int au_drinfo_load(struct au_drinfo_load *w, aufs_bindex_t bindex,
			  struct au_branch *br)
{
	int err, wkq_err, whnamelen, e;
	char whname[sizeof(AUFS_WH_DR_INFO_PFX) + AUFS_DIRREN_ENV_VAL_SZ]
		= AUFS_WH_DR_INFO_PFX;
	struct au_drinfo *drinfo;
	struct qstr oldname;
	struct inode *h_dir, *delegated;
	struct dentry *info_dentry;
	struct path infopath;

	whnamelen = sizeof(AUFS_WH_DR_INFO_PFX) - 1;
	whnamelen += au_drinfo_name(br, whname + whnamelen,
				    sizeof(whname) - whnamelen);
	if (w->no_sio)
		drinfo = au_drinfo_do_load(&w->h_ppath, whname, whnamelen,
					   &info_dentry);
	else {
		struct au_drinfo_do_load_args args = {
			.drinfop	= &drinfo,
			.h_ppath	= &w->h_ppath,
			.whname		= whname,
			.whnamelen	= whnamelen,
			.info_dentry	= &info_dentry
		};
		wkq_err = au_wkq_wait(au_call_drinfo_do_load, &args);
		if (unlikely(wkq_err))
			drinfo = ERR_PTR(wkq_err);
	}
	err = PTR_ERR(drinfo);
	if (IS_ERR_OR_NULL(drinfo))
		goto out;

	err = 0;
	oldname.len = drinfo->oldnamelen;
	oldname.name = drinfo->oldname;
	if (au_qstreq(w->qname, &oldname)) {
		/* the name is renamed back */
		kfree(drinfo);
		drinfo = NULL;

		infopath.dentry = info_dentry;
		infopath.mnt = w->h_ppath.mnt;
		h_dir = d_inode(w->h_ppath.dentry);
		delegated = NULL;
		inode_lock_nested(h_dir, AuLsc_I_PARENT);
		e = vfsub_unlink(h_dir, &infopath, &delegated, !w->no_sio);
		inode_unlock(h_dir);
		if (unlikely(e))
			AuIOErr("ignored %d, %pd2\n", e, &infopath.dentry);
		if (unlikely(e == -EWOULDBLOCK))
			iput(delegated);
	}
	kfree(w->drinfo[bindex]);
	w->drinfo[bindex] = drinfo;
	dput(info_dentry);

out:
	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

static void au_dr_lkup_free(struct au_drinfo **drinfo, int n)
{
	struct au_drinfo **p = drinfo;

	while (n-- > 0)
		kfree(*drinfo++);
	kfree(p);
}

int au_dr_lkup(struct au_do_lookup_args *lkup, struct dentry *dentry,
	       aufs_bindex_t btgt)
{
	int err, ninfo;
	struct au_drinfo_load w;
	aufs_bindex_t bindex, bbot;
	struct au_branch *br;
	struct inode *h_dir;
	struct au_dr_hino *ent;
	struct super_block *sb;

	AuDbg("%.*s, name %.*s, whname %.*s, b%d\n",
	      AuLNPair(&dentry->d_name), AuLNPair(&lkup->dirren.dr_name),
	      AuLNPair(&lkup->whname), btgt);

	sb = dentry->d_sb;
	bbot = au_sbbot(sb);
	w.ninfo = bbot + 1;
	if (!lkup->dirren.drinfo) {
		lkup->dirren.drinfo = kcalloc(w.ninfo,
					      sizeof(*lkup->dirren.drinfo),
					      GFP_NOFS);
		if (unlikely(!lkup->dirren.drinfo)) {
			err = -ENOMEM;
			goto out;
		}
		lkup->dirren.ninfo = w.ninfo;
	}
	w.drinfo = lkup->dirren.drinfo;
	w.no_sio = !!uid_eq(current_fsuid(), GLOBAL_ROOT_UID);
	w.h_ppath.dentry = au_h_dptr(dentry, btgt);
	AuDebugOn(!w.h_ppath.dentry);
	w.h_ppath.mnt = au_sbr_mnt(sb, btgt);
	w.qname = &dentry->d_name;

	ninfo = 0;
	for (bindex = btgt + 1; bindex <= bbot; bindex++) {
		br = au_sbr(sb, bindex);
		err = au_drinfo_load(&w, bindex, br);
		if (unlikely(err))
			goto out_free;
		if (w.drinfo[bindex])
			ninfo++;
	}
	if (!ninfo) {
		br = au_sbr(sb, btgt);
		h_dir = d_inode(w.h_ppath.dentry);
		ent = au_dr_hino_find(&br->br_dirren, h_dir->i_ino);
		AuDebugOn(!ent);
		au_dr_hino_del(&br->br_dirren, ent);
		kfree(ent);
	}
	goto out; /* success */

out_free:
	au_dr_lkup_free(lkup->dirren.drinfo, lkup->dirren.ninfo);
	lkup->dirren.ninfo = 0;
	lkup->dirren.drinfo = NULL;
out:
	AuTraceErr(err);
	return err;
}

void au_dr_lkup_fin(struct au_do_lookup_args *lkup)
{
	au_dr_lkup_free(lkup->dirren.drinfo, lkup->dirren.ninfo);
}

int au_dr_lkup_name(struct au_do_lookup_args *lkup, aufs_bindex_t btgt)
{
	int err;
	struct au_drinfo *drinfo;

	err = 0;
	if (!lkup->dirren.drinfo)
		goto out;
	AuDebugOn(lkup->dirren.ninfo < btgt + 1);
	drinfo = lkup->dirren.drinfo[btgt + 1];
	if (!drinfo)
		goto out;

	kfree(lkup->whname.name);
	lkup->whname.name = NULL;
	lkup->dirren.dr_name.len = drinfo->oldnamelen;
	lkup->dirren.dr_name.name = drinfo->oldname;
	lkup->name = &lkup->dirren.dr_name;
	err = au_wh_name_alloc(&lkup->whname, lkup->name);
	if (!err)
		AuDbg("name %.*s, whname %.*s, b%d\n",
		      AuLNPair(lkup->name), AuLNPair(&lkup->whname),
		      btgt);

out:
	AuTraceErr(err);
	return err;
}

int au_dr_lkup_h_ino(struct au_do_lookup_args *lkup, aufs_bindex_t bindex,
		     ino_t h_ino)
{
	int match;
	struct au_drinfo *drinfo;

	match = 1;
	if (!lkup->dirren.drinfo)
		goto out;
	AuDebugOn(lkup->dirren.ninfo < bindex + 1);
	drinfo = lkup->dirren.drinfo[bindex + 1];
	if (!drinfo)
		goto out;

	match = (drinfo->ino == h_ino);
	AuDbg("match %d\n", match);

out:
	return match;
}

/* ---------------------------------------------------------------------- */

int au_dr_opt_set(struct super_block *sb)
{
	int err;
	aufs_bindex_t bindex, bbot;
	struct au_branch *br;

	err = 0;
	bbot = au_sbbot(sb);
	for (bindex = 0; !err && bindex <= bbot; bindex++) {
		br = au_sbr(sb, bindex);
		err = au_dr_hino(sb, bindex, /*br*/NULL, &br->br_path);
	}

	return err;
}

int au_dr_opt_flush(struct super_block *sb)
{
	int err;
	aufs_bindex_t bindex, bbot;
	struct au_branch *br;

	err = 0;
	bbot = au_sbbot(sb);
	for (bindex = 0; !err && bindex <= bbot; bindex++) {
		br = au_sbr(sb, bindex);
		if (au_br_writable(br->br_perm))
			err = au_dr_hino(sb, bindex, /*br*/NULL, /*path*/NULL);
	}

	return err;
}

int au_dr_opt_clr(struct super_block *sb, int no_flush)
{
	int err;
	aufs_bindex_t bindex, bbot;
	struct au_branch *br;

	err = 0;
	if (!no_flush) {
		err = au_dr_opt_flush(sb);
		if (unlikely(err))
			goto out;
	}

	bbot = au_sbbot(sb);
	for (bindex = 0; bindex <= bbot; bindex++) {
		br = au_sbr(sb, bindex);
		au_dr_hino_free(&br->br_dirren);
	}

out:
	return err;
}
