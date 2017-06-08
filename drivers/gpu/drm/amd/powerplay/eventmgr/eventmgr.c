/*
 * Copyright 2015 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 */
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/slab.h>
#include "eventmgr.h"
#include "hwmgr.h"
#include "eventinit.h"
#include "eventmanagement.h"

static int pem_init(struct pp_eventmgr *eventmgr)
{
	int result = 0;
	struct pem_event_data event_data = { {0} };

	/* Initialize PowerPlay feature info */
	pem_init_feature_info(eventmgr);

	/* Initialize event action chains */
	pem_init_event_action_chains(eventmgr);

	/* Call initialization event */
	result = pem_handle_event(eventmgr, AMD_PP_EVENT_INITIALIZE, &event_data);

	if (0 != result)
		return result;

	/* Register interrupt callback functions */
	result = pem_register_interrupts(eventmgr);
	return 0;
}

static void pem_fini(struct pp_eventmgr *eventmgr)
{
	struct pem_event_data event_data = { {0} };

	pem_uninit_featureInfo(eventmgr);
	pem_unregister_interrupts(eventmgr);

	pem_handle_event(eventmgr, AMD_PP_EVENT_UNINITIALIZE, &event_data);
}

int eventmgr_early_init(struct pp_instance *handle)
{
	struct pp_eventmgr *eventmgr;

	if (handle == NULL)
		return -EINVAL;

	eventmgr = kzalloc(sizeof(struct pp_eventmgr), GFP_KERNEL);
	if (eventmgr == NULL)
		return -ENOMEM;

	eventmgr->hwmgr = handle->hwmgr;
	handle->eventmgr = eventmgr;

	eventmgr->platform_descriptor = &(eventmgr->hwmgr->platform_descriptor);
	eventmgr->pp_eventmgr_init = pem_init;
	eventmgr->pp_eventmgr_fini = pem_fini;

	return 0;
}

static int pem_handle_event_unlocked(struct pp_eventmgr *eventmgr, enum amd_pp_event event, struct pem_event_data *data)
{
	if (eventmgr == NULL || event >= AMD_PP_EVENT_MAX || data == NULL)
		return -EINVAL;

	return pem_excute_event_chain(eventmgr, eventmgr->event_chain[event], data);
}

int pem_handle_event(struct pp_eventmgr *eventmgr, enum amd_pp_event event, struct pem_event_data *event_data)
{
	int r = 0;

	r = pem_handle_event_unlocked(eventmgr, event, event_data);

	return r;
}

bool pem_is_hw_access_blocked(struct pp_eventmgr *eventmgr)
{
	return (eventmgr->block_adjust_power_state || phm_is_hw_access_blocked(eventmgr->hwmgr));
}
