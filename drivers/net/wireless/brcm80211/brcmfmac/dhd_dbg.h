/*
 * Copyright (c) 2010 Broadcom Corporation
 *
 * Permission to use, copy, modify, and/or distribute this software for any
 * purpose with or without fee is hereby granted, provided that the above
 * copyright notice and this permission notice appear in all copies.
 *
 * THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
 * WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
 * MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY
 * SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
 * WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN ACTION
 * OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF OR IN
 * CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
 */

#ifndef _BRCMF_DBG_H_
#define _BRCMF_DBG_H_

/* message levels */
#define BRCMF_ERROR_VAL	0x0001
#define BRCMF_TRACE_VAL	0x0002
#define BRCMF_INFO_VAL	0x0004
#define BRCMF_DATA_VAL	0x0008
#define BRCMF_CTL_VAL	0x0010
#define BRCMF_TIMER_VAL	0x0020
#define BRCMF_HDRS_VAL	0x0040
#define BRCMF_BYTES_VAL	0x0080
#define BRCMF_INTR_VAL	0x0100
#define BRCMF_GLOM_VAL	0x0400
#define BRCMF_EVENT_VAL	0x0800
#define BRCMF_BTA_VAL	0x1000
#define BRCMF_ISCAN_VAL 0x2000

#if defined(BCMDBG)

#define brcmf_dbg(level, fmt, ...)					\
do {									\
	if (BRCMF_ERROR_VAL == BRCMF_##level##_VAL) {			\
		if (brcmf_msg_level & BRCMF_##level##_VAL) {		\
			if (net_ratelimit())				\
				printk(KERN_DEBUG "%s: " fmt,		\
				       __func__, ##__VA_ARGS__);	\
		}							\
	} else {							\
		if (brcmf_msg_level & BRCMF_##level##_VAL) {		\
			printk(KERN_DEBUG "%s: " fmt,			\
			       __func__, ##__VA_ARGS__);		\
		}							\
	}								\
} while (0)

#define BRCMF_DATA_ON()		(brcmf_msg_level & BRCMF_DATA_VAL)
#define BRCMF_CTL_ON()		(brcmf_msg_level & BRCMF_CTL_VAL)
#define BRCMF_HDRS_ON()		(brcmf_msg_level & BRCMF_HDRS_VAL)
#define BRCMF_BYTES_ON()	(brcmf_msg_level & BRCMF_BYTES_VAL)
#define BRCMF_GLOM_ON()		(brcmf_msg_level & BRCMF_GLOM_VAL)

#else	/* (defined BCMDBG) || (defined BCMDBG) */

#define brcmf_dbg(level, fmt, ...) no_printk(fmt, ##__VA_ARGS__)

#define BRCMF_DATA_ON()		0
#define BRCMF_CTL_ON()		0
#define BRCMF_HDRS_ON()		0
#define BRCMF_BYTES_ON()	0
#define BRCMF_GLOM_ON()		0

#endif				/* defined(BCMDBG) */

extern int brcmf_msg_level;

#endif				/* _BRCMF_DBG_H_ */
