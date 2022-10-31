/**
 ******************************************************************************
 *
 * @file ecrnx_main.h
 *
 * Copyright (C) ESWIN 2015-2020
 *
 ******************************************************************************
 */

#ifndef _ECRNX_MAIN_H_
#define _ECRNX_MAIN_H_

#include "ecrnx_defs.h"

int ecrnx_cfg80211_init(struct ecrnx_plat *ecrnx_plat, void **platform_data);
void ecrnx_cfg80211_deinit(struct ecrnx_hw *ecrnx_hw);

#endif /* _ECRNX_MAIN_H_ */
