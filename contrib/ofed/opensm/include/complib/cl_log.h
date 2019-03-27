/*
 * Copyright (c) 2004, 2005 Voltaire, Inc. All rights reserved.
 * Copyright (c) 2002-2005 Mellanox Technologies LTD. All rights reserved.
 * Copyright (c) 1996-2003 Intel Corporation. All rights reserved.
 *
 * This software is available to you under a choice of one of two
 * licenses.  You may choose to be licensed under the terms of the GNU
 * General Public License (GPL) Version 2, available from the file
 * COPYING in the main directory of this source tree, or the
 * OpenIB.org BSD license below:
 *
 *     Redistribution and use in source and binary forms, with or
 *     without modification, are permitted provided that the following
 *     conditions are met:
 *
 *      - Redistributions of source code must retain the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer.
 *
 *      - Redistributions in binary form must reproduce the above
 *        copyright notice, this list of conditions and the following
 *        disclaimer in the documentation and/or other materials
 *        provided with the distribution.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * Abstract:
 *	Declaration of logging mechanisms.
 */

#ifndef _CL_LOG_H_
#define _CL_LOG_H_

#include <complib/cl_types.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
/****h* Component Library/Log Provider
* NAME
*	Log Provider
*
* DESCRIPTION
*	The log provider allows users to log information in a system log instead of
*	the console or debugger target.
**********/
/****d* Component Library: Log Provider/cl_log_type_t
* NAME
*	cl_log_type_t
*
* DESCRIPTION
*	The cl_log_type_t enumerated type is used to differentiate between
*	different types of log entries.
*
* SYNOPSIS
*/
typedef enum _cl_log_type {
	CL_LOG_INFO,
	CL_LOG_WARN,
	CL_LOG_ERROR
} cl_log_type_t;
/*
* VALUES
*	CL_LOG_INFO
*		Indicates a log entry is purely informational.
*
*	CL_LOG_WARN
*		Indicates a log entry is a warning but non-fatal.
*
*	CL_LOG_ERROR
*		Indicates a log entry is a fatal error.
*
* SEE ALSO
*	Log Provider, cl_log_event
*********/

/****f* Component Library: Log Provider/cl_log_event
* NAME
*	cl_log_event
*
* DESCRIPTION
*	The cl_log_event function adds a new entry to the system log.
*
* SYNOPSIS
*/
void
cl_log_event(IN const char *const name,
	     IN const cl_log_type_t type,
	     IN const char *const message,
	     IN const void *const p_data OPTIONAL, IN const uint32_t data_len);
/*
* PARAMETERS
*	name
*		[in] Pointer to an ANSI string containing the name of the source for
*		the log entry.
*
*	type
*		[in] Defines the type of log entry to add to the system log.
*		See the definition of cl_log_type_t for acceptable values.
*
*	message
*		[in] Pointer to an ANSI string containing the text for the log entry.
*		The message should not be terminated with a new line, as the log
*		provider appends a new line to all log entries.
*
*	p_data
*		[in] Optional pointer to data providing context for the log entry.
*		At most 256 bytes of data can be successfully logged.
*
*	data_len
*		[in] Length of the buffer pointed to by the p_data parameter.  Ignored
*		if p_data is NULL.
*
* RETURN VALUE
*	This function does not return a value.
*
* NOTES
*	If the data length exceeds the maximum supported, the event is logged
*	without its accompanying data.
*
* SEE ALSO
*	Log Provider, cl_log_type_t
*********/

END_C_DECLS
#endif				/* _CL_LOG_H_ */
