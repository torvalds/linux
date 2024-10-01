/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 * Applied Micro X-Gene SoC Ethernet v2 Driver
 *
 * Copyright (c) 2017, Applied Micro Circuits Corporation
 * Author(s): Iyappan Subramanian <isubramanian@apm.com>
 *	      Keyur Chudgar <kchudgar@apm.com>
 */

#ifndef __XGENE_ENET_V2_ENET_H__
#define __XGENE_ENET_V2_ENET_H__

#define ENET_CLKEN		0xc008
#define ENET_SRST		0xc000
#define ENET_SHIM		0xc010
#define CFG_MEM_RAM_SHUTDOWN	0xd070
#define BLOCK_MEM_RDY		0xd074

#define MEM_RDY			0xffffffff
#define DEVM_ARAUX_COH		BIT(19)
#define DEVM_AWAUX_COH		BIT(3)

#define CFG_FORCE_LINK_STATUS_EN	0x229c
#define FORCE_LINK_STATUS		0x22a0
#define CFG_LINK_AGGR_RESUME		0x27c8
#define RX_DV_GATE_REG			0x2dfc

void xge_wr_csr(struct xge_pdata *pdata, u32 offset, u32 val);
u32 xge_rd_csr(struct xge_pdata *pdata, u32 offset);
int xge_port_reset(struct net_device *ndev);
void xge_port_init(struct net_device *ndev);

#endif  /* __XGENE_ENET_V2_ENET__H__ */
