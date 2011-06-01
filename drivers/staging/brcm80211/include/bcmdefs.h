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

#ifndef	_bcmdefs_h_
#define	_bcmdefs_h_

#define	SI_BUS			0
#define	PCI_BUS			1
#define	PCMCIA_BUS		2
#define SDIO_BUS		3
#define JTAG_BUS		4
#define USB_BUS			5
#define SPI_BUS			6


#ifndef OFF
#define	OFF	0
#endif

#ifndef ON
#define	ON	1		/* ON = 1 */
#endif

#define	AUTO	(-1)		/* Auto = -1 */

/* Bus types */
#define	SI_BUS			0	/* SOC Interconnect */
#define	PCI_BUS			1	/* PCI target */
#define SDIO_BUS		3	/* SDIO target */
#define JTAG_BUS		4	/* JTAG */
#define USB_BUS			5	/* USB (does not support R/W REG) */
#define SPI_BUS			6	/* gSPI target */
#define RPC_BUS			7	/* RPC target */


/* Defines for DMA Address Width - Shared between OSL and HNDDMA */
#define DMADDR_MASK_32 0x0	/* Address mask for 32-bits */
#define DMADDR_MASK_30 0xc0000000	/* Address mask for 30-bits */
#define DMADDR_MASK_0  0xffffffff	/* Address mask for 0-bits (hi-part) */

#define	DMADDRWIDTH_30  30	/* 30-bit addressing capability */
#define	DMADDRWIDTH_32  32	/* 32-bit addressing capability */
#define	DMADDRWIDTH_63  63	/* 64-bit addressing capability */
#define	DMADDRWIDTH_64  64	/* 64-bit addressing capability */

#ifdef BCMDMA64OSL
typedef struct {
	u32 loaddr;
	u32 hiaddr;
} dma64addr_t;

typedef dma64addr_t dmaaddr_t;
#define PHYSADDRHI(_pa) ((_pa).hiaddr)
#define PHYSADDRHISET(_pa, _val) \
	do { \
		(_pa).hiaddr = (_val);		\
	} while (0)
#define PHYSADDRLO(_pa) ((_pa).loaddr)
#define PHYSADDRLOSET(_pa, _val) \
	do { \
		(_pa).loaddr = (_val);		\
	} while (0)

#else
typedef unsigned long dmaaddr_t;
#define PHYSADDRHI(_pa) (0)
#define PHYSADDRHISET(_pa, _val)
#define PHYSADDRLO(_pa) ((_pa))
#define PHYSADDRLOSET(_pa, _val) \
	do { \
		(_pa) = (_val);			\
	} while (0)
#endif				/* BCMDMA64OSL */

/* One physical DMA segment */
typedef struct {
	dmaaddr_t addr;
	u32 length;
} hnddma_seg_t;

#define MAX_DMA_SEGS 4

typedef struct {
	void *oshdmah;		/* Opaque handle for OSL to store its information */
	uint origsize;		/* Size of the virtual packet */
	uint nsegs;
	hnddma_seg_t segs[MAX_DMA_SEGS];
} hnddma_seg_map_t;

/* packet headroom necessary to accommodate the largest header in the system, (i.e TXOFF).
 * By doing, we avoid the need  to allocate an extra buffer for the header when bridging to WL.
 * There is a compile time check in wlc.c which ensure that this value is at least as big
 * as TXOFF. This value is used in dma_rxfill (hnddma.c).
 */

#define BCMEXTRAHDROOM 172

/* Macros for doing definition and get/set of bitfields
 * Usage example, e.g. a three-bit field (bits 4-6):
 *    #define <NAME>_M	BITFIELD_MASK(3)
 *    #define <NAME>_S	4
 * ...
 *    regval = R_REG(osh, &regs->regfoo);
 *    field = GFIELD(regval, <NAME>);
 *    regval = SFIELD(regval, <NAME>, 1);
 *    W_REG(osh, &regs->regfoo, regval);
 */
#define BITFIELD_MASK(width) \
		(((unsigned)1 << (width)) - 1)
#define GFIELD(val, field) \
		(((val) >> field ## _S) & field ## _M)
#define SFIELD(val, field, bits) \
		(((val) & (~(field ## _M << field ## _S))) | \
		 ((unsigned)(bits) << field ## _S))

/*
 * Priority definitions according 802.1D
 */
#define	PRIO_8021D_NONE		2
#define	PRIO_8021D_BK		1
#define	PRIO_8021D_BE		0
#define	PRIO_8021D_EE		3
#define	PRIO_8021D_CL		4
#define	PRIO_8021D_VI		5
#define	PRIO_8021D_VO		6
#define	PRIO_8021D_NC		7
#define	MAXPRIO			7
#define NUMPRIO			(MAXPRIO + 1)

/* Max. nvram variable table size */
#define	MAXSZ_NVRAM_VARS	4096

/* handle forward declaration */
struct wl_info;
struct wlc_bsscfg;

#define WL_NUMRATES		16	/* max # of rates in a rateset */
typedef struct wl_rateset {
	u32 count;		/* # rates in this set */
	u8 rates[WL_NUMRATES];	/* rates in 500kbps units w/hi bit set if basic */
} wl_rateset_t;

#define WLC_CNTRY_BUF_SZ	4	/* Country string is 3 bytes + NUL */

#define WLC_SET_CHANNEL				30
#define WLC_SET_SRL				32
#define WLC_SET_LRL				34

#define WLC_SET_RATESET				72
#define WLC_SET_BCNPRD				76
#define WLC_GET_CURR_RATESET			114	/* current rateset */
#define WLC_GET_PHYLIST				180

/* Bit masks for radio disabled status - returned by WL_GET_RADIO */
#define WL_RADIO_SW_DISABLE		(1<<0)
#define WL_RADIO_HW_DISABLE		(1<<1)
#define WL_RADIO_MPC_DISABLE		(1<<2)
#define WL_RADIO_COUNTRY_DISABLE	(1<<3)	/* some countries don't support any channel */

/* Override bit for WLC_SET_TXPWR.  if set, ignore other level limits */
#define WL_TXPWR_OVERRIDE	(1U<<31)

/* band types */
#define	WLC_BAND_AUTO		0	/* auto-select */
#define	WLC_BAND_5G		1	/* 5 Ghz */
#define	WLC_BAND_2G		2	/* 2.4 Ghz */
#define	WLC_BAND_ALL		3	/* all bands */

/* Values for PM */
#define PM_OFF	0
#define PM_MAX	1

/* Message levels */
#define WL_ERROR_VAL		0x00000001
#define WL_TRACE_VAL		0x00000002

#define	NFIFO			6	/* # tx/rx fifopairs */

#define PM_OFF	0
#define PM_MAX	1
#define PM_FAST 2

/* band range returned by band_range iovar */
#define WL_CHAN_FREQ_RANGE_2G      0
#define WL_CHAN_FREQ_RANGE_5GL     1
#define WL_CHAN_FREQ_RANGE_5GM     2
#define WL_CHAN_FREQ_RANGE_5GH     3

#endif				/* _bcmdefs_h_ */
