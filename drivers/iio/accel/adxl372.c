// SPDX-License-Identifier: GPL-2.0+
/*
 * ADXL372 3-Axis Digital Accelerometer core driver
 *
 * Copyright 2018 Analog Devices Inc.
 */

#include <linux/bitops.h>
#include <linux/interrupt.h>
#include <linux/irq.h>
#include <linux/module.h>
#include <linux/regmap.h>
#include <linux/spi/spi.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

#include "adxl372.h"

/* ADXL372 registers definition */
#define ADXL372_DEVID			0x00
#define ADXL372_DEVID_MST		0x01
#define ADXL372_PARTID			0x02
#define ADXL372_STATUS_1		0x04
#define ADXL372_STATUS_2		0x05
#define ADXL372_FIFO_ENTRIES_2		0x06
#define ADXL372_FIFO_ENTRIES_1		0x07
#define ADXL372_X_DATA_H		0x08
#define ADXL372_X_DATA_L		0x09
#define ADXL372_Y_DATA_H		0x0A
#define ADXL372_Y_DATA_L		0x0B
#define ADXL372_Z_DATA_H		0x0C
#define ADXL372_Z_DATA_L		0x0D
#define ADXL372_X_MAXPEAK_H		0x15
#define ADXL372_X_MAXPEAK_L		0x16
#define ADXL372_Y_MAXPEAK_H		0x17
#define ADXL372_Y_MAXPEAK_L		0x18
#define ADXL372_Z_MAXPEAK_H		0x19
#define ADXL372_Z_MAXPEAK_L		0x1A
#define ADXL372_OFFSET_X		0x20
#define ADXL372_OFFSET_Y		0x21
#define ADXL372_OFFSET_Z		0x22
#define ADXL372_X_THRESH_ACT_H		0x23
#define ADXL372_X_THRESH_ACT_L		0x24
#define ADXL372_Y_THRESH_ACT_H		0x25
#define ADXL372_Y_THRESH_ACT_L		0x26
#define ADXL372_Z_THRESH_ACT_H		0x27
#define ADXL372_Z_THRESH_ACT_L		0x28
#define ADXL372_TIME_ACT		0x29
#define ADXL372_X_THRESH_INACT_H	0x2A
#define ADXL372_X_THRESH_INACT_L	0x2B
#define ADXL372_Y_THRESH_INACT_H	0x2C
#define ADXL372_Y_THRESH_INACT_L	0x2D
#define ADXL372_Z_THRESH_INACT_H	0x2E
#define ADXL372_Z_THRESH_INACT_L	0x2F
#define ADXL372_TIME_INACT_H		0x30
#define ADXL372_TIME_INACT_L		0x31
#define ADXL372_X_THRESH_ACT2_H		0x32
#define ADXL372_X_THRESH_ACT2_L		0x33
#define ADXL372_Y_THRESH_ACT2_H		0x34
#define ADXL372_Y_THRESH_ACT2_L		0x35
#define ADXL372_Z_THRESH_ACT2_H		0x36
#define ADXL372_Z_THRESH_ACT2_L		0x37
#define ADXL372_HPF			0x38
#define ADXL372_FIFO_SAMPLES		0x39
#define ADXL372_FIFO_CTL		0x3A
#define ADXL372_INT1_MAP		0x3B
#define ADXL372_INT2_MAP		0x3C
#define ADXL372_TIMING			0x3D
#define ADXL372_MEASURE			0x3E
#define ADXL372_POWER_CTL		0x3F
#define ADXL372_SELF_TEST		0x40
#define ADXL372_RESET			0x41
#define ADXL372_FIFO_DATA		0x42

#define ADXL372_DEVID_VAL		0xAD
#define ADXL372_PARTID_VAL		0xFA
#define ADXL372_RESET_CODE		0x52

/* ADXL372_POWER_CTL */
#define ADXL372_POWER_CTL_MODE_MSK		GENMASK_ULL(1, 0)
#define ADXL372_POWER_CTL_MODE(x)		(((x) & 0x3) << 0)

/* ADXL372_MEASURE */
#define ADXL372_MEASURE_LINKLOOP_MSK		GENMASK_ULL(5, 4)
#define ADXL372_MEASURE_LINKLOOP_MODE(x)	(((x) & 0x3) << 4)
#define ADXL372_MEASURE_BANDWIDTH_MSK		GENMASK_ULL(2, 0)
#define ADXL372_MEASURE_BANDWIDTH_MODE(x)	(((x) & 0x7) << 0)

/* ADXL372_TIMING */
#define ADXL372_TIMING_ODR_MSK			GENMASK_ULL(7, 5)
#define ADXL372_TIMING_ODR_MODE(x)		(((x) & 0x7) << 5)

/* ADXL372_FIFO_CTL */
#define ADXL372_FIFO_CTL_FORMAT_MSK		GENMASK(5, 3)
#define ADXL372_FIFO_CTL_FORMAT_MODE(x)		(((x) & 0x7) << 3)
#define ADXL372_FIFO_CTL_MODE_MSK		GENMASK(2, 1)
#define ADXL372_FIFO_CTL_MODE_MODE(x)		(((x) & 0x3) << 1)
#define ADXL372_FIFO_CTL_SAMPLES_MSK		BIT(1)
#define ADXL372_FIFO_CTL_SAMPLES_MODE(x)	(((x) > 0xFF) ? 1 : 0)

/* ADXL372_STATUS_1 */
#define ADXL372_STATUS_1_DATA_RDY(x)		(((x) >> 0) & 0x1)
#define ADXL372_STATUS_1_FIFO_RDY(x)		(((x) >> 1) & 0x1)
#define ADXL372_STATUS_1_FIFO_FULL(x)		(((x) >> 2) & 0x1)
#define ADXL372_STATUS_1_FIFO_OVR(x)		(((x) >> 3) & 0x1)
#define ADXL372_STATUS_1_USR_NVM_BUSY(x)	(((x) >> 5) & 0x1)
#define ADXL372_STATUS_1_AWAKE(x)		(((x) >> 6) & 0x1)
#define ADXL372_STATUS_1_ERR_USR_REGS(x)	(((x) >> 7) & 0x1)

/* ADXL372_INT1_MAP */
#define ADXL372_INT1_MAP_DATA_RDY_MSK		BIT(0)
#define ADXL372_INT1_MAP_DATA_RDY_MODE(x)	(((x) & 0x1) << 0)
#define ADXL372_INT1_MAP_FIFO_RDY_MSK		BIT(1)
#define ADXL372_INT1_MAP_FIFO_RDY_MODE(x)	(((x) & 0x1) << 1)
#define ADXL372_INT1_MAP_FIFO_FULL_MSK		BIT(2)
#define ADXL372_INT1_MAP_FIFO_FULL_MODE(x)	(((x) & 0x1) << 2)
#define ADXL372_INT1_MAP_FIFO_OVR_MSK		BIT(3)
#define ADXL372_INT1_MAP_FIFO_OVR_MODE(x)	(((x) & 0x1) << 3)
#define ADXL372_INT1_MAP_INACT_MSK		BIT(4)
#define ADXL372_INT1_MAP_INACT_MODE(x)		(((x) & 0x1) << 4)
#define ADXL372_INT1_MAP_ACT_MSK		BIT(5)
#define ADXL372_INT1_MAP_ACT_MODE(x)		(((x) & 0x1) << 5)
#define ADXL372_INT1_MAP_AWAKE_MSK		BIT(6)
#define ADXL372_INT1_MAP_AWAKE_MODE(x)		(((x) & 0x1) << 6)
#define ADXL372_INT1_MAP_LOW_MSK		BIT(7)
#define ADXL372_INT1_MAP_LOW_MODE(x)		(((x) & 0x1) << 7)

/* The ADXL372 includes a deep, 512 sample FIFO buffer */
#define ADXL372_FIFO_SIZE			512

/*
 * At +/- 200g with 12-bit resolution, scale is computed as:
 * (200 + 200) * 9.81 / (2^12 - 1) = 0.958241
 */
#define ADXL372_USCALE	958241

enum adxl372_op_mode {
	ADXL372_STANDBY,
	ADXL372_WAKE_UP,
	ADXL372_INSTANT_ON,
	ADXL372_FULL_BW_MEASUREMENT,
};

enum adxl372_act_proc_mode {
	ADXL372_DEFAULT,
	ADXL372_LINKED,
	ADXL372_LOOPED,
};

enum adxl372_th_activity {
	ADXL372_ACTIVITY,
	ADXL372_ACTIVITY2,
	ADXL372_INACTIVITY,
};

enum adxl372_odr {
	ADXL372_ODR_400HZ,
	ADXL372_ODR_800HZ,
	ADXL372_ODR_1600HZ,
	ADXL372_ODR_3200HZ,
	ADXL372_ODR_6400HZ,
};

enum adxl372_bandwidth {
	ADXL372_BW_200HZ,
	ADXL372_BW_400HZ,
	ADXL372_BW_800HZ,
	ADXL372_BW_1600HZ,
	ADXL372_BW_3200HZ,
};

static const unsigned int adxl372_th_reg_high_addr[3] = {
	[ADXL372_ACTIVITY] = ADXL372_X_THRESH_ACT_H,
	[ADXL372_ACTIVITY2] = ADXL372_X_THRESH_ACT2_H,
	[ADXL372_INACTIVITY] = ADXL372_X_THRESH_INACT_H,
};

enum adxl372_fifo_format {
	ADXL372_XYZ_FIFO,
	ADXL372_X_FIFO,
	ADXL372_Y_FIFO,
	ADXL372_XY_FIFO,
	ADXL372_Z_FIFO,
	ADXL372_XZ_FIFO,
	ADXL372_YZ_FIFO,
	ADXL372_XYZ_PEAK_FIFO,
};

enum adxl372_fifo_mode {
	ADXL372_FIFO_BYPASSED,
	ADXL372_FIFO_STREAMED,
	ADXL372_FIFO_TRIGGERED,
	ADXL372_FIFO_OLD_SAVED
};

static const int adxl372_samp_freq_tbl[5] = {
	400, 800, 1600, 3200, 6400,
};

static const int adxl372_bw_freq_tbl[5] = {
	200, 400, 800, 1600, 3200,
};

struct adxl372_axis_lookup {
	unsigned int bits;
	enum adxl372_fifo_format fifo_format;
};

static const struct adxl372_axis_lookup adxl372_axis_lookup_table[] = {
	{ BIT(0), ADXL372_X_FIFO },
	{ BIT(1), ADXL372_Y_FIFO },
	{ BIT(2), ADXL372_Z_FIFO },
	{ BIT(0) | BIT(1), ADXL372_XY_FIFO },
	{ BIT(0) | BIT(2), ADXL372_XZ_FIFO },
	{ BIT(1) | BIT(2), ADXL372_YZ_FIFO },
	{ BIT(0) | BIT(1) | BIT(2), ADXL372_XYZ_FIFO },
};

#define ADXL372_ACCEL_CHANNEL(index, reg, axis) {			\
	.type = IIO_ACCEL,						\
	.address = reg,							\
	.modified = 1,							\
	.channel2 = IIO_MOD_##axis,					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),			\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |		\
				    BIT(IIO_CHAN_INFO_SAMP_FREQ) |	\
		BIT(IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY),	\
	.scan_index = index,						\
	.scan_type = {							\
		.sign = 's',						\
		.realbits = 12,						\
		.storagebits = 16,					\
		.shift = 4,						\
	},								\
}

static const struct iio_chan_spec adxl372_channels[] = {
	ADXL372_ACCEL_CHANNEL(0, ADXL372_X_DATA_H, X),
	ADXL372_ACCEL_CHANNEL(1, ADXL372_Y_DATA_H, Y),
	ADXL372_ACCEL_CHANNEL(2, ADXL372_Z_DATA_H, Z),
};

struct adxl372_state {
	int				irq;
	struct device			*dev;
	struct regmap			*regmap;
	struct iio_trigger		*dready_trig;
	enum adxl372_fifo_mode		fifo_mode;
	enum adxl372_fifo_format	fifo_format;
	enum adxl372_op_mode		op_mode;
	enum adxl372_act_proc_mode	act_proc_mode;
	enum adxl372_odr		odr;
	enum adxl372_bandwidth		bw;
	u32				act_time_ms;
	u32				inact_time_ms;
	u8				fifo_set_size;
	u8				int1_bitmask;
	u8				int2_bitmask;
	u16				watermark;
	__be16				fifo_buf[ADXL372_FIFO_SIZE];
};

static const unsigned long adxl372_channel_masks[] = {
	BIT(0), BIT(1), BIT(2),
	BIT(0) | BIT(1),
	BIT(0) | BIT(2),
	BIT(1) | BIT(2),
	BIT(0) | BIT(1) | BIT(2),
	0
};

static int adxl372_read_axis(struct adxl372_state *st, u8 addr)
{
	__be16 regval;
	int ret;

	ret = regmap_bulk_read(st->regmap, addr, &regval, sizeof(regval));
	if (ret < 0)
		return ret;

	return be16_to_cpu(regval);
}

static int adxl372_set_op_mode(struct adxl372_state *st,
			       enum adxl372_op_mode op_mode)
{
	int ret;

	ret = regmap_update_bits(st->regmap, ADXL372_POWER_CTL,
				 ADXL372_POWER_CTL_MODE_MSK,
				 ADXL372_POWER_CTL_MODE(op_mode));
	if (ret < 0)
		return ret;

	st->op_mode = op_mode;

	return ret;
}

static int adxl372_set_odr(struct adxl372_state *st,
			   enum adxl372_odr odr)
{
	int ret;

	ret = regmap_update_bits(st->regmap, ADXL372_TIMING,
				 ADXL372_TIMING_ODR_MSK,
				 ADXL372_TIMING_ODR_MODE(odr));
	if (ret < 0)
		return ret;

	st->odr = odr;

	return ret;
}

static int adxl372_find_closest_match(const int *array,
				      unsigned int size, int val)
{
	int i;

	for (i = 0; i < size; i++) {
		if (val <= array[i])
			return i;
	}

	return size - 1;
}

static int adxl372_set_bandwidth(struct adxl372_state *st,
				 enum adxl372_bandwidth bw)
{
	int ret;

	ret = regmap_update_bits(st->regmap, ADXL372_MEASURE,
				 ADXL372_MEASURE_BANDWIDTH_MSK,
				 ADXL372_MEASURE_BANDWIDTH_MODE(bw));
	if (ret < 0)
		return ret;

	st->bw = bw;

	return ret;
}

static int adxl372_set_act_proc_mode(struct adxl372_state *st,
				     enum adxl372_act_proc_mode mode)
{
	int ret;

	ret = regmap_update_bits(st->regmap,
				 ADXL372_MEASURE,
				 ADXL372_MEASURE_LINKLOOP_MSK,
				 ADXL372_MEASURE_LINKLOOP_MODE(mode));
	if (ret < 0)
		return ret;

	st->act_proc_mode = mode;

	return ret;
}

static int adxl372_set_activity_threshold(struct adxl372_state *st,
					  enum adxl372_th_activity act,
					  bool ref_en, bool enable,
					  unsigned int threshold)
{
	unsigned char buf[6];
	unsigned char th_reg_high_val, th_reg_low_val, th_reg_high_addr;

	/* scale factor is 100 mg/code */
	th_reg_high_val = (threshold / 100) >> 3;
	th_reg_low_val = ((threshold / 100) << 5) | (ref_en << 1) | enable;
	th_reg_high_addr = adxl372_th_reg_high_addr[act];

	buf[0] = th_reg_high_val;
	buf[1] = th_reg_low_val;
	buf[2] = th_reg_high_val;
	buf[3] = th_reg_low_val;
	buf[4] = th_reg_high_val;
	buf[5] = th_reg_low_val;

	return regmap_bulk_write(st->regmap, th_reg_high_addr,
				 buf, ARRAY_SIZE(buf));
}

static int adxl372_set_activity_time_ms(struct adxl372_state *st,
					unsigned int act_time_ms)
{
	unsigned int reg_val, scale_factor;
	int ret;

	/*
	 * 3.3 ms per code is the scale factor of the TIME_ACT register for
	 * ODR = 6400 Hz. It is 6.6 ms per code for ODR = 3200 Hz and below.
	 */
	if (st->odr == ADXL372_ODR_6400HZ)
		scale_factor = 3300;
	else
		scale_factor = 6600;

	reg_val = DIV_ROUND_CLOSEST(act_time_ms * 1000, scale_factor);

	/* TIME_ACT register is 8 bits wide */
	if (reg_val > 0xFF)
		reg_val = 0xFF;

	ret = regmap_write(st->regmap, ADXL372_TIME_ACT, reg_val);
	if (ret < 0)
		return ret;

	st->act_time_ms = act_time_ms;

	return ret;
}

static int adxl372_set_inactivity_time_ms(struct adxl372_state *st,
					  unsigned int inact_time_ms)
{
	unsigned int reg_val_h, reg_val_l, res, scale_factor;
	int ret;

	/*
	 * 13 ms per code is the scale factor of the TIME_INACT register for
	 * ODR = 6400 Hz. It is 26 ms per code for ODR = 3200 Hz and below.
	 */
	if (st->odr == ADXL372_ODR_6400HZ)
		scale_factor = 13;
	else
		scale_factor = 26;

	res = DIV_ROUND_CLOSEST(inact_time_ms, scale_factor);
	reg_val_h = (res >> 8) & 0xFF;
	reg_val_l = res & 0xFF;

	ret = regmap_write(st->regmap, ADXL372_TIME_INACT_H, reg_val_h);
	if (ret < 0)
		return ret;

	ret = regmap_write(st->regmap, ADXL372_TIME_INACT_L, reg_val_l);
	if (ret < 0)
		return ret;

	st->inact_time_ms = inact_time_ms;

	return ret;
}

static int adxl372_set_interrupts(struct adxl372_state *st,
				  unsigned char int1_bitmask,
				  unsigned char int2_bitmask)
{
	int ret;

	ret = regmap_write(st->regmap, ADXL372_INT1_MAP, int1_bitmask);
	if (ret < 0)
		return ret;

	return regmap_write(st->regmap, ADXL372_INT2_MAP, int2_bitmask);
}

static int adxl372_configure_fifo(struct adxl372_state *st)
{
	unsigned int fifo_samples, fifo_ctl;
	int ret;

	/* FIFO must be configured while in standby mode */
	ret = adxl372_set_op_mode(st, ADXL372_STANDBY);
	if (ret < 0)
		return ret;

	fifo_samples = st->watermark & 0xFF;
	fifo_ctl = ADXL372_FIFO_CTL_FORMAT_MODE(st->fifo_format) |
		   ADXL372_FIFO_CTL_MODE_MODE(st->fifo_mode) |
		   ADXL372_FIFO_CTL_SAMPLES_MODE(st->watermark);

	ret = regmap_write(st->regmap, ADXL372_FIFO_SAMPLES, fifo_samples);
	if (ret < 0)
		return ret;

	ret = regmap_write(st->regmap, ADXL372_FIFO_CTL, fifo_ctl);
	if (ret < 0)
		return ret;

	return adxl372_set_op_mode(st, ADXL372_FULL_BW_MEASUREMENT);
}

static int adxl372_get_status(struct adxl372_state *st,
			      u8 *status1, u8 *status2,
			      u16 *fifo_entries)
{
	__be32 buf;
	u32 val;
	int ret;

	/* STATUS1, STATUS2, FIFO_ENTRIES2 and FIFO_ENTRIES are adjacent regs */
	ret = regmap_bulk_read(st->regmap, ADXL372_STATUS_1,
			       &buf, sizeof(buf));
	if (ret < 0)
		return ret;

	val = be32_to_cpu(buf);

	*status1 = (val >> 24) & 0x0F;
	*status2 = (val >> 16) & 0x0F;
	/*
	 * FIFO_ENTRIES contains the least significant byte, and FIFO_ENTRIES2
	 * contains the two most significant bits
	 */
	*fifo_entries = val & 0x3FF;

	return ret;
}

static irqreturn_t adxl372_trigger_handler(int irq, void  *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct adxl372_state *st = iio_priv(indio_dev);
	u8 status1, status2;
	u16 fifo_entries;
	int i, ret;

	ret = adxl372_get_status(st, &status1, &status2, &fifo_entries);
	if (ret < 0)
		goto err;

	if (st->fifo_mode != ADXL372_FIFO_BYPASSED &&
	    ADXL372_STATUS_1_FIFO_FULL(status1)) {
		/*
		 * When reading data from multiple axes from the FIFO,
		 * to ensure that data is not overwritten and stored out
		 * of order at least one sample set must be left in the
		 * FIFO after every read.
		 */
		fifo_entries -= st->fifo_set_size;

		/* Read data from the FIFO */
		ret = regmap_noinc_read(st->regmap, ADXL372_FIFO_DATA,
					st->fifo_buf,
					fifo_entries * sizeof(u16));
		if (ret < 0)
			goto err;

		/* Each sample is 2 bytes */
		for (i = 0; i < fifo_entries * sizeof(u16);
		     i += st->fifo_set_size * sizeof(u16))
			iio_push_to_buffers(indio_dev, &st->fifo_buf[i]);
	}
err:
	iio_trigger_notify_done(indio_dev->trig);
	return IRQ_HANDLED;
}

static int adxl372_setup(struct adxl372_state *st)
{
	unsigned int regval;
	int ret;

	ret = regmap_read(st->regmap, ADXL372_DEVID, &regval);
	if (ret < 0)
		return ret;

	if (regval != ADXL372_DEVID_VAL) {
		dev_err(st->dev, "Invalid chip id %x\n", regval);
		return -ENODEV;
	}

	ret = adxl372_set_op_mode(st, ADXL372_STANDBY);
	if (ret < 0)
		return ret;

	/* Set threshold for activity detection to 1g */
	ret = adxl372_set_activity_threshold(st, ADXL372_ACTIVITY,
					     true, true, 1000);
	if (ret < 0)
		return ret;

	/* Set threshold for inactivity detection to 100mg */
	ret = adxl372_set_activity_threshold(st, ADXL372_INACTIVITY,
					     true, true, 100);
	if (ret < 0)
		return ret;

	/* Set activity processing in Looped mode */
	ret = adxl372_set_act_proc_mode(st, ADXL372_LOOPED);
	if (ret < 0)
		return ret;

	ret = adxl372_set_odr(st, ADXL372_ODR_6400HZ);
	if (ret < 0)
		return ret;

	ret = adxl372_set_bandwidth(st, ADXL372_BW_3200HZ);
	if (ret < 0)
		return ret;

	/* Set activity timer to 1ms */
	ret = adxl372_set_activity_time_ms(st, 1);
	if (ret < 0)
		return ret;

	/* Set inactivity timer to 10s */
	ret = adxl372_set_inactivity_time_ms(st, 10000);
	if (ret < 0)
		return ret;

	/* Set the mode of operation to full bandwidth measurement mode */
	return adxl372_set_op_mode(st, ADXL372_FULL_BW_MEASUREMENT);
}

static int adxl372_reg_access(struct iio_dev *indio_dev,
			      unsigned int reg,
			      unsigned int writeval,
			      unsigned int *readval)
{
	struct adxl372_state *st = iio_priv(indio_dev);

	if (readval)
		return regmap_read(st->regmap, reg, readval);
	else
		return regmap_write(st->regmap, reg, writeval);
}

static int adxl372_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2, long info)
{
	struct adxl372_state *st = iio_priv(indio_dev);
	int ret;

	switch (info) {
	case IIO_CHAN_INFO_RAW:
		ret = iio_device_claim_direct_mode(indio_dev);
		if (ret)
			return ret;

		ret = adxl372_read_axis(st, chan->address);
		iio_device_release_direct_mode(indio_dev);
		if (ret < 0)
			return ret;

		*val = sign_extend32(ret >> chan->scan_type.shift,
				     chan->scan_type.realbits - 1);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		*val = 0;
		*val2 = ADXL372_USCALE;
		return IIO_VAL_INT_PLUS_MICRO;
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = adxl372_samp_freq_tbl[st->odr];
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		*val = adxl372_bw_freq_tbl[st->bw];
		return IIO_VAL_INT;
	}

	return -EINVAL;
}

static int adxl372_write_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int val, int val2, long info)
{
	struct adxl372_state *st = iio_priv(indio_dev);
	int odr_index, bw_index, ret;

	switch (info) {
	case IIO_CHAN_INFO_SAMP_FREQ:
		odr_index = adxl372_find_closest_match(adxl372_samp_freq_tbl,
					ARRAY_SIZE(adxl372_samp_freq_tbl),
					val);
		ret = adxl372_set_odr(st, odr_index);
		if (ret < 0)
			return ret;
		/*
		 * The timer period depends on the ODR selected.
		 * At 3200 Hz and below, it is 6.6 ms; at 6400 Hz, it is 3.3 ms
		 */
		ret = adxl372_set_activity_time_ms(st, st->act_time_ms);
		if (ret < 0)
			return ret;
		/*
		 * The timer period depends on the ODR selected.
		 * At 3200 Hz and below, it is 26 ms; at 6400 Hz, it is 13 ms
		 */
		ret = adxl372_set_inactivity_time_ms(st, st->inact_time_ms);
		if (ret < 0)
			return ret;
		/*
		 * The maximum bandwidth is constrained to at most half of
		 * the ODR to ensure that the Nyquist criteria is not violated
		 */
		if (st->bw > odr_index)
			ret = adxl372_set_bandwidth(st, odr_index);

		return ret;
	case IIO_CHAN_INFO_LOW_PASS_FILTER_3DB_FREQUENCY:
		bw_index = adxl372_find_closest_match(adxl372_bw_freq_tbl,
					ARRAY_SIZE(adxl372_bw_freq_tbl),
					val);
		return adxl372_set_bandwidth(st, bw_index);
	default:
		return -EINVAL;
	}
}

static ssize_t adxl372_show_filter_freq_avail(struct device *dev,
					      struct device_attribute *attr,
					      char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct adxl372_state *st = iio_priv(indio_dev);
	int i;
	size_t len = 0;

	for (i = 0; i <= st->odr; i++)
		len += scnprintf(buf + len, PAGE_SIZE - len,
				 "%d ", adxl372_bw_freq_tbl[i]);

	buf[len - 1] = '\n';

	return len;
}

static ssize_t adxl372_get_fifo_enabled(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct adxl372_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", st->fifo_mode);
}

static ssize_t adxl372_get_fifo_watermark(struct device *dev,
					  struct device_attribute *attr,
					  char *buf)
{
	struct iio_dev *indio_dev = dev_to_iio_dev(dev);
	struct adxl372_state *st = iio_priv(indio_dev);

	return sprintf(buf, "%d\n", st->watermark);
}

static IIO_CONST_ATTR(hwfifo_watermark_min, "1");
static IIO_CONST_ATTR(hwfifo_watermark_max,
		      __stringify(ADXL372_FIFO_SIZE));
static IIO_DEVICE_ATTR(hwfifo_watermark, 0444,
		       adxl372_get_fifo_watermark, NULL, 0);
static IIO_DEVICE_ATTR(hwfifo_enabled, 0444,
		       adxl372_get_fifo_enabled, NULL, 0);

static const struct attribute *adxl372_fifo_attributes[] = {
	&iio_const_attr_hwfifo_watermark_min.dev_attr.attr,
	&iio_const_attr_hwfifo_watermark_max.dev_attr.attr,
	&iio_dev_attr_hwfifo_watermark.dev_attr.attr,
	&iio_dev_attr_hwfifo_enabled.dev_attr.attr,
	NULL,
};

static int adxl372_set_watermark(struct iio_dev *indio_dev, unsigned int val)
{
	struct adxl372_state *st  = iio_priv(indio_dev);

	if (val > ADXL372_FIFO_SIZE)
		val = ADXL372_FIFO_SIZE;

	st->watermark = val;

	return 0;
}

static int adxl372_buffer_postenable(struct iio_dev *indio_dev)
{
	struct adxl372_state *st = iio_priv(indio_dev);
	unsigned int mask;
	int i, ret;

	ret = adxl372_set_interrupts(st, ADXL372_INT1_MAP_FIFO_FULL_MSK, 0);
	if (ret < 0)
		return ret;

	mask = *indio_dev->active_scan_mask;

	for (i = 0; i < ARRAY_SIZE(adxl372_axis_lookup_table); i++) {
		if (mask == adxl372_axis_lookup_table[i].bits)
			break;
	}

	if (i == ARRAY_SIZE(adxl372_axis_lookup_table))
		return -EINVAL;

	st->fifo_format = adxl372_axis_lookup_table[i].fifo_format;
	st->fifo_set_size = bitmap_weight(indio_dev->active_scan_mask,
					  indio_dev->masklength);
	/*
	 * The 512 FIFO samples can be allotted in several ways, such as:
	 * 170 sample sets of concurrent 3-axis data
	 * 256 sample sets of concurrent 2-axis data (user selectable)
	 * 512 sample sets of single-axis data
	 */
	if ((st->watermark * st->fifo_set_size) > ADXL372_FIFO_SIZE)
		st->watermark = (ADXL372_FIFO_SIZE  / st->fifo_set_size);

	st->fifo_mode = ADXL372_FIFO_STREAMED;

	ret = adxl372_configure_fifo(st);
	if (ret < 0) {
		st->fifo_mode = ADXL372_FIFO_BYPASSED;
		adxl372_set_interrupts(st, 0, 0);
		return ret;
	}

	return iio_triggered_buffer_postenable(indio_dev);
}

static int adxl372_buffer_predisable(struct iio_dev *indio_dev)
{
	struct adxl372_state *st = iio_priv(indio_dev);
	int ret;

	ret = iio_triggered_buffer_predisable(indio_dev);
	if (ret < 0)
		return ret;

	adxl372_set_interrupts(st, 0, 0);
	st->fifo_mode = ADXL372_FIFO_BYPASSED;
	adxl372_configure_fifo(st);

	return 0;
}

static const struct iio_buffer_setup_ops adxl372_buffer_ops = {
	.postenable = adxl372_buffer_postenable,
	.predisable = adxl372_buffer_predisable,
};

static int adxl372_dready_trig_set_state(struct iio_trigger *trig,
					 bool state)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct adxl372_state *st = iio_priv(indio_dev);
	unsigned long int mask = 0;

	if (state)
		mask = ADXL372_INT1_MAP_FIFO_FULL_MSK;

	return adxl372_set_interrupts(st, mask, 0);
}

static int adxl372_validate_trigger(struct iio_dev *indio_dev,
				    struct iio_trigger *trig)
{
	struct adxl372_state *st = iio_priv(indio_dev);

	if (st->dready_trig != trig)
		return -EINVAL;

	return 0;
}

static const struct iio_trigger_ops adxl372_trigger_ops = {
	.validate_device = &iio_trigger_validate_own_device,
	.set_trigger_state = adxl372_dready_trig_set_state,
};

static IIO_CONST_ATTR_SAMP_FREQ_AVAIL("400 800 1600 3200 6400");
static IIO_DEVICE_ATTR(in_accel_filter_low_pass_3db_frequency_available,
		       0444, adxl372_show_filter_freq_avail, NULL, 0);

static struct attribute *adxl372_attributes[] = {
	&iio_const_attr_sampling_frequency_available.dev_attr.attr,
	&iio_dev_attr_in_accel_filter_low_pass_3db_frequency_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group adxl372_attrs_group = {
	.attrs = adxl372_attributes,
};

static const struct iio_info adxl372_info = {
	.validate_trigger = &adxl372_validate_trigger,
	.attrs = &adxl372_attrs_group,
	.read_raw = adxl372_read_raw,
	.write_raw = adxl372_write_raw,
	.debugfs_reg_access = &adxl372_reg_access,
	.hwfifo_set_watermark = adxl372_set_watermark,
};

bool adxl372_readable_noinc_reg(struct device *dev, unsigned int reg)
{
	return (reg == ADXL372_FIFO_DATA);
}
EXPORT_SYMBOL_GPL(adxl372_readable_noinc_reg);

int adxl372_probe(struct device *dev, struct regmap *regmap,
		  int irq, const char *name)
{
	struct iio_dev *indio_dev;
	struct adxl372_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;

	st = iio_priv(indio_dev);
	dev_set_drvdata(dev, indio_dev);

	st->dev = dev;
	st->regmap = regmap;
	st->irq = irq;

	indio_dev->channels = adxl372_channels;
	indio_dev->num_channels = ARRAY_SIZE(adxl372_channels);
	indio_dev->available_scan_masks = adxl372_channel_masks;
	indio_dev->dev.parent = dev;
	indio_dev->name = name;
	indio_dev->info = &adxl372_info;
	indio_dev->modes = INDIO_DIRECT_MODE | INDIO_BUFFER_SOFTWARE;

	ret = adxl372_setup(st);
	if (ret < 0) {
		dev_err(dev, "ADXL372 setup failed\n");
		return ret;
	}

	ret = devm_iio_triggered_buffer_setup(dev,
					      indio_dev, NULL,
					      adxl372_trigger_handler,
					      &adxl372_buffer_ops);
	if (ret < 0)
		return ret;

	iio_buffer_set_attrs(indio_dev->buffer, adxl372_fifo_attributes);

	if (st->irq) {
		st->dready_trig = devm_iio_trigger_alloc(dev,
							 "%s-dev%d",
							 indio_dev->name,
							 indio_dev->id);
		if (st->dready_trig == NULL)
			return -ENOMEM;

		st->dready_trig->ops = &adxl372_trigger_ops;
		st->dready_trig->dev.parent = dev;
		iio_trigger_set_drvdata(st->dready_trig, indio_dev);
		ret = devm_iio_trigger_register(dev, st->dready_trig);
		if (ret < 0)
			return ret;

		indio_dev->trig = iio_trigger_get(st->dready_trig);

		ret = devm_request_threaded_irq(dev, st->irq,
					iio_trigger_generic_data_rdy_poll,
					NULL,
					IRQF_TRIGGER_RISING | IRQF_ONESHOT,
					indio_dev->name, st->dready_trig);
		if (ret < 0)
			return ret;
	}

	return devm_iio_device_register(dev, indio_dev);
}
EXPORT_SYMBOL_GPL(adxl372_probe);

MODULE_AUTHOR("Stefan Popa <stefan.popa@analog.com>");
MODULE_DESCRIPTION("Analog Devices ADXL372 3-axis accelerometer driver");
MODULE_LICENSE("GPL");
