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
WPAS_DBUS_INTERFACES_OPATH = "/fi/w1/wpa_supplicant1/Interfaces"
WPAS_DBUS_BSS_INTERFACE = "fi.w1.wpa_supplicant1.BSS"

def byte_array_to_string(s):
	import urllib
	r = ""    
	for c in s:
		if c >= 32 and c < 127:
			r += "%c" % c
		else:
			r += urllib.quote(chr(c))
	return r

def list_interfaces(wpas_obj):
	ifaces = wpas_obj.Get(WPAS_DBUS_INTERFACE, 'Interfaces',
			      dbus_interface=dbus.PROPERTIES_IFACE)
	for path in ifaces:
		if_obj = bus.get_object(WPAS_DBUS_SERVICE, path)
		ifname = if_obj.Get(WPAS_DBUS_INTERFACES_INTERFACE, 'Ifname',
			      dbus_interface=dbus.PROPERTIES_IFACE)
		print ifname

def propertiesChanged(properties):
	if properties.has_key("State"):
		print "PropertiesChanged: State: %s" % (properties["State"])

def showBss(bss):
	net_obj = bus.get_object(WPAS_DBUS_SERVICE, bss)
	net = dbus.Interface(net_obj, WPAS_DBUS_BSS_INTERFACE)

	# Convert the byte-array for SSID and BSSID to printable strings
	val = net_obj.Get(WPAS_DBUS_BSS_INTERFACE, 'BSSID',
			  dbus_interface=dbus.PROPERTIES_IFACE)
	bssid = ""
	for item in val:
		bssid = bssid + ":%02x" % item
	bssid = bssid[1:]
	val = net_obj.Get(WPAS_DBUS_BSS_INTERFACE, 'SSID',
			  dbus_interface=dbus.PROPERTIES_IFACE)
	ssid = byte_array_to_string(val)

	val = net_obj.Get(WPAS_DBUS_BSS_INTERFACE, 'WPA',
			  dbus_interface=dbus.PROPERTIES_IFACE)
	wpa = "no"
	if len(val["KeyMgmt"]) > 0:
		wpa = "yes"
	val = net_obj.Get(WPAS_DBUS_BSS_INTERFACE, 'RSN',
			  dbus_interface=dbus.PROPERTIES_IFACE)
	wpa2 = "no"
	if len(val["KeyMgmt"]) > 0:
		wpa2 = "yes"
	freq = net_obj.Get(WPAS_DBUS_BSS_INTERFACE, 'Frequency',
			   dbus_interface=dbus.PROPERTIES_IFACE)
	signal = net_obj.Get(WPAS_DBUS_BSS_INTERFACE, 'Signal',
			     dbus_interface=dbus.PROPERTIES_IFACE)
	val = net_obj.Get(WPAS_DBUS_BSS_INTERFACE, 'Rates',
			  dbus_interface=dbus.PROPERTIES_IFACE)
	if len(val) > 0:
		maxrate = val[0] / 1000000
	else:
		maxrate = 0

	print "  %s  ::  ssid='%s'  wpa=%s  wpa2=%s  signal=%d  rate=%d  freq=%d" % (bssid, ssid, wpa, wpa2, signal, maxrate, freq)

def scanDone(success):
	print "Scan done: success=%s" % success
	
	res = if_obj.Get(WPAS_DBUS_INTERFACES_INTERFACE, 'BSSs',
			 dbus_interface=dbus.PROPERTIES_IFACE)

	print "Scanned wireless networks:"
	for opath in res:
		print opath
		showBss(opath)

def bssAdded(bss, properties):
	print "BSS added: %s" % (bss)
	showBss(bss)

def bssRemoved(bss):
	print "BSS removed: %s" % (bss)

def main():
	dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
	global bus
	bus = dbus.SystemBus()
	wpas_obj = bus.get_object(WPAS_DBUS_SERVICE, WPAS_DBUS_OPATH)

	if len(sys.argv) != 2:
		list_interfaces(wpas_obj)
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

	ifname = sys.argv[1]

	# See if wpa_supplicant already knows about this interface
	path = None
	try:
		path = wpas.GetInterface(ifname)
	except dbus.DBusException, exc:
		if not str(exc).startswith("fi.w1.wpa_supplicant1.InterfaceUnknown:"):
			raise exc
		try:
			path = wpas.CreateInterface({'Ifname': ifname, 'Driver': 'test'})
			time.sleep(1)

		except dbus.DBusException, exc:
			if not str(exc).startswith("fi.w1.wpa_supplicant1.InterfaceExists:"):
				raise exc

	global if_obj
	if_obj = bus.get_object(WPAS_DBUS_SERVICE, path)
	global iface
	iface = dbus.Interface(if_obj, WPAS_DBUS_INTERFACES_INTERFACE)
	iface.Scan({'Type': 'active'})

	gobject.MainLoop().run()

	wpas.RemoveInterface(dbus.ObjectPath(path))

if __name__ == "__main__":
	main()

