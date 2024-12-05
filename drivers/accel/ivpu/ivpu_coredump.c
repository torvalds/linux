// SPDX-License-Identifier: GPL-2.0-only
/*
 * Copyright (C) 2020-2024 Intel Corporation
 */

#include <linux/devcoredump.h>
#include <linux/firmware.h>

#include "ivpu_coredump.h"
#include "ivpu_fw.h"
#include "ivpu_gem.h"
#include "vpu_boot_api.h"

#define CRASH_DUMP_HEADER "Intel NPU crash dump"
#define CRASH_DUMP_HEADERS_SIZE SZ_4K

void ivpu_dev_coredump(struct ivpu_device *vdev)
{
	struct drm_print_iterator pi = {};
	struct drm_printer p;
	size_t coredump_size;
	char *coredump;

	coredump_size = CRASH_DUMP_HEADERS_SIZE + FW_VERSION_HEADER_SIZE +
			ivpu_bo_size(vdev->fw->mem_log_crit) + ivpu_bo_size(vdev->fw->mem_log_verb);
	coredump = vmalloc(coredump_size);
	if (!coredump)
		return;

	pi.data = coredump;
	pi.remain = coredump_size;
	p = drm_coredump_printer(&pi);

	drm_printf(&p, "%s\n", CRASH_DUMP_HEADER);
	drm_printf(&p, "FW version: %s\n", vdev->fw->version);
	ivpu_fw_log_print(vdev, false, &p);

	dev_coredumpv(vdev->drm.dev, coredump, pi.offset, GFP_KERNEL);
}
