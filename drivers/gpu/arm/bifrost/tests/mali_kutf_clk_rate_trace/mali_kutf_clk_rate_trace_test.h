/* SPDX-License-Identifier: GPL-2.0 WITH Linux-syscall-note */
/*
 *
 * (C) COPYRIGHT 2020-2022 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU license.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, you can access it online at
 * http://www.gnu.org/licenses/gpl-2.0.html.
 *
 */

#ifndef _KUTF_CLK_RATE_TRACE_TEST_H_
#define _KUTF_CLK_RATE_TRACE_TEST_H_

#define CLK_RATE_TRACE_APP_NAME "clk_rate_trace"
#define CLK_RATE_TRACE_SUITE_NAME "rate_trace"
#define CLK_RATE_TRACE_PORTAL "portal"

/**
 * enum kbasep_clk_rate_trace_req - request command to the clock rate trace
 *                                  service portal.
 *
 * @PORTAL_CMD_GET_PLATFORM:       Request the platform that the tests are
 *                                 to be run on.
 * @PORTAL_CMD_GET_CLK_RATE_MGR:   Request the clock trace manager internal
 *                                 data record. On a positive acknowledgement
 *                                 the prevailing clock rates and the GPU idle
 *                                 condition flag are returned.
 * @PORTAL_CMD_GET_CLK_RATE_TRACE: Request the clock trace portal to return its
 *                                 data record. On a positive acknowledgement
 *                                 the last trace recorded clock rates and the
 *                                 GPU idle condition flag are returned.
 * @PORTAL_CMD_GET_TRACE_SNAPSHOT: Request the clock trace portal to return its
 *                                 current snapshot data record. On a positive
 *                                 acknowledgement the snapshot array matching
 *                                 the number of clocks are returned. It also
 *                                 starts a fresh snapshot inside the clock
 *                                 trace portal.
 * @PORTAL_CMD_INC_PM_CTX_CNT:     Request the clock trace portal to increase
 *                                 its internal PM_CTX_COUNT. If this increase
 *                                 yielded a count of 0 -> 1 change, the portal
 *                                 will initiate a PM_CTX_ACTIVE call to the
 *                                 Kbase power management. Futher increase
 *                                 requests will limit to only affect the
 *                                 portal internal count value.
 * @PORTAL_CMD_DEC_PM_CTX_CNT:     Request the clock trace portal to decrease
 *                                 its internal PM_CTX_COUNT. If this decrease
 *                                 yielded a count of 1 -> 0 change, the portal
 *                                 will initiate a PM_CTX_IDLE call to the
 *                                 Kbase power management.
 * @PORTAL_CMD_CLOSE_PORTAL:       Inform the clock trace portal service the
 *                                 client has completed its session. The portal
 *                                 will start the close down action. If no
 *                                 error has occurred during the dynamic
 *                                 interactive session, an inherent basic test
 *                                 carrying out some sanity check on the clock
 *                                 trace is undertaken.
 * @PORTAL_CMD_INVOKE_NOTIFY_42KHZ: Invokes all clock rate trace manager callbacks
 *                                 for the top clock domain with a new GPU frequency
 *                                 set to 42 kHZ.
 * @PORTAL_CMD_INVALID:            Valid commands termination marker. Must be
 *                                 the highest enumeration value, as it
 *                                 represents valid command array size.
 * @PORTAL_TOTAL_CMDS:             Alias of PORTAL_CMD_INVALID.
 */
/* PORTAL_CMD_INVALID must be the last one, serving the size */
enum kbasep_clk_rate_trace_req {
	PORTAL_CMD_GET_PLATFORM,
	PORTAL_CMD_GET_CLK_RATE_MGR,
	PORTAL_CMD_GET_CLK_RATE_TRACE,
	PORTAL_CMD_GET_TRACE_SNAPSHOT,
	PORTAL_CMD_INC_PM_CTX_CNT,
	PORTAL_CMD_DEC_PM_CTX_CNT,
	PORTAL_CMD_CLOSE_PORTAL,
	PORTAL_CMD_INVOKE_NOTIFY_42KHZ,
	PORTAL_CMD_INVALID,
	PORTAL_TOTAL_CMDS = PORTAL_CMD_INVALID,
};

/**
 * DOC: Portal service request command names.
 *
 * The portal request consists of a kutf named u64-value.
 * For those above enumerated PORTAL_CMD, the names defined
 * here are used to mark the name and then followed with a sequence number
 * value. Example (manual script here for illustration):
 *   exec 5<>run                   # open the portal kutf run as fd-5
 *   echo GET_CLK_RATE_MGR=1 >&5   # send the cmd and sequence number 1
 *   head -n 1 <&5                 # read back the 1-line server reseponse
 *     ACK="{SEQ:1, RATE:[0x1ad27480], GPU_IDLE:1}"   # response string
 *   echo GET_TRACE_SNAPSHOT=1 >&5 # send the cmd and sequence number 1
 *   head -n 1 <&5                 # read back the 1-line server reseponse
 *     ACK="{SEQ:1, SNAPSHOT_ARRAY:[(0x0, 0x1ad27480, 1, 0)]}"
 *   echo CLOSE_PORTAL=1 >&5       # close the portal
 *   cat <&5                       # read back all the response lines
 *     ACK="{SEQ:1, PM_CTX_CNT:0}"      # response to close command
 *     KUTF_RESULT_PASS:(explicit pass) # internal sanity test passed.
 *   exec 5>&-                     # close the service portal fd.
 *
 * Expected request command return format:
 *  GET_CLK_RATE_MGR:   ACK="{SEQ:12, RATE:[1080, 1280], GPU_IDLE:1}"
 *    Note, the above contains 2-clock with rates in [], GPU idle
 *  GET_CLK_RATE_TRACE: ACK="{SEQ:6, RATE:[0x1ad27480], GPU_IDLE:0}"
 *    Note, 1-clock with rate in [], GPU not idle
 *  GET_TRACE_SNAPSHOT: ACK="{SEQ:8, SNAPSHOT_ARRAY:[(0x0, 0x1ad27480, 1, 0)]}"
 *    Note, 1-clock, (start_rate : 0,  last_rate : 0x1ad27480,
 *                    trace_rate_up_count: 1, trace_rate_down_count : 0)
 *    For the specific sample case here, there is a single rate_trace event
 *    that yielded a rate increase change. No rate drop event recorded in the
 *    reporting snapshot duration.
 *  INC_PM_CTX_CNT:     ACK="{SEQ:1, PM_CTX_CNT:1}"
 *    Note, after the increment, M_CTX_CNT is 1. (i.e. 0 -> 1)
 *  DEC_PM_CTX_CNT:     ACK="{SEQ:3, PM_CTX_CNT:0}"
 *    Note, after the decrement, PM_CTX_CNT is 0. (i.e. 1 -> 0)
 *  CLOSE_PORTAL:       ACK="{SEQ:1, PM_CTX_CNT:1}"
 *    Note, at the close, PM_CTX_CNT is 1. The PM_CTX_CNT will internally be
 *    dropped down to 0 as part of the portal close clean up.
 */
#define GET_PLATFORM         "GET_PLATFORM"
#define GET_CLK_RATE_MGR     "GET_CLK_RATE_MGR"
#define GET_CLK_RATE_TRACE   "GET_CLK_RATE_TRACE"
#define GET_TRACE_SNAPSHOT   "GET_TRACE_SNAPSHOT"
#define INC_PM_CTX_CNT       "INC_PM_CTX_CNT"
#define DEC_PM_CTX_CNT       "DEC_PM_CTX_CNT"
#define CLOSE_PORTAL         "CLOSE_PORTAL"
#define INVOKE_NOTIFY_42KHZ  "INVOKE_NOTIFY_42KHZ"

/**
 * DOC: Portal service response tag names.
 *
 * The response consists of a kutf named string-value.
 * In case of a 'NACK' (negative acknowledgment), it can be one of the two formats:
 *   1. NACK="{SEQ:2, MSG:xyzed}"     # NACK on command with sequence tag-2.
 *      Note, the portal has received a valid name and valid sequence number
 *            but can't carry-out the request, reason in the MSG field.
 *   2. NACK="Failing-message"
 *      Note, unable to parse a valid name or valid sequence number,
 *            or some internal error condition. Reason in the quoted string.
 */
#define ACK "ACK"
#define NACK "NACK"
#define MAX_REPLY_NAME_LEN 32

#endif /* _KUTF_CLK_RATE_TRACE_TEST_H_ */
