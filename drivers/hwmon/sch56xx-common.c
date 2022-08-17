// SPDX-License-Identifier: GPL-2.0-or-later
/***************************************************************************
 *   Copyright (C) 2010-2012 Hans de Goede <hdegoede@redhat.com>           *
 *                                                                         *
 ***************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/dmi.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/watchdog.h>
#include <linux/uaccess.h>
#include <linux/slab.h>
#include "sch56xx-common.h"

static bool ignore_dmi;
module_param(ignore_dmi, bool, 0);
MODULE_PARM_DESC(ignore_dmi, "Omit DMI check for supported devices (default=0)");

static bool nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, bool, 0);
MODULE_PARM_DESC(nowayout, "Watchdog cannot be stopped once started (default="
	__MODULE_STRING(WATCHDOG_NOWAYOUT) ")");

#define SIO_SCH56XX_LD_EM	0x0C	/* Embedded uController Logical Dev */
#define SIO_UNLOCK_KEY		0x55	/* Key to enable Super-I/O */
#define SIO_LOCK_KEY		0xAA	/* Key to disable Super-I/O */

#define SIO_REG_LDSEL		0x07	/* Logical device select */
#define SIO_REG_DEVID		0x20	/* Device ID */
#define SIO_REG_ENABLE		0x30	/* Logical device enable */
#define SIO_REG_ADDR		0x66	/* Logical device address (2 bytes) */

#define SIO_SCH5627_ID		0xC6	/* Chipset ID */
#define SIO_SCH5636_ID		0xC7	/* Chipset ID */

#define REGION_LENGTH		10

#define SCH56XX_CMD_READ	0x02
#define SCH56XX_CMD_WRITE	0x03

/* Watchdog registers */
#define SCH56XX_REG_WDOG_PRESET		0x58B
#define SCH56XX_REG_WDOG_CONTROL	0x58C
#define SCH56XX_WDOG_TIME_BASE_SEC	0x01
#define SCH56XX_REG_WDOG_OUTPUT_ENABLE	0x58E
#define SCH56XX_WDOG_OUTPUT_ENABLE	0x02

struct sch56xx_watchdog_data {
	u16 addr;
	struct mutex *io_lock;
	struct watchdog_info wdinfo;
	struct watchdog_device wddev;
	u8 watchdog_preset;
	u8 watchdog_control;
	u8 watchdog_output_enable;
};

static struct platform_device *sch56xx_pdev;

/* Super I/O functions */
static inline int superio_inb(int base, int reg)
{
	outb(reg, base);
	return inb(base + 1);
}

static inline int superio_enter(int base)
{
	/* Don't step on other drivers' I/O space by accident */
	if (!request_muxed_region(base, 2, "sch56xx")) {
		pr_err("I/O address 0x%04x already in use\n", base);
		return -EBUSY;
	}

	outb(SIO_UNLOCK_KEY, base);

	return 0;
}

static inline void superio_select(int base, int ld)
{
	outb(SIO_REG_LDSEL, base);
	outb(ld, base + 1);
}

static inline void superio_exit(int base)
{
	outb(SIO_LOCK_KEY, base);
	release_region(base, 2);
}

static int sch56xx_send_cmd(u16 addr, u8 cmd, u16 reg, u8 v)
{
	u8 val;
	int i;
	/*
	 * According to SMSC for the commands we use the maximum time for
	 * the EM to respond is 15 ms, but testing shows in practice it
	 * responds within 15-32 reads, so we first busy poll, and if
	 * that fails sleep a bit and try again until we are way past
	 * the 15 ms maximum response time.
	 */
	const int max_busy_polls = 64;
	const int max_lazy_polls = 32;

	/* (Optional) Write-Clear the EC to Host Mailbox Register */
	val = inb(addr + 1);
	outb(val, addr + 1);

	/* Set Mailbox Address Pointer to first location in Region 1 */
	outb(0x00, addr + 2);
	outb(0x80, addr + 3);

	/* Write Request Packet Header */
	outb(cmd, addr + 4); /* VREG Access Type read:0x02 write:0x03 */
	outb(0x01, addr + 5); /* # of Entries: 1 Byte (8-bit) */
	outb(0x04, addr + 2); /* Mailbox AP to first data entry loc. */

	/* Write Value field */
	if (cmd == SCH56XX_CMD_WRITE)
		outb(v, addr + 4);

	/* Write Address field */
	outb(reg & 0xff, addr + 6);
	outb(reg >> 8, addr + 7);

	/* Execute the Random Access Command */
	outb(0x01, addr); /* Write 01h to the Host-to-EC register */

	/* EM Interface Polling "Algorithm" */
	for (i = 0; i < max_busy_polls + max_lazy_polls; i++) {
		if (i >= max_busy_polls)
			usleep_range(1000, 2000);
		/* Read Interrupt source Register */
		val = inb(addr + 8);
		/* Write Clear the interrupt source bits */
		if (val)
			outb(val, addr + 8);
		/* Command Completed ? */
		if (val & 0x01)
			break;
	}
	if (i == max_busy_polls + max_lazy_polls) {
		pr_err("Max retries exceeded reading virtual register 0x%04hx (%d)\n",
		       reg, 1);
		return -EIO;
	}

	/*
	 * According to SMSC we may need to retry this, but sofar I've always
	 * seen this succeed in 1 try.
	 */
	for (i = 0; i < max_busy_polls; i++) {
		/* Read EC-to-Host Register */
		val = inb(addr + 1);
		/* Command Completed ? */
		if (val == 0x01)
			break;

		if (i == 0)
			pr_warn("EC reports: 0x%02x reading virtual register 0x%04hx\n",
				(unsigned int)val, reg);
	}
	if (i == max_busy_polls) {
		pr_err("Max retries exceeded reading virtual register 0x%04hx (%d)\n",
		       reg, 2);
		return -EIO;
	}

	/*
	 * According to the SMSC app note we should now do:
	 *
	 * Set Mailbox Address Pointer to first location in Region 1 *
	 * outb(0x00, addr + 2);
	 * outb(0x80, addr + 3);
	 *
	 * But if we do that things don't work, so let's not.
	 */

	/* Read Value field */
	if (cmd == SCH56XX_CMD_READ)
		return inb(addr + 4);

	return 0;
}

int sch56xx_read_virtual_reg(u16 addr, u16 reg)
{
	return sch56xx_send_cmd(addr, SCH56XX_CMD_READ, reg, 0);
}
EXPORT_SYMBOL(sch56xx_read_virtual_reg);

int sch56xx_write_virtual_reg(u16 addr, u16 reg, u8 val)
{
	return sch56xx_send_cmd(addr, SCH56XX_CMD_WRITE, reg, val);
}
EXPORT_SYMBOL(sch56xx_write_virtual_reg);

int sch56xx_read_virtual_reg16(u16 addr, u16 reg)
{
	int lsb, msb;

	/* Read LSB first, this will cause the matching MSB to be latched */
	lsb = sch56xx_read_virtual_reg(addr, reg);
	if (lsb < 0)
		return lsb;

	msb = sch56xx_read_virtual_reg(addr, reg + 1);
	if (msb < 0)
		return msb;

	return lsb | (msb << 8);
}
EXPORT_SYMBOL(sch56xx_read_virtual_reg16);

int sch56xx_read_virtual_reg12(u16 addr, u16 msb_reg, u16 lsn_reg,
			       int high_nibble)
{
	int msb, lsn;

	/* Read MSB first, this will cause the matching LSN to be latched */
	msb = sch56xx_read_virtual_reg(addr, msb_reg);
	if (msb < 0)
		return msb;

	lsn = sch56xx_read_virtual_reg(addr, lsn_reg);
	if (lsn < 0)
		return lsn;

	if (high_nibble)
		return (msb << 4) | (lsn >> 4);
	else
		return (msb << 4) | (lsn & 0x0f);
}
EXPORT_SYMBOL(sch56xx_read_virtual_reg12);

/*
 * Watchdog routines
 */

static int watchdog_set_timeout(struct watchdog_device *wddev,
				unsigned int timeout)
{
	struct sch56xx_watchdog_data *data = watchdog_get_drvdata(wddev);
	unsigned int resolution;
	u8 control;
	int ret;

	/* 1 second or 60 second resolution? */
	if (timeout <= 255)
		resolution = 1;
	else
		resolution = 60;

	if (timeout < resolution || timeout > (resolution * 255))
		return -EINVAL;

	if (resolution == 1)
		control = data->watchdog_control | SCH56XX_WDOG_TIME_BASE_SEC;
	else
		control = data->watchdog_control & ~SCH56XX_WDOG_TIME_BASE_SEC;

	if (data->watchdog_control != control) {
		mutex_lock(data->io_lock);
		ret = sch56xx_write_virtual_reg(data->addr,
						SCH56XX_REG_WDOG_CONTROL,
						control);
		mutex_unlock(data->io_lock);
		if (ret)
			return ret;

		data->watchdog_control = control;
	}

	/*
	 * Remember new timeout value, but do not write as that (re)starts
	 * the watchdog countdown.
	 */
	data->watchdog_preset = DIV_ROUND_UP(timeout, resolution);
	wddev->timeout = data->watchdog_preset * resolution;

	return 0;
}

static int watchdog_start(struct watchdog_device *wddev)
{
	struct sch56xx_watchdog_data *data = watchdog_get_drvdata(wddev);
	int ret;
	u8 val;

	/*
	 * The sch56xx's watchdog cannot really be started / stopped
	 * it is always running, but we can avoid the timer expiring
	 * from causing a system reset by clearing the output enable bit.
	 *
	 * The sch56xx's watchdog will set the watchdog event bit, bit 0
	 * of the second interrupt source register (at base-address + 9),
	 * when the timer expires.
	 *
	 * This will only cause a system reset if the 0-1 flank happens when
	 * output enable is true. Setting output enable after the flank will
	 * not cause a reset, nor will the timer expiring a second time.
	 * This means we must clear the watchdog event bit in case it is set.
	 *
	 * The timer may still be running (after a recent watchdog_stop) and
	 * mere milliseconds away from expiring, so the timer must be reset
	 * first!
	 */

	mutex_lock(data->io_lock);

	/* 1. Reset the watchdog countdown counter */
	ret = sch56xx_write_virtual_reg(data->addr, SCH56XX_REG_WDOG_PRESET,
					data->watchdog_preset);
	if (ret)
		goto leave;

	/* 2. Enable output */
	val = data->watchdog_output_enable | SCH56XX_WDOG_OUTPUT_ENABLE;
	ret = sch56xx_write_virtual_reg(data->addr,
					SCH56XX_REG_WDOG_OUTPUT_ENABLE, val);
	if (ret)
		goto leave;

	data->watchdog_output_enable = val;

	/* 3. Clear the watchdog event bit if set */
	val = inb(data->addr + 9);
	if (val & 0x01)
		outb(0x01, data->addr + 9);

leave:
	mutex_unlock(data->io_lock);
	return ret;
}

static int watchdog_trigger(struct watchdog_device *wddev)
{
	struct sch56xx_watchdog_data *data = watchdog_get_drvdata(wddev);
	int ret;

	/* Reset the watchdog countdown counter */
	mutex_lock(data->io_lock);
	ret = sch56xx_write_virtual_reg(data->addr, SCH56XX_REG_WDOG_PRESET,
					data->watchdog_preset);
	mutex_unlock(data->io_lock);

	return ret;
}

static int watchdog_stop(struct watchdog_device *wddev)
{
	struct sch56xx_watchdog_data *data = watchdog_get_drvdata(wddev);
	int ret = 0;
	u8 val;

	val = data->watchdog_output_enable & ~SCH56XX_WDOG_OUTPUT_ENABLE;
	mutex_lock(data->io_lock);
	ret = sch56xx_write_virtual_reg(data->addr,
					SCH56XX_REG_WDOG_OUTPUT_ENABLE, val);
	mutex_unlock(data->io_lock);
	if (ret)
		return ret;

	data->watchdog_output_enable = val;
	return 0;
}

static const struct watchdog_ops watchdog_ops = {
	.owner		= THIS_MODULE,
	.start		= watchdog_start,
	.stop		= watchdog_stop,
	.ping		= watchdog_trigger,
	.set_timeout	= watchdog_set_timeout,
};

void sch56xx_watchdog_register(struct device *parent, u16 addr, u32 revision,
			       struct mutex *io_lock, int check_enabled)
{
	struct sch56xx_watchdog_data *data;
	int err, control, output_enable;

	/* Cache the watchdog registers */
	mutex_lock(io_lock);
	control =
		sch56xx_read_virtual_reg(addr, SCH56XX_REG_WDOG_CONTROL);
	output_enable =
		sch56xx_read_virtual_reg(addr, SCH56XX_REG_WDOG_OUTPUT_ENABLE);
	mutex_unlock(io_lock);

	if (control < 0)
		return;
	if (output_enable < 0)
		return;
	if (check_enabled && !(output_enable & SCH56XX_WDOG_OUTPUT_ENABLE)) {
		pr_warn("Watchdog not enabled by BIOS, not registering\n");
		return;
	}

	data = devm_kzalloc(parent, sizeof(struct sch56xx_watchdog_data), GFP_KERNEL);
	if (!data)
		return;

	data->addr = addr;
	data->io_lock = io_lock;

	strscpy(data->wdinfo.identity, "sch56xx watchdog", sizeof(data->wdinfo.identity));
	data->wdinfo.firmware_version = revision;
	data->wdinfo.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT;
	if (!nowayout)
		data->wdinfo.options |= WDIOF_MAGICCLOSE;

	data->wddev.info = &data->wdinfo;
	data->wddev.ops = &watchdog_ops;
	data->wddev.parent = parent;
	data->wddev.timeout = 60;
	data->wddev.min_timeout = 1;
	data->wddev.max_timeout = 255 * 60;
	watchdog_set_nowayout(&data->wddev, nowayout);
	if (output_enable & SCH56XX_WDOG_OUTPUT_ENABLE)
		set_bit(WDOG_HW_RUNNING, &data->wddev.status);

	/* Since the watchdog uses a downcounter there is no register to read
	   the BIOS set timeout from (if any was set at all) ->
	   Choose a preset which will give us a 1 minute timeout */
	if (control & SCH56XX_WDOG_TIME_BASE_SEC)
		data->watchdog_preset = 60; /* seconds */
	else
		data->watchdog_preset = 1; /* minute */

	data->watchdog_control = control;
	data->watchdog_output_enable = output_enable;

	watchdog_set_drvdata(&data->wddev, data);
	err = devm_watchdog_register_device(parent, &data->wddev);
	if (err) {
		pr_err("Registering watchdog chardev: %d\n", err);
		devm_kfree(parent, data);
	}
}
EXPORT_SYMBOL(sch56xx_watchdog_register);

/*
 * platform dev find, add and remove functions
 */

static int __init sch56xx_find(int sioaddr, const char **name)
{
	u8 devid;
	unsigned short address;
	int err;

	err = superio_enter(sioaddr);
	if (err)
		return err;

	devid = superio_inb(sioaddr, SIO_REG_DEVID);
	switch (devid) {
	case SIO_SCH5627_ID:
		*name = "sch5627";
		break;
	case SIO_SCH5636_ID:
		*name = "sch5636";
		break;
	default:
		pr_debug("Unsupported device id: 0x%02x\n",
			 (unsigned int)devid);
		err = -ENODEV;
		goto exit;
	}

	superio_select(sioaddr, SIO_SCH56XX_LD_EM);

	if (!(superio_inb(sioaddr, SIO_REG_ENABLE) & 0x01)) {
		pr_warn("Device not activated\n");
		err = -ENODEV;
		goto exit;
	}

	/*
	 * Warning the order of the low / high byte is the other way around
	 * as on most other superio devices!!
	 */
	address = superio_inb(sioaddr, SIO_REG_ADDR) |
		   superio_inb(sioaddr, SIO_REG_ADDR + 1) << 8;
	if (address == 0) {
		pr_warn("Base address not set\n");
		err = -ENODEV;
		goto exit;
	}
	err = address;

exit:
	superio_exit(sioaddr);
	return err;
}

static int __init sch56xx_device_add(int address, const char *name)
{
	struct resource res = {
		.start	= address,
		.end	= address + REGION_LENGTH - 1,
		.name	= name,
		.flags	= IORESOURCE_IO,
	};
	int err;

	err = acpi_check_resource_conflict(&res);
	if (err)
		return err;

	sch56xx_pdev = platform_device_register_simple(name, -1, &res, 1);

	return PTR_ERR_OR_ZERO(sch56xx_pdev);
}

static const struct dmi_system_id sch56xx_dmi_override_table[] __initconst = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "CELSIUS W380"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ESPRIMO P710"),
		},
	},
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
			DMI_MATCH(DMI_PRODUCT_NAME, "ESPRIMO E9900"),
		},
	},
	{ }
};

/* For autoloading only */
static const struct dmi_system_id sch56xx_dmi_table[] __initconst = {
	{
		.matches = {
			DMI_MATCH(DMI_SYS_VENDOR, "FUJITSU"),
		},
	},
	{ }
};
MODULE_DEVICE_TABLE(dmi, sch56xx_dmi_table);

static int __init sch56xx_init(void)
{
	const char *name = NULL;
	int address;

	if (!ignore_dmi) {
		if (!dmi_check_system(sch56xx_dmi_table))
			return -ENODEV;

		if (!dmi_check_system(sch56xx_dmi_override_table)) {
			/*
			 * Some machines like the Esprimo P720 and Esprimo C700 have
			 * onboard devices named " Antiope"/" Theseus" instead of
			 * "Antiope"/"Theseus", so we need to check for both.
			 */
			if (!dmi_find_device(DMI_DEV_TYPE_OTHER, "Antiope", NULL) &&
			    !dmi_find_device(DMI_DEV_TYPE_OTHER, " Antiope", NULL) &&
			    !dmi_find_device(DMI_DEV_TYPE_OTHER, "Theseus", NULL) &&
			    !dmi_find_device(DMI_DEV_TYPE_OTHER, " Theseus", NULL))
				return -ENODEV;
		}
	}

	/*
	 * Some devices like the Esprimo C700 have both onboard devices,
	 * so we still have to check manually
	 */
	address = sch56xx_find(0x4e, &name);
	if (address < 0)
		address = sch56xx_find(0x2e, &name);
	if (address < 0)
		return address;

	return sch56xx_device_add(address, name);
}

static void __exit sch56xx_exit(void)
{
	platform_device_unregister(sch56xx_pdev);
}

MODULE_DESCRIPTION("SMSC SCH56xx Hardware Monitoring Common Code");
MODULE_AUTHOR("Hans de Goede <hdegoede@redhat.com>");
MODULE_LICENSE("GPL");

module_init(sch56xx_init);
module_exit(sch56xx_exit);
