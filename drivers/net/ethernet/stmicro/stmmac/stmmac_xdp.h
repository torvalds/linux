/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021, Intel Corporation. */

#ifndef _STMMAC_XDP_H_
#define _STMMAC_XDP_H_

#define STMMAC_RX_DMA_ATTR	(DMA_ATTR_SKIP_CPU_SYNC | DMA_ATTR_WEAK_ORDERING)

int stmmac_xdp_setup_pool(struct stmmac_priv *priv, struct xsk_buff_pool *pool,
			  u16 queue);
int stmmac_xdp_set_prog(struct stmmac_priv *priv, struct bpf_prog *prog,
			struct netlink_ext_ack *extack);

#endif /* _STMMAC_XDP_H_ */
