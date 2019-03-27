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
WPAS_DBUS_NETWORK_INTERFACE = "fi.w1.wpa_supplicant1.Network"

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

def interfaceAdded(interface, properties):
	print "InterfaceAdded(%s): Ifname=%s" % (interface, properties['Ifname'])

def interfaceRemoved(interface):
	print "InterfaceRemoved(%s)" % (interface)

def propertiesChanged(properties):
	for i in properties:
		print "PropertiesChanged: %s=%s" % (i, properties[i])

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
	if val != None:
		wpa = "yes"
	val = net_obj.Get(WPAS_DBUS_BSS_INTERFACE, 'RSN',
			  dbus_interface=dbus.PROPERTIES_IFACE)
	wpa2 = "no"
	if val != None:
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
	gobject.MainLoop().quit()
	print "Scan done: success=%s" % success

def scanDone2(success, path=None):
	print "Scan done: success=%s [path=%s]" % (success, path)

def bssAdded(bss, properties):
	print "BSS added: %s" % (bss)
	showBss(bss)

def bssRemoved(bss):
	print "BSS removed: %s" % (bss)

def blobAdded(blob):
	print "BlobAdded(%s)" % (blob)

def blobRemoved(blob):
	print "BlobRemoved(%s)" % (blob)

def networkAdded(network, properties):
	print "NetworkAdded(%s)" % (network)

def networkRemoved(network):
	print "NetworkRemoved(%s)" % (network)

def networkSelected(network):
	print "NetworkSelected(%s)" % (network)

def propertiesChangedInterface(properties):
	for i in properties:
		print "PropertiesChanged(interface): %s=%s" % (i, properties[i])

def propertiesChangedBss(properties):
	for i in properties:
		print "PropertiesChanged(BSS): %s=%s" % (i, properties[i])

def propertiesChangedNetwork(properties):
	for i in properties:
		print "PropertiesChanged(Network): %s=%s" % (i, properties[i])

def main():
	dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)
	global bus
	bus = dbus.SystemBus()
	wpas_obj = bus.get_object(WPAS_DBUS_SERVICE, WPAS_DBUS_OPATH)

	if len(sys.argv) != 2:
		list_interfaces(wpas_obj)
		os._exit(1)

	wpas = dbus.Interface(wpas_obj, WPAS_DBUS_INTERFACE)
	bus.add_signal_receiver(interfaceAdded,
				dbus_interface=WPAS_DBUS_INTERFACE,
				signal_name="InterfaceAdded")
	bus.add_signal_receiver(interfaceRemoved,
				dbus_interface=WPAS_DBUS_INTERFACE,
				signal_name="InterfaceRemoved")
	bus.add_signal_receiver(propertiesChanged,
				dbus_interface=WPAS_DBUS_INTERFACE,
				signal_name="PropertiesChanged")

	ifname = sys.argv[1]
	path = wpas.GetInterface(ifname)
	if_obj = bus.get_object(WPAS_DBUS_SERVICE, path)
	iface = dbus.Interface(if_obj, WPAS_DBUS_INTERFACES_INTERFACE)
	iface.connect_to_signal("ScanDone", scanDone2,
				path_keyword='path')

	bus.add_signal_receiver(scanDone,
				dbus_interface=WPAS_DBUS_INTERFACES_INTERFACE,
				signal_name="ScanDone",
				path=path)
	bus.add_signal_receiver(bssAdded,
				dbus_interface=WPAS_DBUS_INTERFACES_INTERFACE,
				signal_name="BSSAdded",
				path=path)
	bus.add_signal_receiver(bssRemoved,
				dbus_interface=WPAS_DBUS_INTERFACES_INTERFACE,
				signal_name="BSSRemoved",
				path=path)
	bus.add_signal_receiver(blobAdded,
				dbus_interface=WPAS_DBUS_INTERFACES_INTERFACE,
				signal_name="BlobAdded",
				path=path)
	bus.add_signal_receiver(blobRemoved,
				dbus_interface=WPAS_DBUS_INTERFACES_INTERFACE,
				signal_name="BlobRemoved",
				path=path)
	bus.add_signal_receiver(networkAdded,
				dbus_interface=WPAS_DBUS_INTERFACES_INTERFACE,
				signal_name="NetworkAdded",
				path=path)
	bus.add_signal_receiver(networkRemoved,
				dbus_interface=WPAS_DBUS_INTERFACES_INTERFACE,
				signal_name="NetworkRemoved",
				path=path)
	bus.add_signal_receiver(networkSelected,
				dbus_interface=WPAS_DBUS_INTERFACES_INTERFACE,
				signal_name="NetworkSelected",
				path=path)
	bus.add_signal_receiver(propertiesChangedInterface,
				dbus_interface=WPAS_DBUS_INTERFACES_INTERFACE,
				signal_name="PropertiesChanged",
				path=path)

	bus.add_signal_receiver(propertiesChangedBss,
				dbus_interface=WPAS_DBUS_BSS_INTERFACE,
				signal_name="PropertiesChanged")

	bus.add_signal_receiver(propertiesChangedNetwork,
				dbus_interface=WPAS_DBUS_NETWORK_INTERFACE,
				signal_name="PropertiesChanged")

	gobject.MainLoop().run()

if __name__ == "__main__":
	main()

