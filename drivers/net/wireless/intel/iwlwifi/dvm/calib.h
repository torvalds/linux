/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2005-2014 Intel Corporation
 */
#ifndef __iwl_calib_h__
#define __iwl_calib_h__

#include "dev.h"
#include "commands.h"

void iwl_chain_noise_calibration(struct iwl_priv *priv);
void iwl_sensitivity_calibration(struct iwl_priv *priv);

void iwl_init_sensitivity(struct iwl_priv *priv);
void iwl_reset_run_time_calib(struct iwl_priv *priv);

#endif /* __iwl_calib_h__ */
