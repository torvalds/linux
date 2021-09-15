/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright(C) 2020 Linaro Limited. All rights reserved.
 * Author: Mike Leach <mike.leach@linaro.org>
 */

/* declare preloaded configurations and features */

/* from coresight-cfg-afdo.c - etm 4x features */
#if IS_ENABLED(CONFIG_CORESIGHT_SOURCE_ETM4X)
extern struct cscfg_feature_desc strobe_etm4x;
extern struct cscfg_config_desc afdo_etm4x;
#endif
