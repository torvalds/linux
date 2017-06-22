#ifndef ISP2401
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
#else
/**
Support for Intel Camera Imaging ISP subsystem.
Copyright (c) 2010 - 2015, Intel Corporation.

This program is free software; you can redistribute it and/or modify it
under the terms and conditions of the GNU General Public License,
version 2, as published by the Free Software Foundation.

This program is distributed in the hope it will be useful, but WITHOUT
ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
more details.
*/
#endif

#include "sh_css_sp.h"

#include "dma.h"	/* N_DMA_CHANNEL_ID */

#include <type_support.h>
#include "ia_css_binary.h"
#include "sh_css_hrt.h"
#include "sh_css_defs.h"
#include "sh_css_internal.h"
#include "ia_css_debug.h"
#include "ia_css_debug_internal.h"
#include "sh_css_legacy.h"

#include "gdc_device.h"				/* HRT_GDC_N */

/*#include "sp.h"*/	/* host2sp_enqueue_frame_data() */

#include "memory_access.h"

#include "assert_support.h"
#include "platform_support.h"	/* hrt_sleep() */

#include "ia_css_queue.h"	/* host_sp_enqueue_XXX */
#include "ia_css_event.h"	/* ia_css_event_encode */
/**
 * @brief Encode the information into the software-event.
 * Refer to "sw_event_public.h" for details.
 */
bool ia_css_event_encode(
	uint8_t	*in,
	uint8_t	nr,
	uint32_t	*out)
{
	bool ret;
	uint32_t nr_of_bits;
	uint32_t i;
	assert(in != NULL);
	assert(out != NULL);
	OP___assert(nr > 0 && nr <= MAX_NR_OF_PAYLOADS_PER_SW_EVENT);

	/* initialize the output */
	*out = 0;

	/* get the number of bits per information */
	nr_of_bits = sizeof(uint32_t) * 8 / nr;

	/* compress the all inputs into a signle output */
	for (i = 0; i < nr; i++) {
		*out <<= nr_of_bits;
		*out |= in[i];
	}

	/* get the return value */
	ret = (nr > 0 && nr <= MAX_NR_OF_PAYLOADS_PER_SW_EVENT);

	return ret;
}

void ia_css_event_decode(
	uint32_t event,
	uint8_t *payload)
{
	assert(payload[1] == 0);
	assert(payload[2] == 0);
	assert(payload[3] == 0);

	ia_css_debug_dtrace(IA_CSS_DEBUG_TRACE_PRIVATE, "ia_css_event_decode() enter:\n");

	/* First decode according to the common case
	 * In case of a PORT_EOF event we overwrite with
	 * the specific values
	 * This is somewhat ugly but probably somewhat efficient
	 * (and it avoids some code duplication)
	 */
	payload[0] = event & 0xff;  /*event_code */
	payload[1] = (event >> 8) & 0xff;
	payload[2] = (event >> 16) & 0xff;
	payload[3] = 0;

	switch (payload[0]) {
	case SH_CSS_SP_EVENT_PORT_EOF:
		payload[2] = 0;
		payload[3] = (event >> 24) & 0xff;
		break;

	case SH_CSS_SP_EVENT_ACC_STAGE_COMPLETE:
	case SH_CSS_SP_EVENT_TIMER:
	case SH_CSS_SP_EVENT_FRAME_TAGGED:
	case SH_CSS_SP_EVENT_FW_WARNING:
	case SH_CSS_SP_EVENT_FW_ASSERT:
		payload[3] = (event >> 24) & 0xff;
		break;
	default:
		break;
	}
}
