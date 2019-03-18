/*
 * This file is part of the Chelsio T4/T5/T6 Ethernet driver for Linux.
 *
 * Copyright (c) 2017 Chelsio Communications, Inc. All rights reserved.
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

#ifndef __T4_TCB_H
#define __T4_TCB_H

#define TCB_SMAC_SEL_W		0
#define TCB_SMAC_SEL_S		24
#define TCB_SMAC_SEL_M		0xffULL
#define TCB_SMAC_SEL_V(x)	((x) << TCB_SMAC_SEL_S)

#define TCB_T_FLAGS_W		1
#define TCB_T_FLAGS_S		0
#define TCB_T_FLAGS_M		0xffffffffffffffffULL
#define TCB_T_FLAGS_V(x)	((__u64)(x) << TCB_T_FLAGS_S)

#define TCB_RQ_START_W		30
#define TCB_RQ_START_S		0
#define TCB_RQ_START_M		0x3ffffffULL
#define TCB_RQ_START_V(x)	((x) << TCB_RQ_START_S)

#define TF_CCTRL_ECE_S		60
#define TF_CCTRL_CWR_S		61
#define TF_CCTRL_RFR_S		62

#define TCB_RSS_INFO_W		3
#define TCB_RSS_INFO_S		0
#define TCB_RSS_INFO_M		0x3ffULL
#define TCB_RSS_INFO_V(x)	((x) << TCB_RSS_INFO_S)

#define TCB_TIMESTAMP_W		5
#define TCB_TIMESTAMP_S		0
#define TCB_TIMESTAMP_M		0xffffffffULL
#define TCB_TIMESTAMP_V(x)	((x) << TCB_TIMESTAMP_S)

#define TCB_RTT_TS_RECENT_AGE_W		6
#define TCB_RTT_TS_RECENT_AGE_S		0
#define TCB_RTT_TS_RECENT_AGE_M		0xffffffffULL
#define TCB_RTT_TS_RECENT_AGE_V(x)	((x) << TCB_RTT_TS_RECENT_AGE_S)

#define TCB_SND_UNA_RAW_W	10
#define TCB_RX_FRAG2_PTR_RAW_W	27
#define TCB_RX_FRAG3_LEN_RAW_W	29
#define TCB_RX_FRAG3_START_IDX_OFFSET_RAW_W	30
#define TCB_PDU_HDR_LEN_W	31

#define TF_RX_PDU_OUT_S		49
#define TF_RX_PDU_OUT_V(x)	((__u64)(x) << TF_RX_PDU_OUT_S)

#endif /* __T4_TCB_H */
