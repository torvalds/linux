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
#include <linux/timer.h>
#include <linux/jiffies.h>
#include <linux/delay.h>

#define ACPI_SBS_COMPONENT		0x00080000
#define ACPI_SBS_CLASS			"sbs"
#define ACPI_AC_CLASS			"ac_adapter"
#define ACPI_BATTERY_CLASS		"battery"
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

ACPI_MODULE_NAME("sbs");

MODULE_AUTHOR("Rich Townsend");
MODULE_DESCRIPTION("Smart Battery System ACPI interface driver");
MODULE_LICENSE("GPL");

#define	xmsleep(t)	msleep(t)

#define ACPI_EC_SMB_PRTCL	0x00	/* protocol, PEC */

#define ACPI_EC_SMB_STS		0x01	/* status */
#define ACPI_EC_SMB_ADDR	0x02	/* address */
#define ACPI_EC_SMB_CMD		0x03	/* command */
#define ACPI_EC_SMB_DATA	0x04	/* 32 data registers */
#define ACPI_EC_SMB_BCNT	0x24	/* number of data bytes */

#define ACPI_EC_SMB_STS_DONE	0x80
#define ACPI_EC_SMB_STS_STATUS	0x1f

#define ACPI_EC_SMB_PRTCL_WRITE		0x00
#define ACPI_EC_SMB_PRTCL_READ		0x01
#define ACPI_EC_SMB_PRTCL_WORD_DATA	0x08
#define ACPI_EC_SMB_PRTCL_BLOCK_DATA	0x0a

#define ACPI_EC_SMB_TRANSACTION_SLEEP	1
#define ACPI_EC_SMB_ACCESS_SLEEP1	1
#define ACPI_EC_SMB_ACCESS_SLEEP2	10

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

#define	MAX_SBS_BAT			4
#define ACPI_SBS_BLOCK_MAX		32

#define ACPI_SBS_SMBUS_READ		1
#define ACPI_SBS_SMBUS_WRITE		2

#define ACPI_SBS_WORD_DATA		1
#define ACPI_SBS_BLOCK_DATA		2

#define	UPDATE_DELAY	10

/* 0 - every time, > 0 - by update_time */
static unsigned int update_time = 120;

static unsigned int capacity_mode = CAPACITY_UNIT;

module_param(update_time, uint, 0644);
module_param(capacity_mode, uint, 0444);

static int acpi_sbs_add(struct acpi_device *device);
static int acpi_sbs_remove(struct acpi_device *device, int type);
static int acpi_sbs_resume(struct acpi_device *device);

static const struct acpi_device_id sbs_device_ids[] = {
	{"ACPI0001", 0},
	{"ACPI0005", 0},
	{"", 0},
};
MODULE_DEVICE_TABLE(acpi, sbs_device_ids);

static struct acpi_driver acpi_sbs_driver = {
	.name = "sbs",
	.class = ACPI_SBS_CLASS,
	.ids = sbs_device_ids,
	.ops = {
		.add = acpi_sbs_add,
		.remove = acpi_sbs_remove,
		.resume = acpi_sbs_resume,
		},
};

struct acpi_ac {
	int ac_present;
};

struct acpi_battery_info {
	int capacity_mode;
	s16 full_charge_capacity;
	s16 design_capacity;
	s16 design_voltage;
	int vscale;
	int ipscale;
	s16 serial_number;
	char manufacturer_name[ACPI_SBS_BLOCK_MAX + 3];
	char device_name[ACPI_SBS_BLOCK_MAX + 3];
	char device_chemistry[ACPI_SBS_BLOCK_MAX + 3];
};

struct acpi_battery_state {
	s16 voltage;
	s16 amperage;
	s16 remaining_capacity;
	s16 battery_state;
};

struct acpi_battery_alarm {
	s16 remaining_capacity;
};

struct acpi_battery {
	int alive;
	int id;
	int init_state;
	int battery_present;
	struct acpi_sbs *sbs;
	struct acpi_battery_info info;
	struct acpi_battery_state state;
	struct acpi_battery_alarm alarm;
	struct proc_dir_entry *battery_entry;
};

struct acpi_sbs {
	int base;
	struct acpi_device *device;
	struct mutex mutex;
	int sbsm_present;
	int sbsm_batteries_supported;
	struct proc_dir_entry *ac_entry;
	struct acpi_ac ac;
	struct acpi_battery battery[MAX_SBS_BAT];
	int zombie;
	struct timer_list update_timer;
	int run_cnt;
	int update_proc_flg;
};

static int acpi_sbs_update_run(struct acpi_sbs *sbs, int id, int data_type);
static void acpi_sbs_update_time(void *data);

union sbs_rw_data {
	u16 word;
	u8 block[ACPI_SBS_BLOCK_MAX + 2];
};

static int acpi_ec_sbs_access(struct acpi_sbs *sbs, u16 addr,
			      char read_write, u8 command, int size,
			      union sbs_rw_data *data);

/* --------------------------------------------------------------------------
                               SMBus Communication
   -------------------------------------------------------------------------- */

static int acpi_ec_sbs_read(struct acpi_sbs *sbs, u8 address, u8 * data)
{
	u8 val;
	int err;

	err = ec_read(sbs->base + address, &val);
	if (!err) {
		*data = val;
	}
	xmsleep(ACPI_EC_SMB_TRANSACTION_SLEEP);
	return (err);
}

static int acpi_ec_sbs_write(struct acpi_sbs *sbs, u8 address, u8 data)
{
	int err;

	err = ec_write(sbs->base + address, data);
	return (err);
}

static int
acpi_ec_sbs_access(struct acpi_sbs *sbs, u16 addr,
		   char read_write, u8 command, int size,
		   union sbs_rw_data *data)
{
	unsigned char protocol, len = 0, temp[2] = { 0, 0 };
	int i;

	if (read_write == ACPI_SBS_SMBUS_READ) {
		protocol = ACPI_EC_SMB_PRTCL_READ;
	} else {
		protocol = ACPI_EC_SMB_PRTCL_WRITE;
	}

	switch (size) {

	case ACPI_SBS_WORD_DATA:
		acpi_ec_sbs_write(sbs, ACPI_EC_SMB_CMD, command);
		if (read_write == ACPI_SBS_SMBUS_WRITE) {
			acpi_ec_sbs_write(sbs, ACPI_EC_SMB_DATA, data->word);
			acpi_ec_sbs_write(sbs, ACPI_EC_SMB_DATA + 1,
					  data->word >> 8);
		}
		protocol |= ACPI_EC_SMB_PRTCL_WORD_DATA;
		break;
	case ACPI_SBS_BLOCK_DATA:
		acpi_ec_sbs_write(sbs, ACPI_EC_SMB_CMD, command);
		if (read_write == ACPI_SBS_SMBUS_WRITE) {
			len = min_t(u8, data->block[0], 32);
			acpi_ec_sbs_write(sbs, ACPI_EC_SMB_BCNT, len);
			for (i = 0; i < len; i++)
				acpi_ec_sbs_write(sbs, ACPI_EC_SMB_DATA + i,
						  data->block[i + 1]);
		}
		protocol |= ACPI_EC_SMB_PRTCL_BLOCK_DATA;
		break;
	default:
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"unsupported transaction %d", size));
		return (-1);
	}

	acpi_ec_sbs_write(sbs, ACPI_EC_SMB_ADDR, addr << 1);
	acpi_ec_sbs_write(sbs, ACPI_EC_SMB_PRTCL, protocol);

	acpi_ec_sbs_read(sbs, ACPI_EC_SMB_STS, temp);

	if (~temp[0] & ACPI_EC_SMB_STS_DONE) {
		xmsleep(ACPI_EC_SMB_ACCESS_SLEEP1);
		acpi_ec_sbs_read(sbs, ACPI_EC_SMB_STS, temp);
	}
	if (~temp[0] & ACPI_EC_SMB_STS_DONE) {
		xmsleep(ACPI_EC_SMB_ACCESS_SLEEP2);
		acpi_ec_sbs_read(sbs, ACPI_EC_SMB_STS, temp);
	}
	if ((~temp[0] & ACPI_EC_SMB_STS_DONE)
	    || (temp[0] & ACPI_EC_SMB_STS_STATUS)) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"transaction %d error", size));
		return (-1);
	}

	if (read_write == ACPI_SBS_SMBUS_WRITE) {
		return (0);
	}

	switch (size) {

	case ACPI_SBS_WORD_DATA:
		acpi_ec_sbs_read(sbs, ACPI_EC_SMB_DATA, temp);
		acpi_ec_sbs_read(sbs, ACPI_EC_SMB_DATA + 1, temp + 1);
		data->word = (temp[1] << 8) | temp[0];
		break;

	case ACPI_SBS_BLOCK_DATA:
		len = 0;
		acpi_ec_sbs_read(sbs, ACPI_EC_SMB_BCNT, &len);
		len = min_t(u8, len, 32);
		for (i = 0; i < len; i++)
			acpi_ec_sbs_read(sbs, ACPI_EC_SMB_DATA + i,
					 data->block + i + 1);
		data->block[0] = len;
		break;
	default:
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"unsupported transaction %d", size));
		return (-1);
	}

	return (0);
}

static int
acpi_sbs_read_word(struct acpi_sbs *sbs, int addr, int func, u16 * word)
{
	union sbs_rw_data data;
	int result = 0;

	result = acpi_ec_sbs_access(sbs, addr,
				    ACPI_SBS_SMBUS_READ, func,
				    ACPI_SBS_WORD_DATA, &data);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_ec_sbs_access() failed"));
	} else {
		*word = data.word;
	}

	return result;
}

static int
acpi_sbs_read_str(struct acpi_sbs *sbs, int addr, int func, char *str)
{
	union sbs_rw_data data;
	int result = 0;

	result = acpi_ec_sbs_access(sbs, addr,
				    ACPI_SBS_SMBUS_READ, func,
				    ACPI_SBS_BLOCK_DATA, &data);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_ec_sbs_access() failed"));
	} else {
		strncpy(str, (const char *)data.block + 1, data.block[0]);
		str[data.block[0]] = 0;
	}

	return result;
}

static int
acpi_sbs_write_word(struct acpi_sbs *sbs, int addr, int func, int word)
{
	union sbs_rw_data data;
	int result = 0;

	data.word = word;

	result = acpi_ec_sbs_access(sbs, addr,
				    ACPI_SBS_SMBUS_WRITE, func,
				    ACPI_SBS_WORD_DATA, &data);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_ec_sbs_access() failed"));
	}

	return result;
}

static int sbs_zombie(struct acpi_sbs *sbs)
{
	return (sbs->zombie);
}

static int sbs_mutex_lock(struct acpi_sbs *sbs)
{
	if (sbs_zombie(sbs)) {
		return -ENODEV;
	}
	mutex_lock(&sbs->mutex);
	return 0;
}

static void sbs_mutex_unlock(struct acpi_sbs *sbs)
{
	mutex_unlock(&sbs->mutex);
}

/* --------------------------------------------------------------------------
                            Smart Battery System Management
   -------------------------------------------------------------------------- */

static int acpi_check_update_proc(struct acpi_sbs *sbs)
{
	acpi_status status = AE_OK;

	if (update_time == 0) {
		sbs->update_proc_flg = 0;
		return 0;
	}
	if (sbs->update_proc_flg == 0) {
		status = acpi_os_execute(OSL_GPE_HANDLER,
					 acpi_sbs_update_time, sbs);
		if (status != AE_OK) {
			ACPI_EXCEPTION((AE_INFO, status,
					"acpi_os_execute() failed"));
			return 1;
		}
		sbs->update_proc_flg = 1;
	}
	return 0;
}

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

	result = acpi_sbs_read_word(battery->sbs,
				    ACPI_SBSM_SMBUS_ADDR, 0x01, &state);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
	}
	if (!result) {
		is_present = (state & 0x000f) & (1 << battery->id);
	}
	battery->battery_present = is_present;

	return result;
}

static int acpi_battery_select(struct acpi_battery *battery)
{
	struct acpi_sbs *sbs = battery->sbs;
	int result = 0;
	s16 state;
	int foo;

	if (sbs->sbsm_present) {

		/* Take special care not to knobble other nibbles of
		 * state (aka selector_state), since
		 * it causes charging to halt on SBSELs */

		result =
		    acpi_sbs_read_word(sbs, ACPI_SBSM_SMBUS_ADDR, 0x01, &state);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_read_word() failed"));
			goto end;
		}

		foo = (state & 0x0fff) | (1 << (battery->id + 12));
		result =
		    acpi_sbs_write_word(sbs, ACPI_SBSM_SMBUS_ADDR, 0x01, foo);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_write_word() failed"));
			goto end;
		}
	}

      end:
	return result;
}

static int acpi_sbsm_get_info(struct acpi_sbs *sbs)
{
	int result = 0;
	s16 battery_system_info;

	result = acpi_sbs_read_word(sbs, ACPI_SBSM_SMBUS_ADDR, 0x04,
				    &battery_system_info);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
		goto end;
	}
	sbs->sbsm_present = 1;
	sbs->sbsm_batteries_supported = battery_system_info & 0x000f;

      end:

	return result;
}

static int acpi_battery_get_info(struct acpi_battery *battery)
{
	struct acpi_sbs *sbs = battery->sbs;
	int result = 0;
	s16 battery_mode;
	s16 specification_info;

	result = acpi_sbs_read_word(sbs, ACPI_SB_SMBUS_ADDR, 0x03,
				    &battery_mode);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
		goto end;
	}
	battery->info.capacity_mode = (battery_mode & 0x8000) >> 15;

	result = acpi_sbs_read_word(sbs, ACPI_SB_SMBUS_ADDR, 0x10,
				    &battery->info.full_charge_capacity);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
		goto end;
	}

	result = acpi_sbs_read_word(sbs, ACPI_SB_SMBUS_ADDR, 0x18,
				    &battery->info.design_capacity);

	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
		goto end;
	}

	result = acpi_sbs_read_word(sbs, ACPI_SB_SMBUS_ADDR, 0x19,
				    &battery->info.design_voltage);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
		goto end;
	}

	result = acpi_sbs_read_word(sbs, ACPI_SB_SMBUS_ADDR, 0x1a,
				    &specification_info);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
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

	result = acpi_sbs_read_word(sbs, ACPI_SB_SMBUS_ADDR, 0x1c,
				    &battery->info.serial_number);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
		goto end;
	}

	result = acpi_sbs_read_str(sbs, ACPI_SB_SMBUS_ADDR, 0x20,
				   battery->info.manufacturer_name);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_str() failed"));
		goto end;
	}

	result = acpi_sbs_read_str(sbs, ACPI_SB_SMBUS_ADDR, 0x21,
				   battery->info.device_name);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_str() failed"));
		goto end;
	}

	result = acpi_sbs_read_str(sbs, ACPI_SB_SMBUS_ADDR, 0x22,
				   battery->info.device_chemistry);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_str() failed"));
		goto end;
	}

      end:
	return result;
}

static int acpi_battery_get_state(struct acpi_battery *battery)
{
	struct acpi_sbs *sbs = battery->sbs;
	int result = 0;

	result = acpi_sbs_read_word(sbs, ACPI_SB_SMBUS_ADDR, 0x09,
				    &battery->state.voltage);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
		goto end;
	}

	result = acpi_sbs_read_word(sbs, ACPI_SB_SMBUS_ADDR, 0x0a,
				    &battery->state.amperage);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
		goto end;
	}

	result = acpi_sbs_read_word(sbs, ACPI_SB_SMBUS_ADDR, 0x0f,
				    &battery->state.remaining_capacity);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
		goto end;
	}

	result = acpi_sbs_read_word(sbs, ACPI_SB_SMBUS_ADDR, 0x16,
				    &battery->state.battery_state);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
		goto end;
	}

      end:
	return result;
}

static int acpi_battery_get_alarm(struct acpi_battery *battery)
{
	struct acpi_sbs *sbs = battery->sbs;
	int result = 0;

	result = acpi_sbs_read_word(sbs, ACPI_SB_SMBUS_ADDR, 0x01,
				    &battery->alarm.remaining_capacity);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
		goto end;
	}

      end:

	return result;
}

static int acpi_battery_set_alarm(struct acpi_battery *battery,
				  unsigned long alarm)
{
	struct acpi_sbs *sbs = battery->sbs;
	int result = 0;
	s16 battery_mode;
	int foo;

	result = acpi_battery_select(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_select() failed"));
		goto end;
	}

	/* If necessary, enable the alarm */

	if (alarm > 0) {
		result =
		    acpi_sbs_read_word(sbs, ACPI_SB_SMBUS_ADDR, 0x03,
				       &battery_mode);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_read_word() failed"));
			goto end;
		}

		result =
		    acpi_sbs_write_word(sbs, ACPI_SB_SMBUS_ADDR, 0x01,
					battery_mode & 0xbfff);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_write_word() failed"));
			goto end;
		}
	}

	foo = alarm / (battery->info.capacity_mode ? 10 : 1);
	result = acpi_sbs_write_word(sbs, ACPI_SB_SMBUS_ADDR, 0x01, foo);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_write_word() failed"));
		goto end;
	}

      end:

	return result;
}

static int acpi_battery_set_mode(struct acpi_battery *battery)
{
	struct acpi_sbs *sbs = battery->sbs;
	int result = 0;
	s16 battery_mode;

	if (capacity_mode == DEF_CAPACITY_UNIT) {
		goto end;
	}

	result = acpi_sbs_read_word(sbs,
				    ACPI_SB_SMBUS_ADDR, 0x03, &battery_mode);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
		goto end;
	}

	if (capacity_mode == MAH_CAPACITY_UNIT) {
		battery_mode &= 0x7fff;
	} else {
		battery_mode |= 0x8000;
	}
	result = acpi_sbs_write_word(sbs,
				     ACPI_SB_SMBUS_ADDR, 0x03, battery_mode);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_write_word() failed"));
		goto end;
	}

	result = acpi_sbs_read_word(sbs,
				    ACPI_SB_SMBUS_ADDR, 0x03, &battery_mode);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
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
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_select() failed"));
		goto end;
	}

	result = acpi_battery_set_mode(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_set_mode() failed"));
		goto end;
	}

	result = acpi_battery_get_info(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_get_info() failed"));
		goto end;
	}

	result = acpi_battery_get_state(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_get_state() failed"));
		goto end;
	}

	result = acpi_battery_get_alarm(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_get_alarm() failed"));
		goto end;
	}

      end:
	return result;
}

static int acpi_ac_get_present(struct acpi_sbs *sbs)
{
	int result = 0;
	s16 charger_status;

	result = acpi_sbs_read_word(sbs, ACPI_SBC_SMBUS_ADDR, 0x13,
				    &charger_status);

	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_read_word() failed"));
		goto end;
	}

	sbs->ac.ac_present = (charger_status & 0x8000) >> 15;

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
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"proc_mkdir() failed"));
			return -ENODEV;
		}
		(*dir)->owner = THIS_MODULE;
	}

	/* 'info' [R] */
	if (info_fops) {
		entry = create_proc_entry(ACPI_SBS_FILE_INFO, S_IRUGO, *dir);
		if (!entry) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"create_proc_entry() failed"));
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
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"create_proc_entry() failed"));
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
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"create_proc_entry() failed"));
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
	struct acpi_sbs *sbs = battery->sbs;
	int cscale;
	int result = 0;

	if (sbs_mutex_lock(sbs)) {
		return -ENODEV;
	}

	result = acpi_check_update_proc(sbs);
	if (result)
		goto end;

	if (update_time == 0) {
		result = acpi_sbs_update_run(sbs, battery->id, DATA_TYPE_INFO);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_update_run() failed"));
		}
	}

	if (battery->battery_present) {
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
	seq_printf(seq, "design capacity:         %i%s\n",
		   battery->info.design_capacity * cscale,
		   battery->info.capacity_mode ? "0 mWh" : " mAh");

	seq_printf(seq, "last full capacity:      %i%s\n",
		   battery->info.full_charge_capacity * cscale,
		   battery->info.capacity_mode ? "0 mWh" : " mAh");

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

	sbs_mutex_unlock(sbs);

	return result;
}

static int acpi_battery_info_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_info, PDE(inode)->data);
}

static int acpi_battery_read_state(struct seq_file *seq, void *offset)
{
	struct acpi_battery *battery = seq->private;
	struct acpi_sbs *sbs = battery->sbs;
	int result = 0;
	int cscale;
	int foo;

	if (sbs_mutex_lock(sbs)) {
		return -ENODEV;
	}

	result = acpi_check_update_proc(sbs);
	if (result)
		goto end;

	if (update_time == 0) {
		result = acpi_sbs_update_run(sbs, battery->id, DATA_TYPE_STATE);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_update_run() failed"));
		}
	}

	if (battery->battery_present) {
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

	if (battery->state.battery_state & 0x0010) {
		seq_printf(seq, "capacity state:          critical\n");
	} else {
		seq_printf(seq, "capacity state:          ok\n");
	}

	foo = (s16) battery->state.amperage * battery->info.ipscale;
	if (battery->info.capacity_mode) {
		foo = foo * battery->info.design_voltage / 1000;
	}
	if (battery->state.amperage < 0) {
		seq_printf(seq, "charging state:          discharging\n");
		seq_printf(seq, "present rate:            %d %s\n",
			   -foo, battery->info.capacity_mode ? "mW" : "mA");
	} else if (battery->state.amperage > 0) {
		seq_printf(seq, "charging state:          charging\n");
		seq_printf(seq, "present rate:            %d %s\n",
			   foo, battery->info.capacity_mode ? "mW" : "mA");
	} else {
		seq_printf(seq, "charging state:          charged\n");
		seq_printf(seq, "present rate:            0 %s\n",
			   battery->info.capacity_mode ? "mW" : "mA");
	}

	seq_printf(seq, "remaining capacity:      %i%s\n",
		   battery->state.remaining_capacity * cscale,
		   battery->info.capacity_mode ? "0 mWh" : " mAh");

	seq_printf(seq, "present voltage:         %i mV\n",
		   battery->state.voltage * battery->info.vscale);

      end:

	sbs_mutex_unlock(sbs);

	return result;
}

static int acpi_battery_state_open_fs(struct inode *inode, struct file *file)
{
	return single_open(file, acpi_battery_read_state, PDE(inode)->data);
}

static int acpi_battery_read_alarm(struct seq_file *seq, void *offset)
{
	struct acpi_battery *battery = seq->private;
	struct acpi_sbs *sbs = battery->sbs;
	int result = 0;
	int cscale;

	if (sbs_mutex_lock(sbs)) {
		return -ENODEV;
	}

	result = acpi_check_update_proc(sbs);
	if (result)
		goto end;

	if (update_time == 0) {
		result = acpi_sbs_update_run(sbs, battery->id, DATA_TYPE_ALARM);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_update_run() failed"));
		}
	}

	if (!battery->battery_present) {
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
		seq_printf(seq, "%i%s\n",
			   battery->alarm.remaining_capacity * cscale,
			   battery->info.capacity_mode ? "0 mWh" : " mAh");
	} else {
		seq_printf(seq, "disabled\n");
	}

      end:

	sbs_mutex_unlock(sbs);

	return result;
}

static ssize_t
acpi_battery_write_alarm(struct file *file, const char __user * buffer,
			 size_t count, loff_t * ppos)
{
	struct seq_file *seq = file->private_data;
	struct acpi_battery *battery = seq->private;
	struct acpi_sbs *sbs = battery->sbs;
	char alarm_string[12] = { '\0' };
	int result, old_alarm, new_alarm;

	if (sbs_mutex_lock(sbs)) {
		return -ENODEV;
	}

	result = acpi_check_update_proc(sbs);
	if (result)
		goto end;

	if (!battery->battery_present) {
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
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_set_alarm() failed"));
		acpi_battery_set_alarm(battery, old_alarm);
		goto end;
	}
	result = acpi_battery_get_alarm(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_get_alarm() failed"));
		acpi_battery_set_alarm(battery, old_alarm);
		goto end;
	}

      end:
	sbs_mutex_unlock(sbs);

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

	if (sbs_mutex_lock(sbs)) {
		return -ENODEV;
	}

	if (update_time == 0) {
		result = acpi_sbs_update_run(sbs, -1, DATA_TYPE_AC_STATE);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_update_run() failed"));
		}
	}

	seq_printf(seq, "state:                   %s\n",
		   sbs->ac.ac_present ? "on-line" : "off-line");

	sbs_mutex_unlock(sbs);

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
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_select() failed"));
		goto end;
	}

	result = acpi_battery_get_present(battery);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_battery_get_present() failed"));
		goto end;
	}

	is_present = battery->battery_present;

	if (is_present) {
		result = acpi_battery_init(battery);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_battery_init() failed"));
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
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_generic_add_fs() failed"));
		goto end;
	}
	battery->alive = 1;

	printk(KERN_INFO PREFIX "%s [%s]: Battery Slot [%s] (battery %s)\n",
	       ACPI_SBS_DEVICE_NAME, acpi_device_bid(sbs->device), dir_name,
	       sbs->battery->battery_present ? "present" : "absent");

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
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_ac_get_present() failed"));
		goto end;
	}

	result = acpi_sbs_generic_add_fs(&sbs->ac_entry,
					 acpi_ac_dir,
					 ACPI_AC_DIR_NAME,
					 NULL, &acpi_ac_state_fops, NULL, sbs);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_generic_add_fs() failed"));
		goto end;
	}

	printk(KERN_INFO PREFIX "%s [%s]: AC Adapter [%s] (%s)\n",
	       ACPI_SBS_DEVICE_NAME, acpi_device_bid(sbs->device),
	       ACPI_AC_DIR_NAME, sbs->ac.ac_present ? "on-line" : "off-line");

      end:

	return result;
}

static void acpi_ac_remove(struct acpi_sbs *sbs)
{

	if (sbs->ac_entry) {
		acpi_sbs_generic_remove_fs(&sbs->ac_entry, acpi_ac_dir);
	}
}

static void acpi_sbs_update_time_run(unsigned long data)
{
	acpi_os_execute(OSL_GPE_HANDLER, acpi_sbs_update_time, (void *)data);
}

static int acpi_sbs_update_run(struct acpi_sbs *sbs, int id, int data_type)
{
	struct acpi_battery *battery;
	int result = 0, cnt;
	int old_ac_present = -1;
	int old_battery_present = -1;
	int new_ac_present = -1;
	int new_battery_present = -1;
	int id_min = 0, id_max = MAX_SBS_BAT - 1;
	char dir_name[32];
	int do_battery_init = 0, do_ac_init = 0;
	int old_remaining_capacity = 0;
	int update_battery = 1;
	int up_tm = update_time;

	if (sbs_zombie(sbs)) {
		goto end;
	}

	if (id >= 0) {
		id_min = id_max = id;
	}

	if (data_type == DATA_TYPE_COMMON && up_tm > 0) {
		cnt = up_tm / (up_tm > UPDATE_DELAY ? UPDATE_DELAY : up_tm);
		if (sbs->run_cnt % cnt != 0) {
			update_battery = 0;
		}
	}

	sbs->run_cnt++;

	old_ac_present = sbs->ac.ac_present;

	result = acpi_ac_get_present(sbs);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_ac_get_present() failed"));
	}

	new_ac_present = sbs->ac.ac_present;

	do_ac_init = (old_ac_present != new_ac_present);
	if (sbs->run_cnt == 1 && data_type == DATA_TYPE_COMMON) {
		do_ac_init = 1;
	}

	if (do_ac_init) {
		result = acpi_sbs_generate_event(sbs->device,
						 ACPI_SBS_AC_NOTIFY_STATUS,
						 new_ac_present,
						 ACPI_AC_DIR_NAME,
						 ACPI_AC_CLASS);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_generate_event() failed"));
		}
	}

	if (data_type == DATA_TYPE_COMMON) {
		if (!do_ac_init && !update_battery) {
			goto end;
		}
	}

	if (data_type == DATA_TYPE_AC_STATE && !do_ac_init) {
		goto end;
	}

	for (id = id_min; id <= id_max; id++) {
		battery = &sbs->battery[id];
		if (battery->alive == 0) {
			continue;
		}

		old_remaining_capacity = battery->state.remaining_capacity;

		old_battery_present = battery->battery_present;

		result = acpi_battery_select(battery);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_battery_select() failed"));
		}

		result = acpi_battery_get_present(battery);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_battery_get_present() failed"));
		}

		new_battery_present = battery->battery_present;

		do_battery_init = ((old_battery_present != new_battery_present)
				   && new_battery_present);
		if (!new_battery_present)
			goto event;
		if (do_ac_init || do_battery_init) {
			result = acpi_battery_init(battery);
			if (result) {
				ACPI_EXCEPTION((AE_INFO, AE_ERROR,
						"acpi_battery_init() "
						"failed"));
			}
		}
		if (sbs_zombie(sbs)) {
			goto end;
		}

		if ((data_type == DATA_TYPE_COMMON
		     || data_type == DATA_TYPE_INFO)
		    && new_battery_present) {
			result = acpi_battery_get_info(battery);
			if (result) {
				ACPI_EXCEPTION((AE_INFO, AE_ERROR,
						"acpi_battery_get_info() failed"));
			}
		}
		if (data_type == DATA_TYPE_INFO) {
			continue;
		}
		if (sbs_zombie(sbs)) {
			goto end;
		}

		if ((data_type == DATA_TYPE_COMMON
		     || data_type == DATA_TYPE_STATE)
		    && new_battery_present) {
			result = acpi_battery_get_state(battery);
			if (result) {
				ACPI_EXCEPTION((AE_INFO, AE_ERROR,
						"acpi_battery_get_state() failed"));
			}
		}
		if (data_type == DATA_TYPE_STATE) {
			goto event;
		}
		if (sbs_zombie(sbs)) {
			goto end;
		}

		if ((data_type == DATA_TYPE_COMMON
		     || data_type == DATA_TYPE_ALARM)
		    && new_battery_present) {
			result = acpi_battery_get_alarm(battery);
			if (result) {
				ACPI_EXCEPTION((AE_INFO, AE_ERROR,
						"acpi_battery_get_alarm() "
						"failed"));
			}
		}
		if (data_type == DATA_TYPE_ALARM) {
			continue;
		}
		if (sbs_zombie(sbs)) {
			goto end;
		}

	      event:

		if (old_battery_present != new_battery_present || do_ac_init ||
		    old_remaining_capacity !=
		    battery->state.remaining_capacity) {
			sprintf(dir_name, ACPI_BATTERY_DIR_NAME, id);
			result = acpi_sbs_generate_event(sbs->device,
							 ACPI_SBS_BATTERY_NOTIFY_STATUS,
							 new_battery_present,
							 dir_name,
							 ACPI_BATTERY_CLASS);
			if (result) {
				ACPI_EXCEPTION((AE_INFO, AE_ERROR,
						"acpi_sbs_generate_event() "
						"failed"));
			}
		}
	}

      end:

	return result;
}

static void acpi_sbs_update_time(void *data)
{
	struct acpi_sbs *sbs = data;
	unsigned long delay = -1;
	int result;
	unsigned int up_tm = update_time;

	if (sbs_mutex_lock(sbs))
		return;

	result = acpi_sbs_update_run(sbs, -1, DATA_TYPE_COMMON);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_sbs_update_run() failed"));
	}

	if (sbs_zombie(sbs)) {
		goto end;
	}

	if (!up_tm) {
		if (timer_pending(&sbs->update_timer))
			del_timer(&sbs->update_timer);
	} else {
		delay = (up_tm > UPDATE_DELAY ? UPDATE_DELAY : up_tm);
		delay = jiffies + HZ * delay;
		if (timer_pending(&sbs->update_timer)) {
			mod_timer(&sbs->update_timer, delay);
		} else {
			sbs->update_timer.data = (unsigned long)data;
			sbs->update_timer.function = acpi_sbs_update_time_run;
			sbs->update_timer.expires = delay;
			add_timer(&sbs->update_timer);
		}
	}

      end:

	sbs_mutex_unlock(sbs);
}

static int acpi_sbs_add(struct acpi_device *device)
{
	struct acpi_sbs *sbs = NULL;
	int result = 0, remove_result = 0;
	int id;
	acpi_status status = AE_OK;
	unsigned long val;

	status =
	    acpi_evaluate_integer(device->handle, "_EC", NULL, &val);
	if (ACPI_FAILURE(status)) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR, "Error obtaining _EC"));
		return -EIO;
	}

	sbs = kzalloc(sizeof(struct acpi_sbs), GFP_KERNEL);
	if (!sbs) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR, "kzalloc() failed"));
		result = -ENOMEM;
		goto end;
	}

	mutex_init(&sbs->mutex);

	sbs_mutex_lock(sbs);

	sbs->base = 0xff & (val >> 8);
	sbs->device = device;

	strcpy(acpi_device_name(device), ACPI_SBS_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_SBS_CLASS);
	acpi_driver_data(device) = sbs;

	result = acpi_ac_add(sbs);
	if (result) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR, "acpi_ac_add() failed"));
		goto end;
	}

	acpi_sbsm_get_info(sbs);

	if (!sbs->sbsm_present) {
		result = acpi_battery_add(sbs, 0);
		if (result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_battery_add() failed"));
			goto end;
		}
	} else {
		for (id = 0; id < MAX_SBS_BAT; id++) {
			if ((sbs->sbsm_batteries_supported & (1 << id))) {
				result = acpi_battery_add(sbs, id);
				if (result) {
					ACPI_EXCEPTION((AE_INFO, AE_ERROR,
							"acpi_battery_add() failed"));
					goto end;
				}
			}
		}
	}

	init_timer(&sbs->update_timer);
	result = acpi_check_update_proc(sbs);
	if (result)
		goto end;

      end:

	sbs_mutex_unlock(sbs);

	if (result) {
		remove_result = acpi_sbs_remove(device, 0);
		if (remove_result) {
			ACPI_EXCEPTION((AE_INFO, AE_ERROR,
					"acpi_sbs_remove() failed"));
		}
	}

	return result;
}

static int acpi_sbs_remove(struct acpi_device *device, int type)
{
	struct acpi_sbs *sbs;
	int id;

	if (!device) {
		return -EINVAL;
	}

	sbs = acpi_driver_data(device);
	if (!sbs) {
		return -EINVAL;
	}

	sbs_mutex_lock(sbs);

	sbs->zombie = 1;
	del_timer_sync(&sbs->update_timer);
	acpi_os_wait_events_complete(NULL);
	del_timer_sync(&sbs->update_timer);

	for (id = 0; id < MAX_SBS_BAT; id++) {
		acpi_battery_remove(sbs, id);
	}

	acpi_ac_remove(sbs);

	sbs_mutex_unlock(sbs);

	mutex_destroy(&sbs->mutex);

	kfree(sbs);

	return 0;
}

static void acpi_sbs_rmdirs(void)
{
	if (acpi_ac_dir) {
		acpi_unlock_ac_dir(acpi_ac_dir);
		acpi_ac_dir = NULL;
	}
	if (acpi_battery_dir) {
		acpi_unlock_battery_dir(acpi_battery_dir);
		acpi_battery_dir = NULL;
	}
}

static int acpi_sbs_resume(struct acpi_device *device)
{
	struct acpi_sbs *sbs;

	if (!device)
		return -EINVAL;

	sbs = device->driver_data;

	sbs->run_cnt = 0;

	return 0;
}

static int __init acpi_sbs_init(void)
{
	int result = 0;

	if (acpi_disabled)
		return -ENODEV;

	if (capacity_mode != DEF_CAPACITY_UNIT
	    && capacity_mode != MAH_CAPACITY_UNIT
	    && capacity_mode != MWH_CAPACITY_UNIT) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"invalid capacity_mode = %d", capacity_mode));
		return -EINVAL;
	}

	acpi_ac_dir = acpi_lock_ac_dir();
	if (!acpi_ac_dir) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_lock_ac_dir() failed"));
		return -ENODEV;
	}

	acpi_battery_dir = acpi_lock_battery_dir();
	if (!acpi_battery_dir) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_lock_battery_dir() failed"));
		acpi_sbs_rmdirs();
		return -ENODEV;
	}

	result = acpi_bus_register_driver(&acpi_sbs_driver);
	if (result < 0) {
		ACPI_EXCEPTION((AE_INFO, AE_ERROR,
				"acpi_bus_register_driver() failed"));
		acpi_sbs_rmdirs();
		return -ENODEV;
	}

	return 0;
}

static void __exit acpi_sbs_exit(void)
{
	acpi_bus_unregister_driver(&acpi_sbs_driver);

	acpi_sbs_rmdirs();

	return;
}

module_init(acpi_sbs_init);
module_exit(acpi_sbs_exit);
