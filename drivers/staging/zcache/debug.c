#include <linux/atomic.h>
#include "debug.h"

#ifdef CONFIG_ZCACHE_DEBUG
#include <linux/debugfs.h>

#define ATTR(x)  { .name = #x, .val = &zcache_##x, }
static struct debug_entry {
	const char *name;
	ssize_t *val;
} attrs[] = {
	ATTR(obj_count), ATTR(obj_count_max),
	ATTR(objnode_count), ATTR(objnode_count_max),
	ATTR(flush_total), ATTR(flush_found),
	ATTR(flobj_total), ATTR(flobj_found),
	ATTR(failed_eph_puts), ATTR(failed_pers_puts),
	ATTR(failed_getfreepages), ATTR(failed_alloc),
	ATTR(put_to_flush),
	ATTR(compress_poor), ATTR(mean_compress_poor),
	ATTR(eph_ate_tail), ATTR(eph_ate_tail_failed),
	ATTR(pers_ate_eph), ATTR(pers_ate_eph_failed),
	ATTR(evicted_eph_zpages), ATTR(evicted_eph_pageframes),
	ATTR(eph_pageframes), ATTR(eph_pageframes_max),
	ATTR(eph_zpages), ATTR(eph_zpages_max),
	ATTR(pers_zpages), ATTR(pers_zpages_max),
	ATTR(last_active_file_pageframes),
	ATTR(last_inactive_file_pageframes),
	ATTR(last_active_anon_pageframes),
	ATTR(last_inactive_anon_pageframes),
	ATTR(eph_nonactive_puts_ignored),
	ATTR(pers_nonactive_puts_ignored),
#ifdef CONFIG_ZCACHE_WRITEBACK
	ATTR(zcache_outstanding_writeback_pages),
	ATTR(zcache_writtenback_pages),
#endif
};
#undef ATTR
int zcache_debugfs_init(void)
{
	unsigned int i;
	struct dentry *root = debugfs_create_dir("zcache", NULL);
	if (root == NULL)
		return -ENXIO;

	for (i = 0; i < ARRAY_SIZE(attrs); i++)
		if (!debugfs_create_size_t(attrs[i].name, S_IRUGO, root, attrs[i].val))
			goto out;

	debugfs_create_u64("eph_zbytes", S_IRUGO, root, &zcache_eph_zbytes);
	debugfs_create_u64("eph_zbytes_max", S_IRUGO, root, &zcache_eph_zbytes_max);
	debugfs_create_u64("pers_zbytes", S_IRUGO, root, &zcache_pers_zbytes);
	debugfs_create_u64("pers_zbytes_max", S_IRUGO, root, &zcache_pers_zbytes_max);

	return 0;
out:
	return -ENODEV;
}

/* developers can call this in case of ooms, e.g. to find memory leaks */
void zcache_dump(void)
{
	unsigned int i;
	for (i = 0; i < ARRAY_SIZE(attrs); i++)
		pr_debug("zcache: %s=%zu\n", attrs[i].name, *attrs[i].val);

	pr_debug("zcache: eph_zbytes=%llu\n", (unsigned long long)zcache_eph_zbytes);
	pr_debug("zcache: eph_zbytes_max=%llu\n", (unsigned long long)zcache_eph_zbytes_max);
	pr_debug("zcache: pers_zbytes=%llu\n", (unsigned long long)zcache_pers_zbytes);
	pr_debug("zcache: pers_zbytes_max=%llu\n", (unsigned long long)zcache_pers_zbytes_max);
}
#endif
