/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RPROC_QCOM_COMMON_H__
#define __RPROC_QCOM_COMMON_H__

#include <linux/remoteproc.h>
#include "remoteproc_internal.h"

struct qcom_rproc_glink {
	struct rproc_subdev subdev;

	struct device *dev;
	struct device_node *node;
	struct qcom_glink *edge;
};

struct qcom_rproc_subdev {
	struct rproc_subdev subdev;

	struct device *dev;
	struct device_node *node;
	struct qcom_smd_edge *edge;
};

struct qcom_rproc_ssr {
	struct rproc_subdev subdev;

	const char *name;
};

struct resource_table *qcom_mdt_find_rsc_table(struct rproc *rproc,
					       const struct firmware *fw,
					       int *tablesz);

void qcom_add_glink_subdev(struct rproc *rproc, struct qcom_rproc_glink *glink);
void qcom_remove_glink_subdev(struct rproc *rproc, struct qcom_rproc_glink *glink);

void qcom_add_smd_subdev(struct rproc *rproc, struct qcom_rproc_subdev *smd);
void qcom_remove_smd_subdev(struct rproc *rproc, struct qcom_rproc_subdev *smd);

void qcom_add_ssr_subdev(struct rproc *rproc, struct qcom_rproc_ssr *ssr,
			 const char *ssr_name);
void qcom_remove_ssr_subdev(struct rproc *rproc, struct qcom_rproc_ssr *ssr);

#endif
