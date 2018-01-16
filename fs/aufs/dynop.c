/*
 * Copyright (C) 2010-2017 Junjiro R. Okajima
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
 * dynamically customizable operations for regular files
 */

#include "aufs.h"

#define DyPrSym(key)	AuDbgSym(key->dk_op.dy_hop)

/*
 * How large will these lists be?
 * Usually just a few elements, 20-30 at most for each, I guess.
 */
static struct hlist_bl_head dynop[AuDyLast];

static struct au_dykey *dy_gfind_get(struct hlist_bl_head *hbl,
				     const void *h_op)
{
	struct au_dykey *key, *tmp;
	struct hlist_bl_node *pos;

	key = NULL;
	hlist_bl_lock(hbl);
	hlist_bl_for_each_entry(tmp, pos, hbl, dk_hnode)
		if (tmp->dk_op.dy_hop == h_op) {
			key = tmp;
			kref_get(&key->dk_kref);
			break;
		}
	hlist_bl_unlock(hbl);

	return key;
}

static struct au_dykey *dy_bradd(struct au_branch *br, struct au_dykey *key)
{
	struct au_dykey **k, *found;
	const void *h_op = key->dk_op.dy_hop;
	int i;

	found = NULL;
	k = br->br_dykey;
	for (i = 0; i < AuBrDynOp; i++)
		if (k[i]) {
			if (k[i]->dk_op.dy_hop == h_op) {
				found = k[i];
				break;
			}
		} else
			break;
	if (!found) {
		spin_lock(&br->br_dykey_lock);
		for (; i < AuBrDynOp; i++)
			if (k[i]) {
				if (k[i]->dk_op.dy_hop == h_op) {
					found = k[i];
					break;
				}
			} else {
				k[i] = key;
				break;
			}
		spin_unlock(&br->br_dykey_lock);
		BUG_ON(i == AuBrDynOp); /* expand the array */
	}

	return found;
}

/* kref_get() if @key is already added */
static struct au_dykey *dy_gadd(struct hlist_bl_head *hbl, struct au_dykey *key)
{
	struct au_dykey *tmp, *found;
	struct hlist_bl_node *pos;
	const void *h_op = key->dk_op.dy_hop;

	found = NULL;
	hlist_bl_lock(hbl);
	hlist_bl_for_each_entry(tmp, pos, hbl, dk_hnode)
		if (tmp->dk_op.dy_hop == h_op) {
			kref_get(&tmp->dk_kref);
			found = tmp;
			break;
		}
	if (!found)
		hlist_bl_add_head(&key->dk_hnode, hbl);
	hlist_bl_unlock(hbl);

	if (!found)
		DyPrSym(key);
	return found;
}

static void dy_free_rcu(struct rcu_head *rcu)
{
	struct au_dykey *key;

	key = container_of(rcu, struct au_dykey, dk_rcu);
	DyPrSym(key);
	kfree(key);
}

static void dy_free(struct kref *kref)
{
	struct au_dykey *key;
	struct hlist_bl_head *hbl;

	key = container_of(kref, struct au_dykey, dk_kref);
	hbl = dynop + key->dk_op.dy_type;
	au_hbl_del(&key->dk_hnode, hbl);
	call_rcu(&key->dk_rcu, dy_free_rcu);
}

void au_dy_put(struct au_dykey *key)
{
	kref_put(&key->dk_kref, dy_free);
}

/* ---------------------------------------------------------------------- */

#define DyDbgSize(cnt, op)	AuDebugOn(cnt != sizeof(op)/sizeof(void *))

#ifdef CONFIG_AUFS_DEBUG
#define DyDbgDeclare(cnt)	unsigned int cnt = 0
#define DyDbgInc(cnt)		do { cnt++; } while (0)
#else
#define DyDbgDeclare(cnt)	do {} while (0)
#define DyDbgInc(cnt)		do {} while (0)
#endif

#define DySet(func, dst, src, h_op, h_sb) do {				\
	DyDbgInc(cnt);							\
	if (h_op->func) {						\
		if (src.func)						\
			dst.func = src.func;				\
		else							\
			AuDbg("%s %s\n", au_sbtype(h_sb), #func);	\
	}								\
} while (0)

#define DySetForce(func, dst, src) do {		\
	AuDebugOn(!src.func);			\
	DyDbgInc(cnt);				\
	dst.func = src.func;			\
} while (0)

#define DySetAop(func) \
	DySet(func, dyaop->da_op, aufs_aop, h_aop, h_sb)
#define DySetAopForce(func) \
	DySetForce(func, dyaop->da_op, aufs_aop)

static void dy_aop(struct au_dykey *key, const void *h_op,
		   struct super_block *h_sb __maybe_unused)
{
	struct au_dyaop *dyaop = (void *)key;
	const struct address_space_operations *h_aop = h_op;
	DyDbgDeclare(cnt);

	AuDbg("%s\n", au_sbtype(h_sb));

	DySetAop(writepage);
	DySetAopForce(readpage);	/* force */
	DySetAop(writepages);
	DySetAop(set_page_dirty);
	DySetAop(readpages);
	DySetAop(write_begin);
	DySetAop(write_end);
	DySetAop(bmap);
	DySetAop(invalidatepage);
	DySetAop(releasepage);
	DySetAop(freepage);
	/* this one will be changed according to an aufs mount option */
	DySetAop(direct_IO);
	DySetAop(migratepage);
	DySetAop(isolate_page);
	DySetAop(putback_page);
	DySetAop(launder_page);
	DySetAop(is_partially_uptodate);
	DySetAop(is_dirty_writeback);
	DySetAop(error_remove_page);
	DySetAop(swap_activate);
	DySetAop(swap_deactivate);

	DyDbgSize(cnt, *h_aop);
}

/* ---------------------------------------------------------------------- */

static void dy_bug(struct kref *kref)
{
	BUG();
}

static struct au_dykey *dy_get(struct au_dynop *op, struct au_branch *br)
{
	struct au_dykey *key, *old;
	struct hlist_bl_head *hbl;
	struct op {
		unsigned int sz;
		void (*set)(struct au_dykey *key, const void *h_op,
			    struct super_block *h_sb __maybe_unused);
	};
	static const struct op a[] = {
		[AuDy_AOP] = {
			.sz	= sizeof(struct au_dyaop),
			.set	= dy_aop
		}
	};
	const struct op *p;

	hbl = dynop + op->dy_type;
	key = dy_gfind_get(hbl, op->dy_hop);
	if (key)
		goto out_add; /* success */

	p = a + op->dy_type;
	key = kzalloc(p->sz, GFP_NOFS);
	if (unlikely(!key)) {
		key = ERR_PTR(-ENOMEM);
		goto out;
	}

	key->dk_op.dy_hop = op->dy_hop;
	kref_init(&key->dk_kref);
	p->set(key, op->dy_hop, au_br_sb(br));
	old = dy_gadd(hbl, key);
	if (old) {
		kfree(key);
		key = old;
	}

out_add:
	old = dy_bradd(br, key);
	if (old)
		/* its ref-count should never be zero here */
		kref_put(&key->dk_kref, dy_bug);
out:
	return key;
}

/* ---------------------------------------------------------------------- */
/*
 * Aufs prohibits O_DIRECT by defaut even if the branch supports it.
 * This behaviour is necessary to return an error from open(O_DIRECT) instead
 * of the succeeding I/O. The dio mount option enables O_DIRECT and makes
 * open(O_DIRECT) always succeed, but the succeeding I/O may return an error.
 * See the aufs manual in detail.
 */
static void dy_adx(struct au_dyaop *dyaop, int do_dx)
{
	if (!do_dx)
		dyaop->da_op.direct_IO = NULL;
	else
		dyaop->da_op.direct_IO = aufs_aop.direct_IO;
}

static struct au_dyaop *dy_aget(struct au_branch *br,
				const struct address_space_operations *h_aop,
				int do_dx)
{
	struct au_dyaop *dyaop;
	struct au_dynop op;

	op.dy_type = AuDy_AOP;
	op.dy_haop = h_aop;
	dyaop = (void *)dy_get(&op, br);
	if (IS_ERR(dyaop))
		goto out;
	dy_adx(dyaop, do_dx);

out:
	return dyaop;
}

int au_dy_iaop(struct inode *inode, aufs_bindex_t bindex,
		struct inode *h_inode)
{
	int err, do_dx;
	struct super_block *sb;
	struct au_branch *br;
	struct au_dyaop *dyaop;

	AuDebugOn(!S_ISREG(h_inode->i_mode));
	IiMustWriteLock(inode);

	sb = inode->i_sb;
	br = au_sbr(sb, bindex);
	do_dx = !!au_opt_test(au_mntflags(sb), DIO);
	dyaop = dy_aget(br, h_inode->i_mapping->a_ops, do_dx);
	err = PTR_ERR(dyaop);
	if (IS_ERR(dyaop))
		/* unnecessary to call dy_fput() */
		goto out;

	err = 0;
	inode->i_mapping->a_ops = &dyaop->da_op;

out:
	return err;
}

/*
 * Is it safe to replace a_ops during the inode/file is in operation?
 * Yes, I hope so.
 */
int au_dy_irefresh(struct inode *inode)
{
	int err;
	aufs_bindex_t btop;
	struct inode *h_inode;

	err = 0;
	if (S_ISREG(inode->i_mode)) {
		btop = au_ibtop(inode);
		h_inode = au_h_iptr(inode, btop);
		err = au_dy_iaop(inode, btop, h_inode);
	}
	return err;
}

void au_dy_arefresh(int do_dx)
{
	struct hlist_bl_head *hbl;
	struct hlist_bl_node *pos;
	struct au_dykey *key;

	hbl = dynop + AuDy_AOP;
	hlist_bl_lock(hbl);
	hlist_bl_for_each_entry(key, pos, hbl, dk_hnode)
		dy_adx((void *)key, do_dx);
	hlist_bl_unlock(hbl);
}

/* ---------------------------------------------------------------------- */

void __init au_dy_init(void)
{
	int i;

	/* make sure that 'struct au_dykey *' can be any type */
	BUILD_BUG_ON(offsetof(struct au_dyaop, da_key));

	for (i = 0; i < AuDyLast; i++)
		INIT_HLIST_BL_HEAD(dynop + i);
}

void au_dy_fin(void)
{
	int i;

	for (i = 0; i < AuDyLast; i++)
		WARN_ON(!hlist_bl_empty(dynop + i));
}
