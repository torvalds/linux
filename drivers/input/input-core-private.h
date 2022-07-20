/* SPDX-License-Identifier: GPL-2.0-only */
#ifndef _INPUT_CORE_PRIVATE_H
#define _INPUT_CORE_PRIVATE_H

/*
 * Functions and definitions that are private to input core,
 * should not be used by input drivers or handlers.
 */

struct input_dev;

void input_mt_release_slots(struct input_dev *dev);
void input_handle_event(struct input_dev *dev,
			unsigned int type, unsigned int code, int value);

#endif /* _INPUT_CORE_PRIVATE_H */
