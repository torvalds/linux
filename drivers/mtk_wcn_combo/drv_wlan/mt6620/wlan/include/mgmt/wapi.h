/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/mgmt/wapi.h#1 $
*/

/*! \file  wapi.h
    \brief  The wapi related define, macro and structure are described here.
*/



/*
** $Log: wapi.h $
 *
 * 07 20 2010 wh.su
 * 
 * .
 *
 * 01 27 2010 wh.su
 * [BORA00000476][Wi-Fi][firmware] Add the security module initialize code
 * add and fixed some security function.
 *
 * Dec 8 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * change the wapi function name and adding the generate wapi ie function
 *
 * Dec 7 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 * adding some wapi structure define
 *
 * Nov 23 2009 mtk01088
 * [BORA00000476] [Wi-Fi][firmware] Add the security module initialize code
 *
**  \main\maintrunk.MT5921\1 2009-10-09 17:06:29 GMT mtk01088
**
*/

#ifndef _WAPI_H
#define _WAPI_H

#if CFG_SUPPORT_WAPI

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

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
#define WAPI_CIPHER_SUITE_WPI           0x01721400 /* WPI_SMS4 */
#define WAPI_AKM_SUITE_802_1X           0x01721400 /* WAI */
#define WAPI_AKM_SUITE_PSK              0x02721400 /* WAI_PSK */

#define ELEM_ID_WAPI                    68 /* WAPI IE */

#define WAPI_IE(fp)                     ((P_WAPI_INFO_ELEM_T) fp)


/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

VOID
wapiGenerateWAPIIE(
    IN P_ADAPTER_T          prAdapter,
    IN P_MSDU_INFO_T        prMsduInfo
    );

BOOLEAN
wapiParseWapiIE (
    IN  P_WAPI_INFO_ELEM_T  prInfoElem,
    OUT P_WAPI_INFO_T       prWapiInfo
    );

BOOLEAN
wapiPerformPolicySelection(
    IN P_ADAPTER_T          prAdapter,
    IN P_BSS_DESC_T         prBss
    );

//BOOLEAN
//wapiUpdateTxKeyIdx (
//    IN  P_STA_RECORD_T     prStaRec,
//    IN  UINT_8             ucWlanIdx
//    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#endif
#endif /* _WAPI_H */

