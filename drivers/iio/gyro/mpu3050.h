/* SPDX-License-Identifier: GPL-2.0 */
#include <linux/iio/iio.h>
#include <linux/mutex.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <linux/i2c.h>

/**
 * enum mpu3050_fullscale - indicates the full range of the sensor in deg/sec
 */
enum mpu3050_fullscale {
	FS_250_DPS = 0,
	FS_500_DPS,
	FS_1000_DPS,
	FS_2000_DPS,
};

/**
 * enum mpu3050_lpf - indicates the low pass filter width
 */
enum mpu3050_lpf {
	/* This implicity sets sample frequency to 8 kHz */
	LPF_256_HZ_NOLPF = 0,
	/* All others sets the sample frequency to 1 kHz */
	LPF_188_HZ,
	LPF_98_HZ,
	LPF_42_HZ,
	LPF_20_HZ,
	LPF_10_HZ,
	LPF_5_HZ,
	LPF_2100_HZ_NOLPF,
};

enum mpu3050_axis {
	AXIS_X = 0,
	AXIS_Y,
	AXIS_Z,
	AXIS_MAX,
};

/**
 * struct mpu3050 - instance state container for the device
 * @dev: parent device for this instance
 * @orientation: mounting matrix, flipped axis etc
 * @map: regmap to reach the registers
 * @lock: serialization lock to marshal all requests
 * @irq: the IRQ used for this device
 * @regs: the regulators to power this device
 * @fullscale: the current fullscale setting for the device
 * @lpf: digital low pass filter setting for the device
 * @divisor: base frequency divider: divides 8 or 1 kHz
 * @calibration: the three signed 16-bit calibration settings that
 * get written into the offset registers for each axis to compensate
 * for DC offsets
 * @trig: trigger for the MPU-3050 interrupt, if present
 * @hw_irq_trigger: hardware interrupt trigger is in use
 * @irq_actl: interrupt is active low
 * @irq_latch: latched IRQ, this means that it is a level IRQ
 * @irq_opendrain: the interrupt line shall be configured open drain
 * @pending_fifo_footer: tells us if there is a pending footer in the FIFO
 * that we have to read out first when handling the FIFO
 * @hw_timestamp: latest hardware timestamp from the trigger IRQ, when in
 * use
 * @i2cmux: an I2C mux reflecting the fact that this sensor is a hub with
 * a pass-through I2C interface coming out of it: this device needs to be
 * powered up in order to reach devices on the other side of this mux
 */
struct mpu3050 {
	struct device *dev;
	struct iio_mount_matrix orientation;
	struct regmap *map;
	struct mutex lock;
	int irq;
	struct regulator_bulk_data regs[2];
	enum mpu3050_fullscale fullscale;
	enum mpu3050_lpf lpf;
	u8 divisor;
	s16 calibration[3];
	struct iio_trigger *trig;
	bool hw_irq_trigger;
	bool irq_actl;
	bool irq_latch;
	bool irq_opendrain;
	bool pending_fifo_footer;
	s64 hw_timestamp;
	struct i2c_mux_core *i2cmux;
};

/* Probe called from different transports */
int mpu3050_common_probe(struct device *dev,
			 struct regmap *map,
			 int irq,
			 const char *name);
void mpu3050_common_remove(struct device *dev);

/* PM ops */
extern const struct dev_pm_ops mpu3050_dev_pm_ops;
