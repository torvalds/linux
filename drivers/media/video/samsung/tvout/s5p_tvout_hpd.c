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

#include <plat/tvout.h>
#include <linux/delay.h>
#include <linux/sched.h>
#ifdef CONFIG_HDMI_SWITCH_HPD
#include <linux/switch.h>
#endif

#include "s5p_tvout_common_lib.h"
#include "hw_if/hw_if.h"

#ifdef CONFIG_TVOUT_DEBUG
#define HPDIFPRINTK(fmt, args...)					\
do {									\
	if (unlikely(tvout_dbg_flag & (1 << DBG_FLAG_HPD))) {		\
		printk(KERN_INFO "[HPD_IF] %s: " fmt,			\
			__func__ , ## args);				\
	}								\
} while (0)
#else
#define HPDIFPRINTK(fmt, args...)
#endif

#define HPDPRINTK(fmt, args...) \
	printk(KERN_INFO "[HPD_IF] %s: " fmt, __func__ , ## args)

#define VERSION         "1.2"	/* Driver version number */
#define HPD_MINOR       243	/* Major 10, Minor 243, /dev/hpd */

#define HPD_LO          0
#define HPD_HI          1

#define HDMI_ON		1
#define HDMI_OFF	0

#define RETRY_COUNT	50

struct hpd_struct {
	spinlock_t lock;
	wait_queue_head_t waitq;
	atomic_t state;
	void (*int_src_hdmi_hpd) (void);
	void (*int_src_ext_hpd) (void);
	int (*read_gpio) (void);
	int irq_n;
#ifdef CONFIG_HDMI_SWITCH_HPD
	struct switch_dev hpd_switch;
#endif
#ifdef CONFIG_HDMI_CONTROLLED_BY_EXT_IC
	void (*ext_ic_control) (bool ic_on);
#endif
};

#ifdef CONFIG_HDMI_CONTROLLED_BY_EXT_IC
static work_func_t  ext_ic_control_func(void) ;
static DECLARE_DELAYED_WORK(ext_ic_control_dwork,
		(work_func_t)ext_ic_control_func);
#endif

static struct hpd_struct hpd_struct;

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
	.owner = THIS_MODULE,
	.open = s5p_hpd_open,
	.release = s5p_hpd_release,
	.read = s5p_hpd_read,
	.poll = s5p_hpd_poll,
	.unlocked_ioctl = s5p_hpd_ioctl,
};

static struct miscdevice hpd_misc_device = {
	HPD_MINOR,
	"HPD",
	&hpd_fops,
};

#ifdef CONFIG_LSI_HDMI_AUDIO_CH_EVENT
	static struct switch_dev g_audio_ch_switch;
#endif

static void s5p_hpd_kobject_uevent(void)
{
	char env_buf[120];
#ifndef CONFIG_HDMI_SWITCH_HPD
	char *envp[2];
	int env_offset = 0;
#endif
	int i = 0;
	int hpd_state = atomic_read(&hpd_struct.state);

	HPDIFPRINTK("++\n");
	memset(env_buf, 0, sizeof(env_buf));

	if (hpd_state) {
		while (on_stop_process && (i < RETRY_COUNT)) {
			HPDIFPRINTK("waiting on_stop_process\n");
			usleep_range(5000, 5000);
			i++;
		};
	} else {
		while (on_start_process && (i < RETRY_COUNT)) {
			HPDIFPRINTK("waiting on_start_process\n");
			usleep_range(5000, 5000);
			i++;
		};
	}

	if (i == RETRY_COUNT) {
		on_stop_process = false;
		on_start_process = false;
		printk(KERN_ERR	"[ERROR] %s() %s fail !!\n", __func__,
			hpd_state ? "on_stop_process" : "on_start_process");
	}

	hpd_state = atomic_read(&hpd_struct.state);
	if (hpd_state) {
		if (last_uevent_state == -1 || last_uevent_state == HPD_LO) {
#ifdef CONFIG_HDMI_CONTROLLED_BY_EXT_IC
			HPDPRINTK("ext_ic power ON\n");
			hpd_struct.ext_ic_control(true);
			msleep(20);
#endif
#ifdef CONFIG_HDMI_SWITCH_HPD
			hpd_struct.hpd_switch.state = 0;
			switch_set_state(&hpd_struct.hpd_switch, 1);
#else
			sprintf(env_buf, "HDMI_STATE=online");
			envp[env_offset++] = env_buf;
			envp[env_offset] = NULL;
			HPDIFPRINTK("online event\n");
			kobject_uevent_env(&(hpd_misc_device.this_device->kobj),
					   KOBJ_CHANGE, envp);
#endif
			HPDPRINTK("[HDMI] HPD event -connect!!!\n");
			if (atomic_read(&hdmi_status) == HDMI_OFF) {
				on_start_process = true;
			} else {
				on_start_process = false;
			}
			HPDIFPRINTK("%s() on_start_process(%d)\n",
					__func__, on_start_process);
		}
		last_uevent_state = HPD_HI;
	} else {
		if (last_uevent_state == -1 || last_uevent_state == HPD_HI) {
#ifdef CONFIG_LSI_HDMI_AUDIO_CH_EVENT
			switch_set_state(&g_audio_ch_switch, (int)-1);
#endif
#ifdef CONFIG_HDMI_SWITCH_HPD
			hpd_struct.hpd_switch.state = 1;
			switch_set_state(&hpd_struct.hpd_switch, 0);
#else
			sprintf(env_buf, "HDMI_STATE=offline");
			envp[env_offset++] = env_buf;
			envp[env_offset] = NULL;
			HPDIFPRINTK("offline event\n");
			kobject_uevent_env(&(hpd_misc_device.this_device->kobj),
					   KOBJ_CHANGE, envp);
#endif
			HPDPRINTK("[HDMI] HPD event -disconnet!!!\n");
			if (atomic_read(&hdmi_status) == HDMI_ON) {
				on_stop_process = true;
			} else {
				on_stop_process = false;
			}
			HPDIFPRINTK("%s() on_stop_process(%d)\n",
					__func__, on_stop_process);

		}
		last_uevent_state = HPD_LO;
	}
}

static DECLARE_WORK(hpd_work, (void *)s5p_hpd_kobject_uevent);

static int s5p_hpd_open(struct inode *inode, struct file *file)
{
	atomic_set(&poll_state, 1);

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
			  (unsigned int __user *)buffer);

	atomic_set(&poll_state, -1);
	spin_unlock_irqrestore(&hpd_struct.lock, spin_flags);

	return retval;
}

static unsigned int s5p_hpd_poll(struct file *file, poll_table * wait)
{
	poll_wait(file, &hpd_struct.waitq, wait);

	if (atomic_read(&poll_state) != -1)
		return POLLIN | POLLRDNORM;

	return 0;
}

#define HPD_GET_STATE _IOR('H', 100, unsigned int)
#define AUDIO_CH_SET_STATE _IOR('H', 101, unsigned int)

#ifdef	CONFIG_LSI_HDMI_AUDIO_CH_EVENT
void hdmi_send_audio_ch_num(
	int supported_ch_num, struct switch_dev *p_audio_ch_switch)
{
	if (last_uevent_state == HPD_LO) {
		printk(KERN_INFO	"[WARNING] %s() "
			"HDMI Audio ch = %d but not send\n",
			__func__, supported_ch_num);
		return;
	} else
		printk(KERN_INFO	"%s() "
			"HDMI Audio ch = %d\n",
			__func__, supported_ch_num);

	p_audio_ch_switch->state = 0;
	switch_set_state(p_audio_ch_switch, (int)supported_ch_num);
}
#endif

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
				HPDIFPRINTK("%s() on_start_process, "
					"on_stop_process = false" , __func__);
			}

			HPDIFPRINTK("HPD status is %s\n",
				    (*status) ? "plugged" : "unplugged");
			return 0;
		}
#ifdef	CONFIG_LSI_HDMI_AUDIO_CH_EVENT
	case AUDIO_CH_SET_STATE:
		{
			int supported_ch_num;
			if (copy_from_user(&supported_ch_num,
				(void __user *)arg, sizeof(supported_ch_num))) {
				printk(KERN_ERR	"%s() -copy_from_user error\n",
					__func__);
				return -EFAULT;
			}

			printk(KERN_INFO	"%s() - AUDIO_CH_SET_STATE = 0x%x\n",
				__func__, supported_ch_num);
			hdmi_send_audio_ch_num(supported_ch_num,
				&g_audio_ch_switch);
			return 0;
		}
#endif
	default:
		printk(KERN_ERR "(%d) unknown ioctl, HPD_GET_STATE(%d)\n",
		       (unsigned int)cmd, (unsigned int)HPD_GET_STATE);
		return -EFAULT;
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

static int s5p_hpd_irq_eint(int irq)
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
#if defined(CONFIG_SAMSUNG_WORKAROUND_HPD_GLANCE) &&\
	!defined(CONFIG_SAMSUNG_MHL_9290) &&\
	!defined(CONFIG_MHL_SII9234)
		call_sched_mhl_hpd_handler();
#endif
		irq_set_irq_type(hpd_struct.irq_n, IRQ_TYPE_LEVEL_HIGH);
		if (atomic_read(&hpd_struct.state) == HPD_LO)
			return IRQ_HANDLED;

		atomic_set(&hpd_struct.state, HPD_LO);
		atomic_set(&poll_state, 1);

		last_hpd_state = HPD_LO;

#ifdef CONFIG_HDMI_CONTROLLED_BY_EXT_IC
		schedule_delayed_work(&ext_ic_control_dwork ,
				msecs_to_jiffies(1000));
#endif
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
		printk(KERN_WARNING "%s() flag is wrong : 0x%x\n",
		       __func__, flag);
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
		HPDIFPRINTK("HPD_HI\n");

		s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_UNPLUG, 1);
		if (atomic_read(&hpd_struct.state) == HPD_HI)
			return IRQ_HANDLED;

		atomic_set(&hpd_struct.state, HPD_HI);
		atomic_set(&poll_state, 1);

		last_hpd_state = HPD_HI;
		wake_up_interruptible(&hpd_struct.waitq);

	} else if (flag & (1 << HDMI_IRQ_HPD_UNPLUG)) {
		HPDIFPRINTK("HPD_LO\n");
#if defined(CONFIG_SAMSUNG_WORKAROUND_HPD_GLANCE) &&\
	!defined(CONFIG_SAMSUNG_MHL_9290) &&\
	!defined(CONFIG_MHL_SII9234)
		call_sched_mhl_hpd_handler();
#endif

		s5p_hdcp_stop();

		s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_PLUG, 1);
		if (atomic_read(&hpd_struct.state) == HPD_LO)
			return IRQ_HANDLED;

		atomic_set(&hpd_struct.state, HPD_LO);
		atomic_set(&poll_state, 1);

		last_hpd_state = HPD_LO;
#ifdef CONFIG_HDMI_CONTROLLED_BY_EXT_IC
		schedule_delayed_work(&ext_ic_control_dwork ,
				msecs_to_jiffies(1000));
#endif

		wake_up_interruptible(&hpd_struct.waitq);
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

	/* check HDMI status */
	if (atomic_read(&hdmi_status)) {
		/* HDMI on */
		HPDIFPRINTK("HDMI HPD interrupt\n");
		ret = s5p_hpd_irq_hdmi(irq);
		HPDIFPRINTK("HDMI HPD interrupt - end\n");
	} else {
		/* HDMI off */
		HPDIFPRINTK("EINT HPD interrupt\n");
		ret = s5p_hpd_irq_eint(irq);
		HPDIFPRINTK("EINT HPD interrupt - end\n");
	}

	return ret;
}

#ifdef CONFIG_HDMI_CONTROLLED_BY_EXT_IC
static work_func_t  ext_ic_control_func(void)
{
	if (!hpd_struct.read_gpio()) {
		hpd_struct.ext_ic_control(false);
		HPDPRINTK("HDMI_EXT_IC Power Off\n");
	} else {
		HPDPRINTK("HDMI_EXT_IC Delay work do nothing\n");
	}
	return 0;
}
#endif


#ifdef	CONFIG_SAMSUNG_WORKAROUND_HPD_GLANCE
static irqreturn_t s5p_hpd_irq_default_handler(int irq, void *dev_id)
{
	u8 flag;

	flag = s5p_hdmi_reg_intc_status();

	if (s5p_hdmi_reg_get_hpd_status())
		s5p_hdmi_reg_intc_clear_pending(HDMI_IRQ_HPD_PLUG);
	else
		s5p_hdmi_reg_intc_clear_pending(HDMI_IRQ_HPD_UNPLUG);

	s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_UNPLUG, 0);
	s5p_hdmi_reg_intc_enable(HDMI_IRQ_HPD_PLUG, 0);

	if (flag & (1 << HDMI_IRQ_HPD_PLUG))
		HPDIFPRINTK("HPD_HI\n");
	else if (flag & (1 << HDMI_IRQ_HPD_UNPLUG))
		HPDIFPRINTK("HPD_LO\n");
	else
		HPDIFPRINTK("UNKNOWN EVENT\n");

	return IRQ_HANDLED;
}

void mhl_hpd_handler(bool onoff)
{
	static int old_state;

	if (old_state == onoff) {
		printk(KERN_INFO	"%s() state is aready %s\n",
			__func__, onoff ? "on" : "off");
		return;
	} else {
		printk(KERN_INFO	"%s(%d), old_state(%d)\n",
			__func__, onoff, old_state);
		old_state = onoff;
	}

	if (onoff == true) {
		enable_irq(hpd_struct.irq_n);
		s5p_hdmi_reg_intc_set_isr(s5p_hpd_irq_handler,
					  (u8) HDMI_IRQ_HPD_PLUG);
	} else {
		disable_irq_nosync(hpd_struct.irq_n);
		s5p_hdmi_reg_intc_set_isr(s5p_hpd_irq_default_handler,
					  (u8) HDMI_IRQ_HPD_PLUG);
	}
}
EXPORT_SYMBOL(mhl_hpd_handler);
#endif

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
#ifdef CONFIG_HDMI_CONTROLLED_BY_EXT_IC
	if (pdata->ext_ic_control)
		hpd_struct.ext_ic_control = pdata->ext_ic_control;
#endif
	hpd_struct.irq_n = platform_get_irq(pdev, 0);

	hpd_struct.int_src_ext_hpd();
	if (hpd_struct.read_gpio()) {
		atomic_set(&hpd_struct.state, HPD_HI);
		last_hpd_state = HPD_HI;
#ifdef CONFIG_HDMI_CONTROLLED_BY_EXT_IC
		hpd_struct.ext_ic_control(true);
#endif
	} else {
		atomic_set(&hpd_struct.state, HPD_LO);
		last_hpd_state = HPD_LO;
	}

#ifdef CONFIG_HDMI_SWITCH_HPD
	hpd_struct.hpd_switch.name = "hdmi";
	switch_dev_register(&hpd_struct.hpd_switch);
#endif
	switch_set_state(&hpd_struct.hpd_switch, last_hpd_state);
	irq_set_irq_type(hpd_struct.irq_n, IRQ_TYPE_EDGE_BOTH);

	ret = request_irq(hpd_struct.irq_n, (irq_handler_t) s5p_hpd_irq_handler,
			  IRQF_DISABLED, "hpd", (void *)(&pdev->dev));

	if (ret) {
		printk(KERN_ERR "failed to install hpd irq\n");
		misc_deregister(&hpd_misc_device);
		return -EIO;
	}
#ifdef	CONFIG_SAMSUNG_WORKAROUND_HPD_GLANCE
	disable_irq(hpd_struct.irq_n);
#	if !defined(CONFIG_SAMSUNG_MHL_9290) &&\
		!defined(CONFIG_MHL_SII9234)
		hpd_intr_state = s5p_hpd_get_status;
#	endif
#endif

	s5p_hdmi_reg_intc_set_isr(s5p_hpd_irq_handler, (u8) HDMI_IRQ_HPD_PLUG);
	s5p_hdmi_reg_intc_set_isr(s5p_hpd_irq_handler,
				  (u8) HDMI_IRQ_HPD_UNPLUG);

	last_uevent_state = -1;

#ifdef CONFIG_LSI_HDMI_AUDIO_CH_EVENT
	g_audio_ch_switch.name = "ch_hdmi_audio";
	switch_dev_register(&g_audio_ch_switch);
#endif
	return 0;
}

static int __devexit s5p_hpd_remove(struct platform_device *pdev)
{
#ifdef CONFIG_HDMI_SWITCH_HPD
	switch_dev_unregister(&hpd_struct.hpd_switch);
#endif
#ifdef CONFIG_LSI_HDMI_AUDIO_CH_EVENT
	switch_dev_unregister(&g_audio_ch_switch);
#endif
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
	.probe = s5p_hpd_probe,
	.remove = __devexit_p(s5p_hpd_remove),
	.suspend = s5p_hpd_suspend,
	.resume = s5p_hpd_resume,
	.driver = {
		   .name = "s5p-tvout-hpd",
		   .owner = THIS_MODULE,
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
