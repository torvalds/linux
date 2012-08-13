/*
 * ---------------------------------------------------------------------------
 *
 *  FILE: unifiio.h
 *
 *      Public definitions for the UniFi linux driver.
 *      This is mostly ioctl command values and structs.
 *
 *      Include <sys/ioctl.h> or similar before this file
 *
 * Copyright (C) 2005-2009 by Cambridge Silicon Radio Ltd.
 *
 * Refer to LICENSE.txt included with this source code for details on
 * the license terms.
 *
 * ---------------------------------------------------------------------------
 */
#ifndef __UNIFIIO_H__
#define __UNIFIIO_H__

#include <linux/types.h>

#define UNIFI_GET_UDI_ENABLE    _IOR('u',  1, int)
#define UNIFI_SET_UDI_ENABLE    _IOW('u',  2, int)
/* Values for UDI_ENABLE */
#define UDI_ENABLE_DATA         0x1
#define UDI_ENABLE_CONTROL      0x2

/* MIB set/get. Arg is a pointer to a varbind */
#define UNIFI_GET_MIB           _IOWR('u',  3, unsigned char *)
#define UNIFI_SET_MIB           _IOW ('u',  4, unsigned char *)
#define MAX_VARBIND_LENGTH 127

/* Private IOCTLs */
#define SIOCIWS80211POWERSAVEPRIV           SIOCIWFIRSTPRIV
#define SIOCIWG80211POWERSAVEPRIV           SIOCIWFIRSTPRIV + 1
#define SIOCIWS80211RELOADDEFAULTSPRIV      SIOCIWFIRSTPRIV + 2
#define SIOCIWSCONFWAPIPRIV                 SIOCIWFIRSTPRIV + 4
#define SIOCIWSWAPIKEYPRIV                  SIOCIWFIRSTPRIV + 6
#define SIOCIWSSMEDEBUGPRIV                 SIOCIWFIRSTPRIV + 8
#define SIOCIWSAPCFGPRIV                    SIOCIWFIRSTPRIV + 10
#define SIOCIWSAPSTARTPRIV                  SIOCIWFIRSTPRIV + 12
#define SIOCIWSAPSTOPPRIV                   SIOCIWFIRSTPRIV + 14
#define SIOCIWSFWRELOADPRIV                 SIOCIWFIRSTPRIV + 16
#define SIOCIWSSTACKSTART                   SIOCIWFIRSTPRIV + 18
#define SIOCIWSSTACKSTOP                    SIOCIWFIRSTPRIV + 20



#define IWPRIV_POWER_SAVE_MAX_STRING 32
#define IWPRIV_SME_DEBUG_MAX_STRING 32
#define IWPRIV_SME_MAX_STRING 120


/* Private configuration commands */
#define UNIFI_CFG               _IOWR('u',  5, unsigned char *)
/*
 * <------------------  Read/Write Buffer  -------------------->
 * _____________________________________________________________
 * |    Cmd    |    Arg    |   ...  Buffer (opt)  ...          |
 * -------------------------------------------------------------
 * <-- uint --><-- uint --><-----  unsigned char buffer  ------>
 *
 * Cmd:    A unifi_cfg_command_t command.
 * Arg:    Out:Length     if Cmd==UNIFI_CFG_GET
 *         In:PowerOnOff  if Cmd==UNIFI_CFG_POWER
 *         In:PowerMode   if Cmd==UNIFI_CFG_POWERSAVE
 *         In:Length      if Cmd==UNIFI_CFG_FILTER
 *         In:WMM Qos Info if Cmd==UNIFI_CFG_WMM_QOS_INFO
 * Buffer: Out:Data       if Cmd==UNIFI_CFG_GET
 *         NULL           if Cmd==UNIFI_CFG_POWER
 *         NULL           if Cmd==UNIFI_CFG_POWERSAVE
 *         In:Filters     if Cmd==UNIFI_CFG_FILTER
 *
 * where Filters is a uf_cfg_bcast_packet_filter_t structure
 * followed by 0 - n tclas_t structures. The length of the tclas_t
 * structures is obtained by uf_cfg_bcast_packet_filter_t::tclas_ies_length.
 */


#define UNIFI_PUTEST            _IOWR('u',  6, unsigned char *)
/*
 * <------------------  Read/Write Buffer  -------------------->
 * _____________________________________________________________
 * |    Cmd    |    Arg    |   ...  Buffer (opt)  ...          |
 * -------------------------------------------------------------
 * <-- uint --><-- uint --><-----  unsigned char buffer  ------>
 *
 * Cmd:    A unifi_putest_command_t command.
 * Arg:    N/A                           if Cmd==UNIFI_PUTEST_START
 *         N/A                           if Cmd==UNIFI_PUTEST_STOP
 *         In:int (Clock Speed)          if Cmd==UNIFI_PUTEST_SET_SDIO_CLOCK
 *         In/Out:sizeof(unifi_putest_cmd52) if Cmd==UNIFI_PUTEST_CMD52_READ
 *         In:sizeof(unifi_putest_cmd52) if Cmd==UNIFI_PUTEST_CMD52_WRITE
 *         In:uint (f/w file name length) if Cmd==UNIFI_PUTEST_DL_FW
 * Buffer: NULL                          if Cmd==UNIFI_PUTEST_START
 *         NULL                          if Cmd==UNIFI_PUTEST_STOP
 *         NULL                          if Cmd==UNIFI_PUTEST_SET_SDIO_CLOCK
 *         In/Out:unifi_putest_cmd52     if Cmd==UNIFI_PUTEST_CMD52_READ
 *         In:unifi_putest_cmd52         if Cmd==UNIFI_PUTEST_CMD52_WRITE
 *         In:f/w file name              if Cmd==UNIFI_PUTEST_DL_FW
 */

#define UNIFI_BUILD_TYPE _IOWR('u', 7, unsigned char)
#define UNIFI_BUILD_NME 1
#define UNIFI_BUILD_WEXT 2
#define UNIFI_BUILD_AP 3

/* debugging */
#define UNIFI_KICK              _IO ('u',  0x10)
#define UNIFI_SET_DEBUG         _IO ('u',  0x11)
#define UNIFI_SET_TRACE         _IO ('u',  0x12)

#define UNIFI_GET_INIT_STATUS   _IOR ('u', 0x15, int)
#define UNIFI_SET_UDI_LOG_MASK  _IOR('u',  0x18, unifiio_filter_t)
#define UNIFI_SET_UDI_SNAP_MASK _IOW('u',  0x1a, unifiio_snap_filter_t)
#define UNIFI_SET_AMP_ENABLE    _IOWR('u',  0x1b, int)

#define UNIFI_INIT_HW           _IOR ('u', 0x13, unsigned char)
#define UNIFI_INIT_NETDEV       _IOW ('u', 0x14, unsigned char[6])
#define UNIFI_SME_PRESENT       _IOW ('u', 0x19, int)

#define UNIFI_CFG_PERIOD_TRAFFIC _IOW ('u', 0x21, unsigned char *)
#define UNIFI_CFG_UAPSD_TRAFFIC _IOW ('u', 0x22, unsigned char)

#define UNIFI_COREDUMP_GET_REG  _IOWR('u', 0x23, unifiio_coredump_req_t)


/*
 * Following reset, f/w may only be downloaded using CMD52.
 * This is slow, so there is a facility to download a secondary
 * loader first which supports CMD53.
 * If loader_len is > 0, then loader_data is assumed to point to
 * a suitable secondary loader that can be used to download the
 * main image.
 *
 * The driver will run the host protocol initialisation sequence
 * after downloading the image.
 *
 * If both lengths are zero, then the f/w is assumed to have been
 * booted from Flash and the host protocol initialisation sequence
 * is run.
 */
typedef struct {

    /* Number of bytes in the image */
    int img_len;

    /* Pointer to image data. */
    unsigned char *img_data;


    /* Number of bytes in the loader image */
    int loader_len;

    /* Pointer to loader image data. */
    unsigned char *loader_data;

} unifiio_img_t;


/* Structure of data read from the unifi device. */
typedef struct
{
    /* Length (in bytes) of entire structure including appended bulk data */
    int length;

    /* System time (in milliseconds) that signal was transferred */
    int timestamp;

    /* Direction in which signal was transferred. */
    int direction;
#define UDI_FROM_HOST   0
#define UDI_TO_HOST     1
#define UDI_CONFIG_IND  2

    /* The length of the signal (in bytes) not including bulk data */
    int signal_length;

    /* Signal body follows, then any bulk data */

} udi_msg_t;


typedef enum
{
    UfSigFil_AllOn = 0,         /* Log all signal IDs */
    UfSigFil_AllOff = 1,        /* Don't log any signal IDs */
    UfSigFil_SelectOn = 2,      /* Log these signal IDs */
    UfSigFil_SelectOff = 3      /* Don't log these signal IDs */
} uf_sigfilter_action_t;

typedef struct {

    /* Number of 16-bit ints in the sig_ids array */
    int num_sig_ids;
    /* The action to perform */
    uf_sigfilter_action_t action;
    /* List of signal IDs to pass or block */
    unsigned short *sig_ids;

} unifiio_filter_t;


typedef struct {
    /* Number of 16-bit ints in the protocols array */
    u16 count;
    /* List of protocol ids to pass */
    u16 *protocols;
} unifiio_snap_filter_t;



typedef u8 unifi_putest_command_t;

#define UNIFI_PUTEST_START 0
#define UNIFI_PUTEST_STOP 1
#define UNIFI_PUTEST_SET_SDIO_CLOCK 2
#define UNIFI_PUTEST_CMD52_READ 3
#define UNIFI_PUTEST_CMD52_WRITE 4
#define UNIFI_PUTEST_DL_FW 5
#define UNIFI_PUTEST_DL_FW_BUFF 6
#define UNIFI_PUTEST_CMD52_BLOCK_READ 7
#define UNIFI_PUTEST_COREDUMP_PREPARE 8
#define UNIFI_PUTEST_GP_READ16 9
#define UNIFI_PUTEST_GP_WRITE16 10


struct unifi_putest_cmd52 {
    int funcnum;
    unsigned long addr;
    unsigned char data;
};


struct unifi_putest_block_cmd52_r {
    int           funcnum;
    unsigned long addr;
    unsigned int  length;
    unsigned char *data;
};

struct unifi_putest_gp_rw16 {
    unsigned long addr;        /* generic address */
    unsigned short data;
};

typedef enum unifi_cfg_command {
    UNIFI_CFG_GET,
    UNIFI_CFG_POWER,
    UNIFI_CFG_POWERSAVE,
    UNIFI_CFG_FILTER,
    UNIFI_CFG_POWERSUPPLY,
    UNIFI_CFG_WMM_QOSINFO,
    UNIFI_CFG_WMM_ADDTS,
    UNIFI_CFG_WMM_DELTS,
    UNIFI_CFG_STRICT_DRAFT_N,
    UNIFI_CFG_ENABLE_OKC,
    UNIFI_CFG_SET_AP_CONFIG,
    UNIFI_CFG_CORE_DUMP /* request to take a fw core dump */
} unifi_cfg_command_t;

typedef enum unifi_cfg_power {
    UNIFI_CFG_POWER_UNSPECIFIED,
    UNIFI_CFG_POWER_OFF,
    UNIFI_CFG_POWER_ON
} unifi_cfg_power_t;

typedef enum unifi_cfg_powersupply {
    UNIFI_CFG_POWERSUPPLY_UNSPECIFIED,
    UNIFI_CFG_POWERSUPPLY_MAINS,
    UNIFI_CFG_POWERSUPPLY_BATTERIES
} unifi_cfg_powersupply_t;

typedef enum unifi_cfg_powersave {
    UNIFI_CFG_POWERSAVE_UNSPECIFIED,
    UNIFI_CFG_POWERSAVE_NONE,
    UNIFI_CFG_POWERSAVE_FAST,
    UNIFI_CFG_POWERSAVE_FULL,
    UNIFI_CFG_POWERSAVE_AUTO
} unifi_cfg_powersave_t;

typedef enum unifi_cfg_get {
    UNIFI_CFG_GET_COEX,
    UNIFI_CFG_GET_POWER_MODE,
    UNIFI_CFG_GET_VERSIONS,
    UNIFI_CFG_GET_POWER_SUPPLY,
    UNIFI_CFG_GET_INSTANCE,
    UNIFI_CFG_GET_AP_CONFIG
} unifi_cfg_get_t;

#define UNIFI_CFG_FILTER_NONE            0x0000
#define UNIFI_CFG_FILTER_DHCP            0x0001
#define UNIFI_CFG_FILTER_ARP             0x0002
#define UNIFI_CFG_FILTER_NBNS            0x0004
#define UNIFI_CFG_FILTER_NBDS            0x0008
#define UNIFI_CFG_FILTER_CUPS            0x0010
#define UNIFI_CFG_FILTER_ALL             0xFFFF


typedef struct uf_cfg_bcast_packet_filter
{
    unsigned long filter_mode;     //as defined by HIP protocol
    unsigned char arp_filter;
    unsigned char dhcp_filter;
    unsigned long tclas_ies_length; // length of tclas_ies in bytes
    unsigned char tclas_ies[1];    // variable length depending on above field
} uf_cfg_bcast_packet_filter_t;

typedef struct uf_cfg_ap_config
{
    u8    phySupportedBitmap;
    u8    channel;
    u16   beaconInterval;
    u8    dtimPeriod;
    u8     wmmEnabled;
    u8    shortSlotTimeEnabled;
    u16   groupkeyTimeout;
    u8     strictGtkRekeyEnabled;
    u16   gmkTimeout;
    u16   responseTimeout;
    u8    retransLimit;
    u8    rxStbc;
    u8     rifsModeAllowed;
    u8    dualCtsProtection;
    u8    ctsProtectionType;
    u16   maxListenInterval;
}uf_cfg_ap_config_t;

typedef struct tcpic_clsfr
{
    __u8 cls_fr_type;
    __u8 cls_fr_mask;
    __u8 version;
    __u8 source_ip_addr[4];
    __u8 dest_ip_addr[4];
    __u16 source_port;
    __u16 dest_port;
    __u8 dscp;
    __u8 protocol;
    __u8 reserved;
} __attribute__ ((packed)) tcpip_clsfr_t;

typedef struct tclas {
    __u8 element_id;
    __u8 length;
    __u8 user_priority;
    tcpip_clsfr_t tcp_ip_cls_fr;
} __attribute__ ((packed)) tclas_t;


#define CONFIG_IND_ERROR            0x01
#define CONFIG_IND_EXIT             0x02
#define CONFIG_SME_NOT_PRESENT      0x10
#define CONFIG_SME_PRESENT          0x20

/* WAPI Key */
typedef struct
{
    u8                          unicastKey;
    /* If non zero, then unicast key otherwise group key */
    u8                          keyIndex;
    u8                          keyRsc[16];
    u8                          authenticator;
    /* If non zero, then authenticator otherwise supplicant */
    u8                          address[6];
    u8                          key[32];
} unifiio_wapi_key_t;

/* Values describing XAP memory regions captured by the mini-coredump system */
typedef enum unifiio_coredump_space {
    UNIFIIO_COREDUMP_MAC_REG,
    UNIFIIO_COREDUMP_PHY_REG,
    UNIFIIO_COREDUMP_SH_DMEM,
    UNIFIIO_COREDUMP_MAC_DMEM,
    UNIFIIO_COREDUMP_PHY_DMEM,
    UNIFIIO_COREDUMP_TRIGGER_MAGIC = 0xFEED
} unifiio_coredump_space_t;

/* Userspace tool uses this structure to retrieve a register value from a
 * mini-coredump buffer previously saved by the HIP
 */
typedef struct unifiio_coredump_req {
    /* From user */
    int index;                      /* 0=newest, -1=oldest */
    unsigned int offset;            /* register offset in space */
    unifiio_coredump_space_t space; /* memory space */
    /* Filled by driver */
    unsigned int drv_build;         /* driver build id */
    unsigned int chip_ver;          /* chip version */
    unsigned int fw_ver;            /* firmware version */
    int requestor;                  /* requestor: 0=auto dump, 1=manual */
    unsigned int timestamp;         /* time of capture by driver */
    unsigned int serial;            /* capture serial number */
    int value;                      /* 16 bit register value, -ve for error */
} unifiio_coredump_req_t;           /* Core-dumped register value request */

#endif /* __UNIFIIO_H__ */
