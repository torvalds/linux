/*
 * Copyright (c) 2011 Lawrence Livermore National Lab.  All rights reserved.
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

#include <infiniband/umad.h>
#include <infiniband/mad.h>
#include "mad_internal.h"

#undef DEBUG
#define DEBUG 	if (ibdebug)	IBWARN

void *cc_query_status_via(void *rcvbuf, ib_portid_t * portid,
			  unsigned attrid, unsigned mod, unsigned timeout,
			  int *rstatus, const struct ibmad_port * srcport,
			  uint64_t cckey)
{
	ib_rpc_cc_t rpc = { 0 };
	void *res;

	DEBUG("attr 0x%x mod 0x%x route %s", attrid, mod, portid2str(portid));
	rpc.method = IB_MAD_METHOD_GET;
	rpc.attr.id = attrid;
	rpc.attr.mod = mod;
	rpc.timeout = timeout;
	if (attrid == IB_CC_ATTR_CONGESTION_LOG) {
		rpc.datasz = IB_CC_LOG_DATA_SZ;
		rpc.dataoffs = IB_CC_LOG_DATA_OFFS;
	}
	else {
		rpc.datasz = IB_CC_DATA_SZ;
		rpc.dataoffs = IB_CC_DATA_OFFS;
	}
	rpc.mgtclass = IB_CC_CLASS;
	rpc.cckey = cckey;

	portid->qp = 1;
	if (!portid->qkey)
		portid->qkey = IB_DEFAULT_QP1_QKEY;

	res = mad_rpc(srcport, (ib_rpc_t *)&rpc, portid, rcvbuf, rcvbuf);
	if (rstatus)
		*rstatus = rpc.rstatus;

	return res;
}

void *cc_config_status_via(void *payload, void *rcvbuf, ib_portid_t * portid,
                           unsigned attrid, unsigned mod, unsigned timeout,
                           int *rstatus, const struct ibmad_port * srcport,
                           uint64_t cckey)
{
	ib_rpc_cc_t rpc = { 0 };
	void *res;

	DEBUG("attr 0x%x mod 0x%x route %s", attrid, mod, portid2str(portid));
	rpc.method = IB_MAD_METHOD_SET;
	rpc.attr.id = attrid;
	rpc.attr.mod = mod;
	rpc.timeout = timeout;
	if (attrid == IB_CC_ATTR_CONGESTION_LOG) {
		rpc.datasz = IB_CC_LOG_DATA_SZ;
		rpc.dataoffs = IB_CC_LOG_DATA_OFFS;
	}
	else {
		rpc.datasz = IB_CC_DATA_SZ;
		rpc.dataoffs = IB_CC_DATA_OFFS;
	}
	rpc.mgtclass = IB_CC_CLASS;
	rpc.cckey = cckey;

	portid->qp = 1;
	if (!portid->qkey)
		portid->qkey = IB_DEFAULT_QP1_QKEY;

	res = mad_rpc(srcport, (ib_rpc_t *)&rpc, portid, payload, rcvbuf);
	if (rstatus)
		*rstatus = rpc.rstatus;

	return res;
}


