/* Cypress West Bridge API source file (cyasdma.c)
## ===========================
## Copyright (C) 2010  Cypress Semiconductor
##
## This program is free software; you can redistribute it and/or
## modify it under the terms of the GNU General Public License
## as published by the Free Software Foundation; either version 2
## of the License, or (at your option) any later version.
##
## This program is distributed in the hope that it will be useful,
## but WITHOUT ANY WARRANTY; without even the implied warranty of
## MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
## GNU General Public License for more details.
##
## You should have received a copy of the GNU General Public License
## along with this program; if not, write to the Free Software
## Foundation, Inc., 51 Franklin Street, Fifth Floor
## Boston, MA  02110-1301, USA.
## ===========================
*/

#include "../../include/linux/westbridge/cyashal.h"
#include "../../include/linux/westbridge/cyasdma.h"
#include "../../include/linux/westbridge/cyaslowlevel.h"
#include "../../include/linux/westbridge/cyaserr.h"
#include "../../include/linux/westbridge/cyasregs.h"

/*
 * Add the DMA queue entry to the free list to be re-used later
 */
static void
cy_as_dma_add_request_to_free_queue(cy_as_device *dev_p,
	cy_as_dma_queue_entry *req_p)
{
	uint32_t imask;
	imask = cy_as_hal_disable_interrupts();

	req_p->next_p = dev_p->dma_freelist_p;
	dev_p->dma_freelist_p = req_p;

	cy_as_hal_enable_interrupts(imask);
}

/*
 * Get a DMA queue entry from the free list.
 */
static cy_as_dma_queue_entry *
cy_as_dma_get_dma_queue_entry(cy_as_device *dev_p)
{
	cy_as_dma_queue_entry *req_p;
	uint32_t imask;

	cy_as_hal_assert(dev_p->dma_freelist_p != 0);

	imask = cy_as_hal_disable_interrupts();
	req_p = dev_p->dma_freelist_p;
	dev_p->dma_freelist_p = req_p->next_p;
	cy_as_hal_enable_interrupts(imask);

	return req_p;
}

/*
 * Set the maximum size that the West Bridge hardware
 * can handle in a single DMA operation.  This size
 * may change for the P <-> U endpoints as a function
 * of the endpoint type and whether we are running
 * at full speed or high speed.
 */
cy_as_return_status_t
cy_as_dma_set_max_dma_size(cy_as_device *dev_p,
		cy_as_end_point_number_t ep, uint32_t size)
{
	/* In MTP mode, EP2 is allowed to have all max sizes. */
	if ((!dev_p->is_mtp_firmware) || (ep != 0x02)) {
		if (size < 64 || size > 1024)
			return CY_AS_ERROR_INVALID_SIZE;
	}

	CY_AS_NUM_EP(dev_p, ep)->maxhwdata = (uint16_t)size;
	return CY_AS_ERROR_SUCCESS;
}

/*
 * The callback for requests sent to West Bridge
 * to relay endpoint data.  Endpoint data for EP0
 * and EP1 are sent using mailbox requests.  This
 * is the callback that is called when a response
 * to a mailbox request to send data is received.
 */
static void
cy_as_dma_request_callback(
	cy_as_device *dev_p,
	uint8_t context,
	cy_as_ll_request_response *req_p,
	cy_as_ll_request_response *resp_p,
	cy_as_return_status_t ret)
{
	uint16_t v;
	uint16_t datacnt;
	cy_as_end_point_number_t ep;

	(void)context;

	cy_as_log_debug_message(5, "cy_as_dma_request_callback called");

	/*
	 * extract the return code from the firmware
	 */
	if (ret == CY_AS_ERROR_SUCCESS) {
		if (cy_as_ll_request_response__get_code(resp_p) !=
		CY_RESP_SUCCESS_FAILURE)
			ret = CY_AS_ERROR_INVALID_RESPONSE;
		else
			ret = cy_as_ll_request_response__get_word(resp_p, 0);
	}

	/*
	 * extract the endpoint number and the transferred byte count
	 * from the request.
	 */
	v = cy_as_ll_request_response__get_word(req_p, 0);
	ep = (cy_as_end_point_number_t)((v >> 13) & 0x01);

	if (ret == CY_AS_ERROR_SUCCESS) {
		/*
		 * if the firmware returns success,
		 * all of the data requested was
		 * transferred.  there are no partial
		 * transfers.
		 */
		datacnt = v & 0x3FF;
	} else {
		/*
		 * if the firmware returned an error, no data was transferred.
		 */
		datacnt = 0;
	}

	/*
	 * queue the request and response data structures for use with the
	 * next EP0 or EP1 request.
	 */
	if (ep == 0) {
		dev_p->usb_ep0_dma_req = req_p;
		dev_p->usb_ep0_dma_resp = resp_p;
	} else {
		dev_p->usb_ep1_dma_req = req_p;
		dev_p->usb_ep1_dma_resp = resp_p;
	}

	/*
	 * call the DMA complete function so we can
	 * signal that this portion of the transfer
	 * has completed.  if the low level request
	 * was canceled, we do not need to signal
	 * the completed function as the only way a
	 * cancel can happen is via the DMA cancel
	 * function.
	 */
	if (ret != CY_AS_ERROR_CANCELED)
		cy_as_dma_completed_callback(dev_p->tag, ep, datacnt, ret);
}

/*
 * Set the DRQ mask register for the given endpoint number.  If state is
 * CyTrue, the DRQ interrupt for the given endpoint is enabled, otherwise
 * it is disabled.
 */
static void
cy_as_dma_set_drq(cy_as_device *dev_p,
		cy_as_end_point_number_t ep, cy_bool state)
{
	uint16_t mask;
	uint16_t v;
	uint32_t intval;

	/*
	 * there are not DRQ register bits for EP0 and EP1
	 */
	if (ep == 0 || ep == 1)
		return;

	/*
	 * disable interrupts while we do this to be sure the state of the
	 * DRQ mask register is always well defined.
	 */
	intval = cy_as_hal_disable_interrupts();

	/*
	 * set the DRQ bit to the given state for the ep given
	 */
	mask = (1 << ep);
	v = cy_as_hal_read_register(dev_p->tag, CY_AS_MEM_P0_DRQ_MASK);

	if (state)
		v |= mask;
	else
		v &= ~mask;

	cy_as_hal_write_register(dev_p->tag, CY_AS_MEM_P0_DRQ_MASK, v);
	cy_as_hal_enable_interrupts(intval);
}

/*
* Send the next DMA request for the endpoint given
*/
static void
cy_as_dma_send_next_dma_request(cy_as_device *dev_p, cy_as_dma_end_point *ep_p)
{
	uint32_t datacnt;
	void *buf_p;
	cy_as_dma_queue_entry *dma_p;

	cy_as_log_debug_message(6, "cy_as_dma_send_next_dma_request called");

	/* If the queue is empty, nothing to do */
	dma_p = ep_p->queue_p;
	if (dma_p == 0) {
		/*
		 * there are no pending DMA requests
		 * for this endpoint.  disable the DRQ
		 * mask bits to insure no interrupts
		 * will be triggered by this endpoint
		 * until someone is interested in the data.
		 */
		cy_as_dma_set_drq(dev_p, ep_p->ep, cy_false);
		return;
	}

	cy_as_dma_end_point_set_running(ep_p);

	/*
	 * get the number of words that still
	 * need to be xferred in this request.
	 */
	datacnt = dma_p->size - dma_p->offset;
	cy_as_hal_assert(datacnt >= 0);

	/*
	 * the HAL layer should never limit the size
	 * of the transfer to something less than the
	 * maxhwdata otherwise, the data will be sent
	 * in packets that are not correct in size.
	 */
	cy_as_hal_assert(ep_p->maxhaldata == CY_AS_DMA_MAX_SIZE_HW_SIZE
			|| ep_p->maxhaldata >= ep_p->maxhwdata);

	/*
	 * update the number of words that need to be xferred yet
	 * based on the limits of the HAL layer.
	 */
	if (ep_p->maxhaldata == CY_AS_DMA_MAX_SIZE_HW_SIZE) {
		if (datacnt > ep_p->maxhwdata)
			datacnt = ep_p->maxhwdata;
	} else {
		if (datacnt > ep_p->maxhaldata)
			datacnt = ep_p->maxhaldata;
	}

	/*
	 * find a pointer to the data that needs to be transferred
	 */
	buf_p = (((char *)dma_p->buf_p) + dma_p->offset);

	/*
	 * mark a request in transit
	 */
	cy_as_dma_end_point_set_in_transit(ep_p);

	if (ep_p->ep == 0 || ep_p->ep == 1) {
		/*
		 * if this is a WRITE request on EP0 and EP1
		 * we write the data via an EP_DATA request
		 * to west bridge via the mailbox registers.
		 * if this is a READ request, we do nothing
		 * and the data will arrive via an EP_DATA
		 * request from west bridge.  in the request
		 * handler for the USB context we will pass
		 * the data back into the DMA module.
		 */
		if (dma_p->readreq == cy_false) {
			uint16_t v;
			uint16_t len;
			cy_as_ll_request_response *resp_p;
			cy_as_ll_request_response *req_p;
			cy_as_return_status_t ret;

			len = (uint16_t)(datacnt / 2);
			if (datacnt % 2)
				len++;

			len++;

			if (ep_p->ep == 0) {
				req_p = dev_p->usb_ep0_dma_req;
				resp_p = dev_p->usb_ep0_dma_resp;
				dev_p->usb_ep0_dma_req = 0;
				dev_p->usb_ep0_dma_resp = 0;
			} else {
				req_p = dev_p->usb_ep1_dma_req;
				resp_p = dev_p->usb_ep1_dma_resp;
				dev_p->usb_ep1_dma_req = 0;
				dev_p->usb_ep1_dma_resp = 0;
			}

			cy_as_hal_assert(req_p != 0);
			cy_as_hal_assert(resp_p != 0);
			cy_as_hal_assert(len <= 64);

			cy_as_ll_init_request(req_p, CY_RQT_USB_EP_DATA,
				CY_RQT_USB_RQT_CONTEXT, len);

			v = (uint16_t)(datacnt | (ep_p->ep << 13) | (1 << 14));
			if (dma_p->offset == 0)
				v |= (1 << 12);/* Set the first packet bit */
			if (dma_p->offset + datacnt == dma_p->size)
				v |= (1 << 11);/* Set the last packet bit */

			cy_as_ll_request_response__set_word(req_p, 0, v);
			cy_as_ll_request_response__pack(req_p,
					1, datacnt, buf_p);

			cy_as_ll_init_response(resp_p, 1);

			ret = cy_as_ll_send_request(dev_p, req_p, resp_p,
				cy_false, cy_as_dma_request_callback);
			if (ret == CY_AS_ERROR_SUCCESS)
				cy_as_log_debug_message(5,
				"+++ send EP 0/1 data via mailbox registers");
			else
				cy_as_log_debug_message(5,
				"+++ error sending EP 0/1 data via mailbox "
				"registers - CY_AS_ERROR_TIMEOUT");

			if (ret != CY_AS_ERROR_SUCCESS)
				cy_as_dma_completed_callback(dev_p->tag,
					ep_p->ep, 0, ret);
		}
	} else {
		/*
		 * this is a DMA request on an endpoint that is accessible
		 * via the P port.  ask the HAL DMA capabilities to
		 * perform this.  the amount of data sent is limited by the
		 * HAL max size as well as what we need to send.  if the
		 * ep_p->maxhaldata is set to a value larger than the
		 * endpoint buffer size, then we will pass more than a
		 * single buffer worth of data to the HAL layer and expect
		 * the HAL layer to divide the data into packets.  the last
		 * parameter here (ep_p->maxhwdata) gives the packet size for
		 * the data so the HAL layer knows what the packet size should
		 * be.
		 */
		if (cy_as_dma_end_point_is_direction_in(ep_p))
			cy_as_hal_dma_setup_write(dev_p->tag,
				ep_p->ep, buf_p, datacnt, ep_p->maxhwdata);
		else
			cy_as_hal_dma_setup_read(dev_p->tag,
				ep_p->ep, buf_p, datacnt, ep_p->maxhwdata);

		/*
		 * the DRQ interrupt for this endpoint should be enabled
		 * so that the data transfer progresses at interrupt time.
		 */
		cy_as_dma_set_drq(dev_p, ep_p->ep, cy_true);
	}
}

/*
 * This function is called when the HAL layer has
 * completed the last requested DMA operation.
 * This function sends/receives the next batch of
 * data associated with the current DMA request,
 * or it is is complete, moves to the next DMA request.
 */
void
cy_as_dma_completed_callback(cy_as_hal_device_tag tag,
	cy_as_end_point_number_t ep, uint32_t cnt, cy_as_return_status_t status)
{
	uint32_t mask;
	cy_as_dma_queue_entry *req_p;
	cy_as_dma_end_point *ep_p;
	cy_as_device *dev_p = cy_as_device_find_from_tag(tag);

	/* Make sure the HAL layer gave us good parameters */
	cy_as_hal_assert(dev_p != 0);
	cy_as_hal_assert(dev_p->sig == CY_AS_DEVICE_HANDLE_SIGNATURE);
	cy_as_hal_assert(ep < 16);


	/* Get the endpoint ptr */
	ep_p = CY_AS_NUM_EP(dev_p, ep);
	cy_as_hal_assert(ep_p->queue_p != 0);

	/* Get a pointer to the current entry in the queue */
	mask = cy_as_hal_disable_interrupts();
	req_p = ep_p->queue_p;

	/* Update the offset to reflect the data actually received or sent */
	req_p->offset += cnt;

	/*
	 * if we are still sending/receiving the current packet,
	 * send/receive the next chunk basically we keep going
	 * if we have not sent/received enough data, and we are
	 * not doing a packet operation, and the last packet
	 * sent or received was a full sized packet.  in other
	 * words, when we are NOT doing a packet operation, a
	 * less than full size packet (a short packet) will
	 * terminate the operation.
	 *
	 * note: if this is EP1 request and the request has
	 * timed out, it means the buffer is not free.
	 * we have to resend the data.
	 *
	 * note: for the MTP data transfers, the DMA transfer
	 * for the next packet can only be started asynchronously,
	 * after a firmware event notifies that the device is ready.
	 */
	if (((req_p->offset != req_p->size) && (req_p->packet == cy_false) &&
		((cnt == ep_p->maxhaldata) || ((cnt == ep_p->maxhwdata) &&
		((ep != CY_AS_MTP_READ_ENDPOINT) ||
		(cnt == dev_p->usb_max_tx_size)))))
			|| ((ep == 1) && (status == CY_AS_ERROR_TIMEOUT))) {
		cy_as_hal_enable_interrupts(mask);

		/*
		 * and send the request again to send the next block of
		 * data. special handling for MTP transfers on E_ps 2
		 * and 6. the send_next_request will be processed based
		 * on the event sent by the firmware.
		 */
		if ((ep == CY_AS_MTP_WRITE_ENDPOINT) || (
				(ep == CY_AS_MTP_READ_ENDPOINT) &&
				(!cy_as_dma_end_point_is_direction_in(ep_p))))
			cy_as_dma_end_point_set_stopped(ep_p);
		else
			cy_as_dma_send_next_dma_request(dev_p, ep_p);
	} else {
		/*
		 * we get here if ...
		 *	we have sent or received all of the data
		 *		 or
		 *	we are doing a packet operation
		 *		 or
		 *	we receive a short packet
		 */

		/*
		 * remove this entry from the DMA queue for this endpoint.
		 */
		cy_as_dma_end_point_clear_in_transit(ep_p);
		ep_p->queue_p = req_p->next_p;
		if (ep_p->last_p == req_p) {
			/*
			 * we have removed the last packet from the DMA queue,
			 * disable the interrupt associated with this interrupt.
			 */
			ep_p->last_p = 0;
			cy_as_hal_enable_interrupts(mask);
			cy_as_dma_set_drq(dev_p, ep, cy_false);
		} else
			cy_as_hal_enable_interrupts(mask);

		if (req_p->cb) {
			/*
			 * if the request has a callback associated with it,
			 * call the callback to tell the interested party that
			 * this DMA request has completed.
			 *
			 * note, we set the in_callback bit to insure that we
			 * cannot recursively call an API function that is
			 * synchronous only from a callback.
			 */
			cy_as_device_set_in_callback(dev_p);
			(*req_p->cb)(dev_p, ep, req_p->buf_p,
				req_p->offset, status);
			cy_as_device_clear_in_callback(dev_p);
		}

		/*
		 * we are done with this request, put it on the freelist to be
		 * reused at a later time.
		 */
		cy_as_dma_add_request_to_free_queue(dev_p, req_p);

		if (ep_p->queue_p == 0) {
			/*
			 * if the endpoint is out of DMA entries, set the
			 * endpoint as stopped.
			 */
			cy_as_dma_end_point_set_stopped(ep_p);

			/*
			 * the DMA queue is empty, wake any task waiting on
			 * the QUEUE to drain.
			 */
			if (cy_as_dma_end_point_is_sleeping(ep_p)) {
				cy_as_dma_end_point_set_wake_state(ep_p);
				cy_as_hal_wake(&ep_p->channel);
			}
		} else {
			/*
			 * if the queued operation is a MTP transfer,
			 * wait until firmware event before sending
			 * down the next DMA request.
			 */
			if ((ep == CY_AS_MTP_WRITE_ENDPOINT) ||
				((ep == CY_AS_MTP_READ_ENDPOINT) &&
				(!cy_as_dma_end_point_is_direction_in(ep_p))) ||
				((ep == dev_p->storage_read_endpoint) &&
				(!cy_as_device_is_p2s_dma_start_recvd(dev_p)))
				|| ((ep == dev_p->storage_write_endpoint) &&
				(!cy_as_device_is_p2s_dma_start_recvd(dev_p))))
				cy_as_dma_end_point_set_stopped(ep_p);
			else
				cy_as_dma_send_next_dma_request(dev_p, ep_p);
		}
	}
}

/*
* This function is used to kick start DMA on a given
* channel.  If DMA is already running on the given
* endpoint, nothing happens.  If DMA is not running,
* the first entry is pulled from the DMA queue and
* sent/recevied to/from the West Bridge device.
*/
cy_as_return_status_t
cy_as_dma_kick_start(cy_as_device *dev_p, cy_as_end_point_number_t ep)
{
	cy_as_dma_end_point *ep_p;
	cy_as_hal_assert(dev_p->sig == CY_AS_DEVICE_HANDLE_SIGNATURE);

	ep_p = CY_AS_NUM_EP(dev_p, ep);

	/* We are already running */
	if (cy_as_dma_end_point_is_running(ep_p))
		return CY_AS_ERROR_SUCCESS;

	cy_as_dma_send_next_dma_request(dev_p, ep_p);
	return CY_AS_ERROR_SUCCESS;
}

/*
 * This function stops the given endpoint.  Stopping and endpoint cancels
 * any pending DMA operations and frees all resources associated with the
 * given endpoint.
 */
static cy_as_return_status_t
cy_as_dma_stop_end_point(cy_as_device *dev_p, cy_as_end_point_number_t ep)
{
	cy_as_return_status_t ret;
	cy_as_dma_end_point *ep_p = CY_AS_NUM_EP(dev_p, ep);

	/*
	 * cancel any pending DMA requests associated with this endpoint. this
	 * cancels any DMA requests at the HAL layer as well as dequeues any
	 * request that is currently pending.
	 */
	ret = cy_as_dma_cancel(dev_p, ep, CY_AS_ERROR_CANCELED);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	/*
	 * destroy the sleep channel
	 */
	if (!cy_as_hal_destroy_sleep_channel(&ep_p->channel)
		&& ret == CY_AS_ERROR_SUCCESS)
		ret = CY_AS_ERROR_DESTROY_SLEEP_CHANNEL_FAILED;

	/*
	 * free the memory associated with this endpoint
	 */
	cy_as_hal_free(ep_p);

	/*
	 * set the data structure ptr to something sane since the
	 * previous pointer is now free.
	 */
	dev_p->endp[ep] = 0;

	return ret;
}

/*
 * This method stops the USB stack.  This is an internal function that does
 * all of the work of destroying the USB stack without the protections that
 * we provide to the API (i.e. stopping at stack that is not running).
 */
static cy_as_return_status_t
cy_as_dma_stop_internal(cy_as_device *dev_p)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	cy_as_return_status_t lret;
	cy_as_end_point_number_t i;

	/*
	 * stop all of the endpoints.  this cancels all DMA requests, and
	 * frees all resources associated with each endpoint.
	 */
	for (i = 0; i < sizeof(dev_p->endp)/(sizeof(dev_p->endp[0])); i++) {
		lret = cy_as_dma_stop_end_point(dev_p, i);
		if (lret != CY_AS_ERROR_SUCCESS && ret == CY_AS_ERROR_SUCCESS)
			ret = lret;
	}

	/*
	 * now, free the list of DMA requests structures that we use to manage
	 * DMA requests.
	 */
	while (dev_p->dma_freelist_p) {
		cy_as_dma_queue_entry *req_p;
		uint32_t imask = cy_as_hal_disable_interrupts();

		req_p = dev_p->dma_freelist_p;
		dev_p->dma_freelist_p = req_p->next_p;

		cy_as_hal_enable_interrupts(imask);

		cy_as_hal_free(req_p);
	}

	cy_as_ll_destroy_request(dev_p, dev_p->usb_ep0_dma_req);
	cy_as_ll_destroy_request(dev_p, dev_p->usb_ep1_dma_req);
	cy_as_ll_destroy_response(dev_p, dev_p->usb_ep0_dma_resp);
	cy_as_ll_destroy_response(dev_p, dev_p->usb_ep1_dma_resp);

	return ret;
}


/*
 * CyAsDmaStop()
 *
 * This function shuts down the DMA module.  All resources
 * associated with the DMA module will be freed.  This
 * routine is the API stop function.  It insures that we
 * are stopping a stack that is actually running and then
 * calls the internal function to do the work.
 */
cy_as_return_status_t
cy_as_dma_stop(cy_as_device *dev_p)
{
	cy_as_return_status_t ret;

	ret = cy_as_dma_stop_internal(dev_p);
	cy_as_device_set_dma_stopped(dev_p);

	return ret;
}

/*
 * CyAsDmaStart()
 *
 * This function initializes the DMA module to insure it is up and running.
 */
cy_as_return_status_t
cy_as_dma_start(cy_as_device *dev_p)
{
	cy_as_end_point_number_t i;
	uint16_t cnt;

	if (cy_as_device_is_dma_running(dev_p))
		return CY_AS_ERROR_ALREADY_RUNNING;

	/*
	 * pre-allocate DMA queue structures to be used in the interrupt context
	 */
	for (cnt = 0; cnt < 32; cnt++) {
		cy_as_dma_queue_entry *entry_p = (cy_as_dma_queue_entry *)
			cy_as_hal_alloc(sizeof(cy_as_dma_queue_entry));
		if (entry_p == 0) {
			cy_as_dma_stop_internal(dev_p);
			return CY_AS_ERROR_OUT_OF_MEMORY;
		}
		cy_as_dma_add_request_to_free_queue(dev_p, entry_p);
	}

	/*
	 * pre-allocate the DMA requests for sending EP0
	 * and EP1 data to west bridge
	 */
	dev_p->usb_ep0_dma_req = cy_as_ll_create_request(dev_p,
		CY_RQT_USB_EP_DATA, CY_RQT_USB_RQT_CONTEXT, 64);
	dev_p->usb_ep1_dma_req = cy_as_ll_create_request(dev_p,
		CY_RQT_USB_EP_DATA, CY_RQT_USB_RQT_CONTEXT, 64);

	if (dev_p->usb_ep0_dma_req == 0 || dev_p->usb_ep1_dma_req == 0) {
		cy_as_dma_stop_internal(dev_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}
	dev_p->usb_ep0_dma_req_save = dev_p->usb_ep0_dma_req;

	dev_p->usb_ep0_dma_resp = cy_as_ll_create_response(dev_p, 1);
	dev_p->usb_ep1_dma_resp = cy_as_ll_create_response(dev_p, 1);
	if (dev_p->usb_ep0_dma_resp == 0 || dev_p->usb_ep1_dma_resp == 0) {
		cy_as_dma_stop_internal(dev_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}
	dev_p->usb_ep0_dma_resp_save = dev_p->usb_ep0_dma_resp;

	/*
	 * set the dev_p->endp to all zeros to insure cleanup is possible if
	 * an error occurs during initialization.
	 */
	cy_as_hal_mem_set(dev_p->endp, 0, sizeof(dev_p->endp));

	/*
	 * now, iterate through each of the endpoints and initialize each
	 * one.
	 */
	for (i = 0; i < sizeof(dev_p->endp)/sizeof(dev_p->endp[0]); i++) {
		dev_p->endp[i] = (cy_as_dma_end_point *)
			cy_as_hal_alloc(sizeof(cy_as_dma_end_point));
		if (dev_p->endp[i] == 0) {
			cy_as_dma_stop_internal(dev_p);
			return CY_AS_ERROR_OUT_OF_MEMORY;
		}
		cy_as_hal_mem_set(dev_p->endp[i], 0,
			sizeof(cy_as_dma_end_point));

		dev_p->endp[i]->ep = i;
		dev_p->endp[i]->queue_p = 0;
		dev_p->endp[i]->last_p = 0;

		cy_as_dma_set_drq(dev_p, i, cy_false);

		if (!cy_as_hal_create_sleep_channel(&dev_p->endp[i]->channel))
			return CY_AS_ERROR_CREATE_SLEEP_CHANNEL_FAILED;
	}

	/*
	 * tell the HAL layer who to call when the
	 * HAL layer completes a DMA request
	 */
	cy_as_hal_dma_register_callback(dev_p->tag,
		cy_as_dma_completed_callback);

	/*
	 * mark DMA as up and running on this device
	 */
	cy_as_device_set_dma_running(dev_p);

	return CY_AS_ERROR_SUCCESS;
}

/*
* Wait for all entries in the DMA queue associated
* the given endpoint to be drained.  This function
* will not return until all the DMA data has been
* transferred.
*/
cy_as_return_status_t
cy_as_dma_drain_queue(cy_as_device *dev_p,
	cy_as_end_point_number_t ep, cy_bool kickstart)
{
	cy_as_dma_end_point *ep_p;
	int loopcount = 1000;
	uint32_t mask;

	/*
	* make sure the endpoint is valid
	*/
	if (ep >= sizeof(dev_p->endp)/sizeof(dev_p->endp[0]))
		return CY_AS_ERROR_INVALID_ENDPOINT;

	/* Get the endpoint pointer based on the endpoint number */
	ep_p = CY_AS_NUM_EP(dev_p, ep);

	/*
	* if the endpoint is empty of traffic, we return
	* with success immediately
	*/
	mask = cy_as_hal_disable_interrupts();
	if (ep_p->queue_p == 0) {
		cy_as_hal_enable_interrupts(mask);
		return CY_AS_ERROR_SUCCESS;
	} else {
		/*
		 * add 10 seconds to the time out value for each 64 KB segment
		 * of data to be transferred.
		 */
		if (ep_p->queue_p->size > 0x10000)
			loopcount += ((ep_p->queue_p->size / 0x10000) * 1000);
	}
	cy_as_hal_enable_interrupts(mask);

	/* If we are already sleeping on this endpoint, it is an error */
	if (cy_as_dma_end_point_is_sleeping(ep_p))
		return CY_AS_ERROR_NESTED_SLEEP;

	/*
	* we disable the endpoint while the queue drains to
	* prevent any additional requests from being queued while we are waiting
	*/
	cy_as_dma_enable_end_point(dev_p, ep,
		cy_false, cy_as_direction_dont_change);

	if (kickstart) {
		/*
		* now, kick start the DMA if necessary
		*/
		cy_as_dma_kick_start(dev_p, ep);
	}

	/*
	* check one last time before we begin sleeping to see if the
	* queue is drained.
	*/
	if (ep_p->queue_p == 0) {
		cy_as_dma_enable_end_point(dev_p, ep, cy_true,
			cy_as_direction_dont_change);
		return CY_AS_ERROR_SUCCESS;
	}

	while (loopcount-- > 0) {
		/*
		 * sleep for 10 ms maximum (per loop) while
		 * waiting for the transfer to complete.
		 */
		cy_as_dma_end_point_set_sleep_state(ep_p);
		cy_as_hal_sleep_on(&ep_p->channel, 10);

		/* If we timed out, the sleep bit will still be set */
		cy_as_dma_end_point_set_wake_state(ep_p);

		/* Check the queue to see if is drained */
		if (ep_p->queue_p == 0) {
			/*
			 * clear the endpoint running and in transit flags
			 * for the endpoint, now that its DMA queue is empty.
			 */
			cy_as_dma_end_point_clear_in_transit(ep_p);
			cy_as_dma_end_point_set_stopped(ep_p);

			cy_as_dma_enable_end_point(dev_p, ep,
				cy_true, cy_as_direction_dont_change);
			return CY_AS_ERROR_SUCCESS;
		}
	}

	/*
	 * the DMA operation that has timed out can be cancelled, so that later
	 * operations on this queue can proceed.
	 */
	cy_as_dma_cancel(dev_p, ep, CY_AS_ERROR_TIMEOUT);
	cy_as_dma_enable_end_point(dev_p, ep,
		cy_true, cy_as_direction_dont_change);
	return CY_AS_ERROR_TIMEOUT;
}

/*
* This function queues a write request in the DMA queue
* for a given endpoint.  The direction of the
* entry will be inferred from the endpoint direction.
*/
cy_as_return_status_t
cy_as_dma_queue_request(cy_as_device *dev_p,
	cy_as_end_point_number_t ep, void *mem_p,
	uint32_t size, cy_bool pkt, cy_bool readreq, cy_as_dma_callback cb)
{
	uint32_t mask;
	cy_as_dma_queue_entry *entry_p;
	cy_as_dma_end_point *ep_p;

	/*
	* make sure the endpoint is valid
	*/
	if (ep >= sizeof(dev_p->endp)/sizeof(dev_p->endp[0]))
		return CY_AS_ERROR_INVALID_ENDPOINT;

	/* Get the endpoint pointer based on the endpoint number */
	ep_p = CY_AS_NUM_EP(dev_p, ep);

	if (!cy_as_dma_end_point_is_enabled(ep_p))
		return CY_AS_ERROR_ENDPOINT_DISABLED;

	entry_p = cy_as_dma_get_dma_queue_entry(dev_p);

	entry_p->buf_p = mem_p;
	entry_p->cb = cb;
	entry_p->size = size;
	entry_p->offset = 0;
	entry_p->packet = pkt;
	entry_p->readreq = readreq;

	mask = cy_as_hal_disable_interrupts();
	entry_p->next_p = 0;
	if (ep_p->last_p)
		ep_p->last_p->next_p = entry_p;
	ep_p->last_p = entry_p;
	if (ep_p->queue_p == 0)
		ep_p->queue_p = entry_p;
	cy_as_hal_enable_interrupts(mask);

	return CY_AS_ERROR_SUCCESS;
}

/*
* This function enables or disables and endpoint for DMA
* queueing.  If an endpoint is disabled, any queue requests
* continue to be processed, but no new requests can be queued.
*/
cy_as_return_status_t
cy_as_dma_enable_end_point(cy_as_device *dev_p,
	cy_as_end_point_number_t ep, cy_bool enable, cy_as_dma_direction dir)
{
	cy_as_dma_end_point *ep_p;

	/*
	* make sure the endpoint is valid
	*/
	if (ep >= sizeof(dev_p->endp)/sizeof(dev_p->endp[0]))
		return CY_AS_ERROR_INVALID_ENDPOINT;

	/* Get the endpoint pointer based on the endpoint number */
	ep_p = CY_AS_NUM_EP(dev_p, ep);

	if (dir == cy_as_direction_out)
		cy_as_dma_end_point_set_direction_out(ep_p);
	else if (dir == cy_as_direction_in)
		cy_as_dma_end_point_set_direction_in(ep_p);

	/*
	* get the maximum size of data buffer the HAL
	* layer can accept.  this is used when the DMA
	* module is sending DMA requests to the HAL.
	* the DMA module will never send down a request
	* that is greater than this value.
	*
	* for EP0 and EP1, we can send no more than 64
	* bytes of data at one time as this is the maximum
	* size of a packet that can be sent via these
	* endpoints.
	*/
	if (ep == 0 || ep == 1)
		ep_p->maxhaldata = 64;
	else
		ep_p->maxhaldata = cy_as_hal_dma_max_request_size(
						dev_p->tag, ep);

	if (enable)
		cy_as_dma_end_point_enable(ep_p);
	else
		cy_as_dma_end_point_disable(ep_p);

	return CY_AS_ERROR_SUCCESS;
}

/*
 * This function cancels any DMA operations pending with the HAL layer as well
 * as any DMA operation queued on the endpoint.
 */
cy_as_return_status_t
cy_as_dma_cancel(
	cy_as_device *dev_p,
	cy_as_end_point_number_t ep,
	cy_as_return_status_t err)
{
	uint32_t mask;
	cy_as_dma_end_point *ep_p;
	cy_as_dma_queue_entry *entry_p;
	cy_bool epstate;

	/*
	 * make sure the endpoint is valid
	 */
	if (ep >= sizeof(dev_p->endp)/sizeof(dev_p->endp[0]))
		return CY_AS_ERROR_INVALID_ENDPOINT;

	/* Get the endpoint pointer based on the endpoint number */
	ep_p = CY_AS_NUM_EP(dev_p, ep);

	if (ep_p) {
		/* Remember the state of the endpoint */
		epstate = cy_as_dma_end_point_is_enabled(ep_p);

		/*
		 * disable the endpoint so no more DMA packets can be
		 * queued.
		 */
		cy_as_dma_enable_end_point(dev_p, ep,
			cy_false, cy_as_direction_dont_change);

		/*
		 * don't allow any interrupts from this endpoint
		 * while we get the most current request off of
		 * the queue.
		 */
		cy_as_dma_set_drq(dev_p, ep, cy_false);

		/*
		 * cancel any pending request queued in the HAL layer
		 */
		if (cy_as_dma_end_point_in_transit(ep_p))
			cy_as_hal_dma_cancel_request(dev_p->tag, ep_p->ep);

		/*
		 * shutdown the DMA for this endpoint so no
		 * more data is transferred
		 */
		cy_as_dma_end_point_set_stopped(ep_p);

		/*
		 * mark the endpoint as not in transit, because we are
		 * going to consume any queued requests
		 */
		cy_as_dma_end_point_clear_in_transit(ep_p);

		/*
		 * now, remove each entry in the queue and call the
		 * associated callback stating that the request was
		 * canceled.
		 */
		ep_p->last_p = 0;
		while (ep_p->queue_p != 0) {
			/* Disable interrupts to manipulate the queue */
			mask = cy_as_hal_disable_interrupts();

			/* Remove an entry from the queue */
			entry_p = ep_p->queue_p;
			ep_p->queue_p = entry_p->next_p;

			/* Ok, the queue has been updated, we can
			 * turn interrupts back on */
			cy_as_hal_enable_interrupts(mask);

			/* Call the callback indicating we have
			 * canceled the DMA */
			if (entry_p->cb)
				entry_p->cb(dev_p, ep,
					entry_p->buf_p, entry_p->size, err);

			cy_as_dma_add_request_to_free_queue(dev_p, entry_p);
		}

		if (ep == 0 || ep == 1) {
			/*
			 * if this endpoint is zero or one, we need to
			 * clear the queue of any pending CY_RQT_USB_EP_DATA
			 * requests as these are pending requests to send
			 * data to the west bridge device.
			 */
			cy_as_ll_remove_ep_data_requests(dev_p, ep);
		}

		if (epstate) {
			/*
			 * the endpoint started out enabled, so we
			 * re-enable the endpoint here.
			 */
			cy_as_dma_enable_end_point(dev_p, ep,
				cy_true, cy_as_direction_dont_change);
		}
	}

	return CY_AS_ERROR_SUCCESS;
}

cy_as_return_status_t
cy_as_dma_received_data(cy_as_device *dev_p,
	cy_as_end_point_number_t ep, uint32_t dsize, void *data)
{
	cy_as_dma_queue_entry *dma_p;
	uint8_t *src_p, *dest_p;
	cy_as_dma_end_point *ep_p;
	uint32_t xfersize;

	/*
	 * make sure the endpoint is valid
	 */
	if (ep != 0 && ep != 1)
		return CY_AS_ERROR_INVALID_ENDPOINT;

	/* Get the endpoint pointer based on the endpoint number */
	ep_p = CY_AS_NUM_EP(dev_p, ep);
	dma_p = ep_p->queue_p;
	if (dma_p == 0)
		return CY_AS_ERROR_SUCCESS;

	/*
	 * if the data received exceeds the size of the DMA buffer,
	 * clip the data to the size of the buffer.  this can lead
	 * to losing some data, but is not different than doing
	 * non-packet reads on the other endpoints.
	 */
	if (dsize > dma_p->size - dma_p->offset)
		dsize = dma_p->size - dma_p->offset;

	/*
	 * copy the data from the request packet to the DMA buffer
	 * for the endpoint
	 */
	src_p = (uint8_t *)data;
	dest_p = ((uint8_t *)(dma_p->buf_p)) + dma_p->offset;
	xfersize = dsize;
	while (xfersize-- > 0)
		*dest_p++ = *src_p++;

	/* Signal the DMA module that we have
	 * received data for this EP request */
	cy_as_dma_completed_callback(dev_p->tag,
		ep, dsize, CY_AS_ERROR_SUCCESS);

	return CY_AS_ERROR_SUCCESS;
}
