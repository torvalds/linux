// SPDX-License-Identifier: GPL-2.0-or-later
#include <linux/types.h>

int aicwf_patch_table_load(struct rwnx_hw *rwnx_hw, char *filename);
void aicwf_patch_config_8800dc(struct rwnx_hw *rwnx_hw);
int aicwf_set_rf_config_8800dc(struct rwnx_hw *rwnx_hw, struct mm_set_rf_calib_cfm *cfm);
int aicwf_plat_patch_load_8800dc(struct rwnx_hw *rwnx_hw);
int	rwnx_plat_userconfig_load_8800dc(struct rwnx_hw *rwnx_hw);
void system_config_8800dc(struct rwnx_hw *rwnx_hw);


