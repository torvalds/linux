/* SPDX-License-Identifier: (GPL-2.0 OR MIT) */
// Copyright (c) 2017 Synopsys, Inc. and/or its affiliates.
// stmmac Support for 5.xx Ethernet QoS cores

#ifndef __DWMAC5_H__
#define __DWMAC5_H__

#define MAC_DPP_FSM_INT_STATUS		0x00000140
#define MAC_AXI_SLV_DPE_ADDR_STATUS	0x00000144
#define MAC_FSM_CONTROL			0x00000148
#define PRTYEN				BIT(1)
#define TMOUTEN				BIT(0)

#define MAC_FPE_CTRL_STS		0x00000234
#define TRSP				BIT(19)
#define TVER				BIT(18)
#define RRSP				BIT(17)
#define RVER				BIT(16)
#define SRSP				BIT(2)
#define SVER				BIT(1)
#define EFPE				BIT(0)

#define MAC_PPS_CONTROL			0x00000b70
#define PPS_MAXIDX(x)			((((x) + 1) * 8) - 1)
#define PPS_MINIDX(x)			((x) * 8)
#define PPSx_MASK(x)			GENMASK(PPS_MAXIDX(x), PPS_MINIDX(x))
#define MCGRENx(x)			BIT(PPS_MAXIDX(x))
#define TRGTMODSELx(x, val)		\
	GENMASK(PPS_MAXIDX(x) - 1, PPS_MAXIDX(x) - 2) & \
	((val) << (PPS_MAXIDX(x) - 2))
#define PPSCMDx(x, val)			\
	GENMASK(PPS_MINIDX(x) + 3, PPS_MINIDX(x)) & \
	((val) << PPS_MINIDX(x))
#define PPSEN0				BIT(4)
#define MAC_PPSx_TARGET_TIME_SEC(x)	(0x00000b80 + ((x) * 0x10))
#define MAC_PPSx_TARGET_TIME_NSEC(x)	(0x00000b84 + ((x) * 0x10))
#define TRGTBUSY0			BIT(31)
#define TTSL0				GENMASK(30, 0)
#define MAC_PPSx_INTERVAL(x)		(0x00000b88 + ((x) * 0x10))
#define MAC_PPSx_WIDTH(x)		(0x00000b8c + ((x) * 0x10))

#define MTL_RXP_CONTROL_STATUS		0x00000ca0
#define RXPI				BIT(31)
#define NPE				GENMASK(23, 16)
#define NVE				GENMASK(7, 0)
#define MTL_RXP_IACC_CTRL_STATUS	0x00000cb0
#define STARTBUSY			BIT(31)
#define RXPEIEC				GENMASK(22, 21)
#define RXPEIEE				BIT(20)
#define WRRDN				BIT(16)
#define ADDR				GENMASK(15, 0)
#define MTL_RXP_IACC_DATA		0x00000cb4
#define MTL_ECC_CONTROL			0x00000cc0
#define MEEAO				BIT(8)
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

/* EQoS version 5.xx VLAN Tag Filter Fail Packets Queuing */
#define GMAC_RXQ_CTRL4			0x00000094
#define GMAC_RXQCTRL_VFFQ_MASK		GENMASK(19, 17)
#define GMAC_RXQCTRL_VFFQ_SHIFT		17
#define GMAC_RXQCTRL_VFFQE		BIT(16)

#define GMAC_INT_FPE_EN			BIT(17)

int dwmac5_safety_feat_config(void __iomem *ioaddr, unsigned int asp,
			      struct stmmac_safety_feature_cfg *safety_cfg);
int dwmac5_safety_feat_irq_status(struct net_device *ndev,
		void __iomem *ioaddr, unsigned int asp,
		struct stmmac_safety_stats *stats);
int dwmac5_safety_feat_dump(struct stmmac_safety_stats *stats,
			int index, unsigned long *count, const char **desc);
int dwmac5_rxp_config(void __iomem *ioaddr, struct stmmac_tc_entry *entries,
		      unsigned int count);
int dwmac5_flex_pps_config(void __iomem *ioaddr, int index,
			   struct stmmac_pps_cfg *cfg, bool enable,
			   u32 sub_second_inc, u32 systime_flags);
void dwmac5_fpe_configure(void __iomem *ioaddr, struct stmmac_fpe_cfg *cfg,
			  u32 num_txq, u32 num_rxq,
			  bool enable);
void dwmac5_fpe_send_mpacket(void __iomem *ioaddr,
			     struct stmmac_fpe_cfg *cfg,
			     enum stmmac_mpacket_type type);
int dwmac5_fpe_irq_status(void __iomem *ioaddr, struct net_device *dev);

#endif /* __DWMAC5_H__ */
