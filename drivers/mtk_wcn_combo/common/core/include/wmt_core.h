/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/



#ifndef _WMT_CORE_H_
#define _WMT_CORE_H_

#include "osal.h"
#include "wmt_ctrl.h"
#include "wmt_exp.h"
#include "wmt_plat.h"
//TODO: [GeorgeKuo][FixMe] remove temporarily
//#include "mtk_wcn_cmb_stub.h" /* for AIF state definition */

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/
#if defined(MT6620E3) || defined(MT6620E6)   //need modify this part
#define CFG_CORE_MT6620_SUPPORT 1 /* whether MT6620 is supported or not */
#else
#define CFG_CORE_MT6620_SUPPORT 1 /* whether MT6620 is supported or not */
#endif

#if defined(MT6628)
#define CFG_CORE_MT6628_SUPPORT 1 /* whether MT6628 is supported or not */
#else
#define CFG_CORE_MT6628_SUPPORT 1 /* whether MT6628 is supported or not */
#endif

// TODO:[ChangeFeature][George] move this definition outside so that wmt_dev can remove wmt_core.h inclusion.
#define defaultPatchName "mt66xx_patch_hdr.bin"


/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#define BCNT_PATCH_BUF_HEADROOM (8)

#define DWCNT_HIF_CONF    (4)
#define DWCNT_STRAP_CONF  (4)
#define DWCNT_RESERVED    (8)
#define DWCNT_CTRL_DATA  (16)


#if 0 // TODO: [obsolete][GeorgeKuo]: remove ubsolete definitions
#define WMT_SET (1)
#define WMT_QUERY (0)
#define WMT_PKT_FMT_RAW (1)
#define WMT_PKT_FMT_STP (0)
#endif

#define WMT_FUNC_CTRL_ON  (MTK_WCN_BOOL_TRUE)
#define WMT_FUNC_CTRL_OFF (MTK_WCN_BOOL_FALSE)

#define WMT_HDR_LEN             (4)     /* header length */
#define WMT_STS_LEN             (1)     /* status length */
#define WMT_FLAG_LEN            (1)
#define WMT_HIF_UART_INFO_LEN   (4)
#define WMT_FUNC_CTRL_PARAM_LEN (1)
#define WMT_LPBK_CMD_LEN        (5)
#define WMT_LPBK_BUF_LEN        (1024+WMT_LPBK_CMD_LEN)
#define WMT_DEFAULT_BAUD_RATE   (115200)

#define INIT_CMD(c, e, s) {.cmd= c, .cmdSz=sizeof(c), .evt=e, .evtSz=sizeof(e), .str=s}

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

typedef enum _ENUM_WMT_FM_T
{
    WMT_FM_INVALID = 0,
    WMT_FM_I2C = 1,
    WMT_FM_COMM = 2,
    WMT_FM_MAX
} ENUM_WMT_FM_T, *P_ENUM_WMT_FM_T;

typedef enum _ENUM_WMT_HIF_T
{
    WMT_HIF_UART = 0,
    WMT_HIF_SDIO = 1,
    WMT_HIF_MAX
} ENUM_WMT_HIF_T, *P_ENUM_WMT_HIF_T;

#if 0 /* [George] moved to wmt_exp.h for hif_sdio's use */
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

typedef enum _ENUM_WMT_OPID_T {
    WMT_OPID_HIF_CONF = 0,
    WMT_OPID_PWR_ON = 1,
    WMT_OPID_PWR_OFF = 2,
    WMT_OPID_FUNC_ON = 3,
    WMT_OPID_FUNC_OFF = 4,
    WMT_OPID_REG_RW =  5, // TODO:[ChangeFeature][George] is this OP obsoleted?
    WMT_OPID_EXIT = 6,
    WMT_OPID_PWR_SV = 7,
    WMT_OPID_DSNS = 8,
    WMT_OPID_LPBK = 9,
    WMT_OPID_CMD_TEST = 10,
    WMT_OPID_HW_RST = 11,
    WMT_OPID_SW_RST = 12,
    WMT_OPID_BAUD_RST = 13,
    WMT_OPID_STP_RST = 14,
    WMT_OPID_THERM_CTRL = 15,
    WMT_OPID_EFUSE_RW = 16,
    WMT_OPID_GPIO_CTRL = 17,
    WMT_OPID_SDIO_CTRL = 18,
    WMT_OPID_FW_COREDMP = 19,
    WMT_OPID_GPIO_STATE = 20,
    WMT_OPID_MAX
} ENUM_WMT_OPID_T, *P_ENUM_WMT_OPID_T;

typedef OSAL_OP_DAT WMT_OP;
typedef P_OSAL_OP_DAT P_WMT_OP;

typedef enum _ENUM_WMT_UART_FC_T
{
    WMT_UART_NO_FC = 0,
    WMT_UART_MTK_SW_FC = 1,
    WMT_UART_LUX_SW_FC = 2,
    WMT_UART_HW_FC = 3,
    WMT_UART_MAX
} ENUM_WMT_UART_FC_T, *P_ENUM_UART_FC_T;


typedef struct _WMT_HIF_CONF {
    UINT32 hifType; // HIF Type
    UINT32 uartFcCtrl; // UART FC config
    UINT32 au4HifConf[DWCNT_HIF_CONF]; // HIF Config
    UINT32 au4StrapConf[DWCNT_STRAP_CONF]; // Strap Config
} WMT_HIF_CONF, *P_WMT_HIF_CONF;

typedef INT32 (*WMT_OPID_FUNC)(P_WMT_OP);

typedef struct _WMT_GEN_CONF {
    UCHAR cfgExist;

    UCHAR coex_wmt_ant_mode;
    UCHAR coex_wmt_wifi_time_ctl;
    UCHAR coex_wmt_ext_pta_dev_on;

    UCHAR coex_bt_rssi_upper_limit;
    UCHAR coex_bt_rssi_mid_limit;
    UCHAR coex_bt_rssi_lower_limit;
    UCHAR coex_bt_pwr_high;
    UCHAR coex_bt_pwr_mid;
    UCHAR coex_bt_pwr_low;

    UCHAR coex_wifi_rssi_upper_limit;
    UCHAR coex_wifi_rssi_mid_limit;
    UCHAR coex_wifi_rssi_lower_limit;
    UCHAR coex_wifi_pwr_high;
    UCHAR coex_wifi_pwr_mid;
    UCHAR coex_wifi_pwr_low;

    UCHAR coex_ext_pta_hi_tx_tag;
    UCHAR coex_ext_pta_hi_rx_tag;
    UCHAR coex_ext_pta_lo_tx_tag;
    UCHAR coex_ext_pta_lo_rx_tag;
    UINT16 coex_ext_pta_sample_t1;
    UINT16 coex_ext_pta_sample_t2;
    UCHAR coex_ext_pta_wifi_bt_con_trx;

    UINT32 coex_misc_ext_pta_on;
    UINT32 coex_misc_ext_feature_set;
    /*GPS LNA setting*/
    UCHAR wmt_gps_lna_pin;
    UCHAR wmt_gps_lna_enable;
    /*Power on sequence*/
    UCHAR pwr_on_rtc_slot;
    UCHAR pwr_on_ldo_slot;
    UCHAR pwr_on_rst_slot;
    UCHAR pwr_on_off_slot;
    UCHAR pwr_on_on_slot;
	UCHAR co_clock_flag;

    /* Combo chip side SDIO driving setting */
    UINT32 sdio_driving_cfg;
    
} WMT_GEN_CONF, *P_WMT_GEN_CONF;

typedef enum _ENUM_DRV_STS_ {
#if 0
    DRV_STS_INVALID = 0,
    DRV_STS_UNREG = 1, /* Initial State */
#endif
    DRV_STS_POWER_OFF = 0, /* initial state */
    DRV_STS_POWER_ON = 1, /* powered on, only WMT */
    DRV_STS_FUNC_ON = 2, /* FUNC ON */
    DRV_STS_MAX
} ENUM_DRV_STS, *P_ENUM_DRV_STS;

typedef enum _WMT_IC_PIN_ID_
{
    WMT_IC_PIN_AUDIO = 0,
    WMT_IC_PIN_EEDI = 1,
    WMT_IC_PIN_EEDO = 2,
    WMT_IC_PIN_GSYNC = 3,
    WMT_IC_PIN_MAX
}WMT_IC_PIN_ID, *P_WMT_IC_PIN_ID;


typedef enum _WMT_IC_PIN_STATE_
{
    WMT_IC_PIN_EN = 0,
    WMT_IC_PIN_DIS = 1,
    WMT_IC_AIF_0 = 2, // = CMB_STUB_AIF_0,
    WMT_IC_AIF_1 = 3, // = CMB_STUB_AIF_1,
    WMT_IC_AIF_2 = 4, // = CMB_STUB_AIF_2,
    WMT_IC_AIF_3 = 5, // = CMB_STUB_AIF_3,
    WMT_IC_PIN_MUX = 6,
    WMT_IC_PIN_GPIO = 7,
    WMT_IC_PIN_GPIO_HIGH = 8,
    WMT_IC_PIN_GPIO_LOW = 8,
    WMT_IC_PIN_STATE_MAX
} WMT_IC_PIN_STATE, *P_WMT_IC_PIN_STATE;

typedef enum _WMT_CO_CLOCK_
{
    WMT_CO_CLOCK_DIS = 0,
    WMT_CO_CLOCK_EN = 1,
    WMT_CO_CLOCK_MAX
} WMT_CO_CLOCK, *P_WMT_CO_CLOCK;


typedef INT32 (*SW_INIT)(P_WMT_HIF_CONF pWmtHifConf);
typedef INT32 (*SW_DEINIT)(P_WMT_HIF_CONF pWmtHifConf);
typedef INT32 (*IC_PIN_CTRL)(WMT_IC_PIN_ID id, WMT_IC_PIN_STATE state, UINT32 flag);
typedef INT32 (*IC_VER_CHECK)(VOID);
typedef INT32 (*CO_CLOCK_CTRL)(WMT_CO_CLOCK on);
typedef MTK_WCN_BOOL(*IS_QUICK_SLEEP_SUPPORT)(VOID);
typedef MTK_WCN_BOOL(*IS_AEE_DUMP_SUPPORT)(VOID);


typedef struct _WMT_IC_OPS_ {
    UINT32 icId;
    SW_INIT sw_init;
    SW_DEINIT sw_deinit;
    IC_PIN_CTRL ic_pin_ctrl;
    IC_VER_CHECK ic_ver_check;
	CO_CLOCK_CTRL co_clock_ctrl;
	IS_QUICK_SLEEP_SUPPORT is_quick_sleep;
	IS_AEE_DUMP_SUPPORT is_aee_dump_support;
} WMT_IC_OPS, *P_WMT_IC_OPS;

typedef struct _WMT_CTX_
{
    ENUM_DRV_STS eDrvStatus[WMTDRV_TYPE_MAX]; /* Controlled driver status */
    UINT32 wmtInfoBit;                  /* valid info bit */
    WMT_HIF_CONF wmtHifConf;            /* HIF information */

    /* Pointer to WMT_IC_OPS. Shall be assigned to a correct table in stp_init
     * if and only if getting chip id successfully. hwver and fwver are kept in
     * WMT-IC module only.
     */
    P_WMT_IC_OPS p_ic_ops;
} WMT_CTX, *P_WMT_CTX;

// TODO:[ChangeFeature][George] remove WMT_PKT. replace it with hardcoded arrays.
// Using this struct relies on compiler's implementation and pack() settings
typedef struct _WMT_PKT_ {
    UINT8 eType;       // PKT_TYPE_*
    UINT8 eOpCode;     // OPCODE_*
    UINT16 u2SduLen;   // 2 bytes length, little endian
    UINT8 aucParam[32];
} WMT_PKT, *P_WMT_PKT;

/* WMT Packet Format */
typedef enum _ENUM_PKT_TYPE {
    PKT_TYPE_INVALID = 0,
    PKT_TYPE_CMD = 1,
    PKT_TYPE_EVENT = 2,
    PKT_TYPE_MAX
} ENUM_PKT_TYPE, *P_ENUM_PKT_TYPE;

typedef enum _ENUM_OPCODE {
    OPCODE_INVALID = 0,
    OPCODE_PATCH = 1,
    OPCODE_TEST = 2,
    OPCODE_WAKEUP = 3,
    OPCODE_HIF = 4,
    OPCODE_STRAP_CONF = 5,
    OPCODE_FUNC_CTRL = 6,
    OPCODE_RESET = 7,
    OPCODE_INT = 8,
    OPCODE_MAX
} ENUM_OPCODE, *P_ENUM_OPCODE;

typedef enum {
    WMT_STP_CONF_EN = 0,
    WMT_STP_CONF_RDY = 1,
    WMT_STP_CONF_MODE = 2,
    WMT_STP_CONF_MAX
} WMT_STP_CONF_TYPE;

struct init_script {
    UCHAR *cmd;
    UINT32 cmdSz;
    UCHAR *evt;
    UINT32 evtSz;
    UCHAR *str;
};

typedef struct _WMT_PATCH {
    UINT8 ucDateTime[16];
    UINT8 ucPLat[4];
    UINT16 u2HwVer;
    UINT16 u2SwVer;
    UINT32 u4PatchVer;
} WMT_PATCH, *P_WMT_PATCH;


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

extern INT32 wmt_core_init(VOID);
extern INT32 wmt_core_deinit(VOID);

/*****************************************************************************
* FUNCTION
*  mtk_wcn_wmtd
* DESCRIPTION
*  deinit STP kernel
* PARAMETERS
*  void
* RETURNS
*  INT32    0 = success, others = failure
*****************************************************************************/
extern INT32
wmt_core_opid (
    P_WMT_OP pWmtOp
    );

extern INT32
wmt_core_ctrl (
    ENUM_WMT_CTRL_T ctrId,
    PUINT32 pPa1,
    PUINT32 pPa2
    );

extern INT32
wmt_core_func_ctrl_cmd (
    ENUM_WMTDRV_TYPE_T type,
    MTK_WCN_BOOL fgEn
    );

extern INT32
wmt_core_reg_rw_raw (
    UINT32 isWrite,
    UINT32 offset,
    PUINT32 pVal,
    UINT32 mask
);

extern VOID
wmt_core_dump_data (
    PUINT8 pData,
    PUINT8 pTitle,
    UINT32 len
    );

extern MTK_WCN_BOOL
wmt_core_patch_check (
    UINT32 u4PatchVer,
    UINT32 u4HwVer
    );

extern INT32
wmt_core_init_script (
    struct init_script *script,
    INT32 count
    );

extern INT32
wmt_core_rx (
    PUINT8 pBuf,
    UINT32 bufLen,
    UINT32 *readSize
    );

extern INT32
wmt_core_tx (
    const UINT8 *pData,
    UINT32 size,
    UINT32 *writtenSize,
    MTK_WCN_BOOL bRawFlag
    );
extern MTK_WCN_BOOL wmt_core_is_quick_ps_support (void);

extern MTK_WCN_BOOL wmt_core_get_aee_dump_flag(void);


/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

static _osal_inline_ MTK_WCN_BOOL
wmt_core_ic_ops_check (
    P_WMT_IC_OPS p_ops
    )
{
    if (!p_ops) {
        return MTK_WCN_BOOL_FALSE;
    }
    if ( (NULL == p_ops->sw_init)
        || (NULL == p_ops->sw_deinit)
        || (NULL == p_ops->ic_ver_check)
        || (NULL == p_ops->ic_pin_ctrl) ) {
        return MTK_WCN_BOOL_FALSE;
    }
    else {
        return MTK_WCN_BOOL_TRUE;
    }
}

#endif /* _WMT_CORE_H_ */

