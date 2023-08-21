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
 * Authors: AMD
 *
 */

#include <linux/string.h>
#include <linux/acpi.h>
#include <linux/i2c.h>

#include <drm/drm_atomic.h>
#include <drm/drm_probe_helper.h>
#include <drm/amdgpu_drm.h>
#include <drm/drm_edid.h>

#include "dm_services.h"
#include "amdgpu.h"
#include "dc.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_irq.h"
#include "amdgpu_dm_mst_types.h"
#include "dpcd_defs.h"
#include "dc/inc/core_types.h"

#include "dm_helpers.h"
#include "ddc_service_types.h"

static u32 edid_extract_panel_id(struct edid *edid)
{
	return (u32)edid->mfg_id[0] << 24   |
	       (u32)edid->mfg_id[1] << 16   |
	       (u32)EDID_PRODUCT_ID(edid);
}

static void apply_edid_quirks(struct edid *edid, struct dc_edid_caps *edid_caps)
{
	uint32_t panel_id = edid_extract_panel_id(edid);

	switch (panel_id) {
	/* Workaround for some monitors which does not work well with FAMS */
	case drm_edid_encode_panel_id('S', 'A', 'M', 0x0E5E):
	case drm_edid_encode_panel_id('S', 'A', 'M', 0x7053):
	case drm_edid_encode_panel_id('S', 'A', 'M', 0x71AC):
		DRM_DEBUG_DRIVER("Disabling FAMS on monitor with panel id %X\n", panel_id);
		edid_caps->panel_patch.disable_fams = true;
		break;
	default:
		return;
	}
}

/**
 * dm_helpers_parse_edid_caps() - Parse edid caps
 *
 * @link: current detected link
 * @edid:	[in] pointer to edid
 * @edid_caps:	[in] pointer to edid caps
 *
 * Return: void
 */
enum dc_edid_status dm_helpers_parse_edid_caps(
		struct dc_link *link,
		const struct dc_edid *edid,
		struct dc_edid_caps *edid_caps)
{
	struct amdgpu_dm_connector *aconnector = link->priv;
	struct drm_connector *connector = &aconnector->base;
	struct edid *edid_buf = edid ? (struct edid *) edid->raw_edid : NULL;
	struct cea_sad *sads;
	int sad_count = -1;
	int sadb_count = -1;
	int i = 0;
	uint8_t *sadb = NULL;

	enum dc_edid_status result = EDID_OK;

	if (!edid_caps || !edid)
		return EDID_BAD_INPUT;

	if (!drm_edid_is_valid(edid_buf))
		result = EDID_BAD_CHECKSUM;

	edid_caps->manufacturer_id = (uint16_t) edid_buf->mfg_id[0] |
					((uint16_t) edid_buf->mfg_id[1])<<8;
	edid_caps->product_id = (uint16_t) edid_buf->prod_code[0] |
					((uint16_t) edid_buf->prod_code[1])<<8;
	edid_caps->serial_number = edid_buf->serial;
	edid_caps->manufacture_week = edid_buf->mfg_week;
	edid_caps->manufacture_year = edid_buf->mfg_year;

	drm_edid_get_monitor_name(edid_buf,
				  edid_caps->display_name,
				  AUDIO_INFO_DISPLAY_NAME_SIZE_IN_CHARS);

	edid_caps->edid_hdmi = connector->display_info.is_hdmi;

	sad_count = drm_edid_to_sad((struct edid *) edid->raw_edid, &sads);
	if (sad_count <= 0)
		return result;

	edid_caps->audio_mode_count = min(sad_count, DC_MAX_AUDIO_DESC_COUNT);
	for (i = 0; i < edid_caps->audio_mode_count; ++i) {
		struct cea_sad *sad = &sads[i];

		edid_caps->audio_modes[i].format_code = sad->format;
		edid_caps->audio_modes[i].channel_count = sad->channels + 1;
		edid_caps->audio_modes[i].sample_rate = sad->freq;
		edid_caps->audio_modes[i].sample_size = sad->byte2;
	}

	sadb_count = drm_edid_to_speaker_allocation((struct edid *) edid->raw_edid, &sadb);

	if (sadb_count < 0) {
		DRM_ERROR("Couldn't read Speaker Allocation Data Block: %d\n", sadb_count);
		sadb_count = 0;
	}

	if (sadb_count)
		edid_caps->speaker_flags = sadb[0];
	else
		edid_caps->speaker_flags = DEFAULT_SPEAKER_LOCATION;

	apply_edid_quirks(edid_buf, edid_caps);

	kfree(sads);
	kfree(sadb);

	return result;
}

static void
fill_dc_mst_payload_table_from_drm(struct dc_link *link,
				   bool enable,
				   struct drm_dp_mst_atomic_payload *target_payload,
				   struct dc_dp_mst_stream_allocation_table *table)
{
	struct dc_dp_mst_stream_allocation_table new_table = { 0 };
	struct dc_dp_mst_stream_allocation *sa;
	struct link_mst_stream_allocation_table copy_of_link_table =
										link->mst_stream_alloc_table;

	int i;
	int current_hw_table_stream_cnt = copy_of_link_table.stream_count;
	struct link_mst_stream_allocation *dc_alloc;

	/* TODO: refactor to set link->mst_stream_alloc_table directly if possible.*/
	if (enable) {
		dc_alloc =
		&copy_of_link_table.stream_allocations[current_hw_table_stream_cnt];
		dc_alloc->vcp_id = target_payload->vcpi;
		dc_alloc->slot_count = target_payload->time_slots;
	} else {
		for (i = 0; i < copy_of_link_table.stream_count; i++) {
			dc_alloc =
			&copy_of_link_table.stream_allocations[i];

			if (dc_alloc->vcp_id == target_payload->vcpi) {
				dc_alloc->vcp_id = 0;
				dc_alloc->slot_count = 0;
				break;
			}
		}
		ASSERT(i != copy_of_link_table.stream_count);
	}

	/* Fill payload info*/
	for (i = 0; i < MAX_CONTROLLER_NUM; i++) {
		dc_alloc =
			&copy_of_link_table.stream_allocations[i];
		if (dc_alloc->vcp_id > 0 && dc_alloc->slot_count > 0) {
			sa = &new_table.stream_allocations[new_table.stream_count];
			sa->slot_count = dc_alloc->slot_count;
			sa->vcp_id = dc_alloc->vcp_id;
			new_table.stream_count++;
		}
	}

	/* Overwrite the old table */
	*table = new_table;
}

void dm_helpers_dp_update_branch_info(
	struct dc_context *ctx,
	const struct dc_link *link)
{}

static void dm_helpers_construct_old_payload(
			struct dc_link *link,
			int pbn_per_slot,
			struct drm_dp_mst_atomic_payload *new_payload,
			struct drm_dp_mst_atomic_payload *old_payload)
{
	struct link_mst_stream_allocation_table current_link_table =
									link->mst_stream_alloc_table;
	struct link_mst_stream_allocation *dc_alloc;
	int i;

	*old_payload = *new_payload;

	/* Set correct time_slots/PBN of old payload.
	 * other fields (delete & dsc_enabled) in
	 * struct drm_dp_mst_atomic_payload are don't care fields
	 * while calling drm_dp_remove_payload()
	 */
	for (i = 0; i < current_link_table.stream_count; i++) {
		dc_alloc =
			&current_link_table.stream_allocations[i];

		if (dc_alloc->vcp_id == new_payload->vcpi) {
			old_payload->time_slots = dc_alloc->slot_count;
			old_payload->pbn = dc_alloc->slot_count * pbn_per_slot;
			break;
		}
	}

	/* make sure there is an old payload*/
	ASSERT(i != current_link_table.stream_count);

}

/*
 * Writes payload allocation table in immediate downstream device.
 */
bool dm_helpers_dp_mst_write_payload_allocation_table(
		struct dc_context *ctx,
		const struct dc_stream_state *stream,
		struct dc_dp_mst_stream_allocation_table *proposed_table,
		bool enable)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_atomic_payload *target_payload, *new_payload, old_payload;
	struct drm_dp_mst_topology_mgr *mst_mgr;

	aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;
	/* Accessing the connector state is required for vcpi_slots allocation
	 * and directly relies on behaviour in commit check
	 * that blocks before commit guaranteeing that the state
	 * is not gonna be swapped while still in use in commit tail
	 */

	if (!aconnector || !aconnector->mst_root)
		return false;

	mst_mgr = &aconnector->mst_root->mst_mgr;
	mst_state = to_drm_dp_mst_topology_state(mst_mgr->base.state);

	/* It's OK for this to fail */
	new_payload = drm_atomic_get_mst_payload_state(mst_state, aconnector->mst_output_port);

	if (enable) {
		target_payload = new_payload;

		drm_dp_add_payload_part1(mst_mgr, mst_state, new_payload);
	} else {
		/* construct old payload by VCPI*/
		dm_helpers_construct_old_payload(stream->link, mst_state->pbn_div,
						new_payload, &old_payload);
		target_payload = &old_payload;

		drm_dp_remove_payload(mst_mgr, mst_state, &old_payload, new_payload);
	}

	/* mst_mgr->->payloads are VC payload notify MST branch using DPCD or
	 * AUX message. The sequence is slot 1-63 allocated sequence for each
	 * stream. AMD ASIC stream slot allocation should follow the same
	 * sequence. copy DRM MST allocation to dc
	 */
	fill_dc_mst_payload_table_from_drm(stream->link, enable, target_payload, proposed_table);

	return true;
}

/*
 * poll pending down reply
 */
void dm_helpers_dp_mst_poll_pending_down_reply(
	struct dc_context *ctx,
	const struct dc_link *link)
{}

/*
 * Clear payload allocation table before enable MST DP link.
 */
void dm_helpers_dp_mst_clear_payload_allocation_table(
	struct dc_context *ctx,
	const struct dc_link *link)
{}

/*
 * Polls for ACT (allocation change trigger) handled and sends
 * ALLOCATE_PAYLOAD message.
 */
enum act_return_status dm_helpers_dp_mst_poll_for_allocation_change_trigger(
		struct dc_context *ctx,
		const struct dc_stream_state *stream)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_mst_topology_mgr *mst_mgr;
	int ret;

	aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;

	if (!aconnector || !aconnector->mst_root)
		return ACT_FAILED;

	mst_mgr = &aconnector->mst_root->mst_mgr;

	if (!mst_mgr->mst_state)
		return ACT_FAILED;

	ret = drm_dp_check_act_status(mst_mgr);

	if (ret)
		return ACT_FAILED;

	return ACT_SUCCESS;
}

bool dm_helpers_dp_mst_send_payload_allocation(
		struct dc_context *ctx,
		const struct dc_stream_state *stream,
		bool enable)
{
	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_mst_topology_state *mst_state;
	struct drm_dp_mst_topology_mgr *mst_mgr;
	struct drm_dp_mst_atomic_payload *payload;
	enum mst_progress_status set_flag = MST_ALLOCATE_NEW_PAYLOAD;
	enum mst_progress_status clr_flag = MST_CLEAR_ALLOCATED_PAYLOAD;
	int ret = 0;

	aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;

	if (!aconnector || !aconnector->mst_root)
		return false;

	mst_mgr = &aconnector->mst_root->mst_mgr;
	mst_state = to_drm_dp_mst_topology_state(mst_mgr->base.state);

	payload = drm_atomic_get_mst_payload_state(mst_state, aconnector->mst_output_port);

	if (!enable) {
		set_flag = MST_CLEAR_ALLOCATED_PAYLOAD;
		clr_flag = MST_ALLOCATE_NEW_PAYLOAD;
	}

	if (enable)
		ret = drm_dp_add_payload_part2(mst_mgr, mst_state->base.state, payload);

	if (ret) {
		amdgpu_dm_set_mst_status(&aconnector->mst_status,
			set_flag, false);
	} else {
		amdgpu_dm_set_mst_status(&aconnector->mst_status,
			set_flag, true);
		amdgpu_dm_set_mst_status(&aconnector->mst_status,
			clr_flag, false);
	}

	return true;
}

void dm_dtn_log_begin(struct dc_context *ctx,
	struct dc_log_buffer_ctx *log_ctx)
{
	static const char msg[] = "[dtn begin]\n";

	if (!log_ctx) {
		pr_info("%s", msg);
		return;
	}

	dm_dtn_log_append_v(ctx, log_ctx, "%s", msg);
}

__printf(3, 4)
void dm_dtn_log_append_v(struct dc_context *ctx,
	struct dc_log_buffer_ctx *log_ctx,
	const char *msg, ...)
{
	va_list args;
	size_t total;
	int n;

	if (!log_ctx) {
		/* No context, redirect to dmesg. */
		struct va_format vaf;

		vaf.fmt = msg;
		vaf.va = &args;

		va_start(args, msg);
		pr_info("%pV", &vaf);
		va_end(args);

		return;
	}

	/* Measure the output. */
	va_start(args, msg);
	n = vsnprintf(NULL, 0, msg, args);
	va_end(args);

	if (n <= 0)
		return;

	/* Reallocate the string buffer as needed. */
	total = log_ctx->pos + n + 1;

	if (total > log_ctx->size) {
		char *buf = kvcalloc(total, sizeof(char), GFP_KERNEL);

		if (buf) {
			memcpy(buf, log_ctx->buf, log_ctx->pos);
			kfree(log_ctx->buf);

			log_ctx->buf = buf;
			log_ctx->size = total;
		}
	}

	if (!log_ctx->buf)
		return;

	/* Write the formatted string to the log buffer. */
	va_start(args, msg);
	n = vscnprintf(
		log_ctx->buf + log_ctx->pos,
		log_ctx->size - log_ctx->pos,
		msg,
		args);
	va_end(args);

	if (n > 0)
		log_ctx->pos += n;
}

void dm_dtn_log_end(struct dc_context *ctx,
	struct dc_log_buffer_ctx *log_ctx)
{
	static const char msg[] = "[dtn end]\n";

	if (!log_ctx) {
		pr_info("%s", msg);
		return;
	}

	dm_dtn_log_append_v(ctx, log_ctx, "%s", msg);
}

bool dm_helpers_dp_mst_start_top_mgr(
		struct dc_context *ctx,
		const struct dc_link *link,
		bool boot)
{
	struct amdgpu_dm_connector *aconnector = link->priv;
	int ret;

	if (!aconnector) {
		DRM_ERROR("Failed to find connector for link!");
		return false;
	}

	if (boot) {
		DRM_INFO("DM_MST: Differing MST start on aconnector: %p [id: %d]\n",
					aconnector, aconnector->base.base.id);
		return true;
	}

	DRM_INFO("DM_MST: starting TM on aconnector: %p [id: %d]\n",
			aconnector, aconnector->base.base.id);

	ret = drm_dp_mst_topology_mgr_set_mst(&aconnector->mst_mgr, true);
	if (ret < 0) {
		DRM_ERROR("DM_MST: Failed to set the device into MST mode!");
		return false;
	}

	DRM_INFO("DM_MST: DP%x, %d-lane link detected\n", aconnector->mst_mgr.dpcd[0],
		aconnector->mst_mgr.dpcd[2] & DP_MAX_LANE_COUNT_MASK);

	return true;
}

bool dm_helpers_dp_mst_stop_top_mgr(
		struct dc_context *ctx,
		struct dc_link *link)
{
	struct amdgpu_dm_connector *aconnector = link->priv;

	if (!aconnector) {
		DRM_ERROR("Failed to find connector for link!");
		return false;
	}

	DRM_INFO("DM_MST: stopping TM on aconnector: %p [id: %d]\n",
			aconnector, aconnector->base.base.id);

	if (aconnector->mst_mgr.mst_state == true) {
		drm_dp_mst_topology_mgr_set_mst(&aconnector->mst_mgr, false);
		link->cur_link_settings.lane_count = 0;
	}

	return false;
}

bool dm_helpers_dp_read_dpcd(
		struct dc_context *ctx,
		const struct dc_link *link,
		uint32_t address,
		uint8_t *data,
		uint32_t size)
{

	struct amdgpu_dm_connector *aconnector = link->priv;

	if (!aconnector) {
		DC_LOG_DC("Failed to find connector for link!\n");
		return false;
	}

	return drm_dp_dpcd_read(&aconnector->dm_dp_aux.aux, address, data,
				size) == size;
}

bool dm_helpers_dp_write_dpcd(
		struct dc_context *ctx,
		const struct dc_link *link,
		uint32_t address,
		const uint8_t *data,
		uint32_t size)
{
	struct amdgpu_dm_connector *aconnector = link->priv;

	if (!aconnector) {
		DRM_ERROR("Failed to find connector for link!");
		return false;
	}

	return drm_dp_dpcd_write(&aconnector->dm_dp_aux.aux,
			address, (uint8_t *)data, size) > 0;
}

bool dm_helpers_submit_i2c(
		struct dc_context *ctx,
		const struct dc_link *link,
		struct i2c_command *cmd)
{
	struct amdgpu_dm_connector *aconnector = link->priv;
	struct i2c_msg *msgs;
	int i = 0;
	int num = cmd->number_of_payloads;
	bool result;

	if (!aconnector) {
		DRM_ERROR("Failed to find connector for link!");
		return false;
	}

	msgs = kcalloc(num, sizeof(struct i2c_msg), GFP_KERNEL);

	if (!msgs)
		return false;

	for (i = 0; i < num; i++) {
		msgs[i].flags = cmd->payloads[i].write ? 0 : I2C_M_RD;
		msgs[i].addr = cmd->payloads[i].address;
		msgs[i].len = cmd->payloads[i].length;
		msgs[i].buf = cmd->payloads[i].data;
	}

	result = i2c_transfer(&aconnector->i2c->base, msgs, num) == num;

	kfree(msgs);

	return result;
}

static bool execute_synaptics_rc_command(struct drm_dp_aux *aux,
		bool is_write_cmd,
		unsigned char cmd,
		unsigned int length,
		unsigned int offset,
		unsigned char *data)
{
	bool success = false;
	unsigned char rc_data[16] = {0};
	unsigned char rc_offset[4] = {0};
	unsigned char rc_length[2] = {0};
	unsigned char rc_cmd = 0;
	unsigned char rc_result = 0xFF;
	unsigned char i = 0;
	int ret;

	if (is_write_cmd) {
		// write rc data
		memmove(rc_data, data, length);
		ret = drm_dp_dpcd_write(aux, SYNAPTICS_RC_DATA, rc_data, sizeof(rc_data));
	}

	// write rc offset
	rc_offset[0] = (unsigned char) offset & 0xFF;
	rc_offset[1] = (unsigned char) (offset >> 8) & 0xFF;
	rc_offset[2] = (unsigned char) (offset >> 16) & 0xFF;
	rc_offset[3] = (unsigned char) (offset >> 24) & 0xFF;
	ret = drm_dp_dpcd_write(aux, SYNAPTICS_RC_OFFSET, rc_offset, sizeof(rc_offset));

	// write rc length
	rc_length[0] = (unsigned char) length & 0xFF;
	rc_length[1] = (unsigned char) (length >> 8) & 0xFF;
	ret = drm_dp_dpcd_write(aux, SYNAPTICS_RC_LENGTH, rc_length, sizeof(rc_length));

	// write rc cmd
	rc_cmd = cmd | 0x80;
	ret = drm_dp_dpcd_write(aux, SYNAPTICS_RC_COMMAND, &rc_cmd, sizeof(rc_cmd));

	if (ret < 0) {
		DRM_ERROR("%s: write cmd ..., err = %d\n",  __func__, ret);
		return false;
	}

	// poll until active is 0
	for (i = 0; i < 10; i++) {
		drm_dp_dpcd_read(aux, SYNAPTICS_RC_COMMAND, &rc_cmd, sizeof(rc_cmd));
		if (rc_cmd == cmd)
			// active is 0
			break;
		msleep(10);
	}

	// read rc result
	drm_dp_dpcd_read(aux, SYNAPTICS_RC_RESULT, &rc_result, sizeof(rc_result));
	success = (rc_result == 0);

	if (success && !is_write_cmd) {
		// read rc data
		drm_dp_dpcd_read(aux, SYNAPTICS_RC_DATA, data, length);
	}

	DC_LOG_DC("%s: success = %d\n", __func__, success);

	return success;
}

static void apply_synaptics_fifo_reset_wa(struct drm_dp_aux *aux)
{
	unsigned char data[16] = {0};

	DC_LOG_DC("Start %s\n", __func__);

	// Step 2
	data[0] = 'P';
	data[1] = 'R';
	data[2] = 'I';
	data[3] = 'U';
	data[4] = 'S';

	if (!execute_synaptics_rc_command(aux, true, 0x01, 5, 0, data))
		return;

	// Step 3 and 4
	if (!execute_synaptics_rc_command(aux, false, 0x31, 4, 0x220998, data))
		return;

	data[0] &= (~(1 << 1)); // set bit 1 to 0
	if (!execute_synaptics_rc_command(aux, true, 0x21, 4, 0x220998, data))
		return;

	if (!execute_synaptics_rc_command(aux, false, 0x31, 4, 0x220D98, data))
		return;

	data[0] &= (~(1 << 1)); // set bit 1 to 0
	if (!execute_synaptics_rc_command(aux, true, 0x21, 4, 0x220D98, data))
		return;

	if (!execute_synaptics_rc_command(aux, false, 0x31, 4, 0x221198, data))
		return;

	data[0] &= (~(1 << 1)); // set bit 1 to 0
	if (!execute_synaptics_rc_command(aux, true, 0x21, 4, 0x221198, data))
		return;

	// Step 3 and 5
	if (!execute_synaptics_rc_command(aux, false, 0x31, 4, 0x220998, data))
		return;

	data[0] |= (1 << 1); // set bit 1 to 1
	if (!execute_synaptics_rc_command(aux, true, 0x21, 4, 0x220998, data))
		return;

	if (!execute_synaptics_rc_command(aux, false, 0x31, 4, 0x220D98, data))
		return;

	data[0] |= (1 << 1); // set bit 1 to 1

	if (!execute_synaptics_rc_command(aux, false, 0x31, 4, 0x221198, data))
		return;

	data[0] |= (1 << 1); // set bit 1 to 1
	if (!execute_synaptics_rc_command(aux, true, 0x21, 4, 0x221198, data))
		return;

	// Step 6
	if (!execute_synaptics_rc_command(aux, true, 0x02, 0, 0, NULL))
		return;

	DC_LOG_DC("Done %s\n", __func__);
}

/* MST Dock */
static const uint8_t SYNAPTICS_DEVICE_ID[] = "SYNA";

static uint8_t write_dsc_enable_synaptics_non_virtual_dpcd_mst(
		struct drm_dp_aux *aux,
		const struct dc_stream_state *stream,
		bool enable)
{
	uint8_t ret = 0;

	DC_LOG_DC("Configure DSC to non-virtual dpcd synaptics\n");

	if (enable) {
		/* When DSC is enabled on previous boot and reboot with the hub,
		 * there is a chance that Synaptics hub gets stuck during reboot sequence.
		 * Applying a workaround to reset Synaptics SDP fifo before enabling the first stream
		 */
		if (!stream->link->link_status.link_active &&
			memcmp(stream->link->dpcd_caps.branch_dev_name,
				(int8_t *)SYNAPTICS_DEVICE_ID, 4) == 0)
			apply_synaptics_fifo_reset_wa(aux);

		ret = drm_dp_dpcd_write(aux, DP_DSC_ENABLE, &enable, 1);
		DRM_INFO("Send DSC enable to synaptics\n");

	} else {
		/* Synaptics hub not support virtual dpcd,
		 * external monitor occur garbage while disable DSC,
		 * Disable DSC only when entire link status turn to false,
		 */
		if (!stream->link->link_status.link_active) {
			ret = drm_dp_dpcd_write(aux, DP_DSC_ENABLE, &enable, 1);
			DRM_INFO("Send DSC disable to synaptics\n");
		}
	}

	return ret;
}

bool dm_helpers_dp_write_dsc_enable(
		struct dc_context *ctx,
		const struct dc_stream_state *stream,
		bool enable)
{
	static const uint8_t DSC_DISABLE;
	static const uint8_t DSC_DECODING = 0x01;
	static const uint8_t DSC_PASSTHROUGH = 0x02;

	struct amdgpu_dm_connector *aconnector;
	struct drm_dp_mst_port *port;
	uint8_t enable_dsc = enable ? DSC_DECODING : DSC_DISABLE;
	uint8_t enable_passthrough = enable ? DSC_PASSTHROUGH : DSC_DISABLE;
	uint8_t ret = 0;

	if (!stream)
		return false;

	if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
		aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;

		if (!aconnector->dsc_aux)
			return false;

		// apply w/a to synaptics
		if (needs_dsc_aux_workaround(aconnector->dc_link) &&
		    (aconnector->mst_downstream_port_present.byte & 0x7) != 0x3)
			return write_dsc_enable_synaptics_non_virtual_dpcd_mst(
				aconnector->dsc_aux, stream, enable_dsc);

		port = aconnector->mst_output_port;

		if (enable) {
			if (port->passthrough_aux) {
				ret = drm_dp_dpcd_write(port->passthrough_aux,
							DP_DSC_ENABLE,
							&enable_passthrough, 1);
				DC_LOG_DC("Sent DSC pass-through enable to virtual dpcd port, ret = %u\n",
					  ret);
			}

			ret = drm_dp_dpcd_write(aconnector->dsc_aux,
						DP_DSC_ENABLE, &enable_dsc, 1);
			DC_LOG_DC("Sent DSC decoding enable to %s port, ret = %u\n",
				  (port->passthrough_aux) ? "remote RX" :
				  "virtual dpcd",
				  ret);
		} else {
			ret = drm_dp_dpcd_write(aconnector->dsc_aux,
						DP_DSC_ENABLE, &enable_dsc, 1);
			DC_LOG_DC("Sent DSC decoding disable to %s port, ret = %u\n",
				  (port->passthrough_aux) ? "remote RX" :
				  "virtual dpcd",
				  ret);

			if (port->passthrough_aux) {
				ret = drm_dp_dpcd_write(port->passthrough_aux,
							DP_DSC_ENABLE,
							&enable_passthrough, 1);
				DC_LOG_DC("Sent DSC pass-through disable to virtual dpcd port, ret = %u\n",
					  ret);
			}
		}
	}

	if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT || stream->signal == SIGNAL_TYPE_EDP) {
		if (stream->sink->link->dpcd_caps.dongle_type == DISPLAY_DONGLE_NONE) {
			ret = dm_helpers_dp_write_dpcd(ctx, stream->link, DP_DSC_ENABLE, &enable_dsc, 1);
			DC_LOG_DC("Send DSC %s to SST RX\n", enable_dsc ? "enable" : "disable");
		} else if (stream->sink->link->dpcd_caps.dongle_type == DISPLAY_DONGLE_DP_HDMI_CONVERTER) {
			ret = dm_helpers_dp_write_dpcd(ctx, stream->link, DP_DSC_ENABLE, &enable_dsc, 1);
			DC_LOG_DC("Send DSC %s to DP-HDMI PCON\n", enable_dsc ? "enable" : "disable");
		}
	}

	return ret;
}

bool dm_helpers_is_dp_sink_present(struct dc_link *link)
{
	bool dp_sink_present;
	struct amdgpu_dm_connector *aconnector = link->priv;

	if (!aconnector) {
		BUG_ON("Failed to find connector for link!");
		return true;
	}

	mutex_lock(&aconnector->dm_dp_aux.aux.hw_mutex);
	dp_sink_present = dc_link_is_dp_sink_present(link);
	mutex_unlock(&aconnector->dm_dp_aux.aux.hw_mutex);
	return dp_sink_present;
}

enum dc_edid_status dm_helpers_read_local_edid(
		struct dc_context *ctx,
		struct dc_link *link,
		struct dc_sink *sink)
{
	struct amdgpu_dm_connector *aconnector = link->priv;
	struct drm_connector *connector = &aconnector->base;
	struct i2c_adapter *ddc;
	int retry = 3;
	enum dc_edid_status edid_status;
	struct edid *edid;

	if (link->aux_mode)
		ddc = &aconnector->dm_dp_aux.aux.ddc;
	else
		ddc = &aconnector->i2c->base;

	/* some dongles read edid incorrectly the first time,
	 * do check sum and retry to make sure read correct edid.
	 */
	do {

		edid = drm_get_edid(&aconnector->base, ddc);

		/* DP Compliance Test 4.2.2.6 */
		if (link->aux_mode && connector->edid_corrupt)
			drm_dp_send_real_edid_checksum(&aconnector->dm_dp_aux.aux, connector->real_edid_checksum);

		if (!edid && connector->edid_corrupt) {
			connector->edid_corrupt = false;
			return EDID_BAD_CHECKSUM;
		}

		if (!edid)
			return EDID_NO_RESPONSE;

		sink->dc_edid.length = EDID_LENGTH * (edid->extensions + 1);
		memmove(sink->dc_edid.raw_edid, (uint8_t *)edid, sink->dc_edid.length);

		/* We don't need the original edid anymore */
		kfree(edid);

		edid_status = dm_helpers_parse_edid_caps(
						link,
						&sink->dc_edid,
						&sink->edid_caps);

	} while (edid_status == EDID_BAD_CHECKSUM && --retry > 0);

	if (edid_status != EDID_OK)
		DRM_ERROR("EDID err: %d, on connector: %s",
				edid_status,
				aconnector->base.name);
	if (link->aux_mode) {
		union test_request test_request = {0};
		union test_response test_response = {0};

		dm_helpers_dp_read_dpcd(ctx,
					link,
					DP_TEST_REQUEST,
					&test_request.raw,
					sizeof(union test_request));

		if (!test_request.bits.EDID_READ)
			return edid_status;

		test_response.bits.EDID_CHECKSUM_WRITE = 1;

		dm_helpers_dp_write_dpcd(ctx,
					link,
					DP_TEST_EDID_CHECKSUM,
					&sink->dc_edid.raw_edid[sink->dc_edid.length-1],
					1);

		dm_helpers_dp_write_dpcd(ctx,
					link,
					DP_TEST_RESPONSE,
					&test_response.raw,
					sizeof(test_response));

	}

	return edid_status;
}
int dm_helper_dmub_aux_transfer_sync(
		struct dc_context *ctx,
		const struct dc_link *link,
		struct aux_payload *payload,
		enum aux_return_code_type *operation_result)
{
	return amdgpu_dm_process_dmub_aux_transfer_sync(ctx, link->link_index, payload,
			operation_result);
}

int dm_helpers_dmub_set_config_sync(struct dc_context *ctx,
		const struct dc_link *link,
		struct set_config_cmd_payload *payload,
		enum set_config_status *operation_result)
{
	return amdgpu_dm_process_dmub_set_config_sync(ctx, link->link_index, payload,
			operation_result);
}

void dm_set_dcn_clocks(struct dc_context *ctx, struct dc_clocks *clks)
{
	/* TODO: something */
}

void dm_helpers_smu_timeout(struct dc_context *ctx, unsigned int msg_id, unsigned int param, unsigned int timeout_us)
{
	// TODO:
	//amdgpu_device_gpu_recover(dc_context->driver-context, NULL);
}

void dm_helpers_init_panel_settings(
	struct dc_context *ctx,
	struct dc_panel_config *panel_config,
	struct dc_sink *sink)
{
	// Extra Panel Power Sequence
	panel_config->pps.extra_t3_ms = sink->edid_caps.panel_patch.extra_t3_ms;
	panel_config->pps.extra_t7_ms = sink->edid_caps.panel_patch.extra_t7_ms;
	panel_config->pps.extra_delay_backlight_off = sink->edid_caps.panel_patch.extra_delay_backlight_off;
	panel_config->pps.extra_post_t7_ms = 0;
	panel_config->pps.extra_pre_t11_ms = 0;
	panel_config->pps.extra_t12_ms = sink->edid_caps.panel_patch.extra_t12_ms;
	panel_config->pps.extra_post_OUI_ms = 0;
	// Feature DSC
	panel_config->dsc.disable_dsc_edp = false;
	panel_config->dsc.force_dsc_edp_policy = 0;
}

void dm_helpers_override_panel_settings(
	struct dc_context *ctx,
	struct dc_panel_config *panel_config)
{
	// Feature DSC
	if (amdgpu_dc_debug_mask & DC_DISABLE_DSC)
		panel_config->dsc.disable_dsc_edp = true;
}

void *dm_helpers_allocate_gpu_mem(
		struct dc_context *ctx,
		enum dc_gpu_mem_alloc_type type,
		size_t size,
		long long *addr)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct dal_allocation *da;
	u32 domain = (type == DC_MEM_ALLOC_TYPE_GART) ?
		AMDGPU_GEM_DOMAIN_GTT : AMDGPU_GEM_DOMAIN_VRAM;
	int ret;

	da = kzalloc(sizeof(struct dal_allocation), GFP_KERNEL);
	if (!da)
		return NULL;

	ret = amdgpu_bo_create_kernel(adev, size, PAGE_SIZE,
				      domain, &da->bo,
				      &da->gpu_addr, &da->cpu_ptr);

	*addr = da->gpu_addr;

	if (ret) {
		kfree(da);
		return NULL;
	}

	/* add da to list in dm */
	list_add(&da->list, &adev->dm.da_list);

	return da->cpu_ptr;
}

void dm_helpers_free_gpu_mem(
		struct dc_context *ctx,
		enum dc_gpu_mem_alloc_type type,
		void *pvMem)
{
	struct amdgpu_device *adev = ctx->driver_context;
	struct dal_allocation *da;

	/* walk the da list in DM */
	list_for_each_entry(da, &adev->dm.da_list, list) {
		if (pvMem == da->cpu_ptr) {
			amdgpu_bo_free_kernel(&da->bo, &da->gpu_addr, &da->cpu_ptr);
			list_del(&da->list);
			kfree(da);
			break;
		}
	}
}

bool dm_helpers_dmub_outbox_interrupt_control(struct dc_context *ctx, bool enable)
{
	enum dc_irq_source irq_source;
	bool ret;

	irq_source = DC_IRQ_SOURCE_DMCUB_OUTBOX;

	ret = dc_interrupt_set(ctx->dc, irq_source, enable);

	DRM_DEBUG_DRIVER("Dmub trace irq %sabling: r=%d\n",
			 enable ? "en" : "dis", ret);
	return ret;
}

void dm_helpers_mst_enable_stream_features(const struct dc_stream_state *stream)
{
	/* TODO: virtual DPCD */
	struct dc_link *link = stream->link;
	union down_spread_ctrl old_downspread;
	union down_spread_ctrl new_downspread;

	if (link->aux_access_disabled)
		return;

	if (!dm_helpers_dp_read_dpcd(link->ctx, link, DP_DOWNSPREAD_CTRL,
				     &old_downspread.raw,
				     sizeof(old_downspread)))
		return;

	new_downspread.raw = old_downspread.raw;
	new_downspread.bits.IGNORE_MSA_TIMING_PARAM =
		(stream->ignore_msa_timing_param) ? 1 : 0;

	if (new_downspread.raw != old_downspread.raw)
		dm_helpers_dp_write_dpcd(link->ctx, link, DP_DOWNSPREAD_CTRL,
					 &new_downspread.raw,
					 sizeof(new_downspread));
}

bool dm_helpers_dp_handle_test_pattern_request(
		struct dc_context *ctx,
		const struct dc_link *link,
		union link_test_pattern dpcd_test_pattern,
		union test_misc dpcd_test_params)
{
	enum dp_test_pattern test_pattern;
	enum dp_test_pattern_color_space test_pattern_color_space =
			DP_TEST_PATTERN_COLOR_SPACE_UNDEFINED;
	enum dc_color_depth requestColorDepth = COLOR_DEPTH_UNDEFINED;
	enum dc_pixel_encoding requestPixelEncoding = PIXEL_ENCODING_UNDEFINED;
	struct pipe_ctx *pipes = link->dc->current_state->res_ctx.pipe_ctx;
	struct pipe_ctx *pipe_ctx = NULL;
	struct amdgpu_dm_connector *aconnector = link->priv;
	int i;

	for (i = 0; i < MAX_PIPES; i++) {
		if (pipes[i].stream == NULL)
			continue;

		if (pipes[i].stream->link == link && !pipes[i].top_pipe &&
			!pipes[i].prev_odm_pipe) {
			pipe_ctx = &pipes[i];
			break;
		}
	}

	if (pipe_ctx == NULL)
		return false;

	switch (dpcd_test_pattern.bits.PATTERN) {
	case LINK_TEST_PATTERN_COLOR_RAMP:
		test_pattern = DP_TEST_PATTERN_COLOR_RAMP;
	break;
	case LINK_TEST_PATTERN_VERTICAL_BARS:
		test_pattern = DP_TEST_PATTERN_VERTICAL_BARS;
	break; /* black and white */
	case LINK_TEST_PATTERN_COLOR_SQUARES:
		test_pattern = (dpcd_test_params.bits.DYN_RANGE ==
				TEST_DYN_RANGE_VESA ?
				DP_TEST_PATTERN_COLOR_SQUARES :
				DP_TEST_PATTERN_COLOR_SQUARES_CEA);
	break;
	default:
		test_pattern = DP_TEST_PATTERN_VIDEO_MODE;
	break;
	}

	if (dpcd_test_params.bits.CLR_FORMAT == 0)
		test_pattern_color_space = DP_TEST_PATTERN_COLOR_SPACE_RGB;
	else
		test_pattern_color_space = dpcd_test_params.bits.YCBCR_COEFS ?
				DP_TEST_PATTERN_COLOR_SPACE_YCBCR709 :
				DP_TEST_PATTERN_COLOR_SPACE_YCBCR601;

	switch (dpcd_test_params.bits.BPC) {
	case 0: // 6 bits
		requestColorDepth = COLOR_DEPTH_666;
		break;
	case 1: // 8 bits
		requestColorDepth = COLOR_DEPTH_888;
		break;
	case 2: // 10 bits
		requestColorDepth = COLOR_DEPTH_101010;
		break;
	case 3: // 12 bits
		requestColorDepth = COLOR_DEPTH_121212;
		break;
	default:
		break;
	}

	switch (dpcd_test_params.bits.CLR_FORMAT) {
	case 0:
		requestPixelEncoding = PIXEL_ENCODING_RGB;
		break;
	case 1:
		requestPixelEncoding = PIXEL_ENCODING_YCBCR422;
		break;
	case 2:
		requestPixelEncoding = PIXEL_ENCODING_YCBCR444;
		break;
	default:
		requestPixelEncoding = PIXEL_ENCODING_RGB;
		break;
	}

	if ((requestColorDepth != COLOR_DEPTH_UNDEFINED
		&& pipe_ctx->stream->timing.display_color_depth != requestColorDepth)
		|| (requestPixelEncoding != PIXEL_ENCODING_UNDEFINED
		&& pipe_ctx->stream->timing.pixel_encoding != requestPixelEncoding)) {
		DC_LOG_DEBUG("%s: original bpc %d pix encoding %d, changing to %d  %d\n",
				__func__,
				pipe_ctx->stream->timing.display_color_depth,
				pipe_ctx->stream->timing.pixel_encoding,
				requestColorDepth,
				requestPixelEncoding);
		pipe_ctx->stream->timing.display_color_depth = requestColorDepth;
		pipe_ctx->stream->timing.pixel_encoding = requestPixelEncoding;

		dc_link_update_dsc_config(pipe_ctx);

		aconnector->timing_changed = true;
		/* store current timing */
		if (aconnector->timing_requested)
			*aconnector->timing_requested = pipe_ctx->stream->timing;
		else
			DC_LOG_ERROR("%s: timing storage failed\n", __func__);

	}

	dc_link_dp_set_test_pattern(
		(struct dc_link *) link,
		test_pattern,
		test_pattern_color_space,
		NULL,
		NULL,
		0);

	return false;
}

void dm_set_phyd32clk(struct dc_context *ctx, int freq_khz)
{
       // TODO
}

void dm_helpers_enable_periodic_detection(struct dc_context *ctx, bool enable)
{
	/* TODO: add periodic detection implementation */
}

void dm_helpers_dp_mst_update_branch_bandwidth(
		struct dc_context *ctx,
		struct dc_link *link)
{
	// TODO
}

static bool dm_is_freesync_pcon_whitelist(const uint32_t branch_dev_id)
{
	bool ret_val = false;

	switch (branch_dev_id) {
	case DP_BRANCH_DEVICE_ID_0060AD:
	case DP_BRANCH_DEVICE_ID_00E04C:
	case DP_BRANCH_DEVICE_ID_90CC24:
		ret_val = true;
		break;
	default:
		break;
	}

	return ret_val;
}

enum adaptive_sync_type dm_get_adaptive_sync_support_type(struct dc_link *link)
{
	struct dpcd_caps *dpcd_caps = &link->dpcd_caps;
	enum adaptive_sync_type as_type = ADAPTIVE_SYNC_TYPE_NONE;

	switch (dpcd_caps->dongle_type) {
	case DISPLAY_DONGLE_DP_HDMI_CONVERTER:
		if (dpcd_caps->adaptive_sync_caps.dp_adap_sync_caps.bits.ADAPTIVE_SYNC_SDP_SUPPORT == true &&
			dpcd_caps->allow_invalid_MSA_timing_param == true &&
			dm_is_freesync_pcon_whitelist(dpcd_caps->branch_dev_id))
			as_type = FREESYNC_TYPE_PCON_IN_WHITELIST;
		break;
	default:
		break;
	}

	return as_type;
}
