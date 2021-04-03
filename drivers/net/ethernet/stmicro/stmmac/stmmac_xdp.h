/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright (c) 2021, Intel Corporation. */

#ifndef _STMMAC_XDP_H_
#define _STMMAC_XDP_H_

#define STMMAC_MAX_RX_BUF_SIZE(num)	(((num) * PAGE_SIZE) - XDP_PACKET_HEADROOM)

int stmmac_xdp_set_prog(struct stmmac_priv *priv, struct bpf_prog *prog,
			struct netlink_ext_ack *extack);

#endif /* _STMMAC_XDP_H_ */
