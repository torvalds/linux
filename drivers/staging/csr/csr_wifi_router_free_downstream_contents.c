/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2011
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */
#include <linux/slab.h>
#include "csr_pmem.h"
#include "csr_wifi_router_prim.h"
#include "csr_wifi_router_lib.h"

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrWifiRouterFreeDownstreamMessageContents
 *
 *  DESCRIPTION
 *
 *
 *  PARAMETERS
 *      eventClass: only the value CSR_WIFI_ROUTER_PRIM will be handled
 *      message:    the message to free
 *----------------------------------------------------------------------------*/
void CsrWifiRouterFreeDownstreamMessageContents(u16 eventClass, void *message)
{
    if (eventClass != CSR_WIFI_ROUTER_PRIM)
    {
        return;
    }
    if (NULL == message)
    {
        return;
    }

    switch (*((CsrWifiRouterPrim *) message))
    {
        case CSR_WIFI_ROUTER_MA_PACKET_REQ:
        {
            CsrWifiRouterMaPacketReq *p = (CsrWifiRouterMaPacketReq *)message;
            kfree(p->frame);
            p->frame = NULL;
            break;
        }

        default:
            break;
    }
}


