// SPDX-License-Identifier: GPL-2.0-only
/* Copyright (C) 2021 Marvell. */

#include "otx2_cpt_devlink.h"

static int otx2_cpt_dl_egrp_create(struct devlink *dl, u32 id,
				   struct devlink_param_gset_ctx *ctx)
{
	struct otx2_cpt_devlink *cpt_dl = devlink_priv(dl);
	struct otx2_cptpf_dev *cptpf = cpt_dl->cptpf;

	return otx2_cpt_dl_custom_egrp_create(cptpf, ctx);
}

static int otx2_cpt_dl_egrp_delete(struct devlink *dl, u32 id,
				   struct devlink_param_gset_ctx *ctx)
{
	struct otx2_cpt_devlink *cpt_dl = devlink_priv(dl);
	struct otx2_cptpf_dev *cptpf = cpt_dl->cptpf;

	return otx2_cpt_dl_custom_egrp_delete(cptpf, ctx);
}

static int otx2_cpt_dl_uc_info(struct devlink *dl, u32 id,
			       struct devlink_param_gset_ctx *ctx)
{
	struct otx2_cpt_devlink *cpt_dl = devlink_priv(dl);
	struct otx2_cptpf_dev *cptpf = cpt_dl->cptpf;

	otx2_cpt_print_uc_dbg_info(cptpf);

	return 0;
}

enum otx2_cpt_dl_param_id {
	OTX2_CPT_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	OTX2_CPT_DEVLINK_PARAM_ID_EGRP_CREATE,
	OTX2_CPT_DEVLINK_PARAM_ID_EGRP_DELETE,
};

static const struct devlink_param otx2_cpt_dl_params[] = {
	DEVLINK_PARAM_DRIVER(OTX2_CPT_DEVLINK_PARAM_ID_EGRP_CREATE,
			     "egrp_create", DEVLINK_PARAM_TYPE_STRING,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     otx2_cpt_dl_uc_info, otx2_cpt_dl_egrp_create,
			     NULL),
	DEVLINK_PARAM_DRIVER(OTX2_CPT_DEVLINK_PARAM_ID_EGRP_DELETE,
			     "egrp_delete", DEVLINK_PARAM_TYPE_STRING,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     otx2_cpt_dl_uc_info, otx2_cpt_dl_egrp_delete,
			     NULL),
};

static int otx2_cpt_devlink_info_get(struct devlink *devlink,
				     struct devlink_info_req *req,
				     struct netlink_ext_ack *extack)
{
	return devlink_info_driver_name_put(req, "rvu_cptpf");
}

static const struct devlink_ops otx2_cpt_devlink_ops = {
	.info_get = otx2_cpt_devlink_info_get,
};

int otx2_cpt_register_dl(struct otx2_cptpf_dev *cptpf)
{
	struct device *dev = &cptpf->pdev->dev;
	struct otx2_cpt_devlink *cpt_dl;
	struct devlink *dl;
	int ret;

	dl = devlink_alloc(&otx2_cpt_devlink_ops,
			   sizeof(struct otx2_cpt_devlink), dev);
	if (!dl) {
		dev_warn(dev, "devlink_alloc failed\n");
		return -ENOMEM;
	}

	cpt_dl = devlink_priv(dl);
	cpt_dl->dl = dl;
	cpt_dl->cptpf = cptpf;
	cptpf->dl = dl;
	ret = devlink_params_register(dl, otx2_cpt_dl_params,
				      ARRAY_SIZE(otx2_cpt_dl_params));
	if (ret) {
		dev_err(dev, "devlink params register failed with error %d",
			ret);
		devlink_free(dl);
		return ret;
	}

	devlink_register(dl);

	return 0;
}

void otx2_cpt_unregister_dl(struct otx2_cptpf_dev *cptpf)
{
	struct devlink *dl = cptpf->dl;

	if (!dl)
		return;

	devlink_unregister(dl);
	devlink_params_unregister(dl, otx2_cpt_dl_params,
				  ARRAY_SIZE(otx2_cpt_dl_params));
	devlink_free(dl);
}
