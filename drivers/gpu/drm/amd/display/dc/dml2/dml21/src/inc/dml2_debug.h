// SPDX-License-Identifier: MIT
//
// Copyright 2024 Advanced Micro Devices, Inc.

#ifndef __DML2_DEBUG_H__
#define __DML2_DEBUG_H__

#include "os_types.h"
#define DML_ASSERT(condition) ASSERT(condition)
#define DML_LOG_LEVEL_DEFAULT DML_LOG_LEVEL_WARN
#define DML_LOG_INTERNAL(fmt, ...) dm_output_to_console(fmt, ## __VA_ARGS__)

/* private helper macros */
#define _BOOL_FORMAT(field) "%s", field ? "true" : "false"
#define _UINT_FORMAT(field) "%u", field
#define _INT_FORMAT(field) "%d", field
#define _DOUBLE_FORMAT(field) "%lf", field
#define _ELEMENT_FUNC "function"
#define _ELEMENT_COMP_IF "component_interface"
#define _ELEMENT_TOP_IF "top_interface"
#define _LOG_ENTRY(element) do {		\
	DML_LOG_INTERNAL("<"element" name=\"");	\
	DML_LOG_INTERNAL(__func__);		\
	DML_LOG_INTERNAL("\">\n");		\
} while (0)
#define _LOG_EXIT(element) DML_LOG_INTERNAL("</"element">\n")
#define _LOG_SCALAR(field, format) do {						\
	DML_LOG_INTERNAL(#field" = "format(field));				\
	DML_LOG_INTERNAL("\n");							\
} while (0)
#define _LOG_ARRAY(field, size, format) do {					\
	DML_LOG_INTERNAL(#field " = [");					\
	for (int _i = 0; _i < (int) size; _i++) {				\
		DML_LOG_INTERNAL(format(field[_i]));				\
		if (_i + 1 == (int) size)					\
			DML_LOG_INTERNAL("]\n");				\
		else								\
			DML_LOG_INTERNAL(", ");					\
}} while (0)
#define _LOG_2D_ARRAY(field, size0, size1, format) do {				\
	DML_LOG_INTERNAL(#field" = [");						\
	for (int _i = 0; _i < (int) size0; _i++) {				\
		DML_LOG_INTERNAL("\n\t[");					\
		for (int _j = 0; _j < (int) size1; _j++) {			\
			DML_LOG_INTERNAL(format(field[_i][_j]));		\
			if (_j + 1 == (int) size1)				\
				DML_LOG_INTERNAL("]");				\
			else							\
				DML_LOG_INTERNAL(", ");				\
		}								\
		if (_i + 1 == (int) size0)					\
			DML_LOG_INTERNAL("]\n");				\
		else								\
			DML_LOG_INTERNAL(", ");					\
	}									\
} while (0)
#define _LOG_3D_ARRAY(field, size0, size1, size2, format) do {			\
	DML_LOG_INTERNAL(#field" = [");						\
	for (int _i = 0; _i < (int) size0; _i++) {				\
		DML_LOG_INTERNAL("\n\t[");					\
		for (int _j = 0; _j < (int) size1; _j++) {			\
			DML_LOG_INTERNAL("[");					\
			for (int _k = 0; _k < (int) size2; _k++) {		\
				DML_LOG_INTERNAL(format(field[_i][_j][_k]));	\
				if (_k + 1 == (int) size2)			\
					DML_LOG_INTERNAL("]");			\
				else						\
					DML_LOG_INTERNAL(", ");			\
			}							\
			if (_j + 1 == (int) size1)				\
				DML_LOG_INTERNAL("]");				\
			else							\
				DML_LOG_INTERNAL(", ");				\
		}								\
		if (_i + 1 == (int) size0)					\
			DML_LOG_INTERNAL("]\n");				\
		else								\
			DML_LOG_INTERNAL(", ");					\
	}									\
} while (0)

/* fatal errors for unrecoverable DML states until a full reset */
#define DML_LOG_LEVEL_FATAL 0
/* unexpected but recoverable failures inside DML */
#define DML_LOG_LEVEL_ERROR 1
/* unexpected inputs or events to DML */
#define DML_LOG_LEVEL_WARN 2
/* high level tracing of DML interfaces */
#define DML_LOG_LEVEL_INFO 3
/* tracing of DML internal executions */
#define DML_LOG_LEVEL_DEBUG 4
/* detailed tracing of DML calculation procedure */
#define DML_LOG_LEVEL_VERBOSE 5

#ifndef DML_LOG_LEVEL
#define DML_LOG_LEVEL DML_LOG_LEVEL_DEFAULT
#endif /* #ifndef DML_LOG_LEVEL */

/* public macros for DML_LOG_LEVEL_FATAL and up */
#define DML_LOG_FATAL(fmt, ...) DML_LOG_INTERNAL("[DML FATAL] " fmt, ## __VA_ARGS__)

/* public macros for DML_LOG_LEVEL_ERROR and up */
#if DML_LOG_LEVEL >= DML_LOG_LEVEL_ERROR
#define DML_LOG_ERROR(fmt, ...) DML_LOG_INTERNAL("[DML ERROR] "fmt, ## __VA_ARGS__)
#define DML_ASSERT_MSG(condition, fmt, ...)								\
	do {												\
		if (!(condition)) {									\
			DML_LOG_ERROR("ASSERT hit in %s line %d\n", __func__, __LINE__);		\
			DML_LOG_ERROR(fmt, ## __VA_ARGS__);						\
			DML_ASSERT(condition);								\
		}											\
	} while (0)
#else
#define DML_LOG_ERROR(fmt, ...) ((void)0)
#define DML_ASSERT_MSG(condition, fmt, ...) ((void)0)
#endif

/* public macros for DML_LOG_LEVEL_WARN and up */
#if DML_LOG_LEVEL >= DML_LOG_LEVEL_WARN
#define DML_LOG_WARN(fmt, ...) DML_LOG_INTERNAL("[DML WARN] "fmt, ## __VA_ARGS__)
#else
#define DML_LOG_WARN(fmt, ...) ((void)0)
#endif

/* public macros for DML_LOG_LEVEL_INFO and up */
#if DML_LOG_LEVEL >= DML_LOG_LEVEL_INFO
#define DML_LOG_INFO(fmt, ...) DML_LOG_INTERNAL("[DML INFO] "fmt, ## __VA_ARGS__)
#define DML_LOG_TOP_IF_ENTER() _LOG_ENTRY(_ELEMENT_TOP_IF)
#define DML_LOG_TOP_IF_EXIT() _LOG_EXIT(_ELEMENT_TOP_IF)
#else
#define DML_LOG_INFO(fmt, ...) ((void)0)
#define DML_LOG_TOP_IF_ENTER() ((void)0)
#define DML_LOG_TOP_IF_EXIT() ((void)0)
#endif

/* public macros for DML_LOG_LEVEL_DEBUG and up */
#if DML_LOG_LEVEL >= DML_LOG_LEVEL_DEBUG
#define DML_LOG_DEBUG(fmt, ...) DML_LOG_INTERNAL(fmt, ## __VA_ARGS__)
#define DML_LOG_COMP_IF_ENTER() _LOG_ENTRY(_ELEMENT_COMP_IF)
#define DML_LOG_COMP_IF_EXIT() _LOG_EXIT(_ELEMENT_COMP_IF)
#define DML_LOG_FUNC_ENTER() _LOG_ENTRY(_ELEMENT_FUNC)
#define DML_LOG_FUNC_EXIT() _LOG_EXIT(_ELEMENT_FUNC)
#define DML_LOG_DEBUG_BOOL(field) _LOG_SCALAR(field, _BOOL_FORMAT)
#define DML_LOG_DEBUG_UINT(field) _LOG_SCALAR(field, _UINT_FORMAT)
#define DML_LOG_DEBUG_INT(field) _LOG_SCALAR(field, _INT_FORMAT)
#define DML_LOG_DEBUG_DOUBLE(field) _LOG_SCALAR(field, _DOUBLE_FORMAT)
#define DML_LOG_DEBUG_ARRAY_BOOL(field, size) _LOG_ARRAY(field, size, _BOOL_FORMAT)
#define DML_LOG_DEBUG_ARRAY_UINT(field, size) _LOG_ARRAY(field, size, _UINT_FORMAT)
#define DML_LOG_DEBUG_ARRAY_INT(field, size) _LOG_ARRAY(field, size, _INT_FORMAT)
#define DML_LOG_DEBUG_ARRAY_DOUBLE(field, size) _LOG_ARRAY(field, size, _DOUBLE_FORMAT)
#define DML_LOG_DEBUG_2D_ARRAY_BOOL(field, size0, size1) _LOG_2D_ARRAY(field, size0, size1, _BOOL_FORMAT)
#define DML_LOG_DEBUG_2D_ARRAY_UINT(field, size0, size1) _LOG_2D_ARRAY(field, size0, size1, _UINT_FORMAT)
#define DML_LOG_DEBUG_2D_ARRAY_INT(field, size0, size1) _LOG_2D_ARRAY(field, size0, size1, _INT_FORMAT)
#define DML_LOG_DEBUG_2D_ARRAY_DOUBLE(field, size0, size1) _LOG_2D_ARRAY(field, size0, size1, _DOUBLE_FORMAT)
#define DML_LOG_DEBUG_3D_ARRAY_BOOL(field, size0, size1, size2) _LOG_3D_ARRAY(field, size0, size1, size2, _BOOL_FORMAT)
#define DML_LOG_DEBUG_3D_ARRAY_UINT(field, size0, size1, size2) _LOG_3D_ARRAY(field, size0, size1, size2, _UINT_FORMAT)
#define DML_LOG_DEBUG_3D_ARRAY_INT(field, size0, size1, size2) _LOG_3D_ARRAY(field, size0, size1, size2, _INT_FORMAT)
#define DML_LOG_DEBUG_3D_ARRAY_DOUBLE(field, size0, size1, size2) _LOG_3D_ARRAY(field, size0, size1, size2, _DOUBLE_FORMAT)
#else
#define DML_LOG_DEBUG(fmt, ...) ((void)0)
#define DML_LOG_COMP_IF_ENTER() ((void)0)
#define DML_LOG_COMP_IF_EXIT() ((void)0)
#define DML_LOG_FUNC_ENTER() ((void)0)
#define DML_LOG_FUNC_EXIT() ((void)0)
#define DML_LOG_DEBUG_BOOL(field) ((void)0)
#define DML_LOG_DEBUG_UINT(field) ((void)0)
#define DML_LOG_DEBUG_INT(field) ((void)0)
#define DML_LOG_DEBUG_DOUBLE(field) ((void)0)
#define DML_LOG_DEBUG_ARRAY_BOOL(field, size) ((void)0)
#define DML_LOG_DEBUG_ARRAY_UINT(field, size) ((void)0)
#define DML_LOG_DEBUG_ARRAY_INT(field, size) ((void)0)
#define DML_LOG_DEBUG_ARRAY_DOUBLE(field, size) ((void)0)
#define DML_LOG_DEBUG_2D_ARRAY_BOOL(field, size0, size1) ((void)0)
#define DML_LOG_DEBUG_2D_ARRAY_UINT(field, size0, size1) ((void)0)
#define DML_LOG_DEBUG_2D_ARRAY_INT(field, size0, size1) ((void)0)
#define DML_LOG_DEBUG_2D_ARRAY_DOUBLE(field, size0, size1) ((void)0)
#define DML_LOG_DEBUG_3D_ARRAY_BOOL(field, size0, size1, size2) ((void)0)
#define DML_LOG_DEBUG_3D_ARRAY_UINT(field, size0, size1, size2) ((void)0)
#define DML_LOG_DEBUG_3D_ARRAY_INT(field, size0, size1, size2) ((void)0)
#define DML_LOG_DEBUG_3D_ARRAY_DOUBLE(field, size0, size1, size2) ((void)0)
#endif

/* public macros for DML_LOG_LEVEL_VERBOSE */
#if DML_LOG_LEVEL >= DML_LOG_LEVEL_VERBOSE
#define DML_LOG_VERBOSE(fmt, ...) DML_LOG_INTERNAL(fmt, ## __VA_ARGS__)
#else
#define DML_LOG_VERBOSE(fmt, ...) ((void)0)
#endif /* #if DML_LOG_LEVEL >= DML_LOG_LEVEL_VERBOSE */
#endif /* __DML2_DEBUG_H__ */
