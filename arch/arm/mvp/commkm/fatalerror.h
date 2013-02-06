/*
 * Linux 2.6.32 and later Kernel module for VMware MVP Guest Communications
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
 * @brief fatal error handlers.  They all post fatal errors regardless of build
 * type.
 */

#ifndef _FATALERROR_H
#define _FATALERROR_H

#define INCLUDE_ALLOW_MVPD
#define INCLUDE_ALLOW_VMX
#define INCLUDE_ALLOW_MODULE
#define INCLUDE_ALLOW_MONITOR
#define INCLUDE_ALLOW_PV
#define INCLUDE_ALLOW_HOSTUSER
#define INCLUDE_ALLOW_GUESTUSER
#define INCLUDE_ALLOW_WORKSTATION
#define INCLUDE_ALLOW_GPL
#include "include_check.h"

#include "mvp_compiler.h"

#ifdef __cplusplus
extern "C" {
#endif

enum FECode {
   FECodeMisc,    ///< generic FATAL() call of sorts
   FECodeOOM,     ///< FATAL_OOM() call of sorts
   FECodeAssert,  ///< ASSERT() call of sorts
   FECodeNR,      ///< NOT_REACHED() call of sorts
   FECodeNI,      ///< NOT_IMPLEMENTED() call of sorts
   FECodeNT,      ///< NOT_TESTED() call of sorts
   FECodeCF       ///< COMPILE_FAIL() call of sorts
};
typedef enum FECode FECode;

#define FATAL() FatalError(__FILE__, __LINE__, FECodeMisc, 0, NULL)
#define FATAL_IF(x) do { if (UNLIKELY(x)) FATAL(); } while (0)
#define FATAL_OOM() FatalError(__FILE__, __LINE__, FECodeOOM, 0, NULL)
#define FATAL_OOM_IF(x) do { if (UNLIKELY(x)) FATAL_OOM(); } while (0)

extern _Bool FatalError_hit;

void NORETURN FatalError(char const *file,
                         int line,
                         FECode feCode,
                         int bugno,
                         char const *fmt,
                         ...) FORMAT(printf,5,6);

#define FATALERROR_COMMON(printFunc,                            \
                          printFuncV,                           \
                          file,                                 \
                          line,                                 \
                          feCode,                               \
                          bugno,                                \
                          fmt) {                                \
      va_list ap;                                               \
                                                                \
      printFunc("FatalError: %s:%d, code %d, bugno %d\n",       \
                file, line, feCode, bugno);                     \
      if (fmt != NULL) {                                        \
         va_start(ap, fmt);                                     \
         printFuncV(fmt, ap);                                   \
         va_end(ap);                                            \
      }                                                         \
   }

#if defined IN_HOSTUSER || defined IN_GUESTUSER || defined IN_WORKSTATION

#define FATALERROR_POSIX_USER \
void \
FatalError_VErrPrintf(const char *fmt, va_list ap) \
{ \
   vfprintf(stderr, fmt, ap); \
} \
\
void \
FatalError_ErrPrintf(const char *fmt, ...) \
{ \
   va_list ap; \
   va_start(ap, fmt); \
   FatalError_VErrPrintf(fmt, ap); \
   va_end(ap); \
} \
\
void NORETURN \
FatalError(char const *file, \
           int line, \
           FECode feCode, \
           int bugno, \
           const char *fmt, \
           ...) \
{ \
   FATALERROR_COMMON(FatalError_ErrPrintf, FatalError_VErrPrintf, file, line, feCode, bugno, fmt); \
   exit(EXIT_FAILURE); \
}
#endif

#ifdef __cplusplus
}
#endif

#endif
