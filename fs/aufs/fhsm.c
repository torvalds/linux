/*
 * Copyright (C) 2011-2014 Junjiro R. Okajima
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
 * File-based Hierarchy Storage Management
 */

#include <linux/anon_inodes.h>
#include <linux/poll.h>
#include <linux/seq_file.h>
#include "aufs.h"

static unsigned int au_fhsm_poll(struct file *file,
				 struct poll_table_struct *wait)
{
	unsigned int mask;
	struct au_sbinfo *sbinfo;
	struct au_fhsm *fhsm;

	mask = 0;
	sbinfo = file->private_data;
	fhsm = &sbinfo->si_fhsm;
	poll_wait(file, &fhsm->fhsm_wqh, wait);
	if (atomic_read(&fhsm->fhsm_readable))
		mask = POLLIN /* | POLLRDNORM */;

	AuTraceErr((int)mask);
	return mask;
}

static int au_fhsm_do_read_one(struct aufs_stbr __user *stbr,
			      struct aufs_stfs *stfs, __s16 brid)
{
	int err;

	err = copy_to_user(&stbr->stfs, stfs, sizeof(*stfs));
	if (!err)
		err = __put_user(brid, &stbr->brid);
	if (unlikely(err))
		err = -EFAULT;

	return err;
}

static ssize_t au_fhsm_do_read(struct super_block *sb,
			       struct aufs_stbr __user *stbr, size_t count)
{
	ssize_t err;
	int nstbr;
	aufs_bindex_t bindex, bend;
	struct au_branch *br;
	struct au_wbr *wbr;

	/* except the bottom branch */
	err = 0;
	nstbr = 0;
	bend = au_sbend(sb);
	for (bindex = 0; !err && bindex < bend; bindex++) {
		br = au_sbr(sb, bindex);
		if (!au_br_fhsm(br->br_perm))
			continue;

		wbr = br->br_wbr;
		mutex_lock(&wbr->wbr_fhsm_notify.lock);
		if (wbr->wbr_fhsm_notify.readable) {
			err = -EFAULT;
			if (count >= sizeof(*stbr))
				err = au_fhsm_do_read_one
					(stbr++, &wbr->wbr_fhsm_notify.stfs,
					 br->br_id);
			if (!err) {
				wbr->wbr_fhsm_notify.readable = 0;
				count -= sizeof(*stbr);
				nstbr++;
			}
		}
		mutex_unlock(&wbr->wbr_fhsm_notify.lock);
	}
	if (!err)
		err = sizeof(*stbr) * nstbr;

	return err;
}

static ssize_t au_fhsm_read(struct file *file, char __user *buf, size_t count,
			   loff_t *pos)
{
	ssize_t err;
	int readable;
	aufs_bindex_t nfhsm, bindex, bend;
	struct au_sbinfo *sbinfo;
	struct au_fhsm *fhsm;
	struct au_branch *br;
	struct super_block *sb;

	err = 0;
	sbinfo = file->private_data;
	fhsm = &sbinfo->si_fhsm;
need_data:
	spin_lock_irq(&fhsm->fhsm_wqh.lock);
	if (!atomic_read(&fhsm->fhsm_readable)) {
		if (vfsub_file_flags(file) & O_NONBLOCK)
			err = -EAGAIN;
		else
			err = wait_event_interruptible_locked_irq
				(fhsm->fhsm_wqh,
				 atomic_read(&fhsm->fhsm_readable));
	}
	spin_unlock_irq(&fhsm->fhsm_wqh.lock);
	if (unlikely(err))
		goto out;

	/* sb may already be dead */
	au_rw_read_lock(&sbinfo->si_rwsem);
	readable = atomic_read(&fhsm->fhsm_readable);
	if (readable > 0) {
		sb = sbinfo->si_sb;
		AuDebugOn(!sb);
		/* exclude the bottom branch */
		nfhsm = 0;
		bend = au_sbend(sb);
		for (bindex = 0; bindex < bend; bindex++) {
			br = au_sbr(sb, bindex);
			if (au_br_fhsm(br->br_perm))
				nfhsm++;
		}
		err = -EMSGSIZE;
		if (nfhsm * sizeof(struct aufs_stbr) <= count) {
			atomic_set(&fhsm->fhsm_readable, 0);
			err = au_fhsm_do_read(sbinfo->si_sb, (void __user *)buf,
					     count);
		}
	}
	au_rw_read_unlock(&sbinfo->si_rwsem);
	if (!readable)
		goto need_data;

out:
	return err;
}

static int au_fhsm_release(struct inode *inode, struct file *file)
{
	struct au_sbinfo *sbinfo;
	struct au_fhsm *fhsm;

	/* sb may already be dead */
	sbinfo = file->private_data;
	fhsm = &sbinfo->si_fhsm;
	spin_lock(&fhsm->fhsm_spin);
	fhsm->fhsm_pid = 0;
	spin_unlock(&fhsm->fhsm_spin);
	kobject_put(&sbinfo->si_kobj);

	return 0;
}

static const struct file_operations au_fhsm_fops = {
	.owner		= THIS_MODULE,
	.llseek		= noop_llseek,
	.read		= au_fhsm_read,
	.poll		= au_fhsm_poll,
	.release	= au_fhsm_release
};

int au_fhsm_fd(struct super_block *sb, int oflags)
{
	int err, fd;
	struct au_sbinfo *sbinfo;
	struct au_fhsm *fhsm;

	err = -EPERM;
	if (unlikely(!capable(CAP_SYS_ADMIN)))
		goto out;

	err = -EINVAL;
	if (unlikely(oflags & ~(O_CLOEXEC | O_NONBLOCK)))
		goto out;

	err = 0;
	sbinfo = au_sbi(sb);
	fhsm = &sbinfo->si_fhsm;
	spin_lock(&fhsm->fhsm_spin);
	if (!fhsm->fhsm_pid)
		fhsm->fhsm_pid = current->pid;
	else
		err = -EBUSY;
	spin_unlock(&fhsm->fhsm_spin);
	if (unlikely(err))
		goto out;

	oflags |= O_RDONLY;
	/* oflags |= FMODE_NONOTIFY; */
	fd = anon_inode_getfd("[aufs_fhsm]", &au_fhsm_fops, sbinfo, oflags);
	err = fd;
	if (unlikely(fd < 0))
		goto out_pid;

	/* succeed reglardless 'fhsm' status */
	kobject_get(&sbinfo->si_kobj);
	goto out; /* success */

out_pid:
	spin_lock(&fhsm->fhsm_spin);
	fhsm->fhsm_pid = 0;
	spin_unlock(&fhsm->fhsm_spin);
out:
	AuTraceErr(err);
	return err;
}

/* ---------------------------------------------------------------------- */

void au_fhsm_init(struct au_sbinfo *sbinfo)
{
	struct au_fhsm *fhsm;

	fhsm = &sbinfo->si_fhsm;
	spin_lock_init(&fhsm->fhsm_spin);
	init_waitqueue_head(&fhsm->fhsm_wqh);
	atomic_set(&fhsm->fhsm_readable, 0);
	fhsm->fhsm_expire
		= msecs_to_jiffies(AUFS_FHSM_CACHE_DEF_SEC * MSEC_PER_SEC);
}

void au_fhsm_set(struct au_sbinfo *sbinfo, unsigned int sec)
{
	sbinfo->si_fhsm.fhsm_expire
		= msecs_to_jiffies(sec * MSEC_PER_SEC);
}

void au_fhsm_show(struct seq_file *seq, struct au_sbinfo *sbinfo)
{
	unsigned int u;

	if (!au_ftest_si(sbinfo, FHSM))
		return;

	u = jiffies_to_msecs(sbinfo->si_fhsm.fhsm_expire) / MSEC_PER_SEC;
	if (u != AUFS_FHSM_CACHE_DEF_SEC)
		seq_printf(seq, ",fhsm_sec=%u", u);
}
