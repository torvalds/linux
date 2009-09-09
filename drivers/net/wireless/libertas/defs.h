/**
  * This header file contains global constant/enum definitions,
  * global variable declaration.
  */
#ifndef _LBS_DEFS_H_
#define _LBS_DEFS_H_

#include <linux/spinlock.h>

#ifdef CONFIG_LIBERTAS_DEBUG
#define DEBUG
#define PROC_DEBUG
#endif

#ifndef DRV_NAME
#define DRV_NAME "libertas"
#endif


#define LBS_DEB_ENTER	0x00000001
#define LBS_DEB_LEAVE	0x00000002
#define LBS_DEB_MAIN	0x00000004
#define LBS_DEB_NET	0x00000008
#define LBS_DEB_MESH	0x00000010
#define LBS_DEB_WEXT	0x00000020
#define LBS_DEB_IOCTL	0x00000040
#define LBS_DEB_SCAN	0x00000080
#define LBS_DEB_ASSOC	0x00000100
#define LBS_DEB_JOIN	0x00000200
#define LBS_DEB_11D	0x00000400
#define LBS_DEB_DEBUGFS	0x00000800
#define LBS_DEB_ETHTOOL	0x00001000
#define LBS_DEB_HOST	0x00002000
#define LBS_DEB_CMD	0x00004000
#define LBS_DEB_RX	0x00008000
#define LBS_DEB_TX	0x00010000
#define LBS_DEB_USB	0x00020000
#define LBS_DEB_CS	0x00040000
#define LBS_DEB_FW	0x00080000
#define LBS_DEB_THREAD	0x00100000
#define LBS_DEB_HEX	0x00200000
#define LBS_DEB_SDIO	0x00400000
#define LBS_DEB_SYSFS	0x00800000
#define LBS_DEB_SPI	0x01000000

extern unsigned int lbs_debug;

#ifdef DEBUG
#define LBS_DEB_LL(grp, grpnam, fmt, args...) \
do { if ((lbs_debug & (grp)) == (grp)) \
  printk(KERN_DEBUG DRV_NAME grpnam "%s: " fmt, \
         in_interrupt() ? " (INT)" : "", ## args); } while (0)
#else
#define LBS_DEB_LL(grp, grpnam, fmt, args...) do {} while (0)
#endif

#define lbs_deb_enter(grp) \
  LBS_DEB_LL(grp | LBS_DEB_ENTER, " enter", "%s()\n", __func__);
#define lbs_deb_enter_args(grp, fmt, args...) \
  LBS_DEB_LL(grp | LBS_DEB_ENTER, " enter", "%s(" fmt ")\n", __func__, ## args);
#define lbs_deb_leave(grp) \
  LBS_DEB_LL(grp | LBS_DEB_LEAVE, " leave", "%s()\n", __func__);
#define lbs_deb_leave_args(grp, fmt, args...) \
  LBS_DEB_LL(grp | LBS_DEB_LEAVE, " leave", "%s(), " fmt "\n", \
  __func__, ##args);
#define lbs_deb_main(fmt, args...)      LBS_DEB_LL(LBS_DEB_MAIN, " main", fmt, ##args)
#define lbs_deb_net(fmt, args...)       LBS_DEB_LL(LBS_DEB_NET, " net", fmt, ##args)
#define lbs_deb_mesh(fmt, args...)      LBS_DEB_LL(LBS_DEB_MESH, " mesh", fmt, ##args)
#define lbs_deb_wext(fmt, args...)      LBS_DEB_LL(LBS_DEB_WEXT, " wext", fmt, ##args)
#define lbs_deb_ioctl(fmt, args...)     LBS_DEB_LL(LBS_DEB_IOCTL, " ioctl", fmt, ##args)
#define lbs_deb_scan(fmt, args...)      LBS_DEB_LL(LBS_DEB_SCAN, " scan", fmt, ##args)
#define lbs_deb_assoc(fmt, args...)     LBS_DEB_LL(LBS_DEB_ASSOC, " assoc", fmt, ##args)
#define lbs_deb_join(fmt, args...)      LBS_DEB_LL(LBS_DEB_JOIN, " join", fmt, ##args)
#define lbs_deb_11d(fmt, args...)       LBS_DEB_LL(LBS_DEB_11D, " 11d", fmt, ##args)
#define lbs_deb_debugfs(fmt, args...)   LBS_DEB_LL(LBS_DEB_DEBUGFS, " debugfs", fmt, ##args)
#define lbs_deb_ethtool(fmt, args...)   LBS_DEB_LL(LBS_DEB_ETHTOOL, " ethtool", fmt, ##args)
#define lbs_deb_host(fmt, args...)      LBS_DEB_LL(LBS_DEB_HOST, " host", fmt, ##args)
#define lbs_deb_cmd(fmt, args...)       LBS_DEB_LL(LBS_DEB_CMD, " cmd", fmt, ##args)
#define lbs_deb_rx(fmt, args...)        LBS_DEB_LL(LBS_DEB_RX, " rx", fmt, ##args)
#define lbs_deb_tx(fmt, args...)        LBS_DEB_LL(LBS_DEB_TX, " tx", fmt, ##args)
#define lbs_deb_fw(fmt, args...)        LBS_DEB_LL(LBS_DEB_FW, " fw", fmt, ##args)
#define lbs_deb_usb(fmt, args...)       LBS_DEB_LL(LBS_DEB_USB, " usb", fmt, ##args)
#define lbs_deb_usbd(dev, fmt, args...) LBS_DEB_LL(LBS_DEB_USB, " usbd", "%s:" fmt, dev_name(dev), ##args)
#define lbs_deb_cs(fmt, args...)        LBS_DEB_LL(LBS_DEB_CS, " cs", fmt, ##args)
#define lbs_deb_thread(fmt, args...)    LBS_DEB_LL(LBS_DEB_THREAD, " thread", fmt, ##args)
#define lbs_deb_sdio(fmt, args...)      LBS_DEB_LL(LBS_DEB_SDIO, " sdio", fmt, ##args)
#define lbs_deb_sysfs(fmt, args...)     LBS_DEB_LL(LBS_DEB_SYSFS, " sysfs", fmt, ##args)
#define lbs_deb_spi(fmt, args...)       LBS_DEB_LL(LBS_DEB_SPI, " spi", fmt, ##args)

#define lbs_pr_info(format, args...) \
	printk(KERN_INFO DRV_NAME": " format, ## args)
#define lbs_pr_err(format, args...) \
	printk(KERN_ERR DRV_NAME": " format, ## args)
#define lbs_pr_alert(format, args...) \
	printk(KERN_ALERT DRV_NAME": " format, ## args)

#ifdef DEBUG
static inline void lbs_deb_hex(unsigned int grp, const char *prompt, u8 *buf, int len)
{
	int i = 0;

	if (len &&
	    (lbs_debug & LBS_DEB_HEX) &&
	    (lbs_debug & grp))
	{
		for (i = 1; i <= len; i++) {
			if ((i & 0xf) == 1) {
				if (i != 1)
					printk("\n");
				printk(DRV_NAME " %s: ", prompt);
			}
			printk("%02x ", (u8) * buf);
			buf++;
		}
		printk("\n");
	}
}
#else
#define lbs_deb_hex(grp,prompt,buf,len)	do {} while (0)
#endif



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
#define LBS_NUM_CMD_BUFFERS             10
#define LBS_CMD_BUFFER_SIZE             (2 * 1024)
#define MRVDRV_MAX_CHANNEL_SIZE		14
#define MRVDRV_ASSOCIATION_TIME_OUT	255
#define MRVDRV_SNAP_HEADER_LEN          8

#define	LBS_UPLD_SIZE			2312
#define DEV_NAME_LEN			32

/* Wake criteria for HOST_SLEEP_CFG command */
#define EHS_WAKE_ON_BROADCAST_DATA	0x0001
#define EHS_WAKE_ON_UNICAST_DATA	0x0002
#define EHS_WAKE_ON_MAC_EVENT		0x0004
#define EHS_WAKE_ON_MULTICAST_DATA	0x0008
#define EHS_REMOVE_WAKEUP		0xFFFFFFFF
/* Wake rules for Host_Sleep_CFG command */
#define WOL_RULE_NET_TYPE_INFRA_OR_IBSS	0x00
#define WOL_RULE_NET_TYPE_MESH		0x10
#define WOL_RULE_ADDR_TYPE_BCAST	0x01
#define WOL_RULE_ADDR_TYPE_MCAST	0x08
#define WOL_RULE_ADDR_TYPE_UCAST	0x02
#define WOL_RULE_OP_AND			0x01
#define WOL_RULE_OP_OR			0x02
#define WOL_RULE_OP_INVALID		0xFF
#define WOL_RESULT_VALID_CMD		0
#define WOL_RESULT_NOSPC_ERR		1
#define WOL_RESULT_EEXIST_ERR		2

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

#define MRVDRV_MIN_BEACON_INTERVAL		20
#define MRVDRV_MAX_BEACON_INTERVAL		1000
#define MRVDRV_BEACON_INTERVAL			100

#define MARVELL_MESH_IE_LENGTH		9

/* Values used to populate the struct mrvl_mesh_ie.  The only time you need this
 * is when enabling the mesh using CMD_MESH_CONFIG.
 */
#define MARVELL_MESH_IE_TYPE		4
#define MARVELL_MESH_IE_SUBTYPE		0
#define MARVELL_MESH_IE_VERSION		0
#define MARVELL_MESH_PROTO_ID_HWMP	0
#define MARVELL_MESH_METRIC_ID		0
#define MARVELL_MESH_CAPABILITY		0

/** INT status Bit Definition*/
#define MRVDRV_TX_DNLD_RDY		0x0001
#define MRVDRV_RX_UPLD_RDY		0x0002
#define MRVDRV_CMD_DNLD_RDY		0x0004
#define MRVDRV_CMD_UPLD_RDY		0x0008
#define MRVDRV_CARDEVENT		0x0010

/* Automatic TX control default levels */
#define POW_ADAPT_DEFAULT_P0 13
#define POW_ADAPT_DEFAULT_P1 15
#define POW_ADAPT_DEFAULT_P2 18
#define TPC_DEFAULT_P0 5
#define TPC_DEFAULT_P1 10
#define TPC_DEFAULT_P2 13

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

/** Mesh interface ID */
#define MESH_IFACE_ID					0x0001
/** Mesh id should be in bits 14-13-12 */
#define MESH_IFACE_BIT_OFFSET				0x000c
/** Mesh enable bit in FW capability */
#define MESH_CAPINFO_ENABLE_MASK			(1<<16)

/** FW definition from Marvell v4 */
#define MRVL_FW_V4					(0x04)
/** FW definition from Marvell v5 */
#define MRVL_FW_V5					(0x05)
/** FW definition from Marvell v10 */
#define MRVL_FW_V10					(0x0a)
/** FW major revision definition */
#define MRVL_FW_MAJOR_REV(x)				((x)>>24)

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
#define FW_CAPINFO_PS  		(1 << 1)
#define FW_CAPINFO_FIRMWARE_UPGRADE	(1 << 13)
#define FW_CAPINFO_BOOT2_UPGRADE	(1<<14)
#define FW_CAPINFO_PERSISTENT_CONFIG	(1<<15)

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

#define MAX_RATES			14

#define	MAX_LEDS			8

/** Global Variable Declaration */
extern const char lbs_driver_version[];
extern u16 lbs_region_code_to_index[MRVDRV_MAX_REGION_CODE];

extern u8 lbs_bg_rates[MAX_RATES];

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

/** LBS_802_11_POWER_MODE */
enum LBS_802_11_POWER_MODE {
	LBS802_11POWERMODECAM,
	LBS802_11POWERMODEMAX_PSP,
	LBS802_11POWERMODEFAST_PSP,
	/*not a real mode, defined as an upper bound */
	LBS802_11POWEMODEMAX
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
	DNLD_CMD_SENT,
	DNLD_BOOTCMD_SENT,
};

/** LBS_MEDIA_STATE */
enum LBS_MEDIA_STATE {
	LBS_CONNECTED,
	LBS_DISCONNECTED
};

/** LBS_802_11_PRIVACY_FILTER */
enum LBS_802_11_PRIVACY_FILTER {
	LBS802_11PRIVFILTERACCEPTALL,
	LBS802_11PRIVFILTER8021XWEP
};

/** mv_ms_type */
enum mv_ms_type {
	MVMS_DAT = 0,
	MVMS_CMD = 1,
	MVMS_TXDONE = 2,
	MVMS_EVENT
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

/** mesh_fw_ver */
enum _mesh_fw_ver {
	MESH_NONE = 0, /* MESH is not supported */
	MESH_FW_OLD,   /* MESH is supported in FW V5 */
	MESH_FW_NEW,   /* MESH is supported in FW V10 and newer */
};

/* Default values for fwt commands. */
#define FWT_DEFAULT_METRIC 0
#define FWT_DEFAULT_DIR 1
/* Default Rate, 11Mbps */
#define FWT_DEFAULT_RATE 3
#define FWT_DEFAULT_SSN 0xffffffff
#define FWT_DEFAULT_DSN 0
#define FWT_DEFAULT_HOPCOUNT 0
#define FWT_DEFAULT_TTL 0
#define FWT_DEFAULT_EXPIRATION 0
#define FWT_DEFAULT_SLEEPMODE 0
#define FWT_DEFAULT_SNR 0

#endif
