/* SPDX-License-Identifier: MIT */
/*
 * Copyright © 2023 Intel Corporation
 */

#ifndef _XE_GSC_REGS_H_
#define _XE_GSC_REGS_H_

#include <linux/compiler.h>
#include <linux/types.h>

#include "regs/xe_reg_defs.h"

/* Definitions of GSC H/W registers, bits, etc */

#define MTL_GSC_HECI1_BASE	0x00116000
#define MTL_GSC_HECI2_BASE	0x00117000

#define HECI_H_CSR(base)	XE_REG((base) + 0x4)
#define   HECI_H_CSR_IE		REG_BIT(0)
#define   HECI_H_CSR_IS		REG_BIT(1)
#define   HECI_H_CSR_IG		REG_BIT(2)
#define   HECI_H_CSR_RDY	REG_BIT(3)
#define   HECI_H_CSR_RST	REG_BIT(4)

/*
 * The FWSTS register values are FW defined and can be different between
 * HECI1 and HECI2
 */
#define HECI_FWSTS1(base)				XE_REG((base) + 0xc40)
#define   HECI1_FWSTS1_CURRENT_STATE			REG_GENMASK(3, 0)
#define   HECI1_FWSTS1_CURRENT_STATE_RESET		0
#define   HECI1_FWSTS1_PROXY_STATE_NORMAL		5
#define   HECI1_FWSTS1_INIT_COMPLETE			REG_BIT(9)
#define HECI_FWSTS2(base)				XE_REG((base) + 0xc48)
#define HECI_FWSTS3(base)				XE_REG((base) + 0xc60)
#define HECI_FWSTS4(base)				XE_REG((base) + 0xc64)
#define HECI_FWSTS5(base)				XE_REG((base) + 0xc68)
#define   HECI1_FWSTS5_HUC_AUTH_DONE			REG_BIT(19)
#define HECI_FWSTS6(base)				XE_REG((base) + 0xc6c)

#define HECI_H_GS1(base)	XE_REG((base) + 0xc4c)
#define   HECI_H_GS1_ER_PREP	REG_BIT(0)

#define GSCI_TIMER_STATUS				XE_REG(0x11ca28)
#define   GSCI_TIMER_STATUS_VALUE			REG_GENMASK(1, 0)
#define   GSCI_TIMER_STATUS_RESET_IN_PROGRESS		0
#define   GSCI_TIMER_STATUS_TIMER_EXPIRED		1
#define   GSCI_TIMER_STATUS_RESET_COMPLETE		2
#define   GSCI_TIMER_STATUS_OUT_OF_RESET		3

#endif
