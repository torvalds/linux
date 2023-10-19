==============================
LAN9303 Ethernet switch driver
==============================

The LAN9303 is a three port 10/100 Mbps ethernet switch with integrated phys for
the two external ethernet ports. The third port is an RMII/MII interface to a
host master network interface (e.g. fixed link).


Driver details
==============

The driver is implemented as a DSA driver, see ``Documentation/networking/dsa/dsa.rst``.

See ``Documentation/devicetree/bindings/net/dsa/lan9303.txt`` for device tree
binding.

The LAN9303 can be managed both via MDIO and I2C, both supported by this driver.

At startup the driver configures the device to provide two separate network
interfaces (which is the default state of a DSA device). Due to HW limitations,
no HW MAC learning takes place in this mode.

When both user ports are joined to the same bridge, the normal HW MAC learning
is enabled. This means that unicast traffic is forwarded in HW. Broadcast and
multicast is flooded in HW. STP is also supported in this mode. The driver
support fdb/mdb operations as well, meaning IGMP snooping is supported.

If one of the user ports leave the bridge, the ports goes back to the initial
separated operation.


Driver limitations
==================

 - Support for VLAN filtering is not implemented
 - The HW does not support VLAN-specific fdb entries
