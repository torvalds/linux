// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * (C) Copyright 2008
 * Stefano Babic, DENX Software Engineering, sbabic@denx.de.
 *
 * This driver implements a lcd device for the ILITEK 922x display
 * controller. The interface to the display is SPI and the display's
 * memory is cyclically updated over the RGB interface.
 */

#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/init.h>
#include <linux/kernel.h>
#include <linux/lcd.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/string.h>

/* Register offset, see manual section 8.2 */
#define REG_START_OSCILLATION			0x00
#define REG_DRIVER_CODE_READ			0x00
#define REG_DRIVER_OUTPUT_CONTROL		0x01
#define REG_LCD_AC_DRIVEING_CONTROL		0x02
#define REG_ENTRY_MODE				0x03
#define REG_COMPARE_1				0x04
#define REG_COMPARE_2				0x05
#define REG_DISPLAY_CONTROL_1			0x07
#define REG_DISPLAY_CONTROL_2			0x08
#define REG_DISPLAY_CONTROL_3			0x09
#define REG_FRAME_CYCLE_CONTROL			0x0B
#define REG_EXT_INTF_CONTROL			0x0C
#define REG_POWER_CONTROL_1			0x10
#define REG_POWER_CONTROL_2			0x11
#define REG_POWER_CONTROL_3			0x12
#define REG_POWER_CONTROL_4			0x13
#define REG_RAM_ADDRESS_SET			0x21
#define REG_WRITE_DATA_TO_GRAM			0x22
#define REG_RAM_WRITE_MASK1			0x23
#define REG_RAM_WRITE_MASK2			0x24
#define REG_GAMMA_CONTROL_1			0x30
#define REG_GAMMA_CONTROL_2			0x31
#define REG_GAMMA_CONTROL_3			0x32
#define REG_GAMMA_CONTROL_4			0x33
#define REG_GAMMA_CONTROL_5			0x34
#define REG_GAMMA_CONTROL_6			0x35
#define REG_GAMMA_CONTROL_7			0x36
#define REG_GAMMA_CONTROL_8			0x37
#define REG_GAMMA_CONTROL_9			0x38
#define REG_GAMMA_CONTROL_10			0x39
#define REG_GATE_SCAN_CONTROL			0x40
#define REG_VERT_SCROLL_CONTROL			0x41
#define REG_FIRST_SCREEN_DRIVE_POS		0x42
#define REG_SECOND_SCREEN_DRIVE_POS		0x43
#define REG_RAM_ADDR_POS_H			0x44
#define REG_RAM_ADDR_POS_V			0x45
#define REG_OSCILLATOR_CONTROL			0x4F
#define REG_GPIO				0x60
#define REG_OTP_VCM_PROGRAMMING			0x61
#define REG_OTP_VCM_STATUS_ENABLE		0x62
#define REG_OTP_PROGRAMMING_ID_KEY		0x65

/*
 * maximum frequency for register access
 * (not for the GRAM access)
 */
#define ILITEK_MAX_FREQ_REG	4000000

/*
 * Device ID as found in the datasheet (supports 9221 and 9222)
 */
#define ILITEK_DEVICE_ID	0x9220
#define ILITEK_DEVICE_ID_MASK	0xFFF0

/* Last two bits in the START BYTE */
#define START_RS_INDEX		0
#define START_RS_REG		1
#define START_RW_WRITE		0
#define START_RW_READ		1

/**
 * START_BYTE(id, rs, rw)
 *
 * Set the start byte according to the required operation.
 * The start byte is defined as:
 *   ----------------------------------
 *  | 0 | 1 | 1 | 1 | 0 | ID | RS | RW |
 *   ----------------------------------
 * @id: display's id as set by the manufacturer
 * @rs: operation type bit, one of:
 *	  - START_RS_INDEX	set the index register
 *	  - START_RS_REG	write/read registers/GRAM
 * @rw: read/write operation
 *	 - START_RW_WRITE	write
 *	 - START_RW_READ	read
 */
#define START_BYTE(id, rs, rw)	\
	(0x70 | (((id) & 0x01) << 2) | (((rs) & 0x01) << 1) | ((rw) & 0x01))

/**
 * CHECK_FREQ_REG(spi_device s, spi_transfer x) - Check the frequency
 *	for the SPI transfer. According to the datasheet, the controller
 *	accept higher frequency for the GRAM transfer, but it requires
 *	lower frequency when the registers are read/written.
 *	The macro sets the frequency in the spi_transfer structure if
 *	the frequency exceeds the maximum value.
 * @s: pointer to an SPI device
 * @x: pointer to the read/write buffer pair
 */
#define CHECK_FREQ_REG(s, x)	\
	do {			\
		if (s->max_speed_hz > ILITEK_MAX_FREQ_REG)	\
			((struct spi_transfer *)x)->speed_hz =	\
					ILITEK_MAX_FREQ_REG;	\
	} while (0)

#define CMD_BUFSIZE		16

#define POWER_IS_ON(pwr)	((pwr) <= FB_BLANK_NORMAL)

#define set_tx_byte(b)		(tx_invert ? ~(b) : b)

/*
 * ili922x_id - id as set by manufacturer
 */
static int ili922x_id = 1;
module_param(ili922x_id, int, 0);

static int tx_invert;
module_param(tx_invert, int, 0);

/*
 * driver's private structure
 */
struct ili922x {
	struct spi_device *spi;
	struct lcd_device *ld;
	int power;
};

/**
 * ili922x_read_status - read status register from display
 * @spi: spi device
 * @rs:  output value
 */
static int ili922x_read_status(struct spi_device *spi, u16 *rs)
{
	struct spi_message msg;
	struct spi_transfer xfer;
	unsigned char tbuf[CMD_BUFSIZE];
	unsigned char rbuf[CMD_BUFSIZE];
	int ret, i;

	memset(&xfer, 0, sizeof(struct spi_transfer));
	spi_message_init(&msg);
	xfer.tx_buf = tbuf;
	xfer.rx_buf = rbuf;
	xfer.cs_change = 1;
	CHECK_FREQ_REG(spi, &xfer);

	tbuf[0] = set_tx_byte(START_BYTE(ili922x_id, START_RS_INDEX,
					 START_RW_READ));
	/*
	 * we need 4-byte xfer here due to invalid dummy byte
	 * received after start byte
	 */
	for (i = 1; i < 4; i++)
		tbuf[i] = set_tx_byte(0);	/* dummy */

	xfer.bits_per_word = 8;
	xfer.len = 4;
	spi_message_add_tail(&xfer, &msg);
	ret = spi_sync(spi, &msg);
	if (ret < 0) {
		dev_dbg(&spi->dev, "Error sending SPI message 0x%x", ret);
		return ret;
	}

	*rs = (rbuf[2] << 8) + rbuf[3];
	return 0;
}

/**
 * ili922x_read - read register from display
 * @spi: spi device
 * @reg: offset of the register to be read
 * @rx:  output value
 */
static int ili922x_read(struct spi_device *spi, u8 reg, u16 *rx)
{
	struct spi_message msg;
	struct spi_transfer xfer_regindex, xfer_regvalue;
	unsigned char tbuf[CMD_BUFSIZE];
	unsigned char rbuf[CMD_BUFSIZE];
	int ret, len = 0, send_bytes;

	memset(&xfer_regindex, 0, sizeof(struct spi_transfer));
	memset(&xfer_regvalue, 0, sizeof(struct spi_transfer));
	spi_message_init(&msg);
	xfer_regindex.tx_buf = tbuf;
	xfer_regindex.rx_buf = rbuf;
	xfer_regindex.cs_change = 1;
	CHECK_FREQ_REG(spi, &xfer_regindex);

	tbuf[0] = set_tx_byte(START_BYTE(ili922x_id, START_RS_INDEX,
					 START_RW_WRITE));
	tbuf[1] = set_tx_byte(0);
	tbuf[2] = set_tx_byte(reg);
	xfer_regindex.bits_per_word = 8;
	len = xfer_regindex.len = 3;
	spi_message_add_tail(&xfer_regindex, &msg);

	send_bytes = len;

	tbuf[len++] = set_tx_byte(START_BYTE(ili922x_id, START_RS_REG,
					     START_RW_READ));
	tbuf[len++] = set_tx_byte(0);
	tbuf[len] = set_tx_byte(0);

	xfer_regvalue.cs_change = 1;
	xfer_regvalue.len = 3;
	xfer_regvalue.tx_buf = &tbuf[send_bytes];
	xfer_regvalue.rx_buf = &rbuf[send_bytes];
	CHECK_FREQ_REG(spi, &xfer_regvalue);

	spi_message_add_tail(&xfer_regvalue, &msg);
	ret = spi_sync(spi, &msg);
	if (ret < 0) {
		dev_dbg(&spi->dev, "Error sending SPI message 0x%x", ret);
		return ret;
	}

	*rx = (rbuf[1 + send_bytes] << 8) + rbuf[2 + send_bytes];
	return 0;
}

/**
 * ili922x_write - write a controller register
 * @spi: struct spi_device *
 * @reg: offset of the register to be written
 * @value: value to be written
 */
static int ili922x_write(struct spi_device *spi, u8 reg, u16 value)
{
	struct spi_message msg;
	struct spi_transfer xfer_regindex, xfer_regvalue;
	unsigned char tbuf[CMD_BUFSIZE];
	unsigned char rbuf[CMD_BUFSIZE];
	int ret;

	memset(&xfer_regindex, 0, sizeof(struct spi_transfer));
	memset(&xfer_regvalue, 0, sizeof(struct spi_transfer));

	spi_message_init(&msg);
	xfer_regindex.tx_buf = tbuf;
	xfer_regindex.rx_buf = rbuf;
	xfer_regindex.cs_change = 1;
	CHECK_FREQ_REG(spi, &xfer_regindex);

	tbuf[0] = set_tx_byte(START_BYTE(ili922x_id, START_RS_INDEX,
					 START_RW_WRITE));
	tbuf[1] = set_tx_byte(0);
	tbuf[2] = set_tx_byte(reg);
	xfer_regindex.bits_per_word = 8;
	xfer_regindex.len = 3;
	spi_message_add_tail(&xfer_regindex, &msg);

	ret = spi_sync(spi, &msg);

	spi_message_init(&msg);
	tbuf[0] = set_tx_byte(START_BYTE(ili922x_id, START_RS_REG,
					 START_RW_WRITE));
	tbuf[1] = set_tx_byte((value & 0xFF00) >> 8);
	tbuf[2] = set_tx_byte(value & 0x00FF);

	xfer_regvalue.cs_change = 1;
	xfer_regvalue.len = 3;
	xfer_regvalue.tx_buf = tbuf;
	xfer_regvalue.rx_buf = rbuf;
	CHECK_FREQ_REG(spi, &xfer_regvalue);

	spi_message_add_tail(&xfer_regvalue, &msg);

	ret = spi_sync(spi, &msg);
	if (ret < 0) {
		dev_err(&spi->dev, "Error sending SPI message 0x%x", ret);
		return ret;
	}
	return 0;
}

#ifdef DEBUG
/**
 * ili922x_reg_dump - dump all registers
 *
 * @spi: pointer to an SPI device
 */
static void ili922x_reg_dump(struct spi_device *spi)
{
	u8 reg;
	u16 rx;

	dev_dbg(&spi->dev, "ILI922x configuration registers:\n");
	for (reg = REG_START_OSCILLATION;
	     reg <= REG_OTP_PROGRAMMING_ID_KEY; reg++) {
		ili922x_read(spi, reg, &rx);
		dev_dbg(&spi->dev, "reg @ 0x%02X: 0x%04X\n", reg, rx);
	}
}
#else
static inline void ili922x_reg_dump(struct spi_device *spi) {}
#endif

/**
 * set_write_to_gram_reg - initialize the display to write the GRAM
 * @spi: spi device
 */
static void set_write_to_gram_reg(struct spi_device *spi)
{
	struct spi_message msg;
	struct spi_transfer xfer;
	unsigned char tbuf[CMD_BUFSIZE];

	memset(&xfer, 0, sizeof(struct spi_transfer));

	spi_message_init(&msg);
	xfer.tx_buf = tbuf;
	xfer.rx_buf = NULL;
	xfer.cs_change = 1;

	tbuf[0] = START_BYTE(ili922x_id, START_RS_INDEX, START_RW_WRITE);
	tbuf[1] = 0;
	tbuf[2] = REG_WRITE_DATA_TO_GRAM;

	xfer.bits_per_word = 8;
	xfer.len = 3;
	spi_message_add_tail(&xfer, &msg);
	spi_sync(spi, &msg);
}

/**
 * ili922x_poweron - turn the display on
 * @spi: spi device
 *
 * The sequence to turn on the display is taken from
 * the datasheet and/or the example code provided by the
 * manufacturer.
 */
static int ili922x_poweron(struct spi_device *spi)
{
	int ret;

	/* Power on */
	ret = ili922x_write(spi, REG_POWER_CONTROL_1, 0x0000);
	usleep_range(10000, 10500);
	ret += ili922x_write(spi, REG_POWER_CONTROL_2, 0x0000);
	ret += ili922x_write(spi, REG_POWER_CONTROL_3, 0x0000);
	msleep(40);
	ret += ili922x_write(spi, REG_POWER_CONTROL_4, 0x0000);
	msleep(40);
	/* register 0x56 is not documented in the datasheet */
	ret += ili922x_write(spi, 0x56, 0x080F);
	ret += ili922x_write(spi, REG_POWER_CONTROL_1, 0x4240);
	usleep_range(10000, 10500);
	ret += ili922x_write(spi, REG_POWER_CONTROL_2, 0x0000);
	ret += ili922x_write(spi, REG_POWER_CONTROL_3, 0x0014);
	msleep(40);
	ret += ili922x_write(spi, REG_POWER_CONTROL_4, 0x1319);
	msleep(40);

	return ret;
}

/**
 * ili922x_poweroff - turn the display off
 * @spi: spi device
 */
static int ili922x_poweroff(struct spi_device *spi)
{
	int ret;

	/* Power off */
	ret = ili922x_write(spi, REG_POWER_CONTROL_1, 0x0000);
	usleep_range(10000, 10500);
	ret += ili922x_write(spi, REG_POWER_CONTROL_2, 0x0000);
	ret += ili922x_write(spi, REG_POWER_CONTROL_3, 0x0000);
	msleep(40);
	ret += ili922x_write(spi, REG_POWER_CONTROL_4, 0x0000);
	msleep(40);

	return ret;
}

/**
 * ili922x_display_init - initialize the display by setting
 *			  the configuration registers
 * @spi: spi device
 */
static void ili922x_display_init(struct spi_device *spi)
{
	ili922x_write(spi, REG_START_OSCILLATION, 1);
	usleep_range(10000, 10500);
	ili922x_write(spi, REG_DRIVER_OUTPUT_CONTROL, 0x691B);
	ili922x_write(spi, REG_LCD_AC_DRIVEING_CONTROL, 0x0700);
	ili922x_write(spi, REG_ENTRY_MODE, 0x1030);
	ili922x_write(spi, REG_COMPARE_1, 0x0000);
	ili922x_write(spi, REG_COMPARE_2, 0x0000);
	ili922x_write(spi, REG_DISPLAY_CONTROL_1, 0x0037);
	ili922x_write(spi, REG_DISPLAY_CONTROL_2, 0x0202);
	ili922x_write(spi, REG_DISPLAY_CONTROL_3, 0x0000);
	ili922x_write(spi, REG_FRAME_CYCLE_CONTROL, 0x0000);

	/* Set RGB interface */
	ili922x_write(spi, REG_EXT_INTF_CONTROL, 0x0110);

	ili922x_poweron(spi);

	ili922x_write(spi, REG_GAMMA_CONTROL_1, 0x0302);
	ili922x_write(spi, REG_GAMMA_CONTROL_2, 0x0407);
	ili922x_write(spi, REG_GAMMA_CONTROL_3, 0x0304);
	ili922x_write(spi, REG_GAMMA_CONTROL_4, 0x0203);
	ili922x_write(spi, REG_GAMMA_CONTROL_5, 0x0706);
	ili922x_write(spi, REG_GAMMA_CONTROL_6, 0x0407);
	ili922x_write(spi, REG_GAMMA_CONTROL_7, 0x0706);
	ili922x_write(spi, REG_GAMMA_CONTROL_8, 0x0000);
	ili922x_write(spi, REG_GAMMA_CONTROL_9, 0x0C06);
	ili922x_write(spi, REG_GAMMA_CONTROL_10, 0x0F00);
	ili922x_write(spi, REG_RAM_ADDRESS_SET, 0x0000);
	ili922x_write(spi, REG_GATE_SCAN_CONTROL, 0x0000);
	ili922x_write(spi, REG_VERT_SCROLL_CONTROL, 0x0000);
	ili922x_write(spi, REG_FIRST_SCREEN_DRIVE_POS, 0xDB00);
	ili922x_write(spi, REG_SECOND_SCREEN_DRIVE_POS, 0xDB00);
	ili922x_write(spi, REG_RAM_ADDR_POS_H, 0xAF00);
	ili922x_write(spi, REG_RAM_ADDR_POS_V, 0xDB00);
	ili922x_reg_dump(spi);
	set_write_to_gram_reg(spi);
}

static int ili922x_lcd_power(struct ili922x *lcd, int power)
{
	int ret = 0;

	if (POWER_IS_ON(power) && !POWER_IS_ON(lcd->power))
		ret = ili922x_poweron(lcd->spi);
	else if (!POWER_IS_ON(power) && POWER_IS_ON(lcd->power))
		ret = ili922x_poweroff(lcd->spi);

	if (!ret)
		lcd->power = power;

	return ret;
}

static int ili922x_set_power(struct lcd_device *ld, int power)
{
	struct ili922x *ili = lcd_get_data(ld);

	return ili922x_lcd_power(ili, power);
}

static int ili922x_get_power(struct lcd_device *ld)
{
	struct ili922x *ili = lcd_get_data(ld);

	return ili->power;
}

static struct lcd_ops ili922x_ops = {
	.get_power = ili922x_get_power,
	.set_power = ili922x_set_power,
};

static int ili922x_probe(struct spi_device *spi)
{
	struct ili922x *ili;
	struct lcd_device *lcd;
	int ret;
	u16 reg = 0;

	ili = devm_kzalloc(&spi->dev, sizeof(*ili), GFP_KERNEL);
	if (!ili)
		return -ENOMEM;

	ili->spi = spi;
	spi_set_drvdata(spi, ili);

	/* check if the device is connected */
	ret = ili922x_read(spi, REG_DRIVER_CODE_READ, &reg);
	if (ret || ((reg & ILITEK_DEVICE_ID_MASK) != ILITEK_DEVICE_ID)) {
		dev_err(&spi->dev,
			"no LCD found: Chip ID 0x%x, ret %d\n",
			reg, ret);
		return -ENODEV;
	}

	dev_info(&spi->dev, "ILI%x found, SPI freq %d, mode %d\n",
		 reg, spi->max_speed_hz, spi->mode);

	ret = ili922x_read_status(spi, &reg);
	if (ret) {
		dev_err(&spi->dev, "reading RS failed...\n");
		return ret;
	}

	dev_dbg(&spi->dev, "status: 0x%x\n", reg);

	ili922x_display_init(spi);

	ili->power = FB_BLANK_POWERDOWN;

	lcd = devm_lcd_device_register(&spi->dev, "ili922xlcd", &spi->dev, ili,
					&ili922x_ops);
	if (IS_ERR(lcd)) {
		dev_err(&spi->dev, "cannot register LCD\n");
		return PTR_ERR(lcd);
	}

	ili->ld = lcd;
	spi_set_drvdata(spi, ili);

	ili922x_lcd_power(ili, FB_BLANK_UNBLANK);

	return 0;
}

static int ili922x_remove(struct spi_device *spi)
{
	ili922x_poweroff(spi);
	return 0;
}

static struct spi_driver ili922x_driver = {
	.driver = {
		.name = "ili922x",
	},
	.probe = ili922x_probe,
	.remove = ili922x_remove,
};

module_spi_driver(ili922x_driver);

MODULE_AUTHOR("Stefano Babic <sbabic@denx.de>");
MODULE_DESCRIPTION("ILI9221/9222 LCD driver");
MODULE_LICENSE("GPL");
MODULE_PARM_DESC(ili922x_id, "set controller identifier (default=1)");
MODULE_PARM_DESC(tx_invert, "invert bytes before sending");
