// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2018, 2020-2021, The Linux Foundation. All rights reserved.
 * Copyright (c) 2023-2024, Qualcomm Innovation Center, Inc. All rights reserved.
 */

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/list.h>
#include <linux/lockdep.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <soc/qcom/rpmh.h>

#include "rpmh-internal.h"

#define RPMH_TIMEOUT_MS			msecs_to_jiffies(10000)

#define DEFINE_RPMH_MSG_ONSTACK(device, s, q, name)	\
	struct rpmh_request name = {			\
		.msg = {				\
			.state = s,			\
			.cmds = name.cmd,		\
			.num_cmds = 0,			\
			.wait_for_compl = true,		\
		},					\
		.cmd = { { 0 } },			\
		.completion = q,			\
		.dev = device,				\
	}

#define ctrlr_to_drv(ctrlr) container_of(ctrlr, struct rsc_drv, client)

static struct rpmh_ctrlr *get_rpmh_ctrlr(const struct device *dev)
{
	struct rsc_drv *drv = dev_get_drvdata(dev->parent);

	return &drv->client;
}

static struct rpmh_ctrlr *get_rpmh_ctrlr_no_child(const struct device *dev)
{
	struct rsc_drv *drv = dev_get_drvdata(dev);

	return &drv->client;
}

static int check_ctrlr_state(struct rpmh_ctrlr *ctrlr, enum rpmh_state state)
{
	int ret = 0;

	if (state != RPMH_ACTIVE_ONLY_STATE)
		return ret;

	if (!(ctrlr->flags & SOLVER_PRESENT))
		return ret;

	/* Do not allow sending active votes when in solver mode */
	spin_lock(&ctrlr->cache_lock);
	if (ctrlr->in_solver_mode)
		ret = -EBUSY;
	spin_unlock(&ctrlr->cache_lock);

	return ret;
}

void rpmh_tx_done(const struct tcs_request *msg)
{
	struct rpmh_request *rpm_msg = container_of(msg, struct rpmh_request,
						    msg);
	struct completion *compl = rpm_msg->completion;

	if (!compl)
		return;

	/* Signal the blocking thread we are done */
	complete(compl);
}

static struct cache_req *get_non_batch_cache_req(struct rpmh_ctrlr *ctrlr, u32 addr)
{
	struct cache_req *req = ctrlr->non_batch_cache;
	int i;

	if (ctrlr->non_batch_cache_idx >= CMD_DB_MAX_RESOURCES)
		return NULL;

	for (i = 0; i < ctrlr->non_batch_cache_idx; i++) {
		req = &ctrlr->non_batch_cache[i];
		if (req->addr == addr)
			return req;
	}

	req = &ctrlr->non_batch_cache[ctrlr->non_batch_cache_idx];
	req->sleep_val = req->wake_val = UINT_MAX;
	ctrlr->non_batch_cache_idx++;

	return req;
}

static struct cache_req *cache_rpm_request(struct rpmh_ctrlr *ctrlr,
					   enum rpmh_state state,
					   struct tcs_cmd *cmd)
{
	struct cache_req *req;
	unsigned long flags;
	u32 old_sleep_val, old_wake_val;

	spin_lock_irqsave(&ctrlr->cache_lock, flags);

	req = get_non_batch_cache_req(ctrlr, cmd->addr);
	if (!req) {
		req = ERR_PTR(-ENOMEM);
		goto unlock;
	}

	req->addr = cmd->addr;
	old_sleep_val = req->sleep_val;
	old_wake_val = req->wake_val;

	switch (state) {
	case RPMH_ACTIVE_ONLY_STATE:
	case RPMH_WAKE_ONLY_STATE:
		req->wake_val = cmd->data;
		break;
	case RPMH_SLEEP_STATE:
		req->sleep_val = cmd->data;
		break;
	}

	ctrlr->dirty |= (req->sleep_val != old_sleep_val ||
			 req->wake_val != old_wake_val) &&
			 req->sleep_val != UINT_MAX &&
			 req->wake_val != UINT_MAX;

unlock:
	spin_unlock_irqrestore(&ctrlr->cache_lock, flags);

	return req;
}

/**
 * __rpmh_write: Cache and send the RPMH request
 *
 * @dev: The device making the request
 * @state: Active/Sleep request type
 * @rpm_msg: The data that needs to be sent (cmds).
 *
 * Cache the RPMH request and send if the state is ACTIVE_ONLY.
 * SLEEP/WAKE_ONLY requests are not sent to the controller at
 * this time. Use rpmh_flush() to send them to the controller.
 */
static int __rpmh_write(const struct device *dev, enum rpmh_state state,
			struct rpmh_request *rpm_msg)
{
	struct rpmh_ctrlr *ctrlr = get_rpmh_ctrlr(dev);
	int ret = -EINVAL;
	struct cache_req *req;
	int i, ch;

	/* Cache the request in our store and link the payload */
	for (i = 0; i < rpm_msg->msg.num_cmds; i++) {
		req = cache_rpm_request(ctrlr, state, &rpm_msg->msg.cmds[i]);
		if (IS_ERR(req))
			return PTR_ERR(req);
	}

	if (state == RPMH_ACTIVE_ONLY_STATE) {
		WARN_ON(irqs_disabled());

		ch = rpmh_rsc_get_channel(ctrlr_to_drv(ctrlr));
		if (ch < 0)
			return ch;

		ret = rpmh_rsc_send_data(ctrlr_to_drv(ctrlr), &rpm_msg->msg, ch);
	} else {
		/* Clean up our call by spoofing tx_done */
		ret = 0;
		rpmh_tx_done(&rpm_msg->msg);
	}

	return ret;
}

static int __fill_rpmh_msg(struct rpmh_request *req, enum rpmh_state state,
		const struct tcs_cmd *cmd, u32 n)
{
	if (!cmd || !n || n > MAX_RPMH_PAYLOAD)
		return -EINVAL;

	memcpy(req->cmd, cmd, n * sizeof(*cmd));

	req->msg.state = state;
	req->msg.cmds = req->cmd;
	req->msg.num_cmds = n;

	return 0;
}

/**
 * rpmh_write_async: Write a set of RPMH commands
 *
 * @dev: The device making the request
 * @state: Active/sleep set
 * @cmd: The payload data
 * @n: The number of elements in payload
 *
 * Write a set of RPMH commands, the order of commands is maintained
 * and will be sent as a single shot.
 */
int rpmh_write_async(const struct device *dev, enum rpmh_state state,
		     const struct tcs_cmd *cmd, u32 n)
{
	DEFINE_RPMH_MSG_ONSTACK(dev, state, NULL, rpm_msg);
	struct rpmh_ctrlr *ctrlr = get_rpmh_ctrlr(dev);
	int ret;

	if (rpmh_standalone)
		return 0;

	rpm_msg.msg.wait_for_compl = false;
	ret = check_ctrlr_state(ctrlr, state);
	if (ret)
		return ret;

	ret = __fill_rpmh_msg(&rpm_msg, state, cmd, n);
	if (ret)
		return ret;

	return __rpmh_write(dev, state, &rpm_msg);
}
EXPORT_SYMBOL(rpmh_write_async);

/**
 * rpmh_write: Write a set of RPMH commands and block until response
 *
 * @dev: The device making the request
 * @state: Active/sleep set
 * @cmd: The payload data
 * @n: The number of elements in @cmd
 *
 * May sleep. Do not call from atomic contexts.
 */
int rpmh_write(const struct device *dev, enum rpmh_state state,
	       const struct tcs_cmd *cmd, u32 n)
{
	DECLARE_COMPLETION_ONSTACK(compl);
	DEFINE_RPMH_MSG_ONSTACK(dev, state, &compl, rpm_msg);
	struct rpmh_ctrlr *ctrlr = get_rpmh_ctrlr(dev);
	int ret;

	if (rpmh_standalone)
		return 0;

	ret = check_ctrlr_state(ctrlr, state);
	if (ret)
		return ret;

	ret = __fill_rpmh_msg(&rpm_msg, state, cmd, n);
	if (ret)
		return ret;

	ret = __rpmh_write(dev, state, &rpm_msg);
	if (ret)
		return ret;

	ret = wait_for_completion_timeout(&compl, RPMH_TIMEOUT_MS);
	if (!ret) {
		rpmh_rsc_debug(ctrlr_to_drv(ctrlr), &compl);
		return -ETIMEDOUT;
	}

	return 0;
}
EXPORT_SYMBOL(rpmh_write);

static int flush_batch(struct rpmh_ctrlr *ctrlr, int ch)
{
	int ret;

	/* Send Sleep/Wake requests to the controller, expect no response IRQ */
	ret = rpmh_rsc_write_ctrl_data(ctrlr_to_drv(ctrlr),
				       &ctrlr->batch_cache[RPMH_SLEEP_STATE].msg, ch);
	if (ret)
		return ret;

	return rpmh_rsc_write_ctrl_data(ctrlr_to_drv(ctrlr),
					&ctrlr->batch_cache[RPMH_WAKE_ONLY_STATE].msg, ch);
}

/**
 * rpmh_write_batch: Write multiple sets of RPMH commands and wait for the
 * batch to finish.
 *
 * @dev: the device making the request
 * @state: Active/sleep set
 * @cmd: The payload data
 * @n: The array of count of elements in each batch, 0 terminated.
 *
 * Write a request to the RSC controller without caching. If the request
 * state is ACTIVE, then the requests are treated as completion request
 * and sent to the controller immediately. The function waits until all the
 * commands are complete. If the request was to SLEEP or WAKE_ONLY, then the
 * request is sent as fire-n-forget and no ack is expected.
 *
 * May sleep. Do not call from atomic contexts for ACTIVE_ONLY requests.
 */
int rpmh_write_batch(const struct device *dev, enum rpmh_state state,
		     const struct tcs_cmd *cmd, u32 *n)
{
	DECLARE_COMPLETION_ONSTACK(compl);
	DEFINE_RPMH_MSG_ONSTACK(dev, state, &compl, rpm_msg);
	struct rpmh_ctrlr *ctrlr = get_rpmh_ctrlr(dev);
	int ret, ch;
	unsigned long flags;

	if (rpmh_standalone)
		return 0;

	ret = check_ctrlr_state(ctrlr, state);
	if (ret)
		return ret;

	if (state == RPMH_ACTIVE_ONLY_STATE) {
		ret = __fill_rpmh_msg(&rpm_msg, state, cmd, *n);
		if (ret)
			return ret;
	} else {
		spin_lock_irqsave(&ctrlr->cache_lock, flags);
		memset(&ctrlr->batch_cache[state], 0, sizeof(struct rpmh_request));
		ret = __fill_rpmh_msg(&ctrlr->batch_cache[state], state, cmd, *n);
		ctrlr->dirty = true;
		spin_unlock_irqrestore(&ctrlr->cache_lock, flags);

		return ret;
	}

	ch = rpmh_rsc_get_channel(ctrlr_to_drv(ctrlr));
	if (ch < 0)
		return ch;

	ret = rpmh_rsc_send_data(ctrlr_to_drv(ctrlr), &rpm_msg.msg, ch);
	if (ret) {
		pr_err("Error(%d) sending RPMH message addr=%#x\n",
		       ret, rpm_msg.msg.cmds[0].addr);
		return ret;
	}

	ret = wait_for_completion_timeout(&compl, RPMH_TIMEOUT_MS);
	if (!ret) {
		/*
		 * Better hope they never finish because they'll signal
		 * the completion that we're going to free once
		 * we've returned from this function.
		 */
		rpmh_rsc_debug(ctrlr_to_drv(ctrlr), &compl);
		BUG_ON(1);
	}

	return 0;
}
EXPORT_SYMBOL(rpmh_write_batch);

static int is_req_valid(struct cache_req *req)
{
	return (req->sleep_val != UINT_MAX &&
		req->wake_val != UINT_MAX &&
		req->sleep_val != req->wake_val);
}

static int send_single(struct rpmh_ctrlr *ctrlr, enum rpmh_state state,
		       u32 addr, u32 data, int ch)
{
	DEFINE_RPMH_MSG_ONSTACK(NULL, state, NULL, rpm_msg);

	/* Wake sets are always complete and sleep sets are not */
	rpm_msg.msg.wait_for_compl = (state == RPMH_WAKE_ONLY_STATE);
	rpm_msg.cmd[0].addr = addr;
	rpm_msg.cmd[0].data = data;
	rpm_msg.msg.num_cmds = 1;

	return rpmh_rsc_write_ctrl_data(ctrlr_to_drv(ctrlr), &rpm_msg.msg, ch);
}

int _rpmh_flush(struct rpmh_ctrlr *ctrlr, int ch)
{
	struct cache_req *p;
	int ret = 0, i;

	if (!ctrlr->dirty) {
		pr_debug("Skipping flush, TCS has latest data.\n");
		return ret;
	}

	/* Invalidate the TCSes first to avoid stale data */
	rpmh_rsc_invalidate(ctrlr_to_drv(ctrlr), ch);

	/* First flush the cached batch requests */
	ret = flush_batch(ctrlr, ch);
	if (ret)
		return ret;

	for (i = 0; i < ctrlr->non_batch_cache_idx; i++) {
		p = &ctrlr->non_batch_cache[i];
		if (!is_req_valid(p)) {
			pr_debug("%s: skipping RPMH req: a:%#x s:%#x w:%#x\n",
				 __func__, p->addr, p->sleep_val, p->wake_val);
			continue;
		}
		ret = send_single(ctrlr, RPMH_SLEEP_STATE, p->addr,
				  p->sleep_val, ch);
		if (ret)
			return ret;
		ret = send_single(ctrlr, RPMH_WAKE_ONLY_STATE, p->addr,
				  p->wake_val, ch);
		if (ret)
			return ret;
	}

	ctrlr->dirty = false;

	return ret;
}

/**
 * rpmh_flush() - Flushes the buffered sleep and wake sets to TCSes
 *
 * @ctrlr: Controller making request to flush cached data
 * @ch:    Channel number
 *
 * Return:
 * * 0          - Success
 * * Error code - Otherwise
 */
int rpmh_flush(struct rpmh_ctrlr *ctrlr, int ch)
{
	unsigned long flags;
	int ret;

	if (rpmh_standalone)
		return 0;

	/*
	 * For RSC that don't have solver mode,
	 * rpmh_flush() is only called when we think we're running
	 * on the last CPU with irqs_disabled.
	 *
	 * For RSC that have solver mode,
	 * rpmh_flush() can be invoked with irqs enabled by any CPU.
	 *
	 * Conditionally check for irqs_disabled only when solver mode
	 * is not available.
	 */
	if (!(ctrlr->flags & SOLVER_PRESENT))
		lockdep_assert_irqs_disabled();

	spin_lock_irqsave(&ctrlr->cache_lock, flags);
	ret = _rpmh_flush(ctrlr, ch);
	spin_unlock_irqrestore(&ctrlr->cache_lock, flags);

	return ret;
}

/**
 * rpmh_write_sleep_and_wake: Writes the buffered wake and sleep sets to TCSes
 *
 * @dev: The device making the request
 *
 * Return:
 * * 0          - Success
 * * Error code - Otherwise
 */
int rpmh_write_sleep_and_wake(const struct device *dev)
{
	struct rpmh_ctrlr *ctrlr = get_rpmh_ctrlr(dev);
	int ch, ret;

	ch = rpmh_rsc_get_channel(ctrlr_to_drv(ctrlr));
	if (ch < 0)
		return ch;

	ret = rpmh_flush(ctrlr, ch);
	if (ret || !(ctrlr->flags & HW_CHANNEL_PRESENT))
		return ret;

	return rpmh_rsc_switch_channel(ctrlr_to_drv(ctrlr), ch);
}
EXPORT_SYMBOL(rpmh_write_sleep_and_wake);

/**
 * rpmh_write_sleep_and_wake_no_child: Writes the buffered wake and sleep sets to TCSes
 *
 * Used when the client calling this is not a child device of RSC device.
 * Use it only after getting the device using rpmh_get_device().
 * @dev: The device making the request
 *
 * Return:
 * * 0          - Success
 * * Error code - Otherwise
 */
int rpmh_write_sleep_and_wake_no_child(const struct device *dev)
{
	struct rpmh_ctrlr *ctrlr = get_rpmh_ctrlr_no_child(dev);
	int ch, ret;

	ch = rpmh_rsc_get_channel(ctrlr_to_drv(ctrlr));
	if (ch < 0) {
		rpmh_rsc_debug_channel_busy(ctrlr_to_drv(ctrlr));
		BUG_ON(1);
		return ch;
	}

	ret = rpmh_flush(ctrlr, ch);
	if (ret || !(ctrlr->flags & HW_CHANNEL_PRESENT))
		return ret;

	ret = rpmh_rsc_switch_channel(ctrlr_to_drv(ctrlr), ch);
	if (ret) {
		rpmh_rsc_debug_channel_busy(ctrlr_to_drv(ctrlr));
		BUG_ON(1);
	}

	return ret;
}
EXPORT_SYMBOL(rpmh_write_sleep_and_wake_no_child);

/**
 * rpmh_invalidate: Invalidate sleep and wake sets in batch_cache
 *
 * @dev: The device making the request
 *
 * Invalidate the sleep and wake values in batch_cache.
 */
void rpmh_invalidate(const struct device *dev)
{
	struct rpmh_ctrlr *ctrlr = get_rpmh_ctrlr(dev);
	unsigned long flags;

	if (rpmh_standalone)
		return;

	spin_lock_irqsave(&ctrlr->cache_lock, flags);
	memset(&ctrlr->batch_cache[RPMH_SLEEP_STATE], 0, sizeof(struct rpmh_request));
	memset(&ctrlr->batch_cache[RPMH_WAKE_ONLY_STATE], 0, sizeof(struct rpmh_request));
	ctrlr->dirty = true;
	spin_unlock_irqrestore(&ctrlr->cache_lock, flags);
}
EXPORT_SYMBOL(rpmh_invalidate);

/**
 * rpmh_mode_solver_set: Indicate that the RSC controller hardware has
 * been configured to be in solver mode
 *
 * @dev: The device making the request
 * @enable: Boolean value indicating if the controller is in solver mode.
 * Return:
 * * 0          - Success
 * * Error code - Otherwise
 */
int rpmh_mode_solver_set(const struct device *dev, bool enable)
{
	int ret;
	struct rpmh_ctrlr *ctrlr = get_rpmh_ctrlr(dev);

	if (rpmh_standalone)
		return 0;

	if (!(ctrlr->flags & SOLVER_PRESENT))
		return -EINVAL;

	spin_lock(&ctrlr->cache_lock);
	ret = rpmh_rsc_mode_solver_set(ctrlr_to_drv(ctrlr), enable);
	if (!ret)
		ctrlr->in_solver_mode = enable;
	spin_unlock(&ctrlr->cache_lock);

	return ret;
}
EXPORT_SYMBOL(rpmh_mode_solver_set);


/**
 * RPMH Fast Path mode
 *
 * - Fast path mode is a lock-less request transmission to AOSS.
 * - Requests may be sent from interrupt/scheduler context.
 * - Dedicated fast-path TCS must be available in the RSC.
 * - Error returned, when dedicated TCS is not available.
 * - Only one client is expected to call RPMH fast path.
 * - Serialization of requests is a responsibility of the client.
 * - Only active state requests may be made through this mode.
 * - Sleep/Wake requests for the nodes must be made through
 *   the standard RPMH APIs (locking modes).
 * - Fast path requests are not cached and sent immediately.
 * - Fast path requests are all blocking requests.
 * - Fast path requests do not use AMC completion interrupts,
 *   therefore will not receive a tx_done callback.
 */

/**
 * rpmh_init_fast_path: Initialize TCS request in fast-path TCS
 *
 * @dev: The device making the request
 * @cmd: The {addr, data} to be initialized with.
 * @n:   The number of @cmd
 *
 * Return:
 * * 0          - Success
 * * Error code - Otherwise
 */
int rpmh_init_fast_path(const struct device *dev,
			struct tcs_cmd *cmd, int n)
{
	struct rpmh_ctrlr *ctrlr = get_rpmh_ctrlr(dev);
	struct tcs_request req;
	int ch;

	if (rpmh_standalone)
		return 0;

	ch = rpmh_rsc_get_channel(ctrlr_to_drv(ctrlr));
	if (ch < 0)
		return ch;

	req.cmds = cmd;
	req.num_cmds = n;
	req.wait_for_compl = 0;

	return rpmh_rsc_init_fast_path(ctrlr_to_drv(ctrlr), &req, ch);
}
EXPORT_SYMBOL(rpmh_init_fast_path);

/**
 * rpmh_update_fast_path: Initialize TCS request in fast-path TCS
 *
 * @dev:         The device making the request
 * @cmd:         The data to be updated.
 * @n:           The number of @cmd
 * @update_mask: The elements in @cmd that only need to be updated
 *
 * Return:
 * * 0          - Success
 * * Error code - Otherwise
 */
int rpmh_update_fast_path(const struct device *dev,
			  struct tcs_cmd *cmd, int n, u32 update_mask)
{
	struct rpmh_ctrlr *ctrlr = get_rpmh_ctrlr(dev);
	struct tcs_request req;
	int ch;

	if (rpmh_standalone)
		return 0;

	ch = rpmh_rsc_get_channel(ctrlr_to_drv(ctrlr));
	if (ch < 0)
		return ch;

	req.cmds = cmd;
	req.num_cmds = n;
	req.wait_for_compl = 0;

	return rpmh_rsc_update_fast_path(ctrlr_to_drv(ctrlr), &req,
					 update_mask, ch);
}
EXPORT_SYMBOL(rpmh_update_fast_path);

/**
 * rpmh_get_device: Get the DRV device
 *
 * @name:        The RSC device used for DRV DRV
 * @drv_id:      The index of DRV
 *
 * Used when the device voting to RPMh is not a child device
 * of RSC device. Such device can get RSC device using this API.
 * but will be able to use only rpmh_drv_start(), rpmh_drv_stop()
 * and rpmh_write_sleep_and_wake_no_child().
 *
 * Return:
 * * dev          - Device to use when calling above APIs
 * * Error        - Error pointer
 */
const struct device *rpmh_get_device(const char *name, u32 drv_id)
{
	return rpmh_rsc_get_device(name, drv_id);
}
EXPORT_SYMBOL(rpmh_get_device);

/**
 * rpmh_drv_start: Start the DRV channel
 *
 * @dev:         The device making the request
 *
 * Return:
 * * 0          - Success
 * * Error code - Otherwise
 */
int rpmh_drv_start(const struct device *dev)
{
	struct rpmh_ctrlr *ctrlr = get_rpmh_ctrlr_no_child(dev);
	int ret;

	if (rpmh_standalone)
		return 0;

	ret = rpmh_rsc_drv_enable(ctrlr_to_drv(ctrlr), true);
	if (ret) {
		rpmh_rsc_debug_channel_busy(ctrlr_to_drv(ctrlr));
		BUG_ON(1);
	}

	return ret;
}
EXPORT_SYMBOL(rpmh_drv_start);

/**
 * rpmh_drv_stop: Start the DRV channel
 *
 * @dev:         The device making the request
 *
 * Return:
 * * 0          - Success
 * * Error code - Otherwise
 */
int rpmh_drv_stop(const struct device *dev)
{
	struct rpmh_ctrlr *ctrlr = get_rpmh_ctrlr_no_child(dev);
	int ret;

	if (rpmh_standalone)
		return 0;

	ret = rpmh_rsc_drv_enable(ctrlr_to_drv(ctrlr), false);
	if (ret) {
		rpmh_rsc_debug_channel_busy(ctrlr_to_drv(ctrlr));
		BUG_ON(1);
	}

	return ret;
}
EXPORT_SYMBOL(rpmh_drv_stop);
