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

/* preload called on initialisation */
int cscfg_preload(void)
{
	return cscfg_load_config_sets(preload_cfgs, preload_feats);
}
