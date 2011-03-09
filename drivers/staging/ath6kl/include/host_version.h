//------------------------------------------------------------------------------
// <copyright file="host_version.h" company="Atheros">
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
// This file contains version information for the sample host driver for the
// AR6000 chip
//
// Author(s): ="Atheros"
//==============================================================================
#ifndef _HOST_VERSION_H_
#define _HOST_VERSION_H_

#ifdef __cplusplus
extern "C" {
#endif

#include <AR6002/AR6K_version.h>

/*
 * The version number is made up of major, minor, patch and build
 * numbers. These are 16 bit numbers.  The build and release script will
 * set the build number using a Perforce counter.  Here the build number is
 * set to 9999 so that builds done without the build-release script are easily
 * identifiable.
 */

#define ATH_SW_VER_MAJOR      __VER_MAJOR_
#define ATH_SW_VER_MINOR      __VER_MINOR_
#define ATH_SW_VER_PATCH      __VER_PATCH_
#define ATH_SW_VER_BUILD      __BUILD_NUMBER_ 

#ifdef __cplusplus
}
#endif

#endif /* _HOST_VERSION_H_ */
