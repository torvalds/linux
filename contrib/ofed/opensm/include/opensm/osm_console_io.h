/*
 * Copyright (c) 2005-2007 Voltaire, Inc. All rights reserved.
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
 * 	Declaration of osm_console_t.
 *	This object represents the OpenSM Console object.
 *	This object is part of the OpenSM family of objects.
 */

#ifndef _OSM_CONSOLE_IO_H_
#define _OSM_CONSOLE_IO_H_

#include <opensm/osm_subnet.h>
#include <opensm/osm_log.h>

#define OSM_DISABLE_CONSOLE      "off"
#define OSM_LOCAL_CONSOLE        "local"
#ifdef ENABLE_OSM_CONSOLE_SOCKET
#define OSM_REMOTE_CONSOLE       "socket"
#endif
#ifdef ENABLE_OSM_CONSOLE_LOOPBACK
#define OSM_LOOPBACK_CONSOLE     "loopback"
#endif
#define OSM_CONSOLE_NAME         "OSM Console"

#define OSM_DEFAULT_CONSOLE      OSM_DISABLE_CONSOLE
#define OSM_DEFAULT_CONSOLE_PORT 10000
#define OSM_DAEMON_NAME          "opensm"

#define OSM_COMMAND_PROMPT	 "$ "

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
typedef struct osm_console {
	int socket;
	int in_fd;
	int out_fd;
	int authorized;
	FILE *in;
	FILE *out;
	char client_type[32];
	char client_ip[64];
	char client_hn[128];
} osm_console_t;

void osm_console_prompt(FILE * out);
int osm_console_init(osm_subn_opt_t * opt, osm_console_t * p_oct, osm_log_t * p_log);
void osm_console_exit(osm_console_t * p_oct, osm_log_t * p_log);
int is_console_enabled(osm_subn_opt_t *p_opt);

#ifdef ENABLE_OSM_CONSOLE_LOOPBACK
int cio_open(osm_console_t * p_oct, int new_fd, osm_log_t * p_log);
int cio_close(osm_console_t * p_oct, osm_log_t * p_log);
int is_authorized(osm_console_t * p_oct);
#else
#define cio_close(c, log)
#endif

END_C_DECLS
#endif				/* _OSM_CONSOLE_IO_H_ */
