/*
 * Synergy compatible API -- basic types.
 *
 * Copyright (C) 2010 Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 */
#ifndef CSR_TYPES_H__
#define CSR_TYPES_H__

#include <oska/types.h>

#ifndef FALSE
#define FALSE false
#endif

#ifndef TRUE
#define TRUE true
#endif

/* Data types */

typedef size_t                  CsrSize;

typedef uint8_t                 CsrUint8;
typedef uint16_t                CsrUint16;
typedef uint32_t                CsrUint32;

typedef int8_t                  CsrInt8;
typedef int16_t                 CsrInt16;
typedef int32_t                 CsrInt32;

typedef bool                    CsrBool;

typedef char                    CsrCharString;
typedef unsigned char           CsrUtf8String;
typedef CsrUint16               CsrUtf16String; /* 16-bit UTF16 strings */
typedef CsrUint32               CsrUint24;

/*
 * 64-bit integers
 *
 * Note: If a given compiler does not support 64-bit types, it is
 * OK to omit these definitions;  32-bit versions of the code using
 * these types may be available.  Consult the relevant documentation
 * or the customer support group for information on this.
 */
#define CSR_HAVE_64_BIT_INTEGERS
typedef uint64_t    CsrUint64;
typedef int64_t     CsrInt64;

#endif
