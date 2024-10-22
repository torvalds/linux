/* SPDX-License-Identifier: GPL-2.0 */
/* $Date: 2005/03/07 23:59:05 $ $RCSfile: tp.h,v $ $Revision: 1.20 $ */
#ifndef CHELSIO_TP_H
#define CHELSIO_TP_H

#include "common.h"

#define TP_MAX_RX_COALESCING_SIZE 16224U

struct tp_mib_statistics {

	/* IP */
	u32 ipInReceive_hi;
	u32 ipInReceive_lo;
	u32 ipInHdrErrors_hi;
	u32 ipInHdrErrors_lo;
	u32 ipInAddrErrors_hi;
	u32 ipInAddrErrors_lo;
	u32 ipInUnknownProtos_hi;
	u32 ipInUnknownProtos_lo;
	u32 ipInDiscards_hi;
	u32 ipInDiscards_lo;
	u32 ipInDelivers_hi;
	u32 ipInDelivers_lo;
	u32 ipOutRequests_hi;
	u32 ipOutRequests_lo;
	u32 ipOutDiscards_hi;
	u32 ipOutDiscards_lo;
	u32 ipOutNoRoutes_hi;
	u32 ipOutNoRoutes_lo;
	u32 ipReasmTimeout;
	u32 ipReasmReqds;
	u32 ipReasmOKs;
	u32 ipReasmFails;

	u32 reserved[8];

	/* TCP */
	u32 tcpActiveOpens;
	u32 tcpPassiveOpens;
	u32 tcpAttemptFails;
	u32 tcpEstabResets;
	u32 tcpOutRsts;
	u32 tcpCurrEstab;
	u32 tcpInSegs_hi;
	u32 tcpInSegs_lo;
	u32 tcpOutSegs_hi;
	u32 tcpOutSegs_lo;
	u32 tcpRetransSeg_hi;
	u32 tcpRetransSeg_lo;
	u32 tcpInErrs_hi;
	u32 tcpInErrs_lo;
	u32 tcpRtoMin;
	u32 tcpRtoMax;
};

struct petp;
struct tp_params;

struct petp *t1_tp_create(adapter_t *adapter, struct tp_params *p);
void t1_tp_destroy(struct petp *tp);

void t1_tp_intr_disable(struct petp *tp);
void t1_tp_intr_enable(struct petp *tp);
void t1_tp_intr_clear(struct petp *tp);
int t1_tp_intr_handler(struct petp *tp);

void t1_tp_set_tcp_checksum_offload(struct petp *tp, int enable);
void t1_tp_set_ip_checksum_offload(struct petp *tp, int enable);
int t1_tp_reset(struct petp *tp, struct tp_params *p, unsigned int tp_clk);
#endif
