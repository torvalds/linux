//------------------------------------------------------------------------------
// Project: HDMI Repeater
// Copyright (C) 2002-2013, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1140 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------


#ifndef SII_VIDEOMODEDETECTION_H
#define SII_VIDEOMODEDETECTION_H

#include "si_common.h"
// VMD_GetVsifPacketType() return type
typedef enum
{
	NOT_HDMI_VSIF,			// VSIF packet is not HDMI VSIF
	NEW_EXTENDED_RESOLUTION, // VSIF packet carries Extended Resolution info: first detection
	OLD_EXTENDED_RESOLUTION, // VSIF packet carries Extended Resolution info: no change
	NEW_3D,					// VSIF packet with 3D info: first detection
	OLD_3D					// VSIF packet with 3D info: no change from last time
}
vsif_check_result_t;


typedef	struct
{
	uint16_t	ClocksPerLine; // number of pixel clocks per line
	uint16_t	TotalLines; // number of lines
	uint16_t 	PixelFreq; // pixel frequency in 10kHz units
	bit_fld_t	Interlaced	: 1; // true for interlaced video
	bit_fld_t	HPol		: 1; // true on negative polarity for horizontal pulses
	bit_fld_t	VPol		: 1; // true on negative polarity for vertical pulses
}
sync_info_type;

#define VIDEO_STABLE_TIME 200

void VMD_ResetTimingData(void);

void VMD_ResetInfoFrameData(void);

uint8_t VMD_GetVideoIndex(void);

// returns true if video format is detected, false otherwise
bool_t VMD_DetectVideoResolution(void);

bool_t VMD_WasResolutionChanged(void);

void VMD_OnAviPacketReceiving(uint8_t cea861vic);

void VMD_VsifProcessing(uint8_t *p_packet, uint8_t length);

void VMD_HdmiVsifProcessing(uint8_t *p_packet, uint8_t length);

void VMD_MhlVsifProcessing(uint8_t *p_packet, uint8_t length);

vsif_check_result_t VMD_GetVsifPacketType(uint8_t *p_packet, uint8_t length);

vsif_check_result_t VMD_GetHdmiVsifPacketType(uint8_t *p_packet, uint8_t length);

vsif_check_result_t VMD_GetMhlVsifPacketType(uint8_t *p_packet, uint8_t length);

void VMD_OnHdmiVsifPacketDiscontinuation(void);

void VMD_Init(void);

uint16_t VMD_GetPixFreq10kHz(void);

void SiiRxSetVideoStableTimer(void);

#if !defined(__KERNEL__)
void SiiRxFormatDetect(void);
#endif

void RX_ConfigureGpioAs3dFrameIndicator(void);

#endif //SII_VIDEOMODEDETECTION_H

