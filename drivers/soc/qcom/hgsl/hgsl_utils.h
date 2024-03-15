/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __HGSL_UTILS_H
#define __HGSL_UTILS_H

#include <linux/vmalloc.h>
#include <linux/mm.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/sched/signal.h>
#include <linux/stdarg.h>

enum {
	LOG_LEVEL_ERROR,
	LOG_LEVEL_WARN,
	LOG_LEVEL_INFO,
	LOG_LEVEL_DEBUG,
	LOG_LEVEL_NUM
};

#define LOGE(...) hgsl_log(LOG_LEVEL_ERROR, __func__, __LINE__, ##__VA_ARGS__)
#define LOGW(...) hgsl_log(LOG_LEVEL_WARN, __func__, __LINE__, ##__VA_ARGS__)
#define LOGI(...)
#define LOGD(...)

#define OS_UNUSED(param) ((void)param)

static inline void *hgsl_malloc(size_t size)
{
	if (size <= PAGE_SIZE)
		return kmalloc(size, GFP_KERNEL);

	return vmalloc(size);
}

static inline void *hgsl_zalloc(size_t size)
{
	if (size <= PAGE_SIZE)
		return kzalloc(size, GFP_KERNEL);

	return vzalloc(size);
}

static inline void hgsl_free(void *ptr)
{
	if (ptr != NULL) {
		if (is_vmalloc_addr(ptr))
			vfree(ptr);
		else
			kfree(ptr);
	}
}

static inline void hgsl_log(unsigned int level, const char * const fun,
	unsigned int line, const char *format, ...)
{
	va_list arglist;
	char buffer[512];
	const char *tag = NULL;
	unsigned int offset = 0;
	struct pid *pid = task_tgid(current);
	struct task_struct *task = pid_task(pid, PIDTYPE_PID);

	switch (level) {
	case LOG_LEVEL_DEBUG:
		tag = "DEBUG";
		break;
	case LOG_LEVEL_INFO:
		tag = "INFO";
		break;
	case LOG_LEVEL_WARN:
		tag = "WARNING";
		break;
	case LOG_LEVEL_ERROR:
		tag = "ERROR";
		break;
	default:
		tag = "UNKNOWN";
		break;
	}

	if (task)
		snprintf(buffer, sizeof(buffer), "HGSL [%s] [%s:%u] [%s:%u:%u]",
			tag, fun, line, task->comm, task_pid_nr(task), current->pid);
	else
		snprintf(buffer, sizeof(buffer), "HGSL [%s] [%s:%u]",
			tag, fun, line);

	offset = strlen(buffer);
	va_start(arglist, format);
	vsnprintf(buffer + offset, sizeof(buffer) - offset, format, arglist);
	va_end(arglist);

	pr_err("%s\n", buffer);
}

#endif  /* __HGSL_UTILS_H */
