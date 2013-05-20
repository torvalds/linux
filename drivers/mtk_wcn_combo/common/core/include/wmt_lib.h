/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/



#ifndef _WMT_LIB_H_
#define _WMT_LIB_H_

#include "osal.h"
#include "wmt_core.h"
#include "wmt_exp.h"
#include <mach/mtk_wcn_cmb_stub.h>
#include "stp_wmt.h"

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/


/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#define WMT_OP_BUF_SIZE (16)
#if 0 /* moved to wmt_exp.h */
#define WMT_LOG_LOUD    4
#define WMT_LOG_DBG     3
#define WMT_LOG_INFO    2
#define WMT_LOG_WARN    1
#define WMT_LOG_ERR     0
#endif
typedef enum _ENUM_WMTRSTRET_TYPE_T{
    WMTRSTRET_SUCCESS = 0x0,
    WMTRSTRET_FAIL = 0x1,
    WMTRSTRET_ONGOING = 0x2,
    WMTRSTRET_MAX
} ENUM_WMTRSTRET_TYPE_T, *P_ENUM_WMTRSTRET_TYPE_T;

/*
3(retry times) * 180 (STP retry time out)
+ 10 (firmware process time) +
10 (transmit time) +
10 (uart process -> WMT response pool) +
230 (others)
*/
#define WMT_LIB_RX_TIMEOUT 2000/*800-->cover v1.2phone BT function on time (~830ms)*/
/*
open wifi during wifi power on procedure
(because wlan is insert to system after mtk_hif_sdio module,
so wifi card is not registered to hif module
when mtk_wcn_wmt_func_on is called by wifi through rfkill)
*/
#define MAX_WIFI_ON_TIME 5500

#define WMT_PWRON_RTY_DFT 2
#define MAX_RETRY_TIME_DUE_TO_RX_TIMEOUT WMT_PWRON_RTY_DFT * WMT_LIB_RX_TIMEOUT
#define MAX_EACH_FUNC_ON_WHEN_CHIP_POWER_ON_ALREADY WMT_LIB_RX_TIMEOUT /*each WMT command*/
#define MAX_FUNC_ON_TIME (MAX_WIFI_ON_TIME + MAX_RETRY_TIME_DUE_TO_RX_TIMEOUT + MAX_EACH_FUNC_ON_WHEN_CHIP_POWER_ON_ALREADY * 3)

#define MAX_EACH_FUNC_OFF WMT_LIB_RX_TIMEOUT + 1000 /*1000->WMT_LIB_RX_TIMEOUT + 1000, logical judgement*/
#define MAX_FUNC_OFF_TIME MAX_EACH_FUNC_OFF * 4

#define MAX_EACH_WMT_CMD WMT_LIB_RX_TIMEOUT + 1000 /*1000->WMT_LIB_RX_TIMEOUT + 1000, logical judgement*/

#define MAX_GPIO_CTRL_TIME (2000) /* [FixMe][GeorgeKuo] a temp value */

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#if 0 /* moved to wmt_exp.h */
/* FIXME: apply KERN_* definition? */
extern UINT32 gWmtDbgLvl ;
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
*                              C O N S T A N T S
********************************************************************************
*/

/* AIF FLAG definition */
/* bit(0): share pin or not */
#define WMT_LIB_AIF_FLAG_MASK (0x1UL)
#define WMT_LIB_AIF_FLAG_SHARE (0x1UL << 0)
#define WMT_LIB_AIF_FLAG_SEPARATE (0x0UL << 0)

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/* bit field offset definition */
typedef enum {
    WMT_STAT_PWR = 0,      /* is powered on */
    WMT_STAT_STP_REG = 1,  /* is STP driver registered: */
    WMT_STAT_STP_OPEN = 2, /* is STP opened: default FALSE*/
    WMT_STAT_STP_EN = 3,   /* is STP enabled: default FALSE*/
    WMT_STAT_STP_RDY = 4,  /* is STP ready for client: default FALSE*/
    WMT_STAT_RX = 5,       /* is rx data available */
    WMT_STAT_CMD = 6,      /* is cmd string to be read */
    WMT_STAT_SDIO1_ON = 7, /* is SDIO1 on */
    WMT_STAT_SDIO2_ON = 8, /* is SDIO2 on */
    WMT_STAT_SDIO_WIFI_ON = 9,  /* is Wi-Fi SDIO function on */
    WMT_STAT_SDIO_STP_ON = 10,  /* is STP SDIO function on */
    WMT_STAT_RST_ON = 11,
    WMT_STAT_MAX
} WMT_STAT;

typedef enum _ENUM_WMTRSTSRC_TYPE_T{
    WMTRSTSRC_RESET_BT = 0x0,
    WMTRSTSRC_RESET_FM = 0x1,
    WMTRSTSRC_RESET_GPS = 0x2,
    WMTRSTSRC_RESET_WIFI = 0x3,
    WMTRSTSRC_RESET_STP = 0x4,
    WMTRSTSRC_RESET_TEST = 0x5,
    WMTRSTSRC_RESET_MAX
} ENUM_WMTRSTSRC_TYPE_T, *P_ENUM_WMTRSTSRC_TYPE_T;


typedef struct {
    PF_WMT_CB fDrvRst[4];
} WMT_FDRV_CB, *P_WMT_FDRV_CB;


typedef struct {
	UINT32 dowloadSeq;
	UCHAR addRess[4];
	UCHAR patchName[256];
}WMT_PATCH_INFO,*P_WMT_PATCH_INFO;


/* OS independent wrapper for WMT_OP */
typedef struct _DEV_WMT_ {

    OSAL_SLEEPABLE_LOCK psm_lock;        
    /* WMTd thread information */
//    struct task_struct *pWmtd;   /* main thread (wmtd) handle */
    OSAL_THREAD thread;
//    wait_queue_head_t rWmtdWq;  /*WMTd command wait queue */
    OSAL_EVENT rWmtdWq;    //rename
    //ULONG state; /* bit field of WMT_STAT */
    OSAL_BIT_OP_VAR state;

    /* STP context information */
//    wait_queue_head_t rWmtRxWq;  /* STP Rx wait queue */
    OSAL_EVENT rWmtRxWq; //rename
//    WMT_STP_FUNC rStpFunc; /* STP functions */
    WMT_FDRV_CB  rFdrvCb;

    /* WMT Configurations */
    WMT_HIF_CONF  rWmtHifConf;
    WMT_GEN_CONF  rWmtGenConf;

    /* Patch information */
    UCHAR cPatchName[NAME_MAX + 1];
    UCHAR cFullPatchName[NAME_MAX + 1];
	UINT32 patchNum;
    const osal_firmware *pPatch;

    UCHAR cWmtcfgName[NAME_MAX + 1];
    const osal_firmware *pWmtCfg;

	const osal_firmware *pNvram;
	
    /* Current used UART port description*/
    CHAR cUartName[NAME_MAX + 1];
    	
    OSAL_OP_Q rFreeOpQ; /* free op queue */
    OSAL_OP_Q rActiveOpQ; /* active op queue */
    OSAL_OP arQue[WMT_OP_BUF_SIZE]; /* real op instances */
    P_OSAL_OP pCurOP; /* current op*/

    /* cmd str buffer */
    UCHAR cCmd[NAME_MAX + 1];
    INT32 cmdResult;
//    struct completion cmd_comp;
//    wait_queue_head_t cmd_wq; /* read command queues */
    OSAL_SIGNAL cmdResp;
    OSAL_EVENT cmdReq;

    /* WMT loopback Thread Information */
//    WMT_CMB_VER combo_ver;
    //P_WMT_CMB_CHIP_INFO_S pChipInfo;
    UINT32 chip_id;
    UINT32 hw_ver;
    UINT32 fw_ver;
    // TODO:  [FixMe][GeorgeKuo] remove this translated version code in the
    // future. Just return the above 3 info to querist
    ENUM_WMTHWVER_TYPE_T eWmtHwVer;

	P_WMT_PATCH_INFO pWmtPatchInfo;
}DEV_WMT, *P_DEV_WMT;

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
extern INT32 wmt_lib_init(VOID);
extern INT32 wmt_lib_deinit(VOID);
extern INT32 wmt_lib_tx (UINT8 *data, UINT32 size, UINT32 *writtenSize);
extern INT32 wmt_lib_tx_raw (UINT8 *data, UINT32 size, UINT32 *writtenSize);
extern INT32 wmt_lib_rx(UINT8 *buff, UINT32 buffLen, UINT32 *readSize);
extern VOID
wmt_lib_flush_rx(VOID);

#if CFG_WMT_PS_SUPPORT
extern INT32
wmt_lib_ps_set_idle_time(UINT32 psIdleTime);
extern INT32
wmt_lib_ps_init(VOID);
extern INT32
wmt_lib_ps_deinit(VOID);
extern INT32
wmt_lib_ps_enable(VOID);
extern INT32 
wmt_lib_ps_ctrl(UINT32 state);

extern INT32
wmt_lib_ps_disable(VOID);
extern VOID
wmt_lib_ps_irq_cb(VOID);
#endif
extern VOID
wmt_lib_ps_set_sdio_psop (
    PF_WMT_SDIO_PSOP own_cb
    );

/* LXOP functions: */
extern P_OSAL_OP wmt_lib_get_free_op (VOID);
extern INT32 wmt_lib_put_op_to_free_queue(P_OSAL_OP pOp);
extern MTK_WCN_BOOL wmt_lib_put_act_op (P_OSAL_OP pOp);

//extern ENUM_WMTHWVER_TYPE_T wmt_lib_get_hwver (VOID);
extern UINT32 wmt_lib_get_icinfo (ENUM_WMT_CHIPINFO_TYPE_T type);

extern MTK_WCN_BOOL wmt_lib_is_therm_ctrl_support (VOID);
extern MTK_WCN_BOOL wmt_lib_is_dsns_ctrl_support (VOID);
extern INT32 wmt_lib_trigger_cmd_signal (INT32 result);
extern UCHAR *wmt_lib_get_cmd(VOID);
extern P_OSAL_EVENT wmt_lib_get_cmd_event(VOID);
extern INT32 wmt_lib_set_patch_name(UCHAR *cPatchName);
extern INT32 wmt_lib_set_uart_name(CHAR *cUartName);
extern INT32 wmt_lib_set_hif(ULONG hifconf);
extern P_WMT_HIF_CONF wmt_lib_get_hif(VOID);
extern MTK_WCN_BOOL wmt_lib_get_cmd_status(VOID);

/* GeorgeKuo: replace set_chip_gpio() with more specific ones */
#if 0/* moved to wmt_exp.h */
extern INT32 wmt_lib_set_aif (CMB_STUB_AIF_X aif, MTK_WCN_BOOL share); /* set AUDIO interface options */
#endif
extern INT32 wmt_lib_host_awake_get(VOID);
extern INT32 wmt_lib_host_awake_put(VOID);
extern UINT32 wmt_lib_dbg_level_set(UINT32 level);

extern INT32 wmt_lib_msgcb_reg (
    ENUM_WMTDRV_TYPE_T eType,
    PF_WMT_CB pCb
    );

extern INT32 wmt_lib_msgcb_unreg (
    ENUM_WMTDRV_TYPE_T eType
    );
ENUM_WMTRSTRET_TYPE_T wmt_lib_cmb_rst( ENUM_WMTRSTSRC_TYPE_T src);
MTK_WCN_BOOL wmt_lib_sw_rst(INT32 baudRst);
MTK_WCN_BOOL wmt_lib_hw_rst(VOID);
INT32 wmt_lib_reg_rw (
    UINT32 isWrite,
    UINT32 offset,
    PUINT32 pvalue,
    UINT32 mask
    );
INT32 wmt_lib_efuse_rw (
    UINT32 isWrite,
    UINT32 offset,
    PUINT32 pvalue,
    UINT32 mask
    );
INT32 wmt_lib_sdio_ctrl(UINT32 on);

extern INT32 DISABLE_PSM_MONITOR(void);
extern VOID ENABLE_PSM_MONITOR(void);
extern INT32 wmt_lib_notify_stp_sleep(void);
extern void wmt_lib_psm_lock_release(void);
extern INT32 wmt_lib_psm_lock_aquire(void);
extern INT32 wmt_lib_set_stp_wmt_last_close(UINT32 value);

extern VOID wmt_lib_set_patch_num(ULONG num);
extern VOID wmt_lib_set_patch_info(P_WMT_PATCH_INFO pPatchinfo);

extern INT32 wmt_lib_set_current_op(P_DEV_WMT pWmtDev, P_OSAL_OP pOp);
extern P_OSAL_OP wmt_lib_get_current_op(P_DEV_WMT pWmtDev);

extern INT32 wmt_lib_merge_if_flag_ctrl(UINT32 enable);
extern INT32 wmt_lib_merge_if_flag_get(UINT32 enable);

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#endif /* _WMT_LIB_H_ */

