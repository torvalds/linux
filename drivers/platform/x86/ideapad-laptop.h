/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
 *  ideapad-laptop.h - Lenovo IdeaPad ACPI Extras
 *
 *  Copyright © 2010 Intel Corporation
 *  Copyright © 2010 David Woodhouse <dwmw2@infradead.org>
 */

#ifndef _IDEAPAD_LAPTOP_H_
#define _IDEAPAD_LAPTOP_H_

#include <linux/notifier.h>

enum ideapad_laptop_notifier_actions {
	IDEAPAD_LAPTOP_YMC_EVENT,
};

int ideapad_laptop_register_notifier(struct notifier_block *nb);
int ideapad_laptop_unregister_notifier(struct notifier_block *nb);
void ideapad_laptop_call_notifier(unsigned long action, void *data);

#endif /* !_IDEAPAD_LAPTOP_H_ */
