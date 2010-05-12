/*
 * Copyright (c) 2006-2009 NVIDIA Corporation.
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

#ifndef INCLUDED_NVERROR_H
#define INCLUDED_NVERROR_H

/** 
 * @defgroup nverror Error Handling
 * 
 * nverror.h contains our error code enumeration and helper macros.
 * 
 * @{
 */

/**
 * The NvError enumeration contains ALL return / error codes.  Error codes
 * are specifically explicit to make it easy to identify where an error
 * came from.  
 * 
 * All error codes are derived from the macros in nverrval.h.
 * @ingroup nv_errors 
 */
typedef enum
{
#define NVERROR(_name_, _value_, _desc_) NvError_##_name_ = _value_,
    /* header included for macro expansion of error codes */
    #include "nverrval.h"
#undef NVERROR

    // An alias for success
    NvSuccess = NvError_Success,

    NvError_Force32 = 0x7FFFFFFF
} NvError;

/**
 * A helper macro to check a function's error return code and propagate any
 * errors upward.  This assumes that no cleanup is necessary in the event of
 * failure.  This macro does not locally define its own NvError variable out of
 * fear that this might burn too much stack space, particularly in debug builds
 * or with mediocre optimizing compilers.  The user of this macro is therefore
 * expected to provide their own local variable "NvError e;".
 */
#define NV_CHECK_ERROR(expr) \
    do \
    { \
        e = (expr); \
        if (e != NvSuccess) \
            return e; \
    } while (0)

/**
 * A helper macro to check a function's error return code and, if an error
 * occurs, jump to a label where cleanup can take place.  Like NV_CHECK_ERROR,
 * this macro does not locally define its own NvError variable.  (Even if we
 * wanted it to, this one can't, because the code at the "fail" label probably
 * needs to do a "return e;" to propagate the error upwards.)
 */
#define NV_CHECK_ERROR_CLEANUP(expr) \
    do \
    { \
        e = (expr); \
        if (e != NvSuccess) \
            goto fail; \
    } while (0)


/**
 * Prints err if it is an error (does nothing if err==NvSuccess).
 * Always returns err unchanged
 * never prints anything if err==NvSuccess)
 *
 * NOTE: Do not use this with errors that are expected to occur under normal
 * situations.
 *
 * @param err - the error to return
 * @returns err
 */
#define NV_SHOW_ERRORS  NV_DEBUG
#if     NV_SHOW_ERRORS
#define NV_SHOW_ERROR(err)  NvOsShowError(err,__FILE__,__LINE__)
#else
#define NV_SHOW_ERROR(err)  (err)
#endif


/** @} */

#endif // INCLUDED_NVERROR_H
