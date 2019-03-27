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

#ifndef _OSMV_SVC_H_
#define _OSMV_SVC_H_

#include <stdlib.h>
#include <string.h>
#include <iba/ib_types.h>
#include <vendor/osm_vendor_mlx_defs.h>

#ifdef __cplusplus
#  define BEGIN_C_DECLS extern "C" {
#  define END_C_DECLS   }
#else				/* !__cplusplus */
#  define BEGIN_C_DECLS
#  define END_C_DECLS
#endif				/* __cplusplus */

BEGIN_C_DECLS
inline static uint8_t osmv_invert_method(IN uint8_t req_method)
{
	switch (req_method) {
	case IB_MAD_METHOD_GET_RESP:
		/* Not a 1-1 mapping! */
		return IB_MAD_METHOD_GET;

	case IB_MAD_METHOD_GET:
		return IB_MAD_METHOD_GET_RESP;

	case IB_MAD_METHOD_SET:
		return IB_MAD_METHOD_GET_RESP;

	case IB_MAD_METHOD_GETTABLE_RESP:
		return IB_MAD_METHOD_GETTABLE;

	case IB_MAD_METHOD_GETTABLE:
		return IB_MAD_METHOD_GETTABLE_RESP;

	case IB_MAD_METHOD_GETMULTI_RESP:
		/* Not a 1-1 mapping! */
		return IB_MAD_METHOD_GETMULTI;

	case IB_MAD_METHOD_GETTRACETABLE:
	case IB_MAD_METHOD_GETMULTI:
		return IB_MAD_METHOD_GETMULTI_RESP;

	case IB_MAD_METHOD_TRAP:
		return IB_MAD_METHOD_TRAP_REPRESS;

	case IB_MAD_METHOD_TRAP_REPRESS:
		return IB_MAD_METHOD_TRAP;

	case IB_MAD_METHOD_REPORT:
		return IB_MAD_METHOD_REPORT_RESP;

	case IB_MAD_METHOD_REPORT_RESP:
		return IB_MAD_METHOD_REPORT;

		/*  IB_MAD_METHOD_SEND does not have a response */
	case IB_MAD_METHOD_SEND:
		return IB_MAD_METHOD_SEND;

	default:
		CL_ASSERT(FALSE);
	}

	return 0;		/* Just make the compiler happy */
}

inline static boolean_t osmv_mad_is_rmpp(IN const ib_mad_t * p_mad)
{
	uint8_t rmpp_flags;
	CL_ASSERT(NULL != p_mad);

	rmpp_flags = ((ib_rmpp_mad_t *) p_mad)->rmpp_flags;
	/* HACK - JUST SA and DevMgt for now - need to add BIS and DevAdm */
	if ((p_mad->mgmt_class != IB_MCLASS_SUBN_ADM) &&
	    (p_mad->mgmt_class != IB_MCLASS_DEV_MGMT))
		return (0);
	return (0 != (rmpp_flags & IB_RMPP_FLAG_ACTIVE));
}

inline static boolean_t osmv_mad_is_multi_resp(IN const ib_mad_t * p_mad)
{
	CL_ASSERT(NULL != p_mad);
	return (IB_MAD_METHOD_GETMULTI == p_mad->method
		|| IB_MAD_METHOD_GETTRACETABLE == p_mad->method);
}

inline static boolean_t osmv_mad_is_sa(IN const ib_mad_t * p_mad)
{
	CL_ASSERT(NULL != p_mad);
	return (IB_MCLASS_SUBN_ADM == p_mad->mgmt_class);
}

inline static boolean_t osmv_rmpp_is_abort_stop(IN const ib_mad_t * p_mad)
{
	uint8_t rmpp_type;
	CL_ASSERT(p_mad);

	rmpp_type = ((ib_rmpp_mad_t *) p_mad)->rmpp_type;
	return (IB_RMPP_TYPE_STOP == rmpp_type
		|| IB_RMPP_TYPE_ABORT == rmpp_type);
}

inline static boolean_t osmv_rmpp_is_data(IN const ib_mad_t * p_mad)
{
	CL_ASSERT(p_mad);
	return (IB_RMPP_TYPE_DATA == ((ib_rmpp_mad_t *) p_mad)->rmpp_type);
}

inline static boolean_t osmv_rmpp_is_ack(IN const ib_mad_t * p_mad)
{
	CL_ASSERT(p_mad);
	return (IB_RMPP_TYPE_ACK == ((ib_rmpp_mad_t *) p_mad)->rmpp_type);
}

inline static boolean_t osmv_rmpp_is_first(IN const ib_mad_t * p_mad)
{
	uint8_t rmpp_flags;
	CL_ASSERT(NULL != p_mad);

	rmpp_flags = ((ib_rmpp_mad_t *) p_mad)->rmpp_flags;
	return (0 != (IB_RMPP_FLAG_FIRST & rmpp_flags));
}

inline static boolean_t osmv_rmpp_is_last(IN const ib_mad_t * p_mad)
{
	uint8_t rmpp_flags;
	CL_ASSERT(NULL != p_mad);

	rmpp_flags = ((ib_rmpp_mad_t *) p_mad)->rmpp_flags;
	return (0 != (IB_RMPP_FLAG_LAST & rmpp_flags));
}

inline static uint8_t *osmv_mad_copy(IN const ib_mad_t * p_mad)
{
	uint8_t *p_copy;

	CL_ASSERT(p_mad);
	p_copy = malloc(MAD_BLOCK_SIZE);

	if (NULL != p_copy) {
		memset(p_copy, 0, MAD_BLOCK_SIZE);
		memcpy(p_copy, p_mad, MAD_BLOCK_SIZE);
	}

	return p_copy;
}

/* Should be passed externally from the Makefile */
/*  #define OSMV_RANDOM_DROP 1 */
#define OSMV_DROP_RATE   0.3

inline static boolean_t osmv_random_drop(void)
{
	srand(1);		/* Pick a new base */
	return (rand() / (double)RAND_MAX < OSMV_DROP_RATE);
}

END_C_DECLS
#endif				/* _OSMV_SVC_H_ */
