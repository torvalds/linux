/*
 * Copyright (c) 2003-2007 Chelsio, Inc. All rights reserved.
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
 */
#ifndef __CHIOCTL_H__
#define __CHIOCTL_H__

/*
 * Ioctl commands specific to this driver.
 */
enum {
	CHELSIO_GETMTUTAB 		= 1029,
	CHELSIO_SETMTUTAB 		= 1030,
	CHELSIO_SET_PM 			= 1032,
	CHELSIO_GET_PM			= 1033,
	CHELSIO_GET_MEM			= 1038,
	CHELSIO_LOAD_FW			= 1041,
	CHELSIO_SET_TRACE_FILTER	= 1044,
	CHELSIO_SET_QSET_PARAMS		= 1045,
	CHELSIO_GET_QSET_PARAMS		= 1046,
	CHELSIO_SET_QSET_NUM		= 1047,
	CHELSIO_GET_QSET_NUM		= 1048,
};

struct ch_reg {
	uint32_t cmd;
	uint32_t addr;
	uint32_t val;
};

struct ch_cntxt {
	uint32_t cmd;
	uint32_t cntxt_type;
	uint32_t cntxt_id;
	uint32_t data[4];
};

/* context types */
enum { CNTXT_TYPE_EGRESS, CNTXT_TYPE_FL, CNTXT_TYPE_RSP, CNTXT_TYPE_CQ };

struct ch_desc {
	uint32_t cmd;
	uint32_t queue_num;
	uint32_t idx;
	uint32_t size;
	uint8_t data[128];
};

struct ch_mem_range {
	uint32_t cmd;
	uint32_t mem_id;
	uint32_t addr;
	uint32_t len;
	uint32_t version;
	uint8_t buf[0];
};

struct ch_qset_params {
	uint32_t cmd;
	uint32_t qset_idx;
	int32_t txq_size[3];
	int32_t rspq_size;
	int32_t fl_size[2];
	int32_t intr_lat;
	int32_t polling;
	int32_t lro;
	int32_t cong_thres;
	int32_t  vector;
	int32_t  qnum;
};

struct ch_pktsched_params {
	uint32_t cmd;
	uint8_t sched;
	uint8_t idx;
	uint8_t min;
	uint8_t max;
	uint8_t binding;
};

#ifndef TCB_SIZE
# define TCB_SIZE   128
#endif

/* TCB size in 32-bit words */
#define TCB_WORDS (TCB_SIZE / 4)

enum { MEM_CM, MEM_PMRX, MEM_PMTX };	/* ch_mem_range.mem_id values */

struct ch_mtus {
	uint32_t cmd;
	uint32_t nmtus;
	uint16_t mtus[NMTUS];
};

struct ch_pm {
	uint32_t cmd;
	uint32_t tx_pg_sz;
	uint32_t tx_num_pg;
	uint32_t rx_pg_sz;
	uint32_t rx_num_pg;
	uint32_t pm_total;
};

struct ch_tcam {
	uint32_t cmd;
	uint32_t tcam_size;
	uint32_t nservers;
	uint32_t nroutes;
	uint32_t nfilters;
};

struct ch_tcb {
	uint32_t cmd;
	uint32_t tcb_index;
	uint32_t tcb_data[TCB_WORDS];
};

struct ch_tcam_word {
	uint32_t cmd;
	uint32_t addr;
	uint32_t buf[3];
};

struct ch_trace {
	uint32_t cmd;
	uint32_t sip;
	uint32_t sip_mask;
	uint32_t dip;
	uint32_t dip_mask;
	uint16_t sport;
	uint16_t sport_mask;
	uint16_t dport;
	uint16_t dport_mask;
	uint32_t vlan:12;
	uint32_t vlan_mask:12;
	uint32_t intf:4;
	uint32_t intf_mask:4;
	uint8_t proto;
	uint8_t proto_mask;
	uint8_t invert_match:1;
	uint8_t config_tx:1;
	uint8_t config_rx:1;
	uint8_t trace_tx:1;
	uint8_t trace_rx:1;
};

#define SIOCCHIOCTL SIOCDEVPRIVATE

#endif
