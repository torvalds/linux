/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/mgmt/wlan_typedef.h#1 $
*/

/*! \file   wlan_typedef.h
    \brief  Declaration of data type and return values of internal protocol stack.

    In this file we declare the data type and return values which will be exported
    to all MGMT Protocol Stack.
*/



/*
** $Log: wlan_typedef.h $
*/

#ifndef _WLAN_TYPEDEF_H
#define _WLAN_TYPEDEF_H

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
/* Type definition for BSS_INFO_T structure, to describe the attributes used in a
 * common BSS.
 */
typedef struct _BSS_INFO_T      BSS_INFO_T, *P_BSS_INFO_T;

typedef BSS_INFO_T              AIS_BSS_INFO_T, *P_AIS_BSS_INFO_T;
typedef BSS_INFO_T              P2P_BSS_INFO_T, *P_P2P_BSS_INFO_T;
typedef BSS_INFO_T              BOW_BSS_INFO_T, *P_BOW_BSS_INFO_T;

typedef struct _AIS_SPECIFIC_BSS_INFO_T AIS_SPECIFIC_BSS_INFO_T, *P_AIS_SPECIFIC_BSS_INFO_T;
typedef struct _P2P_SPECIFIC_BSS_INFO_T P2P_SPECIFIC_BSS_INFO_T, *P_P2P_SPECIFIC_BSS_INFO_T;
typedef struct _BOW_SPECIFIC_BSS_INFO_T BOW_SPECIFIC_BSS_INFO_T, *P_BOW_SPECIFIC_BSS_INFO_T;
/* CFG_SUPPORT_WFD */
typedef struct _WFD_CFG_SETTINGS_T WFD_CFG_SETTINGS_T, *P_WFD_CFG_SETTINGS_T;


/* BSS related structures */
/* Type definition for BSS_DESC_T structure, to describe parameter sets of a particular BSS */
typedef struct _BSS_DESC_T      BSS_DESC_T, *P_BSS_DESC_T, **PP_BSS_DESC_T;

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
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
#endif /* _WLAN_TYPEDEF_H */


