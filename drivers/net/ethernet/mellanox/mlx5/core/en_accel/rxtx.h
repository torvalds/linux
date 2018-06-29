
#ifndef __MLX5E_EN_ACCEL_RX_TX_H__
#define __MLX5E_EN_ACCEL_RX_TX_H__

#include <linux/skbuff.h>
#include "en.h"

struct sk_buff *mlx5e_udp_gso_handle_tx_skb(struct net_device *netdev,
					    struct mlx5e_txqsq *sq,
					    struct sk_buff *skb,
					    struct mlx5e_tx_wqe **wqe,
					    u16 *pi);

#endif
