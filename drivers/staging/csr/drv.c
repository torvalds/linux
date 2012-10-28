/*
 * ---------------------------------------------------------------------------
 *  FILE:     drv.c
 *
 *  PURPOSE:
 *      Conventional device interface for debugging/monitoring of the
 *      driver and h/w using unicli. This interface is also being used
 *      by the SME linux implementation and the helper apps.
 *
 * Copyright (C) 2005-2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */

/*
 * Porting Notes:
 * Part of this file contains an example for how to glue the OS layer
 * with the HIP core lib, the SDIO glue layer, and the SME.
 *
 * When the unifi_sdio.ko modules loads, the linux kernel calls unifi_load().
 * unifi_load() calls uf_sdio_load() which is exported by the SDIO glue
 * layer. uf_sdio_load() registers this driver with the underlying SDIO driver.
 * When a card is detected, the SDIO glue layer calls register_unifi_sdio()
 * to pass the SDIO function context and ask the OS layer to initialise
 * the card. register_unifi_sdio() allocates all the private data of the OS
 * layer and calls uf_run_unifihelper() to start the SME. The SME calls
 * unifi_sys_wifi_on_req() which uses the HIP core lib to initialise the card.
 */

#include <linux/init.h>
#include <linux/slab.h>
#include <linux/poll.h>
#include <asm/uaccess.h>
#include <linux/jiffies.h>
#include <linux/version.h>

#include "csr_wifi_hip_unifiversion.h"
#include "unifi_priv.h"
#include "csr_wifi_hip_conversions.h"
#include "unifi_native.h"

/* Module parameter variables */
int buswidth = 0;               /* 0 means use default, values 1,4 */
int sdio_clock = 50000;         /* kHz */
int unifi_debug = 0;
/* fw_init prevents f/w initialisation on error. */
int fw_init[MAX_UNIFI_DEVS] = {-1, -1};
int use_5g = 0;
int led_mask = 0;               /* 0x0c00 for dev-pc-1503c, dev-pc-1528a */
int disable_hw_reset = 0;
int disable_power_control = 0;
int enable_wol = UNIFI_WOL_OFF; /* 0 for none, 1 for SDIO IRQ, 2 for PIO */
#if (defined CSR_SUPPORT_SME) && (defined CSR_SUPPORT_WEXT)
int tl_80211d = (int)CSR_WIFI_SME_80211D_TRUST_LEVEL_MIB;
#endif
int sdio_block_size = -1;      /* Override SDIO block size */
int sdio_byte_mode = 0;        /* 0 for block mode + padding, 1 for byte mode */
int coredump_max = CSR_WIFI_HIP_NUM_COREDUMP_BUFFERS;
int run_bh_once = -1;          /* Set for scheduled interrupt mode, -1 = default */
int bh_priority = -1;
#ifdef CSR_WIFI_HIP_DEBUG_OFFLINE
#define UNIFI_LOG_HIP_SIGNALS_FILTER_BULKDATA   (1 << 1)
#define UNIFI_LOG_HIP_SIGNALS_FILTER_TIMESTAMP  (1 << 2)
int log_hip_signals = 0;
#endif

MODULE_DESCRIPTION("CSR UniFi (SDIO)");

module_param(buswidth,    int, S_IRUGO|S_IWUSR);
module_param(sdio_clock,  int, S_IRUGO|S_IWUSR);
module_param(unifi_debug, int, S_IRUGO|S_IWUSR);
module_param_array(fw_init, int, NULL, S_IRUGO|S_IWUSR);
module_param(use_5g,      int, S_IRUGO|S_IWUSR);
module_param(led_mask,    int, S_IRUGO|S_IWUSR);
module_param(disable_hw_reset,  int, S_IRUGO|S_IWUSR);
module_param(disable_power_control,  int, S_IRUGO|S_IWUSR);
module_param(enable_wol,  int, S_IRUGO|S_IWUSR);
#if (defined CSR_SUPPORT_SME) && (defined CSR_SUPPORT_WEXT)
module_param(tl_80211d,   int, S_IRUGO|S_IWUSR);
#endif
module_param(sdio_block_size, int, S_IRUGO|S_IWUSR);
module_param(sdio_byte_mode, int, S_IRUGO|S_IWUSR);
module_param(coredump_max, int, S_IRUGO|S_IWUSR);
module_param(run_bh_once, int, S_IRUGO|S_IWUSR);
module_param(bh_priority, int, S_IRUGO|S_IWUSR);
#ifdef CSR_WIFI_HIP_DEBUG_OFFLINE
module_param(log_hip_signals, int, S_IRUGO|S_IWUSR);
#endif

MODULE_PARM_DESC(buswidth, "SDIO bus width (0=default), set 1 for 1-bit or 4 for 4-bit mode");
MODULE_PARM_DESC(sdio_clock, "SDIO bus frequency in kHz, (default = 50 MHz)");
MODULE_PARM_DESC(unifi_debug, "Diagnostic reporting level");
MODULE_PARM_DESC(fw_init, "Set to 0 to prevent f/w initialization on error");
MODULE_PARM_DESC(use_5g, "Use the 5G (802.11a) radio band");
MODULE_PARM_DESC(led_mask, "LED mask flags");
MODULE_PARM_DESC(disable_hw_reset, "Set to 1 to disable hardware reset");
MODULE_PARM_DESC(disable_power_control, "Set to 1 to disable SDIO power control");
MODULE_PARM_DESC(enable_wol, "Enable wake-on-wlan function 0=off, 1=SDIO, 2=PIO");
#if (defined CSR_SUPPORT_SME) && (defined CSR_SUPPORT_WEXT)
MODULE_PARM_DESC(tl_80211d, "802.11d Trust Level (1-6, default = 5)");
#endif
MODULE_PARM_DESC(sdio_block_size, "Set to override SDIO block size");
MODULE_PARM_DESC(sdio_byte_mode, "Set to 1 for byte mode SDIO");
MODULE_PARM_DESC(coredump_max, "Number of chip mini-coredump buffers to allocate");
MODULE_PARM_DESC(run_bh_once, "Run BH only when firmware interrupts");
MODULE_PARM_DESC(bh_priority, "Modify the BH thread priority");
#ifdef CSR_WIFI_HIP_DEBUG_OFFLINE
MODULE_PARM_DESC(log_hip_signals, "Set to 1 to enable HIP signal offline logging");
#endif


/* Callback for event logging to UDI clients */
static void udi_log_event(ul_client_t *client,
                          const u8 *signal, int signal_len,
                          const bulk_data_param_t *bulkdata,
                          int dir);

static void udi_set_log_filter(ul_client_t *pcli,
                               unifiio_filter_t *udi_filter);


/* Mutex to protect access to  priv->sme_cli */
DEFINE_SEMAPHORE(udi_mutex);

s32 CsrHipResultToStatus(CsrResult csrResult)
{
    s32 r = -EIO;

    switch (csrResult)
    {
    case CSR_RESULT_SUCCESS:
        r = 0;
        break;
    case CSR_WIFI_HIP_RESULT_RANGE:
        r = -ERANGE;
        break;
    case CSR_WIFI_HIP_RESULT_NO_DEVICE:
        r = -ENODEV;
        break;
    case CSR_WIFI_HIP_RESULT_INVALID_VALUE:
        r = -EINVAL;
        break;
    case CSR_WIFI_HIP_RESULT_NOT_FOUND:
        r = -ENOENT;
        break;
    case CSR_WIFI_HIP_RESULT_NO_SPACE:
        r = -ENOSPC;
        break;
    case CSR_WIFI_HIP_RESULT_NO_MEMORY:
        r = -ENOMEM;
        break;
    case CSR_RESULT_FAILURE:
        r = -EIO;
        break;
    default:
        /*unifi_warning(card->ospriv, "CsrHipResultToStatus: Unrecognised csrResult error code: %d\n", csrResult);*/
        r = -EIO;
    }
    return r;
}


static const char*
trace_putest_cmdid(unifi_putest_command_t putest_cmd)
{
    switch (putest_cmd)
    {
        case UNIFI_PUTEST_START:
            return "START";
        case UNIFI_PUTEST_STOP:
            return "STOP";
        case UNIFI_PUTEST_SET_SDIO_CLOCK:
            return "SET CLOCK";
        case UNIFI_PUTEST_CMD52_READ:
            return "CMD52R";
        case UNIFI_PUTEST_CMD52_BLOCK_READ:
            return "CMD52BR";
        case UNIFI_PUTEST_CMD52_WRITE:
            return "CMD52W";
        case UNIFI_PUTEST_DL_FW:
            return "D/L FW";
        case UNIFI_PUTEST_DL_FW_BUFF:
            return "D/L FW BUFFER";
        case UNIFI_PUTEST_COREDUMP_PREPARE:
            return "PREPARE COREDUMP";
        case UNIFI_PUTEST_GP_READ16:
            return "GP16R";
        case UNIFI_PUTEST_GP_WRITE16:
            return "GP16W";
        default:
            return "ERROR: unrecognised command";
    }
 }

#ifdef CSR_WIFI_HIP_DEBUG_OFFLINE
int uf_register_hip_offline_debug(unifi_priv_t *priv)
{
    ul_client_t *udi_cli;
    int i;

    udi_cli = ul_register_client(priv, CLI_USING_WIRE_FORMAT, udi_log_event);
    if (udi_cli == NULL) {
        /* Too many clients already using this device */
        unifi_error(priv, "Too many UDI clients already open\n");
        return -ENOSPC;
    }
    unifi_trace(priv, UDBG1, "Offline HIP client is registered\n");

    down(&priv->udi_logging_mutex);
    udi_cli->event_hook = udi_log_event;
    unifi_set_udi_hook(priv->card, logging_handler);
    /* Log all signals by default */
    for (i = 0; i < SIG_FILTER_SIZE; i++) {
        udi_cli->signal_filter[i] = 0xFFFF;
    }
    priv->logging_client = udi_cli;
    up(&priv->udi_logging_mutex);

    return 0;
}

int uf_unregister_hip_offline_debug(unifi_priv_t *priv)
{
    ul_client_t *udi_cli = priv->logging_client;
    if (udi_cli == NULL)
    {
        unifi_error(priv, "Unknown HIP client unregister request\n");
        return -ERANGE;
    }

    unifi_trace(priv, UDBG1, "Offline HIP client is unregistered\n");

    down(&priv->udi_logging_mutex);
    priv->logging_client = NULL;
    udi_cli->event_hook = NULL;
    up(&priv->udi_logging_mutex);

    ul_deregister_client(udi_cli);

    return 0;
}
#endif


/*
 * ---------------------------------------------------------------------------
 *  unifi_open
 *  unifi_release
 *
 *      Open and release entry points for the UniFi debug driver.
 *
 *  Arguments:
 *      Normal linux driver args.
 *
 *  Returns:
 *      Linux error code.
 * ---------------------------------------------------------------------------
 */
static int
unifi_open(struct inode *inode, struct file *file)
{
    int devno;
    unifi_priv_t *priv;
    ul_client_t *udi_cli;

    func_enter();

    devno = MINOR(inode->i_rdev) >> 1;

    /*
     * Increase the ref_count for the char device clients.
     * Make sure you call uf_put_instance() to decreace it if
     * unifi_open returns an error.
     */
    priv = uf_get_instance(devno);
    if (priv == NULL) {
        unifi_error(NULL, "unifi_open: No device present\n");
        func_exit();
        return -ENODEV;
    }

    /* Register this instance in the client's list. */
    /* The minor number determines the nature of the client (Unicli or SME). */
    if (MINOR(inode->i_rdev) & 0x1) {
        udi_cli = ul_register_client(priv, CLI_USING_WIRE_FORMAT, udi_log_event);
        if (udi_cli == NULL) {
            /* Too many clients already using this device */
            unifi_error(priv, "Too many clients already open\n");
            uf_put_instance(devno);
            func_exit();
            return -ENOSPC;
        }
        unifi_trace(priv, UDBG1, "Client is registered to /dev/unifiudi%d\n", devno);
    } else {
        /*
         * Even-numbered device nodes are the control application.
         * This is the userspace helper containing SME or
         * unifi_manager.
         */

        down(&udi_mutex);

#ifdef CSR_SME_USERSPACE
        /* Check if a config client is already attached */
        if (priv->sme_cli) {
            up(&udi_mutex);
            uf_put_instance(devno);

            unifi_info(priv, "There is already a configuration client using the character device\n");
            func_exit();
            return -EBUSY;
        }
#endif /* CSR_SME_USERSPACE */

#ifdef CSR_SUPPORT_SME
        udi_cli = ul_register_client(priv,
                                     CLI_USING_WIRE_FORMAT | CLI_SME_USERSPACE,
                                     sme_log_event);
#else
        /* Config client for native driver */
        udi_cli = ul_register_client(priv,
                                     0,
                                     sme_native_log_event);
#endif
        if (udi_cli == NULL) {
            /* Too many clients already using this device */
            up(&udi_mutex);
            uf_put_instance(devno);

            unifi_error(priv, "Too many clients already open\n");
            func_exit();
            return -ENOSPC;
        }

        /*
         * Fill-in the pointer to the configuration client.
         * This is the SME userspace helper or unifi_manager.
         * Not used in the SME embedded version.
         */
        unifi_trace(priv, UDBG1, "SME client (id:%d s:0x%X) is registered\n",
                    udi_cli->client_id, udi_cli->sender_id);
        /* Store the SME UniFi Linux Client */
        if (priv->sme_cli == NULL) {
            priv->sme_cli = udi_cli;
        }

        up(&udi_mutex);
    }


    /*
     * Store the pointer to the client.
     * All char driver's entry points will pass this pointer.
     */
    file->private_data = udi_cli;

    func_exit();
    return 0;
} /* unifi_open() */


static int
unifi_release(struct inode *inode, struct file *filp)
{
    ul_client_t *udi_cli = (void*)filp->private_data;
    int devno;
    unifi_priv_t *priv;

    func_enter();

    priv = uf_find_instance(udi_cli->instance);
    if (!priv) {
        unifi_error(priv, "unifi_close: instance for device not found\n");
        return -ENODEV;
    }

    devno = MINOR(inode->i_rdev) >> 1;

    /* Even device nodes are the config client (i.e. SME or unifi_manager) */
    if ((MINOR(inode->i_rdev) & 0x1) == 0) {

        if (priv->sme_cli != udi_cli) {
            unifi_notice(priv, "Surprise closing config device: not the sme client\n");
        }
        unifi_notice(priv, "SME client close (unifi%d)\n", devno);

        /*
         * Clear sme_cli before calling unifi_sys_... so it doesn't try to
         * queue a reply to the (now gone) SME.
         */
        down(&udi_mutex);
        priv->sme_cli = NULL;
        up(&udi_mutex);

#ifdef CSR_SME_USERSPACE
        /* Power-down when config client closes */
        {
            CsrWifiRouterCtrlWifiOffReq req = {{CSR_WIFI_ROUTER_CTRL_HIP_REQ, 0, 0, 0, NULL}};
            CsrWifiRouterCtrlWifiOffReqHandler(priv, &req.common);
        }

        uf_sme_deinit(priv);

       /* It is possible that a blocking SME request was made from another process
        * which did not get read by the SME before the WifiOffReq.
        * So check for a pending request which will go unanswered and cancel
        * the wait for event. As only one blocking request can be in progress at
        * a time, up to one event should be completed.
        */
       uf_sme_cancel_request(priv, 0);

#endif /* CSR_SME_USERSPACE */
    } else {

        unifi_trace(priv, UDBG2, "UDI client close (unifiudi%d)\n", devno);

        /* If the pointer matches the logging client, stop logging. */
        down(&priv->udi_logging_mutex);
        if (udi_cli == priv->logging_client) {
            priv->logging_client = NULL;
        }
        up(&priv->udi_logging_mutex);

        if (udi_cli == priv->amp_client) {
            priv->amp_client = NULL;
        }
    }

    /* Deregister this instance from the client's list. */
    ul_deregister_client(udi_cli);

    uf_put_instance(devno);

    return 0;
} /* unifi_release() */



/*
 * ---------------------------------------------------------------------------
 *  unifi_read
 *
 *      The read() driver entry point.
 *
 *  Arguments:
 *      filp        The file descriptor returned by unifi_open()
 *      p           The user space buffer to copy the read data
 *      len         The size of the p buffer
 *      poff
 *
 *  Returns:
 *      number of bytes read or an error code on failure
 * ---------------------------------------------------------------------------
 */
static ssize_t
unifi_read(struct file *filp, char *p, size_t len, loff_t *poff)
{
    ul_client_t *pcli = (void*)filp->private_data;
    unifi_priv_t *priv;
    udi_log_t *logptr = NULL;
    udi_msg_t *msgptr;
    struct list_head *l;
    int msglen;

    func_enter();

    priv = uf_find_instance(pcli->instance);
    if (!priv) {
        unifi_error(priv, "invalid priv\n");
        return -ENODEV;
    }

    if (!pcli->udi_enabled) {
        unifi_error(priv, "unifi_read: unknown client.");
        return -EINVAL;
    }

    if (list_empty(&pcli->udi_log)) {
        if (filp->f_flags & O_NONBLOCK) {
            /* Non-blocking - just return if the udi_log is empty */
            return 0;
        } else {
            /* Blocking - wait on the UDI wait queue */
            if (wait_event_interruptible(pcli->udi_wq,
                !list_empty(&pcli->udi_log)))
            {
                unifi_error(priv, "unifi_read: wait_event_interruptible failed.");
                return -ERESTARTSYS;
            }
        }
    }

    /* Read entry from list head and remove it from the list */
    if (down_interruptible(&pcli->udi_sem)) {
        return -ERESTARTSYS;
    }
    l = pcli->udi_log.next;
    list_del(l);
    up(&pcli->udi_sem);

    /* Get a pointer to whole struct */
    logptr = list_entry(l, udi_log_t, q);
    if (logptr == NULL) {
        unifi_error(priv, "unifi_read: failed to get event.\n");
        return -EINVAL;
    }

    /* Get the real message */
    msgptr = &logptr->msg;
    msglen = msgptr->length;
    if (msglen > len) {
        printk(KERN_WARNING "truncated read to %d actual msg len is %lu\n", msglen, (long unsigned int)len);
        msglen = len;
    }

    /* and pass it to the client (SME or Unicli). */
    if (copy_to_user(p, msgptr, msglen))
    {
        printk(KERN_ERR "Failed to copy UDI log to user\n");
        kfree(logptr);
        return -EFAULT;
    }

    /* It is our resposibility to free the message buffer. */
    kfree(logptr);

    func_exit_r(msglen);
    return msglen;

} /* unifi_read() */



/*
 * ---------------------------------------------------------------------------
 * udi_send_signal_unpacked
 *
 *      Sends an unpacked signal to UniFi.
 *
 * Arguments:
 *      priv            Pointer to private context struct
 *      data            Pointer to request structure and data to send
 *      data_len        Length of data in data pointer.
 *
 * Returns:
 *      Number of bytes written, error otherwise.
 *
 * Notes:
 *      All clients that use this function to send a signal to the unifi
 *      must use the host formatted structures.
 * ---------------------------------------------------------------------------
 */
static int
udi_send_signal_unpacked(unifi_priv_t *priv, unsigned char* data, uint data_len)
{
    CSR_SIGNAL *sigptr = (CSR_SIGNAL*)data;
    CSR_DATAREF *datarefptr;
    bulk_data_param_t bulk_data;
    uint signal_size, i;
    uint bulk_data_offset = 0;
    int bytecount, r;
    CsrResult csrResult;

    /* Number of bytes in the signal */
    signal_size = SigGetSize(sigptr);
    if (!signal_size || (signal_size > data_len)) {
        unifi_error(priv, "unifi_sme_mlme_req - Invalid signal 0x%x size should be %d bytes\n",
                    sigptr->SignalPrimitiveHeader.SignalId,
                    signal_size);
        return -EINVAL;
    }
    bytecount = signal_size;

    /* Get a pointer to the information of the first data reference */
    datarefptr = (CSR_DATAREF*)&sigptr->u;

    /* Initialize the offset in the data buffer, bulk data is right after the signal. */
    bulk_data_offset = signal_size;

    /* store the references and the size of the bulk data to the bulkdata structure */
    for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; i++) {
        /* the length of the bulk data is in the signal */
        if ((datarefptr+i)->DataLength) {
            void *dest;

            csrResult = unifi_net_data_malloc(priv, &bulk_data.d[i], (datarefptr+i)->DataLength);
            if (csrResult != CSR_RESULT_SUCCESS) {
                unifi_error(priv, "udi_send_signal_unpacked: failed to allocate request_data.\n");
                return -EIO;
            }

            dest = (void*)bulk_data.d[i].os_data_ptr;
            memcpy(dest, data + bulk_data_offset, bulk_data.d[i].data_length);
        } else {
            bulk_data.d[i].data_length = 0;
        }

        bytecount += bulk_data.d[i].data_length;
        /* advance the offset, to point the next bulk data */
        bulk_data_offset += bulk_data.d[i].data_length;
    }


    unifi_trace(priv, UDBG3, "SME Send: signal 0x%.4X\n", sigptr->SignalPrimitiveHeader.SignalId);

    /* Send the signal. */
    r = ul_send_signal_unpacked(priv, sigptr, &bulk_data);
    if (r < 0) {
        unifi_error(priv, "udi_send_signal_unpacked: send failed (%d)\n", r);
        for(i=0;i<UNIFI_MAX_DATA_REFERENCES;i++) {
            if(bulk_data.d[i].data_length != 0) {
                unifi_net_data_free(priv, &bulk_data.d[i]);
            }
        }
        func_exit();
        return -EIO;
    }

    return bytecount;
} /* udi_send_signal_unpacked() */



/*
 * ---------------------------------------------------------------------------
 * udi_send_signal_raw
 *
 *      Sends a packed signal to UniFi.
 *
 * Arguments:
 *      priv            Pointer to private context struct
 *      buf             Pointer to request structure and data to send
 *      buflen          Length of data in data pointer.
 *
 * Returns:
 *      Number of bytes written, error otherwise.
 *
 * Notes:
 *      All clients that use this function to send a signal to the unifi
 *      must use the wire formatted structures.
 * ---------------------------------------------------------------------------
 */
static int
udi_send_signal_raw(unifi_priv_t *priv, unsigned char *buf, int buflen)
{
    int signal_size;
    int sig_id;
    bulk_data_param_t data_ptrs;
    int i, r;
    unsigned int num_data_refs;
    int bytecount;
    CsrResult csrResult;

    func_enter();

    /*
     * The signal is the first thing in buf, the signal id is the
     * first 16 bits of the signal.
     */
    /* Number of bytes in the signal */
    sig_id = GET_SIGNAL_ID(buf);
    signal_size = buflen;
    signal_size -= GET_PACKED_DATAREF_LEN(buf, 0);
    signal_size -= GET_PACKED_DATAREF_LEN(buf, 1);
    if ((signal_size <= 0) || (signal_size > buflen)) {
        unifi_error(priv, "udi_send_signal_raw - Couldn't find length of signal 0x%x\n",
                    sig_id);
        func_exit();
        return -EINVAL;
    }
    unifi_trace(priv, UDBG2, "udi_send_signal_raw: signal 0x%.4X len:%d\n",
                sig_id, signal_size);
    /* Zero the data ref arrays */
    memset(&data_ptrs, 0, sizeof(data_ptrs));

    /*
     * Find the number of associated bulk data packets.  Scan through
     * the data refs to check that we have enough data and pick out
     * pointers to appended bulk data.
     */
    num_data_refs = 0;
    bytecount = signal_size;

    for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; ++i)
    {
        unsigned int len = GET_PACKED_DATAREF_LEN(buf, i);
        unifi_trace(priv, UDBG3, "udi_send_signal_raw: data_ref length = %d\n", len);

        if (len != 0) {
            void *dest;

            csrResult = unifi_net_data_malloc(priv, &data_ptrs.d[i], len);
            if (csrResult != CSR_RESULT_SUCCESS) {
                unifi_error(priv, "udi_send_signal_raw: failed to allocate request_data.\n");
                return -EIO;
            }

            dest = (void*)data_ptrs.d[i].os_data_ptr;
            memcpy(dest, buf + bytecount, len);

            bytecount += len;
            num_data_refs++;
        }
        data_ptrs.d[i].data_length = len;
    }

    unifi_trace(priv, UDBG3, "Queueing signal 0x%.4X from UDI with %u data refs\n",
          sig_id,
          num_data_refs);

    if (bytecount > buflen) {
        unifi_error(priv, "udi_send_signal_raw: Not enough data (%d instead of %d)\n", buflen, bytecount);
        func_exit();
        return -EINVAL;
    }

    /* Send the signal calling the function that uses the wire-formatted signals. */
    r = ul_send_signal_raw(priv, buf, signal_size, &data_ptrs);
    if (r < 0) {
        unifi_error(priv, "udi_send_signal_raw: send failed (%d)\n", r);
        func_exit();
        return -EIO;
    }

#ifdef CSR_NATIVE_LINUX
    if (sig_id == CSR_MLME_POWERMGT_REQUEST_ID) {
        int power_mode = CSR_GET_UINT16_FROM_LITTLE_ENDIAN((buf +
                                              SIZEOF_SIGNAL_HEADER + (UNIFI_MAX_DATA_REFERENCES*SIZEOF_DATAREF)));
#ifdef CSR_SUPPORT_WEXT
        /* Overide the wext power mode to the new value */
        priv->wext_conf.power_mode = power_mode;
#endif
        /* Configure deep sleep signaling */
        if (power_mode || (priv->interfacePriv[0]->connected == UnifiNotConnected)) {
            csrResult = unifi_configure_low_power_mode(priv->card,
                                                   UNIFI_LOW_POWER_ENABLED,
                                                   UNIFI_PERIODIC_WAKE_HOST_DISABLED);
        } else {
            csrResult = unifi_configure_low_power_mode(priv->card,
                                                   UNIFI_LOW_POWER_DISABLED,
                                                   UNIFI_PERIODIC_WAKE_HOST_DISABLED);
        }
    }
#endif

    func_exit_r(bytecount);

    return bytecount;
} /* udi_send_signal_raw */

/*
 * ---------------------------------------------------------------------------
 *  unifi_write
 *
 *      The write() driver entry point.
 *      A UniFi Debug Interface client such as unicli can write a signal
 *      plus bulk data to the driver for sending to the UniFi chip.
 *
 *      Only one signal may be sent per write operation.
 *
 *  Arguments:
 *      filp        The file descriptor returned by unifi_open()
 *      p           The user space buffer to get the data from
 *      len         The size of the p buffer
 *      poff
 *
 *  Returns:
 *      number of bytes written or an error code on failure
 * ---------------------------------------------------------------------------
 */
static ssize_t
unifi_write(struct file *filp, const char *p, size_t len, loff_t *poff)
{
    ul_client_t *pcli = (ul_client_t*)filp->private_data;
    unifi_priv_t *priv;
    unsigned char *buf;
    unsigned char *bufptr;
    int remaining;
    int bytes_written;
    int r;
    bulk_data_param_t bulkdata;
    CsrResult csrResult;

    func_enter();

    priv = uf_find_instance(pcli->instance);
    if (!priv) {
        unifi_error(priv, "invalid priv\n");
        return -ENODEV;
    }

    unifi_trace(priv, UDBG5, "unifi_write: len = %d\n", len);

    if (!pcli->udi_enabled) {
        unifi_error(priv, "udi disabled\n");
        return -EINVAL;
    }

    /*
     * AMP client sends only one signal at a time, so we can use
     * unifi_net_data_malloc to save the extra copy.
     */
    if (pcli == priv->amp_client) {
        int signal_size;
        int sig_id;
        unsigned char *signal_buf;
        char *user_data_buf;

        csrResult = unifi_net_data_malloc(priv, &bulkdata.d[0], len);
        if (csrResult != CSR_RESULT_SUCCESS) {
            unifi_error(priv, "unifi_write: failed to allocate request_data.\n");
            func_exit();
            return -ENOMEM;
        }

        user_data_buf = (char*)bulkdata.d[0].os_data_ptr;

        /* Get the data from the AMP client. */
        if (copy_from_user((void*)user_data_buf, p, len)) {
            unifi_error(priv, "unifi_write: copy from user failed\n");
            unifi_net_data_free(priv, &bulkdata.d[0]);
            func_exit();
            return -EFAULT;
        }

        bulkdata.d[1].os_data_ptr = NULL;
        bulkdata.d[1].data_length = 0;

        /* Number of bytes in the signal */
        sig_id = GET_SIGNAL_ID(bulkdata.d[0].os_data_ptr);
        signal_size = len;
        signal_size -= GET_PACKED_DATAREF_LEN(bulkdata.d[0].os_data_ptr, 0);
        signal_size -= GET_PACKED_DATAREF_LEN(bulkdata.d[0].os_data_ptr, 1);
        if ((signal_size <= 0) || (signal_size > len)) {
            unifi_error(priv, "unifi_write - Couldn't find length of signal 0x%x\n",
                        sig_id);
            unifi_net_data_free(priv, &bulkdata.d[0]);
            func_exit();
            return -EINVAL;
        }

        unifi_trace(priv, UDBG2, "unifi_write: signal 0x%.4X len:%d\n",
                    sig_id, signal_size);

        /* Allocate a buffer for the signal */
        signal_buf = kmalloc(signal_size, GFP_KERNEL);
        if (!signal_buf) {
            unifi_net_data_free(priv, &bulkdata.d[0]);
            func_exit();
            return -ENOMEM;
        }

        /* Get the signal from the os_data_ptr */
        memcpy(signal_buf, bulkdata.d[0].os_data_ptr, signal_size);
        signal_buf[5] = (pcli->sender_id >> 8) & 0xff;

        if (signal_size < len) {
            /* Remove the signal from the os_data_ptr */
            bulkdata.d[0].data_length -= signal_size;
            bulkdata.d[0].os_data_ptr += signal_size;
        } else {
            bulkdata.d[0].data_length = 0;
            bulkdata.d[0].os_data_ptr = NULL;
        }

        /* Send the signal calling the function that uses the wire-formatted signals. */
        r = ul_send_signal_raw(priv, signal_buf, signal_size, &bulkdata);
        if (r < 0) {
            unifi_error(priv, "unifi_write: send failed (%d)\n", r);
            if (bulkdata.d[0].os_data_ptr != NULL) {
                unifi_net_data_free(priv, &bulkdata.d[0]);
            }
        }

        /* Free the signal buffer and return */
        kfree(signal_buf);
        return len;
    }

    buf = kmalloc(len, GFP_KERNEL);
    if (!buf) {
        return -ENOMEM;
    }

    /* Get the data from the client (SME or Unicli). */
    if (copy_from_user((void*)buf, p, len)) {
        unifi_error(priv, "copy from user failed\n");
        kfree(buf);
        return -EFAULT;
    }

    /*
     * In SME userspace build read() contains a SYS or MGT message.
     * Note that even though the SME sends one signal at a time, we can not
     * use unifi_net_data_malloc because in the early stages, before having
     * initialised the core, it will fail since the I/O block size is unknown.
     */
#ifdef CSR_SME_USERSPACE
    if (pcli->configuration & CLI_SME_USERSPACE) {
        CsrWifiRouterTransportRecv(priv, buf, len);
        kfree(buf);
        return len;
    }
#endif

    /* ul_send_signal_raw will  do a sanity check of len against signal content */

    /*
     * udi_send_signal_raw() and udi_send_signal_unpacked() return the number of bytes consumed.
     * A write call can pass multiple signal concatenated together.
     */
    bytes_written = 0;
    remaining = len;
    bufptr = buf;
    while (remaining > 0)
    {
        int r;

        /*
         * Set the SenderProcessId.
         * The SignalPrimitiveHeader is the first 3 16-bit words of the signal,
         * the SenderProcessId is bytes 4,5.
         * The MSB of the sender ID needs to be set to the client ID.
         * The LSB is controlled by the SME.
         */
        bufptr[5] = (pcli->sender_id >> 8) & 0xff;

        /* use the appropriate interface, depending on the clients' configuration */
        if (pcli->configuration & CLI_USING_WIRE_FORMAT) {
            unifi_trace(priv, UDBG1, "unifi_write: call udi_send_signal().\n");
            r = udi_send_signal_raw(priv, bufptr, remaining);
        } else {
            r = udi_send_signal_unpacked(priv, bufptr, remaining);
        }
        if (r < 0) {
            /* Set the return value to the error code */
            unifi_error(priv, "unifi_write: (udi or sme)_send_signal() returns %d\n", r);
            bytes_written = r;
            break;
        }
        bufptr += r;
        remaining -= r;
        bytes_written += r;
    }

    kfree(buf);

    func_exit_r(bytes_written);

    return bytes_written;
} /* unifi_write() */


static const char* build_type_to_string(unsigned char build_type)
{
    switch (build_type)
    {
    case UNIFI_BUILD_NME: return "NME";
    case UNIFI_BUILD_WEXT: return "WEXT";
    case UNIFI_BUILD_AP: return "AP";
    }
    return "unknown";
}


/*
 * ----------------------------------------------------------------
 *  unifi_ioctl
 *
 *      Ioctl handler for unifi driver.
 *
 * Arguments:
 *  inodep          Pointer to inode structure.
 *  filp            Pointer to file structure.
 *  cmd             Ioctl cmd passed by user.
 *  arg             Ioctl arg passed by user.
 *
 * Returns:
 *      0 on success, -ve error code on error.
 * ----------------------------------------------------------------
 */
static long
unifi_ioctl(struct file *filp, unsigned int cmd, unsigned long arg)
{
    ul_client_t *pcli = (ul_client_t*)filp->private_data;
    unifi_priv_t *priv;
    struct net_device *dev;
    int r = 0;
    int int_param, i;
    u8* buf;
    CsrResult csrResult;
#if (defined CSR_SUPPORT_SME)
    unifi_cfg_command_t cfg_cmd;
#if (defined CSR_SUPPORT_WEXT)
    CsrWifiSmeCoexConfig coex_config;
    unsigned char uchar_param;
    unsigned char varbind[MAX_VARBIND_LENGTH];
    int vblen;
#endif
#endif
    unifi_putest_command_t putest_cmd;

    priv = uf_find_instance(pcli->instance);
    if (!priv) {
        unifi_error(priv, "ioctl error: unknown instance=%d\n", pcli->instance);
        r = -ENODEV;
        goto out;
    }
    unifi_trace(priv, UDBG5, "unifi_ioctl: cmd=0x%X, arg=0x%lX\n", cmd, arg);

    switch (cmd) {

      case UNIFI_GET_UDI_ENABLE:
        unifi_trace(priv, UDBG4, "UniFi Get UDI Enable\n");

        down(&priv->udi_logging_mutex);
        int_param = (priv->logging_client == NULL) ? 0 : 1;
        up(&priv->udi_logging_mutex);

        if (put_user(int_param, (int*)arg))
        {
            unifi_error(priv, "UNIFI_GET_UDI_ENABLE: Failed to copy to user\n");
            r = -EFAULT;
            goto out;
        }
        break;

      case UNIFI_SET_UDI_ENABLE:
        unifi_trace(priv, UDBG4, "UniFi Set UDI Enable\n");
        if (get_user(int_param, (int*)arg))
        {
            unifi_error(priv, "UNIFI_SET_UDI_ENABLE: Failed to copy from user\n");
            r = -EFAULT;
            goto out;
        }

#ifdef CSR_WIFI_HIP_DEBUG_OFFLINE
        if (log_hip_signals) {
            unifi_error(priv, "omnicli cannot be used when log_hip_signals is used\n");
            r = -EFAULT;
            goto out;
        }
#endif

        down(&priv->udi_logging_mutex);
        if (int_param) {
            pcli->event_hook = udi_log_event;
            unifi_set_udi_hook(priv->card, logging_handler);
            /* Log all signals by default */
            for (i = 0; i < SIG_FILTER_SIZE; i++) {
                pcli->signal_filter[i] = 0xFFFF;
            }
            priv->logging_client = pcli;

        } else {
            priv->logging_client = NULL;
            pcli->event_hook = NULL;
        }
        up(&priv->udi_logging_mutex);

        break;

      case UNIFI_SET_MIB:
        unifi_trace(priv, UDBG4, "UniFi Set MIB\n");
#if (defined CSR_SUPPORT_SME) && (defined CSR_SUPPORT_WEXT)
        /* Read first 2 bytes and check length */
        if (copy_from_user((void*)varbind, (void*)arg, 2)) {
            unifi_error(priv,
                        "UNIFI_SET_MIB: Failed to copy in varbind header\n");
            r = -EFAULT;
            goto out;
        }
        vblen = varbind[1];
        if ((vblen + 2) > MAX_VARBIND_LENGTH) {
            unifi_error(priv,
                        "UNIFI_SET_MIB: Varbind too long (%d, limit %d)\n",
                        (vblen+2), MAX_VARBIND_LENGTH);
            r = -EINVAL;
            goto out;
        }
        /* Read rest of varbind */
        if (copy_from_user((void*)(varbind+2), (void*)(arg+2), vblen)) {
            unifi_error(priv, "UNIFI_SET_MIB: Failed to copy in varbind\n");
            r = -EFAULT;
            goto out;
        }

        /* send to SME */
        vblen += 2;
        r = sme_mgt_mib_set(priv, varbind, vblen);
        if (r) {
            goto out;
        }
#else
        unifi_notice(priv, "UNIFI_SET_MIB: Unsupported.\n");
#endif /* CSR_SUPPORT_WEXT */
        break;

      case UNIFI_GET_MIB:
        unifi_trace(priv, UDBG4, "UniFi Get MIB\n");
#if (defined CSR_SUPPORT_SME) && (defined CSR_SUPPORT_WEXT)
        /* Read first 2 bytes and check length */
        if (copy_from_user((void*)varbind, (void*)arg, 2)) {
            unifi_error(priv, "UNIFI_GET_MIB: Failed to copy in varbind header\n");
            r = -EFAULT;
            goto out;
        }
        vblen = varbind[1];
        if ((vblen+2) > MAX_VARBIND_LENGTH) {
            unifi_error(priv, "UNIFI_GET_MIB: Varbind too long (%d, limit %d)\n",
                        (vblen+2), MAX_VARBIND_LENGTH);
            r = -EINVAL;
            goto out;
        }
        /* Read rest of varbind */
        if (copy_from_user((void*)(varbind+2), (void*)(arg+2), vblen)) {
            unifi_error(priv, "UNIFI_GET_MIB: Failed to copy in varbind\n");
            r = -EFAULT;
            goto out;
        }

        vblen += 2;
        r = sme_mgt_mib_get(priv, varbind, &vblen);
        if (r) {
            goto out;
        }
        /* copy out varbind */
        if (vblen > MAX_VARBIND_LENGTH) {
            unifi_error(priv,
                        "UNIFI_GET_MIB: Varbind result too long (%d, limit %d)\n",
                        vblen, MAX_VARBIND_LENGTH);
            r = -EINVAL;
            goto out;
        }
        if (copy_to_user((void*)arg, varbind, vblen)) {
            r = -EFAULT;
            goto out;
        }
#else
        unifi_notice(priv, "UNIFI_GET_MIB: Unsupported.\n");
#endif /* CSR_SUPPORT_WEXT */
        break;

      case UNIFI_CFG:
#if (defined CSR_SUPPORT_SME)
        if (get_user(cfg_cmd, (unifi_cfg_command_t*)arg))
        {
            unifi_error(priv, "UNIFI_CFG: Failed to get the command\n");
            r = -EFAULT;
            goto out;
        }

        unifi_trace(priv, UDBG1, "UNIFI_CFG: Command is %d (t=%u) sz=%d\n",
                    cfg_cmd, jiffies_to_msecs(jiffies), sizeof(unifi_cfg_command_t));
        switch (cfg_cmd) {
          case UNIFI_CFG_POWER:
            r = unifi_cfg_power(priv, (unsigned char*)arg);
            break;
          case UNIFI_CFG_POWERSAVE:
            r = unifi_cfg_power_save(priv, (unsigned char*)arg);
            break;
          case UNIFI_CFG_POWERSUPPLY:
            r = unifi_cfg_power_supply(priv, (unsigned char*)arg);
            break;
          case UNIFI_CFG_FILTER:
            r = unifi_cfg_packet_filters(priv, (unsigned char*)arg);
            break;
          case UNIFI_CFG_GET:
            r = unifi_cfg_get_info(priv, (unsigned char*)arg);
            break;
          case UNIFI_CFG_WMM_QOSINFO:
            r = unifi_cfg_wmm_qos_info(priv, (unsigned char*)arg);
            break;
          case UNIFI_CFG_WMM_ADDTS:
            r = unifi_cfg_wmm_addts(priv, (unsigned char*)arg);
            break;
          case UNIFI_CFG_WMM_DELTS:
            r = unifi_cfg_wmm_delts(priv, (unsigned char*)arg);
            break;
          case UNIFI_CFG_STRICT_DRAFT_N:
            r = unifi_cfg_strict_draft_n(priv, (unsigned char*)arg);
            break;
          case UNIFI_CFG_ENABLE_OKC:
            r = unifi_cfg_enable_okc(priv, (unsigned char*)arg);
            break;
#ifdef CSR_SUPPORT_SME
          case UNIFI_CFG_CORE_DUMP:
            CsrWifiRouterCtrlWifiOffIndSend(priv->CSR_WIFI_SME_IFACEQUEUE,0,CSR_WIFI_SME_CONTROL_INDICATION_ERROR);
            unifi_trace(priv, UDBG2, "UNIFI_CFG_CORE_DUMP: sent wifi off indication\n");
            break;
#endif
#ifdef CSR_SUPPORT_WEXT_AP
          case UNIFI_CFG_SET_AP_CONFIG:
            r= unifi_cfg_set_ap_config(priv,(unsigned char*)arg);
            break;
#endif
          default:
            unifi_error(priv, "UNIFI_CFG: Unknown Command (%d)\n", cfg_cmd);
            r = -EINVAL;
            goto out;
        }
#endif

        break;

      case UNIFI_PUTEST:
        if (get_user(putest_cmd, (unifi_putest_command_t*)arg))
        {
            unifi_error(priv, "UNIFI_PUTEST: Failed to get the command\n");
            r = -EFAULT;
            goto out;
        }

        unifi_trace(priv, UDBG1, "UNIFI_PUTEST: Command is %s\n",
                    trace_putest_cmdid(putest_cmd));
        switch (putest_cmd) {
          case UNIFI_PUTEST_START:
            r = unifi_putest_start(priv, (unsigned char*)arg);
            break;
          case UNIFI_PUTEST_STOP:
            r = unifi_putest_stop(priv, (unsigned char*)arg);
            break;
          case UNIFI_PUTEST_SET_SDIO_CLOCK:
            r = unifi_putest_set_sdio_clock(priv, (unsigned char*)arg);
            break;
          case UNIFI_PUTEST_CMD52_READ:
            r = unifi_putest_cmd52_read(priv, (unsigned char*)arg);
            break;
          case UNIFI_PUTEST_CMD52_BLOCK_READ:
            r = unifi_putest_cmd52_block_read(priv, (unsigned char*)arg);
            break;
          case UNIFI_PUTEST_CMD52_WRITE:
            r = unifi_putest_cmd52_write(priv, (unsigned char*)arg);
            break;
          case UNIFI_PUTEST_DL_FW:
            r = unifi_putest_dl_fw(priv, (unsigned char*)arg);
            break;
          case UNIFI_PUTEST_DL_FW_BUFF:
            r = unifi_putest_dl_fw_buff(priv, (unsigned char*)arg);
            break;
          case UNIFI_PUTEST_COREDUMP_PREPARE:
            r = unifi_putest_coredump_prepare(priv, (unsigned char*)arg);
            break;
          case UNIFI_PUTEST_GP_READ16:
            r = unifi_putest_gp_read16(priv, (unsigned char*)arg);
            break;
          case UNIFI_PUTEST_GP_WRITE16:
            r = unifi_putest_gp_write16(priv, (unsigned char*)arg);
            break;
          default:
            unifi_error(priv, "UNIFI_PUTEST: Unknown Command (%d)\n", putest_cmd);
            r = -EINVAL;
            goto out;
        }

        break;
      case UNIFI_BUILD_TYPE:
        unifi_trace(priv, UDBG2, "UNIFI_BUILD_TYPE userspace=%s\n", build_type_to_string(*(unsigned char*)arg));
#ifndef CSR_SUPPORT_WEXT_AP
        if (UNIFI_BUILD_AP == *(unsigned char*)arg)
        {
            unifi_error(priv, "Userspace has AP support, which is incompatible\n");
        }
#endif

#ifndef CSR_SUPPORT_WEXT
        if (UNIFI_BUILD_WEXT == *(unsigned char*)arg)
        {
            unifi_error(priv, "Userspace has WEXT support, which is incompatible\n");
        }
#endif
        break;
      case UNIFI_INIT_HW:
        unifi_trace(priv, UDBG2, "UNIFI_INIT_HW.\n");
        priv->init_progress = UNIFI_INIT_NONE;

#if defined(CSR_SUPPORT_WEXT) || defined (CSR_NATIVE_LINUX)
        /* At this point we are ready to start the SME. */
        r = sme_mgt_wifi_on(priv);
        if (r) {
            goto out;
        }
#endif

        break;

      case UNIFI_INIT_NETDEV:
        {
            /* get the proper interfaceTagId */
            u16 interfaceTag=0;
            netInterface_priv_t *interfacePriv = priv->interfacePriv[interfaceTag];

            dev = priv->netdev[interfaceTag];
            unifi_trace(priv, UDBG2, "UNIFI_INIT_NETDEV.\n");

            if (copy_from_user((void*)dev->dev_addr, (void*)arg, 6)) {
                r = -EFAULT;
                goto out;
            }

            /* Attach the network device to the stack */
            if (!interfacePriv->netdev_registered)
            {
                r = uf_register_netdev(priv,interfaceTag);
                if (r) {
                    unifi_error(priv, "Failed to register the network device.\n");
                    goto out;
                }
            }

            /* Apply scheduled interrupt mode, if requested by module param */
            if (run_bh_once != -1) {
                unifi_set_interrupt_mode(priv->card, (u32)run_bh_once);
            }

            priv->init_progress = UNIFI_INIT_COMPLETED;

            /* Firmware initialisation is complete, so let the SDIO bus
             * clock be raised when convienent to the core.
             */
            unifi_request_max_sdio_clock(priv->card);

#ifdef CSR_SUPPORT_WEXT
            /* Notify the Android wpa_supplicant that we are ready */
            wext_send_started_event(priv);
#endif

            unifi_info(priv, "UniFi ready\n");

#ifdef ANDROID_BUILD
            /* Release the wakelock */
            unifi_trace(priv, UDBG1, "netdev_init: release wake lock\n");
            wake_unlock(&unifi_sdio_wake_lock);
#endif
#ifdef CSR_NATIVE_SOFTMAC /* For softmac dev, force-enable the network interface rather than wait for a connected-ind */
            {
                struct net_device *dev = priv->netdev[interfaceTag];
#ifdef CSR_SUPPORT_WEXT
                interfacePriv->wait_netdev_change = TRUE;
#endif
                netif_carrier_on(dev);
            }
#endif
        }
        break;
      case UNIFI_GET_INIT_STATUS:
        unifi_trace(priv, UDBG2, "UNIFI_GET_INIT_STATUS.\n");
        if (put_user(priv->init_progress, (int*)arg))
        {
            printk(KERN_ERR "UNIFI_GET_INIT_STATUS: Failed to copy to user\n");
            r = -EFAULT;
            goto out;
        }
        break;

      case UNIFI_KICK:
        unifi_trace(priv, UDBG4, "Kick UniFi\n");
        unifi_sdio_interrupt_handler(priv->card);
        break;

      case UNIFI_SET_DEBUG:
        unifi_debug = arg;
        unifi_trace(priv, UDBG4, "unifi_debug set to %d\n", unifi_debug);
        break;

      case UNIFI_SET_TRACE:
        /* no longer supported */
        r = -EINVAL;
        break;


      case UNIFI_SET_UDI_LOG_MASK:
        {
            unifiio_filter_t udi_filter;
            uint16_t *sig_ids_addr;
#define UF_MAX_SIG_IDS  128     /* Impose a sensible limit */

            if (copy_from_user((void*)(&udi_filter), (void*)arg, sizeof(udi_filter))) {
                r = -EFAULT;
                goto out;
            }
            if ((udi_filter.action < UfSigFil_AllOn) ||
                (udi_filter.action > UfSigFil_SelectOff))
            {
                printk(KERN_WARNING
                       "UNIFI_SET_UDI_LOG_MASK: Bad action value: %d\n",
                       udi_filter.action);
                r = -EINVAL;
                goto out;
            }
            /* No signal list for "All" actions */
            if ((udi_filter.action == UfSigFil_AllOn) ||
                (udi_filter.action == UfSigFil_AllOff))
            {
                udi_filter.num_sig_ids = 0;
            }

            if (udi_filter.num_sig_ids > UF_MAX_SIG_IDS) {
                printk(KERN_WARNING
                       "UNIFI_SET_UDI_LOG_MASK: too many signal ids (%d, max %d)\n",
                       udi_filter.num_sig_ids, UF_MAX_SIG_IDS);
                r = -EINVAL;
                goto out;
            }

            /* Copy in signal id list if given */
            if (udi_filter.num_sig_ids > 0) {
                /* Preserve userspace address of sig_ids array */
                sig_ids_addr = udi_filter.sig_ids;
                /* Allocate kernel memory for sig_ids and copy to it */
                udi_filter.sig_ids =
                    kmalloc(udi_filter.num_sig_ids * sizeof(uint16_t), GFP_KERNEL);
                if (!udi_filter.sig_ids) {
                    r = -ENOMEM;
                    goto out;
                }
                if (copy_from_user((void*)udi_filter.sig_ids,
                                   (void*)sig_ids_addr,
                                   udi_filter.num_sig_ids * sizeof(uint16_t)))
                {
                    kfree(udi_filter.sig_ids);
                    r = -EFAULT;
                    goto out;
                }
            }

            udi_set_log_filter(pcli, &udi_filter);

            if (udi_filter.num_sig_ids > 0) {
                kfree(udi_filter.sig_ids);
            }
        }
        break;

      case UNIFI_SET_AMP_ENABLE:
        unifi_trace(priv, UDBG4, "UniFi Set AMP Enable\n");
        if (get_user(int_param, (int*)arg))
        {
            unifi_error(priv, "UNIFI_SET_AMP_ENABLE: Failed to copy from user\n");
            r = -EFAULT;
            goto out;
        }

        if (int_param) {
            priv->amp_client = pcli;
        } else {
            priv->amp_client = NULL;
        }

        int_param = 0;
        buf = (u8*)&int_param;
        buf[0] = UNIFI_SOFT_COMMAND_Q_LENGTH - 1;
        buf[1] = UNIFI_SOFT_TRAFFIC_Q_LENGTH - 1;
        if (copy_to_user((void*)arg, &int_param, sizeof(int))) {
            r = -EFAULT;
            goto out;
        }
        break;

      case UNIFI_SET_UDI_SNAP_MASK:
        {
            unifiio_snap_filter_t snap_filter;

            if (copy_from_user((void*)(&snap_filter), (void*)arg, sizeof(snap_filter))) {
                r = -EFAULT;
                goto out;
            }

            if (pcli->snap_filter.count) {
                pcli->snap_filter.count = 0;
                kfree(pcli->snap_filter.protocols);
            }

            if (snap_filter.count == 0) {
                break;
            }

            pcli->snap_filter.protocols = kmalloc(snap_filter.count * sizeof(u16), GFP_KERNEL);
            if (!pcli->snap_filter.protocols) {
                r = -ENOMEM;
                goto out;
            }
            if (copy_from_user((void*)pcli->snap_filter.protocols,
                               (void*)snap_filter.protocols,
                               snap_filter.count * sizeof(u16)))
            {
                kfree(pcli->snap_filter.protocols);
                r = -EFAULT;
                goto out;
            }

            pcli->snap_filter.count = snap_filter.count;

        }
        break;

      case UNIFI_SME_PRESENT:
        {
            u8 ind;
            unifi_trace(priv, UDBG4, "UniFi SME Present IOCTL.\n");
            if (copy_from_user((void*)(&int_param), (void*)arg, sizeof(int)))
            {
                printk(KERN_ERR "UNIFI_SME_PRESENT: Failed to copy from user\n");
                r = -EFAULT;
                goto out;
            }

            priv->sme_is_present = int_param;
            if (priv->sme_is_present == 1) {
                ind = CONFIG_SME_PRESENT;
            } else {
                ind = CONFIG_SME_NOT_PRESENT;
            }
            /* Send an indication to the helper app. */
            ul_log_config_ind(priv, &ind, sizeof(u8));
        }
        break;

      case UNIFI_CFG_PERIOD_TRAFFIC:
      {
#if (defined CSR_SUPPORT_SME) && (defined CSR_SUPPORT_WEXT)
          CsrWifiSmeCoexConfig coexConfig;
#endif /* CSR_SUPPORT_SME && CSR_SUPPORT_WEXT */
        unifi_trace(priv, UDBG4, "UniFi Configure Periodic Traffic.\n");
#if (defined CSR_SUPPORT_SME) && (defined CSR_SUPPORT_WEXT)
        if (copy_from_user((void*)(&uchar_param), (void*)arg, sizeof(unsigned char))) {
            unifi_error(priv, "UNIFI_CFG_PERIOD_TRAFFIC: Failed to copy from user\n");
            r = -EFAULT;
            goto out;
        }

        if (uchar_param == 0) {
            r = sme_mgt_coex_config_get(priv, &coexConfig);
            if (r) {
                unifi_error(priv, "UNIFI_CFG_PERIOD_TRAFFIC: Get unifi_CoexInfoValue failed.\n");
                goto out;
            }
            if (copy_to_user((void*)(arg + 1),
                             (void*)&coexConfig,
                             sizeof(CsrWifiSmeCoexConfig))) {
                r = -EFAULT;
                goto out;
            }
            goto out;
        }

        if (copy_from_user((void*)(&coex_config), (void*)(arg + 1), sizeof(CsrWifiSmeCoexConfig)))
        {
            unifi_error(priv, "UNIFI_CFG_PERIOD_TRAFFIC: Failed to copy from user\n");
            r = -EFAULT;
            goto out;
        }

        coexConfig = coex_config;
        r = sme_mgt_coex_config_set(priv, &coexConfig);
        if (r) {
            unifi_error(priv, "UNIFI_CFG_PERIOD_TRAFFIC: Set unifi_CoexInfoValue failed.\n");
            goto out;
        }

#endif /* CSR_SUPPORT_SME && CSR_SUPPORT_WEXT */
        break;
      }
      case UNIFI_CFG_UAPSD_TRAFFIC:
        unifi_trace(priv, UDBG4, "UniFi Configure U-APSD Mask.\n");
#if (defined CSR_SUPPORT_SME) && (defined CSR_SUPPORT_WEXT)
        if (copy_from_user((void*)(&uchar_param), (void*)arg, sizeof(unsigned char))) {
            unifi_error(priv, "UNIFI_CFG_UAPSD_TRAFFIC: Failed to copy from user\n");
            r = -EFAULT;
            goto out;
        }
        unifi_trace(priv, UDBG4, "New U-APSD Mask: 0x%x\n", uchar_param);
#endif /* CSR_SUPPORT_SME && CSR_SUPPORT_WEXT */
        break;

#ifndef UNIFI_DISABLE_COREDUMP
      case UNIFI_COREDUMP_GET_REG:
        unifi_trace(priv, UDBG4, "Mini-coredump data request\n");
        {
            unifiio_coredump_req_t dump_req;    /* Public OS layer structure */
            unifi_coredump_req_t priv_req;      /* Private HIP structure */

            if (copy_from_user((void*)(&dump_req), (void*)arg, sizeof(dump_req))) {
                r = -EFAULT;
                goto out;
            }
            memset(&priv_req, 0, sizeof(priv_req));
            priv_req.index = dump_req.index;
            priv_req.offset = dump_req.offset;

            /* Convert OS-layer's XAP memory space ID to HIP's ID in case they differ */
            switch (dump_req.space) {
                case UNIFIIO_COREDUMP_MAC_REG: priv_req.space = UNIFI_COREDUMP_MAC_REG; break;
                case UNIFIIO_COREDUMP_PHY_REG: priv_req.space = UNIFI_COREDUMP_PHY_REG; break;
                case UNIFIIO_COREDUMP_SH_DMEM: priv_req.space = UNIFI_COREDUMP_SH_DMEM; break;
                case UNIFIIO_COREDUMP_MAC_DMEM: priv_req.space = UNIFI_COREDUMP_MAC_DMEM; break;
                case UNIFIIO_COREDUMP_PHY_DMEM: priv_req.space = UNIFI_COREDUMP_PHY_DMEM; break;
                case UNIFIIO_COREDUMP_TRIGGER_MAGIC: priv_req.space = UNIFI_COREDUMP_TRIGGER_MAGIC; break;
                default:
                  r = -EINVAL;
                  goto out;
            }

            if (priv_req.space == UNIFI_COREDUMP_TRIGGER_MAGIC) {
                /* Force a coredump grab now */
                unifi_trace(priv, UDBG2, "UNIFI_COREDUMP_GET_REG: Force capture\n");
                csrResult = unifi_coredump_capture(priv->card, &priv_req);
                r = CsrHipResultToStatus(csrResult);
                unifi_trace(priv, UDBG5, "UNIFI_COREDUMP_GET_REG: status %d\n", r);
            } else {
                /* Retrieve the appropriate register entry */
                csrResult = unifi_coredump_get_value(priv->card, &priv_req);
                r = CsrHipResultToStatus(csrResult);
                if (r) {
                    unifi_trace(priv, UDBG5, "UNIFI_COREDUMP_GET_REG: Status %d\n", r);
                    goto out;
                }
                /* Update the OS-layer structure with values returned in the private */
                dump_req.value = priv_req.value;
                dump_req.timestamp = priv_req.timestamp;
                dump_req.requestor = priv_req.requestor;
                dump_req.serial = priv_req.serial;
                dump_req.chip_ver = priv_req.chip_ver;
                dump_req.fw_ver = priv_req.fw_ver;
                dump_req.drv_build = 0;

                unifi_trace(priv, UDBG6,
                            "Dump: %d (seq %d): V:0x%04x (%d) @0x%02x:%04x = 0x%04x\n",
                            dump_req.index, dump_req.serial,
                            dump_req.chip_ver, dump_req.drv_build,
                            dump_req.space, dump_req.offset, dump_req.value);
            }
            if (copy_to_user((void*)arg, (void*)&dump_req, sizeof(dump_req))) {
                r = -EFAULT;
                goto out;
            }
        }
        break;
#endif
      default:
        r = -EINVAL;
    }

out:
    return (long)r;
} /* unifi_ioctl() */



static unsigned int
unifi_poll(struct file *filp, poll_table *wait)
{
    ul_client_t *pcli = (ul_client_t*)filp->private_data;
    unsigned int mask = 0;
    int ready;

    func_enter();

    ready = !list_empty(&pcli->udi_log);

    poll_wait(filp, &pcli->udi_wq, wait);

    if (ready) {
        mask |= POLLIN | POLLRDNORM;    /* readable */
    }

    func_exit();

    return mask;
} /* unifi_poll() */



/*
 * ---------------------------------------------------------------------------
 *  udi_set_log_filter
 *
 *      Configure the bit mask that determines which signal primitives are
 *      passed to the logging process.
 *
 *  Arguments:
 *      pcli            Pointer to the client to configure.
 *      udi_filter      Pointer to a unifiio_filter_t containing instructions.
 *
 *  Returns:
 *      None.
 *
 *  Notes:
 *      SigGetFilterPos() returns a 32-bit value that contains an index and a
 *      mask for accessing a signal_filter array. The top 16 bits specify an
 *      index into a signal_filter, the bottom 16 bits specify a mask to
 *      apply.
 * ---------------------------------------------------------------------------
 */
static void
udi_set_log_filter(ul_client_t *pcli, unifiio_filter_t *udi_filter)
{
    u32 filter_pos;
    int i;

    if (udi_filter->action == UfSigFil_AllOn)
    {
        for (i = 0; i < SIG_FILTER_SIZE; i++) {
            pcli->signal_filter[i] = 0xFFFF;
        }
    }
    else if (udi_filter->action == UfSigFil_AllOff)
    {
        for (i = 0; i < SIG_FILTER_SIZE; i++) {
            pcli->signal_filter[i] = 0;
        }
    }
    else if (udi_filter->action == UfSigFil_SelectOn)
    {
        for (i = 0; i < udi_filter->num_sig_ids; i++) {
            filter_pos = SigGetFilterPos(udi_filter->sig_ids[i]);
            if (filter_pos == 0xFFFFFFFF)
            {
                printk(KERN_WARNING
                       "Unrecognised signal id (0x%X) specifed in logging filter\n",
                       udi_filter->sig_ids[i]);
            } else {
                pcli->signal_filter[filter_pos >> 16] |= (filter_pos & 0xFFFF);
            }
        }
    }
    else if (udi_filter->action == UfSigFil_SelectOff)
    {
        for (i = 0; i < udi_filter->num_sig_ids; i++) {
            filter_pos = SigGetFilterPos(udi_filter->sig_ids[i]);
            if (filter_pos == 0xFFFFFFFF)
            {
                printk(KERN_WARNING
                       "Unrecognised signal id (0x%X) specifed in logging filter\n",
                       udi_filter->sig_ids[i]);
            } else {
                pcli->signal_filter[filter_pos >> 16] &= ~(filter_pos & 0xFFFF);
            }
        }
    }

} /* udi_set_log_filter() */


/*
 * ---------------------------------------------------------------------------
 *  udi_log_event
 *
 *      Callback function to be registered as the UDI hook callback.
 *      Copies the signal content into a new udi_log_t struct and adds
 *      it to the read queue for this UDI client.
 *
 *  Arguments:
 *      pcli            A pointer to the client instance.
 *      signal          Pointer to the received signal.
 *      signal_len      Size of the signal structure in bytes.
 *      bulkdata        Pointers to any associated bulk data.
 *      dir             Direction of the signal. Zero means from host,
 *                      non-zero means to host.
 *
 *  Returns:
 *      None.
 * ---------------------------------------------------------------------------
 */
void
udi_log_event(ul_client_t *pcli,
              const u8 *signal, int signal_len,
              const bulk_data_param_t *bulkdata,
              int dir)
{
    udi_log_t *logptr;
    u8 *p;
    int i;
    int total_len;
    udi_msg_t *msgptr;
    u32 filter_pos;
#ifdef OMNICLI_LINUX_EXTRA_LOG
    static volatile unsigned int printk_cpu = UINT_MAX;
    unsigned long long t;
    unsigned long nanosec_rem;
    unsigned long n_1000;
#endif

    func_enter();

    /* Just a sanity check */
    if ((signal == NULL) || (signal_len <= 0)) {
        return;
    }

#ifdef CSR_WIFI_HIP_DEBUG_OFFLINE
    /* When HIP offline signal logging is enabled, omnicli cannot run */
    if (log_hip_signals)
    {
        /* Add timestamp */
        if (log_hip_signals & UNIFI_LOG_HIP_SIGNALS_FILTER_TIMESTAMP)
        {
            int timestamp = jiffies_to_msecs(jiffies);
            unifi_debug_log_to_buf("T:");
            unifi_debug_log_to_buf("%04X%04X ", *(((u16*)&timestamp) + 1),
                                   *(u16*)&timestamp);
        }

        /* Add signal */
        unifi_debug_log_to_buf("S%s:%04X R:%04X D:%04X ",
                               dir ? "T" : "F",
                               *(u16*)signal,
                               *(u16*)(signal + 2),
                               *(u16*)(signal + 4));
        unifi_debug_hex_to_buf(signal + 6, signal_len - 6);

        /* Add bulk data (assume 1 bulk data per signal) */
        if ((log_hip_signals & UNIFI_LOG_HIP_SIGNALS_FILTER_BULKDATA) &&
            (bulkdata->d[0].data_length > 0))
        {
            unifi_debug_log_to_buf("\nD:");
            unifi_debug_hex_to_buf(bulkdata->d[0].os_data_ptr, bulkdata->d[0].data_length);
        }
        unifi_debug_log_to_buf("\n");

        return;
    }
#endif

#ifdef CSR_NATIVE_LINUX
    uf_native_process_udi_signal(pcli, signal, signal_len, bulkdata, dir);
#endif

    /*
     * Apply the logging filter - only report signals that have their
     * bit set in the filter mask.
     */
    filter_pos = SigGetFilterPos(GET_SIGNAL_ID(signal));

    if ((filter_pos != 0xFFFFFFFF) &&
        ((pcli->signal_filter[filter_pos >> 16] & (filter_pos & 0xFFFF)) == 0))
    {
        /* Signal is not wanted by client */
        return;
    }


    /* Calculate the buffer we need to store signal plus bulk data */
    total_len = signal_len;
    for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; i++) {
        total_len += bulkdata->d[i].data_length;
    }

    /* Allocate log structure plus actual signal. */
    logptr = (udi_log_t *)kmalloc(sizeof(udi_log_t) + total_len, GFP_KERNEL);

    if (logptr == NULL) {
        printk(KERN_ERR
               "Failed to allocate %lu bytes for a UDI log record\n",
               (long unsigned int)(sizeof(udi_log_t) + total_len));
        return;
    }

    /* Fill in udi_log struct */
    INIT_LIST_HEAD(&logptr->q);
    msgptr = &logptr->msg;
    msgptr->length = sizeof(udi_msg_t) + total_len;
#ifdef OMNICLI_LINUX_EXTRA_LOG
    t = cpu_clock(printk_cpu);
    nanosec_rem = do_div(t, 1000000000);
    n_1000 = nanosec_rem/1000;
    msgptr->timestamp = (t <<10 ) | ((unsigned long)(n_1000 >> 10) & 0x3ff);
#else
    msgptr->timestamp = jiffies_to_msecs(jiffies);
#endif
    msgptr->direction = dir;
    msgptr->signal_length = signal_len;

    /* Copy signal and bulk data to the log */
    p = (u8 *)(msgptr + 1);
    memcpy(p, signal, signal_len);
    p += signal_len;

    /* Append any bulk data */
    for (i = 0; i < UNIFI_MAX_DATA_REFERENCES; i++) {
        int len = bulkdata->d[i].data_length;

        /*
         * Len here might not be the same as the length in the bulk data slot.
         * The slot length will always be even, but len could be odd.
         */
        if (len > 0) {
            if (bulkdata->d[i].os_data_ptr) {
                memcpy(p, bulkdata->d[i].os_data_ptr, len);
            } else {
                memset(p, 0, len);
            }
            p += len;
        }
    }

    /* Add to tail of log queue */
    if (down_interruptible(&pcli->udi_sem)) {
        printk(KERN_WARNING "udi_log_event_q: Failed to get udi sem\n");
        kfree(logptr);
        func_exit();
        return;
    }
    list_add_tail(&logptr->q, &pcli->udi_log);
    up(&pcli->udi_sem);

    /* Wake any waiting user process */
    wake_up_interruptible(&pcli->udi_wq);

    func_exit();
} /* udi_log_event() */

#ifdef CSR_SME_USERSPACE
int
uf_sme_queue_message(unifi_priv_t *priv, u8 *buffer, int length)
{
    udi_log_t *logptr;
    udi_msg_t *msgptr;
    u8 *p;

    func_enter();

    /* Just a sanity check */
    if ((buffer == NULL) || (length <= 0)) {
        return -EINVAL;
    }

    /* Allocate log structure plus actual signal. */
    logptr = (udi_log_t *)kmalloc(sizeof(udi_log_t) + length, GFP_ATOMIC);
    if (logptr == NULL) {
        unifi_error(priv, "Failed to allocate %d bytes for an SME message\n",
                    sizeof(udi_log_t) + length);
                    kfree(buffer);
                    return -ENOMEM;
    }

    /* Fill in udi_log struct */
    INIT_LIST_HEAD(&logptr->q);
    msgptr = &logptr->msg;
    msgptr->length = sizeof(udi_msg_t) + length;
    msgptr->signal_length = length;

    /* Copy signal and bulk data to the log */
    p = (u8 *)(msgptr + 1);
    memcpy(p, buffer, length);

    /* Add to tail of log queue */
    down(&udi_mutex);
    if (priv->sme_cli == NULL) {
        kfree(logptr);
        kfree(buffer);
        up(&udi_mutex);
        unifi_info(priv, "Message for the SME dropped, SME has gone away\n");
        return 0;
    }

    down(&priv->sme_cli->udi_sem);
    list_add_tail(&logptr->q, &priv->sme_cli->udi_log);
    up(&priv->sme_cli->udi_sem);

    /* Wake any waiting user process */
    wake_up_interruptible(&priv->sme_cli->udi_wq);
    up(&udi_mutex);

    /* It is our responsibility to free the buffer allocated in build_packed_*() */
    kfree(buffer);

    func_exit();

    return 0;

} /* uf_sme_queue_message() */
#endif

/*
 ****************************************************************************
 *
 *      Driver instantiation
 *
 ****************************************************************************
 */
static struct file_operations unifi_fops = {
    .owner      = THIS_MODULE,
    .open       = unifi_open,
    .release    = unifi_release,
    .read       = unifi_read,
    .write      = unifi_write,
    .unlocked_ioctl = unifi_ioctl,
    .poll       = unifi_poll,
};

static dev_t unifi_first_devno;
static struct class *unifi_class;


int uf_create_device_nodes(unifi_priv_t *priv, int bus_id)
{
    dev_t devno;
    int r;

    cdev_init(&priv->unifi_cdev, &unifi_fops);

    /* cdev_init() should set the cdev owner, but it does not */
    priv->unifi_cdev.owner = THIS_MODULE;

    devno = MKDEV(MAJOR(unifi_first_devno),
                  MINOR(unifi_first_devno) + (bus_id * 2));
    r = cdev_add(&priv->unifi_cdev, devno, 1);
    if (r) {
        return r;
    }

#ifdef SDIO_EXPORTS_STRUCT_DEVICE
    if (!device_create(unifi_class, priv->unifi_device,
                       devno, priv, "unifi%d", bus_id)) {
#else
    priv->unifi_device = device_create(unifi_class, NULL,
                                       devno, priv, "unifi%d", bus_id);
    if (priv->unifi_device == NULL) {
#endif /* SDIO_EXPORTS_STRUCT_DEVICE */

        cdev_del(&priv->unifi_cdev);
        return -EINVAL;
    }

    cdev_init(&priv->unifiudi_cdev, &unifi_fops);

    /* cdev_init() should set the cdev owner, but it does not */
    priv->unifiudi_cdev.owner = THIS_MODULE;

    devno = MKDEV(MAJOR(unifi_first_devno),
                  MINOR(unifi_first_devno) + (bus_id * 2) + 1);
    r = cdev_add(&priv->unifiudi_cdev, devno, 1);
    if (r) {
        device_destroy(unifi_class, priv->unifi_cdev.dev);
        cdev_del(&priv->unifi_cdev);
        return r;
    }

    if (!device_create(unifi_class,
#ifdef SDIO_EXPORTS_STRUCT_DEVICE
                       priv->unifi_device,
#else
                       NULL,
#endif /* SDIO_EXPORTS_STRUCT_DEVICE */
                       devno, priv, "unifiudi%d", bus_id)) {
        device_destroy(unifi_class, priv->unifi_cdev.dev);
        cdev_del(&priv->unifiudi_cdev);
        cdev_del(&priv->unifi_cdev);
        return -EINVAL;
    }

    return 0;
}


void uf_destroy_device_nodes(unifi_priv_t *priv)
{
    device_destroy(unifi_class, priv->unifiudi_cdev.dev);
    device_destroy(unifi_class, priv->unifi_cdev.dev);
    cdev_del(&priv->unifiudi_cdev);
    cdev_del(&priv->unifi_cdev);
}



/*
 * ----------------------------------------------------------------
 *  uf_create_debug_device
 *
 *      Allocates device numbers for unifi character device nodes
 *      and creates a unifi class in sysfs
 *
 * Arguments:
 *  fops          Pointer to the char device operations structure.
 *
 * Returns:
 *      0 on success, -ve error code on error.
 * ----------------------------------------------------------------
 */
static int
uf_create_debug_device(struct file_operations *fops)
{
    int ret;

    /* Allocate two device numbers for each device. */
    ret = alloc_chrdev_region(&unifi_first_devno, 0, MAX_UNIFI_DEVS*2, UNIFI_NAME);
    if (ret) {
        unifi_error(NULL, "Failed to add alloc dev numbers: %d\n", ret);
        return ret;
    }

    /* Create a UniFi class */
    unifi_class = class_create(THIS_MODULE, UNIFI_NAME);
    if (IS_ERR(unifi_class)) {
        unifi_error(NULL, "Failed to create UniFi class\n");

        /* Release device numbers */
        unregister_chrdev_region(unifi_first_devno, MAX_UNIFI_DEVS*2);
        unifi_first_devno = 0;
        return -EINVAL;
    }

    return 0;
} /* uf_create_debug_device() */


/*
 * ----------------------------------------------------------------
 *  uf_remove_debug_device
 *
 *      Destroys the unifi class and releases the allocated
 *      device numbers for unifi character device nodes.
 *
 * Arguments:
 *
 * Returns:
 * ----------------------------------------------------------------
 */
static void
uf_remove_debug_device(void)
{
    /* Destroy the UniFi class */
    class_destroy(unifi_class);

    /* Release device numbers */
    unregister_chrdev_region(unifi_first_devno, MAX_UNIFI_DEVS*2);
    unifi_first_devno = 0;

} /* uf_remove_debug_device() */


/*
 * ---------------------------------------------------------------------------
 *
 *      Module loading.
 *
 * ---------------------------------------------------------------------------
 */
int __init
unifi_load(void)
{
    int r;

    printk("UniFi SDIO Driver: %s %s %s\n",
            CSR_WIFI_VERSION,
           __DATE__, __TIME__);

#ifdef CSR_SME_USERSPACE
#ifdef CSR_SUPPORT_WEXT
    printk("CSR SME with WEXT support\n");
#else
    printk("CSR SME no WEXT support\n");
#endif /* CSR_SUPPORT_WEXT */
#endif /* CSR_SME_USERSPACE */

#ifdef CSR_NATIVE_LINUX
#ifdef CSR_SUPPORT_WEXT
#error WEXT unsupported in the native driver
#endif
    printk("CSR native no WEXT support\n");
#endif
#ifdef CSR_WIFI_SPLIT_PATCH
    printk("Split patch support\n");
#endif
    printk("Kernel %d.%d.%d\n",
           ((LINUX_VERSION_CODE) >> 16) & 0xff,
           ((LINUX_VERSION_CODE) >> 8) & 0xff,
           (LINUX_VERSION_CODE) & 0xff);
    /*
     * Instantiate the /dev/unifi* device nodes.
     * We must do this before registering with the SDIO driver because it
     * will immediately call the "insert" callback if the card is
     * already present.
     */
    r = uf_create_debug_device(&unifi_fops);
    if (r) {
        return r;
    }

    /* Now register with the SDIO driver */
    r = uf_sdio_load();
    if (r) {
        uf_remove_debug_device();
        return r;
    }

    if (sdio_block_size > -1) {
        unifi_info(NULL, "sdio_block_size %d\n", sdio_block_size);
    }

    if (sdio_byte_mode) {
        unifi_info(NULL, "sdio_byte_mode\n");
    }

    if (disable_power_control) {
        unifi_info(NULL, "disable_power_control\n");
    }

    if (disable_hw_reset) {
        unifi_info(NULL, "disable_hw_reset\n");
    }

    if (enable_wol) {
        unifi_info(NULL, "enable_wol %d\n", enable_wol);
    }

    if (run_bh_once != -1) {
        unifi_info(NULL, "run_bh_once %d\n", run_bh_once);
    }

    return 0;
} /* unifi_load() */


void __exit
unifi_unload(void)
{
    /* The SDIO remove hook will call unifi_disconnect(). */
    uf_sdio_unload();

    uf_remove_debug_device();

} /* unifi_unload() */

module_init(unifi_load);
module_exit(unifi_unload);

MODULE_DESCRIPTION("UniFi Device driver");
MODULE_AUTHOR("Cambridge Silicon Radio Ltd.");
MODULE_LICENSE("GPL and additional rights");
