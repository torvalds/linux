/*****************************************************************************

  FILE: csr_wifi_nme_sef.c

  (c) Cambridge Silicon Radio Limited 2010

  Refer to LICENSE.txt included with this source for details
  on the license terms.

 *****************************************************************************/
#include "csr_wifi_nme_ap_sef.h"
#include "unifi_priv.h"

void CsrWifiNmeApUpstreamStateHandlers(void* drvpriv, CsrWifiFsmEvent* msg)
{
    switch(msg->type) {
        case CSR_WIFI_NME_AP_START_CFM:
            CsrWifiNmeApStartCfmHandler(drvpriv, msg);
            break;
        case CSR_WIFI_NME_AP_STOP_CFM:
            CsrWifiNmeApStopCfmHandler(drvpriv, msg);
            break;
        case CSR_WIFI_NME_AP_CONFIG_SET_CFM:
            CsrWifiNmeApConfigSetCfmHandler(drvpriv,msg);
            break;
        default:
	    unifi_error(drvpriv, "CsrWifiNmeApUpstreamStateHandlers: unhandled NME_AP message type 0x%.4X\n",msg->type);
            break;
    }
}
