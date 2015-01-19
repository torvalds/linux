//------------------------------------------------------------------------------
// Project: HDMI Repeater
// Copyright (C) 2002-2013, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1140 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------

#ifndef SIIRXAUDIO_H
#define SIIRXAUDIO_H

#include "si_rx_info.h"

typedef enum
{
	RX_CFG_128FS = 0,
	RX_CFG_256FS = 1,
	RX_CFG_384FS = 2,
	RX_CFG_512FS = 3,
}
rxCfgAudioMclk_t;

#define SI_AUDIO_ST_CH_LEN 5
typedef struct
{
	bit_fld_t         audioLayout  : 1;   //!< incoming HDMI audio layout (0 or 1)
	bit_fld_t         audioEncoded : 1;   //!< true for encoded audio, false for PCM and DSD
	uint8_t        audioChannelAllocation;//!< audio Channel Allocation (See CEA-861D)
	uint8_t        audioStatusChannel[SI_AUDIO_ST_CH_LEN]; //!< first 5 bytes of Audio Status Channel
}
SiiRxAudioFormat_t;

void RxAudio_OnAudioInfoFrame(uint8_t *p_data, uint8_t length);
void RxAudio_OnChannelStatusChange(void);
void RxAudio_Init(void);
void RxAudio_Stop(void);
void RxAudio_ReStart(void);
 void RxAudio_OnAcpPacketUpdate(acp_type_type acp_type);

#endif // SIIRXAUDIO_H

