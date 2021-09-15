// SPDX-License-Identifier: BSD-3-Clause OR GPL-2.0
/* Copyright (c) 2018 Mellanox Technologies. All rights reserved */

#include <linux/kernel.h>
#include <linux/err.h>
#include <linux/ethtool.h>
#include <linux/sfp.h>
#include <linux/mutex.h>

#include "core.h"
#include "core_env.h"
#include "item.h"
#include "reg.h"

struct mlxsw_env_module_info {
	u64 module_overheat_counter;
	bool is_overheat;
	int num_ports_mapped;
	int num_ports_up;
};

struct mlxsw_env {
	struct mlxsw_core *core;
	u8 module_count;
	struct mutex module_info_lock; /* Protects 'module_info'. */
	struct mlxsw_env_module_info module_info[];
};

static int mlxsw_env_validate_cable_ident(struct mlxsw_core *core, int id,
					  bool *qsfp, bool *cmis)
{
	char mcia_pl[MLXSW_REG_MCIA_LEN];
	char *eeprom_tmp;
	u8 ident;
	int err;

	mlxsw_reg_mcia_pack(mcia_pl, id, 0, MLXSW_REG_MCIA_PAGE0_LO_OFF, 0, 1,
			    MLXSW_REG_MCIA_I2C_ADDR_LOW);
	err = mlxsw_reg_query(core, MLXSW_REG(mcia), mcia_pl);
	if (err)
		return err;
	eeprom_tmp = mlxsw_reg_mcia_eeprom_data(mcia_pl);
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
	char mcia_pl[MLXSW_REG_MCIA_LEN];
	char *eeprom_tmp;
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

	eeprom_tmp = mlxsw_reg_mcia_eeprom_data(mcia_pl);
	memcpy(data, eeprom_tmp, size);
	*p_read_size = size;

	return 0;
}

int mlxsw_env_module_temp_thresholds_get(struct mlxsw_core *core, int module,
					 int off, int *temp)
{
	unsigned int module_temp, module_crit, module_emerg;
	union {
		u8 buf[MLXSW_REG_MCIA_TH_ITEM_SIZE];
		u16 temp;
	} temp_thresh;
	char mcia_pl[MLXSW_REG_MCIA_LEN] = {0};
	char mtmp_pl[MLXSW_REG_MTMP_LEN];
	char *eeprom_tmp;
	bool qsfp, cmis;
	int page;
	int err;

	mlxsw_reg_mtmp_pack(mtmp_pl, MLXSW_REG_MTMP_MODULE_INDEX_MIN + module,
			    false, false);
	err = mlxsw_reg_query(core, MLXSW_REG(mtmp), mtmp_pl);
	if (err)
		return err;
	mlxsw_reg_mtmp_unpack(mtmp_pl, &module_temp, NULL, &module_crit,
			      &module_emerg, NULL);
	if (!module_temp) {
		*temp = 0;
		return 0;
	}

	/* Validate if threshold reading is available through MTMP register,
	 * otherwise fallback to read through MCIA.
	 */
	if (module_emerg) {
		*temp = off == SFP_TEMP_HIGH_WARN ? module_crit : module_emerg;
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

	eeprom_tmp = mlxsw_reg_mcia_eeprom_data(mcia_pl);
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

static int mlxsw_env_mcia_status_process(const char *mcia_pl,
					 struct netlink_ext_ack *extack)
{
	u8 status = mlxsw_reg_mcia_status_get(mcia_pl);

	switch (status) {
	case MLXSW_REG_MCIA_STATUS_GOOD:
		return 0;
	case MLXSW_REG_MCIA_STATUS_NO_EEPROM_MODULE:
		NL_SET_ERR_MSG_MOD(extack, "No response from module's EEPROM");
		return -EIO;
	case MLXSW_REG_MCIA_STATUS_MODULE_NOT_SUPPORTED:
		NL_SET_ERR_MSG_MOD(extack, "Module type not supported by the device");
		return -EOPNOTSUPP;
	case MLXSW_REG_MCIA_STATUS_MODULE_NOT_CONNECTED:
		NL_SET_ERR_MSG_MOD(extack, "No module present indication");
		return -EIO;
	case MLXSW_REG_MCIA_STATUS_I2C_ERROR:
		NL_SET_ERR_MSG_MOD(extack, "Error occurred while trying to access module's EEPROM using I2C");
		return -EIO;
	case MLXSW_REG_MCIA_STATUS_MODULE_DISABLED:
		NL_SET_ERR_MSG_MOD(extack, "Module is disabled");
		return -EIO;
	default:
		NL_SET_ERR_MSG_MOD(extack, "Unknown error");
		return -EIO;
	}
}

int
mlxsw_env_get_module_eeprom_by_page(struct mlxsw_core *mlxsw_core, u8 module,
				    const struct ethtool_module_eeprom *page,
				    struct netlink_ext_ack *extack)
{
	u32 bytes_read = 0;
	u16 device_addr;

	/* Offset cannot be larger than 2 * ETH_MODULE_EEPROM_PAGE_LEN */
	device_addr = page->offset;

	while (bytes_read < page->length) {
		char mcia_pl[MLXSW_REG_MCIA_LEN];
		char *eeprom_tmp;
		u8 size;
		int err;

		size = min_t(u8, page->length - bytes_read,
			     MLXSW_REG_MCIA_EEPROM_SIZE);

		mlxsw_reg_mcia_pack(mcia_pl, module, 0, page->page,
				    device_addr + bytes_read, size,
				    page->i2c_address);
		mlxsw_reg_mcia_bank_number_set(mcia_pl, page->bank);

		err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mcia), mcia_pl);
		if (err) {
			NL_SET_ERR_MSG_MOD(extack, "Failed to access module's EEPROM");
			return err;
		}

		err = mlxsw_env_mcia_status_process(mcia_pl, extack);
		if (err)
			return err;

		eeprom_tmp = mlxsw_reg_mcia_eeprom_data(mcia_pl);
		memcpy(page->data + bytes_read, eeprom_tmp, size);
		bytes_read += size;
	}

	return bytes_read;
}
EXPORT_SYMBOL(mlxsw_env_get_module_eeprom_by_page);

static int mlxsw_env_module_has_temp_sensor(struct mlxsw_core *mlxsw_core,
					    u8 module,
					    bool *p_has_temp_sensor)
{
	char mtbr_pl[MLXSW_REG_MTBR_LEN];
	u16 temp;
	int err;

	mlxsw_reg_mtbr_pack(mtbr_pl, MLXSW_REG_MTBR_BASE_MODULE_INDEX + module,
			    1);
	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mtbr), mtbr_pl);
	if (err)
		return err;

	mlxsw_reg_mtbr_temp_unpack(mtbr_pl, 0, &temp, NULL);

	switch (temp) {
	case MLXSW_REG_MTBR_BAD_SENS_INFO:
	case MLXSW_REG_MTBR_NO_CONN:
	case MLXSW_REG_MTBR_NO_TEMP_SENS:
	case MLXSW_REG_MTBR_INDEX_NA:
		*p_has_temp_sensor = false;
		break;
	default:
		*p_has_temp_sensor = temp ? true : false;
	}
	return 0;
}

static int mlxsw_env_temp_event_set(struct mlxsw_core *mlxsw_core,
				    u16 sensor_index, bool enable)
{
	char mtmp_pl[MLXSW_REG_MTMP_LEN] = {0};
	enum mlxsw_reg_mtmp_tee tee;
	int err, threshold_hi;

	mlxsw_reg_mtmp_sensor_index_set(mtmp_pl, sensor_index);
	err = mlxsw_reg_query(mlxsw_core, MLXSW_REG(mtmp), mtmp_pl);
	if (err)
		return err;

	if (enable) {
		err = mlxsw_env_module_temp_thresholds_get(mlxsw_core,
							   sensor_index -
							   MLXSW_REG_MTMP_MODULE_INDEX_MIN,
							   SFP_TEMP_HIGH_WARN,
							   &threshold_hi);
		/* In case it is not possible to query the module's threshold,
		 * use the default value.
		 */
		if (err)
			threshold_hi = MLXSW_REG_MTMP_THRESH_HI;
		else
			/* mlxsw_env_module_temp_thresholds_get() multiplies
			 * Celsius degrees by 1000 whereas MTMP expects
			 * temperature in 0.125 Celsius degrees units.
			 * Convert threshold_hi to correct units.
			 */
			threshold_hi = threshold_hi / 1000 * 8;

		mlxsw_reg_mtmp_temperature_threshold_hi_set(mtmp_pl, threshold_hi);
		mlxsw_reg_mtmp_temperature_threshold_lo_set(mtmp_pl, threshold_hi -
							    MLXSW_REG_MTMP_HYSTERESIS_TEMP);
	}
	tee = enable ? MLXSW_REG_MTMP_TEE_GENERATE_EVENT : MLXSW_REG_MTMP_TEE_NO_EVENT;
	mlxsw_reg_mtmp_tee_set(mtmp_pl, tee);
	return mlxsw_reg_write(mlxsw_core, MLXSW_REG(mtmp), mtmp_pl);
}

static int mlxsw_env_module_temp_event_enable(struct mlxsw_core *mlxsw_core,
					      u8 module_count)
{
	int i, err, sensor_index;
	bool has_temp_sensor;

	for (i = 0; i < module_count; i++) {
		err = mlxsw_env_module_has_temp_sensor(mlxsw_core, i,
						       &has_temp_sensor);
		if (err)
			return err;

		if (!has_temp_sensor)
			continue;

		sensor_index = i + MLXSW_REG_MTMP_MODULE_INDEX_MIN;
		err = mlxsw_env_temp_event_set(mlxsw_core, sensor_index, true);
		if (err)
			return err;
	}

	return 0;
}

struct mlxsw_env_module_temp_warn_event {
	struct mlxsw_env *mlxsw_env;
	char mtwe_pl[MLXSW_REG_MTWE_LEN];
	struct work_struct work;
};

static void mlxsw_env_mtwe_event_work(struct work_struct *work)
{
	struct mlxsw_env_module_temp_warn_event *event;
	struct mlxsw_env *mlxsw_env;
	int i, sensor_warning;
	bool is_overheat;

	event = container_of(work, struct mlxsw_env_module_temp_warn_event,
			     work);
	mlxsw_env = event->mlxsw_env;

	for (i = 0; i < mlxsw_env->module_count; i++) {
		/* 64-127 of sensor_index are mapped to the port modules
		 * sequentially (module 0 is mapped to sensor_index 64,
		 * module 1 to sensor_index 65 and so on)
		 */
		sensor_warning =
			mlxsw_reg_mtwe_sensor_warning_get(event->mtwe_pl,
							  i + MLXSW_REG_MTMP_MODULE_INDEX_MIN);
		mutex_lock(&mlxsw_env->module_info_lock);
		is_overheat =
			mlxsw_env->module_info[i].is_overheat;

		if ((is_overheat && sensor_warning) ||
		    (!is_overheat && !sensor_warning)) {
			/* Current state is "warning" and MTWE still reports
			 * warning OR current state in "no warning" and MTWE
			 * does not report warning.
			 */
			mutex_unlock(&mlxsw_env->module_info_lock);
			continue;
		} else if (is_overheat && !sensor_warning) {
			/* MTWE reports "no warning", turn is_overheat off.
			 */
			mlxsw_env->module_info[i].is_overheat = false;
			mutex_unlock(&mlxsw_env->module_info_lock);
		} else {
			/* Current state is "no warning" and MTWE reports
			 * "warning", increase the counter and turn is_overheat
			 * on.
			 */
			mlxsw_env->module_info[i].is_overheat = true;
			mlxsw_env->module_info[i].module_overheat_counter++;
			mutex_unlock(&mlxsw_env->module_info_lock);
		}
	}

	kfree(event);
}

static void
mlxsw_env_mtwe_listener_func(const struct mlxsw_reg_info *reg, char *mtwe_pl,
			     void *priv)
{
	struct mlxsw_env_module_temp_warn_event *event;
	struct mlxsw_env *mlxsw_env = priv;

	event = kmalloc(sizeof(*event), GFP_ATOMIC);
	if (!event)
		return;

	event->mlxsw_env = mlxsw_env;
	memcpy(event->mtwe_pl, mtwe_pl, MLXSW_REG_MTWE_LEN);
	INIT_WORK(&event->work, mlxsw_env_mtwe_event_work);
	mlxsw_core_schedule_work(&event->work);
}

static const struct mlxsw_listener mlxsw_env_temp_warn_listener =
	MLXSW_EVENTL(mlxsw_env_mtwe_listener_func, MTWE, MTWE);

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

struct mlxsw_env_module_plug_unplug_event {
	struct mlxsw_env *mlxsw_env;
	u8 module;
	struct work_struct work;
};

static void mlxsw_env_pmpe_event_work(struct work_struct *work)
{
	struct mlxsw_env_module_plug_unplug_event *event;
	struct mlxsw_env *mlxsw_env;
	bool has_temp_sensor;
	u16 sensor_index;
	int err;

	event = container_of(work, struct mlxsw_env_module_plug_unplug_event,
			     work);
	mlxsw_env = event->mlxsw_env;

	mutex_lock(&mlxsw_env->module_info_lock);
	mlxsw_env->module_info[event->module].is_overheat = false;
	mutex_unlock(&mlxsw_env->module_info_lock);

	err = mlxsw_env_module_has_temp_sensor(mlxsw_env->core, event->module,
					       &has_temp_sensor);
	/* Do not disable events on modules without sensors or faulty sensors
	 * because FW returns errors.
	 */
	if (err)
		goto out;

	if (!has_temp_sensor)
		goto out;

	sensor_index = event->module + MLXSW_REG_MTMP_MODULE_INDEX_MIN;
	mlxsw_env_temp_event_set(mlxsw_env->core, sensor_index, true);

out:
	kfree(event);
}

static void
mlxsw_env_pmpe_listener_func(const struct mlxsw_reg_info *reg, char *pmpe_pl,
			     void *priv)
{
	struct mlxsw_env_module_plug_unplug_event *event;
	enum mlxsw_reg_pmpe_module_status module_status;
	u8 module = mlxsw_reg_pmpe_module_get(pmpe_pl);
	struct mlxsw_env *mlxsw_env = priv;

	if (WARN_ON_ONCE(module >= mlxsw_env->module_count))
		return;

	module_status = mlxsw_reg_pmpe_module_status_get(pmpe_pl);
	if (module_status != MLXSW_REG_PMPE_MODULE_STATUS_PLUGGED_ENABLED)
		return;

	event = kmalloc(sizeof(*event), GFP_ATOMIC);
	if (!event)
		return;

	event->mlxsw_env = mlxsw_env;
	event->module = module;
	INIT_WORK(&event->work, mlxsw_env_pmpe_event_work);
	mlxsw_core_schedule_work(&event->work);
}

static const struct mlxsw_listener mlxsw_env_module_plug_listener =
	MLXSW_EVENTL(mlxsw_env_pmpe_listener_func, PMPE, PMPE);

static int
mlxsw_env_module_plug_event_register(struct mlxsw_core *mlxsw_core)
{
	struct mlxsw_env *mlxsw_env = mlxsw_core_env(mlxsw_core);

	if (!mlxsw_core_temp_warn_enabled(mlxsw_core))
		return 0;

	return mlxsw_core_trap_register(mlxsw_core,
					&mlxsw_env_module_plug_listener,
					mlxsw_env);
}

static void
mlxsw_env_module_plug_event_unregister(struct mlxsw_env *mlxsw_env)
{
	if (!mlxsw_core_temp_warn_enabled(mlxsw_env->core))
		return;

	mlxsw_core_trap_unregister(mlxsw_env->core,
				   &mlxsw_env_module_plug_listener,
				   mlxsw_env);
}

static int
mlxsw_env_module_oper_state_event_enable(struct mlxsw_core *mlxsw_core,
					 u8 module_count)
{
	int i, err;

	for (i = 0; i < module_count; i++) {
		char pmaos_pl[MLXSW_REG_PMAOS_LEN];

		mlxsw_reg_pmaos_pack(pmaos_pl, i,
				     MLXSW_REG_PMAOS_E_GENERATE_EVENT);
		err = mlxsw_reg_write(mlxsw_core, MLXSW_REG(pmaos), pmaos_pl);
		if (err)
			return err;
	}
	return 0;
}

int
mlxsw_env_module_overheat_counter_get(struct mlxsw_core *mlxsw_core, u8 module,
				      u64 *p_counter)
{
	struct mlxsw_env *mlxsw_env = mlxsw_core_env(mlxsw_core);

	if (WARN_ON_ONCE(module >= mlxsw_env->module_count))
		return -EINVAL;

	mutex_lock(&mlxsw_env->module_info_lock);
	*p_counter = mlxsw_env->module_info[module].module_overheat_counter;
	mutex_unlock(&mlxsw_env->module_info_lock);

	return 0;
}
EXPORT_SYMBOL(mlxsw_env_module_overheat_counter_get);

void mlxsw_env_module_port_map(struct mlxsw_core *mlxsw_core, u8 module)
{
	struct mlxsw_env *mlxsw_env = mlxsw_core_env(mlxsw_core);

	if (WARN_ON_ONCE(module >= mlxsw_env->module_count))
		return;

	mutex_lock(&mlxsw_env->module_info_lock);
	mlxsw_env->module_info[module].num_ports_mapped++;
	mutex_unlock(&mlxsw_env->module_info_lock);
}
EXPORT_SYMBOL(mlxsw_env_module_port_map);

void mlxsw_env_module_port_unmap(struct mlxsw_core *mlxsw_core, u8 module)
{
	struct mlxsw_env *mlxsw_env = mlxsw_core_env(mlxsw_core);

	if (WARN_ON_ONCE(module >= mlxsw_env->module_count))
		return;

	mutex_lock(&mlxsw_env->module_info_lock);
	mlxsw_env->module_info[module].num_ports_mapped--;
	mutex_unlock(&mlxsw_env->module_info_lock);
}
EXPORT_SYMBOL(mlxsw_env_module_port_unmap);

int mlxsw_env_module_port_up(struct mlxsw_core *mlxsw_core, u8 module)
{
	struct mlxsw_env *mlxsw_env = mlxsw_core_env(mlxsw_core);

	if (WARN_ON_ONCE(module >= mlxsw_env->module_count))
		return -EINVAL;

	mutex_lock(&mlxsw_env->module_info_lock);
	mlxsw_env->module_info[module].num_ports_up++;
	mutex_unlock(&mlxsw_env->module_info_lock);

	return 0;
}
EXPORT_SYMBOL(mlxsw_env_module_port_up);

void mlxsw_env_module_port_down(struct mlxsw_core *mlxsw_core, u8 module)
{
	struct mlxsw_env *mlxsw_env = mlxsw_core_env(mlxsw_core);

	if (WARN_ON_ONCE(module >= mlxsw_env->module_count))
		return;

	mutex_lock(&mlxsw_env->module_info_lock);
	mlxsw_env->module_info[module].num_ports_up--;
	mutex_unlock(&mlxsw_env->module_info_lock);
}
EXPORT_SYMBOL(mlxsw_env_module_port_down);

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

	mutex_init(&env->module_info_lock);
	env->core = mlxsw_core;
	env->module_count = module_count;
	*p_env = env;

	err = mlxsw_env_temp_warn_event_register(mlxsw_core);
	if (err)
		goto err_temp_warn_event_register;

	err = mlxsw_env_module_plug_event_register(mlxsw_core);
	if (err)
		goto err_module_plug_event_register;

	err = mlxsw_env_module_oper_state_event_enable(mlxsw_core,
						       env->module_count);
	if (err)
		goto err_oper_state_event_enable;

	err = mlxsw_env_module_temp_event_enable(mlxsw_core, env->module_count);
	if (err)
		goto err_temp_event_enable;

	return 0;

err_temp_event_enable:
err_oper_state_event_enable:
	mlxsw_env_module_plug_event_unregister(env);
err_module_plug_event_register:
	mlxsw_env_temp_warn_event_unregister(env);
err_temp_warn_event_register:
	mutex_destroy(&env->module_info_lock);
	kfree(env);
	return err;
}

void mlxsw_env_fini(struct mlxsw_env *env)
{
	mlxsw_env_module_plug_event_unregister(env);
	/* Make sure there is no more event work scheduled. */
	mlxsw_core_flush_owq();
	mlxsw_env_temp_warn_event_unregister(env);
	mutex_destroy(&env->module_info_lock);
	kfree(env);
}
