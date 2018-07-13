#ifndef __MLX5E_ACCEL_H__
#define __MLX5E_ACCEL_H__

#ifdef CONFIG_MLX5_ACCEL

#include <linux/skbuff.h>
#include <linux/netdevice.h>
#include "en.h"

static inline bool is_metadata_hdr_valid(struct sk_buff *skb)
{
	__be16 *ethtype;

	if (unlikely(skb->len < ETH_HLEN + MLX5E_METADATA_ETHER_LEN))
		return false;
	ethtype = (__be16 *)(skb->data + ETH_ALEN * 2);
	if (*ethtype != cpu_to_be16(MLX5E_METADATA_ETHER_TYPE))
		return false;
	return true;
}

static inline void remove_metadata_hdr(struct sk_buff *skb)
{
	struct ethhdr *old_eth;
	struct ethhdr *new_eth;

	/* Remove the metadata from the buffer */
	old_eth = (struct ethhdr *)skb->data;
	new_eth = (struct ethhdr *)(skb->data + MLX5E_METADATA_ETHER_LEN);
	memmove(new_eth, old_eth, 2 * ETH_ALEN);
	/* Ethertype is already in its new place */
	skb_pull_inline(skb, MLX5E_METADATA_ETHER_LEN);
}

#endif /* CONFIG_MLX5_ACCEL */

#endif /* __MLX5E_EN_ACCEL_H__ */
