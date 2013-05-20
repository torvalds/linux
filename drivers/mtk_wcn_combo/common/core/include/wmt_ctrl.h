/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/



#ifndef _WMT_CTRL_H_
#define _WMT_CTRL_H_

#include "osal.h"

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#define DWCNT_CTRL_DATA  (16)


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

typedef struct _WMT_CTRL_DATA_ {
    UINT32 ctrlId;
    UINT32 au4CtrlData[DWCNT_CTRL_DATA];
} WMT_CTRL_DATA, *P_WMT_CTRL_DATA;

typedef enum _ENUM_WMT_CTRL_T {
    WMT_CTRL_HW_PWR_OFF   = 0, /* whole chip power off */
    WMT_CTRL_HW_PWR_ON = 1, /* whole chip power on */
    WMT_CTRL_HW_RST = 2,/* whole chip power on */
    WMT_CTRL_STP_CLOSE = 3,
    WMT_CTRL_STP_OPEN = 4,
    WMT_CTRL_STP_CONF = 5,
    WMT_CTRL_FREE_PATCH = 6,
    WMT_CTRL_GET_PATCH = 7,
    WMT_CTRL_GET_PATCH_NAME = 8,
    WMT_CTRL_HOST_BAUDRATE_SET = 9,
    WMT_CTRL_SDIO_HW = 10, /* enable/disable SDIO1/2 of combo chip */
    WMT_CTRL_SDIO_FUNC = 11, /* probe/remove STP/Wi-Fi driver in SDIO1/2 of combo chip*/
    WMT_CTRL_HWIDVER_SET = 12, // TODO: rename this and add chip id information in addition to chip version
    WMT_CTRL_HWVER_GET = 13, // TODO: [FixMe][GeorgeKuo] remove unused functions
    WMT_CTRL_STP_RST = 14,
    WMT_CTRL_GET_WMT_CONF = 15,
    WMT_CTRL_TX = 16, /* [FixMe][GeorgeKuo]: to be removed by Sean's stp integration */
    WMT_CTRL_RX = 17, /* [FixMe][GeorgeKuo]: to be removed by Sean's stp integration */
    WMT_CTRL_RX_FLUSH = 18, /* [FixMe][SeanWang]: to be removed by Sean's stp integration */
    WMT_CTRL_GPS_SYNC_SET = 19,
    WMT_CTRL_GPS_LNA_SET = 20,
    WMT_CTRL_PATCH_SEARCH = 21,
    WMT_CTRL_CRYSTAL_TRIMING_GET = 22,
    WMT_CTRL_CRYSTAL_TRIMING_PUT = 23,
    WMT_CTRL_HW_STATE_DUMP = 24,
    WMT_CTRL_GET_PATCH_NUM = 25,
    WMT_CTRL_GET_PATCH_INFO = 26,
    WMT_CTRL_MAX
} ENUM_WMT_CTRL_T, *P_ENUM_WMT_CTRL_T;

typedef INT32 (*WMT_CTRL_FUNC)(P_WMT_CTRL_DATA);

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

extern INT32
wmt_ctrl (
    P_WMT_CTRL_DATA pWmtCtrlData
    );

extern INT32
wmt_ctrl_tx_ex (
    const UINT8 *pData,
    const UINT32 size,
    UINT32 *writtenSize,
    const MTK_WCN_BOOL bRawFlag
    );


/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/



#endif /* _WMT_CTRL_H_ */








