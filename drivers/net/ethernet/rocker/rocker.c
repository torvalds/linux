/*
 * drivers/net/ethernet/rocker/rocker.c - Rocker switch device driver
 * Copyright (c) 2014 Jiri Pirko <jiri@resnulli.us>
 * Copyright (c) 2014 Scott Feldman <sfeldma@gmail.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/pci.h>
#include <linux/interrupt.h>
#include <linux/sched.h>
#include <linux/wait.h>
#include <linux/spinlock.h>
#include <linux/hashtable.h>
#include <linux/crc32.h>
#include <linux/sort.h>
#include <linux/random.h>
#include <linux/netdevice.h>
#include <linux/inetdevice.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/if_bridge.h>
#include <linux/bitops.h>
#include <net/switchdev.h>
#include <net/rtnetlink.h>
#include <asm-generic/io-64-nonatomic-lo-hi.h>
#include <generated/utsrelease.h>

#include "rocker.h"

static const char rocker_driver_name[] = "rocker";

static const struct pci_device_id rocker_pci_id_table[] = {
	{PCI_VDEVICE(REDHAT, PCI_DEVICE_ID_REDHAT_ROCKER), 0},
	{0, }
};

struct rocker_flow_tbl_key {
	u32 priority;
	enum rocker_of_dpa_table_id tbl_id;
	union {
		struct {
			u32 in_lport;
			u32 in_lport_mask;
			enum rocker_of_dpa_table_id goto_tbl;
		} ig_port;
		struct {
			u32 in_lport;
			__be16 vlan_id;
			__be16 vlan_id_mask;
			enum rocker_of_dpa_table_id goto_tbl;
			bool untagged;
			__be16 new_vlan_id;
		} vlan;
		struct {
			u32 in_lport;
			u32 in_lport_mask;
			__be16 eth_type;
			u8 eth_dst[ETH_ALEN];
			u8 eth_dst_mask[ETH_ALEN];
			__be16 vlan_id;
			__be16 vlan_id_mask;
			enum rocker_of_dpa_table_id goto_tbl;
			bool copy_to_cpu;
		} term_mac;
		struct {
			__be16 eth_type;
			__be32 dst4;
			__be32 dst4_mask;
			enum rocker_of_dpa_table_id goto_tbl;
			u32 group_id;
		} ucast_routing;
		struct {
			u8 eth_dst[ETH_ALEN];
			u8 eth_dst_mask[ETH_ALEN];
			int has_eth_dst;
			int has_eth_dst_mask;
			__be16 vlan_id;
			u32 tunnel_id;
			enum rocker_of_dpa_table_id goto_tbl;
			u32 group_id;
			bool copy_to_cpu;
		} bridge;
		struct {
			u32 in_lport;
			u32 in_lport_mask;
			u8 eth_src[ETH_ALEN];
			u8 eth_src_mask[ETH_ALEN];
			u8 eth_dst[ETH_ALEN];
			u8 eth_dst_mask[ETH_ALEN];
			__be16 eth_type;
			__be16 vlan_id;
			__be16 vlan_id_mask;
			u8 ip_proto;
			u8 ip_proto_mask;
			u8 ip_tos;
			u8 ip_tos_mask;
			u32 group_id;
		} acl;
	};
};

struct rocker_flow_tbl_entry {
	struct hlist_node entry;
	u32 ref_count;
	u64 cookie;
	struct rocker_flow_tbl_key key;
	u32 key_crc32; /* key */
};

struct rocker_group_tbl_entry {
	struct hlist_node entry;
	u32 cmd;
	u32 group_id; /* key */
	u16 group_count;
	u32 *group_ids;
	union {
		struct {
			u8 pop_vlan;
		} l2_interface;
		struct {
			u8 eth_src[ETH_ALEN];
			u8 eth_dst[ETH_ALEN];
			__be16 vlan_id;
			u32 group_id;
		} l2_rewrite;
		struct {
			u8 eth_src[ETH_ALEN];
			u8 eth_dst[ETH_ALEN];
			__be16 vlan_id;
			bool ttl_check;
			u32 group_id;
		} l3_unicast;
	};
};

struct rocker_fdb_tbl_entry {
	struct hlist_node entry;
	u32 key_crc32; /* key */
	bool learned;
	struct rocker_fdb_tbl_key {
		u32 lport;
		u8 addr[ETH_ALEN];
		__be16 vlan_id;
	} key;
};

struct rocker_internal_vlan_tbl_entry {
	struct hlist_node entry;
	int ifindex; /* key */
	u32 ref_count;
	__be16 vlan_id;
};

struct rocker_desc_info {
	char *data; /* mapped */
	size_t data_size;
	size_t tlv_size;
	struct rocker_desc *desc;
	DEFINE_DMA_UNMAP_ADDR(mapaddr);
};

struct rocker_dma_ring_info {
	size_t size;
	u32 head;
	u32 tail;
	struct rocker_desc *desc; /* mapped */
	dma_addr_t mapaddr;
	struct rocker_desc_info *desc_info;
	unsigned int type;
};

struct rocker;

enum {
	ROCKER_CTRL_LINK_LOCAL_MCAST,
	ROCKER_CTRL_LOCAL_ARP,
	ROCKER_CTRL_IPV4_MCAST,
	ROCKER_CTRL_IPV6_MCAST,
	ROCKER_CTRL_DFLT_BRIDGING,
	ROCKER_CTRL_MAX,
};

#define ROCKER_INTERNAL_VLAN_ID_BASE	0x0f00
#define ROCKER_N_INTERNAL_VLANS		255
#define ROCKER_VLAN_BITMAP_LEN		BITS_TO_LONGS(VLAN_N_VID)
#define ROCKER_INTERNAL_VLAN_BITMAP_LEN	BITS_TO_LONGS(ROCKER_N_INTERNAL_VLANS)

struct rocker_port {
	struct net_device *dev;
	struct net_device *bridge_dev;
	struct rocker *rocker;
	unsigned int port_number;
	u32 lport;
	__be16 internal_vlan_id;
	int stp_state;
	u32 brport_flags;
	bool ctrls[ROCKER_CTRL_MAX];
	unsigned long vlan_bitmap[ROCKER_VLAN_BITMAP_LEN];
	struct napi_struct napi_tx;
	struct napi_struct napi_rx;
	struct rocker_dma_ring_info tx_ring;
	struct rocker_dma_ring_info rx_ring;
};

struct rocker {
	struct pci_dev *pdev;
	u8 __iomem *hw_addr;
	struct msix_entry *msix_entries;
	unsigned int port_count;
	struct rocker_port **ports;
	struct {
		u64 id;
	} hw;
	spinlock_t cmd_ring_lock;
	struct rocker_dma_ring_info cmd_ring;
	struct rocker_dma_ring_info event_ring;
	DECLARE_HASHTABLE(flow_tbl, 16);
	spinlock_t flow_tbl_lock;
	u64 flow_tbl_next_cookie;
	DECLARE_HASHTABLE(group_tbl, 16);
	spinlock_t group_tbl_lock;
	DECLARE_HASHTABLE(fdb_tbl, 16);
	spinlock_t fdb_tbl_lock;
	unsigned long internal_vlan_bitmap[ROCKER_INTERNAL_VLAN_BITMAP_LEN];
	DECLARE_HASHTABLE(internal_vlan_tbl, 8);
	spinlock_t internal_vlan_tbl_lock;
};

static const u8 zero_mac[ETH_ALEN]   = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u8 ff_mac[ETH_ALEN]     = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };
static const u8 ll_mac[ETH_ALEN]     = { 0x01, 0x80, 0xc2, 0x00, 0x00, 0x00 };
static const u8 ll_mask[ETH_ALEN]    = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xf0 };
static const u8 mcast_mac[ETH_ALEN]  = { 0x01, 0x00, 0x00, 0x00, 0x00, 0x00 };
static const u8 ipv4_mcast[ETH_ALEN] = { 0x01, 0x00, 0x5e, 0x00, 0x00, 0x00 };
static const u8 ipv4_mask[ETH_ALEN]  = { 0xff, 0xff, 0xff, 0x80, 0x00, 0x00 };
static const u8 ipv6_mcast[ETH_ALEN] = { 0x33, 0x33, 0x00, 0x00, 0x00, 0x00 };
static const u8 ipv6_mask[ETH_ALEN]  = { 0xff, 0xff, 0x00, 0x00, 0x00, 0x00 };

/* Rocker priority levels for flow table entries.  Higher
 * priority match takes precedence over lower priority match.
 */

enum {
	ROCKER_PRIORITY_UNKNOWN = 0,
	ROCKER_PRIORITY_IG_PORT = 1,
	ROCKER_PRIORITY_VLAN = 1,
	ROCKER_PRIORITY_TERM_MAC_UCAST = 0,
	ROCKER_PRIORITY_TERM_MAC_MCAST = 1,
	ROCKER_PRIORITY_UNICAST_ROUTING = 1,
	ROCKER_PRIORITY_BRIDGING_VLAN_DFLT_EXACT = 1,
	ROCKER_PRIORITY_BRIDGING_VLAN_DFLT_WILD = 2,
	ROCKER_PRIORITY_BRIDGING_VLAN = 3,
	ROCKER_PRIORITY_BRIDGING_TENANT_DFLT_EXACT = 1,
	ROCKER_PRIORITY_BRIDGING_TENANT_DFLT_WILD = 2,
	ROCKER_PRIORITY_BRIDGING_TENANT = 3,
	ROCKER_PRIORITY_ACL_CTRL = 3,
	ROCKER_PRIORITY_ACL_NORMAL = 2,
	ROCKER_PRIORITY_ACL_DFLT = 1,
};

static bool rocker_vlan_id_is_internal(__be16 vlan_id)
{
	u16 start = ROCKER_INTERNAL_VLAN_ID_BASE;
	u16 end = 0xffe;
	u16 _vlan_id = ntohs(vlan_id);

	return (_vlan_id >= start && _vlan_id <= end);
}

static __be16 rocker_port_vid_to_vlan(struct rocker_port *rocker_port,
				      u16 vid, bool *pop_vlan)
{
	__be16 vlan_id;

	if (pop_vlan)
		*pop_vlan = false;
	vlan_id = htons(vid);
	if (!vlan_id) {
		vlan_id = rocker_port->internal_vlan_id;
		if (pop_vlan)
			*pop_vlan = true;
	}

	return vlan_id;
}

static u16 rocker_port_vlan_to_vid(struct rocker_port *rocker_port,
				   __be16 vlan_id)
{
	if (rocker_vlan_id_is_internal(vlan_id))
		return 0;

	return ntohs(vlan_id);
}

static bool rocker_port_is_bridged(struct rocker_port *rocker_port)
{
	return !!rocker_port->bridge_dev;
}

struct rocker_wait {
	wait_queue_head_t wait;
	bool done;
	bool nowait;
};

static void rocker_wait_reset(struct rocker_wait *wait)
{
	wait->done = false;
	wait->nowait = false;
}

static void rocker_wait_init(struct rocker_wait *wait)
{
	init_waitqueue_head(&wait->wait);
	rocker_wait_reset(wait);
}

static struct rocker_wait *rocker_wait_create(gfp_t gfp)
{
	struct rocker_wait *wait;

	wait = kmalloc(sizeof(*wait), gfp);
	if (!wait)
		return NULL;
	rocker_wait_init(wait);
	return wait;
}

static void rocker_wait_destroy(struct rocker_wait *work)
{
	kfree(work);
}

static bool rocker_wait_event_timeout(struct rocker_wait *wait,
				      unsigned long timeout)
{
	wait_event_timeout(wait->wait, wait->done, HZ / 10);
	if (!wait->done)
		return false;
	return true;
}

static void rocker_wait_wake_up(struct rocker_wait *wait)
{
	wait->done = true;
	wake_up(&wait->wait);
}

static u32 rocker_msix_vector(struct rocker *rocker, unsigned int vector)
{
	return rocker->msix_entries[vector].vector;
}

static u32 rocker_msix_tx_vector(struct rocker_port *rocker_port)
{
	return rocker_msix_vector(rocker_port->rocker,
				  ROCKER_MSIX_VEC_TX(rocker_port->port_number));
}

static u32 rocker_msix_rx_vector(struct rocker_port *rocker_port)
{
	return rocker_msix_vector(rocker_port->rocker,
				  ROCKER_MSIX_VEC_RX(rocker_port->port_number));
}

#define rocker_write32(rocker, reg, val)	\
	writel((val), (rocker)->hw_addr + (ROCKER_ ## reg))
#define rocker_read32(rocker, reg)	\
	readl((rocker)->hw_addr + (ROCKER_ ## reg))
#define rocker_write64(rocker, reg, val)	\
	writeq((val), (rocker)->hw_addr + (ROCKER_ ## reg))
#define rocker_read64(rocker, reg)	\
	readq((rocker)->hw_addr + (ROCKER_ ## reg))

/*****************************
 * HW basic testing functions
 *****************************/

static int rocker_reg_test(struct rocker *rocker)
{
	struct pci_dev *pdev = rocker->pdev;
	u64 test_reg;
	u64 rnd;

	rnd = prandom_u32();
	rnd >>= 1;
	rocker_write32(rocker, TEST_REG, rnd);
	test_reg = rocker_read32(rocker, TEST_REG);
	if (test_reg != rnd * 2) {
		dev_err(&pdev->dev, "unexpected 32bit register value %08llx, expected %08llx\n",
			test_reg, rnd * 2);
		return -EIO;
	}

	rnd = prandom_u32();
	rnd <<= 31;
	rnd |= prandom_u32();
	rocker_write64(rocker, TEST_REG64, rnd);
	test_reg = rocker_read64(rocker, TEST_REG64);
	if (test_reg != rnd * 2) {
		dev_err(&pdev->dev, "unexpected 64bit register value %16llx, expected %16llx\n",
			test_reg, rnd * 2);
		return -EIO;
	}

	return 0;
}

static int rocker_dma_test_one(struct rocker *rocker, struct rocker_wait *wait,
			       u32 test_type, dma_addr_t dma_handle,
			       unsigned char *buf, unsigned char *expect,
			       size_t size)
{
	struct pci_dev *pdev = rocker->pdev;
	int i;

	rocker_wait_reset(wait);
	rocker_write32(rocker, TEST_DMA_CTRL, test_type);

	if (!rocker_wait_event_timeout(wait, HZ / 10)) {
		dev_err(&pdev->dev, "no interrupt received within a timeout\n");
		return -EIO;
	}

	for (i = 0; i < size; i++) {
		if (buf[i] != expect[i]) {
			dev_err(&pdev->dev, "unexpected memory content %02x at byte %x\n, %02x expected",
				buf[i], i, expect[i]);
			return -EIO;
		}
	}
	return 0;
}

#define ROCKER_TEST_DMA_BUF_SIZE (PAGE_SIZE * 4)
#define ROCKER_TEST_DMA_FILL_PATTERN 0x96

static int rocker_dma_test_offset(struct rocker *rocker,
				  struct rocker_wait *wait, int offset)
{
	struct pci_dev *pdev = rocker->pdev;
	unsigned char *alloc;
	unsigned char *buf;
	unsigned char *expect;
	dma_addr_t dma_handle;
	int i;
	int err;

	alloc = kzalloc(ROCKER_TEST_DMA_BUF_SIZE * 2 + offset,
			GFP_KERNEL | GFP_DMA);
	if (!alloc)
		return -ENOMEM;
	buf = alloc + offset;
	expect = buf + ROCKER_TEST_DMA_BUF_SIZE;

	dma_handle = pci_map_single(pdev, buf, ROCKER_TEST_DMA_BUF_SIZE,
				    PCI_DMA_BIDIRECTIONAL);
	if (pci_dma_mapping_error(pdev, dma_handle)) {
		err = -EIO;
		goto free_alloc;
	}

	rocker_write64(rocker, TEST_DMA_ADDR, dma_handle);
	rocker_write32(rocker, TEST_DMA_SIZE, ROCKER_TEST_DMA_BUF_SIZE);

	memset(expect, ROCKER_TEST_DMA_FILL_PATTERN, ROCKER_TEST_DMA_BUF_SIZE);
	err = rocker_dma_test_one(rocker, wait, ROCKER_TEST_DMA_CTRL_FILL,
				  dma_handle, buf, expect,
				  ROCKER_TEST_DMA_BUF_SIZE);
	if (err)
		goto unmap;

	memset(expect, 0, ROCKER_TEST_DMA_BUF_SIZE);
	err = rocker_dma_test_one(rocker, wait, ROCKER_TEST_DMA_CTRL_CLEAR,
				  dma_handle, buf, expect,
				  ROCKER_TEST_DMA_BUF_SIZE);
	if (err)
		goto unmap;

	prandom_bytes(buf, ROCKER_TEST_DMA_BUF_SIZE);
	for (i = 0; i < ROCKER_TEST_DMA_BUF_SIZE; i++)
		expect[i] = ~buf[i];
	err = rocker_dma_test_one(rocker, wait, ROCKER_TEST_DMA_CTRL_INVERT,
				  dma_handle, buf, expect,
				  ROCKER_TEST_DMA_BUF_SIZE);
	if (err)
		goto unmap;

unmap:
	pci_unmap_single(pdev, dma_handle, ROCKER_TEST_DMA_BUF_SIZE,
			 PCI_DMA_BIDIRECTIONAL);
free_alloc:
	kfree(alloc);

	return err;
}

static int rocker_dma_test(struct rocker *rocker, struct rocker_wait *wait)
{
	int i;
	int err;

	for (i = 0; i < 8; i++) {
		err = rocker_dma_test_offset(rocker, wait, i);
		if (err)
			return err;
	}
	return 0;
}

static irqreturn_t rocker_test_irq_handler(int irq, void *dev_id)
{
	struct rocker_wait *wait = dev_id;

	rocker_wait_wake_up(wait);

	return IRQ_HANDLED;
}

static int rocker_basic_hw_test(struct rocker *rocker)
{
	struct pci_dev *pdev = rocker->pdev;
	struct rocker_wait wait;
	int err;

	err = rocker_reg_test(rocker);
	if (err) {
		dev_err(&pdev->dev, "reg test failed\n");
		return err;
	}

	err = request_irq(rocker_msix_vector(rocker, ROCKER_MSIX_VEC_TEST),
			  rocker_test_irq_handler, 0,
			  rocker_driver_name, &wait);
	if (err) {
		dev_err(&pdev->dev, "cannot assign test irq\n");
		return err;
	}

	rocker_wait_init(&wait);
	rocker_write32(rocker, TEST_IRQ, ROCKER_MSIX_VEC_TEST);

	if (!rocker_wait_event_timeout(&wait, HZ / 10)) {
		dev_err(&pdev->dev, "no interrupt received within a timeout\n");
		err = -EIO;
		goto free_irq;
	}

	err = rocker_dma_test(rocker, &wait);
	if (err)
		dev_err(&pdev->dev, "dma test failed\n");

free_irq:
	free_irq(rocker_msix_vector(rocker, ROCKER_MSIX_VEC_TEST), &wait);
	return err;
}

/******
 * TLV
 ******/

#define ROCKER_TLV_ALIGNTO 8U
#define ROCKER_TLV_ALIGN(len) \
	(((len) + ROCKER_TLV_ALIGNTO - 1) & ~(ROCKER_TLV_ALIGNTO - 1))
#define ROCKER_TLV_HDRLEN ROCKER_TLV_ALIGN(sizeof(struct rocker_tlv))

/*  <------- ROCKER_TLV_HDRLEN -------> <--- ROCKER_TLV_ALIGN(payload) --->
 * +-----------------------------+- - -+- - - - - - - - - - - - - - -+- - -+
 * |             Header          | Pad |           Payload           | Pad |
 * |      (struct rocker_tlv)    | ing |                             | ing |
 * +-----------------------------+- - -+- - - - - - - - - - - - - - -+- - -+
 *  <--------------------------- tlv->len -------------------------->
 */

static struct rocker_tlv *rocker_tlv_next(const struct rocker_tlv *tlv,
					  int *remaining)
{
	int totlen = ROCKER_TLV_ALIGN(tlv->len);

	*remaining -= totlen;
	return (struct rocker_tlv *) ((char *) tlv + totlen);
}

static int rocker_tlv_ok(const struct rocker_tlv *tlv, int remaining)
{
	return remaining >= (int) ROCKER_TLV_HDRLEN &&
	       tlv->len >= ROCKER_TLV_HDRLEN &&
	       tlv->len <= remaining;
}

#define rocker_tlv_for_each(pos, head, len, rem)	\
	for (pos = head, rem = len;			\
	     rocker_tlv_ok(pos, rem);			\
	     pos = rocker_tlv_next(pos, &(rem)))

#define rocker_tlv_for_each_nested(pos, tlv, rem)	\
	rocker_tlv_for_each(pos, rocker_tlv_data(tlv),	\
			    rocker_tlv_len(tlv), rem)

static int rocker_tlv_attr_size(int payload)
{
	return ROCKER_TLV_HDRLEN + payload;
}

static int rocker_tlv_total_size(int payload)
{
	return ROCKER_TLV_ALIGN(rocker_tlv_attr_size(payload));
}

static int rocker_tlv_padlen(int payload)
{
	return rocker_tlv_total_size(payload) - rocker_tlv_attr_size(payload);
}

static int rocker_tlv_type(const struct rocker_tlv *tlv)
{
	return tlv->type;
}

static void *rocker_tlv_data(const struct rocker_tlv *tlv)
{
	return (char *) tlv + ROCKER_TLV_HDRLEN;
}

static int rocker_tlv_len(const struct rocker_tlv *tlv)
{
	return tlv->len - ROCKER_TLV_HDRLEN;
}

static u8 rocker_tlv_get_u8(const struct rocker_tlv *tlv)
{
	return *(u8 *) rocker_tlv_data(tlv);
}

static u16 rocker_tlv_get_u16(const struct rocker_tlv *tlv)
{
	return *(u16 *) rocker_tlv_data(tlv);
}

static __be16 rocker_tlv_get_be16(const struct rocker_tlv *tlv)
{
	return *(__be16 *) rocker_tlv_data(tlv);
}

static u32 rocker_tlv_get_u32(const struct rocker_tlv *tlv)
{
	return *(u32 *) rocker_tlv_data(tlv);
}

static u64 rocker_tlv_get_u64(const struct rocker_tlv *tlv)
{
	return *(u64 *) rocker_tlv_data(tlv);
}

static void rocker_tlv_parse(struct rocker_tlv **tb, int maxtype,
			     const char *buf, int buf_len)
{
	const struct rocker_tlv *tlv;
	const struct rocker_tlv *head = (const struct rocker_tlv *) buf;
	int rem;

	memset(tb, 0, sizeof(struct rocker_tlv *) * (maxtype + 1));

	rocker_tlv_for_each(tlv, head, buf_len, rem) {
		u32 type = rocker_tlv_type(tlv);

		if (type > 0 && type <= maxtype)
			tb[type] = (struct rocker_tlv *) tlv;
	}
}

static void rocker_tlv_parse_nested(struct rocker_tlv **tb, int maxtype,
				    const struct rocker_tlv *tlv)
{
	rocker_tlv_parse(tb, maxtype, rocker_tlv_data(tlv),
			 rocker_tlv_len(tlv));
}

static void rocker_tlv_parse_desc(struct rocker_tlv **tb, int maxtype,
				  struct rocker_desc_info *desc_info)
{
	rocker_tlv_parse(tb, maxtype, desc_info->data,
			 desc_info->desc->tlv_size);
}

static struct rocker_tlv *rocker_tlv_start(struct rocker_desc_info *desc_info)
{
	return (struct rocker_tlv *) ((char *) desc_info->data +
					       desc_info->tlv_size);
}

static int rocker_tlv_put(struct rocker_desc_info *desc_info,
			  int attrtype, int attrlen, const void *data)
{
	int tail_room = desc_info->data_size - desc_info->tlv_size;
	int total_size = rocker_tlv_total_size(attrlen);
	struct rocker_tlv *tlv;

	if (unlikely(tail_room < total_size))
		return -EMSGSIZE;

	tlv = rocker_tlv_start(desc_info);
	desc_info->tlv_size += total_size;
	tlv->type = attrtype;
	tlv->len = rocker_tlv_attr_size(attrlen);
	memcpy(rocker_tlv_data(tlv), data, attrlen);
	memset((char *) tlv + tlv->len, 0, rocker_tlv_padlen(attrlen));
	return 0;
}

static int rocker_tlv_put_u8(struct rocker_desc_info *desc_info,
			     int attrtype, u8 value)
{
	return rocker_tlv_put(desc_info, attrtype, sizeof(u8), &value);
}

static int rocker_tlv_put_u16(struct rocker_desc_info *desc_info,
			      int attrtype, u16 value)
{
	return rocker_tlv_put(desc_info, attrtype, sizeof(u16), &value);
}

static int rocker_tlv_put_be16(struct rocker_desc_info *desc_info,
			       int attrtype, __be16 value)
{
	return rocker_tlv_put(desc_info, attrtype, sizeof(__be16), &value);
}

static int rocker_tlv_put_u32(struct rocker_desc_info *desc_info,
			      int attrtype, u32 value)
{
	return rocker_tlv_put(desc_info, attrtype, sizeof(u32), &value);
}

static int rocker_tlv_put_be32(struct rocker_desc_info *desc_info,
			       int attrtype, __be32 value)
{
	return rocker_tlv_put(desc_info, attrtype, sizeof(__be32), &value);
}

static int rocker_tlv_put_u64(struct rocker_desc_info *desc_info,
			      int attrtype, u64 value)
{
	return rocker_tlv_put(desc_info, attrtype, sizeof(u64), &value);
}

static struct rocker_tlv *
rocker_tlv_nest_start(struct rocker_desc_info *desc_info, int attrtype)
{
	struct rocker_tlv *start = rocker_tlv_start(desc_info);

	if (rocker_tlv_put(desc_info, attrtype, 0, NULL) < 0)
		return NULL;

	return start;
}

static void rocker_tlv_nest_end(struct rocker_desc_info *desc_info,
				struct rocker_tlv *start)
{
	start->len = (char *) rocker_tlv_start(desc_info) - (char *) start;
}

static void rocker_tlv_nest_cancel(struct rocker_desc_info *desc_info,
				   struct rocker_tlv *start)
{
	desc_info->tlv_size = (char *) start - desc_info->data;
}

/******************************************
 * DMA rings and descriptors manipulations
 ******************************************/

static u32 __pos_inc(u32 pos, size_t limit)
{
	return ++pos == limit ? 0 : pos;
}

static int rocker_desc_err(struct rocker_desc_info *desc_info)
{
	return -(desc_info->desc->comp_err & ~ROCKER_DMA_DESC_COMP_ERR_GEN);
}

static void rocker_desc_gen_clear(struct rocker_desc_info *desc_info)
{
	desc_info->desc->comp_err &= ~ROCKER_DMA_DESC_COMP_ERR_GEN;
}

static bool rocker_desc_gen(struct rocker_desc_info *desc_info)
{
	u32 comp_err = desc_info->desc->comp_err;

	return comp_err & ROCKER_DMA_DESC_COMP_ERR_GEN ? true : false;
}

static void *rocker_desc_cookie_ptr_get(struct rocker_desc_info *desc_info)
{
	return (void *) desc_info->desc->cookie;
}

static void rocker_desc_cookie_ptr_set(struct rocker_desc_info *desc_info,
				       void *ptr)
{
	desc_info->desc->cookie = (long) ptr;
}

static struct rocker_desc_info *
rocker_desc_head_get(struct rocker_dma_ring_info *info)
{
	static struct rocker_desc_info *desc_info;
	u32 head = __pos_inc(info->head, info->size);

	desc_info = &info->desc_info[info->head];
	if (head == info->tail)
		return NULL; /* ring full */
	desc_info->tlv_size = 0;
	return desc_info;
}

static void rocker_desc_commit(struct rocker_desc_info *desc_info)
{
	desc_info->desc->buf_size = desc_info->data_size;
	desc_info->desc->tlv_size = desc_info->tlv_size;
}

static void rocker_desc_head_set(struct rocker *rocker,
				 struct rocker_dma_ring_info *info,
				 struct rocker_desc_info *desc_info)
{
	u32 head = __pos_inc(info->head, info->size);

	BUG_ON(head == info->tail);
	rocker_desc_commit(desc_info);
	info->head = head;
	rocker_write32(rocker, DMA_DESC_HEAD(info->type), head);
}

static struct rocker_desc_info *
rocker_desc_tail_get(struct rocker_dma_ring_info *info)
{
	static struct rocker_desc_info *desc_info;

	if (info->tail == info->head)
		return NULL; /* nothing to be done between head and tail */
	desc_info = &info->desc_info[info->tail];
	if (!rocker_desc_gen(desc_info))
		return NULL; /* gen bit not set, desc is not ready yet */
	info->tail = __pos_inc(info->tail, info->size);
	desc_info->tlv_size = desc_info->desc->tlv_size;
	return desc_info;
}

static void rocker_dma_ring_credits_set(struct rocker *rocker,
					struct rocker_dma_ring_info *info,
					u32 credits)
{
	if (credits)
		rocker_write32(rocker, DMA_DESC_CREDITS(info->type), credits);
}

static unsigned long rocker_dma_ring_size_fix(size_t size)
{
	return max(ROCKER_DMA_SIZE_MIN,
		   min(roundup_pow_of_two(size), ROCKER_DMA_SIZE_MAX));
}

static int rocker_dma_ring_create(struct rocker *rocker,
				  unsigned int type,
				  size_t size,
				  struct rocker_dma_ring_info *info)
{
	int i;

	BUG_ON(size != rocker_dma_ring_size_fix(size));
	info->size = size;
	info->type = type;
	info->head = 0;
	info->tail = 0;
	info->desc_info = kcalloc(info->size, sizeof(*info->desc_info),
				  GFP_KERNEL);
	if (!info->desc_info)
		return -ENOMEM;

	info->desc = pci_alloc_consistent(rocker->pdev,
					  info->size * sizeof(*info->desc),
					  &info->mapaddr);
	if (!info->desc) {
		kfree(info->desc_info);
		return -ENOMEM;
	}

	for (i = 0; i < info->size; i++)
		info->desc_info[i].desc = &info->desc[i];

	rocker_write32(rocker, DMA_DESC_CTRL(info->type),
		       ROCKER_DMA_DESC_CTRL_RESET);
	rocker_write64(rocker, DMA_DESC_ADDR(info->type), info->mapaddr);
	rocker_write32(rocker, DMA_DESC_SIZE(info->type), info->size);

	return 0;
}

static void rocker_dma_ring_destroy(struct rocker *rocker,
				    struct rocker_dma_ring_info *info)
{
	rocker_write64(rocker, DMA_DESC_ADDR(info->type), 0);

	pci_free_consistent(rocker->pdev,
			    info->size * sizeof(struct rocker_desc),
			    info->desc, info->mapaddr);
	kfree(info->desc_info);
}

static void rocker_dma_ring_pass_to_producer(struct rocker *rocker,
					     struct rocker_dma_ring_info *info)
{
	int i;

	BUG_ON(info->head || info->tail);

	/* When ring is consumer, we need to advance head for each desc.
	 * That tells hw that the desc is ready to be used by it.
	 */
	for (i = 0; i < info->size - 1; i++)
		rocker_desc_head_set(rocker, info, &info->desc_info[i]);
	rocker_desc_commit(&info->desc_info[i]);
}

static int rocker_dma_ring_bufs_alloc(struct rocker *rocker,
				      struct rocker_dma_ring_info *info,
				      int direction, size_t buf_size)
{
	struct pci_dev *pdev = rocker->pdev;
	int i;
	int err;

	for (i = 0; i < info->size; i++) {
		struct rocker_desc_info *desc_info = &info->desc_info[i];
		struct rocker_desc *desc = &info->desc[i];
		dma_addr_t dma_handle;
		char *buf;

		buf = kzalloc(buf_size, GFP_KERNEL | GFP_DMA);
		if (!buf) {
			err = -ENOMEM;
			goto rollback;
		}

		dma_handle = pci_map_single(pdev, buf, buf_size, direction);
		if (pci_dma_mapping_error(pdev, dma_handle)) {
			kfree(buf);
			err = -EIO;
			goto rollback;
		}

		desc_info->data = buf;
		desc_info->data_size = buf_size;
		dma_unmap_addr_set(desc_info, mapaddr, dma_handle);

		desc->buf_addr = dma_handle;
		desc->buf_size = buf_size;
	}
	return 0;

rollback:
	for (i--; i >= 0; i--) {
		struct rocker_desc_info *desc_info = &info->desc_info[i];

		pci_unmap_single(pdev, dma_unmap_addr(desc_info, mapaddr),
				 desc_info->data_size, direction);
		kfree(desc_info->data);
	}
	return err;
}

static void rocker_dma_ring_bufs_free(struct rocker *rocker,
				      struct rocker_dma_ring_info *info,
				      int direction)
{
	struct pci_dev *pdev = rocker->pdev;
	int i;

	for (i = 0; i < info->size; i++) {
		struct rocker_desc_info *desc_info = &info->desc_info[i];
		struct rocker_desc *desc = &info->desc[i];

		desc->buf_addr = 0;
		desc->buf_size = 0;
		pci_unmap_single(pdev, dma_unmap_addr(desc_info, mapaddr),
				 desc_info->data_size, direction);
		kfree(desc_info->data);
	}
}

static int rocker_dma_rings_init(struct rocker *rocker)
{
	struct pci_dev *pdev = rocker->pdev;
	int err;

	err = rocker_dma_ring_create(rocker, ROCKER_DMA_CMD,
				     ROCKER_DMA_CMD_DEFAULT_SIZE,
				     &rocker->cmd_ring);
	if (err) {
		dev_err(&pdev->dev, "failed to create command dma ring\n");
		return err;
	}

	spin_lock_init(&rocker->cmd_ring_lock);

	err = rocker_dma_ring_bufs_alloc(rocker, &rocker->cmd_ring,
					 PCI_DMA_BIDIRECTIONAL, PAGE_SIZE);
	if (err) {
		dev_err(&pdev->dev, "failed to alloc command dma ring buffers\n");
		goto err_dma_cmd_ring_bufs_alloc;
	}

	err = rocker_dma_ring_create(rocker, ROCKER_DMA_EVENT,
				     ROCKER_DMA_EVENT_DEFAULT_SIZE,
				     &rocker->event_ring);
	if (err) {
		dev_err(&pdev->dev, "failed to create event dma ring\n");
		goto err_dma_event_ring_create;
	}

	err = rocker_dma_ring_bufs_alloc(rocker, &rocker->event_ring,
					 PCI_DMA_FROMDEVICE, PAGE_SIZE);
	if (err) {
		dev_err(&pdev->dev, "failed to alloc event dma ring buffers\n");
		goto err_dma_event_ring_bufs_alloc;
	}
	rocker_dma_ring_pass_to_producer(rocker, &rocker->event_ring);
	return 0;

err_dma_event_ring_bufs_alloc:
	rocker_dma_ring_destroy(rocker, &rocker->event_ring);
err_dma_event_ring_create:
	rocker_dma_ring_bufs_free(rocker, &rocker->cmd_ring,
				  PCI_DMA_BIDIRECTIONAL);
err_dma_cmd_ring_bufs_alloc:
	rocker_dma_ring_destroy(rocker, &rocker->cmd_ring);
	return err;
}

static void rocker_dma_rings_fini(struct rocker *rocker)
{
	rocker_dma_ring_bufs_free(rocker, &rocker->event_ring,
				  PCI_DMA_BIDIRECTIONAL);
	rocker_dma_ring_destroy(rocker, &rocker->event_ring);
	rocker_dma_ring_bufs_free(rocker, &rocker->cmd_ring,
				  PCI_DMA_BIDIRECTIONAL);
	rocker_dma_ring_destroy(rocker, &rocker->cmd_ring);
}

static int rocker_dma_rx_ring_skb_map(struct rocker *rocker,
				      struct rocker_port *rocker_port,
				      struct rocker_desc_info *desc_info,
				      struct sk_buff *skb, size_t buf_len)
{
	struct pci_dev *pdev = rocker->pdev;
	dma_addr_t dma_handle;

	dma_handle = pci_map_single(pdev, skb->data, buf_len,
				    PCI_DMA_FROMDEVICE);
	if (pci_dma_mapping_error(pdev, dma_handle))
		return -EIO;
	if (rocker_tlv_put_u64(desc_info, ROCKER_TLV_RX_FRAG_ADDR, dma_handle))
		goto tlv_put_failure;
	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_RX_FRAG_MAX_LEN, buf_len))
		goto tlv_put_failure;
	return 0;

tlv_put_failure:
	pci_unmap_single(pdev, dma_handle, buf_len, PCI_DMA_FROMDEVICE);
	desc_info->tlv_size = 0;
	return -EMSGSIZE;
}

static size_t rocker_port_rx_buf_len(struct rocker_port *rocker_port)
{
	return rocker_port->dev->mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;
}

static int rocker_dma_rx_ring_skb_alloc(struct rocker *rocker,
					struct rocker_port *rocker_port,
					struct rocker_desc_info *desc_info)
{
	struct net_device *dev = rocker_port->dev;
	struct sk_buff *skb;
	size_t buf_len = rocker_port_rx_buf_len(rocker_port);
	int err;

	/* Ensure that hw will see tlv_size zero in case of an error.
	 * That tells hw to use another descriptor.
	 */
	rocker_desc_cookie_ptr_set(desc_info, NULL);
	desc_info->tlv_size = 0;

	skb = netdev_alloc_skb_ip_align(dev, buf_len);
	if (!skb)
		return -ENOMEM;
	err = rocker_dma_rx_ring_skb_map(rocker, rocker_port, desc_info,
					 skb, buf_len);
	if (err) {
		dev_kfree_skb_any(skb);
		return err;
	}
	rocker_desc_cookie_ptr_set(desc_info, skb);
	return 0;
}

static void rocker_dma_rx_ring_skb_unmap(struct rocker *rocker,
					 struct rocker_tlv **attrs)
{
	struct pci_dev *pdev = rocker->pdev;
	dma_addr_t dma_handle;
	size_t len;

	if (!attrs[ROCKER_TLV_RX_FRAG_ADDR] ||
	    !attrs[ROCKER_TLV_RX_FRAG_MAX_LEN])
		return;
	dma_handle = rocker_tlv_get_u64(attrs[ROCKER_TLV_RX_FRAG_ADDR]);
	len = rocker_tlv_get_u16(attrs[ROCKER_TLV_RX_FRAG_MAX_LEN]);
	pci_unmap_single(pdev, dma_handle, len, PCI_DMA_FROMDEVICE);
}

static void rocker_dma_rx_ring_skb_free(struct rocker *rocker,
					struct rocker_desc_info *desc_info)
{
	struct rocker_tlv *attrs[ROCKER_TLV_RX_MAX + 1];
	struct sk_buff *skb = rocker_desc_cookie_ptr_get(desc_info);

	if (!skb)
		return;
	rocker_tlv_parse_desc(attrs, ROCKER_TLV_RX_MAX, desc_info);
	rocker_dma_rx_ring_skb_unmap(rocker, attrs);
	dev_kfree_skb_any(skb);
}

static int rocker_dma_rx_ring_skbs_alloc(struct rocker *rocker,
					 struct rocker_port *rocker_port)
{
	struct rocker_dma_ring_info *rx_ring = &rocker_port->rx_ring;
	int i;
	int err;

	for (i = 0; i < rx_ring->size; i++) {
		err = rocker_dma_rx_ring_skb_alloc(rocker, rocker_port,
						   &rx_ring->desc_info[i]);
		if (err)
			goto rollback;
	}
	return 0;

rollback:
	for (i--; i >= 0; i--)
		rocker_dma_rx_ring_skb_free(rocker, &rx_ring->desc_info[i]);
	return err;
}

static void rocker_dma_rx_ring_skbs_free(struct rocker *rocker,
					 struct rocker_port *rocker_port)
{
	struct rocker_dma_ring_info *rx_ring = &rocker_port->rx_ring;
	int i;

	for (i = 0; i < rx_ring->size; i++)
		rocker_dma_rx_ring_skb_free(rocker, &rx_ring->desc_info[i]);
}

static int rocker_port_dma_rings_init(struct rocker_port *rocker_port)
{
	struct rocker *rocker = rocker_port->rocker;
	int err;

	err = rocker_dma_ring_create(rocker,
				     ROCKER_DMA_TX(rocker_port->port_number),
				     ROCKER_DMA_TX_DEFAULT_SIZE,
				     &rocker_port->tx_ring);
	if (err) {
		netdev_err(rocker_port->dev, "failed to create tx dma ring\n");
		return err;
	}

	err = rocker_dma_ring_bufs_alloc(rocker, &rocker_port->tx_ring,
					 PCI_DMA_TODEVICE,
					 ROCKER_DMA_TX_DESC_SIZE);
	if (err) {
		netdev_err(rocker_port->dev, "failed to alloc tx dma ring buffers\n");
		goto err_dma_tx_ring_bufs_alloc;
	}

	err = rocker_dma_ring_create(rocker,
				     ROCKER_DMA_RX(rocker_port->port_number),
				     ROCKER_DMA_RX_DEFAULT_SIZE,
				     &rocker_port->rx_ring);
	if (err) {
		netdev_err(rocker_port->dev, "failed to create rx dma ring\n");
		goto err_dma_rx_ring_create;
	}

	err = rocker_dma_ring_bufs_alloc(rocker, &rocker_port->rx_ring,
					 PCI_DMA_BIDIRECTIONAL,
					 ROCKER_DMA_RX_DESC_SIZE);
	if (err) {
		netdev_err(rocker_port->dev, "failed to alloc rx dma ring buffers\n");
		goto err_dma_rx_ring_bufs_alloc;
	}

	err = rocker_dma_rx_ring_skbs_alloc(rocker, rocker_port);
	if (err) {
		netdev_err(rocker_port->dev, "failed to alloc rx dma ring skbs\n");
		goto err_dma_rx_ring_skbs_alloc;
	}
	rocker_dma_ring_pass_to_producer(rocker, &rocker_port->rx_ring);

	return 0;

err_dma_rx_ring_skbs_alloc:
	rocker_dma_ring_bufs_free(rocker, &rocker_port->rx_ring,
				  PCI_DMA_BIDIRECTIONAL);
err_dma_rx_ring_bufs_alloc:
	rocker_dma_ring_destroy(rocker, &rocker_port->rx_ring);
err_dma_rx_ring_create:
	rocker_dma_ring_bufs_free(rocker, &rocker_port->tx_ring,
				  PCI_DMA_TODEVICE);
err_dma_tx_ring_bufs_alloc:
	rocker_dma_ring_destroy(rocker, &rocker_port->tx_ring);
	return err;
}

static void rocker_port_dma_rings_fini(struct rocker_port *rocker_port)
{
	struct rocker *rocker = rocker_port->rocker;

	rocker_dma_rx_ring_skbs_free(rocker, rocker_port);
	rocker_dma_ring_bufs_free(rocker, &rocker_port->rx_ring,
				  PCI_DMA_BIDIRECTIONAL);
	rocker_dma_ring_destroy(rocker, &rocker_port->rx_ring);
	rocker_dma_ring_bufs_free(rocker, &rocker_port->tx_ring,
				  PCI_DMA_TODEVICE);
	rocker_dma_ring_destroy(rocker, &rocker_port->tx_ring);
}

static void rocker_port_set_enable(struct rocker_port *rocker_port, bool enable)
{
	u64 val = rocker_read64(rocker_port->rocker, PORT_PHYS_ENABLE);

	if (enable)
		val |= 1 << rocker_port->lport;
	else
		val &= ~(1 << rocker_port->lport);
	rocker_write64(rocker_port->rocker, PORT_PHYS_ENABLE, val);
}

/********************************
 * Interrupt handler and helpers
 ********************************/

static irqreturn_t rocker_cmd_irq_handler(int irq, void *dev_id)
{
	struct rocker *rocker = dev_id;
	struct rocker_desc_info *desc_info;
	struct rocker_wait *wait;
	u32 credits = 0;

	spin_lock(&rocker->cmd_ring_lock);
	while ((desc_info = rocker_desc_tail_get(&rocker->cmd_ring))) {
		wait = rocker_desc_cookie_ptr_get(desc_info);
		if (wait->nowait) {
			rocker_desc_gen_clear(desc_info);
			rocker_wait_destroy(wait);
		} else {
			rocker_wait_wake_up(wait);
		}
		credits++;
	}
	spin_unlock(&rocker->cmd_ring_lock);
	rocker_dma_ring_credits_set(rocker, &rocker->cmd_ring, credits);

	return IRQ_HANDLED;
}

static void rocker_port_link_up(struct rocker_port *rocker_port)
{
	netif_carrier_on(rocker_port->dev);
	netdev_info(rocker_port->dev, "Link is up\n");
}

static void rocker_port_link_down(struct rocker_port *rocker_port)
{
	netif_carrier_off(rocker_port->dev);
	netdev_info(rocker_port->dev, "Link is down\n");
}

static int rocker_event_link_change(struct rocker *rocker,
				    const struct rocker_tlv *info)
{
	struct rocker_tlv *attrs[ROCKER_TLV_EVENT_LINK_CHANGED_MAX + 1];
	unsigned int port_number;
	bool link_up;
	struct rocker_port *rocker_port;

	rocker_tlv_parse_nested(attrs, ROCKER_TLV_EVENT_LINK_CHANGED_MAX, info);
	if (!attrs[ROCKER_TLV_EVENT_LINK_CHANGED_LPORT] ||
	    !attrs[ROCKER_TLV_EVENT_LINK_CHANGED_LINKUP])
		return -EIO;
	port_number =
		rocker_tlv_get_u32(attrs[ROCKER_TLV_EVENT_LINK_CHANGED_LPORT]) - 1;
	link_up = rocker_tlv_get_u8(attrs[ROCKER_TLV_EVENT_LINK_CHANGED_LINKUP]);

	if (port_number >= rocker->port_count)
		return -EINVAL;

	rocker_port = rocker->ports[port_number];
	if (netif_carrier_ok(rocker_port->dev) != link_up) {
		if (link_up)
			rocker_port_link_up(rocker_port);
		else
			rocker_port_link_down(rocker_port);
	}

	return 0;
}

#define ROCKER_OP_FLAG_REMOVE		BIT(0)
#define ROCKER_OP_FLAG_NOWAIT		BIT(1)
#define ROCKER_OP_FLAG_LEARNED		BIT(2)
#define ROCKER_OP_FLAG_REFRESH		BIT(3)

static int rocker_port_fdb(struct rocker_port *rocker_port,
			   const unsigned char *addr,
			   __be16 vlan_id, int flags);

static int rocker_event_mac_vlan_seen(struct rocker *rocker,
				      const struct rocker_tlv *info)
{
	struct rocker_tlv *attrs[ROCKER_TLV_EVENT_MAC_VLAN_MAX + 1];
	unsigned int port_number;
	struct rocker_port *rocker_port;
	unsigned char *addr;
	int flags = ROCKER_OP_FLAG_NOWAIT | ROCKER_OP_FLAG_LEARNED;
	__be16 vlan_id;

	rocker_tlv_parse_nested(attrs, ROCKER_TLV_EVENT_MAC_VLAN_MAX, info);
	if (!attrs[ROCKER_TLV_EVENT_MAC_VLAN_LPORT] ||
	    !attrs[ROCKER_TLV_EVENT_MAC_VLAN_MAC] ||
	    !attrs[ROCKER_TLV_EVENT_MAC_VLAN_VLAN_ID])
		return -EIO;
	port_number =
		rocker_tlv_get_u32(attrs[ROCKER_TLV_EVENT_MAC_VLAN_LPORT]) - 1;
	addr = rocker_tlv_data(attrs[ROCKER_TLV_EVENT_MAC_VLAN_MAC]);
	vlan_id = rocker_tlv_get_be16(attrs[ROCKER_TLV_EVENT_MAC_VLAN_VLAN_ID]);

	if (port_number >= rocker->port_count)
		return -EINVAL;

	rocker_port = rocker->ports[port_number];

	if (rocker_port->stp_state != BR_STATE_LEARNING &&
	    rocker_port->stp_state != BR_STATE_FORWARDING)
		return 0;

	return rocker_port_fdb(rocker_port, addr, vlan_id, flags);
}

static int rocker_event_process(struct rocker *rocker,
				struct rocker_desc_info *desc_info)
{
	struct rocker_tlv *attrs[ROCKER_TLV_EVENT_MAX + 1];
	struct rocker_tlv *info;
	u16 type;

	rocker_tlv_parse_desc(attrs, ROCKER_TLV_EVENT_MAX, desc_info);
	if (!attrs[ROCKER_TLV_EVENT_TYPE] ||
	    !attrs[ROCKER_TLV_EVENT_INFO])
		return -EIO;

	type = rocker_tlv_get_u16(attrs[ROCKER_TLV_EVENT_TYPE]);
	info = attrs[ROCKER_TLV_EVENT_INFO];

	switch (type) {
	case ROCKER_TLV_EVENT_TYPE_LINK_CHANGED:
		return rocker_event_link_change(rocker, info);
	case ROCKER_TLV_EVENT_TYPE_MAC_VLAN_SEEN:
		return rocker_event_mac_vlan_seen(rocker, info);
	}

	return -EOPNOTSUPP;
}

static irqreturn_t rocker_event_irq_handler(int irq, void *dev_id)
{
	struct rocker *rocker = dev_id;
	struct pci_dev *pdev = rocker->pdev;
	struct rocker_desc_info *desc_info;
	u32 credits = 0;
	int err;

	while ((desc_info = rocker_desc_tail_get(&rocker->event_ring))) {
		err = rocker_desc_err(desc_info);
		if (err) {
			dev_err(&pdev->dev, "event desc received with err %d\n",
				err);
		} else {
			err = rocker_event_process(rocker, desc_info);
			if (err)
				dev_err(&pdev->dev, "event processing failed with err %d\n",
					err);
		}
		rocker_desc_gen_clear(desc_info);
		rocker_desc_head_set(rocker, &rocker->event_ring, desc_info);
		credits++;
	}
	rocker_dma_ring_credits_set(rocker, &rocker->event_ring, credits);

	return IRQ_HANDLED;
}

static irqreturn_t rocker_tx_irq_handler(int irq, void *dev_id)
{
	struct rocker_port *rocker_port = dev_id;

	napi_schedule(&rocker_port->napi_tx);
	return IRQ_HANDLED;
}

static irqreturn_t rocker_rx_irq_handler(int irq, void *dev_id)
{
	struct rocker_port *rocker_port = dev_id;

	napi_schedule(&rocker_port->napi_rx);
	return IRQ_HANDLED;
}

/********************
 * Command interface
 ********************/

typedef int (*rocker_cmd_cb_t)(struct rocker *rocker,
			       struct rocker_port *rocker_port,
			       struct rocker_desc_info *desc_info,
			       void *priv);

static int rocker_cmd_exec(struct rocker *rocker,
			   struct rocker_port *rocker_port,
			   rocker_cmd_cb_t prepare, void *prepare_priv,
			   rocker_cmd_cb_t process, void *process_priv,
			   bool nowait)
{
	struct rocker_desc_info *desc_info;
	struct rocker_wait *wait;
	unsigned long flags;
	int err;

	wait = rocker_wait_create(nowait ? GFP_ATOMIC : GFP_KERNEL);
	if (!wait)
		return -ENOMEM;
	wait->nowait = nowait;

	spin_lock_irqsave(&rocker->cmd_ring_lock, flags);
	desc_info = rocker_desc_head_get(&rocker->cmd_ring);
	if (!desc_info) {
		spin_unlock_irqrestore(&rocker->cmd_ring_lock, flags);
		err = -EAGAIN;
		goto out;
	}
	err = prepare(rocker, rocker_port, desc_info, prepare_priv);
	if (err) {
		spin_unlock_irqrestore(&rocker->cmd_ring_lock, flags);
		goto out;
	}
	rocker_desc_cookie_ptr_set(desc_info, wait);
	rocker_desc_head_set(rocker, &rocker->cmd_ring, desc_info);
	spin_unlock_irqrestore(&rocker->cmd_ring_lock, flags);

	if (nowait)
		return 0;

	if (!rocker_wait_event_timeout(wait, HZ / 10))
		return -EIO;

	err = rocker_desc_err(desc_info);
	if (err)
		return err;

	if (process)
		err = process(rocker, rocker_port, desc_info, process_priv);

	rocker_desc_gen_clear(desc_info);
out:
	rocker_wait_destroy(wait);
	return err;
}

static int
rocker_cmd_get_port_settings_prep(struct rocker *rocker,
				  struct rocker_port *rocker_port,
				  struct rocker_desc_info *desc_info,
				  void *priv)
{
	struct rocker_tlv *cmd_info;

	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_CMD_TYPE,
			       ROCKER_TLV_CMD_TYPE_GET_PORT_SETTINGS))
		return -EMSGSIZE;
	cmd_info = rocker_tlv_nest_start(desc_info, ROCKER_TLV_CMD_INFO);
	if (!cmd_info)
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_LPORT,
			       rocker_port->lport))
		return -EMSGSIZE;
	rocker_tlv_nest_end(desc_info, cmd_info);
	return 0;
}

static int
rocker_cmd_get_port_settings_ethtool_proc(struct rocker *rocker,
					  struct rocker_port *rocker_port,
					  struct rocker_desc_info *desc_info,
					  void *priv)
{
	struct ethtool_cmd *ecmd = priv;
	struct rocker_tlv *attrs[ROCKER_TLV_CMD_MAX + 1];
	struct rocker_tlv *info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_MAX + 1];
	u32 speed;
	u8 duplex;
	u8 autoneg;

	rocker_tlv_parse_desc(attrs, ROCKER_TLV_CMD_MAX, desc_info);
	if (!attrs[ROCKER_TLV_CMD_INFO])
		return -EIO;

	rocker_tlv_parse_nested(info_attrs, ROCKER_TLV_CMD_PORT_SETTINGS_MAX,
				attrs[ROCKER_TLV_CMD_INFO]);
	if (!info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_SPEED] ||
	    !info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_DUPLEX] ||
	    !info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_AUTONEG])
		return -EIO;

	speed = rocker_tlv_get_u32(info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_SPEED]);
	duplex = rocker_tlv_get_u8(info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_DUPLEX]);
	autoneg = rocker_tlv_get_u8(info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_AUTONEG]);

	ecmd->transceiver = XCVR_INTERNAL;
	ecmd->supported = SUPPORTED_TP;
	ecmd->phy_address = 0xff;
	ecmd->port = PORT_TP;
	ethtool_cmd_speed_set(ecmd, speed);
	ecmd->duplex = duplex ? DUPLEX_FULL : DUPLEX_HALF;
	ecmd->autoneg = autoneg ? AUTONEG_ENABLE : AUTONEG_DISABLE;

	return 0;
}

static int
rocker_cmd_get_port_settings_macaddr_proc(struct rocker *rocker,
					  struct rocker_port *rocker_port,
					  struct rocker_desc_info *desc_info,
					  void *priv)
{
	unsigned char *macaddr = priv;
	struct rocker_tlv *attrs[ROCKER_TLV_CMD_MAX + 1];
	struct rocker_tlv *info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_MAX + 1];
	struct rocker_tlv *attr;

	rocker_tlv_parse_desc(attrs, ROCKER_TLV_CMD_MAX, desc_info);
	if (!attrs[ROCKER_TLV_CMD_INFO])
		return -EIO;

	rocker_tlv_parse_nested(info_attrs, ROCKER_TLV_CMD_PORT_SETTINGS_MAX,
				attrs[ROCKER_TLV_CMD_INFO]);
	attr = info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_MACADDR];
	if (!attr)
		return -EIO;

	if (rocker_tlv_len(attr) != ETH_ALEN)
		return -EINVAL;

	ether_addr_copy(macaddr, rocker_tlv_data(attr));
	return 0;
}

static int
rocker_cmd_set_port_settings_ethtool_prep(struct rocker *rocker,
					  struct rocker_port *rocker_port,
					  struct rocker_desc_info *desc_info,
					  void *priv)
{
	struct ethtool_cmd *ecmd = priv;
	struct rocker_tlv *cmd_info;

	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_CMD_TYPE,
			       ROCKER_TLV_CMD_TYPE_SET_PORT_SETTINGS))
		return -EMSGSIZE;
	cmd_info = rocker_tlv_nest_start(desc_info, ROCKER_TLV_CMD_INFO);
	if (!cmd_info)
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_LPORT,
			       rocker_port->lport))
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_SPEED,
			       ethtool_cmd_speed(ecmd)))
		return -EMSGSIZE;
	if (rocker_tlv_put_u8(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_DUPLEX,
			      ecmd->duplex))
		return -EMSGSIZE;
	if (rocker_tlv_put_u8(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_AUTONEG,
			      ecmd->autoneg))
		return -EMSGSIZE;
	rocker_tlv_nest_end(desc_info, cmd_info);
	return 0;
}

static int
rocker_cmd_set_port_settings_macaddr_prep(struct rocker *rocker,
					  struct rocker_port *rocker_port,
					  struct rocker_desc_info *desc_info,
					  void *priv)
{
	unsigned char *macaddr = priv;
	struct rocker_tlv *cmd_info;

	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_CMD_TYPE,
			       ROCKER_TLV_CMD_TYPE_SET_PORT_SETTINGS))
		return -EMSGSIZE;
	cmd_info = rocker_tlv_nest_start(desc_info, ROCKER_TLV_CMD_INFO);
	if (!cmd_info)
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_LPORT,
			       rocker_port->lport))
		return -EMSGSIZE;
	if (rocker_tlv_put(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_MACADDR,
			   ETH_ALEN, macaddr))
		return -EMSGSIZE;
	rocker_tlv_nest_end(desc_info, cmd_info);
	return 0;
}

static int
rocker_cmd_set_port_learning_prep(struct rocker *rocker,
				  struct rocker_port *rocker_port,
				  struct rocker_desc_info *desc_info,
				  void *priv)
{
	struct rocker_tlv *cmd_info;

	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_CMD_TYPE,
			       ROCKER_TLV_CMD_TYPE_SET_PORT_SETTINGS))
		return -EMSGSIZE;
	cmd_info = rocker_tlv_nest_start(desc_info, ROCKER_TLV_CMD_INFO);
	if (!cmd_info)
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_LPORT,
			       rocker_port->lport))
		return -EMSGSIZE;
	if (rocker_tlv_put_u8(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_LEARNING,
			      !!(rocker_port->brport_flags & BR_LEARNING)))
		return -EMSGSIZE;
	rocker_tlv_nest_end(desc_info, cmd_info);
	return 0;
}

static int rocker_cmd_get_port_settings_ethtool(struct rocker_port *rocker_port,
						struct ethtool_cmd *ecmd)
{
	return rocker_cmd_exec(rocker_port->rocker, rocker_port,
			       rocker_cmd_get_port_settings_prep, NULL,
			       rocker_cmd_get_port_settings_ethtool_proc,
			       ecmd, false);
}

static int rocker_cmd_get_port_settings_macaddr(struct rocker_port *rocker_port,
						unsigned char *macaddr)
{
	return rocker_cmd_exec(rocker_port->rocker, rocker_port,
			       rocker_cmd_get_port_settings_prep, NULL,
			       rocker_cmd_get_port_settings_macaddr_proc,
			       macaddr, false);
}

static int rocker_cmd_set_port_settings_ethtool(struct rocker_port *rocker_port,
						struct ethtool_cmd *ecmd)
{
	return rocker_cmd_exec(rocker_port->rocker, rocker_port,
			       rocker_cmd_set_port_settings_ethtool_prep,
			       ecmd, NULL, NULL, false);
}

static int rocker_cmd_set_port_settings_macaddr(struct rocker_port *rocker_port,
						unsigned char *macaddr)
{
	return rocker_cmd_exec(rocker_port->rocker, rocker_port,
			       rocker_cmd_set_port_settings_macaddr_prep,
			       macaddr, NULL, NULL, false);
}

static int rocker_port_set_learning(struct rocker_port *rocker_port)
{
	return rocker_cmd_exec(rocker_port->rocker, rocker_port,
			       rocker_cmd_set_port_learning_prep,
			       NULL, NULL, NULL, false);
}

static int rocker_cmd_flow_tbl_add_ig_port(struct rocker_desc_info *desc_info,
					   struct rocker_flow_tbl_entry *entry)
{
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_IN_LPORT,
			       entry->key.ig_port.in_lport))
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_IN_LPORT_MASK,
			       entry->key.ig_port.in_lport_mask))
		return -EMSGSIZE;
	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_OF_DPA_GOTO_TABLE_ID,
			       entry->key.ig_port.goto_tbl))
		return -EMSGSIZE;

	return 0;
}

static int rocker_cmd_flow_tbl_add_vlan(struct rocker_desc_info *desc_info,
					struct rocker_flow_tbl_entry *entry)
{
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_IN_LPORT,
			       entry->key.vlan.in_lport))
		return -EMSGSIZE;
	if (rocker_tlv_put_be16(desc_info, ROCKER_TLV_OF_DPA_VLAN_ID,
				entry->key.vlan.vlan_id))
		return -EMSGSIZE;
	if (rocker_tlv_put_be16(desc_info, ROCKER_TLV_OF_DPA_VLAN_ID_MASK,
				entry->key.vlan.vlan_id_mask))
		return -EMSGSIZE;
	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_OF_DPA_GOTO_TABLE_ID,
			       entry->key.vlan.goto_tbl))
		return -EMSGSIZE;
	if (entry->key.vlan.untagged &&
	    rocker_tlv_put_be16(desc_info, ROCKER_TLV_OF_DPA_NEW_VLAN_ID,
				entry->key.vlan.new_vlan_id))
		return -EMSGSIZE;

	return 0;
}

static int rocker_cmd_flow_tbl_add_term_mac(struct rocker_desc_info *desc_info,
					    struct rocker_flow_tbl_entry *entry)
{
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_IN_LPORT,
			       entry->key.term_mac.in_lport))
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_IN_LPORT_MASK,
			       entry->key.term_mac.in_lport_mask))
		return -EMSGSIZE;
	if (rocker_tlv_put_be16(desc_info, ROCKER_TLV_OF_DPA_ETHERTYPE,
				entry->key.term_mac.eth_type))
		return -EMSGSIZE;
	if (rocker_tlv_put(desc_info, ROCKER_TLV_OF_DPA_DST_MAC,
			   ETH_ALEN, entry->key.term_mac.eth_dst))
		return -EMSGSIZE;
	if (rocker_tlv_put(desc_info, ROCKER_TLV_OF_DPA_DST_MAC_MASK,
			   ETH_ALEN, entry->key.term_mac.eth_dst_mask))
		return -EMSGSIZE;
	if (rocker_tlv_put_be16(desc_info, ROCKER_TLV_OF_DPA_VLAN_ID,
				entry->key.term_mac.vlan_id))
		return -EMSGSIZE;
	if (rocker_tlv_put_be16(desc_info, ROCKER_TLV_OF_DPA_VLAN_ID_MASK,
				entry->key.term_mac.vlan_id_mask))
		return -EMSGSIZE;
	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_OF_DPA_GOTO_TABLE_ID,
			       entry->key.term_mac.goto_tbl))
		return -EMSGSIZE;
	if (entry->key.term_mac.copy_to_cpu &&
	    rocker_tlv_put_u8(desc_info, ROCKER_TLV_OF_DPA_COPY_CPU_ACTION,
			      entry->key.term_mac.copy_to_cpu))
		return -EMSGSIZE;

	return 0;
}

static int
rocker_cmd_flow_tbl_add_ucast_routing(struct rocker_desc_info *desc_info,
				      struct rocker_flow_tbl_entry *entry)
{
	if (rocker_tlv_put_be16(desc_info, ROCKER_TLV_OF_DPA_ETHERTYPE,
				entry->key.ucast_routing.eth_type))
		return -EMSGSIZE;
	if (rocker_tlv_put_be32(desc_info, ROCKER_TLV_OF_DPA_DST_IP,
				entry->key.ucast_routing.dst4))
		return -EMSGSIZE;
	if (rocker_tlv_put_be32(desc_info, ROCKER_TLV_OF_DPA_DST_IP_MASK,
				entry->key.ucast_routing.dst4_mask))
		return -EMSGSIZE;
	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_OF_DPA_GOTO_TABLE_ID,
			       entry->key.ucast_routing.goto_tbl))
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_GROUP_ID,
			       entry->key.ucast_routing.group_id))
		return -EMSGSIZE;

	return 0;
}

static int rocker_cmd_flow_tbl_add_bridge(struct rocker_desc_info *desc_info,
					  struct rocker_flow_tbl_entry *entry)
{
	if (entry->key.bridge.has_eth_dst &&
	    rocker_tlv_put(desc_info, ROCKER_TLV_OF_DPA_DST_MAC,
			   ETH_ALEN, entry->key.bridge.eth_dst))
		return -EMSGSIZE;
	if (entry->key.bridge.has_eth_dst_mask &&
	    rocker_tlv_put(desc_info, ROCKER_TLV_OF_DPA_DST_MAC_MASK,
			   ETH_ALEN, entry->key.bridge.eth_dst_mask))
		return -EMSGSIZE;
	if (entry->key.bridge.vlan_id &&
	    rocker_tlv_put_be16(desc_info, ROCKER_TLV_OF_DPA_VLAN_ID,
				entry->key.bridge.vlan_id))
		return -EMSGSIZE;
	if (entry->key.bridge.tunnel_id &&
	    rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_TUNNEL_ID,
			       entry->key.bridge.tunnel_id))
		return -EMSGSIZE;
	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_OF_DPA_GOTO_TABLE_ID,
			       entry->key.bridge.goto_tbl))
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_GROUP_ID,
			       entry->key.bridge.group_id))
		return -EMSGSIZE;
	if (entry->key.bridge.copy_to_cpu &&
	    rocker_tlv_put_u8(desc_info, ROCKER_TLV_OF_DPA_COPY_CPU_ACTION,
			      entry->key.bridge.copy_to_cpu))
		return -EMSGSIZE;

	return 0;
}

static int rocker_cmd_flow_tbl_add_acl(struct rocker_desc_info *desc_info,
				       struct rocker_flow_tbl_entry *entry)
{
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_IN_LPORT,
			       entry->key.acl.in_lport))
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_IN_LPORT_MASK,
			       entry->key.acl.in_lport_mask))
		return -EMSGSIZE;
	if (rocker_tlv_put(desc_info, ROCKER_TLV_OF_DPA_SRC_MAC,
			   ETH_ALEN, entry->key.acl.eth_src))
		return -EMSGSIZE;
	if (rocker_tlv_put(desc_info, ROCKER_TLV_OF_DPA_SRC_MAC_MASK,
			   ETH_ALEN, entry->key.acl.eth_src_mask))
		return -EMSGSIZE;
	if (rocker_tlv_put(desc_info, ROCKER_TLV_OF_DPA_DST_MAC,
			   ETH_ALEN, entry->key.acl.eth_dst))
		return -EMSGSIZE;
	if (rocker_tlv_put(desc_info, ROCKER_TLV_OF_DPA_DST_MAC_MASK,
			   ETH_ALEN, entry->key.acl.eth_dst_mask))
		return -EMSGSIZE;
	if (rocker_tlv_put_be16(desc_info, ROCKER_TLV_OF_DPA_ETHERTYPE,
				entry->key.acl.eth_type))
		return -EMSGSIZE;
	if (rocker_tlv_put_be16(desc_info, ROCKER_TLV_OF_DPA_VLAN_ID,
				entry->key.acl.vlan_id))
		return -EMSGSIZE;
	if (rocker_tlv_put_be16(desc_info, ROCKER_TLV_OF_DPA_VLAN_ID_MASK,
				entry->key.acl.vlan_id_mask))
		return -EMSGSIZE;

	switch (ntohs(entry->key.acl.eth_type)) {
	case ETH_P_IP:
	case ETH_P_IPV6:
		if (rocker_tlv_put_u8(desc_info, ROCKER_TLV_OF_DPA_IP_PROTO,
				      entry->key.acl.ip_proto))
			return -EMSGSIZE;
		if (rocker_tlv_put_u8(desc_info,
				      ROCKER_TLV_OF_DPA_IP_PROTO_MASK,
				      entry->key.acl.ip_proto_mask))
			return -EMSGSIZE;
		if (rocker_tlv_put_u8(desc_info, ROCKER_TLV_OF_DPA_IP_DSCP,
				      entry->key.acl.ip_tos & 0x3f))
			return -EMSGSIZE;
		if (rocker_tlv_put_u8(desc_info,
				      ROCKER_TLV_OF_DPA_IP_DSCP_MASK,
				      entry->key.acl.ip_tos_mask & 0x3f))
			return -EMSGSIZE;
		if (rocker_tlv_put_u8(desc_info, ROCKER_TLV_OF_DPA_IP_ECN,
				      (entry->key.acl.ip_tos & 0xc0) >> 6))
			return -EMSGSIZE;
		if (rocker_tlv_put_u8(desc_info,
				      ROCKER_TLV_OF_DPA_IP_ECN_MASK,
				      (entry->key.acl.ip_tos_mask & 0xc0) >> 6))
			return -EMSGSIZE;
		break;
	}

	if (entry->key.acl.group_id != ROCKER_GROUP_NONE &&
	    rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_GROUP_ID,
			       entry->key.acl.group_id))
		return -EMSGSIZE;

	return 0;
}

static int rocker_cmd_flow_tbl_add(struct rocker *rocker,
				   struct rocker_port *rocker_port,
				   struct rocker_desc_info *desc_info,
				   void *priv)
{
	struct rocker_flow_tbl_entry *entry = priv;
	struct rocker_tlv *cmd_info;
	int err = 0;

	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_CMD_TYPE,
			       ROCKER_TLV_CMD_TYPE_OF_DPA_FLOW_ADD))
		return -EMSGSIZE;
	cmd_info = rocker_tlv_nest_start(desc_info, ROCKER_TLV_CMD_INFO);
	if (!cmd_info)
		return -EMSGSIZE;
	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_OF_DPA_TABLE_ID,
			       entry->key.tbl_id))
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_PRIORITY,
			       entry->key.priority))
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_HARDTIME, 0))
		return -EMSGSIZE;
	if (rocker_tlv_put_u64(desc_info, ROCKER_TLV_OF_DPA_COOKIE,
			       entry->cookie))
		return -EMSGSIZE;

	switch (entry->key.tbl_id) {
	case ROCKER_OF_DPA_TABLE_ID_INGRESS_PORT:
		err = rocker_cmd_flow_tbl_add_ig_port(desc_info, entry);
		break;
	case ROCKER_OF_DPA_TABLE_ID_VLAN:
		err = rocker_cmd_flow_tbl_add_vlan(desc_info, entry);
		break;
	case ROCKER_OF_DPA_TABLE_ID_TERMINATION_MAC:
		err = rocker_cmd_flow_tbl_add_term_mac(desc_info, entry);
		break;
	case ROCKER_OF_DPA_TABLE_ID_UNICAST_ROUTING:
		err = rocker_cmd_flow_tbl_add_ucast_routing(desc_info, entry);
		break;
	case ROCKER_OF_DPA_TABLE_ID_BRIDGING:
		err = rocker_cmd_flow_tbl_add_bridge(desc_info, entry);
		break;
	case ROCKER_OF_DPA_TABLE_ID_ACL_POLICY:
		err = rocker_cmd_flow_tbl_add_acl(desc_info, entry);
		break;
	default:
		err = -ENOTSUPP;
		break;
	}

	if (err)
		return err;

	rocker_tlv_nest_end(desc_info, cmd_info);

	return 0;
}

static int rocker_cmd_flow_tbl_del(struct rocker *rocker,
				   struct rocker_port *rocker_port,
				   struct rocker_desc_info *desc_info,
				   void *priv)
{
	const struct rocker_flow_tbl_entry *entry = priv;
	struct rocker_tlv *cmd_info;

	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_CMD_TYPE,
			       ROCKER_TLV_CMD_TYPE_OF_DPA_FLOW_DEL))
		return -EMSGSIZE;
	cmd_info = rocker_tlv_nest_start(desc_info, ROCKER_TLV_CMD_INFO);
	if (!cmd_info)
		return -EMSGSIZE;
	if (rocker_tlv_put_u64(desc_info, ROCKER_TLV_OF_DPA_COOKIE,
			       entry->cookie))
		return -EMSGSIZE;
	rocker_tlv_nest_end(desc_info, cmd_info);

	return 0;
}

static int
rocker_cmd_group_tbl_add_l2_interface(struct rocker_desc_info *desc_info,
				      struct rocker_group_tbl_entry *entry)
{
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_OUT_LPORT,
			       ROCKER_GROUP_PORT_GET(entry->group_id)))
		return -EMSGSIZE;
	if (rocker_tlv_put_u8(desc_info, ROCKER_TLV_OF_DPA_POP_VLAN,
			      entry->l2_interface.pop_vlan))
		return -EMSGSIZE;

	return 0;
}

static int
rocker_cmd_group_tbl_add_l2_rewrite(struct rocker_desc_info *desc_info,
				    struct rocker_group_tbl_entry *entry)
{
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_GROUP_ID_LOWER,
			       entry->l2_rewrite.group_id))
		return -EMSGSIZE;
	if (!is_zero_ether_addr(entry->l2_rewrite.eth_src) &&
	    rocker_tlv_put(desc_info, ROCKER_TLV_OF_DPA_SRC_MAC,
			   ETH_ALEN, entry->l2_rewrite.eth_src))
		return -EMSGSIZE;
	if (!is_zero_ether_addr(entry->l2_rewrite.eth_dst) &&
	    rocker_tlv_put(desc_info, ROCKER_TLV_OF_DPA_DST_MAC,
			   ETH_ALEN, entry->l2_rewrite.eth_dst))
		return -EMSGSIZE;
	if (entry->l2_rewrite.vlan_id &&
	    rocker_tlv_put_be16(desc_info, ROCKER_TLV_OF_DPA_VLAN_ID,
				entry->l2_rewrite.vlan_id))
		return -EMSGSIZE;

	return 0;
}

static int
rocker_cmd_group_tbl_add_group_ids(struct rocker_desc_info *desc_info,
				   struct rocker_group_tbl_entry *entry)
{
	int i;
	struct rocker_tlv *group_ids;

	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_OF_DPA_GROUP_COUNT,
			       entry->group_count))
		return -EMSGSIZE;

	group_ids = rocker_tlv_nest_start(desc_info,
					  ROCKER_TLV_OF_DPA_GROUP_IDS);
	if (!group_ids)
		return -EMSGSIZE;

	for (i = 0; i < entry->group_count; i++)
		/* Note TLV array is 1-based */
		if (rocker_tlv_put_u32(desc_info, i + 1, entry->group_ids[i]))
			return -EMSGSIZE;

	rocker_tlv_nest_end(desc_info, group_ids);

	return 0;
}

static int
rocker_cmd_group_tbl_add_l3_unicast(struct rocker_desc_info *desc_info,
				    struct rocker_group_tbl_entry *entry)
{
	if (!is_zero_ether_addr(entry->l3_unicast.eth_src) &&
	    rocker_tlv_put(desc_info, ROCKER_TLV_OF_DPA_SRC_MAC,
			   ETH_ALEN, entry->l3_unicast.eth_src))
		return -EMSGSIZE;
	if (!is_zero_ether_addr(entry->l3_unicast.eth_dst) &&
	    rocker_tlv_put(desc_info, ROCKER_TLV_OF_DPA_DST_MAC,
			   ETH_ALEN, entry->l3_unicast.eth_dst))
		return -EMSGSIZE;
	if (entry->l3_unicast.vlan_id &&
	    rocker_tlv_put_be16(desc_info, ROCKER_TLV_OF_DPA_VLAN_ID,
				entry->l3_unicast.vlan_id))
		return -EMSGSIZE;
	if (rocker_tlv_put_u8(desc_info, ROCKER_TLV_OF_DPA_TTL_CHECK,
			      entry->l3_unicast.ttl_check))
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_GROUP_ID_LOWER,
			       entry->l3_unicast.group_id))
		return -EMSGSIZE;

	return 0;
}

static int rocker_cmd_group_tbl_add(struct rocker *rocker,
				    struct rocker_port *rocker_port,
				    struct rocker_desc_info *desc_info,
				    void *priv)
{
	struct rocker_group_tbl_entry *entry = priv;
	struct rocker_tlv *cmd_info;
	int err = 0;

	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_CMD_TYPE, entry->cmd))
		return -EMSGSIZE;
	cmd_info = rocker_tlv_nest_start(desc_info, ROCKER_TLV_CMD_INFO);
	if (!cmd_info)
		return -EMSGSIZE;

	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_GROUP_ID,
			       entry->group_id))
		return -EMSGSIZE;

	switch (ROCKER_GROUP_TYPE_GET(entry->group_id)) {
	case ROCKER_OF_DPA_GROUP_TYPE_L2_INTERFACE:
		err = rocker_cmd_group_tbl_add_l2_interface(desc_info, entry);
		break;
	case ROCKER_OF_DPA_GROUP_TYPE_L2_REWRITE:
		err = rocker_cmd_group_tbl_add_l2_rewrite(desc_info, entry);
		break;
	case ROCKER_OF_DPA_GROUP_TYPE_L2_FLOOD:
	case ROCKER_OF_DPA_GROUP_TYPE_L2_MCAST:
		err = rocker_cmd_group_tbl_add_group_ids(desc_info, entry);
		break;
	case ROCKER_OF_DPA_GROUP_TYPE_L3_UCAST:
		err = rocker_cmd_group_tbl_add_l3_unicast(desc_info, entry);
		break;
	default:
		err = -ENOTSUPP;
		break;
	}

	if (err)
		return err;

	rocker_tlv_nest_end(desc_info, cmd_info);

	return 0;
}

static int rocker_cmd_group_tbl_del(struct rocker *rocker,
				    struct rocker_port *rocker_port,
				    struct rocker_desc_info *desc_info,
				    void *priv)
{
	const struct rocker_group_tbl_entry *entry = priv;
	struct rocker_tlv *cmd_info;

	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_CMD_TYPE, entry->cmd))
		return -EMSGSIZE;
	cmd_info = rocker_tlv_nest_start(desc_info, ROCKER_TLV_CMD_INFO);
	if (!cmd_info)
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_OF_DPA_GROUP_ID,
			       entry->group_id))
		return -EMSGSIZE;
	rocker_tlv_nest_end(desc_info, cmd_info);

	return 0;
}

/*****************************************
 * Flow, group, FDB, internal VLAN tables
 *****************************************/

static int rocker_init_tbls(struct rocker *rocker)
{
	hash_init(rocker->flow_tbl);
	spin_lock_init(&rocker->flow_tbl_lock);

	hash_init(rocker->group_tbl);
	spin_lock_init(&rocker->group_tbl_lock);

	hash_init(rocker->fdb_tbl);
	spin_lock_init(&rocker->fdb_tbl_lock);

	hash_init(rocker->internal_vlan_tbl);
	spin_lock_init(&rocker->internal_vlan_tbl_lock);

	return 0;
}

static void rocker_free_tbls(struct rocker *rocker)
{
	unsigned long flags;
	struct rocker_flow_tbl_entry *flow_entry;
	struct rocker_group_tbl_entry *group_entry;
	struct rocker_fdb_tbl_entry *fdb_entry;
	struct rocker_internal_vlan_tbl_entry *internal_vlan_entry;
	struct hlist_node *tmp;
	int bkt;

	spin_lock_irqsave(&rocker->flow_tbl_lock, flags);
	hash_for_each_safe(rocker->flow_tbl, bkt, tmp, flow_entry, entry)
		hash_del(&flow_entry->entry);
	spin_unlock_irqrestore(&rocker->flow_tbl_lock, flags);

	spin_lock_irqsave(&rocker->group_tbl_lock, flags);
	hash_for_each_safe(rocker->group_tbl, bkt, tmp, group_entry, entry)
		hash_del(&group_entry->entry);
	spin_unlock_irqrestore(&rocker->group_tbl_lock, flags);

	spin_lock_irqsave(&rocker->fdb_tbl_lock, flags);
	hash_for_each_safe(rocker->fdb_tbl, bkt, tmp, fdb_entry, entry)
		hash_del(&fdb_entry->entry);
	spin_unlock_irqrestore(&rocker->fdb_tbl_lock, flags);

	spin_lock_irqsave(&rocker->internal_vlan_tbl_lock, flags);
	hash_for_each_safe(rocker->internal_vlan_tbl, bkt,
			   tmp, internal_vlan_entry, entry)
		hash_del(&internal_vlan_entry->entry);
	spin_unlock_irqrestore(&rocker->internal_vlan_tbl_lock, flags);
}

static struct rocker_flow_tbl_entry *
rocker_flow_tbl_find(struct rocker *rocker, struct rocker_flow_tbl_entry *match)
{
	struct rocker_flow_tbl_entry *found;

	hash_for_each_possible(rocker->flow_tbl, found,
			       entry, match->key_crc32) {
		if (memcmp(&found->key, &match->key, sizeof(found->key)) == 0)
			return found;
	}

	return NULL;
}

static int rocker_flow_tbl_add(struct rocker_port *rocker_port,
			       struct rocker_flow_tbl_entry *match,
			       bool nowait)
{
	struct rocker *rocker = rocker_port->rocker;
	struct rocker_flow_tbl_entry *found;
	unsigned long flags;
	bool add_to_hw = false;
	int err = 0;

	match->key_crc32 = crc32(~0, &match->key, sizeof(match->key));

	spin_lock_irqsave(&rocker->flow_tbl_lock, flags);

	found = rocker_flow_tbl_find(rocker, match);

	if (found) {
		kfree(match);
	} else {
		found = match;
		found->cookie = rocker->flow_tbl_next_cookie++;
		hash_add(rocker->flow_tbl, &found->entry, found->key_crc32);
		add_to_hw = true;
	}

	found->ref_count++;

	spin_unlock_irqrestore(&rocker->flow_tbl_lock, flags);

	if (add_to_hw) {
		err = rocker_cmd_exec(rocker, rocker_port,
				      rocker_cmd_flow_tbl_add,
				      found, NULL, NULL, nowait);
		if (err) {
			spin_lock_irqsave(&rocker->flow_tbl_lock, flags);
			hash_del(&found->entry);
			spin_unlock_irqrestore(&rocker->flow_tbl_lock, flags);
			kfree(found);
		}
	}

	return err;
}

static int rocker_flow_tbl_del(struct rocker_port *rocker_port,
			       struct rocker_flow_tbl_entry *match,
			       bool nowait)
{
	struct rocker *rocker = rocker_port->rocker;
	struct rocker_flow_tbl_entry *found;
	unsigned long flags;
	bool del_from_hw = false;
	int err = 0;

	match->key_crc32 = crc32(~0, &match->key, sizeof(match->key));

	spin_lock_irqsave(&rocker->flow_tbl_lock, flags);

	found = rocker_flow_tbl_find(rocker, match);

	if (found) {
		found->ref_count--;
		if (found->ref_count == 0) {
			hash_del(&found->entry);
			del_from_hw = true;
		}
	}

	spin_unlock_irqrestore(&rocker->flow_tbl_lock, flags);

	kfree(match);

	if (del_from_hw) {
		err = rocker_cmd_exec(rocker, rocker_port,
				      rocker_cmd_flow_tbl_del,
				      found, NULL, NULL, nowait);
		kfree(found);
	}

	return err;
}

static gfp_t rocker_op_flags_gfp(int flags)
{
	return flags & ROCKER_OP_FLAG_NOWAIT ? GFP_ATOMIC : GFP_KERNEL;
}

static int rocker_flow_tbl_do(struct rocker_port *rocker_port,
			      int flags, struct rocker_flow_tbl_entry *entry)
{
	bool nowait = flags & ROCKER_OP_FLAG_NOWAIT;

	if (flags & ROCKER_OP_FLAG_REMOVE)
		return rocker_flow_tbl_del(rocker_port, entry, nowait);
	else
		return rocker_flow_tbl_add(rocker_port, entry, nowait);
}

static int rocker_flow_tbl_ig_port(struct rocker_port *rocker_port,
				   int flags, u32 in_lport, u32 in_lport_mask,
				   enum rocker_of_dpa_table_id goto_tbl)
{
	struct rocker_flow_tbl_entry *entry;

	entry = kzalloc(sizeof(*entry), rocker_op_flags_gfp(flags));
	if (!entry)
		return -ENOMEM;

	entry->key.priority = ROCKER_PRIORITY_IG_PORT;
	entry->key.tbl_id = ROCKER_OF_DPA_TABLE_ID_INGRESS_PORT;
	entry->key.ig_port.in_lport = in_lport;
	entry->key.ig_port.in_lport_mask = in_lport_mask;
	entry->key.ig_port.goto_tbl = goto_tbl;

	return rocker_flow_tbl_do(rocker_port, flags, entry);
}

static int rocker_flow_tbl_vlan(struct rocker_port *rocker_port,
				int flags, u32 in_lport,
				__be16 vlan_id, __be16 vlan_id_mask,
				enum rocker_of_dpa_table_id goto_tbl,
				bool untagged, __be16 new_vlan_id)
{
	struct rocker_flow_tbl_entry *entry;

	entry = kzalloc(sizeof(*entry), rocker_op_flags_gfp(flags));
	if (!entry)
		return -ENOMEM;

	entry->key.priority = ROCKER_PRIORITY_VLAN;
	entry->key.tbl_id = ROCKER_OF_DPA_TABLE_ID_VLAN;
	entry->key.vlan.in_lport = in_lport;
	entry->key.vlan.vlan_id = vlan_id;
	entry->key.vlan.vlan_id_mask = vlan_id_mask;
	entry->key.vlan.goto_tbl = goto_tbl;

	entry->key.vlan.untagged = untagged;
	entry->key.vlan.new_vlan_id = new_vlan_id;

	return rocker_flow_tbl_do(rocker_port, flags, entry);
}

static int rocker_flow_tbl_term_mac(struct rocker_port *rocker_port,
				    u32 in_lport, u32 in_lport_mask,
				    __be16 eth_type, const u8 *eth_dst,
				    const u8 *eth_dst_mask, __be16 vlan_id,
				    __be16 vlan_id_mask, bool copy_to_cpu,
				    int flags)
{
	struct rocker_flow_tbl_entry *entry;

	entry = kzalloc(sizeof(*entry), rocker_op_flags_gfp(flags));
	if (!entry)
		return -ENOMEM;

	if (is_multicast_ether_addr(eth_dst)) {
		entry->key.priority = ROCKER_PRIORITY_TERM_MAC_MCAST;
		entry->key.term_mac.goto_tbl =
			 ROCKER_OF_DPA_TABLE_ID_MULTICAST_ROUTING;
	} else {
		entry->key.priority = ROCKER_PRIORITY_TERM_MAC_UCAST;
		entry->key.term_mac.goto_tbl =
			 ROCKER_OF_DPA_TABLE_ID_UNICAST_ROUTING;
	}

	entry->key.tbl_id = ROCKER_OF_DPA_TABLE_ID_TERMINATION_MAC;
	entry->key.term_mac.in_lport = in_lport;
	entry->key.term_mac.in_lport_mask = in_lport_mask;
	entry->key.term_mac.eth_type = eth_type;
	ether_addr_copy(entry->key.term_mac.eth_dst, eth_dst);
	ether_addr_copy(entry->key.term_mac.eth_dst_mask, eth_dst_mask);
	entry->key.term_mac.vlan_id = vlan_id;
	entry->key.term_mac.vlan_id_mask = vlan_id_mask;
	entry->key.term_mac.copy_to_cpu = copy_to_cpu;

	return rocker_flow_tbl_do(rocker_port, flags, entry);
}

static int rocker_flow_tbl_bridge(struct rocker_port *rocker_port,
				  int flags,
				  const u8 *eth_dst, const u8 *eth_dst_mask,
				  __be16 vlan_id, u32 tunnel_id,
				  enum rocker_of_dpa_table_id goto_tbl,
				  u32 group_id, bool copy_to_cpu)
{
	struct rocker_flow_tbl_entry *entry;
	u32 priority;
	bool vlan_bridging = !!vlan_id;
	bool dflt = !eth_dst || (eth_dst && eth_dst_mask);
	bool wild = false;

	entry = kzalloc(sizeof(*entry), rocker_op_flags_gfp(flags));
	if (!entry)
		return -ENOMEM;

	entry->key.tbl_id = ROCKER_OF_DPA_TABLE_ID_BRIDGING;

	if (eth_dst) {
		entry->key.bridge.has_eth_dst = 1;
		ether_addr_copy(entry->key.bridge.eth_dst, eth_dst);
	}
	if (eth_dst_mask) {
		entry->key.bridge.has_eth_dst_mask = 1;
		ether_addr_copy(entry->key.bridge.eth_dst_mask, eth_dst_mask);
		if (memcmp(eth_dst_mask, ff_mac, ETH_ALEN))
			wild = true;
	}

	priority = ROCKER_PRIORITY_UNKNOWN;
	if (vlan_bridging && dflt && wild)
		priority = ROCKER_PRIORITY_BRIDGING_VLAN_DFLT_WILD;
	else if (vlan_bridging && dflt && !wild)
		priority = ROCKER_PRIORITY_BRIDGING_VLAN_DFLT_EXACT;
	else if (vlan_bridging && !dflt)
		priority = ROCKER_PRIORITY_BRIDGING_VLAN;
	else if (!vlan_bridging && dflt && wild)
		priority = ROCKER_PRIORITY_BRIDGING_TENANT_DFLT_WILD;
	else if (!vlan_bridging && dflt && !wild)
		priority = ROCKER_PRIORITY_BRIDGING_TENANT_DFLT_EXACT;
	else if (!vlan_bridging && !dflt)
		priority = ROCKER_PRIORITY_BRIDGING_TENANT;

	entry->key.priority = priority;
	entry->key.bridge.vlan_id = vlan_id;
	entry->key.bridge.tunnel_id = tunnel_id;
	entry->key.bridge.goto_tbl = goto_tbl;
	entry->key.bridge.group_id = group_id;
	entry->key.bridge.copy_to_cpu = copy_to_cpu;

	return rocker_flow_tbl_do(rocker_port, flags, entry);
}

static int rocker_flow_tbl_acl(struct rocker_port *rocker_port,
			       int flags, u32 in_lport,
			       u32 in_lport_mask,
			       const u8 *eth_src, const u8 *eth_src_mask,
			       const u8 *eth_dst, const u8 *eth_dst_mask,
			       __be16 eth_type,
			       __be16 vlan_id, __be16 vlan_id_mask,
			       u8 ip_proto, u8 ip_proto_mask,
			       u8 ip_tos, u8 ip_tos_mask,
			       u32 group_id)
{
	u32 priority;
	struct rocker_flow_tbl_entry *entry;

	entry = kzalloc(sizeof(*entry), rocker_op_flags_gfp(flags));
	if (!entry)
		return -ENOMEM;

	priority = ROCKER_PRIORITY_ACL_NORMAL;
	if (eth_dst && eth_dst_mask) {
		if (memcmp(eth_dst_mask, mcast_mac, ETH_ALEN) == 0)
			priority = ROCKER_PRIORITY_ACL_DFLT;
		else if (is_link_local_ether_addr(eth_dst))
			priority = ROCKER_PRIORITY_ACL_CTRL;
	}

	entry->key.priority = priority;
	entry->key.tbl_id = ROCKER_OF_DPA_TABLE_ID_ACL_POLICY;
	entry->key.acl.in_lport = in_lport;
	entry->key.acl.in_lport_mask = in_lport_mask;

	if (eth_src)
		ether_addr_copy(entry->key.acl.eth_src, eth_src);
	if (eth_src_mask)
		ether_addr_copy(entry->key.acl.eth_src_mask, eth_src_mask);
	if (eth_dst)
		ether_addr_copy(entry->key.acl.eth_dst, eth_dst);
	if (eth_dst_mask)
		ether_addr_copy(entry->key.acl.eth_dst_mask, eth_dst_mask);

	entry->key.acl.eth_type = eth_type;
	entry->key.acl.vlan_id = vlan_id;
	entry->key.acl.vlan_id_mask = vlan_id_mask;
	entry->key.acl.ip_proto = ip_proto;
	entry->key.acl.ip_proto_mask = ip_proto_mask;
	entry->key.acl.ip_tos = ip_tos;
	entry->key.acl.ip_tos_mask = ip_tos_mask;
	entry->key.acl.group_id = group_id;

	return rocker_flow_tbl_do(rocker_port, flags, entry);
}

static struct rocker_group_tbl_entry *
rocker_group_tbl_find(struct rocker *rocker,
		      struct rocker_group_tbl_entry *match)
{
	struct rocker_group_tbl_entry *found;

	hash_for_each_possible(rocker->group_tbl, found,
			       entry, match->group_id) {
		if (found->group_id == match->group_id)
			return found;
	}

	return NULL;
}

static void rocker_group_tbl_entry_free(struct rocker_group_tbl_entry *entry)
{
	switch (ROCKER_GROUP_TYPE_GET(entry->group_id)) {
	case ROCKER_OF_DPA_GROUP_TYPE_L2_FLOOD:
	case ROCKER_OF_DPA_GROUP_TYPE_L2_MCAST:
		kfree(entry->group_ids);
		break;
	default:
		break;
	}
	kfree(entry);
}

static int rocker_group_tbl_add(struct rocker_port *rocker_port,
				struct rocker_group_tbl_entry *match,
				bool nowait)
{
	struct rocker *rocker = rocker_port->rocker;
	struct rocker_group_tbl_entry *found;
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&rocker->group_tbl_lock, flags);

	found = rocker_group_tbl_find(rocker, match);

	if (found) {
		hash_del(&found->entry);
		rocker_group_tbl_entry_free(found);
		found = match;
		found->cmd = ROCKER_TLV_CMD_TYPE_OF_DPA_GROUP_MOD;
	} else {
		found = match;
		found->cmd = ROCKER_TLV_CMD_TYPE_OF_DPA_GROUP_ADD;
	}

	hash_add(rocker->group_tbl, &found->entry, found->group_id);

	spin_unlock_irqrestore(&rocker->group_tbl_lock, flags);

	if (found->cmd)
		err = rocker_cmd_exec(rocker, rocker_port,
				      rocker_cmd_group_tbl_add,
				      found, NULL, NULL, nowait);

	return err;
}

static int rocker_group_tbl_del(struct rocker_port *rocker_port,
				struct rocker_group_tbl_entry *match,
				bool nowait)
{
	struct rocker *rocker = rocker_port->rocker;
	struct rocker_group_tbl_entry *found;
	unsigned long flags;
	int err = 0;

	spin_lock_irqsave(&rocker->group_tbl_lock, flags);

	found = rocker_group_tbl_find(rocker, match);

	if (found) {
		hash_del(&found->entry);
		found->cmd = ROCKER_TLV_CMD_TYPE_OF_DPA_GROUP_DEL;
	}

	spin_unlock_irqrestore(&rocker->group_tbl_lock, flags);

	rocker_group_tbl_entry_free(match);

	if (found) {
		err = rocker_cmd_exec(rocker, rocker_port,
				      rocker_cmd_group_tbl_del,
				      found, NULL, NULL, nowait);
		rocker_group_tbl_entry_free(found);
	}

	return err;
}

static int rocker_group_tbl_do(struct rocker_port *rocker_port,
			       int flags, struct rocker_group_tbl_entry *entry)
{
	bool nowait = flags & ROCKER_OP_FLAG_NOWAIT;

	if (flags & ROCKER_OP_FLAG_REMOVE)
		return rocker_group_tbl_del(rocker_port, entry, nowait);
	else
		return rocker_group_tbl_add(rocker_port, entry, nowait);
}

static int rocker_group_l2_interface(struct rocker_port *rocker_port,
				     int flags, __be16 vlan_id,
				     u32 out_lport, int pop_vlan)
{
	struct rocker_group_tbl_entry *entry;

	entry = kzalloc(sizeof(*entry), rocker_op_flags_gfp(flags));
	if (!entry)
		return -ENOMEM;

	entry->group_id = ROCKER_GROUP_L2_INTERFACE(vlan_id, out_lport);
	entry->l2_interface.pop_vlan = pop_vlan;

	return rocker_group_tbl_do(rocker_port, flags, entry);
}

static int rocker_group_l2_fan_out(struct rocker_port *rocker_port,
				   int flags, u8 group_count,
				   u32 *group_ids, u32 group_id)
{
	struct rocker_group_tbl_entry *entry;

	entry = kzalloc(sizeof(*entry), rocker_op_flags_gfp(flags));
	if (!entry)
		return -ENOMEM;

	entry->group_id = group_id;
	entry->group_count = group_count;

	entry->group_ids = kcalloc(group_count, sizeof(u32),
				   rocker_op_flags_gfp(flags));
	if (!entry->group_ids) {
		kfree(entry);
		return -ENOMEM;
	}
	memcpy(entry->group_ids, group_ids, group_count * sizeof(u32));

	return rocker_group_tbl_do(rocker_port, flags, entry);
}

static int rocker_group_l2_flood(struct rocker_port *rocker_port,
				 int flags, __be16 vlan_id,
				 u8 group_count, u32 *group_ids,
				 u32 group_id)
{
	return rocker_group_l2_fan_out(rocker_port, flags,
				       group_count, group_ids,
				       group_id);
}

static int rocker_port_vlan_flood_group(struct rocker_port *rocker_port,
					int flags, __be16 vlan_id)
{
	struct rocker_port *p;
	struct rocker *rocker = rocker_port->rocker;
	u32 group_id = ROCKER_GROUP_L2_FLOOD(vlan_id, 0);
	u32 group_ids[rocker->port_count];
	u8 group_count = 0;
	int err;
	int i;

	/* Adjust the flood group for this VLAN.  The flood group
	 * references an L2 interface group for each port in this
	 * VLAN.
	 */

	for (i = 0; i < rocker->port_count; i++) {
		p = rocker->ports[i];
		if (!rocker_port_is_bridged(p))
			continue;
		if (test_bit(ntohs(vlan_id), p->vlan_bitmap)) {
			group_ids[group_count++] =
				ROCKER_GROUP_L2_INTERFACE(vlan_id,
							  p->lport);
		}
	}

	/* If there are no bridged ports in this VLAN, we're done */
	if (group_count == 0)
		return 0;

	err = rocker_group_l2_flood(rocker_port, flags, vlan_id,
				    group_count, group_ids,
				    group_id);
	if (err)
		netdev_err(rocker_port->dev,
			   "Error (%d) port VLAN l2 flood group\n", err);

	return err;
}

static int rocker_port_vlan_l2_groups(struct rocker_port *rocker_port,
				      int flags, __be16 vlan_id,
				      bool pop_vlan)
{
	struct rocker *rocker = rocker_port->rocker;
	struct rocker_port *p;
	bool adding = !(flags & ROCKER_OP_FLAG_REMOVE);
	u32 out_lport;
	int ref = 0;
	int err;
	int i;

	/* An L2 interface group for this port in this VLAN, but
	 * only when port STP state is LEARNING|FORWARDING.
	 */

	if (rocker_port->stp_state == BR_STATE_LEARNING ||
	    rocker_port->stp_state == BR_STATE_FORWARDING) {
		out_lport = rocker_port->lport;
		err = rocker_group_l2_interface(rocker_port, flags,
						vlan_id, out_lport,
						pop_vlan);
		if (err) {
			netdev_err(rocker_port->dev,
				   "Error (%d) port VLAN l2 group for lport %d\n",
				   err, out_lport);
			return err;
		}
	}

	/* An L2 interface group for this VLAN to CPU port.
	 * Add when first port joins this VLAN and destroy when
	 * last port leaves this VLAN.
	 */

	for (i = 0; i < rocker->port_count; i++) {
		p = rocker->ports[i];
		if (test_bit(ntohs(vlan_id), p->vlan_bitmap))
			ref++;
	}

	if ((!adding || ref != 1) && (adding || ref != 0))
		return 0;

	out_lport = 0;
	err = rocker_group_l2_interface(rocker_port, flags,
					vlan_id, out_lport,
					pop_vlan);
	if (err) {
		netdev_err(rocker_port->dev,
			   "Error (%d) port VLAN l2 group for CPU port\n", err);
		return err;
	}

	return 0;
}

static struct rocker_ctrl {
	const u8 *eth_dst;
	const u8 *eth_dst_mask;
	__be16 eth_type;
	bool acl;
	bool bridge;
	bool term;
	bool copy_to_cpu;
} rocker_ctrls[] = {
	[ROCKER_CTRL_LINK_LOCAL_MCAST] = {
		/* pass link local multicast pkts up to CPU for filtering */
		.eth_dst = ll_mac,
		.eth_dst_mask = ll_mask,
		.acl = true,
	},
	[ROCKER_CTRL_LOCAL_ARP] = {
		/* pass local ARP pkts up to CPU */
		.eth_dst = zero_mac,
		.eth_dst_mask = zero_mac,
		.eth_type = htons(ETH_P_ARP),
		.acl = true,
	},
	[ROCKER_CTRL_IPV4_MCAST] = {
		/* pass IPv4 mcast pkts up to CPU, RFC 1112 */
		.eth_dst = ipv4_mcast,
		.eth_dst_mask = ipv4_mask,
		.eth_type = htons(ETH_P_IP),
		.term  = true,
		.copy_to_cpu = true,
	},
	[ROCKER_CTRL_IPV6_MCAST] = {
		/* pass IPv6 mcast pkts up to CPU, RFC 2464 */
		.eth_dst = ipv6_mcast,
		.eth_dst_mask = ipv6_mask,
		.eth_type = htons(ETH_P_IPV6),
		.term  = true,
		.copy_to_cpu = true,
	},
	[ROCKER_CTRL_DFLT_BRIDGING] = {
		/* flood any pkts on vlan */
		.bridge = true,
		.copy_to_cpu = true,
	},
};

static int rocker_port_ctrl_vlan_acl(struct rocker_port *rocker_port,
				     int flags, struct rocker_ctrl *ctrl,
				     __be16 vlan_id)
{
	u32 in_lport = rocker_port->lport;
	u32 in_lport_mask = 0xffffffff;
	u32 out_lport = 0;
	u8 *eth_src = NULL;
	u8 *eth_src_mask = NULL;
	__be16 vlan_id_mask = htons(0xffff);
	u8 ip_proto = 0;
	u8 ip_proto_mask = 0;
	u8 ip_tos = 0;
	u8 ip_tos_mask = 0;
	u32 group_id = ROCKER_GROUP_L2_INTERFACE(vlan_id, out_lport);
	int err;

	err = rocker_flow_tbl_acl(rocker_port, flags,
				  in_lport, in_lport_mask,
				  eth_src, eth_src_mask,
				  ctrl->eth_dst, ctrl->eth_dst_mask,
				  ctrl->eth_type,
				  vlan_id, vlan_id_mask,
				  ip_proto, ip_proto_mask,
				  ip_tos, ip_tos_mask,
				  group_id);

	if (err)
		netdev_err(rocker_port->dev, "Error (%d) ctrl ACL\n", err);

	return err;
}

static int rocker_port_ctrl_vlan_bridge(struct rocker_port *rocker_port,
					int flags, struct rocker_ctrl *ctrl,
					__be16 vlan_id)
{
	enum rocker_of_dpa_table_id goto_tbl =
		ROCKER_OF_DPA_TABLE_ID_ACL_POLICY;
	u32 group_id = ROCKER_GROUP_L2_FLOOD(vlan_id, 0);
	u32 tunnel_id = 0;
	int err;

	if (!rocker_port_is_bridged(rocker_port))
		return 0;

	err = rocker_flow_tbl_bridge(rocker_port, flags,
				     ctrl->eth_dst, ctrl->eth_dst_mask,
				     vlan_id, tunnel_id,
				     goto_tbl, group_id, ctrl->copy_to_cpu);

	if (err)
		netdev_err(rocker_port->dev, "Error (%d) ctrl FLOOD\n", err);

	return err;
}

static int rocker_port_ctrl_vlan_term(struct rocker_port *rocker_port,
				      int flags, struct rocker_ctrl *ctrl,
				      __be16 vlan_id)
{
	u32 in_lport_mask = 0xffffffff;
	__be16 vlan_id_mask = htons(0xffff);
	int err;

	if (ntohs(vlan_id) == 0)
		vlan_id = rocker_port->internal_vlan_id;

	err = rocker_flow_tbl_term_mac(rocker_port,
				       rocker_port->lport, in_lport_mask,
				       ctrl->eth_type, ctrl->eth_dst,
				       ctrl->eth_dst_mask, vlan_id,
				       vlan_id_mask, ctrl->copy_to_cpu,
				       flags);

	if (err)
		netdev_err(rocker_port->dev, "Error (%d) ctrl term\n", err);

	return err;
}

static int rocker_port_ctrl_vlan(struct rocker_port *rocker_port, int flags,
				 struct rocker_ctrl *ctrl, __be16 vlan_id)
{
	if (ctrl->acl)
		return rocker_port_ctrl_vlan_acl(rocker_port, flags,
						 ctrl, vlan_id);
	if (ctrl->bridge)
		return rocker_port_ctrl_vlan_bridge(rocker_port, flags,
						    ctrl, vlan_id);

	if (ctrl->term)
		return rocker_port_ctrl_vlan_term(rocker_port, flags,
						  ctrl, vlan_id);

	return -EOPNOTSUPP;
}

static int rocker_port_ctrl_vlan_add(struct rocker_port *rocker_port,
				     int flags, __be16 vlan_id)
{
	int err = 0;
	int i;

	for (i = 0; i < ROCKER_CTRL_MAX; i++) {
		if (rocker_port->ctrls[i]) {
			err = rocker_port_ctrl_vlan(rocker_port, flags,
						    &rocker_ctrls[i], vlan_id);
			if (err)
				return err;
		}
	}

	return err;
}

static int rocker_port_ctrl(struct rocker_port *rocker_port, int flags,
			    struct rocker_ctrl *ctrl)
{
	u16 vid;
	int err = 0;

	for (vid = 1; vid < VLAN_N_VID; vid++) {
		if (!test_bit(vid, rocker_port->vlan_bitmap))
			continue;
		err = rocker_port_ctrl_vlan(rocker_port, flags,
					    ctrl, htons(vid));
		if (err)
			break;
	}

	return err;
}

static int rocker_port_vlan(struct rocker_port *rocker_port, int flags,
			    u16 vid)
{
	enum rocker_of_dpa_table_id goto_tbl =
		ROCKER_OF_DPA_TABLE_ID_TERMINATION_MAC;
	u32 in_lport = rocker_port->lport;
	__be16 vlan_id = htons(vid);
	__be16 vlan_id_mask = htons(0xffff);
	__be16 internal_vlan_id;
	bool untagged;
	bool adding = !(flags & ROCKER_OP_FLAG_REMOVE);
	int err;

	internal_vlan_id = rocker_port_vid_to_vlan(rocker_port, vid, &untagged);

	if (adding && test_and_set_bit(ntohs(internal_vlan_id),
				       rocker_port->vlan_bitmap))
			return 0; /* already added */
	else if (!adding && !test_and_clear_bit(ntohs(internal_vlan_id),
						rocker_port->vlan_bitmap))
			return 0; /* already removed */

	if (adding) {
		err = rocker_port_ctrl_vlan_add(rocker_port, flags,
						internal_vlan_id);
		if (err) {
			netdev_err(rocker_port->dev,
				   "Error (%d) port ctrl vlan add\n", err);
			return err;
		}
	}

	err = rocker_port_vlan_l2_groups(rocker_port, flags,
					 internal_vlan_id, untagged);
	if (err) {
		netdev_err(rocker_port->dev,
			   "Error (%d) port VLAN l2 groups\n", err);
		return err;
	}

	err = rocker_port_vlan_flood_group(rocker_port, flags,
					   internal_vlan_id);
	if (err) {
		netdev_err(rocker_port->dev,
			   "Error (%d) port VLAN l2 flood group\n", err);
		return err;
	}

	err = rocker_flow_tbl_vlan(rocker_port, flags,
				   in_lport, vlan_id, vlan_id_mask,
				   goto_tbl, untagged, internal_vlan_id);
	if (err)
		netdev_err(rocker_port->dev,
			   "Error (%d) port VLAN table\n", err);

	return err;
}

static int rocker_port_ig_tbl(struct rocker_port *rocker_port, int flags)
{
	enum rocker_of_dpa_table_id goto_tbl;
	u32 in_lport;
	u32 in_lport_mask;
	int err;

	/* Normal Ethernet Frames.  Matches pkts from any local physical
	 * ports.  Goto VLAN tbl.
	 */

	in_lport = 0;
	in_lport_mask = 0xffff0000;
	goto_tbl = ROCKER_OF_DPA_TABLE_ID_VLAN;

	err = rocker_flow_tbl_ig_port(rocker_port, flags,
				      in_lport, in_lport_mask,
				      goto_tbl);
	if (err)
		netdev_err(rocker_port->dev,
			   "Error (%d) ingress port table entry\n", err);

	return err;
}

struct rocker_fdb_learn_work {
	struct work_struct work;
	struct net_device *dev;
	int flags;
	u8 addr[ETH_ALEN];
	u16 vid;
};

static void rocker_port_fdb_learn_work(struct work_struct *work)
{
	struct rocker_fdb_learn_work *lw =
		container_of(work, struct rocker_fdb_learn_work, work);
	bool removing = (lw->flags & ROCKER_OP_FLAG_REMOVE);
	bool learned = (lw->flags & ROCKER_OP_FLAG_LEARNED);

	if (learned && removing)
		br_fdb_external_learn_del(lw->dev, lw->addr, lw->vid);
	else if (learned && !removing)
		br_fdb_external_learn_add(lw->dev, lw->addr, lw->vid);

	kfree(work);
}

static int rocker_port_fdb_learn(struct rocker_port *rocker_port,
				 int flags, const u8 *addr, __be16 vlan_id)
{
	struct rocker_fdb_learn_work *lw;
	enum rocker_of_dpa_table_id goto_tbl =
		ROCKER_OF_DPA_TABLE_ID_ACL_POLICY;
	u32 out_lport = rocker_port->lport;
	u32 tunnel_id = 0;
	u32 group_id = ROCKER_GROUP_NONE;
	bool syncing = !!(rocker_port->brport_flags & BR_LEARNING_SYNC);
	bool copy_to_cpu = false;
	int err;

	if (rocker_port_is_bridged(rocker_port))
		group_id = ROCKER_GROUP_L2_INTERFACE(vlan_id, out_lport);

	if (!(flags & ROCKER_OP_FLAG_REFRESH)) {
		err = rocker_flow_tbl_bridge(rocker_port, flags, addr, NULL,
					     vlan_id, tunnel_id, goto_tbl,
					     group_id, copy_to_cpu);
		if (err)
			return err;
	}

	if (!syncing)
		return 0;

	if (!rocker_port_is_bridged(rocker_port))
		return 0;

	lw = kmalloc(sizeof(*lw), rocker_op_flags_gfp(flags));
	if (!lw)
		return -ENOMEM;

	INIT_WORK(&lw->work, rocker_port_fdb_learn_work);

	lw->dev = rocker_port->dev;
	lw->flags = flags;
	ether_addr_copy(lw->addr, addr);
	lw->vid = rocker_port_vlan_to_vid(rocker_port, vlan_id);

	schedule_work(&lw->work);

	return 0;
}

static struct rocker_fdb_tbl_entry *
rocker_fdb_tbl_find(struct rocker *rocker, struct rocker_fdb_tbl_entry *match)
{
	struct rocker_fdb_tbl_entry *found;

	hash_for_each_possible(rocker->fdb_tbl, found, entry, match->key_crc32)
		if (memcmp(&found->key, &match->key, sizeof(found->key)) == 0)
			return found;

	return NULL;
}

static int rocker_port_fdb(struct rocker_port *rocker_port,
			   const unsigned char *addr,
			   __be16 vlan_id, int flags)
{
	struct rocker *rocker = rocker_port->rocker;
	struct rocker_fdb_tbl_entry *fdb;
	struct rocker_fdb_tbl_entry *found;
	bool removing = (flags & ROCKER_OP_FLAG_REMOVE);
	unsigned long lock_flags;

	fdb = kzalloc(sizeof(*fdb), rocker_op_flags_gfp(flags));
	if (!fdb)
		return -ENOMEM;

	fdb->learned = (flags & ROCKER_OP_FLAG_LEARNED);
	fdb->key.lport = rocker_port->lport;
	ether_addr_copy(fdb->key.addr, addr);
	fdb->key.vlan_id = vlan_id;
	fdb->key_crc32 = crc32(~0, &fdb->key, sizeof(fdb->key));

	spin_lock_irqsave(&rocker->fdb_tbl_lock, lock_flags);

	found = rocker_fdb_tbl_find(rocker, fdb);

	if (removing && found) {
		kfree(fdb);
		hash_del(&found->entry);
	} else if (!removing && !found) {
		hash_add(rocker->fdb_tbl, &fdb->entry, fdb->key_crc32);
	}

	spin_unlock_irqrestore(&rocker->fdb_tbl_lock, lock_flags);

	/* Check if adding and already exists, or removing and can't find */
	if (!found != !removing) {
		kfree(fdb);
		if (!found && removing)
			return 0;
		/* Refreshing existing to update aging timers */
		flags |= ROCKER_OP_FLAG_REFRESH;
	}

	return rocker_port_fdb_learn(rocker_port, flags, addr, vlan_id);
}

static int rocker_port_fdb_flush(struct rocker_port *rocker_port)
{
	struct rocker *rocker = rocker_port->rocker;
	struct rocker_fdb_tbl_entry *found;
	unsigned long lock_flags;
	int flags = ROCKER_OP_FLAG_NOWAIT | ROCKER_OP_FLAG_REMOVE;
	struct hlist_node *tmp;
	int bkt;
	int err = 0;

	if (rocker_port->stp_state == BR_STATE_LEARNING ||
	    rocker_port->stp_state == BR_STATE_FORWARDING)
		return 0;

	spin_lock_irqsave(&rocker->fdb_tbl_lock, lock_flags);

	hash_for_each_safe(rocker->fdb_tbl, bkt, tmp, found, entry) {
		if (found->key.lport != rocker_port->lport)
			continue;
		if (!found->learned)
			continue;
		err = rocker_port_fdb_learn(rocker_port, flags,
					    found->key.addr,
					    found->key.vlan_id);
		if (err)
			goto err_out;
		hash_del(&found->entry);
	}

err_out:
	spin_unlock_irqrestore(&rocker->fdb_tbl_lock, lock_flags);

	return err;
}

static int rocker_port_router_mac(struct rocker_port *rocker_port,
				  int flags, __be16 vlan_id)
{
	u32 in_lport_mask = 0xffffffff;
	__be16 eth_type;
	const u8 *dst_mac_mask = ff_mac;
	__be16 vlan_id_mask = htons(0xffff);
	bool copy_to_cpu = false;
	int err;

	if (ntohs(vlan_id) == 0)
		vlan_id = rocker_port->internal_vlan_id;

	eth_type = htons(ETH_P_IP);
	err = rocker_flow_tbl_term_mac(rocker_port,
				       rocker_port->lport, in_lport_mask,
				       eth_type, rocker_port->dev->dev_addr,
				       dst_mac_mask, vlan_id, vlan_id_mask,
				       copy_to_cpu, flags);
	if (err)
		return err;

	eth_type = htons(ETH_P_IPV6);
	err = rocker_flow_tbl_term_mac(rocker_port,
				       rocker_port->lport, in_lport_mask,
				       eth_type, rocker_port->dev->dev_addr,
				       dst_mac_mask, vlan_id, vlan_id_mask,
				       copy_to_cpu, flags);

	return err;
}

static int rocker_port_fwding(struct rocker_port *rocker_port)
{
	bool pop_vlan;
	u32 out_lport;
	__be16 vlan_id;
	u16 vid;
	int flags = ROCKER_OP_FLAG_NOWAIT;
	int err;

	/* Port will be forwarding-enabled if its STP state is LEARNING
	 * or FORWARDING.  Traffic from CPU can still egress, regardless of
	 * port STP state.  Use L2 interface group on port VLANs as a way
	 * to toggle port forwarding: if forwarding is disabled, L2
	 * interface group will not exist.
	 */

	if (rocker_port->stp_state != BR_STATE_LEARNING &&
	    rocker_port->stp_state != BR_STATE_FORWARDING)
		flags |= ROCKER_OP_FLAG_REMOVE;

	out_lport = rocker_port->lport;
	for (vid = 1; vid < VLAN_N_VID; vid++) {
		if (!test_bit(vid, rocker_port->vlan_bitmap))
			continue;
		vlan_id = htons(vid);
		pop_vlan = rocker_vlan_id_is_internal(vlan_id);
		err = rocker_group_l2_interface(rocker_port, flags,
						vlan_id, out_lport,
						pop_vlan);
		if (err) {
			netdev_err(rocker_port->dev,
				   "Error (%d) port VLAN l2 group for lport %d\n",
				   err, out_lport);
			return err;
		}
	}

	return 0;
}

static int rocker_port_stp_update(struct rocker_port *rocker_port, u8 state)
{
	bool want[ROCKER_CTRL_MAX] = { 0, };
	int flags;
	int err;
	int i;

	if (rocker_port->stp_state == state)
		return 0;

	rocker_port->stp_state = state;

	switch (state) {
	case BR_STATE_DISABLED:
		/* port is completely disabled */
		break;
	case BR_STATE_LISTENING:
	case BR_STATE_BLOCKING:
		want[ROCKER_CTRL_LINK_LOCAL_MCAST] = true;
		break;
	case BR_STATE_LEARNING:
	case BR_STATE_FORWARDING:
		want[ROCKER_CTRL_LINK_LOCAL_MCAST] = true;
		want[ROCKER_CTRL_IPV4_MCAST] = true;
		want[ROCKER_CTRL_IPV6_MCAST] = true;
		if (rocker_port_is_bridged(rocker_port))
			want[ROCKER_CTRL_DFLT_BRIDGING] = true;
		else
			want[ROCKER_CTRL_LOCAL_ARP] = true;
		break;
	}

	for (i = 0; i < ROCKER_CTRL_MAX; i++) {
		if (want[i] != rocker_port->ctrls[i]) {
			flags = ROCKER_OP_FLAG_NOWAIT |
				(want[i] ? 0 : ROCKER_OP_FLAG_REMOVE);
			err = rocker_port_ctrl(rocker_port, flags,
					       &rocker_ctrls[i]);
			if (err)
				return err;
			rocker_port->ctrls[i] = want[i];
		}
	}

	err = rocker_port_fdb_flush(rocker_port);
	if (err)
		return err;

	return rocker_port_fwding(rocker_port);
}

static struct rocker_internal_vlan_tbl_entry *
rocker_internal_vlan_tbl_find(struct rocker *rocker, int ifindex)
{
	struct rocker_internal_vlan_tbl_entry *found;

	hash_for_each_possible(rocker->internal_vlan_tbl, found,
			       entry, ifindex) {
		if (found->ifindex == ifindex)
			return found;
	}

	return NULL;
}

static __be16 rocker_port_internal_vlan_id_get(struct rocker_port *rocker_port,
					       int ifindex)
{
	struct rocker *rocker = rocker_port->rocker;
	struct rocker_internal_vlan_tbl_entry *entry;
	struct rocker_internal_vlan_tbl_entry *found;
	unsigned long lock_flags;
	int i;

	entry = kzalloc(sizeof(*entry), GFP_KERNEL);
	if (!entry)
		return 0;

	entry->ifindex = ifindex;

	spin_lock_irqsave(&rocker->internal_vlan_tbl_lock, lock_flags);

	found = rocker_internal_vlan_tbl_find(rocker, ifindex);
	if (found) {
		kfree(entry);
		goto found;
	}

	found = entry;
	hash_add(rocker->internal_vlan_tbl, &found->entry, found->ifindex);

	for (i = 0; i < ROCKER_N_INTERNAL_VLANS; i++) {
		if (test_and_set_bit(i, rocker->internal_vlan_bitmap))
			continue;
		found->vlan_id = htons(ROCKER_INTERNAL_VLAN_ID_BASE + i);
		goto found;
	}

	netdev_err(rocker_port->dev, "Out of internal VLAN IDs\n");

found:
	found->ref_count++;
	spin_unlock_irqrestore(&rocker->internal_vlan_tbl_lock, lock_flags);

	return found->vlan_id;
}

static void rocker_port_internal_vlan_id_put(struct rocker_port *rocker_port,
					     int ifindex)
{
	struct rocker *rocker = rocker_port->rocker;
	struct rocker_internal_vlan_tbl_entry *found;
	unsigned long lock_flags;
	unsigned long bit;

	spin_lock_irqsave(&rocker->internal_vlan_tbl_lock, lock_flags);

	found = rocker_internal_vlan_tbl_find(rocker, ifindex);
	if (!found) {
		netdev_err(rocker_port->dev,
			   "ifindex (%d) not found in internal VLAN tbl\n",
			   ifindex);
		goto not_found;
	}

	if (--found->ref_count <= 0) {
		bit = ntohs(found->vlan_id) - ROCKER_INTERNAL_VLAN_ID_BASE;
		clear_bit(bit, rocker->internal_vlan_bitmap);
		hash_del(&found->entry);
		kfree(found);
	}

not_found:
	spin_unlock_irqrestore(&rocker->internal_vlan_tbl_lock, lock_flags);
}

/*****************
 * Net device ops
 *****************/

static int rocker_port_open(struct net_device *dev)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	u8 stp_state = rocker_port_is_bridged(rocker_port) ?
		BR_STATE_BLOCKING : BR_STATE_FORWARDING;
	int err;

	err = rocker_port_dma_rings_init(rocker_port);
	if (err)
		return err;

	err = request_irq(rocker_msix_tx_vector(rocker_port),
			  rocker_tx_irq_handler, 0,
			  rocker_driver_name, rocker_port);
	if (err) {
		netdev_err(rocker_port->dev, "cannot assign tx irq\n");
		goto err_request_tx_irq;
	}

	err = request_irq(rocker_msix_rx_vector(rocker_port),
			  rocker_rx_irq_handler, 0,
			  rocker_driver_name, rocker_port);
	if (err) {
		netdev_err(rocker_port->dev, "cannot assign rx irq\n");
		goto err_request_rx_irq;
	}

	err = rocker_port_stp_update(rocker_port, stp_state);
	if (err)
		goto err_stp_update;

	napi_enable(&rocker_port->napi_tx);
	napi_enable(&rocker_port->napi_rx);
	rocker_port_set_enable(rocker_port, true);
	netif_start_queue(dev);
	return 0;

err_stp_update:
	free_irq(rocker_msix_rx_vector(rocker_port), rocker_port);
err_request_rx_irq:
	free_irq(rocker_msix_tx_vector(rocker_port), rocker_port);
err_request_tx_irq:
	rocker_port_dma_rings_fini(rocker_port);
	return err;
}

static int rocker_port_stop(struct net_device *dev)
{
	struct rocker_port *rocker_port = netdev_priv(dev);

	netif_stop_queue(dev);
	rocker_port_set_enable(rocker_port, false);
	napi_disable(&rocker_port->napi_rx);
	napi_disable(&rocker_port->napi_tx);
	rocker_port_stp_update(rocker_port, BR_STATE_DISABLED);
	free_irq(rocker_msix_rx_vector(rocker_port), rocker_port);
	free_irq(rocker_msix_tx_vector(rocker_port), rocker_port);
	rocker_port_dma_rings_fini(rocker_port);

	return 0;
}

static void rocker_tx_desc_frags_unmap(struct rocker_port *rocker_port,
				       struct rocker_desc_info *desc_info)
{
	struct rocker *rocker = rocker_port->rocker;
	struct pci_dev *pdev = rocker->pdev;
	struct rocker_tlv *attrs[ROCKER_TLV_TX_MAX + 1];
	struct rocker_tlv *attr;
	int rem;

	rocker_tlv_parse_desc(attrs, ROCKER_TLV_TX_MAX, desc_info);
	if (!attrs[ROCKER_TLV_TX_FRAGS])
		return;
	rocker_tlv_for_each_nested(attr, attrs[ROCKER_TLV_TX_FRAGS], rem) {
		struct rocker_tlv *frag_attrs[ROCKER_TLV_TX_FRAG_ATTR_MAX + 1];
		dma_addr_t dma_handle;
		size_t len;

		if (rocker_tlv_type(attr) != ROCKER_TLV_TX_FRAG)
			continue;
		rocker_tlv_parse_nested(frag_attrs, ROCKER_TLV_TX_FRAG_ATTR_MAX,
					attr);
		if (!frag_attrs[ROCKER_TLV_TX_FRAG_ATTR_ADDR] ||
		    !frag_attrs[ROCKER_TLV_TX_FRAG_ATTR_LEN])
			continue;
		dma_handle = rocker_tlv_get_u64(frag_attrs[ROCKER_TLV_TX_FRAG_ATTR_ADDR]);
		len = rocker_tlv_get_u16(frag_attrs[ROCKER_TLV_TX_FRAG_ATTR_LEN]);
		pci_unmap_single(pdev, dma_handle, len, DMA_TO_DEVICE);
	}
}

static int rocker_tx_desc_frag_map_put(struct rocker_port *rocker_port,
				       struct rocker_desc_info *desc_info,
				       char *buf, size_t buf_len)
{
	struct rocker *rocker = rocker_port->rocker;
	struct pci_dev *pdev = rocker->pdev;
	dma_addr_t dma_handle;
	struct rocker_tlv *frag;

	dma_handle = pci_map_single(pdev, buf, buf_len, DMA_TO_DEVICE);
	if (unlikely(pci_dma_mapping_error(pdev, dma_handle))) {
		if (net_ratelimit())
			netdev_err(rocker_port->dev, "failed to dma map tx frag\n");
		return -EIO;
	}
	frag = rocker_tlv_nest_start(desc_info, ROCKER_TLV_TX_FRAG);
	if (!frag)
		goto unmap_frag;
	if (rocker_tlv_put_u64(desc_info, ROCKER_TLV_TX_FRAG_ATTR_ADDR,
			       dma_handle))
		goto nest_cancel;
	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_TX_FRAG_ATTR_LEN,
			       buf_len))
		goto nest_cancel;
	rocker_tlv_nest_end(desc_info, frag);
	return 0;

nest_cancel:
	rocker_tlv_nest_cancel(desc_info, frag);
unmap_frag:
	pci_unmap_single(pdev, dma_handle, buf_len, DMA_TO_DEVICE);
	return -EMSGSIZE;
}

static netdev_tx_t rocker_port_xmit(struct sk_buff *skb, struct net_device *dev)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	struct rocker *rocker = rocker_port->rocker;
	struct rocker_desc_info *desc_info;
	struct rocker_tlv *frags;
	int i;
	int err;

	desc_info = rocker_desc_head_get(&rocker_port->tx_ring);
	if (unlikely(!desc_info)) {
		if (net_ratelimit())
			netdev_err(dev, "tx ring full when queue awake\n");
		return NETDEV_TX_BUSY;
	}

	rocker_desc_cookie_ptr_set(desc_info, skb);

	frags = rocker_tlv_nest_start(desc_info, ROCKER_TLV_TX_FRAGS);
	if (!frags)
		goto out;
	err = rocker_tx_desc_frag_map_put(rocker_port, desc_info,
					  skb->data, skb_headlen(skb));
	if (err)
		goto nest_cancel;
	if (skb_shinfo(skb)->nr_frags > ROCKER_TX_FRAGS_MAX)
		goto nest_cancel;

	for (i = 0; i < skb_shinfo(skb)->nr_frags; i++) {
		const skb_frag_t *frag = &skb_shinfo(skb)->frags[i];

		err = rocker_tx_desc_frag_map_put(rocker_port, desc_info,
						  skb_frag_address(frag),
						  skb_frag_size(frag));
		if (err)
			goto unmap_frags;
	}
	rocker_tlv_nest_end(desc_info, frags);

	rocker_desc_gen_clear(desc_info);
	rocker_desc_head_set(rocker, &rocker_port->tx_ring, desc_info);

	desc_info = rocker_desc_head_get(&rocker_port->tx_ring);
	if (!desc_info)
		netif_stop_queue(dev);

	return NETDEV_TX_OK;

unmap_frags:
	rocker_tx_desc_frags_unmap(rocker_port, desc_info);
nest_cancel:
	rocker_tlv_nest_cancel(desc_info, frags);
out:
	dev_kfree_skb(skb);
	return NETDEV_TX_OK;
}

static int rocker_port_set_mac_address(struct net_device *dev, void *p)
{
	struct sockaddr *addr = p;
	struct rocker_port *rocker_port = netdev_priv(dev);
	int err;

	if (!is_valid_ether_addr(addr->sa_data))
		return -EADDRNOTAVAIL;

	err = rocker_cmd_set_port_settings_macaddr(rocker_port, addr->sa_data);
	if (err)
		return err;
	memcpy(dev->dev_addr, addr->sa_data, dev->addr_len);
	return 0;
}

static int rocker_port_vlan_rx_add_vid(struct net_device *dev,
				       __be16 proto, u16 vid)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	int err;

	err = rocker_port_vlan(rocker_port, 0, vid);
	if (err)
		return err;

	return rocker_port_router_mac(rocker_port, 0, htons(vid));
}

static int rocker_port_vlan_rx_kill_vid(struct net_device *dev,
					__be16 proto, u16 vid)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	int err;

	err = rocker_port_router_mac(rocker_port, ROCKER_OP_FLAG_REMOVE,
				     htons(vid));
	if (err)
		return err;

	return rocker_port_vlan(rocker_port, ROCKER_OP_FLAG_REMOVE, vid);
}

static int rocker_port_fdb_add(struct ndmsg *ndm, struct nlattr *tb[],
			       struct net_device *dev,
			       const unsigned char *addr, u16 vid,
			       u16 nlm_flags)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	__be16 vlan_id = rocker_port_vid_to_vlan(rocker_port, vid, NULL);
	int flags = 0;

	if (!rocker_port_is_bridged(rocker_port))
		return -EINVAL;

	return rocker_port_fdb(rocker_port, addr, vlan_id, flags);
}

static int rocker_port_fdb_del(struct ndmsg *ndm, struct nlattr *tb[],
			       struct net_device *dev,
			       const unsigned char *addr, u16 vid)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	__be16 vlan_id = rocker_port_vid_to_vlan(rocker_port, vid, NULL);
	int flags = ROCKER_OP_FLAG_REMOVE;

	if (!rocker_port_is_bridged(rocker_port))
		return -EINVAL;

	return rocker_port_fdb(rocker_port, addr, vlan_id, flags);
}

static int rocker_fdb_fill_info(struct sk_buff *skb,
				struct rocker_port *rocker_port,
				const unsigned char *addr, u16 vid,
				u32 portid, u32 seq, int type,
				unsigned int flags)
{
	struct nlmsghdr *nlh;
	struct ndmsg *ndm;

	nlh = nlmsg_put(skb, portid, seq, type, sizeof(*ndm), flags);
	if (!nlh)
		return -EMSGSIZE;

	ndm = nlmsg_data(nlh);
	ndm->ndm_family	 = AF_BRIDGE;
	ndm->ndm_pad1    = 0;
	ndm->ndm_pad2    = 0;
	ndm->ndm_flags	 = NTF_SELF;
	ndm->ndm_type	 = 0;
	ndm->ndm_ifindex = rocker_port->dev->ifindex;
	ndm->ndm_state   = NUD_REACHABLE;

	if (nla_put(skb, NDA_LLADDR, ETH_ALEN, addr))
		goto nla_put_failure;

	if (vid && nla_put_u16(skb, NDA_VLAN, vid))
		goto nla_put_failure;

	return nlmsg_end(skb, nlh);

nla_put_failure:
	nlmsg_cancel(skb, nlh);
	return -EMSGSIZE;
}

static int rocker_port_fdb_dump(struct sk_buff *skb,
				struct netlink_callback *cb,
				struct net_device *dev,
				struct net_device *filter_dev,
				int idx)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	struct rocker *rocker = rocker_port->rocker;
	struct rocker_fdb_tbl_entry *found;
	struct hlist_node *tmp;
	int bkt;
	unsigned long lock_flags;
	const unsigned char *addr;
	u16 vid;
	int err;

	spin_lock_irqsave(&rocker->fdb_tbl_lock, lock_flags);
	hash_for_each_safe(rocker->fdb_tbl, bkt, tmp, found, entry) {
		if (found->key.lport != rocker_port->lport)
			continue;
		if (idx < cb->args[0])
			goto skip;
		addr = found->key.addr;
		vid = rocker_port_vlan_to_vid(rocker_port, found->key.vlan_id);
		err = rocker_fdb_fill_info(skb, rocker_port, addr, vid,
					   NETLINK_CB(cb->skb).portid,
					   cb->nlh->nlmsg_seq,
					   RTM_NEWNEIGH, NLM_F_MULTI);
		if (err < 0)
			break;
skip:
		++idx;
	}
	spin_unlock_irqrestore(&rocker->fdb_tbl_lock, lock_flags);
	return idx;
}

static int rocker_port_bridge_setlink(struct net_device *dev,
				      struct nlmsghdr *nlh)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	struct nlattr *protinfo;
	struct nlattr *afspec;
	struct nlattr *attr;
	u16 mode;
	int err;

	protinfo = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg),
				   IFLA_PROTINFO);
	afspec = nlmsg_find_attr(nlh, sizeof(struct ifinfomsg), IFLA_AF_SPEC);

	if (afspec) {
		attr = nla_find_nested(afspec, IFLA_BRIDGE_MODE);
		if (attr) {
			if (nla_len(attr) < sizeof(mode))
				return -EINVAL;

			mode = nla_get_u16(attr);
			if (mode != BRIDGE_MODE_SWDEV)
				return -EINVAL;
		}
	}

	if (protinfo) {
		attr = nla_find_nested(protinfo, IFLA_BRPORT_LEARNING);
		if (attr) {
			if (nla_len(attr) < sizeof(u8))
				return -EINVAL;

			if (nla_get_u8(attr))
				rocker_port->brport_flags |= BR_LEARNING;
			else
				rocker_port->brport_flags &= ~BR_LEARNING;
			err = rocker_port_set_learning(rocker_port);
			if (err)
				return err;
		}
		attr = nla_find_nested(protinfo, IFLA_BRPORT_LEARNING_SYNC);
		if (attr) {
			if (nla_len(attr) < sizeof(u8))
				return -EINVAL;

			if (nla_get_u8(attr))
				rocker_port->brport_flags |= BR_LEARNING_SYNC;
			else
				rocker_port->brport_flags &= ~BR_LEARNING_SYNC;
		}
	}

	return 0;
}

static int rocker_port_bridge_getlink(struct sk_buff *skb, u32 pid, u32 seq,
				      struct net_device *dev,
				      u32 filter_mask)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	u16 mode = BRIDGE_MODE_SWDEV;
	u32 mask = BR_LEARNING | BR_LEARNING_SYNC;

	return ndo_dflt_bridge_getlink(skb, pid, seq, dev, mode,
				       rocker_port->brport_flags, mask);
}

static int rocker_port_switch_parent_id_get(struct net_device *dev,
					    struct netdev_phys_item_id *psid)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	struct rocker *rocker = rocker_port->rocker;

	psid->id_len = sizeof(rocker->hw.id);
	memcpy(&psid->id, &rocker->hw.id, psid->id_len);
	return 0;
}

static int rocker_port_switch_port_stp_update(struct net_device *dev, u8 state)
{
	struct rocker_port *rocker_port = netdev_priv(dev);

	return rocker_port_stp_update(rocker_port, state);
}

static const struct net_device_ops rocker_port_netdev_ops = {
	.ndo_open			= rocker_port_open,
	.ndo_stop			= rocker_port_stop,
	.ndo_start_xmit			= rocker_port_xmit,
	.ndo_set_mac_address		= rocker_port_set_mac_address,
	.ndo_vlan_rx_add_vid		= rocker_port_vlan_rx_add_vid,
	.ndo_vlan_rx_kill_vid		= rocker_port_vlan_rx_kill_vid,
	.ndo_fdb_add			= rocker_port_fdb_add,
	.ndo_fdb_del			= rocker_port_fdb_del,
	.ndo_fdb_dump			= rocker_port_fdb_dump,
	.ndo_bridge_setlink		= rocker_port_bridge_setlink,
	.ndo_bridge_getlink		= rocker_port_bridge_getlink,
	.ndo_switch_parent_id_get	= rocker_port_switch_parent_id_get,
	.ndo_switch_port_stp_update	= rocker_port_switch_port_stp_update,
};

/********************
 * ethtool interface
 ********************/

static int rocker_port_get_settings(struct net_device *dev,
				    struct ethtool_cmd *ecmd)
{
	struct rocker_port *rocker_port = netdev_priv(dev);

	return rocker_cmd_get_port_settings_ethtool(rocker_port, ecmd);
}

static int rocker_port_set_settings(struct net_device *dev,
				    struct ethtool_cmd *ecmd)
{
	struct rocker_port *rocker_port = netdev_priv(dev);

	return rocker_cmd_set_port_settings_ethtool(rocker_port, ecmd);
}

static void rocker_port_get_drvinfo(struct net_device *dev,
				    struct ethtool_drvinfo *drvinfo)
{
	strlcpy(drvinfo->driver, rocker_driver_name, sizeof(drvinfo->driver));
	strlcpy(drvinfo->version, UTS_RELEASE, sizeof(drvinfo->version));
}

static const struct ethtool_ops rocker_port_ethtool_ops = {
	.get_settings		= rocker_port_get_settings,
	.set_settings		= rocker_port_set_settings,
	.get_drvinfo		= rocker_port_get_drvinfo,
	.get_link		= ethtool_op_get_link,
};

/*****************
 * NAPI interface
 *****************/

static struct rocker_port *rocker_port_napi_tx_get(struct napi_struct *napi)
{
	return container_of(napi, struct rocker_port, napi_tx);
}

static int rocker_port_poll_tx(struct napi_struct *napi, int budget)
{
	struct rocker_port *rocker_port = rocker_port_napi_tx_get(napi);
	struct rocker *rocker = rocker_port->rocker;
	struct rocker_desc_info *desc_info;
	u32 credits = 0;
	int err;

	/* Cleanup tx descriptors */
	while ((desc_info = rocker_desc_tail_get(&rocker_port->tx_ring))) {
		err = rocker_desc_err(desc_info);
		if (err && net_ratelimit())
			netdev_err(rocker_port->dev, "tx desc received with err %d\n",
				   err);
		rocker_tx_desc_frags_unmap(rocker_port, desc_info);
		dev_kfree_skb_any(rocker_desc_cookie_ptr_get(desc_info));
		credits++;
	}

	if (credits && netif_queue_stopped(rocker_port->dev))
		netif_wake_queue(rocker_port->dev);

	napi_complete(napi);
	rocker_dma_ring_credits_set(rocker, &rocker_port->tx_ring, credits);

	return 0;
}

static int rocker_port_rx_proc(struct rocker *rocker,
			       struct rocker_port *rocker_port,
			       struct rocker_desc_info *desc_info)
{
	struct rocker_tlv *attrs[ROCKER_TLV_RX_MAX + 1];
	struct sk_buff *skb = rocker_desc_cookie_ptr_get(desc_info);
	size_t rx_len;

	if (!skb)
		return -ENOENT;

	rocker_tlv_parse_desc(attrs, ROCKER_TLV_RX_MAX, desc_info);
	if (!attrs[ROCKER_TLV_RX_FRAG_LEN])
		return -EINVAL;

	rocker_dma_rx_ring_skb_unmap(rocker, attrs);

	rx_len = rocker_tlv_get_u16(attrs[ROCKER_TLV_RX_FRAG_LEN]);
	skb_put(skb, rx_len);
	skb->protocol = eth_type_trans(skb, rocker_port->dev);
	netif_receive_skb(skb);

	return rocker_dma_rx_ring_skb_alloc(rocker, rocker_port, desc_info);
}

static struct rocker_port *rocker_port_napi_rx_get(struct napi_struct *napi)
{
	return container_of(napi, struct rocker_port, napi_rx);
}

static int rocker_port_poll_rx(struct napi_struct *napi, int budget)
{
	struct rocker_port *rocker_port = rocker_port_napi_rx_get(napi);
	struct rocker *rocker = rocker_port->rocker;
	struct rocker_desc_info *desc_info;
	u32 credits = 0;
	int err;

	/* Process rx descriptors */
	while (credits < budget &&
	       (desc_info = rocker_desc_tail_get(&rocker_port->rx_ring))) {
		err = rocker_desc_err(desc_info);
		if (err) {
			if (net_ratelimit())
				netdev_err(rocker_port->dev, "rx desc received with err %d\n",
					   err);
		} else {
			err = rocker_port_rx_proc(rocker, rocker_port,
						  desc_info);
			if (err && net_ratelimit())
				netdev_err(rocker_port->dev, "rx processing failed with err %d\n",
					   err);
		}
		rocker_desc_gen_clear(desc_info);
		rocker_desc_head_set(rocker, &rocker_port->rx_ring, desc_info);
		credits++;
	}

	if (credits < budget)
		napi_complete(napi);

	rocker_dma_ring_credits_set(rocker, &rocker_port->rx_ring, credits);

	return credits;
}

/*****************
 * PCI driver ops
 *****************/

static void rocker_carrier_init(struct rocker_port *rocker_port)
{
	struct rocker *rocker = rocker_port->rocker;
	u64 link_status = rocker_read64(rocker, PORT_PHYS_LINK_STATUS);
	bool link_up;

	link_up = link_status & (1 << rocker_port->lport);
	if (link_up)
		netif_carrier_on(rocker_port->dev);
	else
		netif_carrier_off(rocker_port->dev);
}

static void rocker_remove_ports(struct rocker *rocker)
{
	struct rocker_port *rocker_port;
	int i;

	for (i = 0; i < rocker->port_count; i++) {
		rocker_port = rocker->ports[i];
		rocker_port_ig_tbl(rocker_port, ROCKER_OP_FLAG_REMOVE);
		unregister_netdev(rocker_port->dev);
	}
	kfree(rocker->ports);
}

static void rocker_port_dev_addr_init(struct rocker *rocker,
				      struct rocker_port *rocker_port)
{
	struct pci_dev *pdev = rocker->pdev;
	int err;

	err = rocker_cmd_get_port_settings_macaddr(rocker_port,
						   rocker_port->dev->dev_addr);
	if (err) {
		dev_warn(&pdev->dev, "failed to get mac address, using random\n");
		eth_hw_addr_random(rocker_port->dev);
	}
}

static int rocker_probe_port(struct rocker *rocker, unsigned int port_number)
{
	struct pci_dev *pdev = rocker->pdev;
	struct rocker_port *rocker_port;
	struct net_device *dev;
	int err;

	dev = alloc_etherdev(sizeof(struct rocker_port));
	if (!dev)
		return -ENOMEM;
	rocker_port = netdev_priv(dev);
	rocker_port->dev = dev;
	rocker_port->rocker = rocker;
	rocker_port->port_number = port_number;
	rocker_port->lport = port_number + 1;
	rocker_port->brport_flags = BR_LEARNING | BR_LEARNING_SYNC;

	rocker_port_dev_addr_init(rocker, rocker_port);
	dev->netdev_ops = &rocker_port_netdev_ops;
	dev->ethtool_ops = &rocker_port_ethtool_ops;
	netif_napi_add(dev, &rocker_port->napi_tx, rocker_port_poll_tx,
		       NAPI_POLL_WEIGHT);
	netif_napi_add(dev, &rocker_port->napi_rx, rocker_port_poll_rx,
		       NAPI_POLL_WEIGHT);
	rocker_carrier_init(rocker_port);

	dev->features |= NETIF_F_HW_VLAN_CTAG_FILTER;

	err = register_netdev(dev);
	if (err) {
		dev_err(&pdev->dev, "register_netdev failed\n");
		goto err_register_netdev;
	}
	rocker->ports[port_number] = rocker_port;

	rocker_port_set_learning(rocker_port);

	rocker_port->internal_vlan_id =
		rocker_port_internal_vlan_id_get(rocker_port, dev->ifindex);
	err = rocker_port_ig_tbl(rocker_port, 0);
	if (err) {
		dev_err(&pdev->dev, "install ig port table failed\n");
		goto err_port_ig_tbl;
	}

	return 0;

err_port_ig_tbl:
	unregister_netdev(dev);
err_register_netdev:
	free_netdev(dev);
	return err;
}

static int rocker_probe_ports(struct rocker *rocker)
{
	int i;
	size_t alloc_size;
	int err;

	alloc_size = sizeof(struct rocker_port *) * rocker->port_count;
	rocker->ports = kmalloc(alloc_size, GFP_KERNEL);
	for (i = 0; i < rocker->port_count; i++) {
		err = rocker_probe_port(rocker, i);
		if (err)
			goto remove_ports;
	}
	return 0;

remove_ports:
	rocker_remove_ports(rocker);
	return err;
}

static int rocker_msix_init(struct rocker *rocker)
{
	struct pci_dev *pdev = rocker->pdev;
	int msix_entries;
	int i;
	int err;

	msix_entries = pci_msix_vec_count(pdev);
	if (msix_entries < 0)
		return msix_entries;

	if (msix_entries != ROCKER_MSIX_VEC_COUNT(rocker->port_count))
		return -EINVAL;

	rocker->msix_entries = kmalloc_array(msix_entries,
					     sizeof(struct msix_entry),
					     GFP_KERNEL);
	if (!rocker->msix_entries)
		return -ENOMEM;

	for (i = 0; i < msix_entries; i++)
		rocker->msix_entries[i].entry = i;

	err = pci_enable_msix_exact(pdev, rocker->msix_entries, msix_entries);
	if (err < 0)
		goto err_enable_msix;

	return 0;

err_enable_msix:
	kfree(rocker->msix_entries);
	return err;
}

static void rocker_msix_fini(struct rocker *rocker)
{
	pci_disable_msix(rocker->pdev);
	kfree(rocker->msix_entries);
}

static int rocker_probe(struct pci_dev *pdev, const struct pci_device_id *id)
{
	struct rocker *rocker;
	int err;

	rocker = kzalloc(sizeof(*rocker), GFP_KERNEL);
	if (!rocker)
		return -ENOMEM;

	err = pci_enable_device(pdev);
	if (err) {
		dev_err(&pdev->dev, "pci_enable_device failed\n");
		goto err_pci_enable_device;
	}

	err = pci_request_regions(pdev, rocker_driver_name);
	if (err) {
		dev_err(&pdev->dev, "pci_request_regions failed\n");
		goto err_pci_request_regions;
	}

	err = pci_set_dma_mask(pdev, DMA_BIT_MASK(64));
	if (!err) {
		err = pci_set_consistent_dma_mask(pdev, DMA_BIT_MASK(64));
		if (err) {
			dev_err(&pdev->dev, "pci_set_consistent_dma_mask failed\n");
			goto err_pci_set_dma_mask;
		}
	} else {
		err = pci_set_dma_mask(pdev, DMA_BIT_MASK(32));
		if (err) {
			dev_err(&pdev->dev, "pci_set_dma_mask failed\n");
			goto err_pci_set_dma_mask;
		}
	}

	if (pci_resource_len(pdev, 0) < ROCKER_PCI_BAR0_SIZE) {
		dev_err(&pdev->dev, "invalid PCI region size\n");
		goto err_pci_resource_len_check;
	}

	rocker->hw_addr = ioremap(pci_resource_start(pdev, 0),
				  pci_resource_len(pdev, 0));
	if (!rocker->hw_addr) {
		dev_err(&pdev->dev, "ioremap failed\n");
		err = -EIO;
		goto err_ioremap;
	}
	pci_set_master(pdev);

	rocker->pdev = pdev;
	pci_set_drvdata(pdev, rocker);

	rocker->port_count = rocker_read32(rocker, PORT_PHYS_COUNT);

	err = rocker_msix_init(rocker);
	if (err) {
		dev_err(&pdev->dev, "MSI-X init failed\n");
		goto err_msix_init;
	}

	err = rocker_basic_hw_test(rocker);
	if (err) {
		dev_err(&pdev->dev, "basic hw test failed\n");
		goto err_basic_hw_test;
	}

	rocker_write32(rocker, CONTROL, ROCKER_CONTROL_RESET);

	err = rocker_dma_rings_init(rocker);
	if (err)
		goto err_dma_rings_init;

	err = request_irq(rocker_msix_vector(rocker, ROCKER_MSIX_VEC_CMD),
			  rocker_cmd_irq_handler, 0,
			  rocker_driver_name, rocker);
	if (err) {
		dev_err(&pdev->dev, "cannot assign cmd irq\n");
		goto err_request_cmd_irq;
	}

	err = request_irq(rocker_msix_vector(rocker, ROCKER_MSIX_VEC_EVENT),
			  rocker_event_irq_handler, 0,
			  rocker_driver_name, rocker);
	if (err) {
		dev_err(&pdev->dev, "cannot assign event irq\n");
		goto err_request_event_irq;
	}

	rocker->hw.id = rocker_read64(rocker, SWITCH_ID);

	err = rocker_init_tbls(rocker);
	if (err) {
		dev_err(&pdev->dev, "cannot init rocker tables\n");
		goto err_init_tbls;
	}

	err = rocker_probe_ports(rocker);
	if (err) {
		dev_err(&pdev->dev, "failed to probe ports\n");
		goto err_probe_ports;
	}

	dev_info(&pdev->dev, "Rocker switch with id %016llx\n", rocker->hw.id);

	return 0;

err_probe_ports:
	rocker_free_tbls(rocker);
err_init_tbls:
	free_irq(rocker_msix_vector(rocker, ROCKER_MSIX_VEC_EVENT), rocker);
err_request_event_irq:
	free_irq(rocker_msix_vector(rocker, ROCKER_MSIX_VEC_CMD), rocker);
err_request_cmd_irq:
	rocker_dma_rings_fini(rocker);
err_dma_rings_init:
err_basic_hw_test:
	rocker_msix_fini(rocker);
err_msix_init:
	iounmap(rocker->hw_addr);
err_ioremap:
err_pci_resource_len_check:
err_pci_set_dma_mask:
	pci_release_regions(pdev);
err_pci_request_regions:
	pci_disable_device(pdev);
err_pci_enable_device:
	kfree(rocker);
	return err;
}

static void rocker_remove(struct pci_dev *pdev)
{
	struct rocker *rocker = pci_get_drvdata(pdev);

	rocker_free_tbls(rocker);
	rocker_write32(rocker, CONTROL, ROCKER_CONTROL_RESET);
	rocker_remove_ports(rocker);
	free_irq(rocker_msix_vector(rocker, ROCKER_MSIX_VEC_EVENT), rocker);
	free_irq(rocker_msix_vector(rocker, ROCKER_MSIX_VEC_CMD), rocker);
	rocker_dma_rings_fini(rocker);
	rocker_msix_fini(rocker);
	iounmap(rocker->hw_addr);
	pci_release_regions(rocker->pdev);
	pci_disable_device(rocker->pdev);
	kfree(rocker);
}

static struct pci_driver rocker_pci_driver = {
	.name		= rocker_driver_name,
	.id_table	= rocker_pci_id_table,
	.probe		= rocker_probe,
	.remove		= rocker_remove,
};

/************************************
 * Net device notifier event handler
 ************************************/

static bool rocker_port_dev_check(struct net_device *dev)
{
	return dev->netdev_ops == &rocker_port_netdev_ops;
}

static int rocker_port_bridge_join(struct rocker_port *rocker_port,
				   struct net_device *bridge)
{
	int err;

	rocker_port_internal_vlan_id_put(rocker_port,
					 rocker_port->dev->ifindex);

	rocker_port->bridge_dev = bridge;

	/* Use bridge internal VLAN ID for untagged pkts */
	err = rocker_port_vlan(rocker_port, ROCKER_OP_FLAG_REMOVE, 0);
	if (err)
		return err;
	rocker_port->internal_vlan_id =
		rocker_port_internal_vlan_id_get(rocker_port,
						 bridge->ifindex);
	err = rocker_port_vlan(rocker_port, 0, 0);

	return err;
}

static int rocker_port_bridge_leave(struct rocker_port *rocker_port)
{
	int err;

	rocker_port_internal_vlan_id_put(rocker_port,
					 rocker_port->bridge_dev->ifindex);

	rocker_port->bridge_dev = NULL;

	/* Use port internal VLAN ID for untagged pkts */
	err = rocker_port_vlan(rocker_port, ROCKER_OP_FLAG_REMOVE, 0);
	if (err)
		return err;
	rocker_port->internal_vlan_id =
		rocker_port_internal_vlan_id_get(rocker_port,
						 rocker_port->dev->ifindex);
	err = rocker_port_vlan(rocker_port, 0, 0);

	return err;
}

static int rocker_port_master_changed(struct net_device *dev)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	struct net_device *master = netdev_master_upper_dev_get(dev);
	int err = 0;

	if (master && master->rtnl_link_ops &&
	    !strcmp(master->rtnl_link_ops->kind, "bridge"))
		err = rocker_port_bridge_join(rocker_port, master);
	else
		err = rocker_port_bridge_leave(rocker_port);

	return err;
}

static int rocker_netdevice_event(struct notifier_block *unused,
				  unsigned long event, void *ptr)
{
	struct net_device *dev;
	int err;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		dev = netdev_notifier_info_to_dev(ptr);
		if (!rocker_port_dev_check(dev))
			return NOTIFY_DONE;
		err = rocker_port_master_changed(dev);
		if (err)
			netdev_warn(dev,
				    "failed to reflect master change (err %d)\n",
				    err);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block rocker_netdevice_nb __read_mostly = {
	.notifier_call = rocker_netdevice_event,
};

/***********************
 * Module init and exit
 ***********************/

static int __init rocker_module_init(void)
{
	int err;

	register_netdevice_notifier(&rocker_netdevice_nb);
	err = pci_register_driver(&rocker_pci_driver);
	if (err)
		goto err_pci_register_driver;
	return 0;

err_pci_register_driver:
	unregister_netdevice_notifier(&rocker_netdevice_nb);
	return err;
}

static void __exit rocker_module_exit(void)
{
	unregister_netdevice_notifier(&rocker_netdevice_nb);
	pci_unregister_driver(&rocker_pci_driver);
}

module_init(rocker_module_init);
module_exit(rocker_module_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jiri Pirko <jiri@resnulli.us>");
MODULE_AUTHOR("Scott Feldman <sfeldma@gmail.com>");
MODULE_DESCRIPTION("Rocker switch device driver");
MODULE_DEVICE_TABLE(pci, rocker_pci_id_table);
