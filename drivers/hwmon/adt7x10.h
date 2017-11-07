/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __HWMON_ADT7X10_H__
#define __HWMON_ADT7X10_H__

#include <linux/types.h>
#include <linux/pm.h>

/* ADT7410 registers definition */
#define ADT7X10_TEMPERATURE		0
#define ADT7X10_STATUS			2
#define ADT7X10_CONFIG			3
#define ADT7X10_T_ALARM_HIGH		4
#define ADT7X10_T_ALARM_LOW		6
#define ADT7X10_T_CRIT			8
#define ADT7X10_T_HYST			0xA
#define ADT7X10_ID			0xB

struct device;

struct adt7x10_ops {
	int (*read_byte)(struct device *, u8 reg);
	int (*write_byte)(struct device *, u8 reg, u8 data);
	int (*read_word)(struct device *, u8 reg);
	int (*write_word)(struct device *, u8 reg, u16 data);
};

int adt7x10_probe(struct device *dev, const char *name, int irq,
	const struct adt7x10_ops *ops);
int adt7x10_remove(struct device *dev, int irq);

#ifdef CONFIG_PM_SLEEP
extern const struct dev_pm_ops adt7x10_dev_pm_ops;
#define ADT7X10_DEV_PM_OPS (&adt7x10_dev_pm_ops)
#else
#define ADT7X10_DEV_PM_OPS NULL
#endif

#endif
