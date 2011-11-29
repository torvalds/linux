//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

#ifndef _DEBUG_LINUX_H_
#define _DEBUG_LINUX_H_

    /* macro to remove parens */
#define ATH_PRINTX_ARG(arg...) arg

#ifdef DEBUG
    /* NOTE: the AR_DEBUG_PRINTF macro is defined here to handle special handling of variable arg macros
     * which may be compiler dependent. */
#define AR_DEBUG_PRINTF(mask, args) do {        \
    if (GET_ATH_MODULE_DEBUG_VAR_MASK(ATH_MODULE_NAME) & (mask)) {                    \
        A_LOGGER(mask, ATH_MODULE_NAME, ATH_PRINTX_ARG args);    \
    }                                            \
} while (0)
#else
    /* on non-debug builds, keep in error and warning messages in the driver, all other
     * message tracing will get compiled out */
#define AR_DEBUG_PRINTF(mask, args) \
    if ((mask) & (ATH_DEBUG_ERR | ATH_DEBUG_WARN)) { A_PRINTF(ATH_PRINTX_ARG args); }

#endif

    /* compile specific macro to get the function name string */
#define _A_FUNCNAME_  __func__


#endif /* _DEBUG_LINUX_H_ */
