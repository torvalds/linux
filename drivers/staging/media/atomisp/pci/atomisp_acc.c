// SPDX-License-Identifier: GPL-2.0
/*
 * Support for Clovertrail PNW Camera Imaging ISP subsystem.
 *
 * Copyright (c) 2012 Intel Corporation. All Rights Reserved.
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License version
 * 2 as published by the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 *
 */

/*
 * This file implements loadable acceleration firmware API,
 * including ioctls to map and unmap acceleration parameters and buffers.
 */

#include <linux/init.h>
#include <media/v4l2-event.h>

#include "hmm.h"

#include "atomisp_acc.h"
#include "atomisp_internal.h"
#include "atomisp_compat.h"
#include "atomisp_cmd.h"

#include "ia_css.h"

static const struct {
	unsigned int flag;
	enum ia_css_pipe_id pipe_id;
} acc_flag_to_pipe[] = {
	{ ATOMISP_ACC_FW_LOAD_FL_PREVIEW, IA_CSS_PIPE_ID_PREVIEW },
	{ ATOMISP_ACC_FW_LOAD_FL_COPY, IA_CSS_PIPE_ID_COPY },
	{ ATOMISP_ACC_FW_LOAD_FL_VIDEO, IA_CSS_PIPE_ID_VIDEO },
	{ ATOMISP_ACC_FW_LOAD_FL_CAPTURE, IA_CSS_PIPE_ID_CAPTURE },
	{ ATOMISP_ACC_FW_LOAD_FL_ACC, IA_CSS_PIPE_ID_ACC }
};

static void acc_free_fw(struct atomisp_acc_fw *acc_fw)
{
	vfree(acc_fw->fw);
	kfree(acc_fw);
}

static int acc_stop_acceleration(struct atomisp_sub_device *asd)
{
	int ret;

	ret = atomisp_css_stop_acc_pipe(asd);
	atomisp_css_destroy_acc_pipe(asd);

	return ret;
}

void atomisp_acc_cleanup(struct atomisp_device *isp)
{
	int i;

	for (i = 0; i < isp->num_of_streams; i++)
		ida_destroy(&isp->asd[i].acc.ida);
}

void atomisp_acc_release(struct atomisp_sub_device *asd)
{
	struct atomisp_acc_fw *acc_fw, *ta;
	struct atomisp_map *atomisp_map, *tm;

	/* Stop acceleration if already running */
	if (asd->acc.pipeline)
		acc_stop_acceleration(asd);

	/* Unload all loaded acceleration binaries */
	list_for_each_entry_safe(acc_fw, ta, &asd->acc.fw, list) {
		list_del(&acc_fw->list);
		ida_free(&asd->acc.ida, acc_fw->handle);
		acc_free_fw(acc_fw);
	}

	/* Free all mapped memory blocks */
	list_for_each_entry_safe(atomisp_map, tm, &asd->acc.memory_maps, list) {
		list_del(&atomisp_map->list);
		hmm_free(atomisp_map->ptr);
		kfree(atomisp_map);
	}
}

void atomisp_acc_done(struct atomisp_sub_device *asd, unsigned int handle)
{
	struct v4l2_event event = { 0 };

	event.type = V4L2_EVENT_ATOMISP_ACC_COMPLETE;
	event.u.frame_sync.frame_sequence = atomic_read(&asd->sequence);
	event.id = handle;

	v4l2_event_queue(asd->subdev.devnode, &event);
}

static void atomisp_acc_unload_some_extensions(struct atomisp_sub_device *asd,
					      int i,
					      struct atomisp_acc_fw *acc_fw)
{
	while (--i >= 0) {
		if (acc_fw->flags & acc_flag_to_pipe[i].flag) {
			atomisp_css_unload_acc_extension(asd, acc_fw->fw,
							 acc_flag_to_pipe[i].pipe_id);
		}
	}
}

/*
 * Appends the loaded acceleration binary extensions to the
 * current ISP mode. Must be called just before sh_css_start().
 */
int atomisp_acc_load_extensions(struct atomisp_sub_device *asd)
{
	struct atomisp_acc_fw *acc_fw;
	bool ext_loaded = false;
	bool continuous = asd->continuous_mode->val &&
			  asd->run_mode->val == ATOMISP_RUN_MODE_PREVIEW;
	int ret = 0, i = -1;
	struct atomisp_device *isp = asd->isp;

	if (asd->acc.pipeline || asd->acc.extension_mode)
		return -EBUSY;

	/* Invalidate caches. FIXME: should flush only necessary buffers */
	wbinvd();

	list_for_each_entry(acc_fw, &asd->acc.fw, list) {
		if (acc_fw->type != ATOMISP_ACC_FW_LOAD_TYPE_OUTPUT &&
		    acc_fw->type != ATOMISP_ACC_FW_LOAD_TYPE_VIEWFINDER)
			continue;

		for (i = 0; i < ARRAY_SIZE(acc_flag_to_pipe); i++) {
			/*
			 * QoS (ACC pipe) acceleration stages are
			 * currently allowed only in continuous mode.
			 * Skip them for all other modes.
			 */
			if (!continuous &&
			    acc_flag_to_pipe[i].flag ==
			    ATOMISP_ACC_FW_LOAD_FL_ACC)
				continue;

			if (acc_fw->flags & acc_flag_to_pipe[i].flag) {
				ret = atomisp_css_load_acc_extension(asd,
								     acc_fw->fw,
								     acc_flag_to_pipe[i].pipe_id,
								     acc_fw->type);
				if (ret) {
					atomisp_acc_unload_some_extensions(asd, i, acc_fw);
					goto error;
				}

				ext_loaded = true;
			}
		}

		ret = atomisp_css_set_acc_parameters(acc_fw);
		if (ret < 0) {
			atomisp_acc_unload_some_extensions(asd, i, acc_fw);
			goto error;
		}
	}

	if (!ext_loaded)
		return ret;

	ret = atomisp_css_update_stream(asd);
	if (ret) {
		dev_err(isp->dev, "%s: update stream failed.\n", __func__);
		atomisp_acc_unload_extensions(asd);
		goto error;
	}

	asd->acc.extension_mode = true;
	return 0;

error:
	list_for_each_entry_continue_reverse(acc_fw, &asd->acc.fw, list) {
		if (acc_fw->type != ATOMISP_ACC_FW_LOAD_TYPE_OUTPUT &&
		    acc_fw->type != ATOMISP_ACC_FW_LOAD_TYPE_VIEWFINDER)
			continue;

		for (i = ARRAY_SIZE(acc_flag_to_pipe) - 1; i >= 0; i--) {
			if (!continuous &&
			    acc_flag_to_pipe[i].flag ==
			    ATOMISP_ACC_FW_LOAD_FL_ACC)
				continue;
			if (acc_fw->flags & acc_flag_to_pipe[i].flag) {
				atomisp_css_unload_acc_extension(asd,
								 acc_fw->fw,
								 acc_flag_to_pipe[i].pipe_id);
			}
		}
	}
	return ret;
}

void atomisp_acc_unload_extensions(struct atomisp_sub_device *asd)
{
	struct atomisp_acc_fw *acc_fw;
	int i;

	if (!asd->acc.extension_mode)
		return;

	list_for_each_entry_reverse(acc_fw, &asd->acc.fw, list) {
		if (acc_fw->type != ATOMISP_ACC_FW_LOAD_TYPE_OUTPUT &&
		    acc_fw->type != ATOMISP_ACC_FW_LOAD_TYPE_VIEWFINDER)
			continue;

		for (i = ARRAY_SIZE(acc_flag_to_pipe) - 1; i >= 0; i--) {
			if (acc_fw->flags & acc_flag_to_pipe[i].flag) {
				atomisp_css_unload_acc_extension(asd,
								 acc_fw->fw,
								 acc_flag_to_pipe[i].pipe_id);
			}
		}
	}

	asd->acc.extension_mode = false;
}
