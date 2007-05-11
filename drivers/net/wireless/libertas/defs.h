/**
  * This header file contains global constant/enum definitions,
  * global variable declaration.
  */
#ifndef _WLAN_DEFS_H_
#define _WLAN_DEFS_H_

#include <linux/spinlock.h>

extern unsigned int libertas_debug;

#ifdef CONFIG_LIBERTAS_DEBUG
#define DEBUG
#define PROC_DEBUG
#endif

#define DRV_NAME		"usb8xxx"

#define lbs_pr_info(format, args...) \
	printk(KERN_INFO DRV_NAME": " format, ## args)
#define lbs_pr_err(format, args...) \
	printk(KERN_ERR DRV_NAME": " format, ## args)
#define lbs_pr_alert(format, args...) \
	printk(KERN_ALERT DRV_NAME": " format, ## args)

#ifdef DEBUG
#define lbs_pr_debug(level, format, args...) \
	do { if (libertas_debug >= level) \
	printk(KERN_INFO DRV_NAME": " format, ##args); } while (0)
#define lbs_dev_dbg(level, device, format, args...) \
        lbs_pr_debug(level, "%s: " format, \
        (device)->bus_id , ## args)

static inline void lbs_dbg_hex(char *prompt, u8 * buf, int len)
{
	int i = 0;

	if (!libertas_debug)
		return;

	printk(KERN_DEBUG "%s: ", prompt);
	for (i = 1; i <= len; i++) {
		printk(KERN_DEBUG "%02x ", (u8) * buf);
		buf++;
	}
	printk("\n");
}
#else
#define lbs_pr_debug(level, format, args...)		do {} while (0)
#define lbs_dev_dbg(level, device, format, args...)	do {} while (0)
#define lbs_dbg_hex(x,y,z)				do {} while (0)
#endif

#define	ENTER()			lbs_pr_debug(1, "Enter: %s, %s:%i\n", \
					__FUNCTION__, __FILE__, __LINE__)
#define	LEAVE()			lbs_pr_debug(1, "Leave: %s, %s:%i\n", \
					__FUNCTION__, __FILE__, __LINE__)

/** Buffer Constants */

/*	The size of SQ memory PPA, DPA are 8 DWORDs, that keep the physical
*	addresses of TxPD buffers. Station has only 8 TxPD available, Whereas
*	driver has more local TxPDs. Each TxPD on the host memory is associated
*	with a Tx control node. The driver maintains 8 RxPD descriptors for
*	station firmware to store Rx packet information.
*
*	Current version of MAC has a 32x6 multicast address buffer.
*
*	802.11b can have up to  14 channels, the driver keeps the
*	BSSID(MAC address) of each APs or Ad hoc stations it has sensed.
*/

#define MRVDRV_MAX_MULTICAST_LIST_SIZE	32
#define MRVDRV_NUM_OF_CMD_BUFFER        10
#define MRVDRV_SIZE_OF_CMD_BUFFER       (2 * 1024)
#define MRVDRV_MAX_CHANNEL_SIZE		14
#define MRVDRV_MAX_BSSID_LIST		64
#define MRVDRV_ASSOCIATION_TIME_OUT	255
#define MRVDRV_SNAP_HEADER_LEN          8

#define	WLAN_UPLD_SIZE			2312
#define DEV_NAME_LEN			32

/** Misc constants */
/* This section defines 802.11 specific contants */

#define MRVDRV_MAX_BSS_DESCRIPTS		16
#define MRVDRV_MAX_REGION_CODE			6

#define MRVDRV_IGNORE_MULTIPLE_DTIM		0xfffe
#define MRVDRV_MIN_MULTIPLE_DTIM		1
#define MRVDRV_MAX_MULTIPLE_DTIM		5
#define MRVDRV_DEFAULT_MULTIPLE_DTIM		1

#define MRVDRV_DEFAULT_LISTEN_INTERVAL		10

#define	MRVDRV_CHANNELS_PER_SCAN		4
#define	MRVDRV_MAX_CHANNELS_PER_SCAN		14

#define MRVDRV_DEBUG_RX_PATH		0x00000001
#define MRVDRV_DEBUG_TX_PATH		0x00000002

#define MRVDRV_MIN_BEACON_INTERVAL		20
#define MRVDRV_MAX_BEACON_INTERVAL		1000
#define MRVDRV_BEACON_INTERVAL			100

/** TxPD status */

/*	Station firmware use TxPD status field to report final Tx transmit
*	result, Bit masks are used to present combined situations.
*/

#define MRVDRV_TxPD_POWER_MGMT_NULL_PACKET 0x01
#define MRVDRV_TxPD_POWER_MGMT_LAST_PACKET 0x08

/** Tx mesh flag */
/* Currently we are using normal WDS flag as mesh flag.
 * TODO: change to proper mesh flag when MAC understands it.
 */
#define TxPD_CONTROL_WDS_FRAME (1<<17)
#define TxPD_MESH_FRAME TxPD_CONTROL_WDS_FRAME

/** RxPD status */

#define MRVDRV_RXPD_STATUS_OK                0x0001

/** RxPD status - Received packet types */
/** Rx mesh flag */
/* Currently we are using normal WDS flag as mesh flag.
 * TODO: change to proper mesh flag when MAC understands it.
 */
#define RxPD_CONTROL_WDS_FRAME (0x40)
#define RxPD_MESH_FRAME RxPD_CONTROL_WDS_FRAME

/** RSSI-related defines */
/*	RSSI constants are used to implement 802.11 RSSI threshold
*	indication. if the Rx packet signal got too weak for 5 consecutive
*	times, miniport driver (driver) will report this event to wrapper
*/

#define MRVDRV_NF_DEFAULT_SCAN_VALUE		(-96)

/** RTS/FRAG related defines */
#define MRVDRV_RTS_MIN_VALUE		0
#define MRVDRV_RTS_MAX_VALUE		2347
#define MRVDRV_FRAG_MIN_VALUE		256
#define MRVDRV_FRAG_MAX_VALUE		2346

/* This is for firmware specific length */
#define EXTRA_LEN	36

#define MRVDRV_ETH_TX_PACKET_BUFFER_SIZE \
	(ETH_FRAME_LEN + sizeof(struct txpd) + EXTRA_LEN)

#define MRVDRV_ETH_RX_PACKET_BUFFER_SIZE \
	(ETH_FRAME_LEN + sizeof(struct rxpd) \
	 + MRVDRV_SNAP_HEADER_LEN + EXTRA_LEN)

#define	CMD_F_HOSTCMD		(1 << 0)
#define FW_CAPINFO_WPA  	(1 << 0)

/** WPA key LENGTH*/
#define MRVL_MAX_KEY_WPA_KEY_LENGTH     32

#define KEY_LEN_WPA_AES			16
#define KEY_LEN_WPA_TKIP		32
#define KEY_LEN_WEP_104			13
#define KEY_LEN_WEP_40			5

#define RF_ANTENNA_1		0x1
#define RF_ANTENNA_2		0x2
#define RF_ANTENNA_AUTO		0xFFFF

#define	BAND_B			(0x01)
#define	BAND_G			(0x02)
#define ALL_802_11_BANDS	(BAND_B | BAND_G)

/** MACRO DEFINITIONS */
#define CAL_NF(NF)			((s32)(-(s32)(NF)))
#define CAL_RSSI(SNR, NF) 		((s32)((s32)(SNR) + CAL_NF(NF)))
#define SCAN_RSSI(RSSI)			(0x100 - ((u8)(RSSI)))

#define DEFAULT_BCN_AVG_FACTOR		8
#define DEFAULT_DATA_AVG_FACTOR		8
#define AVG_SCALE			100
#define CAL_AVG_SNR_NF(AVG, SNRNF, N)         \
                        (((AVG) == 0) ? ((u16)(SNRNF) * AVG_SCALE) : \
                        ((((int)(AVG) * (N -1)) + ((u16)(SNRNF) * \
                        AVG_SCALE))  / N))

#define B_SUPPORTED_RATES		8
#define G_SUPPORTED_RATES		14

#define	WLAN_SUPPORTED_RATES		14

#define	MAX_LEDS			8

#define IS_MESH_FRAME(x) (x->cb[6])
#define SET_MESH_FRAME(x) (x->cb[6]=1)
#define UNSET_MESH_FRAME(x) (x->cb[6]=0)

/** Global Variable Declaration */
typedef struct _wlan_private wlan_private;
typedef struct _wlan_adapter wlan_adapter;
extern const char libertas_driver_version[];
extern u16 libertas_region_code_to_index[MRVDRV_MAX_REGION_CODE];

extern u8 libertas_wlan_data_rates[WLAN_SUPPORTED_RATES];

extern u8 libertas_supported_rates[G_SUPPORTED_RATES];

extern u8 libertas_adhoc_rates_g[G_SUPPORTED_RATES];

extern u8 libertas_adhoc_rates_b[4];

/** ENUM definition*/
/** SNRNF_TYPE */
enum SNRNF_TYPE {
	TYPE_BEACON = 0,
	TYPE_RXPD,
	MAX_TYPE_B
};

/** SNRNF_DATA*/
enum SNRNF_DATA {
	TYPE_NOAVG = 0,
	TYPE_AVG,
	MAX_TYPE_AVG
};

/** WLAN_802_11_POWER_MODE */
enum WLAN_802_11_POWER_MODE {
	wlan802_11powermodecam,
	wlan802_11powermodemax_psp,
	wlan802_11Powermodefast_psp,
	/*not a real mode, defined as an upper bound */
	wlan802_11powemodemax
};

/** PS_STATE */
enum PS_STATE {
	PS_STATE_FULL_POWER,
	PS_STATE_AWAKE,
	PS_STATE_PRE_SLEEP,
	PS_STATE_SLEEP
};

/** DNLD_STATE */
enum DNLD_STATE {
	DNLD_RES_RECEIVED,
	DNLD_DATA_SENT,
	DNLD_CMD_SENT
};

/** WLAN_MEDIA_STATE */
enum WLAN_MEDIA_STATE {
	libertas_connected,
	libertas_disconnected
};

/** WLAN_802_11_PRIVACY_FILTER */
enum WLAN_802_11_PRIVACY_FILTER {
	wlan802_11privfilteracceptall,
	wlan802_11privfilter8021xWEP
};

/** mv_ms_type */
enum mv_ms_type {
	MVMS_DAT = 0,
	MVMS_CMD = 1,
	MVMS_TXDONE = 2,
	MVMS_EVENT
};

/** SNMP_MIB_INDEX_e */
enum SNMP_MIB_INDEX_e {
	desired_bsstype_i = 0,
	op_rateset_i,
	bcnperiod_i,
	dtimperiod_i,
	assocrsp_timeout_i,
	rtsthresh_i,
	short_retrylim_i,
	long_retrylim_i,
	fragthresh_i,
	dot11d_i,
	dot11h_i,
	manufid_i,
	prodID_i,
	manuf_oui_i,
	manuf_name_i,
	manuf_prodname_i,
	manuf_prodver_i,
};

/** KEY_TYPE_ID */
enum KEY_TYPE_ID {
	KEY_TYPE_ID_WEP = 0,
	KEY_TYPE_ID_TKIP,
	KEY_TYPE_ID_AES
};

/** KEY_INFO_WPA (applies to both TKIP and AES/CCMP) */
enum KEY_INFO_WPA {
	KEY_INFO_WPA_MCAST = 0x01,
	KEY_INFO_WPA_UNICAST = 0x02,
	KEY_INFO_WPA_ENABLED = 0x04
};

/** SNMP_MIB_VALUE_e */
enum SNMP_MIB_VALUE_e {
	SNMP_MIB_VALUE_INFRA = 1,
	SNMP_MIB_VALUE_ADHOC
};

/* Default values for fwt commands. */
#define FWT_DEFAULT_METRIC 0
#define FWT_DEFAULT_DIR 1
#define FWT_DEFAULT_SSN 0xffffffff
#define FWT_DEFAULT_DSN 0
#define FWT_DEFAULT_HOPCOUNT 0
#define FWT_DEFAULT_TTL 0
#define FWT_DEFAULT_EXPIRATION 0
#define FWT_DEFAULT_SLEEPMODE 0
#define FWT_DEFAULT_SNR 0

#endif				/* _WLAN_DEFS_H_ */
