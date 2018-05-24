/* SPDX-License-Identifier: GPL-2.0-only */
/*
 *  Copyright 2014-2015 Cisco Systems, Inc. and/or its affiliates.
 *  All rights reserved.
 */

#ifndef M00514_SYNCGEN_FLOW_EVCNT_MEMMAP_PACKAGE_H
#define M00514_SYNCGEN_FLOW_EVCNT_MEMMAP_PACKAGE_H

/*******************************************************************
 * Register Block
 * M00514_SYNCGEN_FLOW_EVCNT_MEMMAP_PACKAGE_VHD_REGMAP
 *******************************************************************/
struct m00514_syncgen_flow_evcnt_regmap {
	uint32_t control;                            /* Reg 0x0000, Default=0x0 */
	uint32_t sync_generator_h_sync_length;       /* Reg 0x0004, Default=0x0 */
	uint32_t sync_generator_h_backporch_length;  /* Reg 0x0008, Default=0x0 */
	uint32_t sync_generator_h_active_length;     /* Reg 0x000c, Default=0x0 */
	uint32_t sync_generator_h_frontporch_length; /* Reg 0x0010, Default=0x0 */
	uint32_t sync_generator_v_sync_length;       /* Reg 0x0014, Default=0x0 */
	uint32_t sync_generator_v_backporch_length;  /* Reg 0x0018, Default=0x0 */
	uint32_t sync_generator_v_active_length;     /* Reg 0x001c, Default=0x0 */
	uint32_t sync_generator_v_frontporch_length; /* Reg 0x0020, Default=0x0 */
	uint32_t error_color;                        /* Reg 0x0024, Default=0x0 */
	uint32_t rd_status;                          /* Reg 0x0028 */
	uint32_t rd_evcnt_count;                     /* Reg 0x002c */
};

#define M00514_SYNCGEN_FLOW_EVCNT_REG_CONTROL_OFST 0
#define M00514_SYNCGEN_FLOW_EVCNT_REG_SYNC_GENERATOR_H_SYNC_LENGTH_OFST 4
#define M00514_SYNCGEN_FLOW_EVCNT_REG_SYNC_GENERATOR_H_BACKPORCH_LENGTH_OFST 8
#define M00514_SYNCGEN_FLOW_EVCNT_REG_SYNC_GENERATOR_H_ACTIVE_LENGTH_OFST 12
#define M00514_SYNCGEN_FLOW_EVCNT_REG_SYNC_GENERATOR_H_FRONTPORCH_LENGTH_OFST 16
#define M00514_SYNCGEN_FLOW_EVCNT_REG_SYNC_GENERATOR_V_SYNC_LENGTH_OFST 20
#define M00514_SYNCGEN_FLOW_EVCNT_REG_SYNC_GENERATOR_V_BACKPORCH_LENGTH_OFST 24
#define M00514_SYNCGEN_FLOW_EVCNT_REG_SYNC_GENERATOR_V_ACTIVE_LENGTH_OFST 28
#define M00514_SYNCGEN_FLOW_EVCNT_REG_SYNC_GENERATOR_V_FRONTPORCH_LENGTH_OFST 32
#define M00514_SYNCGEN_FLOW_EVCNT_REG_ERROR_COLOR_OFST 36
#define M00514_SYNCGEN_FLOW_EVCNT_REG_RD_STATUS_OFST 40
#define M00514_SYNCGEN_FLOW_EVCNT_REG_RD_EVCNT_COUNT_OFST 44

/*******************************************************************
 * Bit Mask for register
 * M00514_SYNCGEN_FLOW_EVCNT_MEMMAP_PACKAGE_VHD_BITMAP
 *******************************************************************/
/* control [7:0] */
#define M00514_CONTROL_BITMAP_SYNC_GENERATOR_LOAD_PARAM_OFST (0)
#define M00514_CONTROL_BITMAP_SYNC_GENERATOR_LOAD_PARAM_MSK  (0x1 << M00514_CONTROL_BITMAP_SYNC_GENERATOR_LOAD_PARAM_OFST)
#define M00514_CONTROL_BITMAP_SYNC_GENERATOR_ENABLE_OFST     (1)
#define M00514_CONTROL_BITMAP_SYNC_GENERATOR_ENABLE_MSK      (0x1 << M00514_CONTROL_BITMAP_SYNC_GENERATOR_ENABLE_OFST)
#define M00514_CONTROL_BITMAP_FLOW_CTRL_OUTPUT_ENABLE_OFST   (2)
#define M00514_CONTROL_BITMAP_FLOW_CTRL_OUTPUT_ENABLE_MSK    (0x1 << M00514_CONTROL_BITMAP_FLOW_CTRL_OUTPUT_ENABLE_OFST)
#define M00514_CONTROL_BITMAP_HSYNC_POLARITY_LOW_OFST        (3)
#define M00514_CONTROL_BITMAP_HSYNC_POLARITY_LOW_MSK         (0x1 << M00514_CONTROL_BITMAP_HSYNC_POLARITY_LOW_OFST)
#define M00514_CONTROL_BITMAP_VSYNC_POLARITY_LOW_OFST        (4)
#define M00514_CONTROL_BITMAP_VSYNC_POLARITY_LOW_MSK         (0x1 << M00514_CONTROL_BITMAP_VSYNC_POLARITY_LOW_OFST)
#define M00514_CONTROL_BITMAP_EVCNT_ENABLE_OFST              (5)
#define M00514_CONTROL_BITMAP_EVCNT_ENABLE_MSK               (0x1 << M00514_CONTROL_BITMAP_EVCNT_ENABLE_OFST)
#define M00514_CONTROL_BITMAP_EVCNT_CLEAR_OFST               (6)
#define M00514_CONTROL_BITMAP_EVCNT_CLEAR_MSK                (0x1 << M00514_CONTROL_BITMAP_EVCNT_CLEAR_OFST)
#define M00514_CONTROL_BITMAP_FORMAT_16_BPP_OFST             (7)
#define M00514_CONTROL_BITMAP_FORMAT_16_BPP_MSK              (0x1 << M00514_CONTROL_BITMAP_FORMAT_16_BPP_OFST)
/* error_color [23:0] */
#define M00514_ERROR_COLOR_BITMAP_BLUE_OFST                  (0)
#define M00514_ERROR_COLOR_BITMAP_BLUE_MSK                   (0xff << M00514_ERROR_COLOR_BITMAP_BLUE_OFST)
#define M00514_ERROR_COLOR_BITMAP_GREEN_OFST                 (8)
#define M00514_ERROR_COLOR_BITMAP_GREEN_MSK                  (0xff << M00514_ERROR_COLOR_BITMAP_GREEN_OFST)
#define M00514_ERROR_COLOR_BITMAP_RED_OFST                   (16)
#define M00514_ERROR_COLOR_BITMAP_RED_MSK                    (0xff << M00514_ERROR_COLOR_BITMAP_RED_OFST)
/* rd_status [1:0] */
#define M00514_RD_STATUS_BITMAP_FLOW_CTRL_NO_DATA_ERROR_OFST (0)
#define M00514_RD_STATUS_BITMAP_FLOW_CTRL_NO_DATA_ERROR_MSK  (0x1 << M00514_RD_STATUS_BITMAP_FLOW_CTRL_NO_DATA_ERROR_OFST)
#define M00514_RD_STATUS_BITMAP_READY_BUFFER_FULL_OFST       (1)
#define M00514_RD_STATUS_BITMAP_READY_BUFFER_FULL_MSK        (0x1 << M00514_RD_STATUS_BITMAP_READY_BUFFER_FULL_OFST)

#endif /*M00514_SYNCGEN_FLOW_EVCNT_MEMMAP_PACKAGE_H*/
