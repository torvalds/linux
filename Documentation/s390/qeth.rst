=============================
IBM s390 QDIO Ethernet Driver
=============================

OSA and HiperSockets Bridge Port Support
========================================

Uevents
-------

To generate the events the device must be assigned a role of either
a primary or a secondary Bridge Port. For more information, see
"z/VM Connectivity, SC24-6174".

When run on an OSA or HiperSockets Bridge Capable Port hardware, and the state
of some configured Bridge Port device on the channel changes, a udev
event with ACTION=CHANGE is emitted on behalf of the corresponding
ccwgroup device. The event has the following attributes:

BRIDGEPORT=statechange
  indicates that the Bridge Port device changed
  its state.

ROLE={primary|secondary|none}
  the role assigned to the port.

STATE={active|standby|inactive}
  the newly assumed state of the port.

When run on HiperSockets Bridge Capable Port hardware with host address
notifications enabled, a udev event with ACTION=CHANGE is emitted.
It is emitted on behalf of the corresponding ccwgroup device when a host
or a VLAN is registered or unregistered on the network served by the device.
The event has the following attributes:

BRIDGEDHOST={reset|register|deregister|abort}
  host address
  notifications are started afresh, a new host or VLAN is registered or
  deregistered on the Bridge Port HiperSockets channel, or address
  notifications are aborted.

VLAN=numeric-vlan-id
  VLAN ID on which the event occurred. Not included
  if no VLAN is involved in the event.

MAC=xx:xx:xx:xx:xx:xx
  MAC address of the host that is being registered
  or deregistered from the HiperSockets channel. Not reported if the
  event reports the creation or destruction of a VLAN.

NTOK_BUSID=x.y.zzzz
  device bus ID (CSSID, SSID and device number).

NTOK_IID=xx
  device IID.

NTOK_CHPID=xx
  device CHPID.

NTOK_CHID=xxxx
  device channel ID.

Note that the `NTOK_*` attributes refer to devices other than  the one
connected to the system on which the OS is running.
