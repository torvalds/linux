
#include "stp_core.h"
#include "psm_core.h"
#include "btm_core.h"
#include "stp_dbg.h"

#define PFX                         "[STP] "
#define STP_LOG_DBG                  4
#define STP_LOG_PKHEAD               3
#define STP_LOG_INFO                 2
#define STP_LOG_WARN                 1
#define STP_LOG_ERR                  0

#define STP_DEL_SIZE   2    /* STP delimiter length */

UINT32 gStpDbgLvl = STP_LOG_INFO;

#define REMOVE_USELESS_LOG 1


/* global variables */
static const UINT8       stp_delimiter[STP_DEL_SIZE] = {0x55, 0x55};
static INT32             fgEnableNak         = 0; // 0=enable NAK; 1=disable NAK
static INT32             fgEnableDelimiter   = 0; // 0=disable Delimiter; 1=enable Delimiter

/* common interface */
static IF_TX            sys_if_tx           = NULL;
/* event/signal */
static EVENT_SET        sys_event_set = NULL;
static EVENT_TX_RESUME  sys_event_tx_resume = NULL;
static FUNCTION_STATUS  sys_check_function_status = NULL;
/* kernel lib */
//INT32                g_block_tx = 0; 
static mtkstp_context_struct stp_core_ctx = {0};

#define STP_PSM_CORE(x)           ((x).psm)
#define STP_SET_PSM_CORE(x,v)     ((x).psm = (v))

#define STP_BTM_CORE(x)           ((x).btm)
#define STP_SET_BTM_CORE(x,v)     ((x).btm = (v))

#define STP_IS_ENABLE(x)          ((x).f_enable != 0)
#define STP_NOT_ENABLE(x)         ((x).f_enable == 0)
#define STP_SET_ENABLE(x,v)       ((x).f_enable = (v))

#define STP_IS_READY(x)           ((x).f_ready != 0)
#define STP_NOT_READY(x)          ((x).f_ready == 0)
#define STP_SET_READY(x,v)        ((x).f_ready = (v))

#define STP_PENDING_TYPE(x)       ((x).f_pending_type)
#define STP_SET_PENDING_TYPE(x,v) ((x).f_pending_type = (v))

#define STP_BLUE_ANGEL        (0)
#define STP_BLUE_Z            (1)
#define STP_BT_STK(x)         ((x).f_bluez)
#define STP_BT_STK_IS_BLUEZ(x) ((x).f_bluez == (STP_BLUE_Z))
#define STP_SET_BT_STK(x,v) ((x).f_bluez = (v))

#define STP_IS_ENABLE_DBG(x)         ((x).f_dbg_en != 0)
#define STP_NOT_ENABLE_DBG(x)        ((x).f_dbg_en == 0)
#define STP_SET_ENABLE_DBG(x,v)      ((x).f_dbg_en = (v))

#define STP_IS_ENABLE_RST(x)         ((x).f_autorst_en != 0)
#define STP_NOT_ENABLE_RST(x)        ((x).f_autorst_en == 0)
#define STP_SET_ENABLE_RST(x,v)        ((x).f_autorst_en = (v))

#define STP_SUPPORT_PROTOCOL(x)      ((x).f_mode)
#define STP_SET_SUPPORT_PROTOCOL(x,v)  ((x).f_mode = (v))

#define STP_FW_COREDUMP_FLAG(x)  ((x).f_coredump)
#define STP_SET_FW_COREDUMP_FLAG(x,v)  ((x).f_coredump = (v))
#define STP_ENABLE_FW_COREDUMP(x,v)  ((x).en_coredump = (v))
#define STP_ENABLE_FW_COREDUMP_FLAG(x)  ((x).en_coredump)

#define STP_WMT_LAST_CLOSE(x)       ((x).f_wmt_last_close) 
#define STP_SET_WMT_LAST_CLOSE(x,v) ((x).f_wmt_last_close = (v))


/*[PatchNeed]Need to calulate the timeout value*/
static UINT32 mtkstp_tx_timeout = MTKSTP_TX_TIMEOUT;
static mtkstp_parser_state prev_state = -1;


#define CONFIG_DEBUG_STP_TRAFFIC_SUPPORT
#ifdef CONFIG_DEBUG_STP_TRAFFIC_SUPPORT
static MTKSTP_DBG_T *g_mtkstp_dbg = NULL;
#endif
static VOID stp_dbg_pkt_log(INT32 type, INT32 txAck, INT32 seq, INT32 crc, INT32 dir, const UINT8 *pBuf, INT32 len);
static MTK_WCN_BOOL stp_check_crc(UINT8 *buffer, UINT32 length, UINT16 crc);
static VOID   stp_update_tx_queue(UINT32 txseq);
static VOID   stp_rest_ctx_state (VOID);
static VOID   stp_change_rx_state(mtkstp_parser_state next);
static void   stp_tx_timeout_handler(ULONG data);
static VOID   stp_dump_data(const UINT8 *buf, const UINT8 *title,  const UINT32 len);
static VOID   stp_dump_tx_queue(UINT32 txseq);
static INT32  stp_is_apply_powersaving(VOID);
static INT32  stp_is_privileges_cmd(const UINT8 *buffer, const UINT32 length, const UINT8 type);
static MTK_WCN_BOOL stp_is_tx_res_available(UINT32 length);
static VOID   stp_add_to_tx_queue(const UINT8 *buffer, UINT32 length);
static INT32  stp_add_to_rx_queue(UINT8 *buffer, UINT32 length, UINT8 type);
static VOID   stp_send_tx_queue(UINT32 txseq);
static VOID   stp_send_ack(UINT8  txAck, UINT8 nak);
INT32 stp_send_data_no_ps(UINT8 *buffer, UINT32 length, UINT8 type);
static INT32  stp_process_rxack(VOID);
static VOID   stp_process_packet(VOID);
VOID stp_do_tx_timeout(VOID);

/*Bad*/
extern INT32 mtk_wcn_sys_if_rx(UINT8 *data, INT32 size);
#if WMT_PLAT_ALPS
extern void stop_log(void);//uart export API
#endif

static INT32 stp_ctx_lock_init (mtkstp_context_struct *pctx)
{
#if CFG_STP_CORE_CTX_SPIN_LOCK
	#if defined(CONFIG_PROVE_LOCKING)
	    osal_unsleepable_lock_init(&((pctx)->stp_mutex));
		return 0;
	#else
		return osal_unsleepable_lock_init(&((pctx)->stp_mutex));
	#endif
#else
	#if defined(CONFIG_PROVE_LOCKING)
		osal_sleepable_lock_init(&((pctx)->stp_mutex));
		return 0;
	#else
	    return osal_sleepable_lock_init(&((pctx)->stp_mutex));
	#endif
#endif
}

static INT32 stp_ctx_lock_deinit (mtkstp_context_struct *pctx)
{
#if CFG_STP_CORE_CTX_SPIN_LOCK
    return osal_unsleepable_lock_deinit(&((pctx)->stp_mutex));
#else
    return osal_sleepable_lock_deinit(&((pctx)->stp_mutex));
#endif
}

static INT32 stp_ctx_lock (mtkstp_context_struct *pctx)
{
	//dump_stack();
	//printk("stp_lock\n\r");
#if CFG_STP_CORE_CTX_SPIN_LOCK
    return osal_lock_unsleepable_lock(&((pctx)->stp_mutex));
#else
    return osal_lock_sleepable_lock(&((pctx)->stp_mutex));
#endif
}

static INT32 stp_ctx_unlock (mtkstp_context_struct *pctx)
{
	//dump_stack();
	//printk("stp_unlock\n\r");

#if CFG_STP_CORE_CTX_SPIN_LOCK
    return osal_unlock_unsleepable_lock(&((pctx)->stp_mutex));
#else
    return osal_unlock_sleepable_lock(&((pctx)->stp_mutex));
#endif
}


MTK_WCN_BOOL mtk_wcn_stp_dbg_level(UINT32 dbglevel)
{
    if (0 <= dbglevel && dbglevel <= 4)
    {
     gStpDbgLvl = dbglevel;
     STP_INFO_FUNC("gStpDbgLvl = %d\n", gStpDbgLvl);
     return MTK_WCN_BOOL_TRUE;
    }
    else
    {
     STP_INFO_FUNC("invalid stp debug level. gStpDbgLvl = %d\n", gStpDbgLvl);
    }
    return MTK_WCN_BOOL_FALSE;
}
#if 0
/*****************************************************************************
* FUNCTION
*  crc16
* DESCRIPTION
*  Compute the CRC-16 for the data buffer
* PARAMETERS
*  crc         [IN]        previous CRC value
*  buffer      [IN]        data buffer
*  length      [IN]        data buffer length
* RETURNS
*  the updated CRC value
*****************************************************************************/
static UINT16 crc16(const UINT8 *buffer, const UINT32 length)
{
    UINT32 crc, i;

    //FIXME: Add STP checksum feature
    crc = 0;
    for (i = 0; i < length; i++, buffer++)
    {
        crc = (crc >> 8) ^ crc16_table[(crc ^ (*buffer)) & 0xff];
    }
    return crc;
}

#endif


VOID stp_dbg_pkt_log(INT32 type, INT32 txAck, INT32 seq, INT32 crc, INT32 dir, const UINT8 *pBuf, INT32 len){

#ifndef CONFIG_LOG_STP_INTERNAL
        return ;
#endif

        if(STP_IS_ENABLE_DBG(stp_core_ctx)){
            stp_dbg_log_pkt(g_mtkstp_dbg,
            STP_DBG_PKT,
            type, //type
            txAck, //ack
            seq, //seq
            crc, //crc
            dir, //dir
            len, //len
            pBuf); //body
         }
        else
        {
            STP_DBG_FUNC("stp_dbg not enabled");
        }
}

/*****************************************************************************
* FUNCTION
*  stp_check_crc
* DESCRIPTION
*  check the check sum of packet payload
* PARAMETERS
*  pdata       [IN]        the data want to check
*  length      [IN]        the length of pdata
*  crc         [IN]        the crc of pdata
* RETURNS
*  KAL_TRUE        crc is ok
*  KAL_FALSE       crc is wrong
*****************************************************************************/
static MTK_WCN_BOOL stp_check_crc(UINT8 *buffer, UINT32 length, UINT16 crc)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    UINT16 checksum;

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/

    // FIXME: Add STP feature: check or skip crc

    checksum = osal_crc16(buffer, length);
    if (checksum == crc)
    {
        return MTK_WCN_BOOL_TRUE;
    }
    else
    {
        STP_ERR_FUNC("CRC fail, length = %d, rx = %x, calc = %x \r\n", length, crc, checksum);
        return MTK_WCN_BOOL_FALSE;
    }
}

/*****************************************************************************
* FUNCTION
*  stp_update_tx_queue
* DESCRIPTION
*  update packet's ACK field
* PARAMETERS
*  txseq       [IN]        index of the tx packet which we want to update
* RETURNS
*  void
*****************************************************************************/
static void stp_update_tx_queue(UINT32 txseq)
{
    INT32 tx_read, i;
    UINT8 checksum = 0;

    tx_read = stp_core_ctx.tx_start_addr[txseq];
    stp_core_ctx.tx_buf[tx_read] &= 0xf8;
    stp_core_ctx.tx_buf[tx_read] |= stp_core_ctx.sequence.txack;

    for (i = 0; i < 3; i++)
    {
        checksum += stp_core_ctx.tx_buf[tx_read];
        tx_read++;
        if( tx_read >= MTKSTP_BUFFER_SIZE)
        {
            tx_read -= MTKSTP_BUFFER_SIZE;
        }
    }

    stp_core_ctx.tx_buf[tx_read] = checksum;

    return;
}

/*****************************************************************************
* FUNCTION
*  stp_rest_ctx_state
* DESCRIPTION
*  Reset stp context state variables only. Mutex and timer resources are not touched.
*
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
static VOID stp_rest_ctx_state (VOID)
{
    INT32 i;

    stp_ctx_lock(&stp_core_ctx);
    stp_core_ctx.rx_counter = 0;

    /*reset rx buffer pointer*/
    for (i = 0; i < MTKSTP_MAX_TASK_NUM; i++)
    {
        stp_core_ctx.ring[i].read_p = 0;
        stp_core_ctx.ring[i].write_p = 0;
    }

    /*reset tx buffer pointer*/
    stp_core_ctx.tx_write = 0;
    stp_core_ctx.tx_read = 0;

    /*reset STP protocol context*/
    stp_core_ctx.parser.state = MTKSTP_SYNC;
    stp_core_ctx.sequence.txseq = 0;
    stp_core_ctx.sequence.txack = 7;
    stp_core_ctx.sequence.rxack = 7;
    stp_core_ctx.sequence.winspace = MTKSTP_WINSIZE;
    stp_core_ctx.sequence.expected_rxseq = 0;
    stp_core_ctx.sequence.retry_times = 0;
    stp_core_ctx.inband_rst_set = 0;

    stp_ctx_unlock(&stp_core_ctx);
}

/*****************************************************************************
* FUNCTION
*  stp_change_rx_state
* DESCRIPTION
*  change the rx fsm of STP to "next"
* PARAMETERS
*  next     [IN] the next state of rx fsm
* RETURNS
*  void
*****************************************************************************/
static VOID stp_change_rx_state(mtkstp_parser_state next)
{
    prev_state = stp_core_ctx.parser.state;
    stp_core_ctx.parser.state = next;

    return;
}

//static void stp_tx_timeout_handler(void){
static void stp_tx_timeout_handler(ULONG data)
{
    STP_WARN_FUNC("call retry btm retry wq ...\n");
    /*shorten the softirq lattency*/
	#if WMT_PLAT_ALPS
    stop_log();
	#endif
    stp_btm_notify_stp_retry_wq(STP_BTM_CORE(stp_core_ctx));
    STP_WARN_FUNC("call retry btm retry wq ...#\n");
}

VOID stp_do_tx_timeout(VOID)
{
    UINT32 seq;
    UINT32 ret;
    UINT8 resync[4];
	
	STP_WARN_FUNC("==============================================================================\n");

    stp_ctx_lock(&stp_core_ctx);

    seq = stp_core_ctx.sequence.rxack;
    INDEX_INC(seq);

    if (seq != stp_core_ctx.sequence.txseq)
    {
        osal_memset(&resync[0], 127, 4);
        (*sys_if_tx)(&resync[0], 4, &ret);
        if (ret != 4)
        {
            STP_ERR_FUNC("mtkstp_tx_timeout_handler: send resync fail\n");
            osal_assert(0);
        }

        
        do
        {
            STP_WARN_FUNC("[stp.ctx]*rxack (=last rx ack) = %d\n\r", stp_core_ctx.sequence.rxack);
            STP_WARN_FUNC("[stp.ctx]txack (=last rx seq)= %d\n\r", stp_core_ctx.sequence.txack);
            STP_WARN_FUNC("[stp.ctx]*txseq (=next tx seq)= %d\n\r", stp_core_ctx.sequence.txseq);
            STP_WARN_FUNC("Resend STP packet from %d -> %d\n\r", seq ,
                (stp_core_ctx.sequence.txseq <= 0)?(7):(stp_core_ctx.sequence.txseq - 1));
            stp_dump_tx_queue(seq);

            stp_send_tx_queue(seq);
            INDEX_INC(seq);
        } while (seq != stp_core_ctx.sequence.txseq);
        
    }

    osal_timer_stop(&stp_core_ctx.tx_timer);
    osal_timer_start(&stp_core_ctx.tx_timer, mtkstp_tx_timeout);

    if (stp_core_ctx.sequence.winspace == MTKSTP_WINSIZE)
    {
        osal_timer_stop(&stp_core_ctx.tx_timer);
        STP_ERR_FUNC("mtkstp_tx_timeout_handler: wmt_stop_timer\n");
    }
    else
    {
        stp_core_ctx.sequence.retry_times++;
        STP_ERR_FUNC("mtkstp_tx_timeout_handler, retry = %d\n", stp_core_ctx.sequence.retry_times);

        /*If retry too much, try to recover STP by return back to initializatin state*/
        /*And not to retry again*/
        if (stp_core_ctx.sequence.retry_times > MTKSTP_RETRY_LIMIT)
        {
            osal_timer_stop(&stp_core_ctx.tx_timer);
            STP_ERR_FUNC("mtkstp_tx_timeout_handler: wmt_stop_timer\n");

            STP_ERR_FUNC("TX retry limit = %d\n", MTKSTP_RETRY_LIMIT);
            osal_assert(0);
            // stop_log();
            mtk_wcn_stp_dbg_dump_package();               	  	
            stp_notify_btm_dump(STP_BTM_CORE(stp_core_ctx));

            /*Whole Chip Reset Procedure Invoke*/
            /*if(STP_NOT_ENABLE_DBG(stp_core_ctx))*/
            {
                osal_dbg_assert_aee("[MT662x]NoAck", "**STP Tx Timeout**\n F/W has NO any RESPONSE. Please check F/W status first\n");
                if(STP_IS_ENABLE_RST(stp_core_ctx))
                {
                    STP_SET_READY(stp_core_ctx, 0);
                    stp_btm_notify_wmt_rst_wq(STP_BTM_CORE(stp_core_ctx));
                }
                else
                {
                    STP_INFO_FUNC("No to launch whole chip reset! for debugging purpose\n");
                }
            }
        }
    }

    stp_ctx_unlock(&stp_core_ctx);
	STP_WARN_FUNC("==============================================================================#\n");
    return;
}

static VOID stp_dump_data(const UINT8 *buf, const UINT8 *title,  const UINT32 len)
{
    osal_buffer_dump(buf, title, len, 32);
}

/*****************************************************************************
 * FUNCTION
 *  stp_tx_timeout_handler
 * DESCRIPTION
 *  tx timeout handler, send resync & retransmitt
 * PARAMETERS
 *  void
 * RETURNS
 *  void
 *****************************************************************************/
static VOID stp_dump_tx_queue(UINT32 txseq)
{
    INT32 tx_read, tx_length, last_len;

    tx_read = stp_core_ctx.tx_start_addr[txseq];
    tx_length = stp_core_ctx.tx_length[txseq];

    STP_ERR_FUNC("tx_seq=%d ..", txseq);

    if (tx_read + tx_length < MTKSTP_BUFFER_SIZE)
    {
        stp_dump_data(&stp_core_ctx.tx_buf[tx_read], "tx_q",  (tx_length >= 8)?(8):(tx_length));
    }
    else
    {
        last_len = MTKSTP_BUFFER_SIZE - tx_read;
        stp_dump_data(&stp_core_ctx.tx_buf[tx_read], "tx_q_0",  (last_len >= 8) ? (8):(last_len));
        stp_dump_data(&stp_core_ctx.tx_buf[0], "tx_q_0",  ((tx_length - last_len)?(8):(tx_length - last_len)));
    }
}

/*****************************************************************************
* FUNCTION
*  stp_is_apply_powersaving
* DESCRIPTION
*  Check if STP support power saving mode.
* PARAMETERS
*
* RETURNS
*  True: support power saving False: not support power saving
*****************************************************************************/
static INT32 stp_is_apply_powersaving(VOID){

    if(STP_IS_READY(stp_core_ctx) && !stp_psm_is_disable(STP_PSM_CORE(stp_core_ctx)))
    {
        //osal_dbg_print("apply power saving\n");
        return MTK_WCN_BOOL_TRUE;
    }
    else
    {
        if (mtk_wcn_stp_is_sdio_mode()) return MTK_WCN_BOOL_FALSE;

        STP_DBG_FUNC("not apply power saving\n");
        return MTK_WCN_BOOL_FALSE;
    }
}
/*****************************************************************************
* FUNCTION
*  stp_is_privileges_cmd
* DESCRIPTION
*  Check if the data is privilege command
* PARAMETERS
*
* RETURNS
*  True/False
*****************************************************************************/
static INT32 stp_is_privileges_cmd(const UINT8 *buffer, const UINT32 length, const UINT8 type)
{
    typedef struct privileges_cmd {
        UINT32 length;
        UINT8 type;
        UINT8 buf[7]; // MAX length of target command is only 5 currently
    } p_cmd_t;

    p_cmd_t p_cmd_table[] = {
        {0x05, WMT_TASK_INDX, {0x01, 0x03, 0x01, 0x00, 0x01}}, // sleep command
        {0x05, WMT_TASK_INDX, {0x01, 0x03, 0x01, 0x00, 0x02}}, // host_awake command
    };

    UINT32 i;
    UINT32 size = sizeof(p_cmd_table)/sizeof(p_cmd_table[0]);

    for (i = 0; i < size; i++)
    {
        if (type != p_cmd_table[i].type)
        {
            continue;
        }

        if (length != p_cmd_table[i].length)
        {
            continue;
        }

        if (osal_memcmp(p_cmd_table[i].buf, buffer, length))
        {
            continue;
        }

        // matched entry is found
        STP_DBG_FUNC("It's p_cmd_t\n");
        return MTK_WCN_BOOL_TRUE;
    }

    return MTK_WCN_BOOL_FALSE;
}

/*****************************************************************************
* FUNCTION
*  tx_queue_room_available
* DESCRIPTION
*  check room if availabe,
* PARAMETERS
*  buffer      [IN]        data buffer
*  length      [IN]        data buffer length
* RETURNS
*  void
*****************************************************************************/
static MTK_WCN_BOOL stp_is_tx_res_available(UINT32 length)
{
    UINT32 roomLeft;

    /*
        Get availale space of TX Queue
    */
    if (stp_core_ctx.tx_read <= stp_core_ctx.tx_write)
    {
        roomLeft = MTKSTP_BUFFER_SIZE - stp_core_ctx.tx_write + stp_core_ctx.tx_read - 1;
    }
    else
    {
        roomLeft = stp_core_ctx.tx_read - stp_core_ctx.tx_write - 1;
    }

    if (roomLeft < length)
    {
        STP_ERR_FUNC("%s: tx queue room shortage\n", __FUNCTION__);
        return MTK_WCN_BOOL_FALSE;
    }
    else
    {
        return MTK_WCN_BOOL_TRUE;
    };
}

/*****************************************************************************
* FUNCTION
*  stp_add_to_tx_queue
* DESCRIPTION
*  put data to tx queue
* PARAMETERS
*  buffer      [IN]        data buffer
*  length      [IN]        data buffer length
* RETURNS
*  void
*****************************************************************************/
static VOID stp_add_to_tx_queue(const UINT8 *buffer, UINT32 length)
{
    UINT32 last_len;

    // Get availale space of TX Queue
    if (length + stp_core_ctx.tx_write < MTKSTP_BUFFER_SIZE)
    {
        osal_memcpy(stp_core_ctx.tx_buf + stp_core_ctx.tx_write, buffer, length);
        stp_core_ctx.tx_write += length;
    }
    else
    {
        last_len = MTKSTP_BUFFER_SIZE - stp_core_ctx.tx_write;
        osal_memcpy(stp_core_ctx.tx_buf + stp_core_ctx.tx_write, buffer, last_len);
        osal_memcpy(stp_core_ctx.tx_buf, buffer + last_len, length - last_len);

        stp_core_ctx.tx_write = length - last_len;
    }

    return;
}

/*****************************************************************************
* FUNCTION
*  stp_add_to_rx_queue
* DESCRIPTION
*  put data to corresponding task's rx queue and notify corresponding task
* PARAMETERS
*  buffer      [IN]        data buffer
*  length      [IN]        data buffer length
*  type        [IN]        corresponding task index
* RETURNS
*  INT32    0=success, others=error
*****************************************************************************/
static INT32 stp_add_to_rx_queue(UINT8 *buffer, UINT32 length, UINT8 type)
{
    UINT32 roomLeft, last_len;

    stp_ctx_lock(&stp_core_ctx);

    if (stp_core_ctx.ring[type].read_p <= stp_core_ctx.ring[type].write_p)
    {
        roomLeft = MTKSTP_BUFFER_SIZE - stp_core_ctx.ring[type].write_p + stp_core_ctx.ring[type].read_p - 1;
    }
    else
    {
        roomLeft = stp_core_ctx.ring[type].read_p - stp_core_ctx.ring[type].write_p - 1;
    }

    if (roomLeft < length)
    {
        stp_ctx_unlock(&stp_core_ctx);
        STP_ERR_FUNC("Queue is full !!!, type = %d\n", type);
        osal_assert(0);
        return (-1);
    }

    if (length + stp_core_ctx.ring[type].write_p < MTKSTP_BUFFER_SIZE)
    {
        osal_memcpy(stp_core_ctx.ring[type].buffer + stp_core_ctx.ring[type].write_p, buffer, length);
        stp_core_ctx.ring[type].write_p += length;
    }
    else
    {
        last_len = MTKSTP_BUFFER_SIZE - stp_core_ctx.ring[type].write_p;
        osal_memcpy(stp_core_ctx.ring[type].buffer + stp_core_ctx.ring[type].write_p, buffer, last_len);
        osal_memcpy(stp_core_ctx.ring[type].buffer, buffer + last_len, length - last_len);
        stp_core_ctx.ring[type].write_p = length - last_len;
    }

    stp_ctx_unlock(&stp_core_ctx);

    return 0;
}

/*****************************************************************************
* FUNCTION
*  stp_send_tx_queue
* DESCRIPTION
*  send data in tx buffer to common interface
* PARAMETERS
*  txseq       [IN]        sequence number of outgoing packet in tx buffer
* RETURNS
*  void
*****************************************************************************/
static VOID stp_send_tx_queue(UINT32 txseq)
{
    UINT32 ret;
    INT32 tx_read, tx_length, last_len;

    tx_read = stp_core_ctx.tx_start_addr[txseq];
    tx_length = stp_core_ctx.tx_length[txseq];

    stp_update_tx_queue(txseq);

    if (tx_read + tx_length < MTKSTP_BUFFER_SIZE)
    {

        (*sys_if_tx)(&stp_core_ctx.tx_buf[tx_read], tx_length, &ret);

        if (ret != tx_length)
        {
            STP_ERR_FUNC("stp_send_tx_queue, %d/%d\n", tx_length, ret);
            osal_assert(0);
        }
    }
    else
    {
        last_len = MTKSTP_BUFFER_SIZE - tx_read;
        (*sys_if_tx)(&stp_core_ctx.tx_buf[tx_read], last_len, &ret);

        if (ret != last_len)
        {
            STP_ERR_FUNC("stp_send_tx_queue, %d/%d\n", last_len, ret);
            osal_assert(0);
        }

        (*sys_if_tx)(&stp_core_ctx.tx_buf[0], tx_length - last_len, &ret);

        if (ret != tx_length - last_len)
        {
            STP_ERR_FUNC("stp_send_tx_queue, %d/%d\n", tx_length - last_len, ret);
            osal_assert(0);
        }
    }

    return;
}


/*****************************************************************************
* FUNCTION
*  stp_send_ack
* DESCRIPTION
*  send ack packet to the peer
* PARAMETERS
*  txAck    [IN] Ack number
*  nak      [IN] 0 = ack; !0 = NAK
* RETURNS
*  void
*****************************************************************************/
static VOID stp_send_ack(UINT8  txAck, UINT8 nak)
{
    UINT8  mtkstp_header[MTKSTP_HEADER_SIZE];
    UINT32 ret;
    INT32  iStatus;

    mtkstp_header[0] = 0x80 + (0 << 3) + txAck;//stp_core_ctx.sequence.txack;

    if (fgEnableNak == 0)
    {
        mtkstp_header[1] = 0x00;    // disable NAK
    }
    else
    {
        mtkstp_header[1] = ((nak == 0) ? 0x00 : 0x80);
    }

    mtkstp_header[2] = 0;
    mtkstp_header[3] = (mtkstp_header[0] + mtkstp_header[1] + mtkstp_header[2]) & 0xff;

    stp_dbg_pkt_log(STP_TASK_INDX,
        txAck,
        0,
        0,
        PKT_DIR_TX,
        NULL,
        0);

    if(fgEnableDelimiter == 1)
    {
        iStatus = (*sys_if_tx)(&stp_delimiter[0], STP_DEL_SIZE, &ret);
        STP_DUMP_PACKET_HEAD((PUINT8)&stp_delimiter[0], "tx del", STP_DEL_SIZE);
        if (ret != STP_DEL_SIZE)
        {
            STP_ERR_FUNC("stp_send_ack, %d/%d status %d\n", STP_DEL_SIZE, ret, iStatus);
            osal_assert(0);
        }
    }

    iStatus = (*sys_if_tx)(&mtkstp_header[0], MTKSTP_HEADER_SIZE, &ret);

    if (ret != MTKSTP_HEADER_SIZE)
    {
        STP_ERR_FUNC("stp_send_ack, %d/%d status %d\n", MTKSTP_HEADER_SIZE, ret, iStatus);
        osal_assert(0);
    }

    return;
}



INT32 stp_send_data_no_ps(UINT8 *buffer, UINT32 length, UINT8 type)
{
    UINT8 mtkstp_header[MTKSTP_HEADER_SIZE], temp[2];
    UINT8 *p_tx_buf = NULL;
    UINT16 crc;
    INT32 ret = 0;

    stp_ctx_lock(&stp_core_ctx);

    /*Only WMT can set raw data */
    if(STP_NOT_ENABLE(stp_core_ctx) && WMT_TASK_INDX != type)
    {
        //no op
    }
    else if (STP_NOT_ENABLE(stp_core_ctx) && WMT_TASK_INDX == type)
    {
        //ret = mtk_wcn_stp_send_data_raw(buffer, length, type);
    }

    // STP over SDIO
    else if ((mtk_wcn_stp_is_sdio_mode() || mtk_wcn_stp_is_uart_mand_mode()) && STP_IS_ENABLE(stp_core_ctx))
    {
        osal_printtimeofday("[ STP][SDIO][ B][W]");

        mtkstp_header[0] = 0x80;
        mtkstp_header[1] = (type << 4) + (((length)>>8) & 0x0f);
        mtkstp_header[2] = (length) & 0xff;
        mtkstp_header[3] = 0x00;

        p_tx_buf = &stp_core_ctx.tx_buf[0];
        osal_memcpy(p_tx_buf, mtkstp_header, MTKSTP_HEADER_SIZE);
        p_tx_buf += MTKSTP_HEADER_SIZE;

        osal_memcpy(p_tx_buf, buffer, length);
        p_tx_buf += length;

        temp[0] = 0x00;
        temp[1] = 0x00;
        osal_memcpy(p_tx_buf, temp, 2);
        stp_dbg_pkt_log(type,
                stp_core_ctx.sequence.txack,
                stp_core_ctx.sequence.txseq,
                0,
                PKT_DIR_TX,
                buffer,
                length);
        (*sys_if_tx)(&stp_core_ctx.tx_buf[0], (MTKSTP_HEADER_SIZE+length+2), &ret);
        if ( (MTKSTP_HEADER_SIZE+length+2) != ret )
        {
            STP_ERR_FUNC("stp send tx packet: %d, maybe stp_if_tx == NULL\n", ret);
            osal_assert(0);
            ret = 0;
        }
        else
        {
            ret = (INT32)length;
        }

        osal_printtimeofday("[ STP][SDIO][ E][W]");
    }

    // STP over UART
    else if ( mtk_wcn_stp_is_uart_fullset_mode() && STP_IS_ENABLE(stp_core_ctx))
    {

        if ((stp_core_ctx.sequence.winspace > 0) &&
                (stp_is_tx_res_available(MTKSTP_HEADER_SIZE + length + MTKSTP_CRC_SIZE)))
        {
            mtkstp_header[0] = 0x80 + (stp_core_ctx.sequence.txseq << 3) + stp_core_ctx.sequence.txack;
            mtkstp_header[1] = (type << 4) + ((length & 0xf00) >> 8);
            mtkstp_header[2] = length & 0xff;
            mtkstp_header[3] = (mtkstp_header[0] + mtkstp_header[1] + mtkstp_header[2]) & 0xff;

            stp_core_ctx.tx_start_addr[stp_core_ctx.sequence.txseq] = stp_core_ctx.tx_write;
            stp_core_ctx.tx_length[stp_core_ctx.sequence.txseq] = MTKSTP_HEADER_SIZE + length + 2;

            if (fgEnableDelimiter == 1)
            {
                stp_core_ctx.tx_length[stp_core_ctx.sequence.txseq] += STP_DEL_SIZE;
                stp_add_to_tx_queue(&stp_delimiter[0], STP_DEL_SIZE);
            }

            stp_add_to_tx_queue(mtkstp_header, MTKSTP_HEADER_SIZE);

            /*Make Payload*/
            stp_add_to_tx_queue(buffer, length);

            /*Make CRC*/
            crc = osal_crc16(buffer, length);
            temp[0] = crc & 0xff;
            temp[1] = (crc & 0xff00) >> 8;
            stp_add_to_tx_queue(temp, 2);
            
            stp_dbg_pkt_log(type,
                stp_core_ctx.sequence.txack,
                stp_core_ctx.sequence.txseq,
                crc,
                PKT_DIR_TX,
                buffer,
                length);
            
            /*Kick to UART*/
            stp_send_tx_queue(stp_core_ctx.sequence.txseq);
            INDEX_INC(stp_core_ctx.sequence.txseq);
            stp_core_ctx.sequence.winspace--;

            /*Setup the Retry Timer*/
            osal_timer_stop(&stp_core_ctx.tx_timer);
            if (stp_core_ctx.sequence.winspace != MTKSTP_WINSIZE)
            {
                osal_timer_start(&stp_core_ctx.tx_timer, mtkstp_tx_timeout);
            }
            else
            {
                STP_ERR_FUNC("mtk_wcn_stp_send_data: wmt_stop_timer\n");
            }
            ret = (INT32)length;
        }
        else
        {
            /* No winspace to send. Let caller retry*/
            STP_ERR_FUNC("%s: There is no winspace/txqueue to send !!!\n", __FUNCTION__);
            ret = 0;
        }
    }

    stp_ctx_unlock(&stp_core_ctx);

    return ret;
}

/*****************************************************************************
* FUNCTION
*  stp_process_rxack
* DESCRIPTION
*  process ack packet
* PARAMETERS
*  void
* RETURNS
*  INT32    0=success, others=error
*****************************************************************************/
static INT32 stp_process_rxack(VOID)
{
    INT32 j, k;
    UINT8 rxack;
    INT32 fgResult = (-1);
    if (stp_core_ctx.sequence.rxack != stp_core_ctx.parser.ack)
    {
        j = k = 0;
        rxack = stp_core_ctx.sequence.rxack;
        INDEX_INC(rxack);
        while (rxack != stp_core_ctx.sequence.txseq)
        {
            j++;
            if (rxack == stp_core_ctx.parser.ack)
            {
                k = 1;
                break;
            }
            INDEX_INC(rxack);
        }
        if (k == 1)
        {
            stp_core_ctx.sequence.rxack = stp_core_ctx.parser.ack;
            stp_core_ctx.tx_read = stp_core_ctx.tx_start_addr[rxack] + stp_core_ctx.tx_length[rxack];
            if (stp_core_ctx.tx_read >= MTKSTP_BUFFER_SIZE)
            {
                stp_core_ctx.tx_read -= MTKSTP_BUFFER_SIZE;
            }
            stp_core_ctx.sequence.winspace += j;
            stp_core_ctx.sequence.retry_times = 0;

            osal_timer_stop(&stp_core_ctx.tx_timer);
            if (stp_core_ctx.sequence.winspace != MTKSTP_WINSIZE)
            {
               osal_timer_start(&stp_core_ctx.tx_timer, mtkstp_tx_timeout);
            }
            fgResult = 0;
        }
    }

    return fgResult;
}

/*****************************************************************************
* FUNCTION
*  stp_process_packet
* DESCRIPTION
*  process STP packet
* PARAMETERS
*  void
* RETURNS
*  void
*****************************************************************************/
static VOID stp_process_packet(VOID)
{
    INT32 fgTriggerResume = (-1);
    UINT8   txAck = 0;
    static INT32 fgRxOk = 0;
    MTK_WCN_BOOL b;
    MTK_WCN_BOOL is_function_active = 0;
    static INT32 stp_process_packet_fail_count = 0;
        
    stp_dbg_pkt_log(stp_core_ctx.parser.type,
        stp_core_ctx.parser.ack,
        stp_core_ctx.parser.seq,
        stp_core_ctx.parser.crc,
        PKT_DIR_RX,
        stp_core_ctx.rx_buf,
        stp_core_ctx.parser.length);
    /*Optimization*/
    /*If bluez, direct send packet to hci_core not through RX buffer!*/
    if((stp_core_ctx.sequence.expected_rxseq == stp_core_ctx.parser.seq) &&
        (stp_core_ctx.parser.type == BT_TASK_INDX) && STP_BT_STK_IS_BLUEZ(stp_core_ctx))
    {
            /*Indicate packet to hci_stp*/
            STP_DBG_FUNC("Send Packet to BT_SUBFUCTION, len = %d\n", stp_core_ctx.rx_counter);

            b=mtk_wcn_sys_if_rx(stp_core_ctx.rx_buf, stp_core_ctx.rx_counter);
            if(b)
            {
                STP_ERR_FUNC("mtk_wcn_sys_if_rx is NULL\n");
            }

            stp_ctx_lock(&stp_core_ctx);

            /*Process rx ack*/
            fgTriggerResume = stp_process_rxack();
            stp_core_ctx.sequence.txack = stp_core_ctx.parser.seq;
            INDEX_INC(stp_core_ctx.sequence.expected_rxseq);
            txAck = stp_core_ctx.sequence.txack;

            /*Send ack back*/
            stp_send_ack(txAck, 0);

            stp_ctx_unlock(&stp_core_ctx);

            fgRxOk = 0;
    }
    /* sequence matches expected, enqueue packet*/
    else if (stp_core_ctx.sequence.expected_rxseq == stp_core_ctx.parser.seq)
    {
        is_function_active = ((*sys_check_function_status)(stp_core_ctx.parser.type, OP_FUNCTION_ACTIVE) == STATUS_FUNCTION_ACTIVE);
        /*If type is valid and function works, then try to enqueue*/
        if ((stp_core_ctx.parser.type < MTKSTP_MAX_TASK_NUM) && (is_function_active == MTK_WCN_BOOL_TRUE))
        {
            if(stp_core_ctx.parser.type == BT_TASK_INDX)            
            {                
                const static UINT8 rst_buf[7] = {0x04, 0x0e, 0x04, 0x01, 0x3, 0xc,0x00};                
                if(!osal_strncmp(stp_core_ctx.rx_buf, rst_buf, 7))                
                {                    
                    osal_printtimeofday("############ BT Rest end <--");                
                }            
            }
        
            

            fgRxOk = stp_add_to_rx_queue(stp_core_ctx.rx_buf, stp_core_ctx.rx_counter, stp_core_ctx.parser.type);
        }
        else
        {
            if(is_function_active == MTK_WCN_BOOL_FALSE)
            {
                STP_ERR_FUNC("function type = %d is inactive, so no en-queue to rx\n", stp_core_ctx.parser.type);
                fgRxOk = 0;/*drop packet*/
            }
            else
            {
                STP_ERR_FUNC("mtkstp_process_packet: type = %x, the type is invalid\n", stp_core_ctx.parser.type);
                fgRxOk = 0;/*drop packet*/
            }
        }

        /* enqueue successfully */
        if(fgRxOk == 0)
        {
            stp_process_packet_fail_count = 0;
            /*notify corresponding subfunction of incoming data*/
            (*sys_event_set)(stp_core_ctx.parser.type);

            stp_ctx_lock(&stp_core_ctx);

            fgTriggerResume = stp_process_rxack();
            stp_core_ctx.sequence.txack = stp_core_ctx.parser.seq;
            INDEX_INC(stp_core_ctx.sequence.expected_rxseq);

            /*Send tx ack*/
            txAck = stp_core_ctx.sequence.txack;
            stp_send_ack(txAck, 0);

            stp_ctx_unlock(&stp_core_ctx);
        }
        else
        {
            stp_process_packet_fail_count++;
            /*Queue is full*/
            if(stp_core_ctx.parser.type == GPS_TASK_INDX)
            {
                /*Clear Rx Queue if GPS*/
                mtk_wcn_stp_flush_rx_queue(GPS_TASK_INDX);
            }
            else
            {
                /*notify corresponding subfunction of incoming data*/
                (*sys_event_set)(stp_core_ctx.parser.type);
            }
            /*enqueue fail, don't send ack and wait for peer retry*/
            STP_ERR_FUNC("Enqueue to Rx queue fail, maybe function %d queue is full\n", stp_core_ctx.parser.type);
        }
    }
    /*sequence not match && previous packet enqueue successfully, send the previous ACK*/
    else if(fgRxOk == 0)
    {
        STP_ERR_FUNC("mtkstp_process_packet: expected_rxseq = %d, parser.seq = %d\n", stp_core_ctx.sequence.expected_rxseq, stp_core_ctx.parser.seq);
        stp_process_packet_fail_count++;

        stp_ctx_lock(&stp_core_ctx);
        txAck = stp_core_ctx.sequence.txack;
        stp_send_ack(txAck, 1);
        stp_ctx_unlock(&stp_core_ctx);
		STP_ERR_FUNC("sequence not match && previous packet enqueue successfully, send the previous ACK (ack no =%d)\n", txAck);
    }
    /*sequence not match && previous packet enqueue failed, do nothing, make the other side timeout*/
    else
    {
        stp_process_packet_fail_count++;
        STP_ERR_FUNC("sequence not match && previous packet enqueue failed, do nothing, make the other side timeout\n");
    }

    if(fgTriggerResume == 0)
    {
        /*[PatchNeed]Just Notificaiton, not blocking call*/
        (*sys_event_tx_resume)(stp_core_ctx.sequence.winspace); // notify adaptation layer for possible tx resume mechanism
    }

    if(stp_process_packet_fail_count > MTKSTP_RETRY_LIMIT)
    {
        stp_process_packet_fail_count = 0;
        STP_ERR_FUNC("The process packet fail count > 10 lastly\n\r, whole chip reset\n\r");
		#if WMT_PLAT_ALPS
        stop_log();                     //dump_uart_history();
        #endif
        mtk_wcn_stp_dbg_dump_package();                     
        stp_notify_btm_dump(STP_BTM_CORE(stp_core_ctx));

        /*Whole Chip Reset Procedure Invoke*/
        /*if(STP_NOT_ENABLE_DBG(stp_core_ctx))*/
        {
            //(*sys_dbg_assert_aee)("[MT662x]Ack Miss", "**STP Ack Miss**\n Ack Miss.\n");
            osal_dbg_assert_aee("[MT662x]Ack Miss", "**STP Ack Miss**\n Ack Miss.\n");

            if(STP_IS_ENABLE_RST(stp_core_ctx))
            {
                STP_SET_READY(stp_core_ctx, 0);
                stp_btm_notify_wmt_rst_wq(STP_BTM_CORE(stp_core_ctx));
            }
            else
            {
                STP_INFO_FUNC("No to launch whole chip reset! for debugging purpose\n");
            }
        }
    }

    return;
}

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
INT32 mtk_wcn_stp_init(const mtkstp_callback * const cb_func)
{
    INT32 ret = 0;
    INT32 i = 0;

    /* Function pointer to point to the currently used transmission interface
    */
    sys_if_tx           = cb_func->cb_if_tx;

    /* Used to inform the function driver has received the corresponding type of information*/
    sys_event_set       = cb_func->cb_event_set;

    /*  Used to inform the function driver can continue to send information and 
        STP has resources to deal with
    */
    sys_event_tx_resume = cb_func->cb_event_tx_resume;

    /*  STP driver determines whether the function is enable. If not enable and 
        STP has received the kind of information, and STP have the right to put it away.
    */
    sys_check_function_status = cb_func->cb_check_funciton_status;

    stp_ctx_lock_init(&stp_core_ctx);

    /*  Setup timer to be used to check if f/w receive the data in the specific time 
        interval after being sent
    */
    for(i = 0; i < MTKSTP_MAX_TASK_NUM; i++)
    {
        osal_unsleepable_lock_init(&stp_core_ctx.ring[i].mtx);
    }
    stp_core_ctx.tx_timer.timeoutHandler = stp_tx_timeout_handler;
    stp_core_ctx.tx_timer.timeroutHandlerData = 0;
    osal_timer_create(&stp_core_ctx.tx_timer);

    STP_SET_BT_STK(stp_core_ctx, 0);
    STP_SET_ENABLE(stp_core_ctx, 0);
    STP_SET_ENABLE_DBG(stp_core_ctx, 0);
    STP_SET_ENABLE_RST(stp_core_ctx, 0);
    STP_SET_PENDING_TYPE(stp_core_ctx, 0);
    STP_SET_READY(stp_core_ctx, 0);
    STP_SET_SUPPORT_PROTOCOL(stp_core_ctx, 0);
    STP_SET_PSM_CORE(stp_core_ctx, stp_psm_init());
    STP_SET_FW_COREDUMP_FLAG(stp_core_ctx, 0);
    STP_ENABLE_FW_COREDUMP(stp_core_ctx, 0);
    STP_SET_WMT_LAST_CLOSE(stp_core_ctx,0);

    if(!STP_PSM_CORE(stp_core_ctx))
    {
        ret = (-3);
        goto ERROR;
    }

    STP_SET_BTM_CORE(stp_core_ctx, stp_btm_init());
    if(!STP_BTM_CORE(stp_core_ctx))
    {
        STP_ERR_FUNC("STP_BTM_CORE(stp_core_ctx) initialization fail!\n")
        ret = (-3);
        goto ERROR;
    }
    
    if(STP_BTM_CORE(stp_core_ctx) != NULL)
    {
        g_mtkstp_dbg = stp_dbg_init(STP_BTM_CORE(stp_core_ctx));
    }
    else
    {
        g_mtkstp_dbg = stp_dbg_init(NULL);
    }

    if(!g_mtkstp_dbg)
    {
          STP_ERR_FUNC("g_mtkstp_dbg initialization fail!\n");
          ret = (-3);
          goto ERROR;
    }
    STP_SET_ENABLE_RST(stp_core_ctx, 1);
#ifdef CONFIG_LOG_STP_INTERNAL
    mtk_wcn_stp_dbg_enable();
#else 
    mtk_wcn_stp_dbg_enable();
#endif
    goto RETURN;

ERROR:
    stp_psm_deinit(STP_PSM_CORE(stp_core_ctx));

RETURN:
    return ret;

}

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
INT32 mtk_wcn_stp_deinit(void)
{

    sys_if_tx           = NULL;
    sys_event_set       = NULL;
    sys_event_tx_resume = NULL;
    sys_check_function_status = NULL;

    stp_dbg_deinit(g_mtkstp_dbg);
    stp_btm_deinit(STP_BTM_CORE(stp_core_ctx));
    stp_psm_deinit(STP_PSM_CORE(stp_core_ctx));
    return 0;
}

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

INT32 mtk_wcn_stp_btm_get_dmp(CHAR *buf, INT32 *len)
{
    return stp_dbg_dmp_out(g_mtkstp_dbg, buf, len);
}

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
INT32 mtk_wcn_stp_psm_notify_stp(const MTKSTP_PSM_ACTION_T action){
    return stp_psm_notify_stp(STP_PSM_CORE(stp_core_ctx), action);
}

INT32 mtk_wcn_stp_set_psm_state(MTKSTP_PSM_STATE_T state){
    return stp_psm_set_state(STP_PSM_CORE(stp_core_ctx), state);
}


/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_psm_enable
* DESCRIPTION
*  enable STP sleep/wakeup support
* PARAMETERS
*  void
* RETURNS
*  0: Sccuess  Negative value: Fail
*****************************************************************************/
INT32 mtk_wcn_stp_psm_enable(INT32 idle_time_to_sleep)
{
#if 0
    if (MTK_WCN_BOOL_TRUE == stp_psm_is_quick_ps_support())
    {
	    if( mtk_wcn_stp_is_ready())
	    {
	        return stp_psm_enable(STP_PSM_CORE(stp_core_ctx), idle_time_to_sleep);
	    }
	    else
	    {
	        STP_WARN_FUNC("STP Not Ready, Dont do Sleep/Wakeup\n");
	        return -1;
	    }
    }
    else
    {
	    if( mtk_wcn_stp_is_ready() && mtk_wcn_stp_is_uart_fullset_mode())
	    {
	        return stp_psm_enable(STP_PSM_CORE(stp_core_ctx), idle_time_to_sleep);
	    }
	    else if (mtk_wcn_stp_is_sdio_mode())
	    {
	        stp_psm_enable(STP_PSM_CORE(stp_core_ctx), idle_time_to_sleep);
	        STP_DBG_FUNC("PSM is not support under SDIO mode\n");
	        return 0;
	    }
	    else
	    {
	        STP_WARN_FUNC("STP Not Ready, Dont do Sleep/Wakeup\n");
	        return -1;
	    }   
    }
#else
    if( mtk_wcn_stp_is_ready() && mtk_wcn_stp_is_uart_fullset_mode())
	{
	    return stp_psm_enable(STP_PSM_CORE(stp_core_ctx), idle_time_to_sleep);
	}
	else if (mtk_wcn_stp_is_sdio_mode())
	{
	    stp_psm_enable(STP_PSM_CORE(stp_core_ctx), idle_time_to_sleep);
	    STP_DBG_FUNC("PSM is not support under SDIO mode\n");
	    return 0;
	}
	else
	{
	    STP_WARN_FUNC("STP Not Ready, Dont do Sleep/Wakeup\n");
	    return -1;
	}
#endif
}

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_psm_disable
* DESCRIPTION
*  disable STP sleep/wakeup support
* PARAMETERS
*  void
* RETURNS
*  0: Sccuess  Negative value: Fail
*****************************************************************************/
extern INT32 mtk_wcn_stp_psm_disable(VOID)
{
#if 0
    if (MTK_WCN_BOOL_TRUE == stp_psm_is_quick_ps_support())
    {
	    if( mtk_wcn_stp_is_ready())
	    {
	        return stp_psm_disable(STP_PSM_CORE(stp_core_ctx));
	    }
	    else
	    {
	        STP_WARN_FUNC("STP Not Ready, Dont do Sleep/Wakeup\n");
	        return -1;
	    }
    }
    else
    {
	    if( mtk_wcn_stp_is_ready() && mtk_wcn_stp_is_uart_fullset_mode())
	    {
	        return stp_psm_disable(STP_PSM_CORE(stp_core_ctx));
	    }
	    else if (mtk_wcn_stp_is_sdio_mode())
	    {
	        stp_psm_disable(STP_PSM_CORE(stp_core_ctx));
	        return 0;
	    }
	    else
	    {
	        STP_WARN_FUNC("STP Not Ready, Dont do Sleep/Wakeup\n");
	        return -1;
	    }
    }
#else
    if( mtk_wcn_stp_is_ready() && mtk_wcn_stp_is_uart_fullset_mode())
	{
	    return stp_psm_disable(STP_PSM_CORE(stp_core_ctx));
	}
	else if (mtk_wcn_stp_is_sdio_mode())
	{
	    stp_psm_disable(STP_PSM_CORE(stp_core_ctx));
        return 0;
    }
    else
    {
        STP_WARN_FUNC("STP Not Ready, Dont do Sleep/Wakeup\n");
        return -1;
    }
#endif
}

extern INT32 mtk_wcn_stp_psm_reset(VOID)
{
    return stp_psm_reset(STP_PSM_CORE(stp_core_ctx));
}

extern INT32 mtk_wcn_stp_dbg_disable(VOID)
{
    if(STP_IS_ENABLE_DBG(stp_core_ctx))
    {
        STP_INFO_FUNC("STP dbg mode is turned off\n");
        STP_SET_ENABLE_DBG(stp_core_ctx, 0);
        stp_dbg_disable(g_mtkstp_dbg);
    }
    else
    {
        STP_WARN_FUNC("STP dbg mode has been turned off\n");
    }

    return 0;
}

extern INT32 mtk_wcn_stp_dbg_enable(VOID)
{
    if(STP_NOT_ENABLE_DBG(stp_core_ctx))
    {
        STP_INFO_FUNC("STP dbg mode is turned on\n");
        STP_SET_ENABLE_DBG(stp_core_ctx, 1);
        stp_dbg_enable(g_mtkstp_dbg);
    }
    else
    {
        STP_WARN_FUNC("STP dbg mode has been turned on\n")
    }

    return 0;
}

INT32 mtk_wcn_stp_dbg_log_ctrl(UINT32 on)
{
    stp_dbg_log_ctrl (on);
    return 0;
}

INT32 mtk_wcn_stp_coredump_flag_ctrl(UINT32 on)
{
    STP_ENABLE_FW_COREDUMP(stp_core_ctx, on);
	STP_INFO_FUNC("%s coredump function.\n", 0 == on ? "disable" : "enable")
    return 0;
}

INT32 mtk_wcn_stp_coredump_flag_get(VOID)
{
    return STP_ENABLE_FW_COREDUMP_FLAG(stp_core_ctx);
}








/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_parser_data
* DESCRIPTION
*  push data to serial transport protocol parser engine
* PARAMETERS
*  buffer      [IN]        data buffer
*  length      [IN]        data buffer length
* RETURNS
*  INT32            0 = sucess; -1 = crc/checksum error
*****************************************************************************/
INT32 mtk_wcn_stp_parser_data(UINT8 *buffer, UINT32 length)
{
    /*----------------------------------------------------------------*/
    /* Local Variables                                                */
    /*----------------------------------------------------------------*/
    INT32 i;
    UINT8 *p_data;
    UINT8 padding_len = 0;
    INT32 remain_length; // GeorgeKuo: sync from MAUI, change to unsigned
    MTK_WCN_BOOL is_function_active = 0;
#ifdef DEBUG_DUMP_PACKET_HEAD
    static UINT32 counter = 0;
    STP_TRACE_FUNC("++, rx (cnt=%d,len=%d)\n", ++counter, length);
#endif

#if 0
#ifdef CONFIG_POWER_SAVING_SUPPORT
     if(stp_is_apply_powersaving()) {
        //If now chip is awake, to restart monitor!
        if(!stp_psm_is_to_block_traffic(STP_PSM_CORE(stp_core_ctx))){
            STP_DBG_FUNC("To restart moinotr when rx\n\r");
            stp_psm_start_monitor(STP_PSM_CORE(stp_core_ctx));
        }
     }
#endif
#endif

    /*----------------------------------------------------------------*/
    /* Code Body                                                      */
    /*----------------------------------------------------------------*/
    /* George FIXME: WHY or HOW can we reduct the locked region? */
    /*flags = (*sys_mutex_lock)(stp_core_ctx.stp_mutex);*/
    i = length;
    p_data = (UINT8 *)buffer;

//    stp_dump_data(buffer, "rx queue", length);

    /*STP is not enabled and only WMT can use Raw data path*/
    if (STP_NOT_ENABLE(stp_core_ctx) && WMT_TASK_INDX == STP_PENDING_TYPE(stp_core_ctx))
    {
        //(*sys_dbg_print)("mtk_wcn_stp_parser_data, buff = %x %x %x %x %x %x %x %x %x %x", buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5], buffer[6], buffer[7], buffer[8], buffer[9]);
        stp_add_to_rx_queue(buffer, length, STP_PENDING_TYPE(stp_core_ctx)); //route to task who send command

        /* mike: notify corresponding subfunction of incoming data */
        (*sys_event_set)(STP_PENDING_TYPE(stp_core_ctx));
    }
    // STP over SDIO
    else if ( (mtk_wcn_stp_is_sdio_mode()||mtk_wcn_stp_is_uart_mand_mode()) && STP_IS_ENABLE(stp_core_ctx))
    {
#if !(REMOVE_USELESS_LOG)
        if(gStpDbgLvl >= STP_LOG_DBG)
        {
            stp_dump_data(buffer, "sdio parser_in", length);
        }
#endif
        //STP_DBG_FUNC("sdio stp parser data length = %d\n", length);
        while ( i > 0 ) {
            switch ( stp_core_ctx.parser.state )
            {
                case MTKSTP_SYNC: // b'10
                    //if (((*p_data & 0x80) == 0x80) && ((*p_data & 0x40) == 0x00))
                    //if(*p_data == 0x80)
                    if((*p_data & 0x80) == 0x80)
                    {
                        //STP_DBG_FUNC("[STP] STP Packet Start =========> \n");
                        if(*p_data != 0x80) {
                            STP_WARN_FUNC("SDIO not 0x80!!(0x%x)\n", *p_data);
                        }

                        if (i >= 4) {
#if !(REMOVE_USELESS_LOG)
                            if (gStpDbgLvl >= STP_LOG_DBG) {/*print header, when get the full STP header*/
                                INT32 type = (*(p_data+1) & 0x70) >> 4;
                                PCHAR type_name ="<UNKOWN>";

                                //if(type == BT_TASK_INDX){
                                    if(type == BT_TASK_INDX)
                                        type_name="< BT>";
                                    else if(type == GPS_TASK_INDX)
                                        type_name="<GPS>";
                                    else if(type == WMT_TASK_INDX)
                                        type_name="<WMT>";
                                    else if(type == FM_TASK_INDX)
                                        type_name="< FM>";

                                    STP_DBG_FUNC("STP Rx Header: [%02x %02x %02x %02x] type=%s, len=%d, seq=%d, ack=%d\n",
                                                *p_data, *(p_data+1), *(p_data+2), *(p_data+3),
                                                type_name,
                                                ((*(p_data+1) & 0x0f) << 8) + *(p_data+2),
                                                (*p_data & 0x38) >> 3,
                                                 *p_data & 0x07 );
                            }
#endif                            
                        }
                        else{
                              STP_WARN_FUNC("STP Rx: discard due to i < 4 (%d)\n", i);
                        }

                        //STP_DBG_FUNC("[STP] sync->nak\n");
                        stp_change_rx_state(MTKSTP_NAK);
                        stp_core_ctx.rx_counter++;
                    }
                    else
                    {
                        STP_WARN_FUNC("sync to sync!!(0x%x)\n", *p_data);
                        stp_change_rx_state(MTKSTP_SYNC);
                    }
                    break;

                case MTKSTP_NAK:
                    //STP_DBG_FUNC("[STP] nak->length\n");
                    stp_change_rx_state(MTKSTP_LENGTH);
                    stp_core_ctx.parser.type = (*p_data & 0x70) >> 4;
                    if ( stp_core_ctx.parser.type <= MTKSTP_MAX_TASK_NUM )
                    {
                        stp_core_ctx.parser.length = (*p_data & 0x0f) << 8;
                        stp_core_ctx.rx_counter++;
                    }
                    else
                    {
                        STP_WARN_FUNC("nak to sync\n");
                        stp_change_rx_state(MTKSTP_SYNC);
                    }
                    break;

                case MTKSTP_LENGTH:
                    //STP_DBG_FUNC("[STP] length -> checksum\n");
                    stp_change_rx_state(MTKSTP_CHECKSUM);
                    stp_core_ctx.parser.length += *p_data;

                    /*Valid length checking*/
                    if(stp_core_ctx.parser.length < 2000){
                        stp_core_ctx.rx_counter++;
                    } else {
                        STP_WARN_FUNC("The length of STP packet is not valid !!! length = %d\n", stp_core_ctx.parser.length);
                        stp_change_rx_state(MTKSTP_SYNC);
                        stp_core_ctx.rx_counter = 0;
                        //return -1;
                    }

                    break;

                case MTKSTP_CHECKSUM:

                    if ((stp_core_ctx.parser.type == STP_TASK_INDX)||
                        (stp_core_ctx.parser.type == INFO_TASK_INDX)){
                        stp_change_rx_state(MTKSTP_FW_MSG);
                        stp_core_ctx.rx_counter = 0;
                        i -= 1;
                        if(i != 0){
                            p_data += 1;
                        }    
                        continue;
                    }
                    
                    if (stp_core_ctx.parser.length == 0)
                    {
                        STP_WARN_FUNC("checksum to sync\n");
                        stp_change_rx_state(MTKSTP_SYNC);
                        stp_core_ctx.rx_counter = 0;
                    }
                    else
                    {
                        //STP_DBG_FUNC("[STP] checksum->data\n");
                        stp_change_rx_state(MTKSTP_DATA);
                        stp_core_ctx.rx_counter = 0;
                    }
                    break;

                case MTKSTP_DATA:

                    /* block copy instead of byte copy */
                    if (stp_core_ctx.parser.length < stp_core_ctx.rx_counter) {
                        STP_ERR_FUNC("Abnormal length in STP_DATA phase 0x%x, 0x%x\n", stp_core_ctx.parser.length, stp_core_ctx.rx_counter);
                        osal_assert(0);
                    }
                    remain_length = stp_core_ctx.parser.length - stp_core_ctx.rx_counter;
                    if (i >= remain_length)
                    {
                        /*boundary checking*/
                        if(stp_core_ctx.rx_counter + remain_length >= MTKSTP_BUFFER_SIZE){
                            STP_ERR_FUNC("Abnormal!! Memory operation over boundary!!\n");
                            stp_change_rx_state(MTKSTP_SYNC);
                            stp_core_ctx.rx_counter = 0;
                            return -1;
                        }

                        osal_memcpy(stp_core_ctx.rx_buf + stp_core_ctx.rx_counter, p_data, remain_length);
                        i -= remain_length;
                        p_data += remain_length;
                        stp_core_ctx.rx_counter = stp_core_ctx.parser.length;
                        stp_core_ctx.parser.state = MTKSTP_CRC1;
                        continue;

                    }
                    else /* only copy by data length*/
                    {
                        /*fixed klocwork insight issue*/
                        /*boundary checking*/
                        if(i + stp_core_ctx.rx_counter >= MTKSTP_BUFFER_SIZE){
                            STP_ERR_FUNC("Abnormal!! Memory operation over boundary 2!!\n");
                            stp_core_ctx.rx_counter = 0;
                            return -1;
                        }

                        osal_memcpy( stp_core_ctx.rx_buf + stp_core_ctx.rx_counter, p_data, i);
                        stp_core_ctx.rx_counter += i; //all remain buffer are data
                        i = 0;
                        p_data += i;
                        continue;
                    }
                    break;

                case MTKSTP_CRC1:
                    stp_change_rx_state(MTKSTP_CRC2);
                    break;

                case MTKSTP_CRC2:

                    /*SDIO mode do it.*/
                    if(mtk_wcn_stp_is_sdio_mode()){
                        /*STP packet 4-bytes alignment*/
                        /*Discard padding bytes , otherwise make parser state machine disorder*/
                        if(i <= 4)
                        {
                            /*STP_DBG_FUNC("STP last block padding %d bytes\n", i-1);*/
                            p_data += (i-1);
                            i -= (i-1);
                        }
                        else
                        {
                            padding_len = (0x04 - ((stp_core_ctx.parser.length + 6) & 0x03))& 0x03;
                            p_data += padding_len;
                            i -= padding_len;
                            /*STP_DBG_FUNC("STP Agg padding %d bytes \n", padding_len);*/
                        }
                    }
                    stp_dbg_pkt_log(stp_core_ctx.parser.type,
                        0,
                        0,
                        0,
                        PKT_DIR_RX,
                        stp_core_ctx.rx_buf,
                        stp_core_ctx.rx_counter);
                    if((stp_core_ctx.parser.type == BT_TASK_INDX) && STP_BT_STK_IS_BLUEZ(stp_core_ctx))
                    {
                        INT32 b;

                        /*Indicate packet to hci_stp*/
                        if(gStpDbgLvl >= STP_LOG_DBG)
                        {
                            stp_dump_data(stp_core_ctx.rx_buf, "indicate_to_bt_core" , stp_core_ctx.rx_counter);
                        }

                        b=mtk_wcn_sys_if_rx(stp_core_ctx.rx_buf, stp_core_ctx.rx_counter);
                        if(b)
                        {
                            STP_ERR_FUNC("mtk_wcn_sys_if_rx is NULL\n");
                        }
                    }
                    else {

                        is_function_active = ((*sys_check_function_status)(stp_core_ctx.parser.type, OP_FUNCTION_ACTIVE) == STATUS_FUNCTION_ACTIVE);

                        /*check type and function if active?*/
                        if((stp_core_ctx.parser.type < MTKSTP_MAX_TASK_NUM) && (is_function_active == MTK_WCN_BOOL_TRUE))
                        {
                            stp_add_to_rx_queue(stp_core_ctx.rx_buf, stp_core_ctx.rx_counter, \
                                stp_core_ctx.parser.type);

                            /*notify corresponding subfunction of incoming data*/
                            (*sys_event_set)(stp_core_ctx.parser.type);
                        }
                        else
                        {
                            if(is_function_active == MTK_WCN_BOOL_FALSE)
                            {
                                STP_ERR_FUNC("function type = %d is inactive, so no en-queue to rx\n", stp_core_ctx.parser.type);
                            }
                            else
                            {
                                STP_ERR_FUNC("mtkstp_process_packet: type = %x, the type is invalid\n", stp_core_ctx.parser.type);
                            }
                        }
                    }

                    //STP_DBG_FUNC("[STP] crc2->sync\n");
                    //STP_DBG_FUNC("[STP] STP Packet End <========= \n");
                    stp_core_ctx.rx_counter = 0;
                    stp_change_rx_state(MTKSTP_SYNC);

                    break;

                case MTKSTP_FW_MSG:

                    /*f/w assert and exception information*/
                    if (stp_core_ctx.parser.length < stp_core_ctx.rx_counter) 
                    {
                        STP_ERR_FUNC("Abnormal length in STP_DATA phase 0x%x, 0x%x\n", stp_core_ctx.parser.length, stp_core_ctx.rx_counter);
                    }

                    remain_length = stp_core_ctx.parser.length - stp_core_ctx.rx_counter;

                    if (i >= remain_length)
                    {
                        osal_memcpy(stp_core_ctx.rx_buf + stp_core_ctx.rx_counter, p_data, remain_length);
                        i -= remain_length;
                        p_data += remain_length;
                        stp_core_ctx.rx_counter = stp_core_ctx.parser.length;
                        *(stp_core_ctx.rx_buf + stp_core_ctx.rx_counter) = '\0';
                        /*Trace32 Dump*/
                        if(stp_core_ctx.parser.type == STP_TASK_INDX){
//                            g_block_tx = 1; 
                            mtk_wcn_stp_coredump_start_ctrl(1);
                            osal_dbg_print("[len=%d][type=%d]\n%s\n",stp_core_ctx.rx_counter, stp_core_ctx.parser.type , stp_core_ctx.rx_buf);
                            stp_dbg_log_pkt(g_mtkstp_dbg, STP_DBG_FW_DMP /*STP_DBG_FW_ASSERT*/,5,0,0,0,0,
                            (stp_core_ctx.rx_counter + 1), 
                            stp_core_ctx.rx_buf);
                        }
                        
                        /*discard CRC*/
                        //we will discard antoher CRC on the outer switch procedure.
                        if(i >= 1){
                            STP_INFO_FUNC("crc discard.. i = %d\n", i);
                            i-=1;
                            if(i > 0){
                                p_data+=1;
                            }    
                        }

                        /*STP packet 4-bytes alignment*/
                        /*Discard padding bytes , otherwise make parser state machine disorder*/
                        if(i <= 4)
                        {
                            STP_INFO_FUNC("\n[STP]FW_EVENT========= block padding %d bytes =========\n", i-1);
                            p_data += (i-1);
                            i -= (i-1);
                        }
                        else
                        {
                            padding_len = (0x04 - ((stp_core_ctx.parser.length+6) & 0x03))& 0x03;
                            p_data += padding_len;
                            i -= padding_len;
                            STP_INFO_FUNC("\n[STP]FW_EVENT========= STP Agg padding %d bytes =========\n", padding_len);
                        }
                        stp_change_rx_state(MTKSTP_SYNC);

                    }
                    else /* only copy by data length*/
                    {
                        STP_ERR_FUNC("raw data doesn't contain full stp packet!!\n");
                    }
                    break;  
                default:
                    break;
            }
            p_data++;
            i--;
        }
    }

    // STP over UART
    else if ( mtk_wcn_stp_is_uart_fullset_mode() && STP_IS_ENABLE(stp_core_ctx))
    {
        while (i > 0)
        {
            switch (stp_core_ctx.parser.state)
            {

                case MTKSTP_RESYNC1:    // RESYNC must be 4 _continuous_ 0x7f
                    if (*p_data == 0x7f)
                    {
                        stp_change_rx_state(MTKSTP_RESYNC2);
                    }
                    else
                    {
                        stp_change_rx_state(MTKSTP_RESYNC1);
                    }
                    break;
                case MTKSTP_RESYNC2:
                    if (*p_data == 0x7f)
                    {
                        stp_change_rx_state(MTKSTP_RESYNC3);
                    }
                    else
                    {
                        stp_change_rx_state(MTKSTP_RESYNC1);
                    }
                    break;
                case MTKSTP_RESYNC3:
                    if (*p_data == 0x7f)
                    {
                        stp_change_rx_state(MTKSTP_RESYNC4);
                    }
                    else
                    {
                        stp_change_rx_state(MTKSTP_RESYNC1);
                    }
                    break;
                case MTKSTP_RESYNC4:
                    if (*p_data == 0x7f)
                    {
                        stp_change_rx_state(MTKSTP_SYNC);
                    }
                    else
                    {
                        stp_change_rx_state(MTKSTP_RESYNC1);
                    }
                    break;
                case MTKSTP_SYNC: // b'10
                    STP_DUMP_PACKET_HEAD(p_data, "rx (uart):", length > 4 ? 4 : length);
                    if (((*p_data & 0x80) == 0x80) &&
                        ((*p_data & 0x40) == 0x00))
                    {
                        stp_change_rx_state(MTKSTP_NAK);
                        stp_core_ctx.parser.seq = (*p_data & 0x38) >> 3;
                        stp_core_ctx.parser.ack = *p_data & 0x07;
                        stp_core_ctx.rx_buf[0] = *p_data;
                        /* Geoge FIXME: WHY comment the following line? */
                        //stp_core_ctx.rx_counter++;

                        if(i >= 4 && gStpDbgLvl >= STP_LOG_DBG){/*print header, when get the full STP header*/
#if !(REMOVE_USELESS_LOG)                            
                            INT32 type = (*(p_data+1) & 0x70) >> 4;
                            PCHAR type_name ="<UNKOWN>";
                            if(type == BT_TASK_INDX)
                                type_name="< BT>";
                            else if(type == GPS_TASK_INDX)
                                type_name="<GPS>";
                            else if(type == WMT_TASK_INDX)
                                type_name="<WMT>";
                            else if(type == FM_TASK_INDX)
                                type_name="< FM>";

                            STP_DBG_FUNC("STP Rx Header: [%02x %02x %02x %02x] type=%s, len=%d, seq=%d, ack=%d\n",
                                        *p_data, *(p_data+1), *(p_data+2), *(p_data+3),
                                        type_name,
                                        ((*(p_data+1) & 0x0f) << 8) + *(p_data+2),
                                        (*p_data & 0x38) >> 3,
                                         *p_data & 0x07 );
#endif                            
                        }
                        else{
                              STP_DBG_FUNC("STP Rx: discard due to i < 4\n");
                        }
                    }
                    else if ((*p_data == 0x7f) && (prev_state == MTKSTP_RESYNC4)) // if this 0x7f is continuous to resync pattern
                    {
                        // skip this continuous 0x7f, remain current & prev state
                        osal_assert(0);
                        STP_ERR_FUNC("MTKSTP_SYNC: continuous resync pattern, buff = %x\n", *p_data);
                    }
                    else if (*p_data == 0x7f)   // a start of 0x7f, maybe this is resync pattern
                    {
                        stp_change_rx_state(MTKSTP_RESYNC2);
                        osal_assert(0);
                        STP_ERR_FUNC("MTKSTP_SYNC: go to MTKSTP_RESYNC2, buff = %x\n", *p_data);
                    }
                    else if (*p_data == 0x55)   // STP delimiter
                    {
                        // do nothing for delimiter
                    }
                    else    // unexpected, go to resync1
                    {
                        osal_assert(0);
                        STP_ERR_FUNC("MTKSTP_SYNC: unexpected data, buff = %x\n", *p_data);
                    }
                    break;

                case MTKSTP_NAK:
                    //(*sys_dbg_print)("MTKSTP_NAK : mtk_wcn_stp_parser_data, buff = %x", *p_data);
                    if(fgEnableNak == 0)
                    {
                        stp_core_ctx.parser.nak = 0;    // disable NAK
                    }
                    else
                    {
                        stp_core_ctx.parser.nak = (*p_data & 0x80) >> 7;
                    }
                    stp_core_ctx.parser.type = (*p_data & 0x70) >> 4;
                    stp_core_ctx.parser.length = (*p_data & 0x0f) << 8;
                    stp_core_ctx.rx_buf[1] = *p_data;
                    /* Geoge FIXME: WHY comment the following line? */
                    /*stp_core_ctx.rx_counter++;*/
                    if(stp_core_ctx.parser.nak)
                    {
                        STP_ERR_FUNC("MTKSTP_NAK TRUE: mtk_wcn_stp_parser_data, buff = %x\n", *p_data);
                    }
                    if(stp_core_ctx.parser.type < MTKSTP_MAX_TASK_NUM)
                    {
                        stp_change_rx_state(MTKSTP_LENGTH);
                    }
                    else
                    {
                        stp_change_rx_state(MTKSTP_SYNC);
                    }
                    break;

                case MTKSTP_LENGTH:
                    //(*sys_dbg_print)("MTKSTP_LENGTH : mtk_wcn_stp_parser_data, buff = %x", *p_data);
                    stp_change_rx_state(MTKSTP_CHECKSUM);
                    stp_core_ctx.parser.length += *p_data;

                    /*Valid length checking*/
                    if(stp_core_ctx.parser.length > 2048)
                    {
                        STP_ERR_FUNC("The length of STP packet is not valid !!! length = %d\n", stp_core_ctx.parser.length);
                        stp_change_rx_state(MTKSTP_RESYNC1);
                        stp_core_ctx.rx_counter = 0;
                        STP_TRACE_FUNC("--\n");
                        return -1;
                    }

                    stp_core_ctx.rx_buf[2] = *p_data;
                    /* Geoge FIXME: WHY comment the following line? */
                    /*stp_core_ctx.rx_counter++;*/
                    break;

                case MTKSTP_CHECKSUM:
                    //(*sys_dbg_print)("MTKSTP_CHECKSUM : mtk_wcn_stp_parser_data, buff = %x", *p_data);
                     if ((stp_core_ctx.parser.type == STP_TASK_INDX)||
                            (stp_core_ctx.parser.type == INFO_TASK_INDX))
                     {
                        stp_change_rx_state(MTKSTP_FW_MSG);
                        stp_core_ctx.rx_counter = 0;
                        i -= 1;
                        if(i != 0)
                        {
                            p_data += 1;
                        }
                        continue;
                     }

                    if (((stp_core_ctx.rx_buf[0] +
                          stp_core_ctx.rx_buf[1] +
                          stp_core_ctx.rx_buf[2]) & 0xff) == *p_data)
                    {
                        // header only packet
                        if (stp_core_ctx.parser.length == 0)
                        {
                            INT32 fgTriggerResume = (-1);

                            stp_ctx_lock(&stp_core_ctx);
                            if(stp_core_ctx.inband_rst_set == 0)
                            {
                                stp_dbg_pkt_log(STP_TASK_INDX,
                                    stp_core_ctx.parser.ack,
                                    stp_core_ctx.parser.seq,
                                    5,//STP type id
                                    PKT_DIR_RX,
                                    NULL,
                                    0);
                                fgTriggerResume = stp_process_rxack();
                                if(fgTriggerResume == 0)
                                {
                                    (*sys_event_tx_resume)(stp_core_ctx.sequence.winspace); // notify adaptation layer for possible tx resume mechanism
                                }
                            } else {
                                STP_WARN_FUNC("Now it's inband reset process and drop ACK packet.\n");
                            }
                            stp_ctx_unlock(&stp_core_ctx);
                            stp_change_rx_state(MTKSTP_SYNC);
                            stp_core_ctx.rx_counter = 0;
                        }
                        else
                        {
                            stp_change_rx_state(MTKSTP_DATA);
                            stp_core_ctx.rx_counter = 0;
                        }
                    }
                    else
                    {
                        STP_ERR_FUNC("The checksum of header is error !!! %02x %02x %02x %02x\n", stp_core_ctx.rx_buf[0], stp_core_ctx.rx_buf[1], stp_core_ctx.rx_buf[2], *p_data);
                        /* George FIXME: error handling mechanism shall be refined */
                        stp_change_rx_state(MTKSTP_RESYNC1);
                        stp_core_ctx.rx_counter = 0;

                        /* since checksum error is usually related to interface
                        * buffer overflow, so we just let timeout mechanism to
                        * handle such error.
                        */
                         STP_TRACE_FUNC("--\n");
                        /* return and purge COMM port */
                        return -1;
                        /*stp_send_ack(1); NAK mechanism is removed */
                    }
                    break;

                case MTKSTP_DATA:
#if 0
                    if (stp_core_ctx.rx_counter < stp_core_ctx.parser.length)
                    {
                        stp_core_ctx.rx_buf[stp_core_ctx.rx_counter] = *p_data;
                        stp_core_ctx.rx_counter++;
                    }
                    if (stp_core_ctx.rx_counter == stp_core_ctx.parser.length)
                    {
                        stp_change_rx_state(MTKSTP_CRC1);
                    }
#else
                    /* block copy instead of byte copy */
                    if (stp_core_ctx.parser.length < stp_core_ctx.rx_counter)
                    {
                        STP_ERR_FUNC("Abnormal length in STP_DATA phase 0x%x, 0x%x\n", stp_core_ctx.parser.length, stp_core_ctx.rx_counter);
                        osal_assert(0);
                    }
                    remain_length = stp_core_ctx.parser.length - stp_core_ctx.rx_counter;
                    if (i >= remain_length)
                    {
                        osal_memcpy(stp_core_ctx.rx_buf + stp_core_ctx.rx_counter, p_data, remain_length);

                        i -= remain_length;
                        p_data += remain_length;
                        stp_core_ctx.rx_counter = stp_core_ctx.parser.length;
                        stp_core_ctx.parser.state = MTKSTP_CRC1;
                        continue;
                    }
                    else /* only copy by data length*/
                    {
                        /*fixed klocwork insight issue*/
                        if(i + stp_core_ctx.rx_counter >= MTKSTP_BUFFER_SIZE)
                        {
                            STP_ERR_FUNC("Fail to handle Packet, maybe it doesn't follow STP protocol.\n");
                            stp_change_rx_state(MTKSTP_RESYNC1);
                            stp_core_ctx.rx_counter = 0;
                            STP_TRACE_FUNC("--\n");
                            return -1;
                        }

                        osal_memcpy( stp_core_ctx.rx_buf + stp_core_ctx.rx_counter, p_data, i);
                        stp_core_ctx.rx_counter += i; //all remain buffer are data
                        i = 0;
                        p_data += i;
                        continue;
                    }
#endif
                    break;

                case MTKSTP_CRC1:
                    stp_change_rx_state(MTKSTP_CRC2);
                    stp_core_ctx.parser.crc = *p_data;
                    break;
                case MTKSTP_CRC2:
                    stp_change_rx_state(MTKSTP_SYNC);
                    stp_core_ctx.parser.crc += (*p_data) << 8;
                    if (stp_check_crc(stp_core_ctx.rx_buf, stp_core_ctx.rx_counter, stp_core_ctx.parser.crc) == MTK_WCN_BOOL_TRUE)
                    {
                        if(stp_core_ctx.inband_rst_set == 0)
                        {
                            stp_process_packet();
                        } else {
                            STP_WARN_FUNC("Now it's inband reset process and drop packet.\n");
                        }
                    }
                    else
                    {
                        STP_ERR_FUNC("The CRC of packet is error !!!\n");
                        /* George FIXME: error handling mechanism shall be refined */
                        stp_change_rx_state(MTKSTP_RESYNC1);
                        stp_core_ctx.rx_counter = 0;

                        /* since checksum error is usually related to interface
                        * buffer overflow, so we just let timeout mechanism to
                        * handle such error.
                       */
                         STP_TRACE_FUNC("--\n");
                        /* return and purge COMM port */
                        return -1;
                        /*stp_send_ack(1); NAK mechanism is removed */
                    }
                    break;

                case MTKSTP_FW_MSG:
                    if (STP_IS_READY(stp_core_ctx)) {
						#if WMT_PLAT_ALPS
                	    stop_log();
						#endif
                        mtk_wcn_stp_dbg_dump_package();
                	    stp_notify_btm_dump(STP_BTM_CORE(stp_core_ctx));
                	}
                	STP_SET_READY(stp_core_ctx, 0);
                    /*stp inband reset*/
                    if (stp_core_ctx.parser.type == STP_TASK_INDX &&
                            stp_core_ctx.parser.seq == 0 &&
                            stp_core_ctx.parser.ack == 0 &&
                            stp_core_ctx.parser.length == 0 &&
                            stp_core_ctx.inband_rst_set == 1)
                        {
                            STP_INFO_FUNC("Inband reset event get! Resync STP with firmware!\n\r");
                            stp_rest_ctx_state();
                            stp_change_rx_state(MTKSTP_RESYNC1);
                            stp_core_ctx.inband_rst_set = 0;
                            //STP_INFO_FUNC("Restart STP Timer\n\r");
                            //(*sys_timer_start)(stp_core_ctx.tx_timer, mtkstp_tx_timeout, (MTK_WCN_TIMER_CB)stp_tx_timeout_handler, NULL);
                            STP_TRACE_FUNC("--\n");
                            return 0;
                        }

                    /*f/w assert and exception information*/
                    if (stp_core_ctx.parser.length < stp_core_ctx.rx_counter) {
                        STP_ERR_FUNC("Abnormal length in STP_DATA phase 0x%x, 0x%x\n", stp_core_ctx.parser.length, stp_core_ctx.rx_counter);
                        osal_assert(0);
                    }

                    remain_length = stp_core_ctx.parser.length - stp_core_ctx.rx_counter;
                    if (i >= remain_length)
                    {
                        osal_memcpy(stp_core_ctx.rx_buf + stp_core_ctx.rx_counter, p_data, remain_length);
                        i -= remain_length;
                        p_data += remain_length;
                        stp_core_ctx.rx_counter = stp_core_ctx.parser.length;
                        stp_change_rx_state(MTKSTP_SYNC);
                        *(stp_core_ctx.rx_buf + stp_core_ctx.rx_counter) = '\0';
                        //STP_ERR_FUNC("%s [%d]\n", stp_core_ctx.rx_buf, stp_core_ctx.rx_counter);
                        #if 0
                        if((stp_core_ctx.rx_counter == 1) && (stp_core_ctx.rx_buf[0] == 0xFF))
                        {
                            //For MT6620, enable/disable coredump function is controlled by firmware for the moment, we need to set coredump enable flag to be 1 after 
                            //see firmware send a pariticallar charactor(0xff) before any coredump packet is sent
                            mtk_wcn_stp_coredump_flag_ctrl(1);
                        }
						#endif
                        /*Trace32 Dump*/
                        if(STP_IS_ENABLE_DBG(stp_core_ctx) && (stp_core_ctx.parser.type == STP_TASK_INDX) && (0 != mtk_wcn_stp_coredump_flag_get())){
                            if (0 != stp_core_ctx.rx_counter)
                            {
                                STP_SET_READY(stp_core_ctx, 0);
                                mtk_wcn_stp_coredump_start_ctrl(1);
                                stp_psm_set_sleep_disable(stp_core_ctx.psm);
                                stp_dbg_log_pkt(g_mtkstp_dbg, STP_DBG_FW_DMP/*STP_DBG_FW_ASSERT*/,STP_TASK_INDX,0,0,0,0,
                                    (stp_core_ctx.rx_counter + 1),
                                    stp_core_ctx.rx_buf);
                            }
                            osal_dbg_print("[len=%d][type=%d]\n%s\n",stp_core_ctx.rx_counter, stp_core_ctx.parser.type , stp_core_ctx.rx_buf);
                            if (0 == osal_strncmp("coredump end", stp_core_ctx.rx_buf + stp_core_ctx.rx_counter - osal_strlen("coredump end") - 2, osal_strlen("coredump end")))
                            {
                                STP_ERR_FUNC("coredump end\n");
                                mtk_wcn_stp_coredump_start_ctrl(0);
                                stp_psm_set_sleep_enable(stp_core_ctx.psm);
                                stp_btm_reset_btm_wq(STP_BTM_CORE(stp_core_ctx));
                                if(STP_IS_ENABLE_RST(stp_core_ctx)){
                                    stp_btm_notify_wmt_rst_wq(STP_BTM_CORE(stp_core_ctx));
                                }else{
                                    STP_INFO_FUNC("No to launch whole chip reset! for debugging purpose\n");
                                }
                            }
                        }
                        /*Runtime FW Log*/
                        else if(STP_IS_ENABLE_DBG(stp_core_ctx)&& (stp_core_ctx.parser.type == INFO_TASK_INDX)){
                           stp_dbg_log_pkt(g_mtkstp_dbg, STP_DBG_FW_LOG,STP_TASK_INDX,5,0,0,0,
                                (stp_core_ctx.rx_counter + 1),
                                stp_core_ctx.rx_buf);
                            mtk_wcn_stp_dbg_dump_package();
                        }
                        /*Normal mode: whole chip reset*/
                        else
                        {
                            /*Aee Kernel Warning Message Shown First*/
                            //(*sys_dbg_assert_aee)("[MT662x]f/w Assert", stp_core_ctx.rx_buf);
                            mtk_wcn_stp_coredump_start_ctrl(0);
                            mtk_wcn_stp_dbg_dump_package();
                            
                            if (0 == mtk_wcn_stp_coredump_flag_get()) {
                                STP_ERR_FUNC("fw error happend but coredump disabled\n");
                            } else {
                            osal_dbg_assert_aee(stp_core_ctx.rx_buf, stp_core_ctx.rx_buf);
                            }
                            
                            /*Whole Chip Reset Procedure Invoke*/
                            if(STP_IS_ENABLE_RST(stp_core_ctx)){
                                STP_SET_READY(stp_core_ctx, 0);
                                stp_btm_notify_wmt_rst_wq(STP_BTM_CORE(stp_core_ctx));
                            }else{
                                STP_INFO_FUNC("No to launch whole chip reset! for debugging purpose\n");
                            }
                        }

                        /*discard CRC*/
                        if(i >= 2){
                            STP_DBG_FUNC("crc discard.. i = %d\n", i);
                            i-=2;
                            if(i > 0){
                                p_data+=2;
                            }
                        }
                        continue;
                    }
                    else /* only copy by data length*/
                    {
                        /*fixed klocwork insight issue*/
                        if(i + stp_core_ctx.rx_counter >= MTKSTP_BUFFER_SIZE)
                        {
                            STP_ERR_FUNC("Fail to handle Packet, maybe it doesn't follow STP protocol.\n");
                            stp_change_rx_state(MTKSTP_RESYNC1);
                            stp_core_ctx.rx_counter = 0;
                            return -1;
                        }
                        osal_memcpy( stp_core_ctx.rx_buf + stp_core_ctx.rx_counter, p_data, i);
                        stp_core_ctx.rx_counter += i; //all remain buffer are data
                        i = 0;
                        p_data += i;
                        continue;
                    }

                    break;
                default:
                    break;
            }
            p_data++;
            i--;
        }
    }
    /* George FIXME: WHY or HOW can we reduct the locked region? */
    /*(*sys_mutex_unlock)(stp_core_ctx.stp_mutex, flags);*/
    STP_TRACE_FUNC("--\n");
    return 0;
}

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_enable
* DESCRIPTION
*  enable/disable STP
* PARAMETERS
*  value        [IN]        0=disable, others=enable
* RETURNS
*  INT32    0=success, others=error
*****************************************************************************/
INT32 mtk_wcn_stp_enable(INT32 value)
{
    STP_INFO_FUNC("%s: set the current enable = (%d)\n", __func__, value);

    stp_rest_ctx_state();
    STP_SET_ENABLE(stp_core_ctx, value);
    if (!value)
    {
        mtk_wcn_stp_psm_reset();
    }
    else
    {
//        g_block_tx = 0;
        mtk_wcn_stp_coredump_start_ctrl(0);
    }
    return 0;
}


INT32 mtk_wcn_stp_dbg_dump_package(VOID){
    if(STP_NOT_ENABLE(stp_core_ctx)){
        STP_INFO_FUNC("STP dbg mode is off\n");

    } else {
        STP_INFO_FUNC("STP dbg mode is on\n");
        //if (0 == g_block_tx)
        if (0 == mtk_wcn_stp_coredump_start_get())
        {
            stp_dbg_dmp_printk(g_mtkstp_dbg);
        }
        else
        {
            STP_INFO_FUNC("assert start flag is set, disable packet dump function\n");
        }
    }
    return 0;
}


/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_ready
* DESCRIPTION
*  ready/un-ready STP
* PARAMETERS
*  value        [IN]        0=un-ready, others=ready
* RETURNS
*  INT32    0=success, others=error
*****************************************************************************/
INT32 mtk_wcn_stp_ready(INT32 value)
{
    STP_INFO_FUNC("set ready (%d)\n", value);

    STP_SET_READY(stp_core_ctx, value);
    /*if whole chip reset, reset the debuggine mode*/
#ifndef CONFIG_LOG_STP_INTERNAL
    //mtk_wcn_stp_dbg_disable();    
#endif

    if(stp_is_apply_powersaving())
    {
            STP_INFO_FUNC("Restart the stp-psm monitor !!\n");
            stp_psm_disable(STP_PSM_CORE(stp_core_ctx));
    }

    return 0;
}


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
INT32 mtk_wcn_stp_coredump_start_ctrl(UINT32 value)
{
    STP_INFO_FUNC("set f/w assert (%d)\n", value);

    STP_SET_FW_COREDUMP_FLAG(stp_core_ctx, value);

    return 0;
}

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
INT32 mtk_wcn_stp_coredump_start_get(VOID)
{
    return STP_FW_COREDUMP_FLAG(stp_core_ctx);
}


/* mtk_wcn_stp_set_wmt_last_close -- set the state of link(UART or SDIO)
 * @ value - 1, link already be closed; 0, link is open
 * 
 * Return 0 if success; else error code 
 */
INT32 mtk_wcn_stp_set_wmt_last_close(UINT32 value)
{
    STP_INFO_FUNC("set wmt_last_close flag (%d)\n", value);

    STP_SET_WMT_LAST_CLOSE(stp_core_ctx, value);

    return 0;
}


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
*  INT32    > 0: length transmitted; = 0: error
*****************************************************************************/
INT32 mtk_wcn_stp_send_data(const UINT8 *buffer, const UINT32 length, const UINT8 type)
{
    UINT8 mtkstp_header[MTKSTP_HEADER_SIZE], temp[2];
    UINT8 *p_tx_buf = NULL;
    UINT16 crc;
    INT32 ret = 0;

    //osal_buffer_dump(buffer,"tx", length, 32);

    if (0 != STP_WMT_LAST_CLOSE(stp_core_ctx)) {
        STP_ERR_FUNC("WMT lats close,shoud not have tx request!\n");
        return length;
    }
	
    //if(g_block_tx)
    if (0 != mtk_wcn_stp_coredump_start_get())
    {
        STP_ERR_FUNC("STP fw coredump start flag set...\n");
        return length;
    }

#ifdef CONFIG_POWER_SAVING_SUPPORT
    if (MTK_WCN_BOOL_TRUE == stp_psm_is_quick_ps_support())
    {
	    if(type != WMT_TASK_INDX)
	    {
	        stp_psm_disable_by_tx_rx_density(STP_PSM_CORE(stp_core_ctx), 0);
	    }

	    //if(stp_is_apply_powersaving()) 
	    {
	        if(type == WMT_TASK_INDX){
	            goto DONT_MONITOR;
	        }
	        /*-----------------------------STP_PSM_Lock----------------------------------------*/
	        ret = stp_psm_thread_lock_aquire(STP_PSM_CORE(stp_core_ctx));
	        if (ret) {
	            STP_ERR_FUNC("--->lock psm_thread_lock failed ret=%d\n", ret);
	            return ret;
	        }
	        
	        if(!stp_psm_is_to_block_traffic(STP_PSM_CORE(stp_core_ctx))){
	            if(stp_psm_has_pending_data(STP_PSM_CORE(stp_core_ctx))){
	                STP_WARN_FUNC("***** Release psm hold data before send normal data *****\n");
	                stp_psm_release_data(STP_PSM_CORE(stp_core_ctx));   
	            }
	        } else {
	            ret = stp_psm_hold_data(STP_PSM_CORE(stp_core_ctx), buffer, length, type);
	            stp_psm_notify_wmt_wakeup(STP_PSM_CORE(stp_core_ctx));
				/*-----------------------------STP_PSM_UnLock----------------------------------------*/
				stp_psm_thread_lock_release(STP_PSM_CORE(stp_core_ctx));
	            return ret;
	        }
	    }
    }
	else
	{
	     //if(stp_is_apply_powersaving())
	     {
	        if(stp_is_privileges_cmd(buffer, length , type))
	        {
	            STP_DBG_FUNC("send privileges cmd\n");
	            goto DONT_MONITOR;
	        }
	        //If now chip is awake, to restart monitor!
	        //STP_INFO_FUNC("check if block traffic !!\n");
	        /*-----------------------------STP_PSM_Lock----------------------------------------*/
	        ret = stp_psm_thread_lock_aquire(STP_PSM_CORE(stp_core_ctx));
	        if (ret) {
	            STP_ERR_FUNC("--->lock psm_thread_lock failed ret=%d\n", ret);
	            return ret;
	        }
	        
	        if(!stp_psm_is_to_block_traffic(STP_PSM_CORE(stp_core_ctx)))
	        {
	            //STP_INFO_FUNC("not to block !!\n");
	            if(stp_psm_has_pending_data(STP_PSM_CORE(stp_core_ctx)))
	            {
	                STP_WARN_FUNC("***** Release psm hold data before send normal data *****\n");
	                stp_psm_release_data(STP_PSM_CORE(stp_core_ctx));
	            }
	            stp_psm_start_monitor(STP_PSM_CORE(stp_core_ctx));
	        }
	        else
	        {
	            //STP_INFO_FUNC("to block !!\n");

	            //STP_INFO_FUNC("****************hold data in psm queue data length = %d\n", length);
	            //stp_dump_data(buffer, "Hold in psm queue", length);
	            //hold datas
	            ret = stp_psm_hold_data(STP_PSM_CORE(stp_core_ctx), buffer, length, type);
	            //wmt notification
	            STP_INFO_FUNC("#####Type = %d, to inform WMT to wakeup chip, ret = %d\n", type, ret);
	            stp_psm_notify_wmt_wakeup(STP_PSM_CORE(stp_core_ctx));
	            //STP_INFO_FUNC("*********Type = %d, to inform WMT to wakeup chip>end\n", type);
	            /*-----------------------------STP_PSM_UnLock----------------------------------------*/
				stp_psm_thread_lock_release(STP_PSM_CORE(stp_core_ctx));
	            return ret;
	        }
	    }
    }
	DONT_MONITOR:
#endif
    if(type == BT_TASK_INDX)    
    {        
        const static UINT8 rst_buf[4] = {0x01, 0x03, 0x0c, 0x00};        
        if(!osal_strncmp(buffer, rst_buf, 4))        
        {            
            osal_printtimeofday("############ BT Rest start -->");        
         }    
    }

    stp_ctx_lock(&stp_core_ctx);
    /*Only WMT can set raw data */
    if(STP_NOT_ENABLE(stp_core_ctx) && WMT_TASK_INDX != type)
    {
        //no-op
    }
    else if (STP_NOT_ENABLE(stp_core_ctx) && WMT_TASK_INDX == type)
    {
        //ret = mtk_wcn_stp_send_data_raw(buffer, length, type);
    }

    // STP over SDIO
    else if ((mtk_wcn_stp_is_sdio_mode() || mtk_wcn_stp_is_uart_mand_mode()) && STP_IS_ENABLE(stp_core_ctx))
    {

        //osal_printtimeofday("[ STP][SDIO][ B][W]");

        mtkstp_header[0] = 0x80;
        mtkstp_header[1] = (type << 4) + (((length)>>8) & 0x0f);
        mtkstp_header[2] = (length) & 0xff;
        mtkstp_header[3] = 0x00;

        //HEADER
        p_tx_buf = &stp_core_ctx.tx_buf[0];
        osal_memcpy(p_tx_buf, mtkstp_header, MTKSTP_HEADER_SIZE);
        p_tx_buf += MTKSTP_HEADER_SIZE;

        //PAYLOAD
        osal_memcpy(p_tx_buf, buffer, length);
        p_tx_buf += length;

        //CRC
        temp[0] = 0x00;
        temp[1] = 0x00;
        osal_memcpy(p_tx_buf, temp, 2);
        stp_dbg_pkt_log(type,
                0,
                0,
                0,
                PKT_DIR_TX,
                buffer,
                length);
        (*sys_if_tx)(&stp_core_ctx.tx_buf[0], (MTKSTP_HEADER_SIZE+length+2), &ret);

        if ( (MTKSTP_HEADER_SIZE+length+2) != ret )
        {
            STP_ERR_FUNC("stp send tx packet: %d, maybe stp_if_tx == NULL\n", ret);
            osal_assert(0);
            ret = 0;
        }
        else
        {
            ret = (INT32)length;
        }

        //osal_printtimeofday("[ STP][SDIO][ E][W]");
    }

    // STP over UART
    else if ( mtk_wcn_stp_is_uart_fullset_mode() && STP_IS_ENABLE(stp_core_ctx))
    {

        //osal_printtimeofday("[ STP][UART][ B][W]");
        //STP_INFO_FUNC("Write byte %d\n", length);
        
        if ((stp_core_ctx.sequence.winspace > 0) &&
                (stp_core_ctx.inband_rst_set == 0) &&
                    (stp_is_tx_res_available(MTKSTP_HEADER_SIZE + length + MTKSTP_CRC_SIZE)))
        {
            /*Make Header*/
            //(*sys_dbg_print)("mtk_wcn_stp_send_data 1, txseq = %d, winspace = %d", stp_core_ctx.sequence.txseq, stp_core_ctx.sequence.winspace);
            mtkstp_header[0] = 0x80 + (stp_core_ctx.sequence.txseq << 3) + stp_core_ctx.sequence.txack;
            mtkstp_header[1] = (type << 4) + ((length & 0xf00) >> 8);
            mtkstp_header[2] = length & 0xff;
            mtkstp_header[3] = (mtkstp_header[0] + mtkstp_header[1] + mtkstp_header[2]) & 0xff;
            stp_core_ctx.tx_start_addr[stp_core_ctx.sequence.txseq] = stp_core_ctx.tx_write;
            stp_core_ctx.tx_length[stp_core_ctx.sequence.txseq] = MTKSTP_HEADER_SIZE + length + 2;
            if (fgEnableDelimiter == 1)
            {
                stp_core_ctx.tx_length[stp_core_ctx.sequence.txseq] += STP_DEL_SIZE;
                stp_add_to_tx_queue(&stp_delimiter[0], STP_DEL_SIZE);
            }
            stp_add_to_tx_queue(mtkstp_header, MTKSTP_HEADER_SIZE);

            /*Make Payload*/
            stp_add_to_tx_queue(buffer, length);

            /*Make CRC*/
            crc = osal_crc16(buffer, length);
            temp[0] = crc & 0xff;
            temp[1] = (crc & 0xff00) >> 8;
            stp_add_to_tx_queue(temp, 2);
            stp_dbg_pkt_log(type,
                stp_core_ctx.sequence.txack,
                stp_core_ctx.sequence.txseq,
                crc,
                PKT_DIR_TX,
                buffer,
                length);

            /*Kick to UART*/
            stp_send_tx_queue(stp_core_ctx.sequence.txseq);

            INDEX_INC(stp_core_ctx.sequence.txseq);
            stp_core_ctx.sequence.winspace--;

            /*Setup the Retry Timer*/
            osal_timer_stop(&stp_core_ctx.tx_timer);
            if (stp_core_ctx.sequence.winspace != MTKSTP_WINSIZE)
            {
                osal_timer_start(&stp_core_ctx.tx_timer, mtkstp_tx_timeout);
            }
            else
            {
                STP_ERR_FUNC("mtk_wcn_stp_send_data: wmt_stop_timer\n");
            }
            ret = (INT32)length;
        }
        else
        {
            /*
                No winspace to send. Let caller retry
            */
            if(stp_core_ctx.inband_rst_set == 1)
            {
                STP_WARN_FUNC("Now it's inband reset process and drop sent packet.\n");
            }
            else
            {
                STP_ERR_FUNC("%s: There is no winspace/txqueue to send !!!\n", __FUNCTION__);
            }
            ret = 0;
        }

        //osal_printtimeofday("[ STP][UART][ E][W]");
    }
    stp_ctx_unlock(&stp_core_ctx);
	
#ifdef CONFIG_POWER_SAVING_SUPPORT

    if (MTK_WCN_BOOL_TRUE == stp_psm_is_quick_ps_support())
    {
        if(type != WMT_TASK_INDX) 
        {
            stp_psm_notify_wmt_sleep(STP_PSM_CORE(stp_core_ctx));
			/*-----------------------------STP_PSM_UnLock----------------------------------------*/
			stp_psm_thread_lock_release(STP_PSM_CORE(stp_core_ctx));
        }    
    }
    else
    {
	    //if(stp_is_apply_powersaving()) 
	   {
		    if((MTK_WCN_BOOL_FALSE == stp_is_privileges_cmd(buffer, length , type))) 
		    {
			
		    	/*-----------------------------STP_PSM_UnLock----------------------------------------*/
		    	stp_psm_thread_lock_release(STP_PSM_CORE(stp_core_ctx));
	    	}	 
    	}
    }
 #endif
 
    return ret;
}

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
*  INT32    >= 0: length transmitted; < 0: error
*****************************************************************************/
INT32 mtk_wcn_stp_send_data_raw (const UINT8 *buffer, const UINT32 length, const UINT8 type)
{
    UINT32 written = 0;
    INT32 ret = 0;

    if (0 != STP_WMT_LAST_CLOSE(stp_core_ctx)) {
        STP_ERR_FUNC("WMT lats close,shoud not have tx request!");
        return length;
    }
	
    STP_DBG_FUNC("mtk_wcn_stp_send_data_raw, type = %d, data = %x %x %x %x %x %x ", type, buffer[0], buffer[1], buffer[2], buffer[3], buffer[4], buffer[5]);
    STP_SET_PENDING_TYPE(stp_core_ctx, type); // remember tx type, forward following rx to this type

    stp_ctx_lock(&stp_core_ctx);
    stp_dbg_pkt_log(
        type,
        0,
        0,
        0,
        PKT_DIR_TX,
        buffer,
        1);
    (*sys_if_tx)(&buffer[0], length, &written);
    stp_ctx_unlock(&stp_core_ctx);

    if (written == 0)
    {
        stp_dump_data(&buffer[0], "tx raw failed:", length);
    }

    if(written == length)
    {
        ret = (INT32)written;
    }
    else
    {
        ret = (-1);
    }

    return ret;
}

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_receive_data
* DESCRIPTION
*  receive data from serial protocol engine
* PARAMETERS
*  buffer      [IN]        data buffer
*  length      [IN]        data buffer length
*  type        [IN]        subfunction type
* RETURNS
*  INT32    >= 0: size of data received; < 0: error
*****************************************************************************/
INT32 mtk_wcn_stp_receive_data(UINT8 *buffer, UINT32 length, UINT8 type)
{
    /* GeorgeKuo modify: reduce "if" branch */
    UINT16 copyLen = 0;
    UINT16 tailLen = 0;
    if ((MTK_WCN_BOOL_TRUE == stp_psm_is_quick_ps_support()) && (type != WMT_TASK_INDX))
    {
        stp_psm_disable_by_tx_rx_density(STP_PSM_CORE(stp_core_ctx), 1);
    }

    stp_ctx_lock(&stp_core_ctx);
    while (stp_core_ctx.ring[type].read_p != stp_core_ctx.ring[type].write_p)
    {
        /* GeorgeKuo modify: reduce if branch */
        if ( stp_core_ctx.ring[type].write_p > stp_core_ctx.ring[type].read_p)
        {
            copyLen = stp_core_ctx.ring[type].write_p - stp_core_ctx.ring[type].read_p;
            if (copyLen > length)
            {
                copyLen = length;
            }
            osal_memcpy(buffer, stp_core_ctx.ring[type].buffer + stp_core_ctx.ring[type].read_p, copyLen);
            stp_core_ctx.ring[type].read_p += copyLen;
            break;
        }
        else
        {
            tailLen = MTKSTP_BUFFER_SIZE - stp_core_ctx.ring[type].read_p;
            if (tailLen > length)
            { // exclude equal case to skip wrap check
                copyLen = length;
                osal_memcpy(buffer, stp_core_ctx.ring[type].buffer + stp_core_ctx.ring[type].read_p, copyLen);
                stp_core_ctx.ring[type].read_p += copyLen;
            }
            else
            {
                // part 1: copy tailLen
                osal_memcpy(buffer, stp_core_ctx.ring[type].buffer + stp_core_ctx.ring[type].read_p, tailLen);

                buffer += tailLen; //update buffer offset

                // part 2: check if head length is enough
                copyLen = length - tailLen;
                copyLen = (stp_core_ctx.ring[type].write_p < copyLen) ? stp_core_ctx.ring[type].write_p : copyLen;

                if (copyLen)
                {
                    osal_memcpy(buffer, stp_core_ctx.ring[type].buffer + 0, copyLen);
                }

                // Update read_p final position
                stp_core_ctx.ring[type].read_p = copyLen;

                // update return length: head + tail
                copyLen += tailLen;
            }
            break;
        }
    }

    stp_ctx_unlock(&stp_core_ctx);

    return copyLen;
}

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
INT32 mtk_wcn_stp_is_rxqueue_empty(UINT8 type)
{
    INT32 ret;

    stp_ctx_lock(&stp_core_ctx);

    if(stp_core_ctx.ring[type].read_p == stp_core_ctx.ring[type].write_p)
    {
        ret = 1;    // queue is empty
    }
    else
    {
        ret = 0;    // queue is not empty
    }

    stp_ctx_unlock(&stp_core_ctx);

    return ret;
}

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

void mtk_wcn_stp_set_mode(UINT32 mode)
{
    STP_SET_SUPPORT_PROTOCOL(stp_core_ctx, mode);
    
    STP_INFO_FUNC("STP_SUPPORT_PROTOCOL = %08x \n", STP_SUPPORT_PROTOCOL(stp_core_ctx));
}

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_is_uart_fullset_mode
* DESCRIPTION
*  Is stp use UART fullset mode?
* PARAMETERS
*  none.
* RETURNS
*  MTK_WCN_BOOL    TRUE:Uart Fullset mode, FALSE:Not UART Fullset mode
*****************************************************************************/
MTK_WCN_BOOL mtk_wcn_stp_is_uart_fullset_mode(void)
{
     /*
        bit 0: uart fullset   mode
        bit 1: uart mandatory mode
        bit 2: sdio mode
    */
    if(STP_SUPPORT_PROTOCOL(stp_core_ctx)& MTKSTP_UART_FULL_MODE)
        return MTK_WCN_BOOL_TRUE;
    else
        return MTK_WCN_BOOL_FALSE;
}

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_is_uart_mand_mode
* DESCRIPTION
*  Is stp use UART mandatory mode?
* PARAMETERS
*  none.
* RETURNS
*  MTK_WCN_BOOL    TRUE:Uart Mandatory mode, FALSE:Not UART Mandotary mode
*****************************************************************************/
MTK_WCN_BOOL mtk_wcn_stp_is_uart_mand_mode(void)
{
    /*
        bit 0: uart fullset   mode
        bit 1: uart mandatory mode
        bit 2: sdio mode
    */
    if(STP_SUPPORT_PROTOCOL(stp_core_ctx)& MTKSTP_UART_MAND_MODE)
        return MTK_WCN_BOOL_TRUE;
    else
        return MTK_WCN_BOOL_FALSE;
}

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
MTK_WCN_BOOL mtk_wcn_stp_is_sdio_mode(void)
{
    /*
        bit 0: uart fullset   mode
        bit 1: uart mandatory mode
        bit 2: sdio mode
    */
    if(STP_SUPPORT_PROTOCOL(stp_core_ctx)& MTKSTP_SDIO_MODE)
        return MTK_WCN_BOOL_TRUE;
    else
        return MTK_WCN_BOOL_FALSE;
}

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
void mtk_wcn_stp_inband_reset(void)
{
    UINT8    inband_reset_packet[64];
    UINT32   txseq = 0;
    UINT32   txack = 0;
    UINT32   crc =0;
    UINT32   ret = 0;
    UINT32   reset_payload_len=0;

    /*512 bytes*/
    UINT8  reset_payload[]= {
        0xc0, 0x01, 0xc0, 0xde, 0x3e, 0xd1, 0xa7, 0xef
    };

    stp_ctx_lock(&stp_core_ctx);

    /*RESYNC*/
    inband_reset_packet[0]=0x7f;
    inband_reset_packet[1]=0x7f;
    inband_reset_packet[2]=0x7f;
    inband_reset_packet[3]=0x7f;
    inband_reset_packet[4]=0x7f;
    inband_reset_packet[5]=0x7f;
    inband_reset_packet[6]=0x7f;
    inband_reset_packet[7]=0x7f;

    /*header*/
    reset_payload_len = sizeof(reset_payload)/sizeof(reset_payload[0]);
    inband_reset_packet[8] = 0x80 + (txseq << 3) + txack;
    inband_reset_packet[9] = (STP_TASK_INDX << 4) + ((reset_payload_len & 0xf00) >> 8);
    inband_reset_packet[10] = reset_payload_len & 0xff;
    inband_reset_packet[11] = (inband_reset_packet[8] + inband_reset_packet[9] + inband_reset_packet[10]) & 0xff;

    /*payload*/
    osal_memcpy(&inband_reset_packet[12], reset_payload, reset_payload_len);

    /*crc*/
    crc = osal_crc16(&reset_payload[0], reset_payload_len);
    inband_reset_packet[12 + reset_payload_len] = crc & 0xff;
    inband_reset_packet[12 + reset_payload_len + 1] = (crc & 0xff00) >> 8;

    (*sys_if_tx)(&inband_reset_packet[0], 14 + reset_payload_len, &ret);

    if (ret != (14 + reset_payload_len))
    {
         STP_ERR_FUNC("Inband sending error, sending %d , but ret = %d\n", 10 + reset_payload_len, ret);
    }

    stp_core_ctx.inband_rst_set = 1;
    stp_ctx_unlock(&stp_core_ctx);
}

void mtk_wcn_stp_debug_ctrl(INT32 op, INT32 filter, INT32 filter_param)
{
    return ;
}

void mtk_wcn_stp_test_cmd(INT32 cmd_no)
{
    UINT8    test_packet[64];
    UINT32   txseq = 0;
    UINT32   txack = 0;
    UINT32   crc =0;
    UINT32   ret = 0;
    UINT32   reset_payload_len=0;

    UINT8  test_payload[]= {
        0xAA, 0xAA, 0xC0, 0xDE, 0x3E, 0xD1, 0xA7, 0xEF
    };
//
// select your test command by cmd_no
//
if(cmd_no == 0)
{
    //to test new command to chip
    stp_ctx_lock(&stp_core_ctx);

    /*RESYNC*/
    test_packet[0]=0x7f;
    test_packet[1]=0x7f;
    test_packet[2]=0x7f;
    test_packet[3]=0x7f;
    test_packet[4]=0x7f;
    test_packet[5]=0x7f;
    test_packet[6]=0x7f;
    test_packet[7]=0x7f;

    /*header*/
    reset_payload_len = sizeof(test_payload)/sizeof(test_payload[0]);
    test_packet[8] = 0x80 + (txseq << 3) + txack;
    test_packet[9] = (STP_TASK_INDX << 4) + ((reset_payload_len & 0xf00) >> 8);
    test_packet[10] = reset_payload_len & 0xff;
    test_packet[11] = (test_packet[8] + test_packet[9] + test_packet[10]) & 0xff;

    /*payload*/
    osal_memcpy(&test_packet[12], test_payload, reset_payload_len);

    /*crc*/
    crc = osal_crc16(&test_payload[0], reset_payload_len);
    test_packet[12 + reset_payload_len] = crc & 0xff;
    test_packet[12 + reset_payload_len + 1] = (crc & 0xff00) >> 8;

    (*sys_if_tx)(&test_packet[0], 14 + reset_payload_len, &ret);
    if (ret != (14 + reset_payload_len))
    {
        STP_ERR_FUNC("stp test sending error, sending %d , but ret = %d\n", 10 + reset_payload_len, ret);
    }

    stp_ctx_unlock(&stp_core_ctx);
}

}
/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_flush_context
* DESCRIPTION
*  Flush STP Context
* PARAMETERS
*  none.
* RETURNS
*  none
*****************************************************************************/
void mtk_wcn_stp_flush_context(void)
{
    stp_rest_ctx_state();
}


/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_flush_rx_queue
* DESCRIPTION
*  Flush STP Rx Queue
* PARAMETERS
*  none.
* RETURNS
*  none
*****************************************************************************/

void mtk_wcn_stp_flush_rx_queue(UINT32 type)
{
    stp_ctx_lock(&stp_core_ctx);
    if(type >= 0 && type < MTKSTP_MAX_TASK_NUM)
    {
        stp_core_ctx.ring[type].read_p = 0;
        stp_core_ctx.ring[type].write_p = 0;
    }
    stp_ctx_unlock(&stp_core_ctx);
}

/*****************************************************************************
* FUNCTION
*  mtk_wcn_stp_is_enable
* DESCRIPTION
*  STP is ready?
* PARAMETERS
*  none.
* RETURNS
*  none
*****************************************************************************/
MTK_WCN_BOOL mtk_wcn_stp_is_ready(void)
{
    return STP_IS_READY(stp_core_ctx);
}

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

void mtk_wcn_stp_set_bluez(MTK_WCN_BOOL bluez_flag)
{
    //g_mtkstp_bluez_flag = bluez_flag;
    STP_SET_BT_STK(stp_core_ctx, bluez_flag);
}

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
void mtk_wcn_stp_set_dbg_mode(MTK_WCN_BOOL dbg_mode)
{
    STP_SET_ENABLE_DBG(stp_core_ctx, dbg_mode);
}

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
void mtk_wcn_stp_set_auto_rst(MTK_WCN_BOOL auto_rst)
{
    STP_SET_ENABLE_RST(stp_core_ctx, auto_rst);
}


INT32 mtk_wcn_stp_notify_sleep_for_thermal()
{
    return stp_psm_sleep_for_thermal(STP_PSM_CORE(stp_core_ctx));
}

EXPORT_SYMBOL(mtk_wcn_stp_dbg_level);


