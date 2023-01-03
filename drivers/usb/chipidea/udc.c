// SPDX-License-Identifier: GPL-2.0
/*
 * udc.c - ChipIdea UDC driver
 *
 * Copyright (C) 2008 Chipidea - MIPS Technologies, Inc. All rights reserved.
 *
 * Author: David Lopo
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/dmapool.h>
#include <linux/err.h>
#include <linux/irqreturn.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include <linux/pm_runtime.h>
#include <linux/pinctrl/consumer.h>
#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb/otg-fsm.h>
#include <linux/usb/chipidea.h>

#include "ci.h"
#include "udc.h"
#include "bits.h"
#include "otg.h"
#include "otg_fsm.h"
#include "trace.h"

/* control endpoint description */
static const struct usb_endpoint_descriptor
ctrl_endpt_out_desc = {
	.bLength         = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_OUT,
	.bmAttributes    = USB_ENDPOINT_XFER_CONTROL,
	.wMaxPacketSize  = cpu_to_le16(CTRL_PAYLOAD_MAX),
};

static const struct usb_endpoint_descriptor
ctrl_endpt_in_desc = {
	.bLength         = USB_DT_ENDPOINT_SIZE,
	.bDescriptorType = USB_DT_ENDPOINT,

	.bEndpointAddress = USB_DIR_IN,
	.bmAttributes    = USB_ENDPOINT_XFER_CONTROL,
	.wMaxPacketSize  = cpu_to_le16(CTRL_PAYLOAD_MAX),
};

static int reprime_dtd(struct ci_hdrc *ci, struct ci_hw_ep *hwep,
		       struct td_node *node);
/**
 * hw_ep_bit: calculates the bit number
 * @num: endpoint number
 * @dir: endpoint direction
 *
 * This function returns bit number
 */
static inline int hw_ep_bit(int num, int dir)
{
	return num + ((dir == TX) ? 16 : 0);
}

static inline int ep_to_bit(struct ci_hdrc *ci, int n)
{
	int fill = 16 - ci->hw_ep_max / 2;

	if (n >= ci->hw_ep_max / 2)
		n += fill;

	return n;
}

/**
 * hw_device_state: enables/disables interrupts (execute without interruption)
 * @ci: the controller
 * @dma: 0 => disable, !0 => enable and set dma engine
 *
 * This function returns an error code
 */
static int hw_device_state(struct ci_hdrc *ci, u32 dma)
{
	if (dma) {
		hw_write(ci, OP_ENDPTLISTADDR, ~0, dma);
		/* interrupt, error, port change, reset, sleep/suspend */
		hw_write(ci, OP_USBINTR, ~0,
			     USBi_UI|USBi_UEI|USBi_PCI|USBi_URI|USBi_SLI);
	} else {
		hw_write(ci, OP_USBINTR, ~0, 0);
	}
	return 0;
}

/**
 * hw_ep_flush: flush endpoint fifo (execute without interruption)
 * @ci: the controller
 * @num: endpoint number
 * @dir: endpoint direction
 *
 * This function returns an error code
 */
static int hw_ep_flush(struct ci_hdrc *ci, int num, int dir)
{
	int n = hw_ep_bit(num, dir);

	do {
		/* flush any pending transfer */
		hw_write(ci, OP_ENDPTFLUSH, ~0, BIT(n));
		while (hw_read(ci, OP_ENDPTFLUSH, BIT(n)))
			cpu_relax();
	} while (hw_read(ci, OP_ENDPTSTAT, BIT(n)));

	return 0;
}

/**
 * hw_ep_disable: disables endpoint (execute without interruption)
 * @ci: the controller
 * @num: endpoint number
 * @dir: endpoint direction
 *
 * This function returns an error code
 */
static int hw_ep_disable(struct ci_hdrc *ci, int num, int dir)
{
	hw_write(ci, OP_ENDPTCTRL + num,
		 (dir == TX) ? ENDPTCTRL_TXE : ENDPTCTRL_RXE, 0);
	return 0;
}

/**
 * hw_ep_enable: enables endpoint (execute without interruption)
 * @ci: the controller
 * @num:  endpoint number
 * @dir:  endpoint direction
 * @type: endpoint type
 *
 * This function returns an error code
 */
static int hw_ep_enable(struct ci_hdrc *ci, int num, int dir, int type)
{
	u32 mask, data;

	if (dir == TX) {
		mask  = ENDPTCTRL_TXT;  /* type    */
		data  = type << __ffs(mask);

		mask |= ENDPTCTRL_TXS;  /* unstall */
		mask |= ENDPTCTRL_TXR;  /* reset data toggle */
		data |= ENDPTCTRL_TXR;
		mask |= ENDPTCTRL_TXE;  /* enable  */
		data |= ENDPTCTRL_TXE;
	} else {
		mask  = ENDPTCTRL_RXT;  /* type    */
		data  = type << __ffs(mask);

		mask |= ENDPTCTRL_RXS;  /* unstall */
		mask |= ENDPTCTRL_RXR;  /* reset data toggle */
		data |= ENDPTCTRL_RXR;
		mask |= ENDPTCTRL_RXE;  /* enable  */
		data |= ENDPTCTRL_RXE;
	}
	hw_write(ci, OP_ENDPTCTRL + num, mask, data);
	return 0;
}

/**
 * hw_ep_get_halt: return endpoint halt status
 * @ci: the controller
 * @num: endpoint number
 * @dir: endpoint direction
 *
 * This function returns 1 if endpoint halted
 */
static int hw_ep_get_halt(struct ci_hdrc *ci, int num, int dir)
{
	u32 mask = (dir == TX) ? ENDPTCTRL_TXS : ENDPTCTRL_RXS;

	return hw_read(ci, OP_ENDPTCTRL + num, mask) ? 1 : 0;
}

/**
 * hw_ep_prime: primes endpoint (execute without interruption)
 * @ci: the controller
 * @num:     endpoint number
 * @dir:     endpoint direction
 * @is_ctrl: true if control endpoint
 *
 * This function returns an error code
 */
static int hw_ep_prime(struct ci_hdrc *ci, int num, int dir, int is_ctrl)
{
	int n = hw_ep_bit(num, dir);

	/* Synchronize before ep prime */
	wmb();

	if (is_ctrl && dir == RX && hw_read(ci, OP_ENDPTSETUPSTAT, BIT(num)))
		return -EAGAIN;

	hw_write(ci, OP_ENDPTPRIME, ~0, BIT(n));

	while (hw_read(ci, OP_ENDPTPRIME, BIT(n)))
		cpu_relax();
	if (is_ctrl && dir == RX && hw_read(ci, OP_ENDPTSETUPSTAT, BIT(num)))
		return -EAGAIN;

	/* status shoult be tested according with manual but it doesn't work */
	return 0;
}

/**
 * hw_ep_set_halt: configures ep halt & resets data toggle after clear (execute
 *                 without interruption)
 * @ci: the controller
 * @num:   endpoint number
 * @dir:   endpoint direction
 * @value: true => stall, false => unstall
 *
 * This function returns an error code
 */
static int hw_ep_set_halt(struct ci_hdrc *ci, int num, int dir, int value)
{
	if (value != 0 && value != 1)
		return -EINVAL;

	do {
		enum ci_hw_regs reg = OP_ENDPTCTRL + num;
		u32 mask_xs = (dir == TX) ? ENDPTCTRL_TXS : ENDPTCTRL_RXS;
		u32 mask_xr = (dir == TX) ? ENDPTCTRL_TXR : ENDPTCTRL_RXR;

		/* data toggle - reserved for EP0 but it's in ESS */
		hw_write(ci, reg, mask_xs|mask_xr,
			  value ? mask_xs : mask_xr);
	} while (value != hw_ep_get_halt(ci, num, dir));

	return 0;
}

/**
 * hw_port_is_high_speed: test if port is high speed
 * @ci: the controller
 *
 * This function returns true if high speed port
 */
static int hw_port_is_high_speed(struct ci_hdrc *ci)
{
	return ci->hw_bank.lpm ? hw_read(ci, OP_DEVLC, DEVLC_PSPD) :
		hw_read(ci, OP_PORTSC, PORTSC_HSP);
}

/**
 * hw_test_and_clear_complete: test & clear complete status (execute without
 *                             interruption)
 * @ci: the controller
 * @n: endpoint number
 *
 * This function returns complete status
 */
static int hw_test_and_clear_complete(struct ci_hdrc *ci, int n)
{
	n = ep_to_bit(ci, n);
	return hw_test_and_clear(ci, OP_ENDPTCOMPLETE, BIT(n));
}

/**
 * hw_test_and_clear_intr_active: test & clear active interrupts (execute
 *                                without interruption)
 * @ci: the controller
 *
 * This function returns active interrutps
 */
static u32 hw_test_and_clear_intr_active(struct ci_hdrc *ci)
{
	u32 reg = hw_read_intr_status(ci) & hw_read_intr_enable(ci);

	hw_write(ci, OP_USBSTS, ~0, reg);
	return reg;
}

/**
 * hw_test_and_clear_setup_guard: test & clear setup guard (execute without
 *                                interruption)
 * @ci: the controller
 *
 * This function returns guard value
 */
static int hw_test_and_clear_setup_guard(struct ci_hdrc *ci)
{
	return hw_test_and_write(ci, OP_USBCMD, USBCMD_SUTW, 0);
}

/**
 * hw_test_and_set_setup_guard: test & set setup guard (execute without
 *                              interruption)
 * @ci: the controller
 *
 * This function returns guard value
 */
static int hw_test_and_set_setup_guard(struct ci_hdrc *ci)
{
	return hw_test_and_write(ci, OP_USBCMD, USBCMD_SUTW, USBCMD_SUTW);
}

/**
 * hw_usb_set_address: configures USB address (execute without interruption)
 * @ci: the controller
 * @value: new USB address
 *
 * This function explicitly sets the address, without the "USBADRA" (advance)
 * feature, which is not supported by older versions of the controller.
 */
static void hw_usb_set_address(struct ci_hdrc *ci, u8 value)
{
	hw_write(ci, OP_DEVICEADDR, DEVICEADDR_USBADR,
		 value << __ffs(DEVICEADDR_USBADR));
}

/**
 * hw_usb_reset: restart device after a bus reset (execute without
 *               interruption)
 * @ci: the controller
 *
 * This function returns an error code
 */
static int hw_usb_reset(struct ci_hdrc *ci)
{
	hw_usb_set_address(ci, 0);

	/* ESS flushes only at end?!? */
	hw_write(ci, OP_ENDPTFLUSH,    ~0, ~0);

	/* clear setup token semaphores */
	hw_write(ci, OP_ENDPTSETUPSTAT, 0,  0);

	/* clear complete status */
	hw_write(ci, OP_ENDPTCOMPLETE,  0,  0);

	/* wait until all bits cleared */
	while (hw_read(ci, OP_ENDPTPRIME, ~0))
		udelay(10);             /* not RTOS friendly */

	/* reset all endpoints ? */

	/* reset internal status and wait for further instructions
	   no need to verify the port reset status (ESS does it) */

	return 0;
}

/******************************************************************************
 * UTIL block
 *****************************************************************************/

static int add_td_to_list(struct ci_hw_ep *hwep, struct ci_hw_req *hwreq,
			unsigned int length, struct scatterlist *s)
{
	int i;
	u32 temp;
	struct td_node *lastnode, *node = kzalloc(sizeof(struct td_node),
						  GFP_ATOMIC);

	if (node == NULL)
		return -ENOMEM;

	node->ptr = dma_pool_zalloc(hwep->td_pool, GFP_ATOMIC, &node->dma);
	if (node->ptr == NULL) {
		kfree(node);
		return -ENOMEM;
	}

	node->ptr->token = cpu_to_le32(length << __ffs(TD_TOTAL_BYTES));
	node->ptr->token &= cpu_to_le32(TD_TOTAL_BYTES);
	node->ptr->token |= cpu_to_le32(TD_STATUS_ACTIVE);
	if (hwep->type == USB_ENDPOINT_XFER_ISOC && hwep->dir == TX) {
		u32 mul = hwreq->req.length / hwep->ep.maxpacket;

		if (hwreq->req.length == 0
				|| hwreq->req.length % hwep->ep.maxpacket)
			mul++;
		node->ptr->token |= cpu_to_le32(mul << __ffs(TD_MULTO));
	}

	if (s) {
		temp = (u32) (sg_dma_address(s) + hwreq->req.actual);
		node->td_remaining_size = CI_MAX_BUF_SIZE - length;
	} else {
		temp = (u32) (hwreq->req.dma + hwreq->req.actual);
	}

	if (length) {
		node->ptr->page[0] = cpu_to_le32(temp);
		for (i = 1; i < TD_PAGE_COUNT; i++) {
			u32 page = temp + i * CI_HDRC_PAGE_SIZE;
			page &= ~TD_RESERVED_MASK;
			node->ptr->page[i] = cpu_to_le32(page);
		}
	}

	hwreq->req.actual += length;

	if (!list_empty(&hwreq->tds)) {
		/* get the last entry */
		lastnode = list_entry(hwreq->tds.prev,
				struct td_node, td);
		lastnode->ptr->next = cpu_to_le32(node->dma);
	}

	INIT_LIST_HEAD(&node->td);
	list_add_tail(&node->td, &hwreq->tds);

	return 0;
}

/**
 * _usb_addr: calculates endpoint address from direction & number
 * @ep:  endpoint
 */
static inline u8 _usb_addr(struct ci_hw_ep *ep)
{
	return ((ep->dir == TX) ? USB_ENDPOINT_DIR_MASK : 0) | ep->num;
}

static int prepare_td_for_non_sg(struct ci_hw_ep *hwep,
		struct ci_hw_req *hwreq)
{
	unsigned int rest = hwreq->req.length;
	int pages = TD_PAGE_COUNT;
	int ret = 0;

	if (rest == 0) {
		ret = add_td_to_list(hwep, hwreq, 0, NULL);
		if (ret < 0)
			return ret;
	}

	/*
	 * The first buffer could be not page aligned.
	 * In that case we have to span into one extra td.
	 */
	if (hwreq->req.dma % PAGE_SIZE)
		pages--;

	while (rest > 0) {
		unsigned int count = min(hwreq->req.length - hwreq->req.actual,
			(unsigned int)(pages * CI_HDRC_PAGE_SIZE));

		ret = add_td_to_list(hwep, hwreq, count, NULL);
		if (ret < 0)
			return ret;

		rest -= count;
	}

	if (hwreq->req.zero && hwreq->req.length && hwep->dir == TX
	    && (hwreq->req.length % hwep->ep.maxpacket == 0)) {
		ret = add_td_to_list(hwep, hwreq, 0, NULL);
		if (ret < 0)
			return ret;
	}

	return ret;
}

static int prepare_td_per_sg(struct ci_hw_ep *hwep, struct ci_hw_req *hwreq,
		struct scatterlist *s)
{
	unsigned int rest = sg_dma_len(s);
	int ret = 0;

	hwreq->req.actual = 0;
	while (rest > 0) {
		unsigned int count = min_t(unsigned int, rest,
				CI_MAX_BUF_SIZE);

		ret = add_td_to_list(hwep, hwreq, count, s);
		if (ret < 0)
			return ret;

		rest -= count;
	}

	return ret;
}

static void ci_add_buffer_entry(struct td_node *node, struct scatterlist *s)
{
	int empty_td_slot_index = (CI_MAX_BUF_SIZE - node->td_remaining_size)
			/ CI_HDRC_PAGE_SIZE;
	int i;
	u32 token;

	token = le32_to_cpu(node->ptr->token) + (sg_dma_len(s) << __ffs(TD_TOTAL_BYTES));
	node->ptr->token = cpu_to_le32(token);

	for (i = empty_td_slot_index; i < TD_PAGE_COUNT; i++) {
		u32 page = (u32) sg_dma_address(s) +
			(i - empty_td_slot_index) * CI_HDRC_PAGE_SIZE;

		page &= ~TD_RESERVED_MASK;
		node->ptr->page[i] = cpu_to_le32(page);
	}
}

static int prepare_td_for_sg(struct ci_hw_ep *hwep, struct ci_hw_req *hwreq)
{
	struct usb_request *req = &hwreq->req;
	struct scatterlist *s = req->sg;
	int ret = 0, i = 0;
	struct td_node *node = NULL;

	if (!s || req->zero || req->length == 0) {
		dev_err(hwep->ci->dev, "not supported operation for sg\n");
		return -EINVAL;
	}

	while (i++ < req->num_mapped_sgs) {
		if (sg_dma_address(s) % PAGE_SIZE) {
			dev_err(hwep->ci->dev, "not page aligned sg buffer\n");
			return -EINVAL;
		}

		if (node && (node->td_remaining_size >= sg_dma_len(s))) {
			ci_add_buffer_entry(node, s);
			node->td_remaining_size -= sg_dma_len(s);
		} else {
			ret = prepare_td_per_sg(hwep, hwreq, s);
			if (ret)
				return ret;

			node = list_entry(hwreq->tds.prev,
				struct td_node, td);
		}

		s = sg_next(s);
	}

	return ret;
}

/**
 * _hardware_enqueue: configures a request at hardware level
 * @hwep:   endpoint
 * @hwreq:  request
 *
 * This function returns an error code
 */
static int _hardware_enqueue(struct ci_hw_ep *hwep, struct ci_hw_req *hwreq)
{
	struct ci_hdrc *ci = hwep->ci;
	int ret = 0;
	struct td_node *firstnode, *lastnode;

	/* don't queue twice */
	if (hwreq->req.status == -EALREADY)
		return -EALREADY;

	hwreq->req.status = -EALREADY;

	ret = usb_gadget_map_request_by_dev(ci->dev->parent,
					    &hwreq->req, hwep->dir);
	if (ret)
		return ret;

	if (hwreq->req.num_mapped_sgs)
		ret = prepare_td_for_sg(hwep, hwreq);
	else
		ret = prepare_td_for_non_sg(hwep, hwreq);

	if (ret)
		return ret;

	lastnode = list_entry(hwreq->tds.prev,
		struct td_node, td);

	lastnode->ptr->next = cpu_to_le32(TD_TERMINATE);
	if (!hwreq->req.no_interrupt)
		lastnode->ptr->token |= cpu_to_le32(TD_IOC);

	list_for_each_entry_safe(firstnode, lastnode, &hwreq->tds, td)
		trace_ci_prepare_td(hwep, hwreq, firstnode);

	firstnode = list_first_entry(&hwreq->tds, struct td_node, td);

	wmb();

	hwreq->req.actual = 0;
	if (!list_empty(&hwep->qh.queue)) {
		struct ci_hw_req *hwreqprev;
		int n = hw_ep_bit(hwep->num, hwep->dir);
		int tmp_stat;
		struct td_node *prevlastnode;
		u32 next = firstnode->dma & TD_ADDR_MASK;

		hwreqprev = list_entry(hwep->qh.queue.prev,
				struct ci_hw_req, queue);
		prevlastnode = list_entry(hwreqprev->tds.prev,
				struct td_node, td);

		prevlastnode->ptr->next = cpu_to_le32(next);
		wmb();

		if (ci->rev == CI_REVISION_22) {
			if (!hw_read(ci, OP_ENDPTSTAT, BIT(n)))
				reprime_dtd(ci, hwep, prevlastnode);
		}

		if (hw_read(ci, OP_ENDPTPRIME, BIT(n)))
			goto done;
		do {
			hw_write(ci, OP_USBCMD, USBCMD_ATDTW, USBCMD_ATDTW);
			tmp_stat = hw_read(ci, OP_ENDPTSTAT, BIT(n));
		} while (!hw_read(ci, OP_USBCMD, USBCMD_ATDTW));
		hw_write(ci, OP_USBCMD, USBCMD_ATDTW, 0);
		if (tmp_stat)
			goto done;
	}

	/*  QH configuration */
	hwep->qh.ptr->td.next = cpu_to_le32(firstnode->dma);
	hwep->qh.ptr->td.token &=
		cpu_to_le32(~(TD_STATUS_HALTED|TD_STATUS_ACTIVE));

	if (hwep->type == USB_ENDPOINT_XFER_ISOC && hwep->dir == RX) {
		u32 mul = hwreq->req.length / hwep->ep.maxpacket;

		if (hwreq->req.length == 0
				|| hwreq->req.length % hwep->ep.maxpacket)
			mul++;
		hwep->qh.ptr->cap |= cpu_to_le32(mul << __ffs(QH_MULT));
	}

	ret = hw_ep_prime(ci, hwep->num, hwep->dir,
			   hwep->type == USB_ENDPOINT_XFER_CONTROL);
done:
	return ret;
}

/**
 * free_pending_td: remove a pending request for the endpoint
 * @hwep: endpoint
 */
static void free_pending_td(struct ci_hw_ep *hwep)
{
	struct td_node *pending = hwep->pending_td;

	dma_pool_free(hwep->td_pool, pending->ptr, pending->dma);
	hwep->pending_td = NULL;
	kfree(pending);
}

static int reprime_dtd(struct ci_hdrc *ci, struct ci_hw_ep *hwep,
					   struct td_node *node)
{
	hwep->qh.ptr->td.next = cpu_to_le32(node->dma);
	hwep->qh.ptr->td.token &=
		cpu_to_le32(~(TD_STATUS_HALTED | TD_STATUS_ACTIVE));

	return hw_ep_prime(ci, hwep->num, hwep->dir,
				hwep->type == USB_ENDPOINT_XFER_CONTROL);
}

/**
 * _hardware_dequeue: handles a request at hardware level
 * @hwep: endpoint
 * @hwreq:  request
 *
 * This function returns an error code
 */
static int _hardware_dequeue(struct ci_hw_ep *hwep, struct ci_hw_req *hwreq)
{
	u32 tmptoken;
	struct td_node *node, *tmpnode;
	unsigned remaining_length;
	unsigned actual = hwreq->req.length;
	struct ci_hdrc *ci = hwep->ci;

	if (hwreq->req.status != -EALREADY)
		return -EINVAL;

	hwreq->req.status = 0;

	list_for_each_entry_safe(node, tmpnode, &hwreq->tds, td) {
		tmptoken = le32_to_cpu(node->ptr->token);
		trace_ci_complete_td(hwep, hwreq, node);
		if ((TD_STATUS_ACTIVE & tmptoken) != 0) {
			int n = hw_ep_bit(hwep->num, hwep->dir);

			if (ci->rev == CI_REVISION_24)
				if (!hw_read(ci, OP_ENDPTSTAT, BIT(n)))
					reprime_dtd(ci, hwep, node);
			hwreq->req.status = -EALREADY;
			return -EBUSY;
		}

		remaining_length = (tmptoken & TD_TOTAL_BYTES);
		remaining_length >>= __ffs(TD_TOTAL_BYTES);
		actual -= remaining_length;

		hwreq->req.status = tmptoken & TD_STATUS;
		if ((TD_STATUS_HALTED & hwreq->req.status)) {
			hwreq->req.status = -EPIPE;
			break;
		} else if ((TD_STATUS_DT_ERR & hwreq->req.status)) {
			hwreq->req.status = -EPROTO;
			break;
		} else if ((TD_STATUS_TR_ERR & hwreq->req.status)) {
			hwreq->req.status = -EILSEQ;
			break;
		}

		if (remaining_length) {
			if (hwep->dir == TX) {
				hwreq->req.status = -EPROTO;
				break;
			}
		}
		/*
		 * As the hardware could still address the freed td
		 * which will run the udc unusable, the cleanup of the
		 * td has to be delayed by one.
		 */
		if (hwep->pending_td)
			free_pending_td(hwep);

		hwep->pending_td = node;
		list_del_init(&node->td);
	}

	usb_gadget_unmap_request_by_dev(hwep->ci->dev->parent,
					&hwreq->req, hwep->dir);

	hwreq->req.actual += actual;

	if (hwreq->req.status)
		return hwreq->req.status;

	return hwreq->req.actual;
}

/**
 * _ep_nuke: dequeues all endpoint requests
 * @hwep: endpoint
 *
 * This function returns an error code
 * Caller must hold lock
 */
static int _ep_nuke(struct ci_hw_ep *hwep)
__releases(hwep->lock)
__acquires(hwep->lock)
{
	struct td_node *node, *tmpnode;
	if (hwep == NULL)
		return -EINVAL;

	hw_ep_flush(hwep->ci, hwep->num, hwep->dir);

	while (!list_empty(&hwep->qh.queue)) {

		/* pop oldest request */
		struct ci_hw_req *hwreq = list_entry(hwep->qh.queue.next,
						     struct ci_hw_req, queue);

		list_for_each_entry_safe(node, tmpnode, &hwreq->tds, td) {
			dma_pool_free(hwep->td_pool, node->ptr, node->dma);
			list_del_init(&node->td);
			node->ptr = NULL;
			kfree(node);
		}

		list_del_init(&hwreq->queue);
		hwreq->req.status = -ESHUTDOWN;

		if (hwreq->req.complete != NULL) {
			spin_unlock(hwep->lock);
			usb_gadget_giveback_request(&hwep->ep, &hwreq->req);
			spin_lock(hwep->lock);
		}
	}

	if (hwep->pending_td)
		free_pending_td(hwep);

	return 0;
}

static int _ep_set_halt(struct usb_ep *ep, int value, bool check_transfer)
{
	struct ci_hw_ep *hwep = container_of(ep, struct ci_hw_ep, ep);
	int direction, retval = 0;
	unsigned long flags;

	if (ep == NULL || hwep->ep.desc == NULL)
		return -EINVAL;

	if (usb_endpoint_xfer_isoc(hwep->ep.desc))
		return -EOPNOTSUPP;

	spin_lock_irqsave(hwep->lock, flags);

	if (value && hwep->dir == TX && check_transfer &&
		!list_empty(&hwep->qh.queue) &&
			!usb_endpoint_xfer_control(hwep->ep.desc)) {
		spin_unlock_irqrestore(hwep->lock, flags);
		return -EAGAIN;
	}

	direction = hwep->dir;
	do {
		retval |= hw_ep_set_halt(hwep->ci, hwep->num, hwep->dir, value);

		if (!value)
			hwep->wedge = 0;

		if (hwep->type == USB_ENDPOINT_XFER_CONTROL)
			hwep->dir = (hwep->dir == TX) ? RX : TX;

	} while (hwep->dir != direction);

	spin_unlock_irqrestore(hwep->lock, flags);
	return retval;
}


/**
 * _gadget_stop_activity: stops all USB activity, flushes & disables all endpts
 * @gadget: gadget
 *
 * This function returns an error code
 */
static int _gadget_stop_activity(struct usb_gadget *gadget)
{
	struct usb_ep *ep;
	struct ci_hdrc    *ci = container_of(gadget, struct ci_hdrc, gadget);
	unsigned long flags;

	/* flush all endpoints */
	gadget_for_each_ep(ep, gadget) {
		usb_ep_fifo_flush(ep);
	}
	usb_ep_fifo_flush(&ci->ep0out->ep);
	usb_ep_fifo_flush(&ci->ep0in->ep);

	/* make sure to disable all endpoints */
	gadget_for_each_ep(ep, gadget) {
		usb_ep_disable(ep);
	}

	if (ci->status != NULL) {
		usb_ep_free_request(&ci->ep0in->ep, ci->status);
		ci->status = NULL;
	}

	spin_lock_irqsave(&ci->lock, flags);
	ci->gadget.speed = USB_SPEED_UNKNOWN;
	ci->remote_wakeup = 0;
	ci->suspended = 0;
	spin_unlock_irqrestore(&ci->lock, flags);

	return 0;
}

/******************************************************************************
 * ISR block
 *****************************************************************************/
/**
 * isr_reset_handler: USB reset interrupt handler
 * @ci: UDC device
 *
 * This function resets USB engine after a bus reset occurred
 */
static void isr_reset_handler(struct ci_hdrc *ci)
__releases(ci->lock)
__acquires(ci->lock)
{
	int retval;

	spin_unlock(&ci->lock);
	if (ci->gadget.speed != USB_SPEED_UNKNOWN)
		usb_gadget_udc_reset(&ci->gadget, ci->driver);

	retval = _gadget_stop_activity(&ci->gadget);
	if (retval)
		goto done;

	retval = hw_usb_reset(ci);
	if (retval)
		goto done;

	ci->status = usb_ep_alloc_request(&ci->ep0in->ep, GFP_ATOMIC);
	if (ci->status == NULL)
		retval = -ENOMEM;

done:
	spin_lock(&ci->lock);

	if (retval)
		dev_err(ci->dev, "error: %i\n", retval);
}

/**
 * isr_get_status_complete: get_status request complete function
 * @ep:  endpoint
 * @req: request handled
 *
 * Caller must release lock
 */
static void isr_get_status_complete(struct usb_ep *ep, struct usb_request *req)
{
	if (ep == NULL || req == NULL)
		return;

	kfree(req->buf);
	usb_ep_free_request(ep, req);
}

/**
 * _ep_queue: queues (submits) an I/O request to an endpoint
 * @ep:        endpoint
 * @req:       request
 * @gfp_flags: GFP flags (not used)
 *
 * Caller must hold lock
 * This function returns an error code
 */
static int _ep_queue(struct usb_ep *ep, struct usb_request *req,
		    gfp_t __maybe_unused gfp_flags)
{
	struct ci_hw_ep  *hwep  = container_of(ep,  struct ci_hw_ep, ep);
	struct ci_hw_req *hwreq = container_of(req, struct ci_hw_req, req);
	struct ci_hdrc *ci = hwep->ci;
	int retval = 0;

	if (ep == NULL || req == NULL || hwep->ep.desc == NULL)
		return -EINVAL;

	if (hwep->type == USB_ENDPOINT_XFER_CONTROL) {
		if (req->length)
			hwep = (ci->ep0_dir == RX) ?
			       ci->ep0out : ci->ep0in;
		if (!list_empty(&hwep->qh.queue)) {
			_ep_nuke(hwep);
			dev_warn(hwep->ci->dev, "endpoint ctrl %X nuked\n",
				 _usb_addr(hwep));
		}
	}

	if (usb_endpoint_xfer_isoc(hwep->ep.desc) &&
	    hwreq->req.length > hwep->ep.mult * hwep->ep.maxpacket) {
		dev_err(hwep->ci->dev, "request length too big for isochronous\n");
		return -EMSGSIZE;
	}

	/* first nuke then test link, e.g. previous status has not sent */
	if (!list_empty(&hwreq->queue)) {
		dev_err(hwep->ci->dev, "request already in queue\n");
		return -EBUSY;
	}

	/* push request */
	hwreq->req.status = -EINPROGRESS;
	hwreq->req.actual = 0;

	retval = _hardware_enqueue(hwep, hwreq);

	if (retval == -EALREADY)
		retval = 0;
	if (!retval)
		list_add_tail(&hwreq->queue, &hwep->qh.queue);

	return retval;
}

/**
 * isr_get_status_response: get_status request response
 * @ci: ci struct
 * @setup: setup request packet
 *
 * This function returns an error code
 */
static int isr_get_status_response(struct ci_hdrc *ci,
				   struct usb_ctrlrequest *setup)
__releases(hwep->lock)
__acquires(hwep->lock)
{
	struct ci_hw_ep *hwep = ci->ep0in;
	struct usb_request *req = NULL;
	gfp_t gfp_flags = GFP_ATOMIC;
	int dir, num, retval;

	if (hwep == NULL || setup == NULL)
		return -EINVAL;

	spin_unlock(hwep->lock);
	req = usb_ep_alloc_request(&hwep->ep, gfp_flags);
	spin_lock(hwep->lock);
	if (req == NULL)
		return -ENOMEM;

	req->complete = isr_get_status_complete;
	req->length   = 2;
	req->buf      = kzalloc(req->length, gfp_flags);
	if (req->buf == NULL) {
		retval = -ENOMEM;
		goto err_free_req;
	}

	if ((setup->bRequestType & USB_RECIP_MASK) == USB_RECIP_DEVICE) {
		*(u16 *)req->buf = (ci->remote_wakeup << 1) |
			ci->gadget.is_selfpowered;
	} else if ((setup->bRequestType & USB_RECIP_MASK) \
		   == USB_RECIP_ENDPOINT) {
		dir = (le16_to_cpu(setup->wIndex) & USB_ENDPOINT_DIR_MASK) ?
			TX : RX;
		num =  le16_to_cpu(setup->wIndex) & USB_ENDPOINT_NUMBER_MASK;
		*(u16 *)req->buf = hw_ep_get_halt(ci, num, dir);
	}
	/* else do nothing; reserved for future use */

	retval = _ep_queue(&hwep->ep, req, gfp_flags);
	if (retval)
		goto err_free_buf;

	return 0;

 err_free_buf:
	kfree(req->buf);
 err_free_req:
	spin_unlock(hwep->lock);
	usb_ep_free_request(&hwep->ep, req);
	spin_lock(hwep->lock);
	return retval;
}

/**
 * isr_setup_status_complete: setup_status request complete function
 * @ep:  endpoint
 * @req: request handled
 *
 * Caller must release lock. Put the port in test mode if test mode
 * feature is selected.
 */
static void
isr_setup_status_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct ci_hdrc *ci = req->context;
	unsigned long flags;

	if (req->status < 0)
		return;

	if (ci->setaddr) {
		hw_usb_set_address(ci, ci->address);
		ci->setaddr = false;
		if (ci->address)
			usb_gadget_set_state(&ci->gadget, USB_STATE_ADDRESS);
	}

	spin_lock_irqsave(&ci->lock, flags);
	if (ci->test_mode)
		hw_port_test_set(ci, ci->test_mode);
	spin_unlock_irqrestore(&ci->lock, flags);
}

/**
 * isr_setup_status_phase: queues the status phase of a setup transation
 * @ci: ci struct
 *
 * This function returns an error code
 */
static int isr_setup_status_phase(struct ci_hdrc *ci)
{
	struct ci_hw_ep *hwep;

	/*
	 * Unexpected USB controller behavior, caused by bad signal integrity
	 * or ground reference problems, can lead to isr_setup_status_phase
	 * being called with ci->status equal to NULL.
	 * If this situation occurs, you should review your USB hardware design.
	 */
	if (WARN_ON_ONCE(!ci->status))
		return -EPIPE;

	hwep = (ci->ep0_dir == TX) ? ci->ep0out : ci->ep0in;
	ci->status->context = ci;
	ci->status->complete = isr_setup_status_complete;

	return _ep_queue(&hwep->ep, ci->status, GFP_ATOMIC);
}

/**
 * isr_tr_complete_low: transaction complete low level handler
 * @hwep: endpoint
 *
 * This function returns an error code
 * Caller must hold lock
 */
static int isr_tr_complete_low(struct ci_hw_ep *hwep)
__releases(hwep->lock)
__acquires(hwep->lock)
{
	struct ci_hw_req *hwreq, *hwreqtemp;
	struct ci_hw_ep *hweptemp = hwep;
	int retval = 0;

	list_for_each_entry_safe(hwreq, hwreqtemp, &hwep->qh.queue,
			queue) {
		retval = _hardware_dequeue(hwep, hwreq);
		if (retval < 0)
			break;
		list_del_init(&hwreq->queue);
		if (hwreq->req.complete != NULL) {
			spin_unlock(hwep->lock);
			if ((hwep->type == USB_ENDPOINT_XFER_CONTROL) &&
					hwreq->req.length)
				hweptemp = hwep->ci->ep0in;
			usb_gadget_giveback_request(&hweptemp->ep, &hwreq->req);
			spin_lock(hwep->lock);
		}
	}

	if (retval == -EBUSY)
		retval = 0;

	return retval;
}

static int otg_a_alt_hnp_support(struct ci_hdrc *ci)
{
	dev_warn(&ci->gadget.dev,
		"connect the device to an alternate port if you want HNP\n");
	return isr_setup_status_phase(ci);
}

/**
 * isr_setup_packet_handler: setup packet handler
 * @ci: UDC descriptor
 *
 * This function handles setup packet 
 */
static void isr_setup_packet_handler(struct ci_hdrc *ci)
__releases(ci->lock)
__acquires(ci->lock)
{
	struct ci_hw_ep *hwep = &ci->ci_hw_ep[0];
	struct usb_ctrlrequest req;
	int type, num, dir, err = -EINVAL;
	u8 tmode = 0;

	/*
	 * Flush data and handshake transactions of previous
	 * setup packet.
	 */
	_ep_nuke(ci->ep0out);
	_ep_nuke(ci->ep0in);

	/* read_setup_packet */
	do {
		hw_test_and_set_setup_guard(ci);
		memcpy(&req, &hwep->qh.ptr->setup, sizeof(req));
	} while (!hw_test_and_clear_setup_guard(ci));

	type = req.bRequestType;

	ci->ep0_dir = (type & USB_DIR_IN) ? TX : RX;

	switch (req.bRequest) {
	case USB_REQ_CLEAR_FEATURE:
		if (type == (USB_DIR_OUT|USB_RECIP_ENDPOINT) &&
				le16_to_cpu(req.wValue) ==
				USB_ENDPOINT_HALT) {
			if (req.wLength != 0)
				break;
			num  = le16_to_cpu(req.wIndex);
			dir = (num & USB_ENDPOINT_DIR_MASK) ? TX : RX;
			num &= USB_ENDPOINT_NUMBER_MASK;
			if (dir == TX)
				num += ci->hw_ep_max / 2;
			if (!ci->ci_hw_ep[num].wedge) {
				spin_unlock(&ci->lock);
				err = usb_ep_clear_halt(
					&ci->ci_hw_ep[num].ep);
				spin_lock(&ci->lock);
				if (err)
					break;
			}
			err = isr_setup_status_phase(ci);
		} else if (type == (USB_DIR_OUT|USB_RECIP_DEVICE) &&
				le16_to_cpu(req.wValue) ==
				USB_DEVICE_REMOTE_WAKEUP) {
			if (req.wLength != 0)
				break;
			ci->remote_wakeup = 0;
			err = isr_setup_status_phase(ci);
		} else {
			goto delegate;
		}
		break;
	case USB_REQ_GET_STATUS:
		if ((type != (USB_DIR_IN|USB_RECIP_DEVICE) ||
			le16_to_cpu(req.wIndex) == OTG_STS_SELECTOR) &&
		    type != (USB_DIR_IN|USB_RECIP_ENDPOINT) &&
		    type != (USB_DIR_IN|USB_RECIP_INTERFACE))
			goto delegate;
		if (le16_to_cpu(req.wLength) != 2 ||
		    le16_to_cpu(req.wValue)  != 0)
			break;
		err = isr_get_status_response(ci, &req);
		break;
	case USB_REQ_SET_ADDRESS:
		if (type != (USB_DIR_OUT|USB_RECIP_DEVICE))
			goto delegate;
		if (le16_to_cpu(req.wLength) != 0 ||
		    le16_to_cpu(req.wIndex)  != 0)
			break;
		ci->address = (u8)le16_to_cpu(req.wValue);
		ci->setaddr = true;
		err = isr_setup_status_phase(ci);
		break;
	case USB_REQ_SET_FEATURE:
		if (type == (USB_DIR_OUT|USB_RECIP_ENDPOINT) &&
				le16_to_cpu(req.wValue) ==
				USB_ENDPOINT_HALT) {
			if (req.wLength != 0)
				break;
			num  = le16_to_cpu(req.wIndex);
			dir = (num & USB_ENDPOINT_DIR_MASK) ? TX : RX;
			num &= USB_ENDPOINT_NUMBER_MASK;
			if (dir == TX)
				num += ci->hw_ep_max / 2;

			spin_unlock(&ci->lock);
			err = _ep_set_halt(&ci->ci_hw_ep[num].ep, 1, false);
			spin_lock(&ci->lock);
			if (!err)
				isr_setup_status_phase(ci);
		} else if (type == (USB_DIR_OUT|USB_RECIP_DEVICE)) {
			if (req.wLength != 0)
				break;
			switch (le16_to_cpu(req.wValue)) {
			case USB_DEVICE_REMOTE_WAKEUP:
				ci->remote_wakeup = 1;
				err = isr_setup_status_phase(ci);
				break;
			case USB_DEVICE_TEST_MODE:
				tmode = le16_to_cpu(req.wIndex) >> 8;
				switch (tmode) {
				case USB_TEST_J:
				case USB_TEST_K:
				case USB_TEST_SE0_NAK:
				case USB_TEST_PACKET:
				case USB_TEST_FORCE_ENABLE:
					ci->test_mode = tmode;
					err = isr_setup_status_phase(
							ci);
					break;
				default:
					break;
				}
				break;
			case USB_DEVICE_B_HNP_ENABLE:
				if (ci_otg_is_fsm_mode(ci)) {
					ci->gadget.b_hnp_enable = 1;
					err = isr_setup_status_phase(
							ci);
				}
				break;
			case USB_DEVICE_A_ALT_HNP_SUPPORT:
				if (ci_otg_is_fsm_mode(ci))
					err = otg_a_alt_hnp_support(ci);
				break;
			case USB_DEVICE_A_HNP_SUPPORT:
				if (ci_otg_is_fsm_mode(ci)) {
					ci->gadget.a_hnp_support = 1;
					err = isr_setup_status_phase(
							ci);
				}
				break;
			default:
				goto delegate;
			}
		} else {
			goto delegate;
		}
		break;
	default:
delegate:
		if (req.wLength == 0)   /* no data phase */
			ci->ep0_dir = TX;

		spin_unlock(&ci->lock);
		err = ci->driver->setup(&ci->gadget, &req);
		spin_lock(&ci->lock);
		break;
	}

	if (err < 0) {
		spin_unlock(&ci->lock);
		if (_ep_set_halt(&hwep->ep, 1, false))
			dev_err(ci->dev, "error: _ep_set_halt\n");
		spin_lock(&ci->lock);
	}
}

/**
 * isr_tr_complete_handler: transaction complete interrupt handler
 * @ci: UDC descriptor
 *
 * This function handles traffic events
 */
static void isr_tr_complete_handler(struct ci_hdrc *ci)
__releases(ci->lock)
__acquires(ci->lock)
{
	unsigned i;
	int err;

	for (i = 0; i < ci->hw_ep_max; i++) {
		struct ci_hw_ep *hwep  = &ci->ci_hw_ep[i];

		if (hwep->ep.desc == NULL)
			continue;   /* not configured */

		if (hw_test_and_clear_complete(ci, i)) {
			err = isr_tr_complete_low(hwep);
			if (hwep->type == USB_ENDPOINT_XFER_CONTROL) {
				if (err > 0)   /* needs status phase */
					err = isr_setup_status_phase(ci);
				if (err < 0) {
					spin_unlock(&ci->lock);
					if (_ep_set_halt(&hwep->ep, 1, false))
						dev_err(ci->dev,
						"error: _ep_set_halt\n");
					spin_lock(&ci->lock);
				}
			}
		}

		/* Only handle setup packet below */
		if (i == 0 &&
			hw_test_and_clear(ci, OP_ENDPTSETUPSTAT, BIT(0)))
			isr_setup_packet_handler(ci);
	}
}

/******************************************************************************
 * ENDPT block
 *****************************************************************************/
/*
 * ep_enable: configure endpoint, making it usable
 *
 * Check usb_ep_enable() at "usb_gadget.h" for details
 */
static int ep_enable(struct usb_ep *ep,
		     const struct usb_endpoint_descriptor *desc)
{
	struct ci_hw_ep *hwep = container_of(ep, struct ci_hw_ep, ep);
	int retval = 0;
	unsigned long flags;
	u32 cap = 0;

	if (ep == NULL || desc == NULL)
		return -EINVAL;

	spin_lock_irqsave(hwep->lock, flags);

	/* only internal SW should enable ctrl endpts */

	if (!list_empty(&hwep->qh.queue)) {
		dev_warn(hwep->ci->dev, "enabling a non-empty endpoint!\n");
		spin_unlock_irqrestore(hwep->lock, flags);
		return -EBUSY;
	}

	hwep->ep.desc = desc;

	hwep->dir  = usb_endpoint_dir_in(desc) ? TX : RX;
	hwep->num  = usb_endpoint_num(desc);
	hwep->type = usb_endpoint_type(desc);

	hwep->ep.maxpacket = usb_endpoint_maxp(desc);
	hwep->ep.mult = usb_endpoint_maxp_mult(desc);

	if (hwep->type == USB_ENDPOINT_XFER_CONTROL)
		cap |= QH_IOS;

	cap |= QH_ZLT;
	cap |= (hwep->ep.maxpacket << __ffs(QH_MAX_PKT)) & QH_MAX_PKT;
	/*
	 * For ISO-TX, we set mult at QH as the largest value, and use
	 * MultO at TD as real mult value.
	 */
	if (hwep->type == USB_ENDPOINT_XFER_ISOC && hwep->dir == TX)
		cap |= 3 << __ffs(QH_MULT);

	hwep->qh.ptr->cap = cpu_to_le32(cap);

	hwep->qh.ptr->td.next |= cpu_to_le32(TD_TERMINATE);   /* needed? */

	if (hwep->num != 0 && hwep->type == USB_ENDPOINT_XFER_CONTROL) {
		dev_err(hwep->ci->dev, "Set control xfer at non-ep0\n");
		retval = -EINVAL;
	}

	/*
	 * Enable endpoints in the HW other than ep0 as ep0
	 * is always enabled
	 */
	if (hwep->num)
		retval |= hw_ep_enable(hwep->ci, hwep->num, hwep->dir,
				       hwep->type);

	spin_unlock_irqrestore(hwep->lock, flags);
	return retval;
}

/*
 * ep_disable: endpoint is no longer usable
 *
 * Check usb_ep_disable() at "usb_gadget.h" for details
 */
static int ep_disable(struct usb_ep *ep)
{
	struct ci_hw_ep *hwep = container_of(ep, struct ci_hw_ep, ep);
	int direction, retval = 0;
	unsigned long flags;

	if (ep == NULL)
		return -EINVAL;
	else if (hwep->ep.desc == NULL)
		return -EBUSY;

	spin_lock_irqsave(hwep->lock, flags);
	if (hwep->ci->gadget.speed == USB_SPEED_UNKNOWN) {
		spin_unlock_irqrestore(hwep->lock, flags);
		return 0;
	}

	/* only internal SW should disable ctrl endpts */

	direction = hwep->dir;
	do {
		retval |= _ep_nuke(hwep);
		retval |= hw_ep_disable(hwep->ci, hwep->num, hwep->dir);

		if (hwep->type == USB_ENDPOINT_XFER_CONTROL)
			hwep->dir = (hwep->dir == TX) ? RX : TX;

	} while (hwep->dir != direction);

	hwep->ep.desc = NULL;

	spin_unlock_irqrestore(hwep->lock, flags);
	return retval;
}

/*
 * ep_alloc_request: allocate a request object to use with this endpoint
 *
 * Check usb_ep_alloc_request() at "usb_gadget.h" for details
 */
static struct usb_request *ep_alloc_request(struct usb_ep *ep, gfp_t gfp_flags)
{
	struct ci_hw_req *hwreq = NULL;

	if (ep == NULL)
		return NULL;

	hwreq = kzalloc(sizeof(struct ci_hw_req), gfp_flags);
	if (hwreq != NULL) {
		INIT_LIST_HEAD(&hwreq->queue);
		INIT_LIST_HEAD(&hwreq->tds);
	}

	return (hwreq == NULL) ? NULL : &hwreq->req;
}

/*
 * ep_free_request: frees a request object
 *
 * Check usb_ep_free_request() at "usb_gadget.h" for details
 */
static void ep_free_request(struct usb_ep *ep, struct usb_request *req)
{
	struct ci_hw_ep  *hwep  = container_of(ep,  struct ci_hw_ep, ep);
	struct ci_hw_req *hwreq = container_of(req, struct ci_hw_req, req);
	struct td_node *node, *tmpnode;
	unsigned long flags;

	if (ep == NULL || req == NULL) {
		return;
	} else if (!list_empty(&hwreq->queue)) {
		dev_err(hwep->ci->dev, "freeing queued request\n");
		return;
	}

	spin_lock_irqsave(hwep->lock, flags);

	list_for_each_entry_safe(node, tmpnode, &hwreq->tds, td) {
		dma_pool_free(hwep->td_pool, node->ptr, node->dma);
		list_del_init(&node->td);
		node->ptr = NULL;
		kfree(node);
	}

	kfree(hwreq);

	spin_unlock_irqrestore(hwep->lock, flags);
}

/*
 * ep_queue: queues (submits) an I/O request to an endpoint
 *
 * Check usb_ep_queue()* at usb_gadget.h" for details
 */
static int ep_queue(struct usb_ep *ep, struct usb_request *req,
		    gfp_t __maybe_unused gfp_flags)
{
	struct ci_hw_ep  *hwep  = container_of(ep,  struct ci_hw_ep, ep);
	int retval = 0;
	unsigned long flags;

	if (ep == NULL || req == NULL || hwep->ep.desc == NULL)
		return -EINVAL;

	spin_lock_irqsave(hwep->lock, flags);
	if (hwep->ci->gadget.speed == USB_SPEED_UNKNOWN) {
		spin_unlock_irqrestore(hwep->lock, flags);
		return 0;
	}
	retval = _ep_queue(ep, req, gfp_flags);
	spin_unlock_irqrestore(hwep->lock, flags);
	return retval;
}

/*
 * ep_dequeue: dequeues (cancels, unlinks) an I/O request from an endpoint
 *
 * Check usb_ep_dequeue() at "usb_gadget.h" for details
 */
static int ep_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	struct ci_hw_ep  *hwep  = container_of(ep,  struct ci_hw_ep, ep);
	struct ci_hw_req *hwreq = container_of(req, struct ci_hw_req, req);
	unsigned long flags;
	struct td_node *node, *tmpnode;

	if (ep == NULL || req == NULL || hwreq->req.status != -EALREADY ||
		hwep->ep.desc == NULL || list_empty(&hwreq->queue) ||
		list_empty(&hwep->qh.queue))
		return -EINVAL;

	spin_lock_irqsave(hwep->lock, flags);
	if (hwep->ci->gadget.speed != USB_SPEED_UNKNOWN)
		hw_ep_flush(hwep->ci, hwep->num, hwep->dir);

	list_for_each_entry_safe(node, tmpnode, &hwreq->tds, td) {
		dma_pool_free(hwep->td_pool, node->ptr, node->dma);
		list_del(&node->td);
		kfree(node);
	}

	/* pop request */
	list_del_init(&hwreq->queue);

	usb_gadget_unmap_request(&hwep->ci->gadget, req, hwep->dir);

	req->status = -ECONNRESET;

	if (hwreq->req.complete != NULL) {
		spin_unlock(hwep->lock);
		usb_gadget_giveback_request(&hwep->ep, &hwreq->req);
		spin_lock(hwep->lock);
	}

	spin_unlock_irqrestore(hwep->lock, flags);
	return 0;
}

/*
 * ep_set_halt: sets the endpoint halt feature
 *
 * Check usb_ep_set_halt() at "usb_gadget.h" for details
 */
static int ep_set_halt(struct usb_ep *ep, int value)
{
	return _ep_set_halt(ep, value, true);
}

/*
 * ep_set_wedge: sets the halt feature and ignores clear requests
 *
 * Check usb_ep_set_wedge() at "usb_gadget.h" for details
 */
static int ep_set_wedge(struct usb_ep *ep)
{
	struct ci_hw_ep *hwep = container_of(ep, struct ci_hw_ep, ep);
	unsigned long flags;

	if (ep == NULL || hwep->ep.desc == NULL)
		return -EINVAL;

	spin_lock_irqsave(hwep->lock, flags);
	hwep->wedge = 1;
	spin_unlock_irqrestore(hwep->lock, flags);

	return usb_ep_set_halt(ep);
}

/*
 * ep_fifo_flush: flushes contents of a fifo
 *
 * Check usb_ep_fifo_flush() at "usb_gadget.h" for details
 */
static void ep_fifo_flush(struct usb_ep *ep)
{
	struct ci_hw_ep *hwep = container_of(ep, struct ci_hw_ep, ep);
	unsigned long flags;

	if (ep == NULL) {
		dev_err(hwep->ci->dev, "%02X: -EINVAL\n", _usb_addr(hwep));
		return;
	}

	spin_lock_irqsave(hwep->lock, flags);
	if (hwep->ci->gadget.speed == USB_SPEED_UNKNOWN) {
		spin_unlock_irqrestore(hwep->lock, flags);
		return;
	}

	hw_ep_flush(hwep->ci, hwep->num, hwep->dir);

	spin_unlock_irqrestore(hwep->lock, flags);
}

/*
 * Endpoint-specific part of the API to the USB controller hardware
 * Check "usb_gadget.h" for details
 */
static const struct usb_ep_ops usb_ep_ops = {
	.enable	       = ep_enable,
	.disable       = ep_disable,
	.alloc_request = ep_alloc_request,
	.free_request  = ep_free_request,
	.queue	       = ep_queue,
	.dequeue       = ep_dequeue,
	.set_halt      = ep_set_halt,
	.set_wedge     = ep_set_wedge,
	.fifo_flush    = ep_fifo_flush,
};

/******************************************************************************
 * GADGET block
 *****************************************************************************/

static int ci_udc_get_frame(struct usb_gadget *_gadget)
{
	struct ci_hdrc *ci = container_of(_gadget, struct ci_hdrc, gadget);
	unsigned long flags;
	int ret;

	spin_lock_irqsave(&ci->lock, flags);
	ret = hw_read(ci, OP_FRINDEX, 0x3fff);
	spin_unlock_irqrestore(&ci->lock, flags);
	return ret >> 3;
}

/*
 * ci_hdrc_gadget_connect: caller makes sure gadget driver is binded
 */
static void ci_hdrc_gadget_connect(struct usb_gadget *_gadget, int is_active)
{
	struct ci_hdrc *ci = container_of(_gadget, struct ci_hdrc, gadget);

	if (is_active) {
		pm_runtime_get_sync(ci->dev);
		hw_device_reset(ci);
		spin_lock_irq(&ci->lock);
		if (ci->driver) {
			hw_device_state(ci, ci->ep0out->qh.dma);
			usb_gadget_set_state(_gadget, USB_STATE_POWERED);
			spin_unlock_irq(&ci->lock);
			usb_udc_vbus_handler(_gadget, true);
		} else {
			spin_unlock_irq(&ci->lock);
		}
	} else {
		usb_udc_vbus_handler(_gadget, false);
		if (ci->driver)
			ci->driver->disconnect(&ci->gadget);
		hw_device_state(ci, 0);
		if (ci->platdata->notify_event)
			ci->platdata->notify_event(ci,
			CI_HDRC_CONTROLLER_STOPPED_EVENT);
		_gadget_stop_activity(&ci->gadget);
		pm_runtime_put_sync(ci->dev);
		usb_gadget_set_state(_gadget, USB_STATE_NOTATTACHED);
	}
}

static int ci_udc_vbus_session(struct usb_gadget *_gadget, int is_active)
{
	struct ci_hdrc *ci = container_of(_gadget, struct ci_hdrc, gadget);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&ci->lock, flags);
	ci->vbus_active = is_active;
	spin_unlock_irqrestore(&ci->lock, flags);

	if (ci->usb_phy)
		usb_phy_set_charger_state(ci->usb_phy, is_active ?
			USB_CHARGER_PRESENT : USB_CHARGER_ABSENT);

	if (ci->platdata->notify_event)
		ret = ci->platdata->notify_event(ci,
				CI_HDRC_CONTROLLER_VBUS_EVENT);

	if (ci->driver)
		ci_hdrc_gadget_connect(_gadget, is_active);

	return ret;
}

static int ci_udc_wakeup(struct usb_gadget *_gadget)
{
	struct ci_hdrc *ci = container_of(_gadget, struct ci_hdrc, gadget);
	unsigned long flags;
	int ret = 0;

	spin_lock_irqsave(&ci->lock, flags);
	if (ci->gadget.speed == USB_SPEED_UNKNOWN) {
		spin_unlock_irqrestore(&ci->lock, flags);
		return 0;
	}
	if (!ci->remote_wakeup) {
		ret = -EOPNOTSUPP;
		goto out;
	}
	if (!hw_read(ci, OP_PORTSC, PORTSC_SUSP)) {
		ret = -EINVAL;
		goto out;
	}
	hw_write(ci, OP_PORTSC, PORTSC_FPR, PORTSC_FPR);
out:
	spin_unlock_irqrestore(&ci->lock, flags);
	return ret;
}

static int ci_udc_vbus_draw(struct usb_gadget *_gadget, unsigned ma)
{
	struct ci_hdrc *ci = container_of(_gadget, struct ci_hdrc, gadget);

	if (ci->usb_phy)
		return usb_phy_set_power(ci->usb_phy, ma);
	return -ENOTSUPP;
}

static int ci_udc_selfpowered(struct usb_gadget *_gadget, int is_on)
{
	struct ci_hdrc *ci = container_of(_gadget, struct ci_hdrc, gadget);
	struct ci_hw_ep *hwep = ci->ep0in;
	unsigned long flags;

	spin_lock_irqsave(hwep->lock, flags);
	_gadget->is_selfpowered = (is_on != 0);
	spin_unlock_irqrestore(hwep->lock, flags);

	return 0;
}

/* Change Data+ pullup status
 * this func is used by usb_gadget_connect/disconnect
 */
static int ci_udc_pullup(struct usb_gadget *_gadget, int is_on)
{
	struct ci_hdrc *ci = container_of(_gadget, struct ci_hdrc, gadget);

	/*
	 * Data+ pullup controlled by OTG state machine in OTG fsm mode;
	 * and don't touch Data+ in host mode for dual role config.
	 */
	if (ci_otg_is_fsm_mode(ci) || ci->role == CI_ROLE_HOST)
		return 0;

	pm_runtime_get_sync(ci->dev);
	if (is_on)
		hw_write(ci, OP_USBCMD, USBCMD_RS, USBCMD_RS);
	else
		hw_write(ci, OP_USBCMD, USBCMD_RS, 0);
	pm_runtime_put_sync(ci->dev);

	return 0;
}

static int ci_udc_start(struct usb_gadget *gadget,
			 struct usb_gadget_driver *driver);
static int ci_udc_stop(struct usb_gadget *gadget);

/* Match ISOC IN from the highest endpoint */
static struct usb_ep *ci_udc_match_ep(struct usb_gadget *gadget,
			      struct usb_endpoint_descriptor *desc,
			      struct usb_ss_ep_comp_descriptor *comp_desc)
{
	struct ci_hdrc *ci = container_of(gadget, struct ci_hdrc, gadget);
	struct usb_ep *ep;

	if (usb_endpoint_xfer_isoc(desc) && usb_endpoint_dir_in(desc)) {
		list_for_each_entry_reverse(ep, &ci->gadget.ep_list, ep_list) {
			if (ep->caps.dir_in && !ep->claimed)
				return ep;
		}
	}

	return NULL;
}

/*
 * Device operations part of the API to the USB controller hardware,
 * which don't involve endpoints (or i/o)
 * Check  "usb_gadget.h" for details
 */
static const struct usb_gadget_ops usb_gadget_ops = {
	.get_frame	= ci_udc_get_frame,
	.vbus_session	= ci_udc_vbus_session,
	.wakeup		= ci_udc_wakeup,
	.set_selfpowered	= ci_udc_selfpowered,
	.pullup		= ci_udc_pullup,
	.vbus_draw	= ci_udc_vbus_draw,
	.udc_start	= ci_udc_start,
	.udc_stop	= ci_udc_stop,
	.match_ep 	= ci_udc_match_ep,
};

static int init_eps(struct ci_hdrc *ci)
{
	int retval = 0, i, j;

	for (i = 0; i < ci->hw_ep_max/2; i++)
		for (j = RX; j <= TX; j++) {
			int k = i + j * ci->hw_ep_max/2;
			struct ci_hw_ep *hwep = &ci->ci_hw_ep[k];

			scnprintf(hwep->name, sizeof(hwep->name), "ep%i%s", i,
					(j == TX)  ? "in" : "out");

			hwep->ci          = ci;
			hwep->lock         = &ci->lock;
			hwep->td_pool      = ci->td_pool;

			hwep->ep.name      = hwep->name;
			hwep->ep.ops       = &usb_ep_ops;

			if (i == 0) {
				hwep->ep.caps.type_control = true;
			} else {
				hwep->ep.caps.type_iso = true;
				hwep->ep.caps.type_bulk = true;
				hwep->ep.caps.type_int = true;
			}

			if (j == TX)
				hwep->ep.caps.dir_in = true;
			else
				hwep->ep.caps.dir_out = true;

			/*
			 * for ep0: maxP defined in desc, for other
			 * eps, maxP is set by epautoconfig() called
			 * by gadget layer
			 */
			usb_ep_set_maxpacket_limit(&hwep->ep, (unsigned short)~0);

			INIT_LIST_HEAD(&hwep->qh.queue);
			hwep->qh.ptr = dma_pool_zalloc(ci->qh_pool, GFP_KERNEL,
						       &hwep->qh.dma);
			if (hwep->qh.ptr == NULL)
				retval = -ENOMEM;

			/*
			 * set up shorthands for ep0 out and in endpoints,
			 * don't add to gadget's ep_list
			 */
			if (i == 0) {
				if (j == RX)
					ci->ep0out = hwep;
				else
					ci->ep0in = hwep;

				usb_ep_set_maxpacket_limit(&hwep->ep, CTRL_PAYLOAD_MAX);
				continue;
			}

			list_add_tail(&hwep->ep.ep_list, &ci->gadget.ep_list);
		}

	return retval;
}

static void destroy_eps(struct ci_hdrc *ci)
{
	int i;

	for (i = 0; i < ci->hw_ep_max; i++) {
		struct ci_hw_ep *hwep = &ci->ci_hw_ep[i];

		if (hwep->pending_td)
			free_pending_td(hwep);
		dma_pool_free(ci->qh_pool, hwep->qh.ptr, hwep->qh.dma);
	}
}

/**
 * ci_udc_start: register a gadget driver
 * @gadget: our gadget
 * @driver: the driver being registered
 *
 * Interrupts are enabled here.
 */
static int ci_udc_start(struct usb_gadget *gadget,
			 struct usb_gadget_driver *driver)
{
	struct ci_hdrc *ci = container_of(gadget, struct ci_hdrc, gadget);
	int retval;

	if (driver->disconnect == NULL)
		return -EINVAL;

	ci->ep0out->ep.desc = &ctrl_endpt_out_desc;
	retval = usb_ep_enable(&ci->ep0out->ep);
	if (retval)
		return retval;

	ci->ep0in->ep.desc = &ctrl_endpt_in_desc;
	retval = usb_ep_enable(&ci->ep0in->ep);
	if (retval)
		return retval;

	ci->driver = driver;

	/* Start otg fsm for B-device */
	if (ci_otg_is_fsm_mode(ci) && ci->fsm.id) {
		ci_hdrc_otg_fsm_start(ci);
		return retval;
	}

	if (ci->vbus_active)
		ci_hdrc_gadget_connect(gadget, 1);
	else
		usb_udc_vbus_handler(&ci->gadget, false);

	return retval;
}

static void ci_udc_stop_for_otg_fsm(struct ci_hdrc *ci)
{
	if (!ci_otg_is_fsm_mode(ci))
		return;

	mutex_lock(&ci->fsm.lock);
	if (ci->fsm.otg->state == OTG_STATE_A_PERIPHERAL) {
		ci->fsm.a_bidl_adis_tmout = 1;
		ci_hdrc_otg_fsm_start(ci);
	} else if (ci->fsm.otg->state == OTG_STATE_B_PERIPHERAL) {
		ci->fsm.protocol = PROTO_UNDEF;
		ci->fsm.otg->state = OTG_STATE_UNDEFINED;
	}
	mutex_unlock(&ci->fsm.lock);
}

/*
 * ci_udc_stop: unregister a gadget driver
 */
static int ci_udc_stop(struct usb_gadget *gadget)
{
	struct ci_hdrc *ci = container_of(gadget, struct ci_hdrc, gadget);
	unsigned long flags;

	spin_lock_irqsave(&ci->lock, flags);
	ci->driver = NULL;

	if (ci->vbus_active) {
		hw_device_state(ci, 0);
		spin_unlock_irqrestore(&ci->lock, flags);
		if (ci->platdata->notify_event)
			ci->platdata->notify_event(ci,
			CI_HDRC_CONTROLLER_STOPPED_EVENT);
		_gadget_stop_activity(&ci->gadget);
		spin_lock_irqsave(&ci->lock, flags);
		pm_runtime_put(ci->dev);
	}

	spin_unlock_irqrestore(&ci->lock, flags);

	ci_udc_stop_for_otg_fsm(ci);
	return 0;
}

/******************************************************************************
 * BUS block
 *****************************************************************************/
/*
 * udc_irq: ci interrupt handler
 *
 * This function returns IRQ_HANDLED if the IRQ has been handled
 * It locks access to registers
 */
static irqreturn_t udc_irq(struct ci_hdrc *ci)
{
	irqreturn_t retval;
	u32 intr;

	if (ci == NULL)
		return IRQ_HANDLED;

	spin_lock(&ci->lock);

	if (ci->platdata->flags & CI_HDRC_REGS_SHARED) {
		if (hw_read(ci, OP_USBMODE, USBMODE_CM) !=
				USBMODE_CM_DC) {
			spin_unlock(&ci->lock);
			return IRQ_NONE;
		}
	}
	intr = hw_test_and_clear_intr_active(ci);

	if (intr) {
		/* order defines priority - do NOT change it */
		if (USBi_URI & intr)
			isr_reset_handler(ci);

		if (USBi_PCI & intr) {
			ci->gadget.speed = hw_port_is_high_speed(ci) ?
				USB_SPEED_HIGH : USB_SPEED_FULL;
			if (ci->suspended) {
				if (ci->driver->resume) {
					spin_unlock(&ci->lock);
					ci->driver->resume(&ci->gadget);
					spin_lock(&ci->lock);
				}
				ci->suspended = 0;
				usb_gadget_set_state(&ci->gadget,
						ci->resume_state);
			}
		}

		if (USBi_UI  & intr)
			isr_tr_complete_handler(ci);

		if ((USBi_SLI & intr) && !(ci->suspended)) {
			ci->suspended = 1;
			ci->resume_state = ci->gadget.state;
			if (ci->gadget.speed != USB_SPEED_UNKNOWN &&
			    ci->driver->suspend) {
				spin_unlock(&ci->lock);
				ci->driver->suspend(&ci->gadget);
				spin_lock(&ci->lock);
			}
			usb_gadget_set_state(&ci->gadget,
					USB_STATE_SUSPENDED);
		}
		retval = IRQ_HANDLED;
	} else {
		retval = IRQ_NONE;
	}
	spin_unlock(&ci->lock);

	return retval;
}

/**
 * udc_start: initialize gadget role
 * @ci: chipidea controller
 */
static int udc_start(struct ci_hdrc *ci)
{
	struct device *dev = ci->dev;
	struct usb_otg_caps *otg_caps = &ci->platdata->ci_otg_caps;
	int retval = 0;

	ci->gadget.ops          = &usb_gadget_ops;
	ci->gadget.speed        = USB_SPEED_UNKNOWN;
	ci->gadget.max_speed    = USB_SPEED_HIGH;
	ci->gadget.name         = ci->platdata->name;
	ci->gadget.otg_caps	= otg_caps;
	ci->gadget.sg_supported = 1;
	ci->gadget.irq		= ci->irq;

	if (ci->platdata->flags & CI_HDRC_REQUIRES_ALIGNED_DMA)
		ci->gadget.quirk_avoids_skb_reserve = 1;

	if (ci->is_otg && (otg_caps->hnp_support || otg_caps->srp_support ||
						otg_caps->adp_support))
		ci->gadget.is_otg = 1;

	INIT_LIST_HEAD(&ci->gadget.ep_list);

	/* alloc resources */
	ci->qh_pool = dma_pool_create("ci_hw_qh", dev->parent,
				       sizeof(struct ci_hw_qh),
				       64, CI_HDRC_PAGE_SIZE);
	if (ci->qh_pool == NULL)
		return -ENOMEM;

	ci->td_pool = dma_pool_create("ci_hw_td", dev->parent,
				       sizeof(struct ci_hw_td),
				       64, CI_HDRC_PAGE_SIZE);
	if (ci->td_pool == NULL) {
		retval = -ENOMEM;
		goto free_qh_pool;
	}

	retval = init_eps(ci);
	if (retval)
		goto free_pools;

	ci->gadget.ep0 = &ci->ep0in->ep;

	retval = usb_add_gadget_udc(dev, &ci->gadget);
	if (retval)
		goto destroy_eps;

	return retval;

destroy_eps:
	destroy_eps(ci);
free_pools:
	dma_pool_destroy(ci->td_pool);
free_qh_pool:
	dma_pool_destroy(ci->qh_pool);
	return retval;
}

/*
 * ci_hdrc_gadget_destroy: parent remove must call this to remove UDC
 *
 * No interrupts active, the IRQ has been released
 */
void ci_hdrc_gadget_destroy(struct ci_hdrc *ci)
{
	if (!ci->roles[CI_ROLE_GADGET])
		return;

	usb_del_gadget_udc(&ci->gadget);

	destroy_eps(ci);

	dma_pool_destroy(ci->td_pool);
	dma_pool_destroy(ci->qh_pool);
}

static int udc_id_switch_for_device(struct ci_hdrc *ci)
{
	if (ci->platdata->pins_device)
		pinctrl_select_state(ci->platdata->pctl,
				     ci->platdata->pins_device);

	if (ci->is_otg)
		/* Clear and enable BSV irq */
		hw_write_otgsc(ci, OTGSC_BSVIS | OTGSC_BSVIE,
					OTGSC_BSVIS | OTGSC_BSVIE);

	return 0;
}

static void udc_id_switch_for_host(struct ci_hdrc *ci)
{
	/*
	 * host doesn't care B_SESSION_VALID event
	 * so clear and disable BSV irq
	 */
	if (ci->is_otg)
		hw_write_otgsc(ci, OTGSC_BSVIE | OTGSC_BSVIS, OTGSC_BSVIS);

	ci->vbus_active = 0;

	if (ci->platdata->pins_device && ci->platdata->pins_default)
		pinctrl_select_state(ci->platdata->pctl,
				     ci->platdata->pins_default);
}

#ifdef CONFIG_PM_SLEEP
static void udc_suspend(struct ci_hdrc *ci)
{
	/*
	 * Set OP_ENDPTLISTADDR to be non-zero for
	 * checking if controller resume from power lost
	 * in non-host mode.
	 */
	if (hw_read(ci, OP_ENDPTLISTADDR, ~0) == 0)
		hw_write(ci, OP_ENDPTLISTADDR, ~0, ~0);
}

static void udc_resume(struct ci_hdrc *ci, bool power_lost)
{
	if (power_lost) {
		if (ci->is_otg)
			hw_write_otgsc(ci, OTGSC_BSVIS | OTGSC_BSVIE,
					OTGSC_BSVIS | OTGSC_BSVIE);
		if (ci->vbus_active)
			usb_gadget_vbus_disconnect(&ci->gadget);
	}

	/* Restore value 0 if it was set for power lost check */
	if (hw_read(ci, OP_ENDPTLISTADDR, ~0) == 0xFFFFFFFF)
		hw_write(ci, OP_ENDPTLISTADDR, ~0, 0);
}
#endif

/**
 * ci_hdrc_gadget_init - initialize device related bits
 * @ci: the controller
 *
 * This function initializes the gadget, if the device is "device capable".
 */
int ci_hdrc_gadget_init(struct ci_hdrc *ci)
{
	struct ci_role_driver *rdrv;
	int ret;

	if (!hw_read(ci, CAP_DCCPARAMS, DCCPARAMS_DC))
		return -ENXIO;

	rdrv = devm_kzalloc(ci->dev, sizeof(*rdrv), GFP_KERNEL);
	if (!rdrv)
		return -ENOMEM;

	rdrv->start	= udc_id_switch_for_device;
	rdrv->stop	= udc_id_switch_for_host;
#ifdef CONFIG_PM_SLEEP
	rdrv->suspend	= udc_suspend;
	rdrv->resume	= udc_resume;
#endif
	rdrv->irq	= udc_irq;
	rdrv->name	= "gadget";

	ret = udc_start(ci);
	if (!ret)
		ci->roles[CI_ROLE_GADGET] = rdrv;

	return ret;
}
