/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef __BCM4908_ENET_H
#define __BCM4908_ENET_H

#define ENET_CONTROL					0x000
#define ENET_MIB_CTRL					0x004
#define  ENET_MIB_CTRL_CLR_MIB				0x00000001
#define ENET_RX_ERR_MASK				0x008
#define ENET_MIB_MAX_PKT_SIZE				0x00C
#define  ENET_MIB_MAX_PKT_SIZE_VAL			0x00003fff
#define ENET_DIAG_OUT					0x01c
#define ENET_ENABLE_DROP_PKT				0x020
#define ENET_IRQ_ENABLE					0x024
#define  ENET_IRQ_ENABLE_OVFL				0x00000001
#define ENET_GMAC_STATUS				0x028
#define  ENET_GMAC_STATUS_ETH_SPEED_MASK		0x00000003
#define  ENET_GMAC_STATUS_ETH_SPEED_10			0x00000000
#define  ENET_GMAC_STATUS_ETH_SPEED_100			0x00000001
#define  ENET_GMAC_STATUS_ETH_SPEED_1000		0x00000002
#define  ENET_GMAC_STATUS_HD				0x00000004
#define  ENET_GMAC_STATUS_AUTO_CFG_EN			0x00000008
#define  ENET_GMAC_STATUS_LINK_UP			0x00000010
#define ENET_IRQ_STATUS					0x02c
#define  ENET_IRQ_STATUS_OVFL				0x00000001
#define ENET_OVERFLOW_COUNTER				0x030
#define ENET_FLUSH					0x034
#define  ENET_FLUSH_RXFIFO_FLUSH			0x00000001
#define  ENET_FLUSH_TXFIFO_FLUSH			0x00000002
#define ENET_RSV_SELECT					0x038
#define ENET_BP_FORCE					0x03c
#define  ENET_BP_FORCE_FORCE				0x00000001
#define ENET_DMA_RX_OK_TO_SEND_COUNT			0x040
#define  ENET_DMA_RX_OK_TO_SEND_COUNT_VAL		0x0000000f
#define ENET_TX_CRC_CTRL				0x044
#define ENET_MIB					0x200
#define ENET_UNIMAC					0x400
#define ENET_DMA					0x800
#define ENET_DMA_CONTROLLER_CFG				0x800
#define  ENET_DMA_CTRL_CFG_MASTER_EN			0x00000001
#define  ENET_DMA_CTRL_CFG_FLOWC_CH1_EN			0x00000002
#define  ENET_DMA_CTRL_CFG_FLOWC_CH3_EN			0x00000004
#define ENET_DMA_FLOWCTL_CH1_THRESH_LO			0x804
#define ENET_DMA_FLOWCTL_CH1_THRESH_HI			0x808
#define ENET_DMA_FLOWCTL_CH1_ALLOC			0x80c
#define  ENET_DMA_FLOWCTL_CH1_ALLOC_FORCE		0x80000000
#define ENET_DMA_FLOWCTL_CH3_THRESH_LO			0x810
#define ENET_DMA_FLOWCTL_CH3_THRESH_HI			0x814
#define ENET_DMA_FLOWCTL_CH3_ALLOC			0x818
#define ENET_DMA_FLOWCTL_CH5_THRESH_LO			0x81C
#define ENET_DMA_FLOWCTL_CH5_THRESH_HI			0x820
#define ENET_DMA_FLOWCTL_CH5_ALLOC			0x824
#define ENET_DMA_FLOWCTL_CH7_THRESH_LO			0x828
#define ENET_DMA_FLOWCTL_CH7_THRESH_HI			0x82C
#define ENET_DMA_FLOWCTL_CH7_ALLOC			0x830
#define ENET_DMA_CTRL_CHANNEL_RESET			0x834
#define ENET_DMA_CTRL_CHANNEL_DEBUG			0x838
#define ENET_DMA_CTRL_GLOBAL_INTERRUPT_STATUS		0x840
#define ENET_DMA_CTRL_GLOBAL_INTERRUPT_MASK		0x844
#define ENET_DMA_CH0_CFG				0xa00		/* RX */
#define ENET_DMA_CH1_CFG				0xa10		/* TX */
#define ENET_DMA_CH0_STATE_RAM				0xc00		/* RX */
#define ENET_DMA_CH1_STATE_RAM				0xc10		/* TX */

#define ENET_DMA_CH_CFG					0x00		/* assorted configuration */
#define  ENET_DMA_CH_CFG_ENABLE				0x00000001	/* set to enable channel */
#define  ENET_DMA_CH_CFG_PKT_HALT			0x00000002	/* idle after an EOP flag is detected */
#define  ENET_DMA_CH_CFG_BURST_HALT			0x00000004	/* idle after finish current memory burst */
#define ENET_DMA_CH_CFG_INT_STAT			0x04		/* interrupts control and status */
#define ENET_DMA_CH_CFG_INT_MASK			0x08		/* interrupts mask */
#define  ENET_DMA_CH_CFG_INT_BUFF_DONE			0x00000001	/* buffer done */
#define  ENET_DMA_CH_CFG_INT_DONE			0x00000002	/* packet xfer complete */
#define  ENET_DMA_CH_CFG_INT_NO_DESC			0x00000004	/* no valid descriptors */
#define  ENET_DMA_CH_CFG_INT_RX_ERROR			0x00000008	/* rxdma detect client protocol error */
#define ENET_DMA_CH_CFG_MAX_BURST			0x0c		/* max burst length permitted */
#define  ENET_DMA_CH_CFG_MAX_BURST_DESCSIZE_SEL		0x00040000	/* DMA Descriptor Size Selection */
#define ENET_DMA_CH_CFG_SIZE				0x10

#define ENET_DMA_CH_STATE_RAM_BASE_DESC_PTR		0x00		/* descriptor ring start address */
#define ENET_DMA_CH_STATE_RAM_STATE_DATA		0x04		/* state/bytes done/ring offset */
#define ENET_DMA_CH_STATE_RAM_DESC_LEN_STATUS		0x08		/* buffer descriptor status and len */
#define ENET_DMA_CH_STATE_RAM_DESC_BASE_BUFPTR		0x0c		/* buffer descrpitor current processing */
#define ENET_DMA_CH_STATE_RAM_SIZE			0x10

#define DMA_CTL_STATUS_APPEND_CRC			0x00000100
#define DMA_CTL_STATUS_APPEND_BRCM_TAG			0x00000200
#define DMA_CTL_STATUS_PRIO				0x00000C00  /* Prio for Tx */
#define DMA_CTL_STATUS_WRAP				0x00001000  /* */
#define DMA_CTL_STATUS_SOP				0x00002000  /* first buffer in packet */
#define DMA_CTL_STATUS_EOP				0x00004000  /* last buffer in packet */
#define DMA_CTL_STATUS_OWN				0x00008000  /* cleared by DMA, set by SW */
#define DMA_CTL_LEN_DESC_BUFLENGTH			0x0fff0000
#define DMA_CTL_LEN_DESC_BUFLENGTH_SHIFT		16
#define DMA_CTL_LEN_DESC_MULTICAST			0x40000000
#define DMA_CTL_LEN_DESC_USEFPM				0x80000000

#endif
