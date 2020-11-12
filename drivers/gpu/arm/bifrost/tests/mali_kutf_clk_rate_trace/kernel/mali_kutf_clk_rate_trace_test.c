/*
 *
 * (C) COPYRIGHT 2020 ARM Limited. All rights reserved.
 *
 * This program is free software and is provided to you under the terms of the
 * GNU General Public License version 2 as published by the Free Software
 * Foundation, and any use by you of this program is subject to the terms
 * of such GNU licence.
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
 * SPDX-License-Identifier: GPL-2.0
 *
 */

#include <linux/fdtable.h>
#include <linux/module.h>

#include <linux/delay.h>
#include <linux/mutex.h>
#include <linux/ktime.h>
#include <linux/version.h>
#if (KERNEL_VERSION(4, 11, 0) <= LINUX_VERSION_CODE)
#include <linux/sched/task.h>
#else
#include <linux/sched.h>
#endif
#include "mali_kbase.h"
#include "mali_kbase_irq_internal.h"
#include "mali_kbase_pm_internal.h"
#include "mali_kbase_clk_rate_trace_mgr.h"

#include <kutf/kutf_suite.h>
#include <kutf/kutf_utils.h>
#include <kutf/kutf_helpers.h>
#include <kutf/kutf_helpers_user.h>

#include "../mali_kutf_clk_rate_trace_test.h"

#define MINOR_FOR_FIRST_KBASE_DEV	(-1)

/* KUTF test application pointer for this test */
struct kutf_application *kutf_app;

enum portal_server_state {
	PORTAL_STATE_NO_CLK,
	PORTAL_STATE_LIVE,
	PORTAL_STATE_CLOSING,
};

/**
 * struct clk_trace_snapshot - Trace info data on a clock.
 * @previous_rate:   Snapshot start point clock rate.
 * @current_rate:    End point clock rate. It becomes the start rate of the
 *                   next trace snapshot.
 * @rate_up_cnt:     Count in the snapshot duration when the clock trace
 *                   write is a rate of higher value than the last.
 * @rate_down_cnt:   Count in the snapshot duration when the clock trace write
 *                   is a rate of lower value than the last.
 */
struct clk_trace_snapshot {
	unsigned long previous_rate;
	unsigned long current_rate;
	u32 rate_up_cnt;
	u32 rate_down_cnt;
};

/**
 * struct kutf_clk_rate_trace_fixture_data - Fixture data for the test.
 * @kbdev:            kbase device for the GPU.
 * @listener:         Clock rate change listener structure.
 * @invoke_notify:    When true, invoke notify command is being executed.
 * @snapshot:         Clock trace update snapshot data array. A snapshot
 *                    for each clock contains info accumulated beteen two
 *                    GET_TRACE_SNAPSHOT requests.
 * @nclks:            Number of clocks visible to the trace portal.
 * @pm_ctx_cnt:       Net count of PM (Power Management) context INC/DEC
 *                    PM_CTX_CNT requests made to the portal. On change from
 *                    0 to 1 (INC), or, 1 to 0 (DEC), a PM context action is
 *                    triggered.
 * @total_update_cnt: Total number of received trace write callbacks.
 * @server_state:     Portal server operational state.
 * @result_msg:       Message for the test result.
 * @test_status:      Portal test reslt status.
 */
struct kutf_clk_rate_trace_fixture_data {
	struct kbase_device *kbdev;
	struct kbase_clk_rate_listener listener;
	bool invoke_notify;
	struct clk_trace_snapshot snapshot[BASE_MAX_NR_CLOCKS_REGULATORS];
	unsigned int nclks;
	unsigned int pm_ctx_cnt;
	unsigned int total_update_cnt;
	enum portal_server_state server_state;
	char const *result_msg;
	enum kutf_result_status test_status;
};

struct clk_trace_portal_input {
	struct kutf_helper_named_val cmd_input;
	enum kbasep_clk_rate_trace_req portal_cmd;
	int named_val_err;
};

struct kbasep_cmd_name_pair {
	enum kbasep_clk_rate_trace_req cmd;
	const char *name;
};

struct kbasep_cmd_name_pair kbasep_portal_cmd_name_map[] = {
			{PORTAL_CMD_GET_CLK_RATE_MGR, GET_CLK_RATE_MGR},
			{PORTAL_CMD_GET_CLK_RATE_TRACE, GET_CLK_RATE_TRACE},
			{PORTAL_CMD_GET_TRACE_SNAPSHOT, GET_TRACE_SNAPSHOT},
			{PORTAL_CMD_INC_PM_CTX_CNT, INC_PM_CTX_CNT},
			{PORTAL_CMD_DEC_PM_CTX_CNT, DEC_PM_CTX_CNT},
			{PORTAL_CMD_CLOSE_PORTAL, CLOSE_PORTAL},
			{PORTAL_CMD_INVOKE_NOTIFY_42KHZ, INVOKE_NOTIFY_42KHZ},
		};

/* Global pointer for the kutf_portal_trace_write() to use. When
 * this pointer is engaged, new requests for create fixture will fail
 * hence limiting the use of the portal at any time to a singleton.
 */
struct kutf_clk_rate_trace_fixture_data *g_ptr_portal_data;

#define PORTAL_MSG_LEN (KUTF_MAX_LINE_LENGTH - MAX_REPLY_NAME_LEN)
static char portal_msg_buf[PORTAL_MSG_LEN];

static void kutf_portal_trace_write(
	struct kbase_clk_rate_listener *listener,
	u32 index, u32 new_rate)
{
	struct clk_trace_snapshot *snapshot;
	struct kutf_clk_rate_trace_fixture_data *data = container_of(
		listener, struct kutf_clk_rate_trace_fixture_data, listener);

	lockdep_assert_held(&data->kbdev->pm.clk_rtm.lock);

	if (WARN_ON(g_ptr_portal_data == NULL))
		return;
	if (WARN_ON(index >= g_ptr_portal_data->nclks))
		return;

	/* This callback is triggered by invoke notify command, skipping */
	if (data->invoke_notify)
		return;

	snapshot = &g_ptr_portal_data->snapshot[index];
	if (new_rate > snapshot->current_rate)
		snapshot->rate_up_cnt++;
	else
		snapshot->rate_down_cnt++;
	snapshot->current_rate = new_rate;
	g_ptr_portal_data->total_update_cnt++;
}

static void kutf_set_pm_ctx_active(struct kutf_context *context)
{
	struct kutf_clk_rate_trace_fixture_data *data = context->fixture;

	if (WARN_ON(data->pm_ctx_cnt != 1))
		return;

	kbase_pm_context_active(data->kbdev);
	kbase_pm_wait_for_desired_state(data->kbdev);
#if !MALI_USE_CSF
	kbase_pm_request_gpu_cycle_counter(data->kbdev);
#endif
}

static void kutf_set_pm_ctx_idle(struct kutf_context *context)
{
	struct kutf_clk_rate_trace_fixture_data *data = context->fixture;

	if (WARN_ON(data->pm_ctx_cnt > 0))
		return;

	kbase_pm_context_idle(data->kbdev);
#if !MALI_USE_CSF
	kbase_pm_release_gpu_cycle_counter(data->kbdev);
#endif
}

static char const *kutf_clk_trace_do_change_pm_ctx(struct kutf_context *context,
				struct clk_trace_portal_input *cmd)
{
	struct kutf_clk_rate_trace_fixture_data *data = context->fixture;
	int seq = cmd->cmd_input.u.val_u64 & 0xFF;
	const unsigned int cnt = data->pm_ctx_cnt;
	const enum kbasep_clk_rate_trace_req req = cmd->portal_cmd;
	char const *errmsg = NULL;

	WARN_ON(req != PORTAL_CMD_INC_PM_CTX_CNT &&
		req != PORTAL_CMD_DEC_PM_CTX_CNT);

	if (req == PORTAL_CMD_INC_PM_CTX_CNT && cnt < UINT_MAX) {
		data->pm_ctx_cnt++;
		if (data->pm_ctx_cnt == 1)
			kutf_set_pm_ctx_active(context);
	}

	if (req == PORTAL_CMD_DEC_PM_CTX_CNT && cnt > 0) {
		data->pm_ctx_cnt--;
		if (data->pm_ctx_cnt == 0)
			kutf_set_pm_ctx_idle(context);
	}

	/* Skip the length check, no chance of overflow for two ints */
	snprintf(portal_msg_buf, PORTAL_MSG_LEN,
			"{SEQ:%d, PM_CTX_CNT:%u}", seq, data->pm_ctx_cnt);

	if (kutf_helper_send_named_str(context, "ACK", portal_msg_buf)) {
		pr_warn("Error in sending ack for adjusting pm_ctx_cnt\n");
		errmsg = kutf_dsprintf(&context->fixture_pool,
				"Error in sending ack for adjusting pm_ctx_cnt");
	}

	return errmsg;
}

static char const *kutf_clk_trace_do_get_rate(struct kutf_context *context,
				struct clk_trace_portal_input *cmd)
{
	struct kutf_clk_rate_trace_fixture_data *data = context->fixture;
	struct kbase_device *kbdev = data->kbdev;
	int seq = cmd->cmd_input.u.val_u64 & 0xFF;
	unsigned long rate;
	bool idle;
	int ret;
	int i;
	char const *errmsg = NULL;

	WARN_ON((cmd->portal_cmd != PORTAL_CMD_GET_CLK_RATE_MGR) &&
		(cmd->portal_cmd != PORTAL_CMD_GET_CLK_RATE_TRACE));

	ret = snprintf(portal_msg_buf, PORTAL_MSG_LEN,
			"{SEQ:%d, RATE:[", seq);

	for (i = 0; i < data->nclks; i++) {
		spin_lock(&kbdev->pm.clk_rtm.lock);
		if (cmd->portal_cmd == PORTAL_CMD_GET_CLK_RATE_MGR)
			rate = kbdev->pm.clk_rtm.clks[i]->clock_val;
		else
			rate = data->snapshot[i].current_rate;
		idle = kbdev->pm.clk_rtm.gpu_idle;
		spin_unlock(&kbdev->pm.clk_rtm.lock);

		if ((i + 1) == data->nclks)
			ret += snprintf(portal_msg_buf + ret,
				PORTAL_MSG_LEN - ret, "0x%lx], GPU_IDLE:%d}",
				rate, idle);
		else
			ret += snprintf(portal_msg_buf + ret,
				PORTAL_MSG_LEN - ret, "0x%lx, ", rate);

		if (ret >= PORTAL_MSG_LEN) {
			pr_warn("Message buf overflow with rate array data\n");
			return kutf_dsprintf(&context->fixture_pool,
						"Message buf overflow with rate array data");
		}
	}

	if (kutf_helper_send_named_str(context, "ACK", portal_msg_buf)) {
		pr_warn("Error in sending back rate array\n");
		errmsg = kutf_dsprintf(&context->fixture_pool,
				"Error in sending rate array");
	}

	return errmsg;
}

/**
 * kutf_clk_trace_do_get_snapshot() - Send back the current snapshot
 * @context:  KUTF context
 * @cmd:      The decoded portal input request
 *
 * The accumulated clock rate trace information is kept inside as an snapshot
 * record. A user request of getting the snapshot marks the closure of the
 * current snapshot record, and the start of the next one. The response
 * message contains the current snapshot record, with each clock's
 * data sequentially placed inside (array marker) [ ].
 */
static char const *kutf_clk_trace_do_get_snapshot(struct kutf_context *context,
				struct clk_trace_portal_input *cmd)
{
	struct kutf_clk_rate_trace_fixture_data *data = context->fixture;
	struct clk_trace_snapshot snapshot;
	int seq = cmd->cmd_input.u.val_u64 & 0xFF;
	int ret;
	int i;
	char const *fmt;
	char const *errmsg = NULL;

	WARN_ON(cmd->portal_cmd != PORTAL_CMD_GET_TRACE_SNAPSHOT);

	ret = snprintf(portal_msg_buf, PORTAL_MSG_LEN,
			"{SEQ:%d, SNAPSHOT_ARRAY:[", seq);

	for (i = 0; i < data->nclks; i++) {
		spin_lock(&data->kbdev->pm.clk_rtm.lock);
		/* copy out the snapshot of the clock */
		snapshot = data->snapshot[i];
		/* Set the next snapshot start condition */
		data->snapshot[i].previous_rate = snapshot.current_rate;
		data->snapshot[i].rate_up_cnt = 0;
		data->snapshot[i].rate_down_cnt = 0;
		spin_unlock(&data->kbdev->pm.clk_rtm.lock);

		/* Check i corresponding to the last clock */
		if ((i + 1) == data->nclks)
			fmt = "(0x%lx, 0x%lx, %u, %u)]}";
		else
			fmt = "(0x%lx, 0x%lx, %u, %u), ";
		ret += snprintf(portal_msg_buf + ret, PORTAL_MSG_LEN - ret,
			    fmt, snapshot.previous_rate, snapshot.current_rate,
			    snapshot.rate_up_cnt, snapshot.rate_down_cnt);
		if (ret >= PORTAL_MSG_LEN) {
			pr_warn("Message buf overflow with snapshot data\n");
			return kutf_dsprintf(&context->fixture_pool,
					"Message buf overflow with snapshot data");
		}
	}

	if (kutf_helper_send_named_str(context, "ACK", portal_msg_buf)) {
		pr_warn("Error in sending back snapshot array\n");
		errmsg = kutf_dsprintf(&context->fixture_pool,
				"Error in sending snapshot array");
	}

	return errmsg;
}

/**
 * kutf_clk_trace_do_invoke_notify_42k() - Invokes the stored notification callback
 * @context:  KUTF context
 * @cmd:      The decoded portal input request
 *
 * Invokes frequency change notification callbacks with a fake
 * GPU frequency 42 kHz for the top clock domain.
 */
static char const *kutf_clk_trace_do_invoke_notify_42k(
	struct kutf_context *context,
	struct clk_trace_portal_input *cmd)
{
	struct kutf_clk_rate_trace_fixture_data *data = context->fixture;
	int seq = cmd->cmd_input.u.val_u64 & 0xFF;
	const unsigned long new_rate_hz = 42000;
	int ret;
	char const *errmsg = NULL;
	struct kbase_clk_rate_trace_manager *clk_rtm = &data->kbdev->pm.clk_rtm;

	WARN_ON(cmd->portal_cmd != PORTAL_CMD_INVOKE_NOTIFY_42KHZ);

	spin_lock(&clk_rtm->lock);

	data->invoke_notify = true;
	kbase_clk_rate_trace_manager_notify_all(
		clk_rtm, 0, new_rate_hz);
	data->invoke_notify = false;

	spin_unlock(&clk_rtm->lock);

	ret = snprintf(portal_msg_buf, PORTAL_MSG_LEN,
		       "{SEQ:%d, HZ:%lu}", seq, new_rate_hz);

	if (ret >= PORTAL_MSG_LEN) {
		pr_warn("Message buf overflow with invoked data\n");
		return kutf_dsprintf(&context->fixture_pool,
				"Message buf overflow with invoked data");
	}

	if (kutf_helper_send_named_str(context, "ACK", portal_msg_buf)) {
		pr_warn("Error in sending ack for " INVOKE_NOTIFY_42KHZ "request\n");
		errmsg = kutf_dsprintf(&context->fixture_pool,
			"Error in sending ack for " INVOKE_NOTIFY_42KHZ "request");
	}

	return errmsg;
}

static char const *kutf_clk_trace_do_close_portal(struct kutf_context *context,
				struct clk_trace_portal_input *cmd)
{
	struct kutf_clk_rate_trace_fixture_data *data = context->fixture;
	int seq = cmd->cmd_input.u.val_u64 & 0xFF;
	char const *errmsg = NULL;

	WARN_ON(cmd->portal_cmd != PORTAL_CMD_CLOSE_PORTAL);

	data->server_state = PORTAL_STATE_CLOSING;

	/* Skip the length check, no chance of overflow for two ints */
	snprintf(portal_msg_buf, PORTAL_MSG_LEN,
			"{SEQ:%d, PM_CTX_CNT:%u}", seq, data->pm_ctx_cnt);

	if (kutf_helper_send_named_str(context, "ACK", portal_msg_buf)) {
		pr_warn("Error in sending ack for " CLOSE_PORTAL "reuquest\n");
		errmsg = kutf_dsprintf(&context->fixture_pool,
			"Error in sending ack for " CLOSE_PORTAL "reuquest");
	}

	return errmsg;
}

static bool kutf_clk_trace_dequeue_portal_cmd(struct kutf_context *context,
				struct clk_trace_portal_input *cmd)
{
	int i;
	int err = kutf_helper_receive_named_val(context, &cmd->cmd_input);

	cmd->named_val_err = err;
	if (err == KUTF_HELPER_ERR_NONE &&
		cmd->cmd_input.type == KUTF_HELPER_VALTYPE_U64) {
		/* All portal request commands are of format (named u64):
		 *   CMD_NAME=1234
		 * where, 1234 is a (variable) sequence number tag.
		 */
		for (i = 0; i < PORTAL_TOTAL_CMDS; i++) {
			if (strcmp(cmd->cmd_input.val_name,
				kbasep_portal_cmd_name_map[i].name))
				continue;

			cmd->portal_cmd = kbasep_portal_cmd_name_map[i].cmd;
			return true;
		}
	}

	cmd->portal_cmd = PORTAL_CMD_INVALID;
	return false;
}

static void kutf_clk_trace_flag_result(struct kutf_context *context,
			enum kutf_result_status result, char const *msg)
{
	struct kutf_clk_rate_trace_fixture_data *data = context->fixture;

	if (result > data->test_status) {
		data->test_status = result;
		if (msg)
			data->result_msg = msg;
		if (data->server_state == PORTAL_STATE_LIVE &&
			result > KUTF_RESULT_WARN) {
			data->server_state = PORTAL_STATE_CLOSING;
		}
	}
}

static bool kutf_clk_trace_process_portal_cmd(struct kutf_context *context,
				struct clk_trace_portal_input *cmd)
{
	char const *errmsg = NULL;

	BUILD_BUG_ON(ARRAY_SIZE(kbasep_portal_cmd_name_map) !=
				PORTAL_TOTAL_CMDS);
	WARN_ON(cmd->portal_cmd == PORTAL_CMD_INVALID);

	switch (cmd->portal_cmd) {
	case PORTAL_CMD_GET_CLK_RATE_MGR:
		/* Fall through */
	case PORTAL_CMD_GET_CLK_RATE_TRACE:
		errmsg = kutf_clk_trace_do_get_rate(context, cmd);
		break;
	case PORTAL_CMD_GET_TRACE_SNAPSHOT:
		errmsg = kutf_clk_trace_do_get_snapshot(context, cmd);
		break;
	case PORTAL_CMD_INC_PM_CTX_CNT:
		/* Fall through */
	case PORTAL_CMD_DEC_PM_CTX_CNT:
		errmsg = kutf_clk_trace_do_change_pm_ctx(context, cmd);
		break;
	case PORTAL_CMD_CLOSE_PORTAL:
		errmsg = kutf_clk_trace_do_close_portal(context, cmd);
		break;
	case PORTAL_CMD_INVOKE_NOTIFY_42KHZ:
		errmsg = kutf_clk_trace_do_invoke_notify_42k(context, cmd);
		break;
	default:
		pr_warn("Don't know how to handle portal_cmd: %d, abort session.\n",
				cmd->portal_cmd);
		errmsg = kutf_dsprintf(&context->fixture_pool,
				"Don't know how to handle portal_cmd: %d",
				cmd->portal_cmd);
		break;
	}

	if (errmsg)
		kutf_clk_trace_flag_result(context, KUTF_RESULT_FAIL, errmsg);

	return (errmsg == NULL);
}

/**
 * kutf_clk_trace_do_nack_response() - respond a NACK to erroneous input
 * @context:  KUTF context
 * @cmd:      The erroneous input request
 *
 * This function deal with an erroneous input request, and respond with
 * a proper 'NACK' message.
 */
static int kutf_clk_trace_do_nack_response(struct kutf_context *context,
				struct clk_trace_portal_input *cmd)
{
	int seq;
	int err;
	char const *errmsg = NULL;

	WARN_ON(cmd->portal_cmd != PORTAL_CMD_INVALID);

	if (cmd->named_val_err == KUTF_HELPER_ERR_NONE &&
			  cmd->cmd_input.type == KUTF_HELPER_VALTYPE_U64) {
		/* Keep seq number as % 256 */
		seq = cmd->cmd_input.u.val_u64 & 255;
		snprintf(portal_msg_buf, PORTAL_MSG_LEN,
				 "{SEQ:%d, MSG: Unknown command '%s'.}", seq,
				 cmd->cmd_input.val_name);
		err = kutf_helper_send_named_str(context, "NACK",
						portal_msg_buf);
	} else
		err = kutf_helper_send_named_str(context, "NACK",
			"Wrong portal cmd format (Ref example: CMD_NAME=0X16)");

	if (err) {
		errmsg = kutf_dsprintf(&context->fixture_pool,
						"Failed to send portal NACK response");
		kutf_clk_trace_flag_result(context, KUTF_RESULT_FAIL, errmsg);
	}

	return err;
}

/**
 * kutf_clk_trace_barebone_check() - Sanity test on the clock tracing
 * @context:	KUTF context
 *
 * This function carries out some basic test on the tracing operation:
 *     1). GPU idle on test start, trace rate should be 0 (low power state)
 *     2). Make sure GPU is powered up, the trace rate should match
 *         that from the clcok manager's internal recorded rate
 *     3). If the GPU active transition occurs following 2), there
 *         must be rate change event from tracing.
 */
void kutf_clk_trace_barebone_check(struct kutf_context *context)
{
	struct kutf_clk_rate_trace_fixture_data *data = context->fixture;
	struct kbase_device *kbdev = data->kbdev;
	bool fail = false;
	bool idle[2] = { false };
	char const *msg = NULL;
	int i;

	/* Check consistency if gpu happens to be idle */
	spin_lock(&kbdev->pm.clk_rtm.lock);
	idle[0] = kbdev->pm.clk_rtm.gpu_idle;
	if (kbdev->pm.clk_rtm.gpu_idle) {
		for (i = 0; i < data->nclks; i++) {
			if (data->snapshot[i].current_rate) {
				/* Idle should have a rate 0 */
				fail = true;
				break;
			}
		}
	}
	spin_unlock(&kbdev->pm.clk_rtm.lock);
	if (fail) {
		msg = kutf_dsprintf(&context->fixture_pool,
				"GPU Idle not yielding 0-rate");
		pr_err("Trace did not see idle rate\n");
	} else {
		/* Make local PM active if not done so yet */
		if (data->pm_ctx_cnt == 0) {
			/* Ensure the GPU is powered */
			data->pm_ctx_cnt++;
			kutf_set_pm_ctx_active(context);
		}
		/* Checking the rate is consistent */
		spin_lock(&kbdev->pm.clk_rtm.lock);
		idle[1] = kbdev->pm.clk_rtm.gpu_idle;
		for (i = 0; i < data->nclks; i++) {
			/* Rate match between the manager and the trace */
			if (kbdev->pm.clk_rtm.clks[i]->clock_val !=
				data->snapshot[i].current_rate) {
				fail = true;
				break;
			}
		}
		spin_unlock(&kbdev->pm.clk_rtm.lock);

		if (idle[1]) {
			msg = kutf_dsprintf(&context->fixture_pool,
				"GPU still idle after set_pm_ctx_active");
			pr_err("GPU still idle after set_pm_ctx_active\n");
		}

		if (!msg && fail) {
			msg = kutf_dsprintf(&context->fixture_pool,
				"Trace rate not matching Clk manager's read");
			pr_err("Trace rate not matching Clk manager's read\n");
		}
	}

	if (!msg && idle[0] && !idle[1] && !data->total_update_cnt) {
		msg = kutf_dsprintf(&context->fixture_pool,
				"Trace update did not occur");
		pr_err("Trace update did not occur\n");
	}
	if (msg)
		kutf_clk_trace_flag_result(context, KUTF_RESULT_FAIL, msg);
	else if (!data->total_update_cnt) {
		msg = kutf_dsprintf(&context->fixture_pool,
				"No trace update seen during the test!");
		kutf_clk_trace_flag_result(context, KUTF_RESULT_WARN, msg);
	}
}

static bool kutf_clk_trace_end_of_stream(struct clk_trace_portal_input *cmd)
{
	return (cmd->named_val_err == -EBUSY);
}

void kutf_clk_trace_no_clks_dummy(struct kutf_context *context)
{
	struct clk_trace_portal_input cmd;
	unsigned long timeout = jiffies + HZ * 2;
	bool has_cmd;

	while (time_before(jiffies, timeout)) {
		if (kutf_helper_pending_input(context)) {
			has_cmd = kutf_clk_trace_dequeue_portal_cmd(context,
									&cmd);
			if (!has_cmd && kutf_clk_trace_end_of_stream(&cmd))
				break;

			kutf_helper_send_named_str(context, "NACK",
				"Fatal! No clocks visible, aborting");
		}
		msleep(20);
	}

	kutf_clk_trace_flag_result(context, KUTF_RESULT_FATAL,
				"No clocks visble to the portal");
}

/**
 * mali_kutf_clk_rate_trace_test_portal() - Service portal input
 * @context:	KUTF context
 *
 * The test portal operates on input requests. If the input request is one
 * of the recognized portal commands, it handles it accordingly. Otherwise
 * a negative response 'NACK' is returned. The portal service terminates
 * when a 'CLOSE_PORTAL' request is received, or due to an internal error.
 * Both case would result in the server_state transitioned to CLOSING.
 *
 * If the portal is closed on request, a sanity test on the clock rate
 * trace operation is undertaken via function:
 *    kutf_clk_trace_barebone_check();
 */
static void mali_kutf_clk_rate_trace_test_portal(struct kutf_context *context)
{
	struct kutf_clk_rate_trace_fixture_data *data = context->fixture;
	struct clk_trace_portal_input new_cmd;

	pr_debug("Test portal service start\n");

	while (data->server_state == PORTAL_STATE_LIVE) {
		if (kutf_clk_trace_dequeue_portal_cmd(context, &new_cmd))
			kutf_clk_trace_process_portal_cmd(context, &new_cmd);
		else if (kutf_clk_trace_end_of_stream(&new_cmd))
			/* Dequeue on portal input, end of stream */
			data->server_state = PORTAL_STATE_CLOSING;
		else
			kutf_clk_trace_do_nack_response(context, &new_cmd);
	}

	/* Closing, exhausting all the pending inputs with NACKs. */
	if (data->server_state == PORTAL_STATE_CLOSING) {
		while (kutf_helper_pending_input(context) &&
		       (kutf_clk_trace_dequeue_portal_cmd(context, &new_cmd) ||
				!kutf_clk_trace_end_of_stream(&new_cmd))) {
			kutf_helper_send_named_str(context, "NACK",
					"Portal closing down");
		}
	}

	/* If no portal error, do a barebone test here irrespective
	 * whatever the portal live session has been testing, which
	 * is entirely driven by the user-side via portal requests.
	 */
	if (data->test_status <= KUTF_RESULT_WARN) {
		if (data->server_state != PORTAL_STATE_NO_CLK)
			kutf_clk_trace_barebone_check(context);
		else {
			/* No clocks case, NACK 2-sec for the fatal situation */
			kutf_clk_trace_no_clks_dummy(context);
		}
	}

	/* If we have changed pm_ctx count, drop it back */
	if (data->pm_ctx_cnt) {
		/* Although we count on portal requests, it only has material
		 * impact when from 0 -> 1. So the reverse is a simple one off.
		 */
		data->pm_ctx_cnt = 0;
		kutf_set_pm_ctx_idle(context);
	}

	/* Finally log the test result line */
	if (data->test_status < KUTF_RESULT_WARN)
		kutf_test_pass(context, data->result_msg);
	else if (data->test_status == KUTF_RESULT_WARN)
		kutf_test_warn(context, data->result_msg);
	else if (data->test_status == KUTF_RESULT_FATAL)
		kutf_test_fatal(context, data->result_msg);
	else
		kutf_test_fail(context, data->result_msg);

	pr_debug("Test end\n");
}

/**
 * mali_kutf_clk_rate_trace_create_fixture() - Creates the fixture data
 *                           required for mali_kutf_clk_rate_trace_test_portal.
 * @context:	KUTF context.
 *
 * Return: Fixture data created on success or NULL on failure
 */
static void *mali_kutf_clk_rate_trace_create_fixture(
		struct kutf_context *context)
{
	struct kutf_clk_rate_trace_fixture_data *data;
	struct kbase_device *kbdev;
	unsigned long rate;
	int i;

	/* Acquire the kbase device */
	pr_debug("Finding device\n");
	kbdev = kbase_find_device(MINOR_FOR_FIRST_KBASE_DEV);
	if (kbdev == NULL) {
		kutf_test_fail(context, "Failed to find kbase device");
		return NULL;
	}

	pr_debug("Creating fixture\n");
	data = kutf_mempool_alloc(&context->fixture_pool,
			sizeof(struct kutf_clk_rate_trace_fixture_data));
	if (!data)
		return NULL;

	*data = (const struct kutf_clk_rate_trace_fixture_data) { 0 };
	pr_debug("Hooking up the test portal to kbdev clk rate trace\n");
	spin_lock(&kbdev->pm.clk_rtm.lock);

	if (g_ptr_portal_data != NULL) {
		pr_warn("Test portal is already in use, run aborted\n");
		kutf_test_fail(context, "Portal allows single session only");
		spin_unlock(&kbdev->pm.clk_rtm.lock);
		return NULL;
	}

	for (i = 0; i < BASE_MAX_NR_CLOCKS_REGULATORS; i++) {
		if (kbdev->pm.clk_rtm.clks[i]) {
			data->nclks++;
			if (kbdev->pm.clk_rtm.gpu_idle)
				rate = 0;
			else
				rate = kbdev->pm.clk_rtm.clks[i]->clock_val;
			data->snapshot[i].previous_rate = rate;
			data->snapshot[i].current_rate = rate;
		}
	}

	spin_unlock(&kbdev->pm.clk_rtm.lock);

	if (data->nclks) {
		/* Subscribe this test server portal */
		data->listener.notify = kutf_portal_trace_write;
		data->invoke_notify = false;

		kbase_clk_rate_trace_manager_subscribe(
			&kbdev->pm.clk_rtm, &data->listener);
		/* Update the kutf_server_portal fixture_data pointer */
		g_ptr_portal_data = data;
	}

	data->kbdev = kbdev;
	data->result_msg = NULL;
	data->test_status = KUTF_RESULT_PASS;

	if (data->nclks == 0) {
		data->server_state = PORTAL_STATE_NO_CLK;
		pr_debug("Kbdev has no clocks for rate trace");
	} else
		data->server_state = PORTAL_STATE_LIVE;

	pr_debug("Created fixture\n");

	return data;
}

/**
 * Destroy fixture data previously created by
 * mali_kutf_clk_rate_trace_create_fixture.
 *
 * @context:             KUTF context.
 */
static void mali_kutf_clk_rate_trace_remove_fixture(
		struct kutf_context *context)
{
	struct kutf_clk_rate_trace_fixture_data *data = context->fixture;
	struct kbase_device *kbdev = data->kbdev;

	if (data->nclks) {
		/* Clean up the portal trace write arrangement */
		g_ptr_portal_data = NULL;

		kbase_clk_rate_trace_manager_unsubscribe(
			&kbdev->pm.clk_rtm, &data->listener);
	}
	pr_debug("Destroying fixture\n");
	kbase_release_device(kbdev);
	pr_debug("Destroyed fixture\n");
}

/**
 * mali_kutf_clk_rate_trace_test_module_init() - Entry point for test mdoule.
 */
int mali_kutf_clk_rate_trace_test_module_init(void)
{
	struct kutf_suite *suite;
	unsigned int filters;
	union kutf_callback_data suite_data = { 0 };

	pr_debug("Creating app\n");

	g_ptr_portal_data = NULL;
	kutf_app = kutf_create_application(CLK_RATE_TRACE_APP_NAME);

	if (!kutf_app) {
		pr_warn("Creation of app " CLK_RATE_TRACE_APP_NAME
				" failed!\n");
		return -ENOMEM;
	}

	pr_debug("Create suite %s\n", CLK_RATE_TRACE_SUITE_NAME);
	suite = kutf_create_suite_with_filters_and_data(
			kutf_app, CLK_RATE_TRACE_SUITE_NAME, 1,
			mali_kutf_clk_rate_trace_create_fixture,
			mali_kutf_clk_rate_trace_remove_fixture,
			KUTF_F_TEST_GENERIC,
			suite_data);

	if (!suite) {
		pr_warn("Creation of suite %s failed!\n",
				CLK_RATE_TRACE_SUITE_NAME);
		kutf_destroy_application(kutf_app);
		return -ENOMEM;
	}

	filters = suite->suite_default_flags;
	kutf_add_test_with_filters(
			suite, 0x0, CLK_RATE_TRACE_PORTAL,
			mali_kutf_clk_rate_trace_test_portal,
			filters);

	pr_debug("Init complete\n");
	return 0;
}

/**
 * mali_kutf_clk_rate_trace_test_module_exit() - Module exit point for this
 *                                               test.
 */
void mali_kutf_clk_rate_trace_test_module_exit(void)
{
	pr_debug("Exit start\n");
	kutf_destroy_application(kutf_app);
	pr_debug("Exit complete\n");
}


module_init(mali_kutf_clk_rate_trace_test_module_init);
module_exit(mali_kutf_clk_rate_trace_test_module_exit);

MODULE_LICENSE("GPL");
