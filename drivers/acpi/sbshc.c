/*
 * SMBus driver for ACPI Embedded Controller (v0.1)
 *
 * Copyright (c) 2007 Alexey Starikovskiy
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation version 2.
 */

#include <acpi/acpi_bus.h>
#include <acpi/acpi_drivers.h>
#include <acpi/actypes.h>
#include <linux/wait.h>
#include <linux/delay.h>
#include <linux/interrupt.h>
#include "sbshc.h"

#define ACPI_SMB_HC_CLASS	"smbus_host_controller"
#define ACPI_SMB_HC_DEVICE_NAME	"ACPI SMBus HC"

struct acpi_smb_hc {
	struct acpi_ec *ec;
	struct mutex lock;
	wait_queue_head_t wait;
	u8 offset;
	u8 query_bit;
	smbus_alarm_callback callback;
	void *context;
};

static int acpi_smbus_hc_add(struct acpi_device *device);
static int acpi_smbus_hc_remove(struct acpi_device *device, int type);

static const struct acpi_device_id sbs_device_ids[] = {
	{"ACPI0001", 0},
	{"ACPI0005", 0},
	{"", 0},
};

MODULE_DEVICE_TABLE(acpi, sbs_device_ids);

static struct acpi_driver acpi_smb_hc_driver = {
	.name = "smbus_hc",
	.class = ACPI_SMB_HC_CLASS,
	.ids = sbs_device_ids,
	.ops = {
		.add = acpi_smbus_hc_add,
		.remove = acpi_smbus_hc_remove,
		},
};

union acpi_smb_status {
	u8 raw;
	struct {
		u8 status:5;
		u8 reserved:1;
		u8 alarm:1;
		u8 done:1;
	} fields;
};

enum acpi_smb_status_codes {
	SMBUS_OK = 0,
	SMBUS_UNKNOWN_FAILURE = 0x07,
	SMBUS_DEVICE_ADDRESS_NACK = 0x10,
	SMBUS_DEVICE_ERROR = 0x11,
	SMBUS_DEVICE_COMMAND_ACCESS_DENIED = 0x12,
	SMBUS_UNKNOWN_ERROR = 0x13,
	SMBUS_DEVICE_ACCESS_DENIED = 0x17,
	SMBUS_TIMEOUT = 0x18,
	SMBUS_HOST_UNSUPPORTED_PROTOCOL = 0x19,
	SMBUS_BUSY = 0x1a,
	SMBUS_PEC_ERROR = 0x1f,
};

enum acpi_smb_offset {
	ACPI_SMB_PROTOCOL = 0,	/* protocol, PEC */
	ACPI_SMB_STATUS = 1,	/* status */
	ACPI_SMB_ADDRESS = 2,	/* address */
	ACPI_SMB_COMMAND = 3,	/* command */
	ACPI_SMB_DATA = 4,	/* 32 data registers */
	ACPI_SMB_BLOCK_COUNT = 0x24,	/* number of data bytes */
	ACPI_SMB_ALARM_ADDRESS = 0x25,	/* alarm address */
	ACPI_SMB_ALARM_DATA = 0x26,	/* 2 bytes alarm data */
};

static inline int smb_hc_read(struct acpi_smb_hc *hc, u8 address, u8 *data)
{
	return ec_read(hc->offset + address, data);
}

static inline int smb_hc_write(struct acpi_smb_hc *hc, u8 address, u8 data)
{
	return ec_write(hc->offset + address, data);
}

static inline int smb_check_done(struct acpi_smb_hc *hc)
{
	union acpi_smb_status status = {.raw = 0};
	smb_hc_read(hc, ACPI_SMB_STATUS, &status.raw);
	return status.fields.done && (status.fields.status == SMBUS_OK);
}

static int wait_transaction_complete(struct acpi_smb_hc *hc, int timeout)
{
	if (wait_event_timeout(hc->wait, smb_check_done(hc),
			       msecs_to_jiffies(timeout)))
		return 0;
	else
		return -ETIME;
}

static int acpi_smbus_transaction(struct acpi_smb_hc *hc, u8 protocol,
				  u8 address, u8 command, u8 *data, u8 length)
{
	int ret = -EFAULT, i;
	u8 temp, sz = 0;

	mutex_lock(&hc->lock);
	if (smb_hc_read(hc, ACPI_SMB_PROTOCOL, &temp))
		goto end;
	if (temp) {
		ret = -EBUSY;
		goto end;
	}
	smb_hc_write(hc, ACPI_SMB_COMMAND, command);
	smb_hc_write(hc, ACPI_SMB_COMMAND, command);
	if (!(protocol & 0x01)) {
		smb_hc_write(hc, ACPI_SMB_BLOCK_COUNT, length);
		for (i = 0; i < length; ++i)
			smb_hc_write(hc, ACPI_SMB_DATA + i, data[i]);
	}
	smb_hc_write(hc, ACPI_SMB_ADDRESS, address << 1);
	smb_hc_write(hc, ACPI_SMB_PROTOCOL, protocol);
	/*
	 * Wait for completion. Save the status code, data size,
	 * and data into the return package (if required by the protocol).
	 */
	ret = wait_transaction_complete(hc, 1000);
	if (ret || !(protocol & 0x01))
		goto end;
	switch (protocol) {
	case SMBUS_RECEIVE_BYTE:
	case SMBUS_READ_BYTE:
		sz = 1;
		break;
	case SMBUS_READ_WORD:
		sz = 2;
		break;
	case SMBUS_READ_BLOCK:
		if (smb_hc_read(hc, ACPI_SMB_BLOCK_COUNT, &sz)) {
			ret = -EFAULT;
			goto end;
		}
		sz &= 0x1f;
		break;
	}
	for (i = 0; i < sz; ++i)
		smb_hc_read(hc, ACPI_SMB_DATA + i, &data[i]);
      end:
	mutex_unlock(&hc->lock);
	return ret;
}

int acpi_smbus_read(struct acpi_smb_hc *hc, u8 protocol, u8 address,
		    u8 command, u8 *data)
{
	return acpi_smbus_transaction(hc, protocol, address, command, data, 0);
}

EXPORT_SYMBOL_GPL(acpi_smbus_read);

int acpi_smbus_write(struct acpi_smb_hc *hc, u8 protocol, u8 address,
		     u8 command, u8 *data, u8 length)
{
	return acpi_smbus_transaction(hc, protocol, address, command, data, length);
}

EXPORT_SYMBOL_GPL(acpi_smbus_write);

int acpi_smbus_register_callback(struct acpi_smb_hc *hc,
			         smbus_alarm_callback callback, void *context)
{
	mutex_lock(&hc->lock);
	hc->callback = callback;
	hc->context = context;
	mutex_unlock(&hc->lock);
	return 0;
}

EXPORT_SYMBOL_GPL(acpi_smbus_register_callback);

int acpi_smbus_unregister_callback(struct acpi_smb_hc *hc)
{
	mutex_lock(&hc->lock);
	hc->callback = NULL;
	hc->context = NULL;
	mutex_unlock(&hc->lock);
	return 0;
}

EXPORT_SYMBOL_GPL(acpi_smbus_unregister_callback);

static inline void acpi_smbus_callback(void *context)
{
	struct acpi_smb_hc *hc = context;
	if (hc->callback)
		hc->callback(hc->context);
}

static int smbus_alarm(void *context)
{
	struct acpi_smb_hc *hc = context;
	union acpi_smb_status status;
	u8 address;
	if (smb_hc_read(hc, ACPI_SMB_STATUS, &status.raw))
		return 0;
	/* Check if it is only a completion notify */
	if (status.fields.done)
		wake_up(&hc->wait);
	if (!status.fields.alarm)
		return 0;
	mutex_lock(&hc->lock);
	smb_hc_read(hc, ACPI_SMB_ALARM_ADDRESS, &address);
	status.fields.alarm = 0;
	smb_hc_write(hc, ACPI_SMB_STATUS, status.raw);
	/* We are only interested in events coming from known devices */
	switch (address >> 1) {
		case ACPI_SBS_CHARGER:
		case ACPI_SBS_MANAGER:
		case ACPI_SBS_BATTERY:
			acpi_os_execute(OSL_GPE_HANDLER,
					acpi_smbus_callback, hc);
		default:;
	}
	mutex_unlock(&hc->lock);
	return 0;
}

typedef int (*acpi_ec_query_func) (void *data);

extern int acpi_ec_add_query_handler(struct acpi_ec *ec, u8 query_bit,
			      acpi_handle handle, acpi_ec_query_func func,
			      void *data);

static int acpi_smbus_hc_add(struct acpi_device *device)
{
	int status;
	unsigned long val;
	struct acpi_smb_hc *hc;

	if (!device)
		return -EINVAL;

	status = acpi_evaluate_integer(device->handle, "_EC", NULL, &val);
	if (ACPI_FAILURE(status)) {
		printk(KERN_ERR PREFIX "error obtaining _EC.\n");
		return -EIO;
	}

	strcpy(acpi_device_name(device), ACPI_SMB_HC_DEVICE_NAME);
	strcpy(acpi_device_class(device), ACPI_SMB_HC_CLASS);

	hc = kzalloc(sizeof(struct acpi_smb_hc), GFP_KERNEL);
	if (!hc)
		return -ENOMEM;
	mutex_init(&hc->lock);
	init_waitqueue_head(&hc->wait);

	hc->ec = acpi_driver_data(device->parent);
	hc->offset = (val >> 8) & 0xff;
	hc->query_bit = val & 0xff;
	acpi_driver_data(device) = hc;

	acpi_ec_add_query_handler(hc->ec, hc->query_bit, NULL, smbus_alarm, hc);
	printk(KERN_INFO PREFIX "SBS HC: EC = 0x%p, offset = 0x%0x, query_bit = 0x%0x\n",
		hc->ec, hc->offset, hc->query_bit);

	return 0;
}

extern void acpi_ec_remove_query_handler(struct acpi_ec *ec, u8 query_bit);

static int acpi_smbus_hc_remove(struct acpi_device *device, int type)
{
	struct acpi_smb_hc *hc;

	if (!device)
		return -EINVAL;

	hc = acpi_driver_data(device);
	acpi_ec_remove_query_handler(hc->ec, hc->query_bit);
	kfree(hc);
	return 0;
}

static int __init acpi_smb_hc_init(void)
{
	int result;

	result = acpi_bus_register_driver(&acpi_smb_hc_driver);
	if (result < 0)
		return -ENODEV;
	return 0;
}

static void __exit acpi_smb_hc_exit(void)
{
	acpi_bus_unregister_driver(&acpi_smb_hc_driver);
}

module_init(acpi_smb_hc_init);
module_exit(acpi_smb_hc_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("Alexey Starikovskiy");
MODULE_DESCRIPTION("ACPI SMBus HC driver");
