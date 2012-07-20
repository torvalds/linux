#ifndef CSR_TYPES_H__
#define CSR_TYPES_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/stddef.h>
#include <linux/kernel.h>
#include <linux/types.h>
#include <linux/string.h>
#include <asm/byteorder.h>

#ifdef __cplusplus
extern "C" {
#endif

#undef  FALSE
#define FALSE (0)

#undef  TRUE
#define TRUE (1)

/*
 * 64-bit integers
 *
 * Note: If a given compiler does not support 64-bit types, it is
 * OK to omit these definitions;  32-bit versions of the code using
 * these types may be available.  Consult the relevant documentation
 * or the customer support group for information on this.
 */
#define CSR_HAVE_64_BIT_INTEGERS
typedef uint64_t CsrUint64;
typedef int64_t CsrInt64;


#ifdef __cplusplus
}
#endif

#endif
