/* SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0 */
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#ifndef _MLXSW_CORE_ENV_H
#define _MLXSW_CORE_ENV_H

struct ethtool_modinfo;
struct ethtool_eeprom;

int mlxsw_env_module_temp_thresholds_get(struct mlxsw_core *core, int module,
					 int off, int *temp);

int mlxsw_env_get_module_info(struct mlxsw_core *mlxsw_core, int module,
			      struct ethtool_modinfo *modinfo);

int mlxsw_env_get_module_eeprom(struct net_device *netdev,
				struct mlxsw_core *mlxsw_core, int module,
				struct ethtool_eeprom *ee, u8 *data);

int
mlxsw_env_module_overheat_counter_get(struct mlxsw_core *mlxsw_core, u8 module,
				      u64 *p_counter);
int mlxsw_env_init(struct mlxsw_core *core, struct mlxsw_env **p_env);
void mlxsw_env_fini(struct mlxsw_env *env);

#endif
