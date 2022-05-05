/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2018 Oracle and/or its affiliates. All rights reserved. */

#ifndef _IXGBEVF_IPSEC_H_
#define _IXGBEVF_IPSEC_H_

#define IXGBE_IPSEC_MAX_SA_COUNT	1024
#define IXGBE_IPSEC_BASE_RX_INDEX	0
#define IXGBE_IPSEC_BASE_TX_INDEX	IXGBE_IPSEC_MAX_SA_COUNT
#define IXGBE_IPSEC_AUTH_BITS		128

#define IXGBE_RXMOD_VALID		0x00000001
#define IXGBE_RXMOD_PROTO_ESP		0x00000004
#define IXGBE_RXMOD_DECRYPT		0x00000008
#define IXGBE_RXMOD_IPV6		0x00000010

struct rx_sa {
	struct hlist_node hlist;
	struct xfrm_state *xs;
	__be32 ipaddr[4];
	u32 key[4];
	u32 salt;
	u32 mode;
	u32 pfsa;
	bool used;
	bool decrypt;
};

struct rx_ip_sa {
	__be32 ipaddr[4];
	u32 ref_cnt;
	bool used;
};

struct tx_sa {
	struct xfrm_state *xs;
	u32 key[4];
	u32 salt;
	u32 pfsa;
	bool encrypt;
	bool used;
};

struct ixgbevf_ipsec_tx_data {
	u32 flags;
	u16 trailer_len;
	u16 pfsa;
};

struct ixgbevf_ipsec {
	u16 num_rx_sa;
	u16 num_tx_sa;
	struct rx_sa *rx_tbl;
	struct tx_sa *tx_tbl;
	DECLARE_HASHTABLE(rx_sa_list, 10);
};

struct sa_mbx_msg {
	__be32 spi;
	u8 dir;
	u8 proto;
	u16 family;
	__be32 addr[4];
	u32 key[5];
};
#endif /* _IXGBEVF_IPSEC_H_ */
