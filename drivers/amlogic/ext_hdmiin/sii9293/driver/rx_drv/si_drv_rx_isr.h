//------------------------------------------------------------------------------
// Project: HDMI Repeater
// Copyright (C) 2002-2013, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1140 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------

#ifndef SI_DRV_RX_ISR_H
#define SI_DRV_RX_ISR_H


void RxIsr_Init(void);

void SiiRxInterruptHandler(void);

bool_t SiiDrvCableStatusGet ( bool_t *pData );

bool_t SiiDrvVidStableGet ( bool_t *pData );

#endif // SI_DRV_RX_ISR_H
