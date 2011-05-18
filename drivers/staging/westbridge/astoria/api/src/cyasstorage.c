/* Cypress West Bridge API source file (cyasstorage.c)
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

/*
* Storage Design
*
* The storage module is fairly straight forward once the
* DMA and LOWLEVEL modules have been designed.  The
* storage module simple takes requests from the user, queues
* the associated DMA requests for action, and then sends
* the low level requests to the West Bridge firmware.
*
*/

#include "../../include/linux/westbridge/cyashal.h"
#include "../../include/linux/westbridge/cyasstorage.h"
#include "../../include/linux/westbridge/cyaserr.h"
#include "../../include/linux/westbridge/cyasdevice.h"
#include "../../include/linux/westbridge/cyaslowlevel.h"
#include "../../include/linux/westbridge/cyasdma.h"
#include "../../include/linux/westbridge/cyasregs.h"

/* Map a pre-V1.2 media type to the V1.2+ bus number */
cy_as_return_status_t
cy_an_map_bus_from_media_type(cy_as_device *dev_p,
	cy_as_media_type type, cy_as_bus_number_t *bus)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint8_t code = (uint8_t)(1 << type);
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	if (!cy_as_device_is_configured(dev_p))
		return CY_AS_ERROR_NOT_CONFIGURED;

	if (!cy_as_device_is_firmware_loaded(dev_p))
		return CY_AS_ERROR_NO_FIRMWARE;


	if (dev_p->media_supported[0] & code) {
		if (dev_p->media_supported[1] & code) {
			/*
			 * this media type could be supported on multiple
			 * buses. so, report an address resolution error.
			 */
			ret = CY_AS_ERROR_ADDRESS_RESOLUTION_ERROR;
		} else
			*bus = 0;
	} else {
		if (dev_p->media_supported[1] & code)
			*bus = 1;
		else
			ret = CY_AS_ERROR_NO_SUCH_MEDIA;
	}

	return ret;
}

static uint16_t
create_address(cy_as_bus_number_t bus, uint32_t device, uint8_t unit)
{
	cy_as_hal_assert(bus >= 0  && bus < CY_AS_MAX_BUSES);
	cy_as_hal_assert(device < 16);

	return (uint16_t)(((uint8_t)bus << 12) | (device << 8) | unit);
}

cy_as_media_type
cy_as_storage_get_media_from_address(uint16_t v)
{
	cy_as_media_type media = cy_as_media_max_media_value;

	switch (v & 0xFF) {
	case 0x00:
		break;
	case 0x01:
		media = cy_as_media_nand;
		break;
	case 0x02:
		media = cy_as_media_sd_flash;
		break;
	case 0x04:
		media = cy_as_media_mmc_flash;
		break;
	case 0x08:
		media = cy_as_media_ce_ata;
		break;
	case 0x10:
		media = cy_as_media_sdio;
		break;
	default:
		cy_as_hal_assert(0);
			break;
	}

	return media;
}

cy_as_bus_number_t
cy_as_storage_get_bus_from_address(uint16_t v)
{
	cy_as_bus_number_t bus = (cy_as_bus_number_t)((v >> 12) & 0x0f);
	cy_as_hal_assert(bus >= 0 && bus < CY_AS_MAX_BUSES);
	return bus;
}

uint32_t
cy_as_storage_get_device_from_address(uint16_t v)
{
	return (uint32_t)((v >> 8) & 0x0f);
}

static uint8_t
get_unit_from_address(uint16_t v)
{
	return (uint8_t)(v & 0xff);
}

static cy_as_return_status_t
cy_as_map_bad_addr(uint16_t val)
{
	cy_as_return_status_t ret = CY_AS_ERROR_INVALID_RESPONSE;

	switch (val) {
	case 0:
		ret = CY_AS_ERROR_NO_SUCH_BUS;
		break;
	case 1:
		ret = CY_AS_ERROR_NO_SUCH_DEVICE;
		break;
	case 2:
		ret = CY_AS_ERROR_NO_SUCH_UNIT;
		break;
	case 3:
		ret = CY_AS_ERROR_INVALID_BLOCK;
		break;
	}

	return ret;
}

static void
my_storage_request_callback(cy_as_device *dev_p,
		uint8_t context,
		cy_as_ll_request_response *req_p,
		cy_as_ll_request_response *resp_p,
		cy_as_return_status_t ret)
{
	uint16_t val;
	uint16_t addr;
	cy_as_bus_number_t bus;
	uint32_t device;
	cy_as_device_handle h = (cy_as_device_handle)dev_p;
	cy_as_dma_end_point *ep_p = NULL;

	(void)resp_p;
	(void)context;
	(void)ret;

	switch (cy_as_ll_request_response__get_code(req_p)) {
	case CY_RQT_MEDIA_CHANGED:
		cy_as_ll_send_status_response(dev_p,
			CY_RQT_STORAGE_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 0);

		/* Media has either been inserted or removed */
		addr = cy_as_ll_request_response__get_word(req_p, 0);

		bus = cy_as_storage_get_bus_from_address(addr);
		device = cy_as_storage_get_device_from_address(addr);

		/* Clear the entry for this device to force re-query later */
		cy_as_hal_mem_set(&(dev_p->storage_device_info[bus][device]), 0,
			sizeof(dev_p->storage_device_info[bus][device]));

		val = cy_as_ll_request_response__get_word(req_p, 1);
		if (dev_p->storage_event_cb_ms) {
			if (val == 1)
				dev_p->storage_event_cb_ms(h, bus,
					device, cy_as_storage_removed, 0);
			else
				dev_p->storage_event_cb_ms(h, bus,
					device, cy_as_storage_inserted, 0);
		} else if (dev_p->storage_event_cb) {
			if (val == 1)
				dev_p->storage_event_cb(h, bus,
					cy_as_storage_removed, 0);
			else
				dev_p->storage_event_cb(h, bus,
					cy_as_storage_inserted, 0);
		}

		break;

	case CY_RQT_ANTIOCH_CLAIM:
		cy_as_ll_send_status_response(dev_p,
			CY_RQT_STORAGE_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 0);
		if (dev_p->storage_event_cb || dev_p->storage_event_cb_ms) {
			val = cy_as_ll_request_response__get_word(req_p, 0);
			if (dev_p->storage_event_cb_ms) {
				if (val & 0x0100)
					dev_p->storage_event_cb_ms(h, 0, 0,
						cy_as_storage_antioch, 0);
				if (val & 0x0200)
					dev_p->storage_event_cb_ms(h, 1, 0,
						cy_as_storage_antioch, 0);
			} else {
				if (val & 0x01)
					dev_p->storage_event_cb(h,
						cy_as_media_nand,
						cy_as_storage_antioch, 0);
				if (val & 0x02)
					dev_p->storage_event_cb(h,
						cy_as_media_sd_flash,
						cy_as_storage_antioch, 0);
				if (val & 0x04)
					dev_p->storage_event_cb(h,
						cy_as_media_mmc_flash,
						cy_as_storage_antioch, 0);
				if (val & 0x08)
					dev_p->storage_event_cb(h,
						cy_as_media_ce_ata,
						cy_as_storage_antioch, 0);
			}
		}
		break;

	case CY_RQT_ANTIOCH_RELEASE:
		cy_as_ll_send_status_response(dev_p,
			CY_RQT_STORAGE_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 0);
		val = cy_as_ll_request_response__get_word(req_p, 0);
		if (dev_p->storage_event_cb_ms) {
			if (val & 0x0100)
				dev_p->storage_event_cb_ms(h, 0, 0,
					cy_as_storage_processor, 0);
			if (val & 0x0200)
				dev_p->storage_event_cb_ms(h, 1, 0,
					cy_as_storage_processor, 0);
		} else if (dev_p->storage_event_cb) {
			if (val & 0x01)
				dev_p->storage_event_cb(h,
					cy_as_media_nand,
					cy_as_storage_processor, 0);
			if (val & 0x02)
				dev_p->storage_event_cb(h,
					cy_as_media_sd_flash,
					cy_as_storage_processor, 0);
			if (val & 0x04)
				dev_p->storage_event_cb(h,
					cy_as_media_mmc_flash,
					cy_as_storage_processor, 0);
			if (val & 0x08)
				dev_p->storage_event_cb(h,
					cy_as_media_ce_ata,
					cy_as_storage_processor, 0);
		}
		break;


	case CY_RQT_SDIO_INTR:
		cy_as_ll_send_status_response(dev_p,
			CY_RQT_STORAGE_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 0);
		val = cy_as_ll_request_response__get_word(req_p, 0);
		if (dev_p->storage_event_cb_ms) {
			if (val & 0x0100)
				dev_p->storage_event_cb_ms(h, 1, 0,
					cy_as_sdio_interrupt, 0);
			else
				dev_p->storage_event_cb_ms(h, 0, 0,
					cy_as_sdio_interrupt, 0);

		} else if (dev_p->storage_event_cb) {
			dev_p->storage_event_cb(h,
				cy_as_media_sdio, cy_as_sdio_interrupt, 0);
		}
		break;

	case CY_RQT_P2S_DMA_START:
		/* Do the DMA setup for the waiting operation. */
		cy_as_ll_send_status_response(dev_p,
			CY_RQT_STORAGE_RQT_CONTEXT, CY_AS_ERROR_SUCCESS, 0);
		cy_as_device_set_p2s_dma_start_recvd(dev_p);
		if (dev_p->storage_oper == cy_as_op_read) {
			ep_p = CY_AS_NUM_EP(dev_p, CY_AS_P2S_READ_ENDPOINT);
			cy_as_dma_end_point_set_stopped(ep_p);
			cy_as_dma_kick_start(dev_p, CY_AS_P2S_READ_ENDPOINT);
		} else {
			ep_p = CY_AS_NUM_EP(dev_p, CY_AS_P2S_WRITE_ENDPOINT);
			cy_as_dma_end_point_set_stopped(ep_p);
			cy_as_dma_kick_start(dev_p, CY_AS_P2S_WRITE_ENDPOINT);
		}
		break;

	default:
		cy_as_hal_print_message("invalid request received "
			"on storage context\n");
		val = req_p->box0;
		cy_as_ll_send_data_response(dev_p, CY_RQT_STORAGE_RQT_CONTEXT,
			CY_RESP_INVALID_REQUEST, sizeof(val), &val);
		break;
	}
}

static cy_as_return_status_t
is_storage_active(cy_as_device *dev_p)
{
	if (!cy_as_device_is_configured(dev_p))
		return CY_AS_ERROR_NOT_CONFIGURED;

	if (!cy_as_device_is_firmware_loaded(dev_p))
		return CY_AS_ERROR_NO_FIRMWARE;

	if (dev_p->storage_count == 0)
		return CY_AS_ERROR_NOT_RUNNING;

	if (cy_as_device_is_in_suspend_mode(dev_p))
		return CY_AS_ERROR_IN_SUSPEND;

	return CY_AS_ERROR_SUCCESS;
}

static void
cy_as_storage_func_callback(cy_as_device *dev_p,
					uint8_t context,
					cy_as_ll_request_response *rqt,
					cy_as_ll_request_response *resp,
					cy_as_return_status_t ret);

static cy_as_return_status_t
my_handle_response_no_data(cy_as_device *dev_p,
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

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

static cy_as_return_status_t
my_handle_response_storage_start(cy_as_device *dev_p,
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
	if (dev_p->storage_count > 0 && ret ==
	CY_AS_ERROR_ALREADY_RUNNING)
		ret = CY_AS_ERROR_SUCCESS;

	ret = cy_as_dma_enable_end_point(dev_p,
		CY_AS_P2S_WRITE_ENDPOINT, cy_true, cy_as_direction_in);
	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	ret = cy_as_dma_set_max_dma_size(dev_p,
		CY_AS_P2S_WRITE_ENDPOINT, CY_AS_STORAGE_EP_SIZE);
	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	ret = cy_as_dma_enable_end_point(dev_p,
		CY_AS_P2S_READ_ENDPOINT, cy_true, cy_as_direction_out);
	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	ret = cy_as_dma_set_max_dma_size(dev_p,
		CY_AS_P2S_READ_ENDPOINT, CY_AS_STORAGE_EP_SIZE);
	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	cy_as_ll_register_request_callback(dev_p,
		CY_RQT_STORAGE_RQT_CONTEXT, my_storage_request_callback);

	/* Create the request/response used for storage reads and writes. */
	dev_p->storage_rw_req_p  = cy_as_ll_create_request(dev_p,
		0, CY_RQT_STORAGE_RQT_CONTEXT, 5);
	if (dev_p->storage_rw_req_p == 0) {
		ret = CY_AS_ERROR_OUT_OF_MEMORY;
		goto destroy;
	}

	dev_p->storage_rw_resp_p = cy_as_ll_create_response(dev_p, 5);
	if (dev_p->storage_rw_resp_p == 0) {
		cy_as_ll_destroy_request(dev_p, dev_p->storage_rw_req_p);
		ret = CY_AS_ERROR_OUT_OF_MEMORY;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	/* Increment the storage count only if
	 * the above functionality succeeds.*/
	if (ret == CY_AS_ERROR_SUCCESS) {
		if (dev_p->storage_count == 0) {
			cy_as_hal_mem_set(dev_p->storage_device_info,
				0, sizeof(dev_p->storage_device_info));
			dev_p->is_storage_only_mode = cy_false;
		}

		dev_p->storage_count++;
	}

	cy_as_device_clear_s_s_s_pending(dev_p);

	return ret;
}

cy_as_return_status_t
cy_as_storage_start(cy_as_device_handle handle,
				   cy_as_function_callback cb,
				   uint32_t client)
{
	cy_as_ll_request_response *req_p, *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	cy_as_device *dev_p = (cy_as_device *)handle;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	if (!cy_as_device_is_configured(dev_p))
		return CY_AS_ERROR_NOT_CONFIGURED;

	if (!cy_as_device_is_firmware_loaded(dev_p))
		return CY_AS_ERROR_NO_FIRMWARE;

	if (cy_as_device_is_in_suspend_mode(dev_p))
		return CY_AS_ERROR_IN_SUSPEND;

	if (cy_as_device_is_s_s_s_pending(dev_p))
		return CY_AS_ERROR_STARTSTOP_PENDING;

	cy_as_device_set_s_s_s_pending(dev_p);

	if (dev_p->storage_count == 0) {
		/* Create the request to send to the West Bridge device */
		req_p = cy_as_ll_create_request(dev_p,
			CY_RQT_START_STORAGE, CY_RQT_STORAGE_RQT_CONTEXT, 1);
		if (req_p == 0) {
			cy_as_device_clear_s_s_s_pending(dev_p);
			return CY_AS_ERROR_OUT_OF_MEMORY;
		}

		/* Reserve space for the reply, the reply data
		 * will not exceed one word */
		reply_p = cy_as_ll_create_response(dev_p, 1);
		if (reply_p == 0) {
			cy_as_device_clear_s_s_s_pending(dev_p);
			cy_as_ll_destroy_request(dev_p, req_p);
			return CY_AS_ERROR_OUT_OF_MEMORY;
		}

		if (cb == 0) {
			ret = cy_as_ll_send_request_wait_reply(dev_p,
				req_p, reply_p);
			if (ret != CY_AS_ERROR_SUCCESS)
				goto destroy;

			return my_handle_response_storage_start(dev_p,
				req_p, reply_p, ret);
		} else {
			ret = cy_as_misc_send_request(dev_p, cb, client,
				CY_FUNCT_CB_STOR_START, 0, dev_p->func_cbs_stor,
				CY_AS_REQUEST_RESPONSE_EX, req_p, reply_p,
				cy_as_storage_func_callback);

			if (ret != CY_AS_ERROR_SUCCESS)
				goto destroy;

			/* The request and response are freed as
			 * part of the FuncCallback */
			return ret;
		}

destroy:
		cy_as_ll_destroy_request(dev_p, req_p);
		cy_as_ll_destroy_response(dev_p, reply_p);
	} else {
		dev_p->storage_count++;
		if (cb)
			cb(handle, ret, client, CY_FUNCT_CB_STOR_START, 0);
	}

	cy_as_device_clear_s_s_s_pending(dev_p);

	return ret;
}
EXPORT_SYMBOL(cy_as_storage_start);

static cy_as_return_status_t
my_handle_response_storage_stop(cy_as_device *dev_p,
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

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	if (ret == CY_AS_ERROR_SUCCESS) {
		cy_as_ll_destroy_request(dev_p, dev_p->storage_rw_req_p);
		cy_as_ll_destroy_response(dev_p, dev_p->storage_rw_resp_p);
		dev_p->storage_count--;
	}

	cy_as_device_clear_s_s_s_pending(dev_p);

	return ret;
}
cy_as_return_status_t
cy_as_storage_stop(cy_as_device_handle handle,
				  cy_as_function_callback cb,
				  uint32_t client)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	cy_as_device *dev_p = (cy_as_device *)handle;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (cy_as_device_is_storage_async_pending(dev_p))
		return CY_AS_ERROR_ASYNC_PENDING;

	if (cy_as_device_is_s_s_s_pending(dev_p))
		return CY_AS_ERROR_STARTSTOP_PENDING;

	cy_as_device_set_s_s_s_pending(dev_p);

	if (dev_p->storage_count == 1) {

		/* Create the request to send to the West Bridge device */
		req_p = cy_as_ll_create_request(dev_p,
			CY_RQT_STOP_STORAGE, CY_RQT_STORAGE_RQT_CONTEXT, 0);
		if (req_p == 0) {
			cy_as_device_clear_s_s_s_pending(dev_p);
			return CY_AS_ERROR_OUT_OF_MEMORY;
		}

		/* Reserve space for the reply, the reply data
		 * will not exceed one word */
		reply_p = cy_as_ll_create_response(dev_p, 1);
		if (reply_p == 0) {
			cy_as_device_clear_s_s_s_pending(dev_p);
			cy_as_ll_destroy_request(dev_p, req_p);
			return CY_AS_ERROR_OUT_OF_MEMORY;
		}

		if (cb == 0) {
			ret = cy_as_ll_send_request_wait_reply(dev_p,
				req_p, reply_p);
			if (ret != CY_AS_ERROR_SUCCESS)
				goto destroy;

			return my_handle_response_storage_stop(dev_p,
				req_p, reply_p, ret);
		} else {
			ret = cy_as_misc_send_request(dev_p, cb, client,
				CY_FUNCT_CB_STOR_STOP, 0, dev_p->func_cbs_stor,
				CY_AS_REQUEST_RESPONSE_EX, req_p, reply_p,
				cy_as_storage_func_callback);

			if (ret != CY_AS_ERROR_SUCCESS)
				goto destroy;

			/* The request and response are freed
			 * as part of the MiscFuncCallback */
			return ret;
		}

destroy:
		cy_as_ll_destroy_request(dev_p, req_p);
		cy_as_ll_destroy_response(dev_p, reply_p);
	} else if (dev_p->storage_count > 1) {
		dev_p->storage_count--;
		if (cb)
			cb(handle, ret, client, CY_FUNCT_CB_STOR_STOP, 0);
	}

	cy_as_device_clear_s_s_s_pending(dev_p);

	return ret;
}
EXPORT_SYMBOL(cy_as_storage_stop);

cy_as_return_status_t
cy_as_storage_register_callback(cy_as_device_handle handle,
	cy_as_storage_event_callback callback)
{
	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	if (!cy_as_device_is_configured(dev_p))
		return CY_AS_ERROR_NOT_CONFIGURED;

	if (!cy_as_device_is_firmware_loaded(dev_p))
		return CY_AS_ERROR_NO_FIRMWARE;

	if (dev_p->storage_count == 0)
		return CY_AS_ERROR_NOT_RUNNING;

	dev_p->storage_event_cb = NULL;
	dev_p->storage_event_cb_ms = callback;

	return CY_AS_ERROR_SUCCESS;
}
EXPORT_SYMBOL(cy_as_storage_register_callback);


static cy_as_return_status_t
my_handle_response_storage_claim(cy_as_device *dev_p,
			cy_as_ll_request_response *req_p,
			cy_as_ll_request_response *reply_p)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	if (cy_as_ll_request_response__get_code(reply_p) ==
		CY_RESP_NO_SUCH_ADDRESS) {
		ret = cy_as_map_bad_addr(
			cy_as_ll_request_response__get_word(reply_p, 3));
		goto destroy;
	}

	if (cy_as_ll_request_response__get_code(reply_p) !=
		CY_RESP_MEDIA_CLAIMED_RELEASED) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	/* The response must be about the address I am
	 * trying to claim or the firmware is broken */
	if ((cy_as_storage_get_bus_from_address(
			cy_as_ll_request_response__get_word(req_p, 0)) !=
		cy_as_storage_get_bus_from_address(
			cy_as_ll_request_response__get_word(reply_p, 0))) ||
		(cy_as_storage_get_device_from_address(
			cy_as_ll_request_response__get_word(req_p, 0)) !=
		cy_as_storage_get_device_from_address(
			cy_as_ll_request_response__get_word(reply_p, 0)))) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	if (cy_as_ll_request_response__get_word(reply_p, 1) != 1)
		ret = CY_AS_ERROR_NOT_ACQUIRED;

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

static cy_as_return_status_t
my_storage_claim(cy_as_device *dev_p,
				void *data,
				cy_as_bus_number_t bus,
				uint32_t device,
				uint16_t req_flags,
				cy_as_function_callback cb,
				uint32_t client)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (dev_p->mtp_count > 0)
		return CY_AS_ERROR_NOT_VALID_IN_MTP;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_CLAIM_STORAGE, CY_RQT_STORAGE_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	cy_as_ll_request_response__set_word(req_p,
		0, create_address(bus, device, 0));

	/* Reserve space for the reply, the reply data will
	 * not exceed one word */
	reply_p = cy_as_ll_create_response(dev_p, 4);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return my_handle_response_storage_claim(dev_p, req_p, reply_p);
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_STOR_CLAIM, data, dev_p->func_cbs_stor,
			req_flags, req_p, reply_p,
			cy_as_storage_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		/* The request and response are freed as part of
		 * the MiscFuncCallback */
		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

cy_as_return_status_t
cy_as_storage_claim(cy_as_device_handle handle,
				   cy_as_bus_number_t bus,
				   uint32_t device,
				   cy_as_function_callback cb,
				   uint32_t client)
{
	cy_as_device *dev_p = (cy_as_device *)handle;

	if (bus < 0 || bus >= CY_AS_MAX_BUSES)
		return CY_AS_ERROR_NO_SUCH_BUS;

	return my_storage_claim(dev_p, NULL, bus, device,
		CY_AS_REQUEST_RESPONSE_MS, cb, client);
}
EXPORT_SYMBOL(cy_as_storage_claim);

static cy_as_return_status_t
my_handle_response_storage_release(cy_as_device *dev_p,
				cy_as_ll_request_response *req_p,
				cy_as_ll_request_response *reply_p)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	if (cy_as_ll_request_response__get_code(reply_p) ==
		CY_RESP_NO_SUCH_ADDRESS) {
		ret = cy_as_map_bad_addr(
			cy_as_ll_request_response__get_word(reply_p, 3));
		goto destroy;
	}

	if (cy_as_ll_request_response__get_code(reply_p) !=
		CY_RESP_MEDIA_CLAIMED_RELEASED) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	/* The response must be about the address I am
	 * trying to release or the firmware is broken */
	if ((cy_as_storage_get_bus_from_address(
			cy_as_ll_request_response__get_word(req_p, 0)) !=
		cy_as_storage_get_bus_from_address(
			cy_as_ll_request_response__get_word(reply_p, 0))) ||
		(cy_as_storage_get_device_from_address(
			cy_as_ll_request_response__get_word(req_p, 0)) !=
		cy_as_storage_get_device_from_address(
			cy_as_ll_request_response__get_word(reply_p, 0)))) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}


	if (cy_as_ll_request_response__get_word(reply_p, 1) != 0)
		ret = CY_AS_ERROR_NOT_RELEASED;

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

static cy_as_return_status_t
my_storage_release(cy_as_device *dev_p,
					void *data,
					cy_as_bus_number_t bus,
					uint32_t device,
					uint16_t req_flags,
					cy_as_function_callback cb,
					uint32_t client)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (dev_p->mtp_count > 0)
		return CY_AS_ERROR_NOT_VALID_IN_MTP;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p, CY_RQT_RELEASE_STORAGE,
		CY_RQT_STORAGE_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	cy_as_ll_request_response__set_word(
		req_p, 0, create_address(bus, device, 0));

	/* Reserve space for the reply, the reply
	 * data will not exceed one word */
	reply_p = cy_as_ll_create_response(dev_p, 4);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return my_handle_response_storage_release(
			dev_p, req_p, reply_p);
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_STOR_RELEASE, data, dev_p->func_cbs_stor,
			req_flags, req_p, reply_p,
			cy_as_storage_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		/* The request and response are freed as
		 * part of the MiscFuncCallback */
		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

cy_as_return_status_t
cy_as_storage_release(cy_as_device_handle handle,
				   cy_as_bus_number_t bus,
				   uint32_t device,
				   cy_as_function_callback cb,
				   uint32_t client)
{
	cy_as_device *dev_p = (cy_as_device *)handle;

	if (bus < 0 || bus >= CY_AS_MAX_BUSES)
		return CY_AS_ERROR_NO_SUCH_BUS;

	return my_storage_release(dev_p, NULL, bus, device,
		CY_AS_REQUEST_RESPONSE_MS, cb, client);
}
EXPORT_SYMBOL(cy_as_storage_release);

static cy_as_return_status_t
my_handle_response_storage_query_bus(cy_as_device *dev_p,
				cy_as_ll_request_response *req_p,
				cy_as_ll_request_response *reply_p,
				uint32_t *count)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint8_t code = cy_as_ll_request_response__get_code(reply_p);
	uint16_t v;

	if (code == CY_RESP_NO_SUCH_ADDRESS) {
		ret = CY_AS_ERROR_NO_SUCH_BUS;
		goto destroy;
	}

	if (code != CY_RESP_BUS_DESCRIPTOR) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	/*
	 * verify that the response corresponds to the bus that was queried.
	 */
	if (cy_as_storage_get_bus_from_address(
		cy_as_ll_request_response__get_word(req_p, 0)) !=
		cy_as_storage_get_bus_from_address(
		cy_as_ll_request_response__get_word(reply_p, 0))) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	v = cy_as_ll_request_response__get_word(reply_p, 1);
	if (req_p->flags & CY_AS_REQUEST_RESPONSE_MS) {
		/*
		 * this request is only for the count of devices
		 * on the bus. there is no need to check the media type.
		 */
		if (v)
			*count = 1;
		else
			*count = 0;
	} else {
		/*
		 * this request is for the count of devices of a
		 * particular type. we need to check whether the media
		 * type found matches the queried type.
		 */
		cy_as_media_type queried = (cy_as_media_type)
			cy_as_ll_request_response__get_word(req_p, 1);
		cy_as_media_type found =
			cy_as_storage_get_media_from_address(v);

		if (queried == found)
			*count = 1;
		else
			*count = 0;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

cy_as_return_status_t
my_storage_query_bus(cy_as_device *dev_p,
						cy_as_bus_number_t bus,
						cy_as_media_type   type,
						uint16_t req_flags,
						uint32_t *count,
						cy_as_function_callback cb,
						uint32_t client)
{
	cy_as_return_status_t ret;
	cy_as_ll_request_response *req_p, *reply_p;
	cy_as_funct_c_b_type cb_type = CY_FUNCT_CB_STOR_QUERYBUS;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	/* Create the request to send to the Antioch device */
	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_QUERY_BUS, CY_RQT_STORAGE_RQT_CONTEXT, 2);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	cy_as_ll_request_response__set_word(req_p,
		0, create_address(bus, 0, 0));
	cy_as_ll_request_response__set_word(req_p, 1, (uint16_t)type);

	/* Reserve space for the reply, the reply data
	 * will not exceed two words. */
	reply_p = cy_as_ll_create_response(dev_p, 2);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p,
			req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		req_p->flags |= req_flags;
		return my_handle_response_storage_query_bus(dev_p,
			req_p, reply_p, count);
	} else {
		if (req_flags == CY_AS_REQUEST_RESPONSE_EX)
			cb_type = CY_FUNCT_CB_STOR_QUERYMEDIA;

		ret = cy_as_misc_send_request(dev_p, cb, client, cb_type,
			count, dev_p->func_cbs_stor, req_flags,
			req_p, reply_p, cy_as_storage_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		/* The request and response are freed as part of
		 * the MiscFuncCallback */
		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

cy_as_return_status_t
cy_as_storage_query_bus(cy_as_device_handle handle,
						cy_as_bus_number_t bus,
						uint32_t *count,
						cy_as_function_callback cb,
						uint32_t client)
{
	cy_as_device *dev_p = (cy_as_device *)handle;

	return my_storage_query_bus(dev_p, bus, cy_as_media_max_media_value,
		CY_AS_REQUEST_RESPONSE_MS, count, cb, client);
}
EXPORT_SYMBOL(cy_as_storage_query_bus);

cy_as_return_status_t
cy_as_storage_query_media(cy_as_device_handle handle,
						cy_as_media_type type,
						uint32_t *count,
						cy_as_function_callback cb,
						uint32_t client)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	cy_as_bus_number_t bus;

	cy_as_device *dev_p = (cy_as_device *)handle;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	ret = cy_an_map_bus_from_media_type(dev_p, type, &bus);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	return my_storage_query_bus(dev_p, bus, type, CY_AS_REQUEST_RESPONSE_EX,
			count, cb, client);
}
EXPORT_SYMBOL(cy_as_storage_query_media);

static cy_as_return_status_t
my_handle_response_storage_query_device(cy_as_device *dev_p,
				cy_as_ll_request_response *req_p,
				cy_as_ll_request_response *reply_p,
				void *data_p)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint16_t v;
	cy_as_bus_number_t bus;
	cy_as_media_type type;
	uint32_t device;
	cy_bool removable;
	cy_bool writeable;
	cy_bool locked;
	uint16_t block_size;
	uint32_t number_units;
	uint32_t number_eus;

	if (cy_as_ll_request_response__get_code(reply_p)
		== CY_RESP_NO_SUCH_ADDRESS) {
		ret = cy_as_map_bad_addr(
			cy_as_ll_request_response__get_word(reply_p, 3));
		goto destroy;
	}

	if (cy_as_ll_request_response__get_code(reply_p) !=
	CY_RESP_DEVICE_DESCRIPTOR) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	/* Unpack the response */
	v = cy_as_ll_request_response__get_word(reply_p, 0);
	type = cy_as_storage_get_media_from_address(v);
	bus  = cy_as_storage_get_bus_from_address(v);
	device = cy_as_storage_get_device_from_address(v);

	block_size = cy_as_ll_request_response__get_word(reply_p, 1);

	v = cy_as_ll_request_response__get_word(reply_p, 2);
	removable = (v & 0x8000) ? cy_true : cy_false;
	writeable = (v & 0x0100) ? cy_true : cy_false;
	locked = (v & 0x0200) ? cy_true : cy_false;
	number_units = (v & 0xff);

	number_eus  = (cy_as_ll_request_response__get_word(reply_p, 3) << 16)
		| cy_as_ll_request_response__get_word(reply_p, 4);

	/* Store the results based on the version of originating function */
	if (req_p->flags & CY_AS_REQUEST_RESPONSE_MS) {
		cy_as_storage_query_device_data  *store_p =
			(cy_as_storage_query_device_data *)data_p;

		/* Make sure the response is about the address we asked
		 * about - if not, firmware error */
		if ((bus != store_p->bus) || (device != store_p->device)) {
			ret = CY_AS_ERROR_INVALID_RESPONSE;
			goto destroy;
		}

		store_p->desc_p.type = type;
		store_p->desc_p.removable = removable;
		store_p->desc_p.writeable = writeable;
		store_p->desc_p.block_size = block_size;
		store_p->desc_p.number_units = number_units;
		store_p->desc_p.locked = locked;
		store_p->desc_p.erase_unit_size = number_eus;
		dev_p->storage_device_info[bus][device] = store_p->desc_p;
	} else {
		cy_as_storage_query_device_data_dep	*store_p =
			(cy_as_storage_query_device_data_dep *)data_p;

		/* Make sure the response is about the address we asked
		 * about - if not, firmware error */
		if ((type != store_p->type) || (device != store_p->device)) {
			ret = CY_AS_ERROR_INVALID_RESPONSE;
			goto destroy;
		}

		store_p->desc_p.type = type;
		store_p->desc_p.removable = removable;
		store_p->desc_p.writeable = writeable;
		store_p->desc_p.block_size = block_size;
		store_p->desc_p.number_units = number_units;
		store_p->desc_p.locked = locked;
		store_p->desc_p.erase_unit_size = number_eus;
		dev_p->storage_device_info[bus][device] = store_p->desc_p;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

static cy_as_return_status_t
my_storage_query_device(cy_as_device *dev_p,
						void *data_p,
						uint16_t req_flags,
						cy_as_bus_number_t bus,
						uint32_t device,
						cy_as_function_callback cb,
						uint32_t client)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	/* Create the request to send to the Antioch device */
	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_QUERY_DEVICE, CY_RQT_STORAGE_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, device, 0));

	/* Reserve space for the reply, the reply data
	 * will not exceed five words. */
	reply_p = cy_as_ll_create_response(dev_p, 5);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		req_p->flags |= req_flags;
		return my_handle_response_storage_query_device(dev_p,
			req_p, reply_p, data_p);
	} else {

		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_STOR_QUERYDEVICE, data_p,
			dev_p->func_cbs_stor, req_flags, req_p,
			reply_p, cy_as_storage_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		/* The request and response are freed as part of the
		 * MiscFuncCallback */
		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

cy_as_return_status_t
cy_as_storage_query_device(cy_as_device_handle handle,
			cy_as_storage_query_device_data *data_p,
			cy_as_function_callback cb,
			uint32_t client)
{
	cy_as_device *dev_p = (cy_as_device *)handle;
	return my_storage_query_device(dev_p, data_p,
		CY_AS_REQUEST_RESPONSE_MS, data_p->bus,
			data_p->device, cb, client);
}
EXPORT_SYMBOL(cy_as_storage_query_device);

static cy_as_return_status_t
my_handle_response_storage_query_unit(cy_as_device *dev_p,
			cy_as_ll_request_response *req_p,
			cy_as_ll_request_response *reply_p,
			void *data_p)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	cy_as_bus_number_t bus;
	uint32_t device;
	uint32_t unit;
	cy_as_media_type type;
	uint16_t block_size;
	uint32_t start_block;
	uint32_t unit_size;
	uint16_t v;

	if (cy_as_ll_request_response__get_code(reply_p) ==
	CY_RESP_NO_SUCH_ADDRESS) {
		ret = cy_as_map_bad_addr(
			cy_as_ll_request_response__get_word(reply_p, 3));
		goto destroy;
	}

	if (cy_as_ll_request_response__get_code(reply_p) !=
	CY_RESP_UNIT_DESCRIPTOR) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	/* Unpack the response */
	v	  = cy_as_ll_request_response__get_word(reply_p, 0);
	bus	= cy_as_storage_get_bus_from_address(v);
	device = cy_as_storage_get_device_from_address(v);
	unit   = get_unit_from_address(v);

	type   = cy_as_storage_get_media_from_address(
		cy_as_ll_request_response__get_word(reply_p, 1));

	block_size = cy_as_ll_request_response__get_word(reply_p, 2);
	start_block = cy_as_ll_request_response__get_word(reply_p, 3)
		| (cy_as_ll_request_response__get_word(reply_p, 4) << 16);
	unit_size = cy_as_ll_request_response__get_word(reply_p, 5)
		| (cy_as_ll_request_response__get_word(reply_p, 6) << 16);

	/* Store the results based on the version of
	 * originating function */
	if (req_p->flags & CY_AS_REQUEST_RESPONSE_MS) {
		cy_as_storage_query_unit_data  *store_p =
			(cy_as_storage_query_unit_data *)data_p;

		/* Make sure the response is about the address we
		 * asked about - if not, firmware error */
		if (bus != store_p->bus || device != store_p->device ||
		unit != store_p->unit) {
			ret = CY_AS_ERROR_INVALID_RESPONSE;
			goto destroy;
		}

		store_p->desc_p.type = type;
		store_p->desc_p.block_size = block_size;
		store_p->desc_p.start_block = start_block;
		store_p->desc_p.unit_size = unit_size;
	} else {
		cy_as_storage_query_unit_data_dep *store_p =
			(cy_as_storage_query_unit_data_dep *)data_p;

		/* Make sure the response is about the media type we asked
		 * about - if not, firmware error */
		if ((type != store_p->type) || (device != store_p->device) ||
		(unit != store_p->unit)) {
			ret = CY_AS_ERROR_INVALID_RESPONSE;
			goto destroy;
		}

		store_p->desc_p.type = type;
		store_p->desc_p.block_size = block_size;
		store_p->desc_p.start_block = start_block;
		store_p->desc_p.unit_size = unit_size;
	}

	dev_p->storage_device_info[bus][device].type = type;
	dev_p->storage_device_info[bus][device].block_size = block_size;

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

static cy_as_return_status_t
my_storage_query_unit(cy_as_device *dev_p,
					void *data_p,
					uint16_t req_flags,
					cy_as_bus_number_t bus,
					uint32_t device,
					uint32_t unit,
					cy_as_function_callback cb,
					uint32_t client)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p,
	CY_RQT_QUERY_UNIT, CY_RQT_STORAGE_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	if (device > 255)
		return CY_AS_ERROR_NO_SUCH_DEVICE;

	if (unit > 255)
		return CY_AS_ERROR_NO_SUCH_UNIT;

	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, device, (uint8_t)unit));

	/* Reserve space for the reply, the reply data
	 * will be of seven words. */
	reply_p = cy_as_ll_create_response(dev_p, 7);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		req_p->flags |= req_flags;
		return my_handle_response_storage_query_unit(dev_p,
			req_p, reply_p, data_p);
	} else {

		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_STOR_QUERYUNIT, data_p,
			dev_p->func_cbs_stor, req_flags, req_p, reply_p,
			cy_as_storage_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		/* The request and response are freed
		 * as part of the MiscFuncCallback */
		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

cy_as_return_status_t
cy_as_storage_query_unit(cy_as_device_handle handle,
				cy_as_storage_query_unit_data *data_p,
				cy_as_function_callback cb,
				uint32_t client)
{
	cy_as_device *dev_p = (cy_as_device *)handle;
	return my_storage_query_unit(dev_p, data_p, CY_AS_REQUEST_RESPONSE_MS,
		data_p->bus, data_p->device, data_p->unit, cb, client);
}
EXPORT_SYMBOL(cy_as_storage_query_unit);

static cy_as_return_status_t
cy_as_get_block_size(cy_as_device *dev_p,
					cy_as_bus_number_t bus,
					uint32_t device,
					cy_as_function_callback cb)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p, CY_RQT_QUERY_DEVICE,
		CY_RQT_STORAGE_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, device, 0));

	reply_p = cy_as_ll_create_response(dev_p, 4);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		if (cy_as_ll_request_response__get_code(reply_p)
		== CY_RESP_NO_SUCH_ADDRESS) {
			ret = CY_AS_ERROR_NO_SUCH_BUS;
			goto destroy;
		}

		if (cy_as_ll_request_response__get_code(reply_p) !=
		CY_RESP_DEVICE_DESCRIPTOR) {
			ret = CY_AS_ERROR_INVALID_RESPONSE;
			goto destroy;
		}

		/* Make sure the response is about the media type we asked
		 * about - if not, firmware error */
		if ((cy_as_storage_get_bus_from_address
			(cy_as_ll_request_response__get_word(reply_p, 0))
			!= bus) || (cy_as_storage_get_device_from_address
			(cy_as_ll_request_response__get_word(reply_p, 0))
			!= device)) {
			ret = CY_AS_ERROR_INVALID_RESPONSE;
			goto destroy;
		}


		dev_p->storage_device_info[bus][device].block_size =
			cy_as_ll_request_response__get_word(reply_p, 1);
	} else
		ret = CY_AS_ERROR_INVALID_REQUEST;

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

cy_as_return_status_t
my_storage_device_control(
		cy_as_device		  *dev_p,
		cy_as_bus_number_t	  bus,
		uint32_t			 device,
		cy_bool			   card_detect_en,
		cy_bool			   write_prot_en,
				cy_as_storage_card_detect config_detect,
		cy_as_function_callback cb,
		uint32_t			 client)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret;
	cy_bool use_gpio = cy_false;

	(void)device;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	if (!cy_as_device_is_configured(dev_p))
		return CY_AS_ERROR_NOT_CONFIGURED;

	if (!cy_as_device_is_firmware_loaded(dev_p))
		return CY_AS_ERROR_NO_FIRMWARE;

	if (cy_as_device_is_in_suspend_mode(dev_p))
		return CY_AS_ERROR_IN_SUSPEND;

	if (bus < 0 || bus >= CY_AS_MAX_BUSES)
		return CY_AS_ERROR_NO_SUCH_BUS;

	if (device >= CY_AS_MAX_STORAGE_DEVICES)
		return CY_AS_ERROR_NO_SUCH_DEVICE;

	/* If SD is not supported on the specified bus,
	 * then return ERROR */
	if ((dev_p->media_supported[bus] == 0) ||
		(dev_p->media_supported[bus] & (1<<cy_as_media_nand)))
		return CY_AS_ERROR_NOT_SUPPORTED;

	if (config_detect == cy_as_storage_detect_GPIO)
		use_gpio = cy_true;
	else if (config_detect == cy_as_storage_detect_SDAT_3)
		use_gpio = cy_false;
	else
		return CY_AS_ERROR_INVALID_PARAMETER;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_SD_INTERFACE_CONTROL, CY_RQT_STORAGE_RQT_CONTEXT, 2);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	cy_as_ll_request_response__set_word(req_p,
		0, create_address(bus, device, 0));
	cy_as_ll_request_response__set_word(req_p,
		1, (((uint16_t)card_detect_en << 8) |
		((uint16_t)use_gpio << 1) | (uint16_t)write_prot_en));

	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		if (cy_as_ll_request_response__get_code(reply_p) !=
		CY_RESP_SUCCESS_FAILURE) {
			ret = CY_AS_ERROR_INVALID_RESPONSE;
			goto destroy;
		}

		ret = cy_as_ll_request_response__get_word(reply_p, 0);
	} else {

		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_STOR_DEVICECONTROL,
			0, dev_p->func_cbs_stor, CY_AS_REQUEST_RESPONSE_EX,
			req_p, reply_p, cy_as_storage_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		/* The request and response are freed as part of the
		 * MiscFuncCallback */
		return ret;
	}
destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

cy_as_return_status_t
cy_as_storage_device_control(cy_as_device_handle handle,
					cy_as_bus_number_t bus,
					uint32_t device,
					cy_bool card_detect_en,
					cy_bool write_prot_en,
					cy_as_storage_card_detect config_detect,
					cy_as_function_callback cb,
					uint32_t client)
{
	cy_as_device *dev_p = (cy_as_device *)handle;

	return my_storage_device_control(dev_p, bus, device, card_detect_en,
		write_prot_en, config_detect, cb, client);
}
EXPORT_SYMBOL(cy_as_storage_device_control);

static void
cy_as_async_storage_callback(cy_as_device *dev_p,
	cy_as_end_point_number_t ep, void *buf_p, uint32_t size,
	cy_as_return_status_t ret)
{
	cy_as_storage_callback_dep cb;
	cy_as_storage_callback cb_ms;

	(void)size;
	(void)buf_p;
	(void)ep;

	cy_as_device_clear_storage_async_pending(dev_p);

	/*
	* if the LL request callback has already been called,
	* the user callback has to be called from here.
	*/
	if (!dev_p->storage_wait) {
			cy_as_hal_assert(dev_p->storage_cb != NULL ||
				dev_p->storage_cb_ms != NULL);
			cb = dev_p->storage_cb;
			cb_ms = dev_p->storage_cb_ms;

			dev_p->storage_cb = 0;
			dev_p->storage_cb_ms = 0;

			if (ret == CY_AS_ERROR_SUCCESS)
				ret = dev_p->storage_error;

		if (cb_ms) {
			cb_ms((cy_as_device_handle)dev_p,
				dev_p->storage_bus_index,
				dev_p->storage_device_index,
				dev_p->storage_unit,
				dev_p->storage_block_addr,
				dev_p->storage_oper, ret);
		} else {
			cb((cy_as_device_handle)dev_p,
				dev_p->storage_device_info
				[dev_p->storage_bus_index]
				[dev_p->storage_device_index].type,
				dev_p->storage_device_index,
				dev_p->storage_unit,
				dev_p->storage_block_addr,
				dev_p->storage_oper, ret);
		}
	} else
		dev_p->storage_error = ret;
}

static void
cy_as_async_storage_reply_callback(
					cy_as_device *dev_p,
					uint8_t context,
					cy_as_ll_request_response *rqt,
					cy_as_ll_request_response *resp,
					cy_as_return_status_t ret)
{
	cy_as_storage_callback_dep cb;
	cy_as_storage_callback cb_ms;
	uint8_t reqtype;
	(void)rqt;
	(void)context;

	reqtype = cy_as_ll_request_response__get_code(rqt);

	if (ret == CY_AS_ERROR_SUCCESS) {
		if (cy_as_ll_request_response__get_code(resp) ==
			CY_RESP_ANTIOCH_DEFERRED_ERROR) {
			ret = cy_as_ll_request_response__get_word
				(resp, 0) & 0x00FF;
		} else if (cy_as_ll_request_response__get_code(resp) !=
			CY_RESP_SUCCESS_FAILURE) {
			ret = CY_AS_ERROR_INVALID_RESPONSE;
		}
	}

	if (ret != CY_AS_ERROR_SUCCESS) {
		if (reqtype == CY_RQT_READ_BLOCK)
			cy_as_dma_cancel(dev_p,
				dev_p->storage_read_endpoint, ret);
		else
			cy_as_dma_cancel(dev_p,
				dev_p->storage_write_endpoint, ret);
	}

	dev_p->storage_wait = cy_false;

	/*
	* if the DMA callback has already been called, the
	* user callback has to be called from here.
	*/
	if (!cy_as_device_is_storage_async_pending(dev_p)) {
		cy_as_hal_assert(dev_p->storage_cb != NULL ||
			dev_p->storage_cb_ms != NULL);
		cb = dev_p->storage_cb;
		cb_ms = dev_p->storage_cb_ms;

		dev_p->storage_cb = 0;
		dev_p->storage_cb_ms = 0;

		if (ret == CY_AS_ERROR_SUCCESS)
			ret = dev_p->storage_error;

		if (cb_ms) {
			cb_ms((cy_as_device_handle)dev_p,
				dev_p->storage_bus_index,
				dev_p->storage_device_index,
				dev_p->storage_unit,
				dev_p->storage_block_addr,
				dev_p->storage_oper, ret);
		} else {
			cb((cy_as_device_handle)dev_p,
				dev_p->storage_device_info
				[dev_p->storage_bus_index]
				[dev_p->storage_device_index].type,
				dev_p->storage_device_index,
				dev_p->storage_unit,
				dev_p->storage_block_addr,
				dev_p->storage_oper, ret);
		}
	} else
		dev_p->storage_error = ret;
}

static cy_as_return_status_t
cy_as_storage_async_oper(cy_as_device *dev_p, cy_as_end_point_number_t ep,
		uint8_t reqtype, uint16_t req_flags, cy_as_bus_number_t bus,
		uint32_t device, uint32_t unit, uint32_t block, void *data_p,
		uint16_t num_blocks, cy_as_storage_callback_dep callback,
		cy_as_storage_callback callback_ms)
{
	uint32_t mask;
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (bus < 0 || bus >= CY_AS_MAX_BUSES)
		return CY_AS_ERROR_NO_SUCH_BUS;

	if (device >= CY_AS_MAX_STORAGE_DEVICES)
		return CY_AS_ERROR_NO_SUCH_DEVICE;

	if (unit > 255)
		return CY_AS_ERROR_NO_SUCH_UNIT;

	/* We are supposed to return success if the number of
	* blocks is zero
	*/
	if (num_blocks == 0) {
		if (callback_ms)
			callback_ms((cy_as_device_handle)dev_p,
				bus, device, unit, block,
				((reqtype == CY_RQT_WRITE_BLOCK)
				? cy_as_op_write : cy_as_op_read),
				CY_AS_ERROR_SUCCESS);
		else
			callback((cy_as_device_handle)dev_p,
				dev_p->storage_device_info[bus][device].type,
				device, unit, block,
				((reqtype == CY_RQT_WRITE_BLOCK) ?
					cy_as_op_write : cy_as_op_read),
				CY_AS_ERROR_SUCCESS);

		return CY_AS_ERROR_SUCCESS;
	}

	if (dev_p->storage_device_info[bus][device].block_size == 0)
			return CY_AS_ERROR_QUERY_DEVICE_NEEDED;

	/*
	* since async operations can be triggered by interrupt
	* code, we must insure that we do not get multiple
	* async operations going at one time and protect this
	* test and set operation from interrupts. also need to
	* check for pending async MTP writes
	*/
	mask = cy_as_hal_disable_interrupts();
	if ((cy_as_device_is_storage_async_pending(dev_p)) ||
	(dev_p->storage_wait) ||
	(cy_as_device_is_usb_async_pending(dev_p, 6))) {
		cy_as_hal_enable_interrupts(mask);
		return CY_AS_ERROR_ASYNC_PENDING;
	}

	cy_as_device_set_storage_async_pending(dev_p);
	cy_as_device_clear_p2s_dma_start_recvd(dev_p);
	cy_as_hal_enable_interrupts(mask);

	/*
	* storage information about the currently outstanding request
	*/
	dev_p->storage_cb = callback;
	dev_p->storage_cb_ms = callback_ms;
	dev_p->storage_bus_index = bus;
	dev_p->storage_device_index = device;
	dev_p->storage_unit = unit;
	dev_p->storage_block_addr = block;

	/* Initialise the request to send to the West Bridge. */
	req_p = dev_p->storage_rw_req_p;
	cy_as_ll_init_request(req_p, reqtype, CY_RQT_STORAGE_RQT_CONTEXT, 5);

	/* Initialise the space for reply from the West Bridge. */
	reply_p = dev_p->storage_rw_resp_p;
	cy_as_ll_init_response(reply_p, 5);

	/* Remember which version of the API originated the request */
	req_p->flags |= req_flags;

	/* Setup the DMA request and adjust the storage
	 * operation if we are reading */
	if (reqtype == CY_RQT_READ_BLOCK) {
		ret = cy_as_dma_queue_request(dev_p, ep, data_p,
			dev_p->storage_device_info[bus][device].block_size
			* num_blocks, cy_false, cy_true,
			cy_as_async_storage_callback);
		dev_p->storage_oper = cy_as_op_read;
	} else if (reqtype == CY_RQT_WRITE_BLOCK) {
		ret = cy_as_dma_queue_request(dev_p, ep, data_p,
			dev_p->storage_device_info[bus][device].block_size *
			num_blocks, cy_false, cy_false,
			cy_as_async_storage_callback);
		dev_p->storage_oper = cy_as_op_write;
	}

	if (ret != CY_AS_ERROR_SUCCESS) {
		cy_as_device_clear_storage_async_pending(dev_p);
		return ret;
	}

	cy_as_ll_request_response__set_word(req_p,
		0, create_address(bus, (uint8_t)device, (uint8_t)unit));
	cy_as_ll_request_response__set_word(req_p,
		1, (uint16_t)((block >> 16) & 0xffff));
	cy_as_ll_request_response__set_word(req_p,
		2, (uint16_t)(block & 0xffff));
	cy_as_ll_request_response__set_word(req_p,
		3, (uint16_t)((num_blocks >> 8) & 0x00ff));
	cy_as_ll_request_response__set_word(req_p,
		4, (uint16_t)((num_blocks << 8) & 0xff00));

	/* Set the burst mode flag. */
	if (dev_p->is_storage_only_mode)
		req_p->data[4] |= 0x0001;

	/* Send the request and wait for completion
	 * of storage request */
	dev_p->storage_wait = cy_true;
	ret = cy_as_ll_send_request(dev_p, req_p, reply_p,
		cy_true, cy_as_async_storage_reply_callback);
	if (ret != CY_AS_ERROR_SUCCESS) {
		cy_as_dma_cancel(dev_p, ep, CY_AS_ERROR_CANCELED);
		cy_as_device_clear_storage_async_pending(dev_p);
	}

	return ret;
}

static void
cy_as_sync_storage_callback(cy_as_device *dev_p,
	cy_as_end_point_number_t ep, void *buf_p,
	uint32_t size, cy_as_return_status_t err)
{
	(void)ep;
	(void)buf_p;
	(void)size;

	dev_p->storage_error = err;
}

static void
cy_as_sync_storage_reply_callback(
				cy_as_device *dev_p,
				uint8_t context,
				cy_as_ll_request_response *rqt,
				cy_as_ll_request_response *resp,
				cy_as_return_status_t ret)
{
	uint8_t reqtype;
	(void)rqt;

	reqtype = cy_as_ll_request_response__get_code(rqt);

	if (cy_as_ll_request_response__get_code(resp) ==
	CY_RESP_ANTIOCH_DEFERRED_ERROR) {
		ret = cy_as_ll_request_response__get_word(resp, 0) & 0x00FF;

		if (ret != CY_AS_ERROR_SUCCESS) {
			if (reqtype == CY_RQT_READ_BLOCK)
				cy_as_dma_cancel(dev_p,
					dev_p->storage_read_endpoint, ret);
			else
				cy_as_dma_cancel(dev_p,
					dev_p->storage_write_endpoint, ret);
		}
	} else if (cy_as_ll_request_response__get_code(resp) !=
	CY_RESP_SUCCESS_FAILURE) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
	}

	dev_p->storage_wait = cy_false;
	dev_p->storage_error = ret;

	/* Wake any threads/processes that are waiting on
	 * the read/write completion. */
	cy_as_hal_wake(&dev_p->context[context]->channel);
}

static cy_as_return_status_t
cy_as_storage_sync_oper(cy_as_device *dev_p,
	cy_as_end_point_number_t ep, uint8_t reqtype,
	cy_as_bus_number_t bus, uint32_t device,
	uint32_t unit, uint32_t block, void *data_p,
	uint16_t num_blocks)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	cy_as_context *ctxt_p;
	uint32_t loopcount = 200;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (bus < 0 || bus >= CY_AS_MAX_BUSES)
		return CY_AS_ERROR_NO_SUCH_BUS;

	if (device >= CY_AS_MAX_STORAGE_DEVICES)
		return CY_AS_ERROR_NO_SUCH_DEVICE;

	if (unit > 255)
		return CY_AS_ERROR_NO_SUCH_UNIT;

	if ((cy_as_device_is_storage_async_pending(dev_p)) ||
		(dev_p->storage_wait))
		return CY_AS_ERROR_ASYNC_PENDING;

	/* Also need to check for pending Async MTP writes */
	if (cy_as_device_is_usb_async_pending(dev_p, 6))
		return CY_AS_ERROR_ASYNC_PENDING;

	/* We are supposed to return success if the number of
	* blocks is zero
	*/
	if (num_blocks == 0)
		return CY_AS_ERROR_SUCCESS;

	if (dev_p->storage_device_info[bus][device].block_size == 0) {
		/*
		* normally, a given device has been queried via
		* the query device call before a read request is issued.
		* therefore, this normally will not be run.
		*/
		ret = cy_as_get_block_size(dev_p, bus, device, 0);
		if (ret != CY_AS_ERROR_SUCCESS)
			return ret;
	}

	/* Initialise the request to send to the West Bridge. */
	req_p = dev_p->storage_rw_req_p;
	cy_as_ll_init_request(req_p, reqtype,
		CY_RQT_STORAGE_RQT_CONTEXT, 5);

	/* Initialise the space for reply from
	 * the West Bridge. */
	reply_p = dev_p->storage_rw_resp_p;
	cy_as_ll_init_response(reply_p, 5);
	cy_as_device_clear_p2s_dma_start_recvd(dev_p);

	/* Setup the DMA request */
	if (reqtype == CY_RQT_READ_BLOCK) {
		ret = cy_as_dma_queue_request(dev_p, ep, data_p,
			dev_p->storage_device_info[bus][device].block_size *
			num_blocks, cy_false,
			cy_true, cy_as_sync_storage_callback);
		dev_p->storage_oper = cy_as_op_read;
	} else if (reqtype == CY_RQT_WRITE_BLOCK) {
		ret = cy_as_dma_queue_request(dev_p, ep, data_p,
			dev_p->storage_device_info[bus][device].block_size *
			num_blocks, cy_false, cy_false,
			cy_as_sync_storage_callback);
		dev_p->storage_oper = cy_as_op_write;
	}

	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, (uint8_t)device, (uint8_t)unit));
	cy_as_ll_request_response__set_word(req_p, 1,
		(uint16_t)((block >> 16) & 0xffff));
	cy_as_ll_request_response__set_word(req_p, 2,
		(uint16_t)(block & 0xffff));
	cy_as_ll_request_response__set_word(req_p, 3,
		(uint16_t)((num_blocks >> 8) & 0x00ff));
	cy_as_ll_request_response__set_word(req_p, 4,
		(uint16_t)((num_blocks << 8) & 0xff00));

	/* Set the burst mode flag. */
	if (dev_p->is_storage_only_mode)
		req_p->data[4] |= 0x0001;

	/* Send the request and wait for
	 * completion of storage request */
	dev_p->storage_wait = cy_true;
	ret = cy_as_ll_send_request(dev_p, req_p, reply_p, cy_true,
		cy_as_sync_storage_reply_callback);
	if (ret != CY_AS_ERROR_SUCCESS) {
		cy_as_dma_cancel(dev_p, ep, CY_AS_ERROR_CANCELED);
	} else {
		/* Setup the DMA request */
		ctxt_p = dev_p->context[CY_RQT_STORAGE_RQT_CONTEXT];
		ret = cy_as_dma_drain_queue(dev_p, ep, cy_false);

		while (loopcount-- > 0) {
			if (dev_p->storage_wait == cy_false)
				break;
			cy_as_hal_sleep_on(&ctxt_p->channel, 10);
		}

		if (dev_p->storage_wait == cy_true) {
			dev_p->storage_wait = cy_false;
			cy_as_ll_remove_request(dev_p, ctxt_p, req_p, cy_true);
			ret = CY_AS_ERROR_TIMEOUT;
		}

		if (ret == CY_AS_ERROR_SUCCESS)
			ret = dev_p->storage_error;
	}

	return ret;
}

cy_as_return_status_t
cy_as_storage_read(cy_as_device_handle handle,
	cy_as_bus_number_t bus, uint32_t device,
	uint32_t unit, uint32_t block,
	void *data_p, uint16_t num_blocks)
{
	cy_as_device *dev_p = (cy_as_device *)handle;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	return cy_as_storage_sync_oper(dev_p, dev_p->storage_read_endpoint,
		CY_RQT_READ_BLOCK, bus, device,
		unit, block, data_p, num_blocks);
}
EXPORT_SYMBOL(cy_as_storage_read);

cy_as_return_status_t
cy_as_storage_write(cy_as_device_handle handle,
	cy_as_bus_number_t bus, uint32_t device,
	uint32_t unit, uint32_t block, void *data_p,
	uint16_t num_blocks)
{
	cy_as_device *dev_p = (cy_as_device *)handle;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	if (dev_p->mtp_turbo_active)
		return CY_AS_ERROR_NOT_VALID_DURING_MTP;

	return cy_as_storage_sync_oper(dev_p,
		dev_p->storage_write_endpoint,
		CY_RQT_WRITE_BLOCK, bus, device,
		unit, block, data_p, num_blocks);
}
EXPORT_SYMBOL(cy_as_storage_write);

cy_as_return_status_t
cy_as_storage_read_async(cy_as_device_handle handle,
	cy_as_bus_number_t bus, uint32_t device, uint32_t unit,
	uint32_t block, void *data_p, uint16_t num_blocks,
	cy_as_storage_callback callback)
{
	cy_as_device *dev_p = (cy_as_device *)handle;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	if (callback == 0)
		return CY_AS_ERROR_NULL_CALLBACK;

	return cy_as_storage_async_oper(dev_p,
		dev_p->storage_read_endpoint, CY_RQT_READ_BLOCK,
		CY_AS_REQUEST_RESPONSE_MS, bus, device, unit,
		block, data_p, num_blocks, NULL, callback);
}
EXPORT_SYMBOL(cy_as_storage_read_async);

cy_as_return_status_t
cy_as_storage_write_async(cy_as_device_handle handle,
	cy_as_bus_number_t bus, uint32_t device, uint32_t unit,
	uint32_t block, void *data_p, uint16_t num_blocks,
	cy_as_storage_callback callback)
{
	cy_as_device *dev_p = (cy_as_device *)handle;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	if (callback == 0)
		return CY_AS_ERROR_NULL_CALLBACK;

	if (dev_p->mtp_turbo_active)
		return CY_AS_ERROR_NOT_VALID_DURING_MTP;

	return cy_as_storage_async_oper(dev_p,
		dev_p->storage_write_endpoint, CY_RQT_WRITE_BLOCK,
		CY_AS_REQUEST_RESPONSE_MS, bus, device, unit, block,
		data_p, num_blocks, NULL, callback);
}
EXPORT_SYMBOL(cy_as_storage_write_async);

static void
my_storage_cancel_callback(
		cy_as_device *dev_p,
		uint8_t context,
		cy_as_ll_request_response *rqt,
		cy_as_ll_request_response *resp,
		cy_as_return_status_t stat)
{
	(void)context;
	(void)stat;

	/* Nothing to do here, except free up the
	 * request and response structures. */
	cy_as_ll_destroy_response(dev_p, resp);
	cy_as_ll_destroy_request(dev_p, rqt);
}


cy_as_return_status_t
cy_as_storage_cancel_async(cy_as_device_handle handle)
{
	cy_as_return_status_t ret;
	cy_as_ll_request_response *req_p , *reply_p;

	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (!cy_as_device_is_storage_async_pending(dev_p))
		return CY_AS_ERROR_ASYNC_NOT_PENDING;

	/*
	 * create and send a mailbox request to firmware
	 * asking it to abort processing of the current
	 * P2S operation. the rest of the cancel processing will be
	 * driven through the callbacks for the read/write call.
	 */
	req_p = cy_as_ll_create_request(dev_p, CY_RQT_ABORT_P2S_XFER,
		CY_RQT_GENERAL_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	ret = cy_as_ll_send_request(dev_p, req_p,
		reply_p, cy_false, my_storage_cancel_callback);
	if (ret) {
		cy_as_ll_destroy_request(dev_p, req_p);
		cy_as_ll_destroy_response(dev_p, reply_p);
	}

	return CY_AS_ERROR_SUCCESS;
}
EXPORT_SYMBOL(cy_as_storage_cancel_async);

/*
 * This function does all the API side clean-up associated with
 * CyAsStorageStop, without any communication with the firmware.
 */
void cy_as_storage_cleanup(cy_as_device *dev_p)
{
	if (dev_p->storage_count) {
		cy_as_ll_destroy_request(dev_p, dev_p->storage_rw_req_p);
		cy_as_ll_destroy_response(dev_p, dev_p->storage_rw_resp_p);
		dev_p->storage_count = 0;
		cy_as_device_clear_scsi_messages(dev_p);
		cy_as_hal_mem_set(dev_p->storage_device_info,
			0, sizeof(dev_p->storage_device_info));

		cy_as_device_clear_storage_async_pending(dev_p);
		dev_p->storage_cb = 0;
		dev_p->storage_cb_ms = 0;
		dev_p->storage_wait = cy_false;
	}
}

static cy_as_return_status_t
my_handle_response_sd_reg_read(
		cy_as_device			   *dev_p,
		cy_as_ll_request_response	*req_p,
		cy_as_ll_request_response	*reply_p,
		cy_as_storage_sd_reg_read_data *info)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint8_t  resp_type, i;
	uint16_t resp_len;
	uint8_t  length = info->length;
	uint8_t *data_p = info->buf_p;

	resp_type = cy_as_ll_request_response__get_code(reply_p);
	if (resp_type == CY_RESP_SD_REGISTER_DATA) {
		uint16_t *resp_p = reply_p->data + 1;
		uint16_t temp;

		resp_len = cy_as_ll_request_response__get_word(reply_p, 0);
		cy_as_hal_assert(resp_len >= length);

		/*
		 * copy the values into the output buffer after doing the
		 * necessary bit shifting. the bit shifting is required because
		 * the data comes out of the west bridge with a 6 bit offset.
		 */
		i = 0;
		while (length) {
			temp = ((resp_p[i] << 6) | (resp_p[i + 1] >> 10));
			i++;

			*data_p++ = (uint8_t)(temp >> 8);
			length--;

			if (length) {
				*data_p++ = (uint8_t)(temp & 0xFF);
				length--;
			}
		}
	} else {
		if (resp_type == CY_RESP_SUCCESS_FAILURE)
			ret = cy_as_ll_request_response__get_word(reply_p, 0);
		else
			ret = CY_AS_ERROR_INVALID_RESPONSE;
	}

	cy_as_ll_destroy_response(dev_p, reply_p);
	cy_as_ll_destroy_request(dev_p, req_p);

	return ret;
}

cy_as_return_status_t
cy_as_storage_sd_register_read(
		cy_as_device_handle		  handle,
		cy_as_bus_number_t		   bus,
		uint8_t				   device,
		cy_as_sd_card_reg_type		 reg_type,
		cy_as_storage_sd_reg_read_data *data_p,
		cy_as_function_callback	  cb,
		uint32_t				  client)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint8_t  length;

	/*
	 * sanity checks required before sending the request to the
	 * firmware.
	 */
	cy_as_device *dev_p = (cy_as_device *)handle;
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (device >= CY_AS_MAX_STORAGE_DEVICES)
		return CY_AS_ERROR_NO_SUCH_DEVICE;

	if (reg_type > cy_as_sd_reg_CSD)
		return CY_AS_ERROR_INVALID_PARAMETER;

	/* If SD/MMC media is not supported on the
	 * addressed bus, return error. */
	if ((dev_p->media_supported[bus] & (1 << cy_as_media_sd_flash)) == 0)
		return CY_AS_ERROR_INVALID_PARAMETER;

	/*
	 * find the amount of data to be returned. this will be the minimum of
	 * the actual data length, and the length requested.
	 */
	switch (reg_type) {
	case cy_as_sd_reg_OCR:
		length = CY_AS_SD_REG_OCR_LENGTH;
		break;
	case cy_as_sd_reg_CID:
		length = CY_AS_SD_REG_CID_LENGTH;
		break;
	case cy_as_sd_reg_CSD:
		length = CY_AS_SD_REG_CSD_LENGTH;
		break;

	default:
		length = 0;
		cy_as_hal_assert(0);
	}

	if (length < data_p->length)
		data_p->length = length;
	length = data_p->length;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p, CY_RQT_SD_REGISTER_READ,
		CY_RQT_STORAGE_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	cy_as_ll_request_response__set_word(req_p, 0,
		(create_address(bus, device, 0) | (uint16_t)reg_type));

	reply_p = cy_as_ll_create_response(dev_p,
		CY_AS_SD_REG_MAX_RESP_LENGTH);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return my_handle_response_sd_reg_read(dev_p,
			req_p, reply_p, data_p);
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_STOR_SDREGISTERREAD, data_p,
			dev_p->func_cbs_stor, CY_AS_REQUEST_RESPONSE_EX,
			req_p, reply_p, cy_as_storage_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		/* The request and response are freed as part of the
		 * MiscFuncCallback */
		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}
EXPORT_SYMBOL(cy_as_storage_sd_register_read);

cy_as_return_status_t
cy_as_storage_create_p_partition(
		/* Handle to the device of interest */
		cy_as_device_handle		handle,
		cy_as_bus_number_t		 bus,
		uint32_t				device,
		/* of P-port only partition in blocks */
		uint32_t				size,
		cy_as_function_callback	cb,
		uint32_t				client)
{
	cy_as_ll_request_response *req_p, *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	cy_as_device *dev_p = (cy_as_device *)handle;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	/* Partitions cannot be created or deleted while
	 * the USB stack is active. */
	if (dev_p->usb_count)
		return CY_AS_ERROR_USB_RUNNING;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p, CY_RQT_PARTITION_STORAGE,
		CY_RQT_STORAGE_RQT_CONTEXT, 3);

	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/* Reserve space for the reply, the reply
	 * data will not exceed one word */
	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}
	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, (uint8_t)device, 0x00));
	cy_as_ll_request_response__set_word(req_p, 1,
		(uint16_t)((size >> 16) & 0xffff));
	cy_as_ll_request_response__set_word(req_p, 2,
		(uint16_t)(size & 0xffff));

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return my_handle_response_no_data(dev_p, req_p, reply_p);
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_STOR_PARTITION, 0, dev_p->func_cbs_stor,
			CY_AS_REQUEST_RESPONSE_EX, req_p, reply_p,
			cy_as_storage_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		/* The request and response are freed as part of the
		 * FuncCallback */
		return ret;

	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}
EXPORT_SYMBOL(cy_as_storage_create_p_partition);

cy_as_return_status_t
cy_as_storage_remove_p_partition(
		cy_as_device_handle		handle,
		cy_as_bus_number_t		 bus,
		uint32_t				device,
		cy_as_function_callback	cb,
		uint32_t				client)
{
	cy_as_ll_request_response *req_p, *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	cy_as_device *dev_p = (cy_as_device *)handle;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	/* Partitions cannot be created or deleted while
	 * the USB stack is active. */
	if (dev_p->usb_count)
		return CY_AS_ERROR_USB_RUNNING;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p, CY_RQT_PARTITION_ERASE,
		CY_RQT_STORAGE_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/* Reserve space for the reply, the reply
	 * data will not exceed one word */
	reply_p = cy_as_ll_create_response(dev_p, 1);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	cy_as_ll_request_response__set_word(req_p,
		0, create_address(bus, (uint8_t)device, 0x00));

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return my_handle_response_no_data(dev_p, req_p, reply_p);
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_NODATA, 0, dev_p->func_cbs_stor,
			CY_AS_REQUEST_RESPONSE_EX, req_p, reply_p,
			cy_as_storage_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		/* The request and response are freed
		 * as part of the FuncCallback */
		return ret;

	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}
EXPORT_SYMBOL(cy_as_storage_remove_p_partition);

static cy_as_return_status_t
my_handle_response_get_transfer_amount(cy_as_device *dev_p,
				cy_as_ll_request_response *req_p,
				cy_as_ll_request_response *reply_p,
				cy_as_m_s_c_progress_data *data)
{
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint8_t code = cy_as_ll_request_response__get_code(reply_p);
	uint16_t v1, v2;

	if (code != CY_RESP_TRANSFER_COUNT) {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		goto destroy;
	}

	v1 = cy_as_ll_request_response__get_word(reply_p, 0);
	v2 = cy_as_ll_request_response__get_word(reply_p, 1);
	data->wr_count = (uint32_t)((v1 << 16) | v2);

	v1 = cy_as_ll_request_response__get_word(reply_p, 2);
	v2 = cy_as_ll_request_response__get_word(reply_p, 3);
	data->rd_count = (uint32_t)((v1 << 16) | v2);

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

cy_as_return_status_t
cy_as_storage_get_transfer_amount(
		cy_as_device_handle handle,
		cy_as_bus_number_t  bus,
		uint32_t device,
		cy_as_m_s_c_progress_data *data_p,
		cy_as_function_callback cb,
		uint32_t client
	)
{
	cy_as_ll_request_response *req_p, *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	cy_as_device *dev_p = (cy_as_device *)handle;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	/* Check if the firmware image supports this feature. */
	if ((dev_p->media_supported[0]) && (dev_p->media_supported[0]
	== (1 << cy_as_media_nand)))
		return CY_AS_ERROR_NOT_SUPPORTED;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p, CY_RQT_GET_TRANSFER_AMOUNT,
		CY_RQT_STORAGE_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/* Reserve space for the reply, the reply data
	 * will not exceed four words. */
	reply_p = cy_as_ll_create_response(dev_p, 4);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, (uint8_t)device, 0x00));

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		return my_handle_response_get_transfer_amount(dev_p,
			req_p, reply_p, data_p);
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
		CY_FUNCT_CB_STOR_GETTRANSFERAMOUNT, (void *)data_p,
		dev_p->func_cbs_stor, CY_AS_REQUEST_RESPONSE_EX,
		req_p, reply_p, cy_as_storage_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		/* The request and response are freed as part of the
		 * FuncCallback */
		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;

}
EXPORT_SYMBOL(cy_as_storage_get_transfer_amount);

cy_as_return_status_t
cy_as_storage_erase(
		cy_as_device_handle		handle,
		cy_as_bus_number_t		 bus,
		uint32_t				device,
		uint32_t				erase_unit,
		uint16_t				num_erase_units,
		cy_as_function_callback	cb,
		uint32_t				client
		)
{
	cy_as_ll_request_response *req_p, *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	cy_as_device *dev_p = (cy_as_device *)handle;

	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	ret = is_storage_active(dev_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (bus < 0 || bus >= CY_AS_MAX_BUSES)
		return CY_AS_ERROR_NO_SUCH_BUS;

	if (device >= CY_AS_MAX_STORAGE_DEVICES)
		return CY_AS_ERROR_NO_SUCH_DEVICE;

	if (dev_p->storage_device_info[bus][device].block_size == 0)
		return CY_AS_ERROR_QUERY_DEVICE_NEEDED;

	/* If SD is not supported on the specified bus, then return ERROR */
	if (dev_p->storage_device_info[bus][device].type !=
		cy_as_media_sd_flash)
		return CY_AS_ERROR_NOT_SUPPORTED;

	if (num_erase_units == 0)
		return CY_AS_ERROR_SUCCESS;

	/* Create the request to send to the West Bridge device */
	req_p = cy_as_ll_create_request(dev_p, CY_RQT_ERASE,
		CY_RQT_STORAGE_RQT_CONTEXT, 5);

	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/* Reserve space for the reply, the reply
	 * data will not exceed four words. */
	reply_p = cy_as_ll_create_response(dev_p, 4);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, (uint8_t)device, 0x00));
	cy_as_ll_request_response__set_word(req_p, 1,
		(uint16_t)((erase_unit >> 16) & 0xffff));
	cy_as_ll_request_response__set_word(req_p, 2,
		(uint16_t)(erase_unit & 0xffff));
	cy_as_ll_request_response__set_word(req_p, 3,
		(uint16_t)((num_erase_units >> 8) & 0x00ff));
	cy_as_ll_request_response__set_word(req_p, 4,
		(uint16_t)((num_erase_units << 8) & 0xff00));

	if (cb == 0) {
		ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		ret = my_handle_response_no_data(dev_p, req_p, reply_p);

		/* If error = "invalid response", this (very likely) means
		 * that we are not using the SD-only firmware module which
		 * is the only one supporting storage_erase. in this case
		 * force a "non supported" error code */
		if (ret == CY_AS_ERROR_INVALID_RESPONSE)
			ret = CY_AS_ERROR_NOT_SUPPORTED;

		return ret;
	} else {
		ret = cy_as_misc_send_request(dev_p, cb, client,
			CY_FUNCT_CB_STOR_ERASE, 0, dev_p->func_cbs_stor,
			CY_AS_REQUEST_RESPONSE_EX, req_p, reply_p,
			cy_as_storage_func_callback);

		if (ret != CY_AS_ERROR_SUCCESS)
			goto destroy;

		/* The request and response are freed
		 * as part of the FuncCallback */
		return ret;
	}

destroy:
	cy_as_ll_destroy_request(dev_p, req_p);
	cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}
EXPORT_SYMBOL(cy_as_storage_erase);

static void
cy_as_storage_func_callback(cy_as_device *dev_p,
						uint8_t context,
						cy_as_ll_request_response *rqt,
						cy_as_ll_request_response *resp,
						cy_as_return_status_t stat)
{
	cy_as_func_c_b_node *node = (cy_as_func_c_b_node *)
		dev_p->func_cbs_stor->head_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	cy_bool	ex_request = (rqt->flags & CY_AS_REQUEST_RESPONSE_EX)
			== CY_AS_REQUEST_RESPONSE_EX;
	cy_bool	ms_request = (rqt->flags & CY_AS_REQUEST_RESPONSE_MS)
			== CY_AS_REQUEST_RESPONSE_MS;
	uint8_t	code;
	uint8_t	cntxt;

	cy_as_hal_assert(ex_request || ms_request);
	cy_as_hal_assert(dev_p->func_cbs_stor->count != 0);
	cy_as_hal_assert(dev_p->func_cbs_stor->type == CYAS_FUNC_CB);
	(void) ex_request;
	(void) ms_request;

	(void)context;

	cntxt = cy_as_ll_request_response__get_context(rqt);
	cy_as_hal_assert(cntxt == CY_RQT_STORAGE_RQT_CONTEXT);

	code = cy_as_ll_request_response__get_code(rqt);
	switch (code) {
	case CY_RQT_START_STORAGE:
		ret = my_handle_response_storage_start(dev_p, rqt, resp, stat);
		break;
	case CY_RQT_STOP_STORAGE:
		ret = my_handle_response_storage_stop(dev_p, rqt, resp, stat);
		break;
	case CY_RQT_CLAIM_STORAGE:
		ret = my_handle_response_storage_claim(dev_p, rqt, resp);
		break;
	case CY_RQT_RELEASE_STORAGE:
		ret = my_handle_response_storage_release(dev_p, rqt, resp);
		break;
	case CY_RQT_QUERY_MEDIA:
		cy_as_hal_assert(cy_false);/* Not used any more. */
		break;
	case CY_RQT_QUERY_BUS:
		cy_as_hal_assert(node->data != 0);
		ret = my_handle_response_storage_query_bus(dev_p,
			rqt, resp, (uint32_t *)node->data);
		break;
	case CY_RQT_QUERY_DEVICE:
		cy_as_hal_assert(node->data != 0);
		ret = my_handle_response_storage_query_device(dev_p,
			rqt, resp, node->data);
		break;
	case CY_RQT_QUERY_UNIT:
		cy_as_hal_assert(node->data != 0);
		ret = my_handle_response_storage_query_unit(dev_p,
			rqt, resp, node->data);
		break;
	case CY_RQT_SD_INTERFACE_CONTROL:
		ret = my_handle_response_no_data(dev_p, rqt, resp);
		break;
	case CY_RQT_SD_REGISTER_READ:
		cy_as_hal_assert(node->data != 0);
		ret = my_handle_response_sd_reg_read(dev_p, rqt, resp,
			(cy_as_storage_sd_reg_read_data *)node->data);
		break;
	case CY_RQT_PARTITION_STORAGE:
		ret = my_handle_response_no_data(dev_p, rqt, resp);
		break;
	case CY_RQT_PARTITION_ERASE:
		ret = my_handle_response_no_data(dev_p, rqt, resp);
		break;
	case CY_RQT_GET_TRANSFER_AMOUNT:
		cy_as_hal_assert(node->data != 0);
		ret = my_handle_response_get_transfer_amount(dev_p,
			rqt, resp, (cy_as_m_s_c_progress_data *)node->data);
		break;
	case CY_RQT_ERASE:
		ret = my_handle_response_no_data(dev_p, rqt, resp);

		/* If error = "invalid response", this (very likely)
		 * means that we are not using the SD-only firmware
		 * module which is the only one supporting storage_erase.
		 * in this case force a "non supported" error code */
		if (ret == CY_AS_ERROR_INVALID_RESPONSE)
			ret = CY_AS_ERROR_NOT_SUPPORTED;

		break;

	default:
		ret = CY_AS_ERROR_INVALID_RESPONSE;
		cy_as_hal_assert(cy_false);
		break;
	}

	/*
	 * if the low level layer returns a direct error, use the
	 * corresponding error code. if not, use the error code
	 * based on the response from firmware.
	 */
	if (stat == CY_AS_ERROR_SUCCESS)
		stat = ret;

	/* Call the user callback, if there is one */
	if (node->cb_p)
		node->cb_p((cy_as_device_handle)dev_p, stat,
			node->client_data, node->data_type, node->data);
	cy_as_remove_c_b_node(dev_p->func_cbs_stor);
}


static void
cy_as_sdio_sync_reply_callback(
		cy_as_device *dev_p,
		uint8_t context,
		cy_as_ll_request_response *rqt,
		cy_as_ll_request_response *resp,
		cy_as_return_status_t ret)
{
	(void)rqt;

	if ((cy_as_ll_request_response__get_code(resp) ==
	CY_RESP_SDIO_GET_TUPLE) ||
	(cy_as_ll_request_response__get_code(resp) ==
	CY_RESP_SDIO_EXT)) {
		ret = cy_as_ll_request_response__get_word(resp, 0);
		if ((ret & 0x00FF) != CY_AS_ERROR_SUCCESS) {
			if (cy_as_ll_request_response__get_code(rqt) ==
			CY_RQT_SDIO_READ_EXTENDED)
				cy_as_dma_cancel(dev_p,
					dev_p->storage_read_endpoint, ret);
			else
				cy_as_dma_cancel(dev_p,
					dev_p->storage_write_endpoint, ret);
		}
	} else {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
	}

	dev_p->storage_rw_resp_p = resp;
	dev_p->storage_wait = cy_false;
	if (((ret & 0x00FF) == CY_AS_ERROR_IO_ABORTED) || ((ret & 0x00FF)
	== CY_AS_ERROR_IO_SUSPENDED))
		dev_p->storage_error =  (ret & 0x00FF);
	else
		dev_p->storage_error = (ret & 0x00FF) ?
		CY_AS_ERROR_INVALID_RESPONSE : CY_AS_ERROR_SUCCESS;

	/* Wake any threads/processes that are waiting on
	 * the read/write completion. */
	cy_as_hal_wake(&dev_p->context[context]->channel);
}

cy_as_return_status_t
cy_as_sdio_device_check(
		cy_as_device *dev_p,
		cy_as_bus_number_t	 bus,
		uint32_t			device)
{
	if (!dev_p || (dev_p->sig != CY_AS_DEVICE_HANDLE_SIGNATURE))
		return CY_AS_ERROR_INVALID_HANDLE;

	if (bus < 0 || bus >= CY_AS_MAX_BUSES)
		return CY_AS_ERROR_NO_SUCH_BUS;

	if (device >= CY_AS_MAX_STORAGE_DEVICES)
		return CY_AS_ERROR_NO_SUCH_DEVICE;

	if (!cy_as_device_is_astoria_dev(dev_p))
		return CY_AS_ERROR_NOT_SUPPORTED;

	return  (is_storage_active(dev_p));
}

cy_as_return_status_t
cy_as_sdio_direct_io(
		cy_as_device_handle	handle,
		cy_as_bus_number_t	 bus,
		uint32_t			device,
		uint8_t			 n_function_no,
		uint32_t			address,
		uint8_t			 misc_buf,
		uint16_t			argument,
		uint8_t			 is_write,
		uint8_t *data_p)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint16_t resp_data;

	/*
	 * sanity checks required before sending the request to the
	 * firmware.
	 */
	cy_as_device *dev_p = (cy_as_device *)handle;
	ret = cy_as_sdio_device_check(dev_p, bus, device);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;


	if (!(cy_as_sdio_check_function_initialized(handle,
		bus, n_function_no)))
		return CY_AS_ERROR_INVALID_FUNCTION;
	if (cy_as_sdio_check_function_suspended(handle, bus, n_function_no))
		return CY_AS_ERROR_FUNCTION_SUSPENDED;

	req_p = cy_as_ll_create_request(dev_p, (is_write == cy_true) ?
		CY_RQT_SDIO_WRITE_DIRECT : CY_RQT_SDIO_READ_DIRECT,
			CY_RQT_STORAGE_RQT_CONTEXT, 3);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/*Setting up request*/

	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, (uint8_t)device, n_function_no));
	/* D1 */
	if (is_write == cy_true) {
		cy_as_ll_request_response__set_word(req_p, 1,
			((argument<<8) | 0x0080 | (n_function_no<<4) |
			((misc_buf&CY_SDIO_RAW)<<3) |
			((misc_buf&CY_SDIO_REARM_INT)>>5) |
			(uint16_t)(address>>15)));
	} else {
		cy_as_ll_request_response__set_word(req_p, 1,
			(n_function_no<<4) | ((misc_buf&CY_SDIO_REARM_INT)>>5) |
			(uint16_t)(address>>15));
	}
	/* D2 */
	cy_as_ll_request_response__set_word(req_p, 2,
		((uint16_t)((address&0x00007fff)<<1)));

	/*Create response*/
	reply_p = cy_as_ll_create_response(dev_p, 2);

	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	/*Sending the request*/
	ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	/*Check reply type*/
	if (cy_as_ll_request_response__get_code(reply_p) ==
	CY_RESP_SDIO_DIRECT) {
		resp_data = cy_as_ll_request_response__get_word(reply_p, 0);
		if (resp_data >> 8)
			ret = CY_AS_ERROR_INVALID_RESPONSE;
		else if (data_p != 0)
			*(uint8_t *)(data_p) = (uint8_t)(resp_data&0x00ff);
	} else {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
	}

destroy:
	if (req_p != 0)
		cy_as_ll_destroy_request(dev_p, req_p);
	if (reply_p != 0)
		cy_as_ll_destroy_response(dev_p, reply_p);
	return ret;
}


cy_as_return_status_t
cy_as_sdio_direct_read(
		cy_as_device_handle handle,
		cy_as_bus_number_t bus,
		uint32_t device,
		uint8_t	n_function_no,
		uint32_t address,
		uint8_t	misc_buf,
		uint8_t *data_p)
{
	return cy_as_sdio_direct_io(handle, bus, device, n_function_no,
		address, misc_buf, 0x00, cy_false, data_p);
}
EXPORT_SYMBOL(cy_as_sdio_direct_read);

cy_as_return_status_t
cy_as_sdio_direct_write(
		cy_as_device_handle		handle,
		cy_as_bus_number_t		 bus,
		uint32_t				device,
		uint8_t				 n_function_no,
		uint32_t				address,
		uint8_t				 misc_buf,
		uint16_t				argument,
		uint8_t *data_p)
{
	return cy_as_sdio_direct_io(handle, bus, device, n_function_no,
		address, misc_buf, argument, cy_true, data_p);
}
EXPORT_SYMBOL(cy_as_sdio_direct_write);

/*Cmd53 IO*/
cy_as_return_status_t
cy_as_sdio_extended_i_o(
		cy_as_device_handle		handle,
		cy_as_bus_number_t		 bus,
		uint32_t				device,
		uint8_t				 n_function_no,
		uint32_t				address,
		uint8_t				 misc_buf,
		uint16_t				argument,
		uint8_t				 is_write,
		uint8_t *data_p ,
		uint8_t				 is_resume)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint8_t resp_type;
	uint8_t reqtype;
	uint16_t resp_data;
	cy_as_context *ctxt_p;
	uint32_t  dmasize, loopcount = 200;
	cy_as_end_point_number_t ep;

	cy_as_device *dev_p = (cy_as_device *)handle;
	ret = cy_as_sdio_device_check(dev_p, bus, device);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (!(cy_as_sdio_check_function_initialized(handle,
		bus, n_function_no)))
		return CY_AS_ERROR_INVALID_FUNCTION;
	if (cy_as_sdio_check_function_suspended(handle, bus, n_function_no))
		return CY_AS_ERROR_FUNCTION_SUSPENDED;


	if ((cy_as_device_is_storage_async_pending(dev_p)) ||
	(dev_p->storage_wait))
		return CY_AS_ERROR_ASYNC_PENDING;

	/* Request for 0 bytes of blocks is returned as a success*/
	if (argument == 0)
		return CY_AS_ERROR_SUCCESS;

	/* Initialise the request to send to the West Bridge device. */
	if (is_write == cy_true) {
		reqtype = CY_RQT_SDIO_WRITE_EXTENDED;
		ep = dev_p->storage_write_endpoint;
	} else {
		reqtype = CY_RQT_SDIO_READ_EXTENDED;
		ep = dev_p->storage_read_endpoint;
	}

	req_p = dev_p->storage_rw_req_p;
	cy_as_ll_init_request(req_p, reqtype, CY_RQT_STORAGE_RQT_CONTEXT, 3);

	/* Initialise the space for reply from the Antioch. */
	reply_p = dev_p->storage_rw_resp_p;
	cy_as_ll_init_response(reply_p, 2);

	/* Setup the DMA request */
	if (!(misc_buf&CY_SDIO_BLOCKMODE)) {
		if (argument >
			dev_p->sdiocard[bus].
			function[n_function_no-1].blocksize)
			return CY_AS_ERROR_INVALID_BLOCKSIZE;

	} else {
		if (argument > 511)
			return CY_AS_ERROR_INVALID_BLOCKSIZE;
	}

	if (argument == 512)
		argument = 0;

	dmasize = ((misc_buf&CY_SDIO_BLOCKMODE) != 0) ?
		dev_p->sdiocard[bus].function[n_function_no-1].blocksize
		* argument : argument;

	ret = cy_as_dma_queue_request(dev_p, ep, (void *)(data_p),
		dmasize, cy_false, (is_write & cy_true) ? cy_false :
		cy_true, cy_as_sync_storage_callback);

	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, (uint8_t)device,
		n_function_no | ((is_resume) ? 0x80 : 0x00)));
	cy_as_ll_request_response__set_word(req_p, 1,
		((uint16_t)n_function_no)<<12|
		((uint16_t)(misc_buf & (CY_SDIO_BLOCKMODE|CY_SDIO_OP_INCR)))
		<< 9 | (uint16_t)(address >> 7) |
		((is_write == cy_true) ? 0x8000 : 0x0000));
	cy_as_ll_request_response__set_word(req_p, 2,
		((uint16_t)(address&0x0000ffff) << 9) |  argument);


	/* Send the request and wait for completion of storage request */
	dev_p->storage_wait = cy_true;
	ret = cy_as_ll_send_request(dev_p, req_p, reply_p,
		cy_true, cy_as_sdio_sync_reply_callback);

	if (ret != CY_AS_ERROR_SUCCESS) {
		cy_as_dma_cancel(dev_p, ep, CY_AS_ERROR_CANCELED);
	} else {
		/* Setup the DMA request */
		ctxt_p = dev_p->context[CY_RQT_STORAGE_RQT_CONTEXT];
		ret = cy_as_dma_drain_queue(dev_p, ep, cy_true);

		while (loopcount-- > 0) {
			if (dev_p->storage_wait == cy_false)
				break;
			cy_as_hal_sleep_on(&ctxt_p->channel, 10);
		}
		if (dev_p->storage_wait == cy_true) {
			dev_p->storage_wait = cy_false;
			cy_as_ll_remove_request(dev_p, ctxt_p, req_p, cy_true);
			dev_p->storage_error = CY_AS_ERROR_TIMEOUT;
		}

		ret = dev_p->storage_error;

		if (ret != CY_AS_ERROR_SUCCESS)
			return ret;

		resp_type = cy_as_ll_request_response__get_code(
			dev_p->storage_rw_resp_p);
		if (resp_type == CY_RESP_SDIO_EXT) {
			resp_data = cy_as_ll_request_response__get_word
				(reply_p, 0)&0x00ff;
			if (resp_data)
				ret = CY_AS_ERROR_INVALID_REQUEST;

		} else {
			ret = CY_AS_ERROR_INVALID_RESPONSE;
		}
	}
	return ret;

}

static void
cy_as_sdio_async_reply_callback(
		cy_as_device	*dev_p,
		uint8_t				 context,
		cy_as_ll_request_response *rqt,
		cy_as_ll_request_response *resp,
		cy_as_return_status_t	  ret)
{
	cy_as_storage_callback cb_ms;
	uint8_t reqtype;
	uint32_t pendingblocks;
	(void)rqt;
	(void)context;

	pendingblocks = 0;
	reqtype = cy_as_ll_request_response__get_code(rqt);
	if (ret == CY_AS_ERROR_SUCCESS) {
		if ((cy_as_ll_request_response__get_code(resp) ==
		CY_RESP_SUCCESS_FAILURE) ||
		(cy_as_ll_request_response__get_code(resp) ==
		CY_RESP_SDIO_EXT)) {
			ret = cy_as_ll_request_response__get_word(resp, 0);
			ret &= 0x00FF;
		} else {
			ret = CY_AS_ERROR_INVALID_RESPONSE;
		}
	}

	if (ret != CY_AS_ERROR_SUCCESS) {
		if (reqtype == CY_RQT_SDIO_READ_EXTENDED)
			cy_as_dma_cancel(dev_p,
				dev_p->storage_read_endpoint, ret);
		else
			cy_as_dma_cancel(dev_p,
				dev_p->storage_write_endpoint, ret);

		dev_p->storage_error = ret;
	}

	dev_p->storage_wait = cy_false;

	/*
	 * if the DMA callback has already been called,
	 * the user callback has to be called from here.
	 */
	if (!cy_as_device_is_storage_async_pending(dev_p)) {
		cy_as_hal_assert(dev_p->storage_cb_ms != NULL);
		cb_ms = dev_p->storage_cb_ms;

		dev_p->storage_cb = 0;
		dev_p->storage_cb_ms = 0;

		if ((ret == CY_AS_ERROR_SUCCESS) ||
		(ret == CY_AS_ERROR_IO_ABORTED) ||
		(ret == CY_AS_ERROR_IO_SUSPENDED)) {
			ret = dev_p->storage_error;
			pendingblocks = ((uint32_t)
				cy_as_ll_request_response__get_word
				(resp, 1)) << 16;
		} else
			ret = CY_AS_ERROR_INVALID_RESPONSE;

		cb_ms((cy_as_device_handle)dev_p, dev_p->storage_bus_index,
			dev_p->storage_device_index,
			(dev_p->storage_unit | pendingblocks),
			dev_p->storage_block_addr, dev_p->storage_oper, ret);
	} else
		dev_p->storage_error = ret;
}


cy_as_return_status_t
cy_as_sdio_extended_i_o_async(
		cy_as_device_handle			handle,
		cy_as_bus_number_t			 bus,
		uint32_t					device,
		uint8_t					 n_function_no,
		uint32_t					address,
		uint8_t					 misc_buf,
		uint16_t					argument,
		uint8_t					 is_write,
		uint8_t					*data_p,
		cy_as_storage_callback	   callback)
{

	uint32_t mask;
	uint32_t dmasize;
	cy_as_ll_request_response *req_p , *reply_p;
	uint8_t reqtype;
	cy_as_end_point_number_t ep;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	cy_as_device *dev_p = (cy_as_device *)handle;

	ret = cy_as_sdio_device_check(dev_p, bus, device);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (!(cy_as_sdio_check_function_initialized(handle,
		bus, n_function_no)))
		return CY_AS_ERROR_INVALID_FUNCTION;
	if (cy_as_sdio_check_function_suspended(handle, bus, n_function_no))
		return CY_AS_ERROR_FUNCTION_SUSPENDED;

	if (callback == 0)
		return CY_AS_ERROR_NULL_CALLBACK;

	/* We are supposed to return success if the number of
	 * blocks is zero
	 */
	if (((misc_buf&CY_SDIO_BLOCKMODE) != 0) && (argument == 0)) {
		callback(handle, bus, device, n_function_no, address,
			((is_write) ? cy_as_op_write : cy_as_op_read),
			CY_AS_ERROR_SUCCESS);
		return CY_AS_ERROR_SUCCESS;
	}


	/*
	 * since async operations can be triggered by interrupt
	 * code, we must insure that we do not get multiple async
	 * operations going at one time and protect this test and
	 * set operation from interrupts.
	 */
	mask = cy_as_hal_disable_interrupts();
	if ((cy_as_device_is_storage_async_pending(dev_p)) ||
	(dev_p->storage_wait)) {
		cy_as_hal_enable_interrupts(mask);
		return CY_AS_ERROR_ASYNC_PENDING;
	}
	cy_as_device_set_storage_async_pending(dev_p);
	cy_as_hal_enable_interrupts(mask);


	/*
	 * storage information about the currently
	 * outstanding request
	 */
	dev_p->storage_cb_ms = callback;
	dev_p->storage_bus_index = bus;
	dev_p->storage_device_index = device;
	dev_p->storage_unit = n_function_no;
	dev_p->storage_block_addr = address;

	if (is_write == cy_true) {
		reqtype = CY_RQT_SDIO_WRITE_EXTENDED;
		ep = dev_p->storage_write_endpoint;
	} else {
		reqtype = CY_RQT_SDIO_READ_EXTENDED;
		ep = dev_p->storage_read_endpoint;
	}

	/* Initialise the request to send to the West Bridge. */
	req_p = dev_p->storage_rw_req_p;
	cy_as_ll_init_request(req_p, reqtype,
		CY_RQT_STORAGE_RQT_CONTEXT, 3);

	/* Initialise the space for reply from the West Bridge. */
	reply_p = dev_p->storage_rw_resp_p;
	cy_as_ll_init_response(reply_p, 2);

	if (!(misc_buf&CY_SDIO_BLOCKMODE)) {
		if (argument >
		dev_p->sdiocard[bus].function[n_function_no-1].blocksize)
			return CY_AS_ERROR_INVALID_BLOCKSIZE;

	} else {
		if (argument > 511)
			return CY_AS_ERROR_INVALID_BLOCKSIZE;
	}

	if (argument == 512)
		argument = 0;
	dmasize = ((misc_buf&CY_SDIO_BLOCKMODE) != 0) ?
		dev_p->sdiocard[bus].function[n_function_no-1].blocksize *
		argument : argument;

	/* Setup the DMA request and adjust the storage
	 * operation if we are reading */
	if (reqtype == CY_RQT_SDIO_READ_EXTENDED) {
		ret = cy_as_dma_queue_request(dev_p, ep,
			(void *)data_p, dmasize , cy_false, cy_true,
			cy_as_async_storage_callback);
		dev_p->storage_oper = cy_as_op_read;
	} else if (reqtype == CY_RQT_SDIO_WRITE_EXTENDED) {
		ret = cy_as_dma_queue_request(dev_p, ep, (void *)data_p,
		dmasize, cy_false, cy_false, cy_as_async_storage_callback);
		dev_p->storage_oper = cy_as_op_write;
	}

	if (ret != CY_AS_ERROR_SUCCESS) {
		cy_as_device_clear_storage_async_pending(dev_p);
		return ret;
	}

	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, (uint8_t)device, n_function_no));
	cy_as_ll_request_response__set_word(req_p, 1,
		((uint16_t)n_function_no) << 12 |
		((uint16_t)(misc_buf & (CY_SDIO_BLOCKMODE | CY_SDIO_OP_INCR)))
		<< 9 | (uint16_t)(address>>7) |
		((is_write == cy_true) ? 0x8000 : 0x0000));
	cy_as_ll_request_response__set_word(req_p, 2,
		((uint16_t)(address&0x0000ffff) << 9) |  argument);


	/* Send the request and wait for completion of storage request */
	dev_p->storage_wait = cy_true;
	ret = cy_as_ll_send_request(dev_p, req_p, reply_p, cy_true,
		cy_as_sdio_async_reply_callback);
	if (ret != CY_AS_ERROR_SUCCESS) {
		cy_as_dma_cancel(dev_p, ep, CY_AS_ERROR_CANCELED);
		cy_as_device_clear_storage_async_pending(dev_p);
	} else {
		cy_as_dma_kick_start(dev_p, ep);
	}

	return ret;
}

/* CMD53 Extended Read*/
cy_as_return_status_t
cy_as_sdio_extended_read(
		cy_as_device_handle			handle,
		cy_as_bus_number_t			 bus,
		uint32_t					device,
		uint8_t					 n_function_no,
		uint32_t					address,
		uint8_t					 misc_buf,
		uint16_t					argument,
		uint8_t					*data_p,
		cy_as_sdio_callback			callback)
{
	if (callback == 0)
		return cy_as_sdio_extended_i_o(handle, bus, device,
			n_function_no, address, misc_buf, argument,
			cy_false, data_p, 0);

	return cy_as_sdio_extended_i_o_async(handle, bus, device,
		n_function_no, address, misc_buf, argument, cy_false,
		data_p, callback);
}
EXPORT_SYMBOL(cy_as_sdio_extended_read);

/* CMD53 Extended Write*/
cy_as_return_status_t
cy_as_sdio_extended_write(
		cy_as_device_handle			handle,
		cy_as_bus_number_t			 bus,
		uint32_t					device,
		uint8_t					 n_function_no,
		uint32_t					address,
		uint8_t					 misc_buf,
		uint16_t					argument,
		uint8_t					*data_p,
		cy_as_sdio_callback			callback)
{
	if (callback == 0)
		return cy_as_sdio_extended_i_o(handle, bus, device,
			n_function_no, address, misc_buf, argument, cy_true,
			data_p, 0);

	return cy_as_sdio_extended_i_o_async(handle, bus, device,
		n_function_no, address, misc_buf, argument, cy_true,
		data_p, callback);
}
EXPORT_SYMBOL(cy_as_sdio_extended_write);

/* Read the CIS info tuples for the given function and Tuple ID*/
cy_as_return_status_t
cy_as_sdio_get_c_i_s_info(
		cy_as_device_handle			handle,
		cy_as_bus_number_t			 bus,
		uint32_t					device,
		uint8_t					 n_function_no,
		uint16_t					tuple_id,
		uint8_t					*data_p)
{

	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint16_t resp_data;
	cy_as_context *ctxt_p;
	uint32_t loopcount = 200;

	cy_as_device *dev_p = (cy_as_device *)handle;

	ret = cy_as_sdio_device_check(dev_p, bus, device);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (!(cy_as_sdio_check_function_initialized(handle, bus, 0)))
		return CY_AS_ERROR_INVALID_FUNCTION;

	if ((cy_as_device_is_storage_async_pending(dev_p)) ||
	(dev_p->storage_wait))
		return CY_AS_ERROR_ASYNC_PENDING;


	/* Initialise the request to send to the Antioch. */
	req_p = dev_p->storage_rw_req_p;
	cy_as_ll_init_request(req_p, CY_RQT_SDIO_GET_TUPLE,
		CY_RQT_STORAGE_RQT_CONTEXT, 2);

	/* Initialise the space for reply from the Antioch. */
	reply_p = dev_p->storage_rw_resp_p;
	cy_as_ll_init_response(reply_p, 3);

	/* Setup the DMA request */
	ret = cy_as_dma_queue_request(dev_p, dev_p->storage_read_endpoint,
		data_p+1, 255, cy_false, cy_true, cy_as_sync_storage_callback);

	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, (uint8_t)device, n_function_no));

	/* Set tuple id to fetch. */
	cy_as_ll_request_response__set_word(req_p, 1, tuple_id<<8);

	/* Send the request and wait for completion of storage request */
	dev_p->storage_wait = cy_true;
	ret = cy_as_ll_send_request(dev_p, req_p, reply_p, cy_true,
		cy_as_sdio_sync_reply_callback);

	if (ret != CY_AS_ERROR_SUCCESS) {
		cy_as_dma_cancel(dev_p,
			dev_p->storage_read_endpoint, CY_AS_ERROR_CANCELED);
	} else {
		/* Setup the DMA request */
		ctxt_p = dev_p->context[CY_RQT_STORAGE_RQT_CONTEXT];
		ret = cy_as_dma_drain_queue(dev_p,
			dev_p->storage_read_endpoint, cy_true);

		while (loopcount-- > 0) {
			if (dev_p->storage_wait == cy_false)
				break;
			cy_as_hal_sleep_on(&ctxt_p->channel, 10);
		}

		if (dev_p->storage_wait == cy_true) {
			dev_p->storage_wait = cy_false;
			cy_as_ll_remove_request(dev_p, ctxt_p, req_p, cy_true);
			return CY_AS_ERROR_TIMEOUT;
		}
		ret = dev_p->storage_error;

		if (ret != CY_AS_ERROR_SUCCESS)
			return ret;

		if (cy_as_ll_request_response__get_code
		(dev_p->storage_rw_resp_p) == CY_RESP_SDIO_GET_TUPLE) {
			resp_data = cy_as_ll_request_response__get_word
				(reply_p, 0);
			if (resp_data) {
				ret = CY_AS_ERROR_INVALID_REQUEST;
			} else if (data_p != 0)
				*(uint8_t *)data_p = (uint8_t)
					(cy_as_ll_request_response__get_word
						(reply_p, 0)&0x00ff);
		} else {
			ret = CY_AS_ERROR_INVALID_RESPONSE;
		}
	}
	return ret;
}

/*Query Device*/
cy_as_return_status_t
cy_as_sdio_query_card(
		cy_as_device_handle handle,
		cy_as_bus_number_t bus,
		uint32_t device,
		cy_as_sdio_card *data_p)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;

	uint8_t resp_type;
	cy_as_device *dev_p = (cy_as_device *)handle;

	ret = cy_as_sdio_device_check(dev_p, bus, device);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	/* Allocating memory to the SDIO device structure in dev_p */

	cy_as_hal_mem_set(&dev_p->sdiocard[bus], 0, sizeof(cy_as_sdio_device));

	req_p = cy_as_ll_create_request(dev_p, CY_RQT_SDIO_QUERY_CARD,
		CY_RQT_STORAGE_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, (uint8_t)device, 0));

	reply_p = cy_as_ll_create_response(dev_p, 5);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	ret = cy_as_ll_send_request_wait_reply(dev_p,
		req_p, reply_p);

	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	resp_type = cy_as_ll_request_response__get_code(reply_p);
	if (resp_type == CY_RESP_SDIO_QUERY_CARD) {
		dev_p->sdiocard[bus].card.num_functions	=
			(uint8_t)((reply_p->data[0]&0xff00)>>8);
		dev_p->sdiocard[bus].card.memory_present =
			(uint8_t)reply_p->data[0]&0x0001;
		dev_p->sdiocard[bus].card.manufacturer__id =
			reply_p->data[1];
		dev_p->sdiocard[bus].card.manufacturer_info =
			reply_p->data[2];
		dev_p->sdiocard[bus].card.blocksize =
			reply_p->data[3];
		dev_p->sdiocard[bus].card.maxblocksize =
			reply_p->data[3];
		dev_p->sdiocard[bus].card.card_capability =
			(uint8_t)((reply_p->data[4]&0xff00)>>8);
		dev_p->sdiocard[bus].card.sdio_version =
			(uint8_t)(reply_p->data[4]&0x00ff);
		dev_p->sdiocard[bus].function_init_map = 0x01;
		data_p->num_functions =
			dev_p->sdiocard[bus].card.num_functions;
		data_p->memory_present =
			dev_p->sdiocard[bus].card.memory_present;
		data_p->manufacturer__id =
			dev_p->sdiocard[bus].card.manufacturer__id;
		data_p->manufacturer_info =
			dev_p->sdiocard[bus].card.manufacturer_info;
		data_p->blocksize = dev_p->sdiocard[bus].card.blocksize;
		data_p->maxblocksize =
			dev_p->sdiocard[bus].card.maxblocksize;
		data_p->card_capability	=
			dev_p->sdiocard[bus].card.card_capability;
		data_p->sdio_version =
			dev_p->sdiocard[bus].card.sdio_version;
	} else {
		if (resp_type == CY_RESP_SUCCESS_FAILURE)
			ret = cy_as_ll_request_response__get_word(reply_p, 0);
		else
			ret = CY_AS_ERROR_INVALID_RESPONSE;
	}
destroy:
	if (req_p != 0)
		cy_as_ll_destroy_request(dev_p, req_p);
	if (reply_p != 0)
		cy_as_ll_destroy_response(dev_p, reply_p);
	return ret;
}
EXPORT_SYMBOL(cy_as_sdio_query_card);

/*Reset SDIO card. */
cy_as_return_status_t
cy_as_sdio_reset_card(
		cy_as_device_handle			handle,
		cy_as_bus_number_t		 bus,
		uint32_t				device)
{

	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint8_t resp_type;
	cy_as_device *dev_p = (cy_as_device *)handle;

	ret = cy_as_sdio_device_check(dev_p, bus, device);

	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (dev_p->sdiocard != 0) {
		dev_p->sdiocard[bus].function_init_map = 0;
		dev_p->sdiocard[bus].function_suspended_map = 0;
	}

	req_p = cy_as_ll_create_request(dev_p, CY_RQT_SDIO_RESET_DEV,
		CY_RQT_STORAGE_RQT_CONTEXT, 1);

	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/*Setup mailbox */
	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, (uint8_t)device, 0));

	reply_p = cy_as_ll_create_response(dev_p, 2);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	ret = cy_as_ll_send_request_wait_reply(dev_p,
		req_p, reply_p);

	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	resp_type = cy_as_ll_request_response__get_code(reply_p);

	if (resp_type == CY_RESP_SUCCESS_FAILURE) {
		ret = cy_as_ll_request_response__get_word(reply_p, 0);
		if (ret == CY_AS_ERROR_SUCCESS)
			ret = cy_as_sdio_query_card(handle, bus, device, 0);
	} else
		ret = CY_AS_ERROR_INVALID_RESPONSE;

destroy:
	if (req_p != 0)
		cy_as_ll_destroy_request(dev_p, req_p);
	if (reply_p != 0)
		cy_as_ll_destroy_response(dev_p, reply_p);
	return ret;
}

/* Initialise an IO function*/
cy_as_return_status_t
cy_as_sdio_init_function(
		cy_as_device_handle			handle,
		cy_as_bus_number_t			 bus,
		uint32_t					device,
		uint8_t					 n_function_no,
		uint8_t					 misc_buf)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint8_t resp_type;
	cy_as_device *dev_p = (cy_as_device *)handle;

	ret = cy_as_sdio_device_check(dev_p, bus, device);

	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (!(cy_as_sdio_check_function_initialized
		(handle, bus, 0)))
		return CY_AS_ERROR_NOT_RUNNING;

	if ((cy_as_sdio_check_function_initialized
	(handle, bus, n_function_no))) {
		if (misc_buf&CY_SDIO_FORCE_INIT)
			dev_p->sdiocard[bus].function_init_map &=
				(~(1 << n_function_no));
		else
			return CY_AS_ERROR_ALREADY_RUNNING;
	}

	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_SDIO_INIT_FUNCTION, CY_RQT_STORAGE_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, (uint8_t)device, n_function_no));

	reply_p = cy_as_ll_create_response(dev_p, 5);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);

	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	resp_type = cy_as_ll_request_response__get_code(reply_p);

	if (resp_type == CY_RESP_SDIO_INIT_FUNCTION) {
		dev_p->sdiocard[bus].function[n_function_no-1].function_code =
			(uint8_t)((reply_p->data[0]&0xff00)>>8);
		dev_p->sdiocard[bus].function[n_function_no-1].
			extended_func_code = (uint8_t)reply_p->data[0]&0x00ff;
		dev_p->sdiocard[bus].function[n_function_no-1].blocksize =
			reply_p->data[1];
		dev_p->sdiocard[bus].function[n_function_no-1].
			maxblocksize = reply_p->data[1];
		dev_p->sdiocard[bus].function[n_function_no-1].card_psn	=
			(uint32_t)(reply_p->data[2])<<16;
		dev_p->sdiocard[bus].function[n_function_no-1].card_psn |=
			(uint32_t)(reply_p->data[3]);
		dev_p->sdiocard[bus].function[n_function_no-1].csa_bits =
			(uint8_t)((reply_p->data[4]&0xff00)>>8);
		dev_p->sdiocard[bus].function[n_function_no-1].wakeup_support =
			(uint8_t)(reply_p->data[4]&0x0001);
		dev_p->sdiocard[bus].function_init_map |= (1 << n_function_no);
		cy_as_sdio_clear_function_suspended(handle, bus, n_function_no);

	} else {
		if (resp_type == CY_RESP_SUCCESS_FAILURE)
			ret = cy_as_ll_request_response__get_word(reply_p, 0);
		else
			ret = CY_AS_ERROR_INVALID_FUNCTION;
	}

destroy:
	if (req_p != 0)
		cy_as_ll_destroy_request(dev_p, req_p);
	if (reply_p != 0)
		cy_as_ll_destroy_response(dev_p, reply_p);
	return ret;
}
EXPORT_SYMBOL(cy_as_sdio_init_function);

/*Query individual functions. */
cy_as_return_status_t
cy_as_sdio_query_function(
		cy_as_device_handle			handle,
		cy_as_bus_number_t			 bus,
		uint32_t					device,
		uint8_t					 n_function_no,
		cy_as_sdio_func			*data_p)
{
	cy_as_device *dev_p = (cy_as_device *)handle;
	cy_as_return_status_t ret;

	ret = cy_as_sdio_device_check(dev_p, bus, device);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (!(cy_as_sdio_check_function_initialized(handle,
		bus, n_function_no)))
		return CY_AS_ERROR_INVALID_FUNCTION;

	data_p->blocksize =
		dev_p->sdiocard[bus].function[n_function_no-1].blocksize;
	data_p->card_psn =
		dev_p->sdiocard[bus].function[n_function_no-1].card_psn;
	data_p->csa_bits =
		dev_p->sdiocard[bus].function[n_function_no-1].csa_bits;
	data_p->extended_func_code =
		dev_p->sdiocard[bus].function[n_function_no-1].
		extended_func_code;
	data_p->function_code =
		dev_p->sdiocard[bus].function[n_function_no-1].function_code;
	data_p->maxblocksize =
		dev_p->sdiocard[bus].function[n_function_no-1].maxblocksize;
	data_p->wakeup_support =
		dev_p->sdiocard[bus].function[n_function_no-1].wakeup_support;

	return CY_AS_ERROR_SUCCESS;
}

/* Abort the Current Extended IO Operation*/
cy_as_return_status_t
cy_as_sdio_abort_function(
		cy_as_device_handle			handle,
		cy_as_bus_number_t			 bus,
		uint32_t					device,
		uint8_t					 n_function_no)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	uint8_t resp_type;
	cy_as_device *dev_p = (cy_as_device *)handle;

	ret = cy_as_sdio_device_check(dev_p, bus, device);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (!(cy_as_sdio_check_function_initialized(handle,
		bus, n_function_no)))
		return CY_AS_ERROR_INVALID_FUNCTION;

	if ((cy_as_device_is_storage_async_pending(dev_p)) ||
	(dev_p->storage_wait)) {
		if (!(cy_as_sdio_get_card_capability(handle, bus) &
		CY_SDIO_SDC))
			return CY_AS_ERROR_INVALID_COMMAND;
	}

	req_p = cy_as_ll_create_request(dev_p, CY_RQT_SDIO_ABORT_IO,
		CY_RQT_GENERAL_RQT_CONTEXT, 1);

	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/*Setup mailbox */
	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, (uint8_t)device, n_function_no));

	reply_p = cy_as_ll_create_response(dev_p, 2);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}

	ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);
	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	resp_type = cy_as_ll_request_response__get_code(reply_p);

	if (resp_type == CY_RESP_SUCCESS_FAILURE)
		ret = cy_as_ll_request_response__get_word(reply_p, 0);
	else
		ret = CY_AS_ERROR_INVALID_RESPONSE;


destroy:
	if (req_p != 0)
		cy_as_ll_destroy_request(dev_p, req_p);
	if (reply_p != 0)
		cy_as_ll_destroy_response(dev_p, reply_p);
	return ret;
}

/* Suspend IO to current function*/
cy_as_return_status_t
cy_as_sdio_suspend(
		cy_as_device_handle		handle,
		cy_as_bus_number_t		 bus,
		uint32_t				device,
		uint8_t				 n_function_no)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t ret = CY_AS_ERROR_SUCCESS;
	cy_as_device *dev_p = (cy_as_device *)handle;

	ret = cy_as_sdio_device_check(dev_p, bus, device);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (!(cy_as_sdio_check_function_initialized(handle, bus,
		n_function_no)))
		return CY_AS_ERROR_INVALID_FUNCTION;
	if (!(cy_as_sdio_check_support_bus_suspend(handle, bus)))
		return CY_AS_ERROR_INVALID_FUNCTION;
	if (!(cy_as_sdio_get_card_capability(handle, bus) & CY_SDIO_SDC))
		return CY_AS_ERROR_INVALID_FUNCTION;
	if (cy_as_sdio_check_function_suspended(handle, bus, n_function_no))
		return CY_AS_ERROR_FUNCTION_SUSPENDED;

	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_SDIO_SUSPEND, CY_RQT_GENERAL_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/*Setup mailbox */
	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, (uint8_t)device, n_function_no));

	reply_p = cy_as_ll_create_response(dev_p, 2);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}
	ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);

	if (ret == CY_AS_ERROR_SUCCESS) {
		ret = cy_as_ll_request_response__get_code(reply_p);
		cy_as_sdio_set_function_suspended(handle, bus, n_function_no);
	}

	if (req_p != 0)
		cy_as_ll_destroy_request(dev_p, req_p);
	if (reply_p != 0)
		cy_as_ll_destroy_response(dev_p, reply_p);

	return ret;
}

/*Resume suspended function*/
cy_as_return_status_t
cy_as_sdio_resume(
		cy_as_device_handle		handle,
		cy_as_bus_number_t		 bus,
		uint32_t				device,
		uint8_t				 n_function_no,
		cy_as_oper_type			op,
		uint8_t				 misc_buf,
		uint16_t				pendingblockcount,
		uint8_t				 *data_p
		)
{
	cy_as_ll_request_response *req_p , *reply_p;
	cy_as_return_status_t resp_data, ret = CY_AS_ERROR_SUCCESS;
	cy_as_device *dev_p = (cy_as_device *)handle;

	ret = cy_as_sdio_device_check(dev_p, bus, device);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (!(cy_as_sdio_check_function_initialized
	(handle, bus, n_function_no)))
		return CY_AS_ERROR_INVALID_FUNCTION;

	/* If suspend resume is not supported return */
	if (!(cy_as_sdio_check_support_bus_suspend(handle, bus)))
		return CY_AS_ERROR_INVALID_FUNCTION;

	/* if the function is not suspended return. */
	if (!(cy_as_sdio_check_function_suspended
	(handle, bus, n_function_no)))
		return CY_AS_ERROR_INVALID_FUNCTION;

	req_p = cy_as_ll_create_request(dev_p,
		CY_RQT_SDIO_RESUME, CY_RQT_STORAGE_RQT_CONTEXT, 1);
	if (req_p == 0)
		return CY_AS_ERROR_OUT_OF_MEMORY;

	/*Setup mailbox */
	cy_as_ll_request_response__set_word(req_p, 0,
		create_address(bus, (uint8_t)device, n_function_no));

	reply_p = cy_as_ll_create_response(dev_p, 2);
	if (reply_p == 0) {
		cy_as_ll_destroy_request(dev_p, req_p);
		return CY_AS_ERROR_OUT_OF_MEMORY;
	}
	ret = cy_as_ll_send_request_wait_reply(dev_p, req_p, reply_p);

	if (ret != CY_AS_ERROR_SUCCESS)
		goto destroy;

	if (cy_as_ll_request_response__get_code(reply_p) ==
	CY_RESP_SDIO_RESUME) {
		resp_data = cy_as_ll_request_response__get_word(reply_p, 0);
		if (resp_data & 0x00ff) {
			/* Send extended read request to resume the read. */
			if (op == cy_as_op_read) {
				ret = cy_as_sdio_extended_i_o(handle, bus,
					device, n_function_no, 0, misc_buf,
					pendingblockcount, cy_false, data_p, 1);
			} else {
				ret = cy_as_sdio_extended_i_o(handle, bus,
					device, n_function_no, 0, misc_buf,
					pendingblockcount, cy_true, data_p, 1);
			}
		} else {
			ret = CY_AS_ERROR_SUCCESS;
		}
	} else {
		ret = CY_AS_ERROR_INVALID_RESPONSE;
	}

destroy:
	cy_as_sdio_clear_function_suspended(handle, bus, n_function_no);
	if (req_p != 0)
		cy_as_ll_destroy_request(dev_p, req_p);
	if (reply_p != 0)
		cy_as_ll_destroy_response(dev_p, reply_p);
	return ret;

}

/*Set function blocksize. Size cannot exceed max
 * block size for the function*/
cy_as_return_status_t
cy_as_sdio_set_blocksize(
		cy_as_device_handle			handle,
		cy_as_bus_number_t			 bus,
		uint32_t					device,
		uint8_t					 n_function_no,
		uint16_t					blocksize)
{
	cy_as_return_status_t ret;
	cy_as_device *dev_p = (cy_as_device *)handle;
	ret = cy_as_sdio_device_check(dev_p, bus, device);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (!(cy_as_sdio_check_function_initialized
	(handle, bus, n_function_no)))
		return CY_AS_ERROR_INVALID_FUNCTION;
	if (n_function_no == 0) {
		if (blocksize > cy_as_sdio_get_card_max_blocksize(handle, bus))
			return CY_AS_ERROR_INVALID_BLOCKSIZE;
		else if (blocksize == cy_as_sdio_get_card_blocksize
			(handle, bus))
			return CY_AS_ERROR_SUCCESS;
	} else {
		if (blocksize >
			cy_as_sdio_get_function_max_blocksize(handle,
				bus, n_function_no))
			return CY_AS_ERROR_INVALID_BLOCKSIZE;
		else if (blocksize ==
			cy_as_sdio_get_function_blocksize(handle,
				bus, n_function_no))
			return CY_AS_ERROR_SUCCESS;
	}

	ret = cy_as_sdio_direct_write(handle, bus, device, 0,
		(uint16_t)(n_function_no << 8) |
		0x10, 0, blocksize & 0x00ff, 0);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	ret = cy_as_sdio_direct_write(handle, bus, device, 0,
		(uint16_t)(n_function_no << 8) |
		0x11, 0, (blocksize & 0xff00) >> 8, 0);

	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (n_function_no == 0)
		cy_as_sdio_set_card_block_size(handle, bus, blocksize);
	else
		cy_as_sdio_set_function_block_size(handle,
			bus, n_function_no, blocksize);
	return ret;
}
EXPORT_SYMBOL(cy_as_sdio_set_blocksize);

/* Deinitialize an SDIO function*/
cy_as_return_status_t
cy_as_sdio_de_init_function(
		cy_as_device_handle			handle,
		cy_as_bus_number_t			 bus,
		uint32_t					device,
		uint8_t					 n_function_no)
{
	cy_as_return_status_t ret;
	uint8_t temp;

	if (n_function_no == 0)
		return CY_AS_ERROR_INVALID_FUNCTION;

	ret = cy_as_sdio_device_check((cy_as_device *)handle, bus, device);
	if (ret != CY_AS_ERROR_SUCCESS)
		return ret;

	if (!(cy_as_sdio_check_function_initialized
		(handle, bus, n_function_no)))
		return CY_AS_ERROR_SUCCESS;

	temp = (uint8_t)(((cy_as_device *)handle)->sdiocard[bus].
		function_init_map & (~(1 << n_function_no)));

	cy_as_sdio_direct_write(handle, bus, device, 0, 0x02, 0, temp, 0);

	((cy_as_device *)handle)->sdiocard[bus].function_init_map &=
		(~(1 << n_function_no));

	return CY_AS_ERROR_SUCCESS;
}


/*[]*/
