// SPDX-License-Identifier: GPL-2.0-only OR MIT
/* Copyright (c) 2023 Imagination Technologies Ltd. */

#include "pvr_device.h"
#include "pvr_gem.h"
#include "pvr_rogue_fwif.h"
#include "pvr_fw_trace.h"

#include <drm/drm_file.h>

#include <linux/build_bug.h>
#include <linux/dcache.h>
#include <linux/sysfs.h>
#include <linux/types.h>

static void
tracebuf_ctrl_init(void *cpu_ptr, void *priv)
{
	struct rogue_fwif_tracebuf *tracebuf_ctrl = cpu_ptr;
	struct pvr_fw_trace *fw_trace = priv;
	u32 thread_nr;

	tracebuf_ctrl->tracebuf_size_in_dwords = ROGUE_FW_TRACE_BUF_DEFAULT_SIZE_IN_DWORDS;
	tracebuf_ctrl->tracebuf_flags = 0;

	if (fw_trace->group_mask)
		tracebuf_ctrl->log_type = fw_trace->group_mask | ROGUE_FWIF_LOG_TYPE_TRACE;
	else
		tracebuf_ctrl->log_type = ROGUE_FWIF_LOG_TYPE_NONE;

	for (thread_nr = 0; thread_nr < ARRAY_SIZE(fw_trace->buffers); thread_nr++) {
		struct rogue_fwif_tracebuf_space *tracebuf_space =
			&tracebuf_ctrl->tracebuf[thread_nr];
		struct pvr_fw_trace_buffer *trace_buffer = &fw_trace->buffers[thread_nr];

		pvr_fw_object_get_fw_addr(trace_buffer->buf_obj,
					  &tracebuf_space->trace_buffer_fw_addr);

		tracebuf_space->trace_buffer = trace_buffer->buf;
		tracebuf_space->trace_pointer = 0;
	}
}

int pvr_fw_trace_init(struct pvr_device *pvr_dev)
{
	struct pvr_fw_trace *fw_trace = &pvr_dev->fw_dev.fw_trace;
	struct drm_device *drm_dev = from_pvr_device(pvr_dev);
	u32 thread_nr;
	int err;

	for (thread_nr = 0; thread_nr < ARRAY_SIZE(fw_trace->buffers); thread_nr++) {
		struct pvr_fw_trace_buffer *trace_buffer = &fw_trace->buffers[thread_nr];

		trace_buffer->buf =
			pvr_fw_object_create_and_map(pvr_dev,
						     ROGUE_FW_TRACE_BUF_DEFAULT_SIZE_IN_DWORDS *
						     sizeof(*trace_buffer->buf),
						     PVR_BO_FW_FLAGS_DEVICE_UNCACHED |
						     PVR_BO_FW_NO_CLEAR_ON_RESET,
						     NULL, NULL, &trace_buffer->buf_obj);
		if (IS_ERR(trace_buffer->buf)) {
			drm_err(drm_dev, "Unable to allocate trace buffer\n");
			err = PTR_ERR(trace_buffer->buf);
			trace_buffer->buf = NULL;
			goto err_free_buf;
		}
	}

	/* TODO: Provide control of group mask. */
	fw_trace->group_mask = 0;

	fw_trace->tracebuf_ctrl =
		pvr_fw_object_create_and_map(pvr_dev,
					     sizeof(*fw_trace->tracebuf_ctrl),
					     PVR_BO_FW_FLAGS_DEVICE_UNCACHED |
					     PVR_BO_FW_NO_CLEAR_ON_RESET,
					     tracebuf_ctrl_init, fw_trace,
					     &fw_trace->tracebuf_ctrl_obj);
	if (IS_ERR(fw_trace->tracebuf_ctrl)) {
		drm_err(drm_dev, "Unable to allocate trace buffer control structure\n");
		err = PTR_ERR(fw_trace->tracebuf_ctrl);
		goto err_free_buf;
	}

	BUILD_BUG_ON(ARRAY_SIZE(fw_trace->tracebuf_ctrl->tracebuf) !=
		     ARRAY_SIZE(fw_trace->buffers));

	for (thread_nr = 0; thread_nr < ARRAY_SIZE(fw_trace->buffers); thread_nr++) {
		struct rogue_fwif_tracebuf_space *tracebuf_space =
			&fw_trace->tracebuf_ctrl->tracebuf[thread_nr];
		struct pvr_fw_trace_buffer *trace_buffer = &fw_trace->buffers[thread_nr];

		trace_buffer->tracebuf_space = tracebuf_space;
	}

	return 0;

err_free_buf:
	for (thread_nr = 0; thread_nr < ARRAY_SIZE(fw_trace->buffers); thread_nr++) {
		struct pvr_fw_trace_buffer *trace_buffer = &fw_trace->buffers[thread_nr];

		if (trace_buffer->buf)
			pvr_fw_object_unmap_and_destroy(trace_buffer->buf_obj);
	}

	return err;
}

void pvr_fw_trace_fini(struct pvr_device *pvr_dev)
{
	struct pvr_fw_trace *fw_trace = &pvr_dev->fw_dev.fw_trace;
	u32 thread_nr;

	for (thread_nr = 0; thread_nr < ARRAY_SIZE(fw_trace->buffers); thread_nr++) {
		struct pvr_fw_trace_buffer *trace_buffer = &fw_trace->buffers[thread_nr];

		pvr_fw_object_unmap_and_destroy(trace_buffer->buf_obj);
	}
	pvr_fw_object_unmap_and_destroy(fw_trace->tracebuf_ctrl_obj);
}
