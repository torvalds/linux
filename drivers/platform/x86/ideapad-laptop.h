/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  ideapad-laptop.h - Lenovo IdeaPad ACPI Extras
 *
 *  Copyright © 2010 Intel Corporation
 *  Copyright © 2010 David Woodhouse <dwmw2@infradead.org>
 */

#ifndef _IDEAPAD_LAPTOP_H_
#define _IDEAPAD_LAPTOP_H_

#include <linux/acpi.h>
#include <linux/jiffies.h>
#include <linux/errno.h>
#include <linux/notifier.h>

int ideapad_laptop_register_notifier(struct notifier_block *nb);
int ideapad_laptop_unregister_notifier(struct notifier_block *nb);
void ideapad_laptop_call_notifier(unsigned long action, void *data);

enum {
	VPCCMD_R_VPC1 = 0x10,
	VPCCMD_R_BL_MAX,
	VPCCMD_R_BL,
	VPCCMD_W_BL,
	VPCCMD_R_WIFI,
	VPCCMD_W_WIFI,
	VPCCMD_R_BT,
	VPCCMD_W_BT,
	VPCCMD_R_BL_POWER,
	VPCCMD_R_NOVO,
	VPCCMD_R_VPC2,
	VPCCMD_R_TOUCHPAD,
	VPCCMD_W_TOUCHPAD,
	VPCCMD_R_CAMERA,
	VPCCMD_W_CAMERA,
	VPCCMD_R_3G,
	VPCCMD_W_3G,
	VPCCMD_R_ODD, /* 0x21 */
	VPCCMD_W_FAN,
	VPCCMD_R_RF,
	VPCCMD_W_RF,
	VPCCMD_W_YMC = 0x2A,
	VPCCMD_R_FAN = 0x2B,
	VPCCMD_R_SPECIAL_BUTTONS = 0x31,
	VPCCMD_W_BL_POWER = 0x33,
};

static inline int eval_int_with_arg(acpi_handle handle, const char *name, unsigned long arg, unsigned long *res)
{
	struct acpi_object_list params;
	unsigned long long result;
	union acpi_object in_obj;
	acpi_status status;

	params.count = 1;
	params.pointer = &in_obj;
	in_obj.type = ACPI_TYPE_INTEGER;
	in_obj.integer.value = arg;

	status = acpi_evaluate_integer(handle, (char *)name, &params, &result);
	if (ACPI_FAILURE(status))
		return -EIO;

	if (res)
		*res = result;

	return 0;
}

static inline int eval_vpcr(acpi_handle handle, unsigned long cmd, unsigned long *res)
{
	return eval_int_with_arg(handle, "VPCR", cmd, res);
}

static inline int eval_vpcw(acpi_handle handle, unsigned long cmd, unsigned long data)
{
	struct acpi_object_list params;
	union acpi_object in_obj[2];
	acpi_status status;

	params.count = 2;
	params.pointer = in_obj;
	in_obj[0].type = ACPI_TYPE_INTEGER;
	in_obj[0].integer.value = cmd;
	in_obj[1].type = ACPI_TYPE_INTEGER;
	in_obj[1].integer.value = data;

	status = acpi_evaluate_object(handle, "VPCW", &params, NULL);
	if (ACPI_FAILURE(status))
		return -EIO;

	return 0;
}

#define IDEAPAD_EC_TIMEOUT 200 /* in ms */

static inline int read_ec_data(acpi_handle handle, unsigned long cmd, unsigned long *data)
{
	unsigned long end_jiffies, val;
	int err;

	err = eval_vpcw(handle, 1, cmd);
	if (err)
		return err;

	end_jiffies = jiffies + msecs_to_jiffies(IDEAPAD_EC_TIMEOUT) + 1;

	while (time_before(jiffies, end_jiffies)) {
		schedule();

		err = eval_vpcr(handle, 1, &val);
		if (err)
			return err;

		if (val == 0)
			return eval_vpcr(handle, 0, data);
	}

	acpi_handle_err(handle, "timeout in %s\n", __func__);

	return -ETIMEDOUT;
}

static inline int write_ec_cmd(acpi_handle handle, unsigned long cmd, unsigned long data)
{
	unsigned long end_jiffies, val;
	int err;

	err = eval_vpcw(handle, 0, data);
	if (err)
		return err;

	err = eval_vpcw(handle, 1, cmd);
	if (err)
		return err;

	end_jiffies = jiffies + msecs_to_jiffies(IDEAPAD_EC_TIMEOUT) + 1;

	while (time_before(jiffies, end_jiffies)) {
		schedule();

		err = eval_vpcr(handle, 1, &val);
		if (err)
			return err;

		if (val == 0)
			return 0;
	}

	acpi_handle_err(handle, "timeout in %s\n", __func__);

	return -ETIMEDOUT;
}

#undef IDEAPAD_EC_TIMEOUT
#endif /* !_IDEAPAD_LAPTOP_H_ */
