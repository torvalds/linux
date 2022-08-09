// SPDX-License-Identifier: GPL-2.0 or BSD-3-Clause
/*
 * Copyright(c) 2016 Intel Corporation.
 */

#include <rdma/ib_mad.h>
#include "mad.h"
#include "vt.h"

/**
 * rvt_process_mad - process an incoming MAD packet
 * @ibdev: the infiniband device this packet came in on
 * @mad_flags: MAD flags
 * @port_num: the port number this packet came in on, 1 based from ib core
 * @in_wc: the work completion entry for this packet
 * @in_grh: the global route header for this packet
 * @in: the incoming MAD
 * @in_mad_size: size of the incoming MAD reply
 * @out: any outgoing MAD reply
 * @out_mad_size: size of the outgoing MAD reply
 * @out_mad_pkey_index: unused
 *
 * Note that the verbs framework has already done the MAD sanity checks,
 * and hop count/pointer updating for IB_MGMT_CLASS_SUBN_DIRECTED_ROUTE
 * MADs.
 *
 * This is called by the ib_mad module.
 *
 * Return: IB_MAD_RESULT_SUCCESS or error
 */
int rvt_process_mad(struct ib_device *ibdev, int mad_flags, u32 port_num,
		    const struct ib_wc *in_wc, const struct ib_grh *in_grh,
		    const struct ib_mad_hdr *in, size_t in_mad_size,
		    struct ib_mad_hdr *out, size_t *out_mad_size,
		    u16 *out_mad_pkey_index)
{
	/*
	 * MAD processing is quite different between hfi1 and qib. Therefore
	 * this is expected to be provided by the driver. Other drivers in the
	 * future may choose to implement this but it should not be made into a
	 * requirement.
	 */
	return IB_MAD_RESULT_FAILURE;
}

static void rvt_send_mad_handler(struct ib_mad_agent *agent,
				 struct ib_mad_send_wc *mad_send_wc)
{
	ib_free_send_mad(mad_send_wc->send_buf);
}

/**
 * rvt_create_mad_agents - create mad agents
 * @rdi: rvt dev struct
 *
 * If driver needs to be notified of mad agent creation then call back
 *
 * Return 0 on success
 */
int rvt_create_mad_agents(struct rvt_dev_info *rdi)
{
	struct ib_mad_agent *agent;
	struct rvt_ibport *rvp;
	int p;
	int ret;

	for (p = 0; p < rdi->dparms.nports; p++) {
		rvp = rdi->ports[p];
		agent = ib_register_mad_agent(&rdi->ibdev, p + 1,
					      IB_QPT_SMI,
					      NULL, 0, rvt_send_mad_handler,
					      NULL, NULL, 0);
		if (IS_ERR(agent)) {
			ret = PTR_ERR(agent);
			goto err;
		}

		rvp->send_agent = agent;

		if (rdi->driver_f.notify_create_mad_agent)
			rdi->driver_f.notify_create_mad_agent(rdi, p);
	}

	return 0;

err:
	for (p = 0; p < rdi->dparms.nports; p++) {
		rvp = rdi->ports[p];
		if (rvp->send_agent) {
			agent = rvp->send_agent;
			rvp->send_agent = NULL;
			ib_unregister_mad_agent(agent);
			if (rdi->driver_f.notify_free_mad_agent)
				rdi->driver_f.notify_free_mad_agent(rdi, p);
		}
	}

	return ret;
}

/**
 * rvt_free_mad_agents - free up mad agents
 * @rdi: rvt dev struct
 *
 * If driver needs notification of mad agent removal make the call back
 */
void rvt_free_mad_agents(struct rvt_dev_info *rdi)
{
	struct ib_mad_agent *agent;
	struct rvt_ibport *rvp;
	int p;

	for (p = 0; p < rdi->dparms.nports; p++) {
		rvp = rdi->ports[p];
		if (rvp->send_agent) {
			agent = rvp->send_agent;
			rvp->send_agent = NULL;
			ib_unregister_mad_agent(agent);
		}
		if (rvp->sm_ah) {
			rdma_destroy_ah(&rvp->sm_ah->ibah,
					RDMA_DESTROY_AH_SLEEPABLE);
			rvp->sm_ah = NULL;
		}

		if (rdi->driver_f.notify_free_mad_agent)
			rdi->driver_f.notify_free_mad_agent(rdi, p);
	}
}

