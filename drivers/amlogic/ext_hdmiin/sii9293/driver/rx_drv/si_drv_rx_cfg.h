/******************************************************************************
*
* Copyright (C) 2002-2013, Silicon Image, Inc.  All rights reserved.
*
* No part of this work may be reproduced, modified, distributed, transmitted,
* transcribed, or translated into any language or computer format, in any form
* or by any means without written permission of: Silicon Image, Inc.,
* 1140 East Arques Avenue, Sunnyvale, California 94085
*
*****************************************************************************/
/**
* @file si_rx_cfg.h
*
* HDMI Receiver Configuration Manager
*
*****************************************************************************/

#ifndef __SI_DRV_RX_CFG_H__
#define __SI_DRV_RX_CFG_H__

#include "si_common.h"

#define SI_USE_DEBUG_PRINT ENABLE // enable or disable log print (aka DEBUG_PRINT())

#define SI_INVERT_RX_OUT_PIX_CLOCK_BY_DEFAULT DISABLE
//	RX output clock:
//	DISABLE ¨C Normal output clock (setup and hold to rising edge)
//	ENABLE ¨C Invert the output clock (setup and hold to falling edge)

#define SI_ALLOW_PC_MODES ENABLE
// ENABLE- allows PC resolutions to be used.
// DISABLE- prohibits resolutions that are not CEA-861 or 3D.
// If the repeater is designed to use DE (Data Enable) signal
// at RX and TX, then it is safe to enable this option.
// If DE line is not provided then it is still possible to use PC resolutions
// but RP_M__BOARD_OPTIONS1__DIRECT_VIDEO option of RTPI has to be
// disabled and assistance from the host microcontroller is required
// to fill RTPI registers 0x80...0x8F.

#define SI_ALLOW_300_MHZ_RESOLUTIONS DISABLE
// ENABLE- allow 300 MHz resolutions by default configuration.
// DISABLE- prohibit 300MHz resolutions by default settings.
// Note: if configuration EEPROM is used,
// the default frequency limits in EDID may be overwitten.

//range limits to satisfy all 861D video modes
#define VIDEO__MIN_V_HZ			(22)
#define VIDEO__MAX_V_HZ			(243)
#define VIDEO__MIN_H_KHZ			(14)
#if (SI_ALLOW_300_MHZ_RESOLUTIONS == ENABLE)
#  define VIDEO__MAX_H_KHZ			(134)
#  define VIDEO__MAX_PIX_CLK_10MHZ	(30) // to support 300 MHz resolutions
#else // SI_ALLOW_300_MHZ_RESOLUTIONS
#  define VIDEO__MAX_H_KHZ			(129)
#  define VIDEO__MAX_PIX_CLK_10MHZ	(17) // to support more PC modes
#endif // SI_ALLOW_300_MHZ_RESOLUTIONS


#endif // __SI_DRV_RX_CFG_H__
