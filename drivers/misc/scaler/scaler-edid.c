/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/fb.h>
#include <mach/gpio.h>
#include <mach/iomux.h>

//#include <linux/rk_screen.h>
#include <linux/rk_fb.h>
#include "../../video/edid.h"

#define DDC_ADDR			0x50  //read 0xa0 write 0xa1
#define DDC_I2C_RATE		100*1000
#define DEFAULT_MODE   3

#undef SDEBUG

#ifdef SDEBUG
#define SPRINTK(fmt, args...) printk(fmt,## args)
#else
#define SPRINTK(fmt, args...)
#endif

static struct scaler_ddc_dev
{
	unsigned char        *edid;
	struct i2c_client    *client;
	struct fb_monspecs   specs;
	struct list_head     modelist;
	struct fb_videomode  *mode;
}*ddev = NULL;

static const struct fb_videomode default_modedb[] = {
	//name				refresh		xres	yres	pixclock			h_bp	h_fp	v_bp	v_fp	h_pw	v_pw	polariry	PorI	flag(used for vic)
#if defined(CONFIG_CLK_RK30_BOX)
{	"1024x768p@60Hz",	60,			1024,	768,	KHZ2PICOS(65000),	160,	24,		29,		3,		136,	6,		0,			0,		0	},
{	"1280x720p@60Hz",	60,			1280,	720,	KHZ2PICOS(74250),	220,	110,	20,		5,		40,		5,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		0	},	
{	"1280x1024p@60Hz",	60,			1280,	1024,	KHZ2PICOS(108000),	248,	48,		38,		1,		112,	3,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		0	},
{	"1366x768p@60Hz",	60,			1366,	768,	KHZ2PICOS(85500),	213,	70,		24,		3,		143,	3,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		0	},
{	"1440x900p@60Hz",	60,			1440,	900,	KHZ2PICOS(106500),	232,	80,		25,		3,		152,	6,		FB_SYNC_VERT_HIGH_ACT,			0,		0	},
{	"1680x1050p@60Hz",	60,			1680,	1050,	KHZ2PICOS(146250),	280,	104,	30,		3,		176,	6,		FB_SYNC_VERT_HIGH_ACT,			0,		0	},
{	"1920x1080p@60Hz",	60,			1920,	1080,	KHZ2PICOS(148500),	148,	88,		36,		4,		44,		5,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		0	},
#else
//{	"640x480p@60Hz",	60,			640,	480,	KHZ2PICOS(25175),	48,		16,		33,		10,		96,		2,		0,			0,		1	},
{	"720x480p@60Hz",	60,			720,	480,	KHZ2PICOS(27000),	60,		16,		30,		9,		62,		6,		0,			0,		2	},
{	"720x576p@50Hz",	50,			720,	576,	KHZ2PICOS(27000),	68,		12,		39,		5,		64,		5,		0,			0,		17	},
{	"1280x720p@50Hz",	50,			1280,	720,	KHZ2PICOS(74250),	220,	440,	20,		5,		40,		5,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		19	},
{	"1280x720p@60Hz",	60,			1280,	720,	KHZ2PICOS(74250),	220,	110,	20,		5,		40,		5,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		4	},	
{	"1920x1080p@50Hz",	50,			1920,	1080,	KHZ2PICOS(148500),	148,	528,	36,		4,		44,		5,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		31	},
{	"1920x1080p@60Hz",	60,			1920,	1080,	KHZ2PICOS(148500),	148,	88,		36,		4,		44,		5,		FB_SYNC_HOR_HIGH_ACT | FB_SYNC_VERT_HIGH_ACT,			0,		16	},
#endif
};

static int scaler_mode2screen(const struct fb_videomode *modedb, struct rk29fb_screen *screen)
{
	if(modedb == NULL || screen == NULL)
		return -1;
		
	memset(screen, 0, sizeof(struct rk29fb_screen));
	/* screen type & face */
    screen->type = SCREEN_RGB;
    screen->face = OUT_P888;
	//screen->lvds_format = LVDS_8BIT_1;  //lvds data format
	
	/* Screen size */
	screen->x_res = modedb->xres;
    screen->y_res = modedb->yres;

    /* Timing */
    screen->pixclock = PICOS2KHZ(modedb->pixclock);
    screen->pixclock /= 250;
    screen->pixclock *= 250;
    screen->pixclock *= 1000;
    printk("  pixclock is %d\n", screen->pixclock);
	screen->lcdc_aclk = 300000000;

	screen->left_margin = modedb->left_margin ;
	screen->right_margin = modedb->right_margin;
	screen->hsync_len = modedb->hsync_len;
	screen->upper_margin = modedb->upper_margin ;
	screen->lower_margin = modedb->lower_margin;
	screen->vsync_len = modedb->vsync_len;
	/* Pin polarity */
	if(FB_SYNC_HOR_HIGH_ACT & modedb->sync)
		screen->pin_hsync = 1;
	else
		screen->pin_hsync = 0;

	if(FB_SYNC_VERT_HIGH_ACT & modedb->sync)
		screen->pin_vsync = 1;
	else
		screen->pin_vsync = 0;	

	screen->pin_den = 0;
	screen->pin_dclk = 1;

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

void set_vga_lcd_info(struct rk29fb_screen *screen, struct rk29lcd_info *lcd_info)
{
	scaler_mode2screen(&default_modedb[DEFAULT_MODE], screen);
}

static int scaler_edid_read(char *buf, int len)
{
	int rc;

	if (ddev == NULL || ddev->client == NULL)
		return -ENODEV;

	if (buf == NULL)
		return -ENOMEM;

	// Check ddc i2c communication is available or not.
	rc = i2c_master_reg8_recv(ddev->client, 0, buf, 6, DDC_I2C_RATE);
	if(rc == 6) {
		/************DEBUG*************/	
		SPRINTK("EDID HEAD: ");
		for (rc = 0; rc < 6; rc++)
			SPRINTK(" %#X", buf[rc]);
		SPRINTK("\n");
		/************DEBUG*************/	

		memset(buf, 0, len);
		// Read EDID.
		rc = i2c_master_reg8_recv(ddev->client, 0, buf, len, DDC_I2C_RATE);
		if(rc == len)
			return 0;
	}

	printk("unable to read EDID block.\n");
	return -EIO;
}

/*
 *read edid of monitor and parse it 
 */
static int scaler_parse_vga_edid(void)
{
	int i ;
	struct fb_monspecs *specs = NULL;
	if (ddev == NULL) {
		return -ENODEV;
	}else {
		specs = &ddev->specs;
		//free old edid
		if (ddev->edid) {
			kfree(ddev->edid);
			ddev->edid = NULL;
		}
		ddev->edid = kzalloc(EDID_LENGTH, GFP_KERNEL);
		if (!ddev->edid)
			return -ENOMEM;

		//read edid
		if (!scaler_edid_read(ddev->edid, EDID_LENGTH)) {
			//free old fb_monspecs
			if(specs->modedb)
				kfree(specs->modedb);
			memset(specs, 0, sizeof(struct fb_monspecs));
			//parse edid to fb_monspecs
			fb_edid_to_monspecs(ddev->edid, specs);

			/********** DEBUG ***********/
			SPRINTK("========================================\n");
			SPRINTK("Display Information (EDID)\n");
			SPRINTK("========================================\n");
			SPRINTK("   EDID Version %d.%d\n", (int) specs->version, (int) specs->revision);
			SPRINTK("   Serial Number: %s\n", specs->serial_no);
			SPRINTK("   ASCII Block: %s\n", specs->ascii);
			SPRINTK("   Monitor Name: %s\n", specs->monitor);
			SPRINTK("   Display Characteristics:\n");
			for (i = 0; i < specs->modedb_len; i++)
				SPRINTK("       %4d x %4d @%d [clk: %ldKHZ]\n", specs->modedb[i].xres, specs->modedb[i].yres,
								specs->modedb[i].refresh, PICOS2KHZ(specs->modedb[i].pixclock));
			SPRINTK("========================================\n");
			/********** DEBUG ***********/
		}else {
			return -EIO;
		}
	}
	printk("scaler-ddc: read and parse vga edid success.\n");
	return 0;
}

static void scaler_set_default_modelist(void)
{
	int i;
	struct fb_videomode *mode = NULL;
	struct list_head	*modelist  = &ddev->modelist;

	fb_destroy_modelist(modelist);

	for(i = 0; i < ARRAY_SIZE(default_modedb); i++)
	{
		mode = (struct fb_videomode *)&default_modedb[i];	
		fb_add_videomode(mode, modelist);
	}
}

/*
 * Check mode 1920x1080p@60Hz is in modedb or not.  
 * If exist, set it as output moe.
 * If not exist, try mode 1280x720p@60Hz.
 * If both mode not exist, try 720x480p@60Hz.
 */
static int scaler_check_mode(struct fb_videomode *mode)
{
	struct fb_monspecs	*specs = NULL;
	struct list_head	*modelist = NULL;
	struct fb_videomode *tmp_mode = NULL;
	unsigned int pixclock;
	int i;

	if (ddev == NULL)
		return -ENODEV;
	specs = &ddev->specs;
	modelist = &ddev->modelist;

	fb_destroy_modelist(modelist);

	if (mode) {

		for(i = 0; i < ARRAY_SIZE(default_modedb); i++) {
			tmp_mode = (struct fb_videomode *)&default_modedb[i];	
			pixclock = PICOS2KHZ(tmp_mode->pixclock);
			pixclock /= 250;
			pixclock *= 250;
			pixclock *= 1000;
			if( (pixclock <= specs->dclkmax) &&	
				(tmp_mode->xres <= mode->xres) && (tmp_mode->yres <= mode->yres) &&
				(tmp_mode->refresh <= specs->vfmax) && (tmp_mode->refresh >= specs->vfmin)
			  ) {
				fb_add_videomode(tmp_mode, modelist);
			}
		}
	}

	return 0;
}

/*
 * Find monitor prefered video mode. If not find, 
 * @specs init info of monitor
 */
struct fb_videomode *scaler_find_max_mode(void)
{
	struct fb_videomode *mode = NULL/*, *nearest_mode = NULL*/;
	struct fb_monspecs *specs = NULL;
	int i, pixclock;
	
	if (ddev == NULL)
		return NULL;
	specs = &ddev->specs;
	if(specs->modedb_len) {


		/* Get max resolution timing */
		mode = &specs->modedb[0];
		for (i = 0; i < specs->modedb_len; i++) {
			if(specs->modedb[i].xres > mode->xres)
				mode = &specs->modedb[i];
			else if( (specs->modedb[i].xres == mode->xres) && (specs->modedb[i].yres > mode->yres) )
				mode = &specs->modedb[i];
		}

		// For some monitor, the max pixclock read from EDID is smaller
		// than the clock of max resolution mode supported. We fix it.
		pixclock = PICOS2KHZ(mode->pixclock);
		pixclock /= 250;
		pixclock *= 250;
		pixclock *= 1000;
		if(pixclock == 148250000)
			pixclock = 148500000;
		if(pixclock > specs->dclkmax)
			specs->dclkmax = pixclock;

		printk("scaler-ddc: max mode %dx%d@%d[pixclock-%ld KHZ]\n", mode->xres, mode->yres,
				mode->refresh, PICOS2KHZ(mode->pixclock));
	}
	
	return mode;
}

static struct fb_videomode *scaler_find_best_mode(void)
{
	int res = -1;
	struct fb_videomode *mode = NULL, *best = NULL;

	res = scaler_parse_vga_edid();
	if (res == 0) {
		mode = scaler_find_max_mode();
		if (mode) {
			 scaler_check_mode(mode);
			 best = (struct fb_videomode *)fb_find_nearest_mode(mode, &ddev->modelist);
		}
	} else {
		printk("scaler-ddc: read and parse edid failed errno:%d.\n", res);
	}

	return best;
}

int scaler_switch_screen(struct fb_videomode *mode)
{
	struct rk29fb_screen screen;

	if (mode) {
		scaler_mode2screen(mode, &screen);
#ifdef CONFIG_ARCH_RK29
		return FB_Switch_Screen(&screen, 1);
#else
		return rk_fb_switch_screen(&screen, 1, 0);
#endif
	}
	printk("scaler-ddc:  fb_videomode is null\n");
	return -1;
}

int scaler_switch_default_screen(void)
{
	int res;
	struct fb_videomode *mode = NULL;
	
	if (ddev == NULL) {
		printk("scaler-ddc: No DDC Dev.\n");
		return -ENODEV;
	}

	mode = scaler_find_best_mode();
	if (mode) {
		printk("scaler-ddc: best mode %dx%d@%d[pixclock-%ld KHZ]\n", mode->xres, mode->yres,
				mode->refresh, PICOS2KHZ(mode->pixclock));
		ddev->mode = mode;
		res = scaler_switch_screen(mode);
	}else {
		res = -1;
		printk("scaler-ddc: Don't find best mode\n");
	}

	return res;
}
EXPORT_SYMBOL(scaler_switch_default_screen);

struct fb_videomode *scaler_get_cmode(void)
{
	struct fb_videomode *mode = NULL;
	if (ddev != NULL)
		mode = ddev->mode;
	return mode;
}
EXPORT_SYMBOL(scaler_get_cmode);


static int scaler_ddc_probe(struct i2c_client *client,const struct i2c_device_id *id)
{    

	if (!i2c_check_functionality(client->adapter, I2C_FUNC_I2C)) 
		return -ENODEV;

	ddev = kzalloc(sizeof(struct scaler_ddc_dev), GFP_KERNEL);
	if (ddev == NULL) 
		return -ENOMEM;
	
    INIT_LIST_HEAD(&ddev->modelist);
	ddev->client = client;
	ddev->mode = &default_modedb[DEFAULT_MODE];
	scaler_set_default_modelist();

	printk("%s:      success.\n", __func__);

	return 0;
}

static int __devexit scaler_ddc_remove(struct i2c_client *client)
{
	if(ddev->edid)
		kfree(ddev->edid);
	if (ddev->specs.modedb)
		kfree(ddev->specs.modedb);
	kfree(ddev);
	return 0;
}

static const struct i2c_device_id scaler_ddc_id[] = {
	{ "scaler_ddc", 0 },
	{ }
};

static struct i2c_driver scaler_ddc_driver  = {
    .driver = {
        .name  = "scaler_ddc",
        .owner = THIS_MODULE,
    },
    .probe     = scaler_ddc_probe,
    .remove    = scaler_ddc_remove,
    .id_table  = scaler_ddc_id,
};

static int __init scaler_ddc_init(void)
{
    return i2c_add_driver(&scaler_ddc_driver);
}

static void __exit scaler_ddc_exit(void)
{
    i2c_del_driver(&scaler_ddc_driver);
}

subsys_initcall(scaler_ddc_init);
module_exit(scaler_ddc_exit);


/************SYSFS  DEBUG  ***********/
void scaler_ddc_is_ok(void)
{
	int rc = -1;
	char buf[8];
	if (ddev != NULL) {
		rc = i2c_master_reg8_recv(ddev->client, 0, buf, 8, DDC_I2C_RATE);
		if(rc == 8) {
			if (buf[0] == 0x00 && buf[1] == 0xff && buf[2] == 0xff && buf[3] == 0xff &&
					buf[4] == 0xff && buf[5] == 0xff && buf[6] == 0xff && buf[7] == 0x00)
				printk("scaler-ddc:  is ok\n");
			else
				printk("scaler-ddc: io error");
		}else
			printk("scaler-ddc: i2c  error\n");
	}else
		printk("scaler-ddc:  unknown error\n");
}

void scaler_current_mode(void)
{
	if (ddev != NULL && ddev->mode != NULL) 
		printk("scaler-ddc: cmode %dx%d@%d\n", ddev->mode->xres, ddev->mode->yres,
				ddev->mode->refresh);
	else
		printk("scaler-ddc: unknown mode\n");
}

void scaler_test_read_vga_edid(void)
{
	int i = 0, res;
	struct fb_monspecs *specs = NULL;

	res = scaler_parse_vga_edid();
	if (res == 0) {
		specs = &ddev->specs;

		printk("========================================\n");
		printk("Display Information (EDID)\n");
		printk("========================================\n");
		printk("   EDID Version %d.%d\n", (int) specs->version, (int) specs->revision);
		printk("   Serial Number: %s\n", specs->serial_no);
		printk("   ASCII Block: %s\n", specs->ascii);
		printk("   Monitor Name: %s\n", specs->monitor);
		printk("   Display Characteristics:\n");
		for (i = 0; i < specs->modedb_len; i++)
			printk("       %4d x %4d @%d [clk: %ldKHZ]\n", specs->modedb[i].xres, specs->modedb[i].yres,
							specs->modedb[i].refresh, PICOS2KHZ(specs->modedb[i].pixclock));
		printk("========================================\n");

	}else{
		printk("scaler-ddc: read and parse failed errno %d\n", res);
	}
}
