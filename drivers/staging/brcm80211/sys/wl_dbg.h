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

#ifndef _wl_dbg_h_
#define _wl_dbg_h_

/* wl_msg_level is a bit vector with defs in wlioctl.h */
extern u32 wl_msg_level;

#define WL_NONE(fmt, args...) no_printk(fmt, ##args)

#define WL_PRINT(level, fmt, args...)		\
do {						\
	if (wl_msg_level & level)		\
		printk(fmt, ##args);		\
} while (0)

#ifdef BCMDBG

#define	WL_ERROR(fmt, args...)	WL_PRINT(WL_ERROR_VAL, fmt, ##args)
#define	WL_TRACE(fmt, args...)	WL_PRINT(WL_TRACE_VAL, fmt, ##args)
#define WL_AMPDU(fmt, args...)	WL_PRINT(WL_AMPDU_VAL, fmt, ##args)
#define WL_FFPLD(fmt, args...)	WL_PRINT(WL_FFPLD_VAL, fmt, ##args)

#define WL_ERROR_ON()		(wl_msg_level & WL_ERROR_VAL)

/* Extra message control for AMPDU debugging */
#define   WL_AMPDU_UPDN_VAL	0x00000001	/* Config up/down related  */
#define   WL_AMPDU_ERR_VAL	0x00000002	/* Calls to beaocn update  */
#define   WL_AMPDU_TX_VAL	0x00000004	/* Transmit data path */
#define   WL_AMPDU_RX_VAL	0x00000008	/* Receive data path  */
#define   WL_AMPDU_CTL_VAL	0x00000010	/* TSF-related items  */
#define   WL_AMPDU_HW_VAL       0x00000020	/* AMPDU_HW */
#define   WL_AMPDU_HWTXS_VAL    0x00000040	/* AMPDU_HWTXS */
#define   WL_AMPDU_HWDBG_VAL    0x00000080	/* AMPDU_DBG */

extern u32 wl_ampdu_dbg;

#define WL_AMPDU_PRINT(level, fmt, args...)	\
do {						\
	if (wl_ampdu_dbg & level) {		\
		WL_AMPDU(fmt, ##args);		\
	}					\
} while (0)

#define WL_AMPDU_UPDN(fmt, args...)			\
	WL_AMPDU_PRINT(WL_AMPDU_UPDN_VAL, fmt, ##args)
#define WL_AMPDU_RX(fmt, args...)			\
	WL_AMPDU_PRINT(WL_AMPDU_RX_VAL, fmt, ##args)
#define WL_AMPDU_ERR(fmt, args...)			\
	WL_AMPDU_PRINT(WL_AMPDU_ERR_VAL, fmt, ##args)
#define WL_AMPDU_TX(fmt, args...)			\
	WL_AMPDU_PRINT(WL_AMPDU_TX_VAL, fmt, ##args)
#define WL_AMPDU_CTL(fmt, args...)			\
	WL_AMPDU_PRINT(WL_AMPDU_CTL_VAL, fmt, ##args)
#define WL_AMPDU_HW(fmt, args...)			\
	WL_AMPDU_PRINT(WL_AMPDU_HW_VAL, fmt, ##args)
#define WL_AMPDU_HWTXS(fmt, args...)			\
	WL_AMPDU_PRINT(WL_AMPDU_HWTXS_VAL, fmt, ##args)
#define WL_AMPDU_HWDBG(fmt, args...)			\
	WL_AMPDU_PRINT(WL_AMPDU_HWDBG_VAL, fmt, ##args)
#define WL_AMPDU_ERR_ON() (wl_ampdu_dbg & WL_AMPDU_ERR_VAL)
#define WL_AMPDU_HW_ON() (wl_ampdu_dbg & WL_AMPDU_HW_VAL)
#define WL_AMPDU_HWTXS_ON() (wl_ampdu_dbg & WL_AMPDU_HWTXS_VAL)

#else				/* BCMDBG */

#define	WL_ERROR(fmt, args...)		no_printk(fmt, ##args)
#define	WL_TRACE(fmt, args...)		no_printk(fmt, ##args)
#define WL_AMPDU(fmt, args...)		no_printk(fmt, ##args)
#define WL_FFPLD(fmt, args...)		no_printk(fmt, ##args)

#define WL_ERROR_ON()		0

#define WL_AMPDU_UPDN(fmt, args...)	no_printk(fmt, ##args)
#define WL_AMPDU_RX(fmt, args...)	no_printk(fmt, ##args)
#define WL_AMPDU_ERR(fmt, args...)	no_printk(fmt, ##args)
#define WL_AMPDU_TX(fmt, args...)	no_printk(fmt, ##args)
#define WL_AMPDU_CTL(fmt, args...)	no_printk(fmt, ##args)
#define WL_AMPDU_HW(fmt, args...)	no_printk(fmt, ##args)
#define WL_AMPDU_HWTXS(fmt, args...)	no_printk(fmt, ##args)
#define WL_AMPDU_HWDBG(fmt, args...)	no_printk(fmt, ##args)
#define WL_AMPDU_ERR_ON()       0
#define WL_AMPDU_HW_ON()        0
#define WL_AMPDU_HWTXS_ON()     0

#endif				/* BCMDBG */

#endif				/* _wl_dbg_h_ */
