/* SPDX-License-Identifier: GPL-2.0+ */
/* TI K3 AM65 CPTS driver interface
 *
 * Copyright (C) 2020 Texas Instruments Incorporated - http://www.ti.com
 */

#ifndef K3_CPTS_H_
#define K3_CPTS_H_

#include <linux/device.h>
#include <linux/of.h>

struct am65_cpts;

struct am65_cpts_estf_cfg {
	u64 ns_period;
	u64 ns_start;
};

#if IS_ENABLED(CONFIG_TI_K3_AM65_CPTS)
struct am65_cpts *am65_cpts_create(struct device *dev, void __iomem *regs,
				   struct device_node *node);
int am65_cpts_phc_index(struct am65_cpts *cpts);
void am65_cpts_tx_timestamp(struct am65_cpts *cpts, struct sk_buff *skb);
void am65_cpts_prep_tx_timestamp(struct am65_cpts *cpts, struct sk_buff *skb);
void am65_cpts_rx_enable(struct am65_cpts *cpts, bool en);
u64 am65_cpts_ns_gettime(struct am65_cpts *cpts);
int am65_cpts_estf_enable(struct am65_cpts *cpts, int idx,
			  struct am65_cpts_estf_cfg *cfg);
void am65_cpts_estf_disable(struct am65_cpts *cpts, int idx);
void am65_cpts_suspend(struct am65_cpts *cpts);
void am65_cpts_resume(struct am65_cpts *cpts);
#else
static inline struct am65_cpts *am65_cpts_create(struct device *dev,
						 void __iomem *regs,
						 struct device_node *node)
{
	return ERR_PTR(-EOPNOTSUPP);
}

static inline int am65_cpts_phc_index(struct am65_cpts *cpts)
{
	return -1;
}

static inline void am65_cpts_tx_timestamp(struct am65_cpts *cpts,
					  struct sk_buff *skb)
{
}

static inline void am65_cpts_prep_tx_timestamp(struct am65_cpts *cpts,
					       struct sk_buff *skb)
{
}

static inline void am65_cpts_rx_enable(struct am65_cpts *cpts, bool en)
{
}

static inline s64 am65_cpts_ns_gettime(struct am65_cpts *cpts)
{
	return 0;
}

static inline int am65_cpts_estf_enable(struct am65_cpts *cpts, int idx,
					struct am65_cpts_estf_cfg *cfg)
{
	return 0;
}

static inline void am65_cpts_estf_disable(struct am65_cpts *cpts, int idx)
{
}

static inline void am65_cpts_suspend(struct am65_cpts *cpts)
{
}

static inline void am65_cpts_resume(struct am65_cpts *cpts)
{
}
#endif

#endif /* K3_CPTS_H_ */
