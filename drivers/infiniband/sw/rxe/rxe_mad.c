// SPDX-License-Identifier: GPL-2.0 OR Linux-OpenIB
/*
 * Copyright (c) 2026 zhenwei pi <zhenwei.pi@linux.dev>
 */

#include <rdma/ib_pma.h>
#include "rxe.h"
#include "rxe_hw_counters.h"

static int rxe_get_pma_info(struct ib_mad *out)
{
	struct ib_class_port_info cpi = {};

	cpi.capability_mask = IB_PMA_CLASS_CAP_EXT_WIDTH;
	memcpy((out->data + 40), &cpi, sizeof(cpi));

	return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
}

static int rxe_get_pma_counters(struct rxe_dev *rxe, struct ib_mad *out)
{
	struct ib_pma_portcounters *pma_cnt = (struct ib_pma_portcounters *)(out->data + 40);
	s64 val;

	/* IBA release 1.8, 16.1.3.5: During operation, instead of overflowing, they shall stop
	 * at all ones.
	 */
	val = atomic64_read(&rxe->stats_counters[RXE_CNT_LINK_DOWNED]);
	pma_cnt->link_downed_counter = clamp(val, 0, U8_MAX);
	return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
}

static int rxe_get_pma_counters_ext(struct rxe_dev *rxe, struct ib_mad *out)
{
	struct ib_pma_portcounters_ext *pma_cnt_ext =
		(struct ib_pma_portcounters_ext *)(out->data + 40);
	s64 val;

	val = atomic64_read(&rxe->stats_counters[RXE_CNT_SENT_BYTES]);
	pma_cnt_ext->port_xmit_data = cpu_to_be64(val >> 2);

	val = atomic64_read(&rxe->stats_counters[RXE_CNT_RCVD_BYTES]);
	pma_cnt_ext->port_rcv_data = cpu_to_be64(val >> 2);

	val = atomic64_read(&rxe->stats_counters[RXE_CNT_SENT_PKTS]);
	pma_cnt_ext->port_xmit_packets = cpu_to_be64(val);

	val = atomic64_read(&rxe->stats_counters[RXE_CNT_RCVD_PKTS]);
	pma_cnt_ext->port_rcv_packets = cpu_to_be64(val);

	return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
}

static int rxe_get_perf_mgmt(struct rxe_dev *rxe, const struct ib_mad *in, struct ib_mad *out)
{
	switch (in->mad_hdr.attr_id) {
	case IB_PMA_CLASS_PORT_INFO:
		return rxe_get_pma_info(out);

	case IB_PMA_PORT_COUNTERS:
		return rxe_get_pma_counters(rxe, out);

	case IB_PMA_PORT_COUNTERS_EXT:
		return rxe_get_pma_counters_ext(rxe, out);

	default:
		out->mad_hdr.status = cpu_to_be16(IB_MGMT_MAD_STATUS_UNSUPPORTED_METHOD_ATTRIB);
		return IB_MAD_RESULT_SUCCESS;
	}
}

int rxe_process_mad(struct ib_device *ibdev, int mad_flags, u32 port_num,
		    const struct ib_wc *in_wc, const struct ib_grh *in_grh,
		    const struct ib_mad *in, struct ib_mad *out,
		    size_t *out_mad_size, u16 *out_mad_pkey_index)
{
	struct rxe_dev *rxe = to_rdev(ibdev);
	u8 mgmt_class = in->mad_hdr.mgmt_class;
	u8 method = in->mad_hdr.method;

	if (port_num != 1)
		return IB_MAD_RESULT_FAILURE;

	memset(out, 0, sizeof(*out));
	switch (mgmt_class) {
	case IB_MGMT_CLASS_PERF_MGMT:
		if (method == IB_MGMT_METHOD_GET)
			return rxe_get_perf_mgmt(rxe, in, out);
		break;

	default:
		out->mad_hdr.status = cpu_to_be16(IB_MGMT_MAD_STATUS_UNSUPPORTED_METHOD);
		return IB_MAD_RESULT_SUCCESS;
	}

	return IB_MAD_RESULT_SUCCESS | IB_MAD_RESULT_REPLY;
}
