//------------------------------------------------------------------------------
// Copyright (c) 2004-2010 Atheros Communications Inc.
// All rights reserved.
//
// 
//
// Permission to use, copy, modify, and/or distribute this software for any
// purpose with or without fee is hereby granted, provided that the above
// copyright notice and this permission notice appear in all copies.
//
// THE SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
// WITH REGARD TO THIS SOFTWARE INCLUDING ALL IMPLIED WARRANTIES OF
// MERCHANTABILITY AND FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
// ANY SPECIAL, DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
// WHATSOEVER RESULTING FROM LOSS OF USE, DATA OR PROFITS, WHETHER IN AN
// ACTION OF CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
// OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
//
//
//
// Author(s): ="Atheros"
//------------------------------------------------------------------------------

/*
 * This driver is a pseudo ethernet driver to access the Atheros AR6000
 * WLAN Device
 */

#include "ar6000_drv.h"
#ifdef ATH6K_CONFIG_CFG80211
#include "cfg80211.h"
#endif /* ATH6K_CONFIG_CFG80211 */
#include "htc.h"
#include "wmi_filter_linux.h"
#include "epping_test.h"
#include "wlan_config.h"
#include "ar3kconfig.h"
#include "ar6k_pal.h"
#include "AR6002/addrs.h"


/* LINUX_HACK_FUDGE_FACTOR -- this is used to provide a workaround for linux behavior.  When
 *  the meta data was added to the header it was found that linux did not correctly provide
 *  enough headroom.  However when more headroom was requested beyond what was truly needed
 *  Linux gave the requested headroom. Therefore to get the necessary headroom from Linux
 *  the driver requests more than is needed by the amount = LINUX_HACK_FUDGE_FACTOR */
#define LINUX_HACK_FUDGE_FACTOR 16
#define BDATA_BDADDR_OFFSET     28

A_UINT8 bcast_mac[] = {0xff, 0xff, 0xff, 0xff, 0xff, 0xff};
A_UINT8 null_mac[] = {0x0, 0x0, 0x0, 0x0, 0x0, 0x0};

#ifdef DEBUG

#define  ATH_DEBUG_DBG_LOG       ATH_DEBUG_MAKE_MODULE_MASK(0)
#define  ATH_DEBUG_WLAN_CONNECT  ATH_DEBUG_MAKE_MODULE_MASK(1)
#define  ATH_DEBUG_WLAN_SCAN     ATH_DEBUG_MAKE_MODULE_MASK(2)
#define  ATH_DEBUG_WLAN_TX       ATH_DEBUG_MAKE_MODULE_MASK(3)
#define  ATH_DEBUG_WLAN_RX       ATH_DEBUG_MAKE_MODULE_MASK(4)
#define  ATH_DEBUG_HTC_RAW       ATH_DEBUG_MAKE_MODULE_MASK(5)
#define  ATH_DEBUG_HCI_BRIDGE    ATH_DEBUG_MAKE_MODULE_MASK(6)

static ATH_DEBUG_MASK_DESCRIPTION driver_debug_desc[] = {
    { ATH_DEBUG_DBG_LOG      , "Target Debug Logs"},
    { ATH_DEBUG_WLAN_CONNECT , "WLAN connect"},
    { ATH_DEBUG_WLAN_SCAN    , "WLAN scan"},
    { ATH_DEBUG_WLAN_TX      , "WLAN Tx"},
    { ATH_DEBUG_WLAN_RX      , "WLAN Rx"},
    { ATH_DEBUG_HTC_RAW      , "HTC Raw IF tracing"},
    { ATH_DEBUG_HCI_BRIDGE   , "HCI Bridge Setup"},
    { ATH_DEBUG_HCI_RECV     , "HCI Recv tracing"},
    { ATH_DEBUG_HCI_DUMP     , "HCI Packet dumps"},
};

ATH_DEBUG_INSTANTIATE_MODULE_VAR(driver,
                                 "driver",
                                 "Linux Driver Interface",
                                 ATH_DEBUG_MASK_DEFAULTS | ATH_DEBUG_WLAN_SCAN |
                                 ATH_DEBUG_HCI_BRIDGE,
                                 ATH_DEBUG_DESCRIPTION_COUNT(driver_debug_desc),
                                 driver_debug_desc);

#endif


#define IS_MAC_NULL(mac) (mac[0]==0 && mac[1]==0 && mac[2]==0 && mac[3]==0 && mac[4]==0 && mac[5]==0)
#define IS_MAC_BCAST(mac) (*mac==0xff)

#define DESCRIPTION "Driver to access the Atheros AR600x Device, version " __stringify(__VER_MAJOR_) "." __stringify(__VER_MINOR_) "." __stringify(__VER_PATCH_) "." __stringify(__BUILD_NUMBER_)

MODULE_AUTHOR("Atheros Communications, Inc.");
MODULE_DESCRIPTION(DESCRIPTION);
MODULE_LICENSE("Dual BSD/GPL");

#ifndef REORG_APTC_HEURISTICS
#undef ADAPTIVE_POWER_THROUGHPUT_CONTROL
#endif /* REORG_APTC_HEURISTICS */

#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
#define APTC_TRAFFIC_SAMPLING_INTERVAL     100  /* msec */
#define APTC_UPPER_THROUGHPUT_THRESHOLD    3000 /* Kbps */
#define APTC_LOWER_THROUGHPUT_THRESHOLD    2000 /* Kbps */

typedef struct aptc_traffic_record {
    A_BOOL timerScheduled;
    struct timeval samplingTS;
    unsigned long bytesReceived;
    unsigned long bytesTransmitted;
} APTC_TRAFFIC_RECORD;

A_TIMER aptcTimer;
APTC_TRAFFIC_RECORD aptcTR;
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
// callbacks registered by HCI transport driver
HCI_TRANSPORT_CALLBACKS ar6kHciTransCallbacks = { NULL };
#endif

unsigned int processDot11Hdr = 0;
int bmienable = BMIENABLE_DEFAULT;

char ifname[IFNAMSIZ] = {0,};

int wlaninitmode = WLAN_INIT_MODE_DEFAULT;
unsigned int bypasswmi = 0;
unsigned int debuglevel = 0;
int tspecCompliance = ATHEROS_COMPLIANCE;
unsigned int busspeedlow = 0;
unsigned int onebitmode = 0;
unsigned int skipflash = 0;
unsigned int wmitimeout = 2;
unsigned int wlanNodeCaching = 1;
unsigned int enableuartprint = ENABLEUARTPRINT_DEFAULT;
unsigned int logWmiRawMsgs = 0;
unsigned int enabletimerwar = 0;
unsigned int fwmode = 1;
unsigned int mbox_yield_limit = 99;
unsigned int enablerssicompensation = 0;
int reduce_credit_dribble = 1 + HTC_CONNECT_FLAGS_THRESHOLD_LEVEL_ONE_HALF;
int allow_trace_signal = 0;
#ifdef CONFIG_HOST_TCMD_SUPPORT
unsigned int testmode =0;
#endif

unsigned int irqprocmode = HIF_DEVICE_IRQ_SYNC_ONLY;//HIF_DEVICE_IRQ_ASYNC_SYNC;
unsigned int panic_on_assert = 1;
unsigned int nohifscattersupport = NOHIFSCATTERSUPPORT_DEFAULT;

unsigned int setuphci = SETUPHCI_DEFAULT;
unsigned int setuphcipal = SETUPHCIPAL_DEFAULT;
unsigned int loghci = 0;
unsigned int setupbtdev = SETUPBTDEV_DEFAULT;
#ifndef EXPORT_HCI_BRIDGE_INTERFACE
unsigned int ar3khcibaud = AR3KHCIBAUD_DEFAULT;
unsigned int hciuartscale = HCIUARTSCALE_DEFAULT;
unsigned int hciuartstep = HCIUARTSTEP_DEFAULT;
#endif
#ifdef CONFIG_CHECKSUM_OFFLOAD
unsigned int csumOffload=0;
unsigned int csumOffloadTest=0;
#endif
unsigned int eppingtest=0;

module_param_string(ifname, ifname, sizeof(ifname), 0644);
module_param(wlaninitmode, int, 0644);
module_param(bmienable, int, 0644);
module_param(bypasswmi, uint, 0644);
module_param(debuglevel, uint, 0644);
module_param(tspecCompliance, int, 0644);
module_param(onebitmode, uint, 0644);
module_param(busspeedlow, uint, 0644);
module_param(skipflash, uint, 0644);
module_param(wmitimeout, uint, 0644);
module_param(wlanNodeCaching, uint, 0644);
module_param(logWmiRawMsgs, uint, 0644);
module_param(enableuartprint, uint, 0644);
module_param(enabletimerwar, uint, 0644);
module_param(fwmode, uint, 0644);
module_param(mbox_yield_limit, uint, 0644);
module_param(reduce_credit_dribble, int, 0644);
module_param(allow_trace_signal, int, 0644);
module_param(enablerssicompensation, uint, 0644);
module_param(processDot11Hdr, uint, 0644);
#ifdef CONFIG_CHECKSUM_OFFLOAD
module_param(csumOffload, uint, 0644);
#endif
#ifdef CONFIG_HOST_TCMD_SUPPORT
module_param(testmode, uint, 0644);
#endif
module_param(irqprocmode, uint, 0644);
module_param(nohifscattersupport, uint, 0644);
module_param(panic_on_assert, uint, 0644);
module_param(setuphci, uint, 0644);
module_param(setuphcipal, uint, 0644);
module_param(loghci, uint, 0644);
module_param(setupbtdev, uint, 0644);
#ifndef EXPORT_HCI_BRIDGE_INTERFACE
module_param(ar3khcibaud, uint, 0644);
module_param(hciuartscale, uint, 0644);
module_param(hciuartstep, uint, 0644);
#endif
module_param(eppingtest, uint, 0644);

/* in 2.6.10 and later this is now a pointer to a uint */
unsigned int _mboxnum = HTC_MAILBOX_NUM_MAX;
#define mboxnum &_mboxnum

#ifdef DEBUG
A_UINT32 g_dbg_flags = DBG_DEFAULTS;
unsigned int debugflags = 0;
int debugdriver = 0;
unsigned int debughtc = 0;
unsigned int debugbmi = 0;
unsigned int debughif = 0;
unsigned int txcreditsavailable[HTC_MAILBOX_NUM_MAX] = {0};
unsigned int txcreditsconsumed[HTC_MAILBOX_NUM_MAX] = {0};
unsigned int txcreditintrenable[HTC_MAILBOX_NUM_MAX] = {0};
unsigned int txcreditintrenableaggregate[HTC_MAILBOX_NUM_MAX] = {0};
module_param(debugflags, uint, 0644);
module_param(debugdriver, int, 0644);
module_param(debughtc, uint, 0644);
module_param(debugbmi, uint, 0644);
module_param(debughif, uint, 0644);
module_param_array(txcreditsavailable, uint, mboxnum, 0644);
module_param_array(txcreditsconsumed, uint, mboxnum, 0644);
module_param_array(txcreditintrenable, uint, mboxnum, 0644);
module_param_array(txcreditintrenableaggregate, uint, mboxnum, 0644);

#endif /* DEBUG */

unsigned int resetok = 1;
unsigned int tx_attempt[HTC_MAILBOX_NUM_MAX] = {0};
unsigned int tx_post[HTC_MAILBOX_NUM_MAX] = {0};
unsigned int tx_complete[HTC_MAILBOX_NUM_MAX] = {0};
unsigned int hifBusRequestNumMax = 40;
unsigned int war23838_disabled = 0;
#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
unsigned int enableAPTCHeuristics = 1;
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */
module_param_array(tx_attempt, uint, mboxnum, 0644);
module_param_array(tx_post, uint, mboxnum, 0644);
module_param_array(tx_complete, uint, mboxnum, 0644);
module_param(hifBusRequestNumMax, uint, 0644);
module_param(war23838_disabled, uint, 0644);
module_param(resetok, uint, 0644);
#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
module_param(enableAPTCHeuristics, uint, 0644);
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */

#ifdef BLOCK_TX_PATH_FLAG
int blocktx = 0;
module_param(blocktx, int, 0644);
#endif /* BLOCK_TX_PATH_FLAG */

typedef struct user_rssi_compensation_t {
    A_UINT16         customerID;
    union {
    A_UINT16         a_enable;
    A_UINT16         bg_enable;
    A_UINT16         enable;
    };
    A_INT16          bg_param_a;
    A_INT16          bg_param_b;
    A_INT16          a_param_a;
    A_INT16          a_param_b;
    A_UINT32         reserved;
} USER_RSSI_CPENSATION;

static USER_RSSI_CPENSATION rssi_compensation_param;

static A_INT16 rssi_compensation_table[96];

int reconnect_flag = 0;
static ar6k_pal_config_t ar6k_pal_config_g;

/* Function declarations */
static int ar6000_init_module(void);
static void ar6000_cleanup_module(void);

int ar6000_init(struct net_device *dev);
static int ar6000_open(struct net_device *dev);
static int ar6000_close(struct net_device *dev);
static void ar6000_init_control_info(AR_SOFTC_T *ar);
static int ar6000_data_tx(struct sk_buff *skb, struct net_device *dev);

void ar6000_destroy(struct net_device *dev, unsigned int unregister);
static void ar6000_detect_error(unsigned long ptr);
static void	ar6000_set_multicast_list(struct net_device *dev);
static struct net_device_stats *ar6000_get_stats(struct net_device *dev);
static struct iw_statistics *ar6000_get_iwstats(struct net_device * dev);

static void disconnect_timer_handler(unsigned long ptr);

void read_rssi_compensation_param(AR_SOFTC_T *ar);

    /* for android builds we call external APIs that handle firmware download and configuration */
#ifdef ANDROID_ENV
/* !!!! Interim android support to make it easier to patch the default driver for
 * android use. You must define an external source file ar6000_android.c that handles the following
 * APIs */
extern void android_module_init(OSDRV_CALLBACKS *osdrvCallbacks);
extern void android_module_exit(void);
#endif
/*
 * HTC service connection handlers
 */
static A_STATUS ar6000_avail_ev(void *context, void *hif_handle);

static A_STATUS ar6000_unavail_ev(void *context, void *hif_handle);

A_STATUS ar6000_configure_target(AR_SOFTC_T *ar);

static void ar6000_target_failure(void *Instance, A_STATUS Status);

static void ar6000_rx(void *Context, HTC_PACKET *pPacket);

static void ar6000_rx_refill(void *Context,HTC_ENDPOINT_ID Endpoint);

static void ar6000_tx_complete(void *Context, HTC_PACKET_QUEUE *pPackets);

static HTC_SEND_FULL_ACTION ar6000_tx_queue_full(void *Context, HTC_PACKET *pPacket);

#ifdef ATH_AR6K_11N_SUPPORT
static void ar6000_alloc_netbufs(A_NETBUF_QUEUE_T *q, A_UINT16 num);
#endif
static void ar6000_deliver_frames_to_nw_stack(void * dev, void *osbuf);
//static void ar6000_deliver_frames_to_bt_stack(void * dev, void *osbuf);

static HTC_PACKET *ar6000_alloc_amsdu_rxbuf(void *Context, HTC_ENDPOINT_ID Endpoint, int Length);

static void ar6000_refill_amsdu_rxbufs(AR_SOFTC_T *ar, int Count);

static void ar6000_cleanup_amsdu_rxbufs(AR_SOFTC_T *ar);

static ssize_t
ar6000_sysfs_bmi_read(struct file *fp, struct kobject *kobj,
                      struct bin_attribute *bin_attr,
                      char *buf, loff_t pos, size_t count);

static ssize_t
ar6000_sysfs_bmi_write(struct file *fp, struct kobject *kobj,
                       struct bin_attribute *bin_attr,
                       char *buf, loff_t pos, size_t count);

static A_STATUS
ar6000_sysfs_bmi_init(AR_SOFTC_T *ar);

/* HCI PAL callback function declarations */
A_STATUS ar6k_setup_hci_pal(AR_SOFTC_T *ar);
void  ar6k_cleanup_hci_pal(AR_SOFTC_T *ar);

static void
ar6000_sysfs_bmi_deinit(AR_SOFTC_T *ar);

A_STATUS
ar6000_sysfs_bmi_get_config(AR_SOFTC_T *ar, A_UINT32 mode);

/*
 * Static variables
 */

struct net_device *ar6000_devices[MAX_AR6000];
static int is_netdev_registered;
extern struct iw_handler_def ath_iw_handler_def;
DECLARE_WAIT_QUEUE_HEAD(arEvent);
static void ar6000_cookie_init(AR_SOFTC_T *ar);
static void ar6000_cookie_cleanup(AR_SOFTC_T *ar);
static void ar6000_free_cookie(AR_SOFTC_T *ar, struct ar_cookie * cookie);
static struct ar_cookie *ar6000_alloc_cookie(AR_SOFTC_T *ar);

#ifdef USER_KEYS
static A_STATUS ar6000_reinstall_keys(AR_SOFTC_T *ar,A_UINT8 key_op_ctrl);
#endif

#ifdef CONFIG_AP_VIRTUAL_ADAPTER_SUPPORT
struct net_device *arApNetDev;
#endif /* CONFIG_AP_VIRTUAL_ADAPTER_SUPPORT */

static struct ar_cookie s_ar_cookie_mem[MAX_COOKIE_NUM];

#define HOST_INTEREST_ITEM_ADDRESS(ar, item) \
        (((ar)->arTargetType == TARGET_TYPE_AR6002) ? AR6002_HOST_INTEREST_ITEM_ADDRESS(item) : \
        (((ar)->arTargetType == TARGET_TYPE_AR6003) ? AR6003_HOST_INTEREST_ITEM_ADDRESS(item) : 0))


static struct net_device_ops ar6000_netdev_ops = {
    .ndo_init               = NULL,
    .ndo_open               = ar6000_open,
    .ndo_stop               = ar6000_close,
    .ndo_get_stats          = ar6000_get_stats,
    .ndo_do_ioctl           = ar6000_ioctl,
    .ndo_start_xmit         = ar6000_data_tx,
    .ndo_set_multicast_list = ar6000_set_multicast_list,
};

/* Debug log support */

/*
 * Flag to govern whether the debug logs should be parsed in the kernel
 * or reported to the application.
 */
#define REPORT_DEBUG_LOGS_TO_APP

A_STATUS
ar6000_set_host_app_area(AR_SOFTC_T *ar)
{
    A_UINT32 address, data;
    struct host_app_area_s host_app_area;

    /* Fetch the address of the host_app_area_s instance in the host interest area */
    address = TARG_VTOP(ar->arTargetType, HOST_INTEREST_ITEM_ADDRESS(ar, hi_app_host_interest));
    if (ar6000_ReadRegDiag(ar->arHifDevice, &address, &data) != A_OK) {
        return A_ERROR;
    }
    address = TARG_VTOP(ar->arTargetType, data);
    host_app_area.wmi_protocol_ver = WMI_PROTOCOL_VERSION;
    if (ar6000_WriteDataDiag(ar->arHifDevice, address,
                             (A_UCHAR *)&host_app_area,
                             sizeof(struct host_app_area_s)) != A_OK)
    {
        return A_ERROR;
    }

    return A_OK;
}

A_UINT32
dbglog_get_debug_hdr_ptr(AR_SOFTC_T *ar)
{
    A_UINT32 param;
    A_UINT32 address;
    A_STATUS status;

    address = TARG_VTOP(ar->arTargetType, HOST_INTEREST_ITEM_ADDRESS(ar, hi_dbglog_hdr));
    if ((status = ar6000_ReadDataDiag(ar->arHifDevice, address,
                                      (A_UCHAR *)&param, 4)) != A_OK)
    {
        param = 0;
    }

    return param;
}

/*
 * The dbglog module has been initialized. Its ok to access the relevant
 * data stuctures over the diagnostic window.
 */
void
ar6000_dbglog_init_done(AR_SOFTC_T *ar)
{
    ar->dbglog_init_done = TRUE;
}

A_UINT32
dbglog_get_debug_fragment(A_INT8 *datap, A_UINT32 len, A_UINT32 limit)
{
    A_INT32 *buffer;
    A_UINT32 count;
    A_UINT32 numargs;
    A_UINT32 length;
    A_UINT32 fraglen;

    count = fraglen = 0;
    buffer = (A_INT32 *)datap;
    length = (limit >> 2);

    if (len <= limit) {
        fraglen = len;
    } else {
        while (count < length) {
            numargs = DBGLOG_GET_NUMARGS(buffer[count]);
            fraglen = (count << 2);
            count += numargs + 1;
        }
    }

    return fraglen;
}

void
dbglog_parse_debug_logs(A_INT8 *datap, A_UINT32 len)
{
    A_INT32 *buffer;
    A_UINT32 count;
    A_UINT32 timestamp;
    A_UINT32 debugid;
    A_UINT32 moduleid;
    A_UINT32 numargs;
    A_UINT32 length;

    count = 0;
    buffer = (A_INT32 *)datap;
    length = (len >> 2);
    while (count < length) {
        debugid = DBGLOG_GET_DBGID(buffer[count]);
        moduleid = DBGLOG_GET_MODULEID(buffer[count]);
        numargs = DBGLOG_GET_NUMARGS(buffer[count]);
        timestamp = DBGLOG_GET_TIMESTAMP(buffer[count]);
        switch (numargs) {
            case 0:
            AR_DEBUG_PRINTF(ATH_DEBUG_DBG_LOG,("%d %d (%d)\n", moduleid, debugid, timestamp));
            break;

            case 1:
            AR_DEBUG_PRINTF(ATH_DEBUG_DBG_LOG,("%d %d (%d): 0x%x\n", moduleid, debugid,
                            timestamp, buffer[count+1]));
            break;

            case 2:
            AR_DEBUG_PRINTF(ATH_DEBUG_DBG_LOG,("%d %d (%d): 0x%x, 0x%x\n", moduleid, debugid,
                            timestamp, buffer[count+1], buffer[count+2]));
            break;

            default:
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Invalid args: %d\n", numargs));
        }
        count += numargs + 1;
    }
}

int
ar6000_dbglog_get_debug_logs(AR_SOFTC_T *ar)
{
    A_UINT32 data[8]; /* Should be able to accomodate struct dbglog_buf_s */
    A_UINT32 address;
    A_UINT32 length;
    A_UINT32 dropped;
    A_UINT32 firstbuf;
    A_UINT32 debug_hdr_ptr;

    if (!ar->dbglog_init_done) return A_ERROR;


    AR6000_SPIN_LOCK(&ar->arLock, 0);

    if (ar->dbgLogFetchInProgress) {
        AR6000_SPIN_UNLOCK(&ar->arLock, 0);
        return A_EBUSY;
    }

        /* block out others */
    ar->dbgLogFetchInProgress = TRUE;

    AR6000_SPIN_UNLOCK(&ar->arLock, 0);

    debug_hdr_ptr = dbglog_get_debug_hdr_ptr(ar);
    printk("debug_hdr_ptr: 0x%x\n", debug_hdr_ptr);

    /* Get the contents of the ring buffer */
    if (debug_hdr_ptr) {
        address = TARG_VTOP(ar->arTargetType, debug_hdr_ptr);
        length = 4 /* sizeof(dbuf) */ + 4 /* sizeof(dropped) */;
        A_MEMZERO(data, sizeof(data));
        ar6000_ReadDataDiag(ar->arHifDevice, address, (A_UCHAR *)data, length);
        address = TARG_VTOP(ar->arTargetType, data[0] /* dbuf */);
        firstbuf = address;
        dropped = data[1]; /* dropped */
        length = 4 /* sizeof(next) */ + 4 /* sizeof(buffer) */ + 4 /* sizeof(bufsize) */ + 4 /* sizeof(length) */ + 4 /* sizeof(count) */ + 4 /* sizeof(free) */;
        A_MEMZERO(data, sizeof(data));
        ar6000_ReadDataDiag(ar->arHifDevice, address, (A_UCHAR *)&data, length);

        do {
            address = TARG_VTOP(ar->arTargetType, data[1] /* buffer*/);
            length = data[3]; /* length */
            if ((length) && (length <= data[2] /* bufsize*/)) {
                /* Rewind the index if it is about to overrun the buffer */
                if (ar->log_cnt > (DBGLOG_HOST_LOG_BUFFER_SIZE - length)) {
                    ar->log_cnt = 0;
                }
                if(A_OK != ar6000_ReadDataDiag(ar->arHifDevice, address,
                                    (A_UCHAR *)&ar->log_buffer[ar->log_cnt], length))
                {
                    break;
                }
                ar6000_dbglog_event(ar, dropped, (A_INT8*)&ar->log_buffer[ar->log_cnt], length);
                ar->log_cnt += length;
            } else {
                AR_DEBUG_PRINTF(ATH_DEBUG_DBG_LOG,("Length: %d (Total size: %d)\n",
                                data[3], data[2]));
            }

            address = TARG_VTOP(ar->arTargetType, data[0] /* next */);
            length = 4 /* sizeof(next) */ + 4 /* sizeof(buffer) */ + 4 /* sizeof(bufsize) */ + 4 /* sizeof(length) */ + 4 /* sizeof(count) */ + 4 /* sizeof(free) */;
            A_MEMZERO(data, sizeof(data));
            if(A_OK != ar6000_ReadDataDiag(ar->arHifDevice, address,
                                (A_UCHAR *)&data, length))
            {
                break;
            }

        } while (address != firstbuf);
    }

    ar->dbgLogFetchInProgress = FALSE;

    return A_OK;
}

void
ar6000_dbglog_event(AR_SOFTC_T *ar, A_UINT32 dropped,
                    A_INT8 *buffer, A_UINT32 length)
{
#ifdef REPORT_DEBUG_LOGS_TO_APP
    #define MAX_WIRELESS_EVENT_SIZE 252
    /*
     * Break it up into chunks of MAX_WIRELESS_EVENT_SIZE bytes of messages.
     * There seems to be a limitation on the length of message that could be
     * transmitted to the user app via this mechanism.
     */
    A_UINT32 send, sent;

    sent = 0;
    send = dbglog_get_debug_fragment(&buffer[sent], length - sent,
                                     MAX_WIRELESS_EVENT_SIZE);
    while (send) {
        ar6000_send_event_to_app(ar, WMIX_DBGLOG_EVENTID, (A_UINT8*)&buffer[sent], send);
        sent += send;
        send = dbglog_get_debug_fragment(&buffer[sent], length - sent,
                                         MAX_WIRELESS_EVENT_SIZE);
    }
#else
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Dropped logs: 0x%x\nDebug info length: %d\n",
                    dropped, length));

    /* Interpret the debug logs */
    dbglog_parse_debug_logs((A_INT8*)buffer, length);
#endif /* REPORT_DEBUG_LOGS_TO_APP */
}


static int __init
ar6000_init_module(void)
{
    static int probed = 0;
    A_STATUS status;
    OSDRV_CALLBACKS osdrvCallbacks;

    a_module_debug_support_init();

#ifdef DEBUG
        /* check for debug mask overrides */
    if (debughtc != 0) {
        ATH_DEBUG_SET_DEBUG_MASK(htc,debughtc);
    }
    if (debugbmi != 0) {
        ATH_DEBUG_SET_DEBUG_MASK(bmi,debugbmi);
    }
    if (debughif != 0) {
        ATH_DEBUG_SET_DEBUG_MASK(hif,debughif);
    }
    if (debugdriver != 0) {
        ATH_DEBUG_SET_DEBUG_MASK(driver,debugdriver);
    }

#endif

    A_REGISTER_MODULE_DEBUG_INFO(driver);

    A_MEMZERO(&osdrvCallbacks,sizeof(osdrvCallbacks));
    osdrvCallbacks.deviceInsertedHandler = ar6000_avail_ev;
    osdrvCallbacks.deviceRemovedHandler = ar6000_unavail_ev;
#ifdef CONFIG_PM
    osdrvCallbacks.deviceSuspendHandler = ar6000_suspend_ev;
    osdrvCallbacks.deviceResumeHandler = ar6000_resume_ev;
    osdrvCallbacks.devicePowerChangeHandler = ar6000_power_change_ev;
#endif

    ar6000_pm_init();

#ifdef ANDROID_ENV
    android_module_init(&osdrvCallbacks);
#endif

#ifdef DEBUG
    /* Set the debug flags if specified at load time */
    if(debugflags != 0)
    {
        g_dbg_flags = debugflags;
    }
#endif

    if (probed) {
        return -ENODEV;
    }
    probed++;

#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
    memset(&aptcTR, 0, sizeof(APTC_TRAFFIC_RECORD));
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */

#ifdef CONFIG_HOST_GPIO_SUPPORT
    ar6000_gpio_init();
#endif /* CONFIG_HOST_GPIO_SUPPORT */

    status = HIFInit(&osdrvCallbacks);
    if(status != A_OK)
        return -ENODEV;

    return 0;
}

static void __exit
ar6000_cleanup_module(void)
{
    int i = 0;
    struct net_device *ar6000_netdev;

#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
    /* Delete the Adaptive Power Control timer */
    if (timer_pending(&aptcTimer)) {
        del_timer_sync(&aptcTimer);
    }
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */

    for (i=0; i < MAX_AR6000; i++) {
        if (ar6000_devices[i] != NULL) {
            ar6000_netdev = ar6000_devices[i];
            ar6000_devices[i] = NULL;
            ar6000_destroy(ar6000_netdev, 1);
        }
    }

    HIFShutDownDevice(NULL);

    a_module_debug_support_cleanup();

    ar6000_pm_exit();

#ifdef ANDROID_ENV    
    android_module_exit();
#endif

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("ar6000_cleanup: success\n"));
}

#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
void
aptcTimerHandler(unsigned long arg)
{
    A_UINT32 numbytes;
    A_UINT32 throughput;
    AR_SOFTC_T *ar;
    A_STATUS status;

    ar = (AR_SOFTC_T *)arg;
    A_ASSERT(ar != NULL);
    A_ASSERT(!timer_pending(&aptcTimer));

    AR6000_SPIN_LOCK(&ar->arLock, 0);

    /* Get the number of bytes transferred */
    numbytes = aptcTR.bytesTransmitted + aptcTR.bytesReceived;
    aptcTR.bytesTransmitted = aptcTR.bytesReceived = 0;

    /* Calculate and decide based on throughput thresholds */
    throughput = ((numbytes * 8)/APTC_TRAFFIC_SAMPLING_INTERVAL); /* Kbps */
    if (throughput < APTC_LOWER_THROUGHPUT_THRESHOLD) {
        /* Enable Sleep and delete the timer */
        A_ASSERT(ar->arWmiReady == TRUE);
        AR6000_SPIN_UNLOCK(&ar->arLock, 0);
        status = wmi_powermode_cmd(ar->arWmi, REC_POWER);
        AR6000_SPIN_LOCK(&ar->arLock, 0);
        A_ASSERT(status == A_OK);
        aptcTR.timerScheduled = FALSE;
    } else {
        A_TIMEOUT_MS(&aptcTimer, APTC_TRAFFIC_SAMPLING_INTERVAL, 0);
    }

    AR6000_SPIN_UNLOCK(&ar->arLock, 0);
}
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */

#ifdef ATH_AR6K_11N_SUPPORT
static void
ar6000_alloc_netbufs(A_NETBUF_QUEUE_T *q, A_UINT16 num)
{
    void * osbuf;

    while(num) {
        if((osbuf = A_NETBUF_ALLOC(AR6000_BUFFER_SIZE))) {
            A_NETBUF_ENQUEUE(q, osbuf);
        } else {
            break;
        }
        num--;
    }

    if(num) {
        A_PRINTF("%s(), allocation of netbuf failed", __func__);
    }
}
#endif

static struct bin_attribute bmi_attr = {
    .attr = {.name = "bmi", .mode = 0600},
    .read = ar6000_sysfs_bmi_read,
    .write = ar6000_sysfs_bmi_write,
};

static ssize_t
ar6000_sysfs_bmi_read(struct file *fp, struct kobject *kobj,
                      struct bin_attribute *bin_attr,
                      char *buf, loff_t pos, size_t count)
{
    int index;
    AR_SOFTC_T *ar;
    HIF_DEVICE_OS_DEVICE_INFO   *osDevInfo;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("BMI: Read %d bytes\n", (A_UINT32)count));
    for (index=0; index < MAX_AR6000; index++) {
        ar = (AR_SOFTC_T *)ar6k_priv(ar6000_devices[index]);
        osDevInfo = &ar->osDevInfo;
        if (kobj == (&(((struct device *)osDevInfo->pOSDevice)->kobj))) {
            break;
        }
    }

    if (index == MAX_AR6000) return 0;

    if ((BMIRawRead(ar->arHifDevice, (A_UCHAR*)buf, count, TRUE)) != A_OK) {
        return 0;
    }

    return count;
}

static ssize_t
ar6000_sysfs_bmi_write(struct file *fp, struct kobject *kobj,
                       struct bin_attribute *bin_attr,
                       char *buf, loff_t pos, size_t count)
{
    int index;
    AR_SOFTC_T *ar;
    HIF_DEVICE_OS_DEVICE_INFO   *osDevInfo;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("BMI: Write %d bytes\n", (A_UINT32)count));
    for (index=0; index < MAX_AR6000; index++) {
        ar = (AR_SOFTC_T *)ar6k_priv(ar6000_devices[index]);
        osDevInfo = &ar->osDevInfo;
        if (kobj == (&(((struct device *)osDevInfo->pOSDevice)->kobj))) {
            break;
        }
    }

    if (index == MAX_AR6000) return 0;

    if ((BMIRawWrite(ar->arHifDevice, (A_UCHAR*)buf, count)) != A_OK) {
        return 0;
    }

    return count;
}

static A_STATUS
ar6000_sysfs_bmi_init(AR_SOFTC_T *ar)
{
    A_STATUS status;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("BMI: Creating sysfs entry\n"));
    A_MEMZERO(&ar->osDevInfo, sizeof(HIF_DEVICE_OS_DEVICE_INFO));

    /* Get the underlying OS device */
    status = HIFConfigureDevice(ar->arHifDevice,
                                HIF_DEVICE_GET_OS_DEVICE,
                                &ar->osDevInfo,
                                sizeof(HIF_DEVICE_OS_DEVICE_INFO));

    if (A_FAILED(status)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("BMI: Failed to get OS device info from HIF\n"));
        return A_ERROR;
    }

    /* Create a bmi entry in the sysfs filesystem */
    if ((sysfs_create_bin_file(&(((struct device *)ar->osDevInfo.pOSDevice)->kobj), &bmi_attr)) < 0)
    {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMI: Failed to create entry for bmi in sysfs filesystem\n"));
        return A_ERROR;
    }

    return A_OK;
}

static void
ar6000_sysfs_bmi_deinit(AR_SOFTC_T *ar)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("BMI: Deleting sysfs entry\n"));

    sysfs_remove_bin_file(&(((struct device *)ar->osDevInfo.pOSDevice)->kobj), &bmi_attr);
}

#define bmifn(fn) do { \
    if ((fn) < A_OK) { \
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("BMI operation failed: %d\n", __LINE__)); \
        return A_ERROR; \
    } \
} while(0)

#ifdef INIT_MODE_DRV_ENABLED

#ifdef SOFTMAC_FILE_USED
#define AR6002_MAC_ADDRESS_OFFSET     0x0A
#define AR6003_MAC_ADDRESS_OFFSET     0x16
static
void calculate_crc(A_UINT32 TargetType, A_UCHAR *eeprom_data)
{
    A_UINT16        *ptr_crc;
    A_UINT16        *ptr16_eeprom;
    A_UINT16        checksum;
    A_UINT32        i;
    A_UINT32        eeprom_size;

    if (TargetType == TARGET_TYPE_AR6001)
    {
        eeprom_size = 512;
        ptr_crc = (A_UINT16 *)eeprom_data;
    }
    else if (TargetType == TARGET_TYPE_AR6003)
    {
        eeprom_size = 1024;
        ptr_crc = (A_UINT16 *)((A_UCHAR *)eeprom_data + 0x04);
    }
    else
    {
        eeprom_size = 768;
        ptr_crc = (A_UINT16 *)((A_UCHAR *)eeprom_data + 0x04);
    }


    // Clear the crc
    *ptr_crc = 0;

    // Recalculate new CRC
    checksum = 0;
    ptr16_eeprom = (A_UINT16 *)eeprom_data;
    for (i = 0;i < eeprom_size; i += 2)
    {
        checksum = checksum ^ (*ptr16_eeprom);
        ptr16_eeprom++;
    }
    checksum = 0xFFFF ^ checksum;
    *ptr_crc = checksum;
}

static void 
ar6000_softmac_update(AR_SOFTC_T *ar, A_UCHAR *eeprom_data, size_t size)
{
    const char *source = "random generated";
    const struct firmware *softmac_entry;
    A_UCHAR *ptr_mac;
    switch (ar->arTargetType) {
    case TARGET_TYPE_AR6002:
        ptr_mac = (A_UINT8 *)((A_UCHAR *)eeprom_data + AR6002_MAC_ADDRESS_OFFSET);
        break;
    case TARGET_TYPE_AR6003:
        ptr_mac = (A_UINT8 *)((A_UCHAR *)eeprom_data + AR6003_MAC_ADDRESS_OFFSET);
        break;
    default:
	AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Invalid Target Type\n"));
        return;
    }
	printk(KERN_DEBUG "MAC from EEPROM %pM\n", ptr_mac);

    /* create a random MAC in case we cannot read file from system */
    ptr_mac[0] = 0;
    ptr_mac[1] = 0x03;
    ptr_mac[2] = 0x7F;
    ptr_mac[3] = random32() & 0xff; 
    ptr_mac[4] = random32() & 0xff; 
    ptr_mac[5] = random32() & 0xff; 
    if ((A_REQUEST_FIRMWARE(&softmac_entry, "softmac", ((struct device *)ar->osDevInfo.pOSDevice))) == 0)
    {
        A_CHAR *macbuf = A_MALLOC_NOWAIT(softmac_entry->size+1);
        if (macbuf) {            
            unsigned int softmac[6];
            memcpy(macbuf, softmac_entry->data, softmac_entry->size);
            macbuf[softmac_entry->size] = '\0';
            if (sscanf(macbuf, "%02x:%02x:%02x:%02x:%02x:%02x", 
                        &softmac[0], &softmac[1], &softmac[2],
                        &softmac[3], &softmac[4], &softmac[5])==6) {
                int i;
                for (i=0; i<6; ++i) {
                    ptr_mac[i] = softmac[i] & 0xff;
                }
                source = "softmac file";
            }
            A_FREE(macbuf);
        }
        A_RELEASE_FIRMWARE(softmac_entry);
    }
	printk(KERN_DEBUG "MAC from %s %pM\n", source, ptr_mac);
   calculate_crc(ar->arTargetType, eeprom_data);
}
#endif /* SOFTMAC_FILE_USED */

static A_STATUS
ar6000_transfer_bin_file(AR_SOFTC_T *ar, AR6K_BIN_FILE file, A_UINT32 address, A_BOOL compressed)
{
    A_STATUS status;
    const char *filename;
    const struct firmware *fw_entry;
    A_UINT32 fw_entry_size;

    switch (file) {
        case AR6K_OTP_FILE:
            if (ar->arVersion.target_ver == AR6003_REV1_VERSION) {
                filename = AR6003_REV1_OTP_FILE;
            } else if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                filename = AR6003_REV2_OTP_FILE;
            } else {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown firmware revision: %d\n", ar->arVersion.target_ver));
                return A_ERROR;
            }
            break;

        case AR6K_FIRMWARE_FILE:
            if (ar->arVersion.target_ver == AR6003_REV1_VERSION) {
                filename = AR6003_REV1_FIRMWARE_FILE;
            } else if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                filename = AR6003_REV2_FIRMWARE_FILE;
            } else {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown firmware revision: %d\n", ar->arVersion.target_ver));
                return A_ERROR;
            }
            
            if (eppingtest) {
                bypasswmi = TRUE;    
                if (ar->arVersion.target_ver == AR6003_REV1_VERSION) {
                    filename = AR6003_REV1_EPPING_FIRMWARE_FILE;
                } else if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                    filename = AR6003_REV2_EPPING_FIRMWARE_FILE;
                } else {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("eppingtest : unsupported firmware revision: %d\n", 
                        ar->arVersion.target_ver));
                    return A_ERROR;
                }
                compressed = 0;
            }
            
#ifdef CONFIG_HOST_TCMD_SUPPORT
            if(testmode) {
                if (ar->arVersion.target_ver == AR6003_REV1_VERSION) {
                    filename = AR6003_REV1_TCMD_FIRMWARE_FILE;
                } else if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                    filename = AR6003_REV2_TCMD_FIRMWARE_FILE;
                } else {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown firmware revision: %d\n", ar->arVersion.target_ver));
                    return A_ERROR;
                }
                compressed = 0;
            }
#endif 
#ifdef HTC_RAW_INTERFACE
            if (!eppingtest && bypasswmi) {
                if (ar->arVersion.target_ver == AR6003_REV1_VERSION) {
                    filename = AR6003_REV1_ART_FIRMWARE_FILE;
                } else if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                    filename = AR6003_REV2_ART_FIRMWARE_FILE;
                } else {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown firmware revision: %d\n", ar->arVersion.target_ver));
                    return A_ERROR;
                }
                compressed = 0;                
            }
#endif 
            break;

        case AR6K_PATCH_FILE:
            if (ar->arVersion.target_ver == AR6003_REV1_VERSION) {
                filename = AR6003_REV1_PATCH_FILE;
            } else if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                filename = AR6003_REV2_PATCH_FILE;
            } else {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown firmware revision: %d\n", ar->arVersion.target_ver));
                return A_ERROR;
            }
            break;

        case AR6K_BOARD_DATA_FILE:
            if (ar->arVersion.target_ver == AR6003_REV1_VERSION) {
                filename = AR6003_REV1_BOARD_DATA_FILE;
            } else if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                filename = AR6003_REV2_BOARD_DATA_FILE;
            } else {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown firmware revision: %d\n", ar->arVersion.target_ver));
                return A_ERROR;
            }
            break;

        default:
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown file type: %d\n", file));
            return A_ERROR;
    }
    if ((A_REQUEST_FIRMWARE(&fw_entry, filename, ((struct device *)ar->osDevInfo.pOSDevice))) != 0)
    {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Failed to get %s\n", filename));
        return A_ENOENT;
    }

#ifdef SOFTMAC_FILE_USED
    if (file==AR6K_BOARD_DATA_FILE && fw_entry->data) {
        ar6000_softmac_update(ar, (A_UCHAR *)fw_entry->data, fw_entry->size);
    }
#endif 


    fw_entry_size = fw_entry->size;

    /* Load extended board data for AR6003 */
    if ((file==AR6K_BOARD_DATA_FILE) && (fw_entry->data)) {
        A_UINT32 board_ext_address;
        A_UINT32 board_ext_data_size;
        A_UINT32 board_data_size;

        board_ext_data_size = (((ar)->arTargetType == TARGET_TYPE_AR6002) ? AR6002_BOARD_EXT_DATA_SZ : \
                               (((ar)->arTargetType == TARGET_TYPE_AR6003) ? AR6003_BOARD_EXT_DATA_SZ : 0));

        board_data_size = (((ar)->arTargetType == TARGET_TYPE_AR6002) ? AR6002_BOARD_DATA_SZ : \
                          (((ar)->arTargetType == TARGET_TYPE_AR6003) ? AR6003_BOARD_DATA_SZ : 0));
        
        /* Determine where in Target RAM to write Board Data */
        bmifn(BMIReadMemory(ar->arHifDevice, HOST_INTEREST_ITEM_ADDRESS(ar, hi_board_ext_data), (A_UCHAR *)&board_ext_address, 4));
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("Board extended Data download address: 0x%x\n", board_ext_address));

        /* check whether the target has allocated memory for extended board data and file contains extended board data */
        if ((board_ext_address) && (fw_entry->size == (board_data_size + board_ext_data_size))) {
            A_UINT32 param;

            status = BMIWriteMemory(ar->arHifDevice, board_ext_address, (A_UCHAR *)(((A_UINT32)fw_entry->data) + board_data_size), board_ext_data_size);

            if (status != A_OK) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("BMI operation failed: %d\n", __LINE__));
                A_RELEASE_FIRMWARE(fw_entry);
                return A_ERROR;
            }

            /* Record the fact that extended board Data IS initialized */
            param = 1;
            bmifn(BMIWriteMemory(ar->arHifDevice, HOST_INTEREST_ITEM_ADDRESS(ar, hi_board_ext_data_initialized), (A_UCHAR *)&param, 4));
        }
        fw_entry_size = board_data_size;
    }

    if (compressed) {
        status = BMIFastDownload(ar->arHifDevice, address, (A_UCHAR *)fw_entry->data, fw_entry_size);
    } else {
        status = BMIWriteMemory(ar->arHifDevice, address, (A_UCHAR *)fw_entry->data, fw_entry_size);
    }

    if (status != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("BMI operation failed: %d\n", __LINE__));
        A_RELEASE_FIRMWARE(fw_entry);
        return A_ERROR;
    }
    A_RELEASE_FIRMWARE(fw_entry);
    return A_OK;
}
#endif /* INIT_MODE_DRV_ENABLED */

A_STATUS
ar6000_update_bdaddr(AR_SOFTC_T *ar)
{

        if (setupbtdev != 0) {
            A_UINT32 address;

           if (BMIReadMemory(ar->arHifDevice,
           	HOST_INTEREST_ITEM_ADDRESS(ar, hi_board_data), (A_UCHAR *)&address, 4) != A_OK)
           {
    	      	AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIReadMemory for hi_board_data failed\n"));
           	return A_ERROR;
           }

           if (BMIReadMemory(ar->arHifDevice, address + BDATA_BDADDR_OFFSET, (A_UCHAR *)ar->bdaddr, 6) != A_OK)
           {
    	    	AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIReadMemory for BD address failed\n"));
           	return A_ERROR;
           }
	   AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BDADDR 0x%x:0x%x:0x%x:0x%x:0x%x:0x%x\n", ar->bdaddr[0],
								ar->bdaddr[1], ar->bdaddr[2], ar->bdaddr[3],
								ar->bdaddr[4], ar->bdaddr[5]));
        }

return A_OK;
}

A_STATUS
ar6000_sysfs_bmi_get_config(AR_SOFTC_T *ar, A_UINT32 mode)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("BMI: Requesting device specific configuration\n"));

    if (mode == WLAN_INIT_MODE_UDEV) {
        A_CHAR version[16];
        const struct firmware *fw_entry;

        /* Get config using udev through a script in user space */
        sprintf(version, "%2.2x", ar->arVersion.target_ver);
        if ((A_REQUEST_FIRMWARE(&fw_entry, version, ((struct device *)ar->osDevInfo.pOSDevice))) != 0)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("BMI: Failure to get configuration for target version: %s\n", version));
            return A_ERROR;
        }

        A_RELEASE_FIRMWARE(fw_entry);
#ifdef INIT_MODE_DRV_ENABLED
    } else {
        /* The config is contained within the driver itself */
        A_STATUS status;
        A_UINT32 param, options, sleep, address;

        /* Temporarily disable system sleep */
        address = MBOX_BASE_ADDRESS + LOCAL_SCRATCH_ADDRESS;
        bmifn(BMIReadSOCRegister(ar->arHifDevice, address, &param));
        options = param;
        param |= AR6K_OPTION_SLEEP_DISABLE;
        bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

        address = RTC_BASE_ADDRESS + SYSTEM_SLEEP_ADDRESS;
        bmifn(BMIReadSOCRegister(ar->arHifDevice, address, &param));
        sleep = param;
        param |= WLAN_SYSTEM_SLEEP_DISABLE_SET(1);
        bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("old options: %d, old sleep: %d\n", options, sleep));

        if (ar->arTargetType == TARGET_TYPE_AR6003) {
            /* Program analog PLL register */
            bmifn(BMIWriteSOCRegister(ar->arHifDevice, ANALOG_INTF_BASE_ADDRESS + 0x284, 0xF9104001));
            /* Run at 80/88MHz by default */
            param = CPU_CLOCK_STANDARD_SET(1);
        } else {
            /* Run at 40/44MHz by default */
            param = CPU_CLOCK_STANDARD_SET(0);
        }
        address = RTC_BASE_ADDRESS + CPU_CLOCK_ADDRESS;
        bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

        param = 0;
        if (ar->arTargetType == TARGET_TYPE_AR6002) {
            bmifn(BMIReadMemory(ar->arHifDevice, HOST_INTEREST_ITEM_ADDRESS(ar, hi_ext_clk_detected), (A_UCHAR *)&param, 4));
        }

        /* LPO_CAL.ENABLE = 1 if no external clk is detected */
        if (param != 1) {
            address = RTC_BASE_ADDRESS + LPO_CAL_ADDRESS;
            param = LPO_CAL_ENABLE_SET(1);
            bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));
        }

        /* Venus2.0: Lower SDIO pad drive strength,
         * temporary WAR to avoid SDIO CRC error */
        if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("AR6K: Temporary WAR to avoid SDIO CRC error\n"));
            param = 0x20;
            address = GPIO_BASE_ADDRESS + GPIO_PIN10_ADDRESS;
            bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

            address = GPIO_BASE_ADDRESS + GPIO_PIN11_ADDRESS;
            bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

            address = GPIO_BASE_ADDRESS + GPIO_PIN12_ADDRESS;
            bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

            address = GPIO_BASE_ADDRESS + GPIO_PIN13_ADDRESS;
            bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));
        }

#ifdef FORCE_INTERNAL_CLOCK
        /* Ignore external clock, if any, and force use of internal clock */
        if (ar->arTargetType == TARGET_TYPE_AR6003) {
            /* hi_ext_clk_detected = 0 */
            param = 0;
            bmifn(BMIWriteMemory(ar->arHifDevice, HOST_INTEREST_ITEM_ADDRESS(ar, hi_ext_clk_detected), (A_UCHAR *)&param, 4));

            /* CLOCK_CONTROL &= ~LF_CLK32 */
            address = RTC_BASE_ADDRESS + CLOCK_CONTROL_ADDRESS;
            bmifn(BMIReadSOCRegister(ar->arHifDevice, address, &param));
            param &= (~CLOCK_CONTROL_LF_CLK32_SET(1));
            bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));
        }
#endif /* FORCE_INTERNAL_CLOCK */

        /* Transfer Board Data from Target EEPROM to Target RAM */
        if (ar->arTargetType == TARGET_TYPE_AR6003) {
            /* Determine where in Target RAM to write Board Data */
            bmifn(BMIReadMemory(ar->arHifDevice, HOST_INTEREST_ITEM_ADDRESS(ar, hi_board_data), (A_UCHAR *)&address, 4));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("Board Data download address: 0x%x\n", address));

            /* Write EEPROM data to Target RAM */
            if ((ar6000_transfer_bin_file(ar, AR6K_BOARD_DATA_FILE, address, FALSE)) != A_OK) {
                return A_ERROR;
            }

            /* Record the fact that Board Data IS initialized */
            param = 1;
            bmifn(BMIWriteMemory(ar->arHifDevice, HOST_INTEREST_ITEM_ADDRESS(ar, hi_board_data_initialized), (A_UCHAR *)&param, 4));

            /* Transfer One time Programmable data */
            AR6K_DATA_DOWNLOAD_ADDRESS(address, ar->arVersion.target_ver);
            status = ar6000_transfer_bin_file(ar, AR6K_OTP_FILE, address, TRUE);
            if (status == A_OK) {
                /* Execute the OTP code */
                param = 0;
                AR6K_APP_START_OVERRIDE_ADDRESS(address, ar->arVersion.target_ver);
                bmifn(BMIExecute(ar->arHifDevice, address, &param));
            } else if (status != A_ENOENT) {
                return A_ERROR;
            } 
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Programming of board data for chip %d not supported\n", ar->arTargetType));
            return A_ERROR;
        }

        /* Download Target firmware */
        AR6K_DATA_DOWNLOAD_ADDRESS(address, ar->arVersion.target_ver);
        if ((ar6000_transfer_bin_file(ar, AR6K_FIRMWARE_FILE, address, TRUE)) != A_OK) {
            return A_ERROR;
        }

        /* Set starting address for firmware */
        AR6K_APP_START_OVERRIDE_ADDRESS(address, ar->arVersion.target_ver);
        bmifn(BMISetAppStart(ar->arHifDevice, address));

        /* Apply the patches */
        AR6K_PATCH_DOWNLOAD_ADDRESS(address, ar->arVersion.target_ver);
        if ((ar6000_transfer_bin_file(ar, AR6K_PATCH_FILE, address, FALSE)) != A_OK) {
            return A_ERROR;
        }

        param = address;
        bmifn(BMIWriteMemory(ar->arHifDevice, HOST_INTEREST_ITEM_ADDRESS(ar, hi_dset_list_head), (A_UCHAR *)&param, 4));

        if (ar->arTargetType == TARGET_TYPE_AR6003) {
            if (ar->arVersion.target_ver == AR6003_REV1_VERSION) {
                /* Reserve 5.5K of RAM */
                param = 5632;
            } else { /* AR6003_REV2_VERSION */
                /* Reserve 6.5K of RAM */
                param = 6656;
            }
            bmifn(BMIWriteMemory(ar->arHifDevice, HOST_INTEREST_ITEM_ADDRESS(ar, hi_end_RAM_reserve_sz), (A_UCHAR *)&param, 4));
        }

        /* Restore system sleep */
        address = RTC_BASE_ADDRESS + SYSTEM_SLEEP_ADDRESS;
        bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, sleep));

        address = MBOX_BASE_ADDRESS + LOCAL_SCRATCH_ADDRESS;
        param = options | 0x20;
        bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

        if (ar->arTargetType == TARGET_TYPE_AR6003) {
            /* Configure GPIO AR6003 UART */
#ifndef CONFIG_AR600x_DEBUG_UART_TX_PIN
#define CONFIG_AR600x_DEBUG_UART_TX_PIN 8
#endif
            param = CONFIG_AR600x_DEBUG_UART_TX_PIN;
            bmifn(BMIWriteMemory(ar->arHifDevice, HOST_INTEREST_ITEM_ADDRESS(ar, hi_dbg_uart_txpin), (A_UCHAR *)&param, 4));

#if (CONFIG_AR600x_DEBUG_UART_TX_PIN == 23)
            {
                address = GPIO_BASE_ADDRESS + CLOCK_GPIO_ADDRESS;
                bmifn(BMIReadSOCRegister(ar->arHifDevice, address, &param));
                param |= CLOCK_GPIO_BT_CLK_OUT_EN_SET(1);
                bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));
            }
#endif

            /* Configure GPIO for BT Reset */
#ifdef ATH6KL_CONFIG_GPIO_BT_RESET
#define CONFIG_AR600x_BT_RESET_PIN	0x16
            param = CONFIG_AR600x_BT_RESET_PIN;
            bmifn(BMIWriteMemory(ar->arHifDevice, HOST_INTEREST_ITEM_ADDRESS(ar, hi_hci_uart_support_pins), (A_UCHAR *)&param, 4));
#endif /* ATH6KL_CONFIG_GPIO_BT_RESET */

            /* Configure UART flow control polarity */
#ifndef CONFIG_ATH6KL_BT_UART_FC_POLARITY
#define CONFIG_ATH6KL_BT_UART_FC_POLARITY 0
#endif

#if (CONFIG_ATH6KL_BT_UART_FC_POLARITY == 1)
            if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                param = ((CONFIG_ATH6KL_BT_UART_FC_POLARITY << 1) & 0x2);
                bmifn(BMIWriteMemory(ar->arHifDevice, HOST_INTEREST_ITEM_ADDRESS(ar, hi_hci_uart_pwr_mgmt_params), (A_UCHAR *)&param, 4));
            }
#endif /* CONFIG_ATH6KL_BT_UART_FC_POLARITY */
        }

#ifdef HTC_RAW_INTERFACE
        if (!eppingtest && bypasswmi) {
            /* Don't run BMIDone for ART mode and force resetok=0 */
            resetok = 0;
            msleep(1000);
        }
#endif /* HTC_RAW_INTERFACE */

#endif /* INIT_MODE_DRV_ENABLED */
    }

    return A_OK;
}

A_STATUS
ar6000_configure_target(AR_SOFTC_T *ar)
{
    A_UINT32 param;
    if (enableuartprint) {
        param = 1;
        if (BMIWriteMemory(ar->arHifDevice,
                           HOST_INTEREST_ITEM_ADDRESS(ar, hi_serial_enable),
                           (A_UCHAR *)&param,
                           4)!= A_OK)
        {
             AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIWriteMemory for enableuartprint failed \n"));
             return A_ERROR;
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Serial console prints enabled\n"));
    }

    /* Tell target which HTC version it is used*/
    param = HTC_PROTOCOL_VERSION;
    if (BMIWriteMemory(ar->arHifDevice,
                       HOST_INTEREST_ITEM_ADDRESS(ar, hi_app_host_interest),
                       (A_UCHAR *)&param,
                       4)!= A_OK)
    {
         AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIWriteMemory for htc version failed \n"));
         return A_ERROR;
    }

#ifdef CONFIG_HOST_TCMD_SUPPORT
    if(testmode) {
        ar->arTargetMode = AR6000_TCMD_MODE;
    }else {
        ar->arTargetMode = AR6000_WLAN_MODE;
    }
#endif
    if (enabletimerwar) {
        A_UINT32 param;

        if (BMIReadMemory(ar->arHifDevice,
            HOST_INTEREST_ITEM_ADDRESS(ar, hi_option_flag),
            (A_UCHAR *)&param,
            4)!= A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIReadMemory for enabletimerwar failed \n"));
            return A_ERROR;
        }

        param |= HI_OPTION_TIMER_WAR;

        if (BMIWriteMemory(ar->arHifDevice,
            HOST_INTEREST_ITEM_ADDRESS(ar, hi_option_flag),
            (A_UCHAR *)&param,
            4) != A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIWriteMemory for enabletimerwar failed \n"));
            return A_ERROR;
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Timer WAR enabled\n"));
    }

    /* set the firmware mode to STA/IBSS/AP */
    {
        A_UINT32 param;

        if (BMIReadMemory(ar->arHifDevice,
            HOST_INTEREST_ITEM_ADDRESS(ar, hi_option_flag),
            (A_UCHAR *)&param,
            4)!= A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIReadMemory for setting fwmode failed \n"));
            return A_ERROR;
        }

        param |= (fwmode << HI_OPTION_FW_MODE_SHIFT);

        if (BMIWriteMemory(ar->arHifDevice,
            HOST_INTEREST_ITEM_ADDRESS(ar, hi_option_flag),
            (A_UCHAR *)&param,
            4) != A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIWriteMemory for setting fwmode failed \n"));
            return A_ERROR;
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Firmware mode set\n"));
    }

#ifdef ATH6KL_DISABLE_TARGET_DBGLOGS
    {
        A_UINT32 param;

        if (BMIReadMemory(ar->arHifDevice,
            HOST_INTEREST_ITEM_ADDRESS(ar, hi_option_flag),
            (A_UCHAR *)&param,
            4)!= A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIReadMemory for disabling debug logs failed\n"));
            return A_ERROR;
        }

        param |= HI_OPTION_DISABLE_DBGLOG;

        if (BMIWriteMemory(ar->arHifDevice,
            HOST_INTEREST_ITEM_ADDRESS(ar, hi_option_flag),
            (A_UCHAR *)&param,
            4) != A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIWriteMemory for HI_OPTION_DISABLE_DBGLOG\n"));
            return A_ERROR;
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Firmware mode set\n"));
    }
#endif /* ATH6KL_DISABLE_TARGET_DBGLOGS */

    /* 
     * Hardcode the address use for the extended board data 
     * Ideally this should be pre-allocate by the OS at boot time
     * But since it is a new feature and board data is loaded 
     * at init time, we have to workaround this from host.
     * It is difficult to patch the firmware boot code,
     * but possible in theory.
     */
    if (ar->arTargetType == TARGET_TYPE_AR6003) {
        param = AR6003_BOARD_EXT_DATA_ADDRESS; 
        if (BMIWriteMemory(ar->arHifDevice,
            HOST_INTEREST_ITEM_ADDRESS(ar, hi_board_ext_data),
            (A_UCHAR *)&param,
            4) != A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIWriteMemory for hi_board_ext_data failed \n"));
            return A_ERROR;
        }
    }


        /* since BMIInit is called in the driver layer, we have to set the block
         * size here for the target */

    if (A_FAILED(ar6000_set_htc_params(ar->arHifDevice,
                                       ar->arTargetType,
                                       mbox_yield_limit,
                                       0 /* use default number of control buffers */
                                       ))) {
        return A_ERROR;
    }

    if (setupbtdev != 0) {
        if (A_FAILED(ar6000_set_hci_bridge_flags(ar->arHifDevice,
                                                 ar->arTargetType,
                                                 setupbtdev))) {
            return A_ERROR;
        }
    }
    return A_OK;
}

static void
init_netdev(struct net_device *dev, char *name)
{
    dev->netdev_ops = &ar6000_netdev_ops;
    dev->watchdog_timeo = AR6000_TX_TIMEOUT;
    dev->wireless_handlers = &ath_iw_handler_def;

    ath_iw_handler_def.get_wireless_stats = ar6000_get_iwstats; /*Displayed via proc fs */

   /*
    * We need the OS to provide us with more headroom in order to
    * perform dix to 802.3, WMI header encap, and the HTC header
    */
    if (processDot11Hdr) {
        dev->hard_header_len = sizeof(struct ieee80211_qosframe) + sizeof(ATH_LLC_SNAP_HDR) + sizeof(WMI_DATA_HDR) + HTC_HEADER_LEN + WMI_MAX_TX_META_SZ + LINUX_HACK_FUDGE_FACTOR;
    } else {
        dev->hard_header_len = ETH_HLEN + sizeof(ATH_LLC_SNAP_HDR) +
            sizeof(WMI_DATA_HDR) + HTC_HEADER_LEN + WMI_MAX_TX_META_SZ + LINUX_HACK_FUDGE_FACTOR;
    }

    if (name[0])
    {
        strcpy(dev->name, name);
    }

#ifdef SET_MODULE_OWNER
    SET_MODULE_OWNER(dev);
#endif

#ifdef CONFIG_CHECKSUM_OFFLOAD
    if(csumOffload){
        dev->features |= NETIF_F_IP_CSUM; /*advertise kernel capability to do TCP/UDP CSUM offload for IPV4*/
    }
#endif

    return;
}

/*
 * HTC Event handlers
 */
static A_STATUS
ar6000_avail_ev(void *context, void *hif_handle)
{
    int i;
    struct net_device *dev;
    void *ar_netif;
    AR_SOFTC_T *ar;
    int device_index = 0;
    HTC_INIT_INFO  htcInfo;
#ifdef ATH6K_CONFIG_CFG80211
    struct wireless_dev *wdev;
#endif /* ATH6K_CONFIG_CFG80211 */
    A_STATUS init_status = A_OK;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("ar6000_available\n"));

    for (i=0; i < MAX_AR6000; i++) {
        if (ar6000_devices[i] == NULL) {
            break;
        }
    }

    if (i == MAX_AR6000) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_available: max devices reached\n"));
        return A_ERROR;
    }

    /* Save this. It gives a bit better readability especially since */
    /* we use another local "i" variable below.                      */
    device_index = i;

#ifdef ATH6K_CONFIG_CFG80211
    wdev = ar6k_cfg80211_init(NULL);
    if (IS_ERR(wdev)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: ar6k_cfg80211_init failed\n", __func__));
        return A_ERROR;
    }
    ar_netif = wdev_priv(wdev);
#else
    dev = alloc_etherdev(sizeof(AR_SOFTC_T));
    if (dev == NULL) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_available: can't alloc etherdev\n"));
        return A_ERROR;
    }
    ether_setup(dev);
    ar_netif = ar6k_priv(dev);
#endif /* ATH6K_CONFIG_CFG80211 */

    if (ar_netif == NULL) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Can't allocate ar6k priv memory\n", __func__));
        return A_ERROR;
    }

    A_MEMZERO(ar_netif, sizeof(AR_SOFTC_T));
    ar = (AR_SOFTC_T *)ar_netif;

#ifdef ATH6K_CONFIG_CFG80211
    ar->wdev = wdev;
    wdev->iftype = NL80211_IFTYPE_STATION;

    dev = alloc_netdev_mq(0, "wlan%d", ether_setup, 1);
    if (!dev) {
        printk(KERN_CRIT "AR6K: no memory for network device instance\n");
        ar6k_cfg80211_deinit(ar);
        return A_ERROR;
    }

    dev->ieee80211_ptr = wdev;
    SET_NETDEV_DEV(dev, wiphy_dev(wdev->wiphy));
    wdev->netdev = dev;
    ar->arNetworkType = INFRA_NETWORK;
#endif /* ATH6K_CONFIG_CFG80211 */

    init_netdev(dev, ifname);

#ifdef SET_NETDEV_DEV
    if (ar_netif) { 
        HIF_DEVICE_OS_DEVICE_INFO osDevInfo;
        A_MEMZERO(&osDevInfo, sizeof(osDevInfo));
        if ( A_SUCCESS( HIFConfigureDevice(hif_handle, HIF_DEVICE_GET_OS_DEVICE,
                        &osDevInfo, sizeof(osDevInfo))) ) {
            SET_NETDEV_DEV(dev, osDevInfo.pOSDevice);
        }
    }
#endif 

    ar->arNetDev             = dev;
    ar->arHifDevice          = hif_handle;
    ar->arWlanState          = WLAN_ENABLED;
    ar->arDeviceIndex        = device_index;

    ar->arWlanPowerState     = WLAN_POWER_STATE_ON;
    ar->arWlanOff            = FALSE;   /* We are in ON state */
#ifdef CONFIG_PM
    ar->arWowState           = WLAN_WOW_STATE_NONE;
    ar->arBTOff              = TRUE;   /* BT chip assumed to be OFF */
    ar->arBTSharing          = WLAN_CONFIG_BT_SHARING; 
    ar->arWlanOffConfig      = WLAN_CONFIG_WLAN_OFF;
    ar->arSuspendConfig      = WLAN_CONFIG_PM_SUSPEND;
    ar->arWow2Config         = WLAN_CONFIG_PM_WOW2;
#endif /* CONFIG_PM */

    A_INIT_TIMER(&ar->arHBChallengeResp.timer, ar6000_detect_error, dev);
    ar->arHBChallengeResp.seqNum = 0;
    ar->arHBChallengeResp.outstanding = FALSE;
    ar->arHBChallengeResp.missCnt = 0;
    ar->arHBChallengeResp.frequency = AR6000_HB_CHALLENGE_RESP_FREQ_DEFAULT;
    ar->arHBChallengeResp.missThres = AR6000_HB_CHALLENGE_RESP_MISS_THRES_DEFAULT;

    ar6000_init_control_info(ar);
    init_waitqueue_head(&arEvent);
    sema_init(&ar->arSem, 1);
    ar->bIsDestroyProgress = FALSE;

    INIT_HTC_PACKET_QUEUE(&ar->amsdu_rx_buffer_queue);

#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
    A_INIT_TIMER(&aptcTimer, aptcTimerHandler, ar);
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */

    A_INIT_TIMER(&ar->disconnect_timer, disconnect_timer_handler, dev);

    BMIInit();

    if (bmienable) {
        ar6000_sysfs_bmi_init(ar);
    }

    {
        struct bmi_target_info targ_info;

        if (BMIGetTargetInfo(ar->arHifDevice, &targ_info) != A_OK) {
            init_status = A_ERROR;
            goto avail_ev_failed;
        }

        ar->arVersion.target_ver = targ_info.target_ver;
        ar->arTargetType = targ_info.target_type;

            /* do any target-specific preparation that can be done through BMI */
        if (ar6000_prepare_target(ar->arHifDevice,
                                  targ_info.target_type,
                                  targ_info.target_ver) != A_OK) {
            init_status = A_ERROR;
            goto avail_ev_failed;
        }

    }

    if (ar6000_configure_target(ar) != A_OK) {
            init_status = A_ERROR;
            goto avail_ev_failed;
    }

    A_MEMZERO(&htcInfo,sizeof(htcInfo));
    htcInfo.pContext = ar;
    htcInfo.TargetFailure = ar6000_target_failure;

    ar->arHtcTarget = HTCCreate(ar->arHifDevice,&htcInfo);

    if (ar->arHtcTarget == NULL) {
        init_status = A_ERROR;
        goto avail_ev_failed;
    }

    spin_lock_init(&ar->arLock);

#ifdef WAPI_ENABLE
    ar->arWapiEnable = 0;
#endif


#ifdef CONFIG_CHECKSUM_OFFLOAD
    if(csumOffload){
        /*if external frame work is also needed, change and use an extended rxMetaVerion*/
        ar->rxMetaVersion=WMI_META_VERSION_2;
    }
#endif

#ifdef ATH_AR6K_11N_SUPPORT
    if((ar->aggr_cntxt = aggr_init(ar6000_alloc_netbufs)) == NULL) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s() Failed to initialize aggr.\n", __func__));
            init_status = A_ERROR;
            goto avail_ev_failed;
    }

    aggr_register_rx_dispatcher(ar->aggr_cntxt, (void *)dev, ar6000_deliver_frames_to_nw_stack);
#endif

    HIFClaimDevice(ar->arHifDevice, ar);

    /* We only register the device in the global list if we succeed. */
    /* If the device is in the global list, it will be destroyed     */
    /* when the module is unloaded.                                  */
    ar6000_devices[device_index] = dev;

    /* Don't install the init function if BMI is requested */
    if (!bmienable) {
        ar6000_netdev_ops.ndo_init = ar6000_init;
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("BMI enabled: %d\n", wlaninitmode));
        if ((wlaninitmode == WLAN_INIT_MODE_UDEV) ||
            (wlaninitmode == WLAN_INIT_MODE_DRV))
        {
            A_STATUS status = A_OK;
            do {
                if ((status = ar6000_sysfs_bmi_get_config(ar, wlaninitmode)) != A_OK)
                {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_avail: ar6000_sysfs_bmi_get_config failed\n"));
                    break;
                }
#ifdef HTC_RAW_INTERFACE
                break; /* Don't call ar6000_init for ART */
#endif 
                rtnl_lock();
                status = (ar6000_init(dev)==0) ? A_OK : A_ERROR;
                rtnl_unlock();
                if (status != A_OK) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_avail: ar6000_init\n"));
                }
            } while (FALSE);

            if (status != A_OK) {
                init_status = status;
                goto avail_ev_failed;
            }
        }
    }

    /* This runs the init function if registered */
    if (register_netdev(dev)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_avail: register_netdev failed\n"));
        ar6000_destroy(dev, 0);
        return A_ERROR;
    }

	is_netdev_registered = 1;

#ifdef CONFIG_AP_VIRTUAL_ADAPTER_SUPPORT
    arApNetDev = NULL;
#endif /* CONFIG_AP_VIRTUAL_ADAPTER_SUPPORT */
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("ar6000_avail: name=%s hifdevice=0x%lx, dev=0x%lx (%d), ar=0x%lx\n",
                    dev->name, (unsigned long)ar->arHifDevice, (unsigned long)dev, device_index,
                    (unsigned long)ar));

avail_ev_failed :
    if (A_FAILED(init_status)) {
        if (bmienable) { 
            ar6000_sysfs_bmi_deinit(ar);  
        }
    }

    return init_status;
}

static void ar6000_target_failure(void *Instance, A_STATUS Status)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)Instance;
    WMI_TARGET_ERROR_REPORT_EVENT errEvent;
    static A_BOOL sip = FALSE;

    if (Status != A_OK) {

        printk(KERN_ERR "ar6000_target_failure: target asserted \n");

        if (timer_pending(&ar->arHBChallengeResp.timer)) {
            A_UNTIMEOUT(&ar->arHBChallengeResp.timer);
        }

        /* try dumping target assertion information (if any) */
        ar6000_dump_target_assert_info(ar->arHifDevice,ar->arTargetType);

        /*
         * Fetch the logs from the target via the diagnostic
         * window.
         */
        ar6000_dbglog_get_debug_logs(ar);

        /* Report the error only once */
        if (!sip) {
            sip = TRUE;
            errEvent.errorVal = WMI_TARGET_COM_ERR |
                                WMI_TARGET_FATAL_ERR;
            ar6000_send_event_to_app(ar, WMI_ERROR_REPORT_EVENTID,
                                     (A_UINT8 *)&errEvent,
                                     sizeof(WMI_TARGET_ERROR_REPORT_EVENT));
        }
    }
}

static A_STATUS
ar6000_unavail_ev(void *context, void *hif_handle)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)context;
        /* NULL out it's entry in the global list */
    ar6000_devices[ar->arDeviceIndex] = NULL;
    ar6000_destroy(ar->arNetDev, 1);

    return A_OK;
}

void
ar6000_restart_endpoint(struct net_device *dev)
{
    A_STATUS status = A_OK;
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);

    BMIInit();
    do {
        if ( (status=ar6000_configure_target(ar))!=A_OK)
            break;
        if ( (status=ar6000_sysfs_bmi_get_config(ar, wlaninitmode)) != A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_avail: ar6000_sysfs_bmi_get_config failed\n"));
            break;
        }
        rtnl_lock();
        status = (ar6000_init(dev)==0) ? A_OK : A_ERROR;
        rtnl_unlock();

        if (status!=A_OK) {
            break;
        }
        if (ar->arSsidLen && ar->arWlanState == WLAN_ENABLED) {
            ar6000_connect_to_ap(ar);
        }  
    } while (0);

    if (status==A_OK) {
        return;
    }

    ar6000_devices[ar->arDeviceIndex] = NULL;
    ar6000_destroy(ar->arNetDev, 1);
}

void
ar6000_stop_endpoint(struct net_device *dev, A_BOOL keepprofile, A_BOOL getdbglogs)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);

    /* Stop the transmit queues */
    netif_stop_queue(dev);

    /* Disable the target and the interrupts associated with it */
    if (ar->arWmiReady == TRUE)
    {
        if (!bypasswmi)
        {
            if (ar->arConnected == TRUE || ar->arConnectPending == TRUE)
            {
                AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("%s(): Disconnect\n", __func__));
                if (!keepprofile) {
                    AR6000_SPIN_LOCK(&ar->arLock, 0);
                    ar6000_init_profile_info(ar);
                    AR6000_SPIN_UNLOCK(&ar->arLock, 0);
                }
                wmi_disconnect_cmd(ar->arWmi);
            }

            A_UNTIMEOUT(&ar->disconnect_timer);

            if (getdbglogs) {
                ar6000_dbglog_get_debug_logs(ar);
            }

            ar->arWmiReady  = FALSE;
            wmi_shutdown(ar->arWmi);
            ar->arWmiEnabled = FALSE;
            ar->arWmi = NULL;
            /* 
             * After wmi_shudown all WMI events will be dropped.
             * We need to cleanup the buffers allocated in AP mode
             * and give disconnect notification to stack, which usually
             * happens in the disconnect_event. 
             * Simulate the disconnect_event by calling the function directly.
             * Sometimes disconnect_event will be received when the debug logs 
             * are collected.
             */
            if (ar->arConnected == TRUE || ar->arConnectPending == TRUE) {
                if(ar->arNetworkType & AP_NETWORK) {
                    ar6000_disconnect_event(ar, DISCONNECT_CMD, bcast_mac, 0, NULL, 0);
                } else {
                    ar6000_disconnect_event(ar, DISCONNECT_CMD, ar->arBssid, 0, NULL, 0);
                }
                ar->arConnected = FALSE;
                ar->arConnectPending = FALSE;
            }
#ifdef USER_KEYS
            ar->user_savedkeys_stat = USER_SAVEDKEYS_STAT_INIT;
            ar->user_key_ctrl      = 0;
#endif
        }

         AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("%s(): WMI stopped\n", __func__));
    }
    else
    {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("%s(): WMI not ready 0x%lx 0x%lx\n",
            __func__, (unsigned long) ar, (unsigned long) ar->arWmi));

        /* Shut down WMI if we have started it */
        if(ar->arWmiEnabled == TRUE)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("%s(): Shut down WMI\n", __func__));
            wmi_shutdown(ar->arWmi);
            ar->arWmiEnabled = FALSE;
            ar->arWmi = NULL;
        }
    }

    if (ar->arHtcTarget != NULL) {
#ifdef EXPORT_HCI_BRIDGE_INTERFACE
        if (NULL != ar6kHciTransCallbacks.cleanupTransport) {
            ar6kHciTransCallbacks.cleanupTransport(NULL);
        }
#else
        // FIXME: workaround to reset BT's UART baud rate to default
        if (NULL != ar->exitCallback) {
            AR3K_CONFIG_INFO ar3kconfig;
            A_STATUS status;

            A_MEMZERO(&ar3kconfig,sizeof(ar3kconfig));
            ar6000_set_default_ar3kconfig(ar, (void *)&ar3kconfig);
            status = ar->exitCallback(&ar3kconfig);
            if (A_OK != status) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Failed to reset AR3K baud rate! \n"));
            }
        }
        // END workaround
        if (setuphci)
        	ar6000_cleanup_hci(ar);
#endif
#ifdef EXPORT_HCI_PAL_INTERFACE
        if (setuphcipal && (NULL != ar6kHciPalCallbacks_g.cleanupTransport)) {
           ar6kHciPalCallbacks_g.cleanupTransport(ar);
        }
#else
				/* cleanup hci pal driver data structures */
        if(setuphcipal)
          ar6k_cleanup_hci_pal(ar);
#endif
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,(" Shutting down HTC .... \n"));
        /* stop HTC */
        HTCStop(ar->arHtcTarget);
    }

    if (resetok) {
        /* try to reset the device if we can
         * The driver may have been configure NOT to reset the target during
         * a debug session */
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,(" Attempting to reset target on instance destroy.... \n"));
        if (ar->arHifDevice != NULL) {
            A_BOOL coldReset = (ar->arTargetType == TARGET_TYPE_AR6003) ? TRUE: FALSE;
            ar6000_reset_device(ar->arHifDevice, ar->arTargetType, TRUE, coldReset);
        }
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,(" Host does not want target reset. \n"));
    }
       /* Done with cookies */
    ar6000_cookie_cleanup(ar);
}
/*
 * We need to differentiate between the surprise and planned removal of the
 * device because of the following consideration:
 * - In case of surprise removal, the hcd already frees up the pending
 *   for the device and hence there is no need to unregister the function
 *   driver inorder to get these requests. For planned removal, the function
 *   driver has to explictly unregister itself to have the hcd return all the
 *   pending requests before the data structures for the devices are freed up.
 *   Note that as per the current implementation, the function driver will
 *   end up releasing all the devices since there is no API to selectively
 *   release a particular device.
 * - Certain commands issued to the target can be skipped for surprise
 *   removal since they will anyway not go through.
 */
void
ar6000_destroy(struct net_device *dev, unsigned int unregister)
{
    AR_SOFTC_T *ar;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("+ar6000_destroy \n"));
    
    if((dev == NULL) || ((ar = ar6k_priv(dev)) == NULL))
    {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s(): Failed to get device structure.\n", __func__));
        return;
    }

    ar->bIsDestroyProgress = TRUE;

    if (down_interruptible(&ar->arSem)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s(): down_interruptible failed \n", __func__));
        return;
    }

    if (ar->arWlanPowerState != WLAN_POWER_STATE_CUT_PWR) {
        /* only stop endpoint if we are not stop it in suspend_ev */
        ar6000_stop_endpoint(dev, FALSE, TRUE);
    } else {
        /* clear up the platform power state before rmmod */
        plat_setup_power(1,0);
    }

    ar->arWlanState = WLAN_DISABLED;
    if (ar->arHtcTarget != NULL) {
        /* destroy HTC */
        HTCDestroy(ar->arHtcTarget);
    }
    if (ar->arHifDevice != NULL) {
        /*release the device so we do not get called back on remove incase we
         * we're explicity destroyed by module unload */
        HIFReleaseDevice(ar->arHifDevice);
        HIFShutDownDevice(ar->arHifDevice);
    }
#ifdef ATH_AR6K_11N_SUPPORT
    aggr_module_destroy(ar->aggr_cntxt);
#endif

       /* Done with cookies */
    ar6000_cookie_cleanup(ar);

        /* cleanup any allocated AMSDU buffers */
    ar6000_cleanup_amsdu_rxbufs(ar);

    if (bmienable) {
        ar6000_sysfs_bmi_deinit(ar);
    }

    /* Cleanup BMI */
    BMICleanup();

    /* Clear the tx counters */
    memset(tx_attempt, 0, sizeof(tx_attempt));
    memset(tx_post, 0, sizeof(tx_post));
    memset(tx_complete, 0, sizeof(tx_complete));

#ifdef HTC_RAW_INTERFACE
    if (ar->arRawHtc) {
        A_FREE(ar->arRawHtc);
        ar->arRawHtc = NULL;
    }
#endif 
    /* Free up the device data structure */
    if (unregister && is_netdev_registered) {		
        unregister_netdev(dev);
        is_netdev_registered = 0;
    }
    free_netdev(dev);

#ifdef ATH6K_CONFIG_CFG80211
    ar6k_cfg80211_deinit(ar);
#endif /* ATH6K_CONFIG_CFG80211 */

#ifdef CONFIG_AP_VIRTUL_ADAPTER_SUPPORT
    ar6000_remove_ap_interface();
#endif /*CONFIG_AP_VIRTUAL_ADAPTER_SUPPORT */

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("-ar6000_destroy \n"));
}

static void disconnect_timer_handler(unsigned long ptr)
{
    struct net_device *dev = (struct net_device *)ptr;
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);

    A_UNTIMEOUT(&ar->disconnect_timer);

    ar6000_init_profile_info(ar);
    wmi_disconnect_cmd(ar->arWmi);
}

static void ar6000_detect_error(unsigned long ptr)
{
    struct net_device *dev = (struct net_device *)ptr;
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    WMI_TARGET_ERROR_REPORT_EVENT errEvent;

    AR6000_SPIN_LOCK(&ar->arLock, 0);

    if (ar->arHBChallengeResp.outstanding) {
        ar->arHBChallengeResp.missCnt++;
    } else {
        ar->arHBChallengeResp.missCnt = 0;
    }

    if (ar->arHBChallengeResp.missCnt > ar->arHBChallengeResp.missThres) {
        /* Send Error Detect event to the application layer and do not reschedule the error detection module timer */
        ar->arHBChallengeResp.missCnt = 0;
        ar->arHBChallengeResp.seqNum = 0;
        errEvent.errorVal = WMI_TARGET_COM_ERR | WMI_TARGET_FATAL_ERR;
        AR6000_SPIN_UNLOCK(&ar->arLock, 0);
        ar6000_send_event_to_app(ar, WMI_ERROR_REPORT_EVENTID,
                                 (A_UINT8 *)&errEvent,
                                 sizeof(WMI_TARGET_ERROR_REPORT_EVENT));
        return;
    }

    /* Generate the sequence number for the next challenge */
    ar->arHBChallengeResp.seqNum++;
    ar->arHBChallengeResp.outstanding = TRUE;

    AR6000_SPIN_UNLOCK(&ar->arLock, 0);

    /* Send the challenge on the control channel */
    if (wmi_get_challenge_resp_cmd(ar->arWmi, ar->arHBChallengeResp.seqNum, DRV_HB_CHALLENGE) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Unable to send heart beat challenge\n"));
    }


    /* Reschedule the timer for the next challenge */
    A_TIMEOUT_MS(&ar->arHBChallengeResp.timer, ar->arHBChallengeResp.frequency * 1000, 0);
}

void ar6000_init_profile_info(AR_SOFTC_T *ar)
{
    ar->arSsidLen            = 0;
    A_MEMZERO(ar->arSsid, sizeof(ar->arSsid));

    switch(fwmode) {
        case HI_OPTION_FW_MODE_IBSS:
            ar->arNetworkType = ar->arNextMode = ADHOC_NETWORK;
            break;
        case HI_OPTION_FW_MODE_BSS_STA:
            ar->arNetworkType = ar->arNextMode = INFRA_NETWORK;
            break;
        case HI_OPTION_FW_MODE_AP:
            ar->arNetworkType = ar->arNextMode = AP_NETWORK;
            break;
    }

    ar->arDot11AuthMode      = OPEN_AUTH;
    ar->arAuthMode           = NONE_AUTH;
    ar->arPairwiseCrypto     = NONE_CRYPT;
    ar->arPairwiseCryptoLen  = 0;
    ar->arGroupCrypto        = NONE_CRYPT;
    ar->arGroupCryptoLen     = 0;
    A_MEMZERO(ar->arWepKeyList, sizeof(ar->arWepKeyList));
    A_MEMZERO(ar->arReqBssid, sizeof(ar->arReqBssid));
    A_MEMZERO(ar->arBssid, sizeof(ar->arBssid));
    ar->arBssChannel = 0;
    ar->arConnected = FALSE;
}

static void
ar6000_init_control_info(AR_SOFTC_T *ar)
{
    ar->arWmiEnabled         = FALSE;
    ar6000_init_profile_info(ar);
    ar->arDefTxKeyIndex      = 0;
    A_MEMZERO(ar->arWepKeyList, sizeof(ar->arWepKeyList));
    ar->arChannelHint        = 0;
    ar->arListenIntervalT    = A_DEFAULT_LISTEN_INTERVAL;
    ar->arListenIntervalB    = 0;
    ar->arVersion.host_ver   = AR6K_SW_VERSION;
    ar->arRssi               = 0;
    ar->arTxPwr              = 0;
    ar->arTxPwrSet           = FALSE;
    ar->arSkipScan           = 0;
    ar->arBeaconInterval     = 0;
    ar->arBitRate            = 0;
    ar->arMaxRetries         = 0;
    ar->arWmmEnabled         = TRUE;
    ar->intra_bss            = 1;
    ar->scan_triggered       = 0;
    A_MEMZERO(&ar->scParams, sizeof(ar->scParams));
    ar->scParams.shortScanRatio = WMI_SHORTSCANRATIO_DEFAULT;
    ar->scParams.scanCtrlFlags = DEFAULT_SCAN_CTRL_FLAGS;

    /* Initialize the AP mode state info */
    {
        A_UINT8 ctr;
        A_MEMZERO((A_UINT8 *)ar->sta_list, AP_MAX_NUM_STA * sizeof(sta_t));

        /* init the Mutexes */
        A_MUTEX_INIT(&ar->mcastpsqLock);

        /* Init the PS queues */
        for (ctr=0; ctr < AP_MAX_NUM_STA ; ctr++) {
            A_MUTEX_INIT(&ar->sta_list[ctr].psqLock);
            A_NETBUF_QUEUE_INIT(&ar->sta_list[ctr].psq);
        }

        ar->ap_profile_flag = 0;
        A_NETBUF_QUEUE_INIT(&ar->mcastpsq);

        A_MEMCPY(ar->ap_country_code, DEF_AP_COUNTRY_CODE, 3);
        ar->ap_wmode = DEF_AP_WMODE_G;
        ar->ap_dtim_period = DEF_AP_DTIM;
        ar->ap_beacon_interval = DEF_BEACON_INTERVAL;
    }
}

static int
ar6000_open(struct net_device *dev)
{
    unsigned long  flags;
    AR_SOFTC_T    *ar = (AR_SOFTC_T *)ar6k_priv(dev);

    spin_lock_irqsave(&ar->arLock, flags);

#ifdef ATH6K_CONFIG_CFG80211
    if(ar->arWlanState == WLAN_DISABLED) {
        ar->arWlanState = WLAN_ENABLED;
    }
#endif /* ATH6K_CONFIG_CFG80211 */

    if( ar->arConnected || bypasswmi) {
        netif_carrier_on(dev);
        /* Wake up the queues */
        netif_wake_queue(dev);
    }
    else
        netif_carrier_off(dev);

    spin_unlock_irqrestore(&ar->arLock, flags);
    return 0;
}

static int
ar6000_close(struct net_device *dev)
{
#ifdef ATH6K_CONFIG_CFG80211
    AR_SOFTC_T    *ar = (AR_SOFTC_T *)ar6k_priv(dev);
#endif /* ATH6K_CONFIG_CFG80211 */
    netif_stop_queue(dev);

#ifdef ATH6K_CONFIG_CFG80211
    AR6000_SPIN_LOCK(&ar->arLock, 0);
    if (ar->arConnected == TRUE || ar->arConnectPending == TRUE) {
        AR6000_SPIN_UNLOCK(&ar->arLock, 0);
        wmi_disconnect_cmd(ar->arWmi);
    } else {
        AR6000_SPIN_UNLOCK(&ar->arLock, 0);
    }

    if(ar->arWmiReady == TRUE) {
        if (wmi_scanparams_cmd(ar->arWmi, 0xFFFF, 0,
                               0, 0, 0, 0, 0, 0, 0, 0) != A_OK) {
            return -EIO;
        }
        ar->arWlanState = WLAN_DISABLED;
    }
#endif /* ATH6K_CONFIG_CFG80211 */

    return 0;
}

/* connect to a service */
static A_STATUS ar6000_connectservice(AR_SOFTC_T               *ar,
                                      HTC_SERVICE_CONNECT_REQ  *pConnect,
                                      char                     *pDesc)
{
    A_STATUS                 status;
    HTC_SERVICE_CONNECT_RESP response;

    do {

        A_MEMZERO(&response,sizeof(response));

        status = HTCConnectService(ar->arHtcTarget,
                                   pConnect,
                                   &response);

        if (A_FAILED(status)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" Failed to connect to %s service status:%d \n",
                              pDesc, status));
            break;
        }
        switch (pConnect->ServiceID) {
            case WMI_CONTROL_SVC :
                if (ar->arWmiEnabled) {
                        /* set control endpoint for WMI use */
                    wmi_set_control_ep(ar->arWmi, response.Endpoint);
                }
                    /* save EP for fast lookup */
                ar->arControlEp = response.Endpoint;
                break;
            case WMI_DATA_BE_SVC :
                arSetAc2EndpointIDMap(ar, WMM_AC_BE, response.Endpoint);
                break;
            case WMI_DATA_BK_SVC :
                arSetAc2EndpointIDMap(ar, WMM_AC_BK, response.Endpoint);
                break;
            case WMI_DATA_VI_SVC :
                arSetAc2EndpointIDMap(ar, WMM_AC_VI, response.Endpoint);
                 break;
           case WMI_DATA_VO_SVC :
                arSetAc2EndpointIDMap(ar, WMM_AC_VO, response.Endpoint);
                break;
           default:
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ServiceID not mapped %d\n", pConnect->ServiceID));
                status = A_EINVAL;
            break;
        }

    } while (FALSE);

    return status;
}

void ar6000_TxDataCleanup(AR_SOFTC_T *ar)
{
        /* flush all the data (non-control) streams
         * we only flush packets that are tagged as data, we leave any control packets that
         * were in the TX queues alone */
    HTCFlushEndpoint(ar->arHtcTarget,
                     arAc2EndpointID(ar, WMM_AC_BE),
                     AR6K_DATA_PKT_TAG);
    HTCFlushEndpoint(ar->arHtcTarget,
                     arAc2EndpointID(ar, WMM_AC_BK),
                     AR6K_DATA_PKT_TAG);
    HTCFlushEndpoint(ar->arHtcTarget,
                     arAc2EndpointID(ar, WMM_AC_VI),
                     AR6K_DATA_PKT_TAG);
    HTCFlushEndpoint(ar->arHtcTarget,
                     arAc2EndpointID(ar, WMM_AC_VO),
                     AR6K_DATA_PKT_TAG);
}

HTC_ENDPOINT_ID
ar6000_ac2_endpoint_id ( void * devt, A_UINT8 ac)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *) devt;
    return(arAc2EndpointID(ar, ac));
}

A_UINT8
ar6000_endpoint_id2_ac(void * devt, HTC_ENDPOINT_ID ep )
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *) devt;
    return(arEndpoint2Ac(ar, ep ));
}

/* This function does one time initialization for the lifetime of the device */
int ar6000_init(struct net_device *dev)
{
    AR_SOFTC_T *ar;
    A_STATUS    status;
    A_INT32     timeleft;
    A_INT16     i;
    int         ret = 0;
#if defined(INIT_MODE_DRV_ENABLED) && defined(ENABLE_COEXISTENCE)
    WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMD sbcb_cmd;
    WMI_SET_BTCOEX_FE_ANT_CMD sbfa_cmd;
#endif /* INIT_MODE_DRV_ENABLED && ENABLE_COEXISTENCE */

    if((ar = ar6k_priv(dev)) == NULL)
    {
        return -EIO;
    }

    if (wlaninitmode == WLAN_INIT_MODE_USR || wlaninitmode == WLAN_INIT_MODE_DRV) {
    
        ar6000_update_bdaddr(ar);

        if (enablerssicompensation) {
            ar6000_copy_cust_data_from_target(ar->arHifDevice, ar->arTargetType);
            read_rssi_compensation_param(ar);
            for (i=-95; i<=0; i++) {
                rssi_compensation_table[0-i] = rssi_compensation_calc(ar,i);
            }
        }
    }

    dev_hold(dev);
    rtnl_unlock();

    /* Do we need to finish the BMI phase */
    if ((wlaninitmode == WLAN_INIT_MODE_USR || wlaninitmode == WLAN_INIT_MODE_DRV) && 
        (BMIDone(ar->arHifDevice) != A_OK))
    {
        ret = -EIO;
        goto ar6000_init_done;
    }

    if (!bypasswmi)
    {
#if 0 /* TBDXXX */
        if (ar->arVersion.host_ver != ar->arVersion.target_ver) {
            A_PRINTF("WARNING: Host version 0x%x does not match Target "
                    " version 0x%x!\n",
                    ar->arVersion.host_ver, ar->arVersion.target_ver);
        }
#endif

        /* Indicate that WMI is enabled (although not ready yet) */
        ar->arWmiEnabled = TRUE;
        if ((ar->arWmi = wmi_init((void *) ar)) == NULL)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s() Failed to initialize WMI.\n", __func__));
            ret = -EIO;
            goto ar6000_init_done;
        }

        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s() Got WMI @ 0x%lx.\n", __func__,
            (unsigned long) ar->arWmi));
    }

    do {
        HTC_SERVICE_CONNECT_REQ connect;

            /* the reason we have to wait for the target here is that the driver layer
             * has to init BMI in order to set the host block size,
             */
        status = HTCWaitTarget(ar->arHtcTarget);

        if (A_FAILED(status)) {
            break;
        }

        A_MEMZERO(&connect,sizeof(connect));
            /* meta data is unused for now */
        connect.pMetaData = NULL;
        connect.MetaDataLength = 0;
            /* these fields are the same for all service endpoints */
        connect.EpCallbacks.pContext = ar;
        connect.EpCallbacks.EpTxCompleteMultiple = ar6000_tx_complete;
        connect.EpCallbacks.EpRecv = ar6000_rx;
        connect.EpCallbacks.EpRecvRefill = ar6000_rx_refill;
        connect.EpCallbacks.EpSendFull = ar6000_tx_queue_full;
            /* set the max queue depth so that our ar6000_tx_queue_full handler gets called.
             * Linux has the peculiarity of not providing flow control between the
             * NIC and the network stack. There is no API to indicate that a TX packet
             * was sent which could provide some back pressure to the network stack.
             * Under linux you would have to wait till the network stack consumed all sk_buffs
             * before any back-flow kicked in. Which isn't very friendly.
             * So we have to manage this ourselves */
        connect.MaxSendQueueDepth = MAX_DEFAULT_SEND_QUEUE_DEPTH;
        connect.EpCallbacks.RecvRefillWaterMark = AR6000_MAX_RX_BUFFERS / 4; /* set to 25 % */
        if (0 == connect.EpCallbacks.RecvRefillWaterMark) {
            connect.EpCallbacks.RecvRefillWaterMark++;
        }
            /* connect to control service */
        connect.ServiceID = WMI_CONTROL_SVC;
        status = ar6000_connectservice(ar,
                                       &connect,
                                       "WMI CONTROL");
        if (A_FAILED(status)) {
            break;
        }

        connect.LocalConnectionFlags |= HTC_LOCAL_CONN_FLAGS_ENABLE_SEND_BUNDLE_PADDING;
            /* limit the HTC message size on the send path, although we can receive A-MSDU frames of
             * 4K, we will only send ethernet-sized (802.3) frames on the send path. */
        connect.MaxSendMsgSize = WMI_MAX_TX_DATA_FRAME_LENGTH;

            /* to reduce the amount of committed memory for larger A_MSDU frames, use the recv-alloc threshold
             * mechanism for larger packets */
        connect.EpCallbacks.RecvAllocThreshold = AR6000_BUFFER_SIZE;
        connect.EpCallbacks.EpRecvAllocThresh = ar6000_alloc_amsdu_rxbuf;

            /* for the remaining data services set the connection flag to reduce dribbling,
             * if configured to do so */
        if (reduce_credit_dribble) {
            connect.ConnectionFlags |= HTC_CONNECT_FLAGS_REDUCE_CREDIT_DRIBBLE;
            /* the credit dribble trigger threshold is (reduce_credit_dribble - 1) for a value
             * of 0-3 */
            connect.ConnectionFlags &= ~HTC_CONNECT_FLAGS_THRESHOLD_LEVEL_MASK;
            connect.ConnectionFlags |=
                        ((A_UINT16)reduce_credit_dribble - 1) & HTC_CONNECT_FLAGS_THRESHOLD_LEVEL_MASK;
        }
            /* connect to best-effort service */
        connect.ServiceID = WMI_DATA_BE_SVC;

        status = ar6000_connectservice(ar,
                                       &connect,
                                       "WMI DATA BE");
        if (A_FAILED(status)) {
            break;
        }

            /* connect to back-ground
             * map this to WMI LOW_PRI */
        connect.ServiceID = WMI_DATA_BK_SVC;
        status = ar6000_connectservice(ar,
                                       &connect,
                                       "WMI DATA BK");
        if (A_FAILED(status)) {
            break;
        }

            /* connect to Video service, map this to
             * to HI PRI */
        connect.ServiceID = WMI_DATA_VI_SVC;
        status = ar6000_connectservice(ar,
                                       &connect,
                                       "WMI DATA VI");
        if (A_FAILED(status)) {
            break;
        }

            /* connect to VO service, this is currently not
             * mapped to a WMI priority stream due to historical reasons.
             * WMI originally defined 3 priorities over 3 mailboxes
             * We can change this when WMI is reworked so that priorities are not
             * dependent on mailboxes */
        connect.ServiceID = WMI_DATA_VO_SVC;
        status = ar6000_connectservice(ar,
                                       &connect,
                                       "WMI DATA VO");
        if (A_FAILED(status)) {
            break;
        }

        A_ASSERT(arAc2EndpointID(ar,WMM_AC_BE) != 0);
        A_ASSERT(arAc2EndpointID(ar,WMM_AC_BK) != 0);
        A_ASSERT(arAc2EndpointID(ar,WMM_AC_VI) != 0);
        A_ASSERT(arAc2EndpointID(ar,WMM_AC_VO) != 0);

            /* setup access class priority mappings */
        ar->arAcStreamPriMap[WMM_AC_BK] = 0; /* lowest  */
        ar->arAcStreamPriMap[WMM_AC_BE] = 1; /*         */
        ar->arAcStreamPriMap[WMM_AC_VI] = 2; /*         */
        ar->arAcStreamPriMap[WMM_AC_VO] = 3; /* highest */

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
        if (setuphci && (NULL != ar6kHciTransCallbacks.setupTransport)) {
            HCI_TRANSPORT_MISC_HANDLES hciHandles;

            hciHandles.netDevice = ar->arNetDev;
            hciHandles.hifDevice = ar->arHifDevice;
            hciHandles.htcHandle = ar->arHtcTarget;
            status = (A_STATUS)(ar6kHciTransCallbacks.setupTransport(&hciHandles));
        }
#else
        if (setuphci) {
                /* setup HCI */
            status = ar6000_setup_hci(ar);
        }
#endif
#ifdef EXPORT_HCI_PAL_INTERFACE
        if (setuphcipal && (NULL != ar6kHciPalCallbacks_g.setupTransport))
          status = ar6kHciPalCallbacks_g.setupTransport(ar);
#else
        if(setuphcipal)
          status = ar6k_setup_hci_pal(ar);
#endif

    } while (FALSE);

    if (A_FAILED(status)) {
        ret = -EIO;
        goto ar6000_init_done;
    }

    /*
     * give our connected endpoints some buffers
     */

    ar6000_rx_refill(ar, ar->arControlEp);
    ar6000_rx_refill(ar, arAc2EndpointID(ar,WMM_AC_BE));

    /*
     * We will post the receive buffers only for SPE or endpoint ping testing so we are
     * making it conditional on the 'bypasswmi' flag.
     */
    if (bypasswmi) {
        ar6000_rx_refill(ar,arAc2EndpointID(ar,WMM_AC_BK));
        ar6000_rx_refill(ar,arAc2EndpointID(ar,WMM_AC_VI));
        ar6000_rx_refill(ar,arAc2EndpointID(ar,WMM_AC_VO));
    }

    /* allocate some buffers that handle larger AMSDU frames */
    ar6000_refill_amsdu_rxbufs(ar,AR6000_MAX_AMSDU_RX_BUFFERS);

        /* setup credit distribution */
    ar6000_setup_credit_dist(ar->arHtcTarget, &ar->arCreditStateInfo);

    /* Since cookies are used for HTC transports, they should be */
    /* initialized prior to enabling HTC.                        */
    ar6000_cookie_init(ar);

    /* start HTC */
    status = HTCStart(ar->arHtcTarget);

    if (status != A_OK) {
        if (ar->arWmiEnabled == TRUE) {
            wmi_shutdown(ar->arWmi);
            ar->arWmiEnabled = FALSE;
            ar->arWmi = NULL;
        }
        ar6000_cookie_cleanup(ar);
        ret = -EIO;
        goto ar6000_init_done;
    }

    if (!bypasswmi) {
        /* Wait for Wmi event to be ready */
        timeleft = wait_event_interruptible_timeout(arEvent,
            (ar->arWmiReady == TRUE), wmitimeout * HZ);

        if (ar->arVersion.abi_ver != AR6K_ABI_VERSION) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ABI Version mismatch: Host(0x%x), Target(0x%x)\n", AR6K_ABI_VERSION, ar->arVersion.abi_ver));
#ifndef ATH6K_SKIP_ABI_VERSION_CHECK
            ret = -EIO;
            goto ar6000_init_done;
#endif /* ATH6K_SKIP_ABI_VERSION_CHECK */
        }

        if(!timeleft || signal_pending(current))
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("WMI is not ready or wait was interrupted\n"));
            ret = -EIO;
            goto ar6000_init_done;
        }

        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s() WMI is ready\n", __func__));

        /* Communicate the wmi protocol verision to the target */
        if ((ar6000_set_host_app_area(ar)) != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Unable to set the host app area\n"));
        }

        /* configure the device for rx dot11 header rules 0,0 are the default values
         * therefore this command can be skipped if the inputs are 0,FALSE,FALSE.Required
         if checksum offload is needed. Set RxMetaVersion to 2*/
        if ((wmi_set_rx_frame_format_cmd(ar->arWmi,ar->rxMetaVersion, processDot11Hdr, processDot11Hdr)) != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Unable to set the rx frame format.\n"));
        }

#if defined(INIT_MODE_DRV_ENABLED) && defined(ENABLE_COEXISTENCE)
        /* Configure the type of BT collocated with WLAN */
        A_MEMZERO(&sbcb_cmd, sizeof(WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMD));
#ifdef CONFIG_AR600x_BT_QCOM
        sbcb_cmd.btcoexCoLocatedBTdev = 1;
#elif defined(CONFIG_AR600x_BT_CSR)
        sbcb_cmd.btcoexCoLocatedBTdev = 2;
#elif defined(CONFIG_AR600x_BT_AR3001)
        sbcb_cmd.btcoexCoLocatedBTdev = 3;
#else
#error Unsupported Bluetooth Type
#endif /* Collocated Bluetooth Type */

        if ((wmi_set_btcoex_colocated_bt_dev_cmd(ar->arWmi, &sbcb_cmd)) != A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Unable to set collocated BT type\n"));
        }

        /* Configure the type of BT collocated with WLAN */
        A_MEMZERO(&sbfa_cmd, sizeof(WMI_SET_BTCOEX_FE_ANT_CMD));
#ifdef CONFIG_AR600x_DUAL_ANTENNA
        sbfa_cmd.btcoexFeAntType = 2;
#elif defined(CONFIG_AR600x_SINGLE_ANTENNA)
        sbfa_cmd.btcoexFeAntType = 1;
#else
#error Unsupported Front-End Antenna Configuration
#endif /* AR600x Front-End Antenna Configuration */

        if ((wmi_set_btcoex_fe_ant_cmd(ar->arWmi, &sbfa_cmd)) != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Unable to set fornt end antenna configuration\n"));
        }
#endif /* INIT_MODE_DRV_ENABLED && ENABLE_COEXISTENCE */
    }

    ar->arNumDataEndPts = 1;

    if (bypasswmi) {
            /* for tests like endpoint ping, the MAC address needs to be non-zero otherwise
             * the data path through a raw socket is disabled */
        dev->dev_addr[0] = 0x00;
        dev->dev_addr[1] = 0x01;
        dev->dev_addr[2] = 0x02;
        dev->dev_addr[3] = 0xAA;
        dev->dev_addr[4] = 0xBB;
        dev->dev_addr[5] = 0xCC;
    }

ar6000_init_done:
    rtnl_lock();
    dev_put(dev);

    return ret;
}


void
ar6000_bitrate_rx(void *devt, A_INT32 rateKbps)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)devt;

    ar->arBitRate = rateKbps;
    wake_up(&arEvent);
}

void
ar6000_ratemask_rx(void *devt, A_UINT32 ratemask)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)devt;

    ar->arRateMask = ratemask;
    wake_up(&arEvent);
}

void
ar6000_txPwr_rx(void *devt, A_UINT8 txPwr)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)devt;

    ar->arTxPwr = txPwr;
    wake_up(&arEvent);
}


void
ar6000_channelList_rx(void *devt, A_INT8 numChan, A_UINT16 *chanList)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)devt;

    A_MEMCPY(ar->arChannelList, chanList, numChan * sizeof (A_UINT16));
    ar->arNumChannels = numChan;

    wake_up(&arEvent);
}

A_UINT8
ar6000_ibss_map_epid(struct sk_buff *skb, struct net_device *dev, A_UINT32 * mapNo)
{
    AR_SOFTC_T      *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    A_UINT8         *datap;
    ATH_MAC_HDR     *macHdr;
    A_UINT32         i, eptMap;

    (*mapNo) = 0;
    datap = A_NETBUF_DATA(skb);
    macHdr = (ATH_MAC_HDR *)(datap + sizeof(WMI_DATA_HDR));
    if (IEEE80211_IS_MULTICAST(macHdr->dstMac)) {
        return ENDPOINT_2;
    }

    eptMap = -1;
    for (i = 0; i < ar->arNodeNum; i ++) {
        if (IEEE80211_ADDR_EQ(macHdr->dstMac, ar->arNodeMap[i].macAddress)) {
            (*mapNo) = i + 1;
            ar->arNodeMap[i].txPending ++;
            return ar->arNodeMap[i].epId;
        }

        if ((eptMap == -1) && !ar->arNodeMap[i].txPending) {
            eptMap = i;
        }
    }

    if (eptMap == -1) {
        eptMap = ar->arNodeNum;
        ar->arNodeNum ++;
        A_ASSERT(ar->arNodeNum <= MAX_NODE_NUM);
    }

    A_MEMCPY(ar->arNodeMap[eptMap].macAddress, macHdr->dstMac, IEEE80211_ADDR_LEN);

    for (i = ENDPOINT_2; i <= ENDPOINT_5; i ++) {
        if (!ar->arTxPending[i]) {
            ar->arNodeMap[eptMap].epId = i;
            break;
        }
        // No free endpoint is available, start redistribution on the inuse endpoints.
        if (i == ENDPOINT_5) {
            ar->arNodeMap[eptMap].epId = ar->arNexEpId;
            ar->arNexEpId ++;
            if (ar->arNexEpId > ENDPOINT_5) {
                ar->arNexEpId = ENDPOINT_2;
            }
        }
    }

    (*mapNo) = eptMap + 1;
    ar->arNodeMap[eptMap].txPending ++;

    return ar->arNodeMap[eptMap].epId;
}

#ifdef DEBUG
static void ar6000_dump_skb(struct sk_buff *skb)
{
   u_char *ch;
   for (ch = A_NETBUF_DATA(skb);
        (unsigned long)ch < ((unsigned long)A_NETBUF_DATA(skb) +
        A_NETBUF_LEN(skb)); ch++)
    {
         AR_DEBUG_PRINTF(ATH_DEBUG_WARN,("%2.2x ", *ch));
    }
    AR_DEBUG_PRINTF(ATH_DEBUG_WARN,("\n"));
}
#endif

#ifdef HTC_TEST_SEND_PKTS
static void DoHTCSendPktsTest(AR_SOFTC_T *ar, int MapNo, HTC_ENDPOINT_ID eid, struct sk_buff *skb);
#endif

static int
ar6000_data_tx(struct sk_buff *skb, struct net_device *dev)
{
#define AC_NOT_MAPPED   99
    AR_SOFTC_T        *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    A_UINT8            ac = AC_NOT_MAPPED;
    HTC_ENDPOINT_ID    eid = ENDPOINT_UNUSED;
    A_UINT32          mapNo = 0;
    int               len;
    struct ar_cookie *cookie;
    A_BOOL            checkAdHocPsMapping = FALSE,bMoreData = FALSE;
    HTC_TX_TAG        htc_tag = AR6K_DATA_PKT_TAG;
    A_UINT8           dot11Hdr = processDot11Hdr;
#ifdef CONFIG_PM
    if (ar->arWowState != WLAN_WOW_STATE_NONE) {
        A_NETBUF_FREE(skb);
        return 0;
    }
#endif /* CONFIG_PM */

    AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_TX,("ar6000_data_tx start - skb=0x%lx, data=0x%lx, len=0x%x\n",
                     (unsigned long)skb, (unsigned long)A_NETBUF_DATA(skb),
                     A_NETBUF_LEN(skb)));

    /* If target is not associated */
    if( (!ar->arConnected && !bypasswmi)
#ifdef CONFIG_HOST_TCMD_SUPPORT
     /* TCMD doesnt support any data, free the buf and return */
    || (ar->arTargetMode == AR6000_TCMD_MODE)
#endif
                                            ) {
        A_NETBUF_FREE(skb);
        return 0;
    }

    do {

        if (ar->arWmiReady == FALSE && bypasswmi == 0) {
            break;
        }

#ifdef BLOCK_TX_PATH_FLAG
        if (blocktx) {
            break;
        }
#endif /* BLOCK_TX_PATH_FLAG */

        /* AP mode Power save processing */
        /* If the dst STA is in sleep state, queue the pkt in its PS queue */

        if (ar->arNetworkType == AP_NETWORK) {
            ATH_MAC_HDR *datap = (ATH_MAC_HDR *)A_NETBUF_DATA(skb);
            sta_t *conn = NULL;

            /* If the dstMac is a Multicast address & atleast one of the
             * associated STA is in PS mode, then queue the pkt to the
             * mcastq
             */
            if (IEEE80211_IS_MULTICAST(datap->dstMac)) {
                A_UINT8 ctr=0;
                A_BOOL qMcast=FALSE;


                for (ctr=0; ctr<AP_MAX_NUM_STA; ctr++) {
                    if (STA_IS_PWR_SLEEP((&ar->sta_list[ctr]))) {
                        qMcast = TRUE;
                    }
                }
                if(qMcast) {

                    /* If this transmit is not because of a Dtim Expiry q it */
                    if (ar->DTIMExpired == FALSE) {
                        A_BOOL isMcastqEmpty = FALSE;

                        A_MUTEX_LOCK(&ar->mcastpsqLock);
                        isMcastqEmpty = A_NETBUF_QUEUE_EMPTY(&ar->mcastpsq);
                        A_NETBUF_ENQUEUE(&ar->mcastpsq, skb);
                        A_MUTEX_UNLOCK(&ar->mcastpsqLock);

                        /* If this is the first Mcast pkt getting queued
                         * indicate to the target to set the BitmapControl LSB
                         * of the TIM IE.
                         */
                        if (isMcastqEmpty) {
                             wmi_set_pvb_cmd(ar->arWmi, MCAST_AID, 1);
                        }
                        return 0;
                    } else {
                     /* This transmit is because of Dtim expiry. Determine if
                      * MoreData bit has to be set.
                      */
                         A_MUTEX_LOCK(&ar->mcastpsqLock);
                         if(!A_NETBUF_QUEUE_EMPTY(&ar->mcastpsq)) {
                             bMoreData = TRUE;
                         }
                         A_MUTEX_UNLOCK(&ar->mcastpsqLock);
                    }
                }
            } else {
                conn = ieee80211_find_conn(ar, datap->dstMac);
                if (conn) {
                    if (STA_IS_PWR_SLEEP(conn)) {
                        /* If this transmit is not because of a PsPoll q it*/
                        if (!STA_IS_PS_POLLED(conn)) {
                            A_BOOL isPsqEmpty = FALSE;
                            /* Queue the frames if the STA is sleeping */
                            A_MUTEX_LOCK(&conn->psqLock);
                            isPsqEmpty = A_NETBUF_QUEUE_EMPTY(&conn->psq);
                            A_NETBUF_ENQUEUE(&conn->psq, skb);
                            A_MUTEX_UNLOCK(&conn->psqLock);

                            /* If this is the first pkt getting queued
                             * for this STA, update the PVB for this STA
                             */
                            if (isPsqEmpty) {
                                wmi_set_pvb_cmd(ar->arWmi, conn->aid, 1);
                            }

                            return 0;
                         } else {
                         /* This tx is because of a PsPoll. Determine if
                          * MoreData bit has to be set
                          */
                             A_MUTEX_LOCK(&conn->psqLock);
                             if (!A_NETBUF_QUEUE_EMPTY(&conn->psq)) {
                                 bMoreData = TRUE;
                             }
                             A_MUTEX_UNLOCK(&conn->psqLock);
                         }
                    }
                } else {

                    /* non existent STA. drop the frame */
                    A_NETBUF_FREE(skb);
                    return 0;
                }
            }
        }

        if (ar->arWmiEnabled) {
#ifdef CONFIG_CHECKSUM_OFFLOAD
        A_UINT8 csumStart=0;
        A_UINT8 csumDest=0;
        A_UINT8 csum=skb->ip_summed;
        if(csumOffload && (csum==CHECKSUM_PARTIAL)){
            csumStart=skb->csum_start-(skb->network_header-skb->head)+sizeof(ATH_LLC_SNAP_HDR);
            csumDest=skb->csum_offset+csumStart;
        }
#endif
            if (A_NETBUF_HEADROOM(skb) < dev->hard_header_len - LINUX_HACK_FUDGE_FACTOR) {
                struct sk_buff  *newbuf;

                /*
                 * We really should have gotten enough headroom but sometimes
                 * we still get packets with not enough headroom.  Copy the packet.
                 */
                len = A_NETBUF_LEN(skb);
                newbuf = A_NETBUF_ALLOC(len);
                if (newbuf == NULL) {
                    break;
                }
                A_NETBUF_PUT(newbuf, len);
                A_MEMCPY(A_NETBUF_DATA(newbuf), A_NETBUF_DATA(skb), len);
                A_NETBUF_FREE(skb);
                skb = newbuf;
                /* fall through and assemble header */
            }

            if (dot11Hdr) {
                if (wmi_dot11_hdr_add(ar->arWmi,skb,ar->arNetworkType) != A_OK) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_data_tx-wmi_dot11_hdr_add failed\n"));
                    break;
                }
            } else {
                if (wmi_dix_2_dot3(ar->arWmi, skb) != A_OK) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_data_tx - wmi_dix_2_dot3 failed\n"));
                    break;
                }
            }
#ifdef CONFIG_CHECKSUM_OFFLOAD
            if(csumOffload && (csum ==CHECKSUM_PARTIAL)){
                WMI_TX_META_V2  metaV2;
                metaV2.csumStart =csumStart;
                metaV2.csumDest = csumDest;
                metaV2.csumFlags = 0x1;/*instruct target to calculate checksum*/
                if (wmi_data_hdr_add(ar->arWmi, skb, DATA_MSGTYPE, bMoreData, dot11Hdr,
                                        WMI_META_VERSION_2,&metaV2) != A_OK) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_data_tx - wmi_data_hdr_add failed\n"));
                    break;
                }

            }
            else
#endif
            {
                if (wmi_data_hdr_add(ar->arWmi, skb, DATA_MSGTYPE, bMoreData, dot11Hdr,0,NULL) != A_OK) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_data_tx - wmi_data_hdr_add failed\n"));
                    break;
                }
            }


            if ((ar->arNetworkType == ADHOC_NETWORK) &&
                ar->arIbssPsEnable && ar->arConnected) {
                    /* flag to check adhoc mapping once we take the lock below: */
                checkAdHocPsMapping = TRUE;

            } else {
                    /* get the stream mapping */
                ac  =  wmi_implicit_create_pstream(ar->arWmi, skb, 0, ar->arWmmEnabled);
            }

        } else {
            EPPING_HEADER    *eppingHdr;

            eppingHdr = A_NETBUF_DATA(skb);

            if (IS_EPPING_PACKET(eppingHdr)) {
                    /* the stream ID is mapped to an access class */
                ac = eppingHdr->StreamNo_h;
                    /* some EPPING packets cannot be dropped no matter what access class it was
                     * sent on.  We can change the packet tag to guarantee it will not get dropped */
                if (IS_EPING_PACKET_NO_DROP(eppingHdr)) {
                    htc_tag = AR6K_CONTROL_PKT_TAG;
                }

                if (ac == HCI_TRANSPORT_STREAM_NUM) {
                        /* pass this to HCI */
#ifndef EXPORT_HCI_BRIDGE_INTERFACE
                    if (A_SUCCESS(hci_test_send(ar,skb))) {
                        return 0;
                    }
#endif
                        /* set AC to discard this skb */
                    ac = AC_NOT_MAPPED;
                } else {
                    /* a quirk of linux, the payload of the frame is 32-bit aligned and thus the addition
                     * of the HTC header will mis-align the start of the HTC frame, so we add some
                     * padding which will be stripped off in the target */
                    if (EPPING_ALIGNMENT_PAD > 0) {
                        A_NETBUF_PUSH(skb, EPPING_ALIGNMENT_PAD);
                    }
                }

            } else {
                    /* not a ping packet, drop it */
                ac = AC_NOT_MAPPED;
            }
        }

    } while (FALSE);

        /* did we succeed ? */
    if ((ac == AC_NOT_MAPPED) && !checkAdHocPsMapping) {
            /* cleanup and exit */
        A_NETBUF_FREE(skb);
        AR6000_STAT_INC(ar, tx_dropped);
        AR6000_STAT_INC(ar, tx_aborted_errors);
        return 0;
    }

    cookie = NULL;

        /* take the lock to protect driver data */
    AR6000_SPIN_LOCK(&ar->arLock, 0);

    do {

        if (checkAdHocPsMapping) {
            eid = ar6000_ibss_map_epid(skb, dev, &mapNo);
        }else {
            eid = arAc2EndpointID (ar, ac);
        }
            /* validate that the endpoint is connected */
        if (eid == 0 || eid == ENDPOINT_UNUSED ) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" eid %d is NOT mapped!\n", eid));
            break;
        }
            /* allocate resource for this packet */
        cookie = ar6000_alloc_cookie(ar);

        if (cookie != NULL) {
                /* update counts while the lock is held */
            ar->arTxPending[eid]++;
            ar->arTotalTxDataPending++;
        }

    } while (FALSE);

    AR6000_SPIN_UNLOCK(&ar->arLock, 0);

    if (cookie != NULL) {
        cookie->arc_bp[0] = (unsigned long)skb;
        cookie->arc_bp[1] = mapNo;
        SET_HTC_PACKET_INFO_TX(&cookie->HtcPkt,
                               cookie,
                               A_NETBUF_DATA(skb),
                               A_NETBUF_LEN(skb),
                               eid,
                               htc_tag);

#ifdef DEBUG
        if (debugdriver >= 3) {
            ar6000_dump_skb(skb);
        }
#endif
#ifdef HTC_TEST_SEND_PKTS
        DoHTCSendPktsTest(ar,mapNo,eid,skb);
#endif
            /* HTC interface is asynchronous, if this fails, cleanup will happen in
             * the ar6000_tx_complete callback */
        HTCSendPkt(ar->arHtcTarget, &cookie->HtcPkt);
    } else {
            /* no packet to send, cleanup */
        A_NETBUF_FREE(skb);
        AR6000_STAT_INC(ar, tx_dropped);
        AR6000_STAT_INC(ar, tx_aborted_errors);
    }

    return 0;
}

int
ar6000_acl_data_tx(struct sk_buff *skb, struct net_device *dev)
{
    AR_SOFTC_T        *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    struct ar_cookie *cookie;
    HTC_ENDPOINT_ID    eid = ENDPOINT_UNUSED;

    cookie = NULL;
    AR6000_SPIN_LOCK(&ar->arLock, 0);

        /* For now we send ACL on BE endpoint: We can also have a dedicated EP */
        eid = arAc2EndpointID (ar, 0);
        /* allocate resource for this packet */
        cookie = ar6000_alloc_cookie(ar);

        if (cookie != NULL) {
            /* update counts while the lock is held */
            ar->arTxPending[eid]++;
            ar->arTotalTxDataPending++;
        }


    AR6000_SPIN_UNLOCK(&ar->arLock, 0);

        if (cookie != NULL) {
            cookie->arc_bp[0] = (unsigned long)skb;
            cookie->arc_bp[1] = 0;
            SET_HTC_PACKET_INFO_TX(&cookie->HtcPkt,
                            cookie,
                            A_NETBUF_DATA(skb),
                            A_NETBUF_LEN(skb),
                            eid,
                            AR6K_DATA_PKT_TAG);

            /* HTC interface is asynchronous, if this fails, cleanup will happen in
             * the ar6000_tx_complete callback */
            HTCSendPkt(ar->arHtcTarget, &cookie->HtcPkt);
        } else {
            /* no packet to send, cleanup */
            A_NETBUF_FREE(skb);
            AR6000_STAT_INC(ar, tx_dropped);
            AR6000_STAT_INC(ar, tx_aborted_errors);
        }
    return 0;
}


#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
static void
tvsub(register struct timeval *out, register struct timeval *in)
{
    if((out->tv_usec -= in->tv_usec) < 0) {
        out->tv_sec--;
        out->tv_usec += 1000000;
    }
    out->tv_sec -= in->tv_sec;
}

void
applyAPTCHeuristics(AR_SOFTC_T *ar)
{
    A_UINT32 duration;
    A_UINT32 numbytes;
    A_UINT32 throughput;
    struct timeval ts;
    A_STATUS status;

    AR6000_SPIN_LOCK(&ar->arLock, 0);

    if ((enableAPTCHeuristics) && (!aptcTR.timerScheduled)) {
        do_gettimeofday(&ts);
        tvsub(&ts, &aptcTR.samplingTS);
        duration = ts.tv_sec * 1000 + ts.tv_usec / 1000; /* ms */
        numbytes = aptcTR.bytesTransmitted + aptcTR.bytesReceived;

        if (duration > APTC_TRAFFIC_SAMPLING_INTERVAL) {
            /* Initialize the time stamp and byte count */
            aptcTR.bytesTransmitted = aptcTR.bytesReceived = 0;
            do_gettimeofday(&aptcTR.samplingTS);

            /* Calculate and decide based on throughput thresholds */
            throughput = ((numbytes * 8) / duration);
            if (throughput > APTC_UPPER_THROUGHPUT_THRESHOLD) {
                /* Disable Sleep and schedule a timer */
                A_ASSERT(ar->arWmiReady == TRUE);
                AR6000_SPIN_UNLOCK(&ar->arLock, 0);
                status = wmi_powermode_cmd(ar->arWmi, MAX_PERF_POWER);
                AR6000_SPIN_LOCK(&ar->arLock, 0);
                A_TIMEOUT_MS(&aptcTimer, APTC_TRAFFIC_SAMPLING_INTERVAL, 0);
                aptcTR.timerScheduled = TRUE;
            }
        }
    }

    AR6000_SPIN_UNLOCK(&ar->arLock, 0);
}
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */

static HTC_SEND_FULL_ACTION ar6000_tx_queue_full(void *Context, HTC_PACKET *pPacket)
{
    AR_SOFTC_T     *ar = (AR_SOFTC_T *)Context;
    HTC_SEND_FULL_ACTION    action = HTC_SEND_FULL_KEEP;
    A_BOOL                  stopNet = FALSE;
    HTC_ENDPOINT_ID         Endpoint = HTC_GET_ENDPOINT_FROM_PKT(pPacket);

    do {

        if (bypasswmi) {
            int accessClass;

            if (HTC_GET_TAG_FROM_PKT(pPacket) == AR6K_CONTROL_PKT_TAG) {
                    /* don't drop special control packets */
                break;
            }

            accessClass = arEndpoint2Ac(ar,Endpoint);
                /* for endpoint ping testing drop Best Effort and Background */
            if ((accessClass == WMM_AC_BE) || (accessClass == WMM_AC_BK)) {
                action = HTC_SEND_FULL_DROP;
                stopNet = FALSE;
            } else {
                    /* keep but stop the netqueues */
                stopNet = TRUE;
            }
            break;
        }

        if (Endpoint == ar->arControlEp) {
                /* under normal WMI if this is getting full, then something is running rampant
                 * the host should not be exhausting the WMI queue with too many commands
                 * the only exception to this is during testing using endpointping */
            AR6000_SPIN_LOCK(&ar->arLock, 0);
                /* set flag to handle subsequent messages */
            ar->arWMIControlEpFull = TRUE;
            AR6000_SPIN_UNLOCK(&ar->arLock, 0);
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("WMI Control Endpoint is FULL!!! \n"));
                /* no need to stop the network */
            stopNet = FALSE;
            break;
        }

        /* if we get here, we are dealing with data endpoints getting full */

        if (HTC_GET_TAG_FROM_PKT(pPacket) == AR6K_CONTROL_PKT_TAG) {
            /* don't drop control packets issued on ANY data endpoint */
            break;
        }

        if (ar->arNetworkType == ADHOC_NETWORK) {
            /* in adhoc mode, we cannot differentiate traffic priorities so there is no need to
             * continue, however we should stop the network */
            stopNet = TRUE;
            break;
        }
        /* the last MAX_HI_COOKIE_NUM "batch" of cookies are reserved for the highest
         * active stream */
        if (ar->arAcStreamPriMap[arEndpoint2Ac(ar,Endpoint)] < ar->arHiAcStreamActivePri &&
            ar->arCookieCount <= MAX_HI_COOKIE_NUM) {
                /* this stream's priority is less than the highest active priority, we
                 * give preference to the highest priority stream by directing
                 * HTC to drop the packet that overflowed */
            action = HTC_SEND_FULL_DROP;
                /* since we are dropping packets, no need to stop the network */
            stopNet = FALSE;
            break;
        }

    } while (FALSE);

    if (stopNet) {
        AR6000_SPIN_LOCK(&ar->arLock, 0);
        ar->arNetQueueStopped = TRUE;
        AR6000_SPIN_UNLOCK(&ar->arLock, 0);
        /* one of the data endpoints queues is getting full..need to stop network stack
         * the queue will resume in ar6000_tx_complete() */
        netif_stop_queue(ar->arNetDev);
    }

    return action;
}


static void
ar6000_tx_complete(void *Context, HTC_PACKET_QUEUE *pPacketQueue)
{
    AR_SOFTC_T     *ar = (AR_SOFTC_T *)Context;
    A_UINT32        mapNo = 0;
    A_STATUS        status;
    struct ar_cookie * ar_cookie;
    HTC_ENDPOINT_ID   eid;
    A_BOOL          wakeEvent = FALSE;
    struct sk_buff_head  skb_queue;
    HTC_PACKET      *pPacket;
    struct sk_buff  *pktSkb;
    A_BOOL          flushing = FALSE;

    skb_queue_head_init(&skb_queue);

        /* lock the driver as we update internal state */
    AR6000_SPIN_LOCK(&ar->arLock, 0);

        /* reap completed packets */
    while (!HTC_QUEUE_EMPTY(pPacketQueue)) {

        pPacket = HTC_PACKET_DEQUEUE(pPacketQueue);

        ar_cookie = (struct ar_cookie *)pPacket->pPktContext;
        A_ASSERT(ar_cookie);

        status = pPacket->Status;
        pktSkb = (struct sk_buff *)ar_cookie->arc_bp[0];
        eid = pPacket->Endpoint;
        mapNo = ar_cookie->arc_bp[1];

        A_ASSERT(pktSkb);
        A_ASSERT(pPacket->pBuffer == A_NETBUF_DATA(pktSkb));

            /* add this to the list, use faster non-lock API */
        __skb_queue_tail(&skb_queue,pktSkb);

        if (A_SUCCESS(status)) {
            A_ASSERT(pPacket->ActualLength == A_NETBUF_LEN(pktSkb));
        }

        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_TX,("ar6000_tx_complete skb=0x%lx data=0x%lx len=0x%x eid=%d ",
                         (unsigned long)pktSkb, (unsigned long)pPacket->pBuffer,
                         pPacket->ActualLength,
                         eid));

        ar->arTxPending[eid]--;

        if ((eid  != ar->arControlEp) || bypasswmi) {
            ar->arTotalTxDataPending--;
        }

        if (eid == ar->arControlEp)
        {
            if (ar->arWMIControlEpFull) {
                    /* since this packet completed, the WMI EP is no longer full */
                ar->arWMIControlEpFull = FALSE;
            }

            if (ar->arTxPending[eid] == 0) {
                wakeEvent = TRUE;
            }
        }

        if (A_FAILED(status)) {
            if (status == A_ECANCELED) {
                    /* a packet was flushed  */
                flushing = TRUE;
            }
            AR6000_STAT_INC(ar, tx_errors);
            if (status != A_NO_RESOURCE) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s() -TX ERROR, status: 0x%x\n", __func__,
                            status));
            }
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_TX,("OK\n"));
            flushing = FALSE;
            AR6000_STAT_INC(ar, tx_packets);
            ar->arNetStats.tx_bytes += A_NETBUF_LEN(pktSkb);
#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
            aptcTR.bytesTransmitted += a_netbuf_to_len(pktSkb);
            applyAPTCHeuristics(ar);
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */
        }

        // TODO this needs to be looked at
        if ((ar->arNetworkType == ADHOC_NETWORK) && ar->arIbssPsEnable
            && (eid != ar->arControlEp) && mapNo)
        {
            mapNo --;
            ar->arNodeMap[mapNo].txPending --;

            if (!ar->arNodeMap[mapNo].txPending && (mapNo == (ar->arNodeNum - 1))) {
                A_UINT32 i;
                for (i = ar->arNodeNum; i > 0; i --) {
                    if (!ar->arNodeMap[i - 1].txPending) {
                        A_MEMZERO(&ar->arNodeMap[i - 1], sizeof(struct ar_node_mapping));
                        ar->arNodeNum --;
                    } else {
                        break;
                    }
                }
            }
        }

        ar6000_free_cookie(ar, ar_cookie);

        if (ar->arNetQueueStopped) {
            ar->arNetQueueStopped = FALSE;
        }
    }

    AR6000_SPIN_UNLOCK(&ar->arLock, 0);

    /* lock is released, we can freely call other kernel APIs */

        /* free all skbs in our local list */
    while (!skb_queue_empty(&skb_queue)) {
            /* use non-lock version */
        pktSkb = __skb_dequeue(&skb_queue);
        A_NETBUF_FREE(pktSkb);
    }

    if ((ar->arConnected == TRUE) || (bypasswmi)) {
        if (!flushing) {
                /* don't wake the queue if we are flushing, other wise it will just
                 * keep queueing packets, which will keep failing */
            netif_wake_queue(ar->arNetDev);
        }
    }

    if (wakeEvent) {
        wake_up(&arEvent);
    }

}

sta_t *
ieee80211_find_conn(AR_SOFTC_T *ar, A_UINT8 *node_addr)
{
    sta_t *conn = NULL;
    A_UINT8 i, max_conn;

    switch(ar->arNetworkType) {
        case AP_NETWORK:
            max_conn = AP_MAX_NUM_STA;
            break;
        default:
            max_conn=0;
            break;
    }

    for (i = 0; i < max_conn; i++) {
        if (IEEE80211_ADDR_EQ(node_addr, ar->sta_list[i].mac)) {
            conn = &ar->sta_list[i];
            break;
        }
    }

    return conn;
}

sta_t *ieee80211_find_conn_for_aid(AR_SOFTC_T *ar, A_UINT8 aid)
{
    sta_t *conn = NULL;
    A_UINT8 ctr;

    for (ctr = 0; ctr < AP_MAX_NUM_STA; ctr++) {
        if (ar->sta_list[ctr].aid == aid) {
            conn = &ar->sta_list[ctr];
            break;
        }
    }
    return conn;
}

/*
 * Receive event handler.  This is called by HTC when a packet is received
 */
int pktcount;
static void
ar6000_rx(void *Context, HTC_PACKET *pPacket)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)Context;
    struct sk_buff *skb = (struct sk_buff *)pPacket->pPktContext;
    int minHdrLen;
    A_UINT8 containsDot11Hdr = 0;
    A_STATUS        status = pPacket->Status;
    HTC_ENDPOINT_ID   ept = pPacket->Endpoint;

    A_ASSERT((status != A_OK) ||
             (pPacket->pBuffer == (A_NETBUF_DATA(skb) + HTC_HEADER_LEN)));

    AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_RX,("ar6000_rx ar=0x%lx eid=%d, skb=0x%lx, data=0x%lx, len=0x%x status:%d",
                    (unsigned long)ar, ept, (unsigned long)skb, (unsigned long)pPacket->pBuffer,
                    pPacket->ActualLength, status));
    if (status != A_OK) {
        if (status != A_ECANCELED) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("RX ERR (%d) \n",status));
        }
    }

        /* take lock to protect buffer counts
         * and adaptive power throughput state */
    AR6000_SPIN_LOCK(&ar->arLock, 0);

    if (A_SUCCESS(status)) {
        AR6000_STAT_INC(ar, rx_packets);
        ar->arNetStats.rx_bytes += pPacket->ActualLength;
#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
        aptcTR.bytesReceived += a_netbuf_to_len(skb);
        applyAPTCHeuristics(ar);
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */

        A_NETBUF_PUT(skb, pPacket->ActualLength +  HTC_HEADER_LEN);
        A_NETBUF_PULL(skb, HTC_HEADER_LEN);

#ifdef DEBUG
        if (debugdriver >= 2) {
            ar6000_dump_skb(skb);
        }
#endif /* DEBUG */
    }

    AR6000_SPIN_UNLOCK(&ar->arLock, 0);

    skb->dev = ar->arNetDev;
    if (status != A_OK) {
        AR6000_STAT_INC(ar, rx_errors);
        A_NETBUF_FREE(skb);
    } else if (ar->arWmiEnabled == TRUE) {
        if (ept == ar->arControlEp) {
           /*
            * this is a wmi control msg
            */
#ifdef CONFIG_PM 
            ar6000_check_wow_status(ar, skb, TRUE);
#endif /* CONFIG_PM */
            wmi_control_rx(ar->arWmi, skb);
        } else {
                WMI_DATA_HDR *dhdr = (WMI_DATA_HDR *)A_NETBUF_DATA(skb);
                A_UINT8 is_amsdu, tid, is_acl_data_frame;
                is_acl_data_frame = WMI_DATA_HDR_GET_DATA_TYPE(dhdr) == WMI_DATA_HDR_DATA_TYPE_ACL;
#ifdef CONFIG_PM 
                ar6000_check_wow_status(ar, NULL, FALSE);
#endif /* CONFIG_PM */
                /*
                 * this is a wmi data packet
                 */
                 // NWF

                if (processDot11Hdr) {
                    minHdrLen = sizeof(WMI_DATA_HDR) + sizeof(struct ieee80211_frame) + sizeof(ATH_LLC_SNAP_HDR);
                } else {
                    minHdrLen = sizeof (WMI_DATA_HDR) + sizeof(ATH_MAC_HDR) +
                          sizeof(ATH_LLC_SNAP_HDR);
                }

                /* In the case of AP mode we may receive NULL data frames
                 * that do not have LLC hdr. They are 16 bytes in size.
                 * Allow these frames in the AP mode.
                 * ACL data frames don't follow ethernet frame bounds for
                 * min length
                 */
                if (ar->arNetworkType != AP_NETWORK &&  !is_acl_data_frame &&
                    ((pPacket->ActualLength < minHdrLen) ||
                    (pPacket->ActualLength > AR6000_MAX_RX_MESSAGE_SIZE)))
                {
                    /*
                     * packet is too short or too long
                     */
                    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("TOO SHORT or TOO LONG\n"));
                    AR6000_STAT_INC(ar, rx_errors);
                    AR6000_STAT_INC(ar, rx_length_errors);
                    A_NETBUF_FREE(skb);
                } else {
                    A_UINT16 seq_no;
                    A_UINT8 meta_type;

#if 0
                    /* Access RSSI values here */
                    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("RSSI %d\n",
                        ((WMI_DATA_HDR *) A_NETBUF_DATA(skb))->rssi));
#endif
                    /* Get the Power save state of the STA */
                    if (ar->arNetworkType == AP_NETWORK) {
                        sta_t *conn = NULL;
                        A_UINT8 psState=0,prevPsState;
                        ATH_MAC_HDR *datap=NULL;
                        A_UINT16 offset;

                        meta_type = WMI_DATA_HDR_GET_META(dhdr);

                        psState = (((WMI_DATA_HDR *)A_NETBUF_DATA(skb))->info
                                     >> WMI_DATA_HDR_PS_SHIFT) & WMI_DATA_HDR_PS_MASK;

                        offset = sizeof(WMI_DATA_HDR);

                        switch (meta_type) {
                            case 0:
                                break;
                            case WMI_META_VERSION_1:
                                offset += sizeof(WMI_RX_META_V1);
                                break;
#ifdef CONFIG_CHECKSUM_OFFLOAD
                            case WMI_META_VERSION_2:
                                offset += sizeof(WMI_RX_META_V2);
                                break;
#endif
                            default:
                                break;
                        }

                        datap = (ATH_MAC_HDR *)(A_NETBUF_DATA(skb)+offset);
                        conn = ieee80211_find_conn(ar, datap->srcMac);

                        if (conn) {
                            /* if there is a change in PS state of the STA,
                             * take appropriate steps.
                             * 1. If Sleep-->Awake, flush the psq for the STA
                             *    Clear the PVB for the STA.
                             * 2. If Awake-->Sleep, Starting queueing frames
                             * the STA.
                             */
                            prevPsState = STA_IS_PWR_SLEEP(conn);
                            if (psState) {
                                STA_SET_PWR_SLEEP(conn);
                            } else {
                                STA_CLR_PWR_SLEEP(conn);
                            }

                            if (prevPsState ^ STA_IS_PWR_SLEEP(conn)) {

                                if (!STA_IS_PWR_SLEEP(conn)) {

                                    A_MUTEX_LOCK(&conn->psqLock);
                                    while (!A_NETBUF_QUEUE_EMPTY(&conn->psq)) {
                                        struct sk_buff *skb=NULL;

                                        skb = A_NETBUF_DEQUEUE(&conn->psq);
                                        A_MUTEX_UNLOCK(&conn->psqLock);
                                        ar6000_data_tx(skb,ar->arNetDev);
                                        A_MUTEX_LOCK(&conn->psqLock);
                                    }
                                    A_MUTEX_UNLOCK(&conn->psqLock);
                                    /* Clear the PVB for this STA */
                                    wmi_set_pvb_cmd(ar->arWmi, conn->aid, 0);
                                }
                            }
                        } else {
                            /* This frame is from a STA that is not associated*/
                            A_ASSERT(FALSE);
                        }

                        /* Drop NULL data frames here */
                        if((pPacket->ActualLength < minHdrLen) ||
                                (pPacket->ActualLength > AR6000_MAX_RX_MESSAGE_SIZE)) {
                            A_NETBUF_FREE(skb);
                            goto rx_done;
                        }
                    }

                    is_amsdu = WMI_DATA_HDR_IS_AMSDU(dhdr);
                    tid = WMI_DATA_HDR_GET_UP(dhdr);
                    seq_no = WMI_DATA_HDR_GET_SEQNO(dhdr);
                    meta_type = WMI_DATA_HDR_GET_META(dhdr);
                    containsDot11Hdr = WMI_DATA_HDR_GET_DOT11(dhdr);

                    wmi_data_hdr_remove(ar->arWmi, skb);

                    switch (meta_type) {
                        case WMI_META_VERSION_1:
                            {
                                WMI_RX_META_V1 *pMeta = (WMI_RX_META_V1 *)A_NETBUF_DATA(skb);
                                A_PRINTF("META %d %d %d %d %x\n", pMeta->status, pMeta->rix, pMeta->rssi, pMeta->channel, pMeta->flags);
                                A_NETBUF_PULL((void*)skb, sizeof(WMI_RX_META_V1));
                                break;
                            }
#ifdef CONFIG_CHECKSUM_OFFLOAD
                        case WMI_META_VERSION_2:
                            {
                                WMI_RX_META_V2 *pMeta = (WMI_RX_META_V2 *)A_NETBUF_DATA(skb);
                                if(pMeta->csumFlags & 0x1){
                                    skb->ip_summed=CHECKSUM_COMPLETE;
                                    skb->csum=(pMeta->csum);
                                }
                                A_NETBUF_PULL((void*)skb, sizeof(WMI_RX_META_V2));
                                break;
                            }
#endif
                        default:
                            break;
                    }

                    A_ASSERT(status == A_OK);

                    /* NWF: print the 802.11 hdr bytes */
                    if(containsDot11Hdr) {
                        status = wmi_dot11_hdr_remove(ar->arWmi,skb);
                    } else if(!is_amsdu && !is_acl_data_frame) {
                        status = wmi_dot3_2_dix(skb);
                    }

                    if (status != A_OK) {
                        /* Drop frames that could not be processed (lack of memory, etc.) */
                        A_NETBUF_FREE(skb);
                        goto rx_done;
                    }

                    if (is_acl_data_frame) {
                        A_NETBUF_PUSH(skb, sizeof(int));
                        *((short *)A_NETBUF_DATA(skb)) = WMI_ACL_DATA_EVENTID;
	                /* send the data packet to PAL driver */
			if(ar6k_pal_config_g.fpar6k_pal_recv_pkt) {
				if((*ar6k_pal_config_g.fpar6k_pal_recv_pkt)(ar->hcipal_info, skb) == TRUE)
					goto rx_done;
			}
                    }

                    if ((ar->arNetDev->flags & IFF_UP) == IFF_UP) {
                        if (ar->arNetworkType == AP_NETWORK) {
                            struct sk_buff *skb1 = NULL;
                            ATH_MAC_HDR *datap;

                            datap = (ATH_MAC_HDR *)A_NETBUF_DATA(skb);
                            if (IEEE80211_IS_MULTICAST(datap->dstMac)) {
                                /* Bcast/Mcast frames should be sent to the OS
                                 * stack as well as on the air.
                                 */
                                skb1 = skb_copy(skb,GFP_ATOMIC);
                            } else {
                                /* Search for a connected STA with dstMac as
                                 * the Mac address. If found send the frame to
                                 * it on the air else send the frame up the
                                 * stack
                                 */
                                sta_t *conn = NULL;
                                conn = ieee80211_find_conn(ar, datap->dstMac);

                                if (conn && ar->intra_bss) {
                                    skb1 = skb;
                                    skb = NULL;
                                } else if(conn && !ar->intra_bss) {
                                    A_NETBUF_FREE(skb);
                                    skb = NULL;
                                }
                            }
                            if (skb1) {
                                ar6000_data_tx(skb1, ar->arNetDev);
                            }
                        }
                    }
#ifdef ATH_AR6K_11N_SUPPORT
                    aggr_process_recv_frm(ar->aggr_cntxt, tid, seq_no, is_amsdu, (void **)&skb);
#endif
                    ar6000_deliver_frames_to_nw_stack((void *) ar->arNetDev, (void *)skb);
                }
            }
    } else {
        if (EPPING_ALIGNMENT_PAD > 0) {
            A_NETBUF_PULL(skb, EPPING_ALIGNMENT_PAD);
        }
        ar6000_deliver_frames_to_nw_stack((void *)ar->arNetDev, (void *)skb);
    }

rx_done:

    return;
}

static void
ar6000_deliver_frames_to_nw_stack(void *dev, void *osbuf)
{
    struct sk_buff *skb = (struct sk_buff *)osbuf;

    if(skb) {
        skb->dev = dev;
        if ((skb->dev->flags & IFF_UP) == IFF_UP) {
#ifdef CONFIG_PM 
            ar6000_check_wow_status((AR_SOFTC_T *)ar6k_priv(dev), skb, FALSE);   
#endif /* CONFIG_PM */
            skb->protocol = eth_type_trans(skb, skb->dev);
        /*
         * If this routine is called on a ISR (Hard IRQ) or DSR (Soft IRQ)
         * or tasklet use the netif_rx to deliver the packet to the stack
         * netif_rx will queue the packet onto the receive queue and mark
         * the softirq thread has a pending action to complete. Kernel will 
         * schedule the softIrq kernel thread after processing the DSR.
         *
         * If this routine is called on a process context, use netif_rx_ni
         * which will schedle the softIrq kernel thread after queuing the packet.
         */
            if (in_interrupt()) {
                netif_rx(skb);
            } else {
                netif_rx_ni(skb);
            }
        } else {
            A_NETBUF_FREE(skb);
        }
    }
}

#if 0
static void
ar6000_deliver_frames_to_bt_stack(void *dev, void *osbuf)
{
    struct sk_buff *skb = (struct sk_buff *)osbuf;

    if(skb) {
        skb->dev = dev;
        if ((skb->dev->flags & IFF_UP) == IFF_UP) {
            skb->protocol = htons(ETH_P_CONTROL);
            netif_rx(skb);
        } else {
            A_NETBUF_FREE(skb);
        }
    }
}
#endif

static void
ar6000_rx_refill(void *Context, HTC_ENDPOINT_ID Endpoint)
{
    AR_SOFTC_T  *ar = (AR_SOFTC_T *)Context;
    void        *osBuf;
    int         RxBuffers;
    int         buffersToRefill;
    HTC_PACKET  *pPacket;
    HTC_PACKET_QUEUE queue;

    buffersToRefill = (int)AR6000_MAX_RX_BUFFERS -
                                    HTCGetNumRecvBuffers(ar->arHtcTarget, Endpoint);

    if (buffersToRefill <= 0) {
            /* fast return, nothing to fill */
        return;
    }

    INIT_HTC_PACKET_QUEUE(&queue);

    AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_RX,("ar6000_rx_refill: providing htc with %d buffers at eid=%d\n",
                    buffersToRefill, Endpoint));

    for (RxBuffers = 0; RxBuffers < buffersToRefill; RxBuffers++) {
        osBuf = A_NETBUF_ALLOC(AR6000_BUFFER_SIZE);
        if (NULL == osBuf) {
            break;
        }
            /* the HTC packet wrapper is at the head of the reserved area
             * in the skb */
        pPacket = (HTC_PACKET *)(A_NETBUF_HEAD(osBuf));
            /* set re-fill info */
        SET_HTC_PACKET_INFO_RX_REFILL(pPacket,osBuf,A_NETBUF_DATA(osBuf),AR6000_BUFFER_SIZE,Endpoint);
            /* add to queue */
        HTC_PACKET_ENQUEUE(&queue,pPacket);
    }

    if (!HTC_QUEUE_EMPTY(&queue)) {
            /* add packets */
        HTCAddReceivePktMultiple(ar->arHtcTarget, &queue);
    }

}

  /* clean up our amsdu buffer list */
static void ar6000_cleanup_amsdu_rxbufs(AR_SOFTC_T *ar)
{
    HTC_PACKET  *pPacket;
    void        *osBuf;

        /* empty AMSDU buffer queue and free OS bufs */
    while (TRUE) {

        AR6000_SPIN_LOCK(&ar->arLock, 0);
        pPacket = HTC_PACKET_DEQUEUE(&ar->amsdu_rx_buffer_queue);
        AR6000_SPIN_UNLOCK(&ar->arLock, 0);

        if (NULL == pPacket) {
            break;
        }

        osBuf = pPacket->pPktContext;
        if (NULL == osBuf) {
            A_ASSERT(FALSE);
            break;
        }

        A_NETBUF_FREE(osBuf);
    }

}


    /* refill the amsdu buffer list */
static void ar6000_refill_amsdu_rxbufs(AR_SOFTC_T *ar, int Count)
{
    HTC_PACKET  *pPacket;
    void        *osBuf;

    while (Count > 0) {
        osBuf = A_NETBUF_ALLOC(AR6000_AMSDU_BUFFER_SIZE);
        if (NULL == osBuf) {
            break;
        }
            /* the HTC packet wrapper is at the head of the reserved area
             * in the skb */
        pPacket = (HTC_PACKET *)(A_NETBUF_HEAD(osBuf));
            /* set re-fill info */
        SET_HTC_PACKET_INFO_RX_REFILL(pPacket,osBuf,A_NETBUF_DATA(osBuf),AR6000_AMSDU_BUFFER_SIZE,0);

        AR6000_SPIN_LOCK(&ar->arLock, 0);
            /* put it in the list */
        HTC_PACKET_ENQUEUE(&ar->amsdu_rx_buffer_queue,pPacket);
        AR6000_SPIN_UNLOCK(&ar->arLock, 0);
        Count--;
    }

}

    /* callback to allocate a large receive buffer for a pending packet.  This function is called when
     * an HTC packet arrives whose length exceeds a threshold value
     *
     * We use a pre-allocated list of buffers of maximum AMSDU size (4K).  Under linux it is more optimal to
     * keep the allocation size the same to optimize cached-slab allocations.
     *
     * */
static HTC_PACKET *ar6000_alloc_amsdu_rxbuf(void *Context, HTC_ENDPOINT_ID Endpoint, int Length)
{
    HTC_PACKET  *pPacket = NULL;
    AR_SOFTC_T  *ar = (AR_SOFTC_T *)Context;
    int         refillCount = 0;

    AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_RX,("ar6000_alloc_amsdu_rxbuf: eid=%d, Length:%d\n",Endpoint,Length));

    do {

        if (Length <= AR6000_BUFFER_SIZE) {
                /* shouldn't be getting called on normal sized packets */
            A_ASSERT(FALSE);
            break;
        }

        if (Length > AR6000_AMSDU_BUFFER_SIZE) {
            A_ASSERT(FALSE);
            break;
        }

        AR6000_SPIN_LOCK(&ar->arLock, 0);
            /* allocate a packet from the list */
        pPacket = HTC_PACKET_DEQUEUE(&ar->amsdu_rx_buffer_queue);
            /* see if we need to refill again */
        refillCount = AR6000_MAX_AMSDU_RX_BUFFERS - HTC_PACKET_QUEUE_DEPTH(&ar->amsdu_rx_buffer_queue);
        AR6000_SPIN_UNLOCK(&ar->arLock, 0);

        if (NULL == pPacket) {
            break;
        }
            /* set actual endpoint ID */
        pPacket->Endpoint = Endpoint;

    } while (FALSE);

    if (refillCount >= AR6000_AMSDU_REFILL_THRESHOLD) {
        ar6000_refill_amsdu_rxbufs(ar,refillCount);
    }

    return pPacket;
}

static void	
ar6000_set_multicast_list(struct net_device *dev)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000: Multicast filter not supported\n"));
}

static struct net_device_stats *
ar6000_get_stats(struct net_device *dev)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    return &ar->arNetStats;
}

static struct iw_statistics *
ar6000_get_iwstats(struct net_device * dev)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ar6k_priv(dev);
    TARGET_STATS *pStats = &ar->arTargetStats;
    struct iw_statistics * pIwStats = &ar->arIwStats;
    int rtnllocked;

    if (ar->bIsDestroyProgress || ar->arWmiReady == FALSE || ar->arWlanState == WLAN_DISABLED)
    {
        pIwStats->status = 0;
        pIwStats->qual.qual = 0;
        pIwStats->qual.level =0;
        pIwStats->qual.noise = 0;
        pIwStats->discard.code =0;
        pIwStats->discard.retries=0;
        pIwStats->miss.beacon =0;
        return pIwStats;
    }

    /*
     * The in_atomic function is used to determine if the scheduling is
     * allowed in the current context or not. This was introduced in 2.6
     * From what I have read on the differences between 2.4 and 2.6, the
     * 2.4 kernel did not support preemption and so this check might not
     * be required for 2.4 kernels.
     */
    if (in_atomic())
    {
        wmi_get_stats_cmd(ar->arWmi);

        pIwStats->status = 1 ;
        pIwStats->qual.qual = pStats->cs_aveBeacon_rssi - 161;
        pIwStats->qual.level =pStats->cs_aveBeacon_rssi; /* noise is -95 dBm */
        pIwStats->qual.noise = pStats->noise_floor_calibation;
        pIwStats->discard.code = pStats->rx_decrypt_err;
        pIwStats->discard.retries = pStats->tx_retry_cnt;
        pIwStats->miss.beacon = pStats->cs_bmiss_cnt;
        return pIwStats;
    }

    dev_hold(dev);   
    rtnllocked = rtnl_is_locked();
    if (rtnllocked) {
        rtnl_unlock();
    }
    pIwStats->status = 0;

    if (down_interruptible(&ar->arSem)) {
        goto err_exit;
    }
    
    do {

        if (ar->bIsDestroyProgress || ar->arWlanState == WLAN_DISABLED) {
            break;
        }
    
        ar->statsUpdatePending = TRUE;
    
        if(wmi_get_stats_cmd(ar->arWmi) != A_OK) {
            break;
        }
    
        wait_event_interruptible_timeout(arEvent, ar->statsUpdatePending == FALSE, wmitimeout * HZ);
        if (signal_pending(current)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000 : WMI get stats timeout \n"));
            break;
        }
        pIwStats->status = 1 ;
        pIwStats->qual.qual = pStats->cs_aveBeacon_rssi - 161;
        pIwStats->qual.level =pStats->cs_aveBeacon_rssi;  /* noise is -95 dBm */
        pIwStats->qual.noise = pStats->noise_floor_calibation;
        pIwStats->discard.code = pStats->rx_decrypt_err;
        pIwStats->discard.retries = pStats->tx_retry_cnt;
        pIwStats->miss.beacon = pStats->cs_bmiss_cnt;
    } while (0);
    up(&ar->arSem);

err_exit:
    if (rtnllocked) {
        rtnl_lock();
    }
    dev_put(dev);
    return pIwStats;
}

void
ar6000_ready_event(void *devt, A_UINT8 *datap, A_UINT8 phyCap, A_UINT32 sw_ver, A_UINT32 abi_ver)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)devt;
    struct net_device *dev = ar->arNetDev;

    A_MEMCPY(dev->dev_addr, datap, AR6000_ETH_ADDR_LEN);
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("mac address = %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n",
        dev->dev_addr[0], dev->dev_addr[1],
        dev->dev_addr[2], dev->dev_addr[3],
        dev->dev_addr[4], dev->dev_addr[5]));

    ar->arPhyCapability = phyCap;
    ar->arVersion.wlan_ver = sw_ver;
    ar->arVersion.abi_ver = abi_ver;

    /* Indicate to the waiting thread that the ready event was received */
    ar->arWmiReady = TRUE;
    wake_up(&arEvent);

#if WLAN_CONFIG_IGNORE_POWER_SAVE_FAIL_EVENT_DURING_SCAN
    wmi_pmparams_cmd(ar->arWmi, 0, 1, 0, 0, 1, IGNORE_POWER_SAVE_FAIL_EVENT_DURING_SCAN);
#endif
#if WLAN_CONFIG_DONOT_IGNORE_BARKER_IN_ERP
    wmi_set_lpreamble_cmd(ar->arWmi, 0, WMI_DONOT_IGNORE_BARKER_IN_ERP);
#endif
    wmi_set_keepalive_cmd(ar->arWmi, WLAN_CONFIG_KEEP_ALIVE_INTERVAL);
#if WLAN_CONFIG_DISABLE_11N
    {
        WMI_SET_HT_CAP_CMD htCap;

        A_MEMZERO(&htCap, sizeof(WMI_SET_HT_CAP_CMD));
        htCap.band = 0;
        wmi_set_ht_cap_cmd(ar->arWmi, &htCap);

        htCap.band = 1;
        wmi_set_ht_cap_cmd(ar->arWmi, &htCap);
    }
#endif /* WLAN_CONFIG_DISABLE_11N */

#ifdef ATH6K_CONFIG_OTA_MODE
    wmi_powermode_cmd(ar->arWmi, MAX_PERF_POWER);
#endif
    wmi_disctimeout_cmd(ar->arWmi, WLAN_CONFIG_DISCONNECT_TIMEOUT);
}

void
add_new_sta(AR_SOFTC_T *ar, A_UINT8 *mac, A_UINT16 aid, A_UINT8 *wpaie,
            A_UINT8 ielen, A_UINT8 keymgmt, A_UINT8 ucipher, A_UINT8 auth)
{
    A_UINT8    free_slot=aid-1;

        A_MEMCPY(ar->sta_list[free_slot].mac, mac, ATH_MAC_LEN);
        A_MEMCPY(ar->sta_list[free_slot].wpa_ie, wpaie, ielen);
        ar->sta_list[free_slot].aid = aid;
        ar->sta_list[free_slot].keymgmt = keymgmt;
        ar->sta_list[free_slot].ucipher = ucipher;
        ar->sta_list[free_slot].auth = auth;
        ar->sta_list_index = ar->sta_list_index | (1 << free_slot);
    ar->arAPStats.sta[free_slot].aid = aid;
}

void
ar6000_connect_event(AR_SOFTC_T *ar, A_UINT16 channel, A_UINT8 *bssid,
                     A_UINT16 listenInterval, A_UINT16 beaconInterval,
                     NETWORK_TYPE networkType, A_UINT8 beaconIeLen,
                     A_UINT8 assocReqLen, A_UINT8 assocRespLen,
                     A_UINT8 *assocInfo)
{
    union iwreq_data wrqu;
    int i, beacon_ie_pos, assoc_resp_ie_pos, assoc_req_ie_pos;
    static const char *tag1 = "ASSOCINFO(ReqIEs=";
    static const char *tag2 = "ASSOCRESPIE=";
    static const char *beaconIetag = "BEACONIE=";
    char buf[WMI_CONTROL_MSG_MAX_LEN * 2 + strlen(tag1) + 1];
    char *pos;
    A_UINT8 key_op_ctrl;
    unsigned long flags;
    struct ieee80211req_key *ik;
    CRYPTO_TYPE keyType = NONE_CRYPT;

    if(ar->arNetworkType & AP_NETWORK) {
        struct net_device *dev = ar->arNetDev;
        if(A_MEMCMP(dev->dev_addr, bssid, ATH_MAC_LEN)==0) {
            ar->arACS = channel;
            ik = &ar->ap_mode_bkey;

            switch(ar->arAuthMode) {
            case NONE_AUTH:
                if(ar->arPairwiseCrypto == WEP_CRYPT) {
                    ar6000_install_static_wep_keys(ar);
                }
#ifdef WAPI_ENABLE
                else if(ar->arPairwiseCrypto == WAPI_CRYPT) {
                    ap_set_wapi_key(ar, ik);
                }
#endif
                break;
            case WPA_PSK_AUTH:
            case WPA2_PSK_AUTH:
            case (WPA_PSK_AUTH|WPA2_PSK_AUTH):
                switch (ik->ik_type) {
                    case IEEE80211_CIPHER_TKIP:
                        keyType = TKIP_CRYPT;
                        break;
                    case IEEE80211_CIPHER_AES_CCM:
                        keyType = AES_CRYPT;
                        break;
                    default:
                       goto skip_key;
                }
                wmi_addKey_cmd(ar->arWmi, ik->ik_keyix, keyType, GROUP_USAGE,
                                ik->ik_keylen, (A_UINT8 *)&ik->ik_keyrsc,
                                ik->ik_keydata, KEY_OP_INIT_VAL, ik->ik_macaddr,
                                SYNC_BOTH_WMIFLAG);

                break;
            }
skip_key:
            ar->arConnected  = TRUE;
            return;
        }

        A_PRINTF("NEW STA %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x \n "
            " AID=%d \n", bssid[0], bssid[1], bssid[2],
             bssid[3], bssid[4], bssid[5], channel);
        switch ((listenInterval>>8)&0xFF) {
            case OPEN_AUTH:
                A_PRINTF("AUTH: OPEN\n");
                break;
            case SHARED_AUTH:
                A_PRINTF("AUTH: SHARED\n");
                break;
            default:
                A_PRINTF("AUTH: Unknown\n");
                break;
        };
        switch (listenInterval&0xFF) {
            case WPA_PSK_AUTH:
                A_PRINTF("KeyMgmt: WPA-PSK\n");
                break;
            case WPA2_PSK_AUTH:
                A_PRINTF("KeyMgmt: WPA2-PSK\n");
                break;
            default:
                A_PRINTF("KeyMgmt: NONE\n");
                break;
        };
        switch (beaconInterval) {
            case AES_CRYPT:
                A_PRINTF("Cipher: AES\n");
                break;
            case TKIP_CRYPT:
                A_PRINTF("Cipher: TKIP\n");
                break;
            case WEP_CRYPT:
                A_PRINTF("Cipher: WEP\n");
                break;
#ifdef WAPI_ENABLE
            case WAPI_CRYPT:
                A_PRINTF("Cipher: WAPI\n");
                break;
#endif
            default:
                A_PRINTF("Cipher: NONE\n");
                break;
        };

        add_new_sta(ar, bssid, channel /*aid*/,
            assocInfo /* WPA IE */, assocRespLen /* IE len */,
            listenInterval&0xFF /* Keymgmt */, beaconInterval /* cipher */,
            (listenInterval>>8)&0xFF /* auth alg */);

        /* Send event to application */
        A_MEMZERO(&wrqu, sizeof(wrqu));
        A_MEMCPY(wrqu.addr.sa_data, bssid, ATH_MAC_LEN);
        wireless_send_event(ar->arNetDev, IWEVREGISTERED, &wrqu, NULL);
        /* In case the queue is stopped when we switch modes, this will
         * wake it up
         */
        netif_wake_queue(ar->arNetDev);
        return;
    }

#ifdef ATH6K_CONFIG_CFG80211
    ar6k_cfg80211_connect_event(ar, channel, bssid,
                                listenInterval, beaconInterval,
                                networkType, beaconIeLen,
                                assocReqLen, assocRespLen,
                                assocInfo);
#endif /* ATH6K_CONFIG_CFG80211 */

    A_MEMCPY(ar->arBssid, bssid, sizeof(ar->arBssid));
    ar->arBssChannel = channel;

    A_PRINTF("AR6000 connected event on freq %d ", channel);
    A_PRINTF("with bssid %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x "
            " listenInterval=%d, beaconInterval = %d, beaconIeLen = %d assocReqLen=%d"
            " assocRespLen =%d\n",
             bssid[0], bssid[1], bssid[2],
             bssid[3], bssid[4], bssid[5],
             listenInterval, beaconInterval,
             beaconIeLen, assocReqLen, assocRespLen);
    if (networkType & ADHOC_NETWORK) {
        if (networkType & ADHOC_CREATOR) {
            A_PRINTF("Network: Adhoc (Creator)\n");
        } else {
            A_PRINTF("Network: Adhoc (Joiner)\n");
        }
    } else {
        A_PRINTF("Network: Infrastructure\n");
    }

    if ((ar->arNetworkType == INFRA_NETWORK)) {
        wmi_listeninterval_cmd(ar->arWmi, ar->arListenIntervalT, ar->arListenIntervalB);
    }

    if (beaconIeLen && (sizeof(buf) > (9 + beaconIeLen * 2))) {
        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("\nBeaconIEs= "));

        beacon_ie_pos = 0;
        A_MEMZERO(buf, sizeof(buf));
        sprintf(buf, "%s", beaconIetag);
        pos = buf + 9;
        for (i = beacon_ie_pos; i < beacon_ie_pos + beaconIeLen; i++) {
            AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("%2.2x ", assocInfo[i]));
            sprintf(pos, "%2.2x", assocInfo[i]);
            pos += 2;
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("\n"));

        A_MEMZERO(&wrqu, sizeof(wrqu));
        wrqu.data.length = strlen(buf);
        wireless_send_event(ar->arNetDev, IWEVCUSTOM, &wrqu, buf);
    }

    if (assocRespLen && (sizeof(buf) > (12 + (assocRespLen * 2))))
    {
        assoc_resp_ie_pos = beaconIeLen + assocReqLen +
                            sizeof(A_UINT16)  +  /* capinfo*/
                            sizeof(A_UINT16)  +  /* status Code */
                            sizeof(A_UINT16)  ;  /* associd */
        A_MEMZERO(buf, sizeof(buf));
        sprintf(buf, "%s", tag2);
        pos = buf + 12;
        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("\nAssocRespIEs= "));
        /*
         * The Association Response Frame w.o. the WLAN header is delivered to
         * the host, so skip over to the IEs
         */
        for (i = assoc_resp_ie_pos; i < assoc_resp_ie_pos + assocRespLen - 6; i++)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("%2.2x ", assocInfo[i]));
            sprintf(pos, "%2.2x", assocInfo[i]);
            pos += 2;
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("\n"));

        A_MEMZERO(&wrqu, sizeof(wrqu));
        wrqu.data.length = strlen(buf);
        wireless_send_event(ar->arNetDev, IWEVCUSTOM, &wrqu, buf);
    }

    if (assocReqLen && (sizeof(buf) > (17 + (assocReqLen * 2)))) {
        /*
         * assoc Request includes capability and listen interval. Skip these.
         */
        assoc_req_ie_pos =  beaconIeLen +
                            sizeof(A_UINT16)  +  /* capinfo*/
                            sizeof(A_UINT16);    /* listen interval */

        A_MEMZERO(buf, sizeof(buf));
        sprintf(buf, "%s", tag1);
        pos = buf + 17;
        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("AssocReqIEs= "));
        for (i = assoc_req_ie_pos; i < assoc_req_ie_pos + assocReqLen - 4; i++) {
            AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("%2.2x ", assocInfo[i]));
            sprintf(pos, "%2.2x", assocInfo[i]);
            pos += 2;;
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("\n"));

        A_MEMZERO(&wrqu, sizeof(wrqu));
        wrqu.data.length = strlen(buf);
        wireless_send_event(ar->arNetDev, IWEVCUSTOM, &wrqu, buf);
    }

#ifdef USER_KEYS
    if (ar->user_savedkeys_stat == USER_SAVEDKEYS_STAT_RUN &&
        ar->user_saved_keys.keyOk == TRUE)
    {
        key_op_ctrl = KEY_OP_VALID_MASK & ~KEY_OP_INIT_TSC;

        if (ar->user_key_ctrl & AR6000_USER_SETKEYS_RSC_UNCHANGED) {
            key_op_ctrl &= ~KEY_OP_INIT_RSC;
        } else {
            key_op_ctrl |= KEY_OP_INIT_RSC;
        }
        ar6000_reinstall_keys(ar, key_op_ctrl);
    }
#endif /* USER_KEYS */

    netif_wake_queue(ar->arNetDev);

    /* For CFG80211 the key configuration and the default key comes in after connect so no point in plumbing invalid keys */
#ifndef ATH6K_CONFIG_CFG80211
    if ((networkType & ADHOC_NETWORK)      &&
        (OPEN_AUTH == ar->arDot11AuthMode) &&
        (NONE_AUTH == ar->arAuthMode)      &&
        (WEP_CRYPT == ar->arPairwiseCrypto))
    {
        if (!ar->arConnected) {
            wmi_addKey_cmd(ar->arWmi,
                           ar->arDefTxKeyIndex,
                           WEP_CRYPT,
                           GROUP_USAGE | TX_USAGE,
                           ar->arWepKeyList[ar->arDefTxKeyIndex].arKeyLen,
                           NULL,
                           ar->arWepKeyList[ar->arDefTxKeyIndex].arKey, KEY_OP_INIT_VAL, NULL,
                           NO_SYNC_WMIFLAG);
        }
    }
#endif /* ATH6K_CONFIG_CFG80211 */

    /* Update connect & link status atomically */
    spin_lock_irqsave(&ar->arLock, flags);
    ar->arConnected  = TRUE;
    ar->arConnectPending = FALSE;
    netif_carrier_on(ar->arNetDev);
    spin_unlock_irqrestore(&ar->arLock, flags);
    /* reset the rx aggr state */
    aggr_reset_state(ar->aggr_cntxt);
    reconnect_flag = 0;

    A_MEMZERO(&wrqu, sizeof(wrqu));
    A_MEMCPY(wrqu.addr.sa_data, bssid, IEEE80211_ADDR_LEN);
    wrqu.addr.sa_family = ARPHRD_ETHER;
    wireless_send_event(ar->arNetDev, SIOCGIWAP, &wrqu, NULL);
    if ((ar->arNetworkType == ADHOC_NETWORK) && ar->arIbssPsEnable) {
        A_MEMZERO(ar->arNodeMap, sizeof(ar->arNodeMap));
        ar->arNodeNum = 0;
        ar->arNexEpId = ENDPOINT_2;
    }
   if (!ar->arUserBssFilter) {
        wmi_bssfilter_cmd(ar->arWmi, NONE_BSS_FILTER, 0);
   }

}

void ar6000_set_numdataendpts(AR_SOFTC_T *ar, A_UINT32 num)
{
    A_ASSERT(num <= (HTC_MAILBOX_NUM_MAX - 1));
    ar->arNumDataEndPts = num;
}

void
sta_cleanup(AR_SOFTC_T *ar, A_UINT8 i)
{
    struct sk_buff *skb;

    /* empty the queued pkts in the PS queue if any */
    A_MUTEX_LOCK(&ar->sta_list[i].psqLock);
    while (!A_NETBUF_QUEUE_EMPTY(&ar->sta_list[i].psq)) {
        skb = A_NETBUF_DEQUEUE(&ar->sta_list[i].psq);
        A_NETBUF_FREE(skb);
    }
    A_MUTEX_UNLOCK(&ar->sta_list[i].psqLock);

    /* Zero out the state fields */
    A_MEMZERO(&ar->arAPStats.sta[ar->sta_list[i].aid-1], sizeof(WMI_PER_STA_STAT));
    A_MEMZERO(&ar->sta_list[i].mac, ATH_MAC_LEN);
    A_MEMZERO(&ar->sta_list[i].wpa_ie, IEEE80211_MAX_IE);
    ar->sta_list[i].aid = 0;
    ar->sta_list[i].flags = 0;

    ar->sta_list_index = ar->sta_list_index & ~(1 << i);

}

A_UINT8
remove_sta(AR_SOFTC_T *ar, A_UINT8 *mac, A_UINT16 reason)
{
    A_UINT8 i, removed=0;

    if(IS_MAC_NULL(mac)) {
        return removed;
    }

    if(IS_MAC_BCAST(mac)) {
        A_PRINTF("DEL ALL STA\n");
        for(i=0; i < AP_MAX_NUM_STA; i++) {
            if(!IS_MAC_NULL(ar->sta_list[i].mac)) {
                sta_cleanup(ar, i);
                removed = 1;
            }
        }
    } else {
        for(i=0; i < AP_MAX_NUM_STA; i++) {
            if(A_MEMCMP(ar->sta_list[i].mac, mac, ATH_MAC_LEN)==0) {
                A_PRINTF("DEL STA %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x "
                " aid=%d REASON=%d\n", mac[0], mac[1], mac[2],
                 mac[3], mac[4], mac[5], ar->sta_list[i].aid, reason);

                sta_cleanup(ar, i);
                removed = 1;
                break;
            }
        }
    }
    return removed;
}

void
ar6000_disconnect_event(AR_SOFTC_T *ar, A_UINT8 reason, A_UINT8 *bssid,
                        A_UINT8 assocRespLen, A_UINT8 *assocInfo, A_UINT16 protocolReasonStatus)
{
    A_UINT8 i;
    unsigned long flags;
    union iwreq_data wrqu;

    if(ar->arNetworkType & AP_NETWORK) {
        union iwreq_data wrqu;
        struct sk_buff *skb;

        if(!remove_sta(ar, bssid, protocolReasonStatus)) {
            return;
        }

        /* If there are no more associated STAs, empty the mcast PS q */
        if (ar->sta_list_index == 0) {
            A_MUTEX_LOCK(&ar->mcastpsqLock);
            while (!A_NETBUF_QUEUE_EMPTY(&ar->mcastpsq)) {
                skb = A_NETBUF_DEQUEUE(&ar->mcastpsq);
                A_NETBUF_FREE(skb);
            }
            A_MUTEX_UNLOCK(&ar->mcastpsqLock);

            /* Clear the LSB of the BitMapCtl field of the TIM IE */
            if (ar->arWmiReady) {
                wmi_set_pvb_cmd(ar->arWmi, MCAST_AID, 0);
            }
        }

        if(!IS_MAC_BCAST(bssid)) {
            /* Send event to application */
            A_MEMZERO(&wrqu, sizeof(wrqu));
            A_MEMCPY(wrqu.addr.sa_data, bssid, ATH_MAC_LEN);
            wireless_send_event(ar->arNetDev, IWEVEXPIRED, &wrqu, NULL);
        }
        return;
    }

#ifdef ATH6K_CONFIG_CFG80211
    ar6k_cfg80211_disconnect_event(ar, reason, bssid,
                                   assocRespLen, assocInfo,
                                   protocolReasonStatus);
#endif /* ATH6K_CONFIG_CFG80211 */

    /* Send disconnect event to supplicant */
    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.addr.sa_family = ARPHRD_ETHER;
    wireless_send_event(ar->arNetDev, SIOCGIWAP, &wrqu, NULL);

    /* it is necessary to clear the host-side rx aggregation state */
    aggr_reset_state(ar->aggr_cntxt);

    A_UNTIMEOUT(&ar->disconnect_timer);

    A_PRINTF("AR6000 disconnected");
    if (bssid[0] || bssid[1] || bssid[2] || bssid[3] || bssid[4] || bssid[5]) {
        A_PRINTF(" from %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x ",
                 bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]);
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("\nDisconnect Reason is %d", reason));
    AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("\nProtocol Reason/Status Code is %d", protocolReasonStatus));
    AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("\nAssocResp Frame = %s",
                    assocRespLen ? " " : "NULL"));
    for (i = 0; i < assocRespLen; i++) {
        if (!(i % 0x10)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("\n"));
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("%2.2x ", assocInfo[i]));
    }
    AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("\n"));
    /*
     * If the event is due to disconnect cmd from the host, only they the target
     * would stop trying to connect. Under any other condition, target would
     * keep trying to connect.
     *
     */
    if( reason == DISCONNECT_CMD)
    {
        ar->arConnectPending = FALSE;
        if ((!ar->arUserBssFilter) && (ar->arWmiReady)) {
            wmi_bssfilter_cmd(ar->arWmi, NONE_BSS_FILTER, 0);
        }
    } else {
        ar->arConnectPending = TRUE;
        if (((reason == ASSOC_FAILED) && (protocolReasonStatus == 0x11)) ||
            ((reason == ASSOC_FAILED) && (protocolReasonStatus == 0x0) && (reconnect_flag == 1))) {
            ar->arConnected = TRUE;
            return;
        }
    }

    if ((reason == NO_NETWORK_AVAIL) && (ar->arWmiReady)) 
    {
        bss_t *pWmiSsidnode = NULL;

        /* remove the current associated bssid node */
        wmi_free_node (ar->arWmi, bssid);

        /*
         * In case any other same SSID nodes are present
         * remove it, since those nodes also not available now
         */
        do
        {
            /*
             * Find the nodes based on SSID and remove it
             * NOTE :: This case will not work out for Hidden-SSID
             */
            pWmiSsidnode = wmi_find_Ssidnode (ar->arWmi, ar->arSsid, ar->arSsidLen, FALSE, TRUE);

            if (pWmiSsidnode)
            {
                wmi_free_node (ar->arWmi, pWmiSsidnode->ni_macaddr);
            }

        } while (pWmiSsidnode);
    }

    /* Update connect & link status atomically */
    spin_lock_irqsave(&ar->arLock, flags);
    ar->arConnected = FALSE;
    netif_carrier_off(ar->arNetDev);
    spin_unlock_irqrestore(&ar->arLock, flags);

    if( (reason != CSERV_DISCONNECT) || (reconnect_flag != 1) ) {
        reconnect_flag = 0;
    }

#ifdef USER_KEYS
    if (reason != CSERV_DISCONNECT)
    {
        ar->user_savedkeys_stat = USER_SAVEDKEYS_STAT_INIT;
        ar->user_key_ctrl      = 0;
    }
#endif /* USER_KEYS */

    netif_stop_queue(ar->arNetDev);
    A_MEMZERO(ar->arBssid, sizeof(ar->arBssid));
    ar->arBssChannel = 0;
    ar->arBeaconInterval = 0;

    ar6000_TxDataCleanup(ar);
}

void
ar6000_regDomain_event(AR_SOFTC_T *ar, A_UINT32 regCode)
{
    A_PRINTF("AR6000 Reg Code = 0x%x\n", regCode);
    ar->arRegCode = regCode;
}

#ifdef ATH_AR6K_11N_SUPPORT
void
ar6000_aggr_rcv_addba_req_evt(AR_SOFTC_T *ar, WMI_ADDBA_REQ_EVENT *evt)
{
    if(evt->status == 0) {
        aggr_recv_addba_req_evt(ar->aggr_cntxt, evt->tid, evt->st_seq_no, evt->win_sz);
    }
}

void
ar6000_aggr_rcv_addba_resp_evt(AR_SOFTC_T *ar, WMI_ADDBA_RESP_EVENT *evt)
{
    A_PRINTF("ADDBA RESP. tid %d status %d, sz %d\n", evt->tid, evt->status, evt->amsdu_sz);
    if(evt->status == 0) {
    }
}

void
ar6000_aggr_rcv_delba_req_evt(AR_SOFTC_T *ar, WMI_DELBA_EVENT *evt)
{
    aggr_recv_delba_req_evt(ar->aggr_cntxt, evt->tid);
}
#endif

void register_pal_cb(ar6k_pal_config_t *palConfig_p)
{
  ar6k_pal_config_g = *palConfig_p;
}

void
ar6000_hci_event_rcv_evt(struct ar6_softc *ar, WMI_HCI_EVENT *cmd)
{
    void *osbuf = NULL;
    A_INT8 i;
    A_UINT8 size, *buf;
    A_STATUS ret = A_OK;

    size = cmd->evt_buf_sz + 4;
    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
       ret = A_NO_MEMORY;
       A_PRINTF("Error in allocating netbuf \n");
       return;
    }

    A_NETBUF_PUT(osbuf, size);
    buf = (A_UINT8 *)A_NETBUF_DATA(osbuf);
    /* First 2-bytes carry HCI event/ACL data type
     * the next 2 are free
     */
    *((short *)buf) = WMI_HCI_EVENT_EVENTID;
    buf += sizeof(int);
    A_MEMCPY(buf, cmd->buf, cmd->evt_buf_sz);

    if(ar6k_pal_config_g.fpar6k_pal_recv_pkt)
    {
      /* pass the cmd packet to PAL driver */
      if((*ar6k_pal_config_g.fpar6k_pal_recv_pkt)(ar->hcipal_info, osbuf) == TRUE)
        return;
    }
    ar6000_deliver_frames_to_nw_stack(ar->arNetDev, osbuf);
    if(loghci) {
        A_PRINTF_LOG("HCI Event From PAL <-- \n");
        for(i = 0; i < cmd->evt_buf_sz; i++) {
           A_PRINTF_LOG("0x%02x ", cmd->buf[i]);
           if((i % 10) == 0) {
               A_PRINTF_LOG("\n");
           }
        }
        A_PRINTF_LOG("\n");
        A_PRINTF_LOG("==================================\n");
    }
}

void
ar6000_neighborReport_event(AR_SOFTC_T *ar, int numAps, WMI_NEIGHBOR_INFO *info)
{
#if WIRELESS_EXT >= 18
    struct iw_pmkid_cand *pmkcand;
#else /* WIRELESS_EXT >= 18 */
    static const char *tag = "PRE-AUTH";
    char buf[128];
#endif /* WIRELESS_EXT >= 18 */

    union iwreq_data wrqu;
    int i;

    AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_SCAN,("AR6000 Neighbor Report Event\n"));
    for (i=0; i < numAps; info++, i++) {
        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_SCAN,("bssid %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x ",
            info->bssid[0], info->bssid[1], info->bssid[2],
            info->bssid[3], info->bssid[4], info->bssid[5]));
        if (info->bssFlags & WMI_PREAUTH_CAPABLE_BSS) {
            AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_SCAN,("preauth-cap"));
        }
        if (info->bssFlags & WMI_PMKID_VALID_BSS) {
            AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_SCAN,(" pmkid-valid\n"));
            continue;           /* we skip bss if the pmkid is already valid */
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_SCAN,("\n"));
        A_MEMZERO(&wrqu, sizeof(wrqu));
#if WIRELESS_EXT >= 18
        pmkcand = A_MALLOC_NOWAIT(sizeof(struct iw_pmkid_cand));
        A_MEMZERO(pmkcand, sizeof(struct iw_pmkid_cand));
        pmkcand->index = i;
        pmkcand->flags = info->bssFlags;
        A_MEMCPY(pmkcand->bssid.sa_data, info->bssid, ATH_MAC_LEN);
        wrqu.data.length = sizeof(struct iw_pmkid_cand);
        wireless_send_event(ar->arNetDev, IWEVPMKIDCAND, &wrqu, (char *)pmkcand);
        A_FREE(pmkcand);
#else /* WIRELESS_EXT >= 18 */
        snprintf(buf, sizeof(buf), "%s%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x",
                 tag,
                 info->bssid[0], info->bssid[1], info->bssid[2],
                 info->bssid[3], info->bssid[4], info->bssid[5],
                 i, info->bssFlags);
        wrqu.data.length = strlen(buf);
        wireless_send_event(ar->arNetDev, IWEVCUSTOM, &wrqu, buf);
#endif /* WIRELESS_EXT >= 18 */
    }
}

void
ar6000_tkip_micerr_event(AR_SOFTC_T *ar, A_UINT8 keyid, A_BOOL ismcast)
{
    static const char *tag = "MLME-MICHAELMICFAILURE.indication";
    char buf[128];
    union iwreq_data wrqu;

    /*
     * For AP case, keyid will have aid of STA which sent pkt with
     * MIC error. Use this aid to get MAC & send it to hostapd.
     */
    if (ar->arNetworkType == AP_NETWORK) {
        sta_t *s = ieee80211_find_conn_for_aid(ar, (keyid >> 2));
        if(!s){
            A_PRINTF("AP TKIP MIC error received from Invalid aid / STA not found =%d\n", keyid);
            return;
        }
        A_PRINTF("AP TKIP MIC error received from aid=%d\n", keyid);
        snprintf(buf,sizeof(buf), "%s addr=%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x",
            tag, s->mac[0],s->mac[1],s->mac[2],s->mac[3],s->mac[4],s->mac[5]);
    } else {

#ifdef ATH6K_CONFIG_CFG80211
    ar6k_cfg80211_tkip_micerr_event(ar, keyid, ismcast);
#endif /* ATH6K_CONFIG_CFG80211 */

        A_PRINTF("AR6000 TKIP MIC error received for keyid %d %scast\n",
             keyid & 0x3, ismcast ? "multi": "uni");
        snprintf(buf, sizeof(buf), "%s(keyid=%d %sicast)", tag, keyid & 0x3,
             ismcast ? "mult" : "un");
    }

    memset(&wrqu, 0, sizeof(wrqu));
    wrqu.data.length = strlen(buf);
    wireless_send_event(ar->arNetDev, IWEVCUSTOM, &wrqu, buf);
}

void
ar6000_scanComplete_event(AR_SOFTC_T *ar, A_STATUS status)
{

#ifdef ATH6K_CONFIG_CFG80211
    ar6k_cfg80211_scanComplete_event(ar, status);
#endif /* ATH6K_CONFIG_CFG80211 */

    if (!ar->arUserBssFilter) {
        wmi_bssfilter_cmd(ar->arWmi, NONE_BSS_FILTER, 0);
    }
    if (ar->scan_triggered) {
        if (status==A_OK) {
            union iwreq_data wrqu;
            A_MEMZERO(&wrqu, sizeof(wrqu));
            wireless_send_event(ar->arNetDev, SIOCGIWSCAN, &wrqu, NULL);
        }
        ar->scan_triggered = 0;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_SCAN,( "AR6000 scan complete: %d\n", status));
}

void
ar6000_targetStats_event(AR_SOFTC_T *ar,  A_UINT8 *ptr, A_UINT32 len)
{
    A_UINT8 ac;

    if(ar->arNetworkType == AP_NETWORK) {
        WMI_AP_MODE_STAT *p = (WMI_AP_MODE_STAT *)ptr;
        WMI_AP_MODE_STAT *ap = &ar->arAPStats;

        if (len < sizeof(*p)) {
            return;
        }

        for(ac=0;ac<AP_MAX_NUM_STA;ac++) {
            ap->sta[ac].tx_bytes   += p->sta[ac].tx_bytes;
            ap->sta[ac].tx_pkts    += p->sta[ac].tx_pkts;
            ap->sta[ac].tx_error   += p->sta[ac].tx_error;
            ap->sta[ac].tx_discard += p->sta[ac].tx_discard;
            ap->sta[ac].rx_bytes   += p->sta[ac].rx_bytes;
            ap->sta[ac].rx_pkts    += p->sta[ac].rx_pkts;
            ap->sta[ac].rx_error   += p->sta[ac].rx_error;
            ap->sta[ac].rx_discard += p->sta[ac].rx_discard;
        }

    } else {
        WMI_TARGET_STATS *pTarget = (WMI_TARGET_STATS *)ptr;
         TARGET_STATS *pStats = &ar->arTargetStats;

        if (len < sizeof(*pTarget)) {
            return;
        }

        // Update the RSSI of the connected bss.
        if (ar->arConnected) {
            bss_t *pConnBss = NULL;

            pConnBss = wmi_find_node(ar->arWmi,ar->arBssid);
            if (pConnBss)
            {
                pConnBss->ni_rssi = pTarget->cservStats.cs_aveBeacon_rssi;
                pConnBss->ni_snr = pTarget->cservStats.cs_aveBeacon_snr;
                wmi_node_return(ar->arWmi, pConnBss);
            }
        }

        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AR6000 updating target stats\n"));
        pStats->tx_packets          += pTarget->txrxStats.tx_stats.tx_packets;
        pStats->tx_bytes            += pTarget->txrxStats.tx_stats.tx_bytes;
        pStats->tx_unicast_pkts     += pTarget->txrxStats.tx_stats.tx_unicast_pkts;
        pStats->tx_unicast_bytes    += pTarget->txrxStats.tx_stats.tx_unicast_bytes;
        pStats->tx_multicast_pkts   += pTarget->txrxStats.tx_stats.tx_multicast_pkts;
        pStats->tx_multicast_bytes  += pTarget->txrxStats.tx_stats.tx_multicast_bytes;
        pStats->tx_broadcast_pkts   += pTarget->txrxStats.tx_stats.tx_broadcast_pkts;
        pStats->tx_broadcast_bytes  += pTarget->txrxStats.tx_stats.tx_broadcast_bytes;
        pStats->tx_rts_success_cnt  += pTarget->txrxStats.tx_stats.tx_rts_success_cnt;
        for(ac = 0; ac < WMM_NUM_AC; ac++)
            pStats->tx_packet_per_ac[ac] += pTarget->txrxStats.tx_stats.tx_packet_per_ac[ac];
        pStats->tx_errors           += pTarget->txrxStats.tx_stats.tx_errors;
        pStats->tx_failed_cnt       += pTarget->txrxStats.tx_stats.tx_failed_cnt;
        pStats->tx_retry_cnt        += pTarget->txrxStats.tx_stats.tx_retry_cnt;
        pStats->tx_mult_retry_cnt   += pTarget->txrxStats.tx_stats.tx_mult_retry_cnt;
        pStats->tx_rts_fail_cnt     += pTarget->txrxStats.tx_stats.tx_rts_fail_cnt;
        pStats->tx_unicast_rate      = wmi_get_rate(pTarget->txrxStats.tx_stats.tx_unicast_rate);

        pStats->rx_packets          += pTarget->txrxStats.rx_stats.rx_packets;
        pStats->rx_bytes            += pTarget->txrxStats.rx_stats.rx_bytes;
        pStats->rx_unicast_pkts     += pTarget->txrxStats.rx_stats.rx_unicast_pkts;
        pStats->rx_unicast_bytes    += pTarget->txrxStats.rx_stats.rx_unicast_bytes;
        pStats->rx_multicast_pkts   += pTarget->txrxStats.rx_stats.rx_multicast_pkts;
        pStats->rx_multicast_bytes  += pTarget->txrxStats.rx_stats.rx_multicast_bytes;
        pStats->rx_broadcast_pkts   += pTarget->txrxStats.rx_stats.rx_broadcast_pkts;
        pStats->rx_broadcast_bytes  += pTarget->txrxStats.rx_stats.rx_broadcast_bytes;
        pStats->rx_fragment_pkt     += pTarget->txrxStats.rx_stats.rx_fragment_pkt;
        pStats->rx_errors           += pTarget->txrxStats.rx_stats.rx_errors;
        pStats->rx_crcerr           += pTarget->txrxStats.rx_stats.rx_crcerr;
        pStats->rx_key_cache_miss   += pTarget->txrxStats.rx_stats.rx_key_cache_miss;
        pStats->rx_decrypt_err      += pTarget->txrxStats.rx_stats.rx_decrypt_err;
        pStats->rx_duplicate_frames += pTarget->txrxStats.rx_stats.rx_duplicate_frames;
        pStats->rx_unicast_rate      = wmi_get_rate(pTarget->txrxStats.rx_stats.rx_unicast_rate);


        pStats->tkip_local_mic_failure
                                += pTarget->txrxStats.tkipCcmpStats.tkip_local_mic_failure;
        pStats->tkip_counter_measures_invoked
                                += pTarget->txrxStats.tkipCcmpStats.tkip_counter_measures_invoked;
        pStats->tkip_replays        += pTarget->txrxStats.tkipCcmpStats.tkip_replays;
        pStats->tkip_format_errors  += pTarget->txrxStats.tkipCcmpStats.tkip_format_errors;
        pStats->ccmp_format_errors  += pTarget->txrxStats.tkipCcmpStats.ccmp_format_errors;
        pStats->ccmp_replays        += pTarget->txrxStats.tkipCcmpStats.ccmp_replays;

        pStats->power_save_failure_cnt += pTarget->pmStats.power_save_failure_cnt;
        pStats->noise_floor_calibation = pTarget->noise_floor_calibation;

        pStats->cs_bmiss_cnt        += pTarget->cservStats.cs_bmiss_cnt;
        pStats->cs_lowRssi_cnt      += pTarget->cservStats.cs_lowRssi_cnt;
        pStats->cs_connect_cnt      += pTarget->cservStats.cs_connect_cnt;
        pStats->cs_disconnect_cnt   += pTarget->cservStats.cs_disconnect_cnt;
        pStats->cs_aveBeacon_snr    = pTarget->cservStats.cs_aveBeacon_snr;
        pStats->cs_aveBeacon_rssi   = pTarget->cservStats.cs_aveBeacon_rssi;

        if (enablerssicompensation) {
            pStats->cs_aveBeacon_rssi =
                    rssi_compensation_calc(ar, pStats->cs_aveBeacon_rssi);
        }
        pStats->cs_lastRoam_msec    = pTarget->cservStats.cs_lastRoam_msec;
        pStats->cs_snr              = pTarget->cservStats.cs_snr;
        pStats->cs_rssi             = pTarget->cservStats.cs_rssi;

        pStats->lq_val              = pTarget->lqVal;

        pStats->wow_num_pkts_dropped += pTarget->wowStats.wow_num_pkts_dropped;
        pStats->wow_num_host_pkt_wakeups += pTarget->wowStats.wow_num_host_pkt_wakeups;
        pStats->wow_num_host_event_wakeups += pTarget->wowStats.wow_num_host_event_wakeups;
        pStats->wow_num_events_discarded += pTarget->wowStats.wow_num_events_discarded;
        pStats->arp_received += pTarget->arpStats.arp_received;
        pStats->arp_matched  += pTarget->arpStats.arp_matched;
        pStats->arp_replied  += pTarget->arpStats.arp_replied;

        if (ar->statsUpdatePending) {
            ar->statsUpdatePending = FALSE;
            wake_up(&arEvent);
        }
    }
}

void
ar6000_rssiThreshold_event(AR_SOFTC_T *ar,  WMI_RSSI_THRESHOLD_VAL newThreshold, A_INT16 rssi)
{
    USER_RSSI_THOLD userRssiThold;

    rssi = rssi + SIGNAL_QUALITY_NOISE_FLOOR;

    if (enablerssicompensation) {
        rssi = rssi_compensation_calc(ar, rssi);
    }

    /* Send an event to the app */
    userRssiThold.tag = ar->rssi_map[newThreshold].tag;
    userRssiThold.rssi = rssi;
    A_PRINTF("rssi Threshold range = %d tag = %d  rssi = %d\n", newThreshold,
             userRssiThold.tag, userRssiThold.rssi);

    ar6000_send_event_to_app(ar, WMI_RSSI_THRESHOLD_EVENTID,(A_UINT8 *)&userRssiThold, sizeof(USER_RSSI_THOLD));
}


void
ar6000_hbChallengeResp_event(AR_SOFTC_T *ar, A_UINT32 cookie, A_UINT32 source)
{
    if (source == APP_HB_CHALLENGE) {
        /* Report it to the app in case it wants a positive acknowledgement */
        ar6000_send_event_to_app(ar, WMIX_HB_CHALLENGE_RESP_EVENTID,
                                 (A_UINT8 *)&cookie, sizeof(cookie));
    } else {
        /* This would ignore the replys that come in after their due time */
        if (cookie == ar->arHBChallengeResp.seqNum) {
            ar->arHBChallengeResp.outstanding = FALSE;
        }
    }
}


void
ar6000_reportError_event(AR_SOFTC_T *ar, WMI_TARGET_ERROR_VAL errorVal)
{
	static const char * const errString[] = {
		[WMI_TARGET_PM_ERR_FAIL]    "WMI_TARGET_PM_ERR_FAIL",
		[WMI_TARGET_KEY_NOT_FOUND]  "WMI_TARGET_KEY_NOT_FOUND",
		[WMI_TARGET_DECRYPTION_ERR] "WMI_TARGET_DECRYPTION_ERR",
		[WMI_TARGET_BMISS]          "WMI_TARGET_BMISS",
		[WMI_PSDISABLE_NODE_JOIN]   "WMI_PSDISABLE_NODE_JOIN"
	};

    A_PRINTF("AR6000 Error on Target. Error = 0x%x\n", errorVal);

    /* One error is reported at a time, and errorval is a bitmask */
    if(errorVal & (errorVal - 1))
       return;

    A_PRINTF("AR6000 Error type = ");
    switch(errorVal)
    {
        case WMI_TARGET_PM_ERR_FAIL:
        case WMI_TARGET_KEY_NOT_FOUND:
        case WMI_TARGET_DECRYPTION_ERR:
        case WMI_TARGET_BMISS:
        case WMI_PSDISABLE_NODE_JOIN:
            A_PRINTF("%s\n", errString[errorVal]);
            break;
        default:
            A_PRINTF("INVALID\n");
            break;
    }

}


void
ar6000_cac_event(AR_SOFTC_T *ar, A_UINT8 ac, A_UINT8 cacIndication,
                 A_UINT8 statusCode, A_UINT8 *tspecSuggestion)
{
    WMM_TSPEC_IE    *tspecIe;

    /*
     * This is the TSPEC IE suggestion from AP.
     * Suggestion provided by AP under some error
     * cases, could be helpful for the host app.
     * Check documentation.
     */
    tspecIe = (WMM_TSPEC_IE *)tspecSuggestion;

    /*
     * What do we do, if we get TSPEC rejection? One thought
     * that comes to mind is implictly delete the pstream...
     */
    A_PRINTF("AR6000 CAC notification. "
                "AC = %d, cacIndication = 0x%x, statusCode = 0x%x\n",
                 ac, cacIndication, statusCode);
}

void
ar6000_channel_change_event(AR_SOFTC_T *ar, A_UINT16 oldChannel,
                            A_UINT16 newChannel)
{
    A_PRINTF("Channel Change notification\nOld Channel: %d, New Channel: %d\n",
             oldChannel, newChannel);
}

#define AR6000_PRINT_BSSID(_pBss)  do {     \
        A_PRINTF("%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x ",\
                 (_pBss)[0],(_pBss)[1],(_pBss)[2],(_pBss)[3],\
                 (_pBss)[4],(_pBss)[5]);  \
} while(0)

void
ar6000_roam_tbl_event(AR_SOFTC_T *ar, WMI_TARGET_ROAM_TBL *pTbl)
{
    A_UINT8 i;

    A_PRINTF("ROAM TABLE NO OF ENTRIES is %d ROAM MODE is %d\n",
              pTbl->numEntries, pTbl->roamMode);
    for (i= 0; i < pTbl->numEntries; i++) {
        A_PRINTF("[%d]bssid %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x ", i,
            pTbl->bssRoamInfo[i].bssid[0], pTbl->bssRoamInfo[i].bssid[1],
            pTbl->bssRoamInfo[i].bssid[2],
            pTbl->bssRoamInfo[i].bssid[3],
            pTbl->bssRoamInfo[i].bssid[4],
            pTbl->bssRoamInfo[i].bssid[5]);
        A_PRINTF("RSSI %d RSSIDT %d LAST RSSI %d UTIL %d ROAM_UTIL %d"
                 " BIAS %d\n",
            pTbl->bssRoamInfo[i].rssi,
            pTbl->bssRoamInfo[i].rssidt,
            pTbl->bssRoamInfo[i].last_rssi,
            pTbl->bssRoamInfo[i].util,
            pTbl->bssRoamInfo[i].roam_util,
            pTbl->bssRoamInfo[i].bias);
    }
}

void
ar6000_wow_list_event(struct ar6_softc *ar, A_UINT8 num_filters, WMI_GET_WOW_LIST_REPLY *wow_reply)
{
    A_UINT8 i,j;

    /*Each event now contains exactly one filter, see bug 26613*/
    A_PRINTF("WOW pattern %d of %d patterns\n", wow_reply->this_filter_num,                 wow_reply->num_filters);
    A_PRINTF("wow mode = %s host mode = %s\n",
            (wow_reply->wow_mode == 0? "disabled":"enabled"),
            (wow_reply->host_mode == 1 ? "awake":"asleep"));


    /*If there are no patterns, the reply will only contain generic
      WoW information. Pattern information will exist only if there are
      patterns present. Bug 26716*/

   /* If this event contains pattern information, display it*/
    if (wow_reply->this_filter_num) {
        i=0;
        A_PRINTF("id=%d size=%d offset=%d\n",
                    wow_reply->wow_filters[i].wow_filter_id,
                    wow_reply->wow_filters[i].wow_filter_size,
                    wow_reply->wow_filters[i].wow_filter_offset);
       A_PRINTF("wow pattern = ");
       for (j=0; j< wow_reply->wow_filters[i].wow_filter_size; j++) {
             A_PRINTF("%2.2x",wow_reply->wow_filters[i].wow_filter_pattern[j]);
        }

        A_PRINTF("\nwow mask = ");
        for (j=0; j< wow_reply->wow_filters[i].wow_filter_size; j++) {
            A_PRINTF("%2.2x",wow_reply->wow_filters[i].wow_filter_mask[j]);
        }
        A_PRINTF("\n");
    }
}

/*
 * Report the Roaming related data collected on the target
 */
void
ar6000_display_roam_time(WMI_TARGET_ROAM_TIME *p)
{
    A_PRINTF("Disconnect Data : BSSID: ");
    AR6000_PRINT_BSSID(p->disassoc_bssid);
    A_PRINTF(" RSSI %d DISASSOC Time %d NO_TXRX_TIME %d\n",
             p->disassoc_bss_rssi,p->disassoc_time,
             p->no_txrx_time);
    A_PRINTF("Connect Data: BSSID: ");
    AR6000_PRINT_BSSID(p->assoc_bssid);
    A_PRINTF(" RSSI %d ASSOC Time %d TXRX_TIME %d\n",
             p->assoc_bss_rssi,p->assoc_time,
             p->allow_txrx_time);
}

void
ar6000_roam_data_event(AR_SOFTC_T *ar, WMI_TARGET_ROAM_DATA *p)
{
    switch (p->roamDataType) {
        case ROAM_DATA_TIME:
            ar6000_display_roam_time(&p->u.roamTime);
            break;
        default:
            break;
    }
}

void
ar6000_bssInfo_event_rx(AR_SOFTC_T *ar, A_UINT8 *datap, int len)
{
    struct sk_buff *skb;
    WMI_BSS_INFO_HDR *bih = (WMI_BSS_INFO_HDR *)datap;


    if (!ar->arMgmtFilter) {
        return;
    }
    if (((ar->arMgmtFilter & IEEE80211_FILTER_TYPE_BEACON) &&
        (bih->frameType != BEACON_FTYPE))  ||
        ((ar->arMgmtFilter & IEEE80211_FILTER_TYPE_PROBE_RESP) &&
        (bih->frameType != PROBERESP_FTYPE)))
    {
        return;
    }

    if ((skb = A_NETBUF_ALLOC_RAW(len)) != NULL) {

        A_NETBUF_PUT(skb, len);
        A_MEMCPY(A_NETBUF_DATA(skb), datap, len);
        skb->dev = ar->arNetDev;
        A_MEMCPY(skb_mac_header(skb), A_NETBUF_DATA(skb), 6);
        skb->ip_summed = CHECKSUM_NONE;
        skb->pkt_type = PACKET_OTHERHOST;
        skb->protocol = __constant_htons(0x0019);
        netif_rx(skb);
    }
}

A_UINT32 wmiSendCmdNum;

A_STATUS
ar6000_control_tx(void *devt, void *osbuf, HTC_ENDPOINT_ID eid)
{
    AR_SOFTC_T       *ar = (AR_SOFTC_T *)devt;
    A_STATUS         status = A_OK;
    struct ar_cookie *cookie = NULL;
    int i;
#ifdef CONFIG_PM
    if (ar->arWowState != WLAN_WOW_STATE_NONE) {
        A_NETBUF_FREE(osbuf);
        return A_EACCES;
    }
#endif /* CONFIG_PM */
        /* take lock to protect ar6000_alloc_cookie() */
    AR6000_SPIN_LOCK(&ar->arLock, 0);

    do {

        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_TX,("ar_contrstatus = ol_tx: skb=0x%lx, len=0x%x eid =%d\n",
                         (unsigned long)osbuf, A_NETBUF_LEN(osbuf), eid));

        if (ar->arWMIControlEpFull && (eid == ar->arControlEp)) {
                /* control endpoint is full, don't allocate resources, we
                 * are just going to drop this packet */
            cookie = NULL;
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" WMI Control EP full, dropping packet : 0x%lX, len:%d \n",
                    (unsigned long)osbuf, A_NETBUF_LEN(osbuf)));
        } else {
            cookie = ar6000_alloc_cookie(ar);
        }

        if (cookie == NULL) {
            status = A_NO_MEMORY;
            break;
        }

        if(logWmiRawMsgs) {
            A_PRINTF("WMI cmd send, msgNo %d :", wmiSendCmdNum);
            for(i = 0; i < a_netbuf_to_len(osbuf); i++)
                A_PRINTF("%x ", ((A_UINT8 *)a_netbuf_to_data(osbuf))[i]);
            A_PRINTF("\n");
        }

        wmiSendCmdNum++;

    } while (FALSE);

    if (cookie != NULL) {
            /* got a structure to send it out on */
        ar->arTxPending[eid]++;

        if (eid != ar->arControlEp) {
            ar->arTotalTxDataPending++;
        }
    }

    AR6000_SPIN_UNLOCK(&ar->arLock, 0);

    if (cookie != NULL) {
        cookie->arc_bp[0] = (unsigned long)osbuf;
        cookie->arc_bp[1] = 0;
        SET_HTC_PACKET_INFO_TX(&cookie->HtcPkt,
                               cookie,
                               A_NETBUF_DATA(osbuf),
                               A_NETBUF_LEN(osbuf),
                               eid,
                               AR6K_CONTROL_PKT_TAG);
            /* this interface is asynchronous, if there is an error, cleanup will happen in the
             * TX completion callback */
        HTCSendPkt(ar->arHtcTarget, &cookie->HtcPkt);
        status = A_OK;
    }

    if (status != A_OK) {
        A_NETBUF_FREE(osbuf);
    }
    return status;
}

/* indicate tx activity or inactivity on a WMI stream */
void ar6000_indicate_tx_activity(void *devt, A_UINT8 TrafficClass, A_BOOL Active)
{
    AR_SOFTC_T  *ar = (AR_SOFTC_T *)devt;
    HTC_ENDPOINT_ID eid ;
    int i;

    if (ar->arWmiEnabled) {
        eid = arAc2EndpointID(ar, TrafficClass);

        AR6000_SPIN_LOCK(&ar->arLock, 0);

        ar->arAcStreamActive[TrafficClass] = Active;

        if (Active) {
            /* when a stream goes active, keep track of the active stream with the highest priority */

            if (ar->arAcStreamPriMap[TrafficClass] > ar->arHiAcStreamActivePri) {
                    /* set the new highest active priority */
                ar->arHiAcStreamActivePri = ar->arAcStreamPriMap[TrafficClass];
            }

        } else {
            /* when a stream goes inactive, we may have to search for the next active stream
             * that is the highest priority */

            if (ar->arHiAcStreamActivePri == ar->arAcStreamPriMap[TrafficClass]) {

                /* the highest priority stream just went inactive */

                    /* reset and search for the "next" highest "active" priority stream */
                ar->arHiAcStreamActivePri = 0;
                for (i = 0; i < WMM_NUM_AC; i++) {
                    if (ar->arAcStreamActive[i]) {
                        if (ar->arAcStreamPriMap[i] > ar->arHiAcStreamActivePri) {
                            /* set the new highest active priority */
                            ar->arHiAcStreamActivePri = ar->arAcStreamPriMap[i];
                        }
                    }
                }
            }
        }

        AR6000_SPIN_UNLOCK(&ar->arLock, 0);

    } else {
            /* for mbox ping testing, the traffic class is mapped directly as a stream ID,
             * see handling of AR6000_XIOCTL_TRAFFIC_ACTIVITY_CHANGE in ioctl.c
             * convert the stream ID to a endpoint */
        eid = arAc2EndpointID(ar, TrafficClass);
    }

        /* notify HTC, this may cause credit distribution changes */

    HTCIndicateActivityChange(ar->arHtcTarget,
                              eid,
                              Active);

}

void
ar6000_btcoex_config_event(struct ar6_softc *ar,  A_UINT8 *ptr, A_UINT32 len)
{

    WMI_BTCOEX_CONFIG_EVENT *pBtcoexConfig = (WMI_BTCOEX_CONFIG_EVENT *)ptr;
    WMI_BTCOEX_CONFIG_EVENT *pArbtcoexConfig =&ar->arBtcoexConfig;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AR6000 BTCOEX CONFIG EVENT \n"));

    A_PRINTF("received config event\n");
    pArbtcoexConfig->btProfileType = pBtcoexConfig->btProfileType;
    pArbtcoexConfig->linkId = pBtcoexConfig->linkId;

    switch (pBtcoexConfig->btProfileType) {
        case WMI_BTCOEX_BT_PROFILE_SCO:
            A_MEMCPY(&pArbtcoexConfig->info.scoConfigCmd, &pBtcoexConfig->info.scoConfigCmd,
                                        sizeof(WMI_SET_BTCOEX_SCO_CONFIG_CMD));
            break;
        case WMI_BTCOEX_BT_PROFILE_A2DP:
            A_MEMCPY(&pArbtcoexConfig->info.a2dpConfigCmd, &pBtcoexConfig->info.a2dpConfigCmd,
                                        sizeof(WMI_SET_BTCOEX_A2DP_CONFIG_CMD));
            break;
        case WMI_BTCOEX_BT_PROFILE_ACLCOEX:
            A_MEMCPY(&pArbtcoexConfig->info.aclcoexConfig, &pBtcoexConfig->info.aclcoexConfig,
                                        sizeof(WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD));
            break;
        case WMI_BTCOEX_BT_PROFILE_INQUIRY_PAGE:
           A_MEMCPY(&pArbtcoexConfig->info.btinquiryPageConfigCmd, &pBtcoexConfig->info.btinquiryPageConfigCmd,
                                        sizeof(WMI_SET_BTCOEX_ACLCOEX_CONFIG_CMD));
            break;
    }
    if (ar->statsUpdatePending) {
         ar->statsUpdatePending = FALSE;
          wake_up(&arEvent);
    }
}

void
ar6000_btcoex_stats_event(struct ar6_softc *ar,  A_UINT8 *ptr, A_UINT32 len)
{
    WMI_BTCOEX_STATS_EVENT *pBtcoexStats = (WMI_BTCOEX_STATS_EVENT *)ptr;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AR6000 BTCOEX CONFIG EVENT \n"));

    A_MEMCPY(&ar->arBtcoexStats, pBtcoexStats, sizeof(WMI_BTCOEX_STATS_EVENT));

    if (ar->statsUpdatePending) {
         ar->statsUpdatePending = FALSE;
        wake_up(&arEvent);
    }

}
module_init(ar6000_init_module);
module_exit(ar6000_cleanup_module);

/* Init cookie queue */
static void
ar6000_cookie_init(AR_SOFTC_T *ar)
{
    A_UINT32    i;

    ar->arCookieList = NULL;
    ar->arCookieCount = 0;

    A_MEMZERO(s_ar_cookie_mem, sizeof(s_ar_cookie_mem));

    for (i = 0; i < MAX_COOKIE_NUM; i++) {
        ar6000_free_cookie(ar, &s_ar_cookie_mem[i]);
    }
}

/* cleanup cookie queue */
static void
ar6000_cookie_cleanup(AR_SOFTC_T *ar)
{
    /* It is gone .... */
    ar->arCookieList = NULL;
    ar->arCookieCount = 0;
}

/* Init cookie queue */
static void
ar6000_free_cookie(AR_SOFTC_T *ar, struct ar_cookie * cookie)
{
    /* Insert first */
    A_ASSERT(ar != NULL);
    A_ASSERT(cookie != NULL);

    cookie->arc_list_next = ar->arCookieList;
    ar->arCookieList = cookie;
    ar->arCookieCount++;
}

/* cleanup cookie queue */
static struct ar_cookie *
ar6000_alloc_cookie(AR_SOFTC_T  *ar)
{
    struct ar_cookie   *cookie;

    cookie = ar->arCookieList;
    if(cookie != NULL)
    {
        ar->arCookieList = cookie->arc_list_next;
        ar->arCookieCount--;
    }

    return cookie;
}

#ifdef SEND_EVENT_TO_APP
/*
 * This function is used to send event which come from taget to
 * the application. The buf which send to application is include
 * the event ID and event content.
 */
#define EVENT_ID_LEN   2
void ar6000_send_event_to_app(AR_SOFTC_T *ar, A_UINT16 eventId,
                              A_UINT8 *datap, int len)
{

#if (WIRELESS_EXT >= 15)

/* note: IWEVCUSTOM only exists in wireless extensions after version 15 */

    char *buf;
    A_UINT16 size;
    union iwreq_data wrqu;

    size = len + EVENT_ID_LEN;

    if (size > IW_CUSTOM_MAX) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("WMI event ID : 0x%4.4X, len = %d too big for IWEVCUSTOM (max=%d) \n",
                eventId, size, IW_CUSTOM_MAX));
        return;
    }

    buf = A_MALLOC_NOWAIT(size);
    if (NULL == buf){
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s: failed to allocate %d bytes\n", __func__, size));
        return;
    }

    A_MEMZERO(buf, size);
    A_MEMCPY(buf, &eventId, EVENT_ID_LEN);
    A_MEMCPY(buf+EVENT_ID_LEN, datap, len);

    //AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("event ID = %d,len = %d\n",*(A_UINT16*)buf, size));
    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = size;
    wireless_send_event(ar->arNetDev, IWEVCUSTOM, &wrqu, buf);
    A_FREE(buf);
#endif


}

/*
 * This function is used to send events larger than 256 bytes
 * to the application. The buf which is sent to application
 * includes the event ID and event content.
 */
void ar6000_send_generic_event_to_app(AR_SOFTC_T *ar, A_UINT16 eventId,
                                      A_UINT8 *datap, int len)
{

#if (WIRELESS_EXT >= 18)

/* IWEVGENIE exists in wireless extensions version 18 onwards */

    char *buf;
    A_UINT16 size;
    union iwreq_data wrqu;

    size = len + EVENT_ID_LEN;

    if (size > IW_GENERIC_IE_MAX) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("WMI event ID : 0x%4.4X, len = %d too big for IWEVGENIE (max=%d) \n",
                        eventId, size, IW_GENERIC_IE_MAX));
        return;
    }

    buf = A_MALLOC_NOWAIT(size);
    if (NULL == buf){
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s: failed to allocate %d bytes\n", __func__, size));
        return;
    }

    A_MEMZERO(buf, size);
    A_MEMCPY(buf, &eventId, EVENT_ID_LEN);
    A_MEMCPY(buf+EVENT_ID_LEN, datap, len);

    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = size;
    wireless_send_event(ar->arNetDev, IWEVGENIE, &wrqu, buf);

    A_FREE(buf);

#endif /* (WIRELESS_EXT >= 18) */

}
#endif /* SEND_EVENT_TO_APP */


void
ar6000_tx_retry_err_event(void *devt)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Tx retries reach maximum!\n"));
}

void
ar6000_snrThresholdEvent_rx(void *devt, WMI_SNR_THRESHOLD_VAL newThreshold, A_UINT8 snr)
{
    WMI_SNR_THRESHOLD_EVENT event;
    AR_SOFTC_T *ar = (AR_SOFTC_T *)devt;

    event.range = newThreshold;
    event.snr = snr;

    ar6000_send_event_to_app(ar, WMI_SNR_THRESHOLD_EVENTID, (A_UINT8 *)&event,
                             sizeof(WMI_SNR_THRESHOLD_EVENT));
}

void
ar6000_lqThresholdEvent_rx(void *devt, WMI_LQ_THRESHOLD_VAL newThreshold, A_UINT8 lq)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("lq threshold range %d, lq %d\n", newThreshold, lq));
}



A_UINT32
a_copy_to_user(void *to, const void *from, A_UINT32 n)
{
    return(copy_to_user(to, from, n));
}

A_UINT32
a_copy_from_user(void *to, const void *from, A_UINT32 n)
{
    return(copy_from_user(to, from, n));
}


A_STATUS
ar6000_get_driver_cfg(struct net_device *dev,
                        A_UINT16 cfgParam,
                        void *result)
{

    A_STATUS    ret = 0;

    switch(cfgParam)
    {
        case AR6000_DRIVER_CFG_GET_WLANNODECACHING:
           *((A_UINT32 *)result) = wlanNodeCaching;
           break;
        case AR6000_DRIVER_CFG_LOG_RAW_WMI_MSGS:
           *((A_UINT32 *)result) = logWmiRawMsgs;
            break;
        default:
           ret = EINVAL;
           break;
    }

    return ret;
}

void
ar6000_keepalive_rx(void *devt, A_UINT8 configured)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)devt;

    ar->arKeepaliveConfigured = configured;
    wake_up(&arEvent);
}

void
ar6000_pmkid_list_event(void *devt, A_UINT8 numPMKID, WMI_PMKID *pmkidList,
                        A_UINT8 *bssidList)
{
    A_UINT8 i, j;

    A_PRINTF("Number of Cached PMKIDs is %d\n", numPMKID);

    for (i = 0; i < numPMKID; i++) {
        A_PRINTF("\nBSSID %d ", i);
            for (j = 0; j < ATH_MAC_LEN; j++) {
                A_PRINTF("%2.2x", bssidList[j]);
            }
        bssidList += (ATH_MAC_LEN + WMI_PMKID_LEN);
        A_PRINTF("\nPMKID %d ", i);
            for (j = 0; j < WMI_PMKID_LEN; j++) {
                A_PRINTF("%2.2x", pmkidList->pmkid[j]);
            }
        pmkidList = (WMI_PMKID *)((A_UINT8 *)pmkidList + ATH_MAC_LEN +
                                  WMI_PMKID_LEN);
    }
}

void ar6000_pspoll_event(AR_SOFTC_T *ar,A_UINT8 aid)
{
    sta_t *conn=NULL;
    A_BOOL isPsqEmpty = FALSE;

    conn = ieee80211_find_conn_for_aid(ar, aid);

    /* If the PS q for this STA is not empty, dequeue and send a pkt from
     * the head of the q. Also update the More data bit in the WMI_DATA_HDR
     * if there are more pkts for this STA in the PS q. If there are no more
     * pkts for this STA, update the PVB for this STA.
     */
    A_MUTEX_LOCK(&conn->psqLock);
    isPsqEmpty  = A_NETBUF_QUEUE_EMPTY(&conn->psq);
    A_MUTEX_UNLOCK(&conn->psqLock);

    if (isPsqEmpty) {
        /* TODO:No buffered pkts for this STA. Send out a NULL data frame */
    } else {
        struct sk_buff *skb = NULL;

        A_MUTEX_LOCK(&conn->psqLock);
        skb = A_NETBUF_DEQUEUE(&conn->psq);
        A_MUTEX_UNLOCK(&conn->psqLock);
        /* Set the STA flag to PSPolled, so that the frame will go out */
        STA_SET_PS_POLLED(conn);
        ar6000_data_tx(skb, ar->arNetDev);
        STA_CLR_PS_POLLED(conn);

        /* Clear the PVB for this STA if the queue has become empty */
        A_MUTEX_LOCK(&conn->psqLock);
        isPsqEmpty  = A_NETBUF_QUEUE_EMPTY(&conn->psq);
        A_MUTEX_UNLOCK(&conn->psqLock);

        if (isPsqEmpty) {
            wmi_set_pvb_cmd(ar->arWmi, conn->aid, 0);
        }
    }
}

void ar6000_dtimexpiry_event(AR_SOFTC_T *ar)
{
    A_BOOL isMcastQueued = FALSE;
    struct sk_buff *skb = NULL;

    /* If there are no associated STAs, ignore the DTIM expiry event.
     * There can be potential race conditions where the last associated
     * STA may disconnect & before the host could clear the 'Indicate DTIM'
     * request to the firmware, the firmware would have just indicated a DTIM
     * expiry event. The race is between 'clear DTIM expiry cmd' going
     * from the host to the firmware & the DTIM expiry event happening from
     * the firmware to the host.
     */
    if (ar->sta_list_index == 0) {
        return;
    }

    A_MUTEX_LOCK(&ar->mcastpsqLock);
    isMcastQueued = A_NETBUF_QUEUE_EMPTY(&ar->mcastpsq);
    A_MUTEX_UNLOCK(&ar->mcastpsqLock);

    A_ASSERT(isMcastQueued == FALSE);

    /* Flush the mcast psq to the target */
    /* Set the STA flag to DTIMExpired, so that the frame will go out */
    ar->DTIMExpired = TRUE;

    A_MUTEX_LOCK(&ar->mcastpsqLock);
    while (!A_NETBUF_QUEUE_EMPTY(&ar->mcastpsq)) {
        skb = A_NETBUF_DEQUEUE(&ar->mcastpsq);
        A_MUTEX_UNLOCK(&ar->mcastpsqLock);

        ar6000_data_tx(skb, ar->arNetDev);

        A_MUTEX_LOCK(&ar->mcastpsqLock);
    }
    A_MUTEX_UNLOCK(&ar->mcastpsqLock);

    /* Reset the DTIMExpired flag back to 0 */
    ar->DTIMExpired = FALSE;

    /* Clear the LSB of the BitMapCtl field of the TIM IE */
    wmi_set_pvb_cmd(ar->arWmi, MCAST_AID, 0);
}

void
read_rssi_compensation_param(AR_SOFTC_T *ar)
{
    A_UINT8 *cust_data_ptr;

//#define RSSICOMPENSATION_PRINT

#ifdef RSSICOMPENSATION_PRINT
    A_INT16 i;
    cust_data_ptr = ar6000_get_cust_data_buffer(ar->arTargetType);
    for (i=0; i<16; i++) {
        A_PRINTF("cust_data_%d = %x \n", i, *(A_UINT8 *)cust_data_ptr);
        cust_data_ptr += 1;
    }
#endif

    cust_data_ptr = ar6000_get_cust_data_buffer(ar->arTargetType);

    rssi_compensation_param.customerID = *(A_UINT16 *)cust_data_ptr & 0xffff;
    rssi_compensation_param.enable = *(A_UINT16 *)(cust_data_ptr+2) & 0xffff;
    rssi_compensation_param.bg_param_a = *(A_UINT16 *)(cust_data_ptr+4) & 0xffff;
    rssi_compensation_param.bg_param_b = *(A_UINT16 *)(cust_data_ptr+6) & 0xffff;
    rssi_compensation_param.a_param_a = *(A_UINT16 *)(cust_data_ptr+8) & 0xffff;
    rssi_compensation_param.a_param_b = *(A_UINT16 *)(cust_data_ptr+10) &0xffff;
    rssi_compensation_param.reserved = *(A_UINT32 *)(cust_data_ptr+12);

#ifdef RSSICOMPENSATION_PRINT
    A_PRINTF("customerID = 0x%x \n", rssi_compensation_param.customerID);
    A_PRINTF("enable = 0x%x \n", rssi_compensation_param.enable);
    A_PRINTF("bg_param_a = 0x%x and %d \n", rssi_compensation_param.bg_param_a, rssi_compensation_param.bg_param_a);
    A_PRINTF("bg_param_b = 0x%x and %d \n", rssi_compensation_param.bg_param_b, rssi_compensation_param.bg_param_b);
    A_PRINTF("a_param_a = 0x%x and %d \n", rssi_compensation_param.a_param_a, rssi_compensation_param.a_param_a);
    A_PRINTF("a_param_b = 0x%x and %d \n", rssi_compensation_param.a_param_b, rssi_compensation_param.a_param_b);
    A_PRINTF("Last 4 bytes = 0x%x \n", rssi_compensation_param.reserved);
#endif

    if (rssi_compensation_param.enable != 0x1) {
        rssi_compensation_param.enable = 0;
    }

   return;
}

A_INT32
rssi_compensation_calc_tcmd(A_UINT32 freq, A_INT32 rssi, A_UINT32 totalPkt)
{

    if (freq > 5000)
    {
        if (rssi_compensation_param.enable)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, (">>> 11a\n"));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi before compensation  = %d, totalPkt = %d\n", rssi,totalPkt));
            rssi = rssi * rssi_compensation_param.a_param_a + totalPkt * rssi_compensation_param.a_param_b;
            rssi = (rssi-50) /100;
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi after compensation = %d\n", rssi));
        }
    }
    else
    {
        if (rssi_compensation_param.enable)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, (">>> 11bg\n"));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi before compensation  = %d, totalPkt = %d\n", rssi,totalPkt));
            rssi = rssi * rssi_compensation_param.bg_param_a + totalPkt * rssi_compensation_param.bg_param_b;
            rssi = (rssi-50) /100;
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi after compensation = %d\n", rssi));
        }
    }

    return rssi;
}

A_INT16
rssi_compensation_calc(AR_SOFTC_T *ar, A_INT16 rssi)
{
    if (ar->arBssChannel > 5000)
    {
        if (rssi_compensation_param.enable)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, (">>> 11a\n"));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi before compensation  = %d\n", rssi));
            rssi = rssi * rssi_compensation_param.a_param_a + rssi_compensation_param.a_param_b;
            rssi = (rssi-50) /100;
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi after compensation = %d\n", rssi));
        }
    }
    else
    {
        if (rssi_compensation_param.enable)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, (">>> 11bg\n"));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi before compensation  = %d\n", rssi));
            rssi = rssi * rssi_compensation_param.bg_param_a + rssi_compensation_param.bg_param_b;
            rssi = (rssi-50) /100;
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi after compensation = %d\n", rssi));
        }
    }

    return rssi;
}

A_INT16
rssi_compensation_reverse_calc(AR_SOFTC_T *ar, A_INT16 rssi, A_BOOL Above)
{
    A_INT16 i;

    if (ar->arBssChannel > 5000)
    {
        if (rssi_compensation_param.enable)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, (">>> 11a\n"));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi before rev compensation  = %d\n", rssi));
            rssi = rssi * 100;
            rssi = (rssi - rssi_compensation_param.a_param_b) / rssi_compensation_param.a_param_a;
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi after rev compensation = %d\n", rssi));
        }
    }
    else
    {
        if (rssi_compensation_param.enable)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, (">>> 11bg\n"));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi before rev compensation  = %d\n", rssi));

            if (Above) {
                for (i=95; i>=0; i--) {
                    if (rssi <=  rssi_compensation_table[i]) {
                        rssi = 0 - i;
                        break;
                    }
                }
            } else {
                for (i=0; i<=95; i++) {
                    if (rssi >=  rssi_compensation_table[i]) {
                        rssi = 0 - i;
                        break;
                    }
                }
            }
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi after rev compensation = %d\n", rssi));
        }
    }

    return rssi;
}

#ifdef WAPI_ENABLE
void ap_wapi_rekey_event(AR_SOFTC_T *ar, A_UINT8 type, A_UINT8 *mac)
{
    union iwreq_data wrqu;
    A_CHAR buf[20];

    A_MEMZERO(buf, sizeof(buf));

    strcpy(buf, "WAPI_REKEY");
    buf[10] = type;
    A_MEMCPY(&buf[11], mac, ATH_MAC_LEN);

    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = 10+1+ATH_MAC_LEN;
    wireless_send_event(ar->arNetDev, IWEVCUSTOM, &wrqu, buf);

    A_PRINTF("WAPI REKEY - %d - %02x:%02x\n", type, mac[4], mac[5]);
}
#endif

#ifdef USER_KEYS
static A_STATUS

ar6000_reinstall_keys(AR_SOFTC_T *ar, A_UINT8 key_op_ctrl)
{
    A_STATUS status = A_OK;
    struct ieee80211req_key *uik = &ar->user_saved_keys.ucast_ik;
    struct ieee80211req_key *bik = &ar->user_saved_keys.bcast_ik;
    CRYPTO_TYPE  keyType = ar->user_saved_keys.keyType;

    if (IEEE80211_CIPHER_CCKM_KRK != uik->ik_type) {
        if (NONE_CRYPT == keyType) {
            goto _reinstall_keys_out;
        }

        if (uik->ik_keylen) {
            status = wmi_addKey_cmd(ar->arWmi, uik->ik_keyix,
                    ar->user_saved_keys.keyType, PAIRWISE_USAGE,
                    uik->ik_keylen, (A_UINT8 *)&uik->ik_keyrsc,
                    uik->ik_keydata, key_op_ctrl, uik->ik_macaddr, SYNC_BEFORE_WMIFLAG);
        }

    } else {
        status = wmi_add_krk_cmd(ar->arWmi, uik->ik_keydata);
    }

    if (IEEE80211_CIPHER_CCKM_KRK != bik->ik_type) {
        if (NONE_CRYPT == keyType) {
            goto _reinstall_keys_out;
        }

        if (bik->ik_keylen) {
            status = wmi_addKey_cmd(ar->arWmi, bik->ik_keyix,
                    ar->user_saved_keys.keyType, GROUP_USAGE,
                    bik->ik_keylen, (A_UINT8 *)&bik->ik_keyrsc,
                    bik->ik_keydata, key_op_ctrl, bik->ik_macaddr, NO_SYNC_WMIFLAG);
        }
    } else {
        status = wmi_add_krk_cmd(ar->arWmi, bik->ik_keydata);
    }

_reinstall_keys_out:
    ar->user_savedkeys_stat = USER_SAVEDKEYS_STAT_INIT;
    ar->user_key_ctrl      = 0;

    return status;
}
#endif /* USER_KEYS */


void
ar6000_dset_open_req(
    void *context,
    A_UINT32 id,
    A_UINT32 targHandle,
    A_UINT32 targReplyFn,
    A_UINT32 targReplyArg)
{
}

void
ar6000_dset_close(
    void *context,
    A_UINT32 access_cookie)
{
    return;
}

void
ar6000_dset_data_req(
   void *context,
   A_UINT32 accessCookie,
   A_UINT32 offset,
   A_UINT32 length,
   A_UINT32 targBuf,
   A_UINT32 targReplyFn,
   A_UINT32 targReplyArg)
{
}

int
ar6000_ap_mode_profile_commit(struct ar6_softc *ar)
{
    WMI_CONNECT_CMD p;
    unsigned long  flags;

    /* No change in AP's profile configuration */
    if(ar->ap_profile_flag==0) {
        A_PRINTF("COMMIT: No change in profile!!!\n");
        return -ENODATA;
    }

    if(!ar->arSsidLen) {
        A_PRINTF("SSID not set!!!\n");
        return -ECHRNG;
    }

    switch(ar->arAuthMode) {
    case NONE_AUTH:
        if((ar->arPairwiseCrypto != NONE_CRYPT) &&
#ifdef WAPI_ENABLE
           (ar->arPairwiseCrypto != WAPI_CRYPT) &&
#endif
           (ar->arPairwiseCrypto != WEP_CRYPT)) {
            A_PRINTF("Cipher not supported in AP mode Open auth\n");
            return -EOPNOTSUPP;
        }
        break;
    case WPA_PSK_AUTH:
    case WPA2_PSK_AUTH:
    case (WPA_PSK_AUTH|WPA2_PSK_AUTH):
        break;
    default:
        A_PRINTF("This key mgmt type not supported in AP mode\n");
        return -EOPNOTSUPP;
    }

    /* Update the arNetworkType */
    ar->arNetworkType = ar->arNextMode;

    A_MEMZERO(&p,sizeof(p));
    p.ssidLength = ar->arSsidLen;
    A_MEMCPY(p.ssid,ar->arSsid,p.ssidLength);
    p.channel = ar->arChannelHint;
    p.networkType = ar->arNetworkType;

    p.dot11AuthMode = ar->arDot11AuthMode;
    p.authMode = ar->arAuthMode;
    p.pairwiseCryptoType = ar->arPairwiseCrypto;
    p.pairwiseCryptoLen = ar->arPairwiseCryptoLen;
    p.groupCryptoType = ar->arGroupCrypto;
    p.groupCryptoLen = ar->arGroupCryptoLen;
    p.ctrl_flags = ar->arConnectCtrlFlags;

    ar->arConnected = FALSE;

    wmi_ap_profile_commit(ar->arWmi, &p);
    spin_lock_irqsave(&ar->arLock, flags);
    ar->arConnected  = TRUE;
    netif_carrier_on(ar->arNetDev);
    spin_unlock_irqrestore(&ar->arLock, flags);
    ar->ap_profile_flag = 0;
    return 0;
}

A_STATUS
ar6000_connect_to_ap(struct ar6_softc *ar)
{
    /* The ssid length check prevents second "essid off" from the user,
       to be treated as a connect cmd. The second "essid off" is ignored.
    */
    if((ar->arWmiReady == TRUE) && (ar->arSsidLen > 0) && ar->arNetworkType!=AP_NETWORK)
    {
        A_STATUS status;
        if((ADHOC_NETWORK != ar->arNetworkType) &&
           (NONE_AUTH==ar->arAuthMode)          &&
           (WEP_CRYPT==ar->arPairwiseCrypto)) {
                ar6000_install_static_wep_keys(ar);
        }

        if (!ar->arUserBssFilter) {
            if (wmi_bssfilter_cmd(ar->arWmi, ALL_BSS_FILTER, 0) != A_OK) {
                return -EIO;
            }
        }
#ifdef WAPI_ENABLE
        if (ar->arWapiEnable)  {
            ar->arPairwiseCrypto = WAPI_CRYPT;
            ar->arPairwiseCryptoLen = 0;
            ar->arGroupCrypto = WAPI_CRYPT;
            ar->arGroupCryptoLen = 0;
            ar->arAuthMode = NONE_AUTH;
            ar->arConnectCtrlFlags |= CONNECT_IGNORE_WPAx_GROUP_CIPHER;
        }
#endif
        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("Connect called with authmode %d dot11 auth %d"\
                        " PW crypto %d PW crypto Len %d GRP crypto %d"\
                        " GRP crypto Len %d\n",
                        ar->arAuthMode, ar->arDot11AuthMode,
                        ar->arPairwiseCrypto, ar->arPairwiseCryptoLen,
                        ar->arGroupCrypto, ar->arGroupCryptoLen));
        reconnect_flag = 0;
        /* Set the listen interval into 1000TUs or more. This value will be indicated to Ap in the conn.
           later set it back locally at the STA to 100/1000 TUs depending on the power mode */
        if ((ar->arNetworkType == INFRA_NETWORK)) {
            wmi_listeninterval_cmd(ar->arWmi, max(ar->arListenIntervalT, (A_UINT16)A_MAX_WOW_LISTEN_INTERVAL), 0);
        }
        status = wmi_connect_cmd(ar->arWmi, ar->arNetworkType,
                                 ar->arDot11AuthMode, ar->arAuthMode,
                                 ar->arPairwiseCrypto, ar->arPairwiseCryptoLen,
                                 ar->arGroupCrypto,ar->arGroupCryptoLen,
                                 ar->arSsidLen, ar->arSsid,
                                 ar->arReqBssid, ar->arChannelHint,
                                 ar->arConnectCtrlFlags);
        if (status != A_OK) {
            wmi_listeninterval_cmd(ar->arWmi, ar->arListenIntervalT, ar->arListenIntervalB);
            if (!ar->arUserBssFilter) {
                wmi_bssfilter_cmd(ar->arWmi, NONE_BSS_FILTER, 0);
            }
            return status;
        }

        if ((!(ar->arConnectCtrlFlags & CONNECT_DO_WPA_OFFLOAD)) &&
            ((WPA_PSK_AUTH == ar->arAuthMode) || (WPA2_PSK_AUTH == ar->arAuthMode)))
        {
            A_TIMEOUT_MS(&ar->disconnect_timer, A_DISCONNECT_TIMER_INTERVAL, 0);
        }

        ar->arConnectCtrlFlags &= ~CONNECT_DO_WPA_OFFLOAD;
        
        ar->arConnectPending = TRUE;
        return status;    
    }
    return A_ERROR;
}

A_STATUS
ar6000_ap_mode_get_wpa_ie(struct ar6_softc *ar, struct ieee80211req_wpaie *wpaie)
{
    sta_t *conn = NULL;
    conn = ieee80211_find_conn(ar, wpaie->wpa_macaddr);

    A_MEMZERO(wpaie->wpa_ie, IEEE80211_MAX_IE);
    A_MEMZERO(wpaie->rsn_ie, IEEE80211_MAX_IE);

    if(conn) {
        A_MEMCPY(wpaie->wpa_ie, conn->wpa_ie, IEEE80211_MAX_IE);
    }

    return 0;
}

A_STATUS
is_iwioctl_allowed(A_UINT8 mode, A_UINT16 cmd)
{
    if(cmd >= SIOCSIWCOMMIT && cmd <= SIOCGIWPOWER) {
        cmd -= SIOCSIWCOMMIT;
        if(sioctl_filter[cmd] == 0xFF) return A_OK;
        if(sioctl_filter[cmd] & mode) return A_OK;
    } else if(cmd >= SIOCIWFIRSTPRIV && cmd <= (SIOCIWFIRSTPRIV+30)) {
        cmd -= SIOCIWFIRSTPRIV;
        if(pioctl_filter[cmd] == 0xFF) return A_OK;
        if(pioctl_filter[cmd] & mode) return A_OK;
    } else {
        return A_ERROR;
    }
    return A_ENOTSUP;
}

A_STATUS
is_xioctl_allowed(A_UINT8 mode, int cmd)
{
    if(sizeof(xioctl_filter)-1 < cmd) {
        A_PRINTF("Filter for this cmd=%d not defined\n",cmd);
        return 0;
    }
    if(xioctl_filter[cmd] == 0xFF) return A_OK;
    if(xioctl_filter[cmd] & mode) return A_OK;
    return A_ERROR;
}

#ifdef WAPI_ENABLE
int
ap_set_wapi_key(struct ar6_softc *ar, void *ikey)
{
    struct ieee80211req_key *ik = (struct ieee80211req_key *)ikey;
    KEY_USAGE   keyUsage = 0;
    A_STATUS    status;

    if (A_MEMCMP(ik->ik_macaddr, bcast_mac, IEEE80211_ADDR_LEN) == 0) {
        keyUsage = GROUP_USAGE;
    } else {
        keyUsage = PAIRWISE_USAGE;
    }
    A_PRINTF("WAPI_KEY: Type:%d ix:%d mac:%02x:%02x len:%d\n",
        keyUsage, ik->ik_keyix, ik->ik_macaddr[4], ik->ik_macaddr[5],
        ik->ik_keylen);

    status = wmi_addKey_cmd(ar->arWmi, ik->ik_keyix, WAPI_CRYPT, keyUsage,
                            ik->ik_keylen, (A_UINT8 *)&ik->ik_keyrsc,
                            ik->ik_keydata, KEY_OP_INIT_VAL, ik->ik_macaddr,
                            SYNC_BOTH_WMIFLAG);

    if (A_OK != status) {
        return -EIO;
    }
    return 0;
}
#endif

void ar6000_peer_event(
    void *context,
    A_UINT8 eventCode,
    A_UINT8 *macAddr)
{
    A_UINT8 pos;

    for (pos=0;pos<6;pos++)
        printk("%02x: ",*(macAddr+pos));
    printk("\n");
}

#ifdef HTC_TEST_SEND_PKTS
#define HTC_TEST_DUPLICATE 8
static void DoHTCSendPktsTest(AR_SOFTC_T *ar, int MapNo, HTC_ENDPOINT_ID eid, struct sk_buff *dupskb)
{
    struct ar_cookie *cookie;
    struct ar_cookie *cookieArray[HTC_TEST_DUPLICATE];
    struct sk_buff   *new_skb;
    int    i;
    int    pkts = 0;
    HTC_PACKET_QUEUE pktQueue;
    EPPING_HEADER    *eppingHdr;

    eppingHdr = A_NETBUF_DATA(dupskb);

    if (eppingHdr->Cmd_h == EPPING_CMD_NO_ECHO) {
        /* skip test if this is already a tx perf test */
        return;
    }

    for (i = 0; i < HTC_TEST_DUPLICATE; i++,pkts++) {
        AR6000_SPIN_LOCK(&ar->arLock, 0);
        cookie = ar6000_alloc_cookie(ar);
        if (cookie != NULL) {
            ar->arTxPending[eid]++;
            ar->arTotalTxDataPending++;
        }

        AR6000_SPIN_UNLOCK(&ar->arLock, 0);

        if (NULL == cookie) {
            break;
        }

        new_skb = A_NETBUF_ALLOC(A_NETBUF_LEN(dupskb));

        if (new_skb == NULL) {
            AR6000_SPIN_LOCK(&ar->arLock, 0);
            ar6000_free_cookie(ar,cookie);
            AR6000_SPIN_UNLOCK(&ar->arLock, 0);
            break;
        }

        A_NETBUF_PUT_DATA(new_skb, A_NETBUF_DATA(dupskb), A_NETBUF_LEN(dupskb));
        cookie->arc_bp[0] = (unsigned long)new_skb;
        cookie->arc_bp[1] = MapNo;
        SET_HTC_PACKET_INFO_TX(&cookie->HtcPkt,
                               cookie,
                               A_NETBUF_DATA(new_skb),
                               A_NETBUF_LEN(new_skb),
                               eid,
                               AR6K_DATA_PKT_TAG);

        cookieArray[i] = cookie;

        {
            EPPING_HEADER *pHdr = (EPPING_HEADER *)A_NETBUF_DATA(new_skb);
            pHdr->Cmd_h = EPPING_CMD_NO_ECHO;  /* do not echo the packet */
        }
    }

    if (pkts == 0) {
        return;
    }

    INIT_HTC_PACKET_QUEUE(&pktQueue);

    for (i = 0; i < pkts; i++) {
        HTC_PACKET_ENQUEUE(&pktQueue,&cookieArray[i]->HtcPkt);
    }

    HTCSendPktsMultiple(ar->arHtcTarget, &pktQueue);

}
#endif

#ifdef CONFIG_AP_VIRTUAL_ADAPTER_SUPPORT
/*
 * Add support for adding and removing a virtual adapter for soft AP.
 * Some OS requires different adapters names for station and soft AP mode.
 * To support these requirement, create and destory a netdevice  instance
 * when the AP mode is operational. A full fledged support for virual device
 * is not implemented. Rather a virtual interface is created and is linked
 * with the existing physical device instance during the operation of the 
 * AP mode.
 */

A_STATUS ar6000_start_ap_interface(AR_SOFTC_T *ar)
{
    AR_VIRTUAL_INTERFACE_T *arApDev;

    /* Change net_device to point to AP instance */
    arApDev = (AR_VIRTUAL_INTERFACE_T *)ar->arApDev;
    ar->arNetDev = arApDev->arNetDev;

    return A_OK;
}

A_STATUS ar6000_stop_ap_interface(AR_SOFTC_T *ar)
{
    AR_VIRTUAL_INTERFACE_T *arApDev;

    /* Change net_device to point to sta instance */
    arApDev = (AR_VIRTUAL_INTERFACE_T *)ar->arApDev;
    if (arApDev) {
        ar->arNetDev = arApDev->arStaNetDev;
    }

    return A_OK;
}


A_STATUS ar6000_create_ap_interface(AR_SOFTC_T *ar, char *ap_ifname) 
{
    struct net_device *dev;
    AR_VIRTUAL_INTERFACE_T *arApDev;

    dev = alloc_etherdev(sizeof(AR_VIRTUAL_INTERFACE_T));
    if (dev == NULL) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_create_ap_interface: can't alloc etherdev\n"));
        return A_ERROR;
    } 
    
    ether_setup(dev);
    init_netdev(dev, ap_ifname);

    if (register_netdev(dev)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_create_ap_interface: register_netdev failed\n"));
        return A_ERROR;
    }

    arApDev = netdev_priv(dev);
    arApDev->arDev = ar;
    arApDev->arNetDev = dev;
    arApDev->arStaNetDev = ar->arNetDev;

    ar->arApDev = arApDev;
    arApNetDev = dev;

    /* Copy the MAC address */
    A_MEMCPY(dev->dev_addr, ar->arNetDev->dev_addr, AR6000_ETH_ADDR_LEN);

    return A_OK;
}

A_STATUS ar6000_add_ap_interface(AR_SOFTC_T *ar, char *ap_ifname) 
{
    /* Interface already added, need not proceed further */
    if (ar->arApDev != NULL) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_add_ap_interface: interface already present \n"));
        return A_OK;
    }

    if (ar6000_create_ap_interface(ar, ap_ifname) != A_OK) {
        return A_ERROR;
    }

    A_PRINTF("Add AP interface %s \n",ap_ifname);

    return ar6000_start_ap_interface(ar);
}

A_STATUS ar6000_remove_ap_interface(AR_SOFTC_T *ar)
{
    if (arApNetDev) {
        ar6000_stop_ap_interface(ar);

        unregister_netdev(arApNetDev);
        free_netdev(apApNetDev);

        A_PRINTF("Remove AP interface\n");
    }
    ar->arApDev = NULL;
    arApNetDev = NULL;

    
    return A_OK;
}
#endif /* CONFIG_AP_VIRTUAL_ADAPTER_SUPPORT */


#ifdef EXPORT_HCI_BRIDGE_INTERFACE
EXPORT_SYMBOL(setupbtdev);
#endif
