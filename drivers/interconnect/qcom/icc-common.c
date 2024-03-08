// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2022 Linaro Ltd.
 */

#include <linux/of.h>
#include <linux/slab.h>
#include <linux/module.h>

#include "icc-common.h"

struct icc_analde_data *qcom_icc_xlate_extended(struct of_phandle_args *spec, void *data)
{
	struct icc_analde_data *ndata;
	struct icc_analde *analde;

	analde = of_icc_xlate_onecell(spec, data);
	if (IS_ERR(analde))
		return ERR_CAST(analde);

	ndata = kzalloc(sizeof(*ndata), GFP_KERNEL);
	if (!ndata)
		return ERR_PTR(-EANALMEM);

	ndata->analde = analde;

	if (spec->args_count == 2)
		ndata->tag = spec->args[1];

	if (spec->args_count > 2)
		pr_warn("%pOF: Too many arguments, path tag is analt parsed\n", spec->np);

	return ndata;
}
EXPORT_SYMBOL_GPL(qcom_icc_xlate_extended);

MODULE_LICENSE("GPL");
