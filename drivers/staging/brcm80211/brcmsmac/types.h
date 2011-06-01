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

#ifndef _BRCM_TYPES_H_
#define _BRCM_TYPES_H_

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

/* boardflags */
#define	BFL_PACTRL		0x00000002	/* Board has gpio 9 controlling the PA */
#define	BFL_NOPLLDOWN		0x00000020	/* Not ok to power down the chip pll and oscillator */
#define BFL_FEM			0x00000800	/* Board supports the Front End Module */
#define BFL_EXTLNA		0x00001000	/* Board has an external LNA in 2.4GHz band */
#define BFL_NOPA		0x00010000	/* Board has no PA */
#define BFL_BUCKBOOST		0x00200000	/* Power topology uses BUCKBOOST */
#define BFL_FEM_BT		0x00400000	/* Board has FEM and switch to share antenna w/ BT */
#define BFL_NOCBUCK		0x00800000	/* Power topology doesn't use CBUCK */
#define BFL_PALDO		0x02000000	/* Power topology uses PALDO */
#define BFL_EXTLNA_5GHz		0x10000000	/* Board has an external LNA in 5GHz band */

/* boardflags2 */
#define BFL2_RXBB_INT_REG_DIS	0x00000001	/* Board has an external rxbb regulator */
#define BFL2_APLL_WAR		0x00000002	/* Flag to implement alternative A-band PLL settings */
#define BFL2_TXPWRCTRL_EN	0x00000004	/* Board permits enabling TX Power Control */
#define BFL2_2X4_DIV		0x00000008	/* Board supports the 2X4 diversity switch */
#define BFL2_5G_PWRGAIN		0x00000010	/* Board supports 5G band power gain */
#define BFL2_PCIEWAR_OVR	0x00000020	/* Board overrides ASPM and Clkreq settings */
#define BFL2_LEGACY		0x00000080
#define BFL2_SKWRKFEM_BRD	0x00000100	/* 4321mcm93 board uses Skyworks FEM */
#define BFL2_SPUR_WAR		0x00000200	/* Board has a WAR for clock-harmonic spurs */
#define BFL2_GPLL_WAR		0x00000400	/* Flag to narrow G-band PLL loop b/w */
#define BFL2_SINGLEANT_CCK	0x00001000	/* Tx CCK pkts on Ant 0 only */
#define BFL2_2G_SPUR_WAR	0x00002000	/* WAR to reduce and avoid clock-harmonic spurs in 2G */
#define BFL2_GPLL_WAR2	        0x00010000	/* Flag to widen G-band PLL loop b/w */
#define BFL2_IPALVLSHIFT_3P3    0x00020000
#define BFL2_INTERNDET_TXIQCAL  0x00040000	/* Use internal envelope detector for TX IQCAL */
#define BFL2_XTALBUFOUTEN       0x00080000	/* Keep the buffered Xtal output from radio "ON"
						 * Most drivers will turn it off without this flag
						 * to save power.
						 */

/* board specific GPIO assignment, gpio 0-3 are also customer-configurable led */
#define	BOARD_GPIO_PACTRL	0x200	/* bit 9 controls the PA on new 4306 boards */
#define BOARD_GPIO_12		0x1000	/* gpio 12 */
#define BOARD_GPIO_13		0x2000	/* gpio 13 */

#define BCMMSG(dev, fmt, args...)		\
do {						\
	if (brcm_msg_level & LOG_TRACE_VAL)	\
		wiphy_err(dev, "%s: " fmt, __func__, ##args);	\
} while (0)

#define WL_ERROR_ON()		(brcm_msg_level & LOG_ERROR_VAL)

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

/* brcm_msg_level is a bit vector with defs in defs.h */
extern u32 brcm_msg_level;

#endif				/* _BRCM_TYPES_H_ */
