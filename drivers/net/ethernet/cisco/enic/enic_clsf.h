#ifndef _ENIC_CLSF_H_
#define _ENIC_CLSF_H_

#include "vnic_dev.h"
#include "enic.h"

#define ENIC_CLSF_EXPIRE_COUNT 128

int enic_addfltr_5t(struct enic *enic, struct flow_keys *keys, u16 rq);
int enic_delfltr(struct enic *enic, u16 filter_id);

#ifdef CONFIG_RFS_ACCEL
void enic_rfs_flw_tbl_init(struct enic *enic);
void enic_rfs_flw_tbl_free(struct enic *enic);
int enic_rx_flow_steer(struct net_device *dev, const struct sk_buff *skb,
		       u16 rxq_index, u32 flow_id);
#else
static inline void enic_rfs_flw_tbl_init(struct enic *enic) {}
static inline void enic_rfs_flw_tbl_free(struct enic *enic) {}
#endif /* CONFIG_RFS_ACCEL */

#endif /* _ENIC_CLSF_H_ */
