/*
 * Copyright (c) 2007-2009 NVIDIA Corporation.
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions are met:
 *
 * Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation
 * and/or other materials provided with the distribution.
 *
 * Neither the name of the NVIDIA Corporation nor the names of its contributors
 * may be used to endorse or promote products derived from this software
 * without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
 * POSSIBILITY OF SUCH DAMAGE.
 *
 */

#ifndef INCLUDED_NVRM_RMCTRACE_H
#define INCLUDED_NVRM_RMCTRACE_H

#include "nvcommon.h"
#include "nvos.h"
#include "nvrm_init.h"

/**
 * RMC is a file format for capturing accesses to hardware, both memory
 * and register, that may be played back against a simulator.  Drivers
 * are expected to emit RMC tracing if RMC tracing is enabled.
 *
 * The RM will already have an RMC file open before any drivers are expected
 * to access it, so it is not necessary for NvRmRmcOpen or Close to be called
 * by anyone except the RM itself (but drivers may want to if capturing a
 * subset of commands is useful).
 */

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

#if !defined(NV_OAL)
#define NV_OAL 0
#endif

// FIXME: better rmc compile time macros
#if !defined(NV_DEF_RMC_TRACE)
#if NV_DEBUG && !NV_OAL
#define NV_DEF_RMC_TRACE 1
#else
#define NV_DEF_RMC_TRACE 0
#endif
#endif

/**
 * exposed structure for RMC files.
 */
typedef struct NvRmRMCFile_t
{
    NvOsFileHandle file;
    NvBool enable; /* enable bit for writes */
} NvRmRmcFile;

/**
 * opens the an RMC file.
 *
 * @param name The name of the rmc file
 * @param rmc Out param - the opened rmc file (if successful)
 *
 * NvOsFile* operatations should not be used directly since RMC commands
 * or comments may be emited to the file on open/close/etc.
 */
NvError
NvRmRmcOpen( const char *name, NvRmRmcFile *rmc );

/**
 * closes an RMC file.
 *
 * @param rmc The rmc file to close.
 */
void
NvRmRmcClose( NvRmRmcFile *rmc );

/**
 * emits a string to the RMC file.
 *
 * @param file The RMC file
 * @param format Printf style argument format string
 *
 * NvRmRmcOpen must be called before this function.
 *
 * This function should be called via a macro so that it may be compiled out.
 * Note that double parens will be needed:
 *
 *     NVRM_RMC_TRACE(( file, "# filling memory with stuff\n" ));
 */
void
NvRmRmcTrace( NvRmRmcFile *rmc, const char *format, ... );

/**
 * retrieves the RM's global RMC file.
 *
 * @param hDevice The RM instance
 * @param file Output param: the RMC file
 */
NvError
NvRmGetRmcFile( NvRmDeviceHandle hDevice, NvRmRmcFile **file );

#if NV_DEF_RMC_TRACE
#define NVRM_RMC_TRACE(a) NvRmRmcTrace a
/**
 * enable or disable RMC tracing at runtime.
 *
 * @param file The RMC file
 * @param enable Either enable or disable rmc tracing
 */
#define NVRM_RMC_ENABLE(f, e) \
    ((f)->enable = (e))

#define NVRM_RMC_IS_ENABLED(f) \
    ((f)->enable != 0)

#else
#define NVRM_RMC_TRACE(a) (void)0
#define NVRM_RMC_ENABLE(f,e) (void)0
#define NVRM_RMC_IS_ENABLED(f) (void)0
#endif

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* NVRM_RMCTRACE_H */
