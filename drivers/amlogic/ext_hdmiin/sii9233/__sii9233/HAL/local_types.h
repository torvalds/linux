//------------------------------------------------------------------------------
// Copyright © 2007, Silicon Image, Inc.  All rights reserved.
//
// No part of this work may be reproduced, modified, distributed, transmitted,
// transcribed, or translated into any language or computer format, in any form
// or by any means without written permission of: Silicon Image, Inc.,
// 1060 East Arques Avenue, Sunnyvale, California 94085
//------------------------------------------------------------------------------
#ifndef __LOCAL_TYPES_H__
#define __LOCAL_TYPES_H__

#include <linux/kernel.h>


//new alternetive unsigned type names
//typedef unsigned char  uint8_t;
//typedef unsigned short uint16_t;
//typedef unsigned long  uint32_t;
typedef unsigned char  bool_t;	/* this type can be used in structures */

#define TRUE      1
#define FALSE     0

#define ROM
#define IRAM	
//#define ROM   code       // 8051 type of ROM memory
//#define IRAM  idata      // 8051 type of RAM memory


#endif  // __LOCAL_TYPES_H__
