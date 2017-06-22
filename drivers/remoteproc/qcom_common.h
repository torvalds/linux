#ifndef __RPROC_QCOM_COMMON_H__
#define __RPROC_QCOM_COMMON_H__

#include <linux/remoteproc.h>
#include "remoteproc_internal.h"

struct qcom_rproc_subdev {
	struct rproc_subdev subdev;

	struct device *dev;
	struct device_node *node;
	struct qcom_smd_edge *edge;
};

struct resource_table *qcom_mdt_find_rsc_table(struct rproc *rproc,
					       const struct firmware *fw,
					       int *tablesz);

void qcom_add_smd_subdev(struct rproc *rproc, struct qcom_rproc_subdev *smd);
void qcom_remove_smd_subdev(struct rproc *rproc, struct qcom_rproc_subdev *smd);

#endif
