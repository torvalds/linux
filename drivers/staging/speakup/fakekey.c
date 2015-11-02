/* fakekey.c
 * Functions for simulating keypresses.
 *
 * Copyright (C) 2010 the Speakup Team
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include <linux/types.h>
#include <linux/slab.h>
#include <linux/preempt.h>
#include <linux/percpu.h>
#include <linux/input.h>

#include "speakup.h"

#define PRESSED 1
#define RELEASED 0

static DEFINE_PER_CPU(bool, reporting_keystroke);

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
	if (virt_keyboard != NULL) {
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
