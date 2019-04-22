/*
 * Copyright 2016 Broadcom
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License, version 2, as
 * published by the Free Software Foundation (the "GPL").
 *
 * This program is distributed in the hope that it will be useful, but
 * WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * General Public License version 2 (GPLv2) for more details.
 *
 * You should have received a copy of the GNU General Public License
 * version 2 (GPLv2) along with this source code.
 */

#ifndef _UTIL_H
#define _UTIL_H

#include <linux/kernel.h>
#include <linux/delay.h>

#include "spu.h"

extern int flow_debug_logging;
extern int packet_debug_logging;
extern int debug_logging_sleep;

#ifdef DEBUG
#define flow_log(...)	                \
	do {	                              \
		if (flow_debug_logging) {	        \
			printk(__VA_ARGS__);	          \
			if (debug_logging_sleep)	      \
				msleep(debug_logging_sleep);	\
		}	                                \
	} while (0)
#define flow_dump(msg, var, var_len)	   \
	do {	                                 \
		if (flow_debug_logging) {	           \
			print_hex_dump(KERN_ALERT, msg, DUMP_PREFIX_NONE,  \
					16, 1, var, var_len, false); \
				if (debug_logging_sleep)	       \
					msleep(debug_logging_sleep);   \
		}                                    \
	} while (0)

#define packet_log(...)               \
	do {                                \
		if (packet_debug_logging) {       \
			printk(__VA_ARGS__);            \
			if (debug_logging_sleep)        \
				msleep(debug_logging_sleep);  \
		}                                 \
	} while (0)
#define packet_dump(msg, var, var_len)   \
	do {                                   \
		if (packet_debug_logging) {          \
			print_hex_dump(KERN_ALERT, msg, DUMP_PREFIX_NONE,  \
					16, 1, var, var_len, false); \
			if (debug_logging_sleep)           \
				msleep(debug_logging_sleep);     \
		}                                    \
	} while (0)

void __dump_sg(struct scatterlist *sg, unsigned int skip, unsigned int len);

#define dump_sg(sg, skip, len)     __dump_sg(sg, skip, len)

#else /* !DEBUG_ON */

#define flow_log(...) do {} while (0)
#define flow_dump(msg, var, var_len) do {} while (0)
#define packet_log(...) do {} while (0)
#define packet_dump(msg, var, var_len) do {} while (0)

#define dump_sg(sg, skip, len) do {} while (0)

#endif /* DEBUG_ON */

int spu_sg_at_offset(struct scatterlist *sg, unsigned int skip,
		     struct scatterlist **sge, unsigned int *sge_offset);

/* Copy sg data, from skip, length len, to dest */
void sg_copy_part_to_buf(struct scatterlist *src, u8 *dest,
			 unsigned int len, unsigned int skip);
/* Copy src into scatterlist from offset, length len */
void sg_copy_part_from_buf(struct scatterlist *dest, u8 *src,
			   unsigned int len, unsigned int skip);

int spu_sg_count(struct scatterlist *sg_list, unsigned int skip, int nbytes);
u32 spu_msg_sg_add(struct scatterlist **to_sg,
		   struct scatterlist **from_sg, u32 *skip,
		   u8 from_nents, u32 tot_len);

void add_to_ctr(u8 *ctr_pos, unsigned int increment);

/* produce a message digest from data of length n bytes */
int do_shash(unsigned char *name, unsigned char *result,
	     const u8 *data1, unsigned int data1_len,
	     const u8 *data2, unsigned int data2_len,
	     const u8 *key, unsigned int key_len);

char *spu_alg_name(enum spu_cipher_alg alg, enum spu_cipher_mode mode);

void spu_setup_debugfs(void);
void spu_free_debugfs(void);
void format_value_ccm(unsigned int val, u8 *buf, u8 len);

#endif
