// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#include <linux/ctype.h>
#include <linux/highmem.h>
#include <linux/fs.h>
#include <linux/slab.h>
#include <linux/moduleparam.h>

#include "vpu_boot_api.h"
#include "ivpu_drv.h"
#include "ivpu_fw.h"
#include "ivpu_fw_log.h"
#include "ivpu_gem.h"

#define IVPU_FW_LOG_LINE_LENGTH	256

unsigned int ivpu_fw_log_level = IVPU_FW_LOG_ERROR;
module_param_named(fw_log_level, ivpu_fw_log_level, uint, 0444);
MODULE_PARM_DESC(fw_log_level,
		 "NPU firmware default log level: debug=" __stringify(IVPU_FW_LOG_DEBUG)
		 " info=" __stringify(IVPU_FW_LOG_INFO)
		 " warn=" __stringify(IVPU_FW_LOG_WARN)
		 " error=" __stringify(IVPU_FW_LOG_ERROR)
		 " fatal=" __stringify(IVPU_FW_LOG_FATAL));

static int fw_log_from_bo(struct ivpu_device *vdev, struct ivpu_bo *bo, u32 *offset,
			  struct vpu_tracing_buffer_header **out_log)
{
	struct vpu_tracing_buffer_header *log;

	if ((*offset + sizeof(*log)) > ivpu_bo_size(bo))
		return -EINVAL;

	log = ivpu_bo_vaddr(bo) + *offset;

	if (log->vpu_canary_start != VPU_TRACING_BUFFER_CANARY)
		return -EINVAL;

	if (log->header_size < sizeof(*log) || log->header_size > 1024) {
		ivpu_dbg(vdev, FW_BOOT, "Invalid header size 0x%x\n", log->header_size);
		return -EINVAL;
	}
	if ((char *)log + log->size > (char *)ivpu_bo_vaddr(bo) + ivpu_bo_size(bo)) {
		ivpu_dbg(vdev, FW_BOOT, "Invalid log size 0x%x\n", log->size);
		return -EINVAL;
	}

	*out_log = log;
	*offset += log->size;

	ivpu_dbg(vdev, FW_BOOT,
		 "FW log name \"%s\", write offset 0x%x size 0x%x, wrap count %d, hdr version %d size %d format %d, alignment %d",
		 log->name, log->write_index, log->size, log->wrap_count, log->header_version,
		 log->header_size, log->format, log->alignment);

	return 0;
}

static void fw_log_print_lines(char *buffer, u32 size, struct drm_printer *p)
{
	char line[IVPU_FW_LOG_LINE_LENGTH];
	u32 index = 0;

	if (!size || !buffer)
		return;

	while (size--) {
		if (*buffer == '\n' || *buffer == 0) {
			line[index] = 0;
			if (index != 0)
				drm_printf(p, "%s\n", line);
			index = 0;
			buffer++;
			continue;
		}
		if (index == IVPU_FW_LOG_LINE_LENGTH - 1) {
			line[index] = 0;
			index = 0;
			drm_printf(p, "%s\n", line);
		}
		if (*buffer != '\r' && (isprint(*buffer) || iscntrl(*buffer)))
			line[index++] = *buffer;
		buffer++;
	}
	line[index] = 0;
	if (index != 0)
		drm_printf(p, "%s", line);
}

static void fw_log_print_buffer(struct vpu_tracing_buffer_header *log, const char *prefix,
				bool only_new_msgs, struct drm_printer *p)
{
	char *log_data = (void *)log + log->header_size;
	u32 data_size = log->size - log->header_size;
	u32 log_start = only_new_msgs ? READ_ONCE(log->read_index) : 0;
	u32 log_end = READ_ONCE(log->write_index);

	if (log->wrap_count == log->read_wrap_count) {
		if (log_end <= log_start) {
			drm_printf(p, "==== %s \"%s\" log empty ====\n", prefix, log->name);
			return;
		}
	} else if (log->wrap_count == log->read_wrap_count + 1) {
		if (log_end > log_start)
			log_start = log_end;
	} else {
		log_start = log_end;
	}

	drm_printf(p, "==== %s \"%s\" log start ====\n", prefix, log->name);
	if (log_end > log_start) {
		fw_log_print_lines(log_data + log_start, log_end - log_start, p);
	} else {
		fw_log_print_lines(log_data + log_start, data_size - log_start, p);
		fw_log_print_lines(log_data, log_end, p);
	}
	drm_printf(p, "\n\x1b[0m"); /* add new line and clear formatting */
	drm_printf(p, "==== %s \"%s\" log end   ====\n", prefix, log->name);
}

static void
fw_log_print_all_in_bo(struct ivpu_device *vdev, const char *name,
		       struct ivpu_bo *bo, bool only_new_msgs, struct drm_printer *p)
{
	struct vpu_tracing_buffer_header *log;
	u32 next = 0;

	while (fw_log_from_bo(vdev, bo, &next, &log) == 0)
		fw_log_print_buffer(log, name, only_new_msgs, p);
}

void ivpu_fw_log_print(struct ivpu_device *vdev, bool only_new_msgs, struct drm_printer *p)
{
	fw_log_print_all_in_bo(vdev, "NPU critical", vdev->fw->mem_log_crit, only_new_msgs, p);
	fw_log_print_all_in_bo(vdev, "NPU verbose", vdev->fw->mem_log_verb, only_new_msgs, p);
}

void ivpu_fw_log_mark_read(struct ivpu_device *vdev)
{
	struct vpu_tracing_buffer_header *log;
	u32 next;

	next = 0;
	while (fw_log_from_bo(vdev, vdev->fw->mem_log_crit, &next, &log) == 0) {
		log->read_index = READ_ONCE(log->write_index);
		log->read_wrap_count = READ_ONCE(log->wrap_count);
	}

	next = 0;
	while (fw_log_from_bo(vdev, vdev->fw->mem_log_verb, &next, &log) == 0) {
		log->read_index = READ_ONCE(log->write_index);
		log->read_wrap_count = READ_ONCE(log->wrap_count);
	}
}

void ivpu_fw_log_reset(struct ivpu_device *vdev)
{
	struct vpu_tracing_buffer_header *log;
	u32 next;

	next = 0;
	while (fw_log_from_bo(vdev, vdev->fw->mem_log_crit, &next, &log) == 0) {
		log->read_index = 0;
		log->read_wrap_count = 0;
	}

	next = 0;
	while (fw_log_from_bo(vdev, vdev->fw->mem_log_verb, &next, &log) == 0) {
		log->read_index = 0;
		log->read_wrap_count = 0;
	}
}
