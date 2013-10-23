
/*
 * Driver for keys on GPIO lines capable of generating interrupts.
 *
 * Copyright 2005 Phil Blundell
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/fs.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/sched.h>
#include <linux/pm.h>
#include <linux/sysctl.h>
#include <linux/proc_fs.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/input.h>
#include <linux/workqueue.h>
#include <linux/adc.h>
#include <asm/gpio.h>
#include <mach/remotectl.h>
#include <mach/iomux.h>
#include <linux/wakelock.h>
#include <linux/suspend.h>


#if 1
#define remotectl_dbg(bdata, format, arg...)		\
	dev_printk(KERN_INFO , &bdata->input->dev , format , ## arg)
#else
#define remotectl_dbg(bdata, format, arg...)	
#endif

extern suspend_state_t get_suspend_state(void);

struct rkxx_remotectl_suspend_data{
    int suspend_flag;
    int cnt;
    long scanTime[50];
};

struct rkxx_remote_key_table{
    int scanCode;
	int keyCode;		
};

struct rkxx_remotectl_button {	
    int usercode;
    int nbuttons;
    struct rkxx_remote_key_table *key_table;
};

struct rkxx_remotectl_drvdata {
    int state;
	int nbuttons;
	int result;
    unsigned long pre_time;
    unsigned long cur_time;
    long int pre_sec;
    long int cur_sec;
    long period;
    int scanData;
    int count;
    int keybdNum;
    int keycode;
    int press;
    int pre_press;
    
    struct input_dev *input;
    struct timer_list timer;
    struct tasklet_struct remote_tasklet;
    struct wake_lock remotectl_wake_lock;
    struct rkxx_remotectl_suspend_data remotectl_suspend_data;
};



//特殊功能键值定义
    //193      //photo
    //194      //video
    //195      //music
    //196      //IE
    //197      //
    //198
    //199
    //200
    
    //183      //rorate_left
    //184      //rorate_right
    //185      //zoom out
    //186      //zoom in
    
static struct rkxx_remote_key_table remote_key_table_meiyu_202[] = {
    {0xB0, KEY_ENTER},//ok = DPAD CENTER
    {0xA2, KEY_BACK}, 
    {0xD0, KEY_UP},
    {0x70, KEY_DOWN},
    {0x08, KEY_LEFT},
    {0x88, KEY_RIGHT},  ////////
    {0x42, KEY_HOME},     //home
    {0xA8, KEY_VOLUMEUP},
    {0x38, KEY_VOLUMEDOWN},
    {0xE2, KEY_SEARCH},     //search
    {0xB2, KEY_POWER},     //power off
    {0xC2, KEY_MUTE},       //mute
    {0xC8, KEY_MENU},

//media ctrl
    {0x78,   0x190},      //play pause
    {0xF8,   0x191},      //pre
    {0x02,   0x192},      //next

//pic
    {0xB8, 183},          //rorate left
    {0x58, 248},          //rorate right
    {0x68, 185},          //zoom out
    {0x98, 186},          //zoom in
//mouse switch
    {0xf0,388},
//display switch
    {0x82,   0x175},
};

static struct rkxx_remote_key_table remote_key_table_df[] = {
    {0xf8, KEY_REPLY},
    {0xc0, KEY_BACK}, 
    {0xf0, KEY_UP},
    {0xd8, KEY_DOWN},
    {0xd0, KEY_LEFT},
    {0xe8,KEY_RIGHT},  ////////
    {0x90, KEY_VOLUMEDOWN},
    {0x60, KEY_VOLUMEUP},
    {0x80, KEY_HOME},     //home
    {0xe0, 183},          //rorate left
    {0x10, 184},          //rorate right
    {0x20, 185},          //zoom out
    {0xa0, 186},          //zoom in
    {0x70, KEY_MUTE},       //mute
    {0x50, KEY_POWER},     //power off
    {0x40, KEY_SEARCH},     //search
};

extern suspend_state_t get_suspend_state(void);


static struct rkxx_remotectl_button remotectl_button[] = 
{
    {  
       .usercode = 0x206, 
       .nbuttons =  22, 
       .key_table = &remote_key_table_meiyu_202[0],
    },
    {
       .usercode = 0x12ee,
       .nbuttons =  22,
       .key_table = &remote_key_table_meiyu_202[0],
    },
    {  
       .usercode = 0x202, 
       .nbuttons =  22, 
       .key_table = &remote_key_table_meiyu_202[0],
    },
    {  
       .usercode = 0xdf, 
       .nbuttons =  16, 
       .key_table = &remote_key_table_df[0],
    },    
    
};


static int remotectl_keybdNum_lookup(struct rkxx_remotectl_drvdata *ddata)
{	
    int i;	

    for (i = 0; i < sizeof(remotectl_button)/sizeof(struct rkxx_remotectl_button); i++){		
        if (remotectl_button[i].usercode == (ddata->scanData&0xFFFF)){			
            ddata->keybdNum = i;
            return 1;
        }
    }
    return 0;
}


static int remotectl_keycode_lookup(struct rkxx_remotectl_drvdata *ddata)
{	
    int i;	
    unsigned char keyData = ((ddata->scanData >> 8) & 0xff);

    for (i = 0; i < remotectl_button[ddata->keybdNum].nbuttons; i++){
        if (remotectl_button[ddata->keybdNum].key_table[i].scanCode == keyData){			
            ddata->keycode = remotectl_button[ddata->keybdNum].key_table[i].keyCode;
            return 1;
        }
    }
    return 0;
}


static void remotectl_get_pwr_scanData(struct rkxx_remotectl_drvdata *ddata,int *pwr_data,int loop)
{	
    int i;
    int temp_scanCode;
    int temp_pwr_data;
    
    for (i = 0; i < remotectl_button[loop].nbuttons; i++){
        if (remotectl_button[loop].key_table[i].keyCode == KEY_POWER){			
            temp_scanCode = remotectl_button[loop].key_table[i].scanCode;
            temp_pwr_data = (temp_scanCode<<8)|((~temp_scanCode)&0xFF);
            //printk("pwr data =0x%x\n",temp_pwr_data);
        }
    }
    *pwr_data = temp_pwr_data;
}

static void remotectl_do_something(unsigned long  data)
{
    struct rkxx_remotectl_drvdata *ddata = (struct rkxx_remotectl_drvdata *)data;

    switch (ddata->state)
    {
        case RMC_IDLE:
        {
            ;
        }
        break;
        
        case RMC_PRELOAD:
        {
            mod_timer(&ddata->timer,jiffies + msecs_to_jiffies(130));
            //printk("RMC_PRELOAD,period=%d\n",ddata->period);
            if ((TIME_PRE_MIN < ddata->period) && (ddata->period < TIME_PRE_MAX)){
                
                ddata->scanData = 0;
                ddata->count = 0;
                ddata->state = RMC_USERCODE;
            }else{
                ddata->state = RMC_PRELOAD;
            }
            ddata->pre_time = ddata->cur_time;
            //mod_timer(&ddata->timer,jiffies + msecs_to_jiffies(130));
        }
        break;
        
        case RMC_USERCODE:
        {
            ddata->scanData <<= 1;
            ddata->count ++;
	    printk("RMC_USERCODE,period=%d，count=%d\n",ddata->period,ddata->count );
            if ((TIME_BIT1_MIN < ddata->period) && (ddata->period < TIME_BIT1_MAX)){
                ddata->scanData |= 0x01;
            }
		
            if (ddata->count == 0x10){//16 bit user code
                printk("u=0x%x\n",((ddata->scanData)&0xFFFF));
                if (remotectl_keybdNum_lookup(ddata)){
                    ddata->state = RMC_GETDATA;
                    ddata->scanData = 0;
                    ddata->count = 0;
                }else{                //user code error
                    ddata->state = RMC_PRELOAD;
                }
            }
        }
        break;
        
        case RMC_GETDATA:
        {
            ddata->count ++;
            ddata->scanData <<= 1;

          
            if ((TIME_BIT1_MIN < ddata->period) && (ddata->period < TIME_BIT1_MAX)){
                ddata->scanData |= 0x01;
            }           
            if (ddata->count == 0x10){
                //printk("RMC_GETDATA=%x\n",(ddata->scanData&0xFFFF));

                if ((ddata->scanData&0x0ff) == ((~ddata->scanData >> 8)&0x0ff)){
                    if (remotectl_keycode_lookup(ddata)){
                        ddata->press = 1;
                        /*
                         if (get_suspend_state()==0){
                                input_event(ddata->input, EV_KEY, ddata->keycode, 1);
                                input_sync(ddata->input);
                            }else if ((get_suspend_state())&&(ddata->keycode==KEY_POWER)){
                                input_event(ddata->input, EV_KEY, KEY_WAKEUP, 1);
                                input_sync(ddata->input);
                            }*/
                            //printk("0\n");
                            input_event(ddata->input, EV_KEY, ddata->keycode, 1);
                            input_sync(ddata->input);
                        //input_event(ddata->input, EV_KEY, ddata->keycode, ddata->press);
		                //input_sync(ddata->input);
                        ddata->state = RMC_SEQUENCE;
                    }else{
                        ddata->state = RMC_PRELOAD;
                    }
                }else{
                    ddata->state = RMC_PRELOAD;
                }
            }
        }
        break;
             
        case RMC_SEQUENCE:{

            //printk( "S=%d\n",ddata->period);
  
            if ((TIME_RPT_MIN < ddata->period) && (ddata->period < TIME_RPT_MAX)){
            		 mod_timer(&ddata->timer,jiffies + msecs_to_jiffies(110));
                 //printk("1\n");;
            }else if ((TIME_SEQ1_MIN < ddata->period) && (ddata->period < TIME_SEQ1_MAX)){
	 							  mod_timer(&ddata->timer,jiffies + msecs_to_jiffies(110));
	 							  //printk("2\n");
            }else if ((TIME_SEQ2_MIN < ddata->period) && (ddata->period < TIME_SEQ2_MAX)){
            		  mod_timer(&ddata->timer,jiffies + msecs_to_jiffies(110));
            		  //printk("3\n");;   
            }else{
                	 input_event(ddata->input, EV_KEY, ddata->keycode, 0);
		               input_sync(ddata->input);
                	 ddata->state = RMC_PRELOAD;
                	 ddata->press = 0;
                	 //printk("4\n");
            }
        }
        break;
       
        default:
            break;
    } 
	return;
}


#ifdef CONFIG_PM
void remotectl_wakeup(unsigned long _data)
{
    struct rkxx_remotectl_drvdata *ddata =  (struct rkxx_remotectl_drvdata*)_data;
    long *time;
    int i;
	int power_scanData;
		 
    time = ddata->remotectl_suspend_data.scanTime;

    if (get_suspend_state()){
        ddata->remotectl_suspend_data.suspend_flag = 0;
        ddata->count = 0;
        ddata->state = RMC_USERCODE;
        ddata->scanData = 0;
        
        for (i=0;i<ddata->remotectl_suspend_data.cnt;i++){
        		if (ddata->count>=32)
        			break;

           if ((TIME_BIT1_MIN < time[i]) && (time[i] < TIME_BIT1_MAX)){
                ddata->scanData |= 0x01;
                ddata->scanData <<= 1;
                ddata->count ++;;
            }else if ((TIME_BIT0_MIN < time[i]) && (time[i] < TIME_BIT0_MAX)){
            	  ddata->scanData <<= 1;
            	  ddata->count ++;;
            }/*else{
            	   if (ddata->count>16){
            	   	  break;
            	   }else{
            	   	
            	   	printk(KERN_ERR "ddata->count=0x%x**********************\n",ddata->count);
            	   	ddata->count = 0;
            	   	ddata->scanData = 0;
            	   }		
            }*/
        }
        //printk(KERN_ERR"data=0x%x\n",ddata->scanData);
        if (ddata->scanData)					//(ddata->scanData>16)			
				{
					  ddata->scanData=(ddata->scanData>>1)&0xFFFF;				
					  printk(KERN_ERR"data=0x%x\n",ddata->scanData);
					  
					  for (i=0;i<sizeof(remotectl_button)/sizeof(struct rkxx_remotectl_button);i++){
					  	remotectl_get_pwr_scanData(ddata,&power_scanData,i);
					  	if ((ddata->scanData == power_scanData)||((ddata->scanData&0x0fff) == (power_scanData&0x0fff))||((ddata->scanData&0x00ff) == (power_scanData&0x00ff)))					//modified by zwm	2013.06.19
					    {
					    	input_event(ddata->input, EV_KEY, KEY_WAKEUP, 1);
            		input_sync(ddata->input);
            		input_event(ddata->input, EV_KEY, KEY_WAKEUP, 0);
            		input_sync(ddata->input);
            		break;
					    }
					  }
				}
    }
    memset(ddata->remotectl_suspend_data.scanTime,0,50*sizeof(long));
    ddata->remotectl_suspend_data.cnt= 0; 
    ddata->state = RMC_PRELOAD;
    
}

#endif


static void remotectl_timer(unsigned long _data)
{
    struct rkxx_remotectl_drvdata *ddata =  (struct rkxx_remotectl_drvdata*)_data;
    
    //printk("to\n");
    
    if(ddata->press != ddata->pre_press) {
        ddata->pre_press = ddata->press = 0;
        
				input_event(ddata->input, EV_KEY, ddata->keycode, 0);
        input_sync(ddata->input);
        //printk("5\n");
        //if (get_suspend_state()==0){
            //input_event(ddata->input, EV_KEY, ddata->keycode, 1);
            //input_sync(ddata->input);
            //input_event(ddata->input, EV_KEY, ddata->keycode, 0);
		    //input_sync(ddata->input);
        //}else if ((get_suspend_state())&&(ddata->keycode==KEY_POWER)){
            //input_event(ddata->input, EV_KEY, KEY_WAKEUP, 1);
            //input_sync(ddata->input);
            //input_event(ddata->input, EV_KEY, KEY_WAKEUP, 0);
            //input_sync(ddata->input);
        //}
    }
#ifdef CONFIG_PM
    remotectl_wakeup(_data);
#endif
    ddata->state = RMC_PRELOAD;
}



static irqreturn_t remotectl_isr(int irq, void *dev_id)
{
    struct rkxx_remotectl_drvdata *ddata =  (struct rkxx_remotectl_drvdata*)dev_id;
    struct timeval  ts;


    ddata->pre_time = ddata->cur_time;
    ddata->pre_sec = ddata->cur_sec;
    do_gettimeofday(&ts);
    ddata->cur_time = ts.tv_usec;
    ddata->cur_sec = ts.tv_sec;
    
		if (likely(ddata->cur_sec == ddata->pre_sec)){
			ddata->period =  ddata->cur_time - ddata->pre_time;
	  }else{
				ddata->period =  1000000 - ddata->pre_time + ddata->cur_time;
		}

    tasklet_hi_schedule(&ddata->remote_tasklet); 
    //if ((ddata->state==RMC_PRELOAD)||(ddata->state==RMC_SEQUENCE))
    //mod_timer(&ddata->timer,jiffies + msecs_to_jiffies(130));
#ifdef CONFIG_PM
   if (ddata->state==RMC_PRELOAD)
       wake_lock_timeout(&ddata->remotectl_wake_lock, HZ);
   if ((get_suspend_state())&&(ddata->remotectl_suspend_data.cnt<50))		//zwm
       ddata->remotectl_suspend_data.scanTime[ddata->remotectl_suspend_data.cnt++] = ddata->period;
#endif

    return IRQ_HANDLED;
}


static int __devinit remotectl_probe(struct platform_device *pdev)
{
    struct RKxx_remotectl_platform_data *pdata = pdev->dev.platform_data;
    struct rkxx_remotectl_drvdata *ddata;
    struct input_dev *input;
    int i, j;
    int irq;
    int error = 0;

    printk("++++++++remotectl_probe\n");

    if(!pdata) 
        return -EINVAL;

    ddata = kzalloc(sizeof(struct rkxx_remotectl_drvdata),GFP_KERNEL);
    memset(ddata,0,sizeof(struct rkxx_remotectl_drvdata));

    ddata->state = RMC_PRELOAD;
    input = input_allocate_device();
    
    if (!ddata || !input) {
        error = -ENOMEM;
        goto fail0;
    }

    platform_set_drvdata(pdev, ddata);

    input->name = pdev->name;
    input->phys = "gpio-keys/input0";
    input->dev.parent = &pdev->dev;

    input->id.bustype = BUS_HOST;
    input->id.vendor = 0x0001;
    input->id.product = 0x0001;
    input->id.version = 0x0100;

	/* Enable auto repeat feature of Linux input subsystem */
	if (pdata->rep)
		__set_bit(EV_REP, input->evbit);
    
	ddata->nbuttons = pdata->nbuttons;
	ddata->input = input;
  wake_lock_init(&ddata->remotectl_wake_lock, WAKE_LOCK_SUSPEND, "rk29_remote");
  if (pdata->set_iomux){
  	pdata->set_iomux();
  }
  error = gpio_request(pdata->gpio, "remotectl");
	if (error < 0) {
		printk("gpio-keys: failed to request GPIO %d,"
		" error %d\n", pdata->gpio, error);
		//goto fail1;
	}
	error = gpio_direction_input(pdata->gpio);
	if (error < 0) {
		pr_err("gpio-keys: failed to configure input"
			" direction for GPIO %d, error %d\n",
		pdata->gpio, error);
		gpio_free(pdata->gpio);
		//goto fail1;
	}
    irq = gpio_to_irq(pdata->gpio);
	if (irq < 0) {
		error = irq;
		pr_err("gpio-keys: Unable to get irq number for GPIO %d, error %d\n",
		pdata->gpio, error);
		gpio_free(pdata->gpio);
		goto fail1;
	}
	
	error = request_irq(irq, remotectl_isr,	IRQF_TRIGGER_FALLING , "remotectl", ddata);
	
	if (error) {
		pr_err("gpio-remotectl: Unable to claim irq %d; error %d\n", irq, error);
		gpio_free(pdata->gpio);
		goto fail1;
	}
    setup_timer(&ddata->timer,remotectl_timer, (unsigned long)ddata);
    
    tasklet_init(&ddata->remote_tasklet, remotectl_do_something, (unsigned long)ddata);
    
    for (j=0;j<sizeof(remotectl_button)/sizeof(struct rkxx_remotectl_button);j++){ 
    	printk("remotectl probe j=0x%x\n",j);
		for (i = 0; i < remotectl_button[j].nbuttons; i++) {
			unsigned int type = EV_KEY;
	        
			input_set_capability(input, type, remotectl_button[j].key_table[i].keyCode);
		}
  }
	error = input_register_device(input);
	if (error) {
		pr_err("gpio-keys: Unable to register input device, error: %d\n", error);
		goto fail2;
	}
    
    input_set_capability(input, EV_KEY, KEY_WAKEUP);

	device_init_wakeup(&pdev->dev, 1);

	return 0;

fail2:
    pr_err("gpio-remotectl input_allocate_device fail\n");
	input_free_device(input);
	kfree(ddata);
fail1:
    pr_err("gpio-remotectl gpio irq request fail\n");
    free_irq(gpio_to_irq(pdata->gpio), ddata);
    del_timer_sync(&ddata->timer);
    tasklet_kill(&ddata->remote_tasklet); 
    gpio_free(pdata->gpio);
fail0: 
    pr_err("gpio-remotectl input_register_device fail\n");
    platform_set_drvdata(pdev, NULL);

	return error;
}

static int __devexit remotectl_remove(struct platform_device *pdev)
{
	struct RKxx_remotectl_platform_data *pdata = pdev->dev.platform_data;
	struct rkxx_remotectl_drvdata *ddata = platform_get_drvdata(pdev);
	struct input_dev *input = ddata->input;
    int irq;

	device_init_wakeup(&pdev->dev, 0);
    irq = gpio_to_irq(pdata->gpio);
    free_irq(irq, ddata);
    tasklet_kill(&ddata->remote_tasklet); 
    gpio_free(pdata->gpio);

	input_unregister_device(input);

	return 0;
}


#ifdef CONFIG_PM
static int remotectl_suspend(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct RKxx_remotectl_platform_data *pdata = pdev->dev.platform_data;
    struct rkxx_remotectl_drvdata *ddata = platform_get_drvdata(pdev);
    
    //ddata->remotectl_suspend_data.suspend_flag = 1;
    ddata->remotectl_suspend_data.cnt = 0;

	if (device_may_wakeup(&pdev->dev)) {
		if (pdata->wakeup) {
			int irq = gpio_to_irq(pdata->gpio);
			enable_irq_wake(irq);
		}
	}
    
	return 0;
}

static int remotectl_resume(struct device *dev)
{
	struct platform_device *pdev = to_platform_device(dev);
	struct RKxx_remotectl_platform_data *pdata = pdev->dev.platform_data;

    if (device_may_wakeup(&pdev->dev)) {
        if (pdata->wakeup) {
            int irq = gpio_to_irq(pdata->gpio);
            disable_irq_wake(irq);
        }
    }

	return 0;
}

static const struct dev_pm_ops remotectl_pm_ops = {
	.suspend	= remotectl_suspend,
	.resume		= remotectl_resume,
};
#endif



static struct platform_driver remotectl_device_driver = {
	.probe		= remotectl_probe,
	.remove		= __devexit_p(remotectl_remove),
	.driver		= {
		.name	= "rkxx-remotectl",
		.owner	= THIS_MODULE,
#ifdef CONFIG_PM
	    .pm	= &remotectl_pm_ops,
#endif
	},

};

static int  remotectl_init(void)
{
    printk(KERN_INFO "++++++++remotectl_init\n");
    return platform_driver_register(&remotectl_device_driver);
}


static void  remotectl_exit(void)
{
	platform_driver_unregister(&remotectl_device_driver);
    printk(KERN_INFO "++++++++remotectl_init\n");
}

module_init(remotectl_init);
module_exit(remotectl_exit);

MODULE_LICENSE("GPL");
MODULE_AUTHOR("rockchip");
MODULE_DESCRIPTION("Keyboard driver for CPU GPIOs");
MODULE_ALIAS("platform:gpio-keys1");


