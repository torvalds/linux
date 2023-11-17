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

/*
 * The FWSTS register values are FW defined and can be different between
 * HECI1 and HECI2
 */
#define HECI_FWSTS1(base)				XE_REG((base) + 0xc40)
#define   HECI1_FWSTS1_CURRENT_STATE			REG_GENMASK(3, 0)
#define   HECI1_FWSTS1_CURRENT_STATE_RESET		0
#define   HECI1_FWSTS1_PROXY_STATE_NORMAL		5
#define   HECI1_FWSTS1_INIT_COMPLETE			REG_BIT(9)

#endif
