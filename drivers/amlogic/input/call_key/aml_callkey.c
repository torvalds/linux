/*
 * drivers/amlogic/input/holdkey/aml_holdkey.c
 *
 * aml HoldKey Driver
 *
 * Copyright (C) 2012 Amlogic, Inc.
 *
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA
 * author :   Alex Deng
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/kernel.h>
#include <linux/input.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/wakelock.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <mach/gpio.h>
#include <mach/gpio_data.h>

#include <linux/aml_callkey.h>

#ifdef CONFIG_HAS_EARLYSUSPEND
#include <linux/earlysuspend.h>
static struct early_suspend callkey_early_suspend;
#endif

struct wake_lock wk_lk ;
static int wk_LK_flag=0;

#define CALL_FLAG (0x1234ca11)
#define KEY_CODE KEY_SEND
#define KEY_PHONERING KEY_CAMERA
#define WORK_DELAYTIME (150) //ms
#define UNLOCK_DELAYTIME (9*1000)

struct input_dev *input;

struct call_key_workdata {
    struct call_key_platform_data *callkey_pdata;

    struct timer_list timer;
    struct timer_list starter_timer;
    struct work_struct work_update;
	int hp_status;
    int call_status;
    int first_boot;
    
    struct timer_list wk_timer;
    struct wake_lock wk_lk ;
	int wk_LK_flag;
};

static struct call_key_workdata *gp_call_key_workdata=NULL;

#ifdef CONFIG_AMLOGIC_MODEM
extern ssize_t get_modemPower(void);
#endif

void InitWakeLock(struct call_key_workdata *pck)
{
	wake_lock_init(&pck->wk_lk,WAKE_LOCK_SUSPEND,"aml_callkey");
}

static void requestWakeLock(struct call_key_workdata *pck)
{
    if(!pck->wk_LK_flag){
        printk("aml_callkey requestWakeLock\n");
        wake_lock(&pck->wk_lk);
        pck->wk_LK_flag = 1;
    }
}

static void releaseWakeLock(struct call_key_workdata *pck)
{
    if(pck->wk_LK_flag){
        printk("aml_callkey releaseWakeLock\n");
        wake_unlock(&pck->wk_lk);
        pck->wk_LK_flag = 0 ;
    }
}

void callkey_wakelock_timer_fun(unsigned long data)
{
	struct call_key_workdata *ck_workdata=(struct call_key_workdata *)data;
	releaseWakeLock(ck_workdata);
}

static void callkey_work(struct call_key_workdata *ck_workdata)
{
	static int cnt=0;
	int callkey_status=0;
	int hp_status = 0;
	
	if(ck_workdata->first_boot){
		if(cnt++<50){
			return ;
		}
		else{
			ck_workdata->first_boot = 0;
			cnt =0 ;
		}
	}
	
	hp_status = ck_workdata->callkey_pdata->get_hp_status();
	if(hp_status == ck_workdata->hp_status){
		if(hp_status == ck_workdata->callkey_pdata->hp_in_value){
			if(ck_workdata->callkey_pdata->get_call_key_value()){
				printk("callkey(%d) is pressed!!!\n",KEY_CODE);
				//hp is in,and callkey is pressed
				input_report_key(input, KEY_CODE, 1);
		    	input_sync(input);
		    	ck_workdata->call_status = 1 ;
			}
			else if(ck_workdata->call_status==1){
				printk("callkey(%d) is unpressed!!!\n",KEY_CODE);
				//hp is in,and callkey is pressed
				ck_workdata->call_status = 0;
				input_report_key(input, KEY_CODE, 0);
		    	input_sync(input);
			}
		}
	}else{
		if(cnt++>10){
			ck_workdata->hp_status = hp_status;
			cnt=0;
		}
	}
}

static void update_work_func(struct work_struct *work)
{
    struct call_key_workdata *ck_workdata = container_of(work, struct call_key_workdata, work_update);

    callkey_work(ck_workdata);
}

void callkey_timer_sr(unsigned long data)
{
    struct call_key_workdata *ck_workdata=(struct call_key_workdata *)data;
    schedule_work(&(ck_workdata->work_update));
    mod_timer(&ck_workdata->timer,jiffies+msecs_to_jiffies(WORK_DELAYTIME));
}

void callkey_start_worktimer(unsigned long data)
{
	struct call_key_workdata *ck_workdata=(struct call_key_workdata *)data;
	mod_timer(&ck_workdata->timer,jiffies+msecs_to_jiffies(2000));
}

#ifdef CONFIG_HAS_EARLYSUSPEND
static int aml_callkey_earlysuspend(struct early_suspend *handler)
{
	del_timer_sync(&gp_call_key_workdata->timer);
	return 0;
}

static int aml_callkey_earlyresume(struct early_suspend *handler)
{
	mod_timer(&gp_call_key_workdata->starter_timer,jiffies+msecs_to_jiffies(WORK_DELAYTIME));

	return 0;
}

#endif

#ifdef CONFIG_PM
int aml_callkey_suspend(struct platform_device *pdev, pm_message_t state)
{

    return 0;
}

int aml_callkey_resume(struct platform_device *pdev)
{
    int v=0;
#ifdef CONFIG_AMLOGIC_MODEM
    if(!get_modemPower()){
    printk("aml_callkey_resume, modem is power off\n");
    return 0;
    }
#endif

    if (READ_AOBUS_REG(AO_RTI_STATUS_REG2) == CALL_FLAG) {
        printk("aml_callkey_resume, uboot tell me call wakeup system\n");
        v=1;
    }else{
        v = gp_call_key_workdata->callkey_pdata->get_phone_ring_value();
        printk("aml_callkey_resume, get_phone_ring_value is %d\n",v);
    }
    
    if(0 < v){
        printk("aml_callkey_resume, key %d pressed.\n", KEY_PHONERING);
        requestWakeLock(gp_call_key_workdata);                   
        input_report_key(input, KEY_PHONERING, 1);
        input_sync(input);

        mod_timer(&gp_call_key_workdata->wk_timer,jiffies+msecs_to_jiffies(UNLOCK_DELAYTIME));
        WRITE_AOBUS_REG(AO_RTI_STATUS_REG2, 0);
    }
    return 0 ;
}
#endif

static int __devinit callkey_probe(struct platform_device *pdev)
{
	int ret = -1;
    struct call_key_workdata *ck_workdata;

    struct call_key_platform_data *pdata = pdev->dev.platform_data;
    if (!pdata) {
        dev_err(&pdev->dev, "platform data is required!\n");
        return -EINVAL;
    }
   
    ck_workdata = kzalloc(sizeof(struct call_key_workdata), GFP_KERNEL);
    if (!ck_workdata ) {
        return -ENOMEM;
    }
    gp_call_key_workdata=ck_workdata;

    ck_workdata->call_status = 0;
    ck_workdata->first_boot = 1;
    ck_workdata->callkey_pdata = pdata ;
	
	InitWakeLock(ck_workdata);

    platform_set_drvdata(pdev, ck_workdata);
    
    input = input_allocate_device();
    if(input == NULL){
    	printk("callkey input_allocate_device failed\n");
    	kfree(ck_workdata);
    	return -ENOMEM;
    }
    /* setup input device */
    set_bit(EV_KEY, input->evbit);
    set_bit(EV_REP, input->evbit);
    set_bit(KEY_CODE, input->keybit);
    set_bit(KEY_PHONERING, input->keybit);
    
    input->name = "aml_callkey";
    input->phys = "aml_callkey/input0";
    input->dev.parent = &pdev->dev;

    input->id.bustype = BUS_ISA;
    input->id.vendor = 0x0001;
    input->id.product = 0x0001;
    input->id.version = 0x0100;

    input->rep[REP_DELAY]=0xffffffff;
    input->rep[REP_PERIOD]=0xffffffff;

    input->keycodesize = sizeof(unsigned short);
    input->keycodemax = 0x1ff;
    
    ret = input_register_device(input);
    if (ret < 0) {
        printk(KERN_ERR "Unable to register keypad input device.\n");
        	kfree(ck_workdata);
		    input_free_device(input);
		    return -EINVAL;
    }
    
    INIT_WORK(&(ck_workdata->work_update), update_work_func);
        
    setup_timer(&ck_workdata->timer, callkey_timer_sr, (unsigned long)ck_workdata) ;
    //mod_timer(&ck_workdata->timer, jiffies+msecs_to_jiffies(WORK_DELAYTIME));
    
    setup_timer(&ck_workdata->starter_timer, callkey_start_worktimer, (unsigned long)ck_workdata) ;   
    mod_timer(&ck_workdata->starter_timer, jiffies+msecs_to_jiffies(25000));
    
    setup_timer(&ck_workdata->wk_timer, callkey_wakelock_timer_fun, (unsigned long)ck_workdata) ; 
    
    #ifdef CONFIG_HAS_EARLYSUSPEND
    callkey_early_suspend.level = EARLY_SUSPEND_LEVEL_STOP_DRAWING - 1;
    callkey_early_suspend.suspend = aml_callkey_earlysuspend;
    callkey_early_suspend.resume = aml_callkey_earlyresume;
    callkey_early_suspend.param = gp_call_key_workdata;
    register_early_suspend(&callkey_early_suspend);
    #endif
    
    printk("CallKey register  completed.\r\n");
    return 0;
}

static int callkey_remove(struct platform_device *pdev)
{
	input_unregister_device(input);
	input_free_device(input);
    struct call_key_workdata *ck_data = platform_get_drvdata(pdev);

    kfree(ck_data);
    gp_call_key_workdata=NULL ;
    return 0;
}

static struct platform_driver ck_driver = {
    .probe      = callkey_probe,
    .remove     = callkey_remove,
#ifdef CONFIG_PM
    .suspend    = aml_callkey_suspend,
    .resume     = aml_callkey_resume,
#endif
    .driver     = {
        .name   = "callkey",
    },
};

static int __devinit callkey_init(void)
{
    printk(KERN_INFO " CallKey Driver init.\n");

    return platform_driver_register(&ck_driver);
}

static void __exit callkey_exit(void)
{
    printk(KERN_INFO " CallKey Driver exit.\n");

    platform_driver_unregister(&ck_driver);
}

module_init(callkey_init);
module_exit(callkey_exit);

MODULE_AUTHOR("Alex Deng");
MODULE_DESCRIPTION("CallKey Driver");
MODULE_LICENSE("GPL");




