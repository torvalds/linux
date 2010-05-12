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

#ifndef INCLUDED_NVOS_TRACE_H
#define INCLUDED_NVOS_TRACE_H

#define NVOS_TRACE 0

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

/**
 * The nvos_trace.txt is a nvos trace file to collect aggregate statistics
 * about how system calls and resources are used in higher-level software.
 * It has format:
 *    NvOsFunctionName , CallingFile , CallingLine , CallTime , Data 
 * NvOsFunctionName is the function name 
 * CallingFile and CallingLine are just the __FILE__ and __LINE__ parameters. 
 * CallTime is a NvU32 storing time in miliseconds. 
 * Data is a function-specific data parameter. For MutexLock and MutexUnlock 
 * this would be the mutex handle. For NvOsAlloc * and NvOsFree this 
 * would be the allocated address. 
 *
 */

/**
 * opens the trace file nvos_trace.txt
 *
 * This function should be called via a macro so that it may be compiled out.
 *
 */
void 
NvOsTraceLogStart(void);

/**
 * closes the trace file nvos_trace.txt.
 *
 * This function should be called via a macro so that it may be compiled out.
 */
void
NvOsTraceLogEnd(void);

/**
 * emits a string to the trace file nvos_trace.txt
 *
 * @param format Printf style argument format string
 *
 * This function should be called via a macro so that it may be compiled out.
 *
 */
void
NvOsTraceLogPrintf( const char *format, ... );

/**
 * Helper macro to go along with NvOsTraceLogPrintf.  Usage:
 *    NVOS_TRACE_LOG_PRINTF(("foo: %s\n", bar));
 * The NvOs trace log prints will be disabled by default in all builds, debug
 * and release.
 * Note the use of double parentheses.
 *
 * To enable NvOs trace log prints
 *     #define NVOS_TRACE 1
 */
#if NVOS_TRACE
#define NVOS_TRACE_LOG_PRINTF(a)    NvOsTraceLogPrintf a
#define NVOS_TRACE_LOG_START    \
    do {                        \
        NvOsTraceLogStart();    \
    } while (0);
#define NVOS_TRACE_LOG_END      \
    do {                        \
        NvOsTraceLogEnd();      \
    } while (0);
#else
#define NVOS_TRACE_LOG_PRINTF(a)    (void)0
#define NVOS_TRACE_LOG_START        (void)0
#define NVOS_TRACE_LOG_END          (void)0
#endif

#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif /* NVOS_TRACE_H */

