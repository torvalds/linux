/* Copyright Statement:
 *
 * This software/firmware and related documentation ("MediaTek Software") are
 * protected under relevant copyright laws. The information contained herein
 * is confidential and proprietary to MediaTek Inc. and/or its licensors.
 * Without the prior written permission of MediaTek inc. and/or its licensors,
 * any reproduction, modification, use or disclosure of MediaTek Software,
 * and information contained herein, in whole or in part, shall be strictly prohibited.
 *
 * MediaTek Inc. (C) 2010. All rights reserved.
 *
 * BY OPENING THIS FILE, RECEIVER HEREBY UNEQUIVOCALLY ACKNOWLEDGES AND AGREES
 * THAT THE SOFTWARE/FIRMWARE AND ITS DOCUMENTATIONS ("MEDIATEK SOFTWARE")
 * RECEIVED FROM MEDIATEK AND/OR ITS REPRESENTATIVES ARE PROVIDED TO RECEIVER ON
 * AN "AS-IS" BASIS ONLY. MEDIATEK EXPRESSLY DISCLAIMS ANY AND ALL WARRANTIES,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE IMPLIED WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE OR NONINFRINGEMENT.
 * NEITHER DOES MEDIATEK PROVIDE ANY WARRANTY WHATSOEVER WITH RESPECT TO THE
 * SOFTWARE OF ANY THIRD PARTY WHICH MAY BE USED BY, INCORPORATED IN, OR
 * SUPPLIED WITH THE MEDIATEK SOFTWARE, AND RECEIVER AGREES TO LOOK ONLY TO SUCH
 * THIRD PARTY FOR ANY WARRANTY CLAIM RELATING THERETO. RECEIVER EXPRESSLY ACKNOWLEDGES
 * THAT IT IS RECEIVER'S SOLE RESPONSIBILITY TO OBTAIN FROM ANY THIRD PARTY ALL PROPER LICENSES
 * CONTAINED IN MEDIATEK SOFTWARE. MEDIATEK SHALL ALSO NOT BE RESPONSIBLE FOR ANY MEDIATEK
 * SOFTWARE RELEASES MADE TO RECEIVER'S SPECIFICATION OR TO CONFORM TO A PARTICULAR
 * STANDARD OR OPEN FORUM. RECEIVER'S SOLE AND EXCLUSIVE REMEDY AND MEDIATEK'S ENTIRE AND
 * CUMULATIVE LIABILITY WITH RESPECT TO THE MEDIATEK SOFTWARE RELEASED HEREUNDER WILL BE,
 * AT MEDIATEK'S OPTION, TO REVISE OR REPLACE THE MEDIATEK SOFTWARE AT ISSUE,
 * OR REFUND ANY SOFTWARE LICENSE FEES OR SERVICE CHARGE PAID BY RECEIVER TO
 * MEDIATEK FOR SUCH MEDIATEK SOFTWARE AT ISSUE.
 *
 * The following software/firmware and/or related documentation ("MediaTek Software")
 * have been modified by MediaTek Inc. All revisions are subject to any receiver's
 * applicable license agreements with MediaTek Inc.
 */

/*******************************************************************************
*                         C O M P I L E R   F L A G S
********************************************************************************
*/

#define LDISC_LOW_LATENCY (0)
/* 1: set low_latency to tty; rx may be called in hw_irq context;
* 0: rx may be called in kernel global workqueue context (kthread)
*/

/* ldisc rx and do stp data parsing context options:
 *      LDISC_RX_TASKLET:
 *          use rx tasklet to process rx data in bh
 *          cons: can't sleep, priority too high.
 *      LDISC_RX_TTY_CB:
 *          process rx data directly in tty's recv callback context
 *          (ldisc.receive_buf). Real context type depends on LDISC_LOW_LATENCY:
 *              1:interrupt context
 *              0: thread context.
 *          cons: rx data may come too late.
 *      LDISC_RX_WORK (default):
 *          schedule another work_struct to handle
 */
#define LDISC_RX_TASKLET (0)
#define LDISC_RX_TTY_CB (1)
#define LDISC_RX_WORK (2)
/* Select LDISC RX Method */
#define LDISC_RX (LDISC_RX_WORK)

/* More detailed options */
#if (LDISC_RX == LDISC_RX_TASKLET)
#define LDISC_RX_TASKLET_RWLOCK (0)
/* 1: use rwlock to protect rx kfifo (APEX default) for LDISC_RX_TASKLET
 * 0: use _NO_ rwlock
*/
#elif (LDISC_RX == LDISC_RX_WORK)
/* enable low latency mechanism */
#undef LDISC_LOW_LATENCY
#define LDISC_LOW_LATENCY (1)
#endif

/* ldisc tx context options:
 *      LDISC_TX_OLD:
 *          Do uart tx in the caller thread (STP-CORE). Handle local tx buffer
 *          in old way.
 *      LDISC_TX_CALLER_THRD:
 *          Do uart tx in the caller thread (STP-CORE). Handle tx using caller's
 *          buffer directly.
 */
#define LDISC_TX_OLD (0)
#define LDISC_TX_CALLER_THRD (1)

/* Select LDISC TX Method */
#define LDISC_TX (LDISC_TX_CALLER_THRD)

/*******************************************************************************
*                    E X T E R N A L   R E F E R E N C E S
********************************************************************************
*/
#include <linux/version.h>
#include <linux/module.h>

#include <linux/kernel.h>
#include <linux/init.h>
#include <linux/types.h>
#include <linux/fcntl.h>
#include <linux/interrupt.h>
#include <linux/ptrace.h>
#include <linux/poll.h>

#include <linux/slab.h>
#include <linux/tty.h>
#include <linux/errno.h>
#include <linux/string.h>
#include <linux/signal.h>
#include <linux/ioctl.h>
#include <linux/skbuff.h>

#include <linux/spinlock.h>

#include <linux/time.h>
#include <linux/delay.h>
#include <linux/err.h>
#include <linux/kfifo.h>
#include <linux/vmalloc.h> /* vmalloc, vzalloc */
#include <linux/workqueue.h> /* INIT_WORK, schedule_work */

#include "stp_exp.h"

/*******************************************************************************
*                              C O N S T A N T S
********************************************************************************
*/
#define N_MTKSTP (15 + 1) /* refer to linux tty.h use N_HCI. */
#define HCIUARTSETPROTO (_IOW('U', 200, int))

#define PFX "[STP-U]"
#define UART_LOG_LOUD (4)
#define UART_LOG_DBG (3)
#define UART_LOG_INFO (2)
#define UART_LOG_WARN (1)
#define UART_LOG_ERR (0)

#define MAX_PACKET_ALLOWED (2000)

#if (LDISC_RX == LDISC_RX_TASKLET)
#define LDISC_RX_FIFO_SIZE (0x20000/*0x2000*/) /* 128K or 8K bytes? */

#elif (LDISC_RX == LDISC_RX_WORK)
#define LDISC_RX_FIFO_SIZE (0x4000) /* 16K bytes shall be enough...... */
#define LDISC_RX_BUF_SIZE (2048) /* 2K bytes in one shot is enough */

#endif

#if (LDISC_TX == LDISC_TX_OLD)
/* no additional allocated for tx using caller's thread */
//#define STP_UART_TX_BUF_SIZE (MTKSTP_BUFFER_SIZE)
#define STP_UART_TX_BUF_SIZE (4096) /* 16K or 4K bytes is enough? */

#elif (LDISC_TX == LDISC_TX_CALLER_THRD)

#define LDISC_TX_CALLER_THRD_RTY_LMT (10)
#define LDISC_TX_CALLER_THRD_RTY_MIN_DLY (3)
#endif

/*******************************************************************************
*                            P U B L I C   D A T A
********************************************************************************
*/

unsigned int gStpDbgLevel = UART_LOG_ERR;//UART_LOG_INFO;//modify loglevel

struct tty_struct *stp_tty = NULL;

/*******************************************************************************
*                           P R I V A T E   D A T A
********************************************************************************
*/

#if (LDISC_RX == LDISC_RX_TASKLET)
struct kfifo *g_stp_uart_rx_fifo = NULL;
spinlock_t g_stp_uart_rx_fifo_spinlock;
struct tasklet_struct g_stp_uart_rx_fifo_tasklet;
    #if LDISC_RX_TASKLET_RWLOCK
static DEFINE_RWLOCK(g_stp_uart_rx_handling_lock);
    #endif
#endif

#if (LDISC_RX == LDISC_RX_WORK)
UINT8 *g_stp_uart_rx_buf; /* for stp rx data parsing */
struct kfifo *g_stp_uart_rx_fifo = NULL; /* for uart tty data receiving */
spinlock_t g_stp_uart_rx_fifo_spinlock; /* fifo spinlock */
struct workqueue_struct *g_stp_uart_rx_wq; /* rx work queue (do not use system_wq) */
struct work_struct *g_stp_uart_rx_work; /* rx work */
#endif

/* Private info for each Tx method */
#if (LDISC_TX == LDISC_TX_OLD)
static unsigned char tx_buf[STP_UART_TX_BUF_SIZE] = {0x0};
static unsigned int rd_idx = 0; /* the position next to read */
static unsigned int wr_idx = 0; /* the position next to write */
/* tx_buf in STP-UART is protected by caller(STP-CORE) locking mechanism. */
//struct semaphore buf_mtx; /* unused */
//static spinlock_t buf_lock; /* unused */

#elif (LDISC_TX == LDISC_TX_CALLER_THRD)
static int tx_retry_limit = LDISC_TX_CALLER_THRD_RTY_LMT;
static int tx_retry_delay_ms = LDISC_TX_CALLER_THRD_RTY_MIN_DLY;

#else
#error "UNSUPPORTED LDISC_TX" LDISC_TX
#endif

//static unsigned int tx_count = 0; /* drop tx data test? */

/*******************************************************************************
*                             D A T A   T Y P E S
********************************************************************************
*/

/*******************************************************************************
*                                 M A C R O S
********************************************************************************
*/
//#define MAX(a,b) ((a) > (b) ? (a) : (b)) //unused
//#define MIN(a,b) ((a) < (b) ? (a) : (b)) //unused

#define UART_LOUD_FUNC(fmt, arg...)    if(gStpDbgLevel >= UART_LOG_LOUD){  printk(PFX "[L]%s:"  fmt, __FUNCTION__ ,##arg);}
#define UART_DBG_FUNC(fmt, arg...)    if(gStpDbgLevel >= UART_LOG_DBG){  printk(PFX "[D]%s:"  fmt, __FUNCTION__ ,##arg);}
#define UART_INFO_FUNC(fmt, arg...)   if(gStpDbgLevel >= UART_LOG_INFO){ printk(PFX "[I]%s:"  fmt, __FUNCTION__ ,##arg);}
#define UART_WARN_FUNC(fmt, arg...)   if(gStpDbgLevel >= UART_LOG_WARN){ printk(PFX "[W]%s:"  fmt, __FUNCTION__ ,##arg);}
#define UART_ERR_FUNC(fmt, arg...)    if(gStpDbgLevel >= UART_LOG_ERR){  printk(PFX "[E]%s:"   fmt, __FUNCTION__ ,##arg);}
#define UART_TRC_FUNC(f)              if(gStpDbgLevel >= UART_LOG_DBG){  printk(PFX "<%s/%d>\n", __FUNCTION__, __LINE__);}

/*******************************************************************************
*                  F U N C T I O N   D E C L A R A T I O N S
********************************************************************************
*/
static INT32 mtk_wcn_uart_tx (
    const UINT8 *data,
    const UINT32 size,
    UINT32 *written_size
    );

/*******************************************************************************
*                              F U N C T I O N S
********************************************************************************
*/

#if (LDISC_TX == LDISC_TX_OLD)
static int stp_uart_tty_tx_init (void)
{
    //init_MUTEX(&buf_mtx);
    //spin_lock_init(&buf_lock);
    rd_idx = wr_idx = 0;
    return 0;
}

static int stp_uart_tty_tx_deinit (void)
{
    rd_idx = wr_idx = 0;
    return 0;
}

/* stp_uart_tty_open
 *
 * Arguments:
 *     tty  pointer to tty info structure
 *     data pointer to data buffer to be written
 *     size data buffer length to be written
 * Return Value:
 *     > 0 if success, otherwise error code
 */
static int stp_uart_tty_tx_write (
    struct tty_struct *tty,
    const UINT8 *data,
    const UINT32 size
    )
{
    int ret;
    int len;
    int written;
    int written_count;
    int room;
    unsigned old_wr;
    unsigned old_rd;
    //unsigned long flags;

    UART_LOUD_FUNC("++\n");

    /* Phase-I: put data into STP-UART ring buffer "tx_buf" */
    /* wr_idx : the position next to write
     *  rd_idx : the position next to read
     */

    //down(&buf_mtx);
    /* [PatchNeed] spin_lock_irqsave is redundant */
    //spin_lock_irqsave(&buf_lock, flags);
    old_wr = wr_idx;
    old_rd = rd_idx;

    /* check left room size */
    room = (wr_idx >= rd_idx)
        ? (STP_UART_TX_BUF_SIZE - (wr_idx - rd_idx) - 1)
        : (rd_idx - wr_idx - 1);
    UART_DBG_FUNC("before data in:r(%d)s(%d)wr_i(%d)rd_i(%d)\n",
        room, size, wr_idx, rd_idx);

    if (unlikely(size > room)) {
        UART_ERR_FUNC("buf unavailable FAIL#1,size(%d),wr_idx(%d),rd_idx(%d),room(%d),pid[%d/%s]\n",
            size, wr_idx, rd_idx, room, current->pid, current->comm);
        //up(&buf_mtx);
        /* [PatchNeed] spin_lock_irqsave is redundant */
        //spin_unlock_irqrestore(&buf_lock, flags);
        return -1;
    }
    else {
        len = min(size, STP_UART_TX_BUF_SIZE - (unsigned int)wr_idx);
        memcpy(&tx_buf[wr_idx], &data[0], len);
        memcpy(&tx_buf[0], &data[len], size - len);
        wr_idx = (wr_idx + size) % STP_UART_TX_BUF_SIZE;
        UART_DBG_FUNC("after data in: r(%d)s(%d)wr_i(%d)rd_i(%d)\n",
            room, size, wr_idx, rd_idx);
    }
    //up(&buf_mtx);
    /* [PatchNeed] spin_lock_irqsave is redundant */
    //spin_unlock_irqrestore(&buf_lock, flags);

    /* Phase-II: get data from the buffer and send to tty UART.
     * May be seperated into another context.
    */

    //down(&buf_mtx);
    /* [PatchNeed] spin_lock_irqsave is redundant */
    //spin_lock_irqsave(&buf_lock, flags);
    written_count = 0;

    len = (wr_idx >= rd_idx) ? (wr_idx - rd_idx) : (STP_UART_TX_BUF_SIZE - rd_idx);
    if (likely(len > 0 && len < MAX_PACKET_ALLOWED))
    {
        /* TTY_DO_WRITE_WAKEUP is used for "Call write_wakeup after queuing new"
         * but stp_uart_tty_wakeup() is empty and unused now!
         */
        //set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);

        /*
         * ops->write is called by the kernel to write a series of
         * characters to the tty device.  The characters may come from
         * user space or kernel space.  This routine will return the
         * number of characters actually accepted for writing.
        */
        written = tty->ops->write(tty, &tx_buf[rd_idx], len);
        if (written != len) {
            UART_ERR_FUNC("tty-ops->write FAIL#2,len(%d)wr(%d)wr_i(%d)rd_i(%d),pid[%d/%s]\n",
                len, written, wr_idx, rd_idx, current->pid, current->comm);
            ret = -2;
            goto tx_write_out_unlock_old;
        }
        written_count = written;
        rd_idx = ((rd_idx + written) % STP_UART_TX_BUF_SIZE);

        // all data is accepted by UART driver, check again in case roll over
        len = (wr_idx >= rd_idx) ? (wr_idx - rd_idx) : (STP_UART_TX_BUF_SIZE - rd_idx);
        if (len > 0 && len < MAX_PACKET_ALLOWED)
        {
            /* TTY_DO_WRITE_WAKEUP is used for "Call write_wakeup after queuing new"
             * but stp_uart_tty_wakeup() is empty and unused now!
             */
            //set_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);

            written = tty->ops->write(tty, &tx_buf[rd_idx], len);
            if (unlikely(written != len))
            {
                UART_ERR_FUNC("tty-ops->write FAIL#3,len(%d)wr(%d)wr_i(%d)rd_i(%d),pid[%d/%s]\n",
                    len, written, wr_idx, rd_idx, current->pid, current->comm);
                ret = -3;
                goto tx_write_out_unlock_old;
            }
            rd_idx = ((rd_idx + written) % STP_UART_TX_BUF_SIZE);
            written_count += written;
        }
        else if (unlikely(len < 0 || len >= MAX_PACKET_ALLOWED))
        {
            UART_ERR_FUNC("FAIL#4,len(%d)wr_i(%d)rd_i(%d),pid[%d/%s]\n",
                len, wr_idx, rd_idx,
                current->pid, current->comm);
            ret = -4;
            goto tx_write_out_unlock_old;
        }
    }
    else
    {
        UART_ERR_FUNC("FAIL#5,len(%d)wr_i(%d)rd_i(%d),pid[%d/%s]\n",
            len, wr_idx, rd_idx,
            current->pid, current->comm);
        ret = -5;
        goto tx_write_out_unlock_old;
    }

    /* success case */
    ret = written_count;


tx_write_out_unlock_old:
    if (unlikely(ret < 0)) {
        //reset read and write index of tx_buffer, is there any risk?
        wr_idx = rd_idx = 0;
        UART_ERR_FUNC("err(%d)reset fifo idx\n", ret);
    }

    if (unlikely(wr_idx != rd_idx)) {
        UART_WARN_FUNC("--wr(%d)rd(%d)size(%d)old wr(%d)rd(%d)\n",
            wr_idx, rd_idx, size, old_wr, old_rd);

   }
    else {
        UART_LOUD_FUNC("--wr(%d) rd(%d)\n", wr_idx, rd_idx);
    }
    //up(&buf_mtx);
    //spin_unlock_irqrestore(&buf_lock, flags);

    return ret;
}

/* end of (LDISC_TX == LDISC_TX_OLD) */

#elif (LDISC_TX == LDISC_TX_CALLER_THRD)
static inline int stp_uart_tty_tx_init (void)
{
    /* nothing to be done */
    return 0;
}

static inline int stp_uart_tty_tx_deinit (void)
{
    /* nothing to be done */
    return 0;
}

static int stp_uart_tty_tx_write (
    struct tty_struct *tty,
    const UINT8 *data,
    const UINT32 size
    )
{
    int written;
    int wr_count;
    int retry_left;
    int retry_delay_ms;

    wr_count = tty->ops->write(tty, data, size);
    if (likely(wr_count == size)) {
        /* perfect case! */
        return wr_count;
    }

    UART_DBG_FUNC("tty write FAIL#1,size(%d)wr(%d)pid[%d/%s]\n",
        size, written, current->pid, current->comm);

    /* error handling */
    retry_left = tx_retry_limit;
    retry_delay_ms = tx_retry_delay_ms;
    while ( (retry_left--) && (wr_count < size)) {
        /* do msleep if and only if STP-CORE using process context (caller's or
         * any other task) instead of any irq context (hardirq, softirq, tasklet
         * , timer, etc).
        */
        msleep(retry_delay_ms);
        // TODO: to be refined by considering wr_count, current baud rate, etc.
        retry_delay_ms *= 2;
        written = tty->ops->write(tty, data + wr_count, size - wr_count);
        wr_count += written;
    }

    if (likely(wr_count == size)) {
        UART_INFO_FUNC("recovered,size(%d)retry_left(%d)delay(%d)\n",
            size, retry_left, retry_delay_ms);
        /* workable case! */
        return wr_count;
    }

    /* return -written_count as error code and let caller to further error handle */
    UART_ERR_FUNC("tty write FAIL#2,size(%d)wr(%d)retry_left(%d)pid[%d/%s]\n",
        size, wr_count, retry_left, current->pid, current->comm);

    return -wr_count;
}
#else
#error "unknown LDISC_TX" LDISC_TX
#endif

#if (LDISC_RX == LDISC_RX_TASKLET)
static int stp_uart_fifo_init(void)
{
    int err = 0;

    /*add rx fifo*/
    #if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))
    {
        spin_lock_init(&g_stp_uart_rx_fifo_spinlock);
        g_stp_uart_rx_fifo = kfifo_alloc(LDISC_RX_FIFO_SIZE, GFP_ATOMIC, &g_stp_uart_rx_fifo_spinlock);
        if (NULL == g_stp_uart_rx_fifo)
        {
            UART_ERR_FUNC("kfifo_alloc failed (kernel version < 2.6.33)\n");
            err = -1;
        }
    }
    #else
    {
        g_stp_uart_rx_fifo = kzalloc(sizeof(struct kfifo), GFP_ATOMIC);
        if (NULL == g_stp_uart_rx_fifo)
        {
            err = -2;
            UART_ERR_FUNC("kzalloc for g_stp_uart_rx_fifo failed (kernel version > 2.6.33)\n");
        }
        err = kfifo_alloc(g_stp_uart_rx_fifo, LDISC_RX_FIFO_SIZE, GFP_ATOMIC);
        if (0 != err)
        {
            UART_ERR_FUNC("kfifo_alloc failed, errno(%d)(kernel version > 2.6.33)\n", err);
            kfree(g_stp_uart_rx_fifo);
            g_stp_uart_rx_fifo = NULL;
            err = -3;
        }
    }
    #endif

    if (0 == err)
    {
        if (NULL != g_stp_uart_rx_fifo)
        {
            kfifo_reset(g_stp_uart_rx_fifo);
            UART_ERR_FUNC("stp_uart_fifo_init() success.\n");
        }
        else
        {
            err = -4;
            UART_ERR_FUNC("abnormal case, err = 0 but g_stp_uart_rx_fifo = NULL, set err to %d\n", err);
        }
    }
    else
    {
        UART_ERR_FUNC("stp_uart_fifo_init() failed.\n");
    }
    return err;
}

static int stp_uart_fifo_deinit(void)
{
    if (NULL != g_stp_uart_rx_fifo)
    {
        kfifo_free(g_stp_uart_rx_fifo);
        #if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))
                //do nothing
        #else
        kfree(g_stp_uart_rx_fifo);
        #endif
        g_stp_uart_rx_fifo = NULL;
    }
    return 0;
}

static void stp_uart_rx_handling(unsigned long func_data){
    #define LOCAL_BUFFER_LEN 1024
    unsigned char data[LOCAL_BUFFER_LEN];
    unsigned int how_much_get = 0;
    unsigned int how_much_to_get = 0;
    unsigned int flag = 0;

#if LDISC_RX_TASKLET_RWLOCK
    read_lock(&g_stp_uart_rx_handling_lock);
#endif

    how_much_to_get = kfifo_len(g_stp_uart_rx_fifo);

    if (how_much_to_get >= LOCAL_BUFFER_LEN)
    {
        flag = 1;
        UART_INFO_FUNC ("fifolen(%d)\n", how_much_to_get);
    }

    do {
        #if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))
        how_much_get= kfifo_get(g_stp_uart_rx_fifo, data, LOCAL_BUFFER_LEN);
        #else
        how_much_get= kfifo_out(g_stp_uart_rx_fifo, data, LOCAL_BUFFER_LEN);
        #endif
        UART_INFO_FUNC ("fifoget(%d)\n", how_much_get);
        mtk_wcn_stp_parser_data((UINT8 *)data, how_much_get);
        how_much_to_get = kfifo_len(g_stp_uart_rx_fifo);
    }while(how_much_to_get > 0);

#if LDISC_RX_TASKLET_RWLOCK
    read_unlock(&g_stp_uart_rx_handling_lock);
#endif

    if (1 == flag)
    {
        UART_INFO_FUNC ("finish, fifolen(%d)\n", kfifo_len(g_stp_uart_rx_fifo));
    }
}

/* stp_uart_tty_receive()
 *
 *     Called by tty low level driver when receive data is
 *     available.
 *
 * Arguments:  tty          pointer to tty isntance data
 *             data         pointer to received data
 *             flags        pointer to flags for data
 *             count        count of received data in bytes
 *
 * Return Value:    None
 */
static void stp_uart_tty_receive(struct tty_struct *tty, const u8 *data, char *flags, int count)
{
    unsigned int fifo_avail_len;/* = LDISC_RX_FIFO_SIZE - kfifo_len(g_stp_uart_rx_fifo);*/
    unsigned int how_much_put = 0;

#if 0
    {
        struct timeval now;
        do_gettimeofday(&now);
        printk("[+STP][  ][R] %4d --> sec = %lu, --> usec --> %lu\n",
            count, now.tv_sec, now.tv_usec);
    }
#endif

#if LDISC_RX_TASKLET_RWLOCK
    write_lock(&g_stp_uart_rx_handling_lock);
#endif

    if (count > 2000) {
        /*this is abnormal*/
        UART_ERR_FUNC("abnormal: buffer count = %d\n", count);
    }
    /*How much empty seat?*/
    fifo_avail_len = LDISC_RX_FIFO_SIZE - kfifo_len(g_stp_uart_rx_fifo);
    if (fifo_avail_len > 0) {
        //UART_INFO_FUNC ("fifo left(%d), count(%d)\n", fifo_avail_len, count);
        #if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))
        how_much_put = kfifo_put(g_stp_uart_rx_fifo,(unsigned char *) data, count);
        #else
        how_much_put = kfifo_in(g_stp_uart_rx_fifo,(unsigned char *) data, count);
        #endif

#if LDISC_RX_TASKLET_RWLOCK
        /* George Test */
        write_unlock(&g_stp_uart_rx_handling_lock);
#endif

        /*schedule it!*/
        tasklet_schedule(&g_stp_uart_rx_fifo_tasklet);
    }
    else {
        UART_ERR_FUNC("stp_uart_tty_receive rxfifo is full!!\n");
    }

#if 0
    {
        struct timeval now;
        do_gettimeofday(&now);
        printk("[-STP][  ][R] %4d --> sec = %lu, --> usec --> %lu\n",
            count, now.tv_sec, now.tv_usec);
    }
#endif

#if LDISC_RX_TASKLET_RWLOCK
    /* George Test */
    //write_unlock(&g_stp_uart_rx_handling_lock);
#endif

}

#elif (LDISC_RX == LDISC_RX_TTY_CB)
/* stp_uart_tty_receive()
 *
 *     Called by tty low level driver when receive data is
 *     available.
 *
 * Arguments:  tty          pointer to tty isntance data
 *             data         pointer to received data
 *             flags        pointer to flags for data
 *             count        count of received data in bytes
 *
 * Return Value:    None
 */
static void stp_uart_tty_receive(struct tty_struct *tty, const u8 *data, char *flags, int count)
{
    //UART_INFO_FUNC("count = %d\n", count);

    if (count > 2000){
        /*this is abnormal*/
        UART_WARN_FUNC("count = %d\n", count);
    }

#if 0
    {
        struct timeval now;

        do_gettimeofday(&now);

        printk("[+STP][  ][R] %4d --> sec = %d, --> usec --> %d\n",
            count, now.tv_sec, now.tv_usec);
    }
#endif


    /*There are multi-context to access here? Need to spinlock?*/
    /*Only one context: flush_to_ldisc in tty_buffer.c*/
    mtk_wcn_stp_parser_data((UINT8 *)data, (UINT32)count);

    /* George Test: useless? */
    /*tty_unthrottle(tty);*/

#if 0
    {
        struct timeval now;

        do_gettimeofday(&now);

        printk("[-STP][  ][R] %4d --> sec = %d, --> usec --> %d\n",
            count, now.tv_sec, now.tv_usec);
    }
#endif
    return;
}

#elif (LDISC_RX == LDISC_RX_WORK)
static int stp_uart_fifo_init(void)
{
    int err = 0;

    #if (LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,37))
    g_stp_uart_rx_buf = vzalloc(LDISC_RX_BUF_SIZE);
    if (!g_stp_uart_rx_buf) {
        UART_ERR_FUNC("kfifo_alloc failed (kernel version >= 2.6.37)\n");
        err = -4;
        goto fifo_init_end;
    }
    #else
    g_stp_uart_rx_buf = vmalloc(LDISC_RX_BUF_SIZE);
    if (!g_stp_uart_rx_buf) {
        UART_ERR_FUNC("kfifo_alloc failed (kernel version < 2.6.37)\n");
        err = -4;
        goto fifo_init_end;
    }
    memset(g_stp_uart_rx_buf, 0, LDISC_RX_BUF_SIZE);
    #endif

    UART_LOUD_FUNC("g_stp_uart_rx_buf alloc ok(0x%p, %d)\n",
        g_stp_uart_rx_buf, LDISC_RX_BUF_SIZE);

    /*add rx fifo*/
    #if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))
    spin_lock_init(&g_stp_uart_rx_fifo_spinlock);
    g_stp_uart_rx_fifo = kfifo_alloc(LDISC_RX_FIFO_SIZE, GFP_KERNEL, &g_stp_uart_rx_fifo_spinlock);
    if (NULL == g_stp_uart_rx_fifo) {
        UART_ERR_FUNC("kfifo_alloc failed (kernel version < 2.6.33)\n");
        err = -1;
        goto fifo_init_end;
    }
    #else
    /* allocate struct kfifo first */
    g_stp_uart_rx_fifo = kzalloc(sizeof(struct kfifo), GFP_KERNEL);
    if (NULL == g_stp_uart_rx_fifo) {
        err = -2;
        UART_ERR_FUNC("kzalloc struct kfifo failed (kernel version > 2.6.33)\n");
        goto fifo_init_end;
    }

    /* allocate kfifo data buffer then */
    err = kfifo_alloc(g_stp_uart_rx_fifo, LDISC_RX_FIFO_SIZE, GFP_KERNEL);
    if (0 != err) {
        UART_ERR_FUNC("kfifo_alloc failed, err(%d)(kernel version > 2.6.33)\n", err);
        kfree(g_stp_uart_rx_fifo);
        g_stp_uart_rx_fifo = NULL;
        err = -3;
        goto fifo_init_end;
    }
    #endif
    UART_LOUD_FUNC("g_stp_uart_rx_fifo alloc ok\n");

fifo_init_end:

    if (0 == err) {
        /* kfifo init ok */
        kfifo_reset(g_stp_uart_rx_fifo);
        UART_DBG_FUNC("g_stp_uart_rx_fifo init success\n");
    }
    else {
        UART_ERR_FUNC("stp_uart_fifo_init() fail(%d)\n", err);
        if (g_stp_uart_rx_buf) {
            UART_ERR_FUNC("free g_stp_uart_rx_buf\n");
            vfree(g_stp_uart_rx_buf);
            g_stp_uart_rx_buf = NULL;
        }
    }

    return err;
}

static int stp_uart_fifo_deinit(void)
{
    if (g_stp_uart_rx_buf) {
        vfree(g_stp_uart_rx_buf);
        g_stp_uart_rx_buf = NULL;
    }

    if (NULL != g_stp_uart_rx_fifo) {
        kfifo_free(g_stp_uart_rx_fifo);
        #if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))
        //do nothing
        #else
        kfree(g_stp_uart_rx_fifo);
        #endif
        g_stp_uart_rx_fifo = NULL;
    }
    return 0;
}

static void stp_uart_rx_worker (struct work_struct *work)
{
    unsigned int read;

    if (unlikely(!g_stp_uart_rx_fifo)) {
        UART_ERR_FUNC("NULL rx fifo!\n");
        return;
    }
    if (unlikely(!g_stp_uart_rx_buf)) {
        UART_ERR_FUNC("NULL rx buf!\n");
        return;
    }

    /* run until fifo becomes empty */
#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))
    while (kfifo_len(g_stp_uart_rx_fifo)) {
        read = kfifo_get(g_stp_uart_rx_fifo, g_stp_uart_rx_buf, LDISC_RX_BUF_SIZE);
        //UART_LOUD_FUNC("kfifo_get(%d)\n", read);
        if (likely(read)) {
            mtk_wcn_stp_parser_data((UINT8 *)g_stp_uart_rx_buf, read);
        }
    }
#else
    while (!kfifo_is_empty(g_stp_uart_rx_fifo)) {
        read = kfifo_out(g_stp_uart_rx_fifo, g_stp_uart_rx_buf, LDISC_RX_BUF_SIZE);
        UART_DBG_FUNC("kfifo_out(%d)\n", read);
        if (likely(read)) {
            //UART_LOUD_FUNC("->%d\n", read);
            mtk_wcn_stp_parser_data((UINT8 *)g_stp_uart_rx_buf, read);
            //UART_LOUD_FUNC("<-\n", read);
        }
    }
#endif

    return;
}

/* stp_uart_tty_receive()
 *
 *     Called by tty low level driver when receive data is
 *     available.
 *
 * Arguments:  tty          pointer to tty isntance data
 *             data         pointer to received data
 *             flags        pointer to flags for data
 *             count        count of received data in bytes
 *
 * Return Value:    None
 */
static void stp_uart_tty_receive(struct tty_struct *tty, const u8 *data, char *flags, int count)
{
    unsigned int written;

    //UART_LOUD_FUNC("URX:%d\n", count);
    if (unlikely(count > 2000)) {
        UART_WARN_FUNC("abnormal: buffer count = %d\n", count);
    }

    if (unlikely(!g_stp_uart_rx_fifo || !g_stp_uart_rx_work || !g_stp_uart_rx_wq)) {
        UART_ERR_FUNC("abnormal g_stp_uart_rx_fifo(0x%p),g_stp_uart_rx_work(0x%p),g_stp_uart_rx_wq(0x%p)\n",
            g_stp_uart_rx_fifo, g_stp_uart_rx_work, g_stp_uart_rx_wq);
        return;
    }

    /* need to check available buffer size? skip! */

    /* need to lock fifo? skip for single writer single reader! */

#if (LINUX_VERSION_CODE < KERNEL_VERSION(2,6,33))
    written = kfifo_put(g_stp_uart_rx_fifo, (unsigned char *) data, count);
#else
    written = kfifo_in(g_stp_uart_rx_fifo, (unsigned char *) data, count);
#endif
    queue_work(g_stp_uart_rx_wq, g_stp_uart_rx_work);

    if (unlikely(written != count)) {
        UART_ERR_FUNC("c(%d),w(%d) bytes dropped\n", count, written);
    }

    return;
}

#else
#error "unknown LDISC_RX!" LDISC_RX
#endif

/* ------ LDISC part ------ */
/* stp_uart_tty_open
 *
 *     Called when line discipline changed to HCI_UART.
 *
 * Arguments:
 *     tty    pointer to tty info structure
 * Return Value:
 *     0 if success, otherwise error code
 */
static int stp_uart_tty_open(struct tty_struct *tty)
{
    UART_DBG_FUNC("original receive_room(%d) low_latency(%d) in tty(%p)\n",
        tty->receive_room, tty->low_latency, tty);

    tty->receive_room = 65536;
#if LDISC_LOW_LATENCY
    tty->low_latency = 1;
#endif
    UART_DBG_FUNC("set receive_room(%d) low_latency(%d) to tty(%p)\n",
        tty->receive_room, tty->low_latency, tty);

    /* Flush any pending characters in the driver and line discipline. */

    /* FIXME: why is this needed. Note don't use ldisc_ref here as the
       open path is before the ldisc is referencable */

#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,29)
    /* definition changed!! */
    if (tty->ldisc->ops->flush_buffer) {
        tty->ldisc->ops->flush_buffer(tty);
    }
#else
    if (tty->ldisc.ops->flush_buffer) {
        tty->ldisc.ops->flush_buffer(tty);
    }
#endif

    tty_driver_flush_buffer(tty);

    stp_uart_tty_tx_init();

    stp_tty = tty;

    /* Register to STP-CORE */
    mtk_wcn_stp_register_if_tx(STP_UART_IF_TX,  mtk_wcn_uart_tx);

    return 0;
}

/* stp_uart_tty_close()
 *
 *    Called when the line discipline is changed to something
 *    else, the tty is closed, or the tty detects a hangup.
 */
static void stp_uart_tty_close(struct tty_struct *tty)
{
    UART_DBG_FUNC("stp_uart_tty_close(): tty %p\n", tty);
    mtk_wcn_stp_register_if_tx(STP_UART_IF_TX, NULL);

    stp_uart_tty_tx_deinit();

    return;
}

/* stp_uart_tty_wakeup()
 *
 *    Callback for transmit wakeup. Called when low level
 *    device driver can accept more send data.
 *
 * Arguments:        tty    pointer to associated tty instance data
 * Return Value:    None
 */
static void stp_uart_tty_wakeup(struct tty_struct *tty)
{
    /*
    UART_INFO_FUNC("in\n");
    clear_bit(TTY_DO_WRITE_WAKEUP, &tty->flags);
    stp_uart_tx_wakeup(tty);
    */

    return;
}

/* stp_uart_tty_ioctl()
 *
 *    Process IOCTL system call for the tty device.
 *
 * Arguments:
 *
 *    tty        pointer to tty instance data
 *    file       pointer to open file object for device
 *    cmd        IOCTL command code
 *    arg        argument for IOCTL call (cmd dependent)
 *
 * Return Value:    Command dependent
 */
static int stp_uart_tty_ioctl(struct tty_struct *tty, struct file * file,
                    unsigned int cmd, unsigned long arg)
{
    int err = 0;

    UART_LOUD_FUNC("++ ll(%d)\n", tty->low_latency);

    switch (cmd) {
    case HCIUARTSETPROTO:
#if LDISC_LOW_LATENCY
        UART_INFO_FUNC("set low_latency to 1\n");
        tty->low_latency = 1;
#endif
        break;
    default:
        UART_LOUD_FUNC("redirect to n_tty_ioctl_helper\n");
        err = n_tty_ioctl_helper(tty, file, cmd, arg);
        UART_LOUD_FUNC("n_tty_ioctl_helper result(0x%x %d)\n", cmd, err);
        break;
    };

    UART_LOUD_FUNC("--\n");
    return err;
}

/*
 * We don't provide read/write/poll interface for user space.
 */
static ssize_t stp_uart_tty_read(struct tty_struct *tty, struct file *file,
                    unsigned char __user *buf, size_t nr)
{
    return 0;
}

static ssize_t stp_uart_tty_write(struct tty_struct *tty, struct file *file,
                    const unsigned char *data, size_t count)
{
    return 0;
}

static unsigned int stp_uart_tty_poll(struct tty_struct *tty,
                    struct file *filp, poll_table *wait)
{
    return 0;
}

static INT32 mtk_wcn_uart_tx (
    const UINT8 *data,
    const UINT32 size,
    UINT32 *written_size
    )
{
    INT32 ret;
    int tx_len;

    if (unlikely(0 == size)) {
        /* special case for STP-CORE, return ASAP. */
        if (likely(written_size)) {
            *written_size = 0;
        }
        return 0;
    }

    UART_LOUD_FUNC("++\n");

    /* input sanity checks */
    if (unlikely(stp_tty == NULL)) {
        UART_ERR_FUNC("NULL stp_tty,pid[%d/%s]\n", current->pid, current->comm);
        ret = -1;
        goto uart_tx_out;
    }

    if (unlikely((data == NULL) || (written_size == NULL))) {
        UART_ERR_FUNC("NULL data(0x%p) or written(0x%p),pid[%d/%s]\n",
            data, written_size, current->pid, current->comm);
        ret = -2;
        goto uart_tx_out;
    }

    *written_size = 0;

    /* Do size checking. Only 1~MAX_PACKET_ALLOWED-1 is allowed */
    if (unlikely(MAX_PACKET_ALLOWED <= size)) {
        UART_ERR_FUNC("abnormal size(%d),skip tx,pid[%d/%s]\n",
            size, current->pid, current->comm);
        dump_stack();
        ret = -3;
        goto uart_tx_out;
    }

#if 0 /* drop data test */
    if ((tx_count > 1000) && (tx_count % 5)== 0) {
        UART_INFO_FUNC("i=(%d), ****** drop data from uart******\n", i);
        ++tx_count;
        return 0;
    }
#endif

    tx_len = stp_uart_tty_tx_write(stp_tty, data, size);
    if (unlikely(tx_len < 0)) {
        UART_WARN_FUNC("stp_uart_tty_tx_write err(%d)\n", tx_len);
        *written_size = 0;
        ret = -4;
    }
    else {
        *written_size = tx_len;
        ret = 0;
    }

uart_tx_out:
    UART_LOUD_FUNC("--(%d, %d)\n", ret, *written_size);

    return ret;
}

static int __init mtk_wcn_stp_uart_init(void)
{
    static struct tty_ldisc_ops stp_uart_ldisc;
    int err;
    int fifo_init_done;

    UART_INFO_FUNC("MTK STP UART driver\n");

    fifo_init_done = 0;

#if (LDISC_RX == LDISC_RX_TASKLET)
    err = stp_uart_fifo_init();
    if (err != 0) {
        UART_ERR_FUNC("stp_uart_fifo_init(TASKLET) error(%d)\n", err);
        err = -EFAULT;
        goto init_err;
    }
    fifo_init_done = 1;

    /*init rx tasklet*/
    tasklet_init(&g_stp_uart_rx_fifo_tasklet, stp_uart_rx_handling, (unsigned long) 0);

#elif (LDISC_RX == LDISC_RX_WORK)
    err = stp_uart_fifo_init();
    if (err != 0) {
        UART_ERR_FUNC("stp_uart_fifo_init(WORK) error(%d)\n", err);
        err = -EFAULT;
        goto init_err;
    }
    fifo_init_done = 1;

    g_stp_uart_rx_work = vmalloc(sizeof(struct work_struct));
    if (!g_stp_uart_rx_work) {
        UART_ERR_FUNC("vmalloc work_struct(%d) fail\n", sizeof(struct work_struct));
        err = -ENOMEM;
        goto init_err;
    }

    g_stp_uart_rx_wq = create_singlethread_workqueue("mtk_urxd");
    if (!g_stp_uart_rx_wq) {
        UART_ERR_FUNC("create_singlethread_workqueue fail\n");
        err = -ENOMEM;
        goto init_err;
    }

    /* init rx work */
    INIT_WORK(g_stp_uart_rx_work, stp_uart_rx_worker);
#endif

     /* Register the tty discipline */
    memset(&stp_uart_ldisc, 0, sizeof (stp_uart_ldisc));
    stp_uart_ldisc.magic    = TTY_LDISC_MAGIC;
    stp_uart_ldisc.name     = "n_mtkstp";
    stp_uart_ldisc.open     = stp_uart_tty_open;
    stp_uart_ldisc.close    = stp_uart_tty_close;
    stp_uart_ldisc.read     = stp_uart_tty_read;
    stp_uart_ldisc.write    = stp_uart_tty_write;
    stp_uart_ldisc.ioctl    = stp_uart_tty_ioctl;
    stp_uart_ldisc.poll     = stp_uart_tty_poll;
    stp_uart_ldisc.receive_buf  = stp_uart_tty_receive;
    stp_uart_ldisc.write_wakeup = stp_uart_tty_wakeup;
    stp_uart_ldisc.owner    = THIS_MODULE;

    err = tty_register_ldisc(N_MTKSTP, &stp_uart_ldisc);
    if (err) {
        UART_ERR_FUNC("MTK STP line discipline(%d) registration failed. (%d)\n", N_MTKSTP, err);
        goto init_err;
    }

    /* init ok */
    return 0;

init_err:

#if (LDISC_RX == LDISC_RX_TASKLET)
    /* nothing */
    if (fifo_init_done) {
        stp_uart_fifo_deinit();
    }
#elif (LDISC_RX == LDISC_RX_WORK)
    if (g_stp_uart_rx_wq) {
        destroy_workqueue(g_stp_uart_rx_wq);
        g_stp_uart_rx_wq = NULL;
    }
    if (g_stp_uart_rx_work) {
        vfree(g_stp_uart_rx_work);
    }
    if (fifo_init_done) {
        stp_uart_fifo_deinit();
    }
#endif
    UART_ERR_FUNC("init fail, return(%d)\n", err);

    return err;
}

static void __exit mtk_wcn_stp_uart_exit(void)
{
    int err;

    mtk_wcn_stp_register_if_tx(STP_UART_IF_TX, NULL);    // unregister if_tx function

    /* Release tty registration of line discipline */
    if ((err = tty_unregister_ldisc(N_MTKSTP)))
    {
        UART_ERR_FUNC("Can't unregister MTK STP line discipline (%d)\n", err);
    }

#if (LDISC_RX == LDISC_RX_TASKLET)
    tasklet_kill(&g_stp_uart_rx_fifo_tasklet);
    stp_uart_fifo_deinit();
#elif (LDISC_RX == LDISC_RX_WORK)
    if (g_stp_uart_rx_work) {
        cancel_work_sync(g_stp_uart_rx_work);
    }
    if (g_stp_uart_rx_wq) {
        destroy_workqueue(g_stp_uart_rx_wq);
        g_stp_uart_rx_wq = NULL;
    }
    if (g_stp_uart_rx_work) {
        vfree(g_stp_uart_rx_work);
        g_stp_uart_rx_work = NULL;
    }
    stp_uart_fifo_deinit();
#endif

    return;
}

module_init(mtk_wcn_stp_uart_init);
module_exit(mtk_wcn_stp_uart_exit);

//MODULE_LICENSE("Proprietary");
MODULE_LICENSE("GPL");
MODULE_AUTHOR("MediaTek Inc WCN_SE_CS3");
MODULE_DESCRIPTION("STP-HIF UART Interface");

