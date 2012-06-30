OZWPAN USB Host Controller Driver
---------------------------------
This driver is a USB HCD driver that does not have an associated a physical
device but instead uses Wi-Fi to communicate with the wireless peripheral.
The USB requests are converted into a layer 2 network protocol and transmitted
on the network using an ethertype (0x892e) regestered to Ozmo Device Inc.
This driver is compatible with existing wireless devices that use Ozmo Devices
technology.

To operate the driver must be bound to a suitable network interface. This can
be done when the module is loaded (specifying the name of the network interface
as a parameter - e.g. 'insmod ozwpan g_net_dev=go0') or can be bound after
loading using an ioctl call. See the ozappif.h file and the ioctls
OZ_IOCTL_ADD_BINDING and OZ_IOCTL_REMOVE_BINDING.

The devices connect to the host use Wi-Fi Direct so a network card that supports
Wi-Fi direct is required. A recent version (0.8.x or later) version of the
wpa_supplicant can be used to setup the network interface to create a persistent
autonomous group (for older pre-WFD peripherals) or put in a listen state to
allow group negotiation to occur for more recent devices that support WFD.

The protocol used over the network does not directly mimic the USB bus
transactions as this would be rather busy and inefficient. Instead the chapter 9
requests are converted into a request/response pair of messages. (See
ozprotocol.h for data structures used in the protocol).
