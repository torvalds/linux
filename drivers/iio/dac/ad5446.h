/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _LINUX_AD5446_H
#define _LINUX_AD5446_H

#include <linux/bits.h>
#include <linux/compiler.h>
#include <linux/iio/iio.h>
#include <linux/mutex.h>
#include <linux/types.h>

struct device;

extern const struct iio_chan_spec_ext_info ad5446_ext_info_powerdown[];

#define _AD5446_CHANNEL(bits, storage, _shift, ext) { \
	.type = IIO_VOLTAGE, \
	.indexed = 1, \
	.output = 1, \
	.channel = 0, \
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW), \
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE), \
	.scan_type = { \
		.sign = 'u', \
		.realbits = (bits), \
		.storagebits = (storage), \
		.shift = (_shift), \
		}, \
	.ext_info = (ext), \
}

#define AD5446_CHANNEL(bits, storage, shift) \
	_AD5446_CHANNEL(bits, storage, shift, NULL)

#define AD5446_CHANNEL_POWERDOWN(bits, storage, shift) \
	_AD5446_CHANNEL(bits, storage, shift, ad5446_ext_info_powerdown)

/**
 * struct ad5446_state - driver instance specific data
 * @dev:		this device
 * @chip_info:		chip model specific constants, available modes etc
 * @vref_mv:		actual reference voltage used
 * @cached_val:		store/retrieve values during power down
 * @pwr_down_mode:	power down mode (1k, 100k or tristate)
 * @pwr_down:		true if the device is in power down
 * @lock:		lock to protect the data buffer during write ops
 */
struct ad5446_state {
	struct device *dev;
	const struct ad5446_chip_info *chip_info;
	unsigned short vref_mv;
	unsigned int cached_val;
	unsigned int pwr_down_mode;
	unsigned int pwr_down;
	/* mutex to protect device shared data */
	struct mutex lock;
	union {
		__be16 d16;
		u8 d24[3];
	} __aligned(IIO_DMA_MINALIGN);
};

/**
 * struct ad5446_chip_info - chip specific information
 * @channel:		channel spec for the DAC
 * @int_vref_mv:	AD5620/40/60: the internal reference voltage
 * @write:		chip specific helper function to write to the register
 */
struct ad5446_chip_info {
	struct iio_chan_spec channel;
	u16 int_vref_mv;
	int (*write)(struct ad5446_state *st, unsigned int val);
};

int ad5446_probe(struct device *dev, const char *name,
		 const struct ad5446_chip_info *chip_info);

#endif
