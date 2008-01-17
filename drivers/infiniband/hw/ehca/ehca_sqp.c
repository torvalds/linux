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


#include "ehca_classes.h"
#include "ehca_tools.h"
#include "ehca_iverbs.h"
#include "hcp_if.h"


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
				 "Can't define AQP1 for port %x. h_ret=%li",
				 port, ret);
			return ret;
		}
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
