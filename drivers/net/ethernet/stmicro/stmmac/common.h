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

#include <linux/etherdevice.h>
#include <linux/netdevice.h>
#include <linux/phy.h>
#include <linux/module.h>
#include <linux/init.h>
#if defined(CONFIG_VLAN_8021Q) || defined(CONFIG_VLAN_8021Q_MODULE)
#define STMMAC_VLAN_TAG_USED
#include <linux/if_vlan.h>
#endif

#include "descs.h"
#include "mmc.h"

#undef CHIP_DEBUG_PRINT
/* Turn-on extra printk debug for MAC core, dma and descriptors */
/* #define CHIP_DEBUG_PRINT */

#ifdef CHIP_DEBUG_PRINT
#define CHIP_DBG(fmt, args...)  printk(fmt, ## args)
#else
#define CHIP_DBG(fmt, args...)  do { } while (0)
#endif

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
	/* Tx/Rx IRQ errors */
	unsigned long tx_undeflow_irq;
	unsigned long tx_process_stopped_irq;
	unsigned long tx_jabber_irq;
	unsigned long rx_overflow_irq;
	unsigned long rx_buf_unav_irq;
	unsigned long rx_process_stopped_irq;
	unsigned long rx_watchdog_irq;
	unsigned long tx_early_irq;
	unsigned long fatal_bus_error_irq;
	/* Extra info */
	unsigned long threshold;
	unsigned long tx_pkt_n;
	unsigned long rx_pkt_n;
	unsigned long poll_n;
	unsigned long sched_timer_n;
	unsigned long normal_irq_n;
	unsigned long mmc_tx_irq_n;
	unsigned long mmc_rx_irq_n;
	unsigned long mmc_rx_csum_offload_irq_n;
	/* EEE */
	unsigned long irq_receive_pmt_irq_n;
	unsigned long irq_tx_path_in_lpi_mode_n;
	unsigned long irq_tx_path_exit_lpi_mode_n;
	unsigned long irq_rx_path_in_lpi_mode_n;
	unsigned long irq_rx_path_exit_lpi_mode_n;
	unsigned long phy_eee_wakeup_error_n;
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

#define SF_DMA_MODE 1 /* DMA STORE-AND-FORWARD Operation Mode */

/* DAM HW feature register fields */
#define DMA_HW_FEAT_MIISEL	0x00000001 /* 10/100 Mbps Support */
#define DMA_HW_FEAT_GMIISEL	0x00000002 /* 1000 Mbps Support */
#define DMA_HW_FEAT_HDSEL	0x00000004 /* Half-Duplex Support */
#define DMA_HW_FEAT_EXTHASHEN	0x00000008 /* Expanded DA Hash Filter */
#define DMA_HW_FEAT_HASHSEL	0x00000010 /* HASH Filter */
#define DMA_HW_FEAT_ADDMACADRSEL	0x00000020 /* Multiple MAC Addr Reg */
#define DMA_HW_FEAT_PCSSEL	0x00000040 /* PCS registers */
#define DMA_HW_FEAT_L3L4FLTREN	0x00000080 /* Layer 3 & Layer 4 Feature */
#define DMA_HW_FEAT_SMASEL	0x00000100 /* SMA(MDIO) Interface */
#define DMA_HW_FEAT_RWKSEL	0x00000200 /* PMT Remote Wakeup */
#define DMA_HW_FEAT_MGKSEL	0x00000400 /* PMT Magic Packet */
#define DMA_HW_FEAT_MMCSEL	0x00000800 /* RMON Module */
#define DMA_HW_FEAT_TSVER1SEL	0x00001000 /* Only IEEE 1588-2002 Timestamp */
#define DMA_HW_FEAT_TSVER2SEL	0x00002000 /* IEEE 1588-2008 Adv Timestamp */
#define DMA_HW_FEAT_EEESEL	0x00004000 /* Energy Efficient Ethernet */
#define DMA_HW_FEAT_AVSEL	0x00008000 /* AV Feature */
#define DMA_HW_FEAT_TXCOESEL	0x00010000 /* Checksum Offload in Tx */
#define DMA_HW_FEAT_RXTYP1COE	0x00020000 /* IP csum Offload(Type 1) in Rx */
#define DMA_HW_FEAT_RXTYP2COE	0x00040000 /* IP csum Offload(Type 2) in Rx */
#define DMA_HW_FEAT_RXFIFOSIZE	0x00080000 /* Rx FIFO > 2048 Bytes */
#define DMA_HW_FEAT_RXCHCNT	0x00300000 /* No. of additional Rx Channels */
#define DMA_HW_FEAT_TXCHCNT	0x00c00000 /* No. of additional Tx Channels */
#define DMA_HW_FEAT_ENHDESSEL	0x01000000 /* Alternate (Enhanced Descriptor) */
#define DMA_HW_FEAT_INTTSEN	0x02000000 /* Timestamping with Internal
					      System Time */
#define DMA_HW_FEAT_FLEXIPPSEN	0x04000000 /* Flexible PPS Output */
#define DMA_HW_FEAT_SAVLANINS	0x08000000 /* Source Addr or VLAN Insertion */
#define DMA_HW_FEAT_ACTPHYIF	0x70000000 /* Active/selected PHY interface */
#define DEFAULT_DMA_PBL		8

enum rx_frame_status { /* IPC status */
	good_frame = 0,
	discard_frame = 1,
	csum_none = 2,
	llc_snap = 4,
};

enum tx_dma_irq_status {
	tx_hard_error = 1,
	tx_hard_error_bump_tc = 2,
	handle_tx_rx = 3,
};

enum core_specific_irq_mask {
	core_mmc_tx_irq = 1,
	core_mmc_rx_irq = 2,
	core_mmc_rx_csum_offload_irq = 4,
	core_irq_receive_pmt_irq = 8,
	core_irq_tx_path_in_lpi_mode = 16,
	core_irq_tx_path_exit_lpi_mode = 32,
	core_irq_rx_path_in_lpi_mode = 64,
	core_irq_rx_path_exit_lpi_mode = 128,
};

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
	/* IEEE 1588-2002*/
	unsigned int time_stamp;
	/* IEEE 1588-2008*/
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
	/* Alternate (enhanced) DESC mode*/
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
#define STMMAC_DEFAULT_LIT_LS_TIMER	0x3E8
#define STMMAC_DEFAULT_TWT_LS_TIMER	0x0

struct stmmac_desc_ops {
	/* DMA RX descriptor ring initialization */
	void (*init_rx_desc) (struct dma_desc *p, unsigned int ring_size,
			      int disable_rx_ic);
	/* DMA TX descriptor ring initialization */
	void (*init_tx_desc) (struct dma_desc *p, unsigned int ring_size);

	/* Invoked by the xmit function to prepare the tx descriptor */
	void (*prepare_tx_desc) (struct dma_desc *p, int is_fs, int len,
				 int csum_flag);
	/* Set/get the owner of the descriptor */
	void (*set_tx_owner) (struct dma_desc *p);
	int (*get_tx_owner) (struct dma_desc *p);
	/* Invoked by the xmit function to close the tx descriptor */
	void (*close_tx_desc) (struct dma_desc *p);
	/* Clean the tx descriptor as soon as the tx irq is received */
	void (*release_tx_desc) (struct dma_desc *p);
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
};

struct stmmac_dma_ops {
	/* DMA core initialization */
	int (*init) (void __iomem *ioaddr, int pbl, int fb, int mb,
		     int burst_len, u32 dma_tx, u32 dma_rx);
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
};

struct stmmac_ops {
	/* MAC core initialization */
	void (*core_init) (void __iomem *ioaddr) ____cacheline_aligned;
	/* Enable and verify that the IPC module is supported */
	int (*rx_ipc) (void __iomem *ioaddr);
	/* Dump MAC registers */
	void (*dump_regs) (void __iomem *ioaddr);
	/* Handle extra events on specific interrupts hw dependent */
	int (*host_irq_status) (void __iomem *ioaddr);
	/* Multicast filter setting */
	void (*set_filter) (struct net_device *dev, int id);
	/* Flow control setting */
	void (*flow_ctrl) (void __iomem *ioaddr, unsigned int duplex,
			   unsigned int fc, unsigned int pause_time);
	/* Set power management mode (e.g. magic frame) */
	void (*pmt) (void __iomem *ioaddr, unsigned long mode);
	/* Set/Get Unicast MAC addresses */
	void (*set_umac_addr) (void __iomem *ioaddr, unsigned char *addr,
			       unsigned int reg_n);
	void (*get_umac_addr) (void __iomem *ioaddr, unsigned char *addr,
			       unsigned int reg_n);
	void (*set_eee_mode) (void __iomem *ioaddr);
	void (*reset_eee_mode) (void __iomem *ioaddr);
	void (*set_eee_timer) (void __iomem *ioaddr, int ls, int tw);
	void (*set_eee_pls) (void __iomem *ioaddr, int link);
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

struct stmmac_ring_mode_ops {
	unsigned int (*is_jumbo_frm) (int len, int ehn_desc);
	unsigned int (*jumbo_frm) (void *priv, struct sk_buff *skb, int csum);
	void (*refill_desc3) (int bfsize, struct dma_desc *p);
	void (*init_desc3) (int des3_as_data_buf, struct dma_desc *p);
	void (*init_dma_chain) (struct dma_desc *des, dma_addr_t phy_addr,
				unsigned int size);
	void (*clean_desc3) (struct dma_desc *p);
	int (*set_16kib_bfsize) (int mtu);
};

struct mac_device_info {
	const struct stmmac_ops		*mac;
	const struct stmmac_desc_ops	*desc;
	const struct stmmac_dma_ops	*dma;
	const struct stmmac_ring_mode_ops	*ring;
	struct mii_regs mii;	/* MII register Addresses */
	struct mac_link link;
	unsigned int synopsys_uid;
};

struct mac_device_info *dwmac1000_setup(void __iomem *ioaddr);
struct mac_device_info *dwmac100_setup(void __iomem *ioaddr);

extern void stmmac_set_mac_addr(void __iomem *ioaddr, u8 addr[6],
				unsigned int high, unsigned int low);
extern void stmmac_get_mac_addr(void __iomem *ioaddr, unsigned char *addr,
				unsigned int high, unsigned int low);

extern void stmmac_set_mac(void __iomem *ioaddr, bool enable);

extern void dwmac_dma_flush_tx_fifo(void __iomem *ioaddr);
extern const struct stmmac_ring_mode_ops ring_mode_ops;
