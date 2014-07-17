/*
 * Copyright (C) 2005-2014 Junjiro R. Okajima
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

static inline
ssize_t sysaufs_si_show(struct kobject *kobj, struct attribute *attr,
			 char *buf)
{
	return 0;
}

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
