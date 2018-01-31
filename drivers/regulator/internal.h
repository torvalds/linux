/*
 * internal.h  --  Voltage/Current Regulator framework internal code
 *
 * Copyright 2007, 2008 Wolfson Microelectronics PLC.
 * Copyright 2008 SlimLogic Ltd.
 *
 * Author: Liam Girdwood <lrg@slimlogic.co.uk>
 *
 *  This program is free software; you can redistribute  it and/or modify it
 *  under  the terms of  the GNU General  Public License as published by the
 *  Free Software Foundation;  either version 2 of the  License, or (at your
 *  option) any later version.
 *
 */

#ifndef __REGULATOR_INTERNAL_H
#define __REGULATOR_INTERNAL_H

#include <linux/suspend.h>

#define REGULATOR_STATES_NUM	(PM_SUSPEND_MAX + 1)

struct regulator_voltage {
	int min_uV;
	int max_uV;
};

/*
 * struct regulator
 *
 * One for each consumer device.
 * @voltage - a voltage array for each state of runtime, i.e.:
 *            PM_SUSPEND_ON
 *            PM_SUSPEND_TO_IDLE
 *            PM_SUSPEND_STANDBY
 *            PM_SUSPEND_MEM
 *            PM_SUSPEND_MAX
 */
struct regulator {
	struct device *dev;
	struct list_head list;
	unsigned int always_on:1;
	unsigned int bypass:1;
	int uA_load;
	struct regulator_voltage voltage[REGULATOR_STATES_NUM];
	const char *supply_name;
	struct device_attribute dev_attr;
	struct regulator_dev *rdev;
	struct dentry *debugfs;
};

extern struct class regulator_class;

static inline struct regulator_dev *dev_to_rdev(struct device *dev)
{
	return container_of(dev, struct regulator_dev, dev);
}

struct regulator_dev *of_find_regulator_by_node(struct device_node *np);

#ifdef CONFIG_OF
struct regulator_init_data *regulator_of_get_init_data(struct device *dev,
			         const struct regulator_desc *desc,
				 struct regulator_config *config,
				 struct device_node **node);
#else
static inline struct regulator_init_data *
regulator_of_get_init_data(struct device *dev,
			   const struct regulator_desc *desc,
			   struct regulator_config *config,
			   struct device_node **node)
{
	return NULL;
}
#endif

enum regulator_get_type {
	NORMAL_GET,
	EXCLUSIVE_GET,
	OPTIONAL_GET,
	MAX_GET_TYPE
};

struct regulator *_regulator_get(struct device *dev, const char *id,
				 enum regulator_get_type get_type);

#endif
