
#include "p2p_precomp.h"



UINT_32
p2pCalculate_IEForAssocReq (

    IN P_ADAPTER_T prAdapter,
    IN ENUM_NETWORK_TYPE_INDEX_T eNetTypeIndex,
    IN P_STA_RECORD_T prStaRec
    )
{
    P_P2P_FSM_INFO_T prP2pFsmInfo = (P_P2P_FSM_INFO_T)NULL;
    P_P2P_CONNECTION_REQ_INFO_T prConnReqInfo = (P_P2P_CONNECTION_REQ_INFO_T)NULL;
    UINT_32 u4RetValue = 0;

    do {
        ASSERT_BREAK((eNetTypeIndex == NETWORK_TYPE_P2P_INDEX) && (prAdapter != NULL));

        prP2pFsmInfo = prAdapter->rWifiVar.prP2pFsmInfo;

        prConnReqInfo = &(prP2pFsmInfo->rConnReqInfo);

        u4RetValue = prConnReqInfo->u4BufLength;

    }
while (FALSE);

    return u4RetValue;
} /* p2pCalculate_IEForAssocReq */



/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to generate P2P IE for Beacon frame.
*
* @param[in] prMsduInfo             Pointer to the composed MSDU_INFO_T.
*
* @return none
*/
/*----------------------------------------------------------------------------*/
VOID
p2pGenerate_IEForAssocReq (
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo
    )
{
    P_P2P_FSM_INFO_T prP2pFsmInfo = (P_P2P_FSM_INFO_T)NULL;
    P_P2P_CONNECTION_REQ_INFO_T prConnReqInfo = (P_P2P_CONNECTION_REQ_INFO_T)NULL;
    PUINT_8 pucIEBuf = (PUINT_8)NULL;

    do {
        ASSERT_BREAK((prAdapter != NULL) && (prMsduInfo != NULL));

        prP2pFsmInfo = prAdapter->rWifiVar.prP2pFsmInfo;

        prConnReqInfo = &(prP2pFsmInfo->rConnReqInfo);

        pucIEBuf = (PUINT_8)((UINT_32)prMsduInfo->prPacket + (UINT_32)prMsduInfo->u2FrameLength);

        kalMemCopy(pucIEBuf, prConnReqInfo->aucIEBuf, prConnReqInfo->u4BufLength);

        prMsduInfo->u2FrameLength += prConnReqInfo->u4BufLength;

    } while (FALSE);

    return;

} /* p2pGenerate_IEForAssocReq */


