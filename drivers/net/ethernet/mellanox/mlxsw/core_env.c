// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/sfp.h>

#include "core.h"
#include "core_env.h"
#include "item.h"
#include "reg.h"

struct mlxsw_env_module_info {
	u64 module_overheat_counter;
	bool is_overheat;
};

struct mlxsw_env {
	struct mlxsw_core *core;
	u8 module_count;
	spinlock_t module_info_lock; /* Protects 'module_info'. */
	struct mlxsw_env_module_info module_info[];
};

static int mlxsw_env_validate_cable_ident(struct mlxsw_core *core, int id,
					  bool *qsfp, bool *cmis)
{
	char eeprom_tmp[MLXSW_REG_MCIA_EEPROM_SIZE];
	char mcia_pl[MLXSW_REG_MCIA_LEN];
	u8 ident;
	int err;

	mlxsw_reg_mcia_pack(mcia_pl, id, 0, MLXSW_REG_MCIA_PAGE0_LO_OFF, 0, 1,
			    MLXSW_REG_MCIA_I2C_ADDR_LOW);
	err = mlxsw_reg_query(core, MLXSW_REG(mcia), mcia_pl);
	if (err)
		return err;
	mlxsw_reg_mcia_eeprom_memcpy_from(mcia_pl, eeprom_tmp);
	ident = eeprom_tmp[0];
	*cmis = false;
	switch (ident) {
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_SFP:
		*qsfp = false;
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP:
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_PLUS:
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP28:
		*qsfp = true;
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_DD:
		*qsfp = true;
		*cmis = true;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}

static int
mlxsw_env_query_module_eeprom(struct mlxsw_core *mlxsw_core, int module,
			      u16 offset, u16 size, void *data,
			      bool qsfp, unsigned int *p_read_size)
{
	char eeprom_tmp[MLXSW_REG_MCIA_EEPROM_SIZE];
	char mcia_pl[MLXSW_REG_MCIA_LEN];
	u16 i2c_addr;
	u8 page = 0;
	int status;
	int err;

	/* MCIA register accepts buffer size <= 48. Page of size 128 should be
	 * read by chunks of size 48, 48, 32. Align the size of the last chunk
	 * to avoid reading after the end of the page.
	 */
	size = min_t(u16, size, MLXSW_REG_MCIA_EEPROM_SIZE);

	if (offset < MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH &&
	    offset + size > MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH)
		/* Cross pages read, read until offset 256 in low page */
		size = MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH - offset;

	i2c_addr = MLXSW_REG_MCIA_I2C_ADDR_LOW;
	if (offset >= MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH) {
		if (qsfp) {
			/* When reading upper pages 1, 2 and 3 the offset
			 * starts at 128. Please refer to "QSFP+ Memory Map"
			 * figure in SFF-8436 specification and to "CMIS Module
			 * Memory Map" figure in CMIS specification for
			 * graphical depiction.
			 */
			page = MLXSW_REG_MCIA_PAGE_GET(offset);
			offset -= MLXSW_REG_MCIA_EEPROM_UP_PAGE_LENGTH * page;
			if (offset + size > MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH)
				size = MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH - offset;
		} else {
			/* When reading upper pages 1, 2 and 3 the offset
			 * starts at 0 and I2C high address is used. Please refer
			 * refer to "Memory Organization" figure in SFF-8472
			 * specification for graphical depiction.
			 */
			i2c_addr = MLXSW_REG_MCIA_I2C_ADDR_HIGH;
			offset -= MLXSW_REG_MCIA_EEPROM_PAGE_LENGTH;
		}
	}

	mlxsw_reg_mcia_pack(mcia_pl, module, 0, page, offset, size, i2c_addr);

	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mcia), mcia_pl);
	if (err)
		return err;

	status = mlxsw_reg_mcia_status_get(mcia_pl);
	if (status)
		return -EIO;

	mlxsw_reg_mcia_eeprom_memcpy_from(mcia_pl, eeprom_tmp);
	memcpy(data, eeprom_tmp, size);
	*p_read_size = size;

	return 0;
}

int mlxsw_env_module_temp_thresholds_get(struct mlxsw_core *core, int module,
					 int off, int *temp)
{
	char eeprom_tmp[MLXSW_REG_MCIA_EEPROM_SIZE];
	union {
		u8 buf[MLXSW_REG_MCIA_TH_ITEM_SIZE];
		u16 temp;
	} temp_thresh;
	char mcia_pl[MLXSW_REG_MCIA_LEN] = {0};
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	unsigned int module_temp;
	bool qsfp, cmis;
	int page;
	int err;

	mlxsw_reg_mtmp_pack(mtmp_pl, MLXSW_REG_MTMP_MODULE_INDEX_MIN + module,
			    false, false);
	err = mlxsw_reg_query(core, MLXSW_REG(mtmp), mtmp_pl);
	if (err)
		return err;
	mlxsw_reg_mtmp_unpack(mtmp_pl, &module_temp, NULL, NULL);
	if (!module_temp) {
		*temp = 0;
		return 0;
	}

	/* Read Free Side Device Temperature Thresholds from page 03h
	 * (MSB at lower byte address).
	 * Bytes:
	 * 128-129 - Temp High Alarm (SFP_TEMP_HIGH_ALARM);
	 * 130-131 - Temp Low Alarm (SFP_TEMP_LOW_ALARM);
	 * 132-133 - Temp High Warning (SFP_TEMP_HIGH_WARN);
	 * 134-135 - Temp Low Warning (SFP_TEMP_LOW_WARN);
	 */

	/* Validate module identifier value. */
	err = mlxsw_env_validate_cable_ident(core, module, &qsfp, &cmis);
	if (err)
		return err;

	if (qsfp) {
		/* For QSFP/CMIS module-defined thresholds are located in page
		 * 02h, otherwise in page 03h.
		 */
		if (cmis)
			page = MLXSW_REG_MCIA_TH_PAGE_CMIS_NUM;
		else
			page = MLXSW_REG_MCIA_TH_PAGE_NUM;
		mlxsw_reg_mcia_pack(mcia_pl, module, 0, page,
				    MLXSW_REG_MCIA_TH_PAGE_OFF + off,
				    MLXSW_REG_MCIA_TH_ITEM_SIZE,
				    MLXSW_REG_MCIA_I2C_ADDR_LOW);
	} else {
		mlxsw_reg_mcia_pack(mcia_pl, module, 0,
				    MLXSW_REG_MCIA_PAGE0_LO,
				    off, MLXSW_REG_MCIA_TH_ITEM_SIZE,
				    MLXSW_REG_MCIA_I2C_ADDR_HIGH);
	}

	err = mlxsw_reg_query(core, MLXSW_REG(mcia), mcia_pl);
	if (err)
		return err;

	mlxsw_reg_mcia_eeprom_memcpy_from(mcia_pl, eeprom_tmp);
	memcpy(temp_thresh.buf, eeprom_tmp, MLXSW_REG_MCIA_TH_ITEM_SIZE);
	*temp = temp_thresh.temp * 1000;

	return 0;
}

int mlxsw_env_get_module_info(struct mlxsw_core *mlxsw_core, int module,
			      struct ethtool_modinfo *modinfo)
{
	u8 module_info[MLXSW_REG_MCIA_EEPROM_MODULE_INFO_SIZE];
	u16 offset = MLXSW_REG_MCIA_EEPROM_MODULE_INFO_SIZE;
	u8 module_rev_id, module_id, diag_mon;
	unsigned int read_size;
	int err;

	err = mlxsw_env_query_module_eeprom(mlxsw_core, module, 0, offset,
					    module_info, false, &read_size);
	if (err)
		return err;

	if (read_size < offset)
		return -EIO;

	module_rev_id = module_info[MLXSW_REG_MCIA_EEPROM_MODULE_INFO_REV_ID];
	module_id = module_info[MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID];

	switch (module_id) {
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP:
		modinfo->type       = ETH_MODULE_SFF_8436;
		modinfo->eeprom_len = ETH_MODULE_SFF_8436_MAX_LEN;
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_PLUS:
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP28:
		if (module_id == MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP28 ||
		    module_rev_id >=
		    MLXSW_REG_MCIA_EEPROM_MODULE_INFO_REV_ID_8636) {
			modinfo->type       = ETH_MODULE_SFF_8636;
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_MAX_LEN;
		} else {
			modinfo->type       = ETH_MODULE_SFF_8436;
			modinfo->eeprom_len = ETH_MODULE_SFF_8436_MAX_LEN;
		}
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_SFP:
		/* Verify if transceiver provides diagnostic monitoring page */
		err = mlxsw_env_query_module_eeprom(mlxsw_core, module,
						    SFP_DIAGMON, 1, &diag_mon,
						    false, &read_size);
		if (err)
			return err;

		if (read_size < 1)
			return -EIO;

		modinfo->type       = ETH_MODULE_SFF_8472;
		if (diag_mon)
			modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		else
			modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN / 2;
		break;
	case MLXSW_REG_MCIA_EEPROM_MODULE_INFO_ID_QSFP_DD:
		/* Use SFF_8636 as base type. ethtool should recognize specific
		 * type through the identifier value.
		 */
		modinfo->type       = ETH_MODULE_SFF_8636;
		/* Verify if module EEPROM is a flat memory. In case of flat
		 * memory only page 00h (0-255 bytes) can be read. Otherwise
		 * upper pages 01h and 02h can also be read. Upper pages 10h
		 * and 11h are currently not supported by the driver.
		 */
		if (module_info[MLXSW_REG_MCIA_EEPROM_MODULE_INFO_TYPE_ID] &
		    MLXSW_REG_MCIA_EEPROM_CMIS_FLAT_MEMORY)
			modinfo->eeprom_len = ETH_MODULE_SFF_8636_LEN;
		else
			modinfo->eeprom_len = ETH_MODULE_SFF_8472_LEN;
		break;
	default:
		return -EINVAL;
	}

	return 0;
}
EXPORT_SYMBOL(mlxsw_env_get_module_info);

int mlxsw_env_get_module_eeprom(struct net_device *netdev,
				struct mlxsw_core *mlxsw_core, int module,
				struct ethtool_eeprom *ee, u8 *data)
{
	int offset = ee->offset;
	unsigned int read_size;
	bool qsfp, cmis;
	int i = 0;
	int err;

	if (!ee->len)
		return -EINVAL;

	memset(data, 0, ee->len);
	/* Validate module identifier value. */
	err = mlxsw_env_validate_cable_ident(mlxsw_core, module, &qsfp, &cmis);
	if (err)
		return err;

	while (i < ee->len) {
		err = mlxsw_env_query_module_eeprom(mlxsw_core, module, offset,
						    ee->len - i, data + i,
						    qsfp, &read_size);
		if (err) {
			netdev_err(netdev, "Eeprom query failed\n");
			return err;
		}

		i += read_size;
		offset += read_size;
	}

	return 0;
}
EXPORT_SYMBOL(mlxsw_env_get_module_eeprom);

static void mlxsw_env_mtwe_event_func(const struct mlxsw_reg_info *reg,
				      char *mtwe_pl, void *priv)
{
	struct mlxsw_env *mlxsw_env = priv;
	int i, sensor_warning;
	bool is_overheat;

	for (i = 0; i < mlxsw_env->module_count; i++) {
		/* 64-127 of sensor_index are mapped to the port modules
		 * sequentially (module 0 is mapped to sensor_index 64,
		 * module 1 to sensor_index 65 and so on)
		 */
		sensor_warning =
			mlxsw_reg_mtwe_sensor_warning_get(mtwe_pl,
							  i + MLXSW_REG_MTMP_MODULE_INDEX_MIN);
		spin_lock(&mlxsw_env->module_info_lock);
		is_overheat =
			mlxsw_env->module_info[i].is_overheat;

		if ((is_overheat && sensor_warning) ||
		    (!is_overheat && !sensor_warning)) {
			/* Current state is "warning" and MTWE still reports
			 * warning OR current state in "no warning" and MTWE
			 * does not report warning.
			 */
			spin_unlock(&mlxsw_env->module_info_lock);
			continue;
		} else if (is_overheat && !sensor_warning) {
			/* MTWE reports "no warning", turn is_overheat off.
			 */
			mlxsw_env->module_info[i].is_overheat = false;
			spin_unlock(&mlxsw_env->module_info_lock);
		} else {
			/* Current state is "no warning" and MTWE reports
			 * "warning", increase the counter and turn is_overheat
			 * on.
			 */
			mlxsw_env->module_info[i].is_overheat = true;
			mlxsw_env->module_info[i].module_overheat_counter++;
			spin_unlock(&mlxsw_env->module_info_lock);
		}
	}
}

static const struct mlxsw_listener mlxsw_env_temp_warn_listener =
	MLXSW_EVENTL(mlxsw_env_mtwe_event_func, MTWE, MTWE);

static int mlxsw_env_temp_warn_event_register(struct mlxsw_core *mlxsw_core)
{
	struct mlxsw_env *mlxsw_env = mlxsw_core_env(mlxsw_core);

	if (!mlxsw_core_temp_warn_enabled(mlxsw_core))
		return 0;

	return mlxsw_core_trap_register(mlxsw_core,
					&mlxsw_env_temp_warn_listener,
					mlxsw_env);
}

static void mlxsw_env_temp_warn_event_unregister(struct mlxsw_env *mlxsw_env)
{
	if (!mlxsw_core_temp_warn_enabled(mlxsw_env->core))
		return;

	mlxsw_core_trap_unregister(mlxsw_env->core,
				   &mlxsw_env_temp_warn_listener, mlxsw_env);
}

int
mlxsw_env_module_overheat_counter_get(struct mlxsw_core *mlxsw_core, u8 module,
				      u64 *p_counter)
{
	struct mlxsw_env *mlxsw_env = mlxsw_core_env(mlxsw_core);

	/* Prevent switch driver from accessing uninitialized data. */
	if (!mlxsw_core_is_initialized(mlxsw_core)) {
		*p_counter = 0;
		return 0;
	}

	if (WARN_ON_ONCE(module >= mlxsw_env->module_count))
		return -EINVAL;

	spin_lock_bh(&mlxsw_env->module_info_lock);
	*p_counter = mlxsw_env->module_info[module].module_overheat_counter;
	spin_unlock_bh(&mlxsw_env->module_info_lock);

	return 0;
}
EXPORT_SYMBOL(mlxsw_env_module_overheat_counter_get);

int mlxsw_env_init(struct mlxsw_core *mlxsw_core, struct mlxsw_env **p_env)
{
	char mgpir_pl[MLXSW_REG_MGPIR_LEN];
	struct mlxsw_env *env;
	u8 module_count;
	int err;

	mlxsw_reg_mgpir_pack(mgpir_pl);
	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mgpir), mgpir_pl);
	if (err)
		return err;

	mlxsw_reg_mgpir_unpack(mgpir_pl, NULL, NULL, NULL, &module_count);

	env = kzalloc(struct_size(env, module_info, module_count), GFP_KERNEL);
	if (!env)
		return -ENOMEM;

	spin_lock_init(&env->module_info_lock);
	env->core = mlxsw_core;
	env->module_count = module_count;
	*p_env = env;

	err = mlxsw_env_temp_warn_event_register(mlxsw_core);
	if (err)
		goto err_temp_warn_event_register;

	return 0;

err_temp_warn_event_register:
	kfree(env);
	return err;
}

void mlxsw_env_fini(struct mlxsw_env *env)
{
	mlxsw_env_temp_warn_event_unregister(env);
	kfree(env);
}
