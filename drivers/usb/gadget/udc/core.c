// SPDX-License-Identifier: GPL-2.0
/**
 * udc.c - Core UDC Framework
 *
 * Copyright (C) 2010 Texas Instruments
 * Author: Felipe Balbi <balbi@ti.com>
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/device.h>
#include <linux/list.h>
#include <linux/err.h>
#include <linux/dma-mapping.h>
#include <linux/sched/task_stack.h>
#include <linux/workqueue.h>

#include <linux/usb/ch9.h>
#include <linux/usb/gadget.h>
#include <linux/usb.h>

#include "trace.h"

/**
 * struct usb_udc - describes one usb device controller
 * @driver - the gadget driver pointer. For use by the class code
 * @dev - the child device to the actual controller
 * @gadget - the gadget. For use by the class code
 * @list - for use by the udc class driver
 * @vbus - for udcs who care about vbus status, this value is real vbus status;
 * for udcs who do not care about vbus status, this value is always true
 *
 * This represents the internal data structure which is used by the UDC-class
 * to hold information about udc driver and gadget together.
 */
struct usb_udc {
	struct usb_gadget_driver	*driver;
	struct usb_gadget		*gadget;
	struct device			dev;
	struct list_head		list;
	bool				vbus;
};

static struct class *udc_class;
static LIST_HEAD(udc_list);
static LIST_HEAD(gadget_driver_pending_list);
static DEFINE_MUTEX(udc_lock);

static int udc_bind_to_driver(struct usb_udc *udc,
		struct usb_gadget_driver *driver);

/* ------------------------------------------------------------------------- */

/**
 * usb_ep_set_maxpacket_limit - set maximum packet size limit for endpoint
 * @ep:the endpoint being configured
 * @maxpacket_limit:value of maximum packet size limit
 *
 * This function should be used only in UDC drivers to initialize endpoint
 * (usually in probe function).
 */
void usb_ep_set_maxpacket_limit(struct usb_ep *ep,
					      unsigned maxpacket_limit)
{
	ep->maxpacket_limit = maxpacket_limit;
	ep->maxpacket = maxpacket_limit;

	trace_usb_ep_set_maxpacket_limit(ep, 0);
}
EXPORT_SYMBOL_GPL(usb_ep_set_maxpacket_limit);

/**
 * usb_ep_enable - configure endpoint, making it usable
 * @ep:the endpoint being configured.  may not be the endpoint named "ep0".
 *	drivers discover endpoints through the ep_list of a usb_gadget.
 *
 * When configurations are set, or when interface settings change, the driver
 * will enable or disable the relevant endpoints.  while it is enabled, an
 * endpoint may be used for i/o until the driver receives a disconnect() from
 * the host or until the endpoint is disabled.
 *
 * the ep0 implementation (which calls this routine) must ensure that the
 * hardware capabilities of each endpoint match the descriptor provided
 * for it.  for example, an endpoint named "ep2in-bulk" would be usable
 * for interrupt transfers as well as bulk, but it likely couldn't be used
 * for iso transfers or for endpoint 14.  some endpoints are fully
 * configurable, with more generic names like "ep-a".  (remember that for
 * USB, "in" means "towards the USB master".)
 *
 * This routine must be called in process context.
 *
 * returns zero, or a negative error code.
 */
int usb_ep_enable(struct usb_ep *ep)
{
	int ret = 0;

	if (ep->enabled)
		goto out;

	/* UDC drivers can't handle endpoints with maxpacket size 0 */
	if (usb_endpoint_maxp(ep->desc) == 0) {
		/*
		 * We should log an error message here, but we can't call
		 * dev_err() because there's no way to find the gadget
		 * given only ep.
		 */
		ret = -EINVAL;
		goto out;
	}

	ret = ep->ops->enable(ep, ep->desc);
	if (ret)
		goto out;

	ep->enabled = true;

out:
	trace_usb_ep_enable(ep, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_ep_enable);

/**
 * usb_ep_disable - endpoint is no longer usable
 * @ep:the endpoint being unconfigured.  may not be the endpoint named "ep0".
 *
 * no other task may be using this endpoint when this is called.
 * any pending and uncompleted requests will complete with status
 * indicating disconnect (-ESHUTDOWN) before this call returns.
 * gadget drivers must call usb_ep_enable() again before queueing
 * requests to the endpoint.
 *
 * This routine must be called in process context.
 *
 * returns zero, or a negative error code.
 */
int usb_ep_disable(struct usb_ep *ep)
{
	int ret = 0;

	if (!ep->enabled)
		goto out;

	ret = ep->ops->disable(ep);
	if (ret)
		goto out;

	ep->enabled = false;

out:
	trace_usb_ep_disable(ep, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_ep_disable);

/**
 * usb_ep_alloc_request - allocate a request object to use with this endpoint
 * @ep:the endpoint to be used with with the request
 * @gfp_flags:GFP_* flags to use
 *
 * Request objects must be allocated with this call, since they normally
 * need controller-specific setup and may even need endpoint-specific
 * resources such as allocation of DMA descriptors.
 * Requests may be submitted with usb_ep_queue(), and receive a single
 * completion callback.  Free requests with usb_ep_free_request(), when
 * they are no longer needed.
 *
 * Returns the request, or null if one could not be allocated.
 */
struct usb_request *usb_ep_alloc_request(struct usb_ep *ep,
						       gfp_t gfp_flags)
{
	struct usb_request *req = NULL;

	req = ep->ops->alloc_request(ep, gfp_flags);

	trace_usb_ep_alloc_request(ep, req, req ? 0 : -ENOMEM);

	return req;
}
EXPORT_SYMBOL_GPL(usb_ep_alloc_request);

/**
 * usb_ep_free_request - frees a request object
 * @ep:the endpoint associated with the request
 * @req:the request being freed
 *
 * Reverses the effect of usb_ep_alloc_request().
 * Caller guarantees the request is not queued, and that it will
 * no longer be requeued (or otherwise used).
 */
void usb_ep_free_request(struct usb_ep *ep,
				       struct usb_request *req)
{
	trace_usb_ep_free_request(ep, req, 0);
	ep->ops->free_request(ep, req);
}
EXPORT_SYMBOL_GPL(usb_ep_free_request);

/**
 * usb_ep_queue - queues (submits) an I/O request to an endpoint.
 * @ep:the endpoint associated with the request
 * @req:the request being submitted
 * @gfp_flags: GFP_* flags to use in case the lower level driver couldn't
 *	pre-allocate all necessary memory with the request.
 *
 * This tells the device controller to perform the specified request through
 * that endpoint (reading or writing a buffer).  When the request completes,
 * including being canceled by usb_ep_dequeue(), the request's completion
 * routine is called to return the request to the driver.  Any endpoint
 * (except control endpoints like ep0) may have more than one transfer
 * request queued; they complete in FIFO order.  Once a gadget driver
 * submits a request, that request may not be examined or modified until it
 * is given back to that driver through the completion callback.
 *
 * Each request is turned into one or more packets.  The controller driver
 * never merges adjacent requests into the same packet.  OUT transfers
 * will sometimes use data that's already buffered in the hardware.
 * Drivers can rely on the fact that the first byte of the request's buffer
 * always corresponds to the first byte of some USB packet, for both
 * IN and OUT transfers.
 *
 * Bulk endpoints can queue any amount of data; the transfer is packetized
 * automatically.  The last packet will be short if the request doesn't fill it
 * out completely.  Zero length packets (ZLPs) should be avoided in portable
 * protocols since not all usb hardware can successfully handle zero length
 * packets.  (ZLPs may be explicitly written, and may be implicitly written if
 * the request 'zero' flag is set.)  Bulk endpoints may also be used
 * for interrupt transfers; but the reverse is not true, and some endpoints
 * won't support every interrupt transfer.  (Such as 768 byte packets.)
 *
 * Interrupt-only endpoints are less functional than bulk endpoints, for
 * example by not supporting queueing or not handling buffers that are
 * larger than the endpoint's maxpacket size.  They may also treat data
 * toggle differently.
 *
 * Control endpoints ... after getting a setup() callback, the driver queues
 * one response (even if it would be zero length).  That enables the
 * status ack, after transferring data as specified in the response.  Setup
 * functions may return negative error codes to generate protocol stalls.
 * (Note that some USB device controllers disallow protocol stall responses
 * in some cases.)  When control responses are deferred (the response is
 * written after the setup callback returns), then usb_ep_set_halt() may be
 * used on ep0 to trigger protocol stalls.  Depending on the controller,
 * it may not be possible to trigger a status-stage protocol stall when the
 * data stage is over, that is, from within the response's completion
 * routine.
 *
 * For periodic endpoints, like interrupt or isochronous ones, the usb host
 * arranges to poll once per interval, and the gadget driver usually will
 * have queued some data to transfer at that time.
 *
 * Note that @req's ->complete() callback must never be called from
 * within usb_ep_queue() as that can create deadlock situations.
 *
 * This routine may be called in interrupt context.
 *
 * Returns zero, or a negative error code.  Endpoints that are not enabled
 * report errors; errors will also be
 * reported when the usb peripheral is disconnected.
 *
 * If and only if @req is successfully queued (the return value is zero),
 * @req->complete() will be called exactly once, when the Gadget core and
 * UDC are finished with the request.  When the completion function is called,
 * control of the request is returned to the device driver which submitted it.
 * The completion handler may then immediately free or reuse @req.
 */
int usb_ep_queue(struct usb_ep *ep,
			       struct usb_request *req, gfp_t gfp_flags)
{
	int ret = 0;

	if (WARN_ON_ONCE(!ep->enabled && ep->address)) {
		ret = -ESHUTDOWN;
		goto out;
	}

	ret = ep->ops->queue(ep, req, gfp_flags);

out:
	trace_usb_ep_queue(ep, req, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_ep_queue);

/**
 * usb_ep_dequeue - dequeues (cancels, unlinks) an I/O request from an endpoint
 * @ep:the endpoint associated with the request
 * @req:the request being canceled
 *
 * If the request is still active on the endpoint, it is dequeued and its
 * completion routine is called (with status -ECONNRESET); else a negative
 * error code is returned. This is guaranteed to happen before the call to
 * usb_ep_dequeue() returns.
 *
 * Note that some hardware can't clear out write fifos (to unlink the request
 * at the head of the queue) except as part of disconnecting from usb. Such
 * restrictions prevent drivers from supporting configuration changes,
 * even to configuration zero (a "chapter 9" requirement).
 *
 * This routine may be called in interrupt context.
 */
int usb_ep_dequeue(struct usb_ep *ep, struct usb_request *req)
{
	int ret;

	ret = ep->ops->dequeue(ep, req);
	trace_usb_ep_dequeue(ep, req, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_ep_dequeue);

/**
 * usb_ep_set_halt - sets the endpoint halt feature.
 * @ep: the non-isochronous endpoint being stalled
 *
 * Use this to stall an endpoint, perhaps as an error report.
 * Except for control endpoints,
 * the endpoint stays halted (will not stream any data) until the host
 * clears this feature; drivers may need to empty the endpoint's request
 * queue first, to make sure no inappropriate transfers happen.
 *
 * Note that while an endpoint CLEAR_FEATURE will be invisible to the
 * gadget driver, a SET_INTERFACE will not be.  To reset endpoints for the
 * current altsetting, see usb_ep_clear_halt().  When switching altsettings,
 * it's simplest to use usb_ep_enable() or usb_ep_disable() for the endpoints.
 *
 * This routine may be called in interrupt context.
 *
 * Returns zero, or a negative error code.  On success, this call sets
 * underlying hardware state that blocks data transfers.
 * Attempts to halt IN endpoints will fail (returning -EAGAIN) if any
 * transfer requests are still queued, or if the controller hardware
 * (usually a FIFO) still holds bytes that the host hasn't collected.
 */
int usb_ep_set_halt(struct usb_ep *ep)
{
	int ret;

	ret = ep->ops->set_halt(ep, 1);
	trace_usb_ep_set_halt(ep, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_ep_set_halt);

/**
 * usb_ep_clear_halt - clears endpoint halt, and resets toggle
 * @ep:the bulk or interrupt endpoint being reset
 *
 * Use this when responding to the standard usb "set interface" request,
 * for endpoints that aren't reconfigured, after clearing any other state
 * in the endpoint's i/o queue.
 *
 * This routine may be called in interrupt context.
 *
 * Returns zero, or a negative error code.  On success, this call clears
 * the underlying hardware state reflecting endpoint halt and data toggle.
 * Note that some hardware can't support this request (like pxa2xx_udc),
 * and accordingly can't correctly implement interface altsettings.
 */
int usb_ep_clear_halt(struct usb_ep *ep)
{
	int ret;

	ret = ep->ops->set_halt(ep, 0);
	trace_usb_ep_clear_halt(ep, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_ep_clear_halt);

/**
 * usb_ep_set_wedge - sets the halt feature and ignores clear requests
 * @ep: the endpoint being wedged
 *
 * Use this to stall an endpoint and ignore CLEAR_FEATURE(HALT_ENDPOINT)
 * requests. If the gadget driver clears the halt status, it will
 * automatically unwedge the endpoint.
 *
 * This routine may be called in interrupt context.
 *
 * Returns zero on success, else negative errno.
 */
int usb_ep_set_wedge(struct usb_ep *ep)
{
	int ret;

	if (ep->ops->set_wedge)
		ret = ep->ops->set_wedge(ep);
	else
		ret = ep->ops->set_halt(ep, 1);

	trace_usb_ep_set_wedge(ep, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_ep_set_wedge);

/**
 * usb_ep_fifo_status - returns number of bytes in fifo, or error
 * @ep: the endpoint whose fifo status is being checked.
 *
 * FIFO endpoints may have "unclaimed data" in them in certain cases,
 * such as after aborted transfers.  Hosts may not have collected all
 * the IN data written by the gadget driver (and reported by a request
 * completion).  The gadget driver may not have collected all the data
 * written OUT to it by the host.  Drivers that need precise handling for
 * fault reporting or recovery may need to use this call.
 *
 * This routine may be called in interrupt context.
 *
 * This returns the number of such bytes in the fifo, or a negative
 * errno if the endpoint doesn't use a FIFO or doesn't support such
 * precise handling.
 */
int usb_ep_fifo_status(struct usb_ep *ep)
{
	int ret;

	if (ep->ops->fifo_status)
		ret = ep->ops->fifo_status(ep);
	else
		ret = -EOPNOTSUPP;

	trace_usb_ep_fifo_status(ep, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_ep_fifo_status);

/**
 * usb_ep_fifo_flush - flushes contents of a fifo
 * @ep: the endpoint whose fifo is being flushed.
 *
 * This call may be used to flush the "unclaimed data" that may exist in
 * an endpoint fifo after abnormal transaction terminations.  The call
 * must never be used except when endpoint is not being used for any
 * protocol translation.
 *
 * This routine may be called in interrupt context.
 */
void usb_ep_fifo_flush(struct usb_ep *ep)
{
	if (ep->ops->fifo_flush)
		ep->ops->fifo_flush(ep);

	trace_usb_ep_fifo_flush(ep, 0);
}
EXPORT_SYMBOL_GPL(usb_ep_fifo_flush);

/* ------------------------------------------------------------------------- */

/**
 * usb_gadget_frame_number - returns the current frame number
 * @gadget: controller that reports the frame number
 *
 * Returns the usb frame number, normally eleven bits from a SOF packet,
 * or negative errno if this device doesn't support this capability.
 */
int usb_gadget_frame_number(struct usb_gadget *gadget)
{
	int ret;

	ret = gadget->ops->get_frame(gadget);

	trace_usb_gadget_frame_number(gadget, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_gadget_frame_number);

/**
 * usb_gadget_wakeup - tries to wake up the host connected to this gadget
 * @gadget: controller used to wake up the host
 *
 * Returns zero on success, else negative error code if the hardware
 * doesn't support such attempts, or its support has not been enabled
 * by the usb host.  Drivers must return device descriptors that report
 * their ability to support this, or hosts won't enable it.
 *
 * This may also try to use SRP to wake the host and start enumeration,
 * even if OTG isn't otherwise in use.  OTG devices may also start
 * remote wakeup even when hosts don't explicitly enable it.
 */
int usb_gadget_wakeup(struct usb_gadget *gadget)
{
	int ret = 0;

	if (!gadget->ops->wakeup) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = gadget->ops->wakeup(gadget);

out:
	trace_usb_gadget_wakeup(gadget, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_gadget_wakeup);

/**
 * usb_gadget_set_selfpowered - sets the device selfpowered feature.
 * @gadget:the device being declared as self-powered
 *
 * this affects the device status reported by the hardware driver
 * to reflect that it now has a local power supply.
 *
 * returns zero on success, else negative errno.
 */
int usb_gadget_set_selfpowered(struct usb_gadget *gadget)
{
	int ret = 0;

	if (!gadget->ops->set_selfpowered) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = gadget->ops->set_selfpowered(gadget, 1);

out:
	trace_usb_gadget_set_selfpowered(gadget, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_gadget_set_selfpowered);

/**
 * usb_gadget_clear_selfpowered - clear the device selfpowered feature.
 * @gadget:the device being declared as bus-powered
 *
 * this affects the device status reported by the hardware driver.
 * some hardware may not support bus-powered operation, in which
 * case this feature's value can never change.
 *
 * returns zero on success, else negative errno.
 */
int usb_gadget_clear_selfpowered(struct usb_gadget *gadget)
{
	int ret = 0;

	if (!gadget->ops->set_selfpowered) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = gadget->ops->set_selfpowered(gadget, 0);

out:
	trace_usb_gadget_clear_selfpowered(gadget, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_gadget_clear_selfpowered);

/**
 * usb_gadget_vbus_connect - Notify controller that VBUS is powered
 * @gadget:The device which now has VBUS power.
 * Context: can sleep
 *
 * This call is used by a driver for an external transceiver (or GPIO)
 * that detects a VBUS power session starting.  Common responses include
 * resuming the controller, activating the D+ (or D-) pullup to let the
 * host detect that a USB device is attached, and starting to draw power
 * (8mA or possibly more, especially after SET_CONFIGURATION).
 *
 * Returns zero on success, else negative errno.
 */
int usb_gadget_vbus_connect(struct usb_gadget *gadget)
{
	int ret = 0;

	if (!gadget->ops->vbus_session) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = gadget->ops->vbus_session(gadget, 1);

out:
	trace_usb_gadget_vbus_connect(gadget, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_gadget_vbus_connect);

/**
 * usb_gadget_vbus_draw - constrain controller's VBUS power usage
 * @gadget:The device whose VBUS usage is being described
 * @mA:How much current to draw, in milliAmperes.  This should be twice
 *	the value listed in the configuration descriptor bMaxPower field.
 *
 * This call is used by gadget drivers during SET_CONFIGURATION calls,
 * reporting how much power the device may consume.  For example, this
 * could affect how quickly batteries are recharged.
 *
 * Returns zero on success, else negative errno.
 */
int usb_gadget_vbus_draw(struct usb_gadget *gadget, unsigned mA)
{
	int ret = 0;

	if (!gadget->ops->vbus_draw) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = gadget->ops->vbus_draw(gadget, mA);
	if (!ret)
		gadget->mA = mA;

out:
	trace_usb_gadget_vbus_draw(gadget, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_gadget_vbus_draw);

/**
 * usb_gadget_vbus_disconnect - notify controller about VBUS session end
 * @gadget:the device whose VBUS supply is being described
 * Context: can sleep
 *
 * This call is used by a driver for an external transceiver (or GPIO)
 * that detects a VBUS power session ending.  Common responses include
 * reversing everything done in usb_gadget_vbus_connect().
 *
 * Returns zero on success, else negative errno.
 */
int usb_gadget_vbus_disconnect(struct usb_gadget *gadget)
{
	int ret = 0;

	if (!gadget->ops->vbus_session) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	ret = gadget->ops->vbus_session(gadget, 0);

out:
	trace_usb_gadget_vbus_disconnect(gadget, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_gadget_vbus_disconnect);

/**
 * usb_gadget_connect - software-controlled connect to USB host
 * @gadget:the peripheral being connected
 *
 * Enables the D+ (or potentially D-) pullup.  The host will start
 * enumerating this gadget when the pullup is active and a VBUS session
 * is active (the link is powered).  This pullup is always enabled unless
 * usb_gadget_disconnect() has been used to disable it.
 *
 * Returns zero on success, else negative errno.
 */
int usb_gadget_connect(struct usb_gadget *gadget)
{
	int ret = 0;

	if (!gadget->ops->pullup) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (gadget->deactivated) {
		/*
		 * If gadget is deactivated we only save new state.
		 * Gadget will be connected automatically after activation.
		 */
		gadget->connected = true;
		goto out;
	}

	ret = gadget->ops->pullup(gadget, 1);
	if (!ret)
		gadget->connected = 1;

out:
	trace_usb_gadget_connect(gadget, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_gadget_connect);

/**
 * usb_gadget_disconnect - software-controlled disconnect from USB host
 * @gadget:the peripheral being disconnected
 *
 * Disables the D+ (or potentially D-) pullup, which the host may see
 * as a disconnect (when a VBUS session is active).  Not all systems
 * support software pullup controls.
 *
 * Returns zero on success, else negative errno.
 */
int usb_gadget_disconnect(struct usb_gadget *gadget)
{
	int ret = 0;

	if (!gadget->ops->pullup) {
		ret = -EOPNOTSUPP;
		goto out;
	}

	if (gadget->deactivated) {
		/*
		 * If gadget is deactivated we only save new state.
		 * Gadget will stay disconnected after activation.
		 */
		gadget->connected = false;
		goto out;
	}

	ret = gadget->ops->pullup(gadget, 0);
	if (!ret)
		gadget->connected = 0;

out:
	trace_usb_gadget_disconnect(gadget, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_gadget_disconnect);

/**
 * usb_gadget_deactivate - deactivate function which is not ready to work
 * @gadget: the peripheral being deactivated
 *
 * This routine may be used during the gadget driver bind() call to prevent
 * the peripheral from ever being visible to the USB host, unless later
 * usb_gadget_activate() is called.  For example, user mode components may
 * need to be activated before the system can talk to hosts.
 *
 * Returns zero on success, else negative errno.
 */
int usb_gadget_deactivate(struct usb_gadget *gadget)
{
	int ret = 0;

	if (gadget->deactivated)
		goto out;

	if (gadget->connected) {
		ret = usb_gadget_disconnect(gadget);
		if (ret)
			goto out;

		/*
		 * If gadget was being connected before deactivation, we want
		 * to reconnect it in usb_gadget_activate().
		 */
		gadget->connected = true;
	}
	gadget->deactivated = true;

out:
	trace_usb_gadget_deactivate(gadget, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_gadget_deactivate);

/**
 * usb_gadget_activate - activate function which is not ready to work
 * @gadget: the peripheral being activated
 *
 * This routine activates gadget which was previously deactivated with
 * usb_gadget_deactivate() call. It calls usb_gadget_connect() if needed.
 *
 * Returns zero on success, else negative errno.
 */
int usb_gadget_activate(struct usb_gadget *gadget)
{
	int ret = 0;

	if (!gadget->deactivated)
		goto out;

	gadget->deactivated = false;

	/*
	 * If gadget has been connected before deactivation, or became connected
	 * while it was being deactivated, we call usb_gadget_connect().
	 */
	if (gadget->connected)
		ret = usb_gadget_connect(gadget);

out:
	trace_usb_gadget_activate(gadget, ret);

	return ret;
}
EXPORT_SYMBOL_GPL(usb_gadget_activate);

/* ------------------------------------------------------------------------- */

#ifdef	CONFIG_HAS_DMA

int usb_gadget_map_request_by_dev(struct device *dev,
		struct usb_request *req, int is_in)
{
	if (req->length == 0)
		return 0;

	if (req->num_sgs) {
		int     mapped;

		mapped = dma_map_sg(dev, req->sg, req->num_sgs,
				is_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		if (mapped == 0) {
			dev_err(dev, "failed to map SGs\n");
			return -EFAULT;
		}

		req->num_mapped_sgs = mapped;
	} else {
		if (is_vmalloc_addr(req->buf)) {
			dev_err(dev, "buffer is not dma capable\n");
			return -EFAULT;
		} else if (object_is_on_stack(req->buf)) {
			dev_err(dev, "buffer is on stack\n");
			return -EFAULT;
		}

		req->dma = dma_map_single(dev, req->buf, req->length,
				is_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

		if (dma_mapping_error(dev, req->dma)) {
			dev_err(dev, "failed to map buffer\n");
			return -EFAULT;
		}

		req->dma_mapped = 1;
	}

	return 0;
}
EXPORT_SYMBOL_GPL(usb_gadget_map_request_by_dev);

int usb_gadget_map_request(struct usb_gadget *gadget,
		struct usb_request *req, int is_in)
{
	return usb_gadget_map_request_by_dev(gadget->dev.parent, req, is_in);
}
EXPORT_SYMBOL_GPL(usb_gadget_map_request);

void usb_gadget_unmap_request_by_dev(struct device *dev,
		struct usb_request *req, int is_in)
{
	if (req->length == 0)
		return;

	if (req->num_mapped_sgs) {
		dma_unmap_sg(dev, req->sg, req->num_sgs,
				is_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE);

		req->num_mapped_sgs = 0;
	} else if (req->dma_mapped) {
		dma_unmap_single(dev, req->dma, req->length,
				is_in ? DMA_TO_DEVICE : DMA_FROM_DEVICE);
		req->dma_mapped = 0;
	}
}
EXPORT_SYMBOL_GPL(usb_gadget_unmap_request_by_dev);

void usb_gadget_unmap_request(struct usb_gadget *gadget,
		struct usb_request *req, int is_in)
{
	usb_gadget_unmap_request_by_dev(gadget->dev.parent, req, is_in);
}
EXPORT_SYMBOL_GPL(usb_gadget_unmap_request);

#endif	/* CONFIG_HAS_DMA */

/* ------------------------------------------------------------------------- */

/**
 * usb_gadget_giveback_request - give the request back to the gadget layer
 * Context: in_interrupt()
 *
 * This is called by device controller drivers in order to return the
 * completed request back to the gadget layer.
 */
void usb_gadget_giveback_request(struct usb_ep *ep,
		struct usb_request *req)
{
	if (likely(req->status == 0))
		usb_led_activity(USB_LED_EVENT_GADGET);

	trace_usb_gadget_giveback_request(ep, req, 0);

	req->complete(ep, req);
}
EXPORT_SYMBOL_GPL(usb_gadget_giveback_request);

/* ------------------------------------------------------------------------- */

/**
 * gadget_find_ep_by_name - returns ep whose name is the same as sting passed
 *	in second parameter or NULL if searched endpoint not found
 * @g: controller to check for quirk
 * @name: name of searched endpoint
 */
struct usb_ep *gadget_find_ep_by_name(struct usb_gadget *g, const char *name)
{
	struct usb_ep *ep;

	gadget_for_each_ep(ep, g) {
		if (!strcmp(ep->name, name))
			return ep;
	}

	return NULL;
}
EXPORT_SYMBOL_GPL(gadget_find_ep_by_name);

/* ------------------------------------------------------------------------- */

int usb_gadget_ep_match_desc(struct usb_gadget *gadget,
		struct usb_ep *ep, struct usb_endpoint_descriptor *desc,
		struct usb_ss_ep_comp_descriptor *ep_comp)
{
	u8		type;
	u16		max;
	int		num_req_streams = 0;

	/* endpoint already claimed? */
	if (ep->claimed)
		return 0;

	type = usb_endpoint_type(desc);
	max = usb_endpoint_maxp(desc);

	if (usb_endpoint_dir_in(desc) && !ep->caps.dir_in)
		return 0;
	if (usb_endpoint_dir_out(desc) && !ep->caps.dir_out)
		return 0;

	if (max > ep->maxpacket_limit)
		return 0;

	/* "high bandwidth" works only at high speed */
	if (!gadget_is_dualspeed(gadget) && usb_endpoint_maxp_mult(desc) > 1)
		return 0;

	switch (type) {
	case USB_ENDPOINT_XFER_CONTROL:
		/* only support ep0 for portable CONTROL traffic */
		return 0;
	case USB_ENDPOINT_XFER_ISOC:
		if (!ep->caps.type_iso)
			return 0;
		/* ISO:  limit 1023 bytes full speed, 1024 high/super speed */
		if (!gadget_is_dualspeed(gadget) && max > 1023)
			return 0;
		break;
	case USB_ENDPOINT_XFER_BULK:
		if (!ep->caps.type_bulk)
			return 0;
		if (ep_comp && gadget_is_superspeed(gadget)) {
			/* Get the number of required streams from the
			 * EP companion descriptor and see if the EP
			 * matches it
			 */
			num_req_streams = ep_comp->bmAttributes & 0x1f;
			if (num_req_streams > ep->max_streams)
				return 0;
		}
		break;
	case USB_ENDPOINT_XFER_INT:
		/* Bulk endpoints handle interrupt transfers,
		 * except the toggle-quirky iso-synch kind
		 */
		if (!ep->caps.type_int && !ep->caps.type_bulk)
			return 0;
		/* INT:  limit 64 bytes full speed, 1024 high/super speed */
		if (!gadget_is_dualspeed(gadget) && max > 64)
			return 0;
		break;
	}

	return 1;
}
EXPORT_SYMBOL_GPL(usb_gadget_ep_match_desc);

/* ------------------------------------------------------------------------- */

static void usb_gadget_state_work(struct work_struct *work)
{
	struct usb_gadget *gadget = work_to_gadget(work);
	struct usb_udc *udc = gadget->udc;

	if (udc)
		sysfs_notify(&udc->dev.kobj, NULL, "state");
}

void usb_gadget_set_state(struct usb_gadget *gadget,
		enum usb_device_state state)
{
	gadget->state = state;
	schedule_work(&gadget->work);
}
EXPORT_SYMBOL_GPL(usb_gadget_set_state);

/* ------------------------------------------------------------------------- */

static void usb_udc_connect_control(struct usb_udc *udc)
{
	if (udc->vbus)
		usb_gadget_connect(udc->gadget);
	else
		usb_gadget_disconnect(udc->gadget);
}

/**
 * usb_udc_vbus_handler - updates the udc core vbus status, and try to
 * connect or disconnect gadget
 * @gadget: The gadget which vbus change occurs
 * @status: The vbus status
 *
 * The udc driver calls it when it wants to connect or disconnect gadget
 * according to vbus status.
 */
void usb_udc_vbus_handler(struct usb_gadget *gadget, bool status)
{
	struct usb_udc *udc = gadget->udc;

	if (udc) {
		udc->vbus = status;
		usb_udc_connect_control(udc);
	}
}
EXPORT_SYMBOL_GPL(usb_udc_vbus_handler);

/**
 * usb_gadget_udc_reset - notifies the udc core that bus reset occurs
 * @gadget: The gadget which bus reset occurs
 * @driver: The gadget driver we want to notify
 *
 * If the udc driver has bus reset handler, it needs to call this when the bus
 * reset occurs, it notifies the gadget driver that the bus reset occurs as
 * well as updates gadget state.
 */
void usb_gadget_udc_reset(struct usb_gadget *gadget,
		struct usb_gadget_driver *driver)
{
	driver->reset(gadget);
	usb_gadget_set_state(gadget, USB_STATE_DEFAULT);
}
EXPORT_SYMBOL_GPL(usb_gadget_udc_reset);

/**
 * usb_gadget_udc_start - tells usb device controller to start up
 * @udc: The UDC to be started
 *
 * This call is issued by the UDC Class driver when it's about
 * to register a gadget driver to the device controller, before
 * calling gadget driver's bind() method.
 *
 * It allows the controller to be powered off until strictly
 * necessary to have it powered on.
 *
 * Returns zero on success, else negative errno.
 */
static inline int usb_gadget_udc_start(struct usb_udc *udc)
{
	return udc->gadget->ops->udc_start(udc->gadget, udc->driver);
}

/**
 * usb_gadget_udc_stop - tells usb device controller we don't need it anymore
 * @gadget: The device we want to stop activity
 * @driver: The driver to unbind from @gadget
 *
 * This call is issued by the UDC Class driver after calling
 * gadget driver's unbind() method.
 *
 * The details are implementation specific, but it can go as
 * far as powering off UDC completely and disable its data
 * line pullups.
 */
static inline void usb_gadget_udc_stop(struct usb_udc *udc)
{
	udc->gadget->ops->udc_stop(udc->gadget);
}

/**
 * usb_gadget_udc_set_speed - tells usb device controller speed supported by
 *    current driver
 * @udc: The device we want to set maximum speed
 * @speed: The maximum speed to allowed to run
 *
 * This call is issued by the UDC Class driver before calling
 * usb_gadget_udc_start() in order to make sure that we don't try to
 * connect on speeds the gadget driver doesn't support.
 */
static inline void usb_gadget_udc_set_speed(struct usb_udc *udc,
					    enum usb_device_speed speed)
{
	if (udc->gadget->ops->udc_set_speed) {
		enum usb_device_speed s;

		s = min(speed, udc->gadget->max_speed);
		udc->gadget->ops->udc_set_speed(udc->gadget, s);
	}
}

/**
 * usb_udc_release - release the usb_udc struct
 * @dev: the dev member within usb_udc
 *
 * This is called by driver's core in order to free memory once the last
 * reference is released.
 */
static void usb_udc_release(struct device *dev)
{
	struct usb_udc *udc;

	udc = container_of(dev, struct usb_udc, dev);
	dev_dbg(dev, "releasing '%s'\n", dev_name(dev));
	kfree(udc);
}

static const struct attribute_group *usb_udc_attr_groups[];

static void usb_udc_nop_release(struct device *dev)
{
	dev_vdbg(dev, "%s\n", __func__);
}

/* should be called with udc_lock held */
static int check_pending_gadget_drivers(struct usb_udc *udc)
{
	struct usb_gadget_driver *driver;
	int ret = 0;

	list_for_each_entry(driver, &gadget_driver_pending_list, pending)
		if (!driver->udc_name || strcmp(driver->udc_name,
						dev_name(&udc->dev)) == 0) {
			ret = udc_bind_to_driver(udc, driver);
			if (ret != -EPROBE_DEFER)
				list_del(&driver->pending);
			break;
		}

	return ret;
}

/**
 * usb_add_gadget_udc_release - adds a new gadget to the udc class driver list
 * @parent: the parent device to this udc. Usually the controller driver's
 * device.
 * @gadget: the gadget to be added to the list.
 * @release: a gadget release function.
 *
 * Returns zero on success, negative errno otherwise.
 * Calls the gadget release function in the latter case.
 */
int usb_add_gadget_udc_release(struct device *parent, struct usb_gadget *gadget,
		void (*release)(struct device *dev))
{
	struct usb_udc		*udc;
	int			ret = -ENOMEM;

	dev_set_name(&gadget->dev, "gadget");
	INIT_WORK(&gadget->work, usb_gadget_state_work);
	gadget->dev.parent = parent;

	if (release)
		gadget->dev.release = release;
	else
		gadget->dev.release = usb_udc_nop_release;

	device_initialize(&gadget->dev);

	udc = kzalloc(sizeof(*udc), GFP_KERNEL);
	if (!udc)
		goto err_put_gadget;

	device_initialize(&udc->dev);
	udc->dev.release = usb_udc_release;
	udc->dev.class = udc_class;
	udc->dev.groups = usb_udc_attr_groups;
	udc->dev.parent = parent;
	ret = dev_set_name(&udc->dev, "%s", kobject_name(&parent->kobj));
	if (ret)
		goto err_put_udc;

	ret = device_add(&gadget->dev);
	if (ret)
		goto err_put_udc;

	udc->gadget = gadget;
	gadget->udc = udc;

	mutex_lock(&udc_lock);
	list_add_tail(&udc->list, &udc_list);

	ret = device_add(&udc->dev);
	if (ret)
		goto err_unlist_udc;

	usb_gadget_set_state(gadget, USB_STATE_NOTATTACHED);
	udc->vbus = true;

	/* pick up one of pending gadget drivers */
	ret = check_pending_gadget_drivers(udc);
	if (ret)
		goto err_del_udc;

	mutex_unlock(&udc_lock);

	return 0;

 err_del_udc:
	device_del(&udc->dev);

 err_unlist_udc:
	list_del(&udc->list);
	mutex_unlock(&udc_lock);

	device_del(&gadget->dev);

 err_put_udc:
	put_device(&udc->dev);

 err_put_gadget:
	put_device(&gadget->dev);
	return ret;
}
EXPORT_SYMBOL_GPL(usb_add_gadget_udc_release);

/**
 * usb_get_gadget_udc_name - get the name of the first UDC controller
 * This functions returns the name of the first UDC controller in the system.
 * Please note that this interface is usefull only for legacy drivers which
 * assume that there is only one UDC controller in the system and they need to
 * get its name before initialization. There is no guarantee that the UDC
 * of the returned name will be still available, when gadget driver registers
 * itself.
 *
 * Returns pointer to string with UDC controller name on success, NULL
 * otherwise. Caller should kfree() returned string.
 */
char *usb_get_gadget_udc_name(void)
{
	struct usb_udc *udc;
	char *name = NULL;

	/* For now we take the first available UDC */
	mutex_lock(&udc_lock);
	list_for_each_entry(udc, &udc_list, list) {
		if (!udc->driver) {
			name = kstrdup(udc->gadget->name, GFP_KERNEL);
			break;
		}
	}
	mutex_unlock(&udc_lock);
	return name;
}
EXPORT_SYMBOL_GPL(usb_get_gadget_udc_name);

/**
 * usb_add_gadget_udc - adds a new gadget to the udc class driver list
 * @parent: the parent device to this udc. Usually the controller
 * driver's device.
 * @gadget: the gadget to be added to the list
 *
 * Returns zero on success, negative errno otherwise.
 */
int usb_add_gadget_udc(struct device *parent, struct usb_gadget *gadget)
{
	return usb_add_gadget_udc_release(parent, gadget, NULL);
}
EXPORT_SYMBOL_GPL(usb_add_gadget_udc);

static void usb_gadget_remove_driver(struct usb_udc *udc)
{
	dev_dbg(&udc->dev, "unregistering UDC driver [%s]\n",
			udc->driver->function);

	kobject_uevent(&udc->dev.kobj, KOBJ_CHANGE);

	usb_gadget_disconnect(udc->gadget);
	udc->driver->disconnect(udc->gadget);
	udc->driver->unbind(udc->gadget);
	usb_gadget_udc_stop(udc);

	udc->driver = NULL;
	udc->dev.driver = NULL;
	udc->gadget->dev.driver = NULL;
}

/**
 * usb_del_gadget_udc - deletes @udc from udc_list
 * @gadget: the gadget to be removed.
 *
 * This, will call usb_gadget_unregister_driver() if
 * the @udc is still busy.
 */
void usb_del_gadget_udc(struct usb_gadget *gadget)
{
	struct usb_udc *udc = gadget->udc;

	if (!udc)
		return;

	dev_vdbg(gadget->dev.parent, "unregistering gadget\n");

	mutex_lock(&udc_lock);
	list_del(&udc->list);

	if (udc->driver) {
		struct usb_gadget_driver *driver = udc->driver;

		usb_gadget_remove_driver(udc);
		list_add(&driver->pending, &gadget_driver_pending_list);
	}
	mutex_unlock(&udc_lock);

	kobject_uevent(&udc->dev.kobj, KOBJ_REMOVE);
	flush_work(&gadget->work);
	device_unregister(&udc->dev);
	device_unregister(&gadget->dev);
	memset(&gadget->dev, 0x00, sizeof(gadget->dev));
}
EXPORT_SYMBOL_GPL(usb_del_gadget_udc);

/* ------------------------------------------------------------------------- */

static int udc_bind_to_driver(struct usb_udc *udc, struct usb_gadget_driver *driver)
{
	int ret;

	dev_dbg(&udc->dev, "registering UDC driver [%s]\n",
			driver->function);

	udc->driver = driver;
	udc->dev.driver = &driver->driver;
	udc->gadget->dev.driver = &driver->driver;

	usb_gadget_udc_set_speed(udc, driver->max_speed);

	ret = driver->bind(udc->gadget, driver);
	if (ret)
		goto err1;
	ret = usb_gadget_udc_start(udc);
	if (ret) {
		driver->unbind(udc->gadget);
		goto err1;
	}
	usb_udc_connect_control(udc);

	kobject_uevent(&udc->dev.kobj, KOBJ_CHANGE);
	return 0;
err1:
	if (ret != -EISNAM)
		dev_err(&udc->dev, "failed to start %s: %d\n",
			udc->driver->function, ret);
	udc->driver = NULL;
	udc->dev.driver = NULL;
	udc->gadget->dev.driver = NULL;
	return ret;
}

int usb_gadget_probe_driver(struct usb_gadget_driver *driver)
{
	struct usb_udc		*udc = NULL;
	int			ret = -ENODEV;

	if (!driver || !driver->bind || !driver->setup)
		return -EINVAL;

	mutex_lock(&udc_lock);
	if (driver->udc_name) {
		list_for_each_entry(udc, &udc_list, list) {
			ret = strcmp(driver->udc_name, dev_name(&udc->dev));
			if (!ret)
				break;
		}
		if (ret)
			ret = -ENODEV;
		else if (udc->driver)
			ret = -EBUSY;
		else
			goto found;
	} else {
		list_for_each_entry(udc, &udc_list, list) {
			/* For now we take the first one */
			if (!udc->driver)
				goto found;
		}
	}

	if (!driver->match_existing_only) {
		list_add_tail(&driver->pending, &gadget_driver_pending_list);
		pr_info("udc-core: couldn't find an available UDC - added [%s] to list of pending drivers\n",
			driver->function);
		ret = 0;
	}

	mutex_unlock(&udc_lock);
	return ret;
found:
	ret = udc_bind_to_driver(udc, driver);
	mutex_unlock(&udc_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(usb_gadget_probe_driver);

int usb_gadget_unregister_driver(struct usb_gadget_driver *driver)
{
	struct usb_udc		*udc = NULL;
	int			ret = -ENODEV;

	if (!driver || !driver->unbind)
		return -EINVAL;

	mutex_lock(&udc_lock);
	list_for_each_entry(udc, &udc_list, list) {
		if (udc->driver == driver) {
			usb_gadget_remove_driver(udc);
			usb_gadget_set_state(udc->gadget,
					     USB_STATE_NOTATTACHED);

			/* Maybe there is someone waiting for this UDC? */
			check_pending_gadget_drivers(udc);
			/*
			 * For now we ignore bind errors as probably it's
			 * not a valid reason to fail other's gadget unbind
			 */
			ret = 0;
			break;
		}
	}

	if (ret) {
		list_del(&driver->pending);
		ret = 0;
	}
	mutex_unlock(&udc_lock);
	return ret;
}
EXPORT_SYMBOL_GPL(usb_gadget_unregister_driver);

/* ------------------------------------------------------------------------- */

static ssize_t srp_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct usb_udc		*udc = container_of(dev, struct usb_udc, dev);

	if (sysfs_streq(buf, "1"))
		usb_gadget_wakeup(udc->gadget);

	return n;
}
static DEVICE_ATTR_WO(srp);

static ssize_t soft_connect_store(struct device *dev,
		struct device_attribute *attr, const char *buf, size_t n)
{
	struct usb_udc		*udc = container_of(dev, struct usb_udc, dev);

	if (!udc->driver) {
		dev_err(dev, "soft-connect without a gadget driver\n");
		return -EOPNOTSUPP;
	}

	if (sysfs_streq(buf, "connect")) {
		usb_gadget_udc_start(udc);
		usb_gadget_connect(udc->gadget);
	} else if (sysfs_streq(buf, "disconnect")) {
		usb_gadget_disconnect(udc->gadget);
		udc->driver->disconnect(udc->gadget);
		usb_gadget_udc_stop(udc);
	} else {
		dev_err(dev, "unsupported command '%s'\n", buf);
		return -EINVAL;
	}

	return n;
}
static DEVICE_ATTR_WO(soft_connect);

static ssize_t state_show(struct device *dev, struct device_attribute *attr,
			  char *buf)
{
	struct usb_udc		*udc = container_of(dev, struct usb_udc, dev);
	struct usb_gadget	*gadget = udc->gadget;

	return sprintf(buf, "%s\n", usb_state_string(gadget->state));
}
static DEVICE_ATTR_RO(state);

static ssize_t function_show(struct device *dev, struct device_attribute *attr,
			     char *buf)
{
	struct usb_udc		*udc = container_of(dev, struct usb_udc, dev);
	struct usb_gadget_driver *drv = udc->driver;

	if (!drv || !drv->function)
		return 0;
	return scnprintf(buf, PAGE_SIZE, "%s\n", drv->function);
}
static DEVICE_ATTR_RO(function);

#define USB_UDC_SPEED_ATTR(name, param)					\
ssize_t name##_show(struct device *dev,					\
		struct device_attribute *attr, char *buf)		\
{									\
	struct usb_udc *udc = container_of(dev, struct usb_udc, dev);	\
	return scnprintf(buf, PAGE_SIZE, "%s\n",			\
			usb_speed_string(udc->gadget->param));		\
}									\
static DEVICE_ATTR_RO(name)

static USB_UDC_SPEED_ATTR(current_speed, speed);
static USB_UDC_SPEED_ATTR(maximum_speed, max_speed);

#define USB_UDC_ATTR(name)					\
ssize_t name##_show(struct device *dev,				\
		struct device_attribute *attr, char *buf)	\
{								\
	struct usb_udc		*udc = container_of(dev, struct usb_udc, dev); \
	struct usb_gadget	*gadget = udc->gadget;		\
								\
	return scnprintf(buf, PAGE_SIZE, "%d\n", gadget->name);	\
}								\
static DEVICE_ATTR_RO(name)

static USB_UDC_ATTR(is_otg);
static USB_UDC_ATTR(is_a_peripheral);
static USB_UDC_ATTR(b_hnp_enable);
static USB_UDC_ATTR(a_hnp_support);
static USB_UDC_ATTR(a_alt_hnp_support);
static USB_UDC_ATTR(is_selfpowered);

static struct attribute *usb_udc_attrs[] = {
	&dev_attr_srp.attr,
	&dev_attr_soft_connect.attr,
	&dev_attr_state.attr,
	&dev_attr_function.attr,
	&dev_attr_current_speed.attr,
	&dev_attr_maximum_speed.attr,

	&dev_attr_is_otg.attr,
	&dev_attr_is_a_peripheral.attr,
	&dev_attr_b_hnp_enable.attr,
	&dev_attr_a_hnp_support.attr,
	&dev_attr_a_alt_hnp_support.attr,
	&dev_attr_is_selfpowered.attr,
	NULL,
};

static const struct attribute_group usb_udc_attr_group = {
	.attrs = usb_udc_attrs,
};

static const struct attribute_group *usb_udc_attr_groups[] = {
	&usb_udc_attr_group,
	NULL,
};

static int usb_udc_uevent(struct device *dev, struct kobj_uevent_env *env)
{
	struct usb_udc		*udc = container_of(dev, struct usb_udc, dev);
	int			ret;

	ret = add_uevent_var(env, "USB_UDC_NAME=%s", udc->gadget->name);
	if (ret) {
		dev_err(dev, "failed to add uevent USB_UDC_NAME\n");
		return ret;
	}

	if (udc->driver) {
		ret = add_uevent_var(env, "USB_UDC_DRIVER=%s",
				udc->driver->function);
		if (ret) {
			dev_err(dev, "failed to add uevent USB_UDC_DRIVER\n");
			return ret;
		}
	}

	return 0;
}

static int __init usb_udc_init(void)
{
	udc_class = class_create(THIS_MODULE, "udc");
	if (IS_ERR(udc_class)) {
		pr_err("failed to create udc class --> %ld\n",
				PTR_ERR(udc_class));
		return PTR_ERR(udc_class);
	}

	udc_class->dev_uevent = usb_udc_uevent;
	return 0;
}
subsys_initcall(usb_udc_init);

static void __exit usb_udc_exit(void)
{
	class_destroy(udc_class);
}
module_exit(usb_udc_exit);

MODULE_DESCRIPTION("UDC Framework");
MODULE_AUTHOR("Felipe Balbi <balbi@ti.com>");
MODULE_LICENSE("GPL v2");
