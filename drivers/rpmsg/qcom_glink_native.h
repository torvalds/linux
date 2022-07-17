/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2016-2017, Linaro Ltd
 */

#ifndef __QCOM_GLINK_NATIVE_H__
#define __QCOM_GLINK_NATIVE_H__

#include <linux/rpmsg.h>

#define GLINK_FEATURE_INTENT_REUSE	BIT(0)
#define GLINK_FEATURE_MIGRATION		BIT(1)
#define GLINK_FEATURE_TRACER_PKT	BIT(2)
#define GLINK_FEATURE_ZERO_COPY		BIT(3)
#define GLINK_FEATURE_ZERO_COPY_POOLS	BIT(4)


/**
 * rpmsg rx callback return definitions
 * @RPMSG_HANDLED: rpmsg user is done processing data, framework can free the
 *                 resources related to the buffer
 * @RPMSG_DEFER:   rpmsg user is not done processing data, framework will hold
 *                 onto resources related to the buffer until rpmsg_rx_done is
 *                 called. User should check their endpoint to see if rx_done
 *                 is a supported operation.
 */
#define RPMSG_HANDLED	0
#define RPMSG_DEFER	1

struct qcom_glink_pipe {
	size_t length;

	size_t (*avail)(struct qcom_glink_pipe *glink_pipe);

	void (*peak)(struct qcom_glink_pipe *glink_pipe, void *data,
		     unsigned int offset, size_t count);
	void (*advance)(struct qcom_glink_pipe *glink_pipe, size_t count);

	void (*write)(struct qcom_glink_pipe *glink_pipe,
		      const void *hdr, size_t hlen,
		      const void *data, size_t dlen);

	void (*reset)(struct qcom_glink_pipe *glink_pipe);
};

struct qcom_glink;
extern const struct dev_pm_ops glink_native_pm_ops;

struct qcom_glink *qcom_glink_native_probe(struct device *dev,
					   unsigned long features,
					   struct qcom_glink_pipe *rx,
					   struct qcom_glink_pipe *tx,
					   bool intentless);
int qcom_glink_native_start(struct qcom_glink *glink);
void qcom_glink_native_remove(struct qcom_glink *glink);

void qcom_glink_native_unregister(struct qcom_glink *glink);

/* These operations are temporarily exposing signal interfaces */
int qcom_glink_get_signals(struct rpmsg_endpoint *ept);
int qcom_glink_set_signals(struct rpmsg_endpoint *ept, u32 set, u32 clear);
int qcom_glink_register_signals_cb(struct rpmsg_endpoint *ept,
	int (*signals_cb)(struct rpmsg_device *dev, void *priv, u32 old, u32 new));

/* These operations are temporarily exposing deferred freeing interfaces */
bool qcom_glink_rx_done_supported(struct rpmsg_endpoint *ept);
int qcom_glink_rx_done(struct rpmsg_endpoint *ept, void *data);

void *qcom_glink_prepare_da_for_cpu(u64 da, size_t len);

#endif
