/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __RPROC_QCOM_COMMON_H__
#define __RPROC_QCOM_COMMON_H__

#include <linux/remoteproc.h>
#include "remoteproc_internal.h"
#include <linux/soc/qcom/qmi.h>

struct qcom_sysmon;

struct qcom_rproc_glink {
	struct rproc_subdev subdev;

	const char *ssr_name;

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

void qcom_add_glink_subdev(struct rproc *rproc, struct qcom_rproc_glink *glink,
			   const char *ssr_name);
void qcom_remove_glink_subdev(struct rproc *rproc, struct qcom_rproc_glink *glink);

int qcom_register_dump_segments(struct rproc *rproc, const struct firmware *fw);

void qcom_add_smd_subdev(struct rproc *rproc, struct qcom_rproc_subdev *smd);
void qcom_remove_smd_subdev(struct rproc *rproc, struct qcom_rproc_subdev *smd);

void qcom_add_ssr_subdev(struct rproc *rproc, struct qcom_rproc_ssr *ssr,
			 const char *ssr_name);
void qcom_remove_ssr_subdev(struct rproc *rproc, struct qcom_rproc_ssr *ssr);

#if IS_ENABLED(CONFIG_QCOM_SYSMON)
struct qcom_sysmon *qcom_add_sysmon_subdev(struct rproc *rproc,
					   const char *name,
					   int ssctl_instance);
void qcom_remove_sysmon_subdev(struct qcom_sysmon *sysmon);
#else
static inline struct qcom_sysmon *qcom_add_sysmon_subdev(struct rproc *rproc,
							 const char *name,
							 int ssctl_instance)
{
	return NULL;
}

static inline void qcom_remove_sysmon_subdev(struct qcom_sysmon *sysmon)
{
}
#endif

#endif
