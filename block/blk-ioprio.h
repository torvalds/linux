/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _BLK_IOPRIO_H_
#define _BLK_IOPRIO_H_

#include <linux/kconfig.h>

struct request_queue;

#ifdef CONFIG_BLK_CGROUP_IOPRIO
int blk_ioprio_init(struct request_queue *q);
#else
static inline int blk_ioprio_init(struct request_queue *q)
{
	return 0;
}
#endif

#endif /* _BLK_IOPRIO_H_ */
