/*
 * drivers/net/ethernet/rocker/rocker.c - Rocker switch device driver
 * Copyright (c) 2014-2016 Jiri Pirko <jiri@mellanox.com>
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
#include <linux/sort.h>
#include <linux/random.h>
#include <linux/netdevice.h>
#include <linux/skbuff.h>
#include <linux/socket.h>
#include <linux/etherdevice.h>
#include <linux/ethtool.h>
#include <linux/if_ether.h>
#include <linux/if_vlan.h>
#include <linux/if_bridge.h>
#include <linux/bitops.h>
#include <linux/ctype.h>
#include <net/switchdev.h>
#include <net/rtnetlink.h>
#include <net/netevent.h>
#include <net/arp.h>
#include <linux/io-64-nonatomic-lo-hi.h>
#include <generated/utsrelease.h>

#include "rocker_hw.h"
#include "rocker.h"
#include "rocker_tlv.h"

static const char rocker_driver_name[] = "rocker";

static const struct pci_device_id rocker_pci_id_table[] = {
	{PCI_VDEVICE(REDHAT, PCI_DEVICE_ID_REDHAT_ROCKER), 0},
	{0, }
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

static struct rocker_wait *rocker_wait_create(void)
{
	struct rocker_wait *wait;

	wait = kzalloc(sizeof(*wait), GFP_KERNEL);
	if (!wait)
		return NULL;
	return wait;
}

static void rocker_wait_destroy(struct rocker_wait *wait)
{
	kfree(wait);
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

static u32 rocker_msix_vector(const struct rocker *rocker, unsigned int vector)
{
	return rocker->msix_entries[vector].vector;
}

static u32 rocker_msix_tx_vector(const struct rocker_port *rocker_port)
{
	return rocker_msix_vector(rocker_port->rocker,
				  ROCKER_MSIX_VEC_TX(rocker_port->port_number));
}

static u32 rocker_msix_rx_vector(const struct rocker_port *rocker_port)
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

static int rocker_reg_test(const struct rocker *rocker)
{
	const struct pci_dev *pdev = rocker->pdev;
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

static int rocker_dma_test_one(const struct rocker *rocker,
			       struct rocker_wait *wait, u32 test_type,
			       dma_addr_t dma_handle, const unsigned char *buf,
			       const unsigned char *expect, size_t size)
{
	const struct pci_dev *pdev = rocker->pdev;
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

static int rocker_dma_test_offset(const struct rocker *rocker,
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

static int rocker_dma_test(const struct rocker *rocker,
			   struct rocker_wait *wait)
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

static int rocker_basic_hw_test(const struct rocker *rocker)
{
	const struct pci_dev *pdev = rocker->pdev;
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

/******************************************
 * DMA rings and descriptors manipulations
 ******************************************/

static u32 __pos_inc(u32 pos, size_t limit)
{
	return ++pos == limit ? 0 : pos;
}

static int rocker_desc_err(const struct rocker_desc_info *desc_info)
{
	int err = desc_info->desc->comp_err & ~ROCKER_DMA_DESC_COMP_ERR_GEN;

	switch (err) {
	case ROCKER_OK:
		return 0;
	case -ROCKER_ENOENT:
		return -ENOENT;
	case -ROCKER_ENXIO:
		return -ENXIO;
	case -ROCKER_ENOMEM:
		return -ENOMEM;
	case -ROCKER_EEXIST:
		return -EEXIST;
	case -ROCKER_EINVAL:
		return -EINVAL;
	case -ROCKER_EMSGSIZE:
		return -EMSGSIZE;
	case -ROCKER_ENOTSUP:
		return -EOPNOTSUPP;
	case -ROCKER_ENOBUFS:
		return -ENOBUFS;
	}

	return -EINVAL;
}

static void rocker_desc_gen_clear(const struct rocker_desc_info *desc_info)
{
	desc_info->desc->comp_err &= ~ROCKER_DMA_DESC_COMP_ERR_GEN;
}

static bool rocker_desc_gen(const struct rocker_desc_info *desc_info)
{
	u32 comp_err = desc_info->desc->comp_err;

	return comp_err & ROCKER_DMA_DESC_COMP_ERR_GEN ? true : false;
}

static void *
rocker_desc_cookie_ptr_get(const struct rocker_desc_info *desc_info)
{
	return (void *)(uintptr_t)desc_info->desc->cookie;
}

static void rocker_desc_cookie_ptr_set(const struct rocker_desc_info *desc_info,
				       void *ptr)
{
	desc_info->desc->cookie = (uintptr_t) ptr;
}

static struct rocker_desc_info *
rocker_desc_head_get(const struct rocker_dma_ring_info *info)
{
	static struct rocker_desc_info *desc_info;
	u32 head = __pos_inc(info->head, info->size);

	desc_info = &info->desc_info[info->head];
	if (head == info->tail)
		return NULL; /* ring full */
	desc_info->tlv_size = 0;
	return desc_info;
}

static void rocker_desc_commit(const struct rocker_desc_info *desc_info)
{
	desc_info->desc->buf_size = desc_info->data_size;
	desc_info->desc->tlv_size = desc_info->tlv_size;
}

static void rocker_desc_head_set(const struct rocker *rocker,
				 struct rocker_dma_ring_info *info,
				 const struct rocker_desc_info *desc_info)
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

static void rocker_dma_ring_credits_set(const struct rocker *rocker,
					const struct rocker_dma_ring_info *info,
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

static int rocker_dma_ring_create(const struct rocker *rocker,
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

static void rocker_dma_ring_destroy(const struct rocker *rocker,
				    const struct rocker_dma_ring_info *info)
{
	rocker_write64(rocker, DMA_DESC_ADDR(info->type), 0);

	pci_free_consistent(rocker->pdev,
			    info->size * sizeof(struct rocker_desc),
			    info->desc, info->mapaddr);
	kfree(info->desc_info);
}

static void rocker_dma_ring_pass_to_producer(const struct rocker *rocker,
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

static int rocker_dma_ring_bufs_alloc(const struct rocker *rocker,
				      const struct rocker_dma_ring_info *info,
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
		const struct rocker_desc_info *desc_info = &info->desc_info[i];

		pci_unmap_single(pdev, dma_unmap_addr(desc_info, mapaddr),
				 desc_info->data_size, direction);
		kfree(desc_info->data);
	}
	return err;
}

static void rocker_dma_ring_bufs_free(const struct rocker *rocker,
				      const struct rocker_dma_ring_info *info,
				      int direction)
{
	struct pci_dev *pdev = rocker->pdev;
	int i;

	for (i = 0; i < info->size; i++) {
		const struct rocker_desc_info *desc_info = &info->desc_info[i];
		struct rocker_desc *desc = &info->desc[i];

		desc->buf_addr = 0;
		desc->buf_size = 0;
		pci_unmap_single(pdev, dma_unmap_addr(desc_info, mapaddr),
				 desc_info->data_size, direction);
		kfree(desc_info->data);
	}
}

static int rocker_dma_cmd_ring_wait_alloc(struct rocker_desc_info *desc_info)
{
	struct rocker_wait *wait;

	wait = rocker_wait_create();
	if (!wait)
		return -ENOMEM;
	rocker_desc_cookie_ptr_set(desc_info, wait);
	return 0;
}

static void
rocker_dma_cmd_ring_wait_free(const struct rocker_desc_info *desc_info)
{
	struct rocker_wait *wait = rocker_desc_cookie_ptr_get(desc_info);

	rocker_wait_destroy(wait);
}

static int rocker_dma_cmd_ring_waits_alloc(const struct rocker *rocker)
{
	const struct rocker_dma_ring_info *cmd_ring = &rocker->cmd_ring;
	int i;
	int err;

	for (i = 0; i < cmd_ring->size; i++) {
		err = rocker_dma_cmd_ring_wait_alloc(&cmd_ring->desc_info[i]);
		if (err)
			goto rollback;
	}
	return 0;

rollback:
	for (i--; i >= 0; i--)
		rocker_dma_cmd_ring_wait_free(&cmd_ring->desc_info[i]);
	return err;
}

static void rocker_dma_cmd_ring_waits_free(const struct rocker *rocker)
{
	const struct rocker_dma_ring_info *cmd_ring = &rocker->cmd_ring;
	int i;

	for (i = 0; i < cmd_ring->size; i++)
		rocker_dma_cmd_ring_wait_free(&cmd_ring->desc_info[i]);
}

static int rocker_dma_rings_init(struct rocker *rocker)
{
	const struct pci_dev *pdev = rocker->pdev;
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

	err = rocker_dma_cmd_ring_waits_alloc(rocker);
	if (err) {
		dev_err(&pdev->dev, "failed to alloc command dma ring waits\n");
		goto err_dma_cmd_ring_waits_alloc;
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
err_dma_cmd_ring_waits_alloc:
	rocker_dma_cmd_ring_waits_free(rocker);
err_dma_cmd_ring_bufs_alloc:
	rocker_dma_ring_destroy(rocker, &rocker->cmd_ring);
	return err;
}

static void rocker_dma_rings_fini(struct rocker *rocker)
{
	rocker_dma_ring_bufs_free(rocker, &rocker->event_ring,
				  PCI_DMA_BIDIRECTIONAL);
	rocker_dma_ring_destroy(rocker, &rocker->event_ring);
	rocker_dma_cmd_ring_waits_free(rocker);
	rocker_dma_ring_bufs_free(rocker, &rocker->cmd_ring,
				  PCI_DMA_BIDIRECTIONAL);
	rocker_dma_ring_destroy(rocker, &rocker->cmd_ring);
}

static int rocker_dma_rx_ring_skb_map(const struct rocker_port *rocker_port,
				      struct rocker_desc_info *desc_info,
				      struct sk_buff *skb, size_t buf_len)
{
	const struct rocker *rocker = rocker_port->rocker;
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

static size_t rocker_port_rx_buf_len(const struct rocker_port *rocker_port)
{
	return rocker_port->dev->mtu + ETH_HLEN + ETH_FCS_LEN + VLAN_HLEN;
}

static int rocker_dma_rx_ring_skb_alloc(const struct rocker_port *rocker_port,
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
	err = rocker_dma_rx_ring_skb_map(rocker_port, desc_info, skb, buf_len);
	if (err) {
		dev_kfree_skb_any(skb);
		return err;
	}
	rocker_desc_cookie_ptr_set(desc_info, skb);
	return 0;
}

static void rocker_dma_rx_ring_skb_unmap(const struct rocker *rocker,
					 const struct rocker_tlv **attrs)
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

static void rocker_dma_rx_ring_skb_free(const struct rocker *rocker,
					const struct rocker_desc_info *desc_info)
{
	const struct rocker_tlv *attrs[ROCKER_TLV_RX_MAX + 1];
	struct sk_buff *skb = rocker_desc_cookie_ptr_get(desc_info);

	if (!skb)
		return;
	rocker_tlv_parse_desc(attrs, ROCKER_TLV_RX_MAX, desc_info);
	rocker_dma_rx_ring_skb_unmap(rocker, attrs);
	dev_kfree_skb_any(skb);
}

static int rocker_dma_rx_ring_skbs_alloc(const struct rocker_port *rocker_port)
{
	const struct rocker_dma_ring_info *rx_ring = &rocker_port->rx_ring;
	const struct rocker *rocker = rocker_port->rocker;
	int i;
	int err;

	for (i = 0; i < rx_ring->size; i++) {
		err = rocker_dma_rx_ring_skb_alloc(rocker_port,
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

static void rocker_dma_rx_ring_skbs_free(const struct rocker_port *rocker_port)
{
	const struct rocker_dma_ring_info *rx_ring = &rocker_port->rx_ring;
	const struct rocker *rocker = rocker_port->rocker;
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

	err = rocker_dma_rx_ring_skbs_alloc(rocker_port);
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

	rocker_dma_rx_ring_skbs_free(rocker_port);
	rocker_dma_ring_bufs_free(rocker, &rocker_port->rx_ring,
				  PCI_DMA_BIDIRECTIONAL);
	rocker_dma_ring_destroy(rocker, &rocker_port->rx_ring);
	rocker_dma_ring_bufs_free(rocker, &rocker_port->tx_ring,
				  PCI_DMA_TODEVICE);
	rocker_dma_ring_destroy(rocker, &rocker_port->tx_ring);
}

static void rocker_port_set_enable(const struct rocker_port *rocker_port,
				   bool enable)
{
	u64 val = rocker_read64(rocker_port->rocker, PORT_PHYS_ENABLE);

	if (enable)
		val |= 1ULL << rocker_port->pport;
	else
		val &= ~(1ULL << rocker_port->pport);
	rocker_write64(rocker_port->rocker, PORT_PHYS_ENABLE, val);
}

/********************************
 * Interrupt handler and helpers
 ********************************/

static irqreturn_t rocker_cmd_irq_handler(int irq, void *dev_id)
{
	struct rocker *rocker = dev_id;
	const struct rocker_desc_info *desc_info;
	struct rocker_wait *wait;
	u32 credits = 0;

	spin_lock(&rocker->cmd_ring_lock);
	while ((desc_info = rocker_desc_tail_get(&rocker->cmd_ring))) {
		wait = rocker_desc_cookie_ptr_get(desc_info);
		if (wait->nowait) {
			rocker_desc_gen_clear(desc_info);
		} else {
			rocker_wait_wake_up(wait);
		}
		credits++;
	}
	spin_unlock(&rocker->cmd_ring_lock);
	rocker_dma_ring_credits_set(rocker, &rocker->cmd_ring, credits);

	return IRQ_HANDLED;
}

static void rocker_port_link_up(const struct rocker_port *rocker_port)
{
	netif_carrier_on(rocker_port->dev);
	netdev_info(rocker_port->dev, "Link is up\n");
}

static void rocker_port_link_down(const struct rocker_port *rocker_port)
{
	netif_carrier_off(rocker_port->dev);
	netdev_info(rocker_port->dev, "Link is down\n");
}

static int rocker_event_link_change(const struct rocker *rocker,
				    const struct rocker_tlv *info)
{
	const struct rocker_tlv *attrs[ROCKER_TLV_EVENT_LINK_CHANGED_MAX + 1];
	unsigned int port_number;
	bool link_up;
	struct rocker_port *rocker_port;

	rocker_tlv_parse_nested(attrs, ROCKER_TLV_EVENT_LINK_CHANGED_MAX, info);
	if (!attrs[ROCKER_TLV_EVENT_LINK_CHANGED_PPORT] ||
	    !attrs[ROCKER_TLV_EVENT_LINK_CHANGED_LINKUP])
		return -EIO;
	port_number =
		rocker_tlv_get_u32(attrs[ROCKER_TLV_EVENT_LINK_CHANGED_PPORT]) - 1;
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

static int rocker_world_port_ev_mac_vlan_seen(struct rocker_port *rocker_port,
					      const unsigned char *addr,
					      __be16 vlan_id);

static int rocker_event_mac_vlan_seen(const struct rocker *rocker,
				      const struct rocker_tlv *info)
{
	const struct rocker_tlv *attrs[ROCKER_TLV_EVENT_MAC_VLAN_MAX + 1];
	unsigned int port_number;
	struct rocker_port *rocker_port;
	const unsigned char *addr;
	__be16 vlan_id;

	rocker_tlv_parse_nested(attrs, ROCKER_TLV_EVENT_MAC_VLAN_MAX, info);
	if (!attrs[ROCKER_TLV_EVENT_MAC_VLAN_PPORT] ||
	    !attrs[ROCKER_TLV_EVENT_MAC_VLAN_MAC] ||
	    !attrs[ROCKER_TLV_EVENT_MAC_VLAN_VLAN_ID])
		return -EIO;
	port_number =
		rocker_tlv_get_u32(attrs[ROCKER_TLV_EVENT_MAC_VLAN_PPORT]) - 1;
	addr = rocker_tlv_data(attrs[ROCKER_TLV_EVENT_MAC_VLAN_MAC]);
	vlan_id = rocker_tlv_get_be16(attrs[ROCKER_TLV_EVENT_MAC_VLAN_VLAN_ID]);

	if (port_number >= rocker->port_count)
		return -EINVAL;

	rocker_port = rocker->ports[port_number];
	return rocker_world_port_ev_mac_vlan_seen(rocker_port, addr, vlan_id);
}

static int rocker_event_process(const struct rocker *rocker,
				const struct rocker_desc_info *desc_info)
{
	const struct rocker_tlv *attrs[ROCKER_TLV_EVENT_MAX + 1];
	const struct rocker_tlv *info;
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
	const struct pci_dev *pdev = rocker->pdev;
	const struct rocker_desc_info *desc_info;
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

int rocker_cmd_exec(struct rocker_port *rocker_port, bool nowait,
		    rocker_cmd_prep_cb_t prepare, void *prepare_priv,
		    rocker_cmd_proc_cb_t process, void *process_priv)
{
	struct rocker *rocker = rocker_port->rocker;
	struct rocker_desc_info *desc_info;
	struct rocker_wait *wait;
	unsigned long lock_flags;
	int err;

	spin_lock_irqsave(&rocker->cmd_ring_lock, lock_flags);

	desc_info = rocker_desc_head_get(&rocker->cmd_ring);
	if (!desc_info) {
		spin_unlock_irqrestore(&rocker->cmd_ring_lock, lock_flags);
		return -EAGAIN;
	}

	wait = rocker_desc_cookie_ptr_get(desc_info);
	rocker_wait_init(wait);
	wait->nowait = nowait;

	err = prepare(rocker_port, desc_info, prepare_priv);
	if (err) {
		spin_unlock_irqrestore(&rocker->cmd_ring_lock, lock_flags);
		return err;
	}

	rocker_desc_head_set(rocker, &rocker->cmd_ring, desc_info);

	spin_unlock_irqrestore(&rocker->cmd_ring_lock, lock_flags);

	if (nowait)
		return 0;

	if (!rocker_wait_event_timeout(wait, HZ / 10))
		return -EIO;

	err = rocker_desc_err(desc_info);
	if (err)
		return err;

	if (process)
		err = process(rocker_port, desc_info, process_priv);

	rocker_desc_gen_clear(desc_info);
	return err;
}

static int
rocker_cmd_get_port_settings_prep(const struct rocker_port *rocker_port,
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
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_PPORT,
			       rocker_port->pport))
		return -EMSGSIZE;
	rocker_tlv_nest_end(desc_info, cmd_info);
	return 0;
}

static int
rocker_cmd_get_port_settings_ethtool_proc(const struct rocker_port *rocker_port,
					  const struct rocker_desc_info *desc_info,
					  void *priv)
{
	struct ethtool_cmd *ecmd = priv;
	const struct rocker_tlv *attrs[ROCKER_TLV_CMD_MAX + 1];
	const struct rocker_tlv *info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_MAX + 1];
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
rocker_cmd_get_port_settings_macaddr_proc(const struct rocker_port *rocker_port,
					  const struct rocker_desc_info *desc_info,
					  void *priv)
{
	unsigned char *macaddr = priv;
	const struct rocker_tlv *attrs[ROCKER_TLV_CMD_MAX + 1];
	const struct rocker_tlv *info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_MAX + 1];
	const struct rocker_tlv *attr;

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
rocker_cmd_get_port_settings_mode_proc(const struct rocker_port *rocker_port,
				       const struct rocker_desc_info *desc_info,
				       void *priv)
{
	u8 *p_mode = priv;
	const struct rocker_tlv *attrs[ROCKER_TLV_CMD_MAX + 1];
	const struct rocker_tlv *info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_MAX + 1];
	const struct rocker_tlv *attr;

	rocker_tlv_parse_desc(attrs, ROCKER_TLV_CMD_MAX, desc_info);
	if (!attrs[ROCKER_TLV_CMD_INFO])
		return -EIO;

	rocker_tlv_parse_nested(info_attrs, ROCKER_TLV_CMD_PORT_SETTINGS_MAX,
				attrs[ROCKER_TLV_CMD_INFO]);
	attr = info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_MODE];
	if (!attr)
		return -EIO;

	*p_mode = rocker_tlv_get_u8(info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_MODE]);
	return 0;
}

struct port_name {
	char *buf;
	size_t len;
};

static int
rocker_cmd_get_port_settings_phys_name_proc(const struct rocker_port *rocker_port,
					    const struct rocker_desc_info *desc_info,
					    void *priv)
{
	const struct rocker_tlv *info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_MAX + 1];
	const struct rocker_tlv *attrs[ROCKER_TLV_CMD_MAX + 1];
	struct port_name *name = priv;
	const struct rocker_tlv *attr;
	size_t i, j, len;
	const char *str;

	rocker_tlv_parse_desc(attrs, ROCKER_TLV_CMD_MAX, desc_info);
	if (!attrs[ROCKER_TLV_CMD_INFO])
		return -EIO;

	rocker_tlv_parse_nested(info_attrs, ROCKER_TLV_CMD_PORT_SETTINGS_MAX,
				attrs[ROCKER_TLV_CMD_INFO]);
	attr = info_attrs[ROCKER_TLV_CMD_PORT_SETTINGS_PHYS_NAME];
	if (!attr)
		return -EIO;

	len = min_t(size_t, rocker_tlv_len(attr), name->len);
	str = rocker_tlv_data(attr);

	/* make sure name only contains alphanumeric characters */
	for (i = j = 0; i < len; ++i) {
		if (isalnum(str[i])) {
			name->buf[j] = str[i];
			j++;
		}
	}

	if (j == 0)
		return -EIO;

	name->buf[j] = '\0';

	return 0;
}

static int
rocker_cmd_set_port_settings_ethtool_prep(const struct rocker_port *rocker_port,
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
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_PPORT,
			       rocker_port->pport))
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
rocker_cmd_set_port_settings_macaddr_prep(const struct rocker_port *rocker_port,
					  struct rocker_desc_info *desc_info,
					  void *priv)
{
	const unsigned char *macaddr = priv;
	struct rocker_tlv *cmd_info;

	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_CMD_TYPE,
			       ROCKER_TLV_CMD_TYPE_SET_PORT_SETTINGS))
		return -EMSGSIZE;
	cmd_info = rocker_tlv_nest_start(desc_info, ROCKER_TLV_CMD_INFO);
	if (!cmd_info)
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_PPORT,
			       rocker_port->pport))
		return -EMSGSIZE;
	if (rocker_tlv_put(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_MACADDR,
			   ETH_ALEN, macaddr))
		return -EMSGSIZE;
	rocker_tlv_nest_end(desc_info, cmd_info);
	return 0;
}

static int
rocker_cmd_set_port_settings_mtu_prep(const struct rocker_port *rocker_port,
				      struct rocker_desc_info *desc_info,
				      void *priv)
{
	int mtu = *(int *)priv;
	struct rocker_tlv *cmd_info;

	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_CMD_TYPE,
			       ROCKER_TLV_CMD_TYPE_SET_PORT_SETTINGS))
		return -EMSGSIZE;
	cmd_info = rocker_tlv_nest_start(desc_info, ROCKER_TLV_CMD_INFO);
	if (!cmd_info)
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_PPORT,
			       rocker_port->pport))
		return -EMSGSIZE;
	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_MTU,
			       mtu))
		return -EMSGSIZE;
	rocker_tlv_nest_end(desc_info, cmd_info);
	return 0;
}

static int
rocker_cmd_set_port_learning_prep(const struct rocker_port *rocker_port,
				  struct rocker_desc_info *desc_info,
				  void *priv)
{
	bool learning = *(bool *)priv;
	struct rocker_tlv *cmd_info;

	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_CMD_TYPE,
			       ROCKER_TLV_CMD_TYPE_SET_PORT_SETTINGS))
		return -EMSGSIZE;
	cmd_info = rocker_tlv_nest_start(desc_info, ROCKER_TLV_CMD_INFO);
	if (!cmd_info)
		return -EMSGSIZE;
	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_PPORT,
			       rocker_port->pport))
		return -EMSGSIZE;
	if (rocker_tlv_put_u8(desc_info, ROCKER_TLV_CMD_PORT_SETTINGS_LEARNING,
			      learning))
		return -EMSGSIZE;
	rocker_tlv_nest_end(desc_info, cmd_info);
	return 0;
}

static int rocker_cmd_get_port_settings_ethtool(struct rocker_port *rocker_port,
						struct ethtool_cmd *ecmd)
{
	return rocker_cmd_exec(rocker_port, false,
			       rocker_cmd_get_port_settings_prep, NULL,
			       rocker_cmd_get_port_settings_ethtool_proc,
			       ecmd);
}

static int rocker_cmd_get_port_settings_macaddr(struct rocker_port *rocker_port,
						unsigned char *macaddr)
{
	return rocker_cmd_exec(rocker_port, false,
			       rocker_cmd_get_port_settings_prep, NULL,
			       rocker_cmd_get_port_settings_macaddr_proc,
			       macaddr);
}

static int rocker_cmd_get_port_settings_mode(struct rocker_port *rocker_port,
					     u8 *p_mode)
{
	return rocker_cmd_exec(rocker_port, false,
			       rocker_cmd_get_port_settings_prep, NULL,
			       rocker_cmd_get_port_settings_mode_proc, p_mode);
}

static int rocker_cmd_set_port_settings_ethtool(struct rocker_port *rocker_port,
						struct ethtool_cmd *ecmd)
{
	return rocker_cmd_exec(rocker_port, false,
			       rocker_cmd_set_port_settings_ethtool_prep,
			       ecmd, NULL, NULL);
}

static int rocker_cmd_set_port_settings_macaddr(struct rocker_port *rocker_port,
						unsigned char *macaddr)
{
	return rocker_cmd_exec(rocker_port, false,
			       rocker_cmd_set_port_settings_macaddr_prep,
			       macaddr, NULL, NULL);
}

static int rocker_cmd_set_port_settings_mtu(struct rocker_port *rocker_port,
					    int mtu)
{
	return rocker_cmd_exec(rocker_port, false,
			       rocker_cmd_set_port_settings_mtu_prep,
			       &mtu, NULL, NULL);
}

int rocker_port_set_learning(struct rocker_port *rocker_port,
			     bool learning)
{
	return rocker_cmd_exec(rocker_port, false,
			       rocker_cmd_set_port_learning_prep,
			       &learning, NULL, NULL);
}

/**********************
 * Worlds manipulation
 **********************/

static struct rocker_world_ops *rocker_world_ops[] = {
	&rocker_ofdpa_ops,
};

#define ROCKER_WORLD_OPS_LEN ARRAY_SIZE(rocker_world_ops)

static struct rocker_world_ops *rocker_world_ops_find(u8 mode)
{
	int i;

	for (i = 0; i < ROCKER_WORLD_OPS_LEN; i++)
		if (rocker_world_ops[i]->mode == mode)
			return rocker_world_ops[i];
	return NULL;
}

static int rocker_world_init(struct rocker *rocker, u8 mode)
{
	struct rocker_world_ops *wops;
	int err;

	wops = rocker_world_ops_find(mode);
	if (!wops) {
		dev_err(&rocker->pdev->dev, "port mode \"%d\" is not supported\n",
			mode);
		return -EINVAL;
	}
	rocker->wops = wops;
	rocker->wpriv = kzalloc(wops->priv_size, GFP_KERNEL);
	if (!rocker->wpriv)
		return -ENOMEM;
	if (!wops->init)
		return 0;
	err = wops->init(rocker);
	if (err)
		kfree(rocker->wpriv);
	return err;
}

static void rocker_world_fini(struct rocker *rocker)
{
	struct rocker_world_ops *wops = rocker->wops;

	if (!wops || !wops->fini)
		return;
	wops->fini(rocker);
	kfree(rocker->wpriv);
}

static int rocker_world_check_init(struct rocker_port *rocker_port)
{
	struct rocker *rocker = rocker_port->rocker;
	u8 mode;
	int err;

	err = rocker_cmd_get_port_settings_mode(rocker_port, &mode);
	if (err) {
		dev_err(&rocker->pdev->dev, "failed to get port mode\n");
		return err;
	}
	if (rocker->wops) {
		if (rocker->wops->mode != mode) {
			dev_err(&rocker->pdev->dev, "hardware has ports in different worlds, which is not supported\n");
			return -EINVAL;
		}
		return 0;
	}
	return rocker_world_init(rocker, mode);
}

static int rocker_world_port_pre_init(struct rocker_port *rocker_port)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;
	int err;

	rocker_port->wpriv = kzalloc(wops->port_priv_size, GFP_KERNEL);
	if (!rocker_port->wpriv)
		return -ENOMEM;
	if (!wops->port_pre_init)
		return 0;
	err = wops->port_pre_init(rocker_port);
	if (err)
		kfree(rocker_port->wpriv);
	return 0;
}

static int rocker_world_port_init(struct rocker_port *rocker_port)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_init)
		return 0;
	return wops->port_init(rocker_port);
}

static void rocker_world_port_fini(struct rocker_port *rocker_port)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_fini)
		return;
	wops->port_fini(rocker_port);
}

static void rocker_world_port_post_fini(struct rocker_port *rocker_port)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_post_fini)
		return;
	wops->port_post_fini(rocker_port);
	kfree(rocker_port->wpriv);
}

static int rocker_world_port_open(struct rocker_port *rocker_port)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_open)
		return 0;
	return wops->port_open(rocker_port);
}

static void rocker_world_port_stop(struct rocker_port *rocker_port)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_stop)
		return;
	wops->port_stop(rocker_port);
}

static int rocker_world_port_attr_stp_state_set(struct rocker_port *rocker_port,
						u8 state,
						struct switchdev_trans *trans)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_attr_stp_state_set)
		return -EOPNOTSUPP;
	return wops->port_attr_stp_state_set(rocker_port, state, trans);
}

static int
rocker_world_port_attr_bridge_flags_set(struct rocker_port *rocker_port,
					unsigned long brport_flags,
					struct switchdev_trans *trans)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_attr_bridge_flags_set)
		return -EOPNOTSUPP;
	return wops->port_attr_bridge_flags_set(rocker_port, brport_flags,
						trans);
}

static int
rocker_world_port_attr_bridge_flags_get(const struct rocker_port *rocker_port,
					unsigned long *p_brport_flags)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_attr_bridge_flags_get)
		return -EOPNOTSUPP;
	return wops->port_attr_bridge_flags_get(rocker_port, p_brport_flags);
}

static int
rocker_world_port_attr_bridge_ageing_time_set(struct rocker_port *rocker_port,
					      u32 ageing_time,
					      struct switchdev_trans *trans)

{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_attr_bridge_ageing_time_set)
		return -EOPNOTSUPP;
	return wops->port_attr_bridge_ageing_time_set(rocker_port, ageing_time,
						      trans);
}

static int
rocker_world_port_obj_vlan_add(struct rocker_port *rocker_port,
			       const struct switchdev_obj_port_vlan *vlan,
			       struct switchdev_trans *trans)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_obj_vlan_add)
		return -EOPNOTSUPP;
	return wops->port_obj_vlan_add(rocker_port, vlan, trans);
}

static int
rocker_world_port_obj_vlan_del(struct rocker_port *rocker_port,
			       const struct switchdev_obj_port_vlan *vlan)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_obj_vlan_del)
		return -EOPNOTSUPP;
	return wops->port_obj_vlan_del(rocker_port, vlan);
}

static int
rocker_world_port_obj_vlan_dump(const struct rocker_port *rocker_port,
				struct switchdev_obj_port_vlan *vlan,
				switchdev_obj_dump_cb_t *cb)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_obj_vlan_dump)
		return -EOPNOTSUPP;
	return wops->port_obj_vlan_dump(rocker_port, vlan, cb);
}

static int
rocker_world_port_obj_fdb_add(struct rocker_port *rocker_port,
			      const struct switchdev_obj_port_fdb *fdb,
			      struct switchdev_trans *trans)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_obj_fdb_add)
		return -EOPNOTSUPP;
	return wops->port_obj_fdb_add(rocker_port, fdb, trans);
}

static int
rocker_world_port_obj_fdb_del(struct rocker_port *rocker_port,
			      const struct switchdev_obj_port_fdb *fdb)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_obj_fdb_del)
		return -EOPNOTSUPP;
	return wops->port_obj_fdb_del(rocker_port, fdb);
}

static int
rocker_world_port_obj_fdb_dump(const struct rocker_port *rocker_port,
			       struct switchdev_obj_port_fdb *fdb,
			       switchdev_obj_dump_cb_t *cb)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_obj_fdb_dump)
		return -EOPNOTSUPP;
	return wops->port_obj_fdb_dump(rocker_port, fdb, cb);
}

static int rocker_world_port_master_linked(struct rocker_port *rocker_port,
					   struct net_device *master)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_master_linked)
		return -EOPNOTSUPP;
	return wops->port_master_linked(rocker_port, master);
}

static int rocker_world_port_master_unlinked(struct rocker_port *rocker_port,
					     struct net_device *master)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_master_unlinked)
		return -EOPNOTSUPP;
	return wops->port_master_unlinked(rocker_port, master);
}

static int rocker_world_port_neigh_update(struct rocker_port *rocker_port,
					  struct neighbour *n)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_neigh_update)
		return -EOPNOTSUPP;
	return wops->port_neigh_update(rocker_port, n);
}

static int rocker_world_port_neigh_destroy(struct rocker_port *rocker_port,
					   struct neighbour *n)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_neigh_destroy)
		return -EOPNOTSUPP;
	return wops->port_neigh_destroy(rocker_port, n);
}

static int rocker_world_port_ev_mac_vlan_seen(struct rocker_port *rocker_port,
					      const unsigned char *addr,
					      __be16 vlan_id)
{
	struct rocker_world_ops *wops = rocker_port->rocker->wops;

	if (!wops->port_ev_mac_vlan_seen)
		return -EOPNOTSUPP;
	return wops->port_ev_mac_vlan_seen(rocker_port, addr, vlan_id);
}

static int rocker_world_fib4_add(struct rocker *rocker,
				 const struct fib_entry_notifier_info *fen_info)
{
	struct rocker_world_ops *wops = rocker->wops;

	if (!wops->fib4_add)
		return 0;
	return wops->fib4_add(rocker, fen_info);
}

static int rocker_world_fib4_del(struct rocker *rocker,
				 const struct fib_entry_notifier_info *fen_info)
{
	struct rocker_world_ops *wops = rocker->wops;

	if (!wops->fib4_del)
		return 0;
	return wops->fib4_del(rocker, fen_info);
}

static void rocker_world_fib4_abort(struct rocker *rocker)
{
	struct rocker_world_ops *wops = rocker->wops;

	if (wops->fib4_abort)
		wops->fib4_abort(rocker);
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

	err = rocker_world_port_open(rocker_port);
	if (err) {
		netdev_err(rocker_port->dev, "cannot open port in world\n");
		goto err_world_port_open;
	}

	napi_enable(&rocker_port->napi_tx);
	napi_enable(&rocker_port->napi_rx);
	if (!dev->proto_down)
		rocker_port_set_enable(rocker_port, true);
	netif_start_queue(dev);
	return 0;

err_world_port_open:
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
	rocker_world_port_stop(rocker_port);
	free_irq(rocker_msix_rx_vector(rocker_port), rocker_port);
	free_irq(rocker_msix_tx_vector(rocker_port), rocker_port);
	rocker_port_dma_rings_fini(rocker_port);

	return 0;
}

static void rocker_tx_desc_frags_unmap(const struct rocker_port *rocker_port,
				       const struct rocker_desc_info *desc_info)
{
	const struct rocker *rocker = rocker_port->rocker;
	struct pci_dev *pdev = rocker->pdev;
	const struct rocker_tlv *attrs[ROCKER_TLV_TX_MAX + 1];
	struct rocker_tlv *attr;
	int rem;

	rocker_tlv_parse_desc(attrs, ROCKER_TLV_TX_MAX, desc_info);
	if (!attrs[ROCKER_TLV_TX_FRAGS])
		return;
	rocker_tlv_for_each_nested(attr, attrs[ROCKER_TLV_TX_FRAGS], rem) {
		const struct rocker_tlv *frag_attrs[ROCKER_TLV_TX_FRAG_ATTR_MAX + 1];
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

static int rocker_tx_desc_frag_map_put(const struct rocker_port *rocker_port,
				       struct rocker_desc_info *desc_info,
				       char *buf, size_t buf_len)
{
	const struct rocker *rocker = rocker_port->rocker;
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
	if (skb_shinfo(skb)->nr_frags > ROCKER_TX_FRAGS_MAX) {
		err = skb_linearize(skb);
		if (err)
			goto unmap_frags;
	}

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
	dev->stats.tx_dropped++;

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

static int rocker_port_change_mtu(struct net_device *dev, int new_mtu)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	int running = netif_running(dev);
	int err;

#define ROCKER_PORT_MIN_MTU	68
#define ROCKER_PORT_MAX_MTU	9000

	if (new_mtu < ROCKER_PORT_MIN_MTU || new_mtu > ROCKER_PORT_MAX_MTU)
		return -EINVAL;

	if (running)
		rocker_port_stop(dev);

	netdev_info(dev, "MTU change from %d to %d\n", dev->mtu, new_mtu);
	dev->mtu = new_mtu;

	err = rocker_cmd_set_port_settings_mtu(rocker_port, new_mtu);
	if (err)
		return err;

	if (running)
		err = rocker_port_open(dev);

	return err;
}

static int rocker_port_get_phys_port_name(struct net_device *dev,
					  char *buf, size_t len)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	struct port_name name = { .buf = buf, .len = len };
	int err;

	err = rocker_cmd_exec(rocker_port, false,
			      rocker_cmd_get_port_settings_prep, NULL,
			      rocker_cmd_get_port_settings_phys_name_proc,
			      &name);

	return err ? -EOPNOTSUPP : 0;
}

static int rocker_port_change_proto_down(struct net_device *dev,
					 bool proto_down)
{
	struct rocker_port *rocker_port = netdev_priv(dev);

	if (rocker_port->dev->flags & IFF_UP)
		rocker_port_set_enable(rocker_port, !proto_down);
	rocker_port->dev->proto_down = proto_down;
	return 0;
}

static void rocker_port_neigh_destroy(struct net_device *dev,
				      struct neighbour *n)
{
	struct rocker_port *rocker_port = netdev_priv(n->dev);
	int err;

	err = rocker_world_port_neigh_destroy(rocker_port, n);
	if (err)
		netdev_warn(rocker_port->dev, "failed to handle neigh destroy (err %d)\n",
			    err);
}

static const struct net_device_ops rocker_port_netdev_ops = {
	.ndo_open			= rocker_port_open,
	.ndo_stop			= rocker_port_stop,
	.ndo_start_xmit			= rocker_port_xmit,
	.ndo_set_mac_address		= rocker_port_set_mac_address,
	.ndo_change_mtu			= rocker_port_change_mtu,
	.ndo_bridge_getlink		= switchdev_port_bridge_getlink,
	.ndo_bridge_setlink		= switchdev_port_bridge_setlink,
	.ndo_bridge_dellink		= switchdev_port_bridge_dellink,
	.ndo_fdb_add			= switchdev_port_fdb_add,
	.ndo_fdb_del			= switchdev_port_fdb_del,
	.ndo_fdb_dump			= switchdev_port_fdb_dump,
	.ndo_get_phys_port_name		= rocker_port_get_phys_port_name,
	.ndo_change_proto_down		= rocker_port_change_proto_down,
	.ndo_neigh_destroy		= rocker_port_neigh_destroy,
};

/********************
 * swdev interface
 ********************/

static int rocker_port_attr_get(struct net_device *dev,
				struct switchdev_attr *attr)
{
	const struct rocker_port *rocker_port = netdev_priv(dev);
	const struct rocker *rocker = rocker_port->rocker;
	int err = 0;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_PARENT_ID:
		attr->u.ppid.id_len = sizeof(rocker->hw.id);
		memcpy(&attr->u.ppid.id, &rocker->hw.id, attr->u.ppid.id_len);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		err = rocker_world_port_attr_bridge_flags_get(rocker_port,
							      &attr->u.brport_flags);
		break;
	default:
		return -EOPNOTSUPP;
	}

	return err;
}

static int rocker_port_attr_set(struct net_device *dev,
				const struct switchdev_attr *attr,
				struct switchdev_trans *trans)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	int err = 0;

	switch (attr->id) {
	case SWITCHDEV_ATTR_ID_PORT_STP_STATE:
		err = rocker_world_port_attr_stp_state_set(rocker_port,
							   attr->u.stp_state,
							   trans);
		break;
	case SWITCHDEV_ATTR_ID_PORT_BRIDGE_FLAGS:
		err = rocker_world_port_attr_bridge_flags_set(rocker_port,
							      attr->u.brport_flags,
							      trans);
		break;
	case SWITCHDEV_ATTR_ID_BRIDGE_AGEING_TIME:
		err = rocker_world_port_attr_bridge_ageing_time_set(rocker_port,
								    attr->u.ageing_time,
								    trans);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int rocker_port_obj_add(struct net_device *dev,
			       const struct switchdev_obj *obj,
			       struct switchdev_trans *trans)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	int err = 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = rocker_world_port_obj_vlan_add(rocker_port,
						     SWITCHDEV_OBJ_PORT_VLAN(obj),
						     trans);
		break;
	case SWITCHDEV_OBJ_ID_PORT_FDB:
		err = rocker_world_port_obj_fdb_add(rocker_port,
						    SWITCHDEV_OBJ_PORT_FDB(obj),
						    trans);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int rocker_port_obj_del(struct net_device *dev,
			       const struct switchdev_obj *obj)
{
	struct rocker_port *rocker_port = netdev_priv(dev);
	int err = 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = rocker_world_port_obj_vlan_del(rocker_port,
						     SWITCHDEV_OBJ_PORT_VLAN(obj));
		break;
	case SWITCHDEV_OBJ_ID_PORT_FDB:
		err = rocker_world_port_obj_fdb_del(rocker_port,
						    SWITCHDEV_OBJ_PORT_FDB(obj));
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static int rocker_port_obj_dump(struct net_device *dev,
				struct switchdev_obj *obj,
				switchdev_obj_dump_cb_t *cb)
{
	const struct rocker_port *rocker_port = netdev_priv(dev);
	int err = 0;

	switch (obj->id) {
	case SWITCHDEV_OBJ_ID_PORT_FDB:
		err = rocker_world_port_obj_fdb_dump(rocker_port,
						     SWITCHDEV_OBJ_PORT_FDB(obj),
						     cb);
		break;
	case SWITCHDEV_OBJ_ID_PORT_VLAN:
		err = rocker_world_port_obj_vlan_dump(rocker_port,
						      SWITCHDEV_OBJ_PORT_VLAN(obj),
						      cb);
		break;
	default:
		err = -EOPNOTSUPP;
		break;
	}

	return err;
}

static const struct switchdev_ops rocker_port_switchdev_ops = {
	.switchdev_port_attr_get	= rocker_port_attr_get,
	.switchdev_port_attr_set	= rocker_port_attr_set,
	.switchdev_port_obj_add		= rocker_port_obj_add,
	.switchdev_port_obj_del		= rocker_port_obj_del,
	.switchdev_port_obj_dump	= rocker_port_obj_dump,
};

static int rocker_router_fib_event(struct notifier_block *nb,
				   unsigned long event, void *ptr)
{
	struct rocker *rocker = container_of(nb, struct rocker, fib_nb);
	struct fib_entry_notifier_info *fen_info = ptr;
	int err;

	switch (event) {
	case FIB_EVENT_ENTRY_ADD:
		err = rocker_world_fib4_add(rocker, fen_info);
		if (err)
			rocker_world_fib4_abort(rocker);
		else
		break;
	case FIB_EVENT_ENTRY_DEL:
		rocker_world_fib4_del(rocker, fen_info);
		break;
	case FIB_EVENT_RULE_ADD: /* fall through */
	case FIB_EVENT_RULE_DEL:
		rocker_world_fib4_abort(rocker);
		break;
	}
	return NOTIFY_DONE;
}

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

static struct rocker_port_stats {
	char str[ETH_GSTRING_LEN];
	int type;
} rocker_port_stats[] = {
	{ "rx_packets", ROCKER_TLV_CMD_PORT_STATS_RX_PKTS,    },
	{ "rx_bytes",   ROCKER_TLV_CMD_PORT_STATS_RX_BYTES,   },
	{ "rx_dropped", ROCKER_TLV_CMD_PORT_STATS_RX_DROPPED, },
	{ "rx_errors",  ROCKER_TLV_CMD_PORT_STATS_RX_ERRORS,  },

	{ "tx_packets", ROCKER_TLV_CMD_PORT_STATS_TX_PKTS,    },
	{ "tx_bytes",   ROCKER_TLV_CMD_PORT_STATS_TX_BYTES,   },
	{ "tx_dropped", ROCKER_TLV_CMD_PORT_STATS_TX_DROPPED, },
	{ "tx_errors",  ROCKER_TLV_CMD_PORT_STATS_TX_ERRORS,  },
};

#define ROCKER_PORT_STATS_LEN  ARRAY_SIZE(rocker_port_stats)

static void rocker_port_get_strings(struct net_device *netdev, u32 stringset,
				    u8 *data)
{
	u8 *p = data;
	int i;

	switch (stringset) {
	case ETH_SS_STATS:
		for (i = 0; i < ARRAY_SIZE(rocker_port_stats); i++) {
			memcpy(p, rocker_port_stats[i].str, ETH_GSTRING_LEN);
			p += ETH_GSTRING_LEN;
		}
		break;
	}
}

static int
rocker_cmd_get_port_stats_prep(const struct rocker_port *rocker_port,
			       struct rocker_desc_info *desc_info,
			       void *priv)
{
	struct rocker_tlv *cmd_stats;

	if (rocker_tlv_put_u16(desc_info, ROCKER_TLV_CMD_TYPE,
			       ROCKER_TLV_CMD_TYPE_GET_PORT_STATS))
		return -EMSGSIZE;

	cmd_stats = rocker_tlv_nest_start(desc_info, ROCKER_TLV_CMD_INFO);
	if (!cmd_stats)
		return -EMSGSIZE;

	if (rocker_tlv_put_u32(desc_info, ROCKER_TLV_CMD_PORT_STATS_PPORT,
			       rocker_port->pport))
		return -EMSGSIZE;

	rocker_tlv_nest_end(desc_info, cmd_stats);

	return 0;
}

static int
rocker_cmd_get_port_stats_ethtool_proc(const struct rocker_port *rocker_port,
				       const struct rocker_desc_info *desc_info,
				       void *priv)
{
	const struct rocker_tlv *attrs[ROCKER_TLV_CMD_MAX + 1];
	const struct rocker_tlv *stats_attrs[ROCKER_TLV_CMD_PORT_STATS_MAX + 1];
	const struct rocker_tlv *pattr;
	u32 pport;
	u64 *data = priv;
	int i;

	rocker_tlv_parse_desc(attrs, ROCKER_TLV_CMD_MAX, desc_info);

	if (!attrs[ROCKER_TLV_CMD_INFO])
		return -EIO;

	rocker_tlv_parse_nested(stats_attrs, ROCKER_TLV_CMD_PORT_STATS_MAX,
				attrs[ROCKER_TLV_CMD_INFO]);

	if (!stats_attrs[ROCKER_TLV_CMD_PORT_STATS_PPORT])
		return -EIO;

	pport = rocker_tlv_get_u32(stats_attrs[ROCKER_TLV_CMD_PORT_STATS_PPORT]);
	if (pport != rocker_port->pport)
		return -EIO;

	for (i = 0; i < ARRAY_SIZE(rocker_port_stats); i++) {
		pattr = stats_attrs[rocker_port_stats[i].type];
		if (!pattr)
			continue;

		data[i] = rocker_tlv_get_u64(pattr);
	}

	return 0;
}

static int rocker_cmd_get_port_stats_ethtool(struct rocker_port *rocker_port,
					     void *priv)
{
	return rocker_cmd_exec(rocker_port, false,
			       rocker_cmd_get_port_stats_prep, NULL,
			       rocker_cmd_get_port_stats_ethtool_proc,
			       priv);
}

static void rocker_port_get_stats(struct net_device *dev,
				  struct ethtool_stats *stats, u64 *data)
{
	struct rocker_port *rocker_port = netdev_priv(dev);

	if (rocker_cmd_get_port_stats_ethtool(rocker_port, data) != 0) {
		int i;

		for (i = 0; i < ARRAY_SIZE(rocker_port_stats); ++i)
			data[i] = 0;
	}
}

static int rocker_port_get_sset_count(struct net_device *netdev, int sset)
{
	switch (sset) {
	case ETH_SS_STATS:
		return ROCKER_PORT_STATS_LEN;
	default:
		return -EOPNOTSUPP;
	}
}

static const struct ethtool_ops rocker_port_ethtool_ops = {
	.get_settings		= rocker_port_get_settings,
	.set_settings		= rocker_port_set_settings,
	.get_drvinfo		= rocker_port_get_drvinfo,
	.get_link		= ethtool_op_get_link,
	.get_strings		= rocker_port_get_strings,
	.get_ethtool_stats	= rocker_port_get_stats,
	.get_sset_count		= rocker_port_get_sset_count,
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
	const struct rocker *rocker = rocker_port->rocker;
	const struct rocker_desc_info *desc_info;
	u32 credits = 0;
	int err;

	/* Cleanup tx descriptors */
	while ((desc_info = rocker_desc_tail_get(&rocker_port->tx_ring))) {
		struct sk_buff *skb;

		err = rocker_desc_err(desc_info);
		if (err && net_ratelimit())
			netdev_err(rocker_port->dev, "tx desc received with err %d\n",
				   err);
		rocker_tx_desc_frags_unmap(rocker_port, desc_info);

		skb = rocker_desc_cookie_ptr_get(desc_info);
		if (err == 0) {
			rocker_port->dev->stats.tx_packets++;
			rocker_port->dev->stats.tx_bytes += skb->len;
		} else {
			rocker_port->dev->stats.tx_errors++;
		}

		dev_kfree_skb_any(skb);
		credits++;
	}

	if (credits && netif_queue_stopped(rocker_port->dev))
		netif_wake_queue(rocker_port->dev);

	napi_complete(napi);
	rocker_dma_ring_credits_set(rocker, &rocker_port->tx_ring, credits);

	return 0;
}

static int rocker_port_rx_proc(const struct rocker *rocker,
			       const struct rocker_port *rocker_port,
			       struct rocker_desc_info *desc_info)
{
	const struct rocker_tlv *attrs[ROCKER_TLV_RX_MAX + 1];
	struct sk_buff *skb = rocker_desc_cookie_ptr_get(desc_info);
	size_t rx_len;
	u16 rx_flags = 0;

	if (!skb)
		return -ENOENT;

	rocker_tlv_parse_desc(attrs, ROCKER_TLV_RX_MAX, desc_info);
	if (!attrs[ROCKER_TLV_RX_FRAG_LEN])
		return -EINVAL;
	if (attrs[ROCKER_TLV_RX_FLAGS])
		rx_flags = rocker_tlv_get_u16(attrs[ROCKER_TLV_RX_FLAGS]);

	rocker_dma_rx_ring_skb_unmap(rocker, attrs);

	rx_len = rocker_tlv_get_u16(attrs[ROCKER_TLV_RX_FRAG_LEN]);
	skb_put(skb, rx_len);
	skb->protocol = eth_type_trans(skb, rocker_port->dev);

	if (rx_flags & ROCKER_RX_FLAGS_FWD_OFFLOAD)
		skb->offload_fwd_mark = 1;

	rocker_port->dev->stats.rx_packets++;
	rocker_port->dev->stats.rx_bytes += skb->len;

	netif_receive_skb(skb);

	return rocker_dma_rx_ring_skb_alloc(rocker_port, desc_info);
}

static struct rocker_port *rocker_port_napi_rx_get(struct napi_struct *napi)
{
	return container_of(napi, struct rocker_port, napi_rx);
}

static int rocker_port_poll_rx(struct napi_struct *napi, int budget)
{
	struct rocker_port *rocker_port = rocker_port_napi_rx_get(napi);
	const struct rocker *rocker = rocker_port->rocker;
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
		if (err)
			rocker_port->dev->stats.rx_errors++;

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

static void rocker_carrier_init(const struct rocker_port *rocker_port)
{
	const struct rocker *rocker = rocker_port->rocker;
	u64 link_status = rocker_read64(rocker, PORT_PHYS_LINK_STATUS);
	bool link_up;

	link_up = link_status & (1 << rocker_port->pport);
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
		if (!rocker_port)
			continue;
		rocker_world_port_fini(rocker_port);
		unregister_netdev(rocker_port->dev);
		rocker_world_port_post_fini(rocker_port);
		free_netdev(rocker_port->dev);
	}
	rocker_world_fini(rocker);
	kfree(rocker->ports);
}

static void rocker_port_dev_addr_init(struct rocker_port *rocker_port)
{
	const struct rocker *rocker = rocker_port->rocker;
	const struct pci_dev *pdev = rocker->pdev;
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
	const struct pci_dev *pdev = rocker->pdev;
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
	rocker_port->pport = port_number + 1;

	err = rocker_world_check_init(rocker_port);
	if (err) {
		dev_err(&pdev->dev, "world init failed\n");
		goto err_world_check_init;
	}

	rocker_port_dev_addr_init(rocker_port);
	dev->netdev_ops = &rocker_port_netdev_ops;
	dev->ethtool_ops = &rocker_port_ethtool_ops;
	dev->switchdev_ops = &rocker_port_switchdev_ops;
	netif_tx_napi_add(dev, &rocker_port->napi_tx, rocker_port_poll_tx,
			  NAPI_POLL_WEIGHT);
	netif_napi_add(dev, &rocker_port->napi_rx, rocker_port_poll_rx,
		       NAPI_POLL_WEIGHT);
	rocker_carrier_init(rocker_port);

	dev->features |= NETIF_F_NETNS_LOCAL | NETIF_F_SG;

	err = rocker_world_port_pre_init(rocker_port);
	if (err) {
		dev_err(&pdev->dev, "port world pre-init failed\n");
		goto err_world_port_pre_init;
	}
	err = register_netdev(dev);
	if (err) {
		dev_err(&pdev->dev, "register_netdev failed\n");
		goto err_register_netdev;
	}
	rocker->ports[port_number] = rocker_port;

	err = rocker_world_port_init(rocker_port);
	if (err) {
		dev_err(&pdev->dev, "port world init failed\n");
		goto err_world_port_init;
	}

	return 0;

err_world_port_init:
	rocker->ports[port_number] = NULL;
	unregister_netdev(dev);
err_register_netdev:
	rocker_world_port_post_fini(rocker_port);
err_world_port_pre_init:
err_world_check_init:
	free_netdev(dev);
	return err;
}

static int rocker_probe_ports(struct rocker *rocker)
{
	int i;
	size_t alloc_size;
	int err;

	alloc_size = sizeof(struct rocker_port *) * rocker->port_count;
	rocker->ports = kzalloc(alloc_size, GFP_KERNEL);
	if (!rocker->ports)
		return -ENOMEM;
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

static void rocker_msix_fini(const struct rocker *rocker)
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
		err = -EINVAL;
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

	rocker->fib_nb.notifier_call = rocker_router_fib_event;
	register_fib_notifier(&rocker->fib_nb);

	dev_info(&pdev->dev, "Rocker switch with id %*phN\n",
		 (int)sizeof(rocker->hw.id), &rocker->hw.id);

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

	unregister_fib_notifier(&rocker->fib_nb);
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

static bool rocker_port_dev_check(const struct net_device *dev)
{
	return dev->netdev_ops == &rocker_port_netdev_ops;
}

static bool rocker_port_dev_check_under(const struct net_device *dev,
					struct rocker *rocker)
{
	struct rocker_port *rocker_port;

	if (!rocker_port_dev_check(dev))
		return false;

	rocker_port = netdev_priv(dev);
	if (rocker_port->rocker != rocker)
		return false;

	return true;
}

struct rocker_port *rocker_port_dev_lower_find(struct net_device *dev,
					       struct rocker *rocker)
{
	struct net_device *lower_dev;
	struct list_head *iter;

	if (rocker_port_dev_check_under(dev, rocker))
		return netdev_priv(dev);

	netdev_for_each_all_lower_dev(dev, lower_dev, iter) {
		if (rocker_port_dev_check_under(lower_dev, rocker))
			return netdev_priv(lower_dev);
	}
	return NULL;
}

static int rocker_netdevice_event(struct notifier_block *unused,
				  unsigned long event, void *ptr)
{
	struct net_device *dev = netdev_notifier_info_to_dev(ptr);
	struct netdev_notifier_changeupper_info *info;
	struct rocker_port *rocker_port;
	int err;

	if (!rocker_port_dev_check(dev))
		return NOTIFY_DONE;

	switch (event) {
	case NETDEV_CHANGEUPPER:
		info = ptr;
		if (!info->master)
			goto out;
		rocker_port = netdev_priv(dev);
		if (info->linking) {
			err = rocker_world_port_master_linked(rocker_port,
							      info->upper_dev);
			if (err)
				netdev_warn(dev, "failed to reflect master linked (err %d)\n",
					    err);
		} else {
			err = rocker_world_port_master_unlinked(rocker_port,
								info->upper_dev);
			if (err)
				netdev_warn(dev, "failed to reflect master unlinked (err %d)\n",
					    err);
		}
	}
out:
	return NOTIFY_DONE;
}

static struct notifier_block rocker_netdevice_nb __read_mostly = {
	.notifier_call = rocker_netdevice_event,
};

/************************************
 * Net event notifier event handler
 ************************************/

static int rocker_netevent_event(struct notifier_block *unused,
				 unsigned long event, void *ptr)
{
	struct rocker_port *rocker_port;
	struct net_device *dev;
	struct neighbour *n = ptr;
	int err;

	switch (event) {
	case NETEVENT_NEIGH_UPDATE:
		if (n->tbl != &arp_tbl)
			return NOTIFY_DONE;
		dev = n->dev;
		if (!rocker_port_dev_check(dev))
			return NOTIFY_DONE;
		rocker_port = netdev_priv(dev);
		err = rocker_world_port_neigh_update(rocker_port, n);
		if (err)
			netdev_warn(dev, "failed to handle neigh update (err %d)\n",
				    err);
		break;
	}

	return NOTIFY_DONE;
}

static struct notifier_block rocker_netevent_nb __read_mostly = {
	.notifier_call = rocker_netevent_event,
};

/***********************
 * Module init and exit
 ***********************/

static int __init rocker_module_init(void)
{
	int err;

	register_netdevice_notifier(&rocker_netdevice_nb);
	register_netevent_notifier(&rocker_netevent_nb);
	err = pci_register_driver(&rocker_pci_driver);
	if (err)
		goto err_pci_register_driver;
	return 0;

err_pci_register_driver:
	unregister_netevent_notifier(&rocker_netevent_nb);
	unregister_netdevice_notifier(&rocker_netdevice_nb);
	return err;
}

static void __exit rocker_module_exit(void)
{
	unregister_netevent_notifier(&rocker_netevent_nb);
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
