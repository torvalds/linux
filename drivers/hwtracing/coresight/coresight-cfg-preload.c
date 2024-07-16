// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright(C) 2020 Linaro Limited. All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

#include "coresight-cfg-preload.h"
#include "coresight-config.h"
#include "coresight-syscfg.h"

/* Basic features and configurations pre-loaded on initialisation */

static struct cscfg_feature_desc *preload_feats[] = {
#if IS_ENABLED(CONFIG_CORESIGHT_SOURCE_ETM4X)
	&strobe_etm4x,
#endif
	NULL
};

static struct cscfg_config_desc *preload_cfgs[] = {
#if IS_ENABLED(CONFIG_CORESIGHT_SOURCE_ETM4X)
	&afdo_etm4x,
#endif
	NULL
};

static struct cscfg_load_owner_info preload_owner = {
	.type = CSCFG_OWNER_PRELOAD,
};

/* preload called on initialisation */
int cscfg_preload(void *owner_handle)
{
	preload_owner.owner_handle = owner_handle;
	return cscfg_load_config_sets(preload_cfgs, preload_feats, &preload_owner);
}
