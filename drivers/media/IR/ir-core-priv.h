/*
 * Remote Controller core raw events header
 *
 * Copyright (C) 2010 by Mauro Carvalho Chehab <mchehab@redhat.com>
 *
 * This program is free software; you can redistribute it and/or modify
 *  it under the terms of the GNU General Public License as published by
 *  the Free Software Foundation version 2 of the License.
 *
 *  This program is distributed in the hope that it will be useful,
 *  but WITHOUT ANY WARRANTY; without even the implied warranty of
 *  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 *  GNU General Public License for more details.
 */

#ifndef _IR_RAW_EVENT
#define _IR_RAW_EVENT

#include <linux/slab.h>
#include <media/ir-core.h>

struct ir_raw_handler {
	struct list_head list;

	int (*decode)(struct input_dev *input_dev, s64 duration);
	int (*raw_register)(struct input_dev *input_dev);
	int (*raw_unregister)(struct input_dev *input_dev);
};

struct ir_raw_event_ctrl {
	struct work_struct		rx_work;	/* for the rx decoding workqueue */
	struct kfifo			kfifo;		/* fifo for the pulse/space durations */
	ktime_t				last_event;	/* when last event occurred */
	enum raw_event_type		last_type;	/* last event type */
	struct input_dev		*input_dev;	/* pointer to the parent input_dev */
};

/* macros for IR decoders */
#define PULSE(units)				((units))
#define SPACE(units)				(-(units))
#define IS_RESET(duration)			((duration) == 0)
#define IS_PULSE(duration)			((duration) > 0)
#define IS_SPACE(duration)			((duration) < 0)
#define DURATION(duration)			(abs((duration)))
#define IS_TRANSITION(x, y)			((x) * (y) < 0)
#define DECREASE_DURATION(duration, amount)			\
	do {							\
		if (IS_SPACE(duration))				\
			duration += (amount);			\
		else if (IS_PULSE(duration))			\
			duration -= (amount);			\
	} while (0)

#define TO_UNITS(duration, unit_len)				\
	((int)((duration) > 0 ?					\
		DIV_ROUND_CLOSEST(abs((duration)), (unit_len)) :\
		-DIV_ROUND_CLOSEST(abs((duration)), (unit_len))))
#define TO_US(duration)		((int)TO_UNITS(duration, 1000))

/*
 * Routines from ir-keytable.c to be used internally on ir-core and decoders
 */

u32 ir_g_keycode_from_table(struct input_dev *input_dev,
			    u32 scancode);

/*
 * Routines from ir-sysfs.c - Meant to be called only internally inside
 * ir-core
 */

int ir_register_class(struct input_dev *input_dev);
void ir_unregister_class(struct input_dev *input_dev);

/*
 * Routines from ir-raw-event.c to be used internally and by decoders
 */
int ir_raw_event_register(struct input_dev *input_dev);
void ir_raw_event_unregister(struct input_dev *input_dev);
static inline void ir_raw_event_reset(struct input_dev *input_dev)
{
	ir_raw_event_store(input_dev, 0);
	ir_raw_event_handle(input_dev);
}
int ir_raw_handler_register(struct ir_raw_handler *ir_raw_handler);
void ir_raw_handler_unregister(struct ir_raw_handler *ir_raw_handler);
void ir_raw_init(void);


/*
 * Decoder initialization code
 *
 * Those load logic are called during ir-core init, and automatically
 * loads the compiled decoders for their usage with IR raw events
 */

/* from ir-nec-decoder.c */
#ifdef CONFIG_IR_NEC_DECODER_MODULE
#define load_nec_decode()	request_module("ir-nec-decoder")
#else
#define load_nec_decode()	0
#endif

/* from ir-rc5-decoder.c */
#ifdef CONFIG_IR_RC5_DECODER_MODULE
#define load_rc5_decode()	request_module("ir-rc5-decoder")
#else
#define load_rc5_decode()	0
#endif

/* from ir-rc6-decoder.c */
#ifdef CONFIG_IR_RC5_DECODER_MODULE
#define load_rc6_decode()	request_module("ir-rc6-decoder")
#else
#define load_rc6_decode()	0
#endif

#endif /* _IR_RAW_EVENT */
