// SPDX-License-Identifier: MIT
/*
 * Copyright 2021 Advanced Micro Devices, Inc.
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
 * Authors: AMD
 *
 */

#include "dc.h"
#include "dc_link_dpia.h"
#include "inc/core_status.h"
#include "dc_link.h"
#include "dc_link_dp.h"

#define DC_LOGGER \
	link->ctx->logger

enum dc_status dpcd_get_tunneling_device_data(struct dc_link *link)
{
	/** @todo Read corresponding DPCD region and update link caps. */
	return DC_OK;
}

/* Configure link as prescribed in link_setting; set LTTPR mode; and
 * Initialize link training settings.
 * Abort link training if sink unplug detected.
 *
 * @param link DPIA link being trained.
 * @param[in] link_setting Lane count, link rate and downspread control.
 * @param[out] lt_settings Link settings and drive settings (voltage swing and pre-emphasis).
 */
static enum link_training_result dpia_configure_link(struct dc_link *link,
		const struct dc_link_settings *link_setting,
		struct link_training_settings *lt_settings)
{
	enum dc_status status;
	bool fec_enable;

	DC_LOG_HW_LINK_TRAINING("%s\n DPIA(%d) configuring\n - LTTPR mode(%d)\n",
				__func__,
				link->link_id.enum_id - ENUM_ID_1,
				link->lttpr_mode);

	dp_decide_training_settings(link,
		link_setting,
		lt_settings);

	status = dpcd_configure_channel_coding(link, lt_settings);
	if (status != DC_OK && !link->hpd_status)
		return LINK_TRAINING_ABORT;

	/* Configure lttpr mode */
	status = dpcd_configure_lttpr_mode(link, lt_settings);
	if (status != DC_OK && !link->hpd_status)
		return LINK_TRAINING_ABORT;

	/* Set link rate, lane count and spread. */
	status = dpcd_set_link_settings(link, lt_settings);
	if (status != DC_OK && !link->hpd_status)
		return LINK_TRAINING_ABORT;

	if (link->preferred_training_settings.fec_enable)
		fec_enable = *link->preferred_training_settings.fec_enable;
	else
		fec_enable = true;
	status = dp_set_fec_ready(link, fec_enable);
	if (status != DC_OK && !link->hpd_status)
		return LINK_TRAINING_ABORT;

	return LINK_TRAINING_SUCCESS;
}

/* Execute clock recovery phase of link training for specified hop in display
 * path.
 */
static enum link_training_result dpia_training_cr_phase(struct dc_link *link,
		struct link_training_settings *lt_settings,
		uint32_t hop)
{
	enum link_training_result result;

	/** @todo Fail until implemented. */
	result = LINK_TRAINING_ABORT;

	return result;
}

/* Execute equalization phase of link training for specified hop in display
 * path.
 */
static enum link_training_result dpia_training_eq_phase(struct dc_link *link,
		struct link_training_settings *lt_settings,
		uint32_t hop)
{
	enum link_training_result result;

	/** @todo Fail until implemented. */
	result = LINK_TRAINING_ABORT;

	return result;
}

/* End training of specified hop in display path. */
static enum link_training_result dpia_training_end(struct dc_link *link,
		uint32_t hop)
{
	enum link_training_result result;

	/** @todo Fail until implemented. */
	result = LINK_TRAINING_ABORT;

	return result;
}

enum link_training_result dc_link_dpia_perform_link_training(struct dc_link *link,
	const struct dc_link_settings *link_setting,
	bool skip_video_pattern)
{
	enum link_training_result result;
	struct link_training_settings lt_settings;
	uint8_t repeater_cnt = 0; /* Number of hops/repeaters in display path. */
	uint8_t repeater_id; /* Current hop. */

	/* Configure link as prescribed in link_setting and set LTTPR mode. */
	result = dpia_configure_link(link, link_setting, &lt_settings);
	if (result != LINK_TRAINING_SUCCESS)
		return result;

	if (link->lttpr_mode == LTTPR_MODE_NON_TRANSPARENT)
		repeater_cnt = dp_convert_to_count(link->dpcd_caps.lttpr_caps.phy_repeater_cnt);

	/* Train each hop in turn starting with the one closest to DPTX.
	 * In transparent or non-LTTPR mode, train only the final hop (DPRX).
	 */
	for (repeater_id = repeater_cnt; repeater_id >= 0; repeater_id--) {
		/* Clock recovery. */
		result = dpia_training_cr_phase(link, &lt_settings, repeater_id);
		if (result != LINK_TRAINING_SUCCESS)
			break;

		/* Equalization. */
		result = dpia_training_eq_phase(link, &lt_settings, repeater_id);
		if (result != LINK_TRAINING_SUCCESS)
			break;

		/* Stop training hop. */
		result = dpia_training_end(link, repeater_id);
		if (result != LINK_TRAINING_SUCCESS)
			break;
	}

	/* Double-check link status if training successful; gracefully stop
	 * training of current hop if training failed for any reason other than
	 * sink unplug.
	 */
	if (result == LINK_TRAINING_SUCCESS) {
		msleep(5);
		result = dp_check_link_loss_status(link, &lt_settings);
	} else if (result != LINK_TRAINING_ABORT) {
		dpia_training_end(link, repeater_id);
	}
	return result;
}
