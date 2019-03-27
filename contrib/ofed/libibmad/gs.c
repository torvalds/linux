/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2011 Mellanox Technologies LTD.  All rights reserved.
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

#if HAVE_CONFIG_H
#  include <config.h>
#endif				/* HAVE_CONFIG_H */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>

#undef DEBUG
#define DEBUG 	if (ibdebug)	IBWARN

uint8_t *pma_query_via(void *rcvbuf, ib_portid_t * dest, int port,
		       unsigned timeout, unsigned id,
		       const struct ibmad_port * srcport)
{
	ib_rpc_v1_t rpc = { 0 };
	ib_rpc_t *rpcold = (ib_rpc_t *)(void *)&rpc;
	int lid = dest->lid;
	void *p_ret;

	DEBUG("lid %u port %d", lid, port);

	if (lid == -1) {
		IBWARN("only lid routed is supported");
		return NULL;
	}

	rpc.mgtclass = IB_PERFORMANCE_CLASS | IB_MAD_RPC_VERSION1;
	rpc.method = IB_MAD_METHOD_GET;
	rpc.attr.id = id;

	/* Same for attribute IDs */
	mad_set_field(rcvbuf, 0, IB_PC_PORT_SELECT_F, port);
	rpc.attr.mod = 0;
	rpc.timeout = timeout;
	rpc.datasz = IB_PC_DATA_SZ;
	rpc.dataoffs = IB_PC_DATA_OFFS;

	if (!dest->qp)
		dest->qp = 1;
	if (!dest->qkey)
		dest->qkey = IB_DEFAULT_QP1_QKEY;

	p_ret = mad_rpc(srcport, rpcold, dest, rcvbuf, rcvbuf);
	errno = rpc.error;
	return p_ret;
}

uint8_t *performance_reset_via(void *rcvbuf, ib_portid_t * dest,
			       int port, unsigned mask, unsigned timeout,
			       unsigned id, const struct ibmad_port * srcport)
{
	ib_rpc_v1_t rpc = { 0 };
	ib_rpc_t *rpcold = (ib_rpc_t *)(void *)&rpc;

	int lid = dest->lid;
	void *p_ret;

	DEBUG("lid %u port %d mask 0x%x", lid, port, mask);

	if (lid == -1) {
		IBWARN("only lid routed is supported");
		return NULL;
	}

	if (!mask)
		mask = ~0;

	rpc.mgtclass = IB_PERFORMANCE_CLASS | IB_MAD_RPC_VERSION1;
	rpc.method = IB_MAD_METHOD_SET;
	rpc.attr.id = id;

	memset(rcvbuf, 0, IB_MAD_SIZE);

	/* Next 2 lines - same for attribute IDs */
	mad_set_field(rcvbuf, 0, IB_PC_PORT_SELECT_F, port);
	mad_set_field(rcvbuf, 0, IB_PC_COUNTER_SELECT_F, mask);
	mask = mask >> 16;
	if (id == IB_GSI_PORT_COUNTERS_EXT)
		mad_set_field(rcvbuf, 0, IB_PC_EXT_COUNTER_SELECT2_F, mask);
	else
		mad_set_field(rcvbuf, 0, IB_PC_COUNTER_SELECT2_F, mask);
	rpc.attr.mod = 0;
	rpc.timeout = timeout;
	rpc.datasz = IB_PC_DATA_SZ;
	rpc.dataoffs = IB_PC_DATA_OFFS;
	if (!dest->qp)
		dest->qp = 1;
	if (!dest->qkey)
		dest->qkey = IB_DEFAULT_QP1_QKEY;

	p_ret = mad_rpc(srcport, rpcold, dest, rcvbuf, rcvbuf);
	errno = rpc.error;
	return p_ret;
}
