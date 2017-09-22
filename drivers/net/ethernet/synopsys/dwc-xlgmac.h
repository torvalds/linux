/* Synopsys DesignWare Core Enterprise Ethernet (XLGMAC) Driver
 *
 * Copyright (c) 2017 Synopsys, Inc. (www.synopsys.com)
 *
 * This program is dual-licensed; you may select either version 2 of
 * the GNU General Public License ("GPL") or BSD license ("BSD").
 *
 * This Synopsys DWC XLGMAC software driver and associated documentation
 * (hereinafter the "Software") is an unsupported proprietary work of
 * Synopsys, Inc. unless otherwise expressly agreed to in writing between
 * Synopsys and you. The Software IS NOT an item of Licensed Software or a
 * Licensed Product under any End User Software License Agreement or
 * Agreement for Licensed Products with Synopsys or any supplement thereto.
 * Synopsys is a registered trademark of Synopsys, Inc. Other names included
 * in the SOFTWARE may be the trademarks of their respective owners.
 */

#ifndef __DWC_XLGMAC_H__
#define __DWC_XLGMAC_H__

#include <linux/dma-mapping.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/phy.h>
#include <linux/if_vlan.h>
#include <linux/bitops.h>
#include <linux/timecounter.h>

#define XLGMAC_DRV_NAME			"dwc-xlgmac"
#define XLGMAC_DRV_VERSION		"1.0.0"
#define XLGMAC_DRV_DESC			"Synopsys DWC XLGMAC Driver"

/* Descriptor related parameters */
#define XLGMAC_TX_DESC_CNT		1024
#define XLGMAC_TX_DESC_MIN_FREE		(XLGMAC_TX_DESC_CNT >> 3)
#define XLGMAC_TX_DESC_MAX_PROC		(XLGMAC_TX_DESC_CNT >> 1)
#define XLGMAC_RX_DESC_CNT		1024
#define XLGMAC_RX_DESC_MAX_DIRTY	(XLGMAC_RX_DESC_CNT >> 3)

/* Descriptors required for maximum contiguous TSO/GSO packet */
#define XLGMAC_TX_MAX_SPLIT	((GSO_MAX_SIZE / XLGMAC_TX_MAX_BUF_SIZE) + 1)

/* Maximum possible descriptors needed for a SKB */
#define XLGMAC_TX_MAX_DESC_NR	(MAX_SKB_FRAGS + XLGMAC_TX_MAX_SPLIT + 2)

#define XLGMAC_TX_MAX_BUF_SIZE	(0x3fff & ~(64 - 1))
#define XLGMAC_RX_MIN_BUF_SIZE	(ETH_FRAME_LEN + ETH_FCS_LEN + VLAN_HLEN)
#define XLGMAC_RX_BUF_ALIGN	64

/* Maximum Size for Splitting the Header Data
 * Keep in sync with SKB_ALLOC_SIZE
 * 3'b000: 64 bytes, 3'b001: 128 bytes
 * 3'b010: 256 bytes, 3'b011: 512 bytes
 * 3'b100: 1023 bytes ,   3'b101'3'b111: Reserved
 */
#define XLGMAC_SPH_HDSMS_SIZE		3
#define XLGMAC_SKB_ALLOC_SIZE		512

#define XLGMAC_MAX_FIFO			81920

#define XLGMAC_MAX_DMA_CHANNELS		16
#define XLGMAC_DMA_STOP_TIMEOUT		5
#define XLGMAC_DMA_INTERRUPT_MASK	0x31c7

/* Default coalescing parameters */
#define XLGMAC_INIT_DMA_TX_USECS	1000
#define XLGMAC_INIT_DMA_TX_FRAMES	25
#define XLGMAC_INIT_DMA_RX_USECS	30
#define XLGMAC_INIT_DMA_RX_FRAMES	25
#define XLGMAC_MAX_DMA_RIWT		0xff
#define XLGMAC_MIN_DMA_RIWT		0x01

/* Flow control queue count */
#define XLGMAC_MAX_FLOW_CONTROL_QUEUES	8

/* System clock is 125 MHz */
#define XLGMAC_SYSCLOCK			125000000

/* Maximum MAC address hash table size (256 bits = 8 bytes) */
#define XLGMAC_MAC_HASH_TABLE_SIZE	8

/* Receive Side Scaling */
#define XLGMAC_RSS_HASH_KEY_SIZE	40
#define XLGMAC_RSS_MAX_TABLE_SIZE	256
#define XLGMAC_RSS_LOOKUP_TABLE_TYPE	0
#define XLGMAC_RSS_HASH_KEY_TYPE	1

#define XLGMAC_STD_PACKET_MTU		1500
#define XLGMAC_JUMBO_PACKET_MTU		9000

/* Helper macro for descriptor handling
 *  Always use XLGMAC_GET_DESC_DATA to access the descriptor data
 */
#define XLGMAC_GET_DESC_DATA(ring, idx) ({				\
	typeof(ring) _ring = (ring);					\
	((_ring)->desc_data_head +					\
	 ((idx) & ((_ring)->dma_desc_count - 1)));			\
})

#define XLGMAC_GET_REG_BITS(var, pos, len) ({				\
	typeof(pos) _pos = (pos);					\
	typeof(len) _len = (len);					\
	((var) & GENMASK(_pos + _len - 1, _pos)) >> (_pos);		\
})

#define XLGMAC_GET_REG_BITS_LE(var, pos, len) ({			\
	typeof(pos) _pos = (pos);					\
	typeof(len) _len = (len);					\
	typeof(var) _var = le32_to_cpu((var));				\
	((_var) & GENMASK(_pos + _len - 1, _pos)) >> (_pos);		\
})

#define XLGMAC_SET_REG_BITS(var, pos, len, val) ({			\
	typeof(var) _var = (var);					\
	typeof(pos) _pos = (pos);					\
	typeof(len) _len = (len);					\
	typeof(val) _val = (val);					\
	_val = (_val << _pos) & GENMASK(_pos + _len - 1, _pos);		\
	_var = (_var & ~GENMASK(_pos + _len - 1, _pos)) | _val;		\
})

#define XLGMAC_SET_REG_BITS_LE(var, pos, len, val) ({			\
	typeof(var) _var = (var);					\
	typeof(pos) _pos = (pos);					\
	typeof(len) _len = (len);					\
	typeof(val) _val = (val);					\
	_val = (_val << _pos) & GENMASK(_pos + _len - 1, _pos);		\
	_var = (_var & ~GENMASK(_pos + _len - 1, _pos)) | _val;		\
	cpu_to_le32(_var);						\
})

struct xlgmac_pdata;

enum xlgmac_int {
	XLGMAC_INT_DMA_CH_SR_TI,
	XLGMAC_INT_DMA_CH_SR_TPS,
	XLGMAC_INT_DMA_CH_SR_TBU,
	XLGMAC_INT_DMA_CH_SR_RI,
	XLGMAC_INT_DMA_CH_SR_RBU,
	XLGMAC_INT_DMA_CH_SR_RPS,
	XLGMAC_INT_DMA_CH_SR_TI_RI,
	XLGMAC_INT_DMA_CH_SR_FBE,
	XLGMAC_INT_DMA_ALL,
};

struct xlgmac_stats {
	/* MMC TX counters */
	u64 txoctetcount_gb;
	u64 txframecount_gb;
	u64 txbroadcastframes_g;
	u64 txmulticastframes_g;
	u64 tx64octets_gb;
	u64 tx65to127octets_gb;
	u64 tx128to255octets_gb;
	u64 tx256to511octets_gb;
	u64 tx512to1023octets_gb;
	u64 tx1024tomaxoctets_gb;
	u64 txunicastframes_gb;
	u64 txmulticastframes_gb;
	u64 txbroadcastframes_gb;
	u64 txunderflowerror;
	u64 txoctetcount_g;
	u64 txframecount_g;
	u64 txpauseframes;
	u64 txvlanframes_g;

	/* MMC RX counters */
	u64 rxframecount_gb;
	u64 rxoctetcount_gb;
	u64 rxoctetcount_g;
	u64 rxbroadcastframes_g;
	u64 rxmulticastframes_g;
	u64 rxcrcerror;
	u64 rxrunterror;
	u64 rxjabbererror;
	u64 rxundersize_g;
	u64 rxoversize_g;
	u64 rx64octets_gb;
	u64 rx65to127octets_gb;
	u64 rx128to255octets_gb;
	u64 rx256to511octets_gb;
	u64 rx512to1023octets_gb;
	u64 rx1024tomaxoctets_gb;
	u64 rxunicastframes_g;
	u64 rxlengtherror;
	u64 rxoutofrangetype;
	u64 rxpauseframes;
	u64 rxfifooverflow;
	u64 rxvlanframes_gb;
	u64 rxwatchdogerror;

	/* Extra counters */
	u64 tx_tso_packets;
	u64 rx_split_header_packets;
	u64 tx_process_stopped;
	u64 rx_process_stopped;
	u64 tx_buffer_unavailable;
	u64 rx_buffer_unavailable;
	u64 fatal_bus_error;
	u64 tx_vlan_packets;
	u64 rx_vlan_packets;
	u64 napi_poll_isr;
	u64 napi_poll_txtimer;
};

struct xlgmac_ring_buf {
	struct sk_buff *skb;
	dma_addr_t skb_dma;
	unsigned int skb_len;
};

/* Common Tx and Rx DMA hardware descriptor */
struct xlgmac_dma_desc {
	__le32 desc0;
	__le32 desc1;
	__le32 desc2;
	__le32 desc3;
};

/* Page allocation related values */
struct xlgmac_page_alloc {
	struct page *pages;
	unsigned int pages_len;
	unsigned int pages_offset;

	dma_addr_t pages_dma;
};

/* Ring entry buffer data */
struct xlgmac_buffer_data {
	struct xlgmac_page_alloc pa;
	struct xlgmac_page_alloc pa_unmap;

	dma_addr_t dma_base;
	unsigned long dma_off;
	unsigned int dma_len;
};

/* Tx-related desc data */
struct xlgmac_tx_desc_data {
	unsigned int packets;		/* BQL packet count */
	unsigned int bytes;		/* BQL byte count */
};

/* Rx-related desc data */
struct xlgmac_rx_desc_data {
	struct xlgmac_buffer_data hdr;	/* Header locations */
	struct xlgmac_buffer_data buf;	/* Payload locations */

	unsigned short hdr_len;		/* Length of received header */
	unsigned short len;		/* Length of received packet */
};

struct xlgmac_pkt_info {
	struct sk_buff *skb;

	unsigned int attributes;

	unsigned int errors;

	/* descriptors needed for this packet */
	unsigned int desc_count;
	unsigned int length;

	unsigned int tx_packets;
	unsigned int tx_bytes;

	unsigned int header_len;
	unsigned int tcp_header_len;
	unsigned int tcp_payload_len;
	unsigned short mss;

	unsigned short vlan_ctag;

	u64 rx_tstamp;

	u32 rss_hash;
	enum pkt_hash_types rss_hash_type;
};

struct xlgmac_desc_data {
	/* dma_desc: Virtual address of descriptor
	 *  dma_desc_addr: DMA address of descriptor
	 */
	struct xlgmac_dma_desc *dma_desc;
	dma_addr_t dma_desc_addr;

	/* skb: Virtual address of SKB
	 *  skb_dma: DMA address of SKB data
	 *  skb_dma_len: Length of SKB DMA area
	 */
	struct sk_buff *skb;
	dma_addr_t skb_dma;
	unsigned int skb_dma_len;

	/* Tx/Rx -related data */
	struct xlgmac_tx_desc_data tx;
	struct xlgmac_rx_desc_data rx;

	unsigned int mapped_as_page;

	/* Incomplete receive save location.  If the budget is exhausted
	 * or the last descriptor (last normal descriptor or a following
	 * context descriptor) has not been DMA'd yet the current state
	 * of the receive processing needs to be saved.
	 */
	unsigned int state_saved;
	struct {
		struct sk_buff *skb;
		unsigned int len;
		unsigned int error;
	} state;
};

struct xlgmac_ring {
	/* Per packet related information */
	struct xlgmac_pkt_info pkt_info;

	/* Virtual/DMA addresses of DMA descriptor list and the total count */
	struct xlgmac_dma_desc *dma_desc_head;
	dma_addr_t dma_desc_head_addr;
	unsigned int dma_desc_count;

	/* Array of descriptor data corresponding the DMA descriptor
	 * (always use the XLGMAC_GET_DESC_DATA macro to access this data)
	 */
	struct xlgmac_desc_data *desc_data_head;

	/* Page allocation for RX buffers */
	struct xlgmac_page_alloc rx_hdr_pa;
	struct xlgmac_page_alloc rx_buf_pa;

	/* Ring index values
	 *  cur   - Tx: index of descriptor to be used for current transfer
	 *          Rx: index of descriptor to check for packet availability
	 *  dirty - Tx: index of descriptor to check for transfer complete
	 *          Rx: index of descriptor to check for buffer reallocation
	 */
	unsigned int cur;
	unsigned int dirty;

	/* Coalesce frame count used for interrupt bit setting */
	unsigned int coalesce_count;

	union {
		struct {
			unsigned int xmit_more;
			unsigned int queue_stopped;
			unsigned short cur_mss;
			unsigned short cur_vlan_ctag;
		} tx;
	};
} ____cacheline_aligned;

struct xlgmac_channel {
	char name[16];

	/* Address of private data area for device */
	struct xlgmac_pdata *pdata;

	/* Queue index and base address of queue's DMA registers */
	unsigned int queue_index;
	void __iomem *dma_regs;

	/* Per channel interrupt irq number */
	int dma_irq;
	char dma_irq_name[IFNAMSIZ + 32];

	/* Netdev related settings */
	struct napi_struct napi;

	unsigned int saved_ier;

	unsigned int tx_timer_active;
	struct timer_list tx_timer;

	struct xlgmac_ring *tx_ring;
	struct xlgmac_ring *rx_ring;
} ____cacheline_aligned;

struct xlgmac_desc_ops {
	int (*alloc_channles_and_rings)(struct xlgmac_pdata *pdata);
	void (*free_channels_and_rings)(struct xlgmac_pdata *pdata);
	int (*map_tx_skb)(struct xlgmac_channel *channel,
			  struct sk_buff *skb);
	int (*map_rx_buffer)(struct xlgmac_pdata *pdata,
			     struct xlgmac_ring *ring,
			struct xlgmac_desc_data *desc_data);
	void (*unmap_desc_data)(struct xlgmac_pdata *pdata,
				struct xlgmac_desc_data *desc_data);
	void (*tx_desc_init)(struct xlgmac_pdata *pdata);
	void (*rx_desc_init)(struct xlgmac_pdata *pdata);
};

struct xlgmac_hw_ops {
	int (*init)(struct xlgmac_pdata *pdata);
	int (*exit)(struct xlgmac_pdata *pdata);

	int (*tx_complete)(struct xlgmac_dma_desc *dma_desc);

	void (*enable_tx)(struct xlgmac_pdata *pdata);
	void (*disable_tx)(struct xlgmac_pdata *pdata);
	void (*enable_rx)(struct xlgmac_pdata *pdata);
	void (*disable_rx)(struct xlgmac_pdata *pdata);

	int (*enable_int)(struct xlgmac_channel *channel,
			  enum xlgmac_int int_id);
	int (*disable_int)(struct xlgmac_channel *channel,
			   enum xlgmac_int int_id);
	void (*dev_xmit)(struct xlgmac_channel *channel);
	int (*dev_read)(struct xlgmac_channel *channel);

	int (*set_mac_address)(struct xlgmac_pdata *pdata, u8 *addr);
	int (*config_rx_mode)(struct xlgmac_pdata *pdata);
	int (*enable_rx_csum)(struct xlgmac_pdata *pdata);
	int (*disable_rx_csum)(struct xlgmac_pdata *pdata);

	/* For MII speed configuration */
	int (*set_xlgmii_25000_speed)(struct xlgmac_pdata *pdata);
	int (*set_xlgmii_40000_speed)(struct xlgmac_pdata *pdata);
	int (*set_xlgmii_50000_speed)(struct xlgmac_pdata *pdata);
	int (*set_xlgmii_100000_speed)(struct xlgmac_pdata *pdata);

	/* For descriptor related operation */
	void (*tx_desc_init)(struct xlgmac_channel *channel);
	void (*rx_desc_init)(struct xlgmac_channel *channel);
	void (*tx_desc_reset)(struct xlgmac_desc_data *desc_data);
	void (*rx_desc_reset)(struct xlgmac_pdata *pdata,
			      struct xlgmac_desc_data *desc_data,
			unsigned int index);
	int (*is_last_desc)(struct xlgmac_dma_desc *dma_desc);
	int (*is_context_desc)(struct xlgmac_dma_desc *dma_desc);
	void (*tx_start_xmit)(struct xlgmac_channel *channel,
			      struct xlgmac_ring *ring);

	/* For Flow Control */
	int (*config_tx_flow_control)(struct xlgmac_pdata *pdata);
	int (*config_rx_flow_control)(struct xlgmac_pdata *pdata);

	/* For Vlan related config */
	int (*enable_rx_vlan_stripping)(struct xlgmac_pdata *pdata);
	int (*disable_rx_vlan_stripping)(struct xlgmac_pdata *pdata);
	int (*enable_rx_vlan_filtering)(struct xlgmac_pdata *pdata);
	int (*disable_rx_vlan_filtering)(struct xlgmac_pdata *pdata);
	int (*update_vlan_hash_table)(struct xlgmac_pdata *pdata);

	/* For RX coalescing */
	int (*config_rx_coalesce)(struct xlgmac_pdata *pdata);
	int (*config_tx_coalesce)(struct xlgmac_pdata *pdata);
	unsigned int (*usec_to_riwt)(struct xlgmac_pdata *pdata,
				     unsigned int usec);
	unsigned int (*riwt_to_usec)(struct xlgmac_pdata *pdata,
				     unsigned int riwt);

	/* For RX and TX threshold config */
	int (*config_rx_threshold)(struct xlgmac_pdata *pdata,
				   unsigned int val);
	int (*config_tx_threshold)(struct xlgmac_pdata *pdata,
				   unsigned int val);

	/* For RX and TX Store and Forward Mode config */
	int (*config_rsf_mode)(struct xlgmac_pdata *pdata,
			       unsigned int val);
	int (*config_tsf_mode)(struct xlgmac_pdata *pdata,
			       unsigned int val);

	/* For TX DMA Operate on Second Frame config */
	int (*config_osp_mode)(struct xlgmac_pdata *pdata);

	/* For RX and TX PBL config */
	int (*config_rx_pbl_val)(struct xlgmac_pdata *pdata);
	int (*get_rx_pbl_val)(struct xlgmac_pdata *pdata);
	int (*config_tx_pbl_val)(struct xlgmac_pdata *pdata);
	int (*get_tx_pbl_val)(struct xlgmac_pdata *pdata);
	int (*config_pblx8)(struct xlgmac_pdata *pdata);

	/* For MMC statistics */
	void (*rx_mmc_int)(struct xlgmac_pdata *pdata);
	void (*tx_mmc_int)(struct xlgmac_pdata *pdata);
	void (*read_mmc_stats)(struct xlgmac_pdata *pdata);

	/* For Receive Side Scaling */
	int (*enable_rss)(struct xlgmac_pdata *pdata);
	int (*disable_rss)(struct xlgmac_pdata *pdata);
	int (*set_rss_hash_key)(struct xlgmac_pdata *pdata,
				const u8 *key);
	int (*set_rss_lookup_table)(struct xlgmac_pdata *pdata,
				    const u32 *table);
};

/* This structure contains flags that indicate what hardware features
 * or configurations are present in the device.
 */
struct xlgmac_hw_features {
	/* HW Version */
	unsigned int version;

	/* HW Feature Register0 */
	unsigned int phyifsel;		/* PHY interface support */
	unsigned int vlhash;		/* VLAN Hash Filter */
	unsigned int sma;		/* SMA(MDIO) Interface */
	unsigned int rwk;		/* PMT remote wake-up packet */
	unsigned int mgk;		/* PMT magic packet */
	unsigned int mmc;		/* RMON module */
	unsigned int aoe;		/* ARP Offload */
	unsigned int ts;		/* IEEE 1588-2008 Advanced Timestamp */
	unsigned int eee;		/* Energy Efficient Ethernet */
	unsigned int tx_coe;		/* Tx Checksum Offload */
	unsigned int rx_coe;		/* Rx Checksum Offload */
	unsigned int addn_mac;		/* Additional MAC Addresses */
	unsigned int ts_src;		/* Timestamp Source */
	unsigned int sa_vlan_ins;	/* Source Address or VLAN Insertion */

	/* HW Feature Register1 */
	unsigned int rx_fifo_size;	/* MTL Receive FIFO Size */
	unsigned int tx_fifo_size;	/* MTL Transmit FIFO Size */
	unsigned int adv_ts_hi;		/* Advance Timestamping High Word */
	unsigned int dma_width;		/* DMA width */
	unsigned int dcb;		/* DCB Feature */
	unsigned int sph;		/* Split Header Feature */
	unsigned int tso;		/* TCP Segmentation Offload */
	unsigned int dma_debug;		/* DMA Debug Registers */
	unsigned int rss;		/* Receive Side Scaling */
	unsigned int tc_cnt;		/* Number of Traffic Classes */
	unsigned int hash_table_size;	/* Hash Table Size */
	unsigned int l3l4_filter_num;	/* Number of L3-L4 Filters */

	/* HW Feature Register2 */
	unsigned int rx_q_cnt;		/* Number of MTL Receive Queues */
	unsigned int tx_q_cnt;		/* Number of MTL Transmit Queues */
	unsigned int rx_ch_cnt;		/* Number of DMA Receive Channels */
	unsigned int tx_ch_cnt;		/* Number of DMA Transmit Channels */
	unsigned int pps_out_num;	/* Number of PPS outputs */
	unsigned int aux_snap_num;	/* Number of Aux snapshot inputs */
};

struct xlgmac_resources {
	void __iomem *addr;
	int irq;
};

struct xlgmac_pdata {
	struct net_device *netdev;
	struct device *dev;

	struct xlgmac_hw_ops hw_ops;
	struct xlgmac_desc_ops desc_ops;

	/* Device statistics */
	struct xlgmac_stats stats;

	u32 msg_enable;

	/* MAC registers base */
	void __iomem *mac_regs;

	/* Hardware features of the device */
	struct xlgmac_hw_features hw_feat;

	struct work_struct restart_work;

	/* Rings for Tx/Rx on a DMA channel */
	struct xlgmac_channel *channel_head;
	unsigned int channel_count;
	unsigned int tx_ring_count;
	unsigned int rx_ring_count;
	unsigned int tx_desc_count;
	unsigned int rx_desc_count;
	unsigned int tx_q_count;
	unsigned int rx_q_count;

	/* Tx/Rx common settings */
	unsigned int pblx8;

	/* Tx settings */
	unsigned int tx_sf_mode;
	unsigned int tx_threshold;
	unsigned int tx_pbl;
	unsigned int tx_osp_mode;

	/* Rx settings */
	unsigned int rx_sf_mode;
	unsigned int rx_threshold;
	unsigned int rx_pbl;

	/* Tx coalescing settings */
	unsigned int tx_usecs;
	unsigned int tx_frames;

	/* Rx coalescing settings */
	unsigned int rx_riwt;
	unsigned int rx_usecs;
	unsigned int rx_frames;

	/* Current Rx buffer size */
	unsigned int rx_buf_size;

	/* Flow control settings */
	unsigned int tx_pause;
	unsigned int rx_pause;

	/* Device interrupt number */
	int dev_irq;
	unsigned int per_channel_irq;
	int channel_irq[XLGMAC_MAX_DMA_CHANNELS];

	/* Netdev related settings */
	unsigned char mac_addr[ETH_ALEN];
	netdev_features_t netdev_features;
	struct napi_struct napi;

	/* Filtering support */
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];

	/* Device clocks */
	unsigned long sysclk_rate;

	/* RSS addressing mutex */
	struct mutex rss_mutex;

	/* Receive Side Scaling settings */
	u8 rss_key[XLGMAC_RSS_HASH_KEY_SIZE];
	u32 rss_table[XLGMAC_RSS_MAX_TABLE_SIZE];
	u32 rss_options;

	int phy_speed;

	char drv_name[32];
	char drv_ver[32];
};

void xlgmac_init_desc_ops(struct xlgmac_desc_ops *desc_ops);
void xlgmac_init_hw_ops(struct xlgmac_hw_ops *hw_ops);
const struct net_device_ops *xlgmac_get_netdev_ops(void);
const struct ethtool_ops *xlgmac_get_ethtool_ops(void);
void xlgmac_dump_tx_desc(struct xlgmac_pdata *pdata,
			 struct xlgmac_ring *ring,
			 unsigned int idx,
			 unsigned int count,
			 unsigned int flag);
void xlgmac_dump_rx_desc(struct xlgmac_pdata *pdata,
			 struct xlgmac_ring *ring,
			 unsigned int idx);
void xlgmac_print_pkt(struct net_device *netdev,
		      struct sk_buff *skb, bool tx_rx);
void xlgmac_get_all_hw_features(struct xlgmac_pdata *pdata);
void xlgmac_print_all_hw_features(struct xlgmac_pdata *pdata);
int xlgmac_drv_probe(struct device *dev,
		     struct xlgmac_resources *res);
int xlgmac_drv_remove(struct device *dev);

/* For debug prints */
#ifdef XLGMAC_DEBUG
#define XLGMAC_PR(fmt, args...) \
	pr_alert("[%s,%d]:" fmt, __func__, __LINE__, ## args)
#else
#define XLGMAC_PR(x...)		do { } while (0)
#endif

#endif /* __DWC_XLGMAC_H__ */
