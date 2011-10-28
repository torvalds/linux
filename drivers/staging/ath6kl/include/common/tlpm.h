//------------------------------------------------------------------------------
// Copyright (c) 2010 Atheros Corporation.  All rights reserved.
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

#ifndef __TLPM_H__
#define __TLPM_H__

/* idle timeout in 16-bit value as in HOST_INTEREST hi_hci_uart_pwr_mgmt_params */
#define TLPM_DEFAULT_IDLE_TIMEOUT_MS         1000
/* hex in LSB and MSB for HCI command */
#define TLPM_DEFAULT_IDLE_TIMEOUT_LSB        0xE8
#define TLPM_DEFAULT_IDLE_TIMEOUT_MSB        0x3

/* wakeup timeout in 8-bit value as in HOST_INTEREST hi_hci_uart_pwr_mgmt_params */
#define TLPM_DEFAULT_WAKEUP_TIMEOUT_MS       10

/* default UART FC polarity is low */
#define TLPM_DEFAULT_UART_FC_POLARITY        0

#endif
