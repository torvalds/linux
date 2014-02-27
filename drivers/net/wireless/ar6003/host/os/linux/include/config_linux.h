//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
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
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#ifndef _CONFIG_LINUX_H_
#define _CONFIG_LINUX_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <linux/version.h>

/*
 * Host-side GPIO support is optional.
 * If run-time access to GPIO pins is not required, then
 * this should be changed to #undef.
 */
#define CONFIG_HOST_GPIO_SUPPORT

/*
 * Host side Test Command support
 * Note: when HCI SDIO is enabled, a low stack IRQ or statck overflow is
 *       hit on FC10. So with HCI SDIO, minimize the stack allocation by 
 *       mutually exclude TCMD_SUPPORT, which allocates large buffers 
 *       in AR_TCMD_RESP in AR_SOFTC_T
 *
 */
#ifndef HCI_TRANSPORT_SDIO
//#define CONFIG_HOST_TCMD_SUPPORT
#endif

/* Host-side support for Target-side profiling */
#undef CONFIG_TARGET_PROFILE_SUPPORT
/*DIX OFFLOAD SUPPORT*/
/*#define DIX_RX_OFFLOAD*/
/*#define DIX_TX_OFFLOAD*/

/* IP/TCP checksum offload */
/* Checksum offload is currently not supported for 64 bit platforms */
#ifndef __LP64__
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,25)
#define CONFIG_CHECKSUM_OFFLOAD
#endif
#endif /* __LP64__ */

#ifdef __cplusplus
}
#endif

#endif
