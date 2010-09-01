//------------------------------------------------------------------------------
// <copyright file="a_drv.h" company="Atheros">
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
// This file contains the definitions of the basic atheros data types.
// It is used to map the data types in atheros files to a platform specific
// type.
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef _A_DRV_H_
#define _A_DRV_H_

#if defined(__linux__) && !defined(LINUX_EMULATION)
#include "../os/linux/include/athdrv_linux.h"
#endif

#ifdef UNDER_NWIFI
#include "../os/windows/include/athdrv.h"
#endif

#ifdef ATHR_CE_LEGACY
#include "../os/windows/include/athdrv.h"
#endif

#ifdef REXOS
#include "../os/rexos/include/common/athdrv_rexos.h"
#endif

#ifdef WIN_NWF
#include "../os/windows/include/athdrv.h"
#endif

#ifdef THREADX
#include "../os/threadx/include/common/athdrv_threadx.h"
#endif

#endif /* _ADRV_H_ */
