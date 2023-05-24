/* SPDX-License-Identifier: (GPL-2.0-only OR BSD-2-Clause) */
/* Copyright (C) 2023 Corigine, Inc. */

#ifndef __NFP_NIC_H__
#define __NFP_NIC_H__ 1

#include <linux/netdevice.h>

#ifdef CONFIG_DCB
/* DCB feature definitions */
#define NFP_NET_MAX_DSCP	64
#define NFP_NET_MAX_TC		IEEE_8021QAZ_MAX_TCS
#define NFP_NET_MAX_PRIO	8
#define NFP_DCB_CFG_STRIDE	256

struct nfp_dcb {
	u8 dscp2prio[NFP_NET_MAX_DSCP];
	u8 prio2tc[NFP_NET_MAX_PRIO];
	u8 tc2idx[IEEE_8021QAZ_MAX_TCS];
	u64 tc_maxrate[IEEE_8021QAZ_MAX_TCS];
	u8 tc_tx_pct[IEEE_8021QAZ_MAX_TCS];
	u8 tc_tsa[IEEE_8021QAZ_MAX_TCS];
	u8 dscp_cnt;
	u8 trust_status;
	bool rate_init;
	bool ets_init;

	struct nfp_cpp_area *dcbcfg_tbl_area;
	u8 __iomem *dcbcfg_tbl;
	u32 cfg_offset;
};

int nfp_nic_dcb_init(struct nfp_net *nn);
void nfp_nic_dcb_clean(struct nfp_net *nn);
#else
static inline int nfp_nic_dcb_init(struct nfp_net *nn) { return 0; }
static inline void nfp_nic_dcb_clean(struct nfp_net *nn) {}
#endif

struct nfp_app_nic_private {
#ifdef CONFIG_DCB
	struct nfp_dcb dcb;
#endif
};

#endif
