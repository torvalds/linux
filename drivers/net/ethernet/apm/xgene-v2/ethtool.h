/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Applied Micro X-Gene SoC Ethernet v2 Driver
 *
 * Copyright (c) 2017, Applied Micro Circuits Corporation
 * Author(s): Iyappan Subramanian <isubramanian@apm.com>
 */

#ifndef __XGENE_ENET_V2_ETHTOOL_H__
#define __XGENE_ENET_V2_ETHTOOL_H__

struct xge_gstrings_stats {
	char name[ETH_GSTRING_LEN];
	int offset;
};

struct xge_gstrings_extd_stats {
	char name[ETH_GSTRING_LEN];
	u32 addr;
	u32 value;
};

#define TR64			0xa080
#define TR127			0xa084
#define TR255			0xa088
#define TR511			0xa08c
#define TR1K			0xa090
#define TRMAX			0xa094
#define TRMGV			0xa098
#define RFCS			0xa0a4
#define RMCA			0xa0a8
#define RBCA			0xa0ac
#define RXCF			0xa0b0
#define RXPF			0xa0b4
#define RXUO			0xa0b8
#define RALN			0xa0bc
#define RFLR			0xa0c0
#define RCDE			0xa0c4
#define RCSE			0xa0c8
#define RUND			0xa0cc
#define ROVR			0xa0d0
#define RFRG			0xa0d4
#define RJBR			0xa0d8
#define RDRP			0xa0dc
#define TMCA			0xa0e8
#define TBCA			0xa0ec
#define TXPF			0xa0f0
#define TDFR			0xa0f4
#define TEDF			0xa0f8
#define TSCL			0xa0fc
#define TMCL			0xa100
#define TLCL			0xa104
#define TXCL			0xa108
#define TNCL			0xa10c
#define TPFH			0xa110
#define TDRP			0xa114
#define TJBR			0xa118
#define TFCS			0xa11c
#define TXCF			0xa120
#define TOVR			0xa124
#define TUND			0xa128
#define TFRG			0xa12c

void xge_set_ethtool_ops(struct net_device *ndev);

#endif  /* __XGENE_ENET_V2_ETHTOOL_H__ */
