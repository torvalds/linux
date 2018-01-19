/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _ENIC_CLSF_H_
#define _ENIC_CLSF_H_

#include "vnic_dev.h"
#include "enic.h"

#define ENIC_CLSF_EXPIRE_COUNT 128

int enic_addfltr_5t(struct enic *enic, struct flow_keys *keys, u16 rq);
int enic_delfltr(struct enic *enic, u16 filter_id);
void enic_rfs_flw_tbl_init(struct enic *enic);
void enic_rfs_flw_tbl_free(struct enic *enic);
struct enic_rfs_fltr_node *htbl_fltr_search(struct enic *enic, u16 fltr_id);

#ifdef CONFIG_RFS_ACCEL
int enic_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
		       u16 rxq_index, u32 flow_id);
void enic_flow_may_expire(unsigned long data);

static inline void enic_rfs_timer_start(struct enic *enic)
{
	init_timer(&enic->rfs_h.rfs_may_expire);
	enic->rfs_h.rfs_may_expire.function = enic_flow_may_expire;
	enic->rfs_h.rfs_may_expire.data = (unsigned long)enic;
	mod_timer(&enic->rfs_h.rfs_may_expire, jiffies + HZ/4);
}

static inline void enic_rfs_timer_stop(struct enic *enic)
{
	del_timer_sync(&enic->rfs_h.rfs_may_expire);
}
#else
static inline void enic_rfs_timer_start(struct enic *enic) {}
static inline void enic_rfs_timer_stop(struct enic *enic) {}
#endif /* CONFIG_RFS_ACCEL */

#endif /* _ENIC_CLSF_H_ */
