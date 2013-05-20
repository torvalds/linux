/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/



#ifndef _WMT_IC_H_
#define _WMT_IC_H_

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/

#include "wmt_core.h"
#include "wmt_exp.h"

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

#define WMT_IC_NAME_MT6620 "MT6620"
#define WMT_IC_NAME_MT6628 "MT6628"


#define WMT_IC_VER_E1 "E1"
#define WMT_IC_VER_E2 "E2"
#define WMT_IC_VER_E3 "E3"
#define WMT_IC_VER_E4 "E4"
#define WMT_IC_VER_E5 "E5"
#define WMT_IC_VER_E6 "E6"
#define WMT_IC_VER_E7 "E7"

#define WMT_IC_PATCH_DUMMY_EXT "_ex"
#define WMT_IC_PATCH_NO_EXT ""
#define WMT_IC_PATCH_E1_EXT "_e1"
#define WMT_IC_PATCH_E2_EXT "_e2"
#define WMT_IC_PATCH_E3_EXT "_e3"
#define WMT_IC_PATCH_E4_EXT "_e4"
#define WMT_IC_PATCH_E5_EXT "_e5"
#define WMT_IC_PATCH_E6_EXT "_e6"

#define WMT_IC_PATCH_TAIL    "_hdr.bin"

#define WMT_IC_INVALID_CHIP_ID 0xFFFF

#define MAJORNUM(x) (x & 0x00F0)
#define MINORNUM(x) (x & 0x000F)

/*******************************************************************************
*                             R E G I S T E R   M A P
********************************************************************************
*/
/* General definition used for ALL/UNKNOWN CHIPS */
/* Now MT6620 uses these definitions */
#define GEN_CONFG_BASE (0x80000000UL)
#define GEN_HVR (GEN_CONFG_BASE + 0x0UL) /* HW_VER */
#define GEN_FVR (GEN_CONFG_BASE + 0x4UL) /* FW_VER */
#define GEN_VER_MASK (0x0000FFFFUL) /* HW_VER and FW_VER valid bits mask */
#define GEN_HCR (GEN_CONFG_BASE + 0x8UL) /* HW_CODE, chip id */
#define GEN_HCR_MASK (0x0000FFFFUL) /* HW_CODE valid bits mask */

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

typedef struct _WMT_IC_INFO_S
{
    UINT32 u4HwVer; /* u4HwId */
    PUINT8 cChipName;
    PUINT8 cChipVersion;
    PUINT8 cPatchNameExt;
    MTK_WCN_BOOL bPsmSupport;
    MTK_WCN_BOOL bWorkWithoutPatch;
    ENUM_WMTHWVER_TYPE_T eWmtHwVer;
} WMT_IC_INFO_S, *P_WMT_IC_INFO_S;

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
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

#endif /* _WMT_IC_H_ */

