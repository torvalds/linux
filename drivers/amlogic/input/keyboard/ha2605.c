/*
 * linux/drivers/input/ha2605.c
 *
 * ha2605 Keypad Driver
 *
 * Copyright (C) 2011 Amlogic Corporation
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
 * author :  
 */

#include <linux/delay.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/device.h>
#include <linux/platform_device.h>
#include <linux/workqueue.h>
#include <linux/gpio.h>
#include <linux/i2c/ha2605.h>

#define DRIVER_NAME	"ha2605"

#define	__HA2605_DEBUG__	0

/* periodic polling delay and period */
#define KP_POLL_DELAY		(40 * 1000000)
#define KP_POLL_PERIOD		(30 * 1000000)

struct ha2605 {
	spinlock_t lock;
	struct i2c_client *client;
	struct input_dev *input;
	struct hrtimer timer;
	int config_major;
	char config_name[20];
	struct class *config_class;
	struct device *config_dev;
	struct work_struct work;
	struct workqueue_struct *workqueue;
	int (*get_irq_level)(void);
	struct cap_key *key;
	int key_num;
	int pending_keys;
	int pending_key_code;
	int last_read;
};

static struct ha2605 *gp_kp=NULL;


static u8 ha2605_init_param_table[] = {
	0x88,0x05,0x8e,0x8e,0x9d,0x9d,0x9d,0xc0,0x00,0x05,0x00,0xbd,
};

static int ha2605_read_data(struct i2c_client *client)
{
	int ret;
	u8 buf[2];
	
	struct i2c_msg msg[1] = {
		[0] = {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 1,
			.buf = buf,
		},
	};

	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret >= 0) {
		ret = buf[0];
	}
	return ret;
}

static int ha2605_write_param(struct i2c_client *client)
{

	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = !I2C_M_RD,
		.len = sizeof(ha2605_init_param_table),
		.buf = ha2605_init_param_table,
	};
	
	printk("ha2605 param table len = %d\n", sizeof(ha2605_init_param_table));
	
	return i2c_transfer(client->adapter, &msg, 1);
}

static int ha2605_reset(struct i2c_client *client)
{
	int ret;

	printk("ha2605 client = %x\n", (u32)client);

	ret = ha2605_write_param(client);

	if (ret < 0) {
		printk("write ha2605 param table fail\n");
		return -EINVAL;
	}

	return 0;
}

#if 0
static int ha2605_sleep(struct i2c_client *client,  bool sleep) 
{
	return 0;
}
#endif

static void ha2605_work(struct work_struct *work)
{
	int i;
	int button_val = 0;
	struct ha2605 *kp;
	struct cap_key *key;

	kp = (struct ha2605 *)container_of(work, struct ha2605, work);
	
	if (!kp->get_irq_level())
	{
		button_val = ha2605_read_data(kp->client);
	}
	else
	{
		button_val = 255;
	}
	
	if ((button_val < 255) && (button_val > 5)){
		ha2605_reset(kp->client);
		return;
	}

#if	__HA2605_DEBUG__
	printk(KERN_INFO "button_val = 0x%04x\r\n", button_val);
#endif

	key = kp->key;
	
	if ((button_val <= 5) && (button_val > 0))
	{
		if(kp->pending_keys)
		{
			if(kp->pending_keys != button_val)
			{
				input_report_key(kp->input, kp->pending_key_code, 0);
				kp->pending_keys = 0;
				printk(KERN_INFO"    key(%d) released\n", kp->pending_key_code);
			}
		}

		if(kp->pending_keys != button_val)
		{
			kp->pending_keys = button_val;
			
			for (i = 0; i < kp->key_num; i++)
			{
				if (button_val == key->mask)
				{
					input_report_key(kp->input, key->code, 1);
					printk(KERN_INFO"%s key(%d) pressed\n", key->name, key->code);
					kp->pending_key_code = key->code;
				
					break;
				}
				
				key++;
			}
		}

//restart:
        hrtimer_start(&kp->timer, ktime_set(0, KP_POLL_PERIOD), HRTIMER_MODE_REL);
	}
	else
	{
		if(button_val == 255)
		{
			if(kp->pending_keys)
			{
				input_report_key(kp->input, kp->pending_key_code, 0);
				kp->pending_keys = 0;
				printk(KERN_INFO"    key(%d) released\n", kp->pending_key_code);

			}
		}
		
        enable_irq(kp->client->irq);
	}
}

static struct input_dev* ha2605_register_input(struct cap_key *key, int key_num)
{
	struct input_dev *input;
	int i;
	
	input = input_allocate_device();
	if (input) {
		/* setup input device */
		set_bit(EV_KEY, input->evbit);
		set_bit(EV_REP, input->evbit);
	
		for (i=0; i<key_num; i++) {
			set_bit(key->code, input->keybit);
			printk(KERN_INFO "%s key(%d) registed.\n", key->name, key->code);
			key++;
		}
    
		input->name = DRIVER_NAME;
		input->phys = "ha2605/input0";
//		input->dev.parent = &pdev->dev;
		input->id.bustype = BUS_ISA;
		input->id.vendor = 0x0015;
		input->id.product = 0x0001;
		input->id.version = 0x0100;	
		input->rep[REP_DELAY]=0xffffffff;
		input->rep[REP_PERIOD]=0xffffffff;
		input->keycodesize = sizeof(unsigned short);
		input->keycodemax = 0x1ff;
	
		if (input_register_device(input) < 0) {
			printk(KERN_ERR "ha2605 register input device failed\n");
			input_free_device(input);
			input = 0;
		}
		else {
			printk("ha2605 register input device completed\n");
		}
	}
	return input;
}

/**
 * ha2605_timer() - timer callback function
 * @timer: timer that caused this function call
 */
static enum hrtimer_restart ha2605_timer(struct hrtimer *timer)
{
	struct ha2605 *kp = (struct ha2605*)container_of(timer, struct ha2605, timer);
	unsigned long flags = 0;
	
    spin_lock_irqsave(&kp->lock, flags);
    queue_work(kp->workqueue, &kp->work);
    spin_unlock_irqrestore(&kp->lock, flags);
    return HRTIMER_NORESTART;
}

static irqreturn_t ha2605_interrupt(int irq, void *context)
{
	struct ha2605 *kp = (struct ha2605 *)context;
	unsigned long flags;
	
	spin_lock_irqsave(&kp->lock, flags);
	printk(KERN_INFO "ha2605 enter irq\n");
    /* if the attn low or data not clear, disable IRQ and start timer chain */
	if ((!kp->get_irq_level()) || (kp->pending_keys)) {
		disable_irq_nosync(kp->client->irq);
		hrtimer_start(&kp->timer, ktime_set(0, KP_POLL_DELAY), HRTIMER_MODE_REL);
	}
	spin_unlock_irqrestore(&kp->lock, flags);
	return IRQ_HANDLED;
}

static int ha2605_config_open(struct inode *inode, struct file *file)
{
	file->private_data = gp_kp;
	return 0;
}

static int ha2605_config_release(struct inode *inode, struct file *file)
{
	file->private_data=NULL;
	return 0;
}

static const struct file_operations ha2605_fops = {
	.owner      = THIS_MODULE,
	.open       = ha2605_config_open,
	.ioctl      = NULL,
	.release    = ha2605_config_release,
};

static int ha2605_register_device(struct ha2605 *kp)
{
	int ret=0;
	strcpy(kp->config_name,DRIVER_NAME);
	ret=register_chrdev(0, kp->config_name, &ha2605_fops);
	if(ret<=0) {
		printk("register char device error\r\n");
		return  ret ;
	}
	kp->config_major=ret;
	printk("ha2605 major:%d\r\n",ret);
	kp->config_class=class_create(THIS_MODULE,kp->config_name);
	kp->config_dev=device_create(kp->config_class,	NULL,
	MKDEV(kp->config_major,0),NULL,kp->config_name);
	return ret;
}

static int ha2605_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int err;
	struct ha2605 *kp;
	struct ha2605_platform_data *pdata;
	
	pdata = client->dev.platform_data;
	if (!pdata) {
		dev_err(&client->dev, "ha2605 require platform data!\n");
		return  -EINVAL;
	}
   
	kp = kzalloc(sizeof(struct ha2605), GFP_KERNEL);
	if (!kp) {
		dev_err(&client->dev, "ha2605 alloc data failed!\n");
		return -ENOMEM;
	}
	kp->last_read = 0;
	kp->client = client;
	kp->key = pdata->key;
	kp->key_num = pdata->key_num;
	kp->pending_keys = 0;
	kp->pending_key_code = 0;
	kp->input = ha2605_register_input(kp->key, kp->key_num);
	if (!kp->input) {
		err =  -EINVAL;
		goto fail_irq;
	}

	ha2605_reset(client);
	
	hrtimer_init(&kp->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	kp->timer.function = ha2605_timer;
	INIT_WORK(&kp->work, ha2605_work);
	kp->workqueue = create_singlethread_workqueue(DRIVER_NAME);
	if (kp->workqueue == NULL) {
		dev_err(&client->dev, "ha2605 can't create work queue\n");
		err = -ENOMEM;
		goto fail;
	}

	if (!pdata->init_irq || !pdata->get_irq_level) {
		err = -ENOMEM;
		goto fail;
	}
	kp->get_irq_level = pdata->get_irq_level;
	pdata->init_irq();
	err = request_irq(client->irq, ha2605_interrupt, IRQF_TRIGGER_RISING,
                       client->dev.driver->name, kp);
	if (err) {
		dev_err(&client->dev, "failed to request IRQ#%d: %d\n", client->irq, err);
		goto fail_irq;
	}

	gp_kp=kp;
	i2c_set_clientdata(client, kp);
	ha2605_register_device(gp_kp);
	return 0;

fail_irq:
	free_irq(client->irq, client);

fail:
	kfree(kp);
	return err;
}

static int ha2605_remove(struct i2c_client *client)
{
	struct ha2605 *kp = i2c_get_clientdata(client);

	free_irq(client->irq, client);
	i2c_set_clientdata(client, NULL);
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
	gp_kp=NULL ;
	return 0;
}

static const struct i2c_device_id ha2605_ids[] = {
       { DRIVER_NAME, 0 },
       { }
};

static struct i2c_driver ha2605_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe = ha2605_probe,
	.remove = ha2605_remove,
//	.suspend = ha2605_suspend,
//	.resume = ha2605_resume,
	.id_table = ha2605_ids,
};

static int __init ha2605_init(void)
{
	printk(KERN_INFO "ha2605 init\n");
	return i2c_add_driver(&ha2605_driver);
}

static void __exit ha2605_exit(void)
{
	printk(KERN_INFO "ha2605 exit\n");
	i2c_del_driver(&ha2605_driver);
}

module_init(ha2605_init);
module_exit(ha2605_exit);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("ha2605 driver");
MODULE_LICENSE("GPL");




