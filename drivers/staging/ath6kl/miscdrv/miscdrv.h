//------------------------------------------------------------------------------
// <copyright file="miscdrv.h" company="Atheros">
//    Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//------------------------------------------------------------------------------
//==============================================================================
// Author(s): ="Atheros"
//==============================================================================
#ifndef _MISCDRV_H
#define _MISCDRV_H


#define HOST_INTEREST_ITEM_ADDRESS(target, item)    \
   AR6002_HOST_INTEREST_ITEM_ADDRESS(item)

u32 ar6kRev2Array[][128]   = {
                                    {0xFFFF, 0xFFFF},      // No Patches
                               };

#define CFG_REV2_ITEMS                0     // no patches so far
#define AR6K_RESET_ADDR               0x4000
#define AR6K_RESET_VAL                0x100

#define EEPROM_SZ                     768
#define EEPROM_WAIT_LIMIT             4

#endif

