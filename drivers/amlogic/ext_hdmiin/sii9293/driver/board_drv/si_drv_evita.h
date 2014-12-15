//------------------------------------------------------------------------------
// Project: 5293
// Copyright (C) 2002-2013, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1140 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------


#ifndef SII_DRV_EVITA_H
#define SII_DRV_EVITA_H

#include "si_common.h"

bool_t SiiDrvEvitaInit(void);

void SiiDrvEvitaAviIfUpdate(void);

void SiiDrvEvitaAudioIfUpdate(uint8_t *pPacket);

#endif // SII_DRV_EVITA_H
