/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2017 Oracle and/or its affiliates. All rights reserved. */

#ifndef _IXGBE_IPSEC_H_
#define _IXGBE_IPSEC_H_

#define IXGBE_IPSEC_MAX_SA_COUNT	1024
#define IXGBE_IPSEC_MAX_RX_IP_COUNT	128
#define IXGBE_IPSEC_BASE_RX_INDEX	0
#define IXGBE_IPSEC_BASE_TX_INDEX	IXGBE_IPSEC_MAX_SA_COUNT
#define IXGBE_IPSEC_AUTH_BITS		128

#define IXGBE_RXTXIDX_IPS_EN		0x00000001
#define IXGBE_RXIDX_TBL_SHIFT		1
enum ixgbe_ipsec_tbl_sel {
	ips_rx_ip_tbl	=	0x01,
	ips_rx_spi_tbl	=	0x02,
	ips_rx_key_tbl	=	0x03,
};

#define IXGBE_RXTXIDX_IDX_SHIFT		3
#define IXGBE_RXTXIDX_READ		0x40000000
#define IXGBE_RXTXIDX_WRITE		0x80000000

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
	u8  iptbl_ind;
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
	bool encrypt;
	bool used;
};

struct ixgbe_ipsec_tx_data {
	u32 flags;
	u16 trailer_len;
	u16 sa_idx;
};

struct ixgbe_ipsec {
	u16 num_rx_sa;
	u16 num_tx_sa;
	struct rx_ip_sa *ip_tbl;
	struct rx_sa *rx_tbl;
	struct tx_sa *tx_tbl;
	DECLARE_HASHTABLE(rx_sa_list, 10);
};
#endif /* _IXGBE_IPSEC_H_ */
