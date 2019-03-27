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

#ifdef __WIN__
#pragma warning(disable : 4996)
#endif

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <complib/cl_log.h>
#include <complib/cl_debug.h>
#include <syslog.h>

/* Maximum number of bytes that can be logged. */
#define CL_MAX_LOG_DATA		(256)

/*
 * Size of the character buffer to allow logging the above
 * number of bytes.  A space is added after every DWORD, and
 * a new line is added after 8 DWORDS (for a line length less than 80).
 */
#define CL_LOG_DATA_SIZE	(CL_MAX_LOG_DATA + (CL_MAX_LOG_DATA/4))

void cl_log_event(IN const char *const name, IN const cl_log_type_t type,
		  IN const char *const message,
		  IN const void *const p_data OPTIONAL,
		  IN const uint32_t data_len)
{
	int priority, i;
	char data[CL_LOG_DATA_SIZE];
	char *p_buf;
	uint8_t *p_int_data = (uint8_t *) p_data;

	CL_ASSERT(name);
	CL_ASSERT(message);

	openlog(name, LOG_NDELAY | LOG_PID, LOG_USER);
	switch (type) {
	case CL_LOG_ERROR:
		priority = LOG_ERR;
		break;

	case CL_LOG_WARN:
		priority = LOG_WARNING;
		break;

	case CL_LOG_INFO:
	default:
		priority = LOG_INFO;
		break;
	}

	if (p_data) {
		CL_ASSERT(data_len);
		if (data_len < CL_MAX_LOG_DATA) {
			p_buf = data;
			/* Format the data into ASCII. */
			for (i = 0; i < data_len; i++) {
				sprintf(p_buf, "%02x", *p_int_data++);
				p_buf += 2;

				/* Add line break after 8 DWORDS. */
				if (i % 32) {
					sprintf(p_buf++, "\n");
					continue;
				}

				/* Add a space between DWORDS. */
				if (i % 4)
					sprintf(p_buf++, " ");
			}
			syslog(priority, "%s data:\n%s\n", message, p_buf);
		} else {
			/* The data portion is too large to log. */
			cl_msg_out
			    ("cl_log() - WARNING: data too large to log.\n");
			syslog(priority, "%s\n", message);
		}
	} else {
		syslog(priority, "%s\n", message);
	}
	closelog();
}
