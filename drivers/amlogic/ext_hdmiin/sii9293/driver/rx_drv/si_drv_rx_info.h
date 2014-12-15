//------------------------------------------------------------------------------
// Project: HDMI Repeater
// Copyright (C) 2002-2013, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1140 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------

#ifndef __SI_DRV_RX_INFO_H__
#define __SI_DRV_RX_INFO_H__

#include "si_common.h"

// muting during HDCP authentication
//#define RX_OUT_AV_MUTE_M__HDCP_RX				BIT0

// Propagation of incoming AV Mute packet from upstream to downstream
#define RX_OUT_AV_MUTE_M__INP_AV_MUTE_CAME		BIT1

// mute due to RX chip is not ready
#define RX_OUT_AV_MUTE_M__RX_IS_NOT_READY		BIT2

// mute because of no AVI packet coming and therefore input color space is unknown
#define RX_OUT_AV_MUTE_M__NO_AVI				BIT4

// mute due to an HDCP error
#define RX_OUT_AV_MUTE_M__RX_HDCP_ERROR			BIT7

enum
{
    INFO_AVI = 0x00,
    INFO_SPD,
    INFO_AUD,
    INFO_MPEG,
    INFO_UNREC,
    INFO_ACP,
    INFO_VSI, 
};



void RxInfo_InterruptHandler(uint8_t info_type);

#endif // __SI_DRV_RX_INFO_H__

