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

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>
#include "mad_internal.h"

#undef DEBUG
#define DEBUG 	if (ibdebug)	IBWARN

uint8_t *sa_rpc_call(const struct ibmad_port *ibmad_port, void *rcvbuf,
		     ib_portid_t * portid, ib_sa_call_t * sa, unsigned timeout)
{
	ib_rpc_t rpc = { 0 };
	uint8_t *p;

	DEBUG("attr 0x%x mod 0x%x route %s", sa->attrid, sa->mod,
	      portid2str(portid));

	if (portid->lid <= 0) {
		IBWARN("only lid routes are supported");
		return NULL;
	}

	rpc.mgtclass = IB_SA_CLASS;
	rpc.method = sa->method;
	rpc.attr.id = sa->attrid;
	rpc.attr.mod = sa->mod;
	rpc.mask = sa->mask;
	rpc.timeout = timeout;
	rpc.datasz = IB_SA_DATA_SIZE;
	rpc.dataoffs = IB_SA_DATA_OFFS;
	rpc.trid = sa->trid;

	portid->qp = 1;
	if (!portid->qkey)
		portid->qkey = IB_DEFAULT_QP1_QKEY;

	p = mad_rpc_rmpp(ibmad_port, &rpc, portid, 0 /*&sa->rmpp */ , rcvbuf);	/* TODO: RMPP */

	sa->recsz = rpc.recsz;

	return p;
}

uint8_t *sa_call(void *rcvbuf, ib_portid_t * portid, ib_sa_call_t * sa,
		 unsigned timeout)
{
	return sa_rpc_call(ibmp, rcvbuf, portid, sa, timeout);
}

/* PathRecord */
#define IB_PR_COMPMASK_DGID				(1ull<<2)
#define IB_PR_COMPMASK_SGID				(1ull<<3)
#define IB_PR_COMPMASK_DLID				(1ull<<4)
#define	IB_PR_COMPMASK_SLID				(1ull<<5)
#define	IB_PR_COMPMASK_RAWTRAFIC			(1ull<<6)
#define	IB_PR_COMPMASK_RESV0				(1ull<<7)
#define	IB_PR_COMPMASK_FLOWLABEL			(1ull<<8)
#define	IB_PR_COMPMASK_HOPLIMIT				(1ull<<9)
#define	IB_PR_COMPMASK_TCLASS				(1ull<<10)
#define	IB_PR_COMPMASK_REVERSIBLE			(1ull<<11)
#define	IB_PR_COMPMASK_NUMBPATH				(1ull<<12)
#define	IB_PR_COMPMASK_PKEY				(1ull<<13)
#define	IB_PR_COMPMASK_RESV1				(1ull<<14)
#define	IB_PR_COMPMASK_SL				(1ull<<15)
#define	IB_PR_COMPMASK_MTUSELEC				(1ull<<16)
#define	IB_PR_COMPMASK_MTU				(1ull<<17)
#define	IB_PR_COMPMASK_RATESELEC			(1ull<<18)
#define	IB_PR_COMPMASK_RATE				(1ull<<19)
#define	IB_PR_COMPMASK_PKTLIFETIMESELEC			(1ull<<20)
#define	IB_PR_COMPMASK_PKTLIFETIME			(1ull<<21)
#define	IB_PR_COMPMASK_PREFERENCE			(1ull<<22)

#define IB_PR_DEF_MASK (IB_PR_COMPMASK_DGID |\
			IB_PR_COMPMASK_SGID)

int ib_path_query_via(const struct ibmad_port *srcport, ibmad_gid_t srcgid,
		      ibmad_gid_t destgid, ib_portid_t * sm_id, void *buf)
{
	ib_sa_call_t sa = { 0 };
	uint8_t *p;
	int dlid;

	memset(&sa, 0, sizeof sa);
	sa.method = IB_MAD_METHOD_GET;
	sa.attrid = IB_SA_ATTR_PATHRECORD;
	sa.mask = IB_PR_DEF_MASK;
	sa.trid = mad_trid();

	memset(buf, 0, IB_SA_PR_RECSZ);

	mad_encode_field(buf, IB_SA_PR_DGID_F, destgid);
	mad_encode_field(buf, IB_SA_PR_SGID_F, srcgid);

	p = sa_rpc_call(srcport, buf, sm_id, &sa, 0);
	if (!p) {
		IBWARN("sa call path_query failed");
		return -1;
	}

	mad_decode_field(p, IB_SA_PR_DLID_F, &dlid);
	return dlid;
}

int ib_path_query(ibmad_gid_t srcgid, ibmad_gid_t destgid, ib_portid_t * sm_id,
		  void *buf)
{
	return ib_path_query_via(ibmp, srcgid, destgid, sm_id, buf);
}

/* NodeRecord */
#define IB_NR_COMPMASK_LID				(1ull<<0)
#define IB_NR_COMPMASK_RESERVED1			(1ull<<1)
#define IB_NR_COMPMASK_BASEVERSION			(1ull<<2)
#define IB_NR_COMPMASK_CLASSVERSION			(1ull<<3)
#define IB_NR_COMPMASK_NODETYPE				(1ull<<4)
#define IB_NR_COMPMASK_NUMPORTS				(1ull<<5)
#define IB_NR_COMPMASK_SYSIMAGEGUID			(1ull<<6)
#define IB_NR_COMPMASK_NODEGUID				(1ull<<7)
#define IB_NR_COMPMASK_PORTGUID				(1ull<<8)
#define IB_NR_COMPMASK_PARTCAP				(1ull<<9)
#define IB_NR_COMPMASK_DEVID				(1ull<<10)
#define IB_NR_COMPMASK_REV				(1ull<<11)
#define IB_NR_COMPMASK_PORTNUM				(1ull<<12)
#define IB_NR_COMPMASK_VENDID				(1ull<<13)
#define IB_NR_COMPMASK_NODEDESC				(1ull<<14)

#define IB_NR_DEF_MASK IB_NR_COMPMASK_PORTGUID

int ib_node_query_via(const struct ibmad_port *srcport, uint64_t guid,
		      ib_portid_t * sm_id, void *buf)
{
	ib_sa_call_t sa = { 0 };
	uint8_t *p;

	memset(&sa, 0, sizeof sa);
	sa.method = IB_MAD_METHOD_GET;
	sa.attrid = IB_SA_ATTR_NODERECORD;
	sa.mask = IB_NR_DEF_MASK;
	sa.trid = mad_trid();

	memset(buf, 0, IB_SA_NR_RECSZ);

	mad_encode_field(buf, IB_SA_NR_PORT_GUID_F, &guid);

	p = sa_rpc_call(srcport, buf, sm_id, &sa, 0);
	if (!p) {
		IBWARN("sa call node_query failed");
		return -1;
	}

	return 0;
}
