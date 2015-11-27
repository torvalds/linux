/*
 * Remote Controller core raw events header
 *
 * Copyright (C) 2010 by Mauro Carvalho Chehab
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

#ifndef _RC_CORE_PRIV
#define _RC_CORE_PRIV

/* Define the max number of pulse/space transitions to buffer */
#define	MAX_IR_EVENT_SIZE	512

#include <linux/slab.h>
#include <linux/spinlock.h>
#include <media/rc-core.h>

struct ir_raw_handler {
	struct list_head list;

	u64 protocols; /* which are handled by this handler */
	int (*decode)(struct rc_dev *dev, struct ir_raw_event event);

	/* These two should only be used by the lirc decoder */
	int (*raw_register)(struct rc_dev *dev);
	int (*raw_unregister)(struct rc_dev *dev);
};

struct ir_raw_event_ctrl {
	struct list_head		list;		/* to keep track of raw clients */
	struct task_struct		*thread;
	spinlock_t			lock;
	/* fifo for the pulse/space durations */
	DECLARE_KFIFO(kfifo, struct ir_raw_event, MAX_IR_EVENT_SIZE);
	ktime_t				last_event;	/* when last event occurred */
	enum raw_event_type		last_type;	/* last event type */
	struct rc_dev			*dev;		/* pointer to the parent rc_dev */

	/* raw decoder state follows */
	struct ir_raw_event prev_ev;
	struct ir_raw_event this_ev;
	struct nec_dec {
		int state;
		unsigned count;
		u32 bits;
		bool is_nec_x;
		bool necx_repeat;
	} nec;
	struct rc5_dec {
		int state;
		u32 bits;
		unsigned count;
		bool is_rc5x;
	} rc5;
	struct rc6_dec {
		int state;
		u8 header;
		u32 body;
		bool toggle;
		unsigned count;
		unsigned wanted_bits;
	} rc6;
	struct sony_dec {
		int state;
		u32 bits;
		unsigned count;
	} sony;
	struct jvc_dec {
		int state;
		u16 bits;
		u16 old_bits;
		unsigned count;
		bool first;
		bool toggle;
	} jvc;
	struct sanyo_dec {
		int state;
		unsigned count;
		u64 bits;
	} sanyo;
	struct sharp_dec {
		int state;
		unsigned count;
		u32 bits;
		unsigned int pulse_len;
	} sharp;
	struct mce_kbd_dec {
		struct input_dev *idev;
		struct timer_list rx_timeout;
		char name[64];
		char phys[64];
		int state;
		u8 header;
		u32 body;
		unsigned count;
		unsigned wanted_bits;
	} mce_kbd;
	struct lirc_codec {
		struct rc_dev *dev;
		struct lirc_driver *drv;
		int carrier_low;

		ktime_t gap_start;
		u64 gap_duration;
		bool gap;
		bool send_timeout_reports;

	} lirc;
	struct xmp_dec {
		int state;
		unsigned count;
		u32 durations[16];
	} xmp;
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

/* Returns true if event is normal pulse/space event */
static inline bool is_timing_event(struct ir_raw_event ev)
{
	return !ev.carrier_report && !ev.reset;
}

#define TO_US(duration)			DIV_ROUND_CLOSEST((duration), 1000)
#define TO_STR(is_pulse)		((is_pulse) ? "pulse" : "space")

/*
 * Routines from rc-raw.c to be used internally and by decoders
 */
u64 ir_raw_get_allowed_protocols(void);
int ir_raw_event_register(struct rc_dev *dev);
void ir_raw_event_unregister(struct rc_dev *dev);
int ir_raw_handler_register(struct ir_raw_handler *ir_raw_handler);
void ir_raw_handler_unregister(struct ir_raw_handler *ir_raw_handler);
void ir_raw_init(void);

/*
 * Decoder initialization code
 *
 * Those load logic are called during ir-core init, and automatically
 * loads the compiled decoders for their usage with IR raw events
 */

#endif /* _RC_CORE_PRIV */
