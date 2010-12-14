/* Cypress West Bridge API source file (cyasusb.c)
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
#include "../../include/linux/westbridge/cyasusb.h"
#include "../../include/linux/westbridge/cyaserr.h"
#include "../../include/linux/westbridge/cyasdma.h"
#include "../../include/linux/westbridge/cyaslowlevel.h"
#include "../../include/linux/westbridge/cyaslep2pep.h"
#include "../../include/linux/westbridge/cyasregs.h"
#include "../../include/linux/westbridge/cyasstorage.h"

static cy_as_return_status_t
cy_as_usb_ack_setup_packet(
	/* Handle to the West Bridge device */
	cy_as_device_handle handle,
	/* The callback if async call */
	cy_as_function_callback	cb,
	/* Client supplied data */
	uint32_t client
	);

static void
cy_as_usb_func_callback(
			cy_as_device *dev_p,
			uint8_t context,
			cy_as_ll_request_response *rqt,
			cy_as_ll_request_response *resp,
			cy_as_return_status_t ret);
/*
* Reset the USB EP0 state
*/
static void
cy_as_usb_reset_e_p0_state(cy_as_device *dev_p)
{
	cy_as_log_debug_message(6, "cy_as_usb_reset_e_p0_state called");

	cy_as_device_clear_ack_delayed(dev_p);
	cy_as_device_clear_setup_packet(dev_p);
	if (cy_as_device_is_usb_async_pending(dev_p, 0))
		cy_as_usb_cancel_async((cy_as_device_handle)dev_p, 0);

	dev_p->usb_pending_buffer = 0;
}

/*
* External function to map logical endpoints to physical endpoints
*/
static cy_as_return_status_t
is_usb_active(cy_as_device *dev_p)
{
	if (!cy_as_device_is_configured(dev_p))
		return CY_AS_ERROR_NOT_CONFIGURED;

	if (!cy_as_device_is_firmware_loaded(dev_p))
		return CY_AS_ERROR_NO_FIRMWARE;

	if (dev_p->usb_count == 0)
		return CY_AS_ERROR_NOT_RUNNING;

	if (cy_as_device_is_in_suspend_mode(dev_p))
		return CY_AS_ERROR_IN_SUSPEND;

	return CY_AS_ERROR_SUCCESS;
}

static void
usb_ack_callback(cy_as_device_handle h,
			   cy_as_return_status_t status,
			   uint32_t client,
			   cy_as_funct_c_b_type  type,
			   void *data)
{
	cy_as_device *dev_p = (cy_as_device *)h;

	(void)client;
	(void)status;
	(void)data;

	cy_as_hal_assert(type == CY_FUNCT_CB_NODATA);

	if (dev_p->usb_pending_buffer) {
		cy_as_usb_io_callback cb;

		cb = dev_p->usb_cb[0];
		dev_p->usb_cb[0] = 0;
		cy_as_device_clear_usb_async_pending(dev_p, 0);
		if (cb)
			cb(h, 0, dev_p->usb_pending_size,
				dev_p->usb_pending_buffer, dev_p->usb_error);

		dev_p->usb_pending_buffer = 0;
	}

	cy_as_device_clear_setup_packet(dev_p);
}

static void
my_usb_request_callback_usb_event(cy_as_device *dev_p,
	cy_as_ll_request_response *req_p)
{
	uint16_t ev;
	uint16_t val;
	cy_as_device_handle h = (cy_as_device_handle)dev_p;

	ev = cy_as_ll_request_response__get_word(req_p, 0);
	switch (ev) {
	case 0:			 /* Reserved */
		cy_as_ll_send_status_response(dev_p, CY_RQT_USB_RQT_CONTEXT,
			CY_AS_ERROR_INVALID_REQUEST, 0);
		break;

	case 1:			 /* Reserved */
		cy_as_ll_send_status_response(dev_p, CY_RQT_USB_RQT_CONTEXT,
			CY_AS_ERROR_INVALID_REQUEST, 0);
		break;

	case 2:			 /* USB Suspend */
		dev_p->usb_last_event = cy_as_event_usb_suspend;
		if (dev_p->usb_event_cb_ms)
			dev_p->usb_event_cb_ms(h, cy_as_event_usb_suspend, 0);
		else if (dev_p->usb_event_cb)
			dev_p->usb_event_cb(h, cy_as_event_usb_suspend, 0);
		cy_as_ll_send_status_response(dev_p,
			CY_RQT_USB_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 0);
		break;

	case 3:			 /* USB Resume */
		dev_p->usb_last_event = cy_as_event_usb_resume;
		if (dev_p->usb_event_cb_ms)
			dev_p->usb_event_cb_ms(h, cy_as_event_usb_resume, 0);
		else if (dev_p->usb_event_cb)
			dev_p->usb_event_cb(h, cy_as_event_usb_resume, 0);
		cy_as_ll_send_status_response(dev_p,
			CY_RQT_USB_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 0);
		break;

	case 4:			 /* USB Reset */
		/*
		* if we get a USB reset, the USB host did not understand
		* our response or we timed out for some reason.  reset
		* our internal state to be ready for another set of
		* enumeration based requests.
		*/
		if (cy_as_device_is_ack_delayed(dev_p))
			cy_as_usb_reset_e_p0_state(dev_p);

		dev_p->usb_last_event = cy_as_event_usb_reset;
		if (dev_p->usb_event_cb_ms)
			dev_p->usb_event_cb_ms(h, cy_as_event_usb_reset, 0);
		else if (dev_p->usb_event_cb)
			dev_p->usb_event_cb(h, cy_as_event_usb_reset, 0);

		cy_as_ll_send_status_response(dev_p,
			CY_RQT_USB_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 0);
		cy_as_device_clear_usb_high_speed(dev_p);
		cy_as_usb_set_dma_sizes(dev_p);
		dev_p->usb_max_tx_size = 0x40;
		cy_as_dma_set_max_dma_size(dev_p, 0x06, 0x40);
		break;

	case 5:			 /* USB Set Configuration */
		/* The configuration to set */
		val = cy_as_ll_request_response__get_word(req_p, 1);
		dev_p->usb_last_event = cy_as_event_usb_set_config;
		if (dev_p->usb_event_cb_ms)
			dev_p->usb_event_cb_ms(h,
				cy_as_event_usb_set_config, &val);
		else if (dev_p->usb_event_cb)
			dev_p->usb_event_cb(h,
				cy_as_event_usb_set_config, &val);

		cy_as_ll_send_status_response(dev_p,
			CY_RQT_USB_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 0);
		break;

	case 6:			 /* USB Speed change */
		/* Connect speed */
		val = cy_as_ll_request_response__get_word(req_p, 1);
		dev_p->usb_last_event = cy_as_event_usb_speed_change;
		if (dev_p->usb_event_cb_ms)
			dev_p->usb_event_cb_ms(h,
				cy_as_event_usb_speed_change, &val);
		else if (dev_p->usb_event_cb)
			dev_p->usb_event_cb(h,
				cy_as_event_usb_speed_change, &val);

		cy_as_ll_send_status_response(dev_p,
			CY_RQT_USB_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 0);
		cy_as_device_set_usb_high_speed(dev_p);
		cy_as_usb_set_dma_sizes(dev_p);
		dev_p->usb_max_tx_size = 0x200;
		cy_as_dma_set_max_dma_size(dev_p, 0x06, 0x200);
		break;

	case 7:			 /* USB Clear Feature */
		/* EP Number */
		val = cy_as_ll_request_response__get_word(req_p, 1);
		if (dev_p->usb_event_cb_ms)
			dev_p->usb_event_cb_ms(h,
				cy_as_event_usb_clear_feature, &val);
		if (dev_p->usb_event_cb)
			dev_p->usb_event_cb(h,
				cy_as_event_usb_clear_feature, &val);

		cy_as_ll_send_status_response(dev_p,
			CY_RQT_USB_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 0);
		break;

	default:
		cy_as_hal_print_message("invalid event type\n");
		cy_as_ll_send_data_response(dev_p, CY_RQT_USB_RQT_CONTEXT,
			CY_RESP_USB_INVALID_EVENT, sizeof(ev), &ev);
		break;
	}
}

static void
my_usb_request_callback_usb_data(cy_as_device *dev_p,
	cy_as_ll_request_response *req_p)
{
	cy_as_end_point_number_t ep;
	uint8_t type;
	uint16_t len;
	uint16_t val;
	cy_as_device_handle h = (cy_as_device_handle)dev_p;

	val = cy_as_ll_request_response__get_word(req_p, 0);
	ep = (cy_as_end_point_number_t)((val >> 13) & 0x01);
	len = (val & 0x1ff);

	cy_as_hal_assert(len <= 64);
	cy_as_ll_request_response__unpack(req_p,
		1, len, dev_p->usb_ep_data);

	type = (uint8_t)((val >> 14) & 0x03);
	if (type == 0) {
		if (cy_as_device_is_ack_delayed(dev_p)) {
			/*
			* A setup packet has arrived while we are
			* processing a previous setup packet. reset
			* our state with respect to EP0 to be ready
			* to process the new packet.
			*/
			cy_as_usb_reset_e_p0_state(dev_p);
		}

		if (len != 8)
			cy_as_ll_send_status_response(dev_p,
				CY_RQT_USB_RQT_CONTEXT,
				CY_AS_ERROR_INVALID_REQUEST, 0);
		else {
			cy_as_device_clear_ep0_stalled(dev_p);
			cy_as_device_set_setup_packet(dev_p);
			cy_as_ll_send_status_response(dev_p,
				CY_RQT_USB_RQT_CONTEXT,
				CY_AS_ERROR_SUCCESS, 0);

			if (dev_p->usb_event_cb_ms)
				dev_p->usb_event_cb_ms(h,
					cy_as_event_usb_setup_packet,
					dev_p->usb_ep_data);
			else
				dev_p->usb_event_cb(h,
					cy_as_event_usb_setup_packet,
					dev_p->usb_ep_data);

			if ((!cy_as_device_is_ack_delayed(dev_p)) &&
				(!cy_as_device_is_ep0_stalled(dev_p)))
				cy_as_usb_ack_setup_packet(h,
					usb_ack_callback, 0);
		}
	} else if (type == 2) {
		if (len != 0)
			cy_as_ll_send_status_response(dev_p,
				CY_RQT_USB_RQT_CONTEXT,
				CY_AS_ERROR_INVALID_REQUEST, 0);
		else {
			if (dev_p->usb_event_cb_ms)
				dev_p->usb_event_cb_ms(h,
					cy_as_event_usb_status_packet, 0);
			else
				dev_p->usb_event_cb(h,
					cy_as_event_usb_status_packet, 0);

			cy_as_ll_send_status_response(dev_p,
				CY_RQT_USB_RQT_CONTEXT,
				CY_AS_ERROR_SUCCESS, 0);
		}
	} else if (type == 1) {
		/*
		* we need to hand the data associated with these
		* endpoints to the DMA module.
		*/
		cy_as_dma_received_data(dev_p, ep, len, dev_p->usb_ep_data);
		cy_as_ll_send_status_response(dev_p,
			CY_RQT_USB_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 0);
	}
}

static void
my_usb_request_callback_inquiry(cy_as_device *dev_p,
	cy_as_ll_request_response *req_p)
{
	cy_as_usb_inquiry_data_dep cbdata;
	cy_as_usb_inquiry_data cbdata_ms;
	void *data;
	uint16_t val;
	cy_as_device_handle h = (cy_as_device_handle)dev_p;
	uint8_t def_inq_data[64];
	uint8_t evpd;
	uint8_t codepage;
	cy_bool updated;
	uint16_t length;

	cy_as_bus_number_t bus;
	uint32_t		device;
	cy_as_media_type   media;

	val	= cy_as_ll_request_response__get_word(req_p, 0);
	bus	= cy_as_storage_get_bus_from_address(val);
	device = cy_as_storage_get_device_from_address(val);
	media  = cy_as_storage_get_media_from_address(val);

	val	  = cy_as_ll_request_response__get_word(req_p, 1);
	evpd	 = (uint8_t)((val >> 8) & 0x01);
	codepage = (uint8_t)(val & 0xff);

	length = cy_as_ll_request_response__get_word(req_p, 2);
	data   = (void *)def_inq_data;

	updated = cy_false;

	if (dev_p->usb_event_cb_ms) {
		cbdata_ms.bus = bus;
		cbdata_ms.device = device;
		cbdata_ms.updated = updated;
		cbdata_ms.evpd = evpd;
		cbdata_ms.codepage = codepage;
		cbdata_ms.length = length;
		cbdata_ms.data = data;

		cy_as_hal_assert(cbdata_ms.length <= sizeof(def_inq_data));
		cy_as_ll_request_response__unpack(req_p,
			3, cbdata_ms.length, cbdata_ms.data);

		dev_p->usb_event_cb_ms(h,
			cy_as_event_usb_inquiry_before, &cbdata_ms);

		updated = cbdata_ms.updated;
		data	= cbdata_ms.data;
		length  = cbdata_ms.length;
	} else if (dev_p->usb_event_cb) {
		cbdata.media = media;
		cbdata.updated = updated;
		cbdata.evpd = evpd;
		cbdata.codepage = codepage;
		cbdata.length = length;
		cbdata.data = data;

		cy_as_hal_assert(cbdata.length <=
			sizeof(def_inq_data));
		cy_as_ll_request_response__unpack(req_p, 3,
			cbdata.length, cbdata.data);

		dev_p->usb_event_cb(h,
			cy_as_event_usb_inquiry_before, &cbdata);

		updated = cbdata.updated;
		data	= cbdata.data;
		length  = cbdata.length;
	}

	if (updated && length > 192)
		cy_as_hal_print_message("an inquiry result from a "
			"cy_as_event_usb_inquiry_before event "
			"was greater than 192 bytes.");

	/* Now send the reply with the data back
	 * to the West Bridge device */
	if (updated && length <= 192) {
		/*
		* the callback function modified the inquiry
		* data, ship the data back to the west bridge firmware.
		*/
		cy_as_ll_send_data_response(dev_p,
			CY_RQT_USB_RQT_CONTEXT,
			CY_RESP_INQUIRY_DATA, length, data);
	} else {
		/*
		* the callback did not modify the data, just acknowledge
		* that we processed the request
		*/
		cy_as_ll_send_status_response(dev_p,
			CY_RQT_USB_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 1);
	}

	if (dev_p->usb_event_cb_ms)
		dev_p->usb_event_cb_ms(h,
			cy_as_event_usb_inquiry_after, &cbdata_ms);
	else if (dev_p->usb_event_cb)
		dev_p->usb_event_cb(h,
			cy_as_event_usb_inquiry_after, &cbdata);
}

static void
my_usb_request_callback_start_stop(cy_as_device *dev_p,
	cy_as_ll_request_response *req_p)
{
	cy_as_bus_number_t bus;
	cy_as_media_type media;
	uint32_t device;
	uint16_t val;

	if (dev_p->usb_event_cb_ms || dev_p->usb_event_cb) {
		cy_bool loej;
		cy_bool start;
		cy_as_device_handle h = (cy_as_device_handle)dev_p;

		val = cy_as_ll_request_response__get_word(req_p, 0);
		bus = cy_as_storage_get_bus_from_address(val);
		device = cy_as_storage_get_device_from_address(val);
		media = cy_as_storage_get_media_from_address(val);

		val = cy_as_ll_request_response__get_word(req_p, 1);
		loej = (val & 0x02) ? cy_true : cy_false;
		start = (val & 0x01) ? cy_true : cy_false;

		if (dev_p->usb_event_cb_ms) {
			cy_as_usb_start_stop_data cbdata_ms;

			cbdata_ms.bus = bus;
			cbdata_ms.device = device;
			cbdata_ms.loej = loej;
			cbdata_ms.start = start;
			dev_p->usb_event_cb_ms(h,
				cy_as_event_usb_start_stop, &cbdata_ms);

		} else if (dev_p->usb_event_cb) {
			cy_as_usb_start_stop_data_dep cbdata;

			cbdata.media = media;
			cbdata.loej = loej;
			cbdata.start = start;
			dev_p->usb_event_cb(h,
				cy_as_event_usb_start_stop, &cbdata);
		}
	}
	cy_as_ll_send_status_response(dev_p,
		CY_RQT_USB_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 1);
}

static void
my_usb_request_callback_uknown_c_b_w(cy_as_device *dev_p,
	cy_as_ll_request_response *req_p)
{
	uint16_t val;
	cy_as_device_handle h = (cy_as_device_handle)dev_p;
	uint8_t buf[16];

	uint8_t response[4];
	uint16_t reqlen;
	void *request;
	uint8_t status;
	uint8_t key;
	uint8_t asc;
	uint8_t ascq;

	val = cy_as_ll_request_response__get_word(req_p, 0);
	/* Failed by default */
	status = 1;
	/* Invalid command */
	key = 0x05;
	/* Invalid command */
	asc = 0x20;
	/* Invalid command */
	ascq = 0x00;
	reqlen = cy_as_ll_request_response__get_word(req_p, 1);
	request = buf;

	cy_as_hal_assert(reqlen <= sizeof(buf));
	cy_as_ll_request_response__unpack(req_p, 2, reqlen, request);

	if (dev_p->usb_event_cb_ms)  {
		cy_as_usb_unknown_command_data cbdata_ms;
		cbdata_ms.bus = cy_as_storage_get_bus_from_address(val);
		cbdata_ms.device =
			cy_as_storage_get_device_from_address(val);
		cbdata_ms.reqlen = reqlen;
		cbdata_ms.request = request;
		cbdata_ms.status = status;
		cbdata_ms.key = key;
		cbdata_ms.asc = asc;
		cbdata_ms.ascq = ascq;

		dev_p->usb_event_cb_ms(h,
			cy_as_event_usb_unknown_storage, &cbdata_ms);
		status = cbdata_ms.status;
		key = cbdata_ms.key;
		asc = cbdata_ms.asc;
		ascq = cbdata_ms.ascq;
	} else if (dev_p->usb_event_cb) {
		cy_as_usb_unknown_command_data_dep cbdata;
		cbdata.media =
			cy_as_storage_get_media_from_address(val);
		cbdata.reqlen = reqlen;
		cbdata.request = request;
		cbdata.status = status;
		cbdata.key = key;
		cbdata.asc = asc;
		cbdata.ascq = ascq;

		dev_p->usb_event_cb(h,
			cy_as_event_usb_unknown_storage, &cbdata);
		status = cbdata.status;
		key = cbdata.key;
		asc = cbdata.asc;
		ascq = cbdata.ascq;
	}

	response[0] = status;
	response[1] = key;
	response[2] = asc;
	response[3] = ascq;
	cy_as_ll_send_data_response(dev_p, CY_RQT_USB_RQT_CONTEXT,
		CY_RESP_UNKNOWN_SCSI_COMMAND, sizeof(response), response);
}

static void
my_usb_request_callback_m_s_c_progress(cy_as_device *dev_p,
	cy_as_ll_request_response *req_p)
{
	uint16_t val1, val2;
	cy_as_device_handle h = (cy_as_device_handle)dev_p;

	if ((dev_p->usb_event_cb) || (dev_p->usb_event_cb_ms)) {
		cy_as_m_s_c_progress_data cbdata;

		val1 = cy_as_ll_request_response__get_word(req_p, 0);
		val2 = cy_as_ll_request_response__get_word(req_p, 1);
		cbdata.wr_count = (uint32_t)((val1 << 16) | val2);

		val1 = cy_as_ll_request_response__get_word(req_p, 2);
		val2 = cy_as_ll_request_response__get_word(req_p, 3);
		cbdata.rd_count = (uint32_t)((val1 << 16) | val2);

		if (dev_p->usb_event_cb)
			dev_p->usb_event_cb(h,
				cy_as_event_usb_m_s_c_progress, &cbdata);
		else
			dev_p->usb_event_cb_ms(h,
				cy_as_event_usb_m_s_c_progress, &cbdata);
	}

	cy_as_ll_send_status_response(dev_p,
		CY_RQT_USB_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 0);
}

/*
* This function processes the requests delivered from the
* firmware within the West Bridge device that are delivered
* in the USB context.  These requests generally are EP0 and
* EP1 related requests or USB events.
*/
static void
my_usb_request_callback(cy_as_device *dev_p, uint8_t context,
	cy_as_ll_request_response *req_p,
	cy_as_ll_request_response *resp_p,
	cy_as_return_status_t ret)
{
	uint16_t val;
	uint8_t code = cy_as_ll_request_response__get_code(req_p);

	(void)resp_p;
	(void)context;
	(void)ret;

	switch (code) {
	case CY_RQT_USB_EVENT:
		my_usb_request_callback_usb_event(dev_p, req_p);
		break;

	case CY_RQT_USB_EP_DATA:
		dev_p->usb_last_event = cy_as_event_usb_setup_packet;
		my_usb_request_callback_usb_data(dev_p, req_p);
		break;

	case CY_RQT_SCSI_INQUIRY_COMMAND:
		dev_p->usb_last_event = cy_as_event_usb_inquiry_after;
		my_usb_request_callback_inquiry(dev_p, req_p);
		break;

	case CY_RQT_SCSI_START_STOP_COMMAND:
		dev_p->usb_last_event = cy_as_event_usb_start_stop;
		my_usb_request_callback_start_stop(dev_p, req_p);
		break;

	case CY_RQT_SCSI_UNKNOWN_COMMAND:
		dev_p->usb_last_event = cy_as_event_usb_unknown_storage;
		my_usb_request_callback_uknown_c_b_w(dev_p, req_p);
		break;

	case CY_RQT_USB_ACTIVITY_UPDATE:
		dev_p->usb_last_event = cy_as_event_usb_m_s_c_progress;
		my_usb_request_callback_m_s_c_progress(dev_p, req_p);
		break;

	default:
		cy_as_hal_print_message("invalid request "
			"received on USB context\n");
		val = req_p->box0;
		cy_as_ll_send_data_response(dev_p, CY_RQT_USB_RQT_CONTEXT,
			CY_RESP_INVALID_REQUEST, sizeof(val), &val);
		break;
	}
}

static cy_as_return_status_t
my_handle_response_usb_start(cy_as_device *dev_p,
			cy_as_ll_request_response *req_p,
			cy_as_ll_request_response *reply_p,
			cy_as_return_status_t ret)
{
	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	if (cy_as_ll_request_response__get_code(reply_p) !=
	CY_RESP_SUCCESS_FAILURE) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	ret = cy_as_ll_request_response__get_word(reply_p, 0);
	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	/*
	* mark EP 0 and EP1 as 64 byte endpoints
	*/
	cy_as_dma_set_max_dma_size(dev_p, 0, 64);
	cy_as_dma_set_max_dma_size(dev_p, 1, 64);

	dev_p->usb_count++;

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	if (ret != CY_AS_ERROR_SUCCESS) {
		cy_as_destroy_c_b_queue(dev_p->usb_func_cbs);
		cy_as_ll_register_request_callback(dev_p,
			CY_RQT_USB_RQT_CONTEXT, 0);
	}

	cy_as_device_clear_u_s_s_pending(dev_p);

	return ret;

}

/*
* This function starts the USB stack.  The stack is reference
* counted so if the stack is already started, this function
* just increments the count.  If the stack has not been started,
* a start request is sent to the West Bridge device.
*
* Note: Starting the USB stack does not cause the USB signals
* to be connected to the USB pins.  To do this and therefore
* initiate enumeration, CyAsUsbConnect() must be called.
*/
cy_as_return_status_t
cy_as_usb_start(cy_as_device_handle handle,
			   cy_as_function_callback cb,
			   uint32_t client)
{
	cy_as_ll_request_response *req_p, *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_start called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	if (!cy_as_device_is_configured(dev_p))
		return CY_AS_ERROR_NOT_CONFIGURED;

	if (!cy_as_device_is_firmware_loaded(dev_p))
		return CY_AS_ERROR_NO_FIRMWARE;

	if (cy_as_device_is_in_suspend_mode(dev_p))
		return CY_AS_ERROR_IN_SUSPEND;

	if (cy_as_device_is_in_callback(dev_p))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	if (cy_as_device_is_u_s_s_pending(dev_p))
		return CY_AS_ERROR_STARTSTOP_PENDING;

	cy_as_device_set_u_s_s_pending(dev_p);

	if (dev_p->usb_count == 0) {
		/*
		* since we are just starting the stack,
		* mark USB as not connected to the remote host
		*/
		cy_as_device_clear_usb_connected(dev_p);
		dev_p->usb_phy_config = 0;

		/* Queue for 1.0 Async Requests, kept for
		 * backwards compatibility */
		dev_p->usb_func_cbs = cy_as_create_c_b_queue(CYAS_USB_FUNC_CB);
		if (dev_p->usb_func_cbs == 0) {
			cy_as_device_clear_u_s_s_pending(dev_p);
			return CY_AS_ERROR_OUT_OF_MEMORY;
		}

		/* Reset the EP0 state */
		cy_as_usb_reset_e_p0_state(dev_p);

		/*
		* we register here becuase the start request may cause
		* events to occur before the response to the start request.
		*/
		cy_as_ll_register_request_callback(dev_p,
			CY_RQT_USB_RQT_CONTEXT, my_usb_request_callback);

		/* Create the request to send to the West Bridge device */
		req_p = cy_as_ll_create_request(dev_p,
			CY_RQT_START_USB, CY_RQT_USB_RQT_CONTEXT, 0);
		if (req_p == 0) {
			cy_as_destroy_c_b_queue(dev_p->usb_func_cbs);
			dev_p->usb_func_cbs = 0;
			cy_as_device_clear_u_s_s_pending(dev_p);
			return CY_AS_ERROR_OUT_OF_MEMORY;
		}

		/* Reserve space for the reply, the reply data
		 * will not exceed one word */
		reply_p = cy_as_ll_create_response(dev_p, 1);
		if (reply_p == 0) {
			cy_as_destroy_c_b_queue(dev_p->usb_func_cbs);
			dev_p->usb_func_cbs = 0;
			cy_as_ll_destroy_request(dev_p, req_p);
			cy_as_device_clear_u_s_s_pending(dev_p);
			return CY_AS_ERROR_OUT_OF_MEMORY;
		}

		if (cb == 0) {
			ret = cy_as_ll_send_request_wait_reply(dev_p,
				req_p, reply_p);
			if (ret != CY_AS_ERROR_SUCCESS)
				goto destroy;

			return my_handle_response_usb_start(dev_p,
				req_p, reply_p, ret);
		} else {
			ret = cy_as_misc_send_request(dev_p, cb,
				client, CY_FUNCT_CB_USB_START, 0,
				dev_p->func_cbs_usb,
				CY_AS_REQUEST_RESPONSE_EX, req_p, reply_p,
				cy_as_usb_func_callback);

			if (ret != CY_AS_ERROR_SUCCESS)
				goto destroy;

			return ret;
		}

destroy:
		cy_as_ll_destroy_request(dev_p, req_p);
		cy_as_ll_destroy_response(dev_p, reply_p);
	} else {
		dev_p->usb_count++;
		if (cb)
			cb(handle, ret, client, CY_FUNCT_CB_USB_START, 0);
	}

	cy_as_device_clear_u_s_s_pending(dev_p);

	return ret;
}

void
cy_as_usb_reset(cy_as_device *dev_p)
{
	int i;

	cy_as_device_clear_usb_connected(dev_p);

	for (i = 0; i < sizeof(dev_p->usb_config) /
		sizeof(dev_p->usb_config[0]); i++) {
		/*
		 * cancel all pending USB read/write operations, as it is
		 * possible that the USB stack comes up in a different
		 * configuration with a different set of endpoints.
		 */
		if (cy_as_device_is_usb_async_pending(dev_p, i))
			cy_as_usb_cancel_async(dev_p,
				(cy_as_end_point_number_t)i);

		dev_p->usb_cb[i] = 0;
		dev_p->usb_config[i].enabled = cy_false;
	}

	dev_p->usb_phy_config = 0;
}

/*
 * This function does all the API side clean-up associated
 * with CyAsUsbStop, without any communication with firmware.
 * This needs to be done when the device is being reset while
 * the USB stack is active.
 */
void
cy_as_usb_cleanup(cy_as_device *dev_p)
{
	if (dev_p->usb_count) {
		cy_as_usb_reset_e_p0_state(dev_p);
		cy_as_usb_reset(dev_p);
		cy_as_hal_mem_set(dev_p->usb_config, 0,
			sizeof(dev_p->usb_config));
		cy_as_destroy_c_b_queue(dev_p->usb_func_cbs);

		dev_p->usb_count = 0;
	}
}

static cy_as_return_status_t
my_handle_response_usb_stop(cy_as_device *dev_p,
					cy_as_ll_request_response *req_p,
					cy_as_ll_request_response *reply_p,
					cy_as_return_status_t ret)
{
	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	if (cy_as_ll_request_response__get_code(reply_p) !=
	CY_RESP_SUCCESS_FAILURE) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	ret = cy_as_ll_request_response__get_word(reply_p, 0);
	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	/*
	 * we sucessfully shutdown the stack, so
	 * decrement to make the count zero.
	 */
	cy_as_usb_cleanup(dev_p);

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	if (ret != CY_AS_ERROR_SUCCESS)
		cy_as_ll_register_request_callback(dev_p,
			CY_RQT_USB_RQT_CONTEXT, 0);

	cy_as_device_clear_u_s_s_pending(dev_p);

	return ret;
}

/*
* This function stops the USB stack. The USB stack is reference
* counted so first is reference count is decremented. If the
* reference count is then zero, a request is sent to the West
* Bridge device to stop the USB stack on the West Bridge device.
*/
cy_as_return_status_t
cy_as_usb_stop(cy_as_device_handle handle,
			  cy_as_function_callback cb,
			  uint32_t client)
{
	cy_as_ll_request_response *req_p = 0, *reply_p = 0;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_stop called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_usb_connected(dev_p))
		return CY_AS_ERROR_USB_CONNECTED;

	if (cy_as_device_is_in_callback(dev_p))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	if (cy_as_device_is_u_s_s_pending(dev_p))
		return CY_AS_ERROR_STARTSTOP_PENDING;

	cy_as_device_set_u_s_s_pending(dev_p);

	if (dev_p->usb_count == 1) {
		/* Create the request to send to the West Bridge device */
		req_p = cy_as_ll_create_request(dev_p, CY_RQT_STOP_USB,
			CY_RQT_USB_RQT_CONTEXT, 0);
		if (req_p == 0) {
			ret = CY_AS_ERROR_OUT_OF_MEMORY;
			goto destroy;
		}

		/* Reserve space for the reply, the reply data will not
		 * exceed one word */
		reply_p = cy_as_ll_create_response(dev_p, 1);
		if (reply_p == 0) {
			ret = CY_AS_ERROR_OUT_OF_MEMORY;
			goto destroy;
		}

		if (cb == 0) {
			ret = cy_as_ll_send_request_wait_reply(dev_p,
				req_p, reply_p);
			if (ret != CY_AS_ERROR_SUCCESS)
				goto destroy;

			return my_handle_response_usb_stop(dev_p,
				req_p, reply_p, ret);
		} else {
			ret = cy_as_misc_send_request(dev_p, cb, client,
				CY_FUNCT_CB_USB_STOP, 0, dev_p->func_cbs_usb,
				CY_AS_REQUEST_RESPONSE_EX, req_p, reply_p,
				cy_as_usb_func_callback);

			if (ret != CY_AS_ERROR_SUCCESS)
				goto destroy;

			return ret;
		}

destroy:
		cy_as_ll_destroy_request(dev_p, req_p);
		cy_as_ll_destroy_response(dev_p, reply_p);
	} else if (dev_p->usb_count > 1) {
		/*
		 * reset all LE_ps to inactive state, after cleaning
		 * up any pending async read/write calls.
		 */
		cy_as_usb_reset(dev_p);
		dev_p->usb_count--;

		if (cb)
			cb(handle, ret, client, CY_FUNCT_CB_USB_STOP, 0);
	}

	cy_as_device_clear_u_s_s_pending(dev_p);

	return ret;
}

/*
* This function registers a callback to be called when
* USB events are processed
*/
cy_as_return_status_t
cy_as_usb_register_callback(cy_as_device_handle handle,
	cy_as_usb_event_callback callback)
{
	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_register_callback called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	if (!cy_as_device_is_configured(dev_p))
		return CY_AS_ERROR_NOT_CONFIGURED;

	if (!cy_as_device_is_firmware_loaded(dev_p))
		return CY_AS_ERROR_NO_FIRMWARE;

	dev_p->usb_event_cb = NULL;
	dev_p->usb_event_cb_ms = callback;
	return CY_AS_ERROR_SUCCESS;
}


static cy_as_return_status_t
my_handle_response_no_data(cy_as_device *dev_p,
					cy_as_ll_request_response *req_p,
					cy_as_ll_request_response *reply_p)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	if (cy_as_ll_request_response__get_code(reply_p) !=
	CY_RESP_SUCCESS_FAILURE)
		ret = CY_AS_ERROR_INVALID_RESPONSE;
	else
		ret = cy_as_ll_request_response__get_word(reply_p, 0);

	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

static cy_as_return_status_t
my_handle_response_connect(cy_as_device *dev_p,
					   cy_as_ll_request_response *req_p,
					   cy_as_ll_request_response *reply_p,
					   cy_as_return_status_t ret)
{
	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	if (cy_as_ll_request_response__get_code(reply_p) !=
	CY_RESP_SUCCESS_FAILURE) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	ret = cy_as_ll_request_response__get_word(reply_p, 0);
	if (ret == CY_AS_ERROR_SUCCESS)
		cy_as_device_set_usb_connected(dev_p);

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}


/*
* This method asks the West Bridge device to connect the
* internal USB D+ and D- signals to the USB pins, thus
* starting the enumeration processes if the external pins
* are connnected to a USB host. If the external pins are
* not connect to a USB host, enumeration will begin as soon
* as the USB pins are connected to a host.
*/
cy_as_return_status_t
cy_as_usb_connect(cy_as_device_handle handle,
				 cy_as_function_callback cb,
				 uint32_t client)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_connect called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_in_callback(dev_p))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_SET_CONNECT_STATE, CY_RQT_USB_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/* 1 = Connect request */
	cy_as_ll_request_response__set_word(req_p, 0, 1);

	/* Reserve space for the reply, the reply
	 * data will not exceed one word */
	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return my_handle_response_connect(dev_p, req_p, reply_p, ret);
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_USB_CONNECT, 0, dev_p->func_cbs_usb,
			CY_AS_REQUEST_RESPONSE_EX, req_p, reply_p,
			cy_as_usb_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

static cy_as_return_status_t
my_handle_response_disconnect(cy_as_device *dev_p,
					   cy_as_ll_request_response *req_p,
					   cy_as_ll_request_response *reply_p,
					   cy_as_return_status_t ret)
{
	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	if (cy_as_ll_request_response__get_code(reply_p) !=
	CY_RESP_SUCCESS_FAILURE) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	ret = cy_as_ll_request_response__get_word(reply_p, 0);
	if (ret == CY_AS_ERROR_SUCCESS)
		cy_as_device_clear_usb_connected(dev_p);

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}
/*
* This method forces a disconnect of the D+ and D- pins
* external to the West Bridge device from the D+ and D-
* signals internally, effectively disconnecting the West
* Bridge device from any connected USB host.
*/
cy_as_return_status_t
cy_as_usb_disconnect(cy_as_device_handle handle,
					cy_as_function_callback cb,
					uint32_t client)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_disconnect called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_in_callback(dev_p))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	if (!cy_as_device_is_usb_connected(dev_p))
		return CY_AS_ERROR_USB_NOT_CONNECTED;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_SET_CONNECT_STATE, CY_RQT_USB_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	cy_as_ll_request_response__set_word(req_p, 0, 0);

	/* Reserve space for the reply, the reply
	 * data will not exceed two bytes */
	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return my_handle_response_disconnect(dev_p,
			req_p, reply_p, ret);
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_USB_DISCONNECT, 0, dev_p->func_cbs_usb,
			CY_AS_REQUEST_RESPONSE_EX, req_p, reply_p,
			cy_as_usb_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return ret;
	}
destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

static cy_as_return_status_t
my_handle_response_set_enum_config(cy_as_device *dev_p,
			cy_as_ll_request_response *req_p,
			cy_as_ll_request_response *reply_p)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	if (cy_as_ll_request_response__get_code(reply_p) !=
	CY_RESP_SUCCESS_FAILURE) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	ret = cy_as_ll_request_response__get_word(reply_p, 0);

	if (ret == CY_AS_ERROR_SUCCESS) {
		/*
		* we configured the west bridge device and
		* enumeration is going to happen on the P port
		* processor.  now we must enable endpoint zero
		*/
		cy_as_usb_end_point_config config;

		config.dir = cy_as_usb_in_out;
		config.type = cy_as_usb_control;
		config.enabled = cy_true;

		ret = cy_as_usb_set_end_point_config((cy_as_device_handle *)
			dev_p, 0, &config);
	}

destroy:
		cy_as_ll_destroy_request(dev_p, req_p);
		cy_as_ll_destroy_response(dev_p, reply_p);

		return ret;
}

/*
* This method sets how the USB is enumerated and should
* be called before the CyAsUsbConnect() is called.
*/
static cy_as_return_status_t
my_usb_set_enum_config(cy_as_device *dev_p,
					uint8_t bus_mask,
					uint8_t media_mask,
					cy_bool use_antioch_enumeration,
					uint8_t mass_storage_interface,
					uint8_t mtp_interface,
					cy_bool mass_storage_callbacks,
					cy_as_function_callback cb,
					uint32_t client)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	cy_as_log_debug_message(6, "cy_as_usb_set_enum_config called");

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_usb_connected(dev_p))
		return CY_AS_ERROR_USB_CONNECTED;

	if (cy_as_device_is_in_callback(dev_p))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	/* if we are using MTP firmware:  */
	if (dev_p->is_mtp_firmware == 1) {
		/* we cannot enumerate MSC */
		if (mass_storage_interface != 0)
			return CY_AS_ERROR_INVALID_CONFIGURATION;

		if (bus_mask == 0) {
			if (mtp_interface != 0)
				return CY_AS_ERROR_INVALID_CONFIGURATION;
		} else if (bus_mask == 2) {
			/* enable EP 1 as it will be used */
			cy_as_dma_enable_end_point(dev_p, 1, cy_true,
				cy_as_direction_in);
			dev_p->usb_config[1].enabled = cy_true;
			dev_p->usb_config[1].dir = cy_as_usb_in;
			dev_p->usb_config[1].type = cy_as_usb_int;
		} else {
			return CY_AS_ERROR_INVALID_CONFIGURATION;
		}
	/* if we are not using MTP firmware, we cannot enumerate MTP */
	} else if (mtp_interface != 0)
		return CY_AS_ERROR_INVALID_CONFIGURATION;

	/*
	* if we are not enumerating mass storage, we should
	* not be providing an interface number.
	*/
	if (bus_mask == 0 && mass_storage_interface != 0)
		return CY_AS_ERROR_INVALID_CONFIGURATION;

	/*
	* if we are going to use mtp_interface, bus mask must be 2.
	*/
	if (mtp_interface != 0 && bus_mask != 2)
		return CY_AS_ERROR_INVALID_CONFIGURATION;


	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_SET_USB_CONFIG, CY_RQT_USB_RQT_CONTEXT, 4);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/* Marshal the structure */
	cy_as_ll_request_response__set_word(req_p, 0,
		(uint16_t)((media_mask << 8) | bus_mask));
	cy_as_ll_request_response__set_word(req_p, 1,
		(uint16_t)use_antioch_enumeration);
	cy_as_ll_request_response__set_word(req_p, 2,
		dev_p->is_mtp_firmware ? mtp_interface :
			mass_storage_interface);
	cy_as_ll_request_response__set_word(req_p, 3,
		(uint16_t)mass_storage_callbacks);

	/* Reserve space for the reply, the reply
	 * data will not exceed one word */
	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {

		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return my_handle_response_set_enum_config(dev_p,
					req_p, reply_p);
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_USB_SETENUMCONFIG,  0, dev_p->func_cbs_usb,
			CY_AS_REQUEST_RESPONSE_EX, req_p, reply_p,
			cy_as_usb_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

/*
 * This method sets how the USB is enumerated and should
 * be called before the CyAsUsbConnect() is called.
 */
cy_as_return_status_t
cy_as_usb_set_enum_config(cy_as_device_handle handle,
					   cy_as_usb_enum_control *config_p,
					   cy_as_function_callback cb,
					   uint32_t client)
{
	cy_as_device *dev_p = (cy_as_device *)handle;
	uint8_t bus_mask, media_mask;
	uint32_t bus, device;
	cy_as_return_status_t ret;

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if ((cy_as_device_is_in_callback(dev_p))  && (cb != 0))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	/* Since we are mapping the media types to bus with NAND to 0
	 * and the rest to 1, and we are only allowing for enumerating
	 * all the devices on a bus we just scan the array for any
	 * positions where there a device is enabled and mark the bus
	 * to be enumerated.
	 */
	bus_mask   = 0;
	media_mask = 0;
	media_mask = 0;
	for (bus = 0; bus < CY_AS_MAX_BUSES; bus++) {
		for (device = 0; device < CY_AS_MAX_STORAGE_DEVICES; device++) {
			if (config_p->devices_to_enumerate[bus][device] ==
			cy_true) {
				bus_mask   |= (0x01 << bus);
				media_mask |= dev_p->media_supported[bus];
				media_mask |= dev_p->media_supported[bus];
			}
		}
	}

	return my_usb_set_enum_config(dev_p, bus_mask, media_mask,
			config_p->antioch_enumeration,
			config_p->mass_storage_interface,
			config_p->mtp_interface,
			config_p->mass_storage_callbacks,
			cb,
			client
		);
}


static cy_as_return_status_t
my_handle_response_get_enum_config(cy_as_device *dev_p,
			cy_as_ll_request_response *req_p,
			cy_as_ll_request_response *reply_p,
			void *config_p)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint16_t val;
	uint8_t bus_mask;
	uint32_t bus;

	if (cy_as_ll_request_response__get_code(reply_p) !=
	CY_RESP_USB_CONFIG) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	/* Marshal the reply */
	if (req_p->flags & CY_AS_REQUEST_RESPONSE_MS) {
		uint32_t device;
		cy_bool state;
		cy_as_usb_enum_control *ms_config_p =
			(cy_as_usb_enum_control *)config_p;

		bus_mask = (uint8_t)
			(cy_as_ll_request_response__get_word
			(reply_p, 0) & 0xFF);
		for (bus = 0; bus < CY_AS_MAX_BUSES; bus++)  {
			if (bus_mask & (1 << bus))
				state = cy_true;
			else
				state = cy_false;

			for (device = 0; device < CY_AS_MAX_STORAGE_DEVICES;
				device++)
				ms_config_p->devices_to_enumerate[bus][device]
					= state;
		}

		ms_config_p->antioch_enumeration =
			(cy_bool)cy_as_ll_request_response__get_word
				(reply_p, 1);

		val = cy_as_ll_request_response__get_word(reply_p, 2);
		if (dev_p->is_mtp_firmware) {
			ms_config_p->mass_storage_interface = 0;
			ms_config_p->mtp_interface = (uint8_t)(val & 0xFF);
		} else {
			ms_config_p->mass_storage_interface =
				(uint8_t)(val & 0xFF);
			ms_config_p->mtp_interface = 0;
		}
		ms_config_p->mass_storage_callbacks = (cy_bool)(val >> 8);

		/*
		* firmware returns an invalid interface number for mass storage,
		* if mass storage is not enabled. this needs to be converted to
		* zero to match the input configuration.
		*/
		if (bus_mask == 0) {
			if (dev_p->is_mtp_firmware)
				ms_config_p->mtp_interface = 0;
			else
				ms_config_p->mass_storage_interface = 0;
		}
	} else {
		cy_as_usb_enum_control_dep *ex_config_p =
			(cy_as_usb_enum_control_dep *)config_p;

		ex_config_p->enum_mass_storage = (uint8_t)
			((cy_as_ll_request_response__get_word
				(reply_p, 0) >> 8) & 0xFF);
		ex_config_p->antioch_enumeration = (cy_bool)
			cy_as_ll_request_response__get_word(reply_p, 1);

		val = cy_as_ll_request_response__get_word(reply_p, 2);
		ex_config_p->mass_storage_interface = (uint8_t)(val & 0xFF);
		ex_config_p->mass_storage_callbacks = (cy_bool)(val >> 8);

		/*
		* firmware returns an invalid interface number for mass
		* storage, if mass storage is not enabled. this needs to
		* be converted to zero to match the input configuration.
		*/
		if (ex_config_p->enum_mass_storage == 0)
			ex_config_p->mass_storage_interface = 0;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

/*
* This sets up the request for the enumerateion configuration
* information, based on if the request is from the old pre-1.2
* functions.
*/
static cy_as_return_status_t
my_usb_get_enum_config(cy_as_device_handle handle,
					uint16_t req_flags,
					void *config_p,
					cy_as_function_callback cb,
					uint32_t client)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_get_enum_config called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_in_callback(dev_p))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_GET_USB_CONFIG, CY_RQT_USB_RQT_CONTEXT, 0);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/* Reserve space for the reply, the reply data
	 * will not exceed two bytes */
	reply_p = cy_as_ll_create_response(dev_p, 3);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		/* we need to know the type of request to
		 * know how to manage the data */
		req_p->flags |= req_flags;
		return my_handle_response_get_enum_config(dev_p,
			req_p, reply_p, config_p);
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_USB_GETENUMCONFIG, config_p,
			dev_p->func_cbs_usb, req_flags, req_p, reply_p,
			cy_as_usb_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

/*
 * This method returns the enumerateion configuration information
 * from the West Bridge device. Generally this is not used by
 * client software but is provided mostly for debug information.
 * We want a method to read all state information from the device.
 */
cy_as_return_status_t
cy_as_usb_get_enum_config(cy_as_device_handle handle,
					   cy_as_usb_enum_control *config_p,
					   cy_as_function_callback cb,
					   uint32_t client)
{
	return my_usb_get_enum_config(handle,
		CY_AS_REQUEST_RESPONSE_MS, config_p, cb, client);
}


/*
* This method sets the USB descriptor for a given entity.
*/
cy_as_return_status_t
cy_as_usb_set_descriptor(cy_as_device_handle handle,
					   cy_as_usb_desc_type type,
					   uint8_t index,
					   void *desc_p,
					   uint16_t length,
					   cy_as_function_callback cb,
					   uint32_t client)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint16_t pktlen;

	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_set_descriptor called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_in_callback(dev_p))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	if (length > CY_AS_MAX_USB_DESCRIPTOR_SIZE)
		return CY_AS_ERROR_INVALID_DESCRIPTOR;

	pktlen = (uint16_t)length / 2;
	if (length % 2)
		pktlen++;
	pktlen += 2; /* 1 for type, 1 for length */

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p, CY_RQT_SET_DESCRIPTOR,
		CY_RQT_USB_RQT_CONTEXT, (uint16_t)pktlen);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	cy_as_ll_request_response__set_word(req_p, 0,
		(uint16_t)((uint8_t)type | (index << 8)));
	cy_as_ll_request_response__set_word(req_p, 1,
		(uint16_t)length);
	cy_as_ll_request_response__pack(req_p, 2, length, desc_p);

	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return my_handle_response_no_data(dev_p, req_p, reply_p);
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_USB_SETDESCRIPTOR, 0, dev_p->func_cbs_usb,
			CY_AS_REQUEST_RESPONSE_EX, req_p, reply_p,
			cy_as_usb_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

/*
 * This method clears all descriptors that were previously
 * stored on the West Bridge through CyAsUsbSetDescriptor calls.
 */
cy_as_return_status_t
cy_as_usb_clear_descriptors(cy_as_device_handle handle,
					   cy_as_function_callback cb,
					   uint32_t client)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_clear_descriptors called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if ((cy_as_device_is_in_callback(dev_p)) && (cb == 0))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_CLEAR_DESCRIPTORS, CY_RQT_USB_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {

		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return my_handle_response_no_data(dev_p, req_p, reply_p);
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_USB_CLEARDESCRIPTORS, 0,
			dev_p->func_cbs_usb,
			CY_AS_REQUEST_RESPONSE_EX, req_p, reply_p,
			cy_as_usb_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

static cy_as_return_status_t
my_handle_response_get_descriptor(cy_as_device *dev_p,
					cy_as_ll_request_response *req_p,
					cy_as_ll_request_response *reply_p,
					cy_as_get_descriptor_data *data)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	 uint32_t retlen;

	if (cy_as_ll_request_response__get_code(reply_p) ==
	CY_RESP_SUCCESS_FAILURE) {
		ret = cy_as_ll_request_response__get_word(reply_p, 0);
		goto destroy;
	} else if (cy_as_ll_request_response__get_code(reply_p) !=
	CY_RESP_USB_DESCRIPTOR) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	retlen = cy_as_ll_request_response__get_word(reply_p, 0);
	if (retlen > data->length) {
		ret = CY_AS_ERROR_INVALID_SIZE;
		goto destroy;
	}

	ret = CY_AS_ERROR_SUCCESS;
	cy_as_ll_request_response__unpack(reply_p, 1,
		retlen, data->desc_p);

destroy:
		cy_as_ll_destroy_request(dev_p, req_p);
		cy_as_ll_destroy_response(dev_p, reply_p);

		return ret;
}

/*
* This method retreives the USB descriptor for a given type.
*/
cy_as_return_status_t
cy_as_usb_get_descriptor(cy_as_device_handle handle,
					   cy_as_usb_desc_type type,
					   uint8_t index,
					   cy_as_get_descriptor_data *data,
					   cy_as_function_callback cb,
					   uint32_t client)
{
	cy_as_return_status_t ret;
	cy_as_ll_request_response *req_p , *reply_p;

	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_get_descriptor called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_in_callback(dev_p))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_GET_DESCRIPTOR, CY_RQT_USB_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	cy_as_ll_request_response__set_word(req_p, 0,
		(uint16_t)((uint8_t)type | (index << 8)));

	/* Add one for the length field */
	reply_p = cy_as_ll_create_response(dev_p,
		CY_AS_MAX_USB_DESCRIPTOR_SIZE + 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(
				dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return my_handle_response_get_descriptor(dev_p,
			req_p, reply_p, data);
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_USB_GETDESCRIPTOR, data,
			dev_p->func_cbs_usb,
			CY_AS_REQUEST_RESPONSE_EX, req_p,
			reply_p,  cy_as_usb_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

cy_as_return_status_t
cy_as_usb_set_physical_configuration(cy_as_device_handle handle,
	uint8_t config)
{
	cy_as_return_status_t ret;
	cy_as_device *dev_p;

	cy_as_log_debug_message(6,
		"cy_as_usb_set_physical_configuration called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_usb_connected(dev_p))
		return CY_AS_ERROR_USB_CONNECTED;

	if (config < 1 || config > 12)
		return CY_AS_ERROR_INVALID_CONFIGURATION;

	dev_p->usb_phy_config = config;

	return CY_AS_ERROR_SUCCESS;
}

static cy_bool
is_physical_valid(uint8_t config, cy_as_end_point_number_t ep)
{
	static uint8_t validmask[12] = {
		0x0f,	   /* Config  1 - 1, 2, 3, 4 */
		0x07,	   /* Config  2 - 1, 2, 3 */
		0x07,	   /* Config  3 - 1, 2, 3 */
		0x0d,	   /* Config  4 - 1, 3, 4 */
		0x05,	   /* Config  5 - 1, 3 */
		0x05,	   /* Config  6 - 1, 3 */
		0x0d,	   /* Config  7 - 1, 3, 4 */
		0x05,	   /* Config  8 - 1, 3 */
		0x05,	   /* Config  9 - 1, 3 */
		0x0d,	   /* Config 10 - 1, 3, 4 */
		0x09,	   /* Config 11 - 1, 4 */
		0x01		/* Config 12 - 1 */
	};

	return (validmask[config - 1] & (1 << (ep - 1))) ? cy_true : cy_false;
}

/*
* This method sets the configuration for an endpoint
*/
cy_as_return_status_t
cy_as_usb_set_end_point_config(cy_as_device_handle handle,
	cy_as_end_point_number_t ep, cy_as_usb_end_point_config *config_p)
{
	cy_as_return_status_t ret;
	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_set_end_point_config called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_usb_connected(dev_p))
		return CY_AS_ERROR_USB_CONNECTED;

	if (ep >= 16 || ep == 2 || ep == 4 || ep == 6 || ep == 8)
		return CY_AS_ERROR_INVALID_ENDPOINT;

	if (ep == 0) {
		/* Endpoint 0 must be 64 byte, dir IN/OUT,
		 * and control type */
		if (config_p->dir != cy_as_usb_in_out ||
			config_p->type != cy_as_usb_control)
			return CY_AS_ERROR_INVALID_CONFIGURATION;
	} else if (ep == 1) {
		if ((dev_p->is_mtp_firmware == 1) &&
		(dev_p->usb_config[1].enabled == cy_true)) {
			return CY_AS_ERROR_INVALID_ENDPOINT;
		}

		/*
		 * EP1 can only be used either as an OUT ep, or as an IN ep.
		 */
		if ((config_p->type == cy_as_usb_control) ||
			(config_p->type == cy_as_usb_iso) ||
			(config_p->dir == cy_as_usb_in_out))
			return CY_AS_ERROR_INVALID_CONFIGURATION;
	} else {
		if (config_p->dir == cy_as_usb_in_out ||
			config_p->type == cy_as_usb_control)
			return CY_AS_ERROR_INVALID_CONFIGURATION;

		if (!is_physical_valid(dev_p->usb_phy_config,
			config_p->physical))
			return CY_AS_ERROR_INVALID_PHYSICAL_ENDPOINT;

		/*
		* ISO endpoints must be on E_ps 3, 5, 7 or 9 as
		* they need to align directly with the underlying
		* physical endpoint.
		*/
		if (config_p->type == cy_as_usb_iso) {
			if (ep != 3 && ep != 5 && ep != 7 && ep != 9)
				return CY_AS_ERROR_INVALID_CONFIGURATION;

			if (ep == 3 && config_p->physical != 1)
				return CY_AS_ERROR_INVALID_CONFIGURATION;

			if (ep == 5 && config_p->physical != 2)
				return CY_AS_ERROR_INVALID_CONFIGURATION;

			if (ep == 7 && config_p->physical != 3)
				return CY_AS_ERROR_INVALID_CONFIGURATION;

			if (ep == 9 && config_p->physical != 4)
				return CY_AS_ERROR_INVALID_CONFIGURATION;
		}
	}

	/* Store the configuration information until a
	 * CyAsUsbCommitConfig is done */
	dev_p->usb_config[ep] = *config_p;

	/* If the endpoint is enabled, enable DMA associated
	 * with the endpoint */
	/*
	* we make some assumptions that we check here.  we assume
	* that the direction fields for the DMA module are the same
	* values as the direction values for the USB module.
	*/
	cy_as_hal_assert((int)cy_as_usb_in == (int)cy_as_direction_in);
	cy_as_hal_assert((int)cy_as_usb_out == (int)cy_as_direction_out);
	cy_as_hal_assert((int)cy_as_usb_in_out == (int)cy_as_direction_in_out);

	return cy_as_dma_enable_end_point(dev_p, ep,
		config_p->enabled, (cy_as_dma_direction)config_p->dir);
}

cy_as_return_status_t
cy_as_usb_get_end_point_config(cy_as_device_handle handle,
	cy_as_end_point_number_t ep, cy_as_usb_end_point_config *config_p)
{
	cy_as_return_status_t ret;

	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_get_end_point_config called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (ep >= 16 || ep == 2 || ep == 4 || ep == 6 || ep == 8)
		return CY_AS_ERROR_INVALID_ENDPOINT;

	*config_p = dev_p->usb_config[ep];

	return CY_AS_ERROR_SUCCESS;
}

/*
* Commit the configuration of the various endpoints to the hardware.
*/
cy_as_return_status_t
cy_as_usb_commit_config(cy_as_device_handle handle,
					  cy_as_function_callback cb,
					  uint32_t client)
{
	uint32_t i;
	cy_as_return_status_t ret;
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_device *dev_p;
	uint16_t data;

	cy_as_log_debug_message(6, "cy_as_usb_commit_config called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_usb_connected(dev_p))
		return CY_AS_ERROR_USB_CONNECTED;

	if (cy_as_device_is_in_callback(dev_p))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	/*
	* this performs the mapping based on informatation that was
	* previously stored on the device about the various endpoints
	* and how they are configured. the output of this mapping is
	* setting the the 14 register values contained in usb_lepcfg
	* and usb_pepcfg
	*/
	ret = cy_as_usb_map_logical2_physical(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	/*
	* now, package the information about the various logical and
	* physical endpoint configuration registers and send it
	* across to the west bridge device.
	*/
	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_SET_USB_CONFIG_REGISTERS, CY_RQT_USB_RQT_CONTEXT, 8);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	cy_as_hal_print_message("USB configuration: %d\n",
		dev_p->usb_phy_config);
	cy_as_hal_print_message("EP1OUT: 0x%02x EP1IN: 0x%02x\n",
		dev_p->usb_ep1cfg[0], dev_p->usb_ep1cfg[1]);
	cy_as_hal_print_message("PEP registers: 0x%02x 0x%02x 0x%02x 0x%02x\n",
		dev_p->usb_pepcfg[0], dev_p->usb_pepcfg[1],
		dev_p->usb_pepcfg[2], dev_p->usb_pepcfg[3]);

	cy_as_hal_print_message("LEP registers: 0x%02x 0x%02x 0x%02x "
		"0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x 0x%02x\n",
		dev_p->usb_lepcfg[0], dev_p->usb_lepcfg[1],
		dev_p->usb_lepcfg[2], dev_p->usb_lepcfg[3],
		dev_p->usb_lepcfg[4], dev_p->usb_lepcfg[5],
		dev_p->usb_lepcfg[6], dev_p->usb_lepcfg[7],
		dev_p->usb_lepcfg[8], dev_p->usb_lepcfg[9]);

	/* Write the EP1OUTCFG and EP1INCFG data in the first word. */
	data = (uint16_t)((dev_p->usb_ep1cfg[0] << 8) |
		dev_p->usb_ep1cfg[1]);
	cy_as_ll_request_response__set_word(req_p, 0, data);

	/* Write the PEP CFG data in the next 2 words */
	for (i = 0; i < 4; i += 2) {
		data = (uint16_t)((dev_p->usb_pepcfg[i] << 8) |
			dev_p->usb_pepcfg[i + 1]);
		cy_as_ll_request_response__set_word(req_p,
			1 + i / 2, data);
	}

	/* Write the LEP CFG data in the next 5 words */
	for (i = 0; i < 10; i += 2) {
		data = (uint16_t)((dev_p->usb_lepcfg[i] << 8) |
			dev_p->usb_lepcfg[i + 1]);
		cy_as_ll_request_response__set_word(req_p,
			3 + i / 2, data);
	}

	/* A single status word response type */
	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p,
			req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		ret = my_handle_response_no_data(dev_p,
			req_p, reply_p);

		if (ret == CY_AS_ERROR_SUCCESS)
			ret = cy_as_usb_setup_dma(dev_p);

		return ret;
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_USB_COMMITCONFIG, 0, dev_p->func_cbs_usb,
			CY_AS_REQUEST_RESPONSE_EX, req_p, reply_p,
			cy_as_usb_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

static void
sync_request_callback(cy_as_device *dev_p,
	cy_as_end_point_number_t ep, void *buf_p,
	uint32_t size, cy_as_return_status_t err)
{
	(void)ep;
	(void)buf_p;

	dev_p->usb_error = err;
	dev_p->usb_actual_cnt = size;
}

static void
async_read_request_callback(cy_as_device *dev_p,
	cy_as_end_point_number_t ep, void *buf_p,
	uint32_t size, cy_as_return_status_t err)
{
	cy_as_device_handle h;

	cy_as_log_debug_message(6,
		"async_read_request_callback called");

	h = (cy_as_device_handle)dev_p;

	if (ep == 0 && cy_as_device_is_ack_delayed(dev_p)) {
		dev_p->usb_pending_buffer = buf_p;
		dev_p->usb_pending_size = size;
		dev_p->usb_error = err;
		cy_as_usb_ack_setup_packet(h, usb_ack_callback, 0);
	} else {
		cy_as_usb_io_callback cb;

		cb = dev_p->usb_cb[ep];
		dev_p->usb_cb[ep] = 0;
		cy_as_device_clear_usb_async_pending(dev_p, ep);
		if (cb)
			cb(h, ep, size, buf_p, err);
	}
}

static void
async_write_request_callback(cy_as_device *dev_p,
	cy_as_end_point_number_t ep, void *buf_p,
	uint32_t size, cy_as_return_status_t err)
{
	cy_as_device_handle h;

	cy_as_log_debug_message(6,
		"async_write_request_callback called");

	h = (cy_as_device_handle)dev_p;

	if (ep == 0 && cy_as_device_is_ack_delayed(dev_p)) {
		dev_p->usb_pending_buffer = buf_p;
		dev_p->usb_pending_size = size;
		dev_p->usb_error = err;

		/* The west bridge protocol generates ZLPs as required. */
		cy_as_usb_ack_setup_packet(h, usb_ack_callback, 0);
	} else {
		cy_as_usb_io_callback cb;

		cb = dev_p->usb_cb[ep];
		dev_p->usb_cb[ep] = 0;

		cy_as_device_clear_usb_async_pending(dev_p, ep);
		if (cb)
			cb(h, ep, size, buf_p, err);
	}
}

static void
my_turbo_rqt_callback(cy_as_device *dev_p,
					uint8_t context,
					cy_as_ll_request_response *rqt,
					cy_as_ll_request_response *resp,
					cy_as_return_status_t stat)
{
	uint8_t code;

	(void)context;
	(void)stat;

	/* The Handlers are responsible for Deleting the rqt and resp when
	 * they are finished
	 */
	code = cy_as_ll_request_response__get_code(rqt);
	switch (code) {
	case CY_RQT_TURBO_SWITCH_ENDPOINT:
		cy_as_hal_assert(stat == CY_AS_ERROR_SUCCESS);
		cy_as_ll_destroy_request(dev_p, rqt);
		cy_as_ll_destroy_response(dev_p, resp);
		break;
	default:
		cy_as_hal_assert(cy_false);
		break;
	}
}

/* Send a mailbox request to prepare the endpoint for switching */
static cy_as_return_status_t
my_send_turbo_switch(cy_as_device *dev_p, uint32_t size, cy_bool pktread)
{
	cy_as_return_status_t ret;
	cy_as_ll_request_response *req_p , *reply_p;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_TURBO_SWITCH_ENDPOINT, CY_RQT_TUR_RQT_CONTEXT, 3);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/* Reserve space for the reply, the reply data will
	 * not exceed one word */
	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	cy_as_ll_request_response__set_word(req_p, 0,
		(uint16_t)pktread);
	cy_as_ll_request_response__set_word(req_p, 1,
		(uint16_t)((size >> 16) & 0xFFFF));
	cy_as_ll_request_response__set_word(req_p, 2,
		(uint16_t)(size & 0xFFFF));

	ret = cy_as_ll_send_request(dev_p, req_p,
		reply_p, cy_false, my_turbo_rqt_callback);
	if (ret != CY_AS_ERROR_SUCCESS) {
		cy_as_ll_destroy_request(dev_p, req_p);
		cy_as_ll_destroy_request(dev_p, reply_p);
		return ret;
	}

	return CY_AS_ERROR_SUCCESS;
}

cy_as_return_status_t
cy_as_usb_read_data(cy_as_device_handle handle,
	cy_as_end_point_number_t ep, cy_bool pktread,
	uint32_t dsize, uint32_t *dataread, void *data)
{
	cy_as_return_status_t ret;
	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_read_data called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_in_callback(dev_p))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	if (ep >= 16 || ep == 4 || ep == 6 || ep == 8)
		return CY_AS_ERROR_INVALID_ENDPOINT;

	/* EP2 is available for reading when MTP is active */
	if (dev_p->mtp_count == 0 && ep == CY_AS_MTP_READ_ENDPOINT)
		return CY_AS_ERROR_INVALID_ENDPOINT;

	/* If the endpoint is disabled, we cannot
	 * write data to the endpoint */
	if (!dev_p->usb_config[ep].enabled)
		return CY_AS_ERROR_ENDPOINT_DISABLED;

	if (dev_p->usb_config[ep].dir != cy_as_usb_out)
		return CY_AS_ERROR_USB_BAD_DIRECTION;

	ret = cy_as_dma_queue_request(dev_p, ep, data, dsize,
		pktread, cy_true, sync_request_callback);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (ep == CY_AS_MTP_READ_ENDPOINT)  {
		ret = my_send_turbo_switch(dev_p, dsize, pktread);
		if (ret != CY_AS_ERROR_SUCCESS) {
			cy_as_dma_cancel(dev_p, ep, ret);
			return ret;
		}

		ret = cy_as_dma_drain_queue(dev_p, ep, cy_false);
		if (ret != CY_AS_ERROR_SUCCESS)
			return ret;
	} else {
		ret = cy_as_dma_drain_queue(dev_p, ep, cy_true);
		if (ret != CY_AS_ERROR_SUCCESS)
			return ret;
	}

	ret = dev_p->usb_error;
	*dataread = dev_p->usb_actual_cnt;

	return ret;
}

cy_as_return_status_t
cy_as_usb_read_data_async(cy_as_device_handle handle,
	cy_as_end_point_number_t ep, cy_bool pktread,
	uint32_t dsize, void *data, cy_as_usb_io_callback cb)
{
	cy_as_return_status_t ret;
	uint32_t mask;
	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_read_data_async called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (ep >= 16 || ep == 4 || ep == 6 || ep == 8)
		return CY_AS_ERROR_INVALID_ENDPOINT;

	/* EP2 is available for reading when MTP is active */
	if (dev_p->mtp_count == 0 && ep == CY_AS_MTP_READ_ENDPOINT)
		return CY_AS_ERROR_INVALID_ENDPOINT;

	/* If the endpoint is disabled, we cannot
	 * write data to the endpoint */
	if (!dev_p->usb_config[ep].enabled)
		return CY_AS_ERROR_ENDPOINT_DISABLED;

	if (dev_p->usb_config[ep].dir != cy_as_usb_out &&
		dev_p->usb_config[ep].dir != cy_as_usb_in_out)
		return CY_AS_ERROR_USB_BAD_DIRECTION;

	/*
	* since async operations can be triggered by interrupt
	* code, we must insure that we do not get multiple async
	* operations going at one time and protect this test and
	* set operation from interrupts.
	*/
	mask = cy_as_hal_disable_interrupts();
	if (cy_as_device_is_usb_async_pending(dev_p, ep)) {
		cy_as_hal_enable_interrupts(mask);
		return CY_AS_ERROR_ASYNC_PENDING;
	}
	cy_as_device_set_usb_async_pending(dev_p, ep);

	/*
	* if this is for EP0, we set this bit to delay the
	* ACK response until after this read has completed.
	*/
	if (ep == 0)
		cy_as_device_set_ack_delayed(dev_p);

	cy_as_hal_enable_interrupts(mask);

	cy_as_hal_assert(dev_p->usb_cb[ep] == 0);
	dev_p->usb_cb[ep] = cb;

	ret = cy_as_dma_queue_request(dev_p, ep, data, dsize,
		pktread, cy_true, async_read_request_callback);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (ep == CY_AS_MTP_READ_ENDPOINT)  {
		ret = my_send_turbo_switch(dev_p, dsize, pktread);
		if (ret != CY_AS_ERROR_SUCCESS) {
			cy_as_dma_cancel(dev_p, ep, ret);
			return ret;
		}
	} else {
		/* Kick start the queue if it is not running */
		cy_as_dma_kick_start(dev_p, ep);
	}
	return ret;
}

cy_as_return_status_t
cy_as_usb_write_data(cy_as_device_handle handle,
	cy_as_end_point_number_t ep, uint32_t dsize, void *data)
{
	cy_as_return_status_t ret;
	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_write_data called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_in_callback(dev_p))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	if (ep >= 16 || ep == 2 || ep == 4 || ep == 8)
		return CY_AS_ERROR_INVALID_ENDPOINT;

	/* EP6 is available for writing when MTP is active */
	if (dev_p->mtp_count == 0 && ep == CY_AS_MTP_WRITE_ENDPOINT)
		return CY_AS_ERROR_INVALID_ENDPOINT;

	/* If the endpoint is disabled, we cannot
	 * write data to the endpoint */
	if (!dev_p->usb_config[ep].enabled)
		return CY_AS_ERROR_ENDPOINT_DISABLED;

	if (dev_p->usb_config[ep].dir != cy_as_usb_in &&
		dev_p->usb_config[ep].dir != cy_as_usb_in_out)
		return CY_AS_ERROR_USB_BAD_DIRECTION;

	/* Write on Turbo endpoint */
	if (ep == CY_AS_MTP_WRITE_ENDPOINT) {
		cy_as_ll_request_response *req_p, *reply_p;

		req_p = cy_as_ll_create_request(dev_p,
			CY_RQT_TURBO_SEND_RESP_DATA_TO_HOST,
			CY_RQT_TUR_RQT_CONTEXT, 3);
		if (req_p == 0)
			return CY_AS_ERROR_OUT_OF_MEMORY;

		cy_as_ll_request_response__set_word(req_p,
			0, 0x0006); /* EP number to use. */
		cy_as_ll_request_response__set_word(req_p,
			1, (uint16_t)((dsize >> 16) & 0xFFFF));
		cy_as_ll_request_response__set_word(req_p,
			2, (uint16_t)(dsize & 0xFFFF));

		/* Reserve space for the reply, the reply data
		 * will not exceed one word */
		reply_p = cy_as_ll_create_response(dev_p, 1);
		if (reply_p == 0) {
			cy_as_ll_destroy_request(dev_p, req_p);
			return CY_AS_ERROR_OUT_OF_MEMORY;
		}

		if (dsize) {
			ret = cy_as_dma_queue_request(dev_p,
				ep, data, dsize, cy_false,
				cy_false, sync_request_callback);
			if (ret != CY_AS_ERROR_SUCCESS)
				return ret;
		}

		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret == CY_AS_ERROR_SUCCESS) {
			if (cy_as_ll_request_response__get_code(reply_p) !=
			CY_RESP_SUCCESS_FAILURE)
				ret = CY_AS_ERROR_INVALID_RESPONSE;
			else
				ret = cy_as_ll_request_response__get_word
						(reply_p, 0);
		}

		cy_as_ll_destroy_request(dev_p, req_p);
		cy_as_ll_destroy_response(dev_p, reply_p);

		if (ret != CY_AS_ERROR_SUCCESS) {
			if (dsize)
				cy_as_dma_cancel(dev_p, ep, ret);
			return ret;
		}

		/* If this is a zero-byte write, firmware will
		 * handle it. there is no need to do any work here.
		 */
		if (!dsize)
			return CY_AS_ERROR_SUCCESS;
	} else {
		ret = cy_as_dma_queue_request(dev_p, ep, data, dsize,
			cy_false, cy_false, sync_request_callback);
		if (ret != CY_AS_ERROR_SUCCESS)
			return ret;
	}

	if (ep != CY_AS_MTP_WRITE_ENDPOINT)
		ret = cy_as_dma_drain_queue(dev_p, ep, cy_true);
	else
		ret = cy_as_dma_drain_queue(dev_p, ep, cy_false);

	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	ret = dev_p->usb_error;
	return ret;
}

static void
mtp_write_callback(
		cy_as_device *dev_p,
		uint8_t context,
		cy_as_ll_request_response *rqt,
		cy_as_ll_request_response *resp,
		cy_as_return_status_t ret)
{
	cy_as_usb_io_callback cb;
	cy_as_device_handle h = (cy_as_device_handle)dev_p;

	cy_as_hal_assert(context == CY_RQT_TUR_RQT_CONTEXT);

	if (ret == CY_AS_ERROR_SUCCESS) {
		if (cy_as_ll_request_response__get_code(resp) !=
		CY_RESP_SUCCESS_FAILURE)
			ret = CY_AS_ERROR_INVALID_RESPONSE;
		else
			ret = cy_as_ll_request_response__get_word(resp, 0);
	}

	/* If this was a zero byte transfer request, we can
	 * call the callback from here. */
	if ((cy_as_ll_request_response__get_word(rqt, 1) == 0) &&
			(cy_as_ll_request_response__get_word(rqt, 2) == 0)) {
		cb = dev_p->usb_cb[CY_AS_MTP_WRITE_ENDPOINT];
		dev_p->usb_cb[CY_AS_MTP_WRITE_ENDPOINT] = 0;
		cy_as_device_clear_usb_async_pending(dev_p,
			CY_AS_MTP_WRITE_ENDPOINT);
		if (cb)
			cb(h, CY_AS_MTP_WRITE_ENDPOINT, 0, 0, ret);

		goto destroy;
	}

	if (ret != CY_AS_ERROR_SUCCESS) {
		/* Firmware failed the request. Cancel the DMA transfer. */
		cy_as_dma_cancel(dev_p, 0x06, CY_AS_ERROR_CANCELED);
		dev_p->usb_cb[0x06] = 0;
		cy_as_device_clear_usb_async_pending(dev_p, 0x06);
	}

destroy:
	cy_as_ll_destroy_response(dev_p, resp);
	cy_as_ll_destroy_request(dev_p, rqt);
}

cy_as_return_status_t
cy_as_usb_write_data_async(cy_as_device_handle handle,
	cy_as_end_point_number_t ep, uint32_t dsize, void *data,
	cy_bool spacket, cy_as_usb_io_callback cb)
{
	uint32_t mask;
	cy_as_return_status_t ret;
	cy_as_device *dev_p;

	cy_as_log_debug_message(6, "cy_as_usb_write_data_async called");

	dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (ep >= 16 || ep == 2 || ep == 4 || ep == 8)
		return CY_AS_ERROR_INVALID_ENDPOINT;

	 /* EP6 is available for writing when MTP is active */
	if (dev_p->mtp_count == 0 && ep == CY_AS_MTP_WRITE_ENDPOINT)
		return CY_AS_ERROR_INVALID_ENDPOINT;

	/* If the endpoint is disabled, we cannot
	 * write data to the endpoint */
	if (!dev_p->usb_config[ep].enabled)
		return CY_AS_ERROR_ENDPOINT_DISABLED;

	if (dev_p->usb_config[ep].dir != cy_as_usb_in &&
		dev_p->usb_config[ep].dir != cy_as_usb_in_out)
		return CY_AS_ERROR_USB_BAD_DIRECTION;

	/*
	* since async operations can be triggered by interrupt
	* code, we must insure that we do not get multiple
	* async operations going at one time and
	* protect this test and set operation from interrupts.
	*/
	mask = cy_as_hal_disable_interrupts();
	if (cy_as_device_is_usb_async_pending(dev_p, ep)) {
		cy_as_hal_enable_interrupts(mask);
		return CY_AS_ERROR_ASYNC_PENDING;
	}

	cy_as_device_set_usb_async_pending(dev_p, ep);

	if (ep == 0)
		cy_as_device_set_ack_delayed(dev_p);

	cy_as_hal_enable_interrupts(mask);

	cy_as_hal_assert(dev_p->usb_cb[ep] == 0);
	dev_p->usb_cb[ep] = cb;
	dev_p->usb_spacket[ep] = spacket;

	/* Write on Turbo endpoint */
	if (ep == CY_AS_MTP_WRITE_ENDPOINT) {
		cy_as_ll_request_response *req_p, *reply_p;

		req_p = cy_as_ll_create_request(dev_p,
			CY_RQT_TURBO_SEND_RESP_DATA_TO_HOST,
			CY_RQT_TUR_RQT_CONTEXT, 3);

		if (req_p == 0)
			return CY_AS_ERROR_OUT_OF_MEMORY;

		cy_as_ll_request_response__set_word(req_p, 0,
			0x0006); /* EP number to use. */
		cy_as_ll_request_response__set_word(req_p, 1,
			(uint16_t)((dsize >> 16) & 0xFFFF));
		cy_as_ll_request_response__set_word(req_p, 2,
			(uint16_t)(dsize & 0xFFFF));

		/* Reserve space for the reply, the reply data
		 * will not exceed one word */
		reply_p = cy_as_ll_create_response(dev_p, 1);
		if (reply_p == 0) {
			cy_as_ll_destroy_request(dev_p, req_p);
			return CY_AS_ERROR_OUT_OF_MEMORY;
		}

		if (dsize) {
			ret = cy_as_dma_queue_request(dev_p, ep, data,
				dsize, cy_false, cy_false,
				async_write_request_callback);
			if (ret != CY_AS_ERROR_SUCCESS)
				return ret;
		}

		ret = cy_as_ll_send_request(dev_p, req_p, reply_p,
			cy_false, mtp_write_callback);
		if (ret != CY_AS_ERROR_SUCCESS) {
			if (dsize)
				cy_as_dma_cancel(dev_p, ep, ret);
			return ret;
		}

		/* Firmware will handle a zero byte transfer
		 * without any DMA transfers. */
		if (!dsize)
			return CY_AS_ERROR_SUCCESS;
	} else {
		ret = cy_as_dma_queue_request(dev_p, ep, data, dsize,
			cy_false, cy_false, async_write_request_callback);
		if (ret != CY_AS_ERROR_SUCCESS)
			return ret;
	}

	/* Kick start the queue if it is not running */
	if (ep != CY_AS_MTP_WRITE_ENDPOINT)
		cy_as_dma_kick_start(dev_p, ep);

	return CY_AS_ERROR_SUCCESS;
}

static void
my_usb_cancel_async_callback(
				   cy_as_device *dev_p,
				   uint8_t context,
				   cy_as_ll_request_response *rqt,
				   cy_as_ll_request_response *resp,
				   cy_as_return_status_t ret)
{
	uint8_t ep;
	(void)context;

	ep = (uint8_t)cy_as_ll_request_response__get_word(rqt, 0);
	if (ret == CY_AS_ERROR_SUCCESS) {
		if (cy_as_ll_request_response__get_code(resp) !=
			CY_RESP_SUCCESS_FAILURE)
			ret = CY_AS_ERROR_INVALID_RESPONSE;
		else
			ret = cy_as_ll_request_response__get_word(resp, 0);
	}

	cy_as_ll_destroy_request(dev_p, rqt);
	cy_as_ll_destroy_response(dev_p, resp);

	if (ret == CY_AS_ERROR_SUCCESS) {
		cy_as_dma_cancel(dev_p, ep, CY_AS_ERROR_CANCELED);
		dev_p->usb_cb[ep] = 0;
		cy_as_device_clear_usb_async_pending(dev_p, ep);
	}
}

cy_as_return_status_t
cy_as_usb_cancel_async(cy_as_device_handle handle,
	cy_as_end_point_number_t ep)
{
	cy_as_return_status_t ret;
	cy_as_ll_request_response *req_p, *reply_p;

	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ep &= 0x7F;		 /* Remove the direction bit. */
	if (!cy_as_device_is_usb_async_pending(dev_p, ep))
		return CY_AS_ERROR_ASYNC_NOT_PENDING;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_in_suspend_mode(dev_p))
		return CY_AS_ERROR_IN_SUSPEND;

	if ((ep == CY_AS_MTP_WRITE_ENDPOINT) ||
		(ep == CY_AS_MTP_READ_ENDPOINT)) {
		/* Need firmware support for the cancel operation. */
		req_p = cy_as_ll_create_request(dev_p,
			CY_RQT_CANCEL_ASYNC_TRANSFER,
			CY_RQT_TUR_RQT_CONTEXT, 1);

		if (req_p == 0)
			return CY_AS_ERROR_OUT_OF_MEMORY;

		reply_p = cy_as_ll_create_response(dev_p, 1);
		if (reply_p == 0) {
			cy_as_ll_destroy_request(dev_p, req_p);
			return CY_AS_ERROR_OUT_OF_MEMORY;
		}

		cy_as_ll_request_response__set_word(req_p, 0,
			(uint16_t)ep);

		ret = cy_as_ll_send_request(dev_p, req_p, reply_p,
			cy_false, my_usb_cancel_async_callback);

		if (ret != CY_AS_ERROR_SUCCESS) {
			cy_as_ll_destroy_request(dev_p, req_p);
			cy_as_ll_destroy_response(dev_p, reply_p);
			return ret;
		}
	} else {
		ret = cy_as_dma_cancel(dev_p, ep, CY_AS_ERROR_CANCELED);
		if (ret != CY_AS_ERROR_SUCCESS)
			return ret;

		dev_p->usb_cb[ep] = 0;
		cy_as_device_clear_usb_async_pending(dev_p, ep);
	}

	return CY_AS_ERROR_SUCCESS;
}

static void
cy_as_usb_ack_callback(
				   cy_as_device *dev_p,
				   uint8_t context,
				   cy_as_ll_request_response *rqt,
				   cy_as_ll_request_response *resp,
				   cy_as_return_status_t ret)
{
	cy_as_func_c_b_node *node  = (cy_as_func_c_b_node *)
		dev_p->func_cbs_usb->head_p;

	(void)context;

	if (ret == CY_AS_ERROR_SUCCESS) {
		if (cy_as_ll_request_response__get_code(resp) !=
		CY_RESP_SUCCESS_FAILURE)
			ret = CY_AS_ERROR_INVALID_RESPONSE;
		else
			ret = cy_as_ll_request_response__get_word(resp, 0);
	}

	node->cb_p((cy_as_device_handle)dev_p, ret,
		node->client_data, node->data_type, node->data);
	cy_as_remove_c_b_node(dev_p->func_cbs_usb);

	cy_as_ll_destroy_request(dev_p, rqt);
	cy_as_ll_destroy_response(dev_p, resp);
	cy_as_device_clear_ack_delayed(dev_p);
}

static cy_as_return_status_t
cy_as_usb_ack_setup_packet(cy_as_device_handle handle,
					  cy_as_function_callback	  cb,
					  uint32_t client)
{
	cy_as_return_status_t ret;
	cy_as_ll_request_response *req_p;
	cy_as_ll_request_response *reply_p;
	cy_as_func_c_b_node *cbnode;

	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_in_callback(dev_p) && cb == 0)
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	cy_as_hal_assert(cb != 0);

	cbnode = cy_as_create_func_c_b_node(cb, client);
	if (cbnode == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	req_p = cy_as_ll_create_request(dev_p, 0,
		CY_RQT_USB_RQT_CONTEXT, 2);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	cy_as_ll_init_request(req_p, CY_RQT_ACK_SETUP_PACKET,
		CY_RQT_USB_RQT_CONTEXT, 1);
	cy_as_ll_init_response(reply_p, 1);

	req_p->flags |= CY_AS_REQUEST_RESPONSE_EX;

	cy_as_insert_c_b_node(dev_p->func_cbs_usb, cbnode);

	ret = cy_as_ll_send_request(dev_p, req_p, reply_p,
		cy_false, cy_as_usb_ack_callback);

	return ret;
}

/*
 * Flush all data in logical EP that is being NAK-ed or
 * Stall-ed, so that this does not continue to block data
 * on other LEPs that use the same physical EP.
 */
static void
cy_as_usb_flush_logical_e_p(
		cy_as_device *dev_p,
		uint16_t	ep)
{
	uint16_t addr, val, count;

	addr = CY_AS_MEM_P0_EP2_DMA_REG + ep - 2;
	val  = cy_as_hal_read_register(dev_p->tag, addr);

	while (val) {
		count = ((val & 0xFFF) + 1) / 2;
		while (count--)
			val = cy_as_hal_read_register(dev_p->tag, ep);

		cy_as_hal_write_register(dev_p->tag, addr, 0);
		val = cy_as_hal_read_register(dev_p->tag, addr);
	}
}

static cy_as_return_status_t
cy_as_usb_nak_stall_request(cy_as_device_handle handle,
					   cy_as_end_point_number_t ep,
					   uint16_t request,
					   cy_bool state,
					   cy_as_usb_function_callback cb,
					   cy_as_function_callback fcb,
					   uint32_t client)
{
	cy_as_return_status_t ret;
	cy_as_ll_request_response *req_p , *reply_p;
	uint16_t data;

	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	if (cb)
		cy_as_hal_assert(fcb == 0);
	if (fcb)
		cy_as_hal_assert(cb == 0);

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_in_callback(dev_p) && cb == 0 && fcb == 0)
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	req_p = cy_as_ll_create_request(dev_p,
		request, CY_RQT_USB_RQT_CONTEXT, 2);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/* A single status word response type */
	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	/* Set the endpoint */
	data = (uint8_t)ep;
	cy_as_ll_request_response__set_word(req_p, 0, data);

	/* Set stall state to stalled */
	cy_as_ll_request_response__set_word(req_p, 1, (uint8_t)state);

	if (cb || fcb) {
		void *cbnode;
		cy_as_c_b_queue *queue;
		if (cb) {
			cbnode = cy_as_create_usb_func_c_b_node(cb, client);
			queue = dev_p->usb_func_cbs;
		} else {
			cbnode = cy_as_create_func_c_b_node(fcb, client);
			queue = dev_p->func_cbs_usb;
			req_p->flags |= CY_AS_REQUEST_RESPONSE_EX;
		}

		if (cbnode == 0) {
			ret = CY_AS_ERROR_OUT_OF_MEMORY;
			goto destroy;
		} else
			cy_as_insert_c_b_node(queue, cbnode);


		if (cy_as_device_is_setup_packet(dev_p)) {
			/* No Ack is needed on a stall request on EP0 */
			if ((state == cy_true) && (ep == 0)) {
				cy_as_device_set_ep0_stalled(dev_p);
			} else {
				cy_as_device_set_ack_delayed(dev_p);
				req_p->flags |=
					CY_AS_REQUEST_RESPONSE_DELAY_ACK;
			}
		}

		ret = cy_as_ll_send_request(dev_p, req_p,
			reply_p, cy_false, cy_as_usb_func_callback);
		if (ret != CY_AS_ERROR_SUCCESS) {
			if (req_p->flags & CY_AS_REQUEST_RESPONSE_DELAY_ACK)
				cy_as_device_rem_ack_delayed(dev_p);
			cy_as_remove_c_b_tail_node(queue);

			goto destroy;
		}
	} else {
		ret = cy_as_ll_send_request_wait_reply(dev_p,
			req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		if (cy_as_ll_request_response__get_code(reply_p) !=
		CY_RESP_SUCCESS_FAILURE) {
			ret = CY_AS_ERROR_INVALID_RESPONSE;
			goto destroy;
		}

		ret = cy_as_ll_request_response__get_word(reply_p, 0);

		if ((ret == CY_AS_ERROR_SUCCESS) &&
			(request == CY_RQT_STALL_ENDPOINT)) {
			if ((ep > 1) && (state != 0) &&
				(dev_p->usb_config[ep].dir == cy_as_usb_out))
				cy_as_usb_flush_logical_e_p(dev_p, ep);
		}

destroy:
		cy_as_ll_destroy_request(dev_p, req_p);
		cy_as_ll_destroy_response(dev_p, reply_p);
	}

	return ret;
}

static cy_as_return_status_t
my_handle_response_get_stall(cy_as_device *dev_p,
				cy_as_ll_request_response *req_p,
				cy_as_ll_request_response *reply_p,
				cy_bool *state_p)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint8_t code = cy_as_ll_request_response__get_code(reply_p);

	if (code == CY_RESP_SUCCESS_FAILURE) {
		ret = cy_as_ll_request_response__get_word(reply_p, 0);
		goto destroy;
	} else if (code != CY_RESP_ENDPOINT_STALL) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	*state_p = (cy_bool)cy_as_ll_request_response__get_word(reply_p, 0);
	ret = CY_AS_ERROR_SUCCESS;


destroy:
		cy_as_ll_destroy_request(dev_p, req_p);
		cy_as_ll_destroy_response(dev_p, reply_p);

		return ret;
}

static cy_as_return_status_t
my_handle_response_get_nak(cy_as_device *dev_p,
					   cy_as_ll_request_response *req_p,
					   cy_as_ll_request_response *reply_p,
					   cy_bool *state_p)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint8_t code = cy_as_ll_request_response__get_code(reply_p);

	if (code == CY_RESP_SUCCESS_FAILURE) {
		ret = cy_as_ll_request_response__get_word(reply_p, 0);
		goto destroy;
	} else if (code != CY_RESP_ENDPOINT_NAK) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	*state_p = (cy_bool)cy_as_ll_request_response__get_word(reply_p, 0);
	ret = CY_AS_ERROR_SUCCESS;


destroy:
		cy_as_ll_destroy_request(dev_p, req_p);
		cy_as_ll_destroy_response(dev_p, reply_p);

		return ret;
}

static cy_as_return_status_t
cy_as_usb_get_nak_stall(cy_as_device_handle handle,
				   cy_as_end_point_number_t ep,
				   uint16_t request,
				   uint16_t response,
				   cy_bool *state_p,
				   cy_as_function_callback cb,
				   uint32_t client)
{
	cy_as_return_status_t ret;
	cy_as_ll_request_response *req_p , *reply_p;
	uint16_t data;

	cy_as_device *dev_p = (cy_as_device *)handle;

	(void)response;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_in_callback(dev_p) && !cb)
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	req_p = cy_as_ll_create_request(dev_p, request,
		CY_RQT_USB_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/* Set the endpoint */
	data = (uint8_t)ep;
	cy_as_ll_request_response__set_word(req_p, 0, (uint16_t)ep);

	/* A single status word response type */
	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p,
			req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		if (request == CY_RQT_GET_STALL)
			return my_handle_response_get_stall(dev_p,
				req_p, reply_p, state_p);
		else
			return my_handle_response_get_nak(dev_p,
				req_p, reply_p, state_p);

	} else {
		cy_as_funct_c_b_type type;

		if (request == CY_RQT_GET_STALL)
			type = CY_FUNCT_CB_USB_GETSTALL;
		else
			type = CY_FUNCT_CB_USB_GETNAK;

		ret = cy_as_misc_send_request(dev_p, cb, client, type,
			state_p, dev_p->func_cbs_usb, CY_AS_REQUEST_RESPONSE_EX,
			req_p, reply_p, cy_as_usb_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

cy_as_return_status_t
cy_as_usb_set_nak(cy_as_device_handle handle,
				cy_as_end_point_number_t ep,
				cy_as_function_callback cb,
				uint32_t client)
{
	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	/*
	* we send the firmware the EP# with the appropriate direction
	* bit, regardless of what the user gave us.
	*/
	ep &= 0x0f;
	if (dev_p->usb_config[ep].dir == cy_as_usb_in)
		ep |= 0x80;

		if (dev_p->mtp_count > 0)
				return CY_AS_ERROR_NOT_VALID_IN_MTP;

	return cy_as_usb_nak_stall_request(handle, ep,
		CY_RQT_ENDPOINT_SET_NAK, cy_true, 0, cb, client);
}


cy_as_return_status_t
cy_as_usb_clear_nak(cy_as_device_handle handle,
				  cy_as_end_point_number_t ep,
				  cy_as_function_callback cb,
				  uint32_t client)
{
	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	/*
	* we send the firmware the EP# with the appropriate
	* direction bit, regardless of what the user gave us.
	*/
	ep &= 0x0f;
	if (dev_p->usb_config[ep].dir == cy_as_usb_in)
		ep |= 0x80;

		if (dev_p->mtp_count > 0)
				return CY_AS_ERROR_NOT_VALID_IN_MTP;

	return cy_as_usb_nak_stall_request(handle, ep,
		CY_RQT_ENDPOINT_SET_NAK, cy_false, 0, cb, client);
}

cy_as_return_status_t
cy_as_usb_get_nak(cy_as_device_handle handle,
				cy_as_end_point_number_t ep,
				cy_bool *nak_p,
				cy_as_function_callback cb,
				uint32_t client)
{
	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	/*
	* we send the firmware the EP# with the appropriate
	* direction bit, regardless of what the user gave us.
	*/
	ep &= 0x0f;
	if (dev_p->usb_config[ep].dir == cy_as_usb_in)
		ep |= 0x80;

		if (dev_p->mtp_count > 0)
				return CY_AS_ERROR_NOT_VALID_IN_MTP;

	return cy_as_usb_get_nak_stall(handle, ep,
		CY_RQT_GET_ENDPOINT_NAK, CY_RESP_ENDPOINT_NAK,
		nak_p, cb, client);
}


cy_as_return_status_t
cy_as_usb_set_stall(cy_as_device_handle handle,
				  cy_as_end_point_number_t ep,
				  cy_as_function_callback cb,
				  uint32_t client)
{
	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	/*
	* we send the firmware the EP# with the appropriate
	* direction bit, regardless of what the user gave us.
	*/
	ep &= 0x0f;
	if (dev_p->usb_config[ep].dir == cy_as_usb_in)
		ep |= 0x80;

	if (dev_p->mtp_turbo_active)
		return CY_AS_ERROR_NOT_VALID_DURING_MTP;

	return cy_as_usb_nak_stall_request(handle, ep,
		CY_RQT_STALL_ENDPOINT, cy_true, 0, cb, client);
}

cy_as_return_status_t
cy_as_usb_clear_stall(cy_as_device_handle handle,
					cy_as_end_point_number_t ep,
					cy_as_function_callback cb,
					uint32_t client)
{
	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	/*
	* we send the firmware the EP# with the appropriate
	* direction bit, regardless of what the user gave us.
	*/
	ep &= 0x0f;
	if (dev_p->usb_config[ep].dir == cy_as_usb_in)
		ep |= 0x80;

	if (dev_p->mtp_turbo_active)
		return CY_AS_ERROR_NOT_VALID_DURING_MTP;

	return cy_as_usb_nak_stall_request(handle, ep,
		CY_RQT_STALL_ENDPOINT, cy_false, 0, cb, client);
}

cy_as_return_status_t
cy_as_usb_get_stall(cy_as_device_handle handle,
				  cy_as_end_point_number_t ep,
				  cy_bool *stall_p,
				  cy_as_function_callback cb,
				  uint32_t client)
{
	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	/*
	* we send the firmware the EP# with the appropriate
	* direction bit, regardless of what the user gave us.
	*/
	ep &= 0x0f;
	if (dev_p->usb_config[ep].dir == cy_as_usb_in)
		ep |= 0x80;

	if (dev_p->mtp_turbo_active)
		return CY_AS_ERROR_NOT_VALID_DURING_MTP;

	return cy_as_usb_get_nak_stall(handle, ep,
		CY_RQT_GET_STALL, CY_RESP_ENDPOINT_STALL, stall_p, cb, client);
}

cy_as_return_status_t
cy_as_usb_signal_remote_wakeup(cy_as_device_handle handle,
		cy_as_function_callback cb,
		uint32_t client)
{
	cy_as_return_status_t ret;
	cy_as_ll_request_response *req_p , *reply_p;

	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_in_callback(dev_p))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	if (dev_p->usb_last_event != cy_as_event_usb_suspend)
		return CY_AS_ERROR_NOT_IN_SUSPEND;

	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_USB_REMOTE_WAKEUP, CY_RQT_USB_RQT_CONTEXT, 0);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/* A single status word response type */
	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		if (cy_as_ll_request_response__get_code(reply_p) ==
			CY_RESP_SUCCESS_FAILURE)
			ret = cy_as_ll_request_response__get_word(reply_p, 0);
		else
			ret = CY_AS_ERROR_INVALID_RESPONSE;
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_USB_SIGNALREMOTEWAKEUP, 0,
			dev_p->func_cbs_usb,
			CY_AS_REQUEST_RESPONSE_EX, req_p,
			reply_p, cy_as_usb_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;
		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

cy_as_return_status_t
cy_as_usb_set_m_s_report_threshold(cy_as_device_handle handle,
		uint32_t wr_sectors,
		uint32_t rd_sectors,
		cy_as_function_callback cb,
		uint32_t client)
{
	cy_as_return_status_t ret;
	cy_as_ll_request_response *req_p , *reply_p;

	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if ((cb == 0) && (cy_as_device_is_in_callback(dev_p)))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	/* Check if the firmware version supports this feature. */
	if ((dev_p->media_supported[0]) && (dev_p->media_supported[0] ==
		(1 << cy_as_media_nand)))
		return CY_AS_ERROR_NOT_SUPPORTED;

	req_p = cy_as_ll_create_request(dev_p, CY_RQT_USB_STORAGE_MONITOR,
		CY_RQT_USB_RQT_CONTEXT, 4);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/* A single status word response type */
	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	/* Set the read and write count parameters into
	 * the request structure. */
	cy_as_ll_request_response__set_word(req_p, 0,
		(uint16_t)((wr_sectors >> 16) & 0xFFFF));
	cy_as_ll_request_response__set_word(req_p, 1,
		(uint16_t)(wr_sectors & 0xFFFF));
	cy_as_ll_request_response__set_word(req_p, 2,
		(uint16_t)((rd_sectors >> 16) & 0xFFFF));
	cy_as_ll_request_response__set_word(req_p, 3,
		(uint16_t)(rd_sectors & 0xFFFF));

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		if (cy_as_ll_request_response__get_code(reply_p) ==
		CY_RESP_SUCCESS_FAILURE)
			ret = cy_as_ll_request_response__get_word(reply_p, 0);
		else
			ret = CY_AS_ERROR_INVALID_RESPONSE;
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_USB_SET_MSREPORT_THRESHOLD, 0,
			dev_p->func_cbs_usb, CY_AS_REQUEST_RESPONSE_EX,
			req_p, reply_p, cy_as_usb_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;
		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

cy_as_return_status_t
cy_as_usb_select_m_s_partitions(
		cy_as_device_handle		handle,
		cy_as_bus_number_t		 bus,
		uint32_t				device,
		cy_as_usb_m_s_type_t		 type,
		cy_as_function_callback	cb,
		uint32_t				client)
{
	cy_as_return_status_t ret;
	cy_as_ll_request_response *req_p , *reply_p;
	uint16_t val;

	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_usb_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	/* This API has to be made before SetEnumConfig is called. */
	if (dev_p->usb_config[0].enabled)
		return CY_AS_ERROR_INVALID_CALL_SEQUENCE;

	if ((cb == 0) && (cy_as_device_is_in_callback(dev_p)))
		return CY_AS_ERROR_INVALID_IN_CALLBACK;

	req_p = cy_as_ll_create_request(dev_p, CY_RQT_MS_PARTITION_SELECT,
		CY_RQT_USB_RQT_CONTEXT, 2);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/* A single status word response type */
	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	/* Set the read and write count parameters into
	 * the request structure. */
	cy_as_ll_request_response__set_word(req_p, 0,
		(uint16_t)((bus << 8) | device));

	val = 0;
	if ((type == cy_as_usb_m_s_unit0) || (type == cy_as_usb_m_s_both))
		val |= 1;
	if ((type == cy_as_usb_m_s_unit1) || (type == cy_as_usb_m_s_both))
		val |= (1 << 8);

	cy_as_ll_request_response__set_word(req_p, 1, val);

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		if (cy_as_ll_request_response__get_code(reply_p) ==
		CY_RESP_SUCCESS_FAILURE)
			ret = cy_as_ll_request_response__get_word(reply_p, 0);
		else
			ret = CY_AS_ERROR_INVALID_RESPONSE;
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_NODATA, 0, dev_p->func_cbs_usb,
			CY_AS_REQUEST_RESPONSE_EX, req_p, reply_p,
			cy_as_usb_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;
		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

static void
cy_as_usb_func_callback(
					cy_as_device *dev_p,
					uint8_t context,
					cy_as_ll_request_response *rqt,
					cy_as_ll_request_response *resp,
					cy_as_return_status_t stat)
{
	cy_as_usb_func_c_b_node* node = (cy_as_usb_func_c_b_node *)
		dev_p->usb_func_cbs->head_p;
	cy_as_func_c_b_node* fnode = (cy_as_func_c_b_node *)
		dev_p->func_cbs_usb->head_p;
	cy_as_return_status_t  ret = CY_AS_ERROR_SUCCESS;

	cy_as_device_handle	h = (cy_as_device_handle)dev_p;
	cy_bool	delayed_ack = (rqt->flags & CY_AS_REQUEST_RESPONSE_DELAY_ACK)
		== CY_AS_REQUEST_RESPONSE_DELAY_ACK;
	cy_bool	ex_request = (rqt->flags & CY_AS_REQUEST_RESPONSE_EX)
		== CY_AS_REQUEST_RESPONSE_EX;
	cy_bool	ms_request = (rqt->flags & CY_AS_REQUEST_RESPONSE_MS)
		== CY_AS_REQUEST_RESPONSE_MS;
	uint8_t	code;
	uint8_t	ep, state;

	if (!ex_request && !ms_request) {
		cy_as_hal_assert(dev_p->usb_func_cbs->count != 0);
		cy_as_hal_assert(dev_p->usb_func_cbs->type ==
			CYAS_USB_FUNC_CB);
	} else {
		cy_as_hal_assert(dev_p->func_cbs_usb->count != 0);
		cy_as_hal_assert(dev_p->func_cbs_usb->type == CYAS_FUNC_CB);
	}

	(void)context;

	/* The Handlers are responsible for Deleting the rqt and resp when
	 * they are finished
	 */
	code = cy_as_ll_request_response__get_code(rqt);
	switch (code) {
	case CY_RQT_START_USB:
		ret = my_handle_response_usb_start(dev_p, rqt, resp, stat);
		break;
	case CY_RQT_STOP_USB:
		ret = my_handle_response_usb_stop(dev_p, rqt, resp, stat);
		break;
	case CY_RQT_SET_CONNECT_STATE:
		if (!cy_as_ll_request_response__get_word(rqt, 0))
			ret = my_handle_response_disconnect(
				dev_p, rqt, resp, stat);
		else
			ret = my_handle_response_connect(
				dev_p, rqt, resp, stat);
		break;
	case CY_RQT_GET_CONNECT_STATE:
		break;
	case CY_RQT_SET_USB_CONFIG:
		ret = my_handle_response_set_enum_config(dev_p, rqt, resp);
		break;
	case CY_RQT_GET_USB_CONFIG:
		cy_as_hal_assert(fnode->data != 0);
		ret = my_handle_response_get_enum_config(dev_p,
			rqt, resp, fnode->data);
		break;
	case CY_RQT_STALL_ENDPOINT:
		ep	= (uint8_t)cy_as_ll_request_response__get_word(rqt, 0);
		state = (uint8_t)cy_as_ll_request_response__get_word(rqt, 1);
		ret = my_handle_response_no_data(dev_p, rqt, resp);
		if ((ret == CY_AS_ERROR_SUCCESS) && (ep > 1) && (state != 0)
			&& (dev_p->usb_config[ep].dir == cy_as_usb_out))
			cy_as_usb_flush_logical_e_p(dev_p, ep);
		break;
	case CY_RQT_GET_STALL:
		cy_as_hal_assert(fnode->data != 0);
		ret = my_handle_response_get_stall(dev_p,
			rqt, resp, (cy_bool *)fnode->data);
		break;
	case CY_RQT_SET_DESCRIPTOR:
		ret = my_handle_response_no_data(dev_p, rqt, resp);
		break;
	case CY_RQT_GET_DESCRIPTOR:
		cy_as_hal_assert(fnode->data != 0);
		ret = my_handle_response_get_descriptor(dev_p,
			rqt, resp, (cy_as_get_descriptor_data *)fnode->data);
		break;
	case CY_RQT_SET_USB_CONFIG_REGISTERS:
		ret = my_handle_response_no_data(dev_p, rqt, resp);
		if (ret == CY_AS_ERROR_SUCCESS)
			ret = cy_as_usb_setup_dma(dev_p);
		break;
	case CY_RQT_ENDPOINT_SET_NAK:
		ret = my_handle_response_no_data(dev_p, rqt, resp);
		break;
	case CY_RQT_GET_ENDPOINT_NAK:
		cy_as_hal_assert(fnode->data != 0);
		ret = my_handle_response_get_nak(dev_p,
			rqt, resp, (cy_bool *)fnode->data);
		break;
	case CY_RQT_ACK_SETUP_PACKET:
		break;
	case CY_RQT_USB_REMOTE_WAKEUP:
		ret = my_handle_response_no_data(dev_p, rqt, resp);
		break;
	case CY_RQT_CLEAR_DESCRIPTORS:
		ret = my_handle_response_no_data(dev_p, rqt, resp);
		break;
	case CY_RQT_USB_STORAGE_MONITOR:
		ret = my_handle_response_no_data(dev_p, rqt, resp);
		break;
	case CY_RQT_MS_PARTITION_SELECT:
		ret = my_handle_response_no_data(dev_p, rqt, resp);
		break;
	default:
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		cy_as_hal_assert(cy_false);
		break;
	}

	/*
	 * if the low level layer returns a direct error, use
	 * the corresponding error code. if not, use the error
	 * code based on the response from firmware.
	 */
	if (stat == CY_AS_ERROR_SUCCESS)
		stat = ret;

	if (ex_request || ms_request) {
		fnode->cb_p((cy_as_device_handle)dev_p, stat,
			fnode->client_data, fnode->data_type, fnode->data);
		cy_as_remove_c_b_node(dev_p->func_cbs_usb);
	} else {
		node->cb_p((cy_as_device_handle)dev_p, stat,
			node->client_data);
		cy_as_remove_c_b_node(dev_p->usb_func_cbs);
	}

	if (delayed_ack) {
		cy_as_hal_assert(cy_as_device_is_ack_delayed(dev_p));
		cy_as_device_rem_ack_delayed(dev_p);

		/*
		 * send the ACK if required.
		 */
		if (!cy_as_device_is_ack_delayed(dev_p))
			cy_as_usb_ack_setup_packet(h,
				usb_ack_callback, 0);
	}
}


/*[]*/
