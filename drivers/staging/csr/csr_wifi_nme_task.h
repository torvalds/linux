/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_NME_TASK_H__
#define CSR_WIFI_NME_TASK_H__

#include <linux/types.h>
#include "csr_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

#ifndef CSR_WIFI_NME_ENABLE
#error CSR_WIFI_NME_ENABLE MUST be defined inorder to use csr_wifi_nme_task.h
#endif

#define CSR_WIFI_NME_LOG_ID 0x1203FFFF
extern CsrSchedQid CSR_WIFI_NME_IFACEQUEUE;
void CsrWifiNmeInit(void **gash);
void CsrWifiNmeDeinit(void **gash);
void CsrWifiNmeHandler(void **gash);

#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_NME_TASK_H__ */

