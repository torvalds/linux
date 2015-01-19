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
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <asm/irq.h>
#include <asm/io.h>

#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <asm/uaccess.h>

#include <mach/gpio.h>
#include <linux/switch.h>

#include <linux/aml_holdkey.h>


static struct switch_dev sdev = 
{
    // android ics switch device    
    .name = "hold_key",	
};  

struct hold_key_workdata {
    struct hold_key_cfg *key_cfg;

    struct timer_list timer;
    struct work_struct work_update;

    int hold_status;
};

static struct hold_key_workdata *gp_hold_key_workdata=NULL;

static void holdkey_work(struct hold_key_workdata *hk_workdata)
{
	int io_status;
	io_status = gpio_in_get(hk_workdata->key_cfg->gpio_num);
	if(io_status != hk_workdata->hold_status)
	{
        	//printk("hold_status=%d,io_status=%d\n",hk_workdata->hold_status,io_status);
        	if(hk_workdata->key_cfg->swap)
        		io_status = !io_status;
        	if(hk_workdata->hold_status == -1 && io_status == 1)	//first get hold key status,
        	{
                	switch_set_state(&sdev, io_status);
                	//printk("hold_status=-1,status=%d\n",io_status);
        	}
        	switch_set_state(&sdev, io_status);
        	//printk("kp_work status=%d\n",io_status);
        		
        	hk_workdata->hold_status = io_status;
	}
}

static void update_work_func(struct work_struct *work)
{
    struct hold_key_workdata *hk_workdata = container_of(work, struct hold_key_workdata, work_update);

    holdkey_work(hk_workdata);
}

void holdkey_timer_sr(unsigned long data)
{
    struct hold_key_workdata *hk_workdata=(struct hold_key_workdata *)data;
    schedule_work(&(hk_workdata->work_update));
    mod_timer(&hk_workdata->timer,jiffies+msecs_to_jiffies(25));
}

static int __devinit holdkey_probe(struct platform_device *pdev)
{
    struct hold_key_workdata *hk_workdata;

    struct hold_key_platform_data *pdata = pdev->dev.platform_data;
    if (!pdata) {
        dev_err(&pdev->dev, "platform data is required!\n");
        return -EINVAL;
    }
   
    hk_workdata = kzalloc(sizeof(struct hold_key_workdata), GFP_KERNEL);
    if (!hk_workdata ) {
        return -ENOMEM;
    }
    gp_hold_key_workdata=hk_workdata;

    hk_workdata->hold_status = -1;
    hk_workdata->key_cfg = &(pdata->hk_cfg) ;

    platform_set_drvdata(pdev, hk_workdata);
     
    INIT_WORK(&(hk_workdata->work_update), update_work_func);
     
    setup_timer(&hk_workdata->timer, holdkey_timer_sr, hk_workdata) ;
    mod_timer(&hk_workdata->timer, jiffies+msecs_to_jiffies(100));
    gpio_in_get(hk_workdata->key_cfg->gpio_num);

    switch_dev_register(&sdev);
    
    printk("HoldKey register  completed.\r\n");
    return 0;
}

static int holdkey_remove(struct platform_device *pdev)
{
    switch_dev_unregister(&sdev);
    struct hold_key_workdata *hk_workdata = platform_get_drvdata(pdev);

    kfree(hk_workdata);
    gp_hold_key_workdata=NULL ;
    return 0;
}

static struct platform_driver hk_driver = {
    .probe      = holdkey_probe,
    .remove     = holdkey_remove,
    .suspend    = NULL,
    .resume     = NULL,
    .driver     = {
    .name   = "holdkey",
    },
};

static int __devinit holdkey_init(void)
{
    printk(KERN_INFO " HoldKey Driver init.\n");

    return platform_driver_register(&hk_driver);
}

static void __exit holdkey_exit(void)
{
    printk(KERN_INFO " HoldKey Driver exit.\n");

    platform_driver_unregister(&hk_driver);
}

module_init(holdkey_init);
module_exit(holdkey_exit);

MODULE_AUTHOR("Alex Deng");
MODULE_DESCRIPTION("HoldKey Driver");
MODULE_LICENSE("GPL");




