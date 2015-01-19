//------------------------------------------------------------------------------
// Copyright © 2002-2005, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1060 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------
#ifndef UEDIDTBL
#define UEDIDTBL

#include <local_types.h>

#define CEC_PA_EDID_OFFSET   0xB8   // for different EDID this value can be different



extern ROM const uint8_t abEDIDTabl[256];
extern ROM const uint8_t abEXTRATabl[32];

void ProgramEDID(void);


#endif

