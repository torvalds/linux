// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_DEBUG_H__
#define __DML2_DEBUG_H__

#include "os_types.h"
#define DML_ASSERT(condition) ASSERT(condition)
#define DML_LOG_LEVEL_DEFAULT DML_LOG_LEVEL_WARN
#define DML_LOG_INTERNAL(fmt, ...) dm_output_to_console(fmt, ## __VA_ARGS__)

/* ASSERT with message output */
#define DML_ASSERT_MSG(condition, fmt, ...)								\
	do {												\
		if (!(condition)) {									\
			DML_LOG_ERROR("DML ASSERT hit in %s line %d\n", __func__, __LINE__);	\
			DML_LOG_ERROR(fmt, ## __VA_ARGS__);						\
			DML_ASSERT(condition);								\
		}											\
	} while (0)

/* fatal errors for unrecoverable DML states until a full reset */
#define DML_LOG_LEVEL_FATAL 0
/* unexpected but recoverable failures inside DML */
#define DML_LOG_LEVEL_ERROR 1
/* unexpected inputs or events to DML */
#define DML_LOG_LEVEL_WARN 2
/* high level tracing of DML interfaces */
#define DML_LOG_LEVEL_INFO 3
/* detailed tracing of DML internal components */
#define DML_LOG_LEVEL_DEBUG 4
/* detailed tracing of DML calculation procedure */
#define DML_LOG_LEVEL_VERBOSE 5

#ifndef DML_LOG_LEVEL
#define DML_LOG_LEVEL DML_LOG_LEVEL_DEFAULT
#endif /* #ifndef DML_LOG_LEVEL */

#define DML_LOG_FATAL(fmt, ...) DML_LOG_INTERNAL("[DML FATAL] " fmt, ## __VA_ARGS__)
#if DML_LOG_LEVEL >= DML_LOG_LEVEL_ERROR
#define DML_LOG_ERROR(fmt, ...) DML_LOG_INTERNAL("[DML ERROR] "fmt, ## __VA_ARGS__)
#else
#define DML_LOG_ERROR(fmt, ...) ((void)0)
#endif
#if DML_LOG_LEVEL >= DML_LOG_LEVEL_WARN
#define DML_LOG_WARN(fmt, ...) DML_LOG_INTERNAL("[DML WARN] "fmt, ## __VA_ARGS__)
#else
#define DML_LOG_WARN(fmt, ...) ((void)0)
#endif
#if DML_LOG_LEVEL >= DML_LOG_LEVEL_INFO
#define DML_LOG_INFO(fmt, ...) DML_LOG_INTERNAL("[DML INFO] "fmt, ## __VA_ARGS__)
#else
#define DML_LOG_INFO(fmt, ...) ((void)0)
#endif
#if DML_LOG_LEVEL >= DML_LOG_LEVEL_DEBUG
#define DML_LOG_DEBUG(fmt, ...) DML_LOG_INTERNAL("[DML DEBUG] "fmt, ## __VA_ARGS__)
#else
#define DML_LOG_DEBUG(fmt, ...) ((void)0)
#endif
#if DML_LOG_LEVEL >= DML_LOG_LEVEL_VERBOSE
#define DML_LOG_VERBOSE(fmt, ...) DML_LOG_INTERNAL("[DML VERBOSE] "fmt, ## __VA_ARGS__)
#else
#define DML_LOG_VERBOSE(fmt, ...) ((void)0)
#endif
#endif /* __DML2_DEBUG_H__ */
