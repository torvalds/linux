/* QLogic qed NIC Driver
 * Copyright (c) 2015-2017  QLogic Corporation
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
 *        disclaimer in the documentation and /or other materials
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
#ifndef _QED_PTP_H
#define _QED_PTP_H
#include <linux/types.h>

int qed_ptp_hwtstamp_tx_on(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);
int qed_ptp_cfg_rx_filters(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt,
			   enum qed_ptp_filter_type type);
int qed_ptp_read_rx_ts(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt, u64 *ts);
int qed_ptp_read_tx_ts(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt, u64 *ts);
int qed_ptp_read_cc(struct qed_hwfn *p_hwfn,
		    struct qed_ptt *p_ptt, u64 *cycles);
int qed_ptp_adjfreq(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt, s32 ppb);
int qed_ptp_disable(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);
int qed_ptp_enable(struct qed_hwfn *p_hwfn, struct qed_ptt *p_ptt);

#endif
