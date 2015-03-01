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
 * module global variables and operations
 */

#include <linux/module.h>
#include <linux/seq_file.h>
#include "aufs.h"

void *au_kzrealloc(void *p, unsigned int nused, unsigned int new_sz, gfp_t gfp)
{
	if (new_sz <= nused)
		return p;

	p = krealloc(p, new_sz, gfp);
	if (p)
		memset(p + nused, 0, new_sz - nused);
	return p;
}

/* ---------------------------------------------------------------------- */

/*
 * aufs caches
 */
struct kmem_cache *au_cachep[AuCache_Last];
static int __init au_cache_init(void)
{
	au_cachep[AuCache_DINFO] = AuCacheCtor(au_dinfo, au_di_init_once);
	if (au_cachep[AuCache_DINFO])
		/* SLAB_DESTROY_BY_RCU */
		au_cachep[AuCache_ICNTNR] = AuCacheCtor(au_icntnr,
							au_icntnr_init_once);
	if (au_cachep[AuCache_ICNTNR])
		au_cachep[AuCache_FINFO] = AuCacheCtor(au_finfo,
						       au_fi_init_once);
	if (au_cachep[AuCache_FINFO])
		au_cachep[AuCache_VDIR] = AuCache(au_vdir);
	if (au_cachep[AuCache_VDIR])
		au_cachep[AuCache_DEHSTR] = AuCache(au_vdir_dehstr);
	if (au_cachep[AuCache_DEHSTR])
		return 0;

	return -ENOMEM;
}

static void au_cache_fin(void)
{
	int i;

	/*
	 * Make sure all delayed rcu free inodes are flushed before we
	 * destroy cache.
	 */
	rcu_barrier();

	/* excluding AuCache_HNOTIFY */
	BUILD_BUG_ON(AuCache_HNOTIFY + 1 != AuCache_Last);
	for (i = 0; i < AuCache_HNOTIFY; i++)
		if (au_cachep[i]) {
			kmem_cache_destroy(au_cachep[i]);
			au_cachep[i] = NULL;
		}
}

/* ---------------------------------------------------------------------- */

int au_dir_roflags;

#ifdef CONFIG_AUFS_SBILIST
/*
 * iterate_supers_type() doesn't protect us from
 * remounting (branch management)
 */
struct au_splhead au_sbilist;
#endif

struct lock_class_key au_lc_key[AuLcKey_Last];

/*
 * functions for module interface.
 */
MODULE_LICENSE("GPL");
/* MODULE_LICENSE("GPL v2"); */
MODULE_AUTHOR("Junjiro R. Okajima <aufs-users@lists.sourceforge.net>");
MODULE_DESCRIPTION(AUFS_NAME
	" -- Advanced multi layered unification filesystem");
MODULE_VERSION(AUFS_VERSION);
MODULE_ALIAS_FS(AUFS_NAME);

/* this module parameter has no meaning when SYSFS is disabled */
int sysaufs_brs = 1;
MODULE_PARM_DESC(brs, "use <sysfs>/fs/aufs/si_*/brN");
module_param_named(brs, sysaufs_brs, int, S_IRUGO);

/* this module parameter has no meaning when USER_NS is disabled */
static bool au_userns;
MODULE_PARM_DESC(allow_userns, "allow unprivileged to mount under userns");
module_param_named(allow_userns, au_userns, bool, S_IRUGO);

/* ---------------------------------------------------------------------- */

static char au_esc_chars[0x20 + 3]; /* 0x01-0x20, backslash, del, and NULL */

int au_seq_path(struct seq_file *seq, struct path *path)
{
	return seq_path(seq, path, au_esc_chars);
}

/* ---------------------------------------------------------------------- */

static int __init aufs_init(void)
{
	int err, i;
	char *p;

	p = au_esc_chars;
	for (i = 1; i <= ' '; i++)
		*p++ = i;
	*p++ = '\\';
	*p++ = '\x7f';
	*p = 0;

	au_dir_roflags = au_file_roflags(O_DIRECTORY | O_LARGEFILE);

	au_sbilist_init();
	sysaufs_brs_init();
	au_debug_init();
	au_dy_init();
	err = sysaufs_init();
	if (unlikely(err))
		goto out;
	err = au_procfs_init();
	if (unlikely(err))
		goto out_sysaufs;
	err = au_wkq_init();
	if (unlikely(err))
		goto out_procfs;
	err = au_loopback_init();
	if (unlikely(err))
		goto out_wkq;
	err = au_hnotify_init();
	if (unlikely(err))
		goto out_loopback;
	err = au_sysrq_init();
	if (unlikely(err))
		goto out_hin;
	err = au_cache_init();
	if (unlikely(err))
		goto out_sysrq;

	aufs_fs_type.fs_flags |= au_userns ? FS_USERNS_MOUNT : 0;
	err = register_filesystem(&aufs_fs_type);
	if (unlikely(err))
		goto out_cache;

	/* since we define pr_fmt, call printk directly */
	printk(KERN_INFO AUFS_NAME " " AUFS_VERSION "\n");
	goto out; /* success */

out_cache:
	au_cache_fin();
out_sysrq:
	au_sysrq_fin();
out_hin:
	au_hnotify_fin();
out_loopback:
	au_loopback_fin();
out_wkq:
	au_wkq_fin();
out_procfs:
	au_procfs_fin();
out_sysaufs:
	sysaufs_fin();
	au_dy_fin();
out:
	return err;
}

static void __exit aufs_exit(void)
{
	unregister_filesystem(&aufs_fs_type);
	au_cache_fin();
	au_sysrq_fin();
	au_hnotify_fin();
	au_loopback_fin();
	au_wkq_fin();
	au_procfs_fin();
	sysaufs_fin();
	au_dy_fin();
}

module_init(aufs_init);
module_exit(aufs_exit);
