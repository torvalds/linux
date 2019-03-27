/*
 * binder interface for wpa_supplicant daemon
 * Copyright (c) 2004-2016, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2004-2016, Roshan Pius <rpius@google.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

#ifndef WPA_SUPPLICANT_BINDER_SUPPLICANT_H
#define WPA_SUPPLICANT_BINDER_SUPPLICANT_H

#include "fi/w1/wpa_supplicant/BnSupplicant.h"
#include "fi/w1/wpa_supplicant/IIface.h"
#include "fi/w1/wpa_supplicant/ISupplicantCallbacks.h"

extern "C" {
#include "utils/common.h"
#include "utils/includes.h"
#include "../wpa_supplicant_i.h"
}

namespace wpa_supplicant_binder {

/**
 * Implementation of the supplicant binder object. This binder
 * object is used core for global control operations on
 * wpa_supplicant.
 */
class Supplicant : public fi::w1::wpa_supplicant::BnSupplicant
{
public:
	Supplicant(struct wpa_global *global);
	virtual ~Supplicant() = default;

	android::binder::Status CreateInterface(
	    const android::os::PersistableBundle &params,
	    android::sp<fi::w1::wpa_supplicant::IIface> *aidl_return) override;
	android::binder::Status
	RemoveInterface(const std::string &ifname) override;
	android::binder::Status GetInterface(
	    const std::string &ifname,
	    android::sp<fi::w1::wpa_supplicant::IIface> *aidl_return) override;

private:
	/* Raw pointer to the global structure maintained by the core. */
	struct wpa_global *wpa_global_;
	/* All the callback objects registered by the clients. */
	std::vector<android::sp<fi::w1::wpa_supplicant::ISupplicantCallbacks>>
	    callbacks_;
};

} /* namespace wpa_supplicant_binder */

#endif /* WPA_SUPPLICANT_BINDER_SUPPLICANT_H */
