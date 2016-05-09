/****************************************************************************
*
*    The MIT License (MIT)
*
*    Copyright (c) 2014 - 2016 Vivante Corporation
*
*    Permission is hereby granted, free of charge, to any person obtaining a
*    copy of this software and associated documentation files (the "Software"),
*    to deal in the Software without restriction, including without limitation
*    the rights to use, copy, modify, merge, publish, distribute, sublicense,
*    and/or sell copies of the Software, and to permit persons to whom the
*    Software is furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in
*    all copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
*    FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER
*    DEALINGS IN THE SOFTWARE.
*
*****************************************************************************
*
*    The GPL License (GPL)
*
*    Copyright (C) 2014 - 2016 Vivante Corporation
*
*    This program is free software; you can redistribute it and/or
*    modify it under the terms of the GNU General Public License
*    as published by the Free Software Foundation; either version 2
*    of the License, or (at your option) any later version.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU General Public License for more details.
*
*    You should have received a copy of the GNU General Public License
*    along with this program; if not, write to the Free Software Foundation,
*    Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
*
*****************************************************************************
*
*    Note: This software is released under dual MIT and GPL licenses. A
*    recipient may use this file under the terms of either the MIT license or
*    GPL License. If you wish to use only one license not the other, you can
*    indicate your decision by deleting one of the above license notices in your
*    version of this file.
*
*****************************************************************************/


#ifndef __gc_hal_kernel_debug_h_
#define __gc_hal_kernel_debug_h_

#include <gc_hal_kernel_linux.h>
#include <linux/spinlock.h>
#include <linux/time.h>
#include <stdarg.h>

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
****************************** OS-dependent Macros *****************************
\******************************************************************************/

typedef va_list gctARGUMENTS;

#define gcmkARGUMENTS_START(Arguments, Pointer) \
    va_start(Arguments, Pointer)

#define gcmkARGUMENTS_END(Arguments) \
    va_end(Arguments)

#define gcmkARGUMENTS_ARG(Arguments, Type) \
    va_arg(Arguments, Type)

#if gcdDUMP_COMMAND || gcdDUMP_IN_KERNEL
/* VIV: gckOS_DumpBuffer holds spinlock and return to userspace which causes
        schedule in atomic. Because there isn't dump information from interrupt,
        mutex can be used in this situation. Need to solve this by avoid
        return userspace while lock still being hold which is very dangerous.
*/
#define gcmkDECLARE_LOCK(__mutex__) \
    static DEFINE_MUTEX(__mutex__); \

#define gcmkLOCKSECTION(__mutex__) \
    mutex_lock(&__mutex__);

#define gcmkUNLOCKSECTION(__mutex__) \
    mutex_unlock(&__mutex__);
#else
#define gcmkDECLARE_LOCK(__spinLock__) \
    static DEFINE_SPINLOCK(__spinLock__); \
    unsigned long __spinLock__##flags = 0;

#define gcmkLOCKSECTION(__spinLock__) \
    spin_lock_irqsave(&__spinLock__, __spinLock__##flags)

#define gcmkUNLOCKSECTION(__spinLock__) \
    spin_unlock_irqrestore(&__spinLock__, __spinLock__##flags)
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
#   define gcmkGETPROCESSID() \
        task_tgid_vnr(current)
#else
#   define gcmkGETPROCESSID() \
        current->tgid
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,24)
#   define gcmkGETTHREADID() \
        task_pid_vnr(current)
#else
#   define gcmkGETTHREADID() \
        current->pid
#endif

#define gcmkOUTPUT_STRING(String) \
    if(gckDEBUGFS_IsEnabled()) {\
        while(-ERESTARTSYS == gckDEBUGFS_Print(String));\
    }else{\
        printk(String); \
    }\
    touch_softlockup_watchdog()


#define gcmkSPRINTF(Destination, Size, Message, Value) \
    snprintf(Destination, Size, Message, Value)

#define gcmkSPRINTF2(Destination, Size, Message, Value1, Value2) \
    snprintf(Destination, Size, Message, Value1, Value2)

#define gcmkSPRINTF3(Destination, Size, Message, Value1, Value2, Value3) \
    snprintf(Destination, Size, Message, Value1, Value2, Value3)

#define gcmkVSPRINTF(Destination, Size, Message, Arguments) \
    vsnprintf(Destination, Size, Message, *((va_list*)Arguments))

#define gcmkSTRCAT(Destination, Size, String) \
    strncat(Destination, String, Size)

#define gcmkMEMCPY(Destination, Source, Size) \
    memcpy(Destination, Source, Size)

#define gcmkSTRLEN(String) \
    strlen(String)

/* If not zero, forces data alignment in the variable argument list
   by its individual size. */
#define gcdALIGNBYSIZE      1

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_kernel_debug_h_ */
