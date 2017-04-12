/*
 * drxd_firm.h
 *
 * Copyright (C) 2006-2007 Micronas
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * version 2 only, as published by the Free Software Foundation.
 *
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * To obtain the license, point your browser to
 * http://www.gnu.org/copyleft/gpl.html
 */

#ifndef _DRXD_FIRM_H_
#define _DRXD_FIRM_H_

#include <linux/types.h>
#include "drxd_map_firm.h"

#define VERSION_MAJOR 1
#define VERSION_MINOR 4
#define VERSION_PATCH 23

#define HI_TR_FUNC_ADDR HI_IF_RAM_USR_BEGIN__A

#define DRXD_MAX_RETRIES (1000)
#define HI_I2C_DELAY     84
#define HI_I2C_BRIDGE_DELAY   750

#define EQ_TD_TPS_PWR_UNKNOWN          0x00C0	/* Unknown configurations */
#define EQ_TD_TPS_PWR_QPSK             0x016a
#define EQ_TD_TPS_PWR_QAM16_ALPHAN     0x0195
#define EQ_TD_TPS_PWR_QAM16_ALPHA1     0x0195
#define EQ_TD_TPS_PWR_QAM16_ALPHA2     0x011E
#define EQ_TD_TPS_PWR_QAM16_ALPHA4     0x01CE
#define EQ_TD_TPS_PWR_QAM64_ALPHAN     0x019F
#define EQ_TD_TPS_PWR_QAM64_ALPHA1     0x019F
#define EQ_TD_TPS_PWR_QAM64_ALPHA2     0x00F8
#define EQ_TD_TPS_PWR_QAM64_ALPHA4     0x014D

#define DRXD_DEF_AG_PWD_CONSUMER 0x000E
#define DRXD_DEF_AG_PWD_PRO 0x0000
#define DRXD_DEF_AG_AGC_SIO 0x0000

#define DRXD_FE_CTRL_MAX 1023

#define DRXD_OSCDEV_DO_SCAN  (16)

#define DRXD_OSCDEV_DONT_SCAN  (0)

#define DRXD_OSCDEV_STEP  (275)

#define DRXD_SCAN_TIMEOUT    (650)

#define DRXD_BANDWIDTH_8MHZ_IN_HZ  (0x8B8249L)
#define DRXD_BANDWIDTH_7MHZ_IN_HZ  (0x7A1200L)
#define DRXD_BANDWIDTH_6MHZ_IN_HZ  (0x68A1B6L)

#define IRLEN_COARSE_8K       (10)
#define IRLEN_FINE_8K         (10)
#define IRLEN_COARSE_2K       (7)
#define IRLEN_FINE_2K         (9)
#define DIFF_INVALID          (511)
#define DIFF_TARGET           (4)
#define DIFF_MARGIN           (1)

extern u8 DRXD_InitAtomicRead[];
extern u8 DRXD_HiI2cPatch_1[];
extern u8 DRXD_HiI2cPatch_3[];

extern u8 DRXD_InitSC[];

extern u8 DRXD_ResetCEFR[];
extern u8 DRXD_InitFEA2_1[];
extern u8 DRXD_InitFEA2_2[];
extern u8 DRXD_InitCPA2[];
extern u8 DRXD_InitCEA2[];
extern u8 DRXD_InitEQA2[];
extern u8 DRXD_InitECA2[];
extern u8 DRXD_ResetECA2[];
extern u8 DRXD_ResetECRAM[];

extern u8 DRXD_A2_microcode[];
extern u32 DRXD_A2_microcode_length;

extern u8 DRXD_InitFEB1_1[];
extern u8 DRXD_InitFEB1_2[];
extern u8 DRXD_InitCPB1[];
extern u8 DRXD_InitCEB1[];
extern u8 DRXD_InitEQB1[];
extern u8 DRXD_InitECB1[];

extern u8 DRXD_InitDiversityFront[];
extern u8 DRXD_InitDiversityEnd[];
extern u8 DRXD_DisableDiversity[];
extern u8 DRXD_StartDiversityFront[];
extern u8 DRXD_StartDiversityEnd[];

extern u8 DRXD_DiversityDelay8MHZ[];
extern u8 DRXD_DiversityDelay6MHZ[];

extern u8 DRXD_B1_microcode[];
extern u32 DRXD_B1_microcode_length;

#endif
