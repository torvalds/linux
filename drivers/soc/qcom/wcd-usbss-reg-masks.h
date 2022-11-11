/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef WCD_USBSS_REG_MASKS_H
#define WCD_USBSS_REG_MASKS_H
#include <linux/regmap.h>
#include <linux/device.h>
#include "wcd-usbss-registers.h"

/* Use in conjunction with wcd_usbss-reg-shifts.c for field values. */
/* field_value = (register_value & field_mask) >> field_shift */

#define FIELD_MASK(register_name, field_name) \
WCD_USBSS_##register_name##_##field_name##_MASK

/* WCD_USBSS_PAGE0_PAGE Fields: */
#define WCD_USBSS_PAGE0_PAGE_PAGE_REG_MASK                               0xff


/* WCD_USBSS_PMP_EN Fields: */
#define WCD_USBSS_PMP_EN_SPARE_BIT_7_MASK                                0x80
#define WCD_USBSS_PMP_EN_PMP_OUT_APD_MASK                                0x40
#define WCD_USBSS_PMP_EN_IBIAS_HALF_MASK                                 0x20
#define WCD_USBSS_PMP_EN_PFM_MODE_MASK                                   0x10
#define WCD_USBSS_PMP_EN_VREFGEN_MASK                                    0x08
#define WCD_USBSS_PMP_EN_REGULATE_OUT_MASK                               0x04
#define WCD_USBSS_PMP_EN_CLK_PMP_MASK                                    0x02
#define WCD_USBSS_PMP_EN_LOC_CTRL_EN_MASK                                0x01

/* WCD_USBSS_PMP_OUT1 Fields: */
#define WCD_USBSS_PMP_OUT1_SPARE_BITS_7_5_MASK                           0xe0
#define WCD_USBSS_PMP_OUT1_LOC_CTRL_TRIM_MASK                            0x10
#define WCD_USBSS_PMP_OUT1_PMP_OUT_PCT_MASK                              0x0f

/* WCD_USBSS_PMP_OUT2 Fields: */
#define WCD_USBSS_PMP_OUT2_SPARE_BITS_7_6_MASK                           0xc0
#define WCD_USBSS_PMP_OUT2_VDWN_AUD_MASK                                 0x20
#define WCD_USBSS_PMP_OUT2_VUP_PFM_MASK                                  0x10
#define WCD_USBSS_PMP_OUT2_PMP_OUT_HYST_MASK                             0x0c
#define WCD_USBSS_PMP_OUT2_PMP_OUT_MIN_MAX_MASK                          0x03

/* WCD_USBSS_PMP_CLK Fields: */
#define WCD_USBSS_PMP_CLK_SPARE_BITS_7_6_MASK                            0xc0
#define WCD_USBSS_PMP_CLK_DLY_NOV_MASK                                   0x30
#define WCD_USBSS_PMP_CLK_LOC_CTRL_CLK_DIV_MASK                          0x08
#define WCD_USBSS_PMP_CLK_CLK_DIV_MASK                                   0x04
#define WCD_USBSS_PMP_CLK_CLK_DIV_VAL_MASK                               0x03

/* WCD_USBSS_PMP_MISC1 Fields: */
#define WCD_USBSS_PMP_MISC1_SPARE_BITS_7_6_MASK                          0xc0
#define WCD_USBSS_PMP_MISC1_LOC_CTRL_ALL_MASK                            0x20
#define WCD_USBSS_PMP_MISC1_LOC_CTRL_VREF_MASK                           0x10
#define WCD_USBSS_PMP_MISC1_VREF_SEL_MASK                                0x0c
#define WCD_USBSS_PMP_MISC1_VREF2ATEST_MASK                              0x02
#define WCD_USBSS_PMP_MISC1_CLEAR_PMP_RDY_MASK                           0x01

/* WCD_USBSS_PMP_MISC2 Fields: */
#define WCD_USBSS_PMP_MISC2_SPARE_BITS_7_3_MASK                          0xf8
#define WCD_USBSS_PMP_MISC2_PFM_MODE_STATUS_MASK                         0x04
#define WCD_USBSS_PMP_MISC2_PMP_ENABLE_STATUS_MASK                       0x02
#define WCD_USBSS_PMP_MISC2_PMP_RDY_STATUS_MASK                          0x01


/* WCD_USBSS_RCO_EN Fields: */
#define WCD_USBSS_RCO_EN_SPARE_BITS_7_3_MASK                             0xf8
#define WCD_USBSS_RCO_EN_OTA_OPTION_MASK                                 0x04
#define WCD_USBSS_RCO_EN_ENABLE_RCO_MASK                                 0x02
#define WCD_USBSS_RCO_EN_LOC_CTRL_EN_MASK                                0x01

/* WCD_USBSS_RCO_RST Fields: */
#define WCD_USBSS_RCO_RST_SPARE_BITS_7_2_MASK                            0xfc
#define WCD_USBSS_RCO_RST_LOC_CTRL_RST_MASK                              0x02
#define WCD_USBSS_RCO_RST_RESET_MASK                                     0x01

/* WCD_USBSS_RCO_CLK Fields: */
#define WCD_USBSS_RCO_CLK_SPARE_BITS_7_3_MASK                            0xf8
#define WCD_USBSS_RCO_CLK_RCO_FREQ_C_ICBIAS_MASK                         0x07

/* WCD_USBSS_RCO_IBIAS Fields: */
#define WCD_USBSS_RCO_IBIAS_SPARE_BITS_7_2_MASK                          0xfc
#define WCD_USBSS_RCO_IBIAS_IB2_MASK                                     0x02
#define WCD_USBSS_RCO_IBIAS_IB1_MASK                                     0x01

/* WCD_USBSS_RCO_MISC1 Fields: */
#define WCD_USBSS_RCO_MISC1_SPARE_BITS_7_3_MASK                          0xf8
#define WCD_USBSS_RCO_MISC1_ATEST_CLK_MASK                               0x04
#define WCD_USBSS_RCO_MISC1_ATEST_COMP_MASK                              0x02
#define WCD_USBSS_RCO_MISC1_ATEST_VIR_MASK                               0x01

/* WCD_USBSS_RCO_MISC2 Fields: */
#define WCD_USBSS_RCO_MISC2_SPARE_BITS_7_2_MASK                          0xfc
#define WCD_USBSS_RCO_MISC2_COMP_OUT_MASK                                0x02
#define WCD_USBSS_RCO_MISC2_RESET_INT_MASK                               0x01


/* WCD_USBSS_DP_EN Fields: */
#define WCD_USBSS_DP_EN_EN_PATH_MASK                                     0x80
#define WCD_USBSS_DP_EN_EN_BIAS_MASK                                     0x40
#define WCD_USBSS_DP_EN_EN_PSURGE_MASK                                   0x20
#define WCD_USBSS_DP_EN_EN_NSURGE_MASK                                   0x10
#define WCD_USBSS_DP_EN_P_THRESH_SEL_MASK                                0x0e
#define WCD_USBSS_DP_EN_RED_POFF_THRESH_2X_MASK                          0x01

/* WCD_USBSS_DP_BIAS Fields: */
#define WCD_USBSS_DP_BIAS_PCOMP_BIAS_SEL_MASK                            0xc0
#define WCD_USBSS_DP_BIAS_NCOMP_BIAS_SEL_MASK                            0x30
#define WCD_USBSS_DP_BIAS_PCOMP_DYN_BST_EN_MASK                          0x08
#define WCD_USBSS_DP_BIAS_NCOMP_DYN_BST_EN_MASK                          0x04
#define WCD_USBSS_DP_BIAS_EN_CMP_GAIN_RED_MASK                           0x02
#define WCD_USBSS_DP_BIAS_ASSERT_PLDN_ON_PWRDN_MASK                      0x01

/* WCD_USBSS_DP_DN_MISC1 Fields: */
#define WCD_USBSS_DP_DN_MISC1_EN_NS_AUD_THRESH_M2P2_DP_MASK              0x80
#define WCD_USBSS_DP_DN_MISC1_EN_NS_AUD_THRESH_M2P2_DN_MASK              0x40
#define WCD_USBSS_DP_DN_MISC1_DP_NCOMP_2X_DYN_BST_EN_MASK                0x20
#define WCD_USBSS_DP_DN_MISC1_DP_PCOMP_2X_DYN_BST_OFF_EN_MASK            0x10
#define WCD_USBSS_DP_DN_MISC1_DP_PCOMP_2X_DYN_BST_ON_EN_MASK             0x08
#define WCD_USBSS_DP_DN_MISC1_DN_NCOMP_2X_DYN_BST_EN_MASK                0x04
#define WCD_USBSS_DP_DN_MISC1_DN_PCOMP_2X_DYN_BST_OFF_EN_MASK            0x02
#define WCD_USBSS_DP_DN_MISC1_DN_PCOMP_2X_DYN_BST_ON_EN_MASK             0x01

/* WCD_USBSS_DN_EN Fields: */
#define WCD_USBSS_DN_EN_EN_PATH_MASK                                     0x80
#define WCD_USBSS_DN_EN_EN_BIAS_MASK                                     0x40
#define WCD_USBSS_DN_EN_EN_PSURGE_MASK                                   0x20
#define WCD_USBSS_DN_EN_EN_NSURGE_MASK                                   0x10
#define WCD_USBSS_DN_EN_P_THRESH_SEL_MASK                                0x0e
#define WCD_USBSS_DN_EN_RED_POFF_THRESH_2X_MASK                          0x01

/* WCD_USBSS_DN_BIAS Fields: */
#define WCD_USBSS_DN_BIAS_PCOMP_BIAS_SEL_MASK                            0xc0
#define WCD_USBSS_DN_BIAS_NCOMP_BIAS_SEL_MASK                            0x30
#define WCD_USBSS_DN_BIAS_PCOMP_DYN_BST_EN_MASK                          0x08
#define WCD_USBSS_DN_BIAS_NCOMP_DYN_BST_EN_MASK                          0x04
#define WCD_USBSS_DN_BIAS_EN_CMP_GAIN_RED_MASK                           0x02
#define WCD_USBSS_DN_BIAS_ASSERT_PLDN_ON_PWRDN_MASK                      0x01

/* WCD_USBSS_DP_DN_MISC2 Fields: */
#define WCD_USBSS_DP_DN_MISC2_RED_POFF_THRESH_3X_DP_MASK                 0x80
#define WCD_USBSS_DP_DN_MISC2_RED_POFF_THRESH_3X_DN_MASK                 0x40
#define WCD_USBSS_DP_DN_MISC2_INC_PON_THRESH_AUDMD_DP_MASK               0x20
#define WCD_USBSS_DP_DN_MISC2_INC_PON_THRESH_AUDMD_DN_MASK               0x10
#define WCD_USBSS_DP_DN_MISC2_EN_EXTRA_NCLAMP_MASK                       0x0c
#define WCD_USBSS_DP_DN_MISC2_EN_NCLAMP_ALWAYS_MASK                      0x02
#define WCD_USBSS_DP_DN_MISC2_OVERRIDE_EN_NCLAMP_WHEN_DPDN_OVP_OFF_MASK  0x01

/* WCD_USBSS_MG1_EN Fields: */
#define WCD_USBSS_MG1_EN_EN_PATH_MASK                                    0x80
#define WCD_USBSS_MG1_EN_EN_BIAS_MASK                                    0x40
#define WCD_USBSS_MG1_EN_EN_PSURGE_MASK                                  0x20
#define WCD_USBSS_MG1_EN_EN_NSURGE_MASK                                  0x10
#define WCD_USBSS_MG1_EN_P_THRESH_SEL_MASK                               0x0c
#define WCD_USBSS_MG1_EN_CT_SNS_EN_MASK                                  0x02
#define WCD_USBSS_MG1_EN_RED_POFF_THRESH_2X_MASK                         0x01

/* WCD_USBSS_MG1_BIAS Fields: */
#define WCD_USBSS_MG1_BIAS_PCOMP_BIAS_SEL_MASK                           0xc0
#define WCD_USBSS_MG1_BIAS_NCOMP_BIAS_SEL_MASK                           0x30
#define WCD_USBSS_MG1_BIAS_PCOMP_DYN_BST_EN_MASK                         0x08
#define WCD_USBSS_MG1_BIAS_NCOMP_DYN_BST_EN_MASK                         0x04
#define WCD_USBSS_MG1_BIAS_EN_CMP_GAIN_RED_MASK                          0x02
#define WCD_USBSS_MG1_BIAS_ASSERT_PLDN_ON_PWRDN_MASK                     0x01

/* WCD_USBSS_MG1_CTSNS_CTL Fields: */
#define WCD_USBSS_MG1_CTSNS_CTL_CT_SNS_HP_EN_MASK                        0x80
#define WCD_USBSS_MG1_CTSNS_CTL_OVERRIDE_CTSNS_DYN_DISABLE_MASK          0x40
#define WCD_USBSS_MG1_CTSNS_CTL_CT_SNS_LP_EN_MASK                        0x20
#define WCD_USBSS_MG1_CTSNS_CTL_GND_PIN_VMODE_THRESH_MASK                0x18
#define WCD_USBSS_MG1_CTSNS_CTL_D_REG_RST_MASK                           0x04
#define WCD_USBSS_MG1_CTSNS_CTL_ITHRESH_BST_EN_MASK                      0x03

/* WCD_USBSS_MG1_MISC Fields: */
#define WCD_USBSS_MG1_MISC_NCOMP_2X_DYN_BST_EN_MASK                      0x80
#define WCD_USBSS_MG1_MISC_PCOMP_2X_DYN_BST_OFF_EN_MASK                  0x40
#define WCD_USBSS_MG1_MISC_PCOMP_2X_DYN_BST_ON_EN_MASK                   0x20
#define WCD_USBSS_MG1_MISC_RED_POFF_THRESH_3X_MASK                       0x10
#define WCD_USBSS_MG1_MISC_ASSERT_PLDN_ON_PWRDN_GSBU1_MASK               0x08
#define WCD_USBSS_MG1_MISC_INC_NEG_SURGE_THRESH_MG1_MASK                 0x04
#define WCD_USBSS_MG1_MISC_MG1_GNDPIN_EN_OVERRIDE_MASK                   0x02
#define WCD_USBSS_MG1_MISC_MG1_GNDPIN_EN_MASK                            0x01

/* WCD_USBSS_MG2_EN Fields: */
#define WCD_USBSS_MG2_EN_EN_PATH_MASK                                    0x80
#define WCD_USBSS_MG2_EN_EN_BIAS_MASK                                    0x40
#define WCD_USBSS_MG2_EN_EN_PSURGE_MASK                                  0x20
#define WCD_USBSS_MG2_EN_EN_NSURGE_MASK                                  0x10
#define WCD_USBSS_MG2_EN_P_THRESH_SEL_MASK                               0x0c
#define WCD_USBSS_MG2_EN_CT_SNS_EN_MASK                                  0x02
#define WCD_USBSS_MG2_EN_RED_POFF_THRESH_2X_MASK                         0x01

/* WCD_USBSS_MG2_BIAS Fields: */
#define WCD_USBSS_MG2_BIAS_PCOMP_BIAS_SEL_MASK                           0xc0
#define WCD_USBSS_MG2_BIAS_NCOMP_BIAS_SEL_MASK                           0x30
#define WCD_USBSS_MG2_BIAS_PCOMP_DYN_BST_EN_MASK                         0x08
#define WCD_USBSS_MG2_BIAS_NCOMP_DYN_BST_EN_MASK                         0x04
#define WCD_USBSS_MG2_BIAS_EN_CMP_GAIN_RED_MASK                          0x02
#define WCD_USBSS_MG2_BIAS_ASSERT_PLDN_ON_PWRDN_MASK                     0x01

/* WCD_USBSS_MG2_CTSNS_CTL Fields: */
#define WCD_USBSS_MG2_CTSNS_CTL_CT_SNS_HP_EN_MASK                        0x80
#define WCD_USBSS_MG2_CTSNS_CTL_OVERRIDE_CTSNS_DYN_DISABLE_MASK          0x40
#define WCD_USBSS_MG2_CTSNS_CTL_CT_SNS_LP_EN_MASK                        0x20
#define WCD_USBSS_MG2_CTSNS_CTL_GND_PIN_VMODE_THRESH_MASK                0x18
#define WCD_USBSS_MG2_CTSNS_CTL_D_REG_RST_MASK                           0x04
#define WCD_USBSS_MG2_CTSNS_CTL_ITHRESH_BST_EN_MASK                      0x03

/* WCD_USBSS_MG2_MISC Fields: */
#define WCD_USBSS_MG2_MISC_NCOMP_2X_DYN_BST_EN_MASK                      0x80
#define WCD_USBSS_MG2_MISC_PCOMP_2X_DYN_BST_OFF_EN_MASK                  0x40
#define WCD_USBSS_MG2_MISC_PCOMP_2X_DYN_BST_ON_EN_MASK                   0x20
#define WCD_USBSS_MG2_MISC_RED_POFF_THRESH_3X_MASK                       0x10
#define WCD_USBSS_MG2_MISC_ASSERT_PLDN_ON_PWRDN_GSBU2_MASK               0x08
#define WCD_USBSS_MG2_MISC_INC_NEG_SURGE_THRESH_MG2_MASK                 0x04
#define WCD_USBSS_MG2_MISC_MG2_GNDPIN_EN_OVERRIDE_MASK                   0x02
#define WCD_USBSS_MG2_MISC_MG2_GNDPIN_EN_MASK                            0x01

/* WCD_USBSS_BIAS_TOP Fields: */
#define WCD_USBSS_BIAS_TOP_EN_BIAS_TOP_MASK                              0x80
#define WCD_USBSS_BIAS_TOP_VREF_AMP_BIAS_BST_MASK                        0x40
#define WCD_USBSS_BIAS_TOP_OVP_EN_DPDN_OVERRIDE_MASK                     0x20
#define WCD_USBSS_BIAS_TOP_DIS_NCLAMP_DPDN_MASK                          0x10
#define WCD_USBSS_BIAS_TOP_SPARE_BITS_3_0_MASK                           0x0f

/* WCD_USBSS_VREF_CTRL Fields: */
#define WCD_USBSS_VREF_CTRL_EN_VREF_AMP_MASK                             0x80
#define WCD_USBSS_VREF_CTRL_VREF_AMP_TUNE_MASK                           0x78
#define WCD_USBSS_VREF_CTRL_VREF_AMP_TUNE_REG_EN_MASK                    0x04
#define WCD_USBSS_VREF_CTRL_HIGH_CAP_DRV_EN_MASK                         0x02
#define WCD_USBSS_VREF_CTRL_SPARE_BITS_0_MASK                            0x01

/* WCD_USBSS_TOP_MISC1 Fields: */
#define WCD_USBSS_TOP_MISC1_EN_PLDN_GSBU12_MASK                          0x80
#define WCD_USBSS_TOP_MISC1_D_REG_RST_OVP_INTR_MASK                      0x40
#define WCD_USBSS_TOP_MISC1_SPARE_BITS_5_MASK                            0x20
#define WCD_USBSS_TOP_MISC1_SPARE_BITS_4_3_MASK                          0x18
#define WCD_USBSS_TOP_MISC1_OVP_EN_SBU12_OVERRIDE_MASK                   0x04
#define WCD_USBSS_TOP_MISC1_ATEST_SEL_MASK                               0x03

/* WCD_USBSS_TOP_MISC2 Fields: */
#define WCD_USBSS_TOP_MISC2_EN_ADD_PLDN_DP_MASK                          0x80
#define WCD_USBSS_TOP_MISC2_EN_ADD_PLDN_DN_MASK                          0x40
#define WCD_USBSS_TOP_MISC2_EN_ADD_PLDN_MG1_MASK                         0x20
#define WCD_USBSS_TOP_MISC2_EN_ADD_PLDN_MG2_MASK                         0x10
#define WCD_USBSS_TOP_MISC2_EN_PWRUP_CMP_GATE_DP_MASK                    0x08
#define WCD_USBSS_TOP_MISC2_EN_PWRUP_CMP_GATE_DN_MASK                    0x04
#define WCD_USBSS_TOP_MISC2_EN_PWRUP_CMP_GATE_MG1_MASK                   0x02
#define WCD_USBSS_TOP_MISC2_EN_PWRUP_CMP_GATE_MG2_MASK                   0x01

/* WCD_USBSS_STATUS_1 Fields: */
#define WCD_USBSS_STATUS_1_CMP_OUT_PS_DP_MASK                            0x80
#define WCD_USBSS_STATUS_1_CMP_OUT_NS_DP_MASK                            0x40
#define WCD_USBSS_STATUS_1_CMP_OUT_PSNS_DP_MASK                          0x20
#define WCD_USBSS_STATUS_1_CMP_OUT_PS_DN_MASK                            0x10
#define WCD_USBSS_STATUS_1_CMP_OUT_NS_DN_MASK                            0x08
#define WCD_USBSS_STATUS_1_CMP_OUT_PSNS_DN_MASK                          0x04
#define WCD_USBSS_STATUS_1_OVP_DP_EN_MASK                                0x02
#define WCD_USBSS_STATUS_1_OVP_DN_EN_MASK                                0x01

/* WCD_USBSS_STATUS_2 Fields: */
#define WCD_USBSS_STATUS_2_CMP_OUT_PSNS_MG1_MASK                         0x80
#define WCD_USBSS_STATUS_2_CMP_OUT_GNDSW_OFF_MG1_MASK                    0x40
#define WCD_USBSS_STATUS_2_CT_SNS_OVP_DET_LAT_MG1_MASK                   0x20
#define WCD_USBSS_STATUS_2_CMP_OUT_PS_MG2_MASK                           0x10
#define WCD_USBSS_STATUS_2_CMP_OUT_NS_MG2_MASK                           0x08
#define WCD_USBSS_STATUS_2_CMP_OUT_PSNS_MG2_MASK                         0x04
#define WCD_USBSS_STATUS_2_CMP_OUT_GNDSW_OFF_MG2_MASK                    0x02
#define WCD_USBSS_STATUS_2_CT_SNS_OVP_DET_LAT_MG2_MASK                   0x01

/* WCD_USBSS_STATUS_3 Fields: */
#define WCD_USBSS_STATUS_3_VREF_EN_MASK                                  0x80
#define WCD_USBSS_STATUS_3_BIAS_TOP_EN_MASK                              0x40
#define WCD_USBSS_STATUS_3_CMP_OUT_PS_MG1_MASK                           0x20
#define WCD_USBSS_STATUS_3_CMP_OUT_NS_MG1_MASK                           0x10
#define WCD_USBSS_STATUS_3_OVP_MG1_EN_MASK                               0x08
#define WCD_USBSS_STATUS_3_OVP_MG2_EN_MASK                               0x04
#define WCD_USBSS_STATUS_3_SPARE_BITS_1_0_MASK                           0x03


/* WCD_USBSS_EXT_LIN_EN Fields: */
#define WCD_USBSS_EXT_LIN_EN_EXT_LIN_EN_OVR_MASK                         0x80
#define WCD_USBSS_EXT_LIN_EN_EXT_GNDL_LIN_EN_REG_MASK                    0x40
#define WCD_USBSS_EXT_LIN_EN_EXT_L_LIN_EN_REG_MASK                       0x20
#define WCD_USBSS_EXT_LIN_EN_EXT_R_LIN_EN_REG_MASK                       0x10
#define WCD_USBSS_EXT_LIN_EN_EXTSW_LIN_PWR_OVR_EN_MASK                   0x08
#define WCD_USBSS_EXT_LIN_EN_EXT_GNDR_LIN_EN_REG_MASK                    0x04
#define WCD_USBSS_EXT_LIN_EN_CP_SWR_MG12_REG_MASK                        0x02
#define WCD_USBSS_EXT_LIN_EN_D_EXT_GND_LIN_BYP_MASK                      0x01

/* WCD_USBSS_INT_LIN_EN Fields: */
#define WCD_USBSS_INT_LIN_EN_INT_LIN_EN_OVR_MASK                         0x80
#define WCD_USBSS_INT_LIN_EN_INT_L_LIN_EN_REG_MASK                       0x40
#define WCD_USBSS_INT_LIN_EN_INT_R_LIN_EN_REG_MASK                       0x20
#define WCD_USBSS_INT_LIN_EN_INTSW_LIN_PWR_OVR_EN_MASK                   0x10
#define WCD_USBSS_INT_LIN_EN_LDO_SWR_MG12_REG_MASK                       0x08
#define WCD_USBSS_INT_LIN_EN_OVP_SWR_MG12_REG_MASK                       0x04
#define WCD_USBSS_INT_LIN_EN_INT_L_2ND_COMP_EN_REG_MASK                  0x02
#define WCD_USBSS_INT_LIN_EN_INT_R_2ND_COMP_EN_REG_MASK                  0x01

/* WCD_USBSS_COMBINER_IREF_PROG_1 Fields: */
#define WCD_USBSS_COMBINER_IREF_PROG_1_ACDC_COMBINER_AUD_L_IREF_MASK     0xf0
#define WCD_USBSS_COMBINER_IREF_PROG_1_ACDC_COMBINER_AUD_R_IREF_MASK     0x0f

/* WCD_USBSS_COMBINER_IREF_PROG_2 Fields: */
#define WCD_USBSS_COMBINER_IREF_PROG_2_ACDC_COMBINER_GND_L_IREF_MASK     0xf0
#define WCD_USBSS_COMBINER_IREF_PROG_2_ACDC_COMBINER_GND_R_IREF_MASK     0x0f

/* WCD_USBSS_EXTSW_AMP_BIAS Fields: */
#define WCD_USBSS_EXTSW_AMP_BIAS_EXTSW_AMP_BIAS_1_MASK                   0xf0
#define WCD_USBSS_EXTSW_AMP_BIAS_EXTSW_AMP_BIAS_2_MASK                   0x0f

/* WCD_USBSS_INTSW_ILIFT Fields: */
#define WCD_USBSS_INTSW_ILIFT_INTSW_ILIFT_L_MASK                         0xf0
#define WCD_USBSS_INTSW_ILIFT_INTSW_ILIFT_R_MASK                         0x0f

/* WCD_USBSS_EXT_SW_CTRL_1 Fields: */
#define WCD_USBSS_EXT_SW_CTRL_1_CP_SW_OVR_MASK                           0x80
#define WCD_USBSS_EXT_SW_CTRL_1_CP_SW_DNL_REG_MASK                       0x40
#define WCD_USBSS_EXT_SW_CTRL_1_CP_SW_DPR_REG_MASK                       0x20
#define WCD_USBSS_EXT_SW_CTRL_1_CP_SWL_MG12_REG_MASK                     0x10
#define WCD_USBSS_EXT_SW_CTRL_1_LDO_SW_OVR_MASK                          0x08
#define WCD_USBSS_EXT_SW_CTRL_1_LDO_SW_DNL_REG_MASK                      0x04
#define WCD_USBSS_EXT_SW_CTRL_1_LDO_SW_DPR_REG_MASK                      0x02
#define WCD_USBSS_EXT_SW_CTRL_1_LDO_SWL_MG12_REG_MASK                    0x01

/* WCD_USBSS_EXT_SW_CTRL_2 Fields: */
#define WCD_USBSS_EXT_SW_CTRL_2_OVP_SW_OVR_MASK                          0x80
#define WCD_USBSS_EXT_SW_CTRL_2_OVP_SW_DNL_REG_MASK                      0x40
#define WCD_USBSS_EXT_SW_CTRL_2_OVP_SW_DPR_REG_MASK                      0x20
#define WCD_USBSS_EXT_SW_CTRL_2_OVP_SWL_MG12_REG_MASK                    0x10
#define WCD_USBSS_EXT_SW_CTRL_2_VNEGDAC_LDO_BUF_IBIAS_PROG_MASK          0x0f

/* WCD_USBSS_INT_SW_CTRL_1 Fields: */
#define WCD_USBSS_INT_SW_CTRL_1_SW_MIC_EN_OVR_MASK                       0x80
#define WCD_USBSS_INT_SW_CTRL_1_SW_MIC_MG1_EN_REG_MASK                   0x40
#define WCD_USBSS_INT_SW_CTRL_1_SW_MIC_MG2_EN_REG_MASK                   0x20
#define WCD_USBSS_INT_SW_CTRL_1_SW_AGND_EN_OVR_MASK                      0x10
#define WCD_USBSS_INT_SW_CTRL_1_SW_AGND_MG1_EN_REG_MASK                  0x08
#define WCD_USBSS_INT_SW_CTRL_1_SW_AGND_MG2_EN_REG_MASK                  0x04
#define WCD_USBSS_INT_SW_CTRL_1_INT_L_2ND_COMP_EN_OVR_MASK               0x02
#define WCD_USBSS_INT_SW_CTRL_1_INT_R_2ND_COMP_EN_OVR_MASK               0x01

/* WCD_USBSS_INT_SW_CTRL_2 Fields: */
#define WCD_USBSS_INT_SW_CTRL_2_SW_SENSE_GSBU1_EN_REG_MASK               0x80
#define WCD_USBSS_INT_SW_CTRL_2_SW_SENSE_GSBU2_EN_REG_MASK               0x40
#define WCD_USBSS_INT_SW_CTRL_2_INTSW_L_RDC_PRG_MASK                     0x38
#define WCD_USBSS_INT_SW_CTRL_2_INTSW_R_RDC_PRG_MASK                     0x07

/* WCD_USBSS_INT_SW_CTRL_3 Fields: */
#define WCD_USBSS_INT_SW_CTRL_3_D_INTSW_2NDCOMP_PRG_L_MASK               0xe0
#define WCD_USBSS_INT_SW_CTRL_3_D_INTSW_2NDCOMP_PRG_R_MASK               0x1c
#define WCD_USBSS_INT_SW_CTRL_3_SW_SENSE_EN_OVR_MASK                     0x02
#define WCD_USBSS_INT_SW_CTRL_3_VNEGDAC_LDO_BUF_OVR_EN_MASK              0x01

/* WCD_USBSS_ATEST_CTRL Fields: */
#define WCD_USBSS_ATEST_CTRL_ATEST7_MASK                                 0x80
#define WCD_USBSS_ATEST_CTRL_ATEST6_MASK                                 0x40
#define WCD_USBSS_ATEST_CTRL_ATEST5_MASK                                 0x20
#define WCD_USBSS_ATEST_CTRL_ATEST4_MASK                                 0x10
#define WCD_USBSS_ATEST_CTRL_ATEST3_MASK                                 0x08
#define WCD_USBSS_ATEST_CTRL_ATEST2_MASK                                 0x04
#define WCD_USBSS_ATEST_CTRL_ATEST1_MASK                                 0x02
#define WCD_USBSS_ATEST_CTRL_ATEST0_MASK                                 0x01

/* WCD_USBSS_EXT_LIN_AUD_CEQ_PRG Fields: */
#define WCD_USBSS_EXT_LIN_AUD_CEQ_PRG_EXT_AUD_LINL_AUD_CEQ_MASK          0xf0
#define WCD_USBSS_EXT_LIN_AUD_CEQ_PRG_EXT_AUD_LINR_AUD_CEQ_MASK          0x0f

/* WCD_USBSS_EXT_LIN_GND_CEQ_PRG Fields: */
#define WCD_USBSS_EXT_LIN_GND_CEQ_PRG_EXT_GND_LINL_AUD_CEQ_MASK          0xf0
#define WCD_USBSS_EXT_LIN_GND_CEQ_PRG_EXT_GND_LINR_AUD_CEQ_MASK          0x0f

/* WCD_USBSS_LIN_STATUS_1 Fields: */
#define WCD_USBSS_LIN_STATUS_1_D_EXT_AUDSW_VRATIO_L_MASK                 0xff

/* WCD_USBSS_LIN_STATUS_2 Fields: */
#define WCD_USBSS_LIN_STATUS_2_D_EXT_AUDSW_VRATIO_R_MASK                 0xff

/* WCD_USBSS_LIN_STATUS_3 Fields: */
#define WCD_USBSS_LIN_STATUS_3_D_EXT_GNDSW_VRATIO_L_MASK                 0xff

/* WCD_USBSS_LIN_STATUS_4 Fields: */
#define WCD_USBSS_LIN_STATUS_4_D_EXT_GNDSW_VRATIO_R_MASK                 0xff

/* WCD_USBSS_SW_LIN_CTRL Fields: */
#define WCD_USBSS_SW_LIN_CTRL_D_INTSW_LEGACY_PULL_VNEGDAC_REG_MASK       0x80
#define WCD_USBSS_SW_LIN_CTRL_INTSW_LEGACY_PULL_VNEGDAC_OVR_MASK         0x40
#define WCD_USBSS_SW_LIN_CTRL_D_INTSW_PULL_GND_REG_MASK                  0x20
#define WCD_USBSS_SW_LIN_CTRL_INTSW_PULL_GND_OVR_MASK                    0x10
#define WCD_USBSS_SW_LIN_CTRL_D_RDAC_CEQ_EN_MASK                         0x08
#define WCD_USBSS_SW_LIN_CTRL_D_INTSW_LEGACY_EN_REG_MASK                 0x04
#define WCD_USBSS_SW_LIN_CTRL_D_INTSW_LEGACY_EN_OVR_MASK                 0x02
#define WCD_USBSS_SW_LIN_CTRL_LIN_TOP_ATEST_EN_MASK                      0x01

/* WCD_USBSS_SW_LIN_CTRL_1 Fields: */
#define WCD_USBSS_SW_LIN_CTRL_1_AC_TRIMCODE_OVR_EN_MASK                  0x02
#define WCD_USBSS_SW_LIN_CTRL_1_DC_TRIMCODE_OVR_EN_MASK                  0x01


/* WCD_USBSS_LDO_3P6 Fields: */
#define WCD_USBSS_LDO_3P6_D_INT_AUDSW_DNW_DBG_MASK                       0x80
#define WCD_USBSS_LDO_3P6_D_USB_PMIC_USB_DNW_DBG_MASK                    0x40
#define WCD_USBSS_LDO_3P6_NOT_USED_MASK                                  0x3f

/* WCD_USBSS_SWITCH_BANK_ATEST Fields: */
#define WCD_USBSS_SWITCH_BANK_ATEST_ATEST_SWBANK_EN_MASK                 0x80
#define WCD_USBSS_SWITCH_BANK_ATEST_ATEST_DPRDNL_SWBANK_MASK             0x70
#define WCD_USBSS_SWITCH_BANK_ATEST_ATEST_GSBU_SWBANK_MASK               0x0e
#define WCD_USBSS_SWITCH_BANK_ATEST_NOT_USED_MASK                        0x01

/* WCD_USBSS_EQ_EN Fields: */
#define WCD_USBSS_EQ_EN_D_REG_CTL_OVERRIDE_MASK                          0x80
#define WCD_USBSS_EQ_EN_D_REG_EN_EQUALIZER_MASK                          0x40
#define WCD_USBSS_EQ_EN_D_REG_EQ_REF_SEL_MASK                            0x30
#define WCD_USBSS_EQ_EN_D_REG_EQ_SEG_SEL_MASK                            0x0f

/* WCD_USBSS_EQ_MISC Fields: */
#define WCD_USBSS_EQ_MISC_ATEST_SEL_MASK                                 0xc0
#define WCD_USBSS_EQ_MISC_D_REG_EQ_BIAS_SEL_MASK                         0x30
#define WCD_USBSS_EQ_MISC_D_REG_EQ_RC_SEL_MASK                           0x0c
#define WCD_USBSS_EQ_MISC_D_REG_EQ_VREF_BIAS_SEL_MASK                    0x03

/* WCD_USBSS_STATUS_MISC Fields: */
#define WCD_USBSS_STATUS_MISC_NOT_USED_MASK                              0xff


/* WCD_USBSS_FSM_DELAYS1 Fields: */
#define WCD_USBSS_FSM_DELAYS1_DELTA_T2_MASK                              0xc0
#define WCD_USBSS_FSM_DELAYS1_SPARE_BITS_5_0_MASK                        0x3f

/* WCD_USBSS_FSM_DELAYS2 Fields: */
#define WCD_USBSS_FSM_DELAYS2_DELTA_T4_MASK                              0xf0
#define WCD_USBSS_FSM_DELAYS2_DELTA_T3_MASK                              0x0f

/* WCD_USBSS_FSM_DELAYS3 Fields: */
#define WCD_USBSS_FSM_DELAYS3_DELTA_T5_MASK                              0xf8
#define WCD_USBSS_FSM_DELAYS3_SPARE_BITS_2_0_MASK                        0x07

/* WCD_USBSS_FSM_DELAYS4 Fields: */
#define WCD_USBSS_FSM_DELAYS4_DELTA_T6_MASK                              0xf8
#define WCD_USBSS_FSM_DELAYS4_SPARE_BITS_2_0_MASK                        0x07

/* WCD_USBSS_FSM_DELAYS5 Fields: */
#define WCD_USBSS_FSM_DELAYS5_DELTA_T8_MASK                              0xf0
#define WCD_USBSS_FSM_DELAYS5_DELTA_T7_MASK                              0x0c
#define WCD_USBSS_FSM_DELAYS5_SPARE_BITS_1_0_MASK                        0x03

/* WCD_USBSS_FSM_DELAYS6 Fields: */
#define WCD_USBSS_FSM_DELAYS6_DELTA_T11_MASK                             0xf0
#define WCD_USBSS_FSM_DELAYS6_DELTA_T9_MASK                              0x0f

/* WCD_USBSS_FSM_DELAYS7 Fields: */
#define WCD_USBSS_FSM_DELAYS7_DELTA_T14_MASK                             0xf0
#define WCD_USBSS_FSM_DELAYS7_DELTA_T12_MASK                             0x0f

/* WCD_USBSS_FSM_DELAYS8 Fields: */
#define WCD_USBSS_FSM_DELAYS8_DELTA_T13_MASK                             0xf8
#define WCD_USBSS_FSM_DELAYS8_D_REG_EN_SUP_SW_OVPREF_MASK                0x04
#define WCD_USBSS_FSM_DELAYS8_SPARE_BITS_1_0_MASK                        0x03

/* WCD_USBSS_FSM_DEBUG_SIGNALS Fields: */
#define WCD_USBSS_FSM_DEBUG_SIGNALS_D_EN_OVPREF_REG_MASK                 0x80
#define WCD_USBSS_FSM_DEBUG_SIGNALS_D_EN_CP_REG_MASK                     0x40
#define WCD_USBSS_FSM_DEBUG_SIGNALS_D_EN_LDO_REG_MASK                    0x20
#define WCD_USBSS_FSM_DEBUG_SIGNALS_D_USB_STANDBY_STATE_EN_DELAYED1_REG_MASK 0x10
#define WCD_USBSS_FSM_DEBUG_SIGNALS_D_USB_STANDBY_STATE_EN_DELAYED2_REG_MASK 0x08
#define WCD_USBSS_FSM_DEBUG_SIGNALS_D_REG_FIX_MICGND_SWAP_ENB_MASK       0x04
#define WCD_USBSS_FSM_DEBUG_SIGNALS_D_REG_OVERRIDE_SUPPLY_SWITCH_MASK    0x02
#define WCD_USBSS_FSM_DEBUG_SIGNALS_D_REG_FIX_REDUCEPAVNEGCURRENT_ENB_MASK 0x01

/* WCD_USBSS_FSM_OVERRIDE Fields: */
#define WCD_USBSS_FSM_OVERRIDE_D_FSM_EN_OVERRIDE_MASK                    0x80
#define WCD_USBSS_FSM_OVERRIDE_D_VNEG_PULLDN_MASK_MASK                   0x40
#define WCD_USBSS_FSM_OVERRIDE_EQ_ENABLE_GATED_BY_OVP_MASK               0x20
#define WCD_USBSS_FSM_OVERRIDE_RCO_FAST_RATE_EFUSE_OVERRIDE_MASK         0x10
#define WCD_USBSS_FSM_OVERRIDE_OVP_THRESHOLD_EFUSE_OVERRIDE_MASK         0x08
#define WCD_USBSS_FSM_OVERRIDE_EQ_SEG_SEL_EFUSE_OVERRIDE_MASK            0x04
#define WCD_USBSS_FSM_OVERRIDE_CP_PFM_EFUSE_OVERRIDE_MASK                0x02
#define WCD_USBSS_FSM_OVERRIDE_EQ_EN_EFUSE_OVERRIDE_MASK                 0x01

/* WCD_USBSS_ENABLE_STATUS Fields: */
#define WCD_USBSS_ENABLE_STATUS_D_EN_OVPREF_MASK                         0x80
#define WCD_USBSS_ENABLE_STATUS_D_EN_CP_MASK                             0x40
#define WCD_USBSS_ENABLE_STATUS_D_EN_LDO_MASK                            0x20
#define WCD_USBSS_ENABLE_STATUS_D_USB_STANDBY_STATE_EXTENDED1_MASK       0x10
#define WCD_USBSS_ENABLE_STATUS_D_USB_STANDBY_STATE_EXTENDED2_MASK       0x08
#define WCD_USBSS_ENABLE_STATUS_TIE_LOW_MASK                             0x07

/* WCD_USBSS_FRZ_STATUS Fields: */
#define WCD_USBSS_FRZ_STATUS_D_FRZ_N_SWITCHES_EQ_MASK                    0x80
#define WCD_USBSS_FRZ_STATUS_D_FRZ_ALL_MASK                              0x40
#define WCD_USBSS_FRZ_STATUS_D_FRZ_OVPREF_MASK                           0x20
#define WCD_USBSS_FRZ_STATUS_D_FSM_RESET_MASK                            0x10
#define WCD_USBSS_FRZ_STATUS_TIE_LOW_MASK                                0x0f


/* WCD_USBSS_DPR_DNL_SWITCH_ENABLE_STATUS Fields: */
#define WCD_USBSS_DPR_DNL_SWITCH_ENABLE_STATUS_D_SW_DP_DPR_ENABLE_MASK   0x80
#define WCD_USBSS_DPR_DNL_SWITCH_ENABLE_STATUS_D_SW_DP2_DPR_ENABLE_MASK  0x40
#define WCD_USBSS_DPR_DNL_SWITCH_ENABLE_STATUS_D_SW_R_DPR_ENABLE_MASK    0x20
#define WCD_USBSS_DPR_DNL_SWITCH_ENABLE_STATUS_D_SW_DN_DNL_ENABLE_MASK   0x10
#define WCD_USBSS_DPR_DNL_SWITCH_ENABLE_STATUS_D_SW_DN2_DNL_ENABLE_MASK  0x08
#define WCD_USBSS_DPR_DNL_SWITCH_ENABLE_STATUS_D_SW_L_DNL_ENABLE_MASK    0x04
#define WCD_USBSS_DPR_DNL_SWITCH_ENABLE_STATUS_TIE_LOW_MASK              0x03

/* WCD_USBSS_SBU_GSBU_SWITCH_ENABLE_STATUS Fields: */
#define WCD_USBSS_SBU_GSBU_SWITCH_ENABLE_STATUS_D_SW_MIC_MG1_ENABLE_MASK 0x80
#define WCD_USBSS_SBU_GSBU_SWITCH_ENABLE_STATUS_D_SW_MIC_MG2_ENABLE_MASK 0x40
#define WCD_USBSS_SBU_GSBU_SWITCH_ENABLE_STATUS_D_SW_AGND_MG1_ENABLE_MASK 0x20
#define WCD_USBSS_SBU_GSBU_SWITCH_ENABLE_STATUS_D_SW_AGND_MG2_ENABLE_MASK 0x10
#define WCD_USBSS_SBU_GSBU_SWITCH_ENABLE_STATUS_D_SW_SENSE_GSBU1_ENABLE_MASK 0x08
#define WCD_USBSS_SBU_GSBU_SWITCH_ENABLE_STATUS_D_SW_SENSE_GSBU2_ENABLE_MASK 0x04
#define WCD_USBSS_SBU_GSBU_SWITCH_ENABLE_STATUS_TIE_LOW_MASK             0x03

/* WCD_USBSS_DPAUX_SWITCH_ENABLE_STATUS Fields: */
#define WCD_USBSS_DPAUX_SWITCH_ENABLE_STATUS_D_SW_DPAUXM_MG1_ENABLE_MASK 0x80
#define WCD_USBSS_DPAUX_SWITCH_ENABLE_STATUS_D_SW_DPAUXM_MG2_ENABLE_MASK 0x40
#define WCD_USBSS_DPAUX_SWITCH_ENABLE_STATUS_D_SW_DPAUXP_MG1_ENABLE_MASK 0x20
#define WCD_USBSS_DPAUX_SWITCH_ENABLE_STATUS_D_SW_DPAUXP_MG2_ENABLE_MASK 0x10
#define WCD_USBSS_DPAUX_SWITCH_ENABLE_STATUS_TIE_LOW_MASK                0x0f

/* WCD_USBSS_DPR_DNL_EXTFET_GATE_MUX_STATUS Fields: */
#define WCD_USBSS_DPR_DNL_EXTFET_GATE_MUX_STATUS_D_LIN_EN_DNL_MASK       0x80
#define WCD_USBSS_DPR_DNL_EXTFET_GATE_MUX_STATUS_D_CP_SW_EN_DNL_MASK     0x40
#define WCD_USBSS_DPR_DNL_EXTFET_GATE_MUX_STATUS_D_OVP_SW_EN_DNL_MASK    0x20
#define WCD_USBSS_DPR_DNL_EXTFET_GATE_MUX_STATUS_D_LDO_SW_EN_DNL_MASK    0x10
#define WCD_USBSS_DPR_DNL_EXTFET_GATE_MUX_STATUS_D_LIN_EN_DPR_MASK       0x08
#define WCD_USBSS_DPR_DNL_EXTFET_GATE_MUX_STATUS_D_CP_SW_EN_DPR_MASK     0x04
#define WCD_USBSS_DPR_DNL_EXTFET_GATE_MUX_STATUS_D_OVP_SW_EN_DPR_MASK    0x02
#define WCD_USBSS_DPR_DNL_EXTFET_GATE_MUX_STATUS_D_LDO_SW_EN_DPR_MASK    0x01

/* WCD_USBSS_SBU_EXTFET_GATE_MUX_OVP_STATUS Fields: */
#define WCD_USBSS_SBU_EXTFET_GATE_MUX_OVP_STATUS_D_LIN_EN_MG12_MASK      0x80
#define WCD_USBSS_SBU_EXTFET_GATE_MUX_OVP_STATUS_D_CP_SW_EN_MG12_MASK    0x40
#define WCD_USBSS_SBU_EXTFET_GATE_MUX_OVP_STATUS_D_OVP_SW_EN_MG12_MASK   0x20
#define WCD_USBSS_SBU_EXTFET_GATE_MUX_OVP_STATUS_D_LDO_SW_EN_MG12_MASK   0x10
#define WCD_USBSS_SBU_EXTFET_GATE_MUX_OVP_STATUS_D_EQ_EN_MASK            0x08
#define WCD_USBSS_SBU_EXTFET_GATE_MUX_OVP_STATUS_D_OVP_MG12_ENABLE_MASK  0x04
#define WCD_USBSS_SBU_EXTFET_GATE_MUX_OVP_STATUS_D_OVP_DPR_ENABLE_MASK   0x02
#define WCD_USBSS_SBU_EXTFET_GATE_MUX_OVP_STATUS_D_OVP_DNL_ENABLE_MASK   0x01

/* WCD_USBSS_CP_LIN_CNTL_STATUS Fields: */
#define WCD_USBSS_CP_LIN_CNTL_STATUS_D_CP_ENABLE_MASK                    0x80
#define WCD_USBSS_CP_LIN_CNTL_STATUS_D_CP_PFM_ENABLE_MASK                0x40
#define WCD_USBSS_CP_LIN_CNTL_STATUS_D_CP_CLK_DIV_MASK                   0x30
#define WCD_USBSS_CP_LIN_CNTL_STATUS_D_LIN_MODE_MASK                     0x0c
#define WCD_USBSS_CP_LIN_CNTL_STATUS_D_AUDIO_EN_OVP_MASK                 0x02
#define WCD_USBSS_CP_LIN_CNTL_STATUS_TIE_LOW_MASK                        0x01


/* WCD_USBSS_DISP_AUXP_THRESH Fields: */
#define WCD_USBSS_DISP_AUXP_THRESH_DISP_AUXP_OVPON_CM_MASK               0xe0
#define WCD_USBSS_DISP_AUXP_THRESH_DISP_AUXP_OVPON_INCNEG_CM_MASK        0x1c
#define WCD_USBSS_DISP_AUXP_THRESH_SPARE_BITS_1_0_MASK                   0x03

/* WCD_USBSS_DISP_AUXP_CTL Fields: */
#define WCD_USBSS_DISP_AUXP_CTL_GNDSW_LK_TRK_EN_MASK                     0x80
#define WCD_USBSS_DISP_AUXP_CTL_DISP_CM_MOD_EN_MASK                      0x40
#define WCD_USBSS_DISP_AUXP_CTL_DISP_MODE_OVERRIDE_EN_MASK               0x20
#define WCD_USBSS_DISP_AUXP_CTL_OVP_NEG_THRESH_INC_OVERRIDE_EN_MASK      0x10
#define WCD_USBSS_DISP_AUXP_CTL_OVP_OVERRIDE_EN_MASK                     0x08
#define WCD_USBSS_DISP_AUXP_CTL_LK_CANCEL_TRK_COEFF_MASK                 0x07

/* WCD_USBSS_DISP_AUXM_THRESH Fields: */
#define WCD_USBSS_DISP_AUXM_THRESH_SBU1_DISP_AUXM_OVPON_CM_MASK          0xe0
#define WCD_USBSS_DISP_AUXM_THRESH_SPARE_BITS_4_0_MASK                   0x1f

/* WCD_USBSS_DISP_AUXM_CTL Fields: */
#define WCD_USBSS_DISP_AUXM_CTL_GNDSW_LK_TRK_EN_MASK                     0x80
#define WCD_USBSS_DISP_AUXM_CTL_DISP_CM_MOD_EN_MASK                      0x40
#define WCD_USBSS_DISP_AUXM_CTL_DISP_MODE_OVERRIDE_EN_MASK               0x20
#define WCD_USBSS_DISP_AUXM_CTL_SPARE_BITS_4_MASK                        0x10
#define WCD_USBSS_DISP_AUXM_CTL_OVP_OVERRIDE_EN_MASK                     0x08
#define WCD_USBSS_DISP_AUXM_CTL_LK_CANCEL_TRK_COEFF_MASK                 0x07


/* WCD_USBSS_CTRL_0 Fields: */
#define WCD_USBSS_CTRL_0_PWDN_CTL_MASK                                   0x80
#define WCD_USBSS_CTRL_0_CAL_CTL_MASK                                    0x40
#define WCD_USBSS_CTRL_0_IBIAS_ERRAMP_MASK                               0x30
#define WCD_USBSS_CTRL_0_RES_LOAD_CTL_MASK                               0x0c
#define WCD_USBSS_CTRL_0_FB_GNDSW_OVERRIDE_MASK                          0x02

/* WCD_USBSS_CTRL_1 Fields: */
#define WCD_USBSS_CTRL_1_VOUT_PROG_MASK                                  0xf0
#define WCD_USBSS_CTRL_1_VOUT_CAL_MASK                                   0x0f


/* WCD_USBSS_DC_TRIMCODE_1 Fields: */
#define WCD_USBSS_DC_TRIMCODE_1_AUDSW_R_OFFSET_TRIM_4_2_MASK             0xe0
#define WCD_USBSS_DC_TRIMCODE_1_AUDSW_L_OFFSET_TRIM_4_0_MASK             0x1f

/* WCD_USBSS_DC_TRIMCODE_2 Fields: */
#define WCD_USBSS_DC_TRIMCODE_2_SPARE_MASK                               0x80
#define WCD_USBSS_DC_TRIMCODE_2_GNDSW_L_OFFSET_TRIM_4_0_MASK             0x7c
#define WCD_USBSS_DC_TRIMCODE_2_AUDSW_R_OFFSET_TRIM_1_0_MASK             0x03

/* WCD_USBSS_DC_TRIMCODE_3 Fields: */
#define WCD_USBSS_DC_TRIMCODE_3_SPARE_MASK                               0x80
#define WCD_USBSS_DC_TRIMCODE_3_AUDSW_R_GAIN_TRIM_1_0_MASK               0x60
#define WCD_USBSS_DC_TRIMCODE_3_GNDSW_R_OFFSET_TRIM_4_0_MASK             0x1f

/* WCD_USBSS_AC_TRIMCODE_1 Fields: */
#define WCD_USBSS_AC_TRIMCODE_1_AUDSW_R_GAIN_TRIM_4_2_MASK               0xe0
#define WCD_USBSS_AC_TRIMCODE_1_AUDSW_L_GAIN_TRIM_4_0_MASK               0x1f

/* WCD_USBSS_AC_TRIMCODE_2 Fields: */
#define WCD_USBSS_AC_TRIMCODE_2_GNDSW_R_GAIN_TRIM_3_0_MASK               0xe0
#define WCD_USBSS_AC_TRIMCODE_2_GNDSW_L_GAIN_TRIM_3_0_MASK               0x1f


/* WCD_USBSS_CPLDO_CTL1 Fields: */
#define WCD_USBSS_CPLDO_CTL1_CPLDO_EN_MASK                               0x80
#define WCD_USBSS_CPLDO_CTL1_CPLDO_TOP_EN_OVRRD_MASK                     0x40
#define WCD_USBSS_CPLDO_CTL1_BYPASS_EN_OVERRIDE_MASK                     0x20
#define WCD_USBSS_CPLDO_CTL1_LDO_TUNE_MASK                               0x18
#define WCD_USBSS_CPLDO_CTL1_LDO_IQ_INC_MASK                             0x04
#define WCD_USBSS_CPLDO_CTL1_SPARE_BITS_2_0_MASK                         0x03

/* WCD_USBSS_CPLDO_CTL2 Fields: */
#define WCD_USBSS_CPLDO_CTL2_SPARE_BITS_7_0_MASK                         0xff


/* WCD_USBSS_LUT_REG0 Fields: */
#define WCD_USBSS_LUT_REG0_REG_MASK                                      0xff

/* WCD_USBSS_LUT_REG1 Fields: */
#define WCD_USBSS_LUT_REG1_REG_MASK                                      0xff

/* WCD_USBSS_LUT_REG2 Fields: */
#define WCD_USBSS_LUT_REG2_REG_MASK                                      0xff

/* WCD_USBSS_LUT_REG3 Fields: */
#define WCD_USBSS_LUT_REG3_REG_MASK                                      0xff

/* WCD_USBSS_LUT_REG4 Fields: */
#define WCD_USBSS_LUT_REG4_REG_MASK                                      0xff

/* WCD_USBSS_LUT_REG5 Fields: */
#define WCD_USBSS_LUT_REG5_REG_MASK                                      0xff

/* WCD_USBSS_LUT_REG6 Fields: */
#define WCD_USBSS_LUT_REG6_REG_MASK                                      0xff

/* WCD_USBSS_LUT_REG7 Fields: */
#define WCD_USBSS_LUT_REG7_REG_MASK                                      0xff

/* WCD_USBSS_LUT_REG8 Fields: */
#define WCD_USBSS_LUT_REG8_REG_MASK                                      0xff

/* WCD_USBSS_LUT_REG9 Fields: */
#define WCD_USBSS_LUT_REG9_REG_MASK                                      0xff

/* WCD_USBSS_LUT_REG10 Fields: */
#define WCD_USBSS_LUT_REG10_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG11 Fields: */
#define WCD_USBSS_LUT_REG11_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG12 Fields: */
#define WCD_USBSS_LUT_REG12_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG13 Fields: */
#define WCD_USBSS_LUT_REG13_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG14 Fields: */
#define WCD_USBSS_LUT_REG14_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG15 Fields: */
#define WCD_USBSS_LUT_REG15_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG16 Fields: */
#define WCD_USBSS_LUT_REG16_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG17 Fields: */
#define WCD_USBSS_LUT_REG17_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG18 Fields: */
#define WCD_USBSS_LUT_REG18_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG19 Fields: */
#define WCD_USBSS_LUT_REG19_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG20 Fields: */
#define WCD_USBSS_LUT_REG20_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG21 Fields: */
#define WCD_USBSS_LUT_REG21_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG22 Fields: */
#define WCD_USBSS_LUT_REG22_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG23 Fields: */
#define WCD_USBSS_LUT_REG23_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG24 Fields: */
#define WCD_USBSS_LUT_REG24_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG25 Fields: */
#define WCD_USBSS_LUT_REG25_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG26 Fields: */
#define WCD_USBSS_LUT_REG26_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG27 Fields: */
#define WCD_USBSS_LUT_REG27_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG28 Fields: */
#define WCD_USBSS_LUT_REG28_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG29 Fields: */
#define WCD_USBSS_LUT_REG29_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG30 Fields: */
#define WCD_USBSS_LUT_REG30_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG31 Fields: */
#define WCD_USBSS_LUT_REG31_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG32 Fields: */
#define WCD_USBSS_LUT_REG32_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG33 Fields: */
#define WCD_USBSS_LUT_REG33_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG34 Fields: */
#define WCD_USBSS_LUT_REG34_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG35 Fields: */
#define WCD_USBSS_LUT_REG35_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG36 Fields: */
#define WCD_USBSS_LUT_REG36_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG37 Fields: */
#define WCD_USBSS_LUT_REG37_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG38 Fields: */
#define WCD_USBSS_LUT_REG38_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG39 Fields: */
#define WCD_USBSS_LUT_REG39_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG40 Fields: */
#define WCD_USBSS_LUT_REG40_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG41 Fields: */
#define WCD_USBSS_LUT_REG41_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG42 Fields: */
#define WCD_USBSS_LUT_REG42_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG43 Fields: */
#define WCD_USBSS_LUT_REG43_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG44 Fields: */
#define WCD_USBSS_LUT_REG44_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG45 Fields: */
#define WCD_USBSS_LUT_REG45_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG46 Fields: */
#define WCD_USBSS_LUT_REG46_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG47 Fields: */
#define WCD_USBSS_LUT_REG47_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG48 Fields: */
#define WCD_USBSS_LUT_REG48_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG49 Fields: */
#define WCD_USBSS_LUT_REG49_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG50 Fields: */
#define WCD_USBSS_LUT_REG50_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG51 Fields: */
#define WCD_USBSS_LUT_REG51_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG52 Fields: */
#define WCD_USBSS_LUT_REG52_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG53 Fields: */
#define WCD_USBSS_LUT_REG53_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG54 Fields: */
#define WCD_USBSS_LUT_REG54_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG55 Fields: */
#define WCD_USBSS_LUT_REG55_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG56 Fields: */
#define WCD_USBSS_LUT_REG56_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG57 Fields: */
#define WCD_USBSS_LUT_REG57_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG58 Fields: */
#define WCD_USBSS_LUT_REG58_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG59 Fields: */
#define WCD_USBSS_LUT_REG59_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG60 Fields: */
#define WCD_USBSS_LUT_REG60_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG61 Fields: */
#define WCD_USBSS_LUT_REG61_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG62 Fields: */
#define WCD_USBSS_LUT_REG62_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG63 Fields: */
#define WCD_USBSS_LUT_REG63_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG64 Fields: */
#define WCD_USBSS_LUT_REG64_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG65 Fields: */
#define WCD_USBSS_LUT_REG65_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG66 Fields: */
#define WCD_USBSS_LUT_REG66_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG67 Fields: */
#define WCD_USBSS_LUT_REG67_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG68 Fields: */
#define WCD_USBSS_LUT_REG68_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG69 Fields: */
#define WCD_USBSS_LUT_REG69_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG70 Fields: */
#define WCD_USBSS_LUT_REG70_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG71 Fields: */
#define WCD_USBSS_LUT_REG71_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG72 Fields: */
#define WCD_USBSS_LUT_REG72_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG73 Fields: */
#define WCD_USBSS_LUT_REG73_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG74 Fields: */
#define WCD_USBSS_LUT_REG74_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG75 Fields: */
#define WCD_USBSS_LUT_REG75_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG76 Fields: */
#define WCD_USBSS_LUT_REG76_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG77 Fields: */
#define WCD_USBSS_LUT_REG77_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG78 Fields: */
#define WCD_USBSS_LUT_REG78_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG79 Fields: */
#define WCD_USBSS_LUT_REG79_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG80 Fields: */
#define WCD_USBSS_LUT_REG80_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG81 Fields: */
#define WCD_USBSS_LUT_REG81_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG82 Fields: */
#define WCD_USBSS_LUT_REG82_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG83 Fields: */
#define WCD_USBSS_LUT_REG83_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG84 Fields: */
#define WCD_USBSS_LUT_REG84_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG85 Fields: */
#define WCD_USBSS_LUT_REG85_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG86 Fields: */
#define WCD_USBSS_LUT_REG86_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG87 Fields: */
#define WCD_USBSS_LUT_REG87_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG88 Fields: */
#define WCD_USBSS_LUT_REG88_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG89 Fields: */
#define WCD_USBSS_LUT_REG89_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG90 Fields: */
#define WCD_USBSS_LUT_REG90_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG91 Fields: */
#define WCD_USBSS_LUT_REG91_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG92 Fields: */
#define WCD_USBSS_LUT_REG92_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG93 Fields: */
#define WCD_USBSS_LUT_REG93_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG94 Fields: */
#define WCD_USBSS_LUT_REG94_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG95 Fields: */
#define WCD_USBSS_LUT_REG95_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG96 Fields: */
#define WCD_USBSS_LUT_REG96_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG97 Fields: */
#define WCD_USBSS_LUT_REG97_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG98 Fields: */
#define WCD_USBSS_LUT_REG98_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG99 Fields: */
#define WCD_USBSS_LUT_REG99_REG_MASK                                     0xff

/* WCD_USBSS_LUT_REG100 Fields: */
#define WCD_USBSS_LUT_REG100_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG101 Fields: */
#define WCD_USBSS_LUT_REG101_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG102 Fields: */
#define WCD_USBSS_LUT_REG102_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG103 Fields: */
#define WCD_USBSS_LUT_REG103_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG104 Fields: */
#define WCD_USBSS_LUT_REG104_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG105 Fields: */
#define WCD_USBSS_LUT_REG105_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG106 Fields: */
#define WCD_USBSS_LUT_REG106_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG107 Fields: */
#define WCD_USBSS_LUT_REG107_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG108 Fields: */
#define WCD_USBSS_LUT_REG108_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG109 Fields: */
#define WCD_USBSS_LUT_REG109_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG110 Fields: */
#define WCD_USBSS_LUT_REG110_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG111 Fields: */
#define WCD_USBSS_LUT_REG111_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG112 Fields: */
#define WCD_USBSS_LUT_REG112_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG113 Fields: */
#define WCD_USBSS_LUT_REG113_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG114 Fields: */
#define WCD_USBSS_LUT_REG114_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG115 Fields: */
#define WCD_USBSS_LUT_REG115_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG116 Fields: */
#define WCD_USBSS_LUT_REG116_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG117 Fields: */
#define WCD_USBSS_LUT_REG117_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG118 Fields: */
#define WCD_USBSS_LUT_REG118_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG119 Fields: */
#define WCD_USBSS_LUT_REG119_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG120 Fields: */
#define WCD_USBSS_LUT_REG120_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG121 Fields: */
#define WCD_USBSS_LUT_REG121_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG122 Fields: */
#define WCD_USBSS_LUT_REG122_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG123 Fields: */
#define WCD_USBSS_LUT_REG123_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG124 Fields: */
#define WCD_USBSS_LUT_REG124_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG125 Fields: */
#define WCD_USBSS_LUT_REG125_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG126 Fields: */
#define WCD_USBSS_LUT_REG126_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG127 Fields: */
#define WCD_USBSS_LUT_REG127_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG128 Fields: */
#define WCD_USBSS_LUT_REG128_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG129 Fields: */
#define WCD_USBSS_LUT_REG129_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG130 Fields: */
#define WCD_USBSS_LUT_REG130_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG131 Fields: */
#define WCD_USBSS_LUT_REG131_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG132 Fields: */
#define WCD_USBSS_LUT_REG132_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG133 Fields: */
#define WCD_USBSS_LUT_REG133_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG134 Fields: */
#define WCD_USBSS_LUT_REG134_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG135 Fields: */
#define WCD_USBSS_LUT_REG135_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG136 Fields: */
#define WCD_USBSS_LUT_REG136_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG137 Fields: */
#define WCD_USBSS_LUT_REG137_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG138 Fields: */
#define WCD_USBSS_LUT_REG138_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG139 Fields: */
#define WCD_USBSS_LUT_REG139_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG140 Fields: */
#define WCD_USBSS_LUT_REG140_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG141 Fields: */
#define WCD_USBSS_LUT_REG141_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG142 Fields: */
#define WCD_USBSS_LUT_REG142_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG143 Fields: */
#define WCD_USBSS_LUT_REG143_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG144 Fields: */
#define WCD_USBSS_LUT_REG144_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG145 Fields: */
#define WCD_USBSS_LUT_REG145_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG146 Fields: */
#define WCD_USBSS_LUT_REG146_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG147 Fields: */
#define WCD_USBSS_LUT_REG147_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG148 Fields: */
#define WCD_USBSS_LUT_REG148_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG149 Fields: */
#define WCD_USBSS_LUT_REG149_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG150 Fields: */
#define WCD_USBSS_LUT_REG150_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG151 Fields: */
#define WCD_USBSS_LUT_REG151_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG152 Fields: */
#define WCD_USBSS_LUT_REG152_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG153 Fields: */
#define WCD_USBSS_LUT_REG153_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG154 Fields: */
#define WCD_USBSS_LUT_REG154_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG155 Fields: */
#define WCD_USBSS_LUT_REG155_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG156 Fields: */
#define WCD_USBSS_LUT_REG156_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG157 Fields: */
#define WCD_USBSS_LUT_REG157_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG158 Fields: */
#define WCD_USBSS_LUT_REG158_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG159 Fields: */
#define WCD_USBSS_LUT_REG159_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG160 Fields: */
#define WCD_USBSS_LUT_REG160_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG161 Fields: */
#define WCD_USBSS_LUT_REG161_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG162 Fields: */
#define WCD_USBSS_LUT_REG162_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG163 Fields: */
#define WCD_USBSS_LUT_REG163_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG164 Fields: */
#define WCD_USBSS_LUT_REG164_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG165 Fields: */
#define WCD_USBSS_LUT_REG165_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG166 Fields: */
#define WCD_USBSS_LUT_REG166_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG167 Fields: */
#define WCD_USBSS_LUT_REG167_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG168 Fields: */
#define WCD_USBSS_LUT_REG168_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG169 Fields: */
#define WCD_USBSS_LUT_REG169_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG170 Fields: */
#define WCD_USBSS_LUT_REG170_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG171 Fields: */
#define WCD_USBSS_LUT_REG171_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG172 Fields: */
#define WCD_USBSS_LUT_REG172_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG173 Fields: */
#define WCD_USBSS_LUT_REG173_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG174 Fields: */
#define WCD_USBSS_LUT_REG174_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG175 Fields: */
#define WCD_USBSS_LUT_REG175_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG176 Fields: */
#define WCD_USBSS_LUT_REG176_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG177 Fields: */
#define WCD_USBSS_LUT_REG177_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG178 Fields: */
#define WCD_USBSS_LUT_REG178_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG179 Fields: */
#define WCD_USBSS_LUT_REG179_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG180 Fields: */
#define WCD_USBSS_LUT_REG180_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG181 Fields: */
#define WCD_USBSS_LUT_REG181_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG182 Fields: */
#define WCD_USBSS_LUT_REG182_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG183 Fields: */
#define WCD_USBSS_LUT_REG183_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG184 Fields: */
#define WCD_USBSS_LUT_REG184_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG185 Fields: */
#define WCD_USBSS_LUT_REG185_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG186 Fields: */
#define WCD_USBSS_LUT_REG186_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG187 Fields: */
#define WCD_USBSS_LUT_REG187_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG188 Fields: */
#define WCD_USBSS_LUT_REG188_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG189 Fields: */
#define WCD_USBSS_LUT_REG189_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG190 Fields: */
#define WCD_USBSS_LUT_REG190_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG191 Fields: */
#define WCD_USBSS_LUT_REG191_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG192 Fields: */
#define WCD_USBSS_LUT_REG192_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG193 Fields: */
#define WCD_USBSS_LUT_REG193_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG194 Fields: */
#define WCD_USBSS_LUT_REG194_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG195 Fields: */
#define WCD_USBSS_LUT_REG195_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG196 Fields: */
#define WCD_USBSS_LUT_REG196_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG197 Fields: */
#define WCD_USBSS_LUT_REG197_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG198 Fields: */
#define WCD_USBSS_LUT_REG198_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG199 Fields: */
#define WCD_USBSS_LUT_REG199_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG200 Fields: */
#define WCD_USBSS_LUT_REG200_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG201 Fields: */
#define WCD_USBSS_LUT_REG201_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG202 Fields: */
#define WCD_USBSS_LUT_REG202_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG203 Fields: */
#define WCD_USBSS_LUT_REG203_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG204 Fields: */
#define WCD_USBSS_LUT_REG204_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG205 Fields: */
#define WCD_USBSS_LUT_REG205_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG206 Fields: */
#define WCD_USBSS_LUT_REG206_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG207 Fields: */
#define WCD_USBSS_LUT_REG207_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG208 Fields: */
#define WCD_USBSS_LUT_REG208_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG209 Fields: */
#define WCD_USBSS_LUT_REG209_REG_MASK                                    0xff

/* WCD_USBSS_LUT_REG210 Fields: */
#define WCD_USBSS_LUT_REG210_REG_MASK                                    0xff

/* WCD_USBSS_DATA_SEL Fields: */
#define WCD_USBSS_DATA_SEL_SEL3_MASK                                     0x30
#define WCD_USBSS_DATA_SEL_SEL2_MASK                                     0x0c
#define WCD_USBSS_DATA_SEL_SEL1_MASK                                     0x02
#define WCD_USBSS_DATA_SEL_SEL0_MASK                                     0x01

/* WCD_USBSS_OFF3 Fields: */
#define WCD_USBSS_OFF3_REG_MASK                                          0xff

/* WCD_USBSS_OFF2_LSB Fields: */
#define WCD_USBSS_OFF2_LSB_REG_MASK                                      0xff

/* WCD_USBSS_OFF2_MSB Fields: */
#define WCD_USBSS_OFF2_MSB_REG_MASK                                      0xff

/* WCD_USBSS_OFF1_LSB Fields: */
#define WCD_USBSS_OFF1_LSB_REG_MASK                                      0xff

/* WCD_USBSS_OFF1_MSB Fields: */
#define WCD_USBSS_OFF1_MSB_REG_MASK                                      0xff

/* WCD_USBSS_AUD_L Fields: */
#define WCD_USBSS_AUD_L_STATUS_MASK                                      0xff

/* WCD_USBSS_AUD_R Fields: */
#define WCD_USBSS_AUD_R_STATUS_MASK                                      0xff

/* WCD_USBSS_GND_L Fields: */
#define WCD_USBSS_GND_L_STATUS_MASK                                      0xff

/* WCD_USBSS_GND_R Fields: */
#define WCD_USBSS_GND_R_STATUS_MASK                                      0xff


/* WCD_USBSS_USB_DIG_PAGE Fields: */
#define WCD_USBSS_USB_DIG_PAGE_PAGE_REG_MASK                             0xff

/* WCD_USBSS_OVP_STATUS_SELF_CLEARING Fields: */
#define WCD_USBSS_OVP_STATUS_SELF_CLEARING_OVP_DPR_MASK                  0x80
#define WCD_USBSS_OVP_STATUS_SELF_CLEARING_OVP_DNL_MASK                  0x40
#define WCD_USBSS_OVP_STATUS_SELF_CLEARING_OVP_MG1_MASK                  0x20
#define WCD_USBSS_OVP_STATUS_SELF_CLEARING_OVP_MG2_MASK                  0x10

/* WCD_USBSS_OVP_STATUS Fields: */
#define WCD_USBSS_OVP_STATUS_OVP_DPR_MASK                                0x80
#define WCD_USBSS_OVP_STATUS_OVP_DNL_MASK                                0x40
#define WCD_USBSS_OVP_STATUS_OVP_MG1_MASK                                0x20
#define WCD_USBSS_OVP_STATUS_OVP_MG2_MASK                                0x10

/* WCD_USBSS_SWITCH_SETTINGS_ENABLE Fields: */
#define WCD_USBSS_SWITCH_SETTINGS_ENABLE_DEVICE_ENABLE_MASK              0x80
#define WCD_USBSS_SWITCH_SETTINGS_ENABLE_DP_AUXP_TO_MGX_SWITCHES_MASK    0x40
#define WCD_USBSS_SWITCH_SETTINGS_ENABLE_DP_AUXM_TO_MGX_SWITCHES_MASK    0x20
#define WCD_USBSS_SWITCH_SETTINGS_ENABLE_DNL_SWITCHES_MASK               0x10
#define WCD_USBSS_SWITCH_SETTINGS_ENABLE_DPR_SWITCHES_MASK               0x08
#define WCD_USBSS_SWITCH_SETTINGS_ENABLE_SENSE_SWITCHES_MASK             0x04
#define WCD_USBSS_SWITCH_SETTINGS_ENABLE_MIC_SWITCHES_MASK               0x02
#define WCD_USBSS_SWITCH_SETTINGS_ENABLE_AGND_SWITCHES_MASK              0x01

/* WCD_USBSS_SWITCH_SELECT0 Fields: */
#define WCD_USBSS_SWITCH_SELECT0_DP_AUXP_SWITCHES_MASK                   0x80
#define WCD_USBSS_SWITCH_SELECT0_DP_AUXM_SWITCHES_MASK                   0x40
#define WCD_USBSS_SWITCH_SELECT0_DNL_SWITCHES_MASK                       0x30
#define WCD_USBSS_SWITCH_SELECT0_DPR_SWITCHES_MASK                       0x0c
#define WCD_USBSS_SWITCH_SELECT0_SENSE_SWITCHES_MASK                     0x02
#define WCD_USBSS_SWITCH_SELECT0_MIC_SWITCHES_MASK                       0x01

/* WCD_USBSS_SWITCH_SELECT1 Fields: */
#define WCD_USBSS_SWITCH_SELECT1_AGND_SWITCHES_MASK                      0x01

/* WCD_USBSS_SWITCH_STATUS0 Fields: */
#define WCD_USBSS_SWITCH_STATUS0_SENSE_GSBU2_STATUS_MASK                 0x80
#define WCD_USBSS_SWITCH_STATUS0_SENSE_GSBU1_STATUS_MASK                 0x40
#define WCD_USBSS_SWITCH_STATUS0_L_DNL_STATUS_MASK                       0x20
#define WCD_USBSS_SWITCH_STATUS0_DN2_DNL_STATUS_MASK                     0x10
#define WCD_USBSS_SWITCH_STATUS0_DN_DNL_STATUS_MASK                      0x08
#define WCD_USBSS_SWITCH_STATUS0_R_DPR_STATUS_MASK                       0x04
#define WCD_USBSS_SWITCH_STATUS0_DP2_DPR_STATUS_MASK                     0x02
#define WCD_USBSS_SWITCH_STATUS0_DP_DPR_STATUS_MASK                      0x01

/* WCD_USBSS_SWITCH_STATUS1 Fields: */
#define WCD_USBSS_SWITCH_STATUS1_DPAUXP_MG2_STATUS_MASK                  0x80
#define WCD_USBSS_SWITCH_STATUS1_DPAUXM_MG2_STATUS_MASK                  0x40
#define WCD_USBSS_SWITCH_STATUS1_AGND_MG2_STATUS_MASK                    0x20
#define WCD_USBSS_SWITCH_STATUS1_MIC_MG2_STATUS_MASK                     0x10
#define WCD_USBSS_SWITCH_STATUS1_DPAUXP_MG1_STATUS_MASK                  0x08
#define WCD_USBSS_SWITCH_STATUS1_DPAUXM_MG1_STATUS_MASK                  0x04
#define WCD_USBSS_SWITCH_STATUS1_AGND_MG1_STATUS_MASK                    0x02
#define WCD_USBSS_SWITCH_STATUS1_MIC_MG1_STATUS_MASK                     0x01

/* WCD_USBSS_AUD_LEFT_SW_SLOW Fields: */
#define WCD_USBSS_AUD_LEFT_SW_SLOW_SWITCH_TURN_ON_RISE_TIME_MASK         0xff

/* WCD_USBSS_AUD_RIGHT_SW_SLOW Fields: */
#define WCD_USBSS_AUD_RIGHT_SW_SLOW_SWITCH_TURN_ON_RISE_TIME_MASK        0xff

/* WCD_USBSS_AUD_MIC_SW_SLOW Fields: */
#define WCD_USBSS_AUD_MIC_SW_SLOW_SWITCH_TURN_ON_RISE_TIME_MASK          0xff

/* WCD_USBSS_AUD_SENSE_SW_SLOW Fields: */
#define WCD_USBSS_AUD_SENSE_SW_SLOW_SWITCH_TURN_ON_RISE_TIME_MASK        0xff

/* WCD_USBSS_AUD_GND_SW_SLOW Fields: */
#define WCD_USBSS_AUD_GND_SW_SLOW_SWITCH_TURN_ON_RISE_TIME_MASK          0xff

/* WCD_USBSS_DELAY_R_SW Fields: */
#define WCD_USBSS_DELAY_R_SW_DELAY_TIME_MASK                             0xff

/* WCD_USBSS_DELAY_MIC_SW Fields: */
#define WCD_USBSS_DELAY_MIC_SW_DELAY_TIME_MASK                           0xff

/* WCD_USBSS_DELAY_SENSE_SW Fields: */
#define WCD_USBSS_DELAY_SENSE_SW_DELAY_TIME_MASK                         0xff

/* WCD_USBSS_DELAY_GND_SW Fields: */
#define WCD_USBSS_DELAY_GND_SW_DELAY_TIME_MASK                           0xff

/* WCD_USBSS_DELAY_L_SW Fields: */
#define WCD_USBSS_DELAY_L_SW_DELAY_TIME_MASK                             0xff

/* WCD_USBSS_EXT_FET_ENABLE_DELAY Fields: */
#define WCD_USBSS_EXT_FET_ENABLE_DELAY_DELAY_TIME_MASK                   0xff

/* WCD_USBSS_FUNCTION_ENABLE Fields: */
#define WCD_USBSS_FUNCTION_ENABLE_SEL_CSR_SLEEP_BG_PROG_MASK             0x80
#define WCD_USBSS_FUNCTION_ENABLE_PER_PIN_OVP_ENABLE_MASK                0x40
#define WCD_USBSS_FUNCTION_ENABLE_SW_OVP_ENABLE_MASK                     0x20
#define WCD_USBSS_FUNCTION_ENABLE_SW_CONNECTION_DISABLE_MASK             0x10
#define WCD_USBSS_FUNCTION_ENABLE_SLOW_TURN_ON_ENABLE_MASK               0x08
#define WCD_USBSS_FUNCTION_ENABLE_RDAC_CAL_CODE_SELECT_MASK              0x04
#define WCD_USBSS_FUNCTION_ENABLE_SWITCH_SOURCE_SELECT_MASK              0x03

/* WCD_USBSS_USB_RST_CTL Fields: */
#define WCD_USBSS_USB_RST_CTL_USB_ANA_SW_RST_N_MASK                      0x02
#define WCD_USBSS_USB_RST_CTL_USB_DIG_SW_RST_N_MASK                      0x01

/* WCD_USBSS_EQUALIZER1 Fields: */
#define WCD_USBSS_EQUALIZER1_EQ_EN_MASK                                  0x80
#define WCD_USBSS_EQUALIZER1_BW_SETTINGS_MASK                            0x78

/* WCD_USBSS_SPARE_0 Fields: */
#define WCD_USBSS_SPARE_0_SPARE_BITS_MASK                                0xff

/* WCD_USBSS_DIG_FUNCTIONS_STATUS Fields: */
#define WCD_USBSS_DIG_FUNCTIONS_STATUS_EQ_ENABLE_STATUS_MASK             0x40
#define WCD_USBSS_DIG_FUNCTIONS_STATUS_OVP_DNL_ENABLE_STATUS_MASK        0x20
#define WCD_USBSS_DIG_FUNCTIONS_STATUS_OVP_DPR_ENABLE_STATUS_MASK        0x10
#define WCD_USBSS_DIG_FUNCTIONS_STATUS_OVP_MG12_ENABLE_STATUS_MASK       0x08
#define WCD_USBSS_DIG_FUNCTIONS_STATUS_EXTFET_DNL_ENABLE_STATUS_MASK     0x04
#define WCD_USBSS_DIG_FUNCTIONS_STATUS_EXTFET_DPR_ENABLE_STATUS_MASK     0x02
#define WCD_USBSS_DIG_FUNCTIONS_STATUS_EXTFET_MG12_ENABLE_STATUS_MASK    0x01

/* WCD_USBSS_CLK_SOURCE Fields: */
#define WCD_USBSS_CLK_SOURCE_CP_CLK_SEL_MASK                             0xc0
#define WCD_USBSS_CLK_SOURCE_FSM_CLK_SEL_MASK                            0x20

/* WCD_USBSS_USB_SS_CNTL Fields: */
#define WCD_USBSS_USB_SS_CNTL_STANDBY_STATE_MASK                         0x10
#define WCD_USBSS_USB_SS_CNTL_RCO_EN_MASK                                0x08
#define WCD_USBSS_USB_SS_CNTL_USB_SS_MODE_MASK                           0x07

/* WCD_USBSS_SPARE_1 Fields: */
#define WCD_USBSS_SPARE_1_SPARE_BITS_MASK                                0xff

/* WCD_USBSS_ANA_FUNCTIONS_STATUS Fields: */
#define WCD_USBSS_ANA_FUNCTIONS_STATUS_CP_READY_MASK                     0x80

/* WCD_USBSS_FSM_STATUS Fields: */
#define WCD_USBSS_FSM_STATUS_LINEARIZER_FSM_DONE_MASK                    0x02
#define WCD_USBSS_FSM_STATUS_SWITCH_FSM_DONE_MASK                        0x01

/* WCD_USBSS_SPARE_14 Fields: */
#define WCD_USBSS_SPARE_14_SPARE_BITS_MASK                               0xff

/* WCD_USBSS_SAFE_STATE_PD_DPAUX Fields: */
#define WCD_USBSS_SAFE_STATE_PD_DPAUX_DP_SBU1_SAFE_STATE_PD_MASK         0x0c
#define WCD_USBSS_SAFE_STATE_PD_DPAUX_DP_SBU2_SAFE_STATE_PD_MASK         0x03

/* WCD_USBSS_AUDIO_FSM_START Fields: */
#define WCD_USBSS_AUDIO_FSM_START_AUDIO_FSM_AUDIO_TRIG_MASK              0x01

/* WCD_USBSS_FSM_RESET Fields: */
#define WCD_USBSS_FSM_RESET_AUDIO_FSM_LIN_RESET_MASK                     0x02
#define WCD_USBSS_FSM_RESET_AUDIO_FSM_SWITCH_RESET_MASK                  0x01

/* WCD_USBSS_CHIP_ID0 Fields: */
#define WCD_USBSS_CHIP_ID0_BYTE_0_MASK                                   0xff

/* WCD_USBSS_CHIP_ID1 Fields: */
#define WCD_USBSS_CHIP_ID1_BYTE_1_MASK                                   0xff

/* WCD_USBSS_CHIP_ID2 Fields: */
#define WCD_USBSS_CHIP_ID2_BYTE_2_MASK                                   0xff

/* WCD_USBSS_CHIP_ID3 Fields: */
#define WCD_USBSS_CHIP_ID3_BYTE_3_MASK                                   0xff

/* WCD_USBSS_LINEARIZER_CFG Fields: */
#define WCD_USBSS_LINEARIZER_CFG_COEF_CFG_SEL_MASK                       0x01

/* WCD_USBSS_RATIO_SPKR_REXT_L_LSB Fields: */
#define WCD_USBSS_RATIO_SPKR_REXT_L_LSB_RATIO_L_LSB_MASK                 0xff

/* WCD_USBSS_RATIO_SPKR_REXT_L_MSB Fields: */
#define WCD_USBSS_RATIO_SPKR_REXT_L_MSB_RATIO_L_MSB_MASK                 0x7f

/* WCD_USBSS_RATIO_SPKR_REXT_R_LSB Fields: */
#define WCD_USBSS_RATIO_SPKR_REXT_R_LSB_RATIO_R_LSB_MASK                 0xff

/* WCD_USBSS_RATIO_SPKR_REXT_R_MSB Fields: */
#define WCD_USBSS_RATIO_SPKR_REXT_R_MSB_RATIO_R_MSB_MASK                 0x7f

/* WCD_USBSS_SW_TAP_AUD_L_LSB Fields: */
#define WCD_USBSS_SW_TAP_AUD_L_LSB_TCAL1_SW_LEFT_LSB_MASK                0xff

/* WCD_USBSS_SW_TAP_AUD_L_MSB Fields: */
#define WCD_USBSS_SW_TAP_AUD_L_MSB_TCAL1_SW_LEFT_MSB_MASK                0x03

/* WCD_USBSS_SW_TAP_AUD_R_LSB Fields: */
#define WCD_USBSS_SW_TAP_AUD_R_LSB_TCAL2_SW_RIGHT_LSB_MASK               0xff

/* WCD_USBSS_SW_TAP_AUD_R_MSB Fields: */
#define WCD_USBSS_SW_TAP_AUD_R_MSB_TCAL2_SW_RIGHT_MSB_MASK               0x03

/* WCD_USBSS_SW_TAP_GND_L_LSB Fields: */
#define WCD_USBSS_SW_TAP_GND_L_LSB_TCAL3_SW_GND1_LSB_MASK                0xff

/* WCD_USBSS_SW_TAP_GND_L_MSB Fields: */
#define WCD_USBSS_SW_TAP_GND_L_MSB_TCAL3_SW_GND1_MSB_MASK                0x03

/* WCD_USBSS_SW_TAP_GND_R_LSB Fields: */
#define WCD_USBSS_SW_TAP_GND_R_LSB_TCAL4_SW_GND2_LSB_MASK                0xff

/* WCD_USBSS_SW_TAP_GND_R_MSB Fields: */
#define WCD_USBSS_SW_TAP_GND_R_MSB_TCAL4_SW_GND2_MSB_MASK                0x03

/* WCD_USBSS_HW_TAP_AUD_L_LSB Fields: */
#define WCD_USBSS_HW_TAP_AUD_L_LSB_TCAL1_HW_LEFT_LSB_MASK                0xff

/* WCD_USBSS_HW_TAP_AUD_L_MSB Fields: */
#define WCD_USBSS_HW_TAP_AUD_L_MSB_TCAL1_HW_LEFT_MSB_MASK                0x0f

/* WCD_USBSS_HW_TAP_AUD_R_LSB Fields: */
#define WCD_USBSS_HW_TAP_AUD_R_LSB_TCAL2_HW_RIGHT_LSB_MASK               0xff

/* WCD_USBSS_HW_TAP_AUD_R_MSB Fields: */
#define WCD_USBSS_HW_TAP_AUD_R_MSB_TCAL2_HW_RIGHT_MSB_MASK               0x0f

/* WCD_USBSS_HW_TAP_GND_L_LSB Fields: */
#define WCD_USBSS_HW_TAP_GND_L_LSB_TCAL3_HW_GND1_LSB_MASK                0xff

/* WCD_USBSS_HW_TAP_GND_L_MSB Fields: */
#define WCD_USBSS_HW_TAP_GND_L_MSB_TCAL3_HW_GND1_MSB_MASK                0x0f

/* WCD_USBSS_HW_TAP_GND_R_LSB Fields: */
#define WCD_USBSS_HW_TAP_GND_R_LSB_TCAL4_HW_GND2_LSB_MASK                0xff

/* WCD_USBSS_HW_TAP_GND_R_MSB Fields: */
#define WCD_USBSS_HW_TAP_GND_R_MSB_TCAL4_HW_GND2_MSB_MASK                0x0f

/* WCD_USBSS_AUD_COEF_L_K0_0 Fields: */
#define WCD_USBSS_AUD_COEF_L_K0_0_K0_0_MASK                              0xff

/* WCD_USBSS_AUD_COEF_L_K0_1 Fields: */
#define WCD_USBSS_AUD_COEF_L_K0_1_K0_1_MASK                              0xff

/* WCD_USBSS_AUD_COEF_L_K0_2 Fields: */
#define WCD_USBSS_AUD_COEF_L_K0_2_K0_2_MASK                              0x3f

/* WCD_USBSS_AUD_COEF_L_K1_0 Fields: */
#define WCD_USBSS_AUD_COEF_L_K1_0_K1_0_MASK                              0xff

/* WCD_USBSS_AUD_COEF_L_K1_1 Fields: */
#define WCD_USBSS_AUD_COEF_L_K1_1_K1_1_MASK                              0xff

/* WCD_USBSS_AUD_COEF_L_K2_0 Fields: */
#define WCD_USBSS_AUD_COEF_L_K2_0_K2_0_MASK                              0xff

/* WCD_USBSS_AUD_COEF_L_K2_1 Fields: */
#define WCD_USBSS_AUD_COEF_L_K2_1_K2_1_MASK                              0xff

/* WCD_USBSS_AUD_COEF_L_K3_0 Fields: */
#define WCD_USBSS_AUD_COEF_L_K3_0_K3_0_MASK                              0xff

/* WCD_USBSS_AUD_COEF_L_K3_1 Fields: */
#define WCD_USBSS_AUD_COEF_L_K3_1_K3_1_MASK                              0xff

/* WCD_USBSS_AUD_COEF_L_K4_0 Fields: */
#define WCD_USBSS_AUD_COEF_L_K4_0_K4_0_MASK                              0xff

/* WCD_USBSS_AUD_COEF_L_K4_1 Fields: */
#define WCD_USBSS_AUD_COEF_L_K4_1_K4_1_MASK                              0xff

/* WCD_USBSS_AUD_COEF_L_K5_0 Fields: */
#define WCD_USBSS_AUD_COEF_L_K5_0_K5_0_MASK                              0xff

/* WCD_USBSS_AUD_COEF_L_K5_1 Fields: */
#define WCD_USBSS_AUD_COEF_L_K5_1_K5_1_MASK                              0xff

/* WCD_USBSS_AUD_COEF_R_K0_0 Fields: */
#define WCD_USBSS_AUD_COEF_R_K0_0_K0_0_MASK                              0xff

/* WCD_USBSS_AUD_COEF_R_K0_1 Fields: */
#define WCD_USBSS_AUD_COEF_R_K0_1_K0_1_MASK                              0xff

/* WCD_USBSS_AUD_COEF_R_K0_2 Fields: */
#define WCD_USBSS_AUD_COEF_R_K0_2_K0_2_MASK                              0x3f

/* WCD_USBSS_AUD_COEF_R_K1_0 Fields: */
#define WCD_USBSS_AUD_COEF_R_K1_0_K1_0_MASK                              0xff

/* WCD_USBSS_AUD_COEF_R_K1_1 Fields: */
#define WCD_USBSS_AUD_COEF_R_K1_1_K1_1_MASK                              0xff

/* WCD_USBSS_AUD_COEF_R_K2_0 Fields: */
#define WCD_USBSS_AUD_COEF_R_K2_0_K2_0_MASK                              0xff

/* WCD_USBSS_AUD_COEF_R_K2_1 Fields: */
#define WCD_USBSS_AUD_COEF_R_K2_1_K2_1_MASK                              0xff

/* WCD_USBSS_AUD_COEF_R_K3_0 Fields: */
#define WCD_USBSS_AUD_COEF_R_K3_0_K3_0_MASK                              0xff

/* WCD_USBSS_AUD_COEF_R_K3_1 Fields: */
#define WCD_USBSS_AUD_COEF_R_K3_1_K3_1_MASK                              0xff

/* WCD_USBSS_AUD_COEF_R_K4_0 Fields: */
#define WCD_USBSS_AUD_COEF_R_K4_0_K4_0_MASK                              0xff

/* WCD_USBSS_AUD_COEF_R_K4_1 Fields: */
#define WCD_USBSS_AUD_COEF_R_K4_1_K4_1_MASK                              0xff

/* WCD_USBSS_AUD_COEF_R_K5_0 Fields: */
#define WCD_USBSS_AUD_COEF_R_K5_0_K5_0_MASK                              0xff

/* WCD_USBSS_AUD_COEF_R_K5_1 Fields: */
#define WCD_USBSS_AUD_COEF_R_K5_1_K5_1_MASK                              0xff

/* WCD_USBSS_GND_COEF_L_K0_0 Fields: */
#define WCD_USBSS_GND_COEF_L_K0_0_K0_0_MASK                              0xff

/* WCD_USBSS_GND_COEF_L_K0_1 Fields: */
#define WCD_USBSS_GND_COEF_L_K0_1_K0_1_MASK                              0xff

/* WCD_USBSS_GND_COEF_L_K0_2 Fields: */
#define WCD_USBSS_GND_COEF_L_K0_2_K0_2_MASK                              0x3f

/* WCD_USBSS_GND_COEF_L_K1_0 Fields: */
#define WCD_USBSS_GND_COEF_L_K1_0_K1_0_MASK                              0xff

/* WCD_USBSS_GND_COEF_L_K1_1 Fields: */
#define WCD_USBSS_GND_COEF_L_K1_1_K1_1_MASK                              0xff

/* WCD_USBSS_GND_COEF_L_K2_0 Fields: */
#define WCD_USBSS_GND_COEF_L_K2_0_K2_0_MASK                              0xff

/* WCD_USBSS_GND_COEF_L_K2_1 Fields: */
#define WCD_USBSS_GND_COEF_L_K2_1_K2_1_MASK                              0xff

/* WCD_USBSS_GND_COEF_L_K3_0 Fields: */
#define WCD_USBSS_GND_COEF_L_K3_0_K3_0_MASK                              0xff

/* WCD_USBSS_GND_COEF_L_K3_1 Fields: */
#define WCD_USBSS_GND_COEF_L_K3_1_K3_1_MASK                              0xff

/* WCD_USBSS_GND_COEF_L_K4_0 Fields: */
#define WCD_USBSS_GND_COEF_L_K4_0_K4_0_MASK                              0xff

/* WCD_USBSS_GND_COEF_L_K4_1 Fields: */
#define WCD_USBSS_GND_COEF_L_K4_1_K4_1_MASK                              0xff

/* WCD_USBSS_GND_COEF_L_K5_0 Fields: */
#define WCD_USBSS_GND_COEF_L_K5_0_K5_0_MASK                              0xff

/* WCD_USBSS_GND_COEF_L_K5_1 Fields: */
#define WCD_USBSS_GND_COEF_L_K5_1_K5_1_MASK                              0xff

/* WCD_USBSS_GND_COEF_R_K0_0 Fields: */
#define WCD_USBSS_GND_COEF_R_K0_0_K0_0_MASK                              0xff

/* WCD_USBSS_GND_COEF_R_K0_1 Fields: */
#define WCD_USBSS_GND_COEF_R_K0_1_K0_1_MASK                              0xff

/* WCD_USBSS_GND_COEF_R_K0_2 Fields: */
#define WCD_USBSS_GND_COEF_R_K0_2_K0_2_MASK                              0x3f

/* WCD_USBSS_GND_COEF_R_K1_0 Fields: */
#define WCD_USBSS_GND_COEF_R_K1_0_K1_0_MASK                              0xff

/* WCD_USBSS_GND_COEF_R_K1_1 Fields: */
#define WCD_USBSS_GND_COEF_R_K1_1_K1_1_MASK                              0xff

/* WCD_USBSS_GND_COEF_R_K2_0 Fields: */
#define WCD_USBSS_GND_COEF_R_K2_0_K2_0_MASK                              0xff

/* WCD_USBSS_GND_COEF_R_K2_1 Fields: */
#define WCD_USBSS_GND_COEF_R_K2_1_K2_1_MASK                              0xff

/* WCD_USBSS_GND_COEF_R_K3_0 Fields: */
#define WCD_USBSS_GND_COEF_R_K3_0_K3_0_MASK                              0xff

/* WCD_USBSS_GND_COEF_R_K3_1 Fields: */
#define WCD_USBSS_GND_COEF_R_K3_1_K3_1_MASK                              0xff

/* WCD_USBSS_GND_COEF_R_K4_0 Fields: */
#define WCD_USBSS_GND_COEF_R_K4_0_K4_0_MASK                              0xff

/* WCD_USBSS_GND_COEF_R_K4_1 Fields: */
#define WCD_USBSS_GND_COEF_R_K4_1_K4_1_MASK                              0xff

/* WCD_USBSS_GND_COEF_R_K5_0 Fields: */
#define WCD_USBSS_GND_COEF_R_K5_0_K5_0_MASK                              0xff

/* WCD_USBSS_GND_COEF_R_K5_1 Fields: */
#define WCD_USBSS_GND_COEF_R_K5_1_K5_1_MASK                              0xff

/* WCD_USBSS_AUD_L_SLOPE_SCALE_LSB Fields: */
#define WCD_USBSS_AUD_L_SLOPE_SCALE_LSB_SLOPE_LSB_MASK                   0xff

/* WCD_USBSS_AUD_L_SLOPE_SCALE_MSB Fields: */
#define WCD_USBSS_AUD_L_SLOPE_SCALE_MSB_SLOPE_MSB_MASK                   0x0f

/* WCD_USBSS_AUD_R_SLOPE_SCALE_LSB Fields: */
#define WCD_USBSS_AUD_R_SLOPE_SCALE_LSB_SLOPE_LSB_MASK                   0xff

/* WCD_USBSS_AUD_R_SLOPE_SCALE_MSB Fields: */
#define WCD_USBSS_AUD_R_SLOPE_SCALE_MSB_SLOPE_MSB_MASK                   0x0f

/* WCD_USBSS_GND_L_SLOPE_SCALE_LSB Fields: */
#define WCD_USBSS_GND_L_SLOPE_SCALE_LSB_SLOPE_LSB_MASK                   0xff

/* WCD_USBSS_GND_L_SLOPE_SCALE_MSB Fields: */
#define WCD_USBSS_GND_L_SLOPE_SCALE_MSB_SLOPE_MSB_MASK                   0x0f

/* WCD_USBSS_GND_R_SLOPE_SCALE_LSB Fields: */
#define WCD_USBSS_GND_R_SLOPE_SCALE_LSB_SLOPE_LSB_MASK                   0xff

/* WCD_USBSS_GND_R_SLOPE_SCALE_MSB Fields: */
#define WCD_USBSS_GND_R_SLOPE_SCALE_MSB_SLOPE_MSB_MASK                   0x0f

/* WCD_USBSS_AUD_L_FIRST_TAP Fields: */
#define WCD_USBSS_AUD_L_FIRST_TAP_FIRST_TAP_MASK                         0xff

/* WCD_USBSS_AUD_R_FIRST_TAP Fields: */
#define WCD_USBSS_AUD_R_FIRST_TAP_FIRST_TAP_MASK                         0xff

/* WCD_USBSS_GND_L_FIRST_TAP Fields: */
#define WCD_USBSS_GND_L_FIRST_TAP_FIRST_TAP_MASK                         0xff

/* WCD_USBSS_GND_R_FIRST_TAP Fields: */
#define WCD_USBSS_GND_R_FIRST_TAP_FIRST_TAP_MASK                         0xff

/* WCD_USBSS_FEATURE_SELECTION Fields: */
#define WCD_USBSS_FEATURE_SELECTION_FEATURE_SEL_MASK                     0xf0

/* WCD_USBSS_EFUSE_REG_0 Fields: */
#define WCD_USBSS_EFUSE_REG_0_EFUSE_MUX_SEL_MASK                         0x20
#define WCD_USBSS_EFUSE_REG_0_RCO_RATE_MASK                              0x10
#define WCD_USBSS_EFUSE_REG_0_LDOL_TRIM_MASK                             0x0f

/* WCD_USBSS_EFUSE_REG_1 Fields: */
#define WCD_USBSS_EFUSE_REG_1_OVP_REFERENCE_TRIM_MASK                    0xf0
#define WCD_USBSS_EFUSE_REG_1_CP_OUTPUT_TRIM_MASK                        0x0f

/* WCD_USBSS_EFUSE_REG_2 Fields: */
#define WCD_USBSS_EFUSE_REG_2_EQ_ENABLE_MASK                             0x40

/* WCD_USBSS_EFUSE_REG_3 Fields: */
#define WCD_USBSS_EFUSE_REG_3_DPAUXP_MG1_MASK                            0x80
#define WCD_USBSS_EFUSE_REG_3_DPAUXP_MG2_MASK                            0x40
#define WCD_USBSS_EFUSE_REG_3_DPAUXM_MG1_MASK                            0x20
#define WCD_USBSS_EFUSE_REG_3_DPAUXM_MG2_MASK                            0x10
#define WCD_USBSS_EFUSE_REG_3_DNL_L_MASK                                 0x08
#define WCD_USBSS_EFUSE_REG_3_DNL_DN_MASK                                0x04
#define WCD_USBSS_EFUSE_REG_3_DNL_DN2_MASK                               0x02
#define WCD_USBSS_EFUSE_REG_3_DPR_R_MASK                                 0x01

/* WCD_USBSS_EFUSE_REG_4 Fields: */
#define WCD_USBSS_EFUSE_REG_4_DPR_DP_MASK                                0x80
#define WCD_USBSS_EFUSE_REG_4_DPR_DP2_MASK                               0x40
#define WCD_USBSS_EFUSE_REG_4_SENSE_GSBU1_MASK                           0x20
#define WCD_USBSS_EFUSE_REG_4_SENSE_GSBU2_MASK                           0x10
#define WCD_USBSS_EFUSE_REG_4_MIC_MG1_MASK                               0x08
#define WCD_USBSS_EFUSE_REG_4_MIC_MG2_MASK                               0x04
#define WCD_USBSS_EFUSE_REG_4_AGND_MG1_MASK                              0x02
#define WCD_USBSS_EFUSE_REG_4_AGND_MG2_MASK                              0x01

/* WCD_USBSS_EFUSE_REG_5 Fields: */
#define WCD_USBSS_EFUSE_REG_5_OVP_THRESHOLD_MASK                         0xc0
#define WCD_USBSS_EFUSE_REG_5_EQ_SEG_SEL_MASK                            0x30
#define WCD_USBSS_EFUSE_REG_5_EQ_ENABLE_MASK                             0x08
#define WCD_USBSS_EFUSE_REG_5_CP_PFM_EN_MASK                             0x04
#define WCD_USBSS_EFUSE_REG_5_SPARE_BITS_MASK                            0x03

/* WCD_USBSS_EFUSE_REG_6 Fields: */
#define WCD_USBSS_EFUSE_REG_6_SPARE_BITS_MASK                            0xff

/* WCD_USBSS_EFUSE_REG_7 Fields: */
#define WCD_USBSS_EFUSE_REG_7_SPARE_BITS_MASK                            0xf8
#define WCD_USBSS_EFUSE_REG_7_LDOL_TRIM_MASK                             0x07

/* WCD_USBSS_EFUSE_REG_8 Fields: */
#define WCD_USBSS_EFUSE_REG_8_GNDSW_L_GAIN_TRIM_MASK                     0xf0
#define WCD_USBSS_EFUSE_REG_8_GNDSW_R_GAIN_TRIM_MASK                     0x0f

/* WCD_USBSS_EFUSE_REG_9 Fields: */
#define WCD_USBSS_EFUSE_REG_9_AUDSW_L_GAIN_TRIM_MASK                     0xff

/* WCD_USBSS_EFUSE_REG_10 Fields: */
#define WCD_USBSS_EFUSE_REG_10_AUDSW_R_GAIN_TRIM_MASK                    0xff

/* WCD_USBSS_EFUSE_REG_11 Fields: */
#define WCD_USBSS_EFUSE_REG_11_SPARE_BITS_MASK                           0xe0
#define WCD_USBSS_EFUSE_REG_11_GNDSW_L_OFFSET_TRIM_MASK                  0x1f

/* WCD_USBSS_EFUSE_REG_12 Fields: */
#define WCD_USBSS_EFUSE_REG_12_SPARE_BITS_MASK                           0xe0
#define WCD_USBSS_EFUSE_REG_12_GNDSW_R_OFFSET_TRIM_MASK                  0x1f

/* WCD_USBSS_EFUSE_REG_13 Fields: */
#define WCD_USBSS_EFUSE_REG_13_SPARE_BITS_MASK                           0xe0
#define WCD_USBSS_EFUSE_REG_13_AUDSW_L_OFFSET_TRIM_MASK                  0x1f

/* WCD_USBSS_EFUSE_REG_14 Fields: */
#define WCD_USBSS_EFUSE_REG_14_SPARE_BITS_MASK                           0xe0
#define WCD_USBSS_EFUSE_REG_14_AUDSW_R_OFFSET_TRIM_MASK                  0x1f

/* WCD_USBSS_EFUSE_REG_15 Fields: */
#define WCD_USBSS_EFUSE_REG_15_SPARE_BITS_MASK                           0xff

/* WCD_USBSS_EFUSE_PRG_CTL Fields: */
#define WCD_USBSS_EFUSE_PRG_CTL_PRG_ADDR_MASK                            0xff


#endif /* WCD_USBSS_REG_MASKS_H */
