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

#ifndef _wlc_types_h_
#define _wlc_types_h_

/* Bus types */
#define	SI_BUS			0	/* SOC Interconnect */
#define	PCI_BUS			1	/* PCI target */
#define SDIO_BUS		3	/* SDIO target */
#define JTAG_BUS		4	/* JTAG */
#define USB_BUS			5	/* USB (does not support R/W REG) */
#define SPI_BUS			6	/* gSPI target */
#define RPC_BUS			7	/* RPC target */

#define WL_CHAN_FREQ_RANGE_2G      0
#define WL_CHAN_FREQ_RANGE_5GL     1
#define WL_CHAN_FREQ_RANGE_5GM     2
#define WL_CHAN_FREQ_RANGE_5GH     3

#define MAX_DMA_SEGS 4

#define BCMMSG(dev, fmt, args...)		\
do {						\
	if (brcm_msg_level & LOG_TRACE_VAL)	\
		wiphy_err(dev, "%s: " fmt, __func__, ##args);	\
} while (0)

#define WL_ERROR_ON()		(brcm_msg_level & LOG_ERROR_VAL)

/* register access macros */
#if defined(BCMSDIO)
#ifdef BRCM_FULLMAC
#include <bcmsdh.h>
#endif
#endif

/* register access macros */
#ifndef __BIG_ENDIAN
#ifndef __mips__
#define R_REG(r) \
	({\
		sizeof(*(r)) == sizeof(u8) ? \
		readb((volatile u8*)(r)) : \
		sizeof(*(r)) == sizeof(u16) ? readw((volatile u16*)(r)) : \
		readl((volatile u32*)(r)); \
	})
#else				/* __mips__ */
#define R_REG(r) \
	({ \
		__typeof(*(r)) __osl_v; \
		__asm__ __volatile__("sync"); \
		switch (sizeof(*(r))) { \
		case sizeof(u8): \
			__osl_v = readb((volatile u8*)(r)); \
			break; \
		case sizeof(u16): \
			__osl_v = readw((volatile u16*)(r)); \
			break; \
		case sizeof(u32): \
			__osl_v = \
			readl((volatile u32*)(r)); \
			break; \
		} \
		__asm__ __volatile__("sync"); \
		__osl_v; \
	})
#endif				/* __mips__ */

#define W_REG(r, v) do { \
		switch (sizeof(*(r))) { \
		case sizeof(u8): \
			writeb((u8)(v), (volatile u8*)(r)); break; \
		case sizeof(u16): \
			writew((u16)(v), (volatile u16*)(r)); break; \
		case sizeof(u32): \
			writel((u32)(v), (volatile u32*)(r)); break; \
		}; \
	} while (0)
#else				/* __BIG_ENDIAN */
#define R_REG(r) \
	({ \
		__typeof(*(r)) __osl_v; \
		switch (sizeof(*(r))) { \
		case sizeof(u8): \
			__osl_v = \
			readb((volatile u8*)((r)^3)); \
			break; \
		case sizeof(u16): \
			__osl_v = \
			readw((volatile u16*)((r)^2)); \
			break; \
		case sizeof(u32): \
			__osl_v = readl((volatile u32*)(r)); \
			break; \
		} \
		__osl_v; \
	})

#define W_REG(r, v) do { \
		switch (sizeof(*(r))) { \
		case sizeof(u8):	\
			writeb((u8)(v), \
			(volatile u8*)((r)^3)); break; \
		case sizeof(u16):	\
			writew((u16)(v), \
			(volatile u16*)((r)^2)); break; \
		case sizeof(u32):	\
			writel((u32)(v), \
			(volatile u32*)(r)); break; \
		} \
	} while (0)
#endif				/* __BIG_ENDIAN */

#ifdef __mips__
/*
 * bcm4716 (which includes 4717 & 4718), plus 4706 on PCIe can reorder
 * transactions. As a fix, a read after write is performed on certain places
 * in the code. Older chips and the newer 5357 family don't require this fix.
 */
#define W_REG_FLUSH(r, v)	({ W_REG((r), (v)); (void)R_REG(r); })
#else
#define W_REG_FLUSH(r, v)	W_REG((r), (v))
#endif				/* __mips__ */

#define AND_REG(r, v)	W_REG((r), R_REG(r) & (v))
#define OR_REG(r, v)	W_REG((r), R_REG(r) | (v))

#define SET_REG(r, mask, val) \
		W_REG((r), ((R_REG(r) & ~(mask)) | (val)))

/* forward declarations */
struct sk_buff;
struct brcms_info;
struct wlc_info;
struct wlc_hw_info;
struct wlc_if;
struct brcms_if;
struct ampdu_info;
struct antsel_info;
struct bmac_pmq;
struct d11init;
struct dma_pub;
struct wlc_bsscfg;
struct brcmu_strbuf;
struct si_pub;

/* brcm_msg_level is a bit vector with defs in bcmdefs.h */
extern u32 brcm_msg_level;

#endif				/* _wlc_types_h_ */
