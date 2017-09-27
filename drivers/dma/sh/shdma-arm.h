/*
 * Renesas SuperH DMA Engine support
 *
 * Copyright (C) 2013 Renesas Electronics, Inc.
 *
 * This is free software; you can redistribute it and/or modify it under the
 * terms of version 2 the GNU General Public License as published by the Free
 * Software Foundation.
 */

#ifndef SHDMA_ARM_H
#define SHDMA_ARM_H

#include "shdma.h"

/* Transmit sizes and respective CHCR register values */
enum {
	XMIT_SZ_8BIT		= 0,
	XMIT_SZ_16BIT		= 1,
	XMIT_SZ_32BIT		= 2,
	XMIT_SZ_64BIT		= 7,
	XMIT_SZ_128BIT		= 3,
	XMIT_SZ_256BIT		= 4,
	XMIT_SZ_512BIT		= 5,
};

/* log2(size / 8) - used to calculate number of transfers */
#define SH_DMAE_TS_SHIFT {		\
	[XMIT_SZ_8BIT]		= 0,	\
	[XMIT_SZ_16BIT]		= 1,	\
	[XMIT_SZ_32BIT]		= 2,	\
	[XMIT_SZ_64BIT]		= 3,	\
	[XMIT_SZ_128BIT]	= 4,	\
	[XMIT_SZ_256BIT]	= 5,	\
	[XMIT_SZ_512BIT]	= 6,	\
}

#define TS_LOW_BIT	0x3 /* --xx */
#define TS_HI_BIT	0xc /* xx-- */

#define TS_LOW_SHIFT	(3)
#define TS_HI_SHIFT	(20 - 2)	/* 2 bits for shifted low TS */

#define TS_INDEX2VAL(i) \
	((((i) & TS_LOW_BIT) << TS_LOW_SHIFT) |\
	 (((i) & TS_HI_BIT)  << TS_HI_SHIFT))

#define CHCR_TX(xmit_sz) (DM_FIX | SM_INC | RS_ERS | TS_INDEX2VAL((xmit_sz)))
#define CHCR_RX(xmit_sz) (DM_INC | SM_FIX | RS_ERS | TS_INDEX2VAL((xmit_sz)))

#endif
