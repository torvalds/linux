/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/



#ifndef _WMT_EXP_H_
#define _WMT_EXP_H_

#include <mach/mtk_wcn_cmb_stub.h>
#include "osal.h"
//not to reference to internal wmt
//#include "wmt_core.h"
/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
#if 1 /* moved from wmt_lib.h */
#ifndef DFT_TAG
#define DFT_TAG         "[WMT-DFT]"
#endif

#define WMT_LOUD_FUNC(fmt, arg...)   if (gWmtDbgLvl >= WMT_LOG_LOUD) { osal_dbg_print(DFT_TAG "[L]%s:"  fmt, __FUNCTION__ ,##arg);}
#define WMT_INFO_FUNC(fmt, arg...)   if (gWmtDbgLvl >= WMT_LOG_INFO) { osal_info_print(DFT_TAG "[I]%s:"  fmt, __FUNCTION__ ,##arg);}
#define WMT_WARN_FUNC(fmt, arg...)   if (gWmtDbgLvl >= WMT_LOG_WARN) { osal_warn_print(DFT_TAG "[W]%s:"  fmt, __FUNCTION__ ,##arg);}
#define WMT_ERR_FUNC(fmt, arg...)    if (gWmtDbgLvl >= WMT_LOG_ERR) { osal_err_print(DFT_TAG "[E]%s(%d):"  fmt, __FUNCTION__ , __LINE__, ##arg);}
#define WMT_DBG_FUNC(fmt, arg...)    if (gWmtDbgLvl >= WMT_LOG_DBG) { osal_dbg_print(DFT_TAG "[D]%s:"  fmt, __FUNCTION__ ,##arg);}
#define WMT_TRC_FUNC(f)              if (gWmtDbgLvl >= WMT_LOG_DBG) { osal_dbg_print(DFT_TAG "<%s> <%d>\n", __FUNCTION__, __LINE__);}
#endif

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#if 1 /* moved from wmt_lib.h */
extern UINT32 gWmtDbgLvl ;
#endif

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#if 1 /* moved from wmt_lib.h */
#define WMT_LOG_LOUD    4
#define WMT_LOG_DBG     3
#define WMT_LOG_INFO    2
#define WMT_LOG_WARN    1
#define WMT_LOG_ERR     0
#endif

#define CFG_WMT_PS_SUPPORT 1 /* moved from wmt_lib.h */
/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/
typedef enum _ENUM_WMTDRV_TYPE_T {
    WMTDRV_TYPE_BT = 0,
    WMTDRV_TYPE_FM = 1,
    WMTDRV_TYPE_GPS = 2,
    WMTDRV_TYPE_WIFI = 3,
    WMTDRV_TYPE_WMT = 4,
    WMTDRV_TYPE_STP = 5,
    WMTDRV_TYPE_SDIO1 = 6,
    WMTDRV_TYPE_SDIO2 = 7,
    WMTDRV_TYPE_LPBK = 8,
    WMTDRV_TYPE_COREDUMP = 9,
    WMTDRV_TYPE_MAX
} ENUM_WMTDRV_TYPE_T, *P_ENUM_WMTDRV_TYPE_T;

// TODO: [ChangeFeature][GeorgeKuo] Reconsider usage of this type
// TODO: how do we extend for new chip and newer revision?
// TODO: This way is hard to extend
typedef enum _ENUM_WMTHWVER_TYPE_T{
    WMTHWVER_MT6620_E1 = 0x0,
    WMTHWVER_MT6620_E2 = 0x1,
    WMTHWVER_MT6620_E3 = 0x2,
    WMTHWVER_MT6620_E4 = 0x3,
    WMTHWVER_MT6620_E5 = 0x4,
    WMTHWVER_MT6620_E6 = 0x5,
    WMTHWVER_MT6620_E7 = 0x6,
    WMTHWVER_MT6620_MAX,
    WMTHWVER_INVALID = 0xff
} ENUM_WMTHWVER_TYPE_T, *P_ENUM_WMTHWVER_TYPE_T;

typedef enum _ENUM_WMTCHIN_TYPE_T{
   WMTCHIN_CHIPID = 0x0,
   WMTCHIN_HWVER = WMTCHIN_CHIPID + 1,
   WMTCHIN_MAPPINGHWVER = WMTCHIN_HWVER + 1,
   WMTCHIN_FWVER = WMTCHIN_MAPPINGHWVER + 1,
   WMTCHIN_MAX,
   
}ENUM_WMT_CHIPINFO_TYPE_T, *P_ENUM_WMT_CHIPINFO_TYPE_T;

typedef enum _ENUM_WMTDSNS_TYPE_T{
    WMTDSNS_FM_DISABLE = 0,
    WMTDSNS_FM_ENABLE = 1,
    WMTDSNS_FM_GPS_DISABLE = 2,
    WMTDSNS_FM_GPS_ENABLE = 3,
    WMTDSNS_MAX
} ENUM_WMTDSNS_TYPE_T, *P_ENUM_WMTDSNS_TYPE_T;

typedef enum _ENUM_WMTTHERM_TYPE_T{
    WMTTHERM_ZERO = 0,
    WMTTHERM_ENABLE = WMTTHERM_ZERO + 1,
    WMTTHERM_READ = WMTTHERM_ENABLE + 1,
    WMTTHERM_DISABLE = WMTTHERM_READ + 1,
    WMTTHERM_MAX
}ENUM_WMTTHERM_TYPE_T, *P_ENUM_WMTTHERM_TYPE_T;

typedef enum _ENUM_WMTMSG_TYPE_T {
    WMTMSG_TYPE_POWER_ON = 0,
    WMTMSG_TYPE_POWER_OFF = 1,
    WMTMSG_TYPE_RESET = 2,
    WMTMSG_TYPE_STP_RDY= 3,
    WMTMSG_TYPE_HW_FUNC_ON= 4,
    WMTMSG_TYPE_MAX
} ENUM_WMTMSG_TYPE_T, *P_ENUM_WMTMSG_TYPE_T;

typedef enum _ENUM_WMTRSTMSG_TYPE_T{
    WMTRSTMSG_RESET_START = 0x0,
    WMTRSTMSG_RESET_END = 0x1,
    WMTRSTMSG_RESET_MAX,
    WMTRSTMSG_RESET_INVALID = 0xff
} ENUM_WMTRSTMSG_TYPE_T, *P_ENUM_WMTRSTMSG_TYPE_T;

typedef void (*PF_WMT_CB)(
    ENUM_WMTDRV_TYPE_T, /* Source driver type */
    ENUM_WMTDRV_TYPE_T, /* Destination driver type */
    ENUM_WMTMSG_TYPE_T, /* Message type */
    VOID *, /* READ-ONLY buffer. Buffer is allocated and freed by WMT_drv. Client
            can't touch this buffer after this function return. */
    UINT32 /* Buffer size in unit of byte */
);

typedef enum _SDIO_PS_OP{
    OWN_SET = 0,
    OWN_CLR = 1,
    OWN_STATE = 2,
} SDIO_PS_OP;


typedef INT32 (*PF_WMT_SDIO_PSOP)(SDIO_PS_OP);

#if 1 /* moved from wmt_core.h */
typedef enum {
    WMT_SDIO_SLOT_INVALID = 0,
    WMT_SDIO_SLOT_SDIO1 = 1, /* Wi-Fi dedicated SDIO1*/
    WMT_SDIO_SLOT_SDIO2 = 2,
    WMT_SDIO_SLOT_MAX
} WMT_SDIO_SLOT_NUM;

typedef enum {
    WMT_SDIO_FUNC_STP = 0,
    WMT_SDIO_FUNC_WIFI = 1,
    WMT_SDIO_FUNC_MAX
} WMT_SDIO_FUNC_TYPE;
#endif

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
/*subsystem function ctrl APIs*/
extern MTK_WCN_BOOL
mtk_wcn_wmt_func_off (
    ENUM_WMTDRV_TYPE_T type
    );

extern MTK_WCN_BOOL
mtk_wcn_wmt_func_on (
    ENUM_WMTDRV_TYPE_T type
    );

extern MTK_WCN_BOOL mtk_wcn_wmt_dsns_ctrl (
    ENUM_WMTDSNS_TYPE_T eType
    );

extern MTK_WCN_BOOL mtk_wcn_wmt_assert (
    VOID
    );

extern INT32 mtk_wcn_wmt_msgcb_reg (
    ENUM_WMTDRV_TYPE_T eType,
    PF_WMT_CB pCb
    );

extern INT32 mtk_wcn_wmt_msgcb_unreg (
    ENUM_WMTDRV_TYPE_T eType
    );

extern INT32
mtk_wcn_stp_wmt_sdio_op_reg (
    PF_WMT_SDIO_PSOP own_cb
    );

extern INT32 
mtk_wcn_stp_wmt_sdio_host_awake(
    VOID
    );
/*
return value:
enable/disable thermal sensor function: true(1)/false(0)
read thermal sensor function: thermal value

*/
extern INT8
mtk_wcn_wmt_therm_ctrl (
    ENUM_WMTTHERM_TYPE_T eType
    );

extern ENUM_WMTHWVER_TYPE_T
mtk_wcn_wmt_hwver_get (VOID);

extern INT32
mtk_wcn_wmt_chipid_query (VOID);


extern INT32 wmt_lib_set_aif (
    CMB_STUB_AIF_X aif,
    MTK_WCN_BOOL share
    ); /* set AUDIO interface options */
extern VOID
wmt_lib_ps_irq_cb(VOID);
/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _WMT_EXP_H_ */







