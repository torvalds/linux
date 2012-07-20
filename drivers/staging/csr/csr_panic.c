/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>

#include "csr_types.h"
#include "csr_panic.h"

void CsrPanic(u8 tech, u16 reason, const char *p)
{
    BUG_ON(1);
}
EXPORT_SYMBOL_GPL(CsrPanic);
