/* SPDX-License-Identifier: GPL-2.0 OR BSD-3-Clause */
/*
 * Copyright (C) 2024-2025 Intel Corporation
 */
#ifndef __iwl_mld_regulatory_h__
#define __iwl_mld_regulatory_h__

#include "mld.h"

void iwl_mld_get_bios_tables(struct iwl_mld *mld);
void iwl_mld_configure_lari(struct iwl_mld *mld);
void iwl_mld_init_uats(struct iwl_mld *mld);
void iwl_mld_init_tas(struct iwl_mld *mld);

int iwl_mld_init_ppag(struct iwl_mld *mld);

int iwl_mld_init_sgom(struct iwl_mld *mld);

int iwl_mld_init_sar(struct iwl_mld *mld);

int iwl_mld_config_sar_profile(struct iwl_mld *mld, int prof_a, int prof_b);

#endif /* __iwl_mld_regulatory_h__ */
