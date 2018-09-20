/*
 *  Dell AIO Serial Backlight Driver
 *
 *  Copyright (C) 2017 AceLan Kao <acelan.kao@canonical.com>
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/acpi.h>
#include <linux/serial_8250.h>
#include <linux/delay.h>
#include <linux/backlight.h>
#include <acpi/video.h>

#include "dell-uart-backlight.h"

struct dell_uart_backlight {
	struct device *dev;
	struct backlight_device *dell_uart_bd;
	struct mutex brightness_mutex;
	int line;
	int bl_power;
};
struct uart_8250_port *serial8250_get_port(int line);
static struct tty_struct *tty;
static struct file *ftty;

unsigned int (*io_serial_in)(struct uart_port *p, int offset);
int (*uart_write)(struct tty_struct *tty, const unsigned char *buf, int count);
int (*uart_chars_in_buffer)(struct tty_struct *tty);

static struct dell_uart_bl_cmd uart_cmd[] = {
	/*
	 * Get Firmware Version: Tool uses this command to get firmware version.
	 * Command: 0x6A 0x06 0x8F (Length:3 Type: 0x0A, Cmd:6 Checksum:0x8F)
	 * Return data: 0x0D 0x06 Data checksum (Length:13,Cmd:0x06,
	 *              Data :F/W version(APRILIA=APR27-VXXX,PHINE=PHI23-VXXX),
	 *              checksum:SUM(Length and Cmd and Data)xor 0xFF .
	 */
	[DELL_UART_GET_FIRMWARE_VER] = {
		.cmd	= {0x6A, 0x06, 0x8F},
		.tx_len	= 3,
	},
	/*
	 * Get Brightness level: Application uses this command for scaler to
	 *                       get brightness.
	 * Command: 0x6A 0x0C 0x89
	 *          (Length:3 Type: 0x0A, Cmd:0x0C, Checksum:0x89)
	 * Return data: 0x04 0x0C Data checksum
	 * (Length:4 Cmd: 0x0C Data: brightness level
	 *           checksum: SUM(Length and Cmd and Data)xor 0xFF)
	 *           brightness level which ranges from 0~100.
	 */
	[DELL_UART_GET_BRIGHTNESS] = {
		.cmd	= {0x6A, 0x0C, 0x89},
		.ret	= {0x04, 0x0C, 0x00, 0x00},
		.tx_len	= 3,
		.rx_len	= 4,
	},
	/* Set Brightness level: Application uses this command for scaler to
	 *                       set brightness.
	 * Command: 0x8A 0x0B Byte2 Checksum (Length:4 Type: 0x0A, Cmd:0x0B)
	 *          where Byte2 is the brightness level which ranges from 0~100.
	 * Return data: 0x03 0x0B 0xF1(Length:3,Cmd:B,checksum:0xF1)
	 * Scaler must send the 3bytes ack within 1 second when success,
	 * other value if error
	 */
	[DELL_UART_SET_BRIGHTNESS] = {
		.cmd	= {0x8A, 0x0B, 0x0, 0x0},
		.ret	= {0x03, 0x0B, 0xF1},
		.tx_len	= 4,
		.rx_len	= 3,
	},
	/*
	 * Screen ON/OFF Control: Application uses this command to control
	 *                        screen ON or OFF.
	 * Command: 0x8A 0x0E Byte2 Checksum (Length:4 Type: 0x0A, Cmd:0x0E)
	 *          where
	 *          Byte2=0 to turn OFF the screen.
	 *          Byte2=1 to turn ON the screen
	 *          Other value of Byte2 is reserved and invalid.
	 * Return data: 0x03 0x0E 0xEE(Length:3,Cmd:E,checksum:0xEE)
	 */
	[DELL_UART_SET_BACKLIGHT_POWER] = {
		.cmd	= {0x8A, 0x0E, 0x00, 0x0},
		.ret	= {0x03, 0x0E, 0xEE},
		.tx_len	= 4,
		.rx_len	= 3,
	},
};

static int dell_uart_write(struct uart_8250_port *up, __u8 *buf, int len)
{
	int actual = 0;
	struct uart_port *port = &up->port;

	tty_port_tty_wakeup(&port->state->port);
	tty = tty_port_tty_get(&port->state->port);
	actual = uart_write(tty, buf, len);
	while (uart_chars_in_buffer(tty))
		udelay(10);

	return actual;
}

static int dell_uart_read(struct uart_8250_port *up, __u8 *buf, int len)
{
	int i, retry;
	unsigned long flags;

	spin_lock_irqsave(&up->port.lock, flags);
	for (i = 0; i < len; i++) {
		retry = 10;
		while (!(io_serial_in(&up->port, UART_LSR) & UART_LSR_DR)) {
			if (--retry == 0)
				break;
			mdelay(20);
		}

		if (retry == 0)
			break;
		buf[i] = io_serial_in(&up->port, UART_RX);
	}
	spin_unlock_irqrestore(&up->port.lock, flags);

	return i;
}

static void dell_uart_dump_cmd(const char *func, const char *prefix,
			       const char *cmd, int len)
{
	char buf[80];

	snprintf(buf, 80, "dell_uart_backlight:%s:%s", func, prefix);
	if (len != 0)
		print_hex_dump_debug(buf, DUMP_PREFIX_NONE,
					16, 1, cmd, len, false);
	else
		pr_debug("dell_uart_backlight:%s:%sNULL\n", func, prefix);

}

/*
 * checksum: SUM(Length and Cmd and Data)xor 0xFF)
 */
static unsigned char dell_uart_checksum(unsigned char *buf, int len)
{
	unsigned char val = 0;

	while (len-- > 0)
		val += buf[len];

	return val ^ 0xff;
}

/*
 * There is no command to get backlight power status,
 * so we set the backlight power to "on" while initializing,
 * and then track and report its status by bl_power variable
 */
static inline int dell_uart_get_bl_power(struct dell_uart_backlight *dell_pdata)
{
	return dell_pdata->bl_power;
}

static int dell_uart_set_bl_power(struct backlight_device *bd, int power)
{
	struct dell_uart_bl_cmd *bl_cmd =
		&uart_cmd[DELL_UART_SET_BACKLIGHT_POWER];
	struct dell_uart_backlight *dell_pdata = bl_get_data(bd);
	struct uart_8250_port *uart = serial8250_get_port(dell_pdata->line);
	int rx_len;

	if (power != FB_BLANK_POWERDOWN)
		power = FB_BLANK_UNBLANK;

	bl_cmd->cmd[2] = power ? 0 : 1;
	bl_cmd->cmd[3] = dell_uart_checksum(bl_cmd->cmd, bl_cmd->tx_len - 1);

	dell_uart_dump_cmd(__func__, "tx: ", bl_cmd->cmd, bl_cmd->tx_len);

	if (mutex_lock_killable(&dell_pdata->brightness_mutex) < 0) {
		pr_debug("Failed to get mutex_lock");
		return 0;
	}

	dell_uart_write(uart, bl_cmd->cmd, bl_cmd->tx_len);
	rx_len = dell_uart_read(uart, bl_cmd->ret, bl_cmd->rx_len);

	mutex_unlock(&dell_pdata->brightness_mutex);

	dell_uart_dump_cmd(__func__, "rx: ", bl_cmd->ret, rx_len);

	bd->props.power = power;
	dell_pdata->bl_power = power;

	return 0;
}

static int dell_uart_get_brightness(struct backlight_device *bd)
{
	struct dell_uart_bl_cmd *bl_cmd = &uart_cmd[DELL_UART_GET_BRIGHTNESS];
	struct dell_uart_backlight *dell_pdata = bl_get_data(bd);
	struct uart_8250_port *uart = serial8250_get_port(dell_pdata->line);
	int rx_len, brightness = 0;

	dell_uart_dump_cmd(__func__, "tx: ", bl_cmd->cmd, bl_cmd->tx_len);

	if (mutex_lock_killable(&dell_pdata->brightness_mutex) < 0) {
		pr_debug("Failed to get mutex_lock");
		return 0;
	}

	dell_uart_write(uart, bl_cmd->cmd, bl_cmd->tx_len);
	rx_len = dell_uart_read(uart, bl_cmd->ret, bl_cmd->rx_len);

	mutex_unlock(&dell_pdata->brightness_mutex);

	dell_uart_dump_cmd(__func__, "rx: ", bl_cmd->ret, rx_len);

	brightness = (unsigned int)bl_cmd->ret[2];

	return brightness;
}

static int dell_uart_update_status(struct backlight_device *bd)
{
	struct dell_uart_bl_cmd *bl_cmd = &uart_cmd[DELL_UART_SET_BRIGHTNESS];
	struct dell_uart_backlight *dell_pdata = bl_get_data(bd);
	struct uart_8250_port *uart = serial8250_get_port(dell_pdata->line);
	int rx_len;

	bl_cmd->cmd[2] = bd->props.brightness;
	bl_cmd->cmd[3] = dell_uart_checksum(bl_cmd->cmd, bl_cmd->tx_len - 1);

	dell_uart_dump_cmd(__func__, "tx: ", bl_cmd->cmd, bl_cmd->tx_len);

	if (mutex_lock_killable(&dell_pdata->brightness_mutex) < 0) {
		pr_debug("Failed to get mutex_lock");
		return 0;
	}

	dell_uart_write(uart, bl_cmd->cmd, bl_cmd->tx_len);
	rx_len = dell_uart_read(uart, bl_cmd->ret, bl_cmd->rx_len);

	mutex_unlock(&dell_pdata->brightness_mutex);

	dell_uart_dump_cmd(__func__, "rx: ", bl_cmd->ret, rx_len);

	if (bd->props.power != dell_uart_get_bl_power(dell_pdata))
		dell_uart_set_bl_power(bd, bd->props.power);

	return 0;
}

static int dell_uart_get_scalar_status(struct dell_uart_backlight *dell_pdata)
{
	struct dell_uart_bl_cmd *bl_cmd = &uart_cmd[DELL_UART_GET_SCALAR];
	struct uart_8250_port *uart = serial8250_get_port(dell_pdata->line);
	int rx_len;
	/* assume the scalar IC controls backlight if the command failed */
	int status = 1;

	dell_uart_dump_cmd(__func__, "tx: ", bl_cmd->cmd, bl_cmd->tx_len);

	if (mutex_lock_killable(&dell_pdata->brightness_mutex) < 0) {
		pr_debug("Failed to get mutex_lock");
		return 0;
	}

	dell_uart_write(uart, bl_cmd->cmd, bl_cmd->tx_len);
	rx_len = dell_uart_read(uart, bl_cmd->ret, bl_cmd->rx_len);

	mutex_unlock(&dell_pdata->brightness_mutex);

	dell_uart_dump_cmd(__func__, "rx: ", bl_cmd->ret, rx_len);

	if (rx_len == 4)
		status = (unsigned int)bl_cmd->ret[2];

	return status;
}

static int dell_uart_show_firmware_ver(struct dell_uart_backlight *dell_pdata)
{
	struct dell_uart_bl_cmd *bl_cmd = &uart_cmd[DELL_UART_GET_FIRMWARE_VER];
	struct uart_8250_port *uart = serial8250_get_port(dell_pdata->line);
	int rx_len = 0, retry = 10;

	dell_uart_dump_cmd(__func__, "tx: ", bl_cmd->cmd, bl_cmd->tx_len);

	if (mutex_lock_killable(&dell_pdata->brightness_mutex) < 0) {
		pr_debug("Failed to get mutex_lock");
		return -1;
	}

	dell_uart_write(uart, bl_cmd->cmd, bl_cmd->tx_len);
	while (retry-- > 0) {
		/* first byte is data length */
		dell_uart_read(uart, bl_cmd->ret, 1);
		rx_len = (int)bl_cmd->ret[0];
		if (bl_cmd->ret[0] > 80 || bl_cmd->ret[0] == 0) {
			pr_debug("Failed to get firmware version\n");
			if (retry == 0) {
				mutex_unlock(&dell_pdata->brightness_mutex);
				return -1;
			}
			msleep(100);
			continue;
		}

		dell_uart_read(uart, bl_cmd->ret+1, rx_len-1);
		break;
	}
	mutex_unlock(&dell_pdata->brightness_mutex);

	dell_uart_dump_cmd(__func__, "rx: ", bl_cmd->ret, rx_len);

	pr_debug("Firmare str(%d)= %s\n", (int)bl_cmd->ret[0], bl_cmd->ret+2);
	return rx_len;
}

static const struct backlight_ops dell_uart_backlight_ops = {
	.get_brightness = dell_uart_get_brightness,
	.update_status = dell_uart_update_status,
};

static int dell_uart_startup(struct dell_uart_backlight *dell_pdata)
{
	struct uart_8250_port *uartp;
	struct uart_port *port;

	dell_pdata->line = 0;
	uartp = serial8250_get_port(dell_pdata->line);
	port = &uartp->port;
	tty = port->state->port.tty;
	io_serial_in = port->serial_in;
	uart_write = tty->driver->ops->write;
	uart_chars_in_buffer = tty->driver->ops->chars_in_buffer;

	return 0;
}

static int dell_uart_bl_add(struct acpi_device *dev)
{
	struct dell_uart_backlight *dell_pdata;
	struct backlight_properties props;
	struct backlight_device *dell_uart_bd;

	dell_pdata = kzalloc(sizeof(struct dell_uart_backlight), GFP_KERNEL);
	if (!dell_pdata) {
		pr_debug("Failed to allocate memory for dell_uart_backlight\n");
		return -1;
	}
	dell_pdata->dev = &dev->dev;
	dell_uart_startup(dell_pdata);
	dev->driver_data = dell_pdata;

	mutex_init(&dell_pdata->brightness_mutex);

	if (!dell_uart_get_scalar_status(dell_pdata)) {
		udelay(50);
		/* try another command to make sure there is no scalar IC */
		if (dell_uart_show_firmware_ver(dell_pdata) <= 0) {
			pr_debug("Scalar is not in charge of brightness adjustment.\n");
			kzfree(dell_pdata);
			return -1;
		}
	}

	memset(&props, 0, sizeof(struct backlight_properties));
	props.type = BACKLIGHT_PLATFORM;
	props.max_brightness = 100;

	dell_uart_bd = backlight_device_register("dell_uart_backlight",
						 &dev->dev,
						 dell_pdata,
						 &dell_uart_backlight_ops,
						 &props);
	if (IS_ERR(dell_uart_bd)) {
		kzfree(dell_pdata);
		pr_debug("Backlight registration failed\n");
		return -1;
	}

	dell_pdata->dell_uart_bd = dell_uart_bd;

	dell_uart_set_bl_power(dell_uart_bd, FB_BLANK_UNBLANK);
	dell_uart_bd->props.brightness = 100;
	backlight_update_status(dell_uart_bd);

	/* unregister acpi backlight interface */
	acpi_video_set_dmi_backlight_type(acpi_backlight_vendor);

	return 0;
}

static int dell_uart_bl_remove(struct acpi_device *dev)
{
	struct dell_uart_backlight *dell_pdata = dev->driver_data;

	backlight_device_unregister(dell_pdata->dell_uart_bd);
	kzfree(dell_pdata);

	return 0;
}

static int dell_uart_bl_suspend(struct device *dev)
{
	filp_close(ftty, NULL);
	return 0;
}

static int dell_uart_bl_resume(struct device *dev)
{
	ftty = filp_open("/dev/ttyS0", O_RDWR | O_NOCTTY | O_NDELAY, 0);
	return 0;
}

static SIMPLE_DEV_PM_OPS(dell_uart_bl_pm, dell_uart_bl_suspend, dell_uart_bl_resume);

static const struct acpi_device_id dell_uart_bl_ids[] = {
	{"DELL0501", 0},
	{"", 0},
};

static struct acpi_driver dell_uart_backlight_driver = {
	.name = "Dell AIO serial backlight",
	.ids = dell_uart_bl_ids,
	.ops = {
		.add = dell_uart_bl_add,
		.remove = dell_uart_bl_remove,
	},
	.drv.pm = &dell_uart_bl_pm,
};

static int __init dell_uart_bl_init(void)
{
	ftty = filp_open("/dev/ttyS0", O_RDWR | O_NOCTTY | O_NDELAY, 0);
	if (IS_ERR(ftty)) {
		pr_debug("cannot open /dev/ttyS0\n");
		return -EINVAL;
	}

	return acpi_bus_register_driver(&dell_uart_backlight_driver);
}

static void __exit dell_uart_bl_exit(void)
{
	filp_close(ftty, NULL);

	acpi_bus_unregister_driver(&dell_uart_backlight_driver);
}

module_init(dell_uart_bl_init);
module_exit(dell_uart_bl_exit);
MODULE_DEVICE_TABLE(acpi, dell_uart_bl_ids);
MODULE_DESCRIPTION("Dell AIO Serial Backlight module");
MODULE_AUTHOR("AceLan Kao <acelan.kao@canonical.com>");
MODULE_LICENSE("GPL");
