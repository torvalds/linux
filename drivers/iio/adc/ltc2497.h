/* SPDX-License-Identifier: GPL-2.0-only */

#define LTC2497_ENABLE			0xA0
#define LTC2497_CONFIG_DEFAULT		LTC2497_ENABLE

/*
 * Conversion-time bounds used to gate reads.  Each value is the datasheet
 * t_CONV maximum, rounded UP to the next whole millisecond.
 *
 * The 2x mode (LTC2499_SPD, LTC2499 only) disables the offset auto-calibration
 * to roughly double the output rate; adding the 2x wait time is what makes the
 * SPD control actually faster.
 */
#define LTC2497_CONV_TIME_1X_MS		150ULL	/* ceil(t_CONV_1 simult. max 149.9) */
#define LTC2499_CONV_TIME_2X_MS		76ULL	/* ceil(t_CONV_2 simult. max  75.1) */

/*
 * Sentinel passed as `address` to result_and_measure() to request a
 * temperature conversion instead of a voltage channel.  Valid channel
 * addresses fit in 5 bits (0x00–0x1F), so 0xFF is unambiguous.
 */
#define LTC2497_TEMP_ADDR		0xFF

/* Second config-byte bits (LTC2499 / LTC2493 only) */
#define LTC2499_EN2			BIT(7)	/* enable second config byte */
#define LTC2499_IM			BIT(6)	/* 1 = measure internal temp sensor */
#define LTC2499_SPD			BIT(3)	/* 1 = 2x output rate (offset cal off) */

struct ltc2497_chip_info {
	const char *name;
	u32 resolution;
	bool has_temp;
	bool has_speed_mode;	/* SPD bit in the 2nd config byte (LTC2499/LTC2493) */
};

struct ltc2497core_driverdata {
	struct regulator *ref;
	ktime_t	time_prev;
	/* lock to protect against multiple access to the device */
	struct mutex lock;
	const struct ltc2497_chip_info	*chip_info;
	u8 addr_prev;
	bool speed_2x;	/* SPD: false = 1x (default), true = 2x */
	/* Conversion time (ms) of the conversion currently in flight. */
	unsigned int conv_time_prev;
	int (*result_and_measure)(struct ltc2497core_driverdata *ddata,
				  u8 address, int *val);
};

int ltc2497core_probe(struct device *dev, struct iio_dev *indio_dev);
void ltc2497core_remove(struct iio_dev *indio_dev);

MODULE_IMPORT_NS("LTC2497");
