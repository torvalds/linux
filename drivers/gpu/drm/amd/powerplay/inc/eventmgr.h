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

#ifndef _EVENTMGR_H_
#define _EVENTMGR_H_

#include <linux/mutex.h>
#include "pp_instance.h"
#include "hardwaremanager.h"
#include "eventmanager.h"
#include "pp_feature.h"
#include "pp_power_source.h"
#include "power_state.h"

typedef int (*pem_event_action)(struct pp_eventmgr *eventmgr,
				struct pem_event_data *event_data);

struct action_chain {
	const char *description;  /* action chain description for debugging purpose */
	const pem_event_action * const *action_chain; /* pointer to chain of event actions */
};

struct pem_power_source_ui_state_info {
	enum PP_StateUILabel current_ui_label;
	enum PP_StateUILabel default_ui_lable;
	unsigned long    configurable_ui_mapping;
};

struct pp_clock_range {
	uint32_t min_sclk_khz;
	uint32_t max_sclk_khz;

	uint32_t min_mclk_khz;
	uint32_t max_mclk_khz;

	uint32_t min_vclk_khz;
	uint32_t max_vclk_khz;

	uint32_t min_dclk_khz;
	uint32_t max_dclk_khz;

	uint32_t min_aclk_khz;
	uint32_t max_aclk_khz;

	uint32_t min_eclk_khz;
	uint32_t max_eclk_khz;
};

enum pp_state {
	UNINITIALIZED,
	INACTIVE,
	ACTIVE
};

enum pp_ring_index {
	PP_RING_TYPE_GFX_INDEX = 0,
	PP_RING_TYPE_DMA_INDEX,
	PP_RING_TYPE_DMA1_INDEX,
	PP_RING_TYPE_UVD_INDEX,
	PP_RING_TYPE_VCE0_INDEX,
	PP_RING_TYPE_VCE1_INDEX,
	PP_RING_TYPE_CP1_INDEX,
	PP_RING_TYPE_CP2_INDEX,
	PP_NUM_RINGS,
};

struct pp_request {
	uint32_t flags;
	uint32_t sclk;
	uint32_t sclk_throttle;
	uint32_t mclk;
	uint32_t vclk;
	uint32_t dclk;
	uint32_t eclk;
	uint32_t aclk;
	uint32_t iclk;
	uint32_t vp8clk;
	uint32_t rsv[32];
};

struct pp_eventmgr {
	struct pp_hwmgr	*hwmgr;
	struct pp_smumgr *smumgr;

	struct pp_feature_info features[PP_Feature_Max];
	const struct action_chain *event_chain[AMD_PP_EVENT_MAX];
	struct phm_platform_descriptor   *platform_descriptor;
	struct pp_clock_range clock_range;
	enum pp_power_source  current_power_source;
	struct pem_power_source_ui_state_info  ui_state_info[PP_PowerSource_Max];
	enum pp_state states[PP_NUM_RINGS];
	struct pp_request hi_req;
	struct list_head context_list;
	struct mutex lock;
	bool  block_adjust_power_state;
	bool enable_cg;
	bool enable_gfx_cgpg;
	int (*pp_eventmgr_init)(struct pp_eventmgr *eventmgr);
	void (*pp_eventmgr_fini)(struct pp_eventmgr *eventmgr);
};

int eventmgr_init(struct pp_instance *handle);
int eventmgr_fini(struct pp_eventmgr *eventmgr);

#endif /* _EVENTMGR_H_ */
