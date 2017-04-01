/*
 * Copyright (C) 2016 Fuzhou Rockchip Electronics Co., Ltd
 * author: hehua,hh@rock-chips.com
 * lixinhuang, buluess.li@rock-chips.com
 *
 * This software is licensed under the terms of the GNU General Public
 * License version 2, as published by the Free Software Foundation, and
 * may be copied, distributed, and modified under those terms.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 */

#ifndef _MPP_DEV_H265E_REGISTER_H_
#define _MPP_DEV_H265E_REGISTER_H_

#define H265E_PO_CONF                        0x0000
#define H265E_VCPU_CUR_PC                    0x0004
#define H265E_VPU_PDBG_CTRL                  0x0010
#define H265E_VPU_PDBG_IDX_REG               0x0014
#define H265E_VPU_PDBG_WDATA_REG             0x0018
#define H265E_VPU_PDBG_RDATA_REG             0x001C
#define H265E_VPU_FIO_CTRL_ADDR              0x0020
#define H265E_VPU_FIO_DATA                   0x0024
#define H265E_VPU_VINT_REASON_USR            0x0030
#define H265E_VPU_VINT_REASON_CLR            0x0034
#define H265E_VPU_HOST_INT_REQ               0x0038
#define H265E_VPU_VINT_CLEAR                 0x003C
#define H265E_VPU_HINT_CLEAR                 0x0040
#define H265E_VPU_VPU_INT_STS                0x0044
#define H265E_VPU_VINT_ENABLE                0x0048

#define H265E_CMD_REG_END                    0x0200

#define H265E_VPU_VINT_REASON                0x004c
#define H265E_VPU_RESET_REQ                  0x0050
#define H265E_VPU_RESET_STATUS               0x0070
#define H265E_VPU_REMAP_CTRL                 0x0060
#define H265E_VPU_REMAP_VADDR                0x0064
#define H265E_VPU_REMAP_PADDR                0x0068
#define H265E_VPU_REMAP_CORE_START           0x006C
#define H265E_VPU_BUSY_STATUS                0x0070
#define H265E_COMMAND                        0x0100
#define H265E_CORE_INDEX                     0x0104
#define H265E_INST_INDEX                     0x0108
#define H265E_ENC_SET_PARAM_OPTION           0x010C
#define H265E_RET_FW_VERSION                 0x0118
#define H265E_ADDR_CODE_BASE                 0x0118
#define H265E_CODE_SIZE			     0x011C
#define H265E_CODE_PARAM                     0x0120
#define H265E_HW_OPTION                      0x0124

#define H265E_RET_SUCCESS                    0x0110
#define H265E_VPU_HOST_INT_REQ               0x0038
#define H265E_SFB_OPTION                     0x010C
#define H265E_RET_FAIL_REASON                0x0114
#define H265E_BS_START_ADDR                  0x0120
#define H265E_COMMON_PIC_INFO                0x0120
#define H265E_BS_SIZE                        0x0124
#define H265E_PIC_SIZE                       0x0124
#define H265E_BS_PARAM                       0x0128
#define H265E_SET_FB_NUM                     0x0128
#define H265E_BS_OPTION                      0x012C
#define H265E_BS_RD_PTR                      0x0130
#define H265E_BS_WR_PTR                      0x0134
#define H265E_ADDR_WORK_BASE                 0x0138
#define H265E_WORK_SIZE                      0x013c
#define H265E_WORK_PARAM                     0x0140
#define H265E_ADDR_TEMP_BASE                 0x0144
#define H265E_TEMP_SIZE                      0x0148
#define H265E_TEMP_PARAM                     0x014C
#define H265E_FBC_STRIDE                     0x0154
#define H265E_ENC_SET_PARAM_ENABLE           0x015C
#define H265E_ENC_SEQ_SRC_SIZE               0x0160
#define H265E_ADDR_LUMA_BASE0                0x0160
#define H265E_ADDR_CB_BASE0                  0x0164
#define H265E_ADDR_CR_BASE0                  0x0168
#define H265E_ADDR_FBC_Y_OFFSET0             0x0168
#define H265E_ADDR_FBC_C_OFFSET0             0x016C

#define H265E_ENC_SEQ_PARAM                  0x016C
#define H265E_ENC_SEQ_GOP_PARAM              0x0170
#define H265E_ENC_SRC_PIC_IDX                0x0170
#define H265E_ENC_SEQ_INTRA_PARAM            0x0174
#define H265E_ENC_SEQ_CONF_WIN_TOP_BOT       0x0178
#define H265E_ENC_SEQ_CONF_WIN_LEFT_RIGHT    0x017C
#define H265E_ENC_SEQ_FRAME_RATE             0x0180
#define H265E_ENC_SEQ_INDEPENDENT_SLICE      0x0184
#define H265E_ENC_SEQ_DEPENDENT_SLICE        0x0188

#define H265E_ENC_SEQ_INTRA_REFRESH          0x018C
#define H265E_ENC_PARAM                      0x0190
#define H265E_ENC_RC_INTRA_MIN_MAX_QP        0x0194
#define H265E_ENC_RC_PARAM                   0x0198
#define H265E_ENC_RC_MIN_MAX_QP              0x019C
#define H265E_ENC_RC_BIT_RATIO_LAYER_0_3     0x01A0
#define H265E_ENC_RC_BIT_RATIO_LAYER_4_7     0x01A4
#define H265E_ENC_NR_PARAM                   0x01A8
#define H265E_ENC_NR_WEIGHT                  0x01AC
#define H265E_ENC_NUM_UNITS_IN_TICK          0x01B0
#define H265E_ENC_TIME_SCALE                 0x01B4
#define H265E_ENC_NUM_TICKS_POC_DIFF_ONE     0x01B8
#define H265E_ENC_RC_TRANS_RATE              0x01BC
#define H265E_ENC_RC_TARGET_RATE             0x01C0
#define H265E_ENC_ROT_PARAM                  0x01C4
#define H265E_ENC_ROT_RESERVED               0x01C8
#define H265E_RET_ENC_MIN_FB_NUM             0x01CC
#define H265E_RET_ENC_NAL_INFO_TO_BE_ENCODED 0x01D0
#define H265E_RET_ENC_MIN_SRC_BUF_NUM        0x01D8

#define H265E_ADDR_MV_COL0                   0x01E0
#define H265E_ADDR_MV_COL1                   0x01E4
#define H265E_ADDR_MV_COL2                   0x01E8
#define H265E_ADDR_MV_COL3                   0x01EC
#define H265E_ADDR_MV_COL4                   0x01F0
#define H265E_ADDR_MV_COL5                   0x01F4
#define H265E_ADDR_MV_COL6                   0x01F8
#define H265E_ADDR_MV_COL7                   0x01FC

#define H265E_ADDR_SEC_AXI_BASE              0x150
#define H265E_SEC_AXI_SIZE                   0x154
#define H265E_USE_SEC_AXI                    0x158

/************************************************************************/
/*H265 ENCODER - SET_PARAM + CUSTOM_GOP                                 */
/************************************************************************/
#define H265E_ENC_SET_CUSTOM_GOP_ENABLE      0x015C
#define H265E_ENC_CUSTOM_GOP_PARAM           0x0160
#define H265E_ENC_CUSTOM_GOP_PIC_PARAM_0     0x0164
#define H265E_ENC_CUSTOM_GOP_PIC_PARAM_1     0x0168
#define H265E_ENC_CUSTOM_GOP_PIC_PARAM_2     0x016C
#define H265E_ENC_CUSTOM_GOP_PIC_PARAM_3     0x0170
#define H265E_ENC_CUSTOM_GOP_PIC_PARAM_4     0x0174
#define H265E_ENC_CUSTOM_GOP_PIC_PARAM_5     0x0178
#define H265E_ENC_CUSTOM_GOP_PIC_PARAM_6     0x017C
#define H265E_ENC_CUSTOM_GOP_PIC_PARAM_7     0x0180
#define H265E_ENC_CUSTOM_GOP_RESERVED        0x0184
#define H265E_ENC_CUSTOM_GOP_PIC_LAMBDA_0    0x0188
#define H265E_ENC_CUSTOM_GOP_PIC_LAMBDA_1    0x018C
#define H265E_ENC_CUSTOM_GOP_PIC_LAMBDA_2    0x0190
#define H265E_ENC_CUSTOM_GOP_PIC_LAMBDA_3    0x0194
#define H265E_ENC_CUSTOM_GOP_PIC_LAMBDA_4    0x0198
#define H265E_ENC_CUSTOM_GOP_PIC_LAMBDA_5    0x019C
#define H265E_ENC_CUSTOM_GOP_PIC_LAMBDA_6    0x01A0
#define H265E_ENC_CUSTOM_GOP_PIC_LAMBDA_7    0x01A4

/************************************************************************/
/* H265 ENCODER - SET_PARAM + VUI                                       */
/************************************************************************/
#define H265E_ENC_VUI_PARAM_FLAGS            0x015C
#define H265E_ENC_VUI_ASPECT_RATIO_IDC       0x0160
#define H265E_ENC_VUI_SAR_SIZE               0x0164
#define H265E_ENC_VUI_OVERSCAN_APPROPRIATE   0x0168
#define H265E_ENC_VUI_VIDEO_SIGNAL           0x016C
#define H265E_ENC_VUI_CHROMA_SAMPLE_LOC      0x0170
#define H265E_ENC_VUI_DISP_WIN_LEFT_RIGHT    0x0174
#define H265E_ENC_VUI_DISP_WIN_TOP_BOT       0x0178

#define H265E_ENC_VUI_HRD_RBSP_PARAM_FLAG    0x017C
#define H265E_ENC_VUI_RBSP_ADDR              0x0180
#define H265E_ENC_VUI_RBSP_SIZE              0x0184
#define H265E_ENC_HRD_RBSP_ADDR              0x0188
#define H265E_ENC_HRD_RBSP_SIZE              0x018C

/************************************************************************/
/* H265 ENCODER - SET_FRAMEBUF                                          */
/************************************************************************/
#define H265E_FBC_STRIDE_Y                   0x150
#define H265E_FBC_STRIDE_C                   0x154
/* 1/4 sub-sampled buffer (for S2 ME)
 *      SUB_SAMPLED_ONE_FB_SIZE = ALIGN16(width/4) * ALIGN8(height/4)
 *      total size for sub-sampled buffer = SUB_SAMPLED_ONE_FB_SIZE * SET_FB_NUM
 */
#define H265E_ADDR_SUB_SAMPLED_FB_BASE       0x0158
#define H265E_SUB_SAMPLED_ONE_FB_SIZE        0x015C

/************************************************************************/
/* ENCODER - ENC_PIC                                                    */
/************************************************************************/
#define H265E_CMD_ENC_ADDR_REPORT_BASE       0x015C
#define H265E_CMD_ENC_REPORT_SIZE            0x0160
#define H265E_CMD_ENC_REPORT_PARAM           0x0164
#define H265E_CMD_ENC_CODE_OPTION            0x0168
#define H265E_CMD_ENC_PIC_PARAM              0x016C
#define H265E_CMD_ENC_SRC_PIC_IDX            0x0170
#define H265E_CMD_ENC_SRC_ADDR_Y             0x0174
#define H265E_CMD_ENC_SRC_ADDR_U             0x0178
#define H265E_CMD_ENC_SRC_ADDR_V             0x017C
#define H265E_CMD_ENC_SRC_STRIDE             0x0180
#define H265E_CMD_ENC_SRC_FORMAT             0x0184
#define H265E_CMD_ENC_PREFIX_SEI_NAL_ADDR    0x0188
#define H265E_CMD_ENC_PREFIX_SEI_INFO        0x018C
#define H265E_CMD_ENC_SUFFIX_SEI_NAL_ADDR    0x0190
#define H265E_CMD_ENC_SUFFIX_SEI_INFO        0x0194
#define H265E_CMD_ENC_LONGTERM_PIC           0x0198
#define H265E_CMD_ENC_SUB_FRAME_SYNC_CONFIG  0x019C
#define H265E_CMD_ENC_CTU_OPT_PARAM          0x01A0
#define H265E_CMD_ENC_ROI_ADDR_CTU_MAP       0x01A4
#define H265E_CMD_ENC_CTU_QP_MAP_ADDR        0x01AC
#define H265E_CMD_ENC_SRC_TIMESTAMP_LOW      0x01B0
#define H265E_CMD_ENC_SRC_TIMESTAMP_HIGH     0x01B4

#define H265E_CMD_ENC_FC_PARAM               0x01E8
#define H265E_CMD_ENC_FC_TABLE_ADDR_Y        0x01EC
#define H265E_CMD_ENC_FC_TABLE_ADDR_C        0x01F0

#define H265E_RET_ENC_PIC_IDX                0x01A8
#define H265E_RET_ENC_PIC_SLICE_NUM          0x01AC
#define H265E_RET_ENC_PIC_SKIP               0x01B0
#define H265E_RET_ENC_PIC_NUM_INTRA          0x01B4
#define H265E_RET_ENC_PIC_NUM_MERGE          0x01B8
#define H265E_RET_ENC_PIC_FLAG               0x01BC
#define H265E_RET_ENC_PIC_NUM_SKIP           0x01C0
#define H265E_RET_ENC_PIC_AVG_CU_QP          0x01C4
#define H265E_RET_ENC_PIC_BYTE               0x01C8
#define H265E_RET_ENC_GOP_PIC_IDX            0x01CC
#define H265E_RET_ENC_PIC_POC                0x01D0
#define H265E_RET_ENC_USED_SRC_IDX           0x01D8
#define H265E_RET_ENC_PIC_NUM                0x01DC
#define H265E_RET_ENC_PIC_TYPE               0x01E0
#define H265E_RET_ENC_VCL_NUT                0x01E4

#define H265E_PERF_AXI_CTRL	             0x0240
#define H265E_PERF_LATENCY_CTRL0             0x0264
#define H265E_PERF_LATENCY_CTRL1             0x0268
#define H265E_PERF_RD_MAX_LATENCY_NUM0       0x026C
#define H265E_PERF_RD_LATENCY_SAMP_NUM       0x0270
#define H265E_PERF_RD_LATENCY_ACC_SUM        0x0274
#define H265E_PERF_RD_AXI_TOTAL_BYTE         0x0278
#define H265E_PERF_WR_AXI_TOTAL_BYTE         0x027C
#define H265E_PERF_WORKING_CNT		     0x0280
#endif
