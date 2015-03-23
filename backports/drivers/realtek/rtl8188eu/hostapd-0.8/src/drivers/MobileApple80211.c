#include "includes.h"
#include <dlfcn.h>

#include "common.h"

#include <CoreFoundation/CoreFoundation.h>
#include "MobileApple80211.h"

/*
 * Code for dynamically loading Apple80211 functions from Aeropuerto to avoid
 * having to link with full Preferences.framework.
 */

static void *aeropuerto = NULL;


int _Apple80211Initialized(void)
{
	return aeropuerto ? 1 : 0;
}


static int (*__Apple80211Open)(Apple80211Ref *ctx) = NULL;

int Apple80211Open(Apple80211Ref *ctx)
{
	return __Apple80211Open(ctx);
}


static int (*__Apple80211Close)(Apple80211Ref ctx) = NULL;

int Apple80211Close(Apple80211Ref ctx)
{
	return __Apple80211Close(ctx);
}


static int (*__Apple80211GetIfListCopy)(Apple80211Ref handle, CFArrayRef *list)
	= NULL;

int Apple80211GetIfListCopy(Apple80211Ref handle, CFArrayRef *list)
{
	return __Apple80211GetIfListCopy(handle, list);
}


static int (*__Apple80211BindToInterface)(Apple80211Ref handle,
					  CFStringRef interface) = NULL;

int Apple80211BindToInterface(Apple80211Ref handle,
			      CFStringRef interface)
{
	return __Apple80211BindToInterface(handle, interface);
}


static int (*__Apple80211GetInterfaceNameCopy)(Apple80211Ref handle,
					       CFStringRef *name) = NULL;

int Apple80211GetInterfaceNameCopy(Apple80211Ref handle,
				   CFStringRef *name)
{
	return __Apple80211GetInterfaceNameCopy(handle, name);
}


static int (*__Apple80211GetInfoCopy)(Apple80211Ref handle,
				      CFDictionaryRef *info) = NULL;

int Apple80211GetInfoCopy(Apple80211Ref handle,
			  CFDictionaryRef *info)
{
	return __Apple80211GetInfoCopy(handle, info);
}


static int (*__Apple80211GetPower)(Apple80211Ref handle, char *pwr) = NULL;

int Apple80211GetPower(Apple80211Ref handle, char *pwr)
{
	return __Apple80211GetPower(handle, pwr);
}


static int (*__Apple80211SetPower)(Apple80211Ref handle, char pwr) = NULL;

int Apple80211SetPower(Apple80211Ref handle, char pwr)
{
	return __Apple80211SetPower(handle, pwr);
}


static int (*__Apple80211Scan)(Apple80211Ref handle, CFArrayRef *list,
			       CFDictionaryRef parameters) = NULL;

int Apple80211Scan(Apple80211Ref handle, CFArrayRef *list,
		   CFDictionaryRef parameters)
{
	return __Apple80211Scan(handle, list, parameters);
}


static int (*__Apple80211Associate)(Apple80211Ref handle, CFDictionaryRef bss,
				    CFStringRef password) = NULL;

int Apple80211Associate(Apple80211Ref handle, CFDictionaryRef bss,
			CFStringRef password)
{
	return __Apple80211Associate(handle, bss, password);
}


static int (*__Apple80211AssociateAndCopyInfo)(Apple80211Ref handle,
					       CFDictionaryRef bss,
					       CFStringRef password,
					       CFDictionaryRef *info) =
	NULL;

int Apple80211AssociateAndCopyInfo(Apple80211Ref handle, CFDictionaryRef bss,
				   CFStringRef password, CFDictionaryRef *info)
{
	return __Apple80211AssociateAndCopyInfo(handle, bss, password, info);
}


static int (*__Apple80211CopyValue)(Apple80211Ref handle, int field,
				    CFDictionaryRef arg2, void *value) = NULL;

int Apple80211CopyValue(Apple80211Ref handle, int field, CFDictionaryRef arg2,
			void *value)
{
	return __Apple80211CopyValue(handle, field, arg2, value);
}


#define DLSYM(s) \
do { \
	__ ## s = dlsym(aeropuerto, #s); \
	if (__ ## s == NULL) { \
		wpa_printf(MSG_ERROR, "MobileApple80211: Could not resolve " \
			   "symbol '" #s "' (%s)", dlerror()); \
		err = 1; \
	} \
} while (0)


__attribute__ ((constructor))
void _Apple80211_constructor(void)
{
	const char *fname = "/System/Library/SystemConfiguration/"
		"Aeropuerto.bundle/Aeropuerto";
	int err = 0;

	aeropuerto = dlopen(fname, RTLD_LAZY);
	if (!aeropuerto) {
		wpa_printf(MSG_ERROR, "MobileApple80211: Failed to open %s "
			   "for symbols", fname);
		return;
	}

	DLSYM(Apple80211Open);
	DLSYM(Apple80211Close);
	DLSYM(Apple80211GetIfListCopy);
	DLSYM(Apple80211BindToInterface);
	DLSYM(Apple80211GetInterfaceNameCopy);
	DLSYM(Apple80211GetInfoCopy);
	DLSYM(Apple80211GetPower);
	DLSYM(Apple80211SetPower);
	DLSYM(Apple80211Scan);
	DLSYM(Apple80211Associate);
	DLSYM(Apple80211AssociateAndCopyInfo);
	DLSYM(Apple80211CopyValue);

	if (err) {
		dlclose(aeropuerto);
		aeropuerto = NULL;
	}
}


__attribute__ ((destructor))
void _Apple80211_destructor(void)
{
	if (aeropuerto) {
		dlclose(aeropuerto);
		aeropuerto = NULL;
	}
}
