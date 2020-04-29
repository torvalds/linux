/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Generic serial GNSS receiver driver
 *
 * Copyright (C) 2018 Johan Hovold <johan@kernel.org>
 */

#ifndef _LINUX_GNSS_SERIAL_H
#define _LINUX_GNSS_SERIAL_H

#include <asm/termbits.h>
#include <linux/pm.h>

struct gnss_serial {
	struct serdev_device *serdev;
	struct gnss_device *gdev;
	speed_t	speed;
	const struct gnss_serial_ops *ops;
	unsigned long drvdata[0];
};

enum gnss_serial_pm_state {
	GNSS_SERIAL_OFF,
	GNSS_SERIAL_ACTIVE,
	GNSS_SERIAL_STANDBY,
};

struct gnss_serial_ops {
	int (*set_power)(struct gnss_serial *gserial,
				enum gnss_serial_pm_state state);
};

extern const struct dev_pm_ops gnss_serial_pm_ops;

struct gnss_serial *gnss_serial_allocate(struct serdev_device *gserial,
						size_t data_size);
void gnss_serial_free(struct gnss_serial *gserial);

int gnss_serial_register(struct gnss_serial *gserial);
void gnss_serial_deregister(struct gnss_serial *gserial);

static inline void *gnss_serial_get_drvdata(struct gnss_serial *gserial)
{
	return gserial->drvdata;
}

#endif /* _LINUX_GNSS_SERIAL_H */
