#include <linux/atomic.h>
#include "debug.h"

#ifdef CONFIG_DEBUG_FS
#include <linux/debugfs.h>

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

#define ATTR(x)  { .name = #x, .val = &ramster_##x, }
static struct debug_entry {
	const char *name;
	ssize_t *val;
} attrs[] = {
	ATTR(eph_pages_remoted),
	ATTR(pers_pages_remoted),
	ATTR(eph_pages_remote_failed),
	ATTR(pers_pages_remote_failed),
	ATTR(remote_eph_pages_succ_get),
	ATTR(remote_pers_pages_succ_get),
	ATTR(remote_eph_pages_unsucc_get),
	ATTR(remote_pers_pages_unsucc_get),
	ATTR(pers_pages_remote_nomem),
	ATTR(remote_objects_flushed),
	ATTR(remote_pages_flushed),
	ATTR(remote_object_flushes_failed),
	ATTR(remote_page_flushes_failed),
	ATTR(foreign_eph_pages),
	ATTR(foreign_eph_pages_max),
	ATTR(foreign_pers_pages),
	ATTR(foreign_pers_pages_max),
};
#undef ATTR

int ramster_debugfs_init(void)
{
	int i;
	struct dentry *root = debugfs_create_dir("ramster", NULL);
	if (root == NULL)
		return -ENXIO;

	for (i = 0; i < ARRAY_SIZE(attrs); i++)
		if (!debugfs_create_size_t(attrs[i].name,
				S_IRUGO, root, attrs[i].val))
			goto out;
	return 0;
out:
	return -ENODEV;
}
#else
static inline int ramster_debugfs_init(void)
{
	return 0;
}
#endif
