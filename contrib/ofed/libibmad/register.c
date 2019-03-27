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
#include <errno.h>

#include <infiniband/umad.h>
#include <infiniband/mad.h>

#include "mad_internal.h"

#undef DEBUG
#define DEBUG	if (ibdebug)	IBWARN

static int mgmt_class_vers(int mgmt_class)
{
	if ((mgmt_class >= IB_VENDOR_RANGE1_START_CLASS &&
	     mgmt_class <= IB_VENDOR_RANGE1_END_CLASS) ||
	    (mgmt_class >= IB_VENDOR_RANGE2_START_CLASS &&
	     mgmt_class <= IB_VENDOR_RANGE2_END_CLASS))
		return 1;

	switch (mgmt_class) {
	case IB_SMI_CLASS:
	case IB_SMI_DIRECT_CLASS:
		return 1;
	case IB_SA_CLASS:
		return 2;
	case IB_PERFORMANCE_CLASS:
		return 1;
	case IB_DEVICE_MGMT_CLASS:
		return 1;
	case IB_CC_CLASS:
		return 2;
	case IB_BOARD_MGMT_CLASS:
		return 1;
	}

	return 0;
}

int mad_class_agent(int mgmt)
{
	if (mgmt < 1 || mgmt >= MAX_CLASS)
		return -1;
	return ibmp->class_agents[mgmt];
}

int mad_register_port_client(int port_id, int mgmt, uint8_t rmpp_version)
{
	int vers, agent;

	if ((vers = mgmt_class_vers(mgmt)) <= 0) {
		DEBUG("Unknown class %d mgmt_class", mgmt);
		return -1;
	}

	agent = umad_register(port_id, mgmt, vers, rmpp_version, 0);
	if (agent < 0)
		DEBUG("Can't register agent for class %d", mgmt);

	return agent;
}

int mad_register_client(int mgmt, uint8_t rmpp_version)
{
	return mad_register_client_via(mgmt, rmpp_version, ibmp);
}

int mad_register_client_via(int mgmt, uint8_t rmpp_version,
			    struct ibmad_port *srcport)
{
	int agent;

	if (!srcport)
		return -1;

	agent = mad_register_port_client(mad_rpc_portid(srcport), mgmt,
					 rmpp_version);
	if (agent < 0)
		return agent;

	srcport->class_agents[mgmt] = agent;
	return 0;
}

int mad_register_server(int mgmt, uint8_t rmpp_version,
			long method_mask[], uint32_t class_oui)
{
	return mad_register_server_via(mgmt, rmpp_version, method_mask,
				       class_oui, ibmp);
}

int mad_register_server_via(int mgmt, uint8_t rmpp_version,
			    long method_mask[], uint32_t class_oui,
			    struct ibmad_port *srcport)
{
	long class_method_mask[16 / sizeof(long)];
	uint8_t oui[3];
	int agent, vers;

	if (method_mask)
		memcpy(class_method_mask, method_mask,
		       sizeof class_method_mask);
	else
		memset(class_method_mask, 0xff, sizeof(class_method_mask));

	if (!srcport)
		return -1;

	if (srcport->class_agents[mgmt] >= 0) {
		DEBUG("Class 0x%x already registered %d",
		      mgmt, srcport->class_agents[mgmt]);
		return -1;
	}
	if ((vers = mgmt_class_vers(mgmt)) <= 0) {
		DEBUG("Unknown class 0x%x mgmt_class", mgmt);
		return -1;
	}
	if (mgmt >= IB_VENDOR_RANGE2_START_CLASS &&
	    mgmt <= IB_VENDOR_RANGE2_END_CLASS) {
		oui[0] = (class_oui >> 16) & 0xff;
		oui[1] = (class_oui >> 8) & 0xff;
		oui[2] = class_oui & 0xff;
		if ((agent =
		     umad_register_oui(srcport->port_id, mgmt, rmpp_version,
				       oui, class_method_mask)) < 0) {
			DEBUG("Can't register agent for class %d", mgmt);
			return -1;
		}
	} else
	    if ((agent =
		 umad_register(srcport->port_id, mgmt, vers, rmpp_version,
			       class_method_mask)) < 0) {
		DEBUG("Can't register agent for class %d", mgmt);
		return -1;
	}

	srcport->class_agents[mgmt] = agent;

	return agent;
}
