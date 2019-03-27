/*
 * Copyright (c) 2004-2009 Voltaire Inc.  All rights reserved.
 * Copyright (c) 2009 HNR Consulting.  All rights reserved.
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
#include <time.h>
#include <errno.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>

#include "mad_internal.h"

#undef DEBUG
#define DEBUG	if (ibdebug)	IBWARN

#define GET_IB_USERLAND_TID(tid)	(tid & 0x00000000ffffffff)
/*
 * Generate the 64 bit MAD transaction ID. The upper 32 bits are reserved for
 * use by the kernel. We clear the upper 32 bits here, but MADs received from
 * the kernel may contain kernel specific data in these bits, consequently
 * userland TID matching should only be done on the lower 32 bits.
 */
uint64_t mad_trid(void)
{
	static uint64_t trid;
	uint64_t next;

	if (!trid) {
		srandom((int)time(0) * getpid());
		trid = random();
	}
	next = ++trid;
	next = GET_IB_USERLAND_TID(next);
	return next;
}

int mad_get_timeout(const struct ibmad_port *srcport, int override_ms)
{
	return (override_ms ? override_ms :
		srcport->timeout ? srcport->timeout : madrpc_timeout);
}

int mad_get_retries(const struct ibmad_port *srcport)
{
	return (srcport->retries ? srcport->retries : madrpc_retries);
}

void *mad_encode(void *buf, ib_rpc_t * rpc, ib_dr_path_t * drpath, void *data)
{
	int is_resp = rpc->method & IB_MAD_RESPONSE;
	int mgtclass;

	/* first word */
	mad_set_field(buf, 0, IB_MAD_METHOD_F, rpc->method);
	mad_set_field(buf, 0, IB_MAD_RESPONSE_F, is_resp ? 1 : 0);
	mgtclass = rpc->mgtclass & 0xff;
	if (mgtclass == IB_SA_CLASS || mgtclass == IB_CC_CLASS)
		mad_set_field(buf, 0, IB_MAD_CLASSVER_F, 2);
	else
		mad_set_field(buf, 0, IB_MAD_CLASSVER_F, 1);
	mad_set_field(buf, 0, IB_MAD_MGMTCLASS_F, rpc->mgtclass & 0xff);
	mad_set_field(buf, 0, IB_MAD_BASEVER_F, 1);

	/* second word */
	if ((rpc->mgtclass & 0xff) == IB_SMI_DIRECT_CLASS) {
		if (!drpath) {
			IBWARN("encoding dr mad without drpath (null)");
			errno = EINVAL;
			return NULL;
		}
		if (drpath->cnt >= IB_SUBNET_PATH_HOPS_MAX) {
			IBWARN("dr path with hop count %d", drpath->cnt);
			errno = EINVAL;
			return NULL;
		}
		mad_set_field(buf, 0, IB_DRSMP_HOPCNT_F, drpath->cnt);
		mad_set_field(buf, 0, IB_DRSMP_HOPPTR_F,
			      is_resp ? drpath->cnt + 1 : 0x0);
		mad_set_field(buf, 0, IB_DRSMP_STATUS_F, rpc->rstatus);
		mad_set_field(buf, 0, IB_DRSMP_DIRECTION_F, is_resp ? 1 : 0);	/* out */
	} else
		mad_set_field(buf, 0, IB_MAD_STATUS_F, rpc->rstatus);

	/* words 3,4,5,6 */
	if (!rpc->trid)
		rpc->trid = mad_trid();

	mad_set_field64(buf, 0, IB_MAD_TRID_F, rpc->trid);
	mad_set_field(buf, 0, IB_MAD_ATTRID_F, rpc->attr.id);
	mad_set_field(buf, 0, IB_MAD_ATTRMOD_F, rpc->attr.mod);

	/* words 7,8 */
	mad_set_field64(buf, 0, IB_MAD_MKEY_F, rpc->mkey);

	if ((rpc->mgtclass & 0xff) == IB_SMI_DIRECT_CLASS) {
		/* word 9 */
		mad_set_field(buf, 0, IB_DRSMP_DRDLID_F,
			      drpath->drdlid ? drpath->drdlid : 0xffff);
		mad_set_field(buf, 0, IB_DRSMP_DRSLID_F,
			      drpath->drslid ? drpath->drslid : 0xffff);

		/* bytes 128 - 256 - by default should be zero due to memset */
		if (is_resp)
			mad_set_array(buf, 0, IB_DRSMP_RPATH_F, drpath->p);
		else
			mad_set_array(buf, 0, IB_DRSMP_PATH_F, drpath->p);
	}

	if ((rpc->mgtclass & 0xff) == IB_SA_CLASS)
		mad_set_field64(buf, 0, IB_SA_COMPMASK_F, rpc->mask);

	if ((rpc->mgtclass & 0xff) == IB_CC_CLASS) {
		ib_rpc_cc_t *rpccc = (ib_rpc_cc_t *)rpc;
		mad_set_field64(buf, 0, IB_CC_CCKEY_F, rpccc->cckey);
	}

	if (data)
		memcpy((char *)buf + rpc->dataoffs, data, rpc->datasz);

	/* vendor mads range 2 */
	if (mad_is_vendor_range2(rpc->mgtclass & 0xff))
		mad_set_field(buf, 0, IB_VEND2_OUI_F, rpc->oui);

	return (uint8_t *) buf + IB_MAD_SIZE;
}

int mad_build_pkt(void *umad, ib_rpc_t * rpc, ib_portid_t * dport,
		  ib_rmpp_hdr_t * rmpp, void *data)
{
	uint8_t *p, *mad;
	int lid_routed = (rpc->mgtclass & 0xff) != IB_SMI_DIRECT_CLASS;
	int is_smi = ((rpc->mgtclass & 0xff) == IB_SMI_CLASS ||
		      (rpc->mgtclass & 0xff) == IB_SMI_DIRECT_CLASS);
	struct ib_mad_addr addr;

	if (!is_smi)
		umad_set_addr(umad, dport->lid, dport->qp, dport->sl,
			      dport->qkey);
	else if (lid_routed)
		umad_set_addr(umad, dport->lid, dport->qp, 0, 0);
	else if ((dport->drpath.drslid != 0xffff) && (dport->lid > 0))
		umad_set_addr(umad, dport->lid, 0, 0, 0);
	else
		umad_set_addr(umad, 0xffff, 0, 0, 0);

	if (dport->grh_present && !is_smi) {
		addr.grh_present = 1;
		memcpy(addr.gid, dport->gid, 16);
		addr.hop_limit = 0xff;
		addr.traffic_class = 0;
		addr.flow_label = 0;
		umad_set_grh(umad, &addr);
	} else
		umad_set_grh(umad, 0);
	umad_set_pkey(umad, is_smi ? 0 : dport->pkey_idx);

	mad = umad_get_mad(umad);
	p = mad_encode(mad, rpc, lid_routed ? 0 : &dport->drpath, data);
	if (!p)
		return -1;

	if (!is_smi && rmpp) {
		mad_set_field(mad, 0, IB_SA_RMPP_VERS_F, 1);
		mad_set_field(mad, 0, IB_SA_RMPP_TYPE_F, rmpp->type);
		mad_set_field(mad, 0, IB_SA_RMPP_RESP_F, 0x3f);
		mad_set_field(mad, 0, IB_SA_RMPP_FLAGS_F, rmpp->flags);
		mad_set_field(mad, 0, IB_SA_RMPP_STATUS_F, rmpp->status);
		mad_set_field(mad, 0, IB_SA_RMPP_D1_F, rmpp->d1.u);
		mad_set_field(mad, 0, IB_SA_RMPP_D2_F, rmpp->d2.u);
	}

	return ((int)(p - mad));
}
