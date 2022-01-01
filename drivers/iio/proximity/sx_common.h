/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright 2021 Google LLC.
 *
 * Code shared between most Semtech SAR sensor driver.
 */

#ifndef IIO_SX_COMMON_H
#define IIO_SX_COMMON_H

#include <linux/iio/iio.h>
#include <linux/iio/types.h>
#include <linux/regulator/consumer.h>
#include <linux/types.h>

struct device;
struct i2c_client;
struct regmap_config;
struct sx_common_data;

#define SX_COMMON_REG_IRQ_SRC				0x00

#define SX_COMMON_MAX_NUM_CHANNELS	4
static_assert(SX_COMMON_MAX_NUM_CHANNELS < BITS_PER_LONG);

struct sx_common_reg_default {
	u8 reg;
	u8 def;
};

/**
 * struct sx_common_ops: function pointers needed by common code
 *
 * List functions needed by common code to gather information or configure
 * the sensor.
 *
 * @read_prox_data:	Function to read raw proximity data.
 * @check_whoami:	Set device name based on whoami register.
 * @init_compensation:	Function to set initial compensation.
 * @wait_for_sample:	When there are no physical IRQ, function to wait for a
 *			sample to be ready.
 * @get_default_reg:	Populate the initial value for a given register.
 */
struct sx_common_ops {
	int (*read_prox_data)(struct sx_common_data *data,
			      const struct iio_chan_spec *chan, __be16 *val);
	int (*check_whoami)(struct device *dev, struct iio_dev *indio_dev);
	int (*init_compensation)(struct iio_dev *indio_dev);
	int (*wait_for_sample)(struct sx_common_data *data);
	const struct sx_common_reg_default  *
		(*get_default_reg)(struct device *dev, int idx,
				   struct sx_common_reg_default *reg_def);
};

/**
 * struct sx_common_chip_info: Semtech Sensor private chip information
 *
 * @reg_stat:		Main status register address.
 * @reg_irq_msk:	IRQ mask register address.
 * @reg_enable_chan:	Address to enable/disable channels.
 *			Each phase presented by the sensor is an IIO channel..
 * @reg_reset:		Reset register address.
 * @mask_enable_chan:	Mask over the channels bits in the enable channel
 *			register.
 * @stat_offset:	Offset to check phase status.
 * @irq_msk_offset:	Offset to enable interrupt in the IRQ mask
 *			register.
 * @num_channels:	Number of channels.
 * @num_default_regs:	Number of internal registers that can be configured.
 *
 * @ops:		Private functions pointers.
 * @iio_channels:	Description of exposed iio channels.
 * @num_iio_channels:	Number of iio_channels.
 * @iio_info:		iio_info structure for this driver.
 */
struct sx_common_chip_info {
	unsigned int reg_stat;
	unsigned int reg_irq_msk;
	unsigned int reg_enable_chan;
	unsigned int reg_reset;

	unsigned int mask_enable_chan;
	unsigned int stat_offset;
	unsigned int irq_msk_offset;
	unsigned int num_channels;
	int num_default_regs;

	struct sx_common_ops ops;

	const struct iio_chan_spec *iio_channels;
	int num_iio_channels;
	struct iio_info iio_info;
};

/**
 * struct sx_common_data: Semtech Sensor private data structure.
 *
 * @chip_info:		Structure defining sensor internals.
 * @mutex:		Serialize access to registers and channel configuration.
 * @completion:		completion object to wait for data acquisition.
 * @client:		I2C client structure.
 * @trig:		IIO trigger object.
 * @regmap:		Register map.
 * @num_default_regs:	Number of default registers to set at init.
 * @supplies:		Power supplies object.
 * @chan_prox_stat:	Last reading of the proximity status for each channel.
 *			We only send an event to user space when this changes.
 * @trigger_enabled:	True when the device trigger is enabled.
 * @buffer:		Buffer to store raw samples.
 * @suspend_ctrl:	Remember enabled channels and sample rate during suspend.
 * @chan_read:		Bit field for each raw channel enabled.
 * @chan_event:		Bit field for each event enabled.
 */
struct sx_common_data {
	const struct sx_common_chip_info *chip_info;

	struct mutex mutex;
	struct completion completion;
	struct i2c_client *client;
	struct iio_trigger *trig;
	struct regmap *regmap;

	struct regulator_bulk_data supplies[2];
	unsigned long chan_prox_stat;
	bool trigger_enabled;

	/* Ensure correct alignment of timestamp when present. */
	struct {
		__be16 channels[SX_COMMON_MAX_NUM_CHANNELS];
		s64 ts __aligned(8);
	} buffer;

	unsigned int suspend_ctrl;
	unsigned long chan_read;
	unsigned long chan_event;
};

int sx_common_read_proximity(struct sx_common_data *data,
			     const struct iio_chan_spec *chan, int *val);

int sx_common_read_event_config(struct iio_dev *indio_dev,
				const struct iio_chan_spec *chan,
				enum iio_event_type type,
				enum iio_event_direction dir);
int sx_common_write_event_config(struct iio_dev *indio_dev,
				 const struct iio_chan_spec *chan,
				 enum iio_event_type type,
				 enum iio_event_direction dir, int state);

int sx_common_probe(struct i2c_client *client,
		    const struct sx_common_chip_info *chip_info,
		    const struct regmap_config *regmap_config);

/* 3 is the number of events defined by a single phase. */
extern const struct iio_event_spec sx_common_events[3];

#endif  /* IIO_SX_COMMON_H */
