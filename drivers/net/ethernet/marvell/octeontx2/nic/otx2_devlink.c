// SPDX-License-Identifier: GPL-2.0
/* Marvell RVU PF/VF Netdev Devlink
 *
 * Copyright (C) 2021 Marvell.
 */

#include "otx2_common.h"

/* Devlink Params APIs */
static int otx2_dl_mcam_count_validate(struct devlink *devlink, u32 id,
				       union devlink_param_value val,
				       struct netlink_ext_ack *extack)
{
	struct otx2_devlink *otx2_dl = devlink_priv(devlink);
	struct otx2_nic *pfvf = otx2_dl->pfvf;
	struct otx2_flow_config *flow_cfg;

	if (!pfvf->flow_cfg) {
		NL_SET_ERR_MSG_MOD(extack,
				   "pfvf->flow_cfg not initialized");
		return -EINVAL;
	}

	flow_cfg = pfvf->flow_cfg;
	if (flow_cfg && flow_cfg->nr_flows) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot modify count when there are active rules");
		return -EINVAL;
	}

	return 0;
}

static int otx2_dl_mcam_count_set(struct devlink *devlink, u32 id,
				  struct devlink_param_gset_ctx *ctx,
				  struct netlink_ext_ack *extack)
{
	struct otx2_devlink *otx2_dl = devlink_priv(devlink);
	struct otx2_nic *pfvf = otx2_dl->pfvf;

	if (!pfvf->flow_cfg)
		return 0;

	pfvf->flow_cfg->ntuple_cnt = ctx->val.vu16;
	otx2_alloc_mcam_entries(pfvf, ctx->val.vu16);

	return 0;
}

static int otx2_dl_mcam_count_get(struct devlink *devlink, u32 id,
				  struct devlink_param_gset_ctx *ctx)
{
	struct otx2_devlink *otx2_dl = devlink_priv(devlink);
	struct otx2_nic *pfvf = otx2_dl->pfvf;
	struct otx2_flow_config *flow_cfg;

	if (!pfvf->flow_cfg) {
		ctx->val.vu16 = 0;
		return 0;
	}

	flow_cfg = pfvf->flow_cfg;
	ctx->val.vu16 = flow_cfg->max_flows;

	return 0;
}

static int otx2_dl_ucast_flt_cnt_set(struct devlink *devlink, u32 id,
				     struct devlink_param_gset_ctx *ctx,
				     struct netlink_ext_ack *extack)
{
	struct otx2_devlink *otx2_dl = devlink_priv(devlink);
	struct otx2_nic *pfvf = otx2_dl->pfvf;
	int err;

	pfvf->flow_cfg->ucast_flt_cnt = ctx->val.vu8;

	otx2_mcam_flow_del(pfvf);
	err = otx2_mcam_entry_init(pfvf);
	if (err)
		return err;

	return 0;
}

static int otx2_dl_ucast_flt_cnt_get(struct devlink *devlink, u32 id,
				     struct devlink_param_gset_ctx *ctx)
{
	struct otx2_devlink *otx2_dl = devlink_priv(devlink);
	struct otx2_nic *pfvf = otx2_dl->pfvf;

	ctx->val.vu8 = pfvf->flow_cfg ? pfvf->flow_cfg->ucast_flt_cnt : 0;

	return 0;
}

static int otx2_dl_ucast_flt_cnt_validate(struct devlink *devlink, u32 id,
					  union devlink_param_value val,
					  struct netlink_ext_ack *extack)
{
	struct otx2_devlink *otx2_dl = devlink_priv(devlink);
	struct otx2_nic *pfvf = otx2_dl->pfvf;

	/* Check for UNICAST filter support*/
	if (!(pfvf->flags & OTX2_FLAG_UCAST_FLTR_SUPPORT)) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Unicast filter not enabled");
		return -EINVAL;
	}

	if (!pfvf->flow_cfg) {
		NL_SET_ERR_MSG_MOD(extack,
				   "pfvf->flow_cfg not initialized");
		return -EINVAL;
	}

	if (pfvf->flow_cfg->nr_flows) {
		NL_SET_ERR_MSG_MOD(extack,
				   "Cannot modify count when there are active rules");
		return -EINVAL;
	}

	return 0;
}

enum otx2_dl_param_id {
	OTX2_DEVLINK_PARAM_ID_BASE = DEVLINK_PARAM_GENERIC_ID_MAX,
	OTX2_DEVLINK_PARAM_ID_MCAM_COUNT,
	OTX2_DEVLINK_PARAM_ID_UCAST_FLT_CNT,
};

static const struct devlink_param otx2_dl_params[] = {
	DEVLINK_PARAM_DRIVER(OTX2_DEVLINK_PARAM_ID_MCAM_COUNT,
			     "mcam_count", DEVLINK_PARAM_TYPE_U16,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     otx2_dl_mcam_count_get, otx2_dl_mcam_count_set,
			     otx2_dl_mcam_count_validate),
	DEVLINK_PARAM_DRIVER(OTX2_DEVLINK_PARAM_ID_UCAST_FLT_CNT,
			     "unicast_filter_count", DEVLINK_PARAM_TYPE_U8,
			     BIT(DEVLINK_PARAM_CMODE_RUNTIME),
			     otx2_dl_ucast_flt_cnt_get, otx2_dl_ucast_flt_cnt_set,
			     otx2_dl_ucast_flt_cnt_validate),
};

#ifdef CONFIG_RVU_ESWITCH
static int otx2_devlink_eswitch_mode_get(struct devlink *devlink, u16 *mode)
{
	struct otx2_devlink *otx2_dl = devlink_priv(devlink);
	struct otx2_nic *pfvf = otx2_dl->pfvf;

	if (!otx2_rep_dev(pfvf->pdev))
		return -EOPNOTSUPP;

	*mode = pfvf->esw_mode;

	return 0;
}

static int otx2_devlink_eswitch_mode_set(struct devlink *devlink, u16 mode,
					 struct netlink_ext_ack *extack)
{
	struct otx2_devlink *otx2_dl = devlink_priv(devlink);
	struct otx2_nic *pfvf = otx2_dl->pfvf;
	int ret = 0;

	if (!otx2_rep_dev(pfvf->pdev))
		return -EOPNOTSUPP;

	if (pfvf->esw_mode == mode)
		return 0;

	switch (mode) {
	case DEVLINK_ESWITCH_MODE_LEGACY:
		rvu_rep_destroy(pfvf);
		break;
	case DEVLINK_ESWITCH_MODE_SWITCHDEV:
		ret = rvu_rep_create(pfvf, extack);
		break;
	default:
		return -EINVAL;
	}

	if (!ret)
		pfvf->esw_mode = mode;

	return ret;
}
#endif

static const struct devlink_ops otx2_devlink_ops = {
#ifdef CONFIG_RVU_ESWITCH
	.eswitch_mode_get = otx2_devlink_eswitch_mode_get,
	.eswitch_mode_set = otx2_devlink_eswitch_mode_set,
#endif
};

int otx2_register_dl(struct otx2_nic *pfvf)
{
	struct otx2_devlink *otx2_dl;
	struct devlink *dl;
	int err;

	dl = devlink_alloc(&otx2_devlink_ops,
			   sizeof(struct otx2_devlink), pfvf->dev);
	if (!dl) {
		dev_warn(pfvf->dev, "devlink_alloc failed\n");
		return -ENOMEM;
	}

	otx2_dl = devlink_priv(dl);
	otx2_dl->dl = dl;
	otx2_dl->pfvf = pfvf;
	pfvf->dl = otx2_dl;

	err = devlink_params_register(dl, otx2_dl_params,
				      ARRAY_SIZE(otx2_dl_params));
	if (err) {
		dev_err(pfvf->dev,
			"devlink params register failed with error %d", err);
		goto err_dl;
	}

	devlink_register(dl);
	return 0;

err_dl:
	devlink_free(dl);
	return err;
}
EXPORT_SYMBOL(otx2_register_dl);

void otx2_unregister_dl(struct otx2_nic *pfvf)
{
	struct otx2_devlink *otx2_dl = pfvf->dl;
	struct devlink *dl = otx2_dl->dl;

	devlink_unregister(dl);
	devlink_params_unregister(dl, otx2_dl_params,
				  ARRAY_SIZE(otx2_dl_params));
	devlink_free(dl);
}
EXPORT_SYMBOL(otx2_unregister_dl);
