/*
 * Copyright 2019 Advanced Micro Devices, Inc.
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

#include "amdgpu_dm_hdcp.h"
#include "amdgpu.h"
#include "amdgpu_dm.h"
#include "dm_helpers.h"
#include <drm/drm_hdcp.h>

static bool
lp_write_i2c(void *handle, uint32_t address, const uint8_t *data, uint32_t size)
{

	struct dc_link *link = handle;
	struct i2c_payload i2c_payloads[] = {{true, address, size, (void *)data} };
	struct i2c_command cmd = {i2c_payloads, 1, I2C_COMMAND_ENGINE_HW, link->dc->caps.i2c_speed_in_khz};

	return dm_helpers_submit_i2c(link->ctx, link, &cmd);
}

static bool
lp_read_i2c(void *handle, uint32_t address, uint8_t offset, uint8_t *data, uint32_t size)
{
	struct dc_link *link = handle;

	struct i2c_payload i2c_payloads[] = {{true, address, 1, &offset}, {false, address, size, data} };
	struct i2c_command cmd = {i2c_payloads, 2, I2C_COMMAND_ENGINE_HW, link->dc->caps.i2c_speed_in_khz};

	return dm_helpers_submit_i2c(link->ctx, link, &cmd);
}

static bool
lp_write_dpcd(void *handle, uint32_t address, const uint8_t *data, uint32_t size)
{
	struct dc_link *link = handle;

	return dm_helpers_dp_write_dpcd(link->ctx, link, address, data, size);
}

static bool
lp_read_dpcd(void *handle, uint32_t address, uint8_t *data, uint32_t size)
{
	struct dc_link *link = handle;

	return dm_helpers_dp_read_dpcd(link->ctx, link, address, data, size);
}

static void process_output(struct hdcp_workqueue *hdcp_work)
{
	struct mod_hdcp_output output = hdcp_work->output;

	if (output.callback_stop)
		cancel_delayed_work(&hdcp_work->callback_dwork);

	if (output.callback_needed)
		schedule_delayed_work(&hdcp_work->callback_dwork,
				      msecs_to_jiffies(output.callback_delay));

	if (output.watchdog_timer_stop)
		cancel_delayed_work(&hdcp_work->watchdog_timer_dwork);

	if (output.watchdog_timer_needed)
		schedule_delayed_work(&hdcp_work->watchdog_timer_dwork,
				      msecs_to_jiffies(output.watchdog_timer_delay));

	schedule_delayed_work(&hdcp_work->property_validate_dwork, msecs_to_jiffies(0));
}

void hdcp_update_display(struct hdcp_workqueue *hdcp_work,
			 unsigned int link_index,
			 struct amdgpu_dm_connector *aconnector,
			 uint8_t content_type,
			 bool enable_encryption)
{
	struct hdcp_workqueue *hdcp_w = &hdcp_work[link_index];
	struct mod_hdcp_display *display = &hdcp_work[link_index].display;
	struct mod_hdcp_link *link = &hdcp_work[link_index].link;
	struct mod_hdcp_display_query query;

	mutex_lock(&hdcp_w->mutex);
	hdcp_w->aconnector = aconnector;

	query.display = NULL;
	mod_hdcp_query_display(&hdcp_w->hdcp, aconnector->base.index, &query);

	if (query.display != NULL) {
		memcpy(display, query.display, sizeof(struct mod_hdcp_display));
		mod_hdcp_remove_display(&hdcp_w->hdcp, aconnector->base.index, &hdcp_w->output);

		hdcp_w->link.adjust.hdcp2.force_type = MOD_HDCP_FORCE_TYPE_0;

		if (enable_encryption) {
			display->adjust.disable = 0;
			if (content_type == DRM_MODE_HDCP_CONTENT_TYPE0)
				hdcp_w->link.adjust.hdcp2.force_type = MOD_HDCP_FORCE_TYPE_0;
			else if (content_type == DRM_MODE_HDCP_CONTENT_TYPE1)
				hdcp_w->link.adjust.hdcp2.force_type = MOD_HDCP_FORCE_TYPE_1;

			schedule_delayed_work(&hdcp_w->property_validate_dwork,
					      msecs_to_jiffies(DRM_HDCP_CHECK_PERIOD_MS));
		} else {
			display->adjust.disable = 1;
			hdcp_w->encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF;
			cancel_delayed_work(&hdcp_w->property_validate_dwork);
		}

		display->state = MOD_HDCP_DISPLAY_ACTIVE;
	}

	mod_hdcp_add_display(&hdcp_w->hdcp, link, display, &hdcp_w->output);

	process_output(hdcp_w);
	mutex_unlock(&hdcp_w->mutex);
}

void hdcp_reset_display(struct hdcp_workqueue *hdcp_work, unsigned int link_index)
{
	struct hdcp_workqueue *hdcp_w = &hdcp_work[link_index];

	mutex_lock(&hdcp_w->mutex);

	mod_hdcp_reset_connection(&hdcp_w->hdcp,  &hdcp_w->output);

	cancel_delayed_work(&hdcp_w->property_validate_dwork);
	hdcp_w->encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF;

	process_output(hdcp_w);

	mutex_unlock(&hdcp_w->mutex);
}

void hdcp_handle_cpirq(struct hdcp_workqueue *hdcp_work, unsigned int link_index)
{
	struct hdcp_workqueue *hdcp_w = &hdcp_work[link_index];

	schedule_work(&hdcp_w->cpirq_work);
}




static void event_callback(struct work_struct *work)
{
	struct hdcp_workqueue *hdcp_work;

	hdcp_work = container_of(to_delayed_work(work), struct hdcp_workqueue,
				      callback_dwork);

	mutex_lock(&hdcp_work->mutex);

	cancel_delayed_work(&hdcp_work->watchdog_timer_dwork);

	mod_hdcp_process_event(&hdcp_work->hdcp, MOD_HDCP_EVENT_CALLBACK,
			       &hdcp_work->output);

	process_output(hdcp_work);

	mutex_unlock(&hdcp_work->mutex);


}
static void event_property_update(struct work_struct *work)
{

	struct hdcp_workqueue *hdcp_work = container_of(work, struct hdcp_workqueue, property_update_work);
	struct amdgpu_dm_connector *aconnector = hdcp_work->aconnector;
	struct drm_device *dev = hdcp_work->aconnector->base.dev;
	long ret;

	drm_modeset_lock(&dev->mode_config.connection_mutex, NULL);
	mutex_lock(&hdcp_work->mutex);


	if (aconnector->base.state->commit) {
		ret = wait_for_completion_interruptible_timeout(&aconnector->base.state->commit->hw_done, 10 * HZ);

		if (ret == 0) {
			DRM_ERROR("HDCP state unknown! Setting it to DESIRED");
			hdcp_work->encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF;
		}
	}

	if (hdcp_work->encryption_status != MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF) {
		if (aconnector->base.state->hdcp_content_type == DRM_MODE_HDCP_CONTENT_TYPE0 &&
		    hdcp_work->encryption_status <= MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE0_ON)
			drm_hdcp_update_content_protection(&aconnector->base, DRM_MODE_CONTENT_PROTECTION_ENABLED);
		else if (aconnector->base.state->hdcp_content_type == DRM_MODE_HDCP_CONTENT_TYPE1 &&
			 hdcp_work->encryption_status == MOD_HDCP_ENCRYPTION_STATUS_HDCP2_TYPE1_ON)
			drm_hdcp_update_content_protection(&aconnector->base, DRM_MODE_CONTENT_PROTECTION_ENABLED);
	} else {
		drm_hdcp_update_content_protection(&aconnector->base, DRM_MODE_CONTENT_PROTECTION_DESIRED);
	}


	mutex_unlock(&hdcp_work->mutex);
	drm_modeset_unlock(&dev->mode_config.connection_mutex);
}

static void event_property_validate(struct work_struct *work)
{
	struct hdcp_workqueue *hdcp_work =
		container_of(to_delayed_work(work), struct hdcp_workqueue, property_validate_dwork);
	struct mod_hdcp_display_query query;
	struct amdgpu_dm_connector *aconnector = hdcp_work->aconnector;

	if (!aconnector)
		return;

	mutex_lock(&hdcp_work->mutex);

	query.encryption_status = MOD_HDCP_ENCRYPTION_STATUS_HDCP_OFF;
	mod_hdcp_query_display(&hdcp_work->hdcp, aconnector->base.index, &query);

	if (query.encryption_status != hdcp_work->encryption_status) {
		hdcp_work->encryption_status = query.encryption_status;
		schedule_work(&hdcp_work->property_update_work);
	}

	mutex_unlock(&hdcp_work->mutex);
}

static void event_watchdog_timer(struct work_struct *work)
{
	struct hdcp_workqueue *hdcp_work;

	hdcp_work = container_of(to_delayed_work(work),
				      struct hdcp_workqueue,
				      watchdog_timer_dwork);

	mutex_lock(&hdcp_work->mutex);

	mod_hdcp_process_event(&hdcp_work->hdcp,
			       MOD_HDCP_EVENT_WATCHDOG_TIMEOUT,
			       &hdcp_work->output);

	process_output(hdcp_work);

	mutex_unlock(&hdcp_work->mutex);

}

static void event_cpirq(struct work_struct *work)
{
	struct hdcp_workqueue *hdcp_work;

	hdcp_work = container_of(work, struct hdcp_workqueue, cpirq_work);

	mutex_lock(&hdcp_work->mutex);

	mod_hdcp_process_event(&hdcp_work->hdcp, MOD_HDCP_EVENT_CPIRQ, &hdcp_work->output);

	process_output(hdcp_work);

	mutex_unlock(&hdcp_work->mutex);

}


void hdcp_destroy(struct hdcp_workqueue *hdcp_work)
{
	int i = 0;

	for (i = 0; i < hdcp_work->max_link; i++) {
		cancel_delayed_work_sync(&hdcp_work[i].callback_dwork);
		cancel_delayed_work_sync(&hdcp_work[i].watchdog_timer_dwork);
	}

	kfree(hdcp_work);

}

static void update_config(void *handle, struct cp_psp_stream_config *config)
{
	struct hdcp_workqueue *hdcp_work = handle;
	struct amdgpu_dm_connector *aconnector = config->dm_stream_ctx;
	int link_index = aconnector->dc_link->link_index;
	struct mod_hdcp_display *display = &hdcp_work[link_index].display;
	struct mod_hdcp_link *link = &hdcp_work[link_index].link;

	memset(display, 0, sizeof(*display));
	memset(link, 0, sizeof(*link));

	display->index = aconnector->base.index;
	display->state = MOD_HDCP_DISPLAY_ACTIVE;

	if (aconnector->dc_sink != NULL)
		link->mode = mod_hdcp_signal_type_to_operation_mode(aconnector->dc_sink->sink_signal);

	display->controller = CONTROLLER_ID_D0 + config->otg_inst;
	display->dig_fe = config->stream_enc_inst;
	link->dig_be = config->link_enc_inst;
	link->ddc_line = aconnector->dc_link->ddc_hw_inst + 1;
	link->dp.rev = aconnector->dc_link->dpcd_caps.dpcd_rev.raw;
	display->adjust.disable = 1;
	link->adjust.auth_delay = 2;

	hdcp_update_display(hdcp_work, link_index, aconnector, DRM_MODE_HDCP_CONTENT_TYPE0, false);
}

struct hdcp_workqueue *hdcp_create_workqueue(void *psp_context, struct cp_psp *cp_psp, struct dc *dc)
{

	int max_caps = dc->caps.max_links;
	struct hdcp_workqueue *hdcp_work = kzalloc(max_caps*sizeof(*hdcp_work), GFP_KERNEL);
	int i = 0;

	if (hdcp_work == NULL)
		goto fail_alloc_context;

	hdcp_work->max_link = max_caps;

	for (i = 0; i < max_caps; i++) {

		mutex_init(&hdcp_work[i].mutex);

		INIT_WORK(&hdcp_work[i].cpirq_work, event_cpirq);
		INIT_WORK(&hdcp_work[i].property_update_work, event_property_update);
		INIT_DELAYED_WORK(&hdcp_work[i].callback_dwork, event_callback);
		INIT_DELAYED_WORK(&hdcp_work[i].watchdog_timer_dwork, event_watchdog_timer);
		INIT_DELAYED_WORK(&hdcp_work[i].property_validate_dwork, event_property_validate);

		hdcp_work[i].hdcp.config.psp.handle =  psp_context;
		hdcp_work[i].hdcp.config.ddc.handle = dc_get_link_at_index(dc, i);
		hdcp_work[i].hdcp.config.ddc.funcs.write_i2c = lp_write_i2c;
		hdcp_work[i].hdcp.config.ddc.funcs.read_i2c = lp_read_i2c;
		hdcp_work[i].hdcp.config.ddc.funcs.write_dpcd = lp_write_dpcd;
		hdcp_work[i].hdcp.config.ddc.funcs.read_dpcd = lp_read_dpcd;
	}

	cp_psp->funcs.update_stream_config = update_config;
	cp_psp->handle = hdcp_work;

	return hdcp_work;

fail_alloc_context:
	kfree(hdcp_work);

	return NULL;



}



