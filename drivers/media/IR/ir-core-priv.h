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

	int (*decode)(struct input_dev *input_dev, struct ir_raw_event event);
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
static inline bool geq_margin(unsigned d1, unsigned d2, unsigned margin)
{
	return d1 > (d2 - margin);
}

static inline bool eq_margin(unsigned d1, unsigned d2, unsigned margin)
{
	return ((d1 > (d2 - margin)) && (d1 < (d2 + margin)));
}

static inline bool is_transition(struct ir_raw_event *x, struct ir_raw_event *y)
{
	return x->pulse != y->pulse;
}

static inline void decrease_duration(struct ir_raw_event *ev, unsigned duration)
{
	if (duration > ev->duration)
		ev->duration = 0;
	else
		ev->duration -= duration;
}

#define TO_US(duration)			(((duration) + 500) / 1000)
#define TO_STR(is_pulse)		((is_pulse) ? "pulse" : "space")
#define IS_RESET(ev)			(ev.duration == 0)

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
#ifdef CONFIG_IR_RC6_DECODER_MODULE
#define load_rc6_decode()	request_module("ir-rc6-decoder")
#else
#define load_rc6_decode()	0
#endif

/* from ir-jvc-decoder.c */
#ifdef CONFIG_IR_JVC_DECODER_MODULE
#define load_jvc_decode()	request_module("ir-jvc-decoder")
#else
#define load_jvc_decode()	0
#endif

/* from ir-sony-decoder.c */
#ifdef CONFIG_IR_SONY_DECODER_MODULE
#define load_sony_decode()	request_module("ir-sony-decoder")
#else
#define load_sony_decode()	0
#endif

#endif /* _IR_RAW_EVENT */
