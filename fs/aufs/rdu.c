/*
 * Copyright (C) 2005-2011 Junjiro R. Okajima
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
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA
 */

/*
 * readdir in userspace.
 */

#include <linux/compat.h>
#include <linux/fs_stack.h>
#include <linux/security.h>
#include <linux/uaccess.h>
#include <linux/aufs_type.h>
#include "aufs.h"

/* bits for struct aufs_rdu.flags */
#define	AuRdu_CALLED	1
#define	AuRdu_CONT	(1 << 1)
#define	AuRdu_FULL	(1 << 2)
#define au_ftest_rdu(flags, name)	((flags) & AuRdu_##name)
#define au_fset_rdu(flags, name) \
	do { (flags) |= AuRdu_##name; } while (0)
#define au_fclr_rdu(flags, name) \
	do { (flags) &= ~AuRdu_##name; } while (0)

struct au_rdu_arg {
	struct aufs_rdu			*rdu;
	union au_rdu_ent_ul		ent;
	unsigned long			end;

	struct super_block		*sb;
	int				err;
};

static int au_rdu_fill(void *__arg, const char *name, int nlen,
		       loff_t offset, u64 h_ino, unsigned int d_type)
{
	int err, len;
	struct au_rdu_arg *arg = __arg;
	struct aufs_rdu *rdu = arg->rdu;
	struct au_rdu_ent ent;

	err = 0;
	arg->err = 0;
	au_fset_rdu(rdu->cookie.flags, CALLED);
	len = au_rdu_len(nlen);
	if (arg->ent.ul + len  < arg->end) {
		ent.ino = h_ino;
		ent.bindex = rdu->cookie.bindex;
		ent.type = d_type;
		ent.nlen = nlen;
		if (unlikely(nlen > AUFS_MAX_NAMELEN))
			ent.type = DT_UNKNOWN;

		err = -EFAULT;
		if (copy_to_user(arg->ent.e, &ent, sizeof(ent)))
			goto out;
		if (copy_to_user(arg->ent.e->name, name, nlen))
			goto out;
		/* the terminating NULL */
		if (__put_user(0, arg->ent.e->name + nlen))
			goto out;
		err = 0;
		/* AuDbg("%p, %.*s\n", arg->ent.p, nlen, name); */
		arg->ent.ul += len;
		rdu->rent++;
	} else {
		err = -EFAULT;
		au_fset_rdu(rdu->cookie.flags, FULL);
		rdu->full = 1;
		rdu->tail = arg->ent;
	}

out:
	/* AuTraceErr(err); */
	return err;
}

static int au_rdu_do(struct file *h_file, struct au_rdu_arg *arg)
{
	int err;
	loff_t offset;
	struct au_rdu_cookie *cookie = &arg->rdu->cookie;

	offset = vfsub_llseek(h_file, cookie->h_pos, SEEK_SET);
	err = offset;
	if (unlikely(offset != cookie->h_pos))
		goto out;

	err = 0;
	do {
		arg->err = 0;
		au_fclr_rdu(cookie->flags, CALLED);
		/* smp_mb(); */
		err = vfsub_readdir(h_file, au_rdu_fill, arg);
		if (err >= 0)
			err = arg->err;
	} while (!err
		 && au_ftest_rdu(cookie->flags, CALLED)
		 && !au_ftest_rdu(cookie->flags, FULL));
	cookie->h_pos = h_file->f_pos;

out:
	AuTraceErr(err);
	return err;
}

static int au_rdu(struct file *file, struct aufs_rdu *rdu)
{
	int err;
	aufs_bindex_t bend;
	struct au_rdu_arg arg;
	struct dentry *dentry;
	struct inode *inode;
	struct file *h_file;
	struct au_rdu_cookie *cookie = &rdu->cookie;

	err = !access_ok(VERIFY_WRITE, rdu->ent.e, rdu->sz);
	if (unlikely(err)) {
		err = -EFAULT;
		AuTraceErr(err);
		goto out;
	}
	rdu->rent = 0;
	rdu->tail = rdu->ent;
	rdu->full = 0;
	arg.rdu = rdu;
	arg.ent = rdu->ent;
	arg.end = arg.ent.ul;
	arg.end += rdu->sz;

	err = -ENOTDIR;
	if (unlikely(!file->f_op || !file->f_op->readdir))
		goto out;

	err = security_file_permission(file, MAY_READ);
	AuTraceErr(err);
	if (unlikely(err))
		goto out;

	dentry = file->f_dentry;
	inode = dentry->d_inode;
#if 1
	mutex_lock(&inode->i_mutex);
#else
	err = mutex_lock_killable(&inode->i_mutex);
	AuTraceErr(err);
	if (unlikely(err))
		goto out;
#endif

	arg.sb = inode->i_sb;
	err = si_read_lock(arg.sb, AuLock_FLUSH | AuLock_NOPLM);
	if (unlikely(err))
		goto out_mtx;
	err = au_alive_dir(dentry);
	if (unlikely(err))
		goto out_si;
	/* todo: reval? */
	fi_read_lock(file);

	err = -EAGAIN;
	if (unlikely(au_ftest_rdu(cookie->flags, CONT)
		     && cookie->generation != au_figen(file)))
		goto out_unlock;

	err = 0;
	if (!rdu->blk) {
		rdu->blk = au_sbi(arg.sb)->si_rdblk;
		if (!rdu->blk)
			rdu->blk = au_dir_size(file, /*dentry*/NULL);
	}
	bend = au_fbstart(file);
	if (cookie->bindex < bend)
		cookie->bindex = bend;
	bend = au_fbend_dir(file);
	/* AuDbg("b%d, b%d\n", cookie->bindex, bend); */
	for (; !err && cookie->bindex <= bend;
	     cookie->bindex++, cookie->h_pos = 0) {
		h_file = au_hf_dir(file, cookie->bindex);
		if (!h_file)
			continue;

		au_fclr_rdu(cookie->flags, FULL);
		err = au_rdu_do(h_file, &arg);
		AuTraceErr(err);
		if (unlikely(au_ftest_rdu(cookie->flags, FULL) || err))
			break;
	}
	AuDbg("rent %llu\n", rdu->rent);

	if (!err && !au_ftest_rdu(cookie->flags, CONT)) {
		rdu->shwh = !!au_opt_test(au_sbi(arg.sb)->si_mntflags, SHWH);
		au_fset_rdu(cookie->flags, CONT);
		cookie->generation = au_figen(file);
	}

	ii_read_lock_child(inode);
	fsstack_copy_attr_atime(inode, au_h_iptr(inode, au_ibstart(inode)));
	ii_read_unlock(inode);

out_unlock:
	fi_read_unlock(file);
out_si:
	si_read_unlock(arg.sb);
out_mtx:
	mutex_unlock(&inode->i_mutex);
out:
	AuTraceErr(err);
	return err;
}

static int au_rdu_ino(struct file *file, struct aufs_rdu *rdu)
{
	int err;
	ino_t ino;
	unsigned long long nent;
	union au_rdu_ent_ul *u;
	struct au_rdu_ent ent;
	struct super_block *sb;

	err = 0;
	nent = rdu->nent;
	u = &rdu->ent;
	sb = file->f_dentry->d_sb;
	si_read_lock(sb, AuLock_FLUSH);
	while (nent-- > 0) {
		err = copy_from_user(&ent, u->e, sizeof(ent));
		if (!err)
			err = !access_ok(VERIFY_WRITE, &u->e->ino, sizeof(ino));
		if (unlikely(err)) {
			err = -EFAULT;
			AuTraceErr(err);
			break;
		}

		/* AuDbg("b%d, i%llu\n", ent.bindex, ent.ino); */
		if (!ent.wh)
			err = au_ino(sb, ent.bindex, ent.ino, ent.type, &ino);
		else
			err = au_wh_ino(sb, ent.bindex, ent.ino, ent.type,
					&ino);
		if (unlikely(err)) {
			AuTraceErr(err);
			break;
		}

		err = __put_user(ino, &u->e->ino);
		if (unlikely(err)) {
			err = -EFAULT;
			AuTraceErr(err);
			break;
		}
		u->ul += au_rdu_len(ent.nlen);
	}
	si_read_unlock(sb);

	return err;
}

/* ---------------------------------------------------------------------- */

static int au_rdu_verify(struct aufs_rdu *rdu)
{
	AuDbg("rdu{%llu, %p, %u | %u | %llu, %u, %u | "
	      "%llu, b%d, 0x%x, g%u}\n",
	      rdu->sz, rdu->ent.e, rdu->verify[AufsCtlRduV_SZ],
	      rdu->blk,
	      rdu->rent, rdu->shwh, rdu->full,
	      rdu->cookie.h_pos, rdu->cookie.bindex, rdu->cookie.flags,
	      rdu->cookie.generation);

	if (rdu->verify[AufsCtlRduV_SZ] == sizeof(*rdu))
		return 0;

	AuDbg("%u:%u\n",
	      rdu->verify[AufsCtlRduV_SZ], (unsigned int)sizeof(*rdu));
	return -EINVAL;
}

long au_rdu_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long err, e;
	struct aufs_rdu rdu;
	void __user *p = (void __user *)arg;

	err = copy_from_user(&rdu, p, sizeof(rdu));
	if (unlikely(err)) {
		err = -EFAULT;
		AuTraceErr(err);
		goto out;
	}
	err = au_rdu_verify(&rdu);
	if (unlikely(err))
		goto out;

	switch (cmd) {
	case AUFS_CTL_RDU:
		err = au_rdu(file, &rdu);
		if (unlikely(err))
			break;

		e = copy_to_user(p, &rdu, sizeof(rdu));
		if (unlikely(e)) {
			err = -EFAULT;
			AuTraceErr(err);
		}
		break;
	case AUFS_CTL_RDU_INO:
		err = au_rdu_ino(file, &rdu);
		break;

	default:
		/* err = -ENOTTY; */
		err = -EINVAL;
	}

out:
	AuTraceErr(err);
	return err;
}

#ifdef CONFIG_COMPAT
long au_rdu_compat_ioctl(struct file *file, unsigned int cmd, unsigned long arg)
{
	long err, e;
	struct aufs_rdu rdu;
	void __user *p = compat_ptr(arg);

	/* todo: get_user()? */
	err = copy_from_user(&rdu, p, sizeof(rdu));
	if (unlikely(err)) {
		err = -EFAULT;
		AuTraceErr(err);
		goto out;
	}
	rdu.ent.e = compat_ptr(rdu.ent.ul);
	err = au_rdu_verify(&rdu);
	if (unlikely(err))
		goto out;

	switch (cmd) {
	case AUFS_CTL_RDU:
		err = au_rdu(file, &rdu);
		if (unlikely(err))
			break;

		rdu.ent.ul = ptr_to_compat(rdu.ent.e);
		rdu.tail.ul = ptr_to_compat(rdu.tail.e);
		e = copy_to_user(p, &rdu, sizeof(rdu));
		if (unlikely(e)) {
			err = -EFAULT;
			AuTraceErr(err);
		}
		break;
	case AUFS_CTL_RDU_INO:
		err = au_rdu_ino(file, &rdu);
		break;

	default:
		/* err = -ENOTTY; */
		err = -EINVAL;
	}

out:
	AuTraceErr(err);
	return err;
}
#endif
