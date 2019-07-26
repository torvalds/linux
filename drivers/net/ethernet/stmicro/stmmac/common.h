/*******************************************************************************
  STMMAC Common Header File

  Copyright (C) 2007-2009  STMicroelectronics Ltd

  This program is free software; you can redistribute it and/or modify it
  under the terms and conditions of the GNU General Public License,
  version 2, as published by the Free Software Foundation.

  This program is distributed in the hope it will be useful, but WITHOUT
  ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
  FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
  more details.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#ifndef __COMMON_H__
#define __COMMON_H__

#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/stmmac.h>
#include <linux/phy.h>
#include <linux/module.h>
#if IS_ENABLED(CONFIG_VLAN_8021Q)
#define STMMAC_VLAN_TAG_USED
#include <linux/if_vlan.h>
#endif

#include "descs.h"
#include "hwif.h"
#include "mmc.h"

/* Synopsys Core versions */
#define	DWMAC_CORE_3_40		0x34
#define	DWMAC_CORE_3_50		0x35
#define	DWMAC_CORE_4_00		0x40
#define DWMAC_CORE_4_10		0x41
#define DWMAC_CORE_5_00		0x50
#define DWMAC_CORE_5_10		0x51
#define DWXGMAC_CORE_2_10	0x21

#define STMMAC_CHAN0	0	/* Always supported and default for all chips */

/* These need to be power of two, and >= 4 */
#define DMA_TX_SIZE 512
#define DMA_RX_SIZE 512
#define STMMAC_GET_ENTRY(x, size)	((x + 1) & (size - 1))

#undef FRAME_FILTER_DEBUG
/* #define FRAME_FILTER_DEBUG */

/* Extra statistic and debug information exposed by ethtool */
struct stmmac_extra_stats {
	/* Transmit errors */
	unsigned long tx_underflow ____cacheline_aligned;
	unsigned long tx_carrier;
	unsigned long tx_losscarrier;
	unsigned long vlan_tag;
	unsigned long tx_deferred;
	unsigned long tx_vlan;
	unsigned long tx_jabber;
	unsigned long tx_frame_flushed;
	unsigned long tx_payload_error;
	unsigned long tx_ip_header_error;
	/* Receive errors */
	unsigned long rx_desc;
	unsigned long sa_filter_fail;
	unsigned long overflow_error;
	unsigned long ipc_csum_error;
	unsigned long rx_collision;
	unsigned long rx_crc_errors;
	unsigned long dribbling_bit;
	unsigned long rx_length;
	unsigned long rx_mii;
	unsigned long rx_multicast;
	unsigned long rx_gmac_overflow;
	unsigned long rx_watchdog;
	unsigned long da_rx_filter_fail;
	unsigned long sa_rx_filter_fail;
	unsigned long rx_missed_cntr;
	unsigned long rx_overflow_cntr;
	unsigned long rx_vlan;
	/* Tx/Rx IRQ error info */
	unsigned long tx_undeflow_irq;
	unsigned long tx_process_stopped_irq;
	unsigned long tx_jabber_irq;
	unsigned long rx_overflow_irq;
	unsigned long rx_buf_unav_irq;
	unsigned long rx_process_stopped_irq;
	unsigned long rx_watchdog_irq;
	unsigned long tx_early_irq;
	unsigned long fatal_bus_error_irq;
	/* Tx/Rx IRQ Events */
	unsigned long rx_early_irq;
	unsigned long threshold;
	unsigned long tx_pkt_n;
	unsigned long rx_pkt_n;
	unsigned long normal_irq_n;
	unsigned long rx_normal_irq_n;
	unsigned long napi_poll;
	unsigned long tx_normal_irq_n;
	unsigned long tx_clean;
	unsigned long tx_set_ic_bit;
	unsigned long irq_receive_pmt_irq_n;
	/* MMC info */
	unsigned long mmc_tx_irq_n;
	unsigned long mmc_rx_irq_n;
	unsigned long mmc_rx_csum_offload_irq_n;
	/* EEE */
	unsigned long irq_tx_path_in_lpi_mode_n;
	unsigned long irq_tx_path_exit_lpi_mode_n;
	unsigned long irq_rx_path_in_lpi_mode_n;
	unsigned long irq_rx_path_exit_lpi_mode_n;
	unsigned long phy_eee_wakeup_error_n;
	/* Extended RDES status */
	unsigned long ip_hdr_err;
	unsigned long ip_payload_err;
	unsigned long ip_csum_bypassed;
	unsigned long ipv4_pkt_rcvd;
	unsigned long ipv6_pkt_rcvd;
	unsigned long no_ptp_rx_msg_type_ext;
	unsigned long ptp_rx_msg_type_sync;
	unsigned long ptp_rx_msg_type_follow_up;
	unsigned long ptp_rx_msg_type_delay_req;
	unsigned long ptp_rx_msg_type_delay_resp;
	unsigned long ptp_rx_msg_type_pdelay_req;
	unsigned long ptp_rx_msg_type_pdelay_resp;
	unsigned long ptp_rx_msg_type_pdelay_follow_up;
	unsigned long ptp_rx_msg_type_announce;
	unsigned long ptp_rx_msg_type_management;
	unsigned long ptp_rx_msg_pkt_reserved_type;
	unsigned long ptp_frame_type;
	unsigned long ptp_ver;
	unsigned long timestamp_dropped;
	unsigned long av_pkt_rcvd;
	unsigned long av_tagged_pkt_rcvd;
	unsigned long vlan_tag_priority_val;
	unsigned long l3_filter_match;
	unsigned long l4_filter_match;
	unsigned long l3_l4_filter_no_match;
	/* PCS */
	unsigned long irq_pcs_ane_n;
	unsigned long irq_pcs_link_n;
	unsigned long irq_rgmii_n;
	unsigned long pcs_link;
	unsigned long pcs_duplex;
	unsigned long pcs_speed;
	/* debug register */
	unsigned long mtl_tx_status_fifo_full;
	unsigned long mtl_tx_fifo_not_empty;
	unsigned long mmtl_fifo_ctrl;
	unsigned long mtl_tx_fifo_read_ctrl_write;
	unsigned long mtl_tx_fifo_read_ctrl_wait;
	unsigned long mtl_tx_fifo_read_ctrl_read;
	unsigned long mtl_tx_fifo_read_ctrl_idle;
	unsigned long mac_tx_in_pause;
	unsigned long mac_tx_frame_ctrl_xfer;
	unsigned long mac_tx_frame_ctrl_idle;
	unsigned long mac_tx_frame_ctrl_wait;
	unsigned long mac_tx_frame_ctrl_pause;
	unsigned long mac_gmii_tx_proto_engine;
	unsigned long mtl_rx_fifo_fill_level_full;
	unsigned long mtl_rx_fifo_fill_above_thresh;
	unsigned long mtl_rx_fifo_fill_below_thresh;
	unsigned long mtl_rx_fifo_fill_level_empty;
	unsigned long mtl_rx_fifo_read_ctrl_flush;
	unsigned long mtl_rx_fifo_read_ctrl_read_data;
	unsigned long mtl_rx_fifo_read_ctrl_status;
	unsigned long mtl_rx_fifo_read_ctrl_idle;
	unsigned long mtl_rx_fifo_ctrl_active;
	unsigned long mac_rx_frame_ctrl_fifo;
	unsigned long mac_gmii_rx_proto_engine;
	/* TSO */
	unsigned long tx_tso_frames;
	unsigned long tx_tso_nfrags;
};

/* Safety Feature statistics exposed by ethtool */
struct stmmac_safety_stats {
	unsigned long mac_errors[32];
	unsigned long mtl_errors[32];
	unsigned long dma_errors[32];
};

/* Number of fields in Safety Stats */
#define STMMAC_SAFETY_FEAT_SIZE	\
	(sizeof(struct stmmac_safety_stats) / sizeof(unsigned long))

/* CSR Frequency Access Defines*/
#define CSR_F_35M	35000000
#define CSR_F_60M	60000000
#define CSR_F_100M	100000000
#define CSR_F_150M	150000000
#define CSR_F_250M	250000000
#define CSR_F_300M	300000000

#define	MAC_CSR_H_FRQ_MASK	0x20

#define HASH_TABLE_SIZE 64
#define PAUSE_TIME 0xffff

/* Flow Control defines */
#define FLOW_OFF	0
#define FLOW_RX		1
#define FLOW_TX		2
#define FLOW_AUTO	(FLOW_TX | FLOW_RX)

/* PCS defines */
#define STMMAC_PCS_RGMII	(1 << 0)
#define STMMAC_PCS_SGMII	(1 << 1)
#define STMMAC_PCS_TBI		(1 << 2)
#define STMMAC_PCS_RTBI		(1 << 3)

#define SF_DMA_MODE 1		/* DMA STORE-AND-FORWARD Operation Mode */

/* DAM HW feature register fields */
#define DMA_HW_FEAT_MIISEL	0x00000001	/* 10/100 Mbps Support */
#define DMA_HW_FEAT_GMIISEL	0x00000002	/* 1000 Mbps Support */
#define DMA_HW_FEAT_HDSEL	0x00000004	/* Half-Duplex Support */
#define DMA_HW_FEAT_EXTHASHEN	0x00000008	/* Expanded DA Hash Filter */
#define DMA_HW_FEAT_HASHSEL	0x00000010	/* HASH Filter */
#define DMA_HW_FEAT_ADDMAC	0x00000020	/* Multiple MAC Addr Reg */
#define DMA_HW_FEAT_PCSSEL	0x00000040	/* PCS registers */
#define DMA_HW_FEAT_L3L4FLTREN	0x00000080	/* Layer 3 & Layer 4 Feature */
#define DMA_HW_FEAT_SMASEL	0x00000100	/* SMA(MDIO) Interface */
#define DMA_HW_FEAT_RWKSEL	0x00000200	/* PMT Remote Wakeup */
#define DMA_HW_FEAT_MGKSEL	0x00000400	/* PMT Magic Packet */
#define DMA_HW_FEAT_MMCSEL	0x00000800	/* RMON Module */
#define DMA_HW_FEAT_TSVER1SEL	0x00001000	/* Only IEEE 1588-2002 */
#define DMA_HW_FEAT_TSVER2SEL	0x00002000	/* IEEE 1588-2008 PTPv2 */
#define DMA_HW_FEAT_EEESEL	0x00004000	/* Energy Efficient Ethernet */
#define DMA_HW_FEAT_AVSEL	0x00008000	/* AV Feature */
#define DMA_HW_FEAT_TXCOESEL	0x00010000	/* Checksum Offload in Tx */
#define DMA_HW_FEAT_RXTYP1COE	0x00020000	/* IP COE (Type 1) in Rx */
#define DMA_HW_FEAT_RXTYP2COE	0x00040000	/* IP COE (Type 2) in Rx */
#define DMA_HW_FEAT_RXFIFOSIZE	0x00080000	/* Rx FIFO > 2048 Bytes */
#define DMA_HW_FEAT_RXCHCNT	0x00300000	/* No. additional Rx Channels */
#define DMA_HW_FEAT_TXCHCNT	0x00c00000	/* No. additional Tx Channels */
#define DMA_HW_FEAT_ENHDESSEL	0x01000000	/* Alternate Descriptor */
/* Timestamping with Internal System Time */
#define DMA_HW_FEAT_INTTSEN	0x02000000
#define DMA_HW_FEAT_FLEXIPPSEN	0x04000000	/* Flexible PPS Output */
#define DMA_HW_FEAT_SAVLANINS	0x08000000	/* Source Addr or VLAN */
#define DMA_HW_FEAT_ACTPHYIF	0x70000000	/* Active/selected PHY iface */
#define DEFAULT_DMA_PBL		8

/* PCS status and mask defines */
#define	PCS_ANE_IRQ		BIT(2)	/* PCS Auto-Negotiation */
#define	PCS_LINK_IRQ		BIT(1)	/* PCS Link */
#define	PCS_RGSMIIIS_IRQ	BIT(0)	/* RGMII or SMII Interrupt */

/* Max/Min RI Watchdog Timer count value */
#define MAX_DMA_RIWT		0xff
#define MIN_DMA_RIWT		0x20
/* Tx coalesce parameters */
#define STMMAC_COAL_TX_TIMER	1000
#define STMMAC_MAX_COAL_TX_TICK	100000
#define STMMAC_TX_MAX_FRAMES	256
#define STMMAC_TX_FRAMES	1

/* Packets types */
enum packets_types {
	PACKET_AVCPQ = 0x1, /* AV Untagged Control packets */
	PACKET_PTPQ = 0x2, /* PTP Packets */
	PACKET_DCBCPQ = 0x3, /* DCB Control Packets */
	PACKET_UPQ = 0x4, /* Untagged Packets */
	PACKET_MCBCQ = 0x5, /* Multicast & Broadcast Packets */
};

/* Rx IPC status */
enum rx_frame_status {
	good_frame = 0x0,
	discard_frame = 0x1,
	csum_none = 0x2,
	llc_snap = 0x4,
	dma_own = 0x8,
	rx_not_ls = 0x10,
};

/* Tx status */
enum tx_frame_status {
	tx_done = 0x0,
	tx_not_ls = 0x1,
	tx_err = 0x2,
	tx_dma_own = 0x4,
};

enum dma_irq_status {
	tx_hard_error = 0x1,
	tx_hard_error_bump_tc = 0x2,
	handle_rx = 0x4,
	handle_tx = 0x8,
};

/* EEE and LPI defines */
#define	CORE_IRQ_TX_PATH_IN_LPI_MODE	(1 << 0)
#define	CORE_IRQ_TX_PATH_EXIT_LPI_MODE	(1 << 1)
#define	CORE_IRQ_RX_PATH_IN_LPI_MODE	(1 << 2)
#define	CORE_IRQ_RX_PATH_EXIT_LPI_MODE	(1 << 3)

#define CORE_IRQ_MTL_RX_OVERFLOW	BIT(8)

/* Physical Coding Sublayer */
struct rgmii_adv {
	unsigned int pause;
	unsigned int duplex;
	unsigned int lp_pause;
	unsigned int lp_duplex;
};

#define STMMAC_PCS_PAUSE	1
#define STMMAC_PCS_ASYM_PAUSE	2

/* DMA HW capabilities */
struct dma_features {
	unsigned int mbps_10_100;
	unsigned int mbps_1000;
	unsigned int half_duplex;
	unsigned int hash_filter;
	unsigned int multi_addr;
	unsigned int pcs;
	unsigned int sma_mdio;
	unsigned int pmt_remote_wake_up;
	unsigned int pmt_magic_frame;
	unsigned int rmon;
	/* IEEE 1588-2002 */
	unsigned int time_stamp;
	/* IEEE 1588-2008 */
	unsigned int atime_stamp;
	/* 802.3az - Energy-Efficient Ethernet (EEE) */
	unsigned int eee;
	unsigned int av;
	unsigned int tsoen;
	/* TX and RX csum */
	unsigned int tx_coe;
	unsigned int rx_coe;
	unsigned int rx_coe_type1;
	unsigned int rx_coe_type2;
	unsigned int rxfifo_over_2048;
	/* TX and RX number of channels */
	unsigned int number_rx_channel;
	unsigned int number_tx_channel;
	/* TX and RX number of queues */
	unsigned int number_rx_queues;
	unsigned int number_tx_queues;
	/* PPS output */
	unsigned int pps_out_num;
	/* Alternate (enhanced) DESC mode */
	unsigned int enh_desc;
	/* TX and RX FIFO sizes */
	unsigned int tx_fifo_size;
	unsigned int rx_fifo_size;
	/* Automotive Safety Package */
	unsigned int asp;
	/* RX Parser */
	unsigned int frpsel;
	unsigned int frpbs;
	unsigned int frpes;
};

/* GMAC TX FIFO is 8K, Rx FIFO is 16K */
#define BUF_SIZE_16KiB 16384
/* RX Buffer size must be < 8191 and multiple of 4/8/16 bytes */
#define BUF_SIZE_8KiB 8188
#define BUF_SIZE_4KiB 4096
#define BUF_SIZE_2KiB 2048

/* Power Down and WOL */
#define PMT_NOT_SUPPORTED 0
#define PMT_SUPPORTED 1

/* Common MAC defines */
#define MAC_CTRL_REG		0x00000000	/* MAC Control */
#define MAC_ENABLE_TX		0x00000008	/* Transmitter Enable */
#define MAC_ENABLE_RX		0x00000004	/* Receiver Enable */

/* Default LPI timers */
#define STMMAC_DEFAULT_LIT_LS	0x3E8
#define STMMAC_DEFAULT_TWT_LS	0x1E

#define STMMAC_CHAIN_MODE	0x1
#define STMMAC_RING_MODE	0x2

#define JUMBO_LEN		9000

extern const struct stmmac_desc_ops enh_desc_ops;
extern const struct stmmac_desc_ops ndesc_ops;

struct mac_device_info;

extern const struct stmmac_hwtimestamp stmmac_ptp;
extern const struct stmmac_mode_ops dwmac4_ring_mode_ops;

struct mac_link {
	u32 speed_mask;
	u32 speed10;
	u32 speed100;
	u32 speed1000;
	u32 speed2500;
	u32 speed10000;
	u32 duplex;
};

struct mii_regs {
	unsigned int addr;	/* MII Address */
	unsigned int data;	/* MII Data */
	unsigned int addr_shift;	/* MII address shift */
	unsigned int reg_shift;		/* MII reg shift */
	unsigned int addr_mask;		/* MII address mask */
	unsigned int reg_mask;		/* MII reg mask */
	unsigned int clk_csr_shift;
	unsigned int clk_csr_mask;
};

struct mac_device_info {
	const struct stmmac_ops *mac;
	const struct stmmac_desc_ops *desc;
	const struct stmmac_dma_ops *dma;
	const struct stmmac_mode_ops *mode;
	const struct stmmac_hwtimestamp *ptp;
	const struct stmmac_tc_ops *tc;
	struct mii_regs mii;	/* MII register Addresses */
	struct mac_link link;
	void __iomem *pcsr;     /* vpointer to device CSRs */
	int multicast_filter_bins;
	int unicast_filter_entries;
	int mcast_bits_log2;
	unsigned int rx_csum;
	unsigned int pcs;
	unsigned int pmt;
	unsigned int ps;
};

struct stmmac_rx_routing {
	u32 reg_mask;
	u32 reg_shift;
};

int dwmac100_setup(struct stmmac_priv *priv);
int dwmac1000_setup(struct stmmac_priv *priv);
int dwmac4_setup(struct stmmac_priv *priv);
int dwxgmac2_setup(struct stmmac_priv *priv);

void stmmac_set_mac_addr(void __iomem *ioaddr, u8 addr[6],
			 unsigned int high, unsigned int low);
void stmmac_get_mac_addr(void __iomem *ioaddr, unsigned char *addr,
			 unsigned int high, unsigned int low);
void stmmac_set_mac(void __iomem *ioaddr, bool enable);

void stmmac_dwmac4_set_mac_addr(void __iomem *ioaddr, u8 addr[6],
				unsigned int high, unsigned int low);
void stmmac_dwmac4_get_mac_addr(void __iomem *ioaddr, unsigned char *addr,
				unsigned int high, unsigned int low);
void stmmac_dwmac4_set_mac(void __iomem *ioaddr, bool enable);

void dwmac_dma_flush_tx_fifo(void __iomem *ioaddr);

extern const struct stmmac_mode_ops ring_mode_ops;
extern const struct stmmac_mode_ops chain_mode_ops;
extern const struct stmmac_desc_ops dwmac4_desc_ops;

#endif /* __COMMON_H__ */
