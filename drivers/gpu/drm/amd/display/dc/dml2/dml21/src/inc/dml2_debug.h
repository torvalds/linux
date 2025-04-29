// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_DEBUG_H__
#define __DML2_DEBUG_H__

#ifndef DML2_ASSERT
#define DML2_ASSERT(condition) ((void)0)
#endif

/*
 * DML_LOG_FATAL - fatal errors for unrecoverable DML states until a restart.
 * DML_LOG_ERROR - unexpected but recoverable failures inside DML
 * DML_LOG_WARN - unexpected inputs or events to DML
 * DML_LOG_INFO - high level tracing of DML interfaces
 * DML_LOG_DEBUG - detailed tracing of DML internal components
 * DML_LOG_VERBOSE - detailed tracing of DML calculation procedure
 */
#if !defined(DML_LOG_LEVEL)
#if defined(_DEBUG) && defined(_DEBUG_PRINTS)
/* for backward compatibility with old macros */
#define DML_LOG_LEVEL 5
#else
#define DML_LOG_LEVEL 0
#endif
#endif

#define DML_LOG_FATAL(fmt, ...) dml2_log_internal(fmt, ## __VA_ARGS__)
#if DML_LOG_LEVEL >= 1
#define DML_LOG_ERROR(fmt, ...) dml2_log_internal(fmt, ## __VA_ARGS__)
#else
#define DML_LOG_ERROR(fmt, ...) ((void)0)
#endif
#if DML_LOG_LEVEL >= 2
#define DML_LOG_WARN(fmt, ...) dml2_log_internal(fmt, ## __VA_ARGS__)
#else
#define DML_LOG_WARN(fmt, ...) ((void)0)
#endif
#if DML_LOG_LEVEL >= 3
#define DML_LOG_INFO(fmt, ...) dml2_log_internal(fmt, ## __VA_ARGS__)
#else
#define DML_LOG_INFO(fmt, ...) ((void)0)
#endif
#if DML_LOG_LEVEL >= 4
#define DML_LOG_DEBUG(fmt, ...) dml2_log_internal(fmt, ## __VA_ARGS__)
#else
#define DML_LOG_DEBUG(fmt, ...) ((void)0)
#endif
#if DML_LOG_LEVEL >= 5
#define DML_LOG_VERBOSE(fmt, ...) dml2_log_internal(fmt, ## __VA_ARGS__)
#else
#define DML_LOG_VERBOSE(fmt, ...) ((void)0)
#endif

int dml2_log_internal(const char *format, ...);
int dml2_printf(const char *format, ...);

#endif
