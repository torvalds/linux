/*
 * AMD 10Gb Ethernet driver
 *
 * This file is available to you under your choice of the following two
 * licenses:
 *
 * License 1: GPLv2
 *
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
 *
 * This file is free software; you may copy, redistribute and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 2 of the License, or (at
 * your option) any later version.
 *
 * This file is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 *
 *
 * License 2: Modified BSD
 *
 * Copyright (c) 2014 Advanced Micro Devices, Inc.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *     * Redistributions of source code must retain the above copyright
 *       notice, this list of conditions and the following disclaimer.
 *     * Redistributions in binary form must reproduce the above copyright
 *       notice, this list of conditions and the following disclaimer in the
 *       documentation and/or other materials provided with the distribution.
 *     * Neither the name of Advanced Micro Devices, Inc. nor the
 *       names of its contributors may be used to endorse or promote products
 *       derived from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL <COPYRIGHT HOLDER> BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
 * LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
 * ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 * This file incorporates work covered by the following copyright and
 * permission notice:
 *     The Synopsys DWC ETHER XGMAC Software Driver and documentation
 *     (hereinafter "Software") is an unsupported proprietary work of Synopsys,
 *     Inc. unless otherwise expressly agreed to in writing between Synopsys
 *     and you.
 *
 *     The Software IS NOT an item of Licensed Software or Licensed Product
 *     under any End User Software License Agreement or Agreement for Licensed
 *     Product with Synopsys or any supplement thereto.  Permission is hereby
 *     granted, free of charge, to any person obtaining a copy of this software
 *     annotated with this license and the Software, to deal in the Software
 *     without restriction, including without limitation the rights to use,
 *     copy, modify, merge, publish, distribute, sublicense, and/or sell copies
 *     of the Software, and to permit persons to whom the Software is furnished
 *     to do so, subject to the following conditions:
 *
 *     The above copyright notice and this permission notice shall be included
 *     in all copies or substantial portions of the Software.
 *
 *     THIS SOFTWARE IS BEING DISTRIBUTED BY SYNOPSYS SOLELY ON AN "AS IS"
 *     BASIS AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED
 *     TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A
 *     PARTICULAR PURPOSE ARE HEREBY DISCLAIMED. IN NO EVENT SHALL SYNOPSYS
 *     BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 *     CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 *     SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 *     INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 *     CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 *     ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 *     THE POSSIBILITY OF SUCH DAMAGE.
 */

#ifndef __XGBE_H__
#define __XGBE_H__

#include <linux/dma-mapping.h>
#include <linux/netdevice.h>
#include <linux/workqueue.h>
#include <linux/phy.h>
#include <linux/if_vlan.h>
#include <linux/bitops.h>
#include <linux/ptp_clock_kernel.h>
#include <linux/timecounter.h>
#include <linux/net_tstamp.h>
#include <net/dcbnl.h>

#define XGBE_DRV_NAME		"amd-xgbe"
#define XGBE_DRV_VERSION	"1.0.0-a"
#define XGBE_DRV_DESC		"AMD 10 Gigabit Ethernet Driver"

/* Descriptor related defines */
#define XGBE_TX_DESC_CNT	512
#define XGBE_TX_DESC_MIN_FREE	(XGBE_TX_DESC_CNT >> 3)
#define XGBE_TX_DESC_MAX_PROC	(XGBE_TX_DESC_CNT >> 1)
#define XGBE_RX_DESC_CNT	512

#define XGBE_TX_MAX_BUF_SIZE	(0x3fff & ~(64 - 1))

/* Descriptors required for maximum contigous TSO/GSO packet */
#define XGBE_TX_MAX_SPLIT	((GSO_MAX_SIZE / XGBE_TX_MAX_BUF_SIZE) + 1)

/* Maximum possible descriptors needed for an SKB:
 * - Maximum number of SKB frags
 * - Maximum descriptors for contiguous TSO/GSO packet
 * - Possible context descriptor
 * - Possible TSO header descriptor
 */
#define XGBE_TX_MAX_DESCS	(MAX_SKB_FRAGS + XGBE_TX_MAX_SPLIT + 2)

#define XGBE_RX_MIN_BUF_SIZE	(ETH_FRAME_LEN + ETH_FCS_LEN + VLAN_HLEN)
#define XGBE_RX_BUF_ALIGN	64
#define XGBE_SKB_ALLOC_SIZE	256
#define XGBE_SPH_HDSMS_SIZE	2	/* Keep in sync with SKB_ALLOC_SIZE */

#define XGBE_MAX_DMA_CHANNELS	16
#define XGBE_MAX_QUEUES		16
#define XGBE_DMA_STOP_TIMEOUT	5

/* DMA cache settings - Outer sharable, write-back, write-allocate */
#define XGBE_DMA_OS_AXDOMAIN	0x2
#define XGBE_DMA_OS_ARCACHE	0xb
#define XGBE_DMA_OS_AWCACHE	0xf

/* DMA cache settings - System, no caches used */
#define XGBE_DMA_SYS_AXDOMAIN	0x3
#define XGBE_DMA_SYS_ARCACHE	0x0
#define XGBE_DMA_SYS_AWCACHE	0x0

#define XGBE_DMA_INTERRUPT_MASK	0x31c7

#define XGMAC_MIN_PACKET	60
#define XGMAC_STD_PACKET_MTU	1500
#define XGMAC_MAX_STD_PACKET	1518
#define XGMAC_JUMBO_PACKET_MTU	9000
#define XGMAC_MAX_JUMBO_PACKET	9018

/* MDIO bus phy name */
#define XGBE_PHY_NAME		"amd_xgbe_phy"
#define XGBE_PRTAD		0

/* Common property names */
#define XGBE_MAC_ADDR_PROPERTY	"mac-address"
#define XGBE_PHY_MODE_PROPERTY	"phy-mode"
#define XGBE_DMA_IRQS_PROPERTY	"amd,per-channel-interrupt"

/* Device-tree clock names */
#define XGBE_DMA_CLOCK		"dma_clk"
#define XGBE_PTP_CLOCK		"ptp_clk"

/* ACPI property names */
#define XGBE_ACPI_DMA_FREQ	"amd,dma-freq"
#define XGBE_ACPI_PTP_FREQ	"amd,ptp-freq"

/* Timestamp support - values based on 50MHz PTP clock
 *   50MHz => 20 nsec
 */
#define XGBE_TSTAMP_SSINC	20
#define XGBE_TSTAMP_SNSINC	0

/* Driver PMT macros */
#define XGMAC_DRIVER_CONTEXT	1
#define XGMAC_IOCTL_CONTEXT	2

#define XGBE_FIFO_MAX		81920
#define XGBE_FIFO_SIZE_B(x)	(x)
#define XGBE_FIFO_SIZE_KB(x)	(x * 1024)

#define XGBE_TC_MIN_QUANTUM	10

/* Helper macro for descriptor handling
 *  Always use XGBE_GET_DESC_DATA to access the descriptor data
 *  since the index is free-running and needs to be and-ed
 *  with the descriptor count value of the ring to index to
 *  the proper descriptor data.
 */
#define XGBE_GET_DESC_DATA(_ring, _idx)				\
	((_ring)->rdata +					\
	 ((_idx) & ((_ring)->rdesc_count - 1)))

/* Default coalescing parameters */
#define XGMAC_INIT_DMA_TX_USECS		1000
#define XGMAC_INIT_DMA_TX_FRAMES	25

#define XGMAC_MAX_DMA_RIWT		0xff
#define XGMAC_INIT_DMA_RX_USECS		30
#define XGMAC_INIT_DMA_RX_FRAMES	25

/* Flow control queue count */
#define XGMAC_MAX_FLOW_CONTROL_QUEUES	8

/* Maximum MAC address hash table size (256 bits = 8 bytes) */
#define XGBE_MAC_HASH_TABLE_SIZE	8

/* Receive Side Scaling */
#define XGBE_RSS_HASH_KEY_SIZE		40
#define XGBE_RSS_MAX_TABLE_SIZE		256
#define XGBE_RSS_LOOKUP_TABLE_TYPE	0
#define XGBE_RSS_HASH_KEY_TYPE		1

struct xgbe_prv_data;

struct xgbe_packet_data {
	struct sk_buff *skb;

	unsigned int attributes;

	unsigned int errors;

	unsigned int rdesc_count;
	unsigned int length;

	unsigned int header_len;
	unsigned int tcp_header_len;
	unsigned int tcp_payload_len;
	unsigned short mss;

	unsigned short vlan_ctag;

	u64 rx_tstamp;

	u32 rss_hash;
	enum pkt_hash_types rss_hash_type;

	unsigned int tx_packets;
	unsigned int tx_bytes;
};

/* Common Rx and Tx descriptor mapping */
struct xgbe_ring_desc {
	__le32 desc0;
	__le32 desc1;
	__le32 desc2;
	__le32 desc3;
};

/* Page allocation related values */
struct xgbe_page_alloc {
	struct page *pages;
	unsigned int pages_len;
	unsigned int pages_offset;

	dma_addr_t pages_dma;
};

/* Ring entry buffer data */
struct xgbe_buffer_data {
	struct xgbe_page_alloc pa;
	struct xgbe_page_alloc pa_unmap;

	dma_addr_t dma;
	unsigned int dma_len;
};

/* Tx-related ring data */
struct xgbe_tx_ring_data {
	unsigned int packets;		/* BQL packet count */
	unsigned int bytes;		/* BQL byte count */
};

/* Rx-related ring data */
struct xgbe_rx_ring_data {
	struct xgbe_buffer_data hdr;	/* Header locations */
	struct xgbe_buffer_data buf;	/* Payload locations */

	unsigned short hdr_len;		/* Length of received header */
	unsigned short len;		/* Length of received packet */
};

/* Structure used to hold information related to the descriptor
 * and the packet associated with the descriptor (always use
 * use the XGBE_GET_DESC_DATA macro to access this data from the ring)
 */
struct xgbe_ring_data {
	struct xgbe_ring_desc *rdesc;	/* Virtual address of descriptor */
	dma_addr_t rdesc_dma;		/* DMA address of descriptor */

	struct sk_buff *skb;		/* Virtual address of SKB */
	dma_addr_t skb_dma;		/* DMA address of SKB data */
	unsigned int skb_dma_len;	/* Length of SKB DMA area */

	struct xgbe_tx_ring_data tx;	/* Tx-related data */
	struct xgbe_rx_ring_data rx;	/* Rx-related data */

	unsigned int mapped_as_page;

	/* Incomplete receive save location.  If the budget is exhausted
	 * or the last descriptor (last normal descriptor or a following
	 * context descriptor) has not been DMA'd yet the current state
	 * of the receive processing needs to be saved.
	 */
	unsigned int state_saved;
	struct {
		unsigned int incomplete;
		unsigned int context_next;
		struct sk_buff *skb;
		unsigned int len;
		unsigned int error;
	} state;
};

struct xgbe_ring {
	/* Ring lock - used just for TX rings at the moment */
	spinlock_t lock;

	/* Per packet related information */
	struct xgbe_packet_data packet_data;

	/* Virtual/DMA addresses and count of allocated descriptor memory */
	struct xgbe_ring_desc *rdesc;
	dma_addr_t rdesc_dma;
	unsigned int rdesc_count;

	/* Array of descriptor data corresponding the descriptor memory
	 * (always use the XGBE_GET_DESC_DATA macro to access this data)
	 */
	struct xgbe_ring_data *rdata;

	/* Page allocation for RX buffers */
	struct xgbe_page_alloc rx_hdr_pa;
	struct xgbe_page_alloc rx_buf_pa;

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
			unsigned int queue_stopped;
			unsigned int xmit_more;
			unsigned short cur_mss;
			unsigned short cur_vlan_ctag;
		} tx;
	};
} ____cacheline_aligned;

/* Structure used to describe the descriptor rings associated with
 * a DMA channel.
 */
struct xgbe_channel {
	char name[16];

	/* Address of private data area for device */
	struct xgbe_prv_data *pdata;

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

	struct xgbe_ring *tx_ring;
	struct xgbe_ring *rx_ring;
} ____cacheline_aligned;

enum xgbe_int {
	XGMAC_INT_DMA_CH_SR_TI,
	XGMAC_INT_DMA_CH_SR_TPS,
	XGMAC_INT_DMA_CH_SR_TBU,
	XGMAC_INT_DMA_CH_SR_RI,
	XGMAC_INT_DMA_CH_SR_RBU,
	XGMAC_INT_DMA_CH_SR_RPS,
	XGMAC_INT_DMA_CH_SR_TI_RI,
	XGMAC_INT_DMA_CH_SR_FBE,
	XGMAC_INT_DMA_ALL,
};

enum xgbe_int_state {
	XGMAC_INT_STATE_SAVE,
	XGMAC_INT_STATE_RESTORE,
};

enum xgbe_mtl_fifo_size {
	XGMAC_MTL_FIFO_SIZE_256  = 0x00,
	XGMAC_MTL_FIFO_SIZE_512  = 0x01,
	XGMAC_MTL_FIFO_SIZE_1K   = 0x03,
	XGMAC_MTL_FIFO_SIZE_2K   = 0x07,
	XGMAC_MTL_FIFO_SIZE_4K   = 0x0f,
	XGMAC_MTL_FIFO_SIZE_8K   = 0x1f,
	XGMAC_MTL_FIFO_SIZE_16K  = 0x3f,
	XGMAC_MTL_FIFO_SIZE_32K  = 0x7f,
	XGMAC_MTL_FIFO_SIZE_64K  = 0xff,
	XGMAC_MTL_FIFO_SIZE_128K = 0x1ff,
	XGMAC_MTL_FIFO_SIZE_256K = 0x3ff,
};

struct xgbe_mmc_stats {
	/* Tx Stats */
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

	/* Rx Stats */
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
};

struct xgbe_hw_if {
	int (*tx_complete)(struct xgbe_ring_desc *);

	int (*set_mac_address)(struct xgbe_prv_data *, u8 *addr);
	int (*config_rx_mode)(struct xgbe_prv_data *);

	int (*enable_rx_csum)(struct xgbe_prv_data *);
	int (*disable_rx_csum)(struct xgbe_prv_data *);

	int (*enable_rx_vlan_stripping)(struct xgbe_prv_data *);
	int (*disable_rx_vlan_stripping)(struct xgbe_prv_data *);
	int (*enable_rx_vlan_filtering)(struct xgbe_prv_data *);
	int (*disable_rx_vlan_filtering)(struct xgbe_prv_data *);
	int (*update_vlan_hash_table)(struct xgbe_prv_data *);

	int (*read_mmd_regs)(struct xgbe_prv_data *, int, int);
	void (*write_mmd_regs)(struct xgbe_prv_data *, int, int, int);
	int (*set_gmii_speed)(struct xgbe_prv_data *);
	int (*set_gmii_2500_speed)(struct xgbe_prv_data *);
	int (*set_xgmii_speed)(struct xgbe_prv_data *);

	void (*enable_tx)(struct xgbe_prv_data *);
	void (*disable_tx)(struct xgbe_prv_data *);
	void (*enable_rx)(struct xgbe_prv_data *);
	void (*disable_rx)(struct xgbe_prv_data *);

	void (*powerup_tx)(struct xgbe_prv_data *);
	void (*powerdown_tx)(struct xgbe_prv_data *);
	void (*powerup_rx)(struct xgbe_prv_data *);
	void (*powerdown_rx)(struct xgbe_prv_data *);

	int (*init)(struct xgbe_prv_data *);
	int (*exit)(struct xgbe_prv_data *);

	int (*enable_int)(struct xgbe_channel *, enum xgbe_int);
	int (*disable_int)(struct xgbe_channel *, enum xgbe_int);
	void (*dev_xmit)(struct xgbe_channel *);
	int (*dev_read)(struct xgbe_channel *);
	void (*tx_desc_init)(struct xgbe_channel *);
	void (*rx_desc_init)(struct xgbe_channel *);
	void (*tx_desc_reset)(struct xgbe_ring_data *);
	void (*rx_desc_reset)(struct xgbe_prv_data *, struct xgbe_ring_data *,
			      unsigned int);
	int (*is_last_desc)(struct xgbe_ring_desc *);
	int (*is_context_desc)(struct xgbe_ring_desc *);
	void (*tx_start_xmit)(struct xgbe_channel *, struct xgbe_ring *);

	/* For FLOW ctrl */
	int (*config_tx_flow_control)(struct xgbe_prv_data *);
	int (*config_rx_flow_control)(struct xgbe_prv_data *);

	/* For RX coalescing */
	int (*config_rx_coalesce)(struct xgbe_prv_data *);
	int (*config_tx_coalesce)(struct xgbe_prv_data *);
	unsigned int (*usec_to_riwt)(struct xgbe_prv_data *, unsigned int);
	unsigned int (*riwt_to_usec)(struct xgbe_prv_data *, unsigned int);

	/* For RX and TX threshold config */
	int (*config_rx_threshold)(struct xgbe_prv_data *, unsigned int);
	int (*config_tx_threshold)(struct xgbe_prv_data *, unsigned int);

	/* For RX and TX Store and Forward Mode config */
	int (*config_rsf_mode)(struct xgbe_prv_data *, unsigned int);
	int (*config_tsf_mode)(struct xgbe_prv_data *, unsigned int);

	/* For TX DMA Operate on Second Frame config */
	int (*config_osp_mode)(struct xgbe_prv_data *);

	/* For RX and TX PBL config */
	int (*config_rx_pbl_val)(struct xgbe_prv_data *);
	int (*get_rx_pbl_val)(struct xgbe_prv_data *);
	int (*config_tx_pbl_val)(struct xgbe_prv_data *);
	int (*get_tx_pbl_val)(struct xgbe_prv_data *);
	int (*config_pblx8)(struct xgbe_prv_data *);

	/* For MMC statistics */
	void (*rx_mmc_int)(struct xgbe_prv_data *);
	void (*tx_mmc_int)(struct xgbe_prv_data *);
	void (*read_mmc_stats)(struct xgbe_prv_data *);

	/* For Timestamp config */
	int (*config_tstamp)(struct xgbe_prv_data *, unsigned int);
	void (*update_tstamp_addend)(struct xgbe_prv_data *, unsigned int);
	void (*set_tstamp_time)(struct xgbe_prv_data *, unsigned int sec,
				unsigned int nsec);
	u64 (*get_tstamp_time)(struct xgbe_prv_data *);
	u64 (*get_tx_tstamp)(struct xgbe_prv_data *);

	/* For Data Center Bridging config */
	void (*config_dcb_tc)(struct xgbe_prv_data *);
	void (*config_dcb_pfc)(struct xgbe_prv_data *);

	/* For Receive Side Scaling */
	int (*enable_rss)(struct xgbe_prv_data *);
	int (*disable_rss)(struct xgbe_prv_data *);
	int (*set_rss_hash_key)(struct xgbe_prv_data *, const u8 *);
	int (*set_rss_lookup_table)(struct xgbe_prv_data *, const u32 *);
};

struct xgbe_desc_if {
	int (*alloc_ring_resources)(struct xgbe_prv_data *);
	void (*free_ring_resources)(struct xgbe_prv_data *);
	int (*map_tx_skb)(struct xgbe_channel *, struct sk_buff *);
	int (*map_rx_buffer)(struct xgbe_prv_data *, struct xgbe_ring *,
			     struct xgbe_ring_data *);
	void (*unmap_rdata)(struct xgbe_prv_data *, struct xgbe_ring_data *);
	void (*wrapper_tx_desc_init)(struct xgbe_prv_data *);
	void (*wrapper_rx_desc_init)(struct xgbe_prv_data *);
};

/* This structure contains flags that indicate what hardware features
 * or configurations are present in the device.
 */
struct xgbe_hw_features {
	/* HW Version */
	unsigned int version;

	/* HW Feature Register0 */
	unsigned int gmii;		/* 1000 Mbps support */
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

struct xgbe_prv_data {
	struct net_device *netdev;
	struct platform_device *pdev;
	struct acpi_device *adev;
	struct device *dev;

	/* ACPI or DT flag */
	unsigned int use_acpi;

	/* XGMAC/XPCS related mmio registers */
	void __iomem *xgmac_regs;	/* XGMAC CSRs */
	void __iomem *xpcs_regs;	/* XPCS MMD registers */

	/* Overall device lock */
	spinlock_t lock;

	/* XPCS indirect addressing mutex */
	struct mutex xpcs_mutex;

	/* RSS addressing mutex */
	struct mutex rss_mutex;

	int dev_irq;
	unsigned int per_channel_irq;

	struct xgbe_hw_if hw_if;
	struct xgbe_desc_if desc_if;

	/* AXI DMA settings */
	unsigned int coherent;
	unsigned int axdomain;
	unsigned int arcache;
	unsigned int awcache;

	/* Rings for Tx/Rx on a DMA channel */
	struct xgbe_channel *channel;
	unsigned int channel_count;
	unsigned int tx_ring_count;
	unsigned int tx_desc_count;
	unsigned int rx_ring_count;
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
	unsigned int pause_autoneg;
	unsigned int tx_pause;
	unsigned int rx_pause;

	/* Receive Side Scaling settings */
	u8 rss_key[XGBE_RSS_HASH_KEY_SIZE];
	u32 rss_table[XGBE_RSS_MAX_TABLE_SIZE];
	u32 rss_options;

	/* MDIO settings */
	struct module *phy_module;
	char *mii_bus_id;
	struct mii_bus *mii;
	int mdio_mmd;
	struct phy_device *phydev;
	int default_autoneg;
	int default_speed;

	/* Current PHY settings */
	phy_interface_t phy_mode;
	int phy_link;
	int phy_speed;
	unsigned int phy_tx_pause;
	unsigned int phy_rx_pause;

	/* Netdev related settings */
	unsigned char mac_addr[ETH_ALEN];
	netdev_features_t netdev_features;
	struct napi_struct napi;
	struct xgbe_mmc_stats mmc_stats;

	/* Filtering support */
	unsigned long active_vlans[BITS_TO_LONGS(VLAN_N_VID)];

	/* Device clocks */
	struct clk *sysclk;
	unsigned long sysclk_rate;
	struct clk *ptpclk;
	unsigned long ptpclk_rate;

	/* Timestamp support */
	spinlock_t tstamp_lock;
	struct ptp_clock_info ptp_clock_info;
	struct ptp_clock *ptp_clock;
	struct hwtstamp_config tstamp_config;
	struct cyclecounter tstamp_cc;
	struct timecounter tstamp_tc;
	unsigned int tstamp_addend;
	struct work_struct tx_tstamp_work;
	struct sk_buff *tx_tstamp_skb;
	u64 tx_tstamp;

	/* DCB support */
	struct ieee_ets *ets;
	struct ieee_pfc *pfc;
	unsigned int q2tc_map[XGBE_MAX_QUEUES];
	unsigned int prio2q_map[IEEE_8021QAZ_MAX_TCS];

	/* Hardware features of the device */
	struct xgbe_hw_features hw_feat;

	/* Device restart work structure */
	struct work_struct restart_work;

	/* Keeps track of power mode */
	unsigned int power_down;

#ifdef CONFIG_DEBUG_FS
	struct dentry *xgbe_debugfs;

	unsigned int debugfs_xgmac_reg;

	unsigned int debugfs_xpcs_mmd;
	unsigned int debugfs_xpcs_reg;
#endif
};

/* Function prototypes*/

void xgbe_init_function_ptrs_dev(struct xgbe_hw_if *);
void xgbe_init_function_ptrs_desc(struct xgbe_desc_if *);
struct net_device_ops *xgbe_get_netdev_ops(void);
struct ethtool_ops *xgbe_get_ethtool_ops(void);
#ifdef CONFIG_AMD_XGBE_DCB
const struct dcbnl_rtnl_ops *xgbe_get_dcbnl_ops(void);
#endif

int xgbe_mdio_register(struct xgbe_prv_data *);
void xgbe_mdio_unregister(struct xgbe_prv_data *);
void xgbe_dump_phy_registers(struct xgbe_prv_data *);
void xgbe_ptp_register(struct xgbe_prv_data *);
void xgbe_ptp_unregister(struct xgbe_prv_data *);
void xgbe_dump_tx_desc(struct xgbe_ring *, unsigned int, unsigned int,
		       unsigned int);
void xgbe_dump_rx_desc(struct xgbe_ring *, struct xgbe_ring_desc *,
		       unsigned int);
void xgbe_print_pkt(struct net_device *, struct sk_buff *, bool);
void xgbe_get_all_hw_features(struct xgbe_prv_data *);
int xgbe_powerup(struct net_device *, unsigned int);
int xgbe_powerdown(struct net_device *, unsigned int);
void xgbe_init_rx_coalesce(struct xgbe_prv_data *);
void xgbe_init_tx_coalesce(struct xgbe_prv_data *);

#ifdef CONFIG_DEBUG_FS
void xgbe_debugfs_init(struct xgbe_prv_data *);
void xgbe_debugfs_exit(struct xgbe_prv_data *);
#else
static inline void xgbe_debugfs_init(struct xgbe_prv_data *pdata) {}
static inline void xgbe_debugfs_exit(struct xgbe_prv_data *pdata) {}
#endif /* CONFIG_DEBUG_FS */

/* NOTE: Uncomment for TX and RX DESCRIPTOR DUMP in KERNEL LOG */
#if 0
#define XGMAC_ENABLE_TX_DESC_DUMP
#define XGMAC_ENABLE_RX_DESC_DUMP
#endif

/* NOTE: Uncomment for TX and RX PACKET DUMP in KERNEL LOG */
#if 0
#define XGMAC_ENABLE_TX_PKT_DUMP
#define XGMAC_ENABLE_RX_PKT_DUMP
#endif

/* NOTE: Uncomment for function trace log messages in KERNEL LOG */
#if 0
#define YDEBUG
#define YDEBUG_MDIO
#endif

/* For debug prints */
#ifdef YDEBUG
#define DBGPR(x...) pr_alert(x)
#define DBGPHY_REGS(x...) xgbe_dump_phy_registers(x)
#else
#define DBGPR(x...) do { } while (0)
#define DBGPHY_REGS(x...) do { } while (0)
#endif

#ifdef YDEBUG_MDIO
#define DBGPR_MDIO(x...) pr_alert(x)
#else
#define DBGPR_MDIO(x...) do { } while (0)
#endif

#endif
