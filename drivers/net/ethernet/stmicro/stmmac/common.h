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

  You should have received a copy of the GNU General Public License along with
  this program; if not, write to the Free Software Foundation, Inc.,
  51 Franklin St - Fifth Floor, Boston, MA 02110-1301 USA.

  The full GNU General Public License is included in this distribution in
  the file called "COPYING".

  Author: Giuseppe Cavallaro <peppe.cavallaro@st.com>
*******************************************************************************/

#ifndef __COMMON_H__
#define __COMMON_H__

#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/module.h>
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
#define STMMAC_VLAN_TAG_USED
#include <linux/if_vlan.h>
#endif

#include "descs.h"
#include "mmc.h"

/* Synopsys Core versions */
#define	DWMAC_CORE_3_40	0x34
#define	DWMAC_CORE_3_50	0x35

#undef FRAME_FILTER_DEBUG
/* #define FRAME_FILTER_DEBUG */

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
	unsigned long rx_crc;
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
	unsigned long tx_reset_ic_bit;
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
	unsigned long rx_msg_type_ext_no_ptp;
	unsigned long rx_msg_type_sync;
	unsigned long rx_msg_type_follow_up;
	unsigned long rx_msg_type_delay_req;
	unsigned long rx_msg_type_delay_resp;
	unsigned long rx_msg_type_pdelay_req;
	unsigned long rx_msg_type_pdelay_resp;
	unsigned long rx_msg_type_pdelay_follow_up;
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
};

/* CSR Frequency Access Defines*/
#define CSR_F_35M	35000000
#define CSR_F_60M	60000000
#define CSR_F_100M	100000000
#define CSR_F_150M	150000000
#define CSR_F_250M	250000000
#define CSR_F_300M	300000000

#define	MAC_CSR_H_FRQ_MASK	0x20

#define HASH_TABLE_SIZE 64
#define PAUSE_TIME 0x200

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

/* Max/Min RI Watchdog Timer count value */
#define MAX_DMA_RIWT		0xff
#define MIN_DMA_RIWT		0x20
/* Tx coalesce parameters */
#define STMMAC_COAL_TX_TIMER	40000
#define STMMAC_MAX_COAL_TX_TICK	100000
#define STMMAC_TX_MAX_FRAMES	256
#define STMMAC_TX_FRAMES	64

/* Rx IPC status */
enum rx_frame_status {
	good_frame = 0,
	discard_frame = 1,
	csum_none = 2,
	llc_snap = 4,
};

enum dma_irq_status {
	tx_hard_error = 0x1,
	tx_hard_error_bump_tc = 0x2,
	handle_rx = 0x4,
	handle_tx = 0x8,
};

#define	CORE_IRQ_TX_PATH_IN_LPI_MODE	(1 << 1)
#define	CORE_IRQ_TX_PATH_EXIT_LPI_MODE	(1 << 2)
#define	CORE_IRQ_RX_PATH_IN_LPI_MODE	(1 << 3)
#define	CORE_IRQ_RX_PATH_EXIT_LPI_MODE	(1 << 4)

#define	CORE_PCS_ANE_COMPLETE		(1 << 5)
#define	CORE_PCS_LINK_STATUS		(1 << 6)
#define	CORE_RGMII_IRQ			(1 << 7)

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
	/* TX and RX csum */
	unsigned int tx_coe;
	unsigned int rx_coe_type1;
	unsigned int rx_coe_type2;
	unsigned int rxfifo_over_2048;
	/* TX and RX number of channels */
	unsigned int number_rx_channel;
	unsigned int number_tx_channel;
	/* Alternate (enhanced) DESC mode */
	unsigned int enh_desc;
};

/* GMAC TX FIFO is 8K, Rx FIFO is 16K */
#define BUF_SIZE_16KiB 16384
#define BUF_SIZE_8KiB 8192
#define BUF_SIZE_4KiB 4096
#define BUF_SIZE_2KiB 2048

/* Power Down and WOL */
#define PMT_NOT_SUPPORTED 0
#define PMT_SUPPORTED 1

/* Common MAC defines */
#define MAC_CTRL_REG		0x00000000	/* MAC Control */
#define MAC_ENABLE_TX		0x00000008	/* Transmitter Enable */
#define MAC_RNABLE_RX		0x00000004	/* Receiver Enable */

/* Default LPI timers */
#define STMMAC_DEFAULT_LIT_LS	0x3E8
#define STMMAC_DEFAULT_TWT_LS	0x0

#define STMMAC_CHAIN_MODE	0x1
#define STMMAC_RING_MODE	0x2

#define JUMBO_LEN		9000

struct stmmac_desc_ops {
	/* DMA RX descriptor ring initialization */
	void (*init_rx_desc) (struct dma_desc *p, int disable_rx_ic, int mode,
			      int end);
	/* DMA TX descriptor ring initialization */
	void (*init_tx_desc) (struct dma_desc *p, int mode, int end);

	/* Invoked by the xmit function to prepare the tx descriptor */
	void (*prepare_tx_desc) (struct dma_desc *p, int is_fs, int len,
				 int csum_flag, int mode);
	/* Set/get the owner of the descriptor */
	void (*set_tx_owner) (struct dma_desc *p);
	int (*get_tx_owner) (struct dma_desc *p);
	/* Invoked by the xmit function to close the tx descriptor */
	void (*close_tx_desc) (struct dma_desc *p);
	/* Clean the tx descriptor as soon as the tx irq is received */
	void (*release_tx_desc) (struct dma_desc *p, int mode);
	/* Clear interrupt on tx frame completion. When this bit is
	 * set an interrupt happens as soon as the frame is transmitted */
	void (*clear_tx_ic) (struct dma_desc *p);
	/* Last tx segment reports the transmit status */
	int (*get_tx_ls) (struct dma_desc *p);
	/* Return the transmit status looking at the TDES1 */
	int (*tx_status) (void *data, struct stmmac_extra_stats *x,
			  struct dma_desc *p, void __iomem *ioaddr);
	/* Get the buffer size from the descriptor */
	int (*get_tx_len) (struct dma_desc *p);
	/* Handle extra events on specific interrupts hw dependent */
	int (*get_rx_owner) (struct dma_desc *p);
	void (*set_rx_owner) (struct dma_desc *p);
	/* Get the receive frame size */
	int (*get_rx_frame_len) (struct dma_desc *p, int rx_coe_type);
	/* Return the reception status looking at the RDES1 */
	int (*rx_status) (void *data, struct stmmac_extra_stats *x,
			  struct dma_desc *p);
	void (*rx_extended_status) (void *data, struct stmmac_extra_stats *x,
				    struct dma_extended_desc *p);
	/* Set tx timestamp enable bit */
	void (*enable_tx_timestamp) (struct dma_desc *p);
	/* get tx timestamp status */
	int (*get_tx_timestamp_status) (struct dma_desc *p);
	/* get timestamp value */
	 u64(*get_timestamp) (void *desc, u32 ats);
	/* get rx timestamp status */
	int (*get_rx_timestamp_status) (void *desc, u32 ats);
};

struct stmmac_dma_ops {
	/* DMA core initialization */
	int (*init) (void __iomem *ioaddr, int pbl, int fb, int mb,
		     int burst_len, u32 dma_tx, u32 dma_rx, int atds);
	/* Dump DMA registers */
	void (*dump_regs) (void __iomem *ioaddr);
	/* Set tx/rx threshold in the csr6 register
	 * An invalid value enables the store-and-forward mode */
	void (*dma_mode) (void __iomem *ioaddr, int txmode, int rxmode);
	/* To track extra statistic (if supported) */
	void (*dma_diagnostic_fr) (void *data, struct stmmac_extra_stats *x,
				   void __iomem *ioaddr);
	void (*enable_dma_transmission) (void __iomem *ioaddr);
	void (*enable_dma_irq) (void __iomem *ioaddr);
	void (*disable_dma_irq) (void __iomem *ioaddr);
	void (*start_tx) (void __iomem *ioaddr);
	void (*stop_tx) (void __iomem *ioaddr);
	void (*start_rx) (void __iomem *ioaddr);
	void (*stop_rx) (void __iomem *ioaddr);
	int (*dma_interrupt) (void __iomem *ioaddr,
			      struct stmmac_extra_stats *x);
	/* If supported then get the optional core features */
	unsigned int (*get_hw_feature) (void __iomem *ioaddr);
	/* Program the HW RX Watchdog */
	void (*rx_watchdog) (void __iomem *ioaddr, u32 riwt);
};

struct mac_device_info;

struct stmmac_ops {
	/* MAC core initialization */
	void (*core_init)(struct mac_device_info *hw, int mtu);
	/* Enable and verify that the IPC module is supported */
	int (*rx_ipc)(struct mac_device_info *hw);
	/* Dump MAC registers */
	void (*dump_regs)(struct mac_device_info *hw);
	/* Handle extra events on specific interrupts hw dependent */
	int (*host_irq_status)(struct mac_device_info *hw,
			       struct stmmac_extra_stats *x);
	/* Multicast filter setting */
	void (*set_filter)(struct mac_device_info *hw, struct net_device *dev);
	/* Flow control setting */
	void (*flow_ctrl)(struct mac_device_info *hw, unsigned int duplex,
			  unsigned int fc, unsigned int pause_time);
	/* Set power management mode (e.g. magic frame) */
	void (*pmt)(struct mac_device_info *hw, unsigned long mode);
	/* Set/Get Unicast MAC addresses */
	void (*set_umac_addr)(struct mac_device_info *hw, unsigned char *addr,
			      unsigned int reg_n);
	void (*get_umac_addr)(struct mac_device_info *hw, unsigned char *addr,
			      unsigned int reg_n);
	void (*set_eee_mode)(struct mac_device_info *hw);
	void (*reset_eee_mode)(struct mac_device_info *hw);
	void (*set_eee_timer)(struct mac_device_info *hw, int ls, int tw);
	void (*set_eee_pls)(struct mac_device_info *hw, int link);
	void (*ctrl_ane)(struct mac_device_info *hw, bool restart);
	void (*get_adv)(struct mac_device_info *hw, struct rgmii_adv *adv);
};

struct stmmac_hwtimestamp {
	void (*config_hw_tstamping) (void __iomem *ioaddr, u32 data);
	void (*config_sub_second_increment) (void __iomem *ioaddr);
	int (*init_systime) (void __iomem *ioaddr, u32 sec, u32 nsec);
	int (*config_addend) (void __iomem *ioaddr, u32 addend);
	int (*adjust_systime) (void __iomem *ioaddr, u32 sec, u32 nsec,
			       int add_sub);
	 u64(*get_systime) (void __iomem *ioaddr);
};

struct mac_link {
	int port;
	int duplex;
	int speed;
};

struct mii_regs {
	unsigned int addr;	/* MII Address */
	unsigned int data;	/* MII Data */
};

struct stmmac_mode_ops {
	void (*init) (void *des, dma_addr_t phy_addr, unsigned int size,
		      unsigned int extend_desc);
	unsigned int (*is_jumbo_frm) (int len, int ehn_desc);
	int (*jumbo_frm)(void *priv, struct sk_buff *skb, int csum);
	int (*set_16kib_bfsize)(int mtu);
	void (*init_desc3)(struct dma_desc *p);
	void (*refill_desc3) (void *priv, struct dma_desc *p);
	void (*clean_desc3) (void *priv, struct dma_desc *p);
};

struct mac_device_info {
	const struct stmmac_ops *mac;
	const struct stmmac_desc_ops *desc;
	const struct stmmac_dma_ops *dma;
	const struct stmmac_mode_ops *mode;
	const struct stmmac_hwtimestamp *ptp;
	struct mii_regs mii;	/* MII register Addresses */
	struct mac_link link;
	unsigned int synopsys_uid;
	void __iomem *pcsr;     /* vpointer to device CSRs */
	int multicast_filter_bins;
	int unicast_filter_entries;
	int mcast_bits_log2;
};

struct mac_device_info *dwmac1000_setup(void __iomem *ioaddr, int mcbins,
					int perfect_uc_entries);
struct mac_device_info *dwmac100_setup(void __iomem *ioaddr);

void stmmac_set_mac_addr(void __iomem *ioaddr, u8 addr[6],
			 unsigned int high, unsigned int low);
void stmmac_get_mac_addr(void __iomem *ioaddr, unsigned char *addr,
			 unsigned int high, unsigned int low);

void stmmac_set_mac(void __iomem *ioaddr, bool enable);

void dwmac_dma_flush_tx_fifo(void __iomem *ioaddr);
extern const struct stmmac_mode_ops ring_mode_ops;
extern const struct stmmac_mode_ops chain_mode_ops;

#endif /* __COMMON_H__ */
