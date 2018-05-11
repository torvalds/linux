==================
Device connections
==================

Introduction
------------

Devices often have connections to other devices that are outside of the direct
child/parent relationship. A serial or network communication controller, which
could be a PCI device, may need to be able to get a reference to its PHY
component, which could be attached for example to the I2C bus. Some device
drivers need to be able to control the clocks or the GPIOs for their devices,
and so on.

Device connections are generic descriptions of any type of connection between
two separate devices.

Device connections alone do not create a dependency between the two devices.
They are only descriptions which are not tied to either of the devices directly.
A dependency between the two devices exists only if one of the two endpoint
devices requests a reference to the other. The descriptions themselves can be
defined in firmware (not yet supported) or they can be built-in.

Usage
-----

Device connections should exist before device ``->probe`` callback is called for
either endpoint device in the description. If the connections are defined in
firmware, this is not a problem. It should be considered if the connection
descriptions are "built-in", and need to be added separately.

The connection description consists of the names of the two devices with the
connection, i.e. the endpoints, and unique identifier for the connection which
is needed if there are multiple connections between the two devices.

After a description exists, the devices in it can request reference to the other
endpoint device, or they can request the description itself.

API
---

.. kernel-doc:: drivers/base/devcon.c
   : functions: device_connection_find_match device_connection_find device_connection_add device_connection_remove
