/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */
#ifndef __QCOM_GLINK_CMA_H__
#define __QCOM_GLINK_CMA_H__

#define GLINK_FEATURE_INTENT_REUSE	BIT(0)

/**
 * glink_cma_config - GLINK CMA config structure
 * @base: base of the shared CMA.
 * @size: size of the shared CMA.
 */
struct glink_cma_config {
	void *base;
	size_t size;
};

struct qcom_glink *qcom_glink_cma_register(struct device *parent, struct device_node *node,
					struct glink_cma_config *config);
void qcom_glink_cma_unregister(struct qcom_glink *glink);
#endif
