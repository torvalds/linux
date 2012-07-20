/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include "csr_types.h"
#include "csr_formatted_io.h"
#include "csr_util.h"

s32 CsrSnprintf(CsrCharString *dest, CsrSize n, const CsrCharString *fmt, ...)
{
    s32 r;
    va_list args;
    va_start(args, fmt);
    r = CsrVsnprintf(dest, n, fmt, args);
    va_end(args);

    if (dest && (n > 0))
    {
        dest[n - 1] = '\0';
    }

    return r;
}
