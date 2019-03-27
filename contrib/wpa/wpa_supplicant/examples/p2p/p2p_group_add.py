#!/usr/bin/python
# Tests p2p_group_add
######### MAY NEED TO RUN AS SUDO #############

import dbus
import sys, os
import time
import gobject
import getopt
import threading
from dbus.mainloop.glib import DBusGMainLoop

def usage():
	print "Usage:"
	print "  %s -i <interface_name> [-p <persistent>] \ " \
		% sys.argv[0]
	print "		[-f <frequency>] [-o <group_object_path>] \ "
	print "  		[-w <wpas_dbus_interface>]"
	print "Options:"
	print "  -i = interface name"
	print "  -p = persistant group = 0 (0=false, 1=true)"
	print "  -f = frequency"
	print "  -o = persistent group object path"
	print "  -w = wpas dbus interface = fi.w1.wpa_supplicant1"
	print "Example:"
	print "  %s -i wlan0" % sys.argv[0]

# Required Signals
def GroupStarted(properties):
	if properties.has_key("group_object"):
		print 'Group Formation Complete %s' \
			% properties["group_object"]
	os._exit(0)

def WpsFailure(status, etc):
	print "WPS Authentication Failure".format(status)
	print etc
	os._exit(0)

class P2P_Group_Add (threading.Thread):
	# Needed Variables
	global bus
	global wpas_object
	global interface_object
	global p2p_interface
	global interface_name
	global wpas
	global wpas_dbus_interface
	global path
	global persistent
	global frequency
	global persistent_group_object

	# Dbus Paths
	global wpas_dbus_opath
	global wpas_dbus_interfaces_opath
	global wpas_dbus_interfaces_interface
	global wpas_dbus_interfaces_p2pdevice

	# Arguements
	global P2PDictionary

	# Constructor
	def __init__(self,interface_name,wpas_dbus_interface,persistent,frequency,
						persistent_group_object):
		# Initializes variables and threads
		self.interface_name = interface_name
		self.wpas_dbus_interface = wpas_dbus_interface
		self.persistent = persistent
		self.frequency = frequency
		self.persistent_group_object = persistent_group_object

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

		#Adds listeners
		self.bus.add_signal_receiver(GroupStarted,
			dbus_interface=self.wpas_dbus_interfaces_p2pdevice,
			signal_name="GroupStarted")
		self.bus.add_signal_receiver(WpsFailure,
			dbus_interface=self.wpas_dbus_interfaces_p2pdevice,
			signal_name="WpsFailed")

		# Sets up p2p_group_add dictionary
	def constructArguements(self):
		self.P2PDictionary = {'persistent':self.persistent}

		if (self.frequency != None):
			if (int(self.frequency) > 0):
				self.P2PDictionary.update({'frequency':int(self.frequency)})
			else:
				print "Error:\n  Frequency must be greater than 0"
				usage()
				os._exit(0)

		if (self.persistent_group_object != None):
			self.P2PDictionary.update({'persistent_group_object':
						self.persistent_group_object})

	# Run p2p_group_remove
	def run(self):
		try:
			self.p2p_interface.GroupAdd(self.P2PDictionary)

		except:
			print "Error:\n  Could not preform group add"
			usage()
			os._exit(0)

		# Allows other threads to keep working while MainLoop runs
		# Required for timeout implementation
		gobject.MainLoop().get_context().iteration(True)
		gobject.threads_init()
		gobject.MainLoop().run()


if __name__ == "__main__":

	# Defaults for optional inputs
	# 0 = false, 1 = true
	persistent = False
	frequency = None
	persistent_group_object = None
	wpas_dbus_interface = 'fi.w1.wpa_supplicant1'

	# interface_name is required
	interface_name = None

	# Using getopts to handle options
	try:
		options, args = getopt.getopt(sys.argv[1:],"hi:p:f:o:w:")

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
		elif (key == "-p"):
			if (value == '0'):
				persistent = False
			elif (value == '1'):
				persistent = True
			else:
				print "Error:\n  Persistent can only be 1 or 0"
				usage()
				os._exit(0)
		# Frequency
		elif (key == "-f"):
			frequency = value
		# Persistent group object path
		elif (key == "-o"):
			persistent_group_object = value
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

	try:
		p2p_group_add_test = P2P_Group_Add(interface_name,wpas_dbus_interface,
					persistent,frequency,persistent_group_object)
	except:
		print "Error:\n  Invalid Arguements"

	p2p_group_add_test.constructArguements()
	p2p_group_add_test.start()
	time.sleep(5)
	print "Error:\n  Group formation timed out"
	os._exit(0)
