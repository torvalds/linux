/*
 * Copyright 2022 Advanced Micro Devices, Inc.
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

/* FILE POLICY AND INTENDED USAGE:
 * This file implements FRL link capability and link training related functions.
 * FRL link is established by order of retrieve_link, verify_link, and poll_status.
 * Other helper functions exist to obtain information required to maintain
 * the correct sequence according to HDMI specification. Each sequence and state
 * inside link training functions are timing sensitive and order sensitive.
 * It is mandatory that these functions are debugged with FRL_LTP output message
 * configurable in DSAT.  Any changes in the LT sequence should follow the HDMI
 * specification as much as possible and tested through HDMI electrical and
 * link layer compliance.
 */
#include "link_hdmi_frl.h"
#include "link_ddc.h"
#include "link/link_dpms.h"
#include "link/link_validation.h"
#include "resource.h"
#include "dccg.h"

#include "dml/dml1_frl_cap_chk.h"

#define DC_LOGGER \
	dc_logger
#define DC_LOGGER_INIT(logger) \
	struct dal_logger *dc_logger = logger

#define FRL_INFO(...) \
	DC_LOG_HDMI_FRL_LTP(  \
		__VA_ARGS__)

static bool hdmi_frl_test_max_rate(struct ddc_service *ddc_service)
{
	uint8_t slave_address = HDMI_SCDC_ADDRESS;
	uint8_t offset = HDMI_SCDC_SOURCE_TEST_REQ;
	union hdmi_scdc_source_test_req test_req = {0};

	DC_LOGGER_INIT(ddc_service->link->ctx->logger);

	link_query_ddc_data(ddc_service, slave_address,
					&offset, sizeof(offset), &test_req.byte,
					sizeof(test_req.byte));
	if (test_req.fields.FRL_MAX) {
		FRL_INFO("FRL TEST REQ:  FRL_MAX = 1");
		return true;
	}

	return false;
}

static void hdmi_return_preeshoot_and_deemphasis(struct dc_link *link,
		union hdmi_scdc_source_test_req *test_req, bool *de_emphasis_only,
		bool *pre_shoot_only, bool *no_ffe)
{
	/* check if cable_id is valid */
	if (link->hdmi_cable_id.raw[0] && link->frl_link_settings.frl_link_rate >=
		HDMI_FRL_LINK_RATE_16GBPS) {
		*de_emphasis_only = (test_req->fields.TXFFE_DEEMPHASIS && link->hdmi_cable_id.bits.no_DeEmphasis_n) ||
					!link->hdmi_cable_id.bits.no_PreShoot_n;
		*pre_shoot_only = (test_req->fields.TXFFE_PRESHOOT && link->hdmi_cable_id.bits.no_PreShoot_n) ||
					!link->hdmi_cable_id.bits.no_DeEmphasis_n;
		*no_ffe = test_req->fields.TXFFE_NOFFE ||
					(!link->hdmi_cable_id.bits.no_PreShoot_n &&
					 !link->hdmi_cable_id.bits.no_DeEmphasis_n);
	} else {
		*de_emphasis_only = test_req->fields.TXFFE_DEEMPHASIS;
		*pre_shoot_only = test_req->fields.TXFFE_PRESHOOT;
		*no_ffe = test_req->fields.TXFFE_NOFFE;
	}
}

static bool hdmi_frl_test_dsc_max_rate(struct ddc_service *ddc_service)
{
	uint8_t slave_address = HDMI_SCDC_ADDRESS;
	uint8_t offset = HDMI_SCDC_SOURCE_TEST_REQ;
	union hdmi_scdc_source_test_req test_req = {0};

	DC_LOGGER_INIT(ddc_service->link->ctx->logger);

	link_query_ddc_data(ddc_service, slave_address,
					&offset, sizeof(offset), &test_req.byte,
					sizeof(test_req.byte));
	if (test_req.fields.DSC_FRL_MAX) {
		FRL_INFO("FRL TEST REQ:  DSC_FRL_MAX = 1");
		return true;
	}

	return false;
}

enum clock_source_id hdmi_frl_find_matching_phypll(
		struct dc_link *link)
{
	switch (link->link_enc->transmitter) {
	case TRANSMITTER_UNIPHY_A:
		return CLOCK_SOURCE_COMBO_PHY_PLL0;
	case TRANSMITTER_UNIPHY_B:
		return CLOCK_SOURCE_COMBO_PHY_PLL1;
	case TRANSMITTER_UNIPHY_C:
		return CLOCK_SOURCE_COMBO_PHY_PLL2;
	case TRANSMITTER_UNIPHY_D:
		return CLOCK_SOURCE_COMBO_PHY_PLL3;
	case TRANSMITTER_UNIPHY_E:
		return CLOCK_SOURCE_COMBO_PHY_PLL4;
	case TRANSMITTER_UNIPHY_F:
		return CLOCK_SOURCE_COMBO_PHY_PLL5;
	default:
		return CLOCK_SOURCE_ID_UNDEFINED;
	};
}

void hdmi_frl_retrieve_link_cap(struct dc_link *link, struct dc_sink *sink)
{
	enum hdmi_frl_link_rate encoder_link_rate = HDMI_FRL_LINK_RATE_6GBPS_4LANE;

	if (link->link_enc->features.flags.bits.IS_FRL_8G_CAPABLE)
		encoder_link_rate = HDMI_FRL_LINK_RATE_8GBPS;

	if (link->link_enc->features.flags.bits.IS_FRL_10G_CAPABLE)
		encoder_link_rate = HDMI_FRL_LINK_RATE_10GBPS;

	if (link->link_enc->features.flags.bits.IS_FRL_12G_CAPABLE)
		encoder_link_rate = HDMI_FRL_LINK_RATE_12GBPS;

	if (link->dc->debug.max_frl_rate != 0 && encoder_link_rate > link->dc->debug.max_frl_rate)
		encoder_link_rate = link->dc->debug.max_frl_rate;

	if (link->frl_flags.force_frl_rate != 0 &&
			link->frl_flags.force_frl_rate != 0xF)
		encoder_link_rate = link->frl_flags.force_frl_rate;

	link->frl_reported_link_cap.frl_link_rate =
			(encoder_link_rate < sink->edid_caps.max_frl_rate) ?
					encoder_link_rate : sink->edid_caps.max_frl_rate;

	if (sink->edid_caps.max_frl_rate < HDMI_FRL_LINK_RATE_6GBPS_4LANE)
		link->frl_reported_link_cap.frl_num_lanes = 3;
	else
		link->frl_reported_link_cap.frl_num_lanes = 4;
}

struct dc_hdmi_frl_link_settings *hdmi_frl_get_verified_link_cap(
		struct dc_link *link)
{
	// TODO: rework hdmi_frl_get_verified_link_cap to be a const interface
	return &link->frl_verified_link_cap;
}


void hdmi_frl_LTS_clear_Update_flag(struct ddc_service *ddc_service)
{
	uint8_t slave_address = HDMI_SCDC_ADDRESS;
	uint8_t offset = HDMI_SCDC_UPDATE_0;
	uint8_t write_buffer[2] = { 0 };
	union hdmi_scdc_update_read_data scdc_update = {0};
	DC_LOGGER_INIT(ddc_service->link->ctx->logger);

	/*Check FLT_update flag*/
	link_query_ddc_data(ddc_service, slave_address,
					&offset, sizeof(offset), &scdc_update.byte[0],
					sizeof(scdc_update.byte[0]));

	if (scdc_update.fields.FLT_UPDATE != 0) {
		FRL_INFO("FRL LINK TRAINING:  LTS:L Clear FLT_UPDATE.\n");
		write_buffer[0] = HDMI_SCDC_UPDATE_0;
		/*FLT_update - bit 5*/
		write_buffer[1] = (scdc_update.fields.FLT_UPDATE << 5);
		link_query_ddc_data(ddc_service, slave_address,
				write_buffer, sizeof(write_buffer), NULL, 0);
	}
}


bool hdmi_frl_poll_status_flag(struct dc_link *link)
{
	uint8_t slave_address = HDMI_SCDC_ADDRESS;
	uint8_t offset = 0;
	uint32_t ln0_pattern = 0;
	uint32_t ln1_pattern = 0;
	uint32_t ln2_pattern = 0;
	uint32_t ln3_pattern = 0;
	uint8_t write_buffer[2] = {0};
	union hdmi_scdc_update_read_data scdc_update = {0};
	union hdmi_scdc_source_test_req test_req = {0};
	union hdmi_scdc_LTP_req_data ltp_req = {0};
	struct hpo_frl_link_encoder *hpo_frl_link_enc = link->hpo_frl_link_enc;
	struct link_encoder *dio_link_enc = link->link_enc;
	bool flt_no_timeout = false;
	bool link_update = false;

	/*Test Condition - FLT_no_timeout avoid link training*/
	offset = HDMI_SCDC_SOURCE_TEST_REQ;
	link_query_ddc_data(link->ddc, slave_address, &offset,
			sizeof(offset), &test_req.byte, sizeof(test_req.byte));
	if (test_req.fields.FLT_NO_TIMEOUT)
		flt_no_timeout = true;

	offset = HDMI_SCDC_UPDATE_0;

	/*Check FLT_update flag*/
	link_query_ddc_data(link->ddc, slave_address,
					&offset, sizeof(offset), &scdc_update.byte[0],
					sizeof(scdc_update.byte[0]));

	if (scdc_update.fields.FRL_START == 1) {
		write_buffer[0] = HDMI_SCDC_UPDATE_0;
		/*FLT_START - bit 4*/
		write_buffer[1] = (scdc_update.fields.FRL_START << 4);
		link_query_ddc_data(link->ddc, slave_address,
				write_buffer, sizeof(write_buffer), NULL, 0);
	}
	if (scdc_update.fields.FLT_UPDATE) {
		offset = HDMI_SCDC_LTP_REQ;

		link_query_ddc_data(link->ddc, slave_address,
						&offset, sizeof(offset), ltp_req.byte,
						sizeof(ltp_req.byte));

		ln0_pattern = ltp_req.fields.LN0_LTP_REQ;
		ln1_pattern = ltp_req.fields.LN1_LTP_REQ;
		ln2_pattern = ltp_req.fields.LN2_LTP_REQ;
		ln3_pattern = ltp_req.fields.LN3_LTP_REQ;

		if (ln0_pattern == 0x03 || ln1_pattern == 0x03 ||
				ln2_pattern == 0x03 || ln3_pattern == 0x03) {
			if (flt_no_timeout) {
				hpo_frl_link_enc->funcs->set_hdmi_training_pattern(
					hpo_frl_link_enc,
					ln0_pattern - 1,
					ln1_pattern - 1,
					ln2_pattern - 1,
					ln3_pattern - 1);
			} else
				hpo_frl_link_enc->funcs->set_hdmi_training_pattern(
					hpo_frl_link_enc,
					(ln0_pattern == 0x03) ? 0xF : ln0_pattern - 1,
					(ln1_pattern == 0x03) ? 0xF : ln1_pattern - 1,
					(ln2_pattern == 0x03) ? 0xF : ln2_pattern - 1,
					(ln3_pattern == 0x03) ? 0xF : ln3_pattern - 1);
		} else
			hpo_frl_link_enc->funcs->set_hdmi_training_pattern(
				hpo_frl_link_enc,
				ln0_pattern - 1,
				ln1_pattern - 1,
				ln2_pattern - 1,
				ln3_pattern - 1);
		if (ln0_pattern == 0x0E || ln1_pattern == 0x0E ||
								ln2_pattern == 0x0E  || ln3_pattern == 0x0E) {
			if (scdc_update.fields.FLT_UPDATE) {
				if (link->dc->debug.limit_ffe == 0)
					return false;

				bool de_emphasis_only = false;
				bool pre_shoot_only = false;
				bool no_ffe = false;

				hdmi_return_preeshoot_and_deemphasis(link, &test_req,
					&de_emphasis_only, &pre_shoot_only, &no_ffe);

				dio_link_enc->funcs->prog_eq_setting(dio_link_enc, 0xEE,
						de_emphasis_only,
						pre_shoot_only,
						no_ffe,
						&link->frl_link_settings);
			}
		} else
			if (!flt_no_timeout) {
				hdmi_frl_LTS_clear_Link_Setting(link->ddc);
				hdmi_frl_LTS_clear_Update_flag(link->ddc);
				hpo_frl_link_enc->funcs->set_hdmi_training_pattern(hpo_frl_link_enc, 0, 0, 0, 0);
				hdmi_frl_perform_link_training_with_retries(link);
				link_update = true;
			}

		write_buffer[0] = HDMI_SCDC_UPDATE_0;
		/*Clear SCDC Update Flags*/
		write_buffer[1] = (scdc_update.fields.FLT_UPDATE << 5);
		link_query_ddc_data(link->ddc, slave_address,
				write_buffer, sizeof(write_buffer), NULL, 0);
	}
	if (scdc_update.fields.SOURCE_TEST_UPDATE) {
		offset = HDMI_SCDC_SOURCE_TEST_REQ;
		bool de_emphasis_only = false;
		bool pre_shoot_only = false;
		bool no_ffe = false;

		link_query_ddc_data(link->ddc, slave_address,
						&offset, sizeof(offset), &test_req.byte,
						sizeof(test_req.byte));

		hdmi_return_preeshoot_and_deemphasis(link, &test_req,
			&de_emphasis_only, &pre_shoot_only, &no_ffe);

		dio_link_enc->funcs->prog_eq_setting(dio_link_enc, 0xFF,
				de_emphasis_only,
				pre_shoot_only,
				no_ffe,
				&link->frl_link_settings);


		write_buffer[0] = HDMI_SCDC_UPDATE_0;
		/*Clear SCDC Update Flags*/
		write_buffer[1] = (scdc_update.fields.SOURCE_TEST_UPDATE << 3);
		link_query_ddc_data(link->ddc, slave_address,
				write_buffer, sizeof(write_buffer), NULL, 0);

	}

	return link_update;
}

void hdmi_frl_poll_start(struct ddc_service *ddc_service)
{
	uint8_t slave_address = HDMI_SCDC_ADDRESS;
	uint8_t offset = HDMI_SCDC_UPDATE_0;
	uint8_t write_buffer[2] = {0};
	union hdmi_scdc_source_test_req test_req = {0};
	union hdmi_scdc_update_read_data scdc_update = {0};
	uint16_t num_polls = 100;
	uint16_t wait_time = 2000;
	bool flt_no_timeout = false;

	DC_LOGGER_INIT(ddc_service->link->ctx->logger);

	/*Test Condition - FLT_no_timeout avoid link training*/
	offset = HDMI_SCDC_SOURCE_TEST_REQ;
	link_query_ddc_data(ddc_service, slave_address, &offset,
			sizeof(offset), &test_req.byte, sizeof(test_req.byte));
	if (test_req.fields.FLT_NO_TIMEOUT)
		flt_no_timeout = true;

	/*Test Condition - No need to poll FRL_START if FLT_no_timeout*/
	if (flt_no_timeout)
		return;

	offset = HDMI_SCDC_UPDATE_0;
	/*LTS:P: Check FRL_START, poll for 200ms */
	for (; num_polls; num_polls--) {
		udelay(wait_time);
		/*Check FLT_update flag*/
		link_query_ddc_data(ddc_service, slave_address,
						&offset, sizeof(offset), &scdc_update.byte[0],
						sizeof(scdc_update.byte[0]));
		FRL_INFO("FRL LINK TRAINING:  Read FRL_START = %d, FLT_UPDATE = %d.  num_polls = %d\n",
				scdc_update.fields.FRL_START, scdc_update.fields.FLT_UPDATE, num_polls);
		if (scdc_update.fields.FRL_START == 1) {
			write_buffer[0] = HDMI_SCDC_UPDATE_0;
			/*FLT_update - bit 5*/
			write_buffer[1] = (scdc_update.fields.FRL_START << 4);
			link_query_ddc_data(ddc_service, slave_address,
					write_buffer, sizeof(write_buffer), NULL, 0);
			break;
		}
		if (scdc_update.fields.FLT_UPDATE == 1) {
			write_buffer[0] = HDMI_SCDC_UPDATE_0;
				/*FLT_update - bit 5*/
			write_buffer[1] = (scdc_update.fields.FLT_UPDATE << 5);
			link_query_ddc_data(ddc_service, slave_address,
					write_buffer, sizeof(write_buffer), NULL, 0);
			break;
		}
	}
	return;
}

void hdmi_frl_LTS_clear_Link_Setting(struct ddc_service *ddc_service)
{
	uint8_t slave_address = HDMI_SCDC_ADDRESS;
	uint8_t offset = HDMI_SCDC_CONFIG_1;
	uint8_t write_buffer[2] = { 0 };
	union hdmi_scdc_configuration scdc_config = {0};
	DC_LOGGER_INIT(ddc_service->link->ctx->logger);

	/*Check FRL_Rate for fallback*/
	link_query_ddc_data(ddc_service, slave_address,
					&offset, sizeof(offset), &scdc_config.byte[1],
					sizeof(scdc_config.byte[1]));
	if (scdc_config.fields.FRL_RATE != 0) {
		/*LTS:L FRL_Rate = 0*/
		write_buffer[0] = HDMI_SCDC_CONFIG_1;
		/*FRL_RATE*/
		write_buffer[1] = 0;
		link_query_ddc_data(ddc_service, slave_address,
				write_buffer, sizeof(write_buffer), NULL, 0);
		FRL_INFO("FRL LINK TRAINING: LTS:L - Clear FRL_Rate.\n");
	}

}

static enum link_result hdmi_frl_perform_link_training(struct ddc_service *ddc_service,
		struct dc_hdmi_frl_link_settings *link_settings)
{
	enum link_result result = LINK_RESULT_UNKNOWN;
	uint8_t slave_address = HDMI_SCDC_ADDRESS;
	uint8_t offset = HDMI_SCDC_STATUS_FLAGS;
	uint32_t ln0_pattern = 0, pre_ln0_pattern = 0;
	uint32_t ln1_pattern = 0, pre_ln1_pattern = 0;
	uint32_t ln2_pattern = 0, pre_ln2_pattern = 0;
	uint32_t ln3_pattern = 0, pre_ln3_pattern = 0;
	uint8_t write_buffer[2] = {0};
	union hdmi_scdc_update_read_data scdc_update = {0};
	union hdmi_scdc_status_flags_data status_data = {0};
	union hdmi_scdc_source_test_req test_req = {0};
	union hdmi_scdc_LTP_req_data ltp_req = {0};
	uint16_t num_polls = 0;
	uint16_t max_polls = 105;
	unsigned long long wait_time_ns = 2000000;
	struct hpo_frl_link_encoder *hpo_frl_link_enc = ddc_service->link->hpo_frl_link_enc;
	struct link_encoder *dio_link_enc = ddc_service->link->link_enc;
	uint8_t sink_version = 0;
	uint8_t FFE_Levels = (uint8_t)ddc_service->link->dc->debug.limit_ffe;
	uint8_t current_FFE = 0;
	bool override_FFE = false;
	bool flt_no_timeout = false;
	unsigned long long flt_poll_cur_time = 0, flt_poll_last_time = 0, flt_poll_elapsed_time_ns = 0;

	DC_LOGGER_INIT(ddc_service->link->ctx->logger);
	FRL_INFO("FRL LINK TRAINING:  Starting FRL Link Training.\n");

	hdmi_frl_LTS_clear_Link_Setting(ddc_service);

	offset = HDMI_SCDC_SINK_VERSION;
	link_query_ddc_data(ddc_service, slave_address, &offset,
			sizeof(offset), &sink_version, sizeof(sink_version));

	FRL_INFO("FRL LINK TRAINING:  Read Sink Version = %d.\n", sink_version);

	if (sink_version == 0) {
		FRL_INFO("FRL LINK TRAINING: SKIP - FRL not supported by sink.\n");
		return LINK_RESULT_FALLBACK;
	}

	if (sink_version == 1) {
		/*Source Version = 1*/
		write_buffer[0] = HDMI_SCDC_SOURCE_VERSION;
		write_buffer[1] = 1;
		link_query_ddc_data(ddc_service, slave_address,
				write_buffer, sizeof(write_buffer), NULL, 0);
		FRL_INFO("FRL LINK TRAINING:  Set Source Version = 1.\n");
	}

	FRL_INFO("FRL LINK TRAINING:  Poll for FLT_READY.\n");
	offset = HDMI_SCDC_STATUS_FLAGS;

	/*LTS:2: Check FLT Ready, poll for 200ms */
	while (num_polls < max_polls) {
		flt_poll_cur_time = dm_get_timestamp(ddc_service->ctx);
		flt_poll_elapsed_time_ns = dm_get_elapse_time_in_ns(ddc_service->ctx, flt_poll_cur_time, flt_poll_last_time);
		if (flt_poll_elapsed_time_ns < wait_time_ns)
			continue;
		flt_poll_last_time = dm_get_timestamp(ddc_service->ctx);
		num_polls++;

		link_query_ddc_data(ddc_service, slave_address,
				&offset, sizeof(offset), &status_data.byte,
				sizeof(status_data.byte));
		FRL_INFO("FRL LINK TRAINING:  Read FLT_READY = %d.  num_polls = %d\n",
				status_data.fields.FLT_READY, num_polls);
		if (status_data.fields.FLT_READY) {
			/* Spec recommends to clear update flag, but QD980 has problem */
			hdmi_frl_LTS_clear_Update_flag(ddc_service);
			dio_link_enc->funcs->prog_eq_setting(dio_link_enc, 0,
					test_req.fields.TXFFE_DEEMPHASIS,
					test_req.fields.TXFFE_PRESHOOT,
					test_req.fields.TXFFE_NOFFE,
					link_settings);
			break;
		}
	}

	/*Test Condition - FLT_no_timeout avoid link training*/
	offset = HDMI_SCDC_SOURCE_TEST_REQ;
	link_query_ddc_data(ddc_service, slave_address, &offset,
			sizeof(offset), &test_req.byte, sizeof(test_req.byte));
	FRL_INFO("FRL TEST REQ:  FLT_no_timeout = %d \n", test_req.fields.FLT_NO_TIMEOUT);
	if (test_req.fields.FLT_NO_TIMEOUT)
		flt_no_timeout = true;

	if (status_data.fields.FLT_READY) {
		/*Specify FRL rate*/
		write_buffer[0] = HDMI_SCDC_CONFIG_1;
		/*FRL_RATE*/
		write_buffer[1] = (uint8_t)(link_settings->frl_link_rate | (FFE_Levels << 4));
		link_query_ddc_data(ddc_service, slave_address,
				write_buffer, sizeof(write_buffer), NULL, 0);
		FRL_INFO("FRL LINK TRAINING:  Write link rate = %d.  Max FFE_Levels = %d\n",
				link_settings->frl_link_rate, FFE_Levels);

		FRL_INFO("FRL LINK TRAINING:  Poll for FLT_UPDATE.\n");
		/*LTS:3: Start Link Training*/
		/*Start FLT Timer = 200 ms*/
		num_polls = 0;
		if (flt_no_timeout)
			max_polls = 500;

		while (num_polls < max_polls) {
			flt_poll_cur_time = dm_get_timestamp(ddc_service->ctx);
			flt_poll_elapsed_time_ns = dm_get_elapse_time_in_ns(ddc_service->ctx, flt_poll_cur_time, flt_poll_last_time);
			if (flt_poll_elapsed_time_ns < wait_time_ns)
				continue;
			flt_poll_last_time = flt_poll_cur_time;

			num_polls++;

			offset = HDMI_SCDC_UPDATE_0;
			/*Check FLT_update flag*/
			link_query_ddc_data(ddc_service, slave_address,
							&offset, sizeof(offset), &scdc_update.byte[0],
							sizeof(scdc_update.byte[0]));

			FRL_INFO("FRL LINK TRAINING:  Read FLT_UPDATE = %d.  num_polls = %d\n",
					scdc_update.fields.FLT_UPDATE, num_polls);
			/*Set TxFFE = TxFFE0*/
			/*Program FFE_Levels - scdc_config has this field at 0 */
			if (override_FFE) {
				if (flt_no_timeout)
					current_FFE = 0;
				if (current_FFE == 0)
					dio_link_enc->funcs->prog_eq_setting(dio_link_enc, current_FFE,
							test_req.fields.TXFFE_DEEMPHASIS,
							test_req.fields.TXFFE_PRESHOOT,
							test_req.fields.TXFFE_NOFFE,
							link_settings);
				else {
					if (scdc_update.fields.FLT_UPDATE)
						dio_link_enc->funcs->prog_eq_setting(dio_link_enc, current_FFE,
								test_req.fields.TXFFE_DEEMPHASIS,
								test_req.fields.TXFFE_PRESHOOT,
								test_req.fields.TXFFE_NOFFE,
								link_settings);
				}
				FRL_INFO("FRL LINK TRAINING:  TxFFE = %d.\n", current_FFE);
				override_FFE = false;
			}
			if (scdc_update.fields.FLT_UPDATE) {
				offset = HDMI_SCDC_LTP_REQ;
				link_query_ddc_data(ddc_service, slave_address,
								&offset, sizeof(offset), ltp_req.byte,
								sizeof(ltp_req.byte));

				pre_ln0_pattern = ln0_pattern;
				pre_ln1_pattern = ln1_pattern;
				pre_ln2_pattern = ln2_pattern;
				pre_ln3_pattern = ln3_pattern;

				ln0_pattern = ltp_req.fields.LN0_LTP_REQ;
				ln1_pattern = ltp_req.fields.LN1_LTP_REQ;
				ln2_pattern = ltp_req.fields.LN2_LTP_REQ;
				ln3_pattern = ltp_req.fields.LN3_LTP_REQ;

				FRL_INFO("FRL LINK TRAINING:  Read LN0_LTP_REQ = %d.  LN1_LTP_REQ = %d\n",
						ltp_req.fields.LN0_LTP_REQ,
						ltp_req.fields.LN1_LTP_REQ);
				FRL_INFO("FRL LINK TRAINING:  Read LN2_LTP_REQ = %d.  LN3_LTP_REQ = %d\n",
						ltp_req.fields.LN2_LTP_REQ,
						ltp_req.fields.LN3_LTP_REQ);

				/*Clear FLT_update flag*/
				FRL_INFO("FRL LINK TRAINING:  Clear FLT_UPDATE flag.\n");
				write_buffer[0] = HDMI_SCDC_UPDATE_0;
				/*FLT_update - bit 5*/
				write_buffer[1] = (scdc_update.fields.FLT_UPDATE << 5);
				link_query_ddc_data(ddc_service, slave_address,
						write_buffer, sizeof(write_buffer), NULL, 0);

				if (ln0_pattern == 0x03 || ln1_pattern == 0x03 ||
						ln2_pattern == 0x03 || ln3_pattern == 0x03)
					if (!flt_no_timeout)
						continue;

				if (link_settings->frl_num_lanes == 3) {
					ln3_pattern = 1;
					if (!ln0_pattern && !ln1_pattern && !ln2_pattern) {
						/*Link Training is done*/
						FRL_INFO("FRL LINK TRAINING:  PASSED\n");
						return LINK_RESULT_SUCCESS;
					}
					if (ln0_pattern == 0x0F || ln1_pattern == 0x0F || ln2_pattern == 0x0F) {
						/*sink requesting to lower link rate*/
						FRL_INFO("FRL LINK TRAINING:  Sink requesting lower link rate.\n");
						return LINK_RESULT_LOWER_LINKRATE;
					}
					if (ln0_pattern == 0x0E || ln1_pattern == 0x0E || ln2_pattern == 0x0E) {
						/*sink requesting to next FFE*/
						FRL_INFO("FRL LINK TRAINING:  Sink requesting next FFE.\n");
						if (ddc_service->link->dc->debug.limit_ffe == 0) {
							return LINK_RESULT_LOWER_LINKRATE;
						}
						current_FFE++;
						override_FFE = true;
						if (current_FFE > 3)
							current_FFE = 0;
						if (flt_no_timeout)
							current_FFE = 0;
					}
				} else {
					if (!ln0_pattern && !ln1_pattern && !ln2_pattern && !ln3_pattern) {
						/*Link Training is done*/
						FRL_INFO("FRL LINK TRAINING:  PASSED\n");
						return LINK_RESULT_SUCCESS;
					}
					if (ln0_pattern == 0x0F || ln1_pattern == 0x0F ||
						ln2_pattern == 0x0F || ln3_pattern == 0x0F) {
						/*sink requesting to lower link rate*/
						FRL_INFO("FRL LINK TRAINING:  Sink requesting lower link rate.\n");
						return LINK_RESULT_LOWER_LINKRATE;
					}
					if (ln0_pattern == 0x0E || ln1_pattern == 0x0E ||
						ln2_pattern == 0x0E  || ln3_pattern == 0x0E) {
						/*sink requesting to next FFE*/
						FRL_INFO("FRL LINK TRAINING:  Sink requesting next FFE.\n");
						if (ddc_service->link->dc->debug.limit_ffe == 0) {
							return LINK_RESULT_LOWER_LINKRATE;
						}
						current_FFE++;
						override_FFE = true;
						if (current_FFE > 3)
							current_FFE = 0;
						if (flt_no_timeout)
							current_FFE = 0;
					}
				}

				if (override_FFE) {
					ln0_pattern = pre_ln0_pattern;
					ln1_pattern = pre_ln1_pattern;
					ln2_pattern = pre_ln2_pattern;
					ln3_pattern = pre_ln3_pattern;
				}

				FRL_INFO("FRL LINK TRAINING:  Setting Training Pattern [ln0,ln1,ln2,ln3] = [%d,%d,%d,%d].\n",
						ln0_pattern, ln1_pattern, ln2_pattern, ln3_pattern);

				hpo_frl_link_enc->funcs->set_hdmi_training_pattern(
					hpo_frl_link_enc,
					ln0_pattern - 1,
					ln1_pattern - 1,
					ln2_pattern - 1,
					ln3_pattern - 1);
				/* Workaround for DEDCN3AG-111
				 * HDMI-FRL Incorrect Serialization Order for LTP4
				 */
			}

		}
		if (flt_no_timeout) {
			return LINK_RESULT_SUCCESS;
		} else {
			FRL_INFO("FRL LINK TRAINING:  FAILED - Timeout waiting for FLT_UPDATE to be set by sink.\n");
			write_buffer[0] = HDMI_SCDC_CONFIG_1;
			/*FRL_RATE*/
			write_buffer[1] = HDMI_FRL_LINK_RATE_DISABLE | (0 << 4);
			link_query_ddc_data(ddc_service, slave_address,
					write_buffer, sizeof(write_buffer), NULL, 0);
			hdmi_frl_LTS_clear_Link_Setting(ddc_service);
			result = LINK_RESULT_TIMEOUT;
		}
	} else {
		FRL_INFO("FRL LINK TRAINING:  FAILED - FLT_READY not set by sink.\n");
		result = LINK_RESULT_TIMEOUT;
	}

	return result;
}

enum link_result hdmi_frl_perform_link_training_with_retries(
	struct dc_link *link)
{
	enum link_result status = LINK_RESULT_UNKNOWN;
	unsigned int retry_count = 0;
	unsigned int max_retries = 3;

	DC_LOGGER_INIT(link->ctx->logger);

	if (link->preferred_hdmi_frl_settings.valid)
		max_retries = link->preferred_hdmi_frl_settings.max_retries;

	/* FRL Link Training */
	while (status != LINK_RESULT_FALLBACK) {
		if (status == LINK_RESULT_SUCCESS) {
			break;
		} else if (status == LINK_RESULT_LOWER_LINKRATE) {
			FRL_INFO("FRL LINK TRAINING:  Sink requested lower link rate during link enable. \n");
			break;
		} else {
			if (retry_count > 0)
				msleep(200);
			status = hdmi_frl_perform_link_training(link->ddc,
					&link->frl_link_settings);
		};
		retry_count++;
		FRL_INFO("FRL LINK TRAINING: Retry count = %u out of %u\n", retry_count, max_retries);
		if (retry_count > max_retries) {
			status = LINK_RESULT_FALLBACK;
			break;
		}
	}

	return status;
}

enum link_result hdmi_frl_perform_link_training_with_fallback(
	struct dc_link *link, struct link_resource *link_res,
	enum clock_source_id frl_phy_clock_source_id)
{
	enum link_result status = LINK_RESULT_UNKNOWN;

	DC_LOGGER_INIT(link->ctx->logger);

	/* FRL Link Training */
	while (status != LINK_RESULT_FALLBACK) {
		if (link->frl_link_settings.frl_link_rate ==
				HDMI_FRL_LINK_RATE_DISABLE) {
			FRL_INFO("FRL LINK TRAINING:  Cannot Link Train.  Fall back to TMDS \n");
			status = LINK_RESULT_FALLBACK;
			break;
		}

		msleep(200);

		link->ctx->dc->hwss.setup_hdmi_frl_link(link, 0,
				frl_phy_clock_source_id);

		status = hdmi_frl_perform_link_training(link->ddc,
				&link->frl_link_settings);

		link->dc->hwss.disable_link_output(link, link_res, SIGNAL_TYPE_HDMI_FRL);

		if (status == LINK_RESULT_SUCCESS)
			break;

		link->frl_link_settings.frl_link_rate--;
		if (link->frl_link_settings.frl_link_rate <
				HDMI_FRL_LINK_RATE_6GBPS_4LANE)
			link->frl_link_settings.frl_num_lanes = 3;
	}

	return status;
}

void hdmi_frl_verify_link_cap(struct dc_link *link,
		struct dc_hdmi_frl_link_settings *known_limit_link_setting)
{
	struct dc_hdmi_frl_link_settings cur_link_setting = {0};
	struct dc_hdmi_frl_link_settings *cur = &cur_link_setting;
	bool success = false;
	enum link_result status = LINK_RESULT_UNKNOWN;
	enum clock_source_id frl_phy_clock_source_id;
	unsigned int t_id = link->link_enc->transmitter;
	struct link_resource link_res = {.hpo_frl_link_enc = link->hpo_frl_link_enc};
	struct dc_stream_state *link_stream = NULL;
	struct dc_stream_state *stream = NULL;
	int i;

	DC_LOGGER_INIT(link->ctx->logger);

	link->frl_flags.force_frl_rate =
			link->ctx->dc->debug.force_frl_rate;
	link->frl_flags.force_frl_always =
			link->preferred_hdmi_frl_settings.force_frl_always ||
			link->ctx->dc->debug.force_frl_always;
	link->frl_flags.force_frl_max =
			link->preferred_hdmi_frl_settings.force_frl_max ||
			link->ctx->dc->debug.force_frl_max ? true :
			hdmi_frl_test_max_rate(link->ddc);
	link->frl_flags.force_frl_dsc =
			link->ctx->dc->debug.force_frl_dsc ? true :
			hdmi_frl_test_dsc_max_rate(link->ddc);
	link->frl_flags.apply_vsdb_rcc_wa =
			link->ctx->dc->debug.apply_vsdb_rcc_wa;

	if (link->frl_flags.force_frl_rate == 0xF)
		return;

	if (link->local_sink &&
		link->local_sink->edid_caps.panel_patch.force_frl)
			link->frl_flags.force_frl_always = true;

	if (!link->frl_flags.force_frl_max &&
			!link->frl_flags.force_frl_dsc &&
			link->local_sink->edid_caps.panel_patch.hdmi_comp_auto) {
		link->frl_flags.force_frl_max = true;
		link->frl_flags.force_frl_dsc = true;
	}

	if (link->frl_flags.force_frl_max &&
			!link->frl_flags.force_frl_dsc &&
			link->local_sink->edid_caps.panel_patch.hdmi_comp_auto) {
		link->frl_flags.force_frl_dsc = true;
	}

	if (link->local_sink &&
		link->local_sink->edid_caps.panel_patch.vsdb_rcc_wa)
			link->frl_flags.apply_vsdb_rcc_wa = true;

	frl_phy_clock_source_id = hdmi_frl_find_matching_phypll(link);

	cur_link_setting = *known_limit_link_setting;

	if (link->frl_flags.force_frl_rate != 0) {
		cur->frl_link_rate = (cur_link_setting.frl_link_rate <
				link->frl_flags.force_frl_rate) ?
						cur_link_setting.frl_link_rate :
						link->frl_flags.force_frl_rate;
		link->frl_verified_link_cap = *cur;
		return;
	}

	if (link->local_sink) {
		if (link->local_sink->edid_caps.panel_patch.hdmi_spe_handling) {
			link->dc->hwss.disable_link_output(link, &link_res, link->connector_signal);
			link->dc->res_pool->clock_sources[t_id]->funcs->cs_power_down(
				link->dc->res_pool->clock_sources[t_id]);
			link->frl_verified_link_cap = *cur;
			return;
		}
		/* Monitor patch do decrease 10G to 8G*/
		if (link->local_sink->edid_caps.panel_patch.block_10g) {
			if (cur->frl_link_rate == HDMI_FRL_LINK_RATE_10GBPS)
				cur->frl_link_rate--;
		}
	}

	link->frl_link_settings = cur_link_setting;
	/* disable PHY first for PNP */
	if (link->dc->ctx->dce_version <= DCN_VERSION_3_0)
		link->dc->hwss.disable_link_output(link, &link_res, SIGNAL_TYPE_HDMI_FRL);
	else
		link->dc->hwss.disable_link_output(link, &link_res, link->connector_signal);

	link->dc->res_pool->clock_sources[t_id]->funcs->cs_power_down(
			link->dc->res_pool->clock_sources[t_id]);
	/*Either enable PHY ourselves or use VBIOS*/

	FRL_INFO("FRL LINK TRAINING: Validation\n");

	status = hdmi_frl_perform_link_training_with_fallback(link, &link_res, frl_phy_clock_source_id);

	if (status == LINK_RESULT_SUCCESS) {
		cur->frl_link_rate = link->frl_link_settings.frl_link_rate;
		cur->frl_num_lanes = link->frl_link_settings.frl_num_lanes;
		success = true;
		link->frl_verified_link_cap = *cur;
	}
	if (!success) {
		link->frl_verified_link_cap.frl_link_rate = HDMI_FRL_LINK_RATE_DISABLE;
		link->frl_verified_link_cap.frl_num_lanes = 3;
	}

	for (i = 0; i < MAX_STREAMS; i++) {
		stream = link->dc->current_state->streams[i];
		if (stream && stream->link == link) {
			link_stream = stream;
			break;
		}
	}

	if (link_stream) {
		link->dc->hwss.disable_link_output(link, &link_res, link_stream->signal);
	}
}

void hdmi_frl_set_preferred_link_settings(struct dc *dc,
		struct dc_hdmi_frl_link_settings *link_setting,
		struct dc_hdmi_frl_link_training_overrides *lt_overrides,
		struct dc_link *link)
{
	int i;
	struct pipe_ctx *pipe;
	struct dc_stream_state *link_stream = 0;
	struct pipe_ctx *link_pipe = 0;
	struct pipe_ctx *odm_pipe;
	int opp_cnt = 1;
	enum link_result link_stat = LINK_RESULT_UNKNOWN;
	enum clock_source_id frl_phy_clock_source_id;
	struct dc_stream_state *temp_stream = &dc->scratch.temp_stream;

	DC_LOGGER_INIT(link->ctx->logger);

	for (i = 0; i < MAX_PIPES; i++) {
		pipe = &dc->current_state->res_ctx.pipe_ctx[i];
		if (pipe->stream && pipe->stream->link) {
			if (pipe->stream->link == link) {
				link_stream = pipe->stream;
				link_pipe = pipe;
				break;
			}
		}
	}

	/* Stream not found */
	if (i == MAX_PIPES)
		return;

	FRL_INFO("FRL LINK TRAINING:  Preferred link Update = %d.\n", link_setting->frl_link_rate);

	frl_validate_mode_timing(link, &link_stream->timing, link_setting);

	if (lt_overrides)
		link->preferred_hdmi_frl_settings = *lt_overrides;
	else
		memset(&link->preferred_hdmi_frl_settings, 0, sizeof(link->preferred_hdmi_frl_settings));

	link_stream->link->frl_link_settings = *link_setting;
	link_stream->link->frl_verified_link_cap = *link_setting;

	while (link_stat != LINK_RESULT_SUCCESS) {
		link_set_dpms_off(pipe);
		/* For DCN3.0, can also have 4:1 combine mode.
		 * TODO: Add function get_odm_combine_mode that has different
		 *       implementation for DCN2/DCN3AG and DCN3.0
		 */
		for (odm_pipe = pipe->next_odm_pipe; odm_pipe; odm_pipe = odm_pipe->next_odm_pipe)
			opp_cnt++;

		memcpy(temp_stream, link_stream, sizeof(struct dc_stream_state));
		/* Modify patched_crtc_timing as required for padding */
		if (link_pipe->dsc_padding_params.dsc_hactive_padding) {
			temp_stream->timing.h_addressable = link_stream->timing.h_addressable + link_pipe->dsc_padding_params.dsc_hactive_padding;
			temp_stream->timing.h_total = link_stream->timing.h_total + link_pipe->dsc_padding_params.dsc_htotal_padding;
		}

		pipe->stream_res.hpo_frl_stream_enc->funcs->hdmi_frl_set_stream_attribute(
			pipe->stream_res.hpo_frl_stream_enc,
			&temp_stream->timing,
			&link_stream->link->frl_link_settings.borrow_params,
			opp_cnt);

		if (pipe->stream_res.tg->funcs->set_out_mux)
			pipe->stream_res.tg->funcs->set_out_mux(pipe->stream_res.tg, OUT_MUX_HPO_FRL);

		if ((!link_stream->link->link_enc) ||
				(!link_stream->link->hpo_frl_link_enc) ||
				(!link_stream->ctx->dc->res_pool->dccg->funcs->enable_hdmicharclk) ||
				(!(pipe->stream_res.hpo_frl_stream_enc)))
			return;

		switch (link_stream->link->frl_link_settings.frl_link_rate) {
		case HDMI_FRL_LINK_RATE_3GBPS:
			pipe->stream_res.pix_clk_params.requested_sym_clk = 166667;
			break;
		case HDMI_FRL_LINK_RATE_6GBPS:
		case HDMI_FRL_LINK_RATE_6GBPS_4LANE:
			pipe->stream_res.pix_clk_params.requested_sym_clk = 333333;
			break;
		case HDMI_FRL_LINK_RATE_8GBPS:
			pipe->stream_res.pix_clk_params.requested_sym_clk = 444444;
			break;
		case HDMI_FRL_LINK_RATE_10GBPS:
			pipe->stream_res.pix_clk_params.requested_sym_clk = 555555;
			break;
		case HDMI_FRL_LINK_RATE_12GBPS:
			pipe->stream_res.pix_clk_params.requested_sym_clk = 666667;
			break;
		case HDMI_FRL_LINK_RATE_16GBPS:
			pipe->stream_res.pix_clk_params.requested_sym_clk = 888889;
			break;
		case HDMI_FRL_LINK_RATE_20GBPS:
			pipe->stream_res.pix_clk_params.requested_sym_clk = 1111111;
			break;
		case HDMI_FRL_LINK_RATE_24GBPS:
			pipe->stream_res.pix_clk_params.requested_sym_clk = 1333333;
			break;
		default:
			break;
		}

		link_stream->phy_pix_clk = pipe->stream_res.pix_clk_params.requested_sym_clk;

		memset(&link_stream->link->cur_link_settings, 0,
				sizeof(struct dc_link_settings));

		/* Find proper clock source in HDMI FRL mode for phy used for DCCG */
		frl_phy_clock_source_id = hdmi_frl_find_matching_phypll(link);

		dc->hwss.setup_hdmi_frl_link(link,
				(pipe->stream_res.hpo_frl_stream_enc->id - ENGINE_ID_HPO_0),
				frl_phy_clock_source_id);

		FRL_INFO("FRL LINK TRAINING:  Start forced link training at %d. \n",
				link_stream->link->frl_link_settings.frl_link_rate);
		link_stat = hdmi_frl_perform_link_training_with_retries(link_stream->link);

		/* Enable FRL packet transmission */
		if (link_stat == LINK_RESULT_SUCCESS) {
			link_stream->link->hpo_frl_link_enc->funcs->enable_output(
					link_stream->link->hpo_frl_link_enc);
			if (link_stream->link->frl_flags.apply_vsdb_rcc_wa)
				link_stream->link->hpo_frl_link_enc->funcs->apply_vsdb_rcc_wa(link_stream->link->hpo_frl_link_enc);
			hdmi_frl_poll_start(link_stream->link->ddc);

			/* Set HDMISTREAMCLK source to DTBCLK0 and bypass DTO */
			if (dc->res_pool->dccg->funcs->set_hdmistreamclk) {
				dc->res_pool->dccg->funcs->set_hdmistreamclk(
						dc->res_pool->dccg,
						DTBCLK0,
						pipe->stream_res.tg->inst);
			}

			pipe->stream_res.hpo_frl_stream_enc->funcs->hdmi_frl_enable(
				pipe->stream_res.hpo_frl_stream_enc,
				pipe->stream_res.tg->inst);
			resource_build_info_frame(pipe);
			link_stream->ctx->dc->hwss.update_info_frame(pipe);

			if (link_stream->timing.flags.DSC)
				link_set_dsc_on_stream(pipe, true);

			link_stream->ctx->dc->hwss.enable_audio_stream(pipe);
			link_stream->ctx->dc->hwss.enable_stream(pipe);
			link_stream->ctx->dc->hwss.unblank_stream(pipe,
				&pipe->stream->link->cur_link_settings);
			FRL_INFO("FRL LINK TRAINING:  Forced link training successful. \n");
		}
		if (link_stat == LINK_RESULT_LOWER_LINKRATE) {
			link_stream->link->frl_link_settings.frl_link_rate--;
			if (link_stream->link->frl_link_settings.frl_link_rate >
				 HDMI_FRL_LINK_RATE_6GBPS)
				link_stream->link->frl_link_settings.frl_num_lanes = 4;
			else
				link_stream->link->frl_link_settings.frl_num_lanes = 3;
			FRL_INFO("FRL LINK TRAINING:  Lower link rate = %d.\n",
				link_stream->link->frl_link_settings.frl_link_rate);
		}
		if (link_stat == LINK_RESULT_FALLBACK) {
			FRL_INFO("FRL LINK TRAINING: Forced Link Training failed.  Fallback to TMDS. \n");
			break;
		}
	}
}

static void update_borrow_mode_from_dsc_padding(struct dsc_padding_params *dsc_padding_params,
	struct dc_crtc_timing *timing,
	struct dc_hdmi_frl_link_settings *frl_link_settings)
{
#ifdef CONFIG_DRM_AMD_DC_FP
	uint32_t h_active = timing->h_addressable + timing->h_border_left + timing->h_border_right;
	uint32_t h_blank = timing->h_total - h_active;
	struct frl_borrow_params *borrow_params = &frl_link_settings->borrow_params;

	borrow_params->borrow_mode = frl_modify_borrow_mode_for_dsc_padding(timing->pix_clk_100hz,
		h_active,
		h_active + dsc_padding_params->dsc_hactive_padding,
		h_blank,
		h_blank + dsc_padding_params->dsc_htotal_padding,
		borrow_params->hc_active_target,
		borrow_params->hc_blank_target,
		frl_link_settings->frl_num_lanes,
		frl_link_settings->frl_link_rate);
#endif
}

void hdmi_frl_decide_link_settings(struct dc_stream_state *stream,
	struct dc_hdmi_frl_link_settings *frl_link_settings,
	struct dsc_padding_params *dsc_padding_params)
{
	bool success = false;
	struct dc_hdmi_frl_link_settings temp_settings = {0};

	temp_settings.frl_link_rate = HDMI_FRL_LINK_RATE_3GBPS;
	temp_settings.frl_num_lanes = 3;

	/* Verify FRL and fill in borrow_params to verified_link_cap*/
	frl_validate_mode_timing(
		stream->link,
		&stream->timing,
		&stream->link->frl_verified_link_cap);

	if (stream->link->frl_flags.force_frl_rate != 0 &&
		stream->link->frl_flags.force_frl_rate < stream->link->frl_verified_link_cap.frl_link_rate) {
		switch (stream->link->frl_flags.force_frl_rate) {
		case HDMI_FRL_LINK_RATE_3GBPS:
			temp_settings.frl_link_rate = HDMI_FRL_LINK_RATE_3GBPS;
			temp_settings.frl_num_lanes = 3;
			break;
		case HDMI_FRL_LINK_RATE_6GBPS:
			temp_settings.frl_link_rate = HDMI_FRL_LINK_RATE_6GBPS;
			temp_settings.frl_num_lanes = 3;
			break;
		case HDMI_FRL_LINK_RATE_6GBPS_4LANE:
			temp_settings.frl_link_rate = HDMI_FRL_LINK_RATE_6GBPS;
			temp_settings.frl_num_lanes = 4;
			break;
		case HDMI_FRL_LINK_RATE_8GBPS:
			temp_settings.frl_link_rate = HDMI_FRL_LINK_RATE_8GBPS;
			temp_settings.frl_num_lanes = 4;
			break;
		case HDMI_FRL_LINK_RATE_10GBPS:
			temp_settings.frl_link_rate = HDMI_FRL_LINK_RATE_10GBPS;
			temp_settings.frl_num_lanes = 4;
			break;
		case HDMI_FRL_LINK_RATE_12GBPS:
			temp_settings.frl_link_rate = HDMI_FRL_LINK_RATE_12GBPS;
			temp_settings.frl_num_lanes = 4;
			break;
		case HDMI_FRL_LINK_RATE_16GBPS:
			temp_settings.frl_link_rate = HDMI_FRL_LINK_RATE_16GBPS;
			temp_settings.frl_num_lanes = 4;
			break;
		case HDMI_FRL_LINK_RATE_20GBPS:
			temp_settings.frl_link_rate = HDMI_FRL_LINK_RATE_20GBPS;
			temp_settings.frl_num_lanes = 4;
			break;
		case HDMI_FRL_LINK_RATE_24GBPS:
			temp_settings.frl_link_rate = HDMI_FRL_LINK_RATE_24GBPS;
			temp_settings.frl_num_lanes = 4;
			break;
		default:
			break;
		}
		*frl_link_settings = temp_settings;
		return;
	}
	/*test equipment requires max rate, identified at FRL_Max = 1*/
	if (stream->link->frl_flags.force_frl_max) {
		*frl_link_settings = stream->link->frl_verified_link_cap;
		return;
	}
	if (stream->link->frl_flags.force_frl_dsc) {
		*frl_link_settings = stream->link->frl_verified_link_cap;
		return;
	}

	if (stream->link->local_sink)
		if (stream->link->local_sink->edid_caps.panel_patch.hdmi_spe_handling) {
			*frl_link_settings = stream->link->frl_verified_link_cap;
			return;
		}

	do {
		success = frl_validate_mode_timing(
					stream->link,
					&stream->timing,
					&temp_settings);
		if (temp_settings.frl_link_rate ==
			 stream->link->frl_verified_link_cap.frl_link_rate)
			break;
		if (!success)
			temp_settings.frl_link_rate++;
		if (temp_settings.frl_link_rate > HDMI_FRL_LINK_RATE_6GBPS)
			temp_settings.frl_num_lanes = 4;
	} while (!success);

	*frl_link_settings = temp_settings;
	update_borrow_mode_from_dsc_padding(dsc_padding_params, &stream->timing, frl_link_settings);
}

void hdmi_frl_write_read_request_enable(struct ddc_service *ddc_service)
{
	uint8_t slave_address = HDMI_SCDC_ADDRESS;
	uint8_t offset = HDMI_SCDC_CONFIG_0;
	uint8_t scdc_config = 0;
	uint8_t write_buffer[2] = {0};

	link_query_ddc_data(ddc_service, slave_address, &offset,
			sizeof(offset), &scdc_config, sizeof(scdc_config));

	write_buffer[0] = HDMI_SCDC_CONFIG_0;
	write_buffer[1] = scdc_config;
	write_buffer[1] |= 0x1;

	link_query_ddc_data(ddc_service, slave_address, write_buffer,
		sizeof(write_buffer), NULL, 0);
}
