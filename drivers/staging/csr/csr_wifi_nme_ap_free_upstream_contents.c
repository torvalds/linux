/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#include "csr_wifi_nme_ap_prim.h"
#include "csr_wifi_nme_ap_lib.h"

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrWifiNmeApFreeUpstreamMessageContents
 *
 *  DESCRIPTION
 *
 *
 *  PARAMETERS
 *      eventClass: only the value CSR_WIFI_NME_AP_PRIM will be handled
 *      message:    the message to free
 *----------------------------------------------------------------------------*/
void CsrWifiNmeApFreeUpstreamMessageContents(u16 eventClass, void *message)
{
    if (eventClass != CSR_WIFI_NME_AP_PRIM)
    {
        return;
    }
    if (NULL == message)
    {
        return;
    }
}


