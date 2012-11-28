/*
** $Id: @(#) p2p_assoc.c@@
*/

/*! \file   "p2p_assoc.c"
    \brief  This file includes the Wi-Fi Direct association-related functions.

    This file includes the association-related functions.
*/




/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#include "precomp.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

/*----------------------------------------------------------------------------*/
/*!
* @brief This function is used to compose Common Information Elements for P2P Association
*        Request Frame.
*
* @param[in] prMsduInfo     Pointer to the composed MSDU_INFO_T.
*
* @return (none)
*/
/*----------------------------------------------------------------------------*/
PUINT_8
p2pBuildReAssocReqFrameCommonIEs(
    IN P_ADAPTER_T prAdapter,
    IN P_MSDU_INFO_T prMsduInfo,
    IN PUINT_8 pucBuffer
    )
{
    P_P2P_CONNECTION_SETTINGS_T prP2pConnSettings = (P_P2P_CONNECTION_SETTINGS_T)NULL;

    prP2pConnSettings = prAdapter->rWifiVar.prP2PConnSettings;

    /* Fill the SSID element. */
    SSID_IE(pucBuffer)->ucId = ELEM_ID_SSID;

    /* NOTE(Kevin): We copy the SSID from CONNECTION_SETTINGS for the case of
     * Passive Scan and the target BSS didn't broadcast SSID on its Beacon Frame.
     */

    COPY_SSID(SSID_IE(pucBuffer)->aucSSID,
              SSID_IE(pucBuffer)->ucLength,
              prP2pConnSettings->aucSSID,
              prP2pConnSettings->ucSSIDLen);

    prMsduInfo->u2FrameLength += IE_SIZE(pucBuffer);
    pucBuffer += IE_SIZE(pucBuffer);
    return pucBuffer;
}


