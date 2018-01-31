/* SPDX-License-Identifier: GPL-2.0 */
#ifndef __S5K6AA_H__
#define __S5K6AA_H__

struct reginfo
{
    u16 reg;
    u16 val;
};

/* General purpose section */
#define REG_TC_GP_SpecialEffects		0x01EE
#define REG_TC_GP_EnablePreview			0x01F0
#define REG_TC_GP_EnablePreviewChanged		0x01F2
#define REG_TC_GP_EnableCapture			0x01F4
#define REG_TC_GP_EnableCaptureChanged		0x01F6
#define REG_TC_GP_NewConfigSync			0x01F8
#define REG_TC_GP_PrevReqInputWidth		0x01FA
#define REG_TC_GP_PrevReqInputHeight		0x01FC
#define REG_TC_GP_PrevInputWidthOfs		0x01FE
#define REG_TC_GP_PrevInputHeightOfs		0x0200
#define REG_TC_GP_CapReqInputWidth		0x0202
#define REG_TC_GP_CapReqInputHeight		0x0204
#define REG_TC_GP_CapInputWidthOfs		0x0206
#define REG_TC_GP_CapInputHeightOfs		0x0208
#define REG_TC_GP_PrevZoomReqInputWidth		0x020A
#define REG_TC_GP_PrevZoomReqInputHeight	0x020C
#define REG_TC_GP_PrevZoomReqInputWidthOfs	0x020E
#define REG_TC_GP_PrevZoomReqInputHeightOfs	0x0210
#define REG_TC_GP_CapZoomReqInputWidth		0x0212
#define REG_TC_GP_CapZoomReqInputHeight		0x0214
#define REG_TC_GP_CapZoomReqInputWidthOfs	0x0216
#define REG_TC_GP_CapZoomReqInputHeightOfs	0x0218
#define REG_TC_GP_InputsChangeRequest		0x021A
#define REG_TC_GP_ActivePrevConfig		0x021C
#define REG_TC_GP_PrevConfigChanged		0x021E
#define REG_TC_GP_PrevOpenAfterChange		0x0220
#define REG_TC_GP_ErrorPrevConfig		0x0222
#define REG_TC_GP_ActiveCapConfig		0x0224
#define REG_TC_GP_CapConfigChanged		0x0226
#define REG_TC_GP_ErrorCapConfig		0x0228
#define REG_TC_GP_PrevConfigBypassChanged	0x022A
#define REG_TC_GP_CapConfigBypassChanged	0x022C
#define REG_TC_GP_SleepMode			0x022E
#define REG_TC_GP_SleepModeChanged		0x0230
#define REG_TC_GP_SRA_AddLow			0x0232
#define REG_TC_GP_SRA_AddHigh			0x0234
#define REG_TC_GP_SRA_AccessType		0x0236
#define REG_TC_GP_SRA_Changed			0x0238
#define REG_TC_GP_PrevMinFrTimeMsecMult10	0x023A
#define REG_TC_GP_PrevOutKHzRate		0x023C
#define REG_TC_GP_CapMinFrTimeMsecMult10	0x023E
#define REG_TC_GP_CapOutKHzRate			0x0240

/* Image property control section */
#define REG_TC_UserBrightness			0x01E4
#define REG_TC_UserContrast			0x01E6
#define REG_TC_UserSaturation			0x01E8
#define REG_TC_UserSharpBlur			0x01EA
#define REG_TC_UserGlamour			0x01EC

/* Flash control section */
#define REG_TC_FLS_Mode				0x03B6
#define REG_TC_FLS_Threshold			0x03B8
#define REG_TC_FLS_Polarity			0x03BA
#define REG_TC_FLS_XenonMode			0x03BC
#define REG_TC_FLS_XenonPreFlashCnt		0x03BE

/* Extended image property control section */
#define REG_SF_USER_LeiLow			0x03C0
#define REG_SF_USER_LeiHigh			0x03C2
#define REG_SF_USER_LeiChanged			0x03C4
#define REG_SF_USER_Exposure			0x03C6
#define REG_SF_USER_ExposureChanged		0x03CA
#define REG_SF_USER_TotalGain			0x03CC
#define REG_SF_USER_TotalGainChanged		0x03CE
#define REG_SF_USER_Rgain			0x03D0
#define REG_SF_USER_RgainChanged		0x03D2
#define REG_SF_USER_Ggain			0x03D4
#define REG_SF_USER_GgainChanged		0x03D6
#define REG_SF_USER_Bgain			0x03D8
#define REG_SF_USER_BgainChanged		0x03DA
#define REG_SF_USER_FlickerQuant		0x03DC
#define REG_SF_USER_FlickerQuantChanged		0x03DE
#define REG_SF_USER_GASRAlphaVal		0x03E0
#define REG_SF_USER_GASRAlphaChanged		0x03E2
#define REG_SF_USER_GASGAlphaVal		0x03E4
#define REG_SF_USER_GASGAlphaChanged		0x03E6
#define REG_SF_USER_GASBAlphaVal		0x03E8
#define REG_SF_USER_GASBAlphaChanged		0x03EA
#define REG_SF_USER_DbgIdx			0x03EC
#define REG_SF_USER_DbgVal			0x03EE
#define REG_SF_USER_DbgChanged			0x03F0
#define REG_SF_USER_aGain			0x03F2
#define REG_SF_USER_aGainChanged		0x03F4
#define REG_SF_USER_dGain			0x03F6
#define REG_SF_USER_dGainChanged		0x03F8

/* Output interface control section */
#define REG_TC_OIF_EnMipiLanes			0x03FA
#define REG_TC_OIF_EnPackets			0x03FC
#define REG_TC_OIF_CfgChanged			0x03FE

/* Debug control section */
#define REG_TC_DBG_AutoAlgEnBits		0x0400
#define REG_TC_DBG_IspBypass			0x0402
#define REG_TC_DBG_ReInitCmd			0x0404

/* Version information section */
#define REG_FWdate				0x012C
#define REG_FWapiVer				0x012E
#define REG_FWrevision				0x0130
#define REG_FWpid				0x0132
#define REG_FWprjName				0x0134
#define REG_FWcompDate				0x0140
#define REG_FWSFC_VER				0x014C
#define REG_FWTC_VER				0x014E
#define REG_FWrealImageLine			0x0150
#define REG_FWsenId				0x0152
#define REG_FWusDevIdQaVersion			0x0154
#define REG_FWusFwCompilationBits		0x0156
#define REG_ulSVNrevision			0x0158
#define REG_SVNpathRomAddress			0x015C
#define REG_TRAP_N_PATCH_START_ADD		0x1B00

#define	setot_usForceClocksSettings		0x0AEA
#define	setot_usConfigClocksSettings		0x0AEC

#define REG_0TC_CCFG_uCaptureMode  0x030C
#define REG_0TC_CCFG_usWidth       0x030E
#define REG_0TC_CCFG_usHeight      0x0310
#define REG_0TC_CCFG_Format        0x0312
#define REG_0TC_CCFG_usMaxOut4KHzRate 0x0314
#define REG_0TC_CCFG_usMinOut4KHzRate 0x0316
#define REG_0TC_CCFG_PVIMask    0x0318
#define REG_0TC_CCFG_uClockInd  0x031A
#define REG_0TC_CCFG_usFrTimeType  0x031C
#define REG_0TC_CCFG_FrRateQualityType 0x031E
#define REG_0TC_CCFG_usMaxFrTimeMsecMult10 0x0320
#define REG_0TC_CCFG_usMinFrTimeMsecMult10 0x0322
#define lt_uMaxAnGain2                0x049A
#define REG_TC_GP_ActivePrevConfig    0x021C
#define REG_TC_GP_PrevOpenAfterChange 0x0220
#define REG_TC_GP_NewConfigSync       0x01F8
#define REG_TC_GP_PrevConfigChanged   0x021E
#define REG_TC_GP_ActiveCapConfig   0x0224
#define REG_TC_GP_CapConfigChanged  0x0226
#define REG_TC_GP_EnableCapture      0x01F4
#define REG_TC_GP_EnableCaptureChanged  0x01F6

#define	lt_uMaxExp1									0x0488	// 0x9C40
#define	lt_uMaxExp2									0x048C	// 0xE848
#define	lt_uCapMaxExp1								0x0490	// 0x9C40
#define	lt_uCapMaxExp2								0x0494	// 0xE848
#define	lt_uMaxDigGain								0x049C	// 0x0200
#define	lt_uMaxAnGain1								0x0498	// 0x0200
#define	lt_uMaxAnGain2								0x049A	// 0x0500


#define	REG_1TC_CCFG_uCaptureMode					0x032E	// 0x0000
#define	REG_1TC_CCFG_Cfg							0x0330	// 0x0500
#define	REG_1TC_CCFG_usWidth						0x0330	// 0x0500
#define	REG_1TC_CCFG_usHeight						0x0332	// 0x03C0
#define	REG_1TC_CCFG_Format							0x0334	// 0x0009
#define	REG_1TC_CCFG_usMaxOut4KHzRate				0x0336	// 0x1770
#define	REG_1TC_CCFG_usMinOut4KHzRate				0x0338	// 0x05DC
#define	REG_1TC_CCFG_PVIMask						0x033A	// 0x0042
#define	REG_1TC_CCFG_uClockInd						0x033C	// 0x0000
#define	REG_1TC_CCFG_usFrTimeType					0x033E	// 0x0000
#define	REG_1TC_CCFG_FrRateQualityType				0x0340	// 0x0002
#define	REG_1TC_CCFG_usMaxFrTimeMsecMult10			0x0342	// 0x1964
#define	REG_1TC_CCFG_usMinFrTimeMsecMult10			0x0344	// 0x0000
#define	REG_1TC_CCFG_sSaturation					0x0346	// 0x0000
#define	REG_1TC_CCFG_sSharpBlur						0x0348	// 0x0000
#define	REG_1TC_CCFG_sGlamour						0x034A	// 0x0000
#define	REG_1TC_CCFG_sColorTemp						0x034C	// 0x0000
#define	REG_1TC_CCFG_uDeviceGammaIndex				0x034E	// 0x0000
#define	REG_CapConfigControls_2_					0x0350	// 0x0000


#define REG_1TC_PCFG_usWidth    0x0268
#define REG_1TC_PCFG_usHeight   0x026A
#define REG_1TC_PCFG_Format     0x026C
#define REG_1TC_PCFG_usMaxOut4KHzRate 0x026E
#define REG_1TC_PCFG_usMinOut4KHzRate 0x0270
#define REG_1TC_PCFG_PVIMask    0x0272
#define REG_1TC_PCFG_uClockInd  0x0274
#define REG_1TC_PCFG_usFrTimeType  0x0276
#define REG_1TC_PCFG_FrRateQualityType 0x0278
#define REG_1TC_PCFG_usMaxFrTimeMsecMult10 0x027A
#define REG_1TC_PCFG_usMinFrTimeMsecMult10 0x027C

#define AFC_Default60Hz    0x0B2A
#define REG_TC_DBG_AutoAlgEnBits  0x0400
#define REG_SF_USER_FlickerQuant  0x03DC
#define REG_SF_USER_FlickerQuantChanged  0x03DE


#define REG_2TC_PCFG_usWidth    0x028E
#define REG_2TC_PCFG_usHeight   0x0290
#define REG_2TC_PCFG_Format     0x0292
#define REG_2TC_PCFG_usMaxOut4KHzRate 0x0294
#define REG_2TC_PCFG_usMinOut4KHzRate 0x0296
#define REG_2TC_PCFG_PVIMask    0x0298
#define REG_2TC_PCFG_uClockInd  0x029A
#define REG_2TC_PCFG_usFrTimeType  0x029C
#define REG_2TC_PCFG_FrRateQualityType 0x029E
#define REG_2TC_PCFG_usMaxFrTimeMsecMult10 0x02A0
#define REG_2TC_PCFG_usMinFrTimeMsecMult10 0x02A2

#define REG_3TC_PCFG_usWidth    0x02B4
#define REG_3TC_PCFG_usHeight   0x02B6
#define REG_3TC_PCFG_Format     0x02B8
#define REG_3TC_PCFG_usMaxOut4KHzRate 0x02BA
#define REG_3TC_PCFG_usMinOut4KHzRate 0x02BC
#define REG_3TC_PCFG_PVIMask    0x02BE
#define REG_3TC_PCFG_uClockInd  0x02C0
#define REG_3TC_PCFG_usFrTimeType  0x02C2
#define REG_3TC_PCFG_FrRateQualityType 0x02C4
#define REG_3TC_PCFG_usMaxFrTimeMsecMult10 0x02C6
#define REG_3TC_PCFG_usMinFrTimeMsecMult10 0x02C8

#define SEQUENCE_INIT        0x00
#define SEQUENCE_NORMAL      0x01
#define SEQUENCE_CAPTURE     0x02
#define SEQUENCE_PREVIEW     0x03

#define SEQUENCE_PROPERTY    0xFFF9
#define SEQUENCE_WAIT_MS     0xFFFA
#define SEQUENCE_WAIT_US     0xFFFB
#define SEQUENCE_END                    (0xFFFF)
#define SEQUENCE_FAST_SETMODE_START     (0xFFFD)
#define SEQUENCE_FAST_SETMODE_END       (0xFFFC)


/*configure register for flipe and mirror during initial*/
#define CONFIG_SENSOR_FLIPE     0
#define CONFIG_SENSOR_MIRROR    1
#define CONFIG_SENSOR_MIRROR_AND_FLIPE  0
#define CONFIG_SENSOR_NONE_FLIP_MIRROR  0
/**configure to indicate android cts****/
#define CONFIG_SENSOR_FOR_CTS     1
#endif