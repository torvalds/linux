/* SPDX-License-Identifier: GPL-2.0-only */
/* 10G controller driver for Samsung SoCs
 *
 * Copyright (C) 2013 Samsung Electronics Co., Ltd.
 *		http://www.samsung.com
 *
 * Author: Siva Reddy Kallam <siva.kallam@samsung.com>
 */

#ifndef __SXGBE_COMMON_H__
#define __SXGBE_COMMON_H__

/* forward references */
struct sxgbe_desc_ops;
struct sxgbe_dma_ops;
struct sxgbe_mtl_ops;

#define SXGBE_RESOURCE_NAME	"sam_sxgbeeth"
#define DRV_MODULE_VERSION	"November_2013"

/* MAX HW feature words */
#define SXGBE_HW_WORDS 3

#define SXGBE_RX_COE_NONE	0

/* CSR Frequency Access Defines*/
#define SXGBE_CSR_F_150M	150000000
#define SXGBE_CSR_F_250M	250000000
#define SXGBE_CSR_F_300M	300000000
#define SXGBE_CSR_F_350M	350000000
#define SXGBE_CSR_F_400M	400000000
#define SXGBE_CSR_F_500M	500000000

/* pause time */
#define SXGBE_PAUSE_TIME 0x200

/* tx queues */
#define SXGBE_TX_QUEUES   8
#define SXGBE_RX_QUEUES   16

/* Calculated based how much time does it take to fill 256KB Rx memory
 * at 10Gb speed at 156MHz clock rate and considered little less then
 * the actual value.
 */
#define SXGBE_MAX_DMA_RIWT	0x70
#define SXGBE_MIN_DMA_RIWT	0x01

/* Tx coalesce parameters */
#define SXGBE_COAL_TX_TIMER	40000
#define SXGBE_MAX_COAL_TX_TICK	100000
#define SXGBE_TX_MAX_FRAMES	512
#define SXGBE_TX_FRAMES	128

/* SXGBE TX FIFO is 8K, Rx FIFO is 16K */
#define BUF_SIZE_16KiB 16384
#define BUF_SIZE_8KiB 8192
#define BUF_SIZE_4KiB 4096
#define BUF_SIZE_2KiB 2048

#define SXGBE_DEFAULT_LIT_LS	0x3E8
#define SXGBE_DEFAULT_TWT_LS	0x0

/* Flow Control defines */
#define SXGBE_FLOW_OFF		0
#define SXGBE_FLOW_RX		1
#define SXGBE_FLOW_TX		2
#define SXGBE_FLOW_AUTO		(SXGBE_FLOW_TX | SXGBE_FLOW_RX)

#define SF_DMA_MODE 1		/* DMA STORE-AND-FORWARD Operation Mode */

/* errors */
#define RX_GMII_ERR		0x01
#define RX_WATCHDOG_ERR		0x02
#define RX_CRC_ERR		0x03
#define RX_GAINT_ERR		0x04
#define RX_IP_HDR_ERR		0x05
#define RX_PAYLOAD_ERR		0x06
#define RX_OVERFLOW_ERR		0x07

/* pkt type */
#define RX_LEN_PKT		0x00
#define RX_MACCTL_PKT		0x01
#define RX_DCBCTL_PKT		0x02
#define RX_ARP_PKT		0x03
#define RX_OAM_PKT		0x04
#define RX_UNTAG_PKT		0x05
#define RX_OTHER_PKT		0x07
#define RX_SVLAN_PKT		0x08
#define RX_CVLAN_PKT		0x09
#define RX_DVLAN_OCVLAN_ICVLAN_PKT		0x0A
#define RX_DVLAN_OSVLAN_ISVLAN_PKT		0x0B
#define RX_DVLAN_OSVLAN_ICVLAN_PKT		0x0C
#define RX_DVLAN_OCVLAN_ISVLAN_PKT		0x0D

#define RX_NOT_IP_PKT		0x00
#define RX_IPV4_TCP_PKT		0x01
#define RX_IPV4_UDP_PKT		0x02
#define RX_IPV4_ICMP_PKT	0x03
#define RX_IPV4_UNKNOWN_PKT	0x07
#define RX_IPV6_TCP_PKT		0x09
#define RX_IPV6_UDP_PKT		0x0A
#define RX_IPV6_ICMP_PKT	0x0B
#define RX_IPV6_UNKNOWN_PKT	0x0F

#define RX_NO_PTP		0x00
#define RX_PTP_SYNC		0x01
#define RX_PTP_FOLLOW_UP	0x02
#define RX_PTP_DELAY_REQ	0x03
#define RX_PTP_DELAY_RESP	0x04
#define RX_PTP_PDELAY_REQ	0x05
#define RX_PTP_PDELAY_RESP	0x06
#define RX_PTP_PDELAY_FOLLOW_UP	0x07
#define RX_PTP_ANNOUNCE		0x08
#define RX_PTP_MGMT		0x09
#define RX_PTP_SIGNAL		0x0A
#define RX_PTP_RESV_MSG		0x0F

/* EEE-LPI mode  flags*/
#define TX_ENTRY_LPI_MODE	0x10
#define TX_EXIT_LPI_MODE	0x20
#define RX_ENTRY_LPI_MODE	0x40
#define RX_EXIT_LPI_MODE	0x80

/* EEE-LPI Interrupt status flag */
#define LPI_INT_STATUS		BIT(5)

/* EEE-LPI Default timer values */
#define LPI_LINK_STATUS_TIMER	0x3E8
#define LPI_MAC_WAIT_TIMER	0x00

/* EEE-LPI Control and status definitions */
#define LPI_CTRL_STATUS_TXA	BIT(19)
#define LPI_CTRL_STATUS_PLSDIS	BIT(18)
#define LPI_CTRL_STATUS_PLS	BIT(17)
#define LPI_CTRL_STATUS_LPIEN	BIT(16)
#define LPI_CTRL_STATUS_TXRSTP	BIT(11)
#define LPI_CTRL_STATUS_RXRSTP	BIT(10)
#define LPI_CTRL_STATUS_RLPIST	BIT(9)
#define LPI_CTRL_STATUS_TLPIST	BIT(8)
#define LPI_CTRL_STATUS_RLPIEX	BIT(3)
#define LPI_CTRL_STATUS_RLPIEN	BIT(2)
#define LPI_CTRL_STATUS_TLPIEX	BIT(1)
#define LPI_CTRL_STATUS_TLPIEN	BIT(0)

enum dma_irq_status {
	tx_hard_error	= BIT(0),
	tx_bump_tc	= BIT(1),
	handle_tx	= BIT(2),
	rx_hard_error	= BIT(3),
	rx_bump_tc	= BIT(4),
	handle_rx	= BIT(5),
};

#define NETIF_F_HW_VLAN_ALL     (NETIF_F_HW_VLAN_CTAG_RX |	\
				 NETIF_F_HW_VLAN_STAG_RX |	\
				 NETIF_F_HW_VLAN_CTAG_TX |	\
				 NETIF_F_HW_VLAN_STAG_TX |	\
				 NETIF_F_HW_VLAN_CTAG_FILTER |	\
				 NETIF_F_HW_VLAN_STAG_FILTER)

/* MMC control defines */
#define SXGBE_MMC_CTRL_CNT_FRZ  0x00000008

/* SXGBE HW ADDR regs */
#define SXGBE_ADDR_HIGH(reg)    (((reg > 15) ? 0x00000800 : 0x00000040) + \
				 (reg * 8))
#define SXGBE_ADDR_LOW(reg)     (((reg > 15) ? 0x00000804 : 0x00000044) + \
				 (reg * 8))
#define SXGBE_MAX_PERFECT_ADDRESSES 32 /* Maximum unicast perfect filtering */
#define SXGBE_FRAME_FILTER       0x00000004      /* Frame Filter */

/* SXGBE Frame Filter defines */
#define SXGBE_FRAME_FILTER_PR    0x00000001      /* Promiscuous Mode */
#define SXGBE_FRAME_FILTER_HUC   0x00000002      /* Hash Unicast */
#define SXGBE_FRAME_FILTER_HMC   0x00000004      /* Hash Multicast */
#define SXGBE_FRAME_FILTER_DAIF  0x00000008      /* DA Inverse Filtering */
#define SXGBE_FRAME_FILTER_PM    0x00000010      /* Pass all multicast */
#define SXGBE_FRAME_FILTER_DBF   0x00000020      /* Disable Broadcast frames */
#define SXGBE_FRAME_FILTER_SAIF  0x00000100      /* Inverse Filtering */
#define SXGBE_FRAME_FILTER_SAF   0x00000200      /* Source Address Filter */
#define SXGBE_FRAME_FILTER_HPF   0x00000400      /* Hash or perfect Filter */
#define SXGBE_FRAME_FILTER_RA    0x80000000      /* Receive all mode */

#define SXGBE_HASH_TABLE_SIZE    64
#define SXGBE_HASH_HIGH          0x00000008      /* Multicast Hash Table High */
#define SXGBE_HASH_LOW           0x0000000c      /* Multicast Hash Table Low */

#define SXGBE_HI_REG_AE          0x80000000

/* Minimum and maximum MTU */
#define MIN_MTU         68
#define MAX_MTU         9000

#define SXGBE_FOR_EACH_QUEUE(max_queues, queue_num)			\
	for (queue_num = 0; queue_num < max_queues; queue_num++)

#define DRV_VERSION "1.0.0"

#define SXGBE_MAX_RX_CHANNELS	16
#define SXGBE_MAX_TX_CHANNELS	16

#define START_MAC_REG_OFFSET	0x0000
#define MAX_MAC_REG_OFFSET	0x0DFC
#define START_MTL_REG_OFFSET	0x1000
#define MAX_MTL_REG_OFFSET	0x18FC
#define START_DMA_REG_OFFSET	0x3000
#define MAX_DMA_REG_OFFSET	0x38FC

#define REG_SPACE_SIZE		0x2000

/* sxgbe statistics counters */
struct sxgbe_extra_stats {
	/* TX/RX IRQ events */
	unsigned long tx_underflow_irq;
	unsigned long tx_process_stopped_irq;
	unsigned long tx_ctxt_desc_err;
	unsigned long tx_threshold;
	unsigned long rx_threshold;
	unsigned long tx_pkt_n;
	unsigned long rx_pkt_n;
	unsigned long normal_irq_n;
	unsigned long tx_normal_irq_n;
	unsigned long rx_normal_irq_n;
	unsigned long napi_poll;
	unsigned long tx_clean;
	unsigned long tx_reset_ic_bit;
	unsigned long rx_process_stopped_irq;
	unsigned long rx_underflow_irq;

	/* Bus access errors */
	unsigned long fatal_bus_error_irq;
	unsigned long tx_read_transfer_err;
	unsigned long tx_write_transfer_err;
	unsigned long tx_desc_access_err;
	unsigned long tx_buffer_access_err;
	unsigned long tx_data_transfer_err;
	unsigned long rx_read_transfer_err;
	unsigned long rx_write_transfer_err;
	unsigned long rx_desc_access_err;
	unsigned long rx_buffer_access_err;
	unsigned long rx_data_transfer_err;

	/* EEE-LPI stats */
	unsigned long tx_lpi_entry_n;
	unsigned long tx_lpi_exit_n;
	unsigned long rx_lpi_entry_n;
	unsigned long rx_lpi_exit_n;
	unsigned long eee_wakeup_error_n;

	/* RX specific */
	/* L2 error */
	unsigned long rx_code_gmii_err;
	unsigned long rx_watchdog_err;
	unsigned long rx_crc_err;
	unsigned long rx_gaint_pkt_err;
	unsigned long ip_hdr_err;
	unsigned long ip_payload_err;
	unsigned long overflow_error;

	/* L2 Pkt type */
	unsigned long len_pkt;
	unsigned long mac_ctl_pkt;
	unsigned long dcb_ctl_pkt;
	unsigned long arp_pkt;
	unsigned long oam_pkt;
	unsigned long untag_okt;
	unsigned long other_pkt;
	unsigned long svlan_tag_pkt;
	unsigned long cvlan_tag_pkt;
	unsigned long dvlan_ocvlan_icvlan_pkt;
	unsigned long dvlan_osvlan_isvlan_pkt;
	unsigned long dvlan_osvlan_icvlan_pkt;
	unsigned long dvan_ocvlan_icvlan_pkt;

	/* L3/L4 Pkt type */
	unsigned long not_ip_pkt;
	unsigned long ip4_tcp_pkt;
	unsigned long ip4_udp_pkt;
	unsigned long ip4_icmp_pkt;
	unsigned long ip4_unknown_pkt;
	unsigned long ip6_tcp_pkt;
	unsigned long ip6_udp_pkt;
	unsigned long ip6_icmp_pkt;
	unsigned long ip6_unknown_pkt;

	/* Filter specific */
	unsigned long vlan_filter_match;
	unsigned long sa_filter_fail;
	unsigned long da_filter_fail;
	unsigned long hash_filter_pass;
	unsigned long l3_filter_match;
	unsigned long l4_filter_match;

	/* RX context specific */
	unsigned long timestamp_dropped;
	unsigned long rx_msg_type_no_ptp;
	unsigned long rx_ptp_type_sync;
	unsigned long rx_ptp_type_follow_up;
	unsigned long rx_ptp_type_delay_req;
	unsigned long rx_ptp_type_delay_resp;
	unsigned long rx_ptp_type_pdelay_req;
	unsigned long rx_ptp_type_pdelay_resp;
	unsigned long rx_ptp_type_pdelay_follow_up;
	unsigned long rx_ptp_announce;
	unsigned long rx_ptp_mgmt;
	unsigned long rx_ptp_signal;
	unsigned long rx_ptp_resv_msg_type;
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

struct sxgbe_core_ops {
	/* MAC core initialization */
	void (*core_init)(void __iomem *ioaddr);
	/* Dump MAC registers */
	void (*dump_regs)(void __iomem *ioaddr);
	/* Handle extra events on specific interrupts hw dependent */
	int (*host_irq_status)(void __iomem *ioaddr,
			       struct sxgbe_extra_stats *x);
	/* Set power management mode (e.g. magic frame) */
	void (*pmt)(void __iomem *ioaddr, unsigned long mode);
	/* Set/Get Unicast MAC addresses */
	void (*set_umac_addr)(void __iomem *ioaddr, const unsigned char *addr,
			      unsigned int reg_n);
	void (*get_umac_addr)(void __iomem *ioaddr, unsigned char *addr,
			      unsigned int reg_n);
	void (*enable_rx)(void __iomem *ioaddr, bool enable);
	void (*enable_tx)(void __iomem *ioaddr, bool enable);

	/* controller version specific operations */
	int (*get_controller_version)(void __iomem *ioaddr);

	/* If supported then get the optional core features */
	unsigned int (*get_hw_feature)(void __iomem *ioaddr,
				       unsigned char feature_index);
	/* adjust SXGBE speed */
	void (*set_speed)(void __iomem *ioaddr, unsigned char speed);

	/* EEE-LPI specific operations */
	void (*set_eee_mode)(void __iomem *ioaddr);
	void (*reset_eee_mode)(void __iomem *ioaddr);
	void (*set_eee_timer)(void __iomem *ioaddr, const int ls,
			      const int tw);
	void (*set_eee_pls)(void __iomem *ioaddr, const int link);

	/* Enable disable checksum offload operations */
	void (*enable_rx_csum)(void __iomem *ioaddr);
	void (*disable_rx_csum)(void __iomem *ioaddr);
	void (*enable_rxqueue)(void __iomem *ioaddr, int queue_num);
	void (*disable_rxqueue)(void __iomem *ioaddr, int queue_num);
};

const struct sxgbe_core_ops *sxgbe_get_core_ops(void);

struct sxgbe_ops {
	const struct sxgbe_core_ops *mac;
	const struct sxgbe_desc_ops *desc;
	const struct sxgbe_dma_ops *dma;
	const struct sxgbe_mtl_ops *mtl;
	struct mii_regs mii;	/* MII register Addresses */
	struct mac_link link;
	unsigned int ctrl_uid;
	unsigned int ctrl_id;
};

/* SXGBE private data structures */
struct sxgbe_tx_queue {
	unsigned int irq_no;
	struct sxgbe_priv_data *priv_ptr;
	struct sxgbe_tx_norm_desc *dma_tx;
	dma_addr_t dma_tx_phy;
	dma_addr_t *tx_skbuff_dma;
	struct sk_buff **tx_skbuff;
	struct timer_list txtimer;
	unsigned int cur_tx;
	unsigned int dirty_tx;
	u32 tx_count_frames;
	u32 tx_coal_frames;
	u32 tx_coal_timer;
	int hwts_tx_en;
	u16 prev_mss;
	u8 queue_no;
};

struct sxgbe_rx_queue {
	struct sxgbe_priv_data *priv_ptr;
	struct sxgbe_rx_norm_desc *dma_rx;
	struct sk_buff **rx_skbuff;
	unsigned int cur_rx;
	unsigned int dirty_rx;
	unsigned int irq_no;
	u32 rx_riwt;
	dma_addr_t *rx_skbuff_dma;
	dma_addr_t dma_rx_phy;
	u8 queue_no;
};

/* SXGBE HW capabilities */
struct sxgbe_hw_features {
	/****** CAP [0] *******/
	unsigned int pmt_remote_wake_up;
	unsigned int pmt_magic_frame;
	/* IEEE 1588-2008 */
	unsigned int atime_stamp;

	unsigned int eee;

	unsigned int tx_csum_offload;
	unsigned int rx_csum_offload;
	unsigned int multi_macaddr;
	unsigned int tstamp_srcselect;
	unsigned int sa_vlan_insert;

	/****** CAP [1] *******/
	unsigned int rxfifo_size;
	unsigned int txfifo_size;
	unsigned int atstmap_hword;
	unsigned int dcb_enable;
	unsigned int splithead_enable;
	unsigned int tcpseg_offload;
	unsigned int debug_mem;
	unsigned int rss_enable;
	unsigned int hash_tsize;
	unsigned int l3l4_filer_size;

	/* This value is in bytes and
	 * as mentioned in HW features
	 * of SXGBE data book
	 */
	unsigned int rx_mtl_qsize;
	unsigned int tx_mtl_qsize;

	/****** CAP [2] *******/
	/* TX and RX number of channels */
	unsigned int rx_mtl_queues;
	unsigned int tx_mtl_queues;
	unsigned int rx_dma_channels;
	unsigned int tx_dma_channels;
	unsigned int pps_output_count;
	unsigned int aux_input_count;
};

struct sxgbe_priv_data {
	/* DMA descriptos */
	struct sxgbe_tx_queue *txq[SXGBE_TX_QUEUES];
	struct sxgbe_rx_queue *rxq[SXGBE_RX_QUEUES];
	u8 cur_rx_qnum;

	unsigned int dma_tx_size;
	unsigned int dma_rx_size;
	unsigned int dma_buf_sz;
	u32 rx_riwt;

	struct napi_struct napi;

	void __iomem *ioaddr;
	struct net_device *dev;
	struct device *device;
	struct sxgbe_ops *hw;	/* sxgbe specific ops */
	int no_csum_insertion;
	int irq;
	int rxcsum_insertion;
	spinlock_t stats_lock;	/* lock for tx/rx statatics */

	int oldlink;
	int speed;
	int oldduplex;
	struct mii_bus *mii;
	int mii_irq[PHY_MAX_ADDR];
	u8 rx_pause;
	u8 tx_pause;

	struct sxgbe_extra_stats xstats;
	struct sxgbe_plat_data *plat;
	struct sxgbe_hw_features hw_cap;

	u32 msg_enable;

	struct clk *sxgbe_clk;
	int clk_csr;
	unsigned int mode;
	unsigned int default_addend;

	/* advanced time stamp support */
	u32 adv_ts;
	int use_riwt;
	struct ptp_clock *ptp_clock;

	/* tc control */
	int tx_tc;
	int rx_tc;
	/* EEE-LPI specific members */
	struct timer_list eee_ctrl_timer;
	bool tx_path_in_lpi_mode;
	int lpi_irq;
	int eee_enabled;
	int eee_active;
	int tx_lpi_timer;
};

/* Function prototypes */
struct sxgbe_priv_data *sxgbe_drv_probe(struct device *device,
					struct sxgbe_plat_data *plat_dat,
					void __iomem *addr);
int sxgbe_drv_remove(struct net_device *ndev);
void sxgbe_set_ethtool_ops(struct net_device *netdev);
int sxgbe_mdio_unregister(struct net_device *ndev);
int sxgbe_mdio_register(struct net_device *ndev);
int sxgbe_register_platform(void);
void sxgbe_unregister_platform(void);

#ifdef CONFIG_PM
int sxgbe_suspend(struct net_device *ndev);
int sxgbe_resume(struct net_device *ndev);
int sxgbe_freeze(struct net_device *ndev);
int sxgbe_restore(struct net_device *ndev);
#endif /* CONFIG_PM */

const struct sxgbe_mtl_ops *sxgbe_get_mtl_ops(void);

void sxgbe_disable_eee_mode(struct sxgbe_priv_data * const priv);
bool sxgbe_eee_init(struct sxgbe_priv_data * const priv);
#endif /* __SXGBE_COMMON_H__ */
