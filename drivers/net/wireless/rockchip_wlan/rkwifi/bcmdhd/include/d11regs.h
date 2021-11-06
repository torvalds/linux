/*
 * Chip-specific hardware definitions for
 * Broadcom 802.11abg Networking Device Driver
 *
 * Broadcom Proprietary and Confidential. Copyright (C) 2020,
 * All Rights Reserved.
 *
 * This is UNPUBLISHED PROPRIETARY SOURCE CODE of Broadcom;
 * the contents of this file may not be disclosed to third parties,
 * copied or duplicated in any form, in whole or in part, without
 * the prior written permission of Broadcom.
 *
 *
 * <<Broadcom-WL-IPTag/Proprietary:>>
 */

#ifndef	_D11REGS_H
#define	_D11REGS_H

#include <typedefs.h>
#include <sbhndpio.h>
#include <sbhnddma.h>
#include <sbconfig.h>

#if !defined(BCMDONGLEHOST)
#include <dot11mac_all_regs.h>
#include <d11regs_comp.h>
#endif

#if defined(BCMDONGLEHOST) || defined(WL_UNITTEST)
typedef struct {
	uint32 pad;
} d11regdefs_t;

typedef volatile uint8 d11regs_t;
typedef struct _d11regs_info {
	uint32 pad;
} d11regs_info_t;

#else	/* defined(BCMDONGLEHOST) || defined(WL_UNITTEST) */

typedef volatile struct d11regs d11regs_t;

typedef struct _d11regs_info {
	d11regs_t *regs;
} d11regs_info_t;

#endif /* !defined(BCMDONGLEHOST) || !defined(WL_UNITTEST) */

typedef volatile struct {
	uint32	intstatus;
	uint32	intmask;
} intctrlregs_t;

/**
 * read: 32-bit register that can be read as 32-bit or as 2 16-bit
 * write: only low 16b-it half can be written
 */
typedef volatile union {
	uint32 pmqhostdata;		/**< read only! */
	struct {
		uint16 pmqctrlstatus;	/**< read/write */
		uint16 PAD;
	} w;
} pmqreg_t;

/** dma corerev >= 11 */
typedef volatile struct {
	dma64regs_t	dmaxmt;		/* dma tx */
	pio4regs_t	piotx;		/* pio tx */
	dma64regs_t	dmarcv;		/* dma rx */
	pio4regs_t	piorx;		/* pio rx */
} fifo64_t;

/** indirect dma corerev >= 64 */
typedef volatile struct {
	dma64regs_t	dma;		/**< dma tx */
	uint32		indintstatus;
	uint32		indintmask;
} ind_dma_t;

/** indirect dma corerev 80, 81, 82 */
typedef volatile struct {
	uint32		indintstatus;
	uint32		indintmask;
	dma64regs_t	dma;		/**< dma tx, */
} ind_dma_axc_t;

/* access to register offsets and fields defined in dot11mac_all_regs.h */

#define D11_REG_OFF(regname) \
	dot11mac_##regname##_ADDR
#define D11_REG_FIELD_MASK(regname, regfield) \
	dot11mac_##regname##__##regfield##_MASK
#define D11_REG_FIELD_SHIFT(regname, regfield) \
	dot11mac_##regname##__##regfield##_SHIFT

/* convert register offset to backplane address */

#ifndef D11_REG_ADDR_CHK
// #define D11_REG_ADDR_CHK
#endif

#ifdef D11_REG_ADDR_CHK
#define D11_REG_ADDR_EXEMPT(regname) \
	(D11_REG_OFF(regname) == D11_REG_OFF(PHY_REG_ADDR) || \
	 D11_REG_OFF(regname) == D11_REG_OFF(radioregaddr) || \
	 D11_REG_OFF(regname) == D11_REG_OFF(radioregdata) || \
	 D11_REG_OFF(regname) == D11_REG_OFF(OBJ_DATA) || \
	0)
#define D11_REG32_ADDR(regbase, regname) \
	({ \
	STATIC_ASSERT(D11_REG_ADDR_EXEMPT(regname) || D11_REG_OFF(regname) < 0x3e0); \
	(volatile uint32 *)((uintptr)(regbase) + D11_REG_OFF(regname)); \
	})
#define D11_REG16_ADDR(regbase, regname) \
	({ \
	STATIC_ASSERT(D11_REG_ADDR_EXEMPT(regname) || D11_REG_OFF(regname) >= 0x3e0); \
	(volatile uint16 *)((uintptr)(regbase) + D11_REG_OFF(regname)); \
	})
#else /* !D11_REG_ADDR_CHK */
#define D11_REG32_ADDR(regbase, regname) \
	(volatile uint32 *)((uintptr)(regbase) + D11_REG_OFF(regname))
#define D11_REG16_ADDR(regbase, regname) \
	(volatile uint16 *)((uintptr)(regbase) + D11_REG_OFF(regname))
#endif /* !D11_REG_ADDR_CHK */

/* used in table */
#define D11_REG32_ADDR_ENTRY(regbase, regname) \
	(volatile uint32 *)((uintptr)(regbase) + D11_REG_OFF(regname))
#define D11_REG16_ADDR_ENTRY(regbase, regname) \
	(volatile uint16 *)((uintptr)(regbase) + D11_REG_OFF(regname))

#ifndef D11_NEW_ACCESS_MACROS
/* MOVED TO src/wl/sys/wlc_hw_priv.h */
#define GET_MACINTSTATUS(osh, hw)		R_REG((osh), D11_MACINTSTATUS(hw))
#define SET_MACINTSTATUS(osh, hw, val)		W_REG((osh), D11_MACINTSTATUS(hw), (val))
#define GET_MACINTMASK(osh, hw)			R_REG((osh), D11_MACINTMASK(hw))
#define SET_MACINTMASK(osh, hw, val)		W_REG((osh), D11_MACINTMASK(hw), (val))

#define GET_MACINTSTATUS_X(osh, hw)		R_REG((osh), D11_MACINTSTATUS_psmx(hw))
#define SET_MACINTSTATUS_X(osh, hw, val)	W_REG((osh), D11_MACINTSTATUS_psmx(hw), (val))
#define GET_MACINTMASK_X(osh, hw)		R_REG((osh), D11_MACINTMASK_psmx(hw))
#define SET_MACINTMASK_X(osh, hw, val)		W_REG((osh), D11_MACINTMASK_psmx(hw), (val))

#define GET_MACINTSTATUS_EXT(osh, hw)		R_REG((osh), D11_MACINTSTATUS_EXT(hw))
#define SET_MACINTSTATUS_EXT(osh, hw, val)	W_REG((osh), D11_MACINTSTATUS_EXT(hw), (val))
#define GET_MACINTMASK_EXT(osh, hw)		R_REG((osh), D11_MACINTMASK_EXT(hw))
#define SET_MACINTMASK_EXT(osh, hw, val)	W_REG((osh), D11_MACINTMASK_EXT(hw), (val))

#define GET_MACINTSTATUS_EXT_X(osh, hw)		R_REG((osh), D11_MACINTSTATUS_EXT_psmx(hw))
#define SET_MACINTSTATUS_EXT_X(osh, hw, val)	W_REG((osh), D11_MACINTSTATUS_EXT_psmx(hw), (val))
#define GET_MACINTMASK_EXT_X(osh, hw)		R_REG((osh), D11_MACINTMASK_EXT_psmx(hw))
#define SET_MACINTMASK_EXT_X(osh, hw, val)	W_REG((osh), D11_MACINTMASK_EXT_psmx(hw), (val))

#define D11Reggrp_intctrlregs(hw, ix) ((intctrlregs_t*)(((volatile uint8*)D11_intstat0(hw)) + \
	(sizeof(intctrlregs_t)*ix)))
#define D11Reggrp_inddma(hw, ix) (D11REV_GE(hw->corerev, 86) ? \
	((ind_dma_t*)(((volatile uint8*)D11_ind_xmt_control(hw)) + (sizeof(ind_dma_t)*ix))) : \
	((ind_dma_t*)(((volatile uint8*)D11_inddma(hw)) + (sizeof(ind_dma_t)*ix))))
#define D11Reggrp_inddma_axc(hw, ix) ((ind_dma_axc_t*)(((volatile uint8*)D11_inddma(hw)) + \
		(sizeof(ind_dma_axc_t)*ix)))
#define D11Reggrp_indaqm(hw, ix) (D11REV_GE(hw->corerev, 86) ? \
	((ind_dma_t*)(((volatile uint8*)D11_IndAQMctl(hw)) + (sizeof(ind_dma_t)*ix))) : \
	((ind_dma_t*)(((volatile uint8*)D11_indaqm(hw)) + (sizeof(ind_dma_t)*ix))))
#define D11Reggrp_pmqreg(hw, ix) ((pmqreg_t*)(((volatile uint8*)D11_PMQHOSTDATA(hw)) + \
	(sizeof(pmqreg_t)*ix)))
#define D11Reggrp_f64regs(hw, ix) ((fifo64_t*)(((volatile uint8*)D11_xmt0ctl(hw)) + \
	(sizeof(fifo64_t)*ix)))
#define D11Reggrp_dmafifo(hw, ix) ((dma32diag_t*)(((volatile uint8*)D11_fifobase(hw)) + \
	(sizeof(dma32diag_t)*ix)))
#define D11Reggrp_intrcvlazy(hw, ix) ((volatile uint32*)(((volatile uint8*)D11_intrcvlzy0(hw)) + \
	(sizeof(uint32)*ix)))
#define D11Reggrp_altintmask(hw, ix) ((volatile uint32*)(((volatile uint8*)D11_alt_intmask0(hw)) + \
	(sizeof(uint32)*ix)))
#define D11REG_ISVALID(ptr, addr) ((volatile uint16 *)(addr) != \
	((volatile uint16 *) &((ptr)->regs->INVALID_ID)))
#endif /* D11_NEW_ACCESS_MACROS */

#endif	/* _D11REGS_H */
