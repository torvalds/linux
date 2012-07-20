#ifndef CSR_FORMATTED_IO_H__
#define CSR_FORMATTED_IO_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#ifdef __cplusplus
extern "C" {
#endif

#include <linux/types.h>

s32 CsrSnprintf(char *dest, size_t n, const char *fmt, ...);

#ifdef __cplusplus
}
#endif

#endif
