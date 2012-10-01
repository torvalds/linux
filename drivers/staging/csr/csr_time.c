/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include <linux/kernel.h>
#include <linux/version.h>
#include <linux/time.h>
#include <linux/module.h>

#include "csr_time.h"

CsrTime CsrTimeGet(CsrTime *high)
{
	struct timespec ts;
	u64 time;
	CsrTime low;

	ts = current_kernel_time();
	time = (u64) ts.tv_sec * 1000000 + ts.tv_nsec / 1000;

	if (high != NULL)
		*high = (CsrTime) ((time >> 32) & 0xFFFFFFFF);

	low = (CsrTime) (time & 0xFFFFFFFF);

	return low;
}
EXPORT_SYMBOL_GPL(CsrTimeGet);
