#ifndef CSR_FRAMEWORK_EXT_H__
#define CSR_FRAMEWORK_EXT_H__
/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2010
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

#include "csr_result.h"
#include "csr_framework_ext_types.h"

/* Result codes */
#define CSR_FE_RESULT_NO_MORE_EVENTS    ((CsrResult) 0x0001)
#define CSR_FE_RESULT_INVALID_POINTER   ((CsrResult) 0x0002)
#define CSR_FE_RESULT_INVALID_HANDLE    ((CsrResult) 0x0003)
#define CSR_FE_RESULT_NO_MORE_MUTEXES   ((CsrResult) 0x0004)
#define CSR_FE_RESULT_TIMEOUT           ((CsrResult) 0x0005)
#define CSR_FE_RESULT_NO_MORE_THREADS   ((CsrResult) 0x0006)

/* Thread priorities */
#define CSR_THREAD_PRIORITY_HIGHEST     ((u16) 0)
#define CSR_THREAD_PRIORITY_HIGH        ((u16) 1)
#define CSR_THREAD_PRIORITY_NORMAL      ((u16) 2)
#define CSR_THREAD_PRIORITY_LOW         ((u16) 3)
#define CSR_THREAD_PRIORITY_LOWEST      ((u16) 4)

#define CSR_EVENT_WAIT_INFINITE         ((u16) 0xFFFF)

void CsrThreadSleep(u16 sleepTimeInMs);

#endif
