/*
* Copyright (C) 2011-2014 MediaTek Inc.
*
* This program is free software: you can redistribute it and/or modify it under the terms of the
* GNU General Public License version 2 as published by the Free Software Foundation.
*
* This program is distributed in the hope that it will be useful, but WITHOUT ANY WARRANTY;
* without even the implied warranty of MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
* See the GNU General Public License for more details.
*
* You should have received a copy of the GNU General Public License along with this program.
* If not, see <http://www.gnu.org/licenses/>.
*/

/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/

#ifndef _STP_CORE_H
#define _STP_CORE_H

#include "osal_typedef.h"
#include "osal.h"
#include "stp_exp.h"
#include "psm_core.h"
#include "btm_core.h"
#include "stp_btif.h"
/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/
#define CFG_STP_CORE_CTX_SPIN_LOCK (0)

#define WMT_LTE_COEX_FLAG (0x16)

/*configure using SPINLOCK or just mutex for STP-CORE tx*/
/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#define CONFIG_POWER_SAVING_SUPPORT

#ifdef PFX
#undef PFX
#endif
#define PFX                         "[STP] "

#define STP_LOG_DBG                  4
#define STP_LOG_PKHEAD               3
#define STP_LOG_INFO                 2
#define STP_LOG_WARN                 1
#define STP_LOG_ERR                  0

extern unsigned int gStpDbgLvl;

#define STP_DBG_FUNC(fmt, arg...)\
do { \
	if (gStpDbgLvl >= STP_LOG_DBG) \
		osal_dbg_print(PFX "%s: "  fmt, __func__ , ##arg); \
} while (0)
#define STP_INFO_FUNC(fmt, arg...) \
do { \
	if (gStpDbgLvl >= STP_LOG_INFO) \
		osal_dbg_print(PFX "%s:[I] "  fmt, __func__ , ##arg); \
} while (0)
#define STP_WARN_FUNC(fmt, arg...) \
do { \
	if (gStpDbgLvl >= STP_LOG_WARN) \
		osal_warn_print(PFX "%s:[W] "  fmt, __func__ , ##arg); \
} while (0)
#define STP_ERR_FUNC(fmt, arg...) \
do { \
	if (gStpDbgLvl >= STP_LOG_ERR) \
		osal_err_print(PFX "%s:[E] "   fmt, __func__ , ##arg); \
} while (0)
#define STP_TRC_FUNC(f) \
do { \
	if (gStpDbgLvl >= STP_LOG_DBG) \
		osal_dbg_print(PFX "<%s> <%d>\n", __func__, __LINE__); \
} while (0)

#define STP_DUMP_PACKET_HEAD(a, b, c) \
do { \
	if (gStpDbgLvl >= STP_LOG_PKHEAD) \
		stp_dump_data(a, b, c); \
} while (0)
#define STP_TRACE_FUNC(fmt, arg...) \
do { \
	if (gStpDbgLvl >= STP_LOG_DBG) \
		osal_dbg_print(PFX "%s: "  fmt, __func__ , ##arg); \
} while (0)

#define STP_MODE_BIT(x) (0x1UL << x)
#define MTKSTP_UART_FULL_MODE	  STP_MODE_BIT(0)
#define MTKSTP_UART_MAND_MODE	  STP_MODE_BIT(1)
#define MTKSTP_BTIF_FULL_MODE	  STP_MODE_BIT(2)
#define MTKSTP_BTIF_MAND_MODE	  STP_MODE_BIT(3)
#define MTKSTP_SDIO_MODE          STP_MODE_BIT(4)

#define MTKSTP_BUFFER_SIZE  (16384)

/*To check function driver's status by the the interface*/
/*Operation definition*/
#define OP_FUNCTION_ACTIVE         0

/*Driver's status*/
#define STATUS_OP_INVALID          0
#define STATUS_FUNCTION_INVALID    1

#define STATUS_FUNCTION_ACTIVE     31
#define STATUS_FUNCTION_INACTIVE   32

#define MTKSTP_CRC_SIZE     (2)
#define MTKSTP_HEADER_SIZE  (4)
#define MTKSTP_SEQ_SIZE     (8)

/*#define MTKSTP_WINSIZE      (4)*/
#define MTKSTP_WINSIZE      (7)
#define MTKSTP_TX_TIMEOUT   (180)	/*TODO: Baudrate to decide this */
#define MTKSTP_RETRY_LIMIT  (10)

#define INDEX_INC(idx)  \
{                       \
	idx++;              \
	idx &= 0x7;         \
}

#define INDEX_DEC(idx)  \
{                       \
	idx--;              \
	idx &= 0x7;         \
}

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
typedef INT32(*IF_TX) (const PUINT8 data, const UINT32 size, PUINT32 written_size);
/* event/signal */
typedef INT32(*EVENT_SET) (UINT8 function_type);
typedef INT32(*EVENT_TX_RESUME) (UINT8 winspace);
typedef INT32(*FUNCTION_STATUS) (UINT8 type, UINT8 op);
typedef INT32(*WMT_NOTIFY_FUNC_T) (UINT32 action);
typedef INT32(*BTM_NOTIFY_WMT_FUNC_T) (INT32);

#if CFG_STP_CORE_CTX_SPIN_LOCK
typedef OSAL_UNSLEEPABLE_LOCK STP_CTX_LOCK, *PSTP_CTX_LOCK;
#else
typedef OSAL_SLEEPABLE_LOCK STP_CTX_LOCK, *PSTP_CTX_LOCK;
#endif

typedef struct {
	/* common interface */
	IF_TX cb_if_tx;
	/* event/signal */
	EVENT_SET cb_event_set;
	EVENT_TX_RESUME cb_event_tx_resume;
	FUNCTION_STATUS cb_check_funciton_status;
} mtkstp_callback;

typedef enum {
	MTKSTP_SYNC = 0,
	MTKSTP_SEQ,
	MTKSTP_ACK,
	MTKSTP_NAK,
	MTKSTP_TYPE,
	MTKSTP_LENGTH,
	MTKSTP_CHECKSUM,
	MTKSTP_DATA,
	MTKSTP_CRC1,
	MTKSTP_CRC2,
	MTKSTP_RESYNC1,
	MTKSTP_RESYNC2,
	MTKSTP_RESYNC3,
	MTKSTP_RESYNC4,
	MTKSTP_FW_MSG,
} mtkstp_parser_state;

typedef struct {
	mtkstp_parser_state state;
	UINT8 seq;
	UINT8 ack;
	UINT8 nak;
	UINT8 type;
	UINT16 length;
	UINT8 checksum;
	UINT16 crc;
#if 1
	UINT8 wmtsubtype;
#endif
} mtkstp_parser_context_struct;

typedef struct {
	UINT8 txseq;		/* last tx pkt's seq + 1 */
	UINT8 txack;		/* last tx pkt's ack */
	UINT8 rxack;		/* last rx pkt's ack */
	UINT8 winspace;		/* current sliding window size */
	UINT8 expected_rxseq;	/* last rx pkt's seq + 1 */
	UINT8 retry_times;
} mtkstp_sequence_context_struct;

typedef struct {
	/* MTK_WCN_MUTEX           mtx; */
	OSAL_UNSLEEPABLE_LOCK mtx;
	UINT8 buffer[MTKSTP_BUFFER_SIZE];
	UINT32 read_p;
	UINT32 write_p;
} mtkstp_ring_buffer_struct;

typedef struct {
	UINT8 inband_rst_set;
	UINT32 rx_counter;	/* size of current processing pkt in rx_buf[] */
	UINT8 rx_buf[MTKSTP_BUFFER_SIZE];	/* input buffer of STP, room for current processing pkt */
	UINT32 tx_read;		/* read ptr of tx_buf[] */
	UINT32 tx_write;	/* write ptr of tx_buf[] */
	UINT8 tx_buf[MTKSTP_BUFFER_SIZE];	/* output buffer of STP */
	UINT32 tx_start_addr[MTKSTP_SEQ_SIZE];	/* ptr of each pkt in tx_buf[] */
	UINT32 tx_length[MTKSTP_SEQ_SIZE];	/* length of each pkt in tx_buf[] */
	mtkstp_ring_buffer_struct ring[MTKSTP_MAX_TASK_NUM];	/* ring buffers for each function driver */
	mtkstp_parser_context_struct parser;	/* current rx pkt's content */
	mtkstp_sequence_context_struct sequence;	/* state machine's current status */
	/* MTK_WCN_MUTEX stp_mutex; */
	/* OSAL_UNSLEEPABLE_LOCK stp_mutex; */
	STP_CTX_LOCK stp_mutex;
	/* MTK_WCN_TIMER tx_timer; // timer for tx timeout handling */
	OSAL_TIMER tx_timer;

	MTKSTP_PSM_T *psm;
	MTKSTP_BTM_T *btm;
	UINT8 f_enable;		/* default disabled */
	UINT8 f_ready;		/* default non-ready */
	UINT8 f_pending_type;
	UINT8 f_coredump;	/*block tx flag, for now, only when f/w assert happens, we will set this bit on */
	UINT8 en_coredump;
	/* Flag to identify Blueztooth is Bluez/or MTK Stack */
	MTK_WCN_BOOL f_bluez;
	MTK_WCN_BOOL f_dbg_en;
	MTK_WCN_BOOL f_autorst_en;

	/* Flag to identify STP by SDIO or UART */
	UINT32 f_mode;

	/* Flag to indicate the last WMT CLOSE */
	UINT32 f_wmt_last_close;

	/* Flag to indicate evt err has triggered assert or not */
	UINT32 f_evt_err_assert;
} mtkstp_context_struct;

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

INT32 stp_send_data_no_ps(UINT8 *buffer, UINT32 length, UINT8 type);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_init
* DESCRIPTION
*  init STP kernel
* PARAMETERS
*  cb_func      [IN] function pointers of system APIs
* RETURNS
*  INT32    0 = success, others = failure
*****************************************************************************/
extern INT32 mtk_wcn_stp_init(const mtkstp_callback * const cb_func);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_deinit
* DESCRIPTION
*  deinit STP kernel
* PARAMETERS
*  void
* RETURNS
*  INT32    0 = success, others = failure
*****************************************************************************/
extern INT32 mtk_wcn_stp_deinit(void);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_enable
* DESCRIPTION
*  enable/disable STP
* PARAMETERS
*  value        [IN]        0 = disable, others = enable
* RETURNS
*  INT32    0 = success, others = error
*****************************************************************************/
extern INT32 mtk_wcn_stp_enable(INT32 value);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_ready
* DESCRIPTION
*  ready/non-ready STP
* PARAMETERS
*  value        [IN]        0 = non-ready, others = ready
* RETURNS
*  INT32    0 = success, others = error
*****************************************************************************/
extern INT32 mtk_wcn_stp_ready(INT32 value);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_coredump_start_ctrl
* DESCRIPTION
*  set f/w assert flag in STP context
* PARAMETERS
*  value        [IN]        0=assert end, others=assert begins
* RETURNS
*  INT32    0=success, others=error
*****************************************************************************/
extern INT32 mtk_wcn_stp_coredump_start_ctrl(UINT32 value);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_coredump_start_get
* DESCRIPTION
*  get f/w assert flag in STP context
* PARAMETERS
*  VOID
* RETURNS
*  INT32    0= f/w assert flag is not set, others=f/w assert flag is set
*****************************************************************************/
extern INT32 mtk_wcn_stp_coredump_start_get(VOID);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_send_data_raw
* DESCRIPTION
*  send raw data to common interface, bypass STP
* PARAMETERS
*  buffer      [IN]        data buffer
*  length      [IN]        data buffer length
*  type        [IN]        subfunction type
* RETURNS
*  INT32    length transmitted
*****************************************************************************/
extern INT32 mtk_wcn_stp_send_data_raw(const PUINT8 buffer, const UINT32 length, const UINT8 type);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_set_sdio_mode
* DESCRIPTION
*  Set stp for SDIO mode
* PARAMETERS
*  sdio_flag  [IN]        sdio mode flag (TRUE:SDIO mode, FALSE:UART mode)
* RETURNS
*  void
*****************************************************************************/
extern void mtk_wcn_stp_set_mode(UINT32 sdio_flag);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_is_uart_fullset_mode
* DESCRIPTION
*  Is stp use UART Fullset  mode?
* PARAMETERS
*  none.
* RETURNS
*  MTK_WCN_BOOL    TRUE:UART Fullset, FALSE:UART Fullset
*****************************************************************************/
extern MTK_WCN_BOOL mtk_wcn_stp_is_uart_fullset_mode(void);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_is_uart_mand_mode
* DESCRIPTION
*  Is stp use UART Mandatory  mode?
* PARAMETERS
*  none.
* RETURNS
*  MTK_WCN_BOOL    TRUE:UART Mandatory, FALSE:UART Mandatory
*****************************************************************************/
extern MTK_WCN_BOOL mtk_wcn_stp_is_uart_mand_mode(void);
/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_is_btif_fullset_mode
* DESCRIPTION
*  Is stp use BTIF Fullset  mode?
* PARAMETERS
*  none.
* RETURNS
*  MTK_WCN_BOOL    TRUE:BTIF Fullset, FALSE:BTIF Fullset
*****************************************************************************/
extern MTK_WCN_BOOL mtk_wcn_stp_is_btif_fullset_mode(void);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_is_btif_mand_mode
* DESCRIPTION
*  Is stp use BTIF Mandatory  mode?
* PARAMETERS
*  none.
* RETURNS
*  MTK_WCN_BOOL    TRUE:BTIF Mandatory, FALSE:BTIF Mandatory
*****************************************************************************/
extern MTK_WCN_BOOL mtk_wcn_stp_is_btif_mand_mode(void);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_is_sdio_mode
* DESCRIPTION
*  Is stp use SDIO mode?
* PARAMETERS
*  none.
* RETURNS
*  MTK_WCN_BOOL    TRUE:SDIO mode, FALSE:UART mode
*****************************************************************************/
extern MTK_WCN_BOOL mtk_wcn_stp_is_sdio_mode(void);

/*****************************************************************************
* FUNCTION
*  stp_send_inband_reset
* DESCRIPTION
*  To sync to oringnal stp state with f/w stp
* PARAMETERS
*  none.
* RETURNS
*  none
*****************************************************************************/
extern void mtk_wcn_stp_inband_reset(void);

/*****************************************************************************
* FUNCTION
*  stp_send_inband_reset
* DESCRIPTION
*  To send testing command to chip
* PARAMETERS
*  none.
* RETURNS
*  none
*****************************************************************************/
extern void mtk_wcn_stp_test_cmd(INT32 no);

/*****************************************************************************
* FUNCTION
*  stp_send_inband_reset
* DESCRIPTION
* To control STP debugging mechanism
* PARAMETERS
*  func_no: function control, func_op: dumpping filer, func_param: dumpping parameter
* RETURNS
*  none
*****************************************************************************/
extern void mtk_wcn_stp_debug_ctrl(INT32 func_no, INT32 func_op, INT32 func_param);
/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_flush
* DESCRIPTION
*  flush all stp context
* PARAMETERS
*  none.
* RETURNS
*  none
*****************************************************************************/
extern void mtk_wcn_stp_flush_context(void);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_rx_queue
* DESCRIPTION
*  flush all stp rx queue
* PARAMETERS
*  none.
* RETURNS
*  none
*****************************************************************************/
extern void mtk_wcn_stp_flush_rx_queue(UINT32 type);

/*****************************************************************************
* FUNCTION
*  set stp debugging mdoe
* DESCRIPTION
*  set stp debugging mdoe
* PARAMETERS
* dbg_mode: switch to dbg mode ?
* RETURNS
*  void
*****************************************************************************/
extern void mtk_wcn_stp_set_dbg_mode(MTK_WCN_BOOL dbg_mode);

/*****************************************************************************
* FUNCTION
*  set stp auto reset mdoe
* DESCRIPTION
*  set stp auto reset mdoe
* PARAMETERS
* auto_rst: switch to auto reset mode ?
* RETURNS
*  void
*****************************************************************************/
extern void mtk_wcn_stp_set_auto_rst(MTK_WCN_BOOL auto_rst);

/*stp_psm support*/

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_psm_notify_stp
* DESCRIPTION
*  WMT notification to STP that power saving job is done or not
* PARAMETERS
*
* RETURNS
*  0: Sccuess  Negative value: Fail
*****************************************************************************/
extern int mtk_wcn_stp_psm_notify_stp(const UINT32 action);

extern int mtk_wcn_stp_set_psm_state(MTKSTP_PSM_STATE_T state);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_psm_enabla
* DESCRIPTION
*  enable STP PSM
* PARAMETERS
*  int idle_time_to_sleep: IDLE time to sleep
* RETURNS
*  0: Sccuess  Negative value: Fail
*****************************************************************************/
extern int mtk_wcn_stp_psm_enable(int idle_time_to_sleep);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_psm_disable
* DESCRIPTION
*  disable STP PSM
* PARAMETERS
*  void
* RETURNS
*  0: Sccuess  Negative value: Fail
*****************************************************************************/
extern int mtk_wcn_stp_psm_disable(void);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_psm_reset
* DESCRIPTION
*  reset STP PSM (used on whole chip reset)
* PARAMETERS
*  void
* RETURNS
*  0: Sccuess  Negative value: Fail
*****************************************************************************/
extern int mtk_wcn_stp_psm_reset(void);
extern void stp_do_tx_timeout(void);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_btm_get_dmp
* DESCRIPTION
*  get stp dump related information
* PARAMETERS
*  buffer: dump placement, len: dump size
* RETURNS
*   0: Success Negative Value: Fail
*****************************************************************************/
extern int mtk_wcn_stp_btm_get_dmp(char *buf, int *len);

extern int mtk_wcn_stp_dbg_enable(void);

extern int mtk_wcn_stp_dbg_disable(void);

extern void mtk_wcn_stp_set_if_tx_type(ENUM_STP_TX_IF_TYPE stp_if_type);

extern int mtk_wcn_sys_if_rx(UINT8 *data, INT32 size);

extern MTK_WCN_BOOL mtk_wcn_stp_dbg_level(UINT32 dbglevel);

extern INT32 mtk_wcn_stp_dbg_dump_package(VOID);

extern int stp_drv_init(void);

extern void stp_drv_exit(void);

extern INT32 mtk_wcn_stp_dbg_log_ctrl(UINT32 on);

extern INT32 mtk_wcn_stp_coredump_flag_ctrl(UINT32 on);

extern INT32 mtk_wcn_stp_coredump_flag_get(VOID);
extern INT32 mtk_wcn_stp_notify_sleep_for_thermal(void);

extern INT32 mtk_wcn_stp_set_wmt_last_close(UINT32 value);

/*stp btif API declared*/
extern INT32 mtk_wcn_stp_open_btif(VOID);
extern INT32 mtk_wcn_stp_close_btif(VOID);
extern INT32 mtk_wcn_stp_rxcb_register(MTK_WCN_BTIF_RX_CB rx_cb);
extern INT32 mtk_wcn_stp_tx(UINT8 *pBuf, UINT32 len, UINT32 *written_len);
extern INT32 mtk_wcn_stp_wakeup_consys(VOID);
extern INT32 mtk_wcn_stp_dpidle_ctrl(ENUM_BTIF_DPIDLE_CTRL en_flag);
extern INT32 mtk_wcn_stp_lpbk_ctrl(ENUM_BTIF_LPBK_MODE mode);
extern INT32 mtk_wcn_stp_logger_ctrl(ENUM_BTIF_DBG_ID flag);
extern VOID mtk_wcn_stp_ctx_save(VOID);
extern VOID mtk_wcn_stp_ctx_restore(VOID);
extern INT32 mtk_wcn_stp_wmt_evt_err_trg_assert(VOID);
extern VOID mtk_wcn_stp_set_wmt_evt_err_trg_assert(UINT32 value);
extern UINT32 mtk_wcn_stp_get_wmt_evt_err_trg_assert(VOID);
/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _STP_CORE_H_ */
