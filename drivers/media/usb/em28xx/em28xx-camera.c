/*
   em28xx-camera.c - driver for Empia EM25xx/27xx/28xx USB video capture devices

   Copyright (C) 2009 Mauro Carvalho Chehab <mchehab@infradead.org>
   Copyright (C) 2013 Frank Schäfer <fschaefer.oss@googlemail.com>

   This program is free software; you can redistribute it and/or modify
   it under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 2 of the License, or
   (at your option) any later version.

   This program is distributed in the hope that it will be useful,
   but WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
   GNU General Public License for more details.

   You should have received a copy of the GNU General Public License
   along with this program; if not, write to the Free Software
   Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
*/

#include <linux/i2c.h>
#include <media/mt9v011.h>
#include <media/v4l2-common.h>

#include "em28xx.h"


/* FIXME: Should be replaced by a proper mt9m111 driver */
static int em28xx_initialize_mt9m111(struct em28xx *dev)
{
	int i;
	unsigned char regs[][3] = {
		{ 0x0d, 0x00, 0x01, },  /* reset and use defaults */
		{ 0x0d, 0x00, 0x00, },
		{ 0x0a, 0x00, 0x21, },
		{ 0x21, 0x04, 0x00, },  /* full readout speed, no row/col skipping */
	};

	for (i = 0; i < ARRAY_SIZE(regs); i++)
		i2c_master_send(&dev->i2c_client[dev->def_i2c_bus],
				&regs[i][0], 3);

	return 0;
}


/* FIXME: Should be replaced by a proper mt9m001 driver */
static int em28xx_initialize_mt9m001(struct em28xx *dev)
{
	int i;
	unsigned char regs[][3] = {
		{ 0x0d, 0x00, 0x01, },
		{ 0x0d, 0x00, 0x00, },
		{ 0x04, 0x05, 0x00, },	/* hres = 1280 */
		{ 0x03, 0x04, 0x00, },  /* vres = 1024 */
		{ 0x20, 0x11, 0x00, },
		{ 0x06, 0x00, 0x10, },
		{ 0x2b, 0x00, 0x24, },
		{ 0x2e, 0x00, 0x24, },
		{ 0x35, 0x00, 0x24, },
		{ 0x2d, 0x00, 0x20, },
		{ 0x2c, 0x00, 0x20, },
		{ 0x09, 0x0a, 0xd4, },
		{ 0x35, 0x00, 0x57, },
	};

	for (i = 0; i < ARRAY_SIZE(regs); i++)
		i2c_master_send(&dev->i2c_client[dev->def_i2c_bus],
				&regs[i][0], 3);

	return 0;
}


/*
 * This method works for webcams with Micron sensors
 */
int em28xx_detect_sensor(struct em28xx *dev)
{
	int ret;
	char *name;
	u8 reg;
	__be16 id_be;
	u16 id;

	/* Micron sensor detection */
	dev->i2c_client[dev->def_i2c_bus].addr = 0xba >> 1;
	reg = 0;
	i2c_master_send(&dev->i2c_client[dev->def_i2c_bus], &reg, 1);
	ret = i2c_master_recv(&dev->i2c_client[dev->def_i2c_bus],
			      (char *)&id_be, 2);
	if (ret != 2)
		return -EINVAL;

	id = be16_to_cpu(id_be);
	switch (id) {
	case 0x8232:		/* mt9v011 640x480 1.3 Mpix sensor */
	case 0x8243:		/* mt9v011 rev B 640x480 1.3 Mpix sensor */
		name = "mt9v011";
		dev->em28xx_sensor = EM28XX_MT9V011;
		break;
	case 0x143a:    /* MT9M111 as found in the ECS G200 */
		name = "mt9m111";
		dev->em28xx_sensor = EM28XX_MT9M111;
		break;
	case 0x8431:
		name = "mt9m001";
		dev->em28xx_sensor = EM28XX_MT9M001;
		break;
	default:
		em28xx_info("unknown Micron sensor detected: 0x%04x\n", id);
		return -EINVAL;
	}

	em28xx_info("sensor %s detected\n", name);

	return 0;
}

int em28xx_init_camera(struct em28xx *dev)
{
	switch (dev->em28xx_sensor) {
	case EM28XX_MT9V011:
	{
		struct mt9v011_platform_data pdata;
		struct i2c_board_info mt9v011_info = {
			.type = "mt9v011",
			.addr = dev->i2c_client[dev->def_i2c_bus].addr,
			.platform_data = &pdata,
		};

		dev->sensor_xres = 640;
		dev->sensor_yres = 480;

		/*
		 * FIXME: mt9v011 uses I2S speed as xtal clk - at least with
		 * the Silvercrest cam I have here for testing - for higher
		 * resolutions, a high clock cause horizontal artifacts, so we
		 * need to use a lower xclk frequency.
		 * Yet, it would be possible to adjust xclk depending on the
		 * desired resolution, since this affects directly the
		 * frame rate.
		 */
		dev->board.xclk = EM28XX_XCLK_FREQUENCY_4_3MHZ;
		em28xx_write_reg(dev, EM28XX_R0F_XCLK, dev->board.xclk);
		dev->sensor_xtal = 4300000;
		pdata.xtal = dev->sensor_xtal;
		if (NULL ==
		    v4l2_i2c_new_subdev_board(&dev->v4l2_dev,
					      &dev->i2c_adap[dev->def_i2c_bus],
					      &mt9v011_info, NULL))
			return -ENODEV;
		/* probably means GRGB 16 bit bayer */
		dev->vinmode = 0x0d;
		dev->vinctl = 0x00;

		break;
	}
	case EM28XX_MT9M001:
		dev->sensor_xres = 1280;
		dev->sensor_yres = 1024;

		em28xx_initialize_mt9m001(dev);

		/* probably means BGGR 16 bit bayer */
		dev->vinmode = 0x0c;
		dev->vinctl = 0x00;

		break;
	case EM28XX_MT9M111:
		dev->sensor_xres = 640;
		dev->sensor_yres = 512;

		dev->board.xclk = EM28XX_XCLK_FREQUENCY_48MHZ;
		em28xx_write_reg(dev, EM28XX_R0F_XCLK, dev->board.xclk);
		em28xx_initialize_mt9m111(dev);

		dev->vinmode = 0x0a;
		dev->vinctl = 0x00;

		break;
	case EM28XX_NOSENSOR:
	default:
		return -EINVAL;
	}

	return 0;
}
