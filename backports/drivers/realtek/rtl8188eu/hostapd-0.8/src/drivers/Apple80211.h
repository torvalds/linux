#ifndef APPLE80211_H
#define APPLE80211_H

/*
 * Apple80211 framework definitions
 * This is an undocumented interface and the definitions here are based on
 * information from MacStumbler (http://www.macstumbler.com/Apple80211.h) and
 * whatever related information can be found with google and experiments ;-).
 */

typedef struct __WirelessRef *WirelessRef;
typedef SInt32 WirelessError;
#define errWirelessNoError 0

typedef struct WirelessInfo {
	UInt16 link_qual;
	UInt16 comms_qual;
	UInt16 signal;
	UInt16 noise;
	UInt16 port_stat;
	UInt16 client_mode;
	UInt16 res1;
	UInt16 power;
	UInt16 res2;
	UInt8 bssID[6];
	UInt8 ssid[34];
} WirelessInfo;

typedef struct WirelessInfo2 {
	/* TODO - these are probably not in correct order or complete */
	WirelessInfo info1;
	UInt8 macAddress[6];
} WirelessInfo2;

typedef struct WirelessNetworkInfo {
	UInt16 channel;
	UInt16 noise;
	UInt16 signal;
	UInt8 bssid[6];
	UInt16 beacon_int;
	UInt16 capability;
	UInt16 ssid_len;
	UInt8 ssid[32];
} WirelessNetworkInfo;

typedef int wirelessKeyType; /* TODO */

int WirelessIsAvailable(void);
WirelessError WirelessAttach(WirelessRef *ref, UInt32 res);
WirelessError WirelessDetach(WirelessRef ref);
WirelessError WirelessPrivate(WirelessRef ref, void *in_ptr, int in_bytes,
			      void *out_ptr, int out_bytes);
WirelessError WirelessSetEnabled(WirelessRef ref, UInt8 enabled);
WirelessError WirelessGetEnabled(WirelessRef ref, UInt8 *enabled);
WirelessError WirelessSetPower(WirelessRef ref, UInt8 power);
WirelessError WirelessGetPower(WirelessRef ref, UInt8 *power);
WirelessError WirelessGetInfo(WirelessRef ref, WirelessInfo *info);
WirelessError WirelessGetInfo2(WirelessRef ref, WirelessInfo2 *info);
WirelessError WirelessScan(WirelessRef ref, CFArrayRef *results,
			   UInt32 strip_dups);
WirelessError WirelessScanSplit(WirelessRef ref, CFArrayRef *ap_results,
				CFArrayRef *ibss_results, UInt32 strip_dups);
WirelessError WirelessDirectedScan(WirelessRef ref, CFArrayRef *results,
				   UInt32 strip_dups, CFStringRef ssid);
WirelessError WirelessDirectedScan2(WirelessRef ref, CFDataRef ssid,
				    UInt32 strip_dups, CFArrayRef *results);
WirelessError WirelessJoin(WirelessRef ref, CFStringRef ssid);
WirelessError WirelessJoinWEP(WirelessRef ref, CFStringRef ssid,
			      CFStringRef passwd);
WirelessError WirelessJoin8021x(WirelessRef ref, CFStringRef ssid);
/*
 * Set WEP key
 * ref: wireless reference from WirelessAttach()
 * type: ?
 * key_idx: 0..3
 * key_len: 13 for WEP-104 or 0 for clearing the key
 * key: Pointer to the key or %NULL if key_len = 0
 */
WirelessError WirelessSetKey(WirelessRef ref, wirelessKeyType type,
			     int key_idx, int key_len,
			     const unsigned char *key);
/*
 * Set WPA key (e.g., PMK for 4-way handshake)
 * ref: wireless reference from WirelessAttach()
 * type: 0..4; 1 = PMK
 * key_len: 16, 32, or 0
 * key: Pointer to the key or %NULL if key_len = 0
 */
WirelessError WirelessSetWPAKey(WirelessRef ref, wirelessKeyType type,
				int key_len, const unsigned char *key);
WirelessError WirelessAssociate(WirelessRef ref, int type, CFDataRef ssid,
				CFStringRef key);
WirelessError WirelessAssociate2(WirelessRef ref, CFDictionaryRef scan_res,
				 CFStringRef key);
WirelessError WirelessDisassociate(WirelessRef ref);

/*
 * Get a copy of scan results for the given SSID
 * The returned dictionary includes following entries:
 * beaconInterval: CFNumber(kCFNumberSInt32Type)
 * SSID: CFData buffer of the SSID
 * isWPA: CFNumber(kCFNumberSInt32Type); 0 = not used, 1 = WPA, -128 = WPA2
 * name: Name of the network (SSID string)
 * BSSID: CFData buffer of the BSSID
 * channel: CFNumber(kCFNumberSInt32Type)
 * signal: CFNumber(kCFNumberSInt32Type)
 * appleIE: CFData
 * WPSNOPINRequired: CFBoolean
 * noise: CFNumber(kCFNumberSInt32Type)
 * capability: CFNumber(kCFNumberSInt32Type)
 * uniCipher: CFArray of CFNumber(kCFNumberSInt32Type)
 * appleIE_Version: CFNumber(kCFNumberSInt32Type)
 * appleIE_Robust: CFBoolean
 * WPSConfigured: CFBoolean
 * scanWasDirected: CFBoolean
 * appleIE_Product: CFNumber(kCFNumberSInt32Type)
 * authModes: CFArray of CFNumber(kCFNumberSInt32Type)
 * multiCipher: CFNumber(kCFNumberSInt32Type)
 */
CFDictionaryRef WirelessSafeDirectedScanCopy(WirelessRef ref, CFDataRef ssid);

/*
 * Get information about the current association
 * The returned dictionary includes following entries:
 * keyData: CFData buffer of the key (e.g., 32-octet PSK)
 * multiCipher: CFNumber(kCFNumberSInt32Type); 0 = none, 5 = CCMP?
 * channel: CFNumber(kCFNumberSInt32Type)
 * isIBSS: CFBoolean
 * authMode: CFNumber(kCFNumberSInt32Type); 2 = WPA-Personal; 3 = open,
 *	129 = WPA2-Enterprise
 * isWPA: CFNumber(kCFNumberSInt32Type); 0 = not used, 1 = WPA, -128 == WPA2
 * SSID: CFData buffer of the SSID
 * cipherMode: CFNumber(kCFNumberSInt32Type); 0 = none, 4 = CCMP?
 */
CFDictionaryRef WirelessGetAssociationInfo(WirelessRef ref);

WirelessError WirelessConfigure(WirelessRef ref);

/*
 * Get ASP information
 * The returned dictionary includes following entries:
 * Version: version number (e.g., 3.0)
 * Channel: channel (e.g., 1)
 * Vendor: vendor (e.g., 2)
 */
CFDictionaryRef WirelessGetInfoASP(void);

/*
 * Get a copy of the interface dictionary
 * The returned dictionary has a key,value pairs for wireless interfaces.
 * The key is the interface name and the value is the driver identifier, e.g.,
 * en1: com.apple.driver.AirPort.Atheros
 */
CFDictionaryRef WirelessCopyInterfaceDict(void);

#endif /* APPLE80211_H */
