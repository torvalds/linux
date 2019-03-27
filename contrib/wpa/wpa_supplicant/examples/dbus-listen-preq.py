#!/usr/bin/python

import dbus
import sys
import time
import gobject
from dbus.mainloop.glib import DBusGMainLoop

WPAS_DBUS_SERVICE = "fi.w1.wpa_supplicant1"
WPAS_DBUS_INTERFACE = "fi.w1.wpa_supplicant1"
WPAS_DBUS_OPATH = "/fi/w1/wpa_supplicant1"
WPAS_DBUS_INTERFACES_INTERFACE = "fi.w1.wpa_supplicant1.Interface"

def usage():
	print "Usage: %s <ifname>" % sys.argv[0]
	print "Press Ctrl-C to stop"

def ProbeRequest(args):
	if 'addr' in args:
		print '%.2x:%.2x:%.2x:%.2x:%.2x:%.2x' % tuple(args['addr']),
	if 'dst' in args:
		print '-> %.2x:%.2x:%.2x:%.2x:%.2x:%.2x' % tuple(args['dst']),
	if 'bssid' in args:
		print '(bssid %.2x:%.2x:%.2x:%.2x:%.2x:%.2x)' % tuple(args['dst']),
	if 'signal' in args:
		print 'signal:%d' % args['signal'],
	if 'ies' in args:
		print 'have IEs (%d bytes)' % len(args['ies']),
        print ''

if __name__ == "__main__":
	global bus
	global wpas_obj
	global if_obj
	global p2p_iface

	dbus.mainloop.glib.DBusGMainLoop(set_as_default=True)

	bus = dbus.SystemBus()
	wpas_obj = bus.get_object(WPAS_DBUS_SERVICE, WPAS_DBUS_OPATH)

	# Print list of i/f if no one is specified
	if (len(sys.argv) < 2)  :
		usage()
		sys.exit(0)

	wpas = dbus.Interface(wpas_obj, WPAS_DBUS_INTERFACE)

	ifname = sys.argv[1]

	path = wpas.GetInterface(ifname)

	if_obj = bus.get_object(WPAS_DBUS_SERVICE, path)
	iface = dbus.Interface(if_obj, WPAS_DBUS_INTERFACES_INTERFACE)

	bus.add_signal_receiver(ProbeRequest,
				dbus_interface=WPAS_DBUS_INTERFACES_INTERFACE,
				signal_name="ProbeRequest")

	iface.SubscribeProbeReq()

	gobject.MainLoop().run()
