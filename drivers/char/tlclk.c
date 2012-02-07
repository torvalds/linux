/*
 * Telecom Clock driver for Intel NetStructure(tm) MPCBL0010
 *
 * Copyright (C) 2005 Kontron Canada
 *
 * All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or (at
 * your option) any later version.
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE, GOOD TITLE or
 * NON INFRINGEMENT.  See the GNU General Public License for more
 * details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 * Send feedback to <sebastien.bouchard@ca.kontron.com> and the current
 * Maintainer  <mark.gross@intel.com>
 *
 * Description : This is the TELECOM CLOCK module driver for the ATCA
 * MPCBL0010 ATCA computer.
 */

#include <linux/module.h>
#include <linux/init.h>
#include <linux/kernel.h>	/* printk() */
#include <linux/fs.h>		/* everything... */
#include <linux/errno.h>	/* error codes */
#include <linux/sched.h>
#include <linux/slab.h>
#include <linux/ioport.h>
#include <linux/interrupt.h>
#include <linux/spinlock.h>
#include <linux/mutex.h>
#include <linux/timer.h>
#include <linux/sysfs.h>
#include <linux/device.h>
#include <linux/miscdevice.h>
#include <linux/platform_device.h>
#include <asm/io.h>		/* inb/outb */
#include <asm/uaccess.h>

MODULE_AUTHOR("Sebastien Bouchard <sebastien.bouchard@ca.kontron.com>");
MODULE_LICENSE("GPL");

/*Hardware Reset of the PLL */
#define RESET_ON	0x00
#define RESET_OFF	0x01

/* MODE SELECT */
#define NORMAL_MODE 	0x00
#define HOLDOVER_MODE	0x10
#define FREERUN_MODE	0x20

/* FILTER SELECT */
#define FILTER_6HZ	0x04
#define FILTER_12HZ	0x00

/* SELECT REFERENCE FREQUENCY */
#define REF_CLK1_8kHz		0x00
#define REF_CLK2_19_44MHz	0x02

/* Select primary or secondary redundant clock */
#define PRIMARY_CLOCK	0x00
#define SECONDARY_CLOCK	0x01

/* CLOCK TRANSMISSION DEFINE */
#define CLK_8kHz	0xff
#define CLK_16_384MHz	0xfb

#define CLK_1_544MHz	0x00
#define CLK_2_048MHz	0x01
#define CLK_4_096MHz	0x02
#define CLK_6_312MHz	0x03
#define CLK_8_192MHz	0x04
#define CLK_19_440MHz	0x06

#define CLK_8_592MHz	0x08
#define CLK_11_184MHz	0x09
#define CLK_34_368MHz	0x0b
#define CLK_44_736MHz	0x0a

/* RECEIVED REFERENCE */
#define AMC_B1 0
#define AMC_B2 1

/* HARDWARE SWITCHING DEFINE */
#define HW_ENABLE	0x80
#define HW_DISABLE	0x00

/* HARDWARE SWITCHING MODE DEFINE */
#define PLL_HOLDOVER	0x40
#define LOST_CLOCK	0x00

/* ALARMS DEFINE */
#define UNLOCK_MASK	0x10
#define HOLDOVER_MASK	0x20
#define SEC_LOST_MASK	0x40
#define PRI_LOST_MASK	0x80

/* INTERRUPT CAUSE DEFINE */

#define PRI_LOS_01_MASK		0x01
#define PRI_LOS_10_MASK		0x02

#define SEC_LOS_01_MASK		0x04
#define SEC_LOS_10_MASK		0x08

#define HOLDOVER_01_MASK	0x10
#define HOLDOVER_10_MASK	0x20

#define UNLOCK_01_MASK		0x40
#define UNLOCK_10_MASK		0x80

struct tlclk_alarms {
	__u32 lost_clocks;
	__u32 lost_primary_clock;
	__u32 lost_secondary_clock;
	__u32 primary_clock_back;
	__u32 secondary_clock_back;
	__u32 switchover_primary;
	__u32 switchover_secondary;
	__u32 pll_holdover;
	__u32 pll_end_holdover;
	__u32 pll_lost_sync;
	__u32 pll_sync;
};
/* Telecom clock I/O register definition */
#define TLCLK_BASE 0xa08
#define TLCLK_REG0 TLCLK_BASE
#define TLCLK_REG1 (TLCLK_BASE+1)
#define TLCLK_REG2 (TLCLK_BASE+2)
#define TLCLK_REG3 (TLCLK_BASE+3)
#define TLCLK_REG4 (TLCLK_BASE+4)
#define TLCLK_REG5 (TLCLK_BASE+5)
#define TLCLK_REG6 (TLCLK_BASE+6)
#define TLCLK_REG7 (TLCLK_BASE+7)

#define SET_PORT_BITS(port, mask, val) outb(((inb(port) & mask) | val), port)

/* 0 = Dynamic allocation of the major device number */
#define TLCLK_MAJOR 0

/* sysfs interface definition:
Upon loading the driver will create a sysfs directory under
/sys/devices/platform/telco_clock.

This directory exports the following interfaces.  There operation is
documented in the MCPBL0010 TPS under the Telecom Clock API section, 11.4.
alarms				:
current_ref			:
received_ref_clk3a		:
received_ref_clk3b		:
enable_clk3a_output		:
enable_clk3b_output		:
enable_clka0_output		:
enable_clka1_output		:
enable_clkb0_output		:
enable_clkb1_output		:
filter_select			:
hardware_switching		:
hardware_switching_mode		:
telclock_version		:
mode_select			:
refalign			:
reset				:
select_amcb1_transmit_clock	:
select_amcb2_transmit_clock	:
select_redundant_clock		:
select_ref_frequency		:

All sysfs interfaces are integers in hex format, i.e echo 99 > refalign
has the same effect as echo 0x99 > refalign.
*/

static unsigned int telclk_interrupt;

static int int_events;		/* Event that generate a interrupt */
static int got_event;		/* if events processing have been done */

static void switchover_timeout(unsigned long data);
static struct timer_list switchover_timer =
	TIMER_INITIALIZER(switchover_timeout , 0, 0);
static unsigned long tlclk_timer_data;

static struct tlclk_alarms *alarm_events;

static DEFINE_SPINLOCK(event_lock);

static int tlclk_major = TLCLK_MAJOR;

static irqreturn_t tlclk_interrupt(int irq, void *dev_id);

static DECLARE_WAIT_QUEUE_HEAD(wq);

static unsigned long useflags;
static DEFINE_MUTEX(tlclk_mutex);

static int tlclk_open(struct inode *inode, struct file *filp)
{
	int result;

	mutex_lock(&tlclk_mutex);
	if (test_and_set_bit(0, &useflags)) {
		result = -EBUSY;
		/* this legacy device is always one per system and it doesn't
		 * know how to handle multiple concurrent clients.
		 */
		goto out;
	}

	/* Make sure there is no interrupt pending while
	 * initialising interrupt handler */
	inb(TLCLK_REG6);

	/* This device is wired through the FPGA IO space of the ATCA blade
	 * we can't share this IRQ */
	result = request_irq(telclk_interrupt, &tlclk_interrupt,
			     IRQF_DISABLED, "telco_clock", tlclk_interrupt);
	if (result == -EBUSY)
		printk(KERN_ERR "tlclk: Interrupt can't be reserved.\n");
	else
		inb(TLCLK_REG6);	/* Clear interrupt events */

out:
	mutex_unlock(&tlclk_mutex);
	return result;
}

static int tlclk_release(struct inode *inode, struct file *filp)
{
	free_irq(telclk_interrupt, tlclk_interrupt);
	clear_bit(0, &useflags);

	return 0;
}

static ssize_t tlclk_read(struct file *filp, char __user *buf, size_t count,
		loff_t *f_pos)
{
	if (count < sizeof(struct tlclk_alarms))
		return -EIO;
	if (mutex_lock_interruptible(&tlclk_mutex))
		return -EINTR;


	wait_event_interruptible(wq, got_event);
	if (copy_to_user(buf, alarm_events, sizeof(struct tlclk_alarms))) {
		mutex_unlock(&tlclk_mutex);
		return -EFAULT;
	}

	memset(alarm_events, 0, sizeof(struct tlclk_alarms));
	got_event = 0;

	mutex_unlock(&tlclk_mutex);
	return  sizeof(struct tlclk_alarms);
}

static const struct file_operations tlclk_fops = {
	.read = tlclk_read,
	.open = tlclk_open,
	.release = tlclk_release,
	.llseek = noop_llseek,

};

static struct miscdevice tlclk_miscdev = {
	.minor = MISC_DYNAMIC_MINOR,
	.name = "telco_clock",
	.fops = &tlclk_fops,
};

static ssize_t show_current_ref(struct device *d,
		struct device_attribute *attr, char *buf)
{
	unsigned long ret_val;
	unsigned long flags;

	spin_lock_irqsave(&event_lock, flags);
	ret_val = ((inb(TLCLK_REG1) & 0x08) >> 3);
	spin_unlock_irqrestore(&event_lock, flags);

	return sprintf(buf, "0x%lX\n", ret_val);
}

static DEVICE_ATTR(current_ref, S_IRUGO, show_current_ref, NULL);


static ssize_t show_telclock_version(struct device *d,
		struct device_attribute *attr, char *buf)
{
	unsigned long ret_val;
	unsigned long flags;

	spin_lock_irqsave(&event_lock, flags);
	ret_val = inb(TLCLK_REG5);
	spin_unlock_irqrestore(&event_lock, flags);

	return sprintf(buf, "0x%lX\n", ret_val);
}

static DEVICE_ATTR(telclock_version, S_IRUGO,
		show_telclock_version, NULL);

static ssize_t show_alarms(struct device *d,
		struct device_attribute *attr,  char *buf)
{
	unsigned long ret_val;
	unsigned long flags;

	spin_lock_irqsave(&event_lock, flags);
	ret_val = (inb(TLCLK_REG2) & 0xf0);
	spin_unlock_irqrestore(&event_lock, flags);

	return sprintf(buf, "0x%lX\n", ret_val);
}

static DEVICE_ATTR(alarms, S_IRUGO, show_alarms, NULL);

static ssize_t store_received_ref_clk3a(struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long tmp;
	unsigned char val;
	unsigned long flags;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, ": tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG1, 0xef, val);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(received_ref_clk3a, (S_IWUSR|S_IWGRP), NULL,
		store_received_ref_clk3a);


static ssize_t store_received_ref_clk3b(struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long tmp;
	unsigned char val;
	unsigned long flags;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, ": tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG1, 0xdf, val << 1);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(received_ref_clk3b, (S_IWUSR|S_IWGRP), NULL,
		store_received_ref_clk3b);


static ssize_t store_enable_clk3b_output(struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long tmp;
	unsigned char val;
	unsigned long flags;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, ": tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG3, 0x7f, val << 7);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(enable_clk3b_output, (S_IWUSR|S_IWGRP), NULL,
		store_enable_clk3b_output);

static ssize_t store_enable_clk3a_output(struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long flags;
	unsigned long tmp;
	unsigned char val;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG3, 0xbf, val << 6);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(enable_clk3a_output, (S_IWUSR|S_IWGRP), NULL,
		store_enable_clk3a_output);

static ssize_t store_enable_clkb1_output(struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long flags;
	unsigned long tmp;
	unsigned char val;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG2, 0xf7, val << 3);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(enable_clkb1_output, (S_IWUSR|S_IWGRP), NULL,
		store_enable_clkb1_output);


static ssize_t store_enable_clka1_output(struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long flags;
	unsigned long tmp;
	unsigned char val;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG2, 0xfb, val << 2);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(enable_clka1_output, (S_IWUSR|S_IWGRP), NULL,
		store_enable_clka1_output);

static ssize_t store_enable_clkb0_output(struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long flags;
	unsigned long tmp;
	unsigned char val;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG2, 0xfd, val << 1);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(enable_clkb0_output, (S_IWUSR|S_IWGRP), NULL,
		store_enable_clkb0_output);

static ssize_t store_enable_clka0_output(struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long flags;
	unsigned long tmp;
	unsigned char val;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG2, 0xfe, val);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(enable_clka0_output, (S_IWUSR|S_IWGRP), NULL,
		store_enable_clka0_output);

static ssize_t store_select_amcb2_transmit_clock(struct device *d,
		struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long flags;
	unsigned long tmp;
	unsigned char val;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
		if ((val == CLK_8kHz) || (val == CLK_16_384MHz)) {
			SET_PORT_BITS(TLCLK_REG3, 0xc7, 0x28);
			SET_PORT_BITS(TLCLK_REG1, 0xfb, ~val);
		} else if (val >= CLK_8_592MHz) {
			SET_PORT_BITS(TLCLK_REG3, 0xc7, 0x38);
			switch (val) {
			case CLK_8_592MHz:
				SET_PORT_BITS(TLCLK_REG0, 0xfc, 2);
				break;
			case CLK_11_184MHz:
				SET_PORT_BITS(TLCLK_REG0, 0xfc, 0);
				break;
			case CLK_34_368MHz:
				SET_PORT_BITS(TLCLK_REG0, 0xfc, 3);
				break;
			case CLK_44_736MHz:
				SET_PORT_BITS(TLCLK_REG0, 0xfc, 1);
				break;
			}
		} else
			SET_PORT_BITS(TLCLK_REG3, 0xc7, val << 3);

	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(select_amcb2_transmit_clock, (S_IWUSR|S_IWGRP), NULL,
	store_select_amcb2_transmit_clock);

static ssize_t store_select_amcb1_transmit_clock(struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long tmp;
	unsigned char val;
	unsigned long flags;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
		if ((val == CLK_8kHz) || (val == CLK_16_384MHz)) {
			SET_PORT_BITS(TLCLK_REG3, 0xf8, 0x5);
			SET_PORT_BITS(TLCLK_REG1, 0xfb, ~val);
		} else if (val >= CLK_8_592MHz) {
			SET_PORT_BITS(TLCLK_REG3, 0xf8, 0x7);
			switch (val) {
			case CLK_8_592MHz:
				SET_PORT_BITS(TLCLK_REG0, 0xfc, 2);
				break;
			case CLK_11_184MHz:
				SET_PORT_BITS(TLCLK_REG0, 0xfc, 0);
				break;
			case CLK_34_368MHz:
				SET_PORT_BITS(TLCLK_REG0, 0xfc, 3);
				break;
			case CLK_44_736MHz:
				SET_PORT_BITS(TLCLK_REG0, 0xfc, 1);
				break;
			}
		} else
			SET_PORT_BITS(TLCLK_REG3, 0xf8, val);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(select_amcb1_transmit_clock, (S_IWUSR|S_IWGRP), NULL,
		store_select_amcb1_transmit_clock);

static ssize_t store_select_redundant_clock(struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long tmp;
	unsigned char val;
	unsigned long flags;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG1, 0xfe, val);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(select_redundant_clock, (S_IWUSR|S_IWGRP), NULL,
		store_select_redundant_clock);

static ssize_t store_select_ref_frequency(struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long tmp;
	unsigned char val;
	unsigned long flags;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG1, 0xfd, val);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(select_ref_frequency, (S_IWUSR|S_IWGRP), NULL,
		store_select_ref_frequency);

static ssize_t store_filter_select(struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long tmp;
	unsigned char val;
	unsigned long flags;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG0, 0xfb, val);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(filter_select, (S_IWUSR|S_IWGRP), NULL, store_filter_select);

static ssize_t store_hardware_switching_mode(struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long tmp;
	unsigned char val;
	unsigned long flags;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG0, 0xbf, val);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(hardware_switching_mode, (S_IWUSR|S_IWGRP), NULL,
		store_hardware_switching_mode);

static ssize_t store_hardware_switching(struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long tmp;
	unsigned char val;
	unsigned long flags;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG0, 0x7f, val);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(hardware_switching, (S_IWUSR|S_IWGRP), NULL,
		store_hardware_switching);

static ssize_t store_refalign (struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long tmp;
	unsigned long flags;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG0, 0xf7, 0);
	SET_PORT_BITS(TLCLK_REG0, 0xf7, 0x08);
	SET_PORT_BITS(TLCLK_REG0, 0xf7, 0);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(refalign, (S_IWUSR|S_IWGRP), NULL, store_refalign);

static ssize_t store_mode_select (struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long tmp;
	unsigned char val;
	unsigned long flags;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG0, 0xcf, val);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(mode_select, (S_IWUSR|S_IWGRP), NULL, store_mode_select);

static ssize_t store_reset (struct device *d,
		 struct device_attribute *attr, const char *buf, size_t count)
{
	unsigned long tmp;
	unsigned char val;
	unsigned long flags;

	sscanf(buf, "%lX", &tmp);
	dev_dbg(d, "tmp = 0x%lX\n", tmp);

	val = (unsigned char)tmp;
	spin_lock_irqsave(&event_lock, flags);
	SET_PORT_BITS(TLCLK_REG4, 0xfd, val);
	spin_unlock_irqrestore(&event_lock, flags);

	return strnlen(buf, count);
}

static DEVICE_ATTR(reset, (S_IWUSR|S_IWGRP), NULL, store_reset);

static struct attribute *tlclk_sysfs_entries[] = {
	&dev_attr_current_ref.attr,
	&dev_attr_telclock_version.attr,
	&dev_attr_alarms.attr,
	&dev_attr_received_ref_clk3a.attr,
	&dev_attr_received_ref_clk3b.attr,
	&dev_attr_enable_clk3a_output.attr,
	&dev_attr_enable_clk3b_output.attr,
	&dev_attr_enable_clkb1_output.attr,
	&dev_attr_enable_clka1_output.attr,
	&dev_attr_enable_clkb0_output.attr,
	&dev_attr_enable_clka0_output.attr,
	&dev_attr_select_amcb1_transmit_clock.attr,
	&dev_attr_select_amcb2_transmit_clock.attr,
	&dev_attr_select_redundant_clock.attr,
	&dev_attr_select_ref_frequency.attr,
	&dev_attr_filter_select.attr,
	&dev_attr_hardware_switching_mode.attr,
	&dev_attr_hardware_switching.attr,
	&dev_attr_refalign.attr,
	&dev_attr_mode_select.attr,
	&dev_attr_reset.attr,
	NULL
};

static struct attribute_group tlclk_attribute_group = {
	.name = NULL,		/* put in device directory */
	.attrs = tlclk_sysfs_entries,
};

static struct platform_device *tlclk_device;

static int __init tlclk_init(void)
{
	int ret;

	ret = register_chrdev(tlclk_major, "telco_clock", &tlclk_fops);
	if (ret < 0) {
		printk(KERN_ERR "tlclk: can't get major %d.\n", tlclk_major);
		return ret;
	}
	tlclk_major = ret;
	alarm_events = kzalloc( sizeof(struct tlclk_alarms), GFP_KERNEL);
	if (!alarm_events)
		goto out1;

	/* Read telecom clock IRQ number (Set by BIOS) */
	if (!request_region(TLCLK_BASE, 8, "telco_clock")) {
		printk(KERN_ERR "tlclk: request_region 0x%X failed.\n",
			TLCLK_BASE);
		ret = -EBUSY;
		goto out2;
	}
	telclk_interrupt = (inb(TLCLK_REG7) & 0x0f);

	if (0x0F == telclk_interrupt ) { /* not MCPBL0010 ? */
		printk(KERN_ERR "telclk_interrupt = 0x%x non-mcpbl0010 hw.\n",
			telclk_interrupt);
		ret = -ENXIO;
		goto out3;
	}

	init_timer(&switchover_timer);

	ret = misc_register(&tlclk_miscdev);
	if (ret < 0) {
		printk(KERN_ERR "tlclk: misc_register returns %d.\n", ret);
		goto out3;
	}

	tlclk_device = platform_device_register_simple("telco_clock",
				-1, NULL, 0);
	if (IS_ERR(tlclk_device)) {
		printk(KERN_ERR "tlclk: platform_device_register failed.\n");
		ret = PTR_ERR(tlclk_device);
		goto out4;
	}

	ret = sysfs_create_group(&tlclk_device->dev.kobj,
			&tlclk_attribute_group);
	if (ret) {
		printk(KERN_ERR "tlclk: failed to create sysfs device attributes.\n");
		goto out5;
	}

	return 0;
out5:
	platform_device_unregister(tlclk_device);
out4:
	misc_deregister(&tlclk_miscdev);
out3:
	release_region(TLCLK_BASE, 8);
out2:
	kfree(alarm_events);
out1:
	unregister_chrdev(tlclk_major, "telco_clock");
	return ret;
}

static void __exit tlclk_cleanup(void)
{
	sysfs_remove_group(&tlclk_device->dev.kobj, &tlclk_attribute_group);
	platform_device_unregister(tlclk_device);
	misc_deregister(&tlclk_miscdev);
	unregister_chrdev(tlclk_major, "telco_clock");

	release_region(TLCLK_BASE, 8);
	del_timer_sync(&switchover_timer);
	kfree(alarm_events);

}

static void switchover_timeout(unsigned long data)
{
	unsigned long flags = *(unsigned long *) data;

	if ((flags & 1)) {
		if ((inb(TLCLK_REG1) & 0x08) != (flags & 0x08))
			alarm_events->switchover_primary++;
	} else {
		if ((inb(TLCLK_REG1) & 0x08) != (flags & 0x08))
			alarm_events->switchover_secondary++;
	}

	/* Alarm processing is done, wake up read task */
	del_timer(&switchover_timer);
	got_event = 1;
	wake_up(&wq);
}

static irqreturn_t tlclk_interrupt(int irq, void *dev_id)
{
	unsigned long flags;

	spin_lock_irqsave(&event_lock, flags);
	/* Read and clear interrupt events */
	int_events = inb(TLCLK_REG6);

	/* Primary_Los changed from 0 to 1 ? */
	if (int_events & PRI_LOS_01_MASK) {
		if (inb(TLCLK_REG2) & SEC_LOST_MASK)
			alarm_events->lost_clocks++;
		else
			alarm_events->lost_primary_clock++;
	}

	/* Primary_Los changed from 1 to 0 ? */
	if (int_events & PRI_LOS_10_MASK) {
		alarm_events->primary_clock_back++;
		SET_PORT_BITS(TLCLK_REG1, 0xFE, 1);
	}
	/* Secondary_Los changed from 0 to 1 ? */
	if (int_events & SEC_LOS_01_MASK) {
		if (inb(TLCLK_REG2) & PRI_LOST_MASK)
			alarm_events->lost_clocks++;
		else
			alarm_events->lost_secondary_clock++;
	}
	/* Secondary_Los changed from 1 to 0 ? */
	if (int_events & SEC_LOS_10_MASK) {
		alarm_events->secondary_clock_back++;
		SET_PORT_BITS(TLCLK_REG1, 0xFE, 0);
	}
	if (int_events & HOLDOVER_10_MASK)
		alarm_events->pll_end_holdover++;

	if (int_events & UNLOCK_01_MASK)
		alarm_events->pll_lost_sync++;

	if (int_events & UNLOCK_10_MASK)
		alarm_events->pll_sync++;

	/* Holdover changed from 0 to 1 ? */
	if (int_events & HOLDOVER_01_MASK) {
		alarm_events->pll_holdover++;

		/* TIMEOUT in ~10ms */
		switchover_timer.expires = jiffies + msecs_to_jiffies(10);
		tlclk_timer_data = inb(TLCLK_REG1);
		switchover_timer.data = (unsigned long) &tlclk_timer_data;
		mod_timer(&switchover_timer, switchover_timer.expires);
	} else {
		got_event = 1;
		wake_up(&wq);
	}
	spin_unlock_irqrestore(&event_lock, flags);

	return IRQ_HANDLED;
}

module_init(tlclk_init);
module_exit(tlclk_cleanup);
