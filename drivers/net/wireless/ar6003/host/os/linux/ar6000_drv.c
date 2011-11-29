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

#ifdef ATH_AR6K_11N_SUPPORT
#define SUPPORT_11N
#include "wlan_defs.h"
#endif

#include "ar6000_drv.h"
#ifdef ATH6K_CONFIG_CFG80211
#include "cfg80211.h"
#endif /* ATH6K_CONFIG_CFG80211 */
#include "htc.h"
#include "wmi_filter_linux.h"
#include "epping_test.h"
#include "wlan_config.h"
#include "ar3kconfig.h"
#include "dfs_host.h"
#include "ar6k_pal.h"
#include "AR6002/addrs.h"
#include "target_reg_table.h"
#include "p2p_api.h"

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
MODULE_LICENSE("GPL and additional rights");

#ifndef REORG_APTC_HEURISTICS
#undef ADAPTIVE_POWER_THROUGHPUT_CONTROL
#endif /* REORG_APTC_HEURISTICS */

#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
A_TIMER aptcTimer[NUM_DEV];
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
// callbacks registered by HCI transport driver
HCI_TRANSPORT_CALLBACKS ar6kHciTransCallbacks = { NULL };
#endif

unsigned int processDot11Hdr = 0;
char targetconf[10]={0,};
int bmienable = BMIENABLE_DEFAULT;

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
char ifname[IFNAMSIZ] = {0,};
char devmode[32] ={0,};
char submode[32] ={0,};
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) */

int regcode = 0x60;
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
unsigned int fwsubmode = 0;
unsigned int mbox_yield_limit = 99;
unsigned int enablerssicompensation = 0;
int reduce_credit_dribble = 1 + HTC_CONNECT_FLAGS_THRESHOLD_LEVEL_ONE_HALF;
int allow_trace_signal = 0;
#ifdef CONFIG_HOST_TCMD_SUPPORT
unsigned int testmode =0;
#endif
unsigned int firmware_bridge = 0;

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
unsigned int regscanmode=0;
unsigned int num_device=1;
unsigned char ar6k_init=FALSE;
unsigned int rtc_reset_only_on_exit=0;
unsigned int mac_addr_method=0;
A_BOOL avail_ev_called=FALSE;


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
module_param_string(ifname, ifname, sizeof(ifname), 0644);
module_param(regcode, int, 0644);
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
module_param(firmware_bridge, uint, 0644);
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
module_param(regscanmode, uint, 0644);
module_param_string(devmode, devmode, sizeof(devmode), 0644);
module_param_string(submode, submode, sizeof(submode), 0644);
module_param_string(targetconf, targetconf, sizeof(targetconf), 0644);
module_param(rtc_reset_only_on_exit, uint, 0644);
module_param(mac_addr_method, uint, 0644);
#else

#define __user
/* for linux 2.4 and lower */
MODULE_PARM(bmienable,"i");
MODULE_PARM(wlaninitmode,"i");
MODULE_PARM(bypasswmi,"i");
MODULE_PARM(debuglevel, "i");
MODULE_PARM(onebitmode,"i");
MODULE_PARM(busspeedlow, "i");
MODULE_PARM(skipflash, "i");
MODULE_PARM(wmitimeout, "i");
MODULE_PARM(wlanNodeCaching, "i");
MODULE_PARM(enableuartprint,"i");
MODULE_PARM(logWmiRawMsgs, "i");
MODULE_PARM(enabletimerwar,"i");
MODULE_PARM(mbox_yield_limit,"i");
MODULE_PARM(reduce_credit_dribble,"i");
MODULE_PARM(allow_trace_signal,"i");
MODULE_PARM(enablerssicompensation,"i");
MODULE_PARM(processDot11Hdr,"i");
#ifdef CONFIG_CHECKSUM_OFFLOAD
MODULE_PARM(csumOffload,"i");
#endif
#ifdef CONFIG_HOST_TCMD_SUPPORT
MODULE_PARM(testmode, "i");
#endif
MODULE_PARM(irqprocmode, "i");
MODULE_PARM(nohifscattersupport, "i");
MODULE_PARM(panic_on_assert, "i");
MODULE_PARM(setuphci, "i");
MODULE_PARM(setuphcipal, "i");
MODULE_PARM(loghci, "i");
MODULE_PARM(regscanmode, "i");
MODULE_PARM(rtc_reset_only_on_exit, "i");
MODULE_PARM(mac_addr_method, "i");
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,10)
/* in 2.6.10 and later this is now a pointer to a uint */
unsigned int _mboxnum = HTC_MAILBOX_NUM_MAX;
#define mboxnum &_mboxnum
#else
unsigned int mboxnum = HTC_MAILBOX_NUM_MAX;
#endif

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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
module_param(debugflags, uint, 0644);
module_param(debugdriver, int, 0644);
module_param(debughtc, uint, 0644);
module_param(debugbmi, uint, 0644);
module_param(debughif, uint, 0644);
module_param_array(txcreditsavailable, uint, mboxnum, 0644);
module_param_array(txcreditsconsumed, uint, mboxnum, 0644);
module_param_array(txcreditintrenable, uint, mboxnum, 0644);
module_param_array(txcreditintrenableaggregate, uint, mboxnum, 0644);
#else
/* linux 2.4 and lower */
MODULE_PARM(debugflags,"i");
MODULE_PARM(debugdriver, "i");
MODULE_PARM(debughtc, "i");
MODULE_PARM(debugbmi, "i");
MODULE_PARM(debughif, "i");
MODULE_PARM(txcreditsavailable, "0-3i");
MODULE_PARM(txcreditsconsumed, "0-3i");
MODULE_PARM(txcreditintrenable, "0-3i");
MODULE_PARM(txcreditintrenableaggregate, "0-3i");
#endif

#endif /* DEBUG */

#ifdef RK29
unsigned int resetok = 0;
#else
unsigned int resetok = 1;
#endif
unsigned int tx_attempt[HTC_MAILBOX_NUM_MAX] = {0};
unsigned int tx_post[HTC_MAILBOX_NUM_MAX] = {0};
unsigned int tx_complete[HTC_MAILBOX_NUM_MAX] = {0};
unsigned int hifBusRequestNumMax = 40;
unsigned int war23838_disabled = 0;
#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
unsigned int enableAPTCHeuristics = 1;
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
module_param_array(tx_attempt, uint, mboxnum, 0644);
module_param_array(tx_post, uint, mboxnum, 0644);
module_param_array(tx_complete, uint, mboxnum, 0644);
module_param(hifBusRequestNumMax, uint, 0644);
module_param(war23838_disabled, uint, 0644);
module_param(resetok, uint, 0644);
#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
module_param(enableAPTCHeuristics, uint, 0644);
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */
#else
MODULE_PARM(tx_attempt, "0-3i");
MODULE_PARM(tx_post, "0-3i");
MODULE_PARM(tx_complete, "0-3i");
MODULE_PARM(hifBusRequestNumMax, "i");
MODULE_PARM(war23838_disabled, "i");
MODULE_PARM(resetok, "i");
#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
MODULE_PARM(enableAPTCHeuristics, "i");
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */
#endif

#ifdef BLOCK_TX_PATH_FLAG
int blocktx = 0;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
module_param(blocktx, int, 0644);
#else
MODULE_PARM(blocktx, "i");
#endif
#endif /* BLOCK_TX_PATH_FLAG */

static A_INT16 rssi_compensation_table[NUM_DEV][96];

int reconnect_flag = 0;
static ar6k_pal_config_t ar6k_pal_config_g;

/* Function declarations */
static int ar6000_init_module(void);
static void ar6000_cleanup_module(void);

int ar6000_init(struct net_device *dev);
static int ar6000_open(struct net_device *dev);
static int ar6000_close(struct net_device *dev);
static int ar6000_init_control_info(AR_SOFTC_DEV_T *arPriv);
static int ar6000_data_tx(struct sk_buff *skb, struct net_device *dev);

void ar6000_destroy(struct net_device *dev, unsigned int unregister);
void ar6000_cleanup(AR_SOFTC_T *ar);
static void ar6000_detect_error(unsigned long ptr);
static void ar6000_set_multicast_list(struct net_device *dev);
static struct net_device_stats *ar6000_get_stats(struct net_device *dev);
static struct iw_statistics *ar6000_get_iwstats(struct net_device * dev);

static void disconnect_timer_handler(unsigned long ptr);
static void ap_acs_handler(unsigned long ptr);

void read_rssi_compensation_param(AR_SOFTC_T *ar);
void target_register_tbl_attach(A_UINT32 target_type);
static void ar6000_uapsd_trigger_frame_rx(AR_SOFTC_DEV_T *arPriv, conn_t *conn);

    /* for android builds we call external APIs that handle firmware download and configuration */
#ifdef ANDROID_ENV
/* !!!! Interim android support to make it easier to patch the default driver for
 * android use. You must define an external source file ar6000_android.c that handles the following
 * APIs */
extern void android_module_init(OSDRV_CALLBACKS *osdrvCallbacks);
extern void android_module_exit(void);
extern void android_send_reload_event(AR_SOFTC_DEV_T *arPriv);
#define ANDROID_RELOAD_THRESHOLD_FOR_EP_FULL 5
static int android_epfull_cnt;
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

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
static ssize_t
ar6000_sysfs_bmi_read(struct kobject *kobj, struct bin_attribute *bin_attr,
                      char *buf, loff_t pos, size_t count);

static ssize_t
ar6000_sysfs_bmi_write(struct kobject *kobj, struct bin_attribute *bin_attr,
                       char *buf, loff_t pos, size_t count);
#else
static ssize_t
ar6000_sysfs_bmi_read(struct file *fp, struct kobject *kobj,
                      struct bin_attribute *bin_attr,
                      char *buf, loff_t pos, size_t count);

static ssize_t
ar6000_sysfs_bmi_write(struct file *fp, struct kobject *kobj,
                       struct bin_attribute *bin_attr,
                       char *buf, loff_t pos, size_t count);
#endif

static A_STATUS
ar6000_sysfs_bmi_init(AR_SOFTC_T *ar);

/* HCI PAL callback function declarations */
A_STATUS ar6k_setup_hci_pal(AR_SOFTC_DEV_T *ar);
void  ar6k_cleanup_hci_pal(AR_SOFTC_DEV_T *ar);

static void
ar6000_sysfs_bmi_deinit(AR_SOFTC_T *ar);

A_STATUS
ar6000_sysfs_bmi_get_config(AR_SOFTC_T *ar, A_UINT32 mode);

/*
 * Static variables
 */

struct net_device *ar6000_devices[NUM_DEV];
extern struct iw_handler_def ath_iw_handler_def;
static void ar6000_cookie_init(AR_SOFTC_T *ar);
static void ar6000_cookie_cleanup(AR_SOFTC_T *ar);
static void ar6000_free_cookie(AR_SOFTC_T *ar, struct ar_cookie * cookie);
static struct ar_cookie *ar6000_alloc_cookie(AR_SOFTC_T *ar);

#ifdef USER_KEYS
static A_STATUS ar6000_reinstall_keys(AR_SOFTC_DEV_T *arPriv,A_UINT8 key_op_ctrl);
#endif


static struct ar_cookie s_ar_cookie_mem[MAX_COOKIE_NUM];

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29)
static struct net_device_ops ar6000_netdev_ops = {
    .ndo_init               = NULL,
    .ndo_open               = ar6000_open,
    .ndo_stop               = ar6000_close,
    .ndo_get_stats          = ar6000_get_stats,
    .ndo_do_ioctl           = ar6000_ioctl,
    .ndo_start_xmit         = ar6000_data_tx,
    .ndo_set_multicast_list = ar6000_set_multicast_list,
    .ndo_change_mtu         = eth_change_mtu,
};
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,29) */

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
    address = TARG_VTOP(ar->arTargetType, HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_app_host_interest));
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

    address = TARG_VTOP(ar->arTargetType, HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_dbglog_hdr));
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
ar6000_dbglog_init_done(AR_SOFTC_DEV_T *arPriv)
{
   AR_SOFTC_T *ar = arPriv->arSoftc;
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
    struct dbglog_hdr_s debug_hdr;
    struct dbglog_buf_s debug_buf;
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
        length = sizeof(struct dbglog_hdr_s);
        ar6000_ReadDataDiag(ar->arHifDevice, address,
                            (A_UCHAR *)&debug_hdr, length);
        address = TARG_VTOP(ar->arTargetType, (A_UINT32)debug_hdr.dbuf);
        firstbuf = address;
        dropped = debug_hdr.dropped;
        length = sizeof(struct dbglog_buf_s);
        ar6000_ReadDataDiag(ar->arHifDevice, address,
                            (A_UCHAR *)&debug_buf, length);

        do {
            address = TARG_VTOP(ar->arTargetType, (A_UINT32)debug_buf.buffer);
            length = debug_buf.length;
            if ((length) && (debug_buf.length <= debug_buf.bufsize)) {
                /* Rewind the index if it is about to overrun the buffer */
                if (ar->log_cnt > (DBGLOG_HOST_LOG_BUFFER_SIZE - length)) {
                    ar->log_cnt = 0;
                }
                if(A_OK != ar6000_ReadDataDiag(ar->arHifDevice, address,
                                    (A_UCHAR *)&ar->log_buffer[ar->log_cnt], length))
                {
                    break;
                }
                ar6000_dbglog_event(ar->arDev[0], dropped, (A_INT8*)&ar->log_buffer[ar->log_cnt], length);
                ar->log_cnt += length;
            } else {
                AR_DEBUG_PRINTF(ATH_DEBUG_DBG_LOG,("Length: %d (Total size: %d)\n",
                                debug_buf.length, debug_buf.bufsize));
            }

            address = TARG_VTOP(ar->arTargetType, (A_UINT32)debug_buf.next);
            length = sizeof(struct dbglog_buf_s);
            if(A_OK != ar6000_ReadDataDiag(ar->arHifDevice, address,
                                (A_UCHAR *)&debug_buf, length))
            {
                break;
            }

        } while (address != firstbuf);
    }

    ar->dbgLogFetchInProgress = FALSE;

    return A_OK;
}

void
ar6000_dbglog_event(AR_SOFTC_DEV_T *arPriv, A_UINT32 dropped,
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
        ar6000_send_event_to_app(arPriv, WMIX_DBGLOG_EVENTID, (A_UINT8*)&buffer[sent], send);
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

void
ar6000_parse_dev_mode(A_CHAR *mode)
{
    A_UINT8 i, match = FALSE, mode_len;
    A_UINT8 val_mode, val_submode;
    A_UINT8 num_submode;

    char *valid_modes[] = { "sta",
                            "ap",
                            "ibss",
                            "bt30amp",
                            "sta,ap",
                            "ap,sta",
                            "ap,ap",
                            "sta,sta",
                            "sta,bt30amp",
                            "sta,ap,ap"
                    };
    char *valid_submodes[] = { "none",
                               "p2pdev",
                               /*"p2pclient",*/ //persistent p2p support
                               /*"p2pgo", */ // persistent p2p support
                               "none,none",
                               "none,none,none",
                               "none,p2pdev",
                               "p2pdev,none",
                               /*"none,p2pclient",*/ //persistent p2p support
                               /*"none,p2pgo"*/ // persistent p2p support
                    };

    A_CHAR *dev_mode;
    A_CHAR *str;
    A_UINT32 host_int = 0;

    dev_mode = mode;
    str      = mode;
    num_device = 0;
    fwmode     = 0;

    mode_len = strlen(dev_mode);
    for (i=0; i <= 9; i++) {
        if ((mode_len == strlen(valid_modes[i])) && (strcmp(dev_mode,valid_modes[i]))==0) {
            match = TRUE;
            break;
        }
    }

    if(!match) {
        num_device = fwmode = 1;
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ERROR: Wrong mode. using default (single device STA mode).\n"));
        return;
    }

    do
    {
        str++;
        if(*str == ',' || *str == '\0') {
            num_device++;
            if(strncmp(dev_mode,"ap",2) == 0) {
                host_int =  HI_OPTION_FW_MODE_AP;
            }
            else if(strncmp(dev_mode,"sta",3) == 0) {
                host_int = HI_OPTION_FW_MODE_BSS_STA;
            }
            else if(strncmp(dev_mode,"ibss",4) == 0 ) {
                host_int = HI_OPTION_FW_MODE_IBSS;
            } else if(strncmp(dev_mode,"bt30amp",7) == 0) {
                host_int = HI_OPTION_FW_MODE_BT30AMP;
            }

            fwmode |= (host_int << ((num_device -1) * HI_OPTION_FW_MODE_BITS));
            dev_mode = ++str;
       }
    }while(*dev_mode != '\0');

    /* Validate submode if present */
    if (!submode[0]) {
       /* default "none" submode for all devices */
       fwsubmode = 0;
       return;
    }

    dev_mode = submode;
    str      = submode;
    num_submode = 0;
    fwsubmode = 0;
    match = FALSE;

    mode_len = strlen(dev_mode);
    for (i=0; i<6; i++) {
        if ((mode_len == strlen(valid_submodes[i])) && (strcmp(dev_mode,valid_submodes[i]))==0) {
            match = TRUE;
            break;
        }
    }

    if (!match) {
        fwsubmode = 0;
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ERROR: Wrong submode. using default (none for all devs).\n"));
        return;
    }

    do
    {
        str++;
        if(*str == ',' || *str == '\0') {
            num_submode++;
            if(strncmp(dev_mode,"none",4) == 0) {
                host_int = HI_OPTION_FW_SUBMODE_NONE;
            }
            else if(strncmp(dev_mode,"p2pdev",6) == 0) {
                host_int = HI_OPTION_FW_SUBMODE_P2PDEV;
            }
            else if(strncmp(dev_mode,"p2pclient",9) == 0 ) {
                host_int = HI_OPTION_FW_SUBMODE_P2PCLIENT;
            } else if(strncmp(dev_mode,"p2pgo",5) == 0 ) {
                host_int = HI_OPTION_FW_SUBMODE_P2PGO;
            }
            fwsubmode |= (host_int << ((num_submode -1) * HI_OPTION_FW_SUBMODE_BITS));
            dev_mode = ++str;
       }
    }while(*dev_mode != '\0');

    /* Validate if the subopmode is specified for all the devs.
     */
    if (num_device != num_submode) {
        /* default to "none" submode for all devices */
        fwsubmode = 0;
        return;
    }

    /* Validate if the submode specified is appropriate for the device modes
     * specified for each device. The following is the validation recipe.
     * fwmode        fwsubmode
     * -----------------------
     * IBSS          none
     * STA           none,p2pdev,p2pclient
     * AP            none,p2pgo
     */
    for (i=0; i<num_device; i++) {
        val_mode = (fwmode >> (i * HI_OPTION_FW_MODE_BITS)) &
                                              HI_OPTION_FW_MODE_MASK;
        val_submode = (fwsubmode >> (i * HI_OPTION_FW_SUBMODE_BITS)) &
                                              HI_OPTION_FW_SUBMODE_MASK;
        switch (val_mode) {
        case HI_OPTION_FW_MODE_IBSS:
            if (val_submode != HI_OPTION_FW_SUBMODE_NONE) {
                /* set submode to none */
                fwsubmode &= ~(HI_OPTION_FW_SUBMODE_MASK << (i*HI_OPTION_FW_SUBMODE_BITS));
                fwsubmode |= (HI_OPTION_FW_SUBMODE_NONE << (i * HI_OPTION_FW_SUBMODE_BITS));
            }
        break;

        case HI_OPTION_FW_MODE_BSS_STA:
            if (val_submode == HI_OPTION_FW_SUBMODE_P2PGO) {
                /* set submode to none */
                fwsubmode &= ~(HI_OPTION_FW_SUBMODE_MASK << (i*HI_OPTION_FW_SUBMODE_BITS));
                fwsubmode |= (HI_OPTION_FW_SUBMODE_NONE << (i * HI_OPTION_FW_SUBMODE_BITS));
            }

        break;
        case HI_OPTION_FW_MODE_AP:
            if (val_submode == HI_OPTION_FW_SUBMODE_P2PDEV ||
                  val_submode == HI_OPTION_FW_SUBMODE_P2PCLIENT) {
                /* set submode to none */
                fwsubmode &= ~(HI_OPTION_FW_SUBMODE_MASK << (i*HI_OPTION_FW_SUBMODE_BITS));
                fwsubmode |= (HI_OPTION_FW_SUBMODE_NONE << (i * HI_OPTION_FW_SUBMODE_BITS));
            }
        break;

        default:
        break;
        }
    }

    return;
}

#ifdef RK29
static int
#else
static int __init
#endif
ar6000_init_module(void)
{
#ifndef RK29
    static int probed = 0;
#endif
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
    ar6k_init = FALSE;
    A_MEMZERO(&osdrvCallbacks,sizeof(osdrvCallbacks));
    osdrvCallbacks.deviceInsertedHandler = ar6000_avail_ev;
    osdrvCallbacks.deviceRemovedHandler = ar6000_unavail_ev;

#ifdef CONFIG_PM
    osdrvCallbacks.deviceSuspendHandler = ar6000_suspend_ev;
    osdrvCallbacks.deviceResumeHandler = ar6000_resume_ev;
    osdrvCallbacks.devicePowerChangeHandler = ar6000_power_change_ev;
#endif

#ifndef RK29
    ar6000_pm_init();
#endif

    if(devmode[0])
        ar6000_parse_dev_mode(devmode);

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

#ifndef RK29
    if (probed) {
        return -ENODEV;
    }
    probed++;
#endif

#ifdef CONFIG_HOST_GPIO_SUPPORT
    ar6000_gpio_init();
#endif /* CONFIG_HOST_GPIO_SUPPORT */

    status = HIFInit(&osdrvCallbacks);
    if(status != A_OK)
        return -ENODEV;

    return 0;
}

#ifdef RK29
static void
#else
static void __exit
#endif
ar6000_cleanup_module(void)
{
    int i = 0;
    struct net_device *ar6000_netdev;
    AR_SOFTC_T *ar;
    AR_SOFTC_DEV_T *arPriv = NULL;

    if(ar6000_devices[0] != NULL) {
        arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(ar6000_devices[0]);
        ar = arPriv->arSoftc;
        ar6000_cleanup(ar);
    }
    for (i=0; i < num_device; i++) {
#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
    /* Delete the Adaptive Power Control timer */
    if (timer_pending(&aptcTimer[i])) {
        del_timer_sync(&aptcTimer[i]);
    }
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */
        if (ar6000_devices[i] != NULL) {
            ar6000_netdev = ar6000_devices[i];
            ar6000_devices[i] = NULL;
            ar6000_destroy(ar6000_netdev, 1);
            if (arPriv)
                A_UNTIMEOUT(&arPriv->arSta.disconnect_timer);
        }
    }

    HIFShutDownDevice(NULL);

    a_module_debug_support_cleanup();

#ifndef RK29
    ar6000_pm_exit();
#endif

#ifdef ANDROID_ENV
    android_module_exit();
#endif
    a_meminfo_report(TRUE);
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
    APTC_TRAFFIC_RECORD *aptcTR;
    A_UNIT8  i;

    ar = (AR_SOFTC_T *)arg;
    A_ASSERT(ar != NULL);

    for(i = 0; i < num_device; i++) {
        aptcTR = ar->arDev[i].aptcTR;
        A_ASSERT(!timer_pending(&aptcTimer[i]));
        AR6000_SPIN_LOCK(&ar->arLock, 0);

        /* Get the number of bytes transferred */
        numbytes = aptcTR->bytesTransmitted + aptcTR->bytesReceived;
        aptcTR->bytesTransmitted = aptcTR->bytesReceived = 0;

        /* Calculate and decide based on throughput thresholds */
        throughput = ((numbytes * 8)/APTC_TRAFFIC_SAMPLING_INTERVAL); /* Kbps */
        if (throughput < APTC_LOWER_THROUGHPUT_THRESHOLD) {
            /* Enable Sleep and delete the timer */
            A_ASSERT(ar->arWmiReady == TRUE);
            AR6000_SPIN_UNLOCK(&ar->arLock, 0);
            status = wmi_powermode_cmd(ar->arWmi, REC_POWER);
            AR6000_SPIN_LOCK(&ar->arLock, 0);
            A_ASSERT(status == A_OK);
            aptcTR->timerScheduled = FALSE;
        } else {
            A_TIMEOUT_MS(&aptcTimer[i], APTC_TRAFFIC_SAMPLING_INTERVAL, 0);
        }

        AR6000_SPIN_UNLOCK(&ar->arLock, 0);
    }
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
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s(), allocation of netbuf failed", __func__));
    }
}
#endif

static struct bin_attribute bmi_attr = {
    .attr = {.name = "bmi", .mode = 0600},
    .read = ar6000_sysfs_bmi_read,
    .write = ar6000_sysfs_bmi_write,
};

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
static ssize_t
ar6000_sysfs_bmi_read(struct kobject *kobj, struct bin_attribute *bin_attr,
                      char *buf, loff_t pos, size_t count)
#else
static ssize_t
ar6000_sysfs_bmi_read(struct file *fp, struct kobject *kobj,
                      struct bin_attribute *bin_attr,
                      char *buf, loff_t pos, size_t count)
#endif
{
    int index;
    AR_SOFTC_DEV_T *arPriv;
    AR_SOFTC_T     *ar = NULL;
    HIF_DEVICE_OS_DEVICE_INFO   *osDevInfo;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("BMI: Read %d bytes\n", count));
    for (index=0; index < num_device; index++) {
        arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(ar6000_devices[index]);
        ar = arPriv->arSoftc;
        osDevInfo = &ar->osDevInfo;
        if (kobj == (&(((struct device *)osDevInfo->pOSDevice)->kobj))) {
            break;
        }
    }

    if (ar == NULL) return 0;
    if (index == num_device) return 0;

    if ((BMIRawRead(ar->arHifDevice, (A_UCHAR*)buf, count, TRUE)) != A_OK) {
        return 0;
    }

    return count;
}

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,35)
static ssize_t
ar6000_sysfs_bmi_write(struct kobject *kobj, struct bin_attribute *bin_attr,
                       char *buf, loff_t pos, size_t count)
#else
static ssize_t
ar6000_sysfs_bmi_write(struct file *fp, struct kobject *kobj,
                       struct bin_attribute *bin_attr,
                       char *buf, loff_t pos, size_t count)
#endif
{
    int index;
    AR_SOFTC_DEV_T *arPriv;
    AR_SOFTC_T     *ar = NULL;
    HIF_DEVICE_OS_DEVICE_INFO   *osDevInfo;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("BMI: Write %d bytes\n", count));
    for (index=0; index < num_device; index++) {
        arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(ar6000_devices[index]);
        ar = arPriv->arSoftc;
        osDevInfo = &ar->osDevInfo;
        if (kobj == (&(((struct device *)osDevInfo->pOSDevice)->kobj))) {
            break;
        }
    }

    if (ar == NULL) return 0;
    if (index == num_device) return 0;

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
    if (ar->osDevInfo.pOSDevice) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("BMI: Deleting sysfs entry\n"));
        sysfs_remove_bin_file(&(((struct device *)ar->osDevInfo.pOSDevice)->kobj), &bmi_attr);
        ar->osDevInfo.pOSDevice = NULL;
    }
}

#define bmifn(fn) do { \
    if ((fn) < A_OK) { \
        A_PRINTF("BMI operation failed: %d\n", __LINE__); \
        return A_ERROR; \
    } \
} while(0)

#ifdef INIT_MODE_DRV_ENABLED

#define MCKINLEY_MAC_ADDRESS_OFFSET   0x16
static
void calculate_crc(A_UINT32 TargetType, A_UCHAR *eeprom_data, size_t eeprom_size)
{
    A_UINT16        *ptr_crc;
    A_UINT16        *ptr16_eeprom;
    A_UINT16        checksum;
    A_UINT32        i;

    if (TargetType == TARGET_TYPE_AR6001)
    {
        ptr_crc = (A_UINT16 *)eeprom_data;
    }
    else if (TargetType == TARGET_TYPE_AR6003)
    {
        ptr_crc = (A_UINT16 *)((A_UCHAR *)eeprom_data + 0x04);
    }
    else if (TargetType == TARGET_TYPE_MCKINLEY)
    {
        eeprom_size = 1024;
        ptr_crc = (A_UINT16 *)((A_UCHAR *)eeprom_data + 0x04);
    }
    else
    {
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

#ifdef SOFTMAC_FILE_USED
#define AR6002_MAC_ADDRESS_OFFSET     0x0A
#define AR6003_MAC_ADDRESS_OFFSET     0x16
static void
ar6000_softmac_update(AR_SOFTC_T *ar, A_UCHAR *eeprom_data, size_t eeprom_size)
{
    /* We need to store the MAC, which comes either from the softmac file or is
     * randomly generated, because we do not want to load a new MAC address
     * if the chip goes into suspend and then is resumed later on.  We ONLY
     * want to load a new MAC  if the driver is unloaded and then reloaded
     */
    static A_UCHAR random_mac[6];
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
    case TARGET_TYPE_MCKINLEY:
        ptr_mac = (A_UINT8 *)((A_UCHAR *)eeprom_data + MCKINLEY_MAC_ADDRESS_OFFSET);
        break;
    default:
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Invalid Target Type \n"));
        return;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_WARN,
                ("MAC from EEPROM %02X:%02X:%02X:%02X:%02X:%02X\n",
                ptr_mac[0], ptr_mac[1], ptr_mac[2],
                ptr_mac[3], ptr_mac[4], ptr_mac[5]));

    if (memcmp(random_mac, "\0\0\0\0\0\0", 6)!=0) {
        memcpy(ptr_mac, random_mac, 6);
    } else {
        /* create a random MAC in case we cannot read file from system */
        ptr_mac[0] = random_mac[0] = 2; /* locally administered */
        ptr_mac[1] = random_mac[1] = 0x03;
        ptr_mac[2] = random_mac[2] = 0x7F;
        ptr_mac[3] = random_mac[3] = random32() & 0xff;
        ptr_mac[4] = random_mac[4] = random32() & 0xff;
        ptr_mac[5] = random_mac[5] = random32() & 0xff;
    }

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

#ifdef TCHIP
		if(IS_MAC_NULL(softmac)) {
			softmac[0]= 0x20;
			softmac[1]= 0x59;
			softmac[2]= 0xa0;
			softmac[3]= (random32() & 0x0f) + 0x30;
			softmac[4]= random32() & 0xff;
			softmac[5]= random32() & 0xff;
					
			//write to the file.
			sprintf(macbuf,
				"%02x:%02x:%02x:%02x:%02x:%02x",
				softmac[0], softmac[1], softmac[2],
				softmac[3], softmac[4], softmac[5]);
			{
			extern void android_wifi_softmac_update(char *mac_buff, int len);
			android_wifi_softmac_update(macbuf, softmac_entry->size);
			}		
			AR_DEBUG_PRINTF(ATH_DEBUG_ERR, 
					("MAC random %02X:%02X:%02X:%02X:%02X:%02X\n", 
					 softmac[0], softmac[1], softmac[2],
					 softmac[3], softmac[4], softmac[5]));

		}
#endif

                for (i=0; i<6; ++i) {
                    ptr_mac[i] = softmac[i] & 0xff;
                }
                source = "softmac file";
                A_MEMZERO(random_mac, sizeof(random_mac));
            }
            A_FREE(macbuf);
        }
        A_RELEASE_FIRMWARE(softmac_entry);
    }

    if (memcmp(random_mac, "\0\0\0\0\0\0", 6)!=0) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Warning! Random MAC address is just for testing purpose\n"));
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_WARN,
                    ("MAC from %s %02X:%02X:%02X:%02X:%02X:%02X\n",  source,
                     ptr_mac[0], ptr_mac[1], ptr_mac[2],
                     ptr_mac[3], ptr_mac[4], ptr_mac[5]));
    calculate_crc(ar->arTargetType, eeprom_data, eeprom_size);
}
#endif /* SOFTMAC_FILE_USED */

static void
ar6000_reg_update(AR_SOFTC_T *ar, A_UCHAR *eeprom_data, size_t eeprom_size, int regCode)
{
    A_UCHAR *ptr_reg;
    switch (ar->arTargetType) {
    case TARGET_TYPE_AR6002:
        ptr_reg = (A_UINT8 *)((A_UCHAR *)eeprom_data + 8);
        break;
    case TARGET_TYPE_AR6003:
        ptr_reg = (A_UINT8 *)((A_UCHAR *)eeprom_data + 12);
        break;
    default:
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Invalid Target Type \n"));
        return;
    }

    ptr_reg[0] = (A_UCHAR)(regCode&0xFF);
    ptr_reg[1] = (A_UCHAR)((regCode>>8)&0xFF);
    calculate_crc(ar->arTargetType, eeprom_data, eeprom_size);
}

static A_STATUS
ar6000_transfer_bin_file(AR_SOFTC_T *ar, AR6K_BIN_FILE file, A_UINT32 address, A_BOOL compressed)
{
    A_STATUS status;
    const char *filename;
    const struct firmware *fw_entry;
    A_UINT32 fw_entry_size;
    A_UCHAR *tempEeprom;
    A_UINT32 board_data_size;

    switch (file) {
        case AR6K_OTP_FILE:
            if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                filename = AR6003_REV2_OTP_FILE;
            } else if (ar->arVersion.target_ver == AR6003_REV3_VERSION) {
                filename = AR6003_REV3_OTP_FILE;
            } else {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown firmware revision: %d\n", ar->arVersion.target_ver));
                return A_ERROR;
            }
            break;

        case AR6K_FIRMWARE_FILE:
            if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                filename = AR6003_REV2_FIRMWARE_FILE;
            } else if (ar->arVersion.target_ver == AR6003_REV3_VERSION) {
		if(ar->arVersion.targetconf_ver == AR6003_SUBVER_ROUTER)
                        filename = AR6003_REV3_ROUTER_FIRMWARE_FILE;
                else if (ar->arVersion.targetconf_ver == AR6003_SUBVER_MOBILE)
                        filename = AR6003_REV3_MOBILE_FIRMWARE_FILE;
                else if (ar->arVersion.targetconf_ver == AR6003_SUBVER_TABLET)
                        filename = AR6003_REV3_TABLET_FIRMWARE_FILE;
                else
                        filename = AR6003_REV3_DEFAULT_FIRMWARE_FILE;
            } else {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown firmware revision: %d\n", ar->arVersion.target_ver));
                return A_ERROR;
            }

            if (eppingtest) {
                bypasswmi = TRUE;
                if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                    filename = AR6003_REV2_EPPING_FIRMWARE_FILE;
                } else if (ar->arVersion.target_ver == AR6003_REV3_VERSION) {
                    filename = AR6003_REV3_EPPING_FIRMWARE_FILE;
                } else {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("eppingtest : unsupported firmware revision: %d\n",
                        ar->arVersion.target_ver));
                    return A_ERROR;
                }
                compressed = 0;
            }

#ifdef CONFIG_HOST_TCMD_SUPPORT
            if(testmode == 1) {
                if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                    filename = AR6003_REV2_TCMD_FIRMWARE_FILE;
                } else if (ar->arVersion.target_ver == AR6003_REV3_VERSION) {
                    filename = AR6003_REV3_TCMD_FIRMWARE_FILE;
                } else {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown firmware revision: %d\n", ar->arVersion.target_ver));
                    return A_ERROR;
                }
                compressed = 0;
            }
#endif
#ifdef HTC_RAW_INTERFACE
            if (!eppingtest && bypasswmi) {
                if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                    filename = AR6003_REV2_ART_FIRMWARE_FILE;
                } else if (ar->arVersion.target_ver == AR6003_REV3_VERSION) {
                    filename = AR6003_REV3_ART_FIRMWARE_FILE;
                } else {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown firmware revision: %d\n", ar->arVersion.target_ver));
                    return A_ERROR;
                }
                compressed = 0;
            }
#endif
            break;

        case AR6K_PATCH_FILE:
            if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                filename = AR6003_REV2_PATCH_FILE;
            } else if (ar->arVersion.target_ver == AR6003_REV3_VERSION) {
                filename = AR6003_REV3_PATCH_FILE;
            } else {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Unknown firmware revision: %d\n", ar->arVersion.target_ver));
                return A_ERROR;
            }
            break;

        case AR6K_BOARD_DATA_FILE:
            if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                filename = AR6003_REV2_BOARD_DATA_FILE;
            } else if (ar->arVersion.target_ver == AR6003_REV3_VERSION) {
                filename = AR6003_REV3_BOARD_DATA_FILE;
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

    fw_entry_size = fw_entry->size;
    tempEeprom = NULL;

    /* Load extended board data for AR6003 */
    if ((file==AR6K_BOARD_DATA_FILE) && (fw_entry->data)) {
        A_UINT32 board_ext_address;
        A_INT32 board_ext_data_size;

        tempEeprom = A_MALLOC_NOWAIT(fw_entry->size);
        if (!tempEeprom) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Memory allocation failed\n"));
            A_RELEASE_FIRMWARE(fw_entry);
            return A_ERROR;
        }

        board_data_size = (((ar)->arTargetType == TARGET_TYPE_AR6002) ? AR6002_BOARD_DATA_SZ : \
                          (((ar)->arTargetType == TARGET_TYPE_AR6003) ? AR6003_BOARD_DATA_SZ : 0));

        board_ext_data_size = 0;
        if (ar->arTargetType == TARGET_TYPE_AR6002) {
            board_ext_data_size = AR6002_BOARD_EXT_DATA_SZ;
        } else if (ar->arTargetType == TARGET_TYPE_AR6003) {
            if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
                board_ext_data_size = AR6003_VER2_BOARD_EXT_DATA_SZ;
            } else {
                board_ext_data_size = AR6003_BOARD_EXT_DATA_SZ;
            }
        }

        /* AR6003 2.1.1 support 1792 bytes and 2048 bytes board file */
        if ((board_ext_data_size) &&
            (fw_entry->size < (board_data_size + board_ext_data_size)))
        {
            board_ext_data_size =  fw_entry->size - board_data_size;
            if (board_ext_data_size < 0) {
                board_ext_data_size = 0;
            }
        }

        A_MEMCPY(tempEeprom, (A_UCHAR *)fw_entry->data, fw_entry->size);

#ifdef SOFTMAC_FILE_USED
        ar6000_softmac_update(ar, tempEeprom, board_data_size);
#endif
        if (regcode!=0) {
            ar6000_reg_update(ar, tempEeprom, board_data_size, regcode);
        }

        /* Determine where in Target RAM to write Board Data */
        bmifn(BMIReadMemory(ar->arHifDevice, HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_board_ext_data), (A_UCHAR *)&board_ext_address, 4));
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("Board extended Data download address: 0x%x\n", board_ext_address));

        /* check whether the target has allocated memory for extended board data and file contains extended board data */
        if ((board_ext_address) && (fw_entry->size == (board_data_size + board_ext_data_size))) {
            A_UINT32 param;

            status = BMIWriteMemory(ar->arHifDevice, board_ext_address, (A_UCHAR *)(((A_UINT32)tempEeprom) + board_data_size), board_ext_data_size);

            if (status != A_OK) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("BMI operation failed: %d\n", __LINE__));
                A_RELEASE_FIRMWARE(fw_entry);
                return A_ERROR;
            }

            /* Record the fact that extended board Data IS initialized */
            param = (board_ext_data_size << 16) | 1;
            bmifn(BMIWriteMemory(ar->arHifDevice, HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_board_ext_data_config), (A_UCHAR *)&param, 4));
        }
        fw_entry_size = board_data_size;
    }

    if (compressed) {
        status = BMIFastDownload(ar->arHifDevice, address, (A_UCHAR *)fw_entry->data, fw_entry_size);
    } else {
        if (file==AR6K_BOARD_DATA_FILE && fw_entry->data)
        {
            status = BMIWriteMemory(ar->arHifDevice, address, (A_UCHAR *)tempEeprom, fw_entry_size);
        }
        else
        {
        status = BMIWriteMemory(ar->arHifDevice, address, (A_UCHAR *)fw_entry->data, fw_entry_size);
    }
    }

    if (tempEeprom) {
        A_FREE(tempEeprom);
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
           	HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_board_data), (A_UCHAR *)&address, 4) != A_OK)
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
#if defined(INIT_MODE_DRV_ENABLED) && defined(CONFIG_HOST_TCMD_SUPPORT)
    const char *filename;
    const struct firmware *fw_entry;
#endif

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
        address = MBOX_BASE_ADDRESS + LOCAL_SCRATCH_OFFSET;
        bmifn(BMIReadSOCRegister(ar->arHifDevice, address, &param));
        options = param;
        param |= AR6K_OPTION_SLEEP_DISABLE;
        bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

        address = RTC_WMAC_BASE_ADDRESS + WLAN_SYSTEM_SLEEP_OFFSET;
        bmifn(BMIReadSOCRegister(ar->arHifDevice, address, &param));
        sleep = param;
        param |= WLAN_SYSTEM_SLEEP_DISABLE_SET(1);
        bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("old options: %d, old sleep: %d\n", options, sleep));

        if (ar->arTargetType == TARGET_TYPE_MCKINLEY) {
            /* Run at 40/44MHz by default */
            param = CPU_CLOCK_STANDARD_SET(0);
        } else if (ar->arTargetType == TARGET_TYPE_AR6003) {
            /* Program analog PLL register */
            bmifn(BMIWriteSOCRegister(ar->arHifDevice, ANALOG_INTF_BASE_ADDRESS + 0x284, 0xF9104001));
            /* Run at 80/88MHz by default */
            param = CPU_CLOCK_STANDARD_SET(1);
        } else {
            /* Run at 40/44MHz by default */
            param = CPU_CLOCK_STANDARD_SET(0);
        }
        address = RTC_SOC_BASE_ADDRESS + CPU_CLOCK_OFFSET;
        bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

        param = 0;
        if (ar->arTargetType == TARGET_TYPE_AR6002) {
            bmifn(BMIReadMemory(ar->arHifDevice,
                                HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_ext_clk_detected),
                                (A_UCHAR *)&param, 4));
        }

        /* LPO_CAL.ENABLE = 1 if no external clk is detected */
        if (param != 1) {
            address = RTC_SOC_BASE_ADDRESS + LPO_CAL_OFFSET;
            param = LPO_CAL_ENABLE_SET(1);
            bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));
        }

        /* Venus2.0: Lower SDIO pad drive strength */
        if ((ar->arVersion.target_ver == AR6003_REV2_VERSION) ||
            (ar->arVersion.target_ver == AR6003_REV3_VERSION))
        {
            param = 0x20;
            address = GPIO_BASE_ADDRESS + GPIO_PIN10_OFFSET;
            bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

            address = GPIO_BASE_ADDRESS + GPIO_PIN11_OFFSET;
            bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

            address = GPIO_BASE_ADDRESS + GPIO_PIN12_OFFSET;
            bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

            address = GPIO_BASE_ADDRESS + GPIO_PIN13_OFFSET;
            bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));
        }

#ifdef FORCE_INTERNAL_CLOCK
        /* Ignore external clock, if any, and force use of internal clock */
        if (ar->arTargetType == TARGET_TYPE_AR6003 || ar->arTargetType == TARGET_TYPE_MCKINLEY) {
            /* hi_ext_clk_detected = 0 */
            param = 0;
            bmifn(BMIWriteMemory(ar->arHifDevice,
                                 HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_ext_clk_detected),
                                 (A_UCHAR *)&param, 4));

            /* CLOCK_CONTROL &= ~LF_CLK32 */
            address = RTC_BASE_ADDRESS + CLOCK_CONTROL_ADDRESS;
            bmifn(BMIReadSOCRegister(ar->arHifDevice, address, &param));
            param &= (~CLOCK_CONTROL_LF_CLK32_SET(1));
            bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));
        }
#endif /* FORCE_INTERNAL_CLOCK */

        /* Transfer Board Data from Target EEPROM to Target RAM */
        if (ar->arTargetType == TARGET_TYPE_AR6003 || ar->arTargetType == TARGET_TYPE_MCKINLEY) {
            /* Determine where in Target RAM to write Board Data */
            bmifn(BMIReadMemory(ar->arHifDevice,
                                HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_board_data),
                                (A_UCHAR *)&address, 4));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("Board Data download address: 0x%x\n", address));

            /* Write EEPROM data to Target RAM */
            if ((status=ar6000_transfer_bin_file(ar, AR6K_BOARD_DATA_FILE, address, FALSE)) != A_OK) {
                return A_ERROR;
            }

            /* Record the fact that Board Data IS initialized */
            param = 1;
            bmifn(BMIWriteMemory(ar->arHifDevice,
                                 HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_board_data_initialized),
                                 (A_UCHAR *)&param, 4));

            /* Transfer One time Programmable data */
            AR6K_APP_LOAD_ADDRESS(address, ar->arVersion.target_ver);
            if (ar->arVersion.target_ver == AR6003_REV3_VERSION) {
                address = BMI_SEGMENTED_WRITE_ADDR;
            }
            status = ar6000_transfer_bin_file(ar, AR6K_OTP_FILE, address, TRUE);
            if (status == A_OK) {
                /* Execute the OTP code */
#ifdef SOFTMAC_FILE_USED
                param = 1;
#else
                param = 0;
#endif
                if (regcode != 0)
                    param |= 0x2;
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
        AR6K_APP_LOAD_ADDRESS(address, ar->arVersion.target_ver);
        if (ar->arVersion.target_ver == AR6003_REV3_VERSION) {
            address = BMI_SEGMENTED_WRITE_ADDR;
        }
        if ((ar6000_transfer_bin_file(ar, AR6K_FIRMWARE_FILE, address, TRUE)) != A_OK) {
            return A_ERROR;
        }

        if (ar->arVersion.target_ver == AR6003_REV2_VERSION)
        {
            /* Set starting address for firmware */
            AR6K_APP_START_OVERRIDE_ADDRESS(address, ar->arVersion.target_ver);
            bmifn(BMISetAppStart(ar->arHifDevice, address));
        }

        /* Apply the patches */
        if (ar->arTargetType == TARGET_TYPE_AR6003) {
            AR6K_DATASET_PATCH_ADDRESS(address, ar->arVersion.target_ver);
            if ((ar6000_transfer_bin_file(ar, AR6K_PATCH_FILE, address, FALSE)) != A_OK) {
                return A_ERROR;
            }
            param = address;
            bmifn(BMIWriteMemory(ar->arHifDevice,
                             HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_dset_list_head),
                             (A_UCHAR *)&param, 4));
        }

        /* Restore system sleep */
        address = RTC_WMAC_BASE_ADDRESS + WLAN_SYSTEM_SLEEP_OFFSET;
        bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, sleep));

        address = MBOX_BASE_ADDRESS + LOCAL_SCRATCH_OFFSET;
        param = options | 0x20;
        bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

        if (ar->arTargetType == TARGET_TYPE_AR6003 || ar->arTargetType == TARGET_TYPE_MCKINLEY) {
            /* Configure GPIO AR6003 UART */
#ifndef CONFIG_AR600x_DEBUG_UART_TX_PIN
#define CONFIG_AR600x_DEBUG_UART_TX_PIN 8
#endif
            param = CONFIG_AR600x_DEBUG_UART_TX_PIN;
            bmifn(BMIWriteMemory(ar->arHifDevice,
                                 HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_dbg_uart_txpin),
                                 (A_UCHAR *)&param, 4));

#if (CONFIG_AR600x_DEBUG_UART_TX_PIN == 23)
            if (ATH_REGISTER_SUPPORTED_BY_TARGET(CLOCK_GPIO_OFFSET)) {
                address = GPIO_BASE_ADDRESS + CLOCK_GPIO_OFFSET;
                bmifn(BMIReadSOCRegister(ar->arHifDevice, address, &param));
                param |= CLOCK_GPIO_BT_CLK_OUT_EN_SET(1);
                bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));
            } else {
                /* AR6004 has no need for a CLOCK_GPIO register */
            }
#endif

            /* Configure GPIO for BT Reset */
#ifdef ATH6KL_CONFIG_GPIO_BT_RESET
#define CONFIG_AR600x_BT_RESET_PIN  0x16
            param = CONFIG_AR600x_BT_RESET_PIN;
            bmifn(BMIWriteMemory(ar->arHifDevice,
                                 HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_hci_uart_support_pins),
                                 (A_UCHAR *)&param, 4));
#endif /* ATH6KL_CONFIG_GPIO_BT_RESET */

            /* Configure UART flow control polarity */
#ifndef CONFIG_ATH6KL_BT_UART_FC_POLARITY
#define CONFIG_ATH6KL_BT_UART_FC_POLARITY 0
#endif

#if (CONFIG_ATH6KL_BT_UART_FC_POLARITY == 1)
            if ((ar->arVersion.target_ver == AR6003_REV2_VERSION) ||
                (ar->arVersion.target_ver == AR6003_REV3_VERSION))
            {
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
            param = 1;
            status = BMIWriteMemory(ar->arHifDevice,
                                            HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_board_data_initialized),
                                           (A_UCHAR *)&param, 4);
        }
#endif /* HTC_RAW_INTERFACE */

#ifdef CONFIG_HOST_TCMD_SUPPORT
        if (testmode == 2) {
			if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
 				filename = AR6003_REV2_UTF_FIRMWARE_FILE;
                if ((A_REQUEST_FIRMWARE(&fw_entry, filename, ((struct device *)ar->osDevInfo.pOSDevice))) != 0)
                {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Failed to get %s\n", filename));
                    return A_ENOENT;
                }
		        /* Download Target firmware */
				AR6K_APP_LOAD_ADDRESS(address, ar->arVersion.target_ver);
                status = BMIWriteMemory(ar->arHifDevice, address, (A_UCHAR *)fw_entry->data, fw_entry->size);

                address = HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_end_RAM_reserve_sz);
                param = 11008;
                bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

                address = 0x57D884;
				filename = AR6003_REV2_TESTSCRIPT_FILE;
                if ((A_REQUEST_FIRMWARE(&fw_entry, filename, ((struct device *)ar->osDevInfo.pOSDevice))) != 0)
                {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Failed to get %s\n", filename));
                    return A_ENOENT;
                }
                status = BMIWriteMemory(ar->arHifDevice, address, (A_UCHAR *)fw_entry->data, fw_entry->size);

                param = 0x57D884;
                address = HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_ota_testscript);
                bmifn(BMIWriteMemory(ar->arHifDevice, address, (A_UCHAR *)&param, 4));

                address = HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_test_apps_related);
                bmifn(BMIReadSOCRegister(ar->arHifDevice, address, &param));
                param |= 1;
                bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

				A_RELEASE_FIRMWARE(fw_entry);
            }
	    else if (ar->arVersion.target_ver == AR6003_REV3_VERSION) {
                filename = AR6003_REV3_UTF_FIRMWARE_FILE;
                if ((A_REQUEST_FIRMWARE(&fw_entry, filename, ((struct device *)ar->osDevInfo.pOSDevice))) != 0)
                {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Failed to get %s\n", filename));
                    return A_ENOENT;
                }
                        /* Download Target firmware */
				address = BMI_SEGMENTED_WRITE_ADDR;
                status = BMIWriteMemory(ar->arHifDevice, address, (A_UCHAR *)fw_entry->data, fw_entry->size);

                address = HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_end_RAM_reserve_sz);
                param = 4096;
                bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

                address = 0x57EF74;
                filename = AR6003_REV3_TESTSCRIPT_FILE;
                if ((A_REQUEST_FIRMWARE(&fw_entry, filename, ((struct device *)ar->osDevInfo.pOSDevice))) != 0)
                {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Failed to get %s\n", filename));
                    return A_ENOENT;
                }
                status = BMIWriteMemory(ar->arHifDevice, address, (A_UCHAR *)fw_entry->data, fw_entry->size);

                param = 0x57EF74;
                address = HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_ota_testscript);
                bmifn(BMIWriteMemory(ar->arHifDevice, address, (A_UCHAR *)&param, 4));

                address = HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_test_apps_related);
                bmifn(BMIReadSOCRegister(ar->arHifDevice, address, &param));
                param |= 1;
                bmifn(BMIWriteSOCRegister(ar->arHifDevice, address, param));

                A_RELEASE_FIRMWARE(fw_entry);
		    }
        }
#endif
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
                           HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_serial_enable),
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
                       HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_app_host_interest),
                       (A_UCHAR *)&param,
                       4)!= A_OK)
    {
         AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIWriteMemory for htc version failed \n"));
         return A_ERROR;
    }

    if (enabletimerwar) {
        A_UINT32 param;

        if (BMIReadMemory(ar->arHifDevice,
            HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_option_flag),
            (A_UCHAR *)&param,
            4)!= A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIReadMemory for enabletimerwar failed \n"));
            return A_ERROR;
        }

        param |= HI_OPTION_TIMER_WAR;

        if (BMIWriteMemory(ar->arHifDevice,
            HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_option_flag),
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
            HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_option_flag),
            (A_UCHAR *)&param,
            4)!= A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIReadMemory for setting fwmode failed \n"));
            return A_ERROR;
        }

        param |= (num_device << HI_OPTION_NUM_DEV_SHIFT);
        param |= (fwmode << HI_OPTION_FW_MODE_SHIFT);
        param |= (mac_addr_method << HI_OPTION_MAC_ADDR_METHOD_SHIFT);
        param |= (firmware_bridge << HI_OPTION_FW_BRIDGE_SHIFT);
        param |= (fwsubmode << HI_OPTION_FW_SUBMODE_SHIFT);

        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("NUM_DEV=%d FWMODE=0x%x FWSUBMODE=0x%x FWBR_BUF %d\n",
                            num_device, fwmode, fwsubmode, firmware_bridge));

        if (BMIWriteMemory(ar->arHifDevice,
            HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_option_flag),
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
            HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_option_flag),
            (A_UCHAR *)&param,
            4)!= A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIReadMemory for disabling debug logs failed\n"));
            return A_ERROR;
        }

        param |= HI_OPTION_DISABLE_DBGLOG;

        if (BMIWriteMemory(ar->arHifDevice,
            HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_option_flag),
            (A_UCHAR *)&param,
            4) != A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIWriteMemory for HI_OPTION_DISABLE_DBGLOG\n"));
            return A_ERROR;
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Firmware mode set\n"));
    }
#endif /* ATH6KL_DISABLE_TARGET_DBGLOGS */

    if (regscanmode) {
        A_UINT32 param;

        if (BMIReadMemory(ar->arHifDevice,
            HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_option_flag),
            (A_UCHAR *)&param,
            4)!= A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIReadMemory for setting regscanmode failed\n"));
            return A_ERROR;
        }

        if (regscanmode == 1) {
            param |= HI_OPTION_SKIP_REG_SCAN;
        } else if (regscanmode == 2) {
            param |= HI_OPTION_INIT_REG_SCAN;
        }

        if (BMIWriteMemory(ar->arHifDevice,
            HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_option_flag),
            (A_UCHAR *)&param,
            4) != A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIWriteMemory for setting regscanmode failed\n"));
            return A_ERROR;
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Regulatory scan mode set\n"));
    }

    /*
     * Hardcode the address use for the extended board data
     * Ideally this should be pre-allocate by the OS at boot time
     * But since it is a new feature and board data is loaded
     * at init time, we have to workaround this from host.
     * It is difficult to patch the firmware boot code,
     * but possible in theory.
     */
    if (ar->arTargetType == TARGET_TYPE_AR6003) {
        A_UINT32 ramReservedSz;
        if (ar->arVersion.target_ver == AR6003_REV2_VERSION) {
            param = AR6003_REV2_BOARD_EXT_DATA_ADDRESS;
            ramReservedSz =  AR6003_REV2_RAM_RESERVE_SIZE;
        } else {
            param = AR6003_REV3_BOARD_EXT_DATA_ADDRESS;
            ramReservedSz =  AR6003_REV3_RAM_RESERVE_SIZE;
        }
        if (BMIWriteMemory(ar->arHifDevice,
            HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_board_ext_data),
            (A_UCHAR *)&param,
            4) != A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIWriteMemory for hi_board_ext_data failed \n"));
            return A_ERROR;
        }
        if (BMIWriteMemory(ar->arHifDevice,
              HOST_INTEREST_ITEM_ADDRESS(ar->arTargetType, hi_end_RAM_reserve_sz),
              (A_UCHAR *)&ramReservedSz, 4) != A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("BMIWriteMemory for hi_end_RAM_reserve_sz failed \n"));
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

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
static void ar6000_ethtool_get_drvinfo(struct net_device *dev,
                                    struct ethtool_drvinfo *info)
{
    A_STATUS status;
    HIF_DEVICE_OS_DEVICE_INFO osDevInfo;
    AR_SOFTC_T     *ar;
    AR_SOFTC_DEV_T *arPriv;
    struct ar6000_version *revinfo;
    if((dev == NULL) || ((arPriv = ar6k_priv(dev)) == NULL)) {
        return;
    }
    ar = arPriv->arSoftc;
    revinfo = &ar->arVersion;
    strcpy(info->driver, "AR6000");
    snprintf(info->version, sizeof(info->version), "%u.%u.%u.%u",
             ((revinfo->host_ver)&0xf0000000)>>28,
             ((revinfo->host_ver)&0x0f000000)>>24,
             ((revinfo->host_ver)&0x00ff0000)>>16,
             ((revinfo->host_ver)&0x0000ffff));
    snprintf(info->fw_version, sizeof(info->fw_version), "%u.%u.%u.%u",
             ((revinfo->wlan_ver)&0xf0000000)>>28,
             ((revinfo->wlan_ver)&0x0f000000)>>24,
             ((revinfo->wlan_ver)&0x00ff0000)>>16,
             ((revinfo->wlan_ver)&0x0000ffff));

    status = HIFConfigureDevice(ar->arHifDevice,
                                HIF_DEVICE_GET_OS_DEVICE,
                                &osDevInfo,
                                sizeof(HIF_DEVICE_OS_DEVICE_INFO));
    if (A_SUCCESS(status) && osDevInfo.pOSDevice) {
        struct device *dev = (struct device*)osDevInfo.pOSDevice;
        if (dev->bus && dev->bus->name) {
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 27)
            const char *dinfo = dev_name(dev);
#else
            const char *dinfo = kobject_name(&dev->kobj);
#endif
            snprintf(info->bus_info, sizeof(info->bus_info), dinfo);
        }
    }
}

static u32 ar6000_ethtool_get_link(struct net_device *dev)
{
    AR_SOFTC_DEV_T *arPriv;
    return ((arPriv = ar6k_priv(dev))!=NULL) ? arPriv->arConnected : 0;
}

#ifdef CONFIG_CHECKSUM_OFFLOAD
static u32 ar6000_ethtool_get_rx_csum(struct net_device *dev)
{
    AR_SOFTC_DEV_T *arPriv;
    if((dev == NULL) || ((arPriv = ar6k_priv(dev)) == NULL)) {
        return 0;
    }
    return (arPriv->arSoftc->rxMetaVersion==WMI_META_VERSION_2);
}

static int ar6000_ethtool_set_rx_csum(struct net_device *dev, u32 enable)
{
    AR_SOFTC_T *ar;
    AR_SOFTC_DEV_T *arPriv;
    A_UINT8 metaVersion;
    if((dev == NULL) || ((arPriv = ar6k_priv(dev)) == NULL)) {
        return -EIO;
    }
    ar = arPriv->arSoftc;
    if (ar->arWmiReady == FALSE || ar->arWlanState == WLAN_DISABLED) {
        return -EIO;
    }
    metaVersion = (enable) ? WMI_META_VERSION_2 : 0;
    if ((wmi_set_rx_frame_format_cmd(arPriv->arWmi, metaVersion, processDot11Hdr, processDot11Hdr)) != A_OK) {
        return -EFAULT;
    }
    ar->rxMetaVersion = metaVersion;
    return 0;
}

static u32 ar6000_ethtool_get_tx_csum(struct net_device *dev)
{
    return csumOffload;
}

static int ar6000_ethtool_set_tx_csum(struct net_device *dev, u32 enable)
{
    csumOffload = enable;
    if(enable){
        dev->features |= NETIF_F_IP_CSUM; 
    } else {
        dev->features &= ~NETIF_F_IP_CSUM; 
    }
    return 0;
}
#endif /* CONFIG_CHECKSUM_OFFLOAD */

static const struct ethtool_ops ar6000_ethtool_ops = {
    .get_drvinfo = ar6000_ethtool_get_drvinfo,
    .get_link = ar6000_ethtool_get_link,
#ifdef CONFIG_CHECKSUM_OFFLOAD
    .get_rx_csum = ar6000_ethtool_get_rx_csum,
    .set_rx_csum = ar6000_ethtool_set_rx_csum,
    .get_tx_csum = ar6000_ethtool_get_tx_csum,
    .set_tx_csum = ar6000_ethtool_set_tx_csum,
#endif /* CONFIG_CHECKSUM_OFFLOAD */
};
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24) */

/*
 * HTC Event handlers
 */
static A_STATUS
ar6000_avail_ev(void *context, void *hif_handle)
{
    int i;
    struct net_device *dev;
    void *ar_netif;
    AR_SOFTC_T *ar=NULL;
    AR_SOFTC_DEV_T *arPriv;
    int device_index = 0;
    HTC_INIT_INFO  htcInfo;
#ifdef ATH6K_CONFIG_CFG80211
    struct wireless_dev *wdev;
#endif /* ATH6K_CONFIG_CFG80211 */
    A_STATUS init_status = A_OK;
    unsigned char devnum = 0;
    unsigned char cnt = 0;

    /*
     * If ar6000_avail_ev is called more than once, this means that
     * multiple AR600x devices have been inserted into the system.
     * We do not support more than one AR600x device at this time.
     */
#ifndef RK29
    if (avail_ev_called) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("ERROR: More than one AR600x device not supported by driver\n"));
        return A_ERROR;
    }

    avail_ev_called = TRUE;
#endif

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("ar6000_available\n"));



     ar = A_MALLOC(sizeof(AR_SOFTC_T));

     if (ar == NULL) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR_SOFTC: can not allocate\n"));
        return A_ERROR;
     }
    A_MEMZERO(ar, sizeof(AR_SOFTC_T));

#ifdef ATH_AR6K_11N_SUPPORT
    if(aggr_init(ar6000_alloc_netbufs, ar6000_deliver_frames_to_nw_stack) != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s() Failed to initialize aggr.\n", __func__));
            init_status = A_ERROR;
            goto avail_ev_failed;
    }
#endif

    A_MEMZERO((A_UINT8 *)ar->connTbl, NUM_CONN * sizeof(conn_t));
      /* Init the PS queues */
    for (i=0; i < NUM_CONN ; i++) {
#ifdef ATH_AR6K_11N_SUPPORT
        if((ar->connTbl[i].conn_aggr = aggr_init_conn()) == NULL) {
             AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
                     ("%s() Failed to initialize aggr.\n", __func__));
                A_FREE(ar);
                return A_ERROR;
        }
#endif
         A_MUTEX_INIT(&ar->connTbl[i].psqLock);
         A_NETBUF_QUEUE_INIT(&ar->connTbl[i].psq);
         A_NETBUF_QUEUE_INIT(&ar->connTbl[i].apsdq);
    }

#if 0//LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    if(ifname[0]) {
        for(i = 0; i < strlen(ifname); i++) {
           if(ifname[i] >= '0' && ifname[i] <= '9' ) {
               devnum = (devnum * 10) + (ifname[i] - '0');
           }
           else {
               cnt++;
           }
        }
        ifname[cnt]='\0';
    }
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) */
    ar->arConfNumDev = num_device;
    for (i=0; i < num_device; i++) {

        if (ar6000_devices[i] != NULL) {
            break;
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
        dev = alloc_etherdev(sizeof(AR_SOFTC_DEV_T));
        if (dev == NULL) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_available: can't alloc etherdev\n"));
            A_FREE(ar);
            return A_ERROR;
        }
        ether_setup(dev);
        ar_netif = ar6k_priv(dev);
#endif /* ATH6K_CONFIG_CFG80211 */

        if (ar_netif == NULL) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("%s: Can't allocate ar6k priv memory\n", __func__));
            A_FREE(ar);
            return A_ERROR;
        }

        A_MEMZERO(ar_netif, sizeof(AR_SOFTC_DEV_T));
        arPriv = (AR_SOFTC_DEV_T *)ar_netif;

#ifdef ATH6K_CONFIG_CFG80211
        arPriv->wdev = wdev;
        wdev->iftype = NL80211_IFTYPE_STATION;

        dev = alloc_netdev_mq(0, "wlan%d", ether_setup, NUM_SUBQUEUE);
        if (!dev) {
            printk(KERN_CRIT "AR6K: no memory for network device instance\n");
            ar6k_cfg80211_deinit(arPriv);
            A_FREE(ar);
            return A_ERROR;
        }

        dev->ieee80211_ptr = wdev;
        SET_NETDEV_DEV(dev, wiphy_dev(wdev->wiphy));
        wdev->netdev = dev;
        arPriv->arNetworkType = INFRA_NETWORK;
#endif /* ATH6K_CONFIG_CFG80211 */

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
        if (ifname[0])
        {
             //sprintf(dev->name, "%s%d", ifname,(devnum + device_index));
			 sprintf(dev->name, "%s", ifname);
        }
#endif /* LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0) */

#ifdef SET_MODULE_OWNER
        SET_MODULE_OWNER(dev);
#endif

#ifdef SET_NETDEV_DEV
#if 0
        if (ar_netif) {
            HIF_DEVICE_OS_DEVICE_INFO osDevInfo;
            A_MEMZERO(&osDevInfo, sizeof(osDevInfo));
            if ( A_SUCCESS( HIFConfigureDevice(hif_handle, HIF_DEVICE_GET_OS_DEVICE,
                            &osDevInfo, sizeof(osDevInfo))) ) {
                SET_NETDEV_DEV(dev, osDevInfo.pOSDevice);
            }
        }
#endif
#endif

        arPriv->arNetDev             = dev;
        ar6000_devices[device_index] = dev;
        arPriv->arSoftc              = ar;
        ar->arDev[device_index]      = arPriv;
        ar->arWlanState              = WLAN_ENABLED;
        arPriv->arDeviceIndex        = device_index;

        ar->arWlanPowerState         = WLAN_POWER_STATE_ON;

        if(ar6000_init_control_info(arPriv) != A_OK) {
                init_status = A_ERROR;
                goto avail_ev_failed;
        }
        init_waitqueue_head(&arPriv->arEvent);

#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
        A_INIT_TIMER(&aptcTimer[i], aptcTimerHandler, ar);
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */

        spin_lock_init(&arPriv->arPrivLock);

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
        dev->open = &ar6000_open;
        dev->stop = &ar6000_close;
        dev->hard_start_xmit = &ar6000_data_tx;
        dev->get_stats = &ar6000_get_stats;

        /* dev->tx_timeout = ar6000_tx_timeout; */
        dev->do_ioctl = &ar6000_ioctl;
        dev->set_multicast_list = &ar6000_set_multicast_list;
#else
        dev->netdev_ops = &ar6000_netdev_ops;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29) */
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2, 6, 24)
        dev->ethtool_ops = &ar6000_ethtool_ops;
#endif
        dev->watchdog_timeo = AR6000_TX_TIMEOUT;
        dev->wireless_handlers = &ath_iw_handler_def;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,19)
        dev->get_wireless_stats = ar6000_get_iwstats; /*Displayed via proc fs */
#else
        ath_iw_handler_def.get_wireless_stats = ar6000_get_iwstats; /*Displayed via proc fs */
#endif
#ifdef CONFIG_CHECKSUM_OFFLOAD
        if(csumOffload){

            dev->features |= NETIF_F_IP_CSUM;/*advertise kernel capability
                                             to do TCP/UDP CSUM offload for IPV4*/
        }
#endif
        if (processDot11Hdr) {
            dev->hard_header_len = sizeof(struct ieee80211_qosframe) + sizeof(ATH_LLC_SNAP_HDR) + sizeof(WMI_DATA_HDR) + HTC_HEADER_LEN + WMI_MAX_TX_META_SZ + LINUX_HACK_FUDGE_FACTOR;
        } else {
            /*
             * We need the OS to provide us with more headroom in order to
             * perform dix to 802.3, WMI header encap, and the HTC header
             */
            dev->hard_header_len = ETH_HLEN + sizeof(ATH_LLC_SNAP_HDR) +
                sizeof(WMI_DATA_HDR) + HTC_HEADER_LEN + WMI_MAX_TX_META_SZ + LINUX_HACK_FUDGE_FACTOR;
        }

       if (!bypasswmi)
       {
           /* Indicate that WMI is enabled (although not ready yet) */
           arPriv->arWmiEnabled = TRUE;
           if ((arPriv->arWmi = wmi_init((void *) arPriv,arPriv->arDeviceIndex)) == NULL)
           {
               AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s() Failed to initialize WMI.\n", __func__));
               init_status = A_ERROR;
               goto avail_ev_failed;
           }

           AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("%s() Got WMI @ 0x%08x.\n", __func__,
               (unsigned int) arPriv->arWmi));
       }
#ifdef P2P
        /* Allocate P2P module context if this dev is in any of the P2P modes.
         * For non-P2P devices, this may be allocated just in time when the
         * device assumes a P2P submode. This may be needed when we do
         * mode switch between none and P2P submodes. For later enhancement.
         */
        if (arPriv->arNetworkSubType == SUBTYPE_P2PDEV ||
            arPriv->arNetworkSubType == SUBTYPE_P2PCLIENT ||
            arPriv->arNetworkSubType == SUBTYPE_P2PGO) {
            arPriv->p2p_ctx = p2p_init(arPriv);
        }
#endif /* P2P */
    }

#ifdef CONFIG_HOST_TCMD_SUPPORT
    if(testmode) {
        ar->arTargetMode = AR6000_TCMD_MODE;
    }else {
        ar->arTargetMode = AR6000_WLAN_MODE;
    }
#endif
    ar->arWlanOff            = FALSE;   /* We are in ON state */
#ifdef CONFIG_PM
    ar->arWowState           = WLAN_WOW_STATE_NONE;
    ar->arBTOff              = TRUE;   /* BT chip assumed to be OFF */
    ar->arBTSharing          = WLAN_CONFIG_BT_SHARING;
    ar->arWlanOffConfig      = WLAN_CONFIG_WLAN_OFF;
    ar->arSuspendConfig      = WLAN_CONFIG_PM_SUSPEND;
    ar->arWow2Config         = WLAN_CONFIG_PM_WOW2;
#endif /* CONFIG_PM */

    A_INIT_TIMER(&ar->arHBChallengeResp.timer, ar6000_detect_error, ar);
    ar->arHBChallengeResp.seqNum = 0;
    ar->arHBChallengeResp.outstanding = FALSE;
    ar->arHBChallengeResp.missCnt = 0;
    ar->arHBChallengeResp.frequency = AR6000_HB_CHALLENGE_RESP_FREQ_DEFAULT;
    ar->arHBChallengeResp.missThres = AR6000_HB_CHALLENGE_RESP_MISS_THRES_DEFAULT;
    ar->arHifDevice              = hif_handle;
    sema_init(&ar->arSem, 1);
    ar->bIsDestroyProgress = FALSE;

    INIT_HTC_PACKET_QUEUE(&ar->amsdu_rx_buffer_queue);
    /*
     * If requested, perform some magic which requires no cooperation from
     * the Target.  It causes the Target to ignore flash and execute to the
     * OS from ROM.
     *
     * This is intended to support recovery from a corrupted flash on Targets
     * that support flash.
     */
    if (skipflash)
    {
        //ar6000_reset_device_skipflash(ar->arHifDevice);
    }

    BMIInit();

    if (bmienable) {
        ar6000_sysfs_bmi_init(ar);
    }

    {
        struct bmi_target_info targ_info;
        A_MEMZERO(&targ_info, sizeof(targ_info));
        if (BMIGetTargetInfo(ar->arHifDevice, &targ_info) != A_OK) {
            init_status = A_ERROR;
            goto avail_ev_failed;
        }

        ar->arVersion.target_ver = targ_info.target_ver;
        ar->arTargetType = targ_info.target_type;

        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("%s() TARGET TYPE: %d\n", __func__,ar->arTargetType));
        target_register_tbl_attach(ar->arTargetType);

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

#ifdef CONFIG_CHECKSUM_OFFLOAD
    if(csumOffload){

        ar->rxMetaVersion=WMI_META_VERSION_2;/*if external frame work is also needed, change and use an extended rxMetaVerion*/
    }
#endif

    HIFClaimDevice(ar->arHifDevice, ar);

    if (bmienable)
    {
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
         }while (FALSE);
      }
   }

   if(bmienable) {
      AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("BMI enabled: %d\n", wlaninitmode));
      if ((wlaninitmode == WLAN_INIT_MODE_UDEV) ||
         (wlaninitmode == WLAN_INIT_MODE_DRV))
      {
          A_STATUS status = A_OK;
          dev = ar6000_devices[0];
          do {
#ifdef HTC_RAW_INTERFACE
              if (!eppingtest && bypasswmi) {
                  break; /* Don't call ar6000_init for ART */
              }
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

   for (i=0; i < num_device; i++)
   {
      dev = ar6000_devices[i];
      arPriv = ar6k_priv(dev);
      ar = arPriv->arSoftc;
      /* Don't install the init function if BMI is requested */
      if (!bmienable) {
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29)
          dev->init = ar6000_init;
#else
          ar6000_netdev_ops.ndo_init = ar6000_init;
#endif /* LINUX_VERSION_CODE < KERNEL_VERSION(2,6,29) */
      }

      /* This runs the init function if registered */
      if (register_netdev(dev)) {
          AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_avail: register_netdev failed\n"));
          ar6000_cleanup(ar);
          ar6000_devices[i]=NULL;
          ar6000_destroy(dev, 0);
          return A_ERROR;
      }

      AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("ar6000_avail: name=%s hifdevice=0x%lx, dev=0x%lx (%d), ar=0x%lx\n",
                    dev->name, (unsigned long)ar->arHifDevice, (unsigned long)dev, device_index,
                    (unsigned long)ar));

   }

avail_ev_failed :
    if (A_FAILED(init_status)) {
        if (bmienable) {
            ar6000_sysfs_bmi_deinit(ar);
        }
       for (i=0; i < num_device; i++)
       {
         dev = ar6000_devices[i];
         arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
         if(arPriv->arWmiEnabled == TRUE)
         {
           wmi_shutdown(arPriv->arWmi);
           arPriv->arWmiEnabled = FALSE;
         }
         ar6000_devices[i] = NULL;
       }
        A_FREE(ar);
    }

    return init_status;
}

static void ar6000_target_failure(void *Instance, A_STATUS Status)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)Instance;
    WMI_TARGET_ERROR_REPORT_EVENT errEvent;
    static A_BOOL sip = FALSE;
    A_UINT8 i;

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
            for(i = 0; i < num_device; i++)
            {

                ar6000_send_event_to_app(ar->arDev[i], WMI_ERROR_REPORT_EVENTID,
                                         (A_UINT8 *)&errEvent,
                                         sizeof(WMI_TARGET_ERROR_REPORT_EVENT));

            }
        }
    }
}

static A_STATUS
ar6000_unavail_ev(void *context, void *hif_handle)
{
    unsigned int old_reset_ok = resetok;
    A_UINT8  i;
    struct net_device *ar6000_netdev;
    AR_SOFTC_T *ar = (AR_SOFTC_T*)context;
    resetok = 0; /* card is remove, don't reset */
    ar6000_cleanup(ar);
    resetok = old_reset_ok;
    /* NULL out it's entry in the global list */
    for(i = 0; i < num_device; i++) {
        ar6000_netdev = ar6000_devices[i];
        ar6000_devices[i] = NULL;
        ar6000_destroy(ar6000_netdev, 1);
    }

    return A_OK;
}


void
ar6000_restart_endpoint(AR_SOFTC_T *ar)
{

    A_STATUS status = A_OK;
    AR_SOFTC_DEV_T *arPriv;
    struct net_device *dev;
    A_UINT8  i = 0;

    /*
     * Call wmi_init for each device.  This must be done BEFORE ar6000_init() is
     * called, or we will get a null pointer exception in the wmi code.  We must
     * also set the arWmiEnabled flag for each device.
     */
    for(i = 0; i < num_device; i++) {
        arPriv = ar->arDev[i];
	arPriv->arWmiEnabled = TRUE;
        if ((arPriv->arWmi = wmi_init((void *) arPriv,arPriv->arDeviceIndex)) == NULL)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s() Failed to initialize WMI.\n", __func__));
            status = A_ERROR;
            goto exit;
        }
    }


    BMIInit();
    if (bmienable) {
        ar6000_sysfs_bmi_init(ar);
    }
    do {
        if ( (status=ar6000_configure_target(ar))!=A_OK)
            break;
        if ( (status=ar6000_sysfs_bmi_get_config(ar, wlaninitmode)) != A_OK)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_avail: ar6000_sysfs_bmi_get_config failed\n"));
            break;
        }
    } while(0);

    dev = ar6000_devices[0];
    rtnl_lock();
    status = (ar6000_init(dev)==0) ? A_OK : A_ERROR;
    rtnl_unlock();

    if (status!=A_OK) {
       goto exit;
    }


    for(i = 0; i < num_device; i++) {
        arPriv = ar->arDev[i];
        if (arPriv->arDoConnectOnResume &&
            arPriv->arSsidLen &&
            ar->arWlanState == WLAN_ENABLED)
        {
            ar6000_connect_to_ap(arPriv);
        }
    }

    if (status==A_OK) {
        return;
    }

exit:
    for(i = 0; i < num_device; i++) {
        arPriv = ar->arDev[i];
        ar6000_devices[i] = NULL;
        ar6000_destroy(arPriv->arNetDev, 1);
    }

}

void
ar6000_stop_endpoint(AR_SOFTC_T *ar, A_BOOL keepprofile, A_BOOL getdbglogs)
{

    AR_SOFTC_DEV_T *arPriv ;
    A_UINT8 i;
    A_UINT8 ctr;
    AR_SOFTC_STA_T *arSta;

    for(i = 0; i < num_device; i++)
    {
        arPriv = ar->arDev[i];
        arSta = &arPriv->arSta;
        /* Stop the transmit queues */
        netif_stop_queue(arPriv->arNetDev);

        /* Disable the target and the interrupts associated with it */
        if (ar->arWmiReady == TRUE)
        {
            if (!bypasswmi) {
                A_BOOL disconnectIssued;

                arPriv->arDoConnectOnResume = arPriv->arConnected;

                A_UNTIMEOUT(&arPriv->arSta.disconnect_timer);
                A_UNTIMEOUT(&ar->ap_reconnect_timer);
                A_UNTIMEOUT(&arPriv->ap_acs_timer);
#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
                /* Delete the Adaptive Power Control timer */
                if (timer_pending(&aptcTimer[i])) {
                        del_timer_sync(&aptcTimer[i]);
                }
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */
#ifdef ATH_AR6K_11N_SUPPORT
                for (ctr=0; ctr < NUM_CONN ; ctr++) {
                        aggr_module_destroy_timers(ar->connTbl[ctr].conn_aggr);
                }
#endif

                disconnectIssued = (arPriv->arConnected) || (arPriv->arSta.arConnectPending);
                ar6000_disconnect(arPriv);
                if (!keepprofile) {
                    ar6000_init_profile_info(arPriv);
                }
                if (getdbglogs) {
                    ar6000_dbglog_get_debug_logs(ar);
                }
                ar->arWmiReady  = FALSE;
                arPriv->arWmiEnabled = FALSE;
                wmi_shutdown(arPriv->arWmi);
                arPriv->arWmi = NULL;
               /*
                * After wmi_shudown all WMI events will be dropped.
                * We need to cleanup the buffers allocated in AP mode
                * and give disconnect notification to stack, which usually
                * happens in the disconnect_event.
                * Simulate the disconnect_event by calling the function directly.
                * Sometimes disconnect_event will be received when the debug logs
                * are collected.
                */
                if (disconnectIssued) {
                    if(arPriv->arNetworkType & AP_NETWORK) {
                        ar6000_disconnect_event(arPriv, DISCONNECT_CMD, bcast_mac, 0, NULL, 0);
                    } else {
                        ar6000_disconnect_event(arPriv, DISCONNECT_CMD, arPriv->arBssid, 0, NULL, 0);
                    }
                }
#ifdef USER_KEYS
                arPriv->arSta.user_savedkeys_stat = USER_SAVEDKEYS_STAT_INIT;
                arPriv->arSta.user_key_ctrl      = 0;
#endif
            }
             AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("%s(): WMI stopped\n", __func__));
        }
        else
        {
             AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("%s(): WMI not ready 0x%lx 0x%lx\n",
                 __func__, (unsigned long) ar, (unsigned long) arPriv->arWmi));
            /* Shut down WMI if we have started it */
            if(arPriv->arWmiEnabled == TRUE)
            {
                AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("%s(): Shut down WMI\n", __func__));
                arPriv->arWmiEnabled = FALSE;
                wmi_shutdown(arPriv->arWmi);
                arPriv->arWmi = NULL;
            }
        }
        /* cleanup hci pal driver data structures */
        if (setuphcipal && (arPriv->isBt30amp == TRUE)) {
            ar6k_cleanup_hci_pal(arPriv);
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

        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,(" Shutting down HTC .... \n"));
    /* stop HTC */
        HTCStop(ar->arHtcTarget);
        ar6k_init = FALSE;
    }

    if (resetok) {
   /* try to reset the device if we can
    * The driver may have been configure NOT to reset the target during
    * a debug session */
        AR_DEBUG_PRINTF(ATH_DEBUG_ANY,(" Attempting to reset target on instance destroy.... \n"));
        if (ar->arHifDevice != NULL) {
#if defined(CONFIG_MMC_MSM) || defined(CONFIG_MMC_SDHCI_S3C)
            A_BOOL coldReset = ((ar->arTargetType == TARGET_TYPE_AR6003)|| (ar->arTargetType == TARGET_TYPE_MCKINLEY)) ? TRUE: FALSE;
#else
            A_BOOL coldReset = (ar->arTargetType == TARGET_TYPE_MCKINLEY) ? TRUE: FALSE;
#endif
            ar6000_reset_device(ar->arHifDevice, ar->arTargetType, TRUE, coldReset);
        }
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,(" Host does not want target reset. \n"));
    }
   /* Done with cookies */
    ar6000_cookie_cleanup(ar);

       /* cleanup any allocated AMSDU buffers */
    ar6000_cleanup_amsdu_rxbufs(ar);

    if (bmienable) {
        ar6000_sysfs_bmi_deinit(ar);
    }
}

void ar6000_cleanup(AR_SOFTC_T *ar)
{
    A_UINT8 ctr;
    ar->bIsDestroyProgress = TRUE;

    if (down_interruptible(&ar->arSem)) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s(): down_interruptible failed \n", __func__));
        return;
    }

    if (ar->arWlanPowerState != WLAN_POWER_STATE_CUT_PWR) {
        /* only stop endpoint if we are not stop it in suspend_ev */
        ar6000_stop_endpoint(ar, FALSE, TRUE);
    } else {
        /* clear up the platform power state before rmmod */
        plat_setup_power(ar, 1, 0);
        ar->arPlatPowerOff = FALSE;
    }

#ifdef ATH_AR6K_11N_SUPPORT
    for (ctr=0; ctr < NUM_CONN ; ctr++) {
        aggr_module_destroy_conn(ar->connTbl[ctr].conn_aggr);
    }
    aggr_module_destroy();
#endif

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
    A_UNTIMEOUT(&ar->ap_reconnect_timer);
    A_UNTIMEOUT(&ar->arHBChallengeResp.timer);
    A_FREE(ar);

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
    AR_SOFTC_DEV_T *arPriv;
    AR_SOFTC_AP_T  *arAp;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("+ar6000_destroy \n"));

    if((dev == NULL) || ((arPriv = ar6k_priv(dev)) == NULL))
    {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s(): Failed to get device structure.\n", __func__));
        return;
    }

    A_UNTIMEOUT(&arPriv->ap_acs_timer);
    aggr_module_destroy_conn(arPriv->conn_aggr);

    if(arPriv->arNetworkType == AP_NETWORK)
    {
        arAp = &arPriv->arAp;

#ifdef ATH_SUPPORT_DFS
        dfs_detach_host(arAp->pDfs);
#endif
    }
    ar6k_init = FALSE;
    /* Free up the device data structure */
    if (unregister) {
        unregister_netdev(dev);
    }
#define free_netdev_support
#ifndef free_netdev_support
    kfree(dev);
#else
    free_netdev(dev);
#endif

#ifdef ATH6K_CONFIG_CFG80211
    ar6k_cfg80211_deinit(arPriv);
#endif /* ATH6K_CONFIG_CFG80211 */

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("-ar6000_destroy \n"));
}

static void ap_reconnect_timer_handler(unsigned long ptr)
{
    AR_SOFTC_T *ar= (AR_SOFTC_T *)ptr;
    AR_SOFTC_DEV_T *arTempPriv = NULL;
    A_UINT8 i=0;
    A_UNTIMEOUT(&ar->ap_reconnect_timer);

    if(ar->arHoldConnection){
        ar->arHoldConnection = FALSE;
        for(i=0;i<ar->arConfNumDev;i++){
            arTempPriv = ar->arDev[i];
            if((AP_NETWORK == arTempPriv->arNetworkType) &&
               (arTempPriv->arHoldConnection)){
                arTempPriv->arHoldConnection = FALSE;
                ar6000_ap_mode_profile_commit(arTempPriv);
            }
        }
    }
}
static void disconnect_timer_handler(unsigned long ptr)
{
    struct net_device *dev = (struct net_device *)ptr;
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);

    A_UNTIMEOUT(&arPriv->arSta.disconnect_timer);

    ar6000_init_profile_info(arPriv);
    ar6000_disconnect(arPriv);
}

static void ar6000_detect_error(unsigned long ptr)
{
    AR_SOFTC_T *ar = (AR_SOFTC_T *)ptr;
    A_UINT8 i;
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
        for(i = 0; i < num_device; i++)
        {
            ar6000_send_event_to_app(ar->arDev[i], WMI_ERROR_REPORT_EVENTID,
                                     (A_UINT8 *)&errEvent,
                                     sizeof(WMI_TARGET_ERROR_REPORT_EVENT));
        }
        return;
    }

    /* Generate the sequence number for the next challenge */
    ar->arHBChallengeResp.seqNum++;
    ar->arHBChallengeResp.outstanding = TRUE;

    AR6000_SPIN_UNLOCK(&ar->arLock, 0);

    /* Send the challenge on the control channel */
    if (wmi_get_challenge_resp_cmd(ar->arDev[0]->arWmi, ar->arHBChallengeResp.seqNum, DRV_HB_CHALLENGE) != A_OK) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Unable to send heart beat challenge\n"));
    }


    /* Reschedule the timer for the next challenge */
    A_TIMEOUT_MS(&ar->arHBChallengeResp.timer, ar->arHBChallengeResp.frequency * 1000, 0);
}

void ar6000_init_profile_info(AR_SOFTC_DEV_T *arPriv)
{
    A_UINT8 mode = 0;
    A_UINT8 submode = 0;
    mode = ((fwmode >> (arPriv->arDeviceIndex * HI_OPTION_FW_MODE_BITS)) & (HI_OPTION_FW_MODE_MASK ));

    switch(mode) {
        case HI_OPTION_FW_MODE_IBSS:
            arPriv->arNetworkType = arPriv->arNextMode = ADHOC_NETWORK;
            break;
        case HI_OPTION_FW_MODE_BSS_STA:
            arPriv->arNetworkType = arPriv->arNextMode = INFRA_NETWORK;
            break;
        case HI_OPTION_FW_MODE_AP:
            arPriv->arNetworkType = arPriv->arNextMode = AP_NETWORK;
            break;
        case HI_OPTION_FW_MODE_BT30AMP:
            arPriv->arNetworkType = arPriv->arNextMode = INFRA_NETWORK;
            arPriv->isBt30amp = TRUE;
            break;
    }
    /* Initialize firware sub mode
     */
    submode = ((fwsubmode>>(arPriv->arDeviceIndex * HI_OPTION_FW_SUBMODE_BITS))
                   & (HI_OPTION_FW_SUBMODE_MASK));

    switch(submode) {
        case HI_OPTION_FW_SUBMODE_NONE:
            arPriv->arNetworkSubType = SUBTYPE_NONE;
            break;
        case HI_OPTION_FW_SUBMODE_P2PDEV:
            arPriv->arNetworkSubType = SUBTYPE_P2PDEV;
            break;
        case HI_OPTION_FW_SUBMODE_P2PCLIENT:
            arPriv->arNetworkSubType = SUBTYPE_P2PCLIENT;
            break;
        case HI_OPTION_FW_SUBMODE_P2PGO:
            arPriv->arNetworkSubType = SUBTYPE_P2PGO;
            break;
    }

    ar6000_init_mode_info(arPriv);
}

static int
ar6000_init_control_info(AR_SOFTC_DEV_T *arPriv)
{
    AR_SOFTC_T       *ar = arPriv->arSoftc;

    arPriv->arWmiEnabled         = FALSE;
    ar->arVersion.host_ver       = AR6K_SW_VERSION;

    if(!(strcmp(targetconf,"mobile")))
            ar->arVersion.targetconf_ver = AR6003_SUBVER_MOBILE;
    else if(!(strcmp(targetconf,"tablet")))
            ar->arVersion.targetconf_ver = AR6003_SUBVER_TABLET;
    else if(!(strcmp(targetconf,"router")))
            ar->arVersion.targetconf_ver = AR6003_SUBVER_ROUTER;
    else if(!(strcmp(targetconf,"default")))
            ar->arVersion.targetconf_ver = AR6003_SUBVER_DEFAULT;
    else
            ar->arVersion.targetconf_ver = AR6003_SUBVER_MOBILE;


    ar6000_init_profile_info(arPriv);

    if((arPriv->conn_aggr = aggr_init_conn()) == NULL) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,
             ("%s() Failed to initialize aggr.\n", __func__));
        return A_ERROR;
    }
    return A_OK;
}

static int
ar6000_open(struct net_device *dev)
{
    unsigned long  flags;
    AR_SOFTC_DEV_T    *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);

    spin_lock_irqsave(&arPriv->arPrivLock, flags);

#ifdef ATH6K_CONFIG_CFG80211
    if(arPriv->arSoftc->arWlanState == WLAN_DISABLED) {
        arPriv->arSoftc->arWlanState = WLAN_ENABLED;
    }
#endif /* ATH6K_CONFIG_CFG80211 */

    if( arPriv->arConnected || bypasswmi) {
        netif_carrier_on(dev);
        /* Wake up the queues */
        netif_wake_queue(dev);
    }
    else
        netif_carrier_off(dev);

    spin_unlock_irqrestore(&arPriv->arPrivLock, flags);
    return 0;
}

static int
ar6000_close(struct net_device *dev)
{
#ifdef ATH6K_CONFIG_CFG80211
    AR_SOFTC_DEV_T    *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
#endif /* ATH6K_CONFIG_CFG80211 */
    netif_stop_queue(dev);

#ifdef ATH6K_CONFIG_CFG80211
    ar6000_disconnect(arPriv);

    if(arPriv->arSoftc->arWmiReady == TRUE) {
        if (wmi_scanparams_cmd(arPriv->arWmi, 0xFFFF, 0,
                               0, 0, 0, 0, 0, 0, 0, 0) != A_OK) {
            return -EIO;
        }
        arPriv->arSoftc->arWlanState = WLAN_DISABLED;
    }
#endif /* ATH6K_CONFIG_CFG80211 */

    return 0;
}

/* connect to a service */
static A_STATUS ar6000_connectservice(AR_SOFTC_DEV_T           *arPriv,
                                      HTC_SERVICE_CONNECT_REQ  *pConnect,
                                      char                     *pDesc)
{
    A_STATUS                 status;
    HTC_SERVICE_CONNECT_RESP response;
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

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
                if(!bypasswmi)
                {
                    /* set control endpoint for WMI use */
                    wmi_set_control_ep(arPriv->arWmi, response.Endpoint);
                    /* save EP for fast lookup */
                    ar->arControlEp = response.Endpoint;
                }
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *) devt;
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    return(arAc2EndpointID(ar, ac));
}

A_UINT8
ar6000_endpoint_id2_ac(void * devt, HTC_ENDPOINT_ID ep )
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *) devt;
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    return(arEndpoint2Ac(ar, ep ));
}


/* This function does one time initialization for the lifetime of the device */
int ar6000_init(struct net_device *dev)
{
    AR_SOFTC_DEV_T *arPriv;
    AR_SOFTC_T  *ar;
    int         ret = 0;
    int           i = 0;
    int           j = 0;
    A_STATUS    status;
    A_INT32     timeleft;
#if defined(INIT_MODE_DRV_ENABLED) && defined(ENABLE_COEXISTENCE)
    WMI_SET_BTCOEX_COLOCATED_BT_DEV_CMD sbcb_cmd;
    WMI_SET_BTCOEX_FE_ANT_CMD sbfa_cmd;
#endif /* INIT_MODE_DRV_ENABLED && ENABLE_COEXISTENCE */

    dev_hold(dev);
    rtnl_unlock();

    if(ar6k_init)
    {
       AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR6000 Initialised\n"));
       goto ar6000_init_done;
    }

    ar6k_init = TRUE;
    if((arPriv = ar6k_priv(dev)) == NULL)
    {
        ret = -EIO;
        goto ar6000_init_done;
    }

    ar = arPriv->arSoftc;

    if (wlaninitmode == WLAN_INIT_MODE_USR || wlaninitmode == WLAN_INIT_MODE_DRV) {

        ar6000_update_bdaddr(ar);
    }

    if (enablerssicompensation) {
       ar6000_copy_cust_data_from_target(ar->arHifDevice, ar->arTargetType);
       read_rssi_compensation_param(ar);
       for(j=0; j<num_device; j++) {
           for (i=-95; i<=0; i++) {
               rssi_compensation_table[j][0-i] = rssi_compensation_calc(ar->arDev[j],i);
           }
       }
    }

   /* Do we need to finish the BMI phase */

    if ((wlaninitmode==WLAN_INIT_MODE_USR || wlaninitmode==WLAN_INIT_MODE_DRV) &&
       (BMIDone(ar->arHifDevice) != A_OK))
    {
        ret = -EIO;
        goto ar6000_init_done;
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
        status = ar6000_connectservice(arPriv,
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

        status = ar6000_connectservice(arPriv,
                                       &connect,
                                       "WMI DATA BE");
        if (A_FAILED(status)) {
            break;
        }

            /* connect to back-ground
             * map this to WMI LOW_PRI */
        connect.ServiceID = WMI_DATA_BK_SVC;
        status = ar6000_connectservice(arPriv,
                                       &connect,
                                       "WMI DATA BK");
        if (A_FAILED(status)) {
            break;
        }

            /* connect to Video service, map this to
             * to HI PRI */
        connect.ServiceID = WMI_DATA_VI_SVC;
        status = ar6000_connectservice(arPriv,
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
        status = ar6000_connectservice(arPriv,
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
        for(i = 0; i < num_device; i++)
        {
            if (ar->arDev[i]->arWmiEnabled == TRUE) {
                wmi_shutdown(ar->arDev[i]->arWmi);
                ar->arDev[i]->arWmiEnabled = FALSE;
                ar->arDev[i]->arWmi = NULL;
            }
        }
        ar6000_cookie_cleanup(ar);
        ret = -EIO;
        goto ar6000_init_done;
    }

    if (!bypasswmi) {
        /* Wait for Wmi event to be ready */
        timeleft = wait_event_interruptible_timeout(ar->arDev[0]->arEvent,
            (ar->arWmiReady == TRUE), wmitimeout * HZ);

        if (ar->arVersion.abi_ver != AR6K_ABI_VERSION) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ABI Version mismatch: Host(0x%x), Target(0x%x)\n", AR6K_ABI_VERSION, ar->arVersion.abi_ver));
#ifndef ATH6KL_SKIP_ABI_VERSION_CHECK
            ret = -EIO;
            goto ar6000_init_done;
#endif /* ATH6KL_SKIP_ABI_VERSION_CHECK */
        }

        if(!timeleft || signal_pending(current))
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("WMI is not ready or wait was interrupted\n"));
            ret = -EIO;
            goto ar6000_init_done;
        }

        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("%s() WMI is ready\n", __func__));

    /* init PAL driver after WMI is ready */

        if(setuphcipal) {
            A_BOOL bt30ampDevFound = FALSE;
            for (i=0; i < num_device; i++) {
                if ( ar->arDev[i]->isBt30amp == TRUE ) {
                    status = ar6k_setup_hci_pal(ar->arDev[i]);
                    bt30ampDevFound = TRUE;
                }

            }
        }

        /* Communicate the wmi protocol verision to the target */
        if ((ar6000_set_host_app_area(ar)) != A_OK) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Unable to set the host app area\n"));
        }

        /* configure the device for rx dot11 header rules 0,0 are the default values
         * therefore this command can be skipped if the inputs are 0,FALSE,FALSE.Required
         if checksum offload is needed. Set RxMetaVersion to 2*/
        if ((wmi_set_rx_frame_format_cmd(arPriv->arWmi,ar->rxMetaVersion, processDot11Hdr, processDot11Hdr)) != A_OK) {
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

        if ((wmi_set_btcoex_colocated_bt_dev_cmd(arPriv->arWmi, &sbcb_cmd)) != A_OK)
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

        if ((wmi_set_btcoex_fe_ant_cmd(arPriv->arWmi, &sbfa_cmd)) != A_OK) {
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)devt;

    arPriv->arBitRate = rateKbps;
    wake_up(&arPriv->arEvent);
}

void
ar6000_ratemask_rx(void *devt, A_UINT32 *ratemask)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)devt;

    arPriv->arRateMask[0] = ratemask[0];
    arPriv->arRateMask[1] = ratemask[1];
    wake_up(&arPriv->arEvent);
}

void
ar6000_txPwr_rx(void *devt, A_UINT8 txPwr)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)devt;

    arPriv->arTxPwr = txPwr;
    wake_up(&arPriv->arEvent);
}


void
ar6000_channelList_rx(void *devt, A_INT8 numChan, A_UINT16 *chanList)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)devt;

    A_MEMCPY(arPriv->arSta.arChannelList, chanList, numChan * sizeof (A_UINT16));
    arPriv->arSta.arNumChannels = numChan;

    wake_up(&arPriv->arEvent);
}

A_UINT8
ar6000_ibss_map_epid(struct sk_buff *skb, struct net_device *dev, A_UINT32 * mapNo)
{
    AR_SOFTC_DEV_T  *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_STA_T  *arSta  = &arPriv->arSta;
    AR_SOFTC_T      *ar     = arPriv->arSoftc;
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
    for (i = 0; i < arSta->arNodeNum; i ++) {
        if (IEEE80211_ADDR_EQ(macHdr->dstMac, arSta->arNodeMap[i].macAddress)) {
            (*mapNo) = i + 1;
            arSta->arNodeMap[i].txPending ++;
            return arSta->arNodeMap[i].epId;
        }

        if ((eptMap == -1) && !arSta->arNodeMap[i].txPending) {
            eptMap = i;
        }
    }

    if (eptMap == -1) {
        eptMap = arSta->arNodeNum;
        arSta->arNodeNum ++;
        A_ASSERT(arSta->arNodeNum <= MAX_NODE_NUM);
    }

    A_MEMCPY(arSta->arNodeMap[eptMap].macAddress, macHdr->dstMac, IEEE80211_ADDR_LEN);

    for (i = ENDPOINT_2; i <= ENDPOINT_5; i ++) {
        if (!ar->arTxPending[i]) {
            arSta->arNodeMap[eptMap].epId = i;
            break;
        }
        // No free endpoint is available, start redistribution on the inuse endpoints.
        if (i == ENDPOINT_5) {
            arSta->arNodeMap[eptMap].epId = arSta->arNexEpId;
            arSta->arNexEpId ++;
            if (arSta->arNexEpId > ENDPOINT_5) {
                arSta->arNexEpId = ENDPOINT_2;
            }
        }
    }

    (*mapNo) = eptMap + 1;
    arSta->arNodeMap[eptMap].txPending ++;

    return arSta->arNodeMap[eptMap].epId;
}

#ifdef DEBUG
static void ar6000_dump_skb(struct sk_buff *skb)
{
   u_char *ch;
   for (ch = A_NETBUF_DATA(skb);
        (A_UINT32)ch < ((A_UINT32)A_NETBUF_DATA(skb) +
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

void
check_addba_state(AR_SOFTC_DEV_T *arPriv, struct sk_buff *skb, conn_t *conn)
{
    A_UINT8         *datap;
    A_UINT8         tid = 0;
    WMI_DATA_HDR    *dtHdr;

    /* Perform ADDBA if required */
    if(conn && (conn->wmode == MODE_11NG_HT20)) {
        datap = A_NETBUF_DATA(skb);
        dtHdr = (WMI_DATA_HDR *)datap;
        tid = WMI_DATA_HDR_GET_UP(dtHdr);

        if(conn->ba_state[tid] < 3) {
            wmi_setup_aggr_cmd(arPriv->arWmi, tid | (conn->aid << 4));
            conn->ba_state[tid]++;
        }
    }
}

static int
ar6000_data_tx(struct sk_buff *skb, struct net_device *dev)
{
#define AC_NOT_MAPPED   99
    AR_SOFTC_DEV_T     *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T         *ar = arPriv->arSoftc;
    AR_SOFTC_STA_T     *arSta = &arPriv->arSta;
    AR_SOFTC_AP_T      *arAp  = &arPriv->arAp;
    A_UINT8            ac = AC_NOT_MAPPED;
    HTC_ENDPOINT_ID    eid = ENDPOINT_UNUSED;
    A_UINT32          mapNo = 0;
    int               len;
    struct ar_cookie *cookie;
    A_BOOL            checkAdHocPsMapping = FALSE;
    HTC_TX_TAG        htc_tag = AR6K_DATA_PKT_TAG;
    A_UINT8           dot11Hdr = processDot11Hdr;
    A_UINT8           check_addba = 0;
    conn_t            *conn = NULL;
    A_UINT32          wmiDataFlags = 0;

#ifdef AR6K_ALLOC_DEBUG
    A_NETBUF_CHECK(skb);
#endif

#ifdef CONFIG_PM
    if ((ar->arWowState != WLAN_WOW_STATE_NONE) || (ar->arWlanState == WLAN_DISABLED)) {
        A_NETBUF_FREE(skb);
        return 0;
    }
#endif /* CONFIG_PM */
#if LINUX_VERSION_CODE <= KERNEL_VERSION(2,6,13)
    skb->list = NULL;
#endif

    AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_TX,("ar6000_data_tx start - skb=0x%x, data=0x%x, len=0x%x\n",
                     (A_UINT32)skb, (A_UINT32)A_NETBUF_DATA(skb),
                     A_NETBUF_LEN(skb)));

    /* If target is not associated */
    if( (!arPriv->arConnected && !bypasswmi)
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

        if (arPriv->arNetworkType == AP_NETWORK) {
            ATH_MAC_HDR *datap = (ATH_MAC_HDR *)A_NETBUF_DATA(skb);

            /* If the dstMac is a Multicast address & atleast one of the
             * associated STA is in PS mode, then queue the pkt to the
             * mcastq
             */
            if (IEEE80211_IS_MULTICAST(datap->dstMac)) {
                A_UINT8 ctr=0;
                A_BOOL qMcast=FALSE;

                for (ctr=0; ctr<NUM_CONN; ctr++) {
                    if(ar->connTbl[ctr].arPriv == arPriv) {
                        if (STA_IS_PWR_SLEEP((&ar->connTbl[ctr]))) {
                            qMcast = TRUE;
                        }
                    }
                }
                if(qMcast) {
                    /* If this transmit is not because of a Dtim Expiry q it */
                    if (arAp->DTIMExpired == FALSE) {
                        A_BOOL isMcastqEmpty = FALSE;

                        A_MUTEX_LOCK(&arAp->mcastpsqLock);
                        isMcastqEmpty = A_NETBUF_QUEUE_EMPTY(&arAp->mcastpsq);
                        A_NETBUF_ENQUEUE(&arAp->mcastpsq, skb);
                        A_MUTEX_UNLOCK(&arAp->mcastpsqLock);

                        /* If this is the first Mcast pkt getting queued
                         * indicate to the target to set the BitmapControl LSB
                         * of the TIM IE.
                         */
                        if (isMcastqEmpty) {
                             wmi_set_pvb_cmd(arPriv->arWmi, MCAST_AID, 1);
                        }
                        return 0;
                    } else {
                     /* This transmit is because of Dtim expiry. Determine if
                      * MoreData bit has to be set.
                      */
                         A_MUTEX_LOCK(&arAp->mcastpsqLock);
                         if(!A_NETBUF_QUEUE_EMPTY(&arAp->mcastpsq)) {
                            wmiDataFlags |= WMI_DATA_HDR_FLAGS_MORE;
                         }
                         A_MUTEX_UNLOCK(&arAp->mcastpsqLock);
                    }
                }
            } else {
                conn = ieee80211_find_conn(arPriv, datap->dstMac);
                if (conn) {
                    if (STA_IS_PWR_SLEEP(conn)) {
                        /* If this transmit is not because of a PsPoll q it*/
                        if (!(STA_IS_PS_POLLED(conn) || STA_IS_APSD_TRIGGER(conn))) {
                            A_BOOL trigger = FALSE;

                            if (conn->apsd_info) {
                                A_UINT8 up = 0;
                                A_UINT8 trafficClass;

                                if (arPriv->arWmmEnabled) {
                                    A_UINT16 ipType = IP_ETHERTYPE;
                                    A_UINT16 etherType;
                                    A_UINT8  *ipHdr;

                                    etherType = datap->typeOrLen;
                                    if (IS_ETHERTYPE(A_BE2CPU16(etherType))) {
                                        /* packet is in DIX format  */
                                        ipHdr = (A_UINT8 *)(datap + 1);
                                    } else {
                                        /* packet is in 802.3 format */
                                        ATH_LLC_SNAP_HDR    *llcHdr;

                                        llcHdr = (ATH_LLC_SNAP_HDR *)(datap + 1);
                                        etherType = llcHdr->etherType;
                                        ipHdr = (A_UINT8 *)(llcHdr + 1);
                                    }

                                    if (etherType == A_CPU2BE16(ipType)) {
                                        up = wmi_determine_userPriority (ipHdr, 0);
                                    }
                                }
                                trafficClass = convert_userPriority_to_trafficClass(up);
                                if (conn->apsd_info & (1 << trafficClass)) {
                                    trigger = TRUE;
                                }
                            }

                            if (trigger) {
                                A_BOOL isApsdqEmpty;
                                /* Queue the frames if the STA is sleeping */
                                A_MUTEX_LOCK(&conn->psqLock);
                                isApsdqEmpty = A_NETBUF_QUEUE_EMPTY(&conn->apsdq);
                                A_NETBUF_ENQUEUE(&conn->apsdq, skb);
                                A_MUTEX_UNLOCK(&conn->psqLock);

                                /* If this is the first pkt getting queued
                                * for this STA, update the PVB for this STA
                                */
                                if (isApsdqEmpty) {
                                    wmi_set_apsd_buffered_traffic_cmd(arPriv->arWmi, conn->aid, 1, 0);
                                }
                            } else {
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
                                    wmi_set_pvb_cmd(arPriv->arWmi, conn->aid, 1);
                                }
                            }

                            return 0;
                        } else {
                         /*
                          * This tx is because of a PsPoll or trigger. Determine if
                          * MoreData bit has to be set
                          */
                            A_MUTEX_LOCK(&conn->psqLock);
                            if (STA_IS_PS_POLLED(conn)) {
                                if (!A_NETBUF_QUEUE_EMPTY(&conn->psq)) {
                                    wmiDataFlags |= WMI_DATA_HDR_FLAGS_MORE;
                                }
                            } else {
                                /*
                                 * This tx is because of a uAPSD trigger, determine
                                 * more and EOSP bit. Set EOSP is queue is empty
                                 * or sufficient frames is delivered for this trigger
                                 */
                                if (!A_NETBUF_QUEUE_EMPTY(&conn->apsdq)) {
                                    wmiDataFlags |= WMI_DATA_HDR_FLAGS_MORE;
                                }
                                if (STA_IS_APSD_EOSP(conn)) {
                                    wmiDataFlags |= WMI_DATA_HDR_FLAGS_EOSP;
                                }
                            }
                            A_MUTEX_UNLOCK(&conn->psqLock);
                         }
                    }
                    check_addba = 1;
                } else {

                    /* non existent STA. drop the frame */
                    A_NETBUF_FREE(skb);
                    return 0;
                }
            }
        }

        if (arPriv->arWmiEnabled) {
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
                if (wmi_dot11_hdr_add(arPriv->arWmi,skb,arPriv->arNetworkType) != A_OK) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_data_tx-wmi_dot11_hdr_add failed\n"));
                    break;
                }
            } else {
                if (wmi_dix_2_dot3(arPriv->arWmi, skb) != A_OK) {
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
                if (wmi_data_hdr_add(arPriv->arWmi, skb, DATA_MSGTYPE, wmiDataFlags, dot11Hdr,
                                        WMI_META_VERSION_2,&metaV2) != A_OK) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_data_tx - wmi_data_hdr_add failed\n"));
                    break;
                }

            }
            else
#endif
            {
                if (wmi_data_hdr_add(arPriv->arWmi, skb, DATA_MSGTYPE, wmiDataFlags, dot11Hdr,0,NULL) != A_OK) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("ar6000_data_tx - wmi_data_hdr_add failed\n"));
                    break;
                }
            }


            if ((arPriv->arNetworkType == ADHOC_NETWORK) &&
                arSta->arIbssPsEnable && arPriv->arConnected) {
                    /* flag to check adhoc mapping once we take the lock below: */
                checkAdHocPsMapping = TRUE;

            } else {
                    /* get the stream mapping */
                ac  =  wmi_implicit_create_pstream(arPriv->arWmi, skb, 0, arPriv->arWmmEnabled);
                if(check_addba && conn) {
                    check_addba_state(arPriv, skb, conn);
                }
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
        AR6000_STAT_INC(arPriv, tx_dropped);
        AR6000_STAT_INC(arPriv, tx_aborted_errors);
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
        cookie->arc_bp[0] = (A_UINT32)skb;
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
        AR6000_STAT_INC(arPriv, tx_dropped);
        AR6000_STAT_INC(arPriv, tx_aborted_errors);
    }

    return 0;
}

int
ar6000_acl_data_tx(struct sk_buff *skb, AR_SOFTC_DEV_T *arPriv)
{
    struct ar_cookie *cookie;
    AR_SOFTC_T *ar = arPriv->arSoftc;
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
            cookie->arc_bp[0] = (A_UINT32)skb;
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
            AR6000_STAT_INC(arPriv, tx_dropped);
            AR6000_STAT_INC(arPriv, tx_aborted_errors);
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
applyAPTCHeuristics(AR_SOFTC_DEV_T *arPriv)
{
    A_UINT32 duration;
    A_UINT32 numbytes;
    A_UINT32 throughput;
    struct timeval ts;
    A_STATUS status;
    APTC_TRAFFIC_RECORD *aptcTR;
    AR_SOFTC_T   *ar = arPriv->arSoftc;

    aptcTR = arPriv->aptcTR;

    AR6000_SPIN_LOCK(&arPriv->arPrivLock, 0);

    if ((enableAPTCHeuristics) && (!aptcTR->timerScheduled)) {
        do_gettimeofday(&ts);
        tvsub(&ts, &aptcTR->samplingTS);
        duration = ts.tv_sec * 1000 + ts.tv_usec / 1000; /* ms */
        numbytes = aptcTR->bytesTransmitted + aptcTR->bytesReceived;

        if (duration > APTC_TRAFFIC_SAMPLING_INTERVAL) {
            /* Initialize the time stamp and byte count */
            aptcTR->bytesTransmitted = aptcTR->bytesReceived = 0;
            do_gettimeofday(&aptcTR->samplingTS);

            /* Calculate and decide based on throughput thresholds */
            throughput = ((numbytes * 8) / duration);
            if (throughput > APTC_UPPER_THROUGHPUT_THRESHOLD) {
                /* Disable Sleep and schedule a timer */
                A_ASSERT(ar->arWmiReady == TRUE);
                AR6000_SPIN_UNLOCK(&arPriv->ariPrivLock, 0);
                status = wmi_powermode_cmd(arPriv->arWmi, MAX_PERF_POWER);
                AR6000_SPIN_LOCK(&arPriv->arPrivLock, 0);
                A_TIMEOUT_MS(&aptcTimer[arPriv->arDeviceIndex], APTC_TRAFFIC_SAMPLING_INTERVAL, 0);
                aptcTR->timerScheduled = TRUE;
            }
        }
    }

    AR6000_SPIN_UNLOCK(&arPriv->arLock, 0);
}
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */

static HTC_SEND_FULL_ACTION ar6000_tx_queue_full(void *Context, HTC_PACKET *pPacket)
{
    AR_SOFTC_T     *ar = (AR_SOFTC_T *)Context;
    HTC_SEND_FULL_ACTION    action = HTC_SEND_FULL_KEEP;
    A_BOOL                  stopNet = FALSE;
    HTC_ENDPOINT_ID         Endpoint = HTC_GET_ENDPOINT_FROM_PKT(pPacket);
    A_UINT8   i;
    AR_SOFTC_DEV_T  *arPriv;

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
        for(i = 0; i < num_device; i++)
        {
            arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(ar6000_devices[i]);
            AR6000_SPIN_LOCK(&arPriv->arPrivLock, 0);
            arPriv->arNetQueueStopped = TRUE;
            AR6000_SPIN_UNLOCK(&arPriv->arPrivLock, 0);
            /* one of the data endpoints queues is getting full..need to stop network stack
             * the queue will resume in ar6000_tx_complete() */
            netif_stop_queue(ar6000_devices[i]);
        }
    }
    else
    {
        /* in adhoc mode, we cannot differentiate traffic priorities so there is no need to
        * continue, however we should stop the network */
        for(i = 0; i < num_device; i++)
        {
            arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(ar6000_devices[i]);
            if(arPriv->arNetworkType == ADHOC_NETWORK) {
                AR6000_SPIN_LOCK(&arPriv->arPrivLock, 0);
                arPriv->arNetQueueStopped = TRUE;
                AR6000_SPIN_UNLOCK(&arPriv->arPrivLock, 0);
                /* one of the data endpoints queues is getting full..need to stop network stack
                * the queue will resume in ar6000_tx_complete() */
                netif_stop_queue(ar6000_devices[i]);
           }
        }
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
    A_BOOL          flushing[NUM_DEV];
    A_INT8         devid = -1;
    AR_SOFTC_DEV_T  *arPriv = NULL;
    AR_SOFTC_STA_T  *arSta;
    A_UINT8 i;

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

        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_TX,("ar6000_tx_complete skb=0x%x data=0x%x len=0x%x eid=%d ",
                         (A_UINT32)pktSkb, (A_UINT32)pPacket->pBuffer,
                         pPacket->ActualLength,
                         eid));

        ar->arTxPending[eid]--;

        if(!bypasswmi)
        {

            if (eid  != ar->arControlEp) {
                WMI_DATA_HDR *dhdr = (WMI_DATA_HDR *)A_NETBUF_DATA(pktSkb);
                ar->arTotalTxDataPending--;
                devid = WMI_DATA_HDR_GET_DEVID(dhdr);
                arPriv = ar->arDev[devid];
            }

            if (eid == ar->arControlEp)
            {
                WMI_CMD_HDR *cmhdr = (WMI_CMD_HDR*)A_NETBUF_DATA(pktSkb);
                if (ar->arWMIControlEpFull) {
                        /* since this packet completed, the WMI EP is no longer full */
                    ar->arWMIControlEpFull = FALSE;
#ifdef ANDROID_ENV
                    android_epfull_cnt = 0;
#endif
                }

                if (ar->arTxPending[eid] == 0) {
                    wakeEvent = TRUE;
                }
                devid = WMI_CMD_HDR_GET_DEVID(cmhdr);
                arPriv = ar->arDev[devid];
            }
        }
        else
        {
            devid = 0;
            arPriv = ar->arDev[devid];
        }


        if (A_FAILED(status)) {
            if (status == A_ECANCELED) {
                    /* a packet was flushed  */
                flushing[devid] = TRUE;
            }
            AR6000_STAT_INC(arPriv, tx_errors);
            if (status != A_NO_RESOURCE) {
                AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s() -TX ERROR, status: 0x%x\n", __func__,
                            status));
            }
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_TX,("OK\n"));
            flushing[devid] = FALSE;
            AR6000_STAT_INC(arPriv, tx_packets);
            arPriv->arNetStats.tx_bytes += A_NETBUF_LEN(pktSkb);
#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
            arPriv->aptcTR.bytesTransmitted += a_netbuf_to_len(pktSkb);
            applyAPTCHeuristics(arPriv);
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */
        }

        // TODO this needs to be looked at
        if (arPriv->arNetworkType == ADHOC_NETWORK)
        {
            arSta = &arPriv->arSta;
            if((arSta->arIbssPsEnable && (eid != ar->arControlEp) && mapNo))
            {
                 mapNo --;
                 arSta->arNodeMap[mapNo].txPending --;

                 if (!arSta->arNodeMap[mapNo].txPending && (mapNo == (arSta->arNodeNum - 1))) {
                     A_UINT32 i;
                     for (i = arSta->arNodeNum; i > 0; i --) {
                         if (!arSta->arNodeMap[i - 1].txPending) {
                             A_MEMZERO(&arSta->arNodeMap[i - 1], sizeof(struct ar_node_mapping));
                             arSta->arNodeNum --;
                         } else {
                             break;
                         }
                     }
                 }
            }
        }

        ar6000_free_cookie(ar, ar_cookie);

        if (arPriv->arNetQueueStopped) {
            arPriv->arNetQueueStopped = FALSE;
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
    for(i = 0; i < num_device; i++) {
        arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(ar6000_devices[i]);
        if (((arPriv->arNetworkType == INFRA_NETWORK ) && (arPriv->arConnected == TRUE))
                || (bypasswmi)) {
            if (!flushing[i]) {
                /* don't wake the queue if we are flushing, other wise it will just
                 * keep queueing packets, which will keep failing */

                netif_wake_queue(arPriv->arNetDev);
            }
        }

        if (wakeEvent) {
            wake_up(&arPriv->arEvent);
        }
    }

}

conn_t *
ieee80211_find_conn(AR_SOFTC_DEV_T *arPriv, A_UINT8 *node_addr)
{
    conn_t *conn = NULL;
    A_UINT8 i;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (IS_MAC_NULL(node_addr)) {
        return NULL;
    }

    for (i = 0; i < NUM_CONN; i++) {
        if (IEEE80211_ADDR_EQ(node_addr, ar->connTbl[i].mac)) {
            conn = &ar->connTbl[i];
            break;
        }
    }

    return conn;
}

conn_t *ieee80211_find_conn_for_aid(AR_SOFTC_DEV_T *arPriv, A_UINT8 aid)
{
    conn_t *conn = NULL;
    AR_SOFTC_T *ar = arPriv->arSoftc;

    if (arPriv->arNetworkType != AP_NETWORK) {
        conn = NULL;
    } else if( (aid > 0) && (aid < NUM_CONN) ) {
        if (ar->connTbl[aid-1].aid == aid) {
            conn = &ar->connTbl[aid-1];
        }
    }
    return conn;
}

void *get_aggr_ctx(AR_SOFTC_DEV_T *arPriv, conn_t *conn)
{
    if (arPriv->arNetworkType != AP_NETWORK) {
        return (arPriv->conn_aggr);
    } else {
        return (conn->conn_aggr);
    }
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
    conn_t *conn = NULL;
    AR_SOFTC_DEV_T *arPriv = NULL;
    A_UINT8        devid ;
    ATH_MAC_HDR *multicastcheck_datap = NULL;

    A_ASSERT((status != A_OK) ||
             (pPacket->pBuffer == (A_NETBUF_DATA(skb) + HTC_HEADER_LEN)));

    AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_RX,("ar6000_rx ar=0x%x eid=%d, skb=0x%x, data=0x%x, len=0x%x status:%d",
                    (A_UINT32)ar, ept, (A_UINT32)skb, (A_UINT32)pPacket->pBuffer,
                    pPacket->ActualLength, status));

    if (status != A_OK) {
        if (status != A_ECANCELED) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("RX ERR (%d) \n",status));
        }
        A_NETBUF_FREE(skb);
        goto rx_done;
    }

        /* take lock to protect buffer counts
         * and adaptive power throughput state */
    AR6000_SPIN_LOCK(&ar->arLock, 0);
    A_NETBUF_PUT(skb, pPacket->ActualLength +  HTC_HEADER_LEN);
    A_NETBUF_PULL(skb, HTC_HEADER_LEN);

    if(!bypasswmi)
    {
        if(ept == ar->arControlEp) {
             WMI_CMD_HDR *cmhdr = (WMI_CMD_HDR*)A_NETBUF_DATA(skb);
             devid = WMI_CMD_HDR_GET_DEVID(cmhdr);
             arPriv = ar->arDev[devid];
        }
        else {
             WMI_DATA_HDR *dhdr = (WMI_DATA_HDR *)A_NETBUF_DATA(skb);
             devid = WMI_DATA_HDR_GET_DEVID(dhdr);
             arPriv = ar->arDev[devid];
        }
    }
    else
    {
        devid = 0;
        arPriv = ar->arDev[devid];
    }

    if (A_SUCCESS(status)) {
        AR6000_STAT_INC(arPriv, rx_packets);
        arPriv->arNetStats.rx_bytes += pPacket->ActualLength;
#ifdef ADAPTIVE_POWER_THROUGHPUT_CONTROL
        arPriv->aptcTR.bytesReceived += pPacket->ActualLength;
        applyAPTCHeuristics(arPriv);
#endif /* ADAPTIVE_POWER_THROUGHPUT_CONTROL */


#ifdef DEBUG
        if (debugdriver >= 2) {
            ar6000_dump_skb(skb);
        }
#endif /* DEBUG */
    }

    AR6000_SPIN_UNLOCK(&ar->arLock, 0);

    skb->dev = arPriv->arNetDev;
    if (status != A_OK) {
        AR6000_STAT_INC(arPriv, rx_errors);
        A_NETBUF_FREE(skb);
    } else if (arPriv->arWmiEnabled == TRUE) {
        if (ept == ar->arControlEp) {
           /*
            * this is a wmi control msg
            */
#ifdef CONFIG_PM
            ar6000_check_wow_status(ar, skb, TRUE);
#endif /* CONFIG_PM */
            wmi_control_rx(arPriv->arWmi, skb);
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
                if (arPriv->arNetworkType != AP_NETWORK &&  !is_acl_data_frame &&
                    ((pPacket->ActualLength < minHdrLen) ||
                    (pPacket->ActualLength > AR6000_MAX_RX_MESSAGE_SIZE)))
                {
                    /*
                     * packet is too short or too long
                     */
                    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("TOO SHORT or TOO LONG\n"));
                    AR6000_STAT_INC(arPriv, rx_errors);
                    AR6000_STAT_INC(arPriv, rx_length_errors);
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
                    if (arPriv->arNetworkType == AP_NETWORK) {
                        A_UINT8 psState=0,prevPsState;
                        ATH_MAC_HDR *datap=NULL;
                        A_UINT16 offset;
                        A_UINT8  triggerState;

                        meta_type = WMI_DATA_HDR_GET_META(dhdr);

                        psState = (((WMI_DATA_HDR *)A_NETBUF_DATA(skb))->info
                                     >> WMI_DATA_HDR_PS_SHIFT) & WMI_DATA_HDR_PS_MASK;
                        triggerState = WMI_DATA_HDR_IS_TRIGGER(dhdr);


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
#ifdef DIX_RX_OFFLOAD
#define SKIP_LLC_LEN 8
                       /*DIX to ETHERNET hdr conversion is offloaded to firmware */
                       /*Empty LLC header is moved to get ethernet header*/
                        A_UINT32 datalen = (A_UINT32)A_NETBUF_LEN(skb)-offset;

                        is_amsdu = WMI_DATA_HDR_IS_AMSDU(dhdr);
                        containsDot11Hdr = WMI_DATA_HDR_GET_DOT11(dhdr);
                        if(!containsDot11Hdr && !is_amsdu && !is_acl_data_frame
                            && datalen >= (sizeof(ATH_MAC_HDR) + sizeof(ATH_LLC_SNAP_HDR))) {
                            datap = (ATH_MAC_HDR *)((A_INT8*)A_NETBUF_DATA(skb)+offset+SKIP_LLC_LEN);
                        }
                        else {
                            datap = (ATH_MAC_HDR *)((A_INT8*)A_NETBUF_DATA(skb)+offset);
                        }

#else
                        datap = (ATH_MAC_HDR *)(A_NETBUF_DATA(skb)+offset);
#endif
                        conn = ieee80211_find_conn(arPriv, datap->srcMac);

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

                            if (STA_IS_PWR_SLEEP(conn)) {
                                /* Accept trigger only when the station is in sleep */
                                if (triggerState) {
                                    ar6000_uapsd_trigger_frame_rx(arPriv, conn);
                                }
                            }

                            if (prevPsState ^ STA_IS_PWR_SLEEP(conn)) {
                                A_BOOL isApsdqEmptyAtStart;

                                if (!STA_IS_PWR_SLEEP(conn)) {

                                    A_MUTEX_LOCK(&conn->psqLock);

                                    while (!A_NETBUF_QUEUE_EMPTY(&conn->psq)) {
                                        struct sk_buff *skb=NULL;

                                        skb = A_NETBUF_DEQUEUE(&conn->psq);
                                        A_MUTEX_UNLOCK(&conn->psqLock);
                                        ar6000_data_tx(skb,arPriv->arNetDev);
                                        A_MUTEX_LOCK(&conn->psqLock);
                                    }

                                    isApsdqEmptyAtStart  = A_NETBUF_QUEUE_EMPTY(&conn->apsdq);

                                    while (!A_NETBUF_QUEUE_EMPTY(&conn->apsdq)) {
                                        struct sk_buff *skb=NULL;

                                        skb = A_NETBUF_DEQUEUE(&conn->apsdq);
                                        A_MUTEX_UNLOCK(&conn->psqLock);
                                        ar6000_data_tx(skb,arPriv->arNetDev);
                                        A_MUTEX_LOCK(&conn->psqLock);
                                    }

                                    A_MUTEX_UNLOCK(&conn->psqLock);

                                    /* Clear the APSD buffered bitmap for this STA */
                                    if (!isApsdqEmptyAtStart) {
                                        wmi_set_apsd_buffered_traffic_cmd(arPriv->arWmi, conn->aid, 0, 0);
                                    }

                                    /* Clear the PVB for this STA */
                                    wmi_set_pvb_cmd(arPriv->arWmi, conn->aid, 0);
                                }
                            }

                        } else {
                            /* This frame is from a STA that is not associated*/
                            A_NETBUF_FREE(skb);
                            goto rx_done;
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

                    wmi_data_hdr_remove(arPriv->arWmi, skb);

                    switch (meta_type) {
                        case WMI_META_VERSION_1:
                            {
                                WMI_RX_META_V1 *pMeta = (WMI_RX_META_V1 *)A_NETBUF_DATA(skb);
                                AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("META %d %d %d %d %x\n", pMeta->status, pMeta->rix, pMeta->rssi, pMeta->channel, pMeta->flags));
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
                        status = wmi_dot11_hdr_remove(arPriv->arWmi,skb);
                    } else if(!is_amsdu && !is_acl_data_frame) {
#ifdef DIX_RX_OFFLOAD
                        /*Skip the conversion its offloaded to firmware*/
                        if(A_NETBUF_PULL(skb, sizeof(ATH_LLC_SNAP_HDR)) != A_OK) {
                            status = A_NO_MEMORY;
                        }
                        else {
                            status = A_OK;
                        }
#else
                        status = wmi_dot3_2_dix(skb);
#endif
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
                            if((*ar6k_pal_config_g.fpar6k_pal_recv_pkt)(arPriv->hcipal_info, skb) == TRUE)
                            goto rx_done;
                        }
                    }
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
                    /*
                     * extra push and memcpy, for eth_type_trans() of 2.4 kernel
                     * will pull out hard_header_len bytes of the skb.
                     */
                    A_NETBUF_PUSH(skb, sizeof(WMI_DATA_HDR) + sizeof(ATH_LLC_SNAP_HDR) + HTC_HEADER_LEN);
                    A_MEMCPY(A_NETBUF_DATA(skb), A_NETBUF_DATA(skb) + sizeof(WMI_DATA_HDR) +
                             sizeof(ATH_LLC_SNAP_HDR) + HTC_HEADER_LEN, sizeof(ATH_MAC_HDR));
#endif

#ifdef ATH_AR6K_11N_SUPPORT
                multicastcheck_datap = (ATH_MAC_HDR *)A_NETBUF_DATA(skb);

                if ((!(IEEE80211_IS_MULTICAST(multicastcheck_datap->dstMac))) && (arPriv->arNetworkType != AP_NETWORK)){ 
                    aggr_process_recv_frm(get_aggr_ctx(arPriv, conn), tid, seq_no, is_amsdu, (void **)&skb);
                 } 
#endif
                    ar6000_deliver_frames_to_nw_stack((void *) arPriv->arNetDev, (void *)skb);
                }
            }
    } else {
        if (EPPING_ALIGNMENT_PAD > 0) {
            A_NETBUF_PULL(skb, EPPING_ALIGNMENT_PAD);
        }
        ar6000_deliver_frames_to_nw_stack((void *)arPriv->arNetDev, (void *)skb);
    }

rx_done:

    return;
}

static void
ar6000_deliver_frames_to_nw_stack(void *dev, void *osbuf)
{
    struct sk_buff *skb = (struct sk_buff *)osbuf;
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;

    if(skb) {
        skb->dev = dev;
        if ((skb->dev->flags & IFF_UP) == IFF_UP) {

            if (arPriv->arNetworkType == AP_NETWORK) {

                struct sk_buff *skb1 = NULL;
                ATH_MAC_HDR *datap;
                struct net_device *net_dev = arPriv->arNetDev;

#ifdef CONFIG_PM
                ar6000_check_wow_status(ar, skb, FALSE);
#endif /* CONFIG_PM */


                datap = (ATH_MAC_HDR *)A_NETBUF_DATA(skb);

                if (IEEE80211_IS_MULTICAST(datap->dstMac)) {

                    /* Bcast/Mcast frames should be sent to the OS
                     * stack as well as on the air.
                     */
                    skb1 = skb_copy(skb,GFP_ATOMIC);
                } else {

                    /* Search for a connected STA with dstMac as
                     * the Mac address. If found send the frame to
                     * it on the air else send the frame up the stack
                     */

                    AR_SOFTC_DEV_T *to_arPriv = NULL;
                    A_UINT8 is_forward = 0;
                    conn_t *to_conn = NULL;

                    to_conn = ieee80211_find_conn(arPriv, datap->dstMac);

                    if (to_conn) {
                        to_arPriv   = (AR_SOFTC_DEV_T *)to_conn->arPriv;
                        /* Forward data within BSS */
                        if(arPriv == to_arPriv) {
                            is_forward = arPriv->arAp.intra_bss;
                        } else {
                            /* Forward data within mBSS */
                            is_forward = ar->inter_bss;
                            net_dev = to_arPriv->arNetDev;
                        }
                         if(is_forward && net_dev) {
                            skb1 = skb;
                            skb = NULL;
                        } else {
                            A_NETBUF_FREE(skb);
                            skb = NULL;
                            return;
                        }
                    }

                }

                if (skb1) {
                    ar6000_data_tx(skb1, net_dev);
                    if (!skb)
                        return;
                }
            }

#ifdef CONFIG_PM
            ar6000_check_wow_status(ar, skb, FALSE);
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
                A_NETIF_RX(skb);
            } else {
                A_NETIF_RX_NI(skb);
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
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 35)
    int mc_count = dev->mc_count;
    struct dev_mc_list *mc;
    int j;
#else
    int mc_count = netdev_mc_count(dev);
    struct netdev_hw_addr *ha;
#endif
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
        
    int i;
    A_BOOL enableAll, disableAll;
    enum {
        IGNORE = 0,
        MATCH = 1,
        ADD = 2,
        DELETE = 3
    } action[MAC_MAX_FILTERS_PER_LIST];
    A_BOOL mcValid;
    A_UINT8 *mac;
    A_UINT8 *filter;
    A_BOOL filterValid;

    if (ar->arWmiReady == FALSE || ar->arWlanState == WLAN_DISABLED)
        return;

    enableAll = FALSE;
    disableAll = FALSE;

    /*
     *  Enable receive all multicast, if
     * 1. promiscous mode,
     * 2. Allow all multicast
     * 3. H/W supported filters is less than application requested filter
     */
    if ((dev->flags & IFF_PROMISC) ||
        (dev->flags & IFF_ALLMULTI) ||
        (mc_count > MAC_MAX_FILTERS_PER_LIST))
    {
        enableAll = TRUE;
    } else {
        /* Disable all multicast if interface has multicast disable or list is empty */
        if ((!(dev->flags & IFF_MULTICAST)) || (!mc_count)) {
            disableAll = TRUE;
        }
    }

    /*
     * Firmware behaviour
     * enableAll - set filter to enable and delete valid filters
     * disableAll - set filter to disable and delete valid filers
     * filter - set valid filters
     */

    /*
     *  Pass 1: Mark all the valid filters to delete
     */
    for (i=0; i<MAC_MAX_FILTERS_PER_LIST; i++) {
        filter = arPriv->mcast_filters[i];
        filterValid = (filter[1] || filter[2]);
        if (filterValid) {
            action[i] = DELETE;
        } else {
            action[i] = IGNORE;
        }
    }

    if ((!enableAll) && (!disableAll))  {
        /*
         *  Pass 2: Mark all filters which match the previous ones
         */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34)
 		for (j = 0, mc = dev->mc_list; mc && (j < dev->mc_count);
 			    j++, mc = mc->next) {
            mac = mc->dmi_addr;
#else
            netdev_for_each_mc_addr(ha, dev) {
            mac = ha->addr;
#endif

            mcValid =  (mac[2] || mac[3] || mac[4] || mac[5]);
            if (mcValid) {
                for (i=0; i<MAC_MAX_FILTERS_PER_LIST; i++) {
                    filter = arPriv->mcast_filters[i];
                    if ((A_MEMCMP(filter, &mac[0], AR_MCAST_FILTER_MAC_ADDR_SIZE)) == 0) {
                        action[i] = MATCH;
                        break;
                    }
                }
            }
        }

        /*
         * Delete old entries and free-up space for new additions
         */
        for (i = 0; i < MAC_MAX_FILTERS_PER_LIST; i++) {
            filter = arPriv->mcast_filters[i];
            if (action[i] == DELETE) {
                A_PRINTF ("Delete Filter %d = %02x:%02x:%02x:%02x:%02x:%02x\n",
                        i, filter[0], filter[1], filter[2], filter[3], filter[4], filter[5]);
                wmi_del_mcast_filter_cmd(arPriv->arWmi, filter);
                A_MEMZERO(filter, AR_MCAST_FILTER_MAC_ADDR_SIZE);
                /* Make this available for further additions */
                action[i] = IGNORE;
            }
        }

        /* 
         *  Pass 3: Add new filters to empty slots
         */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34)
 		for (j = 0, mc = dev->mc_list; mc && (j < dev->mc_count);
 			    j++, mc = mc->next) {
#else
        netdev_for_each_mc_addr(ha, dev) {
            
#endif
            A_BOOL match;
            A_INT32 free;

#if LINUX_VERSION_CODE < KERNEL_VERSION(2, 6, 34)
            mac = mc->dmi_addr;
#else
            mac = ha->addr;
#endif
            mcValid =  (mac[2] || mac[3] || mac[4] || mac[5]);
            if (mcValid) {
                match = FALSE;
                free = -1;
                for (i=0; i<MAC_MAX_FILTERS_PER_LIST; i++) {
                    A_UINT8 *filter = arPriv->mcast_filters[i];
                    if ((A_MEMCMP(filter, &mac[0], AR_MCAST_FILTER_MAC_ADDR_SIZE)) == 0) {
                        match = TRUE;
                        break;
                    } else if (action[i] != MATCH && action[i] != ADD) {
                        if (free == -1) {
                            free = i;   // Mark the first free index
                        }
                    }
                }
                if ((!match) && (free != -1)) {
                    filter = arPriv->mcast_filters[free];
                    A_MEMCPY(filter, &mac[0], AR_MCAST_FILTER_MAC_ADDR_SIZE);
                    action[free] = ADD;
                }
            }
        }
    }


    for (i=0; i<MAC_MAX_FILTERS_PER_LIST; i++) {
        filter = arPriv->mcast_filters[i];
        if (action[i] == DELETE) {
            A_PRINTF ("Delete Filter %d = %02x:%02x:%02x:%02x:%02x:%02x\n",
                    i, filter[0], filter[1], filter[2], filter[3], filter[4], filter[5]);
            wmi_del_mcast_filter_cmd(arPriv->arWmi, filter);
            A_MEMZERO(filter, AR_MCAST_FILTER_MAC_ADDR_SIZE);
        } else if (action[i] == ADD) {
            A_PRINTF ("Add Filter %d = %02x:%02x:%02x:%02x:%02x:%02x\n",
                    i, filter[0], filter[1], filter[2], filter[3],filter[4],filter[5]);
            wmi_set_mcast_filter_cmd(arPriv->arWmi, filter);
        } else if (action[i] == MATCH) {
            A_PRINTF ("Keep Filter %d = %02x:%02x:%02x:%02x:%02x:%02x\n",
                    i, filter[0], filter[1], filter[2], filter[3],filter[4],filter[5]);
        }
    }

    if (enableAll) {
        /* target allow all multicast packets if fitler enable and fitler list is zero */
        wmi_mcast_filter_cmd(arPriv->arWmi, TRUE);
    } else if (disableAll) {
        /* target drop multicast packets if fitler disable and fitler list is zero */
        wmi_mcast_filter_cmd(arPriv->arWmi, FALSE);
    }
}

static struct net_device_stats *
ar6000_get_stats(struct net_device *dev)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    return &arPriv->arNetStats;
}

static struct iw_statistics *
ar6000_get_iwstats(struct net_device * dev)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ar6k_priv(dev);
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    TARGET_STATS *pStats = &arPriv->arTargetStats;
    struct iw_statistics * pIwStats = &arPriv->arIwStats;
    int rtnllocked;

#ifdef CONFIG_HOST_TCMD_SUPPORT
    if (ar->bIsDestroyProgress || ar->arWmiReady == FALSE || ar->arWlanState == WLAN_DISABLED || testmode)
#else
    if (ar->bIsDestroyProgress || ar->arWmiReady == FALSE || ar->arWlanState == WLAN_DISABLED)
#endif
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
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,0)
    /*
     * The in_atomic function is used to determine if the scheduling is
     * allowed in the current context or not. This was introduced in 2.6
     * From what I have read on the differences between 2.4 and 2.6, the
     * 2.4 kernel did not support preemption and so this check might not
     * be required for 2.4 kernels.
     */
    if (in_atomic())
    {
        if (wmi_get_stats_cmd(arPriv->arWmi) == A_OK) {
        }

        pIwStats->status = 1 ;
        pIwStats->qual.qual = pStats->cs_aveBeacon_rssi - 161;
        pIwStats->qual.level =pStats->cs_aveBeacon_rssi; /* noise is -95 dBm */
        pIwStats->qual.noise = pStats->noise_floor_calibation;
        pIwStats->discard.code = pStats->rx_decrypt_err;
        pIwStats->discard.retries = pStats->tx_retry_cnt;
        pIwStats->miss.beacon = pStats->cs_bmiss_cnt;
        return pIwStats;
    }
#endif /* LINUX_VERSION_CODE */
    dev_hold(dev);
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
    rtnllocked = rtnl_is_locked();
#else
    rtnllocked = TRUE;
#endif
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

        arPriv->statsUpdatePending = TRUE;

        if(wmi_get_stats_cmd(arPriv->arWmi) != A_OK) {
            break;
        }

        wait_event_interruptible_timeout(arPriv->arEvent, arPriv->statsUpdatePending == FALSE, wmitimeout * HZ);
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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)devt;
    struct net_device *dev;
    AR_SOFTC_T *ar = arPriv->arSoftc;
    A_UINT8 i, j, k;

    ar->arWmiReady = TRUE;
    ar->arVersion.wlan_ver = sw_ver;
    ar->arVersion.abi_ver = abi_ver;
    wake_up(&arPriv->arEvent);

    for(i = 0; i < num_device ; i++) {
        dev = ar6000_devices[i];
        arPriv = ar->arDev[i];
        arPriv->arPhyCapability = phyCap;
        if (arPriv->arPhyCapability == WMI_11NAG_CAPABILITY){
        arPriv->phymode = DEF_AP_WMODE_AG;
        } else {
        arPriv->phymode = DEF_AP_WMODE_G;
        }
        A_MEMCPY(dev->dev_addr, datap, AR6000_ETH_ADDR_LEN);

        if (i > 0) {
            if(mac_addr_method) {
                k = dev->dev_addr[5];
                dev->dev_addr[5] += i;
                for(j=5; j>3; j--) {
                    if(dev->dev_addr[j] > k) {
                        break;
                    }
                    k = dev->dev_addr[j-1];
                    dev->dev_addr[j-1]++;
                }
            } else {
                dev->dev_addr[0] = (((dev->dev_addr[0]) ^ (1 << i))) | 0x02;
            }
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("DEV%d mac address = %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n",
            i, dev->dev_addr[0], dev->dev_addr[1],
            dev->dev_addr[2], dev->dev_addr[3],
            dev->dev_addr[4], dev->dev_addr[5]));

#ifdef AR6K_ENABLE_HCI_PAL
        ar6k_hci_pal_info_t *pHciPalInfo = (ar6k_hci_pal_info_t *)ar->hcipal_info;
        pHciPalInfo->hdev->bdaddr.b[0]=dev->dev_addr[5];
        pHciPalInfo->hdev->bdaddr.b[1]=dev->dev_addr[4];
        pHciPalInfo->hdev->bdaddr.b[2]=dev->dev_addr[3];
        pHciPalInfo->hdev->bdaddr.b[3]=dev->dev_addr[2];
        pHciPalInfo->hdev->bdaddr.b[4]=dev->dev_addr[1];
        pHciPalInfo->hdev->bdaddr.b[5]=dev->dev_addr[0];

#endif

#if WLAN_CONFIG_IGNORE_POWER_SAVE_FAIL_EVENT_DURING_SCAN
        wmi_pmparams_cmd(arPriv->arWmi, 0, 1, 0, 0, 1, IGNORE_POWER_SAVE_FAIL_EVENT_DURING_SCAN);
#endif
#if WLAN_CONFIG_DONOT_IGNORE_BARKER_IN_ERP
        wmi_set_lpreamble_cmd(arPriv->arWmi, 0, WMI_DONOT_IGNORE_BARKER_IN_ERP);
#endif

        wmi_set_keepalive_cmd(arPriv->arWmi, WLAN_CONFIG_KEEP_ALIVE_INTERVAL);

#if WLAN_CONFIG_DISABLE_11N
        {
            WMI_SET_HT_CAP_CMD htCap;

            A_MEMZERO(&htCap, sizeof(WMI_SET_HT_CAP_CMD));
            htCap.band = 0;
            wmi_set_ht_cap_cmd(arPriv->arWmi, &htCap);

            htCap.band = 1;
            wmi_set_ht_cap_cmd(arPriv->arWmi, &htCap);
        }
#endif /* WLAN_CONFIG_DISABLE_11N */

#ifdef ATH6K_CONFIG_OTA_MODE
        wmi_powermode_cmd(arPriv->arWmi, MAX_PERF_POWER);
#endif
        wmi_disctimeout_cmd(arPriv->arWmi, WLAN_CONFIG_DISCONNECT_TIMEOUT);
    }
}

void
add_new_sta(AR_SOFTC_DEV_T *arPriv, A_UINT8 *mac, A_UINT16 aid, A_UINT8 *wpaie,
            A_UINT8 ielen, A_UINT8 keymgmt, A_UINT8 ucipher, A_UINT8 auth, A_UINT8 wmode, A_UINT8 apsd_info)
{
    AR_SOFTC_T  *ar = arPriv->arSoftc;
    AR_SOFTC_AP_T *arAp = &arPriv->arAp;
    A_UINT8    free_slot=aid-1;

    A_MEMCPY(ar->connTbl[free_slot].mac, mac, ATH_MAC_LEN);
    A_MEMCPY(ar->connTbl[free_slot].wpa_ie, wpaie, ielen);
    ar->connTbl[free_slot].arPriv   = arPriv;
    ar->connTbl[free_slot].aid      = aid;
    ar->connTbl[free_slot].keymgmt  = keymgmt;
    ar->connTbl[free_slot].ucipher  = ucipher;
    ar->connTbl[free_slot].auth     = auth;
    ar->connTbl[free_slot].wmode    = wmode;
    ar->connTbl[free_slot].apsd_info= apsd_info;
    ar->arAPStats[free_slot].aid    = aid;
    A_MEMZERO(ar->connTbl[free_slot].ba_state, 8);
    arAp->sta_list_index = arAp->sta_list_index | (1 << free_slot);
    aggr_reset_state(ar->connTbl[free_slot].conn_aggr, (void *) arPriv->arNetDev);
}

void
ar6000_connect_event(AR_SOFTC_DEV_T *arPriv, WMI_CONNECT_EVENT *pEvt)
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
    AR_SOFTC_STA_T *arSta;
    AR_SOFTC_T *ar = arPriv->arSoftc;
    AR_SOFTC_DEV_T *arTempPriv = NULL;
    struct ieee80211_frame *wh;
    A_UINT8 *frm, *efrm, *ssid, *rates, *xrates, *wpaie, wpaLen=0;
    A_UINT16 subtype;
    A_UINT8 beaconIeLen;
    A_UINT8 assocReqLen;
    A_UINT8 assocRespLen;
    A_UINT8 *assocInfo;
    A_UINT8 *bssid;

    beaconIeLen = pEvt->beaconIeLen;
    assocReqLen = pEvt->assocReqLen;
    assocRespLen = pEvt->assocRespLen;
    assocInfo = pEvt->assocInfo;

    /* BSSID and MAC_ADDR is in the same location for all modes */
    bssid = pEvt->u.infra_ibss_bss.bssid;

    if(arPriv->arNetworkType & AP_NETWORK) {
        struct net_device *dev = arPriv->arNetDev;
        AR_SOFTC_AP_T *arAp = &arPriv->arAp;
        A_UINT8 aid, wmode, keymgmt, auth_alg;

        if(A_MEMCMP(dev->dev_addr, bssid, ATH_MAC_LEN)==0) {
            arPriv->arBssChannel = pEvt->u.ap_bss.channel;
            ik = &arAp->ap_mode_bkey;

            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AP%d: [UP] SSID %s MAC %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n",
                arPriv->arDeviceIndex, arPriv->arSsid,
                bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]));
#ifdef P2P
            if(arPriv->arNetworkSubType == SUBTYPE_P2PDEV)
                arPriv->arNetworkSubType = SUBTYPE_P2PGO;
#endif

            switch(arPriv->arAuthMode) {
            case WMI_NONE_AUTH:
#ifdef WAPI_ENABLE
                if(arPriv->arPairwiseCrypto == WAPI_CRYPT) {
                    ap_set_wapi_key(arPriv, ik);
                }
#endif
                break;
            case WMI_WPA_PSK_AUTH:
            case WMI_WPA2_PSK_AUTH:
            case (WMI_WPA_PSK_AUTH|WMI_WPA2_PSK_AUTH):
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

                wmi_addKey_cmd(arPriv->arWmi, ik->ik_keyix, keyType, GROUP_USAGE,
                                ik->ik_keylen, (A_UINT8 *)&ik->ik_keyrsc,
                                ik->ik_keydata, KEY_OP_INIT_VAL, ik->ik_macaddr,
                                SYNC_BOTH_WMIFLAG);

                break;
            }
skip_key:
            wmi_bssfilter_cmd(arPriv->arWmi, NONE_BSS_FILTER, 0);

            arPriv->arConnected  = TRUE;
            return;
        }

        wh      = (struct ieee80211_frame *) (assocInfo + beaconIeLen);
        subtype = wh->i_fc[0] & IEEE80211_FC0_SUBTYPE_MASK;
        frm     = (A_UINT8 *)&wh[1];
        efrm    = assocInfo + beaconIeLen + assocReqLen;

        /* capability information */
        frm += 2;

        /* listen int */
        frm += 2;

        /* Reassoc will have current AP addr field */
        if(subtype == IEEE80211_FC0_SUBTYPE_REASSOC_REQ) {
            frm += 6;
        }

        ssid = rates = xrates = wpaie = NULL;
        while (frm < efrm) {
            switch (*frm) {
/* currently unused */
/*
                case IEEE80211_ELEMID_SSID:
                    ssid = frm;
                    break;
                case IEEE80211_ELEMID_RATES:
                    rates = frm;
                    break;
                case IEEE80211_ELEMID_XRATES:
                    xrates = frm;
                    break;
*/
                case IEEE80211_ELEMID_VENDOR:
                    if( (frm[1] > 3) && (frm[2] == 0x00) && (frm[3] == 0x50) &&
                        (frm[4] == 0xF2) && ((frm[5] == 0x01) || (frm[5] == 0x04)) )
                    {
                        wpaie = frm;
                        wpaLen = wpaie[1]+2;
                    }
                    break;
                case IEEE80211_ELEMID_RSN:
                    wpaie = frm;
                    wpaLen = wpaie[1]+2;
                    break;
            }
            frm += frm[1] + 2;
        }

        aid         = pEvt->u.ap_sta.aid;
        wmode       = pEvt->u.ap_sta.phymode;
        keymgmt     = pEvt->u.ap_sta.keymgmt;
        auth_alg    = pEvt->u.ap_sta.auth;

        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("NEW STA %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x \n "
            " AID=%d AUTH=%d WMODE=%d KEYMGMT=%d CIPHER=%d APSD=%x\n",
            pEvt->u.ap_sta.mac_addr[0], pEvt->u.ap_sta.mac_addr[1], pEvt->u.ap_sta.mac_addr[2],
            pEvt->u.ap_sta.mac_addr[3], pEvt->u.ap_sta.mac_addr[4], pEvt->u.ap_sta.mac_addr[5],
            aid, auth_alg, wmode, keymgmt, pEvt->u.ap_sta.cipher, pEvt->u.ap_sta.apsd_info));

        add_new_sta(arPriv, pEvt->u.ap_sta.mac_addr, aid, wpaie, wpaLen, keymgmt,
                pEvt->u.ap_sta.cipher, auth_alg, wmode, pEvt->u.ap_sta.apsd_info);

        /* Send event to application */
        A_MEMZERO(&wrqu, sizeof(wrqu));
        A_MEMCPY(wrqu.addr.sa_data, pEvt->u.ap_sta.mac_addr, ATH_MAC_LEN);
        wireless_send_event(arPriv->arNetDev, IWEVREGISTERED, &wrqu, NULL);
        /* In case the queue is stopped when we switch modes, this will
         * wake it up
         */
        netif_wake_queue(arPriv->arNetDev);
        return;
    }
    arSta = &arPriv->arSta;
#ifdef ATH6K_CONFIG_CFG80211
    ar6k_cfg80211_connect_event(arPriv, pEvt->u.infra_ibss_bss.channel, bssid,
                                pEvt->u.infra_ibss_bss.listenInterval, pEvt->u.infra_ibss_bss.beaconInterval,
                                pEvt->u.infra_ibss_bss.networkType, beaconIeLen,
                                assocReqLen, assocRespLen,
                                assocInfo);
#endif /* ATH6K_CONFIG_CFG80211 */

    A_MEMCPY(arPriv->arBssid, bssid, sizeof(arPriv->arBssid));
    arPriv->arBssChannel = pEvt->u.infra_ibss_bss.channel;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AR6000 connected event on freq %d ", pEvt->u.infra_ibss_bss.channel));
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("with bssid %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x "
            " listenInterval=%d, beaconInterval = %d, beaconIeLen = %d assocReqLen=%d"
            " assocRespLen =%d\n",
             bssid[0], bssid[1], bssid[2],
             bssid[3], bssid[4], bssid[5],
             pEvt->u.infra_ibss_bss.listenInterval, pEvt->u.infra_ibss_bss.beaconInterval,
             beaconIeLen, assocReqLen, assocRespLen));
    if (pEvt->u.infra_ibss_bss.networkType & ADHOC_NETWORK) {
        /* Disable BG Scan for ADHOC NETWORK */
        wmi_scanparams_cmd(arPriv->arWmi, 0, 0,
                               0xFFFF, 0, 0, 0, WMI_SHORTSCANRATIO_DEFAULT,DEFAULT_SCAN_CTRL_FLAGS, 0, 0);
        if (pEvt->u.infra_ibss_bss.networkType & ADHOC_CREATOR) {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Network: Adhoc (Creator)\n"));
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Network: Adhoc (Joiner)\n"));
        }
    } else {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Network: Infrastructure\n"));
    }

    if ((arPriv->arNetworkType == INFRA_NETWORK)) {
        if (arSta->arConnectPending) {
            wmi_listeninterval_cmd(arPriv->arWmi, arSta->arListenIntervalT, arSta->arListenIntervalB);
        }
    }
#ifdef P2P
    if(arPriv->arNetworkSubType == SUBTYPE_P2PDEV)
                arPriv->arNetworkSubType = SUBTYPE_P2PCLIENT;
#endif

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
        if (wrqu.data.length <= IW_CUSTOM_MAX) {
            wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);
        } else {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Fail to send BeaconIEs to upper layer\n"));
        }
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
        if (wrqu.data.length <= IW_CUSTOM_MAX) {
            wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);
        } else {
#if (WIRELESS_EXT >= 18)
            wrqu.data.length = (assocRespLen - 6);
            wireless_send_event(arPriv->arNetDev, IWEVASSOCRESPIE, &wrqu, &assocInfo[assoc_resp_ie_pos]);
#else
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Fail to send Association Response to upper layer\n"));
#endif
        }
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
        if (wrqu.data.length <= IW_CUSTOM_MAX) {
            wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);
        } else {
#if (WIRELESS_EXT >= 18)
            wrqu.data.length = (assocReqLen - 4);
            wireless_send_event(arPriv->arNetDev, IWEVASSOCREQIE, &wrqu, &assocInfo[assoc_req_ie_pos]);
#else
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR, ("Fail to send Association Request to upper layer\n"));
#endif
        }
    }

#ifdef USER_KEYS
    if (arSta->user_savedkeys_stat == USER_SAVEDKEYS_STAT_RUN &&
        arSta->user_saved_keys.keyOk == TRUE)
    {
        key_op_ctrl = KEY_OP_VALID_MASK & ~KEY_OP_INIT_TSC;

        if (arSta->user_key_ctrl & AR6000_USER_SETKEYS_RSC_UNCHANGED) {
            key_op_ctrl &= ~KEY_OP_INIT_RSC;
        } else {
            key_op_ctrl |= KEY_OP_INIT_RSC;
        }
        ar6000_reinstall_keys(arPriv, key_op_ctrl);
    }
#endif /* USER_KEYS */

    netif_wake_queue(arPriv->arNetDev);

    /* For CFG80211 the key configuration and the default key comes in after connect so no point in plumbing invalid keys */
#ifndef ATH6K_CONFIG_CFG80211
    if ((pEvt->u.infra_ibss_bss.networkType & ADHOC_NETWORK)      &&
        (OPEN_AUTH == arPriv->arDot11AuthMode) &&
        (WMI_NONE_AUTH == arPriv->arAuthMode)      &&
        (WEP_CRYPT == arPriv->arPairwiseCrypto))
    {
        if (!arPriv->arConnected) {
            wmi_addKey_cmd(arPriv->arWmi,
                           arPriv->arDefTxKeyIndex,
                           WEP_CRYPT,
                           GROUP_USAGE | TX_USAGE,
                           arPriv->arWepKeyList[arPriv->arDefTxKeyIndex].arKeyLen,
                           NULL,
                           arPriv->arWepKeyList[arPriv->arDefTxKeyIndex].arKey, KEY_OP_INIT_VAL, NULL,
                           NO_SYNC_WMIFLAG);
        }
    }
#endif /* ATH6K_CONFIG_CFG80211 */

    /* Update connect & link status atomically */
    spin_lock_irqsave(&arPriv->arPrivLock, flags);
    arPriv->arConnected  = TRUE;
    arSta->arConnectPending = FALSE;
    netif_carrier_on(arPriv->arNetDev);
    spin_unlock_irqrestore(&arPriv->arPrivLock, flags);
    /* reset the rx aggr state */
    aggr_reset_state(arPriv->conn_aggr, (void *) arPriv->arNetDev);
    reconnect_flag = 0;

    A_MEMZERO(&wrqu, sizeof(wrqu));
    A_MEMCPY(wrqu.addr.sa_data, bssid, IEEE80211_ADDR_LEN);
    wrqu.addr.sa_family = ARPHRD_ETHER;
    wireless_send_event(arPriv->arNetDev, SIOCGIWAP, &wrqu, NULL);
    if ((arPriv->arNetworkType == ADHOC_NETWORK) && arSta->arIbssPsEnable) {
        A_MEMZERO(arSta->arNodeMap, sizeof(arSta->arNodeMap));
        arSta->arNodeNum = 0;
        arSta->arNexEpId = ENDPOINT_2;
    }
   if (!arSta->arUserBssFilter) {
        wmi_bssfilter_cmd(arPriv->arWmi, NONE_BSS_FILTER, 0);
   }
   /* AP-STA Concurrency */
   if(ar->arHoldConnection){
       for(i=0;i < ar->arConfNumDev;i++) {
           arTempPriv = ar->arDev[i];
           if((AP_NETWORK == arTempPriv->arNetworkType)){
               arTempPriv->arBssChannel = arTempPriv->arChannelHint = 0;
           }
       }
       A_TIMEOUT_MS(&ar->ap_reconnect_timer, 1*1000, 0);
   }
}

void ar6000_set_numdataendpts(AR_SOFTC_DEV_T *arPriv, A_UINT32 num)
{
    AR_SOFTC_T *ar = arPriv->arSoftc;
    A_ASSERT(num <= (HTC_MAILBOX_NUM_MAX - 1));
    ar->arNumDataEndPts = num;
}

void
sta_cleanup(AR_SOFTC_DEV_T *arPriv, A_UINT8 i)
{
    struct sk_buff *skb;
    AR_SOFTC_T *ar = arPriv->arSoftc;
    AR_SOFTC_AP_T *arAp = &arPriv->arAp;

    /* empty the queued pkts in the PS queue if any */
    A_MUTEX_LOCK(&ar->connTbl[i].psqLock);
    while (!A_NETBUF_QUEUE_EMPTY(&ar->connTbl[i].psq)) {
        skb = A_NETBUF_DEQUEUE(&ar->connTbl[i].psq);
        A_NETBUF_FREE(skb);
    }
    while (!A_NETBUF_QUEUE_EMPTY(&ar->connTbl[i].apsdq)) {
        skb = A_NETBUF_DEQUEUE(&ar->connTbl[i].apsdq);
        A_NETBUF_FREE(skb);
    }
    A_MUTEX_UNLOCK(&ar->connTbl[i].psqLock);

    /* Zero out the state fields */
    A_MEMZERO(&ar->arAPStats[i], sizeof(WMI_PER_STA_STAT));
    A_MEMZERO(&ar->connTbl[i].mac, ATH_MAC_LEN);
    A_MEMZERO(&ar->connTbl[i].wpa_ie, IEEE80211_MAX_IE);
    ar->connTbl[i].aid = 0;
    ar->connTbl[i].flags = 0;
    ar->connTbl[i].arPriv = NULL;

    arAp->sta_list_index =arAp->sta_list_index & ~(1 << i);
    aggr_reset_state(ar->connTbl[i].conn_aggr, NULL);
}

void
ar6000_ap_cleanup(AR_SOFTC_DEV_T *arPriv)
{
    A_UINT8 ctr;
    struct sk_buff *skb;
    AR_SOFTC_T     *ar     = arPriv->arSoftc;
    AR_SOFTC_AP_T  *arAp   = &arPriv->arAp;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("DEL ALL STA\n"));
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AP%d: [DOWN] SSID %s\n", arPriv->arDeviceIndex, arPriv->arSsid));

    for (ctr=0; ctr < NUM_CONN; ctr++) {
        if(ar->connTbl[ctr].arPriv == arPriv) {
            remove_sta(arPriv, ar->connTbl[ctr].mac, 0);
        }
    }
    A_MUTEX_LOCK(&arAp->mcastpsqLock);
    while (!A_NETBUF_QUEUE_EMPTY(&arAp->mcastpsq)) {
        skb = A_NETBUF_DEQUEUE(&arAp->mcastpsq);
        A_NETBUF_FREE(skb);
    }
    A_MUTEX_UNLOCK(&arAp->mcastpsqLock);
    arPriv->arConnected = FALSE;
}

A_UINT8
remove_sta(AR_SOFTC_DEV_T *arPriv, A_UINT8 *mac, A_UINT16 reason)
{
    A_UINT8 i, removed=0;
    AR_SOFTC_T *ar = arPriv->arSoftc;
    union iwreq_data wrqu;
    struct sk_buff *skb;

    if(IS_MAC_NULL(mac)) {
        return removed;
    }

    if(reason == AP_DISCONNECT_MAX_STA) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("MAX STA %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n", mac[0],
            mac[1], mac[2], mac[3], mac[4], mac[5]));
        return removed;
    } else if(reason == AP_DISCONNECT_ACL) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("ACL STA %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x\n", mac[0],
            mac[1], mac[2], mac[3], mac[4], mac[5]));
        return removed;
    }

    for(i=0; i < NUM_CONN; i++) {
        if(A_MEMCMP(ar->connTbl[i].mac, mac, ATH_MAC_LEN)==0) {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("DEL STA %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x "
            " aid=%d REASON=%d\n", mac[0], mac[1], mac[2],
             mac[3], mac[4], mac[5], ar->connTbl[i].aid, reason));

            sta_cleanup(arPriv, i);
            removed = 1;

            /* Send event to application */
            A_MEMZERO(&wrqu, sizeof(wrqu));
            A_MEMCPY(wrqu.addr.sa_data, mac, ATH_MAC_LEN);
            wireless_send_event(arPriv->arNetDev, IWEVEXPIRED, &wrqu, NULL);

            break;
        }
    }

    /* If there are no more associated STAs, empty the mcast PS q */
    if (arPriv->arAp.sta_list_index == 0) {
        A_MUTEX_LOCK(&arPriv->arAp.mcastpsqLock);
        while (!A_NETBUF_QUEUE_EMPTY(&arPriv->arAp.mcastpsq)) {
            skb = A_NETBUF_DEQUEUE(&arPriv->arAp.mcastpsq);
            A_NETBUF_FREE(skb);
        }
        A_MUTEX_UNLOCK(&arPriv->arAp.mcastpsqLock);

        /* Clear the LSB of the BitMapCtl field of the TIM IE */
        if (ar->arWmiReady) {
            wmi_set_pvb_cmd(arPriv->arWmi, MCAST_AID, 0);
        }
    }

    return removed;
}

void
ar6000_disconnect_event(AR_SOFTC_DEV_T *arPriv, A_UINT8 reason, A_UINT8 *bssid,
                        A_UINT8 assocRespLen, A_UINT8 *assocInfo, A_UINT16 protocolReasonStatus)
{
    A_UINT8 i;
    unsigned long flags;
    union iwreq_data wrqu;
    AR_SOFTC_T *ar = arPriv->arSoftc;
    A_BOOL bt30Devfound = FALSE;
#ifdef P2P
        if((arPriv->arNetworkSubType == SUBTYPE_P2PCLIENT) || (arPriv->arNetworkSubType == SUBTYPE_P2PGO) 
            || (arPriv->arNetworkSubType == SUBTYPE_P2PDEV)){
            if(!(IS_MAC_BCAST(bssid)))
                p2p_clear_peers_authorized_flag(arPriv->p2p_ctx, bssid);
        }
#endif

    if(arPriv->arNetworkType & AP_NETWORK) {
        if(IS_MAC_BCAST(bssid)) {
            ar6000_ap_cleanup(arPriv);
#ifdef P2P
            if(arPriv->arNetworkSubType == SUBTYPE_P2PGO) {
                arPriv->arNextMode = INFRA_NETWORK;
                ar6000_init_mode_info(arPriv);
                arPriv->arNetworkType = INFRA_NETWORK;
                arPriv->arNetworkSubType = SUBTYPE_P2PDEV;
            }
#endif
        } else {
            remove_sta(arPriv, bssid, protocolReasonStatus);
        }
        return;
    }

#ifdef ATH6K_CONFIG_CFG80211
    ar6k_cfg80211_disconnect_event(arPriv, reason, bssid,
                                   assocRespLen, assocInfo,
                                   protocolReasonStatus);
#endif /* ATH6K_CONFIG_CFG80211 */

    /* Send disconnect event to supplicant */
    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.addr.sa_family = ARPHRD_ETHER;
    wireless_send_event(arPriv->arNetDev, SIOCGIWAP, &wrqu, NULL);

    /* it is necessary to clear the host-side rx aggregation state */
    aggr_reset_state(arPriv->conn_aggr, NULL);

    A_UNTIMEOUT(&arPriv->arSta.disconnect_timer);

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AR6000 disconnected"));
    if (bssid[0] || bssid[1] || bssid[2] || bssid[3] || bssid[4] || bssid[5]) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,(" from %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x ",
                 bssid[0], bssid[1], bssid[2], bssid[3], bssid[4], bssid[5]));
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Disconnect Reason is %d, Status Code is %d", reason, protocolReasonStatus));

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
        if ((!arPriv->arSta.arUserBssFilter) && (ar->arWmiReady)) {
            wmi_bssfilter_cmd(arPriv->arWmi, NONE_BSS_FILTER, 0);
        }
    } else {
        arPriv->arSta.arConnectPending = TRUE;
        if (((reason == ASSOC_FAILED) && (protocolReasonStatus == 0x11)) ||
            ((reason == ASSOC_FAILED) && (protocolReasonStatus == 0x0) && (reconnect_flag == 1))) {
            arPriv->arConnected = TRUE;
            return;
        }
    }

    if ((reason == NO_NETWORK_AVAIL) && (ar->arWmiReady))
    {
        bss_t *pWmiSsidnode = NULL;

        /* remove the current associated bssid node */
        wmi_free_node (arPriv->arWmi, bssid);

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
            pWmiSsidnode = wmi_find_Ssidnode (arPriv->arWmi, arPriv->arSsid, arPriv->arSsidLen, FALSE, TRUE);

            if (pWmiSsidnode)
            {
                wmi_free_node (arPriv->arWmi, pWmiSsidnode->ni_macaddr);
            }

        }while (pWmiSsidnode);
    }

    /* Update connect & link status atomically */
    spin_lock_irqsave(&arPriv->arPrivLock, flags);

    arPriv->arConnected = FALSE;
    netif_carrier_off(arPriv->arNetDev);
    spin_unlock_irqrestore(&arPriv->arPrivLock, flags);
#ifdef P2P
        if(arPriv->arNetworkSubType == SUBTYPE_P2PCLIENT) {
            arPriv->arNextMode = INFRA_NETWORK;
            ar6000_init_mode_info(arPriv);
            arPriv->arNetworkType = INFRA_NETWORK;
            arPriv->arNetworkSubType = SUBTYPE_P2PDEV;
        }
#endif

    if( (reason != CSERV_DISCONNECT) || (reconnect_flag != 1) ) {
        reconnect_flag = 0;
    }

#ifdef USER_KEYS
    if (reason != CSERV_DISCONNECT)
    {
        arPriv->arSta.user_savedkeys_stat = USER_SAVEDKEYS_STAT_INIT;
        arPriv->arSta.user_key_ctrl      = 0;
    }
#endif /* USER_KEYS */

    netif_stop_queue(arPriv->arNetDev);
    A_MEMZERO(arPriv->arBssid, sizeof(arPriv->arBssid));
    arPriv->arBssChannel = 0;
    arPriv->arSta.arBeaconInterval = 0;

    for (i=0; i < num_device; i++) {
        AR_SOFTC_DEV_T *temparPriv;
        temparPriv = ar->arDev[i];
        if (temparPriv->isBt30amp == TRUE) {
            bt30Devfound = TRUE;
        }
    }
    if (bt30Devfound == FALSE) {
        ar6000_TxDataCleanup(ar);
    }
    /* AP-STA Concurrency */
    if(ar->arHoldConnection){
        A_TIMEOUT_MS(&ar->ap_reconnect_timer, 2*1000, 0);
    }

    if (arPriv->arNetworkType == ADHOC_NETWORK){
        /* Reset Scan params to default */
        wmi_scanparams_cmd(arPriv->arWmi, 0, 0,
		    60, 0, 0, 0, WMI_SHORTSCANRATIO_DEFAULT,DEFAULT_SCAN_CTRL_FLAGS, 0, 0);
    }
}

void
ar6000_regDomain_event(AR_SOFTC_DEV_T *arPriv, A_UINT32 regCode)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AR6000 Reg Code = 0x%x\n", regCode));
    arPriv->arRegCode = regCode;
}

#ifdef ATH_AR6K_11N_SUPPORT
#define BA_EVT_GET_CONNID(a)    ((a)>>4)
#define BA_EVT_GET_TID(b)       ((b)&0xF)

void
ar6000_aggr_rcv_addba_req_evt(AR_SOFTC_DEV_T *arPriv, WMI_ADDBA_REQ_EVENT *evt)
{
    A_UINT8 connid  = BA_EVT_GET_CONNID(evt->tid);
    A_UINT8 tid     = BA_EVT_GET_TID(evt->tid);
    conn_t  *conn   = ieee80211_find_conn_for_aid(arPriv, connid);

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("ADDBA REQ: tid=%d, connid=%d, status=%d, win_sz=%d\n", tid, connid, evt->status, evt->win_sz));
    if(((arPriv->arNetworkType == INFRA_NETWORK) || (conn != NULL)) && evt->status == 0) {
        aggr_recv_addba_req_evt(get_aggr_ctx(arPriv, conn), tid, evt->st_seq_no, evt->win_sz);
    }
}

void
ar6000_aggr_rcv_addba_resp_evt(AR_SOFTC_DEV_T *arPriv, WMI_ADDBA_RESP_EVENT *evt)
{
    A_UINT8 connid  = BA_EVT_GET_CONNID(evt->tid);
    A_UINT8 tid     = BA_EVT_GET_TID(evt->tid);
    conn_t  *conn   = ieee80211_find_conn_for_aid(arPriv, connid);

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("ADDBA RSP: tid=%d, connid=%d, status=%d, sz=%d\n", tid, connid, evt->status, evt->amsdu_sz));
    if(conn) {
        conn->ba_state[tid] = 0x80;
    }
    if(evt->status == 0) {
    }
}

void
ar6000_aggr_rcv_delba_req_evt(AR_SOFTC_DEV_T *arPriv, WMI_DELBA_EVENT *evt)
{
    A_UINT8 connid  = BA_EVT_GET_CONNID(evt->tid);
    A_UINT8 tid     = BA_EVT_GET_TID(evt->tid);
    conn_t  *conn   = ieee80211_find_conn_for_aid(arPriv, connid);

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("DELBA REQ: tid=%d, connid=%d\n", tid, connid));
    if((arPriv->arNetworkType == INFRA_NETWORK) || (conn != NULL)) {
        aggr_recv_delba_req_evt(get_aggr_ctx(arPriv, conn), tid);
    }
}
#endif

void register_pal_cb(ar6k_pal_config_t *palConfig_p)
{
  ar6k_pal_config_g = *palConfig_p;
}

void
ar6000_hci_event_rcv_evt(AR_SOFTC_DEV_T *arPriv, WMI_HCI_EVENT *cmd)
{
    void *osbuf = NULL;
    A_INT8 i;
    A_UINT8 size, *buf;
    A_STATUS ret = A_OK;

    size = cmd->evt_buf_sz + 4;
    osbuf = A_NETBUF_ALLOC(size);
    if (osbuf == NULL) {
       ret = A_NO_MEMORY;
       AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Error in allocating netbuf \n"));
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
      if((*ar6k_pal_config_g.fpar6k_pal_recv_pkt)(arPriv->hcipal_info, osbuf) == TRUE)
        return;
    }
    ar6000_deliver_frames_to_nw_stack(arPriv->arNetDev, osbuf);
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
ar6000_neighborReport_event(AR_SOFTC_DEV_T *arPriv, int numAps, WMI_NEIGHBOR_INFO *info)
{
#if WIRELESS_EXT >= 18
    struct iw_pmkid_cand *pmkcand;
#else /* WIRELESS_EXT >= 18 */
    static const char *tag = "PRE-AUTH";
    char buf[128];
#endif /* WIRELESS_EXT >= 18 */

    union iwreq_data wrqu;
    int i;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AR6000 Neighbor Report Event\n"));
    for (i=0; i < numAps; info++, i++) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("bssid %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x ",
            info->bssid[0], info->bssid[1], info->bssid[2],
            info->bssid[3], info->bssid[4], info->bssid[5]));
        if (info->bssFlags & WMI_PREAUTH_CAPABLE_BSS) {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("preauth-cap"));
        }
        if (info->bssFlags & WMI_PMKID_VALID_BSS) {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,(" pmkid-valid\n"));
            continue;           /* we skip bss if the pmkid is already valid */
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("\n"));
        A_MEMZERO(&wrqu, sizeof(wrqu));
#if WIRELESS_EXT >= 18
        pmkcand = A_MALLOC_NOWAIT(sizeof(struct iw_pmkid_cand));
        A_MEMZERO(pmkcand, sizeof(struct iw_pmkid_cand));
        pmkcand->index = i;
        pmkcand->flags = info->bssFlags;
        A_MEMCPY(pmkcand->bssid.sa_data, info->bssid, ATH_MAC_LEN);
        wrqu.data.length = sizeof(struct iw_pmkid_cand);
        wireless_send_event(arPriv->arNetDev, IWEVPMKIDCAND, &wrqu, (char *)pmkcand);
        A_FREE(pmkcand);
#else /* WIRELESS_EXT >= 18 */
        snprintf(buf, sizeof(buf), "%s%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x%2.2x",
                 tag,
                 info->bssid[0], info->bssid[1], info->bssid[2],
                 info->bssid[3], info->bssid[4], info->bssid[5],
                 i, info->bssFlags);
        wrqu.data.length = strlen(buf);
        wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);
#endif /* WIRELESS_EXT >= 18 */
    }
}

void
ar6000_indicate_proberesp(AR_SOFTC_DEV_T *arPriv , A_UINT8* pData , A_UINT16 len ,A_UINT8* bssid)
{
}

void
ar6000_indicate_beacon(AR_SOFTC_DEV_T *arPriv, A_UINT8* pData , A_UINT16 len ,A_UINT8* bssid)
{
}

void
ar6000_assoc_req_report_event (void *context, A_UINT8 status, A_UINT8 rspType, A_UINT8* pData, int len)
{
}

#ifdef ATH_SUPPORT_DFS

void ar6000_dfs_attach_event(AR_SOFTC_DEV_T *arPriv, WMI_DFS_HOST_ATTACH_EVENT *capinfo)
{
    AR_SOFTC_AP_T *arAp=&arPriv->arAp;
    arAp->pDfs = dfs_attach_host(arPriv, NULL, capinfo);
    if(arAp->pDfs)
    {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("\nDFS host attached\n"));
    }
    else
    {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("\nDFS host ptr NULL\n"));
    }
}

void ar6000_dfs_init_event(AR_SOFTC_DEV_T *arPriv, WMI_DFS_HOST_INIT_EVENT *info)
{
    AR_SOFTC_AP_T *arAp=&arPriv->arAp;
    dfs_init_radar_filters_host(arAp->pDfs, info);
}

void ar6000_dfs_phyerr_event(AR_SOFTC_DEV_T *arPriv, WMI_DFS_PHYERR_EVENT *info)
{
    AR_SOFTC_AP_T *arAp=&arPriv->arAp;
    dfs_process_phyerr_host(arAp->pDfs, info);
}

void ar6000_dfs_reset_delaylines_event(AR_SOFTC_DEV_T *arPriv)
{
    AR_SOFTC_AP_T *arAp=&arPriv->arAp;
    dfs_reset_alldelaylines(arAp->pDfs);
}

void ar6000_dfs_reset_radarq_event(AR_SOFTC_DEV_T *arPriv)
{
    AR_SOFTC_AP_T *arAp=&arPriv->arAp;
    dfs_reset_radarq(arAp->pDfs);
}

void ar6000_dfs_reset_ar_event(AR_SOFTC_DEV_T *arPriv)
{
    AR_SOFTC_AP_T *arAp=&arPriv->arAp;
    dfs_reset_ar(arAp->pDfs);
}

void ar6000_dfs_reset_arq_event(AR_SOFTC_DEV_T *arPriv)
{
    AR_SOFTC_AP_T *arAp=&arPriv->arAp;
    dfs_reset_arq(arAp->pDfs);
}

void ar6000_dfs_set_dur_multiplier_event(AR_SOFTC_DEV_T *arPriv, A_UINT32 value)
{
    AR_SOFTC_AP_T *arAp=&arPriv->arAp;
    dfs_set_dur_multiplier(arAp->pDfs, value);
}

void ar6000_dfs_set_bangradar_event(AR_SOFTC_DEV_T *arPriv, A_UINT32 value)
{
    AR_SOFTC_AP_T *arAp=&arPriv->arAp;
    dfs_bangradar_enable(arAp->pDfs, value);
}

void ar6000_dfs_set_debuglevel_event(AR_SOFTC_DEV_T *arPriv, A_UINT32 value)
{
    AR_SOFTC_AP_T *arAp=&arPriv->arAp;
    dfs_set_debug_level_host(arAp->pDfs, value);
}

A_STATUS ar6000_dfs_set_maxpulsedur_cmd(AR_SOFTC_DEV_T *arPriv, A_UINT32 value)
{
    return wmi_set_dfs_maxpulsedur_cmd(arPriv->arWmi, value);
}

A_STATUS ar6000_dfs_radar_detected_cmd(AR_SOFTC_DEV_T *arPriv, A_INT16 chan_index, A_INT8 bang_radar)
{
    return wmi_radarDetected_cmd(arPriv->arWmi, chan_index, bang_radar);
}

A_STATUS ar6000_dfs_set_minrssithresh_cmd(AR_SOFTC_DEV_T *arPriv,  A_INT32 rssi)
{
    return wmi_set_dfs_minrssithresh_cmd(arPriv->arWmi, rssi);
}

#endif /* ATH_SUPPORT_DFS */

void
ar6000_tkip_micerr_event(AR_SOFTC_DEV_T *arPriv, A_UINT8 keyid, A_BOOL ismcast)
{
    static const char *tag = "MLME-MICHAELMICFAILURE.indication";
    char buf[128];
    union iwreq_data wrqu;

    /*
     * For AP case, keyid will have aid of STA which sent pkt with
     * MIC error. Use this aid to get MAC & send it to hostapd.
     */
    if (arPriv->arNetworkType == AP_NETWORK) {
        conn_t *s = ieee80211_find_conn_for_aid(arPriv, (keyid >> 2));
        if(!s){
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AP TKIP MIC error received from Invalid aid / STA not found =%d\n", keyid));
            return;
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AP TKIP MIC error received from aid=%d\n", keyid));
        snprintf(buf,sizeof(buf), "%s addr=%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x",
            tag, s->mac[0],s->mac[1],s->mac[2],s->mac[3],s->mac[4],s->mac[5]);
    } else {

#ifdef ATH6K_CONFIG_CFG80211
    ar6k_cfg80211_tkip_micerr_event(arPriv, keyid, ismcast);
#endif /* ATH6K_CONFIG_CFG80211 */

        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR6000 TKIP MIC error received for keyid %d %scast\n",
             keyid & 0x3, ismcast ? "multi": "uni"));
        snprintf(buf, sizeof(buf), "%s(keyid=%d %sicast)", tag, keyid & 0x3,
             ismcast ? "mult" : "un");
    }

    memset(&wrqu, 0, sizeof(wrqu));
    wrqu.data.length = strlen(buf);
    wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);
}

void
ar6000_scanComplete_event(AR_SOFTC_DEV_T *arPriv, A_STATUS status)
{

#ifdef ATH6K_CONFIG_CFG80211
    ar6k_cfg80211_scanComplete_event(arPriv, status);
#endif /* ATH6K_CONFIG_CFG80211 */

    if (arPriv->arSoftc->arWmiReady && arPriv->arSoftc->arWlanState==WLAN_ENABLED) {
        if (!arPriv->arSta.arUserBssFilter) {
            wmi_bssfilter_cmd(arPriv->arWmi, NONE_BSS_FILTER, 0);
        }
    }
    if (arPriv->arSta.scan_triggered) {
        union iwreq_data wrqu;
        A_MEMZERO(&wrqu, sizeof(wrqu));
        wireless_send_event(arPriv->arNetDev, SIOCGIWSCAN, &wrqu, NULL);
        arPriv->arSta.scan_triggered = 0;
    }

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,( "AR6000 scan complete: %d\n", status));
}

void
ar6000_targetStats_event(AR_SOFTC_DEV_T *arPriv,  A_UINT8 *ptr, A_UINT32 len)
{
    A_UINT8 ac, i;

    if(arPriv->arNetworkType == AP_NETWORK) {
        WMI_AP_MODE_STAT *p = (WMI_AP_MODE_STAT *)ptr;
        WMI_PER_STA_STAT *ap = arPriv->arSoftc->arAPStats;

        if (len < sizeof(*p)) {
            return;
        }

        for(ac=0;ac<AP_MAX_NUM_STA;ac++) {
            if(p->sta[ac].aid == 0) {
                continue;
            }
            i = p->sta[ac].aid-1;

            ap[i].tx_bytes   += p->sta[ac].tx_bytes;
            ap[i].tx_pkts    += p->sta[ac].tx_pkts;
            ap[i].tx_error   += p->sta[ac].tx_error;
            ap[i].tx_discard += p->sta[ac].tx_discard;
            ap[i].rx_bytes   += p->sta[ac].rx_bytes;
            ap[i].rx_pkts    += p->sta[ac].rx_pkts;
            ap[i].rx_error   += p->sta[ac].rx_error;
            ap[i].rx_discard += p->sta[ac].rx_discard;
        }
    } else {
        WMI_TARGET_STATS *pTarget = (WMI_TARGET_STATS *)ptr;
         TARGET_STATS *pStats = &arPriv->arTargetStats;

        if (len < sizeof(*pTarget)) {
            return;
        }

        // Update the RSSI of the connected bss.
        if (arPriv->arConnected) {
            bss_t *pConnBss = NULL;

            pConnBss = wmi_find_node(arPriv->arWmi,arPriv->arBssid);
            if (pConnBss)
            {
                pConnBss->ni_rssi = pTarget->cservStats.cs_aveBeacon_rssi;
                pConnBss->ni_snr = pTarget->cservStats.cs_aveBeacon_snr;
                wmi_node_return(arPriv->arWmi, pConnBss);
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
                    rssi_compensation_calc(arPriv, pStats->cs_aveBeacon_rssi);
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

        if (arPriv->statsUpdatePending) {
            arPriv->statsUpdatePending = FALSE;
            wake_up(&arPriv->arEvent);
        }
    }
}

void
ar6000_rssiThreshold_event(AR_SOFTC_DEV_T *arPriv,  WMI_RSSI_THRESHOLD_VAL newThreshold, A_INT16 rssi)
{
    USER_RSSI_THOLD userRssiThold;

    rssi = rssi + SIGNAL_QUALITY_NOISE_FLOOR;

    if (enablerssicompensation) {
        rssi = rssi_compensation_calc(arPriv, rssi);
    }

    /* Send an event to the app */
    userRssiThold.tag = arPriv->arSta.rssi_map[newThreshold].tag;
    userRssiThold.rssi = rssi;
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("rssi Threshold range = %d tag = %d  rssi = %d\n", newThreshold,
             userRssiThold.tag, userRssiThold.rssi));

    ar6000_send_event_to_app(arPriv, WMI_RSSI_THRESHOLD_EVENTID,(A_UINT8 *)&userRssiThold, sizeof(USER_RSSI_THOLD));
}


void
ar6000_hbChallengeResp_event(AR_SOFTC_DEV_T *arPriv, A_UINT32 cookie, A_UINT32 source)
{
    AR_SOFTC_T  *ar = arPriv->arSoftc;
    if (source == APP_HB_CHALLENGE) {
        /* Report it to the app in case it wants a positive acknowledgement */
        ar6000_send_event_to_app(arPriv, WMIX_HB_CHALLENGE_RESP_EVENTID,
                                 (A_UINT8 *)&cookie, sizeof(cookie));
    } else {
        /* This would ignore the replys that come in after their due time */
        if (cookie == ar->arHBChallengeResp.seqNum) {
            ar->arHBChallengeResp.outstanding = FALSE;
        }
    }
}


void
ar6000_reportError_event(AR_SOFTC_DEV_T *arPriv, WMI_TARGET_ERROR_VAL errorVal)
{
    char    *errString[] = {
                [WMI_TARGET_PM_ERR_FAIL]    "WMI_TARGET_PM_ERR_FAIL",
                [WMI_TARGET_KEY_NOT_FOUND]  "WMI_TARGET_KEY_NOT_FOUND",
                [WMI_TARGET_DECRYPTION_ERR] "WMI_TARGET_DECRYPTION_ERR",
                [WMI_TARGET_BMISS]          "WMI_TARGET_BMISS",
                [WMI_PSDISABLE_NODE_JOIN]   "WMI_PSDISABLE_NODE_JOIN"
                };

    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR6000 Error on Target. Error = 0x%x\n", errorVal));

    /* One error is reported at a time, and errorval is a bitmask */
    if(errorVal & (errorVal - 1))
       return;

    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("AR6000 Error type = "));
    switch(errorVal)
    {
        case WMI_TARGET_PM_ERR_FAIL:
        case WMI_TARGET_KEY_NOT_FOUND:
        case WMI_TARGET_DECRYPTION_ERR:
        case WMI_TARGET_BMISS:
        case WMI_PSDISABLE_NODE_JOIN:
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("%s\n", errString[errorVal]));
            break;
        default:
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("INVALID\n"));
            break;
    }

}


void
ar6000_cac_event(AR_SOFTC_DEV_T *arPriv, A_UINT8 ac, A_UINT8 cacIndication,
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
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AR6000 CAC notification. "
                "AC = %d, cacIndication = 0x%x, statusCode = 0x%x\n",
                 ac, cacIndication, statusCode));
}

void
ar6000_channel_change_event(AR_SOFTC_DEV_T *arPriv, A_UINT16 oldChannel,
                            A_UINT16 newChannel)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Channel Change notification\nOld Channel: %d, New Channel: %d\n",
             oldChannel, newChannel));
}

#define AR6000_PRINT_BSSID(_pBss)  do {     \
        A_PRINTF("%2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x ",\
                 (_pBss)[0],(_pBss)[1],(_pBss)[2],(_pBss)[3],\
                 (_pBss)[4],(_pBss)[5]);  \
} while(0)

void
ar6000_roam_tbl_event(AR_SOFTC_DEV_T *arPriv, WMI_TARGET_ROAM_TBL *pTbl)
{
    A_UINT8 i;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("ROAM TABLE NO OF ENTRIES is %d ROAM MODE is %d\n",
              pTbl->numEntries, pTbl->roamMode));
    for (i= 0; i < pTbl->numEntries; i++) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("[%d]bssid %2.2x:%2.2x:%2.2x:%2.2x:%2.2x:%2.2x ", i,
            pTbl->bssRoamInfo[i].bssid[0], pTbl->bssRoamInfo[i].bssid[1],
            pTbl->bssRoamInfo[i].bssid[2],
            pTbl->bssRoamInfo[i].bssid[3],
            pTbl->bssRoamInfo[i].bssid[4],
            pTbl->bssRoamInfo[i].bssid[5]));
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("RSSI %d RSSIDT %d LAST RSSI %d UTIL %d ROAM_UTIL %d"
                 " BIAS %d\n",
            pTbl->bssRoamInfo[i].rssi,
            pTbl->bssRoamInfo[i].rssidt,
            pTbl->bssRoamInfo[i].last_rssi,
            pTbl->bssRoamInfo[i].util,
            pTbl->bssRoamInfo[i].roam_util,
            pTbl->bssRoamInfo[i].bias));
    }
}

void
ar6000_wow_list_event(AR_SOFTC_DEV_T *arPriv, A_UINT8 num_filters, WMI_GET_WOW_LIST_REPLY *wow_reply)
{
    A_UINT8 i,j;

    /*Each event now contains exactly one filter, see bug 26613*/
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("WOW pattern %d of %d patterns\n", wow_reply->this_filter_num,                 wow_reply->num_filters));
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("wow mode = %s host mode = %s\n",
            (wow_reply->wow_mode == 0? "disabled":"enabled"),
            (wow_reply->host_mode == 1 ? "awake":"asleep")));


    /*If there are no patterns, the reply will only contain generic
      WoW information. Pattern information will exist only if there are
      patterns present. Bug 26716*/

   /* If this event contains pattern information, display it*/
    if (wow_reply->this_filter_num) {
        i=0;
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("id=%d size=%d offset=%d\n",
                    wow_reply->wow_filters[i].wow_filter_id,
                    wow_reply->wow_filters[i].wow_filter_size,
                    wow_reply->wow_filters[i].wow_filter_offset));
       AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("wow pattern = "));
       for (j=0; j< wow_reply->wow_filters[i].wow_filter_size; j++) {
             AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("%2.2x",wow_reply->wow_filters[i].wow_filter_pattern[j]));
        }

        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("\nwow mask = "));
        for (j=0; j< wow_reply->wow_filters[i].wow_filter_size; j++) {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("%2.2x",wow_reply->wow_filters[i].wow_filter_mask[j]));
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("\n"));
    }
}

/*
 * Report the Roaming related data collected on the target
 */
void
ar6000_display_roam_time(WMI_TARGET_ROAM_TIME *p)
{
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Disconnect Data : BSSID: "));
    AR6000_PRINT_BSSID(p->disassoc_bssid);
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,(" RSSI %d DISASSOC Time %d NO_TXRX_TIME %d\n",
             p->disassoc_bss_rssi,p->disassoc_time,
             p->no_txrx_time));
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Connect Data: BSSID: "));
    AR6000_PRINT_BSSID(p->assoc_bssid);
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,(" RSSI %d ASSOC Time %d TXRX_TIME %d\n",
             p->assoc_bss_rssi,p->assoc_time,
             p->allow_txrx_time));
}

void
ar6000_roam_data_event(AR_SOFTC_DEV_T *arPriv, WMI_TARGET_ROAM_DATA *p)
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
ar6000_bssInfo_event_rx(AR_SOFTC_DEV_T *arPriv, A_UINT8 *datap, int len)
{
    struct sk_buff *skb;
    WMI_BSS_INFO_HDR *bih = (WMI_BSS_INFO_HDR *)datap;


    if (!arPriv->arSta.arMgmtFilter) {
        return;
    }
    if (((arPriv->arSta.arMgmtFilter & IEEE80211_FILTER_TYPE_BEACON) &&
        (bih->frameType != BEACON_FTYPE))  ||
        ((arPriv->arSta.arMgmtFilter & IEEE80211_FILTER_TYPE_PROBE_RESP) &&
        (bih->frameType != PROBERESP_FTYPE)))
    {
        return;
    }

    if ((skb = A_NETBUF_ALLOC_RAW(len)) != NULL) {

        A_NETBUF_PUT(skb, len);
        A_MEMCPY(A_NETBUF_DATA(skb), datap, len);
        skb->dev = arPriv->arNetDev;
#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,22)
        A_MEMCPY(skb_mac_header(skb), A_NETBUF_DATA(skb), 6);
#else
        skb->mac.raw = A_NETBUF_DATA(skb);
#endif
        skb->ip_summed = CHECKSUM_NONE;
        skb->pkt_type = PACKET_OTHERHOST;
        skb->protocol = __constant_htons(0x0019);
        A_NETIF_RX(skb);
    }
}

A_UINT32 wmiSendCmdNum;

A_STATUS
ar6000_control_tx(void *devt, void *osbuf, HTC_ENDPOINT_ID eid)
{
    AR_SOFTC_DEV_T  *arPriv = (AR_SOFTC_DEV_T *)devt;
    AR_SOFTC_T      *ar     = arPriv->arSoftc;
    A_STATUS         status = A_OK;
    struct ar_cookie *cookie = NULL;
    int i;

#ifdef CONFIG_PM
    if (ar->arWowState == WLAN_WOW_STATE_SUSPENDED) {
        return A_EACCES;
    }
#endif /* CONFIG_PM */
        /* take lock to protect ar6000_alloc_cookie() */
    AR6000_SPIN_LOCK(&ar->arLock, 0);

    do {

        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_TX,("ar_contrstatus = ol_tx: skb=0x%x, len=0x%x eid =%d\n",
                         (A_UINT32)osbuf, A_NETBUF_LEN(osbuf), eid));

        if (ar->arWMIControlEpFull && (eid == ar->arControlEp)) {
                /* control endpoint is full, don't allocate resources, we
                 * are just going to drop this packet */
            cookie = NULL;
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,(" WMI Control EP full, dropping packet : 0x%X, len:%d \n",
                    (A_UINT32)osbuf, A_NETBUF_LEN(osbuf)));
#ifdef ANDROID_ENV
            if (++android_epfull_cnt > ANDROID_RELOAD_THRESHOLD_FOR_EP_FULL) {
                android_send_reload_event(arPriv);
                android_epfull_cnt = 0;
            }
#endif
        } else {
            cookie = ar6000_alloc_cookie(ar);
        }

        if (cookie == NULL) {
            status = A_NO_MEMORY;
            break;
        }

        if(logWmiRawMsgs) {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("WMI cmd send, msgNo %d :", wmiSendCmdNum));
            for(i = 0; i < a_netbuf_to_len(osbuf); i++)
                AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("%x ", ((A_UINT8 *)a_netbuf_to_data(osbuf))[i]));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("\n"));
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
        cookie->arc_bp[0] = (A_UINT32)osbuf;
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

    return status;
}

/* indicate tx activity or inactivity on a WMI stream */
void ar6000_indicate_tx_activity(void *devt, A_UINT8 TrafficClass, A_BOOL Active)
{
    AR_SOFTC_DEV_T  *arPriv = (AR_SOFTC_DEV_T *)devt;
    AR_SOFTC_T      *ar      = arPriv->arSoftc;
    HTC_ENDPOINT_ID eid ;
    int i;

    if (ar->arWmiReady) {
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
ar6000_btcoex_config_event(AR_SOFTC_DEV_T *arPriv,  A_UINT8 *ptr, A_UINT32 len)
{

    WMI_BTCOEX_CONFIG_EVENT *pBtcoexConfig = (WMI_BTCOEX_CONFIG_EVENT *)ptr;
    WMI_BTCOEX_CONFIG_EVENT *pArbtcoexConfig =&arPriv->arBtcoexConfig;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AR6000 BTCOEX CONFIG EVENT \n"));

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("received config event\n"));
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
                                        sizeof(WMI_SET_BTCOEX_BTINQUIRY_PAGE_CONFIG_CMD));
            break;
    }
    if (arPriv->statsUpdatePending) {
         arPriv->statsUpdatePending = FALSE;
          wake_up(&arPriv->arEvent);
    }
}

void
ar6000_btcoex_stats_event(AR_SOFTC_DEV_T *arPriv,  A_UINT8 *ptr, A_UINT32 len)
{
    WMI_BTCOEX_STATS_EVENT *pBtcoexStats = (WMI_BTCOEX_STATS_EVENT *)ptr;

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AR6000 BTCOEX CONFIG EVENT \n"));

    A_MEMCPY(&arPriv->arBtcoexStats, pBtcoexStats, sizeof(WMI_BTCOEX_STATS_EVENT));

    if (arPriv->statsUpdatePending) {
         arPriv->statsUpdatePending = FALSE;
        wake_up(&arPriv->arEvent);
    }

}

void
ar6000_wacinfo_event(AR_SOFTC_DEV_T *arPriv,  A_UINT8 *ptr, A_UINT32 len)
{
    WMI_GET_WAC_INFO *pWacInfo = (WMI_GET_WAC_INFO *)ptr;

    A_MEMCPY(&arPriv->wacInfo, pWacInfo, sizeof(WMI_GET_WAC_INFO));

    if (arPriv->statsUpdatePending) {
        arPriv->statsUpdatePending = FALSE;
        wake_up(&arPriv->arEvent);
    }
}

#ifndef RK29
module_init(ar6000_init_module);
module_exit(ar6000_cleanup_module);
#endif

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
void ar6000_send_event_to_app(AR_SOFTC_DEV_T *arPriv, A_UINT16 eventId,
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
    wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);
    A_FREE(buf);

#ifdef ANDROID_ENV
    if (eventId == WMI_ERROR_REPORT_EVENTID) {
        android_send_reload_event(arPriv);
    }
#endif /* ANDROID_ENV */

#endif


}

/*
 * This function is used to send events larger than 256 bytes
 * to the application. The buf which is sent to application
 * includes the event ID and event content.
 */
void ar6000_send_generic_event_to_app(AR_SOFTC_DEV_T *arPriv, A_UINT16 eventId,
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
    wireless_send_event(arPriv->arNetDev, IWEVGENIE, &wrqu, buf);

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
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)devt;

    event.range = newThreshold;
    event.snr = snr;

    ar6000_send_event_to_app(arPriv, WMI_SNR_THRESHOLD_EVENTID, (A_UINT8 *)&event,
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
    A_STATUS ret = A_OK;

    switch(cfgParam)
    {
        case AR6000_DRIVER_CFG_GET_WLANNODECACHING:
           *((A_UINT32 *)result) = wlanNodeCaching;
           break;
        case AR6000_DRIVER_CFG_LOG_RAW_WMI_MSGS:
           *((A_UINT32 *)result) = logWmiRawMsgs;
            break;
        default:
           ret = A_EINVAL;
           break;
    }

    return ret;
}

void
ar6000_keepalive_rx(void *devt, A_UINT8 configured)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)devt;

    arPriv->arSta.arKeepaliveConfigured = configured;
    wake_up(&arPriv->arEvent);
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

void ar6000_pspoll_event(AR_SOFTC_DEV_T *arPriv,A_UINT8 aid)
{
    conn_t *conn=NULL;
    A_BOOL isPsqEmpty = FALSE;

    conn = ieee80211_find_conn_for_aid(arPriv, aid);

    if(!conn) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("PS-POLL from invalid STA\n"));
        return;
    }

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
        ar6000_data_tx(skb, arPriv->arNetDev);
        STA_CLR_PS_POLLED(conn);

        A_MUTEX_LOCK(&conn->psqLock);
        isPsqEmpty  = A_NETBUF_QUEUE_EMPTY(&conn->psq);
        A_MUTEX_UNLOCK(&conn->psqLock);

    }

    /* Clear the PVB for this STA if the queue has become empty */
    if (isPsqEmpty) {
        wmi_set_pvb_cmd(arPriv->arWmi, conn->aid, 0);
    }
}

void ar6000_dtimexpiry_event(AR_SOFTC_DEV_T *arPriv)
{
    A_BOOL isMcastQueued = FALSE;
    struct sk_buff *skb = NULL;
    AR_SOFTC_AP_T  *arAp = &arPriv->arAp;

    /* If there are no associated STAs, ignore the DTIM expiry event.
     * There can be potential race conditions where the last associated
     * STA may disconnect & before the host could clear the 'Indicate DTIM'
     * request to the firmware, the firmware would have just indicated a DTIM
     * expiry event. The race is between 'clear DTIM expiry cmd' going
     * from the host to the firmware & the DTIM expiry event happening from
     * the firmware to the host.
     */
    if (arAp->sta_list_index == 0) {
        return;
    }

    A_MUTEX_LOCK(&arAp->mcastpsqLock);
    isMcastQueued = A_NETBUF_QUEUE_EMPTY(&arAp->mcastpsq);
    A_MUTEX_UNLOCK(&arAp->mcastpsqLock);

    if(isMcastQueued == TRUE) {
        return;
    }

    /* Flush the mcast psq to the target */
    /* Set the STA flag to DTIMExpired, so that the frame will go out */
    arAp->DTIMExpired = TRUE;

    A_MUTEX_LOCK(&arAp->mcastpsqLock);
    while (!A_NETBUF_QUEUE_EMPTY(&arAp->mcastpsq)) {
        skb = A_NETBUF_DEQUEUE(&arAp->mcastpsq);
        A_MUTEX_UNLOCK(&arAp->mcastpsqLock);

        ar6000_data_tx(skb, arPriv->arNetDev);

        A_MUTEX_LOCK(&arAp->mcastpsqLock);
    }
    A_MUTEX_UNLOCK(&arAp->mcastpsqLock);

    /* Reset the DTIMExpired flag back to 0 */
    arAp->DTIMExpired = FALSE;

    /* Clear the LSB of the BitMapCtl field of the TIM IE */
    wmi_set_pvb_cmd(arPriv->arWmi, MCAST_AID, 0);
}

static void ar6000_uapsd_trigger_frame_rx(AR_SOFTC_DEV_T *arPriv, conn_t *conn)
{
    A_BOOL isApsdqEmpty;
    A_BOOL isApsdqEmptyAtStart;
    A_UINT32 numFramesToDeliver;

    /* If the APSD q for this STA is not empty, dequeue and send a pkt from
     * the head of the q. Also update the More data bit in the WMI_DATA_HDR
     * if there are more pkts for this STA in the APSD q. If there are no more
     * pkts for this STA, update the APSD bitmap for this STA.
     */

    numFramesToDeliver = (conn->apsd_info >> 4) & 0xF;

    /* Number of frames to send in a service period is indicated by the station
     * in the QOS_INFO of the association request
     * If it is zero, send all frames
     */
    if (!numFramesToDeliver) {
        numFramesToDeliver = 0xFFFF;
    }

    A_MUTEX_LOCK(&conn->psqLock);
    isApsdqEmpty  = A_NETBUF_QUEUE_EMPTY(&conn->apsdq);
    A_MUTEX_UNLOCK(&conn->psqLock);
    isApsdqEmptyAtStart  = isApsdqEmpty;

    while ((!isApsdqEmpty) && (numFramesToDeliver)) {
        struct sk_buff *skb = NULL;

        A_MUTEX_LOCK(&conn->psqLock);
        skb = A_NETBUF_DEQUEUE(&conn->apsdq);
        isApsdqEmpty  = A_NETBUF_QUEUE_EMPTY(&conn->apsdq);
        A_MUTEX_UNLOCK(&conn->psqLock);

        /* Set the STA flag to Trigger delivery, so that the frame will go out */
        STA_SET_APSD_TRIGGER(conn);
        numFramesToDeliver--;

        /* Last frame in the service period, set EOSP or queue empty */
        if ((isApsdqEmpty) || (!numFramesToDeliver)) {
            STA_SET_APSD_EOSP(conn);
        }
        ar6000_data_tx(skb, arPriv->arNetDev);
        STA_CLR_APSD_TRIGGER(conn);
        STA_CLR_APSD_EOSP(conn);
    }

    if (isApsdqEmpty) {
        if (isApsdqEmptyAtStart) {
            wmi_set_apsd_buffered_traffic_cmd(arPriv->arWmi, conn->aid, 0,
                         WMI_AP_APSD_NO_DELIVERY_FRAMES_FOR_THIS_TRIGGER);
        } else {
            wmi_set_apsd_buffered_traffic_cmd(arPriv->arWmi, conn->aid, 0, 0);
        }
    }

    return;
}

void
read_rssi_compensation_param(AR_SOFTC_T *ar)
{
    A_UINT8 *cust_data_ptr;
    USER_RSSI_CPENSATION *rssi_compensation_param;
//#define RSSICOMPENSATION_PRINT
#ifdef RSSICOMPENSATION_PRINT
    A_INT16 i;
    cust_data_ptr = ar6000_get_cust_data_buffer(ar->arTargetType);
    for (i=0; i<16; i++) {
        AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("cust_data_%d = %x \n", i, *(A_UINT8 *)cust_data_ptr));
        cust_data_ptr += 1;
    }
#endif
    rssi_compensation_param = &ar->rssi_compensation_param;
    cust_data_ptr = ar6000_get_cust_data_buffer(ar->arTargetType);

    rssi_compensation_param->customerID = *(A_UINT16 *)cust_data_ptr & 0xffff;
    rssi_compensation_param->enable = *(A_UINT16 *)(cust_data_ptr+2) & 0xffff;
    rssi_compensation_param->bg_param_a = *(A_UINT16 *)(cust_data_ptr+4) & 0xffff;
    rssi_compensation_param->bg_param_b = *(A_UINT16 *)(cust_data_ptr+6) & 0xffff;
    rssi_compensation_param->a_param_a = *(A_UINT16 *)(cust_data_ptr+8) & 0xffff;
    rssi_compensation_param->a_param_b = *(A_UINT16 *)(cust_data_ptr+10) &0xffff;
    rssi_compensation_param->reserved = *(A_UINT32 *)(cust_data_ptr+12);

#ifdef RSSICOMPENSATION_PRINT
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("customerID = 0x%x \n", rssi_compensation_param->customerID));
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("enable = 0x%x \n", rssi_compensation_param->enable));
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("bg_param_a = 0x%x and %d \n", rssi_compensation_param->bg_param_a, rssi_compensation_param->bg_param_a));
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("bg_param_b = 0x%x and %d \n", rssi_compensation_param->bg_param_b, rssi_compensation_param->bg_param_b));
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("a_param_a = 0x%x and %d \n", rssi_compensation_param->a_param_a, rssi_compensation_param->a_param_a));
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("a_param_b = 0x%x and %d \n", rssi_compensation_param->a_param_b, rssi_compensation_param->a_param_b));
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Last 4 bytes = 0x%x \n", rssi_compensation_param->reserved));
#endif

    if (rssi_compensation_param->enable != 0x1) {
        rssi_compensation_param->enable = 0;
    }

   return;
}

A_INT32
rssi_compensation_calc_tcmd(AR_SOFTC_T *ar, A_UINT32 freq, A_INT32 rssi, A_UINT32 totalPkt)
{
    USER_RSSI_CPENSATION *rssi_compensation_param;

    rssi_compensation_param = &ar->rssi_compensation_param;

    if (freq > 5000)
    {
        if (rssi_compensation_param->enable)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, (">>> 11a\n"));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi before compensation  = %d, totalPkt = %d\n", rssi,totalPkt));
            rssi = rssi * rssi_compensation_param->a_param_a + totalPkt * rssi_compensation_param->a_param_b;
            rssi = (rssi-50) /100;
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi after compensation = %d\n", rssi));
        }
    }
    else
    {
        if (rssi_compensation_param->enable)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, (">>> 11bg\n"));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi before compensation  = %d, totalPkt = %d\n", rssi,totalPkt));
            rssi = rssi * rssi_compensation_param->bg_param_a + totalPkt * rssi_compensation_param->bg_param_b;
            rssi = (rssi-50) /100;
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi after compensation = %d\n", rssi));
        }
    }

    return rssi;
}

A_INT16
rssi_compensation_calc(AR_SOFTC_DEV_T *arPriv, A_INT16 rssi)
{
    USER_RSSI_CPENSATION *rssi_compensation_param;
    AR_SOFTC_T *ar  = arPriv->arSoftc;

    rssi_compensation_param = &ar->rssi_compensation_param;

    if (arPriv->arBssChannel > 5000)
    {
        if (rssi_compensation_param->enable)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, (">>> 11a\n"));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi before compensation  = %d\n", rssi));
            rssi = rssi * rssi_compensation_param->a_param_a + rssi_compensation_param->a_param_b;
            rssi = (rssi-50) /100;
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi after compensation = %d\n", rssi));
        }
    }
    else
    {
        if (rssi_compensation_param->enable)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, (">>> 11bg\n"));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi before compensation  = %d\n", rssi));
            rssi = rssi * rssi_compensation_param->bg_param_a + rssi_compensation_param->bg_param_b;
            rssi = (rssi-50) /100;
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi after compensation = %d\n", rssi));
        }
    }

    return rssi;
}

A_INT16
rssi_compensation_reverse_calc(AR_SOFTC_DEV_T *arPriv, A_INT16 rssi, A_BOOL Above)
{
    A_INT16 i;

    USER_RSSI_CPENSATION *rssi_compensation_param;
    AR_SOFTC_T       *ar  = arPriv->arSoftc;

    rssi_compensation_param = &ar->rssi_compensation_param;

    if (arPriv->arBssChannel > 5000)
    {
        if (rssi_compensation_param->enable)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, (">>> 11a\n"));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi before rev compensation  = %d\n", rssi));
            rssi = rssi * 100;
            rssi = (rssi - rssi_compensation_param->a_param_b) / rssi_compensation_param->a_param_a;
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi after rev compensation = %d\n", rssi));
        }
    }
    else
    {
        if (rssi_compensation_param->enable)
        {
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, (">>> 11bg\n"));
            AR_DEBUG_PRINTF(ATH_DEBUG_INFO, ("rssi before rev compensation  = %d\n", rssi));

            if (Above) {
                for (i=95; i>=0; i--) {
                    if (rssi <=  rssi_compensation_table[arPriv->arDeviceIndex][i]) {
                        rssi = 0 - i;
                        break;
                    }
                }
            } else {
                for (i=0; i<=95; i++) {
                    if (rssi >=  rssi_compensation_table[arPriv->arDeviceIndex][i]) {
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
void ap_wapi_rekey_event(AR_SOFTC_DEV_T *arPriv, A_UINT8 type, A_UINT8 *mac)
{
    union iwreq_data wrqu;
    A_CHAR buf[20];

    A_MEMZERO(buf, sizeof(buf));

    strcpy(buf, "WAPI_REKEY");
    buf[10] = type;
    A_MEMCPY(&buf[11], mac, ATH_MAC_LEN);

    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = 10+1+ATH_MAC_LEN;
    wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);

    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("WAPI REKEY - %d - %02x:%02x\n", type, mac[4], mac[5]));
}
#endif

#ifdef P2P
void *get_p2p_ctx(AR_SOFTC_DEV_T *arPriv)
{
    return (arPriv->p2p_ctx);
}

void *get_wmi_ctx(AR_SOFTC_DEV_T *arPriv)
{
    return (arPriv->arWmi);
}

NETWORK_SUBTYPE get_network_subtype(AR_SOFTC_DEV_T *arPriv)
{
    return (arPriv->arNetworkSubType);
}

#endif /* P2P */

#ifdef USER_KEYS
static A_STATUS

ar6000_reinstall_keys(AR_SOFTC_DEV_T *arPriv, A_UINT8 key_op_ctrl)
{
    A_STATUS status = A_OK;
    struct ieee80211req_key *uik = &arPriv->arSta.user_saved_keys.ucast_ik;
    struct ieee80211req_key *bik = &arPriv->arSta.user_saved_keys.bcast_ik;
    CRYPTO_TYPE  keyType = arPriv->arSta.user_saved_keys.keyType;

    if (IEEE80211_CIPHER_CCKM_KRK != uik->ik_type) {
        if (NONE_CRYPT == keyType) {
            goto _reinstall_keys_out;
        }

        if (uik->ik_keylen) {
            status = wmi_addKey_cmd(arPriv->arWmi, uik->ik_keyix,
                    keyType, PAIRWISE_USAGE,
                    uik->ik_keylen, (A_UINT8 *)&uik->ik_keyrsc,
                    uik->ik_keydata, key_op_ctrl, uik->ik_macaddr, SYNC_BEFORE_WMIFLAG);
        }

    } else {
        status = wmi_add_krk_cmd(arPriv->arWmi, uik->ik_keydata);
    }

    if (IEEE80211_CIPHER_CCKM_KRK != bik->ik_type) {
        if (NONE_CRYPT == keyType) {
            goto _reinstall_keys_out;
        }

        if (bik->ik_keylen) {
            status = wmi_addKey_cmd(arPriv->arWmi, bik->ik_keyix,
                    keyType, GROUP_USAGE,
                    bik->ik_keylen, (A_UINT8 *)&bik->ik_keyrsc,
                    bik->ik_keydata, key_op_ctrl, bik->ik_macaddr, NO_SYNC_WMIFLAG);
        }
    } else {
        status = wmi_add_krk_cmd(arPriv->arWmi, bik->ik_keydata);
    }

_reinstall_keys_out:
    arPriv->arSta.user_savedkeys_stat = USER_SAVEDKEYS_STAT_INIT;
    arPriv->arSta.user_key_ctrl      = 0;

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
void
ar6000_init_mode_info(AR_SOFTC_DEV_T *arPriv)
{
    AR_SOFTC_T       *ar = arPriv->arSoftc;

    arPriv->arDot11AuthMode      = OPEN_AUTH;
    arPriv->arAuthMode           = WMI_NONE_AUTH;
    arPriv->arPairwiseCrypto     = NONE_CRYPT;
    arPriv->arPairwiseCryptoLen  = 0;
    arPriv->arGroupCrypto        = NONE_CRYPT;
    arPriv->arGroupCryptoLen     = 0;
    arPriv->arChannelHint        = 0;
    arPriv->arDefTxKeyIndex      = 0;
    A_MEMZERO(arPriv->arBssid, sizeof(arPriv->arBssid));
    A_MEMZERO(arPriv->arSsid, sizeof(arPriv->arSsid));
    A_MEMZERO(arPriv->arWepKeyList, sizeof(arPriv->arWepKeyList));
    arPriv->arSsidLen = 0;
    arPriv->arTxPwr              = 0;
    arPriv->arTxPwrSet           = FALSE;
    arPriv->arBitRate            = -1;
    arPriv->arMaxRetries         = 0;
    arPriv->arWmmEnabled         = TRUE;
    arPriv->ap_profile_flag      = 0;
    arPriv->num_sta              = 0xFF;
    ar->gNumSta                  = AP_MAX_NUM_STA;

    if(arPriv->arNextMode == AP_NETWORK) {
        AR_SOFTC_AP_T *arAp;
        if(arPriv->arNetworkType != AP_NETWORK) {
            A_MEMZERO(&arPriv->arSta,sizeof(AR_SOFTC_STA_T));
        }
        arAp = &arPriv->arAp;
        arAp->intra_bss     = 1;
        ar->inter_bss       = 1;

      /* init the Mutexes */
        A_NETBUF_QUEUE_INIT(&arAp->mcastpsq);
        A_MUTEX_INIT(&arAp->mcastpsqLock);
        A_MEMCPY(arAp->ap_country_code, DEF_AP_COUNTRY_CODE, 3);
        if (arPriv->arPhyCapability == WMI_11NAG_CAPABILITY){
        arPriv->phymode = DEF_AP_WMODE_AG;
        } else {
        arPriv->phymode = DEF_AP_WMODE_G;
        }
        arAp->ap_dtim_period = DEF_AP_DTIM;
        arAp->ap_beacon_interval = DEF_BEACON_INTERVAL;
        A_INIT_TIMER(&arPriv->ap_acs_timer,ap_acs_handler, arPriv);
        A_INIT_TIMER(&ar->ap_reconnect_timer,ap_reconnect_timer_handler, ar);
    } else {
      /*Station Mode intialisation*/
        AR_SOFTC_STA_T  *arSta;
        if(arPriv->arNetworkType == AP_NETWORK) {
            A_MEMZERO(&arPriv->arAp,sizeof(AR_SOFTC_AP_T));
        }
        arSta = &arPriv->arSta;
        arSta->arListenIntervalT    = A_DEFAULT_LISTEN_INTERVAL;
        arSta->arListenIntervalB    = 0;
        arSta->arBmissTimeT         = A_DEFAULT_BMISS_TIME;
        arSta->arBmissTimeB         = 0;
        arSta->arRssi               = 0;
        arSta->arSkipScan           = 0;
        arSta->arBeaconInterval     = 0;
        arSta->scan_triggered       = 0;
        A_MEMZERO(&arSta->scParams, sizeof(arSta->scParams));
        arSta->scParams.shortScanRatio = WMI_SHORTSCANRATIO_DEFAULT;
        arSta->scParams.scanCtrlFlags = DEFAULT_SCAN_CTRL_FLAGS;
        A_MEMZERO(arSta->arReqBssid, sizeof(arSta->arReqBssid));
        if (!arSta->disconnect_timer_inited) {
            A_INIT_TIMER(&arSta->disconnect_timer, disconnect_timer_handler, arPriv->arNetDev);
            arSta->disconnect_timer_inited = 1;
        }
        else
        {
            A_UNTIMEOUT(&arSta->disconnect_timer);
        }
    }
}

int
ar6000_ap_set_num_sta(AR_SOFTC_T *ar, AR_SOFTC_DEV_T *arPriv, A_UINT8 num_sta)
{
    int ret = A_OK;
    A_UINT8 i, total_num_sta;
    AR_SOFTC_DEV_T *tpriv = NULL;

    if(num_sta & 0x80) {
        total_num_sta = (num_sta & (~0x80));
        for(i=0; i<num_device; i++) {
            tpriv = (AR_SOFTC_DEV_T *)ar6k_priv(ar6000_devices[i]);
            tpriv->num_sta = 0xFF;
        }
    } else {
        total_num_sta = num_sta;
        arPriv->num_sta = num_sta;
        ar->gNumSta = 0xFF;
        for(i=0; i<num_device; i++) {
            tpriv = (AR_SOFTC_DEV_T *)ar6k_priv(ar6000_devices[i]);
            if((tpriv != arPriv) && (tpriv->num_sta !=0xFF) &&
              (tpriv->arNetworkType == AP_NETWORK)) {
                total_num_sta += tpriv->num_sta;
            }
        }
    }

    if(total_num_sta > AP_MAX_NUM_STA) {
        ret = -EINVAL;
    } else {
        if(num_sta & 0x80) {
            ar->gNumSta = (num_sta & (~0x80));
        } else {
            arPriv->num_sta = num_sta;
        }
        wmi_ap_set_num_sta(arPriv->arWmi, num_sta);
    }

    return ret;
}

int
check_channel(AR_SOFTC_DEV_T *arPriv)
{
    A_UINT8 i;
    AR_SOFTC_DEV_T *temp_priv = NULL;

    for(i=0; i<num_device; i++) {
        temp_priv = (AR_SOFTC_DEV_T *)ar6k_priv(ar6000_devices[i]);

        if(arPriv == temp_priv) continue;

        if((temp_priv->arNextMode == AP_NETWORK) && temp_priv->arConnected) {
            if(!(temp_priv->arBssChannel || temp_priv->arChannelHint)) {
                /* ACS inprogress */
                AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("AP%d: ACS in progress\n",temp_priv->arDeviceIndex));
                A_TIMEOUT_MS(&arPriv->ap_acs_timer, 1000, 0);
                return 0;
            }
        }
    }

    for(i=0; i<num_device; i++) {
        temp_priv = (AR_SOFTC_DEV_T *)ar6k_priv(ar6000_devices[i]);

        if(arPriv == temp_priv) continue;

        if(temp_priv->arConnected) {
            /* User has set the channel for this interface */
            if(arPriv->arChannelHint) {
                if(temp_priv->arBssChannel != arPriv->arChannelHint) {
                    AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Error: Channel should be %d MHz\n", temp_priv->arBssChannel));
                    return 0;
                }
            } else {
                /* ACS is enabled for this interface */
                if(temp_priv->arBssChannel) {
                    arPriv->arChannelHint = temp_priv->arBssChannel;
                    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("Selected Channel %d\n", temp_priv->arBssChannel));
                    return 1;
                }
            }
        }
    }
    return 1;
}

static void ap_acs_handler(unsigned long ptr)
{
    AR_SOFTC_DEV_T *arPriv = (AR_SOFTC_DEV_T *)ptr;
    A_UNTIMEOUT(&arPriv->ap_acs_timer);

    if(check_channel(arPriv)) {
        ar6000_ap_mode_profile_commit(arPriv);
    } else {
        A_TIMEOUT_MS(&arPriv->ap_acs_timer, 1000, 0);
    }
}

int
ar6000_ap_mode_profile_commit(AR_SOFTC_DEV_T *arPriv)
{
    AR_SOFTC_T *ar = arPriv->arSoftc;
    WMI_CONNECT_CMD p;
    unsigned long  flags;

    /* No change in AP's profile configuration */
    if(arPriv->ap_profile_flag==0) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("COMMIT: No change in profile!!!\n"));
        return -ENODATA;
    }

    if(!check_channel(arPriv)) {
        return -EOPNOTSUPP;
    }

    if(!arPriv->arSsidLen) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("SSID not set!!!\n"));
        return -ECHRNG;
    }

    switch(arPriv->arAuthMode) {
    case WMI_NONE_AUTH:
        if((arPriv->arPairwiseCrypto != NONE_CRYPT) &&
#ifdef WAPI_ENABLE
           (arPriv->arPairwiseCrypto != WAPI_CRYPT) &&
#endif
           (arPriv->arPairwiseCrypto != WEP_CRYPT)) {
            AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Cipher not supported in AP mode Open auth\n"));
            return -EOPNOTSUPP;
        }
        break;
    case WMI_WPA_PSK_AUTH:
    case WMI_WPA2_PSK_AUTH:
    case (WMI_WPA_PSK_AUTH|WMI_WPA2_PSK_AUTH):
        break;
    default:
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("This key mgmt type not supported in AP mode\n"));
        return -EOPNOTSUPP;
    }

    if ((arPriv->arAuthMode == WMI_NONE_AUTH) &&
        (arPriv->arPairwiseCrypto == WEP_CRYPT))
    {
        ar6000_install_static_wep_keys(arPriv);
    }

    /* Update the arNetworkType */
    arPriv->arNetworkType = arPriv->arNextMode;
    arPriv->arBssChannel = 0;

    A_MEMZERO(&p,sizeof(p));
    p.ssidLength = arPriv->arSsidLen;
    A_MEMCPY(p.ssid,arPriv->arSsid,p.ssidLength);

    /*
     * p.channel == 0 [Do ACS and choose 1, 6, or 11]
     * p.channel == 1 [Do ACS and choose 1, or 6]
     * p.channel == xxxx [No ACS, use xxxx freq]
     */
 if (((arPriv->phymode != WMI_11AG_MODE) && (arPriv->phymode != WMI_11A_MODE)) && (arPriv->arChannelHint >=5180 && arPriv->arChannelHint<= 5825)) {
        arPriv->arChannelHint = 0;
    }
    if ((arPriv->arChannelHint == 0) && (ar->arAcsDisableHiChannel)) {
        p.channel = 1;
    } else {
        p.channel = arPriv->arChannelHint;
        if ((arPriv->arChannelHint >=5180) && (arPriv->arChannelHint<= 5825)) {
            if (! (wmi_set_channelParams_cmd(arPriv->arWmi, 0, WMI_11A_MODE, 0, NULL))){
            arPriv->phymode= WMI_11A_MODE;
            }
        } else if ((arPriv->phymode == WMI_11A_MODE) || (arPriv->phymode == WMI_11AG_MODE)){
            if (! (wmi_set_channelParams_cmd(arPriv->arWmi, 0, WMI_11G_MODE, 0, NULL))){
            arPriv->phymode= WMI_11G_MODE;
           }
       }
    }

    p.networkType = arPriv->arNetworkType;
    p.dot11AuthMode = arPriv->arDot11AuthMode;
    p.authMode = arPriv->arAuthMode;
    p.pairwiseCryptoType = arPriv->arPairwiseCrypto;
    p.pairwiseCryptoLen = arPriv->arPairwiseCryptoLen;
    p.groupCryptoType = arPriv->arGroupCrypto;
    p.groupCryptoLen = arPriv->arGroupCryptoLen;
    p.ctrl_flags = arPriv->arSta.arConnectCtrlFlags;

#if WLAN_CONFIG_NO_DISASSOC_UPON_DEAUTH
    p.ctrl_flags |= AP_NO_DISASSOC_UPON_DEAUTH;
#endif
    wmi_ap_profile_commit(arPriv->arWmi, &p);
    spin_lock_irqsave(&arPriv->arPrivLock, flags);
    arPriv->arConnected  = TRUE;
    netif_carrier_on(arPriv->arNetDev);
    spin_unlock_irqrestore(&arPriv->arPrivLock, flags);
    arPriv->ap_profile_flag = 0;

    return 0;
}

A_STATUS
ar6000_connect_to_ap(AR_SOFTC_DEV_T *arPriv)
{
    AR_SOFTC_T     *ar    = arPriv->arSoftc;
    AR_SOFTC_STA_T *arSta = &arPriv->arSta;

    /* The ssid length check prevents second "essid off" from the user,
       to be treated as a connect cmd. The second "essid off" is ignored.
    */
    if((ar->arWmiReady == TRUE) && (arPriv->arSsidLen > 0) && arPriv->arNetworkType!=AP_NETWORK)
    {
        A_STATUS status;
        if((ADHOC_NETWORK != arPriv->arNetworkType) &&
           (WMI_NONE_AUTH==arPriv->arAuthMode)          &&
           (WEP_CRYPT==arPriv->arPairwiseCrypto)) {
                ar6000_install_static_wep_keys(arPriv);
        }

        if (!arSta->arUserBssFilter) {
            if (wmi_bssfilter_cmd(arPriv->arWmi, ALL_BSS_FILTER, 0) != A_OK) {
                return -EIO;
            }
        }
        AR_DEBUG_PRINTF(ATH_DEBUG_WLAN_CONNECT,("Connect called with authmode %d dot11 auth %d"\
                        " PW crypto %d PW crypto Len %d GRP crypto %d"\
                        " GRP crypto Len %d\n",
                        arPriv->arAuthMode, arPriv->arDot11AuthMode,
                        arPriv->arPairwiseCrypto, arPriv->arPairwiseCryptoLen,
                        arPriv->arGroupCrypto, arPriv->arGroupCryptoLen));
        reconnect_flag = 0;
        /* Set the listen interval into 1000TUs or more. This value will be indicated to Ap in the conn.
           later set it back locally at the STA to 100/1000 TUs depending on the power mode */
        if ((arPriv->arNetworkType == INFRA_NETWORK)) {
            wmi_listeninterval_cmd(arPriv->arWmi, max(arSta->arListenIntervalT, (A_UINT16)A_MAX_WOW_LISTEN_INTERVAL), 0);
        }
        status = wmi_connect_cmd(arPriv->arWmi, arPriv->arNetworkType,
                                 arPriv->arDot11AuthMode, arPriv->arAuthMode,
                                 arPriv->arPairwiseCrypto, arPriv->arPairwiseCryptoLen,
                                 arPriv->arGroupCrypto,arPriv->arGroupCryptoLen,
                                 arPriv->arSsidLen, arPriv->arSsid,
                                 arSta->arReqBssid, arPriv->arChannelHint,
                                 arSta->arConnectCtrlFlags);
        if (status != A_OK) {
            wmi_listeninterval_cmd(arPriv->arWmi, arSta->arListenIntervalT, arSta->arListenIntervalB);
            if (!arSta->arUserBssFilter) {
                wmi_bssfilter_cmd(arPriv->arWmi, NONE_BSS_FILTER, 0);
            }
            return status;
        }

        if ((!(arSta->arConnectCtrlFlags & CONNECT_DO_WPA_OFFLOAD)) &&
            ((WMI_WPA_PSK_AUTH == arPriv->arAuthMode) || (WMI_WPA2_PSK_AUTH == arPriv->arAuthMode)))
        {
            A_TIMEOUT_MS(&arSta->disconnect_timer, A_DISCONNECT_TIMER_INTERVAL, 0);
        }

        arSta->arConnectCtrlFlags &= ~CONNECT_DO_WPA_OFFLOAD;

        arSta->arConnectPending = TRUE;
        return status;
    }
    return A_ERROR;
}

A_STATUS
ar6000_disconnect(AR_SOFTC_DEV_T *arPriv)
{
    if ((arPriv->arConnected == TRUE) || (arPriv->arSta.arConnectPending == TRUE)) {
        wmi_disconnect_cmd(arPriv->arWmi);
#ifdef P2P
        if(arPriv->arNetworkSubType == SUBTYPE_P2PCLIENT) {
            wait_event_interruptible_timeout(arPriv->arEvent, arPriv->arConnected == FALSE, wmitimeout * HZ);

            if (signal_pending(current)) {
                return -EINTR;
            }
        }
#endif
        /*
         * Disconnect cmd is issued, clear connectPending.
         * arConnected will be cleard in disconnect_event notification.
         */
        arPriv->arSta.arConnectPending = FALSE;
    }

    return A_OK;
}

A_STATUS
ar6000_ap_mode_get_wpa_ie(AR_SOFTC_DEV_T *arPriv, struct ieee80211req_wpaie *wpaie)
{
    conn_t *conn = NULL;
    conn = ieee80211_find_conn(arPriv, wpaie->wpa_macaddr);

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
is_xioctl_allowed(A_UINT8 mode, A_UINT8 submode, int cmd)
{
    A_UINT8 mode_bits, submode_bits;
    A_BOOL is_valid_mode=FALSE, is_valid_submode=FALSE;

    if(sizeof(xioctl_filter)-1 < cmd) {
        AR_DEBUG_PRINTF(ATH_DEBUG_ERR,("Filter for this cmd=%d not defined\n",cmd));
        return A_OK;
    }

    /* Valid for all modes/submodes */
    if(xioctl_filter[cmd] == 0xFF) return A_OK;

    /* Check if this cmd is valid for the set mode of this device.
     */
#define XIOCTL_FILTER_MODE_MASK 0x1F
#define XIOCTL_FILTER_MODE_BIT_OFFSET 0x0
    mode_bits = xioctl_filter[cmd] & XIOCTL_FILTER_MODE_MASK;

    if (mode_bits & (mode << XIOCTL_FILTER_MODE_BIT_OFFSET)) {
        /* Valid cmd for this mode */
        is_valid_mode = TRUE;
    }

    /* Check if this cmd is valid for the set submode of this device.
     */
#define XIOCTL_FILTER_SUBMODE_MASK 0xE0
#define XIOCTL_FILTER_SUBMODE_BIT_OFFSET 0x0
    submode_bits = (xioctl_filter[cmd] & XIOCTL_FILTER_SUBMODE_MASK)>>XIOCTL_FILTER_SUBMODE_BIT_OFFSET;

    if (submode == SUBTYPE_P2PDEV || submode == SUBTYPE_P2PCLIENT ||
            submode == SUBTYPE_P2PGO) {
        /* P2P Submode */
        if (submode_bits & XIOCTL_FILTER_P2P_SUBMODE) {
            is_valid_submode = TRUE;
        }
    } else {
       /* Non P2P Sub mode */
        if ((submode_bits & XIOCTL_FILTER_NONP2P_SUBMODE)) {
            is_valid_submode = TRUE;
        }
    }

    if (is_valid_mode && is_valid_submode) {
        return A_OK;
    }

    return A_ERROR;
}

#ifdef WAPI_ENABLE
int
ap_set_wapi_key(AR_SOFTC_DEV_T *arPriv, void *ikey)
{
    struct ieee80211req_key *ik = (struct ieee80211req_key *)ikey;
    KEY_USAGE   keyUsage = 0;
    A_STATUS    status;

    if (A_MEMCMP(ik->ik_macaddr, bcast_mac, IEEE80211_ADDR_LEN) == 0) {
        keyUsage = GROUP_USAGE;
    } else {
        keyUsage = PAIRWISE_USAGE;
    }
    AR_DEBUG_PRINTF(ATH_DEBUG_INFO,("WAPI_KEY: Type:%d ix:%d mac:%02x:%02x len:%d\n",
        keyUsage, ik->ik_keyix, ik->ik_macaddr[4], ik->ik_macaddr[5],
        ik->ik_keylen));

    status = wmi_addKey_cmd(arPriv->arWmi, ik->ik_keyix, WAPI_CRYPT, keyUsage,
                            ik->ik_keylen, (A_UINT8 *)&ik->ik_keyrsc,
                            ik->ik_keydata, KEY_OP_INIT_VAL, ik->ik_macaddr,
                            SYNC_BOTH_WMIFLAG);

    if (A_OK != status) {
        return -EIO;
    }
    return 0;
}
#endif

#ifdef P2P

void ar6000_p2p_prov_disc_req_event(AR_SOFTC_DEV_T *arPriv,
        const A_UINT8 *peer, A_UINT16 wps_config_method,
        const A_UINT8 *dev_addr, const A_UINT8 *pri_dev_type,
        const A_UINT8 *dev_name, A_UINT8 dev_name_len,
        A_UINT16 supp_config_methods, A_UINT8 dev_capab, A_UINT8 group_capab)
{
    union iwreq_data wrqu;
    A_UINT8 buf[100];
    A_UINT8 *pos=buf;

    A_MEMZERO(pos, sizeof(buf));
    A_MEMCPY(pos, "P2PPROVDISCREQ", 14);
    pos += 14;

    A_MEMCPY(pos, peer, IEEE80211_ADDR_LEN);
    pos += IEEE80211_ADDR_LEN;

    A_MEMCPY(pos, dev_addr, IEEE80211_ADDR_LEN);
    pos += IEEE80211_ADDR_LEN;

    A_MEMCPY(pos, pri_dev_type, 8);
    pos += 8;

    A_MEMCPY(pos, dev_name, dev_name_len);
    pos += dev_name_len;
    *pos++ = '\0';

    A_MEMCPY(pos, (A_UINT8 *)&supp_config_methods, 2);
    pos += 2;

    A_MEMCPY(pos, (A_UINT8 *)&wps_config_method, 2);
    pos += 2;

    A_MEMCPY(pos, &dev_capab, 1);
    pos++;

    A_MEMCPY(pos, &group_capab, 1);
    pos++;

    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = (pos-buf);
    wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);
}

void ar6000_p2p_prov_disc_resp_event(AR_SOFTC_DEV_T *arPriv, A_UINT8 *peer,
         A_UINT16 config_methods)
{
    union iwreq_data wrqu;
    A_UINT8 buf[100];
    A_UINT8 *pos=buf;

    A_MEMZERO(pos, sizeof(buf));
    A_MEMCPY(pos, "P2PPROVDISCRESP", 15);
    pos += 15;

    A_MEMCPY(pos, peer, IEEE80211_ADDR_LEN);
    pos += IEEE80211_ADDR_LEN;

    A_MEMCPY(pos, (A_UINT8 *)&config_methods, 2);
    pos += 2;

    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = (pos-buf);
    wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);
}

void ar6000_p2pdev_event(AR_SOFTC_DEV_T *arPriv, const A_UINT8 *addr,
    const A_UINT8 *dev_addr,
    const A_UINT8 *pri_dev_type, const A_UINT8 *dev_name,
    A_UINT8 dev_name_len, A_UINT16 config_methods, A_UINT8 dev_capab,
    A_UINT8 grp_capab)
{
    union iwreq_data wrqu;
    A_UINT8 buf[100];
    A_UINT8 *pos=buf;

    A_MEMZERO(pos, sizeof(buf));
    A_MEMCPY(pos, "P2PDEVFOUND", 11);
    pos += 11;

    A_MEMCPY(pos, addr, IEEE80211_ADDR_LEN);
    pos += IEEE80211_ADDR_LEN;

    A_MEMCPY(pos, dev_addr, IEEE80211_ADDR_LEN);
    pos += IEEE80211_ADDR_LEN;

    /* Size of P2P Attributes hardcoded here. Can this be changed ?
     */
    A_MEMCPY(pos, pri_dev_type, 8);
    pos += 8;

    A_MEMCPY(pos, dev_name, dev_name_len);
    pos += dev_name_len;
    *pos++ = '\0';

    A_MEMCPY(pos, (A_UINT8 *)&config_methods, 2);
    pos += 2;

    A_MEMCPY(pos, &dev_capab, 1);
    pos++;

    A_MEMCPY(pos, &grp_capab, 1);
    pos++;

    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = (pos-buf);
    wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);

    return;
}

void ar6000_p2p_sd_rx_event(AR_SOFTC_DEV_T *arPriv, WMI_P2P_SDPD_RX_EVENT *ev)
{
    union iwreq_data wrqu;
    A_UINT8 *event_ptr;
    A_UINT8 *pos;
    A_UINT16 size;

#define P2P_SD_REQ_RESP_STR_LEN 12
    size = P2P_SD_REQ_RESP_STR_LEN
           + sizeof(WMI_P2P_SDPD_RX_EVENT)
           + ev->tlv_length;
    event_ptr = A_MALLOC_NOWAIT(size);
    pos = event_ptr;
    A_MEMZERO(pos, size);
    A_MEMCPY(pos, "P2PSDREQRESP", P2P_SD_REQ_RESP_STR_LEN);
    pos += P2P_SD_REQ_RESP_STR_LEN;
#undef P2P_SD_REQ_RESP_STR_LEN

    /* Copy the event followed by TLV, parsing will be dobe in supplicant */
    A_MEMCPY(pos, ev, sizeof(WMI_P2P_SDPD_RX_EVENT) + ev->tlv_length);

    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = size;
    wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, event_ptr);

    A_FREE(event_ptr);
    return;
}

void p2p_go_neg_event(AR_SOFTC_DEV_T *arPriv, A_UINT8 *res, A_UINT8 len)
{
    union iwreq_data wrqu;
    A_UINT8 buf[100];

    A_MEMZERO(&buf, sizeof(buf));
    A_MEMCPY(buf, "P2PNEGCOMPLETE",14);
    A_MEMCPY(&buf[14], res, len);

    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = 14+len;
    wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);
}

void p2p_go_neg_req_event(AR_SOFTC_DEV_T *arPriv, const A_UINT8 *sa, A_UINT16 dev_passwd_id)
{
    union iwreq_data wrqu;
    A_UINT8 buf[100];
    A_UINT8 *pos=buf;

    A_MEMZERO(pos, sizeof(buf));
    A_MEMCPY(pos, "P2PNEGREQEV", 11);
    pos += 11;

    A_MEMCPY(pos, sa, IEEE80211_ADDR_LEN);
    pos += IEEE80211_ADDR_LEN;

    A_MEMCPY(pos, (A_UINT8 *)&dev_passwd_id, 2);
    pos += 2;

    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = (pos-buf);
    wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);
}


void p2p_invite_sent_result_event(AR_SOFTC_DEV_T *arPriv, A_UINT8 *res,
        A_UINT8 len)
{
    union iwreq_data wrqu;
    A_UINT8 buf[100];

    A_MEMZERO(&buf, sizeof(buf));
    A_MEMCPY(buf, "P2PINVITESENTRESULT", 19);
    A_MEMCPY(&buf[19], res, len);

    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = 19 + len;
    wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);
}

void p2p_invite_rcvd_result_event(AR_SOFTC_DEV_T *arPriv,
            A_UINT8 *res, A_UINT8 len)
{
    union iwreq_data wrqu;
    A_UINT8 buf[100];

    A_MEMZERO(&buf, sizeof(buf));
    A_MEMCPY(buf, "P2PINVITERCVDRESULT", 19);
    A_MEMCPY(&buf[19], res, len);

    A_MEMZERO(&wrqu, sizeof(wrqu));
    wrqu.data.length = 19 + len;
    wireless_send_event(arPriv->arNetDev, IWEVCUSTOM, &wrqu, buf);
}

#endif /* P2P */

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

void ar6000_get_device_addr(AR_SOFTC_DEV_T *arPriv, A_UINT8 *addr)
{
    A_MEMCPY(addr, arPriv->arNetDev->dev_addr, IEEE80211_ADDR_LEN);
    return;
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
        cookie->arc_bp[0] = (A_UINT32)new_skb;
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

#ifdef RK29

#include <mach/board.h>

static struct wifi_platform_data *wifi_control_data = NULL;
struct semaphore wifi_control_sem;

int wifi_set_carddetect(int on)
{
	if (wifi_control_data && wifi_control_data->set_carddetect) {
		wifi_control_data->set_carddetect(on);
	}
	return 0;
}

int wifi_set_power(int on, unsigned long msec)
{
	if (wifi_control_data && wifi_control_data->set_power) {
		wifi_control_data->set_power(on);
	}
	if (msec)
		mdelay(msec);
	return 0;
}

int wifi_set_reset(int on, unsigned long msec)
{
	if (wifi_control_data && wifi_control_data->set_reset) {
		wifi_control_data->set_reset(on);
	}
	if (msec)
		mdelay(msec);
	return 0;
}

static int wifi_probe(struct platform_device *pdev)
{
	struct wifi_platform_data *wifi_ctrl =
		(struct wifi_platform_data *)(pdev->dev.platform_data);

	wifi_control_data = wifi_ctrl;

	/* make sure power-off */
	wifi_set_reset(0, 5);
	wifi_set_power(0, 5);

	wifi_set_power(1, 50);	/* Power On */
	wifi_set_reset(1, 150);	/* Reset */
	wifi_set_carddetect(1);	/* CardDetect (0->1) */

	up(&wifi_control_sem);
	return 0;
}

static int wifi_remove(struct platform_device *pdev)
{
	struct wifi_platform_data *wifi_ctrl =
		(struct wifi_platform_data *)(pdev->dev.platform_data);

	wifi_control_data = wifi_ctrl;

	wifi_set_reset(0, 5);	/* Reset */
	wifi_set_power(0, 5);	/* Power Off */
	wifi_set_carddetect(0);	/* CardDetect (1->0) */

	up(&wifi_control_sem);
	return 0;
}

static int wifi_suspend(struct platform_device *pdev, pm_message_t state)
{
	return 0;
}
static int wifi_resume(struct platform_device *pdev)
{
	return 0;
}

static struct platform_driver wifi_device = {
	.probe          = wifi_probe,
	.remove         = wifi_remove,
	.suspend        = wifi_suspend,
	.resume         = wifi_resume,
	.driver         = {
		.name   = "bcm4329_wlan",
	}
};

int wifi_add_dev(void)
{
	return platform_driver_register(&wifi_device);
}

void wifi_del_dev(void)
{
	platform_driver_unregister(&wifi_device);
}

static inline int _down_timeout(struct semaphore *sem, long jiffies) {
	int i = 0, ret = 0;
	static const int quantum_ms = 1000 / HZ;

	ret = down_trylock(sem);
	for (i = jiffies; (i > 0 && ret != 0); i -= quantum_ms) {
		schedule_timeout_interruptible(1);
		ret = down_trylock(sem);
	}

	return ret;
}

int rockchip_wifi_init_module(void)
{
	int error;
#if 0//def TCHIP
	extern char softmac_path[];
	char filename[256];
	extern int android_readwrite_file(const A_CHAR *filename, A_CHAR *rbuf, const A_CHAR *wbuf, size_t length);
	
	sprintf(filename, "%s/%s", softmac_path, "tcmd");
        if (android_readwrite_file(filename, NULL, NULL, 0) < 0) {
		printk("%s :: normal mode\n", __func__);
		testmode = 0;
        } else {
		printk("%s :: test mode\n", __func__);
		testmode = 1;
        }
#endif
	printk("ar6003 diver rkversion: [v1.10]\n");
	    
	sema_init(&wifi_control_sem, 0);
	error = wifi_add_dev();
	if (error) {
		printk("%s: platform_driver_register failed\n", __FUNCTION__);
		return -1;
	}

	/* Waiting callback after platform_driver_register is done or exit with error */
	if (_down_timeout(&wifi_control_sem,  msecs_to_jiffies(5000)) != 0) {
		error = -EINVAL;
		printk("%s: platform_driver_register timeout\n", __FUNCTION__);
		wifi_del_dev();
		return -1;
	}

        ifname[0] = 0;
        devmode[0] = 0;
	printk("ifname = %s, devmode = %s.\n", ifname, devmode);

	ar6000_init_module();
	ar6000_pm_init();

	return 0;
}
#include "wifi_launcher/wlan_param.h"
int rockchip_wifi_init_module_param(wlan_param *param)
{
	int error;
#if 0//def TCHIP
	extern char softmac_path[];
	char filename[256];
	extern int android_readwrite_file(const A_CHAR *filename, A_CHAR *rbuf, const A_CHAR *wbuf, size_t length);

	sprintf(filename, "%s/%s", softmac_path, "tcmd");
        if (android_readwrite_file(filename, NULL, NULL, 0) < 0) {
		printk("%s :: normal mode\n", __func__);
		testmode = 0;
        } else {
		printk("%s :: test mode\n", __func__);
		testmode = 1;
        }
#endif

	sema_init(&wifi_control_sem, 0);
	error = wifi_add_dev();
	if (error) {
		printk("%s: platform_driver_register failed\n", __FUNCTION__);
		return -1;
	}

	/* Waiting callback after platform_driver_register is done or exit with error */
	if (_down_timeout(&wifi_control_sem,  msecs_to_jiffies(5000)) != 0) {
		error = -EINVAL;
		printk("%s: platform_driver_register timeout\n", __FUNCTION__);
		wifi_del_dev();
		return -1;
	}	

	strcpy(ifname, param->ifname);
	strcpy(devmode, param->devmode);
	
	printk("ifname = %s, devmode = %s.\n", ifname, devmode);

	ar6000_init_module();
	ar6000_pm_init();

	return 0;
}
void rockchip_wifi_exit_module(void)
{
	ar6000_cleanup_module();
	ar6000_pm_exit();
	wifi_del_dev();
}
EXPORT_SYMBOL(rockchip_wifi_init_module);
EXPORT_SYMBOL(rockchip_wifi_init_module_param);
EXPORT_SYMBOL(rockchip_wifi_exit_module);
//module_init(rockchip_wifi_init_module);
//module_exit(rockchip_wifi_exit_module);
#endif

#ifdef EXPORT_HCI_BRIDGE_INTERFACE
EXPORT_SYMBOL(setupbtdev);
#endif
