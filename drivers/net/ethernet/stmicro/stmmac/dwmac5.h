// SPDX-License-Identifier: (GPL-2.0 OR MIT)
// Copyright (c) 2017 Synopsys, Inc. and/or its affiliates.
// stmmac Support for 5.xx Ethernet QoS cores

#ifndef __DWMAC5_H__
#define __DWMAC5_H__

#define MAC_DPP_FSM_INT_STATUS		0x00000140
#define MAC_AXI_SLV_DPE_ADDR_STATUS	0x00000144
#define MAC_FSM_CONTROL			0x00000148
#define PRTYEN				BIT(1)
#define TMOUTEN				BIT(0)

#define MTL_ECC_CONTROL			0x00000cc0
#define TSOEE				BIT(4)
#define MRXPEE				BIT(3)
#define MESTEE				BIT(2)
#define MRXEE				BIT(1)
#define MTXEE				BIT(0)

#define MTL_SAFETY_INT_STATUS		0x00000cc4
#define MCSIS				BIT(31)
#define MEUIS				BIT(1)
#define MECIS				BIT(0)
#define MTL_ECC_INT_ENABLE		0x00000cc8
#define RPCEIE				BIT(12)
#define ECEIE				BIT(8)
#define RXCEIE				BIT(4)
#define TXCEIE				BIT(0)
#define MTL_ECC_INT_STATUS		0x00000ccc
#define MTL_DPP_CONTROL			0x00000ce0
#define EPSI				BIT(2)
#define OPE				BIT(1)
#define EDPP				BIT(0)

#define DMA_SAFETY_INT_STATUS		0x00001080
#define MSUIS				BIT(29)
#define MSCIS				BIT(28)
#define DEUIS				BIT(1)
#define DECIS				BIT(0)
#define DMA_ECC_INT_ENABLE		0x00001084
#define TCEIE				BIT(0)
#define DMA_ECC_INT_STATUS		0x00001088

int dwmac5_safety_feat_config(void __iomem *ioaddr, unsigned int asp);
bool dwmac5_safety_feat_irq_status(struct net_device *ndev,
		void __iomem *ioaddr, unsigned int asp,
		struct stmmac_safety_stats *stats);
const char *dwmac5_safety_feat_dump(struct stmmac_safety_stats *stats,
			int index, unsigned long *count);

#endif /* __DWMAC5_H__ */
