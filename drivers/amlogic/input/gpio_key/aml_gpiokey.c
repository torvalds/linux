/*
 * drivers/amlogic/input/gpio_keypad/gpio_keypad.c
 *
 * GPIO Keypad Driver
 *
 * Copyright (C) 2010 Amlogic, Inc.
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
 * author :   Robin Zhu
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/interrupt.h>
#include <linux/types.h>
#include <linux/input.h>
#include <linux/kernel.h>
#include <linux/delay.h>
#include <linux/platform_device.h>
#include <linux/mutex.h>
#include <linux/errno.h>
#include <linux/irq.h>

#include <asm/irq.h>
#include <asm/io.h>
#include <mach/gpio.h>
#include <uapi/linux/input.h>
#include <mach/am_regs.h>
#include <mach/pinmux.h>
#include <linux/major.h>
#include <linux/slab.h>
#include <asm/uaccess.h>
#include <linux/of.h>
#include <linux/amlogic/aml_gpio_consumer.h>
#include <linux/switch.h>
#include <plat/wakeup.h>

#define MOD_NAME       "gpio_key"
#define USE_IRQ     1

struct gpio_key{
	int code;	  /* input key code */
	const char *name;
	int pin;    /*pin number*/
	int status; /*0 up, 1 down*/
};

struct gpio_platform_data{
	
	struct gpio_key *key;
	int key_num;
	int repeat_delay;
	int repeat_period;
#ifdef USE_IRQ
    int irq_keyup;
    int irq_keydown;
#endif
};

struct kp {
	
	struct input_dev *input;
	struct timer_list timer;
	int config_major;
	char config_name[20];
	struct class *config_class;
	struct device *config_dev;
	struct gpio_key *keys;
	int key_num;
	struct work_struct work_update;
};

#ifndef CONFIG_OF
#define CONFIG_OF
#endif

static struct kp *gp_kp=NULL;

//static int timer_count = 0;

static void kp_work(struct kp *kp)
{
	struct gpio_key *key;
	int i;
	int io_status;
	
	key = kp->keys;
	for (i=0; i<kp->key_num; i++) {
		io_status = amlogic_get_value(key->pin, MOD_NAME);
		//printk("get gpio key status %s(%d)\n",key->name, io_status);
		if(io_status != key->status ){
			if (io_status) {
				printk("key %d up\n", key->code);
				input_report_key(kp->input, key->code, 0);
				input_sync(kp->input);
			}
			else {
				printk("key %d down\n", key->code);
				input_report_key(kp->input, key->code, 1);
				input_sync(kp->input);
			}
			key->status = io_status;
		}
		key++;
	}
}

static void update_work_func(struct work_struct *work)
{
    struct kp *kp_data = container_of(work, struct kp, work_update);

    kp_work(kp_data);
}

/***What we do here is just for loss wakeup key when suspend. 
	In suspend routine, the intr is disable.			*******/
//we need do more things to adapt the gpio change.
int det_pwr_key(void)
{
	return (aml_read_reg32(P_AO_IRQ_STAT) & (1<<8));
}
/*Enable gpio interrupt for AO domain interrupt*/
void set_pwr_key(void)
{
	aml_write_reg32(P_AO_IRQ_GPIO_REG,aml_read_reg32(P_AO_IRQ_GPIO_REG)|(1<<18) | (1<<16) | (0x3<<0));
	aml_write_reg32(P_AO_IRQ_MASK_FIQ_SEL,aml_read_reg32(P_AO_IRQ_MASK_FIQ_SEL)|(1<<8));
	aml_set_reg32_mask(P_AO_IRQ_STAT_CLR,1<<8);

}

void clr_pwr_key(void)
{
	aml_set_reg32_mask(P_AO_IRQ_STAT_CLR,1<<8);
}

extern int deep_suspend_flag;


#ifdef USE_IRQ

static irqreturn_t kp_isr(int irq, void *data)
{
    struct kp *kp_data=(struct kp *)data;
    schedule_work(&(kp_data->work_update));

	if(!deep_suspend_flag)
		clr_pwr_key();
    return IRQ_HANDLED;
}

#else
void kp_timer_sr(unsigned long data)
{
    struct kp *kp_data=(struct kp *)data;
    schedule_work(&(kp_data->work_update));

    if(!deep_suspend_flag)
                clr_pwr_key();

    mod_timer(&kp_data->timer,jiffies+msecs_to_jiffies(25));
}
#endif
static int gpio_key_config_open(struct inode *inode, struct file *file)
{
    file->private_data = gp_kp;
    return 0;
}

static int gpio_key_config_release(struct inode *inode, struct file *file)
{
    file->private_data=NULL;
    return 0;
}

static const struct file_operations keypad_fops = {
    .owner      = THIS_MODULE,
    .open       = gpio_key_config_open,
    .release    = gpio_key_config_release,
};

static int register_keypad_dev(struct kp  *kp)
{
    int ret=0;
    strcpy(kp->config_name,"gpio_keyboard");
    ret=register_chrdev(0, kp->config_name, &keypad_fops);
    if(ret<=0)
    {
        printk("register char device error\r\n");
        return  ret ;
    }
    kp->config_major=ret;
    printk("gpio keypad major:%d\r\n",ret);
    kp->config_class=class_create(THIS_MODULE,kp->config_name);
    kp->config_dev=device_create(kp->config_class,	NULL,
    		MKDEV(kp->config_major,0),NULL,kp->config_name);
    return ret;
}

static int gpio_key_probe(struct platform_device *pdev)
{
	const char* str;
    struct kp *kp;
    struct input_dev *input_dev;
    int i, ret, key_size;
    struct gpio_key *key;
    struct gpio_platform_data *pdata = NULL;
    int *key_param = NULL;
	int state=-EINVAL;
#ifdef USE_IRQ
    int irq_keyup;
    int irq_keydown;
#endif
    int gpio_highz = 0;

		printk("==%s==\n", __func__);

	  if (!pdev->dev.of_node) {
				printk("gpio_key: pdev->dev.of_node == NULL!\n");
				state = -EINVAL;
				goto get_key_node_fail;
		}
		ret = of_property_read_u32(pdev->dev.of_node,"key_num",&key_size);
    if (ret) {
		  printk("gpio_key: faild to get key_num!\n");
		  state = -EINVAL;
		  goto get_key_node_fail;
	  }

    pdata = kzalloc(sizeof(*pdata), GFP_KERNEL);
    if (!pdata) {
        dev_err(&pdev->dev, "platform data is required!\n");
        state = -EINVAL;
        goto get_key_node_fail;
    }
   
    ret = of_property_read_bool(pdev->dev.of_node, "gpio_high_z");
    if (ret) {
        gpio_highz = 1;     
        printk("gpio request set to High-Z status\n");
    }

		pdata->key = kzalloc(sizeof(*(pdata->key))*key_size, GFP_KERNEL);
		if (!(pdata->key)) {
			dev_err(&pdev->dev, "platform key is required!\n");
			goto get_key_mem_fail;
		}

		pdata->key_num = key_size;
    for (i=0; i<key_size; i++) {
				ret = of_property_read_string_index(pdev->dev.of_node, "key_name", i, &(pdata->key[i].name));
				if(ret < 0){
					printk("gpio_key: find key_name=%d finished\n", i);
					break;
				}
		}
		key_param = kzalloc((sizeof(*key_param))*(pdata->key_num), GFP_KERNEL);
    if(!key_param) {
			printk("gpio_key: key_param can not get mem\n");
			goto get_param_mem_fail;
		}
    ret = of_property_read_u32_array(pdev->dev.of_node,"key_code",key_param, pdata->key_num);
    if (ret) {
		  printk("gpio_key: faild to get key_code!\n");
		  goto get_key_param_failed;
	  }

	  for (i=0; i<pdata->key_num; i++) {
			pdata->key[i].code = *(key_param+i);
			pdata->key[i].status = -1;
	  }

#ifdef USE_IRQ
    ret = of_property_read_u32(pdev->dev.of_node,"irq_keyup",&irq_keyup);
    ret |= of_property_read_u32(pdev->dev.of_node,"irq_keydown",&irq_keydown);
    if (ret) {
        printk(KERN_INFO "Failed to get key irq number from dts.\n");
        goto get_key_param_failed;
	}
    else
    {
        pdata->irq_keyup = irq_keyup;
        pdata->irq_keydown = irq_keydown;
    }

#endif

    for (i=0; i<key_size; i++) {
				ret = of_property_read_string_index(pdev->dev.of_node, "key_pin", i, &str);
				//printk("gpio_key: %d name(%s) pin(%s)\n",i, (pdata->key[i].name), str);
				if(ret < 0){
					printk("gpio_key: find key_name=%d finished\n", i);
					break;
				}
				ret = amlogic_gpio_name_map_num(str);
				//printk("amlogic_gpio_name_map_num pin %d!\n", ret);
				if (ret < 0) {
					printk("gpio_key bad pin !\n");
		  		goto get_key_param_failed;
				}
				//printk("gpio_key: %d %s(%d)\n",i,(pdata->key[i].name), ret);
				pdata->key[i].pin = ret;
				
				amlogic_gpio_request(pdata->key[i].pin, MOD_NAME);
                if (!gpio_highz) {
    				amlogic_gpio_direction_input(pdata->key[i].pin, MOD_NAME);
	    			amlogic_set_pull_up_down(pdata->key[i].pin, 1, MOD_NAME);
                }

#ifdef USE_IRQ
                amlogic_gpio_to_irq(pdata->key[i].pin, MOD_NAME,
                    AML_GPIO_IRQ(irq_keyup, FILTER_NUM7,GPIO_IRQ_RISING));

                amlogic_gpio_to_irq(pdata->key[i].pin, MOD_NAME,
                    AML_GPIO_IRQ(irq_keydown, FILTER_NUM7,GPIO_IRQ_FALLING));
#endif
		}

    kp = kzalloc(sizeof(struct kp), GFP_KERNEL);
    input_dev = input_allocate_device();
    if (!kp || !input_dev) {
        kfree(kp);
        input_free_device(input_dev);
        state = -ENOMEM;
        goto get_key_param_failed;
    }
    gp_kp=kp;

    platform_set_drvdata(pdev, pdata);
    kp->input = input_dev;
     
    INIT_WORK(&(kp->work_update), update_work_func);
#ifdef USE_IRQ

    if(request_irq(irq_keyup + INT_GPIO_0, kp_isr, IRQF_DISABLED, "irq_keyup", kp))
    {
        printk(KERN_INFO "Failed to request gpio key up irq.\n");
        kfree(kp);
        input_free_device(input_dev);
        state = -EINVAL;
        goto get_key_param_failed;
    }

    if(request_irq(irq_keydown + INT_GPIO_0, kp_isr, IRQF_DISABLED, "irq_keydown", kp))
    {
        printk(KERN_INFO "Failed to request gpio key down irq.\n");
        kfree(kp);
        input_free_device(input_dev);
        state = -EINVAL;
        goto get_key_param_failed;
    }
#else
    setup_timer(&kp->timer, kp_timer_sr, (unsigned int)kp) ;
    mod_timer(&kp->timer, jiffies+msecs_to_jiffies(100));
#endif
    /* setup input device */
    set_bit(EV_KEY, input_dev->evbit);
    set_bit(EV_REP, input_dev->evbit);
        
    kp->keys = pdata->key;
    kp->key_num = pdata->key_num;

    key = pdata->key;

    for (i=0; i<kp->key_num; i++) {
        set_bit(key->code, input_dev->keybit);
        printk(KERN_INFO "%s key(%d) registed.\n", key->name, key->code);
    }
    
    input_dev->name = "gpio_keypad";
    input_dev->phys = "gpio_keypad/input0";
    input_dev->dev.parent = &pdev->dev;

    input_dev->id.bustype = BUS_ISA;
    input_dev->id.vendor = 0x0001;
    input_dev->id.product = 0x0001;
    input_dev->id.version = 0x0100;

    input_dev->rep[REP_DELAY]=0xffffffff;
    input_dev->rep[REP_PERIOD]=0xffffffff;

    input_dev->keycodesize = sizeof(unsigned short);
    input_dev->keycodemax = 0x1ff;

    ret = input_register_device(kp->input);
    if (ret < 0) {
        printk(KERN_ERR "Unable to register keypad input device.\n");
		    kfree(kp);
		    input_free_device(input_dev);
		    state = -EINVAL;
		    goto get_key_param_failed;
    }
	set_pwr_key();
    printk("gpio keypad register input device completed.\r\n");
    register_keypad_dev(gp_kp);
    kfree(key_param);
    return 0;

    get_key_param_failed:
			kfree(key_param);
    get_param_mem_fail:
			kfree(pdata->key);
    get_key_mem_fail:
			kfree(pdata);
    get_key_node_fail:
    return state;
}

static int gpio_key_remove(struct platform_device *pdev)
{
    struct gpio_platform_data *pdata = platform_get_drvdata(pdev);
    struct kp *kp = gp_kp;

    input_unregister_device(kp->input);
    input_free_device(kp->input);
    unregister_chrdev(kp->config_major,kp->config_name);
    if(kp->config_class)
    {
        if(kp->config_dev)
        device_destroy(kp->config_class,MKDEV(kp->config_major,0));
        class_destroy(kp->config_class);
    }
    kfree(kp);
#ifdef CONFIG_OF
	kfree(pdata->key);
	kfree(pdata);
#endif
    gp_kp=NULL ;
    return 0;
}

static int gpio_key_suspend(struct platform_device *dev, pm_message_t state)
{
	return 0;
}

static int gpio_key_resume(struct platform_device *dev)
{
    printk("gpio_key_resume");
    if (READ_AOBUS_REG(AO_RTI_STATUS_REG2) == FLAG_WAKEUP_PWRKEY) {
			//if( quick_boot_mode == 0 ) 
			{ 
		        	// power button, not alarm
				//printk("gpio_key_resume send KEY_POWER\n");
		        	input_report_key(gp_kp->input, KEY_POWER, 0);
		        	input_sync(gp_kp->input);
		        	input_report_key(gp_kp->input, KEY_POWER, 1);
		        	input_sync(gp_kp->input);
			}	

        WRITE_AOBUS_REG(AO_RTI_STATUS_REG2, 0);

		deep_suspend_flag = 0;

		clr_pwr_key();
    }
    return 0;
}

#ifdef CONFIG_OF
static const struct of_device_id key_dt_match[]={
	{	.compatible = "amlogic,gpio_keypad",
	},
	{},
};
#else
#define key_dt_match NULL
#endif

static struct platform_driver gpio_driver = {
    .probe      = gpio_key_probe,
    .remove     = gpio_key_remove,
    .suspend    = gpio_key_suspend,
    .resume     = gpio_key_resume,
    .driver     = {
        .name   = "gpio-key",
        .of_match_table = key_dt_match,
    },
};

static int __init gpio_key_init(void)
{
    printk(KERN_INFO "GPIO Keypad Driver init.\n");
    return platform_driver_register(&gpio_driver);
}

static void __exit gpio_key_exit(void)
{
    printk(KERN_INFO "GPIO Keypad Driver exit.\n");
    platform_driver_unregister(&gpio_driver);
}

module_init(gpio_key_init);
module_exit(gpio_key_exit);

MODULE_AUTHOR("Frank Chen");
MODULE_DESCRIPTION("GPIO Keypad Driver");
MODULE_LICENSE("GPL");

