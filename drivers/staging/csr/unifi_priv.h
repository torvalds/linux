/*
 *****************************************************************************
 *
 * FILE : unifi_priv.h
 *
 * PURPOSE : Private header file for unifi driver.
 *
 *           UDI = UniFi Debug Interface
 *
 * Copyright (C) 2005-2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 *****************************************************************************
 */
#ifndef __LINUX_UNIFI_PRIV_H__
#define __LINUX_UNIFI_PRIV_H__ 1

#include <linux/version.h>
#include <linux/module.h>
#include <linux/string.h>
#include <linux/errno.h>
#include <linux/kernel.h>
#include <linux/wait.h>
#include <linux/sched.h>
#include <linux/delay.h>
#include <linux/netdevice.h>
#include <linux/wireless.h>
#include <linux/cdev.h>
#include <linux/kthread.h>
#if LINUX_VERSION_CODE > KERNEL_VERSION(2,6,19)
#include <linux/freezer.h>
#endif

#ifdef CSR_WIFI_SUPPORT_MMC_DRIVER
#include <linux/mmc/core.h>
#include <linux/mmc/card.h>
#include <linux/mmc/host.h>
#include <linux/mmc/sdio_func.h>
#include <linux/mmc/sdio_ids.h>
#include <linux/mmc/sdio.h>
#endif /* CSR_WIFI_SUPPORT_MMC_DRIVER */

#include <linux/fs.h>

#ifdef ANDROID_BUILD
#include <linux/wakelock.h>
#endif

#include "csr_wifi_hip_unifi.h"
#include "csr_wifi_hip_unifi_udi.h"
#include "csr_wifi_router_lib.h"
#include "unifiio.h"
#ifndef CSR_WIFI_HIP_TA_DISABLE
#include "csr_wifi_vif_utils.h"
#endif

/* Define the unifi_priv_t before include the unifi_native.h */
struct unifi_priv;
typedef struct unifi_priv unifi_priv_t;
#ifdef CSR_SUPPORT_WEXT_AP
struct CsrWifiSmeApConfig;
typedef struct CsrWifiSmeApConfig CsrWifiSmeApConfig_t;
#endif
#ifdef CSR_SUPPORT_WEXT
#include "unifi_wext.h"
#endif

#ifdef ANDROID_BUILD
extern struct wake_lock unifi_sdio_wake_lock;
#endif

#include "unifi_clients.h"


#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,20)
#include <linux/workqueue.h>

#undef INIT_WORK
#define INIT_WORK(_work, _func)                                         \
    do {                                                                \
        INIT_LIST_HEAD(&(_work)->entry);                                \
        (_work)->pending = 0;                                           \
        PREPARE_WORK((_work), (_func), (_work));                        \
        init_timer(&(_work)->timer);                                    \
    } while(0)

#endif  /* Linux kernel < 2.6.20 */


#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,27)
#define UF_NETIF_TX_WAKE_ALL_QUEUES(_netdev)    netif_tx_wake_all_queues(_netdev)
#define UF_NETIF_TX_START_ALL_QUEUES(_netdev)   netif_tx_start_all_queues(_netdev)
#define UF_NETIF_TX_STOP_ALL_QUEUES(_netdev)    netif_tx_stop_all_queues(_netdev)
#else
#define UF_NETIF_TX_WAKE_ALL_QUEUES(_netdev)    netif_wake_queue(_netdev)
#define UF_NETIF_TX_START_ALL_QUEUES(_netdev)   netif_start_queue(_netdev)
#define UF_NETIF_TX_STOP_ALL_QUEUES(_netdev)    netif_stop_queue(_netdev)
#endif  /* Linux kernel >= 2.6.27 */


#ifdef CSR_NATIVE_LINUX
#include "sme_native/unifi_native.h"
#else
#include "unifi_sme.h"
#endif

/* The device major number to use when registering the udi driver */
#define UNIFI_NAME      "unifi"
/*
 * MAX_UNIFI_DEVS defines the maximum number of UniFi devices that can be present.
 * This number should be set to the number of SDIO slots supported by the SDIO
 * host controller on the platform.
 * Note: If MAX_UNIFI_DEVS value changes, fw_init[] needs to be corrected in drv.c
 */
#define MAX_UNIFI_DEVS  2

/* 802.11 Mac header offsets */
#define MAC_HEADER_SIZE  24
#define QOS_CONTROL_HEADER_SIZE 2
#define HT_CONTROL_HEADER_SIZE  4
#define QOS_DATA 0x8
#define QOS_DATA_NULL 0xc
#define DATA_NULL 0x04
#define FRAME_CONTROL_ORDER_BIT 0x8000
#define FRAME_CONTROL_TYPE_FIELD_OFFSET  2
#define FRAME_CONTROL_SUBTYPE_FIELD_OFFSET  4
#define IEEE802_11_FRAMETYPE_DATA 0x02
#define IEEE802_11_FRAMETYPE_CONTROL 0x01
#define IEEE802_11_FRAMETYPE_MANAGEMENT 0x00
#define IEEE802_11_FRAMETYPE_RESERVED 0x03

/* octet offset from start of mac header for certain fields */
#define IEEE802_11_ADDR3_OFFSET 16
#define IEEE802_11_SEQUENCE_CONTROL_OFFSET 22
#define IEEE802_11_MAX_DATA_LEN 2304

/* frame control (FC) masks, for frame control as 16 bit integer */
#define IEEE802_11_FC_TO_DS_MASK 0x100
#define IEEE802_11_FC_FROM_DS_MASK 0x200
#define IEEE802_11_FC_MOREDATA_MASK 0x2000
#define IEEE802_11_FC_PROTECTED_MASK 0x4000
#define IEEE80211_FC_ORDER_MASK 0x8000
#define IEEE80211_FC_SUBTYPE_MASK 0x00f0
#define IEEE80211_FC_TYPE_MASK 0x000c
#define IEEE80211_FC_PROTO_VERSION_MASK 0x0003

/*  selected type and subtype combinations as in 7.1.3.1 table 1
    For frame control as 16 bit integer, or for ls octet
*/
#define IEEE802_11_FC_TYPE_DATA 0x08
#define IEEE802_11_FC_TYPE_NULL 0x48
#define IEEE802_11_FC_TYPE_QOS_NULL 0xc8
#define IEEE802_11_FC_TYPE_QOS_DATA 0x88

#define IEEE802_11_FC_TYPE_DATA_SUBTYPE_RESERVED 0x0D

/* qos control (QC) masks for qos control as 16 bit integer, or for ls octet */
#define IEEE802_11_QC_TID_MASK 0x0f
#define IEEE802_11_QC_A_MSDU_PRESENT 0x80

#if (defined(CSR_WIFI_SECURITY_WAPI_ENABLE) && defined(CSR_WIFI_SECURITY_WAPI_QOSCTRL_MIC_WORKAROUND))
#define IEEE802_11_QC_NON_TID_BITS_MASK 0xFFF0
#endif

#define CSR_WIFI_EAPOL_M4_HOST_TAG 0x50000000
#define IEEE802_11_DATA_FRAME_MAC_HEADER_SIZE 36
#define MAX_ACCESS_CATOGORY 4

/* Time in us to check for inactivity of stations 5 mins */
#define INACTIVITY_CHECK_INTERVAL   300000000
/* Time in us before a station is flagged as inactive */
#define MAX_INACTIVITY_INTERVAL     300000000


/* Define for maximum BA session */
#define MAX_SUPPORTED_BA_SESSIONS_TX   1
#define MAX_SUPPORTED_BA_SESSIONS_RX   4

#define MAX_BA_WIND_SIZE 64
#define MAC_HEADER_ADDR1_OFFSET     4
#define MAC_HEADER_ADDR2_OFFSET     10

/* Define for age (in us) value for frames in MPDU reorder buffer */
#define CSR_WIFI_BA_MPDU_FRAME_AGE_TIMEOUT  30000 /* 30 milli seconds */

/* This macro used in prepare_and_add_macheader*/
#define ADDRESS_ONE_OFFSET 20

/* Defines for STA inactivity detection */
#define     STA_INACTIVE_DETECTION_TRIGGER_THRESHOLD           1                /* in number of stations */
#define     STA_INACTIVE_DETECTION_TIMER_INTERVAL              30               /* in seconds */
#define     STA_INACTIVE_TIMEOUT_VAL                           120*1000*1000    /* 120 seconds */

/* Test for modes requiring AP firmware patch */
#define CSR_WIFI_HIP_IS_AP_FW(mode) ((((mode) == CSR_WIFI_ROUTER_CTRL_MODE_AP) || \
                                      ((mode) == CSR_WIFI_ROUTER_CTRL_MODE_P2PGO)) ? TRUE : FALSE)

/* Defines used in beacon filtering in case of P2P */
#define CSR_WIFI_P2P_WILDCARD_SSID_LENGTH        0x7
#define CSR_WIFI_80211_FRAME_SUBTYPE_BEACON      0x8
#define CSR_WIFI_BEACON_FIXED_LENGTH             12
#define CSR_WIFI_FRAME_SUBTYPE_BIT_OFFSET        4
#define CSR_WIFI_80211_FRAME_SUBTYPE_BIT_MASK    ((u8)(0xF << CSR_WIFI_FRAME_SUBTYPE_BIT_OFFSET))

#define CSR_WIFI_80211_GET_FRAME_SUBTYPE(frameBuffer) \
    ((u8)(((u8 *)frameBuffer)[0] & CSR_WIFI_80211_FRAME_SUBTYPE_BIT_MASK) >> CSR_WIFI_FRAME_SUBTYPE_BIT_OFFSET)

/* For M4 request received via netdev*/

typedef u8 CsrWifiPacketType;
#define CSR_WIFI_UNICAST_PDU   ((CsrWifiPacketType) 0x00)
#define CSR_WIFI_MULTICAST_PDU ((CsrWifiPacketType) 0x1)
#define CSR_WIFI_BROADCAST_PDU ((CsrWifiPacketType) 0x2)

#define PRIO_TO_NICE(prio)  ((prio) - MAX_RT_PRIO - 20)

/* Module parameter variables */
extern int buswidth;
extern int sdio_clock;
extern int use_5g;
extern int disable_hw_reset;
extern int disable_power_control;
extern int enable_wol;
extern int sme_debug;
extern int fw_init[MAX_UNIFI_DEVS];
extern int tl_80211d;
extern int sdio_byte_mode;
extern int sdio_block_size;
extern int coredump_max;
extern int run_bh_once;
extern int bh_priority;
#ifdef CSR_WIFI_HIP_DEBUG_OFFLINE
extern int log_hip_signals;
#endif

struct dlpriv {
    const unsigned char *dl_data;
    int dl_len;
    void *fw_desc;
};


struct uf_thread {

    struct task_struct *thread_task;

    /* wait_queue for waking the unifi_thread kernel thread */
    wait_queue_head_t wakeup_q;
    unsigned int wakeup_flag;

    /*
     * Use it to block the I/O thread when
     * an error occurs or UniFi is reinitialised.
     */
    int block_thread;

    char name[16];
    int prio;
};

/*
 * Link list to hold the received packets for the period the port
 * remains closed.
 */
typedef struct rx_buffered_packets {
    /* List link structure */
    struct list_head q;
    /* Packet to indicate when the port reopens */
    struct sk_buff *skb;
    /* Bulkdata to free in case the port closes and need to discard the packet */
    bulk_data_param_t bulkdata;
    /* The source address of the packet */
    CsrWifiMacAddress sa;
    /* The destination address of the packet */
    CsrWifiMacAddress da;
    /* Corresponding signal */
    CSR_SIGNAL signal;
} rx_buffered_packets_t;


typedef u8 CsrWifiAcPowersaveMode;
#define CSR_WIFI_AC_TRIGGER_ONLY_ENABLED 0x00
#define CSR_WIFI_AC_DELIVERY_ONLY_ENABLE 0X01
#define CSR_WIFI_AC_TRIGGER_AND_DELIVERY_ENABLED 0X03
#define CSR_WIFI_AC_LEGACY_POWER_SAVE 0X02


#define IS_DELIVERY_ENABLED(mode) (mode & CSR_WIFI_AC_DELIVERY_ONLY_ENABLE)? 1: 0
#define IS_DELIVERY_AND_TRIGGER_ENABLED(mode) ((mode & CSR_WIFI_AC_DELIVERY_ONLY_ENABLE)||(mode & CSR_WIFI_AC_TRIGGER_AND_DELIVERY_ENABLED))? 1: 0
#define IS_DTIM_ACTIVE(flag,hostTag) ((flag == TRUE || hostTag != INVALID_HOST_TAG))
#define INVALID_HOST_TAG 0xFFFFFFFF
#define UNIFI_TRAFFIC_Q_CONTENTION UNIFI_TRAFFIC_Q_BE




/* Queue to be used for contention priority */

/*
 * Link list to hold the tx packets for the period the peer
 * powersave/free slots in unifi
 */
typedef struct tx_buffered_packets {
    /* List link structure */
    struct list_head q;
    u16 interfaceTag;
    CSR_CLIENT_TAG hostTag;
    CSR_PROCESS_ID leSenderProcessId;
    CSR_TRANSMISSION_CONTROL transmissionControl;
    CSR_RATE rate;
    /* Bulkdata to free in case the port closes and need to discard the packet */
    bulk_data_desc_t bulkdata;
    /* The source address of the packet */
    CsrWifiMacAddress peerMacAddress;
    CSR_PRIORITY priority;
} tx_buffered_packets_t;

/* station record has this data structure */
typedef struct CsrWifiRouterCtrlStaInfo_t {

    /* Sme sends these parameters */
    CsrWifiMacAddress peerMacAddress;
    u32 assignedHandle;
    CsrBool wmmOrQosEnabled;
    CsrWifiAcPowersaveMode powersaveMode[MAX_ACCESS_CATOGORY];
    u16 maxSpLength;
    CsrBool uapsdActive;
    u16 noOfSpFramesSent;

    /* Router/Driver database */
#ifdef CSR_SUPPORT_SME
    unifi_port_cfg_t *peerControlledPort;
    unifi_port_cfg_t *peerUnControlledPort;

    /* Inactivity feature parameters */
    struct netInterface_priv *interfacePriv;
    struct work_struct send_disconnected_ind_task;
    CsrBool activity_flag;
    u16 listenIntervalInTus;
    CSR_CLIENT_TAG nullDataHostTag;

    /* Activity timestamps for the station */
    CsrTime lastActivity;

    /* during m/c transmission sp suspended */
    CsrBool uspSuspend;
    CSR_PRIORITY triggerFramePriority;
#endif
    CsrWifiRouterCtrlPeerStatus currentPeerState;
    struct list_head dataPdu[MAX_ACCESS_CATOGORY];
    struct list_head mgtFrames;
    u8 spStatus;
    u8 prevFrmType;
    u8 prevFrmAccessCatogory;
    CsrBool protection;
    u16 aid;
    CsrBool txSuspend;
    u8 timSet;
    /* Dont change the value of below macro for SET & RESET */
#define CSR_WIFI_TIM_RESET       0
#define CSR_WIFI_TIM_SET         1
#define CSR_WIFI_TIM_RESETTING   2
#define CSR_WIFI_TIM_SETTING     3

    CsrBool timRequestPendingFlag;
    u8 updateTimReqQueued;
    u16 noOfPktQueued;
}CsrWifiRouterCtrlStaInfo_t;

#ifdef CSR_SUPPORT_WEXT_AP
struct CsrWifiSmeApConfig {
    CsrWifiSsid ssid;
    u16 channel;
    CsrWifiNmeApCredentials credentials;
    u8 max_connections;
    u8 if_index;
};
#endif

#ifdef CSR_WIFI_RX_PATH_SPLIT
/* This is a test code and may be removed later*/
#define CSR_WIFI_RX_SIGNAL_BUFFER_SIZE    (60+1)

typedef struct
{
    u8 *bufptr; /* Signal Primitive */
    bulk_data_param_t data_ptrs; /* Bulk Data pointers */
    u16 sig_len;
}rx_buff_struct_t;

typedef struct
{
    u8 writePointer; /**< write pointer */
    u8 readPointer;  /**< read pointer */
    u8 size;         /**< size of circular buffer */
    rx_buff_struct_t rx_buff[CSR_WIFI_RX_SIGNAL_BUFFER_SIZE];    /**< Element of ciruclar buffer */
} rxCircularBuffer_t;

void rx_wq_handler(struct work_struct *work);
#endif

struct unifi_priv {

    card_t *card;
    CsrSdioFunction *sdio;

    /* Index into Unifi_instances[] for this device. */
    int instance;
    /* Reference count for this instance */
    int ref_count;

    /* Firmware images */
    struct dlpriv fw_sta;
    struct dlpriv fw_conv;  /* used for conversion of production test image */

    /* Char device related structures */
    struct cdev unifi_cdev;
    struct cdev unifiudi_cdev;
    struct device *unifi_device;

    /* Which wireless interface to use (1 - 2.4GHz, 2 - 5GHz) */
    CSR_IFINTERFACE if_index;

    /* For multiple interface support */
    struct net_device *netdev[CSR_WIFI_NUM_INTERFACES];
    struct netInterface_priv *interfacePriv[CSR_WIFI_NUM_INTERFACES];

    u8 totalInterfaceCount;

    int prev_queue;

    /* Name of node under /proc */
    char proc_entry_name[64];

    /*
     * Flags:
     *  drop_unencrypted
     *                - Not used?
     *  netdev_registered
     *                - whether the netdev has been registered.
     */
    unsigned int drop_unencrypted       : 1;

    /* Our list of unifi linux clients. */
    ul_client_t ul_clients[MAX_UDI_CLIENTS];

    /* Mutex to protect using the logging hook after UDI client is gone */
    struct semaphore udi_logging_mutex;
    /* Pointer to the ul_clients[] array */
    ul_client_t *logging_client;

    /* A ul_client_t* used to send the netdev related MIB requests. */
    ul_client_t *netdev_client;

    /* The SME ul_client_t pointer. */
    ul_client_t *sme_cli;

    /* The AMP ul_client_t pointer. */
    ul_client_t *amp_client;

    /*
     * Semaphore for locking the top-half to one user process.
     * This is necessary to prevent multiple processes calling driver
     * operations. This can happen because the network driver entry points
     * can be called from multiple processes.
     */
#ifdef USE_DRIVER_LOCK
    struct semaphore lock;
#endif /* USE_DRIVER_LOCK */

    /* Flag to say that an operation was aborted */
    int io_aborted;

    struct uf_thread bh_thread;

#define UNIFI_INIT_NONE         0x00
#define UNIFI_INIT_IN_PROGRESS  0x01
#define UNIFI_INIT_FW_DOWNLOADED 0x02
#define UNIFI_INIT_COMPLETED    0x04
    unsigned char init_progress;

    int sme_is_present;

    /* The WMM features that UniFi uses in the current BSS */
    unsigned int sta_wmm_capabilities;

    /* Debug only */
    char last_debug_string[256];
    unsigned short last_debug_word16[16];

#ifdef CSR_SUPPORT_SME
  /* lock to protect the tx queues list */
    spinlock_t tx_q_lock;
    u8 allPeerDozing;
    u8 pausedStaHandle[MAX_ACCESS_CATOGORY];
    /* Max packet the driver can queue, irrespective of interface number */
    u16 noOfPktQueuedInDriver;
#define CSR_WIFI_DRIVER_SUPPORT_FOR_MAX_PKT_QUEUEING 512
#define CSR_WIFI_DRIVER_MAX_PKT_QUEUING_THRESHOLD_PER_PEER 64
#define CSR_WIFI_DRIVER_MINIMUM_BROADCAST_PKT_THRESHOLD 3

    CsrBool routerBufferEnable[MAX_ACCESS_CATOGORY];
    /* lock to protect stainfo members and priv members*/
    spinlock_t staRecord_lock;
#endif
#ifdef CSR_NATIVE_LINUX
#ifdef CSR_SUPPORT_WEXT
    /* wireless config */
    struct wext_config wext_conf;
#endif

    /* Mutex to protect the MLME blocking requests */
    struct semaphore mlme_blocking_mutex;

    /* The ul_client that provides the blocking API for WEXT calls */
    ul_client_t *wext_client;

#endif /* CSR_NATIVE_LINUX */

#ifdef CSR_SUPPORT_SME
    wait_queue_head_t sme_request_wq;
    /* Semaphore to protect the SME blocking requests */
    struct semaphore sme_sem;
    /* Structure to hold the SME blocking requests data*/
    sme_reply_t sme_reply;

    /* Structure to hold a traffic protocol indication */
    struct ta_ind {
        struct work_struct task;
        CsrWifiRouterCtrlTrafficPacketType packet_type;
        CsrWifiRouterCtrlProtocolDirection direction;
        CsrWifiMacAddress src_addr;
        int in_use;
    } ta_ind_work;

    struct ta_sample_ind {
        struct work_struct task;
        CsrWifiRouterCtrlTrafficStats stats;
        int in_use;
    } ta_sample_ind_work;

    __be32 sta_ip_address;
    CsrWifiRouterCtrlSmeVersions    sme_versions;

    /*
     * Flag to reflect state of unifi_sys_wifi_on_*() progress.
     * This indicates whether we are in an "wifi on" state when we are
     * allowed to indication errors with unifi_mgt_wifi_off_ind()
     */
    enum {
        wifi_on_unspecified = -1,
        wifi_on_in_progress = 0,
        wifi_on_done = 1,
    } wifi_on_state;

    /* Userspace TaskId for the SME Set when a wifi on req is received */
    CsrSchedQid CSR_WIFI_SME_IFACEQUEUE;

    struct work_struct multicast_list_task;
    /*
     * The SME installs filters to ask for specific MA-UNITDATA.req
     * to be passed to different SME components.
     */
#define MAX_MA_UNIDATA_IND_FILTERS      8
    sme_ma_unidata_ind_filter_t sme_unidata_ind_filters[MAX_MA_UNIDATA_IND_FILTERS];

/* UNIFI_CFG related parameters */
    uf_cfg_bcast_packet_filter_t packet_filters;
    unsigned char *filter_tclas_ies;
 /* The structure that holds all the connection configuration. */
    CsrWifiSmeConnectionConfig connection_config;
#ifdef CSR_SUPPORT_WEXT

    int ignore_bssid_join;
    struct iw_statistics wext_wireless_stats;

    /* The MIB and MAC address files contents, read from userspace */
    CsrWifiSmeDataBlock mib_data;
    CsrWifiMacAddress sta_mac_address;

    int wep_tx_key_index;
    wep_key_t wep_keys[NUM_WEPKEYS];


#ifdef CSR_SUPPORT_WEXT_AP
    CsrWifiSmeApMacConfig ap_mac_config;
    CsrWifiNmeApConfig group_sec_config;
    CsrWifiSmeApConfig_t ap_config;
#endif
    struct work_struct sme_config_task;

#endif /* CSR_SUPPORT_WEXT */

#endif /* CSR_SUPPORT_SME */

#ifdef CSR_SME_USERSPACE
    void *smepriv;
#endif /* CSR_SME_USERSPACE */

    card_info_t card_info;

    /* Mutex to protect unifi_send_signal() */
    spinlock_t send_signal_lock;


    /*
     * The workqueue to offload the TA run
     * and the multicast addresses list set
     */
    struct workqueue_struct *unifi_workqueue;

    unsigned char *mib_cfm_buffer;
    unsigned int mib_cfm_buffer_length;

    int ptest_mode;     /* Set when in production test mode */
    int coredump_mode;  /* Set when SME has requested a coredump */
    CsrBool wol_suspend; /* Set when suspending with UniFi powered */

#define UF_UNCONTROLLED_PORT_Q      0
#define UF_CONTROLLED_PORT_Q        1

    /* Semaphore to protect the rx queues list */
    struct semaphore rx_q_sem;

    /* Spinlock to protect M4 data */
    spinlock_t m4_lock;
    /* Mutex to protect BA RX data */
    struct semaphore ba_mutex;

#if (defined(CSR_WIFI_SECURITY_WAPI_ENABLE) && defined(CSR_WIFI_SECURITY_WAPI_SW_ENCRYPTION))
    /* Spinlock to protect the WAPI data */
    spinlock_t wapi_lock;
#endif

#ifndef ALLOW_Q_PAUSE
    /* Array to indicate if a particular Tx queue is paused, this may not be
     * required in a multiqueue implementation since we can directly stop kernel
     * queues */
    u8 tx_q_paused_flag[UNIFI_TRAFFIC_Q_MAX];
#endif

#ifdef CSR_WIFI_RX_PATH_SPLIT
    struct workqueue_struct *rx_workqueue;
    struct work_struct rx_work_struct;
    rxCircularBuffer_t rxSignalBuffer;

#endif

    u32 rxTcpThroughput;
    u32 txTcpThroughput;
    u32 rxUdpThroughput;
    u32 txUdpThroughput;

#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE
    /*Set if multicast KeyID = 1*/
    u8 wapi_multicast_filter;
    /*Set if unicast KeyID = 1*/
    u8 wapi_unicast_filter;
    u8 wapi_unicast_queued_pkt_filter;
#ifdef CSR_WIFI_SECURITY_WAPI_QOSCTRL_MIC_WORKAROUND
    CsrBool  isWapiConnection;
#endif
#endif

#ifdef CSR_WIFI_SPLIT_PATCH
    CsrWifiRouterCtrlModeSetReq pending_mode_set;
#endif

    CsrBool cmanrTestMode;
    CSR_RATE cmanrTestModeTransmitRate;

};

typedef struct {
    u16 queue_length[4];
    u8 os_queue_paused;
} unifi_OsQosInfo;


typedef struct {
    CsrBool active;
    bulk_data_param_t bulkdata;
    CSR_SIGNAL signal;
    u16 sn;
    CsrTime recv_time;
} frame_desc_struct;

typedef struct {
    frame_desc_struct *buffer;
    u16 wind_size;
    u16 occupied_slots;
    struct timer_list timer;
    u16 timeout;
    u16 expected_sn;
    u16 start_sn;
    CsrBool   trigger_ba_after_ssn;
    struct netInterface_priv *interfacePriv;
    u16 tID;
    CsrWifiMacAddress macAddress;
    struct work_struct send_ba_err_task;
} ba_session_rx_struct;


typedef struct {
    struct netInterface_priv *interfacePriv;
    u16 tID;
    CsrWifiMacAddress macAddress;
} ba_session_tx_struct;

typedef struct netInterface_priv
{
    u16 InterfaceTag;
    struct unifi_priv *privPtr;
    ba_session_tx_struct *ba_session_tx[MAX_SUPPORTED_BA_SESSIONS_TX];
    ba_session_rx_struct *ba_session_rx[MAX_SUPPORTED_BA_SESSIONS_RX];
    frame_desc_struct ba_complete[MAX_BA_WIND_SIZE];
    u8 ba_complete_index;
    u8 queueEnabled[UNIFI_NO_OF_TX_QS];
    struct work_struct send_m4_ready_task;
#ifdef CSR_WIFI_SECURITY_WAPI_ENABLE
    struct work_struct send_pkt_to_encrypt;
#endif
    struct net_device_stats stats;
    u8 interfaceMode;
    CsrBool protect;
    CsrWifiMacAddress bssid;
    /*
    * Flag to reflect state of CONNECTED indication signal.
    * This indicates whether we are "joined" an Access Point (i.e. have
    * nominated an AP and are receiving beacons) but give no indication
    * of whether we are authenticated and/or associated.
    */
    enum {
        UnifiConnectedUnknown = -1,
        UnifiNotConnected = 0,
        UnifiConnected = 1,
    } connected;
#ifdef CSR_SUPPORT_WEXT
    /* Tracks when we are waiting for a netdevice state change callback */
    CsrBool wait_netdev_change;
    /* True if we have successfully registered for netdev callbacks */
    CsrBool netdev_callback_registered;
#endif /* CSR_SUPPORT_WEXT */
    unsigned int netdev_registered;
#define UNIFI_MAX_MULTICAST_ADDRESSES 10
    /* The multicast addresses list that the thread needs to set. */
    u8 mc_list[UNIFI_MAX_MULTICAST_ADDRESSES*ETH_ALEN];
    /* The multicast addresses count that the thread needs to set. */
    int mc_list_count;
    u32 tag;
#ifdef CSR_SUPPORT_SME
    /* (un)controlled port configuration */
    unifi_port_config_t controlled_data_port;
    unifi_port_config_t uncontrolled_data_port;

    /* station record maintenance related data structures */
    u8 num_stations_joined;
    CsrWifiRouterCtrlStaInfo_t *(staInfo)[UNIFI_MAX_CONNECTIONS];
    struct list_head genericMgtFrames;
    struct list_head genericMulticastOrBroadCastFrames;
    struct list_head genericMulticastOrBroadCastMgtFrames;

    /* Timer for detecting station inactivity */
    struct timer_list sta_activity_check_timer;
    CsrBool sta_activity_check_enabled;

    /* Timestamp when the last inactivity check was done */
    CsrTime last_inactivity_check;

    /*number of multicast or borad cast packets  queued*/
    u16 noOfbroadcastPktQueued;
#endif
    /* A list to hold the buffered uncontrolled port packets */
    struct list_head rx_uncontrolled_list;
    /* A list to hold the buffered controlled port packets */
    struct list_head rx_controlled_list;
    /* Buffered M4 signal to take care of WPA race condition */
    CSR_SIGNAL m4_signal;
    bulk_data_desc_t m4_bulk_data;

#if (defined(CSR_WIFI_SECURITY_WAPI_ENABLE) && defined(CSR_WIFI_SECURITY_WAPI_SW_ENCRYPTION))
    /* Buffered WAPI Unicast MA Packet Request for encryption in Sme */
    CSR_SIGNAL wapi_unicast_ma_pkt_sig;
    bulk_data_desc_t wapi_unicast_bulk_data;
#endif

    /* This should be removed and m4_hostTag should be used for checking*/
    CsrBool m4_sent;
    CSR_CLIENT_TAG m4_hostTag;
    CsrBool dtimActive;
    CsrBool intraBssEnabled;
    u32 multicastPduHostTag; /* Used to set the tim after getting
       a confirm for it */
    CsrBool bcTimSet;
    CsrBool bcTimSetReqPendingFlag;
    CsrBool bcTimSetReqQueued;
} netInterface_priv_t;

#ifndef ALLOW_Q_PAUSE
#define net_is_tx_q_paused(priv, q)   (priv->tx_q_paused_flag[q])
#define net_tx_q_unpause(priv, q)   (priv->tx_q_paused_flag[q] = 0)
#define net_tx_q_pause(priv, q)   (priv->tx_q_paused_flag[q] = 1)
#endif

#ifdef CSR_SUPPORT_SME
#define routerStartBuffering(priv,queue) priv->routerBufferEnable[(queue)] = TRUE;
#define routerStopBuffering(priv,queue) priv->routerBufferEnable[(queue)]  = FALSE;
#define isRouterBufferEnabled(priv,queue) priv->routerBufferEnable[(queue)]
#endif

#ifdef USE_DRIVER_LOCK
#define LOCK_DRIVER(_p)         down_interruptible(&(_p)->lock)
#define UNLOCK_DRIVER(_p)       up(&(_p)->lock)
#else
#define LOCK_DRIVER(_p)         (void)(_p); /* as nothing */
#define UNLOCK_DRIVER(_p)       (void)(_p); /* as nothing */
#endif /* USE_DRIVER_LOCK */

CsrInt32 CsrHipResultToStatus(CsrResult csrResult);


/*
 * SDIO related functions and callbacks
 */
int  uf_sdio_load(void);
void uf_sdio_unload(void);
unifi_priv_t *uf_find_instance(int inst);
int uf_find_priv(unifi_priv_t *priv);
int uf_find_netdev_priv(netInterface_priv_t *priv);
unifi_priv_t *uf_get_instance(int inst);
void uf_put_instance(int inst);
int csr_sdio_linux_install_irq(CsrSdioFunction *sdio);
int csr_sdio_linux_remove_irq(CsrSdioFunction *sdio);

void uf_add_os_device(int bus_id, struct device *os_device);
void uf_remove_os_device(int bus_id);



/*
 * Claim/release SDIO
 *
 * For multifunction cards, we cannot grub the SDIO lock around the unifi_bh()
 * as this prevents other functions using SDIO.
 * Since some of CSR SDIO API is used regardless of trying to lock unifi_bh()
 * we have followed this scheme:
 * 1. If a function needs protection only when CSR_WIFI_SINGLE_FUNCTION is defined
 *    then we call CsrSdioClaim/CsrSdioRelease().
 * 2. If a function needs protection only when CSR_WIFI_SINGLE_FUNCTION is not defined
 *    then we call _sdio_claim_host/_sdio_claim_host(). Use of this should be restricted
 *    to the SDIO glue layer only (e.g. sdio_mmc.c).
 * 3. If a function needs protection, regardless of the CSR_WIFI_SINGLE_FUNCTION
 *    then we call directly the sdio_claim_host/sdio_release_host().
 *    Use of this must be restricted to the SDIO glue layer only (e.g. sdio_mmc.c).
 *
 * Note: The _func and function pointers are _not_ the same.
 * The former is the (struct sdio_func*) context, which restricts the use to the SDIO glue layer.
 * The latter is the (CsrSdioFunction*) context, which allows calls from all layers.
 */

#ifdef CSR_WIFI_SUPPORT_MMC_DRIVER

#ifdef CSR_WIFI_SINGLE_FUNCTION
#define CsrSdioClaim(function)    sdio_claim_host((function)->priv);
#define CsrSdioRelease(function)  sdio_release_host((function)->priv);

#define _sdio_claim_host(_func)
#define _sdio_release_host(_func)

#else
#define CsrSdioClaim(function)
#define CsrSdioRelease(function)

#define _sdio_claim_host(_func)     sdio_claim_host(_func)
#define _sdio_release_host(_func)   sdio_release_host(_func)

#endif /* CSR_WIFI_SINGLE_FUNCTION */

#else
#define _sdio_claim_host(_func)
#define _sdio_release_host(_func)

#define CsrSdioClaim(function)
#define CsrSdioRelease(function)

#endif /* CSR_WIFI_SUPPORT_MMC_DRIVER */


/*
 * Functions to allocate and free an ethernet device.
 */
unifi_priv_t *uf_alloc_netdevice(CsrSdioFunction *sdio_dev, int bus_id);
int uf_free_netdevice(unifi_priv_t *priv);

/* Allocating function for other interfaces */
CsrBool uf_alloc_netdevice_for_other_interfaces(unifi_priv_t *priv, u16 interfaceTag);

/*
 * Firmware download related functions.
 */
int uf_run_unifihelper(unifi_priv_t *priv);
int uf_request_firmware_files(unifi_priv_t *priv, int is_fw);
int uf_release_firmware_files(unifi_priv_t *priv);
int uf_release_firmware(unifi_priv_t *priv, struct dlpriv *to_free);

/*
 * Functions to create and delete the device nodes.
 */
int uf_create_device_nodes(unifi_priv_t *priv, int bus_id);
void uf_destroy_device_nodes(unifi_priv_t *priv);

/*
 * Upper Edge Initialisation functions
 */
int uf_init_bh(unifi_priv_t *priv);
int uf_init_hw(unifi_priv_t *priv);

/* Thread related helper functions */
int uf_start_thread(unifi_priv_t *priv, struct uf_thread *thread, int (*func)(void *));
void uf_stop_thread(unifi_priv_t *priv, struct uf_thread *thread);
void uf_wait_for_thread_to_stop(unifi_priv_t *priv, struct uf_thread *thread);


/*
 * Unifi Linux functions
 */
void ul_init_clients(unifi_priv_t *priv);

/* Configuration flags */
#define CLI_USING_WIRE_FORMAT   0x0002
#define CLI_SME_USERSPACE       0x0020
ul_client_t *ul_register_client(unifi_priv_t *priv,
        unsigned int configuration,
        udi_event_t udi_event_clbk);
int ul_deregister_client(ul_client_t *pcli);

int ul_send_signal_unpacked(unifi_priv_t *priv,
                            CSR_SIGNAL *sigptr,
                            bulk_data_param_t *bulkdata);
int ul_send_signal_raw(unifi_priv_t *priv,
                       unsigned char *sigptr, int siglen,
                       bulk_data_param_t *bulkdata);

void ul_log_config_ind(unifi_priv_t *priv, u8 *conf_param, int len);


/*
 * Data plane operations
 */
/*
 *      data_tx.c
 */
int uf_verify_m4(unifi_priv_t *priv, const unsigned char *packet,
        unsigned int length);

#ifdef CSR_SUPPORT_SME
CsrBool uf_check_broadcast_bssid(unifi_priv_t *priv, const bulk_data_param_t *bulkdata);
CsrBool uf_process_pm_bit_for_peer(unifi_priv_t * priv, CsrWifiRouterCtrlStaInfo_t * srcStaInfo,u8 pmBit,u16 interfaceTag);
void uf_process_ps_poll(unifi_priv_t *priv,u8* sa,u8* da,u8 pmBit,u16 interfaceTag);
int uf_ap_process_data_pdu(unifi_priv_t *priv, struct sk_buff *skb,
                   struct ethhdr *ehdr, CsrWifiRouterCtrlStaInfo_t * srcStaInfo,
                   const CSR_SIGNAL *signal,
                   bulk_data_param_t *bulkdata,
                   u8 macHeaderLengthInBytes);
CsrBool uf_is_more_data_for_non_delivery_ac(CsrWifiRouterCtrlStaInfo_t *staRecord);
void uf_process_wmm_deliver_ac_uapsd (  unifi_priv_t * priv,
                                        CsrWifiRouterCtrlStaInfo_t * srcStaInfo,
                                        u16 qosControl,
                                        u16 interfaceTag);

void uf_send_buffered_data_from_ac(unifi_priv_t *priv, CsrWifiRouterCtrlStaInfo_t * staInfo, u8 queue, struct list_head *txList);
void uf_send_buffered_data_from_delivery_ac(unifi_priv_t *priv, CsrWifiRouterCtrlStaInfo_t * staInfo, u8 queue, struct list_head *txList);

void uf_continue_uapsd(unifi_priv_t *priv, CsrWifiRouterCtrlStaInfo_t * staInfo);
void uf_send_qos_null(unifi_priv_t * priv,u16 interfaceTag, const u8 *da,CSR_PRIORITY priority,CsrWifiRouterCtrlStaInfo_t * srcStaInfo);
void uf_send_nulldata(unifi_priv_t * priv,u16 interfaceTag, const u8 *da,CSR_PRIORITY priority,CsrWifiRouterCtrlStaInfo_t * srcStaInfo);



#endif
CsrResult uf_process_ma_packet_req(unifi_priv_t *priv,  u8 *peerMacAddress, CSR_CLIENT_TAG hostTag, u16 interfaceTag, CSR_TRANSMISSION_CONTROL transmissionControl, CSR_RATE TransmitRate, CSR_PRIORITY priority, CSR_PROCESS_ID senderId, bulk_data_param_t *bulkdata);
void uf_process_ma_vif_availibility_ind(unifi_priv_t *priv,u8 *sigdata, u32 siglen);
#ifdef CSR_SUPPORT_SME
void uf_send_buffered_frames(unifi_priv_t *priv,unifi_TrafficQueue queue);
int uf_process_station_records_for_sending_data(unifi_priv_t *priv,u16 interfaceTag,
                                                 CsrWifiRouterCtrlStaInfo_t *srcStaInfo,
                                                 CsrWifiRouterCtrlStaInfo_t *dstStaInfo);
void uf_prepare_send_cfm_list_for_queued_pkts(unifi_priv_t * priv,
                                                 struct list_head *frames_need_cfm_list,
                                                 struct list_head * list);
void send_auto_ma_packet_confirm(unifi_priv_t *priv,
                                 netInterface_priv_t *interfacePriv,
                                 struct list_head *buffered_frames_list);
void uf_flush_list(unifi_priv_t * priv, struct list_head * list);
tx_buffered_packets_t *dequeue_tx_data_pdu(unifi_priv_t *priv, struct list_head *txList);
void resume_unicast_buffered_frames(unifi_priv_t *priv, u16 interfaceTag);
void update_eosp_to_head_of_broadcast_list_head(unifi_priv_t *priv,u16 interfaceTag);
void resume_suspended_uapsd(unifi_priv_t* priv,u16 interfaceTag);
#endif
/*
 *      netdev.c
 */

#ifndef P80211_OUI_LEN
#define P80211_OUI_LEN  3
#endif
typedef struct {
    u8    dsap;   /* always 0xAA */
    u8    ssap;   /* always 0xAA */
    u8    ctrl;   /* always 0x03 */
    u8    oui[P80211_OUI_LEN];    /* organizational universal id */
    u16 protocol;
} __attribute__ ((packed)) llc_snap_hdr_t;
int skb_add_llc_snap(struct net_device *dev, struct sk_buff *skb, int proto);
int skb_80211_to_ether(unifi_priv_t *priv, struct sk_buff *skb,
        const unsigned char *daddr, const unsigned char *saddr,
        const CSR_SIGNAL *signal,
        bulk_data_param_t *bulkdata);

const char *result_code_str(int result);


/* prepares & appends the Mac header for the payload */
int prepare_and_add_macheader(unifi_priv_t *priv,
                              struct sk_buff *skb,
                              struct sk_buff *newSkb,
                              CSR_PRIORITY priority,
                              bulk_data_param_t *bulkdata,
                              u16 interfaceTag,
                              const u8 *daddr,
                              const u8 *saddr,
                              CsrBool protection);
CSR_PRIORITY
get_packet_priority(unifi_priv_t *priv, struct sk_buff *skb, const struct ethhdr *ehdr, netInterface_priv_t *interfacePriv);

void
unifi_frame_ma_packet_req(unifi_priv_t *priv, CSR_PRIORITY priority,
                          CSR_RATE TransmitRate, CSR_CLIENT_TAG hostTag,
                          u16 interfaceTag, CSR_TRANSMISSION_CONTROL transmissionControl,
                          CSR_PROCESS_ID leSenderProcessId, u8 *peerMacAddress,
                          CSR_SIGNAL *signal);


/* Pack the LSB to include station handle & status of tim set */
#define CSR_WIFI_PACK_SENDER_ID_LSB_FOR_TIM_REQ(handle, timState)  ((handle << 2) | timState)
/* get the station record handle from the sender ID */
#define CSR_WIFI_GET_STATION_HANDLE_FROM_RECEIVER_ID(receiverProcessId) (u8) ((receiverProcessId & 0xff) >> 2)
/* get the timSet status from the sender ID */
#define CSR_WIFI_GET_TIMSET_STATE_FROM_RECEIVER_ID(receiverProcessId)  (u8) (receiverProcessId & 0x03)

/* handle is 6 bits to accomodate in senderId LSB (only 64 station can be associated) */
#define CSR_WIFI_BROADCAST_OR_MULTICAST_HANDLE 0x3F

void update_tim(unifi_priv_t * priv, u16 aid, u8 setTim, u16 interfaceTag, u32 handle);
void uf_handle_tim_cfm(unifi_priv_t *priv, CSR_MLME_SET_TIM_CONFIRM *cfm, u16 senderProcessId);

/* Clear the Peer station Record, in case of wifioff/unexpected card removal */
void CsrWifiRouterCtrlInterfaceReset(unifi_priv_t *priv, u16 interfaceTag);

void scroll_ba_window(unifi_priv_t *priv,
                      netInterface_priv_t *interfacePriv,
                      ba_session_rx_struct *ba_session,
                      u16 sn);

CsrBool blockack_session_stop(unifi_priv_t *priv,
                              u16 interfaceTag,
                              CsrWifiRouterCtrlBlockAckRole role,
                              u16 tID,
                              CsrWifiMacAddress macAddress);
#ifdef CSR_SUPPORT_SME
/* Fetch the protection information from interface Mode */
s8 uf_get_protection_bit_from_interfacemode(unifi_priv_t *priv, u16 interfaceTag, const u8 *daddr);
#endif

/* Fetch the station record handler from data base for matching Mac address */
#ifdef CSR_SUPPORT_SME
CsrWifiRouterCtrlStaInfo_t *CsrWifiRouterCtrlGetStationRecordFromPeerMacAddress(unifi_priv_t *priv,
                                                                                const u8 *peerMacAddress,
                                                                                u16 interfaceTag);

/* Fetch the station record handler from data base for matching handle */
CsrWifiRouterCtrlStaInfo_t * CsrWifiRouterCtrlGetStationRecordFromHandle(unifi_priv_t *priv,
                                                                 u32 handle,
                                                                 u16 interfaceTag);

void uf_update_sta_activity(unifi_priv_t *priv, u16 interfaceTag, const u8 *peerMacAddress);
void uf_process_ma_pkt_cfm_for_ap(unifi_priv_t *priv,u16 interfaceTag, const CSR_MA_PACKET_CONFIRM *pkt_cfm);
#endif
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,28)
int uf_install_qdisc(struct net_device *dev);
#endif

void uf_resume_data_plane(unifi_priv_t *priv, int queue,
                          CsrWifiMacAddress peer_address,
                          u16 interfaceTag);
void uf_free_pending_rx_packets(unifi_priv_t *priv, int queue,
        CsrWifiMacAddress peer_address,u16 interfaceTag);

int uf_register_netdev(unifi_priv_t *priv, int numOfInterface);
void uf_unregister_netdev(unifi_priv_t *priv);

void uf_net_get_name(struct net_device *dev, char *name, int len);

void uf_send_queue_info(unifi_priv_t *priv);
u16 uf_get_vif_identifier(CsrWifiRouterCtrlMode mode, u16 tag);

void uf_process_rx_pending_queue(unifi_priv_t *priv, int queue,
                                 CsrWifiMacAddress source_address,
                                 int indicate, u16 interfaceTag);

#ifdef CSR_WIFI_HIP_DEBUG_OFFLINE
int uf_register_hip_offline_debug(unifi_priv_t *priv);
int uf_unregister_hip_offline_debug(unifi_priv_t *priv);
#endif

/*
 *      inet.c
 */
void uf_register_inet_notifier(void);
void uf_unregister_inet_notifier(void);


/*
 * Suspend / Resume handlers
 */
void unifi_resume(void *ospriv);
void unifi_suspend(void *ospriv);


#define QOS_CAPABILITY_WMM_ENABLED      0x0001
#define QOS_CAPABILITY_WMM_UAPSD        0x0002
#define QOS_CAPABILITY_ACM_BE_ENABLED   0x0010
#define QOS_CAPABILITY_ACM_BK_ENABLED   0x0020
#define QOS_CAPABILITY_ACM_VI_ENABLED   0x0040
#define QOS_CAPABILITY_ACM_VO_ENABLED   0x0080
#define QOS_CAPABILITY_TS_BE_ENABLED    0x0100
#define QOS_CAPABILITY_TS_BK_ENABLED    0x0200
#define QOS_CAPABILITY_TS_VI_ENABLED    0x0400
#define QOS_CAPABILITY_TS_VO_ENABLED    0x0800


/* EAPOL PDUS */
#ifndef ETH_P_PAE
#define ETH_P_PAE 0x888e
#endif
#ifndef ETH_P_WAI
#define ETH_P_WAI 0x88b4
#endif
/*
 * unifi_dbg.c
 */
void debug_string_indication(unifi_priv_t *priv,
        const unsigned char *extra,
        unsigned int extralen);
void debug_word16_indication(unifi_priv_t *priv, const CSR_SIGNAL *sigptr);
void debug_generic_indication(unifi_priv_t *priv, const CSR_SIGNAL *sigptr);


/*
 * putest.c
 */
int unifi_putest_start(unifi_priv_t *priv, unsigned char *arg);
int unifi_putest_cmd52_block_read(unifi_priv_t *priv, unsigned char *arg);
int unifi_putest_stop(unifi_priv_t *priv, unsigned char *arg);
int unifi_putest_set_sdio_clock(unifi_priv_t *priv, unsigned char *arg);
int unifi_putest_cmd52_read(unifi_priv_t *priv, unsigned char *arg);
int unifi_putest_coredump_prepare(unifi_priv_t *priv, unsigned char *arg);
int unifi_putest_cmd52_write(unifi_priv_t *priv, unsigned char *arg);
int unifi_putest_gp_read16(unifi_priv_t *priv, unsigned char *arg);
int unifi_putest_gp_write16(unifi_priv_t *priv, unsigned char *arg);

int unifi_putest_dl_fw(unifi_priv_t *priv, unsigned char *arg);
int unifi_putest_dl_fw_buff(unifi_priv_t *priv, unsigned char *arg);

#endif /* __LINUX_UNIFI_PRIV_H__ */
