#ifndef MOBILEAPPLE80211_H
#define MOBILEAPPLE80211_H

/*
 * MobileApple80211 interface for iPhone/iPod touch
 * These functions are available from Aeropuerto.
 */

struct Apple80211;
typedef struct Apple80211 *Apple80211Ref;

int Apple80211Open(Apple80211Ref *ctx);
int Apple80211Close(Apple80211Ref ctx);
int Apple80211GetIfListCopy(Apple80211Ref handle, CFArrayRef *list);
int Apple80211BindToInterface(Apple80211Ref handle,
			      CFStringRef interface);
int Apple80211GetInterfaceNameCopy(Apple80211Ref handle,
				   CFStringRef *name);
int Apple80211GetInfoCopy(Apple80211Ref handle,
			  CFDictionaryRef *info);
int Apple80211GetPower(Apple80211Ref handle, char *pwr);
int Apple80211SetPower(Apple80211Ref handle, char pwr);

/* parameters can be NULL; returns scan results in CFArrayRef *list;
 * caller will need to free with CFRelease() */
int Apple80211Scan(Apple80211Ref handle, CFArrayRef *list,
		   CFDictionaryRef parameters);

int Apple80211Associate(Apple80211Ref handle, CFDictionaryRef bss,
			CFStringRef password);
int Apple80211AssociateAndCopyInfo(Apple80211Ref handle, CFDictionaryRef bss,
				   CFStringRef password,
				   CFDictionaryRef *info);

enum {
	APPLE80211_VALUE_SSID = 1,
	APPLE80211_VALUE_BSSID = 9
};

int Apple80211CopyValue(Apple80211Ref handle, int field, CFDictionaryRef arg2,
			void *value);

#endif /* MOBILEAPPLE80211_H */
