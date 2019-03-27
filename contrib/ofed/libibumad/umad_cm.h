/*
 * Copyright (c) 2010 Intel Corporation.  All rights reserved.
 * Copyright (c) 2014 Mellanox Technologies LTD. All rights reserved.
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
 */

#ifndef _UMAD_CM_H
#define _UMAD_CM_H

#include <infiniband/umad_types.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS

/* Communication management attributes */
enum {
	UMAD_CM_ATTR_REQ		= 0x0010,
	UMAD_CM_ATTR_MRA		= 0x0011,
	UMAD_CM_ATTR_REJ		= 0x0012,
	UMAD_CM_ATTR_REP		= 0x0013,
	UMAD_CM_ATTR_RTU		= 0x0014,
	UMAD_CM_ATTR_DREQ		= 0x0015,
	UMAD_CM_ATTR_DREP		= 0x0016,
	UMAD_CM_ATTR_SIDR_REQ		= 0x0017,
	UMAD_CM_ATTR_SIDR_REP		= 0x0018,
	UMAD_CM_ATTR_LAP		= 0x0019,
	UMAD_CM_ATTR_APR		= 0x001A,
	UMAD_CM_ATTR_SAP		= 0x001B,
	UMAD_CM_ATTR_SPR		= 0x001C,
};

END_C_DECLS
#endif				/* _UMAD_CM_H */
