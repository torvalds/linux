#!/usr/bin/python
# Tests P2P_Find
# Will listen
# Then Program will exit
######### MAY NEED TO RUN AS SUDO #############

import dbus
import sys, os
import time
import gobject
import threading
import getopt
from dbus.mainloop.glib import DBusGMainLoop

def usage():
	print "Usage:"
	print "  %s -i <interface_name> [-t <timeout>] \ " \
		% sys.argv[0]
	print "  		[-w <wpas_dbus_interface>]"
	print "Options:"
	print "  -i = interface name"
	print "  -t = timeout = 0s (infinite)"
	print "  -w = wpas dbus interface = fi.w1.wpa_supplicant1"
	print "Example:"
	print "  %s -i wlan0 -t 5" % sys.argv[0]

# Required Signals
def p2pStateChange(status):
	print status

class P2P_Listen(threading.Thread):
	# Needed Variables
	global bus
	global wpas_object
	global interface_object
	global p2p_interface
	global interface_name
	global wpas
	global wpas_dbus_interface
	global path
	global timeout

	# Dbus Paths
	global wpas_dbus_opath
	global wpas_dbus_interfaces_opath
	global wpas_dbus_interfaces_interface
	global wpas_dbus_interfaces_p2pdevice

	# Constructor
	def __init__(self,interface_name,wpas_dbus_interface,timeout):
		# Initializes variables and threads
		self.timeout = int(timeout)
		self.interface_name = interface_name
		self.wpas_dbus_interface = wpas_dbus_interface

		# Initializes thread and daemon allows for ctrl-c kill
		threading.Thread.__init__(self)
		self.daemon = True

		# Generating interface/object paths
		self.wpas_dbus_opath = "/" + \
				self.wpas_dbus_interface.replace(".","/")
		self.wpas_wpas_dbus_interfaces_opath = self.wpas_dbus_opath + \
				"/Interfaces"
		self.wpas_dbus_interfaces_interface = \
				self.wpas_dbus_interface + ".Interface"
		self.wpas_dbus_interfaces_p2pdevice = \
				self.wpas_dbus_interfaces_interface \
				+ ".P2PDevice"

		# Getting interfaces and objects
		DBusGMainLoop(set_as_default=True)
		self.bus = dbus.SystemBus()
		self.wpas_object = self.bus.get_object(
				self.wpas_dbus_interface,
				self.wpas_dbus_opath)
		self.wpas = dbus.Interface(self.wpas_object,
				self.wpas_dbus_interface)

		# Try to see if supplicant knows about interface
		# If not, throw an exception
		try:
			self.path = self.wpas.GetInterface(
					self.interface_name)
		except dbus.DBusException, exc:
			error = 'Error:\n  Interface ' + self.interface_name \
				+ ' was not found'
			print error
			usage()
			os._exit(0)

		self.interface_object = self.bus.get_object(
				self.wpas_dbus_interface, self.path)
		self.p2p_interface = dbus.Interface(self.interface_object,
				self.wpas_dbus_interfaces_p2pdevice)

		self.bus.add_signal_receiver(p2pStateChange,
			dbus_interface=self.wpas_dbus_interfaces_p2pdevice,
			signal_name="P2PStateChanged")

	# Run p2p_find
	def run(self):
		# Sets up p2p_listen
		self.p2p_interface.Listen(int(self.timeout))

		# Allows other threads to keep working while MainLoop runs
		# Required for timeout implementation
		gobject.MainLoop().get_context().iteration(True)
		gobject.threads_init()
		gobject.MainLoop().run()

if __name__ == "__main__":

	# Defaults for optional inputs
	timeout = 0
	wpas_dbus_interface = 'fi.w1.wpa_supplicant1'

	# interface_name is required
	interface_name = None

	# Using getopts to handle options
	try:
		options, args = getopt.getopt(sys.argv[1:],"hi:t:w:")

	except getopt.GetoptError:
		usage()
		quit()

	# If theres a switch, override default option
	for key, value in options:
		# Help
		if (key == "-h"):
			usage()
			quit()
		# Interface Name
		elif (key == "-i"):
			interface_name = value
		# Timeout
		elif (key == "-t"):
			if ( int(value) >= 0):
				timeout = value
			else:
				print "Error:\n  Timeout cannot be negative"
				usage()
				quit()
		# Dbus interface
		elif (key == "-w"):
			wpas_dbus_interface = value
		else:
			assert False, "unhandled option"

	# Interface name is required and was not given
	if (interface_name == None):
		print "Error:\n  interface_name is required"
		usage()
		quit()

	# Constructor
	try:
		p2p_listen_test = P2P_Listen(interface_name, wpas_dbus_interface, timeout)

	except:
		print "Error:\n  Invalid wpas_dbus_interface"
		usage()
		quit()

	# Start P2P_Find
	p2p_listen_test.start()

	try:
		# If timeout is 0, then run forever
		if (int(p2p_listen_test.timeout) == 0):
			while(True):
				pass
		# Else sleep for (timeout)
		else:
			time.sleep(int(p2p_listen_test.timeout))

	except:
		pass

	quit()
