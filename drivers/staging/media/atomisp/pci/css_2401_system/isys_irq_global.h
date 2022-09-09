/* SPDX-License-Identifier: GPL-2.0 */
/*
 * Support for Intel Camera Imaging ISP subsystem.
 * Copyright (c) 2015, Intel Corporation.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms and conditions of the GNU General Public License,
 * version 2, as published by the Free Software Foundation.
 *
 * This program is distributed in the hope it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 */

#ifndef __ISYS_IRQ_GLOBAL_H__
#define __ISYS_IRQ_GLOBAL_H__

#if defined(ISP2401)

/* Register offset/index from base location */
#define ISYS_IRQ_EDGE_REG_IDX		(0)
#define ISYS_IRQ_MASK_REG_IDX		(ISYS_IRQ_EDGE_REG_IDX + 1)
#define ISYS_IRQ_STATUS_REG_IDX		(ISYS_IRQ_EDGE_REG_IDX + 2)
#define ISYS_IRQ_CLEAR_REG_IDX		(ISYS_IRQ_EDGE_REG_IDX + 3)
#define ISYS_IRQ_ENABLE_REG_IDX		(ISYS_IRQ_EDGE_REG_IDX + 4)
#define ISYS_IRQ_LEVEL_NO_REG_IDX	(ISYS_IRQ_EDGE_REG_IDX + 5)

/* Register values */
#define ISYS_IRQ_MASK_REG_VALUE		(0xFFFF)
#define ISYS_IRQ_CLEAR_REG_VALUE	(0xFFFF)
#define ISYS_IRQ_ENABLE_REG_VALUE	(0xFFFF)

#endif /* defined(ISP2401) */

#endif	/* __ISYS_IRQ_GLOBAL_H__ */
