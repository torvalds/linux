/* linux/drivers/video/samsung/s3cfb_lg4591.c
 *
 * MIPI-DSI based lg4591 lcd panel driver.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
*/
//---------------------------------------------------------------------------------------------------
#include <linux/module.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/mutex.h>
#include <linux/wait.h>
#include <linux/ctype.h>
#include <linux/io.h>
#include <linux/delay.h>
#include <linux/irq.h>
#include <linux/interrupt.h>
#include <linux/gpio.h>
#include <linux/workqueue.h>
#include <linux/backlight.h>
#include <linux/lcd.h>
#include <plat/gpio-cfg.h>
#include <plat/regs-dsim.h>
#include <mach/dsim.h>
#include <mach/mipi_ddi.h>

//---------------------------------------------------------------------------------------------------
#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
#endif

#include "s5p-dsim.h"
#include "s3cfb.h"
#include "lg4591_param.h"

//---------------------------------------------------------------------------------------------------
// Debug mipi write (read write register & display)
//---------------------------------------------------------------------------------------------------
//#define DEBUG_MIPI_WR

//---------------------------------------------------------------------------------------------------
struct lcd_info {
	struct mutex			    lock;

	struct device			    *dev;
	struct lcd_device		    *ld;
	struct lcd_platform_data	*lcd_pd;
};

static struct mipi_ddi_platform_data *ddi_pd;

#ifdef CONFIG_HAS_EARLYSUSPEND
    static  struct lcd_info *g_lcd;
#endif

//---------------------------------------------------------------------------------------------------
#ifdef CONFIG_HAS_EARLYSUSPEND
    void lg4591_early_suspend  (void);
    void lg4591_late_resume    (void);
#endif

static  void    dsim_reg_dump   (void);
static  int     lg4591_set_link(void *pd, unsigned int dsim_base,
	                            unsigned char (*cmd_write) (unsigned int dsim_base, unsigned int data0,
                                                            unsigned int data1, unsigned int data2),
                        	    int (*cmd_read) (u32 reg_base, u8 addr, u16 count, u8 *buf));
static  int     lg4591_read            (struct lcd_info *lcd, const u8 addr, u16 count, u8 *buf);
static  int     lg4591_read_with_retry (struct lcd_info *lcd, const u8 addr, u16 count, u8 *buf, u8 retry_cnt);
static  int     lg4591_write           (struct lcd_info *lcd, const unsigned char *seq, int len);
static  int     lg4591_probe           (struct device *dev);

//---------------------------------------------------------------------------------------------------
void lg4591_lcd_init   (void) 
{
	struct  lcd_info *lcd = g_lcd;

#if defined(DEBUG_MIPI_WR)
    printk("%s\n", __func__);    dsim_reg_dump();
#endif

    mdelay(10);
    lg4591_write(lcd, DSI_CONFIG       ,    sizeof(DSI_CONFIG       ));
    lg4591_write(lcd, DISPLAY_MODE1    ,    sizeof(DISPLAY_MODE1    ));
    lg4591_write(lcd, DISPLAY_MODE2    ,    sizeof(DISPLAY_MODE2    ));
    lg4591_write(lcd, P_GAMMA_R_SETTING,    sizeof(P_GAMMA_R_SETTING));
    lg4591_write(lcd, N_GAMMA_R_SETTING,    sizeof(N_GAMMA_R_SETTING));
    lg4591_write(lcd, P_GAMMA_G_SETTING,    sizeof(P_GAMMA_G_SETTING));
    lg4591_write(lcd, N_GAMMA_G_SETTING,    sizeof(N_GAMMA_G_SETTING));
    lg4591_write(lcd, P_GAMMA_B_SETTING,    sizeof(P_GAMMA_B_SETTING));
    lg4591_write(lcd, N_GAMMA_B_SETTING,    sizeof(N_GAMMA_B_SETTING));
    lg4591_write(lcd, OSC_SETTING      ,    sizeof(OSC_SETTING      ));
    lg4591_write(lcd, POWER_SETTING3   ,    sizeof(POWER_SETTING3   ));
    lg4591_write(lcd, POWER_SETTING4   ,    sizeof(POWER_SETTING4   ));
    lg4591_write(lcd, POWER_SETTING7   ,    sizeof(POWER_SETTING7   ));
    lg4591_write(lcd, OTP2_SETTING     ,    sizeof(OTP2_SETTING     ));
    lg4591_write(lcd, DEEP_STANDBY_0   ,    sizeof(DEEP_STANDBY_0   ));
    lg4591_write(lcd, POWER_SETTING2_1 ,    sizeof(POWER_SETTING2_1 ));    mdelay(1);
    lg4591_write(lcd, POWER_SETTING2_2 ,    sizeof(POWER_SETTING2_2 ));    mdelay(1);
    lg4591_write(lcd, POWER_SETTING2_3 ,    sizeof(POWER_SETTING2_3 ));    mdelay(1);
    lg4591_write(lcd, EXIT_SLEEP,           sizeof(EXIT_SLEEP       ));    mdelay(1);
    lg4591_write(lcd, OTP2_SETTING2,        sizeof(OTP2_SETTING2    ));    mdelay(1);
    lg4591_write(lcd, DISPLAY_ON,           sizeof(DISPLAY_ON       ));    mdelay(1);
}
EXPORT_SYMBOL(lg4591_lcd_init);

//---------------------------------------------------------------------------------------------------
static  void    dsim_reg_dump   (void)
{
    unsigned int i;

	for (i = 0; i < 25; i++) {
		printk("[DSIM REG]0x11c8_00%02X = 0x%08X\n", (i*4), readl(ddi_pd->dsim_base + i*4));
	}
}

//---------------------------------------------------------------------------------------------------
static int lg4591_set_link(void *pd, unsigned int dsim_base,
	                        unsigned char (*cmd_write) (unsigned int dsim_base, unsigned int data0,
                                                        unsigned int data1, unsigned int data2),
                        	int (*cmd_read) (u32 reg_base, u8 addr, u16 count, u8 *buf))
{
	struct mipi_ddi_platform_data *temp_pd = NULL;

	temp_pd = (struct mipi_ddi_platform_data *) pd;
	if (temp_pd == NULL) {
		printk(KERN_ERR "mipi_ddi_platform_data is null.\n");
		return -EPERM;
	}

	ddi_pd = temp_pd;

	ddi_pd->dsim_base = dsim_base;

	if (cmd_write)
		ddi_pd->cmd_write = cmd_write;
	else
		printk(KERN_WARNING "cmd_write function is null.\n");

	if (cmd_read)
		ddi_pd->cmd_read = cmd_read;
	else
		printk(KERN_WARNING "cmd_read function is null.\n");

	return 0;
}

//---------------------------------------------------------------------------------------------------
static int lg4591_read(struct lcd_info *lcd, const u8 addr, u16 count, u8 *buf)
{
	int ret = 0;

	mutex_lock(&lcd->lock);

	if (ddi_pd->cmd_read)
		ret = ddi_pd->cmd_read(ddi_pd->dsim_base, addr, count, buf);

	mutex_unlock(&lcd->lock);

	return ret;
}

//---------------------------------------------------------------------------------------------------
static int lg4591_read_with_retry(struct lcd_info *lcd, const u8 addr, u16 count, u8 *buf, u8 retry_cnt)
{
	int ret = 0;

read_retry:
	ret = lg4591_read(lcd, addr, count, buf);
	if (!ret) {
		if (retry_cnt) {
			printk(KERN_WARNING "[WARN:LCD] %s : retry cnt : %d\n", __func__, retry_cnt);
			retry_cnt--;
			goto read_retry;
		} else
			printk(KERN_ERR "[ERROR:LCD] %s : 0x%02x read failed\n", __func__, addr);
	}

	return ret;
}

//---------------------------------------------------------------------------------------------------
static int lg4591_write(struct lcd_info *lcd, const unsigned char *seq, int len)
{
	int size;
	const unsigned char *wbuf;

	mutex_lock(&lcd->lock);

	size = len;
	wbuf = seq;

	if (size == 1)
		ddi_pd->cmd_write(ddi_pd->dsim_base, DCS_WR_NO_PARA, wbuf[0], 0);
	else if (size == 2)
		ddi_pd->cmd_write(ddi_pd->dsim_base, DCS_WR_1_PARA, wbuf[0], wbuf[1]);
	else
		ddi_pd->cmd_write(ddi_pd->dsim_base, DCS_LONG_WR, (unsigned int)wbuf, size);

	mutex_unlock(&lcd->lock);

#if defined(DEBUG_MIPI_WR)
    {
        unsigned char   rbuf[50], i;
        
        lg4591_read(lcd, wbuf[0], size -1, &rbuf[0]);
        
        if(size > 1)    {
            printk(" reg read 0x%02X :", wbuf[0]);

            for(i = 0; i < size -1; i++)    printk(" 0x%02X", rbuf[i]);
            
            printk("\n");
        }
    }
#endif    
    
	return 0;
}

//---------------------------------------------------------------------------------------------------
static int lg4591_probe(struct device *dev)
{
	int     ret = 0;
	struct  lcd_info *lcd;

	if (!(lcd = kzalloc(sizeof(struct lcd_info), GFP_KERNEL))) {
		printk("%s : failed to allocate for lcd\n", __func__);		ret = -ENOMEM;
		goto err_alloc;
	}

#ifdef CONFIG_HAS_EARLYSUSPEND
    // for early_suspend/late_resume
	g_lcd = lcd;
#endif	

	lcd->dev = dev;

	dev_set_drvdata(dev, lcd);

	mutex_init(&lcd->lock);

	printk("lg4591 lcd panel driver has been probed.\n");

	return 0;

err_alloc:
	return ret;
}

//---------------------------------------------------------------------------------------------------
//
// early_suspend/late_resume function call from s3cfb_main.c
//
//---------------------------------------------------------------------------------------------------
#ifdef CONFIG_HAS_EARLYSUSPEND

void lg4591_early_suspend(void)
{
    printk("%s\n", __func__);
}

//---------------------------------------------------------------------------------------------------
void lg4591_late_resume(void)
{
    printk("%s\n", __func__);
    lg4591_lcd_init();
}

#endif
//---------------------------------------------------------------------------------------------------
static int __devexit lg4591_remove(struct device *dev)
{
	return  0;
}

/* Power down all displays on reboot, poweroff or halt. */
//---------------------------------------------------------------------------------------------------
static void lg4591_shutdown(struct device *dev)
{
}

//---------------------------------------------------------------------------------------------------
static struct mipi_lcd_driver lg4591_mipi_driver = {
	.name           = "lg4591_mipi_lcd",
	.set_link		= lg4591_set_link,
	.probe			= lg4591_probe,
	.remove			= __devexit_p(lg4591_remove),
	.shutdown		= lg4591_shutdown,
};

//---------------------------------------------------------------------------------------------------
static int lg4591_init(void)
{
	return s5p_dsim_register_lcd_driver(&lg4591_mipi_driver);
}

//---------------------------------------------------------------------------------------------------
static void lg4591_exit(void)
{
	return;
}

//---------------------------------------------------------------------------------------------------
module_init(lg4591_init);
module_exit(lg4591_exit);

//---------------------------------------------------------------------------------------------------
MODULE_DESCRIPTION("MIPI-DSI lg4591 (720x1280) Panel Driver");
MODULE_LICENSE("GPL");

//---------------------------------------------------------------------------------------------------
//---------------------------------------------------------------------------------------------------
