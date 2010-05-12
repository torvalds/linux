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

#ifndef INCLUDED_NVRM_CONFIGURATION_H
#define INCLUDED_NVRM_CONFIGURATION_H

#include "nvcommon.h"
#include "nverror.h"

/**
 * The RM configuration variables are represented by two structures:
 * a configuration map, which lists all of the variables, their default
 * values and types, and a struct of strings, which holds the runtime value of
 * the variables.  The map holds the index into the runtime structure.
 *
 */

/**
 * The configuration varible type.
 */
typedef enum
{
    /* String should be parsed as a decimal */
    NvRmCfgType_Decimal = 0,

    /* String should be parsed as a hexadecimal */
    NvRmCfgType_Hex = 1,

    /* String should be parsed as a character */
    NvRmCfgType_Char = 2,

    /* String used as-is. */
    NvRmCfgType_String = 3,
} NvRmCfgType;

/**
 * The configuration map (all possible variables).  The map must be
 * null terminated.  Each Rm instance (for each chip) can/will have
 * different configuration maps.
 */
typedef struct NvRmCfgMap_t
{  
    const char *name;
    NvRmCfgType type; 
    void *initial; /* default value of the variable */
    void *offset; /* the index into the string structure */
} NvRmCfgMap;

/* helper macro for generating the offset for the map */
#define STRUCT_OFFSET( s, e )       (void *)(&(((s*)0)->e))

/* maximum size of a configuration variable */
#define NVRM_CFG_MAXLEN NVOS_PATH_MAX

/**
 * get the default configuration variables.
 *
 * @param map The configuration map
 * @param cfg The configuration runtime values
 */
NvError
NvRmPrivGetDefaultCfg( NvRmCfgMap *map, void *cfg );

/**
 * get requested configuration.
 *
 * @param map The configuration map
 * @param cfg The configuration runtime values
 *
 * Note: 'cfg' should have already been initialized with
 * NvRmPrivGetDefaultCfg()  before calling this.
 */
NvError
NvRmPrivReadCfgVars( NvRmCfgMap *map, void *cfg );

#endif
