/*
 * Copyright (C) 2010-2015 Freescale Semiconductor, Inc. All Rights Reserved.
 */

/*
 * The code contained herein is licensed under the GNU General Public
 * License. You may obtain a copy of the GNU General Public License
 * Version 2 or later at the following locations:
 *
 * http://www.opensource.org/licenses/gpl-license.html
 * http://www.gnu.org/copyleft/gpl.html
 */

/*!
 * @defgroup Framebuffer Framebuffer Driver for Sii902x.
 */

/*!
 * @file mxsfb_sii902x.c
 *
 * @brief  Frame buffer driver for SII902x
 *
 * @ingroup Framebuffer
 */

/*!
 * Include files
 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/console.h>
#include <linux/delay.h>
#include <linux/errno.h>
#include <linux/fb.h>
#include <linux/init.h>
#include <linux/platform_device.h>
#include <linux/regulator/consumer.h>
#include <linux/i2c.h>
#include <linux/fsl_devices.h>
#include <linux/interrupt.h>
#include <linux/reset.h>
#include <asm/mach-types.h>
#include <video/mxc_edid.h>

#define SII_EDID_LEN	512
#define DRV_NAME "sii902x"

struct sii902x_data {
	struct i2c_client *client;
	struct delayed_work det_work;
	struct fb_info *fbi;
	struct mxc_edid_cfg edid_cfg;
	u8 cable_plugin;
	u8 edid[SII_EDID_LEN];
	bool dft_mode_set;
	const char *mode_str;
	int bits_per_pixel;
} sii902x;

static void sii902x_poweron(void);
static void sii902x_poweroff(void);

#ifdef DEBUG
static void dump_fb_videomode(struct fb_videomode *m)
{
	pr_debug("fb_videomode = %d %d %d %d %d %d %d %d %d %d %d %d %d\n",
		m->refresh, m->xres, m->yres, m->pixclock, m->left_margin,
		m->right_margin, m->upper_margin, m->lower_margin,
		m->hsync_len, m->vsync_len, m->sync, m->vmode, m->flag);
}
#else
static void dump_fb_videomode(struct fb_videomode *m)
{}
#endif

static __attribute__ ((unused)) void dump_regs(u8 reg, int len)
{
	u8 buf[50];
	int i;

	i2c_smbus_read_i2c_block_data(sii902x.client, reg, len, buf);
	for (i = 0; i < len; i++)
		dev_dbg(&sii902x.client->dev, "reg[0x%02X]: 0x%02X\n",
				i+reg, buf[i]);
}

static ssize_t sii902x_show_name(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	strcpy(buf, sii902x.fbi->fix.id);
	sprintf(buf+strlen(buf), "\n");

	return strlen(buf);
}

static DEVICE_ATTR(fb_name, S_IRUGO, sii902x_show_name, NULL);

static ssize_t sii902x_show_state(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	if (sii902x.cable_plugin == 0)
		strcpy(buf, "plugout\n");
	else
		strcpy(buf, "plugin\n");

	return strlen(buf);
}

static DEVICE_ATTR(cable_state, S_IRUGO, sii902x_show_state, NULL);

static ssize_t sii902x_show_edid(struct device *dev,
		struct device_attribute *attr, char *buf)
{
	int i, j, len = 0;

	for (j = 0; j < SII_EDID_LEN/16; j++) {
		for (i = 0; i < 16; i++)
			len += sprintf(buf+len, "0x%02X ",
					sii902x.edid[j*16 + i]);
		len += sprintf(buf+len, "\n");
	}

	return len;
}

static DEVICE_ATTR(edid, S_IRUGO, sii902x_show_edid, NULL);

static void sii902x_setup(struct fb_info *fbi)
{
	u16 data[4];
	u32 refresh;
	u8 *tmp;
	int i;

	dev_dbg(&sii902x.client->dev, "Sii902x: setup..\n");

	/* Power up */
	i2c_smbus_write_byte_data(sii902x.client, 0x1E, 0x00);

	/* set TPI video mode */
	data[0] = PICOS2KHZ(fbi->var.pixclock) / 10;
	data[2] = fbi->var.hsync_len + fbi->var.left_margin +
		  fbi->var.xres + fbi->var.right_margin;
	data[3] = fbi->var.vsync_len + fbi->var.upper_margin +
		  fbi->var.yres + fbi->var.lower_margin;
	refresh = data[2] * data[3];
	refresh = (PICOS2KHZ(fbi->var.pixclock) * 1000) / refresh;
	data[1] = refresh * 100;
	tmp = (u8 *)data;
	for (i = 0; i < 8; i++)
		i2c_smbus_write_byte_data(sii902x.client, i, tmp[i]);

	/* input bus/pixel: full pixel wide (24bit), rising edge */
	i2c_smbus_write_byte_data(sii902x.client, 0x08, 0x70);
	/* Set input format to RGB */
	i2c_smbus_write_byte_data(sii902x.client, 0x09, 0x00);
	/* set output format to RGB */
	i2c_smbus_write_byte_data(sii902x.client, 0x0A, 0x00);
}

static void sii902x_audio_setup(void)
{
	/* audio setup */
	i2c_smbus_write_byte_data(sii902x.client, 0x25, 0x00);
	i2c_smbus_write_byte_data(sii902x.client, 0x26, 0x40);
	i2c_smbus_write_byte_data(sii902x.client, 0x27, 0x00);
}

#ifdef CONFIG_FB_MODE_HELPERS
static int sii902x_read_edid(struct fb_info *fbi)
{
	int old, dat, ret, cnt = 100;
	unsigned short addr = 0x50;

	dev_dbg(&sii902x.client->dev, "%s\n", __func__);

	old = i2c_smbus_read_byte_data(sii902x.client, 0x1A);

	i2c_smbus_write_byte_data(sii902x.client, 0x1A, old | 0x4);
	do {
		cnt--;
		msleep(10);
		dat = i2c_smbus_read_byte_data(sii902x.client, 0x1A);
	} while ((!(dat & 0x2)) && cnt);

	if (!cnt) {
		ret = -1;
		goto done;
	}

	i2c_smbus_write_byte_data(sii902x.client, 0x1A, old | 0x06);

	/* edid reading */
	ret = mxc_edid_read(sii902x.client->adapter, addr,
				sii902x.edid, &sii902x.edid_cfg, fbi);

	cnt = 100;
	do {
		cnt--;
		i2c_smbus_write_byte_data(sii902x.client, 0x1A, old & ~0x6);
		msleep(10);
		dat = i2c_smbus_read_byte_data(sii902x.client, 0x1A);
	} while ((dat & 0x6) && cnt);

	if (!cnt)
		ret = -1;

done:

	i2c_smbus_write_byte_data(sii902x.client, 0x1A, old);
	return ret;
}
#else
static int sii902x_read_edid(struct fb_info *fbi)
{
	return -1;
}
#endif

static void sii902x_cable_connected(void)
{
	int i;
	const struct fb_videomode *mode;
	struct fb_videomode m;

	if (sii902x_read_edid(sii902x.fbi) < 0)
		dev_err(&sii902x.client->dev,
			"Sii902x: read edid fail\n");
	else {
		if (sii902x.fbi->monspecs.modedb_len > 0) {

			fb_destroy_modelist(&sii902x.fbi->modelist);

			for (i = 0; i < sii902x.fbi->monspecs.modedb_len; i++) {

				mode = &sii902x.fbi->monspecs.modedb[i];

				if (!(mode->vmode & FB_VMODE_INTERLACED)) {
					dev_dbg(&sii902x.client->dev, "Added mode %d:", i);
					dev_dbg(&sii902x.client->dev,
						"xres = %d, yres = %d, freq = %d, vmode = %d, flag = %d\n",
						mode->xres, mode->yres, mode->refresh,
						mode->vmode, mode->flag);

					fb_add_videomode(mode, &sii902x.fbi->modelist);
				}
			}

			/* Set the default mode only once. */
			if (!sii902x.dft_mode_set &&
					sii902x.mode_str && sii902x.bits_per_pixel) {

				dev_dbg(&sii902x.client->dev, "%s: setting to default=%s bpp=%d\n",
						__func__, sii902x.mode_str, sii902x.bits_per_pixel);

				fb_find_mode(&sii902x.fbi->var, sii902x.fbi,
						sii902x.mode_str, NULL, 0, NULL,
						sii902x.bits_per_pixel);

				sii902x.dft_mode_set = true;
			}

			fb_var_to_videomode(&m, &sii902x.fbi->var);
			dump_fb_videomode(&m);

			mode = fb_find_nearest_mode(&m,
					&sii902x.fbi->modelist);

			/* update fbi mode  */
			sii902x.fbi->mode = (struct fb_videomode *)mode;

			fb_videomode_to_var(&sii902x.fbi->var, mode);

			sii902x.fbi->var.activate |= FB_ACTIVATE_FORCE;
			console_lock();
			sii902x.fbi->flags |= FBINFO_MISC_USEREVENT;
			fb_set_var(sii902x.fbi, &sii902x.fbi->var);
			sii902x.fbi->flags &= ~FBINFO_MISC_USEREVENT;
			console_unlock();
		}
		/* Power on sii902x */
		sii902x_poweron();
	}
}

static void det_worker(struct work_struct *work)
{
	int dat;
	char event_string[16];
	char *envp[] = { event_string, NULL };

	dev_dbg(&sii902x.client->dev, "%s\n", __func__);

	dat = i2c_smbus_read_byte_data(sii902x.client, 0x3D);

	/* cable connection state */
	if (dat & 0x4) {
		sii902x.cable_plugin = 1;
		dev_dbg(&sii902x.client->dev, "EVENT=plugin\n");
		sprintf(event_string, "EVENT=plugin");
		sii902x_cable_connected();
	} else {
		sii902x.cable_plugin = 0;
		dev_dbg(&sii902x.client->dev, "EVENT=plugout\n");
		sprintf(event_string, "EVENT=plugout");
		/* Power off sii902x */
		sii902x_poweroff();
	}
	kobject_uevent_env(&sii902x.client->dev.kobj, KOBJ_CHANGE, envp);

	i2c_smbus_write_byte_data(sii902x.client, 0x3D, dat);

	dev_dbg(&sii902x.client->dev, "exit %s\n", __func__);

}

static irqreturn_t sii902x_detect_handler(int irq, void *data)
{
	if (sii902x.fbi)
		schedule_delayed_work(&(sii902x.det_work), msecs_to_jiffies(50));

	return IRQ_HANDLED;
}

static int sii902x_fb_event(struct notifier_block *nb, unsigned long val, void *v)
{
	struct fb_event *event = v;
	struct fb_info *fbi = event->info;

	dev_dbg(&sii902x.client->dev, "%s event=0x%lx\n", __func__, val);

	switch (val) {
	case FB_EVENT_FB_REGISTERED:
		if (sii902x.fbi == NULL)
			sii902x.fbi = fbi;
		/* Manually trigger a plugin/plugout interrupter to check cable state */
		schedule_delayed_work(&(sii902x.det_work), msecs_to_jiffies(50));

		fb_show_logo(fbi, 0);

		break;
	case FB_EVENT_MODE_CHANGE:
		sii902x_setup(fbi);
		break;
	case FB_EVENT_BLANK:
		if (*((int *)event->data) == FB_BLANK_UNBLANK) {
			dev_dbg(&sii902x.client->dev, "FB_BLANK_UNBLANK\n");
			sii902x_poweron();
		} else {
			dev_dbg(&sii902x.client->dev, "FB_BLANK_BLANK\n");
			sii902x_poweroff();
		}
		break;
	}
	return 0;
}

static struct notifier_block nb = {
	.notifier_call = sii902x_fb_event,
};

static int mxsfb_get_of_property(void)
{
	struct device_node *np = sii902x.client->dev.of_node;
	const char *mode_str;
	int bits_per_pixel, ret;

	ret = of_property_read_string(np, "mode_str", &mode_str);
	if (ret < 0) {
		dev_warn(&sii902x.client->dev, "get of property mode_str fail\n");
		return ret;
	}
	ret = of_property_read_u32(np, "bits-per-pixel", &bits_per_pixel);
	if (ret) {
		dev_warn(&sii902x.client->dev, "get of property bpp fail\n");
		return ret;
	}

	sii902x.mode_str = mode_str;
	sii902x.bits_per_pixel = bits_per_pixel;

	return ret;
}

static int sii902x_probe(struct i2c_client *client,
		const struct i2c_device_id *id)
{
	int i, dat, ret;
	struct fb_info edid_fbi;

	memset(&sii902x, 0, sizeof(sii902x));

	sii902x.client = client;

	dev_dbg(&sii902x.client->dev, "%s\n", __func__);;

	/* Reset sii902x */
	ret = device_reset(&sii902x.client->dev);
	if (ret)
		dev_warn(&sii902x.client->dev, "No reset pin found\n");

	/* Set 902x in hardware TPI mode on and jump out of D3 state */
	if (i2c_smbus_write_byte_data(sii902x.client, 0xc7, 0x00) < 0) {
		dev_err(&sii902x.client->dev,
			"Sii902x: cound not find device\n");
		return -ENODEV;
	}

	/* read device ID */
	for (i = 10; i > 0; i--) {
		dat = i2c_smbus_read_byte_data(sii902x.client, 0x1B);
		printk(KERN_DEBUG "Sii902x: read id = 0x%02X", dat);
		if (dat == 0xb0) {
			dat = i2c_smbus_read_byte_data(sii902x.client, 0x1C);
			printk(KERN_DEBUG "-0x%02X", dat);
			dat = i2c_smbus_read_byte_data(sii902x.client, 0x1D);
			printk(KERN_DEBUG "-0x%02X", dat);
			dat = i2c_smbus_read_byte_data(sii902x.client, 0x30);
			printk(KERN_DEBUG "-0x%02X\n", dat);
			break;
		}
	}
	if (i == 0) {
		dev_err(&sii902x.client->dev,
			"Sii902x: cound not find device\n");
		return -ENODEV;
	}

	/* enable hmdi audio */
	sii902x_audio_setup();

	/* try to read edid */
	ret = sii902x_read_edid(&edid_fbi);
	if (ret < 0)
		dev_warn(&sii902x.client->dev, "Can not read edid\n");

	if (sii902x.client->irq) {
		ret = request_irq(sii902x.client->irq, sii902x_detect_handler,
				IRQF_TRIGGER_FALLING,
				"SII902x_det", &sii902x);
		if (ret < 0)
			dev_warn(&sii902x.client->dev,
				"Sii902x: cound not request det irq %d\n",
				sii902x.client->irq);
		else {
			/*enable cable hot plug irq*/
			i2c_smbus_write_byte_data(sii902x.client, 0x3c, 0x01);
			INIT_DELAYED_WORK(&(sii902x.det_work), det_worker);
		}
		ret = device_create_file(&sii902x.client->dev, &dev_attr_fb_name);
		if (ret < 0)
			dev_warn(&sii902x.client->dev,
				"Sii902x: cound not create sys node for fb name\n");
		ret = device_create_file(&sii902x.client->dev, &dev_attr_cable_state);
		if (ret < 0)
			dev_warn(&sii902x.client->dev,
				"Sii902x: cound not create sys node for cable state\n");
		ret = device_create_file(&sii902x.client->dev, &dev_attr_edid);
		if (ret < 0)
			dev_warn(&sii902x.client->dev,
				"Sii902x: cound not create sys node for edid\n");

	}

	mxsfb_get_of_property();
	fb_register_client(&nb);

	return 0;
}

static int sii902x_remove(struct i2c_client *client)
{
	fb_unregister_client(&nb);
	sii902x_poweroff();

	return 0;
}

static void sii902x_poweron(void)
{
	/* Turn on DVI or HDMI */
	if (sii902x.edid_cfg.hdmi_cap)
		i2c_smbus_write_byte_data(sii902x.client, 0x1A, 0x01);
	else
		i2c_smbus_write_byte_data(sii902x.client, 0x1A, 0x00);
	return;
}

static void sii902x_poweroff(void)
{
	/* disable tmds before changing resolution */
	if (sii902x.edid_cfg.hdmi_cap)
		i2c_smbus_write_byte_data(sii902x.client, 0x1A, 0x11);
	else
		i2c_smbus_write_byte_data(sii902x.client, 0x1A, 0x10);

	return;
}

static const struct i2c_device_id sii902x_id[] = {
	{ DRV_NAME, 0},
	{ },
};
MODULE_DEVICE_TABLE(i2c, sii902x_id);

static const struct of_device_id sii902x_dt_ids[] = {
	{ .compatible = "SiI,sii902x", },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, sii902x_dt_ids);

static struct i2c_driver sii902x_driver = {
	.driver = {
		.name = DRV_NAME,
		.owner = THIS_MODULE,
		.of_match_table = sii902x_dt_ids,
	},
	.probe		= sii902x_probe,
	.remove		= sii902x_remove,
	.id_table	= sii902x_id,
};

module_i2c_driver(sii902x_driver);

MODULE_AUTHOR("Freescale Semiconductor, Inc.");
MODULE_DESCRIPTION("SII902x DVI/HDMI driver");
MODULE_LICENSE("GPL");
