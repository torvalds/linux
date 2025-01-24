/* SPDX-License-Identifier: GPL-2.0-only */

#define LTC2497_ENABLE			0xA0
#define LTC2497_CONFIG_DEFAULT		LTC2497_ENABLE
#define LTC2497_CONVERSION_TIME_MS	150ULL

struct ltc2497_chip_info {
	u32 resolution;
	const char *name;
};

struct ltc2497core_driverdata {
	struct regulator *ref;
	ktime_t	time_prev;
	/* lock to protect against multiple access to the device */
	struct mutex lock;
	const struct ltc2497_chip_info	*chip_info;
	u8 addr_prev;
	int (*result_and_measure)(struct ltc2497core_driverdata *ddata,
				  u8 address, int *val);
};

int ltc2497core_probe(struct device *dev, struct iio_dev *indio_dev);
void ltc2497core_remove(struct iio_dev *indio_dev);

MODULE_IMPORT_NS("LTC2497");
