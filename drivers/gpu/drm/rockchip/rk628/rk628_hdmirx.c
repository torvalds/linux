// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (c) 2020 Rockchip Electronics Co. Ltd.
 *
 * Author: Algea Cao <algea.cao@rock-chips.com>
 */

#include <linux/clk.h>
#include <linux/debugfs.h>
#include <linux/delay.h>
#include <linux/module.h>
#include <linux/of.h>
#include <linux/platform_device.h>
#include <linux/regmap.h>
#include <linux/reset.h>
#include <linux/timer.h>
#include <linux/workqueue.h>
#include <linux/mfd/rk628.h>
#include <linux/phy/phy.h>

#include <drm/drmP.h>
#include <drm/drm_atomic.h>
#include <drm/drm_atomic_helper.h>
#include <drm/drm_crtc_helper.h>
#include <drm/drm_dp_helper.h>
#include <drm/drm_of.h>

#define REG(x)                             ((x) + 0x30000)
#define HDMI_RX_HDMI_SETUP_CTRL            REG(0x0000)
#define HOT_PLUG_DETECT_MASK               BIT(0)
#define HOT_PLUG_DETECT(x)                 UPDATE(x, 0, 0)
#define HDMI_RX_HDMI_OVR_CTRL              REG(0x0004)
#define HDMI_RX_HDMI_TIMER_CTRL            REG(0x0008)
#define HDMI_RX_HDMI_RES_OVR               REG(0x0010)
#define HDMI_RX_HDMI_RES_STS               REG(0x0014)
#define HDMI_RX_HDMI_PLL_CTRL              REG(0x0018)
#define HDMI_RX_HDMI_PLL_FRQSET1           REG(0x001c)
#define HDMI_RX_HDMI_PLL_FRQSET2           REG(0x0020)
#define HDMI_RX_HDMI_PLL_PAR1              REG(0x0024)
#define HDMI_RX_HDMI_PLL_PAR2              REG(0x0028)
#define HDMI_RX_HDMI_PLL_PAR3              REG(0x002c)
#define HDMI_RX_HDMI_PLL_LCK_STS           REG(0x0030)
#define HDMI_RX_HDMI_CLK_CTRL              REG(0x0034)
#define HDMI_RX_HDMI_PCB_CTRL              REG(0x0038)
#define SEL_PIXCLKSRC_MASK                 GENMASK(19, 18)
#define SEL_PIXCLKSRC(x)                   UPDATE(x, 19, 18)
#define HDMI_RX_HDMI_PHS_CTR               REG(0x0040)
#define HDMI_RX_HDMI_PHS_USED              REG(0x0044)
#define HDMI_RX_HDMI_MISC_CTRL             REG(0x0048)
#define HDMI_RX_HDMI_EQOFF_CTRL            REG(0x004c)
#define HDMI_RX_HDMI_EQGAIN_CTRL           REG(0x0050)
#define HDMI_RX_HDMI_EQCAL_STS             REG(0x0054)
#define HDMI_RX_HDMI_EQRESULT              REG(0x0058)
#define HDMI_RX_HDMI_EQ_MEAS_CTRL          REG(0x005c)
#define HDMI_RX_HDMI_WR_CFG                REG(0x0060)
#define HDMI_RX_HDMI_CTRL                  REG(0x0064)
#define HDMI_RX_HDMI_MODE_RECOVER          REG(0x0080)
#define PREAMBLE_CNT_LIMIT_MASK            GENMASK(31, 27)
#define PREAMBLE_CNT_LIMIT(x)              UPDATE(x, 31, 27)
#define OESSCTL3_THR_MASK                  GENMASK(20, 19)
#define OESSCTL3_THR(x)                    UPDATE(x, 20, 19)
#define SPIKE_FILTER_EN_MASK               BIT(18)
#define SPIKE_FILTER_EN(x)                 UPDATE(x, 18, 18)
#define DVI_MODE_HYST_MASK                 GENMASK(17, 13)
#define DVI_MODE_HYST(x)                   UPDATE(x, 17, 13)
#define HDMI_MODE_HYST_MASK                GENMASK(12, 8)
#define HDMI_MODE_HYST(x)                  UPDATE(x, 12, 8)
#define HDMI_MODE_MASK                     GENMASK(7, 6)
#define HDMI_MODE(x)                       UPDATE(x, 7, 6)
#define GB_DET_MASK                        GENMASK(5, 4)
#define GB_DET(x)                          UPDATE(x, 5, 4)
#define EESS_OESS_MASK                     GENMASK(3, 2)
#define EESS_OESS(x)                       UPDATE(x, 3, 2)
#define SEL_CTL01_MASK                     GENMASK(1, 0)
#define SEL_CTL01(x)                       UPDATE(x, 1, 0)
#define HDMI_RX_HDMI_ERROR_PROTECT         REG(0x0084)
#define RG_BLOCK_OFF_MASK                  BIT(20)
#define RG_BLOCK_OFF(x)                    UPDATE(x, 20, 20)
#define BLOCK_OFF_MASK                     BIT(19)
#define BLOCK_OFF(x)                       UPDATE(x, 19, 19)
#define VALID_MODE_MASK                    GENMASK(18, 16)
#define VALID_MODE(x)                      UPDATE(x, 18, 16)
#define CTRL_FILT_SEN_MASK                 GENMASK(13, 12)
#define CTRL_FILT_SEN(x)                   UPDATE(x, 13, 12)
#define VS_FILT_SENS_MASK                  GENMASK(11, 10)
#define VS_FILT_SENS(x)                    UPDATE(x, 11, 10)
#define HS_FILT_SENS_MASK                  GENMASK(9, 8)
#define HS_FILT_SENS(x)                    UPDATE(x, 9, 8)
#define DE_MEASURE_MODE_MASK               GENMASK(7, 6)
#define DE_MEASURE_MODE(x)                 UPDATE(x, 7, 6)
#define DE_REGEN_MASK                      BIT(5)
#define DE_REGEN(x)                        UPDATE(x, 5, 5)
#define DE_FILTER_SENS_MASK                GENMASK(4, 3)
#define DE_FILTER_SENS(x)                  UPDATE(x, 4, 3)
#define HDMI_RX_HDMI_ERD_STS               REG(0x0088)
#define HDMI_RX_HDMI_SYNC_CTRL             REG(0x0090)
#define VS_POL_ADJ_MODE_MASK               GENMASK(4, 3)
#define VS_POL_ADJ_MODE(x)                 UPDATE(x, 4, 3)
#define HS_POL_ADJ_MODE_MASK               GENMASK(2, 1)
#define HS_POL_ADJ_MODE(x)                 UPDATE(x, 2, 1)
#define HDMI_RX_HDMI_CKM_EVLTM             REG(0x0094)
#define LOCK_HYST_MASK                     GENMASK(21, 20)
#define LOCK_HYST(x)                       UPDATE(x, 21, 20)
#define CLK_HYST_MASK                      GENMASK(18, 16)
#define CLK_HYST(x)                        UPDATE(x, 18, 16)
#define EVAL_TIME_MASK                     GENMASK(15, 4)
#define EVAL_TIME(x)                       UPDATE(x, 15, 4)
#define HDMI_RX_HDMI_CKM_F                 REG(0x0098)
#define HDMIRX_MAXFREQ_MASK                GENMASK(31, 16)
#define HDMIRX_MAXFREQ(x)                  UPDATE(x, 31, 16)
#define MINFREQ_MASK                       GENMASK(15, 0)
#define MINFREQ(x)                         UPDATE(x, 15, 0)
#define HDMI_RX_HDMI_CKM_RESULT            REG(0x009c)
#define HDMI_RX_HDMI_PVO_CONFIG            REG(0x00a0)
#define HDMI_RX_HDMI_RESMPL_CTRL           REG(0x00a4)
#define MAN_VID_DEREPEAT_MASK              GENMASK(4, 1)
#define MAN_VID_DEREPEAT(x)                UPDATE(x, 4, 1)
#define AUTO_DEREPEAT_MASK                 BIT(0)
#define AUTO_DEREPEAT(x)                   UPDATE(x, 0, 0)
#define HDMI_RX_HDMI_DCM_CTRL              REG(0x00a8)
#define DCM_DEFAULT_PHASE_MASK             BIT(18)
#define DCM_DEFAULT_PHASE(x)               UPDATE(x, 18, 18)
#define DCM_COLOUR_DEPTH_SEL_MASK          BIT(12)
#define DCM_COLOUR_DEPTH_SEL(x)            UPDATE(x, 12, 12)
#define DCM_COLOUR_DEPTH_MASK              GENMASK(11, 8)
#define DCM_COLOUR_DEPTH(x)                UPDATE(x, 11, 8)
#define DCM_GCP_ZERO_FIELDS_MASK           GENMASK(5, 2)
#define DCM_GCP_ZERO_FIELDS(x)             UPDATE(x, 5, 2)
#define HDMI_RX_HDMI_VM_CFG_CH_0_1         REG(0x00b0)
#define HDMI_RX_HDMI_VM_CFG_CH2            REG(0x00b4)
#define HDMI_RX_HDMI_SPARE                 REG(0x00b8)
#define HDMI_RX_HDMI_STS                   REG(0x00bc)
#define HDMI_RX_HDCP_CTRL                  REG(0x00c0)
#define HDCP_ENABLE_MASK                   BIT(24)
#define HDCP_ENABLE(x)                     UPDATE(x, 24, 24)
#define FREEZE_HDCP_FSM_MASK               BIT(21)
#define FREEZE_HDCP_FSM(x)                 UPDATE(x, 21, 21)
#define FREEZE_HDCP_STATE_MASK             GENMASK(20, 15)
#define FREEZE_HDCP_STATE(x)               UPDATE(x, 20, 15)
#define HDCP_CTL_MASK                      GENMASK(9, 8)
#define HDCP_CTL(x)                        UPDATE(x, 9, 8)
#define HDCP_RI_RATE_MASK                  GENMASK(7, 6)
#define HDCP_RI_RATE(x)                    UPDATE(x, 7, 6)
#define KEY_DECRYPT_ENABLE_MASK            BIT(1)
#define KEY_DECRYPT_ENABLE(x)              UPDATE(x, 1, 1)
#define HDCP_ENC_EN_MASK                   BIT(0)
#define HDCP_ENC_EN(x)                     UPDATE(x, 0, 0)
#define HDMI_RX_HDCP_SETTINGS              REG(0x00c4)
#define HDMI_RX_HDCP_SEED                  REG(0x00c8)
#define HDMI_RX_HDCP_BKSV1                 REG(0x00cc)
#define HDMI_RX_HDCP_BKSV0                 REG(0x00d0)
#define HDMI_RX_HDCP_KIDX                  REG(0x00d4)
#define HDMI_RX_HDCP_KEY1                  REG(0x00d8)
#define HDMI_RX_HDCP_KEY0                  REG(0x00dc)
#define HDMI_RX_HDCP_DBG                   REG(0x00e0)
#define HDMI_RX_HDCP_AKSV1                 REG(0x00e4)
#define HDMI_RX_HDCP_AKSV0                 REG(0x00e8)
#define HDMI_RX_HDCP_AN1                   REG(0x00ec)
#define HDMI_RX_HDCP_AN0                   REG(0x00f0)
#define HDMI_RX_HDCP_EESS_WOO              REG(0x00f4)
#define HDMI_RX_HDCP_I2C_TIMEOUT           REG(0x00f8)
#define HDMI_RX_HDCP_STS                   REG(0x00fc)
#define HDMI_RX_MD_HCTRL1                  REG(0x0140)
#define HACT_PIX_ITH_MASK                  GENMASK(10, 8)
#define HACT_PIX_ITH(x)                    UPDATE(x, 10, 8)
#define HACT_PIX_SRC_MASK                  BIT(5)
#define HACT_PIX_SRC(x)                    UPDATE(x, 5, 5)
#define HTOT_PIX_SRC_MASK                  BIT(4)
#define HTOT_PIX_SRC(x)                    UPDATE(x, 4, 4)
#define HDMI_RX_MD_HCTRL2                  REG(0x0144)
#define HS_CLK_ITH_MASK                    GENMASK(14, 12)
#define HS_CLK_ITH(x)                      UPDATE(x, 14, 12)
#define HTOT32_CLK_ITH_MASK                GENMASK(9, 8)
#define HTOT32_CLK_ITH(x)                  UPDATE(x, 9, 8)
#define VS_ACT_TIME_MASK                   BIT(5)
#define VS_ACT_TIME(x)                     UPDATE(x, 5, 5)
#define HS_ACT_TIME_MASK                   GENMASK(4, 3)
#define HS_ACT_TIME(x)                     UPDATE(x, 4, 3)
#define H_START_POS_MASK                   GENMASK(1, 0)
#define H_START_POS(x)                     UPDATE(x, 1, 0)
#define HDMI_RX_MD_HT0                     REG(0x0148)
#define HDMI_RX_MD_HT1                     REG(0x014c)
#define HDMI_RX_MD_HACT_PX                 REG(0x0150)
#define HDMI_RX_MD_HACT_RSV                REG(0x0154)
#define HDMI_RX_MD_VCTRL                   REG(0x0158)
#define V_OFFS_LIN_MODE_MASK               BIT(4)
#define V_OFFS_LIN_MODE(x)                 UPDATE(x, 4, 4)
#define V_EDGE_MASK                        BIT(1)
#define V_EDGE(x)                          UPDATE(x, 1, 1)
#define V_MODE_MASK                        BIT(0)
#define V_MODE(x)                          UPDATE(x, 0, 0)
#define HDMI_RX_MD_VSC                     REG(0x015c)
#define HDMI_RX_MD_VTC                     REG(0x0160)
#define HDMI_RX_MD_VOL                     REG(0x0164)
#define HDMI_RX_MD_VAL                     REG(0x0168)
#define HDMI_RX_MD_VTH                     REG(0x016c)
#define VOFS_LIN_ITH_MASK                  GENMASK(11, 10)
#define VOFS_LIN_ITH(x)                    UPDATE(x, 11, 10)
#define VACT_LIN_ITH_MASK                  GENMASK(9, 8)
#define VACT_LIN_ITH(x)                    UPDATE(x, 9, 8)
#define VTOT_LIN_ITH_MASK                  GENMASK(7, 6)
#define VTOT_LIN_ITH(x)                    UPDATE(x, 7, 6)
#define VS_CLK_ITH_MASK                    GENMASK(5, 3)
#define VS_CLK_ITH(x)                      UPDATE(x, 5, 3)
#define VTOT_CLK_ITH_MASK                  GENMASK(2, 0)
#define VTOT_CLK_ITH(x)                    UPDATE(x, 2, 0)
#define HDMI_RX_MD_VTL                     REG(0x0170)
#define HDMI_RX_MD_IL_CTRL                 REG(0x0174)
#define HDMI_RX_MD_IL_SKEW                 REG(0x0178)
#define HDMI_RX_MD_IL_POL                  REG(0x017c)
#define FAFIELDDET_EN_MASK                 BIT(2)
#define FAFIELDDET_EN(x)                   UPDATE(x, 2, 2)
#define FIELD_POL_MODE_MASK                GENMASK(1, 0)
#define FIELD_POL_MODE(x)                  UPDATE(x, 1, 0)
#define HDMI_RX_MD_STS                     REG(0x0180)
#define HDMI_RX_AUD_CTRL                   REG(0x0200)
#define HDMI_RX_AUD_PLL_CTRL               REG(0x0208)
#define PLL_LOCK_TOGGLE_DIV_MASK           GENMASK(27, 24)
#define PLL_LOCK_TOGGLE_DIV(x)             UPDATE(x, 27, 24)
#define HDMI_RX_AUD_CLK_CTRL               REG(0x0214)
#define CTS_N_REF_MASK                     BIT(4)
#define CTS_N_REF(x)                       UPDATE(x, 4, 4)
#define HDMI_RX_AUD_CLK_STS                REG(0x023c)
#define HDMI_RX_AUD_FIFO_CTRL              REG(0x0240)
#define AFIF_SUBPACKET_DESEL_MASK          GENMASK(27, 24)
#define AFIF_SUBPACKET_DESEL(x)            UPDATE(x, 27, 24)
#define AFIF_SUBPACKETS_MASK               BIT(16)
#define AFIF_SUBPACKETS(x)                 UPDATE(x, 16, 16)
#define MSA_CHANNEL_DESELECT               BIT(24)
#define HDMI_RX_AUD_FIFO_TH                REG(0x0244)
#define AFIF_TH_START_MASK                 GENMASK(26, 18)
#define AFIF_TH_START(x)                   UPDATE(x, 26, 18)
#define AFIF_TH_MAX_MASK                   GENMASK(17, 9)
#define AFIF_TH_MAX(x)                     UPDATE(x, 17, 9)
#define AFIF_TH_MIN_MASK                   GENMASK(8, 0)
#define AFIF_TH_MIN(x)                     UPDATE(x, 8, 0)
#define HDMI_RX_AUD_FIFO_FILL_S            REG(0x0248)
#define HDMI_RX_AUD_FIFO_CLR_MM            REG(0x024c)
#define HDMI_RX_AUD_FIFO_FILLSTS           REG(0x0250)
#define HDMI_RX_AUD_CHEXTR_CTRL            REG(0x0254)
#define AUD_LAYOUT_CTRL(x)                 UPDATE(x, 1, 0)
#define HDMI_RX_AUD_MUTE_CTRL              REG(0x0258)
#define APPLY_INT_MUTE_MASK                BIT(31)
#define APPLY_INT_MUTE(x)                  UPDATE(x, 31, 31)
#define APORT_SHDW_CTRL_MASK               GENMASK(22, 21)
#define APORT_SHDW_CTRL(x)                 UPDATE(x, 22, 21)
#define AUTO_ACLK_MUTE_MASK                GENMASK(20, 19)
#define AUTO_ACLK_MUTE(x)                  UPDATE(x, 20, 19)
#define AUD_MUTE_SPEED_MASK                GENMASK(16, 10)
#define AUD_MUTE_SPEED(x)                  UPDATE(x, 16, 10)
#define AUD_AVMUTE_EN_MASK                 BIT(7)
#define AUD_AVMUTE_EN(x)                   UPDATE(x, 7, 7)
#define AUD_MUTE_SEL_MASK                  GENMASK(6, 5)
#define AUD_MUTE_SEL(x)                    UPDATE(x, 6, 5)
#define AUD_MUTE_MODE_MASK                 GENMASK(4, 3)
#define AUD_MUTE_MODE(x)                   UPDATE(x, 4, 3)
#define HDMI_RX_AUD_FIFO_FILLSTS1          REG(0x025c)
#define HDMI_RX_AUD_SAO_CTRL               REG(0x0260)
#define I2S_LPCM_BPCUV_MASK                BIT(11)
#define I2S_LPCM_BPCUV(x)                  UPDATE(x, 11, 11)
#define I2S_32_16_MASK                     BIT(0)
#define I2S_32_16(x)                       UPDATE(x, 0, 0)
#define HDMI_RX_AUD_PAO_CTRL               REG(0x0264)
#define PAO_RATE_MASK                      GENMASK(17, 16)
#define PAO_RATE(x)                        UPDATE(x, 17, 16)
#define HDMI_RX_AUD_SPARE                  REG(0x0268)
#define HDMI_RX_AUD_FIFO_STS               REG(0x027c)
#define HDMI_RX_AUDPLL_GEN_CTS             REG(0x0280)
#define AUDPLL_CTS_MANUAL(x)               UPDATE(x, 19, 0)
#define HDMI_RX_AUDPLL_GEN_N               REG(0x0284)
#define AUDPLL_N_MANUAL(x)                 UPDATE(x, 19, 0)
#define HDMI_RX_AUDPLL_GEN_CTRL_RW1        REG(0x0288)
#define HDMI_RX_AUDPLL_GEN_CTRL_RW2        REG(0x028c)
#define HDMI_RX_AUDPLL_GEN_CTRL_W1         REG(0x0298)
#define HDMI_RX_AUDPLL_GEN_STS_RO1         REG(0x02a0)
#define HDMI_RX_AUDPLL_GEN_STS_RO2         REG(0x02a4)
#define HDMI_RX_AUDPLL_SC_NDIVCTSTH        REG(0x02a8)
#define HDMI_RX_AUDPLL_SC_CTS              REG(0x02ac)
#define HDMI_RX_AUDPLL_SC_N                REG(0x02b0)
#define HDMI_RX_AUDPLL_SC_CTRL             REG(0x02b4)
#define HDMI_RX_AUDPLL_SC_STS1             REG(0x02b8)
#define HDMI_RX_AUDPLL_SC_STS2             REG(0x02bc)
#define HDMI_RX_SNPS_PHYG3_CTRL            REG(0x02c0)
#define PORTSELECT_MASK                    GENMASK(3, 2)
#define PORTSELECT(x)                      UPDATE(x, 3, 2)
#define HDMI_RX_I2CM_PHYG3_SLAVE           REG(0x02c4)
#define HDMI_RX_I2CM_PHYG3_ADDRESS         REG(0x02c8)
#define HDMI_RX_I2CM_PHYG3_DATAO           REG(0x02cc)
#define HDMI_RX_I2CM_PHYG3_DATAI           REG(0x02d0)
#define HDMI_RX_I2CM_PHYG3_OPERATION       REG(0x02d4)
#define HDMI_RX_I2CM_PHYG3_MODE            REG(0x02d8)
#define HDMI_RX_I2CM_PHYG3_SOFTRST         REG(0x02dc)
#define HDMI_RX_I2CM_PHYG3_SS_CNTS         REG(0x02e0)
#define HDMI_RX_I2CM_PHYG3_FS_HCNT         REG(0x02e4)
#define HDMI_RX_JTAG_CONF                  REG(0x02ec)
#define HDMI_RX_JTAG_TAP_TCLK              REG(0x02f0)
#define HDMI_RX_JTAG_TAP_IN                REG(0x02f4)
#define HDMI_RX_JTAG_TAP_OUT               REG(0x02f8)
#define HDMI_RX_JTAG_ADDR                  REG(0x02fc)
#define HDMI_RX_PDEC_CTRL                  REG(0x0300)
#define PFIFO_SCORE_FILTER_EN              BIT(31)
#define PFIFO_SCORE_HDP_IF                 BIT(29)
#define PFIFO_SCORE_AMP_IF                 BIT(28)
#define PFIFO_SCORE_NTSCVBI_IF             BIT(27)
#define PFIFO_SCORE_MPEGS_IF               BIT(26)
#define PFIFO_SCORE_AUD_IF                 BIT(25)
#define PFIFO_SCORE_SPD_IF                 BIT(24)
#define PFIFO_SCORE_AVI_IF                 BIT(23)
#define PFIFO_SCORE_VS_IF                  BIT(22)
#define PFIFO_SCORE_GMTP                   BIT(21)
#define PFIFO_SCORE_ISRC2                  BIT(20)
#define PFIFO_SCORE_ISRC1                  BIT(19)
#define PFIFO_SCORE_ACP                    BIT(18)
#define PFIFO_SCORE_GCP                    BIT(17)
#define PFIFO_SCORE_ACR                    BIT(16)
#define GCP_GLOBAVMUTE                     BIT(15)
#define PD_FIFO_WE                         BIT(4)
#define PDEC_BCH_EN                        BIT(0)
#define HDMI_RX_PDEC_FIFO_CFG              REG(0x0304)
#define PD_FIFO_TH_START_MASK              GENMASK(29, 20)
#define PD_FIFO_TH_START(x)                UPDATE(x, 29, 20)
#define PD_FIFO_TH_MAX_MASK                GENMASK(19, 10)
#define PD_FIFO_TH_MAX(x)                  UPDATE(x, 19, 10)
#define PD_FIFO_TH_MIN_MASK                GENMASK(9, 0)
#define PD_FIFO_TH_MIN(x)                  UPDATE(x, 9, 0)
#define HDMI_RX_PDEC_FIFO_STS              REG(0x0308)
#define HDMI_RX_PDEC_FIFO_DATA             REG(0x030c)
#define HDMI_RX_PDEC_AUDIODET_CTRL         REG(0x0310)
#define AUDIODET_THRESHOLD_MASK            GENMASK(13, 9)
#define AUDIODET_THRESHOLD(x)              UPDATE(x, 13, 9)
#define HDMI_RX_PDEC_DBG_ACP               REG(0x031c)
#define HDMI_RX_PDEC_DBG_ERR_CORR          REG(0x0320)
#define HDMI_RX_PDEC_FIFO_STS1             REG(0x0324)
#define HDMI_RX_PDEC_ACRM_CTRL             REG(0x0330)
#define DELTACTS_IRQTRIG_MASK              GENMASK(4, 2)
#define DELTACTS_IRQTRIG(x)                UPDATE(x, 4, 2)
#define HDMI_RX_PDEC_ACRM_MAX              REG(0x0334)
#define HDMI_RX_PDEC_ACRM_MIN              REG(0x0338)
#define HDMI_RX_PDEC_ERR_FILTER            REG(0x033c)
#define HDMI_RX_PDEC_ASP_CTRL              REG(0x0340)
#define HDMI_RX_PDEC_ASP_ERR               REG(0x0344)
#define HDMI_RX_PDEC_STS                   REG(0x0360)
#define HDMI_RX_PDEC_AUD_STS               REG(0x0364)
#define HDMI_RX_PDEC_VSI_PAYLOAD0          REG(0x0368)
#define HDMI_RX_PDEC_VSI_PAYLOAD1          REG(0x036c)
#define HDMI_RX_PDEC_VSI_PAYLOAD2          REG(0x0370)
#define HDMI_RX_PDEC_VSI_PAYLOAD3          REG(0x0374)
#define HDMI_RX_PDEC_VSI_PAYLOAD4          REG(0x0378)
#define HDMI_RX_PDEC_VSI_PAYLOAD5          REG(0x037c)
#define HDMI_RX_PDEC_GCP_AVMUTE            REG(0x0380)
#define PKTDEC_GCP_CD_MASK                 GENMASK(7, 4)
#define HDMI_RX_PDEC_ACR_CTS               REG(0x0390)
#define HDMI_RX_PDEC_ACR_N                 REG(0x0394)
#define HDMI_RX_PDEC_AVI_HB                REG(0x03a0)
#define HDMI_RX_PDEC_AVI_PB                REG(0x03a4)
#define VID_IDENT_CODE_VIC7                BIT(31)
#define VID_IDENT_CODE                     GENMASK(30, 24)
#define VIDEO_FORMAT                       GENMASK(6, 5)
#define HDMI_RX_PDEC_AVI_TBB               REG(0x03a8)
#define HDMI_RX_PDEC_AVI_LRB               REG(0x03ac)
#define HDMI_RX_PDEC_AIF_CTRL              REG(0x03c0)
#define FC_LFE_EXCHG                       BIT(18)
#define HDMI_RX_PDEC_AIF_HB                REG(0x03c4)
#define HDMI_RX_PDEC_AIF_PB0               REG(0x03c8)
#define HDMI_RX_PDEC_AIF_PB1               REG(0x03cc)
#define HDMI_RX_PDEC_GMD_HB                REG(0x03d0)
#define HDMI_RX_PDEC_GMD_PB                REG(0x03d4)
#define HDMI_RX_PDEC_VSI_ST0               REG(0x03e0)
#define HDMI_RX_PDEC_VSI_ST1               REG(0x03e4)
#define HDMI_RX_PDEC_VSI_PB0               REG(0x03e8)
#define HDMI_RX_PDEC_VSI_PB1               REG(0x03ec)
#define HDMI_RX_PDEC_VSI_PB2               REG(0x03f0)
#define HDMI_RX_PDEC_VSI_PB3               REG(0x03f4)
#define HDMI_RX_PDEC_VSI_PB4               REG(0x03f8)
#define HDMI_RX_PDEC_VSI_PB5               REG(0x03fc)
#define HDMI_RX_CEAVID_CONFIG              REG(0x0400)
#define HDMI_RX_CEAVID_3DCONFIG            REG(0x0404)
#define HDMI_RX_CEAVID_HCONFIG_LO          REG(0x0408)
#define HDMI_RX_CEAVID_HCONFIG_HI          REG(0x040c)
#define HDMI_RX_CEAVID_VCONFIG_LO          REG(0x0410)
#define HDMI_RX_CEAVID_VCONFIG_HI          REG(0x0414)
#define HDMI_RX_CEAVID_STATUS              REG(0x0418)
#define HDMI_RX_PDEC_AMP_HB                REG(0x0480)
#define HDMI_RX_PDEC_AMP_PAYLOAD0          REG(0x0484)
#define HDMI_RX_PDEC_AMP_PAYLOAD1          REG(0x0488)
#define HDMI_RX_PDEC_AMP_PAYLOAD2          REG(0x048c)
#define HDMI_RX_PDEC_AMP_PAYLOAD3          REG(0x0490)
#define HDMI_RX_PDEC_AMP_PAYLOAD4          REG(0x0494)
#define HDMI_RX_PDEC_AMP_PAYLOAD5          REG(0x0498)
#define HDMI_RX_PDEC_AMP_PAYLOAD6          REG(0x049c)
#define HDMI_RX_PDEC_NTSCVBI_HB            REG(0x04a0)
#define HDMI_RX_PDEC_NTSCVBI_PAYLOAD0      REG(0x04a4)
#define HDMI_RX_PDEC_NTSCVBI_PAYLOAD1      REG(0x04a8)
#define HDMI_RX_PDEC_NTSCVBI_PAYLOAD2      REG(0x04ac)
#define HDMI_RX_PDEC_NTSCVBI_PAYLOAD3      REG(0x04b0)
#define HDMI_RX_PDEC_NTSCVBI_PAYLOAD4      REG(0x04b4)
#define HDMI_RX_PDEC_NTSCVBI_PAYLOAD5      REG(0x04b8)
#define HDMI_RX_PDEC_NTSCVBI_PAYLOAD6      REG(0x04bc)
#define HDMI_RX_PDEC_DRM_HB                REG(0x04c0)
#define HDMI_RX_PDEC_DRM_PAYLOAD0          REG(0x04c4)
#define HDMI_RX_PDEC_DRM_PAYLOAD1          REG(0x04c8)
#define HDMI_RX_PDEC_DRM_PAYLOAD2          REG(0x04cc)
#define HDMI_RX_PDEC_DRM_PAYLOAD3          REG(0x04d0)
#define HDMI_RX_PDEC_DRM_PAYLOAD4          REG(0x04d4)
#define HDMI_RX_PDEC_DRM_PAYLOAD5          REG(0x04d8)
#define HDMI_RX_PDEC_DRM_PAYLOAD6          REG(0x04dc)
#define HDMI_RX_MHLMODE_CTRL               REG(0x0500)
#define HDMI_RX_CDSENSE_STATUS             REG(0x0504)
#define HDMI_RX_DESERFIFO_CTRL             REG(0x0508)
#define HDMI_RX_DESER_INTTRSHCTRL          REG(0x050c)
#define HDMI_RX_DESER_INTCNTCTRL           REG(0x0510)
#define HDMI_RX_DESER_INTCNT               REG(0x0514)
#define HDMI_RX_HDCP_RPT_CTRL              REG(0x0600)
#define HDMI_RX_HDCP_RPT_BSTATUS           REG(0x0604)
#define HDMI_RX_HDCP_RPT_KSVFIFO_CTRL      REG(0x0608)
#define HDMI_RX_HDCP_RPT_KSVFIFO1          REG(0x060c)
#define HDMI_RX_HDCP_RPT_KSVFIFO0          REG(0x0610)
#define HDMI_RX_HDMI20_CONTROL             REG(0x0800)
#define HDMI_RX_SCDC_I2CCONFIG             REG(0x0804)
#define I2CSPIKESUPPR_MASK                 GENMASK(25, 24)
#define I2CSPIKESUPPR(x)                   UPDATE(x, 25, 24)
#define HDMI_RX_SCDC_CONFIG                REG(0x0808)
#define HDMI_RX_CHLOCK_CONFIG              REG(0x080c)
#define CHLOCKMAXER_MASK                   GENMASK(29, 20)
#define CHLOCKMAXER(x)                     UPDATE(x, 29, 20)
#define MILISECTIMERLIMIT_MASK             GENMASK(15, 0)
#define MILISECTIMERLIMIT(x)               UPDATE(x, 15, 0)
#define HDMI_RX_HDCP22_CONTROL             REG(0x081c)
#define HDMI_RX_SCDC_REGS0                 REG(0x0820)
#define HDMI_RX_SCDC_REGS1                 REG(0x0824)
#define HDMI_RX_SCDC_REGS2                 REG(0x0828)
#define HDMI_RX_SCDC_REGS3                 REG(0x082c)
#define HDMI_RX_SCDC_MANSPEC0              REG(0x0840)
#define HDMI_RX_SCDC_MANSPEC1              REG(0x0844)
#define HDMI_RX_SCDC_MANSPEC2              REG(0x0848)
#define HDMI_RX_SCDC_MANSPEC3              REG(0x084c)
#define HDMI_RX_SCDC_MANSPEC4              REG(0x0850)
#define HDMI_RX_SCDC_WRDATA0               REG(0x0860)
#define MANUFACTUREROUI_MASK               GENMASK(31, 8)
#define MANUFACTUREROUI(x)                 UPDATE(x, 31, 8)
#define SINKVERSION_MASK                   GENMASK(7, 0)
#define SINKVERSION(x)                     UPDATE(x, 7, 0)
#define HDMI_RX_SCDC_WRDATA1               REG(0x0864)
#define HDMI_RX_SCDC_WRDATA2               REG(0x0868)
#define HDMI_RX_SCDC_WRDATA3               REG(0x086c)
#define HDMI_RX_SCDC_WRDATA4               REG(0x0870)
#define HDMI_RX_SCDC_WRDATA5               REG(0x0874)
#define HDMI_RX_SCDC_WRDATA6               REG(0x0878)
#define HDMI_RX_SCDC_WRDATA7               REG(0x087c)
#define HDMI_RX_HDMI20_STATUS              REG(0x08e0)
#define HDMI_RX_HDCP2_ESM_GLOBAL_GPIO_IN   REG(0x08e8)
#define HDMI_RX_HDCP2_ESM_GLOBAL_GPIO_OUT  REG(0x08ec)
#define HDMI_RX_HDCP2_ESM_P0_GPIO_IN       REG(0x08f0)
#define HDMI_RX_HDCP2_ESM_P0_GPIO_OUT      REG(0x08f4)
#define HDMI_RX_HDCP22_STATUS              REG(0x08fc)
#define HDMI_RX_HDMI2_IEN_CLR              REG(0x0f60)
#define HDMI_RX_HDMI2_IEN_SET              REG(0x0f64)
#define HDMI_RX_HDMI2_ISTS                 REG(0x0f68)
#define HDMI_RX_HDMI2_IEN                  REG(0x0f6c)
#define HDMI_RX_HDMI2_ICLR                 REG(0x0f70)
#define HDMI_RX_HDMI2_ISET                 REG(0x0f74)
#define HDMI_RX_PDEC_IEN_CLR               REG(0x0f78)
#define HDMI_RX_PDEC_IEN_SET               REG(0x0f7c)
#define HDMI_RX_PDEC_ISTS                  REG(0x0f80)
#define HDMI_RX_PDEC_IEN                   REG(0x0f84)
#define HDMI_RX_PDEC_ICLR                  REG(0x0f88)
#define HDMI_RX_PDEC_ISET                  REG(0x0f8c)
#define HDMI_RX_AUD_CEC_IEN_CLR            REG(0x0f90)
#define HDMI_RX_AUD_CEC_IEN_SET            REG(0x0f94)
#define HDMI_RX_AUD_CEC_ISTS               REG(0x0f98)
#define HDMI_RX_AUD_CEC_IEN                REG(0x0f9c)
#define HDMI_RX_AUD_CEC_ICLR               REG(0x0fa0)
#define HDMI_RX_AUD_CEC_ISET               REG(0x0fa4)
#define HDMI_RX_AUD_FIFO_IEN_CLR           REG(0x0fa8)
#define HDMI_RX_AUD_FIFO_IEN_SET           REG(0x0fac)
#define HDMI_RX_AUD_FIFO_ISTS              REG(0x0fb0)
#define HDMI_RX_AUD_FIFO_IEN               REG(0x0fb4)
#define HDMI_RX_AUD_FIFO_ICLR              REG(0x0fb8)
#define HDMI_RX_AUD_FIFO_ISET              REG(0x0fbc)
#define HDMI_RX_MD_IEN_CLR                 REG(0x0fc0)
#define HDMI_RX_MD_IEN_SET                 REG(0x0fc4)
#define HDMI_RX_MD_ISTS                    REG(0x0fc8)
#define HDMI_RX_MD_IEN                     REG(0x0fcc)
#define HDMI_RX_MD_ICLR                    REG(0x0fd0)
#define HDMI_RX_MD_ISET                    REG(0x0fd4)
#define HDMI_RX_HDMI_IEN_CLR               REG(0x0fd8)
#define HDMI_RX_HDMI_IEN_SET               REG(0x0fdc)
#define HDCP_DKSET_DONE_ENCLR_MASK         BIT(31)
#define HDCP_DKSET_DONE_ENCLR(x)           UPDATE(x, 31, 31)
#define HDMI_RX_HDMI_ISTS                  REG(0x0fe0)
#define HDMI_RX_HDMI_IEN                   REG(0x0fe4)
#define HDMI_RX_HDMI_ICLR                  REG(0x0fe8)
#define HDMI_RX_HDMI_ISET                  REG(0x0fec)
#define HDMI_RX_DMI_SW_RST                 REG(0x0ff0)
#define HDMI_RX_DMI_DISABLE_IF             REG(0x0ff4)
#define MAIN_ENABLE                        BIT(0)
#define MODET_ENABLE                       BIT(1)
#define HDMI_ENABLE                        BIT(2)
#define BUS_ENABLE                         BIT(3)
#define AUD_ENABLE                         BIT(4)
#define CEC_ENABLE                         BIT(5)
#define PIXEL_ENABLE                       BIT(6)
#define VID_ENABLE                         BIT(7)
#define TMDS_ENABLE_MASK                   BIT(16)
#define TMDS_ENABLE(x)                     UPDATE(x, 16, 16)
#define HDMI_RX_DMI_MODULE_ID_EXT          REG(0x0ff8)
#define HDMI_RX_DMI_MODULE_ID              REG(0x0ffc)
#define HDMI_RX_CEC_CTRL                   REG(0x1f00)
#define HDMI_RX_CEC_MASK                   REG(0x1f08)
#define HDMI_RX_CEC_ADDR_L                 REG(0x1f14)
#define HDMI_RX_CEC_ADDR_H                 REG(0x1f18)
#define HDMI_RX_CEC_TX_CNT                 REG(0x1f1c)
#define HDMI_RX_CEC_RX_CNT                 REG(0x1f20)
#define HDMI_RX_CEC_TX_DATA_0              REG(0x1f40)
#define HDMI_RX_CEC_TX_DATA_1              REG(0x1f44)
#define HDMI_RX_CEC_TX_DATA_2              REG(0x1f48)
#define HDMI_RX_CEC_TX_DATA_3              REG(0x1f4c)
#define HDMI_RX_CEC_TX_DATA_4              REG(0x1f50)
#define HDMI_RX_CEC_TX_DATA_5              REG(0x1f54)
#define HDMI_RX_CEC_TX_DATA_6              REG(0x1f58)
#define HDMI_RX_CEC_TX_DATA_7              REG(0x1f5c)
#define HDMI_RX_CEC_TX_DATA_8              REG(0x1f60)
#define HDMI_RX_CEC_TX_DATA_9              REG(0x1f64)
#define HDMI_RX_CEC_TX_DATA_10             REG(0x1f68)
#define HDMI_RX_CEC_TX_DATA_11             REG(0x1f6c)
#define HDMI_RX_CEC_TX_DATA_12             REG(0x1f70)
#define HDMI_RX_CEC_TX_DATA_13             REG(0x1f74)
#define HDMI_RX_CEC_TX_DATA_14             REG(0x1f78)
#define HDMI_RX_CEC_TX_DATA_15             REG(0x1f7c)
#define HDMI_RX_CEC_RX_DATA_0              REG(0x1f80)
#define HDMI_RX_CEC_RX_DATA_1              REG(0x1f84)
#define HDMI_RX_CEC_RX_DATA_2              REG(0x1f88)
#define HDMI_RX_CEC_RX_DATA_3              REG(0x1f8c)
#define HDMI_RX_CEC_RX_DATA_4              REG(0x1f90)
#define HDMI_RX_CEC_RX_DATA_5              REG(0x1f94)
#define HDMI_RX_CEC_RX_DATA_6              REG(0x1f98)
#define HDMI_RX_CEC_RX_DATA_7              REG(0x1f9c)
#define HDMI_RX_CEC_RX_DATA_8              REG(0x1fa0)
#define HDMI_RX_CEC_RX_DATA_9              REG(0x1fa4)
#define HDMI_RX_CEC_RX_DATA_10             REG(0x1fa8)
#define HDMI_RX_CEC_RX_DATA_11             REG(0x1fac)
#define HDMI_RX_CEC_RX_DATA_12             REG(0x1fb0)
#define HDMI_RX_CEC_RX_DATA_13             REG(0x1fb4)
#define HDMI_RX_CEC_RX_DATA_14             REG(0x1fb8)
#define HDMI_RX_CEC_RX_DATA_15             REG(0x1fbc)
#define HDMI_RX_CEC_LOCK                   REG(0x1fc0)
#define HDMI_RX_CEC_WAKEUPCTRL             REG(0x1fc4)
#define HDMI_RX_CBUSSWRESETREQ             REG(0x3000)
#define HDMI_RX_CBUSENABLEIF               REG(0x3004)
#define HDMI_RX_CB_LOCKONCLOCK_STS         REG(0x3010)
#define HDMI_RX_CB_LOCKONCLOCKCLR          REG(0x3014)
#define HDMI_RX_CBUSIOCTRL                 REG(0x3020)
#define HDMI_RX_DD_CTRL                    REG(0x3040)
#define HDMI_RX_DD_OP_CTRL                 REG(0x3044)
#define HDMI_RX_DD_STS                     REG(0x3048)
#define HDMI_RX_DD_BYPASS_EN               REG(0x304c)
#define HDMI_RX_DD_BYPASS_CTRL             REG(0x3050)
#define HDMI_RX_DD_BYPASS_CBUS             REG(0x3054)
#define HDMI_RX_LL_TXPCKFIFO               REG(0x3080)
#define HDMI_RX_LL_RXPCKFIFO_RD_CLR        REG(0x3084)
#define HDMI_RX_LL_RXPCKFIFO_A             REG(0x3088)
#define HDMI_RX_LL_RXPCKFIFO_B             REG(0x308c)
#define HDMI_RX_LL_TXPCKCTRL_0             REG(0x3090)
#define HDMI_RX_LL_TXPCKCTRL_1             REG(0x3094)
#define HDMI_RX_LL_PCKFIFO_STS             REG(0x309c)
#define HDMI_RX_LL_RXPCKCTRL_0             REG(0x30a0)
#define HDMI_RX_LL_RXPCKCTRL_1             REG(0x30a4)
#define HDMI_RX_LL_INTTRSHLDCTRL           REG(0x30b0)
#define HDMI_RX_LL_INTCNTCTRL              REG(0x30b4)
#define HDMI_RX_LL_INTCNT_0                REG(0x30b8)
#define HDMI_RX_LL_INTCNT_1                REG(0x30bc)
#define HDMI_RX_CBHDCP_OPCTRL              REG(0x3100)
#define HDMI_RX_CBHDCP_WDATA_0             REG(0x3104)
#define HDMI_RX_CBHDCP_WDATA_1             REG(0x3108)
#define HDMI_RX_CBHDCP_RDATA_0             REG(0x310c)
#define HDMI_RX_CBHDCP_RDATA_1             REG(0x3110)
#define HDMI_RX_CBHDCP_STATUS              REG(0x3114)
#define HDMI_RX_CBHDCP_DDC_REPORT          REG(0x3118)
#define HDMI_RX_ISTAT_CB_DD                REG(0x3200)
#define HDMI_RX_IMASK_CB_DD                REG(0x3204)
#define HDMI_RX_IFORCE_CB_DD               REG(0x3208)
#define HDMI_RX_ICLEAR_CB_DD               REG(0x320c)
#define HDMI_RX_IMUTE_CB_DD                REG(0x3210)
#define HDMI_RX_ISTAT_CB_LL                REG(0x3220)
#define HDMI_RX_IMASK_CB_LL                REG(0x3224)
#define HDMI_RX_IFORCE_CB_LL               REG(0x3228)
#define HDMI_RX_ICLEAR_CB_LL               REG(0x322c)
#define HDMI_RX_IMUTE_CB_LL                REG(0x3230)
#define HDMI_RX_ISTAT_CB_HDCP              REG(0x3240)
#define HDMI_RX_IMASK_CB_HDCP              REG(0x3244)
#define HDMI_RX_IFORCE_CB_HDCP             REG(0x3248)
#define HDMI_RX_ICLEAR_CB_HDCP             REG(0x324c)
#define HDMI_RX_IMUTE_CB_HDCP              REG(0x3250)
#define HDMI_RX_ISTAT_CB_MCTRL             REG(0x3260)
#define HDMI_RX_IMASK_CB_MCTRL             REG(0x3264)
#define HDMI_RX_IFORCE_CB_MCTRL            REG(0x3268)
#define HDMI_RX_ICLEAR_CB_MCTRL            REG(0x326c)
#define HDMI_RX_IMUTE_CB_MCTRL             REG(0x3270)
#define HDMI_RX_IMASTER_MUTE_CB            REG(0x32e0)
#define HDMI_RX_IVECTOR_INDEX_CB           REG(0x32e4)
#define HDMI_RX_MAX_REGISTER               HDMI_RX_IVECTOR_INDEX_CB

struct rk628_hdmirx {
	struct drm_bridge base;
	struct drm_bridge *bridge;
	struct device *dev;
	struct regmap *regmap;
	struct regmap *grf;
	struct phy *phy;
	struct clk *pclk;
	struct clk *cec_clk;
	struct clk *aud_clk;
	struct clk *imodet_clk;
	struct reset_control *hdmirx;
	struct reset_control *hdmirx_pon;
	struct rk628 *parent;
	struct drm_display_mode mode;
};

static const struct regmap_range rk628_hdmirx_readable_ranges[] = {
	regmap_reg_range(HDMI_RX_HDMI_SETUP_CTRL, HDMI_RX_HDMI_TIMER_CTRL),
	regmap_reg_range(HDMI_RX_HDMI_MODE_RECOVER, HDMI_RX_HDMI_ERD_STS),
	regmap_reg_range(HDMI_RX_MD_HCTRL1, HDMI_RX_MD_STS),
	regmap_reg_range(HDMI_RX_PDEC_ACRM_CTRL, HDMI_RX_PDEC_ASP_ERR),
	regmap_reg_range(HDMI_RX_PDEC_AVI_HB, HDMI_RX_PDEC_AVI_LRB),
	regmap_reg_range(HDMI_RX_PDEC_AIF_CTRL, HDMI_RX_PDEC_GMD_PB),
	regmap_reg_range(HDMI_RX_HDMI20_CONTROL, HDMI_RX_CHLOCK_CONFIG),
	regmap_reg_range(HDMI_RX_SCDC_REGS1, HDMI_RX_SCDC_REGS3),
	regmap_reg_range(HDMI_RX_SCDC_WRDATA0, HDMI_RX_SCDC_WRDATA7),
	regmap_reg_range(HDMI_RX_DMI_DISABLE_IF, HDMI_RX_DMI_DISABLE_IF),
};

static const struct regmap_access_table rk628_hdmirx_readable_table = {
	.yes_ranges	= rk628_hdmirx_readable_ranges,
	.n_yes_ranges   = ARRAY_SIZE(rk628_hdmirx_readable_ranges),
};

static const struct regmap_config rk628_hdmirx_regmap_config = {
	.name = "hdmirx",
	.reg_bits = 32,
	.val_bits = 32,
	.reg_stride = 4,
	.max_register = HDMI_RX_MAX_REGISTER,
	.reg_format_endian = REGMAP_ENDIAN_LITTLE,
	.val_format_endian = REGMAP_ENDIAN_LITTLE,
	.rd_table = &rk628_hdmirx_readable_table,
};

static inline struct rk628_hdmirx *bridge_to_hdmirx(struct drm_bridge *bridge)
{
	return container_of(bridge, struct rk628_hdmirx, base);
}

static void rk628_hdmirx_ctrl_enable(struct rk628_hdmirx *hdmirx)
{
	clk_prepare_enable(hdmirx->pclk);
	clk_prepare_enable(hdmirx->aud_clk);
	clk_prepare_enable(hdmirx->imodet_clk);

	reset_control_deassert(hdmirx->hdmirx);
	reset_control_deassert(hdmirx->hdmirx_pon);

	regmap_update_bits(hdmirx->grf, GRF_SYSTEM_CON0,
			   SW_INPUT_MODE_MASK,
			   SW_INPUT_MODE(INPUT_MODE_HDMI));

	regmap_write(hdmirx->regmap, HDMI_RX_DMI_SW_RST, 0x000101ff);

	regmap_write(hdmirx->regmap, HDMI_RX_DMI_DISABLE_IF, 0x00000000);
	regmap_write(hdmirx->regmap, HDMI_RX_DMI_DISABLE_IF, 0x0000017f);
	regmap_write(hdmirx->regmap, HDMI_RX_DMI_DISABLE_IF, 0x0001017f);

	regmap_write(hdmirx->regmap, HDMI_RX_HDMI20_CONTROL, 0x10001f10);

	regmap_update_bits(hdmirx->regmap, HDMI_RX_CHLOCK_CONFIG,
			   CHLOCKMAXER_MASK | MILISECTIMERLIMIT_MASK,
			   CHLOCKMAXER(0x1) | MILISECTIMERLIMIT(49500));

	regmap_write(hdmirx->regmap, HDMI_RX_SCDC_CONFIG, 0x00000001);
	regmap_write(hdmirx->regmap, HDMI_RX_DMI_SW_RST, 0x000001fe);
	regmap_write(hdmirx->regmap, HDMI_RX_HDMI_CKM_EVLTM, 0x0016fff0);
	regmap_write(hdmirx->regmap, HDMI_RX_HDMI_CKM_F, 0xf98a0190);

	regmap_update_bits(hdmirx->regmap, HDMI_RX_HDMI_MODE_RECOVER,
			   SPIKE_FILTER_EN_MASK | DVI_MODE_HYST_MASK |
			   HDMI_MODE_HYST_MASK | HDMI_MODE_MASK |
			   GB_DET_MASK | EESS_OESS_MASK | SEL_CTL01_MASK,
			   SPIKE_FILTER_EN(0) |
			   DVI_MODE_HYST(0) |
			   HDMI_MODE_HYST(0) |
			   HDMI_MODE(3) |
			   GB_DET(2) |
			   EESS_OESS(0) |
			   SEL_CTL01(1));

	regmap_write(hdmirx->regmap, HDMI_RX_PDEC_CTRL, 0xbfff8011);
	regmap_write(hdmirx->regmap, HDMI_RX_PDEC_ASP_CTRL, 0x00000040);

	regmap_update_bits(hdmirx->regmap, HDMI_RX_HDMI_RESMPL_CTRL,
			   MAN_VID_DEREPEAT_MASK, MAN_VID_DEREPEAT(1));

	regmap_update_bits(hdmirx->regmap, HDMI_RX_HDMI_SYNC_CTRL,
			   VS_POL_ADJ_MODE_MASK | HS_POL_ADJ_MODE_MASK,
			   VS_POL_ADJ_MODE(2) | HS_POL_ADJ_MODE(2));

	regmap_write(hdmirx->regmap, HDMI_RX_PDEC_ERR_FILTER, 0x00000008);

	regmap_update_bits(hdmirx->regmap, HDMI_RX_SCDC_I2CCONFIG,
			   I2CSPIKESUPPR_MASK, I2CSPIKESUPPR(1));

	regmap_write(hdmirx->regmap, HDMI_RX_SCDC_CONFIG, 0x00000001);
	regmap_write(hdmirx->regmap, HDMI_RX_SCDC_WRDATA0, 0xabcdef01);

	regmap_update_bits(hdmirx->regmap, HDMI_RX_CHLOCK_CONFIG,
			   CHLOCKMAXER_MASK | MILISECTIMERLIMIT_MASK,
			   CHLOCKMAXER(0x1) | MILISECTIMERLIMIT(49500));

	regmap_write(hdmirx->regmap, HDMI_RX_HDMI_ERROR_PROTECT, 0x000d0c98);
	regmap_write(hdmirx->regmap, HDMI_RX_MD_HCTRL1, 0x00000010);
	regmap_write(hdmirx->regmap, HDMI_RX_MD_HCTRL2, 0x00001738);
	regmap_write(hdmirx->regmap, HDMI_RX_MD_VCTRL, 0x00000012);
	regmap_write(hdmirx->regmap, HDMI_RX_MD_VTH, 0x0000073a);
	regmap_write(hdmirx->regmap, HDMI_RX_MD_IL_POL, 0x00000004);
	regmap_write(hdmirx->regmap, HDMI_RX_PDEC_ACRM_CTRL, 0x00000000);
	regmap_write(hdmirx->regmap, HDMI_RX_HDMI_DCM_CTRL, 0x00040414);
	regmap_write(hdmirx->regmap, HDMI_RX_HDMI_PCB_CTRL, 0x00100000);
	regmap_write(hdmirx->regmap, HDMI_RX_HDMI_SETUP_CTRL, 0x0f000fff);
	regmap_write(hdmirx->regmap, HDMI_RX_HDMI_CKM_EVLTM, 0x00104260);
	regmap_write(hdmirx->regmap, HDMI_RX_HDMI_CKM_F, 0x0f2d0eed);
	regmap_write(hdmirx->regmap, HDMI_RX_DMI_DISABLE_IF, 0x00000001);
	udelay(400);
	regmap_write(hdmirx->regmap, HDMI_RX_DMI_DISABLE_IF, 0x0001017f);
	regmap_update_bits(hdmirx->regmap, HDMI_RX_HDMI_RESMPL_CTRL,
			   MAN_VID_DEREPEAT_MASK, MAN_VID_DEREPEAT(1));
	regmap_write(hdmirx->regmap, HDMI_RX_DMI_SW_RST, 0x000001fe);
}

static void rk628_hdmirx_ctrl_disable(struct rk628_hdmirx *hdmirx)
{
	reset_control_assert(hdmirx->hdmirx);
	reset_control_assert(hdmirx->hdmirx_pon);
	clk_disable_unprepare(hdmirx->pclk);
	clk_disable_unprepare(hdmirx->aud_clk);
	clk_disable_unprepare(hdmirx->imodet_clk);
}

static void rk628_hdmirx_bridge_enable(struct drm_bridge *bridge)
{
	bool locked;
	u32 value, i, hact, vact, bus_width, hdisplay, vdisplay;
	struct rk628_hdmirx *hdmirx = bridge_to_hdmirx(bridge);

	/* force 594m mode to yuv420 format */
	if (hdmirx->mode.clock == 594000) {
		/*
		 * bit30 is used to indicate whether it is
		 * yuv420 format
		 */
		bus_width = hdmirx->mode.clock | BIT(30);
		hdisplay = hdmirx->mode.hdisplay / 2;
	} else {
		bus_width = hdmirx->mode.clock;
		hdisplay = hdmirx->mode.hdisplay;
	}

	vdisplay = hdmirx->mode.vdisplay;

	phy_set_bus_width(hdmirx->phy, bus_width);
	phy_power_on(hdmirx->phy);
	usleep_range(10*1000, 11*1000);
	rk628_hdmirx_ctrl_enable(hdmirx);

	/* if hdmirx ctrl unlock or get incorrect timing, reset ctrl and phy */
	for (i = 0; i < 5; i++) {
		usleep_range(100*1000, 110*1000);
		regmap_read(hdmirx->regmap, HDMI_RX_SCDC_REGS1, &value);
		dev_dbg(hdmirx->dev, "HDMI_RX_SCDC_REGS1:0x%x\n", value);
		value = (value >> 8) & 0xf;

		regmap_read(hdmirx->regmap, HDMI_RX_MD_HACT_PX, &hact);
		regmap_read(hdmirx->regmap, HDMI_RX_MD_VAL, &vact);

		hact = hact & 0xffff;
		vact = vact & 0xffff;
		dev_dbg(hdmirx->dev, "hact:%d,vact:%d\n", hact, vact);

		if (value == 0xf && hact == hdisplay && vact == vdisplay)
			locked = true;
		else
			locked = false;

		if (!locked) {
			rk628_hdmirx_ctrl_disable(hdmirx);
			usleep_range(10*1000, 11*1000);
			phy_power_off(hdmirx->phy);
			usleep_range(10*1000, 11*1000);
			phy_power_on(hdmirx->phy);
			usleep_range(10*1000, 11*1000);
			rk628_hdmirx_ctrl_enable(hdmirx);
		} else {
			/* hdmirx ctrl get correct timing, enable output */
			regmap_write(hdmirx->regmap, HDMI_RX_DMI_DISABLE_IF,
				     0x000001ff);
			return;
		}
	}

	dev_err(hdmirx->dev, "hdmirx channel can't lock!\n");

}

static void rk628_hdmirx_bridge_disable(struct drm_bridge *bridge)
{
	struct rk628_hdmirx *hdmirx = bridge_to_hdmirx(bridge);

	rk628_hdmirx_ctrl_disable(hdmirx);
	phy_power_off(hdmirx->phy);
}

static int rk628_hdmirx_bridge_attach(struct drm_bridge *bridge)
{
	struct rk628_hdmirx *hdmirx = bridge_to_hdmirx(bridge);
	struct device *dev = hdmirx->dev;
	int ret;

	ret = drm_of_find_panel_or_bridge(dev->of_node, 1, -1,
					  NULL, &hdmirx->bridge);
	if (ret) {
		dev_err(dev, "failed to find next bridge\n");
		return ret;
	}

	ret = drm_bridge_attach(bridge->encoder, hdmirx->bridge, bridge);
	if (ret) {
		dev_err(dev, "failed to attach bridge\n");
		return ret;
	}

	bridge->next = hdmirx->bridge;

	return 0;
}

static void rk628_hdmirx_bridge_mode_set(struct drm_bridge *bridge,
				    struct drm_display_mode *orig_mode,
				    struct drm_display_mode *mode)
{
	struct rk628_hdmirx *hdmirx = bridge_to_hdmirx(bridge);

	memcpy(&hdmirx->mode, mode, sizeof(hdmirx->mode));
}

static const struct drm_bridge_funcs rk628_hdmirx_bridge_funcs = {
	.attach = rk628_hdmirx_bridge_attach,
	.enable = rk628_hdmirx_bridge_enable,
	.disable = rk628_hdmirx_bridge_disable,
	.mode_set = rk628_hdmirx_bridge_mode_set,
};

static int rk628_hdmirx_probe(struct platform_device *pdev)
{
	struct rk628 *rk628 = dev_get_drvdata(pdev->dev.parent);
	struct device *dev = &pdev->dev;
	struct platform_device_info pdevinfo;
	struct rk628_hdmirx *hdmirx;
	int ret, irq;

	if (!of_device_is_available(dev->of_node))
		return -ENODEV;

	hdmirx = devm_kzalloc(dev, sizeof(*hdmirx), GFP_KERNEL);
	if (!hdmirx)
		return -ENOMEM;

	hdmirx->dev = dev;
	hdmirx->parent = rk628;
	platform_set_drvdata(pdev, hdmirx);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0)
		return irq;

	hdmirx->grf = rk628->grf;
	if (!hdmirx->grf)
		return -ENODEV;

	hdmirx->pclk = devm_clk_get(dev, "pclk");
	if (IS_ERR(hdmirx->pclk)) {
		ret = PTR_ERR(hdmirx->pclk);
		dev_err(dev, "failed to get pclk: %d\n", ret);
		return ret;
	}

	hdmirx->cec_clk = devm_clk_get(dev, "cec");
	if (IS_ERR(hdmirx->cec_clk)) {
		ret = PTR_ERR(hdmirx->cec_clk);
		dev_err(dev, "failed to get cec clk: %d\n", ret);
		return ret;
	}

	hdmirx->aud_clk = devm_clk_get(dev, "audio");
	if (IS_ERR(hdmirx->aud_clk)) {
		ret = PTR_ERR(hdmirx->aud_clk);
		dev_err(dev, "failed to get audio clk: %d\n", ret);
		return ret;
	}

	hdmirx->imodet_clk = devm_clk_get(dev, "imodet");
	if (IS_ERR(hdmirx->imodet_clk)) {
		ret = PTR_ERR(hdmirx->imodet_clk);
		dev_err(dev, "failed to get imodet clk: %d\n", ret);
		return ret;
	}

	hdmirx->hdmirx = of_reset_control_get(dev->of_node, "hdmirx");
	if (IS_ERR(hdmirx->hdmirx)) {
		ret = PTR_ERR(hdmirx->hdmirx);
		DRM_DEV_ERROR(dev, "failed to get hdmirx control: %d\n", ret);
		return ret;
	}

	hdmirx->hdmirx_pon = of_reset_control_get(dev->of_node, "hdmirx_pon");
	if (IS_ERR(hdmirx->hdmirx_pon)) {
		ret = PTR_ERR(hdmirx->hdmirx_pon);
		DRM_DEV_ERROR(dev, "failed to get hdmirx_pon control: %d\n", ret);
		return ret;
	}

	hdmirx->phy = devm_of_phy_get(dev, dev->of_node, NULL);
	if (IS_ERR(hdmirx->phy)) {
		ret = PTR_ERR(hdmirx->phy);
		dev_err(dev, "failed to get phy: %d\n", ret);
		return ret;
	}

	hdmirx->regmap = devm_regmap_init_i2c(rk628->client,
					      &rk628_hdmirx_regmap_config);
	if (IS_ERR(hdmirx->regmap)) {
		ret = PTR_ERR(hdmirx->regmap);
		dev_err(dev, "failed to allocate register map: %d\n", ret);
		return ret;
	}

	hdmirx->base.funcs = &rk628_hdmirx_bridge_funcs;
	hdmirx->base.of_node = dev->of_node;
	drm_bridge_add(&hdmirx->base);

	memset(&pdevinfo, 0, sizeof(pdevinfo));
	pdevinfo.parent = dev;
	pdevinfo.id = PLATFORM_DEVID_AUTO;

	return 0;
}

static int rk628_hdmirx_remove(struct platform_device *pdev)
{
	struct rk628_hdmirx *hdmirx = platform_get_drvdata(pdev);

	drm_bridge_remove(&hdmirx->base);

	return 0;
}

static const struct of_device_id rk628_hdmirx_of_match[] = {
	{ .compatible = "rockchip,rk628-hdmirx", },
	{},
};
MODULE_DEVICE_TABLE(of, rk628_hdmirx_of_match);

static struct platform_driver rk628_hdmirx_driver = {
	.driver = {
		.name = "rk628-hdmirx",
		.of_match_table = of_match_ptr(rk628_hdmirx_of_match),
	},
	.probe = rk628_hdmirx_probe,
	.remove = rk628_hdmirx_remove,
};
module_platform_driver(rk628_hdmirx_driver);

MODULE_AUTHOR("Algea Cao <algea.cao@rock-chips.com>");
MODULE_DESCRIPTION("Rockchip RK628 HDMI RX driver");
MODULE_LICENSE("GPL v2");
