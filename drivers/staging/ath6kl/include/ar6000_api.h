//------------------------------------------------------------------------------
// <copyright file="ar6000_api.h" company="Atheros">
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
// This file contains the API to access the OS dependent atheros host driver
// by the WMI or WLAN generic modules.
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef _AR6000_API_H_
#define _AR6000_API_H_

#if defined(__linux__) && !defined(LINUX_EMULATION)
#include "../os/linux/include/ar6xapi_linux.h"
#endif

#ifdef UNDER_NWIFI
#include "../os/windows/include/ar6xapi.h"
#endif

#ifdef ATHR_CE_LEGACY
#include "../os/windows/include/ar6xapi.h"
#endif

#ifdef REXOS
#include "../os/rexos/include/common/ar6xapi_rexos.h"
#endif

#if defined ART_WIN
#include "../os/win_art/include/ar6xapi_win.h"
#endif

#ifdef WIN_NWF
#include "../os/windows/include/ar6xapi.h"
#endif

#endif /* _AR6000_API_H */

