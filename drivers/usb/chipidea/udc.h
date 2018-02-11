// SPDX-License-Identifier: GPL-2.0
/*
 * udc.h - ChipIdea UDC structures
 *
 * Copyright (C) 2008 Chipidea - MIPS Technologies, Inc. All rights reserved.
 *
 * Author: David Lopo
 */

#ifndef __DRIVERS_USB_CHIPIDEA_UDC_H
#define __DRIVERS_USB_CHIPIDEA_UDC_H

#include <linux/list.h>

#define CTRL_PAYLOAD_MAX   64
#define RX        0  /* similar to USB_DIR_OUT but can be used as an index */
#define TX        1  /* similar to USB_DIR_IN  but can be used as an index */

/* DMA layout of transfer descriptors */
struct ci_hw_td {
	/* 0 */
	__le32 next;
#define TD_TERMINATE          BIT(0)
#define TD_ADDR_MASK          (0xFFFFFFEUL << 5)
	/* 1 */
	__le32 token;
#define TD_STATUS             (0x00FFUL <<  0)
#define TD_STATUS_TR_ERR      BIT(3)
#define TD_STATUS_DT_ERR      BIT(5)
#define TD_STATUS_HALTED      BIT(6)
#define TD_STATUS_ACTIVE      BIT(7)
#define TD_MULTO              (0x0003UL << 10)
#define TD_IOC                BIT(15)
#define TD_TOTAL_BYTES        (0x7FFFUL << 16)
	/* 2 */
	__le32 page[5];
#define TD_CURR_OFFSET        (0x0FFFUL <<  0)
#define TD_FRAME_NUM          (0x07FFUL <<  0)
#define TD_RESERVED_MASK      (0x0FFFUL <<  0)
} __attribute__ ((packed, aligned(4)));

/* DMA layout of queue heads */
struct ci_hw_qh {
	/* 0 */
	__le32 cap;
#define QH_IOS                BIT(15)
#define QH_MAX_PKT            (0x07FFUL << 16)
#define QH_ZLT                BIT(29)
#define QH_MULT               (0x0003UL << 30)
#define QH_ISO_MULT(x)		((x >> 11) & 0x03)
	/* 1 */
	__le32 curr;
	/* 2 - 8 */
	struct ci_hw_td		td;
	/* 9 */
	__le32 RESERVED;
	struct usb_ctrlrequest   setup;
} __attribute__ ((packed, aligned(4)));

struct td_node {
	struct list_head	td;
	dma_addr_t		dma;
	struct ci_hw_td		*ptr;
};

/**
 * struct ci_hw_req - usb request representation
 * @req: request structure for gadget drivers
 * @queue: link to QH list
 * @ptr: transfer descriptor for this request
 * @dma: dma address for the transfer descriptor
 * @zptr: transfer descriptor for the zero packet
 * @zdma: dma address of the zero packet's transfer descriptor
 */
struct ci_hw_req {
	struct usb_request	req;
	struct list_head	queue;
	struct list_head	tds;
};

#ifdef CONFIG_USB_CHIPIDEA_UDC

int ci_hdrc_gadget_init(struct ci_hdrc *ci);
void ci_hdrc_gadget_destroy(struct ci_hdrc *ci);

#else

static inline int ci_hdrc_gadget_init(struct ci_hdrc *ci)
{
	return -ENXIO;
}

static inline void ci_hdrc_gadget_destroy(struct ci_hdrc *ci)
{

}

#endif

#endif /* __DRIVERS_USB_CHIPIDEA_UDC_H */
