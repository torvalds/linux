/*
 * linux/drivers/input/so340010.c
 *
 * so340010 Keypad Driver
 *
 * Copyright (C) 2010 Amlogic Corporation
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
#include <linux/i2c/so340010.h>

#define DRIVER_NAME	"so340010"

#define SO340010_REG_GENERAL_CONFIG	0x0001
#define SO340010_REG_GPIO_STATE			0x0108
#define SO340010_REG_BUTTON_STATE		0x0109
#define SO340010_REG_NUM				74

#define SO340010_SLEEP		((unsigned short)(0x0020))
#define SO340010_AWAKE		((unsigned short)(0x00A0))

/* periodic polling delay and period */
#define KP_POLL_DELAY		(1 * 1000000)
#define KP_POLL_PERIOD		(5 * 1000000)

struct so340010 {
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
	int last_read;
};

static struct so340010 *gp_kp=NULL;

struct so340010_register {
	unsigned short address;
	unsigned short value;
};
static struct so340010_register so340010_register_init_table[] = {
	{ 0x0000, (0x00<<8) | 0x07},
	{ 0x0001, (0x00<<8) | 0x20},
	{ 0x0004, (0x00<<8) | 0x0F},
	{ 0x0010, (0xA0<<8) | 0xA0},
	{ 0x0011, (0xA0<<8) | 0xA0},
};

static int so340010_read_reg(struct i2c_client *client, u16 addr)
{
	int ret;
	u8 buf[2] = { addr>>8, addr&0xff};
	struct i2c_msg msg[2] = {
		[0] = {
			.addr = client->addr,
			.flags = !I2C_M_RD,
			.len = 2,
			.buf = buf,
		},
		[1] = {
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = 2,
			.buf = buf,
		},
	};
		
	ret = i2c_transfer(client->adapter, msg, ARRAY_SIZE(msg));
	if (ret >= 0) {
		ret = (buf[0] << 8) | buf[1];
	}
	return ret;
}

static int so340010_write_reg(struct i2c_client *client, u16 addr,  u16 data)
{
	u8 buf[4] = { addr>>8, addr&0xff, data>>8, data&0xff};
	struct i2c_msg msg = {
		.addr = client->addr,
		.flags = !I2C_M_RD,
		.len = 4,
		.buf = buf,
	};
	
	return i2c_transfer(client->adapter, &msg, 1);
}

static int so340010_reset(struct i2c_client *client)
{
	int i, ret;
	unsigned short gpio_val, button_val;
	struct so340010_register *reg = so340010_register_init_table;

	printk("client = %x\n", client);
	for (i = 0; i < ARRAY_SIZE(so340010_register_init_table); i++) {
		ret = so340010_read_reg(client, reg->address);
		printk("before write: register #%x  = %x\n", reg->address, ret);
		ret = so340010_write_reg(client, reg->address, reg->value);
		if (ret < 0) {
			printk("write register #%x failed\n", reg->address);
			return -EINVAL;
		}
		ret = so340010_read_reg(client, reg->address);
		printk("after write: register #%x  = %x\n", reg->address, ret);		
		reg++;
	}

	gpio_val = so340010_read_reg(client, SO340010_REG_GPIO_STATE);
	printk("register #0x0108 = %x\n",  gpio_val);
	button_val = so340010_read_reg(client, SO340010_REG_BUTTON_STATE);
	printk("register #0x0109 = %x\n",  button_val);
	if ((gpio_val < 0) || (button_val < 0))
		return -EINVAL;
	return 0;
}

static int so340010_sleep(struct i2c_client *client,  bool sleep) 
{
	if (sleep) {
		return so340010_write_reg(client, SO340010_REG_GENERAL_CONFIG,
				SO340010_SLEEP);
	}
	else {
		return so340010_write_reg(client, SO340010_REG_GENERAL_CONFIG,
				SO340010_AWAKE);
	}
}


static void so340010_work(struct work_struct *work)
{
	int i;
	int gpio_val = 0;
	int button_val = 0;
	struct so340010 *kp;
	struct cap_key *key;

	kp = (struct so340010 *)container_of(work, struct so340010, work);
	gpio_val = so340010_read_reg(kp->client, SO340010_REG_GPIO_STATE);
	button_val = so340010_read_reg(kp->client, SO340010_REG_BUTTON_STATE);
	if ((gpio_val < 0 ) || (button_val < 0) || (button_val >> 4)){
		so340010_reset(kp->client);
		return;
	}
	printk(KERN_INFO "gpio_val=0x%04x, button_val = 0x%04x\r\n", gpio_val, button_val);

	key = kp->key;
	for (i = 0; i < kp->key_num; i++) {
		if (button_val & key->mask) {
			if (!(kp->pending_keys & key->mask)) {
				kp->pending_keys |= key->mask;
				input_report_key(kp->input, key->code, 1);
				printk(KERN_INFO"%s key(%d) pressed\n", key->name, key->code);
			}
		}
		else if (kp->pending_keys & key->mask) {
			input_report_key(kp->input, key->code, 0);
			kp->pending_keys &= ~(key->mask);
			printk(KERN_INFO"%s key(%d) released\n", key->name, key->code);
		}
		key++;
	}
}

static struct input_dev* so340010_register_input(struct cap_key *key, int key_num)
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
		input->phys = "so340010/input0";
//		input->dev.parent = &pdev->dev;
		input->id.bustype = BUS_ISA;
		input->id.vendor = 0x0001;
		input->id.product = 0x0001;
		input->id.version = 0x0100;	
		input->rep[REP_DELAY]=0xffffffff;
		input->rep[REP_PERIOD]=0xffffffff;
		input->keycodesize = sizeof(unsigned short);
		input->keycodemax = 0x1ff;
	
		if (input_register_device(input) < 0) {
			printk(KERN_ERR "so340010 register input device failed\n");
			input_free_device(input);
			input = 0;
		}
		else {
			printk("so340010 register input device completed\n");
		}
	}
	return input;
}

/**
 * so340010_timer() - timer callback function
 * @timer: timer that caused this function call
 */
static enum hrtimer_restart so340010_timer(struct hrtimer *timer)
{
	struct so340010 *kp = (struct so340010*)container_of(timer, struct so340010, timer);
	unsigned long flags = 0;
	
	spin_lock_irqsave(&kp->lock, flags);
	if (!kp->get_irq_level() &&	((jiffies_to_msecs(jiffies) - kp->last_read) > 200)) {
		queue_work(kp->workqueue, &kp->work);
		kp->last_read = jiffies_to_msecs(jiffies);
	}
	spin_unlock_irqrestore(&kp->lock, flags);
	hrtimer_start(&kp->timer, ktime_set(0, KP_POLL_PERIOD), HRTIMER_MODE_REL);
	return HRTIMER_NORESTART;
}

static irqreturn_t so340010_interrupt(int irq, void *context)
{
	struct so340010 *kp = (struct so340010 *)context;
	unsigned long flags;
	
	spin_lock_irqsave(&kp->lock, flags);
	printk(KERN_INFO "enter penirq\n");
	/* if the attn low, disable IRQ and start timer chain */
	if (!kp->get_irq_level()) {
		disable_irq_nosync(kp->client->irq);
		hrtimer_start(&kp->timer, ktime_set(0, KP_POLL_DELAY), HRTIMER_MODE_REL);
	}
	spin_unlock_irqrestore(&kp->lock, flags);
	return IRQ_HANDLED;
}

static int so340010_config_open(struct inode *inode, struct file *file)
{
	file->private_data = gp_kp;
	return 0;
}

static int so340010_config_release(struct inode *inode, struct file *file)
{
	file->private_data=NULL;
	return 0;
}

static const struct file_operations so340010_fops = {
	.owner      = THIS_MODULE,
	.open       = so340010_config_open,
	.ioctl      = NULL,
	.release    = so340010_config_release,
};

static int so340010_register_device(struct so340010 *kp)
{
	int ret=0;
	strcpy(kp->config_name,DRIVER_NAME);
	ret=register_chrdev(0, kp->config_name, &so340010_fops);
	if(ret<=0) {
		printk("register char device error\r\n");
		return  ret ;
	}
	kp->config_major=ret;
	printk("so340010 major:%d\r\n",ret);
	kp->config_class=class_create(THIS_MODULE,kp->config_name);
	kp->config_dev=device_create(kp->config_class,	NULL,
	MKDEV(kp->config_major,0),NULL,kp->config_name);
	return ret;
}

static int so340010_probe(struct i2c_client *client, const struct i2c_device_id *id)
{
	int err;
	struct so340010 *kp;
	struct so340010_platform_data *pdata;
	
	pdata = client->dev.platform_data;
	if (!pdata) {
		dev_err(&client->dev, "so340010 require platform data!\n");
		return  -EINVAL;
	}
   
	kp = kzalloc(sizeof(struct so340010), GFP_KERNEL);
	if (!kp) {
		dev_err(&client->dev, "so340010 alloc data failed!\n");
		return -ENOMEM;
	}
	kp->last_read = 0;
	kp->client = client;
	kp->key = pdata->key;
	kp->key_num = pdata->key_num;
	kp->pending_keys = 0;
	kp->input = so340010_register_input(kp->key, kp->key_num);
	if (!kp->input) {
		err =  -EINVAL;
		goto fail_irq;
	}

	so340010_reset(client);
	
	hrtimer_init(&kp->timer, CLOCK_MONOTONIC, HRTIMER_MODE_REL);
	kp->timer.function = so340010_timer;
	INIT_WORK(&kp->work, so340010_work);
	kp->workqueue = create_singlethread_workqueue(DRIVER_NAME);
	if (kp->workqueue == NULL) {
		dev_err(&client->dev, "so340010 can't create work queue\n");
		err = -ENOMEM;
		goto fail;
	}

	if (!pdata->init_irq || !pdata->get_irq_level) {
		err = -ENOMEM;
		goto fail;
	}
	kp->get_irq_level = pdata->get_irq_level;
	pdata->init_irq();
	err = request_irq(client->irq, so340010_interrupt, IRQF_TRIGGER_FALLING,
                       client->dev.driver->name, kp);
	if (err) {
		dev_err(&client->dev, "failed to request IRQ#%d: %d\n", client->irq, err);
		goto fail_irq;
	}

	gp_kp=kp;
	i2c_set_clientdata(client, kp);
	so340010_register_device(gp_kp);
	return 0;

fail_irq:
	free_irq(client->irq, client);

fail:
	kfree(kp);
	return err;
}

static int so340010_remove(struct i2c_client *client)
{
	struct so340010 *kp = i2c_get_clientdata(client);

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

static const struct i2c_device_id so340010_ids[] = {
       { DRIVER_NAME, 0 },
       { }
};

static struct i2c_driver so340010_driver = {
	.driver = {
		.name = DRIVER_NAME,
		.owner = THIS_MODULE,
	},
	.probe = so340010_probe,
	.remove = so340010_remove,
//	.suspend = so340010_suspend,
//	.resume = so340010_resume,
	.id_table = so340010_ids,
};

static int __init so340010_init(void)
{
	printk(KERN_INFO "so340010 init\n");
	return i2c_add_driver(&so340010_driver);
}

static void __exit so340010_exit(void)
{
	printk(KERN_INFO "so340010 exit\n");
	i2c_del_driver(&so340010_driver);
}

module_init(so340010_init);
module_exit(so340010_exit);

MODULE_AUTHOR("");
MODULE_DESCRIPTION("so340010 driver");
MODULE_LICENSE("GPL");




