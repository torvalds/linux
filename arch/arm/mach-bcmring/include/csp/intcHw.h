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
*  @file    intcHw.h
*
*  @brief   generic interrupt controller API
*
*  @note
*     None
*/
/****************************************************************************/

#ifndef _INTCHW_H
#define _INTCHW_H

/* ---- Include Files ---------------------------------------------------- */
#include <mach/csp/intcHw_reg.h>

/* ---- Public Constants and Types --------------------------------------- */
/* ---- Public Variable Externs ------------------------------------------ */
/* ---- Public Function Prototypes --------------------------------------- */
static inline void intcHw_irq_disable(void *basep, uint32_t mask);
static inline void intcHw_irq_enable(void *basep, uint32_t mask);

#endif /* _INTCHW_H */

