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

#ifndef INCLUDED_NVRM_CHIPLIB_H
#define INLCUDED_NVRM_CHIPLIB_H

#include "nvcommon.h"
#include "nvrm_hardware_access.h"

#ifdef __cplusplus
extern "C"
{
#endif  /* __cplusplus */

/**
 * Chiplib interrupt handler function.
 */
typedef void (* ChiplibHandleInterrupt)( void );

#if NV_DEF_ENVIRONMENT_SUPPORTS_SIM == 1
NvBool NvRmIsSimulation(void);
#else
#define NvRmIsSimulation() NV_FALSE
#endif

/**
 * starts chiplib.
 *
 * @param lib The chiplib name
 * @param cmdline The chiplib command line
 * @param handle The interrupt handler - will be called by chiplib
 */
NvError
NvRmPrivChiplibStartup( const char *lib, const char *cmdline,
    ChiplibHandleInterrupt handler );

/**
 * stops chiplib.
 */
void
NvRmPrivChiplibShutdown( void );

/**
 * maps a bogus virtual address to a physical address.
 *
 * @param addr The physical address to map
 * @param size The size of the mapping
 */
void *
NvRmPrivChiplibMap( NvRmPhysAddr addr, size_t size );

/**
 * unmaps a previously mapped pointer from NvRmPrivChiplibMap.
 *
 * @param addr The virtual address to unmap
 */
void
NvRmPrivChiplibUnmap( void *addr );


#ifdef __cplusplus
}
#endif  /* __cplusplus */

#endif
