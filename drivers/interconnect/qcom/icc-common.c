// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Linaro Ltd.
 */

#include <linux/of.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "icc-common.h"

struct icc_node_data *qcom_icc_xlate_extended(const struct of_phandle_args *spec,
					      void *data)
{
	struct icc_node_data *ndata;
	struct icc_node *node;

	node = of_icc_xlate_onecell(spec, data);
	if (IS_ERR(node))
		return ERR_CAST(node);

	ndata = kzalloc(sizeof(*ndata), GFP_KERNEL);
	if (!ndata)
		return ERR_PTR(-ENOMEM);

	ndata->node = node;

	if (spec->args_count == 2)
		ndata->tag = spec->args[1];

	if (spec->args_count > 2)
		pr_warn("%pOF: Too many arguments, path tag is not parsed\n", spec->np);

	return ndata;
}
EXPORT_SYMBOL_GPL(qcom_icc_xlate_extended);

MODULE_DESCRIPTION("Qualcomm interconnect common functions");
MODULE_LICENSE("GPL");
