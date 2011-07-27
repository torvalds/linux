// ------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Corporation.  All rights reserved.
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
// ------------------------------------------------------------------
//===================================================================
// Author(s): ="Atheros"
//===================================================================


#ifndef _UART_REG_REG_H_
#define _UART_REG_REG_H_

#define UART_CLKDIV_ADDRESS                      0x00000008
#define UART_CLKDIV_OFFSET                       0x00000008
#define UART_CLKDIV_CLK_SCALE_MSB                23
#define UART_CLKDIV_CLK_SCALE_LSB                16
#define UART_CLKDIV_CLK_SCALE_MASK               0x00ff0000
#define UART_CLKDIV_CLK_SCALE_GET(x)             (((x) & UART_CLKDIV_CLK_SCALE_MASK) >> UART_CLKDIV_CLK_SCALE_LSB)
#define UART_CLKDIV_CLK_SCALE_SET(x)             (((x) << UART_CLKDIV_CLK_SCALE_LSB) & UART_CLKDIV_CLK_SCALE_MASK)
#define UART_CLKDIV_CLK_STEP_MSB                 15
#define UART_CLKDIV_CLK_STEP_LSB                 0
#define UART_CLKDIV_CLK_STEP_MASK                0x0000ffff
#define UART_CLKDIV_CLK_STEP_GET(x)              (((x) & UART_CLKDIV_CLK_STEP_MASK) >> UART_CLKDIV_CLK_STEP_LSB)
#define UART_CLKDIV_CLK_STEP_SET(x)              (((x) << UART_CLKDIV_CLK_STEP_LSB) & UART_CLKDIV_CLK_STEP_MASK)

#endif /* _UART_REG_H_ */
