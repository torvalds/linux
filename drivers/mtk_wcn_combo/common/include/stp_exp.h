/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/



#ifndef _STP_EXP_H_
#define _STP_EXP_H_

#include "osal_typedef.h"
#include "osal.h"

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#define BT_TASK_INDX        (0)
#define FM_TASK_INDX        (1)
#define GPS_TASK_INDX       (2)
#define WIFI_TASK_INDX      (3)
#define WMT_TASK_INDX       (4)
#define STP_TASK_INDX       (5)
#define INFO_TASK_INDX      (6)
#define MTKSTP_MAX_TASK_NUM (7)

#define MTKSTP_BUFFER_SIZE  (16384) //Size of RX Queue



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
typedef void (*MTK_WCN_STP_EVENT_CB)(void);
typedef INT32 (*MTK_WCN_STP_IF_TX)(const UINT8 *data, const UINT32 size, UINT32 *written_size);
/* export for HIF driver */
typedef void (*MTK_WCN_STP_IF_RX)(const UINT8 *data, INT32 size);

typedef enum {
    STP_UART_IF_TX = 0,
    STP_SDIO_IF_TX,
    STP_MAX_IF_TX
}ENUM_STP_TX_IF_TYPE;

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

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_receive_data
* DESCRIPTION
*  receive data from serial protocol engine
* PARAMETERS
*  buffer      [IN]        data buffer
*  length      [IN]        data buffer length
* RETURNS
*  INT32    >= 0: size of data received; < 0: error
*****************************************************************************/
extern INT32 mtk_wcn_stp_receive_data(UINT8 *buffer, UINT32 length, UINT8 type);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_send_data
* DESCRIPTION
*  subfunction send data through STP
* PARAMETERS
*  buffer      [IN]        data buffer
*  length      [IN]        data buffer length
*  type        [IN]        subfunction type
* RETURNS
*  INT32    >= 0: length transmitted; < 0: error
*****************************************************************************/
extern INT32 mtk_wcn_stp_send_data(const UINT8 *buffer, const UINT32 length, const UINT8 type);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_is_rxqueue_empty
* DESCRIPTION
*  Is certain rx queue empty?
* PARAMETERS
*  type        [IN]        subfunction type
* RETURNS
*  INT32    0: queue is NOT empyt; !0: queue is empty
*****************************************************************************/
extern MTK_WCN_BOOL mtk_wcn_stp_is_rxqueue_empty(UINT8 type);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_is_enable
* DESCRIPTION
*  Is STP ready?
* PARAMETERS
*  none.
* RETURNS
*  MTK_WCN_BOOL    TRUE:ready, FALSE:not ready
*****************************************************************************/
extern MTK_WCN_BOOL mtk_wcn_stp_is_ready(void);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_parser_data
* DESCRIPTION
*  push data to serial transport protocol parser engine
* PARAMETERS
*  buffer      [IN]        data buffer
*  length      [IN]        data buffer length
* RETURNS
*  void
*****************************************************************************/
extern INT32 mtk_wcn_stp_parser_data(UINT8 *buffer, UINT32 length);

/*****************************************************************************
* FUNCTION
*  set_bluetooth_rx_interface
* DESCRIPTION
*  Set bluetooth rx interface
* PARAMETERS
*  rx interface type
* RETURNS
*  void
*****************************************************************************/
extern void mtk_wcn_stp_set_bluez(MTK_WCN_BOOL sdio_flag);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_register_tx_event_cb
* DESCRIPTION
*  regiter Tx event callback function
* PARAMETERS
*  func
* RETURNS
*  INT32: 0:successful , -1: fail
*****************************************************************************/
extern INT32 mtk_wcn_stp_register_tx_event_cb(INT32 type, MTK_WCN_STP_EVENT_CB func);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_register_event_cb
* DESCRIPTION
*  regiter Rx event callback function
* PARAMETERS
*  func
* RETURNS
*  INT32: 0:successful , -1: fail
*****************************************************************************/
extern INT32 mtk_wcn_stp_register_event_cb(INT32 type, MTK_WCN_STP_EVENT_CB func);


/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_register_if_tx
* DESCRIPTION
*  regiter Tx event callback function
* PARAMETERS
*  stp_if: SDIO or UART, fnnc: Call back function
* RETURNS
*  INT32: 0:successful , -1: fail
*****************************************************************************/
extern INT32 mtk_wcn_stp_register_if_tx(ENUM_STP_TX_IF_TYPE stp_if, MTK_WCN_STP_IF_TX func);


/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_register_if_rx
* DESCRIPTION
*  regiter Rx event callback function
* PARAMETERS
*  stp_if: SDIO or UART, fnnc: Call back function
* RETURNS
*  INT32: 0:successful , -1: fail
*****************************************************************************/
extern INT32 mtk_wcn_stp_register_if_rx(MTK_WCN_STP_IF_RX func);

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _WMT_EXP_H_ */













