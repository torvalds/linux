/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#ifndef CSR_WIFI_ROUTER_TASK_H__
#define CSR_WIFI_ROUTER_TASK_H__

#include "csr_sched.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CSR_WIFI_ROUTER_LOG_ID 0x1201FFFF
extern CsrSchedQid CSR_WIFI_ROUTER_IFACEQUEUE;
void CsrWifiRouterInit(void **gash);
void CsrWifiRouterDeinit(void **gash);
void CsrWifiRouterHandler(void **gash);

#ifdef __cplusplus
}
#endif

#endif /* CSR_WIFI_ROUTER_TASK_H__ */

