/* SPDX-License-Identifier: GPL-2.0 */
/* Copyright(c) 2020 - 2025 Mucse Corporation. */

#ifndef _RNPGBE_H
#define _RNPGBE_H

#include <linux/types.h>
#include <linux/mutex.h>
#include <linux/netdevice.h>
#include <linux/timer.h>
#include <linux/if.h>
#include <linux/workqueue.h>

#include "rnpgbe_hw.h"

enum rnpgbe_boards {
	board_n500,
	board_n210
};

struct mucse_mbx_info {
	u32 timeout_us;
	u32 delay_us;
	u16 fw_req;
	u16 fw_ack;
	/* lock for only one use mbx */
	struct mutex lock;
	/* fw <--> pf mbx */
	u32 fwpf_shm_base;
	u32 pf2fw_mbx_ctrl;
	u32 fwpf_mbx_mask;
	u32 fwpf_ctrl_base;
};

/* Enum for firmware notification modes,
 * more modes (e.g., portup, link_report) will be added in future
 **/
enum {
	mucse_fw_powerup,
};

struct mucse_hw {
	void __iomem *hw_addr;
	void __iomem *ring_msix_base;
	struct pci_dev *pdev;
	struct mucse_mbx_info mbx;
	int port;
	u16 cycles_per_us;
	u8 pfvfnum;
};

struct rnpgbe_tx_desc {
	__le64 pkt_addr; /* Packet buffer address */
	union {
		__le64 vlan_cmd_bsz;
		struct {
			__le32 blen_mac_ip_len;
			__le32 vlan_cmd; /* vlan & cmd status */
		};
	};
#define M_TXD_CMD_RS          0x040000 /* Report Status */
#define M_TXD_STAT_DD         0x020000 /* Descriptor Done */
#define M_TXD_CMD_EOP         0x010000 /* End of Packet */
};

union rnpgbe_rx_desc {
	struct {
		__le64 pkt_addr; /* Packet buffer address */
		__le64 resv_cmd; /* cmd status */
	};
	struct {
		__le32 rss_hash; /* RSS HASH */
		__le16 mark; /* mark info */
		__le16 rev1;
		__le16 len; /* Packet length */
		__le16 padding_len;
		__le16 vlan; /* VLAN tag */
		__le16 cmd; /* cmd status */
#define M_RXD_STAT_DD         BIT(1) /* Descriptor Done */
#define M_RXD_STAT_EOP        BIT(0) /* End of Packet */
	} wb;
};

#define M_TX_DESC(R, i) (&(((struct rnpgbe_tx_desc *)((R)->desc))[i]))
#define M_RX_DESC(R, i) (&(((union rnpgbe_rx_desc *)((R)->desc))[i]))

static inline __le16 rnpgbe_test_staterr(union rnpgbe_rx_desc *rx_desc,
					 const u16 stat_err_bits)
{
	return rx_desc->wb.cmd & cpu_to_le16(stat_err_bits);
}

struct mucse_tx_buffer {
	struct rnpgbe_tx_desc *next_to_watch;
	struct sk_buff *skb;
	unsigned int bytecount;
	unsigned short gso_segs;
	DEFINE_DMA_UNMAP_ADDR(dma);
	DEFINE_DMA_UNMAP_LEN(len);
	bool mapped_as_page;  /* true if dma was mapped with dma_map_page */
};

struct mucse_queue_stats {
	u64 packets;
	u64 bytes;
	u64 length_errors;
	atomic64_t dropped;
	struct u64_stats_sync syncp;
} ____cacheline_aligned_in_smp;

struct mucse_rx_buffer {
	struct sk_buff *skb;
	dma_addr_t dma;
	struct page *page;
	u32 page_offset;
};

enum mucse_rx_drop_reason {
	MUCSE_RX_DROP_NONE,
	MUCSE_RX_DROP_RESOURCE,
	MUCSE_RX_DROP_LENGTH,
};

struct mucse_ring {
	struct mucse_ring *next;
	struct mucse_q_vector *q_vector;
	struct net_device *netdev;
	struct device *dev;
	struct page_pool *page_pool;
	void *desc;
	union {
		struct mucse_tx_buffer *tx_buffer_info;
		struct mucse_rx_buffer *rx_buffer_info;
	};
	void __iomem *ring_addr;
	void __iomem *tail;
	void __iomem *irq_mask;
	void __iomem *trig;
	u8 queue_index;
	/* hw ring idx */
	u8 rnpgbe_queue_idx;
	u8 pfvfnum;
	u16 count;
	u16 next_to_use;
	u16 next_to_clean;
	dma_addr_t dma;
	unsigned int size;
	u32 drop_bytes; /* bytes in the current frame being dropped */
	struct mucse_queue_stats *stats;
	enum mucse_rx_drop_reason drop_reason;
} ____cacheline_internodealigned_in_smp;

/* Refill RX descriptors in batches of this size. */
#define M_RX_BUFFER_WRITE	16

static inline u16 mucse_desc_unused(struct mucse_ring *ring)
{
	u16 ntc = ring->next_to_clean;
	u16 ntu = ring->next_to_use;

	return ((ntc > ntu) ? 0 : ring->count) + ntc - ntu - 1;
}

static inline u16 mucse_desc_unused_rx(struct mucse_ring *ring)
{
	u16 ntc = ring->next_to_clean;
	u16 ntu = ring->next_to_use;

	/* Keep M_RX_BUFFER_WRITE descriptors unused so the ring is not filled
	 * completely. Refill is attempted once at least this many descriptors
	 * are available.
	 */
	return ((ntc > ntu) ? 0 : ring->count) + ntc - ntu - M_RX_BUFFER_WRITE;
}

static inline __le64 build_ctob(u32 vlan_cmd, u32 mac_ip_len, u32 size)
{
	return cpu_to_le64(((u64)vlan_cmd << 32) | ((u64)mac_ip_len << 16) |
			   ((u64)size));
}

static inline struct netdev_queue *txring_txq(const struct mucse_ring *ring)
{
	return netdev_get_tx_queue(ring->netdev, ring->queue_index);
}

struct mucse_ring_container {
	struct mucse_ring *ring;
	u16 count;
};

struct mucse_q_vector {
	struct mucse *mucse;
	/* hardware interrupt vector number */
	int hw_vector;
	struct mucse_ring_container rx, tx;
	struct napi_struct napi;
	struct timer_list rx_alloc_timer;
	char name[IFNAMSIZ + 18];
	/* for dynamic allocation of rings associated with this q_vector */
	struct mucse_ring ring[] ____cacheline_internodealigned_in_smp;
};

#define MAX_Q_VECTORS 8

#define M_DEFAULT_TXD     512
#define M_DEFAULT_RXD     512
#define M_DEFAULT_TX_WORK 256

enum mucse_state_t {
	__MUCSE_AXI_FAULT,
};

struct mucse {
	struct net_device *netdev;
	struct pci_dev *pdev;
	struct mucse_hw hw;
	struct mucse_ring *tx_ring[RNPGBE_MAX_QUEUES]
		____cacheline_aligned_in_smp;
	struct mucse_ring *rx_ring[RNPGBE_MAX_QUEUES]
		____cacheline_aligned_in_smp;
	struct mucse_queue_stats tx_stats[RNPGBE_MAX_QUEUES];
	struct mucse_queue_stats rx_stats[RNPGBE_MAX_QUEUES];
	struct mucse_q_vector *q_vector[MAX_Q_VECTORS];
	int tx_ring_item_count;
	int tx_work_limit;
	int num_tx_queues;
	int num_q_vectors;
	int rx_ring_item_count;
	int num_rx_queues;
	char mbx_name[32];
	bool mbx_irq_requested;
	unsigned long state;
	struct work_struct mbx_work;
};

int rnpgbe_get_permanent_mac(struct mucse_hw *hw, u8 *perm_addr);
int rnpgbe_reset_hw(struct mucse_hw *hw);
int rnpgbe_send_notify(struct mucse_hw *hw,
		       bool enable,
		       int mode);
int rnpgbe_init_hw(struct mucse_hw *hw, int board_type);

/* Device IDs */
#define PCI_VENDOR_ID_MUCSE               0x8848
#define RNPGBE_DEVICE_ID_N500_QUAD_PORT   0x8308
#define RNPGBE_DEVICE_ID_N500_DUAL_PORT   0x8318
#define RNPGBE_DEVICE_ID_N210             0x8208
#define RNPGBE_DEVICE_ID_N210L            0x820a

#define mucse_hw_wr32(hw, reg, val) \
	writel((val), (hw)->hw_addr + (reg))
#define mucse_hw_rd32(hw, reg) \
	readl((hw)->hw_addr + (reg))
#define mucse_ring_wr32(ring, reg, val) \
	writel((val), (ring)->ring_addr + (reg))
#define mucse_ring_rd32(ring, reg) \
	readl((ring)->ring_addr + (reg))
#endif /* _RNPGBE_H */
