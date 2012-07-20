/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */
#include <linux/slab.h>
#include "csr_wifi_nme_ap_prim.h"
#include "csr_wifi_nme_ap_lib.h"

/*----------------------------------------------------------------------------*
 *  NAME
 *      CsrWifiNmeApFreeDownstreamMessageContents
 *
 *  DESCRIPTION
 *
 *
 *  PARAMETERS
 *      eventClass: only the value CSR_WIFI_NME_AP_PRIM will be handled
 *      message:    the message to free
 *----------------------------------------------------------------------------*/
void CsrWifiNmeApFreeDownstreamMessageContents(u16 eventClass, void *message)
{
    if (eventClass != CSR_WIFI_NME_AP_PRIM)
    {
        return;
    }
    if (NULL == message)
    {
        return;
    }

    switch (*((CsrWifiNmeApPrim *) message))
    {
        case CSR_WIFI_NME_AP_CONFIG_SET_REQ:
        {
            CsrWifiNmeApConfigSetReq *p = (CsrWifiNmeApConfigSetReq *)message;
            kfree(p->apMacConfig.macAddressList);
            p->apMacConfig.macAddressList = NULL;
            break;
        }
        case CSR_WIFI_NME_AP_START_REQ:
        {
            CsrWifiNmeApStartReq *p = (CsrWifiNmeApStartReq *)message;
            switch (p->apCredentials.authType)
            {
                case CSR_WIFI_SME_AP_AUTH_TYPE_PERSONAL:
                    switch (p->apCredentials.nmeAuthType.authTypePersonal.pskOrPassphrase)
                    {
                        case CSR_WIFI_NME_AP_CREDENTIAL_TYPE_PASSPHRASE:
                            kfree(p->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.passphrase.passphrase);
                            p->apCredentials.nmeAuthType.authTypePersonal.authPers_credentials.passphrase.passphrase = NULL;
                            break;
                        default:
                            break;
                    }
                    break;
                default:
                    break;
            }
            {
                u16 i3;
                for (i3 = 0; i3 < p->p2pGoParam.operatingChanList.channelEntryListCount; i3++)
                {
                    kfree(p->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannel);
                    p->p2pGoParam.operatingChanList.channelEntryList[i3].operatingChannel = NULL;
                }
            }
            kfree(p->p2pGoParam.operatingChanList.channelEntryList);
            p->p2pGoParam.operatingChanList.channelEntryList = NULL;
            break;
        }

        default:
            break;
    }
}


