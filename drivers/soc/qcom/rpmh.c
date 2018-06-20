// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2016-2018, The Linux Foundation. All rights reserved.
 */

#include <linux/atomic.h>
#include <linux/bug.h>
#include <linux/interrupt.h>
#include <linux/jiffies.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/wait.h>

#include <soc/qcom/rpmh.h>

#include "rpmh-internal.h"

#define RPMH_TIMEOUT_MS			msecs_to_jiffies(10000)

#define DEFINE_RPMH_MSG_ONSTACK(dev, s, q, name)	\
	struct rpmh_request name = {			\
		.msg = {				\
			.state = s,			\
			.cmds = name.cmd,		\
			.num_cmds = 0,			\
			.wait_for_compl = true,		\
		},					\
		.cmd = { { 0 } },			\
		.completion = q,			\
		.dev = dev,				\
	}

#define ctrlr_to_drv(ctrlr) container_of(ctrlr, struct rsc_drv, client)

static struct rpmh_ctrlr *get_rpmh_ctrlr(const struct device *dev)
{
	struct rsc_drv *drv = dev_get_drvdata(dev->parent);

	return &drv->client;
}

void rpmh_tx_done(const struct tcs_request *msg, int r)
{
	struct rpmh_request *rpm_msg = container_of(msg, struct rpmh_request,
						    msg);
	struct completion *compl = rpm_msg->completion;

	rpm_msg->err = r;

	if (r)
		dev_err(rpm_msg->dev, "RPMH TX fail in msg addr=%#x, err=%d\n",
			rpm_msg->msg.cmds[0].addr, r);

	/* Signal the blocking thread we are done */
	if (compl)
		complete(compl);
}

/**
 * __rpmh_write: send the RPMH request
 *
 * @dev: The device making the request
 * @state: Active/Sleep request type
 * @rpm_msg: The data that needs to be sent (cmds).
 */
static int __rpmh_write(const struct device *dev, enum rpmh_state state,
			struct rpmh_request *rpm_msg)
{
	struct rpmh_ctrlr *ctrlr = get_rpmh_ctrlr(dev);

	rpm_msg->msg.state = state;

	if (state != RPMH_ACTIVE_ONLY_STATE)
		return -EINVAL;

	WARN_ON(irqs_disabled());

	return rpmh_rsc_send_data(ctrlr_to_drv(ctrlr), &rpm_msg->msg);
}

/**
 * rpmh_write: Write a set of RPMH commands and block until response
 *
 * @rc: The RPMH handle got from rpmh_get_client
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
	int ret;

	if (!cmd || !n || n > MAX_RPMH_PAYLOAD)
		return -EINVAL;

	memcpy(rpm_msg.cmd, cmd, n * sizeof(*cmd));
	rpm_msg.msg.num_cmds = n;

	ret = __rpmh_write(dev, state, &rpm_msg);
	if (ret)
		return ret;

	ret = wait_for_completion_timeout(&compl, RPMH_TIMEOUT_MS);
	WARN_ON(!ret);
	return (ret > 0) ? 0 : -ETIMEDOUT;
}
EXPORT_SYMBOL(rpmh_write);
