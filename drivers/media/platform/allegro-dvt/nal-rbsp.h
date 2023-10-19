/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Copyright (C) 2019-2020 Pengutronix, Michael Tretter <kernel@pengutronix.de>
 */

#ifndef __NAL_RBSP_H__
#define __NAL_RBSP_H__

#include <linux/kernel.h>
#include <linux/types.h>

struct rbsp;

struct nal_rbsp_ops {
	int (*rbsp_bit)(struct rbsp *rbsp, int *val);
	int (*rbsp_bits)(struct rbsp *rbsp, int n, unsigned int *val);
	int (*rbsp_uev)(struct rbsp *rbsp, unsigned int *val);
	int (*rbsp_sev)(struct rbsp *rbsp, int *val);
};

/**
 * struct rbsp - State object for handling a raw byte sequence payload
 * @data: pointer to the data of the rbsp
 * @size: maximum size of the data of the rbsp
 * @pos: current bit position inside the rbsp
 * @num_consecutive_zeros: number of zeros before @pos
 * @ops: per datatype functions for interacting with the rbsp
 * @error: an error occurred while handling the rbsp
 *
 * This struct is passed around the various parsing functions and tracks the
 * current position within the raw byte sequence payload.
 *
 * The @ops field allows to separate the operation, i.e., reading/writing a
 * value from/to that rbsp, from the structure of the NAL unit. This allows to
 * have a single function for iterating the NAL unit, while @ops has function
 * pointers for handling each type in the rbsp.
 */
struct rbsp {
	u8 *data;
	size_t size;
	unsigned int pos;
	unsigned int num_consecutive_zeros;
	struct nal_rbsp_ops *ops;
	int error;
};

extern struct nal_rbsp_ops write;
extern struct nal_rbsp_ops read;

void rbsp_init(struct rbsp *rbsp, void *addr, size_t size,
	       struct nal_rbsp_ops *ops);
void rbsp_unsupported(struct rbsp *rbsp);

void rbsp_bit(struct rbsp *rbsp, int *value);
void rbsp_bits(struct rbsp *rbsp, int n, int *value);
void rbsp_uev(struct rbsp *rbsp, unsigned int *value);
void rbsp_sev(struct rbsp *rbsp, int *value);

void rbsp_trailing_bits(struct rbsp *rbsp);

#endif /* __NAL_RBSP_H__ */
