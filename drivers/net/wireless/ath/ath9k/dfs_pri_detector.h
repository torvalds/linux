/*
 * Copyright (c) 2012 Neratec Solutions AG
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
 * ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
 * ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
 * OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef DFS_PRI_DETECTOR_H
#define DFS_PRI_DETECTOR_H

#include <linux/list.h>

/**
 * struct pri_sequence - sequence of pulses matching one PRI
 * @head: list_head
 * @pri: pulse repetition interval (PRI) in usecs
 * @dur: duration of sequence in usecs
 * @count: number of pulses in this sequence
 * @count_falses: number of not matching pulses in this sequence
 * @first_ts: time stamp of first pulse in usecs
 * @last_ts: time stamp of last pulse in usecs
 * @deadline_ts: deadline when this sequence becomes invalid (first_ts + dur)
 */
struct pri_sequence {
	struct list_head head;
	u32 pri;
	u32 dur;
	u32 count;
	u32 count_falses;
	u64 first_ts;
	u64 last_ts;
	u64 deadline_ts;
};

/**
 * struct pri_detector - PRI detector element for a dedicated radar type
 * @exit(): destructor
 * @add_pulse(): add pulse event, returns pri_sequence if pattern was detected
 * @reset(): clear states and reset to given time stamp
 * @rs: detector specs for this detector element
 * @last_ts: last pulse time stamp considered for this element in usecs
 * @sequences: list_head holding potential pulse sequences
 * @pulses: list connecting pulse_elem objects
 * @count: number of pulses in queue
 * @max_count: maximum number of pulses to be queued
 * @window_size: window size back from newest pulse time stamp in usecs
 */
struct pri_detector {
	void (*exit)     (struct pri_detector *de);
	struct pri_sequence *
	     (*add_pulse)(struct pri_detector *de, struct pulse_event *e);
	void (*reset)    (struct pri_detector *de, u64 ts);

/* private: internal use only */
	const struct radar_detector_specs *rs;
	u64 last_ts;
	struct list_head sequences;
	struct list_head pulses;
	u32 count;
	u32 max_count;
	u32 window_size;
};

struct pri_detector *pri_detector_init(const struct radar_detector_specs *rs);

#endif /* DFS_PRI_DETECTOR_H */
