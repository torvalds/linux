#ifndef __TUNER_TDA18272_H
#define __TUNER_TDA18272_H

/**

@file

@brief   TDA18272 tuner module declaration

One can manipulate TDA18272 tuner through TDA18272 module.
TDA18272 module is derived from tuner module.



@par Example:
@code

// The example is the same as the tuner example in tuner_base.h except the listed lines.



#include "tuner_tda18272.h"


...



int main(void)
{
	TUNER_MODULE          *pTuner;
	TDA18272_EXTRA_MODULE *pTunerExtra;

	TUNER_MODULE          TunerModuleMemory;
	BASE_INTERFACE_MODULE BaseInterfaceModuleMemory;
	I2C_BRIDGE_MODULE     I2cBridgeModuleMemory;

	int StandardBandwidthMode;


	...



	// Build TDA18272 tuner module.
	BuildTda18272Module(
		&pTuner,
		&TunerModuleMemory,
		&BaseInterfaceModuleMemory,
		&I2cBridgeModuleMemory,
		0xc0,								// I2C device address is 0xc0 in 8-bit format.
		CRYSTAL_FREQ_16000000HZ,			// Crystal frequency is 16.0 MHz.
		TDA18272_UNIT_0,					// Unit number is 0.
		TDA18272_IF_OUTPUT_VPP_0P7V			// IF output Vp-p is 0.7 V.
		);





	// Get TDA18272 tuner extra module.
	pTunerExtra = (TDA18272_EXTRA_MODULE *)(pTuner->pExtra);





	// ==== Initialize tuner and set its parameters =====

	...

	// Set TDA18272 standard and bandwidth mode.
	pTunerExtra->SetStandardBandwidthMode(pTuner, TDA18272_STANDARD_BANDWIDTH_DVBT_6MHZ);





	// ==== Get tuner information =====

	...

	// Get TDA18272 bandwidth.
	pTunerExtra->GetStandardBandwidthMode(pTuner, &StandardBandwidthMode);



	// See the example for other tuner functions in tuner_base.h


	return 0;
}


@endcode

*/





#include "tuner_base.h"





// Defined by Realtek for tuner TDA18272.
#define TMBSL_TDA18272





// The following context is source code provided by NXP.





// NXP source code - .\cfg\tmbslTDA182I2_InstanceCustom.h


 /*
  Copyright (C) 2006-2009 NXP B.V., All Rights Reserved.
  This source code and any compilation or derivative thereof is the proprietary
  information of NXP B.V. and is confidential in nature. Under no circumstances
  is this software to be  exposed to or placed under an Open Source License of
  any type without the expressed written permission of NXP B.V.
 *
 * \file          tmbslTDA182I2_InstanceCustom.h
 *
 *                1
 *
 * \date          %modify_time%
 *
 * \brief         Describe briefly the purpose of this file.
 *
 * REFERENCE DOCUMENTS :
 *                
 *
 * Detailed description may be added here.
 *
 * \section info Change Information
 *
*/

#ifndef _TMBSL_TDA182I2_INSTANCE_CUSTOM_H
#define _TMBSL_TDA182I2_INSTANCE_CUSTOM_H


#ifdef __cplusplus
extern "C"
{
#endif

/*============================================================================*/
/* Types and defines:                                                         */
/*============================================================================*/

/* Driver settings version definition */
#define TMBSL_TDA182I2_SETTINGS_CUSTOMER_NUM    0  /* SW Settings Customer Number */
#define TMBSL_TDA182I2_SETTINGS_PROJECT_NUM     0  /* SW Settings Project Number  */
#define TMBSL_TDA182I2_SETTINGS_MAJOR_VER       0  /* SW Settings Major Version   */
#define TMBSL_TDA182I2_SETTINGS_MINOR_VER       0  /* SW Settings Minor Version   */

/* Custom Driver Instance Parameters: (Path 0) */
#define TMBSL_TDA182I2_INSTANCE_CUSTOM_MASTER                                                 \
        tmTDA182I2_PowerStandbyWithXtalOn,                      /* Current Power state */               \
        tmTDA182I2_PowerStandbyWithXtalOn,                      /* Minimum Power state */               \
        0,                                                      /* RF */               \
        tmTDA182I2_DVBT_8MHz,                                   /* Standard mode */               \
        True,                                                   /* Master */   \
        0,                                                      /* LT_Enable */    \
        1,                                                      /* PSM_AGC1 */        \
        0,                                                      /* AGC1_6_15dB */            \

#define TMBSL_TDA182I2_INSTANCE_CUSTOM_MASTER_DIGITAL                                           \
        tmTDA182I2_PowerStandbyWithXtalOn,                      /* Current Power state */               \
        tmTDA182I2_PowerStandbyWithLNAOnAndWithXtalOnAndSynthe, /* Minimum Power state */               \
        0,                                                      /* RF */               \
        tmTDA182I2_DVBT_8MHz,                                   /* Standard mode */               \
        True,                                                   /* Master */   \
        1,                                                      /* LT_Enable */    \
        0,                                                      /* PSM_AGC1 */        \
        1,                                                      /* AGC1_6_15dB */            \

#define TMBSL_TDA182I2_INSTANCE_CUSTOM_SLAVE                                                                 \
        tmTDA182I2_PowerStandbyWithXtalOn,                      /* Current Power state */               \
        tmTDA182I2_PowerStandbyWithXtalOn,                      /* Minimum Power state */               \
        0,                                                      /* RF */               \
        tmTDA182I2_DVBT_8MHz,                                   /* Standard mode */               \
        False,                                                  /* Master */   \
        0,                                                      /* LT_Enable */    \
        1,                                                      /* PSM_AGC1 */        \
        0,                                                      /* AGC1_6_15dB */            \

#define TMBSL_TDA182I2_INSTANCE_CUSTOM_SLAVE_DIGITAL                                                                 \
        tmTDA182I2_PowerStandbyWithXtalOn,                      /* Current Power state */               \
        tmTDA182I2_PowerStandbyWithXtalOn,                      /* Minimum Power state */               \
        0,                                                      /* RF */               \
        tmTDA182I2_DVBT_8MHz,                                   /* Standard mode */               \
        False,                                                  /* Master */   \
        0,                                                      /* LT_Enable */    \
        1,                                                      /* PSM_AGC1 */        \
        0,                                                      /* AGC1_6_15dB */            \

#define TMBSL_TDA182I2_INSTANCE_CUSTOM_STD_DIGITAL                                                                 \
            {   /* Std_Array */                                 /* DVB-T 6MHz */               \
                3250000,                                        /* IF */               \
                0,                                              /* CF_Offset */               \
                tmTDA182I2_LPF_6MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_1Vpp_min_6_24dB,         /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Enabled,                    /* IF_Notch */               \
                tmTDA182I2_IF_HPF_0_4MHz,                       /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Enabled,                    /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_100dBuV,             /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_102dBuV,             /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d110_u105dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d110_u105dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Disabled,          /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Enabled,                  /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_2_Step,               /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Enabled,              /* AGC5_Atten_3dB */               \
                0x02,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Enabled,            /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Free,                       /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \
            {                                                   /* DVB-T 7MHz */               \
                3500000,                                        /* IF */               \
                0,                                              /* CF_Offset */               \
                tmTDA182I2_LPF_7MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_min_8pc,                   /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_1Vpp_min_6_24dB,         /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Enabled,                    /* IF_Notch */               \
                tmTDA182I2_IF_HPF_Disabled,                     /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Enabled,                    /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_100dBuV,             /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_102dBuV,             /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d110_u105dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d110_u105dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Disabled,          /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Enabled,                  /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_2_Step,               /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Enabled,              /* AGC5_Atten_3dB */               \
                0x02,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Enabled,            /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Free,                       /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \
            {                                                   /* DVB-T 8MHz */               \
                4000000,                                        /* IF */               \
                0,                                              /* CF_Offset */               \
                tmTDA182I2_LPF_8MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_1Vpp_min_6_24dB,         /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Enabled,                    /* IF_Notch */               \
                tmTDA182I2_IF_HPF_Disabled,                     /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Enabled,                    /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_100dBuV,             /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_102dBuV,             /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d110_u105dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d110_u105dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Disabled,          /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Enabled,                  /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_2_Step,               /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Enabled,              /* AGC5_Atten_3dB */               \
                0x02,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Enabled,            /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Free,                       /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \
            {                                                   /* QAM 6MHz */               \
                3600000,                                        /* IF */               \
                0,                                              /* CF_Offset */               \
                tmTDA182I2_LPF_6MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_min_8pc,                   /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_1Vpp_min_6_24dB,         /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Disabled,                   /* IF_Notch */               \
                tmTDA182I2_IF_HPF_Disabled,                     /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Enabled,                    /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_100dBuV,             /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_100dBuV,             /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d110_u105dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d110_u105dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Disabled,          /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Disabled,                 /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_0_Step,               /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Disabled,             /* AGC5_Atten_3dB */               \
                0x02,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Disabled,           /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Free,                       /* LPF_Gain */               \
				True,											/* AGC1_freeze */ \
				True											/* LTO_STO_immune */ \
            },               \
            {                                                   /* QAM 8MHz */               \
                5000000,                                        /* IF */               \
                0,                                              /* CF_Offset */               \
                tmTDA182I2_LPF_9MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_min_8pc,                   /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_1Vpp_min_6_24dB,         /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Disabled,                   /* IF_Notch */               \
                tmTDA182I2_IF_HPF_0_85MHz,                      /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Enabled,                    /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_100dBuV,             /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_100dBuV,             /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d110_u105dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d110_u105dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Disabled,          /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Disabled,                 /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_0_Step,               /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Disabled,             /* AGC5_Atten_3dB */               \
                0x02,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Disabled,           /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Free,                       /* LPF_Gain */               \
				True,											/* AGC1_freeze */ \
				True											/* LTO_STO_immune */ \
            },               \
            {                                                   /* ISDBT 6MHz */               \
                3250000,                                        /* IF */               \
                0,                                              /* CF_Offset */               \
                tmTDA182I2_LPF_6MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_0_6Vpp_min_10_3_19_7dB,  /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Enabled,                    /* IF_Notch */               \
                tmTDA182I2_IF_HPF_0_4MHz,                       /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Enabled,                    /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_100dBuV,             /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_102dBuV,             /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d110_u105dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d110_u105dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Disabled,          /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Enabled,                  /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_2_Step,        /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Enabled,              /* AGC5_Atten_3dB */               \
                0x02,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Enabled,            /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Free,                       /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \
            {                                                   /* ATSC 6MHz */               \
                3250000,                                        /* IF */               \
                0,                                              /* CF_Offset */               \
                tmTDA182I2_LPF_6MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_0_6Vpp_min_10_3_19_7dB,  /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Enabled,                    /* IF_Notch */               \
                tmTDA182I2_IF_HPF_0_4MHz,                       /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Enabled,                    /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d100_u94dBuV,           /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_104dBuV,             /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_104dBuV,             /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d112_u107dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d112_u107dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Disabled,          /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Enabled,                  /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_3_Step,        /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Enabled,              /* AGC5_Atten_3dB */               \
                0x02,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Enabled,            /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Free,                       /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \
            {                                                   /* DMB-T 8MHz */               \
                4000000,                                        /* IF */               \
                0,                                              /* CF_Offset */               \
                tmTDA182I2_LPF_8MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_1Vpp_min_6_24dB,         /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Enabled,                    /* IF_Notch */               \
                tmTDA182I2_IF_HPF_Disabled,                     /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Enabled,                    /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_100dBuV,             /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_102dBuV,             /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d110_u105dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d110_u105dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Disabled,          /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Enabled,                  /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_2_Step,        /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Enabled,              /* AGC5_Atten_3dB */               \
                0x02,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Enabled,            /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Free,                       /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \

#define  TMBSL_TDA182I2_INSTANCE_CUSTOM_STD_ANALOG                                          \
            {                                                   /* Analog M/N */               \
                5400000,                                        /* IF */               \
                1750000,                                        /* CF_Offset */               \
                tmTDA182I2_LPF_6MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_0_7Vpp_min_9_21dB,       /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Disabled,                   /* IF_Notch */               \
                tmTDA182I2_IF_HPF_Disabled,                     /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Disabled,                   /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d105_u100dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d105_u100dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Enabled,           /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Disabled,                 /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_0_Step,        /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Disabled,             /* AGC5_Atten_3dB */               \
                0x01,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Disabled,           /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Frozen,                     /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \
            {                                                   /* Analog B */               \
                6400000,                                        /* IF */               \
                2250000,                                        /* CF_Offset */               \
                tmTDA182I2_LPF_7MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_0_7Vpp_min_9_21dB,       /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Disabled,                   /* IF_Notch */               \
                tmTDA182I2_IF_HPF_Disabled,                     /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Disabled,                   /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_High_band*/               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d105_u100dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d105_u100dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Enabled,           /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Disabled,                 /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_0_Step,        /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Disabled,             /* AGC5_Atten_3dB */               \
                0x01,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Disabled,           /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Frozen,                     /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \
            {                                                   /* Analog G/H */               \
                6750000,                                        /* IF */               \
                2750000,                                        /* CF_Offset */               \
                tmTDA182I2_LPF_8MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_0_7Vpp_min_9_21dB,       /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Disabled,                   /* IF_Notch */               \
                tmTDA182I2_IF_HPF_Disabled,                     /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Disabled,                   /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d105_u100dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d105_u100dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Enabled,           /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Disabled,                 /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_0_Step,        /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Disabled,             /* AGC5_Atten_3dB */               \
                0x01,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Disabled,           /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Frozen,                     /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \
            {                                                   /* Analog I */               \
                7250000,                                        /* IF */               \
                2750000,                                        /* CF_Offset */               \
                tmTDA182I2_LPF_8MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_0_7Vpp_min_9_21dB,       /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Disabled,                   /* IF_Notch */               \
                tmTDA182I2_IF_HPF_Disabled,                     /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Disabled,                   /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d105_u100dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d105_u100dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Enabled,           /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Disabled,                 /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_0_Step,        /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Disabled,             /* AGC5_Atten_3dB */               \
                0x01,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Disabled,           /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Frozen,                     /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \
            {                                                   /* Analog D/K */               \
                6850000,                                        /* IF */               \
                2750000,                                        /* CF_Offset */               \
                tmTDA182I2_LPF_8MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_0_7Vpp_min_9_21dB,       /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Enabled,                    /* IF_Notch */               \
                tmTDA182I2_IF_HPF_Disabled,                     /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Disabled,                   /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d105_u100dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d105_u100dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Enabled,           /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Disabled,                 /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_0_Step,        /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Disabled,             /* AGC5_Atten_3dB */               \
                0x01,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Disabled,           /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Frozen,                     /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \
            {                                                   /* Analog L */               \
                6750000,                                        /* IF */               \
                2750000,                                        /* CF_Offset */               \
                tmTDA182I2_LPF_8MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_0_7Vpp_min_9_21dB,       /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Enabled,                    /* IF_Notch */               \
                tmTDA182I2_IF_HPF_Disabled,                     /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Disabled,                   /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d105_u100dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d105_u100dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Enabled,           /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Disabled,                 /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_0_Step,        /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Disabled,             /* AGC5_Atten_3dB */               \
                0x01,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Disabled,           /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Frozen,                     /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \
            {                                                   /* Analog L' */               \
                1250000,                                        /* IF */               \
                -2750000,                                       /* CF_Offset */               \
                tmTDA182I2_LPF_8MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_0_7Vpp_min_9_21dB,       /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Disabled,                   /* IF_Notch */               \
                tmTDA182I2_IF_HPF_Disabled,                     /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Disabled,                   /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d105_u100dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d105_u100dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Disabled,          /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Disabled,                 /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_0_Step,        /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Disabled,             /* AGC5_Atten_3dB */               \
                0x01,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Disabled,           /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Frozen,                     /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \
            {                                                   /* Analog FM Radio */               \
                1250000,                                        /* IF */               \
                0,                                              /* CF_Offset */               \
                tmTDA182I2_LPF_1_5MHz,                          /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_0_7Vpp_min_9_21dB,       /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Disabled,                   /* IF_Notch */               \
                tmTDA182I2_IF_HPF_0_85MHz,                      /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Enabled,                    /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d105_u100dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d105_u100dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Disabled,          /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Disabled,                 /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_0_Step,        /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Disabled,             /* AGC5_Atten_3dB */               \
                0x02,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Disabled,           /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Frozen,                     /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \
            {                                                   /* Blind Scanning copy of PAL-I */               \
                7250000,                                        /* IF */               \
                2750000,                                        /* CF_Offset */               \
                tmTDA182I2_LPF_8MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_0_7Vpp_min_9_21dB,       /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Disabled,                   /* IF_Notch */               \
                tmTDA182I2_IF_HPF_Disabled,                     /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Disabled,                   /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,              /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d105_u100dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d105_u100dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Enabled,           /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Disabled,                 /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_0_Step,        /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Disabled,             /* AGC5_Atten_3dB */               \
                0x01,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Disabled,           /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Frozen,                     /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            },               \
            {                                                   /* ScanXpress */               \
                5000000,                                        /* IF */               \
                0,                                              /* CF_Offset */               \
                tmTDA182I2_LPF_9MHz,                            /* LPF */               \
                tmTDA182I2_LPFOffset_0pc,                       /* LPF_Offset */               \
                tmTDA182I2_IF_AGC_Gain_1Vpp_min_6_24dB,         /* IF_Gain */               \
                tmTDA182I2_IF_Notch_Enabled,                    /* IF_Notch */               \
                tmTDA182I2_IF_HPF_Disabled,                     /* IF_HPF */               \
                tmTDA182I2_DC_Notch_Enabled,                    /* DC_Notch */               \
                tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV,            /* AGC1_LNA_TOP */               \
                tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,  /* AGC2_RF_Attenuator_TOP */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_100dBuV,             /* AGC3_RF_AGC_TOP_Low_band */               \
                tmTDA182I2_AGC3_RF_AGC_TOP_102dBuV,             /* AGC3_RF_AGC_TOP_High_band */               \
                tmTDA182I2_AGC4_IR_Mixer_TOP_d110_u105dBuV,     /* AGC4_IR_Mixer_TOP */               \
                tmTDA182I2_AGC5_IF_AGC_TOP_d110_u105dBuV,       /* AGC5_IF_AGC_TOP */               \
                tmTDA182I2_AGC5_Detector_HPF_Disabled,          /* AGC5_Detector_HPF */               \
                tmTDA182I2_AGC3_Adapt_Enabled,                  /* AGC3_Adapt */               \
                tmTDA182I2_AGC3_Adapt_TOP_2_Step,        /* AGC3_Adapt_TOP */               \
                tmTDA182I2_AGC5_Atten_3dB_Enabled,              /* AGC5_Atten_3dB */               \
                0x0e,                                           /* GSK : settings V2.0.0  */               \
                tmTDA182I2_H3H5_VHF_Filter6_Enabled,            /* H3H5_VHF_Filter6 */               \
                tmTDA182I2_LPF_Gain_Free,                       /* LPF_Gain */               \
				False,											/* AGC1_freeze */ \
				False											/* LTO_STO_immune */ \
            }
/* Custom Driver Instance Parameters: (Path 1) */

/******************************************************************/
/* Mode selection for PATH0                                       */
/******************************************************************/

#ifdef TMBSL_TDA18272

#define TMBSL_TDA182I2_INSTANCE_CUSTOM_MODE_PATH0 TMBSL_TDA182I2_INSTANCE_CUSTOM_MASTER
#define TMBSL_TDA182I2_INSTANCE_CUSTOM_STD_DIGITAL_SELECTION_PATH0 TMBSL_TDA182I2_INSTANCE_CUSTOM_STD_DIGITAL
#define TMBSL_TDA182I2_INSTANCE_CUSTOM_STD_ANALOG_SELECTION_PATH0 TMBSL_TDA182I2_INSTANCE_CUSTOM_STD_ANALOG

#else

#define TMBSL_TDA182I2_INSTANCE_CUSTOM_MODE_PATH0 TMBSL_TDA182I2_INSTANCE_CUSTOM_MASTER_DIGITAL
#define TMBSL_TDA182I2_INSTANCE_CUSTOM_STD_DIGITAL_SELECTION_PATH0 TMBSL_TDA182I2_INSTANCE_CUSTOM_STD_DIGITAL

#endif

/******************************************************************/
/* Mode selection for PATH1                                       */
/******************************************************************/

#ifdef TMBSL_TDA18272

#define TMBSL_TDA182I2_INSTANCE_CUSTOM_MODE_PATH1 TMBSL_TDA182I2_INSTANCE_CUSTOM_SLAVE
#define TMBSL_TDA182I2_INSTANCE_CUSTOM_STD_DIGITAL_SELECTION_PATH1 TMBSL_TDA182I2_INSTANCE_CUSTOM_STD_DIGITAL
#define TMBSL_TDA182I2_INSTANCE_CUSTOM_STD_ANALOG_SELECTION_PATH1 TMBSL_TDA182I2_INSTANCE_CUSTOM_STD_ANALOG

#else

#define TMBSL_TDA182I2_INSTANCE_CUSTOM_MODE_PATH1 TMBSL_TDA182I2_INSTANCE_CUSTOM_SLAVE_DIGITAL

#define TMBSL_TDA182I2_INSTANCE_CUSTOM_STD_DIGITAL_SELECTION_PATH1 TMBSL_TDA182I2_INSTANCE_CUSTOM_STD_DIGITAL

#endif

/******************************************************************/
/* End of Mode selection                                          */
/******************************************************************/

#ifdef __cplusplus
}
#endif

#endif /* _TMBSL_TDA182I2_INSTANCE_CUSTOM_H */


//#endif
//*/






















// NXP source code - .\inc\tmFlags.h


 /*                                        */                                                                         
 /*                                                                     */            
 /* -- DO NOT EDIT --  file built by                                    */            
 /*                                        */
 /*-------------------------------------------------------------------------    */
 /* (C) Copyright 2000 Koninklijke Philips Electronics N.V., All Rights Reserved*/
 /*                                                                              */    
 /* This source code and any compilation or derivative thereof is the sole       */
 /* property of Philips Corporation and is provided pursuant to a Software       */
 /* License Agreement.  This code is the proprietary information of              */
 /* Philips Corporation and is confidential in nature.  Its use and              */
 /* dissemination by any party other than Philips Corporation is strictly        */
 /* limited by the confidential information provisions of the Agreement          */
 /* referenced above.                                                            */
 /*-------------------------------------------------------------------------     */
 /*                                        */

 /*                                        */                                                                         
 /* DOCUMENT REF: DVP Build Process Specification                               */
 /*                                                                             */
 /* NOTES:        This file defines the TMFL_xxx build flags.                   */
 /*                                                                             */
 #if         !defined(_TMFLAGS_H_SEEN_)                                     
 #define _TMFLAGS_H_SEEN_                                                   
 /* configuration                                                            */
 /*      */
 /* FILENAME:     tmFlags.h                   */
 /*                                           */
 /* DESCRIPTION:  Generated by  */
 #define TMFL_BUILD_VERSION            00.00.00
 #define TMFL_CPU                      0x00020011
 #define TMFL_ENDIAN                   1
 #define TMFL_OS                       0x03020500
 #define TMFL_CPU_IS_X86               1
 #define TMFL_CPU_IS_MIPS              0
 #define TMFL_CPU_IS_HP                0
 #define TMFL_CPU_IS_TM                0
 #define TMFL_CPU_IS_ARM               0
 #define TMFL_CPU_IS_REAL              0
 #define TMFL_OS_IS_BTM                0
 #define TMFL_OS_IS_CE                 0
 #define TMFL_OS_IS_NT                 1
 #define TMFL_OS_IS_PSOS               0
 #define TMFL_OS_IS_NULLOS             0
 #define TMFL_OS_IS_ECOS               0
 #define TMFL_OS_IS_VXWORKS            0
 #define TMFL_OS_IS_MTOS               0
 #define TMFL_OS_IS_CEXEC              0
 /* DO NOT CHANGE THIS FILE INDEPENDENTLY !!!                                   */
 /* IT MUST BE SYNCHONISED WITH THE TMFLAGS TEMPLATE FILE ON THE                */
 /* MOREUSE WEB SITE http://pww.rtg.sc.philips.com/cmd/html/global_files.html   */
 /* CONTACT MOREUSE BEFORE ADDING NEW VALUES                                    */
 /* constants                                    */
 #define TMFL_CPU_TYPE_MASK            0xffff0000
 #define TMFL_CPU_TYPE_X86             0x00010000
 #define TMFL_CPU_TYPE_MIPS            0x00020000
 #define TMFL_CPU_TYPE_TM              0x00030000
 #define TMFL_CPU_TYPE_HP              0x00040000
 #define TMFL_CPU_TYPE_ARM             0x00050000
 #define TMFL_CPU_TYPE_REAL            0x00060000
 #define TMFL_CPU_MODEL_MASK           0x0000ffff
 #define TMFL_CPU_MODEL_I486           0x00000001
 #define TMFL_CPU_MODEL_R3940          0x00000002
 #define TMFL_CPU_MODEL_R4300          0x00000003
 #define TMFL_CPU_MODEL_TM1100         0x00000004
 #define TMFL_CPU_MODEL_TM1300         0x00000005
 #define TMFL_CPU_MODEL_TM32           0x00000006
 #define TMFL_CPU_MODEL_HP             0x00000007
 #define TMFL_CPU_MODEL_R4640          0x00000008
 #define TMFL_CPU_MODEL_ARM7           0x00000009
 #define TMFL_CPU_MODEL_ARM920T        0x0000000a
 #define TMFL_CPU_MODEL_ARM940T        0x0000000b
 #define TMFL_CPU_MODEL_ARM10          0x0000000c
 #define TMFL_CPU_MODEL_STRONGARM      0x0000000d
 #define TMFL_CPU_MODEL_RD24120        0x0000000e
 #define TMFL_CPU_MODEL_ARM926EJS      0x0000000f
 #define TMFL_CPU_MODEL_ARM946         0x00000010
 #define TMFL_CPU_MODEL_R1910          0x00000011
 #define TMFL_CPU_MODEL_R4450          0x00000012
 #define TMFL_CPU_MODEL_TM3260         0x00000013
 #define TMFL_ENDIAN_BIG               1
 #define TMFL_ENDIAN_LITTLE            0
 #define TMFL_OS_MASK                  0xff000000
 #define TMFL_OS_VERSION_MASK          0x00ffffff
 #define TMFL_OS_BTM                   0x00000000
 #define TMFL_OS_CE                    0x01000000
 #define TMFL_OS_CE212                 0x01020102
 #define TMFL_OS_CE300                 0x01030000
 #define TMFL_OS_NT                    0x02000000
 #define TMFL_OS_NT4                   0x02040000
 #define TMFL_OS_PSOS                  0x03000000
 #define TMFL_OS_PSOS250               0x03020500
 #define TMFL_OS_PSOS200               0x03020000
 #define TMFL_OS_NULLOS                0x04000000
 #define TMFL_OS_ECOS                  0x05000000
 #define TMFL_OS_VXWORKS               0x06000000
 #define TMFL_OS_MTOS                  0x07000000
 #define TMFL_OS_CEXEC                 0x08000000
 #define TMFL_SCOPE_SP                 0
 #define TMFL_SCOPE_MP                 1
 #define TMFL_REL_ASSERT               0x00000002
 #define TMFL_REL_DEBUG                0x00000001
 #define TMFL_REL_RETAIL               0x00000000
 #define TMFL_CPU_I486                 0x00010001
 #define TMFL_CPU_R3940                0x00020002
 #define TMFL_CPU_R4300                0x00020003
 #define TMFL_CPU_TM1100               0x00030004
 #define TMFL_CPU_TM1300               0x00030005
 #define TMFL_CPU_TM32                 0x00030006
 #define TMFL_CPU_HP                   0x00040007
 #define TMFL_CPU_R4640             0x00020008
 #define TMFL_CPU_ARM7              0x00050009
 #define TMFL_CPU_ARM920T           0x0005000a
 #define TMFL_CPU_ARM940T           0x0005000b
 #define TMFL_CPU_ARM10                   0x0005000c
 #define TMFL_CPU_STRONGARM            0x0005000d
 #define TMFL_CPU_RD24120              0x0006000e
 #define TMFL_CPU_ARM926EJS            0x0005000f
 #define TMFL_CPU_ARM946               0x00050010
 #define TMFL_CPU_R1910             0x00020011
 #define TMFL_CPU_R4450             0x00020012
 #define TMFL_CPU_TM3260             0x00030013
 #define TMFL_MODE_KERNEL              1
 #define TMFL_MODE_USER                0

 #endif   /* !defined(_TMFLAGS_H_SEEN_)                                         */























// NXP source code - .\inc\tmNxTypes.h


/*==========================================================================*/
/*     (Copyright (C) 2003 Koninklijke Philips Electronics N.V.             */
/*     All rights reserved.                                                 */
/*     This source code and any compilation or derivative thereof is the    */
/*     proprietary information of Koninklijke Philips Electronics N.V.      */
/*     and is confidential in nature.                                       */
/*     Under no circumstances is this software to be exposed to or placed   */
/*     under an Open Source License of any type without the expressed       */
/*     written permission of Koninklijke Philips Electronics N.V.           */
/*==========================================================================*/
/*
* Copyright (C) 2000,2001
*               Koninklijke Philips Electronics N.V.
*               All Rights Reserved.
*
* Copyright (C) 2000,2001 TriMedia Technologies, Inc.
*               All Rights Reserved.
*
*############################################################
*
* Module name  : tmNxTypes.h  %version: 4 %
*
* Last Update  : %date_modified: Tue Jul  8 18:08:00 2003 %
*
* Description: TriMedia/MIPS global type definitions.
*
* Document Ref: DVP Software Coding Guidelines Specification
* DVP/MoReUse Naming Conventions specification
* DVP Software Versioning Specification
* DVP Device Library Architecture Specification
* DVP Board Support Library Architecture Specification
* DVP Hardware API Architecture Specification
*
*
*############################################################
*/

#ifndef TM_NX_TYPES_H
#define TM_NX_TYPES_H

//-----------------------------------------------------------------------------
// Standard include files:
//-----------------------------------------------------------------------------
//

//-----------------------------------------------------------------------------
// Project include files:
//-----------------------------------------------------------------------------
//
//#include "tmFlags.h"                    // DVP common build control flags

#ifdef __cplusplus
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    // Types and defines:
    //-----------------------------------------------------------------------------
    //

    /*Under the TCS, <tmlib/tmtypes.h> may have been included by our client. In
    order to avoid errors, we take account of this possibility, but in order to
    support environments where the TCS is not available, we do not include the
    file by name.*/

#ifndef _TMtypes_h
#define _TMtypes_h

#define False           0
#define True            1

#ifdef TMFL_NATIVE_C_FORCED
 #undef TMFL_DOT_NET_2_0_TYPES
#undef NXPFE
#endif

#ifndef TMFL_DOT_NET_2_0_TYPES
 #ifdef __cplusplus
  #define Null            0
 #else
  #define Null            ((Void *) 0)
 #endif
#else
 #define Null            nullptr
#endif

    //
    //      Standard Types
    //

    typedef signed   char   CInt8;   //  8 bit   signed integer
    typedef signed   short  CInt16;  // 16 bit   signed integer
    typedef signed   long   CInt32;  // 32 bit   signed integer
    typedef unsigned char   CUInt8;  //  8 bit unsigned integer
    typedef unsigned short  CUInt16; // 16 bit unsigned integer
    typedef unsigned long   CUInt32; // 32 bit unsigned integer
//    typedef float           CFloat;  // 32 bit floating point
    typedef unsigned int    CBool;   // Boolean (True/False)
    typedef char            CChar;   // character, character array ptr
    typedef int             CInt;    // machine-natural integer
    typedef unsigned int    CUInt;   // machine-natural unsigned integer

#ifndef TMFL_DOT_NET_2_0_TYPES
    typedef CInt8           Int8;   //  8 bit   signed integer
    typedef CInt16          Int16;  // 16 bit   signed integer
    typedef CInt32          Int32;  // 32 bit   signed integer
    typedef CUInt8          UInt8;  //  8 bit unsigned integer
    typedef CUInt16         UInt16; // 16 bit unsigned integer
    typedef CUInt32         UInt32; // 32 bit unsigned integer
//    typedef CFloat          Float;  // 32 bit floating point
    typedef CBool           Bool;   // Boolean (True/False)
    typedef CChar           Char;   // character, character array ptr
    typedef CInt            Int;    // machine-natural integer
    typedef CUInt           UInt;   // machine-natural unsigned integer
    typedef char           *String; // Null terminated 8 bit char str

    //-----------------------------------------------------------------------------
    // Legacy TM Types/Structures (Not necessarily DVP Coding Guideline compliant)
    // NOTE: For DVP Coding Gudeline compliant code, do not use these types.
    //
    typedef char          *Address;        // Ready for address-arithmetic
    typedef char const    *ConstAddress;
    typedef unsigned char  Byte;           // Raw byte
//    typedef float          Float32;        // Single-precision float
//    typedef double         Float64;        // Double-precision float
    typedef void          *Pointer;        // Pointer to anonymous object
    typedef void const    *ConstPointer;
    typedef char const    *ConstString;
#else
    using namespace System;    
    typedef int             Int;
    typedef SByte           Int8;
    typedef Byte            UInt8;
    typedef float           Float32;    
    typedef unsigned int    Bool;
#endif


    typedef Int             Endian;
#define BigEndian       0
#define LittleEndian    1

    typedef struct tmVersion
    {
        UInt8   majorVersion;
        UInt8   minorVersion;
        UInt16  buildVersion;
    }   tmVersion_t, *ptmVersion_t;
#endif /*ndef _TMtypes_h*/

    /*Define DVP types that are not TCS types.*/

#ifndef TMFL_DOT_NET_2_0_TYPES
    typedef Int8   *pInt8;            //  8 bit   signed integer
    typedef Int16  *pInt16;           // 16 bit   signed integer
    typedef Int32  *pInt32;           // 32 bit   signed integer
    typedef UInt8  *pUInt8;           //  8 bit unsigned integer
    typedef UInt16 *pUInt16;          // 16 bit unsigned integer
    typedef UInt32 *pUInt32;          // 32 bit unsigned integer
    typedef void    Void, *pVoid;     // Void (typeless)
//    typedef Float  *pFloat;           // 32 bit floating point
//    typedef double  Double, *pDouble; // 32/64 bit floating point
    typedef Bool   *pBool;            // Boolean (True/False)
    typedef Char   *pChar;            // character, character array ptr
    typedef Int    *pInt;             // machine-natural integer
    typedef UInt   *pUInt;            // machine-natural unsigned integer
    typedef String *pString;          // Null terminated 8 bit char str,
#else
    typedef Void *pVoid;     // Void (typeless)
#endif

#if 0 /*added by Realtek 2011-11-24.*/
    /*Assume that 64-bit integers are supported natively by C99 compilers and Visual
    C version 6.00 and higher. More discrimination in this area may be added
    here as necessary.*/
#ifndef TMFL_DOT_NET_2_0_TYPES
#if defined __STDC_VERSION__ && __STDC_VERSION__ > 199409L
    /*This can be enabled only when all explicit references to the hi and lo
    structure members are eliminated from client code.*/
#define TMFL_NATIVE_INT64 1
    typedef signed   long long int Int64,  *pInt64;  // 64-bit integer
    typedef unsigned long long int UInt64, *pUInt64; // 64-bit bitmask
    // #elif defined _MSC_VER && _MSC_VER >= 1200
    // /*This can be enabled only when all explicit references to the hi and lo
    //     structure members are eliminated from client code.*/
    // #define TMFL_NATIVE_INT64 1
    // typedef signed   __int64 Int64,  *pInt64;  // 64-bit integer
    // typedef unsigned __int64 UInt64, *pUInt64; // 64-bit bitmask
#else /*!(defined __STDC_VERSION__ && __STDC_VERSION__ > 199409L)*/
#define TMFL_NATIVE_INT64 0
    typedef
    struct
    {
        /*Get the correct endianness (this has no impact on any other part of
        the system, but may make memory dumps easier to understand).*/
#if TMFL_ENDIAN == TMFL_ENDIAN_BIG
        Int32 hi; UInt32 lo;
#else
        UInt32 lo; Int32 hi;
#endif
    }
    Int64, *pInt64; // 64-bit integer
    typedef
    struct
    {
#if TMFL_ENDIAN == TMFL_ENDIAN_BIG
        UInt32 hi; UInt32 lo;
#else
        UInt32 lo; UInt32 hi;
#endif
    }
    UInt64, *pUInt64; // 64-bit bitmask
#endif /*defined __STDC_VERSION__ && __STDC_VERSION__ > 199409L*/
#endif /*TMFL_DOT_NET_2_0_TYPES*/
#endif /*added by Realtek 2011-11-24.*/

    // Maximum length of device name in all BSP and capability structures
#define HAL_DEVICE_NAME_LENGTH 16

    typedef CUInt32 tmErrorCode_t;
    typedef CUInt32 tmProgressCode_t;

    /* timestamp definition */
#ifndef TMFL_DOT_NET_2_0_TYPES
//    typedef UInt64 tmTimeStamp_t, *ptmTimeStamp_t; /*added by Realtek 2011-11-24.*/
#endif
    //for backwards compatibility with the older tmTimeStamp_t definition
#define ticks   lo
#define hiTicks hi

    typedef union tmColor3                 // 3 byte color structure
    {
        unsigned long u32;
#if (TMFL_ENDIAN == TMFL_ENDIAN_BIG)
        struct {
UInt32       : 8;
            UInt32 red   : 8;
            UInt32 green : 8;
            UInt32 blue  : 8;
        } rgb;
        struct {
UInt32   : 8;
            UInt32 y : 8;
            UInt32 u : 8;
            UInt32 v : 8;
        } yuv;
        struct {
UInt32   : 8;
            UInt32 u : 8;
            UInt32 m : 8;
            UInt32 l : 8;
        } uml;
#else
        struct {
            UInt32 blue  : 8;
            UInt32 green : 8;
            UInt32 red   : 8;
UInt32       : 8;
        } rgb;
        struct {
            UInt32 v : 8;
            UInt32 u : 8;
            UInt32 y : 8;
UInt32   : 8;
        } yuv;
        struct {
            UInt32 l : 8;
            UInt32 m : 8;
            UInt32 u : 8;
UInt32   : 8;
        } uml;
#endif
    }   tmColor3_t, *ptmColor3_t;

    typedef union tmColor4                 // 4 byte color structure
    {
        unsigned long u32;
#if (TMFL_ENDIAN == TMFL_ENDIAN_BIG)
        struct {
            UInt32 alpha    : 8;
            UInt32 red      : 8;
            UInt32 green    : 8;
            UInt32 blue     : 8;
        } argb;
        struct {
            UInt32 alpha    : 8;
            UInt32 y        : 8;
            UInt32 u        : 8;
            UInt32 v        : 8;
        } ayuv;
        struct {
            UInt32 alpha    : 8;
            UInt32 u        : 8;
            UInt32 m        : 8;
            UInt32 l        : 8;
        } auml;
#else
        struct {
            UInt32 blue     : 8;
            UInt32 green    : 8;
            UInt32 red      : 8;
            UInt32 alpha    : 8;
        } argb;
        struct {
            UInt32 v        : 8;
            UInt32 u        : 8;
            UInt32 y        : 8;
            UInt32 alpha    : 8;
        } ayuv;
        struct {
            UInt32 l        : 8;
            UInt32 m        : 8;
            UInt32 u        : 8;
            UInt32 alpha    : 8;
        } auml;
#endif
    }   tmColor4_t, *ptmColor4_t;

    //-----------------------------------------------------------------------------
    // Hardware device power states
    //
    typedef enum tmPowerState
    {
        tmPowerOn,                          // Device powered on      (D0 state)
        tmPowerStandby,                     // Device power standby   (D1 state)
        tmPowerSuspend,                     // Device power suspended (D2 state)
        tmPowerOff                          // Device powered off     (D3 state)

    }   tmPowerState_t, *ptmPowerState_t;

    //-----------------------------------------------------------------------------
    // Software Version Structure
    //
    typedef struct tmSWVersion
    {
        UInt32      compatibilityNr;        // Interface compatibility number
        UInt32      majorVersionNr;         // Interface major version number
        UInt32      minorVersionNr;         // Interface minor version number

    }   tmSWVersion_t, *ptmSWVersion_t;

    /*Under the TCS, <tm1/tmBoardDef.h> may have been included by our client. In
    order to avoid errors, we take account of this possibility, but in order to
    support environments where the TCS is not available, we do not include the
    file by name.*/
#ifndef _TMBOARDDEF_H_
#define _TMBOARDDEF_H_

    //-----------------------------------------------------------------------------
    // HW Unit Selection
    //
    typedef CInt tmUnitSelect_t, *ptmUnitSelect_t;

#define tmUnitNone (-1)
#define tmUnit0    0
#define tmUnit1    1
#define tmUnit2    2
#define tmUnit3    3
#define tmUnit4    4

    /*+compatibility*/
#define unitSelect_t       tmUnitSelect_t
#define unit0              tmUnit0
#define unit1              tmUnit1
#define unit2              tmUnit2
#define unit3              tmUnit3
#define unit4              tmUnit4
#define DEVICE_NAME_LENGTH HAL_DEVICE_NAME_LENGTH
    /*-compatibility*/

#endif /*ndef _TMBOARDDEF_H_ */

    //-----------------------------------------------------------------------------
    // Instance handle
    //
    typedef Int tmInstance_t, *ptmInstance_t;

#ifndef TMFL_DOT_NET_2_0_TYPES
    // Callback function declaration
    typedef Void (*ptmCallback_t) (UInt32 events, Void *pData, UInt32 userData);
#define tmCallback_t ptmCallback_t /*compatibility*/
#endif
    //-----------------------------------------------------------------------------
    // INLINE keyword for inline functions in all environments
    //
    // WinNT/WinCE: Use TMSHARED_DATA_BEGIN/TMSHARED_DATA_END for multiprocess
    //  shared data on a single CPU.  To define data variables that are shared
    //  across all processes for WinNT/WinCE, use the defined #pragma macros
    //  TMSHARED_DATA_BEGIN/TMSHARED_DATA_END and initialize the data variables
    //  as shown in the example below.  Data defined outside of the begin/end
    //  section or not initialized will not be shared across all processes for
    //  WinNT/WinCE; there will be a separate instance of the variable in each
    //  process.  Use WinNT Explorer "QuickView" on the target DLL or text edit
    //  the target DLL *.map file to verify the shared data section presence and
    //  size (shared/static variables will not be named in the MAP file but will
    //  be included in the shared section virtual size).
    // NOTE: All data variables in the multiprocess section _MUST_BE_INITIALIZED_
    //       to be shared across processes; if no explicit initialization is
    //       done, the data variables will not be shared across processes.  This
    //       shared data mechanism only applies to WinNT/WinCE multiprocess data
    //       on a single CPU (pSOS shares all data across tasks by default).  Use
    //       the TMML MP shared data region for shared data across multiple CPUs
    //       and multiple processes.  Example (note global variable naming):
    //
    //  #if     (TMFL_OS_IS_CE || TMFL_OS_IS_NT)
    //  #pragma TMSHARED_DATA_BEGIN         // Multiprocess shared data begin
    //  #endif
    //
    //  static g<Multiprocess shared data variable> = <Initialization value>;
    //         gtm<Multiprocess shared data variable> = <Initialization value>;
    //
    //  #if     (TMFL_OS_IS_CE || TMFL_OS_IS_NT)
    //  #pragma TMSHARED_DATA_END           // Multiprocess shared data end
    //  #endif
    //

#if        TMFL_OS_IS_CE || TMFL_OS_IS_NT
#ifndef inline
#define inline  __inline
#endif

    //
    // Places shared data in named DLL data segment for WinNT/WinCE builds.
    // NOTE: These pragma defines require DLLFLAGS += -section:.tmShare,RWS in the
    //       nt.mak and ce.mak target OS makefiles for this mechanism to work.
    //
#define TMSHARED_DATA_BEGIN     data_seg(".tmShare")
#define TMSHARED_DATA_END       data_seg()

#elif      TMFL_OS_IS_PSOS && TMFL_CPU_IS_MIPS

    // NOTE regarding the keyword INLINE:
    //
    // Inline is not an ANSI-C keyword, hence every compiler can implement inlining
    // the way it wants to. When using the dcc compiler this might possibly lead to
    // redeclaration warnings when linking. For example:
    //
    //      dld: warning: Redeclaration of tmmlGetMemHandle
    //      dld:    Defined in root.o
    //      dld:    and        tmmlApi.o(../../lib/pSOS-MIPS/libtmml.a)
    //
    // For the dcc compiler inlining is not on by default. When building a retail
    // version ( _TMTGTREL=ret), inlining is turned on explicitly in the dvp1 pSOS
    // makefiles by specifying -XO, which enables all standard optimizations plus
    // some others, including inlining (see the Language User's Manual, D-CC and
    // D-C++ Compiler Suites p46). When building a debug version ( _TMTGTREL=dbg),
    // the optimizations are not turned on (and even if they were they would have
    // been overruled by -g anyway).
    //
    // When a .h file with inline function declarations gets included in multiple
    // source files, redeclaration warnings are issued.
    //
    // When building a retail version those functions are inlined, but in addition
    // the function is also declared within the .o file, resulting in redeclaration
    // warnings as the same function is also defined by including that same header
    // file in other source files. Defining the functions as static inline rather
    // than inline solves the problem, as now the additional function declaration
    // is omitted (as now the compiler realizes that there is no point in keeping
    // that declaration as it can only be called from within this specific file,
    // but it isn't, because all calls are already inline).
    //
    // When building a debug version no inlining is done, but the functions are
    // still defined within the .o file, again resulting in redeclaration warnings.
    // Again, defining the functions to be static inline rather than inline solves
    // the problem.

    // Now if we would change the definition of the inline keyword for pSOS from
    // __inline__ to static __inline__, all inline function definitions throughout
    // the code would not issue redeclaration warnings anymore, but all static
    // inline definitions would.
    // If we don't change the definition of the inline keyword, all inline func-
    // tion definitions would return redeclaration warnings.
    //
    // As this is a pSOS linker bug, it was decided not to change the code but
    // rather to ignore the issued warnings.
    //

#define inline  __inline__

#elif      TMFL_OS_IS_PSOS && TMFL_CPU_IS_TM
    // TriMedia keyword is already inline

#elif      TMFL_OS_IS_ECOS && TMFL_CPU_IS_MIPS

#define inline  __inline__

// #elif      (TMFL_OS_IS_HPUNIX || TMFL_OS_IS_NULLOS)
    //
    // TMFL_OS_IS_HPUNIX is the HP Unix workstation target OS environment for the
    // DVP SDE2 using the GNU gcc toolset.  It is the same as TMFL_OS_IS_NULLOS
    // (which is inaccurately named because it is the HP Unix CPU/OS target
    // environment).
    //
    /* LM; 02/07/2202; to be able to use Insure, I modify the definition of inline */
    /* #define inline  __inline__ */
// #define inline  

#elif TMFL_OS_IS_ECOS && TMFL_CPU_IS_MIPS

#define inline

#else // TMFL_OS_IS_???

#error confusing value in TMFL_OS!

#endif // TMFL_OS_IS_XXX

    /*Assume that |restrict| is supported by tmcc and C99 compilers only. More
    discrimination in this area may be added here as necessary.*/
#if !(defined __TCS__ ||                                                       \
    (defined __STDC_VERSION__ && __STDC_VERSION__ > 199409L))
#define restrict /**/
#endif

#ifdef __cplusplus
}
#endif

#endif //ndef TMNXTYPES_H























// NXP source code - .\inc\tmCompId.h


/*==========================================================================*/
/*     (Copyright (C) 2003 Koninklijke Philips Electronics N.V.             */
/*     All rights reserved.                                                 */
/*     This source code and any compilation or derivative thereof is the    */
/*     proprietary information of Koninklijke Philips Electronics N.V.      */
/*     and is confidential in nature.                                       */
/*     Under no circumstances is this software to be exposed to or placed   */
/*     under an Open Source License of any type without the expressed       */
/*     written permission of Koninklijke Philips Electronics N.V.           */
/*==========================================================================*/
//-----------------------------------------------------------------------------
// MoReUse - 2001-11-12  Continuus Version 16
//
// Added
//   CID_COMP_TRICK
//   CID_COMP_TODISK
//   CID_COMP_FROMDISK
// 
// Removed
// CID_COMP_RTC  Twice the same request - duplicate removed          
//
// (C) Copyright 2000-2001 Koninklijke Philips Electronics N.V.,
//     All rights reserved
//
// This source code and any compilation or derivative thereof is the sole
// property of Philips Corporation and is provided pursuant to a Software
// License Agreement.  This code is the proprietary information of Philips
// Corporation and is confidential in nature.  Its use and dissemination by
// any party other than Philips Corporation is strictly limited by the
// confidential information provisions of the Agreement referenced above.
//-----------------------------------------------------------------------------
// FILE NAME:    tmCompId.h
//
// DESCRIPTION:  This header file identifies the standard component identifiers
//               for DVP platforms.  The objective of DVP component IDs is to
//               enable unique identification of software components at all
//               classes, types, and layers.  In addition, standard status
//               values are also defined to make determination of typical error
//               cases much easier.  The component identifier bitfields are
//               four bit aligned to ease in reading the hexadecimal value.
//
//               The process to create a new component ID follows the sequence
//               of steps:
//
//               1) Select a component class:  The class is the most general
//                  component classification.  If none of the classifications
//                  match and the component can still be uniquely identified
//                  by its type/tag/layer combination, use CID_CLASS_NONE.
//                  For customers, the CID_CLASS_CUSTOMER value is defined.
//                  If that value is used in the CID_CLASS_BITMASK field,
//                  all other bits in the component ID are user defined.
//
//               2) Select a component type:   The component type is used to
//                  classify how a component processes data.  Components may
//                  have only output pins (source), only input pins (sink),
//                  input and output pins with or without data modifications
//                  (filter), control of data flow without input/output pins
//                  (control), data storage/access/retrieval (database),
//                  or component group (subsystem).  If the component does
//                  not fit into any type classification, use CID_TYPE_NONE.
//
//               3) Create a component ID:     The component ID is used to
//                  classify the specific type and/or attributes of a software
//                  component API interface.  The currently defined list should
//                  be scanned for a component match.  If no component match
//                  can be found, define a new component tag that descibes the
//                  component clearly.  Component name abbreviations/acronyms
//                  are generally used; build a name starting from left to
//                  right with the most general ('A' or 'AUD' for audio, 'V' or
//                  'VID' for video, etc.) to the most specific ('AC3' or 'MP3'
//                  as specific audio filter types) terms for the component.
//
//               NOTE: Component layer (CID_LAYER_XXX) and status (CID_ERR_XXX)
//                     values are defined within the software component APIs
//                     header files, not in this file.
//
// DOCUMENT REF: DVP Software Coding Guidelines specification
//               DVP/MoReUse Naming Conventions specification
//
// NOTES:        The 32 bit component identifier bitfields are defined in the
//               diagram below:
//
//           +-----------------------------------------  4 bit Component Class
//          /      +-----------------------------------  4 bit Component Type
//         /      /         +--------------------------  8 bit Component Tag
//        /      /         /         +-----------------  4 bit Component Layer
//       /      /         /         /            +----- 12 bit Component Status
//      /      /         /         /            /
//  |31  28|27  24|23        16|15  12|11               0| bit
//  +------+------+------------+------+------------------+
//  |Class | Type |ComponentTag|Layer |  Error/Progress  |
//  +------+------+------------+------+------------------+
//
//-----------------------------------------------------------------------------
//
#ifndef TM_COMP_ID_H //-------------------
#define TM_COMP_ID_H

//-----------------------------------------------------------------------------
// Standard include files:
//-----------------------------------------------------------------------------
//

//-----------------------------------------------------------------------------
// Project include files:
//-----------------------------------------------------------------------------
//

#ifdef __cplusplus
extern "C"
{
#endif

//-----------------------------------------------------------------------------
// Types and defines:
//-----------------------------------------------------------------------------
//

//-----------------------------------------------------------------------------
// TM_OK is the 32 bit global status value used by all DVP software components
//  to indicate successful function/operation status.  If a non-zero value is
//  returned as status, it should use the component ID formats defined.
//
#define TM_OK               0         // Global success return status


//
// NOTE: Component ID types are defined as unsigned 32 bit integers (UInt32).
//
//-----------------------------------------------------------------------------
// Component Class definitions (bits 31:28, 4 bits)
// NOTE: A class of 0x0 must not be defined to ensure that the overall 32 bit
//       component ID/status combination is always non-0 (no TM_OK conflict).
//
#define CID_CLASS_BITSHIFT  28                          // Component class bit shift
#define CID_CLASS_BITMASK   (0xF << CID_CLASS_BITSHIFT) // Class AND bitmsk
#define CID_GET_CLASS(compId)   ((compId & CID_CLASS_BITMASK) >> CID_CLASS_BITSHIFT)
//
#define CID_CLASS_NONE      (0x1 << CID_CLASS_BITSHIFT) // No class information
#define CID_CLASS_VIDEO     (0x2 << CID_CLASS_BITSHIFT) // Video data component
#define CID_CLASS_AUDIO     (0x3 << CID_CLASS_BITSHIFT) // Audio data component
#define CID_CLASS_GRAPHICS  (0x4 << CID_CLASS_BITSHIFT) // Graphics component
#define CID_CLASS_BUS       (0x5 << CID_CLASS_BITSHIFT) // In/out/control bus
#define CID_CLASS_INFRASTR  (0x6 << CID_CLASS_BITSHIFT) // Infrastructure comp.
                            // Up to 0xE = Philips reserved class IDs
#define CID_CLASS_CUSTOMER  (0xF << CID_CLASS_BITSHIFT) // Customer rsvd class


//-----------------------------------------------------------------------------
// Component Type definitions (bits 27:24, 4 bits)
//
#define CID_TYPE_BITSHIFT   24                          // Component type bit shift
#define CID_TYPE_BITMASK    (0xF << CID_TYPE_BITSHIFT)  // Type AND bitmask
#define CID_GET_TYPE(compId)    ((compId & CID_TYPE_BITMASK) >> CID_TYPE_BITSHIFT)
//
#define CID_TYPE_NONE       (0x0 << CID_TYPE_BITSHIFT)  // No data connections
#define CID_TYPE_SOURCE     (0x1 << CID_TYPE_BITSHIFT)  // Source, output pins
#define CID_TYPE_SINK       (0x2 << CID_TYPE_BITSHIFT)  // Sink,   input pins
#define CID_TYPE_ENCODER    (0x3 << CID_TYPE_BITSHIFT)  // Data encoder
#define CID_TYPE_DECODER    (0x4 << CID_TYPE_BITSHIFT)  // Data decoder
#define CID_TYPE_MUX        (0x5 << CID_TYPE_BITSHIFT)  // Data multiplexer
#define CID_TYPE_DEMUX      (0x6 << CID_TYPE_BITSHIFT)  // Data demultiplexer
#define CID_TYPE_DIGITIZER  (0x7 << CID_TYPE_BITSHIFT)  // Data digitizer
#define CID_TYPE_RENDERER   (0x8 << CID_TYPE_BITSHIFT)  // Data renderer
#define CID_TYPE_FILTER     (0x9 << CID_TYPE_BITSHIFT)  // Data filter/processr
#define CID_TYPE_CONTROL    (0xA << CID_TYPE_BITSHIFT)  // Data control/switch
#define CID_TYPE_DATABASE   (0xB << CID_TYPE_BITSHIFT)  // Data store/access
#define CID_TYPE_SUBSYSTEM  (0xC << CID_TYPE_BITSHIFT)  // MultiComp subsystem
#define CID_TYPE_CUSTOMER   (0xF << CID_TYPE_BITSHIFT)  // Customer Defined Type
                            // Up to 0xF = Philips reserved type IDs


//-----------------------------------------------------------------------------
// Component Tag definitions (bits 23:16, 8 bits)
// NOTE: Component tags are defined in groups, dependent on the class and type.
//
#define CID_TAG_BITSHIFT    16                          // Component tag bit shift
#define CID_TAG_BITMASK     (0xFF << CID_TAG_BITSHIFT)  // Comp tag AND bitmask
//
#define CID_TAG_NONE        (0x00 << CID_TAG_BITSHIFT)  // No tag information
                            // Up to 0xFF = Philips reserved component tags
#define CID_TAG_CUSTOMER    (0xE0 << CID_TAG_BITSHIFT)

#define TAG(number)         ((number) << CID_TAG_BITSHIFT) // Create tag from num

//-----------------------------------------------------------------------------
// Component Layer definitions (bits 15:12, 4 bits)
//
#define CID_LAYER_BITSHIFT  12                           // Component layer bit shift
#define CID_LAYER_BITMASK   (0xF << CID_LAYER_BITSHIFT)  // Layer AND bitmask
#define CID_GET_LAYER(compId)   ((compId & CID_LAYER_BITMASK) >> CID_LAYER_BITSHIFT)
//
#define CID_LAYER_NONE      (0x0 << CID_LAYER_BITSHIFT)  // No layer info
#define CID_LAYER_BTM       (0x1 << CID_LAYER_BITSHIFT)  // Boot manager layer
#define CID_LAYER_HWAPI     (0x2 << CID_LAYER_BITSHIFT)  // Hardware API layer
#define CID_LAYER_BSL       (0x3 << CID_LAYER_BITSHIFT)  // Board Supp. Lib lyr
#define CID_LAYER_DEVLIB    (0x4 << CID_LAYER_BITSHIFT)  // Device Library lyr
#define CID_LAYER_TMAL      (0x5 << CID_LAYER_BITSHIFT)  // Application layer
#define CID_LAYER_TMOL      (0x6 << CID_LAYER_BITSHIFT)  // OSDependent layer
#define CID_LAYER_CUSTOMER  (0xF << CID_LAYER_BITSHIFT)  // Customer Defined Layer
                            // Up to 0xF = Philips reserved layer IDs



//-----------------------------------------------------------------------------
// Component Identifier definitions (bits 31:12, 20 bits)
// NOTE: These DVP platform component identifiers are designed to be unique
//       within the system.  The component identifier encompasses the class
//       (CID_CLASS_XXX), type (CID_TYPE_XXX), tag, and layer (CID_LAYER_XXX)
//       fields to form the unique component identifier.  This allows any
//       error/progress status value to be identified as to its original
//       source, whether or not the source component's header file is present.
//       The standard error/progress status definitions should be used
//       whenever possible to ease status interpretation.  No layer
//       information is defined at this point; it should be OR'd into the API
//       status values defined in the API's header file.
//
#if     (CID_LAYER_NONE != 0)
#error  ERROR: DVP component identifiers require the layer type 'NONE' = 0 !
#endif

//
// Classless Types/Components (don't fit into other class categories)
//
#define CTYP_NOCLASS_NOTYPE     (CID_CLASS_NONE | CID_TYPE_NONE)
#define CTYP_NOCLASS_SOURCE     (CID_CLASS_NONE | CID_TYPE_SOURCE)
#define CTYP_NOCLASS_SINK       (CID_CLASS_NONE | CID_TYPE_SINK)
#define CTYP_NOCLASS_MUX        (CID_CLASS_NONE | CID_TYPE_MUX)
#define CTYP_NOCLASS_DEMUX      (CID_CLASS_NONE | CID_TYPE_DEMUX)
#define CTYP_NOCLASS_FILTER     (CID_CLASS_NONE | CID_TYPE_FILTER)
#define CTYP_NOCLASS_CONTROL    (CID_CLASS_NONE | CID_TYPE_CONTROL)
#define CTYP_NOCLASS_DATABASE   (CID_CLASS_NONE | CID_TYPE_DATABASE)
#define CTYP_NOCLASS_SUBSYS     (CID_CLASS_NONE | CID_TYPE_SUBSYSTEM)
//
#define CID_COMP_CLOCK          (TAG(0x01) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_DMA            (TAG(0x02) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_PIC            (TAG(0x03) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_NORFLASH       (TAG(0x04) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_NANDFLASH      (TAG(0x05) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_GPIO           (TAG(0x06) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_SMARTCARD      (TAG(0x07) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_UDMA           (TAG(0x08) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_DSP            (TAG(0x09) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_TIMER          (TAG(0x0A) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_TSDMA          (TAG(0x0B) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_MMIARB         (TAG(0x0C) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_EEPROM         (TAG(0x0D) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_PARPORT        (TAG(0x0E) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_VSS            (TAG(0x0F) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_TSIO           (TAG(0x10) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_DBG            (TAG(0x11) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_TTE            (TAG(0x12) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_AVPROP         (TAG(0x13) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_BLASTER        (TAG(0x14) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_CAPTURE        (TAG(0x15) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_STP            (TAG(0x16) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_SYN            (TAG(0x17) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_TTX            (TAG(0x18) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_MIU            (TAG(0x19) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_INTDRV         (TAG(0x1A) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_RESET          (TAG(0x1B) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_CONFIG         (TAG(0x1C) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_VCTRL          (TAG(0x1D) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_TUNER          (TAG(0x1E) | CTYP_NOCLASS_NOTYPE)
#define CID_COMP_DEMOD          (TAG(0x1F) | CTYP_NOCLASS_NOTYPE)


#define CID_COMP_FREAD          (TAG(0x01) | CTYP_NOCLASS_SOURCE)
#define CID_COMP_CDRREAD        (TAG(0x02) | CTYP_NOCLASS_SOURCE)
#define CID_COMP_VSB            (TAG(0x03) | CTYP_NOCLASS_SOURCE)
#define CID_COMP_ANALOGTVTUNER  (TAG(0x04) | CTYP_NOCLASS_SOURCE)
#define CID_COMP_TPINMPEG2      (TAG(0x05) | CTYP_NOCLASS_SOURCE)
#define CID_COMP_DREAD          (TAG(0x06) | CTYP_NOCLASS_SOURCE)
#define CID_COMP_TREAD          (TAG(0x07) | CTYP_NOCLASS_SOURCE)
#define CID_COMP_RTC            (TAG(0x08) | CTYP_NOCLASS_SOURCE)
#define CID_COMP_TOUCHC         (TAG(0x09) | CTYP_NOCLASS_SOURCE)
#define CID_COMP_KEYPAD         (TAG(0x0A) | CTYP_NOCLASS_SOURCE)
#define CID_COMP_ADC            (TAG(0x0B) | CTYP_NOCLASS_SOURCE)
#define CID_COMP_READLIST       (TAG(0x0C) | CTYP_NOCLASS_SOURCE)
#define CID_COMP_FROMDISK       (TAG(0x0D) | CTYP_NOCLASS_SOURCE)

#define CID_COMP_FWRITE         (TAG(0x01) | CTYP_NOCLASS_SINK)
#define CID_COMP_CDWRITE        (TAG(0x02) | CTYP_NOCLASS_SINK)
#define CID_COMP_CHARLCD        (TAG(0x03) | CTYP_NOCLASS_SINK)
#define CID_COMP_PWM            (TAG(0x04) | CTYP_NOCLASS_SINK)
#define CID_COMP_DAC            (TAG(0x05) | CTYP_NOCLASS_SINK)
#define CID_COMP_TSDMAINJECTOR  (TAG(0x06) | CTYP_NOCLASS_SINK)
#define CID_COMP_TODISK         (TAG(0x07) | CTYP_NOCLASS_SINK)

#define CID_COMP_MUXMPEGPS      (TAG(0x01) | CTYP_NOCLASS_MUX)
#define CID_COMP_MUXMPEG        (TAG(0x02) | CTYP_NOCLASS_MUX)

#define CID_COMP_DEMUXMPEGTS    (TAG(0x01) | CTYP_NOCLASS_DEMUX)
#define CID_COMP_DEMUXMPEGPS    (TAG(0x02) | CTYP_NOCLASS_DEMUX)

#define CID_COMP_COPYIO         (TAG(0x01) | CTYP_NOCLASS_FILTER)
#define CID_COMP_COPYINPLACE    (TAG(0x02) | CTYP_NOCLASS_FILTER)
#define CID_COMP_UART           (TAG(0x03) | CTYP_NOCLASS_FILTER)
#define CID_COMP_SSI            (TAG(0x04) | CTYP_NOCLASS_FILTER)
#define CID_COMP_MODEMV34       (TAG(0x05) | CTYP_NOCLASS_FILTER)
#define CID_COMP_MODEMV42       (TAG(0x06) | CTYP_NOCLASS_FILTER)
#define CID_COMP_HTMLPARSER     (TAG(0x07) | CTYP_NOCLASS_FILTER)
#define CID_COMP_VMSP           (TAG(0x08) | CTYP_NOCLASS_FILTER)
#define CID_COMP_X              (TAG(0x09) | CTYP_NOCLASS_FILTER)
#define CID_COMP_TXTSUBTDECEBU  (TAG(0x0A) | CTYP_NOCLASS_FILTER)
#define CID_COMP_CPI            (TAG(0x0B) | CTYP_NOCLASS_FILTER)
#define CID_COMP_TRICK          (TAG(0x0C) | CTYP_NOCLASS_FILTER)

#define CID_COMP_REMCTL5        (TAG(0x01) | CTYP_NOCLASS_CONTROL)
#define CID_COMP_INFRARED       (TAG(0x02) | CTYP_NOCLASS_CONTROL)

#define CID_COMP_PSIP           (TAG(0x01) | CTYP_NOCLASS_DATABASE)
#define CID_COMP_IDE            (TAG(0x02) | CTYP_NOCLASS_DATABASE)
#define CID_COMP_DISKSCHED      (TAG(0x03) | CTYP_NOCLASS_DATABASE)
#define CID_COMP_AVFS           (TAG(0x04) | CTYP_NOCLASS_DATABASE)
#define CID_COMP_MDB            (TAG(0x05) | CTYP_NOCLASS_DATABASE)

#define CID_COMP_IRDMMPEG       (TAG(0x01) | CTYP_NOCLASS_SUBSYS)
#define CID_COMP_STORSYS        (TAG(0x02) | CTYP_NOCLASS_SUBSYS)

//
// Video Class Types/Components (video types handle video/graphics data)
//
#define CTYP_VIDEO_SINK         (CID_CLASS_VIDEO | CID_TYPE_SINK)
#define CTYP_VIDEO_SOURCE       (CID_CLASS_VIDEO | CID_TYPE_SOURCE)
#define CTYP_VIDEO_ENCODER      (CID_CLASS_VIDEO | CID_TYPE_ENCODER)
#define CTYP_VIDEO_DECODER      (CID_CLASS_VIDEO | CID_TYPE_DECODER)
#define CTYP_VIDEO_DIGITIZER    (CID_CLASS_VIDEO | CID_TYPE_DIGITIZER)
#define CTYP_VIDEO_RENDERER     (CID_CLASS_VIDEO | CID_TYPE_RENDERER)
#define CTYP_VIDEO_FILTER       (CID_CLASS_VIDEO | CID_TYPE_FILTER)
#define CTYP_VIDEO_SUBSYS       (CID_CLASS_VIDEO | CID_TYPE_SUBSYSTEM)
//
#define CID_COMP_LCD            (TAG(0x01) | CTYP_VIDEO_SINK)

#define CID_COMP_VCAPVI         (TAG(0x01) | CTYP_VIDEO_SOURCE)
#define CID_COMP_VIP            (TAG(0x02) | CTYP_VIDEO_SOURCE)
#define CID_COMP_VI             (TAG(0x03) | CTYP_VIDEO_SOURCE)
#define CID_COMP_VSLICER        (TAG(0x04) | CTYP_VIDEO_SOURCE)
#define CID_COMP_FBREAD         (TAG(0x05) | CTYP_VIDEO_SOURCE)
#define CID_COMP_QVI            (TAG(0x06) | CTYP_VIDEO_SOURCE)
#define CID_COMP_CAMERA         (TAG(0x07) | CTYP_VIDEO_SOURCE)

#define CID_COMP_VENCM1         (TAG(0x01) | CTYP_VIDEO_ENCODER)
#define CID_COMP_VENCM2         (TAG(0x02) | CTYP_VIDEO_ENCODER)
#define CID_COMP_VENCMJ         (TAG(0x03) | CTYP_VIDEO_ENCODER)
#define CID_COMP_VENCH263       (TAG(0x04) | CTYP_VIDEO_ENCODER)
#define CID_COMP_VENCH261       (TAG(0x05) | CTYP_VIDEO_ENCODER)

#define CID_COMP_VDECM1         (TAG(0x01) | CTYP_VIDEO_DECODER)
#define CID_COMP_VDECM2         (TAG(0x02) | CTYP_VIDEO_DECODER)
#define CID_COMP_VDECMPEG       (TAG(0x03) | CTYP_VIDEO_DECODER)
#define CID_COMP_VDECMJ         (TAG(0x04) | CTYP_VIDEO_DECODER)
#define CID_COMP_VDECSUBPICSVCD (TAG(0x05) | CTYP_VIDEO_DECODER)
#define CID_COMP_VDECH263       (TAG(0x06) | CTYP_VIDEO_DECODER)
#define CID_COMP_VDECH261       (TAG(0x07) | CTYP_VIDEO_DECODER)
#define CID_COMP_VDEC           (TAG(0x08) | CTYP_VIDEO_DECODER)
#define CID_COMP_VDECSUBPICDVD  (TAG(0x09) | CTYP_VIDEO_DECODER)
#define CID_COMP_VDECSUBPICBMPDVD  (TAG(0x0A) | CTYP_VIDEO_DECODER)
#define CID_COMP_VDECSUBPICRENDDVD (TAG(0x0B) | CTYP_VIDEO_DECODER)
#define CID_COMP_M4PP           (TAG(0x0C) | CTYP_VIDEO_DECODER)
#define CID_COMP_M4MC           (TAG(0x0D) | CTYP_VIDEO_DECODER)
#define CID_COMP_M4CSC          (TAG(0x0E) | CTYP_VIDEO_DECODER)

#define CID_COMP_VDIG           (TAG(0x01) | CTYP_VIDEO_DIGITIZER)
#define CID_COMP_VDIGVIRAW      (TAG(0x02) | CTYP_VIDEO_DIGITIZER)

#define CID_COMP_VREND          (TAG(0x01) | CTYP_VIDEO_RENDERER)
#define CID_COMP_HDVO           (TAG(0x02) | CTYP_VIDEO_RENDERER)
#define CID_COMP_VRENDGFXVO     (TAG(0x03) | CTYP_VIDEO_RENDERER)
#define CID_COMP_AICP           (TAG(0x04) | CTYP_VIDEO_RENDERER)
#define CID_COMP_VRENDVORAW     (TAG(0x05) | CTYP_VIDEO_RENDERER)
#define CID_COMP_VO             (TAG(0x06) | CTYP_VIDEO_RENDERER)
#define CID_COMP_QVO            (TAG(0x06) | CTYP_VIDEO_RENDERER)
#define CID_COMP_VRENDVOICP     (TAG(0x07) | CTYP_VIDEO_RENDERER)
#define CID_COMP_VMIX           (TAG(0x08) | CTYP_VIDEO_RENDERER)
#define CID_COMP_GFX           (TAG(0x09) | CTYP_VIDEO_RENDERER)

#define CID_COMP_MBS            (TAG(0x01) | CTYP_VIDEO_FILTER)
#define CID_COMP_VTRANS         (TAG(0x02) | CTYP_VIDEO_FILTER)
#define CID_COMP_QNM            (TAG(0x03) | CTYP_VIDEO_FILTER)
#define CID_COMP_ICP            (TAG(0x04) | CTYP_VIDEO_FILTER)
#define CID_COMP_VTRANSNM       (TAG(0x05) | CTYP_VIDEO_FILTER)
#define CID_COMP_QFD            (TAG(0x06) | CTYP_VIDEO_FILTER) // film detector
#define CID_COMP_VRENDDVDVO     (TAG(0x07) | CTYP_VIDEO_FILTER)
#define CID_COMP_VTRANSCRYSTAL  (TAG(0x08) | CTYP_VIDEO_FILTER)

#define CID_COMP_VSYSMT3        (TAG(0x01) | CTYP_VIDEO_SUBSYS) //obsolescent
#define CID_COMP_VSYSSTB        (TAG(0x01) | CTYP_VIDEO_SUBSYS)
#define CID_COMP_DVDVIDSYS      (TAG(0x02) | CTYP_VIDEO_SUBSYS)
#define CID_COMP_VDECUD         (TAG(0x03) | CTYP_VIDEO_SUBSYS)
#define CID_COMP_VIDSYS         (TAG(0x04) | CTYP_VIDEO_SUBSYS)
//
// Audio Class Types/Components (audio types primarily handle audio data)
//
#define CTYP_AUDIO_NONE         (CID_CLASS_AUDIO | CID_TYPE_NONE)
#define CTYP_AUDIO_SINK         (CID_CLASS_AUDIO | CID_TYPE_SINK)
#define CTYP_AUDIO_SOURCE       (CID_CLASS_AUDIO | CID_TYPE_SOURCE)
#define CTYP_AUDIO_ENCODER      (CID_CLASS_AUDIO | CID_TYPE_ENCODER)
#define CTYP_AUDIO_DECODER      (CID_CLASS_AUDIO | CID_TYPE_DECODER)
#define CTYP_AUDIO_DIGITIZER    (CID_CLASS_AUDIO | CID_TYPE_DIGITIZER)
#define CTYP_AUDIO_RENDERER     (CID_CLASS_AUDIO | CID_TYPE_RENDERER)
#define CTYP_AUDIO_FILTER       (CID_CLASS_AUDIO | CID_TYPE_FILTER)
#define CTYP_AUDIO_SUBSYS       (CID_CLASS_AUDIO | CID_TYPE_SUBSYSTEM)
//

#define CID_COMP_AI             (TAG(0x01) | CTYP_AUDIO_NONE)
#define CID_COMP_AO             (TAG(0x03) | CTYP_AUDIO_NONE)
#define CID_COMP_ADAI           (TAG(0x04) | CTYP_AUDIO_NONE)

#define CID_COMP_SDAC           (TAG(0x01) | CTYP_AUDIO_SINK)

#define CID_COMP_ADIGAI         (TAG(0x01) | CTYP_AUDIO_DIGITIZER)
#define CID_COMP_ADIGSPDIF      (TAG(0x02) | CTYP_AUDIO_DIGITIZER)

#define CID_COMP_ARENDAO        (TAG(0x01) | CTYP_AUDIO_RENDERER)
#define CID_COMP_ARENDSPDIF     (TAG(0x02) | CTYP_AUDIO_RENDERER)

#define CID_COMP_NOISESEQ       (TAG(0x03) | CTYP_AUDIO_SOURCE)

#define CID_COMP_AENCAC3        (TAG(0x01) | CTYP_AUDIO_ENCODER)
#define CID_COMP_AENCMPEG1      (TAG(0x02) | CTYP_AUDIO_ENCODER)
#define CID_COMP_AENCAAC        (TAG(0x03) | CTYP_AUDIO_ENCODER)
#define CID_COMP_AENCG723       (TAG(0x04) | CTYP_AUDIO_ENCODER)
#define CID_COMP_AENCG728       (TAG(0x05) | CTYP_AUDIO_ENCODER)
#define CID_COMP_AENCWMA        (TAG(0x06) | CTYP_AUDIO_ENCODER)

#define CID_COMP_ADECPROLOGIC   (TAG(0x01) | CTYP_AUDIO_DECODER)
#define CID_COMP_ADECAC3        (TAG(0x02) | CTYP_AUDIO_DECODER)
#define CID_COMP_ADECMPEG1      (TAG(0x03) | CTYP_AUDIO_DECODER)
#define CID_COMP_ADECMP3        (TAG(0x04) | CTYP_AUDIO_DECODER)
#define CID_COMP_ADECAAC        (TAG(0x05) | CTYP_AUDIO_DECODER)
#define CID_COMP_ADECG723       (TAG(0x06) | CTYP_AUDIO_DECODER)
#define CID_COMP_ADECG728       (TAG(0x07) | CTYP_AUDIO_DECODER)
#define CID_COMP_ADECWMA        (TAG(0x08) | CTYP_AUDIO_DECODER)
#define CID_COMP_ADECTHRU       (TAG(0x09) | CTYP_AUDIO_DECODER)
#define CID_COMP_ADEC           (TAG(0x0A) | CTYP_AUDIO_DECODER)

#define CID_COMP_ASPLIB         (TAG(0x01) | CTYP_AUDIO_FILTER)
#define CID_COMP_IIR            (TAG(0x02) | CTYP_AUDIO_FILTER)
#define CID_COMP_ASPEQ2         (TAG(0x03) | CTYP_AUDIO_FILTER)
#define CID_COMP_ASPEQ5         (TAG(0x04) | CTYP_AUDIO_FILTER)
#define CID_COMP_ASPBASSREDIR   (TAG(0x05) | CTYP_AUDIO_FILTER)
#define CID_COMP_ASPLAT2        (TAG(0x06) | CTYP_AUDIO_FILTER)
#define CID_COMP_ASPPLUGIN      (TAG(0x07) | CTYP_AUDIO_FILTER)
#define CID_COMP_AMIXDTV        (TAG(0x08) | CTYP_AUDIO_FILTER)
#define CID_COMP_AMIXSIMPLE     (TAG(0x09) | CTYP_AUDIO_FILTER)
#define CID_COMP_AMIXSTB        (TAG(0x0A) | CTYP_AUDIO_FILTER)
#define CID_COMP_ASPEQ          (TAG(0x0B) | CTYP_AUDIO_FILTER)
#define CID_COMP_ATESTSIG       (TAG(0x0C) | CTYP_AUDIO_FILTER)

#define CID_COMP_AUDSUBSYS      (TAG(0x01) | CTYP_AUDIO_SUBSYS)
#define CID_COMP_AUDSYSSTB      (TAG(0x02) | CTYP_AUDIO_SUBSYS)
#define CID_COMP_AUDSYSDVD      (TAG(0x03) | CTYP_AUDIO_SUBSYS)

//
// Graphics Class Types/Components
//
#define CTYP_GRAPHICS_RENDERER  (CID_CLASS_GRAPHICS | CID_TYPE_SINK)
//
#define CID_COMP_WM             (TAG(0x01) | CTYP_GRAPHICS_RENDERER)
#define CID_COMP_WIDGET         (TAG(0x02) | CTYP_GRAPHICS_RENDERER)
#define CID_COMP_OM             (TAG(0x03) | CTYP_GRAPHICS_RENDERER)
#define CID_COMP_HTMLRENDER     (TAG(0x04) | CTYP_GRAPHICS_RENDERER)
#define CID_COMP_VRENDEIA708    (TAG(0x05) | CTYP_GRAPHICS_RENDERER)
#define CID_COMP_VRENDEIA608    (TAG(0x06) | CTYP_GRAPHICS_RENDERER)
//
#define CTYP_GRAPHICS_DRAW      (CID_CLASS_GRAPHICS | CID_TYPE_NONE)
//
#define CID_COMP_DRAW           (TAG(0x10) | CTYP_GRAPHICS_DRAW)
#define CID_COMP_DRAW_UT        (TAG(0x11) | CTYP_GRAPHICS_DRAW)
#define CID_COMP_DRAW_DE        (TAG(0x12) | CTYP_GRAPHICS_DRAW)
#define CID_COMP_DRAW_REF       (TAG(0x13) | CTYP_GRAPHICS_DRAW)
#define CID_COMP_DRAW_TMH       (TAG(0x14) | CTYP_GRAPHICS_DRAW)
#define CID_COMP_DRAW_TMT       (TAG(0x15) | CTYP_GRAPHICS_DRAW)
#define CID_COMP_DRAW_TMTH      (TAG(0x16) | CTYP_GRAPHICS_DRAW)
//
#define CID_COMP_3D             (TAG(0x30) | CTYP_GRAPHICS_DRAW)
#define CID_COMP_JAWT           (TAG(0x31) | CTYP_GRAPHICS_DRAW)
#define CID_COMP_JINPUT         (TAG(0x32) | CTYP_GRAPHICS_DRAW)
#define CID_COMP_LWM            (TAG(0x33) | CTYP_GRAPHICS_DRAW)
#define CID_COMP_2D             (TAG(0x34) | CTYP_GRAPHICS_DRAW)


//
// Bus Class Types/Components (busses connect hardware components together)
//
#define CTYP_BUS_NOTYPE         (CID_CLASS_BUS | CID_TYPE_NONE)
//
#define CID_COMP_XIO            (TAG(0x01) | CTYP_BUS_NOTYPE)
#define CID_COMP_IIC            (TAG(0x02) | CTYP_BUS_NOTYPE)
#define CID_COMP_PCI            (TAG(0x03) | CTYP_BUS_NOTYPE)
#define CID_COMP_P1394          (TAG(0x04) | CTYP_BUS_NOTYPE)
#define CID_COMP_ENET           (TAG(0x05) | CTYP_BUS_NOTYPE)
#define CID_COMP_ATA            (TAG(0x06) | CTYP_BUS_NOTYPE)
#define CID_COMP_CAN            (TAG(0x07) | CTYP_BUS_NOTYPE)
#define CID_COMP_UCGDMA         (TAG(0x08) | CTYP_BUS_NOTYPE)
#define CID_COMP_I2S            (TAG(0x09) | CTYP_BUS_NOTYPE)
#define CID_COMP_SPI            (TAG(0x0A) | CTYP_BUS_NOTYPE)
#define CID_COMP_PCM            (TAG(0x0B) | CTYP_BUS_NOTYPE)
#define CID_COMP_L3             (TAG(0x0C) | CTYP_BUS_NOTYPE)

//
// Infrastructure Class Types/Components
#define CTYP_INFRASTR_NOTYPE    (CID_CLASS_INFRASTR | CID_TYPE_NONE)
//
#define CID_COMP_OSAL           (TAG(0x01) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_MML            (TAG(0x02) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_TSSA_DEFAULTS  (TAG(0x03) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_RPC            (TAG(0x04) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_THI            (TAG(0x05) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_REGISTRY       (TAG(0x06) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_TMMAN          (TAG(0x07) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_LDT            (TAG(0x08) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_CPUCONN        (TAG(0x09) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_COMMQUE        (TAG(0x0A) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_BSLMGR         (TAG(0x0B) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_CR             (TAG(0x0C) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_NODE           (TAG(0x0D) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_COM            (TAG(0x0E) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_UTIL           (TAG(0x0F) | CTYP_INFRASTR_NOTYPE)
#define CID_COMP_SGLIST         (TAG(0x10) | CTYP_INFRASTR_NOTYPE)

//-----------------------------------------------------------------------------
// Component Standard Error/Progress Status definitions (bits 11:0, 12 bits)
// NOTE: These status codes are OR'd with the component identifier to create
//       component unique 32 bit status values.  The component status values
//       should be defined in the header files where the APIs are defined.
//
#define CID_ERR_BITMASK                 0xFFF // Component error AND bitmask
#define CID_ERR_BITSHIFT                0     // Component error bit shift
#define CID_GET_ERROR(compId)   ((compId & CID_ERR_BITMASK) >> CID_ERR_BITSHIFT)
//
#define TM_ERR_COMPATIBILITY            0x001 // SW Interface compatibility
#define TM_ERR_MAJOR_VERSION            0x002 // SW Major Version error
#define TM_ERR_COMP_VERSION             0x003 // SW component version error
#define TM_ERR_BAD_MODULE_ID            0x004 // SW - HW module ID error
#define TM_ERR_BAD_UNIT_NUMBER          0x005 // Invalid device unit number
#define TM_ERR_BAD_INSTANCE             0x006 // Bad input instance value
#define TM_ERR_BAD_HANDLE               0x007 // Bad input handle
#define TM_ERR_BAD_INDEX                0x008 // Bad input index
#define TM_ERR_BAD_PARAMETER            0x009 // Invalid input parameter
#define TM_ERR_NO_INSTANCES             0x00A // No instances available
#define TM_ERR_NO_COMPONENT             0x00B // Component is not present
#define TM_ERR_NO_RESOURCES             0x00C // Resource is not available
#define TM_ERR_INSTANCE_IN_USE          0x00D // Instance is already in use
#define TM_ERR_RESOURCE_OWNED           0x00E // Resource is already in use
#define TM_ERR_RESOURCE_NOT_OWNED       0x00F // Caller does not own resource
#define TM_ERR_INCONSISTENT_PARAMS      0x010 // Inconsistent input params
#define TM_ERR_NOT_INITIALIZED          0x011 // Component is not initialized
#define TM_ERR_NOT_ENABLED              0x012 // Component is not enabled
#define TM_ERR_NOT_SUPPORTED            0x013 // Function is not supported
#define TM_ERR_INIT_FAILED              0x014 // Initialization failed
#define TM_ERR_BUSY                     0x015 // Component is busy
#define TM_ERR_NOT_BUSY                 0x016 // Component is not busy
#define TM_ERR_READ                     0x017 // Read error
#define TM_ERR_WRITE                    0x018 // Write error
#define TM_ERR_ERASE                    0x019 // Erase error
#define TM_ERR_LOCK                     0x01A // Lock error
#define TM_ERR_UNLOCK                   0x01B // Unlock error
#define TM_ERR_OUT_OF_MEMORY            0x01C // Memory allocation failed
#define TM_ERR_BAD_VIRT_ADDRESS         0x01D // Bad virtual address
#define TM_ERR_BAD_PHYS_ADDRESS         0x01E // Bad physical address
#define TM_ERR_TIMEOUT                  0x01F // Timeout error
#define TM_ERR_OVERFLOW                 0x020 // Data overflow/overrun error
#define TM_ERR_FULL                     0x021 // Queue (etc.) is full
#define TM_ERR_EMPTY                    0x022 // Queue (etc.) is empty
#define TM_ERR_NOT_STARTED              0x023 // Component stream not started
#define TM_ERR_ALREADY_STARTED          0x024 // Comp. stream already started
#define TM_ERR_NOT_STOPPED              0x025 // Component stream not stopped
#define TM_ERR_ALREADY_STOPPED          0x026 // Comp. stream already stopped
#define TM_ERR_ALREADY_SETUP            0x027 // Component already setup
#define TM_ERR_NULL_PARAMETER           0x028 // Null input parameter
#define TM_ERR_NULL_DATAINFUNC          0x029 // Null data input function
#define TM_ERR_NULL_DATAOUTFUNC         0x02A // Null data output function
#define TM_ERR_NULL_CONTROLFUNC         0x02B // Null control function
#define TM_ERR_NULL_COMPLETIONFUNC      0x02C // Null completion function
#define TM_ERR_NULL_PROGRESSFUNC        0x02D // Null progress function
#define TM_ERR_NULL_ERRORFUNC           0x02E // Null error handler function
#define TM_ERR_NULL_MEMALLOCFUNC        0x02F // Null memory alloc function
#define TM_ERR_NULL_MEMFREEFUNC         0x030 // Null memory free  function
#define TM_ERR_NULL_CONFIGFUNC          0x031 // Null configuration function
#define TM_ERR_NULL_PARENT              0x032 // Null parent data
#define TM_ERR_NULL_IODESC              0x033 // Null in/out descriptor
#define TM_ERR_NULL_CTRLDESC            0x034 // Null control descriptor
#define TM_ERR_UNSUPPORTED_DATACLASS    0x035 // Unsupported data class
#define TM_ERR_UNSUPPORTED_DATATYPE     0x036 // Unsupported data type
#define TM_ERR_UNSUPPORTED_DATASUBTYPE  0x037 // Unsupported data subtype
#define TM_ERR_FORMAT                   0x038 // Invalid/unsupported format
#define TM_ERR_INPUT_DESC_FLAGS         0x039 // Bad input  descriptor flags
#define TM_ERR_OUTPUT_DESC_FLAGS        0x03A // Bad output descriptor flags
#define TM_ERR_CAP_REQUIRED             0x03B // Capabilities required ???
#define TM_ERR_BAD_TMALFUNC_TABLE       0x03C // Bad TMAL function table
#define TM_ERR_INVALID_CHANNEL_ID       0x03D // Invalid channel identifier
#define TM_ERR_INVALID_COMMAND          0x03E // Invalid command/request
#define TM_ERR_STREAM_MODE_CONFUSION    0x03F // Stream mode config conflict
#define TM_ERR_UNDERRUN                 0x040 // Data underflow/underrun
#define TM_ERR_EMPTY_PACKET_RECVD       0x041 // Empty data packet received
#define TM_ERR_OTHER_DATAINOUT_ERR      0x042 // Other data input/output err
#define TM_ERR_STOP_REQUESTED           0x043 // Stop in progress
#define TM_ERR_PIN_NOT_STARTED          0x044 // Pin not started
#define TM_ERR_PIN_ALREADY_STARTED      0x045 // Pin already started
#define TM_ERR_PIN_NOT_STOPPED          0x046 // Pin not stopped
#define TM_ERR_PIN_ALREADY_STOPPED      0x047 // Pin already stopped
#define TM_ERR_STOP_PIN_REQUESTED       0x048 // Stop of a single pin is in progress (obsolescent)
#define TM_ERR_PAUSE_PIN_REQUESTED      0x048 // Stop of a single pin is in progress
#define TM_ERR_ASSERTION                0x049 // Assertion failure
#define TM_ERR_HIGHWAY_BANDWIDTH        0x04A // Highway bandwidth bus error
#define TM_ERR_HW_RESET_FAILED          0x04B // Hardware reset failed
#define TM_ERR_PIN_PAUSED               0x04C // Pin Paused

// Add new standard error/progress status codes here

#define TM_ERR_COMP_UNIQUE_START        0x800 // 0x800-0xDFF: Component unique

#define TM_ERR_CUSTOMER_START           0xC00  

// DVP Standard assert error code start offset
// NOTE: This define should be added to the component's base error value and
//       standard error code(s) to define assert error codes.  For example:
// #define TMBSL_ERR_MGR_ASSERT_BAD_PARAM 
//          (TMBSL_ERR_MGR_BASE + TM_ERR_ASSERT_START + TM_ERR_BAD_PARAMETER)
//
#define TM_ERR_ASSERT_START             0xE00 // 0xE00-0xEFF: Assert failures
#define TM_ERR_ASSERT_LAST              0xEFF // Last assert error range value
#define CID_IS_ASSERT_ERROR(compId)     \
    ((CID_GET_ERROR(compId) >= TM_ERR_ASSERT_START) && \
     (CID_GET_ERROR(compId) <= TM_ERR_ASSERT_LAST))

// DVP Standard fatal error code start offset
// NOTE: This define should be added to the component's base error value and
//       standard error code(s) to define fatal error codes.  For example:
// #define TMML_ERR_FATAL_OUT_OF_MEMORY  
//          (TMML_ERR_BASE + TM_ERR_FATAL_START + TM_ERR_OUT_OF_MEMORY)
//
#define TM_ERR_FATAL_START              0xF00 // 0xF00-0xFFF: Fatal failures
#define TM_ERR_FATAL_LAST               0xFFF // Last fatal error range value
#define CID_IS_FATAL_ERROR(compId)      \
    ((CID_GET_ERROR(compId) >= TM_ERR_FATAL_START) && \
     (CID_GET_ERROR(compId) <= TM_ERR_FATAL_LAST))


//-----------------------------------------------------------------------------
// DVP hardware module IDs
//
#define VMPG_100_MOD_ID         0x00000100
#define C1394_101_MOD_ID        0x00000101
#define FPBC_102_MOD_ID         0x00000102
#define JTAG_103_MOD_ID         0x00000103
#define EJTAG_104_MOD_ID        0x00000104
#define IIC_105_MOD_ID          0x00000105
#define SMCARD_106_MOD_ID       0x00000106
#define UART_107_MOD_ID         0x00000107
/* #define CLOCKS_108_MOD_ID       0x00000108 */
#define USB_109_MOD_ID          0x00000109
#define BOOT_10A_MOD_ID         0x0000010A
#define MPBC_10B_MOD_ID         0x0000010B
#define SSI_10C_MOD_ID          0x0000010C
#define AI_10D_MOD_ID           0x0000010D
#define VMSP_10E_MOD_ID         0x0000010E
#define GPIO_10F_MOD_ID         0x0000010F
#define SPDI_110_MOD_ID         0x00000110
#define AICP_111_MOD_ID         0x00000111
#define TPBC_112_MOD_ID         0x00000112
#define PCI_113_MOD_ID          0x00000113
#define MMI_114_MOD_ID          0x00000114
#define ORCA3_115_MOD_ID        0x00000115
#define DBG_116_MOD_ID          0x00000116
#define DE_117_MOD_ID           0x00000117
#define AICP_118_MOD_ID         0x00000118
#define MBS_119_MOD_ID          0x00000119
#define VIP_11A_MOD_ID          0x0000011A
#define PIMI_11B_MOD_ID         0x0000011B
#define PIB_11C_MOD_ID          0x0000011C
#define PIC_11D_MOD_ID          0x0000011D
#define TMDBG_11F_MOD_ID        0x0000011F
#define AO_120_MOD_ID           0x00000120
#define SPDO_121_MOD_ID         0x00000121
#define FPIMI_122_MOD_ID        0x00000122
#define RESET_123_MOD_ID        0x00000123
#define NULL_124_MOD_ID         0x00000124
#define TSDMA_125_MOD_ID        0x00000125
#define GLBREG_126_MOD_ID       0x00000126
#define TMDBG_127_MOD_ID        0x00000127
#define GLBREG_128_MOD_ID       0x00000128
#define DMA_130_MOD_ID          0x00000130
#define IR_131_MOD_ID           0x00000131
#define GFX2D_131_MOD_ID        0x00000132 // TODO: Remove after code corrected
#define GFX2D_132_MOD_ID        0x00000132
#define P1284_133_MOD_ID        0x00000133
#define QNM_134_MOD_ID          0x00000134
#define OSD_136_MOD_ID            0x00000136
#define MIX_137_MOD_ID            0x00000137
#define DENC_138_MOD_ID            0x00000138
#define SYN_13A_MOD_ID            0x0000013A
#define CLOCKS_13E_MOD_ID       0x0000013E
#define CONFIG_13F_MOD_ID       0x0000013F
#define MIU_A04C_MOD_ID            0x0000A04C
#define DISP_A04D_MOD_ID        0x0000A04D
#define VCTRL_A04E_MOD_ID       0x0000A04E


#define PR3930_2B10_MOD_ID      0x00002B10
#define PR3940_2B11_MOD_ID      0x00002B11

#define TM3218_2B80_MOD_ID      0x00002B80
#define TM64_2b81_MOD_ID        0x00002B81

#define QNM_A017_MOD_ID         0x0000A017

// There is no HW module ID for TS IO ROUTER.  We assign
// a SW module ID to this module, because it is needed by Clock 
// device and HWAPI libraries. Use 010Eh for lower word 
// (for lack of better reason ! because IO Router is closely
// associated with VMSP module)

#define IORT_1010E_MOD_ID       0x0001010E


#ifdef __cplusplus
}
#endif

#endif // TMCOMPID_H //-----------------























// NXP source code - .\inc\tmFrontEnd.h


/**
  Copyright (C) 2007 NXP B.V., All Rights Reserved.
  This source code and any compilation or derivative thereof is the proprietary
  information of NXP B.V. and is confidential in nature. Under no circumstances
  is this software to be  exposed to or placed under an Open Source License of
  any type without the expressed written permission of NXP B.V.
 *
 * \file          tmFrontEnd.h
 *                %version: CFR_STB#0.4.1.7 %
 *
 * \date          %date_modified%
 *
 * \brief         Describe briefly the purpose of this file.
 *
 * REFERENCE DOCUMENTS :
 *
 * Detailed description may be added here.
 *
 * \section info Change Information
 *
 * \verbatim
   Date          Modified by CRPRNr  TASKNr  Maintenance description
   -------------|-----------|-------|-------|-----------------------------------
    26-Mar-2008 | B.GUILLOT | 13122 | 23456 | Creation
   -------------|-----------|-------|-------|-----------------------------------
   \endverbatim
 *
*/


#ifndef TMFRONTEND_H
#define TMFRONTEND_H

/*============================================================================*/
/*                       INCLUDE FILES                                        */
/*============================================================================*/


#ifdef __cplusplus
extern "C" {
#endif



/*============================================================================*/
/*                       ENUM OR TYPE DEFINITION                              */
/*============================================================================*/
#define TMFRONTEND_DVBT2MAXPLPNB 250

/* standard*/
typedef enum _tmFrontEndStandard_t {
    tmFrontEndStandardDVBT,
    tmFrontEndStandardDVBS,
    tmFrontEndStandardDVBC,
    tmFrontEndStandardDSS,
    tmFrontEndStandardBSD,
    tmFrontEndStandardDVBH,
    tmFrontEndStandardAnalogDVBT,
    tmFrontEndStandardDVBS2,
    tmFrontEndStandardDVBT2,
    tmFrontEndStandardMax
} tmFrontEndStandard_t, *ptmFrontEndStandard_t;

/* spectral inversion*/
typedef enum _tmFrontEndSpecInv_t {
    tmFrontEndSpecInvAuto = 0,
    tmFrontEndSpecInvOff,
    tmFrontEndSpecInvOn,
    tmFrontEndSpecInvMax
} tmFrontEndSpecInv_t, *ptmFrontEndSpecInv_t;

/* modulation*/
typedef enum _tmFrontEndModulation_t {
    tmFrontEndModulationAuto = 0,
    tmFrontEndModulationBpsk,
    tmFrontEndModulationQpsk,
    tmFrontEndModulationQam4,
    tmFrontEndModulationPsk8,
    tmFrontEndModulationQam16,
    tmFrontEndModulationQam32,
    tmFrontEndModulationQam64,
    tmFrontEndModulationQam128,
    tmFrontEndModulationQam256,
    tmFrontEndModulationQam512,
    tmFrontEndModulationQam1024,
    tmFrontEndModulation8VSB,
    tmFrontEndModulation16VSB,
    tmFrontEndModulationQam,
    tmFrontEndModulationMax
} tmFrontEndModulation_t, *ptmFrontEndModulation_t;

/* viterbi rate*/
typedef enum _tmFrontEndCodeRate_t {
    tmFrontEndCodeRateAuto = 0,
    tmFrontEndCodeRate_1_4,
    tmFrontEndCodeRate_1_3,
    tmFrontEndCodeRate_2_5,
    tmFrontEndCodeRate_1_2,
    tmFrontEndCodeRate_3_5,
    tmFrontEndCodeRate_2_3,
    tmFrontEndCodeRate_3_4,
    tmFrontEndCodeRate_4_5,
    tmFrontEndCodeRate_5_6,
    tmFrontEndCodeRate_6_7,
    tmFrontEndCodeRate_7_8,
    tmFrontEndCodeRate_8_9,
    tmFrontEndCodeRate_9_10,
    tmFrontEndCodeRate_NotRelevant,
    tmFrontEndCodeRateMax
} tmFrontEndCodeRate_t, *ptmFrontEndCodeRate_t;

/* frequency offset*/
typedef enum _tmFrontEndRfOffset_t {
    tmFrontEndRfOffsetAuto = 0,
    tmFrontEndRfOffsetNull,
    tmFrontEndRfOffsetPlus125,
    tmFrontEndRfOffsetMinus125,
    tmFrontEndRfOffsetPlus166,
    tmFrontEndRfOffsetMinus166,
    tmFrontEndRfOffsetPlus333,
    tmFrontEndRfOffsetMinus333,
    tmFrontEndRfOffsetPlus500,
    tmFrontEndRfOffsetMinus500,
    tmFrontEndRfOffsetMax
} tmFrontEndRfOffset_t, *ptmFrontEndRfOffset_t;

/* frequency offset*/
typedef enum _tmFrontEndRfOffsetMode_t {
    tmFrontEndRfOffsetModeAuto,
    tmFrontEndRfOffsetModeManual,
    tmFrontEndRfOffsetModeMax
} tmFrontEndRfOffsetMode_t, *ptmFrontEndRfOffsetMode_t;

/* guard interval*/
typedef enum _tmFrontEndGI_t {
    tmFrontEndGIAuto = 0,
    tmFrontEndGI_1_32,
    tmFrontEndGI_1_16,
    tmFrontEndGI_1_8,
    tmFrontEndGI_1_4,
    tmFrontEndGI_1_128,
    tmFrontEndGI_19_128,
    tmFrontEndGI_19_256,
    tmFrontEndGIMax
} tmFrontEndGI_t, *ptmFrontEndGI_t;

/* fast Fourrier transform size*/
typedef enum _tmFrontEndFft_t {
    tmFrontEndFftAuto = 0,
    tmFrontEndFft2K,
    tmFrontEndFft8K,
    tmFrontEndFft4K,
    tmFrontEndFft16K,
    tmFrontEndFft32K,
    tmFrontEndFftMax
} tmFrontEndFft_t, *ptmFrontEndFft_t;

/* hierarchy*/
typedef enum _tmFrontEndHier_t {
    tmFrontEndHierAuto = 0,
    tmFrontEndHierNo,
    tmFrontEndHierAlpha1,
    tmFrontEndHierAlpha2,
    tmFrontEndHierAlpha4,
    tmFrontEndHierMax
} tmFrontEndHier_t, *ptmFrontEndHier_t;

/* priority*/
typedef enum _tmFrontEndPrior_t {
    tmFrontEndPriorAuto = 0,
    tmFrontEndPriorHigh,
    tmFrontEndPriorLow,
    tmFrontEndPriorMax
} tmFrontEndPrior_t, *ptmFrontEndPrior_t;

/* roll off */
typedef enum _tmFrontEndRollOff_t {
    tmFrontEndRollOffAuto = 0,
    tmFrontEndRollOff_0_15,
    tmFrontEndRollOff_0_20,
    tmFrontEndRollOff_0_25,
    tmFrontEndRollOff_0_35,
    tmFrontEndRollOffMax
} tmFrontEndRollOff_t, *ptmFrontEndRollOff_t;

/* LNB polarity */
typedef enum _tmFrontEndLNBPolarity_t {
    tmFrontEndLNBPolarityAuto = 0,
    tmFrontEndLNBPolarityHigh,
    tmFrontEndLNBPolarityLow,
    tmFrontEndLNBPolarityMax
} tmFrontEndLNBPolarity_t, *ptmFrontEndLNBPolarity_t;

/* continuous tone */
typedef enum _tmFrontEndContinuousTone_t {
    tmFrontEndContinuousToneAuto = 0,
    tmFrontEndContinuousToneOff,
    tmFrontEndContinuousTone22KHz,
    tmFrontEndContinuousToneMax
} tmFrontEndContinuousTone_t, *ptmFrontEndContinuousTone_t;

typedef enum _tmFrontEndChannelType_t
{
    tmFrontEndChannelTypeNone    = 0x00,  /* No detection         */
    tmFrontEndChannelTypeDigital = 0x01,  /* Digital channel      */
    tmFrontEndChannelTypeAnalog  = 0x02,  /* Analog channel       */
    tmFrontEndChannelTypeUnknown = 0x20   /* unknown channel type */
} tmFrontEndChannelType_t;

typedef enum _tmFrontEndChannelConfidence_t
{
    tmFrontEndConfidenceNotAvailable,
    tmFrontEndConfidenceNull,
    tmFrontEndConfidenceLow,
    tmFrontEndConfidenceMedium,
    tmFrontEndConfidenceHigh
} tmFrontEndConfidence_t;

typedef enum _tmFrontEndDVBT2PLPType_t
{
    tmFrontEndDVBT2PLPTypeAuto,
    tmFrontEndDVBT2PLPTypeCommon,
    tmFrontEndDVBT2PLPType1,
    tmFrontEndDVBT2PLPType2,
    tmFrontEndDVBT2PLPTypeMax
} tmFrontEndDVBT2PLPType_t;

typedef enum _tmFrontEndDVBT2PLPPayloadType_t
{
    tmFrontEndDVBT2PLPPayloadTypeAuto,
    tmFrontEndDVBT2PLPPayloadTypeGFPS,
    tmFrontEndDVBT2PLPPayloadTypeGCS,
    tmFrontEndDVBT2PLPPayloadTypeGSE,
    tmFrontEndDVBT2PLPPayloadTypeTS,
    tmFrontEndDVBT2PLPPayloadTypeMax
} tmFrontEndDVBT2PLPPayloadType_t;

typedef enum _tmFrontEndRotationState_t
{
    tmFrontEndRotationStateAuto,
    tmFrontEndRotationStateOn,
    tmFrontEndRotationStateOff,
    tmFrontEndRotationStateMax
} tmFrontEndRotationState;

typedef enum _tmFrontEndDVBT2FECType_t
{
    tmFrontEndDVBT2FECTypeAuto,
    tmFrontEndDVBT2FECType16K,
    tmFrontEndDVBT2FECType64K,
    tmFrontEndDVBT2FECTypeMax
} tmFrontEndDVBT2FECType_t;

typedef enum _tmFrontEndDVBT2InputType_t
{
    tmFrontEndDVBT2InputTypeAuto,
    tmFrontEndDVBT2InputTypeSISO,
    tmFrontEndDVBT2InputTypeMISO,
    tmFrontEndDVBT2InputTypeMax
} tmFrontEndDVBT2InputType_t;

typedef enum _tmFrontEndFECMode_t
{
    tmFrontEndFECModeUnknown = 0,
    tmFrontEndFECModeAnnexA,
    tmFrontEndFECModeAnnexB,
    tmFrontEndFECModeAnnexC,
    tmFrontEndFECModeMax
} tmFrontEndFECMode_t;

typedef struct _tmFrontEndFracNb8_t
{
    Int8 lInteger;
    UInt8 uDivider;
}tmFrontEndFracNb8_t;

typedef struct _tmFrontEndFracNb16_t
{
    Int16 lInteger;
    UInt16 uDivider;
}tmFrontEndFracNb16_t;

typedef struct _tmFrontEndFracNb32_t
{
    Int32 lInteger;
    UInt32 uDivider;
}tmFrontEndFracNb32_t;

#ifdef __cplusplus
}
#endif

#endif /* TMFRONTEND_H */
/*============================================================================*/
/*                            END OF FILE                                     */
/*============================================================================*/























// NXP source code - .\inc\tmUnitParams.h


/**
  Copyright (C) 2007 NXP B.V., All Rights Reserved.
  This source code and any compilation or derivative thereof is the proprietary
  information of NXP B.V. and is confidential in nature. Under no circumstances
  is this software to be  exposed to or placed under an Open Source License of
  any type without the expressed written permission of NXP B.V.
 *
 * \file          tmUnitParams.h
 *                %version: 2 %
 *
 * \date          %date_modified%
 *
 * \brief         Describe briefly the purpose of this file.
 *
 * REFERENCE DOCUMENTS :
 *
 * Detailed description may be added here.
 *
 * \section info Change Information
 *
 * \verbatim
   Date          Modified by CRPRNr  TASKNr  Maintenance description
   -------------|-----------|-------|-------|-----------------------------------
    26-Mar-2008 | B.GUILLOT | 13122 | 23456 | Creation
   -------------|-----------|-------|-------|-----------------------------------
   \endverbatim
 *
*/


#ifndef TMUNITPARAMS_H
#define TMUNITPARAMS_H

/*============================================================================*/
/*                       INCLUDE FILES                                        */
/*============================================================================*/


#ifdef __cplusplus
extern "C" {
#endif



/*============================================================================*/
/*                       ENUM OR TYPE DEFINITION                              */
/*============================================================================*/


/******************************************************************************/
/** \brief "These macros map to tmUnitSelect_t variables parts"
*
******************************************************************************/

#define UNIT_VALID(_tUnIt)                      (((_tUnIt)&0x80000000)==0)

#define UNIT_PATH_INDEX_MASK                    (0x0000001F)
#define UNIT_PATH_INDEX_POS                     (0)

#define UNIT_PATH_TYPE_MASK                     (0x000003E0)
#define UNIT_PATH_TYPE_POS                      (5)

#define UNIT_PATH_CONFIG_MASK                   (0x0003FC00)
#define UNIT_PATH_CONFIG_POS                    (10)

#define UNIT_SYSTEM_INDEX_MASK                  (0x007C0000)
#define UNIT_SYSTEM_INDEX_POS                   (18)

#define UNIT_SYSTEM_CONFIG_MASK                 (0x7F800000)
#define UNIT_SYSTEM_CONFIG_POS                  (23)




#define UNIT_PATH_INDEX_GET(_tUnIt)             ((_tUnIt)&UNIT_PATH_INDEX_MASK)
#define UNIT_PATH_INDEX_VAL(_val)               (((_val)<<UNIT_PATH_INDEX_POS)&UNIT_PATH_INDEX_MASK)
#define UNIT_PATH_INDEX_SET(_tUnIt, _val)       ( ((_tUnIt)&~UNIT_PATH_INDEX_MASK) | UNIT_PATH_INDEX_VAL(_val) )
#define UNIT_PATH_INDEX_VAL_GET(_val)           (UNIT_PATH_INDEX_VAL(UNIT_PATH_INDEX_GET(_val)))

#define UNIT_PATH_TYPE_GET(_tUnIt)              (((_tUnIt)&UNIT_PATH_TYPE_MASK) >> UNIT_PATH_TYPE_POS)
#define UNIT_PATH_TYPE_VAL(_val)                (((_val)<<UNIT_PATH_TYPE_POS)&UNIT_PATH_TYPE_MASK)
#define UNIT_PATH_TYPE_SET(_tUnIt, _val)        ( ((_tUnIt)&~UNIT_PATH_TYPE_MASK) | UNIT_PATH_TYPE_VAL(_val) )
#define UNIT_PATH_TYPE_VAL_GET(_val)            (UNIT_PATH_TYPE_VAL(UNIT_PATH_TYPE_GET(_val)))


#define UNIT_PATH_CONFIG_GET(_tUnIt)            (((_tUnIt)&UNIT_PATH_CONFIG_MASK) >> UNIT_PATH_CONFIG_POS)
#define UNIT_PATH_CONFIG_VAL(_val)              (((_val)<<UNIT_PATH_CONFIG_POS)&UNIT_PATH_CONFIG_MASK)
#define UNIT_PATH_CONFIG_SET(_tUnIt, _val)      ( ((_tUnIt)&~UNIT_PATH_CONFIG_MASK) | UNIT_PATH_CONFIG_VAL(_val) )
#define UNIT_PATH_CONFIG_VAL_GET(_val)          (UNIT_PATH_CONFIG_VAL(UNIT_PATH_CONFIG_GET(_val)))

#define UNIT_SYSTEM_INDEX_GET(_tUnIt)           (((_tUnIt)&UNIT_SYSTEM_INDEX_MASK) >> UNIT_SYSTEM_INDEX_POS)
#define UNIT_SYSTEM_INDEX_VAL(_val)             (((_val)<<UNIT_SYSTEM_INDEX_POS)&UNIT_SYSTEM_INDEX_MASK)
#define UNIT_SYSTEM_INDEX_SET(_tUnIt, _val)     ( ((_tUnIt)&~UNIT_SYSTEM_INDEX_MASK) | UNIT_SYSTEM_INDEX_VAL(_val) )
#define UNIT_SYSTEM_INDEX_VAL_GET(_val)         (UNIT_SYSTEM_INDEX_VAL(UNIT_SYSTEM_INDEX_GET(_val)))

#define UNIT_SYSTEM_CONFIG_GET(_tUnIt)          (((_tUnIt)&UNIT_SYSTEM_CONFIG_MASK) >> UNIT_SYSTEM_CONFIG_POS)
#define UNIT_SYSTEM_CONFIG_VAL(_val)            (((_val)<<UNIT_SYSTEM_CONFIG_POS)&UNIT_SYSTEM_CONFIG_MASK)
#define UNIT_SYSTEM_CONFIG_SET(_tUnIt, _val)    ( ((_tUnIt)&~UNIT_SYSTEM_CONFIG_MASK) | UNIT_SYSTEM_CONFIG_POS(_val) )
#define UNIT_SYSTEM_CONFIG_VAL_GET(_val)        (UNIT_SYSTEM_CONFIG_VAL(UNIT_SYSTEM_CONFIG_GET(_val)))


#define GET_WHOLE_SYSTEM_TUNIT(_tUnIt)          (UNIT_SYSTEM_CONFIG_VAL_GET(_tUnIt)|UNIT_SYSTEM_INDEX_VAL_GET(_tUnIt))

#define GET_SYSTEM_TUNIT(_tUnIt)                (UNIT_SYSTEM_CONFIG_VAL_GET(_tUnIt)|UNIT_SYSTEM_INDEX_VAL_GET(_tUnIt)|UNIT_PATH_INDEX_VAL_GET(_tUnIt))

#define GET_INDEX_TUNIT(_tUnIt)                 (UNIT_SYSTEM_INDEX_VAL_GET(_tUnIt)|UNIT_PATH_INDEX_VAL_GET(_tUnIt))

#define GET_INDEX_TYPE_TUNIT(_tUnIt)            (UNIT_SYSTEM_INDEX_VAL_GET(_tUnIt)|UNIT_PATH_INDEX_VAL_GET(_tUnIt)|UNIT_PATH_TYPE_VAL_GET(_tUnIt))

#define XFER_DISABLED_FLAG                      (UNIT_PATH_CONFIG_VAL(0x80))
#define GET_XFER_DISABLED_FLAG_TUNIT(_tUnIt)    (((_tUnIt)&XFER_DISABLED_FLAG)!=0)


/*============================================================================*/


#ifdef __cplusplus
}
#endif

#endif /* TMUNITPARAMS_H */
/*============================================================================*/
/*                            END OF FILE                                     */
/*============================================================================*/























// NXP source code - .\inc\tmbslFrontEndTypes.h


/**
  Copyright (C) 2007 NXP B.V., All Rights Reserved.
  This source code and any compilation or derivative thereof is the proprietary
  information of NXP B.V. and is confidential in nature. Under no circumstances
  is this software to be  exposed to or placed under an Open Source License of
  any type without the expressed written permission of NXP B.V.
 *
 * \file          tmbslFrontEndTypes.h
 *                %version: CFR_STB#1.10 %
 *
 * \date          %date_modified%
 *
 * \brief         Describe briefly the purpose of this file.
 *
 * REFERENCE DOCUMENTS :
 *
 * Detailed description may be added here.
 *
 * \section info Change Information
 *
 * \verbatim
   Date          Modified by CRPRNr  TASKNr  Maintenance description
   -------------|-----------|-------|-------|-----------------------------------
    27-Mar-2008 | B.GUILLOT | 13122 | 23472 | Integrate with tmbslDvbtIp.
   -------------|-----------|-------|-------|-----------------------------------
   \endverbatim
 *
*/


#ifndef TMBSLFRONTENDTYPES_H
#define TMBSLFRONTENDTYPES_H

/*============================================================================*/
/*                       INCLUDE FILES                                        */
/*============================================================================*/

#ifdef __cplusplus
extern "C" {
#endif



/*============================================================================*/
/*                       MACRO DEFINITION                                     */
/*============================================================================*/



/*============================================================================*/
/*                       ENUM OR TYPE DEFINITION                              */
/*============================================================================*/

    
/* Status of the carrier phase lock loop */
#ifndef _tmbslFrontEndState_t_Struct_
#define _tmbslFrontEndState_t_Struct_
typedef enum _tmbslFrontEndState_t
{
    /** status Unknown */
    tmbslFrontEndStateUnknown = 0,
    /** Channel locked*/
    tmbslFrontEndStateLocked,
    /** Channel not locked */
    tmbslFrontEndStateNotLocked,
    /** Channel lock in process */
    tmbslFrontEndStateSearching,
    tmbslFrontEndStateMax
} tmbslFrontEndState_t, *ptmbslFrontEndState_t;
#endif


/* Gpio config */
#ifndef _tmbslFrontEndGpioConfig_t_Struct_
#define _tmbslFrontEndGpioConfig_t_Struct_
typedef enum _tmbslFrontEndGpioConfig_t
{
    tmbslFrontEndGpioConfUnknown = 0,
    tmbslFrontEndGpioConfInput,
    tmbslFrontEndGpioConfOutputPushPull,
    tmbslFrontEndGpioConfOutputOpenDrain,
    tmbslFrontEndGpioConfTriState,
    tmbslFrontEndGpioConfMax
} tmbslFrontEndGpioConfig_t, *ptmbslFrontEndGpioConfig_t;
#endif


/* Gpio polarity */
#ifndef _tmbslFrontEndGpioPolarity_t_Struct_
#define _tmbslFrontEndGpioPolarity_t_Struct_
typedef enum _tmbslFrontEndGpioPolarity_t
{
    tmbslFrontEndGpioPolUnknown = 0,
    tmbslFrontEndGpioPolNotInverted,
    tmbslFrontEndGpioPolInverted,
    tmbslFrontEndGpioPolMax
} tmbslFrontEndGpioPolarity_t, *ptmbslFrontEndGpioPolarity_t;
#endif


/* IT Selection */
#ifndef _tmbslFrontEndITSel_t_Struct_
#define _tmbslFrontEndITSel_t_Struct_
typedef enum _tmbslFrontEndITSel_t
{
    tmbslFrontEndITSelUnknown = 0,
    tmbslFrontEndITSelFELGoesUp,
    tmbslFrontEndITSelFELGoesDown,
    tmbslFrontEndITSelDVBSynchroFlag,
    tmbslFrontEndITSelVBERRefreshed,
    tmbslFrontEndITSelBERRefreshed,
    tmbslFrontEndITSelUncor,
    tmbslFrontEndGpioITSelMax
} tmbslFrontEndITSel_t, *ptmbslFrontEndITSel_t;
#endif


/* I2C switch */
#ifndef _tmbslFrontEndI2CSwitchState_t_Struct_
#define _tmbslFrontEndI2CSwitchState_t_Struct_
typedef enum _tmbslFrontEndI2CSwitchState_t
{
    tmbslFrontEndI2CSwitchStateUnknown = 0,
    tmbslFrontEndI2CSwitchStateOpen,
    tmbslFrontEndI2CSwitchStateClosed,
    tmbslFrontEndI2CSwitchStateReset,
    tmbslFrontEndI2CSwitchStateMax
} tmbslFrontEndI2CSwitchState_t, *ptmbslFrontEndI2CSwitchState_t;
#endif


/* DVBT2 PLP */
#ifndef _tmbslFrontEndDVBT2PLP_t_Struct_
#define _tmbslFrontEndDVBT2PLP_t_Struct_
typedef struct _tmbslFrontEndDVBT2PLP_t
{
    UInt32 uId;
    UInt32 uGroupId;
    tmFrontEndDVBT2PLPType_t eType;
    tmFrontEndDVBT2PLPPayloadType_t ePayloadType;
    tmFrontEndCodeRate_t eCodeRate;
    tmFrontEndModulation_t eModulation;
    tmFrontEndRotationState eRotation;
    tmFrontEndDVBT2FECType_t eFECType;
} tmbslFrontEndDVBT2PLP_t;
#endif


#ifndef _tmbslFrontEndTVStandard_t_Struct_
#define _tmbslFrontEndTVStandard_t_Struct_
typedef enum _tmbslFrontEndTVStandard_t
{
    tmbslFrontEndTVStandardNone,
    tmbslFrontEndTVStandardMN,
    tmbslFrontEndTVStandardBG,
    tmbslFrontEndTVStandardI,
    tmbslFrontEndTVStandardDKL,
    tmbslFrontEndTVStandardLp,
    tmbslFrontEndTVStandardMax
} tmbslFrontEndTVStandard_t;
#endif
 

/******************************************************************************/
/** \brief "Function pointers to hardware access services"
 *
 ******************************************************************************/
#ifndef _tmbslFrontEndIoFunc_t_Struct_
#define _tmbslFrontEndIoFunc_t_Struct_
typedef struct _tmbslFrontEndIoFunc_t
{
    /** Read hardware function */
    tmErrorCode_t   (*Read)(tmUnitSelect_t tUnit, UInt32 AddrSize, UInt8* pAddr, UInt32 ReadLen, UInt8* pData);
    /** Write hardware register, 8bit aligned function */
    tmErrorCode_t   (*Write)(tmUnitSelect_t tUnit, UInt32 AddrSize, UInt8* pAddr, UInt32 WriteLen, UInt8* pData);
} tmbslFrontEndIoFunc_t, *ptmbslFrontEndIoFunc_t;
#endif


/******************************************************************************/
/** \brief "Function pointers to Time services"
 *
 ******************************************************************************/
#ifndef _tmbslFrontEndTimeFunc_t_Struct_
#define _tmbslFrontEndTimeFunc_t_Struct_
typedef struct _tmbslFrontEndTimeFunc_t
{
    /** Return current time value in ms */
    tmErrorCode_t   (*Get)(UInt32 *ptms); 
    /**  Wait t ms without blocking scheduler; warning this function 
     don't schedule others frontend instance */
    tmErrorCode_t   (*Wait)(tmUnitSelect_t tUnit, UInt32 tms);
} tmbslFrontEndTimeFunc_t, *ptmbslFrontEndTimeFunc_t;
#endif   
   

/******************************************************************************/
/** \brief "Function pointers to Debug services "
 *
 ******************************************************************************/
#ifndef _tmbslFrontEndDebugFunc_t_Struct_
#define _tmbslFrontEndDebugFunc_t_Struct_
typedef struct _tmbslFrontEndDebugFunc_t
{
    /** Print a debug message */
    tmErrorCode_t   (*Print)(UInt32 level, const char* format, ...);
} tmbslFrontEndDebugFunc_t, *ptmbslFrontEndDebugFunc_t;
#endif


/* Mutex types */
typedef void *ptmbslFrontEndMutexHandle;
#define TMBSL_FRONTEND_MUTEX_TIMEOUT_INFINITE		(0xFFFFFFFF)

/******************************************************************************/
/** \brief "Function pointers to Mutex services "
 *
 ******************************************************************************/
#ifndef _tmbslFrontEndMutexFunc_t_Struct_
#define _tmbslFrontEndMutexFunc_t_Struct_
typedef struct _tmbslFrontEndMutexFunc_t
{
    /* Initialize a mutex object */
    tmErrorCode_t   (*Init)(ptmbslFrontEndMutexHandle *ppMutexHandle);
    /* Deinitialize a mutex object */
    tmErrorCode_t   (*DeInit)(ptmbslFrontEndMutexHandle pMutexHandle);
    /* Acquire a mutex object */
    tmErrorCode_t   (*Acquire)(ptmbslFrontEndMutexHandle pMutexHandle, UInt32 timeOut);
    /* Release a mutex object */
    tmErrorCode_t   (*Release)(ptmbslFrontEndMutexHandle pMutexHandle);
} tmbslFrontEndMutexFunc_t, *ptmbslFrontEndMutexFunc_t;
#endif


/******************************************************************************/
/** \brief "This structure contain all the bsl driver external dependencies"
 *
 *  \sa    "all bsl 'init' functions"
 *
 ******************************************************************************/
#ifndef _tmbslFrontEndDependency_t_Struct_
#define _tmbslFrontEndDependency_t_Struct_
typedef struct _tmbslFrontEndDependency_t
{
    /** Hardware access to FrontEnd */
    tmbslFrontEndIoFunc_t       sIo;
    /** Time services (wait, getTime, ...) */
    tmbslFrontEndTimeFunc_t     sTime;
    /** Debug services (Print, Store, ...) */
    tmbslFrontEndDebugFunc_t    sDebug;
    /** Mutex services */
    tmbslFrontEndMutexFunc_t		sMutex;
    /** Device Parameters data size */
    UInt32                      dwAdditionalDataSize;
    /** Device Parameters data pointer */
    void*                       pAdditionalData;
} tmbslFrontEndDependency_t, *ptmbslFrontEndDependency_t;
#endif


/*============================================================================*/
#ifndef tmSWSettingsVersion_Struct_
#define tmSWSettingsVersion_Struct_
typedef struct tmSWSettingsVersion
{
    UInt32      customerNr;         /* SW Settings customer number */
    UInt32      projectNr;          /* SW Settings project number */
    UInt32      majorVersionNr;     /* SW Settings major version number */
    UInt32      minorVersionNr;     /* SW Settings minor version number */

}   tmSWSettingsVersion_t, *ptmSWSettingsVersion_t;
#endif


/******************************************************************************/
/** \brief "These macros map to trace functions "
* P_DBGPRINTEx macro should be defined in each component
******************************************************************************/

#define DEBUGLVL_BLAB    3
#define DEBUGLVL_VERBOSE 2
#define DEBUGLVL_TERSE   1
#define DEBUGLVL_ERROR   0

#ifdef TMBSLFRONTEND_DEBUG_TRACE
/*
#define tmDBGPRINTEx(_level, _format, ...)                                                              \
{                                                                                                       \
    if(P_DBGPRINTVALID)                                                                                 \
    {                                                                                                   \
        if(_level == DEBUGLVL_ERROR)                                                                    \
        {                                                                                               \
            P_DBGPRINTEx(_level, "Error: In Function %s (Line %d):\n==> ", __FUNCTION__, __LINE__ );    \
        }                                                                                               \
        P_DBGPRINTEx(_level, _format, __VA_ARGS__);                                                     \
    }                                                                                                   \
}
*/
#else
//#define tmDBGPRINTEx(_level, _format, ...)
#endif

#define tmASSERTExTR(_retVar, _expr, _strings)                                                      
/*
{                                                                                                   
    if((_retVar) != (_expr))                                                                        
    {                                                                                               
        tmDBGPRINTEx _strings ;                                                                     
        return _retVar;                                                                             
    }                                                                                               
}
*/

#define tmASSERTExT(_retVar, _expr, _strings)                                                       
/*{                                                                                                   \
    if((_retVar) != (_expr))                                                                        \
    {                                                                                               \
        tmDBGPRINTEx _strings ;                                                                     \
    }                                                                                               \
}
*/
/*============================================================================*/

#ifdef __cplusplus
}
#endif

#endif /* TMBSLFRONTENDTYPES_H */
/*============================================================================*/
/*                            END OF FILE                                     */
/*============================================================================*/























// NXP source code - .\tmbslTDA182I2\inc\tmbslTDA18272.h


/**
Copyright (C) 2008 NXP B.V., All Rights Reserved.
This source code and any compilation or derivative thereof is the proprietary
information of NXP B.V. and is confidential in nature. Under no circumstances
is this software to be  exposed to or placed under an Open Source License of
any type without the expressed written permission of NXP B.V.
*
* \file          tmbslTDA18272.h
*                %version: 21 %
*
* \date          %date_modified%
*
* \brief         Describe briefly the purpose of this file.
*
* REFERENCE DOCUMENTS :
*
* Detailed description may be added here.
*
* \section info Change Information
*
* \verbatim
Date          Modified by CRPRNr  TASKNr  Maintenance description
-------------|-----------|-------|-------|-----------------------------------
|            |           |       |
-------------|-----------|-------|-------|-----------------------------------
|            |           |       |
-------------|-----------|-------|-------|-----------------------------------
\endverbatim
*
*/

#ifndef _TMBSL_TDA18272_H
#define _TMBSL_TDA18272_H

/*------------------------------------------------------------------------------*/
/* Standard include files:                                                      */
/*------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------*/
/* Project include files:                                                       */
/*------------------------------------------------------------------------------*/
#ifdef __cplusplus
extern "C"
{
#endif

    /*------------------------------------------------------------------------------*/
    /* Types and defines:                                                           */
    /*------------------------------------------------------------------------------*/

    /* SW Error codes */
#define TDA182I2_ERR_BASE               (CID_COMP_TUNER | CID_LAYER_BSL)
#define TDA182I2_ERR_COMP               (CID_COMP_TUNER | CID_LAYER_BSL | TM_ERR_COMP_UNIQUE_START)

#define TDA182I2_ERR_BAD_UNIT_NUMBER    (TDA182I2_ERR_BASE + TM_ERR_BAD_UNIT_NUMBER)
#define TDA182I2_ERR_NOT_INITIALIZED    (TDA182I2_ERR_BASE + TM_ERR_NOT_INITIALIZED)
#define TDA182I2_ERR_INIT_FAILED        (TDA182I2_ERR_BASE + TM_ERR_INIT_FAILED)
#define TDA182I2_ERR_BAD_PARAMETER      (TDA182I2_ERR_BASE + TM_ERR_BAD_PARAMETER)
#define TDA182I2_ERR_NOT_SUPPORTED      (TDA182I2_ERR_BASE + TM_ERR_NOT_SUPPORTED)
#define TDA182I2_ERR_HW_FAILED          (TDA182I2_ERR_COMP + 0x0001)
#define TDA182I2_ERR_NOT_READY          (TDA182I2_ERR_COMP + 0x0002)
#define TDA182I2_ERR_BAD_VERSION        (TDA182I2_ERR_COMP + 0x0003)


    typedef enum _tmTDA182I2PowerState_t {
        tmTDA182I2_PowerNormalMode = 0,                                 /* Device normal mode */
        tmTDA182I2_PowerStandbyWithLNAOnAndWithXtalOnAndSynthe,         /* Device standby mode with LNA and Xtal Output and Synthe */
        tmTDA182I2_PowerStandbyWithLNAOnAndWithXtalOn,                  /* Device standby mode with LNA and Xtal Output */
        tmTDA182I2_PowerStandbyWithXtalOn,                              /* Device standby mode with Xtal Output */
        tmTDA182I2_PowerStandby,                                        /* Device standby mode */
        tmTDA182I2_PowerMax
    } tmTDA182I2PowerState_t, *ptmTDA182I2PowerState_t;

    typedef enum _tmTDA182I2StandardMode_t {
        tmTDA182I2_DVBT_6MHz = 0,                       /* Digital TV DVB-T 6MHz */
        tmTDA182I2_DVBT_7MHz,                           /* Digital TV DVB-T 7MHz */
        tmTDA182I2_DVBT_8MHz,                           /* Digital TV DVB-T 8MHz */
        tmTDA182I2_QAM_6MHz,                            /* Digital TV QAM 6MHz */
        tmTDA182I2_QAM_8MHz,                            /* Digital TV QAM 8MHz */
        tmTDA182I2_ISDBT_6MHz,                          /* Digital TV ISDBT 6MHz */
        tmTDA182I2_ATSC_6MHz,                           /* Digital TV ATSC 6MHz */
        tmTDA182I2_DMBT_8MHz,                           /* Digital TV DMB-T 8MHz */
        tmTDA182I2_ANLG_MN,                             /* Analog TV M/N */
        tmTDA182I2_ANLG_B,                              /* Analog TV B */
        tmTDA182I2_ANLG_GH,                             /* Analog TV G/H */
        tmTDA182I2_ANLG_I,                              /* Analog TV I */
        tmTDA182I2_ANLG_DK,                             /* Analog TV D/K */
        tmTDA182I2_ANLG_L,                              /* Analog TV L */
        tmTDA182I2_ANLG_LL,                             /* Analog TV L' */
        tmTDA182I2_FM_Radio,                            /* Analog FM Radio */
        tmTDA182I2_Scanning,                            /* analog  preset blind Scanning */
        tmTDA182I2_ScanXpress,                          /* ScanXpress */
        tmTDA182I2_StandardMode_Max
    } tmTDA182I2StandardMode_t, *ptmTDA182I2StandardMode_t;

    typedef enum _tmTDA182I2LPF_t {
        tmTDA182I2_LPF_6MHz = 0,                        /* 6MHz LPFc */
        tmTDA182I2_LPF_7MHz,                            /* 7MHz LPFc */
        tmTDA182I2_LPF_8MHz,                            /* 8MHz LPFc */
        tmTDA182I2_LPF_9MHz,                            /* 9MHz LPFc */
        tmTDA182I2_LPF_1_5MHz,                          /* 1.5MHz LPFc */
        tmTDA182I2_LPF_Max
    } tmTDA182I2LPF_t, *ptmTDA182I2LPF_t;

    typedef enum _tmTDA182I2LPFOffset_t {
        tmTDA182I2_LPFOffset_0pc = 0,                   /* LPFc 0% */
        tmTDA182I2_LPFOffset_min_4pc,                   /* LPFc -4% */
        tmTDA182I2_LPFOffset_min_8pc,                   /* LPFc -8% */
        tmTDA182I2_LPFOffset_min_12pc,                  /* LPFc -12% */
        tmTDA182I2_LPFOffset_Max
    } tmTDA182I2LPFOffset_t, *ptmTDA182I2LPFOffset_t;

    typedef enum _tmTDA182I2IF_AGC_Gain_t {
        tmTDA182I2_IF_AGC_Gain_2Vpp_0_30dB = 0,         /* 2Vpp       0 - 30dB IF AGC Gain */
        tmTDA182I2_IF_AGC_Gain_1_25Vpp_min_4_26dB,      /* 1.25Vpp   -4 - 26dB IF AGC Gain */
        tmTDA182I2_IF_AGC_Gain_1Vpp_min_6_24dB,         /* 1Vpp      -6 - 24dB IF AGC Gain */
        tmTDA182I2_IF_AGC_Gain_0_8Vpp_min_8_22dB,       /* 0.8Vpp    -8 - 22dB IF AGC Gain */
        tmTDA182I2_IF_AGC_Gain_0_85Vpp_min_7_5_22_5dB,  /* 0.85Vpp   -7.5 - 22.5dB IF AGC Gain */
        tmTDA182I2_IF_AGC_Gain_0_7Vpp_min_9_21dB,       /* 0.7Vpp    -9 - 21dB IF AGC Gain */
        tmTDA182I2_IF_AGC_Gain_0_6Vpp_min_10_3_19_7dB,  /* 0.6Vpp    -10.3 - 19.7dB IF AGC Gain */
        tmTDA182I2_IF_AGC_Gain_0_5Vpp_min_12_18dB,      /* 0.5Vpp    -12 - 18dB IF AGC Gain */
        tmTDA182I2_IF_AGC_Gain_Max
    } tmTDA182I2IF_AGC_Gain_t, *ptmTDA182I2IF_AGC_Gain_t;

    typedef enum _tmTDA182I2IF_Notch_t {
        tmTDA182I2_IF_Notch_Disabled = 0,               /* IF Notch Enabled */
        tmTDA182I2_IF_Notch_Enabled,                    /* IF Notch Disabled */
        tmTDA182I2_IF_Notch_Max
    } tmTDA182I2IF_Notch_t, *ptmTDA182I2IF_Notch_t;

    typedef enum _tmTDA182I2IF_HPF_t {
        tmTDA182I2_IF_HPF_Disabled = 0,                 /* IF HPF 0.4MHz */
        tmTDA182I2_IF_HPF_0_4MHz,                       /* IF HPF 0.4MHz */
        tmTDA182I2_IF_HPF_0_85MHz,                      /* IF HPF 0.85MHz */
        tmTDA182I2_IF_HPF_1MHz,                         /* IF HPF 1MHz */
        tmTDA182I2_IF_HPF_1_5MHz,                       /* IF HPF 1.5MHz */
        tmTDA182I2_IF_HPF_Max
    } tmTDA182I2IF_HPF_t, *ptmTDA182I2IF_HPF_t;

    typedef enum _tmTDA182I2DC_Notch_t {
        tmTDA182I2_DC_Notch_Disabled = 0,               /* IF Notch Enabled */
        tmTDA182I2_DC_Notch_Enabled,                    /* IF Notch Disabled */
        tmTDA182I2_DC_Notch_Max
    } tmTDA182I2DC_Notch_t, *ptmTDA182I2DC_Notch_t;

    typedef enum _tmTDA182I2AGC1_LNA_TOP_t {
        tmTDA182I2_AGC1_LNA_TOP_d95_u89dBuV = 0,            /* AGC1 LNA TOP down 95 up 89 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d95_u93dBuV_do_not_use,     /* AGC1 LNA TOP down 95 up 93 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d95_u94dBuV_do_not_use,     /* AGC1 LNA TOP down 95 up 94 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d95_u95dBuV_do_not_use,     /* AGC1 LNA TOP down 95 up 95 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d99_u89dBuV,                /* AGC1 LNA TOP down 99 up 89 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d99_u93dBuV,                /* AGC1 LNA TOP down 95 up 93 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d99_u94dBuV,                /* AGC1 LNA TOP down 99 up 94 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d99_u95dBuV,                /* AGC1 LNA TOP down 99 up 95 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d99_u9SdBuV,                /* AGC1 LNA TOP down 99 up 95 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d100_u93dBuV,               /* AGC1 LNA TOP down 100 up 93 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d100_u94dBuV,               /* AGC1 LNA TOP down 100 up 94 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d100_u95dBuV,               /* AGC1 LNA TOP down 100 up 95 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d100_u9SdBuV,               /* AGC1 LNA TOP down 100 up 95 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d101_u93dBuV,               /* AGC1 LNA TOP down 101 up 93 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d101_u94dBuV,               /* AGC1 LNA TOP down 101 up 94 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d101_u95dBuV,               /* AGC1 LNA TOP down 101 up 95 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_d101_u9SdBuV,               /* AGC1 LNA TOP down 101 up 95 dBuV */
        tmTDA182I2_AGC1_LNA_TOP_Max
    } tmTDA182I2AGC1_LNA_TOP_t, *ptmTDA182I2AGC1_LNA_TOP_t;

    typedef enum _tmTDA182I2AGC2_RF_Attenuator_TOP_t {
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d89_u81dBuV = 0, /* AGC2 RF Attenuator TOP down 89 up 81 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d91_u83dBuV,     /* AGC2 RF Attenuator TOP down 91 up 83 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d93_u85dBuV,     /* AGC2 RF Attenuator TOP down 93 up 85 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d95_u87dBuV,     /* AGC2 RF Attenuator TOP down 95 up 87 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d88_u88dBuV,     /* AGC2 RF Attenuator TOP down 88 up 81 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d89_u82dBuV,     /* AGC2 RF Attenuator TOP down 89 up 82 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u83dBuV,     /* AGC2 RF Attenuator TOP down 90 up 83 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d91_u84dBuV,     /* AGC2 RF Attenuator TOP down 91 up 84 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d92_u85dBuV,     /* AGC2 RF Attenuator TOP down 92 up 85 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d93_u86dBuV,     /* AGC2 RF Attenuator TOP down 93 up 86 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d94_u87dBuV,     /* AGC2 RF Attenuator TOP down 94 up 87 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d95_u88dBuV,     /* AGC2 RF Attenuator TOP down 95 up 88 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d87_u81dBuV,     /* AGC2 RF Attenuator TOP down 87 up 81 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d88_u82dBuV,     /* AGC2 RF Attenuator TOP down 88 up 82 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d89_u83dBuV,     /* AGC2 RF Attenuator TOP down 89 up 83 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d90_u84dBuV,     /* AGC2 RF Attenuator TOP down 90 up 84 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d91_u85dBuV,     /* AGC2 RF Attenuator TOP down 91 up 85 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d92_u86dBuV,     /* AGC2 RF Attenuator TOP down 92 up 86 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d93_u87dBuV,     /* AGC2 RF Attenuator TOP down 93 up 87 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d94_u88dBuV,     /* AGC2 RF Attenuator TOP down 94 up 88 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_d95_u89dBuV,     /* AGC2 RF Attenuator TOP down 95 up 89 dBuV */
        tmTDA182I2_AGC2_RF_Attenuator_TOP_Max
    } tmTDA182I2AGC2_RF_Attenuator_TOP_t, *ptmTDA182I2AGC2_RF_Attenuator_TOP_t;
    
    typedef enum _tmTDA182I2AGC3_RF_AGC_TOP_t {
        tmTDA182I2_AGC3_RF_AGC_TOP_94dBuV = 0, /* AGC3 RF AGC TOP 94 dBuV */
        tmTDA182I2_AGC3_RF_AGC_TOP_96dBuV,     /* AGC3 RF AGC TOP 96 dBuV */
        tmTDA182I2_AGC3_RF_AGC_TOP_98dBuV,     /* AGC3 RF AGC TOP 98 dBuV */
        tmTDA182I2_AGC3_RF_AGC_TOP_100dBuV,    /* AGC3 RF AGC TOP 100 dBuV */
        tmTDA182I2_AGC3_RF_AGC_TOP_102dBuV,    /* AGC3 RF AGC TOP 102 dBuV */
        tmTDA182I2_AGC3_RF_AGC_TOP_104dBuV,    /* AGC3 RF AGC TOP 104 dBuV */
        tmTDA182I2_AGC3_RF_AGC_TOP_106dBuV,    /* AGC3 RF AGC TOP 106 dBuV */
        tmTDA182I2_AGC3_RF_AGC_TOP_107dBuV,    /* AGC3 RF AGC TOP 107 dBuV */
        tmTDA182I2_AGC3_RF_AGC_TOP_Max
    } tmTDA182I2AGC3_RF_AGC_TOP_t, *ptmTDA182I2AGC3_RF_AGC_TOP_t;

#define tmTDA182I2_AGC3_RF_AGC_TOP_FREQ_LIM 291000000

    typedef enum _tmTDA182I2AGC4_IR_Mixer_TOP_t {
        tmTDA182I2_AGC4_IR_Mixer_TOP_d105_u99dBuV = 0,     /* AGC4 IR_Mixer TOP down 105 up 99 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d105_u100dBuV,        /* AGC4 IR_Mixer TOP down 105 up 100 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d105_u101dBuV,        /* AGC4 IR_Mixer TOP down 105 up 101 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d107_u101dBuV,        /* AGC4 IR_Mixer TOP down 107 up 101 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d107_u102dBuV,        /* AGC4 IR_Mixer TOP down 107 up 102 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d107_u103dBuV,        /* AGC4 IR_Mixer TOP down 107 up 103 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d108_u103dBuV,        /* AGC4 IR_Mixer TOP down 108 up 103 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d109_u103dBuV,        /* AGC4 IR_Mixer TOP down 109 up 103 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d109_u104dBuV,        /* AGC4 IR_Mixer TOP down 109 up 104 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d109_u105dBuV,        /* AGC4 IR_Mixer TOP down 109 up 105 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d110_u104dBuV,        /* AGC4 IR_Mixer TOP down 110 up 104 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d110_u105dBuV,        /* AGC4 IR_Mixer TOP down 110 up 105 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d110_u106dBuV,        /* AGC4 IR_Mixer TOP down 110 up 106 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d112_u106dBuV,        /* AGC4 IR_Mixer TOP down 112 up 106 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d112_u107dBuV,        /* AGC4 IR_Mixer TOP down 112 up 107 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_d112_u108dBuV,        /* AGC4 IR_Mixer TOP down 112 up 108 dBuV */
        tmTDA182I2_AGC4_IR_Mixer_TOP_Max
    } tmTDA182I2AGC4_IR_Mixer_TOP_t, *ptmTDA182I2AGC4_IR_Mixer_TOP_t;

    typedef enum _tmTDA182I2AGC5_IF_AGC_TOP_t {
        tmTDA182I2_AGC5_IF_AGC_TOP_d105_u99dBuV = 0,        /* AGC5 IF AGC TOP down 105 up 99 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d105_u100dBuV,           /* AGC5 IF AGC TOP down 105 up 100 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d105_u101dBuV,           /* AGC5 IF AGC TOP down 105 up 101 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d107_u101dBuV,           /* AGC5 IF AGC TOP down 107 up 101 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d107_u102dBuV,           /* AGC5 IF AGC TOP down 107 up 102 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d107_u103dBuV,           /* AGC5 IF AGC TOP down 107 up 103 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d108_u103dBuV,           /* AGC5 IF AGC TOP down 108 up 103 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d109_u103dBuV,           /* AGC5 IF AGC TOP down 109 up 103 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d109_u104dBuV,           /* AGC5 IF AGC TOP down 109 up 104 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d109_u105dBuV,           /* AGC5 IF AGC TOP down 109 up 105 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d110_u104dBuV,           /* AGC5 IF AGC TOP down 108 up 104 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d110_u105dBuV,           /* AGC5 IF AGC TOP down 108 up 105 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d110_u106dBuV,           /* AGC5 IF AGC TOP down 108 up 106 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d112_u106dBuV,           /* AGC5 IF AGC TOP down 108 up 106 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d112_u107dBuV,           /* AGC5 IF AGC TOP down 108 up 107 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_d112_u108dBuV,           /* AGC5 IF AGC TOP down 108 up 108 dBuV */
        tmTDA182I2_AGC5_IF_AGC_TOP_Max
    } tmTDA182I2AGC5_IF_AGC_TOP_t, *ptmTDA182I2AGC5_IF_AGC_TOP_t;

    typedef enum _tmTDA182I2AGC5_Detector_HPF_t {
        tmTDA182I2_AGC5_Detector_HPF_Disabled = 0,          /* AGC5_Detector_HPF Enabled */
        tmTDA182I2_AGC5_Detector_HPF_Enabled,               /* IF Notch Disabled */
        tmTDA182I2_AGC5_Detector_HPF_Max
    } tmTDA182I2AGC5_Detector_HPF_t, *ptmTDA182I2AGC5_Detector_HPFh_t;

    typedef enum _tmTDA182I2AGC3_Adapt_t {
        tmTDA182I2_AGC3_Adapt_Enabled = 0,                  /* AGC3_Adapt Enabled */
        tmTDA182I2_AGC3_Adapt_Disabled,                     /* AGC3_Adapt Disabled */
        tmTDA182I2_AGC3_Adapt_Max
    } tmTDA182I2AGC3_Adapt_t, *ptmTDA182I2AGC3_Adapt_t;

    typedef enum _tmTDA182I2AGC3_Adapt_TOP_t {
        tmTDA182I2_AGC3_Adapt_TOP_0_Step = 0,              /* same level as AGC3 TOP  */
        tmTDA182I2_AGC3_Adapt_TOP_1_Step,                  /* 1 level below AGC3 TOP  */
        tmTDA182I2_AGC3_Adapt_TOP_2_Step,                  /* 2 level below AGC3 TOP  */
        tmTDA182I2_AGC3_Adapt_TOP_3_Step,                  /* 3 level below AGC3 TOP  */
    } tmTDA182I2AGC3_Adapt_TOP_t, *ptmTDA182I2AGC3_Adapt_TOP_t;

    typedef enum _tmTDA182I2AGC5_Atten_3dB_t {
        tmTDA182I2_AGC5_Atten_3dB_Disabled = 0,             /* AGC5_Atten_3dB Disabled */
        tmTDA182I2_AGC5_Atten_3dB_Enabled,                  /* AGC5_Atten_3dB Enabled */
        tmTDA182I2_AGC5_Atten_3dB_Max
    } tmTDA182I2AGC5_Atten_3dB_t, *ptmTDA182I2AGC5_Atten_3dB_t;

    typedef enum _tmTDA182I2H3H5_VHF_Filter6_t {
        tmTDA182I2_H3H5_VHF_Filter6_Disabled = 0,           /* H3H5_VHF_Filter6 Disabled */
        tmTDA182I2_H3H5_VHF_Filter6_Enabled,                /* H3H5_VHF_Filter6 Enabled */
        tmTDA182I2_H3H5_VHF_Filter6_Max
    } tmTDA182I2H3H5_VHF_Filter6_t, *ptmTDA182I2H3H5_VHF_Filter6_t;

    typedef enum _tmTDA182I2_LPF_Gain_t {
        tmTDA182I2_LPF_Gain_Unknown = 0,                    /* LPF_Gain Unknown */
        tmTDA182I2_LPF_Gain_Frozen,                         /* LPF_Gain Frozen */
        tmTDA182I2_LPF_Gain_Free                            /* LPF_Gain Free */
    } tmTDA182I2_LPF_Gain_t, *ptmTDA182I2_LPF_Gain_t;

    typedef struct _tmTDA182I2StdCoefficients
    {
        UInt32                              IF;
        Int32                               CF_Offset;
        tmTDA182I2LPF_t                     LPF;
        tmTDA182I2LPFOffset_t               LPF_Offset;
        tmTDA182I2IF_AGC_Gain_t             IF_Gain;
        tmTDA182I2IF_Notch_t                IF_Notch;
        tmTDA182I2IF_HPF_t                  IF_HPF;
        tmTDA182I2DC_Notch_t                DC_Notch;
        tmTDA182I2AGC1_LNA_TOP_t            AGC1_LNA_TOP;
        tmTDA182I2AGC2_RF_Attenuator_TOP_t  AGC2_RF_Attenuator_TOP;
        tmTDA182I2AGC3_RF_AGC_TOP_t         AGC3_RF_AGC_TOP_Low_band;
        tmTDA182I2AGC3_RF_AGC_TOP_t         AGC3_RF_AGC_TOP_High_band;
        tmTDA182I2AGC4_IR_Mixer_TOP_t       AGC4_IR_Mixer_TOP;
        tmTDA182I2AGC5_IF_AGC_TOP_t         AGC5_IF_AGC_TOP;
        tmTDA182I2AGC5_Detector_HPF_t       AGC5_Detector_HPF;
        tmTDA182I2AGC3_Adapt_t              AGC3_Adapt;
        tmTDA182I2AGC3_Adapt_TOP_t          AGC3_Adapt_TOP;
        tmTDA182I2AGC5_Atten_3dB_t          AGC5_Atten_3dB;
        UInt8                               GSK;
        tmTDA182I2H3H5_VHF_Filter6_t        H3H5_VHF_Filter6;
        tmTDA182I2_LPF_Gain_t               LPF_Gain;
		Bool								AGC1_Freeze;
		Bool								LTO_STO_immune;
    } tmTDA182I2StdCoefficients, *ptmTDA182I2StdCoefficients;

    typedef enum _tmTDA182I2RFFilterRobustness_t {
        tmTDA182I2RFFilterRobustness_Low = 0,
        tmTDA182I2RFFilterRobustness_High,
        tmTDA182I2RFFilterRobustness_Error,
        tmTDA182I2RFFilterRobustness_Max
    } tmTDA182I2RFFilterRobustness_t, *ptmTDA182I2RFFilterRobustness_t;
/*
    typedef struct _tmTDA182I2RFFilterRating {
        double                               VHFLow_0_Margin;
        double                               VHFLow_1_Margin;
        double                               VHFHigh_0_Margin;
        double                               VHFHigh_1_Margin;
        double                               UHFLow_0_Margin;
        double                               UHFLow_1_Margin;
        double                               UHFHigh_0_Margin;
        double                               UHFHigh_1_Margin;    
        tmTDA182I2RFFilterRobustness_t      VHFLow_0_RFFilterRobustness;
        tmTDA182I2RFFilterRobustness_t      VHFLow_1_RFFilterRobustness;
        tmTDA182I2RFFilterRobustness_t      VHFHigh_0_RFFilterRobustness;
        tmTDA182I2RFFilterRobustness_t      VHFHigh_1_RFFilterRobustness;
        tmTDA182I2RFFilterRobustness_t      UHFLow_0_RFFilterRobustness;
        tmTDA182I2RFFilterRobustness_t      UHFLow_1_RFFilterRobustness;
        tmTDA182I2RFFilterRobustness_t      UHFHigh_0_RFFilterRobustness;
        tmTDA182I2RFFilterRobustness_t      UHFHigh_1_RFFilterRobustness;
    } tmTDA182I2RFFilterRating, *ptmTDA182I2RFFilterRating;
*/
    tmErrorCode_t
        tmbslTDA182I2Init(
        tmUnitSelect_t              tUnit,      /*  I: Unit number */
        tmbslFrontEndDependency_t*  psSrvFunc,   /*  I: setup parameters */
        TUNER_MODULE                *pTuner	    // Added by Realtek
        );
    tmErrorCode_t 
        tmbslTDA182I2DeInit (
        tmUnitSelect_t  tUnit   /*  I: Unit number */
        );
    tmErrorCode_t
        tmbslTDA182I2GetSWVersion (
        ptmSWVersion_t  pSWVersion  /*  I: Receives SW Version */
        );
    tmErrorCode_t
        tmbslTDA182I2CheckHWVersion (
        tmUnitSelect_t tUnit    /* I: Unit number */
        );
    tmErrorCode_t
        tmbslTDA182I2SetPowerState (
        tmUnitSelect_t          tUnit,      /*  I: Unit number */
        tmTDA182I2PowerState_t  powerState  /*  I: Power state of this device */
        );
    tmErrorCode_t
        tmbslTDA182I2GetPowerState (
        tmUnitSelect_t            tUnit,        /*  I: Unit number */
        tmTDA182I2PowerState_t    *pPowerState  /*  O: Power state of this device */
        );
    tmErrorCode_t
        tmbslTDA182I2SetStandardMode (
        tmUnitSelect_t              tUnit,          /*  I: Unit number */
        tmTDA182I2StandardMode_t    StandardMode    /*  I: Standard mode of this device */
        );
    tmErrorCode_t
        tmbslTDA182I2GetStandardMode (
        tmUnitSelect_t              tUnit,          /*  I: Unit number */
        tmTDA182I2StandardMode_t    *pStandardMode  /*  O: Standard mode of this device */
        );
    tmErrorCode_t
        tmbslTDA182I2SetRf(
        tmUnitSelect_t  tUnit,  /*  I: Unit number */
        UInt32          uRF,    /*  I: RF frequency in hertz */
        tmTDA182I2IF_AGC_Gain_t   IF_Gain     // Added by realtek
        );
    tmErrorCode_t
        tmbslTDA182I2GetRf(
        tmUnitSelect_t  tUnit,  /*  I: Unit number */
        UInt32*         pRF     /*  O: RF frequency in hertz */
        );
    tmErrorCode_t
        tmbslTDA182I2Reset(
        tmUnitSelect_t tUnit    /* I: Unit number */
        );
    tmErrorCode_t
        tmbslTDA182I2GetIF(
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt32*         puIF    /* O: IF Frequency in hertz */
        );
    tmErrorCode_t
        tmbslTDA182I2GetCF_Offset(
        tmUnitSelect_t  tUnit,      /* I: Unit number */
        UInt32*         puOffset    /* O: Center frequency offset in hertz */
        );
    tmErrorCode_t
        tmbslTDA182I2GetLockStatus(
        tmUnitSelect_t          tUnit,      /* I: Unit number */
        tmbslFrontEndState_t*   pLockStatus /* O: PLL Lock status */
        );
    tmErrorCode_t
        tmbslTDA182I2GetPowerLevel(
        tmUnitSelect_t  tUnit,      /* I: Unit number */
        UInt32*         pPowerLevel /* O: Power Level in dBuV */
        );
    tmErrorCode_t
        tmbslTDA182I2SetIRQWait(
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        Bool            bWait   /* I: Determine if we need to wait IRQ in driver functions */
        );
    tmErrorCode_t
        tmbslTDA182I2GetIRQWait(
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        Bool*           pbWait  /* O: Determine if we need to wait IRQ in driver functions */
        );
    tmErrorCode_t
        tmbslTDA182I2GetIRQ(
        tmUnitSelect_t  tUnit  /* I: Unit number */,
        Bool*           pbIRQ  /* O: IRQ triggered */
        );
    tmErrorCode_t
        tmbslTDA182I2WaitIRQ(
        tmUnitSelect_t  tUnit,      /* I: Unit number */
        UInt32          timeOut,    /* I: timeOut for IRQ wait */
        UInt32          waitStep,   /* I: wait step */
        UInt8           irqStatus   /* I: IRQs to wait */
        );
    tmErrorCode_t
        tmbslTDA182I2GetXtalCal_End(
        tmUnitSelect_t  tUnit           /* I: Unit number */,
        Bool*           pbXtalCal_End   /* O: XtalCal_End triggered */
        );
    tmErrorCode_t
        tmbslTDA182I2WaitXtalCal_End(
        tmUnitSelect_t  tUnit,      /* I: Unit number */
        UInt32          timeOut,    /* I: timeOut for IRQ wait */
        UInt32          waitStep    /* I: wait step */
        );
    tmErrorCode_t
        tmbslTDA182I2SoftReset(
        tmUnitSelect_t  tUnit   /* I: Unit number */
        );
/*
    tmErrorCode_t
        tmbslTDA182I2CheckRFFilterRobustness
        (
        tmUnitSelect_t                         tUnit,      // I: Unit number
        ptmTDA182I2RFFilterRating              rating      // O: RF Filter rating
        );
*/
    tmErrorCode_t
        tmbslTDA182I2Write (
        tmUnitSelect_t  tUnit,      /* I: Unit number */
        UInt32          uIndex,     /* I: Start index to write */
        UInt32          WriteLen,   /* I: Number of bytes to write */
        UInt8*          pData       /* I: Data to write */
        );
    tmErrorCode_t
        tmbslTDA182I2Read (
        tmUnitSelect_t  tUnit,      /* I: Unit number */
        UInt32          uIndex,     /* I: Start index to read */
        UInt32          ReadLen,    /* I: Number of bytes to read */
        UInt8*          pData       /* I: Data to read */
        );

#ifdef __cplusplus
}
#endif

#endif /* _TMBSL_TDA18272_H */























// NXP source code - .\tmbslTDA182I2\inc\tmbslTDA182I2local.h


/**
Copyright (C) 2008 NXP B.V., All Rights Reserved.
This source code and any compilation or derivative thereof is the proprietary
information of NXP B.V. and is confidential in nature. Under no circumstances
is this software to be  exposed to or placed under an Open Source License of
any type without the expressed written permission of NXP B.V.
*
* \file          tmbslTDA182I2local.h
*                %version: 9 %
*
* \date          %date_modified%
*
* \brief         Describe briefly the purpose of this file.
*
* REFERENCE DOCUMENTS :
*
* Detailed description may be added here.
*
* \section info Change Information
*
* \verbatim
Date          Modified by CRPRNr  TASKNr  Maintenance description
-------------|-----------|-------|-------|-----------------------------------
|            |           |       |
-------------|-----------|-------|-------|-----------------------------------
|            |           |       |
-------------|-----------|-------|-------|-----------------------------------
\endverbatim
*
*/

#ifndef _TMBSL_TDA182I2LOCAL_H 
#define _TMBSL_TDA182I2LOCAL_H

/*------------------------------------------------------------------------------*/
/* Standard include files:                                                      */
/*------------------------------------------------------------------------------*/

/*------------------------------------------------------------------------------*/
/* Project include files:                                                       */
/*------------------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C"
{
#endif

/*------------------------------------------------------------------------------*/
/* Types and defines:                                                           */
/*------------------------------------------------------------------------------*/

#define TDA182I2_MUTEX_TIMEOUT  TMBSL_FRONTEND_MUTEX_TIMEOUT_INFINITE

#ifdef TMBSL_TDA18272
 #define TMBSL_TDA182I2_COMPONENT_NAME_STR "TDA18272"
#else /* TMBSL_TDA18272 */
 #define TMBSL_TDA182I2_COMPONENT_NAME_STR "TDA18212"
#endif /* TMBSL_TDA18272 */

#define _SYSTEMFUNC (pObj->SystemFunc)
#define POBJ_SRVFUNC_SIO pObj->sRWFunc
#define POBJ_SRVFUNC_STIME pObj->sTime
#define P_DBGPRINTEx pObj->sDebug.Print
#define P_DBGPRINTVALID ((pObj != Null) && (pObj->sDebug.Print != Null))


/*-------------*/
/* ERROR CODES */
/*-------------*/

#define TDA182I2_MAX_UNITS                          2

    typedef struct _tmTDA182I2Object_t
    {
        tmUnitSelect_t              tUnit;
        tmUnitSelect_t              tUnitW;
        ptmbslFrontEndMutexHandle   pMutex;
        Bool                        init;
        tmbslFrontEndIoFunc_t       sRWFunc;
        tmbslFrontEndTimeFunc_t     sTime;
        tmbslFrontEndDebugFunc_t    sDebug;
        tmbslFrontEndMutexFunc_t    sMutex;
        tmTDA182I2PowerState_t      curPowerState;
        tmTDA182I2PowerState_t      minPowerState;
        UInt32                      uRF;
        tmTDA182I2StandardMode_t    StandardMode;
        Bool                        Master;
        UInt8                       LT_Enable;
        UInt8                       PSM_AGC1;
        UInt8                       AGC1_6_15dB;
        tmTDA182I2StdCoefficients   Std_Array[tmTDA182I2_StandardMode_Max];

        // Added by Realtek.
		TUNER_MODULE                *pTuner;

    } tmTDA182I2Object_t, *ptmTDA182I2Object_t, **pptmTDA182I2Object_t;

/* suppress warning about static */
#pragma GCC diagnostic ignored "-Wunused-function"
static tmErrorCode_t TDA182I2Init(tmUnitSelect_t tUnit);
static tmErrorCode_t TDA182I2Wait(ptmTDA182I2Object_t pObj, UInt32 Time);
static tmErrorCode_t TDA182I2WaitXtalCal_End( ptmTDA182I2Object_t pObj, UInt32 timeOut, UInt32 waitStep);

extern tmErrorCode_t TDA182I2MutexAcquire(ptmTDA182I2Object_t   pObj, UInt32 timeOut);
extern tmErrorCode_t TDA182I2MutexRelease(ptmTDA182I2Object_t   pObj);

#ifdef __cplusplus
}
#endif

#endif /* _TMBSL_TDA182I2LOCAL_H */























// NXP source code - .\tmbslTDA182I2\inc\tmbslTDA182I2Instance.h


//-----------------------------------------------------------------------------
// $Header: 
// (C) Copyright 2001 NXP Semiconductors, All rights reserved
//
// This source code and any compilation or derivative thereof is the sole
// property of NXP Corporation and is provided pursuant to a Software
// License Agreement.  This code is the proprietary information of NXP
// Corporation and is confidential in nature.  Its use and dissemination by
// any party other than NXP Corporation is strictly limited by the
// confidential information provisions of the Agreement referenced above.
//-----------------------------------------------------------------------------
// FILE NAME:    tmbslTDA182I2Instance.h
//
// DESCRIPTION:  define the static Objects
//
// DOCUMENT REF: DVP Software Coding Guidelines v1.14
//               DVP Board Support Library Architecture Specification v0.5
//
// NOTES:        
//-----------------------------------------------------------------------------
//
#ifndef _TMBSLTDA182I2_INSTANCE_H //-----------------
#define _TMBSLTDA182I2_INSTANCE_H

tmErrorCode_t TDA182I2AllocInstance (tmUnitSelect_t tUnit, pptmTDA182I2Object_t ppDrvObject);
tmErrorCode_t TDA182I2DeAllocInstance (tmUnitSelect_t tUnit);
tmErrorCode_t TDA182I2GetInstance (tmUnitSelect_t tUnit, pptmTDA182I2Object_t ppDrvObject);


#endif // _TMBSLTDA182I2_INSTANCE_H //---------------























// NXP source code - .\tmddTDA182I2\inc\tmddTDA182I2.h


/**
Copyright (C) 2008 NXP B.V., All Rights Reserved.
This source code and any compilation or derivative thereof is the proprietary
information of NXP B.V. and is confidential in nature. Under no circumstances
is this software to be  exposed to or placed under an Open Source License of
any type without the expressed written permission of NXP B.V.
*
* \file          tmddTDA182I2.h
*                %version: 11 %
*
* \date          %date_modified%
*
* \brief         Describe briefly the purpose of this file.
*
* REFERENCE DOCUMENTS :
*
* Detailed description may be added here.
*
* \section info Change Information
*
* \verbatim
Date          Modified by CRPRNr  TASKNr  Maintenance description
-------------|-----------|-------|-------|-----------------------------------
|            |           |       |
-------------|-----------|-------|-------|-----------------------------------
|            |           |       |
-------------|-----------|-------|-------|-----------------------------------
\endverbatim
*
*/
#ifndef _TMDD_TDA182I2_H //-----------------
#define _TMDD_TDA182I2_H

//-----------------------------------------------------------------------------
// Standard include files:
//-----------------------------------------------------------------------------
//

//-----------------------------------------------------------------------------
// Project include files:
//-----------------------------------------------------------------------------
//

#ifdef __cplusplus
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    // Types and defines:
    //-----------------------------------------------------------------------------
    //

    /* SW Error codes */
#define ddTDA182I2_ERR_BASE               (CID_COMP_TUNER | CID_LAYER_BSL)
#define ddTDA182I2_ERR_COMP               (CID_COMP_TUNER | CID_LAYER_BSL | TM_ERR_COMP_UNIQUE_START)

#define ddTDA182I2_ERR_BAD_UNIT_NUMBER    (ddTDA182I2_ERR_BASE + TM_ERR_BAD_UNIT_NUMBER)
#define ddTDA182I2_ERR_NOT_INITIALIZED    (ddTDA182I2_ERR_BASE + TM_ERR_NOT_INITIALIZED)
#define ddTDA182I2_ERR_INIT_FAILED        (ddTDA182I2_ERR_BASE + TM_ERR_INIT_FAILED)
#define ddTDA182I2_ERR_BAD_PARAMETER      (ddTDA182I2_ERR_BASE + TM_ERR_BAD_PARAMETER)
#define ddTDA182I2_ERR_NOT_SUPPORTED      (ddTDA182I2_ERR_BASE + TM_ERR_NOT_SUPPORTED)
#define ddTDA182I2_ERR_HW_FAILED          (ddTDA182I2_ERR_COMP + 0x0001)
#define ddTDA182I2_ERR_NOT_READY          (ddTDA182I2_ERR_COMP + 0x0002)
#define ddTDA182I2_ERR_BAD_VERSION        (ddTDA182I2_ERR_COMP + 0x0003)


    typedef enum _tmddTDA182I2PowerState_t {
        tmddTDA182I2_PowerNormalMode,                                               /* Device normal mode */
        tmddTDA182I2_PowerStandbyWithLNAOnAndWithXtalOnAndWithSyntheOn,             /* Device standby mode with LNA and Xtal Output and Synthe on*/
        tmddTDA182I2_PowerStandbyWithLNAOnAndWithXtalOn,                            /* Device standby mode with LNA and Xtal Output */
        tmddTDA182I2_PowerStandbyWithXtalOn,                                        /* Device standby mode with Xtal Output */
        tmddTDA182I2_PowerStandby,                                                  /* Device standby mode */
        tmddTDA182I2_PowerMax
    } tmddTDA182I2PowerState_t, *ptmddTDA182I2PowerState_t;

    typedef enum _tmddTDA182I2_LPF_Gain_t {
        tmddTDA182I2_LPF_Gain_Unknown = 0,                                          /* LPF_Gain Unknown */
        tmddTDA182I2_LPF_Gain_Frozen,                                               /* LPF_Gain Frozen */
        tmddTDA182I2_LPF_Gain_Free                                                  /* LPF_Gain Free */
    } tmddTDA182I2_LPF_Gain_t, *ptmddTDA182I2_LPF_Gain_t;

    tmErrorCode_t
    tmddTDA182I2Init
    (
    tmUnitSelect_t    tUnit,    //  I: Unit number
    tmbslFrontEndDependency_t*  psSrvFunc   /*  I: setup parameters */
    );
    tmErrorCode_t 
    tmddTDA182I2DeInit
    (
    tmUnitSelect_t    tUnit     //  I: Unit number
    );
    tmErrorCode_t
    tmddTDA182I2GetSWVersion
    (
    ptmSWVersion_t    pSWVersion        //  I: Receives SW Version 
    );
    tmErrorCode_t
    tmddTDA182I2Reset
    (
    tmUnitSelect_t  tUnit     //  I: Unit number
    );
    tmErrorCode_t
    tmddTDA182I2SetLPF_Gain_Mode
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uMode   /* I: Unknown/Free/Frozen */
    );
    tmErrorCode_t
    tmddTDA182I2GetLPF_Gain_Mode
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           *puMode /* O: Unknown/Free/Frozen */
    );
    tmErrorCode_t
    tmddTDA182I2Write
    (
    tmUnitSelect_t      tUnit,      //  I: Unit number
    UInt32              uIndex,         //  I: Start index to write
    UInt32              uNbBytes,       //  I: Number of bytes to write
    UInt8*             puBytes         //  I: Pointer on an array of bytes
    );
    tmErrorCode_t
    tmddTDA182I2Read
    (
    tmUnitSelect_t      tUnit,      //  I: Unit number
    UInt32              uIndex,         //  I: Start index to read
    UInt32              uNbBytes,       //  I: Number of bytes to read
    UInt8*             puBytes         //  I: Pointer on an array of bytes
    );
    tmErrorCode_t
    tmddTDA182I2GetPOR
    (
    tmUnitSelect_t    tUnit,    //  I: Unit number
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetLO_Lock
    (
    tmUnitSelect_t    tUnit,    //  I: Unit number
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMS
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIdentity
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt16*        puValue      //  I: Address of the variable to output item value
    );
    tmErrorCode_t
    tmddTDA182I2GetMinorRevision
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMajorRevision
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetTM_D
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetTM_ON
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetTM_ON
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetPowerState
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    tmddTDA182I2PowerState_t  powerState    //  I: Power state of this device
    );
    tmErrorCode_t
    tmddTDA182I2GetPowerState
    (
    tmUnitSelect_t        tUnit,    //  I: Unit number
    ptmddTDA182I2PowerState_t    pPowerState  //  O: Power state of this device
    );

    tmErrorCode_t
    tmddTDA182I2GetPower_Level
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIRQ_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIRQ_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetXtalCal_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetXtalCal_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_RSSI_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_RSSI_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_LOCalc_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_LOCalc_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_RFCAL_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_RFCAL_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_IRCAL_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_IRCAL_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_RCCal_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_RCCal_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIRQ_Clear
    (
        tmUnitSelect_t  tUnit,      /* I: Unit number */
        UInt8           irqStatus   /* I: IRQs to clear */
    );
    tmErrorCode_t
    tmddTDA182I2SetXtalCal_Clear
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetXtalCal_Clear
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_RSSI_Clear
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_RSSI_Clear
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_LOCalc_Clear
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_LOCalc_Clear
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_RFCal_Clear
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_RFCal_Clear
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_IRCAL_Clear
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_IRCAL_Clear
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_RCCal_Clear
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_RCCal_Clear
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIRQ_Set
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIRQ_Set
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetXtalCal_Set
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetXtalCal_Set
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_RSSI_Set
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_RSSI_Set
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_LOCalc_Set
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_LOCalc_Set
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_RFCal_Set
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_RFCal_Set
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_IRCAL_Set
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_IRCAL_Set
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_RCCal_Set
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_RCCal_Set
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIRQ_status
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_XtalCal_End
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_RSSI_End
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_LOCalc_End
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_RFCal_End
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_IRCAL_End
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_RCCal_End
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetLT_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetLT_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC1_6_15dB
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC1_6_15dB
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC1_TOP
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC1_TOP
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC2_TOP
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC2_TOP
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGCs_Up_Step_assym
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetAGCs_Up_Step_assym
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGCs_Up_Step
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetAGCs_Up_Step
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetPulse_Shaper_Disable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetPulse_Shaper_Disable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGCK_Step
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGCK_Step
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGCK_Mode
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGCK_Mode
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetPD_RFAGC_Adapt
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetPD_RFAGC_Adapt
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFAGC_Adapt_TOP
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFAGC_Adapt_TOP
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFAGC_Low_BW
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFAGC_Low_BW
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRF_Atten_3dB
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRF_Atten_3dB
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFAGC_Top
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFAGC_Top
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_Mixer_Top
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_Mixer_Top
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGCs_Do_Step_assym
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetAGCs_Do_Step_assym
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC5_Ana
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC5_Ana
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC5_TOP
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC5_TOP
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIF_Level
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIF_Level
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIF_HP_Fc
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIF_HP_Fc
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIF_ATSC_Notch
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIF_ATSC_Notch
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetLP_FC_Offset
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetLP_FC_Offset
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetLP_FC
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetLP_FC
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetI2C_Clock_Mode
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetI2C_Clock_Mode
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetDigital_Clock_Mode
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetDigital_Clock_Mode
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetXtalOsc_AnaReg_En
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetXtalOsc_AnaReg_En
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetXTout
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetXTout
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIF_Freq
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt32          uValue      //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetIF_Freq
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt32*        puValue      //  I: Address of the variable to output item value
    );
    tmErrorCode_t
    tmddTDA182I2SetRF_Freq
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt32          uValue      //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRF_Freq
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt32*        puValue      //  I: Address of the variable to output item value
    );
    tmErrorCode_t
    tmddTDA182I2SetRSSI_Meas
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRSSI_Meas
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRF_CAL_AV
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRF_CAL_AV
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRF_CAL
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRF_CAL
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_CAL_Loop
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_CAL_Loop
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_Cal_Image
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_Cal_Image
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_CAL_Wanted
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_CAL_Wanted
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRC_Cal
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRC_Cal
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetCalc_PLL
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetCalc_PLL
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetXtalCal_Launch
    (
    tmUnitSelect_t      tUnit    //  I: Unit number
    );
    tmErrorCode_t
    tmddTDA182I2GetXtalCal_Launch
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetMSM_Launch
    (
    tmUnitSelect_t      tUnit    //  I: Unit number
    );
    tmErrorCode_t
    tmddTDA182I2GetMSM_Launch
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetPSM_AGC1
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetPSM_AGC1
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetPSM_StoB
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetPSM_StoB
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetPSMRFpoly
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetPSMRFpoly
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetPSM_Mixer
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetPSM_Mixer
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetPSM_Ifpoly
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetPSM_Ifpoly
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetPSM_Lodriver
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetPSM_Lodriver
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetDCC_Bypass
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetDCC_Bypass
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetDCC_Slow
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetDCC_Slow
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetDCC_psm
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetDCC_psm
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetFmax_Lo
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetFmax_Lo
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_Loop
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_Loop
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_Target
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_Target
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_GStep
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_GStep
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_Corr_Boost
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_Corr_Boost
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_FreqLow_Sel
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_FreqLow_Sel
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_mode_ram_store
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_mode_ram_store
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_FreqLow
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_FreqLow
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_FreqMid
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_FreqMid
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetCoarse_IR_FreqHigh
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetCoarse_IR_FreqHigh
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_FreqHigh
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_FreqHigh
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );

    tmErrorCode_t
    tmddTDA182I2SetPD_Vsync_Mgt
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetPD_Vsync_Mgt
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetPD_Ovld
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetPD_Ovld
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetPD_Udld
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetPD_Udld
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC_Ovld_TOP
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC_Ovld_TOP
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC_Ovld_Timer
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC_Ovld_Timer
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_Mixer_loop_off
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_Mixer_loop_off
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIR_Mixer_Do_step
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIR_Mixer_Do_step
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetHi_Pass
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetHi_Pass
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIF_Notch
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIF_Notch
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC1_loop_off
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC1_loop_off
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC1_Do_step
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC1_Do_step
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetForce_AGC1_gain
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetForce_AGC1_gain
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC1_Gain
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC1_Gain
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC5_loop_off
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC5_loop_off
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC5_Do_step
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC5_Do_step
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetForce_AGC5_gain
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetForce_AGC5_gain
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC5_Gain
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC5_Gain
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Freq0
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Freq0
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Freq1
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Freq1
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Freq2
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Freq2
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Freq3
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Freq3
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Freq4
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Freq4
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Freq5
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Freq5
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Freq6
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Freq6
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Freq7
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Freq7
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Freq8
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Freq8
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Freq9
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Freq9
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Freq10
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Freq10
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Freq11
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Freq11
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Offset0
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Offset0
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Offset1
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Offset1
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Offset2
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Offset2
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Offset3
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Offset3
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Offset4
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Offset4
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Offset5
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue   //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Offset5
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Offset6
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Offset6
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Offset7
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Offset7
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Offset8
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Offset8
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Offset9
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Offset9
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Offset10
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Offset10
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Offset11
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Offset11
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_SW_Algo_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_SW_Algo_Enable
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRF_Filter_Bypass
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRF_Filter_Bypass
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC2_loop_off
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC2_loop_off
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetForce_AGC2_gain
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetForce_AGC2_gain
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRF_Filter_Gv
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRF_Filter_Gv
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRF_Filter_Band
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRF_Filter_Band
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRF_Filter_Cap
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRF_Filter_Cap
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetAGC2_Do_step
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC2_Do_step
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetGain_Taper
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetGain_Taper
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRF_BPF
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRF_BPF
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRF_BPF_Bypass
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRF_BPF_Bypass
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetN_CP_Current
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetN_CP_Current
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetUp_AGC5
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetDo_AGC5
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetUp_AGC4
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetDo_AGC4
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetUp_AGC2
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetDo_AGC2
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetUp_AGC1
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetDo_AGC1
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC2_Gain_Read
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC1_Gain_Read
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetTOP_AGC3_Read
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC5_Gain_Read
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetAGC4_Gain_Read
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRSSI
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRSSI
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRSSI_AV
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRSSI_AV
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRSSI_Cap_Reset_En
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRSSI_Cap_Reset_En
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRSSI_Cap_Val
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRSSI_Cap_Val
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRSSI_Ck_Speed
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRSSI_Ck_Speed
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRSSI_Dicho_not
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRSSI_Dicho_not
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_Phi2
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_Phi2
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetDDS_Polarity
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    UInt8           uValue  //  I: Item value
    );
    tmErrorCode_t
    tmddTDA182I2GetDDS_Polarity
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetRFCAL_DeltaGain
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetRFCAL_DeltaGain
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2SetIRQ_Polarity
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8           uValue  /* I: Item value */
    );
    tmErrorCode_t
    tmddTDA182I2GetIRQ_Polarity
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2Getrfcal_log_0
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2Getrfcal_log_1
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2Getrfcal_log_2
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2Getrfcal_log_3
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2Getrfcal_log_4
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2Getrfcal_log_5
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2Getrfcal_log_6
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2Getrfcal_log_7
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2Getrfcal_log_8
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2Getrfcal_log_9
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2Getrfcal_log_10
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2Getrfcal_log_11
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
        UInt8*          puValue /* I: Address of the variable to output item value */
    );
    tmErrorCode_t
    tmddTDA182I2LaunchRF_CAL
    (
        tmUnitSelect_t  tUnit       /* I: Unit number */
    );
    tmErrorCode_t
    tmddTDA182I2WaitIRQ
    (
        tmUnitSelect_t  tUnit,      /* I: Unit number */
        UInt32          timeOut,    /* I: timeout */
        UInt32          waitStep,   /* I: wait step */
        UInt8           irqStatus   /* I: IRQs to wait */
    );
    tmErrorCode_t
    tmddTDA182I2WaitXtalCal_End
    (
        tmUnitSelect_t  tUnit,      /* I: Unit number */
        UInt32          timeOut,    /* I: timeout */
        UInt32          waitStep    /* I: wait step */
    );
    tmErrorCode_t
    tmddTDA182I2SetIRQWait
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    Bool            bWait       /* I: Determine if we need to wait IRQ in driver functions */
    );
    tmErrorCode_t
    tmddTDA182I2GetIRQWait
    (
        tmUnitSelect_t  tUnit,  /* I: Unit number */
    Bool*           pbWait      /* O: Determine if we need to wait IRQ in driver functions */
    );
    tmErrorCode_t
    tmddTDA182I2AGC1_Adapt
    (
        tmUnitSelect_t  tUnit   /* I: Unit number */
    );

#ifdef __cplusplus
}
#endif

#endif // _TMDD_TDA182I2_H //---------------























// NXP source code - .\tmddTDA182I2\inc\tmddTDA182I2local.h


//-----------------------------------------------------------------------------
// $Header: 
// (C) Copyright 2007 NXP Semiconductors, All rights reserved
//
// This source code and any compilation or derivative thereof is the sole
// property of NXP Corporation and is provided pursuant to a Software
// License Agreement.  This code is the proprietary information of NXP
// Corporation and is confidential in nature.  Its use and dissemination by
// any party other than NXP Corporation is strictly limited by the
// confidential information provisions of the Agreement referenced above.
//-----------------------------------------------------------------------------
// FILE NAME:    tmddTDA182I2local.h
//
// DESCRIPTION:  define the Object for the TDA182I2
//
// DOCUMENT REF: DVP Software Coding Guidelines v1.14
//               DVP Board Support Library Architecture Specification v0.5
//
// NOTES:        
//-----------------------------------------------------------------------------
//
#ifndef _TMDD_TDA182I2LOCAL_H //-----------------
#define _TMDD_TDA182I2LOCAL_H

//-----------------------------------------------------------------------------
// Standard include files:
//-----------------------------------------------------------------------------

//#include "tmddTDA182I2.h"

/*------------------------------------------------------------------------------*/
/* Project include files:                                                       */
/*------------------------------------------------------------------------------*/

#ifdef __cplusplus
extern "C"
{
#endif

    //-----------------------------------------------------------------------------
    // Types and defines:
    //-----------------------------------------------------------------------------
    //

#define ddTDA182I2_MUTEX_TIMEOUT       TMBSL_FRONTEND_MUTEX_TIMEOUT_INFINITE

#define _SYSTEMFUNC (pObj->SystemFunc)

#define POBJ_SRVFUNC_SIO pObj->sRWFunc
#define POBJ_SRVFUNC_STIME pObj->sTime
#define P_DBGPRINTEx pObj->sDebug.Print
#define P_DBGPRINTVALID ((pObj != Null) && (pObj->sDebug.Print != Null))

#define TDA182I2_DD_COMP_NUM    2 // Major protocol change - Specification change required
#define TDA182I2_DD_MAJOR_VER   4 // Minor protocol change - Specification change required
#define TDA182I2_DD_MINOR_VER   7 // Software update - No protocol change - No specification change required

#define TDA182I2_POWER_LEVEL_MIN  40
#define TDA182I2_POWER_LEVEL_MAX  110

#define TDA182I2_MAX_UNITS                  2
#define TDA182I2_I2C_MAP_NB_BYTES           68

#define TDA182I2_DEVICE_ID_MASTER  1
#define TDA182I2_DEVICE_ID_SLAVE   0


    typedef struct _TDA182I2_I2C_Map_t
    {
        union
        {
            UInt8 ID_byte_1;
            struct       
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 MS:1;
                UInt8 Ident_1:7;
#else
                UInt8 Ident_1:7;
                UInt8 MS:1;
#endif
            }bF;
        }uBx00;

        union
        {
            UInt8 ID_byte_2;
            struct
            {
                UInt8 Ident_2:8;
            }bF;
        }uBx01;

        union
        {
            UInt8 ID_byte_3;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 Major_rev:4;
				UInt8 Minor_rev:4;
#else
                UInt8 Minor_rev:4;
                UInt8 Major_rev:4;
#endif                
            }bF;
        }uBx02;

        union
        {
            UInt8 Thermo_byte_1;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:1;
				UInt8 TM_D:7;
#else
                UInt8 TM_D:7;
                UInt8 UNUSED_I0_D0:1;
#endif                
            }bF;
        }uBx03;

        union
        {
            UInt8 Thermo_byte_2;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:7;
				UInt8 TM_ON:1;
#else
                UInt8 TM_ON:1;
                UInt8 UNUSED_I0_D0:7;
#endif                
            }bF;
        }uBx04;

        union
        {
            UInt8 Power_state_byte_1;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:6;
                UInt8 POR:1;
				UInt8 LO_Lock:1;
#else
                UInt8 LO_Lock:1;
                UInt8 POR:1;
                UInt8 UNUSED_I0_D0:6;
#endif                
            }bF;
        }uBx05;

        union
        {
            UInt8 Power_state_byte_2;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:4;
                UInt8 SM:1;
                UInt8 SM_Synthe:1;
                UInt8 SM_LT:1;
				UInt8 SM_XT:1;
#else
                UInt8 SM_XT:1;
                UInt8 SM_LT:1;
                UInt8 SM_Synthe:1;
                UInt8 SM:1;
                UInt8 UNUSED_I0_D0:4;
#endif                
            }bF;
        }uBx06;

        union
        {
            UInt8 Input_Power_Level_byte;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:1;
				UInt8 Power_Level:7;
#else
                UInt8 Power_Level:7;
                UInt8 UNUSED_I0_D0:1;
#endif                
            }bF;
        }uBx07;

        union
        {
            UInt8 IRQ_status;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 IRQ_status:1;
                UInt8 UNUSED_I0_D0:1;
                UInt8 MSM_XtalCal_End:1;
                UInt8 MSM_RSSI_End:1;
                UInt8 MSM_LOCalc_End:1;
                UInt8 MSM_RFCal_End:1;
                UInt8 MSM_IRCAL_End:1;
				UInt8 MSM_RCCal_End:1;				    
#else
                UInt8 MSM_RCCal_End:1;
                UInt8 MSM_IRCAL_End:1;
                UInt8 MSM_RFCal_End:1;
                UInt8 MSM_LOCalc_End:1;
                UInt8 MSM_RSSI_End:1;
                UInt8 MSM_XtalCal_End:1;
                UInt8 UNUSED_I0_D0:1;
                UInt8 IRQ_status:1;
#endif                
            }bF;
        }uBx08;

        union
        {
            UInt8 IRQ_enable;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 IRQ_Enable:1;
                UInt8 UNUSED_I0_D0:1;
                UInt8 XtalCal_Enable:1;
                UInt8 MSM_RSSI_Enable:1;
                UInt8 MSM_LOCalc_Enable:1;
                UInt8 MSM_RFCAL_Enable:1;
                UInt8 MSM_IRCAL_Enable:1;
				UInt8 MSM_RCCal_Enable:1;
#else
                UInt8 MSM_RCCal_Enable:1;
                UInt8 MSM_IRCAL_Enable:1;
                UInt8 MSM_RFCAL_Enable:1;
                UInt8 MSM_LOCalc_Enable:1;
                UInt8 MSM_RSSI_Enable:1;
                UInt8 XtalCal_Enable:1;
                UInt8 UNUSED_I0_D0:1;
                UInt8 IRQ_Enable:1;
#endif                
            }bF;
        }uBx09;

        union
        {
            UInt8 IRQ_clear;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 IRQ_Clear:1;
                UInt8 UNUSED_I0_D0:1;
                UInt8 XtalCal_Clear:1;
                UInt8 MSM_RSSI_Clear:1;
                UInt8 MSM_LOCalc_Clear:1;
                UInt8 MSM_RFCal_Clear:1;
                UInt8 MSM_IRCAL_Clear:1;
				UInt8 MSM_RCCal_Clear:1;
#else
                UInt8 MSM_RCCal_Clear:1;
                UInt8 MSM_IRCAL_Clear:1;
                UInt8 MSM_RFCal_Clear:1;
                UInt8 MSM_LOCalc_Clear:1;
                UInt8 MSM_RSSI_Clear:1;
                UInt8 XtalCal_Clear:1;
                UInt8 UNUSED_I0_D0:1;
                UInt8 IRQ_Clear:1;
#endif                
            }bF;
        }uBx0A;

        union
        {
            UInt8 IRQ_set;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 IRQ_Set:1;
                UInt8 UNUSED_I0_D0:1;
                UInt8 XtalCal_Set:1;
                UInt8 MSM_RSSI_Set:1;
                UInt8 MSM_LOCalc_Set:1;
                UInt8 MSM_RFCal_Set:1;
                UInt8 MSM_IRCAL_Set:1;
				UInt8 MSM_RCCal_Set:1;
#else
                UInt8 MSM_RCCal_Set:1;
                UInt8 MSM_IRCAL_Set:1;
                UInt8 MSM_RFCal_Set:1;
                UInt8 MSM_LOCalc_Set:1;
                UInt8 MSM_RSSI_Set:1;
                UInt8 XtalCal_Set:1;
                UInt8 UNUSED_I0_D0:1;
                UInt8 IRQ_Set:1;
#endif                
            }bF;
        }uBx0B;

        union
        {
            UInt8 AGC1_byte_1;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 LT_Enable:1;
                UInt8 AGC1_6_15dB:1;
                UInt8 UNUSED_I0_D0:2;
				UInt8 AGC1_TOP:4;
#else
                UInt8 AGC1_TOP:4;
                UInt8 UNUSED_I0_D0:2;
                UInt8 AGC1_6_15dB:1;
                UInt8 LT_Enable:1;
#endif                
            }bF;
        }uBx0C;

        union
        {
            UInt8 AGC2_byte_1;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:3;
				UInt8 AGC2_TOP:5;
#else
                UInt8 AGC2_TOP:5;
                UInt8 UNUSED_I0_D0:3;
#endif                
            }bF;
        }uBx0D;

        union
        {
            UInt8 AGCK_byte_1;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 AGCs_Up_Step_assym:2;
                UInt8 AGCs_Up_Step:1;
                UInt8 Pulse_Shaper_Disable:1;
                UInt8 AGCK_Step:2;
				UInt8 AGCK_Mode:2;
#else
                UInt8 AGCK_Mode:2;
                UInt8 AGCK_Step:2;
                UInt8 Pulse_Shaper_Disable:1;
                UInt8 AGCs_Up_Step:1;
                UInt8 AGCs_Up_Step_assym:2;
#endif                
            }bF;
        }uBx0E;

        union
        {
            UInt8 RF_AGC_byte;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 PD_RFAGC_Adapt:1;
                UInt8 RFAGC_Adapt_TOP:2;
                UInt8 RFAGC_Low_BW:1;
                UInt8 RF_Atten_3dB:1;
				UInt8 RFAGC_Top:3;
#else
                UInt8 RFAGC_Top:3;
                UInt8 RF_Atten_3dB:1;
                UInt8 RFAGC_Low_BW:1;
                UInt8 RFAGC_Adapt_TOP:2;
                UInt8 PD_RFAGC_Adapt:1;
#endif                
            }bF;
        }uBx0F;

        union
        {
            UInt8 IR_Mixer_byte_1;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:4;
				UInt8 IR_Mixer_Top:4;
#else
                UInt8 IR_Mixer_Top:4;
                UInt8 UNUSED_I0_D0:4;
#endif                
            }bF;
        }uBx10;

        union
        {
            UInt8 AGC5_byte_1;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:1;
                UInt8 AGCs_Do_Step_assym:2;
                UInt8 AGC5_Ana:1;
				UInt8 AGC5_TOP:4;
#else
                UInt8 AGC5_TOP:4;
                UInt8 AGC5_Ana:1;
                UInt8 AGCs_Do_Step_assym:2;
                UInt8 UNUSED_I0_D0:1;
#endif                
            }bF;
        }uBx11;

        union
        {
            UInt8 IF_AGC_byte;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:5;
				UInt8 IF_level:3;
#else
                UInt8 IF_level:3;
                UInt8 UNUSED_I0_D0:5;
#endif                
            }bF;
        }uBx12;

        union
        {
            UInt8 IF_Byte_1;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 IF_HP_Fc:2;
                UInt8 IF_ATSC_Notch:1;
                UInt8 LP_FC_Offset:2;
				UInt8 LP_Fc:3;
#else
                UInt8 LP_Fc:3;
                UInt8 LP_FC_Offset:2;
                UInt8 IF_ATSC_Notch:1;
                UInt8 IF_HP_Fc:2;
#endif                
            }bF;
        }uBx13;

        union
        {
            UInt8 Reference_Byte;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 I2C_Clock_Mode:1;
                UInt8 Digital_Clock_Mode:1;
                UInt8 UNUSED_I1_D0:1;
                UInt8 XtalOsc_AnaReg_En:1;
                UInt8 UNUSED_I0_D0:2;
				UInt8 XTout:2;
#else
                UInt8 XTout:2;
                UInt8 UNUSED_I0_D0:2;
                UInt8 XtalOsc_AnaReg_En:1;
                UInt8 UNUSED_I1_D0:1;
                UInt8 Digital_Clock_Mode:1;
                UInt8 I2C_Clock_Mode:1;
#endif
            }bF;
        }uBx14;

        union
        {
            UInt8 IF_Frequency_byte;
            struct
            {
                UInt8 IF_Freq:8;
            }bF;
        }uBx15;

        union
        {
            UInt8 RF_Frequency_byte_1;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:4;
				UInt8 RF_Freq_1:4;
#else
                UInt8 RF_Freq_1:4;
                UInt8 UNUSED_I0_D0:4;
#endif
            }bF;
        }uBx16;

        union
        {
            UInt8 RF_Frequency_byte_2;
            struct
            {
                UInt8 RF_Freq_2:8;
            }bF;
        }uBx17;


        union
        {
            UInt8 RF_Frequency_byte_3;
            struct
            {
                UInt8 RF_Freq_3:8;
            }bF;
        }uBx18;

        union
        {
            UInt8 MSM_byte_1;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 RSSI_Meas:1;
                UInt8 RF_CAL_AV:1;
                UInt8 RF_CAL:1;
                UInt8 IR_CAL_Loop:1;
                UInt8 IR_Cal_Image:1;
                UInt8 IR_CAL_Wanted:1;
                UInt8 RC_Cal:1;
				UInt8 Calc_PLL:1;
#else
                UInt8 Calc_PLL:1;
                UInt8 RC_Cal:1;
                UInt8 IR_CAL_Wanted:1;
                UInt8 IR_Cal_Image:1;
                UInt8 IR_CAL_Loop:1;
                UInt8 RF_CAL:1;
                UInt8 RF_CAL_AV:1;
                UInt8 RSSI_Meas:1;
#endif
            }bF;
        }uBx19;

        union
        {
            UInt8 MSM_byte_2;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:6;
                UInt8 XtalCal_Launch:1;
				UInt8 MSM_Launch:1;
#else
                UInt8 MSM_Launch:1;
                UInt8 XtalCal_Launch:1;
                UInt8 UNUSED_I0_D0:6;
#endif
            }bF;
        }uBx1A;

        union
        {
            UInt8 PowerSavingMode;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 PSM_AGC1:2;
                UInt8 PSM_StoB:1;
                UInt8 PSMRFpoly:1;
                UInt8 PSM_Mixer:1;
                UInt8 PSM_Ifpoly:1;
				UInt8 PSM_Lodriver:2;
#else
                UInt8 PSM_Lodriver:2;
                UInt8 PSM_Ifpoly:1;
                UInt8 PSM_Mixer:1;
                UInt8 PSMRFpoly:1;
                UInt8 PSM_StoB:1;
                UInt8 PSM_AGC1:2;
#endif
            }bF;
        }uBx1B;

        union
        {
            UInt8 DCC_byte;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 DCC_Bypass:1;
                UInt8 DCC_Slow:1;
                UInt8 DCC_psm:1;
                UInt8 UNUSED_I0_D0:5;
#else
                UInt8 UNUSED_I0_D0:5;
                UInt8 DCC_psm:1;
                UInt8 DCC_Slow:1;
                UInt8 DCC_Bypass:1;
#endif
            }bF;
        }uBx1C;

        union
        {
            UInt8 FLO_max_byte;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:2;
                UInt8 Fmax_Lo:6;
#else
                UInt8 Fmax_Lo:6;
                UInt8 UNUSED_I0_D0:2;
#endif
            }bF;
        }uBx1D;

        union
        {
            UInt8 IR_Cal_byte_1;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 IR_Loop:2;  
                UInt8 IR_Target:3;
                UInt8 IR_GStep:3;
#else
                UInt8 IR_GStep:3;
                UInt8 IR_Target:3;
                UInt8 IR_Loop:2;  
#endif
            }bF;
        }uBx1E;

        union
        {
            UInt8 IR_Cal_byte_2;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 IR_Corr_Boost:1;
                UInt8 IR_FreqLow_Sel:1;
                UInt8 IR_mode_ram_store:1;
                UInt8 IR_FreqLow:5;
#else
                UInt8 IR_FreqLow:5;
                UInt8 IR_mode_ram_store:1;
                UInt8 IR_FreqLow_Sel:1;
                UInt8 IR_Corr_Boost:1;
#endif
            }bF;
        }uBx1F;

        union
        {
            UInt8 IR_Cal_byte_3;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:3;
                UInt8 IR_FreqMid:5;
#else
                UInt8 IR_FreqMid:5;
                UInt8 UNUSED_I0_D0:3;
#endif
            }bF;
        }uBx20;

        union
        {
            UInt8 IR_Cal_byte_4;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:2;
                UInt8 Coarse_IR_FreqHigh:1;
                UInt8 IR_FreqHigh:5;
#else
                UInt8 IR_FreqHigh:5;
                UInt8 Coarse_IR_FreqHigh:1;
                UInt8 UNUSED_I0_D0:2;
#endif
            }bF;
        }uBx21;

        union
        {
            UInt8 Vsync_Mgt_byte;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 PD_Vsync_Mgt:1;
                UInt8 PD_Ovld:1;
                UInt8 PD_Udld:1;
                UInt8 AGC_Ovld_TOP:3;
                UInt8 AGC_Ovld_Timer:2;
#else
                UInt8 AGC_Ovld_Timer:2;
                UInt8 AGC_Ovld_TOP:3;
                UInt8 PD_Udld:1;
                UInt8 PD_Ovld:1;
                UInt8 PD_Vsync_Mgt:1;
#endif
            }bF;
        }uBx22;

        union
        {
            UInt8 IR_Mixer_byte_2;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 IR_Mixer_loop_off:1;
                UInt8 IR_Mixer_Do_step:2;
                UInt8 UNUSED_I0_D0:3;
                UInt8 Hi_Pass:1;
                UInt8 IF_Notch:1;
#else
                UInt8 IF_Notch:1;
                UInt8 Hi_Pass:1;
                UInt8 UNUSED_I0_D0:3;
                UInt8 IR_Mixer_Do_step:2;
                UInt8 IR_Mixer_loop_off:1;
#endif
            }bF;
        }uBx23;

        union
        {
            UInt8 AGC1_byte_2;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 AGC1_loop_off:1;
                UInt8 AGC1_Do_step:2;
                UInt8 Force_AGC1_gain:1;
                UInt8 AGC1_Gain:4;
#else
                UInt8 AGC1_Gain:4;
                UInt8 Force_AGC1_gain:1;
                UInt8 AGC1_Do_step:2;
                UInt8 AGC1_loop_off:1;
#endif
            }bF;
        }uBx24;

        union
        {
            UInt8 AGC5_byte_2;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 AGC5_loop_off:1;
                UInt8 AGC5_Do_step:2;
                UInt8 UNUSED_I1_D0:1;
                UInt8 Force_AGC5_gain:1;
                UInt8 UNUSED_I0_D0:1;
                UInt8 AGC5_Gain:2;
#else
                UInt8 AGC5_Gain:2;
                UInt8 UNUSED_I0_D0:1;
                UInt8 Force_AGC5_gain:1;
                UInt8 UNUSED_I1_D0:1;
                UInt8 AGC5_Do_step:2;
                UInt8 AGC5_loop_off:1;
#endif
            }bF;
        }uBx25;

        union
        {
            UInt8 RF_Cal_byte_1;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 RFCAL_Offset_Cprog0:2;
                UInt8 RFCAL_Freq0:2;
                UInt8 RFCAL_Offset_Cprog1:2;
                UInt8 RFCAL_Freq1:2;
#else
                UInt8 RFCAL_Freq1:2;
                UInt8 RFCAL_Offset_Cprog1:2;
                UInt8 RFCAL_Freq0:2;
                UInt8 RFCAL_Offset_Cprog0:2;
#endif
            }bF;
        }uBx26;

        union
        {
            UInt8 RF_Cal_byte_2;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 RFCAL_Offset_Cprog2:2;
                UInt8 RFCAL_Freq2:2;
                UInt8 RFCAL_Offset_Cprog3:2;
                UInt8 RFCAL_Freq3:2;
#else
                UInt8 RFCAL_Freq3:2;
                UInt8 RFCAL_Offset_Cprog3:2;
                UInt8 RFCAL_Freq2:2;
                UInt8 RFCAL_Offset_Cprog2:2;
#endif
            }bF;
        }uBx27;

        union
        {
            UInt8 RF_Cal_byte_3;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 RFCAL_Offset_Cprog4:2;
                UInt8 RFCAL_Freq4:2;
                UInt8 RFCAL_Offset_Cprog5:2;
                UInt8 RFCAL_Freq5:2;
#else
                UInt8 RFCAL_Freq5:2;
                UInt8 RFCAL_Offset_Cprog5:2;
                UInt8 RFCAL_Freq4:2;
                UInt8 RFCAL_Offset_Cprog4:2;
#endif
            }bF;
        }uBx28;

        union
        {
            UInt8 RF_Cal_byte_4;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 RFCAL_Offset_Cprog6:2;
                UInt8 RFCAL_Freq6:2;
                UInt8 RFCAL_Offset_Cprog7:2;
                UInt8 RFCAL_Freq7:2;
#else
                UInt8 RFCAL_Freq7:2;
                UInt8 RFCAL_Offset_Cprog7:2;
                UInt8 RFCAL_Freq6:2;
                UInt8 RFCAL_Offset_Cprog6:2;
#endif
            }bF;
        }uBx29;

        union
        {
            UInt8 RF_Cal_byte_5;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 RFCAL_Offset_Cprog8:2;
                UInt8 RFCAL_Freq8:2;
                UInt8 RFCAL_Offset_Cprog9:2;
                UInt8 RFCAL_Freq9:2;
#else
                UInt8 RFCAL_Freq9:2;
                UInt8 RFCAL_Offset_Cprog9:2;
                UInt8 RFCAL_Freq8:2;
                UInt8 RFCAL_Offset_Cprog8:2;
#endif
            }bF;
        }uBx2A;

        union
        {
            UInt8 RF_Cal_byte_6;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 RFCAL_Offset_Cprog10:2;
                UInt8 RFCAL_Freq10:2;
                UInt8 RFCAL_Offset_Cprog11:2;
                UInt8 RFCAL_Freq11:2;
#else
                UInt8 RFCAL_Freq11:2;
                UInt8 RFCAL_Offset_Cprog11:2;
                UInt8 RFCAL_Freq10:2;
                UInt8 RFCAL_Offset_Cprog10:2;
#endif
            }bF;
        }uBx2B;

        union
        {
            UInt8 RF_Filters_byte_1;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 RF_Filter_Bypass:1;
                UInt8 UNUSED_I0_D0:1;
                UInt8 AGC2_loop_off:1;
                UInt8 Force_AGC2_gain:1;
                UInt8 RF_Filter_Gv:2;
                UInt8 RF_Filter_Band:2;
#else
                
                UInt8 RF_Filter_Band:2;
                UInt8 RF_Filter_Gv:2;
                UInt8 Force_AGC2_gain:1;
                UInt8 AGC2_loop_off:1;
                UInt8 UNUSED_I0_D0:1;
                UInt8 RF_Filter_Bypass:1;
#endif
            }bF;
        }uBx2C;

        union
        {
            UInt8 RF_Filters_byte_2;
            struct
            {
                UInt8 RF_Filter_Cap:8;
            }bF;
        }uBx2D;

        union
        {
            UInt8 RF_Filters_byte_3;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 AGC2_Do_step:2;
                UInt8 Gain_Taper:6;
#else
                UInt8 Gain_Taper:6;
                UInt8 AGC2_Do_step:2;
#endif
            }bF;
        }uBx2E;

        union
        {
            UInt8 RF_Band_Pass_Filter_byte;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 RF_BPF_Bypass:1;
                UInt8 UNUSED_I0_D0:4;
                UInt8 RF_BPF:3;
#else
                UInt8 RF_BPF:3;
                UInt8 UNUSED_I0_D0:4;
                UInt8 RF_BPF_Bypass:1;
#endif
            }bF;
        }uBx2F;

        union
        {
            UInt8 CP_Current_byte;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:1;
                UInt8 N_CP_Current:7;
#else
                UInt8 N_CP_Current:7;
                UInt8 UNUSED_I0_D0:1;
#endif
            }bF;
        }uBx30;

        union
        {
            UInt8 AGCs_DetOut_byte;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 Up_AGC5:1;
                UInt8 Do_AGC5:1;
                UInt8 Up_AGC4:1;
                UInt8 Do_AGC4:1;
                UInt8 Up_AGC2:1;
                UInt8 Do_AGC2:1;
                UInt8 Up_AGC1:1;
                UInt8 Do_AGC1:1;
#else
                UInt8 Do_AGC1:1;
                UInt8 Up_AGC1:1;
                UInt8 Do_AGC2:1;
                UInt8 Up_AGC2:1;
                UInt8 Do_AGC4:1;
                UInt8 Up_AGC4:1;
                UInt8 Do_AGC5:1;
                UInt8 Up_AGC5:1;
#endif
            }bF;
        }uBx31;

        union
        {
            UInt8 RFAGCs_Gain_byte_1;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:2;
                UInt8 AGC2_Gain_Read:2;
                UInt8 AGC1_Gain_Read:4;
#else
                UInt8 AGC1_Gain_Read:4;
                UInt8 AGC2_Gain_Read:2;
                UInt8 UNUSED_I0_D0:2;
#endif
            }bF;
        }uBx32;

        union
        {
            UInt8 RFAGCs_Gain_byte_2;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:5;
                UInt8 TOP_AGC3_Read:3;
#else
                UInt8 TOP_AGC3_Read:3;
                UInt8 UNUSED_I0_D0:5;
#endif
            }bF;
        }uBx33;

        union
        {
            UInt8 IFAGCs_Gain_byte;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I0_D0:3;
                UInt8 AGC5_Gain_Read:2;
                UInt8 AGC4_Gain_Read:3;
#else
                UInt8 AGC4_Gain_Read:3;
                UInt8 AGC5_Gain_Read:2;
                UInt8 UNUSED_I0_D0:3;
#endif
            }bF;
        }uBx34;

        union
        {
#ifdef _TARGET_PLATFORM_MSB_FIRST
#else
#endif
            UInt8 RSSI_byte_1;
            UInt8 RSSI;
        }uBx35;

        union
        {
            UInt8 RSSI_byte_2;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 UNUSED_I1_D0:2;
                UInt8 RSSI_AV:1;
                UInt8 UNUSED_I0_D0:1;
                UInt8 RSSI_Cap_Reset_En:1;
                UInt8 RSSI_Cap_Val:1;
                UInt8 RSSI_Ck_Speed:1;
                UInt8 RSSI_Dicho_not:1;
#else
                UInt8 RSSI_Dicho_not:1;
                UInt8 RSSI_Ck_Speed:1;
                UInt8 RSSI_Cap_Val:1;
                UInt8 RSSI_Cap_Reset_En:1;
                UInt8 UNUSED_I0_D0:1;
                UInt8 RSSI_AV:1;
                UInt8 UNUSED_I1_D0:2;
#endif
            }bF;
        }uBx36;

        union
        {
            UInt8 Misc_byte;
            struct
            {
#ifdef _TARGET_PLATFORM_MSB_FIRST
                UInt8 RFCAL_Phi2:2;
                UInt8 DDS_Polarity:1;
                UInt8 RFCAL_DeltaGain:4;
                UInt8 IRQ_Polarity:1;
#else
                UInt8 IRQ_Polarity:1;
                UInt8 RFCAL_DeltaGain:4;
                UInt8 DDS_Polarity:1;
                UInt8 RFCAL_Phi2:2;
#endif
            }bF;
        }uBx37;

        union
        {
            UInt8 rfcal_log_0;
            struct       
            {
                UInt8 rfcal_log_0:8;
            }bF;
        }uBx38;

        union
        {
            UInt8 rfcal_log_1;
            struct       
            {
                UInt8 rfcal_log_1:8;
            }bF;
        }uBx39;

        union
        {
            UInt8 rfcal_log_2;
            struct       
            {
                UInt8 rfcal_log_2:8;
            }bF;
        }uBx3A;

        union
        {
            UInt8 rfcal_log_3;
            struct       
            {
                UInt8 rfcal_log_3:8;
            }bF;
        }uBx3B;

        union
        {
            UInt8 rfcal_log_4;
            struct       
            {
                UInt8 rfcal_log_4:8;
            }bF;
        }uBx3C;

        union
        {
            UInt8 rfcal_log_5;
            struct       
            {
                UInt8 rfcal_log_5:8;
            }bF;
        }uBx3D;

        union
        {
            UInt8 rfcal_log_6;
            struct       
            {
                UInt8 rfcal_log_6:8;
            }bF;
        }uBx3E;

        union
        {
            UInt8 rfcal_log_7;
            struct       
            {
                UInt8 rfcal_log_7:8;
            }bF;
        }uBx3F;

        union
        {
            UInt8 rfcal_log_8;
            struct       
            {
                UInt8 rfcal_log_8:8;
            }bF;
        }uBx40;

        union
        {
            UInt8 rfcal_log_9;
            struct       
            {
                UInt8 rfcal_log_9:8;
            }bF;
        }uBx41;

        union
        {
            UInt8 rfcal_log_10;
            struct       
            {
                UInt8 rfcal_log_10:8;
            }bF;
        }uBx42;

        union
        {
            UInt8 rfcal_log_11;
            struct       
            {
                UInt8 rfcal_log_11:8;
            }bF;
        }uBx43;

    } TDA182I2_I2C_Map_t, *pTDA182I2_I2C_Map_t;

    typedef struct _tmTDA182I2_RFCalProg_t {
        UInt8   Cal_number;
        Int8    DeltaCprog;
        Int8    CprogOffset;
    } tmTDA182I2_RFCalProg_t, *ptmTDA182I2_RFCalProg_t;

    typedef struct _tmTDA182I2_RFCalCoeffs_t {
        UInt8   Sub_band;
        UInt8   Cal_number;
        Int32   RF_A1;
        Int32   RF_B1;
    } tmTDA182I2_RFCalCoeffs_t, *ptmTDA182I2_RFCalCoeffs_t;

#define TDA182I2_RFCAL_PROG_ROW (12)
#define TDA182I2_RFCAL_COEFFS_ROW (8)

    typedef struct _tmddTDA182I2Object_t {
        tmUnitSelect_t              tUnit;
        tmUnitSelect_t              tUnitW;
        ptmbslFrontEndMutexHandle   pMutex;
        Bool                        init;
        tmbslFrontEndIoFunc_t       sRWFunc;
        tmbslFrontEndTimeFunc_t     sTime;
        tmbslFrontEndDebugFunc_t    sDebug;
        tmbslFrontEndMutexFunc_t    sMutex;
        tmddTDA182I2PowerState_t    curPowerState;
        Bool                        bIRQWait;
        TDA182I2_I2C_Map_t          I2CMap;
    } tmddTDA182I2Object_t, *ptmddTDA182I2Object_t, **pptmddTDA182I2Object_t;


    extern tmErrorCode_t ddTDA182I2GetIRQ_status(ptmddTDA182I2Object_t pObj, UInt8* puValue);
    extern tmErrorCode_t ddTDA182I2GetMSM_XtalCal_End(ptmddTDA182I2Object_t pObj, UInt8* puValue);

    extern tmErrorCode_t ddTDA182I2WaitIRQ(ptmddTDA182I2Object_t pObj, UInt32 timeOut, UInt32 waitStep, UInt8 irqStatus);
    extern tmErrorCode_t ddTDA182I2WaitXtalCal_End(ptmddTDA182I2Object_t pObj, UInt32 timeOut, UInt32 waitStep);

    extern tmErrorCode_t ddTDA182I2Write(ptmddTDA182I2Object_t pObj, UInt8 uSubAddress, UInt8 uNbData);
    extern tmErrorCode_t ddTDA182I2Read(ptmddTDA182I2Object_t pObj, UInt8 uSubAddress, UInt8 uNbData);
    extern tmErrorCode_t ddTDA182I2Wait(ptmddTDA182I2Object_t pObj, UInt32 Time);

    extern tmErrorCode_t ddTDA182I2MutexAcquire(ptmddTDA182I2Object_t   pObj, UInt32 timeOut);
    extern tmErrorCode_t ddTDA182I2MutexRelease(ptmddTDA182I2Object_t   pObj);
	extern tmErrorCode_t tmddTDA182I2AGC1_Adapt(tmUnitSelect_t tUnit);

#ifdef __cplusplus
}
#endif

#endif // _TMDD_TDA182I2LOCAL_H //---------------























// NXP source code - .\tmddTDA182I2\inc\tmddTDA182I2Instance.h


/*-----------------------------------------------------------------------------
// $Header: 
// (C) Copyright 2001 NXP Semiconductors, All rights reserved
//
// This source code and any compilation or derivative thereof is the sole
// property of NXP Corporation and is provided pursuant to a Software
// License Agreement.  This code is the proprietary information of NXP
// Corporation and is confidential in nature.  Its use and dissemination by
// any party other than NXP Corporation is strictly limited by the
// confidential information provisions of the Agreement referenced above.
//-----------------------------------------------------------------------------
// FILE NAME:    tmddTDA182I2Instance.h
//
// DESCRIPTION:  define the static Objects
//
// DOCUMENT REF: DVP Software Coding Guidelines v1.14
//               DVP Board Support Library Architecture Specification v0.5
//
// NOTES:        
//-----------------------------------------------------------------------------
*/
#ifndef _TMDDTDA182I2_INSTANCE_H //-----------------
#define _TMDDTDA182I2_INSTANCE_H

tmErrorCode_t ddTDA182I2AllocInstance (tmUnitSelect_t tUnit, pptmddTDA182I2Object_t ppDrvObject);
tmErrorCode_t ddTDA182I2DeAllocInstance (tmUnitSelect_t tUnit);
tmErrorCode_t ddTDA182I2GetInstance (tmUnitSelect_t tUnit, pptmddTDA182I2Object_t ppDrvObject);


#endif // _TMDDTDA182I2_INSTANCE_H //---------------



























// The following context is TDA18272 tuner API source code





// Definitions

// Unit number
enum TDA18272_UNIT_NUM
{
	TDA18272_UNIT_0,	// For master tuner or single tuner only.
	TDA18272_UNIT_1,	// For slave tuner only.
};


// IF output Vp-p mode
enum TDA18272_IF_OUTPUT_VPP_MODE
{
	TDA18272_IF_OUTPUT_VPP_2V    = tmTDA182I2_IF_AGC_Gain_2Vpp_0_30dB,
	TDA18272_IF_OUTPUT_VPP_1P25V = tmTDA182I2_IF_AGC_Gain_1_25Vpp_min_4_26dB,
	TDA18272_IF_OUTPUT_VPP_1V    = tmTDA182I2_IF_AGC_Gain_1Vpp_min_6_24dB,
	TDA18272_IF_OUTPUT_VPP_0P8V  = tmTDA182I2_IF_AGC_Gain_0_8Vpp_min_8_22dB,
	TDA18272_IF_OUTPUT_VPP_0P85V = tmTDA182I2_IF_AGC_Gain_0_85Vpp_min_7_5_22_5dB,
	TDA18272_IF_OUTPUT_VPP_0P7V  = tmTDA182I2_IF_AGC_Gain_0_7Vpp_min_9_21dB,
	TDA18272_IF_OUTPUT_VPP_0P6V  = tmTDA182I2_IF_AGC_Gain_0_6Vpp_min_10_3_19_7dB,
	TDA18272_IF_OUTPUT_VPP_0P5V  = tmTDA182I2_IF_AGC_Gain_0_5Vpp_min_12_18dB,
	TDA18272_IF_OUTPUT_VPP_MAX   = tmTDA182I2_IF_AGC_Gain_Max,
};


// Standard bandwidth mode
enum TDA18272_STANDARD_BANDWIDTH_MODE
{
	TDA18272_STANDARD_BANDWIDTH_DVBT_6MHZ  = tmTDA182I2_DVBT_6MHz,
	TDA18272_STANDARD_BANDWIDTH_DVBT_7MHZ  = tmTDA182I2_DVBT_7MHz,
	TDA18272_STANDARD_BANDWIDTH_DVBT_8MHZ  = tmTDA182I2_DVBT_8MHz,
	TDA18272_STANDARD_BANDWIDTH_QAM_6MHZ   = tmTDA182I2_QAM_6MHz,
	TDA18272_STANDARD_BANDWIDTH_QAM_8MHZ   = tmTDA182I2_QAM_8MHz,
	TDA18272_STANDARD_BANDWIDTH_ISDBT_6MHZ = tmTDA182I2_ISDBT_6MHz,
	TDA18272_STANDARD_BANDWIDTH_ATSC_6MHZ  = tmTDA182I2_ATSC_6MHz,
	TDA18272_STANDARD_BANDWIDTH_DMBT_8MHZ  = tmTDA182I2_DMBT_8MHz,
};


// Power mode
enum TDA18272_POWER_MODE
{
	TDA18272_POWER_NORMAL                             = tmTDA182I2_PowerNormalMode,
	TDA18272_POWER_STANDBY_WITH_XTALOUT_LNA_SYNTHE_ON = tmTDA182I2_PowerStandbyWithLNAOnAndWithXtalOnAndSynthe,
	TDA18272_POWER_STANDBY_WITH_XTALOUT_LNA_ON        = tmTDA182I2_PowerStandbyWithLNAOnAndWithXtalOn,
	TDA18272_POWER_STANDBY_WITH_XTALOUT_ON            = tmTDA182I2_PowerStandbyWithXtalOn,
	TDA18272_POWER_STANDBY                            = tmTDA182I2_PowerStandby,
};





// Builder
void
BuildTda18272Module(
	TUNER_MODULE **ppTuner,
	TUNER_MODULE *pTunerModuleMemory,
	BASE_INTERFACE_MODULE *pBaseInterfaceModuleMemory,
	I2C_BRIDGE_MODULE *pI2cBridgeModuleMemory,
	unsigned char DeviceAddr,
	unsigned long CrystalFreqHz,
	int UnitNo,
	int IfOutputVppMode
	);





// Manipulaing functions
void
tda18272_GetTunerType(
	TUNER_MODULE *pTuner,
	int *pTunerType
	);

void
tda18272_GetDeviceAddr(
	TUNER_MODULE *pTuner,
	unsigned char *pDeviceAddr
	);

int
tda18272_Initialize(
	TUNER_MODULE *pTuner
	);

int
tda18272_SetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long RfFreqHz
	);

int
tda18272_GetRfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long *pRfFreqHz
	);





// Extra manipulaing functions
int
tda18272_SetStandardBandwidthMode(
	TUNER_MODULE *pTuner,
	int StandardBandwidthMode
	);

int
tda18272_GetStandardBandwidthMode(
	TUNER_MODULE *pTuner,
	int *pStandardBandwidthMode
	);

int
tda18272_SetPowerMode(
	TUNER_MODULE *pTuner,
	int PowerMode
	);

int
tda18272_GetPowerMode(
	TUNER_MODULE *pTuner,
	int *pPowerMode
	);

int
tda18272_GetIfFreqHz(
	TUNER_MODULE *pTuner,
	unsigned long *pIfFreqHz
	);

























// The following context is implemented for TDA18272 source code.


// Functions
tmErrorCode_t
tda18272_Read(
	tmUnitSelect_t tUnit,
	UInt32 AddrSize,
	UInt8* pAddr,
	UInt32 ReadLen,
	UInt8* pData
	);

tmErrorCode_t
tda18272_Write(
	tmUnitSelect_t tUnit,
	UInt32 AddrSize,
	UInt8* pAddr,
	UInt32 WriteLen,
	UInt8* pData
	);

tmErrorCode_t
tda18272_Wait(
	tmUnitSelect_t tUnit,
	UInt32 tms
	);













#endif
