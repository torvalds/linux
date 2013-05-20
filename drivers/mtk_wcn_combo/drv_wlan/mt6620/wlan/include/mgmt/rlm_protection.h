/*
** $Id: //Department/DaVinci/BRANCHES/MT6620_WIFI_DRIVER_V2_3/include/mgmt/rlm_protection.h#1 $
*/

/*! \file   "rlm_protection.h"
    \brief
*/



/*
** $Log: rlm_protection.h $
 *
 * 08 20 2010 cm.chang
 * NULL
 * Migrate RLM code to host from FW
 *
 * 07 08 2010 cp.wu
 *
 * [WPD00003833] [MT6620 and MT5931] Driver migration - move to new repository.
 *
 * 06 28 2010 cm.chang
 * [WPD00003841][LITE Driver] Migrate RLM/CNM to host driver
 * 1st draft code for RLM module
 *
 * 04 22 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * First draft code to support protection in AP mode
 *
 * 02 13 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support PCO in STA mode
 *
 * 02 12 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Use bss info array for concurrent handle
 *
 * 01 25 2010 cm.chang
 * [BORA00000018]Integrate WIFI part into BORA for the 1st time
 * Support protection and bandwidth switch
*/

#ifndef _RLM_PROTECTION_H
#define _RLM_PROTECTION_H

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
typedef enum _ENUM_SYS_PROTECT_MODE_T {
    SYS_PROTECT_MODE_NONE = 0,      /* Mode 0 */
    SYS_PROTECT_MODE_ERP,           /* Mode 1 */
    SYS_PROTECT_MODE_NON_HT,        /* Mode 2 */
    SYS_PROTECT_MODE_20M,           /* Mode 3 */

    SYS_PROTECT_MODE_NUM
} ENUM_SYS_PROTECT_MODE_T, *P_ENUM_SYS_PROTECT_MODE_T;

/* This definition follows HT Protection field of HT Operation IE */
typedef enum _ENUM_HT_PROTECT_MODE_T {
    HT_PROTECT_MODE_NONE = 0,
    HT_PROTECT_MODE_NON_MEMBER,
    HT_PROTECT_MODE_20M,
    HT_PROTECT_MODE_NON_HT,

    HT_PROTECT_MODE_NUM
} ENUM_HT_PROTECT_MODE_T, *P_ENUM_HT_PROTECT_MODE_T;


typedef enum _ENUM_GF_MODE_T {
    GF_MODE_NORMAL = 0,
    GF_MODE_PROTECT,
    GF_MODE_DISALLOWED,

    GF_MODE_NUM
} ENUM_GF_MODE_T, *P_ENUM_GF_MODE_T;

typedef enum _ENUM_RIFS_MODE_T {
    RIFS_MODE_NORMAL = 0,
    RIFS_MODE_DISALLOWED,

    RIFS_MODE_NUM
} ENUM_RIFS_MODE_T, *P_ENUM_RIFS_MODE_T;

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

#endif /* _RLM_PROTECTION_H */

