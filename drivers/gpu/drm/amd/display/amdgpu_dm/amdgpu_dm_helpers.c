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

#include <drm/drm_probe_helper.h>
#include <drm/amdgpu_drm.h>
#include <drm/drm_edid.h>

#include "dm_services.h"
#include "amdgpu.h"
#include "dc.h"
#include "amdgpu_dm.h"
#include "amdgpu_dm_irq.h"
#include "amdgpu_dm_mst_types.h"

#include "dm_helpers.h"

struct monitor_patch_info {
	unsigned int manufacturer_id;
	unsigned int product_id;
	void (*patch_func)(struct dc_edid_caps *edid_caps, unsigned int param);
	unsigned int patch_param;
};
static void set_max_dsc_bpp_limit(struct dc_edid_caps *edid_caps, unsigned int param);

static const struct monitor_patch_info monitor_patch_table[] = {
{0x6D1E, 0x5BBF, set_max_dsc_bpp_limit, 15},
{0x6D1E, 0x5B9A, set_max_dsc_bpp_limit, 15},
};

static void set_max_dsc_bpp_limit(struct dc_edid_caps *edid_caps, unsigned int param)
{
	if (edid_caps)
		edid_caps->panel_patch.max_dsc_target_bpp_limit = param;
}

static int amdgpu_dm_patch_edid_caps(struct dc_edid_caps *edid_caps)
{
	int i, ret = 0;

	for (i = 0; i < ARRAY_SIZE(monitor_patch_table); i++)
		if ((edid_caps->manufacturer_id == monitor_patch_table[i].manufacturer_id)
			&&  (edid_caps->product_id == monitor_patch_table[i].product_id)) {
			monitor_patch_table[i].patch_func(edid_caps, monitor_patch_table[i].patch_param);
			ret++;
		}

	return ret;
}

/* dm_helpers_parse_edid_caps
 *
 * Parse edid caps
 *
 * @edid:	[in] pointer to edid
 *  edid_caps:	[in] pointer to edid caps
 * @return
 *	void
 * */
enum dc_edid_status dm_helpers_parse_edid_caps(
		struct dc_context *ctx,
		const struct dc_edid *edid,
		struct dc_edid_caps *edid_caps)
{
	struct edid *edid_buf = (struct edid *) edid->raw_edid;
	struct cea_sad *sads;
	int sad_count = -1;
	int sadb_count = -1;
	int i = 0;
	int j = 0;
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

	/* One of the four detailed_timings stores the monitor name. It's
	 * stored in an array of length 13. */
	for (i = 0; i < 4; i++) {
		if (edid_buf->detailed_timings[i].data.other_data.type == 0xfc) {
			while (j < 13 && edid_buf->detailed_timings[i].data.other_data.data.str.str[j]) {
				if (edid_buf->detailed_timings[i].data.other_data.data.str.str[j] == '\n')
					break;

				edid_caps->display_name[j] =
					edid_buf->detailed_timings[i].data.other_data.data.str.str[j];
				j++;
			}
		}
	}

	edid_caps->edid_hdmi = drm_detect_hdmi_monitor(
			(struct edid *) edid->raw_edid);

	sad_count = drm_edid_to_sad((struct edid *) edid->raw_edid, &sads);
	if (sad_count <= 0)
		return result;

	edid_caps->audio_mode_count = sad_count < DC_MAX_AUDIO_DESC_COUNT ? sad_count : DC_MAX_AUDIO_DESC_COUNT;
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

	kfree(sads);
	kfree(sadb);

	amdgpu_dm_patch_edid_caps(edid_caps);

	return result;
}

static void get_payload_table(
		struct amdgpu_dm_connector *aconnector,
		struct dp_mst_stream_allocation_table *proposed_table)
{
	int i;
	struct drm_dp_mst_topology_mgr *mst_mgr =
			&aconnector->mst_port->mst_mgr;

	mutex_lock(&mst_mgr->payload_lock);

	proposed_table->stream_count = 0;

	/* number of active streams */
	for (i = 0; i < mst_mgr->max_payloads; i++) {
		if (mst_mgr->payloads[i].num_slots == 0)
			break; /* end of vcp_id table */

		ASSERT(mst_mgr->payloads[i].payload_state !=
				DP_PAYLOAD_DELETE_LOCAL);

		if (mst_mgr->payloads[i].payload_state == DP_PAYLOAD_LOCAL ||
			mst_mgr->payloads[i].payload_state ==
					DP_PAYLOAD_REMOTE) {

			struct dp_mst_stream_allocation *sa =
					&proposed_table->stream_allocations[
						proposed_table->stream_count];

			sa->slot_count = mst_mgr->payloads[i].num_slots;
			sa->vcp_id = mst_mgr->proposed_vcpis[i]->vcpi;
			proposed_table->stream_count++;
		}
	}

	mutex_unlock(&mst_mgr->payload_lock);
}

void dm_helpers_dp_update_branch_info(
	struct dc_context *ctx,
	const struct dc_link *link)
{}

/*
 * Writes payload allocation table in immediate downstream device.
 */
bool dm_helpers_dp_mst_write_payload_allocation_table(
		struct dc_context *ctx,
		const struct dc_stream_state *stream,
		struct dp_mst_stream_allocation_table *proposed_table,
		bool enable)
{
	struct amdgpu_dm_connector *aconnector;
	struct dm_connector_state *dm_conn_state;
	struct drm_dp_mst_topology_mgr *mst_mgr;
	struct drm_dp_mst_port *mst_port;
	bool ret;

	aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;
	/* Accessing the connector state is required for vcpi_slots allocation
	 * and directly relies on behaviour in commit check
	 * that blocks before commit guaranteeing that the state
	 * is not gonna be swapped while still in use in commit tail */

	if (!aconnector || !aconnector->mst_port)
		return false;

	dm_conn_state = to_dm_connector_state(aconnector->base.state);

	mst_mgr = &aconnector->mst_port->mst_mgr;

	if (!mst_mgr->mst_state)
		return false;

	mst_port = aconnector->port;

	if (enable) {

		ret = drm_dp_mst_allocate_vcpi(mst_mgr, mst_port,
					       dm_conn_state->pbn,
					       dm_conn_state->vcpi_slots);
		if (!ret)
			return false;

	} else {
		drm_dp_mst_reset_vcpi_slots(mst_mgr, mst_port);
	}

	/* It's OK for this to fail */
	drm_dp_update_payload_part1(mst_mgr);

	/* mst_mgr->->payloads are VC payload notify MST branch using DPCD or
	 * AUX message. The sequence is slot 1-63 allocated sequence for each
	 * stream. AMD ASIC stream slot allocation should follow the same
	 * sequence. copy DRM MST allocation to dc */

	get_payload_table(aconnector, proposed_table);

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

	if (!aconnector || !aconnector->mst_port)
		return ACT_FAILED;

	mst_mgr = &aconnector->mst_port->mst_mgr;

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
	struct drm_dp_mst_topology_mgr *mst_mgr;
	struct drm_dp_mst_port *mst_port;

	aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;

	if (!aconnector || !aconnector->mst_port)
		return false;

	mst_port = aconnector->port;

	mst_mgr = &aconnector->mst_port->mst_mgr;

	if (!mst_mgr->mst_state)
		return false;

	/* It's OK for this to fail */
	drm_dp_update_payload_part2(mst_mgr);

	if (!enable)
		drm_dp_mst_deallocate_vcpi(mst_mgr, mst_port);

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
		char *buf = (char *)kvcalloc(total, sizeof(char), GFP_KERNEL);

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

	return (drm_dp_mst_topology_mgr_set_mst(&aconnector->mst_mgr, true) == 0);
}

void dm_helpers_dp_mst_stop_top_mgr(
		struct dc_context *ctx,
		struct dc_link *link)
{
	struct amdgpu_dm_connector *aconnector = link->priv;
	uint8_t i;

	if (!aconnector) {
		DRM_ERROR("Failed to find connector for link!");
		return;
	}

	DRM_INFO("DM_MST: stopping TM on aconnector: %p [id: %d]\n",
			aconnector, aconnector->base.base.id);

	if (aconnector->mst_mgr.mst_state == true) {
		drm_dp_mst_topology_mgr_set_mst(&aconnector->mst_mgr, false);

		for (i = 0; i < MAX_SINKS_PER_LINK; i++) {
			if (link->remote_sinks[i] == NULL)
				continue;

			if (link->remote_sinks[i]->sink_signal ==
			    SIGNAL_TYPE_DISPLAY_PORT_MST) {
				dc_link_remove_remote_sink(link, link->remote_sinks[i]);

				if (aconnector->dc_sink) {
					dc_sink_release(aconnector->dc_sink);
					aconnector->dc_sink = NULL;
					aconnector->dc_link->cur_link_settings.lane_count = 0;
				}
			}
		}
	}
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

	return drm_dp_dpcd_read(&aconnector->dm_dp_aux.aux, address,
			data, size) > 0;
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
bool dm_helpers_dp_write_dsc_enable(
		struct dc_context *ctx,
		const struct dc_stream_state *stream,
		bool enable)
{
	uint8_t enable_dsc = enable ? 1 : 0;
	struct amdgpu_dm_connector *aconnector;
	uint8_t ret = 0;

	if (!stream)
		return false;

	if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT_MST) {
		aconnector = (struct amdgpu_dm_connector *)stream->dm_stream_context;

		if (!aconnector->dsc_aux)
			return false;

		ret = drm_dp_dpcd_write(aconnector->dsc_aux, DP_DSC_ENABLE, &enable_dsc, 1);
	}

	if (stream->signal == SIGNAL_TYPE_DISPLAY_PORT) {
		ret = dm_helpers_dp_write_dpcd(ctx, stream->link, DP_DSC_ENABLE, &enable_dsc, 1);
		DC_LOG_DC("Send DSC %s to sst display\n", enable_dsc ? "enable" : "disable");
	}

	return (ret > 0);
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

		/* connector->display_info will be parsed from EDID and saved
		 * into drm_connector->display_info from edid by call stack
		 * below:
		 * drm_parse_ycbcr420_deep_color_info
		 * drm_parse_hdmi_forum_vsdb
		 * drm_parse_cea_ext
		 * drm_add_display_info
		 * drm_connector_update_edid_property
		 *
		 * drm_connector->display_info will be used by amdgpu_dm funcs,
		 * like fill_stream_properties_from_drm_display_mode
		 */
		amdgpu_dm_update_connector_after_detect(aconnector);

		edid_status = dm_helpers_parse_edid_caps(
						ctx,
						&sink->dc_edid,
						&sink->edid_caps);

	} while (edid_status == EDID_BAD_CHECKSUM && --retry > 0);

	if (edid_status != EDID_OK)
		DRM_ERROR("EDID err: %d, on connector: %s",
				edid_status,
				aconnector->base.name);

	/* DP Compliance Test 4.2.2.3 */
	if (link->aux_mode)
		drm_dp_send_real_edid_checksum(&aconnector->dm_dp_aux.aux, sink->dc_edid.raw_edid[sink->dc_edid.length-1]);

	return edid_status;
}
int dm_helper_dmub_aux_transfer_sync(
		struct dc_context *ctx,
		const struct dc_link *link,
		struct aux_payload *payload,
		enum aux_return_code_type *operation_result)
{
	return amdgpu_dm_process_dmub_aux_transfer_sync(ctx, link->link_index, payload, operation_result);
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

#if defined(CONFIG_DRM_AMD_DC_DCN)
void dm_set_phyd32clk(struct dc_context *ctx, int freq_khz)
{
       // FPGA programming for this clock in diags framework that
       // needs to go through dm layer, therefore leave dummy interace here
}


void dm_helpers_enable_periodic_detection(struct dc_context *ctx, bool enable)
{
	/* TODO: add peridic detection implementation */
}
#endif