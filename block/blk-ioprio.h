/* SPDX-License-Identifier: GPL-2.0 */

#ifndef _BLK_IOPRIO_H_
#define _BLK_IOPRIO_H_

#include <linux/kconfig.h>

struct request_queue;
struct bio;

#ifdef CONFIG_BLK_CGROUP_IOPRIO
void blkcg_set_ioprio(struct bio *bio);
#else
static inline void blkcg_set_ioprio(struct bio *bio)
{
}
#endif

#endif /* _BLK_IOPRIO_H_ */
