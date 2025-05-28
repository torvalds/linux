/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024 Intel Corporation
 */
#ifndef __iwl_mld_led_h__
#define __iwl_mld_led_h__

#include "mld.h"

#ifdef CONFIG_IWLWIFI_LEDS
int iwl_mld_leds_init(struct iwl_mld *mld);
void iwl_mld_leds_exit(struct iwl_mld *mld);
void iwl_mld_led_config_fw(struct iwl_mld *mld);
#else
static inline int iwl_mld_leds_init(struct iwl_mld *mld)
{
	return 0;
}

static inline void iwl_mld_leds_exit(struct iwl_mld *mld)
{
}

static inline void iwl_mld_led_config_fw(struct iwl_mld *mld)
{
}
#endif

#endif /* __iwl_mld_led_h__ */
