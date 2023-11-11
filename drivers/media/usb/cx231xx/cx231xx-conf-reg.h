/* SPDX-License-Identifier: GPL-2.0-or-later */
/*
   cx231xx_conf-reg.h - driver for Conexant Cx23100/101/102 USB
			video capture devices

   Copyright (C) 2008 <srinivasa.deevi at conexant dot com>

 */

#ifndef _POLARIS_REG_H_
#define _POLARIS_REG_H_

#define BOARD_CFG_STAT          0x0
#define TS_MODE_REG             0x4
#define TS1_CFG_REG             0x8
#define TS1_LENGTH_REG          0xc
#define TS2_CFG_REG             0x10
#define TS2_LENGTH_REG          0x14
#define EP_MODE_SET             0x18
#define CIR_PWR_PTN1            0x1c
#define CIR_PWR_PTN2            0x20
#define CIR_PWR_PTN3            0x24
#define CIR_PWR_MASK0           0x28
#define CIR_PWR_MASK1           0x2c
#define CIR_PWR_MASK2           0x30
#define CIR_GAIN                0x34
#define CIR_CAR_REG             0x38
#define CIR_OT_CFG1             0x40
#define CIR_OT_CFG2             0x44
#define GBULK_BIT_EN            0x68
#define PWR_CTL_EN              0x74

/* Polaris Endpoints capture mask for register EP_MODE_SET */
#define ENABLE_EP1              0x01   /* Bit[0]=1 */
#define ENABLE_EP2              0x02   /* Bit[1]=1 */
#define ENABLE_EP3              0x04   /* Bit[2]=1 */
#define ENABLE_EP4              0x08   /* Bit[3]=1 */
#define ENABLE_EP5              0x10   /* Bit[4]=1 */
#define ENABLE_EP6              0x20   /* Bit[5]=1 */

/* Bit definition for register PWR_CTL_EN */
#define PWR_MODE_MASK           0x17f
#define PWR_AV_EN               0x08   /* bit3 */
#define PWR_ISO_EN              0x40   /* bit6 */
#define PWR_AV_MODE             0x30   /* bit4,5  */
#define PWR_TUNER_EN            0x04   /* bit2 */
#define PWR_DEMOD_EN            0x02   /* bit1 */
#define I2C_DEMOD_EN            0x01   /* bit0 */
#define PWR_RESETOUT_EN         0x100  /* bit8 */

enum AV_MODE{
	POLARIS_AVMODE_DEFAULT = 0,
	POLARIS_AVMODE_DIGITAL = 0x10,
	POLARIS_AVMODE_ANALOGT_TV = 0x20,
	POLARIS_AVMODE_ENXTERNAL_AV = 0x30,

};

/* Colibri Registers */

#define SINGLE_ENDED            0x0
#define LOW_IF                  0x4
#define EU_IF                   0x9
#define US_IF                   0xa

#define SUP_BLK_TUNE1           0x00
#define SUP_BLK_TUNE2           0x01
#define SUP_BLK_TUNE3           0x02
#define SUP_BLK_XTAL            0x03
#define SUP_BLK_PLL1            0x04
#define SUP_BLK_PLL2            0x05
#define SUP_BLK_PLL3            0x06
#define SUP_BLK_REF             0x07
#define SUP_BLK_PWRDN           0x08
#define SUP_BLK_TESTPAD         0x09
#define ADC_COM_INT5_STAB_REF   0x0a
#define ADC_COM_QUANT           0x0b
#define ADC_COM_BIAS1           0x0c
#define ADC_COM_BIAS2           0x0d
#define ADC_COM_BIAS3           0x0e
#define TESTBUS_CTRL            0x12

#define FLD_PWRDN_TUNING_BIAS	0x10
#define FLD_PWRDN_ENABLE_PLL	0x08
#define FLD_PWRDN_PD_BANDGAP	0x04
#define FLD_PWRDN_PD_BIAS	0x02
#define FLD_PWRDN_PD_TUNECK	0x01


#define ADC_STATUS_CH1          0x20
#define ADC_STATUS_CH2          0x40
#define ADC_STATUS_CH3          0x60

#define ADC_STATUS2_CH1         0x21
#define ADC_STATUS2_CH2         0x41
#define ADC_STATUS2_CH3         0x61

#define ADC_CAL_ATEST_CH1       0x22
#define ADC_CAL_ATEST_CH2       0x42
#define ADC_CAL_ATEST_CH3       0x62

#define ADC_PWRDN_CLAMP_CH1     0x23
#define ADC_PWRDN_CLAMP_CH2     0x43
#define ADC_PWRDN_CLAMP_CH3     0x63

#define ADC_CTRL_DAC23_CH1      0x24
#define ADC_CTRL_DAC23_CH2      0x44
#define ADC_CTRL_DAC23_CH3      0x64

#define ADC_CTRL_DAC1_CH1       0x25
#define ADC_CTRL_DAC1_CH2       0x45
#define ADC_CTRL_DAC1_CH3       0x65

#define ADC_DCSERVO_DEM_CH1     0x26
#define ADC_DCSERVO_DEM_CH2     0x46
#define ADC_DCSERVO_DEM_CH3     0x66

#define ADC_FB_FRCRST_CH1       0x27
#define ADC_FB_FRCRST_CH2       0x47
#define ADC_FB_FRCRST_CH3       0x67

#define ADC_INPUT_CH1           0x28
#define ADC_INPUT_CH2           0x48
#define ADC_INPUT_CH3           0x68
#define INPUT_SEL_MASK          0x30   /* [5:4] in_sel */

#define ADC_NTF_PRECLMP_EN_CH1  0x29
#define ADC_NTF_PRECLMP_EN_CH2  0x49
#define ADC_NTF_PRECLMP_EN_CH3  0x69

#define ADC_QGAIN_RES_TRM_CH1   0x2a
#define ADC_QGAIN_RES_TRM_CH2   0x4a
#define ADC_QGAIN_RES_TRM_CH3   0x6a

#define ADC_SOC_PRECLMP_TERM_CH1    0x2b
#define ADC_SOC_PRECLMP_TERM_CH2    0x4b
#define ADC_SOC_PRECLMP_TERM_CH3    0x6b

#define TESTBUS_CTRL_CH1        0x32
#define TESTBUS_CTRL_CH2        0x52
#define TESTBUS_CTRL_CH3        0x72

/******************************************************************************
			    * DIF registers *
 ******************************************************************************/
#define      DIRECT_IF_REVB_BASE  0x00300

/*****************************************************************************/
#define      DIF_PLL_FREQ_WORD        (DIRECT_IF_REVB_BASE + 0x00000000)
/*****************************************************************************/
#define      FLD_DIF_PLL_LOCK                           0x80000000
/*  Reserved                                [30:29] */
#define      FLD_DIF_PLL_FREE_RUN                       0x10000000
#define      FLD_DIF_PLL_FREQ                           0x0fffffff

/*****************************************************************************/
#define      DIF_PLL_CTRL             (DIRECT_IF_REVB_BASE + 0x00000004)
/*****************************************************************************/
#define      FLD_DIF_KD_PD                              0xff000000
/*  Reserved                             [23:20] */
#define      FLD_DIF_KDS_PD                             0x000f0000
#define      FLD_DIF_KI_PD                              0x0000ff00
/*  Reserved                             [7:4] */
#define      FLD_DIF_KIS_PD                             0x0000000f

/*****************************************************************************/
#define      DIF_PLL_CTRL1            (DIRECT_IF_REVB_BASE + 0x00000008)
/*****************************************************************************/
#define      FLD_DIF_KD_FD                              0xff000000
/*  Reserved                             [23:20] */
#define      FLD_DIF_KDS_FD                             0x000f0000
#define      FLD_DIF_KI_FD                              0x0000ff00
#define      FLD_DIF_SIG_PROP_SZ                        0x000000f0
#define      FLD_DIF_KIS_FD                             0x0000000f

/*****************************************************************************/
#define      DIF_PLL_CTRL2            (DIRECT_IF_REVB_BASE + 0x0000000c)
/*****************************************************************************/
#define      FLD_DIF_PLL_AGC_REF                        0xfff00000
#define      FLD_DIF_PLL_AGC_KI                         0x000f0000
/*  Reserved                             [15] */
#define      FLD_DIF_FREQ_LIMIT                         0x00007000
#define      FLD_DIF_K_FD                               0x00000f00
#define      FLD_DIF_DOWNSMPL_FD                        0x000000ff

/*****************************************************************************/
#define      DIF_PLL_CTRL3            (DIRECT_IF_REVB_BASE + 0x00000010)
/*****************************************************************************/
/*  Reserved                             [31:16] */
#define      FLD_DIF_PLL_AGC_EN                         0x00008000
/*  Reserved                             [14:12] */
#define      FLD_DIF_PLL_MAN_GAIN                       0x00000fff

/*****************************************************************************/
#define      DIF_AGC_IF_REF           (DIRECT_IF_REVB_BASE + 0x00000014)
/*****************************************************************************/
#define      FLD_DIF_K_AGC_RF                           0xf0000000
#define      FLD_DIF_K_AGC_IF                           0x0f000000
#define      FLD_DIF_K_AGC_INT                          0x00f00000
/*  Reserved                             [19:12] */
#define      FLD_DIF_IF_REF                             0x00000fff

/*****************************************************************************/
#define      DIF_AGC_CTRL_IF          (DIRECT_IF_REVB_BASE + 0x00000018)
/*****************************************************************************/
#define      FLD_DIF_IF_MAX                             0xff000000
#define      FLD_DIF_IF_MIN                             0x00ff0000
#define      FLD_DIF_IF_AGC                             0x0000ffff

/*****************************************************************************/
#define      DIF_AGC_CTRL_INT         (DIRECT_IF_REVB_BASE + 0x0000001c)
/*****************************************************************************/
#define      FLD_DIF_INT_MAX                            0xff000000
#define      FLD_DIF_INT_MIN                            0x00ff0000
#define      FLD_DIF_INT_AGC                            0x0000ffff

/*****************************************************************************/
#define      DIF_AGC_CTRL_RF          (DIRECT_IF_REVB_BASE + 0x00000020)
/*****************************************************************************/
#define      FLD_DIF_RF_MAX                             0xff000000
#define      FLD_DIF_RF_MIN                             0x00ff0000
#define      FLD_DIF_RF_AGC                             0x0000ffff

/*****************************************************************************/
#define      DIF_AGC_IF_INT_CURRENT   (DIRECT_IF_REVB_BASE + 0x00000024)
/*****************************************************************************/
#define      FLD_DIF_IF_AGC_IN                          0xffff0000
#define      FLD_DIF_INT_AGC_IN                         0x0000ffff

/*****************************************************************************/
#define      DIF_AGC_RF_CURRENT       (DIRECT_IF_REVB_BASE + 0x00000028)
/*****************************************************************************/
/*  Reserved                            [31:16] */
#define      FLD_DIF_RF_AGC_IN                          0x0000ffff

/*****************************************************************************/
#define      DIF_VIDEO_AGC_CTRL       (DIRECT_IF_REVB_BASE + 0x0000002c)
/*****************************************************************************/
#define      FLD_DIF_AFD                                0xc0000000
#define      FLD_DIF_K_VID_AGC                          0x30000000
#define      FLD_DIF_LINE_LENGTH                        0x0fff0000
#define      FLD_DIF_AGC_GAIN                           0x0000ffff

/*****************************************************************************/
#define      DIF_VID_AUD_OVERRIDE     (DIRECT_IF_REVB_BASE + 0x00000030)
/*****************************************************************************/
#define      FLD_DIF_AUDIO_AGC_OVERRIDE                 0x80000000
/*  Reserved                             [30:30] */
#define      FLD_DIF_AUDIO_MAN_GAIN                     0x3f000000
/*  Reserved                             [23:17] */
#define      FLD_DIF_VID_AGC_OVERRIDE                   0x00010000
#define      FLD_DIF_VID_MAN_GAIN                       0x0000ffff

/*****************************************************************************/
#define      DIF_AV_SEP_CTRL          (DIRECT_IF_REVB_BASE + 0x00000034)
/*****************************************************************************/
#define      FLD_DIF_LPF_FREQ                           0xc0000000
#define      FLD_DIF_AV_PHASE_INC                       0x3f000000
#define      FLD_DIF_AUDIO_FREQ                         0x00ffffff

/*****************************************************************************/
#define      DIF_COMP_FLT_CTRL        (DIRECT_IF_REVB_BASE + 0x00000038)
/*****************************************************************************/
/*  Reserved                            [31:24] */
#define      FLD_DIF_IIR23_R2                           0x00ff0000
#define      FLD_DIF_IIR23_R1                           0x0000ff00
#define      FLD_DIF_IIR1_R1                            0x000000ff

/*****************************************************************************/
#define      DIF_MISC_CTRL            (DIRECT_IF_REVB_BASE + 0x0000003c)
/*****************************************************************************/
#define      FLD_DIF_DIF_BYPASS                         0x80000000
#define      FLD_DIF_FM_NYQ_GAIN                        0x40000000
#define      FLD_DIF_RF_AGC_ENA                         0x20000000
#define      FLD_DIF_INT_AGC_ENA                        0x10000000
#define      FLD_DIF_IF_AGC_ENA                         0x08000000
#define      FLD_DIF_FORCE_RF_IF_LOCK                   0x04000000
#define      FLD_DIF_VIDEO_AGC_ENA                      0x02000000
#define      FLD_DIF_RF_AGC_INV                         0x01000000
#define      FLD_DIF_INT_AGC_INV                        0x00800000
#define      FLD_DIF_IF_AGC_INV                         0x00400000
#define      FLD_DIF_SPEC_INV                           0x00200000
#define      FLD_DIF_AUD_FULL_BW                        0x00100000
#define      FLD_DIF_AUD_SRC_SEL                        0x00080000
/*  Reserved                             [18] */
#define      FLD_DIF_IF_FREQ                            0x00030000
/*  Reserved                             [15:14] */
#define      FLD_DIF_TIP_OFFSET                         0x00003f00
/*  Reserved                             [7:5] */
#define      FLD_DIF_DITHER_ENA                         0x00000010
/*  Reserved                             [3:1] */
#define      FLD_DIF_RF_IF_LOCK                         0x00000001

/*****************************************************************************/
#define      DIF_SRC_PHASE_INC        (DIRECT_IF_REVB_BASE + 0x00000040)
/*****************************************************************************/
/*  Reserved                             [31:29] */
#define      FLD_DIF_PHASE_INC                          0x1fffffff

/*****************************************************************************/
#define      DIF_SRC_GAIN_CONTROL     (DIRECT_IF_REVB_BASE + 0x00000044)
/*****************************************************************************/
/*  Reserved                             [31:16] */
#define      FLD_DIF_SRC_KI                             0x0000ff00
#define      FLD_DIF_SRC_KD                             0x000000ff

/*****************************************************************************/
#define      DIF_BPF_COEFF01          (DIRECT_IF_REVB_BASE + 0x00000048)
/*****************************************************************************/
/*  Reserved                             [31:19] */
#define      FLD_DIF_BPF_COEFF_0                        0x00070000
/*  Reserved                             [15:4] */
#define      FLD_DIF_BPF_COEFF_1                        0x0000000f

/*****************************************************************************/
#define      DIF_BPF_COEFF23          (DIRECT_IF_REVB_BASE + 0x0000004c)
/*****************************************************************************/
/*  Reserved                             [31:22] */
#define      FLD_DIF_BPF_COEFF_2                        0x003f0000
/*  Reserved                             [15:7] */
#define      FLD_DIF_BPF_COEFF_3                        0x0000007f

/*****************************************************************************/
#define      DIF_BPF_COEFF45          (DIRECT_IF_REVB_BASE + 0x00000050)
/*****************************************************************************/
/*  Reserved                             [31:24] */
#define      FLD_DIF_BPF_COEFF_4                        0x00ff0000
/*  Reserved                             [15:8] */
#define      FLD_DIF_BPF_COEFF_5                        0x000000ff

/*****************************************************************************/
#define      DIF_BPF_COEFF67          (DIRECT_IF_REVB_BASE + 0x00000054)
/*****************************************************************************/
/*  Reserved                             [31:25] */
#define      FLD_DIF_BPF_COEFF_6                        0x01ff0000
/*  Reserved                             [15:9] */
#define      FLD_DIF_BPF_COEFF_7                        0x000001ff

/*****************************************************************************/
#define      DIF_BPF_COEFF89          (DIRECT_IF_REVB_BASE + 0x00000058)
/*****************************************************************************/
/*  Reserved                             [31:26] */
#define      FLD_DIF_BPF_COEFF_8                        0x03ff0000
/*  Reserved                             [15:10] */
#define      FLD_DIF_BPF_COEFF_9                        0x000003ff

/*****************************************************************************/
#define      DIF_BPF_COEFF1011        (DIRECT_IF_REVB_BASE + 0x0000005c)
/*****************************************************************************/
/*  Reserved                             [31:27] */
#define      FLD_DIF_BPF_COEFF_10                       0x07ff0000
/*  Reserved                             [15:11] */
#define      FLD_DIF_BPF_COEFF_11                       0x000007ff

/*****************************************************************************/
#define      DIF_BPF_COEFF1213        (DIRECT_IF_REVB_BASE + 0x00000060)
/*****************************************************************************/
/*  Reserved                             [31:27] */
#define      FLD_DIF_BPF_COEFF_12                       0x07ff0000
/*  Reserved                             [15:12] */
#define      FLD_DIF_BPF_COEFF_13                       0x00000fff

/*****************************************************************************/
#define      DIF_BPF_COEFF1415        (DIRECT_IF_REVB_BASE + 0x00000064)
/*****************************************************************************/
/*  Reserved                             [31:28] */
#define      FLD_DIF_BPF_COEFF_14                       0x0fff0000
/*  Reserved                             [15:12] */
#define      FLD_DIF_BPF_COEFF_15                       0x00000fff

/*****************************************************************************/
#define      DIF_BPF_COEFF1617        (DIRECT_IF_REVB_BASE + 0x00000068)
/*****************************************************************************/
/*  Reserved                             [31:29] */
#define      FLD_DIF_BPF_COEFF_16                       0x1fff0000
/*  Reserved                             [15:13] */
#define      FLD_DIF_BPF_COEFF_17                       0x00001fff

/*****************************************************************************/
#define      DIF_BPF_COEFF1819        (DIRECT_IF_REVB_BASE + 0x0000006c)
/*****************************************************************************/
/*  Reserved                             [31:29] */
#define      FLD_DIF_BPF_COEFF_18                       0x1fff0000
/*  Reserved                             [15:13] */
#define      FLD_DIF_BPF_COEFF_19                       0x00001fff

/*****************************************************************************/
#define      DIF_BPF_COEFF2021        (DIRECT_IF_REVB_BASE + 0x00000070)
/*****************************************************************************/
/*  Reserved                             [31:29] */
#define      FLD_DIF_BPF_COEFF_20                       0x1fff0000
/*  Reserved                             [15:14] */
#define      FLD_DIF_BPF_COEFF_21                       0x00003fff

/*****************************************************************************/
#define      DIF_BPF_COEFF2223        (DIRECT_IF_REVB_BASE + 0x00000074)
/*****************************************************************************/
/*  Reserved                             [31:30] */
#define      FLD_DIF_BPF_COEFF_22                       0x3fff0000
/*  Reserved                             [15:14] */
#define      FLD_DIF_BPF_COEFF_23                       0x00003fff

/*****************************************************************************/
#define      DIF_BPF_COEFF2425        (DIRECT_IF_REVB_BASE + 0x00000078)
/*****************************************************************************/
/*  Reserved                             [31:30] */
#define      FLD_DIF_BPF_COEFF_24                       0x3fff0000
/*  Reserved                             [15:14] */
#define      FLD_DIF_BPF_COEFF_25                       0x00003fff

/*****************************************************************************/
#define      DIF_BPF_COEFF2627        (DIRECT_IF_REVB_BASE + 0x0000007c)
/*****************************************************************************/
/*  Reserved                             [31:30] */
#define      FLD_DIF_BPF_COEFF_26                       0x3fff0000
/*  Reserved                             [15:14] */
#define      FLD_DIF_BPF_COEFF_27                       0x00003fff

/*****************************************************************************/
#define      DIF_BPF_COEFF2829        (DIRECT_IF_REVB_BASE + 0x00000080)
/*****************************************************************************/
/*  Reserved                             [31:30] */
#define      FLD_DIF_BPF_COEFF_28                       0x3fff0000
/*  Reserved                             [15:14] */
#define      FLD_DIF_BPF_COEFF_29                       0x00003fff

/*****************************************************************************/
#define      DIF_BPF_COEFF3031        (DIRECT_IF_REVB_BASE + 0x00000084)
/*****************************************************************************/
/*  Reserved                             [31:30] */
#define      FLD_DIF_BPF_COEFF_30                       0x3fff0000
/*  Reserved                             [15:14] */
#define      FLD_DIF_BPF_COEFF_31                       0x00003fff

/*****************************************************************************/
#define      DIF_BPF_COEFF3233        (DIRECT_IF_REVB_BASE + 0x00000088)
/*****************************************************************************/
/*  Reserved                             [31:30] */
#define      FLD_DIF_BPF_COEFF_32                       0x3fff0000
/*  Reserved                             [15:14] */
#define      FLD_DIF_BPF_COEFF_33                       0x00003fff

/*****************************************************************************/
#define      DIF_BPF_COEFF3435        (DIRECT_IF_REVB_BASE + 0x0000008c)
/*****************************************************************************/
/*  Reserved                             [31:30] */
#define      FLD_DIF_BPF_COEFF_34                       0x3fff0000
/*  Reserved                             [15:14] */
#define      FLD_DIF_BPF_COEFF_35                       0x00003fff

/*****************************************************************************/
#define      DIF_BPF_COEFF36          (DIRECT_IF_REVB_BASE + 0x00000090)
/*****************************************************************************/
/*  Reserved                             [31:30] */
#define      FLD_DIF_BPF_COEFF_36                       0x3fff0000
/*  Reserved                             [15:0] */

/*****************************************************************************/
#define      DIF_RPT_VARIANCE         (DIRECT_IF_REVB_BASE + 0x00000094)
/*****************************************************************************/
/*  Reserved                             [31:20] */
#define      FLD_DIF_RPT_VARIANCE                       0x000fffff

/*****************************************************************************/
#define      DIF_SOFT_RST_CTRL_REVB       (DIRECT_IF_REVB_BASE + 0x00000098)
/*****************************************************************************/
/*  Reserved                             [31:8] */
#define      FLD_DIF_DIF_SOFT_RST                       0x00000080
#define      FLD_DIF_DIF_REG_RST_MSK                    0x00000040
#define      FLD_DIF_AGC_RST_MSK                        0x00000020
#define      FLD_DIF_CMP_RST_MSK                        0x00000010
#define      FLD_DIF_AVS_RST_MSK                        0x00000008
#define      FLD_DIF_NYQ_RST_MSK                        0x00000004
#define      FLD_DIF_DIF_SRC_RST_MSK                    0x00000002
#define      FLD_DIF_PLL_RST_MSK                        0x00000001

/*****************************************************************************/
#define      DIF_PLL_FREQ_ERR         (DIRECT_IF_REVB_BASE + 0x0000009c)
/*****************************************************************************/
/*  Reserved                             [31:25] */
#define      FLD_DIF_CTL_IP                             0x01ffffff

#endif
