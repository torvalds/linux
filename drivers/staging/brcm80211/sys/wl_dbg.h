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

#define WL_PRINT(args)		printf args
#define WL_NONE(args)

#ifdef BCMDBG

#define	WL_ERROR(args)		do {if ((wl_msg_level & WL_ERROR_VAL)) WL_PRINT(args); } while (0)
#define	WL_TRACE(args)		do {if (wl_msg_level & WL_TRACE_VAL) WL_PRINT(args); } while (0)
#define WL_AMPDU(args)		do {if (wl_msg_level & WL_AMPDU_VAL) WL_PRINT(args); } while (0)
#define WL_FFPLD(args)		do {if (wl_msg_level & WL_FFPLD_VAL) WL_PRINT(args); } while (0)

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

#define WL_AMPDU_UPDN(args) do {if (wl_ampdu_dbg & WL_AMPDU_UPDN_VAL) {WL_AMPDU(args); } } while (0)
#define WL_AMPDU_RX(args) do {if (wl_ampdu_dbg & WL_AMPDU_RX_VAL) {WL_AMPDU(args); } } while (0)
#define WL_AMPDU_ERR(args) do {if (wl_ampdu_dbg & WL_AMPDU_ERR_VAL) {WL_AMPDU(args); } } while (0)
#define WL_AMPDU_TX(args) do {if (wl_ampdu_dbg & WL_AMPDU_TX_VAL) {WL_AMPDU(args); } } while (0)
#define WL_AMPDU_CTL(args) do {if (wl_ampdu_dbg & WL_AMPDU_CTL_VAL) {WL_AMPDU(args); } } while (0)
#define WL_AMPDU_HW(args) do {if (wl_ampdu_dbg & WL_AMPDU_HW_VAL) {WL_AMPDU(args); } } while (0)
#define WL_AMPDU_HWTXS(args) do {if (wl_ampdu_dbg & WL_AMPDU_HWTXS_VAL) {WL_AMPDU(args); } } while (0)
#define WL_AMPDU_HWDBG(args) do {if (wl_ampdu_dbg & WL_AMPDU_HWDBG_VAL) {WL_AMPDU(args); } } while (0)
#define WL_AMPDU_ERR_ON() (wl_ampdu_dbg & WL_AMPDU_ERR_VAL)
#define WL_AMPDU_HW_ON() (wl_ampdu_dbg & WL_AMPDU_HW_VAL)
#define WL_AMPDU_HWTXS_ON() (wl_ampdu_dbg & WL_AMPDU_HWTXS_VAL)

#else				/* BCMDBG */

#define	WL_ERROR(args)
#define	WL_TRACE(args)
#define WL_AMPDU(args)
#define WL_FFPLD(args)

#define WL_ERROR_ON()		0

#define WL_AMPDU_UPDN(args)
#define WL_AMPDU_RX(args)
#define WL_AMPDU_ERR(args)
#define WL_AMPDU_TX(args)
#define WL_AMPDU_CTL(args)
#define WL_AMPDU_HW(args)
#define WL_AMPDU_HWTXS(args)
#define WL_AMPDU_HWDBG(args)
#define WL_AMPDU_ERR_ON()       0
#define WL_AMPDU_HW_ON()        0
#define WL_AMPDU_HWTXS_ON()     0

#endif				/* BCMDBG */

#endif				/* _wl_dbg_h_ */
