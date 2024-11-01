/* SPDX-License-Identifier: GPL-2.0-only */
/****************************************************************************
 * Driver for Solarflare network controllers and boards
 * Copyright 2019 Solarflare Communications Inc.
 * Copyright 2019-2022 Xilinx, Inc.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published
 * by the Free Software Foundation, incorporated herein by reference.
 */

#ifndef MCDI_PCOL_MAE_H
#define MCDI_PCOL_MAE_H
/* MCDI definitions for Match-Action Engine functionality, that are
 * missing from the main mcdi_pcol.h
 */

/* MC_CMD_MAE_COUNTER_LIST_ALLOC is not (yet) a released API, but the
 * following value is needed as an argument to MC_CMD_MAE_ACTION_SET_ALLOC.
 */
/* enum: A counter ID that is guaranteed never to represent a real counter */
#define          MC_CMD_MAE_COUNTER_LIST_ALLOC_OUT_COUNTER_LIST_ID_NULL 0xffffffff

#endif /* MCDI_PCOL_MAE_H */
