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

}

void hdcp_add_display(struct hdcp_workqueue *hdcp_work, unsigned int link_index)
{
	struct hdcp_workqueue *hdcp_w = &hdcp_work[link_index];
	struct mod_hdcp_display *display = &hdcp_work[link_index].display;
	struct mod_hdcp_link *link = &hdcp_work[link_index].link;

	mutex_lock(&hdcp_w->mutex);

	mod_hdcp_add_display(&hdcp_w->hdcp, link, display, &hdcp_w->output);

	process_output(hdcp_w);

	mutex_unlock(&hdcp_w->mutex);

}

void hdcp_remove_display(struct hdcp_workqueue *hdcp_work, unsigned int link_index,  unsigned int display_index)
{
	struct hdcp_workqueue *hdcp_w = &hdcp_work[link_index];

	mutex_lock(&hdcp_w->mutex);

	mod_hdcp_remove_display(&hdcp_w->hdcp, display_index, &hdcp_w->output);

	process_output(hdcp_w);

	mutex_unlock(&hdcp_w->mutex);

}

void hdcp_reset_display(struct hdcp_workqueue *hdcp_work, unsigned int link_index)
{
	struct hdcp_workqueue *hdcp_w = &hdcp_work[link_index];

	mutex_lock(&hdcp_w->mutex);

	mod_hdcp_reset_connection(&hdcp_w->hdcp,  &hdcp_w->output);

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
	link->adjust.hdcp2.disable = 1;

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
		INIT_DELAYED_WORK(&hdcp_work[i].callback_dwork, event_callback);
		INIT_DELAYED_WORK(&hdcp_work[i].watchdog_timer_dwork, event_watchdog_timer);

		hdcp_work[i].hdcp.config.psp.handle =  psp_context;
		hdcp_work[i].hdcp.config.ddc.handle = dc_get_link_at_index(dc, i);

	}

	cp_psp->funcs.update_stream_config = update_config;
	cp_psp->handle = hdcp_work;

	return hdcp_work;

fail_alloc_context:
	kfree(hdcp_work);

	return NULL;



}



