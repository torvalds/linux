#ifndef CSR_UTIL_H__
#define CSR_UTIL_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

#include <linux/kernel.h>
#include <linux/types.h>
#include "csr_macro.h"

/*------------------------------------------------------------------*/
/* Base conversion */
/*------------------------------------------------------------------*/
void CsrUInt16ToHex(u16 number, char *str);

/*------------------------------------------------------------------*/
/* Standard C Library functions */
/*------------------------------------------------------------------*/
#ifdef CSR_USE_STDC_LIB
#define CsrMemCpy memcpy
#else /* !CSR_USE_STDC_LIB */
void *CsrMemCpy(void *dest, const void *src, size_t count);
#endif /* !CSR_USE_STDC_LIB */

#define CsrOffsetOf(st, m)  ((size_t) & ((st *) 0)->m)

#ifdef __cplusplus
}
#endif

#endif
