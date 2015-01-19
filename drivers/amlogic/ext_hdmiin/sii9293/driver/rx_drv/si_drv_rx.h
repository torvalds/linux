//------------------------------------------------------------------------------
// Project: HDMI Repeater
// Copyright (C) 2002-2013, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1140 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------


#ifndef SII_DRV_RX_H
#define SII_DRV_RX_H

#include "si_rx_video_mode_detection.h"
#include "si_rx_audio.h"

typedef struct
{
    uint8_t     videoIndex;         //!< see the table
    uint16_t    pixelFreq;          //!< Pixel frequency in 10kHz units
//    uint8_t     vertFreq;          //!< Vertical frame rate in Hz
    uint16_t    clocksPerLine;      //!< Number of clocks per line
    uint16_t    totalLines;         //!< Total number of lines per frame
    bit_fld_t      interlaced : 1;  //!< true for interlaced, false for progressive
    bit_fld_t      hPol       : 1;  //!< true for negative H Pulse, false for positive
    bit_fld_t      vPol       : 1;  //!< true for negative V Pulse, false for positive
    uint8_t     hdmi3dStructure;    //!< HDMI 3D_Structure according to HDMI spec.
    uint8_t     extra3dData;        //!< HDMI 3D_Detail according to HDMI spec.
}
SiiRxTiming_t;

typedef enum
{
	PATH__RGB,					// RGB, single edge clock
	PATH__YCbCr444,				// YCbCr 4:4:4, single edge clock 
	PATH__YCbCr422_16B,			// YCbCr 4:2:2, single edge clock, Y is separate, Cb and Cr multiplexed, 16 bit bus
	PATH__YCbCr422_20B,			// YCbCr 4:2:2, single edge clock, Y is separate, Cb and Cr multiplexed, 20 bit bus
	PATH__YCbCr422_16B_SYNC,	// YCbCr 4:2:2, single edge clock, Y is separate, Cb and Cr multiplexed, 16 bit bus, embedded sync output
	PATH__YCbCr422_20B_SYNC,	// YCbCr 4:2:2, single edge clock, Y is separate, Cb and Cr multiplexed, 20 bit bus, embedded sync output
	PATH__YCbCr422_MUX8B,		// YCbCr 4:2:2, single edge at 2x clock, Y multiplexed with Cb and Cr, 8 bit bus
	PATH__YCbCr422_MUX10B,		// YCbCr 4:2:2, single edge at 2x clock, Y multiplexed with Cb and Cr, 10 bit bus 
	PATH__YCbCr422_MUX8B_SYNC,	// YCbCr 4:2:2, single edge at 2x clock, Y multiplexed with Cb and Cr, 8 bit bus, embedded sync output
	PATH__YCbCr422_MUX10B_SYNC,	// YCbCr 4:2:2, single edge at 2x clock, Y multiplexed with Cb and Cr, 10 bit bus , embedded sync output
	PATH__MAXVALUE,
} VideoPath_t;

VideoPath_t SiiDrvRxGetOutVideoPath(void);

void SiiDrvRxTimingInfoSet(SiiRxTiming_t *pNewRxTimingInfo);
 
SiiRxTiming_t *SiiDrvRxTimingInfoGet(void);

uint8_t SiiDrvRxGetPixelReplicate(void);

uint8_t SiiDrvRxGetVideoStatus(void);
	
uint16_t SiiDrvRxGetPixelFreq(void);

void SiiDrvRxGetSyncInfo(sync_info_type *p_sync_info);

bool_t SiiDrvRxIsSyncDetected(void);

bool_t SiiDrvRxHdmiModeGet(void);

void SiiDrvRxMuteVideo(uint8_t switch_on);

void SiiDrvSoftwareReset(uint8_t reset_mask);

bool_t SiiDrvRxInitialize(void);

void SiiDrvRxVideoPathSet(void);
#endif // SII_DRV_RX_H

