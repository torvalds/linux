/*
 * WPA Supplicant - Windows/NDIS driver interface
 * Copyright (c) 2004-2007, Jouni Malinen <j@w1.fi>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License version 2 as
 * published by the Free Software Foundation.
 *
 * Alternatively, this software may be distributed under the terms of BSD
 * license.
 *
 * See README and COPYING for more details.
 */

#ifdef __CYGWIN__
/* Avoid some header file conflicts by not including standard headers for
 * cygwin builds when Packet32.h is included. */
#include "build_config.h"
int close(int fd);
#else /* __CYGWIN__ */
#include "includes.h"
#endif /* __CYGWIN__ */
#ifdef CONFIG_USE_NDISUIO
#include <winsock2.h>
#else /* CONFIG_USE_NDISUIO */
#include <Packet32.h>
#endif /* CONFIG_USE_NDISUIO */
#ifdef __MINGW32_VERSION
#include <ddk/ntddndis.h>
#else /* __MINGW32_VERSION */
#include <ntddndis.h>
#endif /* __MINGW32_VERSION */

#ifdef _WIN32_WCE
#include <winioctl.h>
#include <nuiouser.h>
#include <devload.h>
#endif /* _WIN32_WCE */

#include "common.h"
#include "driver.h"
#include "eloop.h"
#include "common/ieee802_11_defs.h"
#include "driver_ndis.h"

int wpa_driver_register_event_cb(struct wpa_driver_ndis_data *drv);
#ifdef CONFIG_NDIS_EVENTS_INTEGRATED
void wpa_driver_ndis_event_pipe_cb(void *eloop_data, void *user_data);
#endif /* CONFIG_NDIS_EVENTS_INTEGRATED */

static void wpa_driver_ndis_deinit(void *priv);
static void wpa_driver_ndis_poll(void *drv);
static void wpa_driver_ndis_poll_timeout(void *eloop_ctx, void *timeout_ctx);
static int wpa_driver_ndis_adapter_init(struct wpa_driver_ndis_data *drv);
static int wpa_driver_ndis_adapter_open(struct wpa_driver_ndis_data *drv);
static void wpa_driver_ndis_adapter_close(struct wpa_driver_ndis_data *drv);


static const u8 pae_group_addr[ETH_ALEN] =
{ 0x01, 0x80, 0xc2, 0x00, 0x00, 0x03 };


/* FIX: to be removed once this can be compiled with the complete NDIS
 * header files */
#ifndef OID_802_11_BSSID
#define OID_802_11_BSSID 			0x0d010101
#define OID_802_11_SSID 			0x0d010102
#define OID_802_11_INFRASTRUCTURE_MODE		0x0d010108
#define OID_802_11_ADD_WEP			0x0D010113
#define OID_802_11_REMOVE_WEP			0x0D010114
#define OID_802_11_DISASSOCIATE			0x0D010115
#define OID_802_11_BSSID_LIST 			0x0d010217
#define OID_802_11_AUTHENTICATION_MODE		0x0d010118
#define OID_802_11_PRIVACY_FILTER		0x0d010119
#define OID_802_11_BSSID_LIST_SCAN 		0x0d01011A
#define OID_802_11_WEP_STATUS	 		0x0d01011B
#define OID_802_11_ENCRYPTION_STATUS OID_802_11_WEP_STATUS
#define OID_802_11_ADD_KEY 			0x0d01011D
#define OID_802_11_REMOVE_KEY 			0x0d01011E
#define OID_802_11_ASSOCIATION_INFORMATION	0x0d01011F
#define OID_802_11_TEST 			0x0d010120
#define OID_802_11_CAPABILITY 			0x0d010122
#define OID_802_11_PMKID 			0x0d010123

#define NDIS_802_11_LENGTH_SSID 32
#define NDIS_802_11_LENGTH_RATES 8
#define NDIS_802_11_LENGTH_RATES_EX 16

typedef UCHAR NDIS_802_11_MAC_ADDRESS[6];

typedef struct NDIS_802_11_SSID {
	ULONG SsidLength;
	UCHAR Ssid[NDIS_802_11_LENGTH_SSID];
} NDIS_802_11_SSID;

typedef LONG NDIS_802_11_RSSI;

typedef enum NDIS_802_11_NETWORK_TYPE {
	Ndis802_11FH,
	Ndis802_11DS,
	Ndis802_11OFDM5,
	Ndis802_11OFDM24,
	Ndis802_11NetworkTypeMax
} NDIS_802_11_NETWORK_TYPE;

typedef struct NDIS_802_11_CONFIGURATION_FH {
	ULONG Length;
	ULONG HopPattern;
	ULONG HopSet;
	ULONG DwellTime;
} NDIS_802_11_CONFIGURATION_FH;

typedef struct NDIS_802_11_CONFIGURATION {
	ULONG Length;
	ULONG BeaconPeriod;
	ULONG ATIMWindow;
	ULONG DSConfig;
	NDIS_802_11_CONFIGURATION_FH FHConfig;
} NDIS_802_11_CONFIGURATION;

typedef enum NDIS_802_11_NETWORK_INFRASTRUCTURE {
	Ndis802_11IBSS,
	Ndis802_11Infrastructure,
	Ndis802_11AutoUnknown,
	Ndis802_11InfrastructureMax
} NDIS_802_11_NETWORK_INFRASTRUCTURE;

typedef enum NDIS_802_11_AUTHENTICATION_MODE {
	Ndis802_11AuthModeOpen,
	Ndis802_11AuthModeShared,
	Ndis802_11AuthModeAutoSwitch,
	Ndis802_11AuthModeWPA,
	Ndis802_11AuthModeWPAPSK,
	Ndis802_11AuthModeWPANone,
	Ndis802_11AuthModeWPA2,
	Ndis802_11AuthModeWPA2PSK,
	Ndis802_11AuthModeMax
} NDIS_802_11_AUTHENTICATION_MODE;

typedef enum NDIS_802_11_WEP_STATUS {
	Ndis802_11WEPEnabled,
	Ndis802_11Encryption1Enabled = Ndis802_11WEPEnabled,
	Ndis802_11WEPDisabled,
	Ndis802_11EncryptionDisabled = Ndis802_11WEPDisabled,
	Ndis802_11WEPKeyAbsent,
	Ndis802_11Encryption1KeyAbsent = Ndis802_11WEPKeyAbsent,
	Ndis802_11WEPNotSupported,
	Ndis802_11EncryptionNotSupported = Ndis802_11WEPNotSupported,
	Ndis802_11Encryption2Enabled,
	Ndis802_11Encryption2KeyAbsent,
	Ndis802_11Encryption3Enabled,
	Ndis802_11Encryption3KeyAbsent
} NDIS_802_11_WEP_STATUS, NDIS_802_11_ENCRYPTION_STATUS;

typedef enum NDIS_802_11_PRIVACY_FILTER {
	Ndis802_11PrivFilterAcceptAll,
	Ndis802_11PrivFilter8021xWEP
} NDIS_802_11_PRIVACY_FILTER;

typedef UCHAR NDIS_802_11_RATES[NDIS_802_11_LENGTH_RATES];
typedef UCHAR NDIS_802_11_RATES_EX[NDIS_802_11_LENGTH_RATES_EX];

typedef struct NDIS_WLAN_BSSID_EX {
	ULONG Length;
	NDIS_802_11_MAC_ADDRESS MacAddress; /* BSSID */
	UCHAR Reserved[2];
	NDIS_802_11_SSID Ssid;
	ULONG Privacy;
	NDIS_802_11_RSSI Rssi;
	NDIS_802_11_NETWORK_TYPE NetworkTypeInUse;
	NDIS_802_11_CONFIGURATION Configuration;
	NDIS_802_11_NETWORK_INFRASTRUCTURE InfrastructureMode;
	NDIS_802_11_RATES_EX SupportedRates;
	ULONG IELength;
	UCHAR IEs[1];
} NDIS_WLAN_BSSID_EX;

typedef struct NDIS_802_11_BSSID_LIST_EX {
	ULONG NumberOfItems;
	NDIS_WLAN_BSSID_EX Bssid[1];
} NDIS_802_11_BSSID_LIST_EX;

typedef struct NDIS_802_11_FIXED_IEs {
	UCHAR Timestamp[8];
	USHORT BeaconInterval;
	USHORT Capabilities;
} NDIS_802_11_FIXED_IEs;

typedef struct NDIS_802_11_WEP {
	ULONG Length;
	ULONG KeyIndex;
	ULONG KeyLength;
	UCHAR KeyMaterial[1];
} NDIS_802_11_WEP;

typedef ULONG NDIS_802_11_KEY_INDEX;
typedef ULONGLONG NDIS_802_11_KEY_RSC;

typedef struct NDIS_802_11_KEY {
	ULONG Length;
	ULONG KeyIndex;
	ULONG KeyLength;
	NDIS_802_11_MAC_ADDRESS BSSID;
	NDIS_802_11_KEY_RSC KeyRSC;
	UCHAR KeyMaterial[1];
} NDIS_802_11_KEY;

typedef struct NDIS_802_11_REMOVE_KEY {
	ULONG Length;
	ULONG KeyIndex;
	NDIS_802_11_MAC_ADDRESS BSSID;
} NDIS_802_11_REMOVE_KEY;

typedef struct NDIS_802_11_AI_REQFI {
	USHORT Capabilities;
	USHORT ListenInterval;
	NDIS_802_11_MAC_ADDRESS CurrentAPAddress;
} NDIS_802_11_AI_REQFI;

typedef struct NDIS_802_11_AI_RESFI {
	USHORT Capabilities;
	USHORT StatusCode;
	USHORT AssociationId;
} NDIS_802_11_AI_RESFI;

typedef struct NDIS_802_11_ASSOCIATION_INFORMATION {
	ULONG Length;
	USHORT AvailableRequestFixedIEs;
	NDIS_802_11_AI_REQFI RequestFixedIEs;
	ULONG RequestIELength;
	ULONG OffsetRequestIEs;
	USHORT AvailableResponseFixedIEs;
	NDIS_802_11_AI_RESFI ResponseFixedIEs;
	ULONG ResponseIELength;
	ULONG OffsetResponseIEs;
} NDIS_802_11_ASSOCIATION_INFORMATION;

typedef struct NDIS_802_11_AUTHENTICATION_ENCRYPTION {
	NDIS_802_11_AUTHENTICATION_MODE AuthModeSupported;
	NDIS_802_11_ENCRYPTION_STATUS EncryptStatusSupported;
} NDIS_802_11_AUTHENTICATION_ENCRYPTION;

typedef struct NDIS_802_11_CAPABILITY {
	ULONG Length;
	ULONG Version;
	ULONG NoOfPMKIDs;
	ULONG NoOfAuthEncryptPairsSupported;
	NDIS_802_11_AUTHENTICATION_ENCRYPTION
		AuthenticationEncryptionSupported[1];
} NDIS_802_11_CAPABILITY;

typedef UCHAR NDIS_802_11_PMKID_VALUE[16];

typedef struct BSSID_INFO {
	NDIS_802_11_MAC_ADDRESS BSSID;
	NDIS_802_11_PMKID_VALUE PMKID;
} BSSID_INFO;

typedef struct NDIS_802_11_PMKID {
	ULONG Length;
	ULONG BSSIDInfoCount;
	BSSID_INFO BSSIDInfo[1];
} NDIS_802_11_PMKID;

typedef enum NDIS_802_11_STATUS_TYPE {
	Ndis802_11StatusType_Authentication,
	Ndis802_11StatusType_PMKID_CandidateList = 2,
	Ndis802_11StatusTypeMax
} NDIS_802_11_STATUS_TYPE;

typedef struct NDIS_802_11_STATUS_INDICATION {
	NDIS_802_11_STATUS_TYPE StatusType;
} NDIS_802_11_STATUS_INDICATION;

typedef struct PMKID_CANDIDATE {
	NDIS_802_11_MAC_ADDRESS BSSID;
	ULONG Flags;
} PMKID_CANDIDATE;

#define NDIS_802_11_PMKID_CANDIDATE_PREAUTH_ENABLED 0x01

typedef struct NDIS_802_11_PMKID_CANDIDATE_LIST {
	ULONG Version;
	ULONG NumCandidates;
	PMKID_CANDIDATE CandidateList[1];
} NDIS_802_11_PMKID_CANDIDATE_LIST;

typedef struct NDIS_802_11_AUTHENTICATION_REQUEST {
	ULONG Length;
	NDIS_802_11_MAC_ADDRESS Bssid;
	ULONG Flags;
} NDIS_802_11_AUTHENTICATION_REQUEST;

#define NDIS_802_11_AUTH_REQUEST_REAUTH			0x01
#define NDIS_802_11_AUTH_REQUEST_KEYUPDATE		0x02
#define NDIS_802_11_AUTH_REQUEST_PAIRWISE_ERROR		0x06
#define NDIS_802_11_AUTH_REQUEST_GROUP_ERROR		0x0E

#endif /* OID_802_11_BSSID */


#ifndef OID_802_11_PMKID
/* Platform SDK for XP did not include WPA2, so add needed definitions */

#define OID_802_11_CAPABILITY 			0x0d010122
#define OID_802_11_PMKID 			0x0d010123

#define Ndis802_11AuthModeWPA2 6
#define Ndis802_11AuthModeWPA2PSK 7

#define Ndis802_11StatusType_PMKID_CandidateList 2

typedef struct NDIS_802_11_AUTHENTICATION_ENCRYPTION {
	NDIS_802_11_AUTHENTICATION_MODE AuthModeSupported;
	NDIS_802_11_ENCRYPTION_STATUS EncryptStatusSupported;
} NDIS_802_11_AUTHENTICATION_ENCRYPTION;

typedef struct NDIS_802_11_CAPABILITY {
	ULONG Length;
	ULONG Version;
	ULONG NoOfPMKIDs;
	ULONG NoOfAuthEncryptPairsSupported;
	NDIS_802_11_AUTHENTICATION_ENCRYPTION
		AuthenticationEncryptionSupported[1];
} NDIS_802_11_CAPABILITY;

typedef UCHAR NDIS_802_11_PMKID_VALUE[16];

typedef struct BSSID_INFO {
	NDIS_802_11_MAC_ADDRESS BSSID;
	NDIS_802_11_PMKID_VALUE PMKID;
} BSSID_INFO;

typedef struct NDIS_802_11_PMKID {
	ULONG Length;
	ULONG BSSIDInfoCount;
	BSSID_INFO BSSIDInfo[1];
} NDIS_802_11_PMKID;

typedef struct PMKID_CANDIDATE {
	NDIS_802_11_MAC_ADDRESS BSSID;
	ULONG Flags;
} PMKID_CANDIDATE;

#define NDIS_802_11_PMKID_CANDIDATE_PREAUTH_ENABLED 0x01

typedef struct NDIS_802_11_PMKID_CANDIDATE_LIST {
	ULONG Version;
	ULONG NumCandidates;
	PMKID_CANDIDATE CandidateList[1];
} NDIS_802_11_PMKID_CANDIDATE_LIST;

#endif /* OID_802_11_CAPABILITY */


#ifndef OID_DOT11_CURRENT_OPERATION_MODE
/* Native 802.11 OIDs */
#define OID_DOT11_NDIS_START 0x0D010300
#define OID_DOT11_CURRENT_OPERATION_MODE (OID_DOT11_NDIS_START + 8)
#define OID_DOT11_SCAN_REQUEST (OID_DOT11_NDIS_START + 11)

typedef enum _DOT11_BSS_TYPE {
	dot11_BSS_type_infrastructure = 1,
	dot11_BSS_type_independent = 2,
	dot11_BSS_type_any = 3
} DOT11_BSS_TYPE, * PDOT11_BSS_TYPE;

typedef UCHAR DOT11_MAC_ADDRESS[6];
typedef DOT11_MAC_ADDRESS * PDOT11_MAC_ADDRESS;

typedef enum _DOT11_SCAN_TYPE {
	dot11_scan_type_active = 1,
	dot11_scan_type_passive = 2,
	dot11_scan_type_auto = 3,
	dot11_scan_type_forced = 0x80000000
} DOT11_SCAN_TYPE, * PDOT11_SCAN_TYPE;

typedef struct _DOT11_SCAN_REQUEST_V2 {
	DOT11_BSS_TYPE dot11BSSType;
	DOT11_MAC_ADDRESS dot11BSSID;
	DOT11_SCAN_TYPE dot11ScanType;
	BOOLEAN bRestrictedScan;
	ULONG udot11SSIDsOffset;
	ULONG uNumOfdot11SSIDs;
	BOOLEAN bUseRequestIE;
	ULONG uRequestIDsOffset;
	ULONG uNumOfRequestIDs;
	ULONG uPhyTypeInfosOffset;
	ULONG uNumOfPhyTypeInfos;
	ULONG uIEsOffset;
	ULONG uIEsLength;
	UCHAR ucBuffer[1];
} DOT11_SCAN_REQUEST_V2, * PDOT11_SCAN_REQUEST_V2;

#endif /* OID_DOT11_CURRENT_OPERATION_MODE */

#ifdef CONFIG_USE_NDISUIO
#ifndef _WIN32_WCE
#ifdef __MINGW32_VERSION
typedef ULONG NDIS_OID;
#endif /* __MINGW32_VERSION */
/* from nuiouser.h */
#define FSCTL_NDISUIO_BASE      FILE_DEVICE_NETWORK

#define _NDISUIO_CTL_CODE(_Function, _Method, _Access) \
	CTL_CODE(FSCTL_NDISUIO_BASE, _Function, _Method, _Access)

#define IOCTL_NDISUIO_OPEN_DEVICE \
	_NDISUIO_CTL_CODE(0x200, METHOD_BUFFERED, \
			  FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_NDISUIO_QUERY_OID_VALUE \
	_NDISUIO_CTL_CODE(0x201, METHOD_BUFFERED, \
			  FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_NDISUIO_SET_OID_VALUE \
	_NDISUIO_CTL_CODE(0x205, METHOD_BUFFERED, \
			  FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_NDISUIO_SET_ETHER_TYPE \
	_NDISUIO_CTL_CODE(0x202, METHOD_BUFFERED, \
			  FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_NDISUIO_QUERY_BINDING \
	_NDISUIO_CTL_CODE(0x203, METHOD_BUFFERED, \
			  FILE_READ_ACCESS | FILE_WRITE_ACCESS)

#define IOCTL_NDISUIO_BIND_WAIT \
	_NDISUIO_CTL_CODE(0x204, METHOD_BUFFERED, \
			  FILE_READ_ACCESS | FILE_WRITE_ACCESS)

typedef struct _NDISUIO_QUERY_OID
{
    NDIS_OID Oid;
    UCHAR Data[sizeof(ULONG)];
} NDISUIO_QUERY_OID, *PNDISUIO_QUERY_OID;

typedef struct _NDISUIO_SET_OID
{
    NDIS_OID Oid;
    UCHAR Data[sizeof(ULONG)];
} NDISUIO_SET_OID, *PNDISUIO_SET_OID;

typedef struct _NDISUIO_QUERY_BINDING
{
	ULONG BindingIndex;
	ULONG DeviceNameOffset;
	ULONG DeviceNameLength;
	ULONG DeviceDescrOffset;
	ULONG DeviceDescrLength;
} NDISUIO_QUERY_BINDING, *PNDISUIO_QUERY_BINDING;
#endif /* _WIN32_WCE */
#endif /* CONFIG_USE_NDISUIO */


static int ndis_get_oid(struct wpa_driver_ndis_data *drv, unsigned int oid,
			char *data, size_t len)
{
#ifdef CONFIG_USE_NDISUIO
	NDISUIO_QUERY_OID *o;
	size_t buflen = sizeof(*o) + len;
	DWORD written;
	int ret;
	size_t hdrlen;

	o = os_zalloc(buflen);
	if (o == NULL)
		return -1;
	o->Oid = oid;
#ifdef _WIN32_WCE
	o->ptcDeviceName = drv->adapter_name;
#endif /* _WIN32_WCE */
	if (!DeviceIoControl(drv->ndisuio, IOCTL_NDISUIO_QUERY_OID_VALUE,
			     o, sizeof(NDISUIO_QUERY_OID), o, buflen, &written,
			     NULL)) {
		wpa_printf(MSG_DEBUG, "NDIS: IOCTL_NDISUIO_QUERY_OID_VALUE "
			   "failed (oid=%08x): %d", oid, (int) GetLastError());
		os_free(o);
		return -1;
	}
	hdrlen = sizeof(NDISUIO_QUERY_OID) - sizeof(o->Data);
	if (written < hdrlen) {
		wpa_printf(MSG_DEBUG, "NDIS: query oid=%08x written (%d); "
			   "too short", oid, (unsigned int) written);
		os_free(o);
		return -1;
	}
	written -= hdrlen;
	if (written > len) {
		wpa_printf(MSG_DEBUG, "NDIS: query oid=%08x written (%d) > "
			   "len (%d)",oid, (unsigned int) written, len);
		os_free(o);
		return -1;
	}
	os_memcpy(data, o->Data, written);
	ret = written;
	os_free(o);
	return ret;
#else /* CONFIG_USE_NDISUIO */
	char *buf;
	PACKET_OID_DATA *o;
	int ret;

	buf = os_zalloc(sizeof(*o) + len);
	if (buf == NULL)
		return -1;
	o = (PACKET_OID_DATA *) buf;
	o->Oid = oid;
	o->Length = len;

	if (!PacketRequest(drv->adapter, FALSE, o)) {
		wpa_printf(MSG_DEBUG, "%s: oid=0x%x len (%d) failed",
			   __func__, oid, len);
		os_free(buf);
		return -1;
	}
	if (o->Length > len) {
		wpa_printf(MSG_DEBUG, "%s: oid=0x%x Length (%d) > len (%d)",
			   __func__, oid, (unsigned int) o->Length, len);
		os_free(buf);
		return -1;
	}
	os_memcpy(data, o->Data, o->Length);
	ret = o->Length;
	os_free(buf);
	return ret;
#endif /* CONFIG_USE_NDISUIO */
}


static int ndis_set_oid(struct wpa_driver_ndis_data *drv, unsigned int oid,
			const char *data, size_t len)
{
#ifdef CONFIG_USE_NDISUIO
	NDISUIO_SET_OID *o;
	size_t buflen, reallen;
	DWORD written;
	char txt[50];

	os_snprintf(txt, sizeof(txt), "NDIS: Set OID %08x", oid);
	wpa_hexdump_key(MSG_MSGDUMP, txt, (const u8 *) data, len);

	buflen = sizeof(*o) + len;
	reallen = buflen - sizeof(o->Data);
	o = os_zalloc(buflen);
	if (o == NULL)
		return -1;
	o->Oid = oid;
#ifdef _WIN32_WCE
	o->ptcDeviceName = drv->adapter_name;
#endif /* _WIN32_WCE */
	if (data)
		os_memcpy(o->Data, data, len);
	if (!DeviceIoControl(drv->ndisuio, IOCTL_NDISUIO_SET_OID_VALUE,
			     o, reallen, NULL, 0, &written, NULL)) {
		wpa_printf(MSG_DEBUG, "NDIS: IOCTL_NDISUIO_SET_OID_VALUE "
			   "(oid=%08x) failed: %d", oid, (int) GetLastError());
		os_free(o);
		return -1;
	}
	os_free(o);
	return 0;
#else /* CONFIG_USE_NDISUIO */
	char *buf;
	PACKET_OID_DATA *o;
	char txt[50];

	os_snprintf(txt, sizeof(txt), "NDIS: Set OID %08x", oid);
	wpa_hexdump_key(MSG_MSGDUMP, txt, (const u8 *) data, len);

	buf = os_zalloc(sizeof(*o) + len);
	if (buf == NULL)
		return -1;
	o = (PACKET_OID_DATA *) buf;
	o->Oid = oid;
	o->Length = len;
	if (data)
		os_memcpy(o->Data, data, len);

	if (!PacketRequest(drv->adapter, TRUE, o)) {
		wpa_printf(MSG_DEBUG, "%s: oid=0x%x len (%d) failed",
			   __func__, oid, len);
		os_free(buf);
		return -1;
	}
	os_free(buf);
	return 0;
#endif /* CONFIG_USE_NDISUIO */
}


static int ndis_set_auth_mode(struct wpa_driver_ndis_data *drv, int mode)
{
	u32 auth_mode = mode;
	if (ndis_set_oid(drv, OID_802_11_AUTHENTICATION_MODE,
			 (char *) &auth_mode, sizeof(auth_mode)) < 0) {
		wpa_printf(MSG_DEBUG, "NDIS: Failed to set "
			   "OID_802_11_AUTHENTICATION_MODE (%d)",
			   (int) auth_mode);
		return -1;
	}
	return 0;
}


static int ndis_get_auth_mode(struct wpa_driver_ndis_data *drv)
{
	u32 auth_mode;
	int res;
	res = ndis_get_oid(drv, OID_802_11_AUTHENTICATION_MODE,
			   (char *) &auth_mode, sizeof(auth_mode));
	if (res != sizeof(auth_mode)) {
		wpa_printf(MSG_DEBUG, "NDIS: Failed to get "
			   "OID_802_11_AUTHENTICATION_MODE");
		return -1;
	}
	return auth_mode;
}


static int ndis_set_encr_status(struct wpa_driver_ndis_data *drv, int encr)
{
	u32 encr_status = encr;
	if (ndis_set_oid(drv, OID_802_11_ENCRYPTION_STATUS,
			 (char *) &encr_status, sizeof(encr_status)) < 0) {
		wpa_printf(MSG_DEBUG, "NDIS: Failed to set "
			   "OID_802_11_ENCRYPTION_STATUS (%d)", encr);
		return -1;
	}
	return 0;
}


static int ndis_get_encr_status(struct wpa_driver_ndis_data *drv)
{
	u32 encr;
	int res;
	res = ndis_get_oid(drv, OID_802_11_ENCRYPTION_STATUS,
			   (char *) &encr, sizeof(encr));
	if (res != sizeof(encr)) {
		wpa_printf(MSG_DEBUG, "NDIS: Failed to get "
			   "OID_802_11_ENCRYPTION_STATUS");
		return -1;
	}
	return encr;
}


static int wpa_driver_ndis_get_bssid(void *priv, u8 *bssid)
{
	struct wpa_driver_ndis_data *drv = priv;

	if (drv->wired) {
		/*
		 * Report PAE group address as the "BSSID" for wired
		 * connection.
		 */
		os_memcpy(bssid, pae_group_addr, ETH_ALEN);
		return 0;
	}

	return ndis_get_oid(drv, OID_802_11_BSSID, (char *) bssid, ETH_ALEN) <
		0 ? -1 : 0;
}


static int wpa_driver_ndis_get_ssid(void *priv, u8 *ssid)
{
	struct wpa_driver_ndis_data *drv = priv;
	NDIS_802_11_SSID buf;
	int res;

	res = ndis_get_oid(drv, OID_802_11_SSID, (char *) &buf, sizeof(buf));
	if (res < 4) {
		wpa_printf(MSG_DEBUG, "NDIS: Failed to get SSID");
		if (drv->wired) {
			wpa_printf(MSG_DEBUG, "NDIS: Allow get_ssid failure "
				   "with a wired interface");
			return 0;
		}
		return -1;
	}
	os_memcpy(ssid, buf.Ssid, buf.SsidLength);
	return buf.SsidLength;
}


static int wpa_driver_ndis_set_ssid(struct wpa_driver_ndis_data *drv,
				    const u8 *ssid, size_t ssid_len)
{
	NDIS_802_11_SSID buf;

	os_memset(&buf, 0, sizeof(buf));
	buf.SsidLength = ssid_len;
	os_memcpy(buf.Ssid, ssid, ssid_len);
	/*
	 * Make sure radio is marked enabled here so that scan request will not
	 * force SSID to be changed to a random one in order to enable radio at
	 * that point.
	 */
	drv->radio_enabled = 1;
	return ndis_set_oid(drv, OID_802_11_SSID, (char *) &buf, sizeof(buf));
}


/* Disconnect using OID_802_11_DISASSOCIATE. This will also turn the radio off.
 */
static int wpa_driver_ndis_radio_off(struct wpa_driver_ndis_data *drv)
{
	drv->radio_enabled = 0;
	return ndis_set_oid(drv, OID_802_11_DISASSOCIATE, "    ", 4);
}


/* Disconnect by setting SSID to random (i.e., likely not used). */
static int wpa_driver_ndis_disconnect(struct wpa_driver_ndis_data *drv)
{
	char ssid[32];
	int i;
	for (i = 0; i < 32; i++)
		ssid[i] = rand() & 0xff;
	return wpa_driver_ndis_set_ssid(drv, (u8 *) ssid, 32);
}


static int wpa_driver_ndis_deauthenticate(void *priv, const u8 *addr,
					  int reason_code)
{
	struct wpa_driver_ndis_data *drv = priv;
	return wpa_driver_ndis_disconnect(drv);
}


static int wpa_driver_ndis_disassociate(void *priv, const u8 *addr,
					int reason_code)
{
	struct wpa_driver_ndis_data *drv = priv;
	return wpa_driver_ndis_disconnect(drv);
}


static void wpa_driver_ndis_scan_timeout(void *eloop_ctx, void *timeout_ctx)
{
	wpa_printf(MSG_DEBUG, "Scan timeout - try to get results");
	wpa_supplicant_event(timeout_ctx, EVENT_SCAN_RESULTS, NULL);
}


static int wpa_driver_ndis_scan_native80211(
	struct wpa_driver_ndis_data *drv,
	struct wpa_driver_scan_params *params)
{
	DOT11_SCAN_REQUEST_V2 req;
	int res;

	os_memset(&req, 0, sizeof(req));
	req.dot11BSSType = dot11_BSS_type_any;
	os_memset(req.dot11BSSID, 0xff, ETH_ALEN);
	req.dot11ScanType = dot11_scan_type_auto;
	res = ndis_set_oid(drv, OID_DOT11_SCAN_REQUEST, (char *) &req,
			   sizeof(req));
	eloop_cancel_timeout(wpa_driver_ndis_scan_timeout, drv, drv->ctx);
	eloop_register_timeout(7, 0, wpa_driver_ndis_scan_timeout, drv,
			       drv->ctx);
	return res;
}


static int wpa_driver_ndis_scan(void *priv,
				struct wpa_driver_scan_params *params)
{
	struct wpa_driver_ndis_data *drv = priv;
	int res;

	if (drv->native80211)
		return wpa_driver_ndis_scan_native80211(drv, params);

	if (!drv->radio_enabled) {
		wpa_printf(MSG_DEBUG, "NDIS: turning radio on before the first"
			   " scan");
		if (wpa_driver_ndis_disconnect(drv) < 0) {
			wpa_printf(MSG_DEBUG, "NDIS: failed to enable radio");
		}
		drv->radio_enabled = 1;
	}

	res = ndis_set_oid(drv, OID_802_11_BSSID_LIST_SCAN, "    ", 4);
	eloop_cancel_timeout(wpa_driver_ndis_scan_timeout, drv, drv->ctx);
	eloop_register_timeout(7, 0, wpa_driver_ndis_scan_timeout, drv,
			       drv->ctx);
	return res;
}


static const u8 * wpa_scan_get_ie(const struct wpa_scan_res *res, u8 ie)
{
	const u8 *end, *pos;

	pos = (const u8 *) (res + 1);
	end = pos + res->ie_len;

	while (pos + 1 < end) {
		if (pos + 2 + pos[1] > end)
			break;
		if (pos[0] == ie)
			return pos;
		pos += 2 + pos[1];
	}

	return NULL;
}


static struct wpa_scan_res * wpa_driver_ndis_add_scan_ssid(
	struct wpa_scan_res *r, NDIS_802_11_SSID *ssid)
{
	struct wpa_scan_res *nr;
	u8 *pos;

	if (wpa_scan_get_ie(r, WLAN_EID_SSID))
		return r; /* SSID IE already present */

	if (ssid->SsidLength == 0 || ssid->SsidLength > 32)
		return r; /* No valid SSID inside scan data */

	nr = os_realloc(r, sizeof(*r) + r->ie_len + 2 + ssid->SsidLength);
	if (nr == NULL)
		return r;

	pos = ((u8 *) (nr + 1)) + nr->ie_len;
	*pos++ = WLAN_EID_SSID;
	*pos++ = ssid->SsidLength;
	os_memcpy(pos, ssid->Ssid, ssid->SsidLength);
	nr->ie_len += 2 + ssid->SsidLength;

	return nr;
}


static struct wpa_scan_results * wpa_driver_ndis_get_scan_results(void *priv)
{
	struct wpa_driver_ndis_data *drv = priv;
	NDIS_802_11_BSSID_LIST_EX *b;
	size_t blen, count, i;
	int len;
	char *pos;
	struct wpa_scan_results *results;
	struct wpa_scan_res *r;

	blen = 65535;
	b = os_zalloc(blen);
	if (b == NULL)
		return NULL;
	len = ndis_get_oid(drv, OID_802_11_BSSID_LIST, (char *) b, blen);
	if (len < 0) {
		wpa_printf(MSG_DEBUG, "NDIS: failed to get scan results");
		os_free(b);
		return NULL;
	}
	count = b->NumberOfItems;

	results = os_zalloc(sizeof(*results));
	if (results == NULL) {
		os_free(b);
		return NULL;
	}
	results->res = os_zalloc(count * sizeof(struct wpa_scan_res *));
	if (results->res == NULL) {
		os_free(results);
		os_free(b);
		return NULL;
	}

	pos = (char *) &b->Bssid[0];
	for (i = 0; i < count; i++) {
		NDIS_WLAN_BSSID_EX *bss = (NDIS_WLAN_BSSID_EX *) pos;
		NDIS_802_11_FIXED_IEs *fixed;

		if (bss->IELength < sizeof(NDIS_802_11_FIXED_IEs)) {
			wpa_printf(MSG_DEBUG, "NDIS: too small IELength=%d",
				   (int) bss->IELength);
			break;
		}
		if (((char *) bss->IEs) + bss->IELength  > (char *) b + blen) {
			/*
			 * Some NDIS drivers have been reported to include an
			 * entry with an invalid IELength in scan results and
			 * this has crashed wpa_supplicant, so validate the
			 * returned value before using it.
			 */
			wpa_printf(MSG_DEBUG, "NDIS: skipped invalid scan "
				   "result IE (BSSID=" MACSTR ") IELength=%d",
				   MAC2STR(bss->MacAddress),
				   (int) bss->IELength);
			break;
		}

		r = os_zalloc(sizeof(*r) + bss->IELength -
			      sizeof(NDIS_802_11_FIXED_IEs));
		if (r == NULL)
			break;

		os_memcpy(r->bssid, bss->MacAddress, ETH_ALEN);
		r->level = (int) bss->Rssi;
		r->freq = bss->Configuration.DSConfig / 1000;
		fixed = (NDIS_802_11_FIXED_IEs *) bss->IEs;
		r->beacon_int = WPA_GET_LE16((u8 *) &fixed->BeaconInterval);
		r->caps = WPA_GET_LE16((u8 *) &fixed->Capabilities);
		r->tsf = WPA_GET_LE64(fixed->Timestamp);
		os_memcpy(r + 1, bss->IEs + sizeof(NDIS_802_11_FIXED_IEs),
			  bss->IELength - sizeof(NDIS_802_11_FIXED_IEs));
		r->ie_len = bss->IELength - sizeof(NDIS_802_11_FIXED_IEs);
		r = wpa_driver_ndis_add_scan_ssid(r, &bss->Ssid);

		results->res[results->num++] = r;

		pos += bss->Length;
		if (pos > (char *) b + blen)
			break;
	}

	os_free(b);

	return results;
}


static int wpa_driver_ndis_remove_key(struct wpa_driver_ndis_data *drv,
				      int key_idx, const u8 *addr,
				      const u8 *bssid, int pairwise)
{
	NDIS_802_11_REMOVE_KEY rkey;
	NDIS_802_11_KEY_INDEX index;
	int res, res2;

	os_memset(&rkey, 0, sizeof(rkey));

	rkey.Length = sizeof(rkey);
	rkey.KeyIndex = key_idx;
	if (pairwise)
		rkey.KeyIndex |= 1 << 30;
	os_memcpy(rkey.BSSID, bssid, ETH_ALEN);

	res = ndis_set_oid(drv, OID_802_11_REMOVE_KEY, (char *) &rkey,
			   sizeof(rkey));
	if (!pairwise) {
		index = key_idx;
		res2 = ndis_set_oid(drv, OID_802_11_REMOVE_WEP,
				    (char *) &index, sizeof(index));
	} else
		res2 = 0;

	if (res < 0 && res2 < 0)
		return -1;
	return 0;
}


static int wpa_driver_ndis_add_wep(struct wpa_driver_ndis_data *drv,
				   int pairwise, int key_idx, int set_tx,
				   const u8 *key, size_t key_len)
{
	NDIS_802_11_WEP *wep;
	size_t len;
	int res;

	len = 12 + key_len;
	wep = os_zalloc(len);
	if (wep == NULL)
		return -1;
	wep->Length = len;
	wep->KeyIndex = key_idx;
	if (set_tx)
		wep->KeyIndex |= 1 << 31;
#if 0 /* Setting bit30 does not seem to work with some NDIS drivers */
	if (pairwise)
		wep->KeyIndex |= 1 << 30;
#endif
	wep->KeyLength = key_len;
	os_memcpy(wep->KeyMaterial, key, key_len);

	wpa_hexdump_key(MSG_MSGDUMP, "NDIS: OID_802_11_ADD_WEP",
			(u8 *) wep, len);
	res = ndis_set_oid(drv, OID_802_11_ADD_WEP, (char *) wep, len);

	os_free(wep);

	return res;
}


static int wpa_driver_ndis_set_key(const char *ifname, void *priv,
				   enum wpa_alg alg, const u8 *addr,
				   int key_idx, int set_tx,
				   const u8 *seq, size_t seq_len,
				   const u8 *key, size_t key_len)
{
	struct wpa_driver_ndis_data *drv = priv;
	size_t len, i;
	NDIS_802_11_KEY *nkey;
	int res, pairwise;
	u8 bssid[ETH_ALEN];

	if (addr == NULL || is_broadcast_ether_addr(addr)) {
		/* Group Key */
		pairwise = 0;
		if (wpa_driver_ndis_get_bssid(drv, bssid) < 0)
			os_memset(bssid, 0xff, ETH_ALEN);
	} else {
		/* Pairwise Key */
		pairwise = 1;
		os_memcpy(bssid, addr, ETH_ALEN);
	}

	if (alg == WPA_ALG_NONE || key_len == 0) {
		return wpa_driver_ndis_remove_key(drv, key_idx, addr, bssid,
						  pairwise);
	}

	if (alg == WPA_ALG_WEP) {
		return wpa_driver_ndis_add_wep(drv, pairwise, key_idx, set_tx,
					       key, key_len);
	}

	len = 12 + 6 + 6 + 8 + key_len;

	nkey = os_zalloc(len);
	if (nkey == NULL)
		return -1;

	nkey->Length = len;
	nkey->KeyIndex = key_idx;
	if (set_tx)
		nkey->KeyIndex |= 1 << 31;
	if (pairwise)
		nkey->KeyIndex |= 1 << 30;
	if (seq && seq_len)
		nkey->KeyIndex |= 1 << 29;
	nkey->KeyLength = key_len;
	os_memcpy(nkey->BSSID, bssid, ETH_ALEN);
	if (seq && seq_len) {
		for (i = 0; i < seq_len; i++)
			nkey->KeyRSC |= (ULONGLONG) seq[i] << (i * 8);
	}
	if (alg == WPA_ALG_TKIP && key_len == 32) {
		os_memcpy(nkey->KeyMaterial, key, 16);
		os_memcpy(nkey->KeyMaterial + 16, key + 24, 8);
		os_memcpy(nkey->KeyMaterial + 24, key + 16, 8);
	} else {
		os_memcpy(nkey->KeyMaterial, key, key_len);
	}

	wpa_hexdump_key(MSG_MSGDUMP, "NDIS: OID_802_11_ADD_KEY",
			(u8 *) nkey, len);
	res = ndis_set_oid(drv, OID_802_11_ADD_KEY, (char *) nkey, len);
	os_free(nkey);

	return res;
}


static int
wpa_driver_ndis_associate(void *priv,
			  struct wpa_driver_associate_params *params)
{
	struct wpa_driver_ndis_data *drv = priv;
	u32 auth_mode, encr, priv_mode, mode;
	u8 bcast[ETH_ALEN] = { 0xff, 0xff, 0xff, 0xff, 0xff, 0xff };

	drv->mode = params->mode;

	/* Note: Setting OID_802_11_INFRASTRUCTURE_MODE clears current keys,
	 * so static WEP keys needs to be set again after this. */
	if (params->mode == IEEE80211_MODE_IBSS) {
		mode = Ndis802_11IBSS;
		/* Need to make sure that BSSID polling is enabled for
		 * IBSS mode. */
		eloop_cancel_timeout(wpa_driver_ndis_poll_timeout, drv, NULL);
		eloop_register_timeout(1, 0, wpa_driver_ndis_poll_timeout,
				       drv, NULL);
	} else
		mode = Ndis802_11Infrastructure;
	if (ndis_set_oid(drv, OID_802_11_INFRASTRUCTURE_MODE,
			 (char *) &mode, sizeof(mode)) < 0) {
		wpa_printf(MSG_DEBUG, "NDIS: Failed to set "
			   "OID_802_11_INFRASTRUCTURE_MODE (%d)",
			   (int) mode);
		/* Try to continue anyway */
	}

	if (params->key_mgmt_suite == KEY_MGMT_NONE ||
	    params->key_mgmt_suite == KEY_MGMT_802_1X_NO_WPA) {
		/* Re-set WEP keys if static WEP configuration is used. */
		int i;
		for (i = 0; i < 4; i++) {
			if (!params->wep_key[i])
				continue;
			wpa_printf(MSG_DEBUG, "NDIS: Re-setting static WEP "
				   "key %d", i);
			wpa_driver_ndis_set_key(drv->ifname, drv, WPA_ALG_WEP,
						bcast, i,
						i == params->wep_tx_keyidx,
						NULL, 0, params->wep_key[i],
						params->wep_key_len[i]);
		}
	}

	if (params->wpa_ie == NULL || params->wpa_ie_len == 0) {
		if (params->auth_alg & WPA_AUTH_ALG_SHARED) {
			if (params->auth_alg & WPA_AUTH_ALG_OPEN)
				auth_mode = Ndis802_11AuthModeAutoSwitch;
			else
				auth_mode = Ndis802_11AuthModeShared;
		} else
			auth_mode = Ndis802_11AuthModeOpen;
		priv_mode = Ndis802_11PrivFilterAcceptAll;
	} else if (params->wpa_ie[0] == WLAN_EID_RSN) {
		priv_mode = Ndis802_11PrivFilter8021xWEP;
		if (params->key_mgmt_suite == KEY_MGMT_PSK)
			auth_mode = Ndis802_11AuthModeWPA2PSK;
		else
			auth_mode = Ndis802_11AuthModeWPA2;
#ifdef CONFIG_WPS
	} else if (params->key_mgmt_suite == KEY_MGMT_WPS) {
		auth_mode = Ndis802_11AuthModeOpen;
		priv_mode = Ndis802_11PrivFilterAcceptAll;
		if (params->wps == WPS_MODE_PRIVACY) {
			u8 dummy_key[5] = { 0x11, 0x22, 0x33, 0x44, 0x55 };
			/*
			 * Some NDIS drivers refuse to associate in open mode
			 * configuration due to Privacy field mismatch, so use
			 * a workaround to make the configuration look like
			 * matching one for WPS provisioning.
			 */
			wpa_printf(MSG_DEBUG, "NDIS: Set dummy WEP key as a "
				   "workaround to allow driver to associate "
				   "for WPS");
			wpa_driver_ndis_set_key(drv->ifname, drv, WPA_ALG_WEP,
						bcast, 0, 1,
						NULL, 0, dummy_key,
						sizeof(dummy_key));
		}
#endif /* CONFIG_WPS */
	} else {
		priv_mode = Ndis802_11PrivFilter8021xWEP;
		if (params->key_mgmt_suite == KEY_MGMT_WPA_NONE)
			auth_mode = Ndis802_11AuthModeWPANone;
		else if (params->key_mgmt_suite == KEY_MGMT_PSK)
			auth_mode = Ndis802_11AuthModeWPAPSK;
		else
			auth_mode = Ndis802_11AuthModeWPA;
	}

	switch (params->pairwise_suite) {
	case CIPHER_CCMP:
		encr = Ndis802_11Encryption3Enabled;
		break;
	case CIPHER_TKIP:
		encr = Ndis802_11Encryption2Enabled;
		break;
	case CIPHER_WEP40:
	case CIPHER_WEP104:
		encr = Ndis802_11Encryption1Enabled;
		break;
	case CIPHER_NONE:
#ifdef CONFIG_WPS
		if (params->wps == WPS_MODE_PRIVACY) {
			encr = Ndis802_11Encryption1Enabled;
			break;
		}
#endif /* CONFIG_WPS */
		if (params->group_suite == CIPHER_CCMP)
			encr = Ndis802_11Encryption3Enabled;
		else if (params->group_suite == CIPHER_TKIP)
			encr = Ndis802_11Encryption2Enabled;
		else
			encr = Ndis802_11EncryptionDisabled;
		break;
	default:
#ifdef CONFIG_WPS
		if (params->wps == WPS_MODE_PRIVACY) {
			encr = Ndis802_11Encryption1Enabled;
			break;
		}
#endif /* CONFIG_WPS */
		encr = Ndis802_11EncryptionDisabled;
		break;
	};

	if (ndis_set_oid(drv, OID_802_11_PRIVACY_FILTER,
			 (char *) &priv_mode, sizeof(priv_mode)) < 0) {
		wpa_printf(MSG_DEBUG, "NDIS: Failed to set "
			   "OID_802_11_PRIVACY_FILTER (%d)",
			   (int) priv_mode);
		/* Try to continue anyway */
	}

	ndis_set_auth_mode(drv, auth_mode);
	ndis_set_encr_status(drv, encr);

	if (params->bssid) {
		ndis_set_oid(drv, OID_802_11_BSSID, (char *) params->bssid,
			     ETH_ALEN);
		drv->oid_bssid_set = 1;
	} else if (drv->oid_bssid_set) {
		ndis_set_oid(drv, OID_802_11_BSSID, "\xff\xff\xff\xff\xff\xff",
			     ETH_ALEN);
		drv->oid_bssid_set = 0;
	}

	return wpa_driver_ndis_set_ssid(drv, params->ssid, params->ssid_len);
}


static int wpa_driver_ndis_set_pmkid(struct wpa_driver_ndis_data *drv)
{
	int len, count, i, ret;
	struct ndis_pmkid_entry *entry;
	NDIS_802_11_PMKID *p;

	count = 0;
	entry = drv->pmkid;
	while (entry) {
		count++;
		if (count >= drv->no_of_pmkid)
			break;
		entry = entry->next;
	}
	len = 8 + count * sizeof(BSSID_INFO);
	p = os_zalloc(len);
	if (p == NULL)
		return -1;

	p->Length = len;
	p->BSSIDInfoCount = count;
	entry = drv->pmkid;
	for (i = 0; i < count; i++) {
		os_memcpy(&p->BSSIDInfo[i].BSSID, entry->bssid, ETH_ALEN);
		os_memcpy(&p->BSSIDInfo[i].PMKID, entry->pmkid, 16);
		entry = entry->next;
	}
	wpa_hexdump(MSG_MSGDUMP, "NDIS: OID_802_11_PMKID", (u8 *) p, len);
	ret = ndis_set_oid(drv, OID_802_11_PMKID, (char *) p, len);
	os_free(p);
	return ret;
}


static int wpa_driver_ndis_add_pmkid(void *priv, const u8 *bssid,
				     const u8 *pmkid)
{
	struct wpa_driver_ndis_data *drv = priv;
	struct ndis_pmkid_entry *entry, *prev;

	if (drv->no_of_pmkid == 0)
		return 0;

	prev = NULL;
	entry = drv->pmkid;
	while (entry) {
		if (os_memcmp(entry->bssid, bssid, ETH_ALEN) == 0)
			break;
		prev = entry;
		entry = entry->next;
	}

	if (entry) {
		/* Replace existing entry for this BSSID and move it into the
		 * beginning of the list. */
		os_memcpy(entry->pmkid, pmkid, 16);
		if (prev) {
			prev->next = entry->next;
			entry->next = drv->pmkid;
			drv->pmkid = entry;
		}
	} else {
		entry = os_malloc(sizeof(*entry));
		if (entry) {
			os_memcpy(entry->bssid, bssid, ETH_ALEN);
			os_memcpy(entry->pmkid, pmkid, 16);
			entry->next = drv->pmkid;
			drv->pmkid = entry;
		}
	}

	return wpa_driver_ndis_set_pmkid(drv);
}


static int wpa_driver_ndis_remove_pmkid(void *priv, const u8 *bssid,
		 			const u8 *pmkid)
{
	struct wpa_driver_ndis_data *drv = priv;
	struct ndis_pmkid_entry *entry, *prev;

	if (drv->no_of_pmkid == 0)
		return 0;

	entry = drv->pmkid;
	prev = NULL;
	while (entry) {
		if (os_memcmp(entry->bssid, bssid, ETH_ALEN) == 0 &&
		    os_memcmp(entry->pmkid, pmkid, 16) == 0) {
			if (prev)
				prev->next = entry->next;
			else
				drv->pmkid = entry->next;
			os_free(entry);
			break;
		}
		prev = entry;
		entry = entry->next;
	}
	return wpa_driver_ndis_set_pmkid(drv);
}


static int wpa_driver_ndis_flush_pmkid(void *priv)
{
	struct wpa_driver_ndis_data *drv = priv;
	NDIS_802_11_PMKID p;
	struct ndis_pmkid_entry *pmkid, *prev;
	int prev_authmode, ret;

	if (drv->no_of_pmkid == 0)
		return 0;

	pmkid = drv->pmkid;
	drv->pmkid = NULL;
	while (pmkid) {
		prev = pmkid;
		pmkid = pmkid->next;
		os_free(prev);
	}

	/*
	 * Some drivers may refuse OID_802_11_PMKID if authMode is not set to
	 * WPA2, so change authMode temporarily, if needed.
	 */
	prev_authmode = ndis_get_auth_mode(drv);
	if (prev_authmode != Ndis802_11AuthModeWPA2)
		ndis_set_auth_mode(drv, Ndis802_11AuthModeWPA2);

	os_memset(&p, 0, sizeof(p));
	p.Length = 8;
	p.BSSIDInfoCount = 0;
	wpa_hexdump(MSG_MSGDUMP, "NDIS: OID_802_11_PMKID (flush)",
		    (u8 *) &p, 8);
	ret = ndis_set_oid(drv, OID_802_11_PMKID, (char *) &p, 8);

	if (prev_authmode != Ndis802_11AuthModeWPA2)
		ndis_set_auth_mode(drv, prev_authmode);

	return ret;
}


static int wpa_driver_ndis_get_associnfo(struct wpa_driver_ndis_data *drv)
{
	char buf[512], *pos;
	NDIS_802_11_ASSOCIATION_INFORMATION *ai;
	int len;
	union wpa_event_data data;
	NDIS_802_11_BSSID_LIST_EX *b;
	size_t blen, i;

	len = ndis_get_oid(drv, OID_802_11_ASSOCIATION_INFORMATION, buf,
			   sizeof(buf));
	if (len < 0) {
		wpa_printf(MSG_DEBUG, "NDIS: failed to get association "
			   "information");
		return -1;
	}
	if (len > sizeof(buf)) {
		/* Some drivers seem to be producing incorrect length for this
		 * data. Limit the length to the current buffer size to avoid
		 * crashing in hexdump. The data seems to be otherwise valid,
		 * so better try to use it. */
		wpa_printf(MSG_DEBUG, "NDIS: ignored bogus association "
			   "information length %d", len);
		len = ndis_get_oid(drv, OID_802_11_ASSOCIATION_INFORMATION,
				   buf, sizeof(buf));
		if (len < -1) {
			wpa_printf(MSG_DEBUG, "NDIS: re-reading association "
				   "information failed");
			return -1;
		}
		if (len > sizeof(buf)) {
			wpa_printf(MSG_DEBUG, "NDIS: ignored bogus association"
				   " information length %d (re-read)", len);
			len = sizeof(buf);
		}
	}
	wpa_hexdump(MSG_MSGDUMP, "NDIS: association information",
		    (u8 *) buf, len);
	if (len < sizeof(*ai)) {
		wpa_printf(MSG_DEBUG, "NDIS: too short association "
			   "information");
		return -1;
	}
	ai = (NDIS_802_11_ASSOCIATION_INFORMATION *) buf;
	wpa_printf(MSG_DEBUG, "NDIS: ReqFixed=0x%x RespFixed=0x%x off_req=%d "
		   "off_resp=%d len_req=%d len_resp=%d",
		   ai->AvailableRequestFixedIEs, ai->AvailableResponseFixedIEs,
		   (int) ai->OffsetRequestIEs, (int) ai->OffsetResponseIEs,
		   (int) ai->RequestIELength, (int) ai->ResponseIELength);

	if (ai->OffsetRequestIEs + ai->RequestIELength > (unsigned) len ||
	    ai->OffsetResponseIEs + ai->ResponseIELength > (unsigned) len) {
		wpa_printf(MSG_DEBUG, "NDIS: association information - "
			   "IE overflow");
		return -1;
	}

	wpa_hexdump(MSG_MSGDUMP, "NDIS: Request IEs",
		    (u8 *) buf + ai->OffsetRequestIEs, ai->RequestIELength);
	wpa_hexdump(MSG_MSGDUMP, "NDIS: Response IEs",
		    (u8 *) buf + ai->OffsetResponseIEs, ai->ResponseIELength);

	os_memset(&data, 0, sizeof(data));
	data.assoc_info.req_ies = (u8 *) buf + ai->OffsetRequestIEs;
	data.assoc_info.req_ies_len = ai->RequestIELength;
	data.assoc_info.resp_ies = (u8 *) buf + ai->OffsetResponseIEs;
	data.assoc_info.resp_ies_len = ai->ResponseIELength;

	blen = 65535;
	b = os_zalloc(blen);
	if (b == NULL)
		goto skip_scan_results;
	len = ndis_get_oid(drv, OID_802_11_BSSID_LIST, (char *) b, blen);
	if (len < 0) {
		wpa_printf(MSG_DEBUG, "NDIS: failed to get scan results");
		os_free(b);
		b = NULL;
		goto skip_scan_results;
	}
	wpa_printf(MSG_DEBUG, "NDIS: %d BSSID items to process for AssocInfo",
		   (unsigned int) b->NumberOfItems);

	pos = (char *) &b->Bssid[0];
	for (i = 0; i < b->NumberOfItems; i++) {
		NDIS_WLAN_BSSID_EX *bss = (NDIS_WLAN_BSSID_EX *) pos;
		if (os_memcmp(drv->bssid, bss->MacAddress, ETH_ALEN) == 0 &&
		    bss->IELength > sizeof(NDIS_802_11_FIXED_IEs)) {
			data.assoc_info.beacon_ies =
				((u8 *) bss->IEs) +
				sizeof(NDIS_802_11_FIXED_IEs);
			data.assoc_info.beacon_ies_len =
				bss->IELength - sizeof(NDIS_802_11_FIXED_IEs);
			wpa_hexdump(MSG_MSGDUMP, "NDIS: Beacon IEs",
				    data.assoc_info.beacon_ies,
				    data.assoc_info.beacon_ies_len);
			break;
		}
		pos += bss->Length;
		if (pos > (char *) b + blen)
			break;
	}

skip_scan_results:
	wpa_supplicant_event(drv->ctx, EVENT_ASSOCINFO, &data);

	os_free(b);

	return 0;
}


static void wpa_driver_ndis_poll_timeout(void *eloop_ctx, void *timeout_ctx)
{
	struct wpa_driver_ndis_data *drv = eloop_ctx;
	u8 bssid[ETH_ALEN];
	int poll;

	if (drv->wired)
		return;

	if (wpa_driver_ndis_get_bssid(drv, bssid)) {
		/* Disconnected */
		if (!is_zero_ether_addr(drv->bssid)) {
			os_memset(drv->bssid, 0, ETH_ALEN);
			wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, NULL);
		}
	} else {
		/* Connected */
		if (os_memcmp(drv->bssid, bssid, ETH_ALEN) != 0) {
			os_memcpy(drv->bssid, bssid, ETH_ALEN);
			wpa_driver_ndis_get_associnfo(drv);
			wpa_supplicant_event(drv->ctx, EVENT_ASSOC, NULL);
		}
	}

	/* When using integrated NDIS event receiver, we can skip BSSID
	 * polling when using infrastructure network. However, when using
	 * IBSS mode, many driver do not seem to generate connection event,
	 * so we need to enable BSSID polling to figure out when IBSS network
	 * has been formed.
	 */
	poll = drv->mode == IEEE80211_MODE_IBSS;
#ifndef CONFIG_NDIS_EVENTS_INTEGRATED
#ifndef _WIN32_WCE
	poll = 1;
#endif /* _WIN32_WCE */
#endif /* CONFIG_NDIS_EVENTS_INTEGRATED */

	if (poll) {
		eloop_register_timeout(1, 0, wpa_driver_ndis_poll_timeout,
					drv, NULL);
	}
}


static void wpa_driver_ndis_poll(void *priv)
{
	struct wpa_driver_ndis_data *drv = priv;
	eloop_cancel_timeout(wpa_driver_ndis_poll_timeout, drv, NULL);
	wpa_driver_ndis_poll_timeout(drv, NULL);
}


/* Called when driver generates Media Connect Event by calling
 * NdisMIndicateStatus() with NDIS_STATUS_MEDIA_CONNECT */
void wpa_driver_ndis_event_connect(struct wpa_driver_ndis_data *drv)
{
	wpa_printf(MSG_DEBUG, "NDIS: Media Connect Event");
	if (wpa_driver_ndis_get_bssid(drv, drv->bssid) == 0) {
		wpa_driver_ndis_get_associnfo(drv);
		wpa_supplicant_event(drv->ctx, EVENT_ASSOC, NULL);
	}
}


/* Called when driver generates Media Disconnect Event by calling
 * NdisMIndicateStatus() with NDIS_STATUS_MEDIA_DISCONNECT */
void wpa_driver_ndis_event_disconnect(struct wpa_driver_ndis_data *drv)
{
	wpa_printf(MSG_DEBUG, "NDIS: Media Disconnect Event");
	os_memset(drv->bssid, 0, ETH_ALEN);
	wpa_supplicant_event(drv->ctx, EVENT_DISASSOC, NULL);
}


static void wpa_driver_ndis_event_auth(struct wpa_driver_ndis_data *drv,
				       const u8 *data, size_t data_len)
{
	NDIS_802_11_AUTHENTICATION_REQUEST *req;
	int pairwise = 0, group = 0;
	union wpa_event_data event;

	if (data_len < sizeof(*req)) {
		wpa_printf(MSG_DEBUG, "NDIS: Too short Authentication Request "
			   "Event (len=%d)", data_len);
		return;
	}
	req = (NDIS_802_11_AUTHENTICATION_REQUEST *) data;

	wpa_printf(MSG_DEBUG, "NDIS: Authentication Request Event: "
		   "Bssid " MACSTR " Flags 0x%x",
		   MAC2STR(req->Bssid), (int) req->Flags);

	if ((req->Flags & NDIS_802_11_AUTH_REQUEST_PAIRWISE_ERROR) ==
	    NDIS_802_11_AUTH_REQUEST_PAIRWISE_ERROR)
		pairwise = 1;
	else if ((req->Flags & NDIS_802_11_AUTH_REQUEST_GROUP_ERROR) ==
	    NDIS_802_11_AUTH_REQUEST_GROUP_ERROR)
		group = 1;

	if (pairwise || group) {
		os_memset(&event, 0, sizeof(event));
		event.michael_mic_failure.unicast = pairwise;
		wpa_supplicant_event(drv->ctx, EVENT_MICHAEL_MIC_FAILURE,
				     &event);
	}
}


static void wpa_driver_ndis_event_pmkid(struct wpa_driver_ndis_data *drv,
					const u8 *data, size_t data_len)
{
	NDIS_802_11_PMKID_CANDIDATE_LIST *pmkid;
	size_t i;
	union wpa_event_data event;

	if (data_len < 8) {
		wpa_printf(MSG_DEBUG, "NDIS: Too short PMKID Candidate List "
			   "Event (len=%d)", data_len);
		return;
	}
	pmkid = (NDIS_802_11_PMKID_CANDIDATE_LIST *) data;
	wpa_printf(MSG_DEBUG, "NDIS: PMKID Candidate List Event - Version %d "
		   "NumCandidates %d",
		   (int) pmkid->Version, (int) pmkid->NumCandidates);

	if (pmkid->Version != 1) {
		wpa_printf(MSG_DEBUG, "NDIS: Unsupported PMKID Candidate List "
			   "Version %d", (int) pmkid->Version);
		return;
	}

	if (data_len < 8 + pmkid->NumCandidates * sizeof(PMKID_CANDIDATE)) {
		wpa_printf(MSG_DEBUG, "NDIS: PMKID Candidate List underflow");
		return;
	}

	os_memset(&event, 0, sizeof(event));
	for (i = 0; i < pmkid->NumCandidates; i++) {
		PMKID_CANDIDATE *p = &pmkid->CandidateList[i];
		wpa_printf(MSG_DEBUG, "NDIS: %d: " MACSTR " Flags 0x%x",
			   i, MAC2STR(p->BSSID), (int) p->Flags);
		os_memcpy(event.pmkid_candidate.bssid, p->BSSID, ETH_ALEN);
		event.pmkid_candidate.index = i;
		event.pmkid_candidate.preauth =
			p->Flags & NDIS_802_11_PMKID_CANDIDATE_PREAUTH_ENABLED;
		wpa_supplicant_event(drv->ctx, EVENT_PMKID_CANDIDATE,
				     &event);
	}
}


/* Called when driver calls NdisMIndicateStatus() with
 * NDIS_STATUS_MEDIA_SPECIFIC_INDICATION */
void wpa_driver_ndis_event_media_specific(struct wpa_driver_ndis_data *drv,
					  const u8 *data, size_t data_len)
{
	NDIS_802_11_STATUS_INDICATION *status;

	if (data == NULL || data_len < sizeof(*status))
		return;

	wpa_hexdump(MSG_DEBUG, "NDIS: Media Specific Indication",
		    data, data_len);

	status = (NDIS_802_11_STATUS_INDICATION *) data;
	data += sizeof(status);
	data_len -= sizeof(status);

	switch (status->StatusType) {
	case Ndis802_11StatusType_Authentication:
		wpa_driver_ndis_event_auth(drv, data, data_len);
		break;
	case Ndis802_11StatusType_PMKID_CandidateList:
		wpa_driver_ndis_event_pmkid(drv, data, data_len);
		break;
	default:
		wpa_printf(MSG_DEBUG, "NDIS: Unknown StatusType %d",
			   (int) status->StatusType);
		break;
	}
}


/* Called when an adapter is added */
void wpa_driver_ndis_event_adapter_arrival(struct wpa_driver_ndis_data *drv)
{
	union wpa_event_data event;
	int i;

	wpa_printf(MSG_DEBUG, "NDIS: Notify Adapter Arrival");

	for (i = 0; i < 30; i++) {
		/* Re-open Packet32/NDISUIO connection */
		wpa_driver_ndis_adapter_close(drv);
		if (wpa_driver_ndis_adapter_init(drv) < 0 ||
		    wpa_driver_ndis_adapter_open(drv) < 0) {
			wpa_printf(MSG_DEBUG, "NDIS: Driver re-initialization "
				   "(%d) failed", i);
			os_sleep(1, 0);
		} else {
			wpa_printf(MSG_DEBUG, "NDIS: Driver re-initialized");
			break;
		}
	}

	os_memset(&event, 0, sizeof(event));
	os_strlcpy(event.interface_status.ifname, drv->ifname,
		   sizeof(event.interface_status.ifname));
	event.interface_status.ievent = EVENT_INTERFACE_ADDED;
	wpa_supplicant_event(drv->ctx, EVENT_INTERFACE_STATUS, &event);
}


/* Called when an adapter is removed */
void wpa_driver_ndis_event_adapter_removal(struct wpa_driver_ndis_data *drv)
{
	union wpa_event_data event;

	wpa_printf(MSG_DEBUG, "NDIS: Notify Adapter Removal");
	os_memset(&event, 0, sizeof(event));
	os_strlcpy(event.interface_status.ifname, drv->ifname,
		   sizeof(event.interface_status.ifname));
	event.interface_status.ievent = EVENT_INTERFACE_REMOVED;
	wpa_supplicant_event(drv->ctx, EVENT_INTERFACE_STATUS, &event);
}


static void
wpa_driver_ndis_get_wpa_capability(struct wpa_driver_ndis_data *drv)
{
	wpa_printf(MSG_DEBUG, "NDIS: verifying driver WPA capability");

	if (ndis_set_auth_mode(drv, Ndis802_11AuthModeWPA) == 0 &&
	    ndis_get_auth_mode(drv) == Ndis802_11AuthModeWPA) {
		wpa_printf(MSG_DEBUG, "NDIS: WPA key management supported");
		drv->capa.key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_WPA;
	}

	if (ndis_set_auth_mode(drv, Ndis802_11AuthModeWPAPSK) == 0 &&
	    ndis_get_auth_mode(drv) == Ndis802_11AuthModeWPAPSK) {
		wpa_printf(MSG_DEBUG, "NDIS: WPA-PSK key management "
			   "supported");
		drv->capa.key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK;
	}

	if (ndis_set_encr_status(drv, Ndis802_11Encryption3Enabled) == 0 &&
	    ndis_get_encr_status(drv) == Ndis802_11Encryption3KeyAbsent) {
		wpa_printf(MSG_DEBUG, "NDIS: CCMP encryption supported");
		drv->capa.enc |= WPA_DRIVER_CAPA_ENC_CCMP;
	}

	if (ndis_set_encr_status(drv, Ndis802_11Encryption2Enabled) == 0 &&
	    ndis_get_encr_status(drv) == Ndis802_11Encryption2KeyAbsent) {
		wpa_printf(MSG_DEBUG, "NDIS: TKIP encryption supported");
		drv->capa.enc |= WPA_DRIVER_CAPA_ENC_TKIP;
	}

	if (ndis_set_encr_status(drv, Ndis802_11Encryption1Enabled) == 0 &&
	    ndis_get_encr_status(drv) == Ndis802_11Encryption1KeyAbsent) {
		wpa_printf(MSG_DEBUG, "NDIS: WEP encryption supported");
		drv->capa.enc |= WPA_DRIVER_CAPA_ENC_WEP40 |
			WPA_DRIVER_CAPA_ENC_WEP104;
	}

	if (ndis_set_auth_mode(drv, Ndis802_11AuthModeShared) == 0 &&
	    ndis_get_auth_mode(drv) == Ndis802_11AuthModeShared) {
		drv->capa.auth |= WPA_DRIVER_AUTH_SHARED;
	}

	if (ndis_set_auth_mode(drv, Ndis802_11AuthModeOpen) == 0 &&
	    ndis_get_auth_mode(drv) == Ndis802_11AuthModeOpen) {
		drv->capa.auth |= WPA_DRIVER_AUTH_OPEN;
	}

	ndis_set_encr_status(drv, Ndis802_11EncryptionDisabled);

	/* Could also verify OID_802_11_ADD_KEY error reporting and
	 * support for OID_802_11_ASSOCIATION_INFORMATION. */

	if (drv->capa.key_mgmt & WPA_DRIVER_CAPA_KEY_MGMT_WPA &&
	    drv->capa.enc & (WPA_DRIVER_CAPA_ENC_TKIP |
			     WPA_DRIVER_CAPA_ENC_CCMP)) {
		wpa_printf(MSG_DEBUG, "NDIS: driver supports WPA");
		drv->has_capability = 1;
	} else {
		wpa_printf(MSG_DEBUG, "NDIS: no WPA support found");
	}

	wpa_printf(MSG_DEBUG, "NDIS: driver capabilities: key_mgmt 0x%x "
		   "enc 0x%x auth 0x%x",
		   drv->capa.key_mgmt, drv->capa.enc, drv->capa.auth);
}


static void wpa_driver_ndis_get_capability(struct wpa_driver_ndis_data *drv)
{
	char buf[512];
	int len;
	size_t i;
	NDIS_802_11_CAPABILITY *c;

	drv->capa.flags = WPA_DRIVER_FLAGS_DRIVER_IE;

	len = ndis_get_oid(drv, OID_802_11_CAPABILITY, buf, sizeof(buf));
	if (len < 0) {
		wpa_driver_ndis_get_wpa_capability(drv);
		return;
	}

	wpa_hexdump(MSG_MSGDUMP, "OID_802_11_CAPABILITY", (u8 *) buf, len);
	c = (NDIS_802_11_CAPABILITY *) buf;
	if (len < sizeof(*c) || c->Version != 2) {
		wpa_printf(MSG_DEBUG, "NDIS: unsupported "
			   "OID_802_11_CAPABILITY data");
		return;
	}
	wpa_printf(MSG_DEBUG, "NDIS: Driver supports OID_802_11_CAPABILITY - "
		   "NoOfPMKIDs %d NoOfAuthEncrPairs %d",
		   (int) c->NoOfPMKIDs,
		   (int) c->NoOfAuthEncryptPairsSupported);
	drv->has_capability = 1;
	drv->no_of_pmkid = c->NoOfPMKIDs;
	for (i = 0; i < c->NoOfAuthEncryptPairsSupported; i++) {
		NDIS_802_11_AUTHENTICATION_ENCRYPTION *ae;
		ae = &c->AuthenticationEncryptionSupported[i];
		if ((char *) (ae + 1) > buf + len) {
			wpa_printf(MSG_DEBUG, "NDIS: auth/encr pair list "
				   "overflow");
			break;
		}
		wpa_printf(MSG_MSGDUMP, "NDIS: %d - auth %d encr %d",
			   i, (int) ae->AuthModeSupported,
			   (int) ae->EncryptStatusSupported);
		switch (ae->AuthModeSupported) {
		case Ndis802_11AuthModeOpen:
			drv->capa.auth |= WPA_DRIVER_AUTH_OPEN;
			break;
		case Ndis802_11AuthModeShared:
			drv->capa.auth |= WPA_DRIVER_AUTH_SHARED;
			break;
		case Ndis802_11AuthModeWPA:
			drv->capa.key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_WPA;
			break;
		case Ndis802_11AuthModeWPAPSK:
			drv->capa.key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_WPA_PSK;
			break;
		case Ndis802_11AuthModeWPA2:
			drv->capa.key_mgmt |= WPA_DRIVER_CAPA_KEY_MGMT_WPA2;
			break;
		case Ndis802_11AuthModeWPA2PSK:
			drv->capa.key_mgmt |=
				WPA_DRIVER_CAPA_KEY_MGMT_WPA2_PSK;
			break;
		case Ndis802_11AuthModeWPANone:
			drv->capa.key_mgmt |=
				WPA_DRIVER_CAPA_KEY_MGMT_WPA_NONE;
			break;
		default:
			break;
		}
		switch (ae->EncryptStatusSupported) {
		case Ndis802_11Encryption1Enabled:
			drv->capa.enc |= WPA_DRIVER_CAPA_ENC_WEP40;
			drv->capa.enc |= WPA_DRIVER_CAPA_ENC_WEP104;
			break;
		case Ndis802_11Encryption2Enabled:
			drv->capa.enc |= WPA_DRIVER_CAPA_ENC_TKIP;
			break;
		case Ndis802_11Encryption3Enabled:
			drv->capa.enc |= WPA_DRIVER_CAPA_ENC_CCMP;
			break;
		default:
			break;
		}
	}

	wpa_printf(MSG_DEBUG, "NDIS: driver capabilities: key_mgmt 0x%x "
		   "enc 0x%x auth 0x%x",
		   drv->capa.key_mgmt, drv->capa.enc, drv->capa.auth);
}


static int wpa_driver_ndis_get_capa(void *priv, struct wpa_driver_capa *capa)
{
	struct wpa_driver_ndis_data *drv = priv;
	if (!drv->has_capability)
		return -1;
	os_memcpy(capa, &drv->capa, sizeof(*capa));
	return 0;
}


static const char * wpa_driver_ndis_get_ifname(void *priv)
{
	struct wpa_driver_ndis_data *drv = priv;
	return drv->ifname;
}


static const u8 * wpa_driver_ndis_get_mac_addr(void *priv)
{
	struct wpa_driver_ndis_data *drv = priv;
	return drv->own_addr;
}


#ifdef _WIN32_WCE

#define NDISUIO_MSG_SIZE (sizeof(NDISUIO_DEVICE_NOTIFICATION) + 512)

static void ndisuio_notification_receive(void *eloop_data, void *user_ctx)
{
	struct wpa_driver_ndis_data *drv = eloop_data;
	NDISUIO_DEVICE_NOTIFICATION *hdr;
	u8 buf[NDISUIO_MSG_SIZE];
	DWORD len, flags;

	if (!ReadMsgQueue(drv->event_queue, buf, NDISUIO_MSG_SIZE, &len, 0,
			  &flags)) {
		wpa_printf(MSG_DEBUG, "ndisuio_notification_receive: "
			   "ReadMsgQueue failed: %d", (int) GetLastError());
		return;
	}

	if (len < sizeof(NDISUIO_DEVICE_NOTIFICATION)) {
		wpa_printf(MSG_DEBUG, "ndisuio_notification_receive: "
			   "Too short message (len=%d)", (int) len);
		return;
	}

	hdr = (NDISUIO_DEVICE_NOTIFICATION *) buf;
	wpa_printf(MSG_DEBUG, "NDIS: Notification received: len=%d type=0x%x",
		   (int) len, hdr->dwNotificationType);

	switch (hdr->dwNotificationType) {
#ifdef NDISUIO_NOTIFICATION_ADAPTER_ARRIVAL
	case NDISUIO_NOTIFICATION_ADAPTER_ARRIVAL:
		wpa_printf(MSG_DEBUG, "NDIS: ADAPTER_ARRIVAL");
		wpa_driver_ndis_event_adapter_arrival(drv);
		break;
#endif
#ifdef NDISUIO_NOTIFICATION_ADAPTER_REMOVAL
	case NDISUIO_NOTIFICATION_ADAPTER_REMOVAL:
		wpa_printf(MSG_DEBUG, "NDIS: ADAPTER_REMOVAL");
		wpa_driver_ndis_event_adapter_removal(drv);
		break;
#endif
	case NDISUIO_NOTIFICATION_MEDIA_CONNECT:
		wpa_printf(MSG_DEBUG, "NDIS: MEDIA_CONNECT");
		SetEvent(drv->connected_event);
		wpa_driver_ndis_event_connect(drv);
		break;
	case NDISUIO_NOTIFICATION_MEDIA_DISCONNECT:
		ResetEvent(drv->connected_event);
		wpa_printf(MSG_DEBUG, "NDIS: MEDIA_DISCONNECT");
		wpa_driver_ndis_event_disconnect(drv);
		break;
	case NDISUIO_NOTIFICATION_MEDIA_SPECIFIC_NOTIFICATION:
		wpa_printf(MSG_DEBUG, "NDIS: MEDIA_SPECIFIC_NOTIFICATION");
#if _WIN32_WCE == 420 || _WIN32_WCE == 0x420
		wpa_driver_ndis_event_media_specific(
			drv, hdr->pvStatusBuffer, hdr->uiStatusBufferSize);
#else
		wpa_driver_ndis_event_media_specific(
			drv, ((const u8 *) hdr) + hdr->uiOffsetToStatusBuffer,
			(size_t) hdr->uiStatusBufferSize);
#endif
		break;
	default:
		wpa_printf(MSG_DEBUG, "NDIS: Unknown notification type 0x%x",
			   hdr->dwNotificationType);
		break;
	}
}


static void ndisuio_notification_deinit(struct wpa_driver_ndis_data *drv)
{
	NDISUIO_REQUEST_NOTIFICATION req;

	memset(&req, 0, sizeof(req));
	req.hMsgQueue = drv->event_queue;
	req.dwNotificationTypes = 0;

	if (!DeviceIoControl(drv->ndisuio, IOCTL_NDISUIO_REQUEST_NOTIFICATION,
			     &req, sizeof(req), NULL, 0, NULL, NULL)) {
		wpa_printf(MSG_INFO, "ndisuio_notification_deinit: "
			   "IOCTL_NDISUIO_REQUEST_NOTIFICATION failed: %d",
			   (int) GetLastError());
	}

	if (!DeviceIoControl(drv->ndisuio, IOCTL_NDISUIO_CANCEL_NOTIFICATION,
			     NULL, 0, NULL, 0, NULL, NULL)) {
		wpa_printf(MSG_INFO, "ndisuio_notification_deinit: "
			   "IOCTL_NDISUIO_CANCEL_NOTIFICATION failed: %d",
			   (int) GetLastError());
	}

	if (drv->event_queue) {
		eloop_unregister_event(drv->event_queue,
				       sizeof(drv->event_queue));
		CloseHandle(drv->event_queue);
		drv->event_queue = NULL;
	}

	if (drv->connected_event) {
		CloseHandle(drv->connected_event);
		drv->connected_event = NULL;
	}
}


static int ndisuio_notification_init(struct wpa_driver_ndis_data *drv)
{
	MSGQUEUEOPTIONS opt;
	NDISUIO_REQUEST_NOTIFICATION req;

	drv->connected_event =
		CreateEvent(NULL, TRUE, FALSE, TEXT("WpaSupplicantConnected"));
	if (drv->connected_event == NULL) {
		wpa_printf(MSG_INFO, "ndisuio_notification_init: "
			   "CreateEvent failed: %d",
			   (int) GetLastError());
		return -1;
	}

	memset(&opt, 0, sizeof(opt));
	opt.dwSize = sizeof(opt);
	opt.dwMaxMessages = 5;
	opt.cbMaxMessage = NDISUIO_MSG_SIZE;
	opt.bReadAccess = TRUE;

	drv->event_queue = CreateMsgQueue(NULL, &opt);
	if (drv->event_queue == NULL) {
		wpa_printf(MSG_INFO, "ndisuio_notification_init: "
			   "CreateMsgQueue failed: %d",
			   (int) GetLastError());
		ndisuio_notification_deinit(drv);
		return -1;
	}

	memset(&req, 0, sizeof(req));
	req.hMsgQueue = drv->event_queue;
	req.dwNotificationTypes =
#ifdef NDISUIO_NOTIFICATION_ADAPTER_ARRIVAL
		NDISUIO_NOTIFICATION_ADAPTER_ARRIVAL |
#endif
#ifdef NDISUIO_NOTIFICATION_ADAPTER_REMOVAL
		NDISUIO_NOTIFICATION_ADAPTER_REMOVAL |
#endif
		NDISUIO_NOTIFICATION_MEDIA_CONNECT |
		NDISUIO_NOTIFICATION_MEDIA_DISCONNECT |
		NDISUIO_NOTIFICATION_MEDIA_SPECIFIC_NOTIFICATION;

	if (!DeviceIoControl(drv->ndisuio, IOCTL_NDISUIO_REQUEST_NOTIFICATION,
			     &req, sizeof(req), NULL, 0, NULL, NULL)) {
		wpa_printf(MSG_INFO, "ndisuio_notification_init: "
			   "IOCTL_NDISUIO_REQUEST_NOTIFICATION failed: %d",
			   (int) GetLastError());
		ndisuio_notification_deinit(drv);
		return -1;
	}

	eloop_register_event(drv->event_queue, sizeof(drv->event_queue),
			     ndisuio_notification_receive, drv, NULL);

	return 0;
}
#endif /* _WIN32_WCE */


static int wpa_driver_ndis_get_names(struct wpa_driver_ndis_data *drv)
{
#ifdef CONFIG_USE_NDISUIO
	NDISUIO_QUERY_BINDING *b;
	size_t blen = sizeof(*b) + 1024;
	int i, error, found = 0;
	DWORD written;
	char name[256], desc[256], *dpos;
	WCHAR *pos;
	size_t j, len, dlen;

	b = os_malloc(blen);
	if (b == NULL)
		return -1;

	for (i = 0; ; i++) {
		os_memset(b, 0, blen);
		b->BindingIndex = i;
		if (!DeviceIoControl(drv->ndisuio, IOCTL_NDISUIO_QUERY_BINDING,
				     b, sizeof(NDISUIO_QUERY_BINDING), b, blen,
				     &written, NULL)) {
			error = (int) GetLastError();
			if (error == ERROR_NO_MORE_ITEMS)
				break;
			wpa_printf(MSG_DEBUG, "IOCTL_NDISUIO_QUERY_BINDING "
				   "failed: %d", error);
			break;
		}

		pos = (WCHAR *) ((char *) b + b->DeviceNameOffset);
		len = b->DeviceNameLength;
		if (len >= sizeof(name))
			len = sizeof(name) - 1;
		for (j = 0; j < len; j++)
			name[j] = (char) pos[j];
		name[len] = '\0';

		pos = (WCHAR *) ((char *) b + b->DeviceDescrOffset);
		len = b->DeviceDescrLength;
		if (len >= sizeof(desc))
			len = sizeof(desc) - 1;
		for (j = 0; j < len; j++)
			desc[j] = (char) pos[j];
		desc[len] = '\0';

		wpa_printf(MSG_DEBUG, "NDIS: %d - %s - %s", i, name, desc);

		if (os_strstr(name, drv->ifname)) {
			wpa_printf(MSG_DEBUG, "NDIS: Interface name match");
			found = 1;
			break;
		}

		if (os_strncmp(desc, drv->ifname, os_strlen(drv->ifname)) == 0)
		{
			wpa_printf(MSG_DEBUG, "NDIS: Interface description "
				   "match");
			found = 1;
			break;
		}
	}

	if (!found) {
		wpa_printf(MSG_DEBUG, "NDIS: Could not find interface '%s'",
			   drv->ifname);
		os_free(b);
		return -1;
	}

	os_strlcpy(drv->ifname,
		   os_strncmp(name, "\\DEVICE\\", 8) == 0 ? name + 8 : name,
		   sizeof(drv->ifname));
#ifdef _WIN32_WCE
	drv->adapter_name = wpa_strdup_tchar(drv->ifname);
	if (drv->adapter_name == NULL) {
		wpa_printf(MSG_ERROR, "NDIS: Failed to allocate memory for "
			   "adapter name");
		os_free(b);
		return -1;
	}
#endif /* _WIN32_WCE */

	dpos = os_strstr(desc, " - ");
	if (dpos)
		dlen = dpos - desc;
	else
		dlen = os_strlen(desc);
	drv->adapter_desc = os_malloc(dlen + 1);
	if (drv->adapter_desc) {
		os_memcpy(drv->adapter_desc, desc, dlen);
		drv->adapter_desc[dlen] = '\0';
	}

	os_free(b);

	if (drv->adapter_desc == NULL)
		return -1;

	wpa_printf(MSG_DEBUG, "NDIS: Adapter description prefix '%s'",
		   drv->adapter_desc);

	return 0;
#else /* CONFIG_USE_NDISUIO */
	PTSTR _names;
	char *names, *pos, *pos2;
	ULONG len;
	BOOLEAN res;
#define MAX_ADAPTERS 32
	char *name[MAX_ADAPTERS];
	char *desc[MAX_ADAPTERS];
	int num_name, num_desc, i, found_name, found_desc;
	size_t dlen;

	wpa_printf(MSG_DEBUG, "NDIS: Packet.dll version: %s",
		   PacketGetVersion());

	len = 8192;
	_names = os_zalloc(len);
	if (_names == NULL)
		return -1;

	res = PacketGetAdapterNames(_names, &len);
	if (!res && len > 8192) {
		os_free(_names);
		_names = os_zalloc(len);
		if (_names == NULL)
			return -1;
		res = PacketGetAdapterNames(_names, &len);
	}

	if (!res) {
		wpa_printf(MSG_ERROR, "NDIS: Failed to get adapter list "
			   "(PacketGetAdapterNames)");
		os_free(_names);
		return -1;
	}

	names = (char *) _names;
	if (names[0] && names[1] == '\0' && names[2] && names[3] == '\0') {
		wpa_printf(MSG_DEBUG, "NDIS: Looks like adapter names are in "
			   "UNICODE");
		/* Convert to ASCII */
		pos2 = pos = names;
		while (pos2 < names + len) {
			if (pos2[0] == '\0' && pos2[1] == '\0' &&
			    pos2[2] == '\0' && pos2[3] == '\0') {
				pos2 += 4;
				break;
			}
			*pos++ = pos2[0];
			pos2 += 2;
		}
		os_memcpy(pos + 2, names, pos - names);
		pos += 2;
	} else
		pos = names;

	num_name = 0;
	while (pos < names + len) {
		name[num_name] = pos;
		while (*pos && pos < names + len)
			pos++;
		if (pos + 1 >= names + len) {
			os_free(names);
			return -1;
		}
		pos++;
		num_name++;
		if (num_name >= MAX_ADAPTERS) {
			wpa_printf(MSG_DEBUG, "NDIS: Too many adapters");
			os_free(names);
			return -1;
		}
		if (*pos == '\0') {
			wpa_printf(MSG_DEBUG, "NDIS: %d adapter names found",
				   num_name);
			pos++;
			break;
		}
	}

	num_desc = 0;
	while (pos < names + len) {
		desc[num_desc] = pos;
		while (*pos && pos < names + len)
			pos++;
		if (pos + 1 >= names + len) {
			os_free(names);
			return -1;
		}
		pos++;
		num_desc++;
		if (num_desc >= MAX_ADAPTERS) {
			wpa_printf(MSG_DEBUG, "NDIS: Too many adapter "
				   "descriptions");
			os_free(names);
			return -1;
		}
		if (*pos == '\0') {
			wpa_printf(MSG_DEBUG, "NDIS: %d adapter descriptions "
				   "found", num_name);
			pos++;
			break;
		}
	}

	/*
	 * Windows 98 with Packet.dll 3.0 alpha3 does not include adapter
	 * descriptions. Fill in dummy descriptors to work around this.
	 */
	while (num_desc < num_name)
		desc[num_desc++] = "dummy description";

	if (num_name != num_desc) {
		wpa_printf(MSG_DEBUG, "NDIS: mismatch in adapter name and "
			   "description counts (%d != %d)",
			   num_name, num_desc);
		os_free(names);
		return -1;
	}

	found_name = found_desc = -1;
	for (i = 0; i < num_name; i++) {
		wpa_printf(MSG_DEBUG, "NDIS: %d - %s - %s",
			   i, name[i], desc[i]);
		if (found_name == -1 && os_strstr(name[i], drv->ifname))
			found_name = i;
		if (found_desc == -1 &&
		    os_strncmp(desc[i], drv->ifname, os_strlen(drv->ifname)) ==
		    0)
			found_desc = i;
	}

	if (found_name < 0 && found_desc >= 0) {
		wpa_printf(MSG_DEBUG, "NDIS: Matched interface '%s' based on "
			   "description '%s'",
			   name[found_desc], desc[found_desc]);
		found_name = found_desc;
		os_strlcpy(drv->ifname,
			   os_strncmp(name[found_desc], "\\Device\\NPF_", 12)
			   == 0 ? name[found_desc] + 12 : name[found_desc],
			   sizeof(drv->ifname));
	}

	if (found_name < 0) {
		wpa_printf(MSG_DEBUG, "NDIS: Could not find interface '%s'",
			   drv->ifname);
		os_free(names);
		return -1;
	}

	i = found_name;
	pos = os_strrchr(desc[i], '(');
	if (pos) {
		dlen = pos - desc[i];
		pos--;
		if (pos > desc[i] && *pos == ' ')
			dlen--;
	} else {
		dlen = os_strlen(desc[i]);
	}
	drv->adapter_desc = os_malloc(dlen + 1);
	if (drv->adapter_desc) {
		os_memcpy(drv->adapter_desc, desc[i], dlen);
		drv->adapter_desc[dlen] = '\0';
	}

	os_free(names);

	if (drv->adapter_desc == NULL)
		return -1;

	wpa_printf(MSG_DEBUG, "NDIS: Adapter description prefix '%s'",
		   drv->adapter_desc);

	return 0;
#endif /* CONFIG_USE_NDISUIO */
}


#if defined(CONFIG_NATIVE_WINDOWS) || defined(__CYGWIN__)
#ifndef _WIN32_WCE
/*
 * These structures are undocumented for WinXP; only WinCE version is
 * documented. These would be included wzcsapi.h if it were available. Some
 * changes here have been needed to make the structures match with WinXP SP2.
 * It is unclear whether these work with any other version.
 */

typedef struct {
	LPWSTR wszGuid;
} INTF_KEY_ENTRY, *PINTF_KEY_ENTRY;

typedef struct {
	DWORD dwNumIntfs;
	PINTF_KEY_ENTRY pIntfs;
} INTFS_KEY_TABLE, *PINTFS_KEY_TABLE;

typedef struct {
	DWORD dwDataLen;
	LPBYTE pData;
} RAW_DATA, *PRAW_DATA;

typedef struct {
	LPWSTR wszGuid;
	LPWSTR wszDescr;
	ULONG ulMediaState;
	ULONG ulMediaType;
	ULONG ulPhysicalMediaType;
	INT nInfraMode;
	INT nAuthMode;
	INT nWepStatus;
#ifndef _WIN32_WCE
	u8 pad[2]; /* why is this needed? */
#endif /* _WIN32_WCE */
	DWORD dwCtlFlags;
	DWORD dwCapabilities; /* something added for WinXP SP2(?) */
	RAW_DATA rdSSID;
	RAW_DATA rdBSSID;
	RAW_DATA rdBSSIDList;
	RAW_DATA rdStSSIDList;
	RAW_DATA rdCtrlData;
#ifdef UNDER_CE
	BOOL bInitialized;
#endif
	DWORD nWPAMCastCipher;
	/* add some extra buffer for later additions since this interface is
	 * far from stable */
	u8 later_additions[100];
} INTF_ENTRY, *PINTF_ENTRY;

#define INTF_ALL 0xffffffff
#define INTF_ALL_FLAGS 0x0000ffff
#define INTF_CTLFLAGS 0x00000010
#define INTFCTL_ENABLED 0x8000
#endif /* _WIN32_WCE */


#ifdef _WIN32_WCE
static int wpa_driver_ndis_rebind_adapter(struct wpa_driver_ndis_data *drv)
{
	HANDLE ndis;
	TCHAR multi[100];
	int len;

	len = _tcslen(drv->adapter_name);
	if (len > 80)
		return -1;

	ndis = CreateFile(DD_NDIS_DEVICE_NAME, GENERIC_READ | GENERIC_WRITE,
			  0, NULL, OPEN_EXISTING, 0, NULL);
	if (ndis == INVALID_HANDLE_VALUE) {
		wpa_printf(MSG_DEBUG, "NDIS: Failed to open file to NDIS "
			   "device: %d", (int) GetLastError());
		return -1;
	}

	len++;
	memcpy(multi, drv->adapter_name, len * sizeof(TCHAR));
	memcpy(&multi[len], TEXT("NDISUIO\0"), 9 * sizeof(TCHAR));
	len += 9;

	if (!DeviceIoControl(ndis, IOCTL_NDIS_REBIND_ADAPTER,
			     multi, len * sizeof(TCHAR), NULL, 0, NULL, NULL))
	{
		wpa_printf(MSG_DEBUG, "NDIS: IOCTL_NDIS_REBIND_ADAPTER "
			   "failed: 0x%x", (int) GetLastError());
		wpa_hexdump_ascii(MSG_DEBUG, "NDIS: rebind multi_sz",
				  (u8 *) multi, len * sizeof(TCHAR));
		CloseHandle(ndis);
		return -1;
	}

	CloseHandle(ndis);

	wpa_printf(MSG_DEBUG, "NDIS: Requested NDIS rebind of NDISUIO "
		   "protocol");

	return 0;
}
#endif /* _WIN32_WCE */


static int wpa_driver_ndis_set_wzc(struct wpa_driver_ndis_data *drv,
				   int enable)
{
#ifdef _WIN32_WCE
	HKEY hk, hk2;
	LONG ret;
	DWORD i, hnd, len;
	TCHAR keyname[256], devname[256];

#define WZC_DRIVER TEXT("Drivers\\BuiltIn\\ZeroConfig")

	if (enable) {
		HANDLE h;
		h = ActivateDeviceEx(WZC_DRIVER, NULL, 0, NULL);
		if (h == INVALID_HANDLE_VALUE || h == 0) {
			wpa_printf(MSG_DEBUG, "NDIS: Failed to re-enable WZC "
				   "- ActivateDeviceEx failed: %d",
				   (int) GetLastError());
			return -1;
		}

		wpa_printf(MSG_DEBUG, "NDIS: WZC re-enabled");
		return wpa_driver_ndis_rebind_adapter(drv);
	}

	/*
	 * Unfortunately, just disabling the WZC for an interface is not enough
	 * to free NDISUIO for us, so need to disable and unload WZC completely
	 * for now when using WinCE with NDISUIO. In addition, must request
	 * NDISUIO protocol to be rebound to the adapter in order to free the
	 * NDISUIO binding that WZC hold before us.
	 */

	/* Enumerate HKLM\Drivers\Active\* to find a handle to WZC. */
	ret = RegOpenKeyEx(HKEY_LOCAL_MACHINE, DEVLOAD_ACTIVE_KEY, 0, 0, &hk);
	if (ret != ERROR_SUCCESS) {
		wpa_printf(MSG_DEBUG, "NDIS: RegOpenKeyEx(DEVLOAD_ACTIVE_KEY) "
			   "failed: %d %d", (int) ret, (int) GetLastError());
		return -1;
	}

	for (i = 0; ; i++) {
		len = sizeof(keyname);
		ret = RegEnumKeyEx(hk, i, keyname, &len, NULL, NULL, NULL,
				   NULL);
		if (ret != ERROR_SUCCESS) {
			wpa_printf(MSG_DEBUG, "NDIS: Could not find active "
				   "WZC - assuming it is not running.");
			RegCloseKey(hk);
			return -1;
		}

		ret = RegOpenKeyEx(hk, keyname, 0, 0, &hk2);
		if (ret != ERROR_SUCCESS) {
			wpa_printf(MSG_DEBUG, "NDIS: RegOpenKeyEx(active dev) "
				   "failed: %d %d",
				   (int) ret, (int) GetLastError());
			continue;
		}

		len = sizeof(devname);
		ret = RegQueryValueEx(hk2, DEVLOAD_DEVKEY_VALNAME, NULL, NULL,
				      (LPBYTE) devname, &len);
		if (ret != ERROR_SUCCESS) {
			wpa_printf(MSG_DEBUG, "NDIS: RegQueryValueEx("
				   "DEVKEY_VALNAME) failed: %d %d",
				   (int) ret, (int) GetLastError());
			RegCloseKey(hk2);
			continue;
		}

		if (_tcscmp(devname, WZC_DRIVER) == 0)
			break;

		RegCloseKey(hk2);
	}

	RegCloseKey(hk);

	/* Found WZC - get handle to it. */
	len = sizeof(hnd);
	ret = RegQueryValueEx(hk2, DEVLOAD_HANDLE_VALNAME, NULL, NULL,
			      (PUCHAR) &hnd, &len);
	if (ret != ERROR_SUCCESS) {
		wpa_printf(MSG_DEBUG, "NDIS: RegQueryValueEx(HANDLE_VALNAME) "
			   "failed: %d %d", (int) ret, (int) GetLastError());
		RegCloseKey(hk2);
		return -1;
	}

	RegCloseKey(hk2);

	/* Deactivate WZC */
	if (!DeactivateDevice((HANDLE) hnd)) {
		wpa_printf(MSG_DEBUG, "NDIS: DeactivateDevice failed: %d",
			   (int) GetLastError());
		return -1;
	}

	wpa_printf(MSG_DEBUG, "NDIS: Disabled WZC temporarily");
	drv->wzc_disabled = 1;
	return wpa_driver_ndis_rebind_adapter(drv);

#else /* _WIN32_WCE */

	HMODULE hm;
	DWORD (WINAPI *wzc_enum_interf)(LPWSTR pSrvAddr,
					PINTFS_KEY_TABLE pIntfs);
	DWORD (WINAPI *wzc_query_interf)(LPWSTR pSrvAddr, DWORD dwInFlags,
					 PINTF_ENTRY pIntf,
					 LPDWORD pdwOutFlags);
	DWORD (WINAPI *wzc_set_interf)(LPWSTR pSrvAddr, DWORD dwInFlags,
				       PINTF_ENTRY pIntf, LPDWORD pdwOutFlags);
	int ret = -1, j;
	DWORD res;
	INTFS_KEY_TABLE guids;
	INTF_ENTRY intf;
	char guid[128];
	WCHAR *pos;
	DWORD flags, i;

	hm = LoadLibrary(TEXT("wzcsapi.dll"));
	if (hm == NULL) {
		wpa_printf(MSG_DEBUG, "NDIS: Failed to load wzcsapi.dll (%u) "
			   "- WZC probably not running",
			   (unsigned int) GetLastError());
		return -1;
	}

#ifdef _WIN32_WCE
	wzc_enum_interf = (void *) GetProcAddressA(hm, "WZCEnumInterfaces");
	wzc_query_interf = (void *) GetProcAddressA(hm, "WZCQueryInterface");
	wzc_set_interf = (void *) GetProcAddressA(hm, "WZCSetInterface");
#else /* _WIN32_WCE */
	wzc_enum_interf = (void *) GetProcAddress(hm, "WZCEnumInterfaces");
	wzc_query_interf = (void *) GetProcAddress(hm, "WZCQueryInterface");
	wzc_set_interf = (void *) GetProcAddress(hm, "WZCSetInterface");
#endif /* _WIN32_WCE */

	if (wzc_enum_interf == NULL || wzc_query_interf == NULL ||
	    wzc_set_interf == NULL) {
		wpa_printf(MSG_DEBUG, "NDIS: WZCEnumInterfaces, "
			   "WZCQueryInterface, or WZCSetInterface not found "
			   "in wzcsapi.dll");
		goto fail;
	}

	os_memset(&guids, 0, sizeof(guids));
	res = wzc_enum_interf(NULL, &guids);
	if (res != 0) {
		wpa_printf(MSG_DEBUG, "NDIS: WZCEnumInterfaces failed: %d; "
			   "WZC service is apparently not running",
			   (int) res);
		goto fail;
	}

	wpa_printf(MSG_DEBUG, "NDIS: WZCEnumInterfaces: %d interfaces",
		   (int) guids.dwNumIntfs);

	for (i = 0; i < guids.dwNumIntfs; i++) {
		pos = guids.pIntfs[i].wszGuid;
		for (j = 0; j < sizeof(guid); j++) {
			guid[j] = (char) *pos;
			if (*pos == 0)
				break;
			pos++;
		}
		guid[sizeof(guid) - 1] = '\0';
		wpa_printf(MSG_DEBUG, "NDIS: intfs %d GUID '%s'",
			   (int) i, guid);
		if (os_strstr(drv->ifname, guid) == NULL)
			continue;

		wpa_printf(MSG_DEBUG, "NDIS: Current interface found from "
			   "WZC");
		break;
	}

	if (i >= guids.dwNumIntfs) {
		wpa_printf(MSG_DEBUG, "NDIS: Current interface not found from "
			   "WZC");
		goto fail;
	}

	os_memset(&intf, 0, sizeof(intf));
	intf.wszGuid = guids.pIntfs[i].wszGuid;
	/* Set flags to verify that the structure has not changed. */
	intf.dwCtlFlags = -1;
	flags = 0;
	res = wzc_query_interf(NULL, INTFCTL_ENABLED, &intf, &flags);
	if (res != 0) {
		wpa_printf(MSG_DEBUG, "NDIS: Could not query flags for the "
			   "WZC interface: %d (0x%x)",
			   (int) res, (int) res);
		wpa_printf(MSG_DEBUG, "NDIS: GetLastError: %u",
			   (unsigned int) GetLastError());
		goto fail;
	}

	wpa_printf(MSG_DEBUG, "NDIS: WZC interface flags 0x%x dwCtlFlags 0x%x",
		   (int) flags, (int) intf.dwCtlFlags);

	if (intf.dwCtlFlags == -1) {
		wpa_printf(MSG_DEBUG, "NDIS: Looks like wzcsapi has changed "
			   "again - could not disable WZC");
		wpa_hexdump(MSG_MSGDUMP, "NDIS: intf",
			    (u8 *) &intf, sizeof(intf));
		goto fail;
	}

	if (enable) {
		if (!(intf.dwCtlFlags & INTFCTL_ENABLED)) {
			wpa_printf(MSG_DEBUG, "NDIS: Enabling WZC for this "
				   "interface");
			intf.dwCtlFlags |= INTFCTL_ENABLED;
			res = wzc_set_interf(NULL, INTFCTL_ENABLED, &intf,
					     &flags);
			if (res != 0) {
				wpa_printf(MSG_DEBUG, "NDIS: Failed to enable "
					   "WZC: %d (0x%x)",
					   (int) res, (int) res);
				wpa_printf(MSG_DEBUG, "NDIS: GetLastError: %u",
					   (unsigned int) GetLastError());
				goto fail;
			}
			wpa_printf(MSG_DEBUG, "NDIS: Re-enabled WZC for this "
				   "interface");
			drv->wzc_disabled = 0;
		}
	} else {
		if (intf.dwCtlFlags & INTFCTL_ENABLED) {
			wpa_printf(MSG_DEBUG, "NDIS: Disabling WZC for this "
				   "interface");
			intf.dwCtlFlags &= ~INTFCTL_ENABLED;
			res = wzc_set_interf(NULL, INTFCTL_ENABLED, &intf,
					     &flags);
			if (res != 0) {
				wpa_printf(MSG_DEBUG, "NDIS: Failed to "
					   "disable WZC: %d (0x%x)",
					   (int) res, (int) res);
				wpa_printf(MSG_DEBUG, "NDIS: GetLastError: %u",
					   (unsigned int) GetLastError());
				goto fail;
			}
			wpa_printf(MSG_DEBUG, "NDIS: Disabled WZC temporarily "
				   "for this interface");
			drv->wzc_disabled = 1;
		} else {
			wpa_printf(MSG_DEBUG, "NDIS: WZC was not enabled for "
				   "this interface");
		}
	}

	ret = 0;

fail:
	FreeLibrary(hm);

	return ret;
#endif /* _WIN32_WCE */
}

#else /* CONFIG_NATIVE_WINDOWS || __CYGWIN__ */

static int wpa_driver_ndis_set_wzc(struct wpa_driver_ndis_data *drv,
				   int enable)
{
	return 0;
}

#endif /* CONFIG_NATIVE_WINDOWS || __CYGWIN__ */


#ifdef CONFIG_USE_NDISUIO
/*
 * l2_packet_ndis.c is sharing the same handle to NDISUIO, so we must be able
 * to export this handle. This is somewhat ugly, but there is no better
 * mechanism available to pass data from driver interface to l2_packet wrapper.
 */
static HANDLE driver_ndis_ndisuio_handle = INVALID_HANDLE_VALUE;

HANDLE driver_ndis_get_ndisuio_handle(void)
{
	return driver_ndis_ndisuio_handle;
}
#endif /* CONFIG_USE_NDISUIO */


static int wpa_driver_ndis_adapter_init(struct wpa_driver_ndis_data *drv)
{
#ifdef CONFIG_USE_NDISUIO
#ifndef _WIN32_WCE
#define NDISUIO_DEVICE_NAME TEXT("\\\\.\\\\Ndisuio")
	DWORD written;
#endif /* _WIN32_WCE */
	drv->ndisuio = CreateFile(NDISUIO_DEVICE_NAME,
				  GENERIC_READ | GENERIC_WRITE, 0, NULL,
				  OPEN_EXISTING,
				  FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
				  INVALID_HANDLE_VALUE);
	if (drv->ndisuio == INVALID_HANDLE_VALUE) {
		wpa_printf(MSG_ERROR, "NDIS: Failed to open connection to "
			   "NDISUIO: %d", (int) GetLastError());
		return -1;
	}
	driver_ndis_ndisuio_handle = drv->ndisuio;

#ifndef _WIN32_WCE
	if (!DeviceIoControl(drv->ndisuio, IOCTL_NDISUIO_BIND_WAIT, NULL, 0,
			     NULL, 0, &written, NULL)) {
		wpa_printf(MSG_ERROR, "NDIS: IOCTL_NDISUIO_BIND_WAIT failed: "
			   "%d", (int) GetLastError());
		CloseHandle(drv->ndisuio);
		drv->ndisuio = INVALID_HANDLE_VALUE;
		return -1;
	}
#endif /* _WIN32_WCE */

	return 0;
#else /* CONFIG_USE_NDISUIO */
	return 0;
#endif /* CONFIG_USE_NDISUIO */
}


static int wpa_driver_ndis_adapter_open(struct wpa_driver_ndis_data *drv)
{
#ifdef CONFIG_USE_NDISUIO
	DWORD written;
#define MAX_NDIS_DEVICE_NAME_LEN 256
	WCHAR ifname[MAX_NDIS_DEVICE_NAME_LEN];
	size_t len, i, pos;
	const char *prefix = "\\DEVICE\\";

#ifdef _WIN32_WCE
	pos = 0;
#else /* _WIN32_WCE */
	pos = 8;
#endif /* _WIN32_WCE */
	len = pos + os_strlen(drv->ifname);
	if (len >= MAX_NDIS_DEVICE_NAME_LEN)
		return -1;
	for (i = 0; i < pos; i++)
		ifname[i] = (WCHAR) prefix[i];
	for (i = pos; i < len; i++)
		ifname[i] = (WCHAR) drv->ifname[i - pos];
	ifname[i] = L'\0';

	if (!DeviceIoControl(drv->ndisuio, IOCTL_NDISUIO_OPEN_DEVICE,
			     ifname, len * sizeof(WCHAR), NULL, 0, &written,
			     NULL)) {
		wpa_printf(MSG_ERROR, "NDIS: IOCTL_NDISUIO_OPEN_DEVICE "
			   "failed: %d", (int) GetLastError());
		wpa_hexdump_ascii(MSG_DEBUG, "NDIS: ifname",
				  (const u8 *) ifname, len * sizeof(WCHAR));
		CloseHandle(drv->ndisuio);
		drv->ndisuio = INVALID_HANDLE_VALUE;
		return -1;
	}

	wpa_printf(MSG_DEBUG, "NDIS: Opened NDISUIO device successfully");

	return 0;
#else /* CONFIG_USE_NDISUIO */
	char ifname[128];
	os_snprintf(ifname, sizeof(ifname), "\\Device\\NPF_%s", drv->ifname);
	drv->adapter = PacketOpenAdapter(ifname);
	if (drv->adapter == NULL) {
		wpa_printf(MSG_DEBUG, "NDIS: PacketOpenAdapter failed for "
			   "'%s'", ifname);
		return -1;
	}
	return 0;
#endif /* CONFIG_USE_NDISUIO */
}


static void wpa_driver_ndis_adapter_close(struct wpa_driver_ndis_data *drv)
{
#ifdef CONFIG_USE_NDISUIO
	driver_ndis_ndisuio_handle = INVALID_HANDLE_VALUE;
	if (drv->ndisuio != INVALID_HANDLE_VALUE)
		CloseHandle(drv->ndisuio);
#else /* CONFIG_USE_NDISUIO */
	if (drv->adapter)
		PacketCloseAdapter(drv->adapter);
#endif /* CONFIG_USE_NDISUIO */
}


static int ndis_add_multicast(struct wpa_driver_ndis_data *drv)
{
	if (ndis_set_oid(drv, OID_802_3_MULTICAST_LIST,
			 (const char *) pae_group_addr, ETH_ALEN) < 0) {
		wpa_printf(MSG_DEBUG, "NDIS: Failed to add PAE group address "
			   "to the multicast list");
		return -1;
	}

	return 0;
}


static void * wpa_driver_ndis_init(void *ctx, const char *ifname)
{
	struct wpa_driver_ndis_data *drv;
	u32 mode;

	drv = os_zalloc(sizeof(*drv));
	if (drv == NULL)
		return NULL;
	drv->ctx = ctx;
	/*
	 * Compatibility code to strip possible prefix from the GUID. Previous
	 * versions include \Device\NPF_ prefix for all names, but the internal
	 * interface name is now only the GUI. Both Packet32 and NDISUIO
	 * prefixes are supported.
	 */
	if (os_strncmp(ifname, "\\Device\\NPF_", 12) == 0)
		ifname += 12;
	else if (os_strncmp(ifname, "\\DEVICE\\", 8) == 0)
		ifname += 8;
	os_strlcpy(drv->ifname, ifname, sizeof(drv->ifname));

	if (wpa_driver_ndis_adapter_init(drv) < 0) {
		os_free(drv);
		return NULL;
	}

	if (wpa_driver_ndis_get_names(drv) < 0) {
		wpa_driver_ndis_adapter_close(drv);
		os_free(drv);
		return NULL;
	}

	wpa_driver_ndis_set_wzc(drv, 0);

	if (wpa_driver_ndis_adapter_open(drv) < 0) {
		wpa_driver_ndis_adapter_close(drv);
		os_free(drv);
		return NULL;
	}

	if (ndis_get_oid(drv, OID_802_3_CURRENT_ADDRESS,
			 (char *) drv->own_addr, ETH_ALEN) < 0) {
		wpa_printf(MSG_DEBUG, "NDIS: Get OID_802_3_CURRENT_ADDRESS "
			   "failed");
		wpa_driver_ndis_adapter_close(drv);
		os_free(drv);
		return NULL;
	}
	wpa_driver_ndis_get_capability(drv);

	/* Make sure that the driver does not have any obsolete PMKID entries.
	 */
	wpa_driver_ndis_flush_pmkid(drv);

	/*
	 * Disconnect to make sure that driver re-associates if it was
	 * connected.
	 */
	wpa_driver_ndis_disconnect(drv);

	eloop_register_timeout(1, 0, wpa_driver_ndis_poll_timeout, drv, NULL);

#ifdef CONFIG_NDIS_EVENTS_INTEGRATED
	drv->events = ndis_events_init(&drv->events_pipe, &drv->event_avail,
				       drv->ifname, drv->adapter_desc);
	if (drv->events == NULL) {
		wpa_driver_ndis_deinit(drv);
		return NULL;
	}
	eloop_register_event(drv->event_avail, sizeof(drv->event_avail),
			     wpa_driver_ndis_event_pipe_cb, drv, NULL);
#endif /* CONFIG_NDIS_EVENTS_INTEGRATED */

#ifdef _WIN32_WCE
	if (ndisuio_notification_init(drv) < 0) {
		wpa_driver_ndis_deinit(drv);
		return NULL;
	}
#endif /* _WIN32_WCE */

	/* Set mode here in case card was configured for ad-hoc mode
	 * previously. */
	mode = Ndis802_11Infrastructure;
	if (ndis_set_oid(drv, OID_802_11_INFRASTRUCTURE_MODE,
			 (char *) &mode, sizeof(mode)) < 0) {
		char buf[8];
		int res;
		wpa_printf(MSG_DEBUG, "NDIS: Failed to set "
			   "OID_802_11_INFRASTRUCTURE_MODE (%d)",
			   (int) mode);
		/* Try to continue anyway */

		res = ndis_get_oid(drv, OID_DOT11_CURRENT_OPERATION_MODE, buf,
				   sizeof(buf));
		if (res > 0) {
			wpa_printf(MSG_INFO, "NDIS: The driver seems to use "
				   "Native 802.11 OIDs. These are not yet "
				   "fully supported.");
			drv->native80211 = 1;
		} else if (!drv->has_capability || drv->capa.enc == 0) {
			/*
			 * Note: This will also happen with NDIS 6 drivers with
			 * Vista.
			 */
			wpa_printf(MSG_DEBUG, "NDIS: Driver did not provide "
				   "any wireless capabilities - assume it is "
				   "a wired interface");
			drv->wired = 1;
			drv->capa.flags |= WPA_DRIVER_FLAGS_WIRED;
			drv->has_capability = 1;
			ndis_add_multicast(drv);
		}
	}

	return drv;
}


static void wpa_driver_ndis_deinit(void *priv)
{
	struct wpa_driver_ndis_data *drv = priv;

#ifdef CONFIG_NDIS_EVENTS_INTEGRATED
	if (drv->events) {
		eloop_unregister_event(drv->event_avail,
				       sizeof(drv->event_avail));
		ndis_events_deinit(drv->events);
	}
#endif /* CONFIG_NDIS_EVENTS_INTEGRATED */

#ifdef _WIN32_WCE
	ndisuio_notification_deinit(drv);
#endif /* _WIN32_WCE */

	eloop_cancel_timeout(wpa_driver_ndis_scan_timeout, drv, drv->ctx);
	eloop_cancel_timeout(wpa_driver_ndis_poll_timeout, drv, NULL);
	wpa_driver_ndis_flush_pmkid(drv);
	wpa_driver_ndis_disconnect(drv);
	if (wpa_driver_ndis_radio_off(drv) < 0) {
		wpa_printf(MSG_DEBUG, "NDIS: failed to disassociate and turn "
			   "radio off");
	}

	wpa_driver_ndis_adapter_close(drv);

	if (drv->wzc_disabled)
		wpa_driver_ndis_set_wzc(drv, 1);

#ifdef _WIN32_WCE
	os_free(drv->adapter_name);
#endif /* _WIN32_WCE */
	os_free(drv->adapter_desc);
	os_free(drv);
}


static struct wpa_interface_info *
wpa_driver_ndis_get_interfaces(void *global_priv)
{
	struct wpa_interface_info *iface = NULL, *niface;

#ifdef CONFIG_USE_NDISUIO
	NDISUIO_QUERY_BINDING *b;
	size_t blen = sizeof(*b) + 1024;
	int i, error;
	DWORD written;
	char name[256], desc[256];
	WCHAR *pos;
	size_t j, len;
	HANDLE ndisuio;

	ndisuio = CreateFile(NDISUIO_DEVICE_NAME,
			     GENERIC_READ | GENERIC_WRITE, 0, NULL,
			     OPEN_EXISTING,
			     FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OVERLAPPED,
			     INVALID_HANDLE_VALUE);
	if (ndisuio == INVALID_HANDLE_VALUE) {
		wpa_printf(MSG_ERROR, "NDIS: Failed to open connection to "
			   "NDISUIO: %d", (int) GetLastError());
		return NULL;
	}

#ifndef _WIN32_WCE
	if (!DeviceIoControl(ndisuio, IOCTL_NDISUIO_BIND_WAIT, NULL, 0,
			     NULL, 0, &written, NULL)) {
		wpa_printf(MSG_ERROR, "NDIS: IOCTL_NDISUIO_BIND_WAIT failed: "
			   "%d", (int) GetLastError());
		CloseHandle(ndisuio);
		return NULL;
	}
#endif /* _WIN32_WCE */

	b = os_malloc(blen);
	if (b == NULL) {
		CloseHandle(ndisuio);
		return NULL;
	}

	for (i = 0; ; i++) {
		os_memset(b, 0, blen);
		b->BindingIndex = i;
		if (!DeviceIoControl(ndisuio, IOCTL_NDISUIO_QUERY_BINDING,
				     b, sizeof(NDISUIO_QUERY_BINDING), b, blen,
				     &written, NULL)) {
			error = (int) GetLastError();
			if (error == ERROR_NO_MORE_ITEMS)
				break;
			wpa_printf(MSG_DEBUG, "IOCTL_NDISUIO_QUERY_BINDING "
				   "failed: %d", error);
			break;
		}

		pos = (WCHAR *) ((char *) b + b->DeviceNameOffset);
		len = b->DeviceNameLength;
		if (len >= sizeof(name))
			len = sizeof(name) - 1;
		for (j = 0; j < len; j++)
			name[j] = (char) pos[j];
		name[len] = '\0';

		pos = (WCHAR *) ((char *) b + b->DeviceDescrOffset);
		len = b->DeviceDescrLength;
		if (len >= sizeof(desc))
			len = sizeof(desc) - 1;
		for (j = 0; j < len; j++)
			desc[j] = (char) pos[j];
		desc[len] = '\0';

		wpa_printf(MSG_DEBUG, "NDIS: %d - %s - %s", i, name, desc);

		niface = os_zalloc(sizeof(*niface));
		if (niface == NULL)
			break;
		niface->drv_name = "ndis";
		if (os_strncmp(name, "\\DEVICE\\", 8) == 0)
			niface->ifname = os_strdup(name + 8);
		else
			niface->ifname = os_strdup(name);
		if (niface->ifname == NULL) {
			os_free(niface);
			break;
		}
		niface->desc = os_strdup(desc);
		niface->next = iface;
		iface = niface;
	}

	os_free(b);
	CloseHandle(ndisuio);
#else /* CONFIG_USE_NDISUIO */
	PTSTR _names;
	char *names, *pos, *pos2;
	ULONG len;
	BOOLEAN res;
	char *name[MAX_ADAPTERS];
	char *desc[MAX_ADAPTERS];
	int num_name, num_desc, i;

	wpa_printf(MSG_DEBUG, "NDIS: Packet.dll version: %s",
		   PacketGetVersion());

	len = 8192;
	_names = os_zalloc(len);
	if (_names == NULL)
		return NULL;

	res = PacketGetAdapterNames(_names, &len);
	if (!res && len > 8192) {
		os_free(_names);
		_names = os_zalloc(len);
		if (_names == NULL)
			return NULL;
		res = PacketGetAdapterNames(_names, &len);
	}

	if (!res) {
		wpa_printf(MSG_ERROR, "NDIS: Failed to get adapter list "
			   "(PacketGetAdapterNames)");
		os_free(_names);
		return NULL;
	}

	names = (char *) _names;
	if (names[0] && names[1] == '\0' && names[2] && names[3] == '\0') {
		wpa_printf(MSG_DEBUG, "NDIS: Looks like adapter names are in "
			   "UNICODE");
		/* Convert to ASCII */
		pos2 = pos = names;
		while (pos2 < names + len) {
			if (pos2[0] == '\0' && pos2[1] == '\0' &&
			    pos2[2] == '\0' && pos2[3] == '\0') {
				pos2 += 4;
				break;
			}
			*pos++ = pos2[0];
			pos2 += 2;
		}
		os_memcpy(pos + 2, names, pos - names);
		pos += 2;
	} else
		pos = names;

	num_name = 0;
	while (pos < names + len) {
		name[num_name] = pos;
		while (*pos && pos < names + len)
			pos++;
		if (pos + 1 >= names + len) {
			os_free(names);
			return NULL;
		}
		pos++;
		num_name++;
		if (num_name >= MAX_ADAPTERS) {
			wpa_printf(MSG_DEBUG, "NDIS: Too many adapters");
			os_free(names);
			return NULL;
		}
		if (*pos == '\0') {
			wpa_printf(MSG_DEBUG, "NDIS: %d adapter names found",
				   num_name);
			pos++;
			break;
		}
	}

	num_desc = 0;
	while (pos < names + len) {
		desc[num_desc] = pos;
		while (*pos && pos < names + len)
			pos++;
		if (pos + 1 >= names + len) {
			os_free(names);
			return NULL;
		}
		pos++;
		num_desc++;
		if (num_desc >= MAX_ADAPTERS) {
			wpa_printf(MSG_DEBUG, "NDIS: Too many adapter "
				   "descriptions");
			os_free(names);
			return NULL;
		}
		if (*pos == '\0') {
			wpa_printf(MSG_DEBUG, "NDIS: %d adapter descriptions "
				   "found", num_name);
			pos++;
			break;
		}
	}

	/*
	 * Windows 98 with Packet.dll 3.0 alpha3 does not include adapter
	 * descriptions. Fill in dummy descriptors to work around this.
	 */
	while (num_desc < num_name)
		desc[num_desc++] = "dummy description";

	if (num_name != num_desc) {
		wpa_printf(MSG_DEBUG, "NDIS: mismatch in adapter name and "
			   "description counts (%d != %d)",
			   num_name, num_desc);
		os_free(names);
		return NULL;
	}

	for (i = 0; i < num_name; i++) {
		niface = os_zalloc(sizeof(*niface));
		if (niface == NULL)
			break;
		niface->drv_name = "ndis";
		if (os_strncmp(name[i], "\\Device\\NPF_", 12) == 0)
			niface->ifname = os_strdup(name[i] + 12);
		else
			niface->ifname = os_strdup(name[i]);
		if (niface->ifname == NULL) {
			os_free(niface);
			break;
		}
		niface->desc = os_strdup(desc[i]);
		niface->next = iface;
		iface = niface;
	}

#endif /* CONFIG_USE_NDISUIO */

	return iface;
}


const struct wpa_driver_ops wpa_driver_ndis_ops = {
	"ndis",
	"Windows NDIS driver",
	wpa_driver_ndis_get_bssid,
	wpa_driver_ndis_get_ssid,
	wpa_driver_ndis_set_key,
	wpa_driver_ndis_init,
	wpa_driver_ndis_deinit,
	NULL /* set_param */,
	NULL /* set_countermeasures */,
	wpa_driver_ndis_deauthenticate,
	wpa_driver_ndis_disassociate,
	wpa_driver_ndis_associate,
	wpa_driver_ndis_add_pmkid,
	wpa_driver_ndis_remove_pmkid,
	wpa_driver_ndis_flush_pmkid,
	wpa_driver_ndis_get_capa,
	wpa_driver_ndis_poll,
	wpa_driver_ndis_get_ifname,
	wpa_driver_ndis_get_mac_addr,
	NULL /* send_eapol */,
	NULL /* set_operstate */,
	NULL /* mlme_setprotection */,
	NULL /* get_hw_feature_data */,
	NULL /* set_channel */,
	NULL /* set_ssid */,
	NULL /* set_bssid */,
	NULL /* send_mlme */,
	NULL /* mlme_add_sta */,
	NULL /* mlme_remove_sta */,
	NULL /* update_ft_ies */,
	NULL /* send_ft_action */,
	wpa_driver_ndis_get_scan_results,
	NULL /* set_country */,
	NULL /* global_init */,
	NULL /* global_deinit */,
	NULL /* init2 */,
	wpa_driver_ndis_get_interfaces,
	wpa_driver_ndis_scan,
	NULL /* authenticate */,
	NULL /* set_beacon */,
	NULL /* hapd_init */,
	NULL /* hapd_deinit */,
	NULL /* set_ieee8021x */,
	NULL /* set_privacy */,
	NULL /* get_seqnum */,
	NULL /* flush */,
	NULL /* set_generic_elem */,
	NULL /* read_sta_data */,
	NULL /* hapd_send_eapol */,
	NULL /* sta_deauth */,
	NULL /* sta_disassoc */,
	NULL /* sta_remove */,
	NULL /* hapd_get_ssid */,
	NULL /* hapd_set_ssid */,
	NULL /* hapd_set_countermeasures */,
	NULL /* sta_add */,
	NULL /* get_inact_sec */,
	NULL /* sta_clear_stats */,
	NULL /* set_freq */,
	NULL /* set_rts */,
	NULL /* set_frag */,
	NULL /* sta_set_flags */,
	NULL /* set_rate_sets */,
	NULL /* set_cts_protect */,
	NULL /* set_preamble */,
	NULL /* set_short_slot_time */,
	NULL /* set_tx_queue_params */,
	NULL /* valid_bss_mask */,
	NULL /* if_add */,
	NULL /* if_remove */,
	NULL /* set_sta_vlan */,
	NULL /* commit */,
	NULL /* send_ether */,
	NULL /* set_radius_acl_auth */,
	NULL /* set_radius_acl_expire */,
	NULL /* set_ht_params */,
	NULL /* set_ap_wps_ie */,
	NULL /* set_supp_port */,
	NULL /* set_wds_sta */,
	NULL /* send_action */,
	NULL /* send_action_cancel_wait */,
	NULL /* remain_on_channel */,
	NULL /* cancel_remain_on_channel */,
	NULL /* probe_req_report */,
	NULL /* disable_11b_rates */,
	NULL /* deinit_ap */,
	NULL /* suspend */,
	NULL /* resume */,
	NULL /* signal_monitor */,
	NULL /* send_frame */,
	NULL /* shared_freq */,
	NULL /* get_noa */,
	NULL /* set_noa */,
	NULL /* set_p2p_powersave */,
	NULL /* ampdu */,
	NULL /* set_intra_bss */,
	NULL /* get_radio_name */,
	NULL /* p2p_find */,
	NULL /* p2p_stop_find */,
	NULL /* p2p_listen */,
	NULL /* p2p_connect */,
	NULL /* wps_success_cb */,
	NULL /* p2p_group_formation_failed */,
	NULL /* p2p_set_params */,
	NULL /* p2p_prov_disc_req */,
	NULL /* p2p_sd_request */,
	NULL /* p2p_sd_cancel_request */,
	NULL /* p2p_sd_response */,
	NULL /* p2p_service_update */,
	NULL /* p2p_reject */,
	NULL /* p2p_invite */,
	NULL /* send_tdls_mgmt */,
	NULL /* tdls_oper */,
	NULL /* signal_poll */
};
