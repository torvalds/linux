/*
 * WPA Supplicant - binder interface for wpa_supplicant daemon
 * Copyright (c) 2004-2016, Jouni Malinen <j@w1.fi>
 * Copyright (c) 2004-2016, Roshan Pius <rpius@google.com>
 *
 * This software may be distributed under the terms of the BSD license.
 * See README for more details.
 */

package fi.w1.wpa_supplicant;

import android.os.PersistableBundle;
import fi.w1.wpa_supplicant.IIface;

/**
 * Interface exposed by the wpa_supplicant binder service registered
 * with the service manager with name: fi.w1.wpa_supplicant.
 */
interface ISupplicant {
	/* Error values returned by the service to RPC method calls. */
	const int ERROR_INVALID_ARGS = 1;
	const int ERROR_UNKNOWN = 2;
	const int ERROR_IFACE_EXISTS = 3;
	const int ERROR_IFACE_UNKNOWN = 4;

	/**
	 * Registers a wireless interface in wpa_supplicant.
	 *
	 * @param args A dictionary with arguments used to add the interface to
	 *             wpa_supplicant.
	 * The dictionary may contain the following entries:
	 *   Ifname(String) Name of the network interface to control, e.g.,
	 *   wlan0.
	 *   BridgeIfname(String) Name of the bridge interface to control, e.g.,
	 *   br0.
	 *   Driver(String) Driver name which the interface uses, e.g., nl80211.
	 *   ConfigFile(String) Configuration file path.
	 *
	 * @return Binder object representing the interface.
	 */
	IIface CreateInterface(in PersistableBundle args);

	/**
	 * Deregisters a wireless interface from wpa_supplicant.
	 *
	 * @param ifname Name of the network interface, e.g., wlan0
	 */
	void RemoveInterface(in @utf8InCpp String ifname);

	/**
	 * Gets a binder object for the interface corresponding to ifname
	 * which wpa_supplicant already controls.
	 *
	 * @param ifname Name of the network interface, e.g., wlan0
	 *
	 * @return Binder object representing the interface.
	 */
	IIface GetInterface(in @utf8InCpp String ifname);
}
