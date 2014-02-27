//------------------------------------------------------------------------------
// Copyright (c) 2005-2010 Atheros Corporation.  All rights reserved.
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

#define AR6001_GPIO_PIN_COUNT 18
#define AR6002_GPIO_PIN_COUNT 18
#define AR6003_GPIO_PIN_COUNT 28
#define MCKINLEY_GPIO_PIN_COUNT 57

/*
 * Values of gpioreg_id in the WMIX_GPIO_REGISTER_SET_CMDID and WMIX_GPIO_REGISTER_GET_CMDID
 * commands come in two flavors.  If the upper bit of gpioreg_id is CLEAR, then the
 * remainder is interpreted as one of these values.  This provides platform-independent
 * access to GPIO registers.  If the upper bit (GPIO_ID_OFFSET_FLAG) of gpioreg_id is SET,
 * then the remainder is interpreted as a platform-specific GPIO register offset.
 */
#define GPIO_ID_OUT             0x00000000
#define GPIO_ID_OUT_W1TS        0x00000001
#define GPIO_ID_OUT_W1TC        0x00000002
#define GPIO_ID_ENABLE          0x00000003
#define GPIO_ID_ENABLE_W1TS     0x00000004
#define GPIO_ID_ENABLE_W1TC     0x00000005
#define GPIO_ID_IN              0x00000006
#define GPIO_ID_STATUS          0x00000007
#define GPIO_ID_STATUS_W1TS     0x00000008
#define GPIO_ID_STATUS_W1TC     0x00000009
#define GPIO_ID_PIN0            0x0000000a
#define GPIO_ID_PIN(n)          (GPIO_ID_PIN0+(n))
#define GPIO_ID_NONE            0xffffffff

#define GPIO_ID_OFFSET_FLAG     0x80000000
#define GPIO_ID_REG_MASK        0x7fffffff
#define GPIO_ID_IS_OFFSET(reg_id) (((reg_id) & GPIO_ID_OFFSET_FLAG) != 0)
