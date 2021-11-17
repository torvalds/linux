// SPDX-License-Identifier: GPL-2.0+
/* fakekey.c
 * Functions for simulating keypresses.
 *
 * Copyright (C) 2010 the Speakup Team
 */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/percpu.h>
#include <linux/input.h>

#include "speakup.h"

#define PRESSED 1
#define RELEASED 0

static DEFINE_PER_CPU(int, reporting_keystroke);

static struct input_dev *virt_keyboard;

int speakup_add_virtual_keyboard(void)
{
	int err;

	virt_keyboard = input_allocate_device();

	if (!virt_keyboard)
		return -ENOMEM;

	virt_keyboard->name = "Speakup";
	virt_keyboard->id.bustype = BUS_VIRTUAL;
	virt_keyboard->phys = "speakup/input0";
	virt_keyboard->dev.parent = NULL;

	__set_bit(EV_KEY, virt_keyboard->evbit);
	__set_bit(KEY_DOWN, virt_keyboard->keybit);

	err = input_register_device(virt_keyboard);
	if (err) {
		input_free_device(virt_keyboard);
		virt_keyboard = NULL;
	}

	return err;
}

void speakup_remove_virtual_keyboard(void)
{
	if (virt_keyboard) {
		input_unregister_device(virt_keyboard);
		virt_keyboard = NULL;
	}
}

/*
 * Send a simulated down-arrow to the application.
 */
void speakup_fake_down_arrow(void)
{
	unsigned long flags;

	/* disable keyboard interrupts */
	local_irq_save(flags);
	/* don't change CPU */
	preempt_disable();

	__this_cpu_write(reporting_keystroke, true);
	input_report_key(virt_keyboard, KEY_DOWN, PRESSED);
	input_report_key(virt_keyboard, KEY_DOWN, RELEASED);
	input_sync(virt_keyboard);
	__this_cpu_write(reporting_keystroke, false);

	/* reenable preemption */
	preempt_enable();
	/* reenable keyboard interrupts */
	local_irq_restore(flags);
}

/*
 * Are we handling a simulated keypress on the current CPU?
 * Returns a boolean.
 */
bool speakup_fake_key_pressed(void)
{
	return this_cpu_read(reporting_keystroke);
}
