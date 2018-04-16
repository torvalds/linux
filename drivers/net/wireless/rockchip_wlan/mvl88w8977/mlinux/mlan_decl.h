/** @file mlan_decl.h
 *
 *  @brief This file declares the generic data structures and APIs.
 *
 *  Copyright (C) 2008-2017, Marvell International Ltd.
 *
 *  This software file (the "File") is distributed by Marvell International
 *  Ltd. under the terms of the GNU General Public License Version 2, June 1991
 *  (the "License").  You may use, redistribute and/or modify this File in
 *  accordance with the terms and conditions of the License, a copy of which
 *  is available by writing to the Free Software Foundation, Inc.,
 *  51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA or on the
 *  worldwide web at http://www.gnu.org/licenses/old-licenses/gpl-2.0.txt.
 *
 *  THE FILE IS DISTRIBUTED AS-IS, WITHOUT WARRANTY OF ANY KIND, AND THE
 *  IMPLIED WARRANTIES OF MERCHANTABILITY OR FITNESS FOR A PARTICULAR PURPOSE
 *  ARE EXPRESSLY DISCLAIMED.  The License provides additional details about
 *  this warranty disclaimer.
 */

/******************************************************
Change log:
    11/07/2008: initial version
******************************************************/

#ifndef _MLAN_DECL_H_
#define _MLAN_DECL_H_

/** MLAN release version */
#define MLAN_RELEASE_VERSION		 "C506"

/** Re-define generic data types for MLAN/MOAL */
/** Signed char (1-byte) */
typedef signed char t_s8;
/** Unsigned char (1-byte) */
typedef unsigned char t_u8;
/** Signed short (2-bytes) */
typedef short t_s16;
/** Unsigned short (2-bytes) */
typedef unsigned short t_u16;
/** Signed long (4-bytes) */
typedef int t_s32;
/** Unsigned long (4-bytes) */
typedef unsigned int t_u32;
/** Signed long long 8-bytes) */
typedef long long t_s64;
/** Unsigned long long 8-bytes) */
typedef unsigned long long t_u64;
/** Void pointer (4-bytes) */
typedef void t_void;
/** Size type */
typedef t_u32 t_size;
/** Boolean type */
typedef t_u8 t_bool;

#ifdef MLAN_64BIT
/** Pointer type (64-bit) */
typedef t_u64 t_ptr;
/** Signed value (64-bit) */
typedef t_s64 t_sval;
#else
/** Pointer type (32-bit) */
typedef t_u32 t_ptr;
/** Signed value (32-bit) */
typedef t_s32 t_sval;
#endif

/** Constants below */

#ifdef __GNUC__
/** Structure packing begins */
#define MLAN_PACK_START
/** Structure packeing end */
#define MLAN_PACK_END  __attribute__((packed))
#else /* !__GNUC__ */
#ifdef PRAGMA_PACK
/** Structure packing begins */
#define MLAN_PACK_START
/** Structure packeing end */
#define MLAN_PACK_END
#else /* !PRAGMA_PACK */
/** Structure packing begins */
#define MLAN_PACK_START   __packed
/** Structure packing end */
#define MLAN_PACK_END
#endif /* PRAGMA_PACK */
#endif /* __GNUC__ */

#ifndef INLINE
#ifdef __GNUC__
/** inline directive */
#define	INLINE	inline
#else
/** inline directive */
#define	INLINE	__inline
#endif
#endif

/** MLAN TRUE */
#define MTRUE                    (1)
/** MLAN FALSE */
#define MFALSE                   (0)

#define CHANNEL_SPEC_SNIFFER_MODE 1

#ifndef MACSTR
/** MAC address security format */
#define MACSTR "%02x:XX:XX:XX:%02x:%02x"
#endif

#ifndef MAC2STR
/** MAC address security print arguments */
#define MAC2STR(a) (a)[0], (a)[4], (a)[5]
#endif

#ifndef FULL_MACSTR
#define FULL_MACSTR "%02x:%02x:%02x:%02x:%02x:%02x"
#endif
#ifndef FULL_MAC2STR
#define FULL_MAC2STR(a) (a)[0], (a)[1], (a)[2], (a)[3], (a)[4], (a)[5]
#endif

/** Macros for Data Alignment : size */
#define ALIGN_SZ(p, a)	\
	(((p) + ((a) - 1)) & ~((a) - 1))

/** Macros for Data Alignment : address */
#define ALIGN_ADDR(p, a)    \
	((((t_ptr)(p)) + (((t_ptr)(a)) - 1)) & ~(((t_ptr)(a)) - 1))

/** Return the byte offset of a field in the given structure */
#define MLAN_FIELD_OFFSET(type, field) ((t_u32)(t_ptr)&(((type *)0)->field))
/** Return aligned offset */
#define OFFSET_ALIGN_ADDR(p, a) (t_u32)(ALIGN_ADDR(p, a) - (t_ptr)p)

/** Maximum BSS numbers */
#define MLAN_MAX_BSS_NUM         (16)

/** NET IP alignment */
#define MLAN_NET_IP_ALIGN        2

/** DMA alignment */
/* SDIO3.0 Inrevium Adapter require 32 bit DMA alignment */
#define DMA_ALIGNMENT            32

/** max size of TxPD */
#define MAX_TXPD_SIZE            32

/** Minimum data header length */
#define MLAN_MIN_DATA_HEADER_LEN (DMA_ALIGNMENT+MAX_TXPD_SIZE)

/** rx data header length */
#define MLAN_RX_HEADER_LEN       MLAN_MIN_DATA_HEADER_LEN

/** This is current limit on Maximum Tx AMPDU allowed */
#define MLAN_MAX_TX_BASTREAM_SUPPORTED          16
#define MLAN_MAX_TX_BASTREAM_DEFAULT            2
/** This is current limit on Maximum Rx AMPDU allowed */
#define MLAN_MAX_RX_BASTREAM_SUPPORTED     16

#ifdef STA_SUPPORT
/** Default Win size attached during ADDBA request */
#define MLAN_STA_AMPDU_DEF_TXWINSIZE       64
/** Default Win size attached during ADDBA response */
#define MLAN_STA_AMPDU_DEF_RXWINSIZE       64
/** RX winsize for COEX */
#define MLAN_STA_COEX_AMPDU_DEF_RXWINSIZE  16
#endif /* STA_SUPPORT */
#ifdef UAP_SUPPORT
/** Default Win size attached during ADDBA request */
#define MLAN_UAP_AMPDU_DEF_TXWINSIZE       48
/** Default Win size attached during ADDBA response */
#define MLAN_UAP_AMPDU_DEF_RXWINSIZE       32
/** RX winsize for COEX */
#define MLAN_UAP_COEX_AMPDU_DEF_RXWINSIZE  16
#endif /* UAP_SUPPORT */

#ifdef WIFI_DIRECT_SUPPORT
/** WFD use the same window size for tx/rx */
#define MLAN_WFD_AMPDU_DEF_TXRXWINSIZE     64
/** RX winsize for COEX */
#define MLAN_WFD_COEX_AMPDU_DEF_RXWINSIZE  16
#endif

/** NAN use the same window size for tx/rx */
#define MLAN_NAN_AMPDU_DEF_TXRXWINSIZE     16
/** RX winsize for COEX */
#define MLAN_NAN_COEX_AMPDU_DEF_RXWINSIZE  16

/** Block ack timeout value */
#define MLAN_DEFAULT_BLOCK_ACK_TIMEOUT  0xffff
/** Maximum Tx Win size configured for ADDBA request [10 bits] */
#define MLAN_AMPDU_MAX_TXWINSIZE        0x3ff
/** Maximum Rx Win size configured for ADDBA request [10 bits] */
#define MLAN_AMPDU_MAX_RXWINSIZE        0x3ff

/** Rate index for HR/DSSS 0 */
#define MLAN_RATE_INDEX_HRDSSS0 0
/** Rate index for HR/DSSS 3 */
#define MLAN_RATE_INDEX_HRDSSS3 3
/** Rate index for OFDM 0 */
#define MLAN_RATE_INDEX_OFDM0   4
/** Rate index for OFDM 7 */
#define MLAN_RATE_INDEX_OFDM7   11
/** Rate index for MCS 0 */
#define MLAN_RATE_INDEX_MCS0    0
/** Rate index for MCS 7 */
#define MLAN_RATE_INDEX_MCS7    7
/** Rate index for MCS 9 */
#define MLAN_RATE_INDEX_MCS9    9
/** Rate index for MCS 32 */
#define MLAN_RATE_INDEX_MCS32   32
/** Rate index for MCS 127 */
#define MLAN_RATE_INDEX_MCS127  127

/** Rate bitmap for OFDM 0 */
#define MLAN_RATE_BITMAP_OFDM0  16
/** Rate bitmap for OFDM 7 */
#define MLAN_RATE_BITMAP_OFDM7  23
/** Rate bitmap for MCS 0 */
#define MLAN_RATE_BITMAP_MCS0   32
/** Rate bitmap for MCS 127 */
#define MLAN_RATE_BITMAP_MCS127 159

/** Size of rx data buffer */
#define MLAN_RX_DATA_BUF_SIZE     (4 * 1024)
/** Size of rx command buffer */
#define MLAN_RX_CMD_BUF_SIZE      (2 * 1024)

#define MLAN_USB_RX_DATA_BUF_SIZE       MLAN_RX_DATA_BUF_SIZE

/** MLAN MAC Address Length */
#define MLAN_MAC_ADDR_LENGTH     (6)
/** MLAN 802.11 MAC Address */
typedef t_u8 mlan_802_11_mac_addr[MLAN_MAC_ADDR_LENGTH];

/** MLAN Maximum SSID Length */
#define MLAN_MAX_SSID_LENGTH     (32)

/** RTS/FRAG related defines */
/** Minimum RTS value */
#define MLAN_RTS_MIN_VALUE              (0)
/** Maximum RTS value */
#define MLAN_RTS_MAX_VALUE              (2347)
/** Minimum FRAG value */
#define MLAN_FRAG_MIN_VALUE             (256)
/** Maximum FRAG value */
#define MLAN_FRAG_MAX_VALUE             (2346)

/** Minimum tx retry count */
#define MLAN_TX_RETRY_MIN		(0)
/** Maximum tx retry count */
#define MLAN_TX_RETRY_MAX		(14)

/** max Wmm AC queues */
#define MAX_AC_QUEUES                   4

/** define SDIO block size for data Tx/Rx */
/* We support up to 480-byte block size due to FW buffer limitation. */
#define MLAN_SDIO_BLOCK_SIZE		256

/** define SDIO block size for firmware download */
#define MLAN_SDIO_BLOCK_SIZE_FW_DNLD	MLAN_SDIO_BLOCK_SIZE

/** define allocated buffer size */
#define ALLOC_BUF_SIZE           (4 * 1024)
/** SDIO MP aggr pkt limit */
#define SDIO_MP_AGGR_DEF_PKT_LIMIT       (16)

/** SDIO IO Port mask */
#define MLAN_SDIO_IO_PORT_MASK		0xfffff
/** SDIO Block/Byte mode mask */
#define MLAN_SDIO_BYTE_MODE_MASK	0x80000000

/** Max retry number of IO write */
#define MAX_WRITE_IOMEM_RETRY		2

/** IN parameter */
#define IN
/** OUT parameter */
#define OUT

/** BIT value */
#define MBIT(x)    (((t_u32)1) << (x))

/** Buffer flag for requeued packet */
#define MLAN_BUF_FLAG_REQUEUED_PKT      MBIT(0)
/** Buffer flag for transmit buf from moal */
#define MLAN_BUF_FLAG_MOAL_TX_BUF        MBIT(1)
/** Buffer flag for malloc mlan_buffer */
#define MLAN_BUF_FLAG_MALLOC_BUF        MBIT(2)

/** Buffer flag for bridge packet */
#define MLAN_BUF_FLAG_BRIDGE_BUF        MBIT(3)

/** Buffer flag for TDLS */
#define MLAN_BUF_FLAG_TDLS	            MBIT(8)

/** Buffer flag for TCP_ACK */
#define MLAN_BUF_FLAG_TCP_ACK		    MBIT(9)

/** Buffer flag for TX_STATUS */
#define MLAN_BUF_FLAG_TX_STATUS         MBIT(10)

/** Buffer flag for NET_MONITOR */
#define MLAN_BUF_FLAG_NET_MONITOR        MBIT(11)

/** Buffer flag for NULL data packet */
#define MLAN_BUF_FLAG_NULL_PKT        MBIT(12)

#define MLAN_BUF_FLAG_TX_CTRL               MBIT(14)

#ifdef DEBUG_LEVEL1
/** Debug level bit definition */
#define	MMSG        MBIT(0)
#define MFATAL      MBIT(1)
#define MERROR      MBIT(2)
#define MDATA       MBIT(3)
#define MCMND       MBIT(4)
#define MEVENT      MBIT(5)
#define MINTR       MBIT(6)
#define MIOCTL      MBIT(7)

#define MMPA_D      MBIT(15)
#define MDAT_D      MBIT(16)
#define MCMD_D      MBIT(17)
#define MEVT_D      MBIT(18)
#define MFW_D       MBIT(19)
#define MIF_D       MBIT(20)

#define MENTRY      MBIT(28)
#define MWARN       MBIT(29)
#define MINFO       MBIT(30)
#define MHEX_DUMP   MBIT(31)
#endif /* DEBUG_LEVEL1 */

/** Memory allocation type: DMA */
#define	MLAN_MEM_DMA    MBIT(0)

/** Default memory allocation flag */
#define MLAN_MEM_DEF    0

/** mlan_status */
typedef enum _mlan_status {
	MLAN_STATUS_FAILURE = 0xffffffff,
	MLAN_STATUS_SUCCESS = 0,
	MLAN_STATUS_PENDING,
	MLAN_STATUS_RESOURCE,
	MLAN_STATUS_COMPLETE,
} mlan_status;

/** mlan_error_code */
typedef enum _mlan_error_code {
    /** No error */
	MLAN_ERROR_NO_ERROR = 0,
    /** Firmware/device errors below (MSB=0) */
	MLAN_ERROR_FW_NOT_READY = 0x00000001,
	MLAN_ERROR_FW_BUSY = 0x00000002,
	MLAN_ERROR_FW_CMDRESP = 0x00000003,
	MLAN_ERROR_DATA_TX_FAIL = 0x00000004,
	MLAN_ERROR_DATA_RX_FAIL = 0x00000005,
    /** Driver errors below (MSB=1) */
	MLAN_ERROR_PKT_SIZE_INVALID = 0x80000001,
	MLAN_ERROR_PKT_TIMEOUT = 0x80000002,
	MLAN_ERROR_PKT_INVALID = 0x80000003,
	MLAN_ERROR_CMD_INVALID = 0x80000004,
	MLAN_ERROR_CMD_TIMEOUT = 0x80000005,
	MLAN_ERROR_CMD_DNLD_FAIL = 0x80000006,
	MLAN_ERROR_CMD_CANCEL = 0x80000007,
	MLAN_ERROR_CMD_RESP_FAIL = 0x80000008,
	MLAN_ERROR_CMD_ASSOC_FAIL = 0x80000009,
	MLAN_ERROR_CMD_SCAN_FAIL = 0x8000000A,
	MLAN_ERROR_IOCTL_INVALID = 0x8000000B,
	MLAN_ERROR_IOCTL_FAIL = 0x8000000C,
	MLAN_ERROR_EVENT_UNKNOWN = 0x8000000D,
	MLAN_ERROR_INVALID_PARAMETER = 0x8000000E,
	MLAN_ERROR_NO_MEM = 0x8000000F,
    /** More to add */
} mlan_error_code;

/** mlan_buf_type */
typedef enum _mlan_buf_type {
	MLAN_BUF_TYPE_CMD = 1,
	MLAN_BUF_TYPE_DATA,
	MLAN_BUF_TYPE_EVENT,
	MLAN_BUF_TYPE_RAW_DATA,
	MLAN_BUF_TYPE_SPA_DATA,
} mlan_buf_type;

/** MLAN BSS type */
typedef enum _mlan_bss_type {
	MLAN_BSS_TYPE_STA = 0,
	MLAN_BSS_TYPE_UAP = 1,
#ifdef WIFI_DIRECT_SUPPORT
	MLAN_BSS_TYPE_WIFIDIRECT = 2,
#endif
	MLAN_BSS_TYPE_NAN = 4,
	MLAN_BSS_TYPE_ANY = 0xff,
} mlan_bss_type;

/** MLAN BSS role */
typedef enum _mlan_bss_role {
	MLAN_BSS_ROLE_STA = 0,
	MLAN_BSS_ROLE_UAP = 1,
	MLAN_BSS_ROLE_ANY = 0xff,
} mlan_bss_role;

/** BSS role bit mask */
#define BSS_ROLE_BIT_MASK    MBIT(0)

/** Get BSS role */
#define GET_BSS_ROLE(priv)   ((priv)->bss_role & BSS_ROLE_BIT_MASK)

/** mlan_data_frame_type */
typedef enum _mlan_data_frame_type {
	MLAN_DATA_FRAME_TYPE_ETH_II = 0,
	MLAN_DATA_FRAME_TYPE_802_11,
} mlan_data_frame_type;

/** mlan_event_id */
typedef enum _mlan_event_id {
	/* Event generated by firmware (MSB=0) */
	MLAN_EVENT_ID_FW_UNKNOWN = 0x00000001,
	MLAN_EVENT_ID_FW_ADHOC_LINK_SENSED = 0x00000002,
	MLAN_EVENT_ID_FW_ADHOC_LINK_LOST = 0x00000003,
	MLAN_EVENT_ID_FW_DISCONNECTED = 0x00000004,
	MLAN_EVENT_ID_FW_MIC_ERR_UNI = 0x00000005,
	MLAN_EVENT_ID_FW_MIC_ERR_MUL = 0x00000006,
	MLAN_EVENT_ID_FW_BCN_RSSI_LOW = 0x00000007,
	MLAN_EVENT_ID_FW_BCN_RSSI_HIGH = 0x00000008,
	MLAN_EVENT_ID_FW_BCN_SNR_LOW = 0x00000009,
	MLAN_EVENT_ID_FW_BCN_SNR_HIGH = 0x0000000A,
	MLAN_EVENT_ID_FW_MAX_FAIL = 0x0000000B,
	MLAN_EVENT_ID_FW_DATA_RSSI_LOW = 0x0000000C,
	MLAN_EVENT_ID_FW_DATA_RSSI_HIGH = 0x0000000D,
	MLAN_EVENT_ID_FW_DATA_SNR_LOW = 0x0000000E,
	MLAN_EVENT_ID_FW_DATA_SNR_HIGH = 0x0000000F,
	MLAN_EVENT_ID_FW_LINK_QUALITY = 0x00000010,
	MLAN_EVENT_ID_FW_PORT_RELEASE = 0x00000011,
	MLAN_EVENT_ID_FW_PRE_BCN_LOST = 0x00000012,
	MLAN_EVENT_ID_FW_DEBUG_INFO = 0x00000013,
	MLAN_EVENT_ID_FW_WMM_CONFIG_CHANGE = 0x0000001A,
	MLAN_EVENT_ID_FW_HS_WAKEUP = 0x0000001B,
	MLAN_EVENT_ID_FW_BG_SCAN = 0x0000001D,
	MLAN_EVENT_ID_FW_BG_SCAN_STOPPED = 0x0000001E,
	MLAN_EVENT_ID_FW_WEP_ICV_ERR = 0x00000020,
	MLAN_EVENT_ID_FW_STOP_TX = 0x00000021,
	MLAN_EVENT_ID_FW_START_TX = 0x00000022,
	MLAN_EVENT_ID_FW_CHANNEL_SWITCH_ANN = 0x00000023,
	MLAN_EVENT_ID_FW_RADAR_DETECTED = 0x00000024,
	MLAN_EVENT_ID_FW_CHANNEL_REPORT_RDY = 0x00000025,
	MLAN_EVENT_ID_FW_BW_CHANGED = 0x00000026,
	MLAN_EVENT_ID_FW_REMAIN_ON_CHAN_EXPIRED = 0x0000002B,
#ifdef UAP_SUPPORT
	MLAN_EVENT_ID_UAP_FW_BSS_START = 0x0000002C,
	MLAN_EVENT_ID_UAP_FW_BSS_ACTIVE = 0x0000002D,
	MLAN_EVENT_ID_UAP_FW_BSS_IDLE = 0x0000002E,
	MLAN_EVENT_ID_UAP_FW_STA_CONNECT = 0x00000030,
	MLAN_EVENT_ID_UAP_FW_STA_DISCONNECT = 0x00000031,
#endif

	MLAN_EVENT_ID_FW_DUMP_INFO = 0x00000033,

	MLAN_EVENT_ID_FW_TX_STATUS = 0x00000034,
	MLAN_EVENT_ID_FW_CHAN_SWITCH_COMPLETE = 0x00000036,
	/* Event generated by MLAN driver (MSB=1) */
	MLAN_EVENT_ID_DRV_CONNECTED = 0x80000001,
	MLAN_EVENT_ID_DRV_DEFER_HANDLING = 0x80000002,
	MLAN_EVENT_ID_DRV_HS_ACTIVATED = 0x80000003,
	MLAN_EVENT_ID_DRV_HS_DEACTIVATED = 0x80000004,
	MLAN_EVENT_ID_DRV_MGMT_FRAME = 0x80000005,
	MLAN_EVENT_ID_DRV_OBSS_SCAN_PARAM = 0x80000006,
	MLAN_EVENT_ID_DRV_PASSTHRU = 0x80000007,
	MLAN_EVENT_ID_DRV_SCAN_REPORT = 0x80000009,
	MLAN_EVENT_ID_DRV_MEAS_REPORT = 0x8000000A,
	MLAN_EVENT_ID_DRV_ASSOC_FAILURE_REPORT = 0x8000000B,
	MLAN_EVENT_ID_DRV_REPORT_STRING = 0x8000000F,
	MLAN_EVENT_ID_DRV_DBG_DUMP = 0x80000012,
	MLAN_EVENT_ID_DRV_BGSCAN_RESULT = 0x80000013,
	MLAN_EVENT_ID_DRV_FLUSH_RX_WORK = 0x80000015,
	MLAN_EVENT_ID_DRV_DEFER_RX_WORK = 0x80000016,
	MLAN_EVENT_ID_DRV_TDLS_TEARDOWN_REQ = 0x80000017,
	MLAN_EVENT_ID_DRV_FT_RESPONSE = 0x80000018,
	MLAN_EVENT_ID_DRV_FLUSH_MAIN_WORK = 0x80000019,
#ifdef UAP_SUPPORT
	MLAN_EVENT_ID_DRV_UAP_CHAN_INFO = 0x80000020,
#endif
	MLAN_EVENT_ID_FW_ROAM_OFFLOAD_RESULT = 0x80000023,
	MLAN_EVENT_ID_NAN_STARTED = 0x80000024,
} mlan_event_id;

/** Data Structures */
/** mlan_image data structure */
typedef struct _mlan_fw_image {
    /** Helper image buffer pointer */
	t_u8 *phelper_buf;
    /** Helper image length */
	t_u32 helper_len;
    /** Firmware image buffer pointer */
	t_u8 *pfw_buf;
    /** Firmware image length */
	t_u32 fw_len;
    /** Firmware reload flag */
	t_u8 fw_reload;
} mlan_fw_image, *pmlan_fw_image;

#define OID_TYPE_CAL    0x2
#define OID_TYPE_DPD    0xa

/** Custom data structure */
typedef struct _mlan_init_param {
    /** DPD data buffer pointer */
	t_u8 *pdpd_data_buf;
    /** DPD data length */
	t_u32 dpd_data_len;
	/** region txpowerlimit cfg data buffer pointer */
	t_u8 *ptxpwr_data_buf;
	/** region txpowerlimit cfg data length */
	t_u32 txpwr_data_len;
    /** Cal data buffer pointer */
	t_u8 *pcal_data_buf;
    /** Cal data length */
	t_u32 cal_data_len;
    /** Other custom data */
} mlan_init_param, *pmlan_init_param;

/** channel band */
enum {
	BAND_2GHZ = 0,
	BAND_5GHZ = 1,
	BAND_4GHZ = 2,
};

/** channel offset */
enum {
	SEC_CHAN_NONE = 0,
	SEC_CHAN_ABOVE = 1,
	SEC_CHAN_5MHZ = 2,
	SEC_CHAN_BELOW = 3
};

/** channel bandwidth */
enum {
	CHAN_BW_20MHZ = 0,
	CHAN_BW_10MHZ,
	CHAN_BW_40MHZ,
};

/** scan mode */
enum {
	SCAN_MODE_MANUAL = 0,
	SCAN_MODE_ACS,
	SCAN_MODE_USER,
};

/** Band_Config_t */
typedef MLAN_PACK_START struct _Band_Config_t {
#ifdef BIG_ENDIAN_SUPPORT
    /** Channel Selection Mode - (00)=manual, (01)=ACS,  (02)=user*/
	t_u8 scanMode:2;
    /** Secondary Channel Offset - (00)=None, (01)=Above, (11)=Below */
	t_u8 chan2Offset:2;
    /** Channel Width - (00)=20MHz, (10)=40MHz, (11)=80MHz */
	t_u8 chanWidth:2;
    /** Band Info - (00)=2.4GHz, (01)=5GHz */
	t_u8 chanBand:2;
#else
    /** Band Info - (00)=2.4GHz, (01)=5GHz */
	t_u8 chanBand:2;
    /** Channel Width - (00)=20MHz, (10)=40MHz, (11)=80MHz */
	t_u8 chanWidth:2;
    /** Secondary Channel Offset - (00)=None, (01)=Above, (11)=Below */
	t_u8 chan2Offset:2;
    /** Channel Selection Mode - (00)=manual, (01)=ACS, (02)=Adoption mode*/
	t_u8 scanMode:2;
#endif
} MLAN_PACK_END Band_Config_t;

/** channel_band_t */
typedef MLAN_PACK_START struct _chan_band_info {
    /** Band Configuration */
	Band_Config_t bandcfg;
    /** channel */
	t_u8 channel;
	/** 11n flag */
	t_u8 is_11n_enabled;
	/** center channel */
	t_u8 center_chan;
} MLAN_PACK_END chan_band_info;

/** mlan_event data structure */
typedef struct _mlan_event {
    /** BSS index number for multiple BSS support */
	t_u32 bss_index;
    /** Event ID */
	mlan_event_id event_id;
    /** Event length */
	t_u32 event_len;
    /** Event buffer */
	t_u8 event_buf[0];
} mlan_event, *pmlan_event;

/** mlan_ioctl_req data structure */
typedef struct _mlan_ioctl_req {
    /** Pointer to previous mlan_ioctl_req */
	struct _mlan_ioctl_req *pprev;
    /** Pointer to next mlan_ioctl_req */
	struct _mlan_ioctl_req *pnext;
    /** Status code from firmware/driver */
	t_u32 status_code;
    /** BSS index number for multiple BSS support */
	t_u32 bss_index;
    /** Request id */
	t_u32 req_id;
    /** Action: set or get */
	t_u32 action;
    /** Pointer to buffer */
	t_u8 *pbuf;
    /** Length of buffer */
	t_u32 buf_len;
    /** Length of the data read/written in buffer */
	t_u32 data_read_written;
    /** Length of buffer needed */
	t_u32 buf_len_needed;
    /** Reserved for MOAL module */
	t_ptr reserved_1;
} mlan_ioctl_req, *pmlan_ioctl_req;

/** mix rate information structure */
typedef MLAN_PACK_START struct _mix_rate_info {
    /**  bit0: LGI: gi=0, SGI: gi= 1 */
    /**  bit1-2: 20M: bw=0, 40M: bw=1, 80M: bw=2, 160M: bw=3  */
    /**  bit3-4: LG: format=0, HT: format=1, VHT: format=2 */
    /**  bit5: LDPC: 0-not support,  1-support */
    /**  bit6-7:reserved */
	t_u8 rate_info;
    /** MCS index */
	t_u8 mcs_index;
    /** bitrate, in 500Kbps */
	t_u16 bitrate;
} MLAN_PACK_END mix_rate_info, *pmix_rate_info;

/** rxpd extra information structure */
typedef MLAN_PACK_START struct _rxpd_extra_info {
    /** flags */
	t_u8 flags;
    /** channel.flags */
	t_u16 channel_flags;
    /** mcs.known */
	t_u8 mcs_known;
    /** mcs.flags */
	t_u8 mcs_flags;
} MLAN_PACK_END rxpd_extra_info, *prxpd_extra_info;

/** rdaio tap information structure */
typedef MLAN_PACK_START struct _radiotap_info {
    /** Rate Info */
	mix_rate_info rate_info;
    /** SNR */
	t_s8 snr;
    /** Noise Floor */
	t_s8 nf;
    /** band config */
	t_u8 band_config;
    /** chan number */
	t_u8 chan_num;
    /** antenna */
	t_u8 antenna;
    /** extra rxpd info from FW */
	rxpd_extra_info extra_info;
} MLAN_PACK_END radiotap_info, *pradiotap_info;

/** txpower structure */
typedef MLAN_PACK_START struct {
#ifdef BIG_ENDIAN_SUPPORT
       /** Host tx power ctrl:
            0x0: use fw setting for TX power
            0x1: value specified in bit[6] and bit[5:0] are valid */
	t_u8 hostctl:1;
       /** Sign of the power specified in bit[5:0] */
	t_u8 sign:1;
       /** Power to be used for transmission(in dBm) */
	t_u8 abs_val:6;
#else
       /** Power to be used for transmission(in dBm) */
	t_u8 abs_val:6;
       /** Sign of the power specified in bit[5:0] */
	t_u8 sign:1;
       /** Host tx power ctrl:
            0x0: use fw setting for TX power
            0x1: value specified in bit[6] and bit[5:0] are valid */
	t_u8 hostctl:1;
#endif
} MLAN_PACK_END tx_power_t;
/* pkt_txctrl */
typedef MLAN_PACK_START struct _pkt_txctrl {
    /**Data rate in unit of 0.5Mbps */
	t_u16 data_rate;
	/*Channel number to transmit the frame */
	t_u8 channel;
    /** Bandwidth to transmit the frame*/
	t_u8 bw;
    /** Power to be used for transmission*/
	union {
		tx_power_t tp;
		t_u8 val;
	} tx_power;
    /** Retry time of tx transmission*/
	t_u8 retry_limit;
} MLAN_PACK_END pkt_txctrl, *ppkt_txctrl;

/** pkt_rxinfo */
typedef MLAN_PACK_START struct _pkt_rxinfo {
    /** Data rate of received paccket*/
	t_u16 data_rate;
    /** Channel on which packet was received*/
	t_u8 channel;
    /** Rx antenna*/
	t_u8 antenna;
    /** Rx Rssi*/
	t_u8 rssi;
} MLAN_PACK_END pkt_rxinfo, *ppkt_rxinfo;

/** mlan_buffer data structure */
typedef struct _mlan_buffer {
    /** Pointer to previous mlan_buffer */
	struct _mlan_buffer *pprev;
    /** Pointer to next mlan_buffer */
	struct _mlan_buffer *pnext;
    /** Status code from firmware/driver */
	t_u32 status_code;
    /** Flags for this buffer */
	t_u32 flags;
    /** BSS index number for multiple BSS support */
	t_u32 bss_index;
    /** Buffer descriptor, e.g. skb in Linux */
	t_void *pdesc;
    /** Pointer to buffer */
	t_u8 *pbuf;
    /** Offset to data */
	t_u32 data_offset;
    /** Data length */
	t_u32 data_len;
    /** Buffer type: data, cmd, event etc. */
	mlan_buf_type buf_type;

    /** Fields below are valid for data packet only */
    /** QoS priority */
	t_u32 priority;
    /** Time stamp when packet is received (seconds) */
	t_u32 in_ts_sec;
    /** Time stamp when packet is received (micro seconds) */
	t_u32 in_ts_usec;
    /** Time stamp when packet is processed (seconds) */
	t_u32 out_ts_sec;
    /** Time stamp when packet is processed (micro seconds) */
	t_u32 out_ts_usec;
    /** tx_seq_num */
	t_u32 tx_seq_num;

    /** Fields below are valid for MLAN module only */
    /** Pointer to parent mlan_buffer */
	struct _mlan_buffer *pparent;
    /** Use count for this buffer */
	t_u32 use_count;
	union {
		pkt_txctrl tx_info;
		pkt_rxinfo rx_info;
	} u;
} mlan_buffer, *pmlan_buffer;

/** mlan_fw_info data structure */
typedef struct _mlan_hw_info {
    /** Firmware capabilities */
	t_u32 fw_cap;
} mlan_hw_info, *pmlan_hw_info;

/** mlan_bss_attr data structure */
typedef struct _mlan_bss_attr {
    /** BSS type */
	t_u32 bss_type;
    /** Data frame type: Ethernet II, 802.11, etc. */
	t_u32 frame_type;
    /** The BSS is active (non-0) or not (0). */
	t_u32 active;
    /** BSS Priority */
	t_u32 bss_priority;
    /** BSS number */
	t_u32 bss_num;
    /** The BSS is virtual */
	t_u32 bss_virtual;
} mlan_bss_attr, *pmlan_bss_attr;

/** bss tbl data structure */
typedef struct _mlan_bss_tbl {
    /** BSS Attributes */
	mlan_bss_attr bss_attr[MLAN_MAX_BSS_NUM];
} mlan_bss_tbl, *pmlan_bss_tbl;

#ifdef PRAGMA_PACK
#pragma pack(push, 1)
#endif

/** Type enumeration for the command result */
typedef MLAN_PACK_START enum _mlan_cmd_result_e {
	MLAN_CMD_RESULT_SUCCESS = 0,
	MLAN_CMD_RESULT_FAILURE = 1,
	MLAN_CMD_RESULT_TIMEOUT = 2,
	MLAN_CMD_RESULT_INVALID_DATA = 3
} MLAN_PACK_END mlan_cmd_result_e;

/** Type enumeration of WMM AC_QUEUES */
typedef MLAN_PACK_START enum _mlan_wmm_ac_e {
	WMM_AC_BK,
	WMM_AC_BE,
	WMM_AC_VI,
	WMM_AC_VO
} MLAN_PACK_END mlan_wmm_ac_e;

/** Type enumeration for the action field in the Queue Config command */
typedef MLAN_PACK_START enum _mlan_wmm_queue_config_action_e {
	MLAN_WMM_QUEUE_CONFIG_ACTION_GET = 0,
	MLAN_WMM_QUEUE_CONFIG_ACTION_SET = 1,
	MLAN_WMM_QUEUE_CONFIG_ACTION_DEFAULT = 2,
	MLAN_WMM_QUEUE_CONFIG_ACTION_MAX
} MLAN_PACK_END mlan_wmm_queue_config_action_e;

/** Type enumeration for the action field in the queue stats command */
typedef MLAN_PACK_START enum _mlan_wmm_queue_stats_action_e {
	MLAN_WMM_STATS_ACTION_START = 0,
	MLAN_WMM_STATS_ACTION_STOP = 1,
	MLAN_WMM_STATS_ACTION_GET_CLR = 2,
	MLAN_WMM_STATS_ACTION_SET_CFG = 3,	/* Not currently used */
	MLAN_WMM_STATS_ACTION_GET_CFG = 4,	/* Not currently used */
	MLAN_WMM_STATS_ACTION_MAX
} MLAN_PACK_END mlan_wmm_queue_stats_action_e;

/**
 *  @brief IOCTL structure for a Traffic stream status.
 *
 */
typedef MLAN_PACK_START struct {
    /** TSID: Range: 0->7 */
	t_u8 tid;
    /** TSID specified is valid */
	t_u8 valid;
    /** AC TSID is active on */
	t_u8 access_category;
    /** UP specified for the TSID */
	t_u8 user_priority;
    /** Power save mode for TSID: 0 (legacy), 1 (UAPSD) */
	t_u8 psb;
    /** Upstream(0), Downlink(1), Bidirectional(3) */
	t_u8 flow_dir;
    /** Medium time granted for the TSID */
	t_u16 medium_time;
} MLAN_PACK_END wlan_ioctl_wmm_ts_status_t,
/** Type definition of mlan_ds_wmm_ts_status for MLAN_OID_WMM_CFG_TS_STATUS */
mlan_ds_wmm_ts_status, *pmlan_ds_wmm_ts_status;

/** Max Ie length */
#define MAX_IE_SIZE             256

/** custom IE */
typedef MLAN_PACK_START struct _custom_ie {
    /** IE Index */
	t_u16 ie_index;
    /** Mgmt Subtype Mask */
	t_u16 mgmt_subtype_mask;
    /** IE Length */
	t_u16 ie_length;
    /** IE buffer */
	t_u8 ie_buffer[MAX_IE_SIZE];
} MLAN_PACK_END custom_ie;

/** Max IE index to FW */
#define MAX_MGMT_IE_INDEX_TO_FW         4
/** Max IE index per BSS */
#define MAX_MGMT_IE_INDEX               16

/** custom IE info */
typedef MLAN_PACK_START struct _custom_ie_info {
    /** size of buffer */
	t_u16 buf_size;
    /** no of buffers of buf_size */
	t_u16 buf_count;
} MLAN_PACK_END custom_ie_info;

/** TLV buffer : Max Mgmt IE */
typedef MLAN_PACK_START struct _tlvbuf_max_mgmt_ie {
    /** Type */
	t_u16 type;
    /** Length */
	t_u16 len;
    /** No of tuples */
	t_u16 count;
    /** custom IE info tuples */
	custom_ie_info info[MAX_MGMT_IE_INDEX];
} MLAN_PACK_END tlvbuf_max_mgmt_ie;

/** TLV buffer : custom IE */
typedef MLAN_PACK_START struct _tlvbuf_custom_ie {
    /** Type */
	t_u16 type;
    /** Length */
	t_u16 len;
    /** IE data */
	custom_ie ie_data_list[MAX_MGMT_IE_INDEX_TO_FW];
    /** Max mgmt IE TLV */
	tlvbuf_max_mgmt_ie max_mgmt_ie;
} MLAN_PACK_END mlan_ds_misc_custom_ie;

/** Max TDLS config data length */
#define MAX_TDLS_DATA_LEN  1024

/** Action commands for TDLS enable/disable */
#define WLAN_TDLS_CONFIG               0x00
/** Action commands for TDLS configuration :Set */
#define WLAN_TDLS_SET_INFO             0x01
/** Action commands for TDLS configuration :Discovery Request */
#define WLAN_TDLS_DISCOVERY_REQ        0x02
/** Action commands for TDLS configuration :Setup Request */
#define WLAN_TDLS_SETUP_REQ            0x03
/** Action commands for TDLS configuration :Tear down Request */
#define WLAN_TDLS_TEAR_DOWN_REQ        0x04
/** Action ID for TDLS power mode */
#define WLAN_TDLS_POWER_MODE           0x05
/**Action ID for init TDLS Channel Switch*/
#define WLAN_TDLS_INIT_CHAN_SWITCH     0x06
/** Action ID for stop TDLS Channel Switch */
#define WLAN_TDLS_STOP_CHAN_SWITCH     0x07
/** Action ID for configure CS related parameters */
#define WLAN_TDLS_CS_PARAMS            0x08
/** Action ID for Disable CS */
#define WLAN_TDLS_CS_DISABLE           0x09
/** Action ID for TDLS link status */
#define WLAN_TDLS_LINK_STATUS          0x0A
/** Action ID for Host TDLS config uapsd and CS */
#define WLAN_HOST_TDLS_CONFIG               0x0D
/** Action ID for TDLS CS immediate return */
#define WLAN_TDLS_DEBUG_CS_RET_IM          0xFFF7
/** Action ID for TDLS Stop RX */
#define WLAN_TDLS_DEBUG_STOP_RX              0xFFF8
/** Action ID for TDLS Allow weak security for links establish */
#define WLAN_TDLS_DEBUG_ALLOW_WEAK_SECURITY  0xFFF9
/** Action ID for TDLS Ignore key lifetime expiry */
#define WLAN_TDLS_DEBUG_IGNORE_KEY_EXPIRY    0xFFFA
/** Action ID for TDLS Higher/Lower mac Test */
#define WLAN_TDLS_DEBUG_HIGHER_LOWER_MAC	 0xFFFB
/** Action ID for TDLS Prohibited Test */
#define WLAN_TDLS_DEBUG_SETUP_PROHIBITED	 0xFFFC
/** Action ID for TDLS Existing link Test */
#define WLAN_TDLS_DEBUG_SETUP_SAME_LINK    0xFFFD
/** Action ID for TDLS Fail Setup Confirm */
#define WLAN_TDLS_DEBUG_FAIL_SETUP_CONFIRM 0xFFFE
/** Action commands for TDLS debug: Wrong BSS Request */
#define WLAN_TDLS_DEBUG_WRONG_BSS      0xFFFF

/** tdls each link rate information */
typedef MLAN_PACK_START struct _tdls_link_rate_info {
    /** Tx Data Rate */
	t_u8 tx_data_rate;
    /** Tx Rate HT info*/
	t_u8 tx_rate_htinfo;
} MLAN_PACK_END tdls_link_rate_info;

/** tdls each link status */
typedef MLAN_PACK_START struct _tdls_each_link_status {
    /** peer mac Address */
	t_u8 peer_mac[MLAN_MAC_ADDR_LENGTH];
    /** Link Flags */
	t_u8 link_flags;
    /** Traffic Status */
	t_u8 traffic_status;
    /** Tx Failure Count */
	t_u8 tx_fail_count;
    /** Channel Number */
	t_u32 active_channel;
    /** Last Data RSSI in dBm */
	t_s16 data_rssi_last;
    /** Last Data NF in dBm */
	t_s16 data_nf_last;
    /** AVG DATA RSSI in dBm */
	t_s16 data_rssi_avg;
    /** AVG DATA NF in dBm */
	t_s16 data_nf_avg;
	union {
	/** tdls rate info */
		tdls_link_rate_info rate_info;
	/** tdls link final rate*/
		t_u16 final_data_rate;
	} u;
    /** Security Method */
	t_u8 security_method;
    /** Key Lifetime in milliseconds */
	t_u32 key_lifetime;
    /** Key Length */
	t_u8 key_length;
    /** actual key */
	t_u8 key[0];
} MLAN_PACK_END tdls_each_link_status;

/** TDLS configuration data */
typedef MLAN_PACK_START struct _tdls_all_config {
	union {
	/** TDLS state enable disable */
		MLAN_PACK_START struct _tdls_config {
	    /** enable or disable */
			t_u16 enable;
		} MLAN_PACK_END tdls_config;
		/** Host tdls config */
		MLAN_PACK_START struct _host_tdls_cfg {
	/** support uapsd */
			t_u8 uapsd_support;
	/** channel_switch */
			t_u8 cs_support;
	/** TLV  length */
			t_u16 tlv_len;
	/** tdls info */
			t_u8 tlv_buffer[0];
		} MLAN_PACK_END host_tdls_cfg;
	/** TDLS set info */
		MLAN_PACK_START struct _tdls_set_data {
	    /** (tlv + capInfo) length */
			t_u16 tlv_length;
	    /** Cap Info */
			t_u16 cap_info;
	    /** TLV buffer */
			t_u8 tlv_buffer[0];
		} MLAN_PACK_END tdls_set;

	/** TDLS discovery and others having mac argument */
		MLAN_PACK_START struct _tdls_discovery_data {
	    /** peer mac Address */
			t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
		} MLAN_PACK_END tdls_discovery, tdls_stop_chan_switch,
			tdls_link_status_req;

	/** TDLS discovery Response */
		MLAN_PACK_START struct _tdls_discovery_resp {
	    /** payload length */
			t_u16 payload_len;
	    /** peer mac Address */
			t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
	    /** RSSI */
			t_s8 rssi;
	    /** Cap Info */
			t_u16 cap_info;
	    /** TLV buffer */
			t_u8 tlv_buffer[0];
		} MLAN_PACK_END tdls_discovery_resp;

	/** TDLS setup request */
		MLAN_PACK_START struct _tdls_setup_data {
	    /** peer mac Address */
			t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
	    /** timeout value in milliseconds */
			t_u32 setup_timeout;
	    /** key lifetime in milliseconds */
			t_u32 key_lifetime;
		} MLAN_PACK_END tdls_setup;

	/** TDLS tear down info */
		MLAN_PACK_START struct _tdls_tear_down_data {
	    /** peer mac Address */
			t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
	    /** reason code */
			t_u16 reason_code;
		} MLAN_PACK_END tdls_tear_down, tdls_cmd_resp;

	/** TDLS power mode info */
		MLAN_PACK_START struct _tdls_power_mode_data {
	    /** peer mac Address */
			t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
	    /** Power Mode */
			t_u16 power_mode;
		} MLAN_PACK_END tdls_power_mode;

	/** TDLS channel switch info */
		MLAN_PACK_START struct _tdls_chan_switch {
	    /** peer mac Address */
			t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
	    /** Channel Switch primary channel no */
			t_u8 primary_channel;
	    /** Channel Switch secondary channel offset */
			t_u8 secondary_channel_offset;
	    /** Channel Switch Band */
			t_u8 band;
	    /** Channel Switch time in milliseconds */
			t_u16 switch_time;
	    /** Channel Switch timeout in milliseconds */
			t_u16 switch_timeout;
	    /** Channel Regulatory class*/
			t_u8 regulatory_class;
	    /** peridicity flag*/
			t_u8 periodicity;
		} MLAN_PACK_END tdls_chan_switch;

	/** TDLS channel switch paramters */
		MLAN_PACK_START struct _tdls_cs_params {
	    /** unit time, multiples of 10ms */
			t_u8 unit_time;
	    /** threshold for other link */
			t_u8 threshold_otherlink;
	    /** threshold for direct link */
			t_u8 threshold_directlink;
		} MLAN_PACK_END tdls_cs_params;

	/** tdls disable channel switch */
		MLAN_PACK_START struct _tdls_disable_cs {
	    /** Data*/
			t_u16 data;
		} MLAN_PACK_END tdls_disable_cs;
	/** TDLS debug data */
		MLAN_PACK_START struct _tdls_debug_data {
	    /** debug data */
			t_u16 debug_data;
		} MLAN_PACK_END tdls_debug_data;

	/** TDLS link status Response */
		MLAN_PACK_START struct _tdls_link_status_resp {
	    /** payload length */
			t_u16 payload_len;
	    /** number of links */
			t_u8 active_links;
	    /** structure for link status */
			tdls_each_link_status link_stats[1];
		} MLAN_PACK_END tdls_link_status_resp;

	} u;
} MLAN_PACK_END tdls_all_config;

/** TDLS configuration buffer */
typedef MLAN_PACK_START struct _buf_tdls_config {
    /** TDLS Action */
	t_u16 tdls_action;
    /** TDLS data */
	t_u8 tdls_data[MAX_TDLS_DATA_LEN];
} MLAN_PACK_END mlan_ds_misc_tdls_config;

/** Event structure for tear down */
typedef struct _tdls_tear_down_event {
    /** Peer mac address */
	t_u8 peer_mac_addr[MLAN_MAC_ADDR_LENGTH];
    /** Reason code */
	t_u16 reason_code;
} tdls_tear_down_event;

#ifdef PRAGMA_PACK
#pragma pack(pop)
#endif

/** mlan_callbacks data structure */
typedef struct _mlan_callbacks {
    /** moal_get_fw_data */
	mlan_status (*moal_get_fw_data) (IN t_void *pmoal_handle,
					 IN t_u32 offset,
					 IN t_u32 len, OUT t_u8 *pbuf);
    /** moal_get_hw_spec_complete */
	mlan_status (*moal_get_hw_spec_complete) (IN t_void *pmoal_handle,
						  IN mlan_status status,
						  IN mlan_hw_info * phw,
						  IN pmlan_bss_tbl ptbl);
    /** moal_init_fw_complete */
	mlan_status (*moal_init_fw_complete) (IN t_void *pmoal_handle,
					      IN mlan_status status);
    /** moal_shutdown_fw_complete */
	mlan_status (*moal_shutdown_fw_complete) (IN t_void *pmoal_handle,
						  IN mlan_status status);
    /** moal_send_packet_complete */
	mlan_status (*moal_send_packet_complete) (IN t_void *pmoal_handle,
						  IN pmlan_buffer pmbuf,
						  IN mlan_status status);
    /** moal_recv_complete */
	mlan_status (*moal_recv_complete) (IN t_void *pmoal_handle,
					   IN pmlan_buffer pmbuf,
					   IN t_u32 port,
					   IN mlan_status status);
    /** moal_recv_packet */
	mlan_status (*moal_recv_packet) (IN t_void *pmoal_handle,
					 IN pmlan_buffer pmbuf);
    /** moal_recv_event */
	mlan_status (*moal_recv_event) (IN t_void *pmoal_handle,
					IN pmlan_event pmevent);
    /** moal_ioctl_complete */
	mlan_status (*moal_ioctl_complete) (IN t_void *pmoal_handle,
					    IN pmlan_ioctl_req pioctl_req,
					    IN mlan_status status);

    /** moal_alloc_mlan_buffer */
	mlan_status (*moal_alloc_mlan_buffer) (IN t_void *pmoal_handle,
					       IN t_u32 size,
					       OUT pmlan_buffer *pmbuf);
    /** moal_free_mlan_buffer */
	mlan_status (*moal_free_mlan_buffer) (IN t_void *pmoal_handle,
					      IN pmlan_buffer pmbuf);

    /** moal_write_reg */
	mlan_status (*moal_write_reg) (IN t_void *pmoal_handle,
				       IN t_u32 reg, IN t_u32 data);
    /** moal_read_reg */
	mlan_status (*moal_read_reg) (IN t_void *pmoal_handle,
				      IN t_u32 reg, OUT t_u32 *data);
    /** moal_write_data_sync */
	mlan_status (*moal_write_data_sync) (IN t_void *pmoal_handle,
					     IN pmlan_buffer pmbuf,
					     IN t_u32 port, IN t_u32 timeout);
    /** moal_read_data_sync */
	mlan_status (*moal_read_data_sync) (IN t_void *pmoal_handle,
					    IN OUT pmlan_buffer pmbuf,
					    IN t_u32 port, IN t_u32 timeout);
    /** moal_malloc */
	mlan_status (*moal_malloc) (IN t_void *pmoal_handle,
				    IN t_u32 size,
				    IN t_u32 flag, OUT t_u8 **ppbuf);
    /** moal_mfree */
	mlan_status (*moal_mfree) (IN t_void *pmoal_handle, IN t_u8 *pbuf);
    /** moal_vmalloc */
	mlan_status (*moal_vmalloc) (IN t_void *pmoal_handle,
				     IN t_u32 size, OUT t_u8 **ppbuf);
    /** moal_vfree */
	mlan_status (*moal_vfree) (IN t_void *pmoal_handle, IN t_u8 *pbuf);
    /** moal_memset */
	t_void *(*moal_memset) (IN t_void *pmoal_handle,
				IN t_void *pmem, IN t_u8 byte, IN t_u32 num);
    /** moal_memcpy */
	t_void *(*moal_memcpy) (IN t_void *pmoal_handle,
				IN t_void *pdest,
				IN const t_void *psrc, IN t_u32 num);
    /** moal_memmove */
	t_void *(*moal_memmove) (IN t_void *pmoal_handle,
				 IN t_void *pdest,
				 IN const t_void *psrc, IN t_u32 num);
    /** moal_memcmp */
	t_s32 (*moal_memcmp) (IN t_void *pmoal_handle,
			      IN const t_void *pmem1,
			      IN const t_void *pmem2, IN t_u32 num);
    /** moal_udelay */
	t_void (*moal_udelay) (IN t_void *pmoal_handle, IN t_u32 udelay);
    /** moal_get_system_time */
	mlan_status (*moal_get_system_time) (IN t_void *pmoal_handle,
					     OUT t_u32 *psec, OUT t_u32 *pusec);
    /** moal_init_timer*/
	mlan_status (*moal_init_timer) (IN t_void *pmoal_handle,
					OUT t_void **pptimer,
					IN t_void (*callback) (t_void
							       *pcontext),
					IN t_void *pcontext);
    /** moal_free_timer */
	mlan_status (*moal_free_timer) (IN t_void *pmoal_handle,
					IN t_void *ptimer);
    /** moal_start_timer*/
	mlan_status (*moal_start_timer) (IN t_void *pmoal_handle,
					 IN t_void *ptimer,
					 IN t_u8 periodic, IN t_u32 msec);
    /** moal_stop_timer*/
	mlan_status (*moal_stop_timer) (IN t_void *pmoal_handle,
					IN t_void *ptimer);
    /** moal_init_lock */
	mlan_status (*moal_init_lock) (IN t_void *pmoal_handle,
				       OUT t_void **pplock);
    /** moal_free_lock */
	mlan_status (*moal_free_lock) (IN t_void *pmoal_handle,
				       IN t_void *plock);
    /** moal_spin_lock */
	mlan_status (*moal_spin_lock) (IN t_void *pmoal_handle,
				       IN t_void *plock);
    /** moal_spin_unlock */
	mlan_status (*moal_spin_unlock) (IN t_void *pmoal_handle,
					 IN t_void *plock);
    /** moal_print */
	t_void (*moal_print) (IN t_void *pmoal_handle,
			      IN t_u32 level, IN char *pformat, IN ...
		);
    /** moal_print_netintf */
	t_void (*moal_print_netintf) (IN t_void *pmoal_handle,
				      IN t_u32 bss_index, IN t_u32 level);
    /** moal_assert */
	t_void (*moal_assert) (IN t_void *pmoal_handle, IN t_u32 cond);
    /** moal_hist_data_add */
	t_void (*moal_hist_data_add) (IN t_void *pmoal_handle,
				      IN t_u32 bss_index,
				      IN t_u8 rx_rate,
				      IN t_s8 snr,
				      IN t_s8 nflr, IN t_u8 antenna);
	t_void (*moal_updata_peer_signal) (IN t_void *pmoal_handle,
					   IN t_u32 bss_index,
					   IN t_u8 *peer_addr,
					   IN t_s8 snr, IN t_s8 nflr);
	mlan_status (*moal_get_host_time_ns) (OUT t_u64 *time);
	t_u32 (*moal_do_div) (IN t_u64 num, IN t_u32 base);
} mlan_callbacks, *pmlan_callbacks;

/** Parameter unchanged, use MLAN default setting */
#define ROBUSTCOEX_GPIO_UNCHANGED               0
/** Parameter enabled, override MLAN default setting */
#define ROBUSTCOEX_GPIO_CFG                     1

/** Interrupt Mode SDIO */
#define INT_MODE_SDIO       0
/** Interrupt Mode GPIO */
#define INT_MODE_GPIO       1
/** New mode: GPIO-1 as a duplicated signal of interrupt as appear of SDIO_DAT1 */
#define GPIO_INT_NEW_MODE   255

/** Parameter unchanged, use MLAN default setting */
#define MLAN_INIT_PARA_UNCHANGED     0
/** Parameter enabled, override MLAN default setting */
#define MLAN_INIT_PARA_ENABLED       1
/** Parameter disabled, override MLAN default setting */
#define MLAN_INIT_PARA_DISABLED      2

/** mlan_device data structure */
typedef struct _mlan_device {
    /** MOAL Handle */
	t_void *pmoal_handle;
    /** BSS Attributes */
	mlan_bss_attr bss_attr[MLAN_MAX_BSS_NUM];
    /** Callbacks */
	mlan_callbacks callbacks;
#ifdef MFG_CMD_SUPPORT
    /** MFG mode */
	t_u32 mfg_mode;
#endif
    /** SDIO interrupt mode (0: INT_MODE_SDIO, 1: INT_MODE_GPIO) */
	t_u32 int_mode;
    /** GPIO interrupt pin number */
	t_u32 gpio_pin;
#ifdef DEBUG_LEVEL1
    /** Driver debug bit masks */
	t_u32 drvdbg;
#endif
    /** allocate fixed buffer size for scan beacon buffer*/
	t_u32 fixed_beacon_buffer;
#ifdef SDIO_MULTI_PORT_TX_AGGR
    /** SDIO MPA Tx */
	t_u32 mpa_tx_cfg;
#endif
#ifdef SDIO_MULTI_PORT_RX_AGGR
    /** SDIO MPA Rx */
	t_u32 mpa_rx_cfg;
#endif
	/** SDIO Single port rx aggr */
	t_u8 sdio_rx_aggr_enable;
#if defined(SDIO_MULTI_PORT_TX_AGGR) || defined(SDIO_MULTI_PORT_RX_AGGR)
	/* see blk_queue_max_segment_size */
	t_u32 max_seg_size;
	/* see blk_queue_max_segments */
	t_u16 max_segs;
#endif
    /** Auto deep sleep */
	t_u32 auto_ds;
    /** IEEE PS mode */
	t_u32 ps_mode;
    /** Max Tx buffer size */
	t_u32 max_tx_buf;
#if defined(STA_SUPPORT)
    /** 802.11d configuration */
	t_u32 cfg_11d;
#endif
    /** enable/disable rx work */
	t_u8 rx_work;
    /** dev cap mask */
	t_u32 dev_cap_mask;
    /** oob independent reset */
	t_u32 indrstcfg;
    /** dtim interval */
	t_u32 multi_dtim;
    /** IEEE ps inactivity timeout value */
	t_u32 inact_tmo;
    /** Host sleep wakeup interval */
	t_u32 hs_wake_interval;
    /** GPIO to indicate wakeup source */
	t_u8 indication_gpio;
    /** channel time and mode for DRCS*/
	t_u32 drcs_chantime_mode;
	t_bool fw_region;
} mlan_device, *pmlan_device;

/** MLAN API function prototype */
#define MLAN_API

/** Registration */
MLAN_API mlan_status mlan_register(IN pmlan_device pmdevice,
				   OUT t_void **ppmlan_adapter);

/** Un-registration */
MLAN_API mlan_status mlan_unregister(IN t_void *pmlan_adapter
	);

/** Firmware Downloading */
MLAN_API mlan_status mlan_dnld_fw(IN t_void *pmlan_adapter,
				  IN pmlan_fw_image pmfw);

/** Custom data pass API */
MLAN_API mlan_status mlan_set_init_param(IN t_void *pmlan_adapter,
					 IN pmlan_init_param pparam);

/** Firmware Initialization */
MLAN_API mlan_status mlan_init_fw(IN t_void *pmlan_adapter
	);

/** Firmware Shutdown */
MLAN_API mlan_status mlan_shutdown_fw(IN t_void *pmlan_adapter
	);

/** Main Process */
MLAN_API mlan_status mlan_main_process(IN t_void *pmlan_adapter
	);

/** Rx process */
mlan_status mlan_rx_process(IN t_void *pmlan_adapter, IN t_u8 *rx_pkts);

/** Packet Transmission */
MLAN_API mlan_status mlan_send_packet(IN t_void *pmlan_adapter,
				      IN pmlan_buffer pmbuf);

/** Packet Reception complete callback */
MLAN_API mlan_status mlan_recv_packet_complete(IN t_void *pmlan_adapter,
					       IN pmlan_buffer pmbuf,
					       IN mlan_status status);

/** interrupt handler */
MLAN_API mlan_status mlan_interrupt(IN t_void *pmlan_adapter);

#if defined(SYSKT)
/** GPIO IRQ callback function */
MLAN_API t_void mlan_hs_callback(IN t_void *pctx);
#endif /* SYSKT_MULTI || SYSKT */

MLAN_API t_void mlan_pm_wakeup_card(IN t_void *pmlan_adapter);

MLAN_API t_u8 mlan_is_main_process_running(IN t_void *adapter);

/** mlan ioctl */
MLAN_API mlan_status mlan_ioctl(IN t_void *pmlan_adapter,
				IN pmlan_ioctl_req pioctl_req);
/** mlan select wmm queue */
MLAN_API t_u8 mlan_select_wmm_queue(IN t_void *pmlan_adapter,
				    IN t_u8 bss_num, IN t_u8 tid);
#endif /* !_MLAN_DECL_H_ */
