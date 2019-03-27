#!/usr/bin/python

import dbus
import sys, os
import time
import gobject

def main():
	bus = dbus.SystemBus()
	wpas_obj = bus.get_object("fi.w1.wpa_supplicant1",
				  "/fi/w1/wpa_supplicant1")
	props = wpas_obj.GetAll("fi.w1.wpa_supplicant1",
				dbus_interface=dbus.PROPERTIES_IFACE)
	print "GetAll(fi.w1.wpa_supplicant1, /fi/w1/wpa_supplicant1):"
	print props

	if len(sys.argv) != 2:
		os._exit(1)

	ifname = sys.argv[1]

	wpas = dbus.Interface(wpas_obj, "fi.w1.wpa_supplicant1")
	path = wpas.GetInterface(ifname)
	if_obj = bus.get_object("fi.w1.wpa_supplicant1", path)
	props = if_obj.GetAll("fi.w1.wpa_supplicant1.Interface",
			      dbus_interface=dbus.PROPERTIES_IFACE)
	print
	print "GetAll(fi.w1.wpa_supplicant1.Interface, %s):" % (path)
	print props

	props = if_obj.GetAll("fi.w1.wpa_supplicant1.Interface.WPS",
			      dbus_interface=dbus.PROPERTIES_IFACE)
	print
	print "GetAll(fi.w1.wpa_supplicant1.Interface.WPS, %s):" % (path)
	print props

	res = if_obj.Get("fi.w1.wpa_supplicant1.Interface", 'BSSs',
			 dbus_interface=dbus.PROPERTIES_IFACE)
	if len(res) > 0:
		bss_obj = bus.get_object("fi.w1.wpa_supplicant1", res[0])
		props = bss_obj.GetAll("fi.w1.wpa_supplicant1.BSS",
				       dbus_interface=dbus.PROPERTIES_IFACE)
		print
		print "GetAll(fi.w1.wpa_supplicant1.BSS, %s):" % (res[0])
		print props

	res = if_obj.Get("fi.w1.wpa_supplicant1.Interface", 'Networks',
			 dbus_interface=dbus.PROPERTIES_IFACE)
	if len(res) > 0:
		net_obj = bus.get_object("fi.w1.wpa_supplicant1", res[0])
		props = net_obj.GetAll("fi.w1.wpa_supplicant1.Network",
				       dbus_interface=dbus.PROPERTIES_IFACE)
		print
		print "GetAll(fi.w1.wpa_supplicant1.Network, %s):" % (res[0])
		print props

if __name__ == "__main__":
	main()

