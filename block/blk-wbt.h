/* SPDX-License-Identifier: GPL-2.0 */
#ifndef WB_THROTTLE_H
#define WB_THROTTLE_H

#ifdef CONFIG_BLK_WBT

int wbt_init(struct gendisk *disk);
void wbt_disable_default(struct gendisk *disk);
void wbt_enable_default(struct gendisk *disk);

u64 wbt_get_min_lat(struct request_queue *q);
void wbt_set_min_lat(struct request_queue *q, u64 val);
bool wbt_disabled(struct request_queue *);

u64 wbt_default_latency_nsec(struct request_queue *);

#else

static inline void wbt_disable_default(struct gendisk *disk)
{
}
static inline void wbt_enable_default(struct gendisk *disk)
{
}

#endif /* CONFIG_BLK_WBT */

#endif
