/* Cypress West Bridge API source file (cyaslowlevel.c)
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
#include "../../include/linux/westbridge/cyascast.h"
#include "../../include/linux/westbridge/cyasdevice.h"
#include "../../include/linux/westbridge/cyaslowlevel.h"
#include "../../include/linux/westbridge/cyasintr.h"
#include "../../include/linux/westbridge/cyaserr.h"
#include "../../include/linux/westbridge/cyasregs.h"

static const uint32_t cy_as_low_level_timeout_count = 65536 * 4;

/* Forward declaration */
static cy_as_return_status_t cy_as_send_one(cy_as_device *dev_p,
	cy_as_ll_request_response *req_p);

/*
* This array holds the size of the largest request we will ever recevie from
* the West Bridge device per context.  The size is in 16 bit words.  Note a
* size of 0xffff indicates that there will be no requests on this context
* from West Bridge.
*/
static uint16_t max_request_length[CY_RQT_CONTEXT_COUNT] = {
	8, /* CY_RQT_GENERAL_RQT_CONTEXT - CY_RQT_INITIALIZATION_COMPLETE */
	8, /* CY_RQT_RESOURCE_RQT_CONTEXT - none */
	8, /* CY_RQT_STORAGE_RQT_CONTEXT - CY_RQT_MEDIA_CHANGED */
	128, /* CY_RQT_USB_RQT_CONTEXT - CY_RQT_USB_EVENT */
	8 /* CY_RQT_TUR_RQT_CONTEXT - CY_RQT_TURBO_CMD_FROM_HOST */
};

/*
* For the given context, this function removes the request node at the head
* of the queue from the context.  This is called after all processing has
* occurred on the given request and response and we are ready to remove this
* entry from the queue.
*/
static void
cy_as_ll_remove_request_queue_head(cy_as_device *dev_p, cy_as_context *ctxt_p)
{
	uint32_t mask, state;
	cy_as_ll_request_list_node *node_p;

	(void)dev_p;
	cy_as_hal_assert(ctxt_p->request_queue_p != 0);

	mask = cy_as_hal_disable_interrupts();
	node_p = ctxt_p->request_queue_p;
	ctxt_p->request_queue_p = node_p->next;
	cy_as_hal_enable_interrupts(mask);

	node_p->callback = 0;
	node_p->rqt = 0;
	node_p->resp = 0;

	/*
	* note that the caller allocates and destroys the request and
	* response.  generally the destroy happens in the callback for
	* async requests and after the wait returns for sync.  the
	* request and response may not actually be destroyed but may be
	* managed in other ways as well.  it is the responsibilty of
	* the caller to deal with these in any case.  the caller can do
	* this in the request/response callback function.
	*/
	state = cy_as_hal_disable_interrupts();
	cy_as_hal_c_b_free(node_p);
	cy_as_hal_enable_interrupts(state);
}

/*
* For the context given, this function sends the next request to
* West Bridge via the mailbox register, if the next request is
* ready to be sent and has not already been sent.
*/
static void
cy_as_ll_send_next_request(cy_as_device *dev_p, cy_as_context *ctxt_p)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	/*
	* ret == ret is equivalent to while (1) but eliminates compiler
	* warnings for some compilers.
	*/
	while (ret == ret) {
		cy_as_ll_request_list_node *node_p = ctxt_p->request_queue_p;
		if (node_p == 0)
			break;

		if (cy_as_request_get_node_state(node_p) !=
			CY_AS_REQUEST_LIST_STATE_QUEUED)
			break;

		cy_as_request_set_node_state(node_p,
			CY_AS_REQUEST_LIST_STATE_WAITING);
		ret = cy_as_send_one(dev_p, node_p->rqt);
		if (ret == CY_AS_ERROR_SUCCESS)
			break;

		/*
		* if an error occurs in sending the request, tell the requester
		* about the error and remove the request from the queue.
		*/
		cy_as_request_set_node_state(node_p,
			CY_AS_REQUEST_LIST_STATE_RECEIVED);
		node_p->callback(dev_p, ctxt_p->number,
			node_p->rqt, node_p->resp, ret);
		cy_as_ll_remove_request_queue_head(dev_p, ctxt_p);

		/*
		* this falls through to the while loop to send the next request
		* since the previous request did not get sent.
		*/
	}
}

/*
* This method removes an entry from the request queue of a given context.
* The entry is removed only if it is not in transit.
*/
cy_as_remove_request_result_t
cy_as_ll_remove_request(cy_as_device *dev_p, cy_as_context *ctxt_p,
	cy_as_ll_request_response *req_p, cy_bool force)
{
	uint32_t imask;
	cy_as_ll_request_list_node *node_p;
	cy_as_ll_request_list_node *tmp_p;
	uint32_t state;

	imask = cy_as_hal_disable_interrupts();
	if (ctxt_p->request_queue_p != 0 &&
		ctxt_p->request_queue_p->rqt == req_p) {
		node_p = ctxt_p->request_queue_p;
		if ((cy_as_request_get_node_state(node_p) ==
			CY_AS_REQUEST_LIST_STATE_WAITING) && (!force)) {
			cy_as_hal_enable_interrupts(imask);
			return cy_as_remove_request_in_transit;
		}

		ctxt_p->request_queue_p = node_p->next;
	} else {
		tmp_p = ctxt_p->request_queue_p;
		while (tmp_p != 0 && tmp_p->next != 0 &&
			tmp_p->next->rqt != req_p)
			tmp_p = tmp_p->next;

		if (tmp_p == 0 || tmp_p->next == 0) {
			cy_as_hal_enable_interrupts(imask);
			return cy_as_remove_request_not_found;
		}

		node_p = tmp_p->next;
		tmp_p->next = node_p->next;
	}

	if (node_p->callback)
		node_p->callback(dev_p, ctxt_p->number, node_p->rqt,
			node_p->resp, CY_AS_ERROR_CANCELED);

	state = cy_as_hal_disable_interrupts();
	cy_as_hal_c_b_free(node_p);
	cy_as_hal_enable_interrupts(state);

	cy_as_hal_enable_interrupts(imask);
	return cy_as_remove_request_sucessful;
}

void
cy_as_ll_remove_all_requests(cy_as_device *dev_p, cy_as_context *ctxt_p)
{
	cy_as_ll_request_list_node *node = ctxt_p->request_queue_p;

	while (node) {
		if (cy_as_request_get_node_state(ctxt_p->request_queue_p) !=
			CY_AS_REQUEST_LIST_STATE_RECEIVED)
			cy_as_ll_remove_request(dev_p, ctxt_p,
				node->rqt, cy_true);
		node = node->next;
	}
}

static cy_bool
cy_as_ll_is_in_queue(cy_as_context *ctxt_p, cy_as_ll_request_response *req_p)
{
	uint32_t mask;
	cy_as_ll_request_list_node *node_p;

	mask = cy_as_hal_disable_interrupts();
	node_p = ctxt_p->request_queue_p;
	while (node_p) {
		if (node_p->rqt == req_p) {
			cy_as_hal_enable_interrupts(mask);
			return cy_true;
		}
		node_p = node_p->next;
	}
	cy_as_hal_enable_interrupts(mask);
	return cy_false;
}

/*
* This is the handler for mailbox data when we are trying to send data
* to the West Bridge firmware.  The firmware may be trying to send us
* data and we need to queue this data to allow the firmware to move
* forward and be in a state to receive our request.  Here we just queue
* the data and it is processed at a later time by the mailbox interrupt
* handler.
*/
void
cy_as_ll_queue_mailbox_data(cy_as_device *dev_p)
{
	cy_as_context *ctxt_p;
	uint8_t context;
	uint16_t data[4];
	int32_t i;

	/* Read the data from mailbox 0 to determine what to do with the data */
	for (i = 3; i >= 0; i--)
		data[i] = cy_as_hal_read_register(dev_p->tag,
			cy_cast_int2U_int16(CY_AS_MEM_P0_MAILBOX0 + i));

	context = cy_as_mbox_get_context(data[0]);
	if (context >= CY_RQT_CONTEXT_COUNT) {
		cy_as_hal_print_message("mailbox request/response received "
			"with invalid context value (%d)\n", context);
		return;
	}

	ctxt_p = dev_p->context[context];

	/*
	* if we have queued too much data, drop future data.
	*/
	cy_as_hal_assert(ctxt_p->queue_index * sizeof(uint16_t) +
		sizeof(data) <= sizeof(ctxt_p->data_queue));

	for (i = 0; i < 4; i++)
		ctxt_p->data_queue[ctxt_p->queue_index++] = data[i];

	cy_as_hal_assert((ctxt_p->queue_index % 4) == 0);
	dev_p->ll_queued_data = cy_true;
}

void
cy_as_mail_box_process_data(cy_as_device *dev_p, uint16_t *data)
{
	cy_as_context *ctxt_p;
	uint8_t context;
	uint16_t *len_p;
	cy_as_ll_request_response *rec_p;
	uint8_t st;
	uint16_t src, dest;

	context = cy_as_mbox_get_context(data[0]);
	if (context >= CY_RQT_CONTEXT_COUNT) {
		cy_as_hal_print_message("mailbox request/response received "
		"with invalid context value (%d)\n", context);
		return;
	}

	ctxt_p = dev_p->context[context];

	if (cy_as_mbox_is_request(data[0])) {
		cy_as_hal_assert(ctxt_p->req_p != 0);
		rec_p = ctxt_p->req_p;
		len_p = &ctxt_p->request_length;

	} else {
		if (ctxt_p->request_queue_p == 0 ||
			cy_as_request_get_node_state(ctxt_p->request_queue_p)
			!= CY_AS_REQUEST_LIST_STATE_WAITING) {
			cy_as_hal_print_message("mailbox response received on "
				"context that was not expecting a response\n");
			cy_as_hal_print_message("  context: %d\n", context);
			cy_as_hal_print_message("  contents: 0x%04x 0x%04x "
				"0x%04x 0x%04x\n",
				data[0], data[1], data[2], data[3]);
			if (ctxt_p->request_queue_p != 0)
				cy_as_hal_print_message("  state: 0x%02x\n",
					ctxt_p->request_queue_p->state);
			return;
		}

		/* Make sure the request has an associated response */
		cy_as_hal_assert(ctxt_p->request_queue_p->resp != 0);

		rec_p = ctxt_p->request_queue_p->resp;
		len_p = &ctxt_p->request_queue_p->length;
	}

	if (rec_p->stored == 0) {
		/*
		* this is the first cycle of the response
		*/
		cy_as_ll_request_response__set_code(rec_p,
			cy_as_mbox_get_code(data[0]));
		cy_as_ll_request_response__set_context(rec_p, context);

		if (cy_as_mbox_is_last(data[0])) {
			/* This is a single cycle response */
			*len_p = rec_p->length;
			st = 1;
		} else {
			/* Ensure that enough memory has been
			 * reserved for the response. */
			cy_as_hal_assert(rec_p->length >= data[1]);
			*len_p = (data[1] < rec_p->length) ?
				data[1] : rec_p->length;
			st = 2;
		}
	} else
		st = 1;

	/* Trasnfer the data from the mailboxes to the response */
	while (rec_p->stored < *len_p && st < 4)
		rec_p->data[rec_p->stored++] = data[st++];

	if (cy_as_mbox_is_last(data[0])) {
		/* NB: The call-back that is made below can cause the
		 * addition of more data in this queue, thus causing
		 * a recursive overflow of the queue. this is prevented
		 * by removing the request entry that is currently
		 * being passed up from the data queue. if this is done,
		 * the queue only needs to be as long as two request
		 * entries from west bridge.
		*/
		if ((ctxt_p->rqt_index > 0) &&
			(ctxt_p->rqt_index <= ctxt_p->queue_index)) {
			dest = 0;
			src  = ctxt_p->rqt_index;

			while (src < ctxt_p->queue_index)
				ctxt_p->data_queue[dest++] =
					ctxt_p->data_queue[src++];

			ctxt_p->rqt_index = 0;
			ctxt_p->queue_index = dest;
			cy_as_hal_assert((ctxt_p->queue_index % 4) == 0);
		}

		if (ctxt_p->request_queue_p != 0 && rec_p ==
			ctxt_p->request_queue_p->resp) {
			/*
			* if this is the last cycle of the response, call the
			* callback and reset for the next response.
			*/
			cy_as_ll_request_response *resp_p =
				ctxt_p->request_queue_p->resp;
			resp_p->length = ctxt_p->request_queue_p->length;
			cy_as_request_set_node_state(ctxt_p->request_queue_p,
				CY_AS_REQUEST_LIST_STATE_RECEIVED);

			cy_as_device_set_in_callback(dev_p);
			ctxt_p->request_queue_p->callback(dev_p, context,
				ctxt_p->request_queue_p->rqt,
				resp_p, CY_AS_ERROR_SUCCESS);

			cy_as_device_clear_in_callback(dev_p);

			cy_as_ll_remove_request_queue_head(dev_p, ctxt_p);
			cy_as_ll_send_next_request(dev_p, ctxt_p);
		} else {
			/* Send the request to the appropriate
			 * module to handle */
			cy_as_ll_request_response *request_p = ctxt_p->req_p;
			ctxt_p->req_p = 0;
			if (ctxt_p->request_callback) {
				cy_as_device_set_in_callback(dev_p);
				ctxt_p->request_callback(dev_p, context,
					request_p, 0, CY_AS_ERROR_SUCCESS);
				cy_as_device_clear_in_callback(dev_p);
			}
			cy_as_ll_init_request(request_p, 0,
				context, request_p->length);
			ctxt_p->req_p = request_p;
		}
	}
}

/*
* This is the handler for processing queued mailbox data
*/
void
cy_as_mail_box_queued_data_handler(cy_as_device *dev_p)
{
	uint16_t i;

	/*
	 * if more data gets queued in between our entering this call
	 * and the end of the iteration on all contexts; we should
	 * continue processing the queued data.
	 */
	while (dev_p->ll_queued_data) {
		dev_p->ll_queued_data = cy_false;
		for (i = 0; i < CY_RQT_CONTEXT_COUNT; i++) {
			uint16_t offset;
			cy_as_context *ctxt_p = dev_p->context[i];
			cy_as_hal_assert((ctxt_p->queue_index % 4) == 0);

			offset = 0;
			while (offset < ctxt_p->queue_index) {
				ctxt_p->rqt_index = offset + 4;
				cy_as_mail_box_process_data(dev_p,
					ctxt_p->data_queue + offset);
				offset = ctxt_p->rqt_index;
			}
			ctxt_p->queue_index = 0;
		}
	}
}

/*
* This is the handler for the mailbox interrupt.  This function reads
* data from the mailbox registers until a complete request or response
* is received.  When a complete request is received, the callback
* associated with requests on that context is called.  When a complete
* response is recevied, the callback associated with the request that
* generated the response is called.
*/
void
cy_as_mail_box_interrupt_handler(cy_as_device *dev_p)
{
	cy_as_hal_assert(dev_p->sig == CY_AS_DEVICE_HANDLE_SIGNATURE);

	/*
	* queue the mailbox data to preserve
	* order for later processing.
	*/
	cy_as_ll_queue_mailbox_data(dev_p);

	/*
	* process what was queued and anything that may be pending
	*/
	cy_as_mail_box_queued_data_handler(dev_p);
}

cy_as_return_status_t
cy_as_ll_start(cy_as_device *dev_p)
{
	uint16_t i;

	if (cy_as_device_is_low_level_running(dev_p))
		return CY_AS_ERROR_ALREADY_RUNNING;

	dev_p->ll_sending_rqt = cy_false;
	dev_p->ll_abort_curr_rqt = cy_false;

	for (i = 0; i < CY_RQT_CONTEXT_COUNT; i++) {
		dev_p->context[i] = (cy_as_context *)
			cy_as_hal_alloc(sizeof(cy_as_context));
		if (dev_p->context[i] == 0)
			return CY_AS_ERROR_OUT_OF_MEMORY;

		dev_p->context[i]->number = (uint8_t)i;
		dev_p->context[i]->request_callback = 0;
		dev_p->context[i]->request_queue_p = 0;
		dev_p->context[i]->last_node_p = 0;
		dev_p->context[i]->req_p = cy_as_ll_create_request(dev_p,
			0, (uint8_t)i, max_request_length[i]);
		dev_p->context[i]->queue_index = 0;

		if (!cy_as_hal_create_sleep_channel
			(&dev_p->context[i]->channel))
			return CY_AS_ERROR_CREATE_SLEEP_CHANNEL_FAILED;
	}

	cy_as_device_set_low_level_running(dev_p);
	return CY_AS_ERROR_SUCCESS;
}

/*
* Shutdown the low level communications module.  This operation will
* also cancel any queued low level requests.
*/
cy_as_return_status_t
cy_as_ll_stop(cy_as_device *dev_p)
{
	uint8_t i;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	cy_as_context *ctxt_p;
	uint32_t mask;

	for (i = 0; i < CY_RQT_CONTEXT_COUNT; i++) {
		ctxt_p = dev_p->context[i];
		if (!cy_as_hal_destroy_sleep_channel(&ctxt_p->channel))
			return CY_AS_ERROR_DESTROY_SLEEP_CHANNEL_FAILED;

		/*
		* now, free any queued requests and assocaited responses
		*/
		while (ctxt_p->request_queue_p) {
			uint32_t state;
			cy_as_ll_request_list_node *node_p =
				ctxt_p->request_queue_p;

			/* Mark this pair as in a cancel operation */
			cy_as_request_set_node_state(node_p,
				CY_AS_REQUEST_LIST_STATE_CANCELING);

			/* Tell the caller that we are canceling this request */
			/* NB: The callback is responsible for destroying the
			 * request and the response.  we cannot count on the
			 * contents of these two after calling the callback.
			*/
			node_p->callback(dev_p, i, node_p->rqt,
				node_p->resp, CY_AS_ERROR_CANCELED);

			/* Remove the pair from the queue */
			mask = cy_as_hal_disable_interrupts();
			ctxt_p->request_queue_p = node_p->next;
			cy_as_hal_enable_interrupts(mask);

			/* Free the list node */
			state = cy_as_hal_disable_interrupts();
			cy_as_hal_c_b_free(node_p);
			cy_as_hal_enable_interrupts(state);
		}

		cy_as_ll_destroy_request(dev_p, dev_p->context[i]->req_p);
		cy_as_hal_free(dev_p->context[i]);
		dev_p->context[i] = 0;

	}
	cy_as_device_set_low_level_stopped(dev_p);

	return ret;
}

void
cy_as_ll_init_request(cy_as_ll_request_response *req_p,
	uint16_t code, uint16_t context, uint16_t length)
{
	uint16_t totallen = sizeof(cy_as_ll_request_response) +
		(length - 1) * sizeof(uint16_t);

	cy_as_hal_mem_set(req_p, 0, totallen);
	req_p->length = length;
	cy_as_ll_request_response__set_code(req_p, code);
	cy_as_ll_request_response__set_context(req_p, context);
	cy_as_ll_request_response__set_request(req_p);
}

/*
* Create a new request.
*/
cy_as_ll_request_response *
cy_as_ll_create_request(cy_as_device *dev_p, uint16_t code,
	uint8_t context, uint16_t length)
{
	cy_as_ll_request_response *req_p;
	uint32_t state;
	uint16_t totallen = sizeof(cy_as_ll_request_response) +
		(length - 1) * sizeof(uint16_t);

	(void)dev_p;

	state = cy_as_hal_disable_interrupts();
	req_p = cy_as_hal_c_b_alloc(totallen);
	cy_as_hal_enable_interrupts(state);
	if (req_p)
		cy_as_ll_init_request(req_p, code, context, length);

	return req_p;
}

/*
* Destroy a request.
*/
void
cy_as_ll_destroy_request(cy_as_device *dev_p, cy_as_ll_request_response *req_p)
{
	uint32_t state;
	(void)dev_p;
	(void)req_p;

	state = cy_as_hal_disable_interrupts();
	cy_as_hal_c_b_free(req_p);
	cy_as_hal_enable_interrupts(state);

}

void
cy_as_ll_init_response(cy_as_ll_request_response *req_p, uint16_t length)
{
	uint16_t totallen = sizeof(cy_as_ll_request_response) +
		(length - 1) * sizeof(uint16_t);

	cy_as_hal_mem_set(req_p, 0, totallen);
	req_p->length = length;
	cy_as_ll_request_response__set_response(req_p);
}

/*
* Create a new response
*/
cy_as_ll_request_response *
cy_as_ll_create_response(cy_as_device *dev_p, uint16_t length)
{
	cy_as_ll_request_response *req_p;
	uint32_t state;
	uint16_t totallen = sizeof(cy_as_ll_request_response) +
		(length - 1) * sizeof(uint16_t);

	(void)dev_p;

	state = cy_as_hal_disable_interrupts();
	req_p = cy_as_hal_c_b_alloc(totallen);
	cy_as_hal_enable_interrupts(state);
	if (req_p)
		cy_as_ll_init_response(req_p, length);

	return req_p;
}

/*
* Destroy the new response
*/
void
cy_as_ll_destroy_response(cy_as_device *dev_p, cy_as_ll_request_response *req_p)
{
	uint32_t state;
	(void)dev_p;
	(void)req_p;

	state = cy_as_hal_disable_interrupts();
	cy_as_hal_c_b_free(req_p);
	cy_as_hal_enable_interrupts(state);
}

static uint16_t
cy_as_read_intr_status(
				   cy_as_device *dev_p)
{
	uint32_t mask;
	cy_bool bloop = cy_true;
	uint16_t v = 0, last = 0xffff;

	/*
	* before determining if the mailboxes are ready for more data,
	* we first check the mailbox interrupt to see if we need to
	* receive data.  this prevents a dead-lock condition that can
	* occur when both sides are trying to receive data.
	*/
	while (last == last) {
		/*
		* disable interrupts to be sure we don't process the mailbox
		* here and have the interrupt routine try to read this data
		* as well.
		*/
		mask = cy_as_hal_disable_interrupts();

		/*
		* see if there is data to be read.
		*/
		v = cy_as_hal_read_register(dev_p->tag, CY_AS_MEM_P0_INTR_REG);
		if ((v & CY_AS_MEM_P0_INTR_REG_MBINT) == 0) {
			cy_as_hal_enable_interrupts(mask);
			break;
		}

		/*
		* queue the mailbox data for later processing.
		* this allows the firmware to move forward and
		* service the requst from the P port.
		*/
		cy_as_ll_queue_mailbox_data(dev_p);

		/*
		* enable interrupts again to service mailbox
		* interrupts appropriately
		*/
		cy_as_hal_enable_interrupts(mask);
	}

	/*
	* now, all data is received
	*/
	last = cy_as_hal_read_register(dev_p->tag,
		CY_AS_MEM_MCU_MB_STAT) & CY_AS_MEM_P0_MCU_MBNOTRD;
	while (bloop) {
		v = cy_as_hal_read_register(dev_p->tag,
		CY_AS_MEM_MCU_MB_STAT) & CY_AS_MEM_P0_MCU_MBNOTRD;
		if (v == last)
			break;

		last = v;
	}

	return v;
}

/*
* Send a single request or response using the mail box register.
* This function does not deal with the internal queues at all,
* but only sends the request or response across to the firmware
*/
static cy_as_return_status_t
cy_as_send_one(
			cy_as_device *dev_p,
			cy_as_ll_request_response *req_p)
{
	int i;
	uint16_t mb0, v;
	int32_t loopcount;
	uint32_t int_stat;

#ifdef _DEBUG
	if (cy_as_ll_request_response__is_request(req_p)) {
		switch (cy_as_ll_request_response__get_context(req_p)) {
		case CY_RQT_GENERAL_RQT_CONTEXT:
			cy_as_hal_assert(req_p->length * 2 + 2 <
				CY_CTX_GEN_MAX_DATA_SIZE);
			break;

		case CY_RQT_RESOURCE_RQT_CONTEXT:
			cy_as_hal_assert(req_p->length * 2 + 2 <
				CY_CTX_RES_MAX_DATA_SIZE);
			break;

		case CY_RQT_STORAGE_RQT_CONTEXT:
			cy_as_hal_assert(req_p->length * 2 + 2 <
				CY_CTX_STR_MAX_DATA_SIZE);
			break;

		case CY_RQT_USB_RQT_CONTEXT:
			cy_as_hal_assert(req_p->length * 2 + 2 <
				CY_CTX_USB_MAX_DATA_SIZE);
			break;
		}
	}
#endif

	/* Write the request to the mail box registers */
	if (req_p->length > 3) {
		uint16_t length = req_p->length;
		int which = 0;
		int st = 1;

		dev_p->ll_sending_rqt = cy_true;
		while (which < length) {
			loopcount = cy_as_low_level_timeout_count;
			do {
				v = cy_as_read_intr_status(dev_p);

			} while (v && loopcount-- > 0);

			if (v) {
				cy_as_hal_print_message(
					">>>>>> LOW LEVEL TIMEOUT "
					"%x %x %x %x\n",
					cy_as_hal_read_register(dev_p->tag,
						CY_AS_MEM_MCU_MAILBOX0),
					cy_as_hal_read_register(dev_p->tag,
						CY_AS_MEM_MCU_MAILBOX1),
					cy_as_hal_read_register(dev_p->tag,
						CY_AS_MEM_MCU_MAILBOX2),
					cy_as_hal_read_register(dev_p->tag,
						CY_AS_MEM_MCU_MAILBOX3));
				return CY_AS_ERROR_TIMEOUT;
			}

			if (dev_p->ll_abort_curr_rqt) {
				dev_p->ll_sending_rqt = cy_false;
				dev_p->ll_abort_curr_rqt = cy_false;
				return CY_AS_ERROR_CANCELED;
			}

			int_stat = cy_as_hal_disable_interrupts();

			/*
			 * check again whether the mailbox is free.
			 * it is possible that an ISR came in and
			 * wrote into the mailboxes since we last
			 * checked the status.
			 */
			v = cy_as_hal_read_register(dev_p->tag,
				CY_AS_MEM_MCU_MB_STAT) &
				CY_AS_MEM_P0_MCU_MBNOTRD;
			if (v) {
				/* Go back to the original check since
				 * the mailbox is not free. */
				cy_as_hal_enable_interrupts(int_stat);
				continue;
			}

			if (which == 0) {
				cy_as_hal_write_register(dev_p->tag,
					CY_AS_MEM_MCU_MAILBOX1, length);
				st = 2;
			} else {
				st = 1;
			}

			while ((which < length) && (st < 4)) {
				cy_as_hal_write_register(dev_p->tag,
					cy_cast_int2U_int16
						(CY_AS_MEM_MCU_MAILBOX0 + st),
						req_p->data[which++]);
				st++;
			}

			mb0 = req_p->box0;
			if (which == length) {
				dev_p->ll_sending_rqt = cy_false;
				mb0 |= CY_AS_REQUEST_RESPONSE_LAST_MASK;
			}

			if (dev_p->ll_abort_curr_rqt) {
				dev_p->ll_sending_rqt = cy_false;
				dev_p->ll_abort_curr_rqt = cy_false;
				cy_as_hal_enable_interrupts(int_stat);
				return CY_AS_ERROR_CANCELED;
			}

			cy_as_hal_write_register(dev_p->tag,
				CY_AS_MEM_MCU_MAILBOX0, mb0);

			/* Wait for the MBOX interrupt to be high */
			cy_as_hal_sleep150();
			cy_as_hal_enable_interrupts(int_stat);
		}
	} else {
check_mailbox_availability:
		/*
		* wait for the mailbox registers to become available. this
		* should be a very quick wait as the firmware is designed
		* to accept requests at interrupt time and queue them for
		* future processing.
		*/
		loopcount = cy_as_low_level_timeout_count;
		do {
			v = cy_as_read_intr_status(dev_p);

		} while (v && loopcount-- > 0);

		if (v) {
			cy_as_hal_print_message(
				">>>>>> LOW LEVEL TIMEOUT %x %x %x %x\n",
				cy_as_hal_read_register(dev_p->tag,
					CY_AS_MEM_MCU_MAILBOX0),
				cy_as_hal_read_register(dev_p->tag,
					CY_AS_MEM_MCU_MAILBOX1),
				cy_as_hal_read_register(dev_p->tag,
					CY_AS_MEM_MCU_MAILBOX2),
				cy_as_hal_read_register(dev_p->tag,
					CY_AS_MEM_MCU_MAILBOX3));
			return CY_AS_ERROR_TIMEOUT;
		}

		int_stat = cy_as_hal_disable_interrupts();

		/*
		 * check again whether the mailbox is free. it is
		 * possible that an ISR came in and wrote into the
		 * mailboxes since we last checked the status.
		 */
		v = cy_as_hal_read_register(dev_p->tag, CY_AS_MEM_MCU_MB_STAT) &
			CY_AS_MEM_P0_MCU_MBNOTRD;
		if (v) {
			/* Go back to the original check
			 * since the mailbox is not free. */
			cy_as_hal_enable_interrupts(int_stat);
			goto check_mailbox_availability;
		}

		/* Write the data associated with the request
		 * into the mbox registers 1 - 3 */
		v = 0;
		for (i = req_p->length - 1; i >= 0; i--)
			cy_as_hal_write_register(dev_p->tag,
				cy_cast_int2U_int16(CY_AS_MEM_MCU_MAILBOX1 + i),
				req_p->data[i]);

		/* Write the mbox register 0 to trigger the interrupt */
		cy_as_hal_write_register(dev_p->tag, CY_AS_MEM_MCU_MAILBOX0,
			req_p->box0 | CY_AS_REQUEST_RESPONSE_LAST_MASK);

		cy_as_hal_sleep150();
		cy_as_hal_enable_interrupts(int_stat);
	}

	return CY_AS_ERROR_SUCCESS;
}

/*
* This function queues a single request to be sent to the firmware.
*/
extern cy_as_return_status_t
cy_as_ll_send_request(
				  cy_as_device *dev_p,
				  /* The request to send */
				  cy_as_ll_request_response *req,
				  /* Storage for a reply, must be sure
				   * it is of sufficient size */
				  cy_as_ll_request_response *resp,
				  /* If true, this is a synchronous request */
				  cy_bool sync,
				  /* Callback to call when reply is received */
				  cy_as_response_callback cb
)
{
	cy_as_context *ctxt_p;
	uint16_t box0 = req->box0;
	uint8_t context;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	cy_as_ll_request_list_node *node_p;
	uint32_t mask, state;

	cy_as_hal_assert(dev_p->sig == CY_AS_DEVICE_HANDLE_SIGNATURE);

	context = cy_as_mbox_get_context(box0);
	cy_as_hal_assert(context < CY_RQT_CONTEXT_COUNT);
	ctxt_p = dev_p->context[context];

	/* Allocate the list node */
	state = cy_as_hal_disable_interrupts();
	node_p = cy_as_hal_c_b_alloc(sizeof(cy_as_ll_request_list_node));
	cy_as_hal_enable_interrupts(state);

	if (node_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/* Initialize the list node */
	node_p->callback = cb;
	node_p->length = 0;
	node_p->next = 0;
	node_p->resp = resp;
	node_p->rqt = req;
	node_p->state = CY_AS_REQUEST_LIST_STATE_QUEUED;
	if (sync)
		cy_as_request_node_set_sync(node_p);

	/* Put the request into the queue */
	mask = cy_as_hal_disable_interrupts();
	if (ctxt_p->request_queue_p == 0) {
		/* Empty queue */
		ctxt_p->request_queue_p = node_p;
		ctxt_p->last_node_p = node_p;
	} else {
		ctxt_p->last_node_p->next = node_p;
		ctxt_p->last_node_p = node_p;
	}
	cy_as_hal_enable_interrupts(mask);
	cy_as_ll_send_next_request(dev_p, ctxt_p);

	if (!cy_as_device_is_in_callback(dev_p)) {
		mask = cy_as_hal_disable_interrupts();
		cy_as_mail_box_queued_data_handler(dev_p);
		cy_as_hal_enable_interrupts(mask);
	}

	return ret;
}

static void
cy_as_ll_send_callback(
				   cy_as_device *dev_p,
				   uint8_t context,
				   cy_as_ll_request_response *rqt,
				   cy_as_ll_request_response *resp,
				   cy_as_return_status_t ret)
{
	(void)rqt;
	(void)resp;
	(void)ret;


	cy_as_hal_assert(dev_p->sig == CY_AS_DEVICE_HANDLE_SIGNATURE);

	/*
	* storage the state to return to the caller
	*/
	dev_p->ll_error = ret;

	/*
	* now wake the caller
	*/
	cy_as_hal_wake(&dev_p->context[context]->channel);
}

cy_as_return_status_t
cy_as_ll_send_request_wait_reply(
		cy_as_device *dev_p,
		/* The request to send */
		cy_as_ll_request_response *req,
		/* Storage for a reply, must be
		 * sure it is of sufficient size */
		cy_as_ll_request_response *resp
		)
{
	cy_as_return_status_t ret;
	uint8_t context;
	/* Larger 8 sec time-out to handle the init
	 * delay for slower storage devices in USB FS. */
	uint32_t loopcount = 800;
	cy_as_context *ctxt_p;

	/* Get the context for the request */
	context = cy_as_ll_request_response__get_context(req);
	cy_as_hal_assert(context < CY_RQT_CONTEXT_COUNT);
	ctxt_p = dev_p->context[context];

	ret = cy_as_ll_send_request(dev_p, req, resp,
		cy_true, cy_as_ll_send_callback);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	while (loopcount-- > 0) {
		/*
		* sleep while we wait on the response.  receiving the reply will
		* wake this thread.  we will wait, at most 2 seconds (10 ms*200
		* tries) before we timeout.  note if the reply arrives, we will
		* not sleep the entire 10 ms, just til the reply arrives.
		*/
		cy_as_hal_sleep_on(&ctxt_p->channel, 10);

		/*
		* if the request has left the queue, it means the request has
		* been sent and the reply has been received.  this means we can
		* return to the caller and be sure the reply has been received.
		*/
		if (!cy_as_ll_is_in_queue(ctxt_p, req))
			return dev_p->ll_error;
	}

	/* Remove the QueueListNode for this request. */
	cy_as_ll_remove_request(dev_p, ctxt_p, req, cy_true);

	return CY_AS_ERROR_TIMEOUT;
}

cy_as_return_status_t
cy_as_ll_register_request_callback(
			cy_as_device *dev_p,
			uint8_t context,
			cy_as_response_callback cb)
{
	cy_as_context *ctxt_p;
	cy_as_hal_assert(context < CY_RQT_CONTEXT_COUNT);
	ctxt_p = dev_p->context[context];

	ctxt_p->request_callback = cb;
	return CY_AS_ERROR_SUCCESS;
}

void
cy_as_ll_request_response__pack(
			cy_as_ll_request_response *req_p,
			uint32_t offset,
			uint32_t length,
			void *data_p)
{
	uint16_t dt;
	uint8_t *dp = (uint8_t *)data_p;

	while (length > 1) {
		dt = ((*dp++) << 8);
		dt |= (*dp++);
		cy_as_ll_request_response__set_word(req_p, offset, dt);
		offset++;
		length -= 2;
	}

	if (length == 1) {
		dt = (*dp << 8);
		cy_as_ll_request_response__set_word(req_p, offset, dt);
	}
}

void
cy_as_ll_request_response__unpack(
			cy_as_ll_request_response *req_p,
			uint32_t offset,
			uint32_t length,
			void *data_p)
{
	uint8_t *dp = (uint8_t *)data_p;

	while (length-- > 0) {
		uint16_t val = cy_as_ll_request_response__get_word
			(req_p, offset++);
		*dp++ = (uint8_t)((val >> 8) & 0xff);

		if (length) {
			length--;
			*dp++ = (uint8_t)(val & 0xff);
		}
	}
}

extern cy_as_return_status_t
cy_as_ll_send_status_response(
						 cy_as_device *dev_p,
						 uint8_t context,
						 uint16_t code,
						 uint8_t clear_storage)
{
	cy_as_return_status_t ret;
	cy_as_ll_request_response resp;
	cy_as_ll_request_response *resp_p = &resp;

	cy_as_hal_mem_set(resp_p, 0, sizeof(resp));
	resp_p->length = 1;
	cy_as_ll_request_response__set_response(resp_p);
	cy_as_ll_request_response__set_context(resp_p, context);

	if (clear_storage)
		cy_as_ll_request_response__set_clear_storage_flag(resp_p);

	cy_as_ll_request_response__set_code(resp_p, CY_RESP_SUCCESS_FAILURE);
	cy_as_ll_request_response__set_word(resp_p, 0, code);

	ret = cy_as_send_one(dev_p, resp_p);

	return ret;
}

extern cy_as_return_status_t
cy_as_ll_send_data_response(
					   cy_as_device *dev_p,
					   uint8_t context,
					   uint16_t code,
					   uint16_t length,
					   void *data)
{
	cy_as_ll_request_response *resp_p;
	uint16_t wlen;
	uint8_t respbuf[256];

	if (length > 192)
		return CY_AS_ERROR_INVALID_SIZE;

	/* Word length for bytes */
	wlen = length / 2;

	/* If byte length odd, add one more */
	if (length % 2)
		wlen++;

	/* One for the length of field */
	wlen++;

	resp_p = (cy_as_ll_request_response *)respbuf;
	cy_as_hal_mem_set(resp_p, 0, sizeof(respbuf));
	resp_p->length = wlen;
	cy_as_ll_request_response__set_context(resp_p, context);
	cy_as_ll_request_response__set_code(resp_p, code);

	cy_as_ll_request_response__set_word(resp_p, 0, length);
	cy_as_ll_request_response__pack(resp_p, 1, length, data);

	return cy_as_send_one(dev_p, resp_p);
}

static cy_bool
cy_as_ll_is_e_p_transfer_related_request(cy_as_ll_request_response *rqt_p,
	cy_as_end_point_number_t ep)
{
	uint16_t v;
	uint8_t  type = cy_as_ll_request_response__get_code(rqt_p);

	if (cy_as_ll_request_response__get_context(rqt_p) !=
		CY_RQT_USB_RQT_CONTEXT)
		return cy_false;

	/*
	 * when cancelling outstanding EP0 data transfers, any pending
	 * setup ACK requests also need to be cancelled.
	 */
	if ((ep == 0) && (type == CY_RQT_ACK_SETUP_PACKET))
		return cy_true;

	if (type != CY_RQT_USB_EP_DATA)
		return cy_false;

	v = cy_as_ll_request_response__get_word(rqt_p, 0);
	if ((cy_as_end_point_number_t)((v >> 13) & 1) != ep)
		return cy_false;

	return cy_true;
}

cy_as_return_status_t
cy_as_ll_remove_ep_data_requests(cy_as_device *dev_p,
	cy_as_end_point_number_t ep)
{
	cy_as_context *ctxt_p;
	cy_as_ll_request_list_node *node_p;
	uint32_t imask;

	/*
	* first, remove any queued requests
	*/
	ctxt_p = dev_p->context[CY_RQT_USB_RQT_CONTEXT];
	if (ctxt_p) {
		for (node_p = ctxt_p->request_queue_p; node_p;
			node_p = node_p->next) {
			if (cy_as_ll_is_e_p_transfer_related_request
			(node_p->rqt, ep)) {
				cy_as_ll_remove_request(dev_p, ctxt_p,
					node_p->rqt, cy_false);
				break;
			}
		}

		/*
		* now, deal with any request that may be in transit
		*/
		imask = cy_as_hal_disable_interrupts();

		if (ctxt_p->request_queue_p != 0 &&
			cy_as_ll_is_e_p_transfer_related_request
			(ctxt_p->request_queue_p->rqt, ep) &&
			cy_as_request_get_node_state(ctxt_p->request_queue_p) ==
			CY_AS_REQUEST_LIST_STATE_WAITING) {
			cy_as_hal_print_message("need to remove an in-transit "
				"request to antioch\n");

			/*
			* if the request has not been fully sent to west bridge
			* yet, abort sending. otherwise, terminate the request
			* with a CANCELED status. firmware will already have
			* terminated this transfer.
			*/
			if (dev_p->ll_sending_rqt)
				dev_p->ll_abort_curr_rqt = cy_true;
			else {
				uint32_t state;

				node_p = ctxt_p->request_queue_p;
				if (node_p->callback)
					node_p->callback(dev_p, ctxt_p->number,
						node_p->rqt, node_p->resp,
						CY_AS_ERROR_CANCELED);

				ctxt_p->request_queue_p = node_p->next;
				state = cy_as_hal_disable_interrupts();
				cy_as_hal_c_b_free(node_p);
				cy_as_hal_enable_interrupts(state);
			}
		}

		cy_as_hal_enable_interrupts(imask);
	}

	return CY_AS_ERROR_SUCCESS;
}
