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
#define DFT_TAG         "[WMT-EXP]"


/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include "osal_typedef.h"

#include <wmt_exp.h>
#include <wmt_lib.h>
#include <hif_sdio.h>


/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/

/*******************************************************************************
*                             D A T A   T Y P E S
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
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/

static MTK_WCN_BOOL
mtk_wcn_wmt_func_ctrl (
    ENUM_WMTDRV_TYPE_T type,
    ENUM_WMT_OPID_T opId
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

static MTK_WCN_BOOL
mtk_wcn_wmt_func_ctrl (
    ENUM_WMTDRV_TYPE_T type,
    ENUM_WMT_OPID_T opId
    )
{
    P_OSAL_OP pOp;
    MTK_WCN_BOOL bRet;
    P_OSAL_SIGNAL pSignal;

    pOp = wmt_lib_get_free_op();
    if (!pOp) {
        WMT_WARN_FUNC("get_free_lxop fail\n");
        return MTK_WCN_BOOL_FALSE;
    }
    
    pSignal = &pOp->signal;

    pOp->op.opId = opId;
    pOp->op.au4OpData[0] = type;
    pSignal->timeoutValue= (WMT_OPID_FUNC_ON == pOp->op.opId) ? MAX_FUNC_ON_TIME : MAX_FUNC_OFF_TIME;

    WMT_INFO_FUNC("OPID(%d) type(%d) start\n",
            pOp->op.opId,
            pOp->op.au4OpData[0]);

    /*do not check return value, we will do this either way*/
    wmt_lib_host_awake_get();
    /*wake up chip first*/
    if (DISABLE_PSM_MONITOR()) {
        WMT_ERR_FUNC("wake up failed\n");
        wmt_lib_put_op_to_free_queue(pOp);
        return MTK_WCN_BOOL_FALSE;
    }
    
    bRet = wmt_lib_put_act_op(pOp);
    ENABLE_PSM_MONITOR();
    wmt_lib_host_awake_put();
    
    if (MTK_WCN_BOOL_FALSE == bRet) {
        WMT_WARN_FUNC("OPID(%d) type(%d) fail\n",
            pOp->op.opId,
            pOp->op.au4OpData[0]);
    }
    else {
        WMT_INFO_FUNC("OPID(%d) type(%d) ok\n",
            pOp->op.opId,
            pOp->op.au4OpData[0]);
    }
    return bRet;
}

MTK_WCN_BOOL
mtk_wcn_wmt_func_off (
    ENUM_WMTDRV_TYPE_T type
    )
{
    MTK_WCN_BOOL ret;        

    if(type == WMTDRV_TYPE_BT)    
    {        
        osal_printtimeofday("############ BT OFF ====>");    
    }    

    ret = mtk_wcn_wmt_func_ctrl(type, WMT_OPID_FUNC_OFF);    

    if(type == WMTDRV_TYPE_BT)    
    {        
        osal_printtimeofday("############ BT OFF <====");    
    }

    return ret;
}

MTK_WCN_BOOL
mtk_wcn_wmt_func_on (
    ENUM_WMTDRV_TYPE_T type
    )
{
    MTK_WCN_BOOL ret;    

    if(type == WMTDRV_TYPE_BT)    
    {        
        osal_printtimeofday("############ BT ON ====>");    
    }
    
    ret = mtk_wcn_wmt_func_ctrl(type, WMT_OPID_FUNC_ON);        

    if(type == WMTDRV_TYPE_BT)    
    {        
        osal_printtimeofday(" ############BT ON <====");    
    }
    
    return ret;
}

/*
return value:
enable/disable thermal sensor function: true(1)/false(0)
read thermal sensor function:thermal value

*/
INT8
mtk_wcn_wmt_therm_ctrl (
    ENUM_WMTTHERM_TYPE_T eType
    )
{
    P_OSAL_OP pOp;
    P_WMT_OP pOpData;
    MTK_WCN_BOOL bRet;
    P_OSAL_SIGNAL pSignal;

    /*parameter validation check*/
    if( WMTTHERM_MAX < eType || WMTTHERM_ENABLE > eType){
        WMT_ERR_FUNC("invalid thermal control command (%d)\n", eType);
        return MTK_WCN_BOOL_FALSE;
    }

    /*check if chip support thermal control function or not*/
    bRet = wmt_lib_is_therm_ctrl_support();
    if (MTK_WCN_BOOL_FALSE == bRet) {
        WMT_ERR_FUNC("thermal ctrl function not supported\n");
        return MTK_WCN_BOOL_FALSE;
    }

    pOp = wmt_lib_get_free_op();
    if (!pOp) {
        WMT_WARN_FUNC("get_free_lxop fail \n");
        return MTK_WCN_BOOL_FALSE;
    }

    pSignal = &pOp->signal;
    pOpData = &pOp->op;
    pOpData->opId = WMT_OPID_THERM_CTRL;
    /*parameter fill*/
    pOpData->au4OpData[0] = eType;
    pSignal->timeoutValue = MAX_EACH_WMT_CMD;

    WMT_INFO_FUNC("OPID(%d) type(%d) start\n",
            pOp->op.opId,
            pOp->op.au4OpData[0]);

    if (DISABLE_PSM_MONITOR()) {
        WMT_ERR_FUNC("wake up failed\n");
        wmt_lib_put_op_to_free_queue(pOp);
        return -1;
    }
    
    bRet = wmt_lib_put_act_op(pOp);
    ENABLE_PSM_MONITOR();

    if (MTK_WCN_BOOL_FALSE == bRet) {
        WMT_WARN_FUNC("OPID(%d) type(%d) fail\n\n",
            pOpData->opId,
            pOpData->au4OpData[0]);
        /*0xFF means read error occurs*/
        pOpData->au4OpData[1] = (eType == WMTTHERM_READ) ? 0xFF : MTK_WCN_BOOL_FALSE;/*will return to function driver*/
    }
    else {
        WMT_INFO_FUNC("OPID(%d) type(%d) return(%d) ok\n\n",
            pOpData->opId,
            pOpData->au4OpData[0],
            pOpData->au4OpData[1]);
    }
    /*return value will be put to lxop->op.au4OpData[1]*/
    WMT_DBG_FUNC("therm ctrl type(%d), iRet(0x%08x) \n", eType, pOpData->au4OpData[1]);
    return (INT8)pOpData->au4OpData[1];
}

ENUM_WMTHWVER_TYPE_T
mtk_wcn_wmt_hwver_get (VOID)
{
    // TODO: [ChangeFeature][GeorgeKuo] Reconsider usage of this type
    // TODO: how do we extend for new chip and newer revision?
    // TODO: This way is hard to extend
    return wmt_lib_get_icinfo(WMTCHIN_MAPPINGHWVER);
}

MTK_WCN_BOOL
mtk_wcn_wmt_dsns_ctrl (
    ENUM_WMTDSNS_TYPE_T eType
    )
{
    P_OSAL_OP pOp;
    P_WMT_OP pOpData;
    MTK_WCN_BOOL bRet;
    P_OSAL_SIGNAL pSignal;

    if (WMTDSNS_MAX <= eType) {
        WMT_ERR_FUNC("invalid desense control command (%d)\n", eType);
        return MTK_WCN_BOOL_FALSE;
    }

    /*check if chip support thermal control function or not*/
    bRet = wmt_lib_is_dsns_ctrl_support();
    if (MTK_WCN_BOOL_FALSE == bRet) {
        WMT_ERR_FUNC("thermal ctrl function not supported\n");
        return MTK_WCN_BOOL_FALSE;
    }

    pOp = wmt_lib_get_free_op();
    if (!pOp) {
        WMT_WARN_FUNC("get_free_lxop fail \n");
        return MTK_WCN_BOOL_FALSE;
    }

    pSignal = &pOp->signal;
    pOpData = &pOp->op;
    pOpData->opId = WMT_OPID_DSNS;
    pSignal->timeoutValue = MAX_EACH_WMT_CMD;
    /*parameter fill*/
    if ((WMTDSNS_FM_DISABLE <= eType) && (WMTDSNS_FM_GPS_ENABLE >= eType)) {
        pOpData->au4OpData[0] = WMTDRV_TYPE_FM;
        pOpData->au4OpData[1] = eType;
    }

    WMT_INFO_FUNC("OPID(%d) type(%d) start\n",
            pOp->op.opId,
            pOp->op.au4OpData[0]);

    if (DISABLE_PSM_MONITOR()) {
        WMT_ERR_FUNC("wake up failed\n");
        wmt_lib_put_op_to_free_queue(pOp);
        return MTK_WCN_BOOL_FALSE;
    }
    
    bRet = wmt_lib_put_act_op(pOp);
    ENABLE_PSM_MONITOR();

    if (MTK_WCN_BOOL_FALSE == bRet) {
        WMT_WARN_FUNC("OPID(%d) type(%d) fail\n\n",
            pOpData->opId,
            pOpData->au4OpData[0]);
    }
    else {
        WMT_INFO_FUNC("OPID(%d) type(%d) ok\n\n",
            pOpData->opId,
            pOpData->au4OpData[0]);
    }

    return bRet;
}

INT32
mtk_wcn_wmt_msgcb_reg (
    ENUM_WMTDRV_TYPE_T eType,
    PF_WMT_CB pCb
    )
{
    return (INT32)wmt_lib_msgcb_reg(eType, pCb);
}

INT32
mtk_wcn_wmt_msgcb_unreg (
    ENUM_WMTDRV_TYPE_T eType
    )
{
    return (INT32)wmt_lib_msgcb_unreg(eType);
}

INT32
mtk_wcn_stp_wmt_sdio_op_reg (
    PF_WMT_SDIO_PSOP own_cb
    )
{
    wmt_lib_ps_set_sdio_psop(own_cb);
    return 0;
}


INT32 
mtk_wcn_stp_wmt_sdio_host_awake(
    VOID
    )
{    
    wmt_lib_ps_irq_cb();
    return 0;
}

MTK_WCN_BOOL mtk_wcn_wmt_assert (
    void
    )
{
    P_OSAL_OP pOp = NULL;
    MTK_WCN_BOOL bRet = MTK_WCN_BOOL_FALSE;
    P_OSAL_SIGNAL pSignal;
    
    pOp  = wmt_lib_get_free_op();
    if (!pOp ) {
        WMT_WARN_FUNC("get_free_lxop fail\n");
        return MTK_WCN_BOOL_FALSE;
    }

    pSignal = &pOp ->signal;

    pOp ->op.opId = WMT_OPID_CMD_TEST;
    
    pSignal->timeoutValue= MAX_EACH_WMT_CMD;
    /*this test command should be run with usb cable connected, so no host awake is needed*/
    //wmt_lib_host_awake_get();
    pOp->op.au4OpData[0] = 0;
    
    /*wake up chip first*/
    if (DISABLE_PSM_MONITOR()) {
        WMT_ERR_FUNC("wake up failed\n");
        wmt_lib_put_op_to_free_queue(pOp);
        return MTK_WCN_BOOL_FALSE;
    }
    
    bRet = wmt_lib_put_act_op(pOp);
    ENABLE_PSM_MONITOR();
    
    //wmt_lib_host_awake_put();
    WMT_INFO_FUNC("CMD_TEST, opid (%d), par(%d, %d), ret(%d), result(%s)\n", \
    pOp->op.opId, \
    pOp->op.au4OpData[0], \
    pOp->op.au4OpData[1], \
    bRet, \
    MTK_WCN_BOOL_FALSE == bRet ? "failed" : "succeed"\
    );
    
    return bRet;
}

extern INT32 mtk_wcn_wmt_chipid_query (VOID)
{
    return mtk_wcn_hif_sdio_query_chipid(0);
}



EXPORT_SYMBOL(mtk_wcn_wmt_assert);
EXPORT_SYMBOL(mtk_wcn_stp_wmt_sdio_host_awake);
EXPORT_SYMBOL(mtk_wcn_stp_wmt_sdio_op_reg);
EXPORT_SYMBOL(mtk_wcn_wmt_msgcb_unreg);
EXPORT_SYMBOL(mtk_wcn_wmt_msgcb_reg);
EXPORT_SYMBOL(mtk_wcn_wmt_dsns_ctrl);
EXPORT_SYMBOL(mtk_wcn_wmt_hwver_get);
EXPORT_SYMBOL(mtk_wcn_wmt_therm_ctrl);
EXPORT_SYMBOL(mtk_wcn_wmt_func_on);
EXPORT_SYMBOL(mtk_wcn_wmt_func_off);
EXPORT_SYMBOL(mtk_wcn_wmt_chipid_query);


