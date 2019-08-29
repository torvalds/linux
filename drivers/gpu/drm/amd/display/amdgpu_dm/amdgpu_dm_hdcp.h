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

#ifndef AMDGPU_DM_AMDGPU_DM_HDCP_H_
#define AMDGPU_DM_AMDGPU_DM_HDCP_H_

#include "mod_hdcp.h"
#include "hdcp.h"
#include "dc.h"
#include "dm_cp_psp.h"

struct mod_hdcp;
struct mod_hdcp_link;
struct mod_hdcp_display;
struct cp_psp;

struct hdcp_workqueue {
	struct work_struct cpirq_work;
	struct work_struct property_update_work;
	struct delayed_work callback_dwork;
	struct delayed_work watchdog_timer_dwork;
	struct delayed_work property_validate_dwork;
	struct amdgpu_dm_connector *aconnector;
	struct mutex mutex;

	struct mod_hdcp hdcp;
	struct mod_hdcp_output output;
	struct mod_hdcp_display display;
	struct mod_hdcp_link link;

	enum mod_hdcp_encryption_status encryption_status;
	uint8_t max_link;
};

void hdcp_update_display(struct hdcp_workqueue *hdcp_work,
			 unsigned int link_index,
			 struct amdgpu_dm_connector *aconnector,
			 uint8_t content_type,
			 bool enable_encryption);

void hdcp_reset_display(struct hdcp_workqueue *work, unsigned int link_index);
void hdcp_handle_cpirq(struct hdcp_workqueue *work, unsigned int link_index);
void hdcp_destroy(struct hdcp_workqueue *work);

struct hdcp_workqueue *hdcp_create_workqueue(void *psp_context, struct cp_psp *cp_psp, struct dc *dc);

#endif /* AMDGPU_DM_AMDGPU_DM_HDCP_H_ */
