// SPDX-License-Identifier: GPL-2.0
/* Driver for ORISE Technology OTM3225A SOC for TFT LCD
 * Copyright (C) 2017, EETS GmbH, Felix Brack <fb@ltec.ch>
 *
 * This driver implements a lcd device for the ORISE OTM3225A display
 * controller. The control interface to the display is SPI and the display's
 * memory is updated over the 16-bit RGB interface.
 * The main source of information for writing this driver was provided by the
 * OTM3225A datasheet from ORISE Technology. Some information arise from the
 * ILI9328 datasheet from ILITEK as well as from the datasheets and sample code
 * provided by Crystalfontz America Inc. who sells the CFAF240320A-032T, a 3.2"
 * TFT LC display using the OTM3225A controller.
 */

#include <linux/delay.h>
#include <linux/device.h>
#include <linux/kernel.h>
#include <linux/lcd.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#define OTM3225A_INDEX_REG	0x70
#define OTM3225A_DATA_REG	0x72

/* instruction register list */
#define DRIVER_OUTPUT_CTRL_1	0x01
#define DRIVER_WAVEFORM_CTRL	0x02
#define ENTRY_MODE		0x03
#define SCALING_CTRL		0x04
#define DISPLAY_CTRL_1		0x07
#define DISPLAY_CTRL_2		0x08
#define DISPLAY_CTRL_3		0x09
#define FRAME_CYCLE_CTRL	0x0A
#define EXT_DISP_IFACE_CTRL_1	0x0C
#define FRAME_MAKER_POS		0x0D
#define EXT_DISP_IFACE_CTRL_2	0x0F
#define POWER_CTRL_1		0x10
#define POWER_CTRL_2		0x11
#define POWER_CTRL_3		0x12
#define POWER_CTRL_4		0x13
#define GRAM_ADDR_HORIZ_SET	0x20
#define GRAM_ADDR_VERT_SET	0x21
#define GRAM_READ_WRITE		0x22
#define POWER_CTRL_7		0x29
#define FRAME_RATE_CTRL		0x2B
#define GAMMA_CTRL_1		0x30
#define GAMMA_CTRL_2		0x31
#define GAMMA_CTRL_3		0x32
#define GAMMA_CTRL_4		0x35
#define GAMMA_CTRL_5		0x36
#define GAMMA_CTRL_6		0x37
#define GAMMA_CTRL_7		0x38
#define GAMMA_CTRL_8		0x39
#define GAMMA_CTRL_9		0x3C
#define GAMMA_CTRL_10		0x3D
#define WINDOW_HORIZ_RAM_START	0x50
#define WINDOW_HORIZ_RAM_END	0x51
#define WINDOW_VERT_RAM_START	0x52
#define WINDOW_VERT_RAM_END	0x53
#define DRIVER_OUTPUT_CTRL_2	0x60
#define BASE_IMG_DISPLAY_CTRL	0x61
#define VERT_SCROLL_CTRL	0x6A
#define PD1_DISPLAY_POS		0x80
#define PD1_RAM_START		0x81
#define PD1_RAM_END		0x82
#define PD2_DISPLAY_POS		0x83
#define PD2_RAM_START		0x84
#define PD2_RAM_END		0x85
#define PANEL_IFACE_CTRL_1	0x90
#define PANEL_IFACE_CTRL_2	0x92
#define PANEL_IFACE_CTRL_4	0x95
#define PANEL_IFACE_CTRL_5	0x97

struct otm3225a_data {
	struct spi_device *spi;
	struct lcd_device *ld;
	int power;
};

struct otm3225a_spi_instruction {
	unsigned char reg;	/* register to write */
	unsigned short value;	/* data to write to 'reg' */
	unsigned short delay;	/* delay in ms after write */
};

static struct otm3225a_spi_instruction display_init[] = {
	{ DRIVER_OUTPUT_CTRL_1,		0x0000, 0 },
	{ DRIVER_WAVEFORM_CTRL,		0x0700, 0 },
	{ ENTRY_MODE,			0x50A0, 0 },
	{ SCALING_CTRL,			0x0000, 0 },
	{ DISPLAY_CTRL_2,		0x0606, 0 },
	{ DISPLAY_CTRL_3,		0x0000, 0 },
	{ FRAME_CYCLE_CTRL,		0x0000, 0 },
	{ EXT_DISP_IFACE_CTRL_1,	0x0000, 0 },
	{ FRAME_MAKER_POS,		0x0000, 0 },
	{ EXT_DISP_IFACE_CTRL_2,	0x0002, 0 },
	{ POWER_CTRL_2,			0x0007, 0 },
	{ POWER_CTRL_3,			0x0000, 0 },
	{ POWER_CTRL_4,			0x0000, 200 },
	{ DISPLAY_CTRL_1,		0x0101, 0 },
	{ POWER_CTRL_1,			0x12B0, 0 },
	{ POWER_CTRL_2,			0x0007, 0 },
	{ POWER_CTRL_3,			0x01BB, 50 },
	{ POWER_CTRL_4,			0x0013, 0 },
	{ POWER_CTRL_7,			0x0010, 50 },
	{ GAMMA_CTRL_1,			0x000A, 0 },
	{ GAMMA_CTRL_2,			0x1326, 0 },
	{ GAMMA_CTRL_3,			0x0A29, 0 },
	{ GAMMA_CTRL_4,			0x0A0A, 0 },
	{ GAMMA_CTRL_5,			0x1E03, 0 },
	{ GAMMA_CTRL_6,			0x031E, 0 },
	{ GAMMA_CTRL_7,			0x0706, 0 },
	{ GAMMA_CTRL_8,			0x0303, 0 },
	{ GAMMA_CTRL_9,			0x010E, 0 },
	{ GAMMA_CTRL_10,		0x040E, 0 },
	{ WINDOW_HORIZ_RAM_START,	0x0000, 0 },
	{ WINDOW_HORIZ_RAM_END,		0x00EF, 0 },
	{ WINDOW_VERT_RAM_START,	0x0000, 0 },
	{ WINDOW_VERT_RAM_END,		0x013F, 0 },
	{ DRIVER_OUTPUT_CTRL_2,		0x2700, 0 },
	{ BASE_IMG_DISPLAY_CTRL,	0x0001, 0 },
	{ VERT_SCROLL_CTRL,		0x0000, 0 },
	{ PD1_DISPLAY_POS,		0x0000, 0 },
	{ PD1_RAM_START,		0x0000, 0 },
	{ PD1_RAM_END,			0x0000, 0 },
	{ PD2_DISPLAY_POS,		0x0000, 0 },
	{ PD2_RAM_START,		0x0000, 0 },
	{ PD2_RAM_END,			0x0000, 0 },
	{ PANEL_IFACE_CTRL_1,		0x0010, 0 },
	{ PANEL_IFACE_CTRL_2,		0x0000, 0 },
	{ PANEL_IFACE_CTRL_4,		0x0210, 0 },
	{ PANEL_IFACE_CTRL_5,		0x0000, 0 },
	{ DISPLAY_CTRL_1,		0x0133, 0 },
};

static struct otm3225a_spi_instruction display_enable_rgb_interface[] = {
	{ ENTRY_MODE,			0x1080, 0 },
	{ GRAM_ADDR_HORIZ_SET,		0x0000, 0 },
	{ GRAM_ADDR_VERT_SET,		0x0000, 0 },
	{ EXT_DISP_IFACE_CTRL_1,	0x0111, 500 },
};

static struct otm3225a_spi_instruction display_off[] = {
	{ DISPLAY_CTRL_1,	0x0131, 100 },
	{ DISPLAY_CTRL_1,	0x0130, 100 },
	{ DISPLAY_CTRL_1,	0x0100, 0 },
	{ POWER_CTRL_1,		0x0280, 0 },
	{ POWER_CTRL_3,		0x018B, 0 },
};

static struct otm3225a_spi_instruction display_on[] = {
	{ POWER_CTRL_1,		0x1280, 0 },
	{ DISPLAY_CTRL_1,	0x0101, 100 },
	{ DISPLAY_CTRL_1,	0x0121, 0 },
	{ DISPLAY_CTRL_1,	0x0123, 100 },
	{ DISPLAY_CTRL_1,	0x0133, 10 },
};

static void otm3225a_write(struct spi_device *spi,
			   struct otm3225a_spi_instruction *instruction,
			   unsigned int count)
{
	unsigned char buf[3];

	while (count--) {
		/* address register using index register */
		buf[0] = OTM3225A_INDEX_REG;
		buf[1] = 0x00;
		buf[2] = instruction->reg;
		spi_write(spi, buf, 3);

		/* write data to addressed register */
		buf[0] = OTM3225A_DATA_REG;
		buf[1] = (instruction->value >> 8) & 0xff;
		buf[2] = instruction->value & 0xff;
		spi_write(spi, buf, 3);

		/* execute delay if any */
		if (instruction->delay)
			msleep(instruction->delay);
		instruction++;
	}
}

static int otm3225a_set_power(struct lcd_device *ld, int power)
{
	struct otm3225a_data *dd = lcd_get_data(ld);

	if (power == dd->power)
		return 0;

	if (power > FB_BLANK_UNBLANK)
		otm3225a_write(dd->spi, display_off, ARRAY_SIZE(display_off));
	else
		otm3225a_write(dd->spi, display_on, ARRAY_SIZE(display_on));
	dd->power = power;

	return 0;
}

static int otm3225a_get_power(struct lcd_device *ld)
{
	struct otm3225a_data *dd = lcd_get_data(ld);

	return dd->power;
}

static const struct lcd_ops otm3225a_ops = {
	.set_power = otm3225a_set_power,
	.get_power = otm3225a_get_power,
};

static int otm3225a_probe(struct spi_device *spi)
{
	struct otm3225a_data *dd;
	struct lcd_device *ld;
	struct device *dev = &spi->dev;

	dd = devm_kzalloc(dev, sizeof(struct otm3225a_data), GFP_KERNEL);
	if (dd == NULL)
		return -ENOMEM;

	ld = devm_lcd_device_register(dev, dev_name(dev), dev, dd,
				      &otm3225a_ops);
	if (IS_ERR(ld))
		return PTR_ERR(ld);

	dd->spi = spi;
	dd->ld = ld;
	dev_set_drvdata(dev, dd);

	dev_info(dev, "Initializing and switching to RGB interface");
	otm3225a_write(spi, display_init, ARRAY_SIZE(display_init));
	otm3225a_write(spi, display_enable_rgb_interface,
		       ARRAY_SIZE(display_enable_rgb_interface));
	return 0;
}

static struct spi_driver otm3225a_driver = {
	.driver = {
		.name = "otm3225a",
	},
	.probe = otm3225a_probe,
};

module_spi_driver(otm3225a_driver);

MODULE_AUTHOR("Felix Brack <fb@ltec.ch>");
MODULE_DESCRIPTION("OTM3225A TFT LCD driver");
MODULE_VERSION("1.0.0");
MODULE_LICENSE("GPL v2");
