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

#if defined(BCMDBG)

#define BRCMF_ERROR(args) \
	do {if ((brcmf_msg_level & BRCMF_ERROR_VAL) && (net_ratelimit())) \
		printk args; } while (0)
#define BRCMF_TRACE(args)	do {if (brcmf_msg_level & BRCMF_TRACE_VAL) \
					printk args; } while (0)
#define BRCMF_INFO(args)	do {if (brcmf_msg_level & BRCMF_INFO_VAL) \
					printk args; } while (0)
#define BRCMF_DATA(args)	do {if (brcmf_msg_level & BRCMF_DATA_VAL) \
					printk args; } while (0)
#define BRCMF_CTL(args)		do {if (brcmf_msg_level & BRCMF_CTL_VAL) \
					printk args; } while (0)
#define BRCMF_TIMER(args)	do {if (brcmf_msg_level & BRCMF_TIMER_VAL) \
					printk args; } while (0)
#define BRCMF_INTR(args)	do {if (brcmf_msg_level & BRCMF_INTR_VAL) \
					printk args; } while (0)
#define BRCMF_GLOM(args)	do {if (brcmf_msg_level & BRCMF_GLOM_VAL) \
					printk args; } while (0)
#define BRCMF_EVENT(args)	do {if (brcmf_msg_level & BRCMF_EVENT_VAL) \
					printk args; } while (0)

#define BRCMF_DATA_ON()		(brcmf_msg_level & BRCMF_DATA_VAL)
#define BRCMF_CTL_ON()		(brcmf_msg_level & BRCMF_CTL_VAL)
#define BRCMF_HDRS_ON()		(brcmf_msg_level & BRCMF_HDRS_VAL)
#define BRCMF_BYTES_ON()	(brcmf_msg_level & BRCMF_BYTES_VAL)
#define BRCMF_GLOM_ON()		(brcmf_msg_level & BRCMF_GLOM_VAL)

#else	/* (defined BCMDBG) || (defined BCMDBG) */

#define BRCMF_ERROR(args)  do {if (net_ratelimit()) printk args; } while (0)
#define BRCMF_TRACE(args)
#define BRCMF_INFO(args)
#define BRCMF_DATA(args)
#define BRCMF_CTL(args)
#define BRCMF_TIMER(args)
#define BRCMF_INTR(args)
#define BRCMF_GLOM(args)
#define BRCMF_EVENT(args)

#define BRCMF_DATA_ON()		0
#define BRCMF_CTL_ON()		0
#define BRCMF_HDRS_ON()		0
#define BRCMF_BYTES_ON()		0
#define BRCMF_GLOM_ON()		0

#endif				/* defined(BCMDBG) */

extern int brcmf_msg_level;

#endif				/* _BRCMF_DBG_H_ */
