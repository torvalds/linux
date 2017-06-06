/*
 * Copyright (C) 2015-2017 Netronome Systems, Inc.
 *
 * This software is dual licensed under the GNU General License Version 2,
 * June 1991 as shown in the file COPYING in the top-level directory of this
 * source tree or the BSD 2-Clause License provided below.  You have the
 * option to license this software under the complete terms of either license.
 *
 * The BSD 2-Clause License:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      1. Redistributions of source code must retain the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer.
 *
 *      2. Redistributions in binary form must reproduce the above
 *         copyright notice, this list of conditions and the following
 *         disclaimer in the documentation and/or other materials
 *         provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

/*
 * nfp_net.h
 * Declarations for Netronome network device driver.
 * Authors: Jakub Kicinski <jakub.kicinski@netronome.com>
 *          Jason McMullan <jason.mcmullan@netronome.com>
 *          Rolf Neugebauer <rolf.neugebauer@netronome.com>
 */

#ifndef _NFP_NET_H_
#define _NFP_NET_H_

#include <linux/interrupt.h>
#include <linux/list.h>
#include <linux/netdevice.h>
#include <linux/pci.h>
#include <linux/io-64-nonatomic-hi-lo.h>

#include "nfp_net_ctrl.h"

#define nn_pr(nn, lvl, fmt, args...)					\
	({								\
		struct nfp_net *__nn = (nn);				\
									\
		if (__nn->dp.netdev)					\
			netdev_printk(lvl, __nn->dp.netdev, fmt, ## args); \
		else							\
			dev_printk(lvl, __nn->dp.dev, "ctrl: " fmt, ## args); \
	})

#define nn_err(nn, fmt, args...)	nn_pr(nn, KERN_ERR, fmt, ## args)
#define nn_warn(nn, fmt, args...)	nn_pr(nn, KERN_WARNING, fmt, ## args)
#define nn_info(nn, fmt, args...)	nn_pr(nn, KERN_INFO, fmt, ## args)
#define nn_dbg(nn, fmt, args...)	nn_pr(nn, KERN_DEBUG, fmt, ## args)

#define nn_dp_warn(dp, fmt, args...)					\
	({								\
		struct nfp_net_dp *__dp = (dp);				\
									\
		if (unlikely(net_ratelimit())) {			\
			if (__dp->netdev)				\
				netdev_warn(__dp->netdev, fmt, ## args); \
			else						\
				dev_warn(__dp->dev, fmt, ## args);	\
		}							\
	})

/* Max time to wait for NFP to respond on updates (in seconds) */
#define NFP_NET_POLL_TIMEOUT	5

/* Interval for reading offloaded filter stats */
#define NFP_NET_STAT_POLL_IVL	msecs_to_jiffies(100)

/* Bar allocation */
#define NFP_NET_CTRL_BAR	0
#define NFP_NET_Q0_BAR		2
#define NFP_NET_Q1_BAR		4	/* OBSOLETE */

/* Max bits in DMA address */
#define NFP_NET_MAX_DMA_BITS	40

/* Default size for MTU and freelist buffer sizes */
#define NFP_NET_DEFAULT_MTU		1500

/* Maximum number of bytes prepended to a packet */
#define NFP_NET_MAX_PREPEND		64

/* Interrupt definitions */
#define NFP_NET_NON_Q_VECTORS		2
#define NFP_NET_IRQ_LSC_IDX		0
#define NFP_NET_IRQ_EXN_IDX		1
#define NFP_NET_MIN_VNIC_IRQS		(NFP_NET_NON_Q_VECTORS + 1)

/* Queue/Ring definitions */
#define NFP_NET_MAX_TX_RINGS	64	/* Max. # of Tx rings per device */
#define NFP_NET_MAX_RX_RINGS	64	/* Max. # of Rx rings per device */
#define NFP_NET_MAX_R_VECS	(NFP_NET_MAX_TX_RINGS > NFP_NET_MAX_RX_RINGS ? \
				 NFP_NET_MAX_TX_RINGS : NFP_NET_MAX_RX_RINGS)
#define NFP_NET_MAX_IRQS	(NFP_NET_NON_Q_VECTORS + NFP_NET_MAX_R_VECS)

#define NFP_NET_MIN_TX_DESCS	256	/* Min. # of Tx descs per ring */
#define NFP_NET_MIN_RX_DESCS	256	/* Min. # of Rx descs per ring */
#define NFP_NET_MAX_TX_DESCS	(256 * 1024) /* Max. # of Tx descs per ring */
#define NFP_NET_MAX_RX_DESCS	(256 * 1024) /* Max. # of Rx descs per ring */

#define NFP_NET_TX_DESCS_DEFAULT 4096	/* Default # of Tx descs per ring */
#define NFP_NET_RX_DESCS_DEFAULT 4096	/* Default # of Rx descs per ring */

#define NFP_NET_FL_BATCH	16	/* Add freelist in this Batch size */
#define NFP_NET_XDP_MAX_COMPLETE 2048	/* XDP bufs to reclaim in NAPI poll */

/* Offload definitions */
#define NFP_NET_N_VXLAN_PORTS	(NFP_NET_CFG_VXLAN_SZ / sizeof(__be16))

#define NFP_NET_RX_BUF_HEADROOM	(NET_SKB_PAD + NET_IP_ALIGN)
#define NFP_NET_RX_BUF_NON_DATA	(NFP_NET_RX_BUF_HEADROOM +		\
				 SKB_DATA_ALIGN(sizeof(struct skb_shared_info)))

/* Forward declarations */
struct nfp_cpp;
struct nfp_eth_table_port;
struct nfp_net;
struct nfp_net_r_vector;
struct nfp_port;

/* Convenience macro for wrapping descriptor index on ring size */
#define D_IDX(ring, idx)	((idx) & ((ring)->cnt - 1))

/* Convenience macro for writing dma address into RX/TX descriptors */
#define nfp_desc_set_dma_addr(desc, dma_addr)				\
	do {								\
		__typeof(desc) __d = (desc);				\
		dma_addr_t __addr = (dma_addr);				\
									\
		__d->dma_addr_lo = cpu_to_le32(lower_32_bits(__addr));	\
		__d->dma_addr_hi = upper_32_bits(__addr) & 0xff;	\
	} while (0)

/* TX descriptor format */

#define PCIE_DESC_TX_EOP		BIT(7)
#define PCIE_DESC_TX_OFFSET_MASK	GENMASK(6, 0)
#define PCIE_DESC_TX_MSS_MASK		GENMASK(13, 0)

/* Flags in the host TX descriptor */
#define PCIE_DESC_TX_CSUM		BIT(7)
#define PCIE_DESC_TX_IP4_CSUM		BIT(6)
#define PCIE_DESC_TX_TCP_CSUM		BIT(5)
#define PCIE_DESC_TX_UDP_CSUM		BIT(4)
#define PCIE_DESC_TX_VLAN		BIT(3)
#define PCIE_DESC_TX_LSO		BIT(2)
#define PCIE_DESC_TX_ENCAP		BIT(1)
#define PCIE_DESC_TX_O_IP4_CSUM	BIT(0)

struct nfp_net_tx_desc {
	union {
		struct {
			u8 dma_addr_hi; /* High bits of host buf address */
			__le16 dma_len;	/* Length to DMA for this desc */
			u8 offset_eop;	/* Offset in buf where pkt starts +
					 * highest bit is eop flag.
					 */
			__le32 dma_addr_lo; /* Low 32bit of host buf addr */

			__le16 mss;	/* MSS to be used for LSO */
			u8 lso_hdrlen;	/* LSO, TCP payload offset */
			u8 flags;	/* TX Flags, see @PCIE_DESC_TX_* */
			union {
				struct {
					u8 l3_offset; /* L3 header offset */
					u8 l4_offset; /* L4 header offset */
				};
				__le16 vlan; /* VLAN tag to add if indicated */
			};
			__le16 data_len; /* Length of frame + meta data */
		} __packed;
		__le32 vals[4];
	};
};

/**
 * struct nfp_net_tx_buf - software TX buffer descriptor
 * @skb:	sk_buff associated with this buffer
 * @dma_addr:	DMA mapping address of the buffer
 * @fidx:	Fragment index (-1 for the head and [0..nr_frags-1] for frags)
 * @pkt_cnt:	Number of packets to be produced out of the skb associated
 *		with this buffer (valid only on the head's buffer).
 *		Will be 1 for all non-TSO packets.
 * @real_len:	Number of bytes which to be produced out of the skb (valid only
 *		on the head's buffer). Equal to skb->len for non-TSO packets.
 */
struct nfp_net_tx_buf {
	union {
		struct sk_buff *skb;
		void *frag;
	};
	dma_addr_t dma_addr;
	short int fidx;
	u16 pkt_cnt;
	u32 real_len;
};

/**
 * struct nfp_net_tx_ring - TX ring structure
 * @r_vec:      Back pointer to ring vector structure
 * @idx:        Ring index from Linux's perspective
 * @qcidx:      Queue Controller Peripheral (QCP) queue index for the TX queue
 * @qcp_q:      Pointer to base of the QCP TX queue
 * @cnt:        Size of the queue in number of descriptors
 * @wr_p:       TX ring write pointer (free running)
 * @rd_p:       TX ring read pointer (free running)
 * @qcp_rd_p:   Local copy of QCP TX queue read pointer
 * @wr_ptr_add:	Accumulated number of buffers to add to QCP write pointer
 *		(used for .xmit_more delayed kick)
 * @txbufs:     Array of transmitted TX buffers, to free on transmit
 * @txds:       Virtual address of TX ring in host memory
 * @dma:        DMA address of the TX ring
 * @size:       Size, in bytes, of the TX ring (needed to free)
 * @is_xdp:	Is this a XDP TX ring?
 */
struct nfp_net_tx_ring {
	struct nfp_net_r_vector *r_vec;

	u32 idx;
	int qcidx;
	u8 __iomem *qcp_q;

	u32 cnt;
	u32 wr_p;
	u32 rd_p;
	u32 qcp_rd_p;

	u32 wr_ptr_add;

	struct nfp_net_tx_buf *txbufs;
	struct nfp_net_tx_desc *txds;

	dma_addr_t dma;
	unsigned int size;
	bool is_xdp;
} ____cacheline_aligned;

/* RX and freelist descriptor format */

#define PCIE_DESC_RX_DD			BIT(7)
#define PCIE_DESC_RX_META_LEN_MASK	GENMASK(6, 0)

/* Flags in the RX descriptor */
#define PCIE_DESC_RX_RSS		cpu_to_le16(BIT(15))
#define PCIE_DESC_RX_I_IP4_CSUM		cpu_to_le16(BIT(14))
#define PCIE_DESC_RX_I_IP4_CSUM_OK	cpu_to_le16(BIT(13))
#define PCIE_DESC_RX_I_TCP_CSUM		cpu_to_le16(BIT(12))
#define PCIE_DESC_RX_I_TCP_CSUM_OK	cpu_to_le16(BIT(11))
#define PCIE_DESC_RX_I_UDP_CSUM		cpu_to_le16(BIT(10))
#define PCIE_DESC_RX_I_UDP_CSUM_OK	cpu_to_le16(BIT(9))
#define PCIE_DESC_RX_BPF		cpu_to_le16(BIT(8))
#define PCIE_DESC_RX_EOP		cpu_to_le16(BIT(7))
#define PCIE_DESC_RX_IP4_CSUM		cpu_to_le16(BIT(6))
#define PCIE_DESC_RX_IP4_CSUM_OK	cpu_to_le16(BIT(5))
#define PCIE_DESC_RX_TCP_CSUM		cpu_to_le16(BIT(4))
#define PCIE_DESC_RX_TCP_CSUM_OK	cpu_to_le16(BIT(3))
#define PCIE_DESC_RX_UDP_CSUM		cpu_to_le16(BIT(2))
#define PCIE_DESC_RX_UDP_CSUM_OK	cpu_to_le16(BIT(1))
#define PCIE_DESC_RX_VLAN		cpu_to_le16(BIT(0))

#define PCIE_DESC_RX_CSUM_ALL		(PCIE_DESC_RX_IP4_CSUM |	\
					 PCIE_DESC_RX_TCP_CSUM |	\
					 PCIE_DESC_RX_UDP_CSUM |	\
					 PCIE_DESC_RX_I_IP4_CSUM |	\
					 PCIE_DESC_RX_I_TCP_CSUM |	\
					 PCIE_DESC_RX_I_UDP_CSUM)
#define PCIE_DESC_RX_CSUM_OK_SHIFT	1
#define __PCIE_DESC_RX_CSUM_ALL		le16_to_cpu(PCIE_DESC_RX_CSUM_ALL)
#define __PCIE_DESC_RX_CSUM_ALL_OK	(__PCIE_DESC_RX_CSUM_ALL >>	\
					 PCIE_DESC_RX_CSUM_OK_SHIFT)

struct nfp_net_rx_desc {
	union {
		struct {
			u8 dma_addr_hi;	/* High bits of the buf address */
			__le16 reserved; /* Must be zero */
			u8 meta_len_dd; /* Must be zero */

			__le32 dma_addr_lo; /* Low bits of the buffer address */
		} __packed fld;

		struct {
			__le16 data_len; /* Length of the frame + meta data */
			u8 reserved;
			u8 meta_len_dd;	/* Length of meta data prepended +
					 * descriptor done flag.
					 */

			__le16 flags;	/* RX flags. See @PCIE_DESC_RX_* */
			__le16 vlan;	/* VLAN if stripped */
		} __packed rxd;

		__le32 vals[2];
	};
};

#define NFP_NET_META_FIELD_MASK GENMASK(NFP_NET_META_FIELD_SIZE - 1, 0)

struct nfp_meta_parsed {
	u8 hash_type;
	u8 csum_type;
	u32 hash;
	u32 mark;
	__wsum csum;
};

struct nfp_net_rx_hash {
	__be32 hash_type;
	__be32 hash;
};

/**
 * struct nfp_net_rx_buf - software RX buffer descriptor
 * @frag:	page fragment buffer
 * @dma_addr:	DMA mapping address of the buffer
 */
struct nfp_net_rx_buf {
	void *frag;
	dma_addr_t dma_addr;
};

/**
 * struct nfp_net_rx_ring - RX ring structure
 * @r_vec:      Back pointer to ring vector structure
 * @cnt:        Size of the queue in number of descriptors
 * @wr_p:       FL/RX ring write pointer (free running)
 * @rd_p:       FL/RX ring read pointer (free running)
 * @idx:        Ring index from Linux's perspective
 * @fl_qcidx:   Queue Controller Peripheral (QCP) queue index for the freelist
 * @qcp_fl:     Pointer to base of the QCP freelist queue
 * @rxbufs:     Array of transmitted FL/RX buffers
 * @rxds:       Virtual address of FL/RX ring in host memory
 * @dma:        DMA address of the FL/RX ring
 * @size:       Size, in bytes, of the FL/RX ring (needed to free)
 */
struct nfp_net_rx_ring {
	struct nfp_net_r_vector *r_vec;

	u32 cnt;
	u32 wr_p;
	u32 rd_p;

	u32 idx;

	int fl_qcidx;
	u8 __iomem *qcp_fl;

	struct nfp_net_rx_buf *rxbufs;
	struct nfp_net_rx_desc *rxds;

	dma_addr_t dma;
	unsigned int size;
} ____cacheline_aligned;

/**
 * struct nfp_net_r_vector - Per ring interrupt vector configuration
 * @nfp_net:        Backpointer to nfp_net structure
 * @napi:           NAPI structure for this ring vec
 * @tx_ring:        Pointer to TX ring
 * @rx_ring:        Pointer to RX ring
 * @xdp_ring:	    Pointer to an extra TX ring for XDP
 * @irq_entry:      MSI-X table entry (use for talking to the device)
 * @rx_sync:	    Seqlock for atomic updates of RX stats
 * @rx_pkts:        Number of received packets
 * @rx_bytes:	    Number of received bytes
 * @rx_drops:	    Number of packets dropped on RX due to lack of resources
 * @hw_csum_rx_ok:  Counter of packets where the HW checksum was OK
 * @hw_csum_rx_inner_ok: Counter of packets where the inner HW checksum was OK
 * @hw_csum_rx_error:	 Counter of packets with bad checksums
 * @tx_sync:	    Seqlock for atomic updates of TX stats
 * @tx_pkts:	    Number of Transmitted packets
 * @tx_bytes:	    Number of Transmitted bytes
 * @hw_csum_tx:	    Counter of packets with TX checksum offload requested
 * @hw_csum_tx_inner:	 Counter of inner TX checksum offload requests
 * @tx_gather:	    Counter of packets with Gather DMA
 * @tx_lso:	    Counter of LSO packets sent
 * @tx_errors:	    How many TX errors were encountered
 * @tx_busy:        How often was TX busy (no space)?
 * @irq_vector:     Interrupt vector number (use for talking to the OS)
 * @handler:        Interrupt handler for this ring vector
 * @name:           Name of the interrupt vector
 * @affinity_mask:  SMP affinity mask for this vector
 *
 * This structure ties RX and TX rings to interrupt vectors and a NAPI
 * context. This currently only supports one RX and TX ring per
 * interrupt vector but might be extended in the future to allow
 * association of multiple rings per vector.
 */
struct nfp_net_r_vector {
	struct nfp_net *nfp_net;
	struct napi_struct napi;

	struct nfp_net_tx_ring *tx_ring;
	struct nfp_net_rx_ring *rx_ring;

	u16 irq_entry;

	struct u64_stats_sync rx_sync;
	u64 rx_pkts;
	u64 rx_bytes;
	u64 rx_drops;
	u64 hw_csum_rx_ok;
	u64 hw_csum_rx_inner_ok;
	u64 hw_csum_rx_error;

	struct nfp_net_tx_ring *xdp_ring;

	struct u64_stats_sync tx_sync;
	u64 tx_pkts;
	u64 tx_bytes;
	u64 hw_csum_tx;
	u64 hw_csum_tx_inner;
	u64 tx_gather;
	u64 tx_lso;
	u64 tx_errors;
	u64 tx_busy;

	u32 irq_vector;
	irq_handler_t handler;
	char name[IFNAMSIZ + 8];
	cpumask_t affinity_mask;
} ____cacheline_aligned;

/* Firmware version as it is written in the 32bit value in the BAR */
struct nfp_net_fw_version {
	u8 minor;
	u8 major;
	u8 class;
	u8 resv;
} __packed;

static inline bool nfp_net_fw_ver_eq(struct nfp_net_fw_version *fw_ver,
				     u8 resv, u8 class, u8 major, u8 minor)
{
	return fw_ver->resv == resv &&
	       fw_ver->class == class &&
	       fw_ver->major == major &&
	       fw_ver->minor == minor;
}

struct nfp_stat_pair {
	u64 pkts;
	u64 bytes;
};

/**
 * struct nfp_net_dp - NFP network device datapath data structure
 * @dev:		Backpointer to struct device
 * @netdev:		Backpointer to net_device structure
 * @is_vf:		Is the driver attached to a VF?
 * @bpf_offload_skip_sw:  Offloaded BPF program will not be rerun by cls_bpf
 * @bpf_offload_xdp:	Offloaded BPF program is XDP
 * @chained_metadata_format:  Firemware will use new metadata format
 * @rx_dma_dir:		Mapping direction for RX buffers
 * @rx_dma_off:		Offset at which DMA packets (for XDP headroom)
 * @rx_offset:		Offset in the RX buffers where packet data starts
 * @ctrl:		Local copy of the control register/word.
 * @fl_bufsz:		Currently configured size of the freelist buffers
 * @xdp_prog:		Installed XDP program
 * @tx_rings:		Array of pre-allocated TX ring structures
 * @rx_rings:		Array of pre-allocated RX ring structures
 * @ctrl_bar:		Pointer to mapped control BAR
 *
 * @txd_cnt:		Size of the TX ring in number of descriptors
 * @rxd_cnt:		Size of the RX ring in number of descriptors
 * @num_r_vecs:		Number of used ring vectors
 * @num_tx_rings:	Currently configured number of TX rings
 * @num_stack_tx_rings:	Number of TX rings used by the stack (not XDP)
 * @num_rx_rings:	Currently configured number of RX rings
 * @mtu:		Device MTU
 */
struct nfp_net_dp {
	struct device *dev;
	struct net_device *netdev;

	u8 is_vf:1;
	u8 bpf_offload_skip_sw:1;
	u8 bpf_offload_xdp:1;
	u8 chained_metadata_format:1;

	u8 rx_dma_dir;
	u8 rx_offset;

	u32 rx_dma_off;

	u32 ctrl;
	u32 fl_bufsz;

	struct bpf_prog *xdp_prog;

	struct nfp_net_tx_ring *tx_rings;
	struct nfp_net_rx_ring *rx_rings;

	u8 __iomem *ctrl_bar;

	/* Cold data follows */

	unsigned int txd_cnt;
	unsigned int rxd_cnt;

	unsigned int num_r_vecs;

	unsigned int num_tx_rings;
	unsigned int num_stack_tx_rings;
	unsigned int num_rx_rings;

	unsigned int mtu;
};

/**
 * struct nfp_net - NFP network device structure
 * @dp:			Datapath structure
 * @fw_ver:		Firmware version
 * @cap:                Capabilities advertised by the Firmware
 * @max_mtu:            Maximum support MTU advertised by the Firmware
 * @rss_hfunc:		RSS selected hash function
 * @rss_cfg:            RSS configuration
 * @rss_key:            RSS secret key
 * @rss_itbl:           RSS indirection table
 * @max_r_vecs:		Number of allocated interrupt vectors for RX/TX
 * @max_tx_rings:       Maximum number of TX rings supported by the Firmware
 * @max_rx_rings:       Maximum number of RX rings supported by the Firmware
 * @r_vecs:             Pre-allocated array of ring vectors
 * @irq_entries:        Pre-allocated array of MSI-X entries
 * @lsc_handler:        Handler for Link State Change interrupt
 * @lsc_name:           Name for Link State Change interrupt
 * @exn_handler:        Handler for Exception interrupt
 * @exn_name:           Name for Exception interrupt
 * @shared_handler:     Handler for shared interrupts
 * @shared_name:        Name for shared interrupt
 * @me_freq_mhz:        ME clock_freq (MHz)
 * @reconfig_lock:	Protects HW reconfiguration request regs/machinery
 * @reconfig_posted:	Pending reconfig bits coming from async sources
 * @reconfig_timer_active:  Timer for reading reconfiguration results is pending
 * @reconfig_sync_present:  Some thread is performing synchronous reconfig
 * @reconfig_timer:	Timer for async reading of reconfig results
 * @link_up:            Is the link up?
 * @link_status_lock:	Protects @link_* and ensures atomicity with BAR reading
 * @rx_coalesce_usecs:      RX interrupt moderation usecs delay parameter
 * @rx_coalesce_max_frames: RX interrupt moderation frame count parameter
 * @tx_coalesce_usecs:      TX interrupt moderation usecs delay parameter
 * @tx_coalesce_max_frames: TX interrupt moderation frame count parameter
 * @vxlan_ports:	VXLAN ports for RX inner csum offload communicated to HW
 * @vxlan_usecnt:	IPv4/IPv6 VXLAN port use counts
 * @qcp_cfg:            Pointer to QCP queue used for configuration notification
 * @tx_bar:             Pointer to mapped TX queues
 * @rx_bar:             Pointer to mapped FL/RX queues
 * @debugfs_dir:	Device directory in debugfs
 * @ethtool_dump_flag:	Ethtool dump flag
 * @vnic_list:		Entry on device vNIC list
 * @pdev:		Backpointer to PCI device
 * @app:		APP handle if available
 * @port:		Pointer to nfp_port structure if vNIC is a port
 * @app_priv:		APP private data for this vNIC
 */
struct nfp_net {
	struct nfp_net_dp dp;

	struct nfp_net_fw_version fw_ver;

	u32 cap;
	u32 max_mtu;

	u8 rss_hfunc;
	u32 rss_cfg;
	u8 rss_key[NFP_NET_CFG_RSS_KEY_SZ];
	u8 rss_itbl[NFP_NET_CFG_RSS_ITBL_SZ];

	unsigned int max_tx_rings;
	unsigned int max_rx_rings;

	int stride_tx;
	int stride_rx;

	unsigned int max_r_vecs;
	struct nfp_net_r_vector r_vecs[NFP_NET_MAX_R_VECS];
	struct msix_entry irq_entries[NFP_NET_MAX_IRQS];

	irq_handler_t lsc_handler;
	char lsc_name[IFNAMSIZ + 8];

	irq_handler_t exn_handler;
	char exn_name[IFNAMSIZ + 8];

	irq_handler_t shared_handler;
	char shared_name[IFNAMSIZ + 8];

	u32 me_freq_mhz;

	bool link_up;
	spinlock_t link_status_lock;

	spinlock_t reconfig_lock;
	u32 reconfig_posted;
	bool reconfig_timer_active;
	bool reconfig_sync_present;
	struct timer_list reconfig_timer;

	u32 rx_coalesce_usecs;
	u32 rx_coalesce_max_frames;
	u32 tx_coalesce_usecs;
	u32 tx_coalesce_max_frames;

	__be16 vxlan_ports[NFP_NET_N_VXLAN_PORTS];
	u8 vxlan_usecnt[NFP_NET_N_VXLAN_PORTS];

	u8 __iomem *qcp_cfg;

	u8 __iomem *tx_bar;
	u8 __iomem *rx_bar;

	struct dentry *debugfs_dir;
	u32 ethtool_dump_flag;

	struct list_head vnic_list;

	struct pci_dev *pdev;
	struct nfp_app *app;

	struct nfp_port *port;

	void *app_priv;
};

/* Functions to read/write from/to a BAR
 * Performs any endian conversion necessary.
 */
static inline u16 nn_readb(struct nfp_net *nn, int off)
{
	return readb(nn->dp.ctrl_bar + off);
}

static inline void nn_writeb(struct nfp_net *nn, int off, u8 val)
{
	writeb(val, nn->dp.ctrl_bar + off);
}

static inline u16 nn_readw(struct nfp_net *nn, int off)
{
	return readw(nn->dp.ctrl_bar + off);
}

static inline void nn_writew(struct nfp_net *nn, int off, u16 val)
{
	writew(val, nn->dp.ctrl_bar + off);
}

static inline u32 nn_readl(struct nfp_net *nn, int off)
{
	return readl(nn->dp.ctrl_bar + off);
}

static inline void nn_writel(struct nfp_net *nn, int off, u32 val)
{
	writel(val, nn->dp.ctrl_bar + off);
}

static inline u64 nn_readq(struct nfp_net *nn, int off)
{
	return readq(nn->dp.ctrl_bar + off);
}

static inline void nn_writeq(struct nfp_net *nn, int off, u64 val)
{
	writeq(val, nn->dp.ctrl_bar + off);
}

/* Flush posted PCI writes by reading something without side effects */
static inline void nn_pci_flush(struct nfp_net *nn)
{
	nn_readl(nn, NFP_NET_CFG_VERSION);
}

/* Queue Controller Peripheral access functions and definitions.
 *
 * Some of the BARs of the NFP are mapped to portions of the Queue
 * Controller Peripheral (QCP) address space on the NFP.  A QCP queue
 * has a read and a write pointer (as well as a size and flags,
 * indicating overflow etc).  The QCP offers a number of different
 * operation on queue pointers, but here we only offer function to
 * either add to a pointer or to read the pointer value.
 */
#define NFP_QCP_QUEUE_ADDR_SZ			0x800
#define NFP_QCP_QUEUE_OFF(_x)			((_x) * NFP_QCP_QUEUE_ADDR_SZ)
#define NFP_QCP_QUEUE_ADD_RPTR			0x0000
#define NFP_QCP_QUEUE_ADD_WPTR			0x0004
#define NFP_QCP_QUEUE_STS_LO			0x0008
#define NFP_QCP_QUEUE_STS_LO_READPTR_mask	0x3ffff
#define NFP_QCP_QUEUE_STS_HI			0x000c
#define NFP_QCP_QUEUE_STS_HI_WRITEPTR_mask	0x3ffff

/* The offset of a QCP queues in the PCIe Target */
#define NFP_PCIE_QUEUE(_q) (0x80000 + (NFP_QCP_QUEUE_ADDR_SZ * ((_q) & 0xff)))

/* nfp_qcp_ptr - Read or Write Pointer of a queue */
enum nfp_qcp_ptr {
	NFP_QCP_READ_PTR = 0,
	NFP_QCP_WRITE_PTR
};

/* There appear to be an *undocumented* upper limit on the value which
 * one can add to a queue and that value is either 0x3f or 0x7f.  We
 * go with 0x3f as a conservative measure.
 */
#define NFP_QCP_MAX_ADD				0x3f

static inline void _nfp_qcp_ptr_add(u8 __iomem *q,
				    enum nfp_qcp_ptr ptr, u32 val)
{
	u32 off;

	if (ptr == NFP_QCP_READ_PTR)
		off = NFP_QCP_QUEUE_ADD_RPTR;
	else
		off = NFP_QCP_QUEUE_ADD_WPTR;

	while (val > NFP_QCP_MAX_ADD) {
		writel(NFP_QCP_MAX_ADD, q + off);
		val -= NFP_QCP_MAX_ADD;
	}

	writel(val, q + off);
}

/**
 * nfp_qcp_rd_ptr_add() - Add the value to the read pointer of a queue
 *
 * @q:   Base address for queue structure
 * @val: Value to add to the queue pointer
 *
 * If @val is greater than @NFP_QCP_MAX_ADD multiple writes are performed.
 */
static inline void nfp_qcp_rd_ptr_add(u8 __iomem *q, u32 val)
{
	_nfp_qcp_ptr_add(q, NFP_QCP_READ_PTR, val);
}

/**
 * nfp_qcp_wr_ptr_add() - Add the value to the write pointer of a queue
 *
 * @q:   Base address for queue structure
 * @val: Value to add to the queue pointer
 *
 * If @val is greater than @NFP_QCP_MAX_ADD multiple writes are performed.
 */
static inline void nfp_qcp_wr_ptr_add(u8 __iomem *q, u32 val)
{
	_nfp_qcp_ptr_add(q, NFP_QCP_WRITE_PTR, val);
}

static inline u32 _nfp_qcp_read(u8 __iomem *q, enum nfp_qcp_ptr ptr)
{
	u32 off;
	u32 val;

	if (ptr == NFP_QCP_READ_PTR)
		off = NFP_QCP_QUEUE_STS_LO;
	else
		off = NFP_QCP_QUEUE_STS_HI;

	val = readl(q + off);

	if (ptr == NFP_QCP_READ_PTR)
		return val & NFP_QCP_QUEUE_STS_LO_READPTR_mask;
	else
		return val & NFP_QCP_QUEUE_STS_HI_WRITEPTR_mask;
}

/**
 * nfp_qcp_rd_ptr_read() - Read the current read pointer value for a queue
 * @q:  Base address for queue structure
 *
 * Return: Value read.
 */
static inline u32 nfp_qcp_rd_ptr_read(u8 __iomem *q)
{
	return _nfp_qcp_read(q, NFP_QCP_READ_PTR);
}

/**
 * nfp_qcp_wr_ptr_read() - Read the current write pointer value for a queue
 * @q:  Base address for queue structure
 *
 * Return: Value read.
 */
static inline u32 nfp_qcp_wr_ptr_read(u8 __iomem *q)
{
	return _nfp_qcp_read(q, NFP_QCP_WRITE_PTR);
}

static inline bool nfp_net_is_data_vnic(struct nfp_net *nn)
{
	WARN_ON_ONCE(!nn->dp.netdev && nn->port);
	return !!nn->dp.netdev;
}

static inline bool nfp_net_running(struct nfp_net *nn)
{
	return nn->dp.ctrl & NFP_NET_CFG_CTRL_ENABLE;
}

/* Globals */
extern const char nfp_driver_version[];

extern const struct net_device_ops nfp_net_netdev_ops;

static inline bool nfp_netdev_is_nfp_net(struct net_device *netdev)
{
	return netdev->netdev_ops == &nfp_net_netdev_ops;
}

/* Prototypes */
void nfp_net_get_fw_version(struct nfp_net_fw_version *fw_ver,
			    void __iomem *ctrl_bar);

struct nfp_net *
nfp_net_alloc(struct pci_dev *pdev, bool needs_netdev,
	      unsigned int max_tx_rings, unsigned int max_rx_rings);
void nfp_net_free(struct nfp_net *nn);

int nfp_net_init(struct nfp_net *nn);
void nfp_net_clean(struct nfp_net *nn);

void nfp_net_set_ethtool_ops(struct net_device *netdev);
void nfp_net_info(struct nfp_net *nn);
int nfp_net_reconfig(struct nfp_net *nn, u32 update);
unsigned int nfp_net_rss_key_sz(struct nfp_net *nn);
void nfp_net_rss_write_itbl(struct nfp_net *nn);
void nfp_net_rss_write_key(struct nfp_net *nn);
void nfp_net_coalesce_write_cfg(struct nfp_net *nn);

unsigned int
nfp_net_irqs_alloc(struct pci_dev *pdev, struct msix_entry *irq_entries,
		   unsigned int min_irqs, unsigned int want_irqs);
void nfp_net_irqs_disable(struct pci_dev *pdev);
void
nfp_net_irqs_assign(struct nfp_net *nn, struct msix_entry *irq_entries,
		    unsigned int n);

struct nfp_net_dp *nfp_net_clone_dp(struct nfp_net *nn);
int nfp_net_ring_reconfig(struct nfp_net *nn, struct nfp_net_dp *new,
			  struct netlink_ext_ack *extack);

#ifdef CONFIG_NFP_DEBUG
void nfp_net_debugfs_create(void);
void nfp_net_debugfs_destroy(void);
struct dentry *nfp_net_debugfs_device_add(struct pci_dev *pdev);
void nfp_net_debugfs_vnic_add(struct nfp_net *nn, struct dentry *ddir, int id);
void nfp_net_debugfs_dir_clean(struct dentry **dir);
#else
static inline void nfp_net_debugfs_create(void)
{
}

static inline void nfp_net_debugfs_destroy(void)
{
}

static inline struct dentry *nfp_net_debugfs_device_add(struct pci_dev *pdev)
{
	return NULL;
}

static inline void
nfp_net_debugfs_vnic_add(struct nfp_net *nn, struct dentry *ddir, int id)
{
}

static inline void nfp_net_debugfs_dir_clean(struct dentry **dir)
{
}
#endif /* CONFIG_NFP_DEBUG */

#endif /* _NFP_NET_H_ */
