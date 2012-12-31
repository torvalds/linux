/* linux/drivers/media/video/samsung/tvout/s5p_hdmi_hpd.c
 *
 * Copyright (c) 2009 Samsung Electronics
 *		http://www.samsung.com/
 *
 * HPD interface function file for Samsung TVOut driver
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 */

#include <linux/miscdevice.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/poll.h>
#include <linux/switch.h>

#include <plat/tvout.h>
#include <linux/delay.h>
#include <linux/sched.h>

#include "s5p_tvout_common_lib.h"
#include "hw_if/hw_if.h"

#ifdef CONFIG_HPD_DEBUG
#define HPDIFPRINTK(fmt, args...) \
	printk(KERN_INFO "[HPD_IF] %s: " fmt, __func__ , ## args)
#else
#define HPDIFPRINTK(fmt, args...)
#endif

#define VERSION         "1.2" /* Driver version number */
#define HPD_MINOR       243 /* Major 10, Minor 243, /dev/hpd */

#define HPD_LO          0
#define HPD_HI          1

#define HDMI_ON		1
#define HDMI_OFF	0

#define RETRY_COUNT	50

//codewalker
//struct switch_dev switch_hdmi_detection = {
//	.name = "hdmi",
//};

struct hpd_struct {
	spinlock_t lock;
	wait_queue_head_t waitq;
	atomic_t state;
	void (*int_src_hdmi_hpd)(void);
	void (*int_src_ext_hpd)(void);
	int (*read_gpio)(void);
	int irq_n;
};

static struct hpd_struct hpd_struct;

#if defined(CONFIG_MACH_ODROID_4X12)&&defined(CONFIG_SND_SAMSUNG_I2S)
extern void	odroid_audio_tvout(bool tvout);
#endif

static int last_hpd_state;
static int last_uevent_state;
atomic_t hdmi_status;
atomic_t poll_state;

static int s5p_hpd_open(struct inode *inode, struct file *file);
static int s5p_hpd_release(struct inode *inode, struct file *file);
static ssize_t s5p_hpd_read(struct file *file, char __user *buffer,
		size_t count, loff_t *ppos);
static unsigned int s5p_hpd_poll(struct file *file, poll_table *wait);
static long s5p_hpd_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg);

static const struct file_operations hpd_fops = {
	.owner   = THIS_MODULE,
	.open    = s5p_hpd_open,
	.release = s5p_hpd_release,
	.read    = s5p_hpd_read,
	.poll    = s5p_hpd_poll,
	.unlocked_ioctl   = s5p_hpd_ioctl,
};

static struct miscdevice hpd_misc_device = {
	HPD_MINOR,
	"HPD",
	&hpd_fops,
};

static void s5p_hpd_kobject_uevent(void)
{
	char env_buf[120];
	char *envp[2];
	int env_offset = 0;
	int i = 0;
	int hpd_state = atomic_read(&hpd_struct.state);

	HPDIFPRINTK("++\n");
	memset(env_buf, 0, sizeof(env_buf));

	if (hpd_state)	{
		while (on_stop_process && (i < RETRY_COUNT)) {
			HPDIFPRINTK("on_stop_process\n");
			msleep(5);
			i++;
		};
	} else {
		while (on_start_process && (i < RETRY_COUNT)) {
			HPDIFPRINTK("on_start_process\n");
			msleep(5);
			i++;
		};
	}

	if (i == RETRY_COUNT) {
		on_stop_process = false;
		on_start_process = false;
	}

	hpd_state = atomic_read(&hpd_struct.state);
	if (hpd_state) {
		if (last_uevent_state == -1 || last_uevent_state == HPD_LO) {
			sprintf(env_buf, "HDMI_STATE=online");
			envp[env_offset++] = env_buf;
			envp[env_offset] = NULL;
			HPDIFPRINTK("online event\n");
			kobject_uevent_env(&(hpd_misc_device.this_device->kobj), KOBJ_CHANGE, envp);
			on_start_process = true;
		}
		last_uevent_state = HPD_HI;
	#if defined(CONFIG_MACH_ODROID_4X12)&&defined(CONFIG_SND_SAMSUNG_I2S)
		odroid_audio_tvout(1);
	#endif
	} else {
		if (last_uevent_state == -1 || last_uevent_state == HPD_HI) {
			sprintf(env_buf, "HDMI_STATE=offline");
			envp[env_offset++] = env_buf;
			envp[env_offset] = NULL;
			HPDIFPRINTK("offline event\n");
			kobject_uevent_env(&(hpd_misc_device.this_device->kobj), KOBJ_CHANGE, envp);
			on_stop_process = true;
		}
		last_uevent_state = HPD_LO;

	#if defined(CONFIG_MACH_ODROID_4X12)&&defined(CONFIG_SND_SAMSUNG_I2S)
		odroid_audio_tvout(0);
	#endif
	}

	//codewalker
	//switch_set_state(&switch_hdmi_detection, hpd_state);
}

static DECLARE_WORK(hpd_work, (void *)s5p_hpd_kobject_uevent);

static int s5p_hpd_open(struct inode *inode, struct file *file)
{
	atomic_set(&poll_state, 1);

	//codewalker
	//int hpd_state = atomic_read(&hpd_struct.state);
	//switch_set_state(&switch_hdmi_detection, hpd_state);

	return 0;
}

static int s5p_hpd_release(struct inode *inode, struct file *file)
{
	return 0;
}

static ssize_t s5p_hpd_read(struct file *file, char __user *buffer,
			size_t count, loff_t *ppos)
{
	ssize_t retval;
	unsigned long spin_flags;

	spin_lock_irqsave(&hpd_struct.lock, spin_flags);

	retval = put_user(atomic_read(&hpd_struct.state),
		(unsigned int __user *) buffer);

	atomic_set(&poll_state, -1);
	spin_unlock_irqrestore(&hpd_struct.lock, spin_flags);

	return retval;
}

static unsigned int s5p_hpd_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &hpd_struct.waitq, wait);

	if (atomic_read(&poll_state) != -1)
		return POLLIN | POLLRDNORM;

	return 0;
}

#define HPD_GET_STATE _IOR('H', 100, unsigned int)
#define HPD_GET_I2C_CHANNEL _IOR('H', 101, unsigned int)

static long s5p_hpd_ioctl(struct file *file,
		unsigned int cmd, unsigned long arg)
{
	switch (cmd) {
	case HPD_GET_STATE:
		{
		unsigned int *status = (unsigned int *)arg;
		*status = atomic_read(&hpd_struct.state);

		if (last_uevent_state == -1)
			last_uevent_state = *status;

		if (last_uevent_state != *status) {
			on_start_process = false;
			on_stop_process = false;
		}

		HPDIFPRINTK("HPD status is %s\n",
				(*status) ? "plugged" : "unplugged");
		return 0;
		}
	case HPD_GET_I2C_CHANNEL:
		{
		unsigned int *channel= (unsigned int *)arg;
	#if defined(CONFIG_MACH_ODROID_4X12)
			char *string="/dev/i2c-2";
	#else
			char *string="/dev/i2c-1";
	#endif

		HPDIFPRINTK("I2C channel is %d\n",channel);
	
		return	copy_to_user(channel,string,11);
		
		}

	default:
		printk(KERN_ERR "(%d) unknown ioctl, HPD_GET_STATE(%d)\n",
				(unsigned int)cmd, (unsigned int)HPD_GET_STATE);
		return  -EFAULT;
	}

}


int s5p_hpd_set_hdmiint(void)
{
	/* EINT -> HDMI */

	HPDIFPRINTK("\n");
	irq_set_irq_type(hpd_struct.irq_n, IRQ_TYPE_NONE);

	if (last_hpd_state)
		s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_UNPLUG, 0);
	else
		s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_PLUG, 0);

	atomic_set(&hdmi_status, HDMI_ON);

	hpd_struct.int_src_hdmi_hpd();

	s5p_hdmi_reg_hpd_gen();

	if (s5p_hdmi_reg_get_hpd_status())
		s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_UNPLUG, 1);
	else
		s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_PLUG, 1);

	return 0;
}

int s5p_hpd_set_eint(void)
{
	HPDIFPRINTK("\n");
	/* HDMI -> EINT */
	atomic_set(&hdmi_status, HDMI_OFF);

	s5p_hdmi_reg_intc_clear_pending(HDMI_IRQ_HPD_PLUG);
	s5p_hdmi_reg_intc_clear_pending(HDMI_IRQ_HPD_UNPLUG);

	s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_PLUG, 0);
	s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_UNPLUG, 0);

	hpd_struct.int_src_ext_hpd();

	return 0;
}

int s5p_hpd_get_status(void)
{
	int hpd_state = atomic_read(&hpd_struct.state);
	return hpd_state;

}
EXPORT_SYMBOL_GPL(s5p_hpd_get_status);

static int s5p_hdp_irq_eint(int irq)
{

	HPDIFPRINTK("\n");

	if (hpd_struct.read_gpio()) {
		HPDIFPRINTK("gpio is high\n");
		irq_set_irq_type(hpd_struct.irq_n, IRQ_TYPE_LEVEL_LOW);
		if (atomic_read(&hpd_struct.state) == HPD_HI)
			return IRQ_HANDLED;

		atomic_set(&hpd_struct.state, HPD_HI);
		atomic_set(&poll_state, 1);

		last_hpd_state = HPD_HI;
		wake_up_interruptible(&hpd_struct.waitq);
	} else {
		HPDIFPRINTK("gpio is low\n");
		irq_set_irq_type(hpd_struct.irq_n, IRQ_TYPE_LEVEL_HIGH);
		if (atomic_read(&hpd_struct.state) == HPD_LO)
			return IRQ_HANDLED;

		atomic_set(&hpd_struct.state, HPD_LO);
		atomic_set(&poll_state, 1);

		last_hpd_state = HPD_LO;

		wake_up_interruptible(&hpd_struct.waitq);
	}
	schedule_work(&hpd_work);

	HPDIFPRINTK("%s\n", atomic_read(&hpd_struct.state) == HPD_HI ?
		"HPD HI" : "HPD LO");

	return IRQ_HANDLED;
}

static int s5p_hpd_irq_hdmi(int irq)
{
	u8 flag;
	int ret = IRQ_HANDLED;
	HPDIFPRINTK("\n");

	/* read flag register */
	flag = s5p_hdmi_reg_intc_status();

	if (s5p_hdmi_reg_get_hpd_status())
		s5p_hdmi_reg_intc_clear_pending(HDMI_IRQ_HPD_PLUG);
	else
		s5p_hdmi_reg_intc_clear_pending(HDMI_IRQ_HPD_UNPLUG);

	s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_UNPLUG, 0);
	s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_PLUG, 0);

	/* is this our interrupt? */
	if (!(flag & (1 << HDMI_IRQ_HPD_PLUG | 1 << HDMI_IRQ_HPD_UNPLUG))) {
		ret = IRQ_NONE;

		goto out;
	}

	if (flag == (1 << HDMI_IRQ_HPD_PLUG | 1 << HDMI_IRQ_HPD_UNPLUG)) {
		HPDIFPRINTK("HPD_HI && HPD_LO\n");

		if (last_hpd_state == HPD_HI && s5p_hdmi_reg_get_hpd_status())
			flag = 1 << HDMI_IRQ_HPD_UNPLUG;
		else
			flag = 1 << HDMI_IRQ_HPD_PLUG;
	}

	if (flag & (1 << HDMI_IRQ_HPD_PLUG)) {
		s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_UNPLUG, 1);
		if (atomic_read(&hpd_struct.state) == HPD_HI)
			return IRQ_HANDLED;

		atomic_set(&hpd_struct.state, HPD_HI);
		atomic_set(&poll_state, 1);

		last_hpd_state = HPD_HI;
		wake_up_interruptible(&hpd_struct.waitq);

		HPDIFPRINTK("HPD_HI\n");

	} else if (flag & (1 << HDMI_IRQ_HPD_UNPLUG)) {
		s5p_hdcp_stop();

		s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_PLUG, 1);
		if (atomic_read(&hpd_struct.state) == HPD_LO)
			return IRQ_HANDLED;

		atomic_set(&hpd_struct.state, HPD_LO);
		atomic_set(&poll_state, 1);

		last_hpd_state = HPD_LO;

		wake_up_interruptible(&hpd_struct.waitq);

		HPDIFPRINTK("HPD_LO\n");
	}

	schedule_work(&hpd_work);

out:
	return IRQ_HANDLED;
}

/*
 * HPD interrupt handler
 *
 * Handles interrupt requests from HPD hardware.
 * Handler changes value of internal variable and notifies waiting thread.
 */
static irqreturn_t s5p_hpd_irq_handler(int irq, void *dev_id)
{
	irqreturn_t ret = IRQ_HANDLED;

	HPDIFPRINTK("\n");
	/* check HDMI status */
	if (atomic_read(&hdmi_status)) {
		/* HDMI on */
		ret = s5p_hpd_irq_hdmi(irq);
		HPDIFPRINTK("HDMI HPD interrupt\n");
	} else {
		/* HDMI off */
		ret = s5p_hdp_irq_eint(irq);
		HPDIFPRINTK("EINT HPD interrupt\n");
	}

	return ret;
}

static int __devinit s5p_hpd_probe(struct platform_device *pdev)
{
	struct s5p_platform_hpd *pdata;
	int ret;

	if (misc_register(&hpd_misc_device)) {
		printk(KERN_WARNING " Couldn't register device 10, %d.\n",
			HPD_MINOR);

		return -EBUSY;
	}

	init_waitqueue_head(&hpd_struct.waitq);

	spin_lock_init(&hpd_struct.lock);

	atomic_set(&hpd_struct.state, -1);

	atomic_set(&hdmi_status, HDMI_OFF);

	pdata = to_tvout_plat(&pdev->dev);

	if (pdata->int_src_hdmi_hpd)
		hpd_struct.int_src_hdmi_hpd =
			(void (*)(void))pdata->int_src_hdmi_hpd;
	if (pdata->int_src_ext_hpd)
		hpd_struct.int_src_ext_hpd =
			(void (*)(void))pdata->int_src_ext_hpd;
	if (pdata->read_gpio)
		hpd_struct.read_gpio = (int (*)(void))pdata->read_gpio;

	hpd_struct.irq_n = platform_get_irq(pdev, 0);

	hpd_struct.int_src_ext_hpd();
	if (hpd_struct.read_gpio()) {
		atomic_set(&hpd_struct.state, HPD_HI);
		last_hpd_state = HPD_HI;
	} else {
		atomic_set(&hpd_struct.state, HPD_LO);
		last_hpd_state = HPD_LO;
	}

	irq_set_irq_type(hpd_struct.irq_n, IRQ_TYPE_EDGE_BOTH);

	ret = request_irq(hpd_struct.irq_n, (irq_handler_t)s5p_hpd_irq_handler,
		IRQF_DISABLED, "hpd", (void *)(&pdev->dev));

	if (ret) {
		printk(KERN_ERR  "failed to install hpd irq\n");
		misc_deregister(&hpd_misc_device);
		return -EIO;
	}

	s5p_hdmi_reg_intc_set_isr(s5p_hpd_irq_handler,
					(u8)HDMI_IRQ_HPD_PLUG);
	s5p_hdmi_reg_intc_set_isr(s5p_hpd_irq_handler,
					(u8)HDMI_IRQ_HPD_UNPLUG);

	last_uevent_state = -1;

	//codewalker
	//switch_dev_register(&switch_hdmi_detection);

	return 0;
}

static int __devexit s5p_hpd_remove(struct platform_device *pdev)
{
	//codewalker
	//switch_dev_unregister(&switch_hdmi_detection);
	return 0;
}

#ifdef CONFIG_PM
static int s5p_hpd_suspend(struct platform_device *dev, pm_message_t state)
{
	hpd_struct.int_src_ext_hpd();
	return 0;
}

static int s5p_hpd_resume(struct platform_device *dev)
{
	if (atomic_read(&hdmi_status) == HDMI_ON)
		hpd_struct.int_src_hdmi_hpd();

	return 0;
}
#else
#define s5p_hpd_suspend NULL
#define s5p_hpd_resume NULL
#endif

static struct platform_driver s5p_hpd_driver = {
	.probe		= s5p_hpd_probe,
	.remove		= __devexit_p(s5p_hpd_remove),
	.suspend	= s5p_hpd_suspend,
	.resume		= s5p_hpd_resume,
	.driver		= {
		.name	= "s5p-tvout-hpd",
		.owner	= THIS_MODULE,
	},
};

static char banner[] __initdata =
	"S5P HPD Driver, (c) 2009 Samsung Electronics\n";

static int __init s5p_hpd_init(void)
{
	int ret;

	printk(banner);

	ret = platform_driver_register(&s5p_hpd_driver);

	if (ret) {
		printk(KERN_ERR "Platform Device Register Failed %d\n", ret);

		return -1;
	}

	return 0;
}

static void __exit s5p_hpd_exit(void)
{
	misc_deregister(&hpd_misc_device);
}

module_init(s5p_hpd_init);
module_exit(s5p_hpd_exit);
