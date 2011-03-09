/* Cypress West Bridge API header file (cyaslowlevel.h)
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
## Foundation, Inc., 51 Franklin Street
## Fifth Floor, Boston, MA  02110-1301, USA.
## ===========================
*/

#ifndef _INCLUDED_CYASLOWLEVEL_H_
#define _INCLUDED_CYASLOWLEVEL_H_

/*@@Low Level Communications

	Summary
	The low level communications module is responsible for
	communications between the West Bridge device and the P
	port processor.  Communications is organized as a series
	of requests and subsequent responses. For each request
	there is a one and only one response. Requests may go
	from the West Bridge device to the P port processor, or
	from the P Port processor to the West Bridge device.

	Description
	Requests are issued across what is called a context. A
	context is a single channel of communications from one
	processor to another processor. There can be only a single
	request outstanding on a context at a given time. Contexts
	are used to identify subsystems that can only process a
	single request at a time, but are independent of other
	contexts in the system. For instance, there is a context
	for communicating storage commands from the P port processor
	to the West Bridge device.  There is also a context for
	communicating USB commands from the P port processor to the
	West Bridge device.

	Requests and responses are identical with the exception of
	the type bit in the request/response header.  If the type
	bit is one, the packet is a request. If this bit is zero,
	the packet is a response. Also encoded within the header of
	the request/response is the code. The code is a command
	code for a request, or a response code for a response.  For
	a request, the code is a function of the context.  The code
	0 has one meaning for the storage context and a different
	meaning for the USB context.  The code is treated differently
	in the response. If the code in the response is less than 16,
	then the meaning of the response is global across all
	contexts. If the response is greater than or equal to 16,
	then the response is specific to the associated context.

	Requests and responses are transferred between processors
	through the mailbox registers.  It may take one or more cycles
	to transmit a complete request or response.  The context is
	encoded into each cycle of the transfer to insure the
	receiving processor can route the data to the appropriate
	context for processing. In this way, the traffic from multiple
	contexts can be multiplexed into a single data stream through
	the mailbox registers by the sending processor, and
	demultiplexed from the mailbox registers by the receiving
	processor.

	* Firmware Assumptions *
	The firmware assumes that mailbox contents will be consumed
	immediately. Therefore for multi-cycle packets, the data is
	sent in a tight polling loop from the firmware. This implies
	that the data must be read from the mailbox register on the P
	port side and processed immediately or performance of the
	firmware will suffer. In order to insure this is the case,
	the data from the mailboxes is read and stored immediately
	in a per context buffer. This occurs until the entire packet
	is received at which time the request packet is processed.
	Since the protocol is designed to allow for only one
	outstanding packet at a time, the firmware can never be in a
	position of waiting on the mailbox registers while the P port
	is processing a request.  Only after the response to the
	previous request is sent will another request be sent.
*/

#include "cyashal.h"
#include "cyasdevice.h"

#include "cyas_cplus_start.h"

/*
 * Constants
 */
#define CY_AS_REQUEST_RESPONSE_CODE_MASK (0x00ff)
#define CY_AS_REQUEST_RESPONSE_CONTEXT_MASK	(0x0F00)
#define CY_AS_REQUEST_RESPONSE_CONTEXT_SHIFT (8)
#define CY_AS_REQUEST_RESPONSE_TYPE_MASK (0x4000)
#define CY_AS_REQUEST_RESPONSE_LAST_MASK (0x8000)
#define CY_AS_REQUEST_RESPONSE_CLEAR_STR_FLAG (0x1000)

/*
 * These macros extract the data from a 16 bit value
 */
#define cy_as_mbox_get_code(c) \
	((uint8_t)((c) & CY_AS_REQUEST_RESPONSE_CODE_MASK))
#define cy_as_mbox_get_context(c) \
	((uint8_t)(((c) & CY_AS_REQUEST_RESPONSE_CONTEXT_MASK) \
		>> CY_AS_REQUEST_RESPONSE_CONTEXT_SHIFT))
#define cy_as_mbox_is_last(c) \
	((c) & CY_AS_REQUEST_RESPONSE_LAST_MASK)
#define cy_as_mbox_is_request(c) \
	(((c) & CY_AS_REQUEST_RESPONSE_TYPE_MASK) != 0)
#define cy_as_mbox_is_response(c) \
	(((c) & CY_AS_REQUEST_RESPONSE_TYPE_MASK) == 0)

/*
 * These macros (not yet written) pack data into or extract data
 * from the m_box0 field of the request or response
 */
#define cy_as_ll_request_response__set_code(req, code) \
		((req)->box0 = \
		((req)->box0 & ~CY_AS_REQUEST_RESPONSE_CODE_MASK) | \
			(code & CY_AS_REQUEST_RESPONSE_CODE_MASK))

#define cy_as_ll_request_response__get_code(req) \
	cy_as_mbox_get_code((req)->box0)

#define cy_as_ll_request_response__set_context(req, context) \
		((req)->box0 |= ((context) << \
			CY_AS_REQUEST_RESPONSE_CONTEXT_SHIFT))

#define cy_as_ll_request_response__set_clear_storage_flag(req) \
		((req)->box0 |= CY_AS_REQUEST_RESPONSE_CLEAR_STR_FLAG)

#define cy_as_ll_request_response__get_context(req) \
	cy_as_mbox_get_context((req)->box0)

#define cy_as_ll_request_response__is_last(req) \
	cy_as_mbox_is_last((req)->box0)

#define CY_an_ll_request_response___set_last(req) \
		((req)->box0 |= CY_AS_REQUEST_RESPONSE_LAST_MASK)

#define cy_as_ll_request_response__is_request(req) \
	cy_as_mbox_is_request((req)->box0)

#define cy_as_ll_request_response__set_request(req) \
		((req)->box0 |= CY_AS_REQUEST_RESPONSE_TYPE_MASK)

#define cy_as_ll_request_response__set_response(req) \
		((req)->box0 &= ~CY_AS_REQUEST_RESPONSE_TYPE_MASK)

#define cy_as_ll_request_response__is_response(req) \
	cy_as_mbox_is_response((req)->box0)

#define cy_as_ll_request_response__get_word(req, offset) \
	((req)->data[(offset)])

#define cy_as_ll_request_response__set_word(req, offset, \
	value) ((req)->data[(offset)] = value)

typedef enum cy_as_remove_request_result_t {
	cy_as_remove_request_sucessful,
	cy_as_remove_request_in_transit,
	cy_as_remove_request_not_found
} cy_as_remove_request_result_t;

/* Summary
   Start the low level communications module

   Description
*/
cy_as_return_status_t
cy_as_ll_start(
		cy_as_device *dev_p
		);

cy_as_return_status_t
cy_as_ll_stop(
   cy_as_device *dev_p
   );


cy_as_ll_request_response *
cy_as_ll_create_request(
		cy_as_device *dev_p,
		uint16_t code,
		uint8_t context,
		/* Length of the request in 16 bit words */
		uint16_t length
		);

void
cy_as_ll_init_request(
	cy_as_ll_request_response *req_p,
	uint16_t code,
	uint16_t context,
	uint16_t length);

void
cy_as_ll_init_response(
	cy_as_ll_request_response *req_p,
	uint16_t length);

void
cy_as_ll_destroy_request(
		cy_as_device *dev_p,
		cy_as_ll_request_response *);

cy_as_ll_request_response *
cy_as_ll_create_response(
		cy_as_device *dev_p,
		/* Length of the request in 16 bit words */
		uint16_t length
		);

cy_as_remove_request_result_t
cy_as_ll_remove_request(
		cy_as_device *dev_p,
		cy_as_context *ctxt_p,
		cy_as_ll_request_response *req_p,
		cy_bool force
		);
void
cy_as_ll_remove_all_requests(cy_as_device *dev_p,
	cy_as_context *ctxt_p);

void
cy_as_ll_destroy_response(
	cy_as_device *dev_p,
	cy_as_ll_request_response *);

cy_as_return_status_t
cy_as_ll_send_request(
	/* The West Bridge device */
	cy_as_device *dev_p,
	/* The request to send */
	cy_as_ll_request_response *req,
	/* Storage for a reply, must be sure it is of sufficient size */
	cy_as_ll_request_response *resp,
	/* If true, this is a sync request */
	cy_bool	sync,
	/* Callback to call when reply is received */
	cy_as_response_callback cb
);

cy_as_return_status_t
cy_as_ll_send_request_wait_reply(
	/* The West Bridge device */
	cy_as_device *dev_p,
	/* The request to send */
	cy_as_ll_request_response *req,
	/* Storage for a reply, must be sure it is of sufficient size */
	cy_as_ll_request_response *resp
);

/* Summary
   This function registers a callback function to be called when a
   request arrives on a given context.

   Description

   Returns
   * CY_AS_ERROR_SUCCESS
*/
extern cy_as_return_status_t
cy_as_ll_register_request_callback(
		cy_as_device *dev_p,
		uint8_t context,
		cy_as_response_callback cb
		);

/* Summary
   This function packs a set of bytes given by the data_p pointer
   into a request, reply structure.
*/
extern void
cy_as_ll_request_response__pack(
	/* The destintation request or response */
	cy_as_ll_request_response *req,
	/* The offset of where to pack the data */
	uint32_t offset,
	/* The length of the data to pack in bytes */
	uint32_t length,
	/* The data to pack */
	void *data_p
	);

/* Summary
   This function unpacks a set of bytes from a request/reply
   structure into a segment of memory given by the data_p pointer.
*/
extern void
cy_as_ll_request_response__unpack(
	/* The source of the data to unpack */
	cy_as_ll_request_response *req,
	/* The offset of the data to unpack */
	uint32_t offset,
	/* The length of the data to unpack in bytes */
	uint32_t length,
	/* The destination of the unpack operation */
	void *data_p
	);

/* Summary
   This function sends a status response back to the West Bridge
   device in response to a previously send request
*/
extern cy_as_return_status_t
cy_as_ll_send_status_response(
	 /* The West Bridge device */
	cy_as_device *dev_p,
	/* The context to send the response on */
	uint8_t context,
	/* The success/failure code to send */
	uint16_t code,
	/* Flag to clear wait on storage context */
	uint8_t clear_storage);

/* Summary
   This function sends a response back to the West Bridge device.

   Description
   This function sends a response back to the West Bridge device.
   The response is sent on the context given by the 'context'
   variable.  The code for the response is given by the 'code'
   argument.  The data for the response is given by the data and
   length arguments.
*/
extern cy_as_return_status_t
cy_as_ll_send_data_response(
	/* The West Bridge device */
	cy_as_device *dev_p,
	/* The context to send the response on */
	uint8_t context,
	/* The response code to use */
	uint16_t code,
	/* The length of the data for the response */
	uint16_t length,
	/* The data for the response */
	void *data
);

/* Summary
   This function removes any requests of the given type
   from the given context.

   Description
   This function removes requests of a given type from the
   context given via the context number.
*/
extern cy_as_return_status_t
cy_as_ll_remove_ep_data_requests(
	/* The West Bridge device */
	cy_as_device *dev_p,
	cy_as_end_point_number_t ep
	);

#include "cyas_cplus_end.h"

#endif				  /* _INCLUDED_CYASLOWLEVEL_H_ */
