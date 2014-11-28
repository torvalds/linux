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

struct rocker_port {
	struct net_device *dev;
	struct rocker *rocker;
	unsigned int port_number;
	u32 lport;
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
};

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

static int rocker_tlv_put_u32(struct rocker_desc_info *desc_info,
			      int attrtype, u32 value)
{
	return rocker_tlv_put(desc_info, attrtype, sizeof(u32), &value);
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

/*****************
 * Net device ops
 *****************/

static int rocker_port_open(struct net_device *dev)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
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

	napi_enable(&rocker_port->napi_tx);
	napi_enable(&rocker_port->napi_rx);
	rocker_port_set_enable(rocker_port, true);
	netif_start_queue(dev);
	return 0;

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

static int rocker_port_switch_parent_id_get(struct net_device *dev,
					    struct netdev_phys_item_id *psid)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	struct rocker *rocker = rocker_port->rocker;

	psid->id_len = sizeof(rocker->hw.id);
	memcpy(&psid->id, &rocker->hw.id, psid->id_len);
	return 0;
}

static const struct net_device_ops rocker_port_netdev_ops = {
	.ndo_open			= rocker_port_open,
	.ndo_stop			= rocker_port_stop,
	.ndo_start_xmit			= rocker_port_xmit,
	.ndo_set_mac_address		= rocker_port_set_mac_address,
	.ndo_switch_parent_id_get	= rocker_port_switch_parent_id_get,
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
	int i;

	for (i = 0; i < rocker->port_count; i++)
		unregister_netdev(rocker->ports[i]->dev);
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

	return 0;

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

	err = rocker_probe_ports(rocker);
	if (err) {
		dev_err(&pdev->dev, "failed to probe ports\n");
		goto err_probe_ports;
	}

	dev_info(&pdev->dev, "Rocker switch with id %016llx\n", rocker->hw.id);

	return 0;

err_probe_ports:
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

/***********************
 * Module init and exit
 ***********************/

static int __init rocker_module_init(void)
{
	return pci_register_driver(&rocker_pci_driver);
}

static void __exit rocker_module_exit(void)
{
	pci_unregister_driver(&rocker_pci_driver);
}

module_init(rocker_module_init);
module_exit(rocker_module_exit);

MODULE_LICENSE("GPL v2");
MODULE_AUTHOR("Jiri Pirko <jiri@resnulli.us>");
MODULE_AUTHOR("Scott Feldman <sfeldma@gmail.com>");
MODULE_DESCRIPTION("Rocker switch device driver");
MODULE_DEVICE_TABLE(pci, rocker_pci_id_table);
