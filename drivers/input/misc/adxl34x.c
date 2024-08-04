// SPDX-License-Identifier: GPL-2.0-or-later
/*
 * ADXL345/346 Three-Axis Digital Accelerometers
 *
 * Enter bugs at http://blackfin.uclinux.org/
 *
 * Copyright (C) 2009 Michael Hennerich, Analog Devices Inc.
 */

#include <linux/device.h>
#include <linux/delay.h>
#include <linux/input.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/slab.h>
#include <linux/workqueue.h>
#include <linux/input/adxl34x.h>
#include <linux/module.h>

#include "adxl34x.h"

/* ADXL345/6 Register Map */
#define DEVID		0x00	/* R   Device ID */
#define THRESH_TAP	0x1D	/* R/W Tap threshold */
#define OFSX		0x1E	/* R/W X-axis offset */
#define OFSY		0x1F	/* R/W Y-axis offset */
#define OFSZ		0x20	/* R/W Z-axis offset */
#define DUR		0x21	/* R/W Tap duration */
#define LATENT		0x22	/* R/W Tap latency */
#define WINDOW		0x23	/* R/W Tap window */
#define THRESH_ACT	0x24	/* R/W Activity threshold */
#define THRESH_INACT	0x25	/* R/W Inactivity threshold */
#define TIME_INACT	0x26	/* R/W Inactivity time */
#define ACT_INACT_CTL	0x27	/* R/W Axis enable control for activity and */
				/* inactivity detection */
#define THRESH_FF	0x28	/* R/W Free-fall threshold */
#define TIME_FF		0x29	/* R/W Free-fall time */
#define TAP_AXES	0x2A	/* R/W Axis control for tap/double tap */
#define ACT_TAP_STATUS	0x2B	/* R   Source of tap/double tap */
#define BW_RATE		0x2C	/* R/W Data rate and power mode control */
#define POWER_CTL	0x2D	/* R/W Power saving features control */
#define INT_ENABLE	0x2E	/* R/W Interrupt enable control */
#define INT_MAP		0x2F	/* R/W Interrupt mapping control */
#define INT_SOURCE	0x30	/* R   Source of interrupts */
#define DATA_FORMAT	0x31	/* R/W Data format control */
#define DATAX0		0x32	/* R   X-Axis Data 0 */
#define DATAX1		0x33	/* R   X-Axis Data 1 */
#define DATAY0		0x34	/* R   Y-Axis Data 0 */
#define DATAY1		0x35	/* R   Y-Axis Data 1 */
#define DATAZ0		0x36	/* R   Z-Axis Data 0 */
#define DATAZ1		0x37	/* R   Z-Axis Data 1 */
#define FIFO_CTL	0x38	/* R/W FIFO control */
#define FIFO_STATUS	0x39	/* R   FIFO status */
#define TAP_SIGN	0x3A	/* R   Sign and source for tap/double tap */
/* Orientation ADXL346 only */
#define ORIENT_CONF	0x3B	/* R/W Orientation configuration */
#define ORIENT		0x3C	/* R   Orientation status */

/* DEVIDs */
#define ID_ADXL345	0xE5
#define ID_ADXL346	0xE6

/* INT_ENABLE/INT_MAP/INT_SOURCE Bits */
#define DATA_READY	(1 << 7)
#define SINGLE_TAP	(1 << 6)
#define DOUBLE_TAP	(1 << 5)
#define ACTIVITY	(1 << 4)
#define INACTIVITY	(1 << 3)
#define FREE_FALL	(1 << 2)
#define WATERMARK	(1 << 1)
#define OVERRUN		(1 << 0)

/* ACT_INACT_CONTROL Bits */
#define ACT_ACDC	(1 << 7)
#define ACT_X_EN	(1 << 6)
#define ACT_Y_EN	(1 << 5)
#define ACT_Z_EN	(1 << 4)
#define INACT_ACDC	(1 << 3)
#define INACT_X_EN	(1 << 2)
#define INACT_Y_EN	(1 << 1)
#define INACT_Z_EN	(1 << 0)

/* TAP_AXES Bits */
#define SUPPRESS	(1 << 3)
#define TAP_X_EN	(1 << 2)
#define TAP_Y_EN	(1 << 1)
#define TAP_Z_EN	(1 << 0)

/* ACT_TAP_STATUS Bits */
#define ACT_X_SRC	(1 << 6)
#define ACT_Y_SRC	(1 << 5)
#define ACT_Z_SRC	(1 << 4)
#define ASLEEP		(1 << 3)
#define TAP_X_SRC	(1 << 2)
#define TAP_Y_SRC	(1 << 1)
#define TAP_Z_SRC	(1 << 0)

/* BW_RATE Bits */
#define LOW_POWER	(1 << 4)
#define RATE(x)		((x) & 0xF)

/* POWER_CTL Bits */
#define PCTL_LINK	(1 << 5)
#define PCTL_AUTO_SLEEP (1 << 4)
#define PCTL_MEASURE	(1 << 3)
#define PCTL_SLEEP	(1 << 2)
#define PCTL_WAKEUP(x)	((x) & 0x3)

/* DATA_FORMAT Bits */
#define SELF_TEST	(1 << 7)
#define SPI		(1 << 6)
#define INT_INVERT	(1 << 5)
#define FULL_RES	(1 << 3)
#define JUSTIFY		(1 << 2)
#define RANGE(x)	((x) & 0x3)
#define RANGE_PM_2g	0
#define RANGE_PM_4g	1
#define RANGE_PM_8g	2
#define RANGE_PM_16g	3

/*
 * Maximum value our axis may get in full res mode for the input device
 * (signed 13 bits)
 */
#define ADXL_FULLRES_MAX_VAL 4096

/*
 * Maximum value our axis may get in fixed res mode for the input device
 * (signed 10 bits)
 */
#define ADXL_FIXEDRES_MAX_VAL 512

/* FIFO_CTL Bits */
#define FIFO_MODE(x)	(((x) & 0x3) << 6)
#define FIFO_BYPASS	0
#define FIFO_FIFO	1
#define FIFO_STREAM	2
#define FIFO_TRIGGER	3
#define TRIGGER		(1 << 5)
#define SAMPLES(x)	((x) & 0x1F)

/* FIFO_STATUS Bits */
#define FIFO_TRIG	(1 << 7)
#define ENTRIES(x)	((x) & 0x3F)

/* TAP_SIGN Bits ADXL346 only */
#define XSIGN		(1 << 6)
#define YSIGN		(1 << 5)
#define ZSIGN		(1 << 4)
#define XTAP		(1 << 3)
#define YTAP		(1 << 2)
#define ZTAP		(1 << 1)

/* ORIENT_CONF ADXL346 only */
#define ORIENT_DEADZONE(x)	(((x) & 0x7) << 4)
#define ORIENT_DIVISOR(x)	((x) & 0x7)

/* ORIENT ADXL346 only */
#define ADXL346_2D_VALID		(1 << 6)
#define ADXL346_2D_ORIENT(x)		(((x) & 0x30) >> 4)
#define ADXL346_3D_VALID		(1 << 3)
#define ADXL346_3D_ORIENT(x)		((x) & 0x7)
#define ADXL346_2D_PORTRAIT_POS		0	/* +X */
#define ADXL346_2D_PORTRAIT_NEG		1	/* -X */
#define ADXL346_2D_LANDSCAPE_POS	2	/* +Y */
#define ADXL346_2D_LANDSCAPE_NEG	3	/* -Y */

#define ADXL346_3D_FRONT		3	/* +X */
#define ADXL346_3D_BACK			4	/* -X */
#define ADXL346_3D_RIGHT		2	/* +Y */
#define ADXL346_3D_LEFT			5	/* -Y */
#define ADXL346_3D_TOP			1	/* +Z */
#define ADXL346_3D_BOTTOM		6	/* -Z */

#undef ADXL_DEBUG

#define ADXL_X_AXIS			0
#define ADXL_Y_AXIS			1
#define ADXL_Z_AXIS			2

#define AC_READ(ac, reg)	((ac)->bops->read((ac)->dev, reg))
#define AC_WRITE(ac, reg, val)	((ac)->bops->write((ac)->dev, reg, val))

struct axis_triple {
	int x;
	int y;
	int z;
};

struct adxl34x {
	struct device *dev;
	struct input_dev *input;
	struct mutex mutex;	/* reentrant protection for struct */
	struct adxl34x_platform_data pdata;
	struct axis_triple swcal;
	struct axis_triple hwcal;
	struct axis_triple saved;
	char phys[32];
	unsigned orient2d_saved;
	unsigned orient3d_saved;
	bool disabled;	/* P: mutex */
	bool opened;	/* P: mutex */
	bool suspended;	/* P: mutex */
	bool fifo_delay;
	int irq;
	unsigned model;
	unsigned int_mask;

	const struct adxl34x_bus_ops *bops;
};

static const struct adxl34x_platform_data adxl34x_default_init = {
	.tap_threshold = 35,
	.tap_duration = 3,
	.tap_latency = 20,
	.tap_window = 20,
	.tap_axis_control = ADXL_TAP_X_EN | ADXL_TAP_Y_EN | ADXL_TAP_Z_EN,
	.act_axis_control = 0xFF,
	.activity_threshold = 6,
	.inactivity_threshold = 4,
	.inactivity_time = 3,
	.free_fall_threshold = 8,
	.free_fall_time = 0x20,
	.data_rate = 8,
	.data_range = ADXL_FULL_RES,

	.ev_type = EV_ABS,
	.ev_code_x = ABS_X,	/* EV_REL */
	.ev_code_y = ABS_Y,	/* EV_REL */
	.ev_code_z = ABS_Z,	/* EV_REL */

	.ev_code_tap = {BTN_TOUCH, BTN_TOUCH, BTN_TOUCH}, /* EV_KEY {x,y,z} */
	.power_mode = ADXL_AUTO_SLEEP | ADXL_LINK,
	.fifo_mode = ADXL_FIFO_STREAM,
	.watermark = 0,
};

static void adxl34x_get_triple(struct adxl34x *ac, struct axis_triple *axis)
{
	__le16 buf[3];

	ac->bops->read_block(ac->dev, DATAX0, DATAZ1 - DATAX0 + 1, buf);

	guard(mutex)(&ac->mutex);

	ac->saved.x = (s16) le16_to_cpu(buf[0]);
	axis->x = ac->saved.x;

	ac->saved.y = (s16) le16_to_cpu(buf[1]);
	axis->y = ac->saved.y;

	ac->saved.z = (s16) le16_to_cpu(buf[2]);
	axis->z = ac->saved.z;
}

static void adxl34x_service_ev_fifo(struct adxl34x *ac)
{
	struct adxl34x_platform_data *pdata = &ac->pdata;
	struct axis_triple axis;

	adxl34x_get_triple(ac, &axis);

	input_event(ac->input, pdata->ev_type, pdata->ev_code_x,
		    axis.x - ac->swcal.x);
	input_event(ac->input, pdata->ev_type, pdata->ev_code_y,
		    axis.y - ac->swcal.y);
	input_event(ac->input, pdata->ev_type, pdata->ev_code_z,
		    axis.z - ac->swcal.z);
}

static void adxl34x_report_key_single(struct input_dev *input, int key)
{
	input_report_key(input, key, true);
	input_sync(input);
	input_report_key(input, key, false);
}

static void adxl34x_send_key_events(struct adxl34x *ac,
		struct adxl34x_platform_data *pdata, int status, int press)
{
	int i;

	for (i = ADXL_X_AXIS; i <= ADXL_Z_AXIS; i++) {
		if (status & (1 << (ADXL_Z_AXIS - i)))
			input_report_key(ac->input,
					 pdata->ev_code_tap[i], press);
	}
}

static void adxl34x_do_tap(struct adxl34x *ac,
		struct adxl34x_platform_data *pdata, int status)
{
	adxl34x_send_key_events(ac, pdata, status, true);
	input_sync(ac->input);
	adxl34x_send_key_events(ac, pdata, status, false);
}

static irqreturn_t adxl34x_irq(int irq, void *handle)
{
	struct adxl34x *ac = handle;
	struct adxl34x_platform_data *pdata = &ac->pdata;
	int int_stat, tap_stat, samples, orient, orient_code;

	/*
	 * ACT_TAP_STATUS should be read before clearing the interrupt
	 * Avoid reading ACT_TAP_STATUS in case TAP detection is disabled
	 */

	if (pdata->tap_axis_control & (TAP_X_EN | TAP_Y_EN | TAP_Z_EN))
		tap_stat = AC_READ(ac, ACT_TAP_STATUS);
	else
		tap_stat = 0;

	int_stat = AC_READ(ac, INT_SOURCE);

	if (int_stat & FREE_FALL)
		adxl34x_report_key_single(ac->input, pdata->ev_code_ff);

	if (int_stat & OVERRUN)
		dev_dbg(ac->dev, "OVERRUN\n");

	if (int_stat & (SINGLE_TAP | DOUBLE_TAP)) {
		adxl34x_do_tap(ac, pdata, tap_stat);

		if (int_stat & DOUBLE_TAP)
			adxl34x_do_tap(ac, pdata, tap_stat);
	}

	if (pdata->ev_code_act_inactivity) {
		if (int_stat & ACTIVITY)
			input_report_key(ac->input,
					 pdata->ev_code_act_inactivity, 1);
		if (int_stat & INACTIVITY)
			input_report_key(ac->input,
					 pdata->ev_code_act_inactivity, 0);
	}

	/*
	 * ORIENTATION SENSING ADXL346 only
	 */
	if (pdata->orientation_enable) {
		orient = AC_READ(ac, ORIENT);
		if ((pdata->orientation_enable & ADXL_EN_ORIENTATION_2D) &&
		    (orient & ADXL346_2D_VALID)) {

			orient_code = ADXL346_2D_ORIENT(orient);
			/* Report orientation only when it changes */
			if (ac->orient2d_saved != orient_code) {
				ac->orient2d_saved = orient_code;
				adxl34x_report_key_single(ac->input,
					pdata->ev_codes_orient_2d[orient_code]);
			}
		}

		if ((pdata->orientation_enable & ADXL_EN_ORIENTATION_3D) &&
		    (orient & ADXL346_3D_VALID)) {

			orient_code = ADXL346_3D_ORIENT(orient) - 1;
			/* Report orientation only when it changes */
			if (ac->orient3d_saved != orient_code) {
				ac->orient3d_saved = orient_code;
				adxl34x_report_key_single(ac->input,
					pdata->ev_codes_orient_3d[orient_code]);
			}
		}
	}

	if (int_stat & (DATA_READY | WATERMARK)) {

		if (pdata->fifo_mode)
			samples = ENTRIES(AC_READ(ac, FIFO_STATUS)) + 1;
		else
			samples = 1;

		for (; samples > 0; samples--) {
			adxl34x_service_ev_fifo(ac);
			/*
			 * To ensure that the FIFO has
			 * completely popped, there must be at least 5 us between
			 * the end of reading the data registers, signified by the
			 * transition to register 0x38 from 0x37 or the CS pin
			 * going high, and the start of new reads of the FIFO or
			 * reading the FIFO_STATUS register. For SPI operation at
			 * 1.5 MHz or lower, the register addressing portion of the
			 * transmission is sufficient delay to ensure the FIFO has
			 * completely popped. It is necessary for SPI operation
			 * greater than 1.5 MHz to de-assert the CS pin to ensure a
			 * total of 5 us, which is at most 3.4 us at 5 MHz
			 * operation.
			 */
			if (ac->fifo_delay && (samples > 1))
				udelay(3);
		}
	}

	input_sync(ac->input);

	return IRQ_HANDLED;
}

static void __adxl34x_disable(struct adxl34x *ac)
{
	/*
	 * A '0' places the ADXL34x into standby mode
	 * with minimum power consumption.
	 */
	AC_WRITE(ac, POWER_CTL, 0);
}

static void __adxl34x_enable(struct adxl34x *ac)
{
	AC_WRITE(ac, POWER_CTL, ac->pdata.power_mode | PCTL_MEASURE);
}

static int adxl34x_suspend(struct device *dev)
{
	struct adxl34x *ac = dev_get_drvdata(dev);

	guard(mutex)(&ac->mutex);

	if (!ac->suspended && !ac->disabled && ac->opened)
		__adxl34x_disable(ac);

	ac->suspended = true;

	return 0;
}

static int adxl34x_resume(struct device *dev)
{
	struct adxl34x *ac = dev_get_drvdata(dev);

	guard(mutex)(&ac->mutex);

	if (ac->suspended && !ac->disabled && ac->opened)
		__adxl34x_enable(ac);

	ac->suspended = false;

	return 0;
}

static ssize_t adxl34x_disable_show(struct device *dev,
				    struct device_attribute *attr, char *buf)
{
	struct adxl34x *ac = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", ac->disabled);
}

static ssize_t adxl34x_disable_store(struct device *dev,
				     struct device_attribute *attr,
				     const char *buf, size_t count)
{
	struct adxl34x *ac = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;

	guard(mutex)(&ac->mutex);

	if (!ac->suspended && ac->opened) {
		if (val) {
			if (!ac->disabled)
				__adxl34x_disable(ac);
		} else {
			if (ac->disabled)
				__adxl34x_enable(ac);
		}
	}

	ac->disabled = !!val;

	return count;
}

static DEVICE_ATTR(disable, 0664, adxl34x_disable_show, adxl34x_disable_store);

static ssize_t adxl34x_calibrate_show(struct device *dev,
				      struct device_attribute *attr, char *buf)
{
	struct adxl34x *ac = dev_get_drvdata(dev);

	guard(mutex)(&ac->mutex);

	return sprintf(buf, "%d,%d,%d\n",
		       ac->hwcal.x * 4 + ac->swcal.x,
		       ac->hwcal.y * 4 + ac->swcal.y,
		       ac->hwcal.z * 4 + ac->swcal.z);
}

static ssize_t adxl34x_calibrate_store(struct device *dev,
				       struct device_attribute *attr,
				       const char *buf, size_t count)
{
	struct adxl34x *ac = dev_get_drvdata(dev);

	/*
	 * Hardware offset calibration has a resolution of 15.6 mg/LSB.
	 * We use HW calibration and handle the remaining bits in SW. (4mg/LSB)
	 */

	guard(mutex)(&ac->mutex);

	ac->hwcal.x -= (ac->saved.x / 4);
	ac->swcal.x = ac->saved.x % 4;

	ac->hwcal.y -= (ac->saved.y / 4);
	ac->swcal.y = ac->saved.y % 4;

	ac->hwcal.z -= (ac->saved.z / 4);
	ac->swcal.z = ac->saved.z % 4;

	AC_WRITE(ac, OFSX, (s8) ac->hwcal.x);
	AC_WRITE(ac, OFSY, (s8) ac->hwcal.y);
	AC_WRITE(ac, OFSZ, (s8) ac->hwcal.z);

	return count;
}

static DEVICE_ATTR(calibrate, 0664,
		   adxl34x_calibrate_show, adxl34x_calibrate_store);

static ssize_t adxl34x_rate_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct adxl34x *ac = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n", RATE(ac->pdata.data_rate));
}

static ssize_t adxl34x_rate_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct adxl34x *ac = dev_get_drvdata(dev);
	unsigned char val;
	int error;

	error = kstrtou8(buf, 10, &val);
	if (error)
		return error;

	guard(mutex)(&ac->mutex);

	ac->pdata.data_rate = RATE(val);
	AC_WRITE(ac, BW_RATE,
		 ac->pdata.data_rate |
			(ac->pdata.low_power_mode ? LOW_POWER : 0));

	return count;
}

static DEVICE_ATTR(rate, 0664, adxl34x_rate_show, adxl34x_rate_store);

static ssize_t adxl34x_autosleep_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct adxl34x *ac = dev_get_drvdata(dev);

	return sprintf(buf, "%u\n",
		ac->pdata.power_mode & (PCTL_AUTO_SLEEP | PCTL_LINK) ? 1 : 0);
}

static ssize_t adxl34x_autosleep_store(struct device *dev,
				  struct device_attribute *attr,
				  const char *buf, size_t count)
{
	struct adxl34x *ac = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	error = kstrtouint(buf, 10, &val);
	if (error)
		return error;

	guard(mutex)(&ac->mutex);

	if (val)
		ac->pdata.power_mode |= (PCTL_AUTO_SLEEP | PCTL_LINK);
	else
		ac->pdata.power_mode &= ~(PCTL_AUTO_SLEEP | PCTL_LINK);

	if (!ac->disabled && !ac->suspended && ac->opened)
		AC_WRITE(ac, POWER_CTL, ac->pdata.power_mode | PCTL_MEASURE);

	return count;
}

static DEVICE_ATTR(autosleep, 0664,
		   adxl34x_autosleep_show, adxl34x_autosleep_store);

static ssize_t adxl34x_position_show(struct device *dev,
				 struct device_attribute *attr, char *buf)
{
	struct adxl34x *ac = dev_get_drvdata(dev);

	guard(mutex)(&ac->mutex);

	return sprintf(buf, "(%d, %d, %d)\n",
		       ac->saved.x, ac->saved.y, ac->saved.z);
}

static DEVICE_ATTR(position, S_IRUGO, adxl34x_position_show, NULL);

#ifdef ADXL_DEBUG
static ssize_t adxl34x_write_store(struct device *dev,
				   struct device_attribute *attr,
				   const char *buf, size_t count)
{
	struct adxl34x *ac = dev_get_drvdata(dev);
	unsigned int val;
	int error;

	/*
	 * This allows basic ADXL register write access for debug purposes.
	 */
	error = kstrtouint(buf, 16, &val);
	if (error)
		return error;

	guard(mutex)(&ac->mutex);
	AC_WRITE(ac, val >> 8, val & 0xFF);

	return count;
}

static DEVICE_ATTR(write, 0664, NULL, adxl34x_write_store);
#endif

static struct attribute *adxl34x_attributes[] = {
	&dev_attr_disable.attr,
	&dev_attr_calibrate.attr,
	&dev_attr_rate.attr,
	&dev_attr_autosleep.attr,
	&dev_attr_position.attr,
#ifdef ADXL_DEBUG
	&dev_attr_write.attr,
#endif
	NULL
};

static const struct attribute_group adxl34x_attr_group = {
	.attrs = adxl34x_attributes,
};

const struct attribute_group *adxl34x_groups[] = {
	&adxl34x_attr_group,
	NULL
};
EXPORT_SYMBOL_GPL(adxl34x_groups);

static int adxl34x_input_open(struct input_dev *input)
{
	struct adxl34x *ac = input_get_drvdata(input);

	guard(mutex)(&ac->mutex);

	if (!ac->suspended && !ac->disabled)
		__adxl34x_enable(ac);

	ac->opened = true;

	return 0;
}

static void adxl34x_input_close(struct input_dev *input)
{
	struct adxl34x *ac = input_get_drvdata(input);

	guard(mutex)(&ac->mutex);

	if (!ac->suspended && !ac->disabled)
		__adxl34x_disable(ac);

	ac->opened = false;
}

struct adxl34x *adxl34x_probe(struct device *dev, int irq,
			      bool fifo_delay_default,
			      const struct adxl34x_bus_ops *bops)
{
	struct adxl34x *ac;
	struct input_dev *input_dev;
	const struct adxl34x_platform_data *pdata;
	int error, range, i;
	int revid;

	if (!irq) {
		dev_err(dev, "no IRQ?\n");
		return ERR_PTR(-ENODEV);
	}

	ac = devm_kzalloc(dev, sizeof(*ac), GFP_KERNEL);
	if (!ac)
		return ERR_PTR(-ENOMEM);

	input_dev = devm_input_allocate_device(dev);
	if (!input_dev)
		return ERR_PTR(-ENOMEM);

	ac->fifo_delay = fifo_delay_default;

	pdata = dev_get_platdata(dev);
	if (!pdata) {
		dev_dbg(dev,
			"No platform data: Using default initialization\n");
		pdata = &adxl34x_default_init;
	}

	ac->pdata = *pdata;
	pdata = &ac->pdata;

	ac->input = input_dev;
	ac->dev = dev;
	ac->irq = irq;
	ac->bops = bops;

	mutex_init(&ac->mutex);

	input_dev->name = "ADXL34x accelerometer";
	revid = AC_READ(ac, DEVID);

	switch (revid) {
	case ID_ADXL345:
		ac->model = 345;
		break;
	case ID_ADXL346:
		ac->model = 346;
		break;
	default:
		dev_err(dev, "Failed to probe %s\n", input_dev->name);
		return ERR_PTR(-ENODEV);
	}

	snprintf(ac->phys, sizeof(ac->phys), "%s/input0", dev_name(dev));

	input_dev->phys = ac->phys;
	input_dev->id.product = ac->model;
	input_dev->id.bustype = bops->bustype;
	input_dev->open = adxl34x_input_open;
	input_dev->close = adxl34x_input_close;

	input_set_drvdata(input_dev, ac);

	if (ac->pdata.ev_type == EV_REL) {
		input_set_capability(input_dev, EV_REL, REL_X);
		input_set_capability(input_dev, EV_REL, REL_Y);
		input_set_capability(input_dev, EV_REL, REL_Z);
	} else {
		/* EV_ABS */
		if (pdata->data_range & FULL_RES)
			range = ADXL_FULLRES_MAX_VAL;	/* Signed 13-bit */
		else
			range = ADXL_FIXEDRES_MAX_VAL;	/* Signed 10-bit */

		input_set_abs_params(input_dev, ABS_X, -range, range, 3, 3);
		input_set_abs_params(input_dev, ABS_Y, -range, range, 3, 3);
		input_set_abs_params(input_dev, ABS_Z, -range, range, 3, 3);
	}

	input_set_capability(input_dev, EV_KEY, pdata->ev_code_tap[ADXL_X_AXIS]);
	input_set_capability(input_dev, EV_KEY, pdata->ev_code_tap[ADXL_Y_AXIS]);
	input_set_capability(input_dev, EV_KEY, pdata->ev_code_tap[ADXL_Z_AXIS]);

	if (pdata->ev_code_ff) {
		ac->int_mask = FREE_FALL;
		input_set_capability(input_dev, EV_KEY, pdata->ev_code_ff);
	}

	if (pdata->ev_code_act_inactivity)
		input_set_capability(input_dev, EV_KEY,
				     pdata->ev_code_act_inactivity);

	ac->int_mask |= ACTIVITY | INACTIVITY;

	if (pdata->watermark) {
		ac->int_mask |= WATERMARK;
		if (FIFO_MODE(pdata->fifo_mode) == FIFO_BYPASS)
			ac->pdata.fifo_mode |= FIFO_STREAM;
	} else {
		ac->int_mask |= DATA_READY;
	}

	if (pdata->tap_axis_control & (TAP_X_EN | TAP_Y_EN | TAP_Z_EN))
		ac->int_mask |= SINGLE_TAP | DOUBLE_TAP;

	if (FIFO_MODE(pdata->fifo_mode) == FIFO_BYPASS)
		ac->fifo_delay = false;

	AC_WRITE(ac, POWER_CTL, 0);

	error = devm_request_threaded_irq(dev, ac->irq, NULL, adxl34x_irq,
					  IRQF_ONESHOT, dev_name(dev), ac);
	if (error) {
		dev_err(dev, "irq %d busy?\n", ac->irq);
		return ERR_PTR(error);
	}

	error = input_register_device(input_dev);
	if (error)
		return ERR_PTR(error);

	AC_WRITE(ac, OFSX, pdata->x_axis_offset);
	ac->hwcal.x = pdata->x_axis_offset;
	AC_WRITE(ac, OFSY, pdata->y_axis_offset);
	ac->hwcal.y = pdata->y_axis_offset;
	AC_WRITE(ac, OFSZ, pdata->z_axis_offset);
	ac->hwcal.z = pdata->z_axis_offset;
	AC_WRITE(ac, THRESH_TAP, pdata->tap_threshold);
	AC_WRITE(ac, DUR, pdata->tap_duration);
	AC_WRITE(ac, LATENT, pdata->tap_latency);
	AC_WRITE(ac, WINDOW, pdata->tap_window);
	AC_WRITE(ac, THRESH_ACT, pdata->activity_threshold);
	AC_WRITE(ac, THRESH_INACT, pdata->inactivity_threshold);
	AC_WRITE(ac, TIME_INACT, pdata->inactivity_time);
	AC_WRITE(ac, THRESH_FF, pdata->free_fall_threshold);
	AC_WRITE(ac, TIME_FF, pdata->free_fall_time);
	AC_WRITE(ac, TAP_AXES, pdata->tap_axis_control);
	AC_WRITE(ac, ACT_INACT_CTL, pdata->act_axis_control);
	AC_WRITE(ac, BW_RATE, RATE(ac->pdata.data_rate) |
		 (pdata->low_power_mode ? LOW_POWER : 0));
	AC_WRITE(ac, DATA_FORMAT, pdata->data_range);
	AC_WRITE(ac, FIFO_CTL, FIFO_MODE(pdata->fifo_mode) |
			SAMPLES(pdata->watermark));

	if (pdata->use_int2) {
		/* Map all INTs to INT2 */
		AC_WRITE(ac, INT_MAP, ac->int_mask | OVERRUN);
	} else {
		/* Map all INTs to INT1 */
		AC_WRITE(ac, INT_MAP, 0);
	}

	if (ac->model == 346 && ac->pdata.orientation_enable) {
		AC_WRITE(ac, ORIENT_CONF,
			ORIENT_DEADZONE(ac->pdata.deadzone_angle) |
			ORIENT_DIVISOR(ac->pdata.divisor_length));

		ac->orient2d_saved = 1234;
		ac->orient3d_saved = 1234;

		if (pdata->orientation_enable & ADXL_EN_ORIENTATION_3D)
			for (i = 0; i < ARRAY_SIZE(pdata->ev_codes_orient_3d); i++)
				input_set_capability(input_dev, EV_KEY,
						     pdata->ev_codes_orient_3d[i]);

		if (pdata->orientation_enable & ADXL_EN_ORIENTATION_2D)
			for (i = 0; i < ARRAY_SIZE(pdata->ev_codes_orient_2d); i++)
				input_set_capability(input_dev, EV_KEY,
						     pdata->ev_codes_orient_2d[i]);
	} else {
		ac->pdata.orientation_enable = 0;
	}

	AC_WRITE(ac, INT_ENABLE, ac->int_mask | OVERRUN);

	ac->pdata.power_mode &= (PCTL_AUTO_SLEEP | PCTL_LINK);

	return ac;
}
EXPORT_SYMBOL_GPL(adxl34x_probe);

EXPORT_GPL_SIMPLE_DEV_PM_OPS(adxl34x_pm, adxl34x_suspend, adxl34x_resume);

MODULE_AUTHOR("Michael Hennerich <hennerich@blackfin.uclinux.org>");
MODULE_DESCRIPTION("ADXL345/346 Three-Axis Digital Accelerometer Driver");
MODULE_LICENSE("GPL");
