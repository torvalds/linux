/*! \file
    \brief  Declaration of library functions

    Any definitions in this file will be shared among GLUE Layer and internal Driver Stack.
*/



/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/

#ifdef DFT_TAG
#undef DFT_TAG
#endif
#define DFT_TAG         "[WMT-CORE]"


/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "osal_typedef.h"

#include "wmt_lib.h"
#include "wmt_core.h"
#include "wmt_ctrl.h"
#include "wmt_ic.h"
#include "wmt_conf.h"

#include "wmt_func.h"
#include "stp_core.h"
#include "psm_core.h"

#if CFG_CORE_MT6620_SUPPORT
extern WMT_IC_OPS wmt_ic_ops_mt6620;
#endif

#if CFG_CORE_MT6628_SUPPORT
extern WMT_IC_OPS wmt_ic_ops_mt6628;
#endif

#if CFG_FUNC_BT_SUPPORT
extern WMT_FUNC_OPS wmt_func_bt_ops;
#endif

#if CFG_FUNC_FM_SUPPORT
extern WMT_FUNC_OPS wmt_func_fm_ops;
#endif

#if CFG_FUNC_GPS_SUPPORT
extern WMT_FUNC_OPS wmt_func_gps_ops;
#endif

#if CFG_FUNC_WIFI_SUPPORT
extern WMT_FUNC_OPS wmt_func_wifi_ops;
#endif

P_WMT_FUNC_OPS gpWmtFuncOps[4] = {
#if CFG_FUNC_BT_SUPPORT
    [0] = &wmt_func_bt_ops,
#else
    [0] = NULL,
#endif

#if CFG_FUNC_FM_SUPPORT
    [1] = &wmt_func_fm_ops,
#else
    [1] = NULL,
#endif

#if CFG_FUNC_GPS_SUPPORT
    [2] = &wmt_func_gps_ops,
#else
    [2] = NULL,
#endif

#if CFG_FUNC_WIFI_SUPPORT
    [3] = &wmt_func_wifi_ops,
#else
    [3] = NULL,
#endif

};

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

// TODO:[FixMe][GeorgeKuo]: is it an MT6620 only or general general setting? move to wmt_ic_6620 temporarily.
/* #define CFG_WMT_BT_PORT2 (1) */ /* BT Port 2 Feature.*/

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

static WMT_CTX gMtkWmtCtx;
static UINT8 gLpbkBuf[WMT_LPBK_BUF_LEN] = {0}; 

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

static INT32 opfunc_hif_conf (P_WMT_OP pWmtOp);
static INT32 opfunc_pwr_on (P_WMT_OP pWmtOp);
static INT32 opfunc_pwr_off (P_WMT_OP pWmtOp);
static INT32 opfunc_func_on (P_WMT_OP pWmtOp);
static INT32 opfunc_func_off (P_WMT_OP pWmtOp);
static INT32 opfunc_reg_rw ( P_WMT_OP pWmtOp);
static INT32 opfunc_exit (P_WMT_OP pWmtOp);
static INT32 opfunc_pwr_sv (P_WMT_OP pWmtOp);
static INT32 opfunc_dsns (P_WMT_OP pWmtOp);
static INT32 opfunc_lpbk (P_WMT_OP pWmtOp);
static INT32 opfunc_cmd_test (P_WMT_OP pWmtOp);
static INT32 opfunc_hw_rst (P_WMT_OP pWmtOp);
static INT32 opfunc_sw_rst (P_WMT_OP pWmtOp);
static INT32 opfunc_stp_rst (P_WMT_OP pWmtOp);
static INT32 opfunc_therm_ctrl (P_WMT_OP pWmtOp);
static INT32 opfunc_efuse_rw (P_WMT_OP pWmtOp);
static INT32 opfunc_therm_ctrl (P_WMT_OP pWmtOp);
static INT32 opfunc_gpio_ctrl (P_WMT_OP pWmtOp);
static INT32 opfunc_sdio_ctrl (P_WMT_OP pWmtOp);
static INT32 opfunc_pin_state (P_WMT_OP pWmtOp);
static VOID wmt_core_dump_func_state (CHAR *pSource);
static INT32 wmt_core_stp_init (VOID);
static INT32 wmt_core_stp_deinit (VOID);
static INT32 wmt_core_hw_check (VOID);



/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

const static UCHAR WMT_SLEEP_CMD[] = {0x01, 0x03, 0x01, 0x00, 0x01};
const static UCHAR WMT_SLEEP_EVT[] = {0x02, 0x03, 0x02, 0x00, 0x00, 0x01};

const static UCHAR WMT_HOST_AWAKE_CMD[] = {0x01, 0x03, 0x01, 0x00, 0x02};
const static UCHAR WMT_HOST_AWAKE_EVT[] = {0x02, 0x03, 0x02, 0x00, 0x00, 0x02};

const static UCHAR WMT_WAKEUP_CMD[] = {0xFF};
const static UCHAR WMT_WAKEUP_EVT[] = {0x02, 0x03, 0x02, 0x00, 0x00, 0x03};

static UCHAR WMT_THERM_CMD[] = {0x01, 0x11, 0x01, 0x00,
    0x00 /*thermal sensor operation*/
};
static UCHAR WMT_THERM_CTRL_EVT[] = {0x02, 0x11, 0x01, 0x00, 0x00};
static UCHAR WMT_THERM_READ_EVT[] = {0x02, 0x11, 0x02, 0x00, 0x00, 0x00};

static UCHAR WMT_EFUSE_CMD[] = {0x01, 0x0D, 0x08, 0x00,
    0x01, /*[4]operation, 0:init, 1:write 2:read*/
    0x01, /*[5]Number of register setting*/
    0xAA, 0xAA, /*[6-7]Address*/
    0xBB, 0xBB, 0xBB, 0xBB /*[8-11] Value*/
};

static UCHAR WMT_EFUSE_EVT[]  = {0x02, 0x0D, 0x08, 0x00,
    0xAA, /*[4]operation, 0:init, 1:write 2:read*/
    0xBB, /*[5]Number of register setting*/
    0xCC, 0xCC, /*[6-7]Address*/
    0xDD, 0xDD, 0xDD, 0xDD /*[8-11] Value*/
};

static UCHAR WMT_DSNS_CMD[] = {0x01, 0x0E, 0x02, 0x00, 0x01,
    0x00 /*desnse type*/
};
static UCHAR WMT_DSNS_EVT[] = {0x02, 0x0E, 0x01, 0x00, 0x00 };

// TODO:[NewFeature][GeorgeKuo] Update register group in ONE CMD/EVT
static UCHAR WMT_SET_REG_CMD[] = {0x01, 0x08, 0x10, 0x00 /*length*/
    , 0x00 /*op: w(1) & r(2) */
    , 0x01 /*type: reg */
    , 0x00 /*res*/
    , 0x01 /*1 register*/
    , 0x00, 0x00, 0x00, 0x00 /* addr */
    , 0x00, 0x00, 0x00, 0x00 /* value */
    , 0xFF, 0xFF, 0xFF, 0xFF /*mask */
};
static UCHAR WMT_SET_REG_WR_EVT[] = {0x02, 0x08, 0x04, 0x00/*length*/
    , 0x00 /*S: 0*/
    , 0x00 /*type: reg */
    , 0x00 /*rev*/
    , 0x01 /*1 register*/
    //, 0x00, 0x00, 0x00, 0x00 /* addr */
    //, 0x00, 0x00, 0x00, 0x00 /* value */
};
static UCHAR WMT_SET_REG_RD_EVT[] = {0x02, 0x08, 0x04, 0x00/*length*/
    , 0x00 /*S: 0*/
    , 0x00 /*type: reg */
    , 0x00 /*rev*/
    , 0x01 /*1 register*/
    , 0x00, 0x00, 0x00, 0x00 /* addr */
    , 0x00, 0x00, 0x00, 0x00 /* value */
};

/* GeorgeKuo: Use designated initializers described in
 * http://gcc.gnu.org/onlinedocs/gcc-4.0.4/gcc/Designated-Inits.html
 */
 
const static WMT_OPID_FUNC wmt_core_opfunc[] = {
    [WMT_OPID_HIF_CONF] = opfunc_hif_conf,
    [WMT_OPID_PWR_ON] = opfunc_pwr_on,
    [WMT_OPID_PWR_OFF] = opfunc_pwr_off,
    [WMT_OPID_FUNC_ON] = opfunc_func_on,
    [WMT_OPID_FUNC_OFF] = opfunc_func_off,
    [WMT_OPID_REG_RW] = opfunc_reg_rw, // TODO:[ChangeFeature][George] is this OP obsoleted?
    [WMT_OPID_EXIT] =  opfunc_exit,
    [WMT_OPID_PWR_SV] = opfunc_pwr_sv,
    [WMT_OPID_DSNS] = opfunc_dsns,
    [WMT_OPID_LPBK] = opfunc_lpbk,
    [WMT_OPID_CMD_TEST] = opfunc_cmd_test,
    [WMT_OPID_HW_RST] = opfunc_hw_rst,
    [WMT_OPID_SW_RST] = opfunc_sw_rst,
    [WMT_OPID_STP_RST] = opfunc_stp_rst,
    [WMT_OPID_THERM_CTRL] = opfunc_therm_ctrl,
    [WMT_OPID_EFUSE_RW] = opfunc_efuse_rw,
    [WMT_OPID_GPIO_CTRL] = opfunc_gpio_ctrl,
    [WMT_OPID_SDIO_CTRL] = opfunc_sdio_ctrl,
    [WMT_OPID_GPIO_STATE] = opfunc_pin_state,
};

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/
INT32 wmt_core_init(VOID)
{
    INT32 i = 0;

    osal_memset(&gMtkWmtCtx, 0, osal_sizeof(gMtkWmtCtx));
    /* gMtkWmtCtx.p_ops is cleared to NULL */

    /* default FUNC_OFF state */
    for (i = 0; i < WMTDRV_TYPE_MAX; ++i) {
        /* WinMo is default to DRV_STS_UNREG;*/
        gMtkWmtCtx.eDrvStatus[i] = DRV_STS_POWER_OFF;
    }

    return 0;
}

INT32 wmt_core_deinit(VOID)
{
    //return to init state
    osal_memset(&gMtkWmtCtx, 0, osal_sizeof(gMtkWmtCtx));
    /* gMtkWmtCtx.p_ops is cleared to NULL */
    return 0;
}

// TODO: [ChangeFeature][George] Is wmt_ctrl a good interface? maybe not......
// parameters shall be copied in/from ctrl buffer, which is also a size-wasting buffer.
INT32
wmt_core_tx (
    const UINT8 *pData,
    const UINT32 size,
    UINT32 *writtenSize,
    const MTK_WCN_BOOL bRawFlag
    )
{
    INT32 iRet;
#if 0 /* Test using direct function call instead of wmt_ctrl() interface */
    WMT_CTRL_DATA ctrlData;
    ctrlData.ctrlId = WMT_CTRL_TX;
    ctrlData.au4CtrlData[0] = (UINT32)pData;
    ctrlData.au4CtrlData[1] = size;
    ctrlData.au4CtrlData[2] = (UINT32)writtenSize;
    ctrlData.au4CtrlData[3] = bRawFlag;

    iRet = wmt_ctrl(&ctrlData);
    if (iRet) {
        /* ERROR */
        WMT_ERR_FUNC("WMT-CORE: wmt_core_ctrl failed: WMT_CTRL_TX, iRet:%d\n", iRet);
        //(*sys_dbg_assert)(0, __FILE__, __LINE__);
        osal_assert(0);
    }
#endif
    iRet = wmt_ctrl_tx_ex(pData, size, writtenSize, bRawFlag);
    return iRet;
}

INT32 wmt_core_rx(PUINT8 pBuf, UINT32 bufLen, UINT32 *readSize)
{
    INT32 iRet;
    WMT_CTRL_DATA ctrlData;
    ctrlData.ctrlId = WMT_CTRL_RX;
    ctrlData.au4CtrlData[0] = (UINT32)pBuf;
    ctrlData.au4CtrlData[1] = bufLen;
    ctrlData.au4CtrlData[2] = (UINT32)readSize;

    iRet = wmt_ctrl(&ctrlData);
    if (iRet) {
        /* ERROR */
        WMT_ERR_FUNC("WMT-CORE: wmt_core_ctrl failed: WMT_CTRL_RX, iRet:%d\n", iRet);
        mtk_wcn_stp_dbg_dump_package();
        osal_assert(0);
    }
    return iRet;
}

INT32 wmt_core_rx_flush(UINT32 type)
{
    INT32 iRet;
    WMT_CTRL_DATA ctrlData;
    ctrlData.ctrlId = WMT_CTRL_RX_FLUSH;
    ctrlData.au4CtrlData[0] = (UINT32)type;

    iRet = wmt_ctrl(&ctrlData);
    if (iRet) {
        /* ERROR */
        WMT_ERR_FUNC("WMT-CORE: wmt_core_ctrl failed: WMT_CTRL_RX_FLUSH, iRet:%d\n", iRet);
        osal_assert(0);
    }
    return iRet;
}

INT32 wmt_core_func_ctrl_cmd (
    ENUM_WMTDRV_TYPE_T type,
    MTK_WCN_BOOL fgEn
    )
{
    INT32 iRet = 0;
    UINT32 u4WmtCmdPduLen;
    UINT32 u4WmtEventPduLen;
    UINT32 u4ReadSize;
    UINT32 u4WrittenSize;
    WMT_PKT rWmtPktCmd;
    WMT_PKT rWmtPktEvent;
    MTK_WCN_BOOL fgFail;

    // TODO:[ChangeFeature][George] remove WMT_PKT. replace it with hardcoded arrays.
    // Using this struct relies on compiler's implementation and pack() settings
    osal_memset(&rWmtPktCmd, 0, osal_sizeof(rWmtPktCmd));
    osal_memset(&rWmtPktEvent, 0, osal_sizeof(rWmtPktEvent));

    rWmtPktCmd.eType = (UINT8)PKT_TYPE_CMD;
    rWmtPktCmd.eOpCode = (UINT8)OPCODE_FUNC_CTRL;

    // Flag field: driver type
    rWmtPktCmd.aucParam[0] = (UINT8)type;
    // Parameter field: ON/OFF
    rWmtPktCmd.aucParam[1] = (fgEn == WMT_FUNC_CTRL_ON) ? 1 : 0;
    rWmtPktCmd.u2SduLen = WMT_FLAG_LEN + WMT_FUNC_CTRL_PARAM_LEN; // (2)

    // WMT Header + WMT SDU
    u4WmtCmdPduLen = WMT_HDR_LEN + rWmtPktCmd.u2SduLen; // (6)
    u4WmtEventPduLen = WMT_HDR_LEN + WMT_STS_LEN; // (5)

    do {
        fgFail = MTK_WCN_BOOL_TRUE;
//        iRet = (*kal_stp_tx)((PUINT8)&rWmtPktCmd, u4WmtCmdPduLen, &u4WrittenSize);
       iRet = wmt_core_tx((PUINT8)&rWmtPktCmd, u4WmtCmdPduLen, &u4WrittenSize, MTK_WCN_BOOL_FALSE);
        if (iRet) {
            WMT_ERR_FUNC("WMT-CORE: wmt_func_ctrl_cmd kal_stp_tx failed \n");
            break;
        }

        iRet = wmt_core_rx((PUINT8)&rWmtPktEvent, u4WmtEventPduLen, &u4ReadSize);
        if (iRet) {
            WMT_ERR_FUNC("WMT-CORE: wmt_func_ctrl_cmd kal_stp_rx failed\n");
            break;
        }

        /* Error Checking */
        if (PKT_TYPE_EVENT != rWmtPktEvent.eType) {
            WMT_ERR_FUNC("WMT-CORE: wmt_func_ctrl_cmd PKT_TYPE_EVENT != rWmtPktEvent.eType %d\n", rWmtPktEvent.eType);
            break;
        }

        if (rWmtPktCmd.eOpCode != rWmtPktEvent.eOpCode) {
            WMT_ERR_FUNC("WMT-CORE: wmt_func_ctrl_cmd rWmtPktCmd.eOpCode(0x%x) != rWmtPktEvent.eType(0x%x)\n",
                rWmtPktCmd.eOpCode, rWmtPktEvent.eOpCode);
            break;
        }

        if (u4WmtEventPduLen != (rWmtPktEvent.u2SduLen + WMT_HDR_LEN)) {
            WMT_ERR_FUNC("WMT-CORE: wmt_func_ctrl_cmd u4WmtEventPduLen(0x%x) != rWmtPktEvent.u2SduLen(0x%x)+4\n",
                u4WmtEventPduLen, rWmtPktEvent.u2SduLen);
            break;
        }

        // Status field of event check
        if (0 != rWmtPktEvent.aucParam[0]) {
            WMT_ERR_FUNC("WMT-CORE: wmt_func_ctrl_cmd, 0 != status(%d)\n", rWmtPktEvent.aucParam[0]);
            break;
        }

        fgFail = MTK_WCN_BOOL_FALSE;
    } while (0);

    if (MTK_WCN_BOOL_FALSE == fgFail) {
        //WMT_INFO_FUNC("WMT-CORE: wmt_func_ctrl_cmd OK!\n");
        return 0;
    }
    else {
        WMT_ERR_FUNC("WMT-CORE: wmt_func_ctrl_cmd 0x%x FAIL\n", rWmtPktCmd.aucParam[0]);
        return -2;
    }
}

INT32 wmt_core_opid_handler(P_WMT_OP pWmtOp)
{
    UINT32 opId;
    INT32 ret;
    
    opId = pWmtOp->opId;

    if (wmt_core_opfunc[opId]) {
        ret = (*(wmt_core_opfunc[opId]))(pWmtOp); /*wmtCoreOpidHandlerPack[].opHandler*/
        return ret;
    }
    else {
        WMT_ERR_FUNC("WMT-CORE: null handler (%d)\n", pWmtOp->opId);
        return -2;
    }
}

INT32 wmt_core_opid(P_WMT_OP pWmtOp)
{

    /*sanity check*/
    if (NULL == pWmtOp) {
        WMT_ERR_FUNC("null pWmtOP\n");
        /*print some message with error info*/
        return -1;
    }

    if (WMT_OPID_MAX <= pWmtOp->opId) {
        WMT_ERR_FUNC("WMT-CORE: invalid OPID(%d)\n", pWmtOp->opId);
        return -2;
    }

    // TODO: [FixMe][GeorgeKuo] do sanity check to const function table when init and skip checking here
    return wmt_core_opid_handler(pWmtOp);    
}

INT32 wmt_core_ctrl (ENUM_WMT_CTRL_T ctrId, PUINT32 pPa1, PUINT32 pPa2)
{
    INT32 iRet = -1;
    WMT_CTRL_DATA ctrlData;
    UINT32 val1 = (pPa1) ? *pPa1: 0;
    UINT32 val2 = (pPa2) ? *pPa2 : 0;

    ctrlData.ctrlId= (UINT32)ctrId;
    ctrlData.au4CtrlData[0] = val1;
    ctrlData.au4CtrlData[1] = val2;

    iRet = wmt_ctrl(&ctrlData);
    if (iRet) {
        /* ERROR */
        WMT_ERR_FUNC("WMT-CORE: wmt_core_ctrl failed: id(%d), type(%d), value(%d) iRet:(%d)\n", ctrId, val1, val2, iRet);
        osal_assert(0);
    }
    else {
        if (pPa1) {
            *pPa1 = ctrlData.au4CtrlData[0];
        }
        if (pPa2) {
            *pPa2 = ctrlData.au4CtrlData[1];
        }
    }
    return iRet;
}


VOID wmt_core_dump_data (
    PUINT8 pData,
    PUINT8 pTitle,
    UINT32 len
    )
{
    PUINT8 ptr = pData;
    INT32 k=0;
    WMT_INFO_FUNC("%s len=%d\n", pTitle, len);
    for(k=0; k < len ; k++) {
        if (k % 16 == 0)  WMT_INFO_FUNC("\n");
        WMT_INFO_FUNC("0x%02x ",  *ptr);
        ptr++;
    }
    WMT_INFO_FUNC("--end\n");
}

/*!
 * \brief An WMT-CORE function to support read, write, and read after write to
 * an internal register.
 *
 * Detailed description.
 *
 * \param isWrite 1 for write, 0 for read
 * \param offset of register to be written or read
 * \param pVal a pointer to the 32-bit value to be writtern or read
 * \param mask a 32-bit mask to be applied for the read or write operation
 *
 * \retval 0 operation success
 * \retval -1 invalid parameters
 * \retval -2 tx cmd fail
 * \retval -3 rx event fail
 * \retval -4 read check error
 */
INT32
wmt_core_reg_rw_raw (
    UINT32 isWrite,
    UINT32 offset,
    PUINT32 pVal,
    UINT32 mask
    )
{
    INT32 iRet;
    UINT32 u4Res;
    UINT32 evtLen;
    UINT8 evtBuf[16] = {0};

    WMT_SET_REG_CMD[4] = (isWrite) ? 0x1 : 0x2; /* w:1, r:2 */
    osal_memcpy(&WMT_SET_REG_CMD[8], &offset, 4); /* offset */
    osal_memcpy(&WMT_SET_REG_CMD[12], pVal, 4); /* [2] is var addr */
    osal_memcpy(&WMT_SET_REG_CMD[16], &mask, 4); /* mask */

    /* send command */
    iRet = wmt_core_tx(WMT_SET_REG_CMD, sizeof(WMT_SET_REG_CMD), &u4Res, MTK_WCN_BOOL_FALSE);
    if ( (iRet) || (u4Res != sizeof(WMT_SET_REG_CMD)) ) {
        WMT_ERR_FUNC("Tx REG_CMD fail!(%d) len (%d, %d) \n", iRet, u4Res, sizeof(WMT_SET_REG_CMD));
        return -2;
    }

    /* receive event */
    evtLen = (isWrite) ? sizeof(WMT_SET_REG_WR_EVT) : sizeof(WMT_SET_REG_RD_EVT);
    iRet = wmt_core_rx(evtBuf, evtLen, &u4Res);
    if ( (iRet) || (u4Res != evtLen) ) {
        WMT_ERR_FUNC("Rx REG_EVT fail!(%d) len(%d, %d)\n", iRet, u4Res, evtLen);
        return -3;
    }

    if (!isWrite) {
        UINT32 rxEvtAddr;
        UINT32 txCmdAddr;
        osal_memcpy(&txCmdAddr, &WMT_SET_REG_CMD[8], 4);
        osal_memcpy(&rxEvtAddr, &evtBuf[8], 4);

        /* check read result */
        if (txCmdAddr != rxEvtAddr) {
            WMT_ERR_FUNC("Check read addr fail (0x%08x, 0x%08x)\n", rxEvtAddr, txCmdAddr);
            return -4;
        }
        else {
            WMT_DBG_FUNC("Check read addr(0x%08x) ok\n", rxEvtAddr);
        }
        osal_memcpy(pVal, &evtBuf[12], 4);
    }

    /* no error here just return 0 */
    return 0;
}

INT32
wmt_core_init_script (
    struct init_script *script,
    INT32 count
    )
{
    UCHAR evtBuf[256];
    UINT32 u4Res;
    INT32 i = 0;
    INT32 iRet;

    for (i = 0; i < count; i++) {
        WMT_DBG_FUNC("WMT-CORE: init_script operation %s start \n", script[i].str);
        /* CMD */
        //iRet = (*kal_stp_tx)(script[i].cmd, script[i].cmdSz, &u4Res);
        iRet = wmt_core_tx(script[i].cmd, script[i].cmdSz, &u4Res, MTK_WCN_BOOL_FALSE);
        if (iRet || (u4Res != script[i].cmdSz)) {
            WMT_ERR_FUNC("WMT-CORE: write (%s) iRet(%d) cmd len err(%d, %d) \n", script[i].str, iRet, u4Res, script[i].cmdSz);
            break;
        }
        /* EVENT BUF */
        osal_memset(evtBuf, 0, sizeof(evtBuf));
        iRet = wmt_core_rx(evtBuf, script[i].evtSz, &u4Res);
        if (iRet || (u4Res != script[i].evtSz)) {
            WMT_ERR_FUNC("WMT-CORE: read (%s) iRet(%d) evt len err(rx:%d, exp:%d) \n", script[i].str, iRet, u4Res, script[i].evtSz);
            mtk_wcn_stp_dbg_dump_package();
            break;
        }
        /* RESULT */
        if (osal_memcmp(evtBuf, script[i].evt, script[i].evtSz) != 0) {
            WMT_ERR_FUNC("WMT-CORE:compare %s result error \n", script[i].str);
            WMT_ERR_FUNC("WMT-CORE:rx(%d):[%02X,%02X,%02X,%02X,%02X] exp(%d):[%02X,%02X,%02X,%02X,%02X]\n",
                u4Res, evtBuf[0], evtBuf[1], evtBuf[2], evtBuf[3], evtBuf[4],
                script[i].evtSz, script[i].evt[0], script[i].evt[1], script[i].evt[2], script[i].evt[3], script[i].evt[4]);
            mtk_wcn_stp_dbg_dump_package();
            break;
        }

        WMT_DBG_FUNC("init_script operation %s ok \n", script[i].str);
    }

    return (i == count) ? 0 : -1;
}

static INT32
wmt_core_stp_init (VOID)
{
    INT32 iRet = -1;
    UINT32 ctrlPa1;
    UINT32 ctrlPa2;
    P_WMT_CTX pctx = &gMtkWmtCtx;
	P_WMT_GEN_CONF pWmtGenConf = NULL;
    wmt_conf_read_file();
	pWmtGenConf = wmt_conf_get_cfg();
    if (!(pctx->wmtInfoBit & WMT_OP_HIF_BIT)) {
        WMT_ERR_FUNC("WMT-CORE: no hif info!\n");
        osal_assert(0);
        return -1;
    }

    //4 <0> turn on SDIO2 for common SDIO
    if (WMT_HIF_SDIO == pctx->wmtHifConf.hifType) {
        ctrlPa1 = WMT_SDIO_SLOT_SDIO2;
        ctrlPa2 = 1; /* turn on SDIO2 slot */
        iRet = wmt_core_ctrl(WMT_CTRL_SDIO_HW, &ctrlPa1, &ctrlPa2) ;
        if (iRet) {
            WMT_ERR_FUNC("WMT-CORE: turn on SLOT_SDIO2 fail (%d)\n", iRet);
            osal_assert(0);

            return -2;
        }
        pctx->eDrvStatus[WMTDRV_TYPE_SDIO2] = DRV_STS_FUNC_ON;

        ctrlPa1 = WMT_SDIO_FUNC_STP;
        ctrlPa2 = 1; /* turn on STP driver */
        iRet = wmt_core_ctrl(WMT_CTRL_SDIO_FUNC, &ctrlPa1, &ctrlPa2) ;
        if (iRet) {
            WMT_ERR_FUNC("WMT-CORE: turn on SDIO_FUNC_STP func fail (%d)\n", iRet);

            /* check all sub-func and do power off */
            return -3;
        }
    }

    //4 <1> open stp
    ctrlPa1 = 0; ctrlPa2 = 0;
    iRet = wmt_core_ctrl(WMT_CTRL_STP_OPEN, &ctrlPa1, &ctrlPa2);
    if (iRet) {
        WMT_ERR_FUNC("WMT-CORE: wmt open stp\n");
        return -4;
    }

    if (WMT_HIF_UART == pctx->wmtHifConf.hifType) {
        ctrlPa1 = WMT_DEFAULT_BAUD_RATE; ctrlPa2 = 0;
        iRet = wmt_core_ctrl(WMT_CTRL_HOST_BAUDRATE_SET, &ctrlPa1, &ctrlPa2);
        if (iRet) {
            WMT_ERR_FUNC("WMT-CORE: change host baudrate(%d) fails \n", pctx->wmtHifConf.au4HifConf[0]);
            return -5;
        }
    }
    //WMT_DBG_FUNC("WMT-CORE: change host baudrate(%d) ok \n", gMtkWmtCtx.wmtHifConf.au4HifConf[0]);

    //4 <1.5> disable and un-ready stp
    ctrlPa1 = WMT_STP_CONF_EN; ctrlPa2 = 0;
    iRet += wmt_core_ctrl(WMT_CTRL_STP_CONF, &ctrlPa1, &ctrlPa2);
    ctrlPa1 = WMT_STP_CONF_RDY; ctrlPa2 = 0;
    iRet += wmt_core_ctrl(WMT_CTRL_STP_CONF, &ctrlPa1, &ctrlPa2);

    //4 <2> set mode and enable
    if (WMT_HIF_UART == pctx->wmtHifConf.hifType) {
        ctrlPa1 = WMT_STP_CONF_MODE; ctrlPa2 = MTKSTP_UART_MAND_MODE;
        iRet += wmt_core_ctrl(WMT_CTRL_STP_CONF, &ctrlPa1, &ctrlPa2);
    }
    else if (WMT_HIF_SDIO == pctx->wmtHifConf.hifType) {
        ctrlPa1 = WMT_STP_CONF_MODE; ctrlPa2 = MTKSTP_SDIO_MODE;
        iRet += wmt_core_ctrl(WMT_CTRL_STP_CONF, &ctrlPa1, &ctrlPa2);
    }
    ctrlPa1 = WMT_STP_CONF_EN; ctrlPa2 = 1;
    iRet += wmt_core_ctrl(WMT_CTRL_STP_CONF, &ctrlPa1, &ctrlPa2);
    if (iRet) {
        WMT_ERR_FUNC("WMT-CORE: stp_init <1><2> fail:%d\n", iRet);
        return -7;
    }

    // TODO: [ChangeFeature][GeorgeKuo] can we apply raise UART baud rate firstly for ALL supported chips???

    iRet = wmt_core_hw_check();
    if (iRet) {
        WMT_ERR_FUNC("hw_check fail:%d\n", iRet);
        return -8;
    }
    /* mtkWmtCtx.p_ic_ops is identified and checked ok */
    if ((NULL != pctx->p_ic_ops->co_clock_ctrl) && (pWmtGenConf != NULL))	
	{
	    (*(pctx->p_ic_ops->co_clock_ctrl))(pWmtGenConf->co_clock_flag == 0 ? WMT_CO_CLOCK_DIS : WMT_CO_CLOCK_EN);	
	}	
	else	
	{
	    WMT_INFO_FUNC("pctx->p_ic_ops->co_clock_ctrl(0x%x), pWmtGenConf(0x%x)\n", pctx->p_ic_ops->co_clock_ctrl, pWmtGenConf);
	}
    osal_assert(NULL != pctx->p_ic_ops->sw_init);
    if (NULL != pctx->p_ic_ops->sw_init) {
        iRet = (*(pctx->p_ic_ops->sw_init))(&pctx->wmtHifConf);
    }
    else {
        WMT_ERR_FUNC("gMtkWmtCtx.p_ic_ops->sw_init is NULL\n");
        return -9;
    }
    if (iRet) {
        WMT_ERR_FUNC("gMtkWmtCtx.p_ic_ops->sw_init fail:%d\n", iRet);
        return -10;
    }

    //4 <10> set stp ready
    ctrlPa1 = WMT_STP_CONF_RDY; ctrlPa2 = 1;
    iRet = wmt_core_ctrl(WMT_CTRL_STP_CONF, &ctrlPa1, &ctrlPa2);

    return iRet;
}

static INT32 wmt_core_stp_deinit (VOID)
{
    INT32 iRet;
    UINT32 ctrlPa1;
    UINT32 ctrlPa2;

    WMT_DBG_FUNC(" start\n");

    if (NULL == gMtkWmtCtx.p_ic_ops) {
        WMT_WARN_FUNC("gMtkWmtCtx.p_ic_ops is NULL\n");
        goto deinit_ic_ops_done;
    }
    if (NULL != gMtkWmtCtx.p_ic_ops->sw_deinit) {
        iRet = (*(gMtkWmtCtx.p_ic_ops->sw_deinit))(&gMtkWmtCtx.wmtHifConf);
        /* unbind WMT-IC */
        gMtkWmtCtx.p_ic_ops= NULL;
    }
    else {
        WMT_ERR_FUNC("gMtkWmtCtx.p_ic_ops->sw_init is NULL\n");
    }

deinit_ic_ops_done:

    //4 <1> un-ready, disable, and close stp.
    ctrlPa1 = WMT_STP_CONF_RDY; ctrlPa2 = 0;
    iRet = wmt_core_ctrl(WMT_CTRL_STP_CONF, &ctrlPa1, &ctrlPa2);
    ctrlPa1 = WMT_STP_CONF_EN; ctrlPa2 = 0;
    iRet += wmt_core_ctrl(WMT_CTRL_STP_CONF, &ctrlPa1, &ctrlPa2);
    ctrlPa1 = 0; ctrlPa2 = 0;
    iRet += wmt_core_ctrl(WMT_CTRL_STP_CLOSE, &ctrlPa1, &ctrlPa2);

    //4 <1.1> turn off SDIO2 for common SDIO
    if (WMT_HIF_SDIO == gMtkWmtCtx.wmtHifConf.hifType) {
        ctrlPa1 = WMT_SDIO_FUNC_STP;
        ctrlPa2 = 0; /* turn off STP driver */
        iRet = wmt_core_ctrl(WMT_CTRL_SDIO_FUNC, &ctrlPa1, &ctrlPa2) ;
        if (iRet) {
            WMT_WARN_FUNC("turn off SDIO_FUNC_STP fail (%d)\n", iRet);
            /* Anyway, continue turning SDIO HW off */
        }
        else {
            WMT_INFO_FUNC("turn off SDIO_FUNC_STP ok \n");
        }

        ctrlPa1 = WMT_SDIO_SLOT_SDIO2;
        ctrlPa2 = 0; /* turn off SDIO2 slot */
        iRet = wmt_core_ctrl(WMT_CTRL_SDIO_HW, &ctrlPa1, &ctrlPa2) ;
        if (iRet) {
            WMT_WARN_FUNC("turn off SDIO2 HW fail (%d)\n", iRet);
            /* Anyway, continue turning STP SDIO to POWER OFF state*/
        }
        else {
            WMT_INFO_FUNC("turn off SDIO2 HW ok \n");
        }
        gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_SDIO2] = DRV_STS_POWER_OFF;
    }

    if (iRet) {
        WMT_WARN_FUNC("end with fail:%d\n", iRet);
    }

    return iRet;
}

static VOID
wmt_core_dump_func_state (
    CHAR *pSource
    )
{
   WMT_INFO_FUNC("[%s]status(b:%d f:%d g:%d w:%d lpbk:%d coredump:%d wmt:%d sd1:%d sd2:%d stp:%d)\n",
        (pSource == NULL ? (CHAR *)"CORE" : pSource),
        gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_BT],
        gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_FM],
        gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_GPS],
        gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WIFI],
        gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_LPBK],
        gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_COREDUMP],
        gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT],
        gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_SDIO1],
        gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_SDIO2],
        gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_STP]
        );
    return;

}

MTK_WCN_BOOL
wmt_core_patch_check (
    UINT32 u4PatchVer,
    UINT32 u4HwVer
    )
{
    if (MAJORNUM(u4HwVer) != MAJORNUM(u4PatchVer)) {
        /*major no. does not match*/
        WMT_ERR_FUNC("WMT-CORE: chip version(0x%d) does not match patch version(0x%d)\n", u4HwVer, u4PatchVer);
        return MTK_WCN_BOOL_FALSE;
    }
    return MTK_WCN_BOOL_TRUE;
}

static INT32
wmt_core_hw_check (VOID)
{
    UINT32 chipid;
    P_WMT_IC_OPS p_ops;
    INT32 iret;

    // 1. get chip id
    chipid = 0;
    WMT_LOUD_FUNC("before read hwcode (chip id)\n");
    iret = wmt_core_reg_rw_raw(0, GEN_HCR, &chipid, GEN_HCR_MASK); /* read 0x80000008 */
    if (iret) {
        WMT_ERR_FUNC("get hwcode (chip id) fail (%d)\n", iret);
        return -2;
    }
    WMT_INFO_FUNC("get hwcode (chip id) (0x%x)\n", chipid);

    // TODO:[ChangeFeature][George]: use a better way to select a correct ops table based on chip id
    switch (chipid) {
#if CFG_CORE_MT6620_SUPPORT
    case 0x6620:
        p_ops = &wmt_ic_ops_mt6620;
        break;
#endif
#if CFG_CORE_MT6628_SUPPORT
    case 0x6628:
        p_ops = &wmt_ic_ops_mt6628;
        break;
#endif
    default:
        p_ops = (P_WMT_IC_OPS)NULL;
        break;
    }

    if (NULL == p_ops) {
        WMT_ERR_FUNC("unsupported chip id (hw_code): 0x%x\n", chipid);
        return -3;
    }
    else if ( MTK_WCN_BOOL_FALSE == wmt_core_ic_ops_check(p_ops)) {
        WMT_ERR_FUNC("chip id(0x%x) with null operation fp: init(0x%p), deinit(0x%p), pin_ctrl(0x%p), ver_chk(0x%p)\n",
            chipid, p_ops->sw_init, p_ops->sw_deinit, p_ops->ic_pin_ctrl, p_ops->ic_ver_check);
        return -4;
    }
    WMT_DBG_FUNC("chip id(0x%x) fp: init(0x%p), deinit(0x%p), pin_ctrl(0x%p), ver_chk(0x%p)\n",
        chipid, p_ops->sw_init, p_ops->sw_deinit, p_ops->ic_pin_ctrl, p_ops->ic_ver_check);

    iret = p_ops->ic_ver_check();
    if (iret) {
        WMT_ERR_FUNC("chip id(0x%x) ver_check error:%d\n", chipid, iret);
        return -5;
    }

    WMT_INFO_FUNC("chip id(0x%x) ver_check ok\n", chipid);
    gMtkWmtCtx.p_ic_ops = p_ops;
    return 0;
}

static INT32 opfunc_hif_conf(P_WMT_OP pWmtOp)
{
    if (!(pWmtOp->u4InfoBit & WMT_OP_HIF_BIT)) {
        WMT_ERR_FUNC("WMT-CORE: no HIF_BIT in WMT_OP!\n");
        return -1;
    }

    if (gMtkWmtCtx.wmtInfoBit & WMT_OP_HIF_BIT) {
        WMT_ERR_FUNC("WMT-CORE: WMT HIF already exist. overwrite! old (%d), new(%d))\n",
            gMtkWmtCtx.wmtHifConf.hifType,
            pWmtOp->au4OpData[0]);
    }
    else {
        gMtkWmtCtx.wmtInfoBit |= WMT_OP_HIF_BIT;
        WMT_ERR_FUNC("WMT-CORE: WMT HIF info added\n");
    }

    osal_memcpy(&gMtkWmtCtx.wmtHifConf,
        &pWmtOp->au4OpData[0],
        osal_sizeof(gMtkWmtCtx.wmtHifConf));
    return 0;

}

static INT32 opfunc_pwr_on(P_WMT_OP pWmtOp)
{

    INT32 iRet;
    UINT32 ctrlPa1;
    UINT32 ctrlPa2;
    INT32 retry = WMT_PWRON_RTY_DFT;

    if (DRV_STS_POWER_OFF != gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT]) {
        WMT_ERR_FUNC("WMT-CORE: already powered on, WMT DRV_STS_[0x%x]\n",
            gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT]);
        osal_assert(0);
        return -1;
    }

    // TODO: [FixMe][GeorgeKuo]: clarify the following is reqiured or not!
    if (pWmtOp->u4InfoBit & WMT_OP_HIF_BIT) {
        opfunc_hif_conf(pWmtOp);
    }

pwr_on_rty:
    /* power on control */
    ctrlPa1 = 0;
    ctrlPa2 = 0;
    iRet = wmt_core_ctrl(WMT_CTRL_HW_PWR_ON, &ctrlPa1, &ctrlPa2) ;
    if (iRet) {
        WMT_ERR_FUNC("WMT-CORE: WMT_CTRL_HW_PWR_ON fail iRet(%d)\n", iRet);
        if (0 == retry--) {
            WMT_INFO_FUNC("WMT-CORE: retry (%d)\n", retry);
            goto pwr_on_rty;
        }
        return -1;
    }
    gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT] = DRV_STS_POWER_ON;

    /* init stp */
    iRet = wmt_core_stp_init();
    if (iRet) {
        WMT_ERR_FUNC("WMT-CORE: wmt_core_stp_init fail (%d)\n", iRet);
        osal_assert(0);

        /* deinit stp */
        iRet = wmt_core_stp_deinit();
        iRet = opfunc_pwr_off(pWmtOp);
        if (iRet) {
            WMT_ERR_FUNC("WMT-CORE: opfunc_pwr_off fail during pwr_on retry\n");
        }

        if (0 < retry--) {
            WMT_INFO_FUNC("WMT-CORE: retry (%d)\n", retry);
            goto pwr_on_rty;
        }
        iRet = -2;
        return iRet;
    }

    WMT_DBG_FUNC("WMT-CORE: WMT [FUNC_ON]\n");
    gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT] = DRV_STS_FUNC_ON;

    /* What to do when state is changed from POWER_OFF to POWER_ON?
         * 1. STP driver does s/w reset
         * 2. UART does 0xFF wake up
         * 3. SDIO does re-init command(changed to trigger by host)
         */
    return iRet;

}

static INT32 opfunc_pwr_off(P_WMT_OP pWmtOp)
{

    INT32 iRet;
    UINT32 ctrlPa1;
    UINT32 ctrlPa2;

    if (DRV_STS_POWER_OFF == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT]) {
        WMT_WARN_FUNC("WMT-CORE: WMT already off, WMT DRV_STS_[0x%x]\n", gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT]);
        osal_assert(0);
        return -1;
    }

    /* wmt and stp are initialized successfully */
    if (DRV_STS_FUNC_ON == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT]) {
        iRet = wmt_core_stp_deinit();
        if (iRet) {
            WMT_WARN_FUNC("wmt_core_stp_deinit fail (%d)\n", iRet);
            /*should let run to power down chip*/
        }
    }
    gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT] = DRV_STS_POWER_ON;

    /* power off control */
    ctrlPa1 = 0;
    ctrlPa2 = 0;
    iRet = wmt_core_ctrl(WMT_CTRL_HW_PWR_OFF, &ctrlPa1, &ctrlPa2) ;
    if (iRet) {
        WMT_WARN_FUNC("HW_PWR_OFF fail (%d)\n", iRet);
    }
    else {
        WMT_WARN_FUNC("HW_PWR_OFF ok\n");
    }

    /*anyway, set to POWER_OFF state */
    gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT] = DRV_STS_POWER_OFF;
    return iRet;

}

static INT32
opfunc_func_on (
    P_WMT_OP pWmtOp
    )
{
    INT32 iRet = -1;
    INT32 iPwrOffRet = -1;
    UINT32 drvType;
    UINT32 ctrlPa1;
    UINT32 ctrlPa2;

    drvType = pWmtOp->au4OpData[0];

    /* Check abnormal type */
    if (WMTDRV_TYPE_COREDUMP < drvType) {
        WMT_ERR_FUNC("abnormal Fun(%d)\n",
            drvType);
        osal_assert(0);
        return -1;
    }

    /* Check abnormal state */
    if ( (DRV_STS_POWER_OFF > gMtkWmtCtx.eDrvStatus[drvType])
        || (DRV_STS_MAX <= gMtkWmtCtx.eDrvStatus[drvType]) ) {
        WMT_ERR_FUNC("func(%d) status[0x%x] abnormal\n",
            drvType,
            gMtkWmtCtx.eDrvStatus[drvType]);
        osal_assert(0);
        return -2;
    }

    /* check if func already on */
    if (DRV_STS_FUNC_ON == gMtkWmtCtx.eDrvStatus[drvType]) {
        WMT_WARN_FUNC("func(%d) already on\n", drvType);
        return 0;
    }

    /* check if chip power on is needed */
    if (DRV_STS_FUNC_ON != gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT]) {
        iRet = opfunc_pwr_on(pWmtOp);

        if (iRet) {
            WMT_ERR_FUNC("func(%d) pwr_on fail(%d)\n", drvType, iRet);
            osal_assert(0);

            /* check all sub-func and do power off */
            return -3;
        }
    }

    if (WMTDRV_TYPE_WMT > drvType) {
        if (NULL != gpWmtFuncOps[drvType] && NULL != gpWmtFuncOps[drvType]->func_on)
        {

            /* special handling for Wi-Fi */
            if (WMTDRV_TYPE_WIFI == drvType) {
                if (WMT_HIF_UART == gMtkWmtCtx.wmtHifConf.hifType) {
                    ctrlPa1 = WMT_SDIO_SLOT_SDIO1;
                    ctrlPa2 = 1; /* turn on SDIO1 slot */
                    iRet = wmt_core_ctrl(WMT_CTRL_SDIO_HW, &ctrlPa1, &ctrlPa2) ;
                    if (iRet) {
                           WMT_ERR_FUNC("turn on SLOT_SDIO1 fail (%d)\n", iRet);
                           osal_assert(0);
                        /* check all sub-func and do power off */
                    }
                    else {
                          gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_SDIO1] = DRV_STS_FUNC_ON;
                    }
                }
                else if (WMT_HIF_SDIO == gMtkWmtCtx.wmtHifConf.hifType) {
                       if (DRV_STS_FUNC_ON == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_SDIO2]) {
                           WMT_DBG_FUNC("SLOT_SDIO2 ready for WIFI\n");
                    }
                       else {
                       /* SDIO2 slot power shall be either turned on in STP init
                                          * procedures already, or failed in STP init before. Here is
                                          * the assert condition.
                                        **/
                        WMT_ERR_FUNC("WMT-CORE: turn on Wi-Fi in comm SDIO2 but SDIO in FUNC_OFF state(0x%x)\n", gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_SDIO2]);
                        osal_assert(0);
                        return -4;
                    }
                }
                else {
                    WMT_ERR_FUNC("WMT-CORE: error, not implemented yet gMtkWmtCtx.wmtHifConf.hifType: 0x%x, unspecified wifi_hif\n", gMtkWmtCtx.wmtHifConf.hifType);
                    // TODO:  Wi-Fi/WMT uses other interfaces. NOT IMPLEMENTED YET!
                }

            }
            iRet = (*(gpWmtFuncOps[drvType]->func_on))(gMtkWmtCtx.p_ic_ops, wmt_conf_get_cfg());
            if (0 != iRet)
            {
                if (WMTDRV_TYPE_WIFI == drvType && WMT_HIF_UART == gMtkWmtCtx.wmtHifConf.hifType) {
                    /*need to power SDIO off when Power on Wi-Fi fail, in case of power leakage and right SDIO power status maintain*/
                    ctrlPa1 = WMT_SDIO_SLOT_SDIO1;
                    ctrlPa2 = 0; /* turn off SDIO1 slot */
                    wmt_core_ctrl(WMT_CTRL_SDIO_HW, &ctrlPa1, &ctrlPa2) ;
                    //does not need to check turn off result
                    gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_SDIO1] = DRV_STS_POWER_OFF;
                    
                }
                gMtkWmtCtx.eDrvStatus[drvType] = DRV_STS_POWER_OFF;
            }
            else
            {
                gMtkWmtCtx.eDrvStatus[drvType] = DRV_STS_FUNC_ON;
            }
        }
        else
        {
            WMT_WARN_FUNC("WMT-CORE: ops for type(%d) not found\n", drvType);
            iRet = -5;
        }
    }
    else
    {
        if (WMTDRV_TYPE_LPBK == drvType) {
            gMtkWmtCtx.eDrvStatus[drvType] = DRV_STS_FUNC_ON;
        }
        else if (WMTDRV_TYPE_COREDUMP == drvType) {
            gMtkWmtCtx.eDrvStatus[drvType] = DRV_STS_FUNC_ON;
        };
        iRet = 0;
    }

    if (iRet) {
        WMT_ERR_FUNC("WMT-CORE:type(0x%x) function on failed, ret(%d)\n", drvType, iRet);
        osal_assert(0);
        //FIX-ME:[Chaozhong Liang], Error handling? check subsystem state and do pwr off if necessary?
        /* check all sub-func and do power off */
       if (  (DRV_STS_POWER_OFF == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_BT]) &&
            (DRV_STS_POWER_OFF == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_GPS]) &&
            (DRV_STS_POWER_OFF == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_FM]) &&
            (DRV_STS_POWER_OFF == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WIFI]) &&
            (DRV_STS_POWER_OFF == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_LPBK]) &&
            (DRV_STS_POWER_OFF == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_COREDUMP])) {
            WMT_INFO_FUNC("WMT-CORE:Fun(%d) [POWER_OFF] and power down chip\n", drvType);

            iPwrOffRet = opfunc_pwr_off(pWmtOp);
            if (iPwrOffRet) {
                WMT_ERR_FUNC("WMT-CORE: wmt_pwr_off fail(%d) when turn off func(%d)\n", iPwrOffRet, drvType);
                osal_assert(0);
            }
        }
        return iRet;
    }

    wmt_core_dump_func_state("AF FUNC ON");

    return 0;
}

static INT32 opfunc_func_off(P_WMT_OP pWmtOp)
{

    INT32 iRet = -1;
    UINT32 drvType;
    UINT32 ctrlPa1;
    UINT32 ctrlPa2;

    drvType = pWmtOp->au4OpData[0];
    /* Check abnormal type */
    if (WMTDRV_TYPE_COREDUMP < drvType) {
        WMT_ERR_FUNC("WMT-CORE: abnormal Fun(%d) in wmt_func_off \n", drvType);
        osal_assert(0);
        return -1;
    }

    /* Check abnormal state */
    if (DRV_STS_MAX <= gMtkWmtCtx.eDrvStatus[drvType]) {
        WMT_ERR_FUNC("WMT-CORE: Fun(%d) DRV_STS_[0x%x] abnormal in wmt_func_off \n",
            drvType,
            gMtkWmtCtx.eDrvStatus[drvType]);
        osal_assert(0);
        return -2;
    }

    if (DRV_STS_FUNC_ON != gMtkWmtCtx.eDrvStatus[drvType]) {
        WMT_WARN_FUNC("WMT-CORE: Fun(%d) DRV_STS_[0x%x] already non-FUN_ON in wmt_func_off \n",
            drvType,
            gMtkWmtCtx.eDrvStatus[drvType]);
        //needs to check 4 subsystem's state?
        return 0;
    }else if (WMTDRV_TYPE_WMT > drvType) {
        if (NULL != gpWmtFuncOps[drvType] && NULL != gpWmtFuncOps[drvType]->func_off)
        {
            iRet = (*(gpWmtFuncOps[drvType]->func_off))(gMtkWmtCtx.p_ic_ops, wmt_conf_get_cfg());

            if (WMTDRV_TYPE_WIFI == drvType && WMT_HIF_UART == gMtkWmtCtx.wmtHifConf.hifType) {
                UINT32 iRet = 0;
                ctrlPa1 = WMT_SDIO_SLOT_SDIO1;
                ctrlPa2 = 0; /* turn off SDIO1 slot */
                iRet = wmt_core_ctrl(WMT_CTRL_SDIO_HW, &ctrlPa1, &ctrlPa2) ;
                if (iRet) {
                    WMT_ERR_FUNC("WMT-CORE: turn on SLOT_SDIO1 fail (%d)\n", iRet);
                    osal_assert(0);
                    /* check all sub-func and do power off */
                }
                //Anyway, turn SDIO1 state to POWER_OFF state
                gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_SDIO1] = DRV_STS_POWER_OFF;
            }
        }
        else
        {
            WMT_WARN_FUNC("WMT-CORE: ops for type(%d) not found\n", drvType);
            iRet = -3;
        }
    } else {
        if (WMTDRV_TYPE_LPBK == drvType) {
            gMtkWmtCtx.eDrvStatus[drvType] = DRV_STS_POWER_OFF;
        }else if (WMTDRV_TYPE_COREDUMP == drvType) {
            gMtkWmtCtx.eDrvStatus[drvType] = DRV_STS_POWER_OFF;
        }
        iRet = 0;
    }

    //shall we put device state to POWER_OFF state when fail?
    gMtkWmtCtx.eDrvStatus[drvType] = DRV_STS_POWER_OFF;
    if (iRet) {
        WMT_ERR_FUNC("WMT-CORE: type(0x%x) function off failed, ret(%d)\n", drvType, iRet);
        osal_assert(0);
        //no matter subsystem function control fail or not, chip should be powered off when no subsystem is active
        //return iRet;
    }


   /* check all sub-func and do power off */
   if (  (DRV_STS_POWER_OFF == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_BT]) &&
        (DRV_STS_POWER_OFF == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_GPS]) &&
        (DRV_STS_POWER_OFF == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_FM]) &&
        (DRV_STS_POWER_OFF == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WIFI]) &&
        (DRV_STS_POWER_OFF == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_LPBK]) &&
        (DRV_STS_POWER_OFF == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_COREDUMP])) {
        WMT_INFO_FUNC("WMT-CORE:Fun(%d) [POWER_OFF] and power down chip\n", drvType);

        iRet = opfunc_pwr_off(pWmtOp);
        if (iRet) {
            WMT_ERR_FUNC("WMT-CORE: wmt_pwr_off fail(%d) when turn off func(%d)\n", iRet, drvType);
            osal_assert(0);
        }
    }

    wmt_core_dump_func_state("AF FUNC OFF");
    return iRet;
}

// TODO:[ChangeFeature][George] is this OP obsoleted?
static INT32
opfunc_reg_rw (
    P_WMT_OP pWmtOp
    )
{
    INT32 iret;

    if (DRV_STS_FUNC_ON != gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT]) {
        WMT_ERR_FUNC("reg_rw when WMT is powered off\n");
        return -1;
    }
    iret = wmt_core_reg_rw_raw(pWmtOp->au4OpData[0],
        pWmtOp->au4OpData[1],
        (PUINT32)pWmtOp->au4OpData[2],
        pWmtOp->au4OpData[3]);

    return iret;
}

static INT32
opfunc_exit (
    P_WMT_OP pWmtOp
    )
{
    // TODO: [FixMe][George] is ok to leave this function empty???
    WMT_WARN_FUNC("EMPTY FUNCTION\n");
    return 0;
}

static INT32 opfunc_pwr_sv(P_WMT_OP pWmtOp)
{
    INT32 ret = -1;
    UINT32 u4_result;
    UINT32 evt_len;
    UINT8 evt_buf[16] = {0};

typedef INT32 (*STP_PSM_CB)(INT32);
    STP_PSM_CB  psm_cb;

    if (SLEEP == pWmtOp->au4OpData[0]) {
        WMT_DBG_FUNC("**** Send sleep command\n");
		//mtk_wcn_stp_set_psm_state(ACT_INACT);
        //(*kal_stp_flush_rx)(WMT_TASK_INDX);
        ret = wmt_core_tx(&WMT_SLEEP_CMD[0], sizeof(WMT_SLEEP_CMD), &u4_result, 0);
        if (ret || (u4_result != sizeof(WMT_SLEEP_CMD))) {
            WMT_ERR_FUNC("wmt_core: SLEEP_CMD ret(%d) cmd len err(%d, %d) ", ret, u4_result, sizeof(WMT_SLEEP_CMD));
            goto pwr_sv_done;
        }

        evt_len = sizeof(WMT_SLEEP_EVT);
        ret = wmt_core_rx(evt_buf, evt_len, &u4_result);
        if (ret || (u4_result != evt_len))
        {
            wmt_core_rx_flush(WMT_TASK_INDX);
            WMT_ERR_FUNC("wmt_core: read SLEEP_EVT fail(%d) len(%d, %d)", ret, u4_result, evt_len);
            mtk_wcn_stp_dbg_dump_package();
            goto pwr_sv_done;
        }

        if (osal_memcmp(evt_buf, WMT_SLEEP_EVT, sizeof(WMT_SLEEP_EVT)) != 0)
        {
            WMT_ERR_FUNC("wmt_core: compare WMT_SLEEP_EVT error\n");
            wmt_core_rx_flush(WMT_TASK_INDX);
            WMT_ERR_FUNC("wmt_core: rx(%d):[%02X,%02X,%02X,%02X,%02X,%02X] exp(%d):[%02X,%02X,%02X,%02X,%02X,%02X]\n",
                u4_result, evt_buf[0], evt_buf[1], evt_buf[2], evt_buf[3], evt_buf[4], evt_buf[5],
                sizeof(WMT_SLEEP_EVT), WMT_SLEEP_EVT[0], WMT_SLEEP_EVT[1],
                WMT_SLEEP_EVT[2], WMT_SLEEP_EVT[3], WMT_SLEEP_EVT[4], WMT_SLEEP_EVT[5]);
            mtk_wcn_stp_dbg_dump_package();
            goto pwr_sv_done;
        }
        else
        {
            WMT_INFO_FUNC("Send sleep command OK!\n");
        }
    }
    else if (pWmtOp->au4OpData[0] == WAKEUP)
    {
        WMT_DBG_FUNC("**** Send wakeup command\n");
        ret = wmt_core_tx(WMT_WAKEUP_CMD, sizeof(WMT_WAKEUP_CMD), &u4_result, 1);

        if (ret || (u4_result != sizeof(WMT_WAKEUP_CMD)))
        {
            wmt_core_rx_flush(WMT_TASK_INDX);
            WMT_ERR_FUNC("wmt_core: WAKEUP_CMD ret(%d) cmd len err(%d, %d) ", ret, u4_result, sizeof(WMT_WAKEUP_CMD));
            goto pwr_sv_done;
        }

        evt_len = sizeof(WMT_WAKEUP_EVT);
        ret = wmt_core_rx(evt_buf, evt_len, &u4_result);
        if (ret || (u4_result != evt_len))
        {
            WMT_ERR_FUNC("wmt_core: read WAKEUP_EVT fail(%d) len(%d, %d)", ret, u4_result, evt_len);
            mtk_wcn_stp_dbg_dump_package();
            goto pwr_sv_done;
        }

        if (osal_memcmp(evt_buf, WMT_WAKEUP_EVT, sizeof(WMT_WAKEUP_EVT)) != 0)
        {
            WMT_ERR_FUNC("wmt_core: compare WMT_WAKEUP_EVT error\n");
            wmt_core_rx_flush(WMT_TASK_INDX);
            WMT_ERR_FUNC("wmt_core: rx(%d):[%02X,%02X,%02X,%02X,%02X,%02X] exp(%d):[%02X,%02X,%02X,%02X,%02X,%02X]\n",
                u4_result, evt_buf[0], evt_buf[1], evt_buf[2], evt_buf[3], evt_buf[4], evt_buf[5],
                sizeof(WMT_WAKEUP_EVT), WMT_WAKEUP_EVT[0], WMT_WAKEUP_EVT[1],
                WMT_WAKEUP_EVT[2], WMT_WAKEUP_EVT[3], WMT_WAKEUP_EVT[4], WMT_WAKEUP_EVT[5]);
            mtk_wcn_stp_dbg_dump_package();
            goto pwr_sv_done;
        }
        else
        {
            WMT_INFO_FUNC("Send wakeup command OK!\n");
        }
    } else if (pWmtOp->au4OpData[0] == HOST_AWAKE){

        WMT_DBG_FUNC("**** Send host awake command\n");

        psm_cb = (STP_PSM_CB)pWmtOp->au4OpData[1];
        //(*kal_stp_flush_rx)(WMT_TASK_INDX);
        ret = wmt_core_tx(WMT_HOST_AWAKE_CMD, sizeof(WMT_HOST_AWAKE_CMD), &u4_result, 0);
        if (ret || (u4_result != sizeof(WMT_HOST_AWAKE_CMD)))
        {
            WMT_ERR_FUNC("wmt_core: HOST_AWAKE_CMD ret(%d) cmd len err(%d, %d) ", ret, u4_result, sizeof(WMT_HOST_AWAKE_CMD));
            goto pwr_sv_done;
        }

        evt_len = sizeof(WMT_HOST_AWAKE_EVT);
        ret = wmt_core_rx(evt_buf, evt_len, &u4_result);
        if (ret || (u4_result != evt_len))
        {
            wmt_core_rx_flush(WMT_TASK_INDX);
            WMT_ERR_FUNC("wmt_core: read HOST_AWAKE_EVT fail(%d) len(%d, %d)", ret, u4_result, evt_len);
            mtk_wcn_stp_dbg_dump_package();
            goto pwr_sv_done;
        }

        if (osal_memcmp(evt_buf, WMT_HOST_AWAKE_EVT, sizeof(WMT_HOST_AWAKE_EVT)) != 0)
        {
            WMT_ERR_FUNC("wmt_core: compare WMT_HOST_AWAKE_EVT error\n");
            wmt_core_rx_flush(WMT_TASK_INDX);
            WMT_ERR_FUNC("wmt_core: rx(%d):[%02X,%02X,%02X,%02X,%02X,%02X] exp(%d):[%02X,%02X,%02X,%02X,%02X,%02X]\n",
                u4_result, evt_buf[0], evt_buf[1], evt_buf[2], evt_buf[3], evt_buf[4], evt_buf[5],
                sizeof(WMT_HOST_AWAKE_EVT), WMT_HOST_AWAKE_EVT[0], WMT_HOST_AWAKE_EVT[1],
                WMT_HOST_AWAKE_EVT[2], WMT_HOST_AWAKE_EVT[3], WMT_HOST_AWAKE_EVT[4], WMT_HOST_AWAKE_EVT[5]);
            mtk_wcn_stp_dbg_dump_package();
            //goto pwr_sv_done;
        }
        else
        {
           WMT_INFO_FUNC("Send host awake command OK!\n");
        }
    }
pwr_sv_done:

    if (pWmtOp->au4OpData[0] < STP_PSM_MAX_ACTION) {
        psm_cb = (STP_PSM_CB)pWmtOp->au4OpData[1];
        WMT_DBG_FUNC("Do STP-CB! %d %p\n", pWmtOp->au4OpData[0], (PVOID)pWmtOp->au4OpData[1]);
        if (NULL != psm_cb) {
            psm_cb(pWmtOp->au4OpData[0]);
        }
        else {
            WMT_ERR_FUNC("fatal error !!!, psm_cb = %p, god, someone must have corrupted our memory.\n", psm_cb);
        }
    }

    return ret;
}

static INT32 opfunc_dsns(P_WMT_OP pWmtOp)
{

    INT32 iRet = -1;
    UINT32 u4Res;
    UINT32 evtLen;
    UINT8 evtBuf[16] = {0};

    WMT_DSNS_CMD[4] = pWmtOp->au4OpData[0];
    WMT_DSNS_CMD[5] = pWmtOp->au4OpData[1];

    /* send command*/
    //iRet = (*kal_stp_tx)(WMT_DSNS_CMD, osal_sizeof(WMT_DSNS_CMD), &u4Res);
    iRet = wmt_core_tx((PUINT8)WMT_DSNS_CMD, osal_sizeof(WMT_DSNS_CMD), &u4Res, MTK_WCN_BOOL_FALSE);
    if (iRet || (u4Res != osal_sizeof(WMT_DSNS_CMD))) {
        WMT_ERR_FUNC("WMT-CORE: DSNS_CMD iRet(%d) cmd len err(%d, %d) \n", iRet, u4Res, osal_sizeof(WMT_DSNS_CMD));
        return iRet;
    }

    evtLen = osal_sizeof(WMT_DSNS_EVT);

    iRet = wmt_core_rx(evtBuf, evtLen, &u4Res);
    if (iRet || (u4Res != evtLen)) {
        WMT_ERR_FUNC("WMT-CORE: read DSNS_EVT fail(%d) len(%d, %d)\n", iRet, u4Res, evtLen);
        mtk_wcn_stp_dbg_dump_package();
        return iRet;
    }

    if (osal_memcmp(evtBuf, WMT_DSNS_EVT, osal_sizeof(WMT_DSNS_EVT)) != 0) {
        WMT_ERR_FUNC("WMT-CORE: compare WMT_DSNS_EVT error\n");
        WMT_ERR_FUNC("WMT-CORE: rx(%d):[%02X,%02X,%02X,%02X,%02X] exp(%d):[%02X,%02X,%02X,%02X,%02X]\n",
        u4Res, evtBuf[0], evtBuf[1], evtBuf[2], evtBuf[3], evtBuf[4],
        osal_sizeof(WMT_DSNS_EVT), WMT_DSNS_EVT[0], WMT_DSNS_EVT[1], WMT_DSNS_EVT[2], WMT_DSNS_EVT[3], WMT_DSNS_EVT[4]);
    } else {
        WMT_INFO_FUNC("Send WMT_DSNS_CMD command OK!\n");
    }

    return iRet;
}


static INT32 opfunc_lpbk(P_WMT_OP pWmtOp)
{

    INT32 iRet;
    UINT32 u4WrittenSize = 0;
    UINT32 u4ReadSize = 0;
    UINT32 buf_length = 0;
    UINT32 *pbuffer = NULL;
    UINT16 len_in_cmd;
    //UINT32 offset;
    UINT8 WMT_TEST_LPBK_CMD[] = {0x1, 0x2, 0x0, 0x0, 0x7};
    UINT8 WMT_TEST_LPBK_EVT[] = {0x2, 0x2, 0x0, 0x0, 0x0};

    //UINT8 lpbk_buf[1024 + 5] = {0};
    MTK_WCN_BOOL fgFail;
    buf_length = pWmtOp->au4OpData[0];    //packet length
    pbuffer = (VOID *)pWmtOp->au4OpData[1]; //packet buffer pointer
    WMT_DBG_FUNC("WMT-CORE: -->wmt_do_lpbk \n");
    /*check if WMTDRV_TYPE_LPBK function is already on */
    if (DRV_STS_FUNC_ON!= gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_LPBK] || buf_length + osal_sizeof(WMT_TEST_LPBK_CMD) > osal_sizeof(gLpbkBuf)) {
            WMT_ERR_FUNC("WMT-CORE: abnormal LPBK in wmt_do_lpbk\n");
            osal_assert(0);
            return -2;
    }
    /*package loopback for STP*/

    // init buffer
    osal_memset(gLpbkBuf, 0, osal_sizeof(gLpbkBuf));

    len_in_cmd = buf_length + 1; /* add flag field */

    osal_memcpy(&WMT_TEST_LPBK_CMD[2], &len_in_cmd, 2);
    osal_memcpy(&WMT_TEST_LPBK_EVT[2], &len_in_cmd, 2);

    // wmt cmd
    osal_memcpy(gLpbkBuf, WMT_TEST_LPBK_CMD, osal_sizeof(WMT_TEST_LPBK_CMD));
    osal_memcpy(gLpbkBuf + osal_sizeof(WMT_TEST_LPBK_CMD), pbuffer, buf_length);

    do {
        fgFail = MTK_WCN_BOOL_TRUE;
        /*send packet through STP*/

        //iRet = (*kal_stp_tx)((PUINT8)gLpbkBuf, osal_sizeof(WMT_TEST_LPBK_CMD) + buf_length, &u4WrittenSize);
        iRet = wmt_core_tx((PUINT8)gLpbkBuf, (osal_sizeof(WMT_TEST_LPBK_CMD) + buf_length), &u4WrittenSize, MTK_WCN_BOOL_FALSE);
        if (iRet) {
            WMT_ERR_FUNC("opfunc_lpbk wmt_core_tx failed \n");
            break;
        }
        /*receive firmware response from STP*/
        iRet = wmt_core_rx((PUINT8)gLpbkBuf, (osal_sizeof(WMT_TEST_LPBK_EVT) + buf_length), &u4ReadSize);
        if (iRet) {
            WMT_ERR_FUNC("opfunc_lpbk wmt_core_rx failed \n");
            break;
        }
        /*check if loopback response ok or not*/
        if (u4ReadSize != (osal_sizeof(WMT_TEST_LPBK_CMD) + buf_length)) {
            WMT_ERR_FUNC("lpbk event read size wrong(%d, %d) \n", u4ReadSize, (osal_sizeof(WMT_TEST_LPBK_CMD) + buf_length));
            break;
        }
        if (osal_memcmp(WMT_TEST_LPBK_EVT, gLpbkBuf, osal_sizeof(WMT_TEST_LPBK_EVT))) {
            WMT_ERR_FUNC("WMT-CORE WMT_TEST_LPBK_EVT error! read len %d [%02x,%02x,%02x,%02x,%02x] \n",
                (INT32)u4ReadSize,
                gLpbkBuf[0], gLpbkBuf[1], gLpbkBuf[2], gLpbkBuf[3], gLpbkBuf[4]
            );
            break;
        }
        pWmtOp->au4OpData[0] = u4ReadSize - osal_sizeof(WMT_TEST_LPBK_EVT);
        osal_memcpy((VOID *)pWmtOp->au4OpData[1], gLpbkBuf + osal_sizeof(WMT_TEST_LPBK_CMD), buf_length);
        fgFail = MTK_WCN_BOOL_FALSE;
    }while(0);
    /*return result*/
    //WMT_DBG_FUNC("WMT-CORE: <--wmt_do_lpbk, fgFail = %d \n", fgFail);
    return fgFail;

}

static INT32 opfunc_cmd_test(P_WMT_OP pWmtOp)
{

    INT32  iRet = 0;
    UINT32 cmdNo = 0;
    UINT32 cmdNoPa = 0;

    UINT8   tstCmd[64];
    UINT8   tstEvt[64];
    UINT8   tstEvtTmp[64];
    UINT32  u4Res;
    UINT32  tstCmdSz = 0;
    UINT32  tstEvtSz = 0;

    UINT8 *pRes = NULL;
    UINT32 resBufRoom = 0;
    /*test command list*/
    /*1*/
    UINT8  WMT_ASSERT_CMD[] = {0x01, 0x02, 0x01, 0x00, 0x08};
    UINT8  WMT_ASSERT_EVT[] = {0x02, 0x02, 0x00, 0x00, 0x00};
    UINT8  WMT_EXCEPTION_CMD[] = {0x01, 0x02, 0x01, 0x00, 0x09};
    UINT8  WMT_EXCEPTION_EVT[] = {0x02, 0x02, 0x00, 0x00, 0x00};
    /*2*/
    UINT8  WMT_COEXDBG_CMD[] = {0x01,0x10,0x02,0x00,
        0x08,
        0xAA /*Debugging Parameter*/
        };
    UINT8  WMT_COEXDBG_1_EVT[] = {0x02,0x10,0x05,0x00,
        0x00,
        0xAA,0xAA,0xAA,0xAA /*event content*/
        };
    UINT8  WMT_COEXDBG_2_EVT[] = {0x02,0x10,0x07,0x00,
        0x00,
        0xAA,0xAA,0xAA,0xAA,0xBB,0xBB /*event content*/
        };
    UINT8  WMT_COEXDBG_3_EVT[] = {0x02,0x10,0x0B,0x00,
        0x00,
        0xAA,0xAA,0xAA,0xAA,0xBB,0xBB,0xBB,0xBB /*event content*/
        };
    /*test command list -end*/

    cmdNo = pWmtOp->au4OpData[0];

    WMT_INFO_FUNC("Send Test command %d!\n", cmdNo);
    if (cmdNo == 0) {
        /*dead command*/
        WMT_INFO_FUNC("Send Assert command !\n");
        tstCmdSz = osal_sizeof(WMT_ASSERT_CMD);
        tstEvtSz = osal_sizeof(WMT_ASSERT_EVT);
        osal_memcpy(tstCmd, WMT_ASSERT_CMD, tstCmdSz);
        osal_memcpy(tstEvt, WMT_ASSERT_EVT, tstEvtSz);
    } else if (cmdNo == 1) {
        /*dead command*/
        WMT_INFO_FUNC("Send Exception command !\n");
        tstCmdSz = osal_sizeof(WMT_EXCEPTION_CMD);
        tstEvtSz = osal_sizeof(WMT_EXCEPTION_EVT);
        osal_memcpy(tstCmd, WMT_EXCEPTION_CMD, tstCmdSz);
        osal_memcpy(tstEvt, WMT_EXCEPTION_EVT, tstEvtSz);
    } else if (cmdNo == 2) {
        cmdNoPa = pWmtOp->au4OpData[1];
        pRes = (UINT8 *)pWmtOp->au4OpData[2];
        resBufRoom = pWmtOp->au4OpData[3];
        if ((cmdNoPa >= 0x0) && (cmdNoPa <= 0xf)) {
            WMT_INFO_FUNC("Send Coexistence Debug command [0x%x]!\n", cmdNoPa);
            tstCmdSz = osal_sizeof(WMT_COEXDBG_CMD);
            osal_memcpy(tstCmd, WMT_COEXDBG_CMD, tstCmdSz);
            if (tstCmdSz > 5) {
                tstCmd[5] = cmdNoPa;
            }

            /*setup the expected event length*/
            if (cmdNoPa >= 0x0 && cmdNoPa <= 0x4) {
                tstEvtSz = osal_sizeof(WMT_COEXDBG_1_EVT);
                osal_memcpy(tstEvt, WMT_COEXDBG_1_EVT, tstEvtSz);
            }else if (cmdNoPa == 0x5) {
                tstEvtSz = osal_sizeof(WMT_COEXDBG_2_EVT);
                osal_memcpy(tstEvt, WMT_COEXDBG_2_EVT, tstEvtSz);
            }else if (cmdNoPa >= 0x6 && cmdNoPa <= 0xf) {
                tstEvtSz = osal_sizeof(WMT_COEXDBG_3_EVT);
                osal_memcpy(tstEvt, WMT_COEXDBG_3_EVT, tstEvtSz);
            }
            else{

            }
        }else{
            WMT_ERR_FUNC("cmdNoPa is wrong\n");
            return iRet;
        }
    } else {
         /*Placed youer test WMT command here, easiler to integrate and test with F/W side*/
    }

    /* send command*/
    //iRet = (*kal_stp_tx)(tstCmd, tstCmdSz, &u4Res);
    iRet = wmt_core_tx((PUINT8)tstCmd, tstCmdSz, &u4Res, MTK_WCN_BOOL_FALSE);
    if (iRet || (u4Res != tstCmdSz)) {
        WMT_ERR_FUNC("WMT-CORE: wmt_cmd_test iRet(%d) cmd len err(%d, %d) \n", iRet, u4Res, tstCmdSz);
        return -1;
    }

    if ((cmdNo == 0) || (cmdNo == 1)) {
        WMT_INFO_FUNC("WMT-CORE: not to rx event for assert command\n");
        return 0;
    }

    iRet = wmt_core_rx(tstEvtTmp, tstEvtSz, &u4Res);

    /*Event Post Handling*/
    if (cmdNo == 2) {
        WMT_INFO_FUNC("#=========================================================#\n");
        WMT_INFO_FUNC("coext debugging id = %d", cmdNoPa);
        if (tstEvtSz > 5) {
            wmt_core_dump_data(&tstEvtTmp[5], "coex debugging ", tstEvtSz - 5);
        }else {
            WMT_ERR_FUNC("error coex debugging event\n");
        }
        /*put response to buffer for shell to read*/
        if (pRes != NULL && resBufRoom > 0)
        {
            pWmtOp->au4OpData[3] = resBufRoom < tstEvtSz - 5 ? resBufRoom : tstEvtSz - 5;
            osal_memcpy(pRes, &tstEvtTmp[5], pWmtOp->au4OpData[3]);
        }
        else
        {
            pWmtOp->au4OpData[3] = 0;
        }
        WMT_INFO_FUNC("#=========================================================#\n");
    }

    return iRet;

}

static INT32 opfunc_hw_rst(P_WMT_OP pWmtOp)
{

    INT32 iRet = -1;
    UINT32 ctrlPa1;
    UINT32 ctrlPa2;

    wmt_core_dump_func_state("BE HW RST");
    /*-->Reset WMT  data structure*/
    gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_BT]   = DRV_STS_POWER_OFF;
    gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_FM]   = DRV_STS_POWER_OFF;
    gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_GPS]  = DRV_STS_POWER_OFF;
    //gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WIFI] = DRV_STS_POWER_OFF;
    gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_LPBK] = DRV_STS_POWER_OFF;
    //gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_SDIO1]= DRV_STS_POWER_OFF;
    //gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_SDIO2]= DRV_STS_POWER_OFF;
    gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_STP]  = DRV_STS_POWER_OFF;

    /* if wmt is poweroff, we need poweron chip first*/
    if (DRV_STS_POWER_OFF == gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT]) {
        WMT_WARN_FUNC("WMT-CORE: WMT is off, need re-poweron\n");
        /* power on control */
        ctrlPa1 = 0;
        ctrlPa2 = 0;
        iRet = wmt_core_ctrl(WMT_CTRL_HW_PWR_ON, &ctrlPa1, &ctrlPa2) ;
        if (iRet) {
            WMT_ERR_FUNC("WMT-CORE: WMT_CTRL_HW_PWR_ON fail iRet(%d)\n", iRet);
            return -1;
        }
        gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT] = DRV_STS_POWER_ON;
    }
    
    /*--> reset SDIO fucntion/slot additionally if wifi ON*/
    if (gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WIFI] == DRV_STS_FUNC_ON) {
            ctrlPa1 = WMT_SDIO_FUNC_WIFI;
            ctrlPa2 = 0;     /* turn off Wi-Fi driver */
            iRet = wmt_core_ctrl(WMT_CTRL_SDIO_FUNC, &ctrlPa1, &ctrlPa2) ;
            if (iRet) {
                WMT_ERR_FUNC("WMT-CORE: turn off SDIO_WIFI func fail (%d)\n", iRet);

                /* check all sub-func and do power off */
            } else {
                WMT_INFO_FUNC("wmt core: turn off SDIO WIFI func ok!!\n");
            }
            //Anyway, turn off Wi-Fi Function
            gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WIFI] = DRV_STS_POWER_OFF;

        if (WMT_HIF_UART == gMtkWmtCtx.wmtHifConf.hifType) {
            ctrlPa1 = WMT_SDIO_SLOT_SDIO1;
            ctrlPa2 = 0;     /* turn off SDIO1 slot */
            iRet = wmt_core_ctrl(WMT_CTRL_SDIO_HW, &ctrlPa1, &ctrlPa2) ;
            if (iRet) {
                WMT_ERR_FUNC("WMT-CORE: turn off SLOT_SDIO1 fail (%d)\n", iRet);
                osal_assert(0);

                /* check all sub-func and do power off */
            } else {
                WMT_INFO_FUNC("WMT-CORE: turn off SLOT_SDIO1 successfully (%d)\n", iRet);
            }
            gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_SDIO1] = DRV_STS_POWER_OFF;
        }
        gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WIFI] = DRV_STS_POWER_OFF;
    }

    if (WMT_HIF_SDIO == gMtkWmtCtx.wmtHifConf.hifType) {
        ctrlPa1 = WMT_SDIO_FUNC_STP;
        ctrlPa2 = 0; /* turn off STP driver */
        iRet = wmt_core_ctrl(WMT_CTRL_SDIO_FUNC, &ctrlPa1, &ctrlPa2) ;
        if (iRet) {
            WMT_ERR_FUNC("WMT-CORE: turn off SDIO_FUNC_STP func fail (%d)\n", iRet);

            /* check all sub-func and do power off */
            //goto stp_deinit_done;
        } else {
            WMT_INFO_FUNC("WMT-CORE: turn off SDIO_FUNC_STP func successfully (%d)\n", iRet);
        }

        ctrlPa1 = WMT_SDIO_SLOT_SDIO2;
        ctrlPa2 = 0; /* turn off SDIO2 slot */
        iRet = wmt_core_ctrl(WMT_CTRL_SDIO_HW, &ctrlPa1, &ctrlPa2) ;
        if (iRet) {
            WMT_ERR_FUNC("WMT-CORE: turn off SLOT_SDIO2 fail (%d)\n", iRet);
            osal_assert(0);

            /* check all sub-func and do power off */
            //goto stp_deinit_done;
        } else {
            WMT_INFO_FUNC("WMT-CORE: turn off SLOT_SDIO2 successfully (%d)\n", iRet);
        }
            gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_SDIO2] = DRV_STS_POWER_OFF;
    }
#if 0
    /*<4>Power off Combo chip*/
    ctrlPa1 = 0; ctrlPa2 = 0;
    iRet = wmt_core_ctrl(WMT_CTRL_HW_RST, &ctrlPa1, &ctrlPa2) ;
    if (iRet) {
        WMT_ERR_FUNC("WMT-CORE: [HW RST] WMT_CTRL_POWER_OFF fail (%d)", iRet);
    } else {
        WMT_INFO_FUNC("WMT-CORE: [HW RST] WMT_CTRL_POWER_OFF ok (%d)", iRet);
    }
#endif
    gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT] = DRV_STS_POWER_OFF;

    /*-->PesetCombo chip*/
    iRet = wmt_core_ctrl(WMT_CTRL_HW_RST, &ctrlPa1, &ctrlPa2) ;
    if (iRet) {
        WMT_ERR_FUNC("WMT-CORE: -->[HW RST] fail iRet(%d)\n", iRet);
    } else {
        WMT_INFO_FUNC("WMT-CORE: -->[HW RST] ok\n");
    }

    //4  close stp
    ctrlPa1 = 0; ctrlPa2 = 0;
    iRet = wmt_core_ctrl(WMT_CTRL_STP_CLOSE, &ctrlPa1, &ctrlPa2);
    if (iRet) {
        WMT_ERR_FUNC("WMT-CORE: wmt close stp failed\n");
        return -1;
    }

    wmt_core_dump_func_state("AF HW RST");
    return iRet;

}

static INT32 opfunc_sw_rst(P_WMT_OP pWmtOp)
{
#if 0
    UINT32 iRet = -1;
    if (NULL != gMtkWmtCtx.p_ic_ops->swInit)
    {
        iRet = (*(gMtkWmtCtx.p_ic_ops->swInit))(&gMtkWmtCtx.wmtHifConf);
    }
    else
    {
        WMT_ERR_FUNC("WMT-CORE: error, gMtkWmtCtx.p_ic_ops->swInit(NULL)\n");
        return -1;
    }
    if (0 == iRet)
    {
        WMT_ERR_FUNC("WMT-CORE: WMT-->[FUNC_ON] succeed\n");
        gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT] = DRV_STS_FUNC_ON;
        return 0;
    }
    return -2;
#endif

    return wmt_core_stp_init();
}

static INT32 opfunc_stp_rst(P_WMT_OP pWmtOp)
{

    return 0;
}

static INT32 opfunc_therm_ctrl(P_WMT_OP pWmtOp)
{

    INT32 iRet = -1;
    UINT32 u4Res;
    UINT32 evtLen;
    UINT8 evtBuf[16] = {0};

    WMT_THERM_CMD[4] = pWmtOp->au4OpData[0];/*CMD type, refer to ENUM_WMTTHERM_TYPE_T*/

    /* send command*/
    //iRet = (*kal_stp_tx)(WMT_THERM_CMD, osal_sizeof(WMT_THERM_CMD), &u4Res);
    iRet = wmt_core_tx((PUINT8)WMT_THERM_CMD, osal_sizeof(WMT_THERM_CMD), &u4Res, MTK_WCN_BOOL_FALSE);
    if (iRet || (u4Res != osal_sizeof(WMT_THERM_CMD))) {
        WMT_ERR_FUNC("WMT-CORE: THERM_CTRL_CMD iRet(%d) cmd len err(%d, %d) \n", iRet, u4Res, osal_sizeof(WMT_THERM_CMD));
        return iRet;
    }

    evtLen = 16;

    iRet = wmt_core_rx(evtBuf, evtLen, &u4Res);
    if (iRet || ((u4Res != osal_sizeof(WMT_THERM_CTRL_EVT)) && (u4Res != osal_sizeof(WMT_THERM_READ_EVT) ))) {
        WMT_ERR_FUNC("WMT-CORE: read THERM_CTRL_EVT/THERM_READ_EVENT fail(%d) len(%d, %d)\n", iRet, u4Res, evtLen);
        mtk_wcn_stp_dbg_dump_package();
        return iRet;
    }
    if (u4Res == osal_sizeof(WMT_THERM_CTRL_EVT))
    {
        if (osal_memcmp(evtBuf, WMT_THERM_CTRL_EVT, osal_sizeof(WMT_THERM_CTRL_EVT)) != 0) {
            WMT_ERR_FUNC("WMT-CORE: compare WMT_THERM_CTRL_EVT error\n");
            WMT_ERR_FUNC("WMT-CORE: rx(%d):[%02X,%02X,%02X,%02X,%02X] exp(%d):[%02X,%02X,%02X,%02X,%02X]\n",
            u4Res, evtBuf[0], evtBuf[1], evtBuf[2], evtBuf[3],evtBuf[4],
            osal_sizeof(WMT_THERM_CTRL_EVT), WMT_THERM_CTRL_EVT[0], WMT_THERM_CTRL_EVT[1], WMT_THERM_CTRL_EVT[2], WMT_THERM_CTRL_EVT[3], WMT_THERM_CTRL_EVT[4]);
            pWmtOp->au4OpData[1] = MTK_WCN_BOOL_FALSE;/*will return to function driver*/
            mtk_wcn_stp_dbg_dump_package();
        } else {
            WMT_INFO_FUNC("Send WMT_THERM_CTRL_CMD command OK!\n");
            pWmtOp->au4OpData[1] = MTK_WCN_BOOL_TRUE;/*will return to function driver*/
        }
    }
    else
    {
    /*no need to judge the real thermal value*/
        if (osal_memcmp(evtBuf, WMT_THERM_READ_EVT, osal_sizeof(WMT_THERM_READ_EVT) - 1) != 0) {
            WMT_ERR_FUNC("WMT-CORE: compare WMT_THERM_READ_EVT error\n");
            WMT_ERR_FUNC("WMT-CORE: rx(%d):[%02X,%02X,%02X,%02X,%02X,%02X] exp(%d):[%02X,%02X,%02X,%02X]\n",
            u4Res, evtBuf[0], evtBuf[1], evtBuf[2], evtBuf[3],evtBuf[4],evtBuf[5],
            osal_sizeof(WMT_THERM_READ_EVT), WMT_THERM_READ_EVT[0], WMT_THERM_READ_EVT[1], WMT_THERM_READ_EVT[2], WMT_THERM_READ_EVT[3]);
            pWmtOp->au4OpData[1] = 0xFF;    /*will return to function driver*/
            mtk_wcn_stp_dbg_dump_package();
        } else {
            WMT_INFO_FUNC("Send WMT_THERM_READ_CMD command OK!\n");
            pWmtOp->au4OpData[1] = evtBuf[5];/*will return to function driver*/
        }
    }

    return iRet;

}

static INT32 opfunc_efuse_rw(P_WMT_OP pWmtOp)
{

    INT32 iRet = -1;
    UINT32 u4Res;
    UINT32 evtLen;
    UINT8 evtBuf[16] = {0};

    if (DRV_STS_FUNC_ON != gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT]) {
        WMT_ERR_FUNC("WMT-CORE: wmt_efuse_rw fail: chip is powered off\n");
        return -1;
    }

    WMT_EFUSE_CMD[4] = (pWmtOp->au4OpData[0]) ? 0x1 : 0x2; /* w:2, r:1 */
    osal_memcpy(&WMT_EFUSE_CMD[6], (PUINT8)&pWmtOp->au4OpData[1], 2); /* address */
    osal_memcpy(&WMT_EFUSE_CMD[8], (PUINT32)pWmtOp->au4OpData[2], 4); /* value */

    wmt_core_dump_data(&WMT_EFUSE_CMD[0], "efuse_cmd", osal_sizeof(WMT_EFUSE_CMD));

    /* send command*/
    //iRet = (*kal_stp_tx)(WMT_EFUSE_CMD, osal_sizeof(WMT_EFUSE_CMD), &u4Res);
    iRet = wmt_core_tx((PUINT8)WMT_EFUSE_CMD, osal_sizeof(WMT_EFUSE_CMD), &u4Res, MTK_WCN_BOOL_FALSE);
    if (iRet || (u4Res != osal_sizeof(WMT_EFUSE_CMD))) {
        WMT_ERR_FUNC("WMT-CORE: EFUSE_CMD iRet(%d) cmd len err(%d, %d) \n", iRet, u4Res, osal_sizeof(WMT_EFUSE_CMD));
        return iRet;
    }

    evtLen = (pWmtOp->au4OpData[0]) ? osal_sizeof(WMT_EFUSE_EVT) : osal_sizeof(WMT_EFUSE_EVT);

    iRet = wmt_core_rx(evtBuf, evtLen, &u4Res);
    if (iRet || (u4Res != evtLen)) {
        WMT_ERR_FUNC("WMT-CORE: read REG_EVB fail(%d) len(%d, %d)\n", iRet, u4Res, evtLen);
    }
    wmt_core_dump_data(&evtBuf[0], "efuse_evt", osal_sizeof(evtBuf));

    return iRet;

}

static INT32 opfunc_gpio_ctrl (P_WMT_OP pWmtOp)
{
    INT32 iRet = -1;
    WMT_IC_PIN_ID id;
    WMT_IC_PIN_STATE stat;
    UINT32 flag;

    if (DRV_STS_FUNC_ON != gMtkWmtCtx.eDrvStatus[WMTDRV_TYPE_WMT]) {
        WMT_ERR_FUNC("WMT-CORE: wmt_gpio_ctrl fail: chip is powered off\n");
        return -1;
    }

    if (!gMtkWmtCtx.p_ic_ops->ic_pin_ctrl) {
        WMT_ERR_FUNC("WMT-CORE: error, gMtkWmtCtx.p_ic_ops->ic_pin_ctrl(NULL)\n");
        return -1;
    }

    id = pWmtOp->au4OpData[0];
    stat = pWmtOp->au4OpData[1];
    flag = pWmtOp->au4OpData[2];

    WMT_INFO_FUNC("ic pin id:%d, stat:%d, flag:0x%x\n", id , stat, flag);

    iRet = (*(gMtkWmtCtx.p_ic_ops->ic_pin_ctrl))(id , stat, flag);

    return iRet;
}

//turn on/off sdio function
INT32 opfunc_sdio_ctrl (P_WMT_OP pWmtOp)
{
    UINT32 ctrlPa1 = 0;
    UINT32 ctrlPa2 = 0;
    UINT32 iRet = 0;
    ctrlPa1 = WMT_HIF_SDIO == gMtkWmtCtx.wmtHifConf.hifType ?     WMT_SDIO_SLOT_SDIO2 :     WMT_SDIO_SLOT_SDIO1;
    ctrlPa2 = pWmtOp->au4OpData[0]; /* turn off/on SDIO slot */
    iRet = wmt_core_ctrl(WMT_CTRL_SDIO_HW, &ctrlPa1, &ctrlPa2) ;
    if (iRet) {
        WMT_WARN_FUNC("SDIO hw ctrl fail ret(%d)\n", iRet);
        /* Anyway, continue turning STP SDIO to POWER OFF/ON state*/
        gMtkWmtCtx.eDrvStatus[ctrlPa1] = DRV_STS_POWER_OFF;
    }
    else {
        WMT_INFO_FUNC("SDIO hw ctrl succeed  \n");
        gMtkWmtCtx.eDrvStatus[ctrlPa1] = 0 ==ctrlPa2 ? DRV_STS_POWER_OFF : DRV_STS_POWER_ON;
    }

    return 0;

}


MTK_WCN_BOOL wmt_core_is_quick_ps_support (void)
{
    P_WMT_CTX pctx = &gMtkWmtCtx;
	if ((NULL != pctx->p_ic_ops) && (NULL != pctx->p_ic_ops->is_quick_sleep))
	{
	    return (*(pctx->p_ic_ops->is_quick_sleep))();
	}
	return MTK_WCN_BOOL_FALSE;
}

MTK_WCN_BOOL wmt_core_get_aee_dump_flag(void)
{
    MTK_WCN_BOOL bRet = MTK_WCN_BOOL_FALSE;
    P_WMT_CTX pctx = &gMtkWmtCtx;
	
	if ((NULL != pctx->p_ic_ops) && (NULL != pctx->p_ic_ops->is_aee_dump_support))
	{
	    bRet = (*(pctx->p_ic_ops->is_aee_dump_support))();
	}
	else
	{
	    bRet = MTK_WCN_BOOL_FALSE;
	}
	
	return bRet;
}


INT32 opfunc_pin_state (P_WMT_OP pWmtOp)
{
    
    UINT32 ctrlPa1 = 0;
    UINT32 ctrlPa2 = 0;
    UINT32 iRet = 0;
	iRet = wmt_core_ctrl(WMT_CTRL_HW_STATE_DUMP, &ctrlPa1, &ctrlPa2) ;
	return iRet;
}



