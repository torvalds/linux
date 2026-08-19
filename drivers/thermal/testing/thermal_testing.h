/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/workqueue.h>

extern struct workqueue_struct *tt_wq;

static inline void tt_queue_work(struct work_struct *work)
{
	queue_work(tt_wq, work);
}

extern struct dentry *d_testing;

int tt_add_tz(void);
int tt_del_tz(const char *arg);
int tt_zone_add_trip(const char *arg);
int tt_zone_reg(const char *arg);
int tt_zone_unreg(const char *arg);

void tt_zone_cleanup(void);
