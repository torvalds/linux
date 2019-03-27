/*
 * binder interface for wpa_supplicant daemon
 * Copyright (c) 2004-2016, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2004-2016, Roshan Pius <rpius@google.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#include <binder/IServiceManager.h>

#include "binder_constants.h"
#include "binder_manager.h"

extern "C" {
#include "utils/common.h"
#include "utils/includes.h"
}

namespace wpa_supplicant_binder {

BinderManager *BinderManager::instance_ = NULL;

BinderManager *BinderManager::getInstance()
{
	if (!instance_)
		instance_ = new BinderManager();
	return instance_;
}

void BinderManager::destroyInstance()
{
	if (instance_)
		delete instance_;
	instance_ = NULL;
}

int BinderManager::registerBinderService(struct wpa_global *global)
{
	/* Create the main binder service object and register with
	 * system service manager. */
	supplicant_object_ = new Supplicant(global);
	android::String16 service_name(binder_constants::kServiceName);
	android::defaultServiceManager()->addService(
	    service_name, android::IInterface::asBinder(supplicant_object_));
	return 0;
}

int BinderManager::registerInterface(struct wpa_supplicant *wpa_s)
{
	if (!wpa_s)
		return 1;

	/* Using the corresponding wpa_supplicant pointer as key to our
	 * object map. */
	const void *iface_key = wpa_s;

	/* Return failure if we already have an object for that iface_key. */
	if (iface_object_map_.find(iface_key) != iface_object_map_.end())
		return 1;

	iface_object_map_[iface_key] = new Iface(wpa_s);
	if (!iface_object_map_[iface_key].get())
		return 1;

	wpa_s->binder_object_key = iface_key;

	return 0;
}

int BinderManager::unregisterInterface(struct wpa_supplicant *wpa_s)
{
	if (!wpa_s || !wpa_s->binder_object_key)
		return 1;

	const void *iface_key = wpa_s;
	if (iface_object_map_.find(iface_key) == iface_object_map_.end())
		return 1;

	/* Delete the corresponding iface object from our map. */
	iface_object_map_.erase(iface_key);
	wpa_s->binder_object_key = NULL;
	return 0;
}

int BinderManager::getIfaceBinderObjectByKey(
    const void *iface_object_key,
    android::sp<fi::w1::wpa_supplicant::IIface> *iface_object)
{
	if (!iface_object_key || !iface_object)
		return 1;

	if (iface_object_map_.find(iface_object_key) == iface_object_map_.end())
		return 1;

	*iface_object = iface_object_map_[iface_object_key];
	return 0;
}

} /* namespace wpa_supplicant_binder */
