//***************************************************************************
//!file     si_sii5293_registers.h
//!brief    5293 Device Register Manifest Constants.
//
// No part of this work may be reproduced, modified, distributed,
// transmitted, transcribed, or translated into any language or computer
// format, in any form or by any means without written permission of
// Silicon Image, Inc., 1140 East Arques Avenue, Sunnyvale, California 94085
//
// Copyright 2007-2013, Silicon Image, Inc.  All rights reserved.
//***************************************************************************/

#ifndef __SI_SII5293_REGISTERS_H__
#define __SI_SII5293_REGISTERS_H__

#include "si_drv_cra_cfg.h"
#include "si_regs_mhl5293.h"

//------------------------------------------------------------------------------
// Registers in Page 0  (0xB0/0xB2)
//------------------------------------------------------------------------------

#define REG_DEV_IDL_RX              (PP_PAGE | 0x02)
#define REG_DEV_IDH_RX              (PP_PAGE | 0x03)
#define REG_DEV_REV                 (PP_PAGE | 0x04)

#define RX_A__SRST                  (PP_PAGE | 0x05)
#define RX_M__SRST__ACR_RST_AUTO		0x40
#define RX_M__SRST__AAC_RST				0x20
#define RX_M__SRST__SRST_AUTO			0x10
#define RX_M__SRST__HDCP_RST			0x08
#define RX_M__SRST__ACR_RST				0x04
#define RX_M__SRST__FIFO_RST			0x02
#define RX_M__SRST__SRST				0x01

#define RX_A__STATE				    (PP_PAGE | 0x06)
#define RX_M__STATE__PCLK_STABLE		0x10
#define RX_M__STATE__PWR5V				0x08
#define RX_M__STATE__VSYNC				0x04
#define RX_M__STATE__CKDT				0x02
#define RX_M__STATE__SCDT				0x01

#define RX_A__SWRST2			    (PP_PAGE | 0x07)
#define RX_M__SWRST2__AUDIO_FIFO_AUTO	0x40
#define RX_M__SWRST2__CEC_SRST          0x04
#define RX_M__SWRST2__CBUS_SRST         0x02

#define RX_A__SYS_CTRL1			    (PP_PAGE | 0x08)
#define RX_M__SYS_CTRL1__OCLKDIV		0xC0
#define RX_M__SYS_CTRL1__ICLK			0x30
#define RX_M__SYS_CTRL1__BSEL			0x04
#define RX_M__SYS_CTRL1__EDGE			0x02
#define RX_M__SYS_CTRL1__PDALL			0x01

#define RX_A__SYS_SWTCH			    (PP_PAGE | 0x09)
#define RX_M__SYS_SWTCH__RX0_EN         0x01
#define RX_M__SYS_SWTCH__DDC0_EN         0x10
#define RX_M__SYS_SWTCH__DDC_DEL_EN		0x80

#define REG_HP_CTRL                 (PP_PAGE | 0x0B)
#define VAL_HP_PORT0_MASK               0x03
#define VAL_HP_PORT_ALL_HI              0x55
#define VAL_HP_PORT_ALL_LO              0x00
#define VAL_HP_PORT_MHL                 0x22

#define RX_A__HDCP_DEBUG		0x031
#define RX_M__HDCP_DEBUG__CLEAR_RI		0x80

#define RX_A__HDCP_STAT			0x032
#define RX_M__HDCP_STAT__DECRIPTING		0x20
#define RX_M__HDCP_STAT__AUTHENTICATED	0x10

// Video Registers --------------------------------------------------------------------------------

#define RX_A__H_RESL			0x03A // [7:0]
#define RX_A__H_RESH			0x03B // [12:8]

#define RX_A__V_RESL			0x03C // [7:0]
#define RX_A__V_RESH			0x03D // [10:8]

#define RX_A__VID_CTRL			0x048
#define RX_M__VID_CTRL__INVERT_VSYNC			0x80
#define RX_M__VID_CTRL__INVERT_HSYNC			0x40
#define RX_M__VID_CTRL__YCBCR_2_RGB_MODE		0x04
#define RX_M__VID_CTRL__EXT_BIT_MODE			0x02
#define RX_M__VID_CTRL__RGB_2_YCBCR_MODE		0x01

#define RX_A__VID_MODE2			0x049
#define RX_M__VID_MODE2__EVEN_ODD_POL			0x20
#define RX_M__VID_MODE2__YCBCR_2_RGB_RANGE_EN	0x08
#define RX_M__VID_MODE2__YCBCR_2_RGB_EN			0x04
#define RX_M__VID_MODE2__CLIP_INPUTS_YC			0x02
#define RX_M__VID_MODE2__RANGE_CLIP				0x01

#define RX_A__VID_MODE			0x04A
#define RX_M__VID_MODE__SYNC_CODES_EN			0x80
#define RX_M__VID_MODE__MUX_YC					0x40
#define RX_M__VID_MODE__DITHER					0x20
#define RX_M__VID_MODE__RGB_2_YCBCR_RANGE		0x10
#define RX_M__VID_MODE__RGB_2_YCBCR_EN			0x08
#define RX_M__VID_MODE__UPSMPL					0x04
#define RX_M__VID_MODE__DOWSMPL					0x02

#define RX_A__VID_BLANK1		0x04B // Blank Data[7:0]
#define RX_A__VID_BLANK2		0x04C // Blank Data[15:8]
#define RX_A__VID_BLANK3		0x04D // Blank Data[23:16]

#define RX_A__DE_PIX1			0x04E // Number of pixels per DE; bits [7:0]
#define RX_A__DE_PIX2			0x04F // Number of pixels per DE; bits [11:8]

#define RX_A__DE_LINE1			0x050 // Number of DE per frame/field; bits [7:0]
#define RX_A__DE_LINE2			0x051 // Number of pixels per DE[10:8]

#define RX_A__VTAVL				0x052 // Vsync to Active Video Lines

#define RX_A__VID_VFP			0x053 // Video Front Porch

#define RX_A__VID_F2BPM			0x054 // Field to Back Porch

#define RX_A__VID_STAT			0x055
#define RX_M__VID_STAT__INTERLACE		0x04
#define RX_M__VID_STAT__VSYNC_POL		0x02
#define RX_M__VID_STAT__HSYNC_POL		0x01

#define RX_A__VID_CH_MAP		0x056
#define RX_M__VID_CH_MAP__CHANNEL_MAP	0x07

#define RX_VID_CTRL2			0x057
#define RX_M__VID_CTRL2__HJITTER_EN		0x01

#define RX_A__VID_HFP			0x059 // Horizontal Front Porch

#define RX_A__VID_HAW1			0x05B // Hsync Active Width; bits [7:0]
#define RX_A__VID_HAW2			0x05C // Hsync Active Width

#define RX_A__VID_AOF			0x05F

#define RX_A_VIDA_XPCLK_BASE	0x69
#define RX_A_VIDA_XPCLK_EN		0x6A
#define RX_M_VIDA_XPCLK_EN__EN			0x01

#define RX_A__VID_XPCNT0		0x06E // # of xclk per 2048 Video clk, low byte
#define RX_A__VID_XPCNT1		0x06F // # of xclk per 2048 Video clk, high byte


// Interrupts Registers ---------------------------------------------------------------------------
#define RX_A__INTR_STATE		0x070
#define RX_M__INTR_STATE				0x01

#define RX_A__INTR1				0x071
#define RX_M__INTR1__ACR_HW_CTS_CHANGED	0x80
#define RX_M__INTR1__ACR_HW_N_CHANGED	0x40
#define RX_M__INTR1__ACR_PACKET_ERROR	0x20
#define RX_M__INTR1__ACR_PLL_UNLOCKED	0x10
#define RX_M__INTR1__AUTH_START			0x02
#define RX_M__INTR1__AUTH_DONE			0x01

#define RX_A__INTR2				0x072
#define RX_M__INTR2__HDMI_MODE			0x80
#define RX_M__INTR2__VSYNC				0x40
#define RX_M__INTR2__SOFTW_INTR			0x20
#define RX_M__INTR2__CLOCK_DETECT		0x10
#define RX_M__INTR2__SCDT				0x08
#define RX_M__INTR2__GOT_CTS_PACKET		0x04
#define RX_M__INTR2__GOT_AUDIO_PKT		0x02
#define RX_M__INTR2__VID_CLK_CHANGED	0x01

#define RX_A__INTR3				0x073
#define RX_M__INTR3__NEW_CP_PACKET		0x80
#define RX_M__INTR3__CP_SET_MUTE		0x40
#define RX_M__INTR3__PARITY_ERR			0x20
#define RX_M__INTR3__NEW_UNREC_PACKET	0x10
#define RX_M__INTR3__NEW_MPEG_PACKET	0x08
#define RX_M__INTR3__NEW_AUD_PACKET		0x04
#define RX_M__INTR3__NEW_SP_PACKET		0x02
#define RX_M__INTR3__NEW_AVI_PACKET		0x01

#define RX_A__INTR4				0x074
#define RX_M__INTR4__HDCP				0x40
#define RX_M__INTR4__T4					0x20
#define RX_M__INTR4__NO_AVI				0x10
#define RX_M__INTR4__CTS_OVERRUN		0x08
#define RX_M__INTR4__CTS_UNDERRUN		0x04
#define RX_M__INTR4__FIFO_OVERUN		0x02
#define RX_M__INTR4__FIFO_UNDERUN		0x01

#define RX_A__INTR1_MASK		0x075

#define RX_A__INTR2_MASK		0x076

#define RX_A__INTR3_MASK		0x077

#define RX_A__INTR4_MASK		0x078

#define RX_A__INT_CTRL			0x079
#define RX_M__INT_CTRL__SOFT_INTR				0x08
#define RX_M__INT_CTRL__OPEN_DRAIN				0x04
#define RX_M__INT_CTRL__POLARITY				0x02

#define RX_A__INT_IF_CTRL		0x07A
#define RX_M__INT_IF_CTRL__NEW_VSI				0x40
#define RX_M__INT_IF_CTRL__NEW_ACP				0x20
#define RX_M__INT_IF_CTRL__NEW_UNREC			0x10
#define RX_M__INT_IF_CTRL__NEW_MPEG				0x08
#define RX_M__INT_IF_CTRL__NEW_AUD				0x04
#define RX_M__INT_IF_CTRL__NEW_SPD				0x02
#define RX_M__INT_IF_CTRL__NEW_AVI				0x01

#define RX_A__INTR5				0x07B
#define RX_M__INTR5__FN_CHANGED					0x80
#define RX_M__INTR5__AAC_DONE					0x40
#define RX_M__INTR5__AUDIO_LINK_EROR			0x20
#define RX_M__INTR5__V_RES_CHANGE				0x10
#define RX_M__INTR5__H_RES_CHANGE				0x08
#define RX_M__INTR5__POLARITY_CHANGE			0x04
#define RX_M__INTR5__INTERLACED_CHANGED			0x02
#define RX_M__INTR5__AUDIO_FS_CHANGED			0x01

#define RX_A__INTR6				0x07C
#define RX_M__INTR6__DC_ERROR					0x80
#define RX_M__INTR6__AUD_FLAT					0x40
#define RX_M__INTR6__CHST_READY					0x20
#define RX_M__INTR6__NEW_ACP_PACKET				0x04
#define RX_M__INTR6__CABLE_UNPLUG				0x01

#define RX_A__INTR5_MASK		0x07D

#define RX_A__INTR6_MASK		0x07E

// ACR Registers ---part1--------------------------------------------------------------------------
#define RX_A__APLL_POLE			0x088 // the rest of settings starts from 0x200 address
#define RX_A__APLL_CLIP			0x089 // the rest of settings starts from 0x200 address

// Interrupts Registers (continued) ---------------------------------------------------------------

#define RX_A__INTR7				0x090
#define RX_M__INTR7__NO_VSI_PACKET				0x80
#define RX_M__INTR7__NEW_VSI_PACKET				0x40
#define RX_M__INTR7__PCLK_STOPPED				0x04
#define RX_M__INTR7__PCLK_STABLE_CHANGED		0x01

#define RX_A__INTR8				0x091
#define RX_M__INTR8__CABLE_IN					0x02

#define RX_A__INTR7_MASK		0x092

#define RX_A__INTR8_MASK		0x093


#define REG_INTR_STATE_2    		    (PP_PAGE | 0x94)
#define BIT_CEC_INTR                        BIT0
#define BIT_CBUS_INTR                       BIT1
#define BIT_RX_INTR                         BIT2


// Auto Exception Control -------------------------------------------------------------------------
#define RX_A__AEC_CTRL			0x0B5
#define RX_M__AEC_CTRL__AAC_OUT_OFF_EN			0x20
#define RX_M__AEC_CTRL__AVC_EN					0x04
#define RX_M__AEC_CTRL__AAC_EN					0x01

#define RX_A__AEC_EN1			0x0B6
#define RX_M__AEC_EN1__CABLE_UNPLUG				0x01
#define RX_M__AEC_EN1__PLL_UNLOCKED				0x02
#define RX_M__AEC_EN1__ACR_N_CHANGED			0x04
#define RX_M__AEC_EN1__ACR_CTS_CHANGED			0x08
#define RX_M__AEC_EN1__VIDEO_CLOCK_CHANGED		0x10
#define RX_M__AEC_EN1__INFOFRAME_CP_MUTE_SET	0x20
#define RX_M__AEC_EN1__SYNC_DETECT				0x40
#define RX_M__AEC_EN1__CLOCK_DETECT				0x80

#define RX_A__AEC_EN2			0x0B7
#define RX_M__AEC_EN2__HDMI_MODE				0x01
#define RX_M__AEC_EN2__AUDIO_FIFO_UNDERUN		0x02
#define RX_M__AEC_EN2__AUDIO_FIFO_OVERUN		0x04
#define RX_M__AEC_EN2__CTS_REUSED				0x08
#define RX_M__AEC_EN2__CHANGE_OF_THE_FS			0x10
#define RX_M__AEC_EN2__CHANGE_OF_INTERLACED		0x20
#define RX_M__AEC_EN2__POLARITY_CHANGE			0x40
#define RX_M__AEC_EN2__H_RESOLUTION_CHANGE		0x80

#define RX_A__AEC_EN3			0x0B8
#define RX_M__AEC_EN3__V_RESOLUTION_CHANGED		0x01
#define RX_M__AEC_EN3__LINK_ERROR				0x02
#define RX_M__AEC_EN3__FN_CLK_CHANGED			0x04

#define RX_A__AVC_EN1			0x0B9
#define RX_M__AVC_EN1__RGB2YCbCr_CFG			0x01
#define RX_M__AVC_EN1__RGB2YCbCr_MODE			0x02
#define RX_M__AVC_EN1__RGB2YCbCr_RANGE			0x04
#define RX_M__AVC_EN1__YCbCr2RGB_CFG			0x08
#define RX_M__AVC_EN1__YCbCr2RGB_MODE			0x10
#define RX_M__AVC_EN1__YCbCr2RGB_RANGE			0x20
#define RX_M__AVC_EN1__UP_SAMPLER_CFG			0x40
#define RX_M__AVC_EN1__DOWN_SAMPLER_CFG			0x80

#define RX_A__AVC_EN2			0x0BA
#define RX_M__AVC_EN2__EXT_BIT					0x01
#define RX_M__AVC_EN2__DITHER					0x02
#define RX_M__AVC_EN2__YC_MUX					0x04
#define RX_M__AVC_EN2__SYNC_CODES				0x08
#define RX_M__AVC_EN2__BLANK_DATA				0x10
#define RX_M__AVC_EN2__PIXEL_REPLICATION		0x20
#define RX_M__AVC_EN2__ODCK_DIV					0x40
// ECC Registers ----------------------------------------------------------------------------------
#define RX_A__ECC_CTRL			0x0BB
#define RX_M__ECC_CTRL__CAPTURE_CNT		0x01

#define RX_A__BCH_THRES			0x0BC

#define RX_A__T4_THRES			0x0BD
#define RX_M__T4_THRES					0x7F

#define RX_A__T4_UNC_THRES		0x0BE
#define RX_M__T4_UNC_THRES				0x7F

#define RX_A__ECC0_HDCP_THRES	0x0C5 // bits 0...7
#define RX_A__ECC0_HDCP_THRES2	0x0C6 // bits 8...15
#define RX_C__ECC0_HDCP_THRES_VALUE		0x0B40 // recommended value

#define	RX_A__HDCP_ERR			0x0CD

//------------------------------------------------------------------------------
// Registers in Page 1      (0xD0)
//------------------------------------------------------------------------------

#define GPIO_MODE					(PP_PAGE_1 | 0x06)
#define VAL_GPIO_0_3D_INDICATOR                  0x01

#define REG_COMB_CTRL               (PP_PAGE_1 | 0x0B)
#define REG_DPLL_CFG3               (PP_PAGE_1 | 0x13)
#define REG_PEQ_VAL0                (PP_PAGE_1 | 0x17)

#define REG_RX_CTRL1                (PP_PAGE_1 | 0x6C)

#define REG_RX_CTRL5                (PP_PAGE_1 | 0x70)
#define VAL_TERM_ON                     0x00
#define VAL_TERM_MHL                    0x01
#define VAL_TERM_SURGE                  0x02
#define VAL_TERM_OFF                    0x03
#define MSK_TERM                        (0x03 | 0x80)

#define REG_DPLL_BW_CFG2            (PP_PAGE_1 | 0x8A)

#define RX_CBUS_CH_RST_CTRL           	(PP_PAGE_1 | 0xE5)
#define BIT_TRI_STATE_EN                    BIT7

#define REG_CBUS_HPD_OEN_CTRL		(PP_PAGE_1 | 0xEA)  // used for ES_0_0 ONLY
#define REG_CBUS_HPD_PE_CTRL		(PP_PAGE_1 | 0xEB)  // used for ES_0_0 ONLY
#define REG_CBUS_HPD_PU_CTRL		(PP_PAGE_1 | 0xEC)  // used for ES_0_0 ONLY
#define REG_CBUS_HPD_OVRT_CTRL	(PP_PAGE_1 | 0xED)  // used for ES_0_0 ONLY

// ACR Registers ---part2--------------------------------------------------------------------------
#define RX_A__ACR_CTRL1			0x200
#define RX_M__ACR_CTRL1__CTS_DROP_AUTO	0x80
#define RX_M__ACR_CTRL1__POST_SEL		0x40
#define RX_M__ACR_CTRL1__UPLL_SEL		0x20
#define RX_M__ACR_CTRL1__CTS_SEL		0x10
#define RX_M__ACR_CTRL1__N_SEL			0x08
#define RX_M__ACR_CTRL1__CTS_REUSE_AUTO	0x04
#define RX_M__ACR_CTRL1__FS_SEL			0x02
#define RX_M__ACR_CTRL1__ACR_INIT		0x01

#define RX_A__FREQ_SVAL			0x202
#define RX_M__FREQ_SVAL__SW_MASTER_CLK_IN_FREQ	0xC0
#define RX_M__FREQ_SVAL__SW_MASTER_CLK_OUT_FREQ	0x30
#define RX_M__FREQ_SVAL__SW_AUDIO_SAMPLING_FREQ	0x0F

#define RX_A__N_SVAL1			0x203 // [7:0]
#define RX_A__N_SVAL2			0x204 // [15:8]
#define RX_A__N_SVAL3			0x205 // [19:16]

#define RX_A__N_HVAL1			0x206 // [7:0]
#define RX_A__N_HVAL2			0x207 // [15:8]
#define RX_A__N_HVAL3			0x208 // [19:16]

#define RX_A__CTS_SVAL1			0x209 // [7:0]
#define RX_A__CTS_SVAL2			0x20A // [15:8]
#define RX_A__CTS_SVAL3			0x20B // [19:16]

#define RX_A__CTS_HVAL1			0x20C // [7:0]
#define RX_A__CTS_HVAL2			0x20D // [15:8]
#define RX_A__CTS_HVAL3			0x20E // [19:16]

#define RX_A__UPLL_SVAL			0x20F // [6:0]
#define RX_A__UPLL_HVAL			0x210 // [6:0]

#define RX_A__POST_SVAL			0x211 // [5:0]
#define RX_A__POST_HVAL			0x212 // [5:0]

#define RX_A__LK_WIN_SVAL		0x213
#define RX_M__LK_WIN_SVAL__WINDOW_DIVIDER	0x1E
#define RX_M__LK_WIN_SVAL__EXACT			0x01

#define RX_A__LK_THRS_SVAL1		0x214 // PLL Locked Stability Threshold[7:0]
#define RX_A__LK_THRS_SVAL2		0x215 // PLL Locked Stability Threshold[15:8]
#define RX_A__LK_THRS_SVAL3		0x216 // PLL Locked Stability Threshold[19:16]

#define RX_A__PCLK_FS			0x217
#define RX_M__PCLK_FS__FS_FILTER			0x10
#define RX_M__PCLK_FS__SPDIF_EXTRACRTED_FS	0x0F

#define RX_A__ACR_CTRL3			0x218
#define RX_M__ACR_CTRL3__CTS_THRESHOLD	0x78
#define RX_M__ACR_CTRL3__MCLK_LOOPBACK	0x04
#define RX_M__ACR_CTRL3__LOG_WIN_EN		0x02
#define RX_M__ACR_CTRL3__POST_DIV2_EN	0x01

// Audio Out Registers ----------------------------------------------------------------------------

#define RX_A__TDM_CTRL1			0x223
#define RX_M__TDM_CTRL1__TDM_EN			0x01

#define RX_A__TDM_CTRL2			0x224

#define RX_A__I2S_CTRL1			0x226
#define RX_M__I2S_CTRL1__INVALID_EN		0x80
#define RX_M__I2S_CTRL1__EDGE			0x40
#define RX_M__I2S_CTRL1__SIZE			0x20
#define RX_M__I2S_CTRL1__MSB			0x10
#define RX_M__I2S_CTRL1__WS				0x08
#define RX_M__I2S_CTRL1__JUSTIFY		0x04
#define RX_M__I2S_CTRL1__DATA_DIR		0x02
#define RX_M__I2S_CTRL1__1ST_BIT		0x01

#define RX_A__I2S_CTRL2			0x227
#define RX_M__I2S_CTRL2__SD3_EN			0x80
#define RX_M__I2S_CTRL2__SD2_EN			0x40
#define RX_M__I2S_CTRL2__SD1_EN			0x20
#define RX_M__I2S_CTRL2__SD0_EN			0x10
#define RX_M__I2S_CTRL2__MCLK_EN		0x08
#define RX_M__I2S_CTRL2__MUTE_FLAT		0x04
#define RX_M__I2S_CTRL2__PCM			0x01

#define RX_A__I2S_MAP			0x228
#define RX_M__I2S_MAP__SD3_MAP			0xC0
#define RX_M__I2S_MAP__SD2_MAP			0x30
#define RX_M__I2S_MAP__SD1_MAP			0x0C
#define RX_M__I2S_MAP__SD0_MAP			0x03

#define RX_A__AUDRX_CTRL		0x229
#define RX_M__AUDRX_CTRL__I2S_LENGTH_OVERWRITE	0x80
#define RX_M__AUDRX_CTRL__HW_MUTE_EN			0x20
#define RX_M__AUDRX_CTRL__PASS_SPDIF_ERR		0x10
#define RX_M__AUDRX_CTRL__PASS_AUDIO_ERR		0x08
#define RX_M__AUDRX_CTRL__I2S_MODE				0x04
#define RX_M__AUDRX_CTRL__SPDIF_MODE			0x02
#define RX_M__AUDRX_CTRL__SPDIF_EN				0x01

#define RX_A__CHST1				0x22A
#define RX_A__CHST2				0x22B
#define RX_A__CHST3				0x22C
#define RX_A__CHST4				0x230
#define RX_A__CHST4__BIT_AUD_FS	0x0F // AUD_FS, sampling frequency

#define RX_A__SW_OW				0x22E
#define RX_M__SW_OW__SWAP_CH3			0x80
#define RX_M__SW_OW__SWAP_CH2			0x40
#define RX_M__SW_OW__SWAP_CH1			0x20
#define RX_M__SW_OW__SWAP_CH0			0x10
#define RX_M__SW_OW__OW_BIT2			0x04
#define RX_M__SW_OW__OW_EN				0x01

#define RX_A__OW_15_8			0x22F // overwrite for bits 15-8

#define RX_A__CHST4				0x230
#define RX_M__CHST4__AUD_ACCURACY		0x30
#define RX_M__CHST4__AUD_SAMPLE_F		0x0F

#define RX_A__CHST5				0x231
#define RX_M__CHST5__AUD_LENGTH			0x0E
#define RX_M__CHST5__AUD_LENGTH_MAX		0x01

#define RX_A__AUDO_MUTE			0x232
#define RX_M__AUDO_MUTE__REG_I2S_LENGTH	0xF0 // overwrite for bits 32-35
#define RX_M__AUDO_MUTE__CH3_MUTE		0x08
#define RX_M__AUDO_MUTE__CH2_MUTE		0x04
#define RX_M__AUDO_MUTE__CH1_MUTE		0x02
#define RX_M__AUDO_MUTE__CH0_MUTE		0x01


// Audio Path Registers ---------------------------------------------------------------------------
#define RX_A__AUDP_STAT			0x234
#define RX_M__AUDP_STAT__LAYOUT				0x08 // 1 bit
#define RX_M__AUDP_STAT__MUTE_STATUS		0x04
#define RX_M__AUDP_STAT__HDMI_MODE_ENABLED	0x02
#define RX_M__AUDP_STAT__HDMI_MODE_DETECTED	0x01

#define RX_A__CRIT1				0x235

#define RX_A__AUDP_MUTE			0x237
#define RX_M__AUDP_MUTE__MUTE_OUT_POLARITY	0x04
#define RX_M__AUDP_MUTE__AUDIO_MUTE			0x02
#define RX_M__AUDP_MUTE__VIDEO_MUTE			0x01

// FIFO read write ptr difference
#define RX_A__AUDP_FIFO			0x239
#define RX_M__AUDP_FIFO					0x7F

// System Power Down Registers --------------------------------------------------------------------
#define RX_A__PD_TOT			0x23C
#define RX_M__PD_TOT					0x01

#define RX_A__PD_SYS2			0x23E
#define RX_M__PD_SYS2__PD_PCLK			0x80
#define RX_M__PD_SYS2__PD_MCLK			0x40
#define RX_M__PD_SYS2__PD_Q				0x04
#define RX_M__PD_SYS2__PD_VHDE			0x02
#define RX_M__PD_SYS2__PD_ODCK			0x01

#define RX_A__PD_SYS			0x23F
#define RX_M__PD_SYS__PD_AO				0x80
#define RX_M__PD_SYS__PD_VO				0x40
#define RX_M__PD_SYS__PD_APLL			0x20
#define RX_M__PD_SYS__PD_12CH			0x08
#define RX_M__PD_SYS__PD_FULL			0x04
#define RX_M__PD_SYS__PDOSC				0x02
#define RX_M__PD_SYS__PD_XTAL			0x01

// AVI InfoFrame ----------------------------------------------------------------------------------
#define RX_A__AVI_TYPE			0x240
#define RX_A__AVI_VERS			0x241
#define RX_A__AVI_LENGTH		0x242
#define RX_A__AVI_CHSUM			0x243
#define RX_A__AVI_DBYTE1		0x244
#define RX_A__AVI_DBYTE2		0x245
#define RX_A__AVI_DBYTE3		0x246
#define RX_A__AVI_DBYTE4		0x247
#define RX_A__AVI_DBYTE5		0x248
#define RX_A__AVI_DBYTE6		0x249
#define RX_A__AVI_DBYTE7		0x24A
#define RX_A__AVI_DBYTE8		0x24B
#define RX_A__AVI_DBYTE9		0x24C
#define RX_A__AVI_DBYTE10		0x24D
#define RX_A__AVI_DBYTE11		0x24E
#define RX_A__AVI_DBYTE12		0x24F
#define RX_A__AVI_DBYTE13		0x250
#define RX_A__AVI_DBYTE14		0x251
#define RX_A__AVI_DBYTE15		0x252

// Source Product Desription (SPD) Info Frame -----------------------------------------------------
#define RX_A__SPD_TYPE			0x260
#define RX_A__SPD_VERS			0x261
#define RX_A__SPD_LENGTH		0x262
#define RX_A__SPD_CHSUM			0x263
#define RX_A__SPD_DBYTE1		0x264
#define RX_A__SPD_DBYTE2		0x265
#define RX_A__SPD_DBYTE3		0x266
#define RX_A__SPD_DBYTE4		0x267
#define RX_A__SPD_DBYTE5		0x268
#define RX_A__SPD_DBYTE6		0x269
#define RX_A__SPD_DBYTE7		0x26A
#define RX_A__SPD_DBYTE8		0x26B
#define RX_A__SPD_DBYTE9		0x26C
#define RX_A__SPD_DBYTE10		0x26D
#define RX_A__SPD_DBYTE11		0x26E
#define RX_A__SPD_DBYTE12		0x26F
#define RX_A__SPD_DBYTE13		0x270
#define RX_A__SPD_DBYTE14		0x271
#define RX_A__SPD_DBYTE15		0x272
#define RX_A__SPD_DBYTE16		0x273
#define RX_A__SPD_DBYTE17		0x274
#define RX_A__SPD_DBYTE18		0x275
#define RX_A__SPD_DBYTE19		0x276
#define RX_A__SPD_DBYTE20		0x277
#define RX_A__SPD_DBYTE21		0x278
#define RX_A__SPD_DBYTE22		0x279
#define RX_A__SPD_DBYTE23		0x27A
#define RX_A__SPD_DBYTE24		0x27B
#define RX_A__SPD_DBYTE25		0x27C
#define RX_A__SPD_DBYTE26		0x27D
#define RX_A__SPD_DBYTE27		0x27E

// SPD Packet decode address
#define RX_A__SPD_DEC			0x27F

// Audio (AUD) InfoFrame --------------------------------------------------------------------------
#define RX_A__AUD_TYPE			0x280
#define RX_A__AUD_VERS			0x281
#define RX_A__AUD_LENGTH		0x282
#define RX_A__AUD_CHSUM			0x283
#define RX_A__AUD_DBYTE1		0x284
#define RX_A__AUD_DBYTE2		0x285
#define RX_A__AUD_DBYTE3		0x286
#define RX_A__AUD_DBYTE4		0x287
#define RX_A__AUD_DBYTE5		0x288
#define RX_A__AUD_DBYTE6		0x289
#define RX_A__AUD_DBYTE7		0x28A
#define RX_A__AUD_DBYTE8		0x28B
#define RX_A__AUD_DBYTE9		0x28C
#define RX_A__AUD_DBYTE10		0x28D

// MPEG InfoFrame ---------------------------------------------------------------------------------
#define RX_A__MPEG_TYPE			0x2A0
#define RX_A__MPEG_VERS			0x2A1
#define RX_A__MPEG_LENGTH		0x2A2
#define RX_A__MPEG_CHSUM		0x2A3
#define RX_A__MPEG_DBYTE1		0x2A4
#define RX_A__MPEG_DBYTE2		0x2A5
#define RX_A__MPEG_DBYTE3		0x2A6
#define RX_A__MPEG_DBYTE4		0x2A7
#define RX_A__MPEG_DBYTE5		0x2A8
#define RX_A__MPEG_DBYTE6		0x2A9
#define RX_A__MPEG_DBYTE7		0x2AA
#define RX_A__MPEG_DBYTE8		0x2AB
#define RX_A__MPEG_DBYTE9		0x2AC
#define RX_A__MPEG_DBYTE10		0x2AD
#define RX_A__MPEG_DBYTE11		0x2AE
#define RX_A__MPEG_DBYTE12		0x2AF
#define RX_A__MPEG_DBYTE13		0x2B0
#define RX_A__MPEG_DBYTE14		0x2B1
#define RX_A__MPEG_DBYTE15		0x2B2
#define RX_A__MPEG_DBYTE16		0x2B3
#define RX_A__MPEG_DBYTE17		0x2B4
#define RX_A__MPEG_DBYTE18		0x2B5
#define RX_A__MPEG_DBYTE19		0x2B6
#define RX_A__MPEG_DBYTE20		0x2B7
#define RX_A__MPEG_DBYTE21		0x2B8
#define RX_A__MPEG_DBYTE22		0x2B9
#define RX_A__MPEG_DBYTE23		0x2BA
#define RX_A__MPEG_DBYTE24		0x2BB
#define RX_A__MPEG_DBYTE25		0x2BC
#define RX_A__MPEG_DBYTE26		0x2BD
#define RX_A__MPEG_DBYTE27		0x2BE

// MPEG Packet decode address
#define RX_A__MPEG_DEC			0x2BF

// Unrecognized Packet ----------------------------------------------------------------------------
#define RX_A__UNREC_BYTE1		0x2C0
#define RX_A__UNREC_BYTE2		0x2C1
#define RX_A__UNREC_BYTE3		0x2C2
#define RX_A__UNREC_BYTE4		0x2C3
#define RX_A__UNREC_BYTE5		0x2C4
#define RX_A__UNREC_BYTE6		0x2C5
#define RX_A__UNREC_BYTE7		0x2C6
#define RX_A__UNREC_BYTE8		0x2C7
#define RX_A__UNREC_BYTE9		0x2C8
#define RX_A__UNREC_BYTE10		0x2C9
#define RX_A__UNREC_BYTE11		0x2CA
#define RX_A__UNREC_BYTE12		0x2CB
#define RX_A__UNREC_BYTE13		0x2CC
#define RX_A__UNREC_BYTE14		0x2CD
#define RX_A__UNREC_BYTE15		0x2CE
#define RX_A__UNREC_BYTE16		0x2CF
#define RX_A__UNREC_BYTE17		0x2D0
#define RX_A__UNREC_BYTE18		0x2D1
#define RX_A__UNREC_BYTE19		0x2D2
#define RX_A__UNREC_BYTE20		0x2D3
#define RX_A__UNREC_BYTE21		0x2D4
#define RX_A__UNREC_BYTE22		0x2D5
#define RX_A__UNREC_BYTE23		0x2D6
#define RX_A__UNREC_BYTE24		0x2D7
#define RX_A__UNREC_BYTE25		0x2D8
#define RX_A__UNREC_BYTE26		0x2D9
#define RX_A__UNREC_BYTE27		0x2DA
#define RX_A__UNREC_BYTE28		0x2DB
#define RX_A__UNREC_BYTE29		0x2DC
#define RX_A__UNREC_BYTE30		0x2DD
#define RX_A__UNREC_BYTE31		0x2DE

// ACP Packet -------------------------------------------------------------------------------------
#define RX_A__ACP_BYTE1			0x2E0
#define RX_A__ACP_BYTE2			0x2E1
#define RX_A__ACP_BYTE3			0x2E2
#define RX_A__ACP_BYTE4			0x2E3
#define RX_A__ACP_BYTE5			0x2E4
#define RX_A__ACP_BYTE6			0x2E5
#define RX_A__ACP_BYTE7			0x2E6
#define RX_A__ACP_BYTE8			0x2E7
#define RX_A__ACP_BYTE9			0x2E8
#define RX_A__ACP_BYTE10		0x2E9
#define RX_A__ACP_BYTE11		0x2EA
#define RX_A__ACP_BYTE12		0x2EB
#define RX_A__ACP_BYTE13		0x2EC
#define RX_A__ACP_BYTE14		0x2ED
#define RX_A__ACP_BYTE15		0x2EE
#define RX_A__ACP_BYTE16		0x2EF
#define RX_A__ACP_BYTE17		0x2F0
#define RX_A__ACP_BYTE18		0x2F1
#define RX_A__ACP_BYTE19		0x2F2
#define RX_A__ACP_BYTE20		0x2F3
#define RX_A__ACP_BYTE21		0x2F4
#define RX_A__ACP_BYTE22		0x2F5
#define RX_A__ACP_BYTE23		0x2F6
#define RX_A__ACP_BYTE24		0x2F7
#define RX_A__ACP_BYTE25		0x2F8
#define RX_A__ACP_BYTE26		0x2F9
#define RX_A__ACP_BYTE27		0x2FA
#define RX_A__ACP_BYTE28		0x2FB
#define RX_A__ACP_BYTE29		0x2FC
#define RX_A__ACP_BYTE30		0x2FD
#define RX_A__ACP_BYTE31		0x2FE

// ACP Packet decode address
#define RX_A__ACP_DEC	 		0x2FF

//VSI InfoFrame ----------------------------------------------------------------------------------
#define RX_A__VSI_CTRL1			0x930
#define RX_A__VSI_TYPE			0x950
#define RX_A__VSI_VERS			0x951
#define RX_A__VSI_LENGTH		0x952
#define RX_A__VSI_CHSUM			0x953
#define RX_A__VSI_DBYTE1		0x954

#endif  // __SI_SII5293_REGISTERS_H__
