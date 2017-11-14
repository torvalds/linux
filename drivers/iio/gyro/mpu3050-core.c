/*
 * MPU3050 gyroscope driver
 *
 * Copyright (C) 2016 Linaro Ltd.
 * Author: Linus Walleij <linus.walleij@linaro.org>
 *
 * Based on the input subsystem driver, Copyright (C) 2011 Wistron Co.Ltd
 * Joseph Lai <joseph_lai@wistron.com> and trimmed down by
 * Alan Cox <alan@linux.intel.com> in turn based on bma023.c.
 * Device behaviour based on a misc driver posted by Nathan Royer in 2011.
 *
 * TODO: add support for setting up the low pass 3dB frequency.
 */

#include <linux/bitops.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/iio/buffer.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>
#include <linux/interrupt.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/random.h>
#include <linux/slab.h>

#include "mpu3050.h"

#define MPU3050_CHIP_ID		0x69

/*
 * Register map: anything suffixed *_H is a big-endian high byte and always
 * followed by the corresponding low byte (*_L) even though these are not
 * explicitly included in the register definitions.
 */
#define MPU3050_CHIP_ID_REG	0x00
#define MPU3050_PRODUCT_ID_REG	0x01
#define MPU3050_XG_OFFS_TC	0x05
#define MPU3050_YG_OFFS_TC	0x08
#define MPU3050_ZG_OFFS_TC	0x0B
#define MPU3050_X_OFFS_USR_H	0x0C
#define MPU3050_Y_OFFS_USR_H	0x0E
#define MPU3050_Z_OFFS_USR_H	0x10
#define MPU3050_FIFO_EN		0x12
#define MPU3050_AUX_VDDIO	0x13
#define MPU3050_SLV_ADDR	0x14
#define MPU3050_SMPLRT_DIV	0x15
#define MPU3050_DLPF_FS_SYNC	0x16
#define MPU3050_INT_CFG		0x17
#define MPU3050_AUX_ADDR	0x18
#define MPU3050_INT_STATUS	0x1A
#define MPU3050_TEMP_H		0x1B
#define MPU3050_XOUT_H		0x1D
#define MPU3050_YOUT_H		0x1F
#define MPU3050_ZOUT_H		0x21
#define MPU3050_DMP_CFG1	0x35
#define MPU3050_DMP_CFG2	0x36
#define MPU3050_BANK_SEL	0x37
#define MPU3050_MEM_START_ADDR	0x38
#define MPU3050_MEM_R_W		0x39
#define MPU3050_FIFO_COUNT_H	0x3A
#define MPU3050_FIFO_R		0x3C
#define MPU3050_USR_CTRL	0x3D
#define MPU3050_PWR_MGM		0x3E

/* MPU memory bank read options */
#define MPU3050_MEM_PRFTCH	BIT(5)
#define MPU3050_MEM_USER_BANK	BIT(4)
/* Bits 8-11 select memory bank */
#define MPU3050_MEM_RAM_BANK_0	0
#define MPU3050_MEM_RAM_BANK_1	1
#define MPU3050_MEM_RAM_BANK_2	2
#define MPU3050_MEM_RAM_BANK_3	3
#define MPU3050_MEM_OTP_BANK_0	4

#define MPU3050_AXIS_REGS(axis) (MPU3050_XOUT_H + (axis * 2))

/* Register bits */

/* FIFO Enable */
#define MPU3050_FIFO_EN_FOOTER		BIT(0)
#define MPU3050_FIFO_EN_AUX_ZOUT	BIT(1)
#define MPU3050_FIFO_EN_AUX_YOUT	BIT(2)
#define MPU3050_FIFO_EN_AUX_XOUT	BIT(3)
#define MPU3050_FIFO_EN_GYRO_ZOUT	BIT(4)
#define MPU3050_FIFO_EN_GYRO_YOUT	BIT(5)
#define MPU3050_FIFO_EN_GYRO_XOUT	BIT(6)
#define MPU3050_FIFO_EN_TEMP_OUT	BIT(7)

/*
 * Digital Low Pass filter (DLPF)
 * Full Scale (FS)
 * and Synchronization
 */
#define MPU3050_EXT_SYNC_NONE		0x00
#define MPU3050_EXT_SYNC_TEMP		0x20
#define MPU3050_EXT_SYNC_GYROX		0x40
#define MPU3050_EXT_SYNC_GYROY		0x60
#define MPU3050_EXT_SYNC_GYROZ		0x80
#define MPU3050_EXT_SYNC_ACCELX	0xA0
#define MPU3050_EXT_SYNC_ACCELY	0xC0
#define MPU3050_EXT_SYNC_ACCELZ	0xE0
#define MPU3050_EXT_SYNC_MASK		0xE0
#define MPU3050_EXT_SYNC_SHIFT		5

#define MPU3050_FS_250DPS		0x00
#define MPU3050_FS_500DPS		0x08
#define MPU3050_FS_1000DPS		0x10
#define MPU3050_FS_2000DPS		0x18
#define MPU3050_FS_MASK			0x18
#define MPU3050_FS_SHIFT		3

#define MPU3050_DLPF_CFG_256HZ_NOLPF2	0x00
#define MPU3050_DLPF_CFG_188HZ		0x01
#define MPU3050_DLPF_CFG_98HZ		0x02
#define MPU3050_DLPF_CFG_42HZ		0x03
#define MPU3050_DLPF_CFG_20HZ		0x04
#define MPU3050_DLPF_CFG_10HZ		0x05
#define MPU3050_DLPF_CFG_5HZ		0x06
#define MPU3050_DLPF_CFG_2100HZ_NOLPF	0x07
#define MPU3050_DLPF_CFG_MASK		0x07
#define MPU3050_DLPF_CFG_SHIFT		0

/* Interrupt config */
#define MPU3050_INT_RAW_RDY_EN		BIT(0)
#define MPU3050_INT_DMP_DONE_EN		BIT(1)
#define MPU3050_INT_MPU_RDY_EN		BIT(2)
#define MPU3050_INT_ANYRD_2CLEAR	BIT(4)
#define MPU3050_INT_LATCH_EN		BIT(5)
#define MPU3050_INT_OPEN		BIT(6)
#define MPU3050_INT_ACTL		BIT(7)
/* Interrupt status */
#define MPU3050_INT_STATUS_RAW_RDY	BIT(0)
#define MPU3050_INT_STATUS_DMP_DONE	BIT(1)
#define MPU3050_INT_STATUS_MPU_RDY	BIT(2)
#define MPU3050_INT_STATUS_FIFO_OVFLW	BIT(7)
/* USR_CTRL */
#define MPU3050_USR_CTRL_FIFO_EN	BIT(6)
#define MPU3050_USR_CTRL_AUX_IF_EN	BIT(5)
#define MPU3050_USR_CTRL_AUX_IF_RST	BIT(3)
#define MPU3050_USR_CTRL_FIFO_RST	BIT(1)
#define MPU3050_USR_CTRL_GYRO_RST	BIT(0)
/* PWR_MGM */
#define MPU3050_PWR_MGM_PLL_X		0x01
#define MPU3050_PWR_MGM_PLL_Y		0x02
#define MPU3050_PWR_MGM_PLL_Z		0x03
#define MPU3050_PWR_MGM_CLKSEL_MASK	0x07
#define MPU3050_PWR_MGM_STBY_ZG		BIT(3)
#define MPU3050_PWR_MGM_STBY_YG		BIT(4)
#define MPU3050_PWR_MGM_STBY_XG		BIT(5)
#define MPU3050_PWR_MGM_SLEEP		BIT(6)
#define MPU3050_PWR_MGM_RESET		BIT(7)
#define MPU3050_PWR_MGM_MASK		0xff

/*
 * Fullscale precision is (for finest precision) +/- 250 deg/s, so the full
 * scale is actually 500 deg/s. All 16 bits are then used to cover this scale,
 * in two's complement.
 */
static unsigned int mpu3050_fs_precision[] = {
	IIO_DEGREE_TO_RAD(250),
	IIO_DEGREE_TO_RAD(500),
	IIO_DEGREE_TO_RAD(1000),
	IIO_DEGREE_TO_RAD(2000)
};

/*
 * Regulator names
 */
static const char mpu3050_reg_vdd[] = "vdd";
static const char mpu3050_reg_vlogic[] = "vlogic";

static unsigned int mpu3050_get_freq(struct mpu3050 *mpu3050)
{
	unsigned int freq;

	if (mpu3050->lpf == MPU3050_DLPF_CFG_256HZ_NOLPF2)
		freq = 8000;
	else
		freq = 1000;
	freq /= (mpu3050->divisor + 1);

	return freq;
}

static int mpu3050_start_sampling(struct mpu3050 *mpu3050)
{
	__be16 raw_val[3];
	int ret;
	int i;

	/* Reset */
	ret = regmap_update_bits(mpu3050->map, MPU3050_PWR_MGM,
				 MPU3050_PWR_MGM_RESET, MPU3050_PWR_MGM_RESET);
	if (ret)
		return ret;

	/* Turn on the Z-axis PLL */
	ret = regmap_update_bits(mpu3050->map, MPU3050_PWR_MGM,
				 MPU3050_PWR_MGM_CLKSEL_MASK,
				 MPU3050_PWR_MGM_PLL_Z);
	if (ret)
		return ret;

	/* Write calibration offset registers */
	for (i = 0; i < 3; i++)
		raw_val[i] = cpu_to_be16(mpu3050->calibration[i]);

	ret = regmap_bulk_write(mpu3050->map, MPU3050_X_OFFS_USR_H, raw_val,
				sizeof(raw_val));
	if (ret)
		return ret;

	/* Set low pass filter (sample rate), sync and full scale */
	ret = regmap_write(mpu3050->map, MPU3050_DLPF_FS_SYNC,
			   MPU3050_EXT_SYNC_NONE << MPU3050_EXT_SYNC_SHIFT |
			   mpu3050->fullscale << MPU3050_FS_SHIFT |
			   mpu3050->lpf << MPU3050_DLPF_CFG_SHIFT);
	if (ret)
		return ret;

	/* Set up sampling frequency */
	ret = regmap_write(mpu3050->map, MPU3050_SMPLRT_DIV, mpu3050->divisor);
	if (ret)
		return ret;

	/*
	 * Max 50 ms start-up time after setting DLPF_FS_SYNC
	 * according to the data sheet, then wait for the next sample
	 * at this frequency T = 1000/f ms.
	 */
	msleep(50 + 1000 / mpu3050_get_freq(mpu3050));

	return 0;
}

static int mpu3050_set_8khz_samplerate(struct mpu3050 *mpu3050)
{
	int ret;
	u8 divisor;
	enum mpu3050_lpf lpf;

	lpf = mpu3050->lpf;
	divisor = mpu3050->divisor;

	mpu3050->lpf = LPF_256_HZ_NOLPF; /* 8 kHz base frequency */
	mpu3050->divisor = 0; /* Divide by 1 */
	ret = mpu3050_start_sampling(mpu3050);

	mpu3050->lpf = lpf;
	mpu3050->divisor = divisor;

	return ret;
}

static int mpu3050_read_raw(struct iio_dev *indio_dev,
			    struct iio_chan_spec const *chan,
			    int *val, int *val2,
			    long mask)
{
	struct mpu3050 *mpu3050 = iio_priv(indio_dev);
	int ret;
	__be16 raw_val;

	switch (mask) {
	case IIO_CHAN_INFO_OFFSET:
		switch (chan->type) {
		case IIO_TEMP:
			/* The temperature scaling is (x+23000)/280 Celsius */
			*val = 23000;
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_CALIBBIAS:
		switch (chan->type) {
		case IIO_ANGL_VEL:
			*val = mpu3050->calibration[chan->scan_index-1];
			return IIO_VAL_INT;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_SAMP_FREQ:
		*val = mpu3050_get_freq(mpu3050);
		return IIO_VAL_INT;
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_TEMP:
			/* Millidegrees, see about temperature scaling above */
			*val = 1000;
			*val2 = 280;
			return IIO_VAL_FRACTIONAL;
		case IIO_ANGL_VEL:
			/*
			 * Convert to the corresponding full scale in
			 * radians. All 16 bits are used with sign to
			 * span the available scale: to account for the one
			 * missing value if we multiply by 1/S16_MAX, instead
			 * multiply with 2/U16_MAX.
			 */
			*val = mpu3050_fs_precision[mpu3050->fullscale] * 2;
			*val2 = U16_MAX;
			return IIO_VAL_FRACTIONAL;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_RAW:
		/* Resume device */
		pm_runtime_get_sync(mpu3050->dev);
		mutex_lock(&mpu3050->lock);

		ret = mpu3050_set_8khz_samplerate(mpu3050);
		if (ret)
			goto out_read_raw_unlock;

		switch (chan->type) {
		case IIO_TEMP:
			ret = regmap_bulk_read(mpu3050->map, MPU3050_TEMP_H,
					       &raw_val, sizeof(raw_val));
			if (ret) {
				dev_err(mpu3050->dev,
					"error reading temperature\n");
				goto out_read_raw_unlock;
			}

			*val = be16_to_cpu(raw_val);
			ret = IIO_VAL_INT;

			goto out_read_raw_unlock;
		case IIO_ANGL_VEL:
			ret = regmap_bulk_read(mpu3050->map,
				       MPU3050_AXIS_REGS(chan->scan_index-1),
				       &raw_val,
				       sizeof(raw_val));
			if (ret) {
				dev_err(mpu3050->dev,
					"error reading axis data\n");
				goto out_read_raw_unlock;
			}

			*val = be16_to_cpu(raw_val);
			ret = IIO_VAL_INT;

			goto out_read_raw_unlock;
		default:
			ret = -EINVAL;
			goto out_read_raw_unlock;
		}
	default:
		break;
	}

	return -EINVAL;

out_read_raw_unlock:
	mutex_unlock(&mpu3050->lock);
	pm_runtime_mark_last_busy(mpu3050->dev);
	pm_runtime_put_autosuspend(mpu3050->dev);

	return ret;
}

static int mpu3050_write_raw(struct iio_dev *indio_dev,
			     const struct iio_chan_spec *chan,
			     int val, int val2, long mask)
{
	struct mpu3050 *mpu3050 = iio_priv(indio_dev);
	/*
	 * Couldn't figure out a way to precalculate these at compile time.
	 */
	unsigned int fs250 =
		DIV_ROUND_CLOSEST(mpu3050_fs_precision[0] * 1000000 * 2,
				  U16_MAX);
	unsigned int fs500 =
		DIV_ROUND_CLOSEST(mpu3050_fs_precision[1] * 1000000 * 2,
				  U16_MAX);
	unsigned int fs1000 =
		DIV_ROUND_CLOSEST(mpu3050_fs_precision[2] * 1000000 * 2,
				  U16_MAX);
	unsigned int fs2000 =
		DIV_ROUND_CLOSEST(mpu3050_fs_precision[3] * 1000000 * 2,
				  U16_MAX);

	switch (mask) {
	case IIO_CHAN_INFO_CALIBBIAS:
		if (chan->type != IIO_ANGL_VEL)
			return -EINVAL;
		mpu3050->calibration[chan->scan_index-1] = val;
		return 0;
	case IIO_CHAN_INFO_SAMP_FREQ:
		/*
		 * The max samplerate is 8000 Hz, the minimum
		 * 1000 / 256 ~= 4 Hz
		 */
		if (val < 4 || val > 8000)
			return -EINVAL;

		/*
		 * Above 1000 Hz we must turn off the digital low pass filter
		 * so we get a base frequency of 8kHz to the divider
		 */
		if (val > 1000) {
			mpu3050->lpf = LPF_256_HZ_NOLPF;
			mpu3050->divisor = DIV_ROUND_CLOSEST(8000, val) - 1;
			return 0;
		}

		mpu3050->lpf = LPF_188_HZ;
		mpu3050->divisor = DIV_ROUND_CLOSEST(1000, val) - 1;
		return 0;
	case IIO_CHAN_INFO_SCALE:
		if (chan->type != IIO_ANGL_VEL)
			return -EINVAL;
		/*
		 * We support +/-250, +/-500, +/-1000 and +/2000 deg/s
		 * which means we need to round to the closest radians
		 * which will be roughly +/-4.3, +/-8.7, +/-17.5, +/-35
		 * rad/s. The scale is then for the 16 bits used to cover
		 * it 2/(2^16) of that.
		 */

		/* Just too large, set the max range */
		if (val != 0) {
			mpu3050->fullscale = FS_2000_DPS;
			return 0;
		}

		/*
		 * Now we're dealing with fractions below zero in millirad/s
		 * do some integer interpolation and match with the closest
		 * fullscale in the table.
		 */
		if (val2 <= fs250 ||
		    val2 < ((fs500 + fs250) / 2))
			mpu3050->fullscale = FS_250_DPS;
		else if (val2 <= fs500 ||
			 val2 < ((fs1000 + fs500) / 2))
			mpu3050->fullscale = FS_500_DPS;
		else if (val2 <= fs1000 ||
			 val2 < ((fs2000 + fs1000) / 2))
			mpu3050->fullscale = FS_1000_DPS;
		else
			/* Catch-all */
			mpu3050->fullscale = FS_2000_DPS;
		return 0;
	default:
		break;
	}

	return -EINVAL;
}

static irqreturn_t mpu3050_trigger_handler(int irq, void *p)
{
	const struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct mpu3050 *mpu3050 = iio_priv(indio_dev);
	int ret;
	/*
	 * Temperature 1*16 bits
	 * Three axes 3*16 bits
	 * Timestamp 64 bits (4*16 bits)
	 * Sum total 8*16 bits
	 */
	__be16 hw_values[8];
	s64 timestamp;
	unsigned int datums_from_fifo = 0;

	/*
	 * If we're using the hardware trigger, get the precise timestamp from
	 * the top half of the threaded IRQ handler. Otherwise get the
	 * timestamp here so it will be close in time to the actual values
	 * read from the registers.
	 */
	if (iio_trigger_using_own(indio_dev))
		timestamp = mpu3050->hw_timestamp;
	else
		timestamp = iio_get_time_ns(indio_dev);

	mutex_lock(&mpu3050->lock);

	/* Using the hardware IRQ trigger? Check the buffer then. */
	if (mpu3050->hw_irq_trigger) {
		__be16 raw_fifocnt;
		u16 fifocnt;
		/* X, Y, Z + temperature */
		unsigned int bytes_per_datum = 8;
		bool fifo_overflow = false;

		ret = regmap_bulk_read(mpu3050->map,
				       MPU3050_FIFO_COUNT_H,
				       &raw_fifocnt,
				       sizeof(raw_fifocnt));
		if (ret)
			goto out_trigger_unlock;
		fifocnt = be16_to_cpu(raw_fifocnt);

		if (fifocnt == 512) {
			dev_info(mpu3050->dev,
				 "FIFO overflow! Emptying and resetting FIFO\n");
			fifo_overflow = true;
			/* Reset and enable the FIFO */
			ret = regmap_update_bits(mpu3050->map,
						 MPU3050_USR_CTRL,
						 MPU3050_USR_CTRL_FIFO_EN |
						 MPU3050_USR_CTRL_FIFO_RST,
						 MPU3050_USR_CTRL_FIFO_EN |
						 MPU3050_USR_CTRL_FIFO_RST);
			if (ret) {
				dev_info(mpu3050->dev, "error resetting FIFO\n");
				goto out_trigger_unlock;
			}
			mpu3050->pending_fifo_footer = false;
		}

		if (fifocnt)
			dev_dbg(mpu3050->dev,
				"%d bytes in the FIFO\n",
				fifocnt);

		while (!fifo_overflow && fifocnt > bytes_per_datum) {
			unsigned int toread;
			unsigned int offset;
			__be16 fifo_values[5];

			/*
			 * If there is a FIFO footer in the pipe, first clear
			 * that out. This follows the complex algorithm in the
			 * datasheet that states that you may never leave the
			 * FIFO empty after the first reading: you have to
			 * always leave two footer bytes in it. The footer is
			 * in practice just two zero bytes.
			 */
			if (mpu3050->pending_fifo_footer) {
				toread = bytes_per_datum + 2;
				offset = 0;
			} else {
				toread = bytes_per_datum;
				offset = 1;
				/* Put in some dummy value */
				fifo_values[0] = 0xAAAA;
			}

			ret = regmap_bulk_read(mpu3050->map,
					       MPU3050_FIFO_R,
					       &fifo_values[offset],
					       toread);

			dev_dbg(mpu3050->dev,
				"%04x %04x %04x %04x %04x\n",
				fifo_values[0],
				fifo_values[1],
				fifo_values[2],
				fifo_values[3],
				fifo_values[4]);

			/* Index past the footer (fifo_values[0]) and push */
			iio_push_to_buffers_with_timestamp(indio_dev,
							   &fifo_values[1],
							   timestamp);

			fifocnt -= toread;
			datums_from_fifo++;
			mpu3050->pending_fifo_footer = true;

			/*
			 * If we're emptying the FIFO, just make sure to
			 * check if something new appeared.
			 */
			if (fifocnt < bytes_per_datum) {
				ret = regmap_bulk_read(mpu3050->map,
						       MPU3050_FIFO_COUNT_H,
						       &raw_fifocnt,
						       sizeof(raw_fifocnt));
				if (ret)
					goto out_trigger_unlock;
				fifocnt = be16_to_cpu(raw_fifocnt);
			}

			if (fifocnt < bytes_per_datum)
				dev_dbg(mpu3050->dev,
					"%d bytes left in the FIFO\n",
					fifocnt);

			/*
			 * At this point, the timestamp that triggered the
			 * hardware interrupt is no longer valid for what
			 * we are reading (the interrupt likely fired for
			 * the value on the top of the FIFO), so set the
			 * timestamp to zero and let userspace deal with it.
			 */
			timestamp = 0;
		}
	}

	/*
	 * If we picked some datums from the FIFO that's enough, else
	 * fall through and just read from the current value registers.
	 * This happens in two cases:
	 *
	 * - We are using some other trigger (external, like an HRTimer)
	 *   than the sensor's own sample generator. In this case the
	 *   sensor is just set to the max sampling frequency and we give
	 *   the trigger a copy of the latest value every time we get here.
	 *
	 * - The hardware trigger is active but unused and we actually use
	 *   another trigger which calls here with a frequency higher
	 *   than what the device provides data. We will then just read
	 *   duplicate values directly from the hardware registers.
	 */
	if (datums_from_fifo) {
		dev_dbg(mpu3050->dev,
			"read %d datums from the FIFO\n",
			datums_from_fifo);
		goto out_trigger_unlock;
	}

	ret = regmap_bulk_read(mpu3050->map, MPU3050_TEMP_H, &hw_values,
			       sizeof(hw_values));
	if (ret) {
		dev_err(mpu3050->dev,
			"error reading axis data\n");
		goto out_trigger_unlock;
	}

	iio_push_to_buffers_with_timestamp(indio_dev, hw_values, timestamp);

out_trigger_unlock:
	mutex_unlock(&mpu3050->lock);
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static int mpu3050_buffer_preenable(struct iio_dev *indio_dev)
{
	struct mpu3050 *mpu3050 = iio_priv(indio_dev);

	pm_runtime_get_sync(mpu3050->dev);

	/* Unless we have OUR trigger active, run at full speed */
	if (!mpu3050->hw_irq_trigger)
		return mpu3050_set_8khz_samplerate(mpu3050);

	return 0;
}

static int mpu3050_buffer_postdisable(struct iio_dev *indio_dev)
{
	struct mpu3050 *mpu3050 = iio_priv(indio_dev);

	pm_runtime_mark_last_busy(mpu3050->dev);
	pm_runtime_put_autosuspend(mpu3050->dev);

	return 0;
}

static const struct iio_buffer_setup_ops mpu3050_buffer_setup_ops = {
	.preenable = mpu3050_buffer_preenable,
	.postenable = iio_triggered_buffer_postenable,
	.predisable = iio_triggered_buffer_predisable,
	.postdisable = mpu3050_buffer_postdisable,
};

static const struct iio_mount_matrix *
mpu3050_get_mount_matrix(const struct iio_dev *indio_dev,
			 const struct iio_chan_spec *chan)
{
	struct mpu3050 *mpu3050 = iio_priv(indio_dev);

	return &mpu3050->orientation;
}

static const struct iio_chan_spec_ext_info mpu3050_ext_info[] = {
	IIO_MOUNT_MATRIX(IIO_SHARED_BY_TYPE, mpu3050_get_mount_matrix),
	{ },
};

#define MPU3050_AXIS_CHANNEL(axis, index)				\
	{								\
		.type = IIO_ANGL_VEL,					\
		.modified = 1,						\
		.channel2 = IIO_MOD_##axis,				\
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |		\
			BIT(IIO_CHAN_INFO_CALIBBIAS),			\
		.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE),	\
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),\
		.ext_info = mpu3050_ext_info,				\
		.scan_index = index,					\
		.scan_type = {						\
			.sign = 's',					\
			.realbits = 16,					\
			.storagebits = 16,				\
			.endianness = IIO_BE,				\
		},							\
	}

static const struct iio_chan_spec mpu3050_channels[] = {
	{
		.type = IIO_TEMP,
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_OFFSET),
		.info_mask_shared_by_all = BIT(IIO_CHAN_INFO_SAMP_FREQ),
		.scan_index = 0,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
	},
	MPU3050_AXIS_CHANNEL(X, 1),
	MPU3050_AXIS_CHANNEL(Y, 2),
	MPU3050_AXIS_CHANNEL(Z, 3),
	IIO_CHAN_SOFT_TIMESTAMP(4),
};

/* Four channels apart from timestamp, scan mask = 0x0f */
static const unsigned long mpu3050_scan_masks[] = { 0xf, 0 };

/*
 * These are just the hardcoded factors resulting from the more elaborate
 * calculations done with fractions in the scale raw get/set functions.
 */
static IIO_CONST_ATTR(anglevel_scale_available,
		      "0.000122070 "
		      "0.000274658 "
		      "0.000518798 "
		      "0.001068115");

static struct attribute *mpu3050_attributes[] = {
	&iio_const_attr_anglevel_scale_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group mpu3050_attribute_group = {
	.attrs = mpu3050_attributes,
};

static const struct iio_info mpu3050_info = {
	.driver_module = THIS_MODULE,
	.read_raw = mpu3050_read_raw,
	.write_raw = mpu3050_write_raw,
	.attrs = &mpu3050_attribute_group,
};

/**
 * mpu3050_read_mem() - read MPU-3050 internal memory
 * @mpu3050: device to read from
 * @bank: target bank
 * @addr: target address
 * @len: number of bytes
 * @buf: the buffer to store the read bytes in
 */
static int mpu3050_read_mem(struct mpu3050 *mpu3050,
			    u8 bank,
			    u8 addr,
			    u8 len,
			    u8 *buf)
{
	int ret;

	ret = regmap_write(mpu3050->map,
			   MPU3050_BANK_SEL,
			   bank);
	if (ret)
		return ret;

	ret = regmap_write(mpu3050->map,
			   MPU3050_MEM_START_ADDR,
			   addr);
	if (ret)
		return ret;

	return regmap_bulk_read(mpu3050->map,
				MPU3050_MEM_R_W,
				buf,
				len);
}

static int mpu3050_hw_init(struct mpu3050 *mpu3050)
{
	int ret;
	u8 otp[8];

	/* Reset */
	ret = regmap_update_bits(mpu3050->map,
				 MPU3050_PWR_MGM,
				 MPU3050_PWR_MGM_RESET,
				 MPU3050_PWR_MGM_RESET);
	if (ret)
		return ret;

	/* Turn on the PLL */
	ret = regmap_update_bits(mpu3050->map,
				 MPU3050_PWR_MGM,
				 MPU3050_PWR_MGM_CLKSEL_MASK,
				 MPU3050_PWR_MGM_PLL_Z);
	if (ret)
		return ret;

	/* Disable IRQs */
	ret = regmap_write(mpu3050->map,
			   MPU3050_INT_CFG,
			   0);
	if (ret)
		return ret;

	/* Read out the 8 bytes of OTP (one-time-programmable) memory */
	ret = mpu3050_read_mem(mpu3050,
			       (MPU3050_MEM_PRFTCH |
				MPU3050_MEM_USER_BANK |
				MPU3050_MEM_OTP_BANK_0),
			       0,
			       sizeof(otp),
			       otp);
	if (ret)
		return ret;

	/* This is device-unique data so it goes into the entropy pool */
	add_device_randomness(otp, sizeof(otp));

	dev_info(mpu3050->dev,
		 "die ID: %04X, wafer ID: %02X, A lot ID: %04X, "
		 "W lot ID: %03X, WP ID: %01X, rev ID: %02X\n",
		 /* Die ID, bits 0-12 */
		 (otp[1] << 8 | otp[0]) & 0x1fff,
		 /* Wafer ID, bits 13-17 */
		 ((otp[2] << 8 | otp[1]) & 0x03e0) >> 5,
		 /* A lot ID, bits 18-33 */
		 ((otp[4] << 16 | otp[3] << 8 | otp[2]) & 0x3fffc) >> 2,
		 /* W lot ID, bits 34-45 */
		 ((otp[5] << 8 | otp[4]) & 0x3ffc) >> 2,
		 /* WP ID, bits 47-49 */
		 ((otp[6] << 8 | otp[5]) & 0x0380) >> 7,
		 /* rev ID, bits 50-55 */
		 otp[6] >> 2);

	return 0;
}

static int mpu3050_power_up(struct mpu3050 *mpu3050)
{
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(mpu3050->regs), mpu3050->regs);
	if (ret) {
		dev_err(mpu3050->dev, "cannot enable regulators\n");
		return ret;
	}
	/*
	 * 20-100 ms start-up time for register read/write according to
	 * the datasheet, be on the safe side and wait 200 ms.
	 */
	msleep(200);

	/* Take device out of sleep mode */
	ret = regmap_update_bits(mpu3050->map, MPU3050_PWR_MGM,
				 MPU3050_PWR_MGM_SLEEP, 0);
	if (ret) {
		dev_err(mpu3050->dev, "error setting power mode\n");
		return ret;
	}
	msleep(10);

	return 0;
}

static int mpu3050_power_down(struct mpu3050 *mpu3050)
{
	int ret;

	/*
	 * Put MPU-3050 into sleep mode before cutting regulators.
	 * This is important, because we may not be the sole user
	 * of the regulator so the power may stay on after this, and
	 * then we would be wasting power unless we go to sleep mode
	 * first.
	 */
	ret = regmap_update_bits(mpu3050->map, MPU3050_PWR_MGM,
				 MPU3050_PWR_MGM_SLEEP, MPU3050_PWR_MGM_SLEEP);
	if (ret)
		dev_err(mpu3050->dev, "error putting to sleep\n");

	ret = regulator_bulk_disable(ARRAY_SIZE(mpu3050->regs), mpu3050->regs);
	if (ret)
		dev_err(mpu3050->dev, "error disabling regulators\n");

	return 0;
}

static irqreturn_t mpu3050_irq_handler(int irq, void *p)
{
	struct iio_trigger *trig = p;
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct mpu3050 *mpu3050 = iio_priv(indio_dev);

	if (!mpu3050->hw_irq_trigger)
		return IRQ_NONE;

	/* Get the time stamp as close in time as possible */
	mpu3050->hw_timestamp = iio_get_time_ns(indio_dev);

	return IRQ_WAKE_THREAD;
}

static irqreturn_t mpu3050_irq_thread(int irq, void *p)
{
	struct iio_trigger *trig = p;
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct mpu3050 *mpu3050 = iio_priv(indio_dev);
	unsigned int val;
	int ret;

	/* ACK IRQ and check if it was from us */
	ret = regmap_read(mpu3050->map, MPU3050_INT_STATUS, &val);
	if (ret) {
		dev_err(mpu3050->dev, "error reading IRQ status\n");
		return IRQ_HANDLED;
	}
	if (!(val & MPU3050_INT_STATUS_RAW_RDY))
		return IRQ_NONE;

	iio_trigger_poll_chained(p);

	return IRQ_HANDLED;
}

/**
 * mpu3050_drdy_trigger_set_state() - set data ready interrupt state
 * @trig: trigger instance
 * @enable: true if trigger should be enabled, false to disable
 */
static int mpu3050_drdy_trigger_set_state(struct iio_trigger *trig,
					  bool enable)
{
	struct iio_dev *indio_dev = iio_trigger_get_drvdata(trig);
	struct mpu3050 *mpu3050 = iio_priv(indio_dev);
	unsigned int val;
	int ret;

	/* Disabling trigger: disable interrupt and return */
	if (!enable) {
		/* Disable all interrupts */
		ret = regmap_write(mpu3050->map,
				   MPU3050_INT_CFG,
				   0);
		if (ret)
			dev_err(mpu3050->dev, "error disabling IRQ\n");

		/* Clear IRQ flag */
		ret = regmap_read(mpu3050->map, MPU3050_INT_STATUS, &val);
		if (ret)
			dev_err(mpu3050->dev, "error clearing IRQ status\n");

		/* Disable all things in the FIFO and reset it */
		ret = regmap_write(mpu3050->map, MPU3050_FIFO_EN, 0);
		if (ret)
			dev_err(mpu3050->dev, "error disabling FIFO\n");

		ret = regmap_write(mpu3050->map, MPU3050_USR_CTRL,
				   MPU3050_USR_CTRL_FIFO_RST);
		if (ret)
			dev_err(mpu3050->dev, "error resetting FIFO\n");

		pm_runtime_mark_last_busy(mpu3050->dev);
		pm_runtime_put_autosuspend(mpu3050->dev);
		mpu3050->hw_irq_trigger = false;

		return 0;
	} else {
		/* Else we're enabling the trigger from this point */
		pm_runtime_get_sync(mpu3050->dev);
		mpu3050->hw_irq_trigger = true;

		/* Disable all things in the FIFO */
		ret = regmap_write(mpu3050->map, MPU3050_FIFO_EN, 0);
		if (ret)
			return ret;

		/* Reset and enable the FIFO */
		ret = regmap_update_bits(mpu3050->map, MPU3050_USR_CTRL,
					 MPU3050_USR_CTRL_FIFO_EN |
					 MPU3050_USR_CTRL_FIFO_RST,
					 MPU3050_USR_CTRL_FIFO_EN |
					 MPU3050_USR_CTRL_FIFO_RST);
		if (ret)
			return ret;

		mpu3050->pending_fifo_footer = false;

		/* Turn on the FIFO for temp+X+Y+Z */
		ret = regmap_write(mpu3050->map, MPU3050_FIFO_EN,
				   MPU3050_FIFO_EN_TEMP_OUT |
				   MPU3050_FIFO_EN_GYRO_XOUT |
				   MPU3050_FIFO_EN_GYRO_YOUT |
				   MPU3050_FIFO_EN_GYRO_ZOUT |
				   MPU3050_FIFO_EN_FOOTER);
		if (ret)
			return ret;

		/* Configure the sample engine */
		ret = mpu3050_start_sampling(mpu3050);
		if (ret)
			return ret;

		/* Clear IRQ flag */
		ret = regmap_read(mpu3050->map, MPU3050_INT_STATUS, &val);
		if (ret)
			dev_err(mpu3050->dev, "error clearing IRQ status\n");

		/* Give us interrupts whenever there is new data ready */
		val = MPU3050_INT_RAW_RDY_EN;

		if (mpu3050->irq_actl)
			val |= MPU3050_INT_ACTL;
		if (mpu3050->irq_latch)
			val |= MPU3050_INT_LATCH_EN;
		if (mpu3050->irq_opendrain)
			val |= MPU3050_INT_OPEN;

		ret = regmap_write(mpu3050->map, MPU3050_INT_CFG, val);
		if (ret)
			return ret;
	}

	return 0;
}

static const struct iio_trigger_ops mpu3050_trigger_ops = {
	.owner = THIS_MODULE,
	.set_trigger_state = mpu3050_drdy_trigger_set_state,
};

static int mpu3050_trigger_probe(struct iio_dev *indio_dev, int irq)
{
	struct mpu3050 *mpu3050 = iio_priv(indio_dev);
	unsigned long irq_trig;
	int ret;

	mpu3050->trig = devm_iio_trigger_alloc(&indio_dev->dev,
					       "%s-dev%d",
					       indio_dev->name,
					       indio_dev->id);
	if (!mpu3050->trig)
		return -ENOMEM;

	/* Check if IRQ is open drain */
	if (of_property_read_bool(mpu3050->dev->of_node, "drive-open-drain"))
		mpu3050->irq_opendrain = true;

	irq_trig = irqd_get_trigger_type(irq_get_irq_data(irq));
	/*
	 * Configure the interrupt generator hardware to supply whatever
	 * the interrupt is configured for, edges low/high level low/high,
	 * we can provide it all.
	 */
	switch (irq_trig) {
	case IRQF_TRIGGER_RISING:
		dev_info(&indio_dev->dev,
			 "pulse interrupts on the rising edge\n");
		break;
	case IRQF_TRIGGER_FALLING:
		mpu3050->irq_actl = true;
		dev_info(&indio_dev->dev,
			 "pulse interrupts on the falling edge\n");
		break;
	case IRQF_TRIGGER_HIGH:
		mpu3050->irq_latch = true;
		dev_info(&indio_dev->dev,
			 "interrupts active high level\n");
		/*
		 * With level IRQs, we mask the IRQ until it is processed,
		 * but with edge IRQs (pulses) we can queue several interrupts
		 * in the top half.
		 */
		irq_trig |= IRQF_ONESHOT;
		break;
	case IRQF_TRIGGER_LOW:
		mpu3050->irq_latch = true;
		mpu3050->irq_actl = true;
		irq_trig |= IRQF_ONESHOT;
		dev_info(&indio_dev->dev,
			 "interrupts active low level\n");
		break;
	default:
		/* This is the most preferred mode, if possible */
		dev_err(&indio_dev->dev,
			"unsupported IRQ trigger specified (%lx), enforce "
			"rising edge\n", irq_trig);
		irq_trig = IRQF_TRIGGER_RISING;
		break;
	}

	/* An open drain line can be shared with several devices */
	if (mpu3050->irq_opendrain)
		irq_trig |= IRQF_SHARED;

	ret = request_threaded_irq(irq,
				   mpu3050_irq_handler,
				   mpu3050_irq_thread,
				   irq_trig,
				   mpu3050->trig->name,
				   mpu3050->trig);
	if (ret) {
		dev_err(mpu3050->dev,
			"can't get IRQ %d, error %d\n", irq, ret);
		return ret;
	}

	mpu3050->irq = irq;
	mpu3050->trig->dev.parent = mpu3050->dev;
	mpu3050->trig->ops = &mpu3050_trigger_ops;
	iio_trigger_set_drvdata(mpu3050->trig, indio_dev);

	ret = iio_trigger_register(mpu3050->trig);
	if (ret)
		return ret;

	indio_dev->trig = iio_trigger_get(mpu3050->trig);

	return 0;
}

int mpu3050_common_probe(struct device *dev,
			 struct regmap *map,
			 int irq,
			 const char *name)
{
	struct iio_dev *indio_dev;
	struct mpu3050 *mpu3050;
	unsigned int val;
	int ret;

	indio_dev = devm_iio_device_alloc(dev, sizeof(*mpu3050));
	if (!indio_dev)
		return -ENOMEM;
	mpu3050 = iio_priv(indio_dev);

	mpu3050->dev = dev;
	mpu3050->map = map;
	mutex_init(&mpu3050->lock);
	/* Default fullscale: 2000 degrees per second */
	mpu3050->fullscale = FS_2000_DPS;
	/* 1 kHz, divide by 100, default frequency = 10 Hz */
	mpu3050->lpf = MPU3050_DLPF_CFG_188HZ;
	mpu3050->divisor = 99;

	/* Read the mounting matrix, if present */
	ret = of_iio_read_mount_matrix(dev, "mount-matrix",
				       &mpu3050->orientation);
	if (ret)
		return ret;

	/* Fetch and turn on regulators */
	mpu3050->regs[0].supply = mpu3050_reg_vdd;
	mpu3050->regs[1].supply = mpu3050_reg_vlogic;
	ret = devm_regulator_bulk_get(dev, ARRAY_SIZE(mpu3050->regs),
				      mpu3050->regs);
	if (ret) {
		dev_err(dev, "Cannot get regulators\n");
		return ret;
	}

	ret = mpu3050_power_up(mpu3050);
	if (ret)
		return ret;

	ret = regmap_read(map, MPU3050_CHIP_ID_REG, &val);
	if (ret) {
		dev_err(dev, "could not read device ID\n");
		ret = -ENODEV;

		goto err_power_down;
	}

	if (val != MPU3050_CHIP_ID) {
		dev_err(dev, "unsupported chip id %02x\n", (u8)val);
		ret = -ENODEV;
		goto err_power_down;
	}

	ret = regmap_read(map, MPU3050_PRODUCT_ID_REG, &val);
	if (ret) {
		dev_err(dev, "could not read device ID\n");
		ret = -ENODEV;

		goto err_power_down;
	}
	dev_info(dev, "found MPU-3050 part no: %d, version: %d\n",
		 ((val >> 4) & 0xf), (val & 0xf));

	ret = mpu3050_hw_init(mpu3050);
	if (ret)
		goto err_power_down;

	indio_dev->dev.parent = dev;
	indio_dev->channels = mpu3050_channels;
	indio_dev->num_channels = ARRAY_SIZE(mpu3050_channels);
	indio_dev->info = &mpu3050_info;
	indio_dev->available_scan_masks = mpu3050_scan_masks;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->name = name;

	ret = iio_triggered_buffer_setup(indio_dev, iio_pollfunc_store_time,
					 mpu3050_trigger_handler,
					 &mpu3050_buffer_setup_ops);
	if (ret) {
		dev_err(dev, "triggered buffer setup failed\n");
		goto err_power_down;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(dev, "device register failed\n");
		goto err_cleanup_buffer;
	}

	dev_set_drvdata(dev, indio_dev);

	/* Check if we have an assigned IRQ to use as trigger */
	if (irq) {
		ret = mpu3050_trigger_probe(indio_dev, irq);
		if (ret)
			dev_err(dev, "failed to register trigger\n");
	}

	/* Enable runtime PM */
	pm_runtime_get_noresume(dev);
	pm_runtime_set_active(dev);
	pm_runtime_enable(dev);
	/*
	 * Set autosuspend to two orders of magnitude larger than the
	 * start-up time. 100ms start-up time means 10000ms autosuspend,
	 * i.e. 10 seconds.
	 */
	pm_runtime_set_autosuspend_delay(dev, 10000);
	pm_runtime_use_autosuspend(dev);
	pm_runtime_put(dev);

	return 0;

err_cleanup_buffer:
	iio_triggered_buffer_cleanup(indio_dev);
err_power_down:
	mpu3050_power_down(mpu3050);

	return ret;
}
EXPORT_SYMBOL(mpu3050_common_probe);

int mpu3050_common_remove(struct device *dev)
{
	struct iio_dev *indio_dev = dev_get_drvdata(dev);
	struct mpu3050 *mpu3050 = iio_priv(indio_dev);

	pm_runtime_get_sync(dev);
	pm_runtime_put_noidle(dev);
	pm_runtime_disable(dev);
	iio_triggered_buffer_cleanup(indio_dev);
	if (mpu3050->irq)
		free_irq(mpu3050->irq, mpu3050);
	iio_device_unregister(indio_dev);
	mpu3050_power_down(mpu3050);

	return 0;
}
EXPORT_SYMBOL(mpu3050_common_remove);

#ifdef CONFIG_PM
static int mpu3050_runtime_suspend(struct device *dev)
{
	return mpu3050_power_down(iio_priv(dev_get_drvdata(dev)));
}

static int mpu3050_runtime_resume(struct device *dev)
{
	return mpu3050_power_up(iio_priv(dev_get_drvdata(dev)));
}
#endif /* CONFIG_PM */

const struct dev_pm_ops mpu3050_dev_pm_ops = {
	SET_SYSTEM_SLEEP_PM_OPS(pm_runtime_force_suspend,
				pm_runtime_force_resume)
	SET_RUNTIME_PM_OPS(mpu3050_runtime_suspend,
			   mpu3050_runtime_resume, NULL)
};
EXPORT_SYMBOL(mpu3050_dev_pm_ops);

MODULE_AUTHOR("Linus Walleij");
MODULE_DESCRIPTION("MPU3050 gyroscope driver");
MODULE_LICENSE("GPL");
