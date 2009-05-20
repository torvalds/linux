#ifndef __WINBOND_PHY_CALIBRATION_H
#define __WINBOND_PHY_CALIBRATION_H

#include "wbhal_f.h"

// 20031229 Turbo add
#define REG_AGC_CTRL1               0x1000
#define REG_AGC_CTRL2               0x1004
#define REG_AGC_CTRL3               0x1008
#define REG_AGC_CTRL4               0x100C
#define REG_AGC_CTRL5               0x1010
#define REG_AGC_CTRL6               0x1014
#define REG_AGC_CTRL7               0x1018
#define REG_AGC_CTRL8               0x101C
#define REG_AGC_CTRL9               0x1020
#define REG_AGC_CTRL10              0x1024
#define REG_CCA_CTRL                0x1028
#define REG_A_ACQ_CTRL              0x102C
#define REG_B_ACQ_CTRL              0x1030
#define REG_A_TXRX_CTRL             0x1034
#define REG_B_TXRX_CTRL             0x1038
#define REG_A_TX_COEF3              0x103C
#define REG_A_TX_COEF2              0x1040
#define REG_A_TX_COEF1              0x1044
#define REG_B_TX_COEF2              0x1048
#define REG_B_TX_COEF1              0x104C
#define REG_MODE_CTRL               0x1050
#define REG_CALIB_DATA              0x1054
#define REG_IQ_ALPHA                0x1058
#define REG_DC_CANCEL               0x105C
#define REG_WTO_READ                0x1060
#define REG_OFFSET_READ             0x1064
#define REG_CALIB_READ1             0x1068
#define REG_CALIB_READ2             0x106C
#define REG_A_FREQ_EST              0x1070




//  20031101 Turbo add
#define MASK_AMER_OFF_REG          BIT(31)

#define MASK_BMER_OFF_REG          BIT(31)

#define MASK_LNA_FIX_GAIN          (BIT(3)|BIT(4))
#define MASK_AGC_FIX               BIT(1)

#define MASK_AGC_FIX_GAIN          0xFF00

#define MASK_ADC_DC_CAL_STR        BIT(10)
#define MASK_CALIB_START           BIT(4)
#define MASK_IQCAL_TONE_SEL        (BIT(3)|BIT(2))
#define MASK_IQCAL_MODE            (BIT(1)|BIT(0))

#define MASK_TX_CAL_0              0xF0000000
#define TX_CAL_0_SHIFT             28
#define MASK_TX_CAL_1              0x0F000000
#define TX_CAL_1_SHIFT             24
#define MASK_TX_CAL_2              0x00F00000
#define TX_CAL_2_SHIFT             20
#define MASK_TX_CAL_3              0x000F0000
#define TX_CAL_3_SHIFT             16
#define MASK_RX_CAL_0              0x0000F000
#define RX_CAL_0_SHIFT             12
#define MASK_RX_CAL_1              0x00000F00
#define RX_CAL_1_SHIFT             8
#define MASK_RX_CAL_2              0x000000F0
#define RX_CAL_2_SHIFT             4
#define MASK_RX_CAL_3              0x0000000F
#define RX_CAL_3_SHIFT             0

#define MASK_CANCEL_DC_I           0x3E0
#define CANCEL_DC_I_SHIFT          5
#define MASK_CANCEL_DC_Q           0x01F
#define CANCEL_DC_Q_SHIFT          0

// LA20040210 kevin
//#define MASK_ADC_DC_CAL_I(x)       (((x)&0x1FE00)>>9)
//#define MASK_ADC_DC_CAL_Q(x)       ((x)&0x1FF)
#define MASK_ADC_DC_CAL_I(x)       (((x)&0x0003FE00)>>9)
#define MASK_ADC_DC_CAL_Q(x)       ((x)&0x000001FF)

// LA20040210 kevin (Turbo has wrong definition)
//#define MASK_IQCAL_TONE_I          0x7FFC000
//#define SHIFT_IQCAL_TONE_I(x)      ((x)>>13)
//#define MASK_IQCAL_TONE_Q          0x1FFF
//#define SHIFT_IQCAL_TONE_Q(x)      ((x)>>0)
#define MASK_IQCAL_TONE_I          0x00001FFF
#define SHIFT_IQCAL_TONE_I(x)      ((x)>>0)
#define MASK_IQCAL_TONE_Q          0x03FFE000
#define SHIFT_IQCAL_TONE_Q(x)      ((x)>>13)

// LA20040210 kevin (Turbo has wrong definition)
//#define MASK_IQCAL_IMAGE_I         0x7FFC000
//#define SHIFT_IQCAL_IMAGE_I(x)     ((x)>>13)
//#define MASK_IQCAL_IMAGE_Q         0x1FFF
//#define SHIFT_IQCAL_IMAGE_Q(x)     ((x)>>0)

//#define MASK_IQCAL_IMAGE_I         0x00001FFF
//#define SHIFT_IQCAL_IMAGE_I(x)     ((x)>>0)
//#define MASK_IQCAL_IMAGE_Q         0x03FFE000
//#define SHIFT_IQCAL_IMAGE_Q(x)     ((x)>>13)

void phy_set_rf_data(  struct hw_data * pHwData,  u32 index,  u32 value );
#define phy_init_rf( _A )	//RFSynthesizer_initial( _A )

#endif
