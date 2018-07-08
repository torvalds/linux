/*   This program is free software; you can redistribute it and/or modify
 *   it under the terms of the GNU General Public License as published by
 *   the Free Software Foundation; version 2 of the License
 *
 *   This program is distributed in the hope that it will be useful,
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *   GNU General Public License for more details.
 *
 *   Copyright (C) 2014-2016 Sean Wang <sean.wang@mediatek.com>
 *   Copyright (C) 2016-2017 John Crispin <blogic@openwrt.org>
 */

#ifndef NF_HNAT_MTK_H
#define NF_HNAT_MTK_H

#include <asm/dma-mapping.h>
#include <linux/netdevice.h>

#define HNAT_SKB_CB2(__skb)		((struct hnat_skb_cb2 *)&((__skb)->cb[44]))
struct hnat_skb_cb2 {
	__u32  magic;
};

struct hnat_desc {
	u32 entry:14;
	u32 crsn:5;
	u32 sport:4;
	u32 alg:9;
} __attribute__ ((packed));

#define skb_hnat_magic(skb)  (((struct hnat_desc *)(skb->head))->magic)
#define skb_hnat_reason(skb) (((struct hnat_desc *)(skb->head))->crsn)
#define skb_hnat_entry(skb)  (((struct hnat_desc *)(skb->head))->entry)
#define skb_hnat_sport(skb)  (((struct hnat_desc *)(skb->head))->sport)
#define skb_hnat_alg(skb)  (((struct hnat_desc *)(skb->head))->alg)

u32 hnat_tx(struct sk_buff *skb);
u32 hnat_set_skb_info(struct sk_buff *skb, u32 *rxd);
u32 hnat_reg(struct net_device *, void __iomem *);
u32 hnat_unreg(void);

#endif

