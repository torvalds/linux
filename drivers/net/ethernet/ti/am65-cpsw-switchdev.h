/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (C) 2020 Texas Instruments Incorporated - https://www.ti.com/
 */

#ifndef DRIVERS_NET_ETHERNET_TI_AM65_CPSW_SWITCHDEV_H_
#define DRIVERS_NET_ETHERNET_TI_AM65_CPSW_SWITCHDEV_H_

#include <linux/skbuff.h>

#if IS_ENABLED(CONFIG_TI_K3_AM65_CPSW_SWITCHDEV)
static inline void am65_cpsw_nuss_set_offload_fwd_mark(struct sk_buff *skb, bool val)
{
	skb->offload_fwd_mark = val;
}

int am65_cpsw_switchdev_register_notifiers(struct am65_cpsw_common *cpsw);
void am65_cpsw_switchdev_unregister_notifiers(struct am65_cpsw_common *cpsw);
#else
static inline int am65_cpsw_switchdev_register_notifiers(struct am65_cpsw_common *cpsw)
{
	return -EOPNOTSUPP;
}

static inline void am65_cpsw_switchdev_unregister_notifiers(struct am65_cpsw_common *cpsw)
{
}

static inline void am65_cpsw_nuss_set_offload_fwd_mark(struct sk_buff *skb, bool val)
{
}

#endif

#endif /* DRIVERS_NET_ETHERNET_TI_AM65_CPSW_SWITCHDEV_H_ */
