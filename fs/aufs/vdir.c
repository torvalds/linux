/*
 * Copyright (C) 2005-2017 Junjiro R. Okajima
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
 * virtual or vertical directory
 */

#include "aufs.h"

static unsigned int calc_size(int nlen)
{
	return ALIGN(sizeof(struct au_vdir_de) + nlen, sizeof(ino_t));
}

static int set_deblk_end(union au_vdir_deblk_p *p,
			 union au_vdir_deblk_p *deblk_end)
{
	if (calc_size(0) <= deblk_end->deblk - p->deblk) {
		p->de->de_str.len = 0;
		/* smp_mb(); */
		return 0;
	}
	return -1; /* error */
}

/* returns true or false */
static int is_deblk_end(union au_vdir_deblk_p *p,
			union au_vdir_deblk_p *deblk_end)
{
	if (calc_size(0) <= deblk_end->deblk - p->deblk)
		return !p->de->de_str.len;
	return 1;
}

static unsigned char *last_deblk(struct au_vdir *vdir)
{
	return vdir->vd_deblk[vdir->vd_nblk - 1];
}

/* ---------------------------------------------------------------------- */

/* estimate the appropriate size for name hash table */
unsigned int au_rdhash_est(loff_t sz)
{
	unsigned int n;

	n = UINT_MAX;
	sz >>= 10;
	if (sz < n)
		n = sz;
	if (sz < AUFS_RDHASH_DEF)
		n = AUFS_RDHASH_DEF;
	/* pr_info("n %u\n", n); */
	return n;
}

/*
 * the allocated memory has to be freed by
 * au_nhash_wh_free() or au_nhash_de_free().
 */
int au_nhash_alloc(struct au_nhash *nhash, unsigned int num_hash, gfp_t gfp)
{
	struct hlist_head *head;
	unsigned int u;
	size_t sz;

	sz = sizeof(*nhash->nh_head) * num_hash;
	head = kmalloc(sz, gfp);
	if (head) {
		nhash->nh_num = num_hash;
		nhash->nh_head = head;
		for (u = 0; u < num_hash; u++)
			INIT_HLIST_HEAD(head++);
		return 0; /* success */
	}

	return -ENOMEM;
}

static void nhash_count(struct hlist_head *head)
{
#if 0
	unsigned long n;
	struct hlist_node *pos;

	n = 0;
	hlist_for_each(pos, head)
		n++;
	pr_info("%lu\n", n);
#endif
}

static void au_nhash_wh_do_free(struct hlist_head *head)
{
	struct au_vdir_wh *pos;
	struct hlist_node *node;

	hlist_for_each_entry_safe(pos, node, head, wh_hash)
		kfree(pos);
}

static void au_nhash_de_do_free(struct hlist_head *head)
{
	struct au_vdir_dehstr *pos;
	struct hlist_node *node;

	hlist_for_each_entry_safe(pos, node, head, hash)
		au_cache_free_vdir_dehstr(pos);
}

static void au_nhash_do_free(struct au_nhash *nhash,
			     void (*free)(struct hlist_head *head))
{
	unsigned int n;
	struct hlist_head *head;

	n = nhash->nh_num;
	if (!n)
		return;

	head = nhash->nh_head;
	while (n-- > 0) {
		nhash_count(head);
		free(head++);
	}
	kfree(nhash->nh_head);
}

void au_nhash_wh_free(struct au_nhash *whlist)
{
	au_nhash_do_free(whlist, au_nhash_wh_do_free);
}

static void au_nhash_de_free(struct au_nhash *delist)
{
	au_nhash_do_free(delist, au_nhash_de_do_free);
}

/* ---------------------------------------------------------------------- */

int au_nhash_test_longer_wh(struct au_nhash *whlist, aufs_bindex_t btgt,
			    int limit)
{
	int num;
	unsigned int u, n;
	struct hlist_head *head;
	struct au_vdir_wh *pos;

	num = 0;
	n = whlist->nh_num;
	head = whlist->nh_head;
	for (u = 0; u < n; u++, head++)
		hlist_for_each_entry(pos, head, wh_hash)
			if (pos->wh_bindex == btgt && ++num > limit)
				return 1;
	return 0;
}

static struct hlist_head *au_name_hash(struct au_nhash *nhash,
				       unsigned char *name,
				       unsigned int len)
{
	unsigned int v;
	/* const unsigned int magic_bit = 12; */

	AuDebugOn(!nhash->nh_num || !nhash->nh_head);

	v = 0;
	if (len > 8)
		len = 8;
	while (len--)
		v += *name++;
	/* v = hash_long(v, magic_bit); */
	v %= nhash->nh_num;
	return nhash->nh_head + v;
}

static int au_nhash_test_name(struct au_vdir_destr *str, const char *name,
			      int nlen)
{
	return str->len == nlen && !memcmp(str->name, name, nlen);
}

/* returns found or not */
int au_nhash_test_known_wh(struct au_nhash *whlist, char *name, int nlen)
{
	struct hlist_head *head;
	struct au_vdir_wh *pos;
	struct au_vdir_destr *str;

	head = au_name_hash(whlist, name, nlen);
	hlist_for_each_entry(pos, head, wh_hash) {
		str = &pos->wh_str;
		AuDbg("%.*s\n", str->len, str->name);
		if (au_nhash_test_name(str, name, nlen))
			return 1;
	}
	return 0;
}

/* returns found(true) or not */
static int test_known(struct au_nhash *delist, char *name, int nlen)
{
	struct hlist_head *head;
	struct au_vdir_dehstr *pos;
	struct au_vdir_destr *str;

	head = au_name_hash(delist, name, nlen);
	hlist_for_each_entry(pos, head, hash) {
		str = pos->str;
		AuDbg("%.*s\n", str->len, str->name);
		if (au_nhash_test_name(str, name, nlen))
			return 1;
	}
	return 0;
}

static void au_shwh_init_wh(struct au_vdir_wh *wh, ino_t ino,
			    unsigned char d_type)
{
#ifdef CONFIG_AUFS_SHWH
	wh->wh_ino = ino;
	wh->wh_type = d_type;
#endif
}

/* ---------------------------------------------------------------------- */

int au_nhash_append_wh(struct au_nhash *whlist, char *name, int nlen, ino_t ino,
		       unsigned int d_type, aufs_bindex_t bindex,
		       unsigned char shwh)
{
	int err;
	struct au_vdir_destr *str;
	struct au_vdir_wh *wh;

	AuDbg("%.*s\n", nlen, name);
	AuDebugOn(!whlist->nh_num || !whlist->nh_head);

	err = -ENOMEM;
	wh = kmalloc(sizeof(*wh) + nlen, GFP_NOFS);
	if (unlikely(!wh))
		goto out;

	err = 0;
	wh->wh_bindex = bindex;
	if (shwh)
		au_shwh_init_wh(wh, ino, d_type);
	str = &wh->wh_str;
	str->len = nlen;
	memcpy(str->name, name, nlen);
	hlist_add_head(&wh->wh_hash, au_name_hash(whlist, name, nlen));
	/* smp_mb(); */

out:
	return err;
}

static int append_deblk(struct au_vdir *vdir)
{
	int err;
	unsigned long ul;
	const unsigned int deblk_sz = vdir->vd_deblk_sz;
	union au_vdir_deblk_p p, deblk_end;
	unsigned char **o;

	err = -ENOMEM;
	o = au_krealloc(vdir->vd_deblk, sizeof(*o) * (vdir->vd_nblk + 1),
			GFP_NOFS, /*may_shrink*/0);
	if (unlikely(!o))
		goto out;

	vdir->vd_deblk = o;
	p.deblk = kmalloc(deblk_sz, GFP_NOFS);
	if (p.deblk) {
		ul = vdir->vd_nblk++;
		vdir->vd_deblk[ul] = p.deblk;
		vdir->vd_last.ul = ul;
		vdir->vd_last.p.deblk = p.deblk;
		deblk_end.deblk = p.deblk + deblk_sz;
		err = set_deblk_end(&p, &deblk_end);
	}

out:
	return err;
}

static int append_de(struct au_vdir *vdir, char *name, int nlen, ino_t ino,
		     unsigned int d_type, struct au_nhash *delist)
{
	int err;
	unsigned int sz;
	const unsigned int deblk_sz = vdir->vd_deblk_sz;
	union au_vdir_deblk_p p, *room, deblk_end;
	struct au_vdir_dehstr *dehstr;

	p.deblk = last_deblk(vdir);
	deblk_end.deblk = p.deblk + deblk_sz;
	room = &vdir->vd_last.p;
	AuDebugOn(room->deblk < p.deblk || deblk_end.deblk <= room->deblk
		  || !is_deblk_end(room, &deblk_end));

	sz = calc_size(nlen);
	if (unlikely(sz > deblk_end.deblk - room->deblk)) {
		err = append_deblk(vdir);
		if (unlikely(err))
			goto out;

		p.deblk = last_deblk(vdir);
		deblk_end.deblk = p.deblk + deblk_sz;
		/* smp_mb(); */
		AuDebugOn(room->deblk != p.deblk);
	}

	err = -ENOMEM;
	dehstr = au_cache_alloc_vdir_dehstr();
	if (unlikely(!dehstr))
		goto out;

	dehstr->str = &room->de->de_str;
	hlist_add_head(&dehstr->hash, au_name_hash(delist, name, nlen));
	room->de->de_ino = ino;
	room->de->de_type = d_type;
	room->de->de_str.len = nlen;
	memcpy(room->de->de_str.name, name, nlen);

	err = 0;
	room->deblk += sz;
	if (unlikely(set_deblk_end(room, &deblk_end)))
		err = append_deblk(vdir);
	/* smp_mb(); */

out:
	return err;
}

/* ---------------------------------------------------------------------- */

void au_vdir_free(struct au_vdir *vdir)
{
	unsigned char **deblk;

	deblk = vdir->vd_deblk;
	while (vdir->vd_nblk--)
		kfree(*deblk++);
	kfree(vdir->vd_deblk);
	au_cache_free_vdir(vdir);
}

static struct au_vdir *alloc_vdir(struct file *file)
{
	struct au_vdir *vdir;
	struct super_block *sb;
	int err;

	sb = file->f_path.dentry->d_sb;
	SiMustAnyLock(sb);

	err = -ENOMEM;
	vdir = au_cache_alloc_vdir();
	if (unlikely(!vdir))
		goto out;

	vdir->vd_deblk = kzalloc(sizeof(*vdir->vd_deblk), GFP_NOFS);
	if (unlikely(!vdir->vd_deblk))
		goto out_free;

	vdir->vd_deblk_sz = au_sbi(sb)->si_rdblk;
	if (!vdir->vd_deblk_sz) {
		/* estimate the appropriate size for deblk */
		vdir->vd_deblk_sz = au_dir_size(file, /*dentry*/NULL);
		/* pr_info("vd_deblk_sz %u\n", vdir->vd_deblk_sz); */
	}
	vdir->vd_nblk = 0;
	vdir->vd_version = 0;
	vdir->vd_jiffy = 0;
	err = append_deblk(vdir);
	if (!err)
		return vdir; /* success */

	kfree(vdir->vd_deblk);

out_free:
	au_cache_free_vdir(vdir);
out:
	vdir = ERR_PTR(err);
	return vdir;
}

static int reinit_vdir(struct au_vdir *vdir)
{
	int err;
	union au_vdir_deblk_p p, deblk_end;

	while (vdir->vd_nblk > 1) {
		kfree(vdir->vd_deblk[vdir->vd_nblk - 1]);
		/* vdir->vd_deblk[vdir->vd_nblk - 1] = NULL; */
		vdir->vd_nblk--;
	}
	p.deblk = vdir->vd_deblk[0];
	deblk_end.deblk = p.deblk + vdir->vd_deblk_sz;
	err = set_deblk_end(&p, &deblk_end);
	/* keep vd_dblk_sz */
	vdir->vd_last.ul = 0;
	vdir->vd_last.p.deblk = vdir->vd_deblk[0];
	vdir->vd_version = 0;
	vdir->vd_jiffy = 0;
	/* smp_mb(); */
	return err;
}

/* ---------------------------------------------------------------------- */

#define AuFillVdir_CALLED	1
#define AuFillVdir_WHABLE	(1 << 1)
#define AuFillVdir_SHWH		(1 << 2)
#define au_ftest_fillvdir(flags, name)	((flags) & AuFillVdir_##name)
#define au_fset_fillvdir(flags, name) \
	do { (flags) |= AuFillVdir_##name; } while (0)
#define au_fclr_fillvdir(flags, name) \
	do { (flags) &= ~AuFillVdir_##name; } while (0)

#ifndef CONFIG_AUFS_SHWH
#undef AuFillVdir_SHWH
#define AuFillVdir_SHWH		0
#endif

struct fillvdir_arg {
	struct dir_context	ctx;
	struct file		*file;
	struct au_vdir		*vdir;
	struct au_nhash		delist;
	struct au_nhash		whlist;
	aufs_bindex_t		bindex;
	unsigned int		flags;
	int			err;
};

static int fillvdir(struct dir_context *ctx, const char *__name, int nlen,
		    loff_t offset __maybe_unused, u64 h_ino,
		    unsigned int d_type)
{
	struct fillvdir_arg *arg = container_of(ctx, struct fillvdir_arg, ctx);
	char *name = (void *)__name;
	struct super_block *sb;
	ino_t ino;
	const unsigned char shwh = !!au_ftest_fillvdir(arg->flags, SHWH);

	arg->err = 0;
	sb = arg->file->f_path.dentry->d_sb;
	au_fset_fillvdir(arg->flags, CALLED);
	/* smp_mb(); */
	if (nlen <= AUFS_WH_PFX_LEN
	    || memcmp(name, AUFS_WH_PFX, AUFS_WH_PFX_LEN)) {
		if (test_known(&arg->delist, name, nlen)
		    || au_nhash_test_known_wh(&arg->whlist, name, nlen))
			goto out; /* already exists or whiteouted */

		arg->err = au_ino(sb, arg->bindex, h_ino, d_type, &ino);
		if (!arg->err) {
			if (unlikely(nlen > AUFS_MAX_NAMELEN))
				d_type = DT_UNKNOWN;
			arg->err = append_de(arg->vdir, name, nlen, ino,
					     d_type, &arg->delist);
		}
	} else if (au_ftest_fillvdir(arg->flags, WHABLE)) {
		name += AUFS_WH_PFX_LEN;
		nlen -= AUFS_WH_PFX_LEN;
		if (au_nhash_test_known_wh(&arg->whlist, name, nlen))
			goto out; /* already whiteouted */

		if (shwh)
			arg->err = au_wh_ino(sb, arg->bindex, h_ino, d_type,
					     &ino);
		if (!arg->err) {
			if (nlen <= AUFS_MAX_NAMELEN + AUFS_WH_PFX_LEN)
				d_type = DT_UNKNOWN;
			arg->err = au_nhash_append_wh
				(&arg->whlist, name, nlen, ino, d_type,
				 arg->bindex, shwh);
		}
	}

out:
	if (!arg->err)
		arg->vdir->vd_jiffy = jiffies;
	/* smp_mb(); */
	AuTraceErr(arg->err);
	return arg->err;
}

static int au_handle_shwh(struct super_block *sb, struct au_vdir *vdir,
			  struct au_nhash *whlist, struct au_nhash *delist)
{
#ifdef CONFIG_AUFS_SHWH
	int err;
	unsigned int nh, u;
	struct hlist_head *head;
	struct au_vdir_wh *pos;
	struct hlist_node *n;
	char *p, *o;
	struct au_vdir_destr *destr;

	AuDebugOn(!au_opt_test(au_mntflags(sb), SHWH));

	err = -ENOMEM;
	o = p = (void *)__get_free_page(GFP_NOFS);
	if (unlikely(!p))
		goto out;

	err = 0;
	nh = whlist->nh_num;
	memcpy(p, AUFS_WH_PFX, AUFS_WH_PFX_LEN);
	p += AUFS_WH_PFX_LEN;
	for (u = 0; u < nh; u++) {
		head = whlist->nh_head + u;
		hlist_for_each_entry_safe(pos, n, head, wh_hash) {
			destr = &pos->wh_str;
			memcpy(p, destr->name, destr->len);
			err = append_de(vdir, o, destr->len + AUFS_WH_PFX_LEN,
					pos->wh_ino, pos->wh_type, delist);
			if (unlikely(err))
				break;
		}
	}

	free_page((unsigned long)o);

out:
	AuTraceErr(err);
	return err;
#else
	return 0;
#endif
}

static int au_do_read_vdir(struct fillvdir_arg *arg)
{
	int err;
	unsigned int rdhash;
	loff_t offset;
	aufs_bindex_t bbot, bindex, btop;
	unsigned char shwh;
	struct file *hf, *file;
	struct super_block *sb;

	file = arg->file;
	sb = file->f_path.dentry->d_sb;
	SiMustAnyLock(sb);

	rdhash = au_sbi(sb)->si_rdhash;
	if (!rdhash)
		rdhash = au_rdhash_est(au_dir_size(file, /*dentry*/NULL));
	err = au_nhash_alloc(&arg->delist, rdhash, GFP_NOFS);
	if (unlikely(err))
		goto out;
	err = au_nhash_alloc(&arg->whlist, rdhash, GFP_NOFS);
	if (unlikely(err))
		goto out_delist;

	err = 0;
	arg->flags = 0;
	shwh = 0;
	if (au_opt_test(au_mntflags(sb), SHWH)) {
		shwh = 1;
		au_fset_fillvdir(arg->flags, SHWH);
	}
	btop = au_fbtop(file);
	bbot = au_fbbot_dir(file);
	for (bindex = btop; !err && bindex <= bbot; bindex++) {
		hf = au_hf_dir(file, bindex);
		if (!hf)
			continue;

		offset = vfsub_llseek(hf, 0, SEEK_SET);
		err = offset;
		if (unlikely(offset))
			break;

		arg->bindex = bindex;
		au_fclr_fillvdir(arg->flags, WHABLE);
		if (shwh
		    || (bindex != bbot
			&& au_br_whable(au_sbr_perm(sb, bindex))))
			au_fset_fillvdir(arg->flags, WHABLE);
		do {
			arg->err = 0;
			au_fclr_fillvdir(arg->flags, CALLED);
			/* smp_mb(); */
			err = vfsub_iterate_dir(hf, &arg->ctx);
			if (err >= 0)
				err = arg->err;
		} while (!err && au_ftest_fillvdir(arg->flags, CALLED));

		/*
		 * dir_relax() may be good for concurrency, but aufs should not
		 * use it since it will cause a lockdep problem.
		 */
	}

	if (!err && shwh)
		err = au_handle_shwh(sb, arg->vdir, &arg->whlist, &arg->delist);

	au_nhash_wh_free(&arg->whlist);

out_delist:
	au_nhash_de_free(&arg->delist);
out:
	return err;
}

static int read_vdir(struct file *file, int may_read)
{
	int err;
	unsigned long expire;
	unsigned char do_read;
	struct fillvdir_arg arg = {
		.ctx = {
			.actor = fillvdir
		}
	};
	struct inode *inode;
	struct au_vdir *vdir, *allocated;

	err = 0;
	inode = file_inode(file);
	IMustLock(inode);
	IiMustWriteLock(inode);
	SiMustAnyLock(inode->i_sb);

	allocated = NULL;
	do_read = 0;
	expire = au_sbi(inode->i_sb)->si_rdcache;
	vdir = au_ivdir(inode);
	if (!vdir) {
		do_read = 1;
		vdir = alloc_vdir(file);
		err = PTR_ERR(vdir);
		if (IS_ERR(vdir))
			goto out;
		err = 0;
		allocated = vdir;
	} else if (may_read
		   && (inode->i_version != vdir->vd_version
		       || time_after(jiffies, vdir->vd_jiffy + expire))) {
		do_read = 1;
		err = reinit_vdir(vdir);
		if (unlikely(err))
			goto out;
	}

	if (!do_read)
		return 0; /* success */

	arg.file = file;
	arg.vdir = vdir;
	err = au_do_read_vdir(&arg);
	if (!err) {
		/* file->f_pos = 0; */ /* todo: ctx->pos? */
		vdir->vd_version = inode->i_version;
		vdir->vd_last.ul = 0;
		vdir->vd_last.p.deblk = vdir->vd_deblk[0];
		if (allocated)
			au_set_ivdir(inode, allocated);
	} else if (allocated)
		au_vdir_free(allocated);

out:
	return err;
}

static int copy_vdir(struct au_vdir *tgt, struct au_vdir *src)
{
	int err, rerr;
	unsigned long ul, n;
	const unsigned int deblk_sz = src->vd_deblk_sz;

	AuDebugOn(tgt->vd_nblk != 1);

	err = -ENOMEM;
	if (tgt->vd_nblk < src->vd_nblk) {
		unsigned char **p;

		p = au_krealloc(tgt->vd_deblk, sizeof(*p) * src->vd_nblk,
				GFP_NOFS, /*may_shrink*/0);
		if (unlikely(!p))
			goto out;
		tgt->vd_deblk = p;
	}

	if (tgt->vd_deblk_sz != deblk_sz) {
		unsigned char *p;

		tgt->vd_deblk_sz = deblk_sz;
		p = au_krealloc(tgt->vd_deblk[0], deblk_sz, GFP_NOFS,
				/*may_shrink*/1);
		if (unlikely(!p))
			goto out;
		tgt->vd_deblk[0] = p;
	}
	memcpy(tgt->vd_deblk[0], src->vd_deblk[0], deblk_sz);
	tgt->vd_version = src->vd_version;
	tgt->vd_jiffy = src->vd_jiffy;

	n = src->vd_nblk;
	for (ul = 1; ul < n; ul++) {
		tgt->vd_deblk[ul] = kmemdup(src->vd_deblk[ul], deblk_sz,
					    GFP_NOFS);
		if (unlikely(!tgt->vd_deblk[ul]))
			goto out;
		tgt->vd_nblk++;
	}
	tgt->vd_nblk = n;
	tgt->vd_last.ul = tgt->vd_last.ul;
	tgt->vd_last.p.deblk = tgt->vd_deblk[tgt->vd_last.ul];
	tgt->vd_last.p.deblk += src->vd_last.p.deblk
		- src->vd_deblk[src->vd_last.ul];
	/* smp_mb(); */
	return 0; /* success */

out:
	rerr = reinit_vdir(tgt);
	BUG_ON(rerr);
	return err;
}

int au_vdir_init(struct file *file)
{
	int err;
	struct inode *inode;
	struct au_vdir *vdir_cache, *allocated;

	/* test file->f_pos here instead of ctx->pos */
	err = read_vdir(file, !file->f_pos);
	if (unlikely(err))
		goto out;

	allocated = NULL;
	vdir_cache = au_fvdir_cache(file);
	if (!vdir_cache) {
		vdir_cache = alloc_vdir(file);
		err = PTR_ERR(vdir_cache);
		if (IS_ERR(vdir_cache))
			goto out;
		allocated = vdir_cache;
	} else if (!file->f_pos && vdir_cache->vd_version != file->f_version) {
		/* test file->f_pos here instead of ctx->pos */
		err = reinit_vdir(vdir_cache);
		if (unlikely(err))
			goto out;
	} else
		return 0; /* success */

	inode = file_inode(file);
	err = copy_vdir(vdir_cache, au_ivdir(inode));
	if (!err) {
		file->f_version = inode->i_version;
		if (allocated)
			au_set_fvdir_cache(file, allocated);
	} else if (allocated)
		au_vdir_free(allocated);

out:
	return err;
}

static loff_t calc_offset(struct au_vdir *vdir)
{
	loff_t offset;
	union au_vdir_deblk_p p;

	p.deblk = vdir->vd_deblk[vdir->vd_last.ul];
	offset = vdir->vd_last.p.deblk - p.deblk;
	offset += vdir->vd_deblk_sz * vdir->vd_last.ul;
	return offset;
}

/* returns true or false */
static int seek_vdir(struct file *file, struct dir_context *ctx)
{
	int valid;
	unsigned int deblk_sz;
	unsigned long ul, n;
	loff_t offset;
	union au_vdir_deblk_p p, deblk_end;
	struct au_vdir *vdir_cache;

	valid = 1;
	vdir_cache = au_fvdir_cache(file);
	offset = calc_offset(vdir_cache);
	AuDbg("offset %lld\n", offset);
	if (ctx->pos == offset)
		goto out;

	vdir_cache->vd_last.ul = 0;
	vdir_cache->vd_last.p.deblk = vdir_cache->vd_deblk[0];
	if (!ctx->pos)
		goto out;

	valid = 0;
	deblk_sz = vdir_cache->vd_deblk_sz;
	ul = div64_u64(ctx->pos, deblk_sz);
	AuDbg("ul %lu\n", ul);
	if (ul >= vdir_cache->vd_nblk)
		goto out;

	n = vdir_cache->vd_nblk;
	for (; ul < n; ul++) {
		p.deblk = vdir_cache->vd_deblk[ul];
		deblk_end.deblk = p.deblk + deblk_sz;
		offset = ul;
		offset *= deblk_sz;
		while (!is_deblk_end(&p, &deblk_end) && offset < ctx->pos) {
			unsigned int l;

			l = calc_size(p.de->de_str.len);
			offset += l;
			p.deblk += l;
		}
		if (!is_deblk_end(&p, &deblk_end)) {
			valid = 1;
			vdir_cache->vd_last.ul = ul;
			vdir_cache->vd_last.p = p;
			break;
		}
	}

out:
	/* smp_mb(); */
	AuTraceErr(!valid);
	return valid;
}

int au_vdir_fill_de(struct file *file, struct dir_context *ctx)
{
	unsigned int l, deblk_sz;
	union au_vdir_deblk_p deblk_end;
	struct au_vdir *vdir_cache;
	struct au_vdir_de *de;

	vdir_cache = au_fvdir_cache(file);
	if (!seek_vdir(file, ctx))
		return 0;

	deblk_sz = vdir_cache->vd_deblk_sz;
	while (1) {
		deblk_end.deblk = vdir_cache->vd_deblk[vdir_cache->vd_last.ul];
		deblk_end.deblk += deblk_sz;
		while (!is_deblk_end(&vdir_cache->vd_last.p, &deblk_end)) {
			de = vdir_cache->vd_last.p.de;
			AuDbg("%.*s, off%lld, i%lu, dt%d\n",
			      de->de_str.len, de->de_str.name, ctx->pos,
			      (unsigned long)de->de_ino, de->de_type);
			if (unlikely(!dir_emit(ctx, de->de_str.name,
					       de->de_str.len, de->de_ino,
					       de->de_type))) {
				/* todo: ignore the error caused by udba? */
				/* return err; */
				return 0;
			}

			l = calc_size(de->de_str.len);
			vdir_cache->vd_last.p.deblk += l;
			ctx->pos += l;
		}
		if (vdir_cache->vd_last.ul < vdir_cache->vd_nblk - 1) {
			vdir_cache->vd_last.ul++;
			vdir_cache->vd_last.p.deblk
				= vdir_cache->vd_deblk[vdir_cache->vd_last.ul];
			ctx->pos = deblk_sz * vdir_cache->vd_last.ul;
			continue;
		}
		break;
	}

	/* smp_mb(); */
	return 0;
}
