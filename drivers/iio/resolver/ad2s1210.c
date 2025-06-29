// SPDX-License-Identifier: GPL-2.0
/*
 * ad2s1210.c support for the ADI Resolver to Digital Converters: AD2S1210
 *
 * Copyright (c) 2010-2010 Analog Devices Inc.
 * Copyright (c) 2023 BayLibre, SAS
 *
 * Device register to IIO ABI mapping:
 *
 * Register                    | Addr | IIO ABI (sysfs)
 * ----------------------------|------|-------------------------------------------
 * DOS Overrange Threshold     | 0x89 | events/in_altvoltage0_thresh_rising_value
 * DOS Mismatch Threshold      | 0x8A | events/in_altvoltage0_mag_rising_value
 * DOS Reset Maximum Threshold | 0x8B | events/in_altvoltage0_mag_rising_reset_max
 * DOS Reset Minimum Threshold | 0x8C | events/in_altvoltage0_mag_rising_reset_min
 * LOT High Threshold          | 0x8D | events/in_angl1_thresh_rising_value
 * LOT Low Threshold [1]       | 0x8E | events/in_angl1_thresh_rising_hysteresis
 * Excitation Frequency        | 0x91 | out_altvoltage0_frequency
 * Control                     | 0x92 | *as bit fields*
 *   Phase lock range          | D5   | events/in_phase0_mag_rising_value
 *   Hysteresis                | D4   | in_angl0_hysteresis
 *   Encoder resolution        | D3:2 | *not implemented*
 *   Resolution                | D1:0 | *device tree: assigned-resolution-bits*
 * Soft Reset                  | 0xF0 | [2]
 * Fault                       | 0xFF | *not implemented*
 *
 * [1]: The value written to the LOT low register is high value minus the
 * hysteresis.
 * [2]: Soft reset is performed when `out_altvoltage0_frequency` is written.
 *
 * Fault to event mapping:
 *
 * Fault                                   |    | Channel     | Type   | Direction
 * ----------------------------------------|----|---------------------------------
 * Sine/cosine inputs clipped [3]          | D7 | altvoltage1 | mag    | either
 * Sine/cosine inputs below LOS            | D6 | altvoltage0 | thresh | falling
 * Sine/cosine inputs exceed DOS overrange | D5 | altvoltage0 | thresh | rising
 * Sine/cosine inputs exceed DOS mismatch  | D4 | altvoltage0 | mag    | rising
 * Tracking error exceeds LOT              | D3 | angl1       | thresh | rising
 * Velocity exceeds maximum tracking rate  | D2 | anglvel0    | mag    | rising
 * Phase error exceeds phase lock range    | D1 | phase0      | mag    | rising
 * Configuration parity error              | D0 | *writes to kernel log*
 *
 * [3]: The chip does not differentiate between fault on sine vs. cosine so
 * there will also be an event on the altvoltage2 channel.
 */

#include <linux/bitfield.h>
#include <linux/bitmap.h>
#include <linux/bits.h>
#include <linux/cleanup.h>
#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/device.h>
#include <linux/gpio/consumer.h>
#include <linux/module.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/slab.h>
#include <linux/spi/spi.h>
#include <linux/sysfs.h>
#include <linux/types.h>

#include <linux/iio/buffer.h>
#include <linux/iio/events.h>
#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/trigger_consumer.h>
#include <linux/iio/triggered_buffer.h>

/* control register flags */
#define AD2S1210_ADDRESS_DATA		BIT(7)
#define AD2S1210_PHASE_LOCK_RANGE_44	BIT(5)
#define AD2S1210_ENABLE_HYSTERESIS	BIT(4)
#define AD2S1210_SET_ENRES		GENMASK(3, 2)
#define AD2S1210_SET_RES		GENMASK(1, 0)

/* fault register flags */
#define AD2S1210_FAULT_CLIP		BIT(7)
#define AD2S1210_FAULT_LOS		BIT(6)
#define AD2S1210_FAULT_DOS_OVR		BIT(5)
#define AD2S1210_FAULT_DOS_MIS		BIT(4)
#define AD2S1210_FAULT_LOT		BIT(3)
#define AD2S1210_FAULT_VELOCITY		BIT(2)
#define AD2S1210_FAULT_PHASE		BIT(1)
#define AD2S1210_FAULT_CONFIG_PARITY	BIT(0)

#define AD2S1210_REG_POSITION_MSB	0x80
#define AD2S1210_REG_POSITION_LSB	0x81
#define AD2S1210_REG_VELOCITY_MSB	0x82
#define AD2S1210_REG_VELOCITY_LSB	0x83
#define AD2S1210_REG_LOS_THRD		0x88
#define AD2S1210_REG_DOS_OVR_THRD	0x89
#define AD2S1210_REG_DOS_MIS_THRD	0x8A
#define AD2S1210_REG_DOS_RST_MAX_THRD	0x8B
#define AD2S1210_REG_DOS_RST_MIN_THRD	0x8C
#define AD2S1210_REG_LOT_HIGH_THRD	0x8D
#define AD2S1210_REG_LOT_LOW_THRD	0x8E
#define AD2S1210_REG_EXCIT_FREQ		0x91
#define AD2S1210_REG_CONTROL		0x92
#define AD2S1210_REG_SOFT_RESET		0xF0
#define AD2S1210_REG_FAULT		0xFF

#define AD2S1210_MIN_CLKIN	6144000
#define AD2S1210_MAX_CLKIN	10240000
#define AD2S1210_MIN_EXCIT	2000
#define AD2S1210_DEF_EXCIT	10000
#define AD2S1210_MAX_EXCIT	20000
#define AD2S1210_MIN_FCW	0x4
#define AD2S1210_MAX_FCW	0x50

/* 44 degrees ~= 0.767945 radians */
#define PHASE_44_DEG_TO_RAD_INT 0
#define PHASE_44_DEG_TO_RAD_MICRO 767945
/* 360 degrees ~= 6.283185 radians */
#define PHASE_360_DEG_TO_RAD_INT 6
#define PHASE_360_DEG_TO_RAD_MICRO 283185

/* Threshold voltage registers have 1 LSB == 38 mV */
#define THRESHOLD_MILLIVOLT_PER_LSB 38
/* max voltage for threshold registers is 0x7F * 38 mV */
#define THRESHOLD_RANGE_STR "[0 38 4826]"

#define FAULT_ONESHOT(bit, new, old) (new & bit && !(old & bit))

enum ad2s1210_mode {
	MOD_POS = 0b00,
	MOD_VEL = 0b01,
	MOD_RESERVED = 0b10,
	MOD_CONFIG = 0b11,
};

enum ad2s1210_resolution {
	AD2S1210_RES_10 = 0b00,
	AD2S1210_RES_12 = 0b01,
	AD2S1210_RES_14 = 0b10,
	AD2S1210_RES_16 = 0b11,
};

struct ad2s1210_state {
	struct mutex lock;
	struct spi_device *sdev;
	/** GPIO pin connected to SAMPLE line. */
	struct gpio_desc *sample_gpio;
	/** GPIO pins connected to A0 and A1 lines (optional). */
	struct gpio_descs *mode_gpios;
	/** Used to access config registers. */
	struct regmap *regmap;
	/** The external oscillator frequency in Hz. */
	unsigned long clkin_hz;
	/** Available raw hysteresis values based on resolution. */
	int hysteresis_available[2];
	/* adi,fixed-mode property - only valid when mode_gpios == NULL. */
	enum ad2s1210_mode fixed_mode;
	/** The selected resolution */
	enum ad2s1210_resolution resolution;
	/** Copy of fault register from the previous read. */
	u8 prev_fault_flags;
	/** For reading raw sample value via SPI. */
	struct {
		__be16 raw;
		u8 fault;
	} sample __aligned(IIO_DMA_MINALIGN);
	/** Scan buffer */
	struct {
		__be16 chan[2];
		/* Ensure timestamp is naturally aligned. */
		aligned_s64 timestamp;
	} scan;
	/** SPI transmit buffer. */
	u8 rx[2];
	/** SPI receive buffer. */
	u8 tx[2];
};

static int ad2s1210_set_mode(struct ad2s1210_state *st, enum ad2s1210_mode mode)
{
	struct gpio_descs *gpios = st->mode_gpios;
	DECLARE_BITMAP(bitmap, 2) = { };

	if (!gpios)
		return mode == st->fixed_mode ? 0 : -EOPNOTSUPP;

	bitmap_write(bitmap, mode, 0, 2);

	return gpiod_multi_set_value_cansleep(gpios, bitmap);
}

/*
 * Writes the given data to the given register address.
 *
 * If the mode is configurable, the device will first be placed in
 * configuration mode.
 */
static int ad2s1210_regmap_reg_write(void *context, unsigned int reg,
				     unsigned int val)
{
	struct ad2s1210_state *st = context;
	struct spi_transfer xfers[] = {
		{
			.len = 1,
			.rx_buf = &st->rx[0],
			.tx_buf = &st->tx[0],
			.cs_change = 1,
		}, {
			.len = 1,
			.rx_buf = &st->rx[1],
			.tx_buf = &st->tx[1],
		},
	};
	int ret;

	/* values can only be 7 bits, the MSB indicates an address */
	if (val & ~0x7F)
		return -EINVAL;

	st->tx[0] = reg;
	st->tx[1] = val;

	ret = ad2s1210_set_mode(st, MOD_CONFIG);
	if (ret < 0)
		return ret;

	ret = spi_sync_transfer(st->sdev, xfers, ARRAY_SIZE(xfers));
	if (ret < 0)
		return ret;

	/* soft reset also clears the fault register */
	if (reg == AD2S1210_REG_SOFT_RESET)
		st->prev_fault_flags = 0;

	return 0;
}

/*
 * Reads value from one of the registers.
 *
 * If the mode is configurable, the device will first be placed in
 * configuration mode.
 */
static int ad2s1210_regmap_reg_read(void *context, unsigned int reg,
				    unsigned int *val)
{
	struct ad2s1210_state *st = context;
	struct spi_transfer xfers[] = {
		{
			.len = 1,
			.rx_buf = &st->rx[0],
			.tx_buf = &st->tx[0],
			.cs_change = 1,
		}, {
			.len = 1,
			.rx_buf = &st->rx[1],
			.tx_buf = &st->tx[1],
		},
	};
	int ret;

	ret = ad2s1210_set_mode(st, MOD_CONFIG);
	if (ret < 0)
		return ret;

	st->tx[0] = reg;
	/*
	 * Must be valid register address here otherwise this could write data.
	 * It doesn't matter which one as long as reading doesn't have side-
	 * effects.
	 */
	st->tx[1] = AD2S1210_REG_CONTROL;

	ret = spi_sync_transfer(st->sdev, xfers, ARRAY_SIZE(xfers));
	if (ret < 0)
		return ret;

	/* reading the fault register also clears it */
	if (reg == AD2S1210_REG_FAULT)
		st->prev_fault_flags = 0;

	/*
	 * If the D7 bit is set on any read/write register, it indicates a
	 * parity error. The fault register is read-only and the D7 bit means
	 * something else there.
	 */
	if ((reg > AD2S1210_REG_VELOCITY_LSB && reg != AD2S1210_REG_FAULT)
	     && st->rx[1] & AD2S1210_ADDRESS_DATA)
		return -EBADMSG;

	*val = st->rx[1];

	return 0;
}

/*
 * Toggles the SAMPLE line on the AD2S1210 to latch in the current position,
 * velocity, and faults.
 *
 * Must be called with lock held.
 */
static void ad2s1210_toggle_sample_line(struct ad2s1210_state *st)
{
	/*
	 * Datasheet specifies minimum hold time t16 = 2 * tck + 20 ns. So the
	 * longest time needed is when CLKIN is 6.144 MHz, in which case t16
	 * ~= 350 ns. The same delay is also needed before re-asserting the
	 * SAMPLE line.
	 */
	gpiod_set_value(st->sample_gpio, 1);
	ndelay(350);
	gpiod_set_value(st->sample_gpio, 0);
	ndelay(350);
}

/*
 * Sets the excitation frequency and performs software reset.
 *
 * Must be called with lock held.
 */
static int ad2s1210_reinit_excitation_frequency(struct ad2s1210_state *st,
						u16 fexcit)
{
	/* Map resolution to settle time in milliseconds. */
	static const int track_time_ms[] = { 10, 20, 25, 60 };
	unsigned int ignored;
	int ret;
	u8 fcw;

	fcw = fexcit * (1 << 15) / st->clkin_hz;
	if (fcw < AD2S1210_MIN_FCW || fcw > AD2S1210_MAX_FCW)
		return -ERANGE;

	ret = regmap_write(st->regmap, AD2S1210_REG_EXCIT_FREQ, fcw);
	if (ret < 0)
		return ret;

	/*
	 * Software reset reinitializes the excitation frequency output.
	 * It does not reset any of the configuration registers.
	 */
	ret = regmap_write(st->regmap, AD2S1210_REG_SOFT_RESET, 0);
	if (ret < 0)
		return ret;

	/*
	 * Soft reset always triggers some faults due the change in the output
	 * signal so clear the faults too. We need to delay for some time
	 * (what datasheet calls t[track]) to allow things to settle before
	 * clearing the faults.
	 */
	msleep(track_time_ms[st->resolution] * 8192000 / st->clkin_hz);

	/* Reading the fault register clears the faults. */
	ret = regmap_read(st->regmap, AD2S1210_REG_FAULT, &ignored);
	if (ret < 0)
		return ret;

	/* Have to toggle sample line to get fault output pins to reset. */
	ad2s1210_toggle_sample_line(st);

	return 0;
}

static void ad2s1210_push_events(struct iio_dev *indio_dev,
				 u8 flags, s64 timestamp)
{
	struct ad2s1210_state *st = iio_priv(indio_dev);

	/* Sine/cosine inputs clipped */
	if (FAULT_ONESHOT(AD2S1210_FAULT_CLIP, flags, st->prev_fault_flags)) {
		/*
		 * The chip does not differentiate between fault on sine vs.
		 * cosine channel so we just send an event on both channels.
		 */
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_ALTVOLTAGE, 1,
						    IIO_EV_TYPE_MAG,
						    IIO_EV_DIR_EITHER),
			       timestamp);
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_ALTVOLTAGE, 2,
						    IIO_EV_TYPE_MAG,
						    IIO_EV_DIR_EITHER),
			       timestamp);
	}

	/* Sine/cosine inputs below LOS threshold */
	if (FAULT_ONESHOT(AD2S1210_FAULT_LOS, flags, st->prev_fault_flags))
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_ALTVOLTAGE, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_FALLING),
			       timestamp);

	/* Sine/cosine inputs exceed DOS overrange threshold */
	if (FAULT_ONESHOT(AD2S1210_FAULT_DOS_OVR, flags, st->prev_fault_flags))
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_ALTVOLTAGE, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			       timestamp);

	/* Sine/cosine inputs exceed DOS mismatch threshold */
	if (FAULT_ONESHOT(AD2S1210_FAULT_DOS_MIS, flags, st->prev_fault_flags))
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_ALTVOLTAGE, 0,
						    IIO_EV_TYPE_MAG,
						    IIO_EV_DIR_RISING),
			       timestamp);

	/* Tracking error exceeds LOT threshold */
	if (FAULT_ONESHOT(AD2S1210_FAULT_LOT, flags, st->prev_fault_flags))
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_ANGL, 1,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			       timestamp);

	/* Velocity exceeds maximum tracking rate */
	if (FAULT_ONESHOT(AD2S1210_FAULT_VELOCITY, flags, st->prev_fault_flags))
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_ANGL_VEL, 0,
						    IIO_EV_TYPE_THRESH,
						    IIO_EV_DIR_RISING),
			       timestamp);

	/* Phase error exceeds phase lock range */
	if (FAULT_ONESHOT(AD2S1210_FAULT_PHASE, flags, st->prev_fault_flags))
		iio_push_event(indio_dev,
			       IIO_UNMOD_EVENT_CODE(IIO_PHASE, 0,
						    IIO_EV_TYPE_MAG,
						    IIO_EV_DIR_RISING),
			       timestamp);

	/* Configuration parity error */
	if (FAULT_ONESHOT(AD2S1210_FAULT_CONFIG_PARITY, flags,
			  st->prev_fault_flags))
		/*
		 * Userspace should also get notified of this via error return
		 * when trying to write to any attribute that writes a register.
		 */
		dev_err_ratelimited(&indio_dev->dev,
				    "Configuration parity error\n");

	st->prev_fault_flags = flags;
}

static int ad2s1210_single_conversion(struct iio_dev *indio_dev,
				      struct iio_chan_spec const *chan,
				      int *val)
{
	struct ad2s1210_state *st = iio_priv(indio_dev);
	s64 timestamp;
	int ret;

	guard(mutex)(&st->lock);

	ad2s1210_toggle_sample_line(st);
	timestamp = iio_get_time_ns(indio_dev);

	if (st->fixed_mode == MOD_CONFIG) {
		unsigned int reg_val;

		switch (chan->type) {
		case IIO_ANGL:
			ret = regmap_bulk_read(st->regmap,
					       AD2S1210_REG_POSITION_MSB,
					       &st->sample.raw, 2);
			if (ret < 0)
				return ret;

			break;
		case IIO_ANGL_VEL:
			ret = regmap_bulk_read(st->regmap,
					       AD2S1210_REG_VELOCITY_MSB,
					       &st->sample.raw, 2);
			if (ret < 0)
				return ret;

			break;
		default:
			return -EINVAL;
		}

		ret = regmap_read(st->regmap, AD2S1210_REG_FAULT, &reg_val);
		if (ret < 0)
			return ret;

		st->sample.fault = reg_val;
	} else {
		switch (chan->type) {
		case IIO_ANGL:
			ret = ad2s1210_set_mode(st, MOD_POS);
			break;
		case IIO_ANGL_VEL:
			ret = ad2s1210_set_mode(st, MOD_VEL);
			break;
		default:
			return -EINVAL;
		}
		if (ret < 0)
			return ret;

		ret = spi_read(st->sdev, &st->sample, 3);
		if (ret < 0)
			return ret;
	}

	switch (chan->type) {
	case IIO_ANGL:
		*val = be16_to_cpu(st->sample.raw);
		ret = IIO_VAL_INT;
		break;
	case IIO_ANGL_VEL:
		*val = (s16)be16_to_cpu(st->sample.raw);
		ret = IIO_VAL_INT;
		break;
	default:
		return -EINVAL;
	}

	ad2s1210_push_events(indio_dev, st->sample.fault, timestamp);

	return ret;
}

static int ad2s1210_get_hysteresis(struct ad2s1210_state *st, int *val)
{
	int ret;

	guard(mutex)(&st->lock);
	ret = regmap_test_bits(st->regmap, AD2S1210_REG_CONTROL,
			       AD2S1210_ENABLE_HYSTERESIS);
	if (ret < 0)
		return ret;

	*val = ret << (2 * (AD2S1210_RES_16 - st->resolution));
	return IIO_VAL_INT;
}

static int ad2s1210_set_hysteresis(struct ad2s1210_state *st, int val)
{
	guard(mutex)(&st->lock);
	return regmap_update_bits(st->regmap, AD2S1210_REG_CONTROL,
				  AD2S1210_ENABLE_HYSTERESIS,
				  val ? AD2S1210_ENABLE_HYSTERESIS : 0);
}

static int ad2s1210_get_phase_lock_range(struct ad2s1210_state *st,
					 int *val, int *val2)
{
	int ret;

	guard(mutex)(&st->lock);
	ret = regmap_test_bits(st->regmap, AD2S1210_REG_CONTROL,
			       AD2S1210_PHASE_LOCK_RANGE_44);
	if (ret < 0)
		return ret;

	if (ret) {
		/* 44 degrees as radians */
		*val = PHASE_44_DEG_TO_RAD_INT;
		*val2 = PHASE_44_DEG_TO_RAD_MICRO;
	} else {
		/* 360 degrees as radians */
		*val = PHASE_360_DEG_TO_RAD_INT;
		*val2 = PHASE_360_DEG_TO_RAD_MICRO;
	}

	return IIO_VAL_INT_PLUS_MICRO;
}

static int ad2s1210_set_phase_lock_range(struct ad2s1210_state *st,
					 int val, int val2)
{
	int deg;

	/* convert radians to degrees - only two allowable values */
	if (val == PHASE_44_DEG_TO_RAD_INT && val2 == PHASE_44_DEG_TO_RAD_MICRO)
		deg = 44;
	else if (val == PHASE_360_DEG_TO_RAD_INT &&
		 val2 == PHASE_360_DEG_TO_RAD_MICRO)
		deg = 360;
	else
		return -EINVAL;

	guard(mutex)(&st->lock);
	return regmap_update_bits(st->regmap, AD2S1210_REG_CONTROL,
				  AD2S1210_PHASE_LOCK_RANGE_44,
				  deg == 44 ? AD2S1210_PHASE_LOCK_RANGE_44 : 0);
}

/* map resolution to microradians/LSB for LOT registers */
static const int ad2s1210_lot_threshold_urad_per_lsb[] = {
	6184, /* 10-bit: ~0.35 deg/LSB, 45 deg max */
	2473, /* 12-bit: ~0.14 deg/LSB, 18 deg max */
	1237, /* 14-bit: ~0.07 deg/LSB, 9 deg max */
	1237, /* 16-bit: same as 14-bit */
};

static int ad2s1210_get_voltage_threshold(struct ad2s1210_state *st,
					  unsigned int reg, int *val)
{
	unsigned int reg_val;
	int ret;

	guard(mutex)(&st->lock);
	ret = regmap_read(st->regmap, reg, &reg_val);
	if (ret < 0)
		return ret;

	*val = reg_val * THRESHOLD_MILLIVOLT_PER_LSB;
	return IIO_VAL_INT;
}

static int ad2s1210_set_voltage_threshold(struct ad2s1210_state *st,
					  unsigned int reg, int val)
{
	unsigned int reg_val;

	reg_val = val / THRESHOLD_MILLIVOLT_PER_LSB;

	guard(mutex)(&st->lock);
	return regmap_write(st->regmap, reg, reg_val);
}

static int ad2s1210_get_lot_high_threshold(struct ad2s1210_state *st,
					   int *val, int *val2)
{
	unsigned int reg_val;
	int ret;

	guard(mutex)(&st->lock);
	ret = regmap_read(st->regmap, AD2S1210_REG_LOT_HIGH_THRD, &reg_val);
	if (ret < 0)
		return ret;

	*val = 0;
	*val2 = reg_val * ad2s1210_lot_threshold_urad_per_lsb[st->resolution];
	return IIO_VAL_INT_PLUS_MICRO;
}

static int ad2s1210_set_lot_high_threshold(struct ad2s1210_state *st,
					   int val, int val2)
{
	unsigned int high_reg_val, low_reg_val, hysteresis;
	int ret;

	/* all valid values are between 0 and pi/4 radians */
	if (val != 0)
		return -EINVAL;

	guard(mutex)(&st->lock);
	/*
	 * We need to read both high and low registers first so we can preserve
	 * the hysteresis.
	 */
	ret = regmap_read(st->regmap, AD2S1210_REG_LOT_HIGH_THRD, &high_reg_val);
	if (ret < 0)
		return ret;

	ret = regmap_read(st->regmap, AD2S1210_REG_LOT_LOW_THRD, &low_reg_val);
	if (ret < 0)
		return ret;

	hysteresis = high_reg_val - low_reg_val;
	high_reg_val = val2 / ad2s1210_lot_threshold_urad_per_lsb[st->resolution];
	low_reg_val = high_reg_val - hysteresis;

	ret = regmap_write(st->regmap, AD2S1210_REG_LOT_HIGH_THRD, high_reg_val);
	if (ret < 0)
		return ret;

	return regmap_write(st->regmap, AD2S1210_REG_LOT_LOW_THRD, low_reg_val);
}

static int ad2s1210_get_lot_low_threshold(struct ad2s1210_state *st,
					  int *val, int *val2)
{
	unsigned int high_reg_val, low_reg_val;
	int ret;

	guard(mutex)(&st->lock);

	ret = regmap_read(st->regmap, AD2S1210_REG_LOT_HIGH_THRD, &high_reg_val);
	if (ret < 0)
		return ret;

	ret = regmap_read(st->regmap, AD2S1210_REG_LOT_LOW_THRD, &low_reg_val);
	if (ret < 0)
		return ret;

	/* sysfs value is hysteresis rather than actual low value */
	*val = 0;
	*val2 = (high_reg_val - low_reg_val) *
		ad2s1210_lot_threshold_urad_per_lsb[st->resolution];
	return IIO_VAL_INT_PLUS_MICRO;
}

static int ad2s1210_set_lot_low_threshold(struct ad2s1210_state *st,
					  int val, int val2)
{
	unsigned int reg_val, hysteresis;
	int ret;

	/* all valid values are between 0 and pi/4 radians */
	if (val != 0)
		return -EINVAL;

	hysteresis = val2 / ad2s1210_lot_threshold_urad_per_lsb[st->resolution];

	guard(mutex)(&st->lock);

	ret = regmap_read(st->regmap, AD2S1210_REG_LOT_HIGH_THRD, &reg_val);
	if (ret < 0)
		return ret;

	return regmap_write(st->regmap, AD2S1210_REG_LOT_LOW_THRD,
			   reg_val - hysteresis);
}

static int ad2s1210_get_excitation_frequency(struct ad2s1210_state *st, int *val)
{
	unsigned int reg_val;
	int ret;

	guard(mutex)(&st->lock);

	ret = regmap_read(st->regmap, AD2S1210_REG_EXCIT_FREQ, &reg_val);
	if (ret < 0)
		return ret;

	*val = reg_val * st->clkin_hz / (1 << 15);
	return IIO_VAL_INT;
}

static int ad2s1210_set_excitation_frequency(struct ad2s1210_state *st, int val)
{
	if (val < AD2S1210_MIN_EXCIT || val > AD2S1210_MAX_EXCIT)
		return -EINVAL;

	guard(mutex)(&st->lock);
	return ad2s1210_reinit_excitation_frequency(st, val);
}

static const int ad2s1210_velocity_scale[] = {
	17089132, /* 8.192MHz / (2*pi * 2500 / 2^15) */
	42722830, /* 8.192MHz / (2*pi * 1000 / 2^15) */
	85445659, /* 8.192MHz / (2*pi * 500 / 2^15) */
	341782638, /* 8.192MHz / (2*pi * 125 / 2^15) */
};

static int ad2s1210_read_raw(struct iio_dev *indio_dev,
			     struct iio_chan_spec const *chan,
			     int *val,
			     int *val2,
			     long mask)
{
	struct ad2s1210_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_RAW:
		return ad2s1210_single_conversion(indio_dev, chan, val);
	case IIO_CHAN_INFO_SCALE:
		switch (chan->type) {
		case IIO_ANGL:
			/* approx 0.3 arc min converted to radians */
			*val = 0;
			*val2 = 95874;
			return IIO_VAL_INT_PLUS_NANO;
		case IIO_ANGL_VEL:
			*val = st->clkin_hz;
			*val2 = ad2s1210_velocity_scale[st->resolution];
			return IIO_VAL_FRACTIONAL;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_FREQUENCY:
		switch (chan->type) {
		case IIO_ALTVOLTAGE:
			return ad2s1210_get_excitation_frequency(st, val);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_HYSTERESIS:
		switch (chan->type) {
		case IIO_ANGL:
			return ad2s1210_get_hysteresis(st, val);
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int ad2s1210_read_avail(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       const int **vals, int *type,
			       int *length, long mask)
{
	static const int excitation_frequency_available[] = {
		AD2S1210_MIN_EXCIT,
		250, /* step */
		AD2S1210_MAX_EXCIT,
	};

	struct ad2s1210_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_FREQUENCY:
		switch (chan->type) {
		case IIO_ALTVOLTAGE:
			*type = IIO_VAL_INT;
			*vals = excitation_frequency_available;
			return IIO_AVAIL_RANGE;
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_HYSTERESIS:
		switch (chan->type) {
		case IIO_ANGL:
			*vals = st->hysteresis_available;
			*type = IIO_VAL_INT;
			*length = ARRAY_SIZE(st->hysteresis_available);
			return IIO_AVAIL_LIST;
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static int ad2s1210_write_raw(struct iio_dev *indio_dev,
			      struct iio_chan_spec const *chan,
			      int val, int val2, long mask)
{
	struct ad2s1210_state *st = iio_priv(indio_dev);

	switch (mask) {
	case IIO_CHAN_INFO_FREQUENCY:
		switch (chan->type) {
		case IIO_ALTVOLTAGE:
			return ad2s1210_set_excitation_frequency(st, val);
		default:
			return -EINVAL;
		}
	case IIO_CHAN_INFO_HYSTERESIS:
		switch (chan->type) {
		case IIO_ANGL:
			return ad2s1210_set_hysteresis(st, val);
		default:
			return -EINVAL;
		}
	default:
		return -EINVAL;
	}
}

static const struct iio_event_spec ad2s1210_position_event_spec[] = {
	{
		/* Tracking error exceeds LOT threshold fault. */
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate =
			/* Loss of tracking high threshold. */
			BIT(IIO_EV_INFO_VALUE) |
			/* Loss of tracking low threshold. */
			BIT(IIO_EV_INFO_HYSTERESIS),
	},
};

static const struct iio_event_spec ad2s1210_velocity_event_spec[] = {
	{
		/* Velocity exceeds maximum tracking rate fault. */
		.type = IIO_EV_TYPE_MAG,
		.dir = IIO_EV_DIR_RISING,
	},
};

static const struct iio_event_spec ad2s1210_phase_event_spec[] = {
	{
		/* Phase error fault. */
		.type = IIO_EV_TYPE_MAG,
		.dir = IIO_EV_DIR_RISING,
		/* Phase lock range. */
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
};

static const struct iio_event_spec ad2s1210_monitor_signal_event_spec[] = {
	{
		/* Sine/cosine below LOS threshold fault. */
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_FALLING,
		/* Loss of signal threshold. */
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		/* Sine/cosine DOS overrange fault.*/
		.type = IIO_EV_TYPE_THRESH,
		.dir = IIO_EV_DIR_RISING,
		/* Degredation of signal overrange threshold. */
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
	{
		/* Sine/cosine DOS mismatch fault.*/
		.type = IIO_EV_TYPE_MAG,
		.dir = IIO_EV_DIR_RISING,
		.mask_separate = BIT(IIO_EV_INFO_VALUE),
	},
};

static const struct iio_event_spec ad2s1210_sin_cos_event_spec[] = {
	{
		/* Sine/cosine clipping fault. */
		.type = IIO_EV_TYPE_MAG,
		.dir = IIO_EV_DIR_EITHER,
	},
};

static const struct iio_chan_spec ad2s1210_channels[] = {
	{
		.type = IIO_ANGL,
		.indexed = 1,
		.channel = 0,
		.scan_index = 0,
		.scan_type = {
			.sign = 'u',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE) |
				      BIT(IIO_CHAN_INFO_HYSTERESIS),
		.info_mask_separate_available =
					BIT(IIO_CHAN_INFO_HYSTERESIS),
	}, {
		.type = IIO_ANGL_VEL,
		.indexed = 1,
		.channel = 0,
		.scan_index = 1,
		.scan_type = {
			.sign = 's',
			.realbits = 16,
			.storagebits = 16,
			.endianness = IIO_BE,
		},
		.info_mask_separate = BIT(IIO_CHAN_INFO_RAW) |
				      BIT(IIO_CHAN_INFO_SCALE),
		.event_spec = ad2s1210_velocity_event_spec,
		.num_event_specs = ARRAY_SIZE(ad2s1210_velocity_event_spec),
	},
	IIO_CHAN_SOFT_TIMESTAMP(2),
	{
		/* used to configure LOT thresholds and get tracking error */
		.type = IIO_ANGL,
		.indexed = 1,
		.channel = 1,
		.scan_index = -1,
		.event_spec = ad2s1210_position_event_spec,
		.num_event_specs = ARRAY_SIZE(ad2s1210_position_event_spec),
	},
	{
		/* used to configure phase lock range and get phase lock error */
		.type = IIO_PHASE,
		.indexed = 1,
		.channel = 0,
		.scan_index = -1,
		.event_spec = ad2s1210_phase_event_spec,
		.num_event_specs = ARRAY_SIZE(ad2s1210_phase_event_spec),
	}, {
		/* excitation frequency output */
		.type = IIO_ALTVOLTAGE,
		.indexed = 1,
		.channel = 0,
		.output = 1,
		.scan_index = -1,
		.info_mask_separate = BIT(IIO_CHAN_INFO_FREQUENCY),
		.info_mask_separate_available = BIT(IIO_CHAN_INFO_FREQUENCY),
	}, {
		/* monitor signal */
		.type = IIO_ALTVOLTAGE,
		.indexed = 1,
		.channel = 0,
		.scan_index = -1,
		.event_spec = ad2s1210_monitor_signal_event_spec,
		.num_event_specs = ARRAY_SIZE(ad2s1210_monitor_signal_event_spec),
	}, {
		/* sine input */
		.type = IIO_ALTVOLTAGE,
		.indexed = 1,
		.channel = 1,
		.scan_index = -1,
		.event_spec = ad2s1210_sin_cos_event_spec,
		.num_event_specs = ARRAY_SIZE(ad2s1210_sin_cos_event_spec),
	}, {
		/* cosine input */
		.type = IIO_ALTVOLTAGE,
		.indexed = 1,
		.channel = 2,
		.scan_index = -1,
		.event_spec = ad2s1210_sin_cos_event_spec,
		.num_event_specs = ARRAY_SIZE(ad2s1210_sin_cos_event_spec),
	},
};

static ssize_t event_attr_voltage_reg_show(struct device *dev,
					   struct device_attribute *attr,
					   char *buf)
{
	struct ad2s1210_state *st = iio_priv(dev_to_iio_dev(dev));
	struct iio_dev_attr *iattr = to_iio_dev_attr(attr);
	unsigned int value;
	int ret;

	guard(mutex)(&st->lock);
	ret = regmap_read(st->regmap, iattr->address, &value);
	if (ret < 0)
		return ret;

	return sprintf(buf, "%d\n", value * THRESHOLD_MILLIVOLT_PER_LSB);
}

static ssize_t event_attr_voltage_reg_store(struct device *dev,
					    struct device_attribute *attr,
					    const char *buf, size_t len)
{
	struct ad2s1210_state *st = iio_priv(dev_to_iio_dev(dev));
	struct iio_dev_attr *iattr = to_iio_dev_attr(attr);
	u16 data;
	int ret;

	ret = kstrtou16(buf, 10, &data);
	if (ret)
		return -EINVAL;

	guard(mutex)(&st->lock);
	ret = regmap_write(st->regmap, iattr->address,
			   data / THRESHOLD_MILLIVOLT_PER_LSB);
	if (ret < 0)
		return ret;

	return len;
}

static ssize_t
in_angl1_thresh_rising_value_available_show(struct device *dev,
					    struct device_attribute *attr,
					    char *buf)
{
	struct ad2s1210_state *st = iio_priv(dev_to_iio_dev(dev));
	int step = ad2s1210_lot_threshold_urad_per_lsb[st->resolution];

	return sysfs_emit(buf, "[0 0.%06d 0.%06d]\n", step, step * 0x7F);
}

static ssize_t
in_angl1_thresh_rising_hysteresis_available_show(struct device *dev,
						 struct device_attribute *attr,
						 char *buf)
{
	struct ad2s1210_state *st = iio_priv(dev_to_iio_dev(dev));
	int step = ad2s1210_lot_threshold_urad_per_lsb[st->resolution];

	return sysfs_emit(buf, "[0 0.%06d 0.%06d]\n", step, step * 0x7F);
}

static IIO_CONST_ATTR(in_phase0_mag_rising_value_available,
		      __stringify(PHASE_44_DEG_TO_RAD_INT) "."
		      __stringify(PHASE_44_DEG_TO_RAD_MICRO) " "
		      __stringify(PHASE_360_DEG_TO_RAD_INT) "."
		      __stringify(PHASE_360_DEG_TO_RAD_MICRO));
static IIO_CONST_ATTR(in_altvoltage0_thresh_falling_value_available,
		      THRESHOLD_RANGE_STR);
static IIO_CONST_ATTR(in_altvoltage0_thresh_rising_value_available,
		      THRESHOLD_RANGE_STR);
static IIO_CONST_ATTR(in_altvoltage0_mag_rising_value_available,
		      THRESHOLD_RANGE_STR);
static IIO_DEVICE_ATTR(in_altvoltage0_mag_rising_reset_max, 0644,
		       event_attr_voltage_reg_show, event_attr_voltage_reg_store,
		       AD2S1210_REG_DOS_RST_MAX_THRD);
static IIO_CONST_ATTR(in_altvoltage0_mag_rising_reset_max_available, THRESHOLD_RANGE_STR);
static IIO_DEVICE_ATTR(in_altvoltage0_mag_rising_reset_min, 0644,
		       event_attr_voltage_reg_show, event_attr_voltage_reg_store,
		       AD2S1210_REG_DOS_RST_MIN_THRD);
static IIO_CONST_ATTR(in_altvoltage0_mag_rising_reset_min_available, THRESHOLD_RANGE_STR);
static IIO_DEVICE_ATTR_RO(in_angl1_thresh_rising_value_available, 0);
static IIO_DEVICE_ATTR_RO(in_angl1_thresh_rising_hysteresis_available, 0);

static struct attribute *ad2s1210_event_attributes[] = {
	&iio_const_attr_in_phase0_mag_rising_value_available.dev_attr.attr,
	&iio_const_attr_in_altvoltage0_thresh_falling_value_available.dev_attr.attr,
	&iio_const_attr_in_altvoltage0_thresh_rising_value_available.dev_attr.attr,
	&iio_const_attr_in_altvoltage0_mag_rising_value_available.dev_attr.attr,
	&iio_dev_attr_in_altvoltage0_mag_rising_reset_max.dev_attr.attr,
	&iio_const_attr_in_altvoltage0_mag_rising_reset_max_available.dev_attr.attr,
	&iio_dev_attr_in_altvoltage0_mag_rising_reset_min.dev_attr.attr,
	&iio_const_attr_in_altvoltage0_mag_rising_reset_min_available.dev_attr.attr,
	&iio_dev_attr_in_angl1_thresh_rising_value_available.dev_attr.attr,
	&iio_dev_attr_in_angl1_thresh_rising_hysteresis_available.dev_attr.attr,
	NULL,
};

static const struct attribute_group ad2s1210_event_attribute_group = {
	.attrs = ad2s1210_event_attributes,
};

static int ad2s1210_initial(struct ad2s1210_state *st)
{
	unsigned int data;
	int ret;

	guard(mutex)(&st->lock);

	/* Use default config register value plus resolution from devicetree. */
	data = FIELD_PREP(AD2S1210_PHASE_LOCK_RANGE_44, 1);
	data |= FIELD_PREP(AD2S1210_ENABLE_HYSTERESIS, 1);
	data |= FIELD_PREP(AD2S1210_SET_ENRES, 0x3);
	data |= FIELD_PREP(AD2S1210_SET_RES, st->resolution);

	ret = regmap_write(st->regmap, AD2S1210_REG_CONTROL, data);
	if (ret < 0)
		return ret;

	return ad2s1210_reinit_excitation_frequency(st, AD2S1210_DEF_EXCIT);
}

static int ad2s1210_read_label(struct iio_dev *indio_dev,
			       struct iio_chan_spec const *chan,
			       char *label)
{
	if (chan->type == IIO_ANGL) {
		if (chan->channel == 0)
			return sprintf(label, "position\n");
		if (chan->channel == 1)
			return sprintf(label, "tracking error\n");
	}
	if (chan->type == IIO_ANGL_VEL)
		return sprintf(label, "velocity\n");
	if (chan->type == IIO_PHASE)
		return sprintf(label, "synthetic reference\n");
	if (chan->type == IIO_ALTVOLTAGE) {
		if (chan->output)
			return sprintf(label, "excitation\n");
		if (chan->channel == 0)
			return sprintf(label, "monitor signal\n");
		if (chan->channel == 1)
			return sprintf(label, "cosine\n");
		if (chan->channel == 2)
			return sprintf(label, "sine\n");
	}

	return -EINVAL;
}

static int ad2s1210_read_event_value(struct iio_dev *indio_dev,
				     const struct iio_chan_spec *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     enum iio_event_info info,
				     int *val, int *val2)
{
	struct ad2s1210_state *st = iio_priv(indio_dev);

	switch (chan->type) {
	case IIO_ANGL:
		switch (info) {
		case IIO_EV_INFO_VALUE:
			return ad2s1210_get_lot_high_threshold(st, val, val2);
		case IIO_EV_INFO_HYSTERESIS:
			return ad2s1210_get_lot_low_threshold(st, val, val2);
		default:
			return -EINVAL;
		}
	case IIO_ALTVOLTAGE:
		if (chan->output)
			return -EINVAL;
		if (type == IIO_EV_TYPE_THRESH && dir == IIO_EV_DIR_FALLING)
			return ad2s1210_get_voltage_threshold(st,
						AD2S1210_REG_LOS_THRD, val);
		if (type == IIO_EV_TYPE_THRESH && dir == IIO_EV_DIR_RISING)
			return ad2s1210_get_voltage_threshold(st,
						AD2S1210_REG_DOS_OVR_THRD, val);
		if (type == IIO_EV_TYPE_MAG)
			return ad2s1210_get_voltage_threshold(st,
						AD2S1210_REG_DOS_MIS_THRD, val);
		return -EINVAL;
	case IIO_PHASE:
		return ad2s1210_get_phase_lock_range(st, val, val2);
	default:
		return -EINVAL;
	}
}

static int ad2s1210_write_event_value(struct iio_dev *indio_dev,
				      const struct iio_chan_spec *chan,
				      enum iio_event_type type,
				      enum iio_event_direction dir,
				      enum iio_event_info info,
				      int val, int val2)
{
	struct ad2s1210_state *st = iio_priv(indio_dev);

	switch (chan->type) {
	case IIO_ANGL:
		switch (info) {
		case IIO_EV_INFO_VALUE:
			return ad2s1210_set_lot_high_threshold(st, val, val2);
		case IIO_EV_INFO_HYSTERESIS:
			return ad2s1210_set_lot_low_threshold(st, val, val2);
		default:
			return -EINVAL;
		}
	case IIO_ALTVOLTAGE:
		if (chan->output)
			return -EINVAL;
		if (type == IIO_EV_TYPE_THRESH && dir == IIO_EV_DIR_FALLING)
			return ad2s1210_set_voltage_threshold(st,
						AD2S1210_REG_LOS_THRD, val);
		if (type == IIO_EV_TYPE_THRESH && dir == IIO_EV_DIR_RISING)
			return ad2s1210_set_voltage_threshold(st,
						AD2S1210_REG_DOS_OVR_THRD, val);
		if (type == IIO_EV_TYPE_MAG)
			return ad2s1210_set_voltage_threshold(st,
						AD2S1210_REG_DOS_MIS_THRD, val);
		return -EINVAL;
	case IIO_PHASE:
		return ad2s1210_set_phase_lock_range(st, val, val2);
	default:
		return -EINVAL;
	}
}

static int ad2s1210_read_event_label(struct iio_dev *indio_dev,
				     struct iio_chan_spec const *chan,
				     enum iio_event_type type,
				     enum iio_event_direction dir,
				     char *label)
{
	if (chan->type == IIO_ANGL)
		return sprintf(label, "LOT\n");
	if (chan->type == IIO_ANGL_VEL)
		return sprintf(label, "max tracking rate\n");
	if (chan->type == IIO_PHASE)
		return sprintf(label, "phase lock\n");
	if (chan->type == IIO_ALTVOLTAGE) {
		if (chan->channel == 0) {
			if (type == IIO_EV_TYPE_THRESH &&
			    dir == IIO_EV_DIR_FALLING)
				return sprintf(label, "LOS\n");
			if (type == IIO_EV_TYPE_THRESH &&
			    dir == IIO_EV_DIR_RISING)
				return sprintf(label, "DOS overrange\n");
			if (type == IIO_EV_TYPE_MAG)
				return sprintf(label, "DOS mismatch\n");
		}
		if (chan->channel == 1 || chan->channel == 2)
			return sprintf(label, "clipped\n");
	}

	return -EINVAL;
}

static int ad2s1210_debugfs_reg_access(struct iio_dev *indio_dev,
				       unsigned int reg, unsigned int writeval,
				       unsigned int *readval)
{
	struct ad2s1210_state *st = iio_priv(indio_dev);

	guard(mutex)(&st->lock);

	if (readval)
		return regmap_read(st->regmap, reg, readval);

	return regmap_write(st->regmap, reg, writeval);
}

static irqreturn_t ad2s1210_trigger_handler(int irq, void *p)
{
	struct iio_poll_func *pf = p;
	struct iio_dev *indio_dev = pf->indio_dev;
	struct ad2s1210_state *st = iio_priv(indio_dev);
	size_t chan = 0;
	int ret;

	guard(mutex)(&st->lock);

	memset(&st->scan, 0, sizeof(st->scan));
	ad2s1210_toggle_sample_line(st);

	if (test_bit(0, indio_dev->active_scan_mask)) {
		if (st->fixed_mode == MOD_CONFIG) {
			ret = regmap_bulk_read(st->regmap,
					       AD2S1210_REG_POSITION_MSB,
					       &st->sample.raw, 2);
			if (ret < 0)
				goto error_ret;
		} else {
			ret = ad2s1210_set_mode(st, MOD_POS);
			if (ret < 0)
				goto error_ret;

			ret = spi_read(st->sdev, &st->sample, 3);
			if (ret < 0)
				goto error_ret;
		}

		memcpy(&st->scan.chan[chan++], &st->sample.raw, 2);
	}

	if (test_bit(1, indio_dev->active_scan_mask)) {
		if (st->fixed_mode == MOD_CONFIG) {
			ret = regmap_bulk_read(st->regmap,
					       AD2S1210_REG_VELOCITY_MSB,
					       &st->sample.raw, 2);
			if (ret < 0)
				goto error_ret;
		} else {
			ret = ad2s1210_set_mode(st, MOD_VEL);
			if (ret < 0)
				goto error_ret;

			ret = spi_read(st->sdev, &st->sample, 3);
			if (ret < 0)
				goto error_ret;
		}

		memcpy(&st->scan.chan[chan++], &st->sample.raw, 2);
	}

	if (st->fixed_mode == MOD_CONFIG) {
		unsigned int reg_val;

		ret = regmap_read(st->regmap, AD2S1210_REG_FAULT, &reg_val);
		if (ret < 0)
			return ret;

		st->sample.fault = reg_val;
	}

	ad2s1210_push_events(indio_dev, st->sample.fault, pf->timestamp);
	iio_push_to_buffers_with_ts(indio_dev, &st->scan, sizeof(st->scan),
				    pf->timestamp);

error_ret:
	iio_trigger_notify_done(indio_dev->trig);

	return IRQ_HANDLED;
}

static const struct iio_info ad2s1210_info = {
	.event_attrs = &ad2s1210_event_attribute_group,
	.read_raw = ad2s1210_read_raw,
	.read_avail = ad2s1210_read_avail,
	.write_raw = ad2s1210_write_raw,
	.read_label = ad2s1210_read_label,
	.read_event_value = ad2s1210_read_event_value,
	.write_event_value = ad2s1210_write_event_value,
	.read_event_label = ad2s1210_read_event_label,
	.debugfs_reg_access = &ad2s1210_debugfs_reg_access,
};

static int ad2s1210_setup_properties(struct ad2s1210_state *st)
{
	struct device *dev = &st->sdev->dev;
	const char *str_val;
	u32 val;
	int ret;

	ret = device_property_read_string(dev, "adi,fixed-mode", &str_val);
	if (ret == -EINVAL)
		st->fixed_mode = -1;
	else if (ret < 0)
		return dev_err_probe(dev, ret,
			"failed to read adi,fixed-mode property\n");
	else {
		if (strcmp(str_val, "config"))
			return dev_err_probe(dev, -EINVAL,
				"only adi,fixed-mode=\"config\" is supported\n");

		st->fixed_mode = MOD_CONFIG;
	}

	ret = device_property_read_u32(dev, "assigned-resolution-bits", &val);
	if (ret < 0)
		return dev_err_probe(dev, ret,
			"failed to read assigned-resolution-bits property\n");

	if (val < 10 || val > 16)
		return dev_err_probe(dev, -EINVAL,
				     "resolution out of range: %u\n", val);

	st->resolution = (val - 10) >> 1;
	/*
	 * These are values that correlate to the hysteresis bit in the Control
	 * register. 0 = disabled, 1 = enabled. When enabled, the actual
	 * hysteresis is +/- 1 LSB of the raw position value. Which bit is the
	 * LSB depends on the specified resolution.
	 */
	st->hysteresis_available[0] = 0;
	st->hysteresis_available[1] = 1 << (2 * (AD2S1210_RES_16 -
						 st->resolution));

	return 0;
}

static int ad2s1210_setup_clocks(struct ad2s1210_state *st)
{
	struct device *dev = &st->sdev->dev;
	struct clk *clk;

	clk = devm_clk_get_enabled(dev, NULL);
	if (IS_ERR(clk))
		return dev_err_probe(dev, PTR_ERR(clk), "failed to get clock\n");

	st->clkin_hz = clk_get_rate(clk);
	if (st->clkin_hz < AD2S1210_MIN_CLKIN || st->clkin_hz > AD2S1210_MAX_CLKIN)
		return dev_err_probe(dev, -EINVAL,
				     "clock frequency out of range: %lu\n",
				     st->clkin_hz);

	return 0;
}

static int ad2s1210_setup_gpios(struct ad2s1210_state *st)
{
	struct device *dev = &st->sdev->dev;
	struct gpio_descs *resolution_gpios;
	struct gpio_desc *reset_gpio;
	DECLARE_BITMAP(bitmap, 2) = { };
	int ret;

	/* should not be sampling on startup */
	st->sample_gpio = devm_gpiod_get(dev, "sample", GPIOD_OUT_LOW);
	if (IS_ERR(st->sample_gpio))
		return dev_err_probe(dev, PTR_ERR(st->sample_gpio),
				     "failed to request sample GPIO\n");

	/* both pins high means that we start in config mode */
	st->mode_gpios = devm_gpiod_get_array_optional(dev, "mode",
						       GPIOD_OUT_HIGH);
	if (IS_ERR(st->mode_gpios))
		return dev_err_probe(dev, PTR_ERR(st->mode_gpios),
				     "failed to request mode GPIOs\n");

	if (!st->mode_gpios && st->fixed_mode == -1)
		return dev_err_probe(dev, -EINVAL,
			"must specify either adi,fixed-mode or mode-gpios\n");

	if (st->mode_gpios && st->fixed_mode != -1)
		return dev_err_probe(dev, -EINVAL,
			"must specify only one of adi,fixed-mode or mode-gpios\n");

	if (st->mode_gpios && st->mode_gpios->ndescs != 2)
		return dev_err_probe(dev, -EINVAL,
				     "requires exactly 2 mode-gpios\n");

	/*
	 * If resolution gpios are provided, they get set to the required
	 * resolution, otherwise it is assumed the RES0 and RES1 pins are
	 * hard-wired to match the resolution indicated in the devicetree.
	 */
	resolution_gpios = devm_gpiod_get_array_optional(dev, "resolution",
							 GPIOD_ASIS);
	if (IS_ERR(resolution_gpios))
		return dev_err_probe(dev, PTR_ERR(resolution_gpios),
				     "failed to request resolution GPIOs\n");

	if (resolution_gpios) {
		if (resolution_gpios->ndescs != 2)
			return dev_err_probe(dev, -EINVAL,
				      "requires exactly 2 resolution-gpios\n");

		bitmap_write(bitmap, st->resolution, 0, 2);

		ret = gpiod_multi_set_value_cansleep(resolution_gpios, bitmap);
		if (ret < 0)
			return dev_err_probe(dev, ret,
					     "failed to set resolution gpios\n");
	}

	/* If the optional reset GPIO is present, toggle it to do a hard reset. */
	reset_gpio = devm_gpiod_get_optional(dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(reset_gpio))
		return dev_err_probe(dev, PTR_ERR(reset_gpio),
				     "failed to request reset GPIO\n");

	if (reset_gpio) {
		udelay(10);
		gpiod_set_value(reset_gpio, 0);
	}

	return 0;
}

static const struct regmap_range ad2s1210_regmap_readable_ranges[] = {
	regmap_reg_range(AD2S1210_REG_POSITION_MSB, AD2S1210_REG_VELOCITY_LSB),
	regmap_reg_range(AD2S1210_REG_LOS_THRD, AD2S1210_REG_LOT_LOW_THRD),
	regmap_reg_range(AD2S1210_REG_EXCIT_FREQ, AD2S1210_REG_CONTROL),
	regmap_reg_range(AD2S1210_REG_FAULT, AD2S1210_REG_FAULT),
};

static const struct regmap_access_table ad2s1210_regmap_rd_table = {
	.yes_ranges = ad2s1210_regmap_readable_ranges,
	.n_yes_ranges = ARRAY_SIZE(ad2s1210_regmap_readable_ranges),
};

static const struct regmap_range ad2s1210_regmap_writeable_ranges[] = {
	regmap_reg_range(AD2S1210_REG_LOS_THRD, AD2S1210_REG_LOT_LOW_THRD),
	regmap_reg_range(AD2S1210_REG_EXCIT_FREQ, AD2S1210_REG_CONTROL),
	regmap_reg_range(AD2S1210_REG_SOFT_RESET, AD2S1210_REG_SOFT_RESET),
	regmap_reg_range(AD2S1210_REG_FAULT, AD2S1210_REG_FAULT),
};

static const struct regmap_access_table ad2s1210_regmap_wr_table = {
	.yes_ranges = ad2s1210_regmap_writeable_ranges,
	.n_yes_ranges = ARRAY_SIZE(ad2s1210_regmap_writeable_ranges),
};

static int ad2s1210_setup_regmap(struct ad2s1210_state *st)
{
	struct device *dev = &st->sdev->dev;
	const struct regmap_config config = {
		.reg_bits = 8,
		.val_bits = 8,
		.disable_locking = true,
		.reg_read = ad2s1210_regmap_reg_read,
		.reg_write = ad2s1210_regmap_reg_write,
		.rd_table = &ad2s1210_regmap_rd_table,
		.wr_table = &ad2s1210_regmap_wr_table,
		.can_sleep = true,
	};

	st->regmap = devm_regmap_init(dev, NULL, st, &config);
	if (IS_ERR(st->regmap))
		return dev_err_probe(dev, PTR_ERR(st->regmap),
				     "failed to allocate register map\n");

	return 0;
}

static int ad2s1210_probe(struct spi_device *spi)
{
	struct iio_dev *indio_dev;
	struct ad2s1210_state *st;
	int ret;

	indio_dev = devm_iio_device_alloc(&spi->dev, sizeof(*st));
	if (!indio_dev)
		return -ENOMEM;
	st = iio_priv(indio_dev);

	mutex_init(&st->lock);
	st->sdev = spi;

	ret = ad2s1210_setup_properties(st);
	if (ret < 0)
		return ret;

	ret = ad2s1210_setup_clocks(st);
	if (ret < 0)
		return ret;

	ret = ad2s1210_setup_gpios(st);
	if (ret < 0)
		return ret;

	ret = ad2s1210_setup_regmap(st);
	if (ret < 0)
		return ret;

	ret = ad2s1210_initial(st);
	if (ret < 0)
		return ret;

	indio_dev->info = &ad2s1210_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = ad2s1210_channels;
	indio_dev->num_channels = ARRAY_SIZE(ad2s1210_channels);
	indio_dev->name = spi_get_device_id(spi)->name;

	ret = devm_iio_triggered_buffer_setup(&spi->dev, indio_dev,
					      &iio_pollfunc_store_time,
					      &ad2s1210_trigger_handler, NULL);
	if (ret < 0)
		return dev_err_probe(&spi->dev, ret,
				     "iio triggered buffer setup failed\n");

	return devm_iio_device_register(&spi->dev, indio_dev);
}

static const struct of_device_id ad2s1210_of_match[] = {
	{ .compatible = "adi,ad2s1210", },
	{ }
};
MODULE_DEVICE_TABLE(of, ad2s1210_of_match);

static const struct spi_device_id ad2s1210_id[] = {
	{ "ad2s1210" },
	{ }
};
MODULE_DEVICE_TABLE(spi, ad2s1210_id);

static struct spi_driver ad2s1210_driver = {
	.driver = {
		.name = "ad2s1210",
		.of_match_table = ad2s1210_of_match,
	},
	.probe = ad2s1210_probe,
	.id_table = ad2s1210_id,
};
module_spi_driver(ad2s1210_driver);

MODULE_AUTHOR("Graff Yang <graff.yang@gmail.com>");
MODULE_DESCRIPTION("Analog Devices AD2S1210 Resolver to Digital SPI driver");
MODULE_LICENSE("GPL v2");
