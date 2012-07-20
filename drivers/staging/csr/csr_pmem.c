/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/module.h>


#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 33)
#include <linux/autoconf.h>
#elif LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 16)
#include <linux/config.h>
#endif

#include <linux/slab.h>

#include "csr_panic.h"
#include "csr_pmem.h"

void *CsrPmemAlloc(size_t size)
{
    void *ret;

    ret = kmalloc(size, GFP_KERNEL);
    if (!ret)
    {
        CsrPanic(CSR_TECH_FW, CSR_PANIC_FW_HEAP_EXHAUSTION,
            "out of memory");
    }

    return ret;
}
EXPORT_SYMBOL_GPL(CsrPmemAlloc);
