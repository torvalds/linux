// SPDX-License-Identifier: GPL-2.0+
/*
 * Supports for the power IC on the Surface 3 tablet.
 *
 * (C) Copyright 2016-2018 Red Hat, Inc
 * (C) Copyright 2016-2018 Benjamin Tissoires <benjamin.tissoires@gmail.com>
 * (C) Copyright 2016 Stephen Just <stephenjust@gmail.com>
 *
 * This driver has been reverse-engineered by parsing the DSDT of the Surface 3
 * and looking at the registers of the chips.
 *
 * The DSDT allowed to find out that:
 * - the driver is required for the ACPI BAT0 device to communicate to the chip
 *   through an operation region.
 * - the various defines for the operation region functions to communicate with
 *   this driver
 * - the DSM 3f99e367-6220-4955-8b0f-06ef2ae79412 allows to trigger ACPI
 *   events to BAT0 (the code is all available in the DSDT).
 *
 * Further findings regarding the 2 chips declared in the MSHW0011 are:
 * - there are 2 chips declared:
 *   . 0x22 seems to control the ADP1 line status (and probably the charger)
 *   . 0x55 controls the battery directly
 * - the battery chip uses a SMBus protocol (using plain SMBus allows non
 *   destructive commands):
 *   . the commands/registers used are in the range 0x00..0x7F
 *   . if bit 8 (0x80) is set in the SMBus command, the returned value is the
 *     same as when it is not set. There is a high chance this bit is the
 *     read/write
 *   . the various registers semantic as been deduced by observing the register
 *     dumps.
 */

#include <linux/acpi.h>
#include <linux/bits.h>
#include <linux/freezer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/kthread.h>
#include <linux/slab.h>
#include <linux/types.h>
#include <linux/uuid.h>
#include <asm/unaligned.h>

#define SURFACE_3_POLL_INTERVAL		(2 * HZ)
#define SURFACE_3_STRLEN		10

struct mshw0011_data {
	struct i2c_client	*adp1;
	struct i2c_client	*bat0;
	unsigned short		notify_mask;
	struct task_struct	*poll_task;
	bool			kthread_running;

	bool			charging;
	bool			bat_charging;
	u8			trip_point;
	s32			full_capacity;
};

struct mshw0011_handler_data {
	struct acpi_connection_info	info;
	struct i2c_client		*client;
};

struct bix {
	u32	revision;
	u32	power_unit;
	u32	design_capacity;
	u32	last_full_charg_capacity;
	u32	battery_technology;
	u32	design_voltage;
	u32	design_capacity_of_warning;
	u32	design_capacity_of_low;
	u32	cycle_count;
	u32	measurement_accuracy;
	u32	max_sampling_time;
	u32	min_sampling_time;
	u32	max_average_interval;
	u32	min_average_interval;
	u32	battery_capacity_granularity_1;
	u32	battery_capacity_granularity_2;
	char	model[SURFACE_3_STRLEN];
	char	serial[SURFACE_3_STRLEN];
	char	type[SURFACE_3_STRLEN];
	char	OEM[SURFACE_3_STRLEN];
} __packed;

struct bst {
	u32	battery_state;
	s32	battery_present_rate;
	u32	battery_remaining_capacity;
	u32	battery_present_voltage;
} __packed;

struct gsb_command {
	u8	arg0;
	u8	arg1;
	u8	arg2;
} __packed;

struct gsb_buffer {
	u8	status;
	u8	len;
	u8	ret;
	union {
		struct gsb_command	cmd;
		struct bst		bst;
		struct bix		bix;
	} __packed;
} __packed;

#define ACPI_BATTERY_STATE_DISCHARGING	BIT(0)
#define ACPI_BATTERY_STATE_CHARGING	BIT(1)
#define ACPI_BATTERY_STATE_CRITICAL	BIT(2)

#define MSHW0011_CMD_DEST_BAT0		0x01
#define MSHW0011_CMD_DEST_ADP1		0x03

#define MSHW0011_CMD_BAT0_STA		0x01
#define MSHW0011_CMD_BAT0_BIX		0x02
#define MSHW0011_CMD_BAT0_BCT		0x03
#define MSHW0011_CMD_BAT0_BTM		0x04
#define MSHW0011_CMD_BAT0_BST		0x05
#define MSHW0011_CMD_BAT0_BTP		0x06
#define MSHW0011_CMD_ADP1_PSR		0x07
#define MSHW0011_CMD_BAT0_PSOC		0x09
#define MSHW0011_CMD_BAT0_PMAX		0x0a
#define MSHW0011_CMD_BAT0_PSRC		0x0b
#define MSHW0011_CMD_BAT0_CHGI		0x0c
#define MSHW0011_CMD_BAT0_ARTG		0x0d

#define MSHW0011_NOTIFY_GET_VERSION	0x00
#define MSHW0011_NOTIFY_ADP1		0x01
#define MSHW0011_NOTIFY_BAT0_BST	0x02
#define MSHW0011_NOTIFY_BAT0_BIX	0x05

#define MSHW0011_ADP1_REG_PSR		0x04

#define MSHW0011_BAT0_REG_CAPACITY		0x0c
#define MSHW0011_BAT0_REG_FULL_CHG_CAPACITY	0x0e
#define MSHW0011_BAT0_REG_DESIGN_CAPACITY	0x40
#define MSHW0011_BAT0_REG_VOLTAGE	0x08
#define MSHW0011_BAT0_REG_RATE		0x14
#define MSHW0011_BAT0_REG_OEM		0x45
#define MSHW0011_BAT0_REG_TYPE		0x4e
#define MSHW0011_BAT0_REG_SERIAL_NO	0x56
#define MSHW0011_BAT0_REG_CYCLE_CNT	0x6e

#define MSHW0011_EV_2_5_MASK		GENMASK(8, 0)

/* 3f99e367-6220-4955-8b0f-06ef2ae79412 */
static const guid_t mshw0011_guid =
	GUID_INIT(0x3F99E367, 0x6220, 0x4955, 0x8B, 0x0F, 0x06, 0xEF,
		  0x2A, 0xE7, 0x94, 0x12);

static int
mshw0011_notify(struct mshw0011_data *cdata, u8 arg1, u8 arg2,
		unsigned int *ret_value)
{
	union acpi_object *obj;
	struct acpi_device *adev;
	acpi_handle handle;
	unsigned int i;

	handle = ACPI_HANDLE(&cdata->adp1->dev);
	if (!handle || acpi_bus_get_device(handle, &adev))
		return -ENODEV;

	obj = acpi_evaluate_dsm_typed(handle, &mshw0011_guid, arg1, arg2, NULL,
				      ACPI_TYPE_BUFFER);
	if (!obj) {
		dev_err(&cdata->adp1->dev, "device _DSM execution failed\n");
		return -ENODEV;
	}

	*ret_value = 0;
	for (i = 0; i < obj->buffer.length; i++)
		*ret_value |= obj->buffer.pointer[i] << (i * 8);

	ACPI_FREE(obj);
	return 0;
}

static const struct bix default_bix = {
	.revision = 0x00,
	.power_unit = 0x01,
	.design_capacity = 0x1dca,
	.last_full_charg_capacity = 0x1dca,
	.battery_technology = 0x01,
	.design_voltage = 0x10df,
	.design_capacity_of_warning = 0x8f,
	.design_capacity_of_low = 0x47,
	.cycle_count = 0xffffffff,
	.measurement_accuracy = 0x00015f90,
	.max_sampling_time = 0x03e8,
	.min_sampling_time = 0x03e8,
	.max_average_interval = 0x03e8,
	.min_average_interval = 0x03e8,
	.battery_capacity_granularity_1 = 0x45,
	.battery_capacity_granularity_2 = 0x11,
	.model = "P11G8M",
	.serial = "",
	.type = "LION",
	.OEM = "",
};

static int mshw0011_bix(struct mshw0011_data *cdata, struct bix *bix)
{
	struct i2c_client *client = cdata->bat0;
	char buf[SURFACE_3_STRLEN];
	int ret;

	*bix = default_bix;

	/* get design capacity */
	ret = i2c_smbus_read_word_data(client,
				       MSHW0011_BAT0_REG_DESIGN_CAPACITY);
	if (ret < 0) {
		dev_err(&client->dev, "Error reading design capacity: %d\n",
			ret);
		return ret;
	}
	bix->design_capacity = ret;

	/* get last full charge capacity */
	ret = i2c_smbus_read_word_data(client,
				       MSHW0011_BAT0_REG_FULL_CHG_CAPACITY);
	if (ret < 0) {
		dev_err(&client->dev,
			"Error reading last full charge capacity: %d\n", ret);
		return ret;
	}
	bix->last_full_charg_capacity = ret;

	/* get serial number */
	ret = i2c_smbus_read_i2c_block_data(client, MSHW0011_BAT0_REG_SERIAL_NO,
					    sizeof(buf), buf);
	if (ret != sizeof(buf)) {
		dev_err(&client->dev, "Error reading serial no: %d\n", ret);
		return ret;
	}
	snprintf(bix->serial, ARRAY_SIZE(bix->serial), "%3pE%6pE", buf + 7, buf);

	/* get cycle count */
	ret = i2c_smbus_read_word_data(client, MSHW0011_BAT0_REG_CYCLE_CNT);
	if (ret < 0) {
		dev_err(&client->dev, "Error reading cycle count: %d\n", ret);
		return ret;
	}
	bix->cycle_count = ret;

	/* get OEM name */
	ret = i2c_smbus_read_i2c_block_data(client, MSHW0011_BAT0_REG_OEM,
					    4, buf);
	if (ret != 4) {
		dev_err(&client->dev, "Error reading cycle count: %d\n", ret);
		return ret;
	}
	snprintf(bix->OEM, ARRAY_SIZE(bix->OEM), "%3pE", buf);

	return 0;
}

static int mshw0011_bst(struct mshw0011_data *cdata, struct bst *bst)
{
	struct i2c_client *client = cdata->bat0;
	int rate, capacity, voltage, state;
	s16 tmp;

	rate = i2c_smbus_read_word_data(client, MSHW0011_BAT0_REG_RATE);
	if (rate < 0)
		return rate;

	capacity = i2c_smbus_read_word_data(client, MSHW0011_BAT0_REG_CAPACITY);
	if (capacity < 0)
		return capacity;

	voltage = i2c_smbus_read_word_data(client, MSHW0011_BAT0_REG_VOLTAGE);
	if (voltage < 0)
		return voltage;

	tmp = rate;
	bst->battery_present_rate = abs((s32)tmp);

	state = 0;
	if ((s32) tmp > 0)
		state |= ACPI_BATTERY_STATE_CHARGING;
	else if ((s32) tmp < 0)
		state |= ACPI_BATTERY_STATE_DISCHARGING;
	bst->battery_state = state;

	bst->battery_remaining_capacity = capacity;
	bst->battery_present_voltage = voltage;

	return 0;
}

static int mshw0011_adp_psr(struct mshw0011_data *cdata)
{
	return i2c_smbus_read_byte_data(cdata->adp1, MSHW0011_ADP1_REG_PSR);
}

static int mshw0011_isr(struct mshw0011_data *cdata)
{
	struct bst bst;
	struct bix bix;
	int ret;
	bool status, bat_status;

	ret = mshw0011_adp_psr(cdata);
	if (ret < 0)
		return ret;

	status = ret;
	if (status != cdata->charging)
		mshw0011_notify(cdata, cdata->notify_mask,
				MSHW0011_NOTIFY_ADP1, &ret);

	cdata->charging = status;

	ret = mshw0011_bst(cdata, &bst);
	if (ret < 0)
		return ret;

	bat_status = bst.battery_state;
	if (bat_status != cdata->bat_charging)
		mshw0011_notify(cdata, cdata->notify_mask,
				MSHW0011_NOTIFY_BAT0_BST, &ret);

	cdata->bat_charging = bat_status;

	ret = mshw0011_bix(cdata, &bix);
	if (ret < 0)
		return ret;

	if (bix.last_full_charg_capacity != cdata->full_capacity)
		mshw0011_notify(cdata, cdata->notify_mask,
				MSHW0011_NOTIFY_BAT0_BIX, &ret);

	cdata->full_capacity = bix.last_full_charg_capacity;

	return 0;
}

static int mshw0011_poll_task(void *data)
{
	struct mshw0011_data *cdata = data;
	int ret = 0;

	cdata->kthread_running = true;

	set_freezable();

	while (!kthread_should_stop()) {
		schedule_timeout_interruptible(SURFACE_3_POLL_INTERVAL);
		try_to_freeze();
		ret = mshw0011_isr(data);
		if (ret)
			break;
	}

	cdata->kthread_running = false;
	return ret;
}

static acpi_status
mshw0011_space_handler(u32 function, acpi_physical_address command,
			u32 bits, u64 *value64,
			void *handler_context, void *region_context)
{
	struct gsb_buffer *gsb = (struct gsb_buffer *)value64;
	struct mshw0011_handler_data *data = handler_context;
	struct acpi_connection_info *info = &data->info;
	struct acpi_resource_i2c_serialbus *sb;
	struct i2c_client *client = data->client;
	struct mshw0011_data *cdata = i2c_get_clientdata(client);
	struct acpi_resource *ares;
	u32 accessor_type = function >> 16;
	acpi_status ret;
	int status = 1;

	ret = acpi_buffer_to_resource(info->connection, info->length, &ares);
	if (ACPI_FAILURE(ret))
		return ret;

	if (!value64 || ares->type != ACPI_RESOURCE_TYPE_SERIAL_BUS) {
		ret = AE_BAD_PARAMETER;
		goto err;
	}

	sb = &ares->data.i2c_serial_bus;
	if (sb->type != ACPI_RESOURCE_SERIAL_TYPE_I2C) {
		ret = AE_BAD_PARAMETER;
		goto err;
	}

	if (accessor_type != ACPI_GSB_ACCESS_ATTRIB_RAW_PROCESS) {
		ret = AE_BAD_PARAMETER;
		goto err;
	}

	if (gsb->cmd.arg0 == MSHW0011_CMD_DEST_ADP1 &&
	    gsb->cmd.arg1 == MSHW0011_CMD_ADP1_PSR) {
		status = mshw0011_adp_psr(cdata);
		if (status >= 0) {
			ret = AE_OK;
			goto out;
		} else {
			ret = AE_ERROR;
			goto err;
		}
	}

	if (gsb->cmd.arg0 != MSHW0011_CMD_DEST_BAT0) {
		ret = AE_BAD_PARAMETER;
		goto err;
	}

	switch (gsb->cmd.arg1) {
	case MSHW0011_CMD_BAT0_STA:
		break;
	case MSHW0011_CMD_BAT0_BIX:
		ret = mshw0011_bix(cdata, &gsb->bix);
		break;
	case MSHW0011_CMD_BAT0_BTP:
		cdata->trip_point = gsb->cmd.arg2;
		break;
	case MSHW0011_CMD_BAT0_BST:
		ret = mshw0011_bst(cdata, &gsb->bst);
		break;
	default:
		dev_info(&cdata->bat0->dev, "command(0x%02x) is not supported.\n", gsb->cmd.arg1);
		ret = AE_BAD_PARAMETER;
		goto err;
	}

 out:
	gsb->ret = status;
	gsb->status = 0;

 err:
	ACPI_FREE(ares);
	return ret;
}

static int mshw0011_install_space_handler(struct i2c_client *client)
{
	struct acpi_device *adev;
	struct mshw0011_handler_data *data;
	acpi_status status;

	adev = ACPI_COMPANION(&client->dev);
	if (!adev)
		return -ENODEV;

	data = kzalloc(sizeof(struct mshw0011_handler_data),
			    GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->client = client;
	status = acpi_bus_attach_private_data(adev->handle, (void *)data);
	if (ACPI_FAILURE(status)) {
		kfree(data);
		return -ENOMEM;
	}

	status = acpi_install_address_space_handler(adev->handle,
						    ACPI_ADR_SPACE_GSBUS,
						    &mshw0011_space_handler,
						    NULL,
						    data);
	if (ACPI_FAILURE(status)) {
		dev_err(&client->dev, "Error installing i2c space handler\n");
		acpi_bus_detach_private_data(adev->handle);
		kfree(data);
		return -ENOMEM;
	}

	acpi_dev_clear_dependencies(adev);
	return 0;
}

static void mshw0011_remove_space_handler(struct i2c_client *client)
{
	struct mshw0011_handler_data *data;
	acpi_handle handle;
	acpi_status status;

	handle = ACPI_HANDLE(&client->dev);
	if (!handle)
		return;

	acpi_remove_address_space_handler(handle,
				ACPI_ADR_SPACE_GSBUS,
				&mshw0011_space_handler);

	status = acpi_bus_get_private_data(handle, (void **)&data);
	if (ACPI_SUCCESS(status))
		kfree(data);

	acpi_bus_detach_private_data(handle);
}

static int mshw0011_probe(struct i2c_client *client)
{
	struct i2c_board_info board_info;
	struct device *dev = &client->dev;
	struct i2c_client *bat0;
	struct mshw0011_data *data;
	int error, mask;

	data = devm_kzalloc(dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->adp1 = client;
	i2c_set_clientdata(client, data);

	memset(&board_info, 0, sizeof(board_info));
	strlcpy(board_info.type, "MSHW0011-bat0", I2C_NAME_SIZE);

	bat0 = i2c_acpi_new_device(dev, 1, &board_info);
	if (IS_ERR(bat0))
		return PTR_ERR(bat0);

	data->bat0 = bat0;
	i2c_set_clientdata(bat0, data);

	error = mshw0011_notify(data, 1, MSHW0011_NOTIFY_GET_VERSION, &mask);
	if (error)
		goto out_err;

	data->notify_mask = mask == MSHW0011_EV_2_5_MASK;

	data->poll_task = kthread_run(mshw0011_poll_task, data, "mshw0011_adp");
	if (IS_ERR(data->poll_task)) {
		error = PTR_ERR(data->poll_task);
		dev_err(&client->dev, "Unable to run kthread err %d\n", error);
		goto out_err;
	}

	error = mshw0011_install_space_handler(client);
	if (error)
		goto out_err;

	return 0;

out_err:
	if (data->kthread_running)
		kthread_stop(data->poll_task);
	i2c_unregister_device(data->bat0);
	return error;
}

static int mshw0011_remove(struct i2c_client *client)
{
	struct mshw0011_data *cdata = i2c_get_clientdata(client);

	mshw0011_remove_space_handler(client);

	if (cdata->kthread_running)
		kthread_stop(cdata->poll_task);

	i2c_unregister_device(cdata->bat0);

	return 0;
}

static const struct acpi_device_id mshw0011_acpi_match[] = {
	{ "MSHW0011", 0 },
	{ }
};
MODULE_DEVICE_TABLE(acpi, mshw0011_acpi_match);

static struct i2c_driver mshw0011_driver = {
	.probe_new = mshw0011_probe,
	.remove = mshw0011_remove,
	.driver = {
		.name = "mshw0011",
		.acpi_match_table = mshw0011_acpi_match,
	},
};
module_i2c_driver(mshw0011_driver);

MODULE_AUTHOR("Benjamin Tissoires <benjamin.tissoires@gmail.com>");
MODULE_DESCRIPTION("mshw0011 driver");
MODULE_LICENSE("GPL v2");
