/*****************************************************************************

            (c) Cambridge Silicon Radio Limited 2012
            All rights reserved and confidential information of CSR

            Refer to LICENSE.txt included with this source for details
            on the license terms.

*****************************************************************************/

/* Note: this is an auto-generated file. */

#include "csr_msgconv.h"
#include "csr_pmem.h"
#include "csr_macro.h"


#ifdef CSR_LOG_ENABLE
#include "csr_log.h"
#endif

#ifndef EXCLUDE_CSR_WIFI_ROUTER_MODULE
#include "csr_wifi_router_serialize.h"
#include "csr_wifi_router_prim.h"

static CsrMsgConvMsgEntry csrwifirouter_conv_lut[] = {
    { CSR_WIFI_ROUTER_MA_PACKET_SUBSCRIBE_REQ, CsrWifiRouterMaPacketSubscribeReqSizeof, CsrWifiRouterMaPacketSubscribeReqSer, CsrWifiRouterMaPacketSubscribeReqDes, CsrWifiRouterMaPacketSubscribeReqSerFree },
    { CSR_WIFI_ROUTER_MA_PACKET_UNSUBSCRIBE_REQ, CsrWifiRouterMaPacketUnsubscribeReqSizeof, CsrWifiRouterMaPacketUnsubscribeReqSer, CsrWifiRouterMaPacketUnsubscribeReqDes, CsrWifiRouterMaPacketUnsubscribeReqSerFree },
    { CSR_WIFI_ROUTER_MA_PACKET_REQ, CsrWifiRouterMaPacketReqSizeof, CsrWifiRouterMaPacketReqSer, CsrWifiRouterMaPacketReqDes, CsrWifiRouterMaPacketReqSerFree },
    { CSR_WIFI_ROUTER_MA_PACKET_RES, CsrWifiRouterMaPacketResSizeof, CsrWifiRouterMaPacketResSer, CsrWifiRouterMaPacketResDes, CsrWifiRouterMaPacketResSerFree },
    { CSR_WIFI_ROUTER_MA_PACKET_CANCEL_REQ, CsrWifiRouterMaPacketCancelReqSizeof, CsrWifiRouterMaPacketCancelReqSer, CsrWifiRouterMaPacketCancelReqDes, CsrWifiRouterMaPacketCancelReqSerFree },
    { CSR_WIFI_ROUTER_MA_PACKET_SUBSCRIBE_CFM, CsrWifiRouterMaPacketSubscribeCfmSizeof, CsrWifiRouterMaPacketSubscribeCfmSer, CsrWifiRouterMaPacketSubscribeCfmDes, CsrWifiRouterMaPacketSubscribeCfmSerFree },
    { CSR_WIFI_ROUTER_MA_PACKET_UNSUBSCRIBE_CFM, CsrWifiRouterMaPacketUnsubscribeCfmSizeof, CsrWifiRouterMaPacketUnsubscribeCfmSer, CsrWifiRouterMaPacketUnsubscribeCfmDes, CsrWifiRouterMaPacketUnsubscribeCfmSerFree },
    { CSR_WIFI_ROUTER_MA_PACKET_CFM, CsrWifiRouterMaPacketCfmSizeof, CsrWifiRouterMaPacketCfmSer, CsrWifiRouterMaPacketCfmDes, CsrWifiRouterMaPacketCfmSerFree },
    { CSR_WIFI_ROUTER_MA_PACKET_IND, CsrWifiRouterMaPacketIndSizeof, CsrWifiRouterMaPacketIndSer, CsrWifiRouterMaPacketIndDes, CsrWifiRouterMaPacketIndSerFree },

    { 0, NULL, NULL, NULL, NULL },
};

CsrMsgConvMsgEntry* CsrWifiRouterConverterLookup(CsrMsgConvMsgEntry *ce, u16 msgType)
{
    if (msgType & CSR_PRIM_UPSTREAM)
    {
        u16 idx = (msgType & ~CSR_PRIM_UPSTREAM) + CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_COUNT;
        if (idx < (CSR_WIFI_ROUTER_PRIM_UPSTREAM_COUNT + CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_COUNT) &&
            csrwifirouter_conv_lut[idx].msgType == msgType)
        {
            return &csrwifirouter_conv_lut[idx];
        }
    }
    else
    {
        if (msgType < CSR_WIFI_ROUTER_PRIM_DOWNSTREAM_COUNT &&
            csrwifirouter_conv_lut[msgType].msgType == msgType)
        {
            return &csrwifirouter_conv_lut[msgType];
        }
    }
    return NULL;
}


void CsrWifiRouterConverterInit(void)
{
    CsrMsgConvInsert(CSR_WIFI_ROUTER_PRIM, csrwifirouter_conv_lut);
    CsrMsgConvCustomLookupRegister(CSR_WIFI_ROUTER_PRIM, CsrWifiRouterConverterLookup);
}


#ifdef CSR_LOG_ENABLE
static const CsrLogPrimitiveInformation csrwifirouter_conv_info = {
    CSR_WIFI_ROUTER_PRIM,
    (char *)"CSR_WIFI_ROUTER_PRIM",
    csrwifirouter_conv_lut
};
const CsrLogPrimitiveInformation* CsrWifiRouterTechInfoGet(void)
{
    return &csrwifirouter_conv_info;
}


#endif /* CSR_LOG_ENABLE */
#endif /* EXCLUDE_CSR_WIFI_ROUTER_MODULE */
