/*
 * Copyright 2010 Advanced Micro Devices, Inc.
 *
 * Permission is hereby granted, free of charge, to any person obtaining a
 * copy of this software and associated documentation files (the "Software"),
 * to deal in the Software without restriction, including without limitation
 * the rights to use, copy, modify, merge, publish, distribute, sublicense,
 * and/or sell copies of the Software, and to permit persons to whom the
 * Software is furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT.  IN NO EVENT SHALL
 * THE COPYRIGHT HOLDER(S) OR AUTHOR(S) BE LIABLE FOR ANY CLAIM, DAMAGES OR
 * OTHER LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE,
 * ARISING FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR
 * OTHER DEALINGS IN THE SOFTWARE.
 *
 * Authors: Alex Deucher
 */
#ifndef EVERGREEND_H
#define EVERGREEND_H

#define EVERGREEN_MAX_SH_GPRS           256
#define EVERGREEN_MAX_TEMP_GPRS         16
#define EVERGREEN_MAX_SH_THREADS        256
#define EVERGREEN_MAX_SH_STACK_ENTRIES  4096
#define EVERGREEN_MAX_FRC_EOV_CNT       16384
#define EVERGREEN_MAX_BACKENDS          8
#define EVERGREEN_MAX_BACKENDS_MASK     0xFF
#define EVERGREEN_MAX_SIMDS             16
#define EVERGREEN_MAX_SIMDS_MASK        0xFFFF
#define EVERGREEN_MAX_PIPES             8
#define EVERGREEN_MAX_PIPES_MASK        0xFF
#define EVERGREEN_MAX_LDS_NUM           0xFFFF

#define CYPRESS_GB_ADDR_CONFIG_GOLDEN        0x02011003
#define BARTS_GB_ADDR_CONFIG_GOLDEN          0x02011003
#define CAYMAN_GB_ADDR_CONFIG_GOLDEN         0x02011003
#define JUNIPER_GB_ADDR_CONFIG_GOLDEN        0x02010002
#define REDWOOD_GB_ADDR_CONFIG_GOLDEN        0x02010002
#define TURKS_GB_ADDR_CONFIG_GOLDEN          0x02010002
#define CEDAR_GB_ADDR_CONFIG_GOLDEN          0x02010001
#define CAICOS_GB_ADDR_CONFIG_GOLDEN         0x02010001
#define SUMO_GB_ADDR_CONFIG_GOLDEN           0x02010002
#define SUMO2_GB_ADDR_CONFIG_GOLDEN          0x02010002

/* Registers */

#define RCU_IND_INDEX           			0x100
#define RCU_IND_DATA            			0x104

/* discrete uvd clocks */
#define CG_UPLL_FUNC_CNTL				0x718
#	define UPLL_RESET_MASK				0x00000001
#	define UPLL_SLEEP_MASK				0x00000002
#	define UPLL_BYPASS_EN_MASK			0x00000004
#	define UPLL_CTLREQ_MASK				0x00000008
#	define UPLL_REF_DIV_MASK			0x003F0000
#	define UPLL_VCO_MODE_MASK			0x00000200
#	define UPLL_CTLACK_MASK				0x40000000
#	define UPLL_CTLACK2_MASK			0x80000000
#define CG_UPLL_FUNC_CNTL_2				0x71c
#	define UPLL_PDIV_A(x)				((x) << 0)
#	define UPLL_PDIV_A_MASK				0x0000007F
#	define UPLL_PDIV_B(x)				((x) << 8)
#	define UPLL_PDIV_B_MASK				0x00007F00
#	define VCLK_SRC_SEL(x)				((x) << 20)
#	define VCLK_SRC_SEL_MASK			0x01F00000
#	define DCLK_SRC_SEL(x)				((x) << 25)
#	define DCLK_SRC_SEL_MASK			0x3E000000
#define CG_UPLL_FUNC_CNTL_3				0x720
#	define UPLL_FB_DIV(x)				((x) << 0)
#	define UPLL_FB_DIV_MASK				0x01FFFFFF
#define CG_UPLL_FUNC_CNTL_4				0x854
#	define UPLL_SPARE_ISPARE9			0x00020000
#define CG_UPLL_SPREAD_SPECTRUM				0x79c
#	define SSEN_MASK				0x00000001

/* fusion uvd clocks */
#define CG_DCLK_CNTL                                    0x610
#       define DCLK_DIVIDER_MASK                        0x7f
#       define DCLK_DIR_CNTL_EN                         (1 << 8)
#define CG_DCLK_STATUS                                  0x614
#       define DCLK_STATUS                              (1 << 0)
#define CG_VCLK_CNTL                                    0x618
#define CG_VCLK_STATUS                                  0x61c
#define	CG_SCRATCH1					0x820

#define RLC_CNTL                                        0x3f00
#       define RLC_ENABLE                               (1 << 0)
#       define GFX_POWER_GATING_ENABLE                  (1 << 7)
#       define GFX_POWER_GATING_SRC                     (1 << 8)
#define RLC_HB_BASE                                       0x3f10
#define RLC_HB_CNTL                                       0x3f0c
#define RLC_HB_RPTR                                       0x3f20
#define RLC_HB_WPTR                                       0x3f1c
#define RLC_HB_WPTR_LSB_ADDR                              0x3f14
#define RLC_HB_WPTR_MSB_ADDR                              0x3f18
#define RLC_MC_CNTL                                       0x3f44
#define RLC_UCODE_CNTL                                    0x3f48
#define RLC_UCODE_ADDR                                    0x3f2c
#define RLC_UCODE_DATA                                    0x3f30

/* new for TN */
#define TN_RLC_SAVE_AND_RESTORE_BASE                      0x3f10
#define TN_RLC_CLEAR_STATE_RESTORE_BASE                   0x3f20

#define GRBM_GFX_INDEX          			0x802C
#define		INSTANCE_INDEX(x)			((x) << 0)
#define		SE_INDEX(x)     			((x) << 16)
#define		INSTANCE_BROADCAST_WRITES      		(1 << 30)
#define		SE_BROADCAST_WRITES      		(1 << 31)
#define RLC_GFX_INDEX           			0x3fC4
#define CC_GC_SHADER_PIPE_CONFIG			0x8950
#define		WRITE_DIS      				(1 << 0)
#define CC_RB_BACKEND_DISABLE				0x98F4
#define		BACKEND_DISABLE(x)     			((x) << 16)
#define GB_ADDR_CONFIG  				0x98F8
#define		NUM_PIPES(x)				((x) << 0)
#define		NUM_PIPES_MASK				0x0000000f
#define		PIPE_INTERLEAVE_SIZE(x)			((x) << 4)
#define		BANK_INTERLEAVE_SIZE(x)			((x) << 8)
#define		NUM_SHADER_ENGINES(x)			((x) << 12)
#define		SHADER_ENGINE_TILE_SIZE(x)     		((x) << 16)
#define		NUM_GPUS(x)     			((x) << 20)
#define		MULTI_GPU_TILE_SIZE(x)     		((x) << 24)
#define		ROW_SIZE(x)             		((x) << 28)
#define GB_BACKEND_MAP  				0x98FC
#define DMIF_ADDR_CONFIG  				0xBD4
#define HDP_ADDR_CONFIG  				0x2F48
#define HDP_MISC_CNTL  					0x2F4C
#define		HDP_FLUSH_INVALIDATE_CACHE      	(1 << 0)

#define	CC_SYS_RB_BACKEND_DISABLE			0x3F88
#define	GC_USER_RB_BACKEND_DISABLE			0x9B7C

#define	CGTS_SYS_TCC_DISABLE				0x3F90
#define	CGTS_TCC_DISABLE				0x9148
#define	CGTS_USER_SYS_TCC_DISABLE			0x3F94
#define	CGTS_USER_TCC_DISABLE				0x914C

#define	CONFIG_MEMSIZE					0x5428

#define	BIF_FB_EN						0x5490
#define		FB_READ_EN					(1 << 0)
#define		FB_WRITE_EN					(1 << 1)

#define	CP_STRMOUT_CNTL					0x84FC

#define	CP_COHER_CNTL					0x85F0
#define	CP_COHER_SIZE					0x85F4
#define	CP_COHER_BASE					0x85F8
#define	CP_STALLED_STAT1			0x8674
#define	CP_STALLED_STAT2			0x8678
#define	CP_BUSY_STAT				0x867C
#define	CP_STAT						0x8680
#define CP_ME_CNTL					0x86D8
#define		CP_ME_HALT					(1 << 28)
#define		CP_PFP_HALT					(1 << 26)
#define	CP_ME_RAM_DATA					0xC160
#define	CP_ME_RAM_RADDR					0xC158
#define	CP_ME_RAM_WADDR					0xC15C
#define CP_MEQ_THRESHOLDS				0x8764
#define		STQ_SPLIT(x)					((x) << 0)
#define	CP_PERFMON_CNTL					0x87FC
#define	CP_PFP_UCODE_ADDR				0xC150
#define	CP_PFP_UCODE_DATA				0xC154
#define	CP_QUEUE_THRESHOLDS				0x8760
#define		ROQ_IB1_START(x)				((x) << 0)
#define		ROQ_IB2_START(x)				((x) << 8)
#define	CP_RB_BASE					0xC100
#define	CP_RB_CNTL					0xC104
#define		RB_BUFSZ(x)					((x) << 0)
#define		RB_BLKSZ(x)					((x) << 8)
#define		RB_NO_UPDATE					(1 << 27)
#define		RB_RPTR_WR_ENA					(1 << 31)
#define		BUF_SWAP_32BIT					(2 << 16)
#define	CP_RB_RPTR					0x8700
#define	CP_RB_RPTR_ADDR					0xC10C
#define		RB_RPTR_SWAP(x)					((x) << 0)
#define	CP_RB_RPTR_ADDR_HI				0xC110
#define	CP_RB_RPTR_WR					0xC108
#define	CP_RB_WPTR					0xC114
#define	CP_RB_WPTR_ADDR					0xC118
#define	CP_RB_WPTR_ADDR_HI				0xC11C
#define	CP_RB_WPTR_DELAY				0x8704
#define	CP_SEM_WAIT_TIMER				0x85BC
#define	CP_SEM_INCOMPLETE_TIMER_CNTL			0x85C8
#define	CP_DEBUG					0xC1FC

/* Audio clocks */
#define DCCG_AUDIO_DTO_SOURCE             0x05ac
#       define DCCG_AUDIO_DTO0_SOURCE_SEL(x) ((x) << 0) /* crtc0 - crtc5 */
#       define DCCG_AUDIO_DTO_SEL         (1 << 4) /* 0=dto0 1=dto1 */

#define DCCG_AUDIO_DTO0_PHASE             0x05b0
#define DCCG_AUDIO_DTO0_MODULE            0x05b4
#define DCCG_AUDIO_DTO0_LOAD              0x05b8
#define DCCG_AUDIO_DTO0_CNTL              0x05bc

#define DCCG_AUDIO_DTO1_PHASE             0x05c0
#define DCCG_AUDIO_DTO1_MODULE            0x05c4
#define DCCG_AUDIO_DTO1_LOAD              0x05c8
#define DCCG_AUDIO_DTO1_CNTL              0x05cc

/* DCE 4.0 AFMT */
#define HDMI_CONTROL                         0x7030
#       define HDMI_KEEPOUT_MODE             (1 << 0)
#       define HDMI_PACKET_GEN_VERSION       (1 << 4) /* 0 = r6xx compat */
#       define HDMI_ERROR_ACK                (1 << 8)
#       define HDMI_ERROR_MASK               (1 << 9)
#       define HDMI_DEEP_COLOR_ENABLE        (1 << 24)
#       define HDMI_DEEP_COLOR_DEPTH         (((x) & 3) << 28)
#       define HDMI_24BIT_DEEP_COLOR         0
#       define HDMI_30BIT_DEEP_COLOR         1
#       define HDMI_36BIT_DEEP_COLOR         2
#define HDMI_STATUS                          0x7034
#       define HDMI_ACTIVE_AVMUTE            (1 << 0)
#       define HDMI_AUDIO_PACKET_ERROR       (1 << 16)
#       define HDMI_VBI_PACKET_ERROR         (1 << 20)
#define HDMI_AUDIO_PACKET_CONTROL            0x7038
#       define HDMI_AUDIO_DELAY_EN(x)        (((x) & 3) << 4)
#       define HDMI_AUDIO_PACKETS_PER_LINE(x)  (((x) & 0x1f) << 16)
#define HDMI_ACR_PACKET_CONTROL              0x703c
#       define HDMI_ACR_SEND                 (1 << 0)
#       define HDMI_ACR_CONT                 (1 << 1)
#       define HDMI_ACR_SELECT(x)            (((x) & 3) << 4)
#       define HDMI_ACR_HW                   0
#       define HDMI_ACR_32                   1
#       define HDMI_ACR_44                   2
#       define HDMI_ACR_48                   3
#       define HDMI_ACR_SOURCE               (1 << 8) /* 0 - hw; 1 - cts value */
#       define HDMI_ACR_AUTO_SEND            (1 << 12)
#       define HDMI_ACR_N_MULTIPLE(x)        (((x) & 7) << 16)
#       define HDMI_ACR_X1                   1
#       define HDMI_ACR_X2                   2
#       define HDMI_ACR_X4                   4
#       define HDMI_ACR_AUDIO_PRIORITY       (1 << 31)
#define HDMI_VBI_PACKET_CONTROL              0x7040
#       define HDMI_NULL_SEND                (1 << 0)
#       define HDMI_GC_SEND                  (1 << 4)
#       define HDMI_GC_CONT                  (1 << 5) /* 0 - once; 1 - every frame */
#define HDMI_INFOFRAME_CONTROL0              0x7044
#       define HDMI_AVI_INFO_SEND            (1 << 0)
#       define HDMI_AVI_INFO_CONT            (1 << 1)
#       define HDMI_AUDIO_INFO_SEND          (1 << 4)
#       define HDMI_AUDIO_INFO_CONT          (1 << 5)
#       define HDMI_MPEG_INFO_SEND           (1 << 8)
#       define HDMI_MPEG_INFO_CONT           (1 << 9)
#define HDMI_INFOFRAME_CONTROL1              0x7048
#       define HDMI_AVI_INFO_LINE(x)         (((x) & 0x3f) << 0)
#       define HDMI_AVI_INFO_LINE_MASK       (0x3f << 0)
#       define HDMI_AUDIO_INFO_LINE(x)       (((x) & 0x3f) << 8)
#       define HDMI_MPEG_INFO_LINE(x)        (((x) & 0x3f) << 16)
#define HDMI_GENERIC_PACKET_CONTROL          0x704c
#       define HDMI_GENERIC0_SEND            (1 << 0)
#       define HDMI_GENERIC0_CONT            (1 << 1)
#       define HDMI_GENERIC1_SEND            (1 << 4)
#       define HDMI_GENERIC1_CONT            (1 << 5)
#       define HDMI_GENERIC0_LINE(x)         (((x) & 0x3f) << 16)
#       define HDMI_GENERIC1_LINE(x)         (((x) & 0x3f) << 24)
#define HDMI_GC                              0x7058
#       define HDMI_GC_AVMUTE                (1 << 0)
#       define HDMI_GC_AVMUTE_CONT           (1 << 2)
#define AFMT_AUDIO_PACKET_CONTROL2           0x705c
#       define AFMT_AUDIO_LAYOUT_OVRD        (1 << 0)
#       define AFMT_AUDIO_LAYOUT_SELECT      (1 << 1)
#       define AFMT_60958_CS_SOURCE          (1 << 4)
#       define AFMT_AUDIO_CHANNEL_ENABLE(x)  (((x) & 0xff) << 8)
#       define AFMT_DP_AUDIO_STREAM_ID(x)    (((x) & 0xff) << 16)
#define AFMT_AVI_INFO0                       0x7084
#       define AFMT_AVI_INFO_CHECKSUM(x)     (((x) & 0xff) << 0)
#       define AFMT_AVI_INFO_S(x)            (((x) & 3) << 8)
#       define AFMT_AVI_INFO_B(x)            (((x) & 3) << 10)
#       define AFMT_AVI_INFO_A(x)            (((x) & 1) << 12)
#       define AFMT_AVI_INFO_Y(x)            (((x) & 3) << 13)
#       define AFMT_AVI_INFO_Y_RGB           0
#       define AFMT_AVI_INFO_Y_YCBCR422      1
#       define AFMT_AVI_INFO_Y_YCBCR444      2
#       define AFMT_AVI_INFO_Y_A_B_S(x)      (((x) & 0xff) << 8)
#       define AFMT_AVI_INFO_R(x)            (((x) & 0xf) << 16)
#       define AFMT_AVI_INFO_M(x)            (((x) & 0x3) << 20)
#       define AFMT_AVI_INFO_C(x)            (((x) & 0x3) << 22)
#       define AFMT_AVI_INFO_C_M_R(x)        (((x) & 0xff) << 16)
#       define AFMT_AVI_INFO_SC(x)           (((x) & 0x3) << 24)
#       define AFMT_AVI_INFO_Q(x)            (((x) & 0x3) << 26)
#       define AFMT_AVI_INFO_EC(x)           (((x) & 0x3) << 28)
#       define AFMT_AVI_INFO_ITC(x)          (((x) & 0x1) << 31)
#       define AFMT_AVI_INFO_ITC_EC_Q_SC(x)  (((x) & 0xff) << 24)
#define AFMT_AVI_INFO1                       0x7088
#       define AFMT_AVI_INFO_VIC(x)          (((x) & 0x7f) << 0) /* don't use avi infoframe v1 */
#       define AFMT_AVI_INFO_PR(x)           (((x) & 0xf) << 8) /* don't use avi infoframe v1 */
#       define AFMT_AVI_INFO_CN(x)           (((x) & 0x3) << 12)
#       define AFMT_AVI_INFO_YQ(x)           (((x) & 0x3) << 14)
#       define AFMT_AVI_INFO_TOP(x)          (((x) & 0xffff) << 16)
#define AFMT_AVI_INFO2                       0x708c
#       define AFMT_AVI_INFO_BOTTOM(x)       (((x) & 0xffff) << 0)
#       define AFMT_AVI_INFO_LEFT(x)         (((x) & 0xffff) << 16)
#define AFMT_AVI_INFO3                       0x7090
#       define AFMT_AVI_INFO_RIGHT(x)        (((x) & 0xffff) << 0)
#       define AFMT_AVI_INFO_VERSION(x)      (((x) & 3) << 24)
#define AFMT_MPEG_INFO0                      0x7094
#       define AFMT_MPEG_INFO_CHECKSUM(x)    (((x) & 0xff) << 0)
#       define AFMT_MPEG_INFO_MB0(x)         (((x) & 0xff) << 8)
#       define AFMT_MPEG_INFO_MB1(x)         (((x) & 0xff) << 16)
#       define AFMT_MPEG_INFO_MB2(x)         (((x) & 0xff) << 24)
#define AFMT_MPEG_INFO1                      0x7098
#       define AFMT_MPEG_INFO_MB3(x)         (((x) & 0xff) << 0)
#       define AFMT_MPEG_INFO_MF(x)          (((x) & 3) << 8)
#       define AFMT_MPEG_INFO_FR(x)          (((x) & 1) << 12)
#define AFMT_GENERIC0_HDR                    0x709c
#define AFMT_GENERIC0_0                      0x70a0
#define AFMT_GENERIC0_1                      0x70a4
#define AFMT_GENERIC0_2                      0x70a8
#define AFMT_GENERIC0_3                      0x70ac
#define AFMT_GENERIC0_4                      0x70b0
#define AFMT_GENERIC0_5                      0x70b4
#define AFMT_GENERIC0_6                      0x70b8
#define AFMT_GENERIC1_HDR                    0x70bc
#define AFMT_GENERIC1_0                      0x70c0
#define AFMT_GENERIC1_1                      0x70c4
#define AFMT_GENERIC1_2                      0x70c8
#define AFMT_GENERIC1_3                      0x70cc
#define AFMT_GENERIC1_4                      0x70d0
#define AFMT_GENERIC1_5                      0x70d4
#define AFMT_GENERIC1_6                      0x70d8
#define HDMI_ACR_32_0                        0x70dc
#       define HDMI_ACR_CTS_32(x)            (((x) & 0xfffff) << 12)
#define HDMI_ACR_32_1                        0x70e0
#       define HDMI_ACR_N_32(x)              (((x) & 0xfffff) << 0)
#define HDMI_ACR_44_0                        0x70e4
#       define HDMI_ACR_CTS_44(x)            (((x) & 0xfffff) << 12)
#define HDMI_ACR_44_1                        0x70e8
#       define HDMI_ACR_N_44(x)              (((x) & 0xfffff) << 0)
#define HDMI_ACR_48_0                        0x70ec
#       define HDMI_ACR_CTS_48(x)            (((x) & 0xfffff) << 12)
#define HDMI_ACR_48_1                        0x70f0
#       define HDMI_ACR_N_48(x)              (((x) & 0xfffff) << 0)
#define HDMI_ACR_STATUS_0                    0x70f4
#define HDMI_ACR_STATUS_1                    0x70f8
#define AFMT_AUDIO_INFO0                     0x70fc
#       define AFMT_AUDIO_INFO_CHECKSUM(x)   (((x) & 0xff) << 0)
#       define AFMT_AUDIO_INFO_CC(x)         (((x) & 7) << 8)
#       define AFMT_AUDIO_INFO_CT(x)         (((x) & 0xf) << 11)
#       define AFMT_AUDIO_INFO_CHECKSUM_OFFSET(x)   (((x) & 0xff) << 16)
#       define AFMT_AUDIO_INFO_CXT(x)        (((x) & 0x1f) << 24)
#define AFMT_AUDIO_INFO1                     0x7100
#       define AFMT_AUDIO_INFO_CA(x)         (((x) & 0xff) << 0)
#       define AFMT_AUDIO_INFO_LSV(x)        (((x) & 0xf) << 11)
#       define AFMT_AUDIO_INFO_DM_INH(x)     (((x) & 1) << 15)
#       define AFMT_AUDIO_INFO_DM_INH_LSV(x) (((x) & 0xff) << 8)
#       define AFMT_AUDIO_INFO_LFEBPL(x)     (((x) & 3) << 16)
#define AFMT_60958_0                         0x7104
#       define AFMT_60958_CS_A(x)            (((x) & 1) << 0)
#       define AFMT_60958_CS_B(x)            (((x) & 1) << 1)
#       define AFMT_60958_CS_C(x)            (((x) & 1) << 2)
#       define AFMT_60958_CS_D(x)            (((x) & 3) << 3)
#       define AFMT_60958_CS_MODE(x)         (((x) & 3) << 6)
#       define AFMT_60958_CS_CATEGORY_CODE(x)      (((x) & 0xff) << 8)
#       define AFMT_60958_CS_SOURCE_NUMBER(x)      (((x) & 0xf) << 16)
#       define AFMT_60958_CS_CHANNEL_NUMBER_L(x)   (((x) & 0xf) << 20)
#       define AFMT_60958_CS_SAMPLING_FREQUENCY(x) (((x) & 0xf) << 24)
#       define AFMT_60958_CS_CLOCK_ACCURACY(x)     (((x) & 3) << 28)
#define AFMT_60958_1                         0x7108
#       define AFMT_60958_CS_WORD_LENGTH(x)  (((x) & 0xf) << 0)
#       define AFMT_60958_CS_ORIGINAL_SAMPLING_FREQUENCY(x)   (((x) & 0xf) << 4)
#       define AFMT_60958_CS_VALID_L(x)      (((x) & 1) << 16)
#       define AFMT_60958_CS_VALID_R(x)      (((x) & 1) << 18)
#       define AFMT_60958_CS_CHANNEL_NUMBER_R(x)   (((x) & 0xf) << 20)
#define AFMT_AUDIO_CRC_CONTROL               0x710c
#       define AFMT_AUDIO_CRC_EN             (1 << 0)
#define AFMT_RAMP_CONTROL0                   0x7110
#       define AFMT_RAMP_MAX_COUNT(x)        (((x) & 0xffffff) << 0)
#       define AFMT_RAMP_DATA_SIGN           (1 << 31)
#define AFMT_RAMP_CONTROL1                   0x7114
#       define AFMT_RAMP_MIN_COUNT(x)        (((x) & 0xffffff) << 0)
#       define AFMT_AUDIO_TEST_CH_DISABLE(x) (((x) & 0xff) << 24)
#define AFMT_RAMP_CONTROL2                   0x7118
#       define AFMT_RAMP_INC_COUNT(x)        (((x) & 0xffffff) << 0)
#define AFMT_RAMP_CONTROL3                   0x711c
#       define AFMT_RAMP_DEC_COUNT(x)        (((x) & 0xffffff) << 0)
#define AFMT_60958_2                         0x7120
#       define AFMT_60958_CS_CHANNEL_NUMBER_2(x)   (((x) & 0xf) << 0)
#       define AFMT_60958_CS_CHANNEL_NUMBER_3(x)   (((x) & 0xf) << 4)
#       define AFMT_60958_CS_CHANNEL_NUMBER_4(x)   (((x) & 0xf) << 8)
#       define AFMT_60958_CS_CHANNEL_NUMBER_5(x)   (((x) & 0xf) << 12)
#       define AFMT_60958_CS_CHANNEL_NUMBER_6(x)   (((x) & 0xf) << 16)
#       define AFMT_60958_CS_CHANNEL_NUMBER_7(x)   (((x) & 0xf) << 20)
#define AFMT_STATUS                          0x7128
#       define AFMT_AUDIO_ENABLE             (1 << 4)
#       define AFMT_AUDIO_HBR_ENABLE         (1 << 8)
#       define AFMT_AZ_FORMAT_WTRIG          (1 << 28)
#       define AFMT_AZ_FORMAT_WTRIG_INT      (1 << 29)
#       define AFMT_AZ_AUDIO_ENABLE_CHG      (1 << 30)
#define AFMT_AUDIO_PACKET_CONTROL            0x712c
#       define AFMT_AUDIO_SAMPLE_SEND        (1 << 0)
#       define AFMT_RESET_FIFO_WHEN_AUDIO_DIS (1 << 11) /* set to 1 */
#       define AFMT_AUDIO_TEST_EN            (1 << 12)
#       define AFMT_AUDIO_CHANNEL_SWAP       (1 << 24)
#       define AFMT_60958_CS_UPDATE          (1 << 26)
#       define AFMT_AZ_AUDIO_ENABLE_CHG_MASK (1 << 27)
#       define AFMT_AZ_FORMAT_WTRIG_MASK     (1 << 28)
#       define AFMT_AZ_FORMAT_WTRIG_ACK      (1 << 29)
#       define AFMT_AZ_AUDIO_ENABLE_CHG_ACK  (1 << 30)
#define AFMT_VBI_PACKET_CONTROL              0x7130
#       define AFMT_GENERIC0_UPDATE          (1 << 2)
#define AFMT_INFOFRAME_CONTROL0              0x7134
#       define AFMT_AUDIO_INFO_SOURCE        (1 << 6) /* 0 - sound block; 1 - afmt regs */
#       define AFMT_AUDIO_INFO_UPDATE        (1 << 7)
#       define AFMT_MPEG_INFO_UPDATE         (1 << 10)
#define AFMT_GENERIC0_7                      0x7138

/* DCE4/5 ELD audio interface */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR0        0x5f84 /* LPCM */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR1        0x5f88 /* AC3 */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR2        0x5f8c /* MPEG1 */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR3        0x5f90 /* MP3 */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR4        0x5f94 /* MPEG2 */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR5        0x5f98 /* AAC */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR6        0x5f9c /* DTS */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR7        0x5fa0 /* ATRAC */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR8        0x5fa4 /* one bit audio - leave at 0 (default) */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR9        0x5fa8 /* Dolby Digital */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR10       0x5fac /* DTS-HD */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR11       0x5fb0 /* MAT-MLP */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR12       0x5fb4 /* DTS */
#define AZ_F0_CODEC_PIN0_CONTROL_AUDIO_DESCRIPTOR13       0x5fb8 /* WMA Pro */
#       define MAX_CHANNELS(x)                            (((x) & 0x7) << 0)
/* max channels minus one.  7 = 8 channels */
#       define SUPPORTED_FREQUENCIES(x)                   (((x) & 0xff) << 8)
#       define DESCRIPTOR_BYTE_2(x)                       (((x) & 0xff) << 16)
#       define SUPPORTED_FREQUENCIES_STEREO(x)            (((x) & 0xff) << 24) /* LPCM only */
/* SUPPORTED_FREQUENCIES, SUPPORTED_FREQUENCIES_STEREO
 * bit0 = 32 kHz
 * bit1 = 44.1 kHz
 * bit2 = 48 kHz
 * bit3 = 88.2 kHz
 * bit4 = 96 kHz
 * bit5 = 176.4 kHz
 * bit6 = 192 kHz
 */

#define AZ_HOT_PLUG_CONTROL                               0x5e78
#       define AZ_FORCE_CODEC_WAKE                        (1 << 0)
#       define PIN0_JACK_DETECTION_ENABLE                 (1 << 4)
#       define PIN1_JACK_DETECTION_ENABLE                 (1 << 5)
#       define PIN2_JACK_DETECTION_ENABLE                 (1 << 6)
#       define PIN3_JACK_DETECTION_ENABLE                 (1 << 7)
#       define PIN0_UNSOLICITED_RESPONSE_ENABLE           (1 << 8)
#       define PIN1_UNSOLICITED_RESPONSE_ENABLE           (1 << 9)
#       define PIN2_UNSOLICITED_RESPONSE_ENABLE           (1 << 10)
#       define PIN3_UNSOLICITED_RESPONSE_ENABLE           (1 << 11)
#       define CODEC_HOT_PLUG_ENABLE                      (1 << 12)
#       define PIN0_AUDIO_ENABLED                         (1 << 24)
#       define PIN1_AUDIO_ENABLED                         (1 << 25)
#       define PIN2_AUDIO_ENABLED                         (1 << 26)
#       define PIN3_AUDIO_ENABLED                         (1 << 27)
#       define AUDIO_ENABLED                              (1 << 31)


#define	GC_USER_SHADER_PIPE_CONFIG			0x8954
#define		INACTIVE_QD_PIPES(x)				((x) << 8)
#define		INACTIVE_QD_PIPES_MASK				0x0000FF00
#define		INACTIVE_SIMDS(x)				((x) << 16)
#define		INACTIVE_SIMDS_MASK				0x00FF0000

#define	GRBM_CNTL					0x8000
#define		GRBM_READ_TIMEOUT(x)				((x) << 0)
#define	GRBM_SOFT_RESET					0x8020
#define		SOFT_RESET_CP					(1 << 0)
#define		SOFT_RESET_CB					(1 << 1)
#define		SOFT_RESET_DB					(1 << 3)
#define		SOFT_RESET_PA					(1 << 5)
#define		SOFT_RESET_SC					(1 << 6)
#define		SOFT_RESET_SPI					(1 << 8)
#define		SOFT_RESET_SH					(1 << 9)
#define		SOFT_RESET_SX					(1 << 10)
#define		SOFT_RESET_TC					(1 << 11)
#define		SOFT_RESET_TA					(1 << 12)
#define		SOFT_RESET_VC					(1 << 13)
#define		SOFT_RESET_VGT					(1 << 14)

#define	GRBM_STATUS					0x8010
#define		CMDFIFO_AVAIL_MASK				0x0000000F
#define		SRBM_RQ_PENDING					(1 << 5)
#define		CF_RQ_PENDING					(1 << 7)
#define		PF_RQ_PENDING					(1 << 8)
#define		GRBM_EE_BUSY					(1 << 10)
#define		SX_CLEAN					(1 << 11)
#define		DB_CLEAN					(1 << 12)
#define		CB_CLEAN					(1 << 13)
#define		TA_BUSY 					(1 << 14)
#define		VGT_BUSY_NO_DMA					(1 << 16)
#define		VGT_BUSY					(1 << 17)
#define		SX_BUSY 					(1 << 20)
#define		SH_BUSY 					(1 << 21)
#define		SPI_BUSY					(1 << 22)
#define		SC_BUSY 					(1 << 24)
#define		PA_BUSY 					(1 << 25)
#define		DB_BUSY 					(1 << 26)
#define		CP_COHERENCY_BUSY      				(1 << 28)
#define		CP_BUSY 					(1 << 29)
#define		CB_BUSY 					(1 << 30)
#define		GUI_ACTIVE					(1 << 31)
#define	GRBM_STATUS_SE0					0x8014
#define	GRBM_STATUS_SE1					0x8018
#define		SE_SX_CLEAN					(1 << 0)
#define		SE_DB_CLEAN					(1 << 1)
#define		SE_CB_CLEAN					(1 << 2)
#define		SE_TA_BUSY					(1 << 25)
#define		SE_SX_BUSY					(1 << 26)
#define		SE_SPI_BUSY					(1 << 27)
#define		SE_SH_BUSY					(1 << 28)
#define		SE_SC_BUSY					(1 << 29)
#define		SE_DB_BUSY					(1 << 30)
#define		SE_CB_BUSY					(1 << 31)
/* evergreen */
#define	CG_THERMAL_CTRL					0x72c
#define		TOFFSET_MASK			        0x00003FE0
#define		TOFFSET_SHIFT			        5
#define	CG_MULT_THERMAL_STATUS				0x740
#define		ASIC_T(x)			        ((x) << 16)
#define		ASIC_T_MASK			        0x07FF0000
#define		ASIC_T_SHIFT			        16
#define	CG_TS0_STATUS					0x760
#define		TS0_ADC_DOUT_MASK			0x000003FF
#define		TS0_ADC_DOUT_SHIFT			0
/* APU */
#define	CG_THERMAL_STATUS			        0x678

#define	HDP_HOST_PATH_CNTL				0x2C00
#define	HDP_NONSURFACE_BASE				0x2C04
#define	HDP_NONSURFACE_INFO				0x2C08
#define	HDP_NONSURFACE_SIZE				0x2C0C
#define HDP_MEM_COHERENCY_FLUSH_CNTL			0x5480
#define HDP_REG_COHERENCY_FLUSH_CNTL			0x54A0
#define	HDP_TILING_CONFIG				0x2F3C

#define MC_SHARED_CHMAP						0x2004
#define		NOOFCHAN_SHIFT					12
#define		NOOFCHAN_MASK					0x00003000
#define MC_SHARED_CHREMAP					0x2008

#define MC_SHARED_BLACKOUT_CNTL           		0x20ac
#define		BLACKOUT_MODE_MASK			0x00000007

#define	MC_ARB_RAMCFG					0x2760
#define		NOOFBANK_SHIFT					0
#define		NOOFBANK_MASK					0x00000003
#define		NOOFRANK_SHIFT					2
#define		NOOFRANK_MASK					0x00000004
#define		NOOFROWS_SHIFT					3
#define		NOOFROWS_MASK					0x00000038
#define		NOOFCOLS_SHIFT					6
#define		NOOFCOLS_MASK					0x000000C0
#define		CHANSIZE_SHIFT					8
#define		CHANSIZE_MASK					0x00000100
#define		BURSTLENGTH_SHIFT				9
#define		BURSTLENGTH_MASK				0x00000200
#define		CHANSIZE_OVERRIDE				(1 << 11)
#define	FUS_MC_ARB_RAMCFG				0x2768
#define	MC_VM_AGP_TOP					0x2028
#define	MC_VM_AGP_BOT					0x202C
#define	MC_VM_AGP_BASE					0x2030
#define	MC_VM_FB_LOCATION				0x2024
#define	MC_FUS_VM_FB_OFFSET				0x2898
#define	MC_VM_MB_L1_TLB0_CNTL				0x2234
#define	MC_VM_MB_L1_TLB1_CNTL				0x2238
#define	MC_VM_MB_L1_TLB2_CNTL				0x223C
#define	MC_VM_MB_L1_TLB3_CNTL				0x2240
#define		ENABLE_L1_TLB					(1 << 0)
#define		ENABLE_L1_FRAGMENT_PROCESSING			(1 << 1)
#define		SYSTEM_ACCESS_MODE_PA_ONLY			(0 << 3)
#define		SYSTEM_ACCESS_MODE_USE_SYS_MAP			(1 << 3)
#define		SYSTEM_ACCESS_MODE_IN_SYS			(2 << 3)
#define		SYSTEM_ACCESS_MODE_NOT_IN_SYS			(3 << 3)
#define		SYSTEM_APERTURE_UNMAPPED_ACCESS_PASS_THRU	(0 << 5)
#define		EFFECTIVE_L1_TLB_SIZE(x)			((x)<<15)
#define		EFFECTIVE_L1_QUEUE_SIZE(x)			((x)<<18)
#define	MC_VM_MD_L1_TLB0_CNTL				0x2654
#define	MC_VM_MD_L1_TLB1_CNTL				0x2658
#define	MC_VM_MD_L1_TLB2_CNTL				0x265C
#define	MC_VM_MD_L1_TLB3_CNTL				0x2698

#define	FUS_MC_VM_MD_L1_TLB0_CNTL			0x265C
#define	FUS_MC_VM_MD_L1_TLB1_CNTL			0x2660
#define	FUS_MC_VM_MD_L1_TLB2_CNTL			0x2664

#define	MC_VM_SYSTEM_APERTURE_DEFAULT_ADDR		0x203C
#define	MC_VM_SYSTEM_APERTURE_HIGH_ADDR			0x2038
#define	MC_VM_SYSTEM_APERTURE_LOW_ADDR			0x2034

#define	PA_CL_ENHANCE					0x8A14
#define		CLIP_VTX_REORDER_ENA				(1 << 0)
#define		NUM_CLIP_SEQ(x)					((x) << 1)
#define	PA_SC_ENHANCE					0x8BF0
#define PA_SC_AA_CONFIG					0x28C04
#define         MSAA_NUM_SAMPLES_SHIFT                  0
#define         MSAA_NUM_SAMPLES_MASK                   0x3
#define PA_SC_CLIPRECT_RULE				0x2820C
#define	PA_SC_EDGERULE					0x28230
#define	PA_SC_FIFO_SIZE					0x8BCC
#define		SC_PRIM_FIFO_SIZE(x)				((x) << 0)
#define		SC_HIZ_TILE_FIFO_SIZE(x)			((x) << 12)
#define		SC_EARLYZ_TILE_FIFO_SIZE(x)			((x) << 20)
#define	PA_SC_FORCE_EOV_MAX_CNTS			0x8B24
#define		FORCE_EOV_MAX_CLK_CNT(x)			((x) << 0)
#define		FORCE_EOV_MAX_REZ_CNT(x)			((x) << 16)
#define PA_SC_LINE_STIPPLE				0x28A0C
#define	PA_SU_LINE_STIPPLE_VALUE			0x8A60
#define	PA_SC_LINE_STIPPLE_STATE			0x8B10

#define	SCRATCH_REG0					0x8500
#define	SCRATCH_REG1					0x8504
#define	SCRATCH_REG2					0x8508
#define	SCRATCH_REG3					0x850C
#define	SCRATCH_REG4					0x8510
#define	SCRATCH_REG5					0x8514
#define	SCRATCH_REG6					0x8518
#define	SCRATCH_REG7					0x851C
#define	SCRATCH_UMSK					0x8540
#define	SCRATCH_ADDR					0x8544

#define	SMX_SAR_CTL0					0xA008
#define	SMX_DC_CTL0					0xA020
#define		USE_HASH_FUNCTION				(1 << 0)
#define		NUMBER_OF_SETS(x)				((x) << 1)
#define		FLUSH_ALL_ON_EVENT				(1 << 10)
#define		STALL_ON_EVENT					(1 << 11)
#define	SMX_EVENT_CTL					0xA02C
#define		ES_FLUSH_CTL(x)					((x) << 0)
#define		GS_FLUSH_CTL(x)					((x) << 3)
#define		ACK_FLUSH_CTL(x)				((x) << 6)
#define		SYNC_FLUSH_CTL					(1 << 8)

#define	SPI_CONFIG_CNTL					0x9100
#define		GPR_WRITE_PRIORITY(x)				((x) << 0)
#define	SPI_CONFIG_CNTL_1				0x913C
#define		VTX_DONE_DELAY(x)				((x) << 0)
#define		INTERP_ONE_PRIM_PER_ROW				(1 << 4)
#define	SPI_INPUT_Z					0x286D8
#define	SPI_PS_IN_CONTROL_0				0x286CC
#define		NUM_INTERP(x)					((x)<<0)
#define		POSITION_ENA					(1<<8)
#define		POSITION_CENTROID				(1<<9)
#define		POSITION_ADDR(x)				((x)<<10)
#define		PARAM_GEN(x)					((x)<<15)
#define		PARAM_GEN_ADDR(x)				((x)<<19)
#define		BARYC_SAMPLE_CNTL(x)				((x)<<26)
#define		PERSP_GRADIENT_ENA				(1<<28)
#define		LINEAR_GRADIENT_ENA				(1<<29)
#define		POSITION_SAMPLE					(1<<30)
#define		BARYC_AT_SAMPLE_ENA				(1<<31)

#define	SQ_CONFIG					0x8C00
#define		VC_ENABLE					(1 << 0)
#define		EXPORT_SRC_C					(1 << 1)
#define		CS_PRIO(x)					((x) << 18)
#define		LS_PRIO(x)					((x) << 20)
#define		HS_PRIO(x)					((x) << 22)
#define		PS_PRIO(x)					((x) << 24)
#define		VS_PRIO(x)					((x) << 26)
#define		GS_PRIO(x)					((x) << 28)
#define		ES_PRIO(x)					((x) << 30)
#define	SQ_GPR_RESOURCE_MGMT_1				0x8C04
#define		NUM_PS_GPRS(x)					((x) << 0)
#define		NUM_VS_GPRS(x)					((x) << 16)
#define		NUM_CLAUSE_TEMP_GPRS(x)				((x) << 28)
#define	SQ_GPR_RESOURCE_MGMT_2				0x8C08
#define		NUM_GS_GPRS(x)					((x) << 0)
#define		NUM_ES_GPRS(x)					((x) << 16)
#define	SQ_GPR_RESOURCE_MGMT_3				0x8C0C
#define		NUM_HS_GPRS(x)					((x) << 0)
#define		NUM_LS_GPRS(x)					((x) << 16)
#define	SQ_GLOBAL_GPR_RESOURCE_MGMT_1			0x8C10
#define	SQ_GLOBAL_GPR_RESOURCE_MGMT_2			0x8C14
#define	SQ_THREAD_RESOURCE_MGMT				0x8C18
#define		NUM_PS_THREADS(x)				((x) << 0)
#define		NUM_VS_THREADS(x)				((x) << 8)
#define		NUM_GS_THREADS(x)				((x) << 16)
#define		NUM_ES_THREADS(x)				((x) << 24)
#define	SQ_THREAD_RESOURCE_MGMT_2			0x8C1C
#define		NUM_HS_THREADS(x)				((x) << 0)
#define		NUM_LS_THREADS(x)				((x) << 8)
#define	SQ_STACK_RESOURCE_MGMT_1			0x8C20
#define		NUM_PS_STACK_ENTRIES(x)				((x) << 0)
#define		NUM_VS_STACK_ENTRIES(x)				((x) << 16)
#define	SQ_STACK_RESOURCE_MGMT_2			0x8C24
#define		NUM_GS_STACK_ENTRIES(x)				((x) << 0)
#define		NUM_ES_STACK_ENTRIES(x)				((x) << 16)
#define	SQ_STACK_RESOURCE_MGMT_3			0x8C28
#define		NUM_HS_STACK_ENTRIES(x)				((x) << 0)
#define		NUM_LS_STACK_ENTRIES(x)				((x) << 16)
#define	SQ_DYN_GPR_CNTL_PS_FLUSH_REQ    		0x8D8C
#define	SQ_DYN_GPR_SIMD_LOCK_EN    			0x8D94
#define	SQ_STATIC_THREAD_MGMT_1    			0x8E20
#define	SQ_STATIC_THREAD_MGMT_2    			0x8E24
#define	SQ_STATIC_THREAD_MGMT_3    			0x8E28
#define	SQ_LDS_RESOURCE_MGMT    			0x8E2C

#define	SQ_MS_FIFO_SIZES				0x8CF0
#define		CACHE_FIFO_SIZE(x)				((x) << 0)
#define		FETCH_FIFO_HIWATER(x)				((x) << 8)
#define		DONE_FIFO_HIWATER(x)				((x) << 16)
#define		ALU_UPDATE_FIFO_HIWATER(x)			((x) << 24)

#define	SX_DEBUG_1					0x9058
#define		ENABLE_NEW_SMX_ADDRESS				(1 << 16)
#define	SX_EXPORT_BUFFER_SIZES				0x900C
#define		COLOR_BUFFER_SIZE(x)				((x) << 0)
#define		POSITION_BUFFER_SIZE(x)				((x) << 8)
#define		SMX_BUFFER_SIZE(x)				((x) << 16)
#define	SX_MEMORY_EXPORT_BASE				0x9010
#define	SX_MISC						0x28350

#define CB_PERF_CTR0_SEL_0				0x9A20
#define CB_PERF_CTR0_SEL_1				0x9A24
#define CB_PERF_CTR1_SEL_0				0x9A28
#define CB_PERF_CTR1_SEL_1				0x9A2C
#define CB_PERF_CTR2_SEL_0				0x9A30
#define CB_PERF_CTR2_SEL_1				0x9A34
#define CB_PERF_CTR3_SEL_0				0x9A38
#define CB_PERF_CTR3_SEL_1				0x9A3C

#define	TA_CNTL_AUX					0x9508
#define		DISABLE_CUBE_WRAP				(1 << 0)
#define		DISABLE_CUBE_ANISO				(1 << 1)
#define		SYNC_GRADIENT					(1 << 24)
#define		SYNC_WALKER					(1 << 25)
#define		SYNC_ALIGNER					(1 << 26)

#define	TCP_CHAN_STEER_LO				0x960c
#define	TCP_CHAN_STEER_HI				0x9610

#define	VGT_CACHE_INVALIDATION				0x88C4
#define		CACHE_INVALIDATION(x)				((x) << 0)
#define			VC_ONLY						0
#define			TC_ONLY						1
#define			VC_AND_TC					2
#define		AUTO_INVLD_EN(x)				((x) << 6)
#define			NO_AUTO						0
#define			ES_AUTO						1
#define			GS_AUTO						2
#define			ES_AND_GS_AUTO					3
#define	VGT_GS_VERTEX_REUSE				0x88D4
#define	VGT_NUM_INSTANCES				0x8974
#define	VGT_OUT_DEALLOC_CNTL				0x28C5C
#define		DEALLOC_DIST_MASK				0x0000007F
#define	VGT_VERTEX_REUSE_BLOCK_CNTL			0x28C58
#define		VTX_REUSE_DEPTH_MASK				0x000000FF

#define VM_CONTEXT0_CNTL				0x1410
#define		ENABLE_CONTEXT					(1 << 0)
#define		PAGE_TABLE_DEPTH(x)				(((x) & 3) << 1)
#define		RANGE_PROTECTION_FAULT_ENABLE_DEFAULT		(1 << 4)
#define VM_CONTEXT1_CNTL				0x1414
#define VM_CONTEXT1_CNTL2				0x1434
#define	VM_CONTEXT0_PAGE_TABLE_BASE_ADDR		0x153C
#define	VM_CONTEXT0_PAGE_TABLE_END_ADDR			0x157C
#define	VM_CONTEXT0_PAGE_TABLE_START_ADDR		0x155C
#define VM_CONTEXT0_PROTECTION_FAULT_DEFAULT_ADDR	0x1518
#define VM_CONTEXT0_REQUEST_RESPONSE			0x1470
#define		REQUEST_TYPE(x)					(((x) & 0xf) << 0)
#define		RESPONSE_TYPE_MASK				0x000000F0
#define		RESPONSE_TYPE_SHIFT				4
#define VM_L2_CNTL					0x1400
#define		ENABLE_L2_CACHE					(1 << 0)
#define		ENABLE_L2_FRAGMENT_PROCESSING			(1 << 1)
#define		ENABLE_L2_PTE_CACHE_LRU_UPDATE_BY_WRITE		(1 << 9)
#define		EFFECTIVE_L2_QUEUE_SIZE(x)			(((x) & 7) << 14)
#define VM_L2_CNTL2					0x1404
#define		INVALIDATE_ALL_L1_TLBS				(1 << 0)
#define		INVALIDATE_L2_CACHE				(1 << 1)
#define VM_L2_CNTL3					0x1408
#define		BANK_SELECT(x)					((x) << 0)
#define		CACHE_UPDATE_MODE(x)				((x) << 6)
#define	VM_L2_STATUS					0x140C
#define		L2_BUSY						(1 << 0)
#define	VM_CONTEXT1_PROTECTION_FAULT_ADDR		0x14FC
#define	VM_CONTEXT1_PROTECTION_FAULT_STATUS		0x14DC

#define	WAIT_UNTIL					0x8040

#define	SRBM_STATUS				        0x0E50
#define		RLC_RQ_PENDING 				(1 << 3)
#define		GRBM_RQ_PENDING 			(1 << 5)
#define		VMC_BUSY 				(1 << 8)
#define		MCB_BUSY 				(1 << 9)
#define		MCB_NON_DISPLAY_BUSY 			(1 << 10)
#define		MCC_BUSY 				(1 << 11)
#define		MCD_BUSY 				(1 << 12)
#define		SEM_BUSY 				(1 << 14)
#define		RLC_BUSY 				(1 << 15)
#define		IH_BUSY 				(1 << 17)
#define	SRBM_STATUS2				        0x0EC4
#define		DMA_BUSY 				(1 << 5)
#define	SRBM_SOFT_RESET				        0x0E60
#define		SRBM_SOFT_RESET_ALL_MASK    	       	0x00FEEFA6
#define		SOFT_RESET_BIF				(1 << 1)
#define		SOFT_RESET_CG				(1 << 2)
#define		SOFT_RESET_DC				(1 << 5)
#define		SOFT_RESET_GRBM				(1 << 8)
#define		SOFT_RESET_HDP				(1 << 9)
#define		SOFT_RESET_IH				(1 << 10)
#define		SOFT_RESET_MC				(1 << 11)
#define		SOFT_RESET_RLC				(1 << 13)
#define		SOFT_RESET_ROM				(1 << 14)
#define		SOFT_RESET_SEM				(1 << 15)
#define		SOFT_RESET_VMC				(1 << 17)
#define		SOFT_RESET_DMA				(1 << 20)
#define		SOFT_RESET_TST				(1 << 21)
#define		SOFT_RESET_REGBB			(1 << 22)
#define		SOFT_RESET_ORB				(1 << 23)

/* display watermarks */
#define	DC_LB_MEMORY_SPLIT				  0x6b0c
#define	PRIORITY_A_CNT			                  0x6b18
#define		PRIORITY_MARK_MASK			  0x7fff
#define		PRIORITY_OFF				  (1 << 16)
#define		PRIORITY_ALWAYS_ON			  (1 << 20)
#define	PRIORITY_B_CNT			                  0x6b1c
#define	PIPE0_ARBITRATION_CONTROL3			  0x0bf0
#       define LATENCY_WATERMARK_MASK(x)                  ((x) << 16)
#define	PIPE0_LATENCY_CONTROL			          0x0bf4
#       define LATENCY_LOW_WATERMARK(x)                   ((x) << 0)
#       define LATENCY_HIGH_WATERMARK(x)                  ((x) << 16)

#define IH_RB_CNTL                                        0x3e00
#       define IH_RB_ENABLE                               (1 << 0)
#       define IH_IB_SIZE(x)                              ((x) << 1) /* log2 */
#       define IH_RB_FULL_DRAIN_ENABLE                    (1 << 6)
#       define IH_WPTR_WRITEBACK_ENABLE                   (1 << 8)
#       define IH_WPTR_WRITEBACK_TIMER(x)                 ((x) << 9) /* log2 */
#       define IH_WPTR_OVERFLOW_ENABLE                    (1 << 16)
#       define IH_WPTR_OVERFLOW_CLEAR                     (1 << 31)
#define IH_RB_BASE                                        0x3e04
#define IH_RB_RPTR                                        0x3e08
#define IH_RB_WPTR                                        0x3e0c
#       define RB_OVERFLOW                                (1 << 0)
#       define WPTR_OFFSET_MASK                           0x3fffc
#define IH_RB_WPTR_ADDR_HI                                0x3e10
#define IH_RB_WPTR_ADDR_LO                                0x3e14
#define IH_CNTL                                           0x3e18
#       define ENABLE_INTR                                (1 << 0)
#       define IH_MC_SWAP(x)                              ((x) << 1)
#       define IH_MC_SWAP_NONE                            0
#       define IH_MC_SWAP_16BIT                           1
#       define IH_MC_SWAP_32BIT                           2
#       define IH_MC_SWAP_64BIT                           3
#       define RPTR_REARM                                 (1 << 4)
#       define MC_WRREQ_CREDIT(x)                         ((x) << 15)
#       define MC_WR_CLEAN_CNT(x)                         ((x) << 20)

#define CP_INT_CNTL                                     0xc124
#       define CNTX_BUSY_INT_ENABLE                     (1 << 19)
#       define CNTX_EMPTY_INT_ENABLE                    (1 << 20)
#       define SCRATCH_INT_ENABLE                       (1 << 25)
#       define TIME_STAMP_INT_ENABLE                    (1 << 26)
#       define IB2_INT_ENABLE                           (1 << 29)
#       define IB1_INT_ENABLE                           (1 << 30)
#       define RB_INT_ENABLE                            (1 << 31)
#define CP_INT_STATUS                                   0xc128
#       define SCRATCH_INT_STAT                         (1 << 25)
#       define TIME_STAMP_INT_STAT                      (1 << 26)
#       define IB2_INT_STAT                             (1 << 29)
#       define IB1_INT_STAT                             (1 << 30)
#       define RB_INT_STAT                              (1 << 31)

#define GRBM_INT_CNTL                                   0x8060
#       define RDERR_INT_ENABLE                         (1 << 0)
#       define GUI_IDLE_INT_ENABLE                      (1 << 19)

/* 0x6e98, 0x7a98, 0x10698, 0x11298, 0x11e98, 0x12a98 */
#define CRTC_STATUS_FRAME_COUNT                         0x6e98

/* 0x6bb8, 0x77b8, 0x103b8, 0x10fb8, 0x11bb8, 0x127b8 */
#define VLINE_STATUS                                    0x6bb8
#       define VLINE_OCCURRED                           (1 << 0)
#       define VLINE_ACK                                (1 << 4)
#       define VLINE_STAT                               (1 << 12)
#       define VLINE_INTERRUPT                          (1 << 16)
#       define VLINE_INTERRUPT_TYPE                     (1 << 17)
/* 0x6bbc, 0x77bc, 0x103bc, 0x10fbc, 0x11bbc, 0x127bc */
#define VBLANK_STATUS                                   0x6bbc
#       define VBLANK_OCCURRED                          (1 << 0)
#       define VBLANK_ACK                               (1 << 4)
#       define VBLANK_STAT                              (1 << 12)
#       define VBLANK_INTERRUPT                         (1 << 16)
#       define VBLANK_INTERRUPT_TYPE                    (1 << 17)

/* 0x6b40, 0x7740, 0x10340, 0x10f40, 0x11b40, 0x12740 */
#define INT_MASK                                        0x6b40
#       define VBLANK_INT_MASK                          (1 << 0)
#       define VLINE_INT_MASK                           (1 << 4)

#define DISP_INTERRUPT_STATUS                           0x60f4
#       define LB_D1_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D1_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD1_INTERRUPT                        (1 << 17)
#       define DC_HPD1_RX_INTERRUPT                     (1 << 18)
#       define DACA_AUTODETECT_INTERRUPT                (1 << 22)
#       define DACB_AUTODETECT_INTERRUPT                (1 << 23)
#       define DC_I2C_SW_DONE_INTERRUPT                 (1 << 24)
#       define DC_I2C_HW_DONE_INTERRUPT                 (1 << 25)
#define DISP_INTERRUPT_STATUS_CONTINUE                  0x60f8
#       define LB_D2_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D2_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD2_INTERRUPT                        (1 << 17)
#       define DC_HPD2_RX_INTERRUPT                     (1 << 18)
#       define DISP_TIMER_INTERRUPT                     (1 << 24)
#define DISP_INTERRUPT_STATUS_CONTINUE2                 0x60fc
#       define LB_D3_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D3_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD3_INTERRUPT                        (1 << 17)
#       define DC_HPD3_RX_INTERRUPT                     (1 << 18)
#define DISP_INTERRUPT_STATUS_CONTINUE3                 0x6100
#       define LB_D4_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D4_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD4_INTERRUPT                        (1 << 17)
#       define DC_HPD4_RX_INTERRUPT                     (1 << 18)
#define DISP_INTERRUPT_STATUS_CONTINUE4                 0x614c
#       define LB_D5_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D5_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD5_INTERRUPT                        (1 << 17)
#       define DC_HPD5_RX_INTERRUPT                     (1 << 18)
#define DISP_INTERRUPT_STATUS_CONTINUE5                 0x6150
#       define LB_D6_VLINE_INTERRUPT                    (1 << 2)
#       define LB_D6_VBLANK_INTERRUPT                   (1 << 3)
#       define DC_HPD6_INTERRUPT                        (1 << 17)
#       define DC_HPD6_RX_INTERRUPT                     (1 << 18)

/* 0x6858, 0x7458, 0x10058, 0x10c58, 0x11858, 0x12458 */
#define GRPH_INT_STATUS                                 0x6858
#       define GRPH_PFLIP_INT_OCCURRED                  (1 << 0)
#       define GRPH_PFLIP_INT_CLEAR                     (1 << 8)
/* 0x685c, 0x745c, 0x1005c, 0x10c5c, 0x1185c, 0x1245c */
#define	GRPH_INT_CONTROL			        0x685c
#       define GRPH_PFLIP_INT_MASK                      (1 << 0)
#       define GRPH_PFLIP_INT_TYPE                      (1 << 8)

#define	DACA_AUTODETECT_INT_CONTROL			0x66c8
#define	DACB_AUTODETECT_INT_CONTROL			0x67c8

#define DC_HPD1_INT_STATUS                              0x601c
#define DC_HPD2_INT_STATUS                              0x6028
#define DC_HPD3_INT_STATUS                              0x6034
#define DC_HPD4_INT_STATUS                              0x6040
#define DC_HPD5_INT_STATUS                              0x604c
#define DC_HPD6_INT_STATUS                              0x6058
#       define DC_HPDx_INT_STATUS                       (1 << 0)
#       define DC_HPDx_SENSE                            (1 << 1)
#       define DC_HPDx_RX_INT_STATUS                    (1 << 8)

#define DC_HPD1_INT_CONTROL                             0x6020
#define DC_HPD2_INT_CONTROL                             0x602c
#define DC_HPD3_INT_CONTROL                             0x6038
#define DC_HPD4_INT_CONTROL                             0x6044
#define DC_HPD5_INT_CONTROL                             0x6050
#define DC_HPD6_INT_CONTROL                             0x605c
#       define DC_HPDx_INT_ACK                          (1 << 0)
#       define DC_HPDx_INT_POLARITY                     (1 << 8)
#       define DC_HPDx_INT_EN                           (1 << 16)
#       define DC_HPDx_RX_INT_ACK                       (1 << 20)
#       define DC_HPDx_RX_INT_EN                        (1 << 24)

#define DC_HPD1_CONTROL                                   0x6024
#define DC_HPD2_CONTROL                                   0x6030
#define DC_HPD3_CONTROL                                   0x603c
#define DC_HPD4_CONTROL                                   0x6048
#define DC_HPD5_CONTROL                                   0x6054
#define DC_HPD6_CONTROL                                   0x6060
#       define DC_HPDx_CONNECTION_TIMER(x)                ((x) << 0)
#       define DC_HPDx_RX_INT_TIMER(x)                    ((x) << 16)
#       define DC_HPDx_EN                                 (1 << 28)

/* ASYNC DMA */
#define DMA_RB_RPTR                                       0xd008
#define DMA_RB_WPTR                                       0xd00c

#define DMA_CNTL                                          0xd02c
#       define TRAP_ENABLE                                (1 << 0)
#       define SEM_INCOMPLETE_INT_ENABLE                  (1 << 1)
#       define SEM_WAIT_INT_ENABLE                        (1 << 2)
#       define DATA_SWAP_ENABLE                           (1 << 3)
#       define FENCE_SWAP_ENABLE                          (1 << 4)
#       define CTXEMPTY_INT_ENABLE                        (1 << 28)
#define DMA_TILING_CONFIG  				  0xD0B8

#define CAYMAN_DMA1_CNTL                                  0xd82c

/* async DMA packets */
#define DMA_PACKET(cmd, sub_cmd, n) ((((cmd) & 0xF) << 28) |    \
                                    (((sub_cmd) & 0xFF) << 20) |\
                                    (((n) & 0xFFFFF) << 0))
#define GET_DMA_CMD(h) (((h) & 0xf0000000) >> 28)
#define GET_DMA_COUNT(h) ((h) & 0x000fffff)
#define GET_DMA_SUB_CMD(h) (((h) & 0x0ff00000) >> 20)

/* async DMA Packet types */
#define	DMA_PACKET_WRITE                        0x2
#define	DMA_PACKET_COPY                         0x3
#define	DMA_PACKET_INDIRECT_BUFFER              0x4
#define	DMA_PACKET_SEMAPHORE                    0x5
#define	DMA_PACKET_FENCE                        0x6
#define	DMA_PACKET_TRAP                         0x7
#define	DMA_PACKET_SRBM_WRITE                   0x9
#define	DMA_PACKET_CONSTANT_FILL                0xd
#define	DMA_PACKET_NOP                          0xf

/* PCIE link stuff */
#define PCIE_LC_TRAINING_CNTL                             0xa1 /* PCIE_P */
#define PCIE_LC_LINK_WIDTH_CNTL                           0xa2 /* PCIE_P */
#       define LC_LINK_WIDTH_SHIFT                        0
#       define LC_LINK_WIDTH_MASK                         0x7
#       define LC_LINK_WIDTH_X0                           0
#       define LC_LINK_WIDTH_X1                           1
#       define LC_LINK_WIDTH_X2                           2
#       define LC_LINK_WIDTH_X4                           3
#       define LC_LINK_WIDTH_X8                           4
#       define LC_LINK_WIDTH_X16                          6
#       define LC_LINK_WIDTH_RD_SHIFT                     4
#       define LC_LINK_WIDTH_RD_MASK                      0x70
#       define LC_RECONFIG_ARC_MISSING_ESCAPE             (1 << 7)
#       define LC_RECONFIG_NOW                            (1 << 8)
#       define LC_RENEGOTIATION_SUPPORT                   (1 << 9)
#       define LC_RENEGOTIATE_EN                          (1 << 10)
#       define LC_SHORT_RECONFIG_EN                       (1 << 11)
#       define LC_UPCONFIGURE_SUPPORT                     (1 << 12)
#       define LC_UPCONFIGURE_DIS                         (1 << 13)
#define PCIE_LC_SPEED_CNTL                                0xa4 /* PCIE_P */
#       define LC_GEN2_EN_STRAP                           (1 << 0)
#       define LC_TARGET_LINK_SPEED_OVERRIDE_EN           (1 << 1)
#       define LC_FORCE_EN_HW_SPEED_CHANGE                (1 << 5)
#       define LC_FORCE_DIS_HW_SPEED_CHANGE               (1 << 6)
#       define LC_SPEED_CHANGE_ATTEMPTS_ALLOWED_MASK      (0x3 << 8)
#       define LC_SPEED_CHANGE_ATTEMPTS_ALLOWED_SHIFT     3
#       define LC_CURRENT_DATA_RATE                       (1 << 11)
#       define LC_VOLTAGE_TIMER_SEL_MASK                  (0xf << 14)
#       define LC_CLR_FAILED_SPD_CHANGE_CNT               (1 << 21)
#       define LC_OTHER_SIDE_EVER_SENT_GEN2               (1 << 23)
#       define LC_OTHER_SIDE_SUPPORTS_GEN2                (1 << 24)
#define MM_CFGREGS_CNTL                                   0x544c
#       define MM_WR_TO_CFG_EN                            (1 << 3)
#define LINK_CNTL2                                        0x88 /* F0 */
#       define TARGET_LINK_SPEED_MASK                     (0xf << 0)
#       define SELECTABLE_DEEMPHASIS                      (1 << 6)


/*
 * UVD
 */
#define UVD_UDEC_ADDR_CONFIG				0xef4c
#define UVD_UDEC_DB_ADDR_CONFIG				0xef50
#define UVD_UDEC_DBW_ADDR_CONFIG			0xef54
#define UVD_RBC_RB_RPTR					0xf690
#define UVD_RBC_RB_WPTR					0xf694

/*
 * PM4
 */
#define PACKET0(reg, n)	((RADEON_PACKET_TYPE0 << 30) |			\
			 (((reg) >> 2) & 0xFFFF) |			\
			 ((n) & 0x3FFF) << 16)
#define CP_PACKET2			0x80000000
#define		PACKET2_PAD_SHIFT		0
#define		PACKET2_PAD_MASK		(0x3fffffff << 0)

#define PACKET2(v)	(CP_PACKET2 | REG_SET(PACKET2_PAD, (v)))

#define PACKET3(op, n)	((RADEON_PACKET_TYPE3 << 30) |			\
			 (((op) & 0xFF) << 8) |				\
			 ((n) & 0x3FFF) << 16)

/* Packet 3 types */
#define	PACKET3_NOP					0x10
#define	PACKET3_SET_BASE				0x11
#define	PACKET3_CLEAR_STATE				0x12
#define	PACKET3_INDEX_BUFFER_SIZE			0x13
#define	PACKET3_DISPATCH_DIRECT				0x15
#define	PACKET3_DISPATCH_INDIRECT			0x16
#define	PACKET3_INDIRECT_BUFFER_END			0x17
#define	PACKET3_MODE_CONTROL				0x18
#define	PACKET3_SET_PREDICATION				0x20
#define	PACKET3_REG_RMW					0x21
#define	PACKET3_COND_EXEC				0x22
#define	PACKET3_PRED_EXEC				0x23
#define	PACKET3_DRAW_INDIRECT				0x24
#define	PACKET3_DRAW_INDEX_INDIRECT			0x25
#define	PACKET3_INDEX_BASE				0x26
#define	PACKET3_DRAW_INDEX_2				0x27
#define	PACKET3_CONTEXT_CONTROL				0x28
#define	PACKET3_DRAW_INDEX_OFFSET			0x29
#define	PACKET3_INDEX_TYPE				0x2A
#define	PACKET3_DRAW_INDEX				0x2B
#define	PACKET3_DRAW_INDEX_AUTO				0x2D
#define	PACKET3_DRAW_INDEX_IMMD				0x2E
#define	PACKET3_NUM_INSTANCES				0x2F
#define	PACKET3_DRAW_INDEX_MULTI_AUTO			0x30
#define	PACKET3_STRMOUT_BUFFER_UPDATE			0x34
#define	PACKET3_DRAW_INDEX_OFFSET_2			0x35
#define	PACKET3_DRAW_INDEX_MULTI_ELEMENT		0x36
#define	PACKET3_MEM_SEMAPHORE				0x39
#define	PACKET3_MPEG_INDEX				0x3A
#define	PACKET3_COPY_DW					0x3B
#define	PACKET3_WAIT_REG_MEM				0x3C
#define	PACKET3_MEM_WRITE				0x3D
#define	PACKET3_INDIRECT_BUFFER				0x32
#define	PACKET3_CP_DMA					0x41
/* 1. header
 * 2. SRC_ADDR_LO or DATA [31:0]
 * 3. CP_SYNC [31] | SRC_SEL [30:29] | ENGINE [27] | DST_SEL [21:20] |
 *    SRC_ADDR_HI [7:0]
 * 4. DST_ADDR_LO [31:0]
 * 5. DST_ADDR_HI [7:0]
 * 6. COMMAND [29:22] | BYTE_COUNT [20:0]
 */
#              define PACKET3_CP_DMA_DST_SEL(x)    ((x) << 20)
                /* 0 - SRC_ADDR
		 * 1 - GDS
		 */
#              define PACKET3_CP_DMA_ENGINE(x)     ((x) << 27)
                /* 0 - ME
		 * 1 - PFP
		 */
#              define PACKET3_CP_DMA_SRC_SEL(x)    ((x) << 29)
                /* 0 - SRC_ADDR
		 * 1 - GDS
		 * 2 - DATA
		 */
#              define PACKET3_CP_DMA_CP_SYNC       (1 << 31)
/* COMMAND */
#              define PACKET3_CP_DMA_DIS_WC        (1 << 21)
#              define PACKET3_CP_DMA_CMD_SRC_SWAP(x) ((x) << 23)
                /* 0 - none
		 * 1 - 8 in 16
		 * 2 - 8 in 32
		 * 3 - 8 in 64
		 */
#              define PACKET3_CP_DMA_CMD_DST_SWAP(x) ((x) << 24)
                /* 0 - none
		 * 1 - 8 in 16
		 * 2 - 8 in 32
		 * 3 - 8 in 64
		 */
#              define PACKET3_CP_DMA_CMD_SAS       (1 << 26)
                /* 0 - memory
		 * 1 - register
		 */
#              define PACKET3_CP_DMA_CMD_DAS       (1 << 27)
                /* 0 - memory
		 * 1 - register
		 */
#              define PACKET3_CP_DMA_CMD_SAIC      (1 << 28)
#              define PACKET3_CP_DMA_CMD_DAIC      (1 << 29)
#define	PACKET3_SURFACE_SYNC				0x43
#              define PACKET3_CB0_DEST_BASE_ENA    (1 << 6)
#              define PACKET3_CB1_DEST_BASE_ENA    (1 << 7)
#              define PACKET3_CB2_DEST_BASE_ENA    (1 << 8)
#              define PACKET3_CB3_DEST_BASE_ENA    (1 << 9)
#              define PACKET3_CB4_DEST_BASE_ENA    (1 << 10)
#              define PACKET3_CB5_DEST_BASE_ENA    (1 << 11)
#              define PACKET3_CB6_DEST_BASE_ENA    (1 << 12)
#              define PACKET3_CB7_DEST_BASE_ENA    (1 << 13)
#              define PACKET3_DB_DEST_BASE_ENA     (1 << 14)
#              define PACKET3_CB8_DEST_BASE_ENA    (1 << 15)
#              define PACKET3_CB9_DEST_BASE_ENA    (1 << 16)
#              define PACKET3_CB10_DEST_BASE_ENA   (1 << 17)
#              define PACKET3_CB11_DEST_BASE_ENA   (1 << 18)
#              define PACKET3_FULL_CACHE_ENA       (1 << 20)
#              define PACKET3_TC_ACTION_ENA        (1 << 23)
#              define PACKET3_VC_ACTION_ENA        (1 << 24)
#              define PACKET3_CB_ACTION_ENA        (1 << 25)
#              define PACKET3_DB_ACTION_ENA        (1 << 26)
#              define PACKET3_SH_ACTION_ENA        (1 << 27)
#              define PACKET3_SX_ACTION_ENA        (1 << 28)
#define	PACKET3_ME_INITIALIZE				0x44
#define		PACKET3_ME_INITIALIZE_DEVICE_ID(x) ((x) << 16)
#define	PACKET3_COND_WRITE				0x45
#define	PACKET3_EVENT_WRITE				0x46
#define	PACKET3_EVENT_WRITE_EOP				0x47
#define	PACKET3_EVENT_WRITE_EOS				0x48
#define	PACKET3_PREAMBLE_CNTL				0x4A
#              define PACKET3_PREAMBLE_BEGIN_CLEAR_STATE     (2 << 28)
#              define PACKET3_PREAMBLE_END_CLEAR_STATE       (3 << 28)
#define	PACKET3_RB_OFFSET				0x4B
#define	PACKET3_ALU_PS_CONST_BUFFER_COPY		0x4C
#define	PACKET3_ALU_VS_CONST_BUFFER_COPY		0x4D
#define	PACKET3_ALU_PS_CONST_UPDATE		        0x4E
#define	PACKET3_ALU_VS_CONST_UPDATE		        0x4F
#define	PACKET3_ONE_REG_WRITE				0x57
#define	PACKET3_SET_CONFIG_REG				0x68
#define		PACKET3_SET_CONFIG_REG_START			0x00008000
#define		PACKET3_SET_CONFIG_REG_END			0x0000ac00
#define	PACKET3_SET_CONTEXT_REG				0x69
#define		PACKET3_SET_CONTEXT_REG_START			0x00028000
#define		PACKET3_SET_CONTEXT_REG_END			0x00029000
#define	PACKET3_SET_ALU_CONST				0x6A
/* alu const buffers only; no reg file */
#define	PACKET3_SET_BOOL_CONST				0x6B
#define		PACKET3_SET_BOOL_CONST_START			0x0003a500
#define		PACKET3_SET_BOOL_CONST_END			0x0003a518
#define	PACKET3_SET_LOOP_CONST				0x6C
#define		PACKET3_SET_LOOP_CONST_START			0x0003a200
#define		PACKET3_SET_LOOP_CONST_END			0x0003a500
#define	PACKET3_SET_RESOURCE				0x6D
#define		PACKET3_SET_RESOURCE_START			0x00030000
#define		PACKET3_SET_RESOURCE_END			0x00038000
#define	PACKET3_SET_SAMPLER				0x6E
#define		PACKET3_SET_SAMPLER_START			0x0003c000
#define		PACKET3_SET_SAMPLER_END				0x0003c600
#define	PACKET3_SET_CTL_CONST				0x6F
#define		PACKET3_SET_CTL_CONST_START			0x0003cff0
#define		PACKET3_SET_CTL_CONST_END			0x0003ff0c
#define	PACKET3_SET_RESOURCE_OFFSET			0x70
#define	PACKET3_SET_ALU_CONST_VS			0x71
#define	PACKET3_SET_ALU_CONST_DI			0x72
#define	PACKET3_SET_CONTEXT_REG_INDIRECT		0x73
#define	PACKET3_SET_RESOURCE_INDIRECT			0x74
#define	PACKET3_SET_APPEND_CNT			        0x75

#define	SQ_RESOURCE_CONSTANT_WORD7_0				0x3001c
#define		S__SQ_CONSTANT_TYPE(x)			(((x) & 3) << 30)
#define		G__SQ_CONSTANT_TYPE(x)			(((x) >> 30) & 3)
#define			SQ_TEX_VTX_INVALID_TEXTURE			0x0
#define			SQ_TEX_VTX_INVALID_BUFFER			0x1
#define			SQ_TEX_VTX_VALID_TEXTURE			0x2
#define			SQ_TEX_VTX_VALID_BUFFER				0x3

#define VGT_VTX_VECT_EJECT_REG				0x88b0

#define SQ_CONST_MEM_BASE				0x8df8

#define SQ_ESGS_RING_BASE				0x8c40
#define SQ_ESGS_RING_SIZE				0x8c44
#define SQ_GSVS_RING_BASE				0x8c48
#define SQ_GSVS_RING_SIZE				0x8c4c
#define SQ_ESTMP_RING_BASE				0x8c50
#define SQ_ESTMP_RING_SIZE				0x8c54
#define SQ_GSTMP_RING_BASE				0x8c58
#define SQ_GSTMP_RING_SIZE				0x8c5c
#define SQ_VSTMP_RING_BASE				0x8c60
#define SQ_VSTMP_RING_SIZE				0x8c64
#define SQ_PSTMP_RING_BASE				0x8c68
#define SQ_PSTMP_RING_SIZE				0x8c6c
#define SQ_LSTMP_RING_BASE				0x8e10
#define SQ_LSTMP_RING_SIZE				0x8e14
#define SQ_HSTMP_RING_BASE				0x8e18
#define SQ_HSTMP_RING_SIZE				0x8e1c
#define VGT_TF_RING_SIZE				0x8988

#define SQ_ESGS_RING_ITEMSIZE				0x28900
#define SQ_GSVS_RING_ITEMSIZE				0x28904
#define SQ_ESTMP_RING_ITEMSIZE				0x28908
#define SQ_GSTMP_RING_ITEMSIZE				0x2890c
#define SQ_VSTMP_RING_ITEMSIZE				0x28910
#define SQ_PSTMP_RING_ITEMSIZE				0x28914
#define SQ_LSTMP_RING_ITEMSIZE				0x28830
#define SQ_HSTMP_RING_ITEMSIZE				0x28834

#define SQ_GS_VERT_ITEMSIZE				0x2891c
#define SQ_GS_VERT_ITEMSIZE_1				0x28920
#define SQ_GS_VERT_ITEMSIZE_2				0x28924
#define SQ_GS_VERT_ITEMSIZE_3				0x28928
#define SQ_GSVS_RING_OFFSET_1				0x2892c
#define SQ_GSVS_RING_OFFSET_2				0x28930
#define SQ_GSVS_RING_OFFSET_3				0x28934

#define SQ_ALU_CONST_BUFFER_SIZE_PS_0			0x28140
#define SQ_ALU_CONST_BUFFER_SIZE_HS_0			0x28f80

#define SQ_ALU_CONST_CACHE_PS_0				0x28940
#define SQ_ALU_CONST_CACHE_PS_1				0x28944
#define SQ_ALU_CONST_CACHE_PS_2				0x28948
#define SQ_ALU_CONST_CACHE_PS_3				0x2894c
#define SQ_ALU_CONST_CACHE_PS_4				0x28950
#define SQ_ALU_CONST_CACHE_PS_5				0x28954
#define SQ_ALU_CONST_CACHE_PS_6				0x28958
#define SQ_ALU_CONST_CACHE_PS_7				0x2895c
#define SQ_ALU_CONST_CACHE_PS_8				0x28960
#define SQ_ALU_CONST_CACHE_PS_9				0x28964
#define SQ_ALU_CONST_CACHE_PS_10			0x28968
#define SQ_ALU_CONST_CACHE_PS_11			0x2896c
#define SQ_ALU_CONST_CACHE_PS_12			0x28970
#define SQ_ALU_CONST_CACHE_PS_13			0x28974
#define SQ_ALU_CONST_CACHE_PS_14			0x28978
#define SQ_ALU_CONST_CACHE_PS_15			0x2897c
#define SQ_ALU_CONST_CACHE_VS_0				0x28980
#define SQ_ALU_CONST_CACHE_VS_1				0x28984
#define SQ_ALU_CONST_CACHE_VS_2				0x28988
#define SQ_ALU_CONST_CACHE_VS_3				0x2898c
#define SQ_ALU_CONST_CACHE_VS_4				0x28990
#define SQ_ALU_CONST_CACHE_VS_5				0x28994
#define SQ_ALU_CONST_CACHE_VS_6				0x28998
#define SQ_ALU_CONST_CACHE_VS_7				0x2899c
#define SQ_ALU_CONST_CACHE_VS_8				0x289a0
#define SQ_ALU_CONST_CACHE_VS_9				0x289a4
#define SQ_ALU_CONST_CACHE_VS_10			0x289a8
#define SQ_ALU_CONST_CACHE_VS_11			0x289ac
#define SQ_ALU_CONST_CACHE_VS_12			0x289b0
#define SQ_ALU_CONST_CACHE_VS_13			0x289b4
#define SQ_ALU_CONST_CACHE_VS_14			0x289b8
#define SQ_ALU_CONST_CACHE_VS_15			0x289bc
#define SQ_ALU_CONST_CACHE_GS_0				0x289c0
#define SQ_ALU_CONST_CACHE_GS_1				0x289c4
#define SQ_ALU_CONST_CACHE_GS_2				0x289c8
#define SQ_ALU_CONST_CACHE_GS_3				0x289cc
#define SQ_ALU_CONST_CACHE_GS_4				0x289d0
#define SQ_ALU_CONST_CACHE_GS_5				0x289d4
#define SQ_ALU_CONST_CACHE_GS_6				0x289d8
#define SQ_ALU_CONST_CACHE_GS_7				0x289dc
#define SQ_ALU_CONST_CACHE_GS_8				0x289e0
#define SQ_ALU_CONST_CACHE_GS_9				0x289e4
#define SQ_ALU_CONST_CACHE_GS_10			0x289e8
#define SQ_ALU_CONST_CACHE_GS_11			0x289ec
#define SQ_ALU_CONST_CACHE_GS_12			0x289f0
#define SQ_ALU_CONST_CACHE_GS_13			0x289f4
#define SQ_ALU_CONST_CACHE_GS_14			0x289f8
#define SQ_ALU_CONST_CACHE_GS_15			0x289fc
#define SQ_ALU_CONST_CACHE_HS_0				0x28f00
#define SQ_ALU_CONST_CACHE_HS_1				0x28f04
#define SQ_ALU_CONST_CACHE_HS_2				0x28f08
#define SQ_ALU_CONST_CACHE_HS_3				0x28f0c
#define SQ_ALU_CONST_CACHE_HS_4				0x28f10
#define SQ_ALU_CONST_CACHE_HS_5				0x28f14
#define SQ_ALU_CONST_CACHE_HS_6				0x28f18
#define SQ_ALU_CONST_CACHE_HS_7				0x28f1c
#define SQ_ALU_CONST_CACHE_HS_8				0x28f20
#define SQ_ALU_CONST_CACHE_HS_9				0x28f24
#define SQ_ALU_CONST_CACHE_HS_10			0x28f28
#define SQ_ALU_CONST_CACHE_HS_11			0x28f2c
#define SQ_ALU_CONST_CACHE_HS_12			0x28f30
#define SQ_ALU_CONST_CACHE_HS_13			0x28f34
#define SQ_ALU_CONST_CACHE_HS_14			0x28f38
#define SQ_ALU_CONST_CACHE_HS_15			0x28f3c
#define SQ_ALU_CONST_CACHE_LS_0				0x28f40
#define SQ_ALU_CONST_CACHE_LS_1				0x28f44
#define SQ_ALU_CONST_CACHE_LS_2				0x28f48
#define SQ_ALU_CONST_CACHE_LS_3				0x28f4c
#define SQ_ALU_CONST_CACHE_LS_4				0x28f50
#define SQ_ALU_CONST_CACHE_LS_5				0x28f54
#define SQ_ALU_CONST_CACHE_LS_6				0x28f58
#define SQ_ALU_CONST_CACHE_LS_7				0x28f5c
#define SQ_ALU_CONST_CACHE_LS_8				0x28f60
#define SQ_ALU_CONST_CACHE_LS_9				0x28f64
#define SQ_ALU_CONST_CACHE_LS_10			0x28f68
#define SQ_ALU_CONST_CACHE_LS_11			0x28f6c
#define SQ_ALU_CONST_CACHE_LS_12			0x28f70
#define SQ_ALU_CONST_CACHE_LS_13			0x28f74
#define SQ_ALU_CONST_CACHE_LS_14			0x28f78
#define SQ_ALU_CONST_CACHE_LS_15			0x28f7c

#define PA_SC_SCREEN_SCISSOR_TL                         0x28030
#define PA_SC_GENERIC_SCISSOR_TL                        0x28240
#define PA_SC_WINDOW_SCISSOR_TL                         0x28204

#define VGT_PRIMITIVE_TYPE                              0x8958
#define VGT_INDEX_TYPE                                  0x895C

#define VGT_NUM_INDICES                                 0x8970

#define VGT_COMPUTE_DIM_X                               0x8990
#define VGT_COMPUTE_DIM_Y                               0x8994
#define VGT_COMPUTE_DIM_Z                               0x8998
#define VGT_COMPUTE_START_X                             0x899C
#define VGT_COMPUTE_START_Y                             0x89A0
#define VGT_COMPUTE_START_Z                             0x89A4
#define VGT_COMPUTE_INDEX                               0x89A8
#define VGT_COMPUTE_THREAD_GROUP_SIZE                   0x89AC
#define VGT_HS_OFFCHIP_PARAM                            0x89B0

#define DB_DEBUG					0x9830
#define DB_DEBUG2					0x9834
#define DB_DEBUG3					0x9838
#define DB_DEBUG4					0x983C
#define DB_WATERMARKS					0x9854
#define DB_DEPTH_CONTROL				0x28800
#define R_028800_DB_DEPTH_CONTROL                    0x028800
#define   S_028800_STENCIL_ENABLE(x)                   (((x) & 0x1) << 0)
#define   G_028800_STENCIL_ENABLE(x)                   (((x) >> 0) & 0x1)
#define   C_028800_STENCIL_ENABLE                      0xFFFFFFFE
#define   S_028800_Z_ENABLE(x)                         (((x) & 0x1) << 1)
#define   G_028800_Z_ENABLE(x)                         (((x) >> 1) & 0x1)
#define   C_028800_Z_ENABLE                            0xFFFFFFFD
#define   S_028800_Z_WRITE_ENABLE(x)                   (((x) & 0x1) << 2)
#define   G_028800_Z_WRITE_ENABLE(x)                   (((x) >> 2) & 0x1)
#define   C_028800_Z_WRITE_ENABLE                      0xFFFFFFFB
#define   S_028800_ZFUNC(x)                            (((x) & 0x7) << 4)
#define   G_028800_ZFUNC(x)                            (((x) >> 4) & 0x7)
#define   C_028800_ZFUNC                               0xFFFFFF8F
#define   S_028800_BACKFACE_ENABLE(x)                  (((x) & 0x1) << 7)
#define   G_028800_BACKFACE_ENABLE(x)                  (((x) >> 7) & 0x1)
#define   C_028800_BACKFACE_ENABLE                     0xFFFFFF7F
#define   S_028800_STENCILFUNC(x)                      (((x) & 0x7) << 8)
#define   G_028800_STENCILFUNC(x)                      (((x) >> 8) & 0x7)
#define   C_028800_STENCILFUNC                         0xFFFFF8FF
#define     V_028800_STENCILFUNC_NEVER                 0x00000000
#define     V_028800_STENCILFUNC_LESS                  0x00000001
#define     V_028800_STENCILFUNC_EQUAL                 0x00000002
#define     V_028800_STENCILFUNC_LEQUAL                0x00000003
#define     V_028800_STENCILFUNC_GREATER               0x00000004
#define     V_028800_STENCILFUNC_NOTEQUAL              0x00000005
#define     V_028800_STENCILFUNC_GEQUAL                0x00000006
#define     V_028800_STENCILFUNC_ALWAYS                0x00000007
#define   S_028800_STENCILFAIL(x)                      (((x) & 0x7) << 11)
#define   G_028800_STENCILFAIL(x)                      (((x) >> 11) & 0x7)
#define   C_028800_STENCILFAIL                         0xFFFFC7FF
#define     V_028800_STENCIL_KEEP                      0x00000000
#define     V_028800_STENCIL_ZERO                      0x00000001
#define     V_028800_STENCIL_REPLACE                   0x00000002
#define     V_028800_STENCIL_INCR                      0x00000003
#define     V_028800_STENCIL_DECR                      0x00000004
#define     V_028800_STENCIL_INVERT                    0x00000005
#define     V_028800_STENCIL_INCR_WRAP                 0x00000006
#define     V_028800_STENCIL_DECR_WRAP                 0x00000007
#define   S_028800_STENCILZPASS(x)                     (((x) & 0x7) << 14)
#define   G_028800_STENCILZPASS(x)                     (((x) >> 14) & 0x7)
#define   C_028800_STENCILZPASS                        0xFFFE3FFF
#define   S_028800_STENCILZFAIL(x)                     (((x) & 0x7) << 17)
#define   G_028800_STENCILZFAIL(x)                     (((x) >> 17) & 0x7)
#define   C_028800_STENCILZFAIL                        0xFFF1FFFF
#define   S_028800_STENCILFUNC_BF(x)                   (((x) & 0x7) << 20)
#define   G_028800_STENCILFUNC_BF(x)                   (((x) >> 20) & 0x7)
#define   C_028800_STENCILFUNC_BF                      0xFF8FFFFF
#define   S_028800_STENCILFAIL_BF(x)                   (((x) & 0x7) << 23)
#define   G_028800_STENCILFAIL_BF(x)                   (((x) >> 23) & 0x7)
#define   C_028800_STENCILFAIL_BF                      0xFC7FFFFF
#define   S_028800_STENCILZPASS_BF(x)                  (((x) & 0x7) << 26)
#define   G_028800_STENCILZPASS_BF(x)                  (((x) >> 26) & 0x7)
#define   C_028800_STENCILZPASS_BF                     0xE3FFFFFF
#define   S_028800_STENCILZFAIL_BF(x)                  (((x) & 0x7) << 29)
#define   G_028800_STENCILZFAIL_BF(x)                  (((x) >> 29) & 0x7)
#define   C_028800_STENCILZFAIL_BF                     0x1FFFFFFF
#define DB_DEPTH_VIEW					0x28008
#define R_028008_DB_DEPTH_VIEW                       0x00028008
#define   S_028008_SLICE_START(x)                      (((x) & 0x7FF) << 0)
#define   G_028008_SLICE_START(x)                      (((x) >> 0) & 0x7FF)
#define   C_028008_SLICE_START                         0xFFFFF800
#define   S_028008_SLICE_MAX(x)                        (((x) & 0x7FF) << 13)
#define   G_028008_SLICE_MAX(x)                        (((x) >> 13) & 0x7FF)
#define   C_028008_SLICE_MAX                           0xFF001FFF
#define DB_HTILE_DATA_BASE				0x28014
#define DB_HTILE_SURFACE				0x28abc
#define   S_028ABC_HTILE_WIDTH(x)                      (((x) & 0x1) << 0)
#define   G_028ABC_HTILE_WIDTH(x)                      (((x) >> 0) & 0x1)
#define   C_028ABC_HTILE_WIDTH                         0xFFFFFFFE
#define   S_028ABC_HTILE_HEIGHT(x)                      (((x) & 0x1) << 1)
#define   G_028ABC_HTILE_HEIGHT(x)                      (((x) >> 1) & 0x1)
#define   C_028ABC_HTILE_HEIGHT                         0xFFFFFFFD
#define   G_028ABC_LINEAR(x)                           (((x) >> 2) & 0x1)
#define DB_Z_INFO					0x28040
#       define Z_ARRAY_MODE(x)                          ((x) << 4)
#       define DB_TILE_SPLIT(x)                         (((x) & 0x7) << 8)
#       define DB_NUM_BANKS(x)                          (((x) & 0x3) << 12)
#       define DB_BANK_WIDTH(x)                         (((x) & 0x3) << 16)
#       define DB_BANK_HEIGHT(x)                        (((x) & 0x3) << 20)
#       define DB_MACRO_TILE_ASPECT(x)                  (((x) & 0x3) << 24)
#define R_028040_DB_Z_INFO                       0x028040
#define   S_028040_FORMAT(x)                           (((x) & 0x3) << 0)
#define   G_028040_FORMAT(x)                           (((x) >> 0) & 0x3)
#define   C_028040_FORMAT                              0xFFFFFFFC
#define     V_028040_Z_INVALID                     0x00000000
#define     V_028040_Z_16                          0x00000001
#define     V_028040_Z_24                          0x00000002
#define     V_028040_Z_32_FLOAT                    0x00000003
#define   S_028040_ARRAY_MODE(x)                       (((x) & 0xF) << 4)
#define   G_028040_ARRAY_MODE(x)                       (((x) >> 4) & 0xF)
#define   C_028040_ARRAY_MODE                          0xFFFFFF0F
#define   S_028040_READ_SIZE(x)                        (((x) & 0x1) << 28)
#define   G_028040_READ_SIZE(x)                        (((x) >> 28) & 0x1)
#define   C_028040_READ_SIZE                           0xEFFFFFFF
#define   S_028040_TILE_SURFACE_ENABLE(x)              (((x) & 0x1) << 29)
#define   G_028040_TILE_SURFACE_ENABLE(x)              (((x) >> 29) & 0x1)
#define   C_028040_TILE_SURFACE_ENABLE                 0xDFFFFFFF
#define   S_028040_ZRANGE_PRECISION(x)                 (((x) & 0x1) << 31)
#define   G_028040_ZRANGE_PRECISION(x)                 (((x) >> 31) & 0x1)
#define   C_028040_ZRANGE_PRECISION                    0x7FFFFFFF
#define   S_028040_TILE_SPLIT(x)                       (((x) & 0x7) << 8)
#define   G_028040_TILE_SPLIT(x)                       (((x) >> 8) & 0x7)
#define   S_028040_NUM_BANKS(x)                        (((x) & 0x3) << 12)
#define   G_028040_NUM_BANKS(x)                        (((x) >> 12) & 0x3)
#define   S_028040_BANK_WIDTH(x)                       (((x) & 0x3) << 16)
#define   G_028040_BANK_WIDTH(x)                       (((x) >> 16) & 0x3)
#define   S_028040_BANK_HEIGHT(x)                      (((x) & 0x3) << 20)
#define   G_028040_BANK_HEIGHT(x)                      (((x) >> 20) & 0x3)
#define   S_028040_MACRO_TILE_ASPECT(x)                (((x) & 0x3) << 24)
#define   G_028040_MACRO_TILE_ASPECT(x)                (((x) >> 24) & 0x3)
#define DB_STENCIL_INFO					0x28044
#define R_028044_DB_STENCIL_INFO                     0x028044
#define   S_028044_FORMAT(x)                           (((x) & 0x1) << 0)
#define   G_028044_FORMAT(x)                           (((x) >> 0) & 0x1)
#define   C_028044_FORMAT                              0xFFFFFFFE
#define	    V_028044_STENCIL_INVALID			0
#define	    V_028044_STENCIL_8				1
#define   G_028044_TILE_SPLIT(x)                       (((x) >> 8) & 0x7)
#define DB_Z_READ_BASE					0x28048
#define DB_STENCIL_READ_BASE				0x2804c
#define DB_Z_WRITE_BASE					0x28050
#define DB_STENCIL_WRITE_BASE				0x28054
#define DB_DEPTH_SIZE					0x28058
#define R_028058_DB_DEPTH_SIZE                       0x028058
#define   S_028058_PITCH_TILE_MAX(x)                   (((x) & 0x7FF) << 0)
#define   G_028058_PITCH_TILE_MAX(x)                   (((x) >> 0) & 0x7FF)
#define   C_028058_PITCH_TILE_MAX                      0xFFFFF800
#define   S_028058_HEIGHT_TILE_MAX(x)                   (((x) & 0x7FF) << 11)
#define   G_028058_HEIGHT_TILE_MAX(x)                   (((x) >> 11) & 0x7FF)
#define   C_028058_HEIGHT_TILE_MAX                      0xFFC007FF
#define R_02805C_DB_DEPTH_SLICE                      0x02805C
#define   S_02805C_SLICE_TILE_MAX(x)                   (((x) & 0x3FFFFF) << 0)
#define   G_02805C_SLICE_TILE_MAX(x)                   (((x) >> 0) & 0x3FFFFF)
#define   C_02805C_SLICE_TILE_MAX                      0xFFC00000

#define SQ_PGM_START_PS					0x28840
#define SQ_PGM_START_VS					0x2885c
#define SQ_PGM_START_GS					0x28874
#define SQ_PGM_START_ES					0x2888c
#define SQ_PGM_START_FS					0x288a4
#define SQ_PGM_START_HS					0x288b8
#define SQ_PGM_START_LS					0x288d0

#define	VGT_STRMOUT_BUFFER_BASE_0			0x28AD8
#define	VGT_STRMOUT_BUFFER_BASE_1			0x28AE8
#define	VGT_STRMOUT_BUFFER_BASE_2			0x28AF8
#define	VGT_STRMOUT_BUFFER_BASE_3			0x28B08
#define VGT_STRMOUT_BUFFER_SIZE_0			0x28AD0
#define VGT_STRMOUT_BUFFER_SIZE_1			0x28AE0
#define VGT_STRMOUT_BUFFER_SIZE_2			0x28AF0
#define VGT_STRMOUT_BUFFER_SIZE_3			0x28B00
#define VGT_STRMOUT_CONFIG				0x28b94
#define VGT_STRMOUT_BUFFER_CONFIG			0x28b98

#define CB_TARGET_MASK					0x28238
#define CB_SHADER_MASK					0x2823c

#define GDS_ADDR_BASE					0x28720

#define	CB_IMMED0_BASE					0x28b9c
#define	CB_IMMED1_BASE					0x28ba0
#define	CB_IMMED2_BASE					0x28ba4
#define	CB_IMMED3_BASE					0x28ba8
#define	CB_IMMED4_BASE					0x28bac
#define	CB_IMMED5_BASE					0x28bb0
#define	CB_IMMED6_BASE					0x28bb4
#define	CB_IMMED7_BASE					0x28bb8
#define	CB_IMMED8_BASE					0x28bbc
#define	CB_IMMED9_BASE					0x28bc0
#define	CB_IMMED10_BASE					0x28bc4
#define	CB_IMMED11_BASE					0x28bc8

/* all 12 CB blocks have these regs */
#define	CB_COLOR0_BASE					0x28c60
#define	CB_COLOR0_PITCH					0x28c64
#define	CB_COLOR0_SLICE					0x28c68
#define	CB_COLOR0_VIEW					0x28c6c
#define R_028C6C_CB_COLOR0_VIEW                      0x00028C6C
#define   S_028C6C_SLICE_START(x)                      (((x) & 0x7FF) << 0)
#define   G_028C6C_SLICE_START(x)                      (((x) >> 0) & 0x7FF)
#define   C_028C6C_SLICE_START                         0xFFFFF800
#define   S_028C6C_SLICE_MAX(x)                        (((x) & 0x7FF) << 13)
#define   G_028C6C_SLICE_MAX(x)                        (((x) >> 13) & 0x7FF)
#define   C_028C6C_SLICE_MAX                           0xFF001FFF
#define R_028C70_CB_COLOR0_INFO                      0x028C70
#define   S_028C70_ENDIAN(x)                           (((x) & 0x3) << 0)
#define   G_028C70_ENDIAN(x)                           (((x) >> 0) & 0x3)
#define   C_028C70_ENDIAN                              0xFFFFFFFC
#define   S_028C70_FORMAT(x)                           (((x) & 0x3F) << 2)
#define   G_028C70_FORMAT(x)                           (((x) >> 2) & 0x3F)
#define   C_028C70_FORMAT                              0xFFFFFF03
#define     V_028C70_COLOR_INVALID                     0x00000000
#define     V_028C70_COLOR_8                           0x00000001
#define     V_028C70_COLOR_4_4                         0x00000002
#define     V_028C70_COLOR_3_3_2                       0x00000003
#define     V_028C70_COLOR_16                          0x00000005
#define     V_028C70_COLOR_16_FLOAT                    0x00000006
#define     V_028C70_COLOR_8_8                         0x00000007
#define     V_028C70_COLOR_5_6_5                       0x00000008
#define     V_028C70_COLOR_6_5_5                       0x00000009
#define     V_028C70_COLOR_1_5_5_5                     0x0000000A
#define     V_028C70_COLOR_4_4_4_4                     0x0000000B
#define     V_028C70_COLOR_5_5_5_1                     0x0000000C
#define     V_028C70_COLOR_32                          0x0000000D
#define     V_028C70_COLOR_32_FLOAT                    0x0000000E
#define     V_028C70_COLOR_16_16                       0x0000000F
#define     V_028C70_COLOR_16_16_FLOAT                 0x00000010
#define     V_028C70_COLOR_8_24                        0x00000011
#define     V_028C70_COLOR_8_24_FLOAT                  0x00000012
#define     V_028C70_COLOR_24_8                        0x00000013
#define     V_028C70_COLOR_24_8_FLOAT                  0x00000014
#define     V_028C70_COLOR_10_11_11                    0x00000015
#define     V_028C70_COLOR_10_11_11_FLOAT              0x00000016
#define     V_028C70_COLOR_11_11_10                    0x00000017
#define     V_028C70_COLOR_11_11_10_FLOAT              0x00000018
#define     V_028C70_COLOR_2_10_10_10                  0x00000019
#define     V_028C70_COLOR_8_8_8_8                     0x0000001A
#define     V_028C70_COLOR_10_10_10_2                  0x0000001B
#define     V_028C70_COLOR_X24_8_32_FLOAT              0x0000001C
#define     V_028C70_COLOR_32_32                       0x0000001D
#define     V_028C70_COLOR_32_32_FLOAT                 0x0000001E
#define     V_028C70_COLOR_16_16_16_16                 0x0000001F
#define     V_028C70_COLOR_16_16_16_16_FLOAT           0x00000020
#define     V_028C70_COLOR_32_32_32_32                 0x00000022
#define     V_028C70_COLOR_32_32_32_32_FLOAT           0x00000023
#define     V_028C70_COLOR_32_32_32_FLOAT              0x00000030
#define   S_028C70_ARRAY_MODE(x)                       (((x) & 0xF) << 8)
#define   G_028C70_ARRAY_MODE(x)                       (((x) >> 8) & 0xF)
#define   C_028C70_ARRAY_MODE                          0xFFFFF0FF
#define     V_028C70_ARRAY_LINEAR_GENERAL              0x00000000
#define     V_028C70_ARRAY_LINEAR_ALIGNED              0x00000001
#define     V_028C70_ARRAY_1D_TILED_THIN1              0x00000002
#define     V_028C70_ARRAY_2D_TILED_THIN1              0x00000004
#define   S_028C70_NUMBER_TYPE(x)                      (((x) & 0x7) << 12)
#define   G_028C70_NUMBER_TYPE(x)                      (((x) >> 12) & 0x7)
#define   C_028C70_NUMBER_TYPE                         0xFFFF8FFF
#define     V_028C70_NUMBER_UNORM                      0x00000000
#define     V_028C70_NUMBER_SNORM                      0x00000001
#define     V_028C70_NUMBER_USCALED                    0x00000002
#define     V_028C70_NUMBER_SSCALED                    0x00000003
#define     V_028C70_NUMBER_UINT                       0x00000004
#define     V_028C70_NUMBER_SINT                       0x00000005
#define     V_028C70_NUMBER_SRGB                       0x00000006
#define     V_028C70_NUMBER_FLOAT                      0x00000007
#define   S_028C70_COMP_SWAP(x)                        (((x) & 0x3) << 15)
#define   G_028C70_COMP_SWAP(x)                        (((x) >> 15) & 0x3)
#define   C_028C70_COMP_SWAP                           0xFFFE7FFF
#define     V_028C70_SWAP_STD                          0x00000000
#define     V_028C70_SWAP_ALT                          0x00000001
#define     V_028C70_SWAP_STD_REV                      0x00000002
#define     V_028C70_SWAP_ALT_REV                      0x00000003
#define   S_028C70_FAST_CLEAR(x)                       (((x) & 0x1) << 17)
#define   G_028C70_FAST_CLEAR(x)                       (((x) >> 17) & 0x1)
#define   C_028C70_FAST_CLEAR                          0xFFFDFFFF
#define   S_028C70_COMPRESSION(x)                      (((x) & 0x3) << 18)
#define   G_028C70_COMPRESSION(x)                      (((x) >> 18) & 0x3)
#define   C_028C70_COMPRESSION                         0xFFF3FFFF
#define   S_028C70_BLEND_CLAMP(x)                      (((x) & 0x1) << 19)
#define   G_028C70_BLEND_CLAMP(x)                      (((x) >> 19) & 0x1)
#define   C_028C70_BLEND_CLAMP                         0xFFF7FFFF
#define   S_028C70_BLEND_BYPASS(x)                     (((x) & 0x1) << 20)
#define   G_028C70_BLEND_BYPASS(x)                     (((x) >> 20) & 0x1)
#define   C_028C70_BLEND_BYPASS                        0xFFEFFFFF
#define   S_028C70_SIMPLE_FLOAT(x)                     (((x) & 0x1) << 21)
#define   G_028C70_SIMPLE_FLOAT(x)                     (((x) >> 21) & 0x1)
#define   C_028C70_SIMPLE_FLOAT                        0xFFDFFFFF
#define   S_028C70_ROUND_MODE(x)                       (((x) & 0x1) << 22)
#define   G_028C70_ROUND_MODE(x)                       (((x) >> 22) & 0x1)
#define   C_028C70_ROUND_MODE                          0xFFBFFFFF
#define   S_028C70_TILE_COMPACT(x)                     (((x) & 0x1) << 23)
#define   G_028C70_TILE_COMPACT(x)                     (((x) >> 23) & 0x1)
#define   C_028C70_TILE_COMPACT                        0xFF7FFFFF
#define   S_028C70_SOURCE_FORMAT(x)                    (((x) & 0x3) << 24)
#define   G_028C70_SOURCE_FORMAT(x)                    (((x) >> 24) & 0x3)
#define   C_028C70_SOURCE_FORMAT                       0xFCFFFFFF
#define     V_028C70_EXPORT_4C_32BPC                   0x0
#define     V_028C70_EXPORT_4C_16BPC                   0x1
#define     V_028C70_EXPORT_2C_32BPC                   0x2 /* Do not use */
#define   S_028C70_RAT(x)                              (((x) & 0x1) << 26)
#define   G_028C70_RAT(x)                              (((x) >> 26) & 0x1)
#define   C_028C70_RAT                                 0xFBFFFFFF
#define   S_028C70_RESOURCE_TYPE(x)                    (((x) & 0x7) << 27)
#define   G_028C70_RESOURCE_TYPE(x)                    (((x) >> 27) & 0x7)
#define   C_028C70_RESOURCE_TYPE                       0xC7FFFFFF

#define	CB_COLOR0_INFO					0x28c70
#	define CB_FORMAT(x)				((x) << 2)
#       define CB_ARRAY_MODE(x)                         ((x) << 8)
#       define ARRAY_LINEAR_GENERAL                     0
#       define ARRAY_LINEAR_ALIGNED                     1
#       define ARRAY_1D_TILED_THIN1                     2
#       define ARRAY_2D_TILED_THIN1                     4
#	define CB_SOURCE_FORMAT(x)			((x) << 24)
#	define CB_SF_EXPORT_FULL			0
#	define CB_SF_EXPORT_NORM			1
#define R_028C74_CB_COLOR0_ATTRIB                      0x028C74
#define   S_028C74_NON_DISP_TILING_ORDER(x)            (((x) & 0x1) << 4)
#define   G_028C74_NON_DISP_TILING_ORDER(x)            (((x) >> 4) & 0x1)
#define   C_028C74_NON_DISP_TILING_ORDER               0xFFFFFFEF
#define   S_028C74_TILE_SPLIT(x)                       (((x) & 0xf) << 5)
#define   G_028C74_TILE_SPLIT(x)                       (((x) >> 5) & 0xf)
#define   S_028C74_NUM_BANKS(x)                        (((x) & 0x3) << 10)
#define   G_028C74_NUM_BANKS(x)                        (((x) >> 10) & 0x3)
#define   S_028C74_BANK_WIDTH(x)                       (((x) & 0x3) << 13)
#define   G_028C74_BANK_WIDTH(x)                       (((x) >> 13) & 0x3)
#define   S_028C74_BANK_HEIGHT(x)                      (((x) & 0x3) << 16)
#define   G_028C74_BANK_HEIGHT(x)                      (((x) >> 16) & 0x3)
#define   S_028C74_MACRO_TILE_ASPECT(x)                (((x) & 0x3) << 19)
#define   G_028C74_MACRO_TILE_ASPECT(x)                (((x) >> 19) & 0x3)
#define	CB_COLOR0_ATTRIB				0x28c74
#       define CB_TILE_SPLIT(x)                         (((x) & 0x7) << 5)
#       define ADDR_SURF_TILE_SPLIT_64B                 0
#       define ADDR_SURF_TILE_SPLIT_128B                1
#       define ADDR_SURF_TILE_SPLIT_256B                2
#       define ADDR_SURF_TILE_SPLIT_512B                3
#       define ADDR_SURF_TILE_SPLIT_1KB                 4
#       define ADDR_SURF_TILE_SPLIT_2KB                 5
#       define ADDR_SURF_TILE_SPLIT_4KB                 6
#       define CB_NUM_BANKS(x)                          (((x) & 0x3) << 10)
#       define ADDR_SURF_2_BANK                         0
#       define ADDR_SURF_4_BANK                         1
#       define ADDR_SURF_8_BANK                         2
#       define ADDR_SURF_16_BANK                        3
#       define CB_BANK_WIDTH(x)                         (((x) & 0x3) << 13)
#       define ADDR_SURF_BANK_WIDTH_1                   0
#       define ADDR_SURF_BANK_WIDTH_2                   1
#       define ADDR_SURF_BANK_WIDTH_4                   2
#       define ADDR_SURF_BANK_WIDTH_8                   3
#       define CB_BANK_HEIGHT(x)                        (((x) & 0x3) << 16)
#       define ADDR_SURF_BANK_HEIGHT_1                  0
#       define ADDR_SURF_BANK_HEIGHT_2                  1
#       define ADDR_SURF_BANK_HEIGHT_4                  2
#       define ADDR_SURF_BANK_HEIGHT_8                  3
#       define CB_MACRO_TILE_ASPECT(x)                  (((x) & 0x3) << 19)
#define	CB_COLOR0_DIM					0x28c78
/* only CB0-7 blocks have these regs */
#define	CB_COLOR0_CMASK					0x28c7c
#define	CB_COLOR0_CMASK_SLICE				0x28c80
#define	CB_COLOR0_FMASK					0x28c84
#define	CB_COLOR0_FMASK_SLICE				0x28c88
#define	CB_COLOR0_CLEAR_WORD0				0x28c8c
#define	CB_COLOR0_CLEAR_WORD1				0x28c90
#define	CB_COLOR0_CLEAR_WORD2				0x28c94
#define	CB_COLOR0_CLEAR_WORD3				0x28c98

#define	CB_COLOR1_BASE					0x28c9c
#define	CB_COLOR2_BASE					0x28cd8
#define	CB_COLOR3_BASE					0x28d14
#define	CB_COLOR4_BASE					0x28d50
#define	CB_COLOR5_BASE					0x28d8c
#define	CB_COLOR6_BASE					0x28dc8
#define	CB_COLOR7_BASE					0x28e04
#define	CB_COLOR8_BASE					0x28e40
#define	CB_COLOR9_BASE					0x28e5c
#define	CB_COLOR10_BASE					0x28e78
#define	CB_COLOR11_BASE					0x28e94

#define	CB_COLOR1_PITCH					0x28ca0
#define	CB_COLOR2_PITCH					0x28cdc
#define	CB_COLOR3_PITCH					0x28d18
#define	CB_COLOR4_PITCH					0x28d54
#define	CB_COLOR5_PITCH					0x28d90
#define	CB_COLOR6_PITCH					0x28dcc
#define	CB_COLOR7_PITCH					0x28e08
#define	CB_COLOR8_PITCH					0x28e44
#define	CB_COLOR9_PITCH					0x28e60
#define	CB_COLOR10_PITCH				0x28e7c
#define	CB_COLOR11_PITCH				0x28e98

#define	CB_COLOR1_SLICE					0x28ca4
#define	CB_COLOR2_SLICE					0x28ce0
#define	CB_COLOR3_SLICE					0x28d1c
#define	CB_COLOR4_SLICE					0x28d58
#define	CB_COLOR5_SLICE					0x28d94
#define	CB_COLOR6_SLICE					0x28dd0
#define	CB_COLOR7_SLICE					0x28e0c
#define	CB_COLOR8_SLICE					0x28e48
#define	CB_COLOR9_SLICE					0x28e64
#define	CB_COLOR10_SLICE				0x28e80
#define	CB_COLOR11_SLICE				0x28e9c

#define	CB_COLOR1_VIEW					0x28ca8
#define	CB_COLOR2_VIEW					0x28ce4
#define	CB_COLOR3_VIEW					0x28d20
#define	CB_COLOR4_VIEW					0x28d5c
#define	CB_COLOR5_VIEW					0x28d98
#define	CB_COLOR6_VIEW					0x28dd4
#define	CB_COLOR7_VIEW					0x28e10
#define	CB_COLOR8_VIEW					0x28e4c
#define	CB_COLOR9_VIEW					0x28e68
#define	CB_COLOR10_VIEW					0x28e84
#define	CB_COLOR11_VIEW					0x28ea0

#define	CB_COLOR1_INFO					0x28cac
#define	CB_COLOR2_INFO					0x28ce8
#define	CB_COLOR3_INFO					0x28d24
#define	CB_COLOR4_INFO					0x28d60
#define	CB_COLOR5_INFO					0x28d9c
#define	CB_COLOR6_INFO					0x28dd8
#define	CB_COLOR7_INFO					0x28e14
#define	CB_COLOR8_INFO					0x28e50
#define	CB_COLOR9_INFO					0x28e6c
#define	CB_COLOR10_INFO					0x28e88
#define	CB_COLOR11_INFO					0x28ea4

#define	CB_COLOR1_ATTRIB				0x28cb0
#define	CB_COLOR2_ATTRIB				0x28cec
#define	CB_COLOR3_ATTRIB				0x28d28
#define	CB_COLOR4_ATTRIB				0x28d64
#define	CB_COLOR5_ATTRIB				0x28da0
#define	CB_COLOR6_ATTRIB				0x28ddc
#define	CB_COLOR7_ATTRIB				0x28e18
#define	CB_COLOR8_ATTRIB				0x28e54
#define	CB_COLOR9_ATTRIB				0x28e70
#define	CB_COLOR10_ATTRIB				0x28e8c
#define	CB_COLOR11_ATTRIB				0x28ea8

#define	CB_COLOR1_DIM					0x28cb4
#define	CB_COLOR2_DIM					0x28cf0
#define	CB_COLOR3_DIM					0x28d2c
#define	CB_COLOR4_DIM					0x28d68
#define	CB_COLOR5_DIM					0x28da4
#define	CB_COLOR6_DIM					0x28de0
#define	CB_COLOR7_DIM					0x28e1c
#define	CB_COLOR8_DIM					0x28e58
#define	CB_COLOR9_DIM					0x28e74
#define	CB_COLOR10_DIM					0x28e90
#define	CB_COLOR11_DIM					0x28eac

#define	CB_COLOR1_CMASK					0x28cb8
#define	CB_COLOR2_CMASK					0x28cf4
#define	CB_COLOR3_CMASK					0x28d30
#define	CB_COLOR4_CMASK					0x28d6c
#define	CB_COLOR5_CMASK					0x28da8
#define	CB_COLOR6_CMASK					0x28de4
#define	CB_COLOR7_CMASK					0x28e20

#define	CB_COLOR1_CMASK_SLICE				0x28cbc
#define	CB_COLOR2_CMASK_SLICE				0x28cf8
#define	CB_COLOR3_CMASK_SLICE				0x28d34
#define	CB_COLOR4_CMASK_SLICE				0x28d70
#define	CB_COLOR5_CMASK_SLICE				0x28dac
#define	CB_COLOR6_CMASK_SLICE				0x28de8
#define	CB_COLOR7_CMASK_SLICE				0x28e24

#define	CB_COLOR1_FMASK					0x28cc0
#define	CB_COLOR2_FMASK					0x28cfc
#define	CB_COLOR3_FMASK					0x28d38
#define	CB_COLOR4_FMASK					0x28d74
#define	CB_COLOR5_FMASK					0x28db0
#define	CB_COLOR6_FMASK					0x28dec
#define	CB_COLOR7_FMASK					0x28e28

#define	CB_COLOR1_FMASK_SLICE				0x28cc4
#define	CB_COLOR2_FMASK_SLICE				0x28d00
#define	CB_COLOR3_FMASK_SLICE				0x28d3c
#define	CB_COLOR4_FMASK_SLICE				0x28d78
#define	CB_COLOR5_FMASK_SLICE				0x28db4
#define	CB_COLOR6_FMASK_SLICE				0x28df0
#define	CB_COLOR7_FMASK_SLICE				0x28e2c

#define	CB_COLOR1_CLEAR_WORD0				0x28cc8
#define	CB_COLOR2_CLEAR_WORD0				0x28d04
#define	CB_COLOR3_CLEAR_WORD0				0x28d40
#define	CB_COLOR4_CLEAR_WORD0				0x28d7c
#define	CB_COLOR5_CLEAR_WORD0				0x28db8
#define	CB_COLOR6_CLEAR_WORD0				0x28df4
#define	CB_COLOR7_CLEAR_WORD0				0x28e30

#define	CB_COLOR1_CLEAR_WORD1				0x28ccc
#define	CB_COLOR2_CLEAR_WORD1				0x28d08
#define	CB_COLOR3_CLEAR_WORD1				0x28d44
#define	CB_COLOR4_CLEAR_WORD1				0x28d80
#define	CB_COLOR5_CLEAR_WORD1				0x28dbc
#define	CB_COLOR6_CLEAR_WORD1				0x28df8
#define	CB_COLOR7_CLEAR_WORD1				0x28e34

#define	CB_COLOR1_CLEAR_WORD2				0x28cd0
#define	CB_COLOR2_CLEAR_WORD2				0x28d0c
#define	CB_COLOR3_CLEAR_WORD2				0x28d48
#define	CB_COLOR4_CLEAR_WORD2				0x28d84
#define	CB_COLOR5_CLEAR_WORD2				0x28dc0
#define	CB_COLOR6_CLEAR_WORD2				0x28dfc
#define	CB_COLOR7_CLEAR_WORD2				0x28e38

#define	CB_COLOR1_CLEAR_WORD3				0x28cd4
#define	CB_COLOR2_CLEAR_WORD3				0x28d10
#define	CB_COLOR3_CLEAR_WORD3				0x28d4c
#define	CB_COLOR4_CLEAR_WORD3				0x28d88
#define	CB_COLOR5_CLEAR_WORD3				0x28dc4
#define	CB_COLOR6_CLEAR_WORD3				0x28e00
#define	CB_COLOR7_CLEAR_WORD3				0x28e3c

#define SQ_TEX_RESOURCE_WORD0_0                         0x30000
#	define TEX_DIM(x)				((x) << 0)
#	define SQ_TEX_DIM_1D				0
#	define SQ_TEX_DIM_2D				1
#	define SQ_TEX_DIM_3D				2
#	define SQ_TEX_DIM_CUBEMAP			3
#	define SQ_TEX_DIM_1D_ARRAY			4
#	define SQ_TEX_DIM_2D_ARRAY			5
#	define SQ_TEX_DIM_2D_MSAA			6
#	define SQ_TEX_DIM_2D_ARRAY_MSAA			7
#define SQ_TEX_RESOURCE_WORD1_0                         0x30004
#       define TEX_ARRAY_MODE(x)                        ((x) << 28)
#define SQ_TEX_RESOURCE_WORD2_0                         0x30008
#define SQ_TEX_RESOURCE_WORD3_0                         0x3000C
#define SQ_TEX_RESOURCE_WORD4_0                         0x30010
#	define TEX_DST_SEL_X(x)				((x) << 16)
#	define TEX_DST_SEL_Y(x)				((x) << 19)
#	define TEX_DST_SEL_Z(x)				((x) << 22)
#	define TEX_DST_SEL_W(x)				((x) << 25)
#	define SQ_SEL_X					0
#	define SQ_SEL_Y					1
#	define SQ_SEL_Z					2
#	define SQ_SEL_W					3
#	define SQ_SEL_0					4
#	define SQ_SEL_1					5
#define SQ_TEX_RESOURCE_WORD5_0                         0x30014
#define SQ_TEX_RESOURCE_WORD6_0                         0x30018
#       define TEX_TILE_SPLIT(x)                        (((x) & 0x7) << 29)
#define SQ_TEX_RESOURCE_WORD7_0                         0x3001c
#       define MACRO_TILE_ASPECT(x)                     (((x) & 0x3) << 6)
#       define TEX_BANK_WIDTH(x)                        (((x) & 0x3) << 8)
#       define TEX_BANK_HEIGHT(x)                       (((x) & 0x3) << 10)
#       define TEX_NUM_BANKS(x)                         (((x) & 0x3) << 16)
#define R_030000_SQ_TEX_RESOURCE_WORD0_0             0x030000
#define   S_030000_DIM(x)                              (((x) & 0x7) << 0)
#define   G_030000_DIM(x)                              (((x) >> 0) & 0x7)
#define   C_030000_DIM                                 0xFFFFFFF8
#define     V_030000_SQ_TEX_DIM_1D                     0x00000000
#define     V_030000_SQ_TEX_DIM_2D                     0x00000001
#define     V_030000_SQ_TEX_DIM_3D                     0x00000002
#define     V_030000_SQ_TEX_DIM_CUBEMAP                0x00000003
#define     V_030000_SQ_TEX_DIM_1D_ARRAY               0x00000004
#define     V_030000_SQ_TEX_DIM_2D_ARRAY               0x00000005
#define     V_030000_SQ_TEX_DIM_2D_MSAA                0x00000006
#define     V_030000_SQ_TEX_DIM_2D_ARRAY_MSAA          0x00000007
#define   S_030000_NON_DISP_TILING_ORDER(x)            (((x) & 0x1) << 5)
#define   G_030000_NON_DISP_TILING_ORDER(x)            (((x) >> 5) & 0x1)
#define   C_030000_NON_DISP_TILING_ORDER               0xFFFFFFDF
#define   S_030000_PITCH(x)                            (((x) & 0xFFF) << 6)
#define   G_030000_PITCH(x)                            (((x) >> 6) & 0xFFF)
#define   C_030000_PITCH                               0xFFFC003F
#define   S_030000_TEX_WIDTH(x)                        (((x) & 0x3FFF) << 18)
#define   G_030000_TEX_WIDTH(x)                        (((x) >> 18) & 0x3FFF)
#define   C_030000_TEX_WIDTH                           0x0003FFFF
#define R_030004_SQ_TEX_RESOURCE_WORD1_0             0x030004
#define   S_030004_TEX_HEIGHT(x)                       (((x) & 0x3FFF) << 0)
#define   G_030004_TEX_HEIGHT(x)                       (((x) >> 0) & 0x3FFF)
#define   C_030004_TEX_HEIGHT                          0xFFFFC000
#define   S_030004_TEX_DEPTH(x)                        (((x) & 0x1FFF) << 14)
#define   G_030004_TEX_DEPTH(x)                        (((x) >> 14) & 0x1FFF)
#define   C_030004_TEX_DEPTH                           0xF8003FFF
#define   S_030004_ARRAY_MODE(x)                       (((x) & 0xF) << 28)
#define   G_030004_ARRAY_MODE(x)                       (((x) >> 28) & 0xF)
#define   C_030004_ARRAY_MODE                          0x0FFFFFFF
#define R_030008_SQ_TEX_RESOURCE_WORD2_0             0x030008
#define   S_030008_BASE_ADDRESS(x)                     (((x) & 0xFFFFFFFF) << 0)
#define   G_030008_BASE_ADDRESS(x)                     (((x) >> 0) & 0xFFFFFFFF)
#define   C_030008_BASE_ADDRESS                        0x00000000
#define R_03000C_SQ_TEX_RESOURCE_WORD3_0             0x03000C
#define   S_03000C_MIP_ADDRESS(x)                      (((x) & 0xFFFFFFFF) << 0)
#define   G_03000C_MIP_ADDRESS(x)                      (((x) >> 0) & 0xFFFFFFFF)
#define   C_03000C_MIP_ADDRESS                         0x00000000
#define R_030010_SQ_TEX_RESOURCE_WORD4_0             0x030010
#define   S_030010_FORMAT_COMP_X(x)                    (((x) & 0x3) << 0)
#define   G_030010_FORMAT_COMP_X(x)                    (((x) >> 0) & 0x3)
#define   C_030010_FORMAT_COMP_X                       0xFFFFFFFC
#define     V_030010_SQ_FORMAT_COMP_UNSIGNED           0x00000000
#define     V_030010_SQ_FORMAT_COMP_SIGNED             0x00000001
#define     V_030010_SQ_FORMAT_COMP_UNSIGNED_BIASED    0x00000002
#define   S_030010_FORMAT_COMP_Y(x)                    (((x) & 0x3) << 2)
#define   G_030010_FORMAT_COMP_Y(x)                    (((x) >> 2) & 0x3)
#define   C_030010_FORMAT_COMP_Y                       0xFFFFFFF3
#define   S_030010_FORMAT_COMP_Z(x)                    (((x) & 0x3) << 4)
#define   G_030010_FORMAT_COMP_Z(x)                    (((x) >> 4) & 0x3)
#define   C_030010_FORMAT_COMP_Z                       0xFFFFFFCF
#define   S_030010_FORMAT_COMP_W(x)                    (((x) & 0x3) << 6)
#define   G_030010_FORMAT_COMP_W(x)                    (((x) >> 6) & 0x3)
#define   C_030010_FORMAT_COMP_W                       0xFFFFFF3F
#define   S_030010_NUM_FORMAT_ALL(x)                   (((x) & 0x3) << 8)
#define   G_030010_NUM_FORMAT_ALL(x)                   (((x) >> 8) & 0x3)
#define   C_030010_NUM_FORMAT_ALL                      0xFFFFFCFF
#define     V_030010_SQ_NUM_FORMAT_NORM                0x00000000
#define     V_030010_SQ_NUM_FORMAT_INT                 0x00000001
#define     V_030010_SQ_NUM_FORMAT_SCALED              0x00000002
#define   S_030010_SRF_MODE_ALL(x)                     (((x) & 0x1) << 10)
#define   G_030010_SRF_MODE_ALL(x)                     (((x) >> 10) & 0x1)
#define   C_030010_SRF_MODE_ALL                        0xFFFFFBFF
#define     V_030010_SRF_MODE_ZERO_CLAMP_MINUS_ONE     0x00000000
#define     V_030010_SRF_MODE_NO_ZERO                  0x00000001
#define   S_030010_FORCE_DEGAMMA(x)                    (((x) & 0x1) << 11)
#define   G_030010_FORCE_DEGAMMA(x)                    (((x) >> 11) & 0x1)
#define   C_030010_FORCE_DEGAMMA                       0xFFFFF7FF
#define   S_030010_ENDIAN_SWAP(x)                      (((x) & 0x3) << 12)
#define   G_030010_ENDIAN_SWAP(x)                      (((x) >> 12) & 0x3)
#define   C_030010_ENDIAN_SWAP                         0xFFFFCFFF
#define   S_030010_DST_SEL_X(x)                        (((x) & 0x7) << 16)
#define   G_030010_DST_SEL_X(x)                        (((x) >> 16) & 0x7)
#define   C_030010_DST_SEL_X                           0xFFF8FFFF
#define     V_030010_SQ_SEL_X                          0x00000000
#define     V_030010_SQ_SEL_Y                          0x00000001
#define     V_030010_SQ_SEL_Z                          0x00000002
#define     V_030010_SQ_SEL_W                          0x00000003
#define     V_030010_SQ_SEL_0                          0x00000004
#define     V_030010_SQ_SEL_1                          0x00000005
#define   S_030010_DST_SEL_Y(x)                        (((x) & 0x7) << 19)
#define   G_030010_DST_SEL_Y(x)                        (((x) >> 19) & 0x7)
#define   C_030010_DST_SEL_Y                           0xFFC7FFFF
#define   S_030010_DST_SEL_Z(x)                        (((x) & 0x7) << 22)
#define   G_030010_DST_SEL_Z(x)                        (((x) >> 22) & 0x7)
#define   C_030010_DST_SEL_Z                           0xFE3FFFFF
#define   S_030010_DST_SEL_W(x)                        (((x) & 0x7) << 25)
#define   G_030010_DST_SEL_W(x)                        (((x) >> 25) & 0x7)
#define   C_030010_DST_SEL_W                           0xF1FFFFFF
#define   S_030010_BASE_LEVEL(x)                       (((x) & 0xF) << 28)
#define   G_030010_BASE_LEVEL(x)                       (((x) >> 28) & 0xF)
#define   C_030010_BASE_LEVEL                          0x0FFFFFFF
#define R_030014_SQ_TEX_RESOURCE_WORD5_0             0x030014
#define   S_030014_LAST_LEVEL(x)                       (((x) & 0xF) << 0)
#define   G_030014_LAST_LEVEL(x)                       (((x) >> 0) & 0xF)
#define   C_030014_LAST_LEVEL                          0xFFFFFFF0
#define   S_030014_BASE_ARRAY(x)                       (((x) & 0x1FFF) << 4)
#define   G_030014_BASE_ARRAY(x)                       (((x) >> 4) & 0x1FFF)
#define   C_030014_BASE_ARRAY                          0xFFFE000F
#define   S_030014_LAST_ARRAY(x)                       (((x) & 0x1FFF) << 17)
#define   G_030014_LAST_ARRAY(x)                       (((x) >> 17) & 0x1FFF)
#define   C_030014_LAST_ARRAY                          0xC001FFFF
#define R_030018_SQ_TEX_RESOURCE_WORD6_0             0x030018
#define   S_030018_MAX_ANISO(x)                        (((x) & 0x7) << 0)
#define   G_030018_MAX_ANISO(x)                        (((x) >> 0) & 0x7)
#define   C_030018_MAX_ANISO                           0xFFFFFFF8
#define   S_030018_PERF_MODULATION(x)                  (((x) & 0x7) << 3)
#define   G_030018_PERF_MODULATION(x)                  (((x) >> 3) & 0x7)
#define   C_030018_PERF_MODULATION                     0xFFFFFFC7
#define   S_030018_INTERLACED(x)                       (((x) & 0x1) << 6)
#define   G_030018_INTERLACED(x)                       (((x) >> 6) & 0x1)
#define   C_030018_INTERLACED                          0xFFFFFFBF
#define   S_030018_TILE_SPLIT(x)                       (((x) & 0x7) << 29)
#define   G_030018_TILE_SPLIT(x)                       (((x) >> 29) & 0x7)
#define R_03001C_SQ_TEX_RESOURCE_WORD7_0             0x03001C
#define   S_03001C_MACRO_TILE_ASPECT(x)                (((x) & 0x3) << 6)
#define   G_03001C_MACRO_TILE_ASPECT(x)                (((x) >> 6) & 0x3)
#define   S_03001C_BANK_WIDTH(x)                       (((x) & 0x3) << 8)
#define   G_03001C_BANK_WIDTH(x)                       (((x) >> 8) & 0x3)
#define   S_03001C_BANK_HEIGHT(x)                      (((x) & 0x3) << 10)
#define   G_03001C_BANK_HEIGHT(x)                      (((x) >> 10) & 0x3)
#define   S_03001C_NUM_BANKS(x)                        (((x) & 0x3) << 16)
#define   G_03001C_NUM_BANKS(x)                        (((x) >> 16) & 0x3)
#define   S_03001C_TYPE(x)                             (((x) & 0x3) << 30)
#define   G_03001C_TYPE(x)                             (((x) >> 30) & 0x3)
#define   C_03001C_TYPE                                0x3FFFFFFF
#define     V_03001C_SQ_TEX_VTX_INVALID_TEXTURE        0x00000000
#define     V_03001C_SQ_TEX_VTX_INVALID_BUFFER         0x00000001
#define     V_03001C_SQ_TEX_VTX_VALID_TEXTURE          0x00000002
#define     V_03001C_SQ_TEX_VTX_VALID_BUFFER           0x00000003
#define   S_03001C_DATA_FORMAT(x)                      (((x) & 0x3F) << 0)
#define   G_03001C_DATA_FORMAT(x)                      (((x) >> 0) & 0x3F)
#define   C_03001C_DATA_FORMAT                         0xFFFFFFC0

#define SQ_VTX_CONSTANT_WORD0_0				0x30000
#define SQ_VTX_CONSTANT_WORD1_0				0x30004
#define SQ_VTX_CONSTANT_WORD2_0				0x30008
#	define SQ_VTXC_BASE_ADDR_HI(x)			((x) << 0)
#	define SQ_VTXC_STRIDE(x)			((x) << 8)
#	define SQ_VTXC_ENDIAN_SWAP(x)			((x) << 30)
#	define SQ_ENDIAN_NONE				0
#	define SQ_ENDIAN_8IN16				1
#	define SQ_ENDIAN_8IN32				2
#define SQ_VTX_CONSTANT_WORD3_0				0x3000C
#	define SQ_VTCX_SEL_X(x)				((x) << 3)
#	define SQ_VTCX_SEL_Y(x)				((x) << 6)
#	define SQ_VTCX_SEL_Z(x)				((x) << 9)
#	define SQ_VTCX_SEL_W(x)				((x) << 12)
#define SQ_VTX_CONSTANT_WORD4_0				0x30010
#define SQ_VTX_CONSTANT_WORD5_0                         0x30014
#define SQ_VTX_CONSTANT_WORD6_0                         0x30018
#define SQ_VTX_CONSTANT_WORD7_0                         0x3001c

#define TD_PS_BORDER_COLOR_INDEX                        0xA400
#define TD_PS_BORDER_COLOR_RED                          0xA404
#define TD_PS_BORDER_COLOR_GREEN                        0xA408
#define TD_PS_BORDER_COLOR_BLUE                         0xA40C
#define TD_PS_BORDER_COLOR_ALPHA                        0xA410
#define TD_VS_BORDER_COLOR_INDEX                        0xA414
#define TD_VS_BORDER_COLOR_RED                          0xA418
#define TD_VS_BORDER_COLOR_GREEN                        0xA41C
#define TD_VS_BORDER_COLOR_BLUE                         0xA420
#define TD_VS_BORDER_COLOR_ALPHA                        0xA424
#define TD_GS_BORDER_COLOR_INDEX                        0xA428
#define TD_GS_BORDER_COLOR_RED                          0xA42C
#define TD_GS_BORDER_COLOR_GREEN                        0xA430
#define TD_GS_BORDER_COLOR_BLUE                         0xA434
#define TD_GS_BORDER_COLOR_ALPHA                        0xA438
#define TD_HS_BORDER_COLOR_INDEX                        0xA43C
#define TD_HS_BORDER_COLOR_RED                          0xA440
#define TD_HS_BORDER_COLOR_GREEN                        0xA444
#define TD_HS_BORDER_COLOR_BLUE                         0xA448
#define TD_HS_BORDER_COLOR_ALPHA                        0xA44C
#define TD_LS_BORDER_COLOR_INDEX                        0xA450
#define TD_LS_BORDER_COLOR_RED                          0xA454
#define TD_LS_BORDER_COLOR_GREEN                        0xA458
#define TD_LS_BORDER_COLOR_BLUE                         0xA45C
#define TD_LS_BORDER_COLOR_ALPHA                        0xA460
#define TD_CS_BORDER_COLOR_INDEX                        0xA464
#define TD_CS_BORDER_COLOR_RED                          0xA468
#define TD_CS_BORDER_COLOR_GREEN                        0xA46C
#define TD_CS_BORDER_COLOR_BLUE                         0xA470
#define TD_CS_BORDER_COLOR_ALPHA                        0xA474

/* cayman 3D regs */
#define CAYMAN_VGT_OFFCHIP_LDS_BASE			0x89B4
#define CAYMAN_SQ_EX_ALLOC_TABLE_SLOTS			0x8E48
#define CAYMAN_DB_EQAA					0x28804
#define CAYMAN_DB_DEPTH_INFO				0x2803C
#define CAYMAN_PA_SC_AA_CONFIG				0x28BE0
#define         CAYMAN_MSAA_NUM_SAMPLES_SHIFT           0
#define         CAYMAN_MSAA_NUM_SAMPLES_MASK            0x7
#define CAYMAN_SX_SCATTER_EXPORT_BASE			0x28358
/* cayman packet3 addition */
#define	CAYMAN_PACKET3_DEALLOC_STATE			0x14

/* DMA regs common on r6xx/r7xx/evergreen/ni */
#define DMA_RB_CNTL                                       0xd000
#       define DMA_RB_ENABLE                              (1 << 0)
#       define DMA_RB_SIZE(x)                             ((x) << 1) /* log2 */
#       define DMA_RB_SWAP_ENABLE                         (1 << 9) /* 8IN32 */
#       define DMA_RPTR_WRITEBACK_ENABLE                  (1 << 12)
#       define DMA_RPTR_WRITEBACK_SWAP_ENABLE             (1 << 13)  /* 8IN32 */
#       define DMA_RPTR_WRITEBACK_TIMER(x)                ((x) << 16) /* log2 */
#define DMA_STATUS_REG                                    0xd034
#       define DMA_IDLE                                   (1 << 0)

#endif
