/* drivers/power/wm831x_charger_display.c
 *
 * battery detect driver for the rk2818 
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */
#include <linux/module.h>
#include <linux/init.h>
#include <linux/slab.h>
#include <linux/kernel.h>
#include <linux/errno.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/fb.h>
#include <linux/delay.h>
#include <linux/suspend.h>
#include <linux/linux_logo.h>
#include <linux/regulator/consumer.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/mfd/wm831x/core.h>
#include <mach/gpio.h>
#include <mach/iomux.h>
#include <mach/rk29_iomap.h>

#include <linux/mfd/wm831x/core.h>
#include <linux/mfd/wm831x/auxadc.h>
#include <linux/mfd/wm831x/pmu.h>
#include <linux/mfd/wm831x/pdata.h>
#include <linux/mfd/wm831x/irq.h>
#include <linux/power_supply.h>

#define READ_ON_PIN_CNT 20/*11*/
#define BACKLIGHT_CNT	2
#define OPEN_CNT		18
#define MC_OPEN 7/*10*/ 
#define BAT_CHARGING    1
#define BAT_DISCHARGING 0
#define BL_DELAY_TIME   (8*1000)


#define SET_BACKLIGHT_ON 	1
#define OPEN_SYSTEM			2
#define SUSPEND_SYSTEM		3

#if 0
#define DBG(x...)   printk(x)
#else
#define DBG(x...)
#endif 

extern void request_suspend_state(suspend_state_t new_state);
extern void kernel_restart(char *cmd);
extern void kernel_power_off(void);

extern unsigned g_vbus_status_register;
extern unsigned long *g_pcd_for_charger;

extern int rk29_backlight_ctrl(int open);

extern void fb_show_charge_logo(struct linux_logo *logo);

extern void kernel_restart_prepare(char *cmd);
extern int dwc_vbus_status( void );
extern int dwc_otg_pcd_check_vbus_detech( unsigned long pdata );

#if defined(CONFIG_LOGO_CHARGER_CLUT224)
extern struct linux_logo logo_charger01_clut224;
extern struct linux_logo logo_charger02_clut224;
extern struct linux_logo logo_charger03_clut224;
extern struct linux_logo logo_charger04_clut224;
extern struct linux_logo logo_charger05_clut224;
extern struct linux_logo logo_charger06_clut224;
extern struct linux_logo logo_charger07_clut224;
extern struct linux_logo logo_charger08_clut224;
#endif

extern struct fb_info *g_fb0_inf;

static struct linux_logo* g_chargerlogo[8]= {
#if defined(CONFIG_LOGO_CHARGER_CLUT224)
	&logo_charger01_clut224,&logo_charger02_clut224,&logo_charger03_clut224,&logo_charger04_clut224,
		&logo_charger05_clut224,&logo_charger06_clut224,&logo_charger07_clut224,&logo_charger08_clut224
#endif
};

#if 1
struct wm831x_chg {
	struct wm831x *wm831x;
	int logo_id;
	int flag_chg;
	int bat_vol;
	int cnt_on;
	int cnt_disp;
	int flag_bl;
	int flag_suspend;
	
};

static int charger_logo_display(struct linux_logo *logo)
{
	fb_show_charge_logo(logo);
	fb_show_logo(g_fb0_inf, 0);
	return 0;
}

extern int charger_suspend(void);//xsf

static int charger_backlight_ctrl(int open)
{
	DBG("%s:open=%d\n",__FUNCTION__,open);
	int ret;

#ifdef CONFIG_RK29_CHARGE_EARLYSUSPEND
	charger_suspend();
	return 0;
#else
	return rk29_backlight_ctrl(open);
#endif


}

static int wm831x_read_on_pin_status(struct wm831x_chg *wm831x_chg)
{
	int ret;
	
	if(!wm831x_chg)
	{
		printk("err:%s:wm831x_chg address is 0\n",__FUNCTION__);
		return -1;
	}
	
	ret = wm831x_reg_read(wm831x_chg->wm831x, WM831X_ON_PIN_CONTROL);
	if (ret < 0)
		return ret;

	return !(ret & WM831X_ON_PIN_STS) ? 1 : 0;
}


static int wm831x_read_chg_status(struct wm831x_chg *wm831x_chg)
{
	int ret, usb_chg = 0, wall_chg = 0;
	
	if(!wm831x_chg)
	{
		printk("err:%s:wm831x_chg address is 0\n",__FUNCTION__);
		return -1;
	}
	
	ret = wm831x_reg_read(wm831x_chg->wm831x, WM831X_SYSTEM_STATUS);
	if (ret < 0)
		return ret;

	if (ret & WM831X_PWR_USB)
		usb_chg = 1;
	if (ret & WM831X_PWR_WALL)
		wall_chg = 1;

	return ((usb_chg | wall_chg) ? 1 : 0);
}

static int wm831x_bat_check_status(struct wm831x *wm831x, int *status)
{
	int ret;

	ret = wm831x_reg_read(wm831x, WM831X_SYSTEM_STATUS);
	if (ret < 0)
		return ret;

	if (ret & WM831X_PWR_SRC_BATT) {
		*status = POWER_SUPPLY_STATUS_DISCHARGING;
		return 0;
	}

	ret = wm831x_reg_read(wm831x, WM831X_CHARGER_STATUS);
	if (ret < 0)
		return ret;

	
	switch (ret & WM831X_CHG_STATE_MASK) {
	case WM831X_CHG_STATE_OFF:
		*status = POWER_SUPPLY_STATUS_NOT_CHARGING;
		break;
	case WM831X_CHG_STATE_TRICKLE:
	case WM831X_CHG_STATE_FAST:
		*status = POWER_SUPPLY_STATUS_CHARGING;
		break;

	default:
		*status = POWER_SUPPLY_STATUS_UNKNOWN;
		break;
	}

	return 0;
}

static int wm831x_read_bat_charging_status(struct wm831x_chg *wm831x_chg)
{
	int ret, status;
	
	if(!wm831x_chg)
	{
		printk("err:%s:g_wm831x_power address is 0\n",__FUNCTION__);
		return -1;
	}
	
	ret = wm831x_bat_check_status(wm831x_chg->wm831x, &status);
	if (ret < 0)
		return ret;
	if (status == POWER_SUPPLY_STATUS_CHARGING) 
		return 1;
	return 0;
}

static int wm831x_read_batt_voltage(struct wm831x_chg *wm831x_chg)
{
	int ret = 0;
	
	if(!wm831x_chg)
	{
		printk("err:%s:wm831x_chg address is 0\n",__FUNCTION__);
		return -1;
	}
	
	ret = wm831x_auxadc_read_uv(wm831x_chg->wm831x, WM831X_AUX_BATT);
	return ret / 1000;
}


static int get_charger_logo_start_num(struct wm831x_chg *wm831x_chg)
{
	int rlogonum, bat_vol;

	/*check charger voltage*/
	bat_vol = wm831x_read_batt_voltage(wm831x_chg);
	if(bat_vol <= 3610) {
		rlogonum = 0;
	}
	else if(bat_vol <= 3700) {
		rlogonum = 1;
	}
	else if(bat_vol <= 3760) {
		rlogonum = 2;
	}
	else if(bat_vol <= 3840) {
		rlogonum = 3;
	}
	else if(bat_vol <= 3900) {
		rlogonum = 4;
	}
	else if(bat_vol <= 3990) {
		rlogonum = 5;
	}
	else if(bat_vol <= 4130) {
		rlogonum = 6;
	}
	else if(bat_vol <= 4200) {
		rlogonum = 7;
	}
	else{
		rlogonum = 7;
	}
		
	return rlogonum;
}

static int wm831x_check_on_pin(struct wm831x_chg *wm831x_chg)
{
	int ret;
	ret = wm831x_read_on_pin_status(wm831x_chg);
	if(ret)
	{
		if(wm831x_chg->cnt_on++ > 1000)
			wm831x_chg->cnt_on = 1000;
	}
	else
	{
		//control backlight if press on pin
		if(wm831x_chg->cnt_on >= 1)
		{
			wm831x_chg->flag_bl = !wm831x_chg->flag_bl;
			charger_backlight_ctrl(wm831x_chg->flag_bl);
			wm831x_chg->cnt_on = 0;	
			if(wm831x_chg->flag_bl)
			{
				wm831x_chg->flag_suspend = 0;
				wm831x_chg->cnt_disp = 0;
			}
		}
	}

	return 0;
}

static int rk29_charger_display(struct wm831x_chg *wm831x_chg)
{
	int status;
	struct linux_logo* chargerlogo[8];
	int ret,i;
	int count = 0;
	
	wm831x_chg->flag_chg = wm831x_read_chg_status(wm831x_chg);
	if(!wm831x_chg->flag_chg)
		return -1;

	while(1)
	{
		wm831x_chg->flag_chg = wm831x_read_chg_status(wm831x_chg);
		if(!wm831x_chg->flag_chg)
			kernel_power_off();

		status = wm831x_read_bat_charging_status(wm831x_chg);

		for(i=0; i<8; i++)
		chargerlogo[i] = g_chargerlogo[i];
	
		if(status == BAT_CHARGING)
		{	
			for(i=get_charger_logo_start_num(wm831x_chg); i<8; i++ )
			{
				wm831x_chg->flag_chg = wm831x_read_chg_status(wm831x_chg);
				if(!wm831x_chg->flag_chg)
				kernel_power_off();
			#ifdef CONFIG_RK29_CHARGE_EARLYSUSPEND
				ret = charger_logo_display(chargerlogo[i]);
			#else
				if(wm831x_chg->flag_bl != 0)
				ret = charger_logo_display(chargerlogo[i]);
			#endif
				DBG("%s:i=%d\n",__FUNCTION__,i);

				msleep(200);	
				wm831x_check_on_pin(wm831x_chg);
				msleep(200);
				wm831x_check_on_pin(wm831x_chg);

			}
					
		}
		else if(status == BAT_DISCHARGING)
		{

		#ifdef CONFIG_RK29_CHARGE_EARLYSUSPEND
			charger_logo_display(chargerlogo[7]);
		#else
			if(wm831x_chg->flag_bl != 0)
			charger_logo_display(chargerlogo[7]);
		#endif	
			msleep(200);
			wm831x_check_on_pin(wm831x_chg);
			msleep(200);
			wm831x_check_on_pin(wm831x_chg);
		}
		
		//suspend when timeout(about 50*200ms)
		if(wm831x_chg->cnt_disp++ > 50)
		{
			if(wm831x_chg->flag_suspend == 0)
			{
				wm831x_chg->flag_suspend = 1;
				wm831x_chg->cnt_disp = 0;
				wm831x_chg->flag_bl = 0;
				charger_backlight_ctrl(wm831x_chg->flag_bl);
		#ifdef CONFIG_RK29_CHARGE_EARLYSUSPEND
				wm831x_chg->flag_suspend = 0;
		#endif
				
			}
			wm831x_chg->cnt_disp = 0;
		}

		printk("%s,status=%d,cnt_on=%d,cnt_disp=%d\n",__FUNCTION__,status,wm831x_chg->cnt_on,wm831x_chg->cnt_disp);

		//open system if long time press
		if(wm831x_chg->cnt_on > 4)
		{
			wm831x_chg->cnt_on = 0;
			wm831x_chg->flag_bl = 1;
			charger_backlight_ctrl(wm831x_chg->flag_bl);
			wm831x_chg->flag_suspend = 0;
			wm831x_chg->cnt_disp = 0;
			break;
		}	
		
	}

	return 0;

}
int charge_status;
static irqreturn_t wm831x_charge_irq(int irq, void *data)
{

	printk("wm831x_charge_irqxxaddxsf\n");
	return IRQ_HANDLED;


}
extern struct wm831x_on *g_wm831x_on;
 irqreturn_t wm831x_on_irq(int irq, void *data);

static int __devinit wm831x_chg_probe(struct platform_device *pdev)
{
	struct wm831x *wm831x = dev_get_drvdata(pdev->dev.parent);;
	struct wm831x_chg *wm831x_chg;
	
//	struct wm831x_on *wm831x_on = container_of(wm831x,struct wm831x_on,*(wm831x));
	
	
	int ret;
	
	wm831x_chg = kzalloc(sizeof(struct wm831x_chg), GFP_KERNEL);
	if (!wm831x_chg) {
		dev_err(&pdev->dev, "Can't allocate data\n");
		return -ENOMEM;
	}
	charge_status = 1;
	printk("%s:start\n",__FUNCTION__);
	wm831x_chg->wm831x = wm831x;
	wm831x_chg->flag_chg = 0;
	wm831x_chg->logo_id = 0;
	wm831x_chg->flag_bl = 1;
	wm831x_chg->cnt_on = 0;
	wm831x_chg->flag_suspend = 0;
	platform_set_drvdata(pdev, wm831x_chg);

#ifdef CONFIG_RK29_CHARGE_EARLYSUSPEND
	wm831x_chg->flag_chg = wm831x_read_chg_status(wm831x_chg);
	if(wm831x_chg->flag_chg != 0)
	{
		free_irq(wm831x_chg->wm831x->irq_base + WM831X_IRQ_ON,g_wm831x_on);
		request_threaded_irq(wm831x_chg->wm831x->irq_base + WM831X_IRQ_ON, 
						NULL, wm831x_charge_irq,IRQF_TRIGGER_RISING, "wm831x_charge",
					   wm831x_chg);

		ret = rk29_charger_display(wm831x_chg);
		

		free_irq(wm831x_chg->wm831x->irq_base + WM831X_IRQ_ON,wm831x_chg);	
		request_threaded_irq(wm831x_chg->wm831x->irq_base + WM831X_IRQ_ON,
			NULL, wm831x_on_irq,IRQF_TRIGGER_RISING, "wm831x_on",  g_wm831x_on);
	}
#else
	disable_irq_nosync(wm831x_chg->wm831x->irq_base + WM831X_IRQ_ON);
	ret = rk29_charger_display(wm831x_chg);
	enable_irq(wm831x_chg->wm831x->irq_base + WM831X_IRQ_ON);
#endif
	wm831x_chg->flag_chg = 0;
	wm831x_chg->flag_bl = 1;
	wm831x_chg->cnt_on = 0;
	wm831x_chg->flag_suspend = 0;
	charge_status = 0;
	printk("%s:exit\n",__FUNCTION__);
	return 0;

}


static int __devexit wm831x_chg_remove(struct platform_device *pdev)
{
	struct wm831x_chg *wm831x_chg = platform_get_drvdata(pdev);

	kfree(wm831x_chg);

	return 0;
}

static struct platform_driver wm831x_chg_driver = {
	.probe		= wm831x_chg_probe,
	.remove		= __devexit_p(wm831x_chg_remove),
	.driver		= {
		.name	= "wm831x_charger_display",
		.owner	= THIS_MODULE,
	},
};

static int __init wm831x_chg_init(void)
{
	return platform_driver_register(&wm831x_chg_driver);
}
late_initcall(wm831x_chg_init);

static void __exit wm831x_chg_exit(void)
{
	platform_driver_unregister(&wm831x_chg_driver);
}
module_exit(wm831x_chg_exit);
#endif


MODULE_LICENSE("GPL");
MODULE_AUTHOR("linjh<linjh@rock-chips.com>");
MODULE_DESCRIPTION("charger display");
