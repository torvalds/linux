/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __EVENT_FIFO_PRIVATE_H
#define __EVENT_FIFO_PRIVATE_H

#include "event_fifo_public.h"

#include "device_access.h"

#include "assert_support.h"

#include <hrt/bits.h>			/* _hrt_get_bits() */

STORAGE_CLASS_EVENT_C void event_wait_for(const event_ID_t ID)
{
	assert(ID < N_EVENT_ID);
	assert(event_source_addr[ID] != ((hrt_address)-1));
	(void)ia_css_device_load_uint32(event_source_addr[ID]);
	return;
}

STORAGE_CLASS_EVENT_C void cnd_event_wait_for(const event_ID_t ID,
						const bool cnd)
{
	if (cnd) {
		event_wait_for(ID);
	}
}

STORAGE_CLASS_EVENT_C hrt_data event_receive_token(const event_ID_t ID)
{
	assert(ID < N_EVENT_ID);
	assert(event_source_addr[ID] != ((hrt_address)-1));
	return ia_css_device_load_uint32(event_source_addr[ID]);
}

STORAGE_CLASS_EVENT_C void event_send_token(const event_ID_t ID,
					    const hrt_data token)
{
	assert(ID < N_EVENT_ID);
	assert(event_sink_addr[ID] != ((hrt_address)-1));
	ia_css_device_store_uint32(event_sink_addr[ID], token);
}

STORAGE_CLASS_EVENT_C bool is_event_pending(const event_ID_t ID)
{
	hrt_data	value;
	assert(ID < N_EVENT_ID);
	assert(event_source_query_addr[ID] != ((hrt_address)-1));
	value = ia_css_device_load_uint32(event_source_query_addr[ID]);
	return !_hrt_get_bit(value, EVENT_QUERY_BIT);
}

STORAGE_CLASS_EVENT_C bool can_event_send_token(const event_ID_t ID)
{
	hrt_data	value;
	assert(ID < N_EVENT_ID);
	assert(event_sink_query_addr[ID] != ((hrt_address)-1));
	value = ia_css_device_load_uint32(event_sink_query_addr[ID]);
	return !_hrt_get_bit(value, EVENT_QUERY_BIT);
}

#endif /* __EVENT_FIFO_PRIVATE_H */
