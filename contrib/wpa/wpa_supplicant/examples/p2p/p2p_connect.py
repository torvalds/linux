#!/usr/bin/python
# Tests p2p_connect
# Will try to connect to another peer
# and form a group
######### MAY NEED TO RUN AS SUDO #############

import dbus
import sys, os
import time
import gobject
import getopt
from dbus.mainloop.glib import DBusGMainLoop


def usage():
	print "Usage:"
	print "  %s -i <interface_name> -m <wps_method> \ " \
		% sys.argv[0]
	print "		-a <addr> [-p <pin>] [-g <go_intent>] \ "
	print "  		[-w <wpas_dbus_interface>]"
	print "Options:"
	print "  -i = interface name"
	print "  -m = wps method"
	print "  -a = peer address"
	print "  -p = pin number (8 digits)"
	print "  -g = group owner intent"
	print "  -w = wpas dbus interface = fi.w1.wpa_supplicant1"
	print "Example:"
	print "  %s -i wlan0 -a 0015008352c0 -m display -p 12345670" % sys.argv[0]


# Required Signals
def GONegotiationSuccess(status):
	print "Go Negotiation Success"

def GONegotiationFailure(status):
	print 'Go Negotiation Failed. Status:'
	print format(status)
	os._exit(0)

def GroupStarted(properties):
	if properties.has_key("group_object"):
		print 'Group Formation Complete %s' \
			% properties["group_object"]
	os._exit(0)

def WpsFailure(status, etc):
	print "WPS Authentication Failure".format(status)
	print etc
	os._exit(0)

class P2P_Connect():
	# Needed Variables
	global bus
	global wpas_object
	global interface_object
	global p2p_interface
	global ifname
	global wpas
	global wpas_dbus_interface
	global timeout
	global path
	global wps_method
	global go_intent
	global addr
	global pin

	# Dbus Paths
	global wpas_dbus_opath
	global wpas_dbus_interfaces_opath
	global wpas_dbus_interfaces_interface
	global wpas_dbus_interfaces_p2pdevice

	# Dictionary of Arguements
	global p2p_connect_arguements

	# Constructor
	def __init__(self,ifname,wpas_dbus_interface,addr,
					pin,wps_method,go_intent):
		# Initializes variables and threads
		self.ifname = ifname
		self.wpas_dbus_interface = wpas_dbus_interface
		self.wps_method = wps_method
		self.go_intent = go_intent
		self.addr = addr
		self.pin = pin

		# Generating interface/object paths
		self.wpas_dbus_opath = \
			"/" + self.wpas_dbus_interface.replace(".","/")
		self.wpas_wpas_dbus_interfaces_opath = \
			self.wpas_dbus_opath + "/Interfaces"
		self.wpas_dbus_interfaces_interface = \
			self.wpas_dbus_interface + ".Interface"
		self.wpas_dbus_interfaces_p2pdevice = \
			self.wpas_dbus_interfaces_interface + ".P2PDevice"

		# Getting interfaces and objects
		DBusGMainLoop(set_as_default=True)
		self.bus = dbus.SystemBus()
		self.wpas_object = self.bus.get_object(
				self.wpas_dbus_interface,
				self.wpas_dbus_opath)
		self.wpas = dbus.Interface(
				self.wpas_object, self.wpas_dbus_interface)

		# See if wpa_supplicant already knows about this interface
		self.path = None
		try:
			self.path = self.wpas.GetInterface(ifname)
		except:
			if not str(exc).startswith(
				self.wpas_dbus_interface + \
				".InterfaceUnknown:"):
				raise exc
			try:
				path = self.wpas.CreateInterface(
					{'Ifname': ifname, 'Driver': 'test'})
				time.sleep(1)

			except dbus.DBusException, exc:
				if not str(exc).startswith(
					self.wpas_dbus_interface + \
					".InterfaceExists:"):
					raise exc

		# Get Interface and objects
		self.interface_object = self.bus.get_object(
				self.wpas_dbus_interface,self.path)
		self.p2p_interface = dbus.Interface(
				self.interface_object,
				self.wpas_dbus_interfaces_p2pdevice)

		# Add signals
		self.bus.add_signal_receiver(GONegotiationSuccess,
			dbus_interface=self.wpas_dbus_interfaces_p2pdevice,
			signal_name="GONegotiationSuccess")
		self.bus.add_signal_receiver(GONegotiationFailure,
			dbus_interface=self.wpas_dbus_interfaces_p2pdevice,
			signal_name="GONegotiationFailure")
		self.bus.add_signal_receiver(GroupStarted,
			dbus_interface=self.wpas_dbus_interfaces_p2pdevice,
			signal_name="GroupStarted")
		self.bus.add_signal_receiver(WpsFailure,
			dbus_interface=self.wpas_dbus_interfaces_p2pdevice,
			signal_name="WpsFailed")


	#Constructing all the arguements needed to connect
	def constructArguements(self):
		# Adding required arguements
		self.p2p_connect_arguements = {'wps_method':self.wps_method,
			'peer':dbus.ObjectPath(self.path+'/Peers/'+self.addr)}

		# Display requires a pin, and a go intent of 15
		if (self.wps_method == 'display'):
			if (self.pin != None):
				self.p2p_connect_arguements.update({'pin':self.pin})
			else:
				print "Error:\n  Pin required for wps_method=display"
				usage()
				quit()

			if (self.go_intent != None and int(self.go_intent) != 15):
				print "go_intent overwritten to 15"

			self.go_intent = '15'

		# Keypad requires a pin, and a go intent of less than 15
		elif (self.wps_method == 'keypad'):
			if (self.pin != None):
				self.p2p_connect_arguements.update({'pin':self.pin})
			else:
				print "Error:\n  Pin required for wps_method=keypad"
				usage()
				quit()

			if (self.go_intent != None and int(self.go_intent) == 15):
				error = "Error :\n Group Owner intent cannot be" + \
					" 15 for wps_method=keypad"
				print error
				usage()
				quit()

		# Doesn't require pin
		# for ./wpa_cli, p2p_connect [mac] [pin#], wps_method=keypad
		elif (self.wps_method == 'pin'):
			if (self.pin != None):
				print "pin ignored"

		# No pin is required for pbc so it is ignored
		elif (self.wps_method == 'pbc'):
			if (self.pin != None):
				print "pin ignored"

		else:
			print "Error:\n  wps_method not supported or does not exist"
			usage()
			quit()

		# Go_intent is optional for all arguements
		if (self.go_intent != None):
			self.p2p_connect_arguements.update(
				{'go_intent':dbus.Int32(self.go_intent)})

	# Running p2p_connect
	def run(self):
		try:
			result_pin = self.p2p_interface.Connect(
				self.p2p_connect_arguements)

		except dbus.DBusException, exc:
				raise exc

		if (self.wps_method == 'pin' and \
		not self.p2p_connect_arguements.has_key('pin') ):
			print "Connect return with pin value of %d " % int(result_pin)
		gobject.MainLoop().run()

if __name__ == "__main__":

	# Required
	interface_name = None
	wps_method = None
	addr = None

	# Conditionally optional
	pin = None

	# Optional
	wpas_dbus_interface = 'fi.w1.wpa_supplicant1'
	go_intent = None

	# Using getopts to handle options
	try:
		options, args = getopt.getopt(sys.argv[1:],"hi:m:a:p:g:w:")

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
		# WPS Method
		elif (key == "-m"):
			wps_method = value
		# Address
		elif (key == "-a"):
			addr = value
		# Pin
		elif (key == "-p"):
			pin = value
		# Group Owner Intent
		elif (key == "-g"):
			go_intent = value
		# Dbus interface
		elif (key == "-w"):
			wpas_dbus_interface = value
		else:
			assert False, "unhandled option"

	# Required Arguements check
	if (interface_name == None or wps_method == None or addr == None):
		print "Error:\n  Required arguements not specified"
		usage()
		quit()

	# Group Owner Intent Check
	if (go_intent != None and (int(go_intent) > 15 or int(go_intent) < 0) ):
		print "Error:\n  Group Owner Intent must be between 0 and 15 inclusive"
		usage()
		quit()

	# Pin Check
	if (pin != None and len(pin) != 8):
		print "Error:\n  Pin is not 8 digits"
		usage()
		quit()

	try:
		p2p_connect_test = P2P_Connect(interface_name,wpas_dbus_interface,
			addr,pin,wps_method,go_intent)

	except:
		print "Error:\n  Invalid Arguements"
		usage()
		quit()

	p2p_connect_test.constructArguements()
	p2p_connect_test.run()

	os._exit(0)
