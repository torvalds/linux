/*
 * Common header file for all error codes.
 *
 * Copyright (C) 2020, Broadcom.
 *
 *      Unless you and Broadcom execute a separate written software license
 * agreement governing use of this software, this software is licensed to you
 * under the terms of the GNU General Public License version 2 (the "GPL"),
 * available at http://www.broadcom.com/licenses/GPLv2.php, with the
 * following added to such license:
 *
 *      As a special exception, the copyright holders of this software give you
 * permission to link this software with independent modules, and to copy and
 * distribute the resulting executable under terms of your choice, provided that
 * you also meet, for each linked independent module, the terms and conditions of
 * the license of that module.  An independent module is a module which is not
 * derived from this software.  The special exception does not apply to any
 * modifications of the software.
 *
 *
 * <<Broadcom-WL-IPTag/Dual:>>
 *
 *
 *
 */

#ifndef	_bcmerror_h_
#define	_bcmerror_h_

#include <typedefs.h>

/* Use error codes from this file only if BCMUTILS_ERR_CODES is defined. */
#ifdef BCMUTILS_ERR_CODES

/* NOTE re: Module specific error codes.
 *
 * BCME_.. error codes are extended by various features - e.g. FTM, NAN, SAE etc.
 * The current process is to allocate a range of 1024 negative 32 bit integers to
 * each module that extends the error codes to indicate a module specific status.
 *
 * The next range to use is below. If that range is used for a new feature, please
 * update the range to be used by the next feature.
 *
 * Next available (inclusive) range: [-8*1024 + 1, -7*1024]
 *
 * Common error codes use BCME_ prefix. Firmware (wl) components should use the
 * convention to prefix the error code name with WL_<Component>_E_ (e.g. WL_NAN_E_?).
 * Non-wl components, other than common error codes use BCM_<Componennt>_E_
 * prefix(e.g. BCM_FWSIGN_E_).
 *
 * End Note
 */

typedef int bcmerror_t;

/*
 * error codes could be added but the defined ones shouldn't be changed/deleted
 * these error codes are exposed to the user code
 * when ever a new error code is added to this list
 * please update errorstring table with the related error string and
 * update osl files with os specific errorcode map
*/

#define BCME_OK				0	/* Success */
#define BCME_ERROR			-1	/* Error generic */
#define BCME_BADARG			-2	/* Bad Argument */
#define BCME_BADOPTION			-3	/* Bad option */
#define BCME_NOTUP			-4	/* Not up */
#define BCME_NOTDOWN			-5	/* Not down */
#define BCME_NOTAP			-6	/* Not AP */
#define BCME_NOTSTA			-7	/* Not STA  */
#define BCME_BADKEYIDX			-8	/* BAD Key Index */
#define BCME_RADIOOFF			-9	/* Radio Off */
#define BCME_NOTBANDLOCKED		-10	/* Not  band locked */
#define BCME_NOCLK			-11	/* No Clock */
#define BCME_BADRATESET			-12	/* BAD Rate valueset */
#define BCME_BADBAND			-13	/* BAD Band */
#define BCME_BUFTOOSHORT		-14	/* Buffer too short */
#define BCME_BUFTOOLONG			-15	/* Buffer too long */
#define BCME_BUSY			-16	/* Busy */
#define BCME_NOTASSOCIATED		-17	/* Not Associated */
#define BCME_BADSSIDLEN			-18	/* Bad SSID len */
#define BCME_OUTOFRANGECHAN		-19	/* Out of Range Channel */
#define BCME_BADCHAN			-20	/* Bad Channel */
#define BCME_BADADDR			-21	/* Bad Address */
#define BCME_NORESOURCE			-22	/* Not Enough Resources */
#define BCME_UNSUPPORTED		-23	/* Unsupported */
#define BCME_BADLEN			-24	/* Bad length */
#define BCME_NOTREADY			-25	/* Not Ready */
#define BCME_EPERM			-26	/* Not Permitted */
#define BCME_NOMEM			-27	/* No Memory */
#define BCME_ASSOCIATED			-28	/* Associated */
#define BCME_RANGE			-29	/* Not In Range */
#define BCME_NOTFOUND			-30	/* Not Found */
#define BCME_WME_NOT_ENABLED		-31	/* WME Not Enabled */
#define BCME_TSPEC_NOTFOUND		-32	/* TSPEC Not Found */
#define BCME_ACM_NOTSUPPORTED		-33	/* ACM Not Supported */
#define BCME_NOT_WME_ASSOCIATION	-34	/* Not WME Association */
#define BCME_SDIO_ERROR			-35	/* SDIO Bus Error */
#define BCME_DONGLE_DOWN		-36	/* Dongle Not Accessible */
#define BCME_VERSION			-37	/* Incorrect version */
#define BCME_TXFAIL			-38	/* TX failure */
#define BCME_RXFAIL			-39	/* RX failure */
#define BCME_NODEVICE			-40	/* Device not present */
#define BCME_NMODE_DISABLED		-41	/* NMODE disabled */
#define BCME_MSCH_DUP_REG		-42	/* Duplicate slot registration */
#define BCME_SCANREJECT			-43	/* reject scan request */
#define BCME_USAGE_ERROR		-44	/* WLCMD usage error */
#define BCME_IOCTL_ERROR		-45	/* WLCMD ioctl error */
#define BCME_SERIAL_PORT_ERR		-46	/* RWL serial port error */
#define BCME_DISABLED			-47	/* Disabled in this build */
#define BCME_DECERR			-48	/* Decrypt error */
#define BCME_ENCERR			-49	/* Encrypt error */
#define BCME_MICERR			-50	/* Integrity/MIC error */
#define BCME_REPLAY			-51	/* Replay */
#define BCME_IE_NOTFOUND		-52	/* IE not found */
#define BCME_DATA_NOTFOUND		-53	/* Complete data not found in buffer */
#define BCME_NOT_GC			-54	/* expecting a group client */
#define BCME_PRS_REQ_FAILED		-55	/* GC presence req failed to sent */
#define BCME_NO_P2P_SE			-56	/* Could not find P2P-Subelement */
#define BCME_NOA_PND			-57	/* NoA pending, CB shuld be NULL */
#define BCME_FRAG_Q_FAILED		-58	/* queueing 80211 frag failedi */
#define BCME_GET_AF_FAILED		-59	/* Get p2p AF pkt failed */
#define BCME_MSCH_NOTREADY		-60	/* scheduler not ready */
#define BCME_IOV_LAST_CMD		-61	/* last batched iov sub-command */
#define BCME_MINIPMU_CAL_FAIL		-62	/* MiniPMU cal failed */
#define BCME_RCAL_FAIL			-63	/* Rcal failed */
#define BCME_LPF_RCCAL_FAIL		-64	/* RCCAL failed */
#define BCME_DACBUF_RCCAL_FAIL		-65	/* RCCAL failed */
#define BCME_VCOCAL_FAIL		-66	/* VCOCAL failed */
#define BCME_BANDLOCKED			-67	/* interface is restricted to a band */
#define BCME_BAD_IE_DATA		-68	/* Recieved ie with invalid/bad data */
#define BCME_REG_FAILED			-69	/* Generic registration failed */
#define BCME_NOCHAN			-70	/* Registration with 0 chans in list */
#define BCME_PKTTOSS			-71	/* Pkt tossed */
#define BCME_DNGL_DEVRESET		-72	/* dongle re-attach during DEVRESET */
#define BCME_ROAM			-73	/* Roam related failures */
#define BCME_NO_SIG_FILE		-74	/* Signature file is missing */

#define BCME_LAST			BCME_NO_SIG_FILE

#define BCME_NOTENABLED BCME_DISABLED

/* This error code is *internal* to the driver, and is not propogated to users. It should
 * only be used by IOCTL patch handlers as an indication that it did not handle the IOCTL.
 * (Since the error code is internal, an entry in 'BCMERRSTRINGTABLE' is not required,
 * nor does it need to be part of any OSL driver-to-OS error code mapping).
 */
#define BCME_IOCTL_PATCH_UNSUPPORTED	-9999
#if (BCME_LAST <= BCME_IOCTL_PATCH_UNSUPPORTED)
	#error "BCME_LAST <= BCME_IOCTL_PATCH_UNSUPPORTED"
#endif

/* These are collection of BCME Error strings */
#define BCMERRSTRINGTABLE {		\
	"OK",				\
	"Undefined error",		\
	"Bad Argument",			\
	"Bad Option",			\
	"Not up",			\
	"Not down",			\
	"Not AP",			\
	"Not STA",			\
	"Bad Key Index",		\
	"Radio Off",			\
	"Not band locked",		\
	"No clock",			\
	"Bad Rate valueset",		\
	"Bad Band",			\
	"Buffer too short",		\
	"Buffer too long",		\
	"Busy",				\
	"Not Associated",		\
	"Bad SSID len",			\
	"Out of Range Channel",		\
	"Bad Channel",			\
	"Bad Address",			\
	"Not Enough Resources",		\
	"Unsupported",			\
	"Bad length",			\
	"Not Ready",			\
	"Not Permitted",		\
	"No Memory",			\
	"Associated",			\
	"Not In Range",			\
	"Not Found",			\
	"WME Not Enabled",		\
	"TSPEC Not Found",		\
	"ACM Not Supported",		\
	"Not WME Association",		\
	"SDIO Bus Error",		\
	"Dongle Not Accessible",	\
	"Incorrect version",		\
	"TX Failure",			\
	"RX Failure",			\
	"Device Not Present",		\
	"NMODE Disabled",		\
	"Host Offload in device",	\
	"Scan Rejected",		\
	"WLCMD usage error",		\
	"WLCMD ioctl error",		\
	"RWL serial port error",	\
	"Disabled",			\
	"Decrypt error",		\
	"Encrypt error",		\
	"MIC error",			\
	"Replay",			\
	"IE not found",			\
	"Data not found",		\
	"NOT GC",			\
	"PRS REQ FAILED",		\
	"NO P2P SubElement",		\
	"NOA Pending",			\
	"FRAG Q FAILED",		\
	"GET ActionFrame failed",	\
	"scheduler not ready",		\
	"Last IOV batched sub-cmd",	\
	"Mini PMU Cal failed",		\
	"R-cal failed",			\
	"LPF RC Cal failed",		\
	"DAC buf RC Cal failed",	\
	"VCO Cal failed",		\
	"band locked",			\
	"Recieved ie with invalid data", \
	"registration failed",		\
	"Registration with zero channels", \
	"pkt toss",			\
	"Dongle Devreset",		\
	"Critical roam in progress",	\
	"Signature file is missing",	\
}

/** status - TBD BCME_ vs proxd status - range reserved for BCME_ */
enum {
	WL_PROXD_E_LAST			= -1057,
	WL_PROXD_E_ASSOC_INPROG		= -1057,
	WL_PROXD_E_NOAVAIL		= -1056,
	WL_PROXD_E_EXT_SCHED		= -1055,
	WL_PROXD_E_NOT_BCM		= -1054,
	WL_PROXD_E_FRAME_TYPE		= -1053,
	WL_PROXD_E_VERNOSUPPORT		= -1052,
	WL_PROXD_E_SEC_NOKEY		= -1051,
	WL_PROXD_E_SEC_POLICY		= -1050,
	WL_PROXD_E_SCAN_INPROCESS	= -1049,
	WL_PROXD_E_BAD_PARTIAL_TSF	= -1048,
	WL_PROXD_E_SCANFAIL		= -1047,
	WL_PROXD_E_NOTSF		= -1046,
	WL_PROXD_E_POLICY		= -1045,
	WL_PROXD_E_INCOMPLETE		= -1044,
	WL_PROXD_E_OVERRIDDEN		= -1043,
	WL_PROXD_E_ASAP_FAILED		= -1042,
	WL_PROXD_E_NOTSTARTED		= -1041,
	WL_PROXD_E_INVALIDMEAS		= -1040,
	WL_PROXD_E_INCAPABLE		= -1039,
	WL_PROXD_E_MISMATCH		= -1038,
	WL_PROXD_E_DUP_SESSION		= -1037,
	WL_PROXD_E_REMOTE_FAIL		= -1036,
	WL_PROXD_E_REMOTE_INCAPABLE	= -1035,
	WL_PROXD_E_SCHED_FAIL		= -1034,
	WL_PROXD_E_PROTO		= -1033,
	WL_PROXD_E_EXPIRED		= -1032,
	WL_PROXD_E_TIMEOUT		= -1031,
	WL_PROXD_E_NOACK		= -1030,
	WL_PROXD_E_DEFERRED		= -1029,
	WL_PROXD_E_INVALID_SID		= -1028,
	WL_PROXD_E_REMOTE_CANCEL	= -1027,
	WL_PROXD_E_CANCELED		= -1026,	/**< local */
	WL_PROXD_E_INVALID_SESSION	= -1025,
	WL_PROXD_E_BAD_STATE		= -1024,
	WL_PROXD_E_START		= -1024,
	WL_PROXD_E_ERROR		= -1,
	WL_PROXD_E_OK			= 0
};
typedef int32 wl_proxd_status_t;

/** status - TBD BCME_ vs NAN status - range reserved for BCME_ */
enum {
	/* add new status here... */
	WL_NAN_E_NO_ACTION		= -2136,	/* status for no action */
	WL_NAN_E_INVALID_TOKEN		= -2135,	/* invalid token or mismatch */
	WL_NAN_E_INVALID_ATTR		= -2134,	/* generic invalid attr error */
	WL_NAN_E_INVALID_NDL_ATTR	= -2133,	/* invalid NDL attribute */
	WL_NAN_E_SCB_NORESOURCE		= -2132,	/* no more peer scb available */
	WL_NAN_E_PEER_NOTAVAIL		= -2131,
	WL_NAN_E_SCB_EXISTS		= -2130,
	WL_NAN_E_INVALID_PEER_NDI	= -2129,
	WL_NAN_E_INVALID_LOCAL_NDI	= -2128,
	WL_NAN_E_ALREADY_EXISTS		= -2127,	/* generic NAN error for duplication */
	WL_NAN_E_EXCEED_MAX_NUM_MAPS	= -2126,
	WL_NAN_E_INVALID_DEV_CHAN_SCHED	= -2125,
	WL_NAN_E_INVALID_PEER_BLOB_TYPE	= -2124,
	WL_NAN_E_INVALID_LCL_BLOB_TYPE	= -2123,
	WL_NAN_E_BCMC_PDPA		= -2122,	/* BCMC NAF PDPA */
	WL_NAN_E_TIMEOUT		= -2121,
	WL_NAN_E_HOST_CFG		= -2120,
	WL_NAN_E_NO_ACK			= -2119,
	WL_NAN_E_SECINST_FAIL		= -2118,
	WL_NAN_E_REJECT_NDL		= -2117,	/* generic NDL rejection error */
	WL_NAN_E_INVALID_NDP_ATTR	= -2116,
	WL_NAN_E_HOST_REJECTED		= -2115,
	WL_NAN_E_PCB_NORESOURCE		= -2114,
	WL_NAN_E_NDC_EXISTS		= -2113,
	WL_NAN_E_NO_NDC_ENTRY_AVAIL	= -2112,
	WL_NAN_E_INVALID_NDC_ENTRY      = -2111,
	WL_NAN_E_SD_TX_LIST_FULL        = -2110,
	WL_NAN_E_SVC_SUB_LIST_FULL      = -2109,
	WL_NAN_E_SVC_PUB_LIST_FULL      = -2108,
	WL_NAN_E_SDF_MAX_LEN_EXCEEDED   = -2107,
	WL_NAN_E_ZERO_CRB		= -2106,	/* no CRB between local and peer */
	WL_NAN_E_PEER_NDC_NOT_SELECTED	= -2105,	/* peer ndc not selected */
	WL_NAN_E_DAM_CHAN_CONFLICT	= -2104,	/* dam schedule channel conflict */
	WL_NAN_E_DAM_SCHED_PERIOD	= -2103,	/* dam schedule period mismatch */
	WL_NAN_E_LCL_NDC_NOT_SELECTED	= -2102,	/* local selected ndc not configured */
	WL_NAN_E_NDL_QOS_INVALID_NA	= -2101,	/* na doesn't comply with ndl qos */
	WL_NAN_E_CLEAR_NAF_WITH_SA_AS_RNDI = -2100,	/* rx clear naf with peer rndi */
	WL_NAN_E_SEC_CLEAR_PKT		= -2099,	/* rx clear pkt from a peer with sec_sa */
	WL_NAN_E_PROT_NON_PDPA_NAF	= -2098,	/* rx protected non PDPA frame */
	WL_NAN_E_DAM_DOUBLE_REMOVE	= -2097,	/* remove peer schedule already removed */
	WL_NAN_E_DAM_DOUBLE_MERGE	= -2096,	/* merge peer schedule already merged */
	WL_NAN_E_DAM_REJECT_INVALID	= -2095,	/* reject for invalid schedule */
	WL_NAN_E_DAM_REJECT_RANGE	= -2094,
	WL_NAN_E_DAM_REJECT_QOS		= -2093,
	WL_NAN_E_DAM_REJECT_NDC		= -2092,
	WL_NAN_E_DAM_REJECT_PEER_IMMUT	= -2091,
	WL_NAN_E_DAM_REJECT_LCL_IMMUT	= -2090,
	WL_NAN_E_DAM_EXCEED_NUM_SCHED	= -2089,
	WL_NAN_E_DAM_INVALID_SCHED_MAP	= -2088,	/* invalid schedule map list */
	WL_NAN_E_DAM_INVALID_LCL_SCHED	= -2087,
	WL_NAN_E_INVALID_MAP_ID		= -2086,
	WL_NAN_E_CHAN_OVERLAP_ACROSS_MAP = -2085,
	WL_NAN_E_INVALID_CHAN_LIST	= -2084,
	WL_NAN_E_INVALID_RANGE_TBMP	= -2083,
	WL_NAN_E_INVALID_IMMUT_SCHED	= -2082,
	WL_NAN_E_INVALID_NDC_ATTR	= -2081,
	WL_NAN_E_INVALID_TIME_BITMAP	= -2080,
	WL_NAN_E_INVALID_NA_ATTR	= -2079,
	WL_NAN_E_NO_NA_ATTR_IN_AVAIL_MAP = -2078,	/* no na attr saved in avail map */
	WL_NAN_E_INVALID_MAP_IDX	= -2077,
	WL_NAN_E_SEC_SA_NOTFOUND	= -2076,
	WL_NAN_E_BSSCFG_NOTFOUND	= -2075,
	WL_NAN_E_SCB_NOTFOUND		= -2074,
	WL_NAN_E_NCS_SK_KDESC_TYPE      = -2073,
	WL_NAN_E_NCS_SK_KEY_DESC_VER    = -2072,	/* key descr ver */
	WL_NAN_E_NCS_SK_KEY_TYPE        = -2071,	/* key descr type */
	WL_NAN_E_NCS_SK_KEYINFO_FAIL    = -2070,	/* key info (generic) */
	WL_NAN_E_NCS_SK_KEY_LEN         = -2069,	/* key len */
	WL_NAN_E_NCS_SK_KDESC_NOT_FOUND = -2068,	/* key desc not found */
	WL_NAN_E_NCS_SK_INVALID_PARAMS  = -2067,	/* invalid args */
	WL_NAN_E_NCS_SK_KDESC_INVALID   = -2066,	/* key descr is not valid */
	WL_NAN_E_NCS_SK_NONCE_MISMATCH  = -2065,
	WL_NAN_E_NCS_SK_KDATA_SAVE_FAIL = -2064,	/* not able to save key data */
	WL_NAN_E_NCS_SK_AUTH_TOKEN_CALC_FAIL = -2063,
	WL_NAN_E_NCS_SK_PTK_CALC_FAIL   = -2062,
	WL_NAN_E_INVALID_STARTOFFSET	= -2061,
	WL_NAN_E_BAD_NA_ENTRY_TYPE	= -2060,
	WL_NAN_E_INVALID_CHANBMP	= -2059,
	WL_NAN_E_INVALID_OP_CLASS	= -2058,
	WL_NAN_E_NO_IES			= -2057,
	WL_NAN_E_NO_PEER_ENTRY_AVAIL	= -2056,
	WL_NAN_E_INVALID_PEER		= -2055,
	WL_NAN_E_PEER_EXISTS		= -2054,
	WL_NAN_E_PEER_NOTFOUND		= -2053,
	WL_NAN_E_NO_MEM			= -2052,
	WL_NAN_E_INVALID_OPTION		= -2051,
	WL_NAN_E_INVALID_BAND		= -2050,
	WL_NAN_E_INVALID_MAC		= -2049,
	WL_NAN_E_BAD_INSTANCE		= -2048,
	/* NAN status code reserved from -2048 to -3071 */
	/* Do NOT add new status below -2048 */
	WL_NAN_E_ERROR			= -1,
	WL_NAN_E_OK			= 0
};

/* SAE (Simultaneous Authentication of Equals) status codes.
 * SAE status codes are reserved from -3072 to -4095 (1K)
 */
enum {
	WL_SAE_E_AUTH_FAILURE			= -3072,
	/* Discard silently */
	WL_SAE_E_AUTH_DISCARD			= -3073,
	/* Authentication in progress */
	WL_SAE_E_AUTH_CONTINUE			= -3074,
	/* Invalid scalar/elt */
	WL_SAE_E_AUTH_COMMIT_INVALID		= -3075,
	/* Invalid confirm token */
	WL_SAE_E_AUTH_CONFIRM_INVALID		= -3076,
	/* Peer scalar validation failure */
	WL_SAE_E_CRYPTO_SCALAR_VALIDATION	= -3077,
	/* Peer element prime validation failure */
	WL_SAE_E_CRYPTO_ELE_PRIME_VALIDATION	= -3078,
	/* Peer element is not on the curve */
	WL_SAE_E_CRYPTO_ELE_NOT_ON_CURVE	= -3079,
	/* Generic EC error (eliptic curve related) */
	WL_SAE_E_CRYPTO_EC_ERROR		= -3080,
	/* Both local and peer mac addrs are same */
	WL_SAE_E_CRYPTO_EQUAL_MACADDRS		= -3081,
	/* Loop exceeded in deriving the scalar */
	WL_SAE_E_CRYPTO_SCALAR_ITER_EXCEEDED	= -3082,
	/* ECC group is unsupported */
	WL_SAE_E_CRYPTO_UNSUPPORTED_GROUP	= -3083,
	/* Exceeded the hunting-and-pecking counter */
	WL_SAE_E_CRYPTO_PWE_COUNTER_EXCEEDED	= -3084,
	/* SAE crypto component is not initialized */
	WL_SAE_E_CRYPTO_NOT_INITED		= -3085,
	/* bn_get has failed */
	WL_SAE_E_CRYPTO_BN_GET_ERROR		= -3086,
	/* bn_set has failed */
	WL_SAE_E_CRYPTO_BN_SET_ERROR		= -3087,
	/* PMK is not computed yet */
	WL_SAE_E_CRYPTO_PMK_UNAVAILABLE		= -3088,
	/* Peer confirm did not match */
	WL_SAE_E_CRYPTO_CONFIRM_MISMATCH	= -3089,
	/* Element K is at infinity no the curve */
	WL_SAE_E_CRYPTO_KEY_AT_INFINITY		= -3090,
	/* SAE Crypto private data magic number mismatch */
	WL_SAE_E_CRYPTO_PRIV_MAGIC_MISMATCH	= -3091,
	/* Max retry exhausted */
	WL_SAE_E_MAX_RETRY_LIMIT_REACHED	= -3092,
	/* peer sent password ID mismatch to local */
	WL_SAE_E_AUTH_PEER_PWDID_MISMATCH	= -3093,
	/* user not configured password */
	WL_SAE_E_AUTH_PASSWORD_NOT_CONFIGURED	= -3094,
	/* user not configured password ID */
	WL_SAE_E_AUTH_PWDID_NOT_CONFIGURED	= -3095,
	/* Anti-clogging token mismatch */
	WL_SAE_E_AUTH_ANTI_CLOG_MISMATCH	= -3096,
	/* SAE PWE method mismatch */
	WL_SAE_E_AUTH_PWE_MISMATCH              = -3097
};

/*
 * Firmware signing error code range: -4096...-5119
 */
enum {
	/* okay */
	BCM_FWSIGN_E_OK				= 0,

	/* Operation is in progress */
	BCM_FWSIGN_E_INPROGRESS			= -4096,

	/* version mismatch */
	BCM_FWSIGN_E_VERSION			= -4097,

	/* key not found */
	BCM_FWSIGN_E_KEY_NOT_FOUND		= -4098,

	/* key found, but is not valid (revoked) */
	BCM_FWSIGN_E_KEY_NOT_VALID		= -4099,

	/* Cipher suite id mismatch for the key */
	BCM_FWSIGN_E_CS_ID_MISMATCH		= -4100,

	/* Signature does not match */
	BCM_FWSIGN_E_SIGNATURE			= -4101,

	/* Continue */
	BCM_FWSIGN_E_CONTINUE			= -4102,

	/* Heap is too small */
	BCM_FWSIGN_E_HEAP_TOO_SMALL		= -4103,

	/* Allocation of bn ctx failed */
	BCM_FWSIGN_E_BN_CTX_ALLOC_FAILED	= -4104,

	/* possible bug */
	BCM_FWSIGN_E_BUGCHECK			= -4105,

	/* chosen key is invalid */
	BCM_FWSIGN_E_INVALID_KEY		= -4106,

	/* signature is invalid */
	BCM_FWSIGN_E_INVALID_SIGNATURE		= -4107,

	/* signature tlv missing */
	BCM_FWSIGN_E_NO_CSID_SIG		= -4108,

	/* chosen key is invalid */
	BCM_FWSIGN_E_REVOKED_KEY		= -4109,

	/* signature has no matching valid key in ROM */
	BCM_FWSIGN_E_NO_OTP_FOR_ROM_KEY		= -4110,

	/* Compression not supported */
	BCM_FWSIGN_E_COMPNOTSUP			= -4111,

	/* OTP read error */
	BCM_FWSIGN_E_OTP_READ			= -4112,

	/* heap address overlaps with FW address space */
	BCM_FWSIGN_E_HEAP_OVR_FW		= -4113,

	/* heap address overlaps with bootloader data/bss region */
	BCM_FWSIGN_E_HEAP_OVR_BSS		= -4114,

	/* heap address overlaps with bootloader stack region */
	BCM_FWSIGN_E_HEAP_OVR_STACK		= -4115,

	/* firmware encryption header tlv is missing */
	BCM_FWSIGN_E_NO_FWENC_HDR		= -4116,

	/* firmware encryption algo not supported */
	BCM_FWSIGN_E_FWENC_ALGO_NOTSUP		= -4117,

	/* firmware encryption tag tlv is missing */
	BCM_FWSIGN_E_NO_FW_TAG			= -4118,

	/* firmware encryption tag tlv is not valid */
	BCM_FWSIGN_E_FW_TAG_INVALID_TLV		= -4119,

	/* firmware encryption tag verification fail */
	BCM_FWSIGN_E_FW_TAG_MISMATCH		= -4120,

	/* signature package is invalid */
	BCM_FWSIGN_E_PACKAGE_INVALID		= -4121,

	/* last error */
	BCM_FWSIGN_E_LAST			= -5119
};
typedef int32 bcm_fwsign_status_t;

/* PMK manager block. Event codes from -5120 to -6143 */
/* PSK hashing event codes */
enum {
	WL_PMK_E_PSK_HASH_FAILED		= -5120,
	WL_PMK_E_PSK_HASH_DONE			= -5121,
	WL_PMK_E_PSK_HASH_RUNNING		= -5122,
	WL_PMK_E_PSK_INVALID			= -5123,
	WL_PMK_E_PSK_NOMEM			= -5124
};

/*
 * SOE (Security Offload Engine) status codes.
 * SOE status codes are reserved from -6144 to -7167 (1K)
 */
enum {
	/* Invalid operational context */
	WL_SOE_E_BAD_OP_CONTEXT			= -6144,

	/* Invalid operational type */
	WL_SOE_E_BAD_OP_TYPE			= -6145,

	/* Failure to get NAF3 encoded scalar */
	WL_SOE_E_BN_NAF3_GET_ERROR		= -6146,

	/* Failure to get NAF3 params */
	WL_SOE_E_NAF3_PARAMS_GET_ERROR		= -6147
};

/* BCM crypto ASN.1 status codes. */
/* Reserved range is from -7168 to -8291 */
enum {
	/* tag mismatch */
	BCM_CRYPTO_E_ASN1_TAG_MISMATCH		= -7168,

	/* OID mismatch */
	BCM_CRYPTO_E_ASN1_OID_MISMATCH		= -7169,

	/* Bad key type */
	BCM_CRYPTO_E_ASN1_BAD_KEY_TYPE		= -7170,

	/* value length is invalid */
	BCM_CRYPTO_E_ASN1_INVALID_LENGTH	= -7171,

	/* Invalid public key length */
	BCM_CRYPTO_E_ASN1_INVALID_PKLEN		= -7172,

	/* Unsupported elliptic curve group */
	BCM_CRYPTO_E_ASN1_UNSUPPORTED_ECG	= -7173
};

#endif	/* BCMUTILS_ERR_CODES */

#endif	/* _bcmerror_h_ */
