// SPDX-License-Identifier: GPL-2.0
/*
 * mtu3_gadget_ep0.c - MediaTek USB3 DRD peripheral driver ep0 handling
 *
 * Copyright (c) 2016 MediaTek Inc.
 *
 * Author:  Chunfeng.Yun <chunfeng.yun@mediatek.com>
 */

#include <linux/iopoll.h>
#include <linux/usb/composite.h>

#include "mtu3.h"
#include "mtu3_debug.h"
#include "mtu3_trace.h"

/* ep0 is always mtu3->in_eps[0] */
#define	next_ep0_request(mtu)	next_request((mtu)->ep0)

/* for high speed test mode; see USB 2.0 spec 7.1.20 */
static const u8 mtu3_test_packet[53] = {
	/* implicit SYNC then DATA0 to start */

	/* JKJKJKJK x9 */
	0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
	/* JJKKJJKK x8 */
	0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa, 0xaa,
	/* JJJJKKKK x8 */
	0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee, 0xee,
	/* JJJJJJJKKKKKKK x8 */
	0xfe, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff,
	/* JJJJJJJK x8 */
	0x7f, 0xbf, 0xdf, 0xef, 0xf7, 0xfb, 0xfd,
	/* JKKKKKKK x10, JK */
	0xfc, 0x7e, 0xbf, 0xdf, 0xef, 0xf7, 0xfb, 0xfd, 0x7e,
	/* implicit CRC16 then EOP to end */
};

static char *decode_ep0_state(struct mtu3 *mtu)
{
	switch (mtu->ep0_state) {
	case MU3D_EP0_STATE_SETUP:
		return "SETUP";
	case MU3D_EP0_STATE_TX:
		return "IN";
	case MU3D_EP0_STATE_RX:
		return "OUT";
	case MU3D_EP0_STATE_TX_END:
		return "TX-END";
	case MU3D_EP0_STATE_STALL:
		return "STALL";
	default:
		return "??";
	}
}

static void ep0_req_giveback(struct mtu3 *mtu, struct usb_request *req)
{
	mtu3_req_complete(mtu->ep0, req, 0);
}

static int
forward_to_driver(struct mtu3 *mtu, const struct usb_ctrlrequest *setup)
__releases(mtu->lock)
__acquires(mtu->lock)
{
	int ret;

	if (!mtu->gadget_driver)
		return -EOPNOTSUPP;

	spin_unlock(&mtu->lock);
	ret = mtu->gadget_driver->setup(&mtu->g, setup);
	spin_lock(&mtu->lock);

	dev_dbg(mtu->dev, "%s ret %d\n", __func__, ret);
	return ret;
}

static void ep0_write_fifo(struct mtu3_ep *mep, const u8 *src, u16 len)
{
	void __iomem *fifo = mep->mtu->mac_base + U3D_FIFO0;
	u16 index = 0;

	dev_dbg(mep->mtu->dev, "%s: ep%din, len=%d, buf=%p\n",
		__func__, mep->epnum, len, src);

	if (len >= 4) {
		iowrite32_rep(fifo, src, len >> 2);
		index = len & ~0x03;
	}
	if (len & 0x02) {
		writew(*(u16 *)&src[index], fifo);
		index += 2;
	}
	if (len & 0x01)
		writeb(src[index], fifo);
}

static void ep0_read_fifo(struct mtu3_ep *mep, u8 *dst, u16 len)
{
	void __iomem *fifo = mep->mtu->mac_base + U3D_FIFO0;
	u32 value;
	u16 index = 0;

	dev_dbg(mep->mtu->dev, "%s: ep%dout len=%d buf=%p\n",
		 __func__, mep->epnum, len, dst);

	if (len >= 4) {
		ioread32_rep(fifo, dst, len >> 2);
		index = len & ~0x03;
	}
	if (len & 0x3) {
		value = readl(fifo);
		memcpy(&dst[index], &value, len & 0x3);
	}

}

static void ep0_load_test_packet(struct mtu3 *mtu)
{
	/*
	 * because the length of test packet is less than max packet of HS ep0,
	 * write it into fifo directly.
	 */
	ep0_write_fifo(mtu->ep0, mtu3_test_packet, sizeof(mtu3_test_packet));
}

/*
 * A. send STALL for setup transfer without data stage:
 *		set SENDSTALL and SETUPPKTRDY at the same time;
 * B. send STALL for other cases:
 *		set SENDSTALL only.
 */
static void ep0_stall_set(struct mtu3_ep *mep0, bool set, u32 pktrdy)
{
	struct mtu3 *mtu = mep0->mtu;
	void __iomem *mbase = mtu->mac_base;
	u32 csr;

	/* EP0_SENTSTALL is W1C */
	csr = mtu3_readl(mbase, U3D_EP0CSR) & EP0_W1C_BITS;
	if (set)
		csr |= EP0_SENDSTALL | pktrdy;
	else
		csr = (csr & ~EP0_SENDSTALL) | EP0_SENTSTALL;
	mtu3_writel(mtu->mac_base, U3D_EP0CSR, csr);

	mtu->delayed_status = false;
	mtu->ep0_state = MU3D_EP0_STATE_SETUP;

	dev_dbg(mtu->dev, "ep0: %s STALL, ep0_state: %s\n",
		set ? "SEND" : "CLEAR", decode_ep0_state(mtu));
}

static void ep0_do_status_stage(struct mtu3 *mtu)
{
	void __iomem *mbase = mtu->mac_base;
	u32 value;

	value = mtu3_readl(mbase, U3D_EP0CSR) & EP0_W1C_BITS;
	mtu3_writel(mbase, U3D_EP0CSR, value | EP0_SETUPPKTRDY | EP0_DATAEND);
}

static int ep0_queue(struct mtu3_ep *mep0, struct mtu3_request *mreq);

static void ep0_dummy_complete(struct usb_ep *ep, struct usb_request *req)
{}

static void ep0_set_sel_complete(struct usb_ep *ep, struct usb_request *req)
{
	struct mtu3_request *mreq;
	struct mtu3 *mtu;
	struct usb_set_sel_req sel;

	memcpy(&sel, req->buf, sizeof(sel));

	mreq = to_mtu3_request(req);
	mtu = mreq->mtu;
	dev_dbg(mtu->dev, "u1sel:%d, u1pel:%d, u2sel:%d, u2pel:%d\n",
		sel.u1_sel, sel.u1_pel, sel.u2_sel, sel.u2_pel);
}

/* queue data stage to handle 6 byte SET_SEL request */
static int ep0_set_sel(struct mtu3 *mtu, struct usb_ctrlrequest *setup)
{
	int ret;
	u16 length = le16_to_cpu(setup->wLength);

	if (unlikely(length != 6)) {
		dev_err(mtu->dev, "%s wrong wLength:%d\n",
			__func__, length);
		return -EINVAL;
	}

	mtu->ep0_req.mep = mtu->ep0;
	mtu->ep0_req.request.length = 6;
	mtu->ep0_req.request.buf = mtu->setup_buf;
	mtu->ep0_req.request.complete = ep0_set_sel_complete;
	ret = ep0_queue(mtu->ep0, &mtu->ep0_req);

	return ret < 0 ? ret : 1;
}

static int
ep0_get_status(struct mtu3 *mtu, const struct usb_ctrlrequest *setup)
{
	struct mtu3_ep *mep = NULL;
	int handled = 1;
	u8 result[2] = {0, 0};
	u8 epnum = 0;
	int is_in;

	switch (setup->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		result[0] = mtu->is_self_powered << USB_DEVICE_SELF_POWERED;
		result[0] |= mtu->may_wakeup << USB_DEVICE_REMOTE_WAKEUP;

		if (mtu->g.speed >= USB_SPEED_SUPER) {
			result[0] |= mtu->u1_enable << USB_DEV_STAT_U1_ENABLED;
			result[0] |= mtu->u2_enable << USB_DEV_STAT_U2_ENABLED;
		}

		dev_dbg(mtu->dev, "%s result=%x, U1=%x, U2=%x\n", __func__,
			result[0], mtu->u1_enable, mtu->u2_enable);

		break;
	case USB_RECIP_INTERFACE:
		break;
	case USB_RECIP_ENDPOINT:
		epnum = (u8) le16_to_cpu(setup->wIndex);
		is_in = epnum & USB_DIR_IN;
		epnum &= USB_ENDPOINT_NUMBER_MASK;

		if (epnum >= mtu->num_eps) {
			handled = -EINVAL;
			break;
		}
		if (!epnum)
			break;

		mep = (is_in ? mtu->in_eps : mtu->out_eps) + epnum;
		if (!mep->desc) {
			handled = -EINVAL;
			break;
		}
		if (mep->flags & MTU3_EP_STALL)
			result[0] |= 1 << USB_ENDPOINT_HALT;

		break;
	default:
		/* class, vendor, etc ... delegate */
		handled = 0;
		break;
	}

	if (handled > 0) {
		int ret;

		/* prepare a data stage for GET_STATUS */
		dev_dbg(mtu->dev, "get_status=%x\n", *(u16 *)result);
		memcpy(mtu->setup_buf, result, sizeof(result));
		mtu->ep0_req.mep = mtu->ep0;
		mtu->ep0_req.request.length = 2;
		mtu->ep0_req.request.buf = &mtu->setup_buf;
		mtu->ep0_req.request.complete = ep0_dummy_complete;
		ret = ep0_queue(mtu->ep0, &mtu->ep0_req);
		if (ret < 0)
			handled = ret;
	}
	return handled;
}

static int handle_test_mode(struct mtu3 *mtu, struct usb_ctrlrequest *setup)
{
	void __iomem *mbase = mtu->mac_base;
	int handled = 1;
	u32 value;

	switch (le16_to_cpu(setup->wIndex) >> 8) {
	case USB_TEST_J:
		dev_dbg(mtu->dev, "USB_TEST_J\n");
		mtu->test_mode_nr = TEST_J_MODE;
		break;
	case USB_TEST_K:
		dev_dbg(mtu->dev, "USB_TEST_K\n");
		mtu->test_mode_nr = TEST_K_MODE;
		break;
	case USB_TEST_SE0_NAK:
		dev_dbg(mtu->dev, "USB_TEST_SE0_NAK\n");
		mtu->test_mode_nr = TEST_SE0_NAK_MODE;
		break;
	case USB_TEST_PACKET:
		dev_dbg(mtu->dev, "USB_TEST_PACKET\n");
		mtu->test_mode_nr = TEST_PACKET_MODE;
		break;
	default:
		handled = -EINVAL;
		goto out;
	}

	mtu->test_mode = true;

	/* no TX completion interrupt, and need restart platform after test */
	if (mtu->test_mode_nr == TEST_PACKET_MODE)
		ep0_load_test_packet(mtu);

	/* send status before entering test mode. */
	ep0_do_status_stage(mtu);

	/* wait for ACK status sent by host */
	readl_poll_timeout_atomic(mbase + U3D_EP0CSR, value,
			!(value & EP0_DATAEND), 100, 5000);

	mtu3_writel(mbase, U3D_USB2_TEST_MODE, mtu->test_mode_nr);

	mtu->ep0_state = MU3D_EP0_STATE_SETUP;

out:
	return handled;
}

static int ep0_handle_feature_dev(struct mtu3 *mtu,
		struct usb_ctrlrequest *setup, bool set)
{
	void __iomem *mbase = mtu->mac_base;
	int handled = -EINVAL;
	u32 lpc;

	switch (le16_to_cpu(setup->wValue)) {
	case USB_DEVICE_REMOTE_WAKEUP:
		mtu->may_wakeup = !!set;
		handled = 1;
		break;
	case USB_DEVICE_TEST_MODE:
		if (!set || (mtu->g.speed != USB_SPEED_HIGH) ||
			(le16_to_cpu(setup->wIndex) & 0xff))
			break;

		handled = handle_test_mode(mtu, setup);
		break;
	case USB_DEVICE_U1_ENABLE:
		if (mtu->g.speed < USB_SPEED_SUPER ||
		    mtu->g.state != USB_STATE_CONFIGURED)
			break;

		lpc = mtu3_readl(mbase, U3D_LINK_POWER_CONTROL);
		if (set)
			lpc |= SW_U1_REQUEST_ENABLE;
		else
			lpc &= ~SW_U1_REQUEST_ENABLE;
		mtu3_writel(mbase, U3D_LINK_POWER_CONTROL, lpc);

		mtu->u1_enable = !!set;
		handled = 1;
		break;
	case USB_DEVICE_U2_ENABLE:
		if (mtu->g.speed < USB_SPEED_SUPER ||
		    mtu->g.state != USB_STATE_CONFIGURED)
			break;

		lpc = mtu3_readl(mbase, U3D_LINK_POWER_CONTROL);
		if (set)
			lpc |= SW_U2_REQUEST_ENABLE;
		else
			lpc &= ~SW_U2_REQUEST_ENABLE;
		mtu3_writel(mbase, U3D_LINK_POWER_CONTROL, lpc);

		mtu->u2_enable = !!set;
		handled = 1;
		break;
	default:
		handled = -EINVAL;
		break;
	}
	return handled;
}

static int ep0_handle_feature(struct mtu3 *mtu,
		struct usb_ctrlrequest *setup, bool set)
{
	struct mtu3_ep *mep;
	int handled = -EINVAL;
	int is_in;
	u16 value;
	u16 index;
	u8 epnum;

	value = le16_to_cpu(setup->wValue);
	index = le16_to_cpu(setup->wIndex);

	switch (setup->bRequestType & USB_RECIP_MASK) {
	case USB_RECIP_DEVICE:
		handled = ep0_handle_feature_dev(mtu, setup, set);
		break;
	case USB_RECIP_INTERFACE:
		/* superspeed only */
		if (value == USB_INTRF_FUNC_SUSPEND &&
		    mtu->g.speed >= USB_SPEED_SUPER) {
			/*
			 * forward the request because function drivers
			 * should handle it
			 */
			handled = 0;
		}
		break;
	case USB_RECIP_ENDPOINT:
		epnum = index & USB_ENDPOINT_NUMBER_MASK;
		if (epnum == 0 || epnum >= mtu->num_eps ||
			value != USB_ENDPOINT_HALT)
			break;

		is_in = index & USB_DIR_IN;
		mep = (is_in ? mtu->in_eps : mtu->out_eps) + epnum;
		if (!mep->desc)
			break;

		handled = 1;
		/* ignore request if endpoint is wedged */
		if (mep->flags & MTU3_EP_WEDGE)
			break;

		mtu3_ep_stall_set(mep, set);
		break;
	default:
		/* class, vendor, etc ... delegate */
		handled = 0;
		break;
	}
	return handled;
}

/*
 * handle all control requests can be handled
 * returns:
 *	negative errno - error happened
 *	zero - need delegate SETUP to gadget driver
 *	positive - already handled
 */
static int handle_standard_request(struct mtu3 *mtu,
			  struct usb_ctrlrequest *setup)
{
	void __iomem *mbase = mtu->mac_base;
	enum usb_device_state state = mtu->g.state;
	int handled = -EINVAL;
	u32 dev_conf;
	u16 value;

	value = le16_to_cpu(setup->wValue);

	/* the gadget driver handles everything except what we must handle */
	switch (setup->bRequest) {
	case USB_REQ_SET_ADDRESS:
		/* change it after the status stage */
		mtu->address = (u8) (value & 0x7f);
		dev_dbg(mtu->dev, "set address to 0x%x\n", mtu->address);

		dev_conf = mtu3_readl(mbase, U3D_DEVICE_CONF);
		dev_conf &= ~DEV_ADDR_MSK;
		dev_conf |= DEV_ADDR(mtu->address);
		mtu3_writel(mbase, U3D_DEVICE_CONF, dev_conf);

		if (mtu->address)
			usb_gadget_set_state(&mtu->g, USB_STATE_ADDRESS);
		else
			usb_gadget_set_state(&mtu->g, USB_STATE_DEFAULT);

		handled = 1;
		break;
	case USB_REQ_SET_CONFIGURATION:
		if (state == USB_STATE_ADDRESS) {
			usb_gadget_set_state(&mtu->g,
					USB_STATE_CONFIGURED);
		} else if (state == USB_STATE_CONFIGURED) {
			/*
			 * USB2 spec sec 9.4.7, if wValue is 0 then dev
			 * is moved to addressed state
			 */
			if (!value)
				usb_gadget_set_state(&mtu->g,
						USB_STATE_ADDRESS);
		}
		handled = 0;
		break;
	case USB_REQ_CLEAR_FEATURE:
		handled = ep0_handle_feature(mtu, setup, 0);
		break;
	case USB_REQ_SET_FEATURE:
		handled = ep0_handle_feature(mtu, setup, 1);
		break;
	case USB_REQ_GET_STATUS:
		handled = ep0_get_status(mtu, setup);
		break;
	case USB_REQ_SET_SEL:
		handled = ep0_set_sel(mtu, setup);
		break;
	case USB_REQ_SET_ISOCH_DELAY:
		handled = 1;
		break;
	default:
		/* delegate SET_CONFIGURATION, etc */
		handled = 0;
	}

	return handled;
}

/* receive an data packet (OUT) */
static void ep0_rx_state(struct mtu3 *mtu)
{
	struct mtu3_request *mreq;
	struct usb_request *req;
	void __iomem *mbase = mtu->mac_base;
	u32 maxp;
	u32 csr;
	u16 count = 0;

	dev_dbg(mtu->dev, "%s\n", __func__);

	csr = mtu3_readl(mbase, U3D_EP0CSR) & EP0_W1C_BITS;
	mreq = next_ep0_request(mtu);
	req = &mreq->request;

	/* read packet and ack; or stall because of gadget driver bug */
	if (req) {
		void *buf = req->buf + req->actual;
		unsigned int len = req->length - req->actual;

		/* read the buffer */
		count = mtu3_readl(mbase, U3D_RXCOUNT0);
		if (count > len) {
			req->status = -EOVERFLOW;
			count = len;
		}
		ep0_read_fifo(mtu->ep0, buf, count);
		req->actual += count;
		csr |= EP0_RXPKTRDY;

		maxp = mtu->g.ep0->maxpacket;
		if (count < maxp || req->actual == req->length) {
			mtu->ep0_state = MU3D_EP0_STATE_SETUP;
			dev_dbg(mtu->dev, "ep0 state: %s\n",
				decode_ep0_state(mtu));

			csr |= EP0_DATAEND;
		} else {
			req = NULL;
		}
	} else {
		csr |= EP0_RXPKTRDY | EP0_SENDSTALL;
		dev_dbg(mtu->dev, "%s: SENDSTALL\n", __func__);
	}

	mtu3_writel(mbase, U3D_EP0CSR, csr);

	/* give back the request if have received all data */
	if (req)
		ep0_req_giveback(mtu, req);

}

/* transmitting to the host (IN) */
static void ep0_tx_state(struct mtu3 *mtu)
{
	struct mtu3_request *mreq = next_ep0_request(mtu);
	struct usb_request *req;
	u32 csr;
	u8 *src;
	u32 count;
	u32 maxp;

	dev_dbg(mtu->dev, "%s\n", __func__);

	if (!mreq)
		return;

	maxp = mtu->g.ep0->maxpacket;
	req = &mreq->request;

	/* load the data */
	src = (u8 *)req->buf + req->actual;
	count = min(maxp, req->length - req->actual);
	if (count)
		ep0_write_fifo(mtu->ep0, src, count);

	dev_dbg(mtu->dev, "%s act=%d, len=%d, cnt=%d, maxp=%d zero=%d\n",
		 __func__, req->actual, req->length, count, maxp, req->zero);

	req->actual += count;

	if ((count < maxp)
		|| ((req->actual == req->length) && !req->zero))
		mtu->ep0_state = MU3D_EP0_STATE_TX_END;

	/* send it out, triggering a "txpktrdy cleared" irq */
	csr = mtu3_readl(mtu->mac_base, U3D_EP0CSR) & EP0_W1C_BITS;
	mtu3_writel(mtu->mac_base, U3D_EP0CSR, csr | EP0_TXPKTRDY);

	dev_dbg(mtu->dev, "%s ep0csr=0x%x\n", __func__,
		mtu3_readl(mtu->mac_base, U3D_EP0CSR));
}

static void ep0_read_setup(struct mtu3 *mtu, struct usb_ctrlrequest *setup)
{
	struct mtu3_request *mreq;
	u32 count;
	u32 csr;

	csr = mtu3_readl(mtu->mac_base, U3D_EP0CSR) & EP0_W1C_BITS;
	count = mtu3_readl(mtu->mac_base, U3D_RXCOUNT0);

	ep0_read_fifo(mtu->ep0, (u8 *)setup, count);

	dev_dbg(mtu->dev, "SETUP req%02x.%02x v%04x i%04x l%04x\n",
		 setup->bRequestType, setup->bRequest,
		 le16_to_cpu(setup->wValue), le16_to_cpu(setup->wIndex),
		 le16_to_cpu(setup->wLength));

	/* clean up any leftover transfers */
	mreq = next_ep0_request(mtu);
	if (mreq)
		ep0_req_giveback(mtu, &mreq->request);

	if (le16_to_cpu(setup->wLength) == 0) {
		;	/* no data stage, nothing to do */
	} else if (setup->bRequestType & USB_DIR_IN) {
		mtu3_writel(mtu->mac_base, U3D_EP0CSR,
			csr | EP0_SETUPPKTRDY | EP0_DPHTX);
		mtu->ep0_state = MU3D_EP0_STATE_TX;
	} else {
		mtu3_writel(mtu->mac_base, U3D_EP0CSR,
			(csr | EP0_SETUPPKTRDY) & (~EP0_DPHTX));
		mtu->ep0_state = MU3D_EP0_STATE_RX;
	}
}

static int ep0_handle_setup(struct mtu3 *mtu)
__releases(mtu->lock)
__acquires(mtu->lock)
{
	struct usb_ctrlrequest setup;
	struct mtu3_request *mreq;
	int handled = 0;

	ep0_read_setup(mtu, &setup);
	trace_mtu3_handle_setup(&setup);

	if ((setup.bRequestType & USB_TYPE_MASK) == USB_TYPE_STANDARD)
		handled = handle_standard_request(mtu, &setup);

	dev_dbg(mtu->dev, "handled %d, ep0_state: %s\n",
		 handled, decode_ep0_state(mtu));

	if (handled < 0)
		goto stall;
	else if (handled > 0)
		goto finish;

	handled = forward_to_driver(mtu, &setup);
	if (handled < 0) {
stall:
		dev_dbg(mtu->dev, "%s stall (%d)\n", __func__, handled);

		ep0_stall_set(mtu->ep0, true,
			le16_to_cpu(setup.wLength) ? 0 : EP0_SETUPPKTRDY);

		return 0;
	}

finish:
	if (mtu->test_mode) {
		;	/* nothing to do */
	} else if (handled == USB_GADGET_DELAYED_STATUS) {

		mreq = next_ep0_request(mtu);
		if (mreq) {
			/* already asked us to continue delayed status */
			ep0_do_status_stage(mtu);
			ep0_req_giveback(mtu, &mreq->request);
		} else {
			/* do delayed STATUS stage till receive ep0_queue */
			mtu->delayed_status = true;
		}
	} else if (le16_to_cpu(setup.wLength) == 0) { /* no data stage */

		ep0_do_status_stage(mtu);
		/* complete zlp request directly */
		mreq = next_ep0_request(mtu);
		if (mreq && !mreq->request.length)
			ep0_req_giveback(mtu, &mreq->request);
	}

	return 0;
}

irqreturn_t mtu3_ep0_isr(struct mtu3 *mtu)
{
	void __iomem *mbase = mtu->mac_base;
	struct mtu3_request *mreq;
	u32 int_status;
	irqreturn_t ret = IRQ_NONE;
	u32 csr;
	u32 len;

	int_status = mtu3_readl(mbase, U3D_EPISR);
	int_status &= mtu3_readl(mbase, U3D_EPIER);
	mtu3_writel(mbase, U3D_EPISR, int_status); /* W1C */

	/* only handle ep0's */
	if (!(int_status & (EP0ISR | SETUPENDISR)))
		return IRQ_NONE;

	/* abort current SETUP, and process new one */
	if (int_status & SETUPENDISR)
		mtu->ep0_state = MU3D_EP0_STATE_SETUP;

	csr = mtu3_readl(mbase, U3D_EP0CSR);

	dev_dbg(mtu->dev, "%s csr=0x%x\n", __func__, csr);

	/* we sent a stall.. need to clear it now.. */
	if (csr & EP0_SENTSTALL) {
		ep0_stall_set(mtu->ep0, false, 0);
		csr = mtu3_readl(mbase, U3D_EP0CSR);
		ret = IRQ_HANDLED;
	}
	dev_dbg(mtu->dev, "ep0_state: %s\n", decode_ep0_state(mtu));
	mtu3_dbg_trace(mtu->dev, "ep0_state %s", decode_ep0_state(mtu));

	switch (mtu->ep0_state) {
	case MU3D_EP0_STATE_TX:
		/* irq on clearing txpktrdy */
		if ((csr & EP0_FIFOFULL) == 0) {
			ep0_tx_state(mtu);
			ret = IRQ_HANDLED;
		}
		break;
	case MU3D_EP0_STATE_RX:
		/* irq on set rxpktrdy */
		if (csr & EP0_RXPKTRDY) {
			ep0_rx_state(mtu);
			ret = IRQ_HANDLED;
		}
		break;
	case MU3D_EP0_STATE_TX_END:
		mtu3_writel(mbase, U3D_EP0CSR,
			(csr & EP0_W1C_BITS) | EP0_DATAEND);

		mreq = next_ep0_request(mtu);
		if (mreq)
			ep0_req_giveback(mtu, &mreq->request);

		mtu->ep0_state = MU3D_EP0_STATE_SETUP;
		ret = IRQ_HANDLED;
		dev_dbg(mtu->dev, "ep0_state: %s\n", decode_ep0_state(mtu));
		break;
	case MU3D_EP0_STATE_SETUP:
		if (!(csr & EP0_SETUPPKTRDY))
			break;

		len = mtu3_readl(mbase, U3D_RXCOUNT0);
		if (len != 8) {
			dev_err(mtu->dev, "SETUP packet len %d != 8 ?\n", len);
			break;
		}

		ep0_handle_setup(mtu);
		ret = IRQ_HANDLED;
		break;
	default:
		/* can't happen */
		ep0_stall_set(mtu->ep0, true, 0);
		WARN_ON(1);
		break;
	}

	return ret;
}


static int mtu3_ep0_enable(struct usb_ep *ep,
	const struct usb_endpoint_descriptor *desc)
{
	/* always enabled */
	return -EINVAL;
}

static int mtu3_ep0_disable(struct usb_ep *ep)
{
	/* always enabled */
	return -EINVAL;
}

static int ep0_queue(struct mtu3_ep *mep, struct mtu3_request *mreq)
{
	struct mtu3 *mtu = mep->mtu;

	mreq->mtu = mtu;
	mreq->request.actual = 0;
	mreq->request.status = -EINPROGRESS;

	dev_dbg(mtu->dev, "%s %s (ep0_state: %s), len#%d\n", __func__,
		mep->name, decode_ep0_state(mtu), mreq->request.length);

	switch (mtu->ep0_state) {
	case MU3D_EP0_STATE_SETUP:
	case MU3D_EP0_STATE_RX:	/* control-OUT data */
	case MU3D_EP0_STATE_TX:	/* control-IN data */
		break;
	default:
		dev_err(mtu->dev, "%s, error in ep0 state %s\n", __func__,
			decode_ep0_state(mtu));
		return -EINVAL;
	}

	if (mtu->delayed_status) {

		mtu->delayed_status = false;
		ep0_do_status_stage(mtu);
		/* needn't giveback the request for handling delay STATUS */
		return 0;
	}

	if (!list_empty(&mep->req_list))
		return -EBUSY;

	list_add_tail(&mreq->list, &mep->req_list);

	/* sequence #1, IN ... start writing the data */
	if (mtu->ep0_state == MU3D_EP0_STATE_TX)
		ep0_tx_state(mtu);

	return 0;
}

static int mtu3_ep0_queue(struct usb_ep *ep,
	struct usb_request *req, gfp_t gfp)
{
	struct mtu3_ep *mep;
	struct mtu3_request *mreq;
	struct mtu3 *mtu;
	unsigned long flags;
	int ret = 0;

	if (!ep || !req)
		return -EINVAL;

	mep = to_mtu3_ep(ep);
	mtu = mep->mtu;
	mreq = to_mtu3_request(req);

	spin_lock_irqsave(&mtu->lock, flags);
	ret = ep0_queue(mep, mreq);
	spin_unlock_irqrestore(&mtu->lock, flags);
	return ret;
}

static int mtu3_ep0_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	/* we just won't support this */
	return -EINVAL;
}

static int mtu3_ep0_halt(struct usb_ep *ep, int value)
{
	struct mtu3_ep *mep;
	struct mtu3 *mtu;
	unsigned long flags;
	int ret = 0;

	if (!ep || !value)
		return -EINVAL;

	mep = to_mtu3_ep(ep);
	mtu = mep->mtu;

	dev_dbg(mtu->dev, "%s\n", __func__);

	spin_lock_irqsave(&mtu->lock, flags);

	if (!list_empty(&mep->req_list)) {
		ret = -EBUSY;
		goto cleanup;
	}

	switch (mtu->ep0_state) {
	/*
	 * stalls are usually issued after parsing SETUP packet, either
	 * directly in irq context from setup() or else later.
	 */
	case MU3D_EP0_STATE_TX:
	case MU3D_EP0_STATE_TX_END:
	case MU3D_EP0_STATE_RX:
	case MU3D_EP0_STATE_SETUP:
		ep0_stall_set(mtu->ep0, true, 0);
		break;
	default:
		dev_dbg(mtu->dev, "ep0 can't halt in state %s\n",
			decode_ep0_state(mtu));
		ret = -EINVAL;
	}

cleanup:
	spin_unlock_irqrestore(&mtu->lock, flags);
	return ret;
}

const struct usb_ep_ops mtu3_ep0_ops = {
	.enable = mtu3_ep0_enable,
	.disable = mtu3_ep0_disable,
	.alloc_request = mtu3_alloc_request,
	.free_request = mtu3_free_request,
	.queue = mtu3_ep0_queue,
	.dequeue = mtu3_ep0_dequeue,
	.set_halt = mtu3_ep0_halt,
};
