#!/usr/bin/python

import dbus
import sys, os
import time
import gobject
from dbus.mainloop.glib import DBusGMainLoop

WPAS_DBUS_SERVICE = "fi.w1.wpa_supplicant1"
WPAS_DBUS_INTERFACE = "fi.w1.wpa_supplicant1"
WPAS_DBUS_OPATH = "/fi/w1/wpa_supplicant1"

WPAS_DBUS_INTERFACES_INTERFACE = "fi.w1.wpa_supplicant1.Interface"
WPAS_DBUS_WPS_INTERFACE = "fi.w1.wpa_supplicant1.Interface.WPS"

def propertiesChanged(properties):
	if properties.has_key("State"):
		print "PropertiesChanged: State: %s" % (properties["State"])

def scanDone(success):
	print "Scan done: success=%s" % success

def bssAdded(bss, properties):
	print "BSS added: %s" % (bss)

def bssRemoved(bss):
	print "BSS removed: %s" % (bss)

def wpsEvent(name, args):
	print "WPS event: %s" % (name)
	print args

def credentials(cred):
	print "WPS credentials: %s" % (cred)

def main():
	dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
	global bus
	bus = dbus.SystemBus()
	wpas_obj = bus.get_object(WPAS_DBUS_SERVICE, WPAS_DBUS_OPATH)

	if len(sys.argv) != 2:
		print "Missing ifname argument"
		os._exit(1)

	wpas = dbus.Interface(wpas_obj, WPAS_DBUS_INTERFACE)
	bus.add_signal_receiver(scanDone,
				dbus_interface=WPAS_DBUS_INTERFACES_INTERFACE,
				signal_name="ScanDone")
	bus.add_signal_receiver(bssAdded,
				dbus_interface=WPAS_DBUS_INTERFACES_INTERFACE,
				signal_name="BSSAdded")
	bus.add_signal_receiver(bssRemoved,
				dbus_interface=WPAS_DBUS_INTERFACES_INTERFACE,
				signal_name="BSSRemoved")
	bus.add_signal_receiver(propertiesChanged,
				dbus_interface=WPAS_DBUS_INTERFACES_INTERFACE,
				signal_name="PropertiesChanged")
	bus.add_signal_receiver(wpsEvent,
				dbus_interface=WPAS_DBUS_WPS_INTERFACE,
				signal_name="Event")
	bus.add_signal_receiver(credentials,
				dbus_interface=WPAS_DBUS_WPS_INTERFACE,
				signal_name="Credentials")

	ifname = sys.argv[1]

	path = wpas.GetInterface(ifname)
	if_obj = bus.get_object(WPAS_DBUS_SERVICE, path)
	if_obj.Set(WPAS_DBUS_WPS_INTERFACE, 'ProcessCredentials',
		   dbus.Boolean(1),
		   dbus_interface=dbus.PROPERTIES_IFACE)
	wps = dbus.Interface(if_obj, WPAS_DBUS_WPS_INTERFACE)
	wps.Start({'Role': 'enrollee', 'Type': 'pbc'})

	gobject.MainLoop().run()

if __name__ == "__main__":
	main()

