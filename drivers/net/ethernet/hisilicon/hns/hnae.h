/*
 * Copyright (c) 2014-2015 Hisilicon Limited.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#ifndef __HNAE_H
#define __HNAE_H

/* Names used in this framework:
 *      ae handle (handle):
 *        a set of queues provided by AE
 *      ring buffer queue (rbq):
 *        the channel between upper layer and the AE, can do tx and rx
 *      ring:
 *        a tx or rx channel within a rbq
 *      ring description (desc):
 *        an element in the ring with packet information
 *      buffer:
 *        a memory region referred by desc with the full packet payload
 *
 * "num" means a static number set as a parameter, "count" mean a dynamic
 *   number set while running
 * "cb" means control block
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/module.h>
#include <linux/netdevice.h>
#include <linux/notifier.h>
#include <linux/phy.h>
#include <linux/types.h>

#define HNAE_DRIVER_VERSION "2.0"
#define HNAE_DRIVER_NAME "hns"
#define HNAE_COPYRIGHT "Copyright(c) 2015 Huawei Corporation."
#define HNAE_DRIVER_STRING "Hisilicon Network Subsystem Driver"
#define HNAE_DEFAULT_DEVICE_DESCR "Hisilicon Network Subsystem"

#ifdef DEBUG

#ifndef assert
#define assert(expr) \
do { \
	if (!(expr)) { \
		pr_err("Assertion failed! %s, %s, %s, line %d\n", \
			   #expr, __FILE__, __func__, __LINE__); \
	} \
} while (0)
#endif

#else

#ifndef assert
#define assert(expr)
#endif

#endif

#define AE_VERSION_1 ('6' << 16 | '6' << 8 | '0')
#define AE_VERSION_2 ('1' << 24 | '6' << 16 | '1' << 8 | '0')
#define AE_IS_VER1(ver) ((ver) == AE_VERSION_1)
#define AE_NAME_SIZE 16

/* some said the RX and TX RCB format should not be the same in the future. But
 * it is the same now...
 */
#define RCB_REG_BASEADDR_L         0x00 /* P660 support only 32bit accessing */
#define RCB_REG_BASEADDR_H         0x04
#define RCB_REG_BD_NUM             0x08
#define RCB_REG_BD_LEN             0x0C
#define RCB_REG_PKTLINE            0x10
#define RCB_REG_TAIL               0x18
#define RCB_REG_HEAD               0x1C
#define RCB_REG_FBDNUM             0x20
#define RCB_REG_OFFSET             0x24 /* pkt num to be handled */
#define RCB_REG_PKTNUM_RECORD      0x2C /* total pkt received */

#define HNS_RX_HEAD_SIZE 256

#define HNAE_AE_REGISTER 0x1

#define RCB_RING_NAME_LEN 16

enum hnae_led_state {
	HNAE_LED_INACTIVE,
	HNAE_LED_ACTIVE,
	HNAE_LED_ON,
	HNAE_LED_OFF
};

#define HNS_RX_FLAG_VLAN_PRESENT 0x1
#define HNS_RX_FLAG_L3ID_IPV4 0x0
#define HNS_RX_FLAG_L3ID_IPV6 0x1
#define HNS_RX_FLAG_L4ID_UDP 0x0
#define HNS_RX_FLAG_L4ID_TCP 0x1

#define HNS_TXD_ASID_S 0
#define HNS_TXD_ASID_M (0xff << HNS_TXD_ASID_S)
#define HNS_TXD_BUFNUM_S 8
#define HNS_TXD_BUFNUM_M (0x3 << HNS_TXD_BUFNUM_S)
#define HNS_TXD_PORTID_S 10
#define HNS_TXD_PORTID_M (0x7 << HNS_TXD_PORTID_S)

#define HNS_TXD_RA_B 8
#define HNS_TXD_RI_B 9
#define HNS_TXD_L4CS_B 10
#define HNS_TXD_L3CS_B 11
#define HNS_TXD_FE_B 12
#define HNS_TXD_VLD_B 13
#define HNS_TXD_IPOFFSET_S 14
#define HNS_TXD_IPOFFSET_M (0xff << HNS_TXD_IPOFFSET_S)

#define HNS_RXD_IPOFFSET_S 0
#define HNS_RXD_IPOFFSET_M (0xff << HNS_TXD_IPOFFSET_S)
#define HNS_RXD_BUFNUM_S 8
#define HNS_RXD_BUFNUM_M (0x3 << HNS_RXD_BUFNUM_S)
#define HNS_RXD_PORTID_S 10
#define HNS_RXD_PORTID_M (0x7 << HNS_RXD_PORTID_S)
#define HNS_RXD_DMAC_S 13
#define HNS_RXD_DMAC_M (0x3 << HNS_RXD_DMAC_S)
#define HNS_RXD_VLAN_S 15
#define HNS_RXD_VLAN_M (0x3 << HNS_RXD_VLAN_S)
#define HNS_RXD_L3ID_S 17
#define HNS_RXD_L3ID_M (0xf << HNS_RXD_L3ID_S)
#define HNS_RXD_L4ID_S 21
#define HNS_RXD_L4ID_M (0xf << HNS_RXD_L4ID_S)
#define HNS_RXD_FE_B 25
#define HNS_RXD_FRAG_B 26
#define HNS_RXD_VLD_B 27
#define HNS_RXD_L2E_B 28
#define HNS_RXD_L3E_B 29
#define HNS_RXD_L4E_B 30
#define HNS_RXD_DROP_B 31

#define HNS_RXD_VLANID_S 8
#define HNS_RXD_VLANID_M (0xfff << HNS_RXD_VLANID_S)
#define HNS_RXD_CFI_B 20
#define HNS_RXD_PRI_S 21
#define HNS_RXD_PRI_M (0x7 << HNS_RXD_PRI_S)
#define HNS_RXD_ASID_S 24
#define HNS_RXD_ASID_M (0xff << HNS_RXD_ASID_S)

#define HNSV2_TXD_BUFNUM_S 0
#define HNSV2_TXD_BUFNUM_M (0x7 << HNSV2_TXD_BUFNUM_S)
#define HNSV2_TXD_RI_B   1
#define HNSV2_TXD_L4CS_B   2
#define HNSV2_TXD_L3CS_B   3
#define HNSV2_TXD_FE_B   4
#define HNSV2_TXD_VLD_B  5

#define HNSV2_TXD_TSE_B   0
#define HNSV2_TXD_VLAN_EN_B   1
#define HNSV2_TXD_SNAP_B   2
#define HNSV2_TXD_IPV6_B   3
#define HNSV2_TXD_SCTP_B   4

/* hardware spec ring buffer format */
struct __packed hnae_desc {
	__le64 addr;
	union {
		struct {
			union {
				__le16 asid_bufnum_pid;
				__le16 asid;
			};
			__le16 send_size;
			union {
				__le32 flag_ipoffset;
				struct {
					__u8 bn_pid;
					__u8 ra_ri_cs_fe_vld;
					__u8 ip_offset;
					__u8 tse_vlan_snap_v6_sctp_nth;
				};
			};
			__le16 mss;
			__u8 l4_len;
			__u8 reserved1;
			__le16 paylen;
			__u8 vmid;
			__u8 qid;
			__le32 reserved2[2];
		} tx;

		struct {
			__le32 ipoff_bnum_pid_flag;
			__le16 pkt_len;
			__le16 size;
			union {
				__le32 vlan_pri_asid;
				struct {
					__le16 asid;
					__le16 vlan_cfi_pri;
				};
			};
			__le32 rss_hash;
			__le32 reserved_1[2];
		} rx;
	};
};

struct hnae_desc_cb {
	dma_addr_t dma; /* dma address of this desc */
	void *buf;      /* cpu addr for a desc */

	/* priv data for the desc, e.g. skb when use with ip stack*/
	void *priv;
	u16 page_offset;
	u16 reuse_flag;

	u16 length;     /* length of the buffer */

       /* desc type, used by the ring user to mark the type of the priv data */
	u16 type;
};

#define setflags(flags, bits) ((flags) |= (bits))
#define unsetflags(flags, bits) ((flags) &= ~(bits))

/* hnae_ring->flags fields */
#define RINGF_DIR 0x1	    /* TX or RX ring, set if TX */
#define is_tx_ring(ring) ((ring)->flags & RINGF_DIR)
#define is_rx_ring(ring) (!is_tx_ring(ring))
#define ring_to_dma_dir(ring) (is_tx_ring(ring) ? \
	DMA_TO_DEVICE : DMA_FROM_DEVICE)

struct ring_stats {
	u64 io_err_cnt;
	u64 sw_err_cnt;
	u64 seg_pkt_cnt;
	union {
		struct {
			u64 tx_pkts;
			u64 tx_bytes;
			u64 tx_err_cnt;
			u64 restart_queue;
			u64 tx_busy;
		};
		struct {
			u64 rx_pkts;
			u64 rx_bytes;
			u64 rx_err_cnt;
			u64 reuse_pg_cnt;
			u64 err_pkt_len;
			u64 non_vld_descs;
			u64 err_bd_num;
			u64 l2_err;
			u64 l3l4_csum_err;
		};
	};
};

struct hnae_queue;

struct hnae_ring {
	u8 __iomem *io_base; /* base io address for the ring */
	struct hnae_desc *desc; /* dma map address space */
	struct hnae_desc_cb *desc_cb;
	struct hnae_queue *q;
	int irq;
	char ring_name[RCB_RING_NAME_LEN];

	/* statistic */
	struct ring_stats stats;

	dma_addr_t desc_dma_addr;
	u32 buf_size;       /* size for hnae_desc->addr, preset by AE */
	u16 desc_num;       /* total number of desc */
	u16 max_desc_num_per_pkt;
	u16 max_raw_data_sz_per_desc;
	u16 max_pkt_size;
	int next_to_use;    /* idx of next spare desc */

	/* idx of lastest sent desc, the ring is empty when equal to
	 * next_to_use
	 */
	int next_to_clean;

	int flags;          /* ring attribute */
	int irq_init_flag;
};

#define ring_ptr_move_fw(ring, p) \
	((ring)->p = ((ring)->p + 1) % (ring)->desc_num)
#define ring_ptr_move_bw(ring, p) \
	((ring)->p = ((ring)->p - 1 + (ring)->desc_num) % (ring)->desc_num)

enum hns_desc_type {
	DESC_TYPE_SKB,
	DESC_TYPE_PAGE,
};

#define assert_is_ring_idx(ring, idx) \
	assert((idx) >= 0 && (idx) < (ring)->desc_num)

/* the distance between [begin, end) in a ring buffer
 * note: there is a unuse slot between the begin and the end
 */
static inline int ring_dist(struct hnae_ring *ring, int begin, int end)
{
	assert_is_ring_idx(ring, begin);
	assert_is_ring_idx(ring, end);

	return (end - begin + ring->desc_num) % ring->desc_num;
}

static inline int ring_space(struct hnae_ring *ring)
{
	return ring->desc_num -
		ring_dist(ring, ring->next_to_clean, ring->next_to_use) - 1;
}

static inline int is_ring_empty(struct hnae_ring *ring)
{
	assert_is_ring_idx(ring, ring->next_to_use);
	assert_is_ring_idx(ring, ring->next_to_clean);

	return ring->next_to_use == ring->next_to_clean;
}

#define hnae_buf_size(_ring) ((_ring)->buf_size)
#define hnae_page_order(_ring) (get_order(hnae_buf_size(_ring)))
#define hnae_page_size(_ring) (PAGE_SIZE << hnae_page_order(_ring))

struct hnae_handle;

/* allocate and dma map space for hnae desc */
struct hnae_buf_ops {
	int (*alloc_buffer)(struct hnae_ring *ring, struct hnae_desc_cb *cb);
	void (*free_buffer)(struct hnae_ring *ring, struct hnae_desc_cb *cb);
	int (*map_buffer)(struct hnae_ring *ring, struct hnae_desc_cb *cb);
	void (*unmap_buffer)(struct hnae_ring *ring, struct hnae_desc_cb *cb);
};

struct hnae_queue {
	void __iomem *io_base;
	phys_addr_t phy_base;
	struct hnae_ae_dev *dev;	/* the device who use this queue */
	struct hnae_ring rx_ring, tx_ring;
	struct hnae_handle *handle;
};

/*hnae loop mode*/
enum hnae_loop {
	MAC_INTERNALLOOP_MAC = 0,
	MAC_INTERNALLOOP_SERDES,
	MAC_INTERNALLOOP_PHY,
	MAC_LOOP_NONE,
};

/*hnae port type*/
enum hnae_port_type {
	HNAE_PORT_SERVICE = 0,
	HNAE_PORT_DEBUG
};

/* This struct defines the operation on the handle.
 *
 * get_handle(): (mandatory)
 *   Get a handle from AE according to its name and options.
 *   the AE driver should manage the space used by handle and its queues while
 *   the HNAE framework will allocate desc and desc_cb for all rings in the
 *   queues.
 * put_handle():
 *   Release the handle.
 * start():
 *   Enable the hardware, include all queues
 * stop():
 *   Disable the hardware
 * set_opts(): (mandatory)
 *   Set options to the AE
 * get_opts(): (mandatory)
 *   Get options from the AE
 * get_status():
 *   Get the carrier state of the back channel of the handle, 1 for ok, 0 for
 *   non-ok
 * toggle_ring_irq(): (mandatory)
 *   Set the ring irq to be enabled(0) or disable(1)
 * toggle_queue_status(): (mandatory)
 *   Set the queue to be enabled(1) or disable(0), this will not change the
 *   ring irq state
 * adjust_link()
 *   adjust link status
 * set_loopback()
 *   set loopback
 * get_ring_bdnum_limit()
 *   get ring bd number limit
 * get_pauseparam()
 *   get tx and rx of pause frame use
 * set_autoneg()
 *   set auto autonegotiation of pause frame use
 * get_autoneg()
 *   get auto autonegotiation of pause frame use
 * set_pauseparam()
 *   set tx and rx of pause frame use
 * get_coalesce_usecs()
 *   get usecs to delay a TX interrupt after a packet is sent
 * get_rx_max_coalesced_frames()
 *   get Maximum number of packets to be sent before a TX interrupt.
 * set_coalesce_usecs()
 *   set usecs to delay a TX interrupt after a packet is sent
 * set_coalesce_frames()
 *   set Maximum number of packets to be sent before a TX interrupt.
 * get_ringnum()
 *   get RX/TX ring number
 * get_max_ringnum()
 *   get RX/TX ring maximum number
 * get_mac_addr()
 *   get mac address
 * set_mac_addr()
 *   set mac address
 * set_mc_addr()
 *   set multicast mode
 * set_mtu()
 *   set mtu
 * update_stats()
 *   update Old network device statistics
 * get_ethtool_stats()
 *   get ethtool network device statistics
 * get_strings()
 *   get a set of strings that describe the requested objects
 * get_sset_count()
 *   get number of strings that @get_strings will write
 * update_led_status()
 *   update the led status
 * set_led_id()
 *   set led id
 * get_regs()
 *   get regs dump
 * get_regs_len()
 *   get the len of the regs dump
 */
struct hnae_ae_ops {
	struct hnae_handle *(*get_handle)(struct hnae_ae_dev *dev,
					  u32 port_id);
	void (*put_handle)(struct hnae_handle *handle);
	void (*init_queue)(struct hnae_queue *q);
	void (*fini_queue)(struct hnae_queue *q);
	int (*start)(struct hnae_handle *handle);
	void (*stop)(struct hnae_handle *handle);
	void (*reset)(struct hnae_handle *handle);
	int (*set_opts)(struct hnae_handle *handle, int type, void *opts);
	int (*get_opts)(struct hnae_handle *handle, int type, void **opts);
	int (*get_status)(struct hnae_handle *handle);
	int (*get_info)(struct hnae_handle *handle,
			u8 *auto_neg, u16 *speed, u8 *duplex);
	void (*toggle_ring_irq)(struct hnae_ring *ring, u32 val);
	void (*toggle_queue_status)(struct hnae_queue *queue, u32 val);
	void (*adjust_link)(struct hnae_handle *handle, int speed, int duplex);
	int (*set_loopback)(struct hnae_handle *handle,
			    enum hnae_loop loop_mode, int en);
	void (*get_ring_bdnum_limit)(struct hnae_queue *queue,
				     u32 *uplimit);
	void (*get_pauseparam)(struct hnae_handle *handle,
			       u32 *auto_neg, u32 *rx_en, u32 *tx_en);
	int (*set_autoneg)(struct hnae_handle *handle, u8 enable);
	int (*get_autoneg)(struct hnae_handle *handle);
	int (*set_pauseparam)(struct hnae_handle *handle,
			      u32 auto_neg, u32 rx_en, u32 tx_en);
	void (*get_coalesce_usecs)(struct hnae_handle *handle,
				   u32 *tx_usecs, u32 *rx_usecs);
	void (*get_rx_max_coalesced_frames)(struct hnae_handle *handle,
					    u32 *tx_frames, u32 *rx_frames);
	void (*set_coalesce_usecs)(struct hnae_handle *handle, u32 timeout);
	int (*set_coalesce_frames)(struct hnae_handle *handle,
				   u32 coalesce_frames);
	void (*set_promisc_mode)(struct hnae_handle *handle, u32 en);
	int (*get_mac_addr)(struct hnae_handle *handle, void **p);
	int (*set_mac_addr)(struct hnae_handle *handle, void *p);
	int (*set_mc_addr)(struct hnae_handle *handle, void *addr);
	int (*set_mtu)(struct hnae_handle *handle, int new_mtu);
	void (*update_stats)(struct hnae_handle *handle,
			     struct net_device_stats *net_stats);
	void (*get_stats)(struct hnae_handle *handle, u64 *data);
	void (*get_strings)(struct hnae_handle *handle,
			    u32 stringset, u8 *data);
	int (*get_sset_count)(struct hnae_handle *handle, int stringset);
	void (*update_led_status)(struct hnae_handle *handle);
	int (*set_led_id)(struct hnae_handle *handle,
			  enum hnae_led_state status);
	void (*get_regs)(struct hnae_handle *handle, void *data);
	int (*get_regs_len)(struct hnae_handle *handle);
	u32	(*get_rss_key_size)(struct hnae_handle *handle);
	u32	(*get_rss_indir_size)(struct hnae_handle *handle);
	int	(*get_rss)(struct hnae_handle *handle, u32 *indir, u8 *key,
			   u8 *hfunc);
	int	(*set_rss)(struct hnae_handle *handle, const u32 *indir,
			   const u8 *key, const u8 hfunc);
};

struct hnae_ae_dev {
	struct device cls_dev; /* the class dev */
	struct device *dev; /* the presented dev */
	struct hnae_ae_ops *ops;
	struct list_head node;
	struct module *owner; /* the module who provides this dev */
	int id;
	char name[AE_NAME_SIZE];
	struct list_head handle_list;
	spinlock_t lock; /* lock to protect the handle_list */
};

struct hnae_handle {
	struct device *owner_dev; /* the device which make use of this handle */
	struct hnae_ae_dev *dev;  /* the device who provides this handle */
	struct device_node *phy_node;
	phy_interface_t phy_if;
	u32 if_support;
	int q_num;
	int vf_id;
	u32 eport_id;
	enum hnae_port_type port_type;
	struct list_head node;    /* list to hnae_ae_dev->handle_list */
	struct hnae_buf_ops *bops; /* operation for the buffer */
	struct hnae_queue **qs;  /* array base of all queues */
};

#define ring_to_dev(ring) ((ring)->q->dev->dev)

struct hnae_handle *hnae_get_handle(struct device *owner_dev, const char *ae_id,
				    u32 port_id, struct hnae_buf_ops *bops);
void hnae_put_handle(struct hnae_handle *handle);
int hnae_ae_register(struct hnae_ae_dev *dev, struct module *owner);
void hnae_ae_unregister(struct hnae_ae_dev *dev);

int hnae_register_notifier(struct notifier_block *nb);
void hnae_unregister_notifier(struct notifier_block *nb);
int hnae_reinit_handle(struct hnae_handle *handle);

#define hnae_queue_xmit(q, buf_num) writel_relaxed(buf_num, \
	(q)->tx_ring.io_base + RCB_REG_TAIL)

#ifndef assert
#define assert(cond)
#endif

static inline int hnae_reserve_buffer_map(struct hnae_ring *ring,
					  struct hnae_desc_cb *cb)
{
	struct hnae_buf_ops *bops = ring->q->handle->bops;
	int ret;

	ret = bops->alloc_buffer(ring, cb);
	if (ret)
		goto out;

	ret = bops->map_buffer(ring, cb);
	if (ret)
		goto out_with_buf;

	return 0;

out_with_buf:
	bops->free_buffer(ring, cb);
out:
	return ret;
}

static inline int hnae_alloc_buffer_attach(struct hnae_ring *ring, int i)
{
	int ret = hnae_reserve_buffer_map(ring, &ring->desc_cb[i]);

	if (ret)
		return ret;

	ring->desc[i].addr = (__le64)ring->desc_cb[i].dma;

	return 0;
}

static inline void hnae_buffer_detach(struct hnae_ring *ring, int i)
{
	ring->q->handle->bops->unmap_buffer(ring, &ring->desc_cb[i]);
	ring->desc[i].addr = 0;
}

static inline void hnae_free_buffer_detach(struct hnae_ring *ring, int i)
{
	struct hnae_buf_ops *bops = ring->q->handle->bops;
	struct hnae_desc_cb *cb = &ring->desc_cb[i];

	if (!ring->desc_cb[i].dma)
		return;

	hnae_buffer_detach(ring, i);
	bops->free_buffer(ring, cb);
}

/* detach a in-used buffer and replace with a reserved one  */
static inline void hnae_replace_buffer(struct hnae_ring *ring, int i,
				       struct hnae_desc_cb *res_cb)
{
	struct hnae_buf_ops *bops = ring->q->handle->bops;
	struct hnae_desc_cb tmp_cb = ring->desc_cb[i];

	bops->unmap_buffer(ring, &ring->desc_cb[i]);
	ring->desc_cb[i] = *res_cb;
	*res_cb = tmp_cb;
	ring->desc[i].addr = (__le64)ring->desc_cb[i].dma;
	ring->desc[i].rx.ipoff_bnum_pid_flag = 0;
}

static inline void hnae_reuse_buffer(struct hnae_ring *ring, int i)
{
	ring->desc_cb[i].reuse_flag = 0;
	ring->desc[i].addr = (__le64)(ring->desc_cb[i].dma
		+ ring->desc_cb[i].page_offset);
	ring->desc[i].rx.ipoff_bnum_pid_flag = 0;
}

#define hnae_set_field(origin, mask, shift, val) \
	do { \
		(origin) &= (~(mask)); \
		(origin) |= ((val) << (shift)) & (mask); \
	} while (0)

#define hnae_set_bit(origin, shift, val) \
	hnae_set_field((origin), (0x1 << (shift)), (shift), (val))

#define hnae_get_field(origin, mask, shift) (((origin) & (mask)) >> (shift))

#define hnae_get_bit(origin, shift) \
	hnae_get_field((origin), (0x1 << (shift)), (shift))

#endif
