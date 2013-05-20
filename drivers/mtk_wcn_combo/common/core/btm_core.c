

#include "osal_typedef.h"
#include "osal.h"
#include "stp_dbg.h"
#include "stp_core.h"
#include "btm_core.h"

#define PFX_BTM                         "[STP-BTM] "
#define STP_BTM_LOG_LOUD                 4
#define STP_BTM_LOG_DBG                  3
#define STP_BTM_LOG_INFO                 2
#define STP_BTM_LOG_WARN                 1
#define STP_BTM_LOG_ERR                  0

INT32 gBtmDbgLevel = STP_BTM_LOG_INFO;

#define STP_BTM_LOUD_FUNC(fmt, arg...)   if(gBtmDbgLevel >= STP_BTM_LOG_LOUD){ osal_dbg_print(PFX_BTM "%s: "  fmt, __FUNCTION__ ,##arg);}
#define STP_BTM_DBG_FUNC(fmt, arg...)    if(gBtmDbgLevel >= STP_BTM_LOG_DBG){ osal_dbg_print(PFX_BTM "%s: "  fmt, __FUNCTION__ ,##arg);}
#define STP_BTM_INFO_FUNC(fmt, arg...)   if(gBtmDbgLevel >= STP_BTM_LOG_INFO){ osal_dbg_print(PFX_BTM "[I]%s: "  fmt, __FUNCTION__ ,##arg);}
#define STP_BTM_WARN_FUNC(fmt, arg...)   if(gBtmDbgLevel >= STP_BTM_LOG_WARN){ osal_dbg_print(PFX_BTM "[W]%s: "  fmt, __FUNCTION__ ,##arg);}
#define STP_BTM_ERR_FUNC(fmt, arg...)    if(gBtmDbgLevel >= STP_BTM_LOG_ERR){  osal_dbg_print(PFX_BTM "[E]%s(%d):ERROR! "   fmt, __FUNCTION__ , __LINE__, ##arg);}
#define STP_BTM_TRC_FUNC(f)              if(gBtmDbgLevel >= STP_BTM_LOG_DBG){ osal_dbg_print(PFX_BTM "<%s> <%d>\n", __FUNCTION__, __LINE__);}

INT32 gDumplogflag = 0;
#if WMT_PLAT_ALPS
extern void dump_uart_history(void);
#endif


#define ASSERT(expr)

MTKSTP_BTM_T stp_btm_i;
MTKSTP_BTM_T *stp_btm = &stp_btm_i;

const PCHAR g_btm_op_name[]={
        "STP_OPID_BTM_RETRY",
        "STP_OPID_BTM_RST",
        "STP_OPID_BTM_DBG_DUMP",
        "STP_OPID_BTM_EXIT"
    };

#if 0
static PCHAR _stp_pkt_type(INT32 type){

    static CHAR s[10]; 

    switch(type){
        case WMT_TASK_INDX:
            osal_memcpy(s, "WMT", strlen("WMT")+1);
            break;
        case BT_TASK_INDX:
            osal_memcpy(s, "BT", strlen("BT")+1);
            break;
        case GPS_TASK_INDX:
            osal_memcpy(s, "GPS", strlen("GPS")+1);
            break;
        case FM_TASK_INDX:
            osal_memcpy(s, "FM", strlen("FM")+1);
            break;
        default:
            osal_memcpy(s, "UNKOWN", strlen("UNKOWN")+1);
            break;
    }

    return s;
}
#endif 

static INT32 _stp_btm_put_dump_to_nl(void)
{
    #define NUM_FETCH_ENTRY 8   

    static UINT8  buf[2048];
    static UINT8  tmp[2048];

    UINT32  buf_len;       
    STP_PACKET_T  *pkt;
    STP_DBG_HDR_T *hdr;
    INT32  remain=0, index =0;
    INT32 retry = 0, rc = 0, nl_retry = 0;
    STP_BTM_INFO_FUNC("Enter..\n");
  
    index = 0;
    tmp[index++]='[';
    tmp[index++]='M';
    tmp[index++]=']'; 

    do
    {
        index = 3;
        remain = stp_dbg_dmp_out_ex(&buf[0], &buf_len);
        if (buf_len > 0)
        {
            pkt = (STP_PACKET_T  *)buf;
            hdr = &pkt->hdr;
            if (hdr->dbg_type == STP_DBG_FW_DMP){
                osal_memcpy(&tmp[index], pkt->raw, pkt->hdr.len);

                if(pkt->hdr.len <= 1500)
                {
                    tmp[index + pkt->hdr.len] = '\n';
                    tmp[index + pkt->hdr.len + 1] = '\0';

                    //printk("\n%s\n+++\n", tmp);
                    rc = stp_dbg_nl_send((PCHAR)&tmp, 2);

                    while(rc){
                       nl_retry++;                       
                       if(nl_retry > 1000){
                            break;
                       }                       
                       STP_BTM_WARN_FUNC("**dump send fails, and retry again.**\n");
                       osal_msleep(3);
                       rc = stp_dbg_nl_send((PCHAR)&tmp, 2);
                       if(!rc){
                          STP_BTM_WARN_FUNC("****retry again ok!**\n");
                       }
                    }                    
                    //schedule();
                } else {
                    STP_BTM_INFO_FUNC("dump entry length is over long\n");
                    osal_bug_on(0);
                }
                retry = 0;
            }
        }else
        {
            retry ++;
            osal_msleep(100);
        }
    }while((remain > 0) || (retry < 2));

    STP_BTM_INFO_FUNC("Exit..\n");
    return 0;
}


static INT32 _stp_btm_put_dump_to_aee(void)
{
    static UINT8  buf[2048];
    static UINT8  tmp[2048];

    UINT32  buf_len;       
    STP_PACKET_T  *pkt;
    STP_DBG_HDR_T *hdr;
    INT32 remain = 0;
    INT32 retry = 0;
    INT32 ret = 0;
    STP_BTM_INFO_FUNC("Enter..\n");

    do {
        remain = stp_dbg_dmp_out_ex(&buf[0], &buf_len);
        if (buf_len > 0) {
            pkt = (STP_PACKET_T*)buf;
            hdr = &pkt->hdr;
            if (hdr->dbg_type == STP_DBG_FW_DMP) {
                osal_memcpy(&tmp[0], pkt->raw, pkt->hdr.len);

                if (pkt->hdr.len <= 1500) {
                    tmp[pkt->hdr.len] = '\n';
                    tmp[pkt->hdr.len + 1] = '\0';

                    ret = stp_dbg_aee_send(tmp, pkt->hdr.len, 0);                 
                } else {
                    STP_BTM_INFO_FUNC("dump entry length is over long\n");
                    osal_bug_on(0);
                }
                retry = 0;
            }
        } else {  
            retry ++;
            osal_msleep(100);
        }
    }while ((remain > 0) || (retry < 2));

    STP_BTM_INFO_FUNC("Exit..\n");
    return ret;
}

#define COMBO_DUMP2AEE

static INT32 _stp_btm_handler(MTKSTP_BTM_T *stp_btm, P_STP_BTM_OP pStpOp)
{
    INT32 ret = -1;
    INT32 dump_sink = 1; //core dump target, 0: aee; 1: netlink

    if (NULL == pStpOp) 
    {
        return -1;
    }

    switch(pStpOp->opId) 
    {
        case STP_OPID_BTM_EXIT:
            // TODO: clean all up?
            ret = 0;
        break;

        /*tx timeout retry*/
        case STP_OPID_BTM_RETRY:
            stp_do_tx_timeout();
            ret = 0;
            
        break;

        /*whole chip reset*/
        case STP_OPID_BTM_RST:
            STP_BTM_INFO_FUNC("whole chip reset start!\n");
            STP_BTM_INFO_FUNC("....+\n");
            if(stp_btm->wmt_notify)
            {
                stp_btm->wmt_notify(BTM_RST_OP);
                ret = 0;
            }
            else
            {
                STP_BTM_ERR_FUNC("stp_btm->wmt_notify is NULL.");
                ret = -1;
            }

            STP_BTM_INFO_FUNC("whole chip reset end!\n");
            
        break;

        case STP_OPID_BTM_DBG_DUMP:
            /*Notify the wmt to get dump data*/
            STP_BTM_DBG_FUNC("wmt dmp notification\n");
            dump_sink = ((stp_btm->wmt_notify(BTM_GET_AEE_SUPPORT_FLAG) == MTK_WCN_BOOL_TRUE) ? 0 : 1);
            
            if (dump_sink == 0) {
                _stp_btm_put_dump_to_aee();
            } else if (dump_sink == 1) {
                _stp_btm_put_dump_to_nl();
            } else {
                STP_BTM_ERR_FUNC("unknown sink %d\n", dump_sink);
            }            
                           
        break;


        case STP_OPID_BTM_DUMP_TIMEOUT:
            // Flush dump data, and reset compressor
            STP_BTM_INFO_FUNC("Flush dump data\n");
            wcn_core_dump_flush(0);
        break;
        
        default:
            ret = -1;
        break;
    }
    
    return ret;
}

static P_OSAL_OP _stp_btm_get_op (
    MTKSTP_BTM_T *stp_btm,
    P_OSAL_OP_Q pOpQ
    )
{
    P_OSAL_OP pOp;
    //INT32 ret = 0;

    if (!pOpQ) 
    {
        STP_BTM_WARN_FUNC("!pOpQ \n");
        return NULL;
    }
    
    osal_lock_unsleepable_lock(&(stp_btm->wq_spinlock));
    /* acquire lock success */
    RB_GET(pOpQ, pOp);
    osal_unlock_unsleepable_lock(&(stp_btm->wq_spinlock));

    if (!pOp) 
    {
        //STP_BTM_WARN_FUNC("RB_GET fail\n");
    }

    return pOp;
}

static INT32 _stp_btm_put_op (
    MTKSTP_BTM_T *stp_btm,
    P_OSAL_OP_Q pOpQ,
    P_OSAL_OP pOp
    )
{
    INT32 ret;

    if (!pOpQ || !pOp) 
    {
        STP_BTM_WARN_FUNC("invalid input param: 0x%p, 0x%p \n", pOpQ, pOp);
        return 0;//;MTK_WCN_BOOL_FALSE;
    }

    ret = 0;

    osal_lock_unsleepable_lock(&(stp_btm->wq_spinlock));
    /* acquire lock success */
    if (!RB_FULL(pOpQ)) 
    {
        RB_PUT(pOpQ, pOp);
    }
    else 
    {
        ret = -1;
    }
    osal_unlock_unsleepable_lock(&(stp_btm->wq_spinlock));
    
    if (ret) 
    {
        STP_BTM_WARN_FUNC("RB_FULL(0x%p) %d ,rFreeOpQ = %p, rActiveOpQ = %p\n", pOpQ, RB_COUNT(pOpQ),
            &stp_btm->rFreeOpQ,  &stp_btm->rActiveOpQ);
        return 0; 
    }
    else 
    {
        //STP_BTM_WARN_FUNC("RB_COUNT = %d\n",RB_COUNT(pOpQ));
        return 1; 
    }
}

P_OSAL_OP _stp_btm_get_free_op (
    MTKSTP_BTM_T *stp_btm
    )
{
    P_OSAL_OP pOp;

    if (stp_btm) 
    {
        pOp = _stp_btm_get_op(stp_btm, &stp_btm->rFreeOpQ);
        if (pOp) 
        {
            osal_memset(&pOp->op, 0, sizeof(pOp->op));
        }
        return pOp;
    }
    else {
        return NULL;
    }
}

INT32 _stp_btm_put_act_op (
    MTKSTP_BTM_T *stp_btm,
    P_OSAL_OP pOp
    )
{
    INT32 bRet = 0;
    INT32 bCleanup = 0;
    LONG wait_ret = -1;

    P_OSAL_SIGNAL pSignal = NULL;

    do {
        if (!stp_btm || !pOp) 
        {
            break;
        }

        pSignal = &pOp->signal;
        
        if (pSignal->timeoutValue) 
        {
            pOp->result = -9;
            osal_signal_init(&pOp->signal);
        }

        /* put to active Q */
        bRet = _stp_btm_put_op(stp_btm, &stp_btm->rActiveOpQ, pOp);
        if(0 == bRet)
        {
            STP_BTM_WARN_FUNC("put active queue fail\n");
            bCleanup = 1;//MTK_WCN_BOOL_TRUE;
            break;
        }
        
        /* wake up wmtd */
        osal_trigger_event(&stp_btm->STPd_event);

        if (pSignal->timeoutValue == 0) 
        {
            bRet = 1;//MTK_WCN_BOOL_TRUE;
            /* clean it in wmtd */
            break;
        }
        
        /* wait result, clean it here */
        bCleanup = 1;//MTK_WCN_BOOL_TRUE;

        /* check result */
        wait_ret = osal_wait_for_signal_timeout(&pOp->signal);

        STP_BTM_DBG_FUNC("wait completion:%ld\n", wait_ret);
        if (!wait_ret) 
        {
            STP_BTM_ERR_FUNC("wait completion timeout \n");
            // TODO: how to handle it? retry?
        }
        else 
        {
            if (pOp->result) 
            {
                STP_BTM_WARN_FUNC("op(%d) result:%d\n", pOp->op.opId, pOp->result);
            }
            
            bRet = (pOp->result) ? 0 : 1;
        }
    } while(0);

    if (bCleanup) {
        /* put Op back to freeQ */
        _stp_btm_put_op(stp_btm, &stp_btm->rFreeOpQ, pOp);
    }

    return bRet;
}

static INT32 _stp_btm_wait_for_msg(void *pvData)
{
    MTKSTP_BTM_T *stp_btm = (MTKSTP_BTM_T *)pvData;
    return ((!RB_EMPTY(&stp_btm->rActiveOpQ)) || osal_thread_should_stop(&stp_btm->BTMd));
}

static INT32 _stp_btm_proc (void *pvData)
{
    MTKSTP_BTM_T *stp_btm = (MTKSTP_BTM_T *)pvData;
    P_OSAL_OP pOp;
    INT32 id;
    INT32 result;
    
    if (!stp_btm) 
    {
        STP_BTM_WARN_FUNC("!stp_btm \n");
        return -1;
    }

    for (;;) 
    {
        pOp = NULL;
        
        osal_wait_for_event(&stp_btm->STPd_event, 
            _stp_btm_wait_for_msg,
            (void *)stp_btm
            );

        if (osal_thread_should_stop(&stp_btm->BTMd)) 
        {
            STP_BTM_INFO_FUNC("should stop now... \n");
            // TODO: clean up active opQ
            break;
        }

#if 1
        if(gDumplogflag)
        {
            //printk("enter place1\n");    
            #if WMT_PLAT_ALPS
            dump_uart_history();
			#endif
            gDumplogflag = 0;
            continue;
        }
#endif

        /* get Op from activeQ */
        pOp = _stp_btm_get_op(stp_btm, &stp_btm->rActiveOpQ);

        if (!pOp) 
        {
            STP_BTM_WARN_FUNC("get_lxop activeQ fail\n");
            continue;
        }

        id = osal_op_get_id(pOp);

        STP_BTM_DBG_FUNC("======> lxop_get_opid = %d, %s, remaining count = *%d*\n",
            id, (id >= 4)?("???"):(g_btm_op_name[id]), RB_COUNT(&stp_btm->rActiveOpQ));

        if (id >= STP_OPID_BTM_NUM) 
        {
            STP_BTM_WARN_FUNC("abnormal opid id: 0x%x \n", id);
            result = -1;
            goto handler_done;
        }
        
        result = _stp_btm_handler(stp_btm, &pOp->op);

handler_done:

        if (result) 
        {
            STP_BTM_WARN_FUNC("opid id(0x%x)(%s) error(%d)\n", id, (id >= 4)?("???"):(g_btm_op_name[id]), result);
        }

        if (osal_op_is_wait_for_signal(pOp)) 
        {
            osal_op_raise_signal(pOp, result);
        }
        else 
        {
            /* put Op back to freeQ */
            _stp_btm_put_op(stp_btm, &stp_btm->rFreeOpQ, pOp);
        }
        
        if (STP_OPID_BTM_EXIT == id) 
        {
            break;
        } else if (STP_OPID_BTM_RST == id) {
            /* prevent multi reset case */
            stp_btm_reset_btm_wq(stp_btm); 
        }
    }
    
    STP_BTM_INFO_FUNC("exits \n");

    return 0;
};

static inline INT32 _stp_btm_notify_wmt_rst_wq(MTKSTP_BTM_T *stp_btm)
{

    P_OSAL_OP       pOp;
    INT32           bRet;
    INT32 retval;

    if(stp_btm == NULL)
    {
        return STP_BTM_OPERATION_FAIL;
    }
    else 
    {
        pOp = _stp_btm_get_free_op(stp_btm);
        if (!pOp) 
        {
            STP_BTM_WARN_FUNC("get_free_lxop fail \n");
            return -1;//break;
        }
        pOp->op.opId = STP_OPID_BTM_RST;
        pOp->signal.timeoutValue= 0;
        bRet = _stp_btm_put_act_op(stp_btm, pOp);
        STP_BTM_DBG_FUNC("OPID(%d) type(%d) bRet(%d) \n\n",
            pOp->op.opId,
            pOp->op.au4OpData[0],
            bRet);
        retval = (0 == bRet) ? STP_BTM_OPERATION_FAIL : STP_BTM_OPERATION_SUCCESS;
    }
    return retval;
}

static inline INT32 _stp_btm_notify_stp_retry_wq(MTKSTP_BTM_T *stp_btm){

    P_OSAL_OP       pOp;
    INT32           bRet;
    INT32 retval;

    if(stp_btm == NULL)
    {
        return STP_BTM_OPERATION_FAIL;
    }
    else 
    {
        pOp = _stp_btm_get_free_op(stp_btm);
        if (!pOp) 
        {
            STP_BTM_WARN_FUNC("get_free_lxop fail \n");
            return -1;//break;
        }
        pOp->op.opId = STP_OPID_BTM_RETRY;
        pOp->signal.timeoutValue= 0;
        bRet = _stp_btm_put_act_op(stp_btm, pOp);
        STP_BTM_DBG_FUNC("OPID(%d) type(%d) bRet(%d) \n\n",
            pOp->op.opId,
            pOp->op.au4OpData[0],
            bRet);
        retval = (0 == bRet) ? STP_BTM_OPERATION_FAIL : STP_BTM_OPERATION_SUCCESS;
    }
    return retval;
}


static inline INT32 _stp_btm_notify_coredump_timeout_wq(MTKSTP_BTM_T *stp_btm){

    P_OSAL_OP       pOp;
    INT32           bRet;
    INT32 retval;

    if (!stp_btm) {
        return STP_BTM_OPERATION_FAIL;
    } else {
        pOp = _stp_btm_get_free_op(stp_btm);
        if (!pOp) {
            STP_BTM_WARN_FUNC("get_free_lxop fail \n");
            return -1;//break;
        }
        pOp->op.opId = STP_OPID_BTM_DUMP_TIMEOUT;
        pOp->signal.timeoutValue= 0;
        bRet = _stp_btm_put_act_op(stp_btm, pOp);
        STP_BTM_DBG_FUNC("OPID(%d) type(%d) bRet(%d) \n\n",
            pOp->op.opId,
            pOp->op.au4OpData[0],
            bRet);
        retval = (0 == bRet) ? STP_BTM_OPERATION_FAIL : STP_BTM_OPERATION_SUCCESS;
    }
    return retval;
}


static inline INT32 _stp_btm_notify_wmt_dmp_wq(MTKSTP_BTM_T *stp_btm){

    P_OSAL_OP     pOp;
    INT32         bRet;
    INT32 retval;

    if(stp_btm == NULL)
    {
        return STP_BTM_OPERATION_FAIL;
    }
    else 
    {
        pOp = _stp_btm_get_free_op(stp_btm);
        if (!pOp) 
        {
            //STP_BTM_WARN_FUNC("get_free_lxop fail \n");
            return -1;//break;
        }
        pOp->op.opId = STP_OPID_BTM_DBG_DUMP;
        pOp->signal.timeoutValue= 0;
        bRet = _stp_btm_put_act_op(stp_btm, pOp);
        STP_BTM_DBG_FUNC("OPID(%d) type(%d) bRet(%d) \n\n",
            pOp->op.opId,
            pOp->op.au4OpData[0],
            bRet);
        retval = (0 == bRet) ? STP_BTM_OPERATION_FAIL : STP_BTM_OPERATION_SUCCESS;
    }
    return retval;
}

INT32 stp_btm_notify_wmt_rst_wq(MTKSTP_BTM_T *stp_btm)
{
    return _stp_btm_notify_wmt_rst_wq(stp_btm);
}

INT32 stp_btm_notify_stp_retry_wq(MTKSTP_BTM_T *stp_btm)
{
    return _stp_btm_notify_stp_retry_wq(stp_btm);
}

INT32 stp_btm_notify_coredump_timeout_wq(MTKSTP_BTM_T *stp_btm)
{
    return _stp_btm_notify_coredump_timeout_wq(stp_btm);
}

INT32 stp_btm_notify_wmt_dmp_wq(MTKSTP_BTM_T *stp_btm)
{
    return _stp_btm_notify_wmt_dmp_wq(stp_btm);
}

MTKSTP_BTM_T *stp_btm_init(void)
{
    INT32 i = 0x0;
    INT32 ret =-1;
    
    osal_unsleepable_lock_init(&stp_btm->wq_spinlock);
    osal_event_init(&stp_btm->STPd_event);
    stp_btm->wmt_notify = wmt_lib_btm_cb;
    
    RB_INIT(&stp_btm->rFreeOpQ, STP_BTM_OP_BUF_SIZE);
    RB_INIT(&stp_btm->rActiveOpQ, STP_BTM_OP_BUF_SIZE);

    /* Put all to free Q */
    for (i = 0; i < STP_BTM_OP_BUF_SIZE; i++) 
    {
         osal_signal_init(&(stp_btm->arQue[i].signal));
         _stp_btm_put_op(stp_btm, &stp_btm->rFreeOpQ, &(stp_btm->arQue[i]));
    }

    /*Generate PSM thread, to servie STP-CORE for packet retrying and core dump receiving*/
    stp_btm->BTMd.pThreadData = (VOID *)stp_btm;
    stp_btm->BTMd.pThreadFunc = (VOID *)_stp_btm_proc;
    osal_memcpy(stp_btm->BTMd.threadName, BTM_THREAD_NAME , osal_strlen(BTM_THREAD_NAME));

    ret = osal_thread_create(&stp_btm->BTMd);
    if (ret < 0) 
    {
        STP_BTM_ERR_FUNC("osal_thread_create fail...\n");
        goto ERR_EXIT1;
    }
    
    /* Start STPd thread*/
    ret = osal_thread_run(&stp_btm->BTMd);
    if(ret < 0)
    {
        STP_BTM_ERR_FUNC("osal_thread_run FAILS\n");
        goto ERR_EXIT1;
    }

    return stp_btm;

ERR_EXIT1:

    return NULL;

}

INT32 stp_btm_deinit(MTKSTP_BTM_T *stp_btm){

    UINT32 ret = -1;

    STP_BTM_INFO_FUNC("btm deinit\n");
    
    if(!stp_btm)
    {
        return STP_BTM_OPERATION_FAIL;
    }

    ret = osal_thread_destroy(&stp_btm->BTMd);
    if(ret < 0)
    {
        STP_BTM_ERR_FUNC("osal_thread_destroy FAILS\n");
        return STP_BTM_OPERATION_FAIL;
    }

    return STP_BTM_OPERATION_SUCCESS;
}


INT32 stp_btm_reset_btm_wq(MTKSTP_BTM_T *stp_btm)
{
    UINT32 i = 0;
    osal_lock_unsleepable_lock(&(stp_btm->wq_spinlock));
    RB_INIT(&stp_btm->rFreeOpQ, STP_BTM_OP_BUF_SIZE);
    RB_INIT(&stp_btm->rActiveOpQ, STP_BTM_OP_BUF_SIZE);
    osal_unlock_unsleepable_lock(&(stp_btm->wq_spinlock));
    /* Put all to free Q */
    for (i = 0; i < STP_BTM_OP_BUF_SIZE; i++) 
    {
         osal_signal_init(&(stp_btm->arQue[i].signal));
         _stp_btm_put_op(stp_btm, &stp_btm->rFreeOpQ, &(stp_btm->arQue[i]));
    }
    
    return 0;
}


INT32 stp_notify_btm_dump(MTKSTP_BTM_T *stp_btm)
{
    //printk("%s:enter++\n",__func__);
	if(NULL == stp_btm)
	{
	    osal_dbg_print("%s: NULL POINTER\n",__func__);
	    return -1;
	}
	else
	{
	    gDumplogflag = 1;
	    osal_trigger_event(&stp_btm->STPd_event);
	    return 0;
  }
}

