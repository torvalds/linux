/*
 *  IBM eServer eHCA Infiniband device driver for Linux on POWER
 *
 *  SQP functions
 *
 *  Authors: Khadija Souissi <souissi@de.ibm.com>
 *           Heiko J Schick <schickhj@de.ibm.com>
 *
 *  Copyright (c) 2005 IBM Corporation
 *
 *  All rights reserved.
 *
 *  This source code is distributed under a dual license of GPL v2.0 and OpenIB
 *  BSD.
 *
 * OpenIB BSD License
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice, this
 * list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials
 * provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR
 * BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER
 * IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 */

#include <rdma/ib_mad.h>

#include "ehca_classes.h"
#include "ehca_tools.h"
#include "ehca_iverbs.h"
#include "hcp_if.h"

#define IB_MAD_STATUS_REDIRECT		cpu_to_be16(0x0002)
#define IB_MAD_STATUS_UNSUP_VERSION	cpu_to_be16(0x0004)
#define IB_MAD_STATUS_UNSUP_METHOD	cpu_to_be16(0x0008)

#define IB_PMA_CLASS_PORT_INFO		cpu_to_be16(0x0001)

/**
 * ehca_define_sqp - Defines special queue pair 1 (GSI QP). When special queue
 * pair is created successfully, the corresponding port gets active.
 *
 * Define Special Queue pair 0 (SMI QP) is still not supported.
 *
 * @qp_init_attr: Queue pair init attributes with port and queue pair type
 */

u64 ehca_define_sqp(struct ehca_shca *shca,
		    struct ehca_qp *ehca_qp,
		    struct ib_qp_init_attr *qp_init_attr)
{
	u32 pma_qp_nr, bma_qp_nr;
	u64 ret;
	u8 port = qp_init_attr->port_num;
	int counter;

	shca->sport[port - 1].port_state = IB_PORT_DOWN;

	switch (qp_init_attr->qp_type) {
	case IB_QPT_SMI:
		/* function not supported yet */
		break;
	case IB_QPT_GSI:
		ret = hipz_h_define_aqp1(shca->ipz_hca_handle,
					 ehca_qp->ipz_qp_handle,
					 ehca_qp->galpas.kernel,
					 (u32) qp_init_attr->port_num,
					 &pma_qp_nr, &bma_qp_nr);

		if (ret != H_SUCCESS) {
			ehca_err(&shca->ib_device,
				 "Can't define AQP1 for port %x. h_ret=%lli",
				 port, ret);
			return ret;
		}
		shca->sport[port - 1].pma_qp_nr = pma_qp_nr;
		ehca_dbg(&shca->ib_device, "port=%x pma_qp_nr=%x",
			 port, pma_qp_nr);
		break;
	default:
		ehca_err(&shca->ib_device, "invalid qp_type=%x",
			 qp_init_attr->qp_type);
		return H_PARAMETER;
	}

	if (ehca_nr_ports < 0) /* autodetect mode */
		return H_SUCCESS;

	for (counter = 0;
	     shca->sport[port - 1].port_state != IB_PORT_ACTIVE &&
		     counter < ehca_port_act_time;
	     counter++) {
		ehca_dbg(&shca->ib_device, "... wait until port %x is active",
			 port);
		msleep_interruptible(1000);
	}

	if (counter == ehca_port_act_time) {
		ehca_err(&shca->ib_device, "Port %x is not active.", port);
		return H_HARDWARE;
	}

	return H_SUCCESS;
}

struct ib_perf {
	struct ib_mad_hdr mad_hdr;
	u8 reserved[40];
	u8 data[192];
} __attribute__ ((packed));

/* TC/SL/FL packed into 32 bits, as in ClassPortInfo */
struct tcslfl {
	u32 tc:8;
	u32 sl:4;
	u32 fl:20;
} __attribute__ ((packed));

/* IP Version/TC/FL packed into 32 bits, as in GRH */
struct vertcfl {
	u32 ver:4;
	u32 tc:8;
	u32 fl:20;
} __attribute__ ((packed));

static int ehca_process_perf(struct ib_device *ibdev, u8 port_num,
			     struct ib_wc *in_wc, struct ib_grh *in_grh,
			     struct ib_mad *in_mad, struct ib_mad *out_mad)
{
	struct ib_perf *in_perf = (struct ib_perf *)in_mad;
	struct ib_perf *out_perf = (struct ib_perf *)out_mad;
	struct ib_class_port_info *poi =
		(struct ib_class_port_info *)out_perf->data;
	struct tcslfl *tcslfl =
		(struct tcslfl *)&poi->redirect_tcslfl;
	struct ehca_shca *shca =
		container_of(ibdev, struct ehca_shca, ib_device);
	struct ehca_sport *sport = &shca->sport[port_num - 1];

	ehca_dbg(ibdev, "method=%x", in_perf->mad_hdr.method);

	*out_mad = *in_mad;

	if (in_perf->mad_hdr.class_version != 1) {
		ehca_warn(ibdev, "Unsupported class_version=%x",
			  in_perf->mad_hdr.class_version);
		out_perf->mad_hdr.status = IB_MAD_STATUS_UNSUP_VERSION;
		goto perf_reply;
	}

	switch (in_perf->mad_hdr.method) {
	case IB_MGMT_METHOD_GET:
	case IB_MGMT_METHOD_SET:
		/* set class port info for redirection */
		out_perf->mad_hdr.attr_id = IB_PMA_CLASS_PORT_INFO;
		out_perf->mad_hdr.status = IB_MAD_STATUS_REDIRECT;
		memset(poi, 0, sizeof(*poi));
		poi->base_version = 1;
		poi->class_version = 1;
		poi->resp_time_value = 18;

		/* copy local routing information from WC where applicable */
		tcslfl->sl         = in_wc->sl;
		poi->redirect_lid  =
			sport->saved_attr.lid | in_wc->dlid_path_bits;
		poi->redirect_qp   = sport->pma_qp_nr;
		poi->redirect_qkey = IB_QP1_QKEY;

		ehca_query_pkey(ibdev, port_num, in_wc->pkey_index,
				&poi->redirect_pkey);

		/* if request was globally routed, copy route info */
		if (in_grh) {
			struct vertcfl *vertcfl =
				(struct vertcfl *)&in_grh->version_tclass_flow;
			memcpy(poi->redirect_gid, in_grh->dgid.raw,
			       sizeof(poi->redirect_gid));
			tcslfl->tc        = vertcfl->tc;
			tcslfl->fl        = vertcfl->fl;
		} else
			/* else only fill in default GID */
			ehca_query_gid(ibdev, port_num, 0,
				       (union ib_gid *)&poi->redirect_gid);

		ehca_dbg(ibdev, "ehca_pma_lid=%x ehca_pma_qp=%x",
			 sport->saved_attr.lid, sport->pma_qp_nr);
		break;

	case IB_MGMT_METHOD_GET_RESP:
		return IB_MAD_RESULT_FAILURE;

	default:
		out_perf->mad_hdr.status = IB_MAD_STATUS_UNSUP_METHOD;
		break;
	}

perf_reply:
	out_perf->mad_hdr.method = IB_MGMT_METHOD_GET_RESP;

	return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
}

int ehca_process_mad(struct ib_device *ibdev, int mad_flags, u8 port_num,
		     struct ib_wc *in_wc, struct ib_grh *in_grh,
		     struct ib_mad *in_mad, struct ib_mad *out_mad)
{
	int ret;

	if (!port_num || port_num > ibdev->phys_port_cnt)
		return IB_MAD_RESULT_FAILURE;

	/* accept only pma request */
	if (in_mad->mad_hdr.mgmt_class != IB_MGMT_CLASS_PERF_MGMT)
		return IB_MAD_RESULT_SUCCESS;

	ehca_dbg(ibdev, "port_num=%x src_qp=%x", port_num, in_wc->src_qp);
	ret = ehca_process_perf(ibdev, port_num, in_wc, in_grh,
				in_mad, out_mad);

	return ret;
}
