/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2025 Intel Corporation
 */

#ifndef _ABI_GUC_SCHEDULER_ABI_H
#define _ABI_GUC_SCHEDULER_ABI_H

#include <linux/types.h>

/**
 * Generic defines required for registration with and submissions to the GuC
 * scheduler. Includes engine class/instance defines and context attributes
 * (id, priority, etc)
 */

/* Engine classes/instances */
#define GUC_RENDER_CLASS		0
#define GUC_VIDEO_CLASS			1
#define GUC_VIDEOENHANCE_CLASS		2
#define GUC_BLITTER_CLASS		3
#define GUC_COMPUTE_CLASS		4
#define GUC_GSC_OTHER_CLASS		5
#define GUC_LAST_ENGINE_CLASS		GUC_GSC_OTHER_CLASS
#define GUC_MAX_ENGINE_CLASSES		16
#define GUC_MAX_INSTANCES_PER_CLASS	32

/* context priority values */
#define GUC_CLIENT_PRIORITY_KMD_HIGH	0
#define GUC_CLIENT_PRIORITY_HIGH	1
#define GUC_CLIENT_PRIORITY_KMD_NORMAL	2
#define GUC_CLIENT_PRIORITY_NORMAL	3
#define GUC_CLIENT_PRIORITY_NUM		4

/* Context registration */
#define GUC_ID_MAX			65535
#define GUC_ID_UNKNOWN			0xffffffff

#define CONTEXT_REGISTRATION_FLAG_KMD	        BIT(0)
#define CONTEXT_REGISTRATION_FLAG_TYPE	        GENMASK(2, 1)
#define   GUC_CONTEXT_NORMAL			0
#define   GUC_CONTEXT_COMPRESSION_SAVE		1
#define   GUC_CONTEXT_COMPRESSION_RESTORE	2
#define   GUC_CONTEXT_COUNT			(GUC_CONTEXT_COMPRESSION_RESTORE + 1)

/* context enable/disable */
#define GUC_CONTEXT_DISABLE		0
#define GUC_CONTEXT_ENABLE		1

/* scheduler groups */
#define GUC_MAX_SCHED_GROUPS		8

struct guc_sched_group {
	u32 engines[GUC_MAX_ENGINE_CLASSES];
} __packed;

#endif
