/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Hypervisor Support
 *
 * Copyright (C) 2010-2012 VMware, Inc. All rights reserved.
 *
 * This program is free software; you can redistribute it and/or modify it
 * under the terms of the GNU General Public License version 2 as published by
 * the Free Software Foundation.
 *
 * This program is distributed in the hope that it will be useful, but WITHOUT
 * ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or
 * FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public License for
 * more details.
 *
 * You should have received a copy of the GNU General Public License along with
 * this program; see the file COPYING.  If not, write to the Free Software
 * Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA 02111-1307, USA.
 */
#line 5

/**
 * @file
 *
 * @brief What version is this?
 *
 */

#ifndef _MVP_VERSION_H_
#define _MVP_VERSION_H_

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_GPL
#define INCLUDE_ALLOW_HOSTUSER
#include "include_check.h"
#include "utils.h"

/*
 * MVP Internal Version Numbering
 *
 *
 * There are 4 different usage areas of version information.
 *
 *   Version Name. This is a marketing string that is used to sell the
 *   product. The update of this string has legal consequences, it
 *   should be done infrequently. Currently we use "V1.0" like
 *   terms. Developer builds have E.X.P as Version Name.
 *
 *   Android Version Code. This is an integer associated with
 *   com.vmware.mvp.apk on Google Play (a.k.a Android Market). If our
 *   product is multi-apk (that is, we release individual apks for the
 *   different Android versions) then the Android Version Code must
 *   satisfy certain constrains. Typically the Android API level is
 *   the high order 2 digits.
 *
 *   Engineering Version Code. During an update process of one of the
 *   3 components on the handset (MVP, VVP, OEK) compatibility needs
 *   to be verified. The Engineering Version Code is a single number
 *   associated with each of the 4 components and it serves as a basis
 *   of this compatibility test. It reflects time, bigger number is
 *   associated with newer code.
 *
 *   Git Revision. The git hash is a unique identifier of the
 *   source. If picked up from a log, engineers can go to the code
 *   depos and check out the exact code used for the build.  For MVP,
 *   VVP, and OEK this is the main/mvp.git, for HMM it is
 *   main/mdm.git. Note that git hash is not ordered, it cannot be
 *   used to directly determine precedence.
 *
 */

#define MVP_VERSION_CODE 16800005
#define MVP_VERSION_CODE_FORMATSTR       "%s_%d"
#define MVP_VERSION_CODE_FORMATARGSV(V_) MVP_STRINGIFY(1.1.3), (V_)
#define MVP_VERSION_CODE_FORMATARGS             \
   MVP_VERSION_CODE_FORMATARGSV(MVP_VERSION_CODE)

#define MVP_VERSION_FORMATSTR                        \
   MVP_VERSION_CODE_FORMATSTR                        \
   " compiled at %s based on revision %s by user %s."

#define MVP_VERSION_FORMATARGS      \
   MVP_VERSION_CODE_FORMATARGS,     \
   __DATE__,                        \
   MVP_STRINGIFY(5c995a85564cd060562bdbcd1422709e7a326301),     \
   MVP_STRINGIFY()

#define MvpVersion_Map(map_, version_)           \
   ({                                            \
      uint32 ii_;                                \
      uint32 versionApi_ = 0;                    \
      for (ii_ = 0; ii_ < NELEM(map_); ii_++) {  \
         if (map_[ii_] <= version_) {            \
            versionApi_ = map_[ii_];             \
         }                                       \
      }                                          \
      versionApi_;                               \
   })

/*
 * MVP.apk must communicate to VVP and OEK on many of its APIs. To
 * ensure compatibility, it is mandated that any VVP and OEK version
 * younger than the minimums defined below can be serviced on all of
 * the various APIs.
 *
 * During the deprecation process, first a marketing decision is made
 * that the limit below can be raised. After the new minimums are
 * determined, they must be entered here. Then the various APIs can
 * remove code that has been obsoleted before the new minimum versions.
 */
#define VVP_VERSION_CODE_MIN 0x0100020e
#define OEK_VERSION_CODE_MIN 0x01000001

#endif /* _MVP_VERSION_H_ */
