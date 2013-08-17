/*
 * Copyright (c) 2013 Samsung Electronics Co., Ltd.
 *              http://www.samsung.com
 *
 * Header file for EXYNOS specific sysfs interface
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
*/

#include <linux/kobject.h>
#include <linux/sysfs.h>

struct sysfs_attr {
	struct attribute attr;
	ssize_t (*show)(struct kobject *kobj,
			struct attribute *attr, char *buf);
	ssize_t (*store)(struct kobject *a, struct attribute *b,
			 const char *c, size_t count);
};

extern unsigned int min_num_cpu;
extern unsigned int max_num_cpu;
