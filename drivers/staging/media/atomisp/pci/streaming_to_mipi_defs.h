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

#ifndef _streaming_to_mipi_defs_h
#define _streaming_to_mipi_defs_h

#define HIVE_STR_TO_MIPI_VALID_A_BIT 0
#define HIVE_STR_TO_MIPI_VALID_B_BIT 1
#define HIVE_STR_TO_MIPI_SOL_BIT     2
#define HIVE_STR_TO_MIPI_EOL_BIT     3
#define HIVE_STR_TO_MIPI_SOF_BIT     4
#define HIVE_STR_TO_MIPI_EOF_BIT     5
#define HIVE_STR_TO_MIPI_CH_ID_LSB   6

#define HIVE_STR_TO_MIPI_DATA_A_LSB  (HIVE_STR_TO_MIPI_VALID_B_BIT + 1)

#endif /* _streaming_to_mipi_defs_h */
