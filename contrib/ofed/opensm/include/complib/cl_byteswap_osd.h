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
 *	Provides common macros for dealing with byte swapping issues.
 */

#ifndef _CL_BYTESWAP_OSD_H_
#define _CL_BYTESWAP_OSD_H_

/*
 * This provides defines __LITTLE_ENDIAN, __BIG_ENDIAN and __BYTE_ORDER
 */
#include <infiniband/endian.h>
#include <infiniband/byteswap.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define cl_ntoh16(x)	bswap_16(x)
#define cl_hton16(x)	bswap_16(x)
#define cl_ntoh32(x)	bswap_32(x)
#define cl_hton32(x)	bswap_32(x)
#define cl_ntoh64(x)	(uint64_t)bswap_64(x)
#define cl_hton64(x)	(uint64_t)bswap_64(x)
#else				/* Big Endian */
#define cl_ntoh16(x)	(x)
#define cl_hton16(x)	(x)
#define cl_ntoh32(x)	(x)
#define cl_hton32(x)	(x)
#define cl_ntoh64(x)	(x)
#define cl_hton64(x)	(x)
#endif
END_C_DECLS
#endif				/* _CL_BYTESWAP_OSD_H_ */
