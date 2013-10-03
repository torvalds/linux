#ifndef LUSTRE_COMMON_H
#define LUSTRE_COMMON_H

#include <linux/sched.h>

static inline int cfs_cleanup_group_info(void)
{
	struct group_info *ginfo;

	ginfo = groups_alloc(0);
	if (!ginfo)
		return -ENOMEM;

	set_current_groups(ginfo);
	put_group_info(ginfo);

	return 0;
}

#define ll_inode_blksize(a)		(1<<(a)->i_blkbits)

#endif
