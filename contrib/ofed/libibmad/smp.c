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

#include <infiniband/umad.h>
#include <infiniband/mad.h>
#include "mad_internal.h"

#undef DEBUG
#define DEBUG 	if (ibdebug)	IBWARN

void smp_mkey_set(struct ibmad_port *srcport, uint64_t mkey)
{
	srcport->smp_mkey = mkey;
}

uint64_t smp_mkey_get(const struct ibmad_port *srcport)
{
	return srcport->smp_mkey;
}

uint8_t *smp_set_status_via(void *data, ib_portid_t * portid, unsigned attrid,
			    unsigned mod, unsigned timeout, int *rstatus,
			    const struct ibmad_port *srcport)
{
	ib_rpc_t rpc = { 0 };
	uint8_t *res;

	DEBUG("attr 0x%x mod 0x%x route %s", attrid, mod, portid2str(portid));
	if ((portid->lid <= 0) ||
	    (portid->drpath.drslid == 0xffff) ||
	    (portid->drpath.drdlid == 0xffff))
		rpc.mgtclass = IB_SMI_DIRECT_CLASS;	/* direct SMI */
	else
		rpc.mgtclass = IB_SMI_CLASS;	/* Lid routed SMI */

	rpc.method = IB_MAD_METHOD_SET;
	rpc.attr.id = attrid;
	rpc.attr.mod = mod;
	rpc.timeout = timeout;
	rpc.datasz = IB_SMP_DATA_SIZE;
	rpc.dataoffs = IB_SMP_DATA_OFFS;
	rpc.mkey = srcport->smp_mkey;

	portid->sl = 0;
	portid->qp = 0;

	res = mad_rpc(srcport, &rpc, portid, data, data);
	if (rstatus)
		*rstatus = rpc.rstatus;
	return res;
}

uint8_t *smp_set_via(void *data, ib_portid_t * portid, unsigned attrid,
		     unsigned mod, unsigned timeout,
		     const struct ibmad_port *srcport)
{
	return smp_set_status_via(data, portid, attrid, mod, timeout, NULL,
				  srcport);
}

uint8_t *smp_set(void *data, ib_portid_t * portid, unsigned attrid,
		 unsigned mod, unsigned timeout)
{
	return smp_set_via(data, portid, attrid, mod, timeout, ibmp);
}

uint8_t *smp_query_status_via(void *rcvbuf, ib_portid_t * portid,
			      unsigned attrid, unsigned mod, unsigned timeout,
			      int *rstatus, const struct ibmad_port * srcport)
{
	ib_rpc_t rpc = { 0 };
	uint8_t *res;

	DEBUG("attr 0x%x mod 0x%x route %s", attrid, mod, portid2str(portid));
	rpc.method = IB_MAD_METHOD_GET;
	rpc.attr.id = attrid;
	rpc.attr.mod = mod;
	rpc.timeout = timeout;
	rpc.datasz = IB_SMP_DATA_SIZE;
	rpc.dataoffs = IB_SMP_DATA_OFFS;
	rpc.mkey = srcport->smp_mkey;

	if ((portid->lid <= 0) ||
	    (portid->drpath.drslid == 0xffff) ||
	    (portid->drpath.drdlid == 0xffff))
		rpc.mgtclass = IB_SMI_DIRECT_CLASS;	/* direct SMI */
	else
		rpc.mgtclass = IB_SMI_CLASS;	/* Lid routed SMI */

	portid->sl = 0;
	portid->qp = 0;

	res = mad_rpc(srcport, &rpc, portid, rcvbuf, rcvbuf);
	if (rstatus)
		*rstatus = rpc.rstatus;
	return res;
}

uint8_t *smp_query_via(void *rcvbuf, ib_portid_t * portid, unsigned attrid,
		       unsigned mod, unsigned timeout,
		       const struct ibmad_port * srcport)
{
	return smp_query_status_via(rcvbuf, portid, attrid, mod, timeout, NULL,
				    srcport);
}

uint8_t *smp_query(void *rcvbuf, ib_portid_t * portid, unsigned attrid,
		   unsigned mod, unsigned timeout)
{
	return smp_query_via(rcvbuf, portid, attrid, mod, timeout, ibmp);
}
