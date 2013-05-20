/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/mgmt/rate.h#1 $
*/

/*! \file  rate.h
    \brief This file contains the rate utility function of 
           IEEE 802.11 family for MediaTek 802.11 Wireless LAN Adapters.
*/



/*
** $Log: rate.h $
 *
 * 09 03 2010 kevin.huang
 * NULL
 * Refine #include sequence and solve recursive/nested #include issue
 *
 * 07 08 2010 cp.wu
 * 
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 10 2010 cp.wu
 * [WPD00003833][MT6620 and MT5931] Driver migration 
 * add buildable & linkable ais_fsm.c
 * 
 * related reference are still waiting to be resolved
 *
*/

#ifndef _RATE_H
#define _RATE_H

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
*                         D A T A   T Y P E S
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
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
/*----------------------------------------------------------------------------*/
/* Routines in rate.c                                                         */
/*----------------------------------------------------------------------------*/
VOID
rateGetRateSetFromIEs (
    IN P_IE_SUPPORTED_RATE_T prIeSupportedRate,
    IN P_IE_EXT_SUPPORTED_RATE_T prIeExtSupportedRate,
    OUT PUINT_16 pu2OperationalRateSet,
    OUT PUINT_16 pu2BSSBasicRateSet,
    OUT PBOOLEAN pfgIsUnknownBSSBasicRate
    );

VOID
rateGetDataRatesFromRateSet (
    IN UINT_16 u2OperationalRateSet,
    IN UINT_16 u2BSSBasicRateSet,
    OUT PUINT_8 pucDataRates,
    OUT PUINT_8 pucDataRatesLen
    );

BOOLEAN
rateGetHighestRateIndexFromRateSet (
    IN UINT_16 u2RateSet,
    OUT PUINT_8 pucHighestRateIndex
    );

BOOLEAN
rateGetLowestRateIndexFromRateSet (
    IN UINT_16 u2RateSet,
    OUT PUINT_8 pucLowestRateIndex
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _RATE_H */

