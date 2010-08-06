/*
 * MUSB OTG peripheral driver ep0 handling
 *
 * Copyright 2005 Mentor Graphics Corporation
 * Copyright (C) 2005-2006 by Texas Instruments
 * Copyright (C) 2006-2007 Nokia Corporation
 * Copyright (C) 2008-2009 MontaVista Software, Inc. <source@mvista.com>
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA
 * 02110-1301 USA
 *
 * THIS SOFTWARE IS PROVIDED "AS IS" AND ANY EXPRESS OR IMPLIED
 * WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.  IN
 * NO EVENT SHALL THE AUTHORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT
 * NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF
 * USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON
 * ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
 * (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF
 * THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 *
 */

#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/timer.h>
#include <linux/spinlock.h>
#include <linux/init.h>
#include <linux/device.h>
#include <linux/interrupt.h>

#include "musb_core.h"

/* ep0 is always musb->endpoints[0].ep_in */
#define	next_ep0_request(musb)	next_in_request(&(musb)->endpoints[0])

/*
 * locking note:  we use only the controller lock, for simpler correctness.
 * It's always held with IRQs blocked.
 *
 * It protects the ep0 request queue as well as ep0_state, not just the
 * controller and indexed registers.  And that lock stays held unless it
 * needs to be dropped to allow reentering this driver ... like upcalls to
 * the gadget driver, or adjusting endpoint halt status.
 */

static char *decode_ep0stage(u8 stage)
{
	switch (stage) {
	case MUSB_EP0_STAGE_IDLE:	return "idle";
	case MUSB_EP0_STAGE_SETUP:	return "setup";
	case MUSB_EP0_STAGE_TX:		return "in";
	case MUSB_EP0_STAGE_RX:		return "out";
	case MUSB_EP0_STAGE_ACKWAIT:	return "wait";
	case MUSB_EP0_STAGE_STATUSIN:	return "in/status";
	case MUSB_EP0_STAGE_STATUSOUT:	return "out/status";
	default:			return "?";
	}
}

/* handle a standard GET_STATUS request
 * Context:  caller holds controller lock
 */
static int service_tx_status_request(
	struct musb *musb,
	const struct usb_ctrlrequest *ctrlrequest)
{
	void __iomem	*mbase = musb->mregs;
	int handled = 1;
	u8 result[2], epnum = 0;
	const u8 recip = ctrlrequest->bRequestType & USB_RECIP_MASK;

	result[1] = 0;

	switch (recip) {
	case USB_RECIP_DEVICE:
		result[0] = musb->is_self_powered << USB_DEVICE_SELF_POWERED;
		result[0] |= musb->may_wakeup << USB_DEVICE_REMOTE_WAKEUP;
#ifdef CONFIG_USB_MUSB_OTG
		if (musb->g.is_otg) {
			result[0] |= musb->g.b_hnp_enable
				<< USB_DEVICE_B_HNP_ENABLE;
			result[0] |= musb->g.a_alt_hnp_support
				<< USB_DEVICE_A_ALT_HNP_SUPPORT;
			result[0] |= musb->g.a_hnp_support
				<< USB_DEVICE_A_HNP_SUPPORT;
		}
#endif
		break;

	case USB_RECIP_INTERFACE:
		result[0] = 0;
		break;

	case USB_RECIP_ENDPOINT: {
		int		is_in;
		struct musb_ep	*ep;
		u16		tmp;
		void __iomem	*regs;

		epnum = (u8) ctrlrequest->wIndex;
		if (!epnum) {
			result[0] = 0;
			break;
		}

		is_in = epnum & USB_DIR_IN;
		if (is_in) {
			epnum &= 0x0f;
			ep = &musb->endpoints[epnum].ep_in;
		} else {
			ep = &musb->endpoints[epnum].ep_out;
		}
		regs = musb->endpoints[epnum].regs;

		if (epnum >= MUSB_C_NUM_EPS || !ep->desc) {
			handled = -EINVAL;
			break;
		}

		musb_ep_select(mbase, epnum);
		if (is_in)
			tmp = musb_readw(regs, MUSB_TXCSR)
						& MUSB_TXCSR_P_SENDSTALL;
		else
			tmp = musb_readw(regs, MUSB_RXCSR)
						& MUSB_RXCSR_P_SENDSTALL;
		musb_ep_select(mbase, 0);

		result[0] = tmp ? 1 : 0;
		} break;

	default:
		/* class, vendor, etc ... delegate */
		handled = 0;
		break;
	}

	/* fill up the fifo; caller updates csr0 */
	if (handled > 0) {
		u16	len = le16_to_cpu(ctrlrequest->wLength);

		if (len > 2)
			len = 2;
		musb_write_fifo(&musb->endpoints[0], len, result);
	}

	return handled;
}

/*
 * handle a control-IN request, the end0 buffer contains the current request
 * that is supposed to be a standard control request. Assumes the fifo to
 * be at least 2 bytes long.
 *
 * @return 0 if the request was NOT HANDLED,
 * < 0 when error
 * > 0 when the request is processed
 *
 * Context:  caller holds controller lock
 */
static int
service_in_request(struct musb *musb, const struct usb_ctrlrequest *ctrlrequest)
{
	int handled = 0;	/* not handled */

	if ((ctrlrequest->bRequestType & USB_TYPE_MASK)
			== USB_TYPE_STANDARD) {
		switch (ctrlrequest->bRequest) {
		case USB_REQ_GET_STATUS:
			handled = service_tx_status_request(musb,
					ctrlrequest);
			break;

		/* case USB_REQ_SYNC_FRAME: */

		default:
			break;
		}
	}
	return handled;
}

/*
 * Context:  caller holds controller lock
 */
static void musb_g_ep0_giveback(struct musb *musb, struct usb_request *req)
{
	musb_g_giveback(&musb->endpoints[0].ep_in, req, 0);
}

/*
 * Tries to start B-device HNP negotiation if enabled via sysfs
 */
static inline void musb_try_b_hnp_enable(struct musb *musb)
{
	void __iomem	*mbase = musb->mregs;
	u8		devctl;

	DBG(1, "HNP: Setting HR\n");
	devctl = musb_readb(mbase, MUSB_DEVCTL);
	musb_writeb(mbase, MUSB_DEVCTL, devctl | MUSB_DEVCTL_HR);
}

/*
 * Handle all control requests with no DATA stage, including standard
 * requests such as:
 * USB_REQ_SET_CONFIGURATION, USB_REQ_SET_INTERFACE, unrecognized
 *	always delegated to the gadget driver
 * USB_REQ_SET_ADDRESS, USB_REQ_CLEAR_FEATURE, USB_REQ_SET_FEATURE
 *	always handled here, except for class/vendor/... features
 *
 * Context:  caller holds controller lock
 */
static int
service_zero_data_request(struct musb *musb,
		struct usb_ctrlrequest *ctrlrequest)
__releases(musb->lock)
__acquires(musb->lock)
{
	int handled = -EINVAL;
	void __iomem *mbase = musb->mregs;
	const u8 recip = ctrlrequest->bRequestType & USB_RECIP_MASK;

	/* the gadget driver handles everything except what we MUST handle */
	if ((ctrlrequest->bRequestType & USB_TYPE_MASK)
			== USB_TYPE_STANDARD) {
		switch (ctrlrequest->bRequest) {
		case USB_REQ_SET_ADDRESS:
			/* change it after the status stage */
			musb->set_address = true;
			musb->address = (u8) (ctrlrequest->wValue & 0x7f);
			handled = 1;
			break;

		case USB_REQ_CLEAR_FEATURE:
			switch (recip) {
			case USB_RECIP_DEVICE:
				if (ctrlrequest->wValue
						!= USB_DEVICE_REMOTE_WAKEUP)
					break;
				musb->may_wakeup = 0;
				handled = 1;
				break;
			case USB_RECIP_INTERFACE:
				break;
			case USB_RECIP_ENDPOINT:{
				const u8		epnum =
					ctrlrequest->wIndex & 0x0f;
				struct musb_ep		*musb_ep;
				struct musb_hw_ep	*ep;
				void __iomem		*regs;
				int			is_in;
				u16			csr;

				if (epnum == 0 || epnum >= MUSB_C_NUM_EPS ||
				    ctrlrequest->wValue != USB_ENDPOINT_HALT)
					break;

				ep = musb->endpoints + epnum;
				regs = ep->regs;
				is_in = ctrlrequest->wIndex & USB_DIR_IN;
				if (is_in)
					musb_ep = &ep->ep_in;
				else
					musb_ep = &ep->ep_out;
				if (!musb_ep->desc)
					break;

				handled = 1;
				/* Ignore request if endpoint is wedged */
				if (musb_ep->wedged)
					break;

				musb_ep_select(mbase, epnum);
				if (is_in) {
					csr  = musb_readw(regs, MUSB_TXCSR);
					csr |= MUSB_TXCSR_CLRDATATOG |
					       MUSB_TXCSR_P_WZC_BITS;
					csr &= ~(MUSB_TXCSR_P_SENDSTALL |
						 MUSB_TXCSR_P_SENTSTALL |
						 MUSB_TXCSR_TXPKTRDY);
					musb_writew(regs, MUSB_TXCSR, csr);
				} else {
					csr  = musb_readw(regs, MUSB_RXCSR);
					csr |= MUSB_RXCSR_CLRDATATOG |
					       MUSB_RXCSR_P_WZC_BITS;
					csr &= ~(MUSB_RXCSR_P_SENDSTALL |
						 MUSB_RXCSR_P_SENTSTALL);
					musb_writew(regs, MUSB_RXCSR, csr);
				}

				/* select ep0 again */
				musb_ep_select(mbase, 0);
				} break;
			default:
				/* class, vendor, etc ... delegate */
				handled = 0;
				break;
			}
			break;

		case USB_REQ_SET_FEATURE:
			switch (recip) {
			case USB_RECIP_DEVICE:
				handled = 1;
				switch (ctrlrequest->wValue) {
				case USB_DEVICE_REMOTE_WAKEUP:
					musb->may_wakeup = 1;
					break;
				case USB_DEVICE_TEST_MODE:
					if (musb->g.speed != USB_SPEED_HIGH)
						goto stall;
					if (ctrlrequest->wIndex & 0xff)
						goto stall;

					switch (ctrlrequest->wIndex >> 8) {
					case 1:
						pr_debug("TEST_J\n");
						/* TEST_J */
						musb->test_mode_nr =
							MUSB_TEST_J;
						break;
					case 2:
						/* TEST_K */
						pr_debug("TEST_K\n");
						musb->test_mode_nr =
							MUSB_TEST_K;
						break;
					case 3:
						/* TEST_SE0_NAK */
						pr_debug("TEST_SE0_NAK\n");
						musb->test_mode_nr =
							MUSB_TEST_SE0_NAK;
						break;
					case 4:
						/* TEST_PACKET */
						pr_debug("TEST_PACKET\n");
						musb->test_mode_nr =
							MUSB_TEST_PACKET;
						break;

					case 0xc0:
						/* TEST_FORCE_HS */
						pr_debug("TEST_FORCE_HS\n");
						musb->test_mode_nr =
							MUSB_TEST_FORCE_HS;
						break;
					case 0xc1:
						/* TEST_FORCE_FS */
						pr_debug("TEST_FORCE_FS\n");
						musb->test_mode_nr =
							MUSB_TEST_FORCE_FS;
						break;
					case 0xc2:
						/* TEST_FIFO_ACCESS */
						pr_debug("TEST_FIFO_ACCESS\n");
						musb->test_mode_nr =
							MUSB_TEST_FIFO_ACCESS;
						break;
					case 0xc3:
						/* TEST_FORCE_HOST */
						pr_debug("TEST_FORCE_HOST\n");
						musb->test_mode_nr =
							MUSB_TEST_FORCE_HOST;
						break;
					default:
						goto stall;
					}

					/* enter test mode after irq */
					if (handled > 0)
						musb->test_mode = true;
					break;
#ifdef CONFIG_USB_MUSB_OTG
				case USB_DEVICE_B_HNP_ENABLE:
					if (!musb->g.is_otg)
						goto stall;
					musb->g.b_hnp_enable = 1;
					musb_try_b_hnp_enable(musb);
					break;
				case USB_DEVICE_A_HNP_SUPPORT:
					if (!musb->g.is_otg)
						goto stall;
					musb->g.a_hnp_support = 1;
					break;
				case USB_DEVICE_A_ALT_HNP_SUPPORT:
					if (!musb->g.is_otg)
						goto stall;
					musb->g.a_alt_hnp_support = 1;
					break;
#endif
stall:
				default:
					handled = -EINVAL;
					break;
				}
				break;

			case USB_RECIP_INTERFACE:
				break;

			case USB_RECIP_ENDPOINT:{
				const u8		epnum =
					ctrlrequest->wIndex & 0x0f;
				struct musb_ep		*musb_ep;
				struct musb_hw_ep	*ep;
				void __iomem		*regs;
				int			is_in;
				u16			csr;

				if (epnum == 0 || epnum >= MUSB_C_NUM_EPS ||
				    ctrlrequest->wValue	!= USB_ENDPOINT_HALT)
					break;

				ep = musb->endpoints + epnum;
				regs = ep->regs;
				is_in = ctrlrequest->wIndex & USB_DIR_IN;
				if (is_in)
					musb_ep = &ep->ep_in;
				else
					musb_ep = &ep->ep_out;
				if (!musb_ep->desc)
					break;

				musb_ep_select(mbase, epnum);
				if (is_in) {
					csr = musb_readw(regs, MUSB_TXCSR);
					if (csr & MUSB_TXCSR_FIFONOTEMPTY)
						csr |= MUSB_TXCSR_FLUSHFIFO;
					csr |= MUSB_TXCSR_P_SENDSTALL
						| MUSB_TXCSR_CLRDATATOG
						| MUSB_TXCSR_P_WZC_BITS;
					musb_writew(regs, MUSB_TXCSR, csr);
				} else {
					csr = musb_readw(regs, MUSB_RXCSR);
					csr |= MUSB_RXCSR_P_SENDSTALL
						| MUSB_RXCSR_FLUSHFIFO
						| MUSB_RXCSR_CLRDATATOG
						| MUSB_RXCSR_P_WZC_BITS;
					musb_writew(regs, MUSB_RXCSR, csr);
				}

				/* select ep0 again */
				musb_ep_select(mbase, 0);
				handled = 1;
				} break;

			default:
				/* class, vendor, etc ... delegate */
				handled = 0;
				break;
			}
			break;
		default:
			/* delegate SET_CONFIGURATION, etc */
			handled = 0;
		}
	} else
		handled = 0;
	return handled;
}

/* we have an ep0out data packet
 * Context:  caller holds controller lock
 */
static void ep0_rxstate(struct musb *musb)
{
	void __iomem		*regs = musb->control_ep->regs;
	struct usb_request	*req;
	u16			count, csr;

	req = next_ep0_request(musb);

	/* read packet and ack; or stall because of gadget driver bug:
	 * should have provided the rx buffer before setup() returned.
	 */
	if (req) {
		void		*buf = req->buf + req->actual;
		unsigned	len = req->length - req->actual;

		/* read the buffer */
		count = musb_readb(regs, MUSB_COUNT0);
		if (count > len) {
			req->status = -EOVERFLOW;
			count = len;
		}
		musb_read_fifo(&musb->endpoints[0], count, buf);
		req->actual += count;
		csr = MUSB_CSR0_P_SVDRXPKTRDY;
		if (count < 64 || req->actual == req->length) {
			musb->ep0_state = MUSB_EP0_STAGE_STATUSIN;
			csr |= MUSB_CSR0_P_DATAEND;
		} else
			req = NULL;
	} else
		csr = MUSB_CSR0_P_SVDRXPKTRDY | MUSB_CSR0_P_SENDSTALL;


	/* Completion handler may choose to stall, e.g. because the
	 * message just received holds invalid data.
	 */
	if (req) {
		musb->ackpend = csr;
		musb_g_ep0_giveback(musb, req);
		if (!musb->ackpend)
			return;
		musb->ackpend = 0;
	}
	musb_ep_select(musb->mregs, 0);
	musb_writew(regs, MUSB_CSR0, csr);
}

/*
 * transmitting to the host (IN), this code might be called from IRQ
 * and from kernel thread.
 *
 * Context:  caller holds controller lock
 */
static void ep0_txstate(struct musb *musb)
{
	void __iomem		*regs = musb->control_ep->regs;
	struct usb_request	*request = next_ep0_request(musb);
	u16			csr = MUSB_CSR0_TXPKTRDY;
	u8			*fifo_src;
	u8			fifo_count;

	if (!request) {
		/* WARN_ON(1); */
		DBG(2, "odd; csr0 %04x\n", musb_readw(regs, MUSB_CSR0));
		return;
	}

	/* load the data */
	fifo_src = (u8 *) request->buf + request->actual;
	fifo_count = min((unsigned) MUSB_EP0_FIFOSIZE,
		request->length - request->actual);
	musb_write_fifo(&musb->endpoints[0], fifo_count, fifo_src);
	request->actual += fifo_count;

	/* update the flags */
	if (fifo_count < MUSB_MAX_END0_PACKET
			|| (request->actual == request->length
				&& !request->zero)) {
		musb->ep0_state = MUSB_EP0_STAGE_STATUSOUT;
		csr |= MUSB_CSR0_P_DATAEND;
	} else
		request = NULL;

	/* report completions as soon as the fifo's loaded; there's no
	 * win in waiting till this last packet gets acked.  (other than
	 * very precise fault reporting, needed by USB TMC; possible with
	 * this hardware, but not usable from portable gadget drivers.)
	 */
	if (request) {
		musb->ackpend = csr;
		musb_g_ep0_giveback(musb, request);
		if (!musb->ackpend)
			return;
		musb->ackpend = 0;
	}

	/* send it out, triggering a "txpktrdy cleared" irq */
	musb_ep_select(musb->mregs, 0);
	musb_writew(regs, MUSB_CSR0, csr);
}

/*
 * Read a SETUP packet (struct usb_ctrlrequest) from the hardware.
 * Fields are left in USB byte-order.
 *
 * Context:  caller holds controller lock.
 */
static void
musb_read_setup(struct musb *musb, struct usb_ctrlrequest *req)
{
	struct usb_request	*r;
	void __iomem		*regs = musb->control_ep->regs;

	musb_read_fifo(&musb->endpoints[0], sizeof *req, (u8 *)req);

	/* NOTE:  earlier 2.6 versions changed setup packets to host
	 * order, but now USB packets always stay in USB byte order.
	 */
	DBG(3, "SETUP req%02x.%02x v%04x i%04x l%d\n",
		req->bRequestType,
		req->bRequest,
		le16_to_cpu(req->wValue),
		le16_to_cpu(req->wIndex),
		le16_to_cpu(req->wLength));

	/* clean up any leftover transfers */
	r = next_ep0_request(musb);
	if (r)
		musb_g_ep0_giveback(musb, r);

	/* For zero-data requests we want to delay the STATUS stage to
	 * avoid SETUPEND errors.  If we read data (OUT), delay accepting
	 * packets until there's a buffer to store them in.
	 *
	 * If we write data, the controller acts happier if we enable
	 * the TX FIFO right away, and give the controller a moment
	 * to switch modes...
	 */
	musb->set_address = false;
	musb->ackpend = MUSB_CSR0_P_SVDRXPKTRDY;
	if (req->wLength == 0) {
		if (req->bRequestType & USB_DIR_IN)
			musb->ackpend |= MUSB_CSR0_TXPKTRDY;
		musb->ep0_state = MUSB_EP0_STAGE_ACKWAIT;
	} else if (req->bRequestType & USB_DIR_IN) {
		musb->ep0_state = MUSB_EP0_STAGE_TX;
		musb_writew(regs, MUSB_CSR0, MUSB_CSR0_P_SVDRXPKTRDY);
		while ((musb_readw(regs, MUSB_CSR0)
				& MUSB_CSR0_RXPKTRDY) != 0)
			cpu_relax();
		musb->ackpend = 0;
	} else
		musb->ep0_state = MUSB_EP0_STAGE_RX;
}

static int
forward_to_driver(struct musb *musb, const struct usb_ctrlrequest *ctrlrequest)
__releases(musb->lock)
__acquires(musb->lock)
{
	int retval;
	if (!musb->gadget_driver)
		return -EOPNOTSUPP;
	spin_unlock(&musb->lock);
	retval = musb->gadget_driver->setup(&musb->g, ctrlrequest);
	spin_lock(&musb->lock);
	return retval;
}

/*
 * Handle peripheral ep0 interrupt
 *
 * Context: irq handler; we won't re-enter the driver that way.
 */
irqreturn_t musb_g_ep0_irq(struct musb *musb)
{
	u16		csr;
	u16		len;
	void __iomem	*mbase = musb->mregs;
	void __iomem	*regs = musb->endpoints[0].regs;
	irqreturn_t	retval = IRQ_NONE;

	musb_ep_select(mbase, 0);	/* select ep0 */
	csr = musb_readw(regs, MUSB_CSR0);
	len = musb_readb(regs, MUSB_COUNT0);

	DBG(4, "csr %04x, count %d, myaddr %d, ep0stage %s\n",
			csr, len,
			musb_readb(mbase, MUSB_FADDR),
			decode_ep0stage(musb->ep0_state));

	/* I sent a stall.. need to acknowledge it now.. */
	if (csr & MUSB_CSR0_P_SENTSTALL) {
		musb_writew(regs, MUSB_CSR0,
				csr & ~MUSB_CSR0_P_SENTSTALL);
		retval = IRQ_HANDLED;
		musb->ep0_state = MUSB_EP0_STAGE_IDLE;
		csr = musb_readw(regs, MUSB_CSR0);
	}

	/* request ended "early" */
	if (csr & MUSB_CSR0_P_SETUPEND) {
		musb_writew(regs, MUSB_CSR0, MUSB_CSR0_P_SVDSETUPEND);
		retval = IRQ_HANDLED;
		/* Transition into the early status phase */
		switch (musb->ep0_state) {
		case MUSB_EP0_STAGE_TX:
			musb->ep0_state = MUSB_EP0_STAGE_STATUSOUT;
			break;
		case MUSB_EP0_STAGE_RX:
			musb->ep0_state = MUSB_EP0_STAGE_STATUSIN;
			break;
		default:
			ERR("SetupEnd came in a wrong ep0stage %s\n",
			    decode_ep0stage(musb->ep0_state));
		}
		csr = musb_readw(regs, MUSB_CSR0);
		/* NOTE:  request may need completion */
	}

	/* docs from Mentor only describe tx, rx, and idle/setup states.
	 * we need to handle nuances around status stages, and also the
	 * case where status and setup stages come back-to-back ...
	 */
	switch (musb->ep0_state) {

	case MUSB_EP0_STAGE_TX:
		/* irq on clearing txpktrdy */
		if ((csr & MUSB_CSR0_TXPKTRDY) == 0) {
			ep0_txstate(musb);
			retval = IRQ_HANDLED;
		}
		break;

	case MUSB_EP0_STAGE_RX:
		/* irq on set rxpktrdy */
		if (csr & MUSB_CSR0_RXPKTRDY) {
			ep0_rxstate(musb);
			retval = IRQ_HANDLED;
		}
		break;

	case MUSB_EP0_STAGE_STATUSIN:
		/* end of sequence #2 (OUT/RX state) or #3 (no data) */

		/* update address (if needed) only @ the end of the
		 * status phase per usb spec, which also guarantees
		 * we get 10 msec to receive this irq... until this
		 * is done we won't see the next packet.
		 */
		if (musb->set_address) {
			musb->set_address = false;
			musb_writeb(mbase, MUSB_FADDR, musb->address);
		}

		/* enter test mode if needed (exit by reset) */
		else if (musb->test_mode) {
			DBG(1, "entering TESTMODE\n");

			if (MUSB_TEST_PACKET == musb->test_mode_nr)
				musb_load_testpacket(musb);

			musb_writeb(mbase, MUSB_TESTMODE,
					musb->test_mode_nr);
		}
		/* FALLTHROUGH */

	case MUSB_EP0_STAGE_STATUSOUT:
		/* end of sequence #1: write to host (TX state) */
		{
			struct usb_request	*req;

			req = next_ep0_request(musb);
			if (req)
				musb_g_ep0_giveback(musb, req);
		}

		/*
		 * In case when several interrupts can get coalesced,
		 * check to see if we've already received a SETUP packet...
		 */
		if (csr & MUSB_CSR0_RXPKTRDY)
			goto setup;

		retval = IRQ_HANDLED;
		musb->ep0_state = MUSB_EP0_STAGE_IDLE;
		break;

	case MUSB_EP0_STAGE_IDLE:
		/*
		 * This state is typically (but not always) indiscernible
		 * from the status states since the corresponding interrupts
		 * tend to happen within too little period of time (with only
		 * a zero-length packet in between) and so get coalesced...
		 */
		retval = IRQ_HANDLED;
		musb->ep0_state = MUSB_EP0_STAGE_SETUP;
		/* FALLTHROUGH */

	case MUSB_EP0_STAGE_SETUP:
setup:
		if (csr & MUSB_CSR0_RXPKTRDY) {
			struct usb_ctrlrequest	setup;
			int			handled = 0;

			if (len != 8) {
				ERR("SETUP packet len %d != 8 ?\n", len);
				break;
			}
			musb_read_setup(musb, &setup);
			retval = IRQ_HANDLED;

			/* sometimes the RESET won't be reported */
			if (unlikely(musb->g.speed == USB_SPEED_UNKNOWN)) {
				u8	power;

				printk(KERN_NOTICE "%s: peripheral reset "
						"irq lost!\n",
						musb_driver_name);
				power = musb_readb(mbase, MUSB_POWER);
				musb->g.speed = (power & MUSB_POWER_HSMODE)
					? USB_SPEED_HIGH : USB_SPEED_FULL;

			}

			switch (musb->ep0_state) {

			/* sequence #3 (no data stage), includes requests
			 * we can't forward (notably SET_ADDRESS and the
			 * device/endpoint feature set/clear operations)
			 * plus SET_CONFIGURATION and others we must
			 */
			case MUSB_EP0_STAGE_ACKWAIT:
				handled = service_zero_data_request(
						musb, &setup);

				/*
				 * We're expecting no data in any case, so
				 * always set the DATAEND bit -- doing this
				 * here helps avoid SetupEnd interrupt coming
				 * in the idle stage when we're stalling...
				 */
				musb->ackpend |= MUSB_CSR0_P_DATAEND;

				/* status stage might be immediate */
				if (handled > 0)
					musb->ep0_state =
						MUSB_EP0_STAGE_STATUSIN;
				break;

			/* sequence #1 (IN to host), includes GET_STATUS
			 * requests that we can't forward, GET_DESCRIPTOR
			 * and others that we must
			 */
			case MUSB_EP0_STAGE_TX:
				handled = service_in_request(musb, &setup);
				if (handled > 0) {
					musb->ackpend = MUSB_CSR0_TXPKTRDY
						| MUSB_CSR0_P_DATAEND;
					musb->ep0_state =
						MUSB_EP0_STAGE_STATUSOUT;
				}
				break;

			/* sequence #2 (OUT from host), always forward */
			default:		/* MUSB_EP0_STAGE_RX */
				break;
			}

			DBG(3, "handled %d, csr %04x, ep0stage %s\n",
				handled, csr,
				decode_ep0stage(musb->ep0_state));

			/* unless we need to delegate this to the gadget
			 * driver, we know how to wrap this up:  csr0 has
			 * not yet been written.
			 */
			if (handled < 0)
				goto stall;
			else if (handled > 0)
				goto finish;

			handled = forward_to_driver(musb, &setup);
			if (handled < 0) {
				musb_ep_select(mbase, 0);
stall:
				DBG(3, "stall (%d)\n", handled);
				musb->ackpend |= MUSB_CSR0_P_SENDSTALL;
				musb->ep0_state = MUSB_EP0_STAGE_IDLE;
finish:
				musb_writew(regs, MUSB_CSR0,
						musb->ackpend);
				musb->ackpend = 0;
			}
		}
		break;

	case MUSB_EP0_STAGE_ACKWAIT:
		/* This should not happen. But happens with tusb6010 with
		 * g_file_storage and high speed. Do nothing.
		 */
		retval = IRQ_HANDLED;
		break;

	default:
		/* "can't happen" */
		WARN_ON(1);
		musb_writew(regs, MUSB_CSR0, MUSB_CSR0_P_SENDSTALL);
		musb->ep0_state = MUSB_EP0_STAGE_IDLE;
		break;
	}

	return retval;
}


static int
musb_g_ep0_enable(struct usb_ep *ep, const struct usb_endpoint_descriptor *desc)
{
	/* always enabled */
	return -EINVAL;
}

static int musb_g_ep0_disable(struct usb_ep *e)
{
	/* always enabled */
	return -EINVAL;
}

static int
musb_g_ep0_queue(struct usb_ep *e, struct usb_request *r, gfp_t gfp_flags)
{
	struct musb_ep		*ep;
	struct musb_request	*req;
	struct musb		*musb;
	int			status;
	unsigned long		lockflags;
	void __iomem		*regs;

	if (!e || !r)
		return -EINVAL;

	ep = to_musb_ep(e);
	musb = ep->musb;
	regs = musb->control_ep->regs;

	req = to_musb_request(r);
	req->musb = musb;
	req->request.actual = 0;
	req->request.status = -EINPROGRESS;
	req->tx = ep->is_in;

	spin_lock_irqsave(&musb->lock, lockflags);

	if (!list_empty(&ep->req_list)) {
		status = -EBUSY;
		goto cleanup;
	}

	switch (musb->ep0_state) {
	case MUSB_EP0_STAGE_RX:		/* control-OUT data */
	case MUSB_EP0_STAGE_TX:		/* control-IN data */
	case MUSB_EP0_STAGE_ACKWAIT:	/* zero-length data */
		status = 0;
		break;
	default:
		DBG(1, "ep0 request queued in state %d\n",
				musb->ep0_state);
		status = -EINVAL;
		goto cleanup;
	}

	/* add request to the list */
	list_add_tail(&(req->request.list), &(ep->req_list));

	DBG(3, "queue to %s (%s), length=%d\n",
			ep->name, ep->is_in ? "IN/TX" : "OUT/RX",
			req->request.length);

	musb_ep_select(musb->mregs, 0);

	/* sequence #1, IN ... start writing the data */
	if (musb->ep0_state == MUSB_EP0_STAGE_TX)
		ep0_txstate(musb);

	/* sequence #3, no-data ... issue IN status */
	else if (musb->ep0_state == MUSB_EP0_STAGE_ACKWAIT) {
		if (req->request.length)
			status = -EINVAL;
		else {
			musb->ep0_state = MUSB_EP0_STAGE_STATUSIN;
			musb_writew(regs, MUSB_CSR0,
					musb->ackpend | MUSB_CSR0_P_DATAEND);
			musb->ackpend = 0;
			musb_g_ep0_giveback(ep->musb, r);
		}

	/* else for sequence #2 (OUT), caller provides a buffer
	 * before the next packet arrives.  deferred responses
	 * (after SETUP is acked) are racey.
	 */
	} else if (musb->ackpend) {
		musb_writew(regs, MUSB_CSR0, musb->ackpend);
		musb->ackpend = 0;
	}

cleanup:
	spin_unlock_irqrestore(&musb->lock, lockflags);
	return status;
}

static int musb_g_ep0_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	/* we just won't support this */
	return -EINVAL;
}

static int musb_g_ep0_halt(struct usb_ep *e, int value)
{
	struct musb_ep		*ep;
	struct musb		*musb;
	void __iomem		*base, *regs;
	unsigned long		flags;
	int			status;
	u16			csr;

	if (!e || !value)
		return -EINVAL;

	ep = to_musb_ep(e);
	musb = ep->musb;
	base = musb->mregs;
	regs = musb->control_ep->regs;
	status = 0;

	spin_lock_irqsave(&musb->lock, flags);

	if (!list_empty(&ep->req_list)) {
		status = -EBUSY;
		goto cleanup;
	}

	musb_ep_select(base, 0);
	csr = musb->ackpend;

	switch (musb->ep0_state) {

	/* Stalls are usually issued after parsing SETUP packet, either
	 * directly in irq context from setup() or else later.
	 */
	case MUSB_EP0_STAGE_TX:		/* control-IN data */
	case MUSB_EP0_STAGE_ACKWAIT:	/* STALL for zero-length data */
	case MUSB_EP0_STAGE_RX:		/* control-OUT data */
		csr = musb_readw(regs, MUSB_CSR0);
		/* FALLTHROUGH */

	/* It's also OK to issue stalls during callbacks when a non-empty
	 * DATA stage buffer has been read (or even written).
	 */
	case MUSB_EP0_STAGE_STATUSIN:	/* control-OUT status */
	case MUSB_EP0_STAGE_STATUSOUT:	/* control-IN status */

		csr |= MUSB_CSR0_P_SENDSTALL;
		musb_writew(regs, MUSB_CSR0, csr);
		musb->ep0_state = MUSB_EP0_STAGE_IDLE;
		musb->ackpend = 0;
		break;
	default:
		DBG(1, "ep0 can't halt in state %d\n", musb->ep0_state);
		status = -EINVAL;
	}

cleanup:
	spin_unlock_irqrestore(&musb->lock, flags);
	return status;
}

const struct usb_ep_ops musb_g_ep0_ops = {
	.enable		= musb_g_ep0_enable,
	.disable	= musb_g_ep0_disable,
	.alloc_request	= musb_alloc_request,
	.free_request	= musb_free_request,
	.queue		= musb_g_ep0_queue,
	.dequeue	= musb_g_ep0_dequeue,
	.set_halt	= musb_g_ep0_halt,
};
