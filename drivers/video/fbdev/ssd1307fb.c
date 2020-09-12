// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * Driver for the Solomon SSD1307 OLED controller
 *
 * Copyright 2012 Free Electrons
 */

#include <linux/backlight.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/property.h>
#include <linux/pwm.h>
#include <linux/uaccess.h>
#include <linux/regulator/consumer.h>

#define SSD1307FB_DATA			0x40
#define SSD1307FB_COMMAND		0x80

#define SSD1307FB_SET_ADDRESS_MODE	0x20
#define SSD1307FB_SET_ADDRESS_MODE_HORIZONTAL	(0x00)
#define SSD1307FB_SET_ADDRESS_MODE_VERTICAL	(0x01)
#define SSD1307FB_SET_ADDRESS_MODE_PAGE		(0x02)
#define SSD1307FB_SET_COL_RANGE		0x21
#define SSD1307FB_SET_PAGE_RANGE	0x22
#define SSD1307FB_CONTRAST		0x81
#define SSD1307FB_SET_LOOKUP_TABLE	0x91
#define	SSD1307FB_CHARGE_PUMP		0x8d
#define SSD1307FB_SEG_REMAP_ON		0xa1
#define SSD1307FB_DISPLAY_OFF		0xae
#define SSD1307FB_SET_MULTIPLEX_RATIO	0xa8
#define SSD1307FB_DISPLAY_ON		0xaf
#define SSD1307FB_START_PAGE_ADDRESS	0xb0
#define SSD1307FB_SET_DISPLAY_OFFSET	0xd3
#define	SSD1307FB_SET_CLOCK_FREQ	0xd5
#define	SSD1307FB_SET_AREA_COLOR_MODE	0xd8
#define	SSD1307FB_SET_PRECHARGE_PERIOD	0xd9
#define	SSD1307FB_SET_COM_PINS_CONFIG	0xda
#define	SSD1307FB_SET_VCOMH		0xdb

#define MAX_CONTRAST 255

#define REFRESHRATE 1

static u_int refreshrate = REFRESHRATE;
module_param(refreshrate, uint, 0);

struct ssd1307fb_deviceinfo {
	u32 default_vcomh;
	u32 default_dclk_div;
	u32 default_dclk_frq;
	int need_pwm;
	int need_chargepump;
};

struct ssd1307fb_par {
	unsigned area_color_enable : 1;
	unsigned com_invdir : 1;
	unsigned com_lrremap : 1;
	unsigned com_seq : 1;
	unsigned lookup_table_set : 1;
	unsigned low_power : 1;
	unsigned seg_remap : 1;
	u32 com_offset;
	u32 contrast;
	u32 dclk_div;
	u32 dclk_frq;
	const struct ssd1307fb_deviceinfo *device_info;
	struct i2c_client *client;
	u32 height;
	struct fb_info *info;
	u8 lookup_table[4];
	u32 page_offset;
	u32 prechargep1;
	u32 prechargep2;
	struct pwm_device *pwm;
	struct gpio_desc *reset;
	struct regulator *vbat_reg;
	u32 vcomh;
	u32 width;
};

struct ssd1307fb_array {
	u8	type;
	u8	data[];
};

static const struct fb_fix_screeninfo ssd1307fb_fix = {
	.id		= "Solomon SSD1307",
	.type		= FB_TYPE_PACKED_PIXELS,
	.visual		= FB_VISUAL_MONO10,
	.xpanstep	= 0,
	.ypanstep	= 0,
	.ywrapstep	= 0,
	.accel		= FB_ACCEL_NONE,
};

static const struct fb_var_screeninfo ssd1307fb_var = {
	.bits_per_pixel	= 1,
	.red = { .length = 1 },
	.green = { .length = 1 },
	.blue = { .length = 1 },
};

static struct ssd1307fb_array *ssd1307fb_alloc_array(u32 len, u8 type)
{
	struct ssd1307fb_array *array;

	array = kzalloc(sizeof(struct ssd1307fb_array) + len, GFP_KERNEL);
	if (!array)
		return NULL;

	array->type = type;

	return array;
}

static int ssd1307fb_write_array(struct i2c_client *client,
				 struct ssd1307fb_array *array, u32 len)
{
	int ret;

	len += sizeof(struct ssd1307fb_array);

	ret = i2c_master_send(client, (u8 *)array, len);
	if (ret != len) {
		dev_err(&client->dev, "Couldn't send I2C command.\n");
		return ret;
	}

	return 0;
}

static inline int ssd1307fb_write_cmd(struct i2c_client *client, u8 cmd)
{
	struct ssd1307fb_array *array;
	int ret;

	array = ssd1307fb_alloc_array(1, SSD1307FB_COMMAND);
	if (!array)
		return -ENOMEM;

	array->data[0] = cmd;

	ret = ssd1307fb_write_array(client, array, 1);
	kfree(array);

	return ret;
}

static void ssd1307fb_update_display(struct ssd1307fb_par *par)
{
	struct ssd1307fb_array *array;
	u8 *vmem = par->info->screen_buffer;
	unsigned int line_length = par->info->fix.line_length;
	unsigned int pages = DIV_ROUND_UP(par->height, 8);
	int i, j, k;

	array = ssd1307fb_alloc_array(par->width * pages, SSD1307FB_DATA);
	if (!array)
		return;

	/*
	 * The screen is divided in pages, each having a height of 8
	 * pixels, and the width of the screen. When sending a byte of
	 * data to the controller, it gives the 8 bits for the current
	 * column. I.e, the first byte are the 8 bits of the first
	 * column, then the 8 bits for the second column, etc.
	 *
	 *
	 * Representation of the screen, assuming it is 5 bits
	 * wide. Each letter-number combination is a bit that controls
	 * one pixel.
	 *
	 * A0 A1 A2 A3 A4
	 * B0 B1 B2 B3 B4
	 * C0 C1 C2 C3 C4
	 * D0 D1 D2 D3 D4
	 * E0 E1 E2 E3 E4
	 * F0 F1 F2 F3 F4
	 * G0 G1 G2 G3 G4
	 * H0 H1 H2 H3 H4
	 *
	 * If you want to update this screen, you need to send 5 bytes:
	 *  (1) A0 B0 C0 D0 E0 F0 G0 H0
	 *  (2) A1 B1 C1 D1 E1 F1 G1 H1
	 *  (3) A2 B2 C2 D2 E2 F2 G2 H2
	 *  (4) A3 B3 C3 D3 E3 F3 G3 H3
	 *  (5) A4 B4 C4 D4 E4 F4 G4 H4
	 */

	for (i = 0; i < pages; i++) {
		for (j = 0; j < par->width; j++) {
			int m = 8;
			u32 array_idx = i * par->width + j;
			array->data[array_idx] = 0;
			/* Last page may be partial */
			if (i + 1 == pages && par->height % 8)
				m = par->height % 8;
			for (k = 0; k < m; k++) {
				u8 byte = vmem[(8 * i + k) * line_length +
					       j / 8];
				u8 bit = (byte >> (j % 8)) & 1;
				array->data[array_idx] |= bit << k;
			}
		}
	}

	ssd1307fb_write_array(par->client, array, par->width * pages);
	kfree(array);
}


static ssize_t ssd1307fb_write(struct fb_info *info, const char __user *buf,
		size_t count, loff_t *ppos)
{
	struct ssd1307fb_par *par = info->par;
	unsigned long total_size;
	unsigned long p = *ppos;
	void *dst;

	total_size = info->fix.smem_len;

	if (p > total_size)
		return -EINVAL;

	if (count + p > total_size)
		count = total_size - p;

	if (!count)
		return -EINVAL;

	dst = info->screen_buffer + p;

	if (copy_from_user(dst, buf, count))
		return -EFAULT;

	ssd1307fb_update_display(par);

	*ppos += count;

	return count;
}

static int ssd1307fb_blank(int blank_mode, struct fb_info *info)
{
	struct ssd1307fb_par *par = info->par;

	if (blank_mode != FB_BLANK_UNBLANK)
		return ssd1307fb_write_cmd(par->client, SSD1307FB_DISPLAY_OFF);
	else
		return ssd1307fb_write_cmd(par->client, SSD1307FB_DISPLAY_ON);
}

static void ssd1307fb_fillrect(struct fb_info *info, const struct fb_fillrect *rect)
{
	struct ssd1307fb_par *par = info->par;
	sys_fillrect(info, rect);
	ssd1307fb_update_display(par);
}

static void ssd1307fb_copyarea(struct fb_info *info, const struct fb_copyarea *area)
{
	struct ssd1307fb_par *par = info->par;
	sys_copyarea(info, area);
	ssd1307fb_update_display(par);
}

static void ssd1307fb_imageblit(struct fb_info *info, const struct fb_image *image)
{
	struct ssd1307fb_par *par = info->par;
	sys_imageblit(info, image);
	ssd1307fb_update_display(par);
}

static const struct fb_ops ssd1307fb_ops = {
	.owner		= THIS_MODULE,
	.fb_read	= fb_sys_read,
	.fb_write	= ssd1307fb_write,
	.fb_blank	= ssd1307fb_blank,
	.fb_fillrect	= ssd1307fb_fillrect,
	.fb_copyarea	= ssd1307fb_copyarea,
	.fb_imageblit	= ssd1307fb_imageblit,
};

static void ssd1307fb_deferred_io(struct fb_info *info,
				struct list_head *pagelist)
{
	ssd1307fb_update_display(info->par);
}

static int ssd1307fb_init(struct ssd1307fb_par *par)
{
	struct pwm_state pwmstate;
	int ret;
	u32 precharge, dclk, com_invdir, compins;

	if (par->device_info->need_pwm) {
		par->pwm = pwm_get(&par->client->dev, NULL);
		if (IS_ERR(par->pwm)) {
			dev_err(&par->client->dev, "Could not get PWM from device tree!\n");
			return PTR_ERR(par->pwm);
		}

		pwm_init_state(par->pwm, &pwmstate);
		pwm_set_relative_duty_cycle(&pwmstate, 50, 100);
		pwm_apply_state(par->pwm, &pwmstate);

		/* Enable the PWM */
		pwm_enable(par->pwm);

		dev_dbg(&par->client->dev, "Using PWM%d with a %lluns period.\n",
			par->pwm->pwm, pwm_get_period(par->pwm));
	}

	/* Set initial contrast */
	ret = ssd1307fb_write_cmd(par->client, SSD1307FB_CONTRAST);
	if (ret < 0)
		return ret;

	ret = ssd1307fb_write_cmd(par->client, par->contrast);
	if (ret < 0)
		return ret;

	/* Set segment re-map */
	if (par->seg_remap) {
		ret = ssd1307fb_write_cmd(par->client, SSD1307FB_SEG_REMAP_ON);
		if (ret < 0)
			return ret;
	}

	/* Set COM direction */
	com_invdir = 0xc0 | par->com_invdir << 3;
	ret = ssd1307fb_write_cmd(par->client,  com_invdir);
	if (ret < 0)
		return ret;

	/* Set multiplex ratio value */
	ret = ssd1307fb_write_cmd(par->client, SSD1307FB_SET_MULTIPLEX_RATIO);
	if (ret < 0)
		return ret;

	ret = ssd1307fb_write_cmd(par->client, par->height - 1);
	if (ret < 0)
		return ret;

	/* set display offset value */
	ret = ssd1307fb_write_cmd(par->client, SSD1307FB_SET_DISPLAY_OFFSET);
	if (ret < 0)
		return ret;

	ret = ssd1307fb_write_cmd(par->client, par->com_offset);
	if (ret < 0)
		return ret;

	/* Set clock frequency */
	ret = ssd1307fb_write_cmd(par->client, SSD1307FB_SET_CLOCK_FREQ);
	if (ret < 0)
		return ret;

	dclk = ((par->dclk_div - 1) & 0xf) | (par->dclk_frq & 0xf) << 4;
	ret = ssd1307fb_write_cmd(par->client, dclk);
	if (ret < 0)
		return ret;

	/* Set Set Area Color Mode ON/OFF & Low Power Display Mode */
	if (par->area_color_enable || par->low_power) {
		u32 mode;

		ret = ssd1307fb_write_cmd(par->client,
					  SSD1307FB_SET_AREA_COLOR_MODE);
		if (ret < 0)
			return ret;

		mode = (par->area_color_enable ? 0x30 : 0) |
			(par->low_power ? 5 : 0);
		ret = ssd1307fb_write_cmd(par->client, mode);
		if (ret < 0)
			return ret;
	}

	/* Set precharge period in number of ticks from the internal clock */
	ret = ssd1307fb_write_cmd(par->client, SSD1307FB_SET_PRECHARGE_PERIOD);
	if (ret < 0)
		return ret;

	precharge = (par->prechargep1 & 0xf) | (par->prechargep2 & 0xf) << 4;
	ret = ssd1307fb_write_cmd(par->client, precharge);
	if (ret < 0)
		return ret;

	/* Set COM pins configuration */
	ret = ssd1307fb_write_cmd(par->client, SSD1307FB_SET_COM_PINS_CONFIG);
	if (ret < 0)
		return ret;

	compins = 0x02 | !par->com_seq << 4 | par->com_lrremap << 5;
	ret = ssd1307fb_write_cmd(par->client, compins);
	if (ret < 0)
		return ret;

	/* Set VCOMH */
	ret = ssd1307fb_write_cmd(par->client, SSD1307FB_SET_VCOMH);
	if (ret < 0)
		return ret;

	ret = ssd1307fb_write_cmd(par->client, par->vcomh);
	if (ret < 0)
		return ret;

	/* Turn on the DC-DC Charge Pump */
	ret = ssd1307fb_write_cmd(par->client, SSD1307FB_CHARGE_PUMP);
	if (ret < 0)
		return ret;

	ret = ssd1307fb_write_cmd(par->client,
		BIT(4) | (par->device_info->need_chargepump ? BIT(2) : 0));
	if (ret < 0)
		return ret;

	/* Set lookup table */
	if (par->lookup_table_set) {
		int i;

		ret = ssd1307fb_write_cmd(par->client,
					  SSD1307FB_SET_LOOKUP_TABLE);
		if (ret < 0)
			return ret;

		for (i = 0; i < ARRAY_SIZE(par->lookup_table); ++i) {
			u8 val = par->lookup_table[i];

			if (val < 31 || val > 63)
				dev_warn(&par->client->dev,
					 "lookup table index %d value out of range 31 <= %d <= 63\n",
					 i, val);
			ret = ssd1307fb_write_cmd(par->client, val);
			if (ret < 0)
				return ret;
		}
	}

	/* Switch to horizontal addressing mode */
	ret = ssd1307fb_write_cmd(par->client, SSD1307FB_SET_ADDRESS_MODE);
	if (ret < 0)
		return ret;

	ret = ssd1307fb_write_cmd(par->client,
				  SSD1307FB_SET_ADDRESS_MODE_HORIZONTAL);
	if (ret < 0)
		return ret;

	/* Set column range */
	ret = ssd1307fb_write_cmd(par->client, SSD1307FB_SET_COL_RANGE);
	if (ret < 0)
		return ret;

	ret = ssd1307fb_write_cmd(par->client, 0x0);
	if (ret < 0)
		return ret;

	ret = ssd1307fb_write_cmd(par->client, par->width - 1);
	if (ret < 0)
		return ret;

	/* Set page range */
	ret = ssd1307fb_write_cmd(par->client, SSD1307FB_SET_PAGE_RANGE);
	if (ret < 0)
		return ret;

	ret = ssd1307fb_write_cmd(par->client, par->page_offset);
	if (ret < 0)
		return ret;

	ret = ssd1307fb_write_cmd(par->client,
				  par->page_offset +
				  DIV_ROUND_UP(par->height, 8) - 1);
	if (ret < 0)
		return ret;

	/* Clear the screen */
	ssd1307fb_update_display(par);

	/* Turn on the display */
	ret = ssd1307fb_write_cmd(par->client, SSD1307FB_DISPLAY_ON);
	if (ret < 0)
		return ret;

	return 0;
}

static int ssd1307fb_update_bl(struct backlight_device *bdev)
{
	struct ssd1307fb_par *par = bl_get_data(bdev);
	int ret;
	int brightness = bdev->props.brightness;

	par->contrast = brightness;

	ret = ssd1307fb_write_cmd(par->client, SSD1307FB_CONTRAST);
	if (ret < 0)
		return ret;
	ret = ssd1307fb_write_cmd(par->client, par->contrast);
	if (ret < 0)
		return ret;
	return 0;
}

static int ssd1307fb_get_brightness(struct backlight_device *bdev)
{
	struct ssd1307fb_par *par = bl_get_data(bdev);

	return par->contrast;
}

static int ssd1307fb_check_fb(struct backlight_device *bdev,
				   struct fb_info *info)
{
	return (info->bl_dev == bdev);
}

static const struct backlight_ops ssd1307fb_bl_ops = {
	.options	= BL_CORE_SUSPENDRESUME,
	.update_status	= ssd1307fb_update_bl,
	.get_brightness	= ssd1307fb_get_brightness,
	.check_fb	= ssd1307fb_check_fb,
};

static struct ssd1307fb_deviceinfo ssd1307fb_ssd1305_deviceinfo = {
	.default_vcomh = 0x34,
	.default_dclk_div = 1,
	.default_dclk_frq = 7,
};

static struct ssd1307fb_deviceinfo ssd1307fb_ssd1306_deviceinfo = {
	.default_vcomh = 0x20,
	.default_dclk_div = 1,
	.default_dclk_frq = 8,
	.need_chargepump = 1,
};

static struct ssd1307fb_deviceinfo ssd1307fb_ssd1307_deviceinfo = {
	.default_vcomh = 0x20,
	.default_dclk_div = 2,
	.default_dclk_frq = 12,
	.need_pwm = 1,
};

static struct ssd1307fb_deviceinfo ssd1307fb_ssd1309_deviceinfo = {
	.default_vcomh = 0x34,
	.default_dclk_div = 1,
	.default_dclk_frq = 10,
};

static const struct of_device_id ssd1307fb_of_match[] = {
	{
		.compatible = "solomon,ssd1305fb-i2c",
		.data = (void *)&ssd1307fb_ssd1305_deviceinfo,
	},
	{
		.compatible = "solomon,ssd1306fb-i2c",
		.data = (void *)&ssd1307fb_ssd1306_deviceinfo,
	},
	{
		.compatible = "solomon,ssd1307fb-i2c",
		.data = (void *)&ssd1307fb_ssd1307_deviceinfo,
	},
	{
		.compatible = "solomon,ssd1309fb-i2c",
		.data = (void *)&ssd1307fb_ssd1309_deviceinfo,
	},
	{},
};
MODULE_DEVICE_TABLE(of, ssd1307fb_of_match);

static int ssd1307fb_probe(struct i2c_client *client)
{
	struct device *dev = &client->dev;
	struct backlight_device *bl;
	char bl_name[12];
	struct fb_info *info;
	struct fb_deferred_io *ssd1307fb_defio;
	u32 vmem_size;
	struct ssd1307fb_par *par;
	void *vmem;
	int ret;

	info = framebuffer_alloc(sizeof(struct ssd1307fb_par), dev);
	if (!info)
		return -ENOMEM;

	par = info->par;
	par->info = info;
	par->client = client;

	par->device_info = device_get_match_data(dev);

	par->reset = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_LOW);
	if (IS_ERR(par->reset)) {
		dev_err(dev, "failed to get reset gpio: %ld\n",
			PTR_ERR(par->reset));
		ret = PTR_ERR(par->reset);
		goto fb_alloc_error;
	}

	par->vbat_reg = devm_regulator_get_optional(dev, "vbat");
	if (IS_ERR(par->vbat_reg)) {
		ret = PTR_ERR(par->vbat_reg);
		if (ret == -ENODEV) {
			par->vbat_reg = NULL;
		} else {
			dev_err(dev, "failed to get VBAT regulator: %d\n", ret);
			goto fb_alloc_error;
		}
	}

	if (device_property_read_u32(dev, "solomon,width", &par->width))
		par->width = 96;

	if (device_property_read_u32(dev, "solomon,height", &par->height))
		par->height = 16;

	if (device_property_read_u32(dev, "solomon,page-offset", &par->page_offset))
		par->page_offset = 1;

	if (device_property_read_u32(dev, "solomon,com-offset", &par->com_offset))
		par->com_offset = 0;

	if (device_property_read_u32(dev, "solomon,prechargep1", &par->prechargep1))
		par->prechargep1 = 2;

	if (device_property_read_u32(dev, "solomon,prechargep2", &par->prechargep2))
		par->prechargep2 = 2;

	if (!device_property_read_u8_array(dev, "solomon,lookup-table",
					   par->lookup_table,
					   ARRAY_SIZE(par->lookup_table)))
		par->lookup_table_set = 1;

	par->seg_remap = !device_property_read_bool(dev, "solomon,segment-no-remap");
	par->com_seq = device_property_read_bool(dev, "solomon,com-seq");
	par->com_lrremap = device_property_read_bool(dev, "solomon,com-lrremap");
	par->com_invdir = device_property_read_bool(dev, "solomon,com-invdir");
	par->area_color_enable =
		device_property_read_bool(dev, "solomon,area-color-enable");
	par->low_power = device_property_read_bool(dev, "solomon,low-power");

	par->contrast = 127;
	par->vcomh = par->device_info->default_vcomh;

	/* Setup display timing */
	if (device_property_read_u32(dev, "solomon,dclk-div", &par->dclk_div))
		par->dclk_div = par->device_info->default_dclk_div;
	if (device_property_read_u32(dev, "solomon,dclk-frq", &par->dclk_frq))
		par->dclk_frq = par->device_info->default_dclk_frq;

	vmem_size = DIV_ROUND_UP(par->width, 8) * par->height;

	vmem = (void *)__get_free_pages(GFP_KERNEL | __GFP_ZERO,
					get_order(vmem_size));
	if (!vmem) {
		dev_err(dev, "Couldn't allocate graphical memory.\n");
		ret = -ENOMEM;
		goto fb_alloc_error;
	}

	ssd1307fb_defio = devm_kzalloc(dev, sizeof(*ssd1307fb_defio),
				       GFP_KERNEL);
	if (!ssd1307fb_defio) {
		dev_err(dev, "Couldn't allocate deferred io.\n");
		ret = -ENOMEM;
		goto fb_alloc_error;
	}

	ssd1307fb_defio->delay = HZ / refreshrate;
	ssd1307fb_defio->deferred_io = ssd1307fb_deferred_io;

	info->fbops = &ssd1307fb_ops;
	info->fix = ssd1307fb_fix;
	info->fix.line_length = DIV_ROUND_UP(par->width, 8);
	info->fbdefio = ssd1307fb_defio;

	info->var = ssd1307fb_var;
	info->var.xres = par->width;
	info->var.xres_virtual = par->width;
	info->var.yres = par->height;
	info->var.yres_virtual = par->height;

	info->screen_buffer = vmem;
	info->fix.smem_start = __pa(vmem);
	info->fix.smem_len = vmem_size;

	fb_deferred_io_init(info);

	i2c_set_clientdata(client, info);

	if (par->reset) {
		/* Reset the screen */
		gpiod_set_value_cansleep(par->reset, 1);
		udelay(4);
		gpiod_set_value_cansleep(par->reset, 0);
		udelay(4);
	}

	if (par->vbat_reg) {
		ret = regulator_enable(par->vbat_reg);
		if (ret) {
			dev_err(dev, "failed to enable VBAT: %d\n", ret);
			goto reset_oled_error;
		}
	}

	ret = ssd1307fb_init(par);
	if (ret)
		goto regulator_enable_error;

	ret = register_framebuffer(info);
	if (ret) {
		dev_err(dev, "Couldn't register the framebuffer\n");
		goto panel_init_error;
	}

	snprintf(bl_name, sizeof(bl_name), "ssd1307fb%d", info->node);
	bl = backlight_device_register(bl_name, dev, par, &ssd1307fb_bl_ops,
				       NULL);
	if (IS_ERR(bl)) {
		ret = PTR_ERR(bl);
		dev_err(dev, "unable to register backlight device: %d\n", ret);
		goto bl_init_error;
	}

	bl->props.brightness = par->contrast;
	bl->props.max_brightness = MAX_CONTRAST;
	info->bl_dev = bl;

	dev_info(dev, "fb%d: %s framebuffer device registered, using %d bytes of video memory\n", info->node, info->fix.id, vmem_size);

	return 0;

bl_init_error:
	unregister_framebuffer(info);
panel_init_error:
	if (par->device_info->need_pwm) {
		pwm_disable(par->pwm);
		pwm_put(par->pwm);
	}
regulator_enable_error:
	if (par->vbat_reg)
		regulator_disable(par->vbat_reg);
reset_oled_error:
	fb_deferred_io_cleanup(info);
fb_alloc_error:
	framebuffer_release(info);
	return ret;
}

static int ssd1307fb_remove(struct i2c_client *client)
{
	struct fb_info *info = i2c_get_clientdata(client);
	struct ssd1307fb_par *par = info->par;

	ssd1307fb_write_cmd(par->client, SSD1307FB_DISPLAY_OFF);

	backlight_device_unregister(info->bl_dev);

	unregister_framebuffer(info);
	if (par->device_info->need_pwm) {
		pwm_disable(par->pwm);
		pwm_put(par->pwm);
	}
	if (par->vbat_reg)
		regulator_disable(par->vbat_reg);
	fb_deferred_io_cleanup(info);
	__free_pages(__va(info->fix.smem_start), get_order(info->fix.smem_len));
	framebuffer_release(info);

	return 0;
}

static const struct i2c_device_id ssd1307fb_i2c_id[] = {
	{ "ssd1305fb", 0 },
	{ "ssd1306fb", 0 },
	{ "ssd1307fb", 0 },
	{ "ssd1309fb", 0 },
	{ }
};
MODULE_DEVICE_TABLE(i2c, ssd1307fb_i2c_id);

static struct i2c_driver ssd1307fb_driver = {
	.probe_new = ssd1307fb_probe,
	.remove = ssd1307fb_remove,
	.id_table = ssd1307fb_i2c_id,
	.driver = {
		.name = "ssd1307fb",
		.of_match_table = ssd1307fb_of_match,
	},
};

module_i2c_driver(ssd1307fb_driver);

MODULE_DESCRIPTION("FB driver for the Solomon SSD1307 OLED controller");
MODULE_AUTHOR("Maxime Ripard <maxime.ripard@free-electrons.com>");
MODULE_LICENSE("GPL");
