/*
 *  lis3lv02d.c - ST LIS3LV02DL accelerometer driver
 *
 *  Copyright (C) 2007-2008 Yan Burman
 *  Copyright (C) 2008 Eric Piel
 *  Copyright (C) 2008-2009 Pavel Machek
 *
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation; either version 2 of the License, or
 *  (at your option) any later version.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 *
 *  You should have received a copy of the GNU General Public License
 *  along with this program; if not, write to the Free Software
 *  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/dmi.h>
#include <linux/module.h>
#include <linux/types.h>
#include <linux/platform_device.h>
#include <linux/interrupt.h>
#include <linux/input-polldev.h>
#include <linux/delay.h>
#include <linux/wait.h>
#include <linux/poll.h>
#include <linux/slab.h>
#include <linux/freezer.h>
#include <linux/uaccess.h>
#include <linux/miscdevice.h>
#include <linux/pm_runtime.h>
#include <asm/atomic.h>
#include "lis3lv02d.h"

#define DRIVER_NAME     "lis3lv02d"

/* joystick device poll interval in milliseconds */
#define MDPS_POLL_INTERVAL 50
#define MDPS_POLL_MIN	   0
#define MDPS_POLL_MAX	   2000

#define LIS3_SYSFS_POWERDOWN_DELAY 5000 /* In milliseconds */

#define SELFTEST_OK	       0
#define SELFTEST_FAIL	       -1
#define SELFTEST_IRQ	       -2

#define IRQ_LINE0	       0
#define IRQ_LINE1	       1

/*
 * The sensor can also generate interrupts (DRDY) but it's pretty pointless
 * because they are generated even if the data do not change. So it's better
 * to keep the interrupt for the free-fall event. The values are updated at
 * 40Hz (at the lowest frequency), but as it can be pretty time consuming on
 * some low processor, we poll the sensor only at 20Hz... enough for the
 * joystick.
 */

#define LIS3_PWRON_DELAY_WAI_12B	(5000)
#define LIS3_PWRON_DELAY_WAI_8B		(3000)

/*
 * LIS3LV02D spec says 1024 LSBs corresponds 1 G -> 1LSB is 1000/1024 mG
 * LIS302D spec says: 18 mG / digit
 * LIS3_ACCURACY is used to increase accuracy of the intermediate
 * calculation results.
 */
#define LIS3_ACCURACY			1024
/* Sensitivity values for -2G +2G scale */
#define LIS3_SENSITIVITY_12B		((LIS3_ACCURACY * 1000) / 1024)
#define LIS3_SENSITIVITY_8B		(18 * LIS3_ACCURACY)

#define LIS3_DEFAULT_FUZZ_12B		3
#define LIS3_DEFAULT_FLAT_12B		3
#define LIS3_DEFAULT_FUZZ_8B		1
#define LIS3_DEFAULT_FLAT_8B		1

struct lis3lv02d lis3_dev = {
	.misc_wait   = __WAIT_QUEUE_HEAD_INITIALIZER(lis3_dev.misc_wait),
};

EXPORT_SYMBOL_GPL(lis3_dev);

/* just like param_set_int() but does sanity-check so that it won't point
 * over the axis array size
 */
static int param_set_axis(const char *val, const struct kernel_param *kp)
{
	int ret = param_set_int(val, kp);
	if (!ret) {
		int val = *(int *)kp->arg;
		if (val < 0)
			val = -val;
		if (!val || val > 3)
			return -EINVAL;
	}
	return ret;
}

static struct kernel_param_ops param_ops_axis = {
	.set = param_set_axis,
	.get = param_get_int,
};

module_param_array_named(axes, lis3_dev.ac.as_array, axis, NULL, 0644);
MODULE_PARM_DESC(axes, "Axis-mapping for x,y,z directions");

static s16 lis3lv02d_read_8(struct lis3lv02d *lis3, int reg)
{
	s8 lo;
	if (lis3->read(lis3, reg, &lo) < 0)
		return 0;

	return lo;
}

static s16 lis3lv02d_read_12(struct lis3lv02d *lis3, int reg)
{
	u8 lo, hi;

	lis3->read(lis3, reg - 1, &lo);
	lis3->read(lis3, reg, &hi);
	/* In "12 bit right justified" mode, bit 6, bit 7, bit 8 = bit 5 */
	return (s16)((hi << 8) | lo);
}

/**
 * lis3lv02d_get_axis - For the given axis, give the value converted
 * @axis:      1,2,3 - can also be negative
 * @hw_values: raw values returned by the hardware
 *
 * Returns the converted value.
 */
static inline int lis3lv02d_get_axis(s8 axis, int hw_values[3])
{
	if (axis > 0)
		return hw_values[axis - 1];
	else
		return -hw_values[-axis - 1];
}

/**
 * lis3lv02d_get_xyz - Get X, Y and Z axis values from the accelerometer
 * @lis3: pointer to the device struct
 * @x:    where to store the X axis value
 * @y:    where to store the Y axis value
 * @z:    where to store the Z axis value
 *
 * Note that 40Hz input device can eat up about 10% CPU at 800MHZ
 */
static void lis3lv02d_get_xyz(struct lis3lv02d *lis3, int *x, int *y, int *z)
{
	int position[3];
	int i;

	if (lis3->blkread) {
		if (lis3_dev.whoami == WAI_12B) {
			u16 data[3];
			lis3->blkread(lis3, OUTX_L, 6, (u8 *)data);
			for (i = 0; i < 3; i++)
				position[i] = (s16)le16_to_cpu(data[i]);
		} else {
			u8 data[5];
			/* Data: x, dummy, y, dummy, z */
			lis3->blkread(lis3, OUTX, 5, data);
			for (i = 0; i < 3; i++)
				position[i] = (s8)data[i * 2];
		}
	} else {
		position[0] = lis3->read_data(lis3, OUTX);
		position[1] = lis3->read_data(lis3, OUTY);
		position[2] = lis3->read_data(lis3, OUTZ);
	}

	for (i = 0; i < 3; i++)
		position[i] = (position[i] * lis3->scale) / LIS3_ACCURACY;

	*x = lis3lv02d_get_axis(lis3->ac.x, position);
	*y = lis3lv02d_get_axis(lis3->ac.y, position);
	*z = lis3lv02d_get_axis(lis3->ac.z, position);
}

/* conversion btw sampling rate and the register values */
static int lis3_12_rates[4] = {40, 160, 640, 2560};
static int lis3_8_rates[2] = {100, 400};
static int lis3_3dc_rates[16] = {0, 1, 10, 25, 50, 100, 200, 400, 1600, 5000};

/* ODR is Output Data Rate */
static int lis3lv02d_get_odr(void)
{
	u8 ctrl;
	int shift;

	lis3_dev.read(&lis3_dev, CTRL_REG1, &ctrl);
	ctrl &= lis3_dev.odr_mask;
	shift = ffs(lis3_dev.odr_mask) - 1;
	return lis3_dev.odrs[(ctrl >> shift)];
}

static int lis3lv02d_set_odr(int rate)
{
	u8 ctrl;
	int i, len, shift;

	if (!rate)
		return -EINVAL;

	lis3_dev.read(&lis3_dev, CTRL_REG1, &ctrl);
	ctrl &= ~lis3_dev.odr_mask;
	len = 1 << hweight_long(lis3_dev.odr_mask); /* # of possible values */
	shift = ffs(lis3_dev.odr_mask) - 1;

	for (i = 0; i < len; i++)
		if (lis3_dev.odrs[i] == rate) {
			lis3_dev.write(&lis3_dev, CTRL_REG1,
					ctrl | (i << shift));
			return 0;
		}
	return -EINVAL;
}

static int lis3lv02d_selftest(struct lis3lv02d *lis3, s16 results[3])
{
	u8 ctlreg, reg;
	s16 x, y, z;
	u8 selftest;
	int ret;
	u8 ctrl_reg_data;
	unsigned char irq_cfg;

	mutex_lock(&lis3->mutex);

	irq_cfg = lis3->irq_cfg;
	if (lis3_dev.whoami == WAI_8B) {
		lis3->data_ready_count[IRQ_LINE0] = 0;
		lis3->data_ready_count[IRQ_LINE1] = 0;

		/* Change interrupt cfg to data ready for selftest */
		atomic_inc(&lis3_dev.wake_thread);
		lis3->irq_cfg = LIS3_IRQ1_DATA_READY | LIS3_IRQ2_DATA_READY;
		lis3->read(lis3, CTRL_REG3, &ctrl_reg_data);
		lis3->write(lis3, CTRL_REG3, (ctrl_reg_data &
				~(LIS3_IRQ1_MASK | LIS3_IRQ2_MASK)) |
				(LIS3_IRQ1_DATA_READY | LIS3_IRQ2_DATA_READY));
	}

	if (lis3_dev.whoami == WAI_3DC) {
		ctlreg = CTRL_REG4;
		selftest = CTRL4_ST0;
	} else {
		ctlreg = CTRL_REG1;
		if (lis3_dev.whoami == WAI_12B)
			selftest = CTRL1_ST;
		else
			selftest = CTRL1_STP;
	}

	lis3->read(lis3, ctlreg, &reg);
	lis3->write(lis3, ctlreg, (reg | selftest));
	msleep(lis3->pwron_delay / lis3lv02d_get_odr());

	/* Read directly to avoid axis remap */
	x = lis3->read_data(lis3, OUTX);
	y = lis3->read_data(lis3, OUTY);
	z = lis3->read_data(lis3, OUTZ);

	/* back to normal settings */
	lis3->write(lis3, ctlreg, reg);
	msleep(lis3->pwron_delay / lis3lv02d_get_odr());

	results[0] = x - lis3->read_data(lis3, OUTX);
	results[1] = y - lis3->read_data(lis3, OUTY);
	results[2] = z - lis3->read_data(lis3, OUTZ);

	ret = 0;

	if (lis3_dev.whoami == WAI_8B) {
		/* Restore original interrupt configuration */
		atomic_dec(&lis3_dev.wake_thread);
		lis3->write(lis3, CTRL_REG3, ctrl_reg_data);
		lis3->irq_cfg = irq_cfg;

		if ((irq_cfg & LIS3_IRQ1_MASK) &&
			lis3->data_ready_count[IRQ_LINE0] < 2) {
			ret = SELFTEST_IRQ;
			goto fail;
		}

		if ((irq_cfg & LIS3_IRQ2_MASK) &&
			lis3->data_ready_count[IRQ_LINE1] < 2) {
			ret = SELFTEST_IRQ;
			goto fail;
		}
	}

	if (lis3->pdata) {
		int i;
		for (i = 0; i < 3; i++) {
			/* Check against selftest acceptance limits */
			if ((results[i] < lis3->pdata->st_min_limits[i]) ||
			    (results[i] > lis3->pdata->st_max_limits[i])) {
				ret = SELFTEST_FAIL;
				goto fail;
			}
		}
	}

	/* test passed */
fail:
	mutex_unlock(&lis3->mutex);
	return ret;
}

/*
 * Order of registers in the list affects to order of the restore process.
 * Perhaps it is a good idea to set interrupt enable register as a last one
 * after all other configurations
 */
static u8 lis3_wai8_regs[] = { FF_WU_CFG_1, FF_WU_THS_1, FF_WU_DURATION_1,
			       FF_WU_CFG_2, FF_WU_THS_2, FF_WU_DURATION_2,
			       CLICK_CFG, CLICK_SRC, CLICK_THSY_X, CLICK_THSZ,
			       CLICK_TIMELIMIT, CLICK_LATENCY, CLICK_WINDOW,
			       CTRL_REG1, CTRL_REG2, CTRL_REG3};

static u8 lis3_wai12_regs[] = {FF_WU_CFG, FF_WU_THS_L, FF_WU_THS_H,
			       FF_WU_DURATION, DD_CFG, DD_THSI_L, DD_THSI_H,
			       DD_THSE_L, DD_THSE_H,
			       CTRL_REG1, CTRL_REG3, CTRL_REG2};

static inline void lis3_context_save(struct lis3lv02d *lis3)
{
	int i;
	for (i = 0; i < lis3->regs_size; i++)
		lis3->read(lis3, lis3->regs[i], &lis3->reg_cache[i]);
	lis3->regs_stored = true;
}

static inline void lis3_context_restore(struct lis3lv02d *lis3)
{
	int i;
	if (lis3->regs_stored)
		for (i = 0; i < lis3->regs_size; i++)
			lis3->write(lis3, lis3->regs[i], lis3->reg_cache[i]);
}

void lis3lv02d_poweroff(struct lis3lv02d *lis3)
{
	if (lis3->reg_ctrl)
		lis3_context_save(lis3);
	/* disable X,Y,Z axis and power down */
	lis3->write(lis3, CTRL_REG1, 0x00);
	if (lis3->reg_ctrl)
		lis3->reg_ctrl(lis3, LIS3_REG_OFF);
}
EXPORT_SYMBOL_GPL(lis3lv02d_poweroff);

void lis3lv02d_poweron(struct lis3lv02d *lis3)
{
	u8 reg;

	lis3->init(lis3);

	/*
	 * Common configuration
	 * BDU: (12 bits sensors only) LSB and MSB values are not updated until
	 *      both have been read. So the value read will always be correct.
	 * Set BOOT bit to refresh factory tuning values.
	 */
	lis3->read(lis3, CTRL_REG2, &reg);
	if (lis3->whoami ==  WAI_12B)
		reg |= CTRL2_BDU | CTRL2_BOOT;
	else
		reg |= CTRL2_BOOT_8B;
	lis3->write(lis3, CTRL_REG2, reg);

	/* LIS3 power on delay is quite long */
	msleep(lis3->pwron_delay / lis3lv02d_get_odr());

	if (lis3->reg_ctrl)
		lis3_context_restore(lis3);
}
EXPORT_SYMBOL_GPL(lis3lv02d_poweron);


static void lis3lv02d_joystick_poll(struct input_polled_dev *pidev)
{
	int x, y, z;

	mutex_lock(&lis3_dev.mutex);
	lis3lv02d_get_xyz(&lis3_dev, &x, &y, &z);
	input_report_abs(pidev->input, ABS_X, x);
	input_report_abs(pidev->input, ABS_Y, y);
	input_report_abs(pidev->input, ABS_Z, z);
	input_sync(pidev->input);
	mutex_unlock(&lis3_dev.mutex);
}

static void lis3lv02d_joystick_open(struct input_polled_dev *pidev)
{
	if (lis3_dev.pm_dev)
		pm_runtime_get_sync(lis3_dev.pm_dev);

	if (lis3_dev.pdata && lis3_dev.whoami == WAI_8B && lis3_dev.idev)
		atomic_set(&lis3_dev.wake_thread, 1);
	/*
	 * Update coordinates for the case where poll interval is 0 and
	 * the chip in running purely under interrupt control
	 */
	lis3lv02d_joystick_poll(pidev);
}

static void lis3lv02d_joystick_close(struct input_polled_dev *pidev)
{
	atomic_set(&lis3_dev.wake_thread, 0);
	if (lis3_dev.pm_dev)
		pm_runtime_put(lis3_dev.pm_dev);
}

static irqreturn_t lis302dl_interrupt(int irq, void *dummy)
{
	if (!test_bit(0, &lis3_dev.misc_opened))
		goto out;

	/*
	 * Be careful: on some HP laptops the bios force DD when on battery and
	 * the lid is closed. This leads to interrupts as soon as a little move
	 * is done.
	 */
	atomic_inc(&lis3_dev.count);

	wake_up_interruptible(&lis3_dev.misc_wait);
	kill_fasync(&lis3_dev.async_queue, SIGIO, POLL_IN);
out:
	if (atomic_read(&lis3_dev.wake_thread))
		return IRQ_WAKE_THREAD;
	return IRQ_HANDLED;
}

static void lis302dl_interrupt_handle_click(struct lis3lv02d *lis3)
{
	struct input_dev *dev = lis3->idev->input;
	u8 click_src;

	mutex_lock(&lis3->mutex);
	lis3->read(lis3, CLICK_SRC, &click_src);

	if (click_src & CLICK_SINGLE_X) {
		input_report_key(dev, lis3->mapped_btns[0], 1);
		input_report_key(dev, lis3->mapped_btns[0], 0);
	}

	if (click_src & CLICK_SINGLE_Y) {
		input_report_key(dev, lis3->mapped_btns[1], 1);
		input_report_key(dev, lis3->mapped_btns[1], 0);
	}

	if (click_src & CLICK_SINGLE_Z) {
		input_report_key(dev, lis3->mapped_btns[2], 1);
		input_report_key(dev, lis3->mapped_btns[2], 0);
	}
	input_sync(dev);
	mutex_unlock(&lis3->mutex);
}

static inline void lis302dl_data_ready(struct lis3lv02d *lis3, int index)
{
	int dummy;

	/* Dummy read to ack interrupt */
	lis3lv02d_get_xyz(lis3, &dummy, &dummy, &dummy);
	lis3->data_ready_count[index]++;
}

static irqreturn_t lis302dl_interrupt_thread1_8b(int irq, void *data)
{
	struct lis3lv02d *lis3 = data;
	u8 irq_cfg = lis3->irq_cfg & LIS3_IRQ1_MASK;

	if (irq_cfg == LIS3_IRQ1_CLICK)
		lis302dl_interrupt_handle_click(lis3);
	else if (unlikely(irq_cfg == LIS3_IRQ1_DATA_READY))
		lis302dl_data_ready(lis3, IRQ_LINE0);
	else
		lis3lv02d_joystick_poll(lis3->idev);

	return IRQ_HANDLED;
}

static irqreturn_t lis302dl_interrupt_thread2_8b(int irq, void *data)
{
	struct lis3lv02d *lis3 = data;
	u8 irq_cfg = lis3->irq_cfg & LIS3_IRQ2_MASK;

	if (irq_cfg == LIS3_IRQ2_CLICK)
		lis302dl_interrupt_handle_click(lis3);
	else if (unlikely(irq_cfg == LIS3_IRQ2_DATA_READY))
		lis302dl_data_ready(lis3, IRQ_LINE1);
	else
		lis3lv02d_joystick_poll(lis3->idev);

	return IRQ_HANDLED;
}

static int lis3lv02d_misc_open(struct inode *inode, struct file *file)
{
	if (test_and_set_bit(0, &lis3_dev.misc_opened))
		return -EBUSY; /* already open */

	if (lis3_dev.pm_dev)
		pm_runtime_get_sync(lis3_dev.pm_dev);

	atomic_set(&lis3_dev.count, 0);
	return 0;
}

static int lis3lv02d_misc_release(struct inode *inode, struct file *file)
{
	fasync_helper(-1, file, 0, &lis3_dev.async_queue);
	clear_bit(0, &lis3_dev.misc_opened); /* release the device */
	if (lis3_dev.pm_dev)
		pm_runtime_put(lis3_dev.pm_dev);
	return 0;
}

static ssize_t lis3lv02d_misc_read(struct file *file, char __user *buf,
				size_t count, loff_t *pos)
{
	DECLARE_WAITQUEUE(wait, current);
	u32 data;
	unsigned char byte_data;
	ssize_t retval = 1;

	if (count < 1)
		return -EINVAL;

	add_wait_queue(&lis3_dev.misc_wait, &wait);
	while (true) {
		set_current_state(TASK_INTERRUPTIBLE);
		data = atomic_xchg(&lis3_dev.count, 0);
		if (data)
			break;

		if (file->f_flags & O_NONBLOCK) {
			retval = -EAGAIN;
			goto out;
		}

		if (signal_pending(current)) {
			retval = -ERESTARTSYS;
			goto out;
		}

		schedule();
	}

	if (data < 255)
		byte_data = data;
	else
		byte_data = 255;

	/* make sure we are not going into copy_to_user() with
	 * TASK_INTERRUPTIBLE state */
	set_current_state(TASK_RUNNING);
	if (copy_to_user(buf, &byte_data, sizeof(byte_data)))
		retval = -EFAULT;

out:
	__set_current_state(TASK_RUNNING);
	remove_wait_queue(&lis3_dev.misc_wait, &wait);

	return retval;
}

static unsigned int lis3lv02d_misc_poll(struct file *file, poll_table *wait)
{
	poll_wait(file, &lis3_dev.misc_wait, wait);
	if (atomic_read(&lis3_dev.count))
		return POLLIN | POLLRDNORM;
	return 0;
}

static int lis3lv02d_misc_fasync(int fd, struct file *file, int on)
{
	return fasync_helper(fd, file, on, &lis3_dev.async_queue);
}

static const struct file_operations lis3lv02d_misc_fops = {
	.owner   = THIS_MODULE,
	.llseek  = no_llseek,
	.read    = lis3lv02d_misc_read,
	.open    = lis3lv02d_misc_open,
	.release = lis3lv02d_misc_release,
	.poll    = lis3lv02d_misc_poll,
	.fasync  = lis3lv02d_misc_fasync,
};

static struct miscdevice lis3lv02d_misc_device = {
	.minor   = MISC_DYNAMIC_MINOR,
	.name    = "freefall",
	.fops    = &lis3lv02d_misc_fops,
};

int lis3lv02d_joystick_enable(void)
{
	struct input_dev *input_dev;
	int err;
	int max_val, fuzz, flat;
	int btns[] = {BTN_X, BTN_Y, BTN_Z};

	if (lis3_dev.idev)
		return -EINVAL;

	lis3_dev.idev = input_allocate_polled_device();
	if (!lis3_dev.idev)
		return -ENOMEM;

	lis3_dev.idev->poll = lis3lv02d_joystick_poll;
	lis3_dev.idev->open = lis3lv02d_joystick_open;
	lis3_dev.idev->close = lis3lv02d_joystick_close;
	lis3_dev.idev->poll_interval = MDPS_POLL_INTERVAL;
	lis3_dev.idev->poll_interval_min = MDPS_POLL_MIN;
	lis3_dev.idev->poll_interval_max = MDPS_POLL_MAX;
	input_dev = lis3_dev.idev->input;

	input_dev->name       = "ST LIS3LV02DL Accelerometer";
	input_dev->phys       = DRIVER_NAME "/input0";
	input_dev->id.bustype = BUS_HOST;
	input_dev->id.vendor  = 0;
	input_dev->dev.parent = &lis3_dev.pdev->dev;

	set_bit(EV_ABS, input_dev->evbit);
	max_val = (lis3_dev.mdps_max_val * lis3_dev.scale) / LIS3_ACCURACY;
	if (lis3_dev.whoami == WAI_12B) {
		fuzz = LIS3_DEFAULT_FUZZ_12B;
		flat = LIS3_DEFAULT_FLAT_12B;
	} else {
		fuzz = LIS3_DEFAULT_FUZZ_8B;
		flat = LIS3_DEFAULT_FLAT_8B;
	}
	fuzz = (fuzz * lis3_dev.scale) / LIS3_ACCURACY;
	flat = (flat * lis3_dev.scale) / LIS3_ACCURACY;

	input_set_abs_params(input_dev, ABS_X, -max_val, max_val, fuzz, flat);
	input_set_abs_params(input_dev, ABS_Y, -max_val, max_val, fuzz, flat);
	input_set_abs_params(input_dev, ABS_Z, -max_val, max_val, fuzz, flat);

	lis3_dev.mapped_btns[0] = lis3lv02d_get_axis(abs(lis3_dev.ac.x), btns);
	lis3_dev.mapped_btns[1] = lis3lv02d_get_axis(abs(lis3_dev.ac.y), btns);
	lis3_dev.mapped_btns[2] = lis3lv02d_get_axis(abs(lis3_dev.ac.z), btns);

	err = input_register_polled_device(lis3_dev.idev);
	if (err) {
		input_free_polled_device(lis3_dev.idev);
		lis3_dev.idev = NULL;
	}

	return err;
}
EXPORT_SYMBOL_GPL(lis3lv02d_joystick_enable);

void lis3lv02d_joystick_disable(void)
{
	if (lis3_dev.irq)
		free_irq(lis3_dev.irq, &lis3_dev);
	if (lis3_dev.pdata && lis3_dev.pdata->irq2)
		free_irq(lis3_dev.pdata->irq2, &lis3_dev);

	if (!lis3_dev.idev)
		return;

	if (lis3_dev.irq)
		misc_deregister(&lis3lv02d_misc_device);
	input_unregister_polled_device(lis3_dev.idev);
	input_free_polled_device(lis3_dev.idev);
	lis3_dev.idev = NULL;
}
EXPORT_SYMBOL_GPL(lis3lv02d_joystick_disable);

/* Sysfs stuff */
static void lis3lv02d_sysfs_poweron(struct lis3lv02d *lis3)
{
	/*
	 * SYSFS functions are fast visitors so put-call
	 * immediately after the get-call. However, keep
	 * chip running for a while and schedule delayed
	 * suspend. This way periodic sysfs calls doesn't
	 * suffer from relatively long power up time.
	 */

	if (lis3->pm_dev) {
		pm_runtime_get_sync(lis3->pm_dev);
		pm_runtime_put_noidle(lis3->pm_dev);
		pm_schedule_suspend(lis3->pm_dev, LIS3_SYSFS_POWERDOWN_DELAY);
	}
}

static ssize_t lis3lv02d_selftest_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	s16 values[3];

	static const char ok[] = "OK";
	static const char fail[] = "FAIL";
	static const char irq[] = "FAIL_IRQ";
	const char *res;

	lis3lv02d_sysfs_poweron(&lis3_dev);
	switch (lis3lv02d_selftest(&lis3_dev, values)) {
	case SELFTEST_FAIL:
		res = fail;
		break;
	case SELFTEST_IRQ:
		res = irq;
		break;
	case SELFTEST_OK:
	default:
		res = ok;
		break;
	}
	return sprintf(buf, "%s %d %d %d\n", res,
		values[0], values[1], values[2]);
}

static ssize_t lis3lv02d_position_show(struct device *dev,
				struct device_attribute *attr, char *buf)
{
	int x, y, z;

	lis3lv02d_sysfs_poweron(&lis3_dev);
	mutex_lock(&lis3_dev.mutex);
	lis3lv02d_get_xyz(&lis3_dev, &x, &y, &z);
	mutex_unlock(&lis3_dev.mutex);
	return sprintf(buf, "(%d,%d,%d)\n", x, y, z);
}

static ssize_t lis3lv02d_rate_show(struct device *dev,
			struct device_attribute *attr, char *buf)
{
	lis3lv02d_sysfs_poweron(&lis3_dev);
	return sprintf(buf, "%d\n", lis3lv02d_get_odr());
}

static ssize_t lis3lv02d_rate_set(struct device *dev,
				struct device_attribute *attr, const char *buf,
				size_t count)
{
	unsigned long rate;

	if (strict_strtoul(buf, 0, &rate))
		return -EINVAL;

	lis3lv02d_sysfs_poweron(&lis3_dev);
	if (lis3lv02d_set_odr(rate))
		return -EINVAL;

	return count;
}

static DEVICE_ATTR(selftest, S_IRUSR, lis3lv02d_selftest_show, NULL);
static DEVICE_ATTR(position, S_IRUGO, lis3lv02d_position_show, NULL);
static DEVICE_ATTR(rate, S_IRUGO | S_IWUSR, lis3lv02d_rate_show,
					    lis3lv02d_rate_set);

static struct attribute *lis3lv02d_attributes[] = {
	&dev_attr_selftest.attr,
	&dev_attr_position.attr,
	&dev_attr_rate.attr,
	NULL
};

static struct attribute_group lis3lv02d_attribute_group = {
	.attrs = lis3lv02d_attributes
};


static int lis3lv02d_add_fs(struct lis3lv02d *lis3)
{
	lis3->pdev = platform_device_register_simple(DRIVER_NAME, -1, NULL, 0);
	if (IS_ERR(lis3->pdev))
		return PTR_ERR(lis3->pdev);

	return sysfs_create_group(&lis3->pdev->dev.kobj, &lis3lv02d_attribute_group);
}

int lis3lv02d_remove_fs(struct lis3lv02d *lis3)
{
	sysfs_remove_group(&lis3->pdev->dev.kobj, &lis3lv02d_attribute_group);
	platform_device_unregister(lis3->pdev);
	if (lis3->pm_dev) {
		/* Barrier after the sysfs remove */
		pm_runtime_barrier(lis3->pm_dev);

		/* SYSFS may have left chip running. Turn off if necessary */
		if (!pm_runtime_suspended(lis3->pm_dev))
			lis3lv02d_poweroff(&lis3_dev);

		pm_runtime_disable(lis3->pm_dev);
		pm_runtime_set_suspended(lis3->pm_dev);
	}
	kfree(lis3->reg_cache);
	return 0;
}
EXPORT_SYMBOL_GPL(lis3lv02d_remove_fs);

static void lis3lv02d_8b_configure(struct lis3lv02d *dev,
				struct lis3lv02d_platform_data *p)
{
	int err;
	int ctrl2 = p->hipass_ctrl;

	if (p->click_flags) {
		dev->write(dev, CLICK_CFG, p->click_flags);
		dev->write(dev, CLICK_TIMELIMIT, p->click_time_limit);
		dev->write(dev, CLICK_LATENCY, p->click_latency);
		dev->write(dev, CLICK_WINDOW, p->click_window);
		dev->write(dev, CLICK_THSZ, p->click_thresh_z & 0xf);
		dev->write(dev, CLICK_THSY_X,
			(p->click_thresh_x & 0xf) |
			(p->click_thresh_y << 4));

		if (dev->idev) {
			struct input_dev *input_dev = lis3_dev.idev->input;
			input_set_capability(input_dev, EV_KEY, BTN_X);
			input_set_capability(input_dev, EV_KEY, BTN_Y);
			input_set_capability(input_dev, EV_KEY, BTN_Z);
		}
	}

	if (p->wakeup_flags) {
		dev->write(dev, FF_WU_CFG_1, p->wakeup_flags);
		dev->write(dev, FF_WU_THS_1, p->wakeup_thresh & 0x7f);
		/* pdata value + 1 to keep this backward compatible*/
		dev->write(dev, FF_WU_DURATION_1, p->duration1 + 1);
		ctrl2 ^= HP_FF_WU1; /* Xor to keep compatible with old pdata*/
	}

	if (p->wakeup_flags2) {
		dev->write(dev, FF_WU_CFG_2, p->wakeup_flags2);
		dev->write(dev, FF_WU_THS_2, p->wakeup_thresh2 & 0x7f);
		/* pdata value + 1 to keep this backward compatible*/
		dev->write(dev, FF_WU_DURATION_2, p->duration2 + 1);
		ctrl2 ^= HP_FF_WU2; /* Xor to keep compatible with old pdata*/
	}
	/* Configure hipass filters */
	dev->write(dev, CTRL_REG2, ctrl2);

	if (p->irq2) {
		err = request_threaded_irq(p->irq2,
					NULL,
					lis302dl_interrupt_thread2_8b,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT |
					(p->irq_flags2 & IRQF_TRIGGER_MASK),
					DRIVER_NAME, &lis3_dev);
		if (err < 0)
			printk(KERN_ERR DRIVER_NAME
				"No second IRQ. Limited functionality\n");
	}
}

/*
 * Initialise the accelerometer and the various subsystems.
 * Should be rather independent of the bus system.
 */
int lis3lv02d_init_device(struct lis3lv02d *dev)
{
	int err;
	irq_handler_t thread_fn;
	int irq_flags = 0;

	dev->whoami = lis3lv02d_read_8(dev, WHO_AM_I);

	switch (dev->whoami) {
	case WAI_12B:
		printk(KERN_INFO DRIVER_NAME ": 12 bits sensor found\n");
		dev->read_data = lis3lv02d_read_12;
		dev->mdps_max_val = 2048;
		dev->pwron_delay = LIS3_PWRON_DELAY_WAI_12B;
		dev->odrs = lis3_12_rates;
		dev->odr_mask = CTRL1_DF0 | CTRL1_DF1;
		dev->scale = LIS3_SENSITIVITY_12B;
		dev->regs = lis3_wai12_regs;
		dev->regs_size = ARRAY_SIZE(lis3_wai12_regs);
		break;
	case WAI_8B:
		printk(KERN_INFO DRIVER_NAME ": 8 bits sensor found\n");
		dev->read_data = lis3lv02d_read_8;
		dev->mdps_max_val = 128;
		dev->pwron_delay = LIS3_PWRON_DELAY_WAI_8B;
		dev->odrs = lis3_8_rates;
		dev->odr_mask = CTRL1_DR;
		dev->scale = LIS3_SENSITIVITY_8B;
		dev->regs = lis3_wai8_regs;
		dev->regs_size = ARRAY_SIZE(lis3_wai8_regs);
		break;
	case WAI_3DC:
		printk(KERN_INFO DRIVER_NAME ": 8 bits 3DC sensor found\n");
		dev->read_data = lis3lv02d_read_8;
		dev->mdps_max_val = 128;
		dev->pwron_delay = LIS3_PWRON_DELAY_WAI_8B;
		dev->odrs = lis3_3dc_rates;
		dev->odr_mask = CTRL1_ODR0|CTRL1_ODR1|CTRL1_ODR2|CTRL1_ODR3;
		dev->scale = LIS3_SENSITIVITY_8B;
		break;
	default:
		printk(KERN_ERR DRIVER_NAME
			": unknown sensor type 0x%X\n", dev->whoami);
		return -EINVAL;
	}

	dev->reg_cache = kzalloc(max(sizeof(lis3_wai8_regs),
				     sizeof(lis3_wai12_regs)), GFP_KERNEL);

	if (dev->reg_cache == NULL) {
		printk(KERN_ERR DRIVER_NAME "out of memory\n");
		return -ENOMEM;
	}

	mutex_init(&dev->mutex);
	atomic_set(&dev->wake_thread, 0);

	lis3lv02d_add_fs(dev);
	lis3lv02d_poweron(dev);

	if (dev->pm_dev) {
		pm_runtime_set_active(dev->pm_dev);
		pm_runtime_enable(dev->pm_dev);
	}

	if (lis3lv02d_joystick_enable())
		printk(KERN_ERR DRIVER_NAME ": joystick initialization failed\n");

	/* passing in platform specific data is purely optional and only
	 * used by the SPI transport layer at the moment */
	if (dev->pdata) {
		struct lis3lv02d_platform_data *p = dev->pdata;

		if (dev->whoami == WAI_8B)
			lis3lv02d_8b_configure(dev, p);

		irq_flags = p->irq_flags1 & IRQF_TRIGGER_MASK;

		dev->irq_cfg = p->irq_cfg;
		if (p->irq_cfg)
			dev->write(dev, CTRL_REG3, p->irq_cfg);

		if (p->default_rate)
			lis3lv02d_set_odr(p->default_rate);
	}

	/* bail if we did not get an IRQ from the bus layer */
	if (!dev->irq) {
		printk(KERN_ERR DRIVER_NAME
			": No IRQ. Disabling /dev/freefall\n");
		goto out;
	}

	/*
	 * The sensor can generate interrupts for free-fall and direction
	 * detection (distinguishable with FF_WU_SRC and DD_SRC) but to keep
	 * the things simple and _fast_ we activate it only for free-fall, so
	 * no need to read register (very slow with ACPI). For the same reason,
	 * we forbid shared interrupts.
	 *
	 * IRQF_TRIGGER_RISING seems pointless on HP laptops because the
	 * io-apic is not configurable (and generates a warning) but I keep it
	 * in case of support for other hardware.
	 */
	if (dev->pdata && dev->whoami == WAI_8B)
		thread_fn = lis302dl_interrupt_thread1_8b;
	else
		thread_fn = NULL;

	err = request_threaded_irq(dev->irq, lis302dl_interrupt,
				thread_fn,
				IRQF_TRIGGER_RISING | IRQF_ONESHOT |
				irq_flags,
				DRIVER_NAME, &lis3_dev);

	if (err < 0) {
		printk(KERN_ERR DRIVER_NAME "Cannot get IRQ\n");
		goto out;
	}

	if (misc_register(&lis3lv02d_misc_device))
		printk(KERN_ERR DRIVER_NAME ": misc_register failed\n");
out:
	return 0;
}
EXPORT_SYMBOL_GPL(lis3lv02d_init_device);

MODULE_DESCRIPTION("ST LIS3LV02Dx three-axis digital accelerometer driver");
MODULE_AUTHOR("Yan Burman, Eric Piel, Pavel Machek");
MODULE_LICENSE("GPL");
