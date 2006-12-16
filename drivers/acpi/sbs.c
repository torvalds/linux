/*
 *  acpi_sbs.c - ACPI Smart Battery System Driver ($Revision: 1.16 $)
 *
 *  Copyright (c) 2005 Rich Townsend <rhdt@bartol.udel.edu>
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or (at
 *  your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful, but
 *  WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 *  General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License along
 *  with this program; if not, write to the Free Software Foundation, Inc.,
 *  59 Temple Place, Suite 330, Boston, MA 02111-1307 USA.
 *
 * ~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
 */

#include <linux/init.h>
#include <linux/module.h>
#include <linux/moduleparam.h>
#include <linux/kernel.h>
#include <linux/proc_fs.h>
#include <linux/seq_file.h>
#include <asm/uaccess.h>
#include <linux/acpi.h>
#include <linux/i2c.h>
#include <linux/delay.h>

#include "i2c_ec.h"

#define	DEF_CAPACITY_UNIT	3
#define	MAH_CAPACITY_UNIT	1
#define	MWH_CAPACITY_UNIT	2
#define	CAPACITY_UNIT		DEF_CAPACITY_UNIT

#define	REQUEST_UPDATE_MODE	1
#define	QUEUE_UPDATE_MODE	2

#define	DATA_TYPE_COMMON	0
#define	DATA_TYPE_INFO		1
#define	DATA_TYPE_STATE		2
#define	DATA_TYPE_ALARM		3
#define	DATA_TYPE_AC_STATE	4

extern struct proc_dir_entry *acpi_lock_ac_dir(void);
extern struct proc_dir_entry *acpi_lock_battery_dir(void);
extern void acpi_unlock_ac_dir(struct proc_dir_entry *acpi_ac_dir);
extern void acpi_unlock_battery_dir(struct proc_dir_entry *acpi_battery_dir);

#define ACPI_SBS_COMPONENT		0x00080000
#define ACPI_SBS_CLASS			"sbs"
#define ACPI_AC_CLASS			"ac_adapter"
#define ACPI_BATTERY_CLASS		"battery"
#define ACPI_SBS_HID			"ACPI0002"
#define ACPI_SBS_DRIVER_NAME		"ACPI Smart Battery System Driver"
#define ACPI_SBS_DEVICE_NAME		"Smart Battery System"
#define ACPI_SBS_FILE_INFO		"info"
#define ACPI_SBS_FILE_STATE		"state"
#define ACPI_SBS_FILE_ALARM		"alarm"
#define ACPI_BATTERY_DIR_NAME		"BAT%i"
#define ACPI_AC_DIR_NAME		"AC0"
#define ACPI_SBC_SMBUS_ADDR		0x9
#define ACPI_SBSM_SMBUS_ADDR		0xa
#define ACPI_SB_SMBUS_ADDR		0xb
#define ACPI_SBS_AC_NOTIFY_STATUS	0x80
#define ACPI_SBS_BATTERY_NOTIFY_STATUS	0x80
#define ACPI_SBS_BATTERY_NOTIFY_INFO	0x81

#define _COMPONENT			ACPI_SBS_COMPONENT

#define	MAX_SBS_BAT			4
#define	MAX_SMBUS_ERR			1

ACPI_MODULE_NAME("acpi_sbs");

MODULE_AUTHOR("Rich Townsend");
MODULE_DESCRIPTION("Smart Battery System ACPI interface driver");
MODULE_LICENSE("GPL");

static struct semaphore sbs_sem;

#define	UPDATE_MODE		QUEUE_UPDATE_MODE
/* REQUEST_UPDATE_MODE  QUEUE_UPDATE_MODE */
#define	UPDATE_INFO_MODE	0
#define	UPDATE_TIME		60
#define	UPDATE_TIME2		0

static int capacity_mode = CAPACITY_UNIT;
static int update_mode = UPDATE_MODE;
static int update_info_mode = UPDATE_INFO_MODE;
static int update_time = UPDATE_TIME;
static int update_time2 = UPDATE_TIME2;

module_param(capacity_mode, int, 0);
module_param(update_mode, int, 0);
module_param(update_info_mode, int, 0);
module_param(update_time, int, 0);
module_param(update_time2, int, 0);

static int acpi_sbs_add(struct acpi_device *device);
static int acpi_sbs_remove(struct acpi_device *device, int type);
static void acpi_battery_smbus_err_handler(struct acpi_ec_smbus *smbus);
static void acpi_sbs_update_queue(void *data);

static struct acpi_driver acpi_sbs_driver = {
	.name = ACPI_SBS_DRIVER_NAME,
	.class = ACPI_SBS_CLASS,
	.ids = ACPI_SBS_HID,
	.ops = {
		.add = acpi_sbs_add,
		.remove = acpi_sbs_remove,
		},
};

struct acpi_battery_info {
	int capacity_mode;
	s16 full_charge_capacity;
	s16 design_capacity;
	s16 design_voltage;
	int vscale;
	int ipscale;
	s16 serial_number;
	char manufacturer_name[I2C_SMBUS_BLOCK_MAX + 3];
	char device_name[I2C_SMBUS_BLOCK_MAX + 3];
	char device_chemistry[I2C_SMBUS_BLOCK_MAX + 3];
};

struct acpi_battery_state {
	s16 voltage;
	s16 amperage;
	s16 remaining_capacity;
	s16 average_time_to_empty;
	s16 average_time_to_full;
	s16 battery_status;
};

struct acpi_battery_alarm {
	s16 remaining_capacity;
};

struct acpi_battery {
	int alive;
	int battery_present;
	int id;
	int init_state;
	struct acpi_sbs *sbs;
	struct acpi_battery_info info;
	struct acpi_battery_state state;
	struct acpi_battery_alarm alarm;
	struct proc_dir_entry *battery_entry;
};

struct acpi_sbs {
	acpi_handle handle;
	struct acpi_device *device;
	struct acpi_ec_smbus *smbus;
	int sbsm_present;
	int sbsm_batteries_supported;
	int ac_present;
	struct proc_dir_entry *ac_entry;
	struct acpi_battery battery[MAX_SBS_BAT];
	int update_info_mode;
	int zombie;
	int update_time;
	int update_time2;
	struct timer_list update_timer;
};

static void acpi_update_delay(struct acpi_sbs *sbs);
static int acpi_sbs_update_run(struct acpi_sbs *sbs, int data_type);

/* --------------------------------------------------------------------------
                               SMBus Communication
   -------------------------------------------------------------------------- */

static void acpi_battery_smbus_err_handler(struct acpi_ec_smbus *smbus)
{
	union i2c_smbus_data data;
	int result = 0;
	char *err_str;
	int err_number;

	data.word = 0;

	result = smbus->adapter.algo->
	    smbus_xfer(&smbus->adapter,
		       ACPI_SB_SMBUS_ADDR,
		       0, I2C_SMBUS_READ, 0x16, I2C_SMBUS_BLOCK_DATA, &data);

	err_number = (data.word & 0x000f);

	switch (data.word & 0x000f) {
	case 0x0000:
		err_str = "unexpected bus error";
		break;
	case 0x0001:
		err_str = "busy";
		break;
	case 0x0002:
		err_str = "reserved command";
		break;
	case 0x0003:
		err_str = "unsupported command";
		break;
	case 0x0004:
		err_str = "access denied";
		break;
	case 0x0005:
		err_str = "overflow/underflow";
		break;
	case 0x0006:
		err_str = "bad size";
		break;
	case 0x0007:
		err_str = "unknown error";
		break;
	default:
		err_str = "unrecognized error";
	}
	ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
			  "%s: ret %i, err %i\n", err_str, result, err_number));
}

static int
acpi_sbs_smbus_read_word(struct acpi_ec_smbus *smbus, int addr, int func,
			 u16 * word,
			 void (*err_handler) (struct acpi_ec_smbus * smbus))
{
	union i2c_smbus_data data;
	int result = 0;
	int i;

	if (err_handler == NULL) {
		err_handler = acpi_battery_smbus_err_handler;
	}

	for (i = 0; i < MAX_SMBUS_ERR; i++) {
		result =
		    smbus->adapter.algo->smbus_xfer(&smbus->adapter, addr, 0,
						    I2C_SMBUS_READ, func,
						    I2C_SMBUS_WORD_DATA, &data);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "try %i: smbus->adapter.algo->smbus_xfer() failed\n",
					  i));
			if (err_handler) {
				err_handler(smbus);
			}
		} else {
			*word = data.word;
			break;
		}
	}

	return result;
}

static int
acpi_sbs_smbus_read_str(struct acpi_ec_smbus *smbus, int addr, int func,
			char *str,
			void (*err_handler) (struct acpi_ec_smbus * smbus))
{
	union i2c_smbus_data data;
	int result = 0;
	int i;

	if (err_handler == NULL) {
		err_handler = acpi_battery_smbus_err_handler;
	}

	for (i = 0; i < MAX_SMBUS_ERR; i++) {
		result =
		    smbus->adapter.algo->smbus_xfer(&smbus->adapter, addr, 0,
						    I2C_SMBUS_READ, func,
						    I2C_SMBUS_BLOCK_DATA,
						    &data);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "try %i: smbus->adapter.algo->smbus_xfer() failed\n",
					  i));
			if (err_handler) {
				err_handler(smbus);
			}
		} else {
			strncpy(str, (const char *)data.block + 1,
				data.block[0]);
			str[data.block[0]] = 0;
			break;
		}
	}

	return result;
}

static int
acpi_sbs_smbus_write_word(struct acpi_ec_smbus *smbus, int addr, int func,
			  int word,
			  void (*err_handler) (struct acpi_ec_smbus * smbus))
{
	union i2c_smbus_data data;
	int result = 0;
	int i;

	if (err_handler == NULL) {
		err_handler = acpi_battery_smbus_err_handler;
	}

	data.word = word;

	for (i = 0; i < MAX_SMBUS_ERR; i++) {
		result =
		    smbus->adapter.algo->smbus_xfer(&smbus->adapter, addr, 0,
						    I2C_SMBUS_WRITE, func,
						    I2C_SMBUS_WORD_DATA, &data);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "try %i: smbus->adapter.algo"
					  "->smbus_xfer() failed\n", i));
			if (err_handler) {
				err_handler(smbus);
			}
		} else {
			break;
		}
	}

	return result;
}

/* --------------------------------------------------------------------------
                            Smart Battery System Management
   -------------------------------------------------------------------------- */

/* Smart Battery */

static int acpi_sbs_generate_event(struct acpi_device *device,
				   int event, int state, char *bid, char *class)
{
	char bid_saved[5];
	char class_saved[20];
	int result = 0;

	strcpy(bid_saved, acpi_device_bid(device));
	strcpy(class_saved, acpi_device_class(device));

	strcpy(acpi_device_bid(device), bid);
	strcpy(acpi_device_class(device), class);

	result = acpi_bus_generate_event(device, event, state);

	strcpy(acpi_device_bid(device), bid_saved);
	strcpy(acpi_device_class(device), class_saved);

	return result;
}

static int acpi_battery_get_present(struct acpi_battery *battery)
{
	s16 state;
	int result = 0;
	int is_present = 0;

	result = acpi_sbs_smbus_read_word(battery->sbs->smbus,
					  ACPI_SBSM_SMBUS_ADDR, 0x01,
					  &state, NULL);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed"));
	}
	if (!result) {
		is_present = (state & 0x000f) & (1 << battery->id);
	}
	battery->battery_present = is_present;

	return result;
}

static int acpi_battery_is_present(struct acpi_battery *battery)
{
	return (battery->battery_present);
}

static int acpi_ac_is_present(struct acpi_sbs *sbs)
{
	return (sbs->ac_present);
}

static int acpi_battery_select(struct acpi_battery *battery)
{
	struct acpi_ec_smbus *smbus = battery->sbs->smbus;
	int result = 0;
	s16 state;
	int foo;

	if (battery->sbs->sbsm_present) {

		/* Take special care not to knobble other nibbles of
		 * state (aka selector_state), since
		 * it causes charging to halt on SBSELs */

		result =
		    acpi_sbs_smbus_read_word(smbus, ACPI_SBSM_SMBUS_ADDR, 0x01,
					     &state, NULL);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_sbs_smbus_read_word() failed\n"));
			goto end;
		}

		foo = (state & 0x0fff) | (1 << (battery->id + 12));
		result =
		    acpi_sbs_smbus_write_word(smbus, ACPI_SBSM_SMBUS_ADDR, 0x01,
					      foo, NULL);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_sbs_smbus_write_word() failed\n"));
			goto end;
		}
	}

      end:
	return result;
}

static int acpi_sbsm_get_info(struct acpi_sbs *sbs)
{
	struct acpi_ec_smbus *smbus = sbs->smbus;
	int result = 0;
	s16 battery_system_info;

	result = acpi_sbs_smbus_read_word(smbus, ACPI_SBSM_SMBUS_ADDR, 0x04,
					  &battery_system_info, NULL);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}

	sbs->sbsm_batteries_supported = battery_system_info & 0x000f;

      end:

	return result;
}

static int acpi_battery_get_info(struct acpi_battery *battery)
{
	struct acpi_ec_smbus *smbus = battery->sbs->smbus;
	int result = 0;
	s16 battery_mode;
	s16 specification_info;

	result = acpi_sbs_smbus_read_word(smbus, ACPI_SB_SMBUS_ADDR, 0x03,
					  &battery_mode,
					  &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}
	battery->info.capacity_mode = (battery_mode & 0x8000) >> 15;

	result = acpi_sbs_smbus_read_word(smbus, ACPI_SB_SMBUS_ADDR, 0x10,
					  &battery->info.full_charge_capacity,
					  &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}

	result = acpi_sbs_smbus_read_word(smbus, ACPI_SB_SMBUS_ADDR, 0x18,
					  &battery->info.design_capacity,
					  &acpi_battery_smbus_err_handler);

	if (result) {
		goto end;
	}

	result = acpi_sbs_smbus_read_word(smbus, ACPI_SB_SMBUS_ADDR, 0x19,
					  &battery->info.design_voltage,
					  &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}

	result = acpi_sbs_smbus_read_word(smbus, ACPI_SB_SMBUS_ADDR, 0x1a,
					  &specification_info,
					  &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}

	switch ((specification_info & 0x0f00) >> 8) {
	case 1:
		battery->info.vscale = 10;
		break;
	case 2:
		battery->info.vscale = 100;
		break;
	case 3:
		battery->info.vscale = 1000;
		break;
	default:
		battery->info.vscale = 1;
	}

	switch ((specification_info & 0xf000) >> 12) {
	case 1:
		battery->info.ipscale = 10;
		break;
	case 2:
		battery->info.ipscale = 100;
		break;
	case 3:
		battery->info.ipscale = 1000;
		break;
	default:
		battery->info.ipscale = 1;
	}

	result = acpi_sbs_smbus_read_word(smbus, ACPI_SB_SMBUS_ADDR, 0x1c,
					  &battery->info.serial_number,
					  &acpi_battery_smbus_err_handler);
	if (result) {
		goto end;
	}

	result = acpi_sbs_smbus_read_str(smbus, ACPI_SB_SMBUS_ADDR, 0x20,
					 battery->info.manufacturer_name,
					 &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_str() failed\n"));
		goto end;
	}

	result = acpi_sbs_smbus_read_str(smbus, ACPI_SB_SMBUS_ADDR, 0x21,
					 battery->info.device_name,
					 &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_str() failed\n"));
		goto end;
	}

	result = acpi_sbs_smbus_read_str(smbus, ACPI_SB_SMBUS_ADDR, 0x22,
					 battery->info.device_chemistry,
					 &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_str() failed\n"));
		goto end;
	}

      end:
	return result;
}

static void acpi_update_delay(struct acpi_sbs *sbs)
{
	if (sbs->zombie) {
		return;
	}
	if (sbs->update_time2 > 0) {
		msleep(sbs->update_time2 * 1000);
	}
}

static int acpi_battery_get_state(struct acpi_battery *battery)
{
	struct acpi_ec_smbus *smbus = battery->sbs->smbus;
	int result = 0;

	acpi_update_delay(battery->sbs);
	result = acpi_sbs_smbus_read_word(smbus, ACPI_SB_SMBUS_ADDR, 0x09,
					  &battery->state.voltage,
					  &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}

	acpi_update_delay(battery->sbs);
	result = acpi_sbs_smbus_read_word(smbus, ACPI_SB_SMBUS_ADDR, 0x0a,
					  &battery->state.amperage,
					  &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}

	acpi_update_delay(battery->sbs);
	result = acpi_sbs_smbus_read_word(smbus, ACPI_SB_SMBUS_ADDR, 0x0f,
					  &battery->state.remaining_capacity,
					  &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}

	acpi_update_delay(battery->sbs);
	result = acpi_sbs_smbus_read_word(smbus, ACPI_SB_SMBUS_ADDR, 0x12,
					  &battery->state.average_time_to_empty,
					  &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}

	acpi_update_delay(battery->sbs);
	result = acpi_sbs_smbus_read_word(smbus, ACPI_SB_SMBUS_ADDR, 0x13,
					  &battery->state.average_time_to_full,
					  &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}

	acpi_update_delay(battery->sbs);
	result = acpi_sbs_smbus_read_word(smbus, ACPI_SB_SMBUS_ADDR, 0x16,
					  &battery->state.battery_status,
					  &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}

	acpi_update_delay(battery->sbs);

      end:
	return result;
}

static int acpi_battery_get_alarm(struct acpi_battery *battery)
{
	struct acpi_ec_smbus *smbus = battery->sbs->smbus;
	int result = 0;

	result = acpi_sbs_smbus_read_word(smbus, ACPI_SB_SMBUS_ADDR, 0x01,
					  &battery->alarm.remaining_capacity,
					  &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}

	acpi_update_delay(battery->sbs);

      end:

	return result;
}

static int acpi_battery_set_alarm(struct acpi_battery *battery,
				  unsigned long alarm)
{
	struct acpi_ec_smbus *smbus = battery->sbs->smbus;
	int result = 0;
	s16 battery_mode;
	int foo;

	result = acpi_battery_select(battery);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_battery_select() failed\n"));
		goto end;
	}

	/* If necessary, enable the alarm */

	if (alarm > 0) {
		result =
		    acpi_sbs_smbus_read_word(smbus, ACPI_SB_SMBUS_ADDR, 0x03,
					     &battery_mode,
					     &acpi_battery_smbus_err_handler);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_sbs_smbus_read_word() failed\n"));
			goto end;
		}

		result =
		    acpi_sbs_smbus_write_word(smbus, ACPI_SB_SMBUS_ADDR, 0x01,
					      battery_mode & 0xbfff,
					      &acpi_battery_smbus_err_handler);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_sbs_smbus_write_word() failed\n"));
			goto end;
		}
	}

	foo = alarm / (battery->info.capacity_mode ? 10 : 1);
	result = acpi_sbs_smbus_write_word(smbus, ACPI_SB_SMBUS_ADDR, 0x01,
					   foo,
					   &acpi_battery_smbus_err_handler);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_write_word() failed\n"));
		goto end;
	}

      end:

	return result;
}

static int acpi_battery_set_mode(struct acpi_battery *battery)
{
	int result = 0;
	s16 battery_mode;

	if (capacity_mode == DEF_CAPACITY_UNIT) {
		goto end;
	}

	result = acpi_sbs_smbus_read_word(battery->sbs->smbus,
					  ACPI_SB_SMBUS_ADDR, 0x03,
					  &battery_mode, NULL);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}

	if (capacity_mode == MAH_CAPACITY_UNIT) {
		battery_mode &= 0x7fff;
	} else {
		battery_mode |= 0x8000;
	}
	result = acpi_sbs_smbus_write_word(battery->sbs->smbus,
					   ACPI_SB_SMBUS_ADDR, 0x03,
					   battery_mode, NULL);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_write_word() failed\n"));
		goto end;
	}

	result = acpi_sbs_smbus_read_word(battery->sbs->smbus,
					  ACPI_SB_SMBUS_ADDR, 0x03,
					  &battery_mode, NULL);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}

      end:
	return result;
}

static int acpi_battery_init(struct acpi_battery *battery)
{
	int result = 0;

	result = acpi_battery_select(battery);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_battery_init() failed\n"));
		goto end;
	}

	result = acpi_battery_set_mode(battery);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_battery_set_mode() failed\n"));
		goto end;
	}

	result = acpi_battery_get_info(battery);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_battery_get_info() failed\n"));
		goto end;
	}

	result = acpi_battery_get_state(battery);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_battery_get_state() failed\n"));
		goto end;
	}

	result = acpi_battery_get_alarm(battery);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_battery_get_alarm() failed\n"));
		goto end;
	}

      end:
	return result;
}

static int acpi_ac_get_present(struct acpi_sbs *sbs)
{
	struct acpi_ec_smbus *smbus = sbs->smbus;
	int result = 0;
	s16 charger_status;

	result = acpi_sbs_smbus_read_word(smbus, ACPI_SBC_SMBUS_ADDR, 0x13,
					  &charger_status, NULL);

	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_smbus_read_word() failed\n"));
		goto end;
	}

	sbs->ac_present = (charger_status & 0x8000) >> 15;

      end:

	return result;
}

/* --------------------------------------------------------------------------
                              FS Interface (/proc/acpi)
   -------------------------------------------------------------------------- */

/* Generic Routines */

static int
acpi_sbs_generic_add_fs(struct proc_dir_entry **dir,
			struct proc_dir_entry *parent_dir,
			char *dir_name,
			struct file_operations *info_fops,
			struct file_operations *state_fops,
			struct file_operations *alarm_fops, void *data)
{
	struct proc_dir_entry *entry = NULL;

	if (!*dir) {
		*dir = proc_mkdir(dir_name, parent_dir);
		if (!*dir) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "proc_mkdir() failed\n"));
			return -ENODEV;
		}
		(*dir)->owner = THIS_MODULE;
	}

	/* 'info' [R] */
	if (info_fops) {
		entry = create_proc_entry(ACPI_SBS_FILE_INFO, S_IRUGO, *dir);
		if (!entry) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "create_proc_entry() failed\n"));
		} else {
			entry->proc_fops = info_fops;
			entry->data = data;
			entry->owner = THIS_MODULE;
		}
	}

	/* 'state' [R] */
	if (state_fops) {
		entry = create_proc_entry(ACPI_SBS_FILE_STATE, S_IRUGO, *dir);
		if (!entry) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "create_proc_entry() failed\n"));
		} else {
			entry->proc_fops = state_fops;
			entry->data = data;
			entry->owner = THIS_MODULE;
		}
	}

	/* 'alarm' [R/W] */
	if (alarm_fops) {
		entry = create_proc_entry(ACPI_SBS_FILE_ALARM, S_IRUGO, *dir);
		if (!entry) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "create_proc_entry() failed\n"));
		} else {
			entry->proc_fops = alarm_fops;
			entry->data = data;
			entry->owner = THIS_MODULE;
		}
	}

	return 0;
}

static void
acpi_sbs_generic_remove_fs(struct proc_dir_entry **dir,
			   struct proc_dir_entry *parent_dir)
{

	if (*dir) {
		remove_proc_entry(ACPI_SBS_FILE_INFO, *dir);
		remove_proc_entry(ACPI_SBS_FILE_STATE, *dir);
		remove_proc_entry(ACPI_SBS_FILE_ALARM, *dir);
		remove_proc_entry((*dir)->name, parent_dir);
		*dir = NULL;
	}

}

/* Smart Battery Interface */

static struct proc_dir_entry *acpi_battery_dir = NULL;

static int acpi_battery_read_info(struct seq_file *seq, void *offset)
{
	struct acpi_battery *battery = seq->private;
	int cscale;
	int result = 0;

	if (battery->sbs->zombie) {
		return -ENODEV;
	}

	down(&sbs_sem);

	if (update_mode == REQUEST_UPDATE_MODE) {
		result = acpi_sbs_update_run(battery->sbs, DATA_TYPE_INFO);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_sbs_update_run() failed\n"));
		}
	}

	if (acpi_battery_is_present(battery)) {
		seq_printf(seq, "present:                 yes\n");
	} else {
		seq_printf(seq, "present:                 no\n");
		goto end;
	}

	if (battery->info.capacity_mode) {
		cscale = battery->info.vscale * battery->info.ipscale;
	} else {
		cscale = battery->info.ipscale;
	}
	seq_printf(seq, "design capacity:         %i%s",
		   battery->info.design_capacity * cscale,
		   battery->info.capacity_mode ? "0 mWh\n" : " mAh\n");

	seq_printf(seq, "last full capacity:      %i%s",
		   battery->info.full_charge_capacity * cscale,
		   battery->info.capacity_mode ? "0 mWh\n" : " mAh\n");

	seq_printf(seq, "battery technology:      rechargeable\n");

	seq_printf(seq, "design voltage:          %i mV\n",
		   battery->info.design_voltage * battery->info.vscale);

	seq_printf(seq, "design capacity warning: unknown\n");
	seq_printf(seq, "design capacity low:     unknown\n");
	seq_printf(seq, "capacity granularity 1:  unknown\n");
	seq_printf(seq, "capacity granularity 2:  unknown\n");

	seq_printf(seq, "model number:            %s\n",
		   battery->info.device_name);

	seq_printf(seq, "serial number:           %i\n",
		   battery->info.serial_number);

	seq_printf(seq, "battery type:            %s\n",
		   battery->info.device_chemistry);

	seq_printf(seq, "OEM info:                %s\n",
		   battery->info.manufacturer_name);

      end:

	up(&sbs_sem);

	return result;
}

static int acpi_battery_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_info, PDE(inode)->data);
}

static int acpi_battery_read_state(struct seq_file *seq, void *offset)
{
	struct acpi_battery *battery = (struct acpi_battery *)seq->private;
	int result = 0;
	int cscale;
	int foo;

	if (battery->sbs->zombie) {
		return -ENODEV;
	}

	down(&sbs_sem);

	if (update_mode == REQUEST_UPDATE_MODE) {
		result = acpi_sbs_update_run(battery->sbs, DATA_TYPE_STATE);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_sbs_update_run() failed\n"));
		}
	}

	if (acpi_battery_is_present(battery)) {
		seq_printf(seq, "present:                 yes\n");
	} else {
		seq_printf(seq, "present:                 no\n");
		goto end;
	}

	if (battery->info.capacity_mode) {
		cscale = battery->info.vscale * battery->info.ipscale;
	} else {
		cscale = battery->info.ipscale;
	}

	if (battery->state.battery_status & 0x0010) {
		seq_printf(seq, "capacity state:          critical\n");
	} else {
		seq_printf(seq, "capacity state:          ok\n");
	}
	if (battery->state.amperage < 0) {
		seq_printf(seq, "charging state:          discharging\n");
		foo = battery->state.remaining_capacity * cscale * 60 /
		    (battery->state.average_time_to_empty == 0 ? 1 :
		     battery->state.average_time_to_empty);
		seq_printf(seq, "present rate:            %i%s\n",
			   foo, battery->info.capacity_mode ? "0 mW" : " mA");
	} else if (battery->state.amperage > 0) {
		seq_printf(seq, "charging state:          charging\n");
		foo = (battery->info.full_charge_capacity -
		       battery->state.remaining_capacity) * cscale * 60 /
		    (battery->state.average_time_to_full == 0 ? 1 :
		     battery->state.average_time_to_full);
		seq_printf(seq, "present rate:            %i%s\n",
			   foo, battery->info.capacity_mode ? "0 mW" : " mA");
	} else {
		seq_printf(seq, "charging state:          charged\n");
		seq_printf(seq, "present rate:            0 %s\n",
			   battery->info.capacity_mode ? "mW" : "mA");
	}

	seq_printf(seq, "remaining capacity:      %i%s",
		   battery->state.remaining_capacity * cscale,
		   battery->info.capacity_mode ? "0 mWh\n" : " mAh\n");

	seq_printf(seq, "present voltage:         %i mV\n",
		   battery->state.voltage * battery->info.vscale);

      end:

	up(&sbs_sem);

	return result;
}

static int acpi_battery_state_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_state, PDE(inode)->data);
}

static int acpi_battery_read_alarm(struct seq_file *seq, void *offset)
{
	struct acpi_battery *battery = seq->private;
	int result = 0;
	int cscale;

	if (battery->sbs->zombie) {
		return -ENODEV;
	}

	down(&sbs_sem);

	if (update_mode == REQUEST_UPDATE_MODE) {
		result = acpi_sbs_update_run(battery->sbs, DATA_TYPE_ALARM);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_sbs_update_run() failed\n"));
		}
	}

	if (!acpi_battery_is_present(battery)) {
		seq_printf(seq, "present:                 no\n");
		goto end;
	}

	if (battery->info.capacity_mode) {
		cscale = battery->info.vscale * battery->info.ipscale;
	} else {
		cscale = battery->info.ipscale;
	}

	seq_printf(seq, "alarm:                   ");
	if (battery->alarm.remaining_capacity) {
		seq_printf(seq, "%i%s",
			   battery->alarm.remaining_capacity * cscale,
			   battery->info.capacity_mode ? "0 mWh\n" : " mAh\n");
	} else {
		seq_printf(seq, "disabled\n");
	}

      end:

	up(&sbs_sem);

	return result;
}

static ssize_t
acpi_battery_write_alarm(struct file *file, const char __user * buffer,
			 size_t count, loff_t * ppos)
{
	struct seq_file *seq = file->private_data;
	struct acpi_battery *battery = seq->private;
	char alarm_string[12] = { '\0' };
	int result, old_alarm, new_alarm;

	if (battery->sbs->zombie) {
		return -ENODEV;
	}

	down(&sbs_sem);

	if (!acpi_battery_is_present(battery)) {
		result = -ENODEV;
		goto end;
	}

	if (count > sizeof(alarm_string) - 1) {
		result = -EINVAL;
		goto end;
	}

	if (copy_from_user(alarm_string, buffer, count)) {
		result = -EFAULT;
		goto end;
	}

	alarm_string[count] = 0;

	old_alarm = battery->alarm.remaining_capacity;
	new_alarm = simple_strtoul(alarm_string, NULL, 0);

	result = acpi_battery_set_alarm(battery, new_alarm);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_battery_set_alarm() failed\n"));
		acpi_battery_set_alarm(battery, old_alarm);
		goto end;
	}
	result = acpi_battery_get_alarm(battery);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_battery_get_alarm() failed\n"));
		acpi_battery_set_alarm(battery, old_alarm);
		goto end;
	}

      end:
	up(&sbs_sem);

	if (result) {
		return result;
	} else {
		return count;
	}
}

static int acpi_battery_alarm_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_alarm, PDE(inode)->data);
}

static struct file_operations acpi_battery_info_fops = {
	.open = acpi_battery_info_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct file_operations acpi_battery_state_fops = {
	.open = acpi_battery_state_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

static struct file_operations acpi_battery_alarm_fops = {
	.open = acpi_battery_alarm_open_fs,
	.read = seq_read,
	.write = acpi_battery_write_alarm,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

/* Legacy AC Adapter Interface */

static struct proc_dir_entry *acpi_ac_dir = NULL;

static int acpi_ac_read_state(struct seq_file *seq, void *offset)
{
	struct acpi_sbs *sbs = seq->private;
	int result;

	if (sbs->zombie) {
		return -ENODEV;
	}

	down(&sbs_sem);

	if (update_mode == REQUEST_UPDATE_MODE) {
		result = acpi_sbs_update_run(sbs, DATA_TYPE_AC_STATE);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_sbs_update_run() failed\n"));
		}
	}

	seq_printf(seq, "state:                   %s\n",
		   sbs->ac_present ? "on-line" : "off-line");

	up(&sbs_sem);

	return 0;
}

static int acpi_ac_state_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_ac_read_state, PDE(inode)->data);
}

static struct file_operations acpi_ac_state_fops = {
	.open = acpi_ac_state_open_fs,
	.read = seq_read,
	.llseek = seq_lseek,
	.release = single_release,
	.owner = THIS_MODULE,
};

/* --------------------------------------------------------------------------
                                 Driver Interface
   -------------------------------------------------------------------------- */

/* Smart Battery */

static int acpi_battery_add(struct acpi_sbs *sbs, int id)
{
	int is_present;
	int result;
	char dir_name[32];
	struct acpi_battery *battery;

	battery = &sbs->battery[id];

	battery->alive = 0;

	battery->init_state = 0;
	battery->id = id;
	battery->sbs = sbs;

	result = acpi_battery_select(battery);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_battery_select() failed\n"));
		goto end;
	}

	result = acpi_battery_get_present(battery);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_battery_get_present() failed\n"));
		goto end;
	}

	is_present = acpi_battery_is_present(battery);

	if (is_present) {
		result = acpi_battery_init(battery);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_battery_init() failed\n"));
			goto end;
		}
		battery->init_state = 1;
	}

	sprintf(dir_name, ACPI_BATTERY_DIR_NAME, id);

	result = acpi_sbs_generic_add_fs(&battery->battery_entry,
					 acpi_battery_dir,
					 dir_name,
					 &acpi_battery_info_fops,
					 &acpi_battery_state_fops,
					 &acpi_battery_alarm_fops, battery);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_generic_add_fs() failed\n"));
		goto end;
	}
	battery->alive = 1;

      end:
	return result;
}

static void acpi_battery_remove(struct acpi_sbs *sbs, int id)
{

	if (sbs->battery[id].battery_entry) {
		acpi_sbs_generic_remove_fs(&(sbs->battery[id].battery_entry),
					   acpi_battery_dir);
	}
}

static int acpi_ac_add(struct acpi_sbs *sbs)
{
	int result;

	result = acpi_ac_get_present(sbs);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_ac_get_present() failed\n"));
		goto end;
	}

	result = acpi_sbs_generic_add_fs(&sbs->ac_entry,
					 acpi_ac_dir,
					 ACPI_AC_DIR_NAME,
					 NULL, &acpi_ac_state_fops, NULL, sbs);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_generic_add_fs() failed\n"));
		goto end;
	}

      end:

	return result;
}

static void acpi_ac_remove(struct acpi_sbs *sbs)
{

	if (sbs->ac_entry) {
		acpi_sbs_generic_remove_fs(&sbs->ac_entry, acpi_ac_dir);
	}
}

static void acpi_sbs_update_queue_run(unsigned long data)
{
	acpi_os_execute(OSL_GPE_HANDLER, acpi_sbs_update_queue, (void *)data);
}

static int acpi_sbs_update_run(struct acpi_sbs *sbs, int data_type)
{
	struct acpi_battery *battery;
	int result = 0;
	int old_ac_present;
	int old_battery_present;
	int new_ac_present;
	int new_battery_present;
	int id;
	char dir_name[32];
	int do_battery_init, do_ac_init;
	s16 old_remaining_capacity;

	if (sbs->zombie) {
		goto end;
	}

	old_ac_present = acpi_ac_is_present(sbs);

	result = acpi_ac_get_present(sbs);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_ac_get_present() failed\n"));
	}

	new_ac_present = acpi_ac_is_present(sbs);

	do_ac_init = (old_ac_present != new_ac_present);

	if (data_type == DATA_TYPE_AC_STATE) {
		goto end;
	}

	for (id = 0; id < MAX_SBS_BAT; id++) {
		battery = &sbs->battery[id];
		if (battery->alive == 0) {
			continue;
		}

		old_remaining_capacity = battery->state.remaining_capacity;

		old_battery_present = acpi_battery_is_present(battery);

		result = acpi_battery_select(battery);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_battery_select() failed\n"));
		}
		if (sbs->zombie) {
			goto end;
		}

		result = acpi_battery_get_present(battery);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_battery_get_present() failed\n"));
		}
		if (sbs->zombie) {
			goto end;
		}

		new_battery_present = acpi_battery_is_present(battery);

		do_battery_init = ((old_battery_present != new_battery_present)
				   && new_battery_present);

		if (sbs->zombie) {
			goto end;
		}
		if (do_ac_init || do_battery_init ||
		    update_info_mode || sbs->update_info_mode) {
			if (sbs->update_info_mode) {
				sbs->update_info_mode = 0;
			} else {
				sbs->update_info_mode = 1;
			}
			result = acpi_battery_init(battery);
			if (result) {
				ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
						  "acpi_battery_init() "
						  "failed\n"));
			}
		}
		if (data_type == DATA_TYPE_INFO) {
			continue;
		}

		if (sbs->zombie) {
			goto end;
		}
		if (new_battery_present) {
			result = acpi_battery_get_alarm(battery);
			if (result) {
				ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
						  "acpi_battery_get_alarm() "
						  "failed\n"));
			}
			if (data_type == DATA_TYPE_ALARM) {
				continue;
			}

			result = acpi_battery_get_state(battery);
			if (result) {
				ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
						  "acpi_battery_get_state() "
						  "failed\n"));
			}
		}
		if (sbs->zombie) {
			goto end;
		}
		if (data_type != DATA_TYPE_COMMON) {
			continue;
		}

		if (old_battery_present != new_battery_present) {
			sprintf(dir_name, ACPI_BATTERY_DIR_NAME, id);
			result = acpi_sbs_generate_event(sbs->device,
							 ACPI_SBS_BATTERY_NOTIFY_STATUS,
							 new_battery_present,
							 dir_name,
							 ACPI_BATTERY_CLASS);
			if (result) {
				ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
						  "acpi_sbs_generate_event() "
						  "failed\n"));
			}
		}
		if (old_remaining_capacity != battery->state.remaining_capacity) {
			sprintf(dir_name, ACPI_BATTERY_DIR_NAME, id);
			result = acpi_sbs_generate_event(sbs->device,
							 ACPI_SBS_BATTERY_NOTIFY_STATUS,
							 new_battery_present,
							 dir_name,
							 ACPI_BATTERY_CLASS);
			if (result) {
				ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
						  "acpi_sbs_generate_event() failed\n"));
			}
		}

	}
	if (sbs->zombie) {
		goto end;
	}
	if (data_type != DATA_TYPE_COMMON) {
		goto end;
	}

	if (old_ac_present != new_ac_present) {
		result = acpi_sbs_generate_event(sbs->device,
						 ACPI_SBS_AC_NOTIFY_STATUS,
						 new_ac_present,
						 ACPI_AC_DIR_NAME,
						 ACPI_AC_CLASS);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_sbs_generate_event() failed\n"));
		}
	}

      end:
	return result;
}

static void acpi_sbs_update_queue(void *data)
{
	struct acpi_sbs *sbs = data;
	unsigned long delay = -1;
	int result;

	if (sbs->zombie) {
		goto end;
	}

	result = acpi_sbs_update_run(sbs, DATA_TYPE_COMMON);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_sbs_update_run() failed\n"));
	}

	if (sbs->zombie) {
		goto end;
	}

	if (update_mode == REQUEST_UPDATE_MODE) {
		goto end;
	}

	delay = jiffies + HZ * update_time;
	sbs->update_timer.data = (unsigned long)data;
	sbs->update_timer.function = acpi_sbs_update_queue_run;
	sbs->update_timer.expires = delay;
	add_timer(&sbs->update_timer);
      end:
	;
}

static int acpi_sbs_add(struct acpi_device *device)
{
	struct acpi_sbs *sbs = NULL;
	struct acpi_ec_hc *ec_hc = NULL;
	int result, remove_result = 0;
	unsigned long sbs_obj;
	int id, cnt;
	acpi_status status = AE_OK;

	sbs = kmalloc(sizeof(struct acpi_sbs), GFP_KERNEL);
	if (!sbs) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "kmalloc() failed\n"));
		return -ENOMEM;
	}
	memset(sbs, 0, sizeof(struct acpi_sbs));

	cnt = 0;
	while (cnt < 10) {
		cnt++;
		ec_hc = acpi_get_ec_hc(device);
		if (ec_hc) {
			break;
		}
		msleep(1000);
	}

	if (!ec_hc) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_get_ec_hc() failed: "
				  "NO driver found for EC HC SMBus\n"));
		result = -ENODEV;
		goto end;
	}

	sbs->device = device;
	sbs->smbus = ec_hc->smbus;

	strcpy(acpi_device_name(device), ACPI_SBS_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_SBS_CLASS);
	acpi_driver_data(device) = sbs;

	sbs->update_time = 0;
	sbs->update_time2 = 0;

	result = acpi_ac_add(sbs);
	if (result) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "acpi_ac_add() failed\n"));
		goto end;
	}
	result = acpi_evaluate_integer(device->handle, "_SBS", NULL, &sbs_obj);
	if (ACPI_FAILURE(result)) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_evaluate_integer() failed\n"));
		result = -EIO;
		goto end;
	}

	if (sbs_obj > 0) {
		result = acpi_sbsm_get_info(sbs);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_sbsm_get_info() failed\n"));
			goto end;
		}
		sbs->sbsm_present = 1;
	}
	if (sbs->sbsm_present == 0) {
		result = acpi_battery_add(sbs, 0);
		if (result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_battery_add() failed\n"));
			goto end;
		}
	} else {
		for (id = 0; id < MAX_SBS_BAT; id++) {
			if ((sbs->sbsm_batteries_supported & (1 << id))) {
				result = acpi_battery_add(sbs, id);
				if (result) {
					ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
							  "acpi_battery_add() "
							  "failed\n"));
					goto end;
				}
			}
		}
	}

	sbs->handle = device->handle;

	init_timer(&sbs->update_timer);
	if (update_mode == QUEUE_UPDATE_MODE) {
		status = acpi_os_execute(OSL_GPE_HANDLER,
					 acpi_sbs_update_queue, sbs);
		if (status != AE_OK) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_os_execute() failed\n"));
		}
	}
	sbs->update_time = update_time;
	sbs->update_time2 = update_time2;

	printk(KERN_INFO PREFIX "%s [%s]\n",
	       acpi_device_name(device), acpi_device_bid(device));

      end:
	if (result) {
		remove_result = acpi_sbs_remove(device, 0);
		if (remove_result) {
			ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
					  "acpi_sbs_remove() failed\n"));
		}
	}

	return result;
}

int acpi_sbs_remove(struct acpi_device *device, int type)
{
	struct acpi_sbs *sbs;
	int id;

	if (!device) {
		return -EINVAL;
	}

	sbs = (struct acpi_sbs *)acpi_driver_data(device);

	if (!sbs) {
		return -EINVAL;
	}

	sbs->zombie = 1;
	sbs->update_time = 0;
	sbs->update_time2 = 0;
	del_timer_sync(&sbs->update_timer);
	acpi_os_wait_events_complete(NULL);
	del_timer_sync(&sbs->update_timer);

	for (id = 0; id < MAX_SBS_BAT; id++) {
		acpi_battery_remove(sbs, id);
	}

	acpi_ac_remove(sbs);

	kfree(sbs);

	return 0;
}

static int __init acpi_sbs_init(void)
{
	int result = 0;

	if (acpi_disabled)
		return -ENODEV;

	init_MUTEX(&sbs_sem);

	if (capacity_mode != DEF_CAPACITY_UNIT
	    && capacity_mode != MAH_CAPACITY_UNIT
	    && capacity_mode != MWH_CAPACITY_UNIT) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR, "acpi_sbs_init: "
				  "invalid capacity_mode = %d\n",
				  capacity_mode));
		return -EINVAL;
	}

	acpi_ac_dir = acpi_lock_ac_dir();
	if (!acpi_ac_dir) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_lock_ac_dir() failed\n"));
		return -ENODEV;
	}

	acpi_battery_dir = acpi_lock_battery_dir();
	if (!acpi_battery_dir) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_lock_battery_dir() failed\n"));
		return -ENODEV;
	}

	result = acpi_bus_register_driver(&acpi_sbs_driver);
	if (result < 0) {
		ACPI_DEBUG_PRINT((ACPI_DB_ERROR,
				  "acpi_bus_register_driver() failed\n"));
		return -ENODEV;
	}

	return 0;
}

static void __exit acpi_sbs_exit(void)
{

	acpi_bus_unregister_driver(&acpi_sbs_driver);

	acpi_unlock_ac_dir(acpi_ac_dir);
	acpi_ac_dir = NULL;
	acpi_unlock_battery_dir(acpi_battery_dir);
	acpi_battery_dir = NULL;

	return;
}

module_init(acpi_sbs_init);
module_exit(acpi_sbs_exit);
