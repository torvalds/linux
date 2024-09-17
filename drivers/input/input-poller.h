/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _INPUT_POLLER_H
#define _INPUT_POLLER_H

/*
 * Support for polling mode for input devices.
 */
#include <linux/sysfs.h>

struct input_dev_poller;

void input_dev_poller_finalize(struct input_dev_poller *poller);
void input_dev_poller_start(struct input_dev_poller *poller);
void input_dev_poller_stop(struct input_dev_poller *poller);

extern struct attribute_group input_poller_attribute_group;

#endif /* _INPUT_POLLER_H */
