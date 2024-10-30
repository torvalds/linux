/* SPDX-License-Identifier: MIT */
/* SPDX-FileCopyrightText: Copyright (c) 2024 NVIDIA CORPORATION & AFFILIATES. */

#ifndef __NVIF_LOG_H__
#define __NVIF_LOG_H__

#ifdef CONFIG_DEBUG_FS

/**
 * nvif_log - structure for tracking logging buffers
 * @entry: an entry in a list of struct nvif_logs
 * @shutdown: pointer to function to call to clean up
 *
 * Structure used to track logging buffers so that they can be cleaned up
 * when the module exits.
 *
 * The @shutdown function is called when the module exits. It should free all
 * backing resources, such as logging buffers.
 */
struct nvif_log {
	struct list_head entry;
	void (*shutdown)(struct nvif_log *log);
};

/**
 * nvif_logs - linked list of nvif_log objects
 */
struct nvif_logs {
	struct list_head head;
};

#define NVIF_LOGS_DECLARE(logs) \
	struct nvif_logs logs = { LIST_HEAD_INIT(logs.head) }

static inline void nvif_log_shutdown(struct nvif_logs *logs)
{
	if (!list_empty(&logs->head)) {
		struct nvif_log *log, *n;

		list_for_each_entry_safe(log, n, &logs->head, entry) {
			/* shutdown() should also delete the log entry */
			log->shutdown(log);
		}
	}
}

extern struct nvif_logs gsp_logs;

#endif

#endif
