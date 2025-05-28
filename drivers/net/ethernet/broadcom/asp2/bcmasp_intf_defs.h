/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __BCMASP_INTF_DEFS_H
#define __BCMASP_INTF_DEFS_H

#define UMC_OFFSET(intf)		\
	((((intf)->port) * 0x800) + 0xc000)
#define  UMC_CMD			0x008
#define   UMC_CMD_TX_EN			BIT(0)
#define   UMC_CMD_RX_EN			BIT(1)
#define   UMC_CMD_SPEED_SHIFT		0x2
#define    UMC_CMD_SPEED_MASK		0x3
#define    UMC_CMD_SPEED_10		0x0
#define    UMC_CMD_SPEED_100		0x1
#define    UMC_CMD_SPEED_1000		0x2
#define    UMC_CMD_SPEED_2500		0x3
#define   UMC_CMD_PROMISC		BIT(4)
#define   UMC_CMD_PAD_EN		BIT(5)
#define   UMC_CMD_CRC_FWD		BIT(6)
#define   UMC_CMD_PAUSE_FWD		BIT(7)
#define   UMC_CMD_RX_PAUSE_IGNORE	BIT(8)
#define   UMC_CMD_TX_ADDR_INS		BIT(9)
#define   UMC_CMD_HD_EN			BIT(10)
#define   UMC_CMD_SW_RESET		BIT(13)
#define   UMC_CMD_LCL_LOOP_EN		BIT(15)
#define   UMC_CMD_AUTO_CONFIG		BIT(22)
#define   UMC_CMD_CNTL_FRM_EN		BIT(23)
#define   UMC_CMD_NO_LEN_CHK		BIT(24)
#define   UMC_CMD_RMT_LOOP_EN		BIT(25)
#define   UMC_CMD_PRBL_EN		BIT(27)
#define   UMC_CMD_TX_PAUSE_IGNORE	BIT(28)
#define   UMC_CMD_TX_RX_EN		BIT(29)
#define   UMC_CMD_RUNT_FILTER_DIS	BIT(30)
#define  UMC_MAC0			0x0c
#define  UMC_MAC1			0x10
#define  UMC_FRM_LEN			0x14
#define  UMC_EEE_CTRL			0x64
#define   EN_LPI_RX_PAUSE		BIT(0)
#define   EN_LPI_TX_PFC			BIT(1)
#define   EN_LPI_TX_PAUSE		BIT(2)
#define   EEE_EN			BIT(3)
#define   RX_FIFO_CHECK			BIT(4)
#define   EEE_TX_CLK_DIS		BIT(5)
#define   DIS_EEE_10M			BIT(6)
#define   LP_IDLE_PREDICTION_MODE	BIT(7)
#define  UMC_EEE_LPI_TIMER		0x68
#define  UMC_PAUSE_CNTRL		0x330
#define  UMC_TX_FLUSH			0x334
#define  UMC_GR64			0x400
#define  UMC_GR127			0x404
#define  UMC_GR255			0x408
#define  UMC_GR511			0x40c
#define  UMC_GR1023			0x410
#define  UMC_GR1518			0x414
#define  UMC_GRMGV			0x418
#define  UMC_GR2047			0x41c
#define  UMC_GR4095			0x420
#define  UMC_GR9216			0x424
#define  UMC_GRPKT			0x428
#define  UMC_GRBYT			0x42c
#define  UMC_GRMCA			0x430
#define  UMC_GRBCA			0x434
#define  UMC_GRFCS			0x438
#define  UMC_GRXCF			0x43c
#define  UMC_GRXPF			0x440
#define  UMC_GRXUO			0x444
#define  UMC_GRALN			0x448
#define  UMC_GRFLR			0x44c
#define  UMC_GRCDE			0x450
#define  UMC_GRFCR			0x454
#define  UMC_GROVR			0x458
#define  UMC_GRJBR			0x45c
#define  UMC_GRMTUE			0x460
#define  UMC_GRPOK			0x464
#define  UMC_GRUC			0x468
#define  UMC_GRPPP			0x46c
#define  UMC_GRMCRC			0x470
#define  UMC_TR64			0x480
#define  UMC_TR127			0x484
#define  UMC_TR255			0x488
#define  UMC_TR511			0x48c
#define  UMC_TR1023			0x490
#define  UMC_TR1518			0x494
#define  UMC_TRMGV			0x498
#define  UMC_TR2047			0x49c
#define  UMC_TR4095			0x4a0
#define  UMC_TR9216			0x4a4
#define  UMC_GTPKT			0x4a8
#define  UMC_GTMCA			0x4ac
#define  UMC_GTBCA			0x4b0
#define  UMC_GTXPF			0x4b4
#define  UMC_GTXCF			0x4b8
#define  UMC_GTFCS			0x4bc
#define  UMC_GTOVR			0x4c0
#define  UMC_GTDRF			0x4c4
#define  UMC_GTEDF			0x4c8
#define  UMC_GTSCL			0x4cc
#define  UMC_GTMCL			0x4d0
#define  UMC_GTLCL			0x4d4
#define  UMC_GTXCL			0x4d8
#define  UMC_GTFRG			0x4dc
#define  UMC_GTNCL			0x4e0
#define  UMC_GTJBR			0x4e4
#define  UMC_GTBYT			0x4e8
#define  UMC_GTPOK			0x4ec
#define  UMC_GTUC			0x4f0
#define  UMC_RRPKT			0x500
#define  UMC_RRUND			0x504
#define  UMC_RRFRG			0x508
#define  UMC_RRBYT			0x50c
#define  UMC_MIB_CNTRL			0x580
#define   UMC_MIB_CNTRL_RX_CNT_RST	BIT(0)
#define   UMC_MIB_CNTRL_RUNT_CNT_RST	BIT(1)
#define   UMC_MIB_CNTRL_TX_CNT_RST	BIT(2)
#define  UMC_RX_MAX_PKT_SZ		0x608
#define  UMC_MPD_CTRL			0x620
#define   UMC_MPD_CTRL_MPD_EN		BIT(0)
#define   UMC_MPD_CTRL_PSW_EN		BIT(27)
#define  UMC_PSW_MS			0x624
#define  UMC_PSW_LS			0x628

#define UMAC2FB_OFFSET			0x9f044
#define  UMAC2FB_CFG			0x0
#define   UMAC2FB_CFG_OPUT_EN		BIT(0)
#define   UMAC2FB_CFG_VLAN_EN		BIT(1)
#define   UMAC2FB_CFG_SNAP_EN		BIT(2)
#define   UMAC2FB_CFG_BCM_TG_EN		BIT(3)
#define   UMAC2FB_CFG_IPUT_EN		BIT(4)
#define   UMAC2FB_CFG_CHID_SHIFT	8
#define   UMAC2FB_CFG_OK_SEND_SHIFT	24
#define   UMAC2FB_CFG_DEFAULT_EN	\
		(UMAC2FB_CFG_OPUT_EN | UMAC2FB_CFG_VLAN_EN \
		| UMAC2FB_CFG_SNAP_EN | UMAC2FB_CFG_IPUT_EN)

#define RGMII_OFFSET(intf)	\
	((((intf)->port) * 0x100) + 0xd000)
#define  RGMII_EPHY_CNTRL		0x00
#define    RGMII_EPHY_CFG_IDDQ_BIAS	BIT(0)
#define    RGMII_EPHY_CFG_EXT_PWRDOWN	BIT(1)
#define    RGMII_EPHY_CFG_FORCE_DLL_EN	BIT(2)
#define    RGMII_EPHY_CFG_IDDQ_GLOBAL	BIT(3)
#define    RGMII_EPHY_CK25_DIS		BIT(4)
#define    RGMII_EPHY_RESET		BIT(7)
#define  RGMII_OOB_CNTRL		0x0c
#define   RGMII_LINK			BIT(4)
#define   RGMII_OOB_DIS			BIT(5)
#define   RGMII_MODE_EN			BIT(6)
#define   RGMII_ID_MODE_DIS		BIT(16)

#define RGMII_PORT_CNTRL		0x60
#define   RGMII_PORT_MODE_EPHY		0
#define   RGMII_PORT_MODE_GPHY		1
#define   RGMII_PORT_MODE_EXT_EPHY	2
#define   RGMII_PORT_MODE_EXT_GPHY	3
#define   RGMII_PORT_MODE_EXT_RVMII	4
#define   RGMII_PORT_MODE_MASK		GENMASK(2, 0)

#define RGMII_SYS_LED_CNTRL		0x74
#define  RGMII_SYS_LED_CNTRL_LINK_OVRD	BIT(15)

#define TX_SPB_DMA_OFFSET(intf) \
	((((intf)->channel) * 0x30) + 0x48180)
#define  TX_SPB_DMA_READ		0x00
#define  TX_SPB_DMA_BASE		0x08
#define  TX_SPB_DMA_END			0x10
#define  TX_SPB_DMA_VALID		0x18
#define  TX_SPB_DMA_FIFO_CTRL		0x20
#define   TX_SPB_DMA_FIFO_FLUSH		BIT(0)
#define  TX_SPB_DMA_FIFO_STATUS		0x24

#define TX_SPB_CTRL_OFFSET(intf) \
	((((intf)->channel) * 0x68) + 0x49340)
#define  TX_SPB_CTRL_ENABLE		0x0
#define   TX_SPB_CTRL_ENABLE_EN		BIT(0)
#define  TX_SPB_CTRL_XF_CTRL2		0x20
#define   TX_SPB_CTRL_XF_BID_SHIFT	16

#define TX_SPB_TOP_OFFSET(intf) \
	((((intf)->channel) * 0x1c) + 0x4a0e0)
#define TX_SPB_TOP_BLKOUT		0x0
#define TX_SPB_TOP_SPRE_BW_CTRL		0x4

#define TX_EPKT_C_OFFSET(intf) \
	((((intf)->channel) * 0x120) + 0x40900)
#define  TX_EPKT_C_CFG_MISC		0x0
#define   TX_EPKT_C_CFG_MISC_EN		BIT(0)
#define   TX_EPKT_C_CFG_MISC_PT		BIT(1)
#define   TX_EPKT_C_CFG_MISC_PS_SHIFT	14
#define   TX_EPKT_C_CFG_MISC_FD_SHIFT	20

#define TX_PAUSE_CTRL_OFFSET(intf) \
	((((intf)->channel * 0xc) + 0x49a20))
#define  TX_PAUSE_MAP_VECTOR		0x8

#define RX_EDPKT_DMA_OFFSET(intf) \
	((((intf)->channel) * 0x38) + 0x9ca00)
#define  RX_EDPKT_DMA_WRITE		0x00
#define  RX_EDPKT_DMA_READ		0x08
#define  RX_EDPKT_DMA_BASE		0x10
#define  RX_EDPKT_DMA_END		0x18
#define  RX_EDPKT_DMA_VALID		0x20
#define  RX_EDPKT_DMA_FULLNESS		0x28
#define  RX_EDPKT_DMA_MIN_THRES		0x2c
#define  RX_EDPKT_DMA_CH_XONOFF		0x30

#define RX_EDPKT_CFG_OFFSET(intf) \
	((((intf)->channel) * 0x70) + 0x9c600)
#define  RX_EDPKT_CFG_CFG0		0x0
#define   RX_EDPKT_CFG_CFG0_DBUF_SHIFT	9
#define    RX_EDPKT_CFG_CFG0_RBUF	0x0
#define    RX_EDPKT_CFG_CFG0_RBUF_4K	0x1
#define    RX_EDPKT_CFG_CFG0_BUF_4K	0x2
/* EFRM STUFF, 0 = no byte stuff, 1 = two byte stuff */
#define   RX_EDPKT_CFG_CFG0_EFRM_STUF	BIT(11)
#define   RX_EDPKT_CFG_CFG0_BALN_SHIFT	12
#define    RX_EDPKT_CFG_CFG0_NO_ALN	0
#define    RX_EDPKT_CFG_CFG0_4_ALN	2
#define    RX_EDPKT_CFG_CFG0_64_ALN	6
#define  RX_EDPKT_RING_BUFFER_WRITE	0x38
#define  RX_EDPKT_RING_BUFFER_READ	0x40
#define  RX_EDPKT_RING_BUFFER_BASE	0x48
#define  RX_EDPKT_RING_BUFFER_END	0x50
#define  RX_EDPKT_RING_BUFFER_VALID	0x58
#define  RX_EDPKT_CFG_ENABLE		0x6c
#define   RX_EDPKT_CFG_ENABLE_EN	BIT(0)

#define RX_SPB_DMA_OFFSET(intf) \
	((((intf)->channel) * 0x30) + 0xa0000)
#define  RX_SPB_DMA_READ		0x00
#define  RX_SPB_DMA_BASE		0x08
#define  RX_SPB_DMA_END			0x10
#define  RX_SPB_DMA_VALID		0x18
#define  RX_SPB_DMA_FIFO_CTRL		0x20
#define   RX_SPB_DMA_FIFO_FLUSH		BIT(0)
#define  RX_SPB_DMA_FIFO_STATUS		0x24

#define RX_SPB_CTRL_OFFSET(intf) \
	((((intf)->channel - 6) * 0x68) + 0xa1000)
#define  RX_SPB_CTRL_ENABLE		0x00
#define   RX_SPB_CTRL_ENABLE_EN		BIT(0)

#define RX_PAUSE_CTRL_OFFSET(intf) \
	((((intf)->channel - 6) * 0x4) + 0xa1138)
#define  RX_PAUSE_MAP_VECTOR		0x00

#define RX_SPB_TOP_CTRL_OFFSET(intf) \
	((((intf)->channel - 6) * 0x14) + 0xa2000)
#define  RX_SPB_TOP_BLKOUT		0x00

#define NUM_4K_BUFFERS			32
#define RING_BUFFER_SIZE		(PAGE_SIZE * NUM_4K_BUFFERS)

#define DESC_RING_COUNT			(64 * NUM_4K_BUFFERS)
#define DESC_SIZE			16
#define DESC_RING_SIZE			(DESC_RING_COUNT * DESC_SIZE)

#endif
