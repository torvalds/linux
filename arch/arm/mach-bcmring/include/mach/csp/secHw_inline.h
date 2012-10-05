/*****************************************************************************
* Copyright 2003 - 2008 Broadcom Corporation.  All rights reserved.
*
* Unless you and Broadcom execute a separate written software license
* agreement governing use of this software, this software is licensed to you
* under the terms of the GNU General Public License version 2, available at
* http://www.broadcom.com/licenses/GPLv2.php (the "GPL").
*
* Notwithstanding the above, under no circumstances may you combine this
* software in any way with any other Broadcom software provided under a
* license other than the GPL, without Broadcom's express prior written
* consent.
*****************************************************************************/

/****************************************************************************/
/**
*  @file    secHw_inline.h
*
*  @brief   Definitions for configuring/testing secure blocks
*
*  @note
*     None
*/
/****************************************************************************/

#ifndef SECHW_INLINE_H
#define SECHW_INLINE_H

/****************************************************************************/
/**
*  @brief  Configures a device as a secure device
*
*/
/****************************************************************************/
static inline void secHw_setSecure(uint32_t mask	/*  mask of type secHw_BLK_MASK_XXXXXX */
    ) {
	secHw_REGS_t __iomem *regp = MM_IO_BASE_TZPC;

	if (mask & 0x0000FFFF) {
		regp->reg[secHw_IDX_LS].setSecure = mask & 0x0000FFFF;
	}

	if (mask & 0xFFFF0000) {
		regp->reg[secHw_IDX_MS].setSecure = mask >> 16;
	}
}

/****************************************************************************/
/**
*  @brief  Configures a device as a non-secure device
*
*/
/****************************************************************************/
static inline void secHw_setUnsecure(uint32_t mask	/*  mask of type secHw_BLK_MASK_XXXXXX */
    ) {
	secHw_REGS_t __iomem *regp = MM_IO_BASE_TZPC;

	if (mask & 0x0000FFFF) {
		writel(mask & 0x0000FFFF, &regp->reg[secHw_IDX_LS].setUnsecure);
	}
	if (mask & 0xFFFF0000) {
		writel(mask >> 16, &regp->reg[secHw_IDX_MS].setUnsecure);
	}
}

/****************************************************************************/
/**
*  @brief  Get the trustzone status for all components. 1 = non-secure, 0 = secure
*
*/
/****************************************************************************/
static inline uint32_t secHw_getStatus(void)
{
	secHw_REGS_t __iomem *regp = MM_IO_BASE_TZPC;

	return (regp->reg[1].status << 16) + regp->reg[0].status;
}

#endif /* SECHW_INLINE_H */
