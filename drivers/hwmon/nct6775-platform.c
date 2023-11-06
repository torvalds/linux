// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * nct6775 - Platform driver for the hardware monitoring
 *	     functionality of Nuvoton NCT677x Super-I/O chips
 *
 * Copyright (C) 2012  Guenter Roeck <linux@roeck-us.net>
 */

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/acpi.h>
#include <linux/dmi.h>
#include <linux/hwmon-sysfs.h>
#include <linux/hwmon-vid.h>
#include <linux/init.h>
#include <linux/io.h>
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>

#include "nct6775.h"

enum sensor_access { access_direct, access_asuswmi };

static const char * const nct6775_sio_names[] __initconst = {
	"NCT6106D",
	"NCT6116D",
	"NCT6775F",
	"NCT6776D/F",
	"NCT6779D",
	"NCT6791D",
	"NCT6792D",
	"NCT6793D",
	"NCT6795D",
	"NCT6796D",
	"NCT6797D",
	"NCT6798D",
	"NCT6799D",
};

static unsigned short force_id;
module_param(force_id, ushort, 0);
MODULE_PARM_DESC(force_id, "Override the detected device ID");

static unsigned short fan_debounce;
module_param(fan_debounce, ushort, 0);
MODULE_PARM_DESC(fan_debounce, "Enable debouncing for fan RPM signal");

#define DRVNAME "nct6775"

#define NCT6775_PORT_CHIPID	0x58

/*
 * ISA constants
 */

#define IOREGION_ALIGNMENT	(~7)
#define IOREGION_OFFSET		5
#define IOREGION_LENGTH		2
#define ADDR_REG_OFFSET		0
#define DATA_REG_OFFSET		1

/*
 * Super-I/O constants and functions
 */

#define NCT6775_LD_ACPI		0x0a
#define NCT6775_LD_HWM		0x0b
#define NCT6775_LD_VID		0x0d
#define NCT6775_LD_12		0x12

#define SIO_REG_LDSEL		0x07	/* Logical device select */
#define SIO_REG_DEVID		0x20	/* Device ID (2 bytes) */
#define SIO_REG_ENABLE		0x30	/* Logical device enable */
#define SIO_REG_ADDR		0x60	/* Logical device address (2 bytes) */

#define SIO_NCT6106_ID		0xc450
#define SIO_NCT6116_ID		0xd280
#define SIO_NCT6775_ID		0xb470
#define SIO_NCT6776_ID		0xc330
#define SIO_NCT6779_ID		0xc560
#define SIO_NCT6791_ID		0xc800
#define SIO_NCT6792_ID		0xc910
#define SIO_NCT6793_ID		0xd120
#define SIO_NCT6795_ID		0xd350
#define SIO_NCT6796_ID		0xd420
#define SIO_NCT6797_ID		0xd450
#define SIO_NCT6798_ID		0xd428
#define SIO_NCT6799_ID		0xd800
#define SIO_ID_MASK		0xFFF8

/*
 * Control registers
 */
#define NCT6775_REG_CR_FAN_DEBOUNCE	0xf0

struct nct6775_sio_data {
	int sioreg;
	int ld;
	enum kinds kind;
	enum sensor_access access;

	/* superio_() callbacks  */
	void (*sio_outb)(struct nct6775_sio_data *sio_data, int reg, int val);
	int (*sio_inb)(struct nct6775_sio_data *sio_data, int reg);
	void (*sio_select)(struct nct6775_sio_data *sio_data, int ld);
	int (*sio_enter)(struct nct6775_sio_data *sio_data);
	void (*sio_exit)(struct nct6775_sio_data *sio_data);
};

#define ASUSWMI_METHOD			"WMBD"
#define ASUSWMI_METHODID_RSIO		0x5253494F
#define ASUSWMI_METHODID_WSIO		0x5753494F
#define ASUSWMI_METHODID_RHWM		0x5248574D
#define ASUSWMI_METHODID_WHWM		0x5748574D
#define ASUSWMI_UNSUPPORTED_METHOD	0xFFFFFFFE
#define ASUSWMI_DEVICE_HID		"PNP0C14"
#define ASUSWMI_DEVICE_UID		"ASUSWMI"
#define ASUSMSI_DEVICE_UID		"AsusMbSwInterface"

#if IS_ENABLED(CONFIG_ACPI)
/*
 * ASUS boards have only one device with WMI "WMBD" method and have provided
 * access to only one SuperIO chip at 0x0290.
 */
static struct acpi_device *asus_acpi_dev;
#endif

static int nct6775_asuswmi_evaluate_method(u32 method_id, u8 bank, u8 reg, u8 val, u32 *retval)
{
#if IS_ENABLED(CONFIG_ACPI)
	acpi_handle handle = acpi_device_handle(asus_acpi_dev);
	u32 args = bank | (reg << 8) | (val << 16);
	struct acpi_object_list input;
	union acpi_object params[3];
	unsigned long long result;
	acpi_status status;

	params[0].type = ACPI_TYPE_INTEGER;
	params[0].integer.value = 0;
	params[1].type = ACPI_TYPE_INTEGER;
	params[1].integer.value = method_id;
	params[2].type = ACPI_TYPE_BUFFER;
	params[2].buffer.length = sizeof(args);
	params[2].buffer.pointer = (void *)&args;
	input.count = 3;
	input.pointer = params;

	status = acpi_evaluate_integer(handle, ASUSWMI_METHOD, &input, &result);
	if (ACPI_FAILURE(status))
		return -EIO;

	if (retval)
		*retval = result;

	return 0;
#else
	return -EOPNOTSUPP;
#endif
}

static inline int nct6775_asuswmi_write(u8 bank, u8 reg, u8 val)
{
	return nct6775_asuswmi_evaluate_method(ASUSWMI_METHODID_WHWM, bank,
					      reg, val, NULL);
}

static inline int nct6775_asuswmi_read(u8 bank, u8 reg, u8 *val)
{
	u32 ret, tmp = 0;

	ret = nct6775_asuswmi_evaluate_method(ASUSWMI_METHODID_RHWM, bank,
					      reg, 0, &tmp);
	*val = tmp;
	return ret;
}

static int superio_wmi_inb(struct nct6775_sio_data *sio_data, int reg)
{
	int tmp = 0;

	nct6775_asuswmi_evaluate_method(ASUSWMI_METHODID_RSIO, sio_data->ld,
					reg, 0, &tmp);
	return tmp;
}

static void superio_wmi_outb(struct nct6775_sio_data *sio_data, int reg, int val)
{
	nct6775_asuswmi_evaluate_method(ASUSWMI_METHODID_WSIO, sio_data->ld,
					reg, val, NULL);
}

static void superio_wmi_select(struct nct6775_sio_data *sio_data, int ld)
{
	sio_data->ld = ld;
}

static int superio_wmi_enter(struct nct6775_sio_data *sio_data)
{
	return 0;
}

static void superio_wmi_exit(struct nct6775_sio_data *sio_data)
{
}

static void superio_outb(struct nct6775_sio_data *sio_data, int reg, int val)
{
	int ioreg = sio_data->sioreg;

	outb(reg, ioreg);
	outb(val, ioreg + 1);
}

static int superio_inb(struct nct6775_sio_data *sio_data, int reg)
{
	int ioreg = sio_data->sioreg;

	outb(reg, ioreg);
	return inb(ioreg + 1);
}

static void superio_select(struct nct6775_sio_data *sio_data, int ld)
{
	int ioreg = sio_data->sioreg;

	outb(SIO_REG_LDSEL, ioreg);
	outb(ld, ioreg + 1);
}

static int superio_enter(struct nct6775_sio_data *sio_data)
{
	int ioreg = sio_data->sioreg;

	/*
	 * Try to reserve <ioreg> and <ioreg + 1> for exclusive access.
	 */
	if (!request_muxed_region(ioreg, 2, DRVNAME))
		return -EBUSY;

	outb(0x87, ioreg);
	outb(0x87, ioreg);

	return 0;
}

static void superio_exit(struct nct6775_sio_data *sio_data)
{
	int ioreg = sio_data->sioreg;

	outb(0xaa, ioreg);
	outb(0x02, ioreg);
	outb(0x02, ioreg + 1);
	release_region(ioreg, 2);
}

static inline void nct6775_wmi_set_bank(struct nct6775_data *data, u16 reg)
{
	u8 bank = reg >> 8;

	data->bank = bank;
}

static int nct6775_wmi_reg_read(void *ctx, unsigned int reg, unsigned int *val)
{
	struct nct6775_data *data = ctx;
	int err, word_sized = nct6775_reg_is_word_sized(data, reg);
	u8 tmp = 0;
	u16 res;

	nct6775_wmi_set_bank(data, reg);

	err = nct6775_asuswmi_read(data->bank, reg & 0xff, &tmp);
	if (err)
		return err;

	res = tmp;
	if (word_sized) {
		err = nct6775_asuswmi_read(data->bank, (reg & 0xff) + 1, &tmp);
		if (err)
			return err;

		res = (res << 8) + tmp;
	}
	*val = res;
	return 0;
}

static int nct6775_wmi_reg_write(void *ctx, unsigned int reg, unsigned int value)
{
	struct nct6775_data *data = ctx;
	int res, word_sized = nct6775_reg_is_word_sized(data, reg);

	nct6775_wmi_set_bank(data, reg);

	if (word_sized) {
		res = nct6775_asuswmi_write(data->bank, reg & 0xff, value >> 8);
		if (res)
			return res;

		res = nct6775_asuswmi_write(data->bank, (reg & 0xff) + 1, value);
	} else {
		res = nct6775_asuswmi_write(data->bank, reg & 0xff, value);
	}

	return res;
}

/*
 * On older chips, only registers 0x50-0x5f are banked.
 * On more recent chips, all registers are banked.
 * Assume that is the case and set the bank number for each access.
 * Cache the bank number so it only needs to be set if it changes.
 */
static inline void nct6775_set_bank(struct nct6775_data *data, u16 reg)
{
	u8 bank = reg >> 8;

	if (data->bank != bank) {
		outb_p(NCT6775_REG_BANK, data->addr + ADDR_REG_OFFSET);
		outb_p(bank, data->addr + DATA_REG_OFFSET);
		data->bank = bank;
	}
}

static int nct6775_reg_read(void *ctx, unsigned int reg, unsigned int *val)
{
	struct nct6775_data *data = ctx;
	int word_sized = nct6775_reg_is_word_sized(data, reg);

	nct6775_set_bank(data, reg);
	outb_p(reg & 0xff, data->addr + ADDR_REG_OFFSET);
	*val = inb_p(data->addr + DATA_REG_OFFSET);
	if (word_sized) {
		outb_p((reg & 0xff) + 1,
		       data->addr + ADDR_REG_OFFSET);
		*val = (*val << 8) + inb_p(data->addr + DATA_REG_OFFSET);
	}
	return 0;
}

static int nct6775_reg_write(void *ctx, unsigned int reg, unsigned int value)
{
	struct nct6775_data *data = ctx;
	int word_sized = nct6775_reg_is_word_sized(data, reg);

	nct6775_set_bank(data, reg);
	outb_p(reg & 0xff, data->addr + ADDR_REG_OFFSET);
	if (word_sized) {
		outb_p(value >> 8, data->addr + DATA_REG_OFFSET);
		outb_p((reg & 0xff) + 1,
		       data->addr + ADDR_REG_OFFSET);
	}
	outb_p(value & 0xff, data->addr + DATA_REG_OFFSET);
	return 0;
}

static void nct6791_enable_io_mapping(struct nct6775_sio_data *sio_data)
{
	int val;

	val = sio_data->sio_inb(sio_data, NCT6791_REG_HM_IO_SPACE_LOCK_ENABLE);
	if (val & 0x10) {
		pr_info("Enabling hardware monitor logical device mappings.\n");
		sio_data->sio_outb(sio_data, NCT6791_REG_HM_IO_SPACE_LOCK_ENABLE,
			       val & ~0x10);
	}
}

static int nct6775_suspend(struct device *dev)
{
	int err;
	u16 tmp;
	struct nct6775_data *data = nct6775_update_device(dev);

	if (IS_ERR(data))
		return PTR_ERR(data);

	mutex_lock(&data->update_lock);
	err = nct6775_read_value(data, data->REG_VBAT, &tmp);
	if (err)
		goto out;
	data->vbat = tmp;
	if (data->kind == nct6775) {
		err = nct6775_read_value(data, NCT6775_REG_FANDIV1, &tmp);
		if (err)
			goto out;
		data->fandiv1 = tmp;

		err = nct6775_read_value(data, NCT6775_REG_FANDIV2, &tmp);
		if (err)
			goto out;
		data->fandiv2 = tmp;
	}
out:
	mutex_unlock(&data->update_lock);

	return err;
}

static int nct6775_resume(struct device *dev)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct nct6775_sio_data *sio_data = dev_get_platdata(dev);
	int i, j, err = 0;
	u8 reg;

	mutex_lock(&data->update_lock);
	data->bank = 0xff;		/* Force initial bank selection */

	err = sio_data->sio_enter(sio_data);
	if (err)
		goto abort;

	sio_data->sio_select(sio_data, NCT6775_LD_HWM);
	reg = sio_data->sio_inb(sio_data, SIO_REG_ENABLE);
	if (reg != data->sio_reg_enable)
		sio_data->sio_outb(sio_data, SIO_REG_ENABLE, data->sio_reg_enable);

	if (data->kind == nct6791 || data->kind == nct6792 ||
	    data->kind == nct6793 || data->kind == nct6795 ||
	    data->kind == nct6796 || data->kind == nct6797 ||
	    data->kind == nct6798 || data->kind == nct6799)
		nct6791_enable_io_mapping(sio_data);

	sio_data->sio_exit(sio_data);

	/* Restore limits */
	for (i = 0; i < data->in_num; i++) {
		if (!(data->have_in & BIT(i)))
			continue;

		err = nct6775_write_value(data, data->REG_IN_MINMAX[0][i], data->in[i][1]);
		if (err)
			goto abort;
		err = nct6775_write_value(data, data->REG_IN_MINMAX[1][i], data->in[i][2]);
		if (err)
			goto abort;
	}

	for (i = 0; i < ARRAY_SIZE(data->fan_min); i++) {
		if (!(data->has_fan_min & BIT(i)))
			continue;

		err = nct6775_write_value(data, data->REG_FAN_MIN[i], data->fan_min[i]);
		if (err)
			goto abort;
	}

	for (i = 0; i < NUM_TEMP; i++) {
		if (!(data->have_temp & BIT(i)))
			continue;

		for (j = 1; j < ARRAY_SIZE(data->reg_temp); j++)
			if (data->reg_temp[j][i]) {
				err = nct6775_write_temp(data, data->reg_temp[j][i],
							 data->temp[j][i]);
				if (err)
					goto abort;
			}
	}

	/* Restore other settings */
	err = nct6775_write_value(data, data->REG_VBAT, data->vbat);
	if (err)
		goto abort;
	if (data->kind == nct6775) {
		err = nct6775_write_value(data, NCT6775_REG_FANDIV1, data->fandiv1);
		if (err)
			goto abort;
		err = nct6775_write_value(data, NCT6775_REG_FANDIV2, data->fandiv2);
	}

abort:
	/* Force re-reading all values */
	data->valid = false;
	mutex_unlock(&data->update_lock);

	return err;
}

static DEFINE_SIMPLE_DEV_PM_OPS(nct6775_dev_pm_ops, nct6775_suspend, nct6775_resume);

static void
nct6775_check_fan_inputs(struct nct6775_data *data, struct nct6775_sio_data *sio_data)
{
	bool fan3pin = false, fan4pin = false, fan4min = false;
	bool fan5pin = false, fan6pin = false, fan7pin = false;
	bool pwm3pin = false, pwm4pin = false, pwm5pin = false;
	bool pwm6pin = false, pwm7pin = false;

	/* Store SIO_REG_ENABLE for use during resume */
	sio_data->sio_select(sio_data, NCT6775_LD_HWM);
	data->sio_reg_enable = sio_data->sio_inb(sio_data, SIO_REG_ENABLE);

	/* fan4 and fan5 share some pins with the GPIO and serial flash */
	if (data->kind == nct6775) {
		int cr2c = sio_data->sio_inb(sio_data, 0x2c);

		fan3pin = cr2c & BIT(6);
		pwm3pin = cr2c & BIT(7);

		/* On NCT6775, fan4 shares pins with the fdc interface */
		fan4pin = !(sio_data->sio_inb(sio_data, 0x2A) & 0x80);
	} else if (data->kind == nct6776) {
		bool gpok = sio_data->sio_inb(sio_data, 0x27) & 0x80;
		const char *board_vendor, *board_name;

		board_vendor = dmi_get_system_info(DMI_BOARD_VENDOR);
		board_name = dmi_get_system_info(DMI_BOARD_NAME);

		if (board_name && board_vendor &&
		    !strcmp(board_vendor, "ASRock")) {
			/*
			 * Auxiliary fan monitoring is not enabled on ASRock
			 * Z77 Pro4-M if booted in UEFI Ultra-FastBoot mode.
			 * Observed with BIOS version 2.00.
			 */
			if (!strcmp(board_name, "Z77 Pro4-M")) {
				if ((data->sio_reg_enable & 0xe0) != 0xe0) {
					data->sio_reg_enable |= 0xe0;
					sio_data->sio_outb(sio_data, SIO_REG_ENABLE,
						     data->sio_reg_enable);
				}
			}
		}

		if (data->sio_reg_enable & 0x80)
			fan3pin = gpok;
		else
			fan3pin = !(sio_data->sio_inb(sio_data, 0x24) & 0x40);

		if (data->sio_reg_enable & 0x40)
			fan4pin = gpok;
		else
			fan4pin = sio_data->sio_inb(sio_data, 0x1C) & 0x01;

		if (data->sio_reg_enable & 0x20)
			fan5pin = gpok;
		else
			fan5pin = sio_data->sio_inb(sio_data, 0x1C) & 0x02;

		fan4min = fan4pin;
		pwm3pin = fan3pin;
	} else if (data->kind == nct6106) {
		int cr24 = sio_data->sio_inb(sio_data, 0x24);

		fan3pin = !(cr24 & 0x80);
		pwm3pin = cr24 & 0x08;
	} else if (data->kind == nct6116) {
		int cr1a = sio_data->sio_inb(sio_data, 0x1a);
		int cr1b = sio_data->sio_inb(sio_data, 0x1b);
		int cr24 = sio_data->sio_inb(sio_data, 0x24);
		int cr2a = sio_data->sio_inb(sio_data, 0x2a);
		int cr2b = sio_data->sio_inb(sio_data, 0x2b);
		int cr2f = sio_data->sio_inb(sio_data, 0x2f);

		fan3pin = !(cr2b & 0x10);
		fan4pin = (cr2b & 0x80) ||			// pin 1(2)
			(!(cr2f & 0x10) && (cr1a & 0x04));	// pin 65(66)
		fan5pin = (cr2b & 0x80) ||			// pin 126(127)
			(!(cr1b & 0x03) && (cr2a & 0x02));	// pin 94(96)

		pwm3pin = fan3pin && (cr24 & 0x08);
		pwm4pin = fan4pin;
		pwm5pin = fan5pin;
	} else {
		/*
		 * NCT6779D, NCT6791D, NCT6792D, NCT6793D, NCT6795D, NCT6796D,
		 * NCT6797D, NCT6798D, NCT6799D
		 */
		int cr1a = sio_data->sio_inb(sio_data, 0x1a);
		int cr1b = sio_data->sio_inb(sio_data, 0x1b);
		int cr1c = sio_data->sio_inb(sio_data, 0x1c);
		int cr1d = sio_data->sio_inb(sio_data, 0x1d);
		int cr2a = sio_data->sio_inb(sio_data, 0x2a);
		int cr2b = sio_data->sio_inb(sio_data, 0x2b);
		int cr2d = sio_data->sio_inb(sio_data, 0x2d);
		int cr2f = sio_data->sio_inb(sio_data, 0x2f);
		bool vsb_ctl_en = cr2f & BIT(0);
		bool dsw_en = cr2f & BIT(3);
		bool ddr4_en = cr2f & BIT(4);
		bool as_seq1_en = cr2f & BIT(7);
		int cre0;
		int cre6;
		int creb;
		int cred;

		cre6 = sio_data->sio_inb(sio_data, 0xe6);

		sio_data->sio_select(sio_data, NCT6775_LD_12);
		cre0 = sio_data->sio_inb(sio_data, 0xe0);
		creb = sio_data->sio_inb(sio_data, 0xeb);
		cred = sio_data->sio_inb(sio_data, 0xed);

		fan3pin = !(cr1c & BIT(5));
		fan4pin = !(cr1c & BIT(6));
		fan5pin = !(cr1c & BIT(7));

		pwm3pin = !(cr1c & BIT(0));
		pwm4pin = !(cr1c & BIT(1));
		pwm5pin = !(cr1c & BIT(2));

		switch (data->kind) {
		case nct6791:
			fan6pin = cr2d & BIT(1);
			pwm6pin = cr2d & BIT(0);
			break;
		case nct6792:
			fan6pin = !dsw_en && (cr2d & BIT(1));
			pwm6pin = !dsw_en && (cr2d & BIT(0));
			break;
		case nct6793:
			fan5pin |= cr1b & BIT(5);
			fan5pin |= creb & BIT(5);

			fan6pin = !dsw_en && (cr2d & BIT(1));
			fan6pin |= creb & BIT(3);

			pwm5pin |= cr2d & BIT(7);
			pwm5pin |= (creb & BIT(4)) && !(cr2a & BIT(0));

			pwm6pin = !dsw_en && (cr2d & BIT(0));
			pwm6pin |= creb & BIT(2);
			break;
		case nct6795:
			fan5pin |= cr1b & BIT(5);
			fan5pin |= creb & BIT(5);

			fan6pin = (cr2a & BIT(4)) &&
					(!dsw_en || (cred & BIT(4)));
			fan6pin |= creb & BIT(3);

			pwm5pin |= cr2d & BIT(7);
			pwm5pin |= (creb & BIT(4)) && !(cr2a & BIT(0));

			pwm6pin = (cr2a & BIT(3)) && (cred & BIT(2));
			pwm6pin |= creb & BIT(2);
			break;
		case nct6796:
			fan5pin |= cr1b & BIT(5);
			fan5pin |= (cre0 & BIT(3)) && !(cr1b & BIT(0));
			fan5pin |= creb & BIT(5);

			fan6pin = (cr2a & BIT(4)) &&
					(!dsw_en || (cred & BIT(4)));
			fan6pin |= creb & BIT(3);

			fan7pin = !(cr2b & BIT(2));

			pwm5pin |= cr2d & BIT(7);
			pwm5pin |= (cre0 & BIT(4)) && !(cr1b & BIT(0));
			pwm5pin |= (creb & BIT(4)) && !(cr2a & BIT(0));

			pwm6pin = (cr2a & BIT(3)) && (cred & BIT(2));
			pwm6pin |= creb & BIT(2);

			pwm7pin = !(cr1d & (BIT(2) | BIT(3)));
			break;
		case nct6797:
			fan5pin |= !ddr4_en && (cr1b & BIT(5));
			fan5pin |= creb & BIT(5);

			fan6pin = cr2a & BIT(4);
			fan6pin |= creb & BIT(3);

			fan7pin = cr1a & BIT(1);

			pwm5pin |= (creb & BIT(4)) && !(cr2a & BIT(0));
			pwm5pin |= !ddr4_en && (cr2d & BIT(7));

			pwm6pin = creb & BIT(2);
			pwm6pin |= cred & BIT(2);

			pwm7pin = cr1d & BIT(4);
			break;
		case nct6798:
			fan6pin = !(cr1b & BIT(0)) && (cre0 & BIT(3));
			fan6pin |= cr2a & BIT(4);
			fan6pin |= creb & BIT(5);

			fan7pin = cr1b & BIT(5);
			fan7pin |= !(cr2b & BIT(2));
			fan7pin |= creb & BIT(3);

			pwm6pin = !(cr1b & BIT(0)) && (cre0 & BIT(4));
			pwm6pin |= !(cred & BIT(2)) && (cr2a & BIT(3));
			pwm6pin |= (creb & BIT(4)) && !(cr2a & BIT(0));

			pwm7pin = !(cr1d & (BIT(2) | BIT(3)));
			pwm7pin |= cr2d & BIT(7);
			pwm7pin |= creb & BIT(2);
			break;
		case nct6799:
			fan4pin = cr1c & BIT(6);
			fan5pin = cr1c & BIT(7);

			fan6pin = !(cr1b & BIT(0)) && (cre0 & BIT(3));
			fan6pin |= cre6 & BIT(5);
			fan6pin |= creb & BIT(5);
			fan6pin |= !as_seq1_en && (cr2a & BIT(4));

			fan7pin = cr1b & BIT(5);
			fan7pin |= !vsb_ctl_en && !(cr2b & BIT(2));
			fan7pin |= creb & BIT(3);

			pwm6pin = !(cr1b & BIT(0)) && (cre0 & BIT(4));
			pwm6pin |= !as_seq1_en && !(cred & BIT(2)) && (cr2a & BIT(3));
			pwm6pin |= (creb & BIT(4)) && !(cr2a & BIT(0));
			pwm6pin |= cre6 & BIT(3);

			pwm7pin = !vsb_ctl_en && !(cr1d & (BIT(2) | BIT(3)));
			pwm7pin |= creb & BIT(2);
			pwm7pin |= cr2d & BIT(7);

			break;
		default:	/* NCT6779D */
			break;
		}

		fan4min = fan4pin;
	}

	/* fan 1 and 2 (0x03) are always present */
	data->has_fan = 0x03 | (fan3pin << 2) | (fan4pin << 3) |
		(fan5pin << 4) | (fan6pin << 5) | (fan7pin << 6);
	data->has_fan_min = 0x03 | (fan3pin << 2) | (fan4min << 3) |
		(fan5pin << 4) | (fan6pin << 5) | (fan7pin << 6);
	data->has_pwm = 0x03 | (pwm3pin << 2) | (pwm4pin << 3) |
		(pwm5pin << 4) | (pwm6pin << 5) | (pwm7pin << 6);
}

static ssize_t
cpu0_vid_show(struct device *dev, struct device_attribute *attr, char *buf)
{
	struct nct6775_data *data = dev_get_drvdata(dev);

	return sprintf(buf, "%d\n", vid_from_reg(data->vid, data->vrm));
}

static DEVICE_ATTR_RO(cpu0_vid);

/* Case open detection */

static const u8 NCT6775_REG_CR_CASEOPEN_CLR[] = { 0xe6, 0xee };
static const u8 NCT6775_CR_CASEOPEN_CLR_MASK[] = { 0x20, 0x01 };

static ssize_t
clear_caseopen(struct device *dev, struct device_attribute *attr,
	       const char *buf, size_t count)
{
	struct nct6775_data *data = dev_get_drvdata(dev);
	struct nct6775_sio_data *sio_data = data->driver_data;
	int nr = to_sensor_dev_attr(attr)->index - INTRUSION_ALARM_BASE;
	unsigned long val;
	u8 reg;
	int ret;

	if (kstrtoul(buf, 10, &val) || val != 0)
		return -EINVAL;

	mutex_lock(&data->update_lock);

	/*
	 * Use CR registers to clear caseopen status.
	 * The CR registers are the same for all chips, and not all chips
	 * support clearing the caseopen status through "regular" registers.
	 */
	ret = sio_data->sio_enter(sio_data);
	if (ret) {
		count = ret;
		goto error;
	}

	sio_data->sio_select(sio_data, NCT6775_LD_ACPI);
	reg = sio_data->sio_inb(sio_data, NCT6775_REG_CR_CASEOPEN_CLR[nr]);
	reg |= NCT6775_CR_CASEOPEN_CLR_MASK[nr];
	sio_data->sio_outb(sio_data, NCT6775_REG_CR_CASEOPEN_CLR[nr], reg);
	reg &= ~NCT6775_CR_CASEOPEN_CLR_MASK[nr];
	sio_data->sio_outb(sio_data, NCT6775_REG_CR_CASEOPEN_CLR[nr], reg);
	sio_data->sio_exit(sio_data);

	data->valid = false;	/* Force cache refresh */
error:
	mutex_unlock(&data->update_lock);
	return count;
}

static SENSOR_DEVICE_ATTR(intrusion0_alarm, 0644, nct6775_show_alarm,
			  clear_caseopen, INTRUSION_ALARM_BASE);
static SENSOR_DEVICE_ATTR(intrusion1_alarm, 0644, nct6775_show_alarm,
			  clear_caseopen, INTRUSION_ALARM_BASE + 1);
static SENSOR_DEVICE_ATTR(intrusion0_beep, 0644, nct6775_show_beep,
			  nct6775_store_beep, INTRUSION_ALARM_BASE);
static SENSOR_DEVICE_ATTR(intrusion1_beep, 0644, nct6775_show_beep,
			  nct6775_store_beep, INTRUSION_ALARM_BASE + 1);
static SENSOR_DEVICE_ATTR(beep_enable, 0644, nct6775_show_beep,
			  nct6775_store_beep, BEEP_ENABLE_BASE);

static umode_t nct6775_other_is_visible(struct kobject *kobj,
					struct attribute *attr, int index)
{
	struct device *dev = kobj_to_dev(kobj);
	struct nct6775_data *data = dev_get_drvdata(dev);

	if (index == 0 && !data->have_vid)
		return 0;

	if (index == 1 || index == 2) {
		if (data->ALARM_BITS[INTRUSION_ALARM_BASE + index - 1] < 0)
			return 0;
	}

	if (index == 3 || index == 4) {
		if (data->BEEP_BITS[INTRUSION_ALARM_BASE + index - 3] < 0)
			return 0;
	}

	return nct6775_attr_mode(data, attr);
}

/*
 * nct6775_other_is_visible uses the index into the following array
 * to determine if attributes should be created or not.
 * Any change in order or content must be matched.
 */
static struct attribute *nct6775_attributes_other[] = {
	&dev_attr_cpu0_vid.attr,				/* 0 */
	&sensor_dev_attr_intrusion0_alarm.dev_attr.attr,	/* 1 */
	&sensor_dev_attr_intrusion1_alarm.dev_attr.attr,	/* 2 */
	&sensor_dev_attr_intrusion0_beep.dev_attr.attr,		/* 3 */
	&sensor_dev_attr_intrusion1_beep.dev_attr.attr,		/* 4 */
	&sensor_dev_attr_beep_enable.dev_attr.attr,		/* 5 */

	NULL
};

static const struct attribute_group nct6775_group_other = {
	.attrs = nct6775_attributes_other,
	.is_visible = nct6775_other_is_visible,
};

static int nct6775_platform_probe_init(struct nct6775_data *data)
{
	int err;
	u8 cr2a;
	struct nct6775_sio_data *sio_data = data->driver_data;

	err = sio_data->sio_enter(sio_data);
	if (err)
		return err;

	cr2a = sio_data->sio_inb(sio_data, 0x2a);
	switch (data->kind) {
	case nct6775:
		data->have_vid = (cr2a & 0x40);
		break;
	case nct6776:
		data->have_vid = (cr2a & 0x60) == 0x40;
		break;
	case nct6106:
	case nct6116:
	case nct6779:
	case nct6791:
	case nct6792:
	case nct6793:
	case nct6795:
	case nct6796:
	case nct6797:
	case nct6798:
	case nct6799:
		break;
	}

	/*
	 * Read VID value
	 * We can get the VID input values directly at logical device D 0xe3.
	 */
	if (data->have_vid) {
		sio_data->sio_select(sio_data, NCT6775_LD_VID);
		data->vid = sio_data->sio_inb(sio_data, 0xe3);
		data->vrm = vid_which_vrm();
	}

	if (fan_debounce) {
		u8 tmp;

		sio_data->sio_select(sio_data, NCT6775_LD_HWM);
		tmp = sio_data->sio_inb(sio_data,
				    NCT6775_REG_CR_FAN_DEBOUNCE);
		switch (data->kind) {
		case nct6106:
		case nct6116:
			tmp |= 0xe0;
			break;
		case nct6775:
			tmp |= 0x1e;
			break;
		case nct6776:
		case nct6779:
			tmp |= 0x3e;
			break;
		case nct6791:
		case nct6792:
		case nct6793:
		case nct6795:
		case nct6796:
		case nct6797:
		case nct6798:
		case nct6799:
			tmp |= 0x7e;
			break;
		}
		sio_data->sio_outb(sio_data, NCT6775_REG_CR_FAN_DEBOUNCE,
			     tmp);
		pr_info("Enabled fan debounce for chip %s\n", data->name);
	}

	nct6775_check_fan_inputs(data, sio_data);

	sio_data->sio_exit(sio_data);

	return nct6775_add_attr_group(data, &nct6775_group_other);
}

static const struct regmap_config nct6775_regmap_config = {
	.reg_bits = 16,
	.val_bits = 16,
	.reg_read = nct6775_reg_read,
	.reg_write = nct6775_reg_write,
};

static const struct regmap_config nct6775_wmi_regmap_config = {
	.reg_bits = 16,
	.val_bits = 16,
	.reg_read = nct6775_wmi_reg_read,
	.reg_write = nct6775_wmi_reg_write,
};

static int nct6775_platform_probe(struct platform_device *pdev)
{
	struct device *dev = &pdev->dev;
	struct nct6775_sio_data *sio_data = dev_get_platdata(dev);
	struct nct6775_data *data;
	struct resource *res;
	const struct regmap_config *regmapcfg;

	if (sio_data->access == access_direct) {
		res = platform_get_resource(pdev, IORESOURCE_IO, 0);
		if (!devm_request_region(&pdev->dev, res->start, IOREGION_LENGTH, DRVNAME))
			return -EBUSY;
	}

	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data)
		return -ENOMEM;

	data->kind = sio_data->kind;
	data->sioreg = sio_data->sioreg;

	if (sio_data->access == access_direct) {
		data->addr = res->start;
		regmapcfg = &nct6775_regmap_config;
	} else {
		regmapcfg = &nct6775_wmi_regmap_config;
	}

	platform_set_drvdata(pdev, data);

	data->driver_data = sio_data;
	data->driver_init = nct6775_platform_probe_init;

	return nct6775_probe(&pdev->dev, data, regmapcfg);
}

static struct platform_driver nct6775_driver = {
	.driver = {
		.name	= DRVNAME,
		.pm	= pm_sleep_ptr(&nct6775_dev_pm_ops),
	},
	.probe		= nct6775_platform_probe,
};

/* nct6775_find() looks for a '627 in the Super-I/O config space */
static int __init nct6775_find(int sioaddr, struct nct6775_sio_data *sio_data)
{
	u16 val;
	int err;
	int addr;

	sio_data->access = access_direct;
	sio_data->sioreg = sioaddr;

	err = sio_data->sio_enter(sio_data);
	if (err)
		return err;

	val = (sio_data->sio_inb(sio_data, SIO_REG_DEVID) << 8) |
		sio_data->sio_inb(sio_data, SIO_REG_DEVID + 1);
	if (force_id && val != 0xffff)
		val = force_id;

	switch (val & SIO_ID_MASK) {
	case SIO_NCT6106_ID:
		sio_data->kind = nct6106;
		break;
	case SIO_NCT6116_ID:
		sio_data->kind = nct6116;
		break;
	case SIO_NCT6775_ID:
		sio_data->kind = nct6775;
		break;
	case SIO_NCT6776_ID:
		sio_data->kind = nct6776;
		break;
	case SIO_NCT6779_ID:
		sio_data->kind = nct6779;
		break;
	case SIO_NCT6791_ID:
		sio_data->kind = nct6791;
		break;
	case SIO_NCT6792_ID:
		sio_data->kind = nct6792;
		break;
	case SIO_NCT6793_ID:
		sio_data->kind = nct6793;
		break;
	case SIO_NCT6795_ID:
		sio_data->kind = nct6795;
		break;
	case SIO_NCT6796_ID:
		sio_data->kind = nct6796;
		break;
	case SIO_NCT6797_ID:
		sio_data->kind = nct6797;
		break;
	case SIO_NCT6798_ID:
		sio_data->kind = nct6798;
		break;
	case SIO_NCT6799_ID:
		sio_data->kind = nct6799;
		break;
	default:
		if (val != 0xffff)
			pr_debug("unsupported chip ID: 0x%04x\n", val);
		sio_data->sio_exit(sio_data);
		return -ENODEV;
	}

	/* We have a known chip, find the HWM I/O address */
	sio_data->sio_select(sio_data, NCT6775_LD_HWM);
	val = (sio_data->sio_inb(sio_data, SIO_REG_ADDR) << 8)
	    | sio_data->sio_inb(sio_data, SIO_REG_ADDR + 1);
	addr = val & IOREGION_ALIGNMENT;
	if (addr == 0) {
		pr_err("Refusing to enable a Super-I/O device with a base I/O port 0\n");
		sio_data->sio_exit(sio_data);
		return -ENODEV;
	}

	/* Activate logical device if needed */
	val = sio_data->sio_inb(sio_data, SIO_REG_ENABLE);
	if (!(val & 0x01)) {
		pr_warn("Forcibly enabling Super-I/O. Sensor is probably unusable.\n");
		sio_data->sio_outb(sio_data, SIO_REG_ENABLE, val | 0x01);
	}

	if (sio_data->kind == nct6791 || sio_data->kind == nct6792 ||
	    sio_data->kind == nct6793 || sio_data->kind == nct6795 ||
	    sio_data->kind == nct6796 || sio_data->kind == nct6797 ||
	    sio_data->kind == nct6798 || sio_data->kind == nct6799)
		nct6791_enable_io_mapping(sio_data);

	sio_data->sio_exit(sio_data);
	pr_info("Found %s or compatible chip at %#x:%#x\n",
		nct6775_sio_names[sio_data->kind], sioaddr, addr);

	return addr;
}

/*
 * when Super-I/O functions move to a separate file, the Super-I/O
 * bus will manage the lifetime of the device and this module will only keep
 * track of the nct6775 driver. But since we use platform_device_alloc(), we
 * must keep track of the device
 */
static struct platform_device *pdev[2];

static const char * const asus_wmi_boards[] = {
	"B360M-BASALT",
	"B360M-D3H",
	"EX-B360M-V",
	"EX-B360M-V3",
	"EX-B360M-V5",
	"EX-B460M-V5",
	"EX-H410M-V3",
	"PRIME A520M-A",
	"PRIME A520M-A II",
	"PRIME A520M-E",
	"PRIME A520M-K",
	"PRIME B360-PLUS",
	"PRIME B360M-A",
	"PRIME B360M-C",
	"PRIME B360M-D",
	"PRIME B360M-K",
	"PRIME B460-PLUS",
	"PRIME B460I-PLUS",
	"PRIME B460M-A",
	"PRIME B460M-A R2.0",
	"PRIME B460M-K",
	"PRIME B550-PLUS",
	"PRIME B550-PLUS AC-HES",
	"PRIME B550M-A",
	"PRIME B550M-A (WI-FI)",
	"PRIME B550M-A AC",
	"PRIME B550M-A WIFI II",
	"PRIME B550M-K",
	"PRIME H310-PLUS",
	"PRIME H310I-PLUS",
	"PRIME H310M-A",
	"PRIME H310M-C",
	"PRIME H310M-D",
	"PRIME H310M-DASH",
	"PRIME H310M-E",
	"PRIME H310M-E/BR",
	"PRIME H310M-F",
	"PRIME H310M-K",
	"PRIME H310T",
	"PRIME H370-A",
	"PRIME H370-PLUS",
	"PRIME H370M-PLUS",
	"PRIME H410I-PLUS",
	"PRIME H410M-A",
	"PRIME H410M-D",
	"PRIME H410M-E",
	"PRIME H410M-F",
	"PRIME H410M-K",
	"PRIME H410M-K R2.0",
	"PRIME H410M-R",
	"PRIME H470-PLUS",
	"PRIME H470M-PLUS",
	"PRIME H510M-K R2.0",
	"PRIME Q370M-C",
	"PRIME X570-P",
	"PRIME X570-PRO",
	"PRIME Z390-A",
	"PRIME Z390-A/H10",
	"PRIME Z390-P",
	"PRIME Z390M-PLUS",
	"PRIME Z490-A",
	"PRIME Z490-P",
	"PRIME Z490-V",
	"PRIME Z490M-PLUS",
	"PRO B460M-C",
	"PRO H410M-C",
	"PRO H410T",
	"PRO Q470M-C",
	"Pro A520M-C",
	"Pro A520M-C II",
	"Pro B550M-C",
	"Pro WS X570-ACE",
	"ProArt B550-CREATOR",
	"ProArt X570-CREATOR WIFI",
	"ProArt Z490-CREATOR 10G",
	"ROG CROSSHAIR VIII DARK HERO",
	"ROG CROSSHAIR VIII EXTREME",
	"ROG CROSSHAIR VIII FORMULA",
	"ROG CROSSHAIR VIII HERO",
	"ROG CROSSHAIR VIII HERO (WI-FI)",
	"ROG CROSSHAIR VIII IMPACT",
	"ROG MAXIMUS XI APEX",
	"ROG MAXIMUS XI CODE",
	"ROG MAXIMUS XI EXTREME",
	"ROG MAXIMUS XI FORMULA",
	"ROG MAXIMUS XI GENE",
	"ROG MAXIMUS XI HERO",
	"ROG MAXIMUS XI HERO (WI-FI)",
	"ROG MAXIMUS XII APEX",
	"ROG MAXIMUS XII EXTREME",
	"ROG MAXIMUS XII FORMULA",
	"ROG MAXIMUS XII HERO (WI-FI)",
	"ROG STRIX B360-F GAMING",
	"ROG STRIX B360-G GAMING",
	"ROG STRIX B360-H GAMING",
	"ROG STRIX B360-H GAMING/OPTANE",
	"ROG STRIX B360-I GAMING",
	"ROG STRIX B460-F GAMING",
	"ROG STRIX B460-G GAMING",
	"ROG STRIX B460-H GAMING",
	"ROG STRIX B460-I GAMING",
	"ROG STRIX B550-A GAMING",
	"ROG STRIX B550-E GAMING",
	"ROG STRIX B550-F GAMING",
	"ROG STRIX B550-F GAMING (WI-FI)",
	"ROG STRIX B550-F GAMING WIFI II",
	"ROG STRIX B550-I GAMING",
	"ROG STRIX B550-XE GAMING WIFI",
	"ROG STRIX H370-F GAMING",
	"ROG STRIX H370-I GAMING",
	"ROG STRIX H470-I GAMING",
	"ROG STRIX X570-E GAMING",
	"ROG STRIX X570-E GAMING WIFI II",
	"ROG STRIX X570-F GAMING",
	"ROG STRIX X570-I GAMING",
	"ROG STRIX Z390-E GAMING",
	"ROG STRIX Z390-F GAMING",
	"ROG STRIX Z390-H GAMING",
	"ROG STRIX Z390-I GAMING",
	"ROG STRIX Z490-A GAMING",
	"ROG STRIX Z490-E GAMING",
	"ROG STRIX Z490-F GAMING",
	"ROG STRIX Z490-G GAMING",
	"ROG STRIX Z490-G GAMING (WI-FI)",
	"ROG STRIX Z490-H GAMING",
	"ROG STRIX Z490-I GAMING",
	"TUF B360-PLUS GAMING",
	"TUF B360-PRO GAMING",
	"TUF B360-PRO GAMING (WI-FI)",
	"TUF B360M-E GAMING",
	"TUF B360M-PLUS GAMING",
	"TUF B360M-PLUS GAMING S",
	"TUF B360M-PLUS GAMING/BR",
	"TUF GAMING A520M-PLUS",
	"TUF GAMING A520M-PLUS II",
	"TUF GAMING A520M-PLUS WIFI",
	"TUF GAMING B460-PLUS",
	"TUF GAMING B460-PRO (WI-FI)",
	"TUF GAMING B460M-PLUS",
	"TUF GAMING B460M-PLUS (WI-FI)",
	"TUF GAMING B460M-PRO",
	"TUF GAMING B550-PLUS",
	"TUF GAMING B550-PLUS (WI-FI)",
	"TUF GAMING B550-PLUS WIFI II",
	"TUF GAMING B550-PRO",
	"TUF GAMING B550M ZAKU (WI-FI)",
	"TUF GAMING B550M-E",
	"TUF GAMING B550M-E WIFI",
	"TUF GAMING B550M-PLUS",
	"TUF GAMING B550M-PLUS (WI-FI)",
	"TUF GAMING B550M-PLUS WIFI II",
	"TUF GAMING H470-PRO",
	"TUF GAMING H470-PRO (WI-FI)",
	"TUF GAMING X570-PLUS",
	"TUF GAMING X570-PLUS (WI-FI)",
	"TUF GAMING X570-PLUS_BR",
	"TUF GAMING X570-PRO (WI-FI)",
	"TUF GAMING X570-PRO WIFI II",
	"TUF GAMING Z490-PLUS",
	"TUF GAMING Z490-PLUS (WI-FI)",
	"TUF H310-PLUS GAMING",
	"TUF H310M-PLUS GAMING",
	"TUF H310M-PLUS GAMING/BR",
	"TUF H370-PRO GAMING",
	"TUF H370-PRO GAMING (WI-FI)",
	"TUF Z390-PLUS GAMING",
	"TUF Z390-PLUS GAMING (WI-FI)",
	"TUF Z390-PRO GAMING",
	"TUF Z390M-PRO GAMING",
	"TUF Z390M-PRO GAMING (WI-FI)",
	"WS Z390 PRO",
	"Z490-GUNDAM (WI-FI)",
};

static const char * const asus_msi_boards[] = {
	"B560M-P",
	"EX-B560M-V5",
	"EX-B660M-V5 D4",
	"EX-B660M-V5 PRO D4",
	"EX-B760M-V5 D4",
	"EX-H510M-V3",
	"EX-H610M-V3 D4",
	"PRIME A620M-A",
	"PRIME B560-PLUS",
	"PRIME B560-PLUS AC-HES",
	"PRIME B560M-A",
	"PRIME B560M-A AC",
	"PRIME B560M-K",
	"PRIME B650-PLUS",
	"PRIME B650M-A",
	"PRIME B650M-A AX",
	"PRIME B650M-A AX II",
	"PRIME B650M-A II",
	"PRIME B650M-A WIFI",
	"PRIME B650M-A WIFI II",
	"PRIME B660-PLUS D4",
	"PRIME B660M-A AC D4",
	"PRIME B660M-A D4",
	"PRIME B660M-A WIFI D4",
	"PRIME B760-PLUS",
	"PRIME B760-PLUS D4",
	"PRIME B760M-A",
	"PRIME B760M-A AX D4",
	"PRIME B760M-A D4",
	"PRIME B760M-A WIFI",
	"PRIME B760M-A WIFI D4",
	"PRIME B760M-AJ D4",
	"PRIME B760M-K D4",
	"PRIME H510M-A",
	"PRIME H510M-A WIFI",
	"PRIME H510M-D",
	"PRIME H510M-E",
	"PRIME H510M-F",
	"PRIME H510M-K",
	"PRIME H510M-R",
	"PRIME H510T2/CSM",
	"PRIME H570-PLUS",
	"PRIME H570M-PLUS",
	"PRIME H610I-PLUS D4",
	"PRIME H610M-A D4",
	"PRIME H610M-A WIFI D4",
	"PRIME H610M-D D4",
	"PRIME H610M-E D4",
	"PRIME H610M-F D4",
	"PRIME H610M-K D4",
	"PRIME H610M-R D4",
	"PRIME H670-PLUS D4",
	"PRIME H770-PLUS D4",
	"PRIME X670-P",
	"PRIME X670-P WIFI",
	"PRIME X670E-PRO WIFI",
	"PRIME Z590-A",
	"PRIME Z590-P",
	"PRIME Z590-P WIFI",
	"PRIME Z590-V",
	"PRIME Z590M-PLUS",
	"PRIME Z690-A",
	"PRIME Z690-P",
	"PRIME Z690-P D4",
	"PRIME Z690-P WIFI",
	"PRIME Z690-P WIFI D4",
	"PRIME Z690M-PLUS D4",
	"PRIME Z790-A WIFI",
	"PRIME Z790-P",
	"PRIME Z790-P D4",
	"PRIME Z790-P WIFI",
	"PRIME Z790-P WIFI D4",
	"PRIME Z790M-PLUS",
	"PRIME Z790M-PLUS D4",
	"Pro B560M-C",
	"Pro B560M-CT",
	"Pro B660M-C",
	"Pro B660M-C D4",
	"Pro B760M-C",
	"Pro B760M-CT",
	"Pro H510M-C",
	"Pro H510M-CT",
	"Pro H610M-C",
	"Pro H610M-C D4",
	"Pro H610M-CT D4",
	"Pro H610T D4",
	"Pro Q670M-C",
	"Pro WS W680-ACE",
	"Pro WS W680-ACE IPMI",
	"Pro WS W790-ACE",
	"Pro WS W790E-SAGE SE",
	"ProArt B650-CREATOR",
	"ProArt B660-CREATOR D4",
	"ProArt B760-CREATOR D4",
	"ProArt X670E-CREATOR WIFI",
	"ProArt Z690-CREATOR WIFI",
	"ProArt Z790-CREATOR WIFI",
	"ROG CROSSHAIR X670E EXTREME",
	"ROG CROSSHAIR X670E GENE",
	"ROG CROSSHAIR X670E HERO",
	"ROG MAXIMUS XIII APEX",
	"ROG MAXIMUS XIII EXTREME",
	"ROG MAXIMUS XIII EXTREME GLACIAL",
	"ROG MAXIMUS XIII HERO",
	"ROG MAXIMUS Z690 APEX",
	"ROG MAXIMUS Z690 EXTREME",
	"ROG MAXIMUS Z690 EXTREME GLACIAL",
	"ROG MAXIMUS Z690 FORMULA",
	"ROG MAXIMUS Z690 HERO",
	"ROG MAXIMUS Z690 HERO EVA",
	"ROG MAXIMUS Z790 APEX",
	"ROG MAXIMUS Z790 EXTREME",
	"ROG MAXIMUS Z790 HERO",
	"ROG STRIX B560-A GAMING WIFI",
	"ROG STRIX B560-E GAMING WIFI",
	"ROG STRIX B560-F GAMING WIFI",
	"ROG STRIX B560-G GAMING WIFI",
	"ROG STRIX B560-I GAMING WIFI",
	"ROG STRIX B650-A GAMING WIFI",
	"ROG STRIX B650E-E GAMING WIFI",
	"ROG STRIX B650E-F GAMING WIFI",
	"ROG STRIX B650E-I GAMING WIFI",
	"ROG STRIX B660-A GAMING WIFI",
	"ROG STRIX B660-A GAMING WIFI D4",
	"ROG STRIX B660-F GAMING WIFI",
	"ROG STRIX B660-G GAMING WIFI",
	"ROG STRIX B660-I GAMING WIFI",
	"ROG STRIX B760-A GAMING WIFI",
	"ROG STRIX B760-A GAMING WIFI D4",
	"ROG STRIX B760-F GAMING WIFI",
	"ROG STRIX B760-G GAMING WIFI",
	"ROG STRIX B760-G GAMING WIFI D4",
	"ROG STRIX B760-I GAMING WIFI",
	"ROG STRIX X670E-A GAMING WIFI",
	"ROG STRIX X670E-E GAMING WIFI",
	"ROG STRIX X670E-F GAMING WIFI",
	"ROG STRIX X670E-I GAMING WIFI",
	"ROG STRIX Z590-A GAMING WIFI",
	"ROG STRIX Z590-A GAMING WIFI II",
	"ROG STRIX Z590-E GAMING WIFI",
	"ROG STRIX Z590-F GAMING WIFI",
	"ROG STRIX Z590-I GAMING WIFI",
	"ROG STRIX Z690-A GAMING WIFI",
	"ROG STRIX Z690-A GAMING WIFI D4",
	"ROG STRIX Z690-E GAMING WIFI",
	"ROG STRIX Z690-F GAMING WIFI",
	"ROG STRIX Z690-G GAMING WIFI",
	"ROG STRIX Z690-I GAMING WIFI",
	"ROG STRIX Z790-A GAMING WIFI",
	"ROG STRIX Z790-A GAMING WIFI D4",
	"ROG STRIX Z790-E GAMING WIFI",
	"ROG STRIX Z790-F GAMING WIFI",
	"ROG STRIX Z790-H GAMING WIFI",
	"ROG STRIX Z790-I GAMING WIFI",
	"TUF GAMING A620M-PLUS",
	"TUF GAMING A620M-PLUS WIFI",
	"TUF GAMING B560-PLUS WIFI",
	"TUF GAMING B560M-E",
	"TUF GAMING B560M-PLUS",
	"TUF GAMING B560M-PLUS WIFI",
	"TUF GAMING B650-PLUS",
	"TUF GAMING B650-PLUS WIFI",
	"TUF GAMING B650M-PLUS",
	"TUF GAMING B650M-PLUS WIFI",
	"TUF GAMING B660-PLUS WIFI D4",
	"TUF GAMING B660M-E D4",
	"TUF GAMING B660M-PLUS D4",
	"TUF GAMING B660M-PLUS WIFI",
	"TUF GAMING B660M-PLUS WIFI D4",
	"TUF GAMING B760-PLUS WIFI",
	"TUF GAMING B760-PLUS WIFI D4",
	"TUF GAMING B760M-BTF WIFI D4",
	"TUF GAMING B760M-E D4",
	"TUF GAMING B760M-PLUS",
	"TUF GAMING B760M-PLUS D4",
	"TUF GAMING B760M-PLUS WIFI",
	"TUF GAMING B760M-PLUS WIFI D4",
	"TUF GAMING H570-PRO",
	"TUF GAMING H570-PRO WIFI",
	"TUF GAMING H670-PRO WIFI D4",
	"TUF GAMING H770-PRO WIFI",
	"TUF GAMING X670E-PLUS",
	"TUF GAMING X670E-PLUS WIFI",
	"TUF GAMING Z590-PLUS",
	"TUF GAMING Z590-PLUS WIFI",
	"TUF GAMING Z690-PLUS",
	"TUF GAMING Z690-PLUS D4",
	"TUF GAMING Z690-PLUS WIFI",
	"TUF GAMING Z690-PLUS WIFI D4",
	"TUF GAMING Z790-PLUS D4",
	"TUF GAMING Z790-PLUS WIFI",
	"TUF GAMING Z790-PLUS WIFI D4",
	"Z590 WIFI GUNDAM EDITION",
};

#if IS_ENABLED(CONFIG_ACPI)
/*
 * Callback for acpi_bus_for_each_dev() to find the right device
 * by _UID and _HID and return 1 to stop iteration.
 */
static int nct6775_asuswmi_device_match(struct device *dev, void *data)
{
	struct acpi_device *adev = to_acpi_device(dev);
	const char *uid = acpi_device_uid(adev);
	const char *hid = acpi_device_hid(adev);

	if (hid && !strcmp(hid, ASUSWMI_DEVICE_HID) && uid && !strcmp(uid, data)) {
		asus_acpi_dev = adev;
		return 1;
	}

	return 0;
}
#endif

static enum sensor_access nct6775_determine_access(const char *device_uid)
{
#if IS_ENABLED(CONFIG_ACPI)
	u8 tmp;

	acpi_bus_for_each_dev(nct6775_asuswmi_device_match, (void *)device_uid);
	if (!asus_acpi_dev)
		return access_direct;

	/* if reading chip id via ACPI succeeds, use WMI "WMBD" method for access */
	if (!nct6775_asuswmi_read(0, NCT6775_PORT_CHIPID, &tmp) && tmp) {
		pr_debug("Using Asus WMBD method of %s to access %#x chip.\n", device_uid, tmp);
		return access_asuswmi;
	}
#endif

	return access_direct;
}

static int __init sensors_nct6775_platform_init(void)
{
	int i, err;
	bool found = false;
	int address;
	struct resource res;
	struct nct6775_sio_data sio_data;
	int sioaddr[2] = { 0x2e, 0x4e };
	enum sensor_access access = access_direct;
	const char *board_vendor, *board_name;

	err = platform_driver_register(&nct6775_driver);
	if (err)
		return err;

	board_vendor = dmi_get_system_info(DMI_BOARD_VENDOR);
	board_name = dmi_get_system_info(DMI_BOARD_NAME);

	if (board_name && board_vendor &&
	    !strcmp(board_vendor, "ASUSTeK COMPUTER INC.")) {
		err = match_string(asus_wmi_boards, ARRAY_SIZE(asus_wmi_boards),
				   board_name);
		if (err >= 0)
			access = nct6775_determine_access(ASUSWMI_DEVICE_UID);

		err = match_string(asus_msi_boards, ARRAY_SIZE(asus_msi_boards),
				   board_name);
		if (err >= 0)
			access = nct6775_determine_access(ASUSMSI_DEVICE_UID);
	}

	/*
	 * initialize sio_data->kind and sio_data->sioreg.
	 *
	 * when Super-I/O functions move to a separate file, the Super-I/O
	 * driver will probe 0x2e and 0x4e and auto-detect the presence of a
	 * nct6775 hardware monitor, and call probe()
	 */
	for (i = 0; i < ARRAY_SIZE(pdev); i++) {
		sio_data.sio_outb = superio_outb;
		sio_data.sio_inb = superio_inb;
		sio_data.sio_select = superio_select;
		sio_data.sio_enter = superio_enter;
		sio_data.sio_exit = superio_exit;

		address = nct6775_find(sioaddr[i], &sio_data);
		if (address <= 0)
			continue;

		found = true;

		sio_data.access = access;

		if (access == access_asuswmi) {
			sio_data.sio_outb = superio_wmi_outb;
			sio_data.sio_inb = superio_wmi_inb;
			sio_data.sio_select = superio_wmi_select;
			sio_data.sio_enter = superio_wmi_enter;
			sio_data.sio_exit = superio_wmi_exit;
		}

		pdev[i] = platform_device_alloc(DRVNAME, address);
		if (!pdev[i]) {
			err = -ENOMEM;
			goto exit_device_unregister;
		}

		err = platform_device_add_data(pdev[i], &sio_data,
					       sizeof(struct nct6775_sio_data));
		if (err)
			goto exit_device_put;

		if (sio_data.access == access_direct) {
			memset(&res, 0, sizeof(res));
			res.name = DRVNAME;
			res.start = address + IOREGION_OFFSET;
			res.end = address + IOREGION_OFFSET + IOREGION_LENGTH - 1;
			res.flags = IORESOURCE_IO;

			err = acpi_check_resource_conflict(&res);
			if (err) {
				platform_device_put(pdev[i]);
				pdev[i] = NULL;
				continue;
			}

			err = platform_device_add_resources(pdev[i], &res, 1);
			if (err)
				goto exit_device_put;
		}

		/* platform_device_add calls probe() */
		err = platform_device_add(pdev[i]);
		if (err)
			goto exit_device_put;
	}
	if (!found) {
		err = -ENODEV;
		goto exit_unregister;
	}

	return 0;

exit_device_put:
	platform_device_put(pdev[i]);
exit_device_unregister:
	while (i--)
		platform_device_unregister(pdev[i]);
exit_unregister:
	platform_driver_unregister(&nct6775_driver);
	return err;
}

static void __exit sensors_nct6775_platform_exit(void)
{
	int i;

	for (i = 0; i < ARRAY_SIZE(pdev); i++)
		platform_device_unregister(pdev[i]);
	platform_driver_unregister(&nct6775_driver);
}

MODULE_AUTHOR("Guenter Roeck <linux@roeck-us.net>");
MODULE_DESCRIPTION("Platform driver for NCT6775F and compatible chips");
MODULE_LICENSE("GPL");
MODULE_IMPORT_NS(HWMON_NCT6775);

module_init(sensors_nct6775_platform_init);
module_exit(sensors_nct6775_platform_exit);
