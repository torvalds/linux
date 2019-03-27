/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
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

#include <string.h>

#include <infiniband/mad.h>

#undef DEBUG
#define DEBUG 	if (ibdebug)	IBWARN

static inline int response_expected(int method)
{
	return method == IB_MAD_METHOD_GET ||
	    method == IB_MAD_METHOD_SET || method == IB_MAD_METHOD_TRAP;
}

uint8_t *bm_call_via(void *data, ib_portid_t * portid, ib_bm_call_t * call,
		     struct ibmad_port * srcport)
{
	ib_rpc_t rpc = { 0 };
	int resp_expected;
	struct {
		uint64_t bkey;
		uint8_t reserved[32];
		uint8_t data[IB_BM_DATA_SZ];
	} bm_data;

	DEBUG("route %s data %p", portid2str(portid), data);
	if (portid->lid <= 0) {
		IBWARN("only lid routes are supported");
		return NULL;
	}

	resp_expected = response_expected(call->method);

	rpc.mgtclass = IB_BOARD_MGMT_CLASS;

	rpc.method = call->method;
	rpc.attr.id = call->attrid;
	rpc.attr.mod = call->mod;
	rpc.timeout = resp_expected ? call->timeout : 0;
	// send data and bkey
	rpc.datasz = IB_BM_BKEY_AND_DATA_SZ;
	rpc.dataoffs = IB_BM_BKEY_OFFS;

	// copy data to a buffer which also includes the bkey
	bm_data.bkey = htonll(call->bkey);
	memset(bm_data.reserved, 0, sizeof(bm_data.reserved));
	memcpy(bm_data.data, data, IB_BM_DATA_SZ);

	DEBUG
	    ("method 0x%x attr 0x%x mod 0x%x datasz %d off %d res_ex %d bkey 0x%08x%08x",
	     rpc.method, rpc.attr.id, rpc.attr.mod, rpc.datasz, rpc.dataoffs,
	     resp_expected, (int)(call->bkey >> 32), (int)call->bkey);

	portid->qp = 1;
	if (!portid->qkey)
		portid->qkey = IB_DEFAULT_QP1_QKEY;

	if (resp_expected) {
		/* FIXME: no RMPP for now */
		if (mad_rpc(srcport, &rpc, portid, &bm_data, &bm_data))
			goto return_ok;
		return NULL;
	}

	if (mad_send_via(&rpc, portid, 0, &bm_data, srcport) < 0)
		return NULL;

return_ok:
	memcpy(data, bm_data.data, IB_BM_DATA_SZ);
	return data;
}
