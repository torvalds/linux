/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <linux/display-sys.h>
#include <linux/rk_screen.h>
#include <linux/rk_fb.h>
#ifdef CONFIG_OF
#include <linux/of.h>
#include <linux/of_gpio.h>
#endif

#include "../../edid.h"

#ifdef CONFIG_SWITCH
#include <linux/switch.h>
#endif
	

#define DDC_ADDR		0x50
#define DDC_I2C_RATE		100*1000
#define INVALID_GPIO		-1
#define GPIO_HIGH		1
#define GPIO_LOW		0
#define DISPLAY_SOURCE_LCDC0    0
#define DISPLAY_SOURCE_LCDC1    1

//static char *vgaenvent[] = {"INTERFACE=VGA", NULL}; 

static const struct fb_videomode rk29_mode[] = {
	//name			refresh		xres	yres	pixclock			h_bp	h_fp	v_bp	v_fp	h_pw	v_pw	polariry	PorI	flag(used for vic)
{	"1024x768p@60Hz",	60,		1024,	768,	KHZ2PICOS(65000),	160,	24,	29,	3,	136,	6,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		0	},
{	"1280x720p@60Hz",	60,		1280,	720,	KHZ2PICOS(74250),	220,	110,	20,	5,	40,	5,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		0	},	
{	"1280x1024p@60Hz",	60,		1280,	1024,	KHZ2PICOS(108000),	248,	48,	38,	1,	112,	3,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		0	},
{	"1366x768p@60Hz",	60,		1366,	768,	KHZ2PICOS(85500),	213,	70,	24,	3,	143,	3,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		0	},
{	"1440x900p@60Hz",	60,		1440,	900,	KHZ2PICOS(116500),	232,	80,	25,	3,	152,	6,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		0	},
{	"1680x1050p@60Hz",	60,		1680,	1050,	KHZ2PICOS(146250),	280,	104,	30,	3,	176,	6,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		0	},
{	"1920x1080p@60Hz",	60,		1920,	1080,	KHZ2PICOS(148500),	148,	88,	36,	4,	44,	5,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		0	},
};

struct rockchip_vga {
	struct device 		 *dev;	/*i2c device*/
	struct rk_display_device *ddev; /*display device*/
	struct i2c_client 	 *client;
	struct list_head	 modelist;
	struct fb_monspecs	 specs;
	struct rk_screen	 screen;
	int 			 indx;
	int 			 en_pin;
	int 			 en_val;
	int 			 lcdc_id;
#ifdef CONFIG_SWITCH
	struct switch_dev	 switch_vga;
#endif
};

static int i2c_master_reg8_recv(const struct i2c_client *client,
		const char reg, char *buf, int count, int scl_rate)
{
        struct i2c_adapter *adap=client->adapter;
        struct i2c_msg msgs[2];
        int ret;
        char reg_buf = reg;

        msgs[0].addr = client->addr;
        msgs[0].flags = client->flags;
        msgs[0].len = 1;
        msgs[0].buf = &reg_buf;
        msgs[0].scl_rate = scl_rate;

        msgs[1].addr = client->addr;
        msgs[1].flags = client->flags | I2C_M_RD;
        msgs[1].len = count;
        msgs[1].buf = (char *)buf;
        msgs[1].scl_rate = scl_rate;

        ret = i2c_transfer(adap, msgs, 2);

        return (ret == 2)? count : ret;
}



static unsigned char *rk29fb_ddc_read(struct i2c_client *client)
{
	int rc;
	unsigned char *buf = kzalloc(EDID_LENGTH, GFP_KERNEL);
	if (!buf) {
		dev_err(&client->dev, "unable to allocate memory for EDID\n");
		return NULL;
	}
	
	/*Check ddc i2c communication is available or not*/
	rc = i2c_master_reg8_recv(client, 0, buf, 6, DDC_I2C_RATE);
	if (rc == 6) {
		memset(buf, 0, EDID_LENGTH);
		rc = i2c_master_reg8_recv(client, 0, buf, EDID_LENGTH, DDC_I2C_RATE);
		if(rc == EDID_LENGTH)
			return buf;
	}

	dev_err(&client->dev, "unable to read EDID block.\n");
	kfree(buf);
	return NULL;
}

static int vga_mode2screen(struct fb_videomode *modedb, struct rk_screen *screen)
{
	if(modedb == NULL || screen == NULL)
		return -1;
		
	memset(screen, 0, sizeof(struct rk_screen));
	memcpy(&screen->mode, modedb, sizeof(*modedb));
	screen->mode.pixclock = PICOS2KHZ(screen->mode.pixclock);
	screen->mode.pixclock /= 250;
	screen->mode.pixclock *= 250;
	screen->mode.pixclock *= 1000;
	screen->xsize = screen->mode.xres;
	screen->ysize = screen->mode.yres;
	screen->overscan.left = 100;
	screen->overscan.top = 100;
	screen->overscan.right = 100;
	screen->overscan.bottom = 100;
	/* screen type & face */
	screen->type = SCREEN_RGB;
	screen->face = OUT_P888;

	screen->pin_vsync = (screen->mode.sync & FB_SYNC_VERT_HIGH_ACT) ? 1:0;
	screen->pin_hsync = (screen->mode.sync & FB_SYNC_HOR_HIGH_ACT) ? 1:0;
	screen->pin_den = 0;
	screen->pin_dclk = 0;

	/* Swap rule */
	screen->swap_rb = 0;
	screen->swap_rg = 0;
	screen->swap_gb = 0;
	screen->swap_delta = 0;
	screen->swap_dumy = 0;
	/* Operation function*/
	screen->init = NULL;
	screen->standby = NULL;
	return 0;
}


static int vga_switch_screen(struct rockchip_vga *vga, int indx)
{
	struct fb_videomode *mode = &vga->specs.modedb[indx];
	struct rk_screen *screen = &vga->screen;
	vga_mode2screen(mode, screen);
	rk_fb_switch_screen(screen, 1 ,vga->lcdc_id);
	vga->indx = indx;
	return 0;
	
	
}
static int vga_get_screen_info(struct rockchip_vga *vga)
{
	u8 *edid;
	int i;
	struct fb_monspecs *specs = &vga->specs;
	struct list_head *modelist = &vga->modelist;
	edid = rk29fb_ddc_read(vga->client);
	if (!edid) {
		dev_info(vga->dev, "get edid failed!\n");
		return -EINVAL;
	}
	fb_edid_to_monspecs(edid,specs);
	INIT_LIST_HEAD(modelist);
	for (i = 0; i < specs->modedb_len; i++) {
		fb_add_videomode(&specs->modedb[i], modelist);
		dev_dbg(vga->dev, "%4dx%4d@%d---dclk:%ld\n",
			specs->modedb[i].xres, specs->modedb[i].yres,
			specs->modedb[i].refresh,
			(PICOS2KHZ(specs->modedb[i].pixclock)/250)*250*1000);
	}
	return 0;
	
}



static int vga_get_modelist(struct rk_display_device *device,
			     struct list_head **modelist)
{
	struct rockchip_vga *vga = device->priv_data;
	*modelist = &vga->modelist;
	return 0;
}

static int vga_set_mode(struct rk_display_device *device,
			 struct fb_videomode *mode)
{
	struct rockchip_vga *vga = device->priv_data;
	struct rk_screen *screen = &vga->screen;
	vga_mode2screen(mode, screen);
	rk_fb_switch_screen(screen, 1 ,vga->lcdc_id);
	return 0;
}

static int vga_get_mode(struct rk_display_device *device,
			 struct fb_videomode *mode)
{
	//struct vga *vga = device->priv_data;
	//struct fb_videomode *vmode;

	return 0;
}

struct rk_display_ops vga_display_ops = {
	.getmodelist = vga_get_modelist,
	.setmode = vga_set_mode,
	.getmode = vga_get_mode,
};

static int vga_display_probe(struct rk_display_device *device, void *devdata)
{
	device->owner = THIS_MODULE;
	strcpy(device->type, "VGA");
	device->priority = DISPLAY_PRIORITY_VGA;
	device->priv_data = devdata;
	device->ops = &vga_display_ops;
	return 1;
}

static struct rk_display_driver display_vga = {
	.probe = vga_display_probe,
};



struct rk_display_device * vga_register_display_sysfs(struct rockchip_vga *vga)
{
	return rk_display_device_register(&display_vga, vga->dev, vga);
}

void vga_unregister_display_sysfs(struct rockchip_vga *vga)
{
	if (vga->ddev)
		rk_display_device_unregister(vga->ddev);
}


static int vga_i2c_probe(struct i2c_client *client,const struct i2c_device_id *id)
{    
	int ret;
	struct rockchip_vga *vga;
	struct device_node *np = client->dev.of_node;
	enum of_gpio_flags pwr_flags;

	if (!np) {
		dev_err(&client->dev, "no device node found!\n");
		return -EINVAL;
	} 

	vga = devm_kzalloc(&client->dev, sizeof(*vga), GFP_KERNEL);
	if (!vga) {
		dev_err(&client->dev, "allocate for vga failed!\n");
		return -ENOMEM;
	}

	vga->client = client;
	vga->dev = &client->dev;
	i2c_set_clientdata(client, vga);
	vga->ddev = vga_register_display_sysfs(vga);
	if (IS_ERR(vga->ddev))
		dev_warn(vga->dev, "Unable to create device for vga :%ld",
			PTR_ERR(vga->ddev));
	
	vga->en_pin = of_get_named_gpio_flags(np, "pwr_gpio", 0, &pwr_flags);
	if (!gpio_is_valid(vga->en_pin)) {
		dev_err(vga->dev, "failed to get pwr_gpio!\n");
		ret =  -EINVAL;
		goto err;
	}

	vga->en_val = (pwr_flags & OF_GPIO_ACTIVE_LOW) ? 0 : 1;
	vga->lcdc_id = DISPLAY_SOURCE_LCDC1;
	
	ret = devm_gpio_request(vga->dev, vga->en_pin, "pwr_pin");
	if(ret < 0) {
		dev_err(vga->dev, "request for pwr_pin failed!\n ");
		goto err;
	}
	gpio_direction_output(vga->en_pin, vga->en_val);
	ret = vga_get_screen_info(vga);
	if (ret < 0)
		goto err;
	vga_switch_screen(vga, 7);
	
	printk("VGA probe successful\n");
	return 0;
err:
	vga_unregister_display_sysfs(vga);
	return ret;
	
}


static int vga_i2c_remove(struct i2c_client *client)
{
	return 0;
}


#if defined(CONFIG_OF)
static struct of_device_id vga_dt_ids[] = {
	{.compatible = "rockchip,vga" },
	{ }
};
#endif

static const struct i2c_device_id vga_id[] = {
	{ "vga_i2c", 0 },
	{ }
};

static struct i2c_driver vga_i2c_driver  = {
	.driver = {
		.name  = "vga_i2c",
		.owner = THIS_MODULE,
#if defined(CONFIG_OF)
		.of_match_table = of_match_ptr(vga_dt_ids),
#endif
	},
	.probe		= &vga_i2c_probe,
	.remove		= &vga_i2c_remove,
	.id_table	= vga_id,
};

static int __init rockchip_vga_init(void)
{
    return i2c_add_driver(&vga_i2c_driver);
}

static void __exit rockchip_vga_exit(void)
{
    i2c_del_driver(&vga_i2c_driver);
}

module_init(rockchip_vga_init);
module_exit(rockchip_vga_exit);
