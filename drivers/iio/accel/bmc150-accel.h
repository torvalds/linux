/* SPDX-License-Identifier: GPL-2.0 */
#ifndef _BMC150_ACCEL_H_
#define _BMC150_ACCEL_H_

#include <linux/atomic.h>
#include <linux/iio/iio.h>
#include <linux/mutex.h>
#include <linux/regulator/consumer.h>
#include <linux/workqueue.h>

struct regmap;
struct i2c_client;
struct bmc150_accel_chip_info;
struct bmc150_accel_interrupt_info;

struct bmc150_accel_interrupt {
	const struct bmc150_accel_interrupt_info *info;
	atomic_t users;
};

struct bmc150_accel_trigger {
	struct bmc150_accel_data *data;
	struct iio_trigger *indio_trig;
	int (*setup)(struct bmc150_accel_trigger *t, bool state);
	int intr;
	bool enabled;
};

enum bmc150_accel_interrupt_id {
	BMC150_ACCEL_INT_DATA_READY,
	BMC150_ACCEL_INT_ANY_MOTION,
	BMC150_ACCEL_INT_WATERMARK,
	BMC150_ACCEL_INTERRUPTS,
};

enum bmc150_accel_trigger_id {
	BMC150_ACCEL_TRIGGER_DATA_READY,
	BMC150_ACCEL_TRIGGER_ANY_MOTION,
	BMC150_ACCEL_TRIGGERS,
};

struct bmc150_accel_data {
	struct regmap *regmap;
	struct regulator_bulk_data regulators[2];
	struct bmc150_accel_interrupt interrupts[BMC150_ACCEL_INTERRUPTS];
	struct bmc150_accel_trigger triggers[BMC150_ACCEL_TRIGGERS];
	struct mutex mutex;
	u8 fifo_mode, watermark;
	s16 buffer[8];
	/*
	 * Ensure there is sufficient space and correct alignment for
	 * the timestamp if enabled
	 */
	struct {
		__le16 channels[3];
		s64 ts __aligned(8);
	} scan;
	u8 bw_bits;
	u32 slope_dur;
	u32 slope_thres;
	u32 range;
	int ev_enable_state;
	int64_t timestamp, old_timestamp; /* Only used in hw fifo mode. */
	const struct bmc150_accel_chip_info *chip_info;
	struct i2c_client *second_device;
	void (*resume_callback)(struct device *dev);
	struct delayed_work resume_work;
	struct iio_mount_matrix orientation;
};

int bmc150_accel_core_probe(struct device *dev, struct regmap *regmap, int irq,
			    const char *name, bool block_supported);
int bmc150_accel_core_remove(struct device *dev);
extern const struct dev_pm_ops bmc150_accel_pm_ops;
extern const struct regmap_config bmc150_regmap_conf;

#endif  /* _BMC150_ACCEL_H_ */
