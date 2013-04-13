#include <linux/atomic.h>
#include "debug.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>
#define zdfs    debugfs_create_size_t
#define zdfs64  debugfs_create_u64

ssize_t ramster_eph_pages_remoted;
ssize_t ramster_pers_pages_remoted;
ssize_t ramster_eph_pages_remote_failed;
ssize_t ramster_pers_pages_remote_failed;
ssize_t ramster_remote_eph_pages_succ_get;
ssize_t ramster_remote_pers_pages_succ_get;
ssize_t ramster_remote_eph_pages_unsucc_get;
ssize_t ramster_remote_pers_pages_unsucc_get;
ssize_t ramster_pers_pages_remote_nomem;
ssize_t ramster_remote_objects_flushed;
ssize_t ramster_remote_object_flushes_failed;
ssize_t ramster_remote_pages_flushed;
ssize_t ramster_remote_page_flushes_failed;

int __init ramster_debugfs_init(void)
{
	struct dentry *root = debugfs_create_dir("ramster", NULL);
	if (root == NULL)
		return -ENXIO;

	zdfs("eph_pages_remoted", S_IRUGO, root, &ramster_eph_pages_remoted);
	zdfs("pers_pages_remoted", S_IRUGO, root, &ramster_pers_pages_remoted);
	zdfs("eph_pages_remote_failed", S_IRUGO, root,
		&ramster_eph_pages_remote_failed);
	zdfs("pers_pages_remote_failed", S_IRUGO, root,
		&ramster_pers_pages_remote_failed);
	zdfs("remote_eph_pages_succ_get", S_IRUGO, root,
		&ramster_remote_eph_pages_succ_get);
	zdfs("remote_pers_pages_succ_get", S_IRUGO, root,
		&ramster_remote_pers_pages_succ_get);
	zdfs("remote_eph_pages_unsucc_get", S_IRUGO, root,
		&ramster_remote_eph_pages_unsucc_get);
	zdfs("remote_pers_pages_unsucc_get", S_IRUGO, root,
		&ramster_remote_pers_pages_unsucc_get);
	zdfs("pers_pages_remote_nomem", S_IRUGO, root,
		&ramster_pers_pages_remote_nomem);
	zdfs("remote_objects_flushed", S_IRUGO, root,
		&ramster_remote_objects_flushed);
	zdfs("remote_pages_flushed", S_IRUGO, root,
		&ramster_remote_pages_flushed);
	zdfs("remote_object_flushes_failed", S_IRUGO, root,
		&ramster_remote_object_flushes_failed);
	zdfs("remote_page_flushes_failed", S_IRUGO, root,
		&ramster_remote_page_flushes_failed);
	zdfs("foreign_eph_pages", S_IRUGO, root,
		&ramster_foreign_eph_pages);
	zdfs("foreign_eph_pages_max", S_IRUGO, root,
		&ramster_foreign_eph_pages_max);
	zdfs("foreign_pers_pages", S_IRUGO, root,
		&ramster_foreign_pers_pages);
	zdfs("foreign_pers_pages_max", S_IRUGO, root,
		&ramster_foreign_pers_pages_max);
	return 0;
}
#undef  zdebugfs
#undef  zdfs64
#else
static inline int ramster_debugfs_init(void)
{
	return 0;
}
#endif
