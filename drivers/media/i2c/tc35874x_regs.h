/*
 * tc35874x - Toshiba HDMI to CSI-2 bridge - register names and bit masks
 *
 * Copyright 2015 Cisco Systems, Inc. and/or its affiliates. All rights
 * reserved.
 *
 * This program is free software; you may redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; version 2 of the License.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT. IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

/*
 * References (c = chapter, p = page):
 * REF_01 - Toshiba, TC358743XBG (H2C), Functional Specification, Rev 0.60
 * REF_02 - Toshiba, TC358749XBG (H2C+), Functional Specification, Rev 0.74
 */

/* Bit masks has prefix 'MASK_' and options after '_'. */

#ifndef __TC35874X_REGS_H
#define __TC35874X_REGS_H

#define CHIPID                                0x0000
#define MASK_CHIPID                           0xff00
#define MASK_REVID                            0x00ff

#define SYSCTL                                0x0002
#define MASK_IRRST                            0x0800
#define MASK_CECRST                           0x0400
#define MASK_CTXRST                           0x0200
#define MASK_HDMIRST                          0x0100
#define MASK_SLEEP                            0x0001

#define CONFCTL                               0x0004
#define MASK_PWRISO                           0x8000
#define MASK_ACLKOPT                          0x1000
#define MASK_AUDCHNUM                         0x0c00
#define MASK_AUDCHNUM_8                       0x0000
#define MASK_AUDCHNUM_6                       0x0400
#define MASK_AUDCHNUM_4                       0x0800
#define MASK_AUDCHNUM_2                       0x0c00
#define MASK_AUDCHSEL                         0x0200
#define MASK_I2SDLYOPT                        0x0100
#define MASK_YCBCRFMT                         0x00c0
#define MASK_YCBCRFMT_444                     0x0000
#define MASK_YCBCRFMT_422_12_BIT              0x0040
#define MASK_YCBCRFMT_COLORBAR                0x0080
#define MASK_YCBCRFMT_422_8_BIT               0x00c0
#define MASK_INFRMEN                          0x0020
#define MASK_AUDOUTSEL                        0x0018
#define MASK_AUDOUTSEL_CSI                    0x0000
#define MASK_AUDOUTSEL_I2S                    0x0010
#define MASK_AUDOUTSEL_TDM                    0x0018
#define MASK_AUTOINDEX                        0x0004
#define MASK_ABUFEN                           0x0002
#define MASK_VBUFEN                           0x0001

#define FIFOCTL                               0x0006

#define INTSTATUS                             0x0014
#define MASK_AMUTE_INT                        0x0400
#define MASK_HDMI_INT                         0x0200
#define MASK_CSI_INT                          0x0100
#define MASK_SYS_INT                          0x0020
#define MASK_CEC_EINT                         0x0010
#define MASK_CEC_TINT                         0x0008
#define MASK_CEC_RINT                         0x0004
#define MASK_IR_EINT                          0x0002
#define MASK_IR_DINT                          0x0001

#define INTMASK                               0x0016
#define MASK_AMUTE_MSK                        0x0400
#define MASK_HDMI_MSK                         0x0200
#define MASK_CSI_MSK                          0x0100
#define MASK_SYS_MSK                          0x0020
#define MASK_CEC_EMSK                         0x0010
#define MASK_CEC_TMSK                         0x0008
#define MASK_CEC_RMSK                         0x0004
#define MASK_IR_EMSK                          0x0002
#define MASK_IR_DMSK                          0x0001

#define INTFLAG                               0x0018
#define INTSYSSTATUS                          0x001A

#define PLLCTL0                               0x0020
#define MASK_PLL_PRD                          0xf000
#define SET_PLL_PRD(prd)                      ((((prd) - 1) << 12) &\
						MASK_PLL_PRD)
#define MASK_PLL_FBD                          0x01ff
#define SET_PLL_FBD(fbd)                      (((fbd) - 1) & MASK_PLL_FBD)

#define PLLCTL1                               0x0022
#define MASK_PLL_FRS                          0x0c00
#define SET_PLL_FRS(frs)                      (((frs) << 10) & MASK_PLL_FRS)
#define MASK_PLL_LBWS                         0x0300
#define MASK_LFBREN                           0x0040
#define MASK_BYPCKEN                          0x0020
#define MASK_CKEN                             0x0010
#define MASK_RESETB                           0x0002
#define MASK_PLL_EN                           0x0001

#define CLW_CNTRL                             0x0140
#define MASK_CLW_LANEDISABLE                  0x0001

#define D0W_CNTRL                             0x0144
#define MASK_D0W_LANEDISABLE                  0x0001

#define D1W_CNTRL                             0x0148
#define MASK_D1W_LANEDISABLE                  0x0001

#define D2W_CNTRL                             0x014C
#define MASK_D2W_LANEDISABLE                  0x0001

#define D3W_CNTRL                             0x0150
#define MASK_D3W_LANEDISABLE                  0x0001

#define STARTCNTRL                            0x0204
#define MASK_START                            0x00000001

#define LINEINITCNT                           0x0210
#define LPTXTIMECNT                           0x0214
#define TCLK_HEADERCNT                        0x0218
#define TCLK_TRAILCNT                         0x021C
#define THS_HEADERCNT                         0x0220
#define TWAKEUP                               0x0224
#define TCLK_POSTCNT                          0x0228
#define THS_TRAILCNT                          0x022C
#define HSTXVREGCNT                           0x0230

#define HSTXVREGEN                            0x0234
#define MASK_D3M_HSTXVREGEN                   0x0010
#define MASK_D2M_HSTXVREGEN                   0x0008
#define MASK_D1M_HSTXVREGEN                   0x0004
#define MASK_D0M_HSTXVREGEN                   0x0002
#define MASK_CLM_HSTXVREGEN                   0x0001


#define TXOPTIONCNTRL                         0x0238
#define MASK_CONTCLKMODE                      0x00000001

#define CSI_CONTROL                           0x040C
#define MASK_CSI_MODE                         0x8000
#define MASK_HTXTOEN                          0x0400
#define MASK_TXHSMD                           0x0080
#define MASK_HSCKMD                           0x0020
#define MASK_NOL                              0x0006
#define MASK_NOL_1                            0x0000
#define MASK_NOL_2                            0x0002
#define MASK_NOL_3                            0x0004
#define MASK_NOL_4                            0x0006
#define MASK_EOTDIS                           0x0001

#define CSI_INT                               0x0414
#define MASK_INTHLT                           0x00000008
#define MASK_INTER                            0x00000004

#define CSI_INT_ENA                           0x0418
#define MASK_IENHLT                           0x00000008
#define MASK_IENER                            0x00000004

#define CSI_ERR                               0x044C
#define MASK_INER                             0x00000200
#define MASK_WCER                             0x00000100
#define MASK_QUNK                             0x00000010
#define MASK_TXBRK                            0x00000002

#define CSI_ERR_INTENA                        0x0450
#define CSI_ERR_HALT                          0x0454

#define CSI_CONFW                             0x0500
#define MASK_MODE                             0xe0000000
#define MASK_MODE_SET                         0xa0000000
#define MASK_MODE_CLEAR                       0xc0000000
#define MASK_ADDRESS                          0x1f000000
#define MASK_ADDRESS_CSI_CONTROL              0x03000000
#define MASK_ADDRESS_CSI_INT_ENA              0x06000000
#define MASK_ADDRESS_CSI_ERR_INTENA           0x14000000
#define MASK_ADDRESS_CSI_ERR_HALT             0x15000000
#define MASK_DATA                             0x0000ffff

#define CSI_INT_CLR                           0x050C
#define MASK_ICRER                            0x00000004

#define CSI_START                             0x0518
#define MASK_STRT                             0x00000001

#define CECEN                                 0x0600
#define MASK_CECEN                            0x0001

#define HDMI_INT0                             0x8500
#define MASK_I_KEY                            0x80
#define MASK_I_MISC                           0x02
#define MASK_I_PHYERR                         0x01

#define HDMI_INT1                             0x8501
#define MASK_I_GBD                            0x80
#define MASK_I_HDCP                           0x40
#define MASK_I_ERR                            0x20
#define MASK_I_AUD                            0x10
#define MASK_I_CBIT                           0x08
#define MASK_I_PACKET                         0x04
#define MASK_I_CLK                            0x02
#define MASK_I_SYS                            0x01

#define SYS_INT                               0x8502
#define MASK_I_ACR_CTS                        0x80
#define MASK_I_ACRN                           0x40
#define MASK_I_DVI                            0x20
#define MASK_I_HDMI                           0x10
#define MASK_I_NOPMBDET                       0x08
#define MASK_I_DPMBDET                        0x04
#define MASK_I_TMDS                           0x02
#define MASK_I_DDC                            0x01

#define CLK_INT                               0x8503
#define MASK_I_OUT_H_CHG                      0x40
#define MASK_I_IN_DE_CHG                      0x20
#define MASK_I_IN_HV_CHG                      0x10
#define MASK_I_DC_CHG                         0x08
#define MASK_I_PXCLK_CHG                      0x04
#define MASK_I_PHYCLK_CHG                     0x02
#define MASK_I_TMDSCLK_CHG                    0x01

#define CBIT_INT                              0x8505
#define MASK_I_AF_LOCK                        0x80
#define MASK_I_AF_UNLOCK                      0x40
#define MASK_I_CBIT_FS                        0x02

#define AUDIO_INT                             0x8506

#define ERR_INT                               0x8507
#define MASK_I_EESS_ERR                       0x80

#define HDCP_INT                              0x8508
#define MASK_I_AVM_SET                        0x80
#define MASK_I_AVM_CLR                        0x40
#define MASK_I_LINKERR                        0x20
#define MASK_I_SHA_END                        0x10
#define MASK_I_R0_END                         0x08
#define MASK_I_KM_END                         0x04
#define MASK_I_AKSV_END                       0x02
#define MASK_I_AN_END                         0x01

#define MISC_INT                              0x850B
#define MASK_I_AS_LAYOUT                      0x10
#define MASK_I_NO_SPD                         0x08
#define MASK_I_NO_VS                          0x03
#define MASK_I_SYNC_CHG                       0x02
#define MASK_I_AUDIO_MUTE                     0x01

#define KEY_INT                               0x850F

#define SYS_INTM                              0x8512
#define MASK_M_ACR_CTS                        0x80
#define MASK_M_ACR_N                          0x40
#define MASK_M_DVI_DET                        0x20
#define MASK_M_HDMI_DET                       0x10
#define MASK_M_NOPMBDET                       0x08
#define MASK_M_BPMBDET                        0x04
#define MASK_M_TMDS                           0x02
#define MASK_M_DDC                            0x01

#define CLK_INTM                              0x8513
#define MASK_M_OUT_H_CHG                      0x40
#define MASK_M_IN_DE_CHG                      0x20
#define MASK_M_IN_HV_CHG                      0x10
#define MASK_M_DC_CHG                         0x08
#define MASK_M_PXCLK_CHG                      0x04
#define MASK_M_PHYCLK_CHG                     0x02
#define MASK_M_TMDS_CHG                       0x01

#define PACKET_INTM                           0x8514

#define CBIT_INTM                             0x8515
#define MASK_M_AF_LOCK                        0x80
#define MASK_M_AF_UNLOCK                      0x40
#define MASK_M_CBIT_FS                        0x02

#define AUDIO_INTM                            0x8516
#define MASK_M_BUFINIT_END                    0x01

#define ERR_INTM                              0x8517
#define MASK_M_EESS_ERR                       0x80

#define HDCP_INTM                             0x8518
#define MASK_M_AVM_SET                        0x80
#define MASK_M_AVM_CLR                        0x40
#define MASK_M_LINKERR                        0x20
#define MASK_M_SHA_END                        0x10
#define MASK_M_R0_END                         0x08
#define MASK_M_KM_END                         0x04
#define MASK_M_AKSV_END                       0x02
#define MASK_M_AN_END                         0x01

#define MISC_INTM                             0x851B
#define MASK_M_AS_LAYOUT                      0x10
#define MASK_M_NO_SPD                         0x08
#define MASK_M_NO_VS                          0x03
#define MASK_M_SYNC_CHG                       0x02
#define MASK_M_AUDIO_MUTE                     0x01

#define KEY_INTM                              0x851F

#define SYS_STATUS                            0x8520
#define MASK_S_SYNC                           0x80
#define MASK_S_AVMUTE                         0x40
#define MASK_S_HDCP                           0x20
#define MASK_S_HDMI                           0x10
#define MASK_S_PHY_SCDT                       0x08
#define MASK_S_PHY_PLL                        0x04
#define MASK_S_TMDS                           0x02
#define MASK_S_DDC5V                          0x01

#define CSI_STATUS                            0x0410
#define MASK_S_WSYNC                          0x0400
#define MASK_S_TXACT                          0x0200
#define MASK_S_RXACT                          0x0100
#define MASK_S_HLT                            0x0001

#define VI_STATUS1                            0x8522
#define MASK_S_V_GBD                          0x08
#define MASK_S_DEEPCOLOR                      0x0c
#define MASK_S_V_422                          0x02
#define MASK_S_V_INTERLACE                    0x01

#define AU_STATUS0                            0x8523
#define MASK_S_A_SAMPLE                       0x01

#define VI_STATUS3                            0x8528
#define MASK_S_V_COLOR                        0x1e
#define MASK_LIMITED                          0x01

#define PHY_CTL0                              0x8531
#define MASK_PHY_SYSCLK_IND                   0x02
#define MASK_PHY_CTL                          0x01


#define PHY_CTL1                              0x8532 /* Not in REF_01 */
#define MASK_PHY_AUTO_RST1                    0xf0
#define MASK_PHY_AUTO_RST1_OFF                0x00
#define SET_PHY_AUTO_RST1_US(us)             ((((us) / 200) << 4) & \
						MASK_PHY_AUTO_RST1)
#define MASK_FREQ_RANGE_MODE                  0x0f
#define SET_FREQ_RANGE_MODE_CYCLES(cycles)   (((cycles) - 1) & \
						MASK_FREQ_RANGE_MODE)

#define PHY_CTL2                              0x8533 /* Not in REF_01 */
#define MASK_PHY_AUTO_RST4                    0x04
#define MASK_PHY_AUTO_RST3                    0x02
#define MASK_PHY_AUTO_RST2                    0x01
#define MASK_PHY_AUTO_RSTn                    (MASK_PHY_AUTO_RST4 | \
						MASK_PHY_AUTO_RST3 | \
						MASK_PHY_AUTO_RST2)

#define PHY_EN                                0x8534
#define MASK_ENABLE_PHY                       0x01

#define PHY_RST                               0x8535
#define MASK_RESET_CTRL                       0x01   /* Reset active low */

#define PHY_BIAS                              0x8536 /* Not in REF_01 */

#define PHY_CSQ                               0x853F /* Not in REF_01 */
#define MASK_CSQ_CNT                          0x0f
#define SET_CSQ_CNT_LEVEL(n)                 (n & MASK_CSQ_CNT)

#define SYS_FREQ0                             0x8540
#define SYS_FREQ1                             0x8541

#define SYS_CLK                               0x8542 /* Not in REF_01 */
#define MASK_CLK_DIFF                         0x0C
#define MASK_CLK_DIV                          0x03

#define DDC_CTL                               0x8543
#define MASK_DDC_ACK_POL                      0x08
#define MASK_DDC_ACTION                       0x04
#define MASK_DDC5V_MODE                       0x03
#define MASK_DDC5V_MODE_0MS                   0x00
#define MASK_DDC5V_MODE_50MS                  0x01
#define MASK_DDC5V_MODE_100MS                 0x02
#define MASK_DDC5V_MODE_200MS                 0x03

#define HPD_CTL                               0x8544
#define MASK_HPD_CTL0                         0x10
#define MASK_HPD_OUT0                         0x01

#define ANA_CTL                               0x8545
#define MASK_APPL_PCSX                        0x30
#define MASK_APPL_PCSX_HIZ                    0x00
#define MASK_APPL_PCSX_L_FIX                  0x10
#define MASK_APPL_PCSX_H_FIX                  0x20
#define MASK_APPL_PCSX_NORMAL                 0x30
#define MASK_ANALOG_ON                        0x01

#define AVM_CTL                               0x8546

#define INIT_END                              0x854A
#define MASK_INIT_END                         0x01

#define HDMI_DET                              0x8552 /* Not in REF_01 */
#define MASK_HDMI_DET_MOD1                    0x80
#define MASK_HDMI_DET_MOD0                    0x40
#define MASK_HDMI_DET_V                       0x30
#define MASK_HDMI_DET_V_SYNC                  0x00
#define MASK_HDMI_DET_V_ASYNC_25MS            0x10
#define MASK_HDMI_DET_V_ASYNC_50MS            0x20
#define MASK_HDMI_DET_V_ASYNC_100MS           0x30
#define MASK_HDMI_DET_NUM                     0x0f

#define HDCP_MODE                             0x8560
#define MASK_MODE_RST_TN                      0x20
#define MASK_LINE_REKEY                       0x10
#define MASK_AUTO_CLR                         0x04
#define MASK_MANUAL_AUTHENTICATION            0x02 /* Not in REF_01 */

#define HDCP_REG1                             0x8563 /* Not in REF_01 */
#define MASK_AUTH_UNAUTH_SEL                  0x70
#define MASK_AUTH_UNAUTH_SEL_12_FRAMES        0x70
#define MASK_AUTH_UNAUTH_SEL_8_FRAMES         0x60
#define MASK_AUTH_UNAUTH_SEL_4_FRAMES         0x50
#define MASK_AUTH_UNAUTH_SEL_2_FRAMES         0x40
#define MASK_AUTH_UNAUTH_SEL_64_FRAMES        0x30
#define MASK_AUTH_UNAUTH_SEL_32_FRAMES        0x20
#define MASK_AUTH_UNAUTH_SEL_16_FRAMES        0x10
#define MASK_AUTH_UNAUTH_SEL_ONCE             0x00
#define MASK_AUTH_UNAUTH                      0x01
#define MASK_AUTH_UNAUTH_AUTO                 0x01

#define HDCP_REG2                             0x8564 /* Not in REF_01 */
#define MASK_AUTO_P3_RESET                    0x0F
#define SET_AUTO_P3_RESET_FRAMES(n)          (n & MASK_AUTO_P3_RESET)
#define MASK_AUTO_P3_RESET_OFF                0x00

#define VI_MODE                               0x8570
#define MASK_RGB_DVI                          0x08 /* Not in REF_01 */

#define VOUT_SET2                             0x8573
#define MASK_SEL422                           0x80
#define MASK_VOUT_422FIL_100                  0x40
#define MASK_VOUTCOLORMODE                    0x03
#define MASK_VOUTCOLORMODE_THROUGH            0x00
#define MASK_VOUTCOLORMODE_AUTO               0x01
#define MASK_VOUTCOLORMODE_MANUAL             0x03

#define VOUT_SET3                             0x8574
#define MASK_VOUT_EXTCNT                      0x08

#define VI_REP                                0x8576
#define MASK_VOUT_COLOR_SEL                   0xe0
#define MASK_VOUT_COLOR_RGB_FULL              0x00
#define MASK_VOUT_COLOR_RGB_LIMITED           0x20
#define MASK_VOUT_COLOR_601_YCBCR_FULL        0x40
#define MASK_VOUT_COLOR_601_YCBCR_LIMITED     0x60
#define MASK_VOUT_COLOR_709_YCBCR_FULL        0x80
#define MASK_VOUT_COLOR_709_YCBCR_LIMITED     0xa0
#define MASK_VOUT_COLOR_FULL_TO_LIMITED       0xc0
#define MASK_VOUT_COLOR_LIMITED_TO_FULL       0xe0
#define MASK_IN_REP_HEN                       0x10
#define MASK_IN_REP                           0x0f

#define VI_MUTE                               0x857F
#define MASK_AUTO_MUTE                        0xc0
#define MASK_VI_MUTE                          0x10

#define DE_WIDTH_H_LO                         0x8582 /* Not in REF_01 */
#define DE_WIDTH_H_HI                         0x8583 /* Not in REF_01 */
#define DE_WIDTH_V_LO                         0x8588 /* Not in REF_01 */
#define DE_WIDTH_V_HI                         0x8589 /* Not in REF_01 */
#define H_SIZE_LO                             0x858A /* Not in REF_01 */
#define H_SIZE_HI                             0x858B /* Not in REF_01 */
#define V_SIZE_LO                             0x858C /* Not in REF_01 */
#define V_SIZE_HI                             0x858D /* Not in REF_01 */
#define FV_CNT_LO                             0x85A1 /* Not in REF_01 */
#define FV_CNT_HI                             0x85A2 /* Not in REF_01 */

#define FH_MIN0                               0x85AA /* Not in REF_01 */
#define FH_MIN1                               0x85AB /* Not in REF_01 */
#define FH_MAX0                               0x85AC /* Not in REF_01 */
#define FH_MAX1                               0x85AD /* Not in REF_01 */

#define HV_RST                                0x85AF /* Not in REF_01 */
#define MASK_H_PI_RST                         0x20
#define MASK_V_PI_RST                         0x10

#define EDID_MODE                             0x85C7
#define MASK_EDID_SPEED                       0x40
#define MASK_EDID_MODE                        0x03
#define MASK_EDID_MODE_DISABLE                0x00
#define MASK_EDID_MODE_DDC2B                  0x01
#define MASK_EDID_MODE_E_DDC                  0x02

#define EDID_LEN1                             0x85CA
#define EDID_LEN2                             0x85CB

#define HDCP_REG3                             0x85D1 /* Not in REF_01 */
#define KEY_RD_CMD                            0x01

#define FORCE_MUTE                            0x8600
#define MASK_FORCE_AMUTE                      0x10
#define MASK_FORCE_DMUTE                      0x01

#define CMD_AUD                               0x8601
#define MASK_CMD_BUFINIT                      0x04
#define MASK_CMD_LOCKDET                      0x02
#define MASK_CMD_MUTE                         0x01

#define AUTO_CMD0                             0x8602
#define MASK_AUTO_MUTE7                       0x80
#define MASK_AUTO_MUTE6                       0x40
#define MASK_AUTO_MUTE5                       0x20
#define MASK_AUTO_MUTE4                       0x10
#define MASK_AUTO_MUTE3                       0x08
#define MASK_AUTO_MUTE2                       0x04
#define MASK_AUTO_MUTE1                       0x02
#define MASK_AUTO_MUTE0                       0x01

#define AUTO_CMD1                             0x8603
#define MASK_AUTO_MUTE10                      0x04
#define MASK_AUTO_MUTE9                       0x02
#define MASK_AUTO_MUTE8                       0x01

#define AUTO_CMD2                             0x8604
#define MASK_AUTO_PLAY3                       0x08
#define MASK_AUTO_PLAY2                       0x04

#define BUFINIT_START                         0x8606
#define SET_BUFINIT_START_MS(milliseconds)   ((milliseconds) / 100)

#define FS_MUTE                               0x8607
#define MASK_FS_ELSE_MUTE                     0x80
#define MASK_FS22_MUTE                        0x40
#define MASK_FS24_MUTE                        0x20
#define MASK_FS88_MUTE                        0x10
#define MASK_FS96_MUTE                        0x08
#define MASK_FS176_MUTE                       0x04
#define MASK_FS192_MUTE                       0x02
#define MASK_FS_NO_MUTE                       0x01

#define FS_IMODE                              0x8620
#define MASK_NLPCM_HMODE                      0x40
#define MASK_NLPCM_SMODE                      0x20
#define MASK_NLPCM_IMODE                      0x10
#define MASK_FS_HMODE                         0x08
#define MASK_FS_AMODE                         0x04
#define MASK_FS_SMODE                         0x02
#define MASK_FS_IMODE                         0x01

#define FS_SET                                0x8621
#define MASK_FS                               0x0f

#define LOCKDET_REF0                          0x8630
#define LOCKDET_REF1                          0x8631
#define LOCKDET_REF2                          0x8632

#define ACR_MODE                              0x8640
#define MASK_ACR_LOAD                         0x10
#define MASK_N_MODE                           0x04
#define MASK_CTS_MODE                         0x01

#define ACR_MDF0                              0x8641
#define MASK_ACR_L2MDF                        0x70
#define MASK_ACR_L2MDF_0_PPM                  0x00
#define MASK_ACR_L2MDF_61_PPM                 0x10
#define MASK_ACR_L2MDF_122_PPM                0x20
#define MASK_ACR_L2MDF_244_PPM                0x30
#define MASK_ACR_L2MDF_488_PPM                0x40
#define MASK_ACR_L2MDF_976_PPM                0x50
#define MASK_ACR_L2MDF_1976_PPM               0x60
#define MASK_ACR_L2MDF_3906_PPM               0x70
#define MASK_ACR_L1MDF                        0x07
#define MASK_ACR_L1MDF_0_PPM                  0x00
#define MASK_ACR_L1MDF_61_PPM                 0x01
#define MASK_ACR_L1MDF_122_PPM                0x02
#define MASK_ACR_L1MDF_244_PPM                0x03
#define MASK_ACR_L1MDF_488_PPM                0x04
#define MASK_ACR_L1MDF_976_PPM                0x05
#define MASK_ACR_L1MDF_1976_PPM               0x06
#define MASK_ACR_L1MDF_3906_PPM               0x07

#define ACR_MDF1                              0x8642
#define MASK_ACR_L3MDF                        0x07
#define MASK_ACR_L3MDF_0_PPM                  0x00
#define MASK_ACR_L3MDF_61_PPM                 0x01
#define MASK_ACR_L3MDF_122_PPM                0x02
#define MASK_ACR_L3MDF_244_PPM                0x03
#define MASK_ACR_L3MDF_488_PPM                0x04
#define MASK_ACR_L3MDF_976_PPM                0x05
#define MASK_ACR_L3MDF_1976_PPM               0x06
#define MASK_ACR_L3MDF_3906_PPM               0x07

#define SDO_MODE1                             0x8652
#define MASK_SDO_BIT_LENG                     0x70
#define MASK_SDO_FMT                          0x03
#define MASK_SDO_FMT_RIGHT                    0x00
#define MASK_SDO_FMT_LEFT                     0x01
#define MASK_SDO_FMT_I2S                      0x02

#define DIV_MODE                              0x8665 /* Not in REF_01 */
#define MASK_DIV_DLY                          0xf0
#define SET_DIV_DLY_MS(milliseconds)         ((((milliseconds) / 100) << 4) & \
						MASK_DIV_DLY)
#define MASK_DIV_MODE                         0x01

#define NCO_F0_MOD                            0x8670
#define MASK_NCO_F0_MOD                       0x03
#define MASK_NCO_F0_MOD_42MHZ                 0x00
#define MASK_NCO_F0_MOD_27MHZ                 0x01

#define PK_INT_MODE                           0x8709
#define MASK_ISRC2_INT_MODE                   0x80
#define MASK_ISRC_INT_MODE                    0x40
#define MASK_ACP_INT_MODE                     0x20
#define MASK_VS_INT_MODE                      0x10
#define MASK_SPD_INT_MODE                     0x08
#define MASK_MS_INT_MODE                      0x04
#define MASK_AUD_INT_MODE                     0x02
#define MASK_AVI_INT_MODE                     0x01

#define NO_PKT_LIMIT                          0x870B
#define MASK_NO_ACP_LIMIT                     0xf0
#define SET_NO_ACP_LIMIT_MS(milliseconds)    ((((milliseconds) / 80) << 4) & \
						MASK_NO_ACP_LIMIT)
#define MASK_NO_AVI_LIMIT                     0x0f
#define SET_NO_AVI_LIMIT_MS(milliseconds)    (((milliseconds) / 80) & \
						MASK_NO_AVI_LIMIT)

#define NO_PKT_CLR                            0x870C
#define MASK_NO_VS_CLR                        0x40
#define MASK_NO_SPD_CLR                       0x20
#define MASK_NO_ACP_CLR                       0x10
#define MASK_NO_AVI_CLR1                      0x02
#define MASK_NO_AVI_CLR0                      0x01

#define ERR_PK_LIMIT                          0x870D
#define NO_PKT_LIMIT2                         0x870E
#define PK_AVI_0HEAD                          0x8710
#define PK_AVI_1HEAD                          0x8711
#define PK_AVI_2HEAD                          0x8712
#define PK_AVI_0BYTE                          0x8713
#define PK_AVI_1BYTE                          0x8714
#define PK_AVI_2BYTE                          0x8715
#define PK_AVI_3BYTE                          0x8716
#define PK_AVI_4BYTE                          0x8717
#define PK_AVI_5BYTE                          0x8718
#define PK_AVI_6BYTE                          0x8719
#define PK_AVI_7BYTE                          0x871A
#define PK_AVI_8BYTE                          0x871B
#define PK_AVI_9BYTE                          0x871C
#define PK_AVI_10BYTE                         0x871D
#define PK_AVI_11BYTE                         0x871E
#define PK_AVI_12BYTE                         0x871F
#define PK_AVI_13BYTE                         0x8720
#define PK_AVI_14BYTE                         0x8721
#define PK_AVI_15BYTE                         0x8722
#define PK_AVI_16BYTE                         0x8723

#define BKSV                                  0x8800

#define BCAPS                                 0x8840
#define MASK_HDMI_RSVD                        0x80
#define MASK_REPEATER                         0x40
#define MASK_READY                            0x20
#define MASK_FASTI2C                          0x10
#define MASK_1_1_FEA                          0x02
#define MASK_FAST_REAU                        0x01

#define BSTATUS1                              0x8842
#define MASK_MAX_EXCED                        0x08

#define EDID_RAM                              0x8C00
#define NO_GDB_LIMIT                          0x9007

#endif