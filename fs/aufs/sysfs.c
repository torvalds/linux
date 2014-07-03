/*
 * Copyright (C) 2005-2014 Junjiro R. Okajima
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
 * sysfs interface
 */

#include <linux/seq_file.h>
#include "aufs.h"

static struct attribute *au_attr[] = {
	NULL,	/* need to NULL terminate the list of attributes */
};

static struct attribute_group sysaufs_attr_group_body = {
	.attrs = au_attr
};

struct attribute_group *sysaufs_attr_group = &sysaufs_attr_group_body;

/* ---------------------------------------------------------------------- */

int sysaufs_si_xi_path(struct seq_file *seq, struct super_block *sb)
{
	int err;

	SiMustAnyLock(sb);

	err = 0;
	if (au_opt_test(au_mntflags(sb), XINO)) {
		err = au_xino_path(seq, au_sbi(sb)->si_xib);
		seq_putc(seq, '\n');
	}
	return err;
}

/*
 * the lifetime of branch is independent from the entry under sysfs.
 * sysfs handles the lifetime of the entry, and never call ->show() after it is
 * unlinked.
 */
static int sysaufs_si_br(struct seq_file *seq, struct super_block *sb,
			 aufs_bindex_t bindex, int idx)
{
	int err;
	struct path path;
	struct dentry *root;
	struct au_branch *br;
	au_br_perm_str_t perm;

	AuDbg("b%d\n", bindex);

	err = 0;
	root = sb->s_root;
	di_read_lock_parent(root, !AuLock_IR);
	br = au_sbr(sb, bindex);

	switch (idx) {
	case AuBrSysfs_BR:
		path.mnt = au_br_mnt(br);
		path.dentry = au_h_dptr(root, bindex);
		au_seq_path(seq, &path);
		au_optstr_br_perm(&perm, br->br_perm);
		err = seq_printf(seq, "=%s\n", perm.a);
		break;
	case AuBrSysfs_BRID:
		err = seq_printf(seq, "%d\n", br->br_id);
		break;
	}
	di_read_unlock(root, !AuLock_IR);
	if (err == -1)
		err = -E2BIG;

	return err;
}

/* ---------------------------------------------------------------------- */

static struct seq_file *au_seq(char *p, ssize_t len)
{
	struct seq_file *seq;

	seq = kzalloc(sizeof(*seq), GFP_NOFS);
	if (seq) {
		/* mutex_init(&seq.lock); */
		seq->buf = p;
		seq->size = len;
		return seq; /* success */
	}

	seq = ERR_PTR(-ENOMEM);
	return seq;
}

#define SysaufsBr_PREFIX	"br"
#define SysaufsBrid_PREFIX	"brid"

/* todo: file size may exceed PAGE_SIZE */
ssize_t sysaufs_si_show(struct kobject *kobj, struct attribute *attr,
			char *buf)
{
	ssize_t err;
	int idx;
	long l;
	aufs_bindex_t bend;
	struct au_sbinfo *sbinfo;
	struct super_block *sb;
	struct seq_file *seq;
	char *name;
	struct attribute **cattr;

	sbinfo = container_of(kobj, struct au_sbinfo, si_kobj);
	sb = sbinfo->si_sb;

	/*
	 * prevent a race condition between sysfs and aufs.
	 * for instance, sysfs_file_read() calls sysfs_get_active_two() which
	 * prohibits maintaining the sysfs entries.
	 * hew we acquire read lock after sysfs_get_active_two().
	 * on the other hand, the remount process may maintain the sysfs/aufs
	 * entries after acquiring write lock.
	 * it can cause a deadlock.
	 * simply we gave up processing read here.
	 */
	err = -EBUSY;
	if (unlikely(!si_noflush_read_trylock(sb)))
		goto out;

	seq = au_seq(buf, PAGE_SIZE);
	err = PTR_ERR(seq);
	if (IS_ERR(seq))
		goto out_unlock;

	name = (void *)attr->name;
	cattr = sysaufs_si_attrs;
	while (*cattr) {
		if (!strcmp(name, (*cattr)->name)) {
			err = container_of(*cattr, struct sysaufs_si_attr, attr)
				->show(seq, sb);
			goto out_seq;
		}
		cattr++;
	}

	if (!strncmp(name, SysaufsBrid_PREFIX,
		     sizeof(SysaufsBrid_PREFIX) - 1)) {
		idx = AuBrSysfs_BRID;
		name += sizeof(SysaufsBrid_PREFIX) - 1;
	} else if (!strncmp(name, SysaufsBr_PREFIX,
			    sizeof(SysaufsBr_PREFIX) - 1)) {
		idx = AuBrSysfs_BR;
		name += sizeof(SysaufsBr_PREFIX) - 1;
	} else
		  BUG();

	err = kstrtol(name, 10, &l);
	if (!err) {
		bend = au_sbend(sb);
		if (l <= bend)
			err = sysaufs_si_br(seq, sb, (aufs_bindex_t)l, idx);
		else
			err = -ENOENT;
	}

out_seq:
	if (!err) {
		err = seq->count;
		/* sysfs limit */
		if (unlikely(err == PAGE_SIZE))
			err = -EFBIG;
	}
	kfree(seq);
out_unlock:
	si_read_unlock(sb);
out:
	return err;
}

/* ---------------------------------------------------------------------- */

void sysaufs_br_init(struct au_branch *br)
{
	int i;
	struct au_brsysfs *br_sysfs;
	struct attribute *attr;

	br_sysfs = br->br_sysfs;
	for (i = 0; i < ARRAY_SIZE(br->br_sysfs); i++) {
		attr = &br_sysfs->attr;
		sysfs_attr_init(attr);
		attr->name = br_sysfs->name;
		attr->mode = S_IRUGO;
		br_sysfs++;
	}
}

void sysaufs_brs_del(struct super_block *sb, aufs_bindex_t bindex)
{
	struct au_branch *br;
	struct kobject *kobj;
	struct au_brsysfs *br_sysfs;
	int i;
	aufs_bindex_t bend;

	dbgaufs_brs_del(sb, bindex);

	if (!sysaufs_brs)
		return;

	kobj = &au_sbi(sb)->si_kobj;
	bend = au_sbend(sb);
	for (; bindex <= bend; bindex++) {
		br = au_sbr(sb, bindex);
		br_sysfs = br->br_sysfs;
		for (i = 0; i < ARRAY_SIZE(br->br_sysfs); i++) {
			sysfs_remove_file(kobj, &br_sysfs->attr);
			br_sysfs++;
		}
	}
}

void sysaufs_brs_add(struct super_block *sb, aufs_bindex_t bindex)
{
	int err, i;
	aufs_bindex_t bend;
	struct kobject *kobj;
	struct au_branch *br;
	struct au_brsysfs *br_sysfs;

	dbgaufs_brs_add(sb, bindex);

	if (!sysaufs_brs)
		return;

	kobj = &au_sbi(sb)->si_kobj;
	bend = au_sbend(sb);
	for (; bindex <= bend; bindex++) {
		br = au_sbr(sb, bindex);
		br_sysfs = br->br_sysfs;
		snprintf(br_sysfs[AuBrSysfs_BR].name, sizeof(br_sysfs->name),
			 SysaufsBr_PREFIX "%d", bindex);
		snprintf(br_sysfs[AuBrSysfs_BRID].name, sizeof(br_sysfs->name),
			 SysaufsBrid_PREFIX "%d", bindex);
		for (i = 0; i < ARRAY_SIZE(br->br_sysfs); i++) {
			err = sysfs_create_file(kobj, &br_sysfs->attr);
			if (unlikely(err))
				pr_warn("failed %s under sysfs(%d)\n",
					br_sysfs->name, err);
			br_sysfs++;
		}
	}
}
