/***************************************************************************
 *   Copyright (C) 2010-2012 Hans de Goede <hdegoede@redhat.com>           *
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 *   This program is distributed in the hope that it will be useful,       *
 *   but WITHOUT ANY WARRANTY; without even the implied warranty of        *
 *   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the         *
 *   GNU General Public License for more details.                          *
 *                                                                         *
 *   You should have received a copy of the GNU General Public License     *
 *   along with this program; if not, write to the                         *
 *   Free Software Foundation, Inc.,                                       *
 *   59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.             *
 ***************************************************************************/

#define pr_fmt(fmt) KBUILD_MODNAME ": " fmt

#include <linux/module.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/err.h>
#include <linux/io.h>
#include <linux/acpi.h>
#include <linux/delay.h>
#include <linux/fs.h>
#include <linux/watchdog.h>
#include <linux/miscdevice.h>
#include <linux/uaccess.h>
#include <linux/kref.h>
#include <linux/slab.h>
#include "sch56xx-common.h"

/* Insmod parameters */
static int nowayout = WATCHDOG_NOWAYOUT;
module_param(nowayout, int, 0);
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
	u32 revision;
	struct mutex *io_lock;
	struct mutex watchdog_lock;
	struct list_head list; /* member of the watchdog_data_list */
	struct kref kref;
	struct miscdevice watchdog_miscdev;
	unsigned long watchdog_is_open;
	char watchdog_name[10]; /* must be unique to avoid sysfs conflict */
	char watchdog_expect_close;
	u8 watchdog_preset;
	u8 watchdog_control;
	u8 watchdog_output_enable;
};

static struct platform_device *sch56xx_pdev;

/*
 * Somewhat ugly :( global data pointer list with all sch56xx devices, so that
 * we can find our device data as when using misc_register there is no other
 * method to get to ones device data from the open fop.
 */
static LIST_HEAD(watchdog_data_list);
/* Note this lock not only protect list access, but also data.kref access */
static DEFINE_MUTEX(watchdog_data_mutex);

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
			msleep(1);
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
		pr_err("Max retries exceeded reading virtual "
		       "register 0x%04hx (%d)\n", reg, 1);
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
			pr_warn("EC reports: 0x%02x reading virtual register "
				"0x%04hx\n", (unsigned int)val, reg);
	}
	if (i == max_busy_polls) {
		pr_err("Max retries exceeded reading virtual "
		       "register 0x%04hx (%d)\n", reg, 2);
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

/*
 * Release our data struct when the platform device has been released *and*
 * all references to our watchdog device are released.
 */
static void sch56xx_watchdog_release_resources(struct kref *r)
{
	struct sch56xx_watchdog_data *data =
		container_of(r, struct sch56xx_watchdog_data, kref);
	kfree(data);
}

static int watchdog_set_timeout(struct sch56xx_watchdog_data *data,
				int timeout)
{
	int ret, resolution;
	u8 control;

	/* 1 second or 60 second resolution? */
	if (timeout <= 255)
		resolution = 1;
	else
		resolution = 60;

	if (timeout < resolution || timeout > (resolution * 255))
		return -EINVAL;

	mutex_lock(&data->watchdog_lock);
	if (!data->addr) {
		ret = -ENODEV;
		goto leave;
	}

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
			goto leave;

		data->watchdog_control = control;
	}

	/*
	 * Remember new timeout value, but do not write as that (re)starts
	 * the watchdog countdown.
	 */
	data->watchdog_preset = DIV_ROUND_UP(timeout, resolution);

	ret = data->watchdog_preset * resolution;
leave:
	mutex_unlock(&data->watchdog_lock);
	return ret;
}

static int watchdog_get_timeout(struct sch56xx_watchdog_data *data)
{
	int timeout;

	mutex_lock(&data->watchdog_lock);
	if (data->watchdog_control & SCH56XX_WDOG_TIME_BASE_SEC)
		timeout = data->watchdog_preset;
	else
		timeout = data->watchdog_preset * 60;
	mutex_unlock(&data->watchdog_lock);

	return timeout;
}

static int watchdog_start(struct sch56xx_watchdog_data *data)
{
	int ret;
	u8 val;

	mutex_lock(&data->watchdog_lock);
	if (!data->addr) {
		ret = -ENODEV;
		goto leave_unlock_watchdog;
	}

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

	/* 2. Enable output (if not already enabled) */
	if (!(data->watchdog_output_enable & SCH56XX_WDOG_OUTPUT_ENABLE)) {
		val = data->watchdog_output_enable |
		      SCH56XX_WDOG_OUTPUT_ENABLE;
		ret = sch56xx_write_virtual_reg(data->addr,
						SCH56XX_REG_WDOG_OUTPUT_ENABLE,
						val);
		if (ret)
			goto leave;

		data->watchdog_output_enable = val;
	}

	/* 3. Clear the watchdog event bit if set */
	val = inb(data->addr + 9);
	if (val & 0x01)
		outb(0x01, data->addr + 9);

leave:
	mutex_unlock(data->io_lock);
leave_unlock_watchdog:
	mutex_unlock(&data->watchdog_lock);
	return ret;
}

static int watchdog_trigger(struct sch56xx_watchdog_data *data)
{
	int ret;

	mutex_lock(&data->watchdog_lock);
	if (!data->addr) {
		ret = -ENODEV;
		goto leave;
	}

	/* Reset the watchdog countdown counter */
	mutex_lock(data->io_lock);
	ret = sch56xx_write_virtual_reg(data->addr, SCH56XX_REG_WDOG_PRESET,
					data->watchdog_preset);
	mutex_unlock(data->io_lock);
leave:
	mutex_unlock(&data->watchdog_lock);
	return ret;
}

static int watchdog_stop_unlocked(struct sch56xx_watchdog_data *data)
{
	int ret = 0;
	u8 val;

	if (!data->addr)
		return -ENODEV;

	if (data->watchdog_output_enable & SCH56XX_WDOG_OUTPUT_ENABLE) {
		val = data->watchdog_output_enable &
		      ~SCH56XX_WDOG_OUTPUT_ENABLE;
		mutex_lock(data->io_lock);
		ret = sch56xx_write_virtual_reg(data->addr,
						SCH56XX_REG_WDOG_OUTPUT_ENABLE,
						val);
		mutex_unlock(data->io_lock);
		if (ret)
			return ret;

		data->watchdog_output_enable = val;
	}

	return ret;
}

static int watchdog_stop(struct sch56xx_watchdog_data *data)
{
	int ret;

	mutex_lock(&data->watchdog_lock);
	ret = watchdog_stop_unlocked(data);
	mutex_unlock(&data->watchdog_lock);

	return ret;
}

static int watchdog_release(struct inode *inode, struct file *filp)
{
	struct sch56xx_watchdog_data *data = filp->private_data;

	if (data->watchdog_expect_close) {
		watchdog_stop(data);
		data->watchdog_expect_close = 0;
	} else {
		watchdog_trigger(data);
		pr_crit("unexpected close, not stopping watchdog!\n");
	}

	clear_bit(0, &data->watchdog_is_open);

	mutex_lock(&watchdog_data_mutex);
	kref_put(&data->kref, sch56xx_watchdog_release_resources);
	mutex_unlock(&watchdog_data_mutex);

	return 0;
}

static int watchdog_open(struct inode *inode, struct file *filp)
{
	struct sch56xx_watchdog_data *pos, *data = NULL;
	int ret, watchdog_is_open;

	/*
	 * We get called from drivers/char/misc.c with misc_mtx hold, and we
	 * call misc_register() from sch56xx_watchdog_probe() with
	 * watchdog_data_mutex hold, as misc_register() takes the misc_mtx
	 * lock, this is a possible deadlock, so we use mutex_trylock here.
	 */
	if (!mutex_trylock(&watchdog_data_mutex))
		return -ERESTARTSYS;
	list_for_each_entry(pos, &watchdog_data_list, list) {
		if (pos->watchdog_miscdev.minor == iminor(inode)) {
			data = pos;
			break;
		}
	}
	/* Note we can never not have found data, so we don't check for this */
	watchdog_is_open = test_and_set_bit(0, &data->watchdog_is_open);
	if (!watchdog_is_open)
		kref_get(&data->kref);
	mutex_unlock(&watchdog_data_mutex);

	if (watchdog_is_open)
		return -EBUSY;

	filp->private_data = data;

	/* Start the watchdog */
	ret = watchdog_start(data);
	if (ret) {
		watchdog_release(inode, filp);
		return ret;
	}

	return nonseekable_open(inode, filp);
}

static ssize_t watchdog_write(struct file *filp, const char __user *buf,
	size_t count, loff_t *offset)
{
	int ret;
	struct sch56xx_watchdog_data *data = filp->private_data;

	if (count) {
		if (!nowayout) {
			size_t i;

			/* Clear it in case it was set with a previous write */
			data->watchdog_expect_close = 0;

			for (i = 0; i != count; i++) {
				char c;
				if (get_user(c, buf + i))
					return -EFAULT;
				if (c == 'V')
					data->watchdog_expect_close = 1;
			}
		}
		ret = watchdog_trigger(data);
		if (ret)
			return ret;
	}
	return count;
}

static long watchdog_ioctl(struct file *filp, unsigned int cmd,
			   unsigned long arg)
{
	struct watchdog_info ident = {
		.options = WDIOF_KEEPALIVEPING | WDIOF_SETTIMEOUT,
		.identity = "sch56xx watchdog"
	};
	int i, ret = 0;
	struct sch56xx_watchdog_data *data = filp->private_data;

	switch (cmd) {
	case WDIOC_GETSUPPORT:
		ident.firmware_version = data->revision;
		if (!nowayout)
			ident.options |= WDIOF_MAGICCLOSE;
		if (copy_to_user((void __user *)arg, &ident, sizeof(ident)))
			ret = -EFAULT;
		break;

	case WDIOC_GETSTATUS:
	case WDIOC_GETBOOTSTATUS:
		ret = put_user(0, (int __user *)arg);
		break;

	case WDIOC_KEEPALIVE:
		ret = watchdog_trigger(data);
		break;

	case WDIOC_GETTIMEOUT:
		i = watchdog_get_timeout(data);
		ret = put_user(i, (int __user *)arg);
		break;

	case WDIOC_SETTIMEOUT:
		if (get_user(i, (int __user *)arg)) {
			ret = -EFAULT;
			break;
		}
		ret = watchdog_set_timeout(data, i);
		if (ret >= 0)
			ret = put_user(ret, (int __user *)arg);
		break;

	case WDIOC_SETOPTIONS:
		if (get_user(i, (int __user *)arg)) {
			ret = -EFAULT;
			break;
		}

		if (i & WDIOS_DISABLECARD)
			ret = watchdog_stop(data);
		else if (i & WDIOS_ENABLECARD)
			ret = watchdog_trigger(data);
		else
			ret = -EINVAL;
		break;

	default:
		ret = -ENOTTY;
	}
	return ret;
}

static const struct file_operations watchdog_fops = {
	.owner = THIS_MODULE,
	.llseek = no_llseek,
	.open = watchdog_open,
	.release = watchdog_release,
	.write = watchdog_write,
	.unlocked_ioctl = watchdog_ioctl,
};

struct sch56xx_watchdog_data *sch56xx_watchdog_register(
	u16 addr, u32 revision, struct mutex *io_lock, int check_enabled)
{
	struct sch56xx_watchdog_data *data;
	int i, err, control, output_enable;
	const int watchdog_minors[] = { WATCHDOG_MINOR, 212, 213, 214, 215 };

	/* Cache the watchdog registers */
	mutex_lock(io_lock);
	control =
		sch56xx_read_virtual_reg(addr, SCH56XX_REG_WDOG_CONTROL);
	output_enable =
		sch56xx_read_virtual_reg(addr, SCH56XX_REG_WDOG_OUTPUT_ENABLE);
	mutex_unlock(io_lock);

	if (control < 0)
		return NULL;
	if (output_enable < 0)
		return NULL;
	if (check_enabled && !(output_enable & SCH56XX_WDOG_OUTPUT_ENABLE)) {
		pr_warn("Watchdog not enabled by BIOS, not registering\n");
		return NULL;
	}

	data = kzalloc(sizeof(struct sch56xx_watchdog_data), GFP_KERNEL);
	if (!data)
		return NULL;

	data->addr = addr;
	data->revision = revision;
	data->io_lock = io_lock;
	data->watchdog_control = control;
	data->watchdog_output_enable = output_enable;
	mutex_init(&data->watchdog_lock);
	INIT_LIST_HEAD(&data->list);
	kref_init(&data->kref);

	err = watchdog_set_timeout(data, 60);
	if (err < 0)
		goto error;

	/*
	 * We take the data_mutex lock early so that watchdog_open() cannot
	 * run when misc_register() has completed, but we've not yet added
	 * our data to the watchdog_data_list.
	 */
	mutex_lock(&watchdog_data_mutex);
	for (i = 0; i < ARRAY_SIZE(watchdog_minors); i++) {
		/* Register our watchdog part */
		snprintf(data->watchdog_name, sizeof(data->watchdog_name),
			"watchdog%c", (i == 0) ? '\0' : ('0' + i));
		data->watchdog_miscdev.name = data->watchdog_name;
		data->watchdog_miscdev.fops = &watchdog_fops;
		data->watchdog_miscdev.minor = watchdog_minors[i];
		err = misc_register(&data->watchdog_miscdev);
		if (err == -EBUSY)
			continue;
		if (err)
			break;

		list_add(&data->list, &watchdog_data_list);
		pr_info("Registered /dev/%s chardev major 10, minor: %d\n",
			data->watchdog_name, watchdog_minors[i]);
		break;
	}
	mutex_unlock(&watchdog_data_mutex);

	if (err) {
		pr_err("Registering watchdog chardev: %d\n", err);
		goto error;
	}
	if (i == ARRAY_SIZE(watchdog_minors)) {
		pr_warn("Couldn't register watchdog (no free minor)\n");
		goto error;
	}

	return data;

error:
	kfree(data);
	return NULL;
}
EXPORT_SYMBOL(sch56xx_watchdog_register);

void sch56xx_watchdog_unregister(struct sch56xx_watchdog_data *data)
{
	mutex_lock(&watchdog_data_mutex);
	misc_deregister(&data->watchdog_miscdev);
	list_del(&data->list);
	mutex_unlock(&watchdog_data_mutex);

	mutex_lock(&data->watchdog_lock);
	if (data->watchdog_is_open) {
		pr_warn("platform device unregistered with watchdog "
			"open! Stopping watchdog.\n");
		watchdog_stop_unlocked(data);
	}
	/* Tell the wdog start/stop/trigger functions our dev is gone */
	data->addr = 0;
	data->io_lock = NULL;
	mutex_unlock(&data->watchdog_lock);

	mutex_lock(&watchdog_data_mutex);
	kref_put(&data->kref, sch56xx_watchdog_release_resources);
	mutex_unlock(&watchdog_data_mutex);
}
EXPORT_SYMBOL(sch56xx_watchdog_unregister);

/*
 * platform dev find, add and remove functions
 */

static int __init sch56xx_find(int sioaddr, unsigned short *address,
			       const char **name)
{
	u8 devid;
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
	*address = superio_inb(sioaddr, SIO_REG_ADDR) |
		   superio_inb(sioaddr, SIO_REG_ADDR + 1) << 8;
	if (*address == 0) {
		pr_warn("Base address not set\n");
		err = -ENODEV;
		goto exit;
	}

exit:
	superio_exit(sioaddr);
	return err;
}

static int __init sch56xx_device_add(unsigned short address, const char *name)
{
	struct resource res = {
		.start	= address,
		.end	= address + REGION_LENGTH - 1,
		.flags	= IORESOURCE_IO,
	};
	int err;

	sch56xx_pdev = platform_device_alloc(name, address);
	if (!sch56xx_pdev)
		return -ENOMEM;

	res.name = sch56xx_pdev->name;
	err = acpi_check_resource_conflict(&res);
	if (err)
		goto exit_device_put;

	err = platform_device_add_resources(sch56xx_pdev, &res, 1);
	if (err) {
		pr_err("Device resource addition failed\n");
		goto exit_device_put;
	}

	err = platform_device_add(sch56xx_pdev);
	if (err) {
		pr_err("Device addition failed\n");
		goto exit_device_put;
	}

	return 0;

exit_device_put:
	platform_device_put(sch56xx_pdev);

	return err;
}

static int __init sch56xx_init(void)
{
	int err;
	unsigned short address;
	const char *name;

	err = sch56xx_find(0x4e, &address, &name);
	if (err)
		err = sch56xx_find(0x2e, &address, &name);
	if (err)
		return err;

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
