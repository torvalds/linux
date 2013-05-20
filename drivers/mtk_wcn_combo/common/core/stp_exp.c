/*******************************************************************************
*                E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#if 0    //to do---- need check why need this header file
#include <linux/types.h>
#include <linux/major.h>
#include <linux/errno.h>
#include <linux/signal.h>
#include <linux/fcntl.h>
#include <linux/sched.h>
#include <linux/interrupt.h>
#include <linux/tty.h>
#include <linux/timer.h>
#include <linux/ctype.h>
#include <linux/mm.h>
#include <linux/string.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <linux/bitops.h>
#include <linux/audit.h>
#include <linux/file.h>
#include <linux/module.h>

#include <linux/spinlock.h>
#include <linux/delay.h> /* udelay() */

#include <asm/uaccess.h>
#include <asm/system.h>
#endif
#include "stp_core.h"
#include "stp_exp.h"

/*******************************************************************************
*                          C O N S T A N T S
********************************************************************************
*/


/*******************************************************************************
*                         D A T A   T Y P E S
********************************************************************************
*/


/*******************************************************************************
*                        P U B L I C   D A T A
********************************************************************************
*/
/*******************************************************************************
*                       P R I V A T E   D A T A
********************************************************************************
*/
static MTK_WCN_STP_IF_TX stp_uart_if_tx = NULL;
static MTK_WCN_STP_IF_TX stp_sdio_if_tx = NULL;
static ENUM_STP_TX_IF_TYPE g_stp_if_type = STP_MAX_IF_TX;
static MTK_WCN_STP_IF_RX stp_if_rx = NULL;
static MTK_WCN_STP_EVENT_CB event_callback_tbl[MTKSTP_MAX_TASK_NUM] = {0x0};
static MTK_WCN_STP_EVENT_CB tx_event_callback_tbl[MTKSTP_MAX_TASK_NUM] = {0x0};

/******************************************************************************
*                   F U N C T I O N   D E C L A R A T I O N S
*******************************************************************************
*/

/*******************************************************************************
*                          F U N C T I O N S
********************************************************************************
*/

INT32 mtk_wcn_sys_if_rx(UINT8 *data, INT32 size)
{
    if(stp_if_rx == 0x0)
    {
        return (-1);
    }
    else
    {
        (*stp_if_rx)(data, size);
        return 0;
    }
}

static INT32 mtk_wcn_sys_if_tx (
    const UINT8 *data,
    const UINT32 size,
    UINT32 *written_size
    )
{

    if (STP_UART_IF_TX == g_stp_if_type) {
        return stp_uart_if_tx != NULL ? (*stp_uart_if_tx)(data, size, written_size) : -1;
    }
    else if (STP_SDIO_IF_TX == g_stp_if_type) {
        return stp_sdio_if_tx != NULL ? (*stp_sdio_if_tx)(data, size, written_size) : -1;
    }
    else {
        /*if (g_stp_if_type >= STP_MAX_IF_TX) */ /* George: remove ALWAYS TRUE condition */
        return (-1);
    }
}

static INT32 mtk_wcn_sys_event_set(UINT8 function_type)
{
    if((function_type < MTKSTP_MAX_TASK_NUM) && (event_callback_tbl[function_type] != 0x0))
    {
        (*event_callback_tbl[function_type])();
    }
    else {
        /* FIXME: error handling */
        osal_dbg_print("[%s] STP set event fail. It seems the function is not active.\n", __func__);
    }

    return 0;
}

static INT32 mtk_wcn_sys_event_tx_resume(UINT8 winspace)
{
    int type = 0;

    for(type = 0 ;  type < MTKSTP_MAX_TASK_NUM ; type ++ )
    {
        if(tx_event_callback_tbl[type])
        {
            tx_event_callback_tbl[type]();
        }
    }

    return 0;
}

static INT32 mtk_wcn_sys_check_function_status(UINT8 type, UINT8 op){

    /*op == FUNCTION_ACTIVE, to check if funciton[type] is active ?*/
    if(!(type >= 0 && type < MTKSTP_MAX_TASK_NUM))
    {
        return STATUS_FUNCTION_INVALID;
    }

    if(op == OP_FUNCTION_ACTIVE)
    {
        if(event_callback_tbl[type] != 0x0)
        {
            return STATUS_FUNCTION_ACTIVE;
        }
        else
        {
            return STATUS_FUNCTION_INACTIVE;
        }
    }
    /*you can define more operation here ..., to queury function's status/information*/

    return STATUS_OP_INVALID;
}

INT32 mtk_wcn_stp_register_if_rx(MTK_WCN_STP_IF_RX func)
{
    stp_if_rx = func;

    return 0;    
}

VOID mtk_wcn_stp_set_if_tx_type (
    ENUM_STP_TX_IF_TYPE stp_if_type
    )
{
    g_stp_if_type = stp_if_type;
    osal_dbg_print("[%s] set STP_IF_TX to %s.\n",
        __FUNCTION__,
        (STP_UART_IF_TX == stp_if_type)? "UART" : ((STP_SDIO_IF_TX == stp_if_type) ? "SDIO" : "NULL"));
}

INT32 mtk_wcn_stp_register_if_tx (
    ENUM_STP_TX_IF_TYPE stp_if,
    MTK_WCN_STP_IF_TX func
    )
{
    if (STP_UART_IF_TX == stp_if) 
    {
        stp_uart_if_tx = func;
    }
    else if (STP_SDIO_IF_TX == stp_if) 
    {
        stp_sdio_if_tx = func;
    }
    else 
    {
        osal_dbg_print("[%s] STP_IF_TX(%d) out of boundary.\n", __FUNCTION__, stp_if);
        return -1;
    }

    return 0;
}

INT32 mtk_wcn_stp_register_event_cb(INT32 type, MTK_WCN_STP_EVENT_CB func)
{
    if (type < MTKSTP_MAX_TASK_NUM)
    {
        event_callback_tbl[type] = func;

        /*clear rx queue*/
        osal_dbg_print("Flush type = %d Rx Queue\n", type);
        mtk_wcn_stp_flush_rx_queue(type);
    }

    return 0;
}

INT32 mtk_wcn_stp_register_tx_event_cb(INT32 type, MTK_WCN_STP_EVENT_CB func)
{
    if(type < MTKSTP_MAX_TASK_NUM)
    {
        tx_event_callback_tbl[type] = func;
    }
    else
    {
        osal_bug_on(0);
    }

    return 0;
}

INT32 stp_drv_init(VOID)
{
    mtkstp_callback cb =
    {
        .cb_if_tx           = mtk_wcn_sys_if_tx,
        .cb_event_set       = mtk_wcn_sys_event_set,
        .cb_event_tx_resume = mtk_wcn_sys_event_tx_resume,
        .cb_check_funciton_status = mtk_wcn_sys_check_function_status
    };

    return mtk_wcn_stp_init(&cb);
}

VOID stp_drv_exit(VOID)
{
    mtk_wcn_stp_deinit();

    return;
}

EXPORT_SYMBOL(mtk_wcn_stp_register_if_tx);
EXPORT_SYMBOL(mtk_wcn_stp_register_if_rx);
EXPORT_SYMBOL(mtk_wcn_stp_register_event_cb);
EXPORT_SYMBOL(mtk_wcn_stp_register_tx_event_cb);
EXPORT_SYMBOL(mtk_wcn_stp_parser_data);
EXPORT_SYMBOL(mtk_wcn_stp_send_data);
EXPORT_SYMBOL(mtk_wcn_stp_send_data_raw);
EXPORT_SYMBOL(mtk_wcn_stp_receive_data);
EXPORT_SYMBOL(mtk_wcn_stp_is_rxqueue_empty);
EXPORT_SYMBOL(mtk_wcn_stp_set_bluez);
EXPORT_SYMBOL(mtk_wcn_stp_is_ready);
EXPORT_SYMBOL(mtk_wcn_stp_dbg_log_ctrl);





