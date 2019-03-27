/*
 * binder interface for wpa_supplicant daemon
 * Copyright (c) 2004-2016, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2004-2016, Roshan Pius <rpius@google.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include "supplicant.h"
#include "binder_manager.h"

namespace wpa_supplicant_binder {

Supplicant::Supplicant(struct wpa_global *global) : wpa_global_(global) {}

android::binder::Status Supplicant::CreateInterface(
    const android::os::PersistableBundle &params,
    android::sp<fi::w1::wpa_supplicant::IIface> *aidl_return)
{
	android::String16 driver, ifname, confname, bridge_ifname;

	/* Check if required Ifname argument is missing */
	if (!params.getString(android::String16("Ifname"), &ifname))
		return android::binder::Status::fromServiceSpecificError(
		    ERROR_INVALID_ARGS,
		    android::String8("Ifname missing in params."));
	/* Retrieve the remaining params from the dictionary */
	params.getString(android::String16("Driver"), &driver);
	params.getString(android::String16("ConfigFile"), &confname);
	params.getString(android::String16("BridgeIfname"), &bridge_ifname);

	/*
	 * Try to get the wpa_supplicant record for this iface, return
	 * an error if we already control it.
	 */
	if (wpa_supplicant_get_iface(
		wpa_global_, android::String8(ifname).string()) != NULL)
		return android::binder::Status::fromServiceSpecificError(
		    ERROR_IFACE_EXISTS,
		    android::String8("wpa_supplicant already controls this "
				     "interface."));

	android::binder::Status status;
	struct wpa_supplicant *wpa_s = NULL;
	struct wpa_interface iface;

	os_memset(&iface, 0, sizeof(iface));
	iface.driver = os_strdup(android::String8(driver).string());
	iface.ifname = os_strdup(android::String8(ifname).string());
	iface.confname = os_strdup(android::String8(confname).string());
	iface.bridge_ifname =
	    os_strdup(android::String8(bridge_ifname).string());
	/* Otherwise, have wpa_supplicant attach to it. */
	wpa_s = wpa_supplicant_add_iface(wpa_global_, &iface, NULL);
	/* The supplicant core creates a corresponding binder object via
	 * BinderManager when |wpa_supplicant_add_iface| is called. */
	if (!wpa_s || !wpa_s->binder_object_key) {
		status = android::binder::Status::fromServiceSpecificError(
		    ERROR_UNKNOWN,
		    android::String8(
			"wpa_supplicant couldn't grab this interface."));
	} else {
		BinderManager *binder_manager = BinderManager::getInstance();

		if (!binder_manager ||
		    binder_manager->getIfaceBinderObjectByKey(
			wpa_s->binder_object_key, aidl_return))
			status =
			    android::binder::Status::fromServiceSpecificError(
				ERROR_UNKNOWN,
				android::String8("wpa_supplicant encountered a "
						 "binder error."));
		else
			status = android::binder::Status::ok();
	}
	os_free((void *)iface.driver);
	os_free((void *)iface.ifname);
	os_free((void *)iface.confname);
	os_free((void *)iface.bridge_ifname);
	return status;
}

android::binder::Status Supplicant::RemoveInterface(const std::string &ifname)
{
	struct wpa_supplicant *wpa_s;

	wpa_s = wpa_supplicant_get_iface(wpa_global_, ifname.c_str());
	if (!wpa_s || !wpa_s->binder_object_key)
		return android::binder::Status::fromServiceSpecificError(
		    ERROR_IFACE_UNKNOWN,
		    android::String8("wpa_supplicant does not control this "
				     "interface."));
	if (wpa_supplicant_remove_iface(wpa_global_, wpa_s, 0))
		return android::binder::Status::fromServiceSpecificError(
		    ERROR_UNKNOWN,
		    android::String8(
			"wpa_supplicant couldn't remove this interface."));
	return android::binder::Status::ok();
}

android::binder::Status Supplicant::GetInterface(
    const std::string &ifname,
    android::sp<fi::w1::wpa_supplicant::IIface> *aidl_return)
{
	struct wpa_supplicant *wpa_s;

	wpa_s = wpa_supplicant_get_iface(wpa_global_, ifname.c_str());
	if (!wpa_s || !wpa_s->binder_object_key)
		return android::binder::Status::fromServiceSpecificError(
		    ERROR_IFACE_UNKNOWN,
		    android::String8(
			"wpa_supplicant does not control this interface."));

	BinderManager *binder_manager = BinderManager::getInstance();
	if (!binder_manager ||
	    binder_manager->getIfaceBinderObjectByKey(
		wpa_s->binder_object_key, aidl_return))
		return android::binder::Status::fromServiceSpecificError(
		    ERROR_UNKNOWN,
		    android::String8(
			"wpa_supplicant encountered a binder error."));

	return android::binder::Status::ok();
}

} /* namespace wpa_supplicant_binder */
