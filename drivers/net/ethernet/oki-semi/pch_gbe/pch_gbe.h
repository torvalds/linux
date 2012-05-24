/*
 * Copyright (C) 1999 - 2010 Intel Corporation.
 * Copyright (C) 2010 OKI SEMICONDUCTOR Co., LTD.
 *
 * This code was derived from the Intel e1000e Linux driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307, USA.
 */

#ifndef _PCH_GBE_H_
#define _PCH_GBE_H_

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/mii.h>
#include <linux/delay.h>
#include <linux/pci.h>
#include <linux/netdevice.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/vmalloc.h>
#include <net/ip.h>
#include <net/tcp.h>
#include <net/udp.h>

/**
 * pch_gbe_regs_mac_adr - Structure holding values of mac address registers
 * @high	Denotes the 1st to 4th byte from the initial of MAC address
 * @low		Denotes the 5th to 6th byte from the initial of MAC address
 */
struct pch_gbe_regs_mac_adr {
	u32 high;
	u32 low;
};
/**
 * pch_udc_regs - Structure holding values of MAC registers
 */
struct pch_gbe_regs {
	u32 INT_ST;
	u32 INT_EN;
	u32 MODE;
	u32 RESET;
	u32 TCPIP_ACC;
	u32 EX_LIST;
	u32 INT_ST_HOLD;
	u32 PHY_INT_CTRL;
	u32 MAC_RX_EN;
	u32 RX_FCTRL;
	u32 PAUSE_REQ;
	u32 RX_MODE;
	u32 TX_MODE;
	u32 RX_FIFO_ST;
	u32 TX_FIFO_ST;
	u32 TX_FID;
	u32 TX_RESULT;
	u32 PAUSE_PKT1;
	u32 PAUSE_PKT2;
	u32 PAUSE_PKT3;
	u32 PAUSE_PKT4;
	u32 PAUSE_PKT5;
	u32 reserve[2];
	struct pch_gbe_regs_mac_adr mac_adr[16];
	u32 ADDR_MASK;
	u32 MIIM;
	u32 MAC_ADDR_LOAD;
	u32 RGMII_ST;
	u32 RGMII_CTRL;
	u32 reserve3[3];
	u32 DMA_CTRL;
	u32 reserve4[3];
	u32 RX_DSC_BASE;
	u32 RX_DSC_SIZE;
	u32 RX_DSC_HW_P;
	u32 RX_DSC_HW_P_HLD;
	u32 RX_DSC_SW_P;
	u32 reserve5[3];
	u32 TX_DSC_BASE;
	u32 TX_DSC_SIZE;
	u32 TX_DSC_HW_P;
	u32 TX_DSC_HW_P_HLD;
	u32 TX_DSC_SW_P;
	u32 reserve6[3];
	u32 RX_DMA_ST;
	u32 TX_DMA_ST;
	u32 reserve7[2];
	u32 WOL_ST;
	u32 WOL_CTRL;
	u32 WOL_ADDR_MASK;
};

/* Interrupt Status */
/* Interrupt Status Hold */
/* Interrupt Enable */
#define PCH_GBE_INT_RX_DMA_CMPLT  0x00000001 /* Receive DMA Transfer Complete */
#define PCH_GBE_INT_RX_VALID      0x00000002 /* MAC Normal Receive Complete */
#define PCH_GBE_INT_RX_FRAME_ERR  0x00000004 /* Receive frame error */
#define PCH_GBE_INT_RX_FIFO_ERR   0x00000008 /* Receive FIFO Overflow */
#define PCH_GBE_INT_RX_DMA_ERR    0x00000010 /* Receive DMA Transfer Error */
#define PCH_GBE_INT_RX_DSC_EMP    0x00000020 /* Receive Descriptor Empty */
#define PCH_GBE_INT_TX_CMPLT      0x00000100 /* MAC Transmission Complete */
#define PCH_GBE_INT_TX_DMA_CMPLT  0x00000200 /* DMA Transfer Complete */
#define PCH_GBE_INT_TX_FIFO_ERR   0x00000400 /* Transmission FIFO underflow. */
#define PCH_GBE_INT_TX_DMA_ERR    0x00000800 /* Transmission DMA Error */
#define PCH_GBE_INT_PAUSE_CMPLT   0x00001000 /* Pause Transmission complete */
#define PCH_GBE_INT_MIIM_CMPLT    0x00010000 /* MIIM I/F Read completion */
#define PCH_GBE_INT_PHY_INT       0x00100000 /* Interruption from PHY */
#define PCH_GBE_INT_WOL_DET       0x01000000 /* Wake On LAN Event detection. */
#define PCH_GBE_INT_TCPIP_ERR     0x10000000 /* TCP/IP Accelerator Error */

/* Mode */
#define PCH_GBE_MODE_MII_ETHER      0x00000000  /* GIGA Ethernet Mode [MII] */
#define PCH_GBE_MODE_GMII_ETHER     0x80000000  /* GIGA Ethernet Mode [GMII] */
#define PCH_GBE_MODE_HALF_DUPLEX    0x00000000  /* Duplex Mode [half duplex] */
#define PCH_GBE_MODE_FULL_DUPLEX    0x40000000  /* Duplex Mode [full duplex] */
#define PCH_GBE_MODE_FR_BST         0x04000000  /* Frame bursting is done */

/* Reset */
#define PCH_GBE_ALL_RST         0x80000000  /* All reset */
#define PCH_GBE_TX_RST          0x00008000  /* TX MAC, TX FIFO, TX DMA reset */
#define PCH_GBE_RX_RST          0x00004000  /* RX MAC, RX FIFO, RX DMA reset */

/* TCP/IP Accelerator Control */
#define PCH_GBE_EX_LIST_EN      0x00000008  /* External List Enable */
#define PCH_GBE_RX_TCPIPACC_OFF 0x00000004  /* RX TCP/IP ACC Disabled */
#define PCH_GBE_TX_TCPIPACC_EN  0x00000002  /* TX TCP/IP ACC Enable */
#define PCH_GBE_RX_TCPIPACC_EN  0x00000001  /* RX TCP/IP ACC Enable */

/* MAC RX Enable */
#define PCH_GBE_MRE_MAC_RX_EN   0x00000001      /* MAC Receive Enable */

/* RX Flow Control */
#define PCH_GBE_FL_CTRL_EN      0x80000000  /* Pause packet is enabled */

/* Pause Packet Request */
#define PCH_GBE_PS_PKT_RQ       0x80000000  /* Pause packet Request */

/* RX Mode */
#define PCH_GBE_ADD_FIL_EN      0x80000000  /* Address Filtering Enable */
/* Multicast Filtering Enable */
#define PCH_GBE_MLT_FIL_EN      0x40000000
/* Receive Almost Empty Threshold */
#define PCH_GBE_RH_ALM_EMP_4    0x00000000      /* 4 words */
#define PCH_GBE_RH_ALM_EMP_8    0x00004000      /* 8 words */
#define PCH_GBE_RH_ALM_EMP_16   0x00008000      /* 16 words */
#define PCH_GBE_RH_ALM_EMP_32   0x0000C000      /* 32 words */
/* Receive Almost Full Threshold */
#define PCH_GBE_RH_ALM_FULL_4   0x00000000      /* 4 words */
#define PCH_GBE_RH_ALM_FULL_8   0x00001000      /* 8 words */
#define PCH_GBE_RH_ALM_FULL_16  0x00002000      /* 16 words */
#define PCH_GBE_RH_ALM_FULL_32  0x00003000      /* 32 words */
/* RX FIFO Read Triger Threshold */
#define PCH_GBE_RH_RD_TRG_4     0x00000000      /* 4 words */
#define PCH_GBE_RH_RD_TRG_8     0x00000200      /* 8 words */
#define PCH_GBE_RH_RD_TRG_16    0x00000400      /* 16 words */
#define PCH_GBE_RH_RD_TRG_32    0x00000600      /* 32 words */
#define PCH_GBE_RH_RD_TRG_64    0x00000800      /* 64 words */
#define PCH_GBE_RH_RD_TRG_128   0x00000A00      /* 128 words */
#define PCH_GBE_RH_RD_TRG_256   0x00000C00      /* 256 words */
#define PCH_GBE_RH_RD_TRG_512   0x00000E00      /* 512 words */

/* Receive Descriptor bit definitions */
#define PCH_GBE_RXD_ACC_STAT_BCAST          0x00000400
#define PCH_GBE_RXD_ACC_STAT_MCAST          0x00000200
#define PCH_GBE_RXD_ACC_STAT_UCAST          0x00000100
#define PCH_GBE_RXD_ACC_STAT_TCPIPOK        0x000000C0
#define PCH_GBE_RXD_ACC_STAT_IPOK           0x00000080
#define PCH_GBE_RXD_ACC_STAT_TCPOK          0x00000040
#define PCH_GBE_RXD_ACC_STAT_IP6ERR         0x00000020
#define PCH_GBE_RXD_ACC_STAT_OFLIST         0x00000010
#define PCH_GBE_RXD_ACC_STAT_TYPEIP         0x00000008
#define PCH_GBE_RXD_ACC_STAT_MACL           0x00000004
#define PCH_GBE_RXD_ACC_STAT_PPPOE          0x00000002
#define PCH_GBE_RXD_ACC_STAT_VTAGT          0x00000001
#define PCH_GBE_RXD_GMAC_STAT_PAUSE         0x0200
#define PCH_GBE_RXD_GMAC_STAT_MARBR         0x0100
#define PCH_GBE_RXD_GMAC_STAT_MARMLT        0x0080
#define PCH_GBE_RXD_GMAC_STAT_MARIND        0x0040
#define PCH_GBE_RXD_GMAC_STAT_MARNOTMT      0x0020
#define PCH_GBE_RXD_GMAC_STAT_TLONG         0x0010
#define PCH_GBE_RXD_GMAC_STAT_TSHRT         0x0008
#define PCH_GBE_RXD_GMAC_STAT_NOTOCTAL      0x0004
#define PCH_GBE_RXD_GMAC_STAT_NBLERR        0x0002
#define PCH_GBE_RXD_GMAC_STAT_CRCERR        0x0001

/* Transmit Descriptor bit definitions */
#define PCH_GBE_TXD_CTRL_TCPIP_ACC_OFF      0x0008
#define PCH_GBE_TXD_CTRL_ITAG               0x0004
#define PCH_GBE_TXD_CTRL_ICRC               0x0002
#define PCH_GBE_TXD_CTRL_APAD               0x0001
#define PCH_GBE_TXD_WORDS_SHIFT             2
#define PCH_GBE_TXD_GMAC_STAT_CMPLT         0x2000
#define PCH_GBE_TXD_GMAC_STAT_ABT           0x1000
#define PCH_GBE_TXD_GMAC_STAT_EXCOL         0x0800
#define PCH_GBE_TXD_GMAC_STAT_SNGCOL        0x0400
#define PCH_GBE_TXD_GMAC_STAT_MLTCOL        0x0200
#define PCH_GBE_TXD_GMAC_STAT_CRSER         0x0100
#define PCH_GBE_TXD_GMAC_STAT_TLNG          0x0080
#define PCH_GBE_TXD_GMAC_STAT_TSHRT         0x0040
#define PCH_GBE_TXD_GMAC_STAT_LTCOL         0x0020
#define PCH_GBE_TXD_GMAC_STAT_TFUNDFLW      0x0010
#define PCH_GBE_TXD_GMAC_STAT_RTYCNT_MASK   0x000F

/* TX Mode */
#define PCH_GBE_TM_NO_RTRY     0x80000000 /* No Retransmission */
#define PCH_GBE_TM_LONG_PKT    0x40000000 /* Long Packt TX Enable */
#define PCH_GBE_TM_ST_AND_FD   0x20000000 /* Stare and Forward */
#define PCH_GBE_TM_SHORT_PKT   0x10000000 /* Short Packet TX Enable */
#define PCH_GBE_TM_LTCOL_RETX  0x08000000 /* Retransmission at Late Collision */
/* Frame Start Threshold */
#define PCH_GBE_TM_TH_TX_STRT_4    0x00000000    /* 4 words */
#define PCH_GBE_TM_TH_TX_STRT_8    0x00004000    /* 8 words */
#define PCH_GBE_TM_TH_TX_STRT_16   0x00008000    /* 16 words */
#define PCH_GBE_TM_TH_TX_STRT_32   0x0000C000    /* 32 words */
/* Transmit Almost Empty Threshold */
#define PCH_GBE_TM_TH_ALM_EMP_4    0x00000000    /* 4 words */
#define PCH_GBE_TM_TH_ALM_EMP_8    0x00000800    /* 8 words */
#define PCH_GBE_TM_TH_ALM_EMP_16   0x00001000    /* 16 words */
#define PCH_GBE_TM_TH_ALM_EMP_32   0x00001800    /* 32 words */
#define PCH_GBE_TM_TH_ALM_EMP_64   0x00002000    /* 64 words */
#define PCH_GBE_TM_TH_ALM_EMP_128  0x00002800    /* 128 words */
#define PCH_GBE_TM_TH_ALM_EMP_256  0x00003000    /* 256 words */
#define PCH_GBE_TM_TH_ALM_EMP_512  0x00003800    /* 512 words */
/* Transmit Almost Full Threshold */
#define PCH_GBE_TM_TH_ALM_FULL_4   0x00000000    /* 4 words */
#define PCH_GBE_TM_TH_ALM_FULL_8   0x00000200    /* 8 words */
#define PCH_GBE_TM_TH_ALM_FULL_16  0x00000400    /* 16 words */
#define PCH_GBE_TM_TH_ALM_FULL_32  0x00000600    /* 32 words */

/* RX FIFO Status */
#define PCH_GBE_RF_ALM_FULL     0x80000000  /* RX FIFO is almost full. */
#define PCH_GBE_RF_ALM_EMP      0x40000000  /* RX FIFO is almost empty. */
#define PCH_GBE_RF_RD_TRG       0x20000000  /* Become more than RH_RD_TRG. */
#define PCH_GBE_RF_STRWD        0x1FFE0000  /* The word count of RX FIFO. */
#define PCH_GBE_RF_RCVING       0x00010000  /* Stored in RX FIFO. */

/* MAC Address Mask */
#define PCH_GBE_BUSY                0x80000000

/* MIIM  */
#define PCH_GBE_MIIM_OPER_WRITE     0x04000000
#define PCH_GBE_MIIM_OPER_READ      0x00000000
#define PCH_GBE_MIIM_OPER_READY     0x04000000
#define PCH_GBE_MIIM_PHY_ADDR_SHIFT 21
#define PCH_GBE_MIIM_REG_ADDR_SHIFT 16

/* RGMII Status */
#define PCH_GBE_LINK_UP             0x80000008
#define PCH_GBE_RXC_SPEED_MSK       0x00000006
#define PCH_GBE_RXC_SPEED_2_5M      0x00000000    /* 2.5MHz */
#define PCH_GBE_RXC_SPEED_25M       0x00000002    /* 25MHz  */
#define PCH_GBE_RXC_SPEED_125M      0x00000004    /* 100MHz */
#define PCH_GBE_DUPLEX_FULL         0x00000001

/* RGMII Control */
#define PCH_GBE_CRS_SEL             0x00000010
#define PCH_GBE_RGMII_RATE_125M     0x00000000
#define PCH_GBE_RGMII_RATE_25M      0x00000008
#define PCH_GBE_RGMII_RATE_2_5M     0x0000000C
#define PCH_GBE_RGMII_MODE_GMII     0x00000000
#define PCH_GBE_RGMII_MODE_RGMII    0x00000002
#define PCH_GBE_CHIP_TYPE_EXTERNAL  0x00000000
#define PCH_GBE_CHIP_TYPE_INTERNAL  0x00000001

/* DMA Control */
#define PCH_GBE_RX_DMA_EN       0x00000002   /* Enables Receive DMA */
#define PCH_GBE_TX_DMA_EN       0x00000001   /* Enables Transmission DMA */

/* RX DMA STATUS */
#define PCH_GBE_IDLE_CHECK       0xFFFFFFFE

/* Wake On LAN Status */
#define PCH_GBE_WLS_BR          0x00000008 /* Broadcas Address */
#define PCH_GBE_WLS_MLT         0x00000004 /* Multicast Address */

/* The Frame registered in Address Recognizer */
#define PCH_GBE_WLS_IND         0x00000002
#define PCH_GBE_WLS_MP          0x00000001 /* Magic packet Address */

/* Wake On LAN Control */
#define PCH_GBE_WLC_WOL_MODE    0x00010000
#define PCH_GBE_WLC_IGN_TLONG   0x00000100
#define PCH_GBE_WLC_IGN_TSHRT   0x00000080
#define PCH_GBE_WLC_IGN_OCTER   0x00000040
#define PCH_GBE_WLC_IGN_NBLER   0x00000020
#define PCH_GBE_WLC_IGN_CRCER   0x00000010
#define PCH_GBE_WLC_BR          0x00000008
#define PCH_GBE_WLC_MLT         0x00000004
#define PCH_GBE_WLC_IND         0x00000002
#define PCH_GBE_WLC_MP          0x00000001

/* Wake On LAN Address Mask */
#define PCH_GBE_WLA_BUSY        0x80000000



/* TX/RX descriptor defines */
#define PCH_GBE_MAX_TXD                     4096
#define PCH_GBE_DEFAULT_TXD                  256
#define PCH_GBE_MIN_TXD                        8
#define PCH_GBE_MAX_RXD                     4096
#define PCH_GBE_DEFAULT_RXD                  256
#define PCH_GBE_MIN_RXD                        8

/* Number of Transmit and Receive Descriptors must be a multiple of 8 */
#define PCH_GBE_TX_DESC_MULTIPLE               8
#define PCH_GBE_RX_DESC_MULTIPLE               8

/* Read/Write operation is done through MII Management IF */
#define PCH_GBE_HAL_MIIM_READ          ((u32)0x00000000)
#define PCH_GBE_HAL_MIIM_WRITE         ((u32)0x04000000)

/* flow control values */
#define PCH_GBE_FC_NONE			0
#define PCH_GBE_FC_RX_PAUSE		1
#define PCH_GBE_FC_TX_PAUSE		2
#define PCH_GBE_FC_FULL			3
#define PCH_GBE_FC_DEFAULT		PCH_GBE_FC_FULL


struct pch_gbe_hw;
/**
 * struct  pch_gbe_functions - HAL APi function pointer
 * @get_bus_info:	for pch_gbe_hal_get_bus_info
 * @init_hw:		for pch_gbe_hal_init_hw
 * @read_phy_reg:	for pch_gbe_hal_read_phy_reg
 * @write_phy_reg:	for pch_gbe_hal_write_phy_reg
 * @reset_phy:		for pch_gbe_hal_phy_hw_reset
 * @sw_reset_phy:	for pch_gbe_hal_phy_sw_reset
 * @power_up_phy:	for pch_gbe_hal_power_up_phy
 * @power_down_phy:	for pch_gbe_hal_power_down_phy
 * @read_mac_addr:	for pch_gbe_hal_read_mac_addr
 */
struct pch_gbe_functions {
	void (*get_bus_info) (struct pch_gbe_hw *);
	s32 (*init_hw) (struct pch_gbe_hw *);
	s32 (*read_phy_reg) (struct pch_gbe_hw *, u32, u16 *);
	s32 (*write_phy_reg) (struct pch_gbe_hw *, u32, u16);
	void (*reset_phy) (struct pch_gbe_hw *);
	void (*sw_reset_phy) (struct pch_gbe_hw *);
	void (*power_up_phy) (struct pch_gbe_hw *hw);
	void (*power_down_phy) (struct pch_gbe_hw *hw);
	s32 (*read_mac_addr) (struct pch_gbe_hw *);
};

/**
 * struct pch_gbe_mac_info - MAC information
 * @addr[6]:		Store the MAC address
 * @fc:			Mode of flow control
 * @fc_autoneg:		Auto negotiation enable for flow control setting
 * @tx_fc_enable:	Enable flag of Transmit flow control
 * @max_frame_size:	Max transmit frame size
 * @min_frame_size:	Min transmit frame size
 * @autoneg:		Auto negotiation enable
 * @link_speed:		Link speed
 * @link_duplex:	Link duplex
 */
struct pch_gbe_mac_info {
	u8 addr[6];
	u8 fc;
	u8 fc_autoneg;
	u8 tx_fc_enable;
	u32 max_frame_size;
	u32 min_frame_size;
	u8 autoneg;
	u16 link_speed;
	u16 link_duplex;
};

/**
 * struct pch_gbe_phy_info - PHY information
 * @addr:		PHY address
 * @id:			PHY's identifier
 * @revision:		PHY's revision
 * @reset_delay_us:	HW reset delay time[us]
 * @autoneg_advertised:	Autoneg advertised
 */
struct pch_gbe_phy_info {
	u32 addr;
	u32 id;
	u32 revision;
	u32 reset_delay_us;
	u16 autoneg_advertised;
};

/*!
 * @ingroup Gigabit Ether driver Layer
 * @struct  pch_gbe_bus_info
 * @brief   Bus information
 */
struct pch_gbe_bus_info {
	u8 type;
	u8 speed;
	u8 width;
};

/*!
 * @ingroup Gigabit Ether driver Layer
 * @struct  pch_gbe_hw
 * @brief   Hardware information
 */
struct pch_gbe_hw {
	void *back;

	struct pch_gbe_regs  __iomem *reg;
	spinlock_t miim_lock;

	const struct pch_gbe_functions *func;
	struct pch_gbe_mac_info mac;
	struct pch_gbe_phy_info phy;
	struct pch_gbe_bus_info bus;
};

/**
 * struct pch_gbe_rx_desc - Receive Descriptor
 * @buffer_addr:	RX Frame Buffer Address
 * @tcp_ip_status:	TCP/IP Accelerator Status
 * @rx_words_eob:	RX word count and Byte position
 * @gbec_status:	GMAC Status
 * @dma_status:		DMA Status
 * @reserved1:		Reserved
 * @reserved2:		Reserved
 */
struct pch_gbe_rx_desc {
	u32 buffer_addr;
	u32 tcp_ip_status;
	u16 rx_words_eob;
	u16 gbec_status;
	u8 dma_status;
	u8 reserved1;
	u16 reserved2;
};

/**
 * struct pch_gbe_tx_desc - Transmit Descriptor
 * @buffer_addr:	TX Frame Buffer Address
 * @length:		Data buffer length
 * @reserved1:		Reserved
 * @tx_words_eob:	TX word count and Byte position
 * @tx_frame_ctrl:	TX Frame Control
 * @dma_status:		DMA Status
 * @reserved2:		Reserved
 * @gbec_status:	GMAC Status
 */
struct pch_gbe_tx_desc {
	u32 buffer_addr;
	u16 length;
	u16 reserved1;
	u16 tx_words_eob;
	u16 tx_frame_ctrl;
	u8 dma_status;
	u8 reserved2;
	u16 gbec_status;
};


/**
 * struct pch_gbe_buffer - Buffer information
 * @skb:	pointer to a socket buffer
 * @dma:	DMA address
 * @time_stamp:	time stamp
 * @length:	data size
 */
struct pch_gbe_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	unsigned char *rx_buffer;
	unsigned long time_stamp;
	u16 length;
	bool mapped;
};

/**
 * struct pch_gbe_tx_ring - tx ring information
 * @tx_lock:	spinlock structs
 * @desc:	pointer to the descriptor ring memory
 * @dma:	physical address of the descriptor ring
 * @size:	length of descriptor ring in bytes
 * @count:	number of descriptors in the ring
 * @next_to_use:	next descriptor to associate a buffer with
 * @next_to_clean:	next descriptor to check for DD status bit
 * @buffer_info:	array of buffer information structs
 */
struct pch_gbe_tx_ring {
	spinlock_t tx_lock;
	struct pch_gbe_tx_desc *desc;
	dma_addr_t dma;
	unsigned int size;
	unsigned int count;
	unsigned int next_to_use;
	unsigned int next_to_clean;
	struct pch_gbe_buffer *buffer_info;
};

/**
 * struct pch_gbe_rx_ring - rx ring information
 * @desc:	pointer to the descriptor ring memory
 * @dma:	physical address of the descriptor ring
 * @size:	length of descriptor ring in bytes
 * @count:	number of descriptors in the ring
 * @next_to_use:	next descriptor to associate a buffer with
 * @next_to_clean:	next descriptor to check for DD status bit
 * @buffer_info:	array of buffer information structs
 */
struct pch_gbe_rx_ring {
	struct pch_gbe_rx_desc *desc;
	dma_addr_t dma;
	unsigned char *rx_buff_pool;
	dma_addr_t rx_buff_pool_logic;
	unsigned int rx_buff_pool_size;
	unsigned int size;
	unsigned int count;
	unsigned int next_to_use;
	unsigned int next_to_clean;
	struct pch_gbe_buffer *buffer_info;
};

/**
 * struct pch_gbe_hw_stats - Statistics counters collected by the MAC
 * @rx_packets:		    total packets received
 * @tx_packets:		    total packets transmitted
 * @rx_bytes:		    total bytes received
 * @tx_bytes:		    total bytes transmitted
 * @rx_errors:		    bad packets received
 * @tx_errors:		    packet transmit problems
 * @rx_dropped:		    no space in Linux buffers
 * @tx_dropped:		    no space available in Linux
 * @multicast:		    multicast packets received
 * @collisions:		    collisions
 * @rx_crc_errors:	    received packet with crc error
 * @rx_frame_errors:	    received frame alignment error
 * @rx_alloc_buff_failed:   allocate failure of a receive buffer
 * @tx_length_errors:	    transmit length error
 * @tx_aborted_errors:	    transmit aborted error
 * @tx_carrier_errors:	    transmit carrier error
 * @tx_timeout_count:	    Number of transmit timeout
 * @tx_restart_count:	    Number of transmit restert
 * @intr_rx_dsc_empty_count:	Interrupt count of receive descriptor empty
 * @intr_rx_frame_err_count:	Interrupt count of receive frame error
 * @intr_rx_fifo_err_count:	Interrupt count of receive FIFO error
 * @intr_rx_dma_err_count:	Interrupt count of receive DMA error
 * @intr_tx_fifo_err_count:	Interrupt count of transmit FIFO error
 * @intr_tx_dma_err_count:	Interrupt count of transmit DMA error
 * @intr_tcpip_err_count:	Interrupt count of TCP/IP Accelerator
 */
struct pch_gbe_hw_stats {
	u32 rx_packets;
	u32 tx_packets;
	u32 rx_bytes;
	u32 tx_bytes;
	u32 rx_errors;
	u32 tx_errors;
	u32 rx_dropped;
	u32 tx_dropped;
	u32 multicast;
	u32 collisions;
	u32 rx_crc_errors;
	u32 rx_frame_errors;
	u32 rx_alloc_buff_failed;
	u32 tx_length_errors;
	u32 tx_aborted_errors;
	u32 tx_carrier_errors;
	u32 tx_timeout_count;
	u32 tx_restart_count;
	u32 intr_rx_dsc_empty_count;
	u32 intr_rx_frame_err_count;
	u32 intr_rx_fifo_err_count;
	u32 intr_rx_dma_err_count;
	u32 intr_tx_fifo_err_count;
	u32 intr_tx_dma_err_count;
	u32 intr_tcpip_err_count;
};

/**
 * struct pch_gbe_adapter - board specific private data structure
 * @stats_lock:	Spinlock structure for status
 * @tx_queue_lock:	Spinlock structure for transmit
 * @ethtool_lock:	Spinlock structure for ethtool
 * @irq_sem:		Semaphore for interrupt
 * @netdev:		Pointer of network device structure
 * @pdev:		Pointer of pci device structure
 * @polling_netdev:	Pointer of polling network device structure
 * @napi:		NAPI structure
 * @hw:			Pointer of hardware structure
 * @stats:		Hardware status
 * @reset_task:		Reset task
 * @mii:		MII information structure
 * @watchdog_timer:	Watchdog timer list
 * @wake_up_evt:	Wake up event
 * @config_space:	Configuration space
 * @msg_enable:		Driver message level
 * @led_status:		LED status
 * @tx_ring:		Pointer of Tx descriptor ring structure
 * @rx_ring:		Pointer of Rx descriptor ring structure
 * @rx_buffer_len:	Receive buffer length
 * @tx_queue_len:	Transmit queue length
 * @have_msi:		PCI MSI mode flag
 */

struct pch_gbe_adapter {
	spinlock_t stats_lock;
	spinlock_t tx_queue_lock;
	spinlock_t ethtool_lock;
	atomic_t irq_sem;
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct net_device *polling_netdev;
	struct napi_struct napi;
	struct pch_gbe_hw hw;
	struct pch_gbe_hw_stats stats;
	struct work_struct reset_task;
	struct mii_if_info mii;
	struct timer_list watchdog_timer;
	u32 wake_up_evt;
	u32 *config_space;
	unsigned long led_status;
	struct pch_gbe_tx_ring *tx_ring;
	struct pch_gbe_rx_ring *rx_ring;
	unsigned long rx_buffer_len;
	unsigned long tx_queue_len;
	bool have_msi;
	bool rx_stop_flag;
	int hwts_tx_en;
	int hwts_rx_en;
	struct pci_dev *ptp_pdev;
};

extern const char pch_driver_version[];

/* pch_gbe_main.c */
extern int pch_gbe_up(struct pch_gbe_adapter *adapter);
extern void pch_gbe_down(struct pch_gbe_adapter *adapter);
extern void pch_gbe_reinit_locked(struct pch_gbe_adapter *adapter);
extern void pch_gbe_reset(struct pch_gbe_adapter *adapter);
extern int pch_gbe_setup_tx_resources(struct pch_gbe_adapter *adapter,
				       struct pch_gbe_tx_ring *txdr);
extern int pch_gbe_setup_rx_resources(struct pch_gbe_adapter *adapter,
				       struct pch_gbe_rx_ring *rxdr);
extern void pch_gbe_free_tx_resources(struct pch_gbe_adapter *adapter,
				       struct pch_gbe_tx_ring *tx_ring);
extern void pch_gbe_free_rx_resources(struct pch_gbe_adapter *adapter,
				       struct pch_gbe_rx_ring *rx_ring);
extern void pch_gbe_update_stats(struct pch_gbe_adapter *adapter);
#ifdef CONFIG_PCH_PTP
extern u32 pch_ch_control_read(struct pci_dev *pdev);
extern void pch_ch_control_write(struct pci_dev *pdev, u32 val);
extern u32 pch_ch_event_read(struct pci_dev *pdev);
extern void pch_ch_event_write(struct pci_dev *pdev, u32 val);
extern u32 pch_src_uuid_lo_read(struct pci_dev *pdev);
extern u32 pch_src_uuid_hi_read(struct pci_dev *pdev);
extern u64 pch_rx_snap_read(struct pci_dev *pdev);
extern u64 pch_tx_snap_read(struct pci_dev *pdev);
#endif

/* pch_gbe_param.c */
extern void pch_gbe_check_options(struct pch_gbe_adapter *adapter);

/* pch_gbe_ethtool.c */
extern void pch_gbe_set_ethtool_ops(struct net_device *netdev);

/* pch_gbe_mac.c */
extern s32 pch_gbe_mac_force_mac_fc(struct pch_gbe_hw *hw);
extern s32 pch_gbe_mac_read_mac_addr(struct pch_gbe_hw *hw);
extern u16 pch_gbe_mac_ctrl_miim(struct pch_gbe_hw *hw,
				  u32 addr, u32 dir, u32 reg, u16 data);
#endif /* _PCH_GBE_H_ */
