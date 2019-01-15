/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2005-2018 Junjiro R. Okajima
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
 * sysfs interface and mount lifetime management
 */

#ifndef __SYSAUFS_H__
#define __SYSAUFS_H__

#ifdef __KERNEL__

#include <linux/sysfs.h>
#include "module.h"

struct super_block;
struct au_sbinfo;

struct sysaufs_si_attr {
	struct attribute attr;
	int (*show)(struct seq_file *seq, struct super_block *sb);
};

/* ---------------------------------------------------------------------- */

/* sysaufs.c */
extern unsigned long sysaufs_si_mask;
extern struct kset *sysaufs_kset;
extern struct attribute *sysaufs_si_attrs[];
int sysaufs_si_init(struct au_sbinfo *sbinfo);
int __init sysaufs_init(void);
void sysaufs_fin(void);

/* ---------------------------------------------------------------------- */

/* some people doesn't like to show a pointer in kernel */
static inline unsigned long sysaufs_si_id(struct au_sbinfo *sbinfo)
{
	return sysaufs_si_mask ^ (unsigned long)sbinfo;
}

#define SysaufsSiNamePrefix	"si_"
#define SysaufsSiNameLen	(sizeof(SysaufsSiNamePrefix) + 16)
static inline void sysaufs_name(struct au_sbinfo *sbinfo, char *name)
{
	snprintf(name, SysaufsSiNameLen, SysaufsSiNamePrefix "%lx",
		 sysaufs_si_id(sbinfo));
}

struct au_branch;
#ifdef CONFIG_SYSFS
/* sysfs.c */
extern struct attribute_group *sysaufs_attr_group;

int sysaufs_si_xi_path(struct seq_file *seq, struct super_block *sb);
ssize_t sysaufs_si_show(struct kobject *kobj, struct attribute *attr,
			 char *buf);
long au_brinfo_ioctl(struct file *file, unsigned long arg);
#ifdef CONFIG_COMPAT
long au_brinfo_compat_ioctl(struct file *file, unsigned long arg);
#endif

void sysaufs_br_init(struct au_branch *br);
void sysaufs_brs_add(struct super_block *sb, aufs_bindex_t bindex);
void sysaufs_brs_del(struct super_block *sb, aufs_bindex_t bindex);

#define sysaufs_brs_init()	do {} while (0)

#else
#define sysaufs_attr_group	NULL

AuStubInt0(sysaufs_si_xi_path, struct seq_file *seq, struct super_block *sb)
AuStub(ssize_t, sysaufs_si_show, return 0, struct kobject *kobj,
       struct attribute *attr, char *buf)
AuStubVoid(sysaufs_br_init, struct au_branch *br)
AuStubVoid(sysaufs_brs_add, struct super_block *sb, aufs_bindex_t bindex)
AuStubVoid(sysaufs_brs_del, struct super_block *sb, aufs_bindex_t bindex)

static inline void sysaufs_brs_init(void)
{
	sysaufs_brs = 0;
}

#endif /* CONFIG_SYSFS */

#endif /* __KERNEL__ */
#endif /* __SYSAUFS_H__ */
