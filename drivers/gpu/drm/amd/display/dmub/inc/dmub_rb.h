/*
 * Copyright 2019 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: AMD
 *
 */

#ifndef _DMUB_RB_H_
#define _DMUB_RB_H_

#include "dmub_types.h"
#include "dmub_cmd.h"

#if defined(__cplusplus)
extern "C" {
#endif

struct dmub_cmd_header;

struct dmub_rb_init_params {
	void *ctx;
	void *base_address;
	uint32_t capacity;
};

struct dmub_rb {
	void *base_address;
	uint32_t data_count;
	uint32_t rptr;
	uint32_t wrpt;
	uint32_t capacity;

	void *ctx;
	void *dmub;
};


static inline bool dmub_rb_empty(struct dmub_rb *rb)
{
	return (rb->wrpt == rb->rptr);
}

static inline bool dmub_rb_full(struct dmub_rb *rb)
{
	uint32_t data_count;

	if (rb->wrpt >= rb->rptr)
		data_count = rb->wrpt - rb->rptr;
	else
		data_count = rb->capacity - (rb->rptr - rb->wrpt);

	return (data_count == (rb->capacity - DMUB_RB_CMD_SIZE));
}

static inline bool dmub_rb_push_front(struct dmub_rb *rb,
				      const struct dmub_cmd_header *cmd)
{
	uint64_t volatile *dst = (uint64_t volatile *)(rb->base_address) + rb->wrpt / sizeof(uint64_t);
	const uint64_t *src = (const uint64_t *)cmd;
	int i;

	if (dmub_rb_full(rb))
		return false;

	// copying data
	for (i = 0; i < DMUB_RB_CMD_SIZE / sizeof(uint64_t); i++)
		*dst++ = *src++;

	rb->wrpt += DMUB_RB_CMD_SIZE;

	if (rb->wrpt >= rb->capacity)
		rb->wrpt %= rb->capacity;

	return true;
}

static inline bool dmub_rb_front(struct dmub_rb *rb,
				 struct dmub_cmd_header *cmd)
{
	uint8_t *rd_ptr = (uint8_t *)rb->base_address + rb->rptr;

	if (dmub_rb_empty(rb))
		return false;

	dmub_memcpy(cmd, rd_ptr, DMUB_RB_CMD_SIZE);

	return true;
}

static inline bool dmub_rb_pop_front(struct dmub_rb *rb)
{
	if (dmub_rb_empty(rb))
		return false;

	rb->rptr += DMUB_RB_CMD_SIZE;

	if (rb->rptr >= rb->capacity)
		rb->rptr %= rb->capacity;

	return true;
}

static inline void dmub_rb_flush_pending(const struct dmub_rb *rb)
{
	uint32_t rptr = rb->rptr;
	uint32_t wptr = rb->wrpt;

	while (rptr != wptr) {
		uint64_t volatile *data = (uint64_t volatile *)rb->base_address + rptr / sizeof(uint64_t);
		//uint64_t volatile *p = (uint64_t volatile *)data;
		uint64_t temp;
		int i;

		for (i = 0; i < DMUB_RB_CMD_SIZE / sizeof(uint64_t); i++)
			temp = *data++;

		rptr += DMUB_RB_CMD_SIZE;
		if (rptr >= rb->capacity)
			rptr %= rb->capacity;
	}
}

static inline void dmub_rb_init(struct dmub_rb *rb,
				struct dmub_rb_init_params *init_params)
{
	rb->base_address = init_params->base_address;
	rb->capacity = init_params->capacity;
	rb->rptr = 0;
	rb->wrpt = 0;
}

#if defined(__cplusplus)
}
#endif

#endif /* _DMUB_RB_H_ */
