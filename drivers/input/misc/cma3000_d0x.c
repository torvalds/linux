// SPDX-License-Identifier: GPL-2.0-only
/*
 * VTI CMA3000_D0x Accelerometer driver
 *
 * Copyright (C) 2010 Texas Instruments
 * Author: Hemanth V <hemanthv@ti.com>
 */

#include <linux/types.h>
#include <linux/interrupt.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/input.h>
#include <linux/input/cma3000.h>
#include <linux/module.h>

#include "cma3000_d0x.h"

#define CMA3000_WHOAMI      0x00
#define CMA3000_REVID       0x01
#define CMA3000_CTRL        0x02
#define CMA3000_STATUS      0x03
#define CMA3000_RSTR        0x04
#define CMA3000_INTSTATUS   0x05
#define CMA3000_DOUTX       0x06
#define CMA3000_DOUTY       0x07
#define CMA3000_DOUTZ       0x08
#define CMA3000_MDTHR       0x09
#define CMA3000_MDFFTMR     0x0A
#define CMA3000_FFTHR       0x0B

#define CMA3000_RANGE2G    (1 << 7)
#define CMA3000_RANGE8G    (0 << 7)
#define CMA3000_BUSI2C     (0 << 4)
#define CMA3000_MODEMASK   (7 << 1)
#define CMA3000_GRANGEMASK (1 << 7)

#define CMA3000_STATUS_PERR    1
#define CMA3000_INTSTATUS_FFDET (1 << 2)

/* Settling time delay in ms */
#define CMA3000_SETDELAY    30

/* Delay for clearing interrupt in us */
#define CMA3000_INTDELAY    44


/*
 * Bit weights in mg for bit 0, other bits need
 * multiply factor 2^n. Eight bit is the sign bit.
 */
#define BIT_TO_2G  18
#define BIT_TO_8G  71

struct cma3000_accl_data {
	const struct cma3000_bus_ops *bus_ops;
	const struct cma3000_platform_data *pdata;

	struct device *dev;
	struct input_dev *input_dev;

	int bit_to_mg;
	int irq;

	int g_range;
	u8 mode;

	struct mutex mutex;
	bool opened;
	bool suspended;
};

#define CMA3000_READ(data, reg, msg) \
	(data->bus_ops->read(data->dev, reg, msg))
#define CMA3000_SET(data, reg, val, msg) \
	((data)->bus_ops->write(data->dev, reg, val, msg))

/*
 * Conversion for each of the eight modes to g, depending
 * on G range i.e 2G or 8G. Some modes always operate in
 * 8G.
 */

static int mode_to_mg[8][2] = {
	{ 0, 0 },
	{ BIT_TO_8G, BIT_TO_2G },
	{ BIT_TO_8G, BIT_TO_2G },
	{ BIT_TO_8G, BIT_TO_8G },
	{ BIT_TO_8G, BIT_TO_8G },
	{ BIT_TO_8G, BIT_TO_2G },
	{ BIT_TO_8G, BIT_TO_2G },
	{ 0, 0},
};

static void decode_mg(struct cma3000_accl_data *data, int *datax,
				int *datay, int *dataz)
{
	/* Data in 2's complement, convert to mg */
	*datax = ((s8)*datax) * data->bit_to_mg;
	*datay = ((s8)*datay) * data->bit_to_mg;
	*dataz = ((s8)*dataz) * data->bit_to_mg;
}

static irqreturn_t cma3000_thread_irq(int irq, void *dev_id)
{
	struct cma3000_accl_data *data = dev_id;
	int datax, datay, dataz, intr_status;
	u8 ctrl, mode, range;

	intr_status = CMA3000_READ(data, CMA3000_INTSTATUS, "interrupt status");
	if (intr_status < 0)
		return IRQ_NONE;

	/* Check if free fall is detected, report immediately */
	if (intr_status & CMA3000_INTSTATUS_FFDET) {
		input_report_abs(data->input_dev, ABS_MISC, 1);
		input_sync(data->input_dev);
	} else {
		input_report_abs(data->input_dev, ABS_MISC, 0);
	}

	datax = CMA3000_READ(data, CMA3000_DOUTX, "X");
	datay = CMA3000_READ(data, CMA3000_DOUTY, "Y");
	dataz = CMA3000_READ(data, CMA3000_DOUTZ, "Z");

	ctrl = CMA3000_READ(data, CMA3000_CTRL, "ctrl");
	mode = (ctrl & CMA3000_MODEMASK) >> 1;
	range = (ctrl & CMA3000_GRANGEMASK) >> 7;

	data->bit_to_mg = mode_to_mg[mode][range];

	/* Interrupt not for this device */
	if (data->bit_to_mg == 0)
		return IRQ_NONE;

	/* Decode register values to milli g */
	decode_mg(data, &datax, &datay, &dataz);

	input_report_abs(data->input_dev, ABS_X, datax);
	input_report_abs(data->input_dev, ABS_Y, datay);
	input_report_abs(data->input_dev, ABS_Z, dataz);
	input_sync(data->input_dev);

	return IRQ_HANDLED;
}

static int cma3000_reset(struct cma3000_accl_data *data)
{
	int val;

	/* Reset sequence */
	CMA3000_SET(data, CMA3000_RSTR, 0x02, "Reset");
	CMA3000_SET(data, CMA3000_RSTR, 0x0A, "Reset");
	CMA3000_SET(data, CMA3000_RSTR, 0x04, "Reset");

	/* Settling time delay */
	mdelay(10);

	val = CMA3000_READ(data, CMA3000_STATUS, "Status");
	if (val < 0) {
		dev_err(data->dev, "Reset failed\n");
		return val;
	}

	if (val & CMA3000_STATUS_PERR) {
		dev_err(data->dev, "Parity Error\n");
		return -EIO;
	}

	return 0;
}

static int cma3000_poweron(struct cma3000_accl_data *data)
{
	const struct cma3000_platform_data *pdata = data->pdata;
	u8 ctrl = 0;
	int ret;

	if (data->g_range == CMARANGE_2G) {
		ctrl = (data->mode << 1) | CMA3000_RANGE2G;
	} else if (data->g_range == CMARANGE_8G) {
		ctrl = (data->mode << 1) | CMA3000_RANGE8G;
	} else {
		dev_info(data->dev,
			 "Invalid G range specified, assuming 8G\n");
		ctrl = (data->mode << 1) | CMA3000_RANGE8G;
	}

	ctrl |= data->bus_ops->ctrl_mod;

	CMA3000_SET(data, CMA3000_MDTHR, pdata->mdthr,
		    "Motion Detect Threshold");
	CMA3000_SET(data, CMA3000_MDFFTMR, pdata->mdfftmr,
		    "Time register");
	CMA3000_SET(data, CMA3000_FFTHR, pdata->ffthr,
		    "Free fall threshold");
	ret = CMA3000_SET(data, CMA3000_CTRL, ctrl, "Mode setting");
	if (ret < 0)
		return -EIO;

	msleep(CMA3000_SETDELAY);

	return 0;
}

static int cma3000_poweroff(struct cma3000_accl_data *data)
{
	int ret;

	ret = CMA3000_SET(data, CMA3000_CTRL, CMAMODE_POFF, "Mode setting");
	msleep(CMA3000_SETDELAY);

	return ret;
}

static int cma3000_open(struct input_dev *input_dev)
{
	struct cma3000_accl_data *data = input_get_drvdata(input_dev);

	mutex_lock(&data->mutex);

	if (!data->suspended)
		cma3000_poweron(data);

	data->opened = true;

	mutex_unlock(&data->mutex);

	return 0;
}

static void cma3000_close(struct input_dev *input_dev)
{
	struct cma3000_accl_data *data = input_get_drvdata(input_dev);

	mutex_lock(&data->mutex);

	if (!data->suspended)
		cma3000_poweroff(data);

	data->opened = false;

	mutex_unlock(&data->mutex);
}

void cma3000_suspend(struct cma3000_accl_data *data)
{
	mutex_lock(&data->mutex);

	if (!data->suspended && data->opened)
		cma3000_poweroff(data);

	data->suspended = true;

	mutex_unlock(&data->mutex);
}
EXPORT_SYMBOL(cma3000_suspend);


void cma3000_resume(struct cma3000_accl_data *data)
{
	mutex_lock(&data->mutex);

	if (data->suspended && data->opened)
		cma3000_poweron(data);

	data->suspended = false;

	mutex_unlock(&data->mutex);
}
EXPORT_SYMBOL(cma3000_resume);

struct cma3000_accl_data *cma3000_init(struct device *dev, int irq,
				       const struct cma3000_bus_ops *bops)
{
	const struct cma3000_platform_data *pdata = dev_get_platdata(dev);
	struct cma3000_accl_data *data;
	struct input_dev *input_dev;
	int rev;
	int error;

	if (!pdata) {
		dev_err(dev, "platform data not found\n");
		error = -EINVAL;
		goto err_out;
	}


	/* if no IRQ return error */
	if (irq == 0) {
		error = -EINVAL;
		goto err_out;
	}

	data = kzalloc(sizeof(*data), GFP_KERNEL);
	input_dev = input_allocate_device();
	if (!data || !input_dev) {
		error = -ENOMEM;
		goto err_free_mem;
	}

	data->dev = dev;
	data->input_dev = input_dev;
	data->bus_ops = bops;
	data->pdata = pdata;
	data->irq = irq;
	mutex_init(&data->mutex);

	data->mode = pdata->mode;
	if (data->mode > CMAMODE_POFF) {
		data->mode = CMAMODE_MOTDET;
		dev_warn(dev,
			 "Invalid mode specified, assuming Motion Detect\n");
	}

	data->g_range = pdata->g_range;
	if (data->g_range != CMARANGE_2G && data->g_range != CMARANGE_8G) {
		dev_info(dev,
			 "Invalid G range specified, assuming 8G\n");
		data->g_range = CMARANGE_8G;
	}

	input_dev->name = "cma3000-accelerometer";
	input_dev->id.bustype = bops->bustype;
	input_dev->open = cma3000_open;
	input_dev->close = cma3000_close;

	input_set_abs_params(input_dev, ABS_X,
			-data->g_range, data->g_range, pdata->fuzz_x, 0);
	input_set_abs_params(input_dev, ABS_Y,
			-data->g_range, data->g_range, pdata->fuzz_y, 0);
	input_set_abs_params(input_dev, ABS_Z,
			-data->g_range, data->g_range, pdata->fuzz_z, 0);
	input_set_abs_params(input_dev, ABS_MISC, 0, 1, 0, 0);

	input_set_drvdata(input_dev, data);

	error = cma3000_reset(data);
	if (error)
		goto err_free_mem;

	rev = CMA3000_READ(data, CMA3000_REVID, "Revid");
	if (rev < 0) {
		error = rev;
		goto err_free_mem;
	}

	pr_info("CMA3000 Accelerometer: Revision %x\n", rev);

	error = request_threaded_irq(irq, NULL, cma3000_thread_irq,
				     pdata->irqflags | IRQF_ONESHOT,
				     "cma3000_d0x", data);
	if (error) {
		dev_err(dev, "request_threaded_irq failed\n");
		goto err_free_mem;
	}

	error = input_register_device(data->input_dev);
	if (error) {
		dev_err(dev, "Unable to register input device\n");
		goto err_free_irq;
	}

	return data;

err_free_irq:
	free_irq(irq, data);
err_free_mem:
	input_free_device(input_dev);
	kfree(data);
err_out:
	return ERR_PTR(error);
}
EXPORT_SYMBOL(cma3000_init);

void cma3000_exit(struct cma3000_accl_data *data)
{
	free_irq(data->irq, data);
	input_unregister_device(data->input_dev);
	kfree(data);
}
EXPORT_SYMBOL(cma3000_exit);

MODULE_DESCRIPTION("CMA3000-D0x Accelerometer Driver");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("Hemanth V <hemanthv@ti.com>");
