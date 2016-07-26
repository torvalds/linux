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


#ifndef __gc_hal_types_h_
#define __gc_hal_types_h_

#include "gc_hal_version.h"
#include "gc_hal_options.h"

#if !defined(VIV_KMD)
#if defined(__KERNEL__)
#include "linux/version.h"
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
    typedef unsigned long uintptr_t;
#   endif
#   include "linux/types.h"
#elif defined(UNDER_CE)
#include <crtdefs.h>
#elif defined(_MSC_VER) && (_MSC_VER <= 1500)
#include <crtdefs.h>
#include "vadefs.h"
#elif defined(__QNXNTO__)
#define _QNX_SOURCE
#include <stdint.h>
#include <stddef.h>
#else
#include <stdlib.h>
#include <stddef.h>
#include <stdint.h>
#endif
#endif

#ifdef _WIN32
#pragma warning(disable:4127)   /* Conditional expression is constant (do { }
                                ** while(0)). */
#pragma warning(disable:4100)   /* Unreferenced formal parameter. */
#pragma warning(disable:4204)   /* Non-constant aggregate initializer (C99). */
#pragma warning(disable:4131)   /* Uses old-style declarator (for Bison and
                                ** Flex generated files). */
#pragma warning(disable:4206)   /* Translation unit is empty. */
#pragma warning(disable:4214)   /* Nonstandard extension used :
                                ** bit field types other than int. */
#endif

#ifdef __cplusplus
extern "C" {
#endif

/******************************************************************************\
**  Platform macros.
*/

#if defined(__GNUC__)
#   define gcdHAS_ELLIPSIS      1       /* GCC always has it. */
#elif defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)
#   define gcdHAS_ELLIPSIS      1       /* C99 has it. */
#elif defined(_MSC_VER) && (_MSC_VER >= 1500)
#   define gcdHAS_ELLIPSIS      1       /* MSVC 2007+ has it. */
#elif defined(UNDER_CE)
#if UNDER_CE >= 600
#       define gcdHAS_ELLIPSIS  1
#   else
#       define gcdHAS_ELLIPSIS  0
#   endif
#else
#   error "gcdHAS_ELLIPSIS: Platform could not be determined"
#endif

/******************************************************************************\
************************************ Keyword ***********************************
\******************************************************************************/

#if defined(ANDROID) && defined(__BIONIC_FORTIFY)
#if defined(__clang__)
#       define gcmINLINE            __inline__ __attribute__ ((always_inline)) __attribute__ ((gnu_inline))
#   else
#       define gcmINLINE            __inline__ __attribute__ ((always_inline)) __attribute__ ((gnu_inline)) __attribute__ ((artificial))
#   endif
#elif ((defined(__STDC_VERSION__) && (__STDC_VERSION__ >= 199901L)) || defined(__APPLE__))
#   define gcmINLINE            inline      /* C99 keyword. */
#elif defined(__GNUC__)
#   define gcmINLINE            __inline__  /* GNU keyword. */
#elif defined(_MSC_VER) || defined(UNDER_CE)
#   define gcmINLINE            __inline    /* Internal keyword. */
#else
#   error "gcmINLINE: Platform could not be determined"
#endif

/* Possible debug flags. */
#define gcdDEBUG_NONE           0
#define gcdDEBUG_ALL            (1 << 0)
#define gcdDEBUG_FATAL          (1 << 1)
#define gcdDEBUG_TRACE          (1 << 2)
#define gcdDEBUG_BREAK          (1 << 3)
#define gcdDEBUG_ASSERT         (1 << 4)
#define gcdDEBUG_CODE           (1 << 5)
#define gcdDEBUG_STACK          (1 << 6)

#define gcmIS_DEBUG(flag)       ( gcdDEBUG & (flag | gcdDEBUG_ALL) )

#ifndef gcdDEBUG
#if (defined(DBG) && DBG) || defined(DEBUG) || defined(_DEBUG)
#       define gcdDEBUG         gcdDEBUG_ALL
#   else
#       define gcdDEBUG         gcdDEBUG_NONE
#   endif
#endif

#ifdef _USRDLL
#ifdef _MSC_VER
#ifdef HAL_EXPORTS
#           define HALAPI       __declspec(dllexport)
#       else
#           define HALAPI       __declspec(dllimport)
#       endif
#       define HALDECL          __cdecl
#   else
#ifdef HAL_EXPORTS
#           define HALAPI
#       else
#           define HALAPI       extern
#       endif
#   endif
#else
#   define HALAPI
#   define HALDECL
#endif

/******************************************************************************\
********************************** Common Types ********************************
\******************************************************************************/

#define gcvFALSE                0
#define gcvTRUE                 1

#define gcvINFINITE             ((gctUINT32) ~0U)

#define gcvINVALID_HANDLE       ((gctHANDLE) ~0U)

typedef int                     gctBOOL;
typedef gctBOOL *               gctBOOL_PTR;

typedef int                     gctINT;
typedef signed char             gctINT8;
typedef signed short            gctINT16;
typedef signed int              gctINT32;
typedef signed long long        gctINT64;

typedef gctINT *                gctINT_PTR;
typedef gctINT8 *               gctINT8_PTR;
typedef gctINT16 *              gctINT16_PTR;
typedef gctINT32 *              gctINT32_PTR;
typedef gctINT64 *              gctINT64_PTR;

typedef unsigned int            gctUINT;
typedef unsigned char           gctUINT8;
typedef unsigned short          gctUINT16;
typedef unsigned int            gctUINT32;
typedef unsigned long long      gctUINT64;
typedef uintptr_t               gctUINTPTR_T;
typedef ptrdiff_t               gctPTRDIFF_T;

typedef gctUINT *               gctUINT_PTR;
typedef gctUINT8 *              gctUINT8_PTR;
typedef gctUINT16 *             gctUINT16_PTR;
typedef gctUINT32 *             gctUINT32_PTR;
typedef gctUINT64 *             gctUINT64_PTR;

typedef size_t                  gctSIZE_T;
typedef gctSIZE_T *             gctSIZE_T_PTR;
typedef gctUINT32               gctTRACE;

#ifdef __cplusplus
#   define gcvNULL              0
#else
#   define gcvNULL              ((void *) 0)
#endif

#define gcvMAXINT8              0x7f
#define gcvMININT8              0x80
#define gcvMAXINT16             0x7fff
#define gcvMININT16             0x8000
#define gcvMAXINT32             0x7fffffff
#define gcvMININT32             0x80000000
#define gcvMAXINT64             0x7fffffffffffffff
#define gcvMININT64             0x8000000000000000
#define gcvMAXUINT8             0xff
#define gcvMINUINT8             0x0
#define gcvMAXUINT16            0xffff
#define gcvMINUINT16            0x0
#define gcvMAXUINT32            0xffffffff
#define gcvMINUINT32            0x0
#define gcvMAXUINT64            0xffffffffffffffff
#define gcvMINUINT64            0x0
#define gcvMAXUINTPTR_T         (~(gctUINTPTR_T)0)

typedef float                   gctFLOAT;
typedef signed int              gctFIXED_POINT;
typedef float *                 gctFLOAT_PTR;

typedef void *                  gctPHYS_ADDR;
typedef void *                  gctHANDLE;
typedef void *                  gctFILE;
typedef void *                  gctSIGNAL;
typedef void *                  gctWINDOW;
typedef void *                  gctIMAGE;
typedef void *                  gctSYNC_POINT;
typedef void *                  gctSHBUF;

typedef void *                  gctSEMAPHORE;

typedef void *                  gctPOINTER;
typedef const void *            gctCONST_POINTER;

typedef char                    gctCHAR;
typedef char *                  gctSTRING;
typedef const char *            gctCONST_STRING;

typedef gctUINT64               gctPHYS_ADDR_T;

typedef struct _gcsCOUNT_STRING
{
    gctSIZE_T                   Length;
    gctCONST_STRING             String;
}
gcsCOUNT_STRING;

typedef union _gcuFLOAT_UINT32
{
    gctFLOAT    f;
    gctUINT32   u;
}
gcuFLOAT_UINT32;

/* Fixed point constants. */
#define gcvZERO_X               ((gctFIXED_POINT) 0x00000000)
#define gcvHALF_X               ((gctFIXED_POINT) 0x00008000)
#define gcvONE_X                ((gctFIXED_POINT) 0x00010000)
#define gcvNEGONE_X             ((gctFIXED_POINT) 0xFFFF0000)
#define gcvTWO_X                ((gctFIXED_POINT) 0x00020000)



#define gcmFIXEDCLAMP_NEG1_TO_1(_x) \
    (((_x) < gcvNEGONE_X) \
        ? gcvNEGONE_X \
        : (((_x) > gcvONE_X) \
            ? gcvONE_X \
            : (_x)))

#define gcmFLOATCLAMP_NEG1_TO_1(_f) \
    (((_f) < -1.0f) \
        ? -1.0f \
        : (((_f) > 1.0f) \
            ? 1.0f \
            : (_f)))


#define gcmFIXEDCLAMP_0_TO_1(_x) \
    (((_x) < 0) \
        ? 0 \
        : (((_x) > gcvONE_X) \
            ? gcvONE_X \
            : (_x)))

#define gcmFLOATCLAMP_0_TO_1(_f) \
    (((_f) < 0.0f) \
        ? 0.0f \
        : (((_f) > 1.0f) \
            ? 1.0f \
            : (_f)))


/******************************************************************************\
******************************* Multicast Values *******************************
\******************************************************************************/

/* Value types. */
typedef enum _gceVALUE_TYPE
{
    gcvVALUE_UINT = 0x0,
    gcvVALUE_FIXED,
    gcvVALUE_FLOAT,
    gcvVALUE_INT,

    /*
    ** The value need be unsigned denormalized. clamp (0.0-1.0) should be done first.
    */
    gcvVALUE_FLAG_UNSIGNED_DENORM = 0x00010000,

    /*
    ** The value need be signed denormalized. clamp (-1.0-1.0) should be done first.
    */
    gcvVALUE_FLAG_SIGNED_DENORM   = 0x00020000,

    /*
    ** The value need to gammar
    */
    gcvVALUE_FLAG_GAMMAR          = 0x00040000,

    /*
    ** The value need to convert from float to float16
    */
    gcvVALUE_FLAG_FLOAT_TO_FLOAT16 = 0x0080000,

    /*
    ** Mask for flag field.
    */
    gcvVALUE_FLAG_MASK            = 0xFFFF0000,
}
gceVALUE_TYPE;

/* Value unions. */
typedef union _gcuVALUE
{
    gctUINT                     uintValue;
    gctFIXED_POINT              fixedValue;
    gctFLOAT                    floatValue;
    gctINT                      intValue;
}
gcuVALUE;




/* Stringizing macro. */
#define gcmSTRING(Value)        #Value

/******************************************************************************\
******************************* Fixed Point Math *******************************
\******************************************************************************/

#define gcmXMultiply(x1, x2)            gcoMATH_MultiplyFixed(x1, x2)
#define gcmXDivide(x1, x2)              gcoMATH_DivideFixed(x1, x2)
#define gcmXMultiplyDivide(x1, x2, x3)  gcoMATH_MultiplyDivideFixed(x1, x2, x3)

/* 2D Engine profile. */
typedef struct _gcs2D_PROFILE
{
    /* Cycle count.
       32bit counter incremented every 2D clock cycle.
       Wraps back to 0 when the counter overflows.
    */
    gctUINT32 cycleCount;

    /* Pixels rendered by the 2D engine.
       Resets to 0 every time it is read. */
    gctUINT32 pixelsRendered;
}
gcs2D_PROFILE;

/* Macro to combine four characters into a Charcater Code. */
#define gcmCC(c1, c2, c3, c4) \
( \
    (char) (c1) \
    | \
    ((char) (c2) <<  8) \
    | \
    ((char) (c3) << 16) \
    | \
    ((char) (c4) << 24) \
)

#define gcmPRINTABLE(c)         ((((c) >= ' ') && ((c) <= '}')) ? ((c) != '%' ?  (c) : ' ') : ' ')

#define gcmCC_PRINT(cc) \
    gcmPRINTABLE((char) ( (cc)        & 0xFF)), \
    gcmPRINTABLE((char) (((cc) >>  8) & 0xFF)), \
    gcmPRINTABLE((char) (((cc) >> 16) & 0xFF)), \
    gcmPRINTABLE((char) (((cc) >> 24) & 0xFF))

/******************************************************************************\
****************************** Function Parameters *****************************
\******************************************************************************/

#define IN
#define OUT
#define INOUT
#define OPTIONAL

/******************************************************************************\
********************************* Status Codes *********************************
\******************************************************************************/

typedef enum _gceSTATUS
{
    gcvSTATUS_OK                    =   0,
    gcvSTATUS_FALSE                 =   0,
    gcvSTATUS_TRUE                  =   1,
    gcvSTATUS_NO_MORE_DATA          =   2,
    gcvSTATUS_CACHED                =   3,
    gcvSTATUS_MIPMAP_TOO_LARGE      =   4,
    gcvSTATUS_NAME_NOT_FOUND        =   5,
    gcvSTATUS_NOT_OUR_INTERRUPT     =   6,
    gcvSTATUS_MISMATCH              =   7,
    gcvSTATUS_MIPMAP_TOO_SMALL      =   8,
    gcvSTATUS_LARGER                =   9,
    gcvSTATUS_SMALLER               =   10,
    gcvSTATUS_CHIP_NOT_READY        =   11,
    gcvSTATUS_NEED_CONVERSION       =   12,
    gcvSTATUS_SKIP                  =   13,
    gcvSTATUS_DATA_TOO_LARGE        =   14,
    gcvSTATUS_INVALID_CONFIG        =   15,
    gcvSTATUS_CHANGED               =   16,
    gcvSTATUS_NOT_SUPPORT_DITHER    =   17,
    gcvSTATUS_EXECUTED              =   18,
    gcvSTATUS_TERMINATE             =   19,

    gcvSTATUS_INVALID_ARGUMENT      =   -1,
    gcvSTATUS_INVALID_OBJECT        =   -2,
    gcvSTATUS_OUT_OF_MEMORY         =   -3,
    gcvSTATUS_MEMORY_LOCKED         =   -4,
    gcvSTATUS_MEMORY_UNLOCKED       =   -5,
    gcvSTATUS_HEAP_CORRUPTED        =   -6,
    gcvSTATUS_GENERIC_IO            =   -7,
    gcvSTATUS_INVALID_ADDRESS       =   -8,
    gcvSTATUS_CONTEXT_LOSSED        =   -9,
    gcvSTATUS_TOO_COMPLEX           =   -10,
    gcvSTATUS_BUFFER_TOO_SMALL      =   -11,
    gcvSTATUS_INTERFACE_ERROR       =   -12,
    gcvSTATUS_NOT_SUPPORTED         =   -13,
    gcvSTATUS_MORE_DATA             =   -14,
    gcvSTATUS_TIMEOUT               =   -15,
    gcvSTATUS_OUT_OF_RESOURCES      =   -16,
    gcvSTATUS_INVALID_DATA          =   -17,
    gcvSTATUS_INVALID_MIPMAP        =   -18,
    gcvSTATUS_NOT_FOUND             =   -19,
    gcvSTATUS_NOT_ALIGNED           =   -20,
    gcvSTATUS_INVALID_REQUEST       =   -21,
    gcvSTATUS_GPU_NOT_RESPONDING    =   -22,
    gcvSTATUS_TIMER_OVERFLOW        =   -23,
    gcvSTATUS_VERSION_MISMATCH      =   -24,
    gcvSTATUS_LOCKED                =   -25,
    gcvSTATUS_INTERRUPTED           =   -26,
    gcvSTATUS_DEVICE                =   -27,
    gcvSTATUS_NOT_MULTI_PIPE_ALIGNED =   -28,
    gcvSTATUS_OUT_OF_SAMPLER         =   -29,

    /* Linker errors. */
    gcvSTATUS_GLOBAL_TYPE_MISMATCH              =   -1000,
    gcvSTATUS_TOO_MANY_ATTRIBUTES               =   -1001,
    gcvSTATUS_TOO_MANY_UNIFORMS                 =   -1002,
    gcvSTATUS_TOO_MANY_VARYINGS                 =   -1003,
    gcvSTATUS_UNDECLARED_VARYING                =   -1004,
    gcvSTATUS_VARYING_TYPE_MISMATCH             =   -1005,
    gcvSTATUS_MISSING_MAIN                      =   -1006,
    gcvSTATUS_NAME_MISMATCH                     =   -1007,
    gcvSTATUS_INVALID_INDEX                     =   -1008,
    gcvSTATUS_UNIFORM_MISMATCH                  =   -1009,
    gcvSTATUS_UNSAT_LIB_SYMBOL                  =   -1010,
    gcvSTATUS_TOO_MANY_SHADERS                  =   -1011,
    gcvSTATUS_LINK_INVALID_SHADERS              =   -1012,
    gcvSTATUS_CS_NO_WORKGROUP_SIZE              =   -1013,
    gcvSTATUS_LINK_LIB_ERROR                    =   -1014,

    gcvSTATUS_SHADER_VERSION_MISMATCH           =   -1015,
    gcvSTATUS_TOO_MANY_INSTRUCTION              =   -1016,
    gcvSTATUS_SSBO_MISMATCH                     =   -1017,
    gcvSTATUS_TOO_MANY_OUTPUT                   =   -1018,
    gcvSTATUS_TOO_MANY_INPUT                    =   -1019,
    gcvSTATUS_NOT_SUPPORT_CL                    =   -1020,
    gcvSTATUS_NOT_SUPPORT_INTEGER               =   -1021,
    gcvSTATUS_UNIFORM_TYPE_MISMATCH             =   -1022,

    gcvSTATUS_MISSING_PRIMITIVE_TYPE            =   -1023,
    gcvSTATUS_MISSING_OUTPUT_VERTEX_COUNT       =   -1024,
    gcvSTATUS_NON_INVOCATION_ID_AS_INDEX        =   -1025,
    gcvSTATUS_INPUT_ARRAY_SIZE_MISMATCH         =   -1026,
    gcvSTATUS_OUTPUT_ARRAY_SIZE_MISMATCH        =   -1027,
    gcvSTATUS_LOCATION_ALIASED                  =   -1028,

    /* Compiler errors. */
    gcvSTATUS_COMPILER_FE_PREPROCESSOR_ERROR    =   -2000,
    gcvSTATUS_COMPILER_FE_PARSER_ERROR          =   -2001,

    /* Recompilation Errors */
    gcvSTATUS_RECOMPILER_CONVERT_UNIMPLEMENTED  =   -3000,
}
gceSTATUS;

/******************************************************************************\
********************************* Status Macros ********************************
\******************************************************************************/

#define gcmIS_ERROR(status)         (status < 0)
#define gcmNO_ERROR(status)         (status >= 0)
#define gcmIS_SUCCESS(status)       (status == gcvSTATUS_OK)

/******************************************************************************\
********************************* Field Macros *********************************
\******************************************************************************/

#define __gcmSTART(reg_field) \
    (0 ? reg_field)

#define __gcmEND(reg_field) \
    (1 ? reg_field)

#define __gcmGETSIZE(reg_field) \
    (__gcmEND(reg_field) - __gcmSTART(reg_field) + 1)

#define __gcmALIGN(data, reg_field) \
    (((gctUINT32) (data)) << __gcmSTART(reg_field))

#define __gcmMASK(reg_field) \
    ((gctUINT32) ((__gcmGETSIZE(reg_field) == 32) \
        ?  ~0 \
        : (~(~0 << __gcmGETSIZE(reg_field)))))

/*******************************************************************************
**
**  gcmFIELDMASK
**
**      Get aligned field mask.
**
**  ARGUMENTS:
**
**      reg     Name of register.
**      field   Name of field within register.
*/
#define gcmFIELDMASK(reg, field) \
( \
    __gcmALIGN(__gcmMASK(reg##_##field), reg##_##field) \
)

/*******************************************************************************
**
**  gcmGETFIELD
**
**      Extract the value of a field from specified data.
**
**  ARGUMENTS:
**
**      data    Data value.
**      reg     Name of register.
**      field   Name of field within register.
*/
#define gcmGETFIELD(data, reg, field) \
( \
    ((((gctUINT32) (data)) >> __gcmSTART(reg##_##field)) \
        & __gcmMASK(reg##_##field)) \
)

/*******************************************************************************
**
**  gcmSETFIELD
**
**      Set the value of a field within specified data.
**
**  ARGUMENTS:
**
**      data    Data value.
**      reg     Name of register.
**      field   Name of field within register.
**      value   Value for field.
*/
#define gcmSETFIELD(data, reg, field, value) \
( \
    (((gctUINT32) (data)) \
        & ~__gcmALIGN(__gcmMASK(reg##_##field), reg##_##field)) \
        |  __gcmALIGN((gctUINT32) (value) \
            & __gcmMASK(reg##_##field), reg##_##field) \
)

/*******************************************************************************
**
**  gcmSETFIELDVALUE
**
**      Set the value of a field within specified data with a
**      predefined value.
**
**  ARGUMENTS:
**
**      data    Data value.
**      reg     Name of register.
**      field   Name of field within register.
**      value   Name of the value within the field.
*/
#define gcmSETFIELDVALUE(data, reg, field, value) \
( \
    (((gctUINT32) (data)) \
        & ~__gcmALIGN(__gcmMASK(reg##_##field), reg##_##field)) \
        |  __gcmALIGN(reg##_##field##_##value \
            & __gcmMASK(reg##_##field), reg##_##field) \
)

/*******************************************************************************
**
**  gcmGETMASKEDFIELDMASK
**
**      Determine field mask of a masked field.
**
**  ARGUMENTS:
**
**      reg     Name of register.
**      field   Name of field within register.
*/
#define gcmGETMASKEDFIELDMASK(reg, field) \
( \
    gcmSETFIELD(0, reg,          field, ~0) | \
    gcmSETFIELD(0, reg, MASK_ ## field, ~0)   \
)

/*******************************************************************************
**
**  gcmSETMASKEDFIELD
**
**      Set the value of a masked field with specified data.
**
**  ARGUMENTS:
**
**      reg     Name of register.
**      field   Name of field within register.
**      value   Value for field.
*/
#define gcmSETMASKEDFIELD(reg, field, value) \
( \
    gcmSETFIELD     (~0, reg,          field, value) & \
    gcmSETFIELDVALUE(~0, reg, MASK_ ## field, ENABLED) \
)

/*******************************************************************************
**
**  gcmSETMASKEDFIELDVALUE
**
**      Set the value of a masked field with specified data.
**
**  ARGUMENTS:
**
**      reg     Name of register.
**      field   Name of field within register.
**      value   Value for field.
*/
#define gcmSETMASKEDFIELDVALUE(reg, field, value) \
( \
    gcmSETFIELDVALUE(~0, reg,          field, value) & \
    gcmSETFIELDVALUE(~0, reg, MASK_ ## field, ENABLED) \
)

/*******************************************************************************
**
**  gcmVERIFYFIELDVALUE
**
**      Verify if the value of a field within specified data equals a
**      predefined value.
**
**  ARGUMENTS:
**
**      data    Data value.
**      reg     Name of register.
**      field   Name of field within register.
**      value   Name of the value within the field.
*/
#define gcmVERIFYFIELDVALUE(data, reg, field, value) \
( \
    (((gctUINT32) (data)) >> __gcmSTART(reg##_##field) & \
                             __gcmMASK(reg##_##field)) \
        == \
    (reg##_##field##_##value & __gcmMASK(reg##_##field)) \
)

/*******************************************************************************
**  Bit field macros.
*/

#define __gcmSTARTBIT(Field) \
    ( 1 ? Field )

#define __gcmBITSIZE(Field) \
    ( 0 ? Field )

#define __gcmBITMASK(Field) \
( \
    (1 << __gcmBITSIZE(Field)) - 1 \
)

#define gcmGETBITS(Value, Type, Field) \
( \
    ( ((Type) (Value)) >> __gcmSTARTBIT(Field) ) \
    & \
    __gcmBITMASK(Field) \
)

#define gcmSETBITS(Value, Type, Field, NewValue) \
( \
    ( ((Type) (Value)) \
    & ~(__gcmBITMASK(Field) << __gcmSTARTBIT(Field)) \
    ) \
    | \
    ( ( ((Type) (NewValue)) \
      & __gcmBITMASK(Field) \
      ) << __gcmSTARTBIT(Field) \
    ) \
)

/*******************************************************************************
**
**  gcmISINREGRANGE
**
**      Verify whether the specified address is in the register range.
**
**  ARGUMENTS:
**
**      Address Address to be verified.
**      Name    Name of a register.
*/

#define gcmISINREGRANGE(Address, Name) \
( \
    ((Address & (~0U << Name ## _LSB)) == (Name ## _Address >> 2)) \
)

/******************************************************************************\
******************************** Ceiling Macro ********************************
\******************************************************************************/
#define gcmCEIL(x) ((x - (gctUINT32)x) == 0 ? (gctUINT32)x : (gctUINT32)x + 1)

/******************************************************************************\
******************************** Min/Max Macros ********************************
\******************************************************************************/

#define gcmMIN(x, y)            (((x) <= (y)) ?  (x) :  (y))
#define gcmMAX(x, y)            (((x) >= (y)) ?  (x) :  (y))
#define gcmCLAMP(x, min, max)   (((x) < (min)) ? (min) : \
                                 ((x) > (max)) ? (max) : (x))
#define gcmABS(x)               (((x) < 0)    ? -(x) :  (x))
#define gcmNEG(x)               (((x) < 0)    ?  (x) : -(x))

/******************************************************************************\
******************************** Bit Macro ********************************
\******************************************************************************/
#define gcmBITSET(x, y)         ((x) & (y))
/*******************************************************************************
**
**  gcmPTR2INT
**
**      Convert a pointer to an integer value.
**
**  ARGUMENTS:
**
**      p       Pointer value.
*/
#define gcmPTR2INT(p) \
( \
    (gctUINTPTR_T) (p) \
)

#define gcmPTR2INT32(p) \
( \
    (gctUINT32)(gctUINTPTR_T) (p) \
)

/*******************************************************************************
**
**  gcmINT2PTR
**
**      Convert an integer value into a pointer.
**
**  ARGUMENTS:
**
**      v       Integer value.
*/

#define gcmINT2PTR(i) \
( \
    (gctPOINTER) (gctUINTPTR_T)(i) \
)

/*******************************************************************************
**
**  gcmOFFSETOF
**
**      Compute the byte offset of a field inside a structure.
**
**  ARGUMENTS:
**
**      s       Structure name.
**      field   Field name.
*/
#define gcmOFFSETOF(s, field) \
( \
    gcmPTR2INT32(& (((struct s *) 0)->field)) \
)

/*******************************************************************************
**
**  gcmCONTAINEROF
**
**      Get containing structure of a member.
**
**  ARGUMENTS:
**
**      Pointer Pointer of member.
**      Type    Structure name.
**      Name    Field name.
*/
#define gcmCONTAINEROF(Pointer, Type, Member) \
(\
    (struct Type *)((gctUINTPTR_T)Pointer - gcmOFFSETOF(Type, Member)) \
)

/*******************************************************************************
**
** gcmSWAB32
**
**      Return a value with all bytes in the 32 bit argument swapped.
*/
#define gcmSWAB32(x) ((gctUINT32)( \
        (((gctUINT32)(x) & (gctUINT32)0x000000FFUL) << 24) | \
        (((gctUINT32)(x) & (gctUINT32)0x0000FF00UL) << 8)  | \
        (((gctUINT32)(x) & (gctUINT32)0x00FF0000UL) >> 8)  | \
        (((gctUINT32)(x) & (gctUINT32)0xFF000000UL) >> 24)))

/*******************************************************************************
***** Database ****************************************************************/

typedef struct _gcsDATABASE_COUNTERS
{
    /* Number of currently allocated bytes. */
    gctUINT64                   bytes;

    /* Maximum number of bytes allocated (memory footprint). */
    gctUINT64                   maxBytes;

    /* Total number of bytes allocated. */
    gctUINT64                   totalBytes;

    /* The numbers of times video memory was allocated. */
    gctUINT32                   allocCount;

    /* The numbers of times video memory was freed. */
    gctUINT32                   freeCount;
}
gcsDATABASE_COUNTERS;

typedef struct _gcuDATABASE_INFO
{
    /* Counters. */
    gcsDATABASE_COUNTERS        counters;

    /* Time value. */
    gctUINT64                   time;
}
gcuDATABASE_INFO;

/*******************************************************************************
***** Frame database **********************************************************/

/* gcsHAL_FRAME_INFO */
typedef struct _gcsHAL_FRAME_INFO
{
    /* Current timer tick. */
    OUT gctUINT64               ticks;

    /* Bandwidth counters. */
    OUT gctUINT                 readBytes8[8];
    OUT gctUINT                 writeBytes8[8];

    /* Counters. */
    OUT gctUINT                 cycles[8];
    OUT gctUINT                 idleCycles[8];
    OUT gctUINT                 mcCycles[8];
    OUT gctUINT                 readRequests[8];
    OUT gctUINT                 writeRequests[8];

    /* 3D counters. */
    OUT gctUINT                 vertexCount;
    OUT gctUINT                 primitiveCount;
    OUT gctUINT                 rejectedPrimitives;
    OUT gctUINT                 culledPrimitives;
    OUT gctUINT                 clippedPrimitives;
    OUT gctUINT                 outPrimitives;
    OUT gctUINT                 inPrimitives;
    OUT gctUINT                 culledQuadCount;
    OUT gctUINT                 totalQuadCount;
    OUT gctUINT                 quadCount;
    OUT gctUINT                 totalPixelCount;

    /* PE counters. */
    OUT gctUINT                 colorKilled[8];
    OUT gctUINT                 colorDrawn[8];
    OUT gctUINT                 depthKilled[8];
    OUT gctUINT                 depthDrawn[8];

    /* Shader counters. */
    OUT gctUINT                 shaderCycles;
    OUT gctUINT                 vsInstructionCount;
    OUT gctUINT                 vsTextureCount;
    OUT gctUINT                 psInstructionCount;
    OUT gctUINT                 psTextureCount;

    /* Texture counters. */
    OUT gctUINT                 bilinearRequests;
    OUT gctUINT                 trilinearRequests;
    OUT gctUINT                 txBytes8;
    OUT gctUINT                 txHitCount;
    OUT gctUINT                 txMissCount;
}
gcsHAL_FRAME_INFO;

typedef struct _gckLINKDATA * gckLINKDATA;
struct _gckLINKDATA
{
    gctUINT32                   start;
    gctUINT32                   end;
    gctUINT32                   pid;
    gctUINT32                   linkLow;
    gctUINT32                   linkHigh;
};

typedef struct _gckADDRESSDATA * gckADDRESSDATA;
struct _gckADDRESSDATA
{
    gctUINT32                   start;
    gctUINT32                   end;
};

typedef union _gcuQUEUEDATA
{
    struct _gckLINKDATA         linkData;

    struct _gckADDRESSDATA      addressData;
}
gcuQUEUEDATA;

typedef struct _gckQUEUE * gckQUEUE;
struct _gckQUEUE
{
    gcuQUEUEDATA *              datas;
    gctUINT32                   rear;
    gctUINT32                   front;
    gctUINT32                   count;
    gctUINT32                   size;
};

typedef enum _gceTRACEMODE
{
    gcvTRACEMODE_NONE     = 0,
    gcvTRACEMODE_FULL     = 1,
    gcvTRACEMODE_LOGGER   = 2,
    gcvTRACEMODE_PRE      = 3,
    gcvTRACEMODE_POST     = 4,
} gceTRACEMODE;

typedef struct _gcsLISTHEAD * gcsLISTHEAD_PTR;
typedef struct _gcsLISTHEAD
{
    gcsLISTHEAD_PTR     prev;
    gcsLISTHEAD_PTR     next;
}
gcsLISTHEAD;

/*
    gcvFEATURE_DATABASE_DATE_MASK

    Mask used to control which bits of chip date will be used to
    query feature database, ignore release date for fpga and emulator.
*/
#if (gcdFPGA_BUILD || defined(EMULATOR))
#   define gcvFEATURE_DATABASE_DATE_MASK    (0U)
#else
#   define gcvFEATURE_DATABASE_DATE_MASK    (~0U)
#endif

#ifdef __cplusplus
}
#endif

#endif /* __gc_hal_types_h_ */
