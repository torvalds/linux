//===-- HostInfoBase.h ------------------------------------------*- C++ -*-===//
//
//                     The LLVM Compiler Infrastructure
//
// This file is distributed under the University of Illinois Open Source
// License. See LICENSE.TXT for details.
//
//===----------------------------------------------------------------------===//

#ifndef lldb_Host_HostInfo_h_
#define lldb_Host_HostInfo_h_

//----------------------------------------------------------------------
/// @class HostInfo HostInfo.h "lldb/Host/HostInfo.h"
/// A class that provides host computer information.
///
/// HostInfo is a class that answers information about the host operating
/// system.  Note that HostInfo is NOT intended to be used to manipulate or
/// control the operating system.
///
/// HostInfo is implemented in an OS-specific class (for example
/// HostInfoWindows) in a separate file, and then typedefed to HostInfo here.
/// Users of the class reference it as HostInfo::method().
///
/// Not all hosts provide the same functionality.  It is important that
/// methods only be implemented at the lowest level at which they make sense.
/// It should be up to the clients of the class to ensure that they not
/// attempt to call a method which doesn't make sense for a particular
/// platform.  For example, when implementing a method that only makes sense
/// on a posix-compliant system, implement it on HostInfoPosix, and not on
/// HostInfoBase with a default implementation.  This way, users of HostInfo
/// are required to think about the implications of calling a particular
/// method and if used in a context where the method doesn't make sense, will
/// generate a compiler error.
///
//----------------------------------------------------------------------

#if defined(_WIN32)
#include "lldb/Host/windows/HostInfoWindows.h"
#define HOST_INFO_TYPE HostInfoWindows
#elif defined(__linux__)
#if defined(__ANDROID__)
#include "lldb/Host/android/HostInfoAndroid.h"
#define HOST_INFO_TYPE HostInfoAndroid
#else
#include "lldb/Host/linux/HostInfoLinux.h"
#define HOST_INFO_TYPE HostInfoLinux
#endif
#elif defined(__FreeBSD__) || defined(__FreeBSD_kernel__)
#include "lldb/Host/freebsd/HostInfoFreeBSD.h"
#define HOST_INFO_TYPE HostInfoFreeBSD
#elif defined(__NetBSD__)
#include "lldb/Host/netbsd/HostInfoNetBSD.h"
#define HOST_INFO_TYPE HostInfoNetBSD
#elif defined(__OpenBSD__)
#include "lldb/Host/openbsd/HostInfoOpenBSD.h"
#define HOST_INFO_TYPE HostInfoOpenBSD
#elif defined(__APPLE__)
#include "lldb/Host/macosx/HostInfoMacOSX.h"
#define HOST_INFO_TYPE HostInfoMacOSX
#else
#include "lldb/Host/posix/HostInfoPosix.h"
#define HOST_INFO_TYPE HostInfoPosix
#endif

namespace lldb_private {
typedef HOST_INFO_TYPE HostInfo;
}

#undef HOST_INFO_TYPE

#endif
