#!/usr/bin/python
# Tests p2p_invite
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
	print "  %s -i <interface_name> -a <addr> \ " \
		% sys.argv[0]
	print "		[-o <persistent_group_object>] [-w <wpas_dbus_interface>]"
	print "Options:"
	print "  -i = interface name"
	print "  -a = address of peer"
	print "  -o = persistent group object path"
	print "  -w = wpas dbus interface = fi.w1.wpa_supplicant1"
	print "Example:"
	print "  %s -i p2p-wlan0-0 -a 00150083523c" % sys.argv[0]

# Required Signals
def InvitationResult(invite_result):
	print "Inviation Result signal :"
	status = invite_result['status']
	print "status = ", status
	if invite_result.has_key('BSSID'):
		bssid = invite_result['BSSID']
		print "BSSID = ", hex(bssid[0]) , ":" , \
		 hex(bssid[1]) , ":" , hex(bssid[2]) , ":", \
		 hex(bssid[3]) , ":" , hex(bssid[4]) , ":" , \
		hex(bssid[5])
	os._exit(0)

class P2P_Invite (threading.Thread):
	# Needed Variables
	global bus
	global wpas_object
	global interface_object
	global p2p_interface
	global interface_name
	global wpas
	global wpas_dbus_interface
	global path
	global addr
	global persistent_group_object

	# Dbus Paths
	global wpas_dbus_opath
	global wpas_dbus_interfaces_opath
	global wpas_dbus_interfaces_interface
	global wpas_dbus_interfaces_p2pdevice

	# Arguements
	global P2PDictionary

	# Constructor
	def __init__(self,interface_name,wpas_dbus_interface,addr,
						persistent_group_object):
		# Initializes variables and threads
		self.interface_name = interface_name
		self.wpas_dbus_interface = wpas_dbus_interface
		self.addr = addr
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
		self.bus.add_signal_receiver(InvitationResult,
			dbus_interface=self.wpas_dbus_interfaces_p2pdevice,
			signal_name="InvitationResult")

	# Sets up p2p_invite dictionary
	def constructArguements(self):
		self.P2PDictionary = \
			{'peer':dbus.ObjectPath(self.path+'/Peers/'+self.addr)}
		if (self.persistent_group_object != None):
			self.P2PDictionary.update({"persistent_group_object":
					self.persistent_group_object})

	# Run p2p_invite
	def run(self):
		try:
			self.p2p_interface.Invite(self.P2PDictionary)

		except:
			print "Error:\n  Invalid Arguements"
			usage()
			os._exit(0)

		# Allows other threads to keep working while MainLoop runs
		# Required for timeout implementation
		gobject.MainLoop().get_context().iteration(True)
		gobject.threads_init()
		gobject.MainLoop().run()

if __name__ == "__main__":
	# Defaults for optional inputs
	addr = None
	persistent_group_object = None
	wpas_dbus_interface = 'fi.w1.wpa_supplicant1'

	# interface_name is required
	interface_name = None

	# Using getopts to handle options
	try:
		options, args = getopt.getopt(sys.argv[1:],"hi:o:w:a:")

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
		elif (key == "-a"):
			addr = value
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

	if (addr == None):
		print "Error:\n  peer address is required"
		usage()
		quit()

	try:
		p2p_invite_test = \
			P2P_Invite(interface_name,wpas_dbus_interface,
					addr,persistent_group_object)
	except:
		print "Error:\n  Invalid Arguements"
		usage()
		os._exit(1)

	p2p_invite_test.constructArguements()
	p2p_invite_test.start()
	time.sleep(10)
	print "Error:\n  p2p_invite timed out"
	os._exit(0)
